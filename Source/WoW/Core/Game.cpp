#include "Game.h"
#include "GameDatabase.h"

core::Game::Game() = default;
core::Game::~Game() = default;

void core::Game::init(std::unique_ptr<core::GameFolder> folder, std::unique_ptr<core::GameDatabase> db)
{
	m_db = std::move(db);
	m_folder = std::move(folder);
	if (m_folder)
		m_folder->init();
}

void core::Game::addCustomFiles(const std::string& path, bool bypassOriginalFiles)
{
	if (m_folder)
		m_folder->addCustomFiles(path, bypassOriginalFiles);
}
