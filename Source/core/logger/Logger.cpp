#include "Logger.h"

#include <windows.h>
#include <QString>
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
}

void Logger::dispatchLog(int type, const std::string& msg)
{
	const std::string message = Logger::formatLog(type, msg);
	for (const auto it : *this)
		it->write(message);
}

std::string Logger::formatLog(int type, const std::string& msg)
{
	std::string msgType;
	switch (type)
	{
	case INFO_LOG:
		msgType = "INFO";
		break;
	case WARNING_LOG:
		msgType = "WARN";
		break;
	case ERROR_LOG:
		msgType = "ERROR";
		break;
	case FATAL_LOG:
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

	return msgType + "\t| " + ts.str() + "\t| " + msg;
}

LogStream Logger::operator()(Logger::LogType type)
{
	return LogStream(*this, static_cast<int>(type));
}

// --- LogStream implementation ---

LogStream::LogStream(Logger& logger, int type)
	: m_logger(&logger), m_type(type), m_active(true)
{
}

LogStream::LogStream(LogStream&& other) noexcept
	: m_logger(other.m_logger), m_type(other.m_type),
	  m_stream(std::move(other.m_stream)), m_active(other.m_active)
{
	other.m_active = false;
}

LogStream::~LogStream()
{
	if (m_active && m_logger)
	{
		m_logger->dispatchLog(m_type, m_stream.str());
	}
}

LogStream& LogStream::operator<<(const std::wstring& value)
{
	if (m_active)
	{
		// Convert wstring to UTF-8 string
		if (!value.empty())
		{
			int size_needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			std::string utf8(size_needed, 0);
			WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &utf8[0], size_needed, nullptr, nullptr);
			m_stream << utf8;
		}
	}
	return *this;
}

LogStream& LogStream::operator<<(const wchar_t* value)
{
	if (m_active && value)
	{
		return *this << std::wstring(value);
	}
	return *this;
}

LogStream& LogStream::operator<<(const QString& value)
{
	if (m_active)
	{
		m_stream << value.toStdString();
	}
	return *this;
}
