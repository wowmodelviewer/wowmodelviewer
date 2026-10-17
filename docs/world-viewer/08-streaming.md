# World viewer 08 -- streaming

Branch `world/08-streaming`, from `develop` 8f4e791f (Stage 07, PR #121). This document is written in the order the
work was done: the investigation and the architecture first (section 1, written before any code), then the
implementation, its tests and measurements.

Evidence grades: **PROVEN_BYTES** (decided by the client's own files), **PROVEN_RUNTIME** (observed in the running viewer
or player on real client data), **PROVEN_SOURCE** (read in the source), **INFERRED** (follows from proven facts, not
observed directly), **UNKNOWN**. Test outcomes: **PASS**, **FAIL**, **SKIP** (not run, said why), **UNSUPPORTED** (cannot be
run on this data or by this design, said why).

Clients (one installation, as updated on 2026-10-17):

| Client | Product | Build |
|---|---|---|
| Retail | wow | 12.1.5.70448 |
| Classic Era | wow_classic_era | 1.15.9.70003 |
| MoP Classic | wow_classic | 5.5.4.70449 |
| Classic Beta | wow_classic_beta | 1.60.1.70450 |

## 1. Architecture (proposed before implementation)

### 1.1 Starting point

- `develop` = 8f4e791f: Stages 01-07 merged. The World Viewer loads one ADT tile with its world models (MODF) and
  doodads (MDDF and WMO doodad sets), or one global world model, into one world scene with a double-precision floating
  origin and a free camera (`05`-`07`).
- The owner's build carries two uncommitted `wow.dll` fixes (bulky-belt tabard, .chr tabard hang) in their own
  worktrees; every deploy of this stage keeps the owner's `wow.dll`/`core.dll` (0ED07B2C / 8472119A), and nothing of
  those fixes enters this branch.
- The roadmap row 08: tiles around the camera (from 3 x 3), residency and eviction, a prioritised cancellable scheduler,
  block-compressed upload, GPU and memory budgets, loading progress by file count; first the transport-wide send cap
  (`04b` 18.2). The Maps entry cost of `05` 13.1 is done (the lazy Browse list, merged).

### 1.2 What the code does today (investigated, PROVEN_SOURCE)

Five read-only passes over the code (transport, host serving, the player's scene, resources and uploads, the test
harnesses). What they found that decides the design:

**One load, everywhere.**
- Host: `ModelViewer::m_terrainRequest` is the only load; `MapAssetStaleReason` answers any other load "stale";
  `decideMapAssetRequest` checks one `current`; `RequestLoad` replaces it wholesale; `MapsViewer` keeps one tile phase
  and filters every report on `r.load == m_load`. The protocol says "only the latest load is valid".
- Player: `WmvTerrainSession.current` is the only job; worker results for any other job are dropped; `Adopt` commits
  a tile by disposing every other tile and the whole world scene (`DisposeTiles`, `BeginTileWorld`); the prototype
  dictionaries are overwritten by FileDataID; answers are matched only against `current.Pending`.
- Nothing de-duplicates placements across tiles. In the census region below, 169 MDDF and 15 MODF uniqueIds (Retail)
  are listed by more than one tile's obj0: placed per tile, they would be drawn twice.

**Concurrency limits that break with more than one load.**
- The player keeps up to 4 tile + 4 world-model + 4 doodad requests out per load; the host queue holds 16 and answers
  "refused" (final, the file is lost) beyond it, so two loads at once already lose files.
- The host has 2 reads in flight on one worker thread; Stage 07 measured about 100 files/s end to end (1,754 files in
  17 s), latency-bound (the audit's raw CascLib read is 2,577 files/s, `00-audit` T12).

**Transport (`04b` 18.2, line numbers since moved).**
- `UnityIpcServer::m_outBuf` has no cap. Only map-asset dispatch and the two inspect sends check
  `MAP_ASSET_SEND_HIGH_WATER` (16 MiB); `getAsset` answers (up to 64 MiB, read on the GUI thread), queue-full refusals,
  completions of reads in flight, unknown-message replies and every viewer push (the animation heartbeat, the
  character scene re-sent every 8 s, multi-MB character images) append without a check. `pollReceive` handles every
  line in the socket in one poll. Nothing detects a client that stopped reading; any local process can connect (which
  drops the player) and grow the buffer without limit.
- The shipped player never stops reading: its reader thread drains the socket into an unbounded `inbox` that `Update`
  dispatches without a budget. So the backpressure that matters for streaming is not visible on the host's buffer at
  all: it has to be a request credit in the player (requests are pull-only: G requests out bound the inbox's map
  payload by G x 8 MiB).

**GUI-thread costs that scale with answers.** One `LOG_INFO` line per map answer (7-16 ms stalls in 22 of 7,488
answers, `04b` 14); about 420 log lines per `terrainTileLoaded`; every `worldProgress` (4 Hz per load) runs the Maps
panel's whole `changed()` update (median 6.9 ms, max 58 ms, `05` 13). Nine loads would break the 50 ms rule.

**Textures.**
- Terrain layer textures already upload their stored DXT1/3/5 blocks with the stored mip chain (`WmvTerrainTexture`),
  per tile, not shared between tiles.
- World-model and M2 textures are decoded to RGBA32 on the worker, flipped on the main thread into a new copy, and
  uploaded with Unity-generated mips (`WmvModelBuilder.CreateTexture`). The model path flips V twice (UVs `v = 1 - y`
  and the pixel rows), so a block upload needs the blocks flipped too (exact: reverse block rows, reverse the four
  pixel rows inside each block).

**Seams and origin.** Tile meshes are built relative to the tile's own corner; with one shared origin the edge x/z
are computed from integer indices in double and so agree exactly between tiles; heights come from each tile's MCNK
(the audit found them equal to 5e-5 yd at 12,387 seam points; bit-equality UNKNOWN). Edge normals are one-sided
(each tile accumulates over its own grid only); the default textured view is unlit, the shaded views are not.
With the origin fixed at the entry tile's corner the float error stays under 6.1e-5 yd anywhere in a 3 x 3 and
under 2.4e-4 yd within 8 tiles.

**Harnesses.** No step moves the camera across tiles over time (`target:` is clamped to the shown tile); the host never
learns the live camera; samples are taken only at explicit steps; GUI rounds are recorded but never asserted; every
expectation assumes one load (`expect:terrain:n` needs `terrainView == n`, a 0/1 flag).

### 1.3 The region and its census (PROVEN_BYTES)

`Tools/world-audit/world08_density.py` counted MODF and MDDF placements per tile of Azeroth columns 28-34, rows 44-51
on the four clients. The acceptance region is the 3 x 3 centred on 31_48: tiles 30-32 x 47-49, Stormwind's Lion's
Rest (30_47), Old Town (31_48), the Cathedral and Trade districts and the edge of Elwynn Forest -- the most world
models of any window on Classic Era (where Stormwind is one world model on 30_47) and the most doodads of the windows
that contain the city on every client; it also holds both tiles of Stage 07.

`Tools/world-audit/world08_corpus.py` then extracted what a load of the region reads (whole: ADTs, world-model roots,
M2s; size only: LOD0 groups and skins; the 148-byte header of every texture):

| | Retail | Classic Era | MoP Classic | Classic Beta |
|---|---|---|---|---|
| MDDF doodads / MODF placements (unique) | 9,723 / 56 | 3,942 / 22 | 8,550 / 49 | 4,516 / 31 |
| ... listed by more than one tile | 169 / 15 | 107 / 8 | 174 / 15 | 110 / 8 |
| WMO-set doodads (set 0 + the placement's) | 8,754 | 7,734 | 7,132 | 7,948 |
| Unique world models / LOD0 groups | 36 / 515 | 16 / 400 | 31 / 532 | 17 / 380 |
| Unique M2s (+ as many skins) | 1,283 | 430 | 715 | 481 |
| Textures (terrain + WMO + M2) | 2,582 | 668 | 1,045 | 869 |
| Files a region load reads | 5,727 | 1,972 | 3,066 | 2,256 |
| Bytes read | 453.7 MB | 87.6 MB | 145.6 MB | 146.7 MB |
| Texture bytes as stored (mip chains as in the files) | 330.9 MB | 20.0 MB | 59.2 MB | 64.2 MB |
| Textures as today's RGBA32 with a full mip chain | 1,862.2 MB | 133.1 MB | 335.2 MB | 525.3 MB |

Texture encodings in the region: DXT1 1,709 / DXT3 45 / DXT5 826 / palettized 2 (Retail); every DXT file is whole
4 x 4 blocks at the top level. What decides the block upload is the stored mip chain:
- **Full stored chain** (every level down to 1 x 1): Retail 1,902 files (290.6 MB stored, 1,432.6 MB as RGBA32),
  Classic Era 665, MoP Classic 1,041, Classic Beta 808. These upload as their own blocks, exactly as stored.
- **Top level only** (has-mips flag 0, one stored level): Retail 678 world-model textures (527 of them 256 x 256,
  others up to 1024 x 1024 and non-power-of-two 768 x 512), Classic Beta 60 (some 1792 x 1536: 327.2 MB as RGBA32),
  none on Era and MoP. Uploading only the stored level without mips would shimmer at a distance (an accuracy loss),
  and Unity cannot generate mips for a compressed format; these keep today's path (decode, RGBA32, generated mips)
  and are named in the diagnostics.
- **Palettized and BGRA**: 2-4 files per client; decoded to RGBA32 as today.

So with block upload the region's textures cost on the GPU about 720 MB on Retail (290.6 MB of blocks + 429.6 MB of
the top-level-only files as RGBA32), 20 MB on Era, 59 MB on MoP and 361 MB on Beta, against 1,862 / 133 / 335 / 525 MB
today. Further reduction of the top-level-only files (re-encoding their generated mips on the worker) is lossy and is
not part of this stage (section 1.11).

### 1.4 The design in one page

```
 host (GUI thread + map asset worker)                         player (main thread + thread pool)
 ------------------------------------                         ----------------------------------
 loadWorld{streaming}  --------------------------------------> StreamSession (one per world entry)
                                                                 RegionManager: camera tile -> wanted 3x3,
 StreamSession                                                    keep zone 5x5, resident cap
   session record: map, epoch, budgets,                          TileJob per wanted tile (terrain -> world
   shared resource ids (WMO, M2, textures)                         models -> doodads), priority by ring/view
   TileRecord per open tile (root, tex0, obj0)  <---- streamTile{open|close}
                                                                 Scheduler: one request credit G for all
 queue: priority-ordered, sized >= G  <---------- getMapAsset{tile, priority}   jobs, nearest first
 worker: reads, cancel between reads  <---------- cancelMapAssets / releaseMapAssets
 transport cap: pause receive, drop non-reader                  Scene residency (ref-counted):
                                                                   placements by uniqueId -> tiles
 assetResponse (binary)  -----------------------------------------> prototypes by FileDataID -> instances
                                                                   textures by key -> holders; LRU pools
 Maps panel: resident tiles, files, memory  <---- streamState (4 Hz)   Uploads: stored DXT blocks (flipped on
                                                                   the worker), per-frame time budget
```

- **The player decides the region** (it alone knows the camera); the host authorises tiles of the session's map and
  serves their files. Tiles open and close by explicit messages, so the host can cancel a closed tile's reads.
- **Tile files are per tile, everything shared is per session.** root, tex0 and obj0 belong to a tile record;
  world-model roots, groups and textures, M2s, skins and textures, and terrain layer textures belong to the session, so a
  file needed by two tiles is fetched once, and closing one tile cannot cancel what the other still waits for.
- **One request credit for the whole region** (G outstanding requests), spent nearest-first; the host's queue is sized
  above G and ordered by the request's priority, so no file is ever refused for queue space.
- **Residency by reference counts**, with a pool of unreferenced resources kept under byte budgets (least recently
  used first out), so leaving a tile and coming back does not refetch, and memory stays bounded.

### 1.5 Transport backpressure (requirement 1, first)

The cap is transport-wide and all-or-nothing (dropping single messages would break the invariants: every request is
answered exactly once; `m_sentImages` would claim images the player never got):

1. **Pause receiving** while `unsentBytes()` is above a high mark (32 MiB) until it falls below a low mark (8 MiB):
   `pollReceive` is skipped, so no new request can produce an answer, and TCP pushes back on the client. With the
   shipped player this never engages in practice (its reader thread always reads); against a client that does not read
   it stops all request-driven growth: what is left are the reads already in flight (at most the in-flight cap x 8
   MiB) and the viewer's own pushes.
2. **Drop a client that does not read**: when `unsentBytes()` exceeds a hard cap (256 MiB), or bytes have been waiting
   with no progress of `send()` for 30 s, the connection is dropped (logged with the reason). For the shipped player
   that is the end of the player (it quits on end of stream) and the viewer shows its existing restart notice; a player
   that has not read for 30 s or let 256 MiB pile up is hung.
3. **Bound the GUI-thread work per poll**: at most 64 lines handled per `pollReceive` (the rest wait for the next poll);
   the per-answer log line becomes a debug line, with one summary line per second of serving.
4. **Accounting**: `unsentBytes()` stays user-space bytes; the kernel holds up to the 64 MiB `SO_SNDBUF` more (Windows has
   no query for it). The bound on host memory is therefore the hard cap plus the largest single append (one asset,
   64 MiB), plus 64 MiB in the kernel.
5. **The player side** (where the streaming backlog would really pile up): the region's request credit G bounds the
   inbox's map payload; `WmvIpcClient.Update` gets a dispatch budget (messages are dispatched until 8 ms are spent, the
   rest next frame, order kept).
6. **Test with a real socket**: an in-process host check starts a real `UnityIpcServer`, connects a raw client with a
   small receive buffer that never reads, and floods the ungated paths (character images, map requests answered, unknown
   requests): the buffer must stop growing at the marks, receiving must pause and resume, and the client must be dropped
   at the cap or the stall timeout. Today only `testFakeUnsentBytes` exists (`04b` 18.2: "no run had a real socket that
   stopped being read").

### 1.6 Protocol 18

A protocol-17 player keeps exactly the single-tile behaviour (loadWorld without `streaming`); a protocol-18 player is
offered streaming only by `loadWorld` with a `streaming` object. New and changed messages:

| Message | Direction | Content |
|---|---|---|
| `loadWorld.streaming` | host -> player | radius (1: 3 x 3), keep radius (2), max resident tiles, request credit, the session's budgets |
| `streamTile` | player -> host | `{load, open: [{tile, x, y}], close: [tile]}`: tiles the region opens and closes (a tile id is the player's, per session) |
| `getMapAsset` (+ fields) | player -> host | `tile` (for root, tex0, obj0), `priority` (smaller is sooner) |
| `cancelMapAssets` | player -> host | `{load, requestIds}`: requests no tile wants any more (queued: answered "cancelled"; in flight: stale) |
| `releaseMapAssets` | player -> host | `{load, role, ids}`: shared files the player evicted, so the host allows them to be read again |
| `streamState` | player -> host, at most 4 Hz | camera position and tile; per tile: state, files asked/answered, bytes; requests out; residency (prototypes, textures, pools, GPU and CPU bytes by family), evictions, failures; frame times since the last message |
| `terrainTileLoaded` | player -> host | unchanged; sent for the entry tile when it settles (the Maps panel, the camera and the harnesses keep working); neighbours report through `streamState` and a compact `tileStreamed` event |

### 1.7 Host: the session, its tiles, the queue

- `m_terrainRequest` stays the session record (load, epoch, map, catalogue, budgets, the WDT served once, the shared
  resource id sets and byte counters); a `std::map<int, TileRecord>` holds the open tiles (x, y, root/tex0/obj0 asked
  and served). For a non-streaming load the entry tile is the only tile record, so `decideMapAssetRequest` and its 110
  table cases keep their meaning.
- **Decisions**: tile roles (root, tex0, obj0) need an open tile record and keep their order rules within it; the WDT is
  read once per session; shared roles keep their order rules session-wide (a world-model root after any obj0 was
  served, groups and textures after their root, ...); "each id once" becomes "once until released" (the player's
  `releaseMapAssets` removes an id); the byte budgets count bytes read minus bytes released (a residency mirror), and
  a per-session read count bounds re-reads.
- **Staleness and cancellation**: a request of a closed tile is stale; closing a tile cancels its queued reads at once
  and its reads in flight through the existing per-poll stale check; `cancelMapAssets` does the same per request.
- **Queue**: `MAP_ASSET_QUEUE_MAX` from 16 to 64 (the player's credit G is 24, so a refusal for queue space cannot
  happen to the shipped player), dispatched lowest `priority` first (stable among equals); reads in flight from 2 to 4
  (one worker: in flight means submitted, the worker reads back to back). The Maps viewer's own reads (minimaps) are
  submitted only when no player read is waiting.
- **GUI thread**: per-answer log lines become debug lines (a once-a-second summary instead); the Maps panel's update on
  `streamState` is coalesced to 4 Hz and touches labels and the overhead map only (no `Layout()`); the per-report log of
  neighbour tiles is not written (their compact event is).

### 1.8 Player: session, jobs, residency

- **`WmvTerrainSession` keeps its single-load code path** for protocol-17 hosts, WMO-only maps and the harnesses; a
  streaming load runs the same per-tile pipeline (WDT -> root/tex0/obj0 -> mesh and material -> world models ->
  doodads) as one job per tile in a job table, with these changes:
  - the WDT is fetched and parsed once per session;
  - a job's commit adds its tile to the scene instead of replacing the scene; the scene and its origin (the entry tile's
    north-west corner) live as long as the session;
  - worker continuations are applied if their job is still alive (not only if it is `current`);
  - answers are routed by request id to their job (a dictionary, not a search of `current`);
  - the retired-request ring grows to 1,024 (a 3 x 3 cancel can retire about 100 requests at once).
- **Region manager**: each frame the camera's tile; wanted = the tiles of the 3 x 3 that exist in the WDT; a resident
  tile is kept while it is within the 5 x 5 keep zone, closed beyond it, and the farthest beyond the 3 x 3 are closed
  first when more than 16 tiles would be resident. Tiles open in order of distance, then in front of the camera first.
- **Scheduler**: one request credit G = 24 for every job of the session; jobs advance in priority order each frame, so
  the nearest tiles take the credit first; families keep their order inside a tile (terrain, then world models, then
  doodads), but a near tile's doodads come before a far tile's terrain.
- **Placements by uniqueId**: a scene registry `(kind, uniqueId) -> entry {instance, tiles}`: the first tile that lists a
  placement creates it, the others add themselves; it is destroyed when its last tile closes. WMO-set doodads belong to
  their placement's entry.
- **Prototypes by FileDataID** (world models, M2s): reference counts = live instances + jobs waiting on the build; a
  resident prototype is used directly (no fetch); a prototype with no reference goes to an LRU pool.
- **Textures by key**: a scene texture cache for world-model and M2 textures, keyed (FileDataID, alpha kept or dropped,
  wrap U, wrap V, colour domain), and a terrain texture cache keyed by FileDataID; holders acquire and release; an
  unreferenced texture goes to the LRU pool.
- **Pools and eviction**: unreferenced prototypes and textures stay resident while the pools fit their budgets
  (section 1.12) and leave least recently used first; the host is told what left (`releaseMapAssets`) so a later
  request is allowed.
- **Per-frame work**: one time budget for the session's main-thread work (tile builds, uploads, prototype builds,
  placements, disposals), with at least one item per frame so loading always advances; the tile build is split into
  batches of chunks (today 256 chunks and every texture in one call).
- **Camera**: the free camera is never moved by streaming; only the entry tile's settle places it (as today). Ground
  queries use the resident tile under the point.
- **Seams**: every tile's mesh is built against the scene origin; a corpus test compares the heights of neighbouring
  tiles' shared edge vertices on the four clients (bit-equal or not, and by how much).

### 1.9 Block-compressed uploads (requirement 5)

- A prepared texture made on the worker: the BLP header, its format, and either the stored DXT levels (flipped for the
  model convention, exact) or decoded RGBA32 rows (flipped on the worker instead of the main thread), with the exact GPU
  byte count.
- Upload: `RGBA_DXT1/3/5_UNorm` (or `_SRGB` when the colour domain asks for it) with the stored levels via
  `SetPixelData`, when the GPU samples the format and the file has a full stored chain of whole blocks; otherwise RGBA32
  as today (`Apply(true)` generates the mips).
- Used by the world paths only (world-model prototypes through a new texture provider, M2 doodads through the existing
  one). The Models view, Characters and Buildings keep RGBA32 (their pinned captures and the self-test's texel reads
  stay as they are).
- Expected differences, to be measured and shown: the GPU decodes DXT with its own interpolation (`04` measured up to
  5.3/255 off the exact ramp), where today's CPU decode truncates; the stored mips replace Unity's box-filtered ones
  (the stored mips are the ones the files carry).
- DXT1 three-colour blocks whose texture is drawn with its alpha dropped: today's path forces alpha to 255 on decoded
  black texels; the block path keeps the format's (0, 0, 0, 0) and relies on such materials not reading alpha
  (to be checked on the shader, else these files decode).

### 1.10 Diagnostics and progress by file count (requirement 6)

- `streamState` (4 Hz) carries the counts the panel shows and the harness samples (section 1.6). The Maps panel shows a
  streaming line ("Tiles: 9 resident, 0 loading, 1 closing. Files 1,840 of 2,013 asked answered (212.4 MB). Requests: 24
  out, 3 waiting on the host. GPU: 412 MB textures, 61 MB geometry. Pools: 37 MB. Evictions 12.") and colours the
  resident, loading and failed tiles on the overhead map with the live camera marker.
- The counts are the files actually asked and answered; a denominator grows as discovery proceeds and is labelled so
  ("of N asked so far"), never an estimate.
- `runtimeState` gains the same fields (camera, tiles, requests, residency) and a light mode without the four
  `FindObjectsOfTypeAll` scans.

### 1.11 Not in this stage

Re-encoding mips for top-level-only textures; a second host reader thread (CascLib allows several readers, cost
UNKNOWN); far terrain (milestone 13); lighting of seams (edge normals stay one-sided: the default view is unlit);
re-basing the origin beyond 8 tiles from the entry (precision stays under 2.4e-4 yd within 8 tiles, documented);
liquids, lighting, effects, sky (milestones 09-12).

### 1.12 Budgets (measured against in sections 8 and 9)

| What | Budget | Why |
|---|---|---|
| Host GUI thread | no round over 50 ms during the acceptance flight | roadmap exit |
| Host unsent bytes | receive paused at 32 MiB (resumed at 8 MiB), client dropped at 256 MiB or 30 s without progress | 1.5 |
| Host queue / reads in flight | 64 / 4 | above the player's credit |
| Player request credit | 24 outstanding for the whole session | about 6 x today's per-family 4; inbox map payload under 24 x 8 MiB |
| Region | 3 x 3 wanted, 5 x 5 keep zone, at most 16 resident | 1.8 |
| Player main thread | 8 ms of streaming work per frame (at least one item) | keeps 60 Hz frames mostly whole |
| GPU textures (scene) | 1.5 GiB, checked against the actual upload bytes | Retail region 720 MB with blocks, 1,862 MB without |
| GPU geometry (scene) | 512 MiB | Retail dense-city estimate 103 MB (`00-audit` 7.1) |
| Pools (unreferenced prototypes and textures) | 256 MiB GPU, 128 MiB managed | re-entry without refetch, bounded |
| Region load time | measured, not budgeted: Retail reads 5,727 files | today's rate (100 files/s) would take about a minute |
| Memory over repeated traversal | private bytes level after the first cycles; every live tally back after Clear | `07` 10 |

### 1.13 Risks

- **Size of the change.** The single-load assumptions run through the host, the protocol, the player and both
  harnesses; the single-tile path must stay byte-identical for protocol 17 and for the regressions (Stages 03-07).
  Mitigation: streaming is a separate mode of the same session classes, entered only by `loadWorld.streaming`.
- **GUI stalls** from answer volume (5,727 answers for the Retail region): per-poll budgets exist; the log lines and
  panel updates are the known costs.
- **Throughput** with one reader and the IPC round trip: UNKNOWN until measured; the credit and in-flight changes are
  the levers.
- **Player frame spikes**: an Era Stormwind prototype build took 118 ms in one frame (`06` 15); world-model builds are
  not split in this stage (recorded, not hidden).
- **Visual differences of block uploads**: measured on pictures, not claimed equal.
- **Client data changing between runs** (seen on 2026-10-17): every comparison names its client build.

### 1.14 Order of work

1. Transport backpressure (host) with the real-socket test.
2. Prepared textures and block uploads (player), with pure tests of the block flip against the decoder.
3. Protocol 18 and the host session (tile records, shared resources, queue order, cancel and release, GUI costs).
4. The player's streaming session (job table, region manager, scheduler, residency, pools, per-frame budget).
5. Diagnostics: `streamState`, `runtimeState`, the Maps panel.
6. Harness: a scripted flight path, continuous sampling, streaming expectations, the GUI-round assertion.
7. Runs on four clients, memory cycles, pictures, regressions; then the adversarial review and the commit.

### 1.15 Where the build departs from this proposal

Settled while building it (section 2 onwards describes what was built):

- **Tile loads instead of tile records** (1.6, 1.7). Each streamed tile is a load of its own on the host: the player
  asks
  for it (`streamTile` open), the host allocates its load id from the same serial as every load, with the entry's map,
  client epoch, catalogue and budgets, and keeps it in a map beside the entry load. Closing a tile erases its load, so
  the existing stale check cancels whatever it still had queued or in flight -- no `cancelMapAssets`, no
  `releaseMapAssets`: a file the player evicted is asked for again under a later tile's load (each load serves an id
  once, so this is allowed by construction). The entry's `decideMapAssetRequest` cases keep their meaning; the only
  change to the rules is that a session's order prerequisites count session-wide (a group, a world-model texture, a skin
  or a doodad texture may be asked under a tile load once ANY load of the session was served the root or model they
  follow -- the case of a shared file taken over from a closed tile).
- **The queue's room and reads in flight change only during a session** (64 and 4; 16 and 2 otherwise), so the queue
  tests of `04b` keep their meaning; the per-answer log line becomes a 2-second summary only during a session
  (`WMV_LOG_MAP_ANSWERS=1` keeps it).
- **The retired-request ring stays at 128**: the session's credit caps every request out at 24 for the whole session,
  so no close can retire more than 24 at once.
- **Texture images shared across tiles** (not in the proposal): a world-model or doodad texture is fetched and decoded
  once per session for every tile whose prototypes sample it (section 5.6); without it 7.2 % of a Retail region's files
  were asked twice.
- **A closed tile's objects are destroyed over the next frames** (not in the proposal): three tiles closing together
  destroyed 9,081 instances in one frame (a 52 ms frame); they are now released at once and destroyed at least 100 and
  then up to 1.5 ms a frame.
- **The acceptance flight is flown by the player** (a test hook, `testFlight`): the free camera follows the path frame
  by frame as the user's keys would move it, so the viewer sees exactly what a user's flight sends -- streamState and
  the session's files -- and no per-step camera message.
- **A tile's Unity objects are built in one piece** (1.8 planned batches of chunks): adding a tile -- its 256 chunk
  objects, alpha array and materials -- takes 4-29 ms on Retail in one frame. Heavy items (a tile added or rebuilt, a
  world-model prototype built from its files) run only as a frame's first item, so they no longer stack behind other
  work; splitting the tile build is left for later (section 11).
- **The streaming lines are always shown** (an idle text when no session streams), at a fixed height, and follow a
  streamState on a 150 ms timer: their first appearance, and then the streamState's own update, landed in the Enter
  world click's repaint round (section 10.2).
- **Two budgets of 1.12 restated.** The host GUI thread's is the roadmap exit as written -- no round over 50 ms --,
  measured from Enter world on and over the flight separately (section 8), not "during the acceptance flight" only. The
  pools' reason holds only while what a route revisits fits them: re-entry without refetch is not achieved on the Retail
  Stormwind loop (section 8.1).

## 2. Transport backpressure (requirement 1; built first)

`UnityIpcServer` (host). Measured by the in-process `-terraintile` checks with a real loopback socket
(`checkTransportCap`, `UnityIpcTestClient`: a raw client that can stop reading):

| Rule | Value | Check (tt_s1, 2026-10-17 09:07) |
|---|---|---|
| Lines handled per poll | at most 64 (the rest next poll, order kept) | 40 lines with a budget of 8: the first poll handled 8, all 40 handled over 5 polls, 40 answers read |
| Receiving paused | above 32 MiB unsent, resumed at 8 MiB | test limits 1 MiB / 256 KiB: paused at 1,310,995 bytes waiting, 0 lines handled while paused, resumed, 5 answers |
| Client dropped: hard cap | above 256 MiB unsent | test cap 4 MiB: a client that never reads dropped after 9 pushes of 512 KiB, the most ever waiting 3,670,456 bytes (<= 4 MiB), 1 message not queued |
| Client dropped: stall | 30 s with bytes waiting and no send progress | test 1,500 ms: dropped after 1,528 ms with 55 bytes waiting |
| A client that reads | never paused or dropped | 8,388,608 of 8,388,608 payload bytes received, 0 pauses, 0 drops |

The pause is checked again after every line handled (a line answered on the spot -- a `getAsset` -- must not take
what waits past the mark within one poll; found by the review, section 10). The hard-cap and stall drops are decided
where
they are found and carried out at a safe point of the poll (`dropPendingClient`); a send that fails (the connection
reset) drops the client at once, as before this stage, so no code holds a queue iterator across a send: the stale sweep
takes its entries out of the queue first and answers them afterwards (section 10.2). The player's side:
`WmvIpcClient.Update` dispatches the inbox until 8 ms are spent (at least one message a frame,
order kept), counted (`InboxDepthMax`, `InboxFramesDeferred`).

## 3. Block-compressed uploads (requirement 5)

`Wow/BlpBlocks.cs` (pure), `WmvModelBuilder.CreateBlockTexture`, the world-model and doodad texture paths.

- **The flip.** The model path flips V twice (the UVs and the pixel rows), so a block upload needs the blocks flipped:
  the block rows reversed, and inside each block the four pixel rows reversed -- DXT1/colour: the index bytes 4..7;
  DXT3: the four 16-bit alpha rows; DXT5: the four 12-bit rows of the 48-bit alpha index. Exact by construction and by
  test (below).
- **Eligibility.** DXT1/3/5 the GPU samples, whole 4 x 4 blocks at the top level, the FULL stored mip chain, every
  level's height a multiple of 4 or below 4 (a 12-tall level's 6-tall child would put its padding rows wrong). Anything
  else -- a top-level-only file (has-mips 0: Retail 678 world-model textures of the region), palettized, BGRA, a file
  whose mip table fails (below) -- keeps the previous path: decoded RGBA32, mips generated by Unity.
- **Upload.** `RGBA_DXT1/3/5_UNorm` (`_SRGB` when `AuthoredTextureDomain` is false), each stored level by
  `SetPixelData`, `Apply(false, true)`. One upload serves both the alpha-kept and alpha-dropped uses of a file (with the
  alpha dropped the shader writes 1 and reads none), so a block file has one cache key without the drop bit.
- **Where.** World-model prototypes (`WmvWmoBuilder.UploadTexture`) and doodads (`WmvDoodadTextures`, refcounted).
  The Models view, Characters and Buildings keep RGBA32.
- **Tests.** Pure (`Tests/BlpBlockTests.cs`): 96 synthetic files / 540 levels flip exactly (every level height down to 1
  x 1, both DXT5 alpha modes); every whole BLP2 file of the Stage 07 corpus on the four clients flips to the decoder's
  own
  level upside down -- Retail 1,194 supported files / 10,448 levels, Era 469 / 3,838, MoP 723 / 6,084, Beta 641 / 5,294:
  0 differ. Self-test: a DXT1 file uploaded as blocks and as RGBA32 reads back from the GPU 0/255 apart, its red band on
  top (the flip's direction).
- **A regression found and fixed later (step 4).** Preparing a file read its whole mip table first; a file whose small
  mip is stored shorter than its size needs ("dxt1 mip 5 holds 8 bytes, needs 16 for 8x2") then failed, where the
  previous path decoded level 0 and never looked at the small mips -- Retail 8, Era 42, MoP 8, Beta 8 files of the
  corpus; on the Stormwind region it left 4 doodad models "partial". Such a file now decodes from level 0 as before
  (`BlocksNotUsed` says why); a pure test and a corpus count were added (0 failures).
- **Measured** (Retail 30_47, the same host and cameras, client 12.1.5.70448; `w08\blocks1`):

| | Stage 07 player | Stage 08 player |
|---|---|---|
| Doodad textures on the GPU | 283.0 MiB | 52.2 MiB |
| World-model textures on the GPU | 171.3 MiB | 27.9 MiB |
| Doodad texture uploads | 548 | 525 (the alpha variants of a block file share one) |
| Longest frame of the last 600 | 42.07 ms | 16.93 ms |
| Decoded pixels held at the most | 26.0 MB | 7.3 MB |

  Pictures (viewport crops, same cameras): mean absolute difference 0.6-0.95/255 in the close and outdoor views (at
  most 2.38 % of pixels more than 8/255 apart); 2.22/255 in the dense view from above (11.92 % > 8/255, 1.55 % >
  24/255).
  The difference is distant alpha-tested foliage: the files' own stored mips against Unity's box-filtered ones
  (`blocks1\compare_05_dense.png`). Not claimed equal; not claimed closer to the game (no in-game reference of these
  views).

## 4. Protocol 18 and the host's session

| Message | Direction | Content |
|---|---|---|
| `loadWorld.streaming` | host -> player | `{radius 1, keepRadius 2, maxResidentTiles 16, requestCredit 24, maxTileLoads 24}`, sent only to a protocol-18 player when the Maps viewer's Enter world asks for it (`WMV_STREAMING=0` turns it off) |
| `streamTile` | player -> host | `{load, open: [{x, y}], close: [tileLoad]}` |
| `streamTileAnswer` | host -> player | `{load, opened: [{x, y, tileLoad}], refused: [{x, y, refused}], closed: [tileLoad], assetEpoch}` |
| `getMapAsset` | player -> host | gains `tile {x, y}` and `priority` (smaller sooner: 1 + 3 x ring + family -- the tile's own files 0, world models 1, doodads 2); the queue inserts by priority, first come first served among equals (requests without one keep arrival order) |
| `streamState` | player -> host, at most every 250 ms | the camera and its tile; per tile its load, state, files asked/answered, bytes, placements and doodads; requests out; prototypes and textures resident and pooled; instances; GPU texture and geometry bytes (terrain alpha arrays and meshes included); pool bytes; evictions; the longest frame since the last one, the 95th percentile of the last 120 frames, frames over 33 / 50 ms; the longest streaming work and terrain tile build since the last one; the managed heap |
| `runtimeState` | both | gains the session's tiles, jobs, requests, placements, doodads, scene terrain textures, evictions, files asked again, images shared; `light` skips the four whole-player object scans (implemented on both sides; nothing asks for it yet) |
| `testFlight` / `testFlightDone` | host -> player / back | TEST hook: a path of world points, a speed and a pitch, flown by the free camera |

Host (`ModelViewer`): `m_streamLoads` (tile loads of the session, `std::map<int, TerrainRequest>`), `terrainRequestFor`
(the request's own load, entry or tile), `ResetTerrainRequests` (every place that cleared the one request now ends the
session too: a new load, a clear, a model load, a client change, a player restart), `OnStreamTile` (closes first, then
opens: a tile of the session's map the client has -- the map catalogue's own WDT check, no read on the GUI thread --
not the entry, not already open, at most 24 open), `noteStreamSessionServed`. In-process checks (`-terraintile`,
tt_s3b): the queue's room 64/4 during a session and 16/2 after; dispatch order by priority; 64 wait and the 65th is
refused with the room named; tile loads open with the next serials and the entry's budgets; the entry tile, a duplicate,
a tile off the grid and a load that is not the session's are refused with why; a tile load's read is accounted to it;
a closed tile load's requests are stale; the cap of 24; closing the entry ends it; the session's prerequisites
session-wide and a tile opened later inherits them; ResetTerrainRequests ends it all. **236 passed, 0 failed, 1
skipped** (the encrypted-file case, Beta only; tt_s8a on the final host).

After the review (section 10): during a session a request still waiting for a load that has ended (a closed tile's) is
answered stale at once rather than when it reaches the queue's front -- the player took its credit back at the close --
and so is everything of the session's when it ends; an entry reported anything but "built" ends the host's session too
(the player has ended its own).

## 5. The player's streaming session

`WmvTerrainSessionStream.cs` (new) and the session's three files. A load without `streaming` runs exactly as before:
`Live(j)` replaces the session's `j == current` tests and is that test for a single-tile load.

### 5.1 Jobs and the region

- The entry is the load in flight (`current`); once its tile is committed the region follows the free camera (not
  flying -- the overview -- it stays where the camera last was; never flown, around the entry's tile): the tiles within
  radius 1 of the camera's tile that the WDT has are wanted, a tile is kept while within 2, at most 16 resident (the
  farthest beyond 1 closed first). Tiles open nearest first, in front of the camera first among equals; one
  `streamTile` is out at a time, a message of closes only too. The camera's world position comes from the scene's root
  and origin, so the region keeps following it when every built tile has closed behind it (a sea gap, a fast flight);
  the first tile built after that is the shown one.
- One job per opened tile: the entry's pipeline with the session's WDT (parsed once): root, tex0, obj0 -> its terrain,
  built against the SCENE'S origin (the entry tile's north-west corner) -> ADDED to the scene (nothing disposed) ->
  its world models -> its doodads -> settled (no `terrainTileLoaded` of its own; `streamState` says it).
- Answers are routed to their job by request id. A tile's timeouts (its files 30 s, its world models 120 s, its doodads
  180 s) run only while requests of its own are out and restart with each answer: a tile waiting for the credit, the
  frame budget, its workers or another tile's fetch is not late.
- The entry closed by the region after its commit, its world models or doodads still coming, is reported "built" with
  why (it was on screen) and the camera it carried is placed at once; the session goes on.

### 5.2 Credit, priorities and frames

- At most 24 requests out for the whole session; a tile of ring 1 leaves 4 for the nearer ones, a tile of ring 2 or more
  leaves 8; the jobs advance nearest first every frame.
- One budget of 8 ms of main-thread work a frame for the whole session -- a tile's Unity objects, a prototype build,
  placements, a rebuild, evictions -- at least one item a frame (nearest tile first). A heavy item (a tile's Unity
  objects added or rebuilt, a world-model prototype built from its files) runs only as the frame's first item. A
  world-model prototype is still built in one frame: 48-50 ms for the largest of the Retail region, and the frames at
  the
  Era and Beta entries reached 126 and 137 ms (Stage 07 built them the same way, 114-128 ms); not split in this stage.

### 5.3 Placements by uniqueId

A world-model placement is keyed by (FileDataID, uniqueId), an MDDF doodad likewise, a world model's MODD doodad by its
placement and MODD index. The first tile that places one makes it; every tile that lists it holds it; it is destroyed
with its last holder. Self-test: placement 500 listed by two tiles and doodad 600 listed by two tiles placed once each;
on Retail's region 25 placements and 12,540 doodads were found shared rather than placed twice.

### 5.4 Prototypes by FileDataID

A world model or M2 is fetched and built once per session: the first job that needs it owns the fetch; a job that needs
it meanwhile waits (a near tile takes over a far tile's fetch that has not begun); when the owner's job ends first the
first waiting job takes the fetch over under its own tile load; a file that fails for good (not in storage, cannot be
parsed) fails every waiting model with the same reason, its own state kept (an "unsupported" model stays unsupported);
a refusal for the owner's tile load (its budgets, the queue's room) or a stale answer is that tile's failure only, and
the fetch is handed on. (A texture refused or capped for the owner's load is not handed on: the prototype is built
without it and shared; latent, section 11.) A built prototype stays while it has instances or a job will place from it;
with neither it is
POOLED.

### 5.5 Terrain textures

Uploaded once per FileDataID and bound by every tile that uses it: a tile's material asks only for what the scene does
not hold (a resident copy of the texture's facts stands in its content); its own uploads are handed to the scene after
its build. Refcounted by tiles (and jobs about to bind); unreferenced ones are pooled.

### 5.6 World-model and doodad texture images

Fetched and decoded once per session for every tile whose prototypes sample them: the same owner / waiting / take-over
scheme as prototypes; the decoded images are kept, least recently used out first, within 96 MiB of managed memory, for
the prototypes built later -- never one a live tile took without its file's bytes (a later model of that tile may sample
it again and it has nothing to decode it from). A fetch the host did not serve is handed to the next waiting job; a
file that came and could not be decoded -- or the file's own failure (not in client storage, too large, no bytes) --
fails them all, asked for once (a refusal for the tile's load or a stale answer is handed on). Retail region: files
asked twice 367 -> 43 (the 43: 15 terrain textures two tiles
fetched before either was committed, 28 images evicted and needed again).

### 5.7 Pools and eviction

What no tile holds stays pooled -- prototypes (world models with their textures; M2s' geometry), the M2 texture uploads
no live prototype holds (held only by pooled prototypes, or by none), terrain textures -- while the pools fit 256 MiB of
GPU bytes and 128 MiB of parsed world-model files. Over budget the uploads no prototype holds go first (their prototypes
have gone), then the least recently used prototypes and terrain textures -- an M2 prototype with the uploads only it
held, so what is taken off the pool is exactly what its eviction frees; over the parsed files' budget alone only world
models go (nothing else holds parsed files). Evictions are main-thread work under the frame budget (at least one a
frame; the rest in the next frames -- the pools' budget is soft), and the pools are looked at when something may have
joined them (a tile closed, a job ended) or an eviction is unfinished, and twice a second besides. The scene's GPU
budgets (1.5 GiB of textures, 512 MiB of geometry) are checked before a doodad prototype is
built, against everything on the GPU then. Self-test: a corner tile's M2, world model and terrain texture stay pooled
while it is closed and serve it again with nothing asked; with no pool budget they are evicted at its close and asked
again when it returns.

### 5.8 Closing a tile

Its job ends if it is still running (its requests retired: their answers are dropped as late; a fetch it owned is handed
on; the entry, if still in flight after its commit, is reported "built" with why); its holdings go (an instance no
other tile holds is destroyed); its terrain goes; the host is told (its tile load closed: what it still had queued is
answered stale at once, what was in flight when it completes). The objects of a closed tile -- its terrain's, its
doodads' -- are released at once (off every tally; the terrain hidden, each doodad's renderer off) and destroyed over
the next frames (at least 100 a frame, then until 1.5 ms).

### 5.9 Seams and the origin

Every tile's mesh is built against the entry tile's corner, so the edge x and z of neighbouring tiles come from the same
double expression and the same float cast. Corpus test (`Tests/StreamSeamTests.cs`, the 12 seams of the region on the
four clients): 1,548 edge vertices each, every one at the bit-equal x and z of its neighbour's; heights bit-equal at
1,546 (Retail), 1,548 (Era), 1,546 (MoP), 1,547 (Beta) of them, the largest difference 0.000008 yd (one float step of
the stored heights) -- no gap. Edge normals stay one-sided (the default textured view is unlit). The origin is never
re-based (within 8 tiles of the entry the float error stays under 2.4e-4 yd).

### 5.10 The view

`setTerrainView` in a session draws the shown tile again at once (as before) and every other resident tile one a frame
from its kept mesh and content (a tile's material and texture facts are kept while it is resident; a texture's data
only while a view may still upload it).

## 6. Diagnostics (requirement 6)

- **Maps panel**: four short streaming lines under the tile's, always shown at a fixed height and never wrapped
  (ellipsized at the pane's width; the whole text in their tooltip), so a `streamState` relabels them and lays nothing
  out -- on a 150 ms timer, at most once in that time and never in the GUI round the message arrived in. The counts come
  first, so they survive a narrow pane: "Tiles: 9 resident, 0 loading (around 31_48)" / "0 out;
  files 4766 of 4766 asked so far (372... MB)" / "GPU 321.8 MB textures, 79.3 MB geometry" / "Pools 0.0 MB; evicted 0
  model(s), 0 textu..." (Retail, the region settled, as the pane shows them: `compat\ab_exp_wow_2_entered.png`). The
  counts are the files actually asked and answered ("asked so far": the denominator grows as
  files name more files, never an estimate). With no session they say what Enter world will do (stream, or load one
  tile with streaming off or a renderer below protocol 18); from Enter world to the session's first state, "Tiles:
  starting (around 31_48)".
- **Overhead map**: the session's tiles outlined -- resident in the accent colour, loading in the warning colour,
  failed dashed -- and the camera where the session last saw it (the grid repaints on a `streamState` only while the
  overhead map is shown and only when a ring or the camera's dot changed).
- **Enter world** at a target on a tile the session holds moves the camera only (no new load); **Fly** and **Look** aim
  at the target when the session holds its tile, else at the middle of the tile the camera is over (the entry's tile
  may have left the region). A camera with no height over a tile not loaded yet stands at the camera's height and on
  the ground once that tile is built.
- **Logs**: the player's per-tile settle line (files, bytes, placements and doodads placed and shared), every open,
  close, refusal, takeover and eviction, and the session's summary at its end; the host's streamState line once a second
  and the getMapAsset summary every 2 s.
- **runtimeState** gains the session's numbers and the light mode.
- The panel's first streaming line (one long line, wrapped) re-wrapped and laid the whole Browse pane out on every
  `streamState`: a 90.5 ms GUI round in the acceptance flight (fl2). Found by the harness's round check; fixed as above
  (fl3: 16.7 ms).

## 7. The harness

The Maps UI test (`-mapsuitest`) gains:
- `flight:<speed>:<pitch>:<x>/<y>/<z>/...` -- the player's free camera flies the path itself (`testFlight`), the step
  waits for its answer;
- `waitstream:<ms>` -- until a `streamState` after the step began says nothing loads and no request is out;
- `streamstate:<label>` -- the session's last state, the player's and the viewer's private bytes;
- `roundmark` / `roundcheck:<ms>` -- every GUI round since the mark at most ms (the acceptance check);
- every `streamState` the player sends is kept in the run's JSON (`streamSamples`: the continuous sampling);
- every GUI round over 20 ms is logged with its step and the last poll's parts (receive, map serving, send).

## 8. Results (PROVEN_RUNTIME)

All on the hidden desktop with the final build, through the Maps viewer's own controls (`-mapsuitest`) and the harness
steps of section 7, on the installation of the header. The final build is viewer exe SHA-256 A7AF7037..., player
Assembly-CSharp MD5 1031DFA1.... Sections 8.1-8.4 were run 14:32-15:00 on 2026-10-17; their logs are in
`F:\wmv-build-fix\w08\st` and `pics`.

### 8.1 The acceptance run (Retail, Stormwind and the 3 x 3 around it)

The run (`st\acc_wow`, PASS 35 / 0):
- Enter world at (-8700, 400) in tile 31_48 (Stormwind).
- Once the region settles, the free camera flies 31_48 -> 34_48 -> 34_50 -> 31_50 -> 31_48 six times, 250 yards up at
  60 yards a second (5,333 yards in 88.9 s a loop). The session's state and both processes' memory are recorded after
  each loop, and every GUI round of the six loops is checked against 50 ms.
- Then the camera flies three tiles east, so the entry's tile leaves the region, and switches to Overview and back to
  Fly. Those rounds are checked against 50 ms on their own.

| | Result |
|---|---|
| The region at entry | 9 tiles settled 17.4 s after the run began; 4,769 files answered of 4,769 asked (372 MB); 56 world-model instances, 18,208 doodads; GPU 322 MB textures, 79 MB geometry (terrain alpha arrays and meshes included) |
| **Viewer GUI thread over the six loops** | **longest round 16.9 ms: roundcheck:50 PASS** |
| **Far flight, Overview, Fly** | **longest round 24.5 ms: roundcheck:50 PASS**. Overview framed a resident tile; Fly stood the camera on the ground at the middle of the camera's tile (34_48), the entry's tile having left the region |
| GUI rounds from Enter world to the settled region | the Enter world click 25.5 ms; over 50 ms only the round in which the world is shown, 71.4 ms (pre-existing, section 8.3) |
| Tiles during the loops | 28 opened and 22 closed in the first loop, 24 and 24 in each loop after; at most 3 loading at once; 0 failed or refused. The host's queue was at most 21 deep, 0 requests refused for room |
| Player frames | the longest frame per loop: 26.3 / 32.6 / 38.5 / 33.8 / 33.9 / 32.8 ms. The 95th percentile of the last 120 frames stayed at 16.8-18.0 ms (the 60 a second cap). Frames over 33 ms: 3 in the six loops, 7 in the whole run. Over 50 ms: 1, at entry (51.2 ms: the region's largest world-model prototype, 42.1 ms of streaming work) |
| Streaming work and tile builds | streaming work at most 15.2-26.2 ms in a loop's frame. A tile's Unity objects (the terrain tile build, "upload" in streamState; the M2 builds are logged per tile when it settles) took at most 14.6-25.9 ms |
| After each loop | 15 tiles resident, 82 world-model instances, 26,080 doodads, GPU 394 MB textures / 108 MB geometry: the same after every loop. The region settled 2.4-2.7 s after each loop |
| Pools | at their 256 MiB budget during each loop, briefly above it (at most 260 MB) while evictions spread over frames. 487 prototypes and 389 textures were evicted in the first loop, about 780 and 635 in each loop after (4,394 and 3,573 after six). Every loop asked about 2,750 files again: 8,542 after the first loop, 22,316 after six, 1.5 GB read in all |
| **Memory after loops 1-6** | **player 2,759 / 2,760 / 2,790 / 2,825 / 2,817 / 2,817 MB private, managed heap 468-482 MB; viewer 2,251-2,254 MB**: flat |
| After clearTerrain | player 2,109 MB private; every live tally 0 |

**The pool budget against this route.** Once the M2 textures are counted, what the Stormwind loop revisits exceeds the
256 MiB budget. The final review estimated about 374 MiB of pool from the run data; that figure is not measured. Least
recently used is the worst order for a loop: what was pooled longest ago is what the next loop needs first. So each loop
evicts most of it and fetches it again.
- The cost: the region settles 2.4-2.7 s after a loop instead of 0.3-0.5 s, and about 180 MB more is read per loop.
- What it does not cost: GUI rounds stay at 17 ms. Frames over 33 ms number 3 in the six loops; the build that evicted
  nothing (fl3) had 0 in its three loops.
- On this route eviction does not lower the peak GPU. With the earlier accounting (terrain alpha arrays and meshes left
  out), the scene was 417.8 MiB at a loop's end without eviction and 418.0 MiB with it.

The budget is one constant (`StreamPoolGpuBudget`). It is not raised here, because the viewer often runs beside the game
client on the same GPU (section 11).

### 8.2 The other clients (one loop each)

Entry at the same point, the region settled, one loop of the same path. Runs `st\c4f_*` (14:43-14:48); the GUI round
check covers the flight and the settle after it.

| | Classic Era 1.15.9.70003 | MoP Classic 5.5.4.70449 | Classic Beta 1.60.1.70450 |
|---|---|---|---|
| Region at entry: files (MB) | 1,955 (83 MB) | 3,037 (139 MB) | 2,165 (109 MB) |
| Instances world / doodads at entry | 22 / 11,676 | 49 / 15,664 | 31 / 12,463 |
| GPU textures / geometry at entry | 42 / 50 MB | 116 / 58 MB | 74 / 60 MB |
| After the loop: files; instances | 3,694; 48 / 19,493 | 4,952; 75 / 23,535 | 4,202; 57 / 20,298 |
| GPU after the loop; pools | 73 / 74 MB; 30 MB | 178 / 85 MB; 39 MB | 140 / 91 MB; 42 MB |
| Evicted prototypes / textures | 0 / 0 | 0 / 0 | 0 / 0 |
| Tiles closed during the loop | 22 (failed 0) | 22 (failed 0) | 22 (failed 0) |
| **Longest GUI round in the flight** | **16.3 ms** | **16.4 ms** | **16.5 ms** |
| Player frames over 33 / 50 ms (whole run) | 1 / 1 (flight max 22.1 ms) | 1 / 0 (flight max 25.7 ms) | 1 / 1 (flight max 26.2 ms) |
| Player private bytes before the loop | 1,444 MB | 1,877 MB | 1,637 MB |
| Player / viewer private bytes after | 1,950 / 770 MB | 2,180 / 1,024 MB | 2,136 / 1,769 MB |
| Harness | PASS 14 / 0 | PASS 14 / 0 | PASS 14 / 0 |

### 8.3 GUI rounds over 50 ms

Every GUI round over 50 ms of the final runs, by step. The harness logs each round over 20 ms with its step; a GUI round
is one IPC poll and one pass of the wx event queue, paints included.

| Run | Mode switch to Maps | Map picked | Enter world click | World shown (waitworld) |
|---|---|---|---|---|
| Retail acceptance (`st\acc_wow`) | 111.3 ms | -- | 25.5 ms (under 50) | 71.4 ms |
| Classic Era (`st\c4f_wow_classic_era`) | 92.4 ms | 51.5 ms | 24.6 ms (under 50) | 81.3 ms |
| MoP Classic (`st\c4f_wow_classic`) | 106.5 ms | -- | 24.3 ms (under 50) | 79.4 ms |
| Classic Beta (`st\c4f_wow_classic_beta`) | 92.2 ms | -- | 23.6 ms (under 50) | 68.8 ms |
| Pictures, after (streaming) | 105.2 ms | -- | 24.5 ms (under 50) | 74.5 ms |
| Pictures, before (`WMV_STREAMING=0`) | 109.0 ms | -- | 36.4 ms (under 50) | 74.9 ms |
| Protocol-17 player (`compat\cmpf_newhost_p17`) | 120.7 ms | -- | 23.5 ms (under 50) | 64.8 ms |
| With the entry-stall fixes (section 8.7), Retail acceptance (`stalls\out_r2\acc_wow`) | 105.8 ms | -- | 9.7 ms | 17.5 ms |
| ... Classic Era / MoP Classic / Classic Beta (`stalls\out_r2\c4_*`) | 83.4 / 92.9 / 82.2 ms | -- | 7.1 / 8.3 / 8.0 ms | 19.0 / 22.5 / 18.2 ms |

What each one is, and whether this stage made it:

- **The mode switch and the map pick** (pre-existing): the map pick (Era, once) is the overhead map's first draw.
  The mode switch: the Maps viewer's first layout and the Browse listing; no streaming code runs in it.
  The Stage 07 host measured 134.8 ms in the same step (`compat\cmp_oldhost_p18`).
- **The round in which the world is shown** (pre-existing, fixed after the stage): about 44 ms of it was the
  synchronous resize of the player's window, which the player's thread answers while it resizes its swap chain. The
  Stage 07 host measured 77.5 ms here. `SWP_ASYNCWINDOWPOS` has no effect between a parent window and a child window of
  another process, because their input queues are attached (measured: 43.1 ms). The player's window is now sized from a
  thread of the viewer's own when the load is sent, in order with every later resize (section 8.7): 17-23 ms.
- **The Enter world click** (new in this stage, fixed). The final review found that this round went over 50 ms only with
  streaming: 55-75 ms, against 22-49 ms with `WMV_STREAMING=0`, a protocol-17 player or the Stage 07 host. Two things
  caused it:
  - the session's first streamState showed the hidden streaming lines, which grew the panel and laid out and repainted
    the Browse pane in the round of the click's own repaint;
  - with the lines shown from the start, the streamState's UI update itself still landed in that round.

  The streaming lines are now always shown, and a streamState's UI follows on a 150 ms timer, outside the round it
  arrived in. Same build, same player, alternating runs (`compat\ab_timer.log`, Retail and Era):

  | | Enter world round |
  |---|---|
  | Streaming, with the timer | 21.5 / 22.4 / 24.4 / 24.6 / 27.1 / 39.9 ms |
  | Streaming, without the timer | 23.6 / 38.4 / 40.8 / 42.2 / 46.2 / 54.3 ms |
  | Streaming off | 23.0 / 23.9 / 25.5 ms |

- **The rounds while the entry's world models complete** (pre-existing mechanism). Each progress message is followed by
  one full Maps panel update of 9-15 ms, the same cost as Stage 07 (median 9.5 ms on the Stage 07 host, 9.1 ms on this
  build). Several of them land in one round when the entry's world models complete and its camera is placed: 45-76 ms
  in some earlier runs, none over 50 ms in the final ones. With streaming this happens while the region's other tiles
  load. Progress is now shown once for the messages that come together, and a panel update costs 2-6 ms (section 8.7).
- **None of the rounds over 50 ms is in the flight**: the acceptance flight's six loops, the far flight with Overview
  and
  Fly, and the other clients' loops stay at 16.1-24.5 ms.

### 8.4 Pictures

`F:\wmv-build-fix\w08\pics\picf_wow_{before,after}_{over,east,west}.png`. The viewer, player and cameras are the same in
both: before is the single-tile world (`WMV_STREAMING=0`, as Stage 07 enters it), after is the streaming session,
settled. The acceptance run's pictures are `st\acc_wow_{a_entered,b_afterloops,c_far,d_overview,e_flyback}.png`.
- **Over 31_48, 1,800 yards up, looking straight down.** Before: tile 31_48 alone, with Stormwind's world models
  reaching
  past its edges into empty space. After: the 3 x 3, all of Stormwind and the Elwynn Forest around it, with the terrain
  continuous across the nine tiles.
- **At the east edge of 31_48, 180 yards up, looking east into 32_48.** Before: one tree at the tile's edge and nothing
  beyond it. After: Elwynn's forest, hills and a chapel continue across the boundary.
- **Overview after the far flight** (`acc_wow_d_overview`): the entry's tile has left the region; Overview frames a
  resident tile, with the region around it drawn.

No in-game reference was taken of these views, so nothing here is claimed to match the game's look.

### 8.5 Session endings, cancellation and repeated loading (Retail, then Era)

Two runs after the final review (`st\life08`, `st\life08b`; the final player, host 70322D6E -- the final host but for
the stream-UI timer, which these steps do not depend on), Retail 12.1.5.70448 unless said:

- **Cancellation with requests out** (`life08b`): Enter world at Stormwind, and as soon as the world is shown the free
  camera flies at 2,000 yards a second about 10 tiles east and back. 65 tiles opened and 54 closed, most of them while
  still loading. 44 requests were retired by those closes and 45 answers dropped as late (the 45th the entry's). The
  host
  answered 28 waiting requests stale while they waited, refused none for room (its queue at most 21 deep), and every
  request the session still had out was answered or retired: 0 out at the end. No tile failed. PASS 10 / 0.
- **Every other way a session ends** (`life08`, PASS 62 / 0):
  - a new Enter world over a live session: Goldshire after a 400 yards-a-second flight;
  - Clear while the entry loads (17 requests retired, 12 answered stale by the host);
  - a model loaded in the Models viewer while a session loads (8 retired);
  - four Enter world / settle / Clear cycles at Stormwind;
  - a client switch while a session loads (16 requests out), then a session on Classic Era.
  Every ending logs its tiles and requests, and every request asked was answered or retired; the next session holds
  nothing of the last (its scene is built anew from the entry). After every Clear the player's live terrain tiles,
  world-model and doodad instances,
  prototypes and textures are 0.
- **Memory over the four cycles**, private bytes after each Clear:

  | | Cycle 1 | Cycle 2 | Cycle 3 | Cycle 4 |
  |---|---|---|---|---|
  | Player | 2,295 MB | 2,329 MB | 2,391 MB | 2,302 MB |
  | Viewer | 2,267 MB | 2,267 MB | 2,268 MB | 2,268 MB |

  With the region settled: 2,672 / 2,768 / 2,797 / 2,797 MB (player).
- The 400 yards-a-second flight of `life08` closed 21 tiles, every one after it had settled; only the 2,000
  yards-a-second
  flight closed tiles while they loaded (the speeds between were not measured).

### 8.6 Protocol compatibility

The new host with the Stage 07 player (protocol 17, `compat\cmpf_newhost_p17`):
- PASS 13 / 0.
- No streaming was offered, and Stormwind 31_48 was entered as one tile with its world models and doodads, as at
  Stage 07.
- The Enter world round was 26.6 ms.

The Stage 07 host (protocol 17) with the new player (`compat\cmpf_oldhost_p18`):
- PASS 13 / 0.
- The single-tile path ran, and Overview, Fly and Clear worked.
- The Enter world round was 29.7 ms.

### 8.7 The entry rounds attributed, and their pre-existing parts fixed (after the stage)

Attributed with a wall-clock stack sampler of the GUI thread (a temporary harness probe, not kept: a thread of the
viewer's suspends the GUI thread about every 1-2 ms in a round over 3 ms and unwinds its stack) and temporary timers in
the overhead map's paint and the panel's refresh. The harness runs the Enter world click's handler outside any round;
its own time is 13-14 ms on Retail (43 ms on Beta).

- **The round after Enter world** (76-85 ms): 43-45 ms is the resize of the player's window as the overhead map goes
  (`showContent(false)` -> `applyEmbeddedVisibility` -> `resizeEmbeddedWindow`), the rest the panel's refresh. Fixed:
  `UnityRendererHost::sizePlayerAhead`, called by `ModelViewer::RequestLoad` just before a load is sent, gives the
  player's window the viewport's size while the content window covers it, from a thread of the viewer's own -- not
  attached to the player's input queue, so the move waits there (38-49 ms) and the GUI thread does not. Putting the
  player back when the world is shown finds the size right. `resizeEmbeddedWindow` first waits for that move
  (`finishAheadMove`, taking sent messages while it waits, as the GUI thread's own moves do), so a later move never lands
  before it; a player Windows considers hung is not waited for (the existing retry from the timer).
- **The Enter world round** (56-75 ms with streaming; 27-41 ms with it off and on the Stage 07 host): the streaming part
  -- the first `streamState` showing the four streaming lines (the panel and the Browse pane laid out again) and the
  overhead map repainted at every `streamState` -- is fixed in the stage itself (section 8.3: the streaming lines always
  shown, the streamState UI on a 150 ms timer); the first table below was measured without that fix. What this section
  changes is the cost of each repaint and refresh in that round. The click queues two or three repaints of the overhead
  map at 16-19 ms each: in a repaint, 26 of 43 samples were wx's buffered paint DC copying its buffer, a DIB, to the
  window through `StretchDIBits`; the minimap pictures' own stretch was 0.1-1.0 ms. The grid now paints through a kept
  bitmap of the screen's own format (`MapTileGrid::m_paintBuffer`), 7-10 ms a paint; three pictures of the overhead map
  (fitted with the target, the whole 64 x 64 grid, zoomed) are pixel-identical to the stage's, the frame differing only
  in the status bar's memory figure. The panel's refresh set six of its eight labels every time (each set repaints the
  label at once, three times with the wrap): `MapTilePanel::setLabel` compared the label's own text and width, which
  after a wrap are never what it was given (line breaks; the text's width under an `xs` border). It now remembers what
  it gave each label: 6-16 ms -> 2-6 ms a refresh while one label changes.
- **Two progress updates in one round** (54 ms in `st\acc_wow`, 74 ms once here): each `worldProgress` updated the
  whole Maps GUI, and an update that changes the progress text's line count lays out the panel and the Browse pane again
  (20-28 ms). `MapsViewer::OnWorldProgress` now shows progress once for what came together (`CallAfter`). One such
  update can still bring a round near 50 ms (51.8 ms once, in a second entry).
- **The mode switch to Maps** (98-238 ms): not fixed. It is the viewer switch: the keyboard handed to Browse's search
  box (`ModelViewer::OnCommandBar`) waits for the player's thread when the player had it (18-30 ms: the player's window
  is sent WM_KILLFOCUS and answers once per frame); the Maps panel is built on the first entry (about 20 ms); Browse's
  list of maps is listed again on every entry (`FileControl::InitMaps`, 9 ms with no map picked, 26 ms with one -- not
  the fraction of a millisecond `FollowViewerMode`'s comment says); the native tree re-measures its rows as it thaws
  (12-27 ms); the other windows thaw and lay out (8-18 ms); the switch paints the new workspace at once (13-25 ms).
  Entering Maps again, the panel built and the keyboard not in the player, took 177-193 ms. Under 50 ms needs a viewer
  switch that neither lists Browse again nor repaints the whole frame in one round.

First measured on the stage as committed (b9c688d4). The longest GUI round of each step, ms (`F:\wmv-build-fix\stalls`; "before" is the stage's exe, "after" this
change, the same player p18l; "before" of the acceptance and the three clients is the stage's own runs of section 8):

| Run | mode:maps | enter | waitworld | rounds over 50 ms |
|---|---|---|---|---|
| Retail entry, before (`out_b1\base_entry1/2`) | 105.4 / 119.9 | 71.7 / 66.7 | 70.0 / 65.6 | 3 / 3 |
| Retail entry, after (`out_r1\entry1/2`) | 110.5 / 128.1 | 24.0 / 38.9 | 20.7 / 31.6 | 1 / 1 |
| Retail, `WMV_STREAMING=0`, before / after | 108.5 / 122.7 | 23.7 / 16.2 | 80.8 / 19.1 | 2 / 1 |
| Beta entry, before / after (`out_b1\base_beta`, `fix_beta`) | 99.4 / 122.0 | 61.7 / 16.9 | 75.0 / 21.9 | 3 / 1 |
| Retail acceptance, before / after (`st\acc_wow`, `out_r1\acc_wow`) | 127.6 / 115.4 | 74.5 / 26.3 | 81.8 / 17.5 | 4 / 1 |
| Era one loop, before / after | 98.0 / 80.8 | 66.1 / 13.8 | 84.7 / 18.1 | 3 / 1 |
| MoP Classic one loop, before / after | 105.9 / 121.5 | 56.4 / 32.3 | 81.3 / 23.8 | 3 / 1 |
| Beta one loop, before / after | 238.2 / 85.2 | 60.6 / 15.0 | 85.0 / 21.5 | 4 / 1 |

After the change the only round over 50 ms in every run is the mode switch. The acceptance run passes (35 passed,
0 failed): its loops' longest round 18.3 ms (17.96 before), the far flight, Overview and Fly 10.2 ms (26.7 before);
the viewer's private bytes at each checkpoint 2,251-2,254 MB (2,251-2,257 before). The three clients pass (14 passed
each), their flights' longest round 3.3-16.8 ms (16.4-16.6 before). The player's window was sized ahead in 34-47 ms
on its own thread in every run. A back-and-forth (`out_r1\backforth`: enter, Back to map, Enter world again, Models,
Maps, a second target and entry) passes 23 of 23; its rounds over 50 ms are the three viewer switches and one 51.8 ms
progress update in the second entry.

On the stage's final host (1d0d0b73, the streamState UI on its timer) and player p18m, before (`stalls\out_b2`, the
final exe A7AF7037) and after (`stalls\out_r2`, this change on top of it):

| Run | mode:maps | enter | waitworld | rounds over 50 ms |
|---|---|---|---|---|
| Retail entry (x2) | 115.0 / 101.1 -> 96.5 / 108.1 | 23.3 / 24.8 -> 10.9 / 10.7 | 67.6 / 69.5 -> 18.0 / 17.0 | 2 / 2 -> 1 / 1 |
| Retail, `WMV_STREAMING=0` | 114.7 -> 109.9 | 23.9 -> 10.5 | 81.5 -> 17.2 | 2 -> 1 |
| Beta entry | 102.7 -> 92.2 | 22.6 -> 8.2 | 66.6 -> 22.7 | 2 -> 1 |
| Retail acceptance (after only) | 105.8 | 9.7 | 17.5 | 1 |
| Era / MoP Classic / Beta one loop (after only) | 83.4 / 92.9 / 82.2 | 7.1 / 8.3 / 8.0 | 19.0 / 22.5 / 18.2 | 1 each |

The acceptance run passes 35 / 0 (the loops' longest round 17.0 ms, the far flight with Overview and Fly 8.2 ms), the
three clients 14 / 0 each (flights 9.5-16.8 ms), the back-and-forth 23 / 0 (only its three viewer switches over 50 ms).
The map pick's round is 14-33 ms.

## 9. Regressions (Stages 03-07, Models, Characters, Buildings)

The Stage 07 battery (`F:\wmv-build-fix\w07\battery`), run again on the final build (`w08\battery_final`, 14:02-14:21)
on
the same client builds, and compared file by file. None of the battery's runs streams (only the Maps viewer's Enter
world
starts a session), so it covers the single-tile paths this stage changed -- block uploads, the session's `Live` tests,
the transport -- and not streaming, which sections 8.1-8.6 cover. It ran on host A7AF7037's predecessor (70322D6E),
which differs only in the timer that puts a streamState's UI update off by 150 ms -- code no battery run reaches; the
final host's own `-terraintile` checks pass (tt_s8d below):

| Set | What | Result against Stage 07 |
|---|---|---|
| geo | terrain geometry dumps, 6 tiles over the four clients | identical but for the load number in each dump's header; checks 6 x PASS, the same counts |
| e2e | composite dumps, 11 runs (98 raw/png/json files) | 98 of 98 files identical; checks 11 x PASS, the same counts |
| reg | Models (bear, animation pinned) and Buildings (WMO 108631) pictures | both pictures byte-identical |
| H | 12 host harness runs (late answers, lifecycles, client switches, the Protocol 12 player) | 11 PASS with the same counts; `H_B_missingtex0` FAIL with the same check as at Stage 07 (below) |
| W | 6 world runs (Beta in-process, heavy tile 29_23, memory, reads, switch) | 6 PASS, the same counts |
| shutdown | the read-in-flight shutdown checks on the four clients | 4 PASS, the same counts |
| map catalogue | the catalogue sequences | PASS 27 / 0 and 44 / 0 (twice each), as at Stage 07 |

`H_B_missingtex0` fails at Stage 07 and here alike, on the same check: its fixture expects map 2736 tile 32_29's
material
to be unavailable (no tex0 ADT), and this installation's client serves a tex0 for it (the material is "textured"). It
is not related to this stage.

Characters (the three .chr cases of `w07\chars2.ps1`, animation pinned): the pictures are byte-identical to Stage 07's
(`w08\chars_f08`). The host's `-terraintile` checks: 237 passed, 0 failed, 1 skipped (tt_s8c, and tt_s8d on the final
host; the stale-sweep check of
section 10.2 added). The player's lifecycle self-test: 953 passed, 0 failed (the checks of sections 10.1 and 10.2
added). Pure tests (`Tests/Runner`, the corpus at `build\world-audit-data`): all suites pass, among them
`BlpBlockTests`, `BlpBlockCorpusTests` and `StreamSeamTests`. Protocol compatibility both ways: section 8.6.

What changed on purpose: the single-tile world load uses block uploads for world-model and doodad textures too, so
its pictures differ from Stage 07's where distant alpha-tested foliage samples the files' own mips (measured in
section 3).

## 10. Adversarial reviews

### 10.1 The first review (before commit b9c688d4)

Four independent reviews of the change (the player's session; the host's streaming and transport; texture residency and
block uploads; the player's glue and IPC), each finding checked against the code before anything was changed. Confirmed
and fixed:

| # | Finding | Fix |
|---|---|---|
| 1 | Once every built tile had closed behind the camera (a sea gap, a fast flight, Overview after flying away), the camera's world position came from the shown tile -- none -- and the region fell back to the entry for good | The free camera's position comes from the scene's root and origin (every tile is built against it), and the first tile built after that is the shown one |
| 2 | Overview (the free camera stopped) snapped the region back to the entry and closed the tile it had just framed | Not flying, the region stays where the camera last was |
| 3 | The entry closed by the region while its world models or doodads were still loading was reported "superseded": the Maps viewer took the session as ended (no streamState shown, Enter world restarting it) | Reported "built" with why (it was on screen); the camera it carried placed at once; a committed entry that fails later likewise; the host clears the streaming line when a session does end |
| 4 | A closed tile's doodads stayed drawn, floating, until the deferred destroy reached them (tens of frames after a 5-tile close) | Each one's renderer off when it is queued (cheaper than deactivating thousands of roots in one frame) |
| 5 | The answer to a streamTile of closes only was taken for the next open's: two out at once, spurious refusals counted as failed tiles | Every streamTile waits for its answer before the next goes |
| 6 | The Maps viewer's Fly, Look and Enter world aimed at the entry's tile after it had left the region (refused, or a camera over a tile not loaded) | The host: at the target when the session holds its tile, else the middle of the camera's tile; Enter world moves the camera only to a tile the session holds. The player: a camera with no height over a tile not loaded yet stands at the camera's height and on the ground once that tile is built |
| 7 | A closed tile's requests waiting in the host's queue kept its room until each reached the front (the player had taken back their credit): live requests could be refused for room | Waiting requests of an ended load answered stale at once during a session, and when a session ends |
| 8 | A player-side session end (the entry failing before its tile) left the host's session on (queue 64, 4 reads) | The host ends its session with an entry that was not built |
| 9 | The receive pause was decided once a poll: 64 lines of on-the-spot getAsset answers could go past the mark | Checked again after every line |
| 10 | A model's later sampler lost its texture once the session's image was evicted (a tile that borrowed it has no bytes to decode again) | An image is kept while a live tile that took it may still need it |
| 11 | Pooled M2 prototypes' textures were outside the pool budget (only their geometry counted): textures could pile up until new doodad models were refused by the scene's budget | An M2 texture upload no live prototype holds counts in the pool; an evicted M2 prototype takes its exclusively held uploads with it. (A first fix -- each prototype counting its share of every upload it holds -- over-evicted: a share of a texture a live model still holds is never freed. On the acceptance run it evicted 4,399 prototypes over six loops and asked about 2,800 files a loop again; found by that run, replaced.) |
| 12 | Eviction emptied every pool when the overage lay where the loop could not reduce it (the M2 uploads no prototype holds; the parsed-file budget alone) | The uploads no prototype holds go first; over the parsed-file budget alone only world models go |
| 13 | The scene's terrain textures were missing from the GPU tallies (tile report, runtimeState) | Counted |
| 14 | Timeouts counted a tile's waiting (for the credit, the frame budget, another tile's fetch) as lateness: a far tile could be thrown away and fetched again | A tile's timeouts run only while its own requests are out |
| 15 | A refusal for one tile's load (its budget, the queue) failed the model for the whole session | Refused or stale: the tile's failure, the fetch handed on; a file's own failure still fails it for all |
| 16 | Tearing a session down (a new load, a clear, a model load, shutdown) handed fetches on to tiles about to end, which sent requests | The session is marked ending first |
| 17 | A hand-over inside a timeout carried the timeout's "transient" context into another tile's own failures | Restored, and cleared around another tile's advance |
| 18 | A tile failing after its adoption dropped the texture data its later view rebuilds need | Kept |
| 19 | The Maps test left its callbacks set when the player never connected | Given back |

New self-tests (the lifecycle self-test's streaming part): the region staying put when the camera stops flying; one
streamTile out at a time with a message of closes only, the region coming back after every built tile closed (a tile
shown again); a world model refused for the entry's load fetched under another tile's load and placed; the entry
closed with its fetch out reported "built" once.

Not changed: a lost streamTile answer is not timed out (the host always answers; the player exits when its connection
ends); a world model with only some groups refused for its tile's load is built partial and shared, as the single-tile
load builds it; the single-tile world load now uses block uploads too (intended, requirement 5).

### 10.2 The final independent review (of commit b9c688d4)

Eight reviewers, one per question the owner asked -- the streaming lifecycle, cancellation, ownership and leaks; shared
residency across tiles; transport backpressure and protocol 17 compatibility; the eviction and refetch on the Retail
flight; the GUI stalls at the Maps viewer's entry; frame spikes; preservation of Models, Characters, Buildings and
Stages
03-07 and of the owner's uncommitted tabard fixes; every exit criterion against the evidence -- and every finding tried
by
one or two independent verifiers (44 agents in all). Nothing blocking was found in the streaming lifecycle (request
credit balanced on every exit, late and stale answers never touching the scene, every object destroyed once, every tally
back to 0). Confirmed, and fixed:

| # | Finding (new in Stage 08 unless said) | Fix |
|---|---|---|
| 1 | **The Enter world click's GUI round over 50 ms only with streaming** (55-75 ms on every client; 22-49 ms with `WMV_STREAMING=0`, the Stage 07 host or a protocol-17 player) -- section 8.3 had called it pre-existing. The session's first streamState showed the hidden streaming lines: the panel grew, the Browse pane was laid out again and repainted, and the overhead map repainted, in the same round as the click's own repaint | The streaming lines are always shown at their fixed height (an idle text when no session streams, "Tiles: starting" from Enter world), so a streamState only relabels them; a streamState's UI follows on a 150 ms timer, never in the round it arrived in; the overhead map repaints on a streamState only when a ring or the camera's dot changed. The Enter world round with streaming: 21.5-39.9 ms against 23.0-25.5 ms with streaming off (same build, alternating runs; section 8.3) |
| 2 | The stale sweep (`answerStaleWaiting`) held a queue iterator across a send: a send that fails (the player gone) drops the client at once and clears the queue -- a use after invalidation, a likely crash | Two passes: the stale entries are taken out of the queue, then answered; a connection lost meanwhile drops the rest. A new in-process check (`-terraintile`): 32 waiting requests made stale, 32 answered stale while waiting, each once |
| 3 | A texture file's own failure (not in client storage, too large, no bytes) was handed on like a refusal: every waiting tile asked again | Only a refusal for the tile's load or a stale answer is handed on; the file's own failure is the session's. New self-test: a missing M2 texture asked for once across nine tiles |
| 4 | streamState's p95 was always the maximum (95th percentile of ~15 frames) | The 95th percentile of the last 120 frames |
| 5 | The scene's GPU bytes left out the terrain alpha arrays (4 MiB a tile) and meshes, in streamState and in the scene-budget check | Counted |
| 6 | The pool bookkeeping ran every frame and allocated a dictionary each time (about 0.9 ms a frame, 2-4 MB/s of garbage) | Run when something may have joined the pools or an eviction is unfinished, and twice a second besides; the dictionary reused |
| 7 | Evictions ran outside the 8 ms budget (up to 447 prototypes and 305 textures in one frame) | Under the frame budget, at least one a frame; an upload removed by its key (no scan) |
| 8 | Heavy items stacked past the budget (a 28.9 ms tile add after other work: the only frame over 33 ms of the six loops) | A tile's Unity objects and a world-model prototype built from files run only as a frame's first item |
| 9 | The streaming line read "tile -1_-1" until the entry's commit, and its counts were cut off at the default pane width | The entry's tile named until then; counts first; the whole text in a tooltip |
| 10 | Settled tiles logged no doodad build or placement times | In the settle line |
| 11 | Real-IPC cancellation, a new load over a live session, Clear while loading, a model load mid-session, a client switch, repeated enter/clear: none run on real data | Run (section 8.5), plus a self-test of a new load over a live session with requests out |

Corrected in this document (the data did not support them): the entry rounds' attribution and their count (8.3, 11);
"the refetching costs no frame or GUI time" (8.1: it costs settle time, and an eviction burst added 7-8 ms of streaming
work to its frame before item 7); the frame spikes at the Era and Beta entries (126 and 137 ms, 5.2); the unsplit tile
build (1.15); upload timing being the terrain tile build only (8.1); the protocol-17 runs (8.6).

Not changed (section 11): the pool budget against the Stormwind loop (a policy choice, measured); an M2 texture fetched
and decoded again though its upload is still on the GPU (efficiency); a texture refused or capped for the owning tile's
load built into the shared prototype (latent: one answer of 22,708 was not "available" in the acceptance run); the
player's inbox counters and the `light` runtimeState, implemented but not reported or asked for.

## 11. Unresolved and limitations

Marked **new** (this stage's own) or **pre-existing** (present at develop 8f4e791f, measured the same with streaming
off,
the Stage 07 host or a protocol-17 player).

- **The mode switch to Maps is over 50 ms** (sections 8.3 and 8.7; pre-existing). The roadmap exit's "no GUI-thread
  stall over 50 ms" holds over the flight and from the Enter world click to the settled region:
  - pre-existing, not fixed: the mode switch to Maps, 82-110 ms with the entry-stall fixes (92-121 ms in the stage's
    final runs) -- Browse's list of maps listed again, the tree's thaw, the frame's layout and paint at once, the Maps
    panel built on the first entry, and the keyboard's move from the player waiting for the player's thread;
  - pre-existing, fixed after the stage (section 8.7): the round in which the world is shown (65-81 ms; now 17-23 ms,
    the player's window sized ahead on a thread of its own), and the full Maps panel update per progress message (now
    shown once per batch, 2-6 ms an update);
  - new, and fixed by the final review: the Enter world click's round, which streaming had pushed to 55-75 ms (the
    streaming lines appearing, then a streamState's update, inside the click's repaint round). With streaming it is now
    23.6-25.5 ms (the final runs), against 23.0-25.5 ms with streaming off.
- **Frames over 50 ms at entry** (pre-existing): the largest world-model prototype of a region is built in one piece --
  48-50 ms on Retail, and frames of 126 and 137 ms at the Era and Beta entries (Stage 07 logged 114-128 ms builds of
  world model 107243 the same way). A tile's Unity objects are built in one piece too (4-29 ms on Retail; new: 1.8
  planned batches of chunks); heavy items now run only as a frame's first item, so they no longer stack.
- **Managed-heap collections**: one 50.3 ms frame with 0.8 ms of streaming work in an earlier run (fl1) is most likely a
  collection; not measured further (the player's GC mode is Unity's default).
- **The pool budget is below the Stormwind loop's revisit set** (new, a policy choice; section 8.1): an estimated 374
  MiB would be needed against 256 MiB, so each loop evicts most of what the next loop needs (least recently used, the
  worst order for a loop) and asks about 2,750 files again; the region settles 2.4-2.7 s after a loop instead of 0.3-0.5
  s. The
  budget is one constant (`StreamPoolGpuBudget`).
- **An M2 texture is fetched and decoded again even when its upload is still on the GPU** (new, efficiency): the cache's
  uploads are reused only with the decoded image; at least about 110 of the files asked again per Retail loop are such.
- **A texture refused or capped for the owning tile's load** is left out of the prototype the whole session shares (new,
  latent: 1 answer of 22,708 in the acceptance run was not "available"); so is a world model's group refused for it (its
  per-file limit: Retail 2320850, group 3 is 4,476,010 bytes against the 4 MiB per read that existed before this stage
  --
  249 of its set doodads are not placed).
- **Files asked twice besides evictions**: two tiles fetching the same terrain texture before either is committed both
  ask (15 at the Retail entry), and a reopened tile reads its root, tex0 and obj0 again (the tile files are not pooled,
  only what they name).
- **World-model textures are uploaded per prototype** (as in Stage 07): decoded once for the session, on the GPU once
  per prototype.
- **Reported but not asked for**: the player's inbox counters (`InboxDepthMax`, `InboxFramesDeferred`) and the `light`
  runtimeState are implemented and never sent or requested.
- **The viewer's title line names the entry's tile** for the whole session; the streaming lines name the camera's.
- **A lost `streamTileAnswer` is not timed out**: the host always answers, and the player exits when its connection
  ends.
- **The view has no fog, sky or liquids** (unchanged): the region's outer edge is visible from high up.
- **Map 0 only in the acceptance runs**: the session is generic (any map with ADTs), but the flights, the session
  endings and the pictures are of Eastern Kingdoms around Stormwind.
- **No parity claim**: no in-game reference was taken of any view in this stage.

## 12. Files

Host (`Source/wowmodelviewer`): `UnityIpcServer.h/.cpp` (protocol 18, receive pause and caps, streamTile, streamState,
testFlight, the queue's priorities and the answer summary); `modelviewer.h/.cpp` (the session's tile loads, streamTile,
the session-wide order prerequisites, stale checks by a request's own load); `MapsViewer.h/.cpp`, `MapTilePanel.h/.cpp`,
`MapOverview.h/.cpp`, `filecontrol.h/.cpp` (the streaming lines and the overhead map's rings);
`UnityRendererHost.h/.cpp`
(the resize timing line, the player's process id for the harness); `app.cpp` (the session's checks, the harness steps).
After the stage (section 8.7): `UnityRendererHost.h/.cpp` (`sizePlayerAhead`, `finishAheadMove`), `modelviewer.cpp`
(`RequestLoad` sizes the player ahead), `MapTilePanel.h/.cpp` (the grid's paint buffer, `setLabel`'s memory),
`MapsViewer.h/.cpp` (progress shown once per batch).

Player (`Tools/UnityRendererProject/Assets/Scripts`): `WmvTerrainSessionStream.cs` (new: the region, jobs, credit,
registries, pools, closing, reports); `WmvTerrainSession.cs`, `WmvTerrainSessionWorld.cs`, `WmvTerrainSessionDoodads.cs`
(one job per tile, shared prototypes and images, the frame budget); `WmvMainTerrain.cs` (the view's stream hooks, the
deferred destroy, the test flight, runtimeState); `WmvIpcClient.cs` (protocol 18); `WmvWorldDoodads.cs`,
`WmvTerrainBuilder.cs`, `WmvTerrainMaterials.cs`, `WmvModelBuilder.cs`, `WmvWmoBuilder.cs` (pools, later destroy, block
uploads); `Wow/BlpBlocks.cs` (new) and `Wow/BlpDecoder.cs` (block-compressed levels); self-tests
`WmvLifecycleSelfTestStreaming.cs` (new), `WmvLifecycleSelfTest.cs`, `WmvLifecycleSelfTestWorldModels.cs`,
`WmvLifecycleSelfTestDoodads.cs`.

Tests: `Tools/UnityRendererProject/Tests/BlpBlockTests.cs` and `StreamSeamTests.cs` (new), the runner's registration.
Corpus tools: `Tools/world-audit/world08_corpus.py`, `world08_density.py` (new; the census of section 1.3).
Docs: this document; `roadmap.md` (the gap table: Load World, progress by file count and streaming done in 08).
