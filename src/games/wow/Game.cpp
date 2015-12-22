/*
 * Game.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "Game.h"

Game * Game::m_instance = 0;

Game::Game()
{
}

void Game::init(const QString & path)
{
  m_gameFolder.init(path, "listfile.txt");
}

void Game::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  m_gameFolder.addCustomFiles(path, bypassOriginalFiles);
}

