# Local changes to CascLib

This copy of CascLib is upstream commit 9fb2d38 (2026-04-20, the merge of PR #287,
`CASCLIB_VERSION` 3.0) plus the changes listed here. They make online storages, which stream
files from Blizzard's CDN into a local cache, usable for WoW Model Viewer's online mode.
Upstream has no equivalent for any of them as of 38a3466 (2026-09-25). When CascLib is
updated, carry these changes over, and drop only those that upstream has gained in its own
form. CascLib's own README.md is left unchanged.

The engine opens an online storage with `CascOpenStorageEx(NULL, &args, true, &h)`, where
`szLocalPath` is the cache folder, `szCodeName` is "wow", `szRegion` is set, `dwLocaleMask`
holds one locale bit and `dwFlags` is 0. Before that, it writes `versions` and `cdns` into
the cache folder itself, fetched over HTTPS. The changes below rely on that.

## 1. One archived file per HTTP range request

Stock CascLib downloaded the whole archive (up to 256 MB) on the first read of any file stored
in a CDN archive. That happened even when only `CascGetFileSize` or `CascGetFileInfo` was
called. Reading the 45 DB2 files the engine loads at startup and 6 models and textures cost
1,625 MB this way (WoW 12.1.0.69933), against 32.8 MB with range requests.

- `CascFiles.cpp`, `FetchCascFile` (the variant with archive info): online storages never
  download a whole archive. If the whole archive is already in the cache and reaches the end
  of the file (`FindCachedArchive`), the file is read from it, as before. Otherwise
  `FetchCascFileRange` fetches just the file's bytes and saves them as the loose file
  `data/xx/yy/<EKey>`. The archive info is then zeroed, which makes `OpenDataStream` read the
  copy through its loose-file branch. `CascGetFileSize` and `CascGetFileInfo` reach this code
  through `EnsureFileSpanFramesLoaded` and `OpenDataStream`, so they fetch only the range too.
- `CascFiles.cpp`, `FetchCascFileRange`: a cached copy counts only if its size equals the
  EncodedSize from the archive index. Anything else is left over from an interrupted run; it
  is deleted and fetched again. The CDN hosts are tried in the order of the `cdns` file.
- `CascFiles.cpp`, `HttpDownloadFile`: the ranged branch existed but could never succeed,
  because its error code was never cleared. It works now, and a failed read is reported as
  an error instead of success.
- `common/FileStream.cpp`, `BaseHttp_Download` and `BaseHttp_Read`, and `common/FileStream.h`
  (new field `fileDataOffset`): a read at an explicit offset from an HTTP stream that has no
  data yet sends a `Range` header. A 206 answer holds the data from the requested offset. A
  200 answer (a server that ignores the range) holds the whole resource, and the requested
  bytes are sliced out of it. The request buffer grew from 0x100 to 0x200 bytes for the
  longer request. Ribbit requests never get a range.
- `common/Mime.cpp`, `CASC_MIME::Load`: status 206 is accepted besides 200.

## 2. Downloads are checked before they enter the cache

Data come over plain HTTP. Block pages and captive portals answer with status 200, and stock
CascLib saved whatever came. When reading, it never compares the EKey, and it checks frame
hashes only with `CASC_STRICT_DATA_CHECK`, while a single-frame file has none.

- `CascFiles.cpp`, `VerifyDownloadedFile`, `VerifyBlteData` and `GetCdnKeyCheck`, called by
  `HttpDownloadFile` before `SaveLocalFile`: everything on the CDN is named by a hash.
  - Config files: the MD5 of the whole file equals the key.
  - Archive indexes: the MD5 of the footer (its last 28 bytes) equals the archive key
    (`VerifyArchiveIndexKey` in `CascIndexFiles.cpp`).
  - Encoded data (loose files such as ENCODING, ROOT and the VFS files, and range-fetched
    files): the EKey equals the MD5 of the BLTE header, or of the whole blob when the header
    size is 0. Each frame must also match the MD5 that the header gives for it, and the
    frames must fill the blob exactly. That covers every byte of the blob.
  - The PATCH manifest is not checked. It is not BLTE-encoded, its key is the MD5 of its own
    header, and it is only read when a caller opens the file `PATCH` by name.
- `CascFiles.cpp`, `DownloadFromCdnServers`: a host that sends data which fail the check is
  passed over like a host that is down, and nothing it sent is cached. The call fails with
  `ERROR_FILE_CORRUPT` when a host sent wrong data and no host sent good data, with
  `ERROR_FILE_NOT_FOUND` when a host answered that it does not have the file, and otherwise
  with the error of the last host (`ERROR_NETWORK_NOT_AVAILABLE` without network).
  A complete answer that lacks the requested bytes, such as a short block page answered to
  a range request, counts as wrong data.

## 3. The cache survives crashes and damage

- `CascFiles.cpp`, `SaveLocalFile`: files are written as `<name>.part` and renamed when
  complete (`ReplaceLocalFile`). On Windows the rename is `MoveFileEx` with
  `MOVEFILE_REPLACE_EXISTING`, because `rename` fails there when the target exists; elsewhere
  it is `rename`. The `.part` file is removed when writing or renaming fails. Stock CascLib
  took any non-empty file as valid, so one file cut short by a crash made every later open
  fail.
- `CascIndexFiles.cpp`, `LoadArchiveIndexFiles`: a cached index must match its archive key
  before it is parsed. An index that cannot be loaded is deleted and fetched once more, so
  that one damaged file no longer makes every later open fail. Without network the open
  still fails, but the damaged file is gone and the next online start fetches it.
- `CascFiles.cpp`, `FetchAndLoadConfigFile`: the same for a config file whose MD5 does not
  match its name.
- Files are only ever deleted in the cache of an online storage, never in a local install.
- `CascFiles.cpp`, `GetLocalFileSize` and `FileAlreadyExists`, and `CascReadFile.cpp`,
  `OpenDataStream`: cached files are opened read-only with write sharing. On Windows, the
  stock read-write open failed with a sharing violation while the same file was open
  elsewhere, e.g. when two files with the same content were open at once.
- `CascFiles.cpp`, `FetchCascFile` (the variant with archive info): the same local path is no
  longer tried twice or three times. In a warm CDN cache, `CheckArchiveFilesDirectories`
  finds `config` and `data` below the cache folder itself (it matches the NULL entry at the
  end of `DataDirs`), so the data path equals the root path. Each failed attempt downloaded
  the file again, which tripled the time and the traffic of every failed download.

## 4. Sockets and HTTP errors

- `common/Sockets.cpp`, `GetAddrInfoWrapper`: `EAI_AGAIN` is retried at most twice. Without
  a network, the resolver keeps returning it, and CascLib spun in this loop forever.
- `common/Sockets.cpp`, `CreateAndConnect`: `socket()` is checked against `INVALID_SOCKET`.
  The stock test `> 0` let a failed socket through on Windows, where `SOCKET` is unsigned and
  `INVALID_SOCKET` is `~0`. Every new socket gets `SO_RCVTIMEO` and `SO_SNDTIMEO` of 20
  seconds (a `DWORD` in milliseconds on Windows, a `timeval` elsewhere). Without them, a
  server that stopped answering blocked `recv()` forever. On Linux the send timeout also
  limits `connect()`; on Windows, `connect()` gives up after its own timeout of about 21 s.
- `common/Sockets.cpp`, `Connect`: the result of `CreateAndConnect` is compared with
  `INVALID_SOCKET`. The stock test `!= 0` took a failed connection as a good one, so the
  next address was never tried and the dead socket was cached for the host until the storage
  was closed. The address list is freed when no address connects.
- `common/Sockets.cpp`, `CASC_SOCKET::ReadResponse`, `Disconnect` and
  `CASC_SOCKET_CACHE::RemoveSocket` (declared in `common/Sockets.h`): a connection whose
  response did not come in completely (closed or reset by the server, timed out, or the
  response could not be stored) is closed and taken out of the socket cache, so that the next
  request connects anew. A failed allocation of the response buffer now sets an error.
- `common/Sockets.cpp`, `CASC_SOCKET::Delete`: closes only a valid socket, and frees the
  address list, which leaked before.
- `common/FileStream.cpp`, `BaseHttp_Open`: a failed connection sets
  `ERROR_NETWORK_NOT_AVAILABLE`. Stock CascLib left `ERROR_SUCCESS`, so the download counted
  as done, `OpenDataStream` returned success without a stream, and the read crashed on a NULL
  pointer (for example with an unresolvable host). `BaseHttp_Download` keeps the error that
  `ReadResponse` gives.
- `CascReadFile.cpp`, `OpenDataStream`: never returns success without a stream.
- Requests stay HTTP/1.1 with keep-alive.

## 5. Opening an online storage

- `CascOpenStorage.cpp`, `LoadCascStorage`: online storages skip the DOWNLOAD manifest. It
  only adds download priorities, tags and entries known by their EKey alone; WoW files are
  found by name or FileDataID through ROOT and ENCODING. Skipping it saves 63 MB on the first
  open of WoW 12.1, and a cache without it opens offline (stock CascLib hung there, upstream
  master crashed). Storages lose `CASC_FEATURE_TAGS`. Products whose ENCODING lists only some
  of their files (TVFS storages) would need the manifest back. Upstream commit 32ea004 made
  the manifest optional in its own way and collides with this change.
- `CascFiles.cpp`, `LoadCsvFile`: without `szCdnHostUrl` from the caller, `versions` and
  `cdns` are taken only from the cache. Stock CascLib then fetched them from its default
  version server, `http://us.patch.battle.net:1119`, which is blocked on many networks and
  made the open wait for minutes. With a missing `cdns`, the open now fails at once with
  `ERROR_FILE_NOT_FOUND`. `CASC_FEATURE_FORCE_DOWNLOAD` has no effect without a URL.
  `CascCdnDownload` still uses the default server.

## 6. Build hygiene

- `common/Path.h`, `CASC_PATH::AppendStringN` counts the characters it appends instead of
  computing an end pointer 64 KB past the string, and `AppendEKey` zero-initializes its
  buffer. The behaviour is unchanged. GCC 13 at -O2 reported both (`-Warray-bounds`,
  `-Wmaybe-uninitialized`) once the download code above was inlined differently.

## Error codes an online read or open can end with

| Code | Windows | Linux | Meaning |
|---|---|---|---|
| `ERROR_NETWORK_NOT_AVAILABLE` | 5035 | 1008 | No CDN host could be reached (no network, unresolvable names, refused connections) |
| `ERROR_FILE_CORRUPT` | 1392 | 1004 | A host sent data that do not match the key, and none sent good data |
| `ERROR_FILE_NOT_FOUND` | 2 | 2 (`ENOENT`) | A host answered that it does not have the file, or `versions`/`cdns` are missing from the cache |
| `ERROR_BAD_FORMAT` | 11 | 1000 | A download ended early (connection closed, or silent for more than 20 s) |
| `ERROR_DISK_FULL`, `ERROR_CAN_NOT_COMPLETE` | 112, 1003 | 28 (`ENOSPC`), 1003 | The cache could not be written, or the finished file could not be renamed |
| `ERROR_CANCELLED` | 1223 | 1009 | The progress callback asked to stop |

The Windows values come from winerror.h, the Linux values from `CascPort.h`.

## Known limits (unchanged)

- Data come over plain HTTP on port 80 with raw sockets: no TLS, no proxy, no redirects, no
  chunked transfers. Each response is held in memory in full (at most 1 GB).
- On Windows, `connect()` is not limited by the send timeout. A network that silently drops
  packets costs about 21 s per address, and the CDN hosts have up to 8 addresses each.
- Two threads that fetch the same file at the same time write the same `.part` file, and the
  socket cache has no lock, as upstream. The engine reads files from one thread.
- Files are checked in full only when CascLib downloads them. When it opens a storage, it checks
  cached archive indexes and config files against their keys again, but it takes any non-empty
  loose file (ENCODING, ROOT) as valid, and a range-fetched file needs only its exact size.
  Whatever else writes into the cache, such as the engine's prefetch, must check files the same
  way and write them under a temporary name first.
- `CheckCascBuildFileDirs` never counts its levels and climbs to the drive root when the cache
  folder has no `versions` file. The engine never opens a storage without it.
