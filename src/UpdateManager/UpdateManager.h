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
 * UpdateManager.h
 *
 *  Created on: 31 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _UPDATEMANAGER_H_
#define _UPDATEMANAGER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <map>

// Qt
#include <QLabel>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QTableWidget>

// Externals

// Other libraries
class VersionManager;

// Current library
class FileDownloader;

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class UpdateManager : public QMainWindow
{
    Q_OBJECT
	public :
		// Constants / Enums
		
		// Constructors
    UpdateManager();
	
		// Destructors
    ~UpdateManager();
	
		// Methods

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
    void updateTable();
    void resizeTable();
		
  private slots:
    void fileDownloaded(QString &);
    void updateLastVersionInfo();
    void cellClicked(int,int);


  private:
    // Members
    QTableWidget * m_table;
    FileDownloader * m_fileDownloader;
    VersionManager * m_versionManager;
		
		// friend class declarations
	
};

// static members definition
#ifdef _UPDATEMANAGER_CPP_

#endif

#endif /* _UPDATEMANAGER_H_ */
