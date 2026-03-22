#pragma once

#include <string>
#include "Component.h"

namespace WMVLog
{
	/// @brief Abstract base class for log output sinks (console, file, etc.).
	class LogOutput : public Component
	{
	public:
		/// @brief Write a formatted log message to this output.
		virtual void write(const std::string& message) = 0;
	};
}
