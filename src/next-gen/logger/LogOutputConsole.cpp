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
 * LogOutputConsole.cpp
 *
 *  Created on: 29 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _LOGOUTPUTCONSOLE_CPP_
#include "LogOutputConsole.h"
#undef _LOGOUTPUTCONSOLE_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>

// Qt 

// Externals

// Other libraries

// Current library


// Namespaces used
//--------------------------------------------------------------------
using namespace WMVLog;

// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
void LogOutputConsole::write(const QString & message)
{
  std::cout << message.toStdString() << std::endl;
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
