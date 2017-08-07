/*
 * Game.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "Game.h"

core::Game * core::Game::m_instance = 0;

core::Game::Game()
  : m_db(0), m_folder(0)
{
}

void core::Game::init(core::GameFolder * folder, core::GameDatabase * db)
{
  m_db = db;
  m_folder = folder;
  if (m_folder)
    m_folder->init();
}

void core::Game::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  if (m_folder)
    m_folder->addCustomFiles(path, bypassOriginalFiles);
}

