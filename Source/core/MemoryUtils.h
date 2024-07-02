#pragma once

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
