#pragma once

#include <string>

#define _MEMORYUTILS_API_

namespace core
{
	_MEMORYUTILS_API_ void __cdecl displayMemInfo(std::string message, bool displaySQLiteSize = false);

	_MEMORYUTILS_API_ int __cdecl getMemoryUsed();
}
