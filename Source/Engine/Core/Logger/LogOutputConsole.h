#pragma once

#include "LogOutput.h"

#define _LOGOUTPUTCONSOLE_API_

namespace WMVLog
{
	/// @brief Log output sink that writes messages to the standard console (stdout).
	class _LOGOUTPUTCONSOLE_API_ LogOutputConsole : public LogOutput
	{
	public:
		/// @brief Write a log message to stdout.
		void write(const std::string& message);
	};
}
