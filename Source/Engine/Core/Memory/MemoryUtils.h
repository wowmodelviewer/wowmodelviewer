#pragma once

#include <string>

namespace core
{
	/// @brief Log current process memory usage to the Logger.
	/// @param message  Descriptive label for the log entry.
	/// @param displaySQLiteSize  If true, also log SQLite memory usage.
	void __cdecl displayMemInfo(std::string message, bool displaySQLiteSize = false);

	/// @brief Return the current process memory usage in kilobytes.
	int __cdecl getMemoryUsed();
}
