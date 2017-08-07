/*
 * GameFolder.h
 *
 *  Created on: 12 dec. 2014
 *      Author: Jeromnimo
 */

#ifndef _GAMEFOLDER_H_
#define _GAMEFOLDER_H_

#include <map>
#include <set>

#include <QString>

#include "GameFile.h"

#include "metaclasses/Container.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _GAMEFOLDER_API_ __declspec(dllexport)
#    else
#        define _GAMEFOLDER_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEFOLDER_API_
#endif

namespace core
{
  class _GAMEFOLDER_API_ GameFolder : public Container<GameFile>
  {
    public:
      GameFolder();
      virtual ~GameFolder() {}

      virtual void init(const QString & path) = 0;
      virtual void initFromListfile(const QString & file) = 0;
      virtual void addCustomFiles(const QString & path, bool bypassOriginalFiles) = 0;

      // return full path for a given file ie :
      // HumanMale.m2 => Character\Human\male\humanmale.m2
      // (not always accurate, as file names not always unique)
      QString getFullPathForFile(QString file);

      void getFilesForFolder(std::vector<GameFile *> &fileNames, QString folderPath, QString extension = "");
      void getFilteredFiles(std::set<GameFile *> &dest, QString & filter);
      GameFile * getFile(QString filename);
      virtual GameFile * getFile(int id) = 0;

      virtual void * openFile(std::string file) = 0;

      virtual QString version() = 0;

      virtual std::string locale() = 0;
      virtual bool setLocale(std::string) = 0;
      virtual std::vector<std::string> localesFound() = 0;

      virtual int lastError() = 0;

      virtual void onChildAdded(GameFile *);
      virtual void onChildRemoved(GameFile *);

    private:
      std::map<QString, GameFile *> m_nameMap;
  };
}




#endif /* _GAMEFOLDER_H_ */
