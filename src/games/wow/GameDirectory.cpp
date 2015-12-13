/*
 * GameDirectory.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "GameDirectory.h"

#include <QFile>

#include "CASCFile.h"
#include "CASCFolder.h"
#include "GameFolder.h"
#include "logger/Logger.h"

GameDirectory * GameDirectory::m_instance = 0;

std::map<QString,QString> globalNameMap;
std::vector<std::pair<QString,QString>> folderFileList;

GameDirectory::GameDirectory() :
    m_gameFolder(0)
{
}

void GameDirectory::init(const QString & path)
{
  m_gameFolderPath = path;
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  m_gameFolder->init(path);
}

QString GameDirectory::version()
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  return m_gameFolder->version();
}

void GameDirectory::initFileList(std::set<FileTreeItem> &dest)
{
  QFile file("listfile.txt");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open listfile.txt.";
    return;
  }

  QTextStream in(&file);

  std::map<QString, GameFolder *> alreadyCreatedSubfolders;
  LOG_INFO << __FUNCTION__ << "Start to build object hierarchy";
  while (!in.atEnd())
  {
    QString line = in.readLine();
    beautifyFileName(line);

    QStringList items = line.split("\\");

    GameFolder * folder = 0;

    std::map<QString, GameFolder *>::iterator it = alreadyCreatedSubfolders.find(items[0]);
    if(it != alreadyCreatedSubfolders.end())
      folder = it->second;

    if(!folder)
    {
      if(items[0].contains("."))
      {
        CASCFile * file = new CASCFile();
        file->setName(items[0]);
        addChild(file);
      }
      else
      {
        folder = new GameFolder(items[0]);
        addChild(folder);
        alreadyCreatedSubfolders[items[0]] = folder;
      }
    }

    items.removeFirst();

    if(folder)
      folder->createChildren(items);

    addFile(line);

    FileTreeItem tmp;
    tmp.displayName = line;
    tmp.color = 0;
    dest.insert(tmp);
  }
  LOG_INFO << __FUNCTION__ << "Hierarchy creation done";
}

void GameDirectory::filterFileList(std::set<FileTreeItem> &dest, bool filterfunc(QString)/* = GameDirectory::defaultFilterFunc*/)
{
  std::map<QString,QString>::iterator itEnd = globalNameMap.end();
  for(std::map<QString,QString>::iterator it = globalNameMap.begin();
      it != itEnd;
      ++it)
  {
    if(filterfunc(it->second))
    {
      FileTreeItem tmp;
      tmp.displayName = it->second;
      tmp.color = 0;
      dest.insert(tmp);
    }
  }
}

QString GameDirectory::getFullPathForFile(QString file)
{
  std::map<QString,QString>::iterator it = globalNameMap.find(file.toLower());

  if(it != globalNameMap.end())
    return it->second;

  return "";
}

void GameDirectory::getFilesForFolder(std::vector<QString> &fileNames, QString folderPath)
{
  std::vector<std::pair<QString,QString>> matches;

  // make a new vector array with only those pairs that match the supplied folder path:
  std::copy_if(folderFileList.begin(), folderFileList.end(), back_inserter(matches),
               [folderPath](std::pair<QString,QString>& p)
                  { return (QString::compare(p.first, folderPath, Qt::CaseInsensitive) == 0); });

  // Convert to a simpler vector array of full file names:
  std::transform(matches.begin(), matches.end(), std::back_inserter(fileNames),
                 [](std::pair<QString, QString>& p) { return p.first + p.second; });
}

bool GameDirectory::fileExists(std::string file)
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

   return m_gameFolder->fileExists(file);
}

void GameDirectory::beautifyFileName(QString & file)
{
  file = file.toLower();
  QString firstLetter = file[0];
  firstLetter = firstLetter.toUpper();
  file[0] = firstLetter[0];
  int ret = file.indexOf('\\');
  if (ret>-1)
  {
    firstLetter = file[ret+1];
    firstLetter = firstLetter.toUpper();
    file[ret+1] = firstLetter[0];
  }
}


void GameDirectory::addFile(QString & file)
{
  QString fileName = file.section('\\',-1).toLower();
  QString filePath = (file.section('\\', 0, -2).toLower()) + "\\";
  folderFileList.push_back(std::make_pair(filePath, fileName));

  // fill global map for path search
  fileName = file.section('\\',-1).toLower();
  globalNameMap[fileName] = file;
}

HANDLE GameDirectory::openFile(std::string file)
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  return m_gameFolder->openFile(file);
}

std::string GameDirectory::locale()
{
  if(m_gameFolder == 0)
     m_gameFolder = new CASCFolder();

   return m_gameFolder->locale();
}


bool GameDirectory::setLocale(std::string val)
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  return m_gameFolder->setLocale(val);
}


std::vector<std::string> GameDirectory::localesFound()
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  return m_gameFolder->localesFound();
}

int GameDirectory::lastError()
{
  if(m_gameFolder == 0)
    m_gameFolder = new CASCFolder();

  return m_gameFolder->lastError();
}
