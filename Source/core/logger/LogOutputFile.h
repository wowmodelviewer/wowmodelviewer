#pragma once

#include <fstream>
#include <mutex>
#include "LogOutput.h"

#define _LOGOUTPUTFILE_API_

namespace WMVLog
{
	class _LOGOUTPUTFILE_API_ LogOutputFile : public LogOutput
	{
	public:
		LogOutputFile(std::string fileName);
		void write(const QString& message);

	private:
		LogOutputFile();
		LogOutputFile(const LogOutputFile&);
		LogOutputFile& operator=(const LogOutputFile&) = delete;

		mutable std::mutex m_mutex;
		std::ofstream m_logFile;
	};
}
