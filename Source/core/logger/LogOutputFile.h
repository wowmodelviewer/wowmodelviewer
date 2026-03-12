#pragma once

#include <fstream>
#include <mutex>
#include "LogOutput.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _LOGOUTPUTFILE_API_ __declspec(dllexport)
#    else
#        define _LOGOUTPUTFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _LOGOUTPUTFILE_API_
#endif

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
