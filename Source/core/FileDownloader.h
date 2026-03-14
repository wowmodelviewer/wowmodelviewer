#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkReply>
#include <QString>
#include <QWaitCondition>

#define _FILEDOWNLOADER_API_

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
