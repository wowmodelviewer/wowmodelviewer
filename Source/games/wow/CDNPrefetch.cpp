/*
 * CDNPrefetch.cpp
 *
 * Parallel HTTPS download of the files CascLib reads while it opens an online storage. See
 * CDNPrefetch.h.
 */

#include "CDNPrefetch.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <list>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector>
#ifndef QT_NO_SSL
#include <QSslSocket>
#endif

#include "logger/Logger.h"

namespace
{
  // Requests in flight: twelve, spread over the first two hosts -- six each, which is also the
  // number of connections Qt keeps to one host. A seventh request to a host would wait inside Qt,
  // unseen, and the idle timeout below would take that wait for a stall. The archive indexes
  // average 89 KB, so fetching them is bound by round trips, not by bandwidth.
  const int kMaxRequests = 12;
  const int kMaxRequestsPerHost = 6;
  const int kHostsInUse = 2;

  // A request that receives nothing for this long is aborted and tried elsewhere. A transfer as a
  // whole has no limit: ENCODING alone is 187 MB, which takes minutes on a slow line.
  const qint64 kIdleTimeoutMs = 20000;

  // Progress is reported, and idle requests are looked for, this often. The progress callback is
  // also how a cancel arrives, so it keeps being called while nothing moves.
  const int kTickMs = 100;
  const qint64 kReportIntervalMs = 90;

  // A host is dropped for the rest of the run after more failures in a row than it can have
  // requests in flight, and it gets plain HTTP after as many HTTPS network failures in a row
  // (plain HTTP may still work: a filtered port 443, a proxy that breaks TLS). Fewer would count
  // a hiccup that breaks all open connections at once as the host failing: a test run lost HTTPS
  // to level3.blizzard.com when six reused connections closed together. A TLS error settles
  // HTTPS at once, and so does a single timeout, since every request to a silent host waits out
  // the whole of it: counting timeouts one by one made a silent host cost 80 s instead of 40.
  const int kFailureLimit = kMaxRequestsPerHost + 1;

  // Sizes the progress total starts from, until a response or the build config says better. The
  // index average is 125,011,188 bytes over 1,401 archives in 12.1.0.69933.
  const qint64 kConfigSizeGuess = 64 * 1024;
  const qint64 kIndexSizeGuess = 89 * 1024;
  const qint64 kEncodingSizeGuess = 200 * 1024 * 1024;

  // A file that cannot be written into the cache is left to CascLib. After this many of them the
  // cache as a whole is taken to be unwritable (disk full, no permission) and nothing more is
  // downloaded; a single one may be a scanner that held on to a new file for a moment (Windows).
  const int kLocalFailureLimit = 3;

  // Configs and indexes are checked in memory before they are written. None is near this size
  // (the largest index in 12.1.0 is 721 KB); anything bigger is not the file that was asked for.
  const int kMaxBufferedSize = 32 * 1024 * 1024;

  // What Qt may hold of an answer before it waits for this code to take it. Without a limit it
  // reads ahead as fast as the network allows: ENCODING from a fast host piled up 128 MB in
  // memory, which then took a second to check and write in one piece, with no progress report in
  // between. 1 MB was also the fastest of the sizes measured (256 KB to 4 MB, and unlimited).
  const qint64 kReadBufferSize = 1024 * 1024;

  // An encoded file's header lists 24 bytes per frame; even a huge file has far fewer frames.
  const qint64 kMaxBlteHeaderSize = 16 * 1024 * 1024;

  const char * const kUserAgent = "ModelViewer-Midnight";

  QByteArray md5(const char * data, int size)
  {
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(data, size);
    return hash.result();
  }

  QString writeError(const QFileDevice & file)
  {
    return QString("cannot write %1: %2").arg(QDir::toNativeSeparators(file.fileName()), file.errorString());
  }

  // Opens a file of the cache for writing. QSaveFile writes to a temporary file next to it and
  // gives it the final name on commit(), so an unfinished file never looks like a cached one.
  bool createFile(QSaveFile & file, QString & error)
  {
    const QString folder = QFileInfo(file.fileName()).absolutePath();
    if (!QDir().mkpath(folder))
      error = QString("cannot create %1").arg(QDir::toNativeSeparators(folder));
    else if (!file.open(QIODevice::WriteOnly))
      error = writeError(file);
    else
      return true;
    return false;
  }

  quint32 bigEndian32(const uchar * p)
  {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
  }

  // Every name here is an MD5 in lower-case hex, the way CascLib spells it in its cache.
  bool isKey(const QByteArray & text)
  {
    if (text.size() != 32)
      return false;
    for (char c : text)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
    return true;
  }

  // CascLib's cache layout: <root>/config/xx/yy/<key> and <root>/data/xx/yy/<key>[.index], xx and
  // yy being the key's first two bytes. On the CDN the same names sit below <host>/<cdn path>/.
  QString keyPath(const char * folder, const QByteArray & key, const char * extension = "")
  {
    return QString("%1/%2/%3/%4%5").arg(folder, QString::fromLatin1(key.left(2)),
                                        QString::fromLatin1(key.mid(2, 2)), QString::fromLatin1(key), extension);
  }

  // Reads one row of a version-service table ("versions" or "cdns"): a header line of
  // "Name!TYPE:n" columns separated by '|', "##" comment lines ("## seqn = ..."), then one row per
  // region. The row is picked the way CascLib's LoadCsvFile picks it when it opens the storage --
  // the requested region, else us, eu, xx, else the first row -- so this fetches exactly the build
  // and the hosts CascLib is about to use. Columns are returned by their lower-case name.
  bool readTableRow(const QString & path, const QByteArray & keyColumn, const QByteArray & region,
                    QHash<QByteArray, QByteArray> & row, QString & error)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
      error = QString("cannot read %1 (%2)").arg(QDir::toNativeSeparators(path), file.errorString());
      return false;
    }

    QList<QByteArray> columns;
    QList<QList<QByteArray>> rows;
    for (const QByteArray & rawLine : file.readAll().split('\n'))
    {
      const QByteArray line = rawLine.trimmed();
      if (line.isEmpty() || line.startsWith("##"))
        continue;
      const QList<QByteArray> fields = line.split('|');
      if (columns.isEmpty())
      {
        for (const QByteArray & field : fields)
        {
          const int type = field.indexOf('!');
          columns << (type < 0 ? field : field.left(type)).trimmed().toLower();
        }
      }
      else if (fields.size() == columns.size())
        rows << fields;
    }

    const int keyIndex = columns.indexOf(keyColumn);
    if (keyIndex < 0 || rows.isEmpty())
    {
      error = QString("%1 is not a version table").arg(QDir::toNativeSeparators(path));
      return false;
    }

    const QList<QByteArray> preference = { region, "us", "eu", "xx" };
    const QList<QByteArray> * chosen = &rows.first();
    for (const QByteArray & wanted : preference)
    {
      auto match = std::find_if(rows.cbegin(), rows.cend(),
                                [&](const QList<QByteArray> & r) { return r[keyIndex].trimmed() == wanted; });
      if (match != rows.cend())
      {
        chosen = &*match;
        break;
      }
    }

    row.clear();
    for (int i = 0; i < columns.size(); i++)
      row.insert(columns[i], (*chosen)[i].trimmed());
    return true;
  }

  // Build and CDN configs are "name = value value ..." lines; '#' starts a comment.
  QHash<QByteArray, QList<QByteArray>> parseConfig(const QByteArray & text)
  {
    QHash<QByteArray, QList<QByteArray>> values;
    for (const QByteArray & rawLine : text.split('\n'))
    {
      const QByteArray line = rawLine.trimmed();
      const int equals = line.indexOf('=');
      if (line.startsWith('#') || equals <= 0)
        continue;
      QList<QByteArray> items;
      for (const QByteArray & item : line.mid(equals + 1).simplified().split(' '))
        if (!item.isEmpty())
          items << item;
      values.insert(line.left(equals).trimmed(), items);
    }
    return values;
  }

  // An archive index is named after the MD5 of its 28-byte footer. The footer holds the hash of
  // the table of contents, and the table holds the hash of every 4 KB page, so these checks cover
  // every byte. The name check alone is what tells an index from an error page; the rest costs
  // about 0.2 ms per file and catches a body damaged somewhere in the middle.
  QString checkArchiveIndex(const QByteArray & data, const QByteArray & key)
  {
    const int footerSize = 28;
    if (data.size() <= footerSize)
      return QString("%1 bytes is too short for an archive index").arg(data.size());
    const char * footer = data.constData() + data.size() - footerSize;
    if (md5(footer, footerSize).toHex() != key)
      return "the content does not match its name";

    const uchar * fields = reinterpret_cast<const uchar *>(footer);
    const int version = fields[8];
    const int pageSize = fields[11] * 1024;
    const int keySize = fields[14];
    const int hashSize = fields[15];
    if (version != 1 || pageSize == 0 || keySize == 0 || hashSize != 8)
      return "unknown archive index layout";

    const int entrySize = pageSize + keySize + hashSize;
    if ((data.size() - footerSize) % entrySize != 0)
      return "the size does not match the footer";
    const int pages = (data.size() - footerSize) / entrySize;
    const char * contents = data.constData() + pages * pageSize;
    if (md5(contents, pages * (keySize + hashSize)).left(hashSize) != QByteArray(footer, hashSize))
      return "the table of contents is damaged";

    const char * pageHashes = contents + pages * keySize;
    for (int i = 0; i < pages; i++)
    {
      const QByteArray expected(pageHashes + i * hashSize, hashSize);
      if (md5(data.constData() + i * pageSize, pageSize).left(hashSize) != expected)
        return QString("page %1 is damaged").arg(i);
    }
    return QString();
  }

  // Checks an encoded (BLTE) file while it streams in. Its EKey is the MD5 of its header -- or of
  // the whole file, for the rare file without a frame table -- which is how CascLib and wow.export
  // identify one. The header lists every frame's size and MD5, so each frame is verified as soon
  // as it is complete: a wrong file is dropped after its first few hundred bytes, a damaged one at
  // the end of the damaged frame, not after 187 MB.
  class BlteCheck
  {
    public:
      explicit BlteCheck(const QByteArray & ekey)
        : m_ekey(QByteArray::fromHex(ekey)), m_hash(QCryptographicHash::Md5)
      {
      }

      // Takes the next bytes of the file. False as soon as they cannot belong to it (see why()).
      bool feed(const char * data, int size)
      {
        while (size > 0 && m_why.isEmpty())
        {
          // The 8-byte prefix ("BLTE" and the header size), then the frame table
          if (m_headerSize < 0 || m_header.size() < m_headerSize)
          {
            const qint64 wanted = (m_headerSize < 0 ? 8 : m_headerSize) - m_header.size();
            const int take = int(qMin<qint64>(wanted, size));
            m_header.append(data, take);
            data += take;
            size -= take;
            m_size += take;
            if (m_headerSize < 0 && m_header.size() == 8)
              readPrefix();
            else if (m_headerSize > 0 && m_header.size() == m_headerSize)
              readFrameTable();
            continue;
          }

          if (m_headerSize == 0)
          {
            m_hash.addData(data, size);
            m_size += size;
            break;
          }

          if (m_frame >= m_frames.size())
            return reject(QString("more than the %1 bytes its header lists").arg(m_expected));
          const int take = int(qMin<qint64>(m_frameLeft, size));
          m_hash.addData(data, take);
          data += take;
          size -= take;
          m_size += take;
          m_frameLeft -= take;
          if (m_frameLeft == 0)
          {
            if (m_hash.result() != m_frames[m_frame].md5)
              return reject(QString("frame %1 is damaged").arg(m_frame));
            m_hash.reset();
            if (++m_frame < m_frames.size())
              m_frameLeft = m_frames[m_frame].size;
          }
        }
        return m_why.isEmpty();
      }

      // After the last byte: whether the file is complete and matches its EKey.
      bool finish()
      {
        if (!m_why.isEmpty())
          return false;
        if (m_headerSize == 0)
          return m_hash.result() == m_ekey || reject("the content does not match its EKey");
        if (m_headerSize < 0 || m_frames.empty() || m_frame < m_frames.size())
        {
          m_incomplete = true;
          return reject(m_expected > 0 ? QString("it ended after %1 of %2 bytes").arg(m_size).arg(m_expected)
                                       : QString("it ended after %1 bytes").arg(m_size));
        }
        return true;
      }

      qint64 size() const { return m_size; }            // bytes taken so far
      qint64 expectedSize() const { return m_expected; } // from the frame table; -1 until it is read
      bool incomplete() const { return m_incomplete; }   // finish() failed only for want of bytes
      const QString & why() const { return m_why; }

    private:
      void readPrefix()
      {
        if (!m_header.startsWith("BLTE"))
        {
          reject("not an encoded (BLTE) file");
          return;
        }
        m_headerSize = bigEndian32(reinterpret_cast<const uchar *>(m_header.constData()) + 4);
        if (m_headerSize == 0)
          m_hash.addData(m_header);    // no frame table: the EKey covers the whole file
        else if (m_headerSize < 12 + 24 || m_headerSize > kMaxBlteHeaderSize)
          reject(QString("implausible header size %1").arg(m_headerSize));
      }

      void readFrameTable()
      {
        if (md5(m_header.constData(), m_header.size()) != m_ekey)
        {
          reject("the header does not match its EKey");
          return;
        }
        // Flags 0x0F and 24 bytes per frame: the only layout CascLib reads, too
        const uchar * table = reinterpret_cast<const uchar *>(m_header.constData());
        const qint64 count = (qint64(table[9]) << 16) | (qint64(table[10]) << 8) | table[11];
        if (table[8] != 0x0F || count == 0 || 12 + 24 * count != m_headerSize)
        {
          reject("unknown frame table layout");
          return;
        }
        m_expected = m_headerSize;
        for (qint64 i = 0; i < count; i++)
        {
          const uchar * entry = table + 12 + 24 * i;
          const Frame frame = { bigEndian32(entry), QByteArray(reinterpret_cast<const char *>(entry) + 8, 16) };
          if (frame.size == 0)
          {
            reject(QString("frame %1 is empty").arg(i));
            return;
          }
          m_frames.push_back(frame);
          m_expected += frame.size;
        }
        m_frameLeft = m_frames[0].size;
      }

      bool reject(const QString & why)
      {
        m_why = why;
        return false;
      }

      struct Frame
      {
        qint64 size;
        QByteArray md5;
      };

      QByteArray m_ekey;          // 16 bytes
      QByteArray m_header;        // until the frame table is complete
      qint64 m_headerSize = -1;   // -1 until the first 8 bytes are in
      std::vector<Frame> m_frames;
      size_t m_frame = 0;         // the frame being received
      qint64 m_frameLeft = 0;
      QCryptographicHash m_hash;  // of that frame, or of the whole file if it has no frame table
      qint64 m_size = 0;
      qint64 m_expected = -1;
      bool m_incomplete = false;
      QString m_why;
  };

  // Why an attempt failed decides where the file is tried next.
  enum class Failure
  {
    None,
    Network,   // no usable answer (refused, reset, proxy): worth plain HTTP to the same host
    Silent,    // nothing for kIdleTimeoutMs: the same, but that scheme is not tried on the host again
    Tls,       // HTTPS itself fails with this host (handshake, no OpenSSL): plain HTTP from now on
    NoHost,    // the name does not resolve: the host is dropped
    Refused,   // the host answered with an HTTP error: next host
    BadData,   // the host answered with bytes that fail a check: next host, and start over
    Local      // the file cannot be written into the cache: not worth another host
  };

  struct Host
  {
    QString name;
    int active = 0;             // requests in flight
    int failures = 0;           // in a row
    int httpsFailures = 0;      // network failures of HTTPS, in a row
    bool httpsBroken = false;   // plain HTTP from now on
    bool dead = false;
  };

  struct Job
  {
    enum Kind { Config, Index, Encoded };

    Kind kind;
    QByteArray key;             // the file's name: config key, archive key or EKey
    QString remotePath;         // below <host>/<cdn path>/
    QString localPath;
    qint64 expected;            // the file's size, for progress; exact if exactSize
    bool exactSize;             // from the build config: a response of any other size is wrong

    // Where the next attempt goes
    int host = -1;
    bool http = false;
    QVector<bool> exhausted;    // hosts this file has failed on
    QString lastError;

    // The attempt in flight
    QNetworkReply * reply = nullptr;
    QElapsedTimer idle;
    bool answered = false;      // the status line has been checked
    qint64 skip = 0;            // bytes to drop from a full answer to a resumed (ranged) request
    Failure failure = Failure::None;
    QString why;

    // What has arrived. Configs and indexes are checked in memory; an encoded file is checked and
    // written while it streams in, and a failed attempt resumes where the last one stopped.
    QByteArray data;
    std::unique_ptr<BlteCheck> check;
    std::unique_ptr<QSaveFile> file;

    bool done = false;
    bool ok = false;

    qint64 obtained() const { return kind == Encoded ? check->size() : data.size(); }
  };

  class Prefetcher
  {
    public:
      Prefetcher(const QString & root, const std::function<bool(qint64, qint64)> & progress)
        : m_root(QDir::fromNativeSeparators(root)), m_progress(progress)
      {
        while (m_root.endsWith('/'))
          m_root.chop(1);
      }

      wow::cdn::PrefetchResult run(const QString & region);

    private:
      bool readTables(const QString & region, QByteArray & buildKey, QByteArray & cdnKey, QString & error);
      Job * addJob(Job::Kind kind, const QByteArray & key, qint64 expected, bool exactSize);
      bool loadCachedConfig(const QByteArray & key, QByteArray & text) const;
      bool isCached(const QString & path, qint64 size) const;

      void transfer(const std::vector<Job *> & jobs);
      void pump();
      int pickHost() const;
      bool moveOn(Job & job);
      bool usesHttp(int host) const { return !m_tls || m_hosts[host].httpsBroken; }
      void start(Job & job);
      void onData(Job & job, QNetworkReply * reply);
      void onRedirected(Job & job, QNetworkReply * reply, const QUrl & target);
      void onFinished(Job & job, QNetworkReply * reply);
      bool consume(Job & job, QNetworkReply * reply);
      bool checkAnswer(Job & job, QNetworkReply * reply);
      Failure classify(const Job & job, QNetworkReply * reply, QString & why) const;
      Failure complete(Job & job, QString & why);
      void succeeded(Job & job);
      void retry(Job & job, Failure failure, const QString & why);
      void giveUp(Job & job);
      void discard(Job & job);
      void breakHttps(Host & host, const QString & why);
      void drop(Host & host, const QString & why);
      void onTick();
      void report();
      void stop();
      bool idle() const { return m_active == 0 && m_fresh.empty() && m_retries.empty(); }

      bool fail(Job & job, Failure failure, const QString & why)
      {
        job.failure = failure;
        job.why = why;
        return false;
      }

      QString m_root;
      std::function<bool(qint64, qint64)> m_progress;

      QString m_cdnPath;
      QVector<Host> m_hosts;
      bool m_tls = true;

      std::vector<std::unique_ptr<Job>> m_jobs;
      std::deque<Job *> m_fresh;    // not tried yet: any of the hosts in use will do
      std::list<Job *> m_retries;   // bound to the host of their next attempt

      std::unique_ptr<QNetworkAccessManager> m_manager;
      QEventLoop * m_loop = nullptr;
      QElapsedTimer m_sinceReport;
      double m_shown = 0.0;         // the fraction last reported
      int m_active = 0;
      bool m_stopping = false;
      bool m_cancelled = false;
      bool m_planned = false;       // the data files are known; before that only configs are fetched

      qint64 m_bytes = 0;
      int m_requests = 0;
      int m_peak = 0;
      int m_cached = 0;
      int m_downloaded = 0;
      int m_failed = 0;
      int m_localFailures = 0;
      QString m_firstFailure;
      QString m_localError;
  };

  wow::cdn::PrefetchResult Prefetcher::run(const QString & region)
  {
    QElapsedTimer clock;
    clock.start();
    wow::cdn::PrefetchResult result;

    QByteArray buildKey, cdnKey;
    if (!readTables(region, buildKey, cdnKey, result.error))
    {
      LOG_ERROR << "CDN prefetch:" << qPrintable(result.error);
      return result;
    }

    // The configs come first: they name everything else.
    QByteArray buildConfig, cdnConfig;
    Job * buildJob = nullptr;
    Job * cdnJob = nullptr;
    std::vector<Job *> configJobs;
    if (!loadCachedConfig(buildKey, buildConfig))
      configJobs.push_back(buildJob = addJob(Job::Config, buildKey, kConfigSizeGuess, false));
    if (!loadCachedConfig(cdnKey, cdnConfig))
      configJobs.push_back(cdnJob = addJob(Job::Config, cdnKey, kConfigSizeGuess, false));
    m_cached += 2 - int(configJobs.size());
    transfer(configJobs);
    if (buildJob && buildJob->ok)
      buildConfig = buildJob->data;
    if (cdnJob && cdnJob->ok)
      cdnConfig = cdnJob->data;

    std::vector<Job *> dataJobs;
    if (!buildConfig.isEmpty())
    {
      // "encoding = <ckey> <ekey>" and "encoding-size = <size> <encoded size>". CascLib reads
      // ENCODING by its EKey, as the loose file data/xx/yy/<ekey>.
      const QHash<QByteArray, QList<QByteArray>> config = parseConfig(buildConfig);
      const QList<QByteArray> encoding = config.value("encoding");
      const QList<QByteArray> sizes = config.value("encoding-size");
      const QByteArray ekey = encoding.size() >= 2 ? encoding[1].toLower() : QByteArray();
      const qint64 size = sizes.size() >= 2 ? sizes[1].toLongLong() : 0;
      if (!isKey(ekey))
        LOG_WARNING << "CDN prefetch: the build config names no ENCODING EKey; CascLib will look it up";
      else if (isCached(m_root + "/" + keyPath("data", ekey), size))
        m_cached++;
      else
        dataJobs.push_back(addJob(Job::Encoded, ekey, size > 0 ? size : kEncodingSizeGuess, size > 0));
    }
    if (!cdnConfig.isEmpty())
    {
      // "archives-index-size" does not list the sizes in the order of "archives", but its sum is
      // right, which is all the progress total needs.
      const QHash<QByteArray, QList<QByteArray>> config = parseConfig(cdnConfig);
      const QList<QByteArray> archives = config.value("archives");
      qint64 indexBytes = 0;
      for (const QByteArray & size : config.value("archives-index-size"))
        indexBytes += size.toLongLong();
      const qint64 averageSize = (indexBytes > 0 && !archives.isEmpty()) ? indexBytes / archives.size()
                                                                         : kIndexSizeGuess;
      for (const QByteArray & archive : archives)
      {
        const QByteArray key = archive.toLower();
        if (!isKey(key))
          continue;
        if (isCached(m_root + "/" + keyPath("data", key, ".index"), 0))
          m_cached++;
        else
          dataJobs.push_back(addJob(Job::Index, key, averageSize, false));
      }
    }
    m_planned = true;
    transfer(dataJobs);

    // Let the bar reach the end of this step before the caller moves on
    if (m_requests > 0 && !m_cancelled && m_progress)
      m_progress(qMax<qint64>(m_bytes, 1), qMax<qint64>(m_bytes, 1));

    result.cancelled = m_cancelled;
    result.bytesDownloaded = m_bytes;
    result.filesDownloaded = m_downloaded;
    result.filesFailed = m_failed;
    if (m_cancelled)
      result.error = "cancelled";
    else if (!m_localError.isEmpty())
      result.error = m_localError;
    else if (buildConfig.isEmpty() || cdnConfig.isEmpty())
      result.error = QString("the %1 config could not be downloaded (%2)")
                       .arg(buildConfig.isEmpty() ? "build" : "CDN", m_firstFailure);
    else if (m_failed > 0)
      result.error = QString("%1 of %2 files could not be downloaded, e.g. %3")
                       .arg(m_failed).arg(m_downloaded + m_failed).arg(m_firstFailure);
    result.ok = result.error.isEmpty();

    const QString summary = QString("CDN prefetch: %1 files downloaded, %2 already cached, %3 failed; "
                                    "%4 MiB in %5 s, %6 requests, at most %7 at a time")
                              .arg(m_downloaded).arg(m_cached).arg(m_failed)
                              .arg(double(m_bytes) / 1048576.0, 0, 'f', 1)
                              .arg(double(clock.elapsed()) / 1000.0, 0, 'f', 2)
                              .arg(m_requests).arg(m_peak);
    if (result.ok)
      LOG_INFO << qPrintable(summary);
    else if (result.cancelled)
      LOG_INFO << qPrintable(summary) << "- cancelled";
    else
      LOG_ERROR << qPrintable(summary) << "-" << qPrintable(result.error);
    return result;
  }

  bool Prefetcher::readTables(const QString & region, QByteArray & buildKey, QByteArray & cdnKey,
                              QString & error)
  {
    const QByteArray regionName = region.trimmed().toLower().toUtf8();
    QHash<QByteArray, QByteArray> versions, cdns;
    if (!readTableRow(m_root + "/versions", "region", regionName, versions, error) ||
        !readTableRow(m_root + "/cdns", "name", regionName, cdns, error))
      return false;

    buildKey = versions.value("buildconfig").toLower();
    cdnKey = versions.value("cdnconfig").toLower();
    if (!isKey(buildKey) || !isKey(cdnKey))
    {
      error = "the versions table names no build or CDN config";
      return false;
    }

    m_cdnPath = QString::fromUtf8(cdns.value("path"));
    while (m_cdnPath.startsWith('/'))
      m_cdnPath.remove(0, 1);
    while (m_cdnPath.endsWith('/'))
      m_cdnPath.chop(1);

    QStringList names;
    for (const QByteArray & host : cdns.value("hosts").simplified().split(' '))
      if (!host.isEmpty() && !names.contains(QString::fromUtf8(host), Qt::CaseInsensitive))
        names << QString::fromUtf8(host);
    for (const QString & name : names)
    {
      Host host;
      host.name = name;
      m_hosts.push_back(host);
    }
    if (m_cdnPath.isEmpty() || m_hosts.empty())
    {
      error = "the cdns table names no CDN path or host";
      return false;
    }
    return true;
  }

  Job * Prefetcher::addJob(Job::Kind kind, const QByteArray & key, qint64 expected, bool exactSize)
  {
    std::unique_ptr<Job> job(new Job);
    job->kind = kind;
    job->key = key;
    job->remotePath = kind == Job::Config ? keyPath("config", key)
                    : kind == Job::Index ? keyPath("data", key, ".index")
                    : keyPath("data", key);
    job->localPath = m_root + "/" + job->remotePath;
    job->expected = expected;
    job->exactSize = exactSize;
    job->exhausted.fill(false, m_hosts.size());
    if (kind == Job::Encoded)
      job->check.reset(new BlteCheck(key));
    m_jobs.push_back(std::move(job));
    return m_jobs.back().get();
  }

  // A cached config is used only while its content still matches its name. CascLib checks that,
  // too, but a mismatch only makes it fail the open (ERROR_FILE_CORRUPT) without fetching the file
  // again -- so a damaged copy is fetched here, and replaces the old one once it checks out.
  bool Prefetcher::loadCachedConfig(const QByteArray & key, QByteArray & text) const
  {
    QFile file(m_root + "/" + keyPath("config", key));
    if (!file.open(QIODevice::ReadOnly))
      return false;
    text = file.readAll();
    if (md5(text.constData(), text.size()).toHex() == key)
      return true;
    LOG_WARNING << "CDN prefetch: cached config" << key.constData() << "is damaged; fetching it again";
    text.clear();
    return false;
  }

  // A cached data file counts if it is not empty: this and CascLib's patched downloader only ever
  // write complete, verified files under their final name. ENCODING has a known size, which also
  // catches a copy cut short by some other writer.
  bool Prefetcher::isCached(const QString & path, qint64 size) const
  {
    const QFileInfo info(path);
    return info.isFile() && (size > 0 ? info.size() == size : info.size() > 0);
  }

  void Prefetcher::transfer(const std::vector<Job *> & jobs)
  {
    if (jobs.empty() || m_stopping)
      return;

    // Created on first use, on the calling thread: the manager serves only the local event loop
    // below, and a start with a warm cache never creates one.
    if (!m_manager)
    {
      m_manager.reset(new QNetworkAccessManager);
#ifndef QT_NO_SSL
      m_tls = QSslSocket::supportsSsl();
#else
      m_tls = false;
#endif
      if (!m_tls)
        LOG_WARNING << "CDN prefetch: no TLS support (are the OpenSSL libraries missing?); using plain HTTP";
    }

    for (Job * job : jobs)
      m_fresh.push_back(job);

    QEventLoop loop;
    QTimer ticker;
    ticker.setInterval(kTickMs);
    QObject::connect(&ticker, &QTimer::timeout, &loop, [this] { onTick(); });
    m_loop = &loop;
    m_sinceReport.start();
    ticker.start();
    pump();
    if (!idle() && !m_stopping)
      loop.exec();
    ticker.stop();
    m_loop = nullptr;
  }

  // Starts queued files while there is room: retries first, each on the host it is bound to, then
  // new files on whichever of the hosts in use has fewer requests running.
  void Prefetcher::pump()
  {
    if (m_stopping)
      return;

    for (auto it = m_retries.begin(); it != m_retries.end() && m_active < kMaxRequests; )
    {
      Job & job = **it;
      if (m_hosts[job.host].dead && !moveOn(job))
      {
        it = m_retries.erase(it);
        giveUp(job);
      }
      else if (m_hosts[job.host].active < kMaxRequestsPerHost)
      {
        it = m_retries.erase(it);
        start(job);
      }
      else
        ++it;
    }

    while (!m_fresh.empty() && m_active < kMaxRequests)
    {
      const int host = pickHost();
      if (host == -1)
        break;
      Job & job = *m_fresh.front();
      m_fresh.pop_front();
      if (host == -2)
      {
        giveUp(job);
        continue;
      }
      job.host = host;
      job.http = usesHttp(host);
      start(job);
    }

    if (idle() && m_loop && m_loop->isRunning())
      m_loop->quit();
  }

  // The hosts in use are the first two that still work. -1: both are busy. -2: none works.
  int Prefetcher::pickHost() const
  {
    int best = -2;
    int inUse = 0;
    for (int i = 0; i < m_hosts.size() && inUse < kHostsInUse; i++)
    {
      if (m_hosts[i].dead)
        continue;
      inUse++;
      if (m_hosts[i].active < kMaxRequestsPerHost && (best < 0 || m_hosts[i].active < m_hosts[best].active))
        best = i;
      else if (best == -2)
        best = -1;
    }
    return best;
  }

  // Binds a file that failed on its host to the next host in the list it has not failed on.
  bool Prefetcher::moveOn(Job & job)
  {
    job.exhausted[job.host] = true;
    const int count = m_hosts.size();
    for (int step = 1; step < count; step++)
    {
      const int next = (job.host + step) % count;
      if (!job.exhausted[next] && !m_hosts[next].dead)
      {
        job.host = next;
        job.http = usesHttp(next);
        return true;
      }
    }
    return false;
  }

  void Prefetcher::start(Job & job)
  {
    Host & host = m_hosts[job.host];
    const QString scheme = job.http ? "http" : "https";
    const QUrl url(QString("%1://%2/%3/%4").arg(scheme, host.name, m_cdnPath, job.remotePath));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    // The file exactly as the CDN stores it: sizes and offsets then mean what they say.
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setMaximumRedirectsAllowed(5);
    const qint64 resumeAt = job.obtained();
    if (job.kind == Job::Encoded && resumeAt > 0)
      request.setRawHeader("Range", "bytes=" + QByteArray::number(resumeAt) + "-");

    QNetworkReply * reply = m_manager->get(request);
    reply->setReadBufferSize(kReadBufferSize);
    job.reply = reply;
    job.idle.start();
    job.answered = false;
    job.skip = 0;
    job.failure = Failure::None;
    job.why.clear();
    if (job.kind != Job::Encoded)
      job.data.clear();
    host.active++;
    m_active++;
    m_requests++;
    m_peak = qMax(m_peak, m_active);

    // The reply is the context of each connection, and a signal is only acted on while this reply
    // is the file's current attempt: a finished attempt must not touch the next one.
    Job * owner = &job;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [this, owner, reply]
    {
      if (owner->reply == reply)
        onData(*owner, reply);
    });
    QObject::connect(reply, &QNetworkReply::redirected, reply, [this, owner, reply](const QUrl & target)
    {
      if (owner->reply == reply)
        onRedirected(*owner, reply, target);
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, owner, reply]
    {
      if (owner->reply == reply)
        onFinished(*owner, reply);
    });
  }

  void Prefetcher::onData(Job & job, QNetworkReply * reply)
  {
    job.idle.restart();
    // abort() finishes the reply at once; onFinished() takes it from there, with job.failure set.
    // An answer Qt has already failed (an HTTP error status) finishes on its own, and aborting it
    // on top only makes Qt complain.
    if (!consume(job, reply))
    {
      if (reply->error() == QNetworkReply::NoError)
        reply->abort();
      return;
    }
    // A stream that arrives faster than it can be checked and written keeps the event queue full,
    // and the ticker then waits behind it (over a second, measured at 150 MB/s): report from here
    // as well, so progress and a cancel do not wait for it.
    if (m_sinceReport.elapsed() >= kReportIntervalMs)
      report();
  }

  // Redirects are followed only within the scheme that was asked for. An HTTPS request must not end
  // up on plain HTTP (Qt refuses that itself), and a plain-HTTP fallback must not end up on the
  // HTTPS that has just failed.
  void Prefetcher::onRedirected(Job & job, QNetworkReply * reply, const QUrl & target)
  {
    job.idle.restart();
    if (target.scheme().compare(job.http ? "http" : "https", Qt::CaseInsensitive) != 0)
    {
      fail(job, Failure::Network, QString("redirected to %1").arg(target.toString()));
      reply->abort();
    }
  }

  // Takes whatever the reply has buffered. False when the attempt has to stop (job.failure says why).
  bool Prefetcher::consume(Job & job, QNetworkReply * reply)
  {
    if (job.failure != Failure::None)
      return false;
    if (!job.answered && !checkAnswer(job, reply))
      return false;

    const QByteArray chunk = reply->readAll();
    m_bytes += chunk.size();
    const char * data = chunk.constData();
    int size = chunk.size();
    if (job.skip > 0)
    {
      const int skipped = int(qMin<qint64>(job.skip, size));
      job.skip -= skipped;
      data += skipped;
      size -= skipped;
    }
    if (size == 0)
      return true;

    if (job.kind != Job::Encoded)
    {
      if (job.data.size() + size > kMaxBufferedSize)
        return fail(job, Failure::BadData, "far larger than expected");
      job.data.append(data, size);
      return true;
    }

    if (!job.check->feed(data, size))
      return fail(job, Failure::BadData, job.check->why());
    if (job.check->expectedSize() > 0 && !job.exactSize)
      job.expected = job.check->expectedSize();
    QString error;
    if (!job.file)
    {
      job.file.reset(new QSaveFile(job.localPath));
      if (!createFile(*job.file, error))
        return fail(job, Failure::Local, error);
    }
    if (job.file->write(data, size) != size)
      return fail(job, Failure::Local, writeError(*job.file));
    return true;
  }

  // The status line and headers of an answer, before its first byte is taken.
  bool Prefetcher::checkAnswer(Job & job, QNetworkReply * reply)
  {
    job.answered = true;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206)
      return fail(job, Failure::Refused, QString("HTTP %1").arg(status));

    // A resumed request asked for the rest of the file. A host may ignore that and send all of it
    // again, which is fine: the part already here is dropped from the answer.
    const qint64 resumeAt = job.kind == Job::Encoded ? job.check->size() : 0;
    if (status == 206)
    {
      const QByteArray range = reply->rawHeader("Content-Range");
      if (resumeAt == 0 || !range.startsWith("bytes " + QByteArray::number(resumeAt) + "-"))
        return fail(job, Failure::BadData, QString("unexpected partial answer (%1)").arg(QString(range)));
    }
    else
      job.skip = resumeAt;

    const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
    if (length.isValid())
    {
      const qint64 fileSize = length.toLongLong() + (status == 206 ? resumeAt : 0);
      if (job.exactSize && fileSize != job.expected)
        return fail(job, Failure::BadData, QString("%1 bytes instead of %2").arg(fileSize).arg(job.expected));
      if (job.kind != Job::Encoded && fileSize > kMaxBufferedSize)
        return fail(job, Failure::BadData, QString("%1 bytes, far larger than expected").arg(fileSize));
      job.expected = fileSize;
    }
    return true;
  }

  void Prefetcher::onFinished(Job & job, QNetworkReply * reply)
  {
    // The last bytes can come with the end of the answer
    if (job.failure == Failure::None && reply->error() == QNetworkReply::NoError)
      consume(job, reply);

    job.reply = nullptr;
    m_hosts[job.host].active--;
    m_active--;
    reply->deleteLater();

    if (m_stopping)
    {
      discard(job);
      return;
    }

    QString why = job.why;
    Failure failure = job.failure;
    if (failure == Failure::None)
      failure = classify(job, reply, why);
    if (failure == Failure::None)
      failure = complete(job, why);

    if (failure == Failure::None)
      succeeded(job);
    else if (failure == Failure::Local)
    {
      job.lastError = why;
      giveUp(job);
      if (++m_localFailures >= kLocalFailureLimit)
      {
        m_localError = why;
        stop();
        return;
      }
    }
    else
      retry(job, failure, why);
    pump();
  }

  Failure Prefetcher::classify(const Job & job, QNetworkReply * reply, QString & why) const
  {
    const QNetworkReply::NetworkError error = reply->error();
    if (error == QNetworkReply::NoError)
      return Failure::None;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
    {
      why = QString("HTTP %1").arg(status);
      return Failure::Refused;
    }

    why = reply->errorString();
    switch (error)
    {
      case QNetworkReply::HostNotFoundError:
        return Failure::NoHost;
      case QNetworkReply::SslHandshakeFailedError:
      case QNetworkReply::ProtocolUnknownError:
      case QNetworkReply::InsecureRedirectError:
        return job.http ? Failure::Network : Failure::Tls;
      default:
        // Qt's content (2xx) and server (4xx) error codes: the host did answer
        if ((error >= QNetworkReply::ContentAccessDenied && error <= QNetworkReply::UnknownContentError) ||
            (error >= QNetworkReply::InternalServerError && error <= QNetworkReply::UnknownServerError))
          return Failure::Refused;
        return Failure::Network;
    }
  }

  // Verifies a completely received file and gives it its final name.
  Failure Prefetcher::complete(Job & job, QString & why)
  {
    if (job.kind == Job::Encoded)
    {
      if (!job.check->finish())
      {
        why = job.check->why();
        // Cut short without an error from the connection: the next attempt resumes
        return job.check->incomplete() ? Failure::Network : Failure::BadData;
      }
      if (!job.file->commit())
      {
        why = writeError(*job.file);
        return Failure::Local;
      }
      job.file.reset();
      return Failure::None;
    }

    if (job.kind == Job::Index)
      why = checkArchiveIndex(job.data, job.key);
    else if (md5(job.data.constData(), job.data.size()).toHex() != job.key)
      why = "the content does not match its name";
    if (!why.isEmpty())
      return Failure::BadData;

    QSaveFile file(job.localPath);
    if (!createFile(file, why))
      return Failure::Local;
    if (file.write(job.data) != job.data.size() || !file.commit())
    {
      why = writeError(file);
      return Failure::Local;
    }
    // Configs are parsed next; an index is done with
    if (job.kind == Job::Index)
      job.data = QByteArray();
    return Failure::None;
  }

  void Prefetcher::succeeded(Job & job)
  {
    Host & host = m_hosts[job.host];
    host.failures = 0;
    if (!job.http)
      host.httpsFailures = 0;
    job.done = true;
    job.ok = true;
    m_downloaded++;
  }

  void Prefetcher::retry(Job & job, Failure failure, const QString & why)
  {
    Host & host = m_hosts[job.host];
    const bool overHttps = !job.http;
    job.lastError = QString("%1: %2").arg(host.name, why);

    switch (failure)
    {
      case Failure::Tls:
        breakHttps(host, why);
        break;
      case Failure::NoHost:
        drop(host, why);
        break;
      case Failure::Network:
      case Failure::Silent:
        if (overHttps)
        {
          if (++host.httpsFailures >= kFailureLimit || failure == Failure::Silent)
            breakHttps(host, why);
        }
        else if (++host.failures >= kFailureLimit || failure == Failure::Silent)
          drop(host, why);
        break;
      case Failure::BadData:
        // Bytes that failed a check do not tell which part was wrong: start the file over
        if (job.kind == Job::Encoded)
        {
          job.file.reset();
          job.check.reset(new BlteCheck(job.key));
        }
        if (++host.failures >= kFailureLimit)
          drop(host, why);
        break;
      case Failure::Refused:
      default:
        if (++host.failures >= kFailureLimit)
          drop(host, why);
        break;
    }

    // HTTPS failed without an answer from the host: try the same host over plain HTTP first
    const bool noAnswer = failure == Failure::Network || failure == Failure::Silent || failure == Failure::Tls;
    if (overHttps && noAnswer && !host.dead)
    {
      job.http = true;
      m_retries.push_back(&job);
      return;
    }
    if (moveOn(job))
      m_retries.push_back(&job);
    else
      giveUp(job);
  }

  void Prefetcher::giveUp(Job & job)
  {
    job.done = true;
    m_failed++;
    if (m_firstFailure.isEmpty())
      m_firstFailure = QString("%1 (%2)").arg(job.remotePath,
                                              job.lastError.isEmpty() ? QString("no CDN host left") : job.lastError);
    discard(job);
  }

  // Drops whatever an unfinished file has received. QSaveFile removes its temporary file, so
  // nothing incomplete is left in the cache.
  void Prefetcher::discard(Job & job)
  {
    job.data = QByteArray();
    job.file.reset();
  }

  void Prefetcher::breakHttps(Host & host, const QString & why)
  {
    if (host.httpsBroken)
      return;
    host.httpsBroken = true;
    LOG_WARNING << qPrintable(QString("CDN prefetch: HTTPS to %1 fails (%2); using plain HTTP for it")
                                .arg(host.name, why));
  }

  void Prefetcher::drop(Host & host, const QString & why)
  {
    if (host.dead)
      return;
    host.dead = true;
    LOG_WARNING << qPrintable(QString("CDN prefetch: giving up on %1 (%2)").arg(host.name, why));
  }

  void Prefetcher::onTick()
  {
    for (const std::unique_ptr<Job> & owned : m_jobs)
    {
      Job & job = *owned;
      if (job.reply && job.idle.elapsed() > kIdleTimeoutMs)
      {
        fail(job, Failure::Silent, QString("no data for %1 s").arg(kIdleTimeoutMs / 1000));
        job.reply->abort();
      }
    }
    if (m_sinceReport.elapsed() >= kReportIntervalMs)
      report();
  }

  void Prefetcher::report()
  {
    m_sinceReport.restart();
    if (m_stopping || !m_progress)
      return;

    // Until the data files are known only the configs are counted, and done stays 0: measured
    // against the configs alone, the bar would run to the end and then jump back.
    if (!m_planned)
    {
      qint64 configs = 0;
      for (const std::unique_ptr<Job> & owned : m_jobs)
        configs += owned->expected;
      if (!m_progress(0, qMax<qint64>(configs, 1)))
      {
        m_cancelled = true;
        stop();
      }
      return;
    }

    // Bytes received so far, plus what the unfinished files still need.
    qint64 remaining = 0;
    for (const std::unique_ptr<Job> & owned : m_jobs)
      if (!owned->done)
        remaining += qMax<qint64>(0, owned->expected - owned->obtained());
    const qint64 total = qMax<qint64>(m_bytes + remaining, 1);
    // A file that has to be fetched again adds what its failed attempt received to the total a
    // second time, which would move the bar back: hold it still until the new bytes catch up.
    const qint64 done = qMin(qMax(m_bytes, qint64(std::ceil(m_shown * double(total)))), total);
    m_shown = double(done) / double(total);
    if (!m_progress(done, total))
    {
      m_cancelled = true;
      stop();
    }
  }

  void Prefetcher::stop()
  {
    if (m_stopping)
      return;
    m_stopping = true;
    m_fresh.clear();
    m_retries.clear();
    for (const std::unique_ptr<Job> & owned : m_jobs)
      if (owned->reply)
        owned->reply->abort();
    if (m_loop)
      m_loop->quit();
  }
}

wow::cdn::PrefetchResult wow::cdn::prefetchForOpen(const QString & root, const QString & region,
                                                   const std::function<bool(qint64 done, qint64 total)> & progress)
{
  Prefetcher prefetcher(root, progress);
  return prefetcher.run(region);
}
