#pragma once

#include <QFile>
#include <qmutex.h>
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

		mutable QMutex mutex;
		QFile m_logFile;
	};
}
