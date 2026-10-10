/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#ifndef _CASCFOLDER_H_
#define _CASCFOLDER_H_

#include <vector>
#include <unordered_set>

typedef void* HANDLE;

#include "GameFolder.h" // GameConfig

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

    QString locale() { return m_currentConfig.locale; }
    QString version() { return m_currentConfig.version; }

    std::vector<core::GameConfig> configsFound() { return m_configs; }
    bool setConfig(core::GameConfig config);

    // Optional progress callback (fraction 0..1) invoked while enumerating present files in
    // setConfig() -- the "opening game data" step -- so the loading UI can advance during it.
    void setProgressCallback(const std::function<void(float)> & cb) { m_progressCb = cb; }
    void reportProgress(float fraction) { if (m_progressCb) m_progressCb(fraction); }

    int lastError() { return m_openError; }

    bool fileExists(int id);

    bool openFile(int id, HANDLE * result);
    bool closeFile(HANDLE file);

    // int fileDataId(std::string & filename);

  private:
    CASCFolder(const CASCFolder &);

    void initLocales();
    void initVersion();
    void initBuildInfo();
    void addExtraEncryptionKeys();
    void buildPresentIdIndex();  // one-shot enumeration of present FileDataIDs -> fast fileExists()

  public:
    // Let go of the storage (a client replaced by one in another folder object).
    void closeStorage();
    // Files in the opened build that are not on this computer (a partly downloaded install).
    size_t remoteFileCount() const { return m_remoteCount; }
    bool isRemote(int id) const { return id >= 0 && (size_t)id < m_remoteIds.size() && m_remoteIds[id]; }
  private:

    int m_currentCascLocale;
    core::GameConfig m_currentConfig;

    QString m_folder;
    int m_openError;
    HANDLE hStorage;

    std::vector<core::GameConfig> m_configs;
    // Every FileDataID with a local copy, and those in the build with none on this computer (buildPresentIdIndex): one
    // bit per id (ids reach ~8.5 million, so 1 MB each), which the ~2.3 million listfile lookups of a load read
    // without hashing.
    std::vector<bool> m_presentIds;
    std::vector<bool> m_remoteIds;
    size_t m_presentCount = 0;
    size_t m_remoteCount = 0;
    std::function<void(float)> m_progressCb; // optional load-progress reporter (see setProgressCallback)
};



#endif /* _CASCFOLDER_H_ */
