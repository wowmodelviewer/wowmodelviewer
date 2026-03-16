#pragma once

#include <string>
#include "GameFolder.h"
#include "GameDatabase.h"

#define GAMEDIRECTORY core::Game::instance().folder()
#define GAMEDATABASE core::Game::instance().database()

#define _GAME_API_

namespace core
{
	class _GAME_API_ Game
	{
	public:
		static Game& instance()
		{
			if (Game::m_instance == nullptr)
				Game::m_instance = new Game();
			return *m_instance;
		}

		void init(core::GameFolder* folder, core::GameDatabase* db);
		bool initDone() { return ((m_db != nullptr) && (m_folder != nullptr)); }
		void addCustomFiles(const std::string& path, bool bypassOriginalFiles);

		core::GameFolder& folder() { return *m_folder; }
		core::GameDatabase& database() { return *m_db; }

		void setConfigFolder(const std::string& folder) { m_configFolder = folder; }
		std::string configFolder() { return m_configFolder; }

	private:
		// disable explicit construct and destruct
		Game();

		virtual ~Game() = default;

		Game(const Game&);
		void operator=(const Game&);

		core::GameFolder* m_folder;
		core::GameDatabase* m_db;

		std::string m_configFolder;

		static Game* m_instance;
	};
}
