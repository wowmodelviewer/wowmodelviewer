#include "GameFolder.h"
#include <regex>
#include "string_utils.h"
#include "logger/Logger.h"

core::GameFolder::GameFolder(std::string path) : m_path(std::move(path))
{
}

std::string core::GameFolder::getFullPathForFile(const std::string& file)
{
	const std::string lower = core::toLower(file);
	for (const auto it : *this)
	{
		if (it->name() == lower)
			return it->fullname();
	}

	return "";
}

void core::GameFolder::getFilesForFolder(std::vector<GameFile*>& fileNames, const std::string& folderPath, const std::string& extension)
{
	for (auto file : *this)
	{
		const auto& fn = file->fullname();
		if (core::startsWithIgnoreCase(fn, folderPath) &&
			(extension.empty() || core::endsWithIgnoreCase(fn, extension)))
		{
			fileNames.push_back(file);
		}
	}
}

void core::GameFolder::getFilteredFiles(std::set<GameFile*>& dest, const std::string& filter)
{
	std::regex regex;
	try
	{
		regex = std::regex(filter, std::regex_constants::icase);
	}
	catch (const std::regex_error& e)
	{
		LOG_ERROR << e.what();
		return;
	}
	int count = 0;
	const int total = static_cast<int>(nbChildren());
	for (auto it : *this)
	{
		if (std::regex_search(it->name(), regex))
		{
			dest.insert(it);
		}
		if (m_progressCallback && ++count % 500 == 0)
			m_progressCallback(count, total);
	}
}

GameFile* core::GameFolder::getFile(const std::string& filename)
{
	std::string lower = core::toLower(filename);
	std::replace(lower.begin(), lower.end(), '\\', '/');

	GameFile* result = nullptr;

	const auto it = m_nameMap.find(lower);
	if (it != m_nameMap.end())
		result = it->second;

	return result;
}

void core::GameFolder::onChildAdded(GameFile* child)
{
	m_nameMap[child->fullname()] = child;
}

void core::GameFolder::onChildRemoved(GameFile* child)
{
	m_nameMap.erase(child->fullname());
}
