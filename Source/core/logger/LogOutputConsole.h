#pragma once

#include "LogOutput.h"

#define _LOGOUTPUTCONSOLE_API_

namespace WMVLog
{
	class _LOGOUTPUTCONSOLE_API_ LogOutputConsole : public LogOutput
	{
	public:
		void write(const QString& message);
	};
}
