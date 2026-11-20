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

#if defined(_WIN32)
#  include <windows.h>
#  include <Psapi.h>
#  pragma comment(lib, "psapi.lib") // Added to support GetProcessMemoryInfo()
#elif defined(__APPLE__)
#  include <mach/mach.h>
#else
#  include <cstdio>
#  include <unistd.h>
#endif

#include "sqlite3.h"

#include "logger/Logger.h"

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
  int result = -1;
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS memCounter;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounter, sizeof(memCounter)))
    result = memCounter.WorkingSetSize / (1024 * 1024);
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
    result = (int)(info.resident_size / (1024 * 1024));
#else
  // statm's second field is the resident set, in pages.
  if (FILE * f = std::fopen("/proc/self/statm", "r"))
  {
    long pages = 0, resident = 0;
    if (std::fscanf(f, "%ld %ld", &pages, &resident) == 2)
      result = (int)((resident * sysconf(_SC_PAGESIZE)) / (1024 * 1024));
    std::fclose(f);
  }
#endif
  return result;
}