#include "MemoryUtils.h"
#include <windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib") // Added to support GetProcessMemoryInfo()
#include "logger\Logger.h"

void core::displayMemInfo(std::string message, bool displaySQLiteSize)
{
	std::string log = message + " Memory: " + std::to_string(getMemoryUsed()) + " Mo";
	LOG_INFO << log.c_str();
}

int core::getMemoryUsed()
{
	PROCESS_MEMORY_COUNTERS memCounter;
	int result = -1;

	if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounter, sizeof(memCounter)))
		result = memCounter.WorkingSetSize / (1024 * 1024);

	return result;
}
