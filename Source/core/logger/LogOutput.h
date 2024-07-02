#pragma once

#include <QString>
#include "../metaclasses/Component.h"

namespace WMVLog
{
	class LogOutput : public Component
	{
	public:
		virtual void write(const QString& message) = 0;
	};
}
