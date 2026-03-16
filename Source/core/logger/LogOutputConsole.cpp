#include "LogOutputConsole.h"
#include <iostream>

using namespace WMVLog;

void LogOutputConsole::write(const std::string& message)
{
	std::cout << message << std::endl;
}
