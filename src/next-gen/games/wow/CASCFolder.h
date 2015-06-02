/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#ifndef _CASCFOLDER_H_
#define _CASCFOLDER_H_

#include "FileTreeItem.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/string.h>
#include <wx/wx.h>

#include "casclib/src/CascLib.h"

#include <iostream>

#define CASCFOLDER CASCFolder::instance()

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _CASCFOLDER_API_ __declspec(dllexport)
#    else
#        define _CASCFOLDER_API_ __declspec(dllimport)
#    endif
#else
#    define _CASCFOLDER_API_
#endif

class _CASCFOLDER_API_ CASCFolder
{
  public:

    static CASCFolder & instance()
    {
      if(CASCFolder::m_instance == 0)
        CASCFolder::m_instance = new CASCFolder();
      return *m_instance;
    }

    void init(const std::string & path);

    HANDLE hStorage;

    std::string locale() { return m_currentLocale; }
    std::vector<std::string> localesFound() { return m_localesFound; }
    std::string version() { return m_version; }
    int CASCLocale() { return m_currentCascLocale; }
    std::string folder() { return m_folder; }
    int lastError() { return m_openError; }

    // return full path for a given file ie :
    // HumanMale.m2 => Character\Human\male\humanmale.m2
    std::string getFullPathForFile(std::string file);
    std::string getFullPathForFile(wxString file) { return getFullPathForFile(std::string(file.mb_str())); }

    bool fileExists(std::string file);

    void initFileList(std::set<FileTreeItem> &dest, bool filterfunc(wxString) = CASCFolder::defaultFilterFunc);

    bool setLocale(std::string);

  private:
    CASCFolder();
    CASCFolder(const CASCFolder &);

    void initLocales();
    void initVersion();
    std::string m_currentLocale;
    std::vector<std::string> m_localesFound;
    std::string m_version;
    int m_currentCascLocale;
    std::string m_folder;
    int m_openError;

    static CASCFolder * m_instance;

    static bool defaultFilterFunc(wxString) { return true; }
};



#endif /* _CASCFOLDER_H_ */
