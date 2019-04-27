/*
 * WoWFolder.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "WoWFolder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "Game.h"
#include "HardDriveFile.h"

#include "logger/Logger.h"

wow::WoWFolder::WoWFolder(const QString & path)
  : GameFolder(path)
{
}

void wow::WoWFolder::init()
{
  m_CASCFolder.init(path());
}


void wow::WoWFolder::initFromListfile(const QString & filename)
{
  QFile file(core::Game::instance().configFolder() + filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Failed to open" << filename;
    return;
  }

  QTextStream in(&file);

  LOG_INFO << "WoWFolder - Starting to build object hierarchy";
  while (!in.atEnd())
  {
    QString line = in.readLine().toLower();
    QStringList lineData = line.split(';');
    if (lineData.size() < 2)
      continue;
    int id = lineData.at(0).toInt();
    QString fileName = lineData.at(1);
    // Add the file to the name-ID mappings even if it can't be found in CASC,
    // as it could be a custom file added by the user:
    m_idNameMap[id] = fileName;
    m_nameIdMap[fileName] = id;
    if (m_CASCFolder.fileExists(id))
    {
      CASCFile * file = new CASCFile(fileName, id);
      file->setName(line.mid(line.lastIndexOf('/') + 1));
      addChild(file);
    }
  }
  LOG_INFO << "WoWFolder - Hierarchy creation done";
}

void wow::WoWFolder::addCustomFiles(const QString & path, bool bypassOriginalFiles)
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

      GameFile * originalFile = GameFolder::getFile(filePath);
      bool addnewfile = true;
      int originalId = -1;
      if(originalFile)
      {
        if(bypassOriginalFiles)
        {
          originalId = originalFile->fileDataId();
          removeChild(originalFile);
          delete originalFile;
          originalFile = 0;
          addnewfile = true;
        }
        else
        {
          addnewfile = false;
        }
      }
      else
      {
        // Even though the file wasn't found in the game database, it's possible to assign it
        // a specific ID in the listfile (useful in some situations) :
        auto it = m_nameIdMap.find(filePath);
        if (it != m_nameIdMap.end())
          originalId = it->second;
      }
      if(addnewfile)
      {
        LOG_INFO << "Add custom file" << filePath << "(ID:" << originalId << ")from hard drive location" << dirIt.filePath();
        HardDriveFile * file = new HardDriveFile(filePath, dirIt.filePath(), originalId);
        file->setName(filePath.mid(filePath.lastIndexOf("/")+1));
        addChild(file);
      }
    }
  }
}


GameFile * wow::WoWFolder::getFile(int id)
{
  GameFile * result = 0;

  if (id <= 0) // bad id given
    return result;

  auto it = m_idMap.find(id);
  if (it != m_idMap.end())
    result = it->second;

  if (!result) // if not found, try to force open by id
  {
    // Build File########.unk filename needed for CASC lib to open file based on id
    QString filename = QString("File%1.unk").arg(id, 8, 16, QLatin1Char('0'));
    LOG_INFO << "File with id" << id << "not found in listfile. Trying to open" << filename;

    HANDLE newfile;
    if(m_CASCFolder.openFile(id, &newfile))
    {
      LOG_INFO << "Succesfully opened";
      m_CASCFolder.closeFile(newfile);
      CASCFile * file = new CASCFile(filename, id);
      file->setName(filename);
      addChild(file);
      result = file;
    }
  }

  return result;
}

bool wow::WoWFolder::openFile(int id, HANDLE * result)
{
  return m_CASCFolder.openFile(id, result);
}

bool wow::WoWFolder::openFile(std::string file, HANDLE * result)
{
  auto it = m_nameIdMap.find(QString::fromStdString(file));
  if (it == m_nameIdMap.end())
    return false;
  return m_CASCFolder.openFile(it->second, result);
}

QString wow::WoWFolder::version()
{
  return m_CASCFolder.version();
}

QString wow::WoWFolder::locale()
{
   return m_CASCFolder.locale();
}

bool wow::WoWFolder::setConfig(core::GameConfig config)
{
  return m_CASCFolder.setConfig(config);
}

std::vector<core::GameConfig> wow::WoWFolder::configsFound()
{
  return m_CASCFolder.configsFound();
}

int wow::WoWFolder::lastError()
{
  return m_CASCFolder.lastError();
}

void wow::WoWFolder::onChildAdded(GameFile * child)
{
  GameFolder::onChildAdded(child);
  m_idMap[child->fileDataId()] = child;
}

void wow::WoWFolder::onChildRemoved(GameFile * child)
{
  GameFolder::onChildRemoved(child);
  m_idMap.erase(child->fileDataId());
}

QString wow::WoWFolder::fileName(int id)
{
  auto it = m_idNameMap.find(id);
  if (it == m_idNameMap.end())
    return QString();
  return it->second;
}

int wow::WoWFolder::fileID(QString fileName)
{
  auto it = m_nameIdMap.find(fileName);
  if (it == m_nameIdMap.end())
    return -1;
  return it->second;
}
   