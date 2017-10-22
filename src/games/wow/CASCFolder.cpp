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
 : hStorage(NULL), m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS)
{

}

void CASCFolder::init(const QString &folder)
{
  m_folder = folder;

  if(m_folder.endsWith("\\"))
    m_folder.remove(m_folder.size()-1,1);

  initBuildInfo();
}

bool CASCFolder::setConfig(core::GameConfig config)
{
  m_currentConfig = config;

  // init map based on CASCLib
  std::map<QString, int> locales;
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

  // set locale
  if (!m_currentConfig.locale.isEmpty())
  {
    auto it = locales.find(m_currentConfig.locale);

    if (it != locales.end())
    {
      HANDLE dummy;
      LOG_INFO << "Loading Game Folder:" << m_folder;
      // locale found => try to open it
      if (!CascOpenStorage(m_folder.toStdString().c_str(), it->second, &hStorage))
      {
        m_openError = GetLastError();
        LOG_ERROR << "Opening" << m_folder << "failed." << "Error" << m_openError;
        return false;
      }

      if (CascOpenFile(hStorage, "Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        m_currentCascLocale = it->second;
        LOG_INFO << "Locale succesfully set:" << m_currentConfig.locale;
      }
      else
      {
        LOG_ERROR << "Setting Locale" << m_currentConfig.locale << "for folder" << m_folder << "failed";
        return false;
      }
    }
  }

  return true;
}

void CASCFolder::initBuildInfo()
{
  QString buildinfofile = m_folder + "\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile;

  QFile file(buildinfofile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open .build.info to grab game config info";
    return;
  }

  QTextStream in(&file);
  QString line;

  // read first line and grab VERSION index
  line = in.readLine();

  QStringList headers = line.split('|');
  int activeIndex = 0;
  int versionIndex = 0;
  int tagIndex = 0;
  for (int index = 0; index < headers.size(); index++)
  {
    if (headers[index].contains("Active", Qt::CaseInsensitive))
      activeIndex = index;
    else if (headers[index].contains("Version", Qt::CaseInsensitive))
      versionIndex = index;
    else if (headers[index].contains("Tags", Qt::CaseInsensitive))
      tagIndex = index;
  }

  // now loop across file lines with actual values
  while (in.readLineInto(&line))
  {
    QString version;
    QStringList values = line.split('|');

    // if inactive config, skip it
    if (values[activeIndex] == "0")
      continue;

    // grab version for this line
    QRegularExpression re("^(\\d).(\\d).(\\d).(\\d+)");
    QRegularExpressionMatch result = re.match(values[versionIndex]);
    if (result.hasMatch())
      version = result.captured(1) + "." + result.captured(2) + "." + result.captured(3) + " (" + result.captured(4) + ")";

    // grab locale(s) for this line
    values = values[tagIndex].split(':');
    for (int i = 0; i < values.size(); i++)
    {
      if (values[i].contains("text?"))
      {
        QStringList tags = values[i].split(" ");
        core::GameConfig config;
        config.locale = tags[tags.size() - 2];
        config.version = version;
        m_configs.push_back(config);
      }
    }
  }

  for (auto it : m_configs)
    LOG_INFO << "config" << it.locale << it.version;
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

bool CASCFolder::openFile(std::string file, HANDLE * result)
{
 
  return CascOpenFile(hStorage,file.c_str(), m_currentCascLocale, 0, result);
}

bool CASCFolder::closeFile(HANDLE file)
{
  return CascCloseFile(file);
}

int CASCFolder::fileDataId(std::string & filename)
{
  return CascGetFileId(hStorage, filename.c_str());
}

