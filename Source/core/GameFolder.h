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
#include <unordered_set>
#include <vector>
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

      // Every file in the index by full path, in path order: the map getFile(QString) looks up,
      // one entry per path (the newest object when a path was added twice). Read-only.
      const std::map<QString, GameFile *> & filesByPath() const { return m_nameMap; }

      // How many paths of that index are models (.m2), textures (.blp) and world models (.wmo): kept as the index
      // changes, so the client's capabilities need no walk over its ~2 million paths.
      struct TypeCounts
      {
        size_t models = 0, textures = 0, buildings = 0;
      };
      const TypeCounts & indexedTypeCounts() const { return m_typeCounts; }

      // The files that are models (.m2), kept as files come and go: Browse lists the models without a walk over every
      // file of the client.
      const std::unordered_set<GameFile *> & modelFiles() const { return m_modelFiles; }

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
      // Which step reports: opening the storage, reading the listfile, indexing this client's files. The fraction
      // is 0..1, or below 0 while the step does not know its amount of work.
      enum class LoadPhase { OpeningStorage, ReadingListfile, IndexingFiles };
      using LoadReport = std::function<void(LoadPhase phase, float fraction)>;
      void setLoadProgressCallback(const LoadReport & cb) { m_loadProgressCb = cb; }

    protected:
      LoadReport m_loadProgressCb;
      ClientProfile m_clientProfile;

      // A bulk reload's name index, built at once (in path order, so every insertion is at its end): replaces the
      // index onChildAdded and onChildRemoved keep, and counts its file types again.
      void replaceNameIndex(std::map<QString, GameFile *> && index);
      // ... and the models among the children (after Container::replaceChildren).
      void replaceModelFiles(const std::vector<GameFile *> & models);

    private:
      void countType(const QString & path, int delta);
      std::map<QString, GameFile *> m_nameMap;
      TypeCounts m_typeCounts;
      std::unordered_set<GameFile *> m_modelFiles;
      QString m_path;
  };
}




#endif /* _GAMEFOLDER_H_ */
