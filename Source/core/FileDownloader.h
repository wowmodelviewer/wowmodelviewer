#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QString>
#include <QWaitCondition>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _FILEDOWNLOADER_API_ __declspec(dllexport)
#    else
#        define _FILEDOWNLOADER_API_ __declspec(dllimport)
#    endif
#else
#    define _FILEDOWNLOADER_API_
#endif

class _FILEDOWNLOADER_API_ FileDownloader : public QObject
{
	Q_OBJECT

public:
	explicit FileDownloader(QObject* parent = nullptr);
	~FileDownloader();

	void get(QUrl url);

	QByteArray m_datas;
	QString m_fileName;

signals:
	void downloadFinished(QString&);

private slots:
	void fileDownloaded();
	void downloadError(QNetworkReply::NetworkError error);

private:
	QNetworkAccessManager m_manager;
	QWaitCondition m_locker;
};
