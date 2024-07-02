#pragma once

#include <map>
#include <QJsonObject>
#include <QObject>
#include <QString>

class UpdateManager;
class FileDownloader;

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _VERSIONMANAGER_API_ __declspec(dllexport)
#    else
#        define _VERSIONMANAGER_API_ __declspec(dllimport)
#    endif
#else
#    define _VERSIONMANAGER_API_
#endif

class _VERSIONMANAGER_API_ VersionManager : public QObject
{
	Q_OBJECT

public:
	VersionManager(QObject* parent = nullptr);
	~VersionManager()
	{
	}

	QString getLastVersionFor(QString&);
	static int compareVersion(const QString& v1, const QString& v2);

signals:
	void downloadFinished(QString&);

private:
	void updateCurrentVersionInfo();
	void checkForNewVersionAndExit();

private slots:
	void fileDownloaded(QString&);

private:
	FileDownloader* m_fileDownloader;
	std::map<QString, QString> m_currentVersionsMap;
	std::vector<QJsonObject> m_lastVersionInfos;

	bool m_standaloneVersion;

	friend class UpdateManager;
};
