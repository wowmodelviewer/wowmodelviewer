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

#include "CASCFile.h"

#include "logger/Logger.h"

CASCFolder::CASCFolder()
 : hStorage(NULL),m_currentLocale(""), m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS)
{

}

bool CASCFolder::init(const std::string &folder)
{
  m_folder = folder;

  if(m_folder.find_last_of("\\") == m_folder.length()-1)
    m_folder = m_folder.substr (0,m_folder.length()-1);

  LOG_INFO << "Loading Game Folder:" << m_folder.c_str();

  // Open the storage directory
  if(!CascOpenStorage(m_folder.c_str(), 0, &hStorage))
  {
    m_openError = GetLastError();
    LOG_ERROR << "Opening" << m_folder.c_str() << "failed." << "Error" << m_openError;
    return false;
  }
  else
  {
    LOG_INFO << "Succesfully opened. ";
    initLocales();
    initVersion();
    return true;
  }
}

void CASCFolder::initLocales()
{
  LOG_INFO << "Determining Locale for:" << m_folder.c_str();

  std::string buildinfofile = m_folder+"\\..\\.build.info";
  std::ifstream buildinfo(buildinfofile.c_str());

  if(!buildinfo.good())
  {
    LOG_ERROR << "Fail to open .build.info to determine locale";
    return;
  }

  std::string line, prevline;

  while(!buildinfo.eof())
  {
    buildinfo >> line;
    if(!prevline.empty() && prevline.find("text?") == std::string::npos)
    {
      if(line.find("text?") != std::string::npos)
        m_localesFound.push_back(prevline);
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


void CASCFolder::setLocale(std::string locale)
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
      // locale found => try to open it
      if(CascOpenFile(hStorage,"Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        m_currentCascLocale = it->second;
        m_currentLocale = locale;
        LOG_INFO << "Locale succesfully set:" << m_currentLocale.c_str();
      }
      else
      {
        LOG_ERROR << "Setting Locale" << locale.c_str() << "for folder" << m_folder.c_str() << "failed";
      }
    }
  }
}


void CASCFolder::initVersion()
{
  std::string buildinfofile = m_folder+"\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile.c_str();
  std::ifstream buildinfo(buildinfofile.c_str());

  if(!buildinfo.good())
  {
    LOG_ERROR << "Fail to open .build.info to determine game version";
    return;
  }

  std::string line;

  while(!buildinfo.eof())
  {
    buildinfo >> line;
  }

  if(line.find_last_of("|") != std::string::npos)
  {
    m_version = line.substr (line.find_last_of("|")+1, line.length()-1);
    size_t lastPointPos = m_version.find_last_of(".");
    std::string version =  m_version.substr (0, lastPointPos);
    std::string build = m_version.substr (lastPointPos +1, m_version.length()-1);
    m_version = version + " (" + build + ")";
    LOG_INFO << "Version successfully found :" << m_version.c_str();
  }
  else
  {
    LOG_ERROR << "Fail to grab game version info in .build.info file";
  }
}

void CASCFolder::initFileList(std::set<FileTreeItem> &dest, bool filterfunc(wxString)/* = CASCFolder::defaultFilterFunc*/)
{
  std::ifstream listfile("listfile.txt");

  if(!listfile.good())
  {
    LOG_ERROR << "Fail to open listfile.txt.";
    return;
  }

  std::string lineStd;
  while(!listfile.eof())
  {
    listfile >> lineStd;
    FileTreeItem tmp;

    wxString line(lineStd.c_str(), wxConvUTF8);

    if (filterfunc(line))
    {
      tmp.fileName = line;
      line.MakeLower();
      line[0] = toupper(line.GetChar(0));
      int ret = line.Find('\\');
      if (ret>-1)
        line[ret+1] = toupper(line.GetChar(ret+1));

      tmp.displayName = line;
      tmp.color = 0;
      dest.insert(tmp);
    }
  }
}

std::string CASCFolder::getFullPathForFile(std::string file)
{
  std::ifstream listfile("listfile.txt");

  if(!listfile.good())
  {
    LOG_ERROR << "Fail to open listfile.txt when searching fullpath for" << file.c_str();
    return "";
  }

  std::transform(file.begin(), file.end(), file.begin(), ::tolower);
  //LOG_INFO << "Looking for full path for file" << file.c_str();
  std::string line;
  while(!listfile.eof())
  {
    listfile >> line;
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);
    if(line.find(file) != std::string::npos)
    {
      //LOG_INFO << "Found:" << line.c_str();
      return line;
    }
  }
  LOG_ERROR << __FUNCTION__ << file.c_str() << "Not found";
  return "";
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
