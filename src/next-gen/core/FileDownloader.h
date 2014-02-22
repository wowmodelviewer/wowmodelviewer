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
 * FileDownloader.h
 *
 *  Created on: 11 janv. 2014
 *   Copyright: 2014 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _FILEDOWNLOADER_H_
#define _FILEDOWNLOADER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QString>
#include <QWaitCondition>

// Externals

// Other libraries

// Current library


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class FileDownloader : public QObject
{
    Q_OBJECT
	public :
		// Constants / Enums
		
		// Constructors
    explicit FileDownloader(QObject *parent = 0);

		// Destructors
    ~FileDownloader();

		// Methods
    void get(QUrl url);

		// Members
    QByteArray m_datas;
    QString m_fileName;

  signals:
    void downloadFinished(QString &);

	protected :
		// Constants / Enums
	
		// Constructors
	
		// Destructors
	
		// Methods
		
		// Members
		
	private :
		// Constants / Enums
	
		// Constructors
	
		// Destructors
	
		// Methods
  private slots:
       void fileDownloaded();


  private:
		// Members
    QNetworkAccessManager m_manager;
    QWaitCondition m_locker;
		
		// friend class declarations
	
};

// static members definition
#ifdef _FILEDOWNLOADER_CPP_

#endif

#endif /* _FILEDOWNLOADER_H_ */
