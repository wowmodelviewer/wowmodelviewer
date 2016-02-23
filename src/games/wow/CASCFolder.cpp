/*
 * CASCFolder.cpp
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#include "CASCFolder.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <utility>

#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "logger/Logger.h"

CASCFolder::CASCFolder()
 : hStorage(NULL),m_currentLocale(""), m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS)
{

}

void CASCFolder::init(const QString &folder)
{
  m_folder = folder;

  if(m_folder.endsWith("\\"))
    m_folder.remove(m_folder.size()-1,1);

  initLocales();
  initVersion();
}

void CASCFolder::initLocales()
{
  LOG_INFO << "Determining Locale for:" << m_folder;

  std::string buildinfofile = m_folder.toStdString()+"\\..\\.build.info";
  std::ifstream buildinfo(buildinfofile.c_str());

  if(!buildinfo.good())
  {
    LOG_ERROR << "Fail to open .build.info to determine locale";
    return;
  }

  std::string line, prevline;
  std::string buildKey = "";
  std::map<std::string, std::string> buildKeys;
  // clean up any previously found locale
  m_localesFound.clear();
  while(!buildinfo.eof())
  {
    buildinfo >> line;

    // find the build key for this locale
    if (!prevline.empty() && prevline.find("|0|") == std::string::npos &&
      prevline.find("|1|") == std::string::npos)
    {
      std::size_t buildKeyIdx;
      if ((buildKeyIdx = line.find("|0|")) != std::string::npos ||
        (buildKeyIdx = line.find("|1|")) != std::string::npos)
      {
        buildKey = line.substr(buildKeyIdx + 3, 32);
      }
    }

    if(!prevline.empty() && prevline.find("text?") == std::string::npos)
    {
      // only add this locale if we don't already have this combination of locale and build key
      if (line.find("text?") != std::string::npos && (buildKey.empty() ||
        buildKeys.find(prevline) == buildKeys.end() || buildKeys[prevline] != buildKey))
      {
        m_localesFound.push_back(prevline);
        buildKeys.insert(std::pair<std::string, std::string>(prevline, buildKey));
      }
    }
    prevline = line;
  }

  if(m_localesFound.empty())
  {
    LOG_ERROR << "Fail to grab locale in .build.info file";
  }
  else
  {
    for(size_t i = 0; i < m_localesFound.size(); i++)
    {
      LOG_INFO << "locale found : " << m_localesFound[i].c_str();
    }
  }
}


bool CASCFolder::setLocale(std::string locale)
{
  // init map based on CASCLib
  std::map<std::string,int> locales;
  locales["frFR"] = CASC_LOCALE_FRFR;
  locales["deDE"] = CASC_LOCALE_DEDE;
  locales["esES"] = CASC_LOCALE_ESES;
  locales["esMX"] = CASC_LOCALE_ESMX;
  locales["ptBR"] = CASC_LOCALE_PTBR;
  locales["itIT"] = CASC_LOCALE_ITIT;
  locales["ptPT"] = CASC_LOCALE_PTPT;
  locales["enGB"] = CASC_LOCALE_ENGB;
  locales["ruRU"] = CASC_LOCALE_RURU;
  locales["enUS"] = CASC_LOCALE_ENUS;
  locales["enCN"] = CASC_LOCALE_ENCN;
  locales["enTW"] = CASC_LOCALE_ENTW;
  locales["koKR"] = CASC_LOCALE_KOKR;
  locales["zhCN"] = CASC_LOCALE_ZHCN;
  locales["zhTW"] = CASC_LOCALE_ZHTW;

  if(!locale.empty())
  {
    std::map<std::string,int>::iterator it = locales.find(locale);

    if(it != locales.end())
    {
      HANDLE dummy;
      LOG_INFO << "Loading Game Folder:" << m_folder;
      // locale found => try to open it
      if(!CascOpenStorage(m_folder.toStdString().c_str(), it->second, &hStorage))
      {
        m_openError = GetLastError();
        LOG_ERROR << "Opening" << m_folder << "failed." << "Error" << m_openError;
        return false;
      }

      if(CascOpenFile(hStorage,"Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        m_currentCascLocale = it->second;
        m_currentLocale = locale;
        LOG_INFO << "Locale succesfully set:" << m_currentLocale.c_str();
      }
      else
      {
        LOG_ERROR << "Setting Locale" << locale.c_str() << "for folder" << m_folder << "failed";
        return false;
      }
    }
  }
  return true;
}


void CASCFolder::initVersion()
{
  QString buildinfofile = m_folder+"\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile;

  QFile file(buildinfofile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open .build.info to determine game version";
    return;
  }

  QTextStream in(&file);
  QString line;

  // read first line and grab VERSION index
  line = in.readLine();

  QStringList headers = line.split('|');
  int index = 0;
  for( ; index < headers.size() ; index++)
  {
    if(headers[index].contains("VERSION", Qt::CaseInsensitive))
      break;
  }

  // now that we have index, let's get actual values
  line = in.readLine();
  QStringList values = line.split('|');
  QRegularExpression re("^(\\d).(\\d).(\\d).(\\d+)");
  QRegularExpressionMatch result = re.match(values[index]);
  if(result.hasMatch())
    m_version = result.captured(1)+"."+result.captured(2)+"."+result.captured(3)+" ("+result.captured(4)+")";

  if(m_version.isEmpty())
    LOG_ERROR << "Fail to grab game version info in .build.info file";
  else
    LOG_INFO << "Version successfully found :" << m_version;

}

bool CASCFolder::fileExists(std::string file)
{
  //LOG_INFO << __FUNCTION__ << " " << file.c_str();
  if(!hStorage)
    return false;

  HANDLE dummy;

  if(CascOpenFile(hStorage,file.c_str(), m_currentCascLocale, 0, &dummy))
  {
   // LOG_INFO << "OK";
    CascCloseFile(dummy);
    return true;
  }

 // LOG_ERROR << "Opening" << file.c_str() << "failed." << "Error" << GetLastError();
  return false;
}

HANDLE CASCFolder::openFile(std::string file)
{
  HANDLE result;
  CascOpenFile(hStorage,file.c_str(), m_currentCascLocale, 0, &result);
  return result;
}

