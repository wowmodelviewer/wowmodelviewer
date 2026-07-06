/*
 * WoWFolder.h
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#ifndef _WOWFOLDER_H_
#define _WOWFOLDER_H_

#include <map>
#include <memory>

#include <QString>

#include "CASCFolder.h"
#include "GameFile.h"
#include "GameFolder.h"
#include "IFileProvider.h"

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

      void init() override;
      void initFromListfile(const QString & file) override;
      void addCustomFiles(const QString & path, bool bypassOriginalFiles) override;

      GameFile * getFile(int id) override;

      bool openFile(int id, HANDLE * result) override;
      bool openFile(std::string file, HANDLE * result) override;

      QString version() override;
      int majorVersion() override;
      QString locale() override;
      bool setConfig(core::GameConfig config) override;
      std::vector<core::GameConfig> configsFound() override;

      int lastError() override;

      void onChildAdded(GameFile *) override;
      void onChildRemoved(GameFile *) override;
      QString fileName(int id);
      int fileID(QString fileName);
    private:
      CASCFolder m_CASCFolder;
      // Storage backend behind openFile(). Created in setConfig() from the detected client
      // profile: a CascFileProvider (forwards to m_CASCFolder -- the modern default) or, for
      // an old MoPaQ client, the placeholder MpqFileProvider. Null until setConfig() runs, in
      // which case openFile() falls back to m_CASCFolder directly.
      std::unique_ptr<core::IFileProvider> m_provider;
      std::map<int, GameFile *> m_idMap;
      std::map<int, QString> m_idNameMap;
      std::map<QString, int> m_nameIdMap;
  };
}


#endif /* _WOWFOLDER_H_ */
