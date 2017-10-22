/*
 * WoWFolder.h
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#ifndef _WOWFOLDER_H_
#define _WOWFOLDER_H_

#include <map>
#include <stdio.h>

#include <QString>
#include <QStringList>

#include "CASCFolder.h"
#include "GameFile.h"
#include "GameFolder.h"

#include "metaclasses/Container.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWFOLDER_API_ __declspec(dllexport)
#    else
#        define _WOWFOLDER_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWFOLDER_API_
#endif

namespace wow
{
  class _WOWFOLDER_API_ WoWFolder : public core::GameFolder
  {
    public:
      WoWFolder(const QString & path);
      virtual ~WoWFolder() {}

      void init();
      void initFromListfile(const QString & file);
      void addCustomFiles(const QString & path, bool bypassOriginalFiles);

      GameFile * getFile(int id);

      bool openFile(std::string file, HANDLE * result);

      QString version();
      QString locale();
      bool setConfig(core::GameConfig config);
      std::vector<core::GameConfig> configsFound();

      int lastError();

      void onChildAdded(GameFile *);
      void onChildRemoved(GameFile *);

    private:
      CASCFolder m_CASCFolder;
      std::map<int, GameFile *> m_idMap;
  };
}


#endif /* _WOWFOLDER_H_ */
