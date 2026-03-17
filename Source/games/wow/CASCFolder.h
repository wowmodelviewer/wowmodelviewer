/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>  // Include Windows headers for standard HANDLE definition
#endif

#include "GameFolder.h" // GameConfig

#define _CASCFOLDER_API_

class _CASCFOLDER_API_ CASCFolder
{
public:
	CASCFolder();

	void init(const std::string& path);

	std::string locale() { return m_currentConfig.locale; }
	std::string version() { return m_currentConfig.version; }

	std::vector<core::GameConfig> configsFound() { return m_configs; }
	bool setConfig(core::GameConfig config);

	int lastError() { return m_openError; }

	bool fileExists(int id);

	bool openFile(int id, HANDLE* result);
	bool closeFile(HANDLE file);

	// int fileDataId(std::string & filename);

private:
	CASCFolder(const CASCFolder&) = delete;
	CASCFolder& operator=(const CASCFolder&) = delete;

	//void initLocales();
	//void initVersion();
	void initBuildInfo();
	void addExtraEncryptionKeys();

	int m_currentCascLocale;
	core::GameConfig m_currentConfig;

	std::string m_folder;
	int m_openError;
	HANDLE hStorage;

	std::vector<core::GameConfig> m_configs;
};
