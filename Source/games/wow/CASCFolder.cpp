#include "CASCFolder.h"

#ifndef __CASCLIB_SELF__
#define __CASCLIB_SELF__
#endif
#include "CascLib.h"

#include <locale>
#include <map>
#include <utility>
#include <fstream>
#include <regex>
#include <string>

#include "CASCFile.h"
#include "string_utils.h"
#include "logger/Logger.h"

CASCFolder::CASCFolder()
	: m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS), hStorage(nullptr)
{
}

void CASCFolder::init(const QString& path)
{
	m_folder = path;

	if (m_folder.endsWith("\\"))
		m_folder.remove(m_folder.size() - 1, 1);

	initBuildInfo();
}

bool CASCFolder::setConfig(core::GameConfig config)
{
	m_currentConfig = config;

	// init map based on CASCLib
	std::map<QString, int> locales;
	locales["frFR"] = CASC_LOCALE_FRFR;
	locales["deDE"] = CASC_LOCALE_DEDE;
	locales["esES"] = CASC_LOCALE_ESES;
	locales["esMX"] = CASC_LOCALE_ESMX;
	locales["ptBR"] = CASC_LOCALE_PTBR;
	locales["itIT"] = CASC_LOCALE_ITIT;
	locales["ptPT"] = CASC_LOCALE_PTPT;
	locales["enGB"] = CASC_LOCALE_ENGB;
	locales["ruRU"] = CASC_LOCALE_RURU;
	locales["enUS"] = CASC_LOCALE_ENUS;
	locales["enCN"] = CASC_LOCALE_ENCN;
	locales["enTW"] = CASC_LOCALE_ENTW;
	locales["koKR"] = CASC_LOCALE_KOKR;
	locales["zhCN"] = CASC_LOCALE_ZHCN;
	locales["zhTW"] = CASC_LOCALE_ZHTW;

	// set locale
	if (!m_currentConfig.locale.isEmpty())
	{
		const auto& it = locales.find(m_currentConfig.locale);

		if (it != locales.end())
		{
			const QString cascParams = m_folder + "*" + m_currentConfig.product;
			LOG_INFO << "Loading Game Folder:" << cascParams;
			// locale found => try to open it
			if (!CascOpenStorage(cascParams.toStdWString().c_str(), it->second, &hStorage))
			{
				m_openError = GetLastError();
				LOG_ERROR << "CASCFolder: Opening" << cascParams << "failed." << "Error" << m_openError;
				return false;
			}

			addExtraEncryptionKeys();

			m_currentCascLocale = it->second;
		}
	}

	return true;
}

void CASCFolder::initBuildInfo()
{
	const QString buildinfofile = m_folder + "\\..\\.build.info";
	LOG_INFO << "buildinfofile : " << buildinfofile;

	std::ifstream file(buildinfofile.toStdWString());
	if (!file.is_open())
	{
		LOG_ERROR << "Fail to open .build.info to grab game config info";
		return;
	}

	std::string stdline;

	// read first line and grab VERSION index
	if (!std::getline(file, stdline))
		return;

	auto headers = core::split(stdline, '|');
	int activeIndex = 0;
	int versionIndex = 0;
	int tagIndex = 0;
	int productIndex = 0;
	for (int index = 0; index < static_cast<int>(headers.size()); index++)
	{
		if (core::containsIgnoreCase(headers[index], "Active"))
			activeIndex = index;
		else if (core::containsIgnoreCase(headers[index], "Version"))
			versionIndex = index;
		else if (core::containsIgnoreCase(headers[index], "Tags"))
			tagIndex = index;
		else if (core::containsIgnoreCase(headers[index], "Product"))
			productIndex = index;
	}

	// now loop across file lines with actual values
	const std::regex re(R"(^(\d+).(\d+).(\d+).(\d+)$)");
	while (std::getline(file, stdline))
	{
		std::string version;
		auto values = core::split(stdline, '|');

		// if inactive config, skip it
		if (values[activeIndex] == "0")
			continue;

		// grab version for this line
		std::smatch result;
		if (std::regex_match(values[versionIndex], result, re))
			version = result[1].str() + "." + result[2].str() + "." + result[3].str() + "." + result[4].str();

		// grab product name for this line
		const std::string product = values[productIndex];

		// grab locale(s) for this line
		auto tagValues = core::split(values[tagIndex], ':');
		for (const auto& value : tagValues)
		{
			if (value.find("text?") != std::string::npos)
			{
				auto tags = core::split(value, ' ');
				core::GameConfig config;
				config.locale = QString::fromStdString(tags[tags.size() - 2]);
				config.version = QString::fromStdString(version);
				config.product = QString::fromStdString(product);
				m_configs.push_back(config);
			}
		}
	}

	for (auto& it : m_configs)
		LOG_INFO << "config" << it.locale << it.version;
}

bool CASCFolder::fileExists(int id)
{
	//LOG_INFO << __FUNCTION__ << " " << file.c_str();
	if (!hStorage)
		return false;

	HANDLE dummy;

	if (CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, &dummy))
	{
		// LOG_INFO << "OK";
		CascCloseFile(dummy);
		return true;
	}
	// LOG_ERROR << "File" << id << "doesn't exist." << "Error" << GetLastError();
	return false;
}

bool CASCFolder::openFile(int id, HANDLE* result)
{
	return CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, result);
}

bool CASCFolder::closeFile(HANDLE file)
{
	return CascCloseFile(file);
}

void CASCFolder::addExtraEncryptionKeys()
{
	std::ifstream tactKeys("extraEncryptionKeys.csv");

		if (tactKeys.is_open())
	{
		std::string stdline;
		while (std::getline(tactKeys, stdline))
		{
			if (stdline.starts_with("##") || stdline.starts_with("\"##"))
				// ignore lines beginning with ##, useful for adding comments.
				continue;

			auto lineData = core::split(stdline, ';');
			if (lineData.size() != 2)
				continue;
			const std::string& keyName = lineData[0];
			const std::string& keyValue = lineData[1];
			if (keyName.empty() || keyValue.empty())
				continue;

			const unsigned long long keyNameVal = std::stoull(keyName, nullptr, 16);
			const bool ok2 = CascAddStringEncryptionKey(hStorage, keyNameVal, keyValue.c_str());
			if (!ok2)
				LOG_ERROR << "Failed to add TACT key from file, Name:" << keyName << ", Value:" << keyValue;
		}
	}
}

/*
int CASCFolder::fileDataId(std::string & filename)
{
  return CascGetFileId(hStorage, filename.c_str());
}
*/
