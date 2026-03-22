#pragma once

#include <fstream>
#include <mutex>
#include "LogOutput.h"

#define _LOGOUTPUTFILE_API_

namespace WMVLog
{
	/// @brief Thread-safe log output sink that writes messages to a file on disk.
	class _LOGOUTPUTFILE_API_ LogOutputFile : public LogOutput
	{
	public:
		/// @brief Construct a file output sink that writes to the given path.
		LogOutputFile(std::string fileName);

		/// @brief Write a log message to the file (thread-safe).
		void write(const std::string& message);

	private:
		LogOutputFile();
		LogOutputFile(const LogOutputFile&);
		LogOutputFile& operator=(const LogOutputFile&) = delete;

		mutable std::mutex m_mutex;
		std::ofstream m_logFile;
	};
}
