#include "Logger.h"

#ifdef min
  #undef min
#endif
#include <QDateTime>

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
	QString message = Logger::formatLog(type, context, msg);
	for (Logger::iterator it = LOGGER.begin();
	     it != LOGGER.end();
	     ++it)
		(*it)->write(message);
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
	}

	return msgType + "\t| " +
		QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") + "\t| " +
		msg;
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
