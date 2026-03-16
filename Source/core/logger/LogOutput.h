#pragma once

#include <string>
#include "../metaclasses/Component.h"

namespace WMVLog
{
	class LogOutput : public Component
	{
	public:
		virtual void write(const std::string& message) = 0;
	};
}
