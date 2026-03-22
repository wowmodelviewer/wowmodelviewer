#pragma once

#include <map>
#include "CASCFolder.h"
#include "GameFile.h"
#include "GameFolder.h"

namespace wow
{
	/// @brief GameFolder implementation backed by a CASC archive.
	///
	/// Wraps CASCFolder to provide file lookup by name and by FileDataID,
	/// listfile loading, and custom-file overlay.
	class WoWFolder : public core::GameFolder
	{
	public:
		WoWFolder(const std::string& path);

		virtual ~WoWFolder()
		{
		}

		void init() override;
		void initFromListfile(const std::string& file) override;
		void addCustomFiles(const std::string& path, bool bypassOriginalFiles) override;

		GameFile* getFile(int id) override;

		bool openFile(int id, HANDLE* result) override;
		bool openFile(std::string file, HANDLE* result) override;

		std::string version() override;
		int majorVersion() override;
		std::string locale() override;
		bool setConfig(core::GameConfig config) override;
		std::vector<core::GameConfig> configsFound() override;

		int lastError() override;

			void onChildAdded(GameFile*) override;
			void onChildRemoved(GameFile*) override;
			std::string fileName(int id);
			int fileID(const std::string& fileName);

		private:
			CASCFolder m_CASCFolder;
			std::map<int, GameFile*> m_idMap;
			std::map<int, std::string> m_idNameMap;
			std::map<std::string, int> m_nameIdMap;
		};
}
