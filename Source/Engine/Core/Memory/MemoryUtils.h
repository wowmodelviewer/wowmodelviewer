#pragma once

#include <string>

#define _MEMORYUTILS_API_

namespace core
{
	/// @brief Log current process memory usage to the Logger.
	/// @param message  Descriptive label for the log entry.
	/// @param displaySQLiteSize  If true, also log SQLite memory usage.
	_MEMORYUTILS_API_ void __cdecl displayMemInfo(std::string message, bool displaySQLiteSize = false);

	/// @brief Return the current process memory usage in kilobytes.
	_MEMORYUTILS_API_ int __cdecl getMemoryUsed();
}
