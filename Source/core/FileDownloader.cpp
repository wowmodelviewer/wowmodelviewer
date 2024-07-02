#include "FileDownloader.h"

FileDownloader::FileDownloader(QObject* parent) : QObject(parent)
{
}

FileDownloader::~FileDownloader() = default;

void FileDownloader::get(QUrl url)
{
	QNetworkRequest request(url);
	m_fileName = url.toString();
	QNetworkReply* reply = m_manager.get(request);
	connect(reply, &QNetworkReply::finished, this, &FileDownloader::fileDownloaded);
	connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
	        this, SLOT(downloadError(QNetworkReply::NetworkError)));
}

void FileDownloader::fileDownloaded()
{
	QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
	m_datas = reply->readAll();
	reply->deleteLater();
	emit downloadFinished(m_fileName);
}

void FileDownloader::downloadError(QNetworkReply::NetworkError error)
{
	// Report the error
	qWarning("Download Error Occured: %i", error);
}
