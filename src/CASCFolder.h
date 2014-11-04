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

class CASCFolder
{
  public:
    CASCFolder(const std::string & path);
    HANDLE hStorage;

    std::string locale() { return m_currentLocale; }
    int CASCLocale() { return m_currentCascLocale; }
    std::string folder() { return m_folder; }


    void initFileList(std::set<FileTreeItem> &dest, bool filterfunc(wxString) = CASCFolder::defaultFilterFunc);


  private:
    void initLocale();
    std::string m_currentLocale;
    int m_currentCascLocale;
    std::string m_folder;

    static bool defaultFilterFunc(wxString) { return true; }
};



#endif /* _CASCFOLDER_H_ */
