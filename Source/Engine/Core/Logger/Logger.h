#pragma once

#include <sstream>
#include <string>

#include "Container.h"
#include "LogOutput.h"

#define LOGGER WMVLog::Logger::instance()
#define LOG_INFO LOGGER(WMVLog::Logger::INFO_LOG)
#define LOG_ERROR LOGGER(WMVLog::Logger::ERROR_LOG)
#define LOG_WARNING LOGGER(WMVLog::Logger::WARNING_LOG)
#define LOG_FATAL LOGGER(WMVLog::Logger::FATAL_LOG)

namespace WMVLog
{
	class Logger;

	/// @brief RAII stream object that collects log output and dispatches it on destruction.
	///
	/// Created by Logger::operator() — use the LOG_INFO / LOG_ERROR / LOG_WARNING macros.
	class LogStream
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

	/// @brief Singleton logging facility that dispatches formatted messages to LogOutput sinks.
	///
	/// Access via the LOGGER macro or the convenience macros LOG_INFO, LOG_ERROR,
	/// LOG_WARNING, and LOG_FATAL.  Outputs are added as children (see Container).
	class Logger : public Container<LogOutput>
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

		/// @brief Initialise the logging subsystem (creates console and file outputs).
		static void init();

		/// @brief Send a pre-formatted message to all registered outputs.
		void dispatchLog(int type, const std::string& msg);

		/// @brief Format a log message with a severity prefix and timestamp.
		static std::string formatLog(int type, const std::string& msg);

		/// @brief Create a LogStream for the given severity level.
		LogStream operator()(Logger::LogType type);

	private:
		Logger();
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		static Logger *m_instance;
	};
}
