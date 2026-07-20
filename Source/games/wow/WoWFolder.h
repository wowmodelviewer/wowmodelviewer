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

      // Legacy-MPQ setup: open the archive chain under dataFolder, build the client profile
      // (storage=MPQ), select the MPQ provider, populate the browsable file tree from the MPQ
      // listfile, and log the banner. Returns the number of archives opened (0 = no MPQ client
      // found). Independent of the CASC path.
      int initMpq(const QString & dataFolder, const QString & locale, const QString & version);

      GameFile * getFile(int id) override;
      GameFile * getFile(QString filename) override; // adds MPQ create-on-demand

      bool openFile(int id, HANDLE * result) override;
      bool openFile(std::string file, HANDLE * result) override;

      int fileKeyStatus(int id) override; // Voice Lines browser: playable/encrypted/missing (0/1/2)

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
      QString m_mpqLocale; // detected/selected locale when in MPQ mode (for locale())
      std::map<int, GameFile *> m_idMap;
      std::map<int, QString> m_idNameMap;
      std::map<QString, int> m_nameIdMap;
  };
}


#endif /* _WOWFOLDER_H_ */
