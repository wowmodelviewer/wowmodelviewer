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
 * VersionManager.h
 *
 *  Created on: 16 feb. 2014
 *   Copyright: 2014 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _VERSIONMANAGER_H_
#define _VERSIONMANAGER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <map>

// Qt
#include <QJsonObject>
#include <QObject>
#include <QString>

// Externals

// Other libraries
class UpdateManager;

// Current library
class FileDownloader;


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
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

	public :
		// Constants / Enums
		
		// Constructors
    VersionManager(QObject *parent = 0);
	
		// Destructors
    ~VersionManager() {}

		// Methods
    QString getLastVersionFor(QString &);
    static int compareVersion(const QString & v1, const QString & v2);

  signals:
    void downloadFinished(QString &);
		
		// Members
		
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
    void updateCurrentVersionInfo();
    void checkForNewVersionAndExit();

  private slots:
    void fileDownloaded(QString &);
		
		// Members
  private:
    FileDownloader * m_fileDownloader;
    std::map<QString, QString> m_currentVersionsMap;
    std::vector<QJsonObject> m_lastVersionInfos;

    bool m_standaloneVersion;

		// friend class declarations
    friend class UpdateManager;
	
};

// static members definition
#ifdef _VERSIONMANAGER_CPP_

#endif

#endif /* _VERSIONMANAGER_H_ */
