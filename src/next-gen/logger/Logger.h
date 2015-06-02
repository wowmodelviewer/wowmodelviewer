/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * Logger.h
 *
 *  Created on: 29 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _LOGGER_H_
#define _LOGGER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <cstdio> // sprintf for wxLogMessage macro
#include <list>

// Qt
#include <QDebug>
#include <QtGlobal>
#include <QString>

class QMessageLogContext;

// Externals

// Other libraries
#include "core/GlobalSettings.h"
#include "core/Plugin.h"
#include "metaclasses/Container.h"

// Current library
#include "LogOutput.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
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

// ugly macro to overload wx function and use Logger stuff
#define wxLogMessage(...) \
  { \
    char buffer[200]; \
    std::sprintf (buffer, __VA_ARGS__); \
    LOG_INFO << buffer; \
  } \

namespace WMVLog
{
class _LOGGER_API_ Logger : public Container<LogOutput>
{
	public :
		// Constants / Enums
    enum LogType
    {
      INFO_LOG = 0,
      WARNING_LOG,
      ERROR_LOG,
      FATAL_LOG
    };

		// Constructors
	
		// Destructors
	
		// Methods
    static Logger & instance()
    {
      if(Logger::m_instance == 0)
        Logger::m_instance = new Logger();

      return *m_instance;
    }
		
    static void init();

    static void writeLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

    static QString formatLog(QtMsgType type, const QMessageLogContext &context, const QString &msg);

    QDebug operator()(Logger::LogType type);

		// Members
		
	protected :
		// Constants / Enums
	
		// Constructors
	
		// Destructors
	
		// Methods
		
		// Members
		
	private :
		// Constants / Enums
	
		// Constructors
    // prevent unwanted constructions
    Logger();
    Logger(Logger &);

		// Destructors
	
		// Methods
		
		// Members
    static Logger * m_instance;

		// friend class declarations
	
};

// static members definition
#ifdef _LOGGER_CPP_

#endif

} // WMVLog
#endif /* _LOGGER_H_ */
