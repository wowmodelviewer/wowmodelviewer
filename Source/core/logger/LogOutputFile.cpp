#include "LogOutputFile.h"
#include <QTextStream>

using namespace WMVLog;

LogOutputFile::LogOutputFile(std::string fileName) : m_logFile(fileName.c_str())
{
	m_logFile.open(QIODevice::WriteOnly | QIODevice::Text);
}

void LogOutputFile::write(const QString& message)
{
	QMutexLocker locker(&mutex);
	QTextStream out(&m_logFile);
	out << message << "\n";
}
