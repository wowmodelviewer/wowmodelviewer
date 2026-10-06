/*
 * Game.cpp
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#include "Game.h"

core::Game * core::Game::m_instance = 0;

core::Game::Game()
  : m_folder(nullptr), m_db(nullptr)
{
}

void core::Game::init(core::GameFolder * folder, core::GameDatabase * db)
{
  m_db = db;
  m_folder = folder;
  if (m_folder)
    m_folder->init();
}

void core::Game::replace(core::GameFolder * folder, core::GameDatabase * db)
{
  if (m_db && m_db != db)
    delete m_db;
  m_db = db;
  m_folder = folder;
}

void core::Game::addCustomFiles(const QString & path, bool bypassOriginalFiles)
{
  if (m_folder)
    m_folder->addCustomFiles(path, bypassOriginalFiles);
}

