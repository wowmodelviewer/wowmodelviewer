/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/


/*
 * FileDownloader.cpp
 *
 *  Created on: 11 janv. 2014
 *   Copyright: 2014 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _FILEDOWNLOADER_CPP_
#include "FileDownloader.h"
#undef _FILEDOWNLOADER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>

// Qt 
#include <QMutex>

// Externals

// Other libraries

// Current library


// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------
FileDownloader::FileDownloader(QObject *parent) :
  QObject(parent)
{
}

FileDownloader::~FileDownloader()
{

}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
void FileDownloader::get(QUrl url)
{
  QNetworkRequest request(url);
  m_fileName = url.toString();
  QNetworkReply * reply = m_manager.get(request);
  connect(reply,SIGNAL(finished()), this, SLOT(fileDownloaded()));
  connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
                   this, SLOT(downloadError(QNetworkReply::NetworkError)));
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void FileDownloader::fileDownloaded()
{
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  m_datas = reply->readAll();
  reply->deleteLater();
  emit downloadFinished(m_fileName);
}
