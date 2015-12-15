/*
 * GameFolder.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "GameFolder.h"

#include <QFile>

#include "CASCFile.h"
#include "Game.h"

#include "logger/Logger.h"

GameFolder::GameFolder()
{
}

void GameFolder::init(const QString & path, const QString & filename)
{
  m_CASCFolder.init(path);

  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << filename;
    return;
  }

  QTextStream in(&file);

  LOG_INFO << __FUNCTION__ << "Start to build object hierarchy";
  while (!in.atEnd())
  {
    QString line = in.readLine();
    line = line.toLower();

    QStringList items = line.split("\\");

    CASCFile * file = new CASCFile();
    file->setName(items[items.size()-1]);
    file->setFullName(line);
    addChild(file);
  }
  LOG_INFO << __FUNCTION__ << "Hierarchy creation done";
}

QString GameFolder::getFullPathForFile(QString file)
{
  LOG_INFO << __FUNCTION__ << file;
  file = file.toLower();
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    if((*it)->name() == file)
    {
      LOG_INFO << (*it)->fullname();
      return (*it)->fullname();
    }
  }

  return "";
}

void GameFolder::getFilesForFolder(std::vector<QString> &fileNames, QString folderPath)
{
  LOG_INFO << __FUNCTION__ << folderPath;
  folderPath = folderPath.toLower();
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    GameFile * file = *it;
    if(file->fullname().startsWith(folderPath))
    {
      fileNames.push_back(file->name());
    }
  }
}

void GameFolder::filterFileList(std::set<FileTreeItem> &dest, bool filterfunc(QString)/* = GameFolder::defaultFilterFunc*/)
{
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {

    if(filterfunc((*it)->name()))
    {
      FileTreeItem tmp;
      tmp.displayName = (*it)->fullname();
      tmp.color = 0;
      dest.insert(tmp);
    }
  }
}

HANDLE GameFolder::openFile(std::string file)
{
  return m_CASCFolder.openFile(file);
}

QString GameFolder::version()
{
  return m_CASCFolder.version();
}

bool GameFolder::fileExists(std::string file)
{
   return m_CASCFolder.fileExists(file);
}

std::string GameFolder::locale()
{
   return m_CASCFolder.locale();
}


bool GameFolder::setLocale(std::string val)
{
  return m_CASCFolder.setLocale(val);
}

std::vector<std::string> GameFolder::localesFound()
{
  return m_CASCFolder.localesFound();
}

int GameFolder::lastError()
{
  return m_CASCFolder.lastError();
}
