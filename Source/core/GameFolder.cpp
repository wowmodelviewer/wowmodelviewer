/*
 * GameFolder.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "GameFolder.h"

#include <QRegularExpression>

#include "logger/Logger.h"

core::GameFolder::GameFolder(const QString & path)
  : m_path(path)
{
}


QString core::GameFolder::getFullPathForFile(QString file)
{
  file = file.toLower();
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    if((*it)->name() == file)
      return (*it)->fullname();
  }

  return "";
}

void core::GameFolder::getFilesForFolder(std::vector<GameFile *> &fileNames, QString folderPath, QString extension)
{
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    GameFile * file = *it;
    if(file->fullname().startsWith(folderPath, Qt::CaseInsensitive) &&
       (!extension.size() || file->fullname().endsWith(extension, Qt::CaseInsensitive)))
    {
      fileNames.push_back(file);
    }
  }
}

void core::GameFolder::getFilteredFiles(std::set<GameFile *> &dest, QString & filter)
{
  QRegularExpression regex(filter);

  if(!regex.isValid())
  {
    LOG_ERROR << regex.errorString();
    return;
  }

  // Fast extension pre-filter: every filter built by the file/anim controls ends in
  // a literal "\.<ext>" (e.g. "^.*human.*\.m2"). The listfile has ~1M entries but
  // only a fraction share the requested extension, so an endsWith() check lets us
  // skip the (comparatively expensive) regex match for the vast majority of files.
  // This is semantically identical -- the regex requires that extension anyway.
  QString ext;
  const int dotIdx = filter.lastIndexOf("\\.");
  if (dotIdx >= 0)
  {
    const QString tail = filter.mid(dotIdx + 2);
    // only use the pre-filter when the tail is a plain extension (letters/digits),
    // i.e. it carries no further regex metacharacters.
    static const QRegularExpression plainExt("^[A-Za-z0-9]+$");
    if (plainExt.match(tail).hasMatch())
      ext = "." + tail;
  }
  const Qt::CaseSensitivity cs = filter.contains("(?i)") ? Qt::CaseInsensitive : Qt::CaseSensitive;

  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    const QString & n = (*it)->name();
    if (!ext.isEmpty() && !n.endsWith(ext, cs))
      continue;
    if(n.contains(regex))
    {
      dest.insert(*it);
    }
  }
}

GameFile * core::GameFolder::getFile(QString filename)
{
  filename = filename.toLower().replace('\\','/');

  GameFile * result = 0;

  auto it = m_nameMap.find(filename);
  if (it != m_nameMap.end())
    result = it->second;

  return result;
}

void core::GameFolder::onChildAdded(GameFile * child)
{
  if (child->fullname().endsWith(QLatin1String(".m2"), Qt::CaseInsensitive))
    m_modelFiles.insert(child);
  auto it = m_nameMap.find(child->fullname());
  if (it == m_nameMap.end())
  {
    m_nameMap.emplace(child->fullname(), child);
    countType(child->fullname(), +1);
  }
  else
    it->second = child;
}

void core::GameFolder::onChildRemoved(GameFile * child)
{
  m_modelFiles.erase(child);
  // Only when the name still leads to this file: a reload can have listed its replacement already.
  auto it = m_nameMap.find(child->fullname());
  if (it != m_nameMap.end() && it->second == child)
  {
    countType(it->first, -1);
    m_nameMap.erase(it);
  }
}

void core::GameFolder::replaceNameIndex(std::map<QString, GameFile *> && index)
{
  m_nameMap.swap(index);
  m_typeCounts = TypeCounts();
  for (const auto & entry : m_nameMap)
    countType(entry.first, +1);
  index.clear(); // the previous index, freed here rather than by the caller
}

void core::GameFolder::replaceModelFiles(const std::vector<GameFile *> & models)
{
  std::unordered_set<GameFile *> next;
  next.reserve(models.size());
  next.insert(models.begin(), models.end());
  m_modelFiles.swap(next);
}

void core::GameFolder::countType(const QString & path, int delta)
{
  if (path.endsWith(QLatin1String(".m2"), Qt::CaseInsensitive))
    m_typeCounts.models += delta;
  else if (path.endsWith(QLatin1String(".blp"), Qt::CaseInsensitive))
    m_typeCounts.textures += delta;
  else if (path.endsWith(QLatin1String(".wmo"), Qt::CaseInsensitive))
    m_typeCounts.buildings += delta;
}

