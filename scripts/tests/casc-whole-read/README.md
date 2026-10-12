# CASC whole-read regression

`CASCFolder::readWholeFile()` reads one whole file of a CASC storage and says what happened: Read, NotListed, NotLocal, Encrypted, Damaged, Corrupt, ... The contract this harness holds it to: **Read means every byte was read AND the MD5 of those bytes equals the file's content key** (the content key is the MD5 of the file's whole content, 16 bytes). Anything less is not Read and hands back no bytes. A whole read whose content key cannot be obtained, or comes back with a length other than 16, has nothing that vouches for its bytes.

The harness compiles the production `Source/games/wow/CASCFolder.cpp` of `WMV_SOURCE_DIR` into the executable (copied at configure time without its unused `CASCFile.h` include, with `BUILDING_WOW_DLL` so the class is defined there) and links only `core.lib` and Qt5Core: no `wow.lib`, no CascLib, no game data. Every CascLib function the file calls is a stub in `regression.cpp` that answers from the current case's script: `CascOpenStorageEx`, `CascCloseStorage`, `CascWowRootNameCount`, `CascAddStringEncryptionKey`, `CascFindEncryptionKey`, `CascWowFindRootEntry`, `CascFindFirstFile`, `CascFindNextFile`, `CascFindClose`, `CascOpenFile`, `CascCloseFile`, `CascGetFileSize64`, `CascReadFile`, `CascGetFileInfo` and `GetCascError`. If `CASCFolder.cpp` starts calling another one, the link names it as an unresolved external; add a stub.

The storage is opened through the public `setConfig()` (locale enUS, product wow): the stub grants the storage open, the legacy name probe finds nothing and the enumeration is unavailable, as on a modern root. Each case then scripts the file's bytes, how `CascReadFile` hands them out (pieces, and where and with which error it fails) and what `CascGetFileInfo(CascFileContentKey)` answers (success or the error it sets, the length it writes or that it writes none, the key bytes), and calls `readWholeFile` by FileDataID (and for cases 1 and 2 also by path; case 9 passes `maxBytes` and `sizeOut`, and reads the file at the limit by path, every other case takes the defaults). The caller's buffer starts non-empty, so "no bytes" is the function's doing. The contents and keys are the MD5 reference test suite of RFC 1321 (appendix A.5), checked once against Qt's MD5 to catch a mistyped digest.

| Case | Script | Required |
|------|--------|----------|
| 1 | key 16 bytes, equal to MD5 of content | Read, all bytes, error code 0 |
| 2 | key query fails, error 50 (no content key) | Unverified, no bytes, error code 50 |
| 3 | key query fails, error 6; fails leaving no error | Unverified, no bytes, error code 6; error code 50 |
| 4 | key query succeeds, length 0, 15, 32, or not written | Unverified, no bytes, error code 50 |
| 5 | 16-byte key of other content, all-zero key, last bit flipped | Corrupt, no bytes, error code 1392 |
| 6 | content in 7-byte and 1-byte pieces, key matches | Read, all bytes |
| 7 | read fails after 48 of 80 bytes with 6002 / 1392 / 2 / 0 | Encrypted / Corrupt / NotLocal / Damaged, no bytes, that error code |
| 8 | empty file, key MD5 of nothing / another key | Read with no bytes / Corrupt, error code 1392 |
| 9 | 80-byte file, `maxBytes` 79 and 0; `maxBytes` 80 by path | TooLarge, no bytes, error code 0, size 80 in `sizeOut`, no `CascReadFile` call / Read, all bytes, size 80 |

Every case also requires the file handle to have been closed exactly once: the stubs answer nothing through a handle that is not open (`ERROR_INVALID_HANDLE`), so a content-key query made after the file is closed, or a handle left open, fails the case.

Cases 2-4 require "not Read, no bytes" and the outcome `Unverified` with the key query's error (50, `ERROR_NOT_SUPPORTED`, when the query succeeded with a key of the wrong length). Every line prints the outcome's numeric value.

Configure and build this directory as a standalone CMake project outside the repository, x64, Release, with the generator of the WMV build. `WMV_SOURCE_DIR` is the source tree whose `CASCFolder.cpp` is tested, `WMV_BUILD_DIR` its Release build (`core.lib`, `core.dll`); `QT_ROOT` defaults to the Qt that build was configured with. The build copies the matching `core.dll` and the Qt DLLs beside the executable.

```powershell
cmake -S scripts/tests/casc-whole-read -B <build-dir> -A x64 "-DWMV_SOURCE_DIR=<wmv-source>" "-DWMV_BUILD_DIR=<wmv-release-build>"
cmake --build <build-dir> --config Release
cd <build-dir>/Release; ./casc-whole-read.exe
```

Run it from a folder without `extraEncryptionKeys.csv` (`setConfig` reads that file from the working directory; the harness refuses to run when it is there). Each case prints `[casc-whole-read-test] PASS|FAIL <case> -- <detail>`, then `[casc-whole-read-test] RESULT: PASS|FAIL (<n> passed, <m> failed)`. Exit code 0 when all pass, 1 when any fails, 2 on a setup error.

Against f7eab99d, before the fix, cases 2, 3 and 4 failed (7 of 20, with the harness's first version, which asserted only "not Read, no bytes" since `Unverified` did not exist yet): a failed content-key query, or one that returns a length other than 16, left the bytes reported as Read without any check. With this version, f7eab99d's `readWholeFile` compiled against the fixed header (so that `Unverified` exists) fails the same cases and the new no-error case: 8 of 21.
