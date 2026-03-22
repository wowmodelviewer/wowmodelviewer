#pragma once

#include <string>
#include "GameFolder.h"

namespace core { class GameDatabase; }

#define GAMEDIRECTORY core::Game::instance().folder()
#define GAMEDATABASE core::Game::instance().database()

#define _GAME_API_

namespace core
{
	/// @brief Singleton entry point for the game data layer.
	///
	/// Owns the GameFolder (archive access) and GameDatabase (DB2 tables).
	/// Access via the GAMEDIRECTORY and GAMEDATABASE macros.
	class _GAME_API_ Game
	{
	public:
		/// @brief Access the singleton instance (created on first call).
		static Game& instance()
		{
			if (Game::m_instance == nullptr)
				Game::m_instance = new Game();
			return *m_instance;
		}

		/// @brief Initialise with the given folder and database backends.
		void init(core::GameFolder* folder, core::GameDatabase* db);

		/// @brief True once both the folder and database have been set.
		bool initDone() { return ((m_db != nullptr) && (m_folder != nullptr)); }

		/// @brief Overlay custom loose files on top of the game archive.
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
