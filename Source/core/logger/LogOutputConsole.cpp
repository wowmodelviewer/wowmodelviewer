#include "LogOutputConsole.h"
#include <iostream>

using namespace WMVLog;

void LogOutputConsole::write(const QString& message)
{
	std::cout << message.toStdString() << std::endl;
}
