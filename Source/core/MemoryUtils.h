#pragma once

#include <QString>

#define _MEMORYUTILS_API_

namespace core
{
	_MEMORYUTILS_API_ void __cdecl displayMemInfo(QString message, bool displaySQLiteSize = false);

	_MEMORYUTILS_API_ int __cdecl getMemoryUsed();
}
