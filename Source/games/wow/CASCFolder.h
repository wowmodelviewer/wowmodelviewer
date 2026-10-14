/*
 * CASCFolder.h
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#ifndef _CASCFOLDER_H_
#define _CASCFOLDER_H_

#include <functional>
#include <vector>
#include <unordered_set>

#include <QByteArray>

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
    static quint64 nextStorageGeneration();

  public:
    // Let go of the storage (a client replaced by one in another folder object).
    void closeStorage();
    // Files in the opened build that are not on this computer (a partly downloaded install).
    size_t remoteFileCount() const { return m_remoteCount; }
    bool isRemote(int id) const { return id >= 0 && (size_t)id < m_remoteIds.size() && m_remoteIds[id]; }

    // Which storage this folder holds right now: a number no other storage of this session has had. It changes
    // whenever setConfig opens a storage and when the storage is closed, so something built from one client can
    // tell the client it was built from is gone (a reused folder becomes another client in place).
    quint64 storageGeneration() const { return m_storageGeneration; }
    // How many files the storage's root knows by name, and how many it lists by FileDataID only (CascWowRootNameCount).
    // A path the root has no name for is absent from the client only when the root lists no file without a name.
    size_t rootNameCount() const { return m_rootNameCount; }
    size_t rootNamelessCount() const { return m_rootNamelessCount; }
    // Does the open storage know this decryption key now (its own list, plus extraEncryptionKeys.csv)?
    bool knowsEncryptionKey(quint64 keyName) const;
    bool storageOpen() const { return hStorage != nullptr; }

    // One file as the storage's ROOT lists it, looked up without opening it and without the folder's file index
    // (CascWowFindRootEntry): by FileDataID, or by path through the root's own name hashes. Only some roots carry
    // names: Classic Era and MoP Classic list their map files by name, Retail and Classic Beta list none
    // (docs/world-viewer/00-audit.md 4.1), so a path lookup there finds nothing.
    struct RootEntry
    {
      bool listed = false;   // the root lists the file (and ENCODING has its content key)
      bool local = false;    // its data is on this computer (the local index lists it)
      int fileDataId = -1;   // the FileDataID the root gives the file; -1 when it gives none
    };
    RootEntry rootEntry(int fileDataId) const;
    RootEntry rootEntryByPath(const QString & path) const;

    // THE STORAGE, SHARED (docs/world-viewer/04b-asset-serving.md section C). CascLib's lookups, reads and storage
    // reference count are made for several threads, each reading through file handles of its own; a file handle is not.
    // Storage names one storage for such a reader: the handle, the locale files are opened with (CascOpenFile ignores
    // it; passed as the folder's own reads pass it), and how many files the root lists without a name. storage() is the
    // folder's own, borrowed: valid while the folder holds it, for this thread's use (the GUI thread). shareStorage()
    // takes one more reference for another thread (CascWowShareStorage): it stays valid whatever the folder does next,
    // until releaseStorage() drops that reference -- once, by its holder, on any thread. From the first share on, the
    // folder adds no decryption key to that storage (adding one races another thread's decryption); keys are added
    // when it opens, before anything reads it.
    struct Storage
    {
      HANDLE handle = nullptr;
      int locale = 0;
      size_t rootNamelessCount = 0;
      bool open() const { return handle != nullptr; }
    };
    Storage storage() const;
    Storage shareStorage();
    static void releaseStorage(Storage & held);
    bool storageShared() const { return m_storageShared; }
    // rootEntry and readWholeFile on a storage named by handle: the same code the folder's own calls run.
    static RootEntry rootEntryIn(const Storage & from, int fileDataId);
    static RootEntry rootEntryByPathIn(const Storage & from, const QString & path);

    // Reads one whole file of the storage -- by FileDataID when fileDataId > 0, else by path through the root's names
    // -- without the folder's file index, and says what happened, so a caller can tell a file the root does not list
    // from one that is not on this computer, one that is encrypted with a key this computer lacks, and a damaged one.
    // Read is the only outcome that hands back bytes, and only bytes whose MD5 equals the file's content key: CascLib
    // checks a frame's hash only where the data carries one, so the content key is what vouches for every byte.
    enum class ReadOutcome
    {
      Read,       // every byte read, and their MD5 equals the file's content key (out holds them)
      NoStorage,  // no storage is open
      NotListed,  // the root does not list it
      NotLocal,   // listed, but its data is not on this computer
      Encrypted,  // listed and local, encrypted with a key this computer does not have
      TooLarge,   // larger than the caller allows (maxBytes), decided from its size before anything is read
      Damaged,    // opened, but its size or data could not be read (errorCode says why)
      Corrupt,    // read, but the data does not match a frame hash or its content key (damaged on disk)
      Unverified, // every byte read, but the file's content key could not be obtained, so they were not checked
                  // (errorCode: the content-key query's error; ERROR_NOT_SUPPORTED for a key of the wrong size, or
                  // a failed query that left no error)
    };
    // Where one readWholeFile spent its time, in milliseconds (-1: the step was not reached).
    struct ReadTimings
    {
      double openMs = -1.0;    // CascOpenFile
      double sizeMs = -1.0;    // CascGetFileSize64
      double allocMs = -1.0;   // the output buffer sized to the file
      double readMs = -1.0;    // the CascReadFile loop: storage reads, decoding, decompression, frame hash checks
      double keyMs = -1.0;     // CascGetFileInfo for the content key
      double closeMs = -1.0;   // CascCloseFile
      double md5Ms = -1.0;     // the MD5 of the bytes, compared with the content key
    };
    // sizeOut, when given, receives the file's size once the storage has told it (TooLarge says how large), else -1.
    // contentKeyOut, when given, receives the file's content key -- the 16-byte MD5 of its content that every byte was
    // checked against -- with Read, and is emptied otherwise: a caller that needs the bytes' MD5 has it without hashing
    // them again. timingsOut, when given, receives the time of each step.
    ReadOutcome readWholeFile(int fileDataId, const QString & path, QByteArray & out, unsigned int * errorCode = nullptr,
                              qint64 maxBytes = 64 * 1024 * 1024, qint64 * sizeOut = nullptr,
                              QByteArray * contentKeyOut = nullptr, ReadTimings * timingsOut = nullptr) const;
    // TEST HOOKS of readWholeFileFrom (the -terraintile harness, through the map asset reads; null in every other call):
    // afterOpen runs with the file open, before its size is asked (a read held there keeps the file and its storage
    // referenced); beforeVerify may change the bytes read before their MD5 is compared with the content key.
    struct ReadHooks
    {
      std::function<void()> afterOpen;
      std::function<void(QByteArray &)> beforeVerify;
    };
    static ReadOutcome readWholeFileFrom(const Storage & from, int fileDataId, const QString & path, QByteArray & out,
                                         unsigned int * errorCode = nullptr, qint64 maxBytes = 64 * 1024 * 1024,
                                         qint64 * sizeOut = nullptr, QByteArray * contentKeyOut = nullptr,
                                         ReadTimings * timingsOut = nullptr, const ReadHooks * hooks = nullptr);
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
    quint64 m_storageGeneration;     // see storageGeneration(); set in the constructor, renewed by every open and close
    size_t m_rootNameCount = 0;      // see rootNameCount()
    size_t m_rootNamelessCount = 0;  // see rootNamelessCount()
    bool m_storageShared = false;    // shareStorage() was called for the storage held now (no key is added to it)
    std::function<void(float)> m_progressCb; // optional load-progress reporter (see setProgressCallback)
};



#endif /* _CASCFOLDER_H_ */
