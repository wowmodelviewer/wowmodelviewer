#pragma once

#include <memory>
#include <string>
#include "GameFolder.h"

namespace core { class GameDatabase; }

#define GAMEDIRECTORY core::Game::instance().folder()
#define GAMEDATABASE core::Game::instance().database()

namespace core
{
	/// @brief Singleton entry point for the game data layer.
	///
	/// Owns the GameFolder (archive access) and GameDatabase (DB2 tables).
	/// Access via the GAMEDIRECTORY and GAMEDATABASE macros.
	class Game
	{
	public:
		/// @brief Access the singleton instance (Meyers singleton).
		static Game& instance()
		{
			static Game s_instance;
			return s_instance;
		}

		/// @brief Initialise with the given folder and database backends.
		void init(std::unique_ptr<core::GameFolder> folder, std::unique_ptr<core::GameDatabase> db);

		/// @brief True once both the folder and database have been set.
		bool initDone() { return (m_db != nullptr) && (m_folder != nullptr); }

		/// @brief Overlay custom loose files on top of the game archive.
		void addCustomFiles(const std::string& path, bool bypassOriginalFiles);

		core::GameFolder& folder() { return *m_folder; }
		core::GameDatabase& database() { return *m_db; }

		void setConfigFolder(const std::string& folder) { m_configFolder = folder; }
		std::string configFolder() { return m_configFolder; }

		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;

	private:
		Game();
		~Game();

		std::unique_ptr<core::GameFolder> m_folder;
		std::unique_ptr<core::GameDatabase> m_db;

		std::string m_configFolder;
	};
}
