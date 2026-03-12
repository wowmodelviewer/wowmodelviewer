#include "LogOutputFile.h"

using namespace WMVLog;

LogOutputFile::LogOutputFile(std::string fileName)
{
	m_logFile.open(fileName, std::ios::out | std::ios::trunc);
}

void LogOutputFile::write(const QString& message)
{
	std::lock_guard<std::mutex> locker(m_mutex);
	m_logFile << message.toStdString() << "\n";
	m_logFile.flush();
}
