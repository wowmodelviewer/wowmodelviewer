/*
 * GameDirectory.h
 *
 *  Created on: 12 dec. 2015
 *      Author: Jeromnimo
 */

#ifndef _GAMEDIRECTORY_H_
#define _GAMEDIRECTORY_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <windows.h> // HANDLE

#include <QString>

#include "metaclasses/Container.h"

#include "GameFolder.h"
#include "FileTreeItem.h"

class CASCFolder;

#define GAMEDIRECTORY GameDirectory::instance()

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _GAMEDIRECTORY_API_ __declspec(dllexport)
#    else
#        define _GAMEDIRECTORY_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEDIRECTORY_API_
#endif

class _GAMEDIRECTORY_API_ GameDirectory : public Container<Component>
{
  public:
    static GameDirectory & instance()
    {
      if(GameDirectory::m_instance == 0)
        GameDirectory::m_instance = new GameDirectory();
      return *m_instance;
    }

    void init(const QString & path);

    std::string locale();
    bool setLocale(std::string);
    std::vector<std::string> localesFound();

    int lastError();

    QString version();

    // return full path for a given file ie :
    // HumanMale.m2 => Character\Human\male\humanmale.m2
    QString getFullPathForFile(QString file);

    bool fileExists(std::string file);

    void initFileList(std::set<FileTreeItem> &dest);

    void filterFileList(std::set<FileTreeItem> &dest, bool filterfunc(QString) = GameDirectory::defaultFilterFunc);
    void getFilesForFolder(std::vector<QString> &fileNames, QString folderPath);

    static void beautifyFileName(QString & file);
    void addFile(QString &);

    HANDLE openFile(std::string file);

  private:
    // disable explicit construct and destruct
    GameDirectory();
    virtual ~GameDirectory() {}
    GameDirectory(const GameDirectory &);
    void operator=(const GameDirectory &);

    static GameDirectory * m_instance;

    CASCFolder * m_gameFolder;
    QString m_gameFolderPath;

    static bool defaultFilterFunc(QString) { return true; }

};



#endif /* _GAMEDIRECTORY_H_ */
