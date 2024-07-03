#include "GameFolder.h"
#include <QRegularExpression>
#include "logger/Logger.h"

core::GameFolder::GameFolder(QString path) : m_path(std::move(path))
{
}

QString core::GameFolder::getFullPathForFile(QString file)
{
	file = file.toLower();
	for (const auto it : *this)
	{
		if (it->name() == file)
			return it->fullname();
	}

	return "";
}

void core::GameFolder::getFilesForFolder(std::vector<GameFile*>& fileNames, QString folderPath, QString extension)
{
	for (auto file : *this)
	{
		if (file->fullname().startsWith(folderPath, Qt::CaseInsensitive) &&
			(!extension.size() || file->fullname().endsWith(extension, Qt::CaseInsensitive)))
		{
			fileNames.push_back(file);
		}
	}
}

void core::GameFolder::getFilteredFiles(std::set<GameFile*>& dest, QString& filter)
{
	const QRegularExpression regex(filter);

	if (!regex.isValid())
	{
		LOG_ERROR << regex.errorString();
		return;
	}
	for (auto it : *this)
	{
		if (it->name().contains(regex))
		{
			dest.insert(it);
		}
	}
}

GameFile* core::GameFolder::getFile(QString filename)
{
	filename = filename.toLower().replace('\\', '/');

	GameFile* result = nullptr;

	const auto it = m_nameMap.find(filename);
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
