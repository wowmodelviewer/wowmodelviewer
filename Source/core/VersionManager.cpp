#include "VersionManager.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "FileDownloader.h"
#include "GlobalSettings.h"
#include "PluginManager.h"

VersionManager::VersionManager(QObject* parent) : QObject(parent), m_standaloneVersion(parent == nullptr)
{
	m_fileDownloader = new FileDownloader(this);
	connect(m_fileDownloader, SIGNAL(downloadFinished(QString &)),
	        this, SLOT(fileDownloaded(QString &)));

	m_fileDownloader->get(QUrl("https://wowmodelviewer.net/update/latest.json"));
	updateCurrentVersionInfo();
}

void VersionManager::updateCurrentVersionInfo()
{
	// core version
	QString appName = QString::fromStdWString(GLOBALSETTINGS.appName());
	QString appVersion = QString::fromStdWString(GLOBALSETTINGS.appVersion().c_str());
	m_currentVersionsMap.insert(std::make_pair(appName, appVersion));

	// init plugins infos
	for (const auto it : PLUGINMANAGER)
	{
		QString pluginName(it->name());
		QString pluginVersion(it->version().c_str());
		m_currentVersionsMap.insert(std::make_pair(pluginName, pluginVersion));
	}
}

QString VersionManager::getLastVersionFor(QString& name)
{
	for (auto& m_lastVersionInfo : m_lastVersionInfos)
	{
		if (m_lastVersionInfo["name"].toString() == name)
		{
			return m_lastVersionInfo["version"].toString();
		}
	}
	return "";
}

void VersionManager::checkForNewVersionAndExit()
{
	// only check core version for now
	QString appName = QString::fromStdWString(GLOBALSETTINGS.appName());
	const QString currentVersion = m_currentVersionsMap[appName];
	const QString lastVersion = getLastVersionFor(appName);
	const int result = compareVersion(currentVersion, lastVersion);
	qApp->exit(result);
}

// returns -1 if v1 < v2
// returns 0 if v1 == v2
// returns 1 if v1 > v2
int VersionManager::compareVersion(const QString& v1, const QString& v2)
{
	QStringList v1list = v1.split(".");
	QStringList v2list = v2.split(".");

	int i = 0;
	for (; i < v1list.size(); i++)
	{
		if (i < v2list.size())
		{
			const int ver1 = v1list[i].toInt();
			const int ver2 = v2list[i].toInt();
			if (ver1 < ver2)
				return -1;
			if (ver1 > ver2)
				return 1;
		}
		else
		{
			// versions equals until last one available in v2, and some remains in v1
			// ==> consider that v1 is higher than v2
			return 1;
		}
	}

	// versions equals until last one available in v1, and some remains in v2
	// ==> consider that v2 is higher than v1
	if (i < v2list.size())
		return -1;

	// perfect equality if we arrive here
	return 0;
}

void VersionManager::fileDownloaded(QString& filename)
{
	if (filename.contains("latest.json"))
	{
		QJsonParseError error;
		const QJsonDocument datas = QJsonDocument::fromJson(m_fileDownloader->m_datas, &error);
		QJsonArray values = datas.array();

		for (auto&& value : values)
		{
			m_lastVersionInfos.push_back(value.toObject());
		}
		emit downloadFinished(filename);

		if (m_standaloneVersion)
		{
			checkForNewVersionAndExit();
		}
	}
}
