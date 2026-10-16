# World viewer 05: the Maps viewer

Branch `world/05-maps-viewer-ui`, from `develop` (verified on `619e9a03`, rebased onto `b01b6b43`, which adds only
PR #116's tabard fix in `WoWItem.cpp`). `develop` contains Stage 04B (`81f79982` and `01071f76`, merged as `4aba4c18`),
PR #113, PR #114 (the player's frame-rate cap) and PR #115.

**Push status: NOT PUSHED.**

**Summary.** WoW Model Viewer has a fourth viewer, **Maps**, next to Models, Textures and Buildings. Browse lists the
active client's maps from the Stage 02 catalogue, grouped by the catalogue's own states and searchable by name, directory
or MapID. In the viewport's place an **overhead map** shows the map's 64 x 64 tiles as its WDT and the client's storage
give them, each with the map's own **minimap picture** -- read and decoded on the Stage 04B map asset worker. A click on
the map, or typed coordinates, set a **target** in the game's world frame; **Enter world** loads the target's tile
through the unchanged Stage 03/04/04B pipeline and puts a **free camera** on the ground at the target, in the existing
Unity viewport. The camera flies with the mouse and, once a click in the view has given the player the keyboard, with
the keys (section 22: why they did not work in the owner's first two tests, and the fix), or shows the tile from above. The display modes are the player's existing views, switched without a reload. Back to map returns to the
overhead map with the world kept behind it; leaving Maps clears the world. The WDT is parsed by the player's verified
parser, so the host parses no WDT.

The stage was built in two parts. The first ("Add interactive single-tile Maps Viewer", committed) gave the viewer
with a tile grid under Browse and a click-to-load tile. The owner then supplied a reference video of the intended
product -- a map list, an overhead map to pick a position on, Load World, and free flight through an assembled world --
and asked this branch to continue with coordinate selection and a free camera. That scope addition was recorded before
it was built (section 21); the revised milestone plan towards the video, and what this stage delivers against what the
complete World Viewer still needs, are in `roadmap.md`.

Before any of this, the shutdown re-entrancy hazard of `04b-asset-serving.md` 18.1 was removed. Its regression test
fails on `81f79982` and passes on this branch (section 1).

Results are graded PASS / FAIL / SKIP / UNSUPPORTED in section 20. Evidence levels are as in `00-audit.md`:
`PROVEN_RUNTIME` (measured here, with the run named), `PROVEN_SOURCE` (read at the cited place), `INFERRED`, `UNKNOWN`.

**Builds used.** Clients: Retail 12.1.5.70391, Classic Era 1.15.9.70003, MoP Classic 5.5.4.70268 and Classic Beta
1.60.1.70425 (70374 during the first part; the install updated itself between the parts), all on this computer, with
every root ADT their WDTs name installed (section 5.4). Rigs live outside the repository, under `F:\wmv-build-fix\w05`. Every run used a private runtime on a desktop of its own, never the user's
screen.

| Binary | Identity |
|---|---|
| Host, this branch (final suite) | `wowmodelviewer.exe` SHA-256 `8004070BBC27C4DA...`, `wow.dll` `432B4CE9...`, `core.dll` `991957BE...` |
| Player, this branch (part 2's final suite) | `Assembly-CSharp.dll` MD5 `C64B99FA4C2E4538FC757F6AD8CCFCB6` (protocol 15), lifecycle self-test **870 passed, 0 failed** |
| Host and player with the keyboard fix (section 22) | `wowmodelviewer.exe` SHA-256 `7B8E368052C1560F...`, `Assembly-CSharp.dll` MD5 `E18427DFCF813A0A13292AC9A891132D`, lifecycle self-test **874 passed, 0 failed** |
| Reference host | `81f79982` plus the shutdown test's instrumentation (no fix), `wowmodelviewer.exe` SHA-256 `043C0AA4...` |
| Reference player | develop's player, `Assembly-CSharp.dll` MD5 `3C7E3E0F7809056B372731D8CA699DEA` (the user's deployed player before this stage) |

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

The end check "no map read is in flight at the end" accepts exactly the one read left for the shutdown.

| Build | Run | Result |
|---|---|---|
| `81f79982` + this test only (worktree `F:\wmv-05-base`, no fix) | `shut_81f79982_2` | **FAIL** (7 passed, 2 failed). Two polls got past their check: the direct one, and the queued wake's, which **completed the stopped read with three map callbacks inside `~ModelViewer`**. A wake posted by the worker was still waiting after `stop()` |
| `develop` + this test only | `shut_testonly_1` | **FAIL** (6 passed, 2 failed): the direct poll (`develop` differs from `81f79982` in none of the files involved but `modelviewer.cpp`'s Dracthyr code) |
| this branch | `shut_fix_2` | **PASS** (9 passed, 0 failed). 0 polls past their check, 0 messages, 0 map callbacks; 1 wake handled, which did nothing; references 1/1; the player exited after 734 ms |
| this branch, Classic Era / MoP Classic / Classic Beta | `S_*` (battery) | **PASS** (9 passed, 0 failed) on each of the four |
| this branch, through the Maps viewer (a tile's root read held, the window closed) | `holdclose` (final suite) | **PASS** (9 checks, then the shutdown check: 9 passed, 0 failed; the player exited after 719 ms) |

`PROVEN_RUNTIME`. Logs are in `F:\wmv-build-fix\w05\shut`, `F:\wmv-build-fix\w05\battery_p2\mine_shut` and
`F:\wmv-build-fix\w05\ui_p2`.

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
| Browse | `FileControl`: one search box (300 ms debounce, Enter forces), one tree, a status line; per-mode search memory | Maps lists the catalogue in the same tree with the same search; the Maps panel sits under the tree in Maps only |
| The viewport's place | `UnityRendererHost`: the player's window, a notice drawn over the parked player, or a **content window** of the app's own in its place (the texture viewer's) | the overhead map is that content window until a world is entered (`MapOverview`), then the player shows the world |
| Terrain in the player | Stage 03: layer 30, `TerrainViewMask`, the camera state kept and restored by `WmvTerrainViewMode` | unchanged; the view mode gains the free camera beside the overview framing |
| Camera | `WmvOrbitCamera` (left drag orbits, right drag pans, the wheel zooms), the map-object framing rule for the tile | kept for the tile overview; the world view's own camera is the new free camera (`WmvFlyCamera`), on the same Unity camera |
| Map catalogue | Stage 02 `WorldMapCatalog`: maps, WDT references, states | the source of the list; `noteInspection` (new) keeps the worker's WDT inspection |
| Map files | Stage 04B map asset worker: one storage-owning thread, lease, epoch and generation | the WDT read, and the minimap pictures read and decoded there (`Job::thumbnailSide`, `MapThumbnail`) |
| Terrain loads | Stage 03/04 `RequestTerrainTile`, protocol 13, 04B worker | unchanged, plus `wdtInspected` (no GUI-thread WDT read) and the camera the load carries (protocol 15) |
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

  The groups other than the first are in the secondary text colour, and the status line counts them. A row reads
  `<name>  [<MapID>]  <directory>`. The search matches every word in the name or the directory, and a number also
  matches the MapID: that map is pinned first, and Enter opens it.
- **The overhead map** (`MapOverview`, in the viewport's place while no world is entered, and after Back to map):
  - the map's name and how to use the map, the whole-grid box, and **Enter world**;
  - the 64 x 64 tile grid (`MapTileGrid`) with each tile's **minimap picture** under it, faint tile borders, the tile
    states as colours or hatching, the coordinates on its edges, the legend; fitted to the box of the tiles the WDT flags,
    or the whole grid; the wheel zooms about the tile under the mouse (up to 160 px a tile), a drag moves a zoomed grid;
  - a click sets the **target** at the point clicked (a ring round its tile, a marker at the point); a double-click
    also enters the world there; the arrow keys move the target a tile and Enter enters;
  - the tile under the mouse with its state and its picture's state, and the target's tile and world coordinates.
- **The Maps panel under Browse** (`MapTilePanel`): the map's name, MapID, directory and WDT reference and its state
  line; the target's X, Y and Z fields (typed coordinates; an empty Z is "on the ground"); **Enter world** and **Back to
  map**; the **Camera** choice (the free camera, or the tile overview) and what it shows; the **Display** choice; Clear
  terrain.
- **The world view** (after Enter world): the Unity viewport with the tile, seen through the free camera standing at the
  target.
- **State**: `MapsViewer` owns all of it, with no widget. The overhead map and the panel draw it, and the command bar's
  label and the window title are read from it.

## 3. Screenshots

All are pictures of the viewer's own window: `PrintWindow` on a desktop of its own, the default layout, taken by the
final suite (`-mapsuitest`, section 15). They are not composites: the overhead map and the player's picture are part of
the window's own. Each is 1906 x 1073: the maximised window on a 1920 x 1080 desktop. The views from inside the world
show the terrain of one tile, as Stages 03 and 04 draw it: no sky, water, objects or lighting, and nothing beyond the
tile's edge (the dark background) -- those are the later milestones of `roadmap.md`.

**1. Maps entry: the command bar's fourth viewer; Browse lists the catalogue; the overhead map in the viewport's place says no map is selected (Retail)**

![01-maps-entry](05-maps-viewer-ui/01-maps-entry.png)

**2. The catalogue with Map.db2's names, MapIDs and directories; Eastern Kingdoms [0] picked: the overhead map with its 1,176 minimap pictures, fitted to the WDT's tiles**

![02-overhead-map](05-maps-viewer-ui/02-overhead-map.png)

**3a. Search by name: "Kalimdor"**

![03a-search-name](05-maps-viewer-ui/03a-search-name.png)

**3b. Search by MapID: "530" pins Outland [530] first**

![03b-search-mapid](05-maps-viewer-ui/03b-search-mapid.png)

**4. The overhead map zoomed with the wheel about 32_48 (56 px a tile): the minimap pictures, the tile borders, the coordinates**

![04-overhead-zoomed](05-maps-viewer-ui/04-overhead-zoomed.png)

**5. A target clicked in tile 32_48 (the ring round the tile, the marker at the point); the panel shows it in tile and world coordinates, and Enter world is enabled**

![05-target](05-maps-viewer-ui/05-target.png)

**6. Entering the world (the tile's root ADT read held in the worker by the test, so the state can be photographed)**

![06-entering](05-maps-viewer-ui/06-entering.png)

**7a. Inside the world: the free camera on the ground at the target in Elwynn Forest (tile 32_48), facing north -- the tile's terrain and textures**

![07a-inside-north](05-maps-viewer-ui/07a-inside-north.png)

**7b. The same camera turned to yaw 200 (south-south-west), pitch 4**

![07b-inside-south](05-maps-viewer-ui/07b-inside-south.png)

**7c. Turned to yaw 300 (west-north-west), pitch -2: the road and the mountains**

![07c-inside-west](05-maps-viewer-ui/07c-inside-west.png)

**7d. Turned to yaw 110 (east), pitch 12: a hole cell in the mountain shows as a gap**

![07d-inside-east-up](05-maps-viewer-ui/07d-inside-east-up.png)

**8a. Display: Neutral, from inside (the camera stays; nothing is read again)**

![08a-inside-neutral](05-maps-viewer-ui/08a-inside-neutral.png)

**8b. Display: Chunk borders**

![08b-inside-chunk-borders](05-maps-viewer-ui/08b-inside-chunk-borders.png)

**8c. Display: Normals**

![08c-inside-normals](05-maps-viewer-ui/08c-inside-normals.png)

**8d. Display: Material states**

![08d-inside-material-states](05-maps-viewer-ui/08d-inside-material-states.png)

**8e. Display: Wireframe**

![08e-inside-wireframe](05-maps-viewer-ui/08e-inside-wireframe.png)

**8f. Display: Neutral with wireframe**

![08f-inside-neutral-wireframe](05-maps-viewer-ui/08f-inside-neutral-wireframe.png)

**8g. Display: Textured, diagnostic light (the viewer's light, not the game's)**

![08g-inside-textured-diagnostic-light](05-maps-viewer-ui/08g-inside-textured-diagnostic-light.png)

**9a. The other camera: Tile overview (Stage 03's framing of the whole tile)**

![09a-overview-camera](05-maps-viewer-ui/09a-overview-camera.png)

**9b. Back to map: the overhead map again, the world kept behind it (the dot marks the tile on screen)**

![09b-back-to-map](05-maps-viewer-ui/09b-back-to-map.png)

**9c. Back to map after all 1,176 pictures came while the world was shown: drawn into the overhead map in the first paint**

![09c-back-to-map-after-pictures](05-maps-viewer-ui/09c-back-to-map-after-pictures.png)

**10a. A target on a tile that is not part of the map (0_0, the whole grid shown): Enter world is refused with the reason**

![10a-target-not-in-map](05-maps-viewer-ui/10a-target-not-in-map.png)

**10b. An unsupported material, from inside: Classic Era Azeroth 32_48 (4-bit alpha) drawn in neutral shading, and said so**

![10b-unsupported-material-era-azeroth-inside](05-maps-viewer-ui/10b-unsupported-material-era-azeroth-inside.png)

**10c. An unavailable map: Moonglade [1534], its WDT not in client storage, with the catalogue's reason**

![10c-unavailable-map](05-maps-viewer-ui/10c-unavailable-map.png)

**10d. A world-model-only map, Stormwind Stockade [34]: read and explained (one world model, WMO 108631), not entered -- milestone 06**

![10d-world-model-only-map](05-maps-viewer-ui/10d-world-model-only-map.png)

**11a. Retail terrain at a typed target (X -9400, Y -1300: tile 34_49), looking west**

![11a-retail-typed-target-inside](05-maps-viewer-ui/11a-retail-typed-target-inside.png)

**11b. Retail terrain at 29_50, entered after two rapid entries (the first one superseded)**

![11b-retail-after-rapid-entries](05-maps-viewer-ui/11b-retail-after-rapid-entries.png)

**12a. Classic Era: Azeroth's overhead map (687 minimap pictures, 256 x 256 each)**

![12a-era-overhead](05-maps-viewer-ui/12a-era-overhead.png)

**12c. Classic Era: map 2853 (Deadwind Pass), its 12 tiles and pictures**

![12c-era-2853-overhead](05-maps-viewer-ui/12c-era-2853-overhead.png)

**12d. Classic Era: inside map 2853 at tile 34_50, textured**

![12d-era-2853-inside](05-maps-viewer-ui/12d-era-2853-inside.png)

**12e. MoP Classic: inside Azeroth at tile 32_48**

![12e-mop-azeroth-inside](05-maps-viewer-ui/12e-mop-azeroth-inside.png)

**12f. MoP Classic: Pandaria's overhead map (684 minimap pictures)**

![12f-mop-pandaria-overhead](05-maps-viewer-ui/12f-mop-pandaria-overhead.png)

**12g. MoP Classic: inside Pandaria at tile 38_28 (the Jade Forest)**

![12g-mop-pandaria-jade-forest-inside](05-maps-viewer-ui/12g-mop-pandaria-jade-forest-inside.png)

**12h. MoP Classic: inside Pandaria at tile 33_32 (the Valley of the Four Winds)**

![12j-mop-pandaria-four-winds-inside](05-maps-viewer-ui/12j-mop-pandaria-four-winds-inside.png)

**12i. Classic Beta: inside Azeroth at tile 32_48**

![12k-beta-azeroth-inside](05-maps-viewer-ui/12k-beta-azeroth-inside.png)

**12j. Classic Beta: Kalimdor's overhead map (988 minimap pictures)**

![12l-beta-kalimdor-overhead](05-maps-viewer-ui/12l-beta-kalimdor-overhead.png)

**13. Models after leaving Maps: the world cleared, a creature loaded and shown**

![13-models-after-maps](05-maps-viewer-ui/13-models-after-maps.png)

**14. Buildings after leaving Maps: the Stormwind jail (WMO 108631) shown, no terrain**

![14-buildings-after-maps](05-maps-viewer-ui/14-buildings-after-maps.png)

## 4. Map catalogue integration

- **Source.** The catalogue is the active client's Stage 02 `WorldMapCatalog` (`ModelViewer::mapCatalog()`), built
  while the client loads and replaced when another client loads. No other list is used: no outside map list and no
  made-up name. A name is Map.db2's `MapName_lang`, in the storage's locale. A map whose name is empty is listed by its
  directory.
- **Listing.** `FileControl::InitMaps` runs on the GUI thread over the catalogue's in-memory rows, with no file read. It
  lists 1,187 Retail maps in 15 to 16 ms (final suite). It runs when Maps is entered, on a search, and after a client
  load completes, in the load's deferred step: inside `LoadWoW` the client does not count as active yet.
- **States respected.** The groups come from the catalogue's own fields (`WdtReference::Kind`, `statedWdtForm()`) and the
  storage root's listing, never from a guess. A row is not a promise that terrain exists: whether the WDT can be read
  is found when the map is picked (section 5). What its tiles are comes from the WDT itself, through the player's parser.
  The census of every map of all four clients (section 5.4) shows each case with its reason.
- **Unavailable or unsupported maps** keep their row (secondary colour). Picking one shows the catalogue's own reason
  on the overhead map and in the panel. Examples: "This client gives the map no WDT file: ..." (Retail, 105 maps; Beta, 2), and
  "The map's WDT is not in client storage: the client's storage root does not list FileDataID 2449139" (Retail map 1534;
  110 Retail maps, 58 MoP, 2 Beta, 1 Era).
- **World-model-only maps** (an instance that is one global WMO, MPHD bit 0x1) get their grid read like any other: the
  player's table says there are no tiles and names the WMO (protocol 15: the MODF records and the first one's file), and
  the overhead map says that entering a world model is a later milestone (`roadmap.md`, 06) and that it can be viewed in
  Buildings. Enter world is refused with that reason (screenshot 10d).
- **Search.** Word search over name and directory (case-insensitive), and MapID: the exact map is pinned first, and
  Enter opens it. Each viewer mode keeps its own search (the per-mode arrays grew from 3 to 4).

## 5. The overhead map, the tiles and the target

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
- MAID words 0 (root ADT), 3 (tex0 ADT) and, since protocol 15, 7 (the minimap);
- for a world-model-only WDT, its MODF records and the first one's file.

The host checks the table decodes to its sizes and counts, and refuses it otherwise.

### 5.2 A tile's state

| State | Rule | Colour |
|---|---|---|
| not part of the map | MAIN flag clear | the empty cell |
| no root ADT named | MAIN flag set, MAID word 0 is 0 | grey |
| not in this client's files | MAID names a root ADT; `storageRootEntry(fdid).listed` is false | brown (hatched over a picture) |
| not downloaded | listed; `.local` is false | amber (hatched over a picture) |
| available | listed and local | its minimap picture, or the accent until the picture comes |

The storage root's answer is a lookup, not a read: 0.015 ms median per map, 0.31 ms at most (Retail map 2444, 1,634
roots). A tile is never called available because its map exists: the root ADT itself must be in the client's storage
and on this computer. `MAID word 0` is the root ADT the terrain load asks for (Stage 03), so the grid and the load agree
on the file.

### 5.3 Orientation

x is the column, growing east, and y the row, growing south. The root ADT of cell (x, y) is `<directory>_<x>_<y>.adt`,
as the audit established (`00-audit.md`: MAID index `i` names `x = i % 64`, `y = i / 64` for all 44,018 Retail tiles).
The grid draws column x and row y; a click returns the tile and the point inside it.

Three checks, on real files:

- **The names** (`orient`, `scan`): for every tile whose MAID names a root ADT, the file name the client (or the listfile)
  gives that FileDataID must be `<directory>_<x>_<y>.adt` for the cell it is drawn in. A transposed or shifted grid fails
  on every tile off the diagonal.
- **The pictures against the terrain (gate G4 of `00-audit.md`, never run before this stage).** The tile seen from
  straight above (the player's top-down capture, `-wmvTerrainTopDown`, textured) against its minimap as the worker
  reduces it: both to 64 x 64 grey, edges by a Sobel filter, the Pearson correlation of the edges for each of the eight
  rotations and mirrors of the minimap. "As is" must score best by at least 0.1 (`g4_score.py`).

  | Client, tile | As is | Best other orientation | Grade |
  |---|---|---|---|
  | Retail, Eastern Kingdoms 32_48 | 0.629 | mirrored left-right 0.102 | PASS |
  | Retail, Eastern Kingdoms 30_50 | 0.737 | mirrored left-right 0.250 | PASS |
  | Classic Era, Azeroth 32_48 | 0.628 | mirrored left-right 0.099 | PASS |
  | Classic Era, map 2853 34_50 | 0.754 | mirrored top-bottom 0.225 | PASS |
  | MoP Classic, Azeroth 32_48 | 0.593 | mirrored left-right 0.110 | PASS |
  | Classic Beta, Azeroth 32_48 | 0.690 | mirrored left-right 0.092 | PASS |

  Side by side, tile 30_50 of Eastern Kingdoms (left: the terrain from above, textured; right: its minimap):

  ![G4](05-maps-viewer-ui/g4-retail-30_50.png)
- **The geography**, by eye: the whole Eastern Kingdoms (screenshot 2) shows Dun Morogh's snow north of the Burning
  Steppes' red, Elwynn's green round 32_48 and Stranglethorn at the bottom, as the game's map has them.

### 5.4 The census: every map of all four clients (`scan`, PROVEN_RUNTIME)

| Client | Maps | Grid read | Of them with tiles | No WDT | WDT not in storage | Tiles | Named root ADTs: match / mismatch | Available | Not local / not in storage / no root |
|---|---|---|---|---|---|---|---|---|---|
| Retail 12.1.5.70391 | 1187 | 972 | 731 | 105 | 110 | 44,078 | 44,078 / 0 | 44,078 | 0 / 0 / 0 |
| Classic Era 1.15.9.70003 | 59 | 58 | 37 | 0 | 1 | 2,719 | 2,719 / 0 | 2,719 | 0 / 0 / 0 |
| MoP Classic 5.5.4.70268 | 285 | 227 | 194 | 0 | 58 | 9,848 | 9,848 / 0 | 9,848 | 0 / 0 / 0 |
| Classic Beta 1.60.1.70425 | 73 | 69 | 48 | 2 | 2 | 3,322 | 3,322 / 0 | 3,322 | 0 / 0 / 0 |

Every root ADT named by every WDT of the four clients is installed on this computer today, so the "not downloaded",
"not in this client's files" and "no root ADT" states have no real case here. They are SKIP with that reason. The rules
are unit-checked in the player (a synthetic WDT with a MAIN-only tile), and their colours and words are in the panel.

### 5.5 The minimap pictures

- **Which file.** MAID word 7 of each tile, from the player's table (protocol 15; `00-audit.md` 4.2 and 4.6: BLP2,
  512 x 512 without mip levels in Retail and Classic Beta, 256 x 256 in Classic Era and MoP Classic). The host never
  parses the WDT for it.
- **Asked of the storage root first**, as the root ADTs are: a minimap not listed, or not downloaded, is not read; the
  tile keeps no picture and the hover line says why (the audit counted 695 Retail minimaps not in the root).
- **Read and decoded off the GUI thread.** The map asset worker reads the file (the same `inspectInStorage`, at most
  1 MiB) and, asked by the job (`Job::thumbnailSide`), decodes it there with `MapThumbnail` -- block compression DXT1,
  DXT3 and DXT5, uncompressed BGRA and palettised, after `BlpInfo`'s header check -- and reduces the smallest level that
  is at least 32 x 32 to a 32 x 32 RGBA thumbnail (a box filter over whole pixels). The result carries the 4 KB thumbnail
  instead of the file. The app's own decoder needs the hidden canvas's GL context and so runs on the GUI thread only;
  it is the reference this decoder is checked against.
- **The decoder against the GL decoder** (`decodecheck`, every client, real minimaps spread over the map):

  | Client | Minimaps compared | Format | Worst pixel difference (of 255) | Worst mean | Pixels differing by more than 8 |
  |---|---|---|---|---|---|
  | Retail (Eastern Kingdoms) | 24 | DXT1 512x512 | 1 | 1.000 | 0 of 6,291,456 |
  | Classic Era (Azeroth; map 2853) | 24 | DXT1 256x256 | 1 | 1.000 | 0 of 1,572,864 |
  |  | 12 | DXT1 512x512 | 1 | 0.516 | 0 of 3,145,728 |
  | MoP Classic (Azeroth) | 24 | DXT1 256x256 | 1 | 1.000 | 0 of 1,572,864 |
  | Classic Beta (Azeroth) | 24 | DXT1 512x512 | 1 | 0.657 | 0 of 6,291,456 |
- **In batches.** At most 8 reads of pictures are with the worker at once, nearest the map's middle first; the next ones
  are given from a 10 ms timer, never from the handler of a picture (section 13 says why), and none while a tile loads,
  so a terrain read waits behind at most 8 small reads. Picking another map and a client change cancel them; a late
  picture of an earlier map comes back as a read of an earlier pick and is dropped.
- **Drawn as one mosaic.** The overhead map keeps one bitmap over the map's box, 32 pixels a tile, and draws each picture
  into it once as it comes. A paint is one stretched copy of it per row of pictures, so its cost does not grow with the
  pictures; while pictures come, the overhead map repaints at most every 100 ms.

### 5.6 The target

- **A click** on the overhead map sets the target at the point clicked: the tile, and the fractions inside it east and
  south, turned into the game's world frame by `MapsViewer::pointIn` -- `x = (32 - ty - fy) T`, `y = (32 - tx - fx) T`,
  `T = 1600/3` -- the inverse of `tileOf` (`tx = floor(32 - y / T)`, `ty = floor(32 - x / T)`), which is the layout of
  `TerrainCoordinates` (Stage 01). The marker is drawn at the same point by the same formula.
- **Typed**: X, Y and an optional Z (yards; ',' or '.' for the decimals). An empty Z is "on the ground". A point off the
  64 x 64 tiles is refused with the range; a point on a tile that cannot be loaded is kept and Enter world says why.
- The target is shown as `Tile 32, 48  X -8857.1  Y -209.5  Z on the ground` -- the form the reference video uses.

## 6. Enter world: the terrain loading workflow

Overhead map -> target -> **Enter world** -> `ModelViewer::RequestTerrainTile(map, x, y, why, wdtInspected = true,
&camera)` -> `loadTerrainTile` (protocol 13, and the camera of protocol 15) -> the player asks `getMapAsset` for the WDT,
the root ADT, the tex0 ADT and the textures -> the host decides each request (`decideMapAssetRequest`), the 04B worker
reads it, and the host checks it again and answers -> the player builds the mesh (Stage 03) and the material (Stage 04)
-> the tile on layer 30 -> **the free camera placed at the target** in the same frame (`PlaceWorldCamera`), answered
`worldCameraApplied` -> `terrainTileLoaded` "built" -> the world has the viewport. The camera's answer comes a frame or two
before the load's report; the host holds it until that report (section 17.2, L2), so the world takes the viewport with its
tile known, and an Enter world that ends without a tile leaves the overhead map, with the reason.

- No parser, reader, blending formula or coordinate transform was added or changed for the terrain. The geometry dumps
  and material composites are byte-identical to the 04B build's (section 16).
- The camera is carried by the load, so the tile is never shown framed and then moved: the player frames it as before
  (so a refit or the overview has a box), then puts the free camera at the target before the frame is drawn.
- **The target's tile already on screen**: only the camera moves (`setWorldCamera`), nothing is loaded.
- `wdtInspected`: `ResolveTerrainTile` used to read the WDT whole on the GUI thread before every load. Measured over
  all 972 Retail WDTs read by the worker, that read takes median 0.51, p95 0.79, at most 9.62 ms (972) over every Retail WDT the census read. A Maps load now uses the inspection the worker made for
  the grid, so the GUI thread reads nothing. The request itself is median 0.21, p95 0.27, at most 0.31 ms (44) on the GUI thread. The harness path is
  unchanged, and still inspects on the GUI thread as before.
- Client epoch, storage generation, the load serial and the read tracker are the existing ones.
- **An older player** (protocol 12 to 14) is sent no camera: the tile is shown framed from above, and the panel says the
  renderer has no free camera.

## 7. States and errors

| State | Where it shows | Words (examples from the runs) |
|---|---|---|
| no map selected | overhead map, panel | "No map selected -- Select a map in Browse, or search for one by name or MapID." |
| map selected (reading) | overhead map, panel | "Reading Eastern Kingdoms [0] -- Reading the map's tiles." |
| map cannot be shown | overhead map, panel | "... cannot be shown -- The map's WDT is not in client storage: the client's storage root does not list FileDataID 2449139" |
| world-model-only map | overhead map, panel | "This map has no terrain tiles: it is one world model (WMO FileDataID 108631). Entering a world model in the world view is a later stage ..." |
| grid known, no target | overhead map, panel | "Click the overhead map, or type X and Y, to set where to enter the world." |
| target on a tile that cannot be loaded | overhead map, panel; Enter world disabled with the reason | "Tile 0_0 is not part of this map: its WDT does not flag it (MAIN)." |
| a typed target off the map | panel | "X ..., Y ... is not on this map's 64 x 64 tiles (each coordinate within +-17066.67)." |
| entering the world | overhead map, panel | "Entering the world at tile 32_48: its terrain and textures are being read and built." |
| the world shown | panel, command bar label | "Tile 32_48 loaded in 0.32 s. Textured: every drawable chunk (256) drawn with its textures. 16 hole cell(s)." and "Free camera at X -8857.1 Y -209.5 Z 82.6 (on the ground, 80.6, plus 2.0) ..." |
| no ground under the target | panel | "... (no ground under the point (a hole, or outside the tile's triangles): placed above the tile's highest point)" |
| tile loaded, not textured | panel | "Not textured: this tile's material is not supported (no chunk can be drawn textured: alphaUnsupported 256); every chunk is drawn in neutral diagnostic shading." (Era Azeroth, 4-bit alpha) |
| tile loaded, partly textured | panel | "Partly textured: N of M chunks drawn with their textures; the others in neutral shading ..." |
| the load failed | overhead map, panel | "The world could not be entered: ..." / "Tile 32_48 could not be loaded: timed out after 30 s waiting for the root ADT answer ..." |
| a load superseded | log, panel | a load replaced by a newer Enter world is answered "superseded" by the player and never shown |
| the camera refused | panel | "The camera could not be placed: no world is on screen" (the tile is still shown, framed from above, when it is on screen) |
| world cleared | overhead map, panel | "The world was cleared. Enter world to load it again." (Clear terrain, leaving Maps, a viewer restart) |
| renderer too old | overhead map, panel | "The Unity renderer build in use speaks protocol 13; listing a map's tiles needs protocol 14 ..." |

A tile whose material is unavailable or unsupported is never called textured. Its words say what was drawn, and the
fallback is the existing Stage 03/04 neutral shading.

## 8. Client support matrix

| Client | Catalogue | WDT by | Grid | Minimap pictures | Enter world (tile, inside view) | Material | Evidence |
|---|---|---|---|---|---|---|---|
| Retail 12.1.5.70391 | 1,187 maps | FileDataID | PASS | PASS: Eastern Kingdoms 1,176 of 1,176 (DXT1 512 x 512) | PASS (Eastern Kingdoms 32_48, 34_49, 29_50, 30_50, 31_50; 31_47 to 33_49 in `repeat`) | textured | `retail`, `retailscan`, `backlog`, `repeat` |
| Classic Era 1.15.9.70003 | 59 maps | path | PASS | PASS: Azeroth 687 of 687 (DXT1 256 x 256); map 2853 12 of 12 (DXT1 512 x 512) | PASS (Azeroth 32_48; 2853 34_50) | Azeroth: **unsupported** (4-bit alpha, neutral fallback, stated); 2853: textured | `era` |
| MoP Classic 5.5.4.70268 | 285 maps | path | PASS | PASS: Azeroth 839 of 839 and Pandaria 684 of 684 (Azeroth's checked: DXT1 256 x 256) | PASS (Azeroth 32_48; Pandaria 30_29, 38_28, 33_32) | textured | `mop`, `mop2` |
| Classic Beta 1.60.1.70425 | 73 maps | FileDataID | PASS | PASS: Azeroth 736 of 736 and Kalimdor 988 of 988 (Azeroth's checked: DXT1 512 x 512) | PASS (Azeroth 32_48) | textured | `beta` |
| Legacy MPQ clients | catalogue Unsupported | -- | UNSUPPORTED | -- | -- | -- | the Maps tool is refused with the catalogue's reason |

## 9. Camera integration

- **One camera, two ways of holding it.** The world view's Unity camera is held either by the **free camera**
  (`WmvFlyCamera`, the default after Enter world) or by the orbit camera framing the whole tile (**Tile overview**, Stage
  03's framing: yaw 135, pitch 30, distance r / sin(half FOV) x 1.25). `WmvTerrainViewMode` hands the camera over: the
  orbit stops taking input while the free camera holds it, and on leaving the terrain view the model view's camera comes
  back exactly as it was (position, rotation, clip planes, zoom range). A new tile's framing, the overview and leaving
  all give the camera back; a window resize does not re-frame a flight.
- **The free camera**, in the game's world frame:
  - placed at the target: on the ground under it -- the height of the tile's own drawn triangle there
    (`TerrainHeight.At`, section 21.4) -- plus an eye height of 2 yards, or at the Z typed;
  - facing a yaw (degrees clockwise from north) and a pitch (up positive); Enter world faces north, 8 degrees down;
  - flying: a mouse drag looks around (0.2 degrees a pixel, pitch held within 89 degrees); the wheel flies forwards and
    backwards along the view (a quarter second at the current speed per notch); W A S D or the arrows move, E or Space
    and Q or C climb and sink, Shift is four times faster and Alt a quarter as fast; 30 yards a second to begin with;
  - clip planes 0.25 to 3,000 yards.
- **Input in the embedded player.** As the orbit camera's own measurement found (its file header), the legacy input
  backend gives the mouse position and buttons and the Input System gives the wheel; the free camera reads them the
  same way, and a drag counts only when its press started over the viewport. **The keys** come as keyboard messages to
  the window with the keyboard focus, which the legacy Input Manager reads once the player's window has been activated
  (the Input System gets no keyboard while embedded). In the owner's first two tests they did not work: the keyboard
  stayed in the host's windows, then the player was not activated. A press in the view now gives the player's window the
  keyboard and activates it, keys count only while it has it, and keys still held when it leaves are released
  (section 22).
- **What was verified**: the step from input to movement (self-test: north at yaw 0, east at yaw 90, strafe, climb,
  look and its pitch limit, the wheel, the speed factors); the placement (`look` and Enter world in the GUI suite, the
  camera's world position answered by the player and checked); the conversion of a world point to Unity space against
  the mesh's own vertices; the views from inside the world (section 3); the keys: with the owner's keyboard, and end to
  end through the real window, each key moving the camera the right way and distance and stopping on release
  (section 22.5).
- **No line of the M2 or WMO camera paths changed**: the M2 and WMO pictures are identical (section 16).

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

**No reload, and the camera stays.** `setTerrainView` (protocol 14) sets the view for later tiles, and the player
rebuilds the tile on screen from what it holds (`WmvTerrainSession.RebuildShown`): the parsed mesh, the material, and
the textures its load settled. The free camera keeps its place (screenshots 8a-8g are taken from inside the world).

- The new tile is built inactive beside the old one, sharing its uploaded textures. They change hands at the commit
  (`WmvRuntimeTerrainTile.Hand`) and are never uploaded twice.
- A texture the old view did not upload is uploaded from data kept for it. The data are kept only while the tile
  is shown in a view the host chose that uploaded nothing, and are released by a textured view or with the tile.
- A rebuild that throws disposes only the new tile.

No request goes to the host. Measured: median 29.4, p95 32.1, at most 32.1 ms (8) from the click to the player's answer, the rebuild itself median 7.2, p95 14.3, at most 14.3 ms (8)
(final suite). The player self-test proves it with synthetic tiles: the same Texture2D count and GPU bytes before and
after, no request sent, no framing, and a failed rebuild leaving the old tile whole.

## 11. Scene lifecycle and resource disposal

| Transition | What happens | Verified (final suite `retail`, `repeat`) |
|---|---|---|
| Models -> Maps | the overhead map takes the viewport's place; the model stays loaded in the player, off screen | `expect:world:overview` |
| Enter world | the target's tile loaded with the camera; the world takes the viewport (the overhead map put away) | `waitworld`, `expect:world:shown`, `expect:camera:placed` |
| Back to map | the overhead map takes the viewport's place again; the world stays loaded behind it, and Enter world on the same tile only moves the camera | `back`, `expect:world:overview` |
| Maps -> Models / Buildings / Textures | `MapsViewer::Left()` clears the terrain (`clearTerrain`): the player disposes the tile and leaves the terrain view; its model view comes back as it was left | `expect:terrain:0` after each; the capybara and the jail shown after leaving Maps |
| Buildings -> Maps | the world model stays alive in the player, hidden (Stage 03 design: a terrain load never replaces the model on screen) | runtimeState liveMapObjects 1, terrainView 1 |
| a new tile | the commit disposes the old tile in the same block | repeated entering: tallies flat (section 14) |
| Clear terrain | the tile disposed, the view left, the overhead map back | tallies 0 |
| a client switch | `ResetClientState` clears the terrain and cancels the viewer's reads (the WDT and the pictures); `MapsViewer` forgets the map, grid, pictures, target, world and camera; Browse re-lists from the new catalogue; a late read of the old storage is answered stale | `switch` (section 12.2) |
| a view switch | the old tile disposed after the new one is built; textures handed over; the camera stays | self-test, tallies |

- Old map assets cannot overwrite another scene. Terrain draws only on layer 30, and only while the terrain view is
  entered. Every terrain report is matched to the current load serial, and every camera answer to the current camera
  request: one for an earlier load or request is not shown, and the log says so.
- Client-specific textures are not reused after a client switch: a tile's textures are owned by the tile, and the
  tile is disposed with the switch. The minimap pictures belong to the map and go with it.

## 12. Client switching and isolation

### 12.1 What isolates a client

- **The viewer's own reads** (the WDT and the minimap pictures) carry a ticket, the client epoch and the storage
  generation. The worker refuses a job for another storage than its lease (04B). `MapsViewer::OnWdtRead` drops a result
  whose ticket is not one it waits for, or whose epoch or generation is not the current client's; picking another map or
  replacing the client cancels every picture read still out.
- **The tile table** carries the inspection serial, the map and the epoch. A table for an earlier pick, another map or
  another client is dropped.
- **Terrain loads and their cameras** are matched by the load serial and the camera request: `ResetClientState` cancels
  every read (the server's and the viewer's), drops the worker's lease, raises the epoch, clears the terrain and calls
  `MapsViewer::ClientReplaced()`, which forgets the map, its grid, pictures and target, and the world and its camera. A
  late `getMapAsset` result is answered "stale" by the completion check (04B D), and a report or camera answer for an old
  load or request is not shown.
- **The catalogue and the list.** The catalogue is a new object per client. Browse re-lists after the load returns, and
  the overhead map is empty until a map of the new client is picked.

### 12.2 The runs (final suite `switch`, through the interface: `LoadWoW(&config)`, what File > Load World of Warcraft's dialog runs)

| Case | Sequence | Result |
|---|---|---|
| no map selected | Retail -> Era | **PASS**: the map, grid and overhead map reset (epoch 2 -> 3); Era Azeroth's grid after (687 tiles) |
| a map selected | Era (Azeroth's grid known) -> MoP | **PASS**: the Era map and grid forgotten, terrain 0; MoP Azeroth's grid after (839 tiles) |
| entering the world | MoP, Enter world at 32_48 -> Beta at once | **PASS**: the MoP load's texture requests answered "stale" (epoch 4, now 5), nothing shown; Beta Azeroth 32_48 entered after (0.25 s) |
| the world shown | Beta, inside 32_48 -> Retail | **PASS**: Beta's tile disposed (terrain 0), the overhead map back; Retail's grid after (1,176 tiles) |
| a read held in the worker | Retail, Enter world at 32_48 with its root ADT read held, the file open -> Era -> released | **PASS**: on release the held root read (2.8 s, its file open) is answered "stale" (epoch 6, now 7), its tex0 read cancelled, nothing shown; Era Azeroth's grid after (687 tiles) |
| a map picked | Era, Azeroth picked -> Retail | **PASS**: no map, grid or tile after the load (epoch 7 -> 8); Retail 32_48 entered after (0.26 s) |

## 13. GUI responsiveness

All times are GUI-thread milliseconds on this computer (Ryzen 9 9950X3D), PROVEN_RUNTIME, final suite unless named.
"GUI round" is one poll plus one pass of the wx queue in the harness's pump (`g_pumpBusy`), so it also contains
handling the harness did not cause.

| Operation | Measured |
|---|---|
| Browse lists the maps (1,187 Retail) | 15 to 16 ms (`InitMaps`, inside one frozen pass) |
| Entering Maps (the whole switch) | 92 to 184 ms (the most is a run's first entry); of it 43-88 ms is the tree control's thaw after the list is made (profiled in an earlier build of this branch; entering Models costs 49 to 77 ms, Buildings 35 to 46 ms after its first entry; the first entries of Textures and Buildings cost 596 and 342 to 451 ms building their own catalogues) |
| A map picked (`SelectMap`) | 0.15 ms; the GUI update that follows (panel, overhead map, labels) at most 58 ms (median 6.9 ms over 454 updates) |
| The WDT read (worker, not GUI) | median 0.51, p95 0.79, at most 9.62 ms (972) over every Retail WDT the census read |
| The tile table (player round trip; the player handles messages once a frame, at the display's refresh rate since PR #114) | median 25.2, p95 33.5, at most 45.8 ms (972) (Retail census) |
| The grid built from the table (storage-root lookups) | median 0.014, p95 0.044, at most 0.284 ms (972) (Retail census) |
| From the pick to the grid shown | median 32.4, p95 41.8, at most 54.0 ms (972) (Retail census) |
| The minimap pictures, all of a map (read and decoded on the worker, 8 at a time) | Eastern Kingdoms 1,176 in 2.9 to 3.7 s; Era Azeroth 687 in 1.6 s; MoP Azeroth 839 in 1.9 s and Pandaria 684 in 1.5 s; Beta Azeroth 736 in 2.2 s and Kalimdor 988 in 3.0 s |
| The longest GUI round while the pictures come | 17 to 39 ms over the runs (Retail 39 and 36) |
| Back to map after all of a map's pictures came behind the world (they are drawn into the mosaic then; at most 16 ms a paint) | 34 ms, the longest GUI round of the Back to map step (`backlog`: Enter world right after the grid, all 1,176 Eastern Kingdoms pictures came behind the world, then Back to map) |
| Enter world: the tile requested (`RequestTerrainTile`, no WDT read) | median 0.21, p95 0.27, at most 0.31 ms (44) |
| Enter world: the tile loaded (click to the "built" report; the player's own total) | median 252.9, p95 392.4, at most 547.6 ms (44); the player's own total median 233.4, p95 383.7, at most 526.2 ms (44) |
| The tile put on screen (the player's own phases of a load: parse, mesh build, upload, first frame drawn) | parse 1.2 ms, mesh build 8.1 ms, upload 7.7 ms (at most 18.5), first frame 24.7 ms (at most 36.9): medians over 44 loads |
| The camera moved (`look`: setWorldCamera to worldCameraApplied) | median 16.0, p95 31.0, at most 32.0 ms (18) |
| A display mode applied (click to the player's answer; its rebuild) | median 29.4, p95 32.1, at most 32.1 ms (8) |
| The longest single GUI round during the whole Retail run | 452 ms (the mode switches; outside them 79 ms: clearing the search, where Browse re-lists 1,187 maps, 71 ms of it the list) |
| The catalogue scan of 1,187 maps (one pick after another, pictures cancelled by each next pick) | 67.8 s, the longest GUI round 69.1 ms |

Nothing of the Maps viewer reads CASC on the GUI thread: the WDT and the minimap pictures are read by the worker (the
pictures decoded there too), and the root ADTs and minimaps are only looked up in the storage root. The overhead map is
one window; a paint is one stretched copy of the picture mosaic per row of tiles. Re-listing after a search rebuilds the
tree rows only, as every Browse search does. A view switch and a camera move read nothing.

**A stall found and fixed while building the pictures.** The first build gave each picture's handler the next read.
A result posts the server a wake, and wx runs the events that become pending while it runs them, so the reads chained
wake after wake: one GUI round lasted 2,940 ms, the whole time the pictures took, with no paint and no input. The
batch is now given from a 10 ms timer (a window message, so the chain ends at each batch), the server takes at most 8 of
the viewer's results a poll (`MAP_VIEWER_RESULTS_PER_POLL`), and the overhead map repaints at most every 100 ms while
pictures come. Each paint takes 14-18 ms at a whole-map fit. Over the same Retail map the longest round fell to 21 ms in that profiling run (the final suite's rounds are in the table above; a Retail round while pictures come also holds the paint of the whole map, 14-18 ms).

**Not acceptable, and documented: entering Maps** costs 92-184 ms in one GUI round, about twice a switch to Models.
The cost is the native tree control re-laying out 1,187 freshly inserted rows when the frozen window is thawed. A lazy
list (rows of a group added on expand) would remove most of it. That is a follow-up (`roadmap.md`, milestone 08 names
it), not done here.

## 14. Memory

| Point (final suite `repeat`, Retail) | Host private MB | Player GPU dedicated MB | Terrain meshes / materials / textures (player) |
|---|---|---|---|
| Maps entered, Eastern Kingdoms picked, its pictures shown, no world | 2246.6 | 220.4 | 0 / 0 / 0 (0 KB) |
| after 12 world entries (six tiles) | 2249.2 | 283.1 | 256 / 256 / 6 (512 KB) |
| after 24 world entries | 2249.1 | 283.1 | 256 / 256 / 6 (512 KB) |
| after Clear terrain | 2249.1 | 277.6 | 0 / 0 / 0 (0 KB) |
| after leaving Maps for Models | 2245.2 | 277.6 | 0 / 0 / 0 (0 KB) |
| Buildings, WMO 108631 loaded | 2254.6 | 426.7 | 0 / 0 / 0 (0 KB) |
| back in Maps, 32_48 entered | 2258.5 | 451.6 | 256 / 256 / 10 (854 KB) |
| 2.5 s after the client change to Classic Era | 1226.0 | 446.6 | 0 / 0 / 0 (0 KB) |
| 7.5 s after the client change | 1226.1 | 446.6 | 0 / 0 / 0 (0 KB) |
| Classic Era Azeroth 32_48 entered | 1225.1 | 446.6 | 256 / 1 / 0 (0 KB) |

- 24 tile changes over six tiles (Back to map, a double-click on the next tile, the world entered there): host private 2246.6 MB before (the map's pictures held), 2249.2 MB after 12 and 2249.1 MB after 24; the player holds one tile (256 meshes, 256 materials, 6 textures) after 12 and after 24, and its GPU memory is 283.1 MB at both.
- The minimap pictures of Eastern Kingdoms: 1,176 thumbnails of 4 KB (4.6 MB) and the mosaic over the map's box (43 x 54
  tiles of 32 x 32 pixels, 24-bit: 6.8 MB), on the host; held until another map or client is picked.
- After Clear terrain, and after leaving Maps for Models and Buildings, the terrain tallies are 0.
- After a client change (Retail -> Era), the host's private memory drops with the smaller client. The player's GPU memory does not fall with it (446.6 MB 2.5 s and 7.5 s after the change, 446.6 MB with the Era tile on screen, which has no terrain textures because its material is unsupported): the world model of the earlier Buildings visit stays alive in the player across the switch (runtimeState liveMapObjects 1). That is
  inherited: no viewer mode tells the player to drop the model it holds when the client changes. See section 18.
- GPU memory is the player process's `\GPU Process Memory\Dedicated Usage`, sampled about once a second. Each row
  takes the last sample before its step ended. The texture bytes are the player's own estimate for its terrain textures.

## 15. Tests

### 15.1 Automated

| Test | Where | Result |
|---|---|---|
| Shutdown re-entrancy (18.1) | `-terraintile ... readshutdownserved` | FAIL at `81f79982` (7/2), PASS here (9/0) with the `wdt` role, and with the `rootAdt` role on all four clients |
| Player self-test. Protocol 14 checks: the tile table at `y*64+x`, never transposed; a no-MAID WDT; a bad MD5, no bytes or a malformed table refused; runtime views; a rebuild with texture hand-over, no request and no framing; a failed rebuild keeps the old tile; kept data released; a view switch in the report window reported "built". Protocol 15: the minimap ids at `y*64+x`; a world-model-only WDT's global WMO; the camera fields of `loadTerrainTile` and `setWorldCamera`; the `worldCameraApplied` line; the ground under 400 points of a ramp (worst 3.4e-6 yd), off the tile, in a hole; a world point to its vertex's Unity position; the free camera's step | `-wmvLifecycleTest` | **870 passed, 0 failed** |
| Maps GUI suite, Retail (entry, search by name and MapID, an unavailable map, a world-model-only map, the overhead map with its pictures, zoom, a target clicked and one typed, Enter world with a read held (the entering state), the free camera in four directions, 8 display modes from inside, the overview camera, Back to map, a target not on the map, rapid entries (supersede), a double-click, the keyboard, Clear terrain, Models/Buildings round trips) | `-mapsuitest`, `retail` | **PASS** (129 passed, 0 failed) |
| The software decoder against the GL decoder, all four clients | `decodecheck` | **PASS**: 108 minimaps of the four clients, every pixel within 1 of 255 of the GL decoder's, none differing by more than 8 |
| Gate G4: the minimap against the tile from above, all four clients | `g4_runs.ps1`, `g4_score.py` | **PASS**: 6 of 6 tiles, "as is" best by at least 0.1 (scored on the central square of the capture, taken before the player's window was square: Classic Era, Azeroth 32_48; Classic Era, map 2853 34_50; MoP Classic, Azeroth 32_48; Classic Beta, Azeroth 32_48) |
| Catalogue scan + orientation, all four clients | `scan`, `orient` | **PASS** (4 passed, 0 failed); every map of the four clients picked (Retail 1,187, Era 59, MoP 285, Beta 73); 0 orientation mismatches over 59,967 named root ADTs |
| Classic Era / MoP / Beta: pictures, Enter world, inside views | `era`, `mop`, `beta` | **PASS** (Era 28, MoP 25 and 16, Beta 20 checks, 0 failed) |
| Client switching (6 cases) | `switch` | **PASS** (52 passed, 0 failed) |
| Close during an active read, through the GUI | `holdclose` | **PASS** (9 checks, then the shutdown check: 9 passed, 0 failed; the player exited after 719 ms) |
| Back to map after the pictures came behind the world; the free camera with the target on another tile (stays on the tile on screen); Back to map while a tile loads (the world stays behind the overhead map) | `backlog` | **PASS** (26 passed, 0 failed) |
| Repeated entering and memory | `repeat` | **PASS** (93 passed, 0 failed) |
| Viewer-mode switching | `modes` | **PASS** (13 passed, 0 failed) |
| Stage 03/04/04B worker battery (W) | `-terraintile`, 6 runs | **PASS**, 6 of 6 |
| Stage 03/04 harness battery (H: late answers, switches, lifecycle, protocol-12 player) | 12 runs | 11 of 12 **PASS**. `H_B_missingtex0` fails for an environment reason: the tex0 it expects to be missing (5862455) is installed now, so "unavailable" cannot be reproduced (SKIP, as in 04B) |
| Geometry dumps vs the 04B host, same data | 6 tiles, 3 clients | **PASS**, 6 of 6 dumps byte-identical (Beta 32_48 against the reference host re-run on today's Beta data: the install updated from 70374 to 70425 between the parts) |
| Material composites vs the 04B host (V1/V2 golden tiles) | 11 runs | **PASS**, 98 of 98 composites byte-identical; 11 of 11 runs PASS (Beta 32_48 against the reference host re-run on today's Beta data: the install updated from 70374 to 70425 between the parts) |
| M2 (bear, clocks pinned) and WMO 108631 pictures | `-unityipctest` | **PASS**: bear identical, WMO 108631 identical (MD5) |
| Map catalogue sequences (`-mapcatalogtest`) | 4 sequences | **PASS**, 4 of 4 (44, 44, 27, 27 checks) |

### 15.2 Manual (GUI steps for the user; the harness cannot press the mouse or keys inside the Unity player)

1. Start the viewer normally and load a client. Click **Maps** in the command bar.
2. Search "Elwynn" or "0" in Browse. Pick Eastern Kingdoms (0). The overhead map fills with the minimap pictures in a
   few seconds.
3. Point at tiles (the line under the map names them), zoom with the wheel, and drag the zoomed map.
4. Click a point in Elwynn Forest (around 32, 48), then **Enter world**. The world is entered in about 0.3 s, the
   camera on the ground at the point clicked.
5. In the viewport: drag to look around, roll the wheel to fly forwards and back. Click in the viewport, then try W A S D,
   Q and E, with and without Shift: section 22.6 has the full keyboard check and the trace (`-inputtrace`) that shows
   where a key went if one does nothing.
6. Pick each **Display** mode. The tile changes at once, with no loading, and the camera stays.
7. Pick **Tile overview**, then **Free camera** again.
8. **Back to map**; type X -9400, Y -1300 and press Enter, then Enter world. Double-click another tile.
9. Go to Models, Buildings and back. Close the viewer while a tile is loading.
10. File > Load World of Warcraft: change the client while in Maps. The list and the overhead map are the new client's.

## 16. Preserved rendering correctness

See 15.1. The geometry and material outputs of this branch are byte-identical to those of the 04B host, on the same
client data. Classic Beta updated itself from 1.60.1.70374 to 70425 between the two parts: its tile 32_48 was therefore
run again on the reference host with today's files (the first reference differs from this branch's output and from the
reference host's own output today alike, so the difference is the client's data). The reference images of `04-terrain-materials.md` were made from older client files
(12.1.0.69933). They were not regenerated: the comparison that matters here is "unchanged by this branch". No shader,
blending formula, coordinate transform, parser or orbit-camera code changed. The player's only changes to existing
loads are additive: the `view` field of `terrainTileLoaded`, prepared texture data kept for a tile shown in a view the
host chose that uploaded nothing, and the camera a load may carry (none from the terrain harness).

## 17. Adversarial review

### 17.1 The first part

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

### 17.2 The scope addition

Two more reviewers read the second part's change (the overhead map, the pictures and their decoder, the target, Enter
world, the camera, protocol 15) with the same instructions: one for lifecycle, threading and state, one for coordinates,
the camera, the decoder and the protocol. Every finding was checked against the code and fixed before the final suite,
except where the table says otherwise.

| # | Finding | Reviewer's grade | Fix |
|---|---|---|---|
| P1 | Picking the free camera (or `look`) with a target typed for another tile sent the camera there: the player found no ground on the tile on screen and put it above that tile's highest point, hundreds of yards off the tile, and answered "applied" | major, confirmed | the free camera goes to the target only when it is on the tile on screen, else to that tile's middle (`flyCameraOnShownTile`); the player refuses a point that is not on its tile (`OnTile`) |
| P2 | A camera chosen while a load with its own camera was still in flight was overridden when that load's tile came: the panel said "Tile overview" while the player flew | minor, confirmed | the player drops a load's camera not yet placed when a newer camera request comes (the user's latest choice wins) |
| P3 | Back to map pressed while the world was being entered was undone when the camera's answer came | minor, confirmed | Back to map ends the entering: the load goes on, its camera's answer no longer takes the viewport |
| P4 | Uncompressed BGRA minimaps could never be decoded: `BlpInfo` checks no mip table for that encoding (no intact level) | minor, confirmed | level 0 of such a file is taken and checked against the file by the decoder itself (no minimap of the four clients uses the encoding) |
| P5 | The target marker was drawn up to one pixel right of and below the click | minor, confirmed | drawn with the pixel the click read |
| P6 | No ground in a band of about 2e-5 yd along a tile's south and east edges: the float edge vertices stop short of the true edge | minor, confirmed (simulated) | the triangle test allows 1e-5 of a cell triangle (about 4e-5 yd) |
| P7 | With a protocol-14 player, entering the tile already on screen left the camera choice on "Free camera" | minor, confirmed | the choice says "Tile overview" |
| P8 | The 1 MiB minimap limit was described as room for any 1024 x 1024 block-compressed image; a DXT3/5 one is larger | minor, plausible | the description corrected (DXT1); a larger file is not read, and the tile says why |
| P9 | Braces missing in `~ModelViewer`: one callback was cleared outside its `if (m_maps)` | minor, confirmed | braces |
| L1 | Back to map was undone when the world finished loading: the camera's answer (or, with a protocol-14 player, the "built" report) still on its way set the world shown again; the same after a camera choice followed by a quick Back to map | major, confirmed | as P3: Back to map ends the entering and forgets the camera request, so neither answer takes the viewport. Final suite `backlog`: Back to map pressed while the root ADT of 34_49 is held, then released -- the overhead map stays, with 34_49 loaded behind it |
| L2 | The free camera's answer arrives two player frames before "built": the overhead map was put away and the "Loading tile" notice covered the player until "built" (14 times over the first suite's runs); a load ending other than "built" after that answer would have left the world shown with no tile | minor, confirmed | the host holds the answer to the camera a load carried until that load's report: taken with "built", dropped otherwise (`MapsViewer::OnWorldCameraApplied`, `applyCamera`). Final suite: the notice 0 times; 43 answers held |
| L3 | The minimap reads stalled for good after a player restart during a load: a load holds the pictures back, and the restart ended it without a report | minor, confirmed | `OnPlayerReady` (and a refused Enter world) give the next batch |
| L4 | A deferred WDT read survived into later picks: a map without a WDT, picked while cancelled reads were still out, was read anyway when they came back | minor, confirmed (older than this part; the picture reads make it reachable) | `resetMap` clears the deferral |
| L5 | An exception in the player while it places the camera leaves the host entering for good (no answer, no timeout) | minor, plausible | not fixed: the player answers every camera request in the same call, and only an exception escapes it; Enter world again recovers. Recorded in section 18 |
| L6 | Squares of the previous map's pictures left on a map with no terrain tiles | minor, confirmed | the mosaic's record of drawn tiles is cleared for a map without tiles (screenshot 10d) |
| L7 | One long paint on Back to map: the pictures that came while the world was shown are drawn in one go | minor, plausible, not measured | measured: the Back to map round after Eastern Kingdoms' pictures came behind the world took 34 ms; the drawing is now bounded at 16 ms a paint, the rest at the next repaint (`MosaicPaintBudgetMs`), which no paint of the final suite reached |
| L8 | Misleading indentation in `~ModelViewer` | minor, harmless | as P9 |

The lifecycle reviewer also found the worker's comment ("at most MAP_ASSET_READS_IN_FLIGHT jobs") out of date: up to 2 of
the player's reads, the viewer's WDT and 8 pictures can be out. Corrected.

**Claims not disproved** (the lifecycle reviewer): a picture or table of another map, client or epoch is never kept
(tickets are unique per worker, every reset cancels the picture tickets, a WDT result must match its ticket); shutdown
(the shutdown pump takes only sent and paint messages, the callbacks are cleared after the server is detached, both timers
die with their objects, and a stopped worker refuses reads); the GUI-thread bounds (8 results a poll, at most 8 picture
jobs and one WDT job out, no read given from a picture's own handler); the world and camera state across client switches,
leaving Maps, picking a map and Clear terrain, except L1, L3, L4 and L5; and the keyboard focus when the overhead map and
the viewport change places.

**Claims not disproved** (the coordinates and camera reviewer): `tileOf` and `pointIn` are exact inverses for every
fraction a click gives and agree with `TerrainCoordinates`; the camera's Unity position is the target's world point by
the mesh's own origin and the root at the identity; the barycentric test and the chunk index are right, and holes, chunks
not built and points off the tile give no ground; the free camera's yaw, pitch and movement, the clip planes and orbit
restored on leaving, no refit or framing fighting a flight, and a superseded load's camera never placed on another tile;
the decoder's DXT1 three-colour mode, DXT3 nibble order, DXT5 tables and index unpacking, block clipping, palette order,
level choice and box filter, with no read past the buffer for any header `BlpInfo` accepts; and old players never sent a
camera, with missing fields handled and a wrong-sized minimap table refused.

## 18. Remaining limitations

- **No in-game comparison.** Nothing here claims the world looks as in the game. The textured mode is Stage 04's
  composite: no lighting, water, objects, shadows or sky, under the limits of `04-terrain-materials.md`. For example, MoP
  Pandaria 30_29 shows high-contrast white patches in the composite, and whether the game shows them so is UNKNOWN.
- **One tile, and nothing on it.** The world entered is the target's tile alone: beyond its edge there is nothing (the
  dark background), and none of its world models, doodads, liquids, sky, fog or lighting is drawn. Each has its
  milestone in `roadmap.md` (06 to 13).
- **World-model-only maps** (instances that are one world model) are listed, read and explained, but not entered:
  milestone 06.
- **A player that never answers a camera request** (an exception while it places the camera) leaves the host entering:
  there is no timeout. Enter world again, another map or another client recovers (section 17.2, L5).
- **Keyboard flight: fixed after two of the owner's tests failed** (section 22). Q, E, Space, Alt and a key released in
  another window are verified through the real window but not yet pressed by hand (22.6).
- **While the 3D view has the keyboard, the app's own keys** (F5-F10, Ctrl shortcuts, F11, Esc in fullscreen) go to the
  player: a click in any of the app's panels gives them back (22.7).
- **Entering Maps** takes 92-184 ms of GUI time (section 13). A lazy list is a follow-up.
- **No real "not downloaded", "not in client files" or "no root ADT" tile** exists on this computer (section 5.4).
- **The player keeps the last model or world model across a client switch** (hidden; inherited, not Maps-specific).
- **Outgoing IPC buffering** (04b 18.2) is still bounded only for the shipped player's requests. The new messages are
  sent once per user choice (`inspectMapTiles` once per pick and gated on the send high water; `setTerrainView` and
  `setWorldCamera` once per choice; the camera rides on the load). There is no periodic Maps message, and no progress or
  state stream. The transport-wide cap remains a separate fix (milestone 08's first item).
- **The worker's reads beside 04B's two**: the viewer's WDT read (at most 4 MiB) or up to 8 picture reads (at most 1 MiB
  each, handed back as 4 KB thumbnails). The worker-side bound becomes 2 x 8 MiB + 8 MiB.
- **The Maps tool's icon is sometimes not drawn.** Measured over the final suite's pictures, the Maps icon is drawn in
  some runs and missing in others, for a whole run at a time and in either state of the tool. The Models, Textures and
  Buildings icons are drawn in every picture. The icons are rasterised from SVG once per icon, colour and size
  (`UiIcons::bundle`), so a run that misses the Maps icon misses one cached bitmap. The cause is UNKNOWN, and it is not
  fixed here. It is the same symptom the owner reported for the Models icon before, and it can now be reproduced on a
  hidden desktop, so it is a follow-up of its own.
- **Map 1582's directory** shows a replacement character: the client's Map.db2 string holds a byte that is not valid
  UTF-8, and the Stage 02 catalogue reads it so (every catalogue run since Stage 02 shows the same).

## 19. Implications for the next milestones

`roadmap.md` is the plan; the points this stage leaves for it:

- **What carries over.** The overhead map and its pictures, the target in world coordinates, Enter world as "a position,
  not a tile", the free camera of the world view, the worker read path for the viewer's own files (with its per-poll
  budget and timer-driven batches), the client isolation (epoch, generation, serials), and the player's scene of tiles
  keyed by (map, x, y).
- **What must change.**
  - Today one tile is in the world and a commit disposes every other. World models (06) and doodads (07) join the same
    scene under the same floating origin; streaming (08) needs several tiles alive, a budget, and eviction.
  - The free camera's ground is the tile's mesh; with placed objects, standing on a floor of a world model needs that
    model's geometry (06).
  - Enter world on a world-model-only map needs the global WMO placed (06); the overhead map then needs that map's
    footprint, which this stage deliberately does not report (the WDT MODF position of the modern form is (0, 0, 0), and
    where that puts the model in the world is 06's to establish).
  - The 04B worker reads one file at a time (FIFO); the pictures already show the need to keep the viewer's reads from
    starving the player's (batches, none while a tile loads).
- **What to fix first.** The transport-wide send cap (04b 18.2), the Maps entry cost (lazy list), the player's retained
  model across a client switch, and the keyboard question of section 9 (a manual check).

## 20. Grades

### 20.1 Acceptance criteria (specification section 20)

Every step below was taken through the viewer's real window by the final suite (`-mapsuitest`), not through the
terrain harness: commands go through the command bar's tool, Browse's search box and tree, the overhead map's mouse and
keys, the panel's fields, buttons and choices, on the viewer's own event paths.

| # | A user can | Grade | Evidence |
|---|---|---|---|
| 1 | open WMW normally | PASS | every run starts the real executable with its normal start-up (maximised window, its own player) |
| 2 | load any supported WoW client | PASS | Retail, Classic Era, MoP Classic, Classic Beta (`retail`, `era`, `mop`, `beta`); legacy MPQ clients: UNSUPPORTED, refused with the catalogue's reason |
| 3 | open Maps | PASS | the command bar's Maps tool, screenshot 1 |
| 4 | browse or search its maps | PASS | screenshots 2, 3a, 3b; name, directory and MapID search |
| 5 | select a supported map | PASS | screenshot 2 and the overhead maps of every client; every map of all four clients picked by the census (section 5.4) |
| 6 | identify an available tile | PASS | the minimap pictures, the state colours and hatching, the hover line (screenshots 2, 4) |
| 7 | click that tile | PASS | a click sets the target (screenshot 5); Enter world, a double-click, Enter on the keyboard, typed coordinates |
| 8 | see its real terrain and supported textures | PASS | the views from inside the world on all four clients (screenshots 7a-7d, 11a, 11b, 12b-12j); Era Azeroth's 4-bit alpha stated unsupported, not shown textured (12b) |
| 9 | navigate the camera | PASS (placement, look, the overview, the wheel, the keys) | the free camera placed at the target and turned (`look`), the overview and back; the keys: failed in the owner's first two tests, fixed; PASS with the owner's keyboard (W, A, D, Shift) and end to end through the real window for every key (section 22.5) |
| 10 | change the terrain display mode | PASS | eight modes from inside the world, no reload, the camera kept (screenshots 8a-8g) |
| 11 | select another tile | PASS | Back to map and another target (11a, 11b); rapid entries superseded; the keyboard |
| 12 | leave Maps without breaking Models or Buildings | PASS | screenshots 13, 14; `expect:terrain:0` after every exit |
| 13 | switch clients without stale assets | PASS | `switch`, six cases (section 12.2) |
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
| World-model-only maps listed, read and explained | PASS | 10d; entering them is milestone 06 |
| Tile grid: x/y orientation | PASS | 0 mismatches over 59,967 named root ADTs, four clients |
| The overhead map's pictures: orientation against the terrain (gate G4) | PASS | section 5.3 |
| The pictures' decoder against the GL decoder | PASS | section 5.5 |
| Tile availability from the WDT and the storage root | PASS | |
| Tile states "not downloaded", "not in client files", "no root ADT" on real data | SKIP | no such tile exists on this computer (section 5.4) |
| The target: clicked, typed, shown in tile and world coordinates | PASS | section 5.6 |
| Enter world through protocol 13 and the 04B worker, no new parser | PASS | section 6 |
| The free camera: placed on the ground at the target, turned, the overview | PASS | sections 9, 21.4 |
| Keyboard flight (W A S D, Q / E, Space, Shift) | PASS | failed in the owner's first two tests (the keyboard stayed in the host, then the player was not activated); fixed (section 22); the owner's keyboard: W, A, D, Shift; through the real window: every key, its direction, distance and stop, no stale key; Q, E, Space and a key released elsewhere still to be pressed by hand (22.6) |
| Live mouse flight (drag, wheel) | PASS | the owner's tests: the wheel, and drag-look in the traced run (yaw 0 to 291 degrees while dragging, section 22.5) |
| Every loading state shown | PASS | section 7 (the "renderer too old" state: PROVEN_SOURCE, an old player was not run through the GUI) |
| Display modes from the existing shaders, without reload | PASS | section 10 |
| Scene switching and disposal | PASS | sections 11, 14 |
| Client switching (all four clients, held read included) | PASS | section 12 |
| GUI responsiveness: no CASC read or decode on the GUI thread | PASS | section 13 |
| GUI responsiveness: the pictures | PASS | section 13 (a stall found and fixed) |
| GUI responsiveness: entering Maps | FAIL (documented) | 92-184 ms in one GUI round; a lazy list is the follow-up |
| IPC buffering: no new periodic source | PASS | section 18 |
| IPC buffering: the transport-wide cap (04b 18.2) | not in scope | inherited, recorded |
| Geometry and material outputs unchanged | PASS | section 16 |
| M2 and WMO regressions | PASS | section 16 |
| Legacy MPQ clients | UNSUPPORTED | the Stage 02 catalogue does not read Map.dbc, and the Maps tool is refused with that reason (PROVEN_SOURCE: no MPQ client on this computer) |
| World models, doodads, several tiles, liquids, lighting, effects, sky, far terrain | not in this stage | `roadmap.md`, milestones 06-13 |
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

### 21.4 What was built, and how it checked

Everything of 21.1 was built as recorded, with two corrections found while building it:

- **The pictures' batches are given from a timer.** Given from each picture's handler, they chained the server's wakes
  and held the GUI thread for the whole 2.9 s the pictures took (section 13). The server now also takes at most 8 of
  the viewer's results a poll, and the overhead map repaints at most every 100 ms while pictures come.
- **No box for a world-model-only map.** The table names the global world model and how many placements the WDT has,
  but no position or box: the modern form's MODF position is (0, 0, 0), and where that puts the model in the world is
  milestone 06's to establish. The overhead map says the map is one world model, without drawing where.

| Check of 21.3 | Result |
|---|---|
| The software decoder against the GL decoder, real minimaps of all four clients | **PASS**: 108 minimaps of the four clients, every pixel within 1 of 255 of the GL decoder's, none differing by more than 8 (section 5.5) |
| Gate G4: the minimap against the tile from above | **PASS**: 6 of 6 tiles, "as is" best by at least 0.1 (scored on the central square of the capture, taken before the player's window was square: Classic Era, Azeroth 32_48; Classic Era, map 2853 34_50; MoP Classic, Azeroth 32_48; Classic Beta, Azeroth 32_48) (section 5.3) |
| Player self-tests: the ground under a point (400 points of a ramp, worst 3.4e-6 yd; a hole; off the tile), a world point to its vertex's Unity position, the free camera's step, the protocol-15 fields and lines | 870 passed, 0 failed |
| The Maps GUI suite on all four clients: click and typed targets, Enter world, Back to map, the camera, scenes, clients, leaving | section 15.1 |
| Views from inside the world | section 3, screenshots 7a-7d, 8a-8g, 11a, 11b, 12b-12j |
| Whether the keyboard reaches the embedded player | not measurable here (section 9); a manual check (15.2) |

The ground under the camera (`TerrainHeight.At`, `Wow/World/TerrainHeight.cs`): the chunk under the point from the
integer layout of `TerrainLayout.cs`, then the triangle of that chunk's drawn mesh whose projection holds the point (a
barycentric test on the Unity-space positions, in double), and the height interpolated on it. A point on a chunk border
is taken by the neighbour when its own chunk misses it by rounding; a hole cell, a chunk not built or a point off the
tile is NaN, and the camera is then put above the tile's highest point, with the reason said. Pure C#, no Unity: the
self-test checks it against ramps whose height is known everywhere.

## 22. Keyboard input in the embedded viewport (2026-10-16)

### 22.1 The reports

The owner tested the World Viewer in the real application three times:

1. **After `6b280039`:** loading a world works and the wheel flies the camera, but **W A S D, Q / E and Shift do
   nothing.** Section 9 had recorded that keyboard delivery to the embedded player could not be measured here; the free
   camera's self-tests passed because they drive its step with synthetic input.
2. **After `351328b1`** (the keyboard focus given to the player on a click): still nothing -- although the player's own
   log shows that the click gave its window the keyboard (`the player's window took the keyboard`).
3. **After `f6d118c0`** (the player's window also activated), with the trace on: **the camera moves.**

This section follows the keys from the window that has them to the camera, records where they stopped -- including a
conclusion of the first fix that the owner's traced run showed to be wrong (22.3.4) -- and the fix.

### 22.2 The trace

`-inputtrace` on the app's command line turns on a trace in both processes (off otherwise; nothing is installed):

- **The host** (`UnityRendererHost`, "[input]" lines in `log.txt`): two thread hooks on the GUI thread log every keyboard
  message the app's own thread takes (and for which window), every raw keyboard input (`WM_INPUT`) it is given, and the
  focus, activation and parent-notify messages its windows are sent; the viewport panel logs where it hands the
  keyboard. The player is started with `-wmvInputTrace`.
- **The player** (`WmvInputTrace.cs`, "WMV: [input]" lines in `unityRenderer.log`): which window has the keyboard
  (`GetFocus`, which answers for the host's windows too: the player's window shares their input queue), the keyboard,
  focus, activation and `WM_INPUT` messages its window's thread takes or is sent, the raw input devices the process
  registered, the keys the legacy Input Manager and the Input System report held, and the free camera: whether its window
  has the keyboard, the keys held and counted, and its position in the game's world frame (ten lines a second at most
  while it moves, one when it stops).

The harness has steps for the experiments (`focusinfo`, `keypost` / `keyplayer` -- a key's messages posted to the window
with the keyboard or to the player's window --, `clickplayer`, `focusplayer`, `focushost`).

### 22.3 Where the keys stopped (PROVEN_RUNTIME: runs `kbdiag1`, `kbdiag2`, `kb5_motion`, and the owner's traced run)

1. **After Enter world the keyboard is in the host.** The overhead map is put away before the player's frame is back on
   screen, so the viewport panel, given the keyboard, keeps it:
   `host: the viewport panel got the keyboard: kept (the player is off screen); the keyboard is now in "UnityRendererHost"`.
   A W pressed then is taken by the host's thread for that panel (`host took WM_KEYDOWN "W" for "UnityRendererHost"`).
2. **A press in the player's window does not move it.** After a left press reached the player's window
   (`taken WM_LBUTTONDOWN for player`) the keyboard was still in the host's panel: the player's window procedure does not
   take the focus on a press, and Windows moves the focus by itself only when it activates a top-level window.
3. **With the keyboard in its window, the player still ignored the keys until its window was activated.** The owner's
   second test (the log line above, and no movement) and the headless runs agree: W's `WM_KEYDOWN` reached the player's
   window with the keyboard there (`focus player`, `Application.isFocused True`) and `Input.GetKey(W)` stayed false; once
   the player's window had been sent `WM_ACTIVATE` (WA_ACTIVE), the same messages counted (`legacy W`). The documented
   `-parentHWND` embedding has the host activate the player's window: Windows never activates a child window, and the
   player keeps its own idea of being the active window.
4. **The keys come as keyboard messages; the Input System gets none of them.** In the owner's run the player's window
   took 205 `WM_KEYDOWN` and 25 `WM_KEYUP` messages for W, A, D and Shift, and **0 raw keyboard inputs** (`WM_INPUT`) --
   with the keyboard and activated; the host's thread took no keyboard message at all. The legacy Input Manager reported
   every key (`legacy W`, `legacy W A Shift` ...); `Keyboard.current` reported none. The player does register Raw Input
   for its window (`keyboard -> player; mouse -> player`, no INPUTSINK), but raw keyboard input never reaches it while it
   is embedded; raw mouse input does -- the wheel.
   **Correction:** the first version of this section (`351328b1`) concluded that both input backends read the keyboard
   from Raw Input, routed by the focus. That was inferred from posted key messages not counting -- which was point 3, the
   missing activation -- and the owner's traced run disproved it.
5. **A key released in another window stayed held.** With W held, a click in Browse took the keyboard and W's release
   went there; the Input Manager kept W held (`legacy W` until the next release of W), so the gate of the first fix kept
   it from moving the camera -- and swallowed the next real press of W with it (run `kb5_motion`: the first W after the
   click moved the camera 0 yd).
6. **The host does not swallow the keys.** Its accelerators are F5-F10, Ctrl+X/E/H/F/Z and Ctrl+Shift+R; its key hook
   acts on Escape (fullscreen) and Ctrl+S (Textures) only. No hook or hot key of the app takes W A S D, Q, E or Shift.

So the keys stopped at two links in turn -- the keyboard focus (in the host), then the player's activation -- and a
released key could stay held. Neither the free camera's key reading nor its movement was at fault, and no forwarding is
needed: with the focus and the activation, the keyboard's own messages reach the player.

### 22.4 The fix (native focus and activation; no forwarding)

- **A press in the 3D view gives the player the keyboard and activates it** (`WmvKeyboardFocus.TakeKeyboard`: `SetFocus`
  on the player's window from its own thread -- it shares the host's input queue -- and `WM_ACTIVATE` WA_ACTIVE; never
  WA_INACTIVE, so the mouse keeps working in the other viewers). The signal is a mouse press message taken from the queue
  for the player's own window -- a thread hook on that window's thread notes it -- not a press the input backends report
  while the pointer is over the viewport's rectangle, where a host dialog or menu may lie. Only while the free camera
  flies: in Models and Buildings, and in the tile overview, a click in the viewport moves no keyboard, as before.
- **Keys count only while the player's window has the keyboard** (`WmvKeyboardFocus.HasKeyboard`, each frame). Typing in
  Browse, the map search or the target fields (the host's windows have the keyboard then) cannot move the camera.
- **No key can stay held.** When the keyboard leaves the player's window, or the flight ends, every movement key the
  Input Manager still holds is released (`WmvFlyCamera.ReleaseHeldKeys`: the key's release sent to the player's window,
  as the keyboard sends it). As a last guard a key already down when the keyboard comes counts only after it has been
  seen released (`GateKeys`); every placement drops all keys.
- **Leaving the world view gives the keyboard back.** Back to map and Textures cover the player, which gives it up as
  before; leaving Maps for Models or Buildings moves it to Browse's search box (`UnityRendererHost::takeKeyboardFromPlayer`),
  unless Windows considers the player hung (the move waits on its thread).
- **The press hook is removed** when the free camera stops and as the player quits. `351328b1` left it in place for the
  life of the process, and every close of the viewer then hung until the host terminated the player after 3 s (all
  twelve runs of that suite; the shutdown check counts a terminated player as gone, so it passed). Since `f6d118c0` the
  player exits in 735-813 ms again.
- The free camera's movement is unchanged: `Step` moves by speed x frame time (frame-rate independent), Shift x4,
  Alt x1/4, E/Space up, Q/C down, opposite keys cancelling.

### 22.5 What was verified

**With the owner's keyboard** (the third test, trace on; PROVEN_RUNTIME from the logs in the owner's `userSettings`):
the click gave the player the keyboard and activated it; every key went to the player's window as a keyboard message
(205 key-downs, 25 key-ups, none to the host, no raw input); the Input Manager reported W, A, D and Shift, alone and
together; the camera moved about 29.7 yd a second with W (3325.50 to 3328.47 in 0.1 s), four times that with Shift held
(`speed x4`), and stood still the moment W was released.

**Through the real window, end to end** (the suite run `keys`, `-inputtrace`): the owner's run shows that real keys reach
the player as the very messages the harness posts -- key messages to the window with the keyboard -- so posted keys now
take the keyboard's own path. The run posts each key's messages and `kb_motion05.py` checks, from the player's camera
lines, how far and which way each segment moved the camera (world frame; the camera faces north, pitch -8):

| Segment | Moved (x north, y west, z up), yd | Expected | Result |
|---|---|---|---|
| W while the host has the keyboard | +0.0, +0.0, +0.0 | no movement | PASS |
| W ~1 s | +31.7, +0.0, -4.4 | north ~30 yd (pitch -8: a little down), then stops | PASS |
| S ~0.55 s | -17.3, +0.0, +2.4 | south ~16 yd | PASS |
| A ~0.55 s | +0.0, +17.5, +0.0 | west (left) ~16 yd | PASS |
| D ~0.55 s | +0.0, -17.5, +0.0 | east (right) ~16 yd | PASS |
| E ~0.55 s | +0.0, +0.0, +16.5 | up ~16 yd | PASS |
| Q ~0.55 s | +0.0, +0.0, -17.5 | down ~16 yd | PASS |
| Space ~0.55 s | +0.0, +0.0, +17.5 | up ~16 yd | PASS |
| Shift+W ~0.55 s | +69.3, +0.0, -9.7 | north ~65 yd (four times as fast) | PASS |
| W+D, D released first | +35.6, -17.0, -5.0 | north ~36 yd and east ~16 yd | PASS |
| W held, then a click in Browse (W released there) | +11.4, +0.0, -1.6 | north ~10 yd until Browse took the keyboard, then stops | PASS |
| a click back in the view, nothing pressed | +0.0, +0.0, +0.0 | no movement: no stale W | PASS |
| W, the first press after that | +17.3, +0.0, -2.4 | north ~16 yd (not swallowed) | PASS |
| W typed into the search box | +0.0, +0.0, +0.0 | no movement | PASS |

The posted Shift uses the left Shift's own key code: a posted generic Shift leaves the system's keyboard state as it is,
and the player's input reads that state to tell the side (a real Shift updates it -- the owner's run counted it).

| Check | Run | Result |
|---|---|---|
| The movement above | `keys` + `kb_motion05.py` | **PASS**, 14 of 14 segments |
| The keyboard's place: the host's after Enter world; the player's after a press in the view; Browse's search box and the target field take it back; Back to map, Models, Buildings and Textures leave it with the host; in Models a press in the viewport moves no keyboard | `keys` (suite) | **PASS** (77 passed, 0 failed) |
| The key gate and the movement keys: none without the keyboard, a key held when it came only after release, simultaneous keys, opposite keys cancelling, one second of W the same distance at 30 and 240 frames a second | player self-test | **874 passed, 0 failed** |
| The whole GUI suite, Classic runs, switching, closing, memory, modes | `ui_kb6` | **PASS** (retail 129, Era 28, MoP 25 and 16, Beta 20, backlog 26, keys 77, scan 4, switch 52, holdclose 9 with the shutdown check 9/0, modes 13, repeat 93; 0 failed; every player exited in 734-781 ms) |
| Regression battery (geometry, composites, M2/WMO pictures, W, H, shutdown, map catalogue) | `battery_kb6` | **PASS**: geometry 6 of 6 and composites 98 of 98 byte-identical to the reference, M2 and WMO pictures identical, W 6 of 6, H 11 of 12 (H_B_missingtex0, the environment reason of 15.1), the shutdown check 9/0 on the four clients, map catalogue 4 of 4 |

Binaries: host `wowmodelviewer.exe` SHA-256 `7B8E368052C1560F...` (the run's), player `Assembly-CSharp.dll` MD5 `E18427DFCF813A0A13292AC9A891132D`.

### 22.6 Still open

- **Real keys not yet pressed by hand:** Q, E, Space, Alt, and a key released in Browse (the stale key of 22.3.5). They
  take the same message path as the keys the owner pressed, and the run above moves the camera with each of them; a short
  check by hand would close it.
- **Switching away from the app and back:** the frame restores the keyboard to the last of its windows that had it; when
  that was the 3D view, wx records the player's frame as that window and its handler gives the player the keyboard again.
  INFERRED from wx's focus handling, not measured; a click in the view always does it.

### 22.7 Consequences

- While the 3D view has the keyboard, the app's own keys (F5-F10, Ctrl shortcuts, F11, Esc leaving fullscreen) go to the
  player and do nothing: a click anywhere in the app's panels gives them back (fullscreen keeps the panels).
- Observed with the trace, not changed here (inherited): when the player's window is destroyed at shutdown, the keyboard
  passes between the player's frame and the dying window a few times before the frame keeps it.
