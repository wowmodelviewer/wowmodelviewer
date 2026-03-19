#pragma once

#include <string>
#include "Component.h"

namespace WMVLog
{
	class LogOutput : public Component
	{
	public:
		virtual void write(const std::string& message) = 0;
	};
}
