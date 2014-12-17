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

#include "CASCFile.h"

#include "logger/Logger.h"

CASCFolder::CASCFolder()
 : hStorage(NULL),m_currentLocale(""), m_currentCascLocale(CASC_LOCALE_NONE), m_folder("")
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
    LOG_ERROR << "Opening" << m_folder.c_str() << "failed." << "Error" << GetLastError();
    return false;
  }
  else
  {
    LOG_INFO << "Succesfully opened. ";
    initLocale();
    initVersion();
    return true;
  }
}

void CASCFolder::initLocale()
{
  LOG_INFO << "Determining Locale for:" << m_folder.c_str();
  // init map based on CASCLib
  std::list<std::pair<int,std::string> > locales;
  locales.push_back(std::make_pair(CASC_LOCALE_FRFR,"frFR"));
  locales.push_back(std::make_pair(CASC_LOCALE_DEDE,"deDE"));
  locales.push_back(std::make_pair(CASC_LOCALE_ESES,"esES"));
  locales.push_back(std::make_pair(CASC_LOCALE_ESMX,"esMX"));
  locales.push_back(std::make_pair(CASC_LOCALE_PTBR,"ptBR"));
  locales.push_back(std::make_pair(CASC_LOCALE_ITIT,"itIT"));
  locales.push_back(std::make_pair(CASC_LOCALE_PTPT,"ptPT"));
  locales.push_back(std::make_pair(CASC_LOCALE_ENGB,"enGB"));
  locales.push_back(std::make_pair(CASC_LOCALE_RURU,"ruRU"));
  locales.push_back(std::make_pair(CASC_LOCALE_ENUS,"enUS"));
  locales.push_back(std::make_pair(CASC_LOCALE_ENCN,"enCN"));
  locales.push_back(std::make_pair(CASC_LOCALE_ENTW,"enTW"));
  locales.push_back(std::make_pair(CASC_LOCALE_KOKR,"koKR"));
  locales.push_back(std::make_pair(CASC_LOCALE_ZHCN,"zhCN"));
  locales.push_back(std::make_pair(CASC_LOCALE_ZHTW,"zhTW"));



  HANDLE dummy;

  //search for current locale for this folder
  // => look at Interface\FrameXML\Localization.lua file
  std::list<std::pair<int,std::string> >::iterator it = locales.begin();
  for( ; it != locales.end() ; ++it)
  {
    if(CascOpenFile(hStorage,"Interface\\FrameXML\\Localization.lua", it->first, 0, &dummy))
    {
      CascCloseFile(dummy);
      break;
    }
  }

  if(it != locales.end())
  {
    m_currentLocale = it->second;
    m_currentCascLocale = it->first;
    LOG_INFO << "Locale succesfully found:" << m_currentLocale.c_str();
  }
  else
  {
    LOG_ERROR << "Determining Locale for folder" << m_folder.c_str() << "failed.";
  }

}


void CASCFolder::initVersion()
{
  std::string buildinfofile = m_folder+"\\..\\.build.info";
  std::cout << "buildinfofile = " << buildinfofile << std::endl;
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
    m_version = line.substr (line.find_last_of("|")+1, line.length()-1);
  else
    LOG_ERROR << "Fail to grab game version info in .build.info file";
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
