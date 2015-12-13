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
#include <vector>


#include "CascLib.h"

#include <iostream>

#include <QString>

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
    CASCFolder();

    void init(const QString & path);

    std::string locale() { return m_currentLocale; }
    bool setLocale(std::string);
    std::vector<std::string> localesFound() { return m_localesFound; }

    QString version() { return m_version; }

    int lastError() { return m_openError; }

    bool fileExists(std::string file);

    HANDLE openFile(std::string file);

  private:
    CASCFolder(const CASCFolder &);

    void initLocales();
    void initVersion();
    std::string m_currentLocale;
    std::vector<std::string> m_localesFound;
    QString m_version;
    int m_currentCascLocale;
    QString m_folder;
    int m_openError;
    HANDLE hStorage;
};



#endif /* _CASCFOLDER_H_ */
