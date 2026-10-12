// CASCFolder::readWholeFile against scripted CascLib answers.
//
// The contract under test: Read means every byte of the file was read AND the MD5
// of those bytes equals the file's content key (a CASC content key is the MD5 of
// the file's whole content, 16 bytes). Anything less is not Read and hands back no
// bytes. A whole read whose content key cannot be obtained, or comes back with a
// length other than 16, has nothing that vouches for its bytes: not Read.
//
// The production CASCFolder.cpp is compiled into this executable. Every CascLib
// function it calls is defined below as a stub that answers from the current
// case's Script: no game data, no storage on disk and no CascLib are involved.
// The storage is opened through the public setConfig(), whose CascOpenStorageEx
// the stub grants.

#include "CascLib.h"
#include "CASCFolder.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

// The Windows error codes the cases use, stated rather than taken on trust.
static_assert(ERROR_SUCCESS == 0, "ERROR_SUCCESS");
static_assert(ERROR_FILE_NOT_FOUND == 2, "ERROR_FILE_NOT_FOUND");
static_assert(ERROR_INVALID_HANDLE == 6, "ERROR_INVALID_HANDLE");
static_assert(ERROR_NOT_SUPPORTED == 50, "ERROR_NOT_SUPPORTED");
static_assert(ERROR_FILE_CORRUPT == 1392, "ERROR_FILE_CORRUPT");
static_assert(ERROR_FILE_ENCRYPTED == 6002, "ERROR_FILE_ENCRYPTED");

namespace
{
  // What the stubbed CascLib does for the current case.
  struct Script
  {
    bool fileOpens = true;
    QByteArray content;               // the file's bytes; CascGetFileSize64 reports their count
    qint64 chunk = 0;                 // most bytes one CascReadFile hands back (0: all it is asked for)
    qint64 failAt = -1;               // CascReadFile fails once this many bytes were handed back (-1: never)
    DWORD readError = ERROR_SUCCESS;  // the error a failing CascReadFile leaves
    bool keyQuerySucceeds = true;     // CascGetFileInfo(CascFileContentKey) returns true
    DWORD keyError = ERROR_SUCCESS;   // the error a failing key query leaves
    bool keyLengthWritten = true;     // the key query writes *pcbLengthNeeded
    size_t keyLength = 16;            // the length it writes there
    QByteArray key;                   // the key bytes it writes (no more than the length and the buffer allow)
  };

  Script script;
  DWORD lastError = ERROR_SUCCESS;
  qint64 readPos = 0;
  int storageToken = 0, fileToken = 0;
  int keysAdded = 0;
  // The file handle's life: CascLib answers nothing through a handle that is not open, and every handle opened must
  // be closed exactly once.
  bool fileOpen = false;
  int fileOpens = 0, fileCloses = 0;

  bool fileHandleOk(HANDLE h)
  {
    if (h == &fileToken && fileOpen)
      return true;
    lastError = ERROR_INVALID_HANDLE;
    return false;
  }
}

extern "C"
{
  bool WINAPI CascOpenStorageEx(LPCTSTR, PCASC_OPEN_STORAGE_ARGS, bool, HANDLE * phStorage)
  {
    *phStorage = &storageToken;
    return true;
  }
  bool WINAPI CascCloseStorage(HANDLE) { return true; }
  bool WINAPI CascWowRootNameCount(HANDLE, size_t * PtrNameCount, size_t * PtrNamelessCount)
  {
    *PtrNameCount = 0;
    if (PtrNamelessCount)
      *PtrNamelessCount = 0;
    return true;
  }
  bool WINAPI CascAddStringEncryptionKey(HANDLE, ULONGLONG, LPCSTR)
  {
    ++keysAdded;
    return true;
  }
  LPBYTE WINAPI CascFindEncryptionKey(HANDLE, ULONGLONG) { return nullptr; }
  bool WINAPI CascWowFindRootEntry(HANDLE, const char *, DWORD, PDWORD, bool *)
  {
    lastError = ERROR_FILE_NOT_FOUND;
    return false;
  }
  // No enumeration: setConfig falls back to its per-id probe and indexes nothing.
  HANDLE WINAPI CascFindFirstFile(HANDLE, LPCSTR, PCASC_FIND_DATA, LPCTSTR)
  {
    lastError = ERROR_NO_MORE_FILES;
    return nullptr;
  }
  bool WINAPI CascFindNextFile(HANDLE, PCASC_FIND_DATA) { return false; }
  bool WINAPI CascFindClose(HANDLE) { return true; }

  bool WINAPI CascOpenFile(HANDLE hStorage, const void *, DWORD, DWORD, HANDLE * PtrFileHandle)
  {
    if (hStorage != &storageToken || !script.fileOpens)
    {
      lastError = ERROR_FILE_NOT_FOUND;
      return false;
    }
    readPos = 0;
    fileOpen = true;
    ++fileOpens;
    *PtrFileHandle = &fileToken;
    return true;
  }
  bool WINAPI CascCloseFile(HANDLE hFile)
  {
    if (!fileHandleOk(hFile))
      return false;
    fileOpen = false;
    ++fileCloses;
    return true;
  }
  bool WINAPI CascGetFileSize64(HANDLE hFile, PULONGLONG PtrFileSize)
  {
    if (!fileHandleOk(hFile))
      return false;
    *PtrFileSize = (ULONGLONG)script.content.size();
    return true;
  }
  bool WINAPI CascReadFile(HANDLE hFile, void * lpBuffer, DWORD dwToRead, PDWORD pdwRead)
  {
    if (pdwRead)
      *pdwRead = 0;
    if (!fileHandleOk(hFile))
      return false;
    if (script.failAt >= 0 && readPos >= script.failAt)
    {
      lastError = script.readError;
      return false;
    }
    qint64 n = std::min((qint64)dwToRead, (qint64)script.content.size() - readPos);
    if (script.chunk > 0)
      n = std::min(n, script.chunk);
    if (script.failAt >= 0)
      n = std::min(n, script.failAt - readPos);
    memcpy(lpBuffer, script.content.constData() + readPos, (size_t)n);
    readPos += n;
    if (pdwRead)
      *pdwRead = (DWORD)n;
    return true;
  }
  bool WINAPI CascGetFileInfo(HANDLE hFile, CASC_FILE_INFO_CLASS InfoClass, void * pvFileInfo, size_t cbFileInfo,
                              size_t * pcbLengthNeeded)
  {
    if (!fileHandleOk(hFile))
      return false;
    if (InfoClass != CascFileContentKey)
    {
      lastError = ERROR_INVALID_PARAMETER;
      return false;
    }
    if (!script.keyQuerySucceeds)
    {
      lastError = script.keyError;
      return false;
    }
    size_t n = std::min((size_t)script.key.size(), cbFileInfo);
    if (script.keyLengthWritten)
      n = std::min(n, script.keyLength);
    if (pvFileInfo)
      memcpy(pvFileInfo, script.key.constData(), n);
    if (pcbLengthNeeded && script.keyLengthWritten)
      *pcbLengthNeeded = script.keyLength;
    return true;
  }
  DWORD GetCascError() { return lastError; }
}

namespace
{
  // MD5 reference test suite (RFC 1321, appendix A.5): message and digest.
  const QByteArray kEmpty = "";
  const QByteArray kEmptyKey = QByteArray::fromHex("d41d8cd98f00b204e9800998ecf8427e");
  const QByteArray kA = "a";
  const QByteArray kAKey = QByteArray::fromHex("0cc175b9c0f1b6a831c399e269772661");
  const QByteArray kAbc = "abc";
  const QByteArray kAbcKey = QByteArray::fromHex("900150983cd24fb0d6963f7d28e17f72");
  const QByteArray kDigest = "message digest";
  const QByteArray kDigestKey = QByteArray::fromHex("f96b697d7cb7938d525a2f31aaf161d0");
  const QByteArray kAlnum = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  const QByteArray kAlnumKey = QByteArray::fromHex("d174ab98d277d9f5a5611c2c9f419d9f");
  const QByteArray kDigits =
    "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
  const QByteArray kDigitsKey = QByteArray::fromHex("57edf4a22be3c955ac49da2e2107b67a");

  const int kFileDataId = 1001;
  const QString kPath = "World\\Maps\\Test\\Test.wdt";

  using Outcome = CASCFolder::ReadOutcome;

  const char * outcomeName(Outcome o)
  {
    switch (o)
    {
      case Outcome::Read: return "Read";
      case Outcome::NoStorage: return "NoStorage";
      case Outcome::NotListed: return "NotListed";
      case Outcome::NotLocal: return "NotLocal";
      case Outcome::Encrypted: return "Encrypted";
      case Outcome::TooLarge: return "TooLarge";
      case Outcome::Damaged: return "Damaged";
      case Outcome::Corrupt: return "Corrupt";
      case Outcome::Unverified: return "Unverified";
      default: return "not named by this test";
    }
  }

  struct Result
  {
    Outcome outcome = Outcome::Read;
    QByteArray out;
    unsigned int error = 0;
    bool handleClosed = false; // every file handle the read opened was closed exactly once, and none is left open
  };

  // One whole read with the given script, by FileDataID or (fileDataId -1) by path. The caller's buffer starts
  // non-empty, so "hands back no bytes" is the function's doing.
  Result read(const CASCFolder & folder, const Script & s, bool byPath = false)
  {
    script = s;
    lastError = ERROR_SUCCESS;
    readPos = 0;
    fileOpen = false;
    fileOpens = fileCloses = 0;
    Result r;
    r.out = "bytes left from an earlier read";
    r.error = 0xFFFFFFFFu;
    r.outcome = byPath ? folder.readWholeFile(-1, kPath, r.out, &r.error)
                       : folder.readWholeFile(kFileDataId, QString(), r.out, &r.error);
    r.handleClosed = !fileOpen && fileCloses == fileOpens;
    return r;
  }

  int passed = 0, failed = 0;

  // Every case also requires the file handle to have been closed exactly once.
  void report(const std::string & name, bool ok, const Result & r, const std::string & expected)
  {
    ok = ok && r.handleClosed;
    std::cout << "[casc-whole-read-test] " << (ok ? "PASS " : "FAIL ") << name << " -- expected " << expected
              << ", handle closed; got outcome=" << (int)r.outcome << " (" << outcomeName(r.outcome) << "), "
              << r.out.size() << " bytes, error code " << r.error << ", handle "
              << (r.handleClosed ? "closed" : "NOT closed exactly once") << std::endl;
    (ok ? passed : failed)++;
  }

  // A file whose whole content and 16-byte content key are both delivered.
  Script whole(const QByteArray & content, const QByteArray & key)
  {
    Script s;
    s.content = content;
    s.key = key;
    return s;
  }

  void expectRead(const CASCFolder & folder, const std::string & name, const Script & s, bool byPath = false)
  {
    const Result r = read(folder, s, byPath);
    report(name, r.outcome == Outcome::Read && r.out == s.content && r.error == ERROR_SUCCESS, r,
           "Read, all " + std::to_string(s.content.size()) + " bytes, error code 0");
  }

  // Every byte read, but the content key not obtained: not Read, no bytes, and the outcome that says so, with the
  // key query's error (ERROR_NOT_SUPPORTED when the query succeeded with a key of the wrong length).
  void expectUnverified(const CASCFolder & folder, const std::string & name, const Script & s, unsigned int error,
                        bool byPath = false)
  {
    const Result r = read(folder, s, byPath);
    report(name, r.outcome != Outcome::Read && r.out.isEmpty() && r.outcome == Outcome::Unverified && r.error == error,
           r, "not Read, no bytes: Unverified (" + std::to_string((int)Outcome::Unverified) + "), error code " +
                std::to_string(error));
  }

  void expectRefused(const CASCFolder & folder, const std::string & name, const Script & s, Outcome want,
                     unsigned int error)
  {
    const Result r = read(folder, s);
    report(name, r.outcome == want && r.out.isEmpty() && r.error == error, r,
           std::string(outcomeName(want)) + " (" + std::to_string((int)want) + "), no bytes, error code " +
             std::to_string(error));
  }
}

int main()
{
  try
  {
    // The digests above are the test's own statement; a typo in one would show as a code failure, so they are
    // checked once against an MD5 implementation that is not the code under test.
    for (const auto & pair : {std::make_pair(kEmpty, kEmptyKey), std::make_pair(kA, kAKey),
                              std::make_pair(kAbc, kAbcKey), std::make_pair(kDigest, kDigestKey),
                              std::make_pair(kAlnum, kAlnumKey), std::make_pair(kDigits, kDigitsKey)})
      if (QCryptographicHash::hash(pair.first, QCryptographicHash::Md5) != pair.second || pair.second.size() != 16)
        throw std::runtime_error("MD5 reference vector mistyped: \"" + pair.first.toStdString() + "\"");

    // setConfig adds the keys listed in extraEncryptionKeys.csv of the working directory; none must be there.
    if (QFile::exists("extraEncryptionKeys.csv"))
      throw std::runtime_error("run from a working directory without extraEncryptionKeys.csv");

    // An open storage through the public API: the stub grants CascOpenStorageEx; the legacy name probe that
    // follows finds nothing (as on a modern root) and the enumeration is unavailable.
    CASCFolder folder;
    core::GameConfig config;
    config.locale = "enUS";
    config.product = "wow";
    config.version = "1.0.0.1";
    script = Script();
    script.fileOpens = false;
    if (!folder.setConfig(config) || !folder.storageOpen())
      throw std::runtime_error("setConfig did not open the scripted storage");
    if (keysAdded != 0)
      throw std::runtime_error("setConfig added encryption keys from a file");

    // 1. Key obtained, 16 bytes, equal to the MD5 of the content.
    expectRead(folder, "1-key-matches-by-id", whole(kDigest, kDigestKey));
    expectRead(folder, "1-key-matches-by-path", whole(kDigest, kDigestKey), true);

    // 2. No content key for the file (ERROR_NOT_SUPPORTED, 50): nothing vouches for the bytes.
    {
      Script s = whole(kAbc, kAbcKey);
      s.keyQuerySucceeds = false;
      s.keyError = ERROR_NOT_SUPPORTED;
      expectUnverified(folder, "2-no-content-key-error-50-by-id", s, ERROR_NOT_SUPPORTED);
      expectUnverified(folder, "2-no-content-key-error-50-by-path", s, ERROR_NOT_SUPPORTED, true);
    }

    // 3. The key query fails for another reason (ERROR_INVALID_HANDLE, 6).
    {
      Script s = whole(kAbc, kAbcKey);
      s.keyQuerySucceeds = false;
      s.keyError = ERROR_INVALID_HANDLE;
      expectUnverified(folder, "3-key-query-error-6", s, ERROR_INVALID_HANDLE);
      // A failed query that leaves no error still has no key: Unverified, and never with error code 0.
      s.keyError = ERROR_SUCCESS;
      expectUnverified(folder, "3-key-query-fails-without-error", s, ERROR_NOT_SUPPORTED);
    }

    // 4. The key query succeeds but the key is not 16 bytes long, or its length is not given. The bytes it writes
    //    are those of the right key, so only the length is wrong.
    for (size_t length : {(size_t)0, (size_t)15, (size_t)32})
    {
      Script s = whole(kAbc, kAbcKey);
      s.keyLength = length;
      expectUnverified(folder, "4-key-length-" + std::to_string(length), s, ERROR_NOT_SUPPORTED);
    }
    {
      Script s = whole(kAbc, kAbcKey);
      s.keyLengthWritten = false;
      expectUnverified(folder, "4-key-length-not-written", s, ERROR_NOT_SUPPORTED);
    }

    // 5. A 16-byte key that is not the MD5 of the content: the bytes are not the file's.
    expectRefused(folder, "5-key-of-other-content", whole(kAbc, kAKey), Outcome::Corrupt, ERROR_FILE_CORRUPT);
    expectRefused(folder, "5-all-zero-key", whole(kAbc, QByteArray(16, '\0')), Outcome::Corrupt, ERROR_FILE_CORRUPT);
    {
      QByteArray flipped = kAbcKey;
      flipped[15] = (char)(flipped[15] ^ 0x01);
      expectRefused(folder, "5-key-last-bit-flipped", whole(kAbc, flipped), Outcome::Corrupt, ERROR_FILE_CORRUPT);
    }

    // 6. The content arrives in several pieces; the key matches the whole.
    {
      Script s = whole(kDigits, kDigitsKey);
      s.chunk = 7;
      expectRead(folder, "6-chunks-of-7-bytes", s);
      Script t = whole(kAlnum, kAlnumKey);
      t.chunk = 1;
      expectRead(folder, "6-chunks-of-1-byte", t);
    }

    // 7. The read fails part way (48 of 80 bytes, in 16-byte pieces), though the key would match the whole: never
    //    Read. The failure says why: no decryption key (6002) -> Encrypted, a frame hash mismatch (1392) -> Corrupt,
    //    data not on this computer (2) -> NotLocal, anything else (here no error at all, 0) -> Damaged.
    {
      const std::pair<DWORD, Outcome> failures[] = {{ERROR_FILE_ENCRYPTED, Outcome::Encrypted},
                                                    {ERROR_FILE_CORRUPT, Outcome::Corrupt},
                                                    {ERROR_FILE_NOT_FOUND, Outcome::NotLocal},
                                                    {ERROR_SUCCESS, Outcome::Damaged}};
      for (const auto & f : failures)
      {
        Script s = whole(kDigits, kDigitsKey);
        s.chunk = 16;
        s.failAt = 48;
        s.readError = f.first;
        expectRefused(folder, "7-partial-read-error-" + std::to_string(f.first), s, f.second, f.first);
      }
    }

    // 8. An empty file: its content key is the MD5 of nothing.
    expectRead(folder, "8-empty-file-key-of-nothing", whole(kEmpty, kEmptyKey));
    expectRefused(folder, "8-empty-file-other-key", whole(kEmpty, kAbcKey), Outcome::Corrupt, ERROR_FILE_CORRUPT);
  }
  catch (const std::exception & e)
  {
    std::cout << "[casc-whole-read-test] ERROR -- " << e.what() << std::endl;
    std::cout << "[casc-whole-read-test] RESULT: FAIL (" << passed << " passed, " << failed << " failed)" << std::endl;
    return 2;
  }
  std::cout << "[casc-whole-read-test] RESULT: " << (failed ? "FAIL" : "PASS") << " (" << passed << " passed, "
            << failed << " failed)" << std::endl;
  return failed ? 1 : 0;
}
