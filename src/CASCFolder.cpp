/*
 * CASCFolder.cpp
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#include "CASCFolder.h"

#include <fstream>
#include <iostream>

#include "CASCFile.h"

#include "logger/Logger.h"


CASCFolder::CASCFolder()
 : hStorage(NULL),m_currentLocale(""), m_currentCascLocale(CASC_LOCALE_NONE), m_folder("")
{

}




void CASCFolder::init(const std::string &folder)
{
  m_folder = folder;

  if(m_folder.find_last_of("\\") == m_folder.length()-1)
    m_folder = m_folder.substr (0,m_folder.length()-1);

  LOG_INFO << "Loading Game Folder:" << m_folder.c_str();

  // Open the storage directory
  int opened = CascOpenStorage(m_folder.c_str(), 0, &hStorage);
  if(opened != 0)
  {
    LOG_ERROR << "Opening" << m_folder.c_str() << "failed." << "Error" << opened;
  }
  else
  {
    LOG_INFO << "Succesfully opened. ";
    initLocale();
  }
}

void CASCFolder::initLocale()
{
  LOG_INFO << "Determining Locale for:" << m_folder.c_str();
  // init map based on CASCLib
  std::map<int,std::string> localesMap;
  localesMap[CASC_LOCALE_ENUS]="enUS";
  localesMap[CASC_LOCALE_KOKR]="koKR";
  localesMap[CASC_LOCALE_FRFR]="frFR";
  localesMap[CASC_LOCALE_DEDE]="deDE";
  localesMap[CASC_LOCALE_ZHCN]="zhCN";
  localesMap[CASC_LOCALE_ESES]="esES";
  localesMap[CASC_LOCALE_ZHTW]="zhTW";
  localesMap[CASC_LOCALE_ENGB]="enGB";
  localesMap[CASC_LOCALE_ENCN]="enCN";
  localesMap[CASC_LOCALE_ENTW]="enTW";
  localesMap[CASC_LOCALE_ESMX]="esMX";
  localesMap[CASC_LOCALE_RURU]="ruRU";
  localesMap[CASC_LOCALE_PTBR]="ptBR";
  localesMap[CASC_LOCALE_ITIT]="itIT";
  localesMap[CASC_LOCALE_PTPT]="ptPT";

  HANDLE dummy;

  //search for current locale for this folder
  // => look at Interface\FrameXML\Localization.lua file
  std::map<int,std::string>::iterator it = localesMap.begin();
  for( ; it != localesMap.end() ; ++it)
  {
    if(CascOpenFile(hStorage,"Interface\\FrameXML\\Localization.lua", it->first, 0, &dummy))
    {
      CascCloseFile(dummy);
      break;
    }
  }

  if(it != localesMap.end())
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
