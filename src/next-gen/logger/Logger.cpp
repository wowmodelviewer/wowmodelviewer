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
 * Logger.cpp
 *
 *  Created on: 29 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _LOGGER_CPP_
#include "Logger.h"
#undef _LOGGER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>

// Qt 
#include <QDateTime>

// Externals

// Other libraries
#include "metaclasses/Iterator.h"

// Current library


// Namespaces used
//--------------------------------------------------------------------
using namespace WMVLog;

// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------
Logger::Logger()
{
  Logger::init();
}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
void Logger::init()
{
  qInstallMessageHandler(Logger::writeLog);
}


void Logger::writeLog(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  QString message = Logger::formatLog(type, context, msg);
  Iterator<LogOutput> outputIt(LOGGER);
  for(outputIt.begin(); !outputIt.ended(); outputIt++)
    (*outputIt)->write(message);
}


QString Logger::formatLog(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  QString msgType;
  switch(type)
  {
    case QtDebugMsg:
      msgType = "INFO";
      break;
    case QtWarningMsg:
      msgType = "WARN";
      break;
    case QtCriticalMsg:
      msgType = "ERROR";
      break;
    case QtFatalMsg:
      msgType = "FATAL";
      break;
  }

  return msgType + "\t| " +
         QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") + "\t| " +
         msg;
}

QDebug Logger::operator()(Logger::LogType type)
{
  switch(type)
  {
    case INFO_LOG:
      return QDebug(QtDebugMsg);
    case WARNING_LOG:
      return QDebug(QtWarningMsg);
    case ERROR_LOG:
      return QDebug(QtCriticalMsg);
    case FATAL_LOG:
      return QDebug(QtFatalMsg);
  }
  return QDebug(QtDebugMsg);
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
