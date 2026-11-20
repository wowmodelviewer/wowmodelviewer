/*
 * ClientInstallations.cpp
 */
#include "ClientInstallations.h"

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <functional>
#include <QTextStream>

#include "util.h"            // gamePath, cfgPath
#include "logger/Logger.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

core::GameConfig InstalledClient::config() const
{
  core::GameConfig c;
  c.locale = locale;
  c.version = version;
  c.product = product;
  return c;
}

QString InstalledClient::dataPath() const
{
  return QDir::toNativeSeparators(root + "/Data/");
}

namespace
{
  // The order the chooser lists them in.
  int familyRank(core::ProductFamily f)
  {
    switch (f)
    {
      case core::ProductFamily::Retail:        return 0;
      case core::ProductFamily::RetailPTR:     return 1;
      case core::ProductFamily::RetailBeta:    return 2;
      case core::ProductFamily::RetailAlpha:   return 3;
      case core::ProductFamily::Classic:       return 4;
      case core::ProductFamily::ClassicPTR:    return 5;
      case core::ProductFamily::ClassicBeta:   return 6;
      case core::ProductFamily::ClassicEra:    return 7;
      case core::ProductFamily::ClassicEraPTR: return 8;
      case core::ProductFamily::ClassicAnniversary:    return 9;
      case core::ProductFamily::ClassicAnniversaryPTR: return 10;
      case core::ProductFamily::Forever:               return 11;
      default:                                 return 12;
    }
  }

  // Is an encoded key (its first 9 bytes, as the local index stores it) in the local CASC index? The index is 16
  // buckets of "<bucket><version>.idx" files under Data\data; a key lives in the bucket its bytes hash to, in the
  // newest file of that bucket. (The same layout CascLib reads; v7 index: a header block, then 18-byte entries.)
  bool ekeyInLocalIndex(const QString & dataDir, const QByteArray & ekey, QString & detail)
  {
    if (ekey.size() < 9)
    {
      detail = "no encoding key";
      return false;
    }
    unsigned char x = 0;
    for (int i = 0; i < 9; i++)
      x ^= (unsigned char)ekey[i];
    const int bucket = (x & 0x0F) ^ (x >> 4);
    const QDir dir(dataDir + "/data");
    const QStringList files =
      dir.entryList(QStringList() << QString("%1????????.idx").arg(bucket, 2, 16, QChar('0')), QDir::Files, QDir::Name);
    if (files.isEmpty())
    {
      detail = "no local archive index";
      return false;
    }
    QFile f(dir.filePath(files.last())); // the newest version of the bucket
    if (!f.open(QIODevice::ReadOnly))
    {
      detail = "the local archive index could not be read";
      return false;
    }
    const QByteArray raw = f.readAll();
    if (raw.size() < 8)
    {
      detail = "the local archive index is incomplete";
      return false;
    }
    quint32 headerSize = 0;
    std::memcpy(&headerSize, raw.constData(), 4);
    int pos = (8 + (int)headerSize + 15) & ~15;
    if (pos + 8 > raw.size())
    {
      detail = "the local archive index is incomplete";
      return false;
    }
    quint32 entriesSize = 0;
    std::memcpy(&entriesSize, raw.constData() + pos, 4);
    pos += 8;
    const int end = (std::min<int>)(raw.size(), pos + (int)entriesSize);
    for (int at = pos; at + 18 <= end; at += 18)
      if (std::memcmp(raw.constData() + at, ekey.constData(), 9) == 0)
        return true;
    return false;
  }

  void checkLocalData(InstalledClient & c)
  {
    const QString dataDir = c.root + "/Data";
    if (c.buildKey.size() < 4)
    {
      c.data = InstalledClient::Data::Unknown;
      c.dataDetail = "no build key in .build.info";
      return;
    }
    QFile cfg(QString("%1/config/%2/%3/%4").arg(dataDir, c.buildKey.left(2), c.buildKey.mid(2, 2), c.buildKey));
    if (!cfg.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      c.data = InstalledClient::Data::NotDownloaded;
      c.dataDetail = "its build configuration is not on this computer";
      return;
    }
    QByteArray encodingKey;
    while (!cfg.atEnd())
    {
      const QByteArray line = cfg.readLine().trimmed();
      if (line.startsWith("encoding = "))
      {
        const QList<QByteArray> parts = line.mid(11).split(' ');
        if (parts.size() >= 2)
          encodingKey = QByteArray::fromHex(parts.at(1));
      }
    }
    QString detail;
    if (encodingKey.isEmpty())
    {
      c.data = InstalledClient::Data::Unknown;
      c.dataDetail = "its build configuration names no encoding manifest";
    }
    else if (ekeyInLocalIndex(dataDir, encodingKey, detail))
    {
      c.data = InstalledClient::Data::Available;
      c.dataDetail = "on this computer";
    }
    else
    {
      c.data = detail.isEmpty() ? InstalledClient::Data::NotDownloaded : InstalledClient::Data::Unknown;
      c.dataDetail = detail.isEmpty() ? QString("its game data is not on this computer (encoding manifest %1 missing)")
                                          .arg(QString::fromLatin1(encodingKey.toHex()))
                                      : detail;
    }
  }
}

QString ClientInstallations::rootOf(const QString & anyPath)
{
  QString p = QDir::fromNativeSeparators(anyPath);
  while (p.endsWith('/'))
    p.chop(1);
  for (int up = 0; up < 3 && !p.isEmpty(); up++)
  {
    if (QFile::exists(p + "/.build.info"))
      return QDir::toNativeSeparators(p);
    p = QFileInfo(p).path();
  }
  return QString();
}

std::vector<InstalledClient> ClientInstallations::discover(const QString & anyPath, QString * error)
{
  std::vector<InstalledClient> found;
  const QString root = rootOf(anyPath);
  if (root.isEmpty())
  {
    if (error)
      *error = "No World of Warcraft installation was found in that folder.";
    return found;
  }
  QFile file(root + "/.build.info");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    if (error)
      *error = "The installation's .build.info could not be read.";
    return found;
  }
  QTextStream in(&file);
  const QStringList headers = in.readLine().split('|');
  auto column = [&headers](const char * name) {
    for (int i = 0; i < headers.size(); i++)
      if (headers[i].section('!', 0, 0).compare(QLatin1String(name), Qt::CaseInsensitive) == 0)
        return i;
    return -1;
  };
  const int cBranch = column("Branch"), cActive = column("Active"), cBuildKey = column("Build Key"),
            cTags = column("Tags"), cVersion = column("Version"), cProduct = column("Product");
  QString line;
  while (in.readLineInto(&line))
  {
    const QStringList v = line.split('|');
    if (cActive < 0 || cVersion < 0 || cProduct < 0 || v.size() <= (std::max)(cVersion, cProduct))
      continue;
    if (v.value(cActive) == "0")
      continue;
    InstalledClient c;
    c.root = root;
    c.product = v.value(cProduct);
    c.version = v.value(cVersion);
    c.region = v.value(cBranch);
    c.buildKey = v.value(cBuildKey);
    // The text locale: the tag before "text?" ("... enUS text?").
    for (const QString & group : v.value(cTags).split(':'))
      if (group.contains("text?"))
      {
        const QStringList tags = group.split(' ', QString::SkipEmptyParts);
        const int at = tags.indexOf("text?");
        if (at > 0)
          c.locale = tags.at(at - 1);
        break;
      }
    if (c.product.isEmpty() || c.version.isEmpty())
      continue;
    // One card per product: a second row (another region of the same product) adds nothing to choose from.
    if (std::any_of(found.begin(), found.end(), [&c](const InstalledClient & o) { return o.product == c.product; }))
      continue;
    c.profile = core::ClientProfile::fromGameConfig(c.config());
    checkLocalData(c);
    found.push_back(c);
  }
  std::stable_sort(found.begin(), found.end(), [](const InstalledClient & a, const InstalledClient & b) {
    return familyRank(a.profile.family) < familyRank(b.profile.family);
  });
  if (found.empty() && error)
    *error = "The installation lists no World of Warcraft product.";
  return found;
}

namespace
{
  // Battle.net's install list (Agent/product.db) is a protobuf record per product holding its install folder as a
  // plain string. The folders are read as the printable runs that look like absolute paths; each is then checked for
  // a .build.info, so a wrong guess costs nothing. mapPath turns a Windows path into this computer's (a Wine
  // prefix's C:/ into its drive_c); it returns "" for a path it cannot place.
  QStringList pathsInProductDb(const QString & productDb, const std::function<QString(const QString &)> & mapPath)
  {
    QStringList paths;
    QFile file(productDb);
    if (!file.open(QIODevice::ReadOnly))
      return paths;
    const QByteArray bytes = file.read(8 * 1024 * 1024);
    QByteArray run;
    auto flush = [&]() {
      if (run.size() >= 3)
      {
        const QString s = QString::fromUtf8(run);
        const bool unixPath = s.startsWith('/');
        const bool windowsPath = s.size() > 2 && s[1] == ':' && (s[2] == '/' || s[2] == '\\') && s[0].isLetter();
        if (unixPath || windowsPath)
        {
          const QString mapped = mapPath ? mapPath(QDir::fromNativeSeparators(s)) : s;
          if (!mapped.isEmpty() && !paths.contains(mapped))
            paths << mapped;
        }
      }
      run.clear();
    };
    for (char c : bytes)
    {
      if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f)
        run.append(c);
      else
        flush();
    }
    flush();
    return paths;
  }

#if !defined(_WIN32) && !defined(__APPLE__)
  // A Windows path of a Wine prefix on this computer: C:/x is <prefix>/drive_c/x, another drive through the prefix's
  // dosdevices links (d: -> /mnt/games, say).
  QString inPrefix(const QString & prefix, const QString & windowsPath)
  {
    if (windowsPath.size() < 3 || windowsPath[1] != ':')
      return QString();
    const QChar drive = windowsPath[0].toLower();
    const QString rest = windowsPath.mid(2);
    if (drive == 'c' && QDir(prefix + "/drive_c").exists())
      return prefix + "/drive_c" + rest;
    const QString device = prefix + "/dosdevices/" + drive + ":";
    if (QFileInfo(device).exists())
      return QFileInfo(device).canonicalFilePath() + rest;
    return QString();
  }

  // Every Wine prefix worth looking in: the default one, Proton's (Steam, in every library it lists), Lutris's
  // (~/Games), Heroic's and Bottles' (native and Flatpak).
  QStringList winePrefixes()
  {
    QStringList prefixes;
    auto add = [&prefixes](const QString & p) {
      if (QDir(p + "/drive_c").exists() && !prefixes.contains(p))
        prefixes << p;
    };
    auto addChildren = [&add](const QString & dir, const QString & sub = QString()) {
      for (const QString & child : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        add(dir + "/" + child + sub);
    };
    const QString home = QDir::homePath();
    const QByteArray wineprefix = qgetenv("WINEPREFIX");
    if (!wineprefix.isEmpty())
      add(QString::fromLocal8Bit(wineprefix));
    add(home + "/.wine");
    addChildren(home + "/.local/share/wineprefixes");
    addChildren(home + "/Games");                       // Lutris: ~/Games/<game>
    addChildren(home + "/Games/Heroic/Prefixes");       // Heroic: ~/Games/Heroic/Prefixes/<game>
    addChildren(home + "/Games/Heroic/Prefixes/default");
    addChildren(home + "/.local/share/bottles/bottles");
    addChildren(home + "/.var/app/com.usebottles.bottles/data/bottles/bottles");
    addChildren(home + "/.var/app/com.heroicgameslauncher.hgl/config/heroic/Prefixes");

    // Steam: its own library and the others libraryfolders.vdf names; Proton keeps a prefix per game (a
    // non-Steam game too) at steamapps/compatdata/<id>/pfx.
    QStringList libraries;
    for (const QString & root : { home + "/.steam/steam", home + "/.local/share/Steam", home + "/.steam/root",
                                  home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam" })
    {
      if (!QDir(root + "/steamapps").exists())
        continue;
      libraries << QDir(root).canonicalPath();
      QFile vdf(root + "/steamapps/libraryfolders.vdf");
      if (vdf.open(QIODevice::ReadOnly | QIODevice::Text))
      {
        const QString text = QString::fromUtf8(vdf.readAll());
        int at = 0;
        while ((at = text.indexOf("\"path\"", at)) >= 0)
        {
          const int open = text.indexOf('"', at + 6);
          const int close = open >= 0 ? text.indexOf('"', open + 1) : -1;
          if (close < 0)
            break;
          libraries << QDir(text.mid(open + 1, close - open - 1).replace("\\\\", "\\")).canonicalPath();
          at = close + 1;
        }
      }
    }
    libraries.removeDuplicates();
    for (const QString & lib : libraries)
      addChildren(lib + "/steamapps/compatdata", "/pfx");
    return prefixes;
  }
#endif
}

QStringList ClientInstallations::candidateRoots()
{
  QStringList roots;
  auto add = [&roots](const QString & path) {
    const QString r = rootOf(path);
    if (!r.isEmpty() && !roots.contains(r, Qt::CaseInsensitive))
      roots << r;
  };
  add(QString::fromWCharArray(gamePath.c_str()));
  {
    QSettings settings(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
    add(settings.value("Client/LastPath").toString());
  }
#ifdef _WINDOWS
  const wchar_t * keys[] = { L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\World of Warcraft",
                             L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\World of Warcraft\\PTR",
                             L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\World of Warcraft\\Beta",
                             L"SOFTWARE\\Blizzard Entertainment\\World of Warcraft" };
  for (const wchar_t * key : keys)
  {
    wchar_t value[1024] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"InstallPath", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
      add(QString::fromWCharArray(value));
  }
  // Battle.net's own list of what it installed.
  {
    const QByteArray programData = qgetenv("ProgramData");
    const QString agent = (programData.isEmpty() ? QString("C:/ProgramData") : QString::fromLocal8Bit(programData)) +
                          "/Battle.net/Agent/product.db";
    for (const QString & p : pathsInProductDb(agent, nullptr))
      add(p);
  }
  add("C:/Program Files (x86)/World of Warcraft");
  add("C:/Program Files/World of Warcraft");
#elif defined(__APPLE__)
  for (const QString & p : pathsInProductDb("/Users/Shared/Battle.net/Agent/product.db", nullptr))
    add(p);
  add("/Applications/World of Warcraft");
  add(QDir::homePath() + "/Applications/World of Warcraft");
#else
  // Linux: World of Warcraft runs in a Wine or Proton prefix; Battle.net's list inside each prefix says where.
  for (const QString & prefix : winePrefixes())
  {
    for (const QString & p : pathsInProductDb(prefix + "/drive_c/ProgramData/Battle.net/Agent/product.db",
                                              [&prefix](const QString & w) { return inPrefix(prefix, w); }))
      add(p);
    add(prefix + "/drive_c/Program Files (x86)/World of Warcraft");
    add(prefix + "/drive_c/Program Files/World of Warcraft");
  }
#endif
  return roots;
}

QString ClientInstallations::resolveSchema(const core::ClientProfile & profile, QString * how)
{
  const QString exact = QString("%1.%2").arg(profile.major).arg(profile.minor);
  if (QFile::exists("games/wow/" + exact + "/database.xml"))
  {
    if (how)
      *how = QString("games/wow/%1 (this version's own schema)").arg(exact);
    return exact;
  }
  // The newest schema of the client's own major version (a 10.2 client reads 10.1's), else the newest of all.
  QString best;
  int bestMajor = -1, bestMinor = -1;
  bool bestSameMajor = false;
  for (const QString & dir : QDir("games/wow").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    bool okMajor = false, okMinor = false;
    const int major = dir.section('.', 0, 0).toInt(&okMajor);
    const int minor = dir.section('.', 1, 1).toInt(&okMinor);
    if (!okMajor || !okMinor || !QFile::exists("games/wow/" + dir + "/database.xml"))
      continue;
    const bool sameMajor = major == profile.major;
    if ((sameMajor && !bestSameMajor) ||
        (sameMajor == bestSameMajor && (major > bestMajor || (major == bestMajor && minor > bestMinor))))
    {
      bestMajor = major;
      bestMinor = minor;
      bestSameMajor = sameMajor;
      best = dir;
    }
  }
  if (how)
    *how = best.isEmpty() ? QString("none: no schema ships with this viewer")
                          : QString("games/wow/%1 as the base (no %2 schema ships); every table checked against this "
                                    "client's own layout").arg(best, exact);
  return best;
}
