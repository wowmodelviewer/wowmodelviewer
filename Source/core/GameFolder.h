#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include "GameFile.h"
#include "metaclasses/Container.h"

#define _GAMEFOLDER_API_

namespace core
{
	using ProgressCallback = std::function<void(int, int)>;

	class _GAMEFOLDER_API_ GameConfig
	{
	public:
		std::string locale;
		std::string version;
		std::string product;
	};

	class _GAMEFOLDER_API_ GameFolder : public Container<GameFile>
	{
	public:
		explicit GameFolder(std::string path);

		virtual ~GameFolder()
		{
		}

		virtual void init() = 0;
		virtual void initFromListfile(const std::string& file) = 0;
		virtual void addCustomFiles(const std::string& path, bool bypassOriginalFiles) = 0;

		// return full path for a given file ie :
		// HumanMale.m2 => Character\Human\male\humanmale.m2
		// (not always accurate, as file names not always unique)
		std::string getFullPathForFile(const std::string& file);

		void getFilesForFolder(std::vector<GameFile*>& fileNames, const std::string& folderPath, const std::string& extension = "");
		void getFilteredFiles(std::set<GameFile*>& dest, const std::string& filter);
		GameFile* getFile(const std::string& filename);
		virtual GameFile* getFile(int id) = 0;

		virtual bool openFile(std::string file, void** result) = 0;
		virtual bool openFile(int id, void** result) = 0;

		virtual std::string version() = 0;
		virtual int majorVersion() = 0;
		virtual std::string locale() = 0;
		virtual bool setConfig(GameConfig config) = 0;
		virtual std::vector<GameConfig> configsFound() = 0;

		virtual int lastError() = 0;

		virtual void onChildAdded(GameFile*) override;
		virtual void onChildRemoved(GameFile*) override;

		const std::string& path() const { return m_path; }

		void setProgressCallback(ProgressCallback cb) { m_progressCallback = std::move(cb); }

	protected:
		ProgressCallback m_progressCallback;

	private:
		std::map<std::string, GameFile*> m_nameMap;
		std::string m_path;
	};
}
