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

#include <wx/string.h>
#include <wx/wx.h>

#include "casclib/src/CascLib.h"


#define CASCFOLDER CASCFolder::instance()

class CASCFolder
{
  public:

    static CASCFolder & instance()
    {
      static CASCFolder m_instance;
      return m_instance;
    }

    bool init(const std::string & path);

    HANDLE hStorage;

    std::string locale() { return m_currentLocale; }
    std::string version() { return m_version; }
    int CASCLocale() { return m_currentCascLocale; }
    std::string folder() { return m_folder; }


    // return full path for a given file ie :
    // HumanMale.m2 => Character\Human\male\humanmale.m2
    std::string getFullPathForFile(std::string file);
    std::string getFullPathForFile(wxString file) { return getFullPathForFile(std::string(file.mb_str())); }

    bool fileExists(std::string file);

    void initFileList(std::set<FileTreeItem> &dest, bool filterfunc(wxString) = CASCFolder::defaultFilterFunc);


  private:
    CASCFolder();
    CASCFolder(const CASCFolder &);

    void initLocale();
    void initVersion();
    std::string m_currentLocale;
    std::string m_version;
    int m_currentCascLocale;
    std::string m_folder;

    static bool defaultFilterFunc(wxString) { return true; }
};



#endif /* _CASCFOLDER_H_ */
