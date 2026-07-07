/*
 * MpqFileProvider.cpp
 *
 * StormLib-backed MPQ provider. See MpqFileProvider.h.
 */

#include "MpqFileProvider.h"

#include <algorithm>
#include <set>
#include <utility>

#include <QDir>
#include <QFileInfo>

#ifndef __STORMLIB_NO_STATIC_LINK__
#  define __STORMLIB_NO_STATIC_LINK__
#endif
#include "StormLib.h"

#include "logger/Logger.h"

namespace
{
  // Split an archive stem (name without ".MPQ") into a base and a trailing numeric suffix, so
  // "patch"->("patch",0), "patch-2"->("patch",2), "common-2"->("common",2),
  // "locale-enGB"->("locale-enGB",0), "patch-enGB-2"->("patch-enGB",2). Lets each group sort in
  // real load order (base before -2 before -3) rather than raw alphabetical ('-' < '.').
  void splitArchiveName(const QString & stem, QString & base, int & num)
  {
    const int dash = stem.lastIndexOf('-');
    if (dash >= 0)
    {
      bool ok = false;
      const int n = stem.mid(dash + 1).toInt(&ok);
      if (ok)
      {
        base = stem.left(dash);
        num = n;
        return;
      }
    }
    base = stem;
    num = 0;
  }

  // Order a folder's *.MPQ into (non-patch, patch) groups, each sorted by (base, trailing number).
  std::pair<QStringList, QStringList> orderGroup(const QDir & dir)
  {
    const QStringList files = dir.entryList(QStringList() << "*.MPQ", QDir::Files);
    QStringList nonpatch, patch;
    for (const QString & f : files)
    {
      if (QFileInfo(f).completeBaseName().startsWith("patch", Qt::CaseInsensitive))
        patch << f;
      else
        nonpatch << f;
    }
    auto cmp = [](const QString & a, const QString & b) {
      QString ba, bb; int na = 0, nb = 0;
      splitArchiveName(QFileInfo(a).completeBaseName(), ba, na);
      splitArchiveName(QFileInfo(b).completeBaseName(), bb, nb);
      const int c = ba.compare(bb, Qt::CaseInsensitive);
      if (c != 0) return c < 0;
      return na < nb;
    };
    std::sort(nonpatch.begin(), nonpatch.end(), cmp);
    std::sort(patch.begin(), patch.end(), cmp);
    return std::make_pair(nonpatch, patch);
  }
}

wow::MpqFileProvider::MpqFileProvider()
{
}

wow::MpqFileProvider::~MpqFileProvider()
{
  for (auto & a : m_archives)
    if (a.handle)
      SFileCloseArchive(a.handle);
  m_archives.clear();
}

QString wow::MpqFileProvider::mpqInternalPath(const std::string & name)
{
  QString q = QString::fromStdString(name);
  q.replace('/', '\\'); // MPQ archives use backslash-separated internal paths
  return q;
}

std::vector<QString> wow::MpqFileProvider::buildArchiveList(const QString & dataRoot, const QString & localeIn)
{
  const QStringList mpqFilter(QStringList() << "*.MPQ");

  // Accept either the WoW install folder or its Data subfolder.
  QString data = dataRoot;
  if (QDir(dataRoot).entryList(mpqFilter, QDir::Files).isEmpty())
  {
    const QString sub = dataRoot + "/Data";
    if (!QDir(sub).entryList(mpqFilter, QDir::Files).isEmpty())
      data = sub;
  }
  m_dataFolder = data;
  const QDir dataDir(data);

  // Auto-detect the locale subfolder if not supplied.
  QString locale = localeIn;
  static const char * KNOWN_LOCALES[] = {
    "enUS", "enGB", "enCN", "enTW", "koKR", "frFR", "deDE",
    "zhCN", "zhTW", "esES", "esMX", "ruRU", "ptBR", "ptPT", "itIT"
  };
  if (locale.isEmpty())
  {
    for (const char * loc : KNOWN_LOCALES)
    {
      const QDir ld(data + "/" + loc);
      if (ld.exists() && !ld.entryList(mpqFilter, QDir::Files).isEmpty())
      {
        locale = loc;
        break;
      }
    }
  }
  m_locale = locale;

  const std::pair<QStringList, QStringList> baseGroups = orderGroup(dataDir);
  std::pair<QStringList, QStringList> localeGroups;
  if (!locale.isEmpty())
    localeGroups = orderGroup(QDir(data + "/" + locale));

  std::vector<QString> ordered;
  auto append = [&ordered](const QStringList & list, const QString & folder) {
    for (const QString & f : list)
      ordered.push_back(folder + "/" + f);
  };
  // low -> high priority: base non-patch, base patch, locale non-patch, locale patch
  append(baseGroups.first, data);
  append(baseGroups.second, data);
  if (!locale.isEmpty())
  {
    append(localeGroups.first, data + "/" + locale);
    append(localeGroups.second, data + "/" + locale);
  }
  return ordered;
}

int wow::MpqFileProvider::init(const QString & dataRoot, const QString & locale)
{
  const std::vector<QString> list = buildArchiveList(dataRoot, locale);

  LOG_INFO << "[mpq] Data folder:" << m_dataFolder
           << " locale:" << (m_locale.isEmpty() ? QString("(none)") : m_locale);

  if (list.empty())
  {
    LOG_ERROR << "[mpq] No .MPQ archives found under" << m_dataFolder
              << "-- is this a legacy (pre-CASC) WoW install?";
    return 0;
  }

  int opened = 0;
  for (const QString & path : list)
  {
    HANDLE h = 0;
    const std::wstring wpath = path.toStdWString();
    if (SFileOpenArchive(wpath.c_str(), 0, MPQ_OPEN_READ_ONLY, &h))
    {
      Archive a;
      a.name = QFileInfo(path).fileName();
      a.handle = h;
      m_archives.push_back(a);
      LOG_INFO << "[mpq]   archive[" << (int)m_archives.size() - 1 << "] loaded:" << a.name;
      opened++;
    }
    else
    {
      LOG_WARNING << "[mpq]   FAILED to open" << path << "(StormLib error" << (int)GetLastError() << ")";
    }
  }

  LOG_INFO << "[mpq] archives opened:" << opened << "of" << (int)list.size()
           << "| lookup mode = Name | override order = base -> patches -> locale (highest first)";
  return opened;
}

bool wow::MpqFileProvider::openById(int /*fileDataId*/, void ** /*handle*/)
{
  // Legacy MPQ clients have no FileDataID.
  return false;
}

bool wow::MpqFileProvider::openByName(const std::string & name, void ** handle)
{
  const std::string ip = mpqInternalPath(name).toStdString();

  // Probe highest-priority archive first (back of the list) so patch/locale archives win.
  for (auto it = m_archives.rbegin(); it != m_archives.rend(); ++it)
  {
    HANDLE hFile = 0;
    if (SFileOpenFileEx(it->handle, ip.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
    {
      *handle = hFile;
      LOG_INFO << "[mpq] open OK:" << QString::fromStdString(name) << "<-" << it->name;
      return true;
    }
  }

  LOG_INFO << "[mpq] open MISS:" << QString::fromStdString(name);
  return false;
}

bool wow::MpqFileProvider::hasFile(const std::string & name)
{
  const std::string ip = mpqInternalPath(name).toStdString();
  for (auto it = m_archives.rbegin(); it != m_archives.rend(); ++it)
    if (SFileHasFile(it->handle, ip.c_str()))
      return true;
  return false;
}

bool wow::MpqFileProvider::closeFile(void * handle)
{
  if (!handle)
    return false;
  return SFileCloseFile(handle) != FALSE;
}

QString wow::MpqFileProvider::archiveListString() const
{
  QString s;
  for (size_t i = 0; i < m_archives.size(); i++)
  {
    if (i) s += "; ";
    s += m_archives[i].name;
  }
  return s;
}

void wow::MpqFileProvider::listAllFiles(std::vector<QString> & out) const
{
  std::set<QString> seen;
  for (const Archive & a : m_archives)
  {
    SFILE_FIND_DATA fd;
    HANDLE hFind = SFileFindFirstFile(a.handle, "*", &fd, NULL);
    if (!hFind)
      continue; // archive without a usable (listfile) -- its files are still openable by name

    do
    {
      // cFileName is the internal MPQ path (backslash, ANSI). Normalise to lowercase '/' so it
      // matches the file-tree / name-map keys (GameFolder lowercases + forward-slashes).
      QString name = QString::fromLocal8Bit(fd.cFileName).toLower().replace('\\', '/');
      if (name.isEmpty() || name.startsWith('(')) // skip MPQ metadata: (listfile), (attributes), ...
        continue;
      if (seen.insert(name).second)
        out.push_back(name);
    } while (SFileFindNextFile(hFind, &fd));

    SFileFindClose(hFind);
  }
}
