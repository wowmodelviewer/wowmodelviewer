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
* MemoryUtils.cpp
*
*  Created on: 11 Aug 2017
*  Copyright: 2017 , WoW Model Viewer (http://wowmodelviewer.net)
*/

#include "MemoryUtils.h"

#include <windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib") // Added to support GetProcessMemoryInfo()

#include "sqlite3.h"

#include "logger\Logger.h"

void core::displayMemInfo(QString message, bool displaySQLiteSize)
{
  QString log = message + " Memory: " + QString::number(getMemoryUsed()) + " Mo";

  if (displaySQLiteSize)
  {
    log += " - SQLite: ";
    log += QString::number(sqlite3_memory_used() / (1024 * 1024));
    log += " Mo";
  }

  LOG_INFO << log;
}

int core::getMemoryUsed()
{
  PROCESS_MEMORY_COUNTERS memCounter;
  int result = -1;

  if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounter, sizeof(memCounter)))
    result = memCounter.WorkingSetSize / (1024 * 1024);

  return result;
}