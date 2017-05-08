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

#include "CASCFile.h"
#include "Game.h"
#include "HardDriveFile.h"

#include "logger/Logger.h"

GameFolder::GameFolder()
{
}

void GameFolder::init(const QString & path)
{
  m_CASCFolder.init(path);
}


void GameFolder::initFiles(const QString & filename)
{
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << filename;
    return;
  }

  QTextStream in(&file);

  LOG_INFO << "GameFolder - Start to build object hierarchy";
  while (!in.atEnd())
  {
    QString line = in.readLine().toLower();
   
    CASCFile * file = new CASCFile(line, m_CASCFolder.fileDataId(line.toStdString()));
    file->setName(line.mid(line.lastIndexOf('/')+1));
    addChild(file);
  }
  LOG_INFO << "GameFolder - Hierarchy creation done";
}

void GameFolder::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  LOG_INFO << "Add customFiles from folder" << path;
  QDirIterator dirIt(path, QDirIterator::Subdirectories);

  while(dirIt.hasNext())
  {
    dirIt.next();
    QString filePath = dirIt.filePath().toLower();

    if(QFileInfo(filePath).isFile())
    {
      QString toRemove = path;
      toRemove += "\\";
      filePath.replace(0, toRemove.size(), "");

      GameFile * originalFile = getFile(filePath);
      bool addnewfile = true;
      if(originalFile)
      {
        if(bypassOriginalFiles)
        {
          removeChild(originalFile);
          delete originalFile;
          addnewfile = true;
        }
        else
        {
          addnewfile = false;
        }
      }

      if(addnewfile)
      {
        LOG_INFO << "Add custom file" << filePath << "from hard drive location" << dirIt.filePath();
        HardDriveFile * file = new HardDriveFile(filePath, dirIt.filePath(), originalFile ? originalFile->fileDataId() : -1);
        file->setName(filePath.mid(filePath.lastIndexOf("/")+1));
        addChild(file);
      }
    }
  }
}


QString GameFolder::getFullPathForFile(QString file)
{
  file = file.toLower();
  for(GameFolder::iterator it = begin() ; it != end() ; ++it)
  {
    if((*it)->name() == file)
      return (*it)->fullname();
  }

  return "";
}

void GameFolder::getFilesForFolder(std::vector<GameFile *> &fileNames, QString folderPath, QString extension)
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

void GameFolder::getFilteredFiles(std::set<GameFile *> &dest, QString & filter)
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

GameFile * GameFolder::getFile(QString filename)
{
  filename = filename.toLower().replace('\\','/');

  GameFile * result = 0;

  auto it = m_nameMap.find(filename);
  if (it != m_nameMap.end())
    result = it->second;

  return result;
}

GameFile * GameFolder::getFile(int id)
{
  GameFile * result = 0;

  auto it = m_idMap.find(id);
  if (it != m_idMap.end())
    result = it->second;

  if (!result) // if not found, try to force open by id
  {
    // Build File########.unk filename needed for CASC lib to open file based on id
    QString filename = QString("File%1.unk").arg(id, 8, 16, QLatin1Char('0'));
    HANDLE newfile = m_CASCFolder.openFile(filename.toStdString());

    if (newfile)
    {
      m_CASCFolder.closeFile(newfile);
      CASCFile * file = new CASCFile(filename, id);
      file->setName(filename);
      addChild(file);
      result = file;
    }
  }

  return result;
}


HANDLE GameFolder::openFile(std::string file)
{
  return m_CASCFolder.openFile(file);
}

QString GameFolder::version()
{
  return m_CASCFolder.version();
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

void GameFolder::onChildAdded(GameFile * child)
{
  m_nameMap[child->fullname()] = child;
  m_idMap[child->fileDataId()] = child;
}

void GameFolder::onChildRemoved(GameFile * child)
{
  m_nameMap.erase(child->fullname());
  m_idMap.erase(child->fileDataId());
}

