/*
 * GameFolder.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */


#include "GameFolder.h"

#include "CASCFile.h"

GameFolder::GameFolder(QString & path)
{
  setName(path);
}

void GameFolder::createChildren(QStringList & items)
{
  if(items.size() == 0)
    return;

  QString item = items[0];

  if(item.contains("."))
  {
    GameFile * file = new CASCFile();
    file->setName(item);
    addChild(file);
    return;
  }

  items.removeFirst();

  GameFolder * folder = getFolder(item);
  if(!folder)
  {
    folder = new GameFolder(item);
    addChild(folder);
  }

  folder->createChildren(items);
}

void GameFolder::onChildAdded(Component * child)
{
  GameFolder * folder = dynamic_cast<GameFolder *>(child);
  if(folder)
    m_subFolderMap[folder->name()] = folder;
}

GameFolder * GameFolder::getFolder(QString &name)
{
  std::map<QString, GameFolder *>::iterator result = m_subFolderMap.find(name);

  if(result != m_subFolderMap.end())
    return result->second;

  return 0;
}
