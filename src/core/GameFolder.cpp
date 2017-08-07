/*
 * GameFolder.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "GameFolder.h"

#include <QDirIterator>
#include <QFile>
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
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    if((*it)->name().contains(regex))
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
  m_nameMap[child->fullname()] = child;
}

void core::GameFolder::onChildRemoved(GameFile * child)
{
  m_nameMap.erase(child->fullname());
}

