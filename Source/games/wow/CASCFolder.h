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

    int lastError() { return m_openError; }

    bool fileExists(int id);

    bool openFile(int id, HANDLE * result);
    bool closeFile(HANDLE file);

    // Availability probe for the Voice Lines browser: open the file by id and try to read its
    // first block (which is where CascLib decrypts). Returns 0 = playable, 1 = encrypted / missing
    // TACT key (ERROR_FILE_ENCRYPTED), 2 = missing/not openable in this build. Does not add the
    // file to the tree. Cheap (16-byte read) but does touch CASC per call.
    int fileKeyStatus(int id);

    // int fileDataId(std::string & filename);

  private:
    CASCFolder(const CASCFolder &);

    void initLocales();
    void initVersion();
    void initBuildInfo();
    void addExtraEncryptionKeys();
    void buildPresentIdIndex();  // one-shot enumeration of present FileDataIDs -> fast fileExists()

    int m_currentCascLocale;
    core::GameConfig m_currentConfig;

    QString m_folder;
    int m_openError;
    HANDLE hStorage;

    std::vector<core::GameConfig> m_configs;
    std::unordered_set<int> m_presentIds;  // every present FileDataID (filled by buildPresentIdIndex)
    std::function<void(float)> m_progressCb; // optional load-progress reporter (see setProgressCallback)
};



#endif /* _CASCFOLDER_H_ */
