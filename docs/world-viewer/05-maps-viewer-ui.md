# World viewer 05: the single-tile Maps viewer

Branch `world/05-maps-viewer-ui`, from `develop` at `619e9a03` (2026-10-15), which contains Stage 04B (`81f79982` and
`01071f76`, merged as `4aba4c18`), PR #113, PR #114 (the player's frame-rate cap) and PR #115.

**Push status: NOT PUSHED.**

**Summary.** WoW Model Viewer has a fourth viewer, **Maps**, next to Models, Textures and Buildings. Browse lists the
active client's maps from the Stage 02 catalogue, grouped by the catalogue's own states and searchable by name, directory
or MapID. A tile grid under the list shows the map's 64 x 64 ADT tiles as the map's WDT and the client's storage give
them. The WDT is read on the Stage 04B map asset worker and parsed by the Unity player's verified parser, so the host
parses no WDT. A click on an available tile loads it through the unchanged Stage 03/04/04B pipeline: the WDT, the root
ADT, the texture ADT and the BLP textures, served under protocol 13's checks. The tile appears in the existing Unity
viewport with the existing orbit camera. The display modes are the player's existing views; switching one rebuilds the
tile on screen from what the player already holds, and nothing is read again. Leaving Maps clears the terrain.

Before any of this, the shutdown re-entrancy hazard of `04b-asset-serving.md` 18.1 was removed. Its regression test
fails on `81f79982` and passes on this branch (section 1).

**This document describes the stage's first part**, committed as "Add interactive single-tile Maps Viewer". After it,
the owner supplied a reference video of the intended product and asked this branch to continue with coordinate
selection and a free camera. That scope addition is recorded in section 21, before it was built. The revised milestone
plan, and what Stage 05 delivers against what the complete World Viewer still needs, are in `roadmap.md`.

Results are graded PASS / FAIL / SKIP / UNSUPPORTED in section 20. Evidence levels are as in `00-audit.md`:
`PROVEN_RUNTIME` (measured here, with the run named), `PROVEN_SOURCE` (read at the cited place), `INFERRED`, `UNKNOWN`.

**Builds used.** Clients: Retail 12.1.5.70391, Classic Era 1.15.9.70003, MoP Classic 5.5.4.70268 and Classic Beta
1.60.1.70374, all on this computer, with every root ADT their WDTs name installed (section 5.4). Rigs live outside the
repository, under `F:\wmv-build-fix\w05`. Every run used a private runtime on a desktop of its own, never the user's
screen.

| Binary | Identity |
|---|---|
| Host, this branch (final suite) | `wowmodelviewer.exe` SHA-256 `D37A7313D474110B...`, `wow.dll` `B89116C2...`, `core.dll` `991957BE...` |
| Player, this branch | `Assembly-CSharp.dll` MD5 `4A72FF8A63D748CFE40C6ECC1DD32CCD` (protocol 14), lifecycle self-test **855 passed, 0 failed** |
| Reference host | `81f79982` plus the shutdown test's instrumentation (no fix), `wowmodelviewer.exe` SHA-256 `043C0AA4...` |
| Reference player | develop's player, `Assembly-CSharp.dll` MD5 `3C7E3E0F7809056B372731D8CA699DEA` (the user's deployed player) |

## 1. The shutdown prerequisite (04b section 18.1), first

### 1.1 The change

`~ModelViewer` now calls `UnityIpcServer::detachForShutdown()` as its first statement (`modelviewer.cpp`, the
destructor), before the map asset worker is stopped:

- The worker's result notifier is cleared under the worker's lock (`setResultNotifier(nullptr)`, as `setMapAssetWorker`
  does). When it returns, no notifier is running, and the result `stop()` makes from the read it lets finish posts no wake.
- The server's 20 ms timer stops.
- The map callbacks into the viewer (`onDispatchMapAsset`, `onCompleteMapAsset`, `onMapAssetStale`, `onMapAssetWait`)
  and the Maps viewer's (`onViewerMapRead`, `onMapTilesInspected`, `onTerrainViewApplied`) are cleared. This is the
  second guard 18.1 recommends.
- `m_detached` is set, and `UnityIpcServer::poll()` returns at its first check while it is set (`if (!m_listen ||
  m_detached) return;`). Nothing is accepted, received (so no player-message handler runs), dispatched, completed or
  sent. This holds whatever called the poll: the timer, a wake queued earlier, or a pump later in the destructor.
- `onMapResultWake` returns as soon as it has lowered the wake flag. A wake already queued is harmless.
- The connection stays open. `UnityRendererHost::shutdown` still closes the player first, as before: `WM_CLOSE`, a pump
  of at most 3 s, then `TerminateProcess`, then `m_ipc->stop()`. The player is never left running.

Nothing else moved:

- The worker is still stopped and joined before anything it reads from goes.
- The lease rules of 04b C.2 are untouched. The worker releases a lease it took over, and `stop()` releases a grant it
  never took. The shutdown line reports "storage references taken N, released N".
- No new lock is taken. The GUI thread holds the worker's mutex only inside `setResultNotifier`, as before. The notifier
  takes wx's pending-event lock only inside `QueueEvent`, and nothing calls `setResultNotifier` from inside a wx lock.

The ownership comment in `MapAssetWorker.h`, flagged in the review (04b sections 10.3 and 19), still said "only the
worker releases it". It now says what 04b C.2 says: the worker releases a lease it took over, and `stop()` or
`grantLease` releases, on the GUI thread, a grant the worker never took over.

### 1.2 The regression test (exactly 18.1's)

There is a new `-terraintile` step, `readshutdownserved:<m>:<x>:<y>[:<role>]`, which must be the last step. The
destructor gets a test probe, `ModelViewer::testShutdownProbe`. It is called at the destructor's first statement,
immediately before the worker is stopped, and before and after the player is shut down. The check runs in
`app.cpp`, `runShutdownCheck`.

1. The tile is requested. Its `<role>` request is dispatched through `serveMapAssetQueue` and held by the server's read
   hook (`testHoldNextMapRead`) in the worker with its file open, so it is a read **tracked by the server**. The load is
   then cleared, and the read stays in flight, unwanted.
2. The harness's own player shutdown is skipped. When `~ModelViewer` begins, the player is running and the server is
   listening and connected. The first check asserts exactly this.
3. Immediately before `m_mapAssetWorker->stop()`, after the fix's statements, the probe calls `ipc->poll()` once. This
   is the timer's route, which needs no message to be retrieved.
4. When the close begins, the probe queues a wake (`QueueEvent(wxThreadEvent)`), as a wake posted just before the
   destructor would be. After `stop()` it runs the server's pending events (that wake, plus any the worker posted).
   It also invalidates a window of this thread, and checks that the shutdown pump retrieved a paint.
5. From the destructor's first statement, the checks count every poll that got past its check, every player message
   handled and every map callback (`UnityIpcServer::activity()`). After the pump, all of them must be 0. A wake handler
   that returns without serving is allowed. The worker's storage references must balance, and the player must be gone
   with the server stopped.

The end check "no map read is in flight at the end" accepts exactly the reads left for the shutdown. For the `wdt` role
that is one read: the WDT gates the others. For a later role it is two: the held read, and the tile's other ADT read,
dispatched beside it and queued in the worker behind it. The first battery ran the Classic clients with the `rootAdt`
role against a check that still wanted exactly one read in flight. The step then never armed the shutdown check. That
was the harness's mistake, not the product's: the check now takes the count it saw, and the runs below are with it.

| Build | Run | Result |
|---|---|---|
| `81f79982` + this test only (worktree `F:\wmv-05-base`, no fix) | `shut_81f79982_2` | **FAIL** (7 passed, 2 failed). Two polls got past their check: the direct one, and the queued wake's, which **completed the stopped read with three map callbacks inside `~ModelViewer`**. A wake posted by the worker was still waiting after `stop()` |
| `develop` + this test only | `shut_testonly_1` | **FAIL** (6 passed, 2 failed): the direct poll (`develop` differs from `81f79982` in none of the files involved but `modelviewer.cpp`'s Dracthyr code) |
| this branch | `shut_fix_2` | **PASS** (9 passed, 0 failed). 0 polls past their check, 0 messages, 0 map callbacks; 1 wake handled, which did nothing; references 1/1; the player exited after 734 ms |
| this branch, all four clients, the root ADT's read held (two reads in flight) | `S_wow`, `S_wow_classic_era`, `S_wow_classic`, `S_wow_classic_beta` (final battery) | **PASS** (9 passed, 0 failed) on each of the four |
| this branch, through the Maps viewer (a tile's root read held, the window closed) | `holdclose` (final suite) | **PASS** (6 checks, then the shutdown check: 9 passed, 0 failed; the player exited after 750 ms) |

`PROVEN_RUNTIME`. Logs are in `F:\wmv-build-fix\w05\shut`, `F:\wmv-build-fix\w05\battery_final\mine_shut` and
`F:\wmv-build-fix\w05\ui_final_run1`.

A finding from the runs: at `81f79982`, in the `shut_81f79982_1` run before the test queued its own wake, the worker's
wake from `stop()` stayed queued through the shutdown pump's five paints and was never handled. wx's message hook did
not run it there. This run therefore does not show the pump route that 18.1 described as possible. The direct poll and
the explicit pending-event run show both routes deterministically, and the fix closes both.

## 2. UI architecture

### 2.1 What exists, and what Maps reuses (investigation, before any control was written)

| Piece | Where | How Maps uses it |
|---|---|---|
| The viewer selector | command bar, `InitCommandBar`: radio tools drawn as one segmented control | a fourth tool, **Maps** (`ID_UI_MAPS`, `UiIcon::Maps`), after Buildings |
| The mode switch | `SetViewerMode`: one atomic, frozen layout batch; `workspacePanes(mode)` puts panels away | `ViewerMode::Maps`; Maps puts Animation, Model and Attachments away (as Textures does): the viewport gets the width |
| Command gating | `isCapabilityGated` / `capabilityRefusal` / `needsModelViewer` | Maps is refused when the client has no readable Map.db2 (`caps.maps`); every Models command is greyed in Maps, as in Buildings |
| Browse | `FileControl`: one search box (300 ms debounce, Enter forces), one tree, a status line; per-mode search memory | Maps lists the catalogue in the same tree with the same search; the tile grid sits under the tree in Maps only |
| The viewport | `UnityRendererHost`: the player's window, or a notice drawn over the parked player | the terrain in the player; a Maps notice while no tile is on screen (`MapsViewer::viewportNotice`) |
| Terrain in the player | Stage 03: layer 30, `TerrainViewMask`, the orbit camera kept and restored by `WmvTerrainViewMode` | unchanged |
| Camera | `WmvOrbitCamera` (left drag orbits, right drag pans, the wheel zooms), the map-object framing rule for the tile | unchanged; no second camera framework |
| Map catalogue | Stage 02 `WorldMapCatalog`: maps, WDT references, states | the source of the list; `noteInspection` (new) keeps the worker's WDT inspection |
| Terrain loads | Stage 03/04 `RequestTerrainTile`, protocol 13, 04B worker | unchanged, plus `wdtInspected` (no GUI-thread WDT read) |
| Display views | Stage 03/04 `WmvTerrainView` (shaded, chunks, wire, shadedwire, normals, textured, texturedlit, materials) | offered as display modes; switched at run time (protocol 14) |

### 2.2 The integration

![Maps entry](05-maps-viewer-ui/01-maps-entry.png)

- **Command bar**: Models | Textures | Buildings | **Maps**. Same tool art, same gating, same switch.
- **Browse in Maps** (`FileControl::InitMaps`): the catalogue's maps in five groups:
  - "Tile-grid maps (by Map.db2's type)": MapType 1 with a WDT reference the storage root lists. Expanded.
  - "World model maps (one WMO, no terrain tiles)": MapType 2 and 4.
  - "Maps of an unknown type".
  - "Maps whose WDT is not in this client's files": the reference is resolved, but the storage root does not list it.
    This is a lookup, not a read.
  - "Maps without a WDT file": no reference, or MapType 3.
  The groups other than the first are in the secondary text colour. The status line counts them. For Retail it reads
  "1187 maps: 744 tile-grid maps, 228 world model maps, 110 whose WDT is not in this client's files, 105 without a
  WDT, 0 of an unknown type". A row reads `<name>  [<MapID>]  <directory>`. The search matches every word in the name or
  the directory, and a number also matches the MapID: that map is pinned first, and Enter opens it.
- **The tile grid** (`MapTilePanel`, under the list in Maps only, two thirds of the height):
  - the map's name, MapID, directory and WDT reference, and its state line;
  - one owner-drawn window (`MapTileGrid`) with the coordinates on its edges, fitted to the box of the tiles the WDT
    flags, or the whole 64 x 64 grid (a box below it); the wheel zooms about the tile under the mouse, and a drag
    moves a zoomed grid;
  - the tile under the mouse with its coordinates and state;
  - the tile picked, with its state or load result;
  - the **Display** choice and **Clear terrain**.
- **State**: `MapsViewer` (new) owns all of it, with no widget. The panel draws it, and the viewport's notice and the
  command bar's label are read from it.

## 3. Screenshots

All are pictures of the viewer's own window: `PrintWindow` on a desktop of its own, the default layout,
taken by the final suite (`-mapsuitest`, section 15). They are not composites. The player's picture is part of the
window's own. Each is 1906 x 1073: the maximised window on a 1920 x 1080 desktop.

**1. Maps entry: the command bar's fourth viewer, Browse listing the catalogue, the notice "No map selected" (Retail)**

![01-maps-entry](05-maps-viewer-ui/01-maps-entry.png)

**2. The catalogue with Map.db2's names, MapIDs and directories, grouped; Eastern Kingdoms [0] picked, its grid fitted to the WDT's tiles**

![02-catalogue-real-names](05-maps-viewer-ui/02-catalogue-real-names.png)

**3a. Search by name: "Kalimdor"**

![03a-search-name](05-maps-viewer-ui/03a-search-name.png)

**3b. Search by MapID: "530" pins Outland [530] first**

![03b-search-mapid](05-maps-viewer-ui/03b-search-mapid.png)

**4. The selected map's tile grid, zoomed with the wheel about 32_48 (coordinates on the edges, the legend under it)**

![04-map-tile-grid](05-maps-viewer-ui/04-map-tile-grid.png)

**5 and 7. Tile 32_48 selected (the ring) and loaded, textured: every drawable chunk (256) with its textures, 16 hole cells**

![05-07-tile-selected-loaded-textured](05-maps-viewer-ui/05-07-tile-selected-loaded-textured.png)

**6. Loading: tile 32_48's terrain and textures being read and built (its root ADT read held in the worker by the test, so the state can be photographed)**

![06-tile-loading](05-maps-viewer-ui/06-tile-loading.png)

**8a. Display: Neutral (the Stage 03 diagnostic shading)**

![08a-neutral](05-maps-viewer-ui/08a-neutral.png)

**8b. Display: Chunk borders**

![08b-chunk-borders](05-maps-viewer-ui/08b-chunk-borders.png)

**8c. Display: Normals**

![08c-normals](05-maps-viewer-ui/08c-normals.png)

**8d. Display: Material states**

![08d-material-states](05-maps-viewer-ui/08d-material-states.png)

**8e. Display: Wireframe**

![08e-wireframe](05-maps-viewer-ui/08e-wireframe.png)

**8f. Display: Neutral with wireframe**

![08f-neutral-wireframe](05-maps-viewer-ui/08f-neutral-wireframe.png)

**8g. Display: Textured, diagnostic light (the viewer's light, not the game's)**

![08g-textured-diagnostic-light](05-maps-viewer-ui/08g-textured-diagnostic-light.png)

**9a. Camera: yaw 45, pitch 22**

![09a-camera-angle-a](05-maps-viewer-ui/09a-camera-angle-a.png)

**9b. Camera: yaw 200, pitch 50, 0.75 x the framing distance**

![09b-camera-angle-b](05-maps-viewer-ui/09b-camera-angle-b.png)

**9c. Camera: yaw 300, pitch 8, 0.55 x the distance (low over the terrain)**

![09c-camera-angle-c](05-maps-viewer-ui/09c-camera-angle-c.png)

**10a. An unavailable tile: 0_0 is not part of Eastern Kingdoms (its WDT does not flag it); the tile on screen stays**

![10a-tile-not-in-map](05-maps-viewer-ui/10a-tile-not-in-map.png)

**10b. An unsupported material: Classic Era Azeroth 32_48 (4-bit alpha) drawn in neutral shading, and said so**

![10b-unsupported-material-era-azeroth](05-maps-viewer-ui/10b-unsupported-material-era-azeroth.png)

**10c. An unavailable map: Moonglade [1534], its WDT not in client storage, with the catalogue's reason**

![10c-unavailable-map](05-maps-viewer-ui/10c-unavailable-map.png)

**11. Retail terrain: Eastern Kingdoms 29_50, picked by rapid re-clicks (the earlier loads superseded)**

![11-retail-azeroth-29_50](05-maps-viewer-ui/11-retail-azeroth-29_50.png)

**12a. Classic Era: map 2853, tile 34_50, textured**

![12a-classic-era-2853-34_50](05-maps-viewer-ui/12a-classic-era-2853-34_50.png)

**12b. MoP Classic: Azeroth 32_48**

![12b-mop-classic-azeroth-32_48](05-maps-viewer-ui/12b-mop-classic-azeroth-32_48.png)

**12c. MoP Classic: Pandaria [870] 30_29**

![12c-mop-classic-pandaria-30_29](05-maps-viewer-ui/12c-mop-classic-pandaria-30_29.png)

**12d. Classic Beta: Azeroth 32_48**

![12d-classic-beta-azeroth-32_48](05-maps-viewer-ui/12d-classic-beta-azeroth-32_48.png)

**13. Models after leaving Maps: the terrain cleared, a creature loaded and shown**

![13-models-after-maps](05-maps-viewer-ui/13-models-after-maps.png)

**14. Buildings after leaving Maps: the Stormwind jail (WMO 108631) shown, no terrain**

![14-buildings-after-maps](05-maps-viewer-ui/14-buildings-after-maps.png)

## 4. Map catalogue integration

- **Source.** The catalogue is the active client's Stage 02 `WorldMapCatalog` (`ModelViewer::mapCatalog()`), built
  while the client loads and replaced when another client loads. No other list is used: no outside map list and no
  made-up name. A name is Map.db2's `MapName_lang`, in the storage's locale. A map whose name is empty is listed by its
  directory.
- **Listing.** `FileControl::InitMaps` runs on the GUI thread over the catalogue's in-memory rows, with no file read. It
  lists 1,187 Retail maps in 18 to 24 ms (final suite). It runs when Maps is entered, on a search, and after a client
  load completes, in the load's deferred step: inside `LoadWoW` the client does not count as active yet.
- **States respected.** The groups come from the catalogue's own fields (`WdtReference::Kind`, `statedWdtForm()`) and the
  storage root's listing, never from a guess. A row is not a promise that terrain exists: whether the WDT can be read is found when the map is picked
  (section 5). That covers not in client storage, not installed locally, unreadable, and unsupported (a path the root
  cannot answer for). What its tiles are comes from the WDT itself, through the player's parser. The census of every
  map of all four clients (section 5.4) shows each case with its reason.
- **Unavailable or unsupported maps** keep their row (secondary colour). Picking one shows the catalogue's own reason
  in the tile panel and on the viewport notice. Examples: "This client gives the map no WDT file: ..." (Retail, 105
  maps), and "The map's WDT is not in client storage: the client's storage root does not list FileDataID 2449139"
  (Retail map 1534; 110 Retail maps, 58 MoP, 2 Beta, 1 Era). A WMO-only WDT ("GlobalWmo" to the parser) gets an empty
  grid and "This map has no terrain tiles: it is one world model (open it in Buildings)".
- **Search.** Word search over name and directory (case-insensitive), and MapID: the exact map is pinned first, and
  Enter opens it. Each viewer mode keeps its own search (the per-mode arrays grew from 3 to 4).

## 5. Tile selection rules

### 5.1 The WDT, off the GUI thread, parsed by the verified parser

A pick (`MapsViewer::SelectMap`) runs on the GUI thread for 0.15 ms. It looks the map up, takes the client epoch and
storage generation, makes sure the worker holds a lease for them (`EnsureMapAssetLease`), and hands the worker one job:

- the map's WDT, by Map.db2's FileDataID (Retail, Beta), or by the directory path through the storage root's names
  (Era, MoP; new `MapAssetWorker::Job::path`);
- read whole, frame-checked, MD5'd against the content key, with the catalogue's own `inspectInStorage`.

The result comes back through the server's poll and wake (`UnityIpcServer::submitViewerRead` / `onViewerMapRead`). It is
checked against the pick's ticket, epoch and generation, and kept as the map's inspection (`WorldMapCatalog::
noteInspection`). Only an Available WDT goes on. Its bytes go to the player (`inspectMapTiles`, protocol 14, a binary
payload). The player checks the MD5 again and parses the bytes with `WdtParser` (Stage 01, verified). It answers with
the table, `mapTilesInspected` (`WmvMapTiles.cs`), for all 4,096 tiles, index `y * 64 + x`:

- the MAIN flag (bit 0);
- whether MAID names a root ADT (bit 1);
- MAID words 0 (root ADT) and 3 (tex0 ADT).

The host checks the table decodes to its sizes and counts, and refuses it otherwise.

### 5.2 A tile's state

| State | Rule | Colour |
|---|---|---|
| not part of the map | MAIN flag clear | the empty cell |
| no root ADT named | MAIN flag set, MAID word 0 is 0 | grey |
| not in this client's files | MAID names a root ADT; `storageRootEntry(fdid).listed` is false | brown |
| not downloaded | listed; `.local` is false | amber |
| available | listed and local | the accent |

The storage root's answer is a lookup, not a read: 0.015 ms median per map, 0.31 ms at most (Retail map 2444, 1,634
roots). A tile is never called available because its map exists: the root ADT itself must be in the client's storage
and on this computer. `MAID word 0` is the root ADT the terrain load asks for (Stage 03), so the grid and the load agree
on the file.

### 5.3 Orientation

x is the column, growing east, and y is the row, growing south. The root ADT of cell (x, y) is
`<directory>_<x>_<y>.adt`, as the audit established (`00-audit.md`: MAID index `i` names `x = i % 64`, `y = i / 64` for
all 44,018 Retail tiles). The grid draws column x and row y. A click returns (x, y), and `RequestTerrainTile(map, x, y)`
sends `tileX = x`, `tileY = y`.

**The orientation check** (`orient`, `scan`) proves it on real files. For every tile whose MAID names a root ADT, the
file name the client (or the listfile) gives that FileDataID must be `<directory>_<x>_<y>.adt` for the cell it is drawn
in. A transposed or shifted grid fails on every tile off the diagonal.

### 5.4 The census: every map of all four clients (`scan`, PROVEN_RUNTIME)

| Client | Maps | Grid read | Of them with tiles | No WDT | WDT not in storage | Tiles | Named root ADTs: match / mismatch | Available | Not local / not in storage / no root |
|---|---|---|---|---|---|---|---|---|---|
| Retail 12.1.5.70391 | 1187 | 972 | 731 | 105 | 110 | 44,078 | 44,078 / 0 | 44,078 | 0 / 0 / 0 |
| Classic Era 1.15.9.70003 | 59 | 58 | 37 | 0 | 1 | 2,719 | 2,719 / 0 | 2,719 | 0 / 0 / 0 |
| MoP Classic 5.5.4.70268 | 285 | 227 | 194 | 0 | 58 | 9,848 | 9,848 / 0 | 9,848 | 0 / 0 / 0 |
| Classic Beta 1.60.1.70374 | 71 | 69 | 48 | 0 | 2 | 3,322 | 3,322 / 0 | 3,322 | 0 / 0 / 0 |

Every root ADT named by every WDT of the four clients is installed on this computer today, so the "not downloaded",
"not in this client's files" and "no root ADT" states have no real case here. They are SKIP with that reason. The rules
are unit-checked in the player (a synthetic WDT with a MAIN-only tile), and their colours and words are in the panel.

## 6. Terrain loading workflow

Map catalogue -> WDT (worker, protocol 14 table) -> click -> `ModelViewer::RequestTerrainTile(map, x, y, why,
wdtInspected = true)` -> `loadTerrainTile` (protocol 13) -> the player asks `getMapAsset` for the WDT, the root ADT, the
tex0 ADT and the textures -> the host decides each request (`decideMapAssetRequest`), the 04B worker reads it, and the
host checks it again and answers -> the player builds the mesh (Stage 03) and the material (Stage 04) -> the tile on
layer 30, framed -> `terrainTileLoaded`.

- No parser, reader, blending formula or coordinate transform was added or changed. The geometry dumps and material
  composites are byte-identical to the 04B build's (section 16).
- `wdtInspected`: `ResolveTerrainTile` used to read the WDT whole on the GUI thread before every load. Measured over
  all 972 Retail WDTs read by the worker, that read takes median 0.51, p95 0.66, at most 2.60 ms (972) over every Retail WDT the census read. On the GUI thread the maximum would be a stall of that
  size. A Maps load now uses the inspection the worker made for the grid, so the GUI thread reads nothing. The request
  itself is median 0.19, p95 0.26, at most 0.29 ms (40) on the GUI thread. The harness path is unchanged, and still inspects on the GUI thread as before.
- Client epoch, storage generation, the load serial and the read tracker are the existing ones.

## 7. Loading states and errors

| State | Where it shows | Words (examples from the runs) |
|---|---|---|
| no map selected | notice, panel | "No map selected -- Select a map in Browse, or search for one by name or MapID." |
| map selected (reading) | notice, panel | "Reading Eastern Kingdoms [0] -- Reading the map's tiles." |
| map cannot be shown | notice, panel | "... cannot be shown -- The map's WDT is not in client storage: the client's storage root does not list FileDataID 2449139" |
| grid known, no tile | notice, panel | "No tile selected -- Click an available tile in the tile grid under the map list in Browse." |
| tile unavailable | panel, notice when nothing is on screen | "Tile 0_0 is not part of this map: its WDT does not flag it (MAIN)." |
| tile loading | notice (first tile), panel | "Loading tile 32_48 -- Eastern Kingdoms [0]: the tile's terrain and textures are being read and built." |
| tile loaded | panel, command bar label | "Tile 32_48 loaded in 0.46 s. Textured: every drawable chunk (256) drawn with its textures. 16 hole cell(s)." |
| tile loaded, not textured | panel | "Not textured: this tile's material is not supported (no chunk can be drawn textured: alphaUnsupported 256); every chunk is drawn in neutral diagnostic shading." (Era Azeroth, 4-bit alpha) |
| tile loaded, partly textured | panel | "Partly textured: N of M chunks drawn with their textures; the others in neutral shading ..." |
| tile failed | notice, panel | "Tile 32_48 could not be loaded: timed out after 30 s waiting for the root ADT answer ..." (the player's reason, with the host's state and detail when it answered one) |
| tile superseded | log, panel | a load replaced by a newer click is answered "superseded" by the player and never shown ("load N ... answered superseded after this viewer moved on: not shown") |
| terrain cleared | notice, panel | "The terrain was cleared. Click a tile to load it again." (Clear terrain, leaving Maps, a viewer restart) |
| renderer too old | notice, panel | "The Unity renderer build in use speaks protocol 13; listing a map's tiles needs protocol 14 ..." |

A tile whose material is unavailable or unsupported is never called textured. Its words say what was drawn, and the
fallback is the existing Stage 03/04 neutral shading.

## 8. Client support matrix

| Client | Catalogue | WDT by | Grid | Tile load | Material | Evidence |
|---|---|---|---|---|---|---|
| Retail 12.1.5.70391 | 1,187 maps | FileDataID | PASS | PASS (Eastern Kingdoms 32_48, 29_50, 30_50, and 31_47 to 33_49 in `repeat`) | textured | `retail`, `retailscan`, `repeat` |
| Classic Era 1.15.9.70003 | 59 maps | path | PASS | PASS (Azeroth 32_48; 2853 34_50) | Azeroth: **unsupported** (4-bit alpha, neutral fallback, stated); 2853: textured | `era` |
| MoP Classic 5.5.4.70268 | 285 maps | path | PASS | PASS (Azeroth 32_48; Pandaria 30_29) | textured | `mop` |
| Classic Beta 1.60.1.70374 | 71 maps | FileDataID | PASS | PASS (Azeroth 32_48) | textured | `beta` |
| Legacy MPQ clients | catalogue Unsupported | -- | UNSUPPORTED | -- | -- | the Maps tool is refused with the catalogue's reason |

## 9. Camera integration

- **One camera framework.** The player's `WmvOrbitCamera` draws the terrain view as Stage 03 set it up: left drag
  orbits, right drag pans, the wheel zooms. `WmvTerrainViewMode.Enter` keeps the model view's orbit state and does not
  disable the orbit. Each tile is framed by the map-object rule: yaw 135, pitch 30, distance r / sin(half FOV) x 1.25,
  far = max(100, 20 d, reach x 1.2), near = max(0.01 d, far x 1e-4). On leaving the terrain view, the model view's
  camera comes back exactly. No line of the camera, its framing or the M2/WMO paths changed: the M2 and WMO pictures
  are identical (section 16).
- **A display-mode switch keeps the camera.** `RebuildShown` does not frame the tile again.
- **Angles.** Three captures from different orbits of the same tile (`WMV_VIEWPORT_ORBIT`, the player's documented
  framing override, applied as the tile is framed): yaw 45 / pitch 22, yaw 200 / pitch 50 at 0.75 x the distance, and
  yaw 300 / pitch 8 at 0.55 x (low over the terrain, mountains in front). None shows near-plane clipping or missing
  far terrain.
- **Interactive mouse input could not be driven headlessly.** Mouse messages posted to the player's window do not
  reach Unity's input, which reads raw input, and `SendInput` would reach the user's game. The orbit/pan/zoom code path
  is the unchanged Stage 03 one (`PROVEN_SOURCE`). Trying it by hand is a manual step (section 15.2).

## 10. Display modes

The modes are the player's own views, unchanged, with no new shading:

| Mode | Player view | Notes |
|---|---|---|
| Textured (default) | `auto` | Stage 04's rule: textured where the material has drawable chunks, the Stage 03 neutral shading otherwise |
| Textured, diagnostic light | `texturedlit` | the composite under the viewer's diagnostic light (not the game's) |
| Neutral | `shaded` | |
| Chunk borders | `chunks` | |
| Wireframe | `wire` | |
| Neutral with wireframe | `shadedwire` | |
| Normals | `normals` | |
| Material states | `materials` | each chunk in its state's colour: textured, layer effect, unsupported alpha, missing texture ... |

Holes and unsupported geometry show in every mode as gaps: a chunk with no triangles has no object. The panel names
the hole cells and any unsupported chunks of the tile. The plain-weights comparison (`texturedplainweights`) is a
validation fixture that fetches textures of its own, so it is not offered, and `setTerrainView` refuses it.

**No reload.** `setTerrainView` (protocol 14) sets the view for later tiles, and the player rebuilds the tile on screen
from what it holds (`WmvTerrainSession.RebuildShown`): the parsed mesh, the material, and the textures its load settled.

- The new tile is built inactive beside the old one, sharing its uploaded textures. They change hands at the commit
  (`WmvRuntimeTerrainTile.Hand`) and are never uploaded twice.
- A texture the old view did not upload is uploaded from data kept for it. The data are kept only while the tile
  is shown in a view that uploaded nothing, and are released by a textured view or with the tile.
- A rebuild that throws disposes only the new tile.

No request goes to the host. Measured: median 16.5, p95 32.4, at most 32.4 ms (8) from the click to the player's answer, the rebuild itself median 6.6, p95 12.4, at most 12.4 ms (8)
(final suite). The player self-test proves it with synthetic tiles: the same Texture2D count and GPU bytes before and
after, no request sent, no framing, and a failed rebuild leaving the old tile whole.

## 11. Scene lifecycle and resource disposal

| Transition | What happens | Verified (final suite `retail`, `repeat`) |
|---|---|---|
| Models -> Maps | notice until a tile is built; the model stays loaded in the player, hidden by the terrain view's mask once a tile shows | `expect:terrain:0` before a click |
| Maps -> Models / Buildings / Textures | `MapsViewer::Left()` clears the terrain (`clearTerrain`): the player disposes the tile and leaves the terrain view; its model view comes back as it was left | `expect:terrain:0` after each; the capybara and the jail shown after leaving Maps |
| Buildings -> Maps | the world model stays alive in the player, hidden (Stage 03 design: a terrain load never replaces the model on screen) | runtimeState liveMapObjects 1, terrainView 1 |
| a new tile | the commit disposes the old tile in the same block | repeated switching: tallies flat (section 13) |
| Clear terrain | the tile disposed, the view left | tallies 0 |
| a client switch | `ResetClientState` clears the terrain and cancels the viewer's read; `MapsViewer` forgets the map, grid and shown tile; Browse re-lists from the new catalogue; a late read of the old storage is answered stale | `switch` (section 12.2) |
| a view switch | the old tile disposed after the new one is built; textures handed over | self-test, tallies |

- Old map assets cannot overwrite another scene. Terrain draws only on layer 30, and only while the terrain view is
  entered. Every terrain report is matched to the current load serial: one for an earlier load is not shown, and the
  log says so.
- Client-specific textures are not reused after a client switch: a tile's textures are owned by the tile, and the
  tile is disposed with the switch.

## 12. Client switching and isolation

### 12.1 What isolates a client

- **The viewer's own read** carries a ticket, the client epoch and the storage generation. The worker refuses a job
  for another storage than its lease (04B). `MapsViewer::OnWdtRead` drops a result whose ticket is not the pick's, or
  whose epoch or generation is not the current client's.
- **The tile table** carries the inspection serial, the map and the epoch. A table for an earlier pick, another map or
  another client is dropped.
- **Terrain loads** are the existing ones. `ResetClientState` cancels every read (the server's and the viewer's),
  drops the worker's lease, raises the epoch, clears the terrain and calls `MapsViewer::ClientReplaced()`. A late
  `getMapAsset` result is answered "stale" by the completion check (04B D), and a report for an old load is not shown.
- **The catalogue and the list.** The catalogue is a new object per client. Browse re-lists after the load returns, and
  the grid is empty until a map of the new client is picked.

### 12.2 The runs (final suite `switch`, through the interface: `LoadWoW(&config)`, what File > Load World of Warcraft's dialog runs)

| Case | Sequence | Result |
|---|---|---|
| no map selected | Retail -> Era | **PASS**: the map, grid and notice reset (epoch 2 -> 3); an Era map picked after (687 tiles) |
| a map selected | Era (Azeroth picked, grid known) -> MoP | **PASS**: the Era map and grid forgotten, terrain 0; MoP Azeroth's grid after (839 tiles) |
| a tile loading | MoP, tile clicked -> Beta at once | **PASS**: the MoP load's root and tex0 requests answered "stale" (epoch 4, now 5), nothing shown; Beta 32_48 loaded after (0.29 s) |
| a tile loaded | Beta, 32_48 on screen -> Retail | **PASS**: Beta's tile disposed (terrain 0); Retail's map picked after |
| a read held in the worker | Retail, 32_48 clicked, its root ADT read held with the file open -> Era -> released | **PASS**: on release the held root read (2.8 s, its file open) is answered "stale" (epoch 6, now 7), its tex0 read cancelled, nothing shown; Era 32_48 loaded after (0.15 s, its unsupported material stated) |

## 13. GUI responsiveness

All times are GUI-thread milliseconds on this computer (Ryzen 9 9950X3D), PROVEN_RUNTIME, final suite unless named.
"GUI round" is one poll plus one pass of the wx queue in the harness's pump (`g_pumpBusy`), so it also contains
handling the harness did not cause.

| Operation | Measured |
|---|---|
| Browse lists the maps (1,187 Retail) | 18 to 24 ms (`InitMaps`, inside one frozen pass) |
| Entering Maps (the whole switch) | 159 to 247 ms (247 is a run's first entry, which also creates the tile panel); of it 43-88 ms is the tree control's thaw after the list is made (profiled in an earlier build of this branch; entering Models costs 72 to 90 ms, Buildings 47 to 69 ms after its first entry; the first entries of Textures and Buildings cost 818 and 465 to 525 ms building their own catalogues) |
| A map picked (`SelectMap`) | 0.15 ms; the GUI update that follows (panel, notice, labels) at most 28 ms (median 5.6 ms over 312 updates) |
| The WDT read (worker, not GUI) | median 0.51, p95 0.66, at most 2.60 ms (972) over every Retail WDT the census read |
| The tile table (player round trip; the player handles messages once a frame, at the display's refresh rate since PR #114) | median 20.4, p95 40.2, at most 54.0 ms (972) (Retail census) |
| The grid built from the table (storage-root lookups) | median 0.015, p95 0.053, at most 0.308 ms (972) (Retail census) |
| From the pick to the grid shown | median 30.0, p95 46.8, at most 63.0 ms (972) (Retail census) |
| A tile requested (`RequestTerrainTile`, no WDT read) | median 0.19, p95 0.26, at most 0.29 ms (40) |
| A tile loaded (click to the "built" report; the player's own total) | median 254.8, p95 330.8, at most 515.7 ms (40); the player's own total median 233.3, p95 283.3, at most 499.3 ms (40) |
| The tile put on screen (the player's own phases of a load: parse, mesh build, upload, first frame drawn) | parse 2.2 ms, mesh build 9.1 ms, upload 8.3 ms (at most 18.4), first frame 24.4 ms (at most 35.9): medians over 40 loads |
| A display mode applied (click to the player's answer; its rebuild) | median 16.5, p95 32.4, at most 32.4 ms (8) |
| The longest single GUI round during the whole Retail run | 525 ms (the mode switches; outside them 114 ms: clearing the search, where Browse re-lists 1,187 maps, 89 ms of it the list) |
| The catalogue scan of 1,187 maps (one pick after another) | 59.6 s, the longest GUI round 61.4 ms |

Nothing of the Maps viewer reads CASC on the GUI thread: the WDT is read by the worker, and the root ADTs are only
looked up in the storage root. The grid is one window, and its cells are rectangles drawn in one paint. Re-listing
after a search rebuilds the tree rows only, as every Browse search does. A view switch reads nothing.

**Not acceptable, and documented: entering Maps** costs 159-247 ms in one GUI round, about twice a switch to Models.
The cost is the native tree control re-laying out 1,187 freshly inserted rows when the frozen window is thawed. A lazy
list (rows of a group added on expand) would remove most of it. That is a follow-up, not done here.

## 14. Memory

| Point (final suite `repeat`, Retail) | Host private MB | Player GPU dedicated MB | Terrain meshes / materials / textures (player) |
|---|---|---|---|
| Maps entered, Eastern Kingdoms picked, no tile | 2228.4 | 220.4 | 0 / 0 / 0 (0 KB) |
| after 12 tile switches (six tiles) | 2231.0 | 283.2 | 256 / 256 / 6 (512 KB) |
| after 24 tile switches | 2231.0 | 283.2 | 256 / 256 / 6 (512 KB) |
| after Clear terrain | 2231.0 | 277.7 | 0 / 0 / 0 (0 KB) |
| after leaving Maps for Models | 2230.9 | 277.7 | 0 / 0 / 0 (0 KB) |
| Buildings, WMO 108631 loaded | 2240.5 | 426.6 | 0 / 0 / 0 (0 KB) |
| back in Maps, 32_48 loaded | 2241.2 | 451.7 | 256 / 256 / 10 (854 KB) |
| after the client change to Classic Era (2.5 s) | 1208.6 | 446.7 | 0 / 0 / 0 (0 KB) |

- 24 tile switches over six tiles: 2,228.4 MB host private before, 2,231.0 MB after 12 and after 24; the player holds one tile (256 meshes, 256 materials, 6 textures) after 12 and after 24 (host and player tallies flat after the first tile).
- After Clear terrain, and after leaving Maps for Models and Buildings, the terrain tallies are 0.
- After a client change (Retail -> Era), the host's private memory drops with the smaller client. The player's GPU memory does not fall with it (446.7 MB): the world model of the earlier Buildings visit stays alive in the player across the switch. The suite's second pass, which runs the same sequence and then waits 5 s more and loads Era Azeroth 32_48, reads the same (447.6 MB 7.5 s after the change; 1,206.5 MB host and 447.6 MB GPU with the Era tile, which has no terrain textures because its material is unsupported). That is
  inherited: no viewer mode tells the player to drop the model it holds when the client changes. See
  section 18.
- GPU memory is the player process's `\GPU Process Memory\Dedicated Usage`, sampled about once a second. Each row
  takes the last sample before its step ended. The texture bytes are the player's own estimate for its terrain textures.

## 15. Tests

### 15.1 Automated

| Test | Where | Result |
|---|---|---|
| Shutdown re-entrancy (18.1) | `-terraintile ... readshutdownserved` | FAIL at `81f79982` (7/2), PASS here (9/0) with the `wdt` role, and with the `rootAdt` role on all four clients |
| Player self-test. New checks: the tile table at `y*64+x`, never transposed; a no-MAID WDT; a bad MD5, no bytes or a malformed table refused; the message line; runtime views; a rebuild with texture hand-over, no request and no framing; a failed rebuild keeps the old tile; kept data uploaded by a textured rebuild and released; kept only for a host-chosen view; a view switch in the report window reported "built"; tallies 0 after a clear | `-wmvLifecycleTest` | **855 passed, 0 failed** |
| Maps GUI suite, Retail (entry, search by name and MapID, an unavailable map, grid, zoom, load, loading state, 8 display modes, an unavailable tile, rapid re-click (supersede), keyboard pick, clear, Models/Buildings round trips) | `-mapsuitest`, `retail` | **PASS** (86 passed, 0 failed) |
| Catalogue scan + orientation, all four clients | `scan`, `orient` | **PASS**: every map of the four clients picked (Retail 1,187, Era 59, MoP 285, Beta 71); 0 orientation mismatches over 59,967 named root ADTs |
| Classic Era / MoP / Beta tiles | `era`, `mop`, `beta` | **PASS** (Era 18, MoP 18, Beta 14 checks, 0 failed) |
| Client switching (5 cases) | `switch` | **PASS** (44 passed, 0 failed) |
| Close during an active read, through the GUI | `holdclose` | **PASS** (6 checks, then the shutdown check: 9 passed, 0 failed; the player exited after 750 ms) |
| Repeated switching and memory | `repeat` | **PASS** (61 passed, 0 failed) |
| Viewer-mode switching | `modes` | **PASS** (13 passed, 0 failed) |
| The whole suite again, a second pass (repeatability) | the 12 runs above | **PASS**, 12 of 12 (`ui_final_run2`) |
| Stage 03/04/04B worker battery (W) | `-terraintile`, 6 runs | **PASS**, 6 of 6 |
| Stage 03/04 harness battery (H: late answers, switches, lifecycle, protocol-12 player) | 12 runs | 11 of 12 **PASS**. `H_B_missingtex0` fails for an environment reason: the tex0 it expects to be missing (5862455) is installed now, so "unavailable" cannot be reproduced (SKIP, as in 04B) |
| Geometry dumps vs the 04B host, same data | 6 tiles, 3 clients | **6 of 6 byte-identical** |
| Material composites vs the 04B host (V1/V2 golden tiles) | 11 runs | **98 of 98 dumps identical**; checker PASS in every run |
| M2 (bear, clocks pinned) and WMO 108631 pictures | `-unityipctest` | identical to the 04B host's; WMO = the long-standing pin `D3DB7749` |
| Map catalogue sequences (`-mapcatalogtest`) | 4 sequences | **PASS**, 4 of 4 (44, 44, 27 and 27 checks) |

### 15.2 Manual (GUI steps for the user; the harness cannot press the mouse inside the Unity player)

1. Start the viewer normally and load a client. Click **Maps** in the command bar.
2. Search "Elwynn" or "1" in Browse. Pick Eastern Kingdoms (0).
3. In the grid, point at tiles (the coordinates show under it), zoom with the wheel, and drag the zoomed grid.
4. Click tile 32_48 (Elwynn Forest). The terrain loads in about 0.3 s.
5. In the viewport: left drag orbits, right drag pans, the wheel zooms. Look at the mountains from low down.
6. Pick each **Display** mode. The tile changes at once, with no loading.
7. Click another tile. Click **Clear terrain**.
8. Go to Models, Buildings and back. Close the viewer while a tile is loading.
9. File > Load World of Warcraft: change the client while in Maps. The list and the grid are the new client's.

## 16. Preserved rendering correctness

See 15.1. The geometry and material outputs of this branch are byte-identical to those of the 04B host, on the same
client data in the same session. The reference images of `04-terrain-materials.md` were made from older client files
(12.1.0.69933). They were not regenerated: the comparison that matters here is "unchanged by this branch". No shader,
blending formula, coordinate transform, parser or camera code changed. The player's only changes to existing loads are
additive: the `view` field of `terrainTileLoaded`, and prepared texture data kept for a tile shown in a view that
uploaded nothing.

## 17. Adversarial review

Three independent reviewers read the finished change with the ten claims of the specification split between them.
They were told to disprove the claims from source, test outputs and the client-derived data, and to build and run
nothing. Every finding was checked against the code, then fixed and re-tested before the final suite.

| # | Finding | Reviewer's grade | Fix | Proof after the fix |
|---|---|---|---|---|
| 1 | Leaving Maps while a load was still in flight, after an unavailable tile was picked (or a new map, or a refused request), skipped `clearTerrain`; the tile then came to the Models/Buildings viewport | major, confirmed | `MapsViewer::Left()` always clears; picking another map clears the old map's terrain and load | final `retail`: `expect:terrain:0` after every exit |
| 2 | A view switch in the two frames between a load's adoption and its report made the player report the tile on screen "superseded" | major, confirmed | `FinishBuilt` recognises the rebuild of its own load (same load number, same tile) and reports it "built" | new self-test check (855/0) |
| 3 | With no player listening (the player missing), the viewer's WDT reads were never taken back: each pick left a result held by the worker | major, confirmed | no read without a listening server (the pick waits for the player, `OnPlayerReady` starts it); at most one viewer read with the worker, a later pick waits for it | code path; the worker-side bound is now 2 x 8 MiB + 4 MiB |
| 4 | Picking a map while a tile loaded showed the old map's tile under the new map's title | minor, confirmed (two reviewers) | (1): the old map's terrain and load are cleared | |
| 5 | Prepared texture data were kept for `-terraintile` loads in non-textured command-line views (a change for existing loads) | minor, confirmed | kept only when the host chose the view (`setTerrainView`) | new self-test check: none kept otherwise |
| 6 | The tile panel's labels kept the old client's text after a switch made in another viewer | minor, confirmed | refreshed when Maps is entered | |
| 7 | A late `terrainViewApplied` (two picks in a row, or after a clear) was shown | minor, confirmed | only the answer to the view asked last counts; an "applied" with no tile on screen reads "applies to the next tile" | |
| 8 | Holding Enter on the grid re-sent the load at key-repeat rate | minor, confirmed | key repeat ignored; a tile already loading is not asked for again | |
| 9 | The Model panel could be opened in Maps (the hidden canvas character editable) | minor | refused in Maps, as in Textures | |
| 10 | A pane resize queued one panel refresh per size event | minor | one refresh at a time | |
| 11 | Browse grouped by Map.db2's type only: 18 Retail MapType-1 maps whose WDT is not in storage listed as terrain maps | minor, confirmed with data | a group "Maps whose WDT is not in this client's files" (storage-root lookup, no read); the terrain group renamed "Tile-grid maps (by Map.db2's type)" | final `retail` listing |
| 12 | A pick made while a client loaded left an empty reason after the load | minor | `ClientLoaded` starts clean | |
| 13 | `detachForShutdown` gated polls, not sends | minor (plausible) | `queueLineParts` / `queueFrame` return once detached | shutdown checks |
| 14 | A textured rebuild that throws after uploading some textures from kept data loses those data | minor (plausible) | not fixed: only an exception inside the build (out of memory) reaches it; recorded in section 18 | |
| 15 | The orientation check compares data, not pixels: `TileRect`/`TileAt` are inverses of each other | harness weakness | recorded; the drawing uses the same layout as `TileRect` (`MapTileGrid::OnPaint`), and the selected-tile ring in the screenshots is at the clicked cell | |

**Claims not disproved** (all three reviewers):

- shutdown safety: the notifier is cleared under the worker's lock, polls return, queued wakes are harmless, the
  join and lease are untouched, there is no deadlock, and the player is closed in 3 s;
- tile orientation, end to end: player index, host decode, drawing, hit-testing, `tileX`/`tileY` on the wire;
- the WDT identity between the grid and the load;
- client isolation of data: no previous-client bytes or tables reach the grid or the viewport;
- the camera mask and the M2/WMO paths;
- disposal accounting: tallies balanced, no double destroy;
- IPC buffering: no periodic source, the `inspectMapTiles` gate, and the line sizes.

## 18. Remaining limitations

- **No in-game comparison.** Nothing here claims the tiles look as in the game. The textured mode is Stage 04's
  composite: no lighting, water, objects, shadows or sky, under the limits of `04-terrain-materials.md`. For example, MoP
  Pandaria 30_29 shows high-contrast white patches in the composite, and whether the game shows them so is UNKNOWN.
- **Interactive camera input** was not driven headlessly (section 9). The angles were taken with the player's orbit
  override.
- **Entering Maps** takes 100-160 ms of GUI time (section 13). A lazy list is a follow-up.
- **No real "not downloaded", "not in client files" or "no root ADT" tile** exists on this computer (section 5.4).
- **The player keeps the last model or world model across a client switch** (hidden; inherited, not Maps-specific).
- **Outgoing IPC buffering** (04b 18.2) is still bounded only for the shipped player's requests. The new
  `inspectMapTiles` is sent once per pick and gated on the send high water. `setTerrainView` is sent once per user
  choice. There is no periodic Maps message, and no progress or state stream. The transport-wide cap remains a separate
  fix.
- **The viewer's own WDT read** adds at most one job (at most 4 MiB) beside 04B's two reads in flight. The worker-side
  bound becomes 2 x 8 MiB + 4 MiB.
- **The Maps tool's icon is sometimes not drawn.** Measured over the final suite's pictures, the Maps icon is drawn in
  some runs and missing in others, for a whole run at a time and in either state of the tool. The Models, Textures and
  Buildings icons are drawn in every picture. The icons are rasterised from SVG once per icon, colour and size
  (`UiIcons::bundle`), so a run that misses the Maps icon misses one cached bitmap. The cause is UNKNOWN, and it is not
  fixed here. It is the same symptom the owner reported for the Models icon before, and it can now be reproduced on a
  hidden desktop, so it is a follow-up of its own.
- **Single tile only**, as specified: no streaming, neighbours, LOD, WMO or M2 placement, liquids, sky, fog or lighting.
- **Map 1582's directory** shows a replacement character: the client's Map.db2 string holds a byte that is not valid
  UTF-8, and the Stage 02 catalogue reads it so (every catalogue run since Stage 02 shows the same).

## 19. Implications for Stage 06 (streaming)

- **What carries over.** The grid's per-tile availability, the worker read path for the viewer's own files, the client
  isolation (epoch, generation, serial), and the player's scene dictionary keyed by (map, x, y) (Stage 03 already keys
  it so).
- **What must change.**
  - Today one load is in flight and a commit disposes every other tile. Streaming needs several tiles alive, a budget
    per tile, and eviction.
  - The 04B worker reads one file at a time (FIFO). Neighbouring tiles would queue behind each other.
  - The per-load limits (128 textures, 128 MiB) would need a per-scene budget.
  - Textures are owned per tile. Sharing them between tiles (out of scope here) would need reference counts.
  - The camera framing per tile must give way to a free camera over a set of tiles.
- **What to fix first.**
  - The transport-wide send cap (04b 18.2): streaming multiplies the answers.
  - The Maps entry cost (lazy list).
  - The player's retained model across a client switch.

## 20. Grades

### 20.1 Acceptance criteria (specification section 20)

Every step below was taken through the viewer's real window by the final suite (`-mapsuitest`), not through the
terrain harness: commands go through the command bar's tool, Browse's search box and tree, the tile grid's mouse and
keys, the Display choice and the Clear terrain button, on the viewer's own event paths.

| # | A user can | Grade | Evidence |
|---|---|---|---|
| 1 | open WMW normally | PASS | every run starts the real executable with its normal start-up (maximised window, its own player) |
| 2 | load any supported WoW client | PASS | Retail, Classic Era, MoP Classic, Classic Beta (`retail`, `era`, `mop`, `beta`); legacy MPQ clients: UNSUPPORTED, refused with the catalogue's reason |
| 3 | open Maps | PASS | the command bar's Maps tool, screenshot 1 |
| 4 | browse or search its maps | PASS | screenshots 2, 3a, 3b; name, directory and MapID search |
| 5 | select a supported map | PASS | screenshot 4; every map of all four clients picked by the census (section 5.4) |
| 6 | identify an available tile | PASS | the grid's colours, the hover line and the tile line (screenshots 4, 5) |
| 7 | click that tile | PASS | a click on the grid window (posted mouse messages); Enter on the keyboard |
| 8 | see its real terrain and supported textures | PASS | screenshots 5/7, 11, 12a-12d; Era Azeroth's 4-bit alpha stated unsupported, not shown textured (10b) |
| 9 | navigate the camera | PASS (source), SKIP (live mouse input) | the unchanged orbit camera; three framings (9a-9c); posted mouse messages do not reach the player's raw input, and `SendInput` would reach the user's game (section 9) |
| 10 | change the terrain display mode | PASS | eight modes, no reload (screenshots 8a-8g) |
| 11 | select another tile | PASS | screenshot 11; rapid re-clicks superseded; arrow keys and Enter |
| 12 | leave Maps without breaking Models or Buildings | PASS | screenshots 13, 14; `expect:terrain:0` after every exit |
| 13 | switch clients without stale assets | PASS | `switch`, five cases (section 12.2) |
| 14 | close WMW safely during an active terrain read | PASS | `holdclose` (the window closed with a tile's read held) and `readshutdownserved` on all four clients (section 1.2) |

### 20.2 Everything graded

| Item | Grade | Note |
|---|---|---|
| Shutdown re-entrancy (18.1) removed, test fails at `81f79982` | PASS | section 1 |
| Ownership comment in `MapAssetWorker.h` | PASS | section 1.1 |
| Maps entry beside Models, Textures, Buildings | PASS | |
| Catalogue from `WorldMapCatalog`, names, MapIDs, states | PASS | |
| Name / directory / MapID search | PASS | |
| Unavailable maps listed with their reason | PASS | 10c |
| Tile grid: x/y orientation | PASS | 0 mismatches over 59,967 named root ADTs, four clients |
| Tile availability from the WDT and the storage root | PASS | |
| Tile states "not downloaded", "not in client files", "no root ADT" on real data | SKIP | no such tile exists on this computer (section 5.4) |
| Loading through protocol 13 and the 04B worker, no new parser | PASS | section 6 |
| Every loading state shown | PASS | section 7 (the "renderer too old" state: PROVEN_SOURCE, an old player was not run through the GUI) |
| Viewport and camera reused | PASS | section 9 |
| Interactive camera input driven headlessly | SKIP | section 9 |
| Display modes from the existing shaders, without reload | PASS | section 10 |
| Scene switching and disposal | PASS | sections 11, 14 |
| Client switching (all four clients, held read included) | PASS | section 12 |
| GUI responsiveness: no CASC read on the GUI thread | PASS | section 13 |
| GUI responsiveness: entering Maps | FAIL (documented) | 159-247 ms in one GUI round; a lazy list is the follow-up |
| IPC buffering: no new periodic source | PASS | section 18 |
| IPC buffering: the transport-wide cap (04b 18.2) | not in scope | inherited, recorded |
| Geometry and material outputs unchanged | PASS | section 16 |
| M2 and WMO regressions | PASS | section 16 |
| Legacy MPQ clients | UNSUPPORTED | the Stage 02 catalogue does not read Map.dbc, and the Maps tool is refused with that reason (PROVEN_SOURCE: no MPQ client on this computer) |
| In-game visual parity | not claimed | no in-game comparison was made |

## 21. Scope addition of 2026-10-15 (recorded before it was built)

After the first part of this stage was finished, the owner supplied a reference video of the intended product: a map
list, an overhead map to pick a position on, Load World, and a free-flying camera through an assembled world with
world models, doodads, liquids, lighting and effects. The video and the revised milestone plan it leads to are in
`roadmap.md`. The owner asked this branch to continue with map selection, coordinate selection, terrain loading and a
proper free camera, so that the interface and scene lifecycle can carry a fully assembled world later, with no shortcut
that would have to be thrown away. This section records that addition before any of it was written. The later
milestones (world models, doodads, streaming, liquids, lighting, effects, sky, far terrain) stay out of this stage.

### 21.1 What is added

1. **The overhead map.** In Maps, before a world is entered, the viewport's place shows the map large: the 64 x 64
   grid with the map's own minimap images (MAID word 7, `00-audit.md` 4.2 and 4.6), faint tile borders, the tile
   states of section 5 as overlays, zoom and pan. The small grid under Browse goes; Browse keeps the list, the map's
   facts and the controls. This is the texture viewer's mechanism: the host's content window in the player's place
   (`UnityRendererHost::showContent`), the player parked, not closed.
2. **A target position.** A click on the overhead map sets a target point, not a load: world X and Y from the point
   clicked inside its cell, in the game's frame (X north, Y west, `TerrainCoordinates`). X, Y and Z can be typed. An
   empty Z means "on the ground". The target is shown as tile and world coordinates. A target on a tile that is not
   available is refused with the tile's reason.
3. **Enter world.** A button (or Enter, or a double-click) enters the world at the target. Today that is the target's
   tile, loaded through the unchanged Stage 03/04/04B pipeline, and the camera placed at the target. The load carries
   the camera (protocol 15: `loadTerrainTile` gains an optional camera), so the tile is never framed and then moved.
   Back to map shows the overhead map again. The world stays loaded behind it until another one is entered, the
   terrain is cleared, or Maps is left.
4. **The free camera.** The world view's camera flies: look with a mouse drag, move forwards and backwards with the
   wheel, and fly, strafe and climb with W A S D, Q and E (Shift faster) where the keyboard reaches the player. The
   placement on the ground uses the tile's own mesh: the height of the fan triangle under the point, plus an eye height.
   The other camera is the tile overview (Stage 03's framing). A new message, `setWorldCamera`, moves the camera or
   switches between the two without a reload.
5. **The minimap images are read and decoded off the GUI thread.** The map asset worker reads them as the viewer's own
   reads, in small batches so a terrain read never waits behind more than one batch, and cancels them when the map
   changes. A small software BLP decoder (the BLP2 block formats the minimaps use) decodes each image on the worker
   and reduces it to a 64 x 64 thumbnail. It is checked against the app's GL decoder on real minimaps. A missing or
   unreadable minimap leaves the cell without an image, and the hover line says why.

### 21.2 Why this is not a shortcut

- **Positions, not tiles.** The camera and the target are world coordinates in the game's frame, converted by the
  player with the scene's floating origin (`WorldToUnity`). When more tiles and placed objects arrive, the same target
  enters the same scene, and the same camera flies through it.
- **One camera.** The free camera belongs to the world view, not to terrain. World models and doodads (milestones 06
  and 07) are drawn in that view and need no camera of their own.
- **Maps without terrain.** The overhead map shows a world-model-only map with no tiles, and Enter world says that
  entering it needs milestone 06 (world models in the world). The flow is the same; only the parts the position holds
  differ.
- **The host reads, the player parses.** The minimap FileDataIDs come from the player's WDT parse (protocol 15 adds
  them to the tile table), and the host never parses a WDT.

### 21.3 What it is checked by

- A software-decoder test: real minimaps of all four clients, decoded by the new decoder and by the app's GL decoder,
  compared pixel by pixel.
- The orientation of the minimap images against the tile: the tile seen from straight above, compared with its minimap
  (gate G4 of `00-audit.md`, never run until now).
- Player self-tests: the ground height under a point (against the mesh's own triangles), the target's world-to-Unity
  conversion, the free camera's movement from synthetic input, and the model view's camera restored on leaving.
- The Maps GUI suite on all four clients: click a target, type a target, Enter world, Back to map, the camera modes,
  scene and client switches, and leaving Maps.
- Screenshots from inside the world: the free camera on the ground, looking across the tile.
- Whether the keyboard reaches the embedded player is measured, not assumed. The mouse alone is enough to fly
  (wheel and drag).
