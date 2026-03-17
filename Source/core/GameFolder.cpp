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
		if (QString::fromStdString(it->name()) == file)
			return QString::fromStdString(it->fullname());
	}

	return "";
}

void core::GameFolder::getFilesForFolder(std::vector<GameFile*>& fileNames, QString folderPath, QString extension)
{
	for (auto file : *this)
	{
		const QString fn = QString::fromStdString(file->fullname());
		if (fn.startsWith(folderPath, Qt::CaseInsensitive) &&
			(!extension.size() || fn.endsWith(extension, Qt::CaseInsensitive)))
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
	int count = 0;
	const int total = static_cast<int>(nbChildren());
	for (auto it : *this)
	{
		if (QString::fromStdString(it->name()).contains(regex))
		{
			dest.insert(it);
		}
		if (m_progressCallback && ++count % 500 == 0)
			m_progressCallback(count, total);
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
	m_nameMap[QString::fromStdString(child->fullname())] = child;
}

void core::GameFolder::onChildRemoved(GameFile* child)
{
	m_nameMap.erase(QString::fromStdString(child->fullname()));
}
