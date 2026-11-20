/*
 * TString.h
 *
 * CascLib and StormLib take TCHAR paths: wide strings on Windows (they are built UNICODE there), UTF-8 file-system
 * strings elsewhere.
 */

#ifndef _TSTRING_H_
#define _TSTRING_H_

#include <string>

#include <QFile>
#include <QString>

namespace wmv
{
#ifdef _WIN32
  typedef std::wstring tstring;
  inline tstring toTString(const QString & s) { return s.toStdWString(); }
#else
  typedef std::string tstring;
  inline tstring toTString(const QString & s) { return QFile::encodeName(s).toStdString(); }
#endif
}

#endif
