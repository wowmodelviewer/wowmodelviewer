/*
 * GameFolder.h
 *
 *  Created on: 12 dec. 2014
 *      Author: Jeromnimo
 */

#ifndef _GAMEFOLDER_H_
#define _GAMEFOLDER_H_

#include <map>
#include <stdio.h>

#include <QString>
#include <QStringList>

#include "CASCFolder.h"
#include "GameFile.h"

#include "metaclasses/Container.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _GAMEFOLDER_API_ __declspec(dllexport)
#    else
#        define _GAMEFOLDER_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEFOLDER_API_
#endif

class _GAMEFOLDER_API_ GameFolder : public Container<GameFile>
{
  public:
    GameFolder();
    virtual ~GameFolder() {}

    void init(const QString & path, const QString & file);

    // return full path for a given file ie :
    // HumanMale.m2 => Character\Human\male\humanmale.m2
    QString getFullPathForFile(QString file);

    bool fileExists(std::string file);

    void getFilesForFolder(std::vector<QString> &fileNames, QString folderPath);
    void getFilteredFiles(std::set<GameFile *> &dest, QString & filter);

    HANDLE openFile(std::string file);

    QString version();

    std::string locale();
    bool setLocale(std::string);
    std::vector<std::string> localesFound();

    int lastError();

  private:
    CASCFolder m_CASCFolder;
};



#endif /* _GAMEFOLDER_H_ */
