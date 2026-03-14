#pragma once

#include <QDebug>
#include <QtGlobal>
#include <QString>

class QMessageLogContext;

#include "../metaclasses/Container.h"
#include "LogOutput.h"

#define _LOGGER_API_

#define LOGGER WMVLog::Logger::instance()
#define LOG_INFO LOGGER(WMVLog::Logger::INFO_LOG)
#define LOG_ERROR LOGGER(WMVLog::Logger::ERROR_LOG)
#define LOG_WARNING LOGGER(WMVLog::Logger::WARNING_LOG)
#define LOG_FATAL LOGGER(WMVLog::Logger::FATAL_LOG)

namespace WMVLog
{
	class _LOGGER_API_ Logger : public Container<LogOutput>
	{
	public:
		enum LogType
		{
			INFO_LOG = 0,
			WARNING_LOG,
			ERROR_LOG,
			FATAL_LOG
		};

		static Logger &instance()
		{
			if (Logger::m_instance == nullptr)
				Logger::m_instance = new Logger();

			return *m_instance;
		}

		static void init();

		static void writeLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

		static QString formatLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

		QDebug operator()(Logger::LogType type);

	private:
		Logger();
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		static Logger *m_instance;
	};
}
