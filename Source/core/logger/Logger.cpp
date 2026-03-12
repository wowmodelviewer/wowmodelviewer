#include "Logger.h"

#ifdef min
  #undef min
#endif
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace WMVLog;

Logger* Logger::m_instance = nullptr;

Logger::Logger()
{
	Logger::init();
}

void Logger::init()
{
	qInstallMessageHandler(Logger::writeLog);
}

void Logger::writeLog(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
	const QString message = Logger::formatLog(type, context, msg);
	for (const auto it : LOGGER)
		it->write(message);
}

QString Logger::formatLog(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
	QString msgType;
	switch (type)
	{
	case QtDebugMsg:
		msgType = "INFO";
		break;
	case QtWarningMsg:
		msgType = "WARN";
		break;
	case QtCriticalMsg:
		msgType = "ERROR";
		break;
	case QtFatalMsg:
		msgType = "FATAL";
		break;
	default: ;
	}

	const auto now = std::chrono::system_clock::now();
	const auto t = std::chrono::system_clock::to_time_t(now);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	std::tm tm_info{};
	localtime_s(&tm_info, &t);
	std::ostringstream ts;
	ts << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S")
	   << "." << std::setfill('0') << std::setw(3) << ms.count();

	return msgType + "\t| " + QString::fromStdString(ts.str()) + "\t| " + msg;
}

QDebug Logger::operator()(Logger::LogType type)
{
	switch (type)
	{
	case INFO_LOG:
		return QDebug(QtDebugMsg);
	case WARNING_LOG:
		return QDebug(QtWarningMsg);
	case ERROR_LOG:
		return QDebug(QtCriticalMsg);
	case FATAL_LOG:
		return QDebug(QtFatalMsg);
	}
	return QDebug(QtDebugMsg);
}
