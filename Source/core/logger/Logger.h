#pragma once

#include <QDebug>
#include <QtGlobal>
#include <QString>

class QMessageLogContext;

#include "../metaclasses/Container.h"
#include "LogOutput.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _LOGGER_API_ __declspec(dllexport)
#    else
#        define _LOGGER_API_ __declspec(dllimport)
#    endif
#else
#    define _LOGGER_API_
#endif

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
			if (Logger::m_instance == 0)
				Logger::m_instance = new Logger();

			return *m_instance;
		}

		static void init();

		static void writeLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

		static QString formatLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

		QDebug operator()(Logger::LogType type);

	private:
		Logger();
		Logger(Logger &);

		static Logger *m_instance;
	};
}
