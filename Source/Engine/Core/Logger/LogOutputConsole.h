#pragma once

#include "LogOutput.h"

namespace WMVLog
{
	/// @brief Log output sink that writes messages to the standard console (stdout).
	class LogOutputConsole : public LogOutput
	{
	public:
		/// @brief Write a log message to stdout.
		void write(const std::string& message);
	};
}
