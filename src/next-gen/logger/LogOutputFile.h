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
 * LogOutputFile.h
 *
 *  Created on: 29 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _LOGOUTPUTFILE_H_
#define _LOGOUTPUTFILE_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QFile>
#include <QString>
#include <QTextStream>

// Externals

// Other libraries

// Current library
#include "LogOutput.h"


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
namespace WMVLog
{
class LogOutputFile : public LogOutput
{
	public :
		// Constants / Enums
		
		// Constructors
    LogOutputFile(std::string fileName);
	
		// Destructors
	
		// Methods
    void write(const QString & message);
		
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
    LogOutputFile();
    LogOutputFile(const LogOutputFile&);
	
		// Destructors
	
		// Methods
		
		// Members
    QFile m_logFile;
		
		// friend class declarations
	
};

// static members definition
#ifdef _LOGOUTPUTFILE_CPP_

#endif

}
#endif /* _LOGOUTPUTFILE_H_ */
