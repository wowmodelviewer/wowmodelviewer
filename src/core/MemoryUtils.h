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
* MemoryUtils.h
*
*  Created on: 11 Aug 2017
*  Copyright: 2017 , WoW Model Viewer (http://wowmodelviewer.net)
*/

#ifndef _MEMORYUTILS_H_
#define _MEMORYUTILS_H_

#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _MEMORYUTILS_API_ __declspec(dllexport)
#    else
#        define _MEMORYUTILS_API_ __declspec(dllimport)
#    endif
#else
#    define _MEMORYUTILS_API_
#endif

namespace core
{
  _MEMORYUTILS_API_ void __cdecl displayMemInfo(QString message, bool displaySQLiteSize = false);

  _MEMORYUTILS_API_ int __cdecl getMemoryUsed();
}


#endif _MEMORYUTILS_H_