#include "LogStackWalker.h"
#include "logger/Logger.h"

#include <string>

void LogStackWalker::OnStackFrame(const wxStackFrame& frame)
{
	const int level = frame.GetLevel();
	const std::wstring func = frame.GetName().ToStdWstring();
	const std::wstring filename = frame.GetFileName().ToStdWstring();
	const int line = frame.GetLine();

	LOG_ERROR << level << func << L"(" << filename << L"-" << line << L")";
}
