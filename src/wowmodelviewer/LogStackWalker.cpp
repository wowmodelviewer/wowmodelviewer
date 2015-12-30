/*
 * LogStackWalker.cpp
 *
 *  Created on: 30 dec. 2015
 *      Author: Jeromnimo
 */

#include "LogStackWalker.h"

#include "logger/Logger.h"

void LogStackWalker::OnStackFrame(const wxStackFrame& frame)
{
  int level = frame.GetLevel();
  QString func = frame.GetName().c_str();
  QString filename = frame.GetFileName().c_str();
  int line = frame.GetLine();

  LOG_ERROR << level << func << "(" << filename << "-" << line << ")";
}
