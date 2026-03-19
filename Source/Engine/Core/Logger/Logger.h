#pragma once

#include <sstream>
#include <string>

#include "Container.h"
#include "LogOutput.h"

#define _LOGGER_API_

#define LOGGER WMVLog::Logger::instance()
#define LOG_INFO LOGGER(WMVLog::Logger::INFO_LOG)
#define LOG_ERROR LOGGER(WMVLog::Logger::ERROR_LOG)
#define LOG_WARNING LOGGER(WMVLog::Logger::WARNING_LOG)
#define LOG_FATAL LOGGER(WMVLog::Logger::FATAL_LOG)

namespace WMVLog
{
	class Logger;

	class _LOGGER_API_ LogStream
	{
	public:
		LogStream(Logger& logger, int type);
		~LogStream();

		LogStream(const LogStream&) = delete;
		LogStream& operator=(const LogStream&) = delete;
		LogStream(LogStream&& other) noexcept;

		template <typename T>
		LogStream& operator<<(const T& value)
		{
			if (m_active)
				m_stream << value;
			return *this;
		}

		// Overload for std::wstring
		LogStream& operator<<(const std::wstring& value);

		// Overload for const wchar_t*
		LogStream& operator<<(const wchar_t* value);

	private:
		Logger* m_logger;
		int m_type;
		std::ostringstream m_stream;
		bool m_active;
	};

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

		void dispatchLog(int type, const std::string& msg);

		static std::string formatLog(int type, const std::string& msg);

		LogStream operator()(Logger::LogType type);

	private:
		Logger();
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		static Logger *m_instance;
	};
}
