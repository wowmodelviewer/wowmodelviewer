#pragma once

#include <map>
#include "CASCFolder.h"
#include "GameFile.h"
#include "GameFolder.h"

#define _WOWFOLDER_API_

namespace wow
{
	class _WOWFOLDER_API_ WoWFolder : public core::GameFolder
	{
	public:
		WoWFolder(const QString& path);

		virtual ~WoWFolder()
		{
		}

		void init() override;
		void initFromListfile(const QString& file) override;
		void addCustomFiles(const QString& path, bool bypassOriginalFiles) override;

		GameFile* getFile(int id) override;

		bool openFile(int id, HANDLE* result) override;
		bool openFile(std::string file, HANDLE* result) override;

		QString version() override;
		int majorVersion() override;
		QString locale() override;
		bool setConfig(core::GameConfig config) override;
		std::vector<core::GameConfig> configsFound() override;

		int lastError() override;

		void onChildAdded(GameFile*) override;
		void onChildRemoved(GameFile*) override;
		QString fileName(int id);
		int fileID(QString fileName);

	private:
		CASCFolder m_CASCFolder;
		std::map<int, GameFile*> m_idMap;
		std::map<int, QString> m_idNameMap;
		std::map<QString, int> m_nameIdMap;
	};
}
