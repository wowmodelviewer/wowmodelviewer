/*
 * Game.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "Game.h"

Game * Game::m_instance = 0;

Game::Game()
  : m_db(0)
{
}

void Game::init(const QString & path, core::GameDatabase * db)
{
  m_db = db;
  m_gameFolder.init(path);
}

void Game::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  m_gameFolder.addCustomFiles(path, bypassOriginalFiles);
}

