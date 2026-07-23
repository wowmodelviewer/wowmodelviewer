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
#include <functional>

#include "GameFile.h"
#include "ClientProfile.h"

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
  class _GAMEFOLDER_API_ GameConfig
  {
    public:
      QString locale;
      QString version;
      QString product;
  };

  class _GAMEFOLDER_API_ GameFolder : public Container<GameFile>
  {
    public:
      explicit GameFolder(const QString & path);
      virtual ~GameFolder() {}

      virtual void init() = 0;
      virtual void initFromListfile(const QString & file) = 0;
      virtual void addCustomFiles(const QString & path, bool bypassOriginalFiles) = 0;

      // return full path for a given file ie :
      // HumanMale.m2 => Character\Human\male\humanmale.m2
      // (not always accurate, as file names not always unique)
      QString getFullPathForFile(QString file);

      void getFilesForFolder(std::vector<GameFile *> &fileNames, QString folderPath, QString extension = "");
      void getFilteredFiles(std::set<GameFile *> &dest, QString & filter);
      // Virtual so name-addressed storage (MPQ) can create files on demand on a name-map miss.
      virtual GameFile * getFile(QString filename);
      virtual GameFile * getFile(int id) = 0;

      virtual bool openFile(std::string file, void ** result) = 0;
      virtual bool openFile(int id, void ** result) = 0;
      
      virtual QString version() = 0;
      virtual int majorVersion() = 0;
      virtual QString locale() = 0;
      virtual bool setConfig(GameConfig config) = 0;
      virtual std::vector<GameConfig> configsFound() = 0;

      virtual int lastError() = 0;

      virtual void onChildAdded(GameFile *) override;
      virtual void onChildRemoved(GameFile *) override;

      QString path() { return m_path; }

      // The client profile (era/version/storage/lookup) for the currently loaded client.
      // Populated by the concrete folder in setConfig(); default-constructed (Unknown) until
      // then. Read-only for the rest of the app.
      const ClientProfile & clientProfile() const { return m_clientProfile; }

      // Optional progress callback (fraction 0..1), invoked while building the file list from
      // the listfile -- the longest startup step -- so the loading UI can advance during it.
      void setLoadProgressCallback(const std::function<void(float)> & cb) { m_loadProgressCb = cb; }

    protected:
      std::function<void(float)> m_loadProgressCb;
      ClientProfile m_clientProfile;

    private:
      std::map<QString, GameFile *> m_nameMap;
      QString m_path;
  };
}




#endif /* _GAMEFOLDER_H_ */
