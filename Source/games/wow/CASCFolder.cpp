/*
 * CASCFolder.cpp
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#include "CASCFolder.h"

#ifndef __CASCLIB_SELF__
  #define __CASCLIB_SELF__
#endif
#include "CascLib.h"

#include <cstring>
#include <locale>
#include <map>
#include <string>
#include <utility>

#include <QByteArrayList>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#ifndef QT_NO_SSL
#include <QSslSocket>
#endif

#include "CASCFile.h"
#include "CDNPrefetch.h"
#include "logger/Logger.h"

namespace
{
  const char * const kUserAgent = "ModelViewer-Midnight";

  // A request that receives nothing for this long is aborted. Qt 5.13 has no transfer timeout of
  // its own (QNetworkRequest::setTransferTimeout came with 5.15), so a timer does it. Data that
  // keeps trickling in holds the request open, but never beyond kRequestDeadlineMs.
  const int kRequestTimeoutMs = 10000;
  const qint64 kRequestDeadlineMs = 15000;

  // No further request to the version service starts after this long. On a network that drops
  // packets silently, every request waits out its whole timeout, and the UI thread waits with it:
  // this way it waits for two of them (about 20 s), never three.
  const qint64 kVersionServiceBudgetMs = 15000;

  // The version service's regional servers, in the order they are asked after the chosen region's
  // own. Each of them serves the same tables, which hold a row for every region.
  const char * const kVersionServiceRegions[] = { "us", "eu", "kr", "tw" };

  // CascLib downloads from the hosts in the Hosts column of the "cdns" row it uses, in that order.
  // Blizzard's eu row names only level3.blizzard.com, so a single host that fails would stop every
  // download. These hosts serve the same files, and all of them answer range requests.
  const char * const kFallbackCdnHosts[] = { "level3.blizzard.com", "us.cdn.blizzard.com",
                                             "eu.cdn.blizzard.com", "cdn.blizzard.com" };

  // wowdev's community list of TACT keys, one "<16 hex key name> <32 hex key>" per line. It knows
  // thousands of keys more than the extraEncryptionKeys.csv shipped with the program, and an
  // online storage is always the newest build, whose encrypted DB2 sections need the newest keys.
  // A real list has many thousands of keys; fewer than kMinTactKeys means something else came.
  const char * const kTactKeysUrl = "https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt";
  const char * const kTactKeysFile = "tactkeys.txt";
  const char * const kTactKeysEtagFile = "tactkeys.txt.etag";
  const size_t kMinTactKeys = 1000;

  // Where the steps of an online setConfig() sit on the progress bar. The download ahead of the
  // open is measured in bytes. CascLib reports its own steps only as named phases; with the
  // archive indexes and ENCODING already downloaded, what remains is reading them from disk and
  // downloading ROOT (50 MB). The enumeration of the file list takes the rest.
  const float kPrefetchEnd = 0.70f;
  const float kIndexesEnd = 0.75f;
  const float kEncoding = 0.75f;
  const float kRoot = 0.80f;
  const float kRootReparsed = 0.87f;
  const float kOpenEnd = 0.90f;

  // LPCTSTR is wchar_t on the Windows build (CASC_UNICODE), char everywhere else.
  std::basic_string<TCHAR> toCascString(const QString & s)
  {
#ifdef _UNICODE
    return s.toStdWString();
#else
    return s.toStdString();
#endif
  }

  // QSaveFile writes to a temporary file next to the target and renames it on commit(), so a
  // cached file is always either the old one or the complete new one.
  bool saveFile(const QString & path, const QByteArray & data)
  {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
  }

  bool haveTls()
  {
#ifndef QT_NO_SSL
    return QSslSocket::supportsSsl();
#else
    return false;
#endif
  }

  // One GET, waited for in a local event loop: initOnline() runs on the UI thread, and a splash
  // screen keeps painting while the loop runs. The request starts at once and wait() may come
  // later, so several requests can be under way at the same time. A timer aborts the request
  // once nothing has arrived for kRequestTimeoutMs, or kRequestDeadlineMs after it started.
  class HttpGet
  {
    public:
      HttpGet(QNetworkAccessManager & manager, const QUrl & url, const QByteArray & etag = QByteArray())
        : m_timedOut(false)
      {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
        // Qt 5 follows no redirect unless told to. Never from HTTPS to plain HTTP, though.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (!etag.isEmpty())
          request.setRawHeader("If-None-Match", etag);
        m_reply = manager.get(request);
        m_started.start();

        m_timer.setSingleShot(true);
        QObject::connect(&m_timer, &QTimer::timeout, m_reply, [this]
        {
          m_timedOut = true;
          m_reply->abort();
        });
        QObject::connect(m_reply, &QNetworkReply::downloadProgress, &m_timer, [this]
        {
          m_timer.start(int(qBound<qint64>(0, kRequestDeadlineMs - m_started.elapsed(), kRequestTimeoutMs)));
        });
        m_timer.start(kRequestTimeoutMs);
      }

      ~HttpGet()
      {
        delete m_reply;
      }

      void wait()
      {
        if (!m_reply->isFinished())
        {
          QEventLoop loop;
          QObject::connect(m_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
          loop.exec();
        }
        m_timer.stop();
      }

      bool ok() const { return m_reply->error() == QNetworkReply::NoError; }
      // The HTTP status, or 0 when no server answered (no network, unknown host, TLS, timeout).
      int status() const { return m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); }
      // Only a successful answer has a body worth reading; an aborted reply is no longer readable.
      QByteArray body() { return ok() ? m_reply->readAll() : QByteArray(); }
      QByteArray etag() const { return m_reply->rawHeader("ETag"); }
      QString error() const
      {
        if (!m_timedOut)
          return m_reply->errorString();
        // The timer is set shorter than kRequestTimeoutMs only when the deadline comes first.
        if (m_timer.interval() < kRequestTimeoutMs)
          return QString("not complete after %1 s").arg(kRequestDeadlineMs / 1000);
        return QString("no answer for %1 s").arg(kRequestTimeoutMs / 1000);
      }

    private:
      HttpGet(const HttpGet &);
      HttpGet & operator=(const HttpGet &);

      QNetworkReply * m_reply;
      QElapsedTimer m_started;
      QTimer m_timer;
      bool m_timedOut;
  };

  // A table of the version service ("versions", "cdns"): the first line names the columns
  // ("Region!STRING:0|BuildConfig!HEX:16|..."), then comes one row per region, keyed by its first
  // column. Lines starting with "##" are comments ("## seqn = ...").
  QList<QByteArray> tableFields(const QByteArray & line)
  {
    return line.trimmed().split('|');
  }

  // The first column whose name starts with prefix, ignoring case; -1 if there is none.
  int findColumn(const QList<QByteArray> & header, const char * prefix)
  {
    for (int i = 0; i < header.size(); i++)
      if (qstrnicmp(header[i].constData(), prefix, uint(strlen(prefix))) == 0)
        return i;
    return -1;
  }

  // The row CascLib uses when it opens the storage (LoadCsvFile): the region's own row, else us,
  // eu, xx, else the first. CascLib only sees rows with as many columns as the header (which
  // leaves out the "##" comments), and it takes the first row of the region but the last us, eu
  // or xx row. An index into lines, or -1 if the table has no rows.
  int chosenRow(const QList<QByteArray> & lines, int keyColumn, const QString & region)
  {
    const int columns = tableFields(lines.first()).size();
    const QByteArray wanted[] = { region.toUtf8(), "us", "eu", "xx" };
    int found[] = { -1, -1, -1, -1 };
    int first = -1;
    for (int i = 1; i < lines.size(); i++)
    {
      const QList<QByteArray> fields = tableFields(lines[i]);
      if (fields.size() != columns || keyColumn >= columns)
        continue;
      if (first < 0)
        first = i;
      for (int k = 0; k < 4; k++)
        if ((found[k] < 0 || k > 0) && fields[keyColumn] == wanted[k])
          found[k] = i;
    }
    for (int k = 0; k < 4; k++)
      if (found[k] >= 0)
        return found[k];
    return first;
  }

  // Whether a download is a table CascLib can use: it must have the columns CascLib reads, under
  // their exact names (CascLib looks them up that way), and at least one row. An error page, a
  // captive portal or a cut-off answer must not replace the copy in the cache.
  bool isVersionTable(const QByteArray & body, const QString & name)
  {
    if (body.isEmpty() || body.size() > 1024 * 1024 || body.contains('\0'))
      return false;
    const QList<QByteArray> lines = body.split('\n');
    const QList<QByteArray> header = tableFields(lines.first());
    const QList<QByteArray> required = (name == "versions")
      ? QList<QByteArray>{ "Region!STRING:0", "BuildConfig!HEX:16", "CDNConfig!HEX:16" }
      : QList<QByteArray>{ "Name!STRING:0", "Path!STRING:0", "Hosts!STRING:0" };
    for (const QByteArray & column : required)
      if (!header.contains(column))
        return false;
    // "versions" also names the build, which initOnlineVersions() needs
    if (name == "versions" && findColumn(header, "VersionsName!") < 0)
      return false;
    for (int i = 1; i < lines.size(); i++)
    {
      const QList<QByteArray> fields = tableFields(lines[i]);
      if (!fields[0].isEmpty() && !fields[0].startsWith("##") && fields.size() == header.size())
        return true;
    }
    return false;
  }

  // Adds kFallbackCdnHosts to the Hosts of the "cdns" row CascLib will use for region, after the
  // row's own hosts and without repeating any. Every other byte stays as it was.
  QByteArray withFallbackHosts(const QByteArray & cdns, const QString & region)
  {
    QList<QByteArray> lines = cdns.split('\n');
    const QList<QByteArray> header = tableFields(lines.first());
    const int nameColumn = header.indexOf("Name!STRING:0");
    const int hostsColumn = header.indexOf("Hosts!STRING:0");
    const int row = (nameColumn >= 0 && hostsColumn >= 0) ? chosenRow(lines, nameColumn, region) : -1;
    if (row < 0)
      return cdns;

    QByteArray line = lines[row];
    const bool carriageReturn = line.endsWith('\r');
    if (carriageReturn)
      line.chop(1);
    QList<QByteArray> fields = line.split('|');
    if (fields.size() <= hostsColumn)
      return cdns;

    QByteArrayList hosts;
    QSet<QByteArray> seen;
    QByteArrayList candidates = fields[hostsColumn].simplified().split(' ');
    for (const char * host : kFallbackCdnHosts)
      candidates << host;
    for (const QByteArray & host : candidates)
    {
      if (!host.isEmpty() && !seen.contains(host.toLower()))
      {
        seen.insert(host.toLower());
        hosts << host;
      }
    }
    fields[hostsColumn] = hosts.join(' ');
    lines[row] = fields.join('|') + (carriageReturn ? "\r" : "");
    return lines.join('\n');
  }

  // Blizzard's version service over HTTPS. CascLib is never left to fetch these tables itself:
  // its only way is plain HTTP on port 1119, which many networks block; there it waits for minutes
  // and then fails, ignoring the copy it has. So initOnline() fetches them into the cache folder,
  // where CascLib reads them.
  class VersionService
  {
    public:
      VersionService(QNetworkAccessManager & manager, const QString & folder, const QString & product,
                     const QString & region)
        : m_manager(manager), m_folder(folder), m_product(product), m_region(region)
      {
        m_clock.start();
      }

      // Fetches one table into the cache folder. False leaves the cached copy as it was.
      bool refresh(const QString & name)
      {
        QByteArray table = fetch(name);
        if (table.isEmpty())
          return false;
        // CascLib takes its download hosts from this file, so the row it will use gets the
        // fallback hosts before the file is saved.
        if (name == "cdns")
          table = withFallbackHosts(table, m_region);
        const QString path = m_folder + QDir::separator() + name;
        if (!saveFile(path, table))
        {
          LOG_ERROR << "CASCFolder: cannot write" << path;
          return false;
        }
        return true;
      }

      // A "cdns" kept from an earlier start may have been fixed up for another region, or not at
      // all when it was written by an older version.
      void addFallbackHostsToCachedCdns()
      {
        const QString path = m_folder + QDir::separator() + "cdns";
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
          return;
        const QByteArray cached = file.readAll();
        file.close();
        const QByteArray updated = withFallbackHosts(cached, m_region);
        if (updated != cached && !saveFile(path, updated))
          LOG_WARNING << "CASCFolder: cannot write" << path;
      }

    private:
      // The chosen region's server first, then us, eu, kr and tw; then the same servers under the
      // legacy path without "v2/products", the one wow.export uses. A server that gave no answer
      // at all is not asked again during this start.
      QByteArray fetch(const QString & name)
      {
        QStringList regions;
        for (const char * region : kVersionServiceRegions)
          if (m_region == region)
            regions << m_region;
        for (const char * region : kVersionServiceRegions)
          if (!regions.contains(region))
            regions << region;

        for (const QString & path : { QString("v2/products/%1/%2"), QString("%1/%2") })
        {
          for (const QString & region : regions)
          {
            const QUrl url(QString("https://%1.version.battle.net/%2").arg(region, path.arg(m_product, name)));
            if (m_unreachable.contains(url.host()))
              continue;
            if (m_clock.elapsed() > kVersionServiceBudgetMs)
            {
              LOG_WARNING << "CASCFolder: no answer from the version service within"
                          << kVersionServiceBudgetMs / 1000 << "s -- giving up on" << name;
              return QByteArray();
            }

            HttpGet get(m_manager, url);
            get.wait();
            const QByteArray body = get.body();
            if (get.ok() && isVersionTable(body, name))
              return body;
            if (get.status() == 0)
              m_unreachable.insert(url.host());
            LOG_WARNING << "CASCFolder:" << url.toString() << "-"
                        << (get.ok() ? QString("not a version table") : get.error());
          }
        }
        return QByteArray();
      }

      QNetworkAccessManager & m_manager;
      const QString m_folder;
      const QString m_product;
      const QString m_region;
      QElapsedTimer m_clock;
      QSet<QString> m_unreachable;
  };

  bool isHex(const QByteArray & text)
  {
    for (char c : text)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return false;
    return true;
  }

  struct TactKey
  {
    quint64 name;
    QByteArray value;   // 32 hex digits, as CascAddStringEncryptionKey() takes it
  };

  // Reads a key list in the format of wowdev's WoW.txt. The format may gain more fields after the
  // first two, so only those two are read; blank lines, comments and anything malformed are skipped.
  std::vector<TactKey> parseTactKeys(const QByteArray & text)
  {
    std::vector<TactKey> keys;
    for (const QByteArray & line : text.split('\n'))
    {
      const QByteArrayList fields = line.simplified().split(' ');
      if (fields.size() < 2 || fields[0].size() != 16 || fields[1].size() != 32 ||
          !isHex(fields[0]) || !isHex(fields[1]))
        continue;
      keys.push_back({ fields[0].toULongLong(nullptr, 16), fields[1] });
    }
    return keys;
  }

  // The ETag of the cached key list, for a conditional request -- but only while that list is
  // usable, so a damaged copy gets replaced instead of confirmed by a 304.
  QByteArray cachedTactKeysEtag(const QString & folder)
  {
    QFile list(folder + QDir::separator() + kTactKeysFile);
    QFile etag(folder + QDir::separator() + kTactKeysEtagFile);
    if (!list.open(QIODevice::ReadOnly) || parseTactKeys(list.readAll()).size() < kMinTactKeys ||
        !etag.open(QIODevice::ReadOnly))
      return QByteArray();
    return etag.readAll().trimmed();
  }

  // Keeps a fresh key list in the cache folder, where addExtraEncryptionKeys() reads it -- on this
  // start and on later ones without network. Nothing here is fatal: whatever fails, the cached
  // list stays.
  void storeTactKeys(HttpGet & get, const QString & folder)
  {
    get.wait();
    if (get.status() == 304)
    {
      LOG_INFO << "CASCFolder: TACT key list unchanged since it was last downloaded";
      return;
    }
    const QByteArray body = get.body();
    const uint count = get.ok() ? uint(parseTactKeys(body).size()) : 0u;
    if (!get.ok() || count < kMinTactKeys)
    {
      LOG_WARNING << "CASCFolder: TACT key list not refreshed -"
                  << (get.ok() ? QString("%1 keys, not a key list").arg(count) : get.error())
                  << "- keeping the cached copy, if any";
      return;
    }

    const QString path = folder + QDir::separator() + kTactKeysFile;
    if (!saveFile(path, body))
    {
      LOG_ERROR << "CASCFolder: cannot write" << path;
      return;
    }
    // The ETag names exactly the list just saved. Without one, the next start fetches it in full.
    const QString etagPath = folder + QDir::separator() + kTactKeysEtagFile;
    const QByteArray etag = get.etag();
    const bool etagSaved = etag.isEmpty() ? (!QFile::exists(etagPath) || QFile::remove(etagPath))
                                          : saveFile(etagPath, etag);
    if (!etagSaved)
      LOG_WARNING << "CASCFolder: cannot update" << etagPath;
    LOG_INFO << "CASCFolder: TACT key list refreshed," << count << "keys";
  }

  // Progress of an online setConfig() as the loading UI sees it: it never moves backwards, and
  // every report answers whether cancelOnlineOpen() has been called.
  class OnlineProgress
  {
    public:
      OnlineProgress(const std::function<void(float)> & callback, const std::atomic<bool> & cancel)
        : m_callback(callback), m_cancel(cancel), m_shown(0.0f)
      {
      }

      // Moves the bar to fraction if that is ahead of it. False once a cancel was requested.
      bool report(float fraction)
      {
        if (m_cancel)
          return false;
        if (fraction > m_shown)
        {
          m_shown = fraction;
          if (m_callback)
            m_callback(fraction);
        }
        return !m_cancel;
      }

      float shown() const { return m_shown; }
      bool cancelled() const { return m_cancel; }

    private:
      const std::function<void(float)> & m_callback;
      const std::atomic<bool> & m_cancel;
      float m_shown;
  };
}

// CascLib reports an online open in phases, which are placed on the bar by kIndexesEnd ..
// kRootReparsed. Returning true makes CascLib abandon the open with ERROR_CANCELLED. It gives no
// progress inside a download: the bar rests while ROOT arrives, and a cancel waits for it.
static bool WINAPI onlineOpenProgress(void * param, CASC_PROGRESS_MSG msg, LPCSTR object, DWORD current, DWORD total)
{
  OnlineProgress & progress = *static_cast<OnlineProgress *>(param);
  float fraction = progress.shown();
  if (msg == CascProgressDownloadingArchiveIndexes && total != 0)
  {
    // 1401 reports, which come within half a second when the indexes are cached: pass on one per
    // thousandth of the bar. A cancel is still answered at every one of them.
    fraction = kPrefetchEnd + (kIndexesEnd - kPrefetchEnd) * (float)current / (float)total;
    if (fraction < progress.shown() + 0.001f)
      return progress.cancelled();
  }
  else if (msg == CascProgressLoadingManifest && object != NULL)
  {
    if (!strcmp(object, "ENCODING"))
      fraction = kEncoding;
    else if (!strcmp(object, "ROOT"))
      fraction = kRoot;
    else if (!strcmp(object, "ROOT (reparsed)"))
      fraction = kRootReparsed;
  }
  return !progress.report(fraction);
}

CASCFolder::CASCFolder()
 : m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS), hStorage(nullptr),
   m_online(false), m_refreshed(false), m_cancel(false)
{

}

void CASCFolder::init(const QString &path)
{
  m_folder = path;

  if(m_folder.endsWith("\\"))
    m_folder.remove(m_folder.size()-1,1);

  initBuildInfo();
}

void CASCFolder::initOnline(const QString & cacheDir, const QString & product, const QString & region,
                            const QString & locale)
{
  m_online = true;
  m_region = region.trimmed().toLower();
  if (m_region.isEmpty())
    m_region = "us";

  // One folder per product: CascLib keeps the build files under the plain names "versions" and
  // "cdns", so wow and wowt in one folder would read each other's build. Regions can share it --
  // both files list every region, and CascLib picks the row by m_region.
  m_folder = QDir::toNativeSeparators(QDir(cacheDir).absoluteFilePath(product));
  if (!QDir().mkpath(m_folder))
  {
    LOG_ERROR << "CASCFolder: cannot create the CDN cache folder" << m_folder;
    m_openError = ERROR_PATH_NOT_FOUND;
    return;
  }

  QNetworkAccessManager manager;
  if (!haveTls())
    LOG_ERROR << "CASCFolder: no TLS support (are the OpenSSL libraries missing?) --"
              << "nothing can be refreshed over HTTPS";

  // The key list comes from GitHub rather than from Blizzard. It downloads while the version
  // service answers, so a network that does not answer costs the wait only once.
  HttpGet keys(manager, QUrl(kTactKeysUrl), cachedTactKeysEtag(m_folder));

  // Either table failing leaves last run's copy in place, which is what lets a start without
  // network open the build that is already cached.
  VersionService service(manager, m_folder, product, m_region);
  const bool versions = service.refresh("versions");
  const bool cdns = service.refresh("cdns");
  m_refreshed = versions && cdns;
  if (!m_refreshed)
    LOG_WARNING << "CASCFolder: version service unreachable -- falling back to the cached build, if any";
  if (!cdns)
    service.addFallbackHostsToCachedCdns();

  storeTactKeys(keys, m_folder);

  initOnlineVersions(product, locale);
}

// The build comes from the "versions" table: VersionsName ("12.1.0.69933") of the row CascLib
// will use when it opens the storage, so the version shown is the build that actually gets opened.
void CASCFolder::initOnlineVersions(const QString & product, const QString & locale)
{
  // CascLib needs both tables in the cache; it fails the open at once without "cdns".
  QFile file(m_folder + QDir::separator() + "versions");
  if (!file.open(QIODevice::ReadOnly) || QFileInfo(m_folder + QDir::separator() + "cdns").size() <= 0)
  {
    LOG_ERROR << "CASCFolder: no build information for" << product
              << "- neither from the version service nor cached in" << m_folder;
    m_openError = ERROR_FILE_NOT_FOUND;
    return;
  }

  const QList<QByteArray> lines = file.readAll().split('\n');
  const QList<QByteArray> header = tableFields(lines.first());
  const int regionColumn = header.indexOf("Region!STRING:0");
  const int versionColumn = findColumn(header, "VersionsName!");
  const int row = (regionColumn >= 0 && versionColumn >= 0) ? chosenRow(lines, regionColumn, m_region) : -1;
  const QList<QByteArray> fields = (row >= 0) ? tableFields(lines[row]) : QList<QByteArray>();
  const QString version = (row >= 0 && fields.size() > versionColumn) ? QString::fromUtf8(fields[versionColumn])
                                                                       : QString();
  if (version.isEmpty())
  {
    LOG_ERROR << "CASCFolder: unreadable versions file in" << m_folder;
    m_openError = ERROR_BAD_FORMAT;
    return;
  }

  core::GameConfig config;
  config.locale = locale.isEmpty() ? QString("enUS") : locale;
  config.version = version;
  config.product = product;
  m_configs.push_back(config);
  LOG_INFO << "config (online)" << config.locale << config.version << product << m_region;
}

// An if-chain, not a switch: off Windows, CascPort.h maps several of these onto one errno value
// (ERROR_FILE_NOT_FOUND and ERROR_PATH_NOT_FOUND are both ENOENT).
QString CASCFolder::errorText(int code)
{
  const DWORD c = (DWORD)code;
  if (c == ERROR_SUCCESS)               return QString();
  if (c == ERROR_FILE_NOT_FOUND)        return "a required file is missing, or every CDN host refused it";
  if (c == ERROR_PATH_NOT_FOUND)        return "folder missing, or the cache folder cannot be created";
  if (c == ERROR_ACCESS_DENIED)         return "access denied (is WoW or the Battle.net updater using the folder?)";
  if (c == ERROR_BAD_FORMAT)            return "unreadable build data, or a download that ended early";
  if (c == ERROR_NETWORK_NOT_AVAILABLE) return "no connection to Blizzard's CDN";
  if (c == ERROR_FILE_CORRUPT)          return "a download does not match its checksum";
  if (c == ERROR_DISK_FULL)             return "the cache folder is full or not writable";
  if (c == ERROR_CAN_NOT_COMPLETE)      return "a download could not be saved in the cache folder";
  if (c == ERROR_NOT_ENOUGH_MEMORY)     return "out of memory";
  if (c == ERROR_CANCELLED)             return "cancelled";
  if (c == ERROR_INVALID_PARAMETER)     return "unknown locale or product";
  return QString("error %1").arg(code);
}

bool CASCFolder::setConfig(core::GameConfig config)
{
  m_currentConfig = config;

  // init map based on CASCLib
  std::map<QString, int> locales;
  locales["frFR"] = CASC_LOCALE_FRFR;
  locales["deDE"] = CASC_LOCALE_DEDE;
  locales["esES"] = CASC_LOCALE_ESES;
  locales["esMX"] = CASC_LOCALE_ESMX;
  locales["ptBR"] = CASC_LOCALE_PTBR;
  locales["itIT"] = CASC_LOCALE_ITIT;
  locales["ptPT"] = CASC_LOCALE_PTPT;
  locales["enGB"] = CASC_LOCALE_ENGB;
  locales["ruRU"] = CASC_LOCALE_RURU;
  locales["enUS"] = CASC_LOCALE_ENUS;
  locales["enCN"] = CASC_LOCALE_ENCN;
  locales["enTW"] = CASC_LOCALE_ENTW;
  locales["koKR"] = CASC_LOCALE_KOKR;
  locales["zhCN"] = CASC_LOCALE_ZHCN;
  locales["zhTW"] = CASC_LOCALE_ZHTW;

  // set locale
  if (!m_currentConfig.locale.isEmpty())
  {
    auto it = locales.find(m_currentConfig.locale);

    if (it != locales.end())
    {
      HANDLE dummy;
      if (m_online)
      {
        if (!openOnlineStorage(it->second))
          return false;
      }
      else
      {
        // CascLib's storage param uses CASC_PARAM_SEPARATOR ('*'), NOT ':', to split the
        // local path from the product code name (e.g. "...\Data*wowt"). Passing ':' left the
        // code name unset, so CascLib fell back to the FIRST active product in .build.info
        // (retail "wow") for every product -- silently loading the retail root instead of the
        // requested one (e.g. the PTR "wowt" root), which hid PTR-only files like new creatures.
        QString cascParams = m_folder + "*" + m_currentConfig.product;
        LOG_INFO << "Loading Game Folder:" << cascParams;
        // locale found => try to open it
        if (!CascOpenStorage(toCascString(cascParams).c_str(), it->second, &hStorage))
        {
          m_openError = GetCascError();
          LOG_ERROR << "CASCFolder: Opening" << cascParams << "failed." << "Error" << m_openError;
          return false;
        }
      }

      addExtraEncryptionKeys();

      // Trust the locale chosen from .build.info and resolve files by FileDataID
      // (see fileExists()/openFile(), which use CASC_OPEN_BY_FILEID with this
      // locale). Modern WoW roots (BfA+) carry no filename hashes, so opening a
      // file BY NAME always fails even with a perfectly valid locale -- the old
      // "Localization.lua" probe below therefore must NOT be treated as fatal.
      m_currentCascLocale = it->second;

      if (CascOpenFile(hStorage, "Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        LOG_INFO << "Locale set (legacy name probe ok):" << m_currentConfig.locale;
      }
      else if (m_online)
      {
        LOG_INFO << "Locale set by the caller (name probe unavailable, normal on modern WoW):"
                 << m_currentConfig.locale;
      }
      else
      {
        LOG_INFO << "Locale set from .build.info (name probe unavailable, normal on modern WoW):" << m_currentConfig.locale;
      }

      // Index every present FileDataID up front so fileExists() -- called ~2.17M times while
      // parsing the listfile -- is an O(1) set lookup instead of a per-id CascOpenFile +
      // CascCloseFile round-trip (that probing was ~6.5s of the startup freeze).
      // Online, the download and the open used 0 .. kOpenEnd of the progress range; the
      // enumeration gets the rest.
      const std::function<void(float)> outerCb = m_progressCb;
      if (m_online && outerCb)
        m_progressCb = [outerCb](float f) { outerCb(kOpenEnd + (1.0f - kOpenEnd) * f); };
      if (!m_online || !m_cancel)
        buildPresentIdIndex();
      m_progressCb = outerCb;

      // CascLib reports nothing while it downloads ROOT, the last step of the open, and the
      // enumeration cannot be stopped. A cancel that came meanwhile gets the same answer as one
      // that came earlier.
      if (m_online && m_cancel)
      {
        CascCloseStorage(hStorage);
        hStorage = nullptr;
        m_presentIds.clear();
        m_openError = ERROR_CANCELLED;
        LOG_INFO << "CASCFolder: online open cancelled";
        return false;
      }
      if (m_online && outerCb)
        outerCb(1.0f);
    }
    else
    {
      // Used to return true with no storage open -- every file then silently missing. Online the
      // locale comes from the caller rather than from .build.info, so a typo ("de_DE") is likely.
      m_openError = ERROR_INVALID_PARAMETER;
      LOG_ERROR << "CASCFolder: unknown locale" << m_currentConfig.locale;
      return false;
    }
  }

  return true;
}

// setConfig() for an online storage. First the files CascLib reads while it opens the storage --
// the build and CDN configs, the 1401 archive indexes and ENCODING -- are downloaded over parallel
// HTTPS (CDNPrefetch). CascLib would fetch them one after another over a single connection, which
// took over half a minute for the indexes alone. The prefetch only speeds things up: whatever it
// could not fetch, CascLib fetches itself. Both steps report progress, and both stop when
// cancelOnlineOpen() is called.
bool CASCFolder::openOnlineStorage(int cascLocale)
{
  LOG_INFO << "Loading online storage (CDN):" << m_folder << m_currentConfig.product << m_region
           << m_currentConfig.version;

  // Without "versions" here CascLib searches the folders above for one, up to the drive root, and
  // would open whatever it finds there. initOnline() had none to offer either.
  if (!QFileInfo(m_folder + QDir::separator() + "versions").isFile() ||
      !QFileInfo(m_folder + QDir::separator() + "cdns").isFile())
  {
    m_openError = ERROR_FILE_NOT_FOUND;
    LOG_ERROR << "CASCFolder: no build information in" << m_folder;
    return false;
  }

  OnlineProgress progress(m_progressCb, m_cancel);
  const wow::cdn::PrefetchResult prefetch = wow::cdn::prefetchForOpen(m_folder, m_region,
    [&progress](qint64 done, qint64 total)
    {
      return progress.report(kPrefetchEnd * (float)((double)done / (double)qMax<qint64>(total, 1)));
    });
  if (prefetch.cancelled || progress.cancelled())
  {
    m_openError = ERROR_CANCELLED;
    LOG_INFO << "CASCFolder: online open cancelled";
    return false;
  }
  if (!prefetch.ok)
    LOG_WARNING << "CASCFolder: the download ahead of the open is incomplete (" << prefetch.error
                << ") -- CascLib fetches the rest itself";
  progress.report(kPrefetchEnd);

  // Everything but versions/cdns is content-addressed (named by its hash), so a cached copy is
  // never stale -- and those two were refreshed over HTTPS by initOnline(). Hence no
  // CASC_FEATURE_FORCE_DOWNLOAD, and szCdnHostUrl stays NULL: with a URL, CascLib would fetch the
  // two tables itself, from a version server on port 1119, which many networks block.
  // szParams stays NULL so the path is not parsed for '*' separators.
  const std::basic_string<TCHAR> localPath = toCascString(m_folder);
  const std::basic_string<TCHAR> codeName = toCascString(m_currentConfig.product);
  const std::basic_string<TCHAR> region = toCascString(m_region);

  CASC_OPEN_STORAGE_ARGS args = {};
  args.Size = sizeof(CASC_OPEN_STORAGE_ARGS);
  args.szLocalPath = localPath.c_str();
  args.szCodeName = codeName.c_str();
  args.szRegion = region.c_str();
  args.dwLocaleMask = (DWORD)cascLocale;
  args.dwFlags = 0;
  args.PfnProgressCallback = onlineOpenProgress;
  args.PtrProgressParam = &progress;

  if (!CascOpenStorageEx(NULL, &args, true, &hStorage))
  {
    m_openError = GetCascError();
    LOG_ERROR << "CASCFolder: Opening" << m_folder << m_currentConfig.product << "failed." << "Error" << m_openError
              << errorText(m_openError);
    return false;
  }
  return true;
}

void CASCFolder::initBuildInfo()
{
  QString buildinfofile = m_folder + "\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile;

  QFile file(buildinfofile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open .build.info to grab game config info";
    return;
  }

  QTextStream in(&file);
  QString line;

  // read first line and grab VERSION index
  line = in.readLine();

  QStringList headers = line.split('|');
  int activeIndex = 0;
  int versionIndex = 0;
  int tagIndex = 0;
  int productIndex = 0;
  for (int index = 0; index < headers.size(); index++)
  {
    if (headers[index].contains("Active", Qt::CaseInsensitive))
      activeIndex = index;
    else if (headers[index].contains("Version", Qt::CaseInsensitive))
      versionIndex = index;
    else if (headers[index].contains("Tags", Qt::CaseInsensitive))
      tagIndex = index;
    else if (headers[index].contains("Product", Qt::CaseInsensitive))
      productIndex = index;
  }

  // now loop across file lines with actual values
  while (in.readLineInto(&line))
  {
    QString version, product;
    QStringList values = line.split('|');

    // if inactive config, skip it
    if (values[activeIndex] == "0")
      continue;

    // grab version for this line
    QRegularExpression re("^(\\d+).(\\d+).(\\d+).(\\d+)$");
    QRegularExpressionMatch result = re.match(values[versionIndex]);
    if (result.hasMatch())
      version = result.captured(1) + "." + result.captured(2) + "." + result.captured(3) + "." + result.captured(4);

    // grab product name for this line
    product = values[productIndex];

    // grab locale(s) for this line
    values = values[tagIndex].split(':');
    for (int i = 0; i < values.size(); i++)
    {
      if (values[i].contains("text?"))
      {
        QStringList tags = values[i].split(" ");
        core::GameConfig config;
        config.locale = tags[tags.size() - 2];
        config.version = version;
        config.product = product;
        m_configs.push_back(config);
      }
    }

  }

  for (auto it : m_configs)
    LOG_INFO << "config" << it.locale << it.version;
}


void CASCFolder::buildPresentIdIndex()
{
  m_presentIds.clear();
  if (!hStorage)
    return;

  CASC_FIND_DATA fd;
  HANDLE hFind = CascFindFirstFile(hStorage, "*", &fd, NULL);
  if (hFind == NULL || hFind == INVALID_HANDLE_VALUE)
  {
    LOG_INFO << "CASCFolder: storage enumeration unavailable; fileExists() will probe per id.";
    return; // leave m_presentIds empty -> fileExists() falls back to the per-id probe
  }

  m_presentIds.reserve(1u << 21); // ~2M files in a modern retail build

  // Progress reporting: the enumeration count isn't known up front, so report a soft fraction
  // against a rough expected total (~4M files in a current retail build) capped below 1.0, just
  // so the loading bar visibly advances during this multi-second step instead of sitting still.
  size_t seen = 0, nextReport = 0;
  const float EXPECTED = 4000000.0f;

  do
  {
    if (m_progressCb && ++seen >= nextReport)
    {
      const float frac = (float)seen / EXPECTED;
      m_progressCb(frac < 0.99f ? frac : 0.99f);
      nextReport = seen + 50000; // ~80 updates over a full enumeration
    }
    // Index EVERY enumerated FileDataID, NOT just fd.bFileAvailable ones. bFileAvailable is set
    // only for files cached locally on disk; on a streaming / partial install many valid files
    // (e.g. creature skin textures) are remote-only. CascLib still opens those on demand via
    // CascOpenFile(CASC_OPEN_BY_FILEID), exactly as the old per-id probe this replaced did --
    // so filtering by bFileAvailable wrongly dropped them from the file tree, which broke the
    // creature skin folder-scan and left those creatures rendering untextured (white).
    if (fd.dwFileDataId != CASC_INVALID_ID)
      m_presentIds.insert(static_cast<int>(fd.dwFileDataId));
  } while (CascFindNextFile(hFind, &fd));
  CascFindClose(hFind);

  LOG_INFO << "CASCFolder: indexed" << (unsigned int)m_presentIds.size() << "present FileDataIDs (single enumeration).";
}

bool CASCFolder::fileExists(int id)
{
  if(!hStorage)
    return false;

  // Fast path: O(1) lookup in the enumerated id set (buildPresentIdIndex). A real model
  // load still force-opens by id (WoWFolder::getFile), so any id missed by enumeration is
  // still loadable -- it just won't appear in the browse tree.
  if (!m_presentIds.empty())
    return m_presentIds.count(id) != 0;

  // Fallback (enumeration unavailable): the original per-id open/close probe.
  HANDLE dummy;
  if(CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, &dummy))
  {
    CascCloseFile(dummy);
    return true;
  }
  return false;
}

bool CASCFolder::openFile(int id, HANDLE * result)
{
  return CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, result);
}

bool CASCFolder::closeFile(HANDLE file)
{
  return CascCloseFile(file);
}

void CASCFolder::addExtraEncryptionKeys()
{
  // Keys CascLib already knows (it has a built-in list, and the two sources overlap) are counted
  // apart, so the log shows what each source actually adds.
  int read = 0;
  int added = 0;
  QFile tactKeys("extraEncryptionKeys.csv");

  if (tactKeys.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QTextStream in(&tactKeys);
    while (!in.atEnd())
    {
      QString line = in.readLine();
      if (line.startsWith("##") || line.startsWith("\"##"))  // ignore lines beginning with ##, useful for adding comments.
        continue;

      QStringList lineData = line.split(';');
      if (lineData.size() != 2)
        continue;
      QString keyName = lineData.at(0);
      QString keyValue = lineData.at(1);
      if (keyName.isEmpty() || keyValue.isEmpty())
        continue;

      bool ok, ok2;
      const ULONGLONG name = keyName.toULongLong(&ok, 16);
      const bool known = CascFindEncryptionKey(hStorage, name) != NULL;
      ok2 = CascAddStringEncryptionKey(hStorage, name, keyValue.toStdString().c_str());
      if (!ok2)
          LOG_ERROR << "Failed to add TACT key from file, Name:" << keyName << ", Value:" << keyValue;
      read++;
      if (ok2 && !known)
        added++;
    }
  }
  LOG_INFO << "TACT keys from extraEncryptionKeys.csv:" << read << "read," << added << "new";

  // Online, the build is always the newest one: add wowdev's list as refreshed by initOnline(),
  // or as cached by an earlier start when this one is offline.
  if (!m_online)
    return;

  QFile cached(m_folder + QDir::separator() + kTactKeysFile);
  if (!cached.open(QIODevice::ReadOnly))
  {
    LOG_WARNING << "TACT keys: no cached key list in" << m_folder;
    return;
  }
  const std::vector<TactKey> keys = parseTactKeys(cached.readAll());
  added = 0;
  for (const TactKey & key : keys)
  {
    const bool known = CascFindEncryptionKey(hStorage, key.name) != NULL;
    if (CascAddStringEncryptionKey(hStorage, key.name, key.value.constData()) && !known)
      added++;
  }
  LOG_INFO << "TACT keys from wowdev's list in the cache:" << (unsigned int)keys.size() << "read," << added << "new";
}

/*
int CASCFolder::fileDataId(std::string & filename)
{
  return CascGetFileId(hStorage, filename.c_str());
}
*/
