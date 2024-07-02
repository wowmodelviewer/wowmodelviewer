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
