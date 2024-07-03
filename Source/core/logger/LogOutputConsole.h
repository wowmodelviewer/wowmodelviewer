#pragma once

#include "LogOutput.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _LOGOUTPUTCONSOLE_API_ __declspec(dllexport)
#    else
#        define _LOGOUTPUTCONSOLE_API_ __declspec(dllimport)
#    endif
#else
#    define _LOGOUTPUTCONSOLE_API_
#endif

namespace WMVLog
{
	class _LOGOUTPUTCONSOLE_API_ LogOutputConsole : public LogOutput
	{
	public:
		void write(const QString& message);
	};
}
