/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#ifndef _CASCFOLDER_H_
#define _CASCFOLDER_H_

#include <map>
#include <set>
#include <string>

#include <wx/wx.h>

#include "casclib/src/CascLib.h"
#include "mpq.h" // FileTreeItem

#define CASCFOLDER CASCFolder::instance()

class CASCFolder
{
  public:

    static CASCFolder & instance()
    {
      static CASCFolder m_instance;
      return m_instance;
    }

    void init(const std::string & path);

    HANDLE hStorage;

    std::string locale() { return m_currentLocale; }
    int CASCLocale() { return m_currentCascLocale; }
    std::string folder() { return m_folder; }

    // return full path for a given file ie :
    // HumanMale.m2 => Character\Human\male\humanmale.m2
    std::string getFullPathForFile(std::string & file);


    void initFileList(std::set<FileTreeItem> &dest, bool filterfunc(wxString) = CASCFolder::defaultFilterFunc);


  private:
    CASCFolder();
    CASCFolder(const CASCFolder &);

    void initLocale();
    std::string m_currentLocale;
    int m_currentCascLocale;
    std::string m_folder;

    static bool defaultFilterFunc(wxString) { return true; }
};



#endif /* _CASCFOLDER_H_ */
