#include "LogStackWalker.h"
#include "logger/Logger.h"

void LogStackWalker::OnStackFrame(const wxStackFrame& frame)
{
	const int level = frame.GetLevel();
	const QString func = QString::fromWCharArray(frame.GetName().c_str());
	const QString filename = QString::fromWCharArray(frame.GetFileName().c_str());
	const int line = frame.GetLine();

	LOG_ERROR << level << func << "(" << filename << "-" << line << ")";
}
