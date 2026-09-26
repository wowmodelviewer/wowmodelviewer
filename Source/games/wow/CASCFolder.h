/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#ifndef _CASCFOLDER_H_
#define _CASCFOLDER_H_

#include <atomic>
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

    // Online (CDN) mode, for a machine without a WoW install. The build comes from Blizzard's
    // version service and setConfig() opens the storage from the CDN, keeping everything it
    // downloads under cacheDir\<product>\ -- so a later start without network still opens the
    // cached build. configsFound() then holds one config: <product>'s current build for <region>
    // ("us", "eu", "kr", "tw", "cn") in <locale> ("enUS", "deDE", ...), or none when neither the
    // version service nor the cache has the build (lastError() is then ERROR_FILE_NOT_FOUND).
    // Runs on the calling thread and waits for the network in local event loops, so a splash
    // screen on that thread keeps painting.
    void initOnline(const QString & cacheDir, const QString & product, const QString & region, const QString & locale);
    bool isOnline() const { return m_online; }

    // Online only: true when initOnline() replaced "versions" and "cdns" with fresh copies from
    // the version service, false when it fell back to the copies already in the cache -- i.e.
    // this start is offline, and only what was downloaded before can be opened.
    bool onlineRefreshed() const { return m_refreshed; }

    // Online only. Asks a running setConfig() to stop at its next progress report -- or, while
    // CascLib downloads ROOT (it reports nothing meanwhile), once that download is complete.
    // setConfig() then fails with lastError() == ERROR_CANCELLED. Safe from any thread --
    // setConfig() may run on a worker while the UI thread shows the download. The request stays
    // in force for this folder.
    void cancelOnlineOpen() { m_cancel = true; }

    // Plain-English reading of a lastError() code, for the log and for a detail text.
    static QString errorText(int code);

    QString locale() { return m_currentConfig.locale; }
    QString version() { return m_currentConfig.version; }

    std::vector<core::GameConfig> configsFound() { return m_configs; }
    bool setConfig(core::GameConfig config);

    // Optional progress callback (fraction 0..1) invoked while enumerating present files in
    // setConfig() -- the "opening game data" step -- so the loading UI can advance during it.
    // Online it covers the whole of setConfig(): the download ahead of the open (0 .. 0.7), the
    // open itself (0.7 .. 0.9) and the enumeration (0.9 .. 1). It is called on the thread that
    // runs setConfig().
    void setProgressCallback(const std::function<void(float)> & cb) { m_progressCb = cb; }

    int lastError() { return m_openError; }

    bool fileExists(int id);

    bool openFile(int id, HANDLE * result);
    bool closeFile(HANDLE file);

    // int fileDataId(std::string & filename);

  private:
    CASCFolder(const CASCFolder &);
    // Declared and never defined, like the copy constructor. m_cancel (std::atomic) cannot be
    // copied, and an exported class must not leave MSVC an implicit assignment to generate.
    CASCFolder & operator=(const CASCFolder &);

    void initLocales();
    void initVersion();
    void initBuildInfo();
    void initOnlineVersions(const QString & product, const QString & locale);
    bool openOnlineStorage(int cascLocale);
    void addExtraEncryptionKeys();
    void buildPresentIdIndex();  // one-shot enumeration of present FileDataIDs -> fast fileExists()

    int m_currentCascLocale;
    core::GameConfig m_currentConfig;

    QString m_folder;   // local: the client's Data folder. online: the product's CDN cache folder
    int m_openError;
    HANDLE hStorage;

    bool m_online;               // set by initOnline()
    bool m_refreshed;            // see onlineRefreshed()
    std::atomic<bool> m_cancel;  // set by cancelOnlineOpen(), read while an online setConfig() runs
    QString m_region;            // online only: which row of the version service's tables to use

    std::vector<core::GameConfig> m_configs;
    std::unordered_set<int> m_presentIds;  // every present FileDataID (filled by buildPresentIdIndex)
    std::function<void(float)> m_progressCb; // optional load-progress reporter (see setProgressCallback)
};



#endif /* _CASCFOLDER_H_ */
