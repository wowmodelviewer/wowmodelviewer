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
      // How many MPQ archives a legacy install folder has (opened and closed again; nothing else is touched).
      static int countMpqArchives(const QString & dataFolder, const QString & locale);

      GameFile * getFile(int id) override;
      GameFile * getFile(QString filename) override; // adds MPQ create-on-demand

      bool openFile(int id, HANDLE * result) override;
      bool openFile(std::string file, HANDLE * result) override;

      QString version() override;
      int majorVersion() override;
      QString locale() override;
      bool setConfig(core::GameConfig config) override;
      std::vector<core::GameConfig> configsFound() override;

      int lastError() override;
      // Let go of the CASC storage: this folder's client was replaced by one in another folder object.
      void closeStorage() { m_CASCFolder.closeStorage(); }
      // Files in the opened build that are not on this computer (a partly downloaded install).
      size_t remoteFileCount() const { return m_CASCFolder.remoteFileCount(); }
      // Of those, the ones this viewer uses (models, skins, animations, skeletons, textures, world models, tables),
      // counted from the file list: optional videos and the like do not make an install "not fully downloaded".
      size_t remoteViewerFileCount() const { return m_remoteViewerFiles; }
      // Is this file listed for the opened build but not on this computer (a partly downloaded install)?
      bool isRemoteFile(const QString & name) const;
      // Frees the files the last reload detached (initFromListfile). Called once the load that detached them has
      // rebuilt everything that could point at them (Browse, the character controls; the canvas was cleared
      // before): ModelViewer::LoadWoW, at its end. Also run by the next reload, for a load that never got there.
      void freeDetachedFiles();
      size_t detachedFileCount() const { return m_detached.size(); }

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
      size_t m_remoteViewerFiles = 0;
      std::map<QString, int> m_nameIdMap;
      std::vector<GameFile *> m_detached; // each holds one reference of its own (freeDetachedFiles drops it)
  };
}


#endif /* _WOWFOLDER_H_ */
