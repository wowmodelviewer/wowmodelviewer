# World viewer 04b: map asset serving off the GUI thread

Branch `world/04b-terrain-asset-serving`, from `develop` at `42977505` (2026-10-14), which contains stage 04
(`138aa369`, merged as `9da5343c`). This branch changes **how** the host serves `getMapAsset`, nothing about **what**
it serves: no shader, terrain material formula, geometry, protocol message or player code is changed.

**Push status: NOT PUSHED.**

**Summary.** `getMapAsset` files are now read, decoded and checked against their content keys by one worker thread that
holds its own reference to the client's CASC storage (a lease taken on the GUI thread while the GUI holds its own; once
the worker has taken it over, released only by the worker); the GUI thread decides, checks again and sends. On the final
build, across 51 runs: GUI time per answer at most 3.62 ms (was 38.21), no poll over 20 ms of map serving (was 37 ms in
every run), every file served read by the worker, no answer crossing a client in 8 switches with reads held across them,
at most one 5.6 MB file held, and whole tiles loading faster than the 1.1 release build on all four tiles measured (map
2923 32_24: 1,238.9 -> 725.9 ms cold). Terrain pixels, geometry, M2 and WMO captures are identical to the release
build's; protocol 13 and the player are unchanged. Section 19 lists PASS / FAIL / SKIP / UNSUPPORTED.

**Before Stage 05.** Two findings of the post-commit review (2026-10-15) bound what the next stage may rely on: a
shutdown event re-entrancy hazard that **must be removed before Stage 05 enables interactive terrain loading** (18.1),
and outgoing IPC buffering that is bounded for the shipped Unity player's requests but **not against arbitrary local
clients**, nor for the viewer's own pushed messages to a player that has stopped reading (15, 18.2). The first cannot
happen at this commit: only the `-terraintile` harness starts terrain loads, and at its end the server is stopped
before `~ModelViewer` could pump with it listening. The second is reachable now by any local process that connects to
the server's port, exactly as at `42977505`; with the shipped player only the viewer's own messages can pile up, and
only while it is connected and not reading.

Sections A-H are the implementation gate the specification requires: they were written, from measurements and a
source audit, before any serving code was changed. Section 9 onwards records the implementation and its validation.

Evidence levels as in `00-audit.md`: `PROVEN_SOURCE` (read at the cited path:line and checked by an independent
verifier), `PROVEN_RUNTIME` (measured here, with the run named), `INFERRED`, `UNKNOWN`. Citations without a commit are
to `42977505` in sections A-H and to the branch's commit `81f79982` from section 9 on. In A-H, citations of
`MapAssetWorker.*` and those added by the amendments and the post-commit corrections (the wake and lease citations in
B.2, B.3, C.2-C.4 and D, such as `UnityIpcServer.cpp:87-88` and `:1659-1709`, `MapAssetWorker.cpp:61`-`:335`,
`modelviewer.cpp:1865` and `:1962`) are to `81f79982` too.

## A. The reproduced bottleneck

### A.1 Method

- **Binaries.** The verified 1.1 release build of `42977505` (`wowmodelviewer.exe` SHA-256 `fb1e9de7...`, `wow.dll`
  `b33a9216...`, `core.dll` `21dfb91b...`, byte-identical to the user's build and to the binaries the 1.1 installer was
  tested with) and the stage 04 final player (`Assembly-CSharp.dll` MD5 `5C6EC342...`, self-test 841/0), in a private
  runtime (`F:\wmv-build-fix\w04b\run_base`). A second build, the same source plus timing instrumentation only (A.3),
  ran in its own runtime.
- **Tile.** Retail map 2923 tile 32_24 (WDT 6838755, root 7473884, tex0 7473887; 32 textures, 90,914,944 bytes, twelve
  of 5,593,604 bytes). The live client is Retail 12.1.5.70391, the pinned stage 04 assets are from 12.1.0.69933: every
  file served in every run was compared with the pinned copy of the same FileDataID
  (`build\world-audit-data\materials\wow\stage04_nibble4`): **525 of 525 identical MD5s, 0 different** -- the tile's
  bytes did not change between the two builds.
- **Runs.** `-terraintile 2923 32 24 -product wow` with `WMV_TERRAINTEST_SEQUENCE=repeat:4` (five loads per process),
  three processes per build, after one warm-up process; hidden desktop; no game client running (checked after every
  run); Ryzen 9 9950X3D, 93.6 GB, the game on NVMe. Harness PASS 123/0/0 in every run.
- **Machine-readable baselines.** `F:\wmv-build-fix\w04b\base\baseline_release.json` and
  `F:\wmv-build-fix\w04b\instr\baseline_instrumented.json` (written by `summarize.py`, which reads each run's out.json
  and the player's log).

### A.2 Result (release binaries; median / p95 / max)

| Files | n | Handler (GUI thread) ms | Storage read ms | Queue wait ms | Player round trip ms |
|---|---|---|---|---|---|
| WDT (163,908 B) | 15 | 0.70 / 0.89 / 0.89 | 0.51 / 0.57 / 0.57 | 0.00 | 15.3 / 16.8 / 16.8 |
| root ADT (458,168 B) | 15 | 2.92 / 3.00 / 3.00 | 2.58 / 2.70 / 2.70 | 0.01 | 16.9 / 28.9 / 28.9 |
| tex0 ADT (1,996,296 B) | 15 | 12.63 / 13.91 / 13.91 | 11.55 / 12.63 / 12.63 | 3.05 / 3.67 / 3.67 | 31.1 / 46.0 / 46.0 |
| textures <= 1 MiB (350,724 B) | 60 | 2.22 / 2.60 / 2.74 | 1.95 / 2.22 / 2.38 | 79.6 / 110.0 / 126.4 | 96.5 / 130.6 / 147.5 |
| textures 1-4 MiB (1,399,300 B) | 240 | 8.84 / 9.62 / 11.03 | 8.22 / 8.81 / 9.50 | 77.6 / 140.5 / 170.7 | 122.5 / 170.0 / 191.7 |
| **textures > 4 MiB (5,593,604 B)** | **180** | **33.08 / 35.98 / 38.21** | **31.27 / 33.67 / 35.63** | 77.5 / 125.6 / 154.7 | 144.4 / 185.7 / 211.2 |

- Per texture id, the twelve 5,593,604-byte textures have handler medians of **28.14-34.12 ms** (n = 15 each): stage 04
  reported 28.35-34.20 ms. **The FAIL against the 20 ms budget is reproduced.**
- GUI time of the polls that served map files: max 35.9, 38.9 and 47.8 ms (one per process); median 11.4-12.4 ms.
- The whole tile: cold (each process's first load, n = 3) total 1,238.9 ms, of which texture fetch 1,062.2 ms; warm
  (n = 12) 1,215.7 ms and 1,071.2 ms.
- Host private bytes 2.34 GB, flat over the five loads (+5.6 MB after the first).

### A.3 Where the 33 ms go (instrumented build, same runs otherwise)

The instrumentation (a patch of timers only, kept as `F:\wmv-build-fix\w04b\instrumentation_before.patch` and carried
into the branch) times every step of the read (`CASCFolder::readWholeFile`) and of the answer
(`UnityIpcServer::answerMapAsset`), every poll, and every round of the harness's pump. Its own cost is below the
noise: handler median 32.95 ms instrumented against 33.08 released.

| Step, files > 4 MiB (n = 180) | median | p95 | max | share of the handler |
|---|---|---|---|---|
| `CascReadFile` loop: archive reads, BLTE frame hash check (`CASC_STRICT_DATA_CHECK`), zlib decode | **24.36** | 26.32 | 28.21 | 74 % |
| MD5 of the 5.6 MB against the content key | **6.78** | 7.13 | 7.62 | 21 % |
| `send()` into the socket (64 MB send buffer) | 1.36 | 1.84 | 2.54 | 4 % |
| copy of header and payload into the send buffer | 0.28 | 0.38 | 0.79 | 1 % |
| `CascGetFileSize64` | 0.025 | 0.098 | 2.412 | |
| output buffer allocation | 0.015 | 0.036 | 0.053 | |
| storage root lookup (`CascWowFindRootEntry`) | 0.002 | 0.004 | 0.011 | |
| `CascOpenFile`, `CascGetFileInfo` (content key), `CascCloseFile` | 0.002, 0.000, 0.002 | | | |
| decision and accounting; kind check; JSON header | 0.005; 0.002; 0.019 | | | |
| the record's log line (after the handler time) | 0.097 | 0.117 | 0.144 | |

- **The time is not spent in CASC lookup.** Handle acquisition, root lookup, size check and allocation together are
  under 0.05 ms. It is spent decoding and checking the data: the CascLib read with its frame hashes (74 %) and the
  content-key MD5 (21 %). The same file read by the audit's `casc_extract` probe, which opens without
  `CASC_STRICT_DATA_CHECK` and computes no MD5, took 19-20 ms (3 runs): the frame checks and the MD5 are the integrity
  contract and stay (section 6 of the specification); they only move off the GUI thread.
- **GUI thread.** Every poll's own time (`gui pollMs`, 374-383 polls per process): max 37.65 / 36.92 / 37.03 ms. The
  longest uninterrupted work of the GUI thread (each round of the harness's pump: the poll and the wx queue): max
  37.93 / 37.22 / 37.30 ms. The poll interval: median 16.0 ms, p95 47.0 ms (a poll that serves a large file delays the
  next).
- **Measured, not assumed, for the remaining parts:** response serialization 0.02 ms, socket queueing (append) 0.28 ms,
  `send()` 1.36 ms -- the only parts of serving that must stay on the GUI thread (the socket is GUI-owned, B.4) cost
  about 1.7 ms for a 5.6 MB file.

### A.4 One more open storage (measured for section C)

`casc_extract.exe` (the audit's probe, built against the viewer's CascLib) opening Retail, enumerating it and reading one
texture, its private bytes sampled every 10 ms (`storage_series.ps1`, three runs of `storage_cost.ps1`): open 2,431-2,466
ms, enumeration 340-345 ms; private bytes 894 MB at the peak (the index files loaded during the open), about 595 MB
through the root load and **about 600 MB once open**.

## B. CASC thread-safety findings

Method: five read-only auditors, each on one area (CascLib concurrency on one storage; CascLib lifetimes and independent
storages; the host's threads, callers and lifetimes; Qt, wx and the logger; the player), every claim with path:line
evidence; the three safety-critical areas then each re-read by an independent verifier told to refute them (section
10 gives the verifiers' results). CascLib is the vendored 3.0 with the project's additions `CascWowFindRootEntry` and
`CascWowRootNameCount`.

### B.1 What may be shared (PROVEN_SOURCE)

- **One storage, several threads, each with its own file handles: supported by CascLib.** Every lookup the read path
  makes is a pure read of structures built during `CascOpenStorageEx`: open by FileDataID (`CascOpenFile.cpp:399-403` ->
  `FileTree.cpp:541-556` -> `ArraySparse.h:137-161`, no allocation), open by name (`Map.h:135-164`), the CKey/EKey maps,
  `CascWowFindRootEntry` (`CascRootFile_WoW.cpp:698-725`) and `CascGetFileInfo(CascFileContentKey)`
  (`CascReadFile.cpp:964-993`). Creating and closing a handle writes only the new `TCascFile` and the storage refcount,
  which is interlocked "to make multi-threaded file opens work" (`CascOpenStorage.cpp:129-151`). Data files open lazily
  under the storage's lock (`CascReadFile.cpp:37-65`), and every archive read is a positioned `ReadFile` under that
  stream's own lock (`FileStream.cpp:161-237`): no shared file pointer. Frame decoding, the frame MD5 and Salsa20 run on
  stack state (`CascReadFile.cpp:546-666`, `Common.cpp:589-605`, `CascDecompress.cpp:18-56`); the vendored zlib has no
  lazily built tables in this build. The error code is the thread's own Win32 last error (`Common.cpp:80-100`). CascLib's
  own test runs up to 40 extraction threads, each opening and reading its own handles with `CASC_STRICT_DATA_CHECK`, on
  one storage (`ThirdParty/casclib/test/CascTest.cpp:536-579`): concurrent reads are the library's design, not an
  accident of this code. (That test opens with `CASC_OPEN_CKEY_ONCE`, so two of its threads never read the same file at
  once; when the GUI and the worker do, both only read the file's shared CKey entry -- the writes below happen only for
  entries without a content size or with a BLTE header that does not parse.)
- **Shared writes on the read path** (formally racy, never different values): `pCKeyEntry->ContentSize` when it is
  invalid (`CascReadFile.cpp:312-316`; never for files resolved through ENCODING, which every root-listed WoW file is:
  `CascOpenStorage.cpp:243-250`), `Flags |= CASC_CE_PLAIN_DATA` for a file whose BLTE header does not parse
  (`CascReadFile.cpp:366-367`), and `hs->LastFailKeyName` on a missing decryption key, which no WMV code reads
  (`CascDecrypt.cpp:996-1002`, `817-838`).
- **Storage lifetime.** `CascCloseStorage` only drops one reference (`CascOpenStorage.cpp:1628-1643`); every open file
  holds its own (`CascOpenFile.cpp:18-23`, released last in `:71-72`); the storage is destroyed, on whichever thread
  drops the last reference, only then (`CascOpenStorage.cpp:83-127, 136-151`). A thread with an open file can therefore
  finish reading it after another thread closed the storage.
- **Qt values** (`QByteArray`, `QString`, `QJsonObject`) are implicitly shared with atomic reference counts
  (`qrefcount.h:108`, `qjson_p.h:675`): a buffer read on one thread can be handed to another without a copy, if the
  first does not touch it afterwards. `QCryptographicHash` and `QElapsedTimer` are used as locals.
- **Logging** from a worker is safe once `OnInit` has added its outputs (`app.cpp:9005, 9355`, the only
  `LOGGER.addChild` calls): the file output writes each line under a `QMutex` (`LogOutputFile.cpp:66-71`).

### B.2 What is not safe, and where the design keeps it out

| Hazard (PROVEN_SOURCE) | Evidence | How the design avoids it |
|---|---|---|
| A storage closed while another thread is **inside** an API call on it: `CascOpenFile`, `CascWowFindRootEntry` and the key APIs check the handle but take no reference until `new TCascFile`, so a concurrent last `Release` frees what they read | `CascOpenFile.cpp:318` then `:200`/`:22`; `CascRootFile_WoW.cpp:704-724`; `CascOpenStorage.cpp:1628-1643` | the worker holds **its own reference** (C.2): no other thread's close can drop the last one while the worker uses the storage |
| One `TCascFile` used by two threads (no lock in `TCascFile`) | `CascCommon.h:362-403`; `CascReadFile.cpp:527-544, 919-930, 1254-1322` | every handle is opened, read and closed inside one read on one thread; none crosses threads |
| Adding a decryption key while another thread reads (unlocked append to the key chains) | `CascDecrypt.cpp:713-746` vs `695-711` | keys are added only inside `CASCFolder::setConfig`, to a storage nobody shares yet (`CASCFolder.cpp:132-134, 519-546`; no other `CascAdd*EncryptionKey` in `Source/`); a storage shared with the worker never gets a key added (checked in code, C.2) |
| Enumerating with a listfile (rebuilds the file tree) | `CascRootFile_WoW.cpp:582-663`; `FileTree.cpp:558-667` | WMV's only enumeration passes no listfile (`CASCFolder.cpp:427`) |
| `CASC_OPEN_CKEY_ONCE` (unlocked flag write) | `CascOpenFile.cpp:412-425` | never used |
| Two `CascOpenStorageEx` overlapping in one process (`.build.info` and config files opened with write access, share-read only) | `CascFiles.cpp:1527-1533, 1133-1146` | the worker opens no storage |
| A **partial** read whose disk read fails: `ReadFile_FrameCached` leaves the caller's buffer as the handle's frame cache, which `CascCloseFile` then frees (found by the lifetime verifier; not thread-related) | `CascReadFile.cpp:872-876, 887, 919-930`; `CascOpenFile.cpp:68` | the worker never asks for part of a file: one request for the whole file, as `readWholeFile` always made (a request that ends at the file's end never takes that path). Cancellation is therefore between reads, not inside one |
| wx: `wxTimer`, `Bind`, `ProcessEvent`, sockets are GUI-thread only; a handler destroyed while another thread queues to it is a use-after-free | `timer.h:41`; `event.h:3795-3798, 3820-3826`; `UnityRendererHost.cpp:353-357` | the worker posts nothing to wx but the wake of C.3's amendment (`QueueEvent` of a `wxThreadEvent`, the call wx makes for other threads, to a server that outlives the worker's notifier); the GUI thread collects results in its poll (C.4) |
| Plain GUI-thread state the current path reads: `g_clientEpoch` and `g_clientLoadDepth` (plain ints), `GAMEDIRECTORY` (`Game::replace` swaps it), `WorldMapCatalog::inspectFile` (writes `m_refusal`), `m_terrainRequest`, every `UnityIpcServer` member | `UnityAssetAccess.cpp:26-37`; `Game.cpp:25-31`; `WorldMapCatalog.cpp:301-308, 389-400`; `modelviewer.cpp:3543-3569`; `UnityIpcServer.cpp:1514-1638` | none of it is read or written by the worker -- except, amended with the wake (C.3), the server's atomic wake flag `m_wakePosted` and its thread-safe `QueueEvent`, which the server's notifier uses on the worker thread (`UnityIpcServer.cpp:1674-1677`): decisions, accounting, revalidation and answers stay on the GUI thread; the worker gets a job of plain values |

### B.3 What must stay thread-confined

- **GUI thread:** the sockets and `m_outBuf`, the wx timer, the server's queue, records, read tracker and statistics,
  `ModelViewer::m_terrainRequest` and every decision about a request, the catalogue, `GAMEDIRECTORY` and every
  `GameFile`, the client epoch and loading state, storage open, key add, enumeration and close.
- **Worker:** the file handles it opens, the buffer of the read in progress, its own reference to the storage.
- **Handed over, never shared:** a job (plain values: ticket, FileDataID, byte limit, epoch, storage generation) to the
  worker; a result (the inspection, the bytes, timings) back. Each is touched by one thread at a time, through one
  mutex-protected exchange -- except the job's cancel flag, a shared atomic the GUI sets and the worker reads
  (`MapAssetWorker.h:66`; 9.2).

### B.4 The host today

- One thread reaches the storage: the GUI thread. The only other threads are one `QThread` per plugin DLL, which never
  touch game data (`Plugin.cpp:94-99, 127-138`); SQLite's sorter threads are off (`sqlite3.c:8499-8500`).
- The client is replaced on the GUI thread in three places, and the old storage goes **before** the epoch moves:
  `CASCFolder::setConfig` on a reused folder (`CascCloseStorage` inside it, `CASCFolder.cpp:116-122`), `closeStorage` on
  a fresh folder (`modelviewer.cpp:4785-4787`) and in `LoadWoWFromMpq` (`4392-4394`); then `ResetClientState` raises
  the epoch (`4531`). Interactive loads pump the event loop (`ClientLoadingPanel.cpp:212-225`), so IPC polls run in the
  middle of a replacement and are refused only by `ClientLoadGuard` (`UnityAssetAccess.cpp:23-37`).
- The harness drives the server synchronously: it calls `poll()` itself (`app.cpp:3490-3494`; no timer events inside
  `OnInit`), and several in-process checks call `ServeMapAsset` and the queue directly and expect the answer at once.
- The player matches each answer by request id within the live load (and the echoed load), dispatches by the
  request's own role, tolerates any order, drops late and unknown answers and counts them, and never retries: a refused
  WDT or root fails the load, a refused texture is a permanent checker (`WmvTerrainSession.cs:384-462, 544-583,
  936-959`).

## C. Storage ownership and lifetime plan

### C.1 Options measured

1. **The worker opens a storage of its own** (complete confinement). Cost (A.4): about **600 MB** more private memory
   while it is open (894 MB peak during the open), **2.4 s** to open per client, and every `CascOpenStorageEx` in the
   process must then be serialized with the GUI's (B.2, the row on two `CascOpenStorageEx` overlapping), which can make
   the GUI's client load wait for the worker's open.
2. **The worker reads the client's storage through its own reference** (shared storage, confined handles). Cost: none
   in memory or open time. Its safety rests on B.1: concurrent reads with separate handles are CascLib's tested design;
   the lifetime hazard of B.2's first row is closed by the worker's own reference; every other hazard is kept out by
   construction.

**Chosen: option 2.** The audit's rule "give the storage one owning thread, not locks around it" (`00-audit.md` 2.3)
was written before CascLib's concurrency was audited; the audit above shows CascLib already is a multi-reader storage
with per-handle state, and the measured price of a second storage (600 MB and 2.4 s per client) buys no safety that the
reference and the confinement rules do not give. No lock is added around CascLib calls.

### C.2 Ownership

- **References.** The GUI's `CASCFolder` owns one reference to each storage it opens, as today. The worker owns at most
  one: its **lease**, a second reference to the client's storage plus the identity it was granted under (client epoch,
  storage generation). A new project addition to the vendored CascLib, `CascWowShareStorage(hStorage)`, takes the
  reference (`AddRef`, next to the other WMV additions in `CascRootFile_WoW.cpp`); it is released with
  `CascCloseStorage`, exactly once: by the worker once it has taken the lease over (on a later grant, a drop, or when
  its thread ends). *(Corrected after the post-commit review, 2026-10-15:)* a grant the worker never took over -- still
  queued when the worker stops, or given after it stopped -- is released by `stop()` or `grantLease` on the calling
  (GUI) thread instead (`MapAssetWorker.cpp:171`, `:61`); either way once.
- **Granting.** On the GUI thread, when the first read of a client epoch is dispatched, from the storage the load was
  sent under (the decision has already checked: no client loading, the catalogue is the loaded client's, its storage
  generation is the load's). The GUI thread holds its own reference then, so the storage is alive while the second is
  taken.
- **Keys and enumeration.** `CASCFolder` remembers that its storage was shared and refuses (logs and skips) a key add
  after that; keys are added only in `setConfig`, before any read (B.2).
- **Revoking.** When the client is replaced (`ResetClientState`), and when the viewer shuts down, the GUI tells the
  worker to drop the lease (at shutdown: `stop()`); the worker releases the reference it took over **on its own
  thread** once the read in progress has ended (a read under way always finishes, C.3) and the jobs queued before the
  drop are done or skipped as cancelled -- a grant still queued at `stop()` is released by `stop()` on the GUI thread
  (above). On a client replacement the GUI never waits for it; at shutdown it joins the worker (D). If the GUI's close
  was the earlier one, the storage is destroyed then, on the worker -- or, for a grant the worker never took over, in
  `stop()` on the GUI thread (CascLib has no thread affinity, B.1).
- **What this guarantees.** A storage cannot be destroyed while the worker uses it: the reference it reads through is
  dropped only by the worker, between reads. A read in progress when the client is replaced finishes (reads are whole,
  B.2) on the storage it started on -- the old one, still alive -- and its result is then discarded (D).

### C.3 The worker

- One thread (`MapAssetWorker`), started on first use (after `OnInit` has added the logger's outputs, B.1; in the
  `-terraintile` harness, still inside `OnInit`), owned by `ModelViewer` and shared with the
  server; jobs are served one at a time in the order they were dispatched (FIFO): no concurrent map reads.
- A job carries plain values: ticket, FileDataID, byte limit, the epoch and storage generation it was decided under,
  and test hooks. The worker reads it with the same verified read as today -- root lookup (listed, local), open with
  `CASC_STRICT_DATA_CHECK`, size and limit, read, content key, MD5 -- through a version of
  `WorldMapCatalog::inspectReference` and `CASCFolder::readWholeFile` that takes the storage handle as an argument
  instead of reading `GAMEDIRECTORY` (the GUI's own calls use the same code with the GUI's handle). Every state, detail
  and error text stays the one code path produces today.
- Before reading, the worker compares the job's epoch and generation with its lease: a job for another storage is not
  read ("the client's storage changed while the request waited").
- A read is whole, exactly as today's: one `CascReadFile` request for the whole file (B.2, the partial-read row: a
  partial request is exposed to a CascLib defect). A job is cancellable until its read begins; a read under way finishes
  -- at most one file's read, about 35 ms for the largest terrain texture (A.3) -- and its result is then discarded (D).
- Results go back through a mutex-protected list; the worker calls nothing of the GUI's and touches no GUI-owned
  object (as first designed it also posted no wx event; the amendment below adds one, the wake).
- **Amendment after the first measurement (section 13.3):** results collected only at the next poll added one poll
  interval to every request, which made ordinary tiles slower than before. The worker now has one call out: a result
  notifier, run on the worker under its own lock after each result. The IPC server's notifier posts the server a
  `wxThreadEvent` with `wxEvtHandler::QueueEvent` (wx's mechanism for other threads; at most one wake waiting, an
  atomic flag); the GUI thread handles it with a `poll()`. The worker touches no GUI-owned object except through this
  notifier, which runs on the worker thread and touches the server only through its atomic wake flag and its
  thread-safe `QueueEvent` (`UnityIpcServer.cpp:1674-1677`; D). Wakes can
  follow one another within one pass of the event queue (a poll's answers bring the next results): a run of wake polls
  (each starting within 1 ms of the previous one's end) spends at most about 8 ms of the GUI thread
  (`MAP_ASSET_WAKE_BURST_MS`); the wakes after that leave their results to the timer's poll, so the window gets its turn.

### C.4 Transfer to the GUI/IPC owner

- **Dispatch** (GUI, in `poll()`): the queue's front request is decided and accounted exactly as today
  (`decideMapAssetRequest`, `noteMapAssetRead`). A request decided "refused" or "stale" is answered at once (it reads
  nothing). A read becomes a job; the request waits in the server's in-flight table with its ticket, the connection it
  came over, and its timings.
- **Completion** (GUI, in `poll()`): results are taken in order; each is checked again **on the GUI thread** -- the
  connection is the one the request came over, the client is not loading, the epoch, the load and the storage
  generation are still the request's and the load has not ended -- and is then finished exactly as today: kind check,
  content-key presence, `noteMapAssetResult`, the answer frame, the record. A result that fails the check is answered
  "stale" with the reasons (or dropped unanswered when its connection is gone), and its bytes are freed.
- Completions are collected in `poll()`, so the harness (which calls `poll()` itself) and the GUI's 20 ms timer both
  drive them; no `CallAfter` is involved, and the only wx event besides the timer's is the wake of the amendment
  below.
- **Amendment (C.3):** a poll also runs when a result wakes it -- the wake is the only wx event involved besides the
  timer's. The worker's notifier runs after each result the worker makes and posts a wake unless one is already waiting
  (the results `stop()` makes run no notifier); the handler posts one more only when its poll took results and more are
  ready (`UnityIpcServer.cpp:1707-1708`). It is handled on the GUI thread like the timer's. The harness's pump waits for
  a result (up to its 10 ms) and handles the wake in its `Yield`, as the viewer's event loop would. The server clears
  the notifier, under the worker's lock, before it is destroyed; the viewer stops the worker before it destroys the
  server.

## D. Client-switch cancellation plan

- **Cancelling.** A job has a cancel flag. The worker checks it before the read begins (and ends a test delay on it); a
  cancelled job is not read and returns no bytes; a read already under way finishes whole (C.3). The GUI cancels, every
  poll, each read the viewer no longer wants (its load ended: the player's report, a newer `loadTerrainTile`,
  `clearTerrain`, a model load; another epoch; a catalogue that is not the loaded client's), every read when the client
  is replaced, and every read of a connection when the connection ends.
- **While a client is being loaded** (its storage and catalogue half replaced) the reads in flight are neither checked nor
  answered: their results wait with the worker until the load is over -- answered stale when it replaced the client (the
  epoch moved on), as read when it failed and the client stayed. New requests during the load are refused, as today.
- **Answering.** Every request is answered once: refused or stale at dispatch; or, after its result, available /
  unreadable / not in storage ... as today, or "stale" when the completion check fails (C.4). Requests of a connection
  that ended are never answered (the player that sent them is gone; a new one numbers its requests from `map1` again).
- **Never into the new client's stream.** Three independent guards: (1) the worker does not read a job decided under
  another epoch or generation than its lease (C.3); (2) the completion check on the GUI compares the request's epoch,
  load and generation with the current ones before any byte is queued; (3) the connection serial of the request must be
  the current connection's. The first relies on the worker, the second and third on the GUI thread alone, so a late
  result can neither reach a new client's load nor a new player.
- **Storage closed during a read.** Not possible for the storage the worker reads: it holds its own reference (C.2).
  The completion check is therefore not the only guard (specification section 5): the read itself is on live memory.
  (An open CascLib file holds a reference of its own too; the lease is what keeps the storage alive between reads -- a
  job dequeued, no file open yet -- and the test of section 11 holds a read exactly there across a client switch.)
- **Shutdown.** `ModelViewer`'s destructor (before any storage or folder is torn down) stops the worker: every job not
  begun gets a "stopped" result unread, the read under way finishes, the worker releases the lease it took over and
  exits (a grant still queued is released by `stop()`, `MapAssetWorker.cpp:171`), and the destructor joins it. The
  server's `stop()` drops its connection's reads in flight. *(Corrected after the post-commit review, 2026-10-15:)* the
  worker holds one pointer into the server and none into the viewer: the result notifier of C.3's amendment captures the
  server's `this` (`UnityIpcServer.cpp:1674-1677`). That is safe because the notifier runs only under the worker's lock
  (`MapAssetWorker.cpp:317`, `:334-335`), the server clears it under the same lock as the first statement of its
  destructor (`UnityIpcServer.cpp:87-88`; `MapAssetWorker.cpp:188-192`), and the viewer stops and joins the worker
  before it destroys the server (`modelviewer.cpp:1865`, `:1962`); wakes already queued go with the server's
  `wxEvtHandler`. The wake can, however, re-enter the viewer during its destructor: 18.1, a prerequisite for Stage 05.
- **The read tracker** (`MapAssetReadTracker`) and the per-load bookkeeping stay GUI-only and unchanged: noted at dispatch
  (the read is accounted before it happens, as today) and at completion.

## E. Queue and memory bounds

| Stage | Bound | Held |
|---|---|---|
| Requests waiting in the server's queue | 16 (`MAP_ASSET_QUEUE_MAX`, unchanged); one more is refused | the request's JSON |
| Jobs in flight (dispatched, not yet completed on the GUI) | **2** (`MAP_ASSET_READS_IN_FLIGHT`): the worker reads one while the next waits, so it is not idle between polls | per job at most its byte limit: 4 MiB (map files) or 8 MiB (textures) |
| Bytes read and held by the worker side (buffers of jobs in flight, results not yet taken) | at most 2 x 8 MiB = **16 MiB** | |
| Results taken per poll (GUI time) | the existing per-poll budget, now on the completion side: until 2 MiB of map payload were queued, or one file over 1 MiB | |
| Unsent bytes in the send buffer | no new job is dispatched while more than **16 MiB** are unsent (`MAP_ASSET_SEND_HIGH_WATER`): the player not reading stops the reads. *(Qualified after the post-commit review:)* this gates every dispatched map request, whatever the client: map serving adds at most about 32 MiB (16 MiB plus the two reads in flight, 15; about 37 MiB to a client that has not announced protocol 10 or later, whose answers are base64). A `getMapAsset` refused on arrival (only with 16 already waiting, which the shipped player reaches only if map reads stall across many superseding loads) and the inherited non-map messages -- asset answers, the viewer's own pushed messages -- are not gated (15, 18.2) | |
| Per load | unchanged: 4 MiB per map file, 8 MiB per texture, 128 MiB of texture bytes, 128 texture ids, 3 + 128 files | |

- **The per-load texture byte budget stays exact with two reads in flight.** Today a texture's limit is
  `min(8 MiB, 128 MiB - bytes read)`, decided after every earlier read. With reads in flight, each dispatched texture
  **reserves** its limit until it completes, and the next one gets `min(8 MiB, 128 MiB - bytes read - bytes reserved)`.
  The budget can never be exceeded; the only difference from today is near its end (a load within 16 MiB of 128 MiB of
  texture bytes, which no real tile is: the largest is 90.9 MB), where a texture may be refused that a later shortfall
  would have allowed. The refusal names both figures. Tested in process (G).
- **Accounting** (GUI thread, reported in every run's out.json): jobs dispatched, completed, cancelled, discarded with
  their connection, answered stale at completion; the most in flight; the most bytes held by the worker side; the most
  unsent bytes; worker busy time.

## F. Compatibility plan

- **Protocol: unchanged (13).** Same messages, fields, roles (`wdt`, `rootAdt`, `tex0Adt`, `texture`), states and
  details. `readMs` stays the storage read (now timed on the worker). No player change.
- **Protocol-12 players** are offered no textures and ask only for the WDT and root: the same decisions, through the
  worker. Older players draw no terrain: no map requests.
- **Kept:** the separate map request namespace (`map<n>`, `OnMapAssetResponse`), the bounded queue, late-response
  isolation (answers of an ended load "stale", dropped by the player), one report per load, old-client invalidation
  (read tracker reset, `clearTerrain`), the content-key contract and every refusal.
- **Order of answers.** A refusal decided at dispatch can now overtake reads still in flight. The player matches answers
  by request id and tolerates any order (B.4; the host already answers a full-queue refusal ahead of the queue).
- **The harness.** In-process checks that called the synchronous path get the same answers from the worker path,
  pumped until the result arrives; the server's records gain the worker's timings (additive in out.json).
- **Model and WMO assets** (`getAsset`, `getAssetByFileDataID`) stay on the GUI thread, unchanged: they go through
  `GameFile` objects that only the GUI may touch (B.3).
- **Out of scope, unchanged:** the WDT read of `ResolveTerrainTile` before a load is sent (0.5 ms for map 2923's
  163,908-byte WDT, at most about 1 ms for the largest WDT, 294,988 bytes) stays on the GUI thread: it decides whether
  the load is sent at all and records the result in the catalogue.

## G. Failure scenarios (each to be tested; deterministic hooks where no real case exists)

| Scenario | How it is produced | Expected |
|---|---|---|
| Slow root ADT read; slow 5.6 MB texture read | worker hooks: hold the next read of a role with its file open (or before it is opened), release later; a delay before every read | GUI polls stay short; answers correct; the reads behind a held one wait for it (one worker), nothing on the GUI thread does |
| File not in client storage; not installed locally | real: Era map 169 25_31 texture 249604; Retail map 2736 32_29 tex0 | the same state and detail as today |
| Corrupt content key | worker hook: flip one byte of the next read before the MD5 | "unreadable", "damaged on this computer ..."; no bytes sent |
| Encrypted | real if a local file still lacks its key (Classic Beta Map.db2 sections), read in process through the worker; otherwise SKIP | "unreadable", "encrypted with a key this computer does not have" |
| Too large | in process: a job with a 1,000-byte limit | "unreadable", size and limit named, nothing read |
| Queue full | the existing in-process queue check, and 16 waiting while the worker is held | "refused", queue unchanged |
| Timeout | the existing `timeout` step, and with a read held in the worker | the load fails once; the late result is answered "stale" and dropped by the player |
| A genuine late file answer | a read held in the worker; the player's timeout ends the load and its report is held at the host; the read is let go | answered "available", a binary frame, to a request the player retired: it reads past the payload and counts it late; the stream stays intact |
| Superseded load during a read | a held texture read, then a new load | the new load's reads wait behind the held one (one worker); the old read, let go, is answered "stale"; the new load complete |
| Client replaced during a read | a read held in the worker with its file open, and one held before its file is opened (only the worker's own reference keeps the storage then), across Retail -> Classic Beta / Era / MoP -> Retail, then released | the read finishes on the old storage (every byte checked against the content key: "available", the memory was intact), is answered "stale", never sent as data; the worker then lets go of that storage; the new client's tile is built from its own reads |
| Player disconnect and reconnect during a read | a held read, then the player restarted | the result is dropped unanswered; the new player starts clean |
| Shutdown during a read | a read held in the worker (its file open) when the run ends | the hold lets go, the read finishes, the worker joins and releases its storage reference; the process exits normally |
| Repeated failed and repeated successful loads | the existing `failrepeat` and `repeat` steps | counts flat, no growth of the worker's held bytes |
| Rapid Retail -> Classic -> Retail | the existing switch steps, with reads in flight | each client's tile from its own storage; no answer crosses |

## H. Expected measurable improvement

From A.3's measured parts, not a projection of percentages:

- **GUI thread per 5.6 MB texture:** what stays is the decision (0.005 ms), the header (0.02), the copy into the send
  buffer (0.28), `send()` (1.36) and the log line (0.10): about **1.8 ms median**, against 33 ms. The worker's read and
  MD5 (about 31 ms) leave the GUI thread entirely.
- **Longest GUI stall caused by map serving:** bounded by one poll's completions (the per-poll budget: one file over 1
  MiB, or 2 MiB of smaller ones) -- expected a few ms, against 37-48 ms. (Amended with the wake, C.3: wake polls can
  follow one another while no input or paint waits; what was measured is in section 14.)
- **The 20 ms check** keeps judging the GUI-thread time per request (handler), now separated from the worker time; the
  worker's own read time is reported, not hidden.
- **Whole tile:** the worker reads back to back while the GUI answers, so the texture fetch is bounded below by the sum
  of the worker's reads (about 0.51 s for map 2923 32_24: 12 x 31.2 + 16 x 8.2 + 4 x 2.0 ms) instead of reads plus the
  poll waits between them (1.06 s today); the player's 4 outstanding requests and its own preparation also limit it.
  Measured, not promised: section 13.
- **Memory:** at most 16 MiB more held during a load (E); no second storage.

## 9. Implementation

Citations in this and the following sections are to the branch's commit.

### 9.1 What changed

| File | Change |
|---|---|
| `ThirdParty/casclib/src/CascRootFile_WoW.cpp:728`, `CascLib.h:460` | `CascWowShareStorage`: one more reference to an open storage (`AddRef`), released with `CascCloseStorage`; beside the project's other CascLib additions |
| `Source/games/wow/CASCFolder.h/.cpp` | `Storage` (handle, locale, nameless count), `storage()` (borrowed), `shareStorage()` (an owned reference, `:197`), `releaseStorage()` (`:212`); `rootEntryIn`/`rootEntryByPathIn` and `readWholeFileFrom` (`:268`), the read on a named storage that the folder's own `rootEntry`/`readWholeFile` now call; `ReadTimings` (each step timed, after its error code was read); a file-handle guard (`:300`) that closes the CascLib handle on every way out; `ReadHooks` for the test holds and the corrupt byte; no key added to a shared storage (`:620`; `m_storageShared` reset whenever another storage is held, `:123`, `:179`) |
| `Source/games/wow/WoWFolder.h` | pass-throughs: `storage()`, `shareStorage()`, the timings |
| `Source/wowmodelviewer/CMakeLists.txt` | `MapAssetWorker.cpp` and `MapAssetWorker.h` added to the viewer's sources |
| `Source/games/wow/WorldMapCatalog.h/.cpp` | `inspectInStorage` (`:410`): `inspectFile`'s inspection on a named storage; `inspectReference` (`:402`) now calls it with the active folder's storage: one code path, the same states, details and checks; the inspection carries the root lookup's and the read's timings |
| `Source/wowmodelviewer/MapAssetWorker.h/.cpp` (new) | the worker: one thread (`run`, `:280`), a FIFO of jobs and lease changes, results back through one mutex; a lease the worker took over released only on the worker (`releaseLease`, `:267`), a grant it never took over by `stop()` (`:171`) or, after a stop, by `grantLease` (`:61`); `stop()` (`:153`); every job one result; accounting (`Stats`); test hooks (hold with the file open or before it is opened, a byte changed before the MD5, a delay); a `std::bad_alloc` in the read is answered, never thrown out of the thread (`:407`); the result notifier, run under the worker's lock after each result (`setResultNotifier`, `:188`; called at `:335`), and the count of results made |
| `Source/wowmodelviewer/UnityIpcServer.h/.cpp` | `serveMapAssetQueue` (`:1731`): (1) the viewer's check of every read in flight (`:1744`), (2) results within the poll budget (`:1764`), (3) dispatch within `MAP_ASSET_READS_IN_FLIGHT` and `MAP_ASSET_SEND_HIGH_WATER` (`:1807`); waits while a client loads (`:1742`); `answerMapAsset` (`:1910`) records `handlerMs` as the GUI thread's time (dispatch + answer, `:2009`), the worker's outcome and every step's time; reads of an ended connection orphaned (`dropHeldMapAssets`, `:1559`) and dropped when taken back (`:1781`); a connection serial (`:279`); `cancelMapReads` (`:1606`); THE WAKE: `wakeOnMapResults` (`:1667`) installs the notifier that posts the server a `wxThreadEvent`, `onMapResultWake` polls -- unless its run of wakes has already spent `MAP_ASSET_WAKE_BURST_MS` (8 ms): then the timer's poll takes the results -- and posts once more while results it could not take are ready; the destructor (`:88`) and `setMapAssetWorker` (`:1659`) clear the notifier first, `waitForMapResult` (`:1711`) for the harness's pump; `MapReadStats` (with `wakes`, `wakesDeferred`, `takenOnWake`, `wakeBurstMsMax`); `poll()` (`:214`) serves with or without a client and times its parts; test hooks (`testHoldNextMapRead`, `testCorruptNextMapRead`, `testDelayMapReads`, `testHoldTerrainReport`, `testFakeUnsentBytes`) |
| `Source/wowmodelviewer/modelviewer.h/.cpp` | `DispatchMapAsset` (`:3806`): the unchanged decision and accounting, a texture's limit reserved (`:3843`), the lease granted (`EnsureMapAssetLease`, `:3852`); `CompleteMapAsset` (`:3924`): checked again (`MapAssetStaleReason`, `:3782`), the reservation released (`:3933`), the old completion (kind check, content key, `noteMapAssetResult`); `MapAssetWait` (`:3777`); `decideMapAssetRequest` counts reserved bytes (`:3560`); `ResetClientState` cancels the reads and drops the lease (`:4758-4760`); the worker made with the viewer (`:272`), its results waking the viewer's server (`wakeOnMapResults`, `:2341`), and stopped first in its destructor (`:1865`); test helpers `ServeMapAssetAndWait`, `TestReadThroughWorker`, `TestHoldReadForShutdown` |
| `Source/wowmodelviewer/app.cpp` | the `-terraintile` harness: the queue check on the worker (`checkMapAssetQueue`, `:6248`), the worker's in-process reads (`checkMapAssetWorkerReads`, `:6735`), new steps `readtimeout`/`readsupersede`/`readclear` (`:8517`), `readswitch` (`:8635`, `:start` holds before the open), `readreconnect` (`:8782`), `readcorrupt` (`:8910`), `readslow` (`:8958`), `readlate` (`:9000`), `readshutdown` (`:9064`); end-of-run worker checks (`:9354`); poll and pump timing (`:3493`, `:7434`); the pump's 10 ms between polls ends early for each result, whose wake it handles in a `Yield`, and then goes on (n slots still last at least n x 10 ms); an in-process check of the wake (coalescing, handling on the GUI thread, the notifier cleared with its server); out.json `mapAssetWorker`, record `readOutcome`/`readState`/`timings` |
| `Tools/world-audit/check_terrain_run.py` | `check_serving` (`:178`): the worker's time reported beside the GUI's, every file served read by the worker (`:221`), no poll over 20 ms of map serving (`:226`), every request answered once (`:242`) |
| `scripts/tests/casc-whole-read/regression.cpp` | a stub for `CascWowShareStorage` (the harness links `CASCFolder.cpp` against stubs) |

No shader, terrain material, geometry, protocol message, player script or player asset is changed.

### 9.2 Thread ownership

| State | Owner | Crosses threads how |
|---|---|---|
| sockets, `m_outBuf`, wx timer, server queue, records, read tracker, `MapReadStats`, in-flight table | GUI thread (server) | never |
| `m_terrainRequest`, decisions, reservations, epoch, catalogue, `GAMEDIRECTORY`, every `GameFile` | GUI thread (viewer) | never |
| the GUI's storage reference (`CASCFolder::hStorage`), key adds, enumeration, storage open/close | GUI thread | never |
| the worker's lease (its own storage reference) | worker | granted by value through the mutex (taken on the GUI thread while the GUI holds its own); once taken over, released only by the worker; a grant still queued at `stop()` is released by `stop()`, and one given after a stop by `grantLease`, both on the GUI thread (`MapAssetWorker.cpp:171`, `:61`) |
| every CascLib file handle | the thread that opened it | never: opened, read and closed inside one read |
| a job (ticket, FileDataID, limit, epoch, generation, role, cancel flag, test flags) | GUI, then worker | handed over under the worker's mutex; the cancel flag is a shared atomic |
| a result (inspection, bytes, timings) | worker, then GUI | handed back under the mutex; the bytes are a `QByteArray` moved, not copied |
| the storage's lookup structures | shared, read only after open | the GUI's and the worker's reads (B.1) |
| the result notifier | set and cleared by the GUI (under the worker's lock); run by the worker (under the same lock) | the server's notifier only posts a `wxThreadEvent` to the server (`QueueEvent`, made for other threads) |
| the wake flag (`m_wakePosted`) | shared atomic | raised by the worker's notifier when it posts or by the GUI's handler when it re-posts, lowered by the GUI's handler before it polls |

### 9.3 One request, step by step

1. `pollReceive` -> `handleGetMapAsset` (`UnityIpcServer.cpp:1623`): held by a test hook, refused at once past 16
   waiting, or queued.
2. `serveMapAssetQueue` step (3): with fewer than 2 reads in flight and at most 16 MiB unsent, the front request goes
   to `DispatchMapAsset`: the decision (refused or stale: answered now), else accounted, a texture's limit reserved, the
   lease granted if the worker holds none for this epoch and storage, and a job submitted.
3. The worker: cancelled? no lease? another epoch or storage? -- not read; otherwise `inspectInStorage` on the lease's
   storage: root lookup, open (`CASC_STRICT_DATA_CHECK`), size and limit, one whole read, content key, MD5.
4. The worker's result posts the server a wake; the GUI thread handles it with a poll (or a timer poll comes first).
   Step (1): if the viewer no longer wants it, cancelled; step (2): its result taken back within the poll's budget; dropped if its connection ended; else `CompleteMapAsset`: checked again (load, epoch, catalogue), the
   reservation released, then the old completion; `answerMapAsset` queues the frame and records the answer.
5. A client load in progress: steps (1) and (2) wait; the client replaced: `ResetClientState` cancels every read and
   drops the lease; the worker releases its reference after the read it is on.

## 10. Audit and review record

### 10.1 The gate audit (section B)

Five read-only auditors, then an independent verifier for each of the three safety-critical areas, told to refute:

| Area | Claims | Confirmed as stated | Corrected | Refuted | What the corrections and the verifier's "missed" items changed |
|---|---|---|---|---|---|
| CascLib concurrency on one storage | 29 | 25 | 4 | 0 | the list of WMV's CascLib calls completed; a data file's lock can be held for a whole file's read (a BLTE file without a chunk table is one frame): the GUI's own reads of the same archive can wait for one frame; `TotalFiles` and the socket cache are lazy shared state (never used by WMV); every `hStorage` API, not only `CascOpenFile`, reads the storage without a reference -> **the worker holds its own reference** |
| CascLib lifetimes, second storage | 19 | 16 | 3 | 0 | `CascCloseStorage` tears the storage down at once when it holds the last reference; **an `AddRef` racing the last `Release` revives a dying storage** (double free) -> the lease is taken on the GUI thread while the GUI still holds its reference, never from zero; the locale and the install state belong to a storage, so a second storage could differ from the GUI's -> the worker reads the GUI's own storage |
| Host threads and lifetimes | 42 | 36 | 6 | 0 | an interactive load can run IPC polls (pumps) and the headless one can run Qt's event loop (network refreshes) -> results wait while a client loads; terrain map reads are reached only from the harness at this commit (no UI entry yet: interactively every `getMapAsset` is "stale" without a read); `bad_alloc` escaping a `std::thread` ends the process -> caught; a send error inside the serving loop can drop the client mid-loop -> orphaning and the connection serial; `Game::instance`/`Logger::instance` are created lazily without a lock -> the worker calls neither: it logs nothing and never touches `GAMEDIRECTORY` (`MapAssetWorker.cpp` has no `LOG_` and no `Game` use) |

The fourth and fifth auditors (Qt, wx and the logger; the player) were not adversarially verified; their claims used
here (Qt's atomic reference counts, the logger's mutex, the player's tolerance of any answer order and of late answers)
were re-read at the cited lines while designing, and the player's are exercised by the tests in section 11.

### 10.2 The implementation review

One review of the finished code, five reviewers (worker, server, viewer, CASC, tests), each finding then checked by a
verifier where one ran (worker, viewer, CASC: 8 confirmed, 2 downgraded to test-only or documentation-only, 0 refuted):

| Finding (merged where several reviewers saw the same thing) | Severity | Fix |
|---|---|---|
| No step tested a genuine late **file** answer any more (the timeout step now ends stale) | major (tests) | `readlate`: the read held in the worker, the player's timeout ends the load, the host holds the report, the read is let go and answered "available" (`app.cpp:9000`) |
| `checkReadThenStale` accepted any read result, so a broken lease would still pass the switch tests | major (tests) | it requires the read to come back "available" with its content key checked (`app.cpp:8122`); the switch steps also hold a read **before** its file is opened (`:start`), where only the worker's own reference keeps the storage |
| A test hold released (or `stop()`) before the worker reached its hold point was lost, stalling the worker up to 60 s | minor (test only) | release serials: a release given at any time after the job was submitted lets its hold go; `stop()` releases every hold |
| `bad_alloc` in the read leaked the CascLib file handle and so the storage | minor (OOM only) | an RAII file handle closes it on every way out (`CASCFolder.cpp:300`); the worker answers "failed" (`MapAssetWorker.cpp:407`) |
| A cancelled job's test delay did not wake | minor (test only) | `wake()` |
| The design described 1 MiB chunked, cancellable reads; the code reads whole files | minor (doc) | the design corrected: whole reads, because a partial read whose disk read fails corrupts the heap (B.2); cancellation is between reads |
| A client load in progress cancelled reads in flight for a condition that passes | minor | results wait while a client loads (`UnityIpcServer.cpp:1742`); only a replaced client cancels |
| The texture reservation, the send high water, queue full with reads in flight, stop during a held read: not tested | minor (tests) | in-process checks for each (`app.cpp:6248` onwards) |
| `stoppedForBudget` set when nothing waited; `recordMs` could exceed `serveMs`; a queue-full refusal's `handlerMs` came from an unstarted timer | minor | `resultReady()`; `recordMs` as a delta; the refusal timed |
| The 20 ms per-poll check included the harness's own time and was a single maximum | minor (tests) | `mapServingMs` excludes the harness's record keeping; the checker also requires every request answered once (`check_terrain_run.py:226, 242`) |
| `testReleaseHeldMapAssets` bypassed the queue bound; hooks survived a connection end | minor (test only) | released requests go through the queue; hooks disarmed when the connection ends |
| Checks that could not fail (readslow's tally, accounting identities, "read by the worker") | minor (tests) | readslow asserts every read took the delay and no poll served over 20 ms; the checker compares the worker's reads with the files served |

All 25 findings were fixed before the final battery (section 11); none was in CascLib's use or in the lease.

### 10.3 The wake (added after the first measurements)

Section 13.3 shows why it was added. One independent reviewer, told to refute it, read the change (worker notifier,
server wake, harness pump) and the wx headers it relies on (`wx/event.h:3820-3826`: `QueueEvent` may be called from
another thread). No blocker or major finding; locking (the notifier runs under the worker's lock, wx releases its
pending-event locks before calling a handler), lifetime (the notifier is cleared under that lock before the server goes;
the viewer stops the worker before it destroys the server; queued wakes go with the `wxEvtHandler`), re-entrancy, lost
wakes and event storms (one wake waiting at most; a re-post only after a poll that took results) were confirmed, and
re-entrancy was judged safe while the viewer runs. *(Corrected after the post-commit review, 2026-10-15:)* this review
missed one re-entrancy: a wake handled inside `~ModelViewer`, during `UnityRendererHost::shutdown`'s message pump
(18.1). Three minor findings, all fixed before the final battery:

| Finding | Fix |
|---|---|
| Wake polls posted during one pass of the event queue run back to back: a burst of answers gave the window no turn | a run of wake polls (each within 1 ms of the last one's end) spends at most about 8 ms of the GUI thread; later wakes in the run leave their results to the timer's poll (`wakesDeferred`; `wakeBurstMsMax` reports the longest run). A first fix -- defer a wake while `GetQueueStatus(QS_INPUT \| QS_PAINT)` reports input or paint waiting -- was measured and dropped: on the harness's hidden desktop it deferred nearly every wake (`W_mem_2923`: 1,086 of 1,086; outputs kept in `final_qsdefer`), which would have disabled the wake |
| Harness only: the pump spun without sleeping when the server had stopped listening with reads in flight (no serving poll updated what it had seen) | a server not listening waits plainly; a wait that saw new results marks them seen |
| Harness only: results cut the pump's 10 ms slots short, so checks that something does *not* happen within n slots got shorter windows | each slot runs its full 10 ms, handling every wake within it |

The reviewer also noted that inside the harness a client switch's loading yields can now run a wake poll (a request
decided there is answered "refused: a client is being loaded", as in the viewer); every switch step passed (section
11). An in-process check now covers the wake itself (one wake for three results, handled on the GUI thread, the
notifier cleared with its server).

## 11. Cross-client and failure tests (final build)

All runs on the final build (`wowmodelviewer.exe` SHA-256 `2C968C26...`, `wow.dll` `8D483890...`, `core.dll` `CA0F19BB...`) and the stage 04 player, one viewer at a time on
a hidden desktop, no game process running before or after each (checked). Outputs, every run's out.json, host log,
player log and checker report: `F:\wmv-build-fix\w04b\final\{w,h}\`. The harness's own exit code is 255 in every run,
release build included (its convention); the result is the `RESULT` line and the checker's.

### 11.1 The new worker battery (`final\w`)

| Run | Client, tile | Harness | Checker | Worker at exit (its log line), storage references taken / released |
|---|---|---|---|---|
| `W_reads_r3049` | Retail 0 30_49: `readtimeout`, `readlate`, `readsupersede`, `readclear`, `readcorrupt` for each role, `readreconnect`, in-process queue, worker and wake checks | PASS 520/0/1 | PASS 71/0/18 | 347 given, 338 read, 0 left; 1 / 1 |
| `W_heavy_2923` | Retail 2923 32_24: the same on the heavy tile, `readslow:60`, `repeat:3` | PASS 228/0/1 | PASS 31/0/9 | 312 given, 307 read, 0 left; 1 / 1 |
| `W_switch` | Retail -> Classic Beta -> Retail -> Era -> Retail -> MoP Classic -> Retail -> Era -> Retail, a read held across each switch, then `readshutdown` | PASS 356/0/1 | PASS 67/0/8 | 156 given, 150 read, 0 left; **9 / 9** |
| `W_beta_inproc` | Classic Beta 0 32_48: in-process worker checks (the encrypted case is real here), `readcorrupt` | PASS 136/0/1 | PASS 22/0/4 | 41 given, 39 read, 0 left; 1 / 1 |
| `W_mem_2923` | Retail 2923 32_24, 31 loads | PASS 220/0/1 | PASS 132/0/32 | 1,095 given, 1,093 read, 0 left; 1 / 1 |
| `W_mem_r3049` | Retail 0 30_49, 101 loads | PASS 430/0/1 | PASS 323/0/102 | 1,626 given, 1,624 read, 0 left; 1 / 1 |

Every job given was answered: the ones not read were cancelled before their read began (the viewer no longer wanted
them: 1-8 per run) or are the in-process check's job decided under another storage (1 per run); no read failed, none
was "stopped". The worker's held bytes peaked at 5,593,604 (one 5.6 MB texture), against the 16 MiB bound; at most 2
jobs were ever waiting in it, at most 2 reads in flight, and no answer ever waited unsent (`unsentBytesMax` 0). "Taken / released" are the worker's own storage references: one per
client used, every one released by the time the thread ended.

### 11.2 Failure scenarios (section G), as run

| Scenario | Evidence (run, step) | Result |
|---|---|---|
| Client replaced during a read, its file **open** | `W_switch` steps 1, 3, 6 (texture Retail -> Beta, WDT Retail -> Era, tex0 MoP -> Retail) | **PASS**: the held read finished on the old storage, "available" (every byte matched its content key), answered "stale", nothing sent; the worker then released that storage (references released n -> n+1, holds none); the new client's tile built from its own first reads, the read tracker holding only the new epoch's files |
| Client replaced during a read held **before its file is opened** (only the worker's reference keeps the old storage) | `W_switch` steps 2, 4, 5, 7 (root Beta -> Retail, WDT Era -> Retail, texture Retail -> MoP, root Retail -> Era) | **PASS**: as above -- the file was opened and read on the replaced storage after the GUI had closed it |
| Rapid Retail -> Classic -> Retail | `W_switch` (8 switches in one process), `H_C_switch_nomop_r3250`, `H_C_switch_withmop_r3250` | **PASS**: no answer crossed a client (every request answered once, every file a first read of its client) |
| Superseded load during a read | `readsupersede` for each role (`W_reads_r3049` steps 8-11, `W_heavy_2923` step 1) | **PASS**: the held load answered "superseded", once; the released read "stale" |
| Load cleared during a read | `readclear` for each role | **PASS** |
| Timeout with a read held | `readtimeout` for each role | **PASS**: the load fails once, "timed out"; the late result "stale" |
| A genuine late **file** answer | `readlate` (texture, tex0, root; `W_reads_r3049` step 5 onwards, `W_heavy_2923` step 3) | **PASS**: the player gave the load up after 2,911 ms (its 3,000 ms timeout); the read, let go, was answered "available" with its binary frame (88,580 bytes, MD5 `f394532c...`); the player counted it late and dropped it; the stream stayed intact |
| Corrupt content key | `readcorrupt` for each role on Retail and on Classic Beta; in process | **PASS**: "unreadable", "damaged on this computer: its data does not match its checksums", nothing sent; a WDT or root fails its load naming the host's state, a tex0 makes the material "unavailable" |
| Slow reads (60 ms each) | `W_heavy_2923` step 5 `readslow:60` | **PASS**: 35 reads, none under 60 ms, the slowest 103.7 ms; the longest map serving in one poll meanwhile **8.17 ms** (section 14: its answer's log line) |
| Player disconnect and reconnect during a read | `readreconnect` (`W_reads_r3049` steps 19-20, `W_heavy_2923`) | **PASS**: the held read and the other in flight dropped with their connection, never answered; the new player clean |
| Shutdown during a read | `W_switch` step 11 `readshutdown` | **PASS**: a WDT read held with its file open at exit; the log's last worker line: "156 read(s) given, 150 read, 0 left unread at the stop; storage references taken 9, released 9"; the process exited normally |
| Too large | in process (`W_reads_r3049`, `W_beta_inproc`) | **PASS**: "larger than this read allows (163,908 bytes, the limit is 1000)", nothing read |
| Encrypted | in process on Classic Beta (`W_beta_inproc`): Map.db2 1349477 | **PASS** on Classic Beta ("encrypted with a key this computer does not have", nothing handed back); **SKIP** on Retail (no such file there) |
| Queue full | in process: 16 waiting with 2 reads in flight; 17th and 18th | **PASS**: refused at once, every request answered once |
| Send high water | in process: more than 16 MiB unsent | **PASS**: nothing dispatched; dispatch resumed in order once sent |
| Texture budget with reads in flight | in process: 120 MiB read + 8 MiB reserved | **PASS**: refused, both figures named; with 1,000 bytes left a read gets a 1,000-byte limit |
| Repeated loads | `W_mem_2923` (31 loads), `W_mem_r3049` (101), `H_C_lifecycle03_r3250`, `H_B_lifecycle` | **PASS**: section 15 |
| File not in client storage | `H_B_era_notinstorage` (Era 169 25_31) | **PASS** |
| Not installed locally | `H_B_missingtex0` (Retail 2736 32_29, tex0 5862455) | **SKIP (environment)**: the file has since been installed (`notlocalpart` precondition: "now available"), so the material is "textured", not "unavailable"; the release build of `42977505` fails the same check the same way (`h_before\H_B_missingtex0_base`); the client has no file left that is listed but not local (census: 0) |

### 11.3 Stage 03/04 regression battery (`final\h`)

| Run | Harness | Checker |
|---|---|---|
| `H_C_late_r3250` (late answers per role, 10 s timeout) | PASS 438/0/1 | PASS 71/0/12 |
| `H_C_switch_nomop_r3250` | PASS 158/0/1 | PASS 39/0/4 |
| `H_C_switch_withmop_r3250` | PASS 169/0/1 | PASS 56/0/4 |
| `H_C_worst_m1051` (map 1051 31_29, 11 loads) | PASS 158/0/1 | PASS 90/0/12 |
| `H_C_lifecycle03_r3250` | PASS 250/0/1 | PASS 173/0/27 |
| `H_B_lifecycle` | PASS 187/0/1 | PASS 74/0/16 |
| `H_B_late` (late answers, reconnects) | PASS 319/0/1 | PASS 28/0/7 |
| `H_B_era_notinstorage` | PASS 133/0/2 | PASS 10/0/4 |
| `H_B_mdid0` (map 169 24_27, MDID 0) | PASS 133/0/1 | PASS 10/0/3 |
| `H_B_missingtex0` | FAIL 132/1/2 | FAIL 9/1/4 -- environment, see 11.2 |
| `H_P_p12player_basic` (protocol-12 player) | PASS 130/0/1 | PASS 13/0/3 |
| `H_P_p12player_lifecycle` (protocol-12 player) | PASS 242/0/1 | PASS 162/0/27 |

Every run's checker includes the new checks: no poll spent over 20 ms of the GUI thread on map serving, every request
answered once, every file served read by the worker. The skips are the harness's and checker's own (checks for other
clients, other tiles or other players), unchanged from stage 04.

## 12. Protocol compatibility

- **Protocol 13, unchanged** (`UnityIpcServer.h:396`). `git diff 42977505` adds or removes no field of any message: the
  `getMapAsset` answer is built by the same lines (`UnityIpcServer.cpp:1960-1993`: type, requestId, ok, fileDataID,
  role, load, assetEpoch, state, detail, readMs, md5, byteLength, encoding, payloadBytes), the binary frame the same
  (`queueFrame`). `readMs` is still the storage read, now timed on the worker.
- **No player change.** Every run of this branch uses the stage 04 player (`Assembly-CSharp.dll` MD5 `5C6EC342...`); the
  harness logs `unityReady (protocolVersion 13)` in each.
- **Protocol-12 player** (the stage 03 snapshot, `F:\wmv-build-fix\terrain\p12_player_snapshot`): `H_P_p12player_basic`
  and `H_P_p12player_lifecycle` (section 11) -- offered no textures, asks for the WDT and root only, served through the
  worker.
- **Answers out of order.** A refusal decided at dispatch can overtake reads in flight; the player matches by request
  id (B.4), and every run's checker requires every request answered exactly once (`check_terrain_run.py:242`).
- **What is new in the harness's out.json** (additive; nothing the player reads): `mapAssetWorker` (the server's and the
  worker's accounting), and per served record `readOutcome`, `readState` and `timings`, every step's time: on the GUI `decideMs`, `dispatchMs`,
  `completeMs`, `kindMs`, `serializeMs`, `appendMs`, `sendMs`, `logMs`; on the worker `rootMs`, `openMs`, `sizeMs`,
  `allocMs`, `cascReadMs`, `keyMs`, `md5Ms`, `closeMs`, `workerWaitMs`, `workerMs`; and `inFlightMs`, dispatch to
  completion.

## 13. Performance, before and after

### 13.1 Method

As A.1, on four tiles: three processes of five loads each per tile and build (`repeat:4`), after one warm-up process,
hidden desktop, no game client running. **Before** = the 1.1 release build of `42977505` (`fb1e9de7...`), the A.2 runs
for map 2923 and `perf_before` for the others; **after** = the final build (`2C968C26...`, section 11); **pre-wake** =
this branch before the wake (`43F67245...`, section 13.3). Same player (`5C6EC342...`) throughout. Machine-readable:
`F:\wmv-build-fix\w04b\sum_before\`, `final_prewake\sum_*_prewake.json`, `final\sum_*_after.json` (`summarize.py`), the
tables below as `final\perf_tables.json` (`perf_table.py`). Every file served was the same bytes: the 525 + 525 served
files of map 2923 match the pinned stage 04 copies (A.1), and the composite dumps are identical (section 17).

Medians in ms; "texture fetch" is the player's time from the first texture request to the last texture answer; the
player's round trip is from its request to its handling of the answer (its log). No percentages are claimed beyond
what these numbers say.

**Whole tile** (the player's report; median of 3 cold and 12 warm loads per build, ms)

| Tile | Load | before total | pre-wake total | after total | before texture fetch | pre-wake | after |
|---|---|---|---|---|---|---|---|
| Retail 2923 32_24 (heavy: 32 textures, 90.9 MB) | cold | 1238.9 | 881.8 | 725.9 | 1062.2 | 672.0 | 551.0 |
| Retail 2923 32_24 (heavy: 32 textures, 90.9 MB) | warm | 1215.7 | 841.7 | 674.9 | 1071.2 | 679.8 | 534.1 |
| Retail 2175 37_32 | cold | 769.7 | 765.1 | 327.1 | 640.6 | 596.3 | 199.5 |
| Retail 2175 37_32 | warm | 719.1 | 669.8 | 312.1 | 601.1 | 535.2 | 196.4 |
| Retail 0 30_49 | cold | 234.3 | 283.2 | 193.6 | 110.8 | 136.8 | 59.9 |
| Retail 0 30_49 | warm | 199.5 | 245.3 | 131.5 | 105.2 | 134.3 | 48.3 |
| Retail 0 32_50 | cold | 169.3 | 232.4 | 157.9 | 46.3 | 87.0 | 32.3 |
| Retail 0 32_50 | warm | 124.0 | 198.9 | 120.7 | 44.2 | 87.4 | 26.8 |

**Per request:** GUI-thread time (`handlerMs`) and the player's round trip (median / p95 / max, ms)

| Tile | Files | n | before handler | after handler | before round trip | pre-wake round trip | after round trip |
|---|---|---|---|---|---|---|---|
| h2923 | wdt | 15 | 0.70 / 0.89 / 0.89 | 0.23 / 0.32 / 0.32 | 15.30 / 16.80 / 16.80 | 30.40 / 32.20 / 32.20 | 15.60 / 18.00 / 18.00 |
| h2923 | rootAdt | 15 | 2.92 / 3.00 / 3.00 | 0.35 / 0.40 / 0.40 | 16.90 / 28.90 / 28.90 | 30.30 / 31.80 / 31.80 | 17.20 / 20.10 / 20.10 |
| h2923 | tex0Adt | 15 | 12.63 / 13.91 / 13.91 | 0.94 / 1.12 / 1.12 | 31.10 / 46.00 / 46.00 | 35.40 / 48.50 / 48.50 | 30.80 / 45.90 / 45.90 |
| h2923 | texture <=1MiB | 60 | 2.22 / 2.60 / 2.74 | 0.33 / 1.49 / 2.19 | 96.50 / 130.60 / 147.50 | 73.40 / 79.50 / 86.80 | 49.50 / 60.20 / 64.20 |
| h2923 | texture 1-4MiB | 240 | 8.84 / 9.62 / 11.03 | 0.73 / 2.20 / 3.32 | 122.45 / 170.00 / 191.70 | 64.60 / 98.10 / 127.40 | 47.00 / 90.80 / 105.90 |
| h2923 | texture >4MiB | 180 | 33.08 / 35.98 / 38.21 | 1.87 / 2.56 / 3.61 | 144.40 / 185.70 / 211.20 | 89.85 / 124.30 / 137.60 | 72.45 / 115.60 / 129.10 |
| m2175 | wdt | 15 | 0.75 / 0.89 / 0.89 | 0.23 / 0.37 / 0.37 | 15.70 / 30.10 / 30.10 | 30.40 / 44.60 / 44.60 | 15.40 / 18.20 / 18.20 |
| m2175 | rootAdt | 15 | 2.51 / 2.79 / 2.79 | 0.40 / 0.54 / 0.54 | 16.80 / 17.80 / 17.80 | 30.30 / 31.90 / 31.90 | 16.70 / 17.50 / 17.50 |
| m2175 | tex0Adt | 15 | 6.44 / 6.75 / 6.75 | 0.46 / 0.92 / 0.92 | 44.80 / 46.20 / 46.20 | 32.80 / 46.90 / 46.90 | 43.80 / 46.50 / 46.50 |
| m2175 | texture <=1MiB | 690 | 2.20 / 2.48 / 2.87 | 0.23 / 0.39 / 0.66 | 32.20 / 59.60 / 64.20 | 30.70 / 45.90 / 61.60 | 11.20 / 21.80 / 29.00 |
| m2175 | texture 1-4MiB | 180 | 8.61 / 9.60 / 9.95 | 0.70 / 0.99 / 1.53 | 37.90 / 69.30 / 85.10 | 31.55 / 60.70 / 76.50 | 13.80 / 24.50 / 36.50 |
| r3049 | wdt | 15 | 0.70 / 0.84 / 0.84 | 0.23 / 0.26 / 0.26 | 15.60 / 30.40 / 30.40 | 30.10 / 32.20 / 32.20 | 15.60 / 32.70 / 32.70 |
| r3049 | rootAdt | 15 | 2.03 / 2.25 / 2.25 | 0.32 / 0.38 / 0.38 | 16.60 / 17.80 / 17.80 | 31.30 / 33.20 / 33.20 | 17.10 / 18.60 / 18.60 |
| r3049 | tex0Adt | 15 | 7.03 / 7.48 / 7.48 | 0.51 / 0.64 / 0.64 | 44.10 / 46.90 / 46.90 | 45.90 / 60.10 / 60.10 | 29.10 / 45.90 / 45.90 |
| r3049 | texture <=1MiB | 165 | 0.68 / 0.88 / 1.00 | 0.12 / 0.24 / 0.65 | 14.70 / 49.20 / 52.20 | 30.40 / 45.20 / 47.60 | 11.90 / 19.30 / 22.40 |
| r3049 | texture 1-4MiB | 30 | 8.77 / 9.43 / 9.65 | 0.66 / 0.79 / 0.81 | 50.05 / 74.70 / 75.40 | 46.20 / 61.80 / 61.90 | 18.40 / 28.10 / 43.10 |
| r3250 | wdt | 15 | 0.71 / 0.87 / 0.87 | 0.24 / 0.30 / 0.30 | 15.30 / 29.60 / 29.60 | 30.40 / 45.60 / 45.60 | 15.60 / 17.20 / 17.20 |
| r3250 | rootAdt | 15 | 2.04 / 2.47 / 2.47 | 0.32 / 0.38 / 0.38 | 17.10 / 19.40 / 19.40 | 31.40 / 32.70 / 32.70 | 16.70 / 18.00 / 18.00 |
| r3250 | tex0Adt | 15 | 7.81 / 8.76 / 8.76 | 0.50 / 0.64 / 0.64 | 25.10 / 46.70 / 46.70 | 45.40 / 46.70 / 46.70 | 44.70 / 45.20 / 45.20 |
| r3250 | texture <=1MiB | 150 | 0.69 / 0.83 / 0.88 | 0.12 / 0.23 / 0.90 | 14.80 / 16.80 / 17.60 | 30.60 / 39.80 / 43.80 | 10.90 / 15.40 / 16.20 |

### 13.2 Reading the numbers

- **GUI time per request** fell for every kind of file: a 5.6 MB texture from 33.08 ms (median; 38.21 max) to 1.87 ms
  (3.61 max); a tex0 ADT from 6.4-12.6 ms to 0.46-0.94 ms. What is left is the decision, the completion and the send
  (13.4).
- **Whole tiles load faster** on every tile measured, most where there are many or large textures: map 2923 32_24
  1,238.9 -> 725.9 ms cold, 1,215.7 -> 674.9 warm; map 2175 37_32 769.7 -> 327.1 cold, 719.1 -> 312.1 warm. The smallest
  tile (0 32_50: 10 textures under 1 MiB) changed least: 169.3 -> 157.9 cold, 124.0 -> 120.7 warm.
- **Why faster, not just off the GUI thread:** before, the GUI read each file while the player waited, and a poll's
  2 MiB budget sent the rest to the next timer tick (15.6 ms here); now the worker reads the next file while the GUI
  answers the last one, and each result is answered as soon as it is read (the wake).
- **What did not change:** the WDT and root round trips (15-17 ms: the player's own frame cadence dominates), the tex0
  round trip (about 30-45 ms), the first frame. The worker's read of a 5.6 MB file is the same 31.6 ms (`cascReadMs`
  24.7 + `md5Ms` 6.8): the integrity checks are kept, only moved.

### 13.3 Without the wake: why it was added

The first complete build of this branch collected results only in the next poll. Measured on the same tiles
(`final_prewake`, the "pre-wake" columns): every request waited one more poll interval (15.6 ms here; the viewer's timer
is 20 ms), so the WDT, root and small-texture round trips doubled (15 -> 30 ms) and the ordinary tiles loaded **slower
than the release build** (0 32_50 warm 124.0 -> 198.9 ms; 0 30_49 warm 199.5 -> 245.3 ms), while map 2923 was already
faster (1,215.7 -> 841.7 ms). That build was not accepted: the wake (C.3) answers a result at once. With it, 99.1 % of
the results read in the final battery were taken by a wake poll (7,362 of 7,428), and the regression is gone.

### 13.4 The GUI thread's work per 5.6 MB texture, now (map 2923, n = 180, median / p95 / max ms)

| Step | median | p95 | max |
|---|---|---|---|
| decide and account (`decideMs`) | 0.006 | 0.017 | 0.022 |
| dispatch to the worker (`dispatchMs`) | 0.012 | 0.028 | 0.039 |
| completion: check again, kind check, accounting, header, answer (`completeMs`, incl. the next three) | 1.852 | 2.521 | 3.594 |
| ... copy into the send buffer (`appendMs`) | 0.286 | 0.342 | 0.808 |
| ... `send()` (`sendMs`) | 1.483 | 2.132 | 3.208 |
| ... JSON header (`serializeMs`) | 0.020 | 0.025 | 0.040 |
| **GUI total (`handlerMs`)** | **1.868** | **2.556** | **3.606** |
| the record's log line, after it (`logMs`) | 0.092 | 0.122 | 1.184 |
| on the worker: root lookup, open, size, alloc, read (`cascReadMs` 24.689), content key, MD5 (`md5Ms` 6.810), close | 31.597 total | 33.312 | 34.262 |

Section H expected about 1.8 ms; measured 1.87 ms.

## 14. GUI responsiveness

Measured on every one of the 51 final runs (`final\{w,h,perf,e2e,geo_after}`: 34,435 polls, 7,488 answers with
timings), against the instrumented before (A.3, map 2923).

| | Before (A.3) | After (all final runs) |
|---|---|---|
| GUI time per answer (`handlerMs`), max | 38.21 ms (5.6 MB texture) | **3.62 ms** (5.6 MB texture, `W_mem_2923`) |
| Map serving in one poll, max | 37.65 ms (the whole poll) | **17.89 ms** (`W_mem_2923`), of which 15.2 ms was its answer's log line |
| ... in the runs where no log line took over 5 ms | -- | **4.59 ms** (`G_h2923`) |
| Polls over 20 ms of map serving | in every run (poll max 37.65 / 36.91 / 37.03 ms) | **0** (every run's checker: PASS) |
| Longest run of wake polls in one pass of the event queue | -- | 20.29 ms (`W_mem_2923`, the same log stall; the cap of 8 ms deferred 11 wakes in all) |

- **The log line.** Every poll over 5 ms of map serving in the final runs contains one answer whose record's log line
  (`LOG_INFO` after the answer, unchanged code) took 7.4-16.0 ms to write: 22 of 7,488 answers; without it the longest
  poll served for 4.59 ms. In the 595 answers of the instrumented before runs the same line never took over 0.21 ms.
  The cause inside the write is not established (UNKNOWN: the worker writes no log and takes no lock the logger takes;
  most stalls are about one Windows timer tick, 15.6 ms, long). It is reported, not hidden, and stays under the 20 ms budget.
- **GUI rounds over 20 ms** (a poll plus the event queue's work, logged with the step; 121 in all final runs): at
  client switches and the first tile after one (`switch`, `readswitch`, `lateswitch`, `tile`: 107, up to 146 ms), player
  restarts (`readreconnect`, `reconnect`: 4, up to 179 ms) and model loads (`model`, `modelthentile`: 9, up to 62 ms --
  the M2 load runs on the GUI thread, out of scope). The map serving in their polls was at most 1.58 ms. They are the
  viewer's own work, the same number with and without the wake (60 in `W_switch` both times). One more, 20.59 ms in
  `W_mem_2923`, is map serving: the poll with the 15.2 ms log line.
- **Slow reads do not reach the GUI.** With every read made 60 ms slower (`readslow:60`), the longest map serving in a
  poll was 8.17 ms (one log line); a read held for seconds (`readtimeout`, `readlate`, `readswitch`) never stalled a
  poll.
- **Maximum GUI-thread stall caused by map serving:** 3.62 ms of work per answer; 4.59 ms in one poll without a log
  stall, 17.89 ms with one; before: 38.21 ms per answer, 37.65 ms per poll.

## 15. Memory

- **Worker side** (all 51 final runs): bytes read and not yet taken back at most **5,593,604** (one 5.6 MB texture;
  bound 16 MiB); at most 2 jobs waiting in the worker and 2 reads in flight (bound 2); unsent bytes at most **0** (the
  player always read promptly; the 16 MiB high water is tested in process, 11.2, never on a real socket that stopped
  being read).
- **Outgoing IPC buffering: bounded for the shipped Unity player's requests, not universally.** *(Corrected after the
  post-commit review, 2026-10-15:)*
  - *Map serving, any client.* Once a client stops reading, map serving adds at most about 32 MiB to the server's send
    buffer: nothing leaves the queue while more than 16 MiB are unsent (`MAP_ASSET_SEND_HIGH_WATER`,
    `UnityIpcServer.cpp:1812-1816`), and the two reads already in flight are still answered, 2 x (8 MiB + header)
    (`:1766-1805`) -- base64, about 2 x 10.7 MiB, to a client that has not announced protocol 10 or later
    (`:1978-1996`, `UnityIpcServer.h:466`), so about 37 MiB then. The kernel's 64 MB `SO_SNDBUF` comes on top
    (`:268-269`, inherited). This gate holds whatever the client.
  - *The shipped player.* Its map requests stay within the gate: it keeps at most 4 map requests out per load, one
    load at a time (`MaxOutstanding`, `Tools/UnityRendererProject/Assets/Scripts/WmvTerrainSession.cs:117`), and asks
    only in reply to the host (its `loadTerrainTile`, or an answer of the same load once applied), so none arrives
    while it is not reading. (Requests of a load that ended still wait at the host until dispatched, so 4 is a limit per
    load, not on the queue: the 16-deep queue can fill, and a request be refused on arrival, only if map reads stall
    across many superseding loads; such a refusal is a small answer to a player that is reading.) It does use other
    ungated paths: every model and world-model load asks with `getAsset`, `getAssetByFileDataID` and `getModelTextures`
    (`WmvIpcClient.cs:1363`, `:1380`, `:1484`), each answer up to 64 MB (`UnityAssetAccess.cpp:71`), a world model's
    groups and then its textures each requested all at once (`WmvMainMapObject.cs:9`, `:14`). Those requests too follow
    only from messages it has read. So with the shipped player the send buffer holds at most the map answers bounded
    above plus the answers to the asset requests it has sent -- hundreds of MB for a large world model, as at
    `42977505` -- apart from the viewer's own messages (next point).
  - *Not gated for any client.* Messages the viewer sends on its own are appended whether or not the client reads,
    the shipped player included: the playback-state heartbeat (at most one small message a second while a model
    animates, `modelviewer.cpp:4155-4164`, `modelviewer.h:782`, `UnityIpcServer.cpp:1033`), a character's scene sent
    again every 8 s while it goes unanswered (`modelviewer.cpp:3245-3287`, `modelviewer.h:552`), and the commands that
    follow user actions -- some of them large: a changed character's composited texture goes out as a `characterImage`
    of several MB (`UnityIpcServer.cpp:1060-1141`). A player that has stopped reading lets these pile up at the rate the
    viewer produces them: slowly while nothing changes, by megabytes per character change.
  - *Arbitrary local clients: not bounded.* The server accepts any connection on its 127.0.0.1 port and drops the
    player's (`pollAccept`, `UnityIpcServer.cpp:245-282`). A client that keeps writing requests without reading grows
    the send buffer without limit: a `getMapAsset` refused on arrival while 16 wait is answered at once without the
    high-water check (`UnityIpcServer.cpp:1636-1646`, about 0.3 KB each), and `getAsset` / `getAssetByFileDataID` (up to
    64 MB each), `getModelTextures` (`:1223-1226`) and answers to unknown messages (`:1255-1266`) are appended with no
    check either (`queueLineParts` / `queueFrame`, `:404-434`).
  - All of these paths are inherited unchanged from `42977505`; a transport-wide cap is a separate change (18.2). The
    high-water path has been tested only in process (`testFakeUnsentBytes`); no run had a real socket that stopped
    being read.
- **No second storage:** the worker reads through a reference to the GUI's own storage (C.2), so the 600 MB steady /
  894 MB peak and 2.4 s of a second open storage (A.4) are not spent.
- **Storage references:** the shutdown log line balanced in every run (51 of 51; 9 and 9 in `W_switch` across eight
  client switches). *(Corrected after the post-commit review, 2026-10-15:)* that line, "storage references taken N ,
  released N" (as logged: Qt's stream puts a space before the comma), is the worker's `leasesGranted` / `leasesReleased`
  read after `stop()` has joined it (`modelviewer.cpp:1864-1869`). out.json's counters of the same names are read before
  the shutdown, while the worker still holds its current lease, and show one fewer released (`W_switch`: 9 / 8,
  `holdsLease` true). Both count only leases the worker took over: a grant still queued when the worker stops (released
  by `stop()`, `MapAssetWorker.cpp:171`) or given after it stopped (released at once by `grantLease`, `:61`) is released
  but counted in neither; that it is released exactly once rests on the source (C.2, 9.1), not on these counters.
- **Host private bytes over many loads:** map 2923, 31 loads: 2,343 -> 2,348 MB (peak 2,349); map 0 30_49, 101 loads:
  2,342 -> 2,344 MB. Before (A.2): 2.34 GB, flat over five loads (+5.6 MB after the first) -- the same profile.
  Client switches raise it while another client's storage is open (up to 2.96 GB in `W_switch`, 2.39 GB after it).
- **Player** (unchanged): its managed heap at report time alternates between about 31 and 118 MB on map 2923 in both
  builds (release `base_r1`: 31.1, 118.1, 31.4, 118.1, 31.4 MB), at most 118.2 MB over 31 loads; 22.4-24.9 MB on map 0
  30_49 over 101 loads. Native allocations 144-217 MB, as before.

## 16. Copies of a file's bytes

Followed for one texture from the archive to the player (PROVEN_SOURCE at the lines cited; times from section 13's
heavy-tile runs, files over 4 MiB, median / max):

| # | Where | Copy | Thread | Time per 5.6 MB |
|---|---|---|---|---|
| 1 | CascLib `ReadFile_FrameCached` (`ThirdParty/casclib/src/CascReadFile.cpp:828-930`) | each frame's encoded bytes read from the archive into a frame buffer (`:887`) | worker | in `cascReadMs` |
| 2 | the same, `DecodeFileFrame` | decoded straight into the read's own buffer when the whole frame is wanted (`:860-875`), as a whole read always does: no second copy | worker | in `cascReadMs` |
| - | MD5 against the content key (`CASCFolder.cpp:412`) | reads in place | worker | `md5Ms` 6.81 / 7.63 |
| - | worker -> GUI (`MapAssetWorker.cpp` `takeResult`: `std::move`), result -> answer (`modelviewer.cpp:3997`: `std::move`) | none: the `QByteArray` is moved | | 0 |
| 3 | `UnityIpcServer::queueFrame` (`UnityIpcServer.cpp:416-434`) | header and payload appended to the send buffer | GUI | `appendMs` 0.29 / 0.81 |
| 4 | `send()` | into the socket's kernel buffer (64 MB send buffer) | GUI | `sendMs` 1.48 / 3.21 |
| 5 | player `WmvStreamReader.ReadPayload` (`Tools/UnityRendererProject/Assets/Scripts/WmvIpcClient.cs:2123-2146`) | kernel -> one `byte[]` of the payload's size (the line buffer's few buffered bytes copied first) | player's reader thread | not measured here |

The branch adds no copy: the bytes the worker reads are the bytes the GUI sends. Copy 3 could be avoided by sending the
payload from its own buffer (a gather send), but it is the GUI thread's only remaining per-byte work besides `send()`
itself, 0.29 ms of a 1.87 ms handler, and it keeps one ordered send buffer for every message; copy 4 cannot be
avoided over TCP. Investigated, not changed. The player's side (copy 5 and the texture upload after it) is unchanged by
this branch.

## 17. Regressions

All on the final build against the release build of `42977505` (run here the same day, outputs in `final\*_before`)
and against stage 04's own validation outputs (`F:\wmv-build-fix\terrain04\r4v`, the pinned references).

| Check | Result |
|---|---|
| Terrain composite dumps (`-wmvTerrainCompositeDump`: the textured view as float pixels, per-chunk states and layers, and the texel dumps of the textures), 11 runs on 9 tiles of 3 clients: Retail 0 32_50, 29_50, 32_48, 34_46, 30_49, 1917 27_19, 2923 32_24; Classic Beta 0 32_48; Classic Era 2853 34_50; plus the validation views of 2923 32_24 and 0 30_49 with their chunk close-ups | **98 of 98 files identical** to the release build's (`.raw`, `.png` by MD5; `.json` with the file names left out) and **98 of 98** to stage 04's references (`.json` also without the client build string: the client updated from 12.1.0.69933 to 12.1.5.70391 since; the pixels did not change) -- `final\cmp_e2e.json` |
| Terrain geometry dumps (`-wmvTerrainDump`, shaded view), 6 tiles: Retail 32_50, 34_46, 1051 31_29, 2923 32_24; Era 32_50; Classic Beta 32_48 | **6 of 6 byte-identical** to the release build's |
| M2 model, bear (`-mo creature/bear/bear.m2`, pinned animation time), 1600 px capture | **identical**: MD5 `2627227A...`, the release build's and stage 04's pin |
| WMO 108631 (Stormwind jail), 1600 px capture | **identical**: MD5 `D3DB7749...`, the release build's and stage 04's pin |
| Map catalogue sequences (`-mapcatalogtest`: Retail -> MoP Classic -> Retail, Retail -> Classic Beta -> Retail, Era -> Retail, Retail -> Retail), the WDT read through `inspectReference` -> `inspectInStorage` | **PASS** 44/0, 44/0, 27/0, 27/0 |
| Every file served on map 2923 compared with the pinned stage 04 copy of its FileDataID | **525 of 525 identical** |
| `scripts/tests/casc-whole-read` (the read's integrity contract, CascLib stubbed; built against the branch's `CASCFolder.cpp`) | **PASS** 24/0 |
| Stage 03/04 harness battery (section 11.3), protocol-12 player included | **PASS**, but `H_B_missingtex0` (environment, SKIP) |
| No shader, material formula, geometry, protocol message or player change | `git diff 42977505 --stat` (section 9.1) |

## 18. Limitations and open points

### 18.1 Mandatory before Stage 05 enables interactive terrain loading: shutdown event re-entrancy

Found by the post-commit review (2026-10-15). It cannot happen at this commit; **it must be removed before any UI path
starts terrain loads** (calls `RequestTerrainTile`).

- **Mechanism.** `~ModelViewer` stops the worker first (`modelviewer.cpp:1865`). `stop()` lets the read under way
  finish, and that result's notifier posts a wake (`MapAssetWorker.cpp:334-335` -> `UnityIpcServer.cpp:1674-1677`) to a
  server that is still listening. `UnityRendererHost::shutdown` (`UnityRendererHost.cpp:498-556`, unchanged from
  `42977505`) runs later in the destructor (`modelviewer.cpp:1906`). With the player process still running -- with none,
  it stops the server at once (`:503-509`) -- it waits for the player, dispatching sent messages and removing paint
  messages (`MsgWaitForMultipleObjects(... QS_SENDMESSAGE | QS_PAINT)`, `PeekMessage(... PM_REMOVE | PM_QS_SENDMESSAGE |
  PM_QS_PAINT)`, `:530-539`), **before** it stops the server (`:554-555`). In wxWidgets 3.3.3 every message retrieved
  with `PM_REMOVE` runs wx's `WH_GETMESSAGE` hook (`wxIdleWakeUpModule::MsgHookProc`, upstream `src/msw/window.cpp`):
  here a `WM_PAINT`, since sent messages are dispatched inside `PeekMessage` and do not reach the hook. The hook calls
  `wxApp::MSWProcessPendingEventsIfNeeded` (upstream `src/msw/app.cpp`), which runs `ProcessPendingEvents` when the
  event loop's wake-up event is signalled (`QueueEvent` signals it through `wxWakeUpIdle`) or no loop is active. The
  wake then runs `onMapResultWake` -> `poll()` (`UnityIpcServer.cpp:1680-1709`, `:214`) on the listening server: its
  `pollAccept` / `pollReceive` (the player-message handlers), `serveMapAssetQueue` with the map callbacks
  (`MapAssetWait`, `MapAssetStaleReason`, `CompleteMapAsset`, `DispatchMapAsset`) and `pollSend` -- **while
  `~ModelViewer` is in progress** (its background window, menu-bar titles and export job manager already gone,
  `modelviewer.cpp:1873-1889`). A wake posted after the main loop last processed pending events but before
  `~ModelViewer` began re-enters the same way. No freed member is reached on the paths read (the freed ones are
  null-checked; dispatch refuses with the worker stopped, `modelviewer.cpp:3824-3828`), but nothing guarantees that for
  the player-message handlers. Base `42977505` had no wake (no `wxEVT_THREAD`; the pump does not retrieve `WM_TIMER`),
  so this re-entry comes with the wake.
- **Not this prerequisite.** The same hook runs every handler's pending events, so other queued events (`CallAfter`s,
  e.g. `modelviewer.cpp:4648`, `UnityRendererHost.cpp:638`) can also run inside `~ModelViewer`. That re-entry predates
  this branch.
- **Why it cannot happen at this commit.** The worker gets jobs only from the `-terraintile` harness: through the
  terrain loads it starts (`RequestTerrainTile`, `modelviewer.cpp:3691`, current at `:3736`, called from `app.cpp:7577`;
  `TerrainTestRaiseLoadWithoutSending`, `modelviewer.cpp:4071-4077`, from `app.cpp:8367`; requests it sets itself,
  `app.cpp:6651-6720`) and through its direct test reads (`TestReadThroughWorker`, `TestHoldReadForShutdown`,
  `ServeMapAssetAndWait`, `modelviewer.cpp:3875-3922`, `:4006-4031`). In the interactive viewer no terrain load is ever
  current, so every `getMapAsset` is refused or answered stale without a read (`modelviewer.cpp:3486-3511`); the worker
  thread never starts and no wake is posted. The harness does not reach the hazard either: a `-terraintile` run
  whose player still runs at its end shuts the player down, which stops the server (`app.cpp:9605-9609` ->
  `UnityRendererHost.cpp:554-555`), before `OnInit` returns (`app.cpp:10224-10225`) and the frame is destroyed;
  `~ModelViewer`'s `shutdown()` then finds no process and pumps nothing (`UnityRendererHost.cpp:503-509`). If the player
  had already exited, the harness skips that shutdown (`isRunning()` is false and has reaped the process handle,
  `UnityRendererHost.cpp:482-495`), so the server is still listening when `~ModelViewer` begins and `stop()` can queue
  a wake to it; but that `shutdown()` also finds no process and stops the server without pumping, and at this commit
  nothing between `modelviewer.cpp:1865` and `:1906` retrieves a message. Either way a wake handled later finds the
  server not listening (`UnityIpcServer.cpp:216-217`). The `W_switch` log shows "[unityipc] stopped" and "player shut
  down" before "Shutting down the program...". A wake still queued when the server is destroyed
  (`UnityRendererHost.cpp:356`, reached from `modelviewer.cpp:1962`) is discarded with it: `wxEvtHandler`'s destructor
  removes the handler from the application's pending list and deletes its pending events (D).
- **Required change (Stage 05 prerequisite).** Once `~ModelViewer` has begun, no IPC poll may get past its listening
  check (`UnityIpcServer.cpp:216-217`): nothing accepted, received (so no player-message handler run), dispatched,
  completed or sent. As its first statement, before `m_mapAssetWorker->stop()`, the destructor must (1) clear the
  worker's result notifier under the worker's lock, as `setMapAssetWorker(nullptr)` does
  (`UnityIpcServer.cpp:1659-1665`, `MapAssetWorker.cpp:188-192`), so that no result made while `stop()` joins posts a
  wake; and (2) make every later poll a no-op -- either a detached state checked at the top of `poll()` itself, which
  covers wakes already queued, the server's timer (`UnityIpcServer.cpp:179`, `:209-212`) and any pump later in the
  destructor, or stopping the server there (the player quits when its connection ends,
  `Tools/UnityRendererProject/Assets/Scripts/WmvIpcClient.cs:888-895`). Clearing `onDispatchMapAsset`,
  `onCompleteMapAsset`, `onMapAssetStale` and `onMapAssetWait` is a useful second guard. Not sufficient on their own: a
  check only in `onMapResultWake` (it leaves the timer's route), and a server stop or
  `wxTheApp->SuspendProcessingOfPendingEvents()` placed only around the shutdown pump -- `stop()` posts its wake at
  `modelviewer.cpp:1865` with the server listening, so that holds only while nothing up to `:1906` retrieves a message,
  which is true at this commit but not guaranteed.
- **Required test.** It must fail at `81f79982` and pass after the change, and its verdict must not rest on a paint
  alone. A `-terraintile` step that (1) leaves a **server-tracked** read under way in the worker at shutdown
  (dispatched through `serveMapAssetQueue` and held by the server's read hook, so its result is made inside `stop()`);
  (2) skips the harness's own player shutdown (`app.cpp:9605-9609`), so that the player is running and the server
  listening when `~ModelViewer` begins -- otherwise `~ModelViewer`'s `shutdown()` never pumps; (3) from a test hook in
  `~ModelViewer` placed immediately before `m_mapAssetWorker->stop()` (after the change's statements), calls the
  server's `poll()` once -- the timer's route, which the shutdown pump never takes, checked without needing any message
  retrieved; (4) makes the shutdown pump retrieve a message after the wake is queued, e.g. by invalidating a window of
  the GUI thread when the close begins, and logs how many messages the pump retrieved -- a paint is not guaranteed with
  the harness's window parked off-screen (`modelviewer.cpp:1895-1898`), and a run whose pump retrieved none has not
  exercised the wake route; and (5) counts, from the first statement of `~ModelViewer`, every `poll()` that goes on to
  accept, receive, serve or send, every player message handled and every map callback, logging the counts after the
  pump (out.json is already written by then, `app.cpp:9578-9583`): all must be 0. A wake handler that returns without
  serving is allowed. Part (3) fails at `81f79982` on every run, and so does a change that only clears the notifier or
  guards only `onMapResultWake` (both named insufficient above). The end check "no map read is in flight at the end"
  (`app.cpp:9359`) must allow this read, and the shutdown line "storage references taken N , released N" must still
  balance.

### 18.2 Outgoing IPC buffering against arbitrary local clients

Bounded for the shipped Unity player's requests, not universally (15). Reachable at this commit by any local process,
exactly as at `42977505`: a process can connect to the server's 127.0.0.1 port (which drops the player's connection)
and write requests without reading. It then grows the send buffer without limit through paths not gated by
`MAP_ASSET_SEND_HIGH_WATER`: `getMapAsset` refusals on arrival (`UnityIpcServer.cpp:1636-1646`), `getAsset` /
`getAssetByFileDataID` responses (up to 64 MB each), `getModelTextures` answers and answers to unknown messages. The
viewer's own messages (the animation heartbeat, at most one a second while a model animates; a character's scene
sent again every 8 s while unanswered; the commands that follow user actions, some of them multi-MB `characterImage`
frames) are appended for any connected client that does not read, the shipped player included. All are
inherited unchanged from `42977505`. A fix is transport-wide -- a hard cap on unsent bytes that stops reading from, or
drops, a client that does not read -- and is not part of this branch. The high-water path itself has been tested only
in process (`testFakeUnsentBytes`); no run had a real socket that stopped being read.

### 18.3 Other limitations

- **The WDT read of `ResolveTerrainTile` stays on the GUI thread** (out of scope, F): 0.5 ms for map 2923's 163,908-byte
  WDT, at most about 1 ms for the largest WDT (294,988 bytes).
- **Model and world-model assets** (`getAsset`, `getAssetByFileDataID`) are still read on the GUI thread: they go through
  `GameFile` objects only the GUI may touch (B.3). Not in this branch's scope.
- **One worker, one read at a time.** A slow read delays the reads behind it (head-of-line): by design -- reads are FIFO
  and never concurrent (C.3). Measured with `readslow` and the holds (11.2): the GUI is not delayed, the next file is.
- **A read under way cannot be cancelled.** Reads are whole because a partial `CascReadFile` whose disk read fails
  corrupts the heap (B.2, a CascLib defect not fixed here); cancellation takes effect between reads. The longest read
  is one file's, about 35 ms for a 5.6 MB texture.
- **Out of memory.** A `std::bad_alloc` inside a read is answered "failed" (`MapAssetWorker.cpp:407`); one when the result
  is queued (`m_results.push_back`, `:331`) or elsewhere in the worker's loop is not caught and ends the process -- as an
  allocation failure on the GUI thread would.
- **Interactive use is not measured.** At this commit only the harness requests terrain (no Maps UI yet, 10.1): the
  viewer's 20 ms timer and its event loop handling the wake are exercised by the code path the harness drives (`poll()`
  and the wake in `Yield`), not by an interactive session.
- **The per-answer log line can stall the GUI thread** for 7-16 ms (22 of 7,488 answers, section 14); the line is the
  server's existing one, the cause inside the write is UNKNOWN; every such poll stayed under the 20 ms budget. A
  follow-up could write these lines off the GUI thread or only on request.
- **Wake runs** are capped at about 8 ms of GUI time, measured from the poll's start: one poll that itself stalls (the
  log line) can take a run past it (20.29 ms once).
- **The client's own data:** the "not installed locally" case has no real file on this computer any more (11.2: SKIP),
  and the "encrypted" case is real only on Classic Beta.
- **CascLib was changed** (one exported function, `CascWowShareStorage`, beside the project's other additions): a vendored
  library diff to carry when CascLib is updated.

## 19. Results

**PASS**

- Thread-safe ownership established before any code (sections B, C, 10.1): one worker, its own storage reference
  taken on the GUI thread while the GUI holds one, released only by the worker once taken over (a grant never taken
  over: by `stop()`, or after a stop by `grantLease`, on the GUI thread); every handle opened, read and closed inside
  one read; no GUI-owned state touched by the worker but the server's atomic wake flag and `QueueEvent`, through the
  result notifier (C.3, D).
- 2923 32_24 reproduced and explained before implementation (A): 33 ms of GUI time per 5.6 MB texture, of which CASC
  decode and frame checks 74 %, MD5 21 %.
- No storage read, decode or content-key check on the GUI thread: every file served in the final runs was read by the
  worker (every run's checker).
- GUI time per answer at most 3.62 ms (was 38.21); no poll over 20 ms of map serving in 51 runs (was 37 ms in each).
- Client switches with reads in flight, held with the file open and before it is opened, across Retail, Classic Beta,
  Classic Era and MoP Classic: no answer crossed a client; the old storage survived to the end of the held read and
  was released by the worker.
- Integrity contract unchanged (frame checks and content-key MD5 on every read; corrupt bytes -> "unreadable", nothing
  sent); every state and detail unchanged.
- Protocol 13 unchanged; protocol-12 player served; player unchanged.
- Bounded memory: at most 5.6 MB held by the worker side (bound 16 MiB), 2 reads in flight, backpressure at 16 MiB
  unsent tested in process; the outgoing IPC buffer is bounded for the shipped player's requests only -- not against
  arbitrary local clients, nor for the viewer's own pushed messages to a player that stops reading (15, 18.2).
- Failure tests: timeouts, a genuine late file answer, supersede, clear, corrupt, slow reads, reconnect, shutdown during
  a read, too large, encrypted (Classic Beta), queue full, send high water, texture budget with reads in flight,
  repeated loads (31 and 101).
- Whole tiles load faster than the release build on all four tiles measured (13.2).
- Rendering, geometry, M2 and WMO captures identical (17).

**FAIL**

- None on this branch's code. (`H_B_missingtex0` fails as a run; see SKIP.)

**SKIP**

- `H_B_missingtex0` ("not installed locally"): the client has since installed the tile's tex0 (5862455); the release
  build fails the same check the same way; no listed-but-not-local file is left on this computer (environment).
- "Encrypted" on Retail (no such file there; PASS on Classic Beta).
- The checkers' and harness's own skips for other clients, tiles or players (counts in 11.1 and 11.3, as in stage 04).

**UNSUPPORTED / not done**

- Cancelling a read under way (CascLib's partial-read defect; reads are whole, cancellation is between reads).
- Interactive measurement (no Maps UI at this commit).
- The shutdown event re-entrancy hazard (18.1): not reachable at this commit; its removal is a mandatory prerequisite
  before Stage 05 enables interactive terrain loading.
- A bound on outgoing IPC buffering against arbitrary local clients, and for the viewer's own pushed messages to a
  client that stops reading (18.2): inherited, transport-wide, not this branch.
- The cause of the occasional 7-16 ms log-line write (UNKNOWN; under the 20 ms budget).
- Out of scope, untouched: the WDT read before a load is sent (about 0.5-1 ms on the GUI thread), model and WMO asset
  serving, Maps UI, multi-tile streaming, cross-tile texture caching, LOD, WMO and M2 placement, liquids, sky, fog,
  terrain blending.
