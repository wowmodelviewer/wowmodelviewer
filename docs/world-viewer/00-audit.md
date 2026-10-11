# World viewer 00: source, client data and feasibility audit

Branch `research/world-map-format-audit`, from `develop` at `ef646931` (2026-10-11). **Audit only**: no map UI, no
terrain renderer, no change to the viewer, its build or its installer. Everything below was measured on the clients
installed on this computer with the read-only tools in `Tools/world-audit/` (section 10). Nothing was taken from an
outside format description without being tested against the bytes. Where a test could not settle a question, it is
listed as UNKNOWN together with the evidence that would settle it. An independent adversarial review challenged the
coordinate transform, the MAID structure, holes, alpha maps and placement mappings; what it refuted has been fixed and
re-measured (section 16).

Clients audited (from `E:\World of Warcraft\.build.info`):

| Product code | Client | Build |
|---|---|---|
| `wow` | Retail | 12.1.0.69933 |
| `wowt` | Retail PTR | 12.1.0.69587 (Map.db2 byte-identical to Retail; not audited further) |
| `wow_classic_beta` | Classic Beta | 1.60.1.70338 |
| `wow_classic_era` | Classic Era | 1.15.9.70003 |
| `wow_classic` | Mists of Pandaria Classic | 5.5.4.70268 |

**Audit data** (extracted game files, never committed): `F:\wowmodelviewer\build\world-audit-data`, ignored through
`.gitignore`'s `build` entry. Every file a reported number depends on is pinned by SHA-256 in
`Tools/world-audit/tests/corpus_hashes.json`: 243 files by name, and 19 file sets (9,441 files) by digest. The tests
fail if a client update changes any of them (section 10).

**Push status at delivery: NOT PUSHED.** The audit was delivered as a local commit and pushed for review on request
(2026-10-11).

## 0. Findings in brief

1. **Nothing on HEAD reads a WDT, ADT or WDL.** The only map reader the project ever had (`maptile.cpp`, removed in
   `cc1e14c5`) had its constructor commented out and never ran, so it is not a baseline (2.4). A world viewer starts
   from new parsers. This audit has already proven the structural rules they need (WDT, split ADT, coordinates and
   placements), in Python, against the bytes of four clients (sections 4-6). What remains UNKNOWN is listed in
   section 9.
2. **FileDataIDs are the only address that works everywhere.**
   - Retail and Classic Beta storages list no file names: tested on the Azeroth WDT, WDL, ADT root, tex0 and obj0, none
     opens by path.
   - Classic Era and MoP Classic open the same files by path. The WDTs (58/58, 227/227) and the 32_50 root, tex0 and
     WDL return the same bytes as by FileDataID; so does the Era obj0, while MoP's obj0 was not compared.
   - Retail and Classic Beta Map.db2 carry the WDT FileDataID in field 21, proven without the listfile. The Era and
     MoP Map.db2 have no such column, so there the WDT is found by path from the map directory.
   - Every Classic-family WDT and 763 of 969 Retail WDTs carry MAID: eight FileDataIDs per tile, in the same order in
     every client (4.1, 4.2).
3. **The coordinate chain is proven in the game's own frame**, from tile and chunk through vertex and placement to the
   world box (section 6).
   - Every MCNK position fits the tile/chunk formula, in all four clients.
   - Heights are exact at seams: 0 of 213,597 shared points differ by more than 1 mm in Retail and in Era, and 0 of
     33,981 in MoP and in Beta. The stored float32 chunk positions are not bit-consistent, though, so vertex x/y must
     be computed from integer indices (6.2).
   - Doodad positions land on the exact terrain surface: median |Δz| 0.021 yd in Retail (0.0003-0.001 yd in the
     Classic clients), with 17,947 of 31,500 Retail doodads within 0.1 yd.
   - The placement rotation `Rz(a1+180°)·Ry(a0)·Rx(a2)` reproduces the stored world boxes:
     - WMOs: 201 of 202 unique WMO placements within 0.1 yd, all 11 tilted ones included.
     - Doodads: 25,621 of 25,648 within 0.5 yd, 8,300 of the 8,305 tilted ones included.
   - The step into Unity is INFERRED: it composes this proven frame with the player's existing converter (6.6), and
     gate G4 checks it visually.
4. **Load is the hard part, not the formats** (7). A dense 3x3 area of Stormwind needs:
   - 5,322 file reads, 446 MB in total;
   - 13,512 M2 instances of 1,179 models, plus 53 WMO instances with 442 LOD0 groups;
   - 2,443 textures: 323 MB stored, but 1.83 GB the way the player uploads textures today (RGBA32, ×5.68).

   CascLib alone reads the set in 2.1-3.5 s on one thread with a warm cache. The host serves every request
   synchronously on its GUI thread. The player holds one model and one WMO, reframes on every load, has no
   cancellation and no instancing (2.3). Those limits, not the parsers, gate a usable world view.
5. **Custom terrain meshes, not Unity Terrain** (8), on measured needs (Retail figures):
   - 20% of inner vertices sit more than half a yard off a regular grid's interpolation (Era 14%);
   - a 257² heightmap still misses the game's surface by more than 0.25 yd in 12% of quarter-cells (Era 9%);
   - per-chunk layer sets (1,529 distinct in Retail's 56 tiles), vertex colours and 64-bit holes have exact rules a
     custom mesh reproduces.
6. **Files can be missing per part, so a viewer must report them per part** (3.3, T11). MAID names files the client
   does not have:
   - minimaps not listed by the root: 695 Retail, 64 Era, 632 MoP;
   - 11 Classic Beta files listed but not on this computer, among them one terrain tile's root ADT (Kalimdor 44_24).

   `NOT_IN_ROOT` and `NOT_LOCAL` are measured as distinct states.
7. **Next branch** (15): `world/01-parsers`. It ports the proven WDT/ADT/DB2 rules to pure C# in the player project,
   with framework-free tests and this audit's pinned corpus. No protocol change, no UI, no rendering.

## 1. Evidence levels

| Level | Meaning in this report |
|---|---|
| `PROVEN_BYTES` | Measured on installed client files by the audit tools; the count is in this report, reproducible (10), and asserted by `tests/test_corpus_rules.py` where marked. |
| `PROVEN_SOURCE` | Read in this repository's code at the cited path:line (or commit), and checked by a second reader (2.0). |
| `PROVEN_RUNTIME` | Observed by running something: the CASC probe opening files and timing reads, the viewer's own logs. |
| `INFERRED` | Follows from proven facts but was not measured directly. Each has a gate test (13). |
| `UNKNOWN` | Not settled; the evidence needed is named (9). |

Names from the community listfile are lookups, never proof: the listfile itself partly derives map-file names from
MAID positions and Map.db2 rows. Every structural rule below is proven from the bytes alone, and where a listfile name is
the only evidence, it is labelled "listfile name only".

---

## 2. What HEAD has today (task 1)

### 2.0 How this was established

Five readers each inventoried one area (Unity M2 path; WMO; CASC and database; map history and viewer modes; IPC and
lifecycle) with path:line or commit citations. Five more agents then checked every citation independently. Of 226
citations, 178 held as written. The rest were corrected (wrong line ranges, overstated claims), and only the corrected
form is used below. All citations are on HEAD `ef646931` unless a commit is named.

### 2.1 Facts that frame everything else

| # | Fact | Evidence | Where |
|---|---|---|---|
| H1 | Commit `8f34b64c` ("Remove WMO and ADT loading/render") is **not** in HEAD's history; it lives only on `origin/develop-archive-pre-midnight-2026-07-05`. HEAD's history starts at `74a3f5b1` (import of the 0.10 classic source). | PROVEN_SOURCE | `git merge-base --is-ancestor 8f34b64c HEAD` → false |
| H2 | On HEAD the map code was deleted by `cc1e14c5` ("Remove the dead map tile (ADT) code": `maptile.cpp/h`, `liquid.cpp/h`, `shaders.cpp/h`, `ModelCanvas::LoadADT/RenderADT/adt`, `ModelViewer::isADT`). `dfcfaad3` removed the Browse ADT category and with it the only `LoadADT` caller. | PROVEN_SOURCE | `git show --stat cc1e14c5`, `dfcfaad3` |
| H3 | The removed reader never ran. `MapTile(wxString)` at `cc1e14c5^:Source/wowmodelviewer/maptile.cpp:310` has its body inside `/* */` (312-756). It was path-based (MCIN), with no split files, FileDataIDs, MAID/MPHD or high-resolution holes, and its MDDF/MODF handling was commented-out TODOs (482-534). | PROVEN_SOURCE | `cc1e14c5^:maptile.cpp` |
| H4 | No file in `Source/`, `Tools/UnityRendererProject/Assets/Scripts/` or the shipped build project `F:/WMWUnityRenderer/My project` reads `.wdt`, `.adt`, `.wdl`, MAID, MPHD or MCNK. | PROVEN_SOURCE | grep over all three trees |
| H5 | The only WoW→Unity transform is for model space: `unity = (-wow.y, wow.z, wow.x)`, determinant −1, winding flipped, scale 1.0. Model space is right-handed, Z up, +X forward, +Y left. No world, tile or placement transform exists. | PROVEN_SOURCE | `Tools/UnityRendererProject/Assets/Scripts/Wow/WowCoordinateConverter.cs:7-24, 34-110` |
| H6 | IPC is protocol 11: TCP on 127.0.0.1, newline JSON with raw binary payloads (since 10), and one `loadWoWModel` message with `kind` `m2` or `wmo`. There is no map, tile, placement, camera, cancel or clear-scene message. | PROVEN_SOURCE | `Source/wowmodelviewer/UnityIpcServer.h:270`, `UnityIpcServer.cpp:1054-1132`, `WmvIpcClient.cs:1018-1127` |
| H7 | The player fetches every byte from the host through `getAsset {path}` / `getAssetByFileDataID {id}`. The host serves any file type raw (CASC stream mode, no chunk re-pointing), up to 64 MB per file. `readByFileDataID` refuses MPQ and name-lookup clients. **WDT/ADT bytes need no new IPC message.** | PROVEN_SOURCE | `UnityAssetAccess.cpp:71, 83-232, 486-523` |
| H8 | The repo's Unity scripts and the shipped build project are byte-identical for every file on the M2, WMO and IPC paths; only `WmvScreenshot.cs` differs. The build project is Unity 6000.5.9f1 with URP 17.5.0 and the terrain module 1.0.0. The repo holds `Assets/` (plus `Tests/` and `TestStub/`) but no `ProjectSettings/` or `Packages/`. | PROVEN_SOURCE | sha1 comparison by the inventory; `My project/ProjectSettings/ProjectVersion.txt`, `Packages/manifest.json:9, 31` |

### 2.2 Live features a world viewer can build on

| Area | What exists | Liveness | Evidence |
|---|---|---|---|
| M2 | The host opens the model (`WoWModel`, LOD0 skin only) and sends `loadWoWModel`. The player parses `.m2/.skin/.skel/.anim/.blp` itself (`M2Parser`, `M2SkinParser`, `BlpDecoder`, no UnityEngine code) and builds one mesh, materials, animation and particles. Parsing and BLP decoding run on the Unity main thread. | live-unity-path | `WoWModel.cpp:444-523, 584-600`; `M2Parser.cs:206, 1239-1252`; `WmvIpcClient.cs:686, 700-745`; `WmvMain.cs:1136` |
| M2 drawing | Skinned when the rig can be reproduced, else a static `MeshRenderer` (`PlanSkinning`); one GameObject per bone; `updateWhenOffscreen=true` when animated. | live-unity-path | `WmvModelBuilder.cs:974, 1585-1591, 1748, 1770-1790` |
| WMO | The player parses root and groups (`WmoParser`, `WmoModel`, `WmoMaterialSemantics`: pure C#, bounds-checked, tested) on the ThreadPool, LOD0 groups only. There is no placement transform (identity in model space). Doodads (`MODS/MODI/MODD`) are parsed but never built. No portal culling; `MLIQ` is kept raw. | live-unity-path | `WmoParser.cs:578-619`; `WmvWmoBuilder.cs:409-429, 581-703`; `WmoModel.cs:262-264, 402-405`; `WmvMainMapObject.cs:395-410` |
| WMO on the host | Metadata only (`wmo.cpp`), constructed only by `ModelCanvas::LoadWMO`; no geometry. | live-host-only | `modelcanvas.cpp:399-421`; `ModelInspector.cpp:1048-1071` |
| Files | `CASCFolder` opens storage as `<folder>*<product>`, resolves files by FileDataID (`CASC_OPEN_BY_FILEID`), and `buildPresentIdIndex` marks ids not on this computer. `[clientcaps] files not installed` counts only `.m2 .skin .anim .skel .blp .wmo .db2`. | live | `CASCFolder.cpp:86-149, 249-317, 332-343`; `WoWFolder.cpp:67-91` |
| Database | 55 tables from `games/wow/12.0/database.xml`, positions refreshed from 47 bundled `.dbd` files, cached per build in SQLite; tables are opened by listfile path. **No Map, AreaTable, Light\*, Liquid\*, ZoneLight\* or GroundEffect\* table is defined, cached or queried.** The host WDC3 reader bounds strings to section 0 and does not merge later sections' copy tables (a multi-section Map.db2 would read with empty names). | live-host-only | `WoWDatabase.cpp:89-128, 130-281`; `wdc3file.cpp:205-238, 240-466`; `bin_support/dbd/` (47 files) |
| Viewer modes | `ViewerMode { Models, Textures, Buildings }`. A mode touches the command bar, `SetViewerMode`, capability gating, `ModelInspector` and the Browse trees (`FileControl`, three tree states). | live | `Source/wowmodelviewer/ViewerMode.h:15-20` |
| Tests | Player: framework-free `WowParserTests.cs` (M2, skin, BLP, WMO, coordinates), `WmvLifecycleSelfTest` (`-wmvLifecycleTest`, offline, no world or multi-root tests). Host: `-m2inspect <list> [csv]`, `-unityipctest` (needs `-mo/-item/-wmo`). | live | `WowParserTests.cs:1-12`; `WmvLifecycleSelfTest.cs:2579-2598`; `app.cpp:236-260, 3739, 5010-5030` |

### 2.3 Global, single-object assumptions a world view breaks (task 6, part 2)

Every row is PROVEN_SOURCE (checked citations). The consequence column is INFERRED.

| Assumption | Where | Consequence for a world view |
|---|---|---|
| One model slot, one M2 job, one skin job, one dresser, one mount scene, one WMO, one WMO job; any load supersedes the job in flight | `WmvMain.cs:55-119, 293-373`; `WmvMainMapObject.cs:38-43` | Many instances need a scene/instance manager, not more slots. |
| Adoption disposes everything else (model, mount, dresser, previous WMO) | `WmvMain.cs:1338-1342`, `WmvMainMapObject.cs:533-546` | Loading a doodad through today's path would tear down the scene. |
| Camera reframed on every adoption (`orbit.Frame` / `FrameMapObject`), zoom/clip from the framed bounds; the camera belongs to the player only (no host camera command or report) | `WmvOrbitCamera.cs:106-107, 142-151, 234-292`; `WmvIpcClient.cs:1018-1127` | Streaming must never reframe; the host needs the camera position to pick tiles, or the player picks them. |
| Pushes are filtered against the one model: animation/skin/geoset pushes name that model's FileDataID; `PushIsAboutMapObject` drops them while a WMO is loading or shown | `WmvMain.cs:1506-1519`; `WmvMainMapObject.cs:123-131` | World instances need their own message family, not the model pushes. |
| One host load serial and await slots (`m_unityLoadSerial`, `m_mapObjectAwaited`, `m_modelAwaited`, `PlayerContent {Nothing, Model, MapObject, Unknown}`) | `modelviewer.h:544-620` | Tiles need per-tile/per-instance ids and states, and "world" as a content kind of its own. |
| Host content is one exclusive kind (`isModel`, `isChar`, `isWMO`; `ModelCanvas` holds one `WMO* wmo` and one `model()` root); the animation clock is the archived canvas's 10 ms timer driving the one canvas model | `modelviewer.h:107-109`; `modelcanvas.h:144-167`; `modelcanvas.cpp:1447-1463` | A world session is a new content kind beside these, not a reuse of the canvas. |
| Host serves requests synchronously, FIFO, on the GUI thread every 20 ms poll, with no per-poll budget, an unbounded send buffer, and one INFO log line per request and per response. All storage access stays on the GUI thread because `GAMEDIRECTORY` is not thread-safe | `UnityIpcServer.cpp:41, 227-267, 330-353, 1171-1244`; `UnityAssetAccess.h:12-13`; `UnityAssetAccess.cpp:21-57` | 5,322 reads for a city would freeze the window and flood the log. Moving serving off the GUI thread means giving the storage one owning thread, not adding locks around it. |
| No cancellation: superseded requests are still read and sent | `WmvMain.cs:430-436` | Panning wastes reads without a cancel message or a player-side scheduler. |
| Asset cache: 256 MB, three load generations, keyed by FileDataID under the client epoch; WMO files fetched with `cacheable=false` | `WmvIpcClient.cs:1730-1782, 1892-1928`; `WmvMainMapObject.cs:101-102` | A world needs a residency policy of its own; per-tile generations would thrash the M2 cache. |
| No GPU asset sharing: each runtime destroys its own meshes and textures; textures are decoded on the CPU and uploaded as uncompressed RGBA32 with mips; no GPU budget, nothing unloads unused assets | `WmvModelBuilder.cs:238-257, 3405-3444`; `WmvWmoBuilder.cs:223-237, 1123-1145` | 13,512 instances of 1,179 M2s need shared, ref-counted meshes and materials, instancing and BC upload (7.3). |
| One 4096 orthographic shadow map fitted to the framed bounds; preview light rig is view-relative | `WmvShadowRig.cs:46-59, 66, 82-86, 174-183` | Useless at tile scale (INFERRED); needs cascades, or off in world mode. |
| One active client per process (`Game` singleton, epoch per replacement) | `UnityAssetAccess.cpp:33`; `modelviewer.cpp:4097` | A world session ends on a client switch. |
| Live-object counters asserted `== 1 / == 0` by the host test; `runtimeState` reports one map object | `app.cpp:2604, 2620-2623, 3554`; `WmvMainMapObject.cs:723-746` | Tests must become mode-aware with the scene model, not be deleted. |
| Host-side chunk parsing cannot reach repeated chunks: `GameFile::setChunk` returns the first chunk of a name; `KNOWN_CHUNKS` lists no WDT/ADT chunk | `CASCFile.cpp:20-88`; `GameFile.cpp:125-156` | Any host-side world logic needs a new parser; do not extend `GameFile` chunk access for 256 MCNKs. |

### 2.4 Do not port

The archived transform (`xbase = -xpos + 32*533.33333`, OpenGL Y-up, normals `(-n1, n2, -n0)/127`) never executed and
was never checked against data. The removed alpha code copied row 62 into row 63 unconditionally in its uncompressed
paths (`cc1e14c5^:maptile.cpp:1341-1402`). That copy changes nothing on Classic Era 4-bit data, which already repeats its
last row and column (5.4). On Retail it alters every uncompressed 8-bit layer whose row 63 differs from row 62: 923 of
the 1,277 in the window (`alpha_edges.py`). Use these only as a hazard list.

---

## 3. Local clients and the corpus (task 2)

### 3.1 Clients, Map.db2, map catalog

Appendix T0 (format matrix by client) and T1. All PROVEN_BYTES.

- **Map.db2.** Every client's Map.db2 is WDC5 (layouts in T1). `wowfmt/db2.py` decodes it without a schema: all six
  field storage types, sections, ID lists, copy tables, and strings counted across all sections' records and string
  tables.
- **Field 0 and field 1.** Field 0 is the map directory, field 1 the map name.
- **Field 21 = WdtFileDataID (Retail, Classic Beta).** Proven without the listfile. Era and MoP open WDTs by path, and
  for every directory that resolves there, Retail's field 21 carries the same FileDataID (220 of 220 agree) and so
  does Classic Beta's (57 of 57). The listfile agrees as a secondary count: 1,073 Retail rows, 71 Beta rows.
- **Era and MoP Map.db2 have no WDT FileDataID column**: no integer column matches any WDT in either. Their rule is
  the path `world/maps/<Directory>/<Directory>.wdt`, tested on every Era/MoP map (4.1).
- **Six Retail rows disagree with the listfile** (T1: 6 mismatch). Maps 2672, 2673, 2674, 2676 and 2677 carry
  5348475, which the listfile names `2671.wdt` (map 2671's own WDT). Map 1582's directory holds an invalid character
  (field 21 = 2449144). Both targets are `NOT_IN_ROOT`, so these rows cannot show which rule is right. The rule for Retail and Beta is WdtFileDataID because
  their storages cannot open paths at all.
- **Rows with no WDT.**
  - Retail: 110 rows (105 distinct WDTs) name WDTs the root does not list, and 105 rows have field 21 = 0, most of
    them transport maps.
  - MoP: all 51 rows the listfile cannot resolve are transport maps. Probed by path, all 51 are `NOT_IN_ROOT`, so they
    have no WDT.
  - Not in the root otherwise: Classic Beta 2, Era 1, MoP 7.
- **Classic Beta's Map.db2** has 3 sections under keys this computer does not have (8 rows). These rows are read as
  zero-filled and reported as `ENCRYPTED_PARTIAL`, never used as data. AreaTable.db2 of Classic Beta is likewise
  partial (1,373 rows readable).

### 3.2 The corpus

Selection is reproducible (`select_corpus.py`: fixed rules, ties go to the smaller x, then y). Retail and Classic Era
were extracted over the same 8x7-tile window of map 0 "Azeroth" (Eastern Kingdoms), x 29-36, y 45-51. Classic Beta and
MoP Classic were extracted over the 3x3 window x 31-33, y 47-49. Every MAID part and the MPHD side files were
extracted. Appendix T4 lists every tile's FileDataIDs, T5 the per-tile content.

| Scenario | Map, tile (client) | Main area (AreaTable) | Why chosen (rule) |
|---|---|---|---|
| Ordinary outdoor | 0 Azeroth, 32_50 (Retail) / 31_48 (Era rule) | Elwynn Forest | the window's main zone; fewest holes+liquid+WMOs; ≥200 doodads |
| Holes / layers | 34_46 (Retail) / 30_48 (Era rule) | Blackrock Mountain / Stormwind City | most high-resolution hole cells (Retail 256) |
| Water | 29_48 (Retail) / 36_49 (Era rule) | Stormwind Harbor / Lake Everstill | most liquid chunks (Retail 229, Era 132) among tiles with ≥100 doodads |
| Dense structures | 30_47 (Retail) / 34_51 (Era rule) | Stormwind Harbor / Duskwood | most WMO placements (Retail 27), then doodads (2,451) |
| Modern terrain material | 30_49 (Retail) | Stormwind City | the only window tile with non-zero height textures (MHID: 2) and MTXP |
| WMO-only (modern form) | 34 StormwindJail (Retail) | -- | MAID present, 0 tiles, one MODF named by FileDataID 108631 (flags 0x8) |
| WMO-only (old form) | 1212 "Garrison - Alliance - Blacksmith 3" (Retail) | -- | MWMO path `world\wmo\draenor\human\6hu_garrison_blacksmithing_hub.wmo`, MODF name_id 0 |
| Another era | Classic Era (both pick sets), MoP Classic and Classic Beta windows | T4 | same FileDataIDs; every scenario tile differs in bytes (13 corpus files, mostly empty obj0/obj1 of sea tiles, are byte-identical across clients) |

Era notes:
- Classic Era's 30_47 is Stormwind City with no harbour (8,248-byte tex0, 168-byte obj1). That is why the Era rule picks
  differ.
- Era MAID has no `lod` and no map textures on 2,537 of its 2,719 tiles. MoP MAID has none on 7,310 of 9,848.

### 3.3 Local availability: FILE NOT LOCAL is its own state (T11)

`casc_extract` decides availability from one enumeration of the storage, as the viewer's `buildPresentIdIndex` does.
CascLib opens a listed file that is not on this computer as an empty file (PROVEN_SOURCE: `CASCFolder.cpp:288`; not
re-measured by this audit), so the open alone cannot tell. Every
non-zero word of every MAID of every extracted WDT was censused (status only, nothing written):

| Client | MAID words | OK | `NOT_IN_ROOT` | `NOT_LOCAL` |
|---|---|---|---|---|
| Retail | 324,898 | 324,203 | 695 (all minimaps, 47 maps) | 0 |
| Classic Beta | 24,245 | 24,234 | 0 | **11** (6 minimaps, 2 obj0, 1 root, 1 obj1, 1 map texture; 8 maps) |
| Classic Era | 14,141 | 14,077 | 64 (all minimaps, 1 map) | 0 |
| MoP Classic | 56,854 | 56,222 | 632 (all minimaps, 29 maps) | 0 |

Consequences, all PROVEN_BYTES:
- A MAID word can name a file the client does not have, either because the root does not list it or because it was
  not downloaded.
- Classic Beta's Kalimdor tile 44_24 has no root ADT on this computer (FileDataID 787450, `NOT_LOCAL`), while its other
  parts are present.
- A viewer must therefore load per part and report a missing part as such. It must never treat an empty read as
  terrain.
- Whole storages: the Retail root lists 1,925,832 FileDataIDs, of which 586 are not on this computer. Beta: 706 of
  1,242,272.

---

## 4. WDT (task 3)

### 4.1 Path versus FileDataID, per client (PROVEN_RUNTIME, CASC probe)

| Client | Azeroth WDT, WDL, ADT root/tex0/obj0 of 32_50 by path | by FileDataID | Map.db2 | Rule |
|---|---|---|---|---|
| Retail | all `NOT_IN_ROOT` (root lists no names) | all OK | field 21 = WdtFileDataID | FileDataID only |
| Classic Beta | all `NOT_IN_ROOT` | all OK | field 21 = WdtFileDataID | FileDataID only |
| Classic Era | all OK | OK, same bytes (WDTs 58/58; root, tex0, obj0 of 32_50 and the WDL identical) | no WDT column | path from the map directory, or FileDataID |
| MoP Classic | all OK | OK, same bytes (WDTs 227/227; root, tex0 and the WDL identical; obj0 not compared) | no WDT column | path from the map directory, or FileDataID |

After the WDT, no file needs a path in any client:
- MAID gives the ADT parts and images by FileDataID (4.2).
- MPHD gives the side files.
- MDDF/MODF name models by FileDataID (5.6).

The one exception is the old WMO-only WDT form: 193 Retail maps carry an MWMO path string. Retail storage cannot open
that path (tested on map 1212: not in root), so only a listfile can turn it into a FileDataID (948620). 189 of these maps are garrison building maps; the other 4 are Bloodtotem
Cavern phase maps (1509, 1510) and Stormheim prescenarios (1517, 1518) (risk R6).

### 4.2 Structure census (T0, T2, T3; PROVEN_BYTES)

- **Forms.** Every WDT starts `MVER(18) MPHD MAIN`, then one of three forms. No WDT has both tiles and a MODF.

  | Form | Retail | Classic-family |
  |---|---|---|
  | `MAID`, optionally `MODF` (WMO-only), `MAI2` or `MANM` | 763 | all |
  | old `MWMO MODF` | 193 | none |
  | bare, nothing after `MAIN` | 13 | none |

- **MAIN** has 4096 entries of 8 bytes, index `y*64 + x`. In every MAID-bearing WDT of every client:
  - flag value 1 ⇔ MAID names files (59,907 tiles);
  - flag value 2 ⇒ no MAID words (1,795,279 entries, plus 49,152 in Retail WDTs without MAID);
  - flag 0 entries have no MAID words either;
  - the second word is 0 in every entry.

  Asserted by `test_main_bit_1_iff_maid_names_files`. The meaning of flag 2 is UNKNOWN (U3).
- **MAID** has 4096 entries of 32 bytes, i.e. 8 FileDataIDs per tile. Word order, proven from content without the
  listfile (T3, second table):

  | Word | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | File | root | obj0 | obj1 | tex0 | lod | map texture | map texture `_n` | minimap |
  | Identified by | MHDR | MDDF | MLDD | MAMP | MLHD | BLP2 512², 10 mips | BLP2 256², mips | BLP2 512², 1 level (256² in Era and MoP) |

  Evidence per word and client (corrected in `01-parsers.md` 13):
  - Words 0-3: chunk tags in all four clients.
  - Word 4: MLHD in Retail and Beta. Era and MoP name no lod in the window and no such file is pinned, so there it rests
    on the listfile.
  - Words 5 and 6: Retail and Beta only, by BLP2 size and mip count, plus decoded images: word 5 shows terrain only and
    word 6 a normal-like field. Era and MoP: listfile only.
  - Word 7: 512² without mips in Retail and Beta, where the decoded image is the minimap with buildings and water.
    256² in Era and MoP. Size alone cannot separate words 5 and 7; the mip count and the image do.

  - The tile index: every root's MCNK 0 sits at the position of MAID index `y*64 + x` (asserted by
    `test_maid_index_is_y_times_64_plus_x`).
  - Over all WDTs the listfile names agree in all four clients (T3, first table). That is a consistency check only.
  - Words 4-6 are optional: zero on 21% (Retail) to 93% (Era) of tiles.
- **MPHD** has 8 words. Word 0 holds the flags; words 1-6 hold the FileDataIDs of `_lgt.wdt`, `_occ.wdt`, `_fogs.wdt`,
  `_mpv.wdt`, `.tex` and `.wdl`, in that order, in all clients. Only Azeroth's side files are pinned: the same six
  FileDataIDs, in the same positions, in all four clients. Retail identifies all six by their chunk ids (T8). In Beta
  (`_mpv`), MoP (`_fogs`, `_mpv`) and Era (`_lgt`, `_fogs`, `_mpv`) the file is a 12-byte MVER-only file, identified only
  by its MVER version (`_lgt` 20, `_fogs` 2, `_mpv` 3) and by having Retail's FileDataID. For other maps the order rests
  on listfile names. Word 7 is non-zero exactly on modern-form WMO-only WDTs (Retail 35, Beta 21,
  Era 21, MoP 33). Where the listfile names it (all of Retail's 35 and MoP's 33; Beta 16 of 21, Era 19 of 21), the name
  is the WMO's `.pd4`. The file was not read (listfile name only).
- **MPHD flag bits.** Ten bits are seen (0x1-0x200, T2).
  - Bit 0x1 ⇔ WMO-only (a MODF and no tile), exactly, in all four clients (Retail 228/228, Beta 21, Era 21, MoP 33).
  - The alpha-map encoding of the four Azeroth WDTs (three distinct flag values) correlates with the flags:
    - Era `0x248`: 4-bit;
    - MoP `0x24E` and Retail/Beta `0x3CA`: 8-bit.

    That is three data points. Bit 0x2 separates them as well as the test "0x4 or 0x80 set" does (neither bit does
    alone), and client identity explains them equally. **The proven rule is the layer spacing (5.4), not an MPHD
    bit** (U1).
- **MAI2**:
  - where it occurs: Retail Azeroth, Kalimdor and PVPZone04; Beta those and 2991; Era PVPZone04;
  - layout: 4096 × 32 bytes; word 0 is non-zero on a few tiles, words 1-7 are always zero;
  - word 0's identity (a `world/liquidflow/...` image) is listfile name only; its use is UNKNOWN (U4).
- **MANM** (four Retail maps): 2,452-82,220 bytes, not a 64x64 table. UNKNOWN (U5).

### 4.3 WMO-only maps (PROVEN_BYTES)

- **Modern form** (MAID present, no tiles). One MODF:
  - name_id is a FileDataID, flags 0x8, scale 1024;
  - the example is 34 StormwindJail: 108631, position (0,0,0), extents (-151.8, -35.5, -197.3)..(150.3, 15.2, 8.1),
    uniqueId 0xFFFFFFFF;
  - **the WDT MODF rotation must be applied**: Era's DireMaul WDT turns its WMO by yaw 180 (`rotation (0, 180, 0)`).
    The placement rules of 6.3-6.4 apply unchanged.
- **Old form** (Retail only, 193 maps). An MWMO path string, plus one MODF with name_id 0, flags 0x0, scale 0 and
  rotation 0.
  - name_id 0 is an index or byte offset into MWMO; the two cannot be told apart with one name.
  - That scale 0 means 1.0 is INFERRED from the absent 0x4 flag.

### 4.4 WDL (PROVEN_BYTES)

- **Chunks**: `MVER, MLDD, MLDX, MLMD, MLMX, [MSSN MSSO MSSF in Retail], MAOF`, then `MARE` (+`MAOE`, `MAHO`) per
  tile with heights (T8).
- **`MAOF`** holds 4096 absolute offsets, index `y*64+x`. At each offset, `MARE` holds 17x17 + 16x16 int16 values.
- **The 17x17 grid** holds the chunk-corner heights, rows along index_y. All 14,336 corners of the Retail window are
  within 1 yard (median 0.50: integer truncation), and so are the Era, MoP and Beta windows. Index 16 is the far edge
  (the next tile's first corner), also within 1 yard. Transposed, 91% (Retail) / 80% (Era) fail.
- **The 16x16 grid** is each chunk's mean of its 145 heights, truncated to an integer: 0 difference on every chunk of
  all four windows. It is not the centre vertex (median 1.8 yd off). Far terrain (W10) must treat it as chunk means.
- **Not interpreted**: `MAOE`, `MAHO`, `MSS*`, and the WDL `ML**` chunks (U6).

### 4.5 Side files (T8; listed, NOT interpreted)

| File (MPHD word) | Retail | Classic Beta | Classic Era | MoP Classic |
|---|---|---|---|---|
| `_lgt.wdt` | `MPL3 MSLT MTEX MLTA` | `MPL3 MSLT MLTA` | `MVER` only | `MPL2 MSLT` |
| `_fogs.wdt` | `VFOG`, `VFEX`×152, `VFE2`×152 | `VFOG`, `VFEX`×246, `VFE2`×246 | `MVER` only | `MVER` only |
| `_mpv.wdt` | `PVMI/PVPD/PVBD`×19 | `MVER` only | `MVER` only | `MVER` only |
| `_occ.wdt` | `MAOI MAOH` | `MAOI MAOH` | `MAOI MAOH` | `MAOI MAOH` |
| `.tex` | `TXVR TXBT TXMD`×14,171 | ×6,464 | ×3,730 | ×6,831 |

No visual rule is derived from these chunk names (task 8). Classic Era carries no fog, light or volume data in its WDT
side files at all, so its lighting must come from the Light tables (T10). MoP has no `_fogs` data, but its `_lgt` holds
128,884 bytes (MPL2) whose role is not interpreted.

### 4.6 Map images (headers PROVEN_BYTES)

MAID words 5-7 are BLP2 images (compression 2, alpha type 0):

| Word | Retail / Beta | Era / MoP |
|---|---|---|
| 5: map texture | 512² | absent on most tiles |
| 6: map texture `_n` | 256² | absent on most tiles |
| 7: minimap | 512² | 256² |

Only their BLP2 headers were read; that they are top-down images of the tile is INFERRED (listfile names
`maptextures`/`minimaps`). Gate G4 uses them as the orientation reference and must confirm it.

---

## 5. ADT (task 4)

Measured on each client's window: Retail and Era 56 tiles (14,336 MCNKs each), Beta and MoP 9 tiles (2,304 each).
Appendix T6. All PROVEN_BYTES.

### 5.1 Split-file assembly

| Part (MAID word) | Top-level chunks | MCNK content |
|---|---|---|
| root (0) | `MVER MHDR [MH2O] MCNK×256 [MFBO]` (MFBO: Retail, MoP); rarely `MBMH MBBB MBMI MBNV` (2 of 56 Retail) | 128-byte header, then `MCVT [MCCV] MCNR MCSE [MCBB] [MPTX]` (MPTX: Beta, 5 chunks) |
| tex0 (3) | `MVER MAMP MDID MHID MCNK×256 [MTXP]` (MTXP: 1 Retail, 1 MoP tile). No `MTEX` names in any client | no header: `MCLY [MCSH] [MCAL]` |
| obj0 (1) | `MVER MDDF MODF [MWDR MWDS] [MLMB] MCNK×256` (MLMB: MoP; MWDR/MWDS: 1 Retail tile). No `MMDX/MMID/MWMO/MWID` | no header: `[MCRD] [MCRW]` |
| obj1 (2) | `MVER [MLFD] MLDD MLDX [MLDL] [MDLI MDLD] [MLDB] MLMD MLMX [MLMB] [MWDR MWDS]` (MWDR/MWDS: 1 Retail tile) | none |
| lod (4) | `MVER MLHD MLVH MLLL MLND MLVI MLSI` + repeated `MLLD MLLN MLLI MLLV`, rarely `MBMH MBBB MBMI MBNV MBMB` (Retail); `... MLLD MASD MALP` (Beta) | none |
| tex1 | **absent in all four clients**: no MAID word, and the listfile's `_tex1` FileDataIDs for the 5 scenario tiles are not in any root, by path or by id | -- |

Proofs:
- Root MCNK headers carry index_x/index_y equal to the chunk order (n%16, n//16) in 100% of 33,280 chunks (asserted by
  `test_mcnk_position_and_index`).
- The sub-chunks after the 128-byte header walk without a single error, and tex0/obj0 MCNKs walk from their first byte.
- The root header's layer count and its alpha/shadow offsets and sizes are 0 in every root header of all four clients,
  and `MCLY/MCAL/MCSH` occur only in tex0: **in split files, layers live only in tex0**.

Sub-chunk sizes (`subchunk_sizes.py`):

| Sub-chunk | Size (bytes) | Content |
|---|---|---|
| `MCVT` | 580 | 145 floats |
| `MCNR` | 448 | 145 × 3 + 13 |
| `MCCV` | 580 | 145 × 4 |
| `MCSE` | 0 | -- |
| `MCSH` | 512 | -- |
| `MCBB` | 20 | -- |
| `MPTX` | 16 | -- |

### 5.2 MCNK header (offsets proven by what each field equals)

| Offset | Field | Proof |
|---|---|---|
| 0 | flags | Values `0x18000`/`0x18040` (Retail), `0x18040`/`0x18042` (Beta), `0x18000-0x18003` (Era), `0x18000/1/40/41` (MoP). Bit 0x40 ⇔ MCCV present (Retail 2,412 = 2,412; Beta 2,304; MoP 299). Bit 0x1 ⇔ MCSH present (Era 6,976 = 6,976; MoP 1,627 = 1,627). Bits 0x10000 and 0x8000 are set on every chunk; bit 0x2 (Era 215, Beta 6) is UNKNOWN (U7) |
| 4, 8 | index_x, index_y | = chunk order, 100% |
| 20 | 64-bit high-resolution hole mask | 5.4, 6.5 |
| 52 | area id | resolves through each client's own AreaTable.db2 to the expected zones (T4) |
| 60 | 16-bit low-resolution holes | always 0 (bit 0x10000 is always set) |
| 88 | MCSE offset | = the MCSE sub-chunk's position from the MCNK chunk start (100%) |
| 96 | "liquid" offset | equals the MCSE offset (the MCSE sub-chunk's start: 1180, or 1768 with MCCV) in every chunk, with size 0, although no MCLQ exists anywhere (U8) |
| 104 | position (x, y, z) | 6.1 |
| 116 | MCCV offset | = the MCCV sub-chunk's position (100% when present) |

### 5.3 Heights and normals

`MCVT` holds 145 floats: rows of 9 outer and 8 inner vertices alternating (17 per pair), heights relative to
position.z. Orientation is in 6.2. `MCNR` (448 bytes) is present in every root MCNK. Its decoding is not used by this
audit (U9).

### 5.4 Holes and alpha maps

**Holes.**
- Every root MCNK of all four clients has flag 0x10000 and uses the 64-bit mask at offset 20; the 16-bit field is
  always 0. Chunks with holes: Retail 81, Era 80, MoP 16, Beta 11.
- Bit order: `bit = row*8 + col`, least significant first, rows along the vertex rows. This is the only one of the 8
  distinct candidate orders that passes both independent tests (6.5).
- Most masks are whole 2x2 blocks (Retail 78 of 81), i.e. a low-resolution 4x4 hole written at high resolution.
- No local client has pre-split ADTs: every local WDT with tiles has MAID.

**MCLY.** 16 bytes per layer: texture index into MDID, flags, offset into MCAL, effect id.
- The decoder reads every layer after the first. Flag 0x100 is set on exactly those layers (32,848 Retail, 25,685 Era,
  5,455 MoP, 5,147 Beta) and on no first layer. **0x100 = "has an alpha map" is a 100% correlation, not an isolated
  meaning**: the audit decodes by layer position, and the planned C# parser (section 15) must decode by 0x100 and
  assert that it agrees with the layer index.
- 0x200 marks a compressed layer.
- Other bits seen (0x1-0x80 and 0x400, the last on two MoP first layers) are UNKNOWN (U10).

**MCAL encodings.**

| Encoding | Retail | MoP | Beta | Era |
|---|---|---|---|---|
| Compressed (RLE; see below) | 13,109 | 2,262 | 2,179 | -- |
| Mixed raw/compressed | 689 | 30 | 28 | -- |
| Raw, 4096-byte layer spacing | 521 | 11 | 4 | -- |
| Raw, 2048-byte layer spacing | -- | -- | -- | 11,167 |

- Compressed: a count byte; top bit set = repeat the next byte `n&0x7F` times, clear = copy `n&0x7F` bytes; ends at
  4096 values. Every RLE layer ends exactly where the next layer starts.
- Raw: the layer spacing is 4096 or 2048 with the first offset 0. That check is structural only.
- **The proven encoding rule is the spacing of the MCLY offsets** (2048 = 4-bit, 4096 = 8-bit), not an MPHD bit (4.2).

**4-bit maps (Era).**
- Layout: 64x64 nibbles, **low nibble first**. Shown by a chunk-edge test (column 63 against the next chunk's column
  0): mean step 0.95 low-first vs 1.49 high-first; and within rows across byte boundaries: 1.13 vs 1.54.
- Every one of the 25,672 non-uniform Era maps repeats its last row and column (row 63 = row 62, column 63 = column 62):
  the data hold 63x63 distinct texels. In Retail's 8-bit maps only 65% do, so it is not a format rule there.
- **The 8-bit expansion of a 4-bit value is UNKNOWN** (U19). Retail's own 8-bit re-encoding of the same Era terrain
  maps nibble n to 16n+15 for 64% of texels with n in 1..14 (mode 16n+15 for n ≥ 4; 30/46/62 for n = 1/2/3). 17n and
  16n each match under 0.3%. How the Era client expands nibbles at draw time cannot be measured from files. The
  candidate expansions (17n, 16n, 16n+15) differ by at most 15/255 of opacity; gate W5 checks it visually.

**Orientation.** `alpha[row*64 + col]`, rows along the vertex rows and columns along the vertex columns. It is the
only one of 8 symmetries that is continuous across both chunk edges (6.5).

**MCSH.** 512 bytes where present; present ⇔ flag 0x1 (Era, MoP); absent in Retail and Beta.

### 5.5 Vertex colours, textures, height texturing

- **Vertex colours.** `MCCV` (145 × 4 bytes) is in 2,412 of 14,336 Retail chunks (none neutral), all 2,304 Beta chunks
  and 299 MoP chunks; none in Era. `MCLV` is in no chunk.
- **Textures.** `MDID` holds diffuse FileDataIDs: 3-23 per Retail tile (median 10), 1-19 Era (median 8). `MHID` holds
  height-texture FileDataIDs, non-zero only on Retail 30_49 (2) in the window. `MTXP` (16 bytes per texture) is in
  Retail 30_49 and one MoP tile. Height-based blending exists in the format; its equation is UNKNOWN (U11).
- **`MAMP`** (1 byte + padding) is present everywhere. Value meaning UNKNOWN (U12).

### 5.6 Placements (obj0, obj1)

- **Records.** `MDDF` is 36 bytes and `MODF` 64 bytes. **Records repeat, identically, in the obj0 of several tiles
  (no uniqueId has differing copies in any window): deduplicate by uniqueId.** Retail window: 32,848 MDDF entries are
  31,667 doodads, and 268 MODF entries are 202 WMO placements.
- **Names are FileDataIDs** in all four clients: `.m2` and `.wmo` by listfile name (T6), and by content for the Retail
  models that were read (T9, 6.4).
  - Every MDDF carries flag 0x40 and every MODF 0x8. Since no record lacks them, their meaning is not isolated; the WDT
    old form (name by MWMO, flag 0) is the only contrast for 0x8.
  - No name tables (`MMDX/MMID/MWMO/MWID`) exist in any client's obj0.
  - Other flags: MDDF 0x200 (6,789 Retail) and 0x20 (7); MODF 0x4 (on all) and 0x40/0x80. All UNKNOWN beyond the
    above (U13).
- **obj1.**
  - `MLDD` repeats obj0's MDDF: each of the 32,848 Retail records matches the MDDF record with the same uniqueId in
    name, position, rotation and scale. The flags gain bits 0x20/0x08/0x10, except on the 7 records that already had
    0x20. The match is by uniqueId, not by position: on Retail tiles 29_48 and 30_48 the MLDD order is a permutation of
    MDDF (212 of 229 and 1,676 of 1,703 positions differ).
  - `MLDX` is each placed doodad's world-space box (6.4); box i belongs to MLDD record i.
  - These give exact per-doodad bounds for culling.
- **Scale.**
  - MDDF: `u16/1024`, proven by the MLDX boxes (6.4).
  - MODF: every ADT MODF in every window has 1024 (= 1.0), so there the field was seen only at that value (the
    old-form WDT MODF has 0, 4.3).
- **`MCRD`/`MCRW`** per MCNK are doodad/WMO reference lists (counts only measured).

### 5.7 Liquids (MH2O, T7)

MH2O is present in 44 of 56 Retail, 35 of 56 Era, 8 of 9 MoP and 8 of 9 Beta tiles.
- Offsets are from MH2O's data start, and every instance is in bounds.
- `liquid_type` values: 1, 2, 5, 7 and 301 (Retail).
- The second word is 42 or a larger value (T7). That the larger values are LiquidObject ids is not checked: Era's
  LiquidObject.db2 has 5 records while 27 distinct values occur, and the tables were read header only (U14).
- Vertex data is located but not decoded. Its format needs LiquidType → LiquidMaterial (or a word < 42), and those
  tables are not readable without definitions (U14).
- `MCLQ` (old liquid) exists in no chunk of any client.

### 5.8 Unavailable vs unsupported

**Unavailable in the local clients** (nothing to read):
- pre-split single-file ADTs; `MCIN`; `MTEX`/`MMDX`/`MWMO` name tables in ADTs;
- low-resolution holes; `MCLQ`; `MCLV`; `tex1`;
- Era: `_lod.adt` and map textures on most tiles (present on 182 of 2,719), and fog/light/volume side data;
- MoP: `_lod.adt` and map textures on most tiles (74%; none in the window), and fog and volume (`_mpv`) data;
- Beta: volume (`_mpv`) data.

**Present but not supported by any parser yet** (UNKNOWN semantics):
- root/tex0/obj0: `MFBO`; blend meshes `MBMH/MBBB/MBMI/MBNV/MBMB`; `MCBB`; `MPTX`; `MTXP`/`MHID` height texturing
  (U11); `MAMP` (U12); `MWDR/MWDS`; `MLMB`;
- obj1: `ML**/MDL*` beyond MLDD/MLDX;
- `_lod.adt`: all chunks (incl. Beta `MASD/MALP`);
- WDT: `MAI2` words; `MANM`;
- WDL: `MAOE/MAHO/MSS*` and `MLDD/MLDX/MLMD/MLMX`;
- side files: `_fogs/_lgt/_mpv/_occ/.tex` contents (`.pd4`: named by the listfile, not read);
- MH2O vertex data.

---

## 6. Coordinates (task 5)

Notation:
- T = 1600/3 yd (one tile), C = T/16 (one chunk), U = C/8 (one cell).
- Tile (tx, ty) is MAIN/MAID index `ty*64 + tx`, named `<map>_<tx>_<ty>.adt`.
- The world frame is the game's: x, y horizontal, z up.

The rules of 6.1-6.5 are asserted on the pinned bytes by `tests/test_corpus_rules.py`, in all four clients except
where noted:
- the MCNK position and index; the outer vertex order (against all 8 symmetries) and the inner vertex index;
- the seam x/y finding; the doodad placement height;
- the hole bit order (both the discriminating-chunk and continuity tests, against the 7 other distinct orders);
- the alpha orientation (against the 7 other symmetries);
- the WMO and MLDX rotation verdicts (Retail).

Each is checked against its competing readings, so the tests fail if the data stop telling them apart. 6.6 has no
test (gate G4).

### 6.1 Tile and chunk → world (PROVEN_BYTES, 100% of 33,280 chunks in four clients)

```
MCNK.position.x = (32 - ty)*T - index_y*C        MCNK.position.z = base height of the chunk
MCNK.position.y = (32 - tx)*T - index_x*C
```
Every stored x/y is within 1/768 yd (0.0013021 yd, a bound that is reached) of the formula; every deviation is 0,
1/3072, 1/1536 or 1/768 yd. tx/ty come from the MAID index, index_x/y from the header, and the
position from the bytes, so the test is not circular. The world origin is the map centre (the corner of tile 32,32).
World x falls as ty grows, world y falls as tx grows, and z is up.

### 6.2 Chunk vertex → world (PROVEN_BYTES)

```
outer vertex (r, c), r, c = 0..8:   x = X0 - r*U,          y = Y0 - c*U,          z = pos.z + MCVT[17r + c]
inner vertex (r, c), r, c = 0..7:   x = X0 - (r + 1/2)*U,  y = Y0 - (c + 1/2)*U,  z = pos.z + MCVT[17r + 9 + c]
X0 = (32 - ty)*T - index_y*C,  Y0 = (32 - tx)*T - index_x*C      -- from the integer indices, in double
```

- **Heights are exact at every seam.** Every vertex two chunks or two tiles share has the same height to 0.00005 yd:
  213,597 shared points (12,387 on tile seams) in Retail and Era, and 33,981 in MoP and Beta. With the transposed or
  any mirrored index order, between 46% and nearly all of them differ.
- **x/y are not exact if taken from the stored position.** Only 9,552 of 28,672 stored values equal the float32 of the
  formula (Retail and Era each; MoP and Beta 1,536 of 4,608). Building seam vertices from the stored float32 position
  gives bit-equal seams on 0 of 13,440 seams (MoP and Beta 0 of 2,160).
  **Compute x/y from the integer vertex index in double, subtract the floating origin, then cast. Use the stored
  position only for z.** A watertight mesh then needs no stitching.
- **Inner vertices** follow `MCVT[17r+9+c]` at the cell centre. Against the mean of their four corners: median 0.12 yd
  (Retail), 0.06 (Era), 0.11 (MoP), 0.10 (Beta). Every transposed or mirrored variant: 1.3-5.5 yd.
- **The surface.** Each cell is four triangles fanned from its inner vertex. This exact surface is what places doodads
  on the ground (6.3).

### 6.3 Placement position → world (PROVEN_BYTES)

`world = (32T - p[2], 32T - p[0], p[1])` for an MDDF/MODF position p in file order. Doodads, measured on the exact
fan surface:

| Client | Unique doodads | Over the window's terrain | Median \|Δz\| | Within 0.1 yd | Within 2 yd |
|---|---|---|---|---|---|
| Retail | 31,667 | 31,500 | 0.021 yd | 17,947 | 28,388 |
| Era | 23,780 | 23,632 | 0.0003 yd | 15,139 | 21,651 |
| MoP | 6,411 | 6,323 | 0.001 yd | 3,797 | 5,724 |
| Beta | 5,154 | 5,073 | 0.0003 yd | 3,310 | 4,675 |

More doodads sit slightly sunk than raised (Retail 11,554 below vs 4,533 above by more than 0.01 yd).

### 6.4 Placement rotation and scale (PROVEN_BYTES)

```
R_world = Rz(a1 + 180°) · Ry(a0) · Rx(a2)       (degrees, world axes; Rx applied first, then Ry, then Rz)
world point = position (6.3) + scale · R_world · model point       (model space: the M2/WMO file's own axes)
scale: MDDF u16/1024; MODF u16/1024 (seen only at 1024)
```

- **WMOs** (`rotation.py`). The candidate set has 2,880 conventions: axis assignments, signs, orders, yaw offsets
  0/90/180/270 and mirrored model frames. Each placed WMO's own box (MOHD) is compared with the MODF's stored world
  extents, over the 202 unique Retail placements:

  | Convention | Placements within 0.1 yd | Tilted ones (of 11) |
  |---|---|---|
  | The one above | 201 (median error 0.001 yd) | 11 |
  | Next best order | 199 | 9 |
  | Best without the 180° yaw offset | 41 | -- |
  | Best with a mirrored model frame | 72 | -- |

  The one misfit (uniqueId 2848560, WMO 112871, 14 yd) is a WMO whose root box (MOHD) disagrees with its own group box:
  its one MOGI box, placed by the same transform, fits the stored extents to 0.001 yd (checked by the review,
  `review\placements-verdict\t3_misfit_wdt.py`). It is not a stale placement. **MODF extents are exact world bounds.**
- **Doodads** (`mldx.py`). obj1 `MLDX` holds each doodad's world box; it is compared with the M2's own box (header
  offset 0xA0) under each convention, over 25,648 doodads:

  | Convention | Within 0.5 yd | Tilted ones (of 8,305) |
  |---|---|---|
  | The one above | 25,621 | 8,300 |
  | Pitch/roll signs (−,+) | 23,636 | 6,315 |
  | Pitch/roll signs (+,−) | 23,527 | 6,206 |
  | Scale ignored | 15,990 | -- |
  | Best without the yaw offset | 18,129 | -- |

  **This proves the pitch/roll signs and the u16/1024 scale.**
- **The rotation is proper** (det +1): mirrored model frames lose clearly. Model space enters the world without a
  handedness change.

### 6.5 Holes and alpha orientation (PROVEN_BYTES)

Hole cell (row, col) of a chunk lies at `(X0 - (row+½)U, Y0 - (col+½)U)`, bit `row*8 + col`. The 8 symmetries of the
cell grid were tested. The list also held whole-word MSB-first, but that reads bit 63-(8r+c) = 8(7-r)+(7-c), the same
cell as "rotate 180", so there are 8 distinct orders, not 9. The 01-parsers validation adds four non-symmetric
readings (u32 halves swapped, nibbles swapped, the u64 at +16 and at +24), which also lose. Two independent tests:

- **Discriminating chunks**: chunks where WMO boxes cover some cells but not all, so the order decides which cells fall
  inside. All 132 discriminating cells (Retail and Era) and all 28 (MoP and Beta) lie inside a WMO's box. The next-best
  order places 98 / 96 / 24 / 24.
- **Continuity**: pairs of hole cells adjacent across chunk edges. Retail 170, Era 200, MoP 26, Beta 19. The next-best
  order gets at most 95 / 120 / 16 / 13.

A naive "inside any WMO box" count is weak: 92-93% of all cells of hole chunks in Retail and Era (90% in MoP, 85% in
Beta, recounted by the review) are under a WMO anyway. That is why the discriminating-chunk test is the evidence.

Alpha texel (row, col) = byte `row*64 + col`, same directions as the vertices. Mean texel step across chunk edges:

| | Retail | Era (4-bit units) |
|---|---|---|
| This layout | 11.7 (both edge directions) | 0.99 / 0.95 |
| Any symmetry that changes an edge | ≥ 74 | ≥ 4.6 |

MoP and Beta behave like Retail: this layout gives 11.8 / 11.7 (MoP) and 12.9 / 12.7 (Beta), any symmetry that changes
an edge ≥ 73.5 (MoP) and ≥ 72.4 (Beta).

### 6.6 World → Unity (INFERRED; gate G4)

The player's only convention (H5) maps model space `(x, y, z)` → `(-y, z, x)` (det −1, winding flipped). The proven
placement is a proper rotation from model axes into world axes (6.4), so world space has the model frame's handedness.
Composing the two:

```
world W = (X, Y, Z)                                   (6.2, 6.3: proven)
unity  = (-(Y - Y0), Z - Z0, X - X0)                  H5 applied to world coordinates, with a floating origin
                                                      (X0, Y0, Z0) near the camera, subtracted in double before
                                                      the cast to float
rotation_unity = M · R_world · M⁻¹                    M = the H5 axis matrix → det +1: meshes converted by H5 keep
                                                      their winding
terrain triangles: build counter-clockwise seen from +Z in world space, then flip with the same FlipWinding as H5
```
Bounding boxes:
- chunks: x/y from the indices, z from MCVT min/max;
- WMOs: the stored MODF extents;
- doodads: the MLDX boxes.

A floating origin is **recommended**, not proven necessary. File positions can range up to 34,133 yd (64 tiles),
where float32 steps by 0.004 yd. The corpus windows reach 27,797 yd, and world coordinates span ±17,067 yd, both with
a 0.002 yd step. The source data are float32 already (stored MCNK positions are off by up to 1/768 yd). The reason is
camera-relative jitter at close range; W4 records the pixel error at the minimum camera distance with and without it.

---

## 7. Load pressure (task 6)

### 7.1 Measured from the files (T9; PROVEN_BYTES except the GPU geometry estimate and the totals that include it)

LOD0 only (what the player loads), every placement counted once per uniqueId:
- WMO doodad sets counted: set 0 plus the placement's set (U15).
- M2 dependency files: 0, because none of the 1,452 doodad M2s has a `SKID`, `AFID` or `BFID` chunk.
- Not read: 0 files in all three sets.

| | one tile (32_50) | 3x3 Elwynn (31-33, 49-51) | dense city 3x3 (29-31, 46-48) |
|---|---|---|---|
| File requests (ADT parts + M2 + first skin + WMO root + LOD0 groups + textures) | 381 | 2,393 | 5,322 |
| Bytes read | 12.4 MB | 93.8 MB | 445.6 MB |
| Terrain vertices / triangles / alpha layers | 37,120 / 65,536 / 693 | 334,080 / 589,824 / 6,014 | 334,080 / 589,824 / 5,008 |
| MDDF / MODF entries before dedup | 1,145 / 5 | 9,965 / 49 | 6,339 / 73 |
| M2 unique / instances (of which from WMO doodad sets) | 93 / 1,344 (199) | 575 / 13,258 (3,677) | 1,179 / 13,512 (7,261) |
| M2 vertices unique / instanced | 17,274 / 156,642 | 116,954 / 1.85 M | 566,671 / 3.55 M |
| WMO unique / instances / LOD0 groups (further-LOD groups not loaded) | 5 / 5 / 8 (0) | 21 / 38 / 154 (137) | 34 / 53 / 442 (484) |
| WMO vertices / indices | 9,789 / 26,466 | 293,283 / 842,424 | 1.03 M / 3.06 M |
| Textures unique / stored mip bytes | 177 / 8.3 MB | 1,023 / 49.6 MB | 2,443 / 323.0 MB |
| Textures as today's RGBA32 + mips upload (measured from each BLP's size) | 51.7 MB | 376.3 MB | **1,833.5 MB (×5.68)** |
| GPU geometry (INFERRED sizes: 32 B per terrain/WMO vertex, 48 B per M2 vertex, 2 B per index, 4 KB per alpha layer) | 5.7 MB | 56.2 MB | 103.4 MB |
| GPU total with block-compressed upload / with today's RGBA32 | 14.1 / 57.5 MB | 105.8 / 432.5 MB | **426.4 MB / 1,936.9 MB** |

### 7.2 Read time (T12; PROVEN_RUNTIME)

`read_timing.py` reads each set's files whole through CascLib (as the host reads them), on one thread, with the
operating system's cache warm from earlier audit reads, three runs each:

| | one tile | 3x3 Elwynn | dense city |
|---|---|---|---|
| Read | 92-145 ms | 652-806 ms | 2,065-3,529 ms |
| Throughput (best) | 135 MB/s, 4,141 files/s | 144 MB/s, 3,670 files/s | 216 MB/s, 2,577 files/s |
| Storage open + enumeration (once per session) | 2.7 s | 2.7 s | 2.7 s |

This approximates the read floor a world view starts from: the timed loop also writes each file to a scratch folder,
so the pure read is somewhat faster. It does not include IPC, the player's decode, mesh building or upload; G6
measures those on the first renderer. A cold disk cache was not measured: it would need the machine's file cache
flushed, which this audit did not do.

### 7.3 What it means (INFERRED from 2.3 and 7.1-7.2)

- **The host.** At today's rules a city means 5,322 synchronous GUI-thread reads of about 2-3.5 s in total, plus one
  log line each way and no cancellation. W3 moves serving to a thread that owns the storage, with a budget, before any
  multi-tile view.
- **Textures.** 2,443 textures uploaded as RGBA32 cost 1.83 GB against 323 MB stored. Block-compressed upload straight
  from the BLP mips is required.
- **Instancing.** 13,512 M2 instances of 1,179 models need shared meshes and materials and instanced or batched static
  drawing. The per-bone GameObject animated path cannot be used for static doodads.
- **WMO groups.** 442 groups need distance and portal culling. WMO further-LOD groups (484 in the city) and
  `_lod.adt`/WDL for far terrain exist in the data (5.1, 4.4).

---

## 8. Renderer: custom mesh vs Unity Terrain (task 7)

The left column is measured (`terrain_needs.py`, PROVEN_BYTES). What Unity Terrain can and cannot do is taken from its
documented heightmap model in the project's version (Unity 6000.5.9f1, URP 17.5.0, terrain module 1.0.0, H8). It was
not tested here, so those cells are INFERRED.

| ADT requirement (measured) | Unity Terrain (INFERRED) | Custom mesh + shader |
|---|---|---|
| 145 vertices per chunk: 64 inner vertices at cell centres. 20% of 917,504 inner vertices are >0.5 yd off the mean of their corners, 3.2% >2 yd, max 53 yd (Retail) / 140 yd (Era) | A heightmap is a regular (2ⁿ+1)² grid. At 129² per tile the inner vertices are lost. At 257² (the smallest that holds all 145) each quarter-cell is split by one fixed diagonal while the game fans every cell from its centre, so in two of each cell's four quarter-cells the diagonal disagrees. Measured at those quarter-cell centres: median 0.074 yd, p95 0.95 yd, max 27-41 yd; >0.25 yd in 24.8% of cells, i.e. 12.4% of all quarter-cells (Retail; Era 17.5% / 8.8%) | Exact: the same 145 vertices, 4 triangles per cell |
| 64-bit holes per chunk, one bit per cell | Holes per heightmap cell: representable at 129² | Exact: drop the cell's 4 triangles |
| Per-chunk texture sets of up to 4 layers (5 in 11 Beta chunks). 3-23 textures per tile, 1,529 distinct layer lists in 56 tiles; 64x64 alpha per layer per chunk | One global layer list per terrain; each 4 layers add a control map and a pass (23 layers = 6 passes) | Per-chunk layer indices into a texture array; one pass |
| Vertex colours (MCCV) on 2,412 Retail chunks, 299 MoP chunks and all 2,304 Beta chunks, none neutral | Not supported | Vertex attribute |
| Supplied normals (MCNR) | Computed by Unity | Used as given |
| Exact seams (heights equal; x/y from indices, 6.2) | Needs `SetNeighbors` and equal edge samples | Automatic: the same vertices |
| Height-texture blending (MHID/MTXP; 1 Retail tile in the window) | URP terrain blends by height only for ≤4 layers | Custom, once its rule is known (U11) |
| Liquids (MH2O per chunk), WMO/M2 placements | Separate anyway | Separate anyway |

**Recommendation**: one mesh per chunk (or per tile with chunk sub-meshes), built in the player from the parsed bytes,
with a terrain shader of our own (texture array, per-chunk layer indices, alpha array, vertex colour) on the existing
URP project. No new rendering framework and no Unity Terrain. This rests on measurement:
- the fan triangulation error at 257² is measured;
- inner vertices and vertex colours are needs Unity Terrain has no place for;
- per-chunk layer sets cost it a pass per four textures.

### 8.1 Architecture (target, for the next twelve branches)

```
 HOST (wx)                                         PLAYER (Unity)
 ------------------------------------------        ------------------------------------------------------------------
 Maps mode (W11): pick client + map ---open---->   WorldSession (W4 root; W6 streaming): world scene root beside the
   MapCatalog (W2): Map/AreaTable, WDT rule,         model/WMO slots, camera-driven tile set, residency, budgets
   capability + availability per part                 | prioritised, capped, cancellable requests
                                                       v
 AssetServer (W3): one thread owns the storage <-- AssetScheduler (W6): dedup, in-flight cap, cancel, client epoch
   budget per poll, log volume ---bytes-->            |
   (cancel arrives with protocol 12, W4)              |
   (getAssetByFileDataID unchanged, H7)               +-- WdtReader / AdtReader / Db2Reader (W1: pure C#, tested on
                                                      |      this corpus) -> TileData: heights, holes, layers, alpha,
                                                      |      MCCV, MH2O, MDDF/MODF + MLDX with the proven transforms
                                                      +-- TerrainMeshBuilder (W4) + TerrainShader (W5)
                                                      +-- WmoInstanceManager (W7): shared roots/groups, per-instance
                                                      |      transform, doodad sets, LOD, culling by MODF extents
                                                      +-- DoodadInstanceManager (W8): shared meshes/materials,
                                                      |      instanced static draws, BC textures, MLDX bounds
                                                      +-- Liquids (W9), far LOD (W10), light/sky/fog (W12, after U16)
 protocol 12 (W4): loadWorld / setCamera / cancel / clearWorld / worldStatus -- model and WMO messages unchanged
```

---

## 9. Unknowns and the evidence that settles them (task 8)

| Id | Unknown | Evidence needed |
|---|---|---|
| U1 | MPHD flag bits beyond 0x1 ⇔ WMO-only; which bit (if any) selects 4- vs 8-bit alpha | one tile from a MoP map with `0x248` and one from an Era map with `0x3CA`, to separate client from flag; per-bit A/B in game |
| U2 | *(closed)* pitch/roll signs: proven by MLDX (6.4) | -- |
| U3 | MAIN flag 2 | which tiles carry it (ocean? unloaded?) against map images |
| U4 | MAI2 (listfile name: per-tile liquid flow texture) | read the file; its use needs liquid rendering first |
| U5 | MANM | structure census of the 4 Retail files |
| U6 | WDL `MAOE`, `MAHO`, `MSSN/MSSO/MSSF`, WDL `ML**` | census + comparison with ADT holes/placements |
| U7 | MCNK flag 0x8000 (always set), 0x2 (Era 215, Beta 6) | a file without 0x8000; per-flag render comparison |
| U8 | MCNK offset 96 pointing past the data | none needed for rendering; record only |
| U9 | MCNR decoding and its 13 trailing bytes | compare decoded normals with normals from MCVT |
| U10 | MCLY flags other than 0x100/0x200 (0x1-0x80, 0x400) | per-bit visual comparison |
| U11 | Height-texture blending (MHID, MTXP fields) | tiles with non-zero MHID (Retail 30_49 is one); render comparison |
| U12 | MAMP | census |
| U13 | MDDF 0x40/0x200/0x20, MODF 0x8/0x40/0x80 meanings | records without them; per-flag samples |
| U14 | MH2O vertex formats and liquid rendering | LiquidType/LiquidMaterial/LiquidObject definitions (not bundled), then byte checks |
| U15 | Which WMO doodad sets a placement shows (set 0 + MODF set assumed) | in-game comparison |
| U16 | `_lgt.wdt` (MPL2/MPL3/MSLT/MTEX/MLTA), `_fogs.wdt` (VFOG/VFEX/VFE2), Light/LightParams/LightData/ZoneLight* (T10: layouts differ per client) | definitions for each client's layout + render comparison. No visual equation is derived from field names here |
| U17 | obj1 chunks beyond MLDD/MLDX, `_lod.adt` chunk semantics (incl. Beta MASD/MALP) | census + comparison with obj0 placements and MCVT |
| U18 | MFBO, blend meshes, MCBB, MPTX, MWDR/MWDS, MLMB, MLDB | census + render comparison |
| U19 | 8-bit expansion of 4-bit alpha (Retail's re-encoding is ~16n+15; 17n and 16n are each <0.3%) | visual comparison in W5 against an Era screenshot |
| U20 | `.pd4` (MPHD word 7) contents | read the file |

The tables needed for U14 and U16 have no `.dbd` among the 47 bundled ones, and their layouts differ by client (T10:
LightParams has 32 fields in Retail, 17 in Era and MoP, 39 in Classic Beta). Obtaining definitions is a dependency
(R4), and this audit downloaded nothing.

---

## 10. Validation tools (task 9)

Everything is read-only and separate from the viewer, its build and its installer (`Tools/world-audit/`). The probe is
a stand-alone exe built against a viewer build's CascLib. It never writes to the game folder and is not referenced by
any CMake target.

| Tool | Purpose |
|---|---|
| `casc_extract.cpp` (+ `build_casc_extract.cmd`) | Copies files out of a local client by FileDataID, or by path (`n <path>`); `s <id>` records status only. Manifest states: `OK`, `NOT_IN_ROOT`, `NOT_LOCAL` (decided from one storage enumeration, `index.txt`), `ENCRYPTED_PARTIAL` (zero-filled blocks, marked), `ENCRYPTED`, `READ_ERROR`. `timing.txt` splits storage open, enumeration and read time. |
| `wowfmt/` | Bounds-checked readers: `chunks` (never reads past a container; reports malformed sizes), `db2` (WDC3/4/5, schema-free, cross-section strings), `wdt`, `adt` (split parts, MCNK header, MCLY, RLE alpha, MDDF/MODF, MH2O), `listfile` (lookup aid only) |
| `map_catalog.py`, `area_catalog.py`, `env_tables.py` | Map.db2 columns; AreaTable names (tile labels only); environment table headers (T10) |
| `audit_wdt.py`, `wdt_flags.py`, `maid_content.py` | WDT census; MAIN/MPHD flag counts; MAID roles and field 21 without the listfile |
| `corpus.py`, `select_corpus.py`, `availability.py` | Corpus extraction by MAID; scenario selection; availability census per map and MAID word |
| `audit_adt.py`, `subchunk_sizes.py`, `coords.py`, `alpha_edges.py`, `scale4.py` | Split-file structure; sub-chunk sizes; positions, vertex order, seams, holes, alpha orientation, nibble order; repeated last row/column; 4-bit vs 8-bit values |
| `rotation.py`, `mldx.py` | Placement rotation and scale against MODF extents and MLDX boxes |
| `audit_side.py`, `terrain_needs.py`, `load_pressure.py`, `read_timing.py` | WDL and side files; renderer needs; load sets; read times |
| `pin_corpus.py`, `report_tables.py` | Pins every cited file and file set; regenerates appendix A |
| `tests/test_wowfmt.py` | 17 tests. Malformed chunks (size past end, trailing bytes, empty); WDT with a wrong MAIN size or truncated; DB2 bad magic, truncated header, and a minimal synthetic table with strings; RLE alpha (exact and truncated); root MCNK smaller than its header; MH2O instance past the end. Plus the pins: 243 files and 19 sets against their SHA-256 |
| `tests/test_corpus_rules.py` | 10 tests on the pinned bytes, all four clients unless noted: MAIN bit 1 ⇔ MAID in every MAID-bearing WDT; MAID index; MCNK position and index (33,280 chunks); seam heights (and their failure when transposed); the script verdicts for outer/inner vertex order, seam x/y, placement height, holes (discriminating chunks and continuity) and alpha orientation, and (Retail) placement rotation/scale, each scoring best against its alternatives |

Results on the committed tree:
- With the data: 27 tests pass, in about 3 minutes.
- Without the data: 15 pass and 12 skip with a message.

Reproduce from the repository root (paths as used here). Placeholders:
- `D` = `F:\wowmodelviewer\build\world-audit-data`;
- `G` = `E:\World of Warcraft`;
- `K` = the viewer's `extraEncryptionKeys.csv` copied to `D`;
- `L` = the listfile copied to `D\listfile.csv`;
- `T` = `Tools\world-audit`.

The id lists (`D\ids_*.txt`) hold one FileDataID per line, `n <path>` for a probe by path, or `s <id>` for status only.

```
T\build_casc_extract.cmd <viewer build>\ThirdParty\casclib\Release\casc.lib D\bin
rem per client c in wow, wow_classic_beta, wow_classic_era, wow_classic:
D\bin\casc_extract.exe G c D\db\c K D\ids_mapdb.txt                    (1349477 Map.db2)
D\bin\casc_extract.exe G c D\db\c K D\ids_area.txt                     (1353545 AreaTable.db2)
python -I T\map_catalog.py D\db\c\1349477.bin L D\catalog\maps_c.json
python -I T\area_catalog.py D\db\c\1353545.bin D\catalog\areas_c.json
D\bin\casc_extract.exe G c D\envdb\c K D\ids_envdb.txt                 (the environment tables of T10)
D\bin\casc_extract.exe G c D\wdt\c K D\ids_wdt_c.txt                   (every WDT the catalog names)
python -I T\audit_wdt.py c D\catalog\maps_c.json D\wdt\c L D\catalog\wdt_c.json
python -I T\wdt_flags.py D c D\results\wdt_flags_c.json
D\bin\casc_extract.exe G c D\nametest\c K D\ids_names.txt              (WDTs by path and by id, 4.1)
D\bin\casc_extract.exe G c D\nametest_adt\c K D\ids_adt_name_probe.txt (ADT parts and WDL by path and by id, 4.1)
D\bin\casc_extract.exe G c D\tex1\c K D\ids_tex1.txt                   (_tex1 probe, 5.1)
python -I T\corpus.py D\bin\casc_extract.exe G c K D\wdt\c\775971.bin D\corpus\c\azeroth 29 45 36 51   (Beta/MoP: 31 47 33 49)
python -I T\audit_adt.py D\corpus\c\azeroth L D\results\adt_c.json
python -I T\coords.py D\corpus\c\azeroth D\results\coords_c.json
python -I T\audit_side.py D\corpus\c\azeroth D\results\side_c.json
python -I T\subchunk_sizes.py D\corpus\c\azeroth D\results\subchunk_sizes_c.json
python -I T\alpha_edges.py D\corpus\c\azeroth D\results\alpha_edges_c.json
python -I T\terrain_needs.py D\corpus\c\azeroth D\results\terrain_c.json
D\bin\casc_extract.exe G c D\avail\c K D\ids_avail_s_c.txt             (every MAID word, "s <id>" lines)
python -I T\availability.py D c D\results\avail_c.json
rem Retail / Era only:
python -I T\select_corpus.py D\corpus\c\azeroth D\catalog\areas_c.json D\results\select_c.json
rem Retail only:
D\bin\casc_extract.exe G wow D\wmoonly K D\ids_wmoonly.txt             (the two WMO-only roots of 3.2)
python -I T\load_pressure.py D\bin\casc_extract.exe G wow K D\corpus\wow\azeroth D\results\select_wow.json D\assets\wow D\results\load_wow.json
D\bin\casc_extract.exe G wow D\assets\wow K D\ids_allwmo.txt            (the 109 WMO roots the window's MODFs name)
python -I T\rotation.py D\corpus\wow\azeroth D\assets\wow D\results\rotation_wow.json
python -I T\mldx.py D\corpus\wow\azeroth D\assets\wow D\results\mldx_wow.json      (the 25,648 doodads whose M2 load_pressure.py extracted)
python -I T\read_timing.py D\bin\casc_extract.exe G wow K D\results\load_wow.json D\scratch_timing D\results\read_timing_wow.json 3
python -I T\scale4.py D\corpus\wow_classic_era\azeroth D\corpus\wow\azeroth D\results\scale4.json
rem all clients:
python -I T\maid_content.py D D\results\maid_content.json
python -I T\env_tables.py D L D\results\envdb.json
python -I T\report_tables.py D > D\results\tables.md
python -I T\pin_corpus.py D T\tests\corpus_hashes.json     (only after a deliberate re-extraction)
set WMV_WORLD_AUDIT_CORPUS=D
python -I -m unittest discover -s Tools/world-audit/tests
```

The host's `-m2inspect` probe pattern (H-row "Tests") is the model for a future in-viewer probe. This audit kept its
probes out of the viewer entirely.

---

## 11. Evidence ledger

| Claim | Level | Where |
|---|---|---|
| Map.db2 field 21 = WdtFileDataID (Retail, Beta), by cross-client name hashes 220/0 and 57/0 | PROVEN_BYTES | 3.1 |
| Era/MoP Map.db2 have no WDT FileDataID column; WDT path from the directory | PROVEN_BYTES + PROVEN_RUNTIME | 3.1, 4.1 |
| Retail/Beta open no map file by path (WDT, WDL, ADT root/tex0/obj0 tested); Era/MoP open them with the same bytes | PROVEN_RUNTIME | 4.1 |
| `NOT_IN_ROOT` vs `NOT_LOCAL` per MAID word, all clients | PROVEN_RUNTIME | 3.3, T11 |
| WDT forms; no WDT with both tiles and MODF | PROVEN_BYTES | 4.2 |
| MAIN flag 1 ⇔ MAID names files; flag 2 ⇒ none | PROVEN_BYTES (test) | 4.2 |
| MAID word roles (by content) and index `y*64+x` | PROVEN_BYTES (test for the index) | 4.2 |
| MPHD words 1-6 order (Azeroth's side files read; MVER-only files identified by version) | PROVEN_BYTES | 4.2, T8 |
| MPHD word 7 = `.pd4`; MAI2 word 0 = liquid-flow image | listfile name only | 4.2 |
| MPHD bit 0x1 ⇔ WMO-only | PROVEN_BYTES | 4.2 |
| MPHD bits → alpha encoding | three-point correlation only (U1) | 4.2 |
| WDT MODF rotation applies (DireMaul yaw 180) | PROVEN_BYTES | 4.3 |
| Old-form MODF scale 0 = 1.0 | INFERRED | 4.3 |
| WDL 17x17 = chunk-corner heights; 16x16 = truncated chunk means | PROVEN_BYTES | 4.4 |
| Split-file chunk placement; root n_layers and alpha offsets always 0 | PROVEN_BYTES | 5.1 |
| `_tex1.adt` absent in all four clients | PROVEN_RUNTIME | 5.1 |
| MCNK header offsets in 5.2 | PROVEN_BYTES | 5.2 |
| Bit 0x40 ⇔ MCCV, bit 0x1 ⇔ MCSH | PROVEN_BYTES (correlation, 100%) | 5.2 |
| High-resolution holes everywhere; bit order | PROVEN_BYTES (test) | 5.4, 6.5 |
| Alpha spacing rule; RLE; orientation; 4-bit low nibble first; Era repeats row/column 63 | PROVEN_BYTES (test for orientation) | 5.4 |
| MCLY 0x100 = has alpha | correlation, 100% | 5.4 |
| 4-bit → 8-bit expansion | UNKNOWN (U19) | 5.4 |
| Placement names are FileDataIDs; records repeat per tile (dedup by uniqueId) | PROVEN_BYTES | 5.6 |
| MLDD = MDDF copy; MLDX = world box | PROVEN_BYTES | 5.6, 6.4 |
| Tile/chunk/vertex positions; seam heights exact; x/y from indices | PROVEN_BYTES (test) | 6.1, 6.2 |
| Placement position → world; doodads on the fan surface | PROVEN_BYTES (test) | 6.3 |
| Rotation `Rz(a1+180)·Ry(a0)·Rx(a2)`, signs, proper, scale u16/1024 (MDDF) | PROVEN_BYTES (test) | 6.4 |
| MODF extents exact world bounds | PROVEN_BYTES | 6.4 |
| World → Unity mapping and winding | INFERRED (G4) | 6.6 |
| Floating origin | INFERRED, recommended | 6.6 |
| Load counts and bytes; RGBA32 bytes from BLP sizes | PROVEN_BYTES | 7.1 |
| CascLib read times (warm cache) | PROVEN_RUNTIME | 7.2 |
| GPU geometry bytes | INFERRED | 7.1 |
| HEAD inventory (2.1-2.3) | PROVEN_SOURCE | 2 |
| Renderer needs (inner vertices, 257² fan error, layer sets, MCCV) | PROVEN_BYTES | 8 |
| Unity Terrain's limits | INFERRED (documented model, not tested) | 8 |
| Other flag bits, side files, lod/obj1 beyond MLDD/MLDX, liquids' vertex formats, lighting | UNKNOWN | 9 |

---

## 12. Risk register

| Id | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | Host GUI thread stalls under world request volume (2.3, 7.2) | certain / high | W3: one storage-owning thread with a budget before any multi-tile view |
| R2 | Player single-object design forces a rewrite of shared paths | certain / high | New WorldSession beside the model/WMO paths; do not route doodads through `loadWoWModel` |
| R3 | GPU memory blow-up (RGBA32 is 5.7x the stored textures; no budget) | certain / high | BC upload, residency budget, G6 measurement |
| R4 | No definitions for Light/Liquid tables; layouts differ per client | certain / medium | Schema-free reads where the bytes prove columns (as done for Map/AreaTable); obtain definitions only with the owner's consent |
| R5 | World → Unity composition wrong (handedness/winding) | possible / medium | G4: top-down render of 32_50 against its map texture and minimap; tilted WMO and doodad against MODF extents/MLDX in Unity space |
| R6 | Old WMO-only WDTs (MWMO paths) unresolvable in Retail without the listfile | certain / low | Treat as unsupported, or resolve through the listfile with a clear notice |
| R7 | Encrypted content (Beta/PTR) | certain / low | `ENCRYPTED_PARTIAL` is its own state, never data |
| R8 | Client updates change files under the corpus | likely / low | The pinned-hash tests fail loudly; re-extract and re-run the audit tools, then re-pin deliberately |
| R9 | Classic Era differs (4-bit alpha, lod/map images on only 182 of 2,719 tiles, empty fog/light files) | certain / medium | Per-client capability flags in W2; Era as a first-class test client |
| R10 | Float precision at world coordinates | likely / medium | Floating origin, subtracted in double (6.6) |
| R11 | Parts missing per tile (`NOT_IN_ROOT` minimaps; `NOT_LOCAL` files incl. a root ADT in Beta) | certain / medium | Load per part, report the state per part; never treat an empty read as data (3.3) |
| R12 | Scope creep into lighting/sky before its data is understood | possible / medium | W12 last, behind U16 |
| R13 | Host-side DB reader limits (strings from section 0 only; path-only table opening) | certain / low | Read Map/AreaTable in the player (W1/W2) with the proven schema-free reader, or fix the host reader with tests first |
| R14 | 4-bit alpha expansion wrong by up to 15/255 | possible / low | U19 visual check in W5 |

## 13. Stage entry and exit gates (the next twelve branches)

| Branch | Content | Entry gate (must hold before it starts) | Exit gate (must hold to merge) |
|---|---|---|---|
| W1 `world/01-parsers` | Pure C# WDT/ADT/DB2 readers in the player project + tests | this audit merged; `test_corpus_rules` passes on the pinned data | C# readers reproduce T2, T3, T6 and the 6.1/6.2/6.4/6.5 verdicts on the corpus; malformed-input tests as in `test_wowfmt` |
| W2 `world/02-catalog` | Map/AreaTable catalog per client (field 21 or directory rule), capability flags (WDTs, 4/8-bit alpha by spacing, lod/map images), availability states | W1 merged | catalog lists every readable map of all four clients with T1's counts; `NOT_IN_ROOT`/`NOT_LOCAL` reported per part (T11) |
| W3 `world/03-host-serving` | Asset serving on a storage-owning thread, per-poll budget, request log volume; no protocol change | W1 merged; G3: today's host throughput for the city set (5,322 files) through `-unityipctest`-style harness recorded | same harness: GUI thread never blocked > 50 ms while serving the city set; throughput ≥ the G3 baseline; existing model/WMO tests pass |
| W4 `world/04-protocol-one-tile` | Protocol 12 (`loadWorld`, `setCamera`, `cancel`, `clearWorld`, `worldStatus`), world scene root beside the model/WMO slots, one tile's untextured terrain (heights, holes, normals, MCCV), dev-only entry | W2, W3 merged | **G4**: headless top-down render of 32_50 matches its map texture/minimap orientation; seams of 2 tiles closed; Unity-space transform of a tilted WMO and doodad matches MODF extents/MLDX; older players get the out-of-date notice; lifecycle tests mode-aware |
| W5 `world/05-terrain-texturing` | Texture array, layers, alpha (4/8-bit, RLE), MCSH | W4 merged | alpha orientation visually confirmed; U19 decided by comparison; GPU bytes per tile recorded (G6 part 1) |
| W6 `world/06-streaming` | WorldSession residency, prioritised cancellable scheduler, BC upload, budgets | W3, W5 merged | **G6**: 3x3 city within a set GPU budget; upload and frame times recorded beside T12 |
| W7 `world/07-wmo-placement` | MODF instances with the proven transform, shared WMO data, doodad sets, culling by MODF extents | W6 merged | WMO-only map 34 and 30_47's WMOs placed to their extents; DireMaul WDT yaw applied |
| W8 `world/08-doodads` | MDDF instances, shared meshes, instanced static path, MLDX bounds | W7 merged | 13,512-instance city within the G6 budget |
| W9 `world/09-liquids` | MH2O | U14 settled (definitions + byte checks) | water tiles 29_48 (Retail) and 36_49 (Era) render against references |
| W10 `world/10-far-lod` | `_lod.adt`, WDL (16x16 as chunk means), WMO further-LOD groups | U17 settled | far terrain matches WDL within its 1-yard truncation |
| W11 `world/11-maps-mode` | Maps viewer mode in the host (every mode site, 2.2) | W2, W6 merged | mode gating and Browse follow the existing mode tests; no model/WMO regression |
| W12 `world/12-light-sky-fog` | Light DBs, `_lgt`, `_fogs`, sky | U16 settled with definitions and render comparisons | per-client comparison against in-game references |

## 14. Performance baseline

| Measured now | Value | Source |
|---|---|---|
| Requests / bytes per set (one tile, 3x3, city) | 381 / 12.4 MB; 2,393 / 93.8 MB; 5,322 / 445.6 MB | T9 |
| CascLib read time per set, warm, one thread | 92-145 ms; 652-806 ms; 2.07-3.53 s | T12 |
| Storage open + enumeration (Retail) | 2.3-2.4 s + 0.33-0.34 s | T12 |
| Texture bytes stored vs today's RGBA32 upload (city) | 323.0 MB vs 1,833.5 MB | T9 |
| Instances (city) | 13,512 M2 of 1,179; 53 WMO, 442 groups | T9 |

Not measured, because no world renderer exists: IPC serving time through today's host (W3's entry gate G3), parse,
decode and upload times, frame times and GPU memory. Gate G6 records them on W5/W6 builds with the same three sets,
using `Profiler.GetAllocatedMemoryForGraphicsDriver` as `WmvScreenshot` already does.

## 15. Next branch

**`world/01-parsers`**:
- **Scope.** Pure C# readers (no UnityEngine) in `Tools/UnityRendererProject/Assets/Scripts/Wow/`:
  - WDT: MPHD, MAIN, MAID, MODF, MWMO;
  - split ADT: root MCNK header + MCVT/MCNR/MCCV/MCSE, tex0 MDID/MHID/MCLY/MCAL with RLE, 4/8-bit by spacing,
    obj0 MDDF/MODF, obj1 MLDD/MLDX, MH2O headers;
  - a schema-free DB2 reader sufficient for Map.db2 and AreaTable.db2.
- **Rules.** Each rule exactly as proven here, with the section number in a comment:
  - vertex x/y from integer indices in double;
  - decode alpha by 0x100 and assert it matches the layer index;
  - deduplicate placements by uniqueId;
  - the rotation of 6.4.
- **Tests.** In `WowParserTests.cs` style (synthetic malformed input), plus a corpus test that reads this audit's
  pinned files when `WMV_WORLD_AUDIT_CORPUS` is set.
- **Out of scope.** No protocol change, no rendering, no UI, no host change.
- **Done when** the C# readers reproduce T2, T3, T6 and the `test_corpus_rules` verdicts on the corpus.

---

## 16. Adversarial review

### 16.1 Method

Four independent reviewers each took one area of the first draft and tried to refute its claims with their own
probes on the same extracted bytes:
1. the coordinate transform, holes included;
2. alpha maps and layers;
3. placement mappings;
4. the MAID structure and WDT rules.

A second set of agents verified each challenge before it was accepted. A completeness critic then listed what the
report still left unproven or unmeasured. Their scripts and findings are kept with the audit data
(`build\world-audit-data\review\`).

### 16.2 What was refuted and how it was fixed

| Area | Draft claim | Finding | Now |
|---|---|---|---|
| Placement (high) | Yaw `Rz(−a1)`; "only 89 of 268 match their stored box"; pitch/roll signs "undecidable"; "MODF extents are often stale" (R11) | The candidate set had no yaw offset, so the true convention could not win. With `Rz(a1+180°)`, the stored extents are exact and the signs are decided | `rotation.py` tests 2,880 conventions incl. offsets and mirrors: 201/202 (6.4). MLDX test proves signs and scale. The draft's R11 "stale extents" deleted (R11 is now "parts missing per tile"); U2 closed |
| Placement (medium) | Counts per MDDF/MODF entry | Records repeat in every tile they overlap | Dedup by uniqueId in all tools: city M2 instances fell from 22,446 to 13,512. Counting LOD0 groups only (completeness item 5) split the draft's 926 WMO groups into 442 loaded + 484 further-LOD; requests fell from 5,806 to 5,322 (7.1) |
| Placement (medium) | "62 placements reproduce their box with pure rotation (det +1)" | Number had no source; improper frames were never tested | Mirrors in the candidate set: 201 proper vs 72 best improper |
| Coordinates (medium) | Doodads "0.2 yd above the ground" | Nearest-vertex sampling, unsigned | Exact fan surface, signed: median \|Δz\| 0.021 yd, slightly more sunk than raised (6.3) |
| Coordinates (medium) | "Tile seams are exact" | Only heights were tested; x/y from stored float32 positions are not bit-equal at any seam | 6.2 now: heights exact; x/y from integer indices in double; x/y exactness measured |
| Coordinates (low) | Inner-vertex rule cited by the seam test | Inner vertices are never shared | Corner-mean test added: 0.12 yd vs ≥1.3 yd (6.2) |
| Coordinates (low) | Weak alternatives (axis-swapped forms score 0 by construction) | Not close competitors | All 8 symmetries tested for vertices, holes and alpha |
| Coordinates (low) | Floating origin "required", R10 "certain" | Not shown | "Recommended", reason and W4 measurement stated; R10 "likely" |
| Holes | 3 bit orders tested; "every hole cell inside a WMO" | Base rate 92-93% makes that weak | Discriminating-chunk and cross-chunk continuity tests over 9 orders, both asserted in tests for all four clients (6.5) |
| Alpha (medium) | 4-bit "low nibble first, scaled by 17" | Nibble order untested by the cited test; no scale measurement, and Retail's re-encoding contradicts 17n | Column-edge nibble test (low first proven); scale UNKNOWN (U19) with the 16n+15 measurement |
| Alpha (medium) | MPHD bits select the encoding | Three data points; bit 0x2 fits as well | Spacing rule is the proven rule; MPHD a correlation (U1) |
| Alpha (medium) | MCLY 0x100 = has alpha map | Decoder used layer position, 0x100 coincides 100% | Labelled a correlation; W1 decodes by 0x100 and asserts agreement |
| Alpha (low) | "Raw layers tile MCAL exactly"; "orientation vs transposed only"; "root n_layers never equals MCLY count"; 0x400 missing | Structural or trivial | Reworded (5.4); all 8 symmetries on both edges; root fields stated as always 0; 0x400 in U10; Era's repeated row/column 63 measured |
| MAID/WDT (medium) | MAID order "proven" by listfile names | Circular: the listfile names files by MAID position | Content-based roles (MHDR/MDDF/MLDD/MAMP/MLHD/BLP2) and MCNK positions against the index |
| MAID/WDT (medium) | Field 21 proven by the listfile | Circular | Cross-client name-hash agreement 220/0 and 57/0 |
| MAID/WDT (low) | Era/MoP "have the column but it is zero" | Column picked by sort order with 0 matches | "No column identified"; `map_catalog.py` reports it |
| MAID/WDT (low) | Mismatch rows listed wrongly; "every WDT carries MAID"; flag 2 stated both ways; "6 bits"; WDL 16x16 "heights" | Wrong or overstated | Corrected in 3.1, 4.2, 4.4 with `wdt_flags.py` counts; 16x16 measured as truncated means |
| MAID/WDT (low) | Path rule from 2 WDTs | Few probes, WDTs only | WDL and ADT root/tex0/obj0 probed by path in all four clients (4.1) |
| MAID/WDT (low) | WMO-only facts for W7 missing | -- | WDT MODF rotation (DireMaul yaw 180) and old-form scale 0 recorded (4.3) |

### 16.3 What survived unchanged

- The MCNK position formula (not circular: index from MAID, chunk indices from the header, position from the bytes).
- The outer vertex order (all mirrored variants fail).
- The placement position mapping.
- The hole bit order (now with stronger tests).
- The RLE rule.
- The MAID word order (now proven from content).
- The MAIN flag 1 ⇔ MAID rule.
- The split-file structure.
- The WDL 17x17 orientation.

### 16.4 Completeness critic: items and status

| # | Gap named | Status |
|---|---|---|
| 1 | HEAD/status block at commit | in the commit message and the changes summary; this report records the base (top) |
| 2 | `NOT_LOCAL` could never be reported | fixed (storage enumeration) and measured: 11 Beta files (3.3) |
| 3 | Modern terrain material tile missing from the scenarios | 30_49 added (3.2, T4, T5) |
| 4 | `_tex1` not probed | probed by path and by id: absent in all four clients (5.1) |
| 5 | Load counted all LODs, no M2 dependencies, no GPU split | LOD0 only, further LODs listed, SKID/AFID/BFID counted (0), geometry vs textures separated (7.1) |
| 6 | RGBA32 upload cost only estimated | computed from every BLP's size: 1.83 GB, ×5.68 (7.1) |
| 7 | Gates without exit criteria | entry and exit per branch (13) |
| 8 | Not every counted file set pinned | 243 files + 19 set digests, tested (10) |
| 9 | Era rule picks absent from T4 | added (T4) |
| 10 | No format matrix by client; Beta unmeasured | T0; Beta window measured throughout |
| 11 | Unknowns incomplete | U7, U10, U17-U20 extended or added |
| 12 | Unity Terrain claims untested | version cited, labelled INFERRED; 257² fan error measured (8) |
| 13 | Lifecycle and ModelCanvas rows | added to 2.3 |
| 14 | No throughput baseline | CascLib read times (7.2, T12) |
| 15 | Tests lacked real-byte assertions | `test_corpus_rules.py`, 10 tests; reproduce steps listed, including every probe and extraction a result depends on (10) |
| 16 | Branch assignments inconsistent | W3 host serving, W4 protocol 12 incl. cancel; diagram and gates agree |
| 17 | Unmeasured statements | labelled INFERRED, or measured (MCNR 448 B, sub-chunk sizes) |
| 18 | Data outside the working folder | moved to `F:\wowmodelviewer\build\world-audit-data` (git-ignored) |

### 16.5 Verification of this report

Four further checkers compared every number and claim of this rewrite with the result files, the appendix and the
cited source lines. They raised 61 findings, all applied:
- **Stale Era water pick.** The stored Era water pick (29_49) failed its own ≥100-doodad rule. The current
  `select_corpus.py` picks 36_49 (Lake Everstill), and the corpus pins were redone with it.
- **Two wrong citations.** Two path:line citations in 2.2/2.3 were corrected (`WoWFolder.cpp:67-91`,
  `GameFile.cpp:125-156`).
- **Claims scoped to what was measured**: per-client figures, listfile-only identities, and the test coverage, which
  now covers all four clients.
- **Missing tools.** `area_catalog.py` and `env_tables.py` were added, so that every result has a producing tool.

### 16.6 Limits of the review

The reviewers ran on the same extracted bytes and the same client builds. A client update or a different region's
files are covered only by the pinned-hash tests failing. Visual confirmation of the World → Unity composition (6.6),
the 4-bit expansion (U19) and every UNKNOWN in section 9 is deliberately left to the gates of section 13; no render was
made in this branch.

---

## Appendix A. Generated tables

Generated by `Tools/world-audit/report_tables.py` from the result files.

### T0. Format matrix by client

Map 0 "Azeroth" unless the row says otherwise. Window = the tiles extracted for that client (Retail and Classic Era 56 tiles x 29-36, y 45-51; Classic Beta and MoP Classic 9 tiles x 31-33, y 47-49).

| | Retail 12.1.0.69933 | Classic Beta 1.60.1.70338 | Classic Era 1.15.9.70003 | MoP Classic 5.5.4.70268 |
|---|---|---|---|---|
| Map.db2 WDT FileDataID column | field 21 (1073 rows agree) | field 21 (71 rows agree) | none (no column matches) | none (no column matches) |
| Open WDT by path | no (root lists no names) | no (root lists no names) | yes, same bytes as by FileDataID (58/58) | yes, same bytes as by FileDataID (227/227) |
| WDTs read / with MAID / old MWMO form | 969 / 763 / 193 | 69 / 69 / 0 | 58 / 58 / 0 | 227 / 227 / 0 |
| MAID tiles; words 4-6 (lod, map textures) zero | 44,018; 9,082 | 3,322; 777 | 2,719; 2,537 | 9,848; 7,310 |
| Azeroth MPHD flags | 0x3CA | 0x3CA | 0x248 | 0x24E |
| Window tiles / MCNKs | 56 / 14,336 | 9 / 2,304 | 56 / 14,336 | 9 / 2,304 |
| `_lod.adt` in MAID (window tiles) | 56 | 9 | 0 | 0 |
| `_tex1.adt` (5 scenario tiles, by path and by listfile FileDataID) | absent (NOT_IN_ROOT 10) | absent (NOT_IN_ROOT 10) | absent (NOT_IN_ROOT 10) | absent (NOT_IN_ROOT 10) |
| MCAL per chunk (chunks with alpha) | rle 13,109, raw4096+rle 689, raw4096 521 | rle 2,179, raw4096+rle 28, raw4096 4 | raw2048 11,167 | rle 2,262, raw4096+rle 30, raw4096 11 |
| MCSH chunks | 0 | 0 | 6,976 | 1,627 |
| MCCV chunks | 2,412 | 2,304 | 0 | 299 |
| MCBB / MPTX chunks | 9 / 0 | 0 / 5 | 0 / 0 | 0 / 0 |
| Tiles with MH2O / MFBO / blend mesh (MBMH) | 44 / 56 / 2 | 8 / 0 / 0 | 35 / 0 / 0 | 8 / 9 / 0 |
| Tiles with MTXP / MLMB (obj0) / MLDB (obj1) | 1 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 1 / 7 / 8 |
| `_lod.adt` chunk ids seen | MBBB, MBMB, MBMH, MBMI, MBNV, MLHD, MLLD, MLLI, MLLL, MLLN, MLLV, MLND, MLSI, MLVH, MLVI, MVER | MALP, MASD, MLHD, MLLD, MLLL, MLND, MLSI, MLVH, MLVI, MVER | - | - |
| Chunks with holes (high-resolution mask) | 81 | 11 | 80 | 16 |
| `_lgt.wdt` | 685,548 B: MVER, MPL3, MSLT, MTEX, MLTA | 439,512 B: MVER, MPL3, MSLT, MLTA | 12 B: MVER | 128,884 B: MVER, MPL2, MSLT |
| `_fogs.wdt` | 59,604 B: MVER, VFOG, VFEXx152, VFE2x152 | 96,452 B: MVER, VFOG, VFEXx246, VFE2x246 | 12 B: MVER | 12 B: MVER |
| `_mpv.wdt` | 107,980 B: MVER, PVMIx19, PVPDx19, PVBDx19 | 12 B: MVER | 12 B: MVER | 12 B: MVER |
| `_occ.wdt` | 693,868 B: MVER, MAOI, MAOH | 434,268 B: MVER, MAOI, MAOH | 405,358 B: MVER, MAOI, MAOH | 495,038 B: MVER, MAOI, MAOH |
| `.tex` | 14,037,432 B: TXVR, TXBT, TXMDx14171 | 5,486,716 B: TXVR, TXBT, TXMDx6464 | 2,907,588 B: TXVR, TXBT, TXMDx3730 | 5,971,872 B: TXVR, TXBT, TXMDx6831 |
| WDL tiles with heights (MARE) | 1,176 | 736 | 687 | 839 |
| Files MAID names: not listed by the root / listed but not on this computer | 695 / 0 | 0 / 11 | 64 / 0 | 632 / 0 |

### T1. Clients and Map.db2

| Product | Client | Build | Map.db2 schema | Layout | Rows | WDT FileDataID column | WDTs read | Not read |
|---|---|---|---|---|---|---|---|---|
| wow | Retail | 12.1.0.69933 | WOWSTATIC_12_1_0_68914 | 75863E23 | 1184 | field 21: 1073 rows match the listfile WDT, 6 mismatch, 105 zero | 969 | NO_FDID 105, NOT_IN_ROOT 110 |
| wow_classic_beta | Classic Beta | 1.60.1.70338 | wowstatic_camelot_1_60_1 | D43AFAC3 | 79 (8 key-locked) | field 21: 71 rows match the listfile WDT, 0 mismatch, 0 zero | 69 | NOT_IN_ROOT 2 |
| wow_classic_era | Classic Era | 1.15.9.70003 | WOWSTATIC_1_15_9_68185 | B47C6F05 | 59 | no column identified (best candidate matches 0 rows) | 58 | NOT_IN_ROOT 1 |
| wow_classic | MoP Classic | 5.5.4.70268 | WOWSTATIC_5_5_4_67732 | B47C6F05 | 285 | no column identified (best candidate matches 0 rows) | 227 | NOT_IN_ROOT 7, NO_FDID 51 |

### T2. WDT structure census (every WDT each client has)

| Client | Chunk orders (count) | MPHD flag words seen (count) |
|---|---|---|
| Retail | MVER MPHD MAIN MAID (721); MVER MPHD MAIN MWMO MODF (193); MVER MPHD MAIN MAID MODF (35); MVER MPHD MAIN (13); MVER MPHD MAIN MAID MANM (4); MVER MPHD MAIN MAID MAI2 (3) | 0x3CA (423), 0x41 (192), 0x2CA (117), 0x24E (98), 0x34E (40), 0x2C8 (35), 0x249 (30), 0xDA (12), 0x24C (11), 0x241 (4), 0x3C8 (3), 0x36E (1), 0x49 (1), 0x4E (1), 0x3C1 (1) |
| Classic Beta | MVER MPHD MAIN MAID (44); MVER MPHD MAIN MAID MODF (21); MVER MPHD MAIN MAID MAI2 (4) | 0x3CA (24), 0x241 (21), 0x248 (17), 0x24A (3), 0x24C (2), 0x3C8 (1), 0x2CA (1) |
| Classic Era | MVER MPHD MAIN MAID (36); MVER MPHD MAIN MAID MODF (21); MVER MPHD MAIN MAID MAI2 (1) | 0x248 (23), 0x241 (21), 0x3CA (9), 0x24C (2), 0x2CA (2), 0x3C8 (1) |
| MoP Classic | MVER MPHD MAIN MAID (194); MVER MPHD MAIN MAID MODF (33) | 0x24E (80), 0x3CA (60), 0x248 (30), 0x249 (25), 0x34E (14), 0x241 (8), 0x24C (7), 0x26E (1), 0x3C8 (1), 0x2CA (1) |

### T3. MAID words, named through the listfile (all tiles of all WDTs)

| Word | Retail | Classic Beta | Classic Era | MoP Classic |
|---|---|---|---|---|
| 0 | adt_root 44018; tile in name = (i%64, i//64): 44018 | adt_root 3322; tile in name = (i%64, i//64): 3322 | adt_root 2719; tile in name = (i%64, i//64): 2719 | adt_root 9848; tile in name = (i%64, i//64): 9848 |
| 1 | adt_obj0 44018; tile in name = (i%64, i//64): 44018 | adt_obj0 3322; tile in name = (i%64, i//64): 3322 | adt_obj0 2719; tile in name = (i%64, i//64): 2719 | adt_obj0 9848; tile in name = (i%64, i//64): 9848 |
| 2 | adt_obj1 44018; tile in name = (i%64, i//64): 44018 | adt_obj1 3322; tile in name = (i%64, i//64): 3322 | adt_obj1 2719; tile in name = (i%64, i//64): 2719 | adt_obj1 9848; tile in name = (i%64, i//64): 9848 |
| 3 | adt_tex0 44018; tile in name = (i%64, i//64): 44018 | adt_tex0 3322; tile in name = (i%64, i//64): 3322 | adt_tex0 2719; tile in name = (i%64, i//64): 2719 | adt_tex0 9848; tile in name = (i%64, i//64): 9848 |
| 4 | adt_lod 34936, zero 9082; tile in name = (i%64, i//64): 34936 | adt_lod 2545, zero 777; tile in name = (i%64, i//64): 2545 | zero 2537, adt_lod 182; tile in name = (i%64, i//64): 182 | zero 7310, adt_lod 2538; tile in name = (i%64, i//64): 2538 |
| 5 | maptexture 34936, zero 9082; tile in name = (i%64, i//64): 34936 | maptexture 2545, zero 777; tile in name = (i%64, i//64): 2545 | zero 2537, maptexture 182; tile in name = (i%64, i//64): 182 | zero 7310, maptexture 2538; tile in name = (i%64, i//64): 2538 |
| 6 | maptexture_n 34903, zero 9082, other.blp 33; tile in name = (i%64, i//64): 34903 | maptexture_n 2214, zero 777, other.blp 331; tile in name = (i%64, i//64): 2214 | zero 2537, maptexture_n 182; tile in name = (i%64, i//64): 182 | zero 7310, maptexture_n 2538; tile in name = (i%64, i//64): 2538 |
| 7 | minimap 43947, other.blp 71; tile in name = (i%64, i//64): 43947 | minimap 3322; tile in name = (i%64, i//64): 3322 | minimap 2719; tile in name = (i%64, i//64): 2719 | minimap 9848; tile in name = (i%64, i//64): 9848 |

The same words identified without the listfile, from the bytes of each extracted file (window tiles):

| Word | wow | wow_classic_beta | wow_classic_era | wow_classic |
|---|---|---|---|---|
| 0 | root (has MHDR) 56 | root (has MHDR) 9 | root (has MHDR) 56 | root (has MHDR) 9 |
| 1 | obj0 (has MDDF) 56 | obj0 (has MDDF) 9 | obj0 (has MDDF) 56 | obj0 (has MDDF) 9 |
| 2 | obj1 (has MLDD) 56 | obj1 (has MLDD) 9 | obj1 (has MLDD) 56 | obj1 (has MLDD) 9 |
| 3 | tex0 (has MAMP) 56 | tex0 (has MAMP) 9 | tex0 (has MAMP) 56 | tex0 (has MAMP) 9 |
| 4 | lod (has MLHD) 56 | lod (has MLHD) 9 | (zero) 56 | (zero) 9 |
| 5 | BLP2 512x512 compression 2 alpha type 0 56 | BLP2 512x512 compression 2 alpha type 0 9 | (zero) 56 | (zero) 9 |
| 6 | BLP2 256x256 compression 2 alpha type 0 56 | BLP2 256x256 compression 2 alpha type 0 9 | (zero) 56 | (zero) 9 |
| 7 | BLP2 512x512 compression 2 alpha type 0 56 | BLP2 512x512 compression 2 alpha type 0 9 | BLP2 256x256 compression 2 alpha type 0 56 | BLP2 256x256 compression 2 alpha type 0 9 |

MPHD words 1..7, named through the listfile:

| Word | Retail | Classic Beta | Classic Era | MoP Classic |
|---|---|---|---|---|
| 1 | aux_lgt.wdt 763 | aux_lgt.wdt 69 | aux_lgt.wdt 58 | aux_lgt.wdt 227 |
| 2 | aux_occ.wdt 763 | aux_occ.wdt 69 | aux_occ.wdt 58 | aux_occ.wdt 227 |
| 3 | aux_fogs.wdt 763 | aux_fogs.wdt 69 | aux_fogs.wdt 58 | aux_fogs.wdt 227 |
| 4 | aux_mpv.wdt 763 | aux_mpv.wdt 69 | aux_mpv.wdt 58 | aux_mpv.wdt 227 |
| 5 | other.tex 763 | other.tex 69 | other.tex 58 | other.tex 227 |
| 6 | aux.wdl 763 | aux.wdl 69 | aux.wdl 58 | aux.wdl 227 |
| 7 | other.pd4 35 | other.pd4 16, unnamed 5 | other.pd4 19, unnamed 2 | other.pd4 33 |

### T4. Corpus: the scenario tiles (map 0 "Azeroth", Eastern Kingdoms)

FileDataIDs of each tile's MAID words; every one of these files is pinned by SHA-256 in `Tools/world-audit/tests/corpus_hashes.json`. "-" = MAID word zero.

| Client | Tile | Scenario | Main area | root | obj0 | obj1 | tex0 | lod | map texture | map texture _n | minimap | root bytes | root SHA-256 (first 12) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Retail | 29_48 | water | Stormwind Harbor | 777427 | 777428 | 777429 | 777430 | 1287878 | 1293727 | 1293728 | 204373 | 371,984 | 956ede6e97d1 |
| Retail | 30_47 | dense_structures | Stormwind Harbor | 777622 | 777623 | 777624 | 777625 | 1287837 | 1293731 | 1293732 | 204412 | 442,545 | 2c9c7b288455 |
| Retail | 30_49 | modern terrain material (MHID, MTXP) | Stormwind City | 777632 | 777633 | 777634 | 777635 | 1287913 | 1293863 | 1293864 | 204414 | 344,617 | d8d709484920 |
| Retail | 32_50 | ordinary_outdoor | Elwynn Forest | 778037 | 778038 | 778039 | 778040 | 1287952 | 1293881 | 1293882 | 204495 | 329,922 | db73475ed0f7 |
| Retail | 34_46 | holes_layers | Blackrock Mountain | 778417 | 778418 | 778419 | 778420 | 1287804 | 1293753 | 1293754 | 204571 | 433,544 | 675050b43840 |
| Classic Beta | 31_47 | window | Elwynn Forest | 777822 | 777823 | 777824 | 777825 | 1287839 | 1293737 | 1293738 | 204452 | 470,584 | 8226403c2b7e |
| Classic Beta | 31_48 | window | Elwynn Forest | 777827 | 777828 | 777829 | 777830 | 1287882 | 1293739 | 1293740 | 204453 | 469,073 | 0ad1c8deea07 |
| Classic Beta | 31_49 | window | Elwynn Forest | 777832 | 777833 | 777834 | 777835 | 1287916 | 1293871 | 1293872 | 204454 | 473,190 | 8d4b5e5e1168 |
| Classic Beta | 32_47 | window | Elwynn Forest | 778022 | 778023 | 778024 | 778025 | 1287842 | 1293743 | 1293744 | 204492 | 488,131 | 8f3f9b504164 |
| Classic Beta | 32_48 | window | Northshire Valley | 778027 | 778028 | 778029 | 778030 | 1287884 | 1293745 | 1293746 | 204493 | 477,275 | d009dbc64a8f |
| Classic Beta | 32_49 | window | Crystal Lake | 778032 | 778033 | 778034 | 778035 | 1287918 | 1293879 | 1293880 | 204494 | 491,723 | 86cce4ed10c8 |
| Classic Beta | 33_47 | window | Draco'dar | 778222 | 778223 | 778224 | 778225 | 1287844 | 1293749 | 1293750 | 204532 | 454,740 | 93a5a8db557d |
| Classic Beta | 33_48 | window | Stone Cairn Lake | 778227 | 778228 | 778229 | 778230 | 1287886 | 1293751 | 1293752 | 204533 | 468,628 | 656ebad94c82 |
| Classic Beta | 33_49 | window | Jasperlode Mine | 778232 | 778233 | 778234 | 778235 | 1287920 | 1293887 | 1293888 | 204534 | 475,868 | 19fe3c38a1cd |
| Classic Era | 29_48 | water (Retail pick) | The Great Sea | 777427 | 777428 | 777429 | 777430 | - | - | - | 204373 | 316,071 | c196e6c6e4be |
| Classic Era | 30_47 | dense_structures (Retail pick) | Stormwind City | 777622 | 777623 | 777624 | 777625 | - | - | - | 204412 | 304,212 | d7f2350312d5 |
| Classic Era | 30_48 | holes_layers (Era rule pick) | Stormwind City | 777627 | 777628 | 777629 | 777630 | - | - | - | 204413 | 310,908 | 2ea1906b2a89 |
| Classic Era | 30_49 | modern terrain material (MHID, MTXP) (Retail pick) | Stormwind City | 777632 | 777633 | 777634 | 777635 | - | - | - | 204414 | 324,311 | f42eb6db52e1 |
| Classic Era | 31_48 | ordinary_outdoor (Era rule pick) | Elwynn Forest | 777827 | 777828 | 777829 | 777830 | - | - | - | 204453 | 318,545 | dd3f64dc162b |
| Classic Era | 32_50 | ordinary_outdoor (Retail pick) | Elwynn Forest | 778037 | 778038 | 778039 | 778040 | - | - | - | 204495 | 329,878 | cd27f188c894 |
| Classic Era | 34_46 | holes_layers (Retail pick) | Blackrock Mountain | 778417 | 778418 | 778419 | 778420 | - | - | - | 204571 | 346,908 | 3de953c335fd |
| Classic Era | 34_51 | dense_structures (Era rule pick) | Duskwood | 778442 | 778443 | 778444 | 778445 | - | - | - | 204576 | 310,463 | 10441a7fb851 |
| Classic Era | 36_49 | water (Era rule pick) | Lake Everstill | 778837 | 778838 | 778839 | 778840 | - | - | - | 204656 | 366,432 | df0ed6f5eaa8 |
| MoP Classic | 31_47 | window | Stormwind City | 777822 | 777823 | 777824 | 777825 | - | - | - | 204452 | 412,371 | de14fbe64afc |
| MoP Classic | 31_48 | window | Elwynn Forest | 777827 | 777828 | 777829 | 777830 | - | - | - | 204453 | 375,707 | 2c93198851c5 |
| MoP Classic | 31_49 | window | Elwynn Forest | 777832 | 777833 | 777834 | 777835 | - | - | - | 204454 | 322,706 | ffd13b6e2b3c |
| MoP Classic | 32_47 | window | Elwynn Forest | 778022 | 778023 | 778024 | 778025 | - | - | - | 204492 | 338,521 | 347ec050bcf7 |
| MoP Classic | 32_48 | window | Northshire Valley | 778027 | 778028 | 778029 | 778030 | - | - | - | 204493 | 359,854 | 4e439522fbc5 |
| MoP Classic | 32_49 | window | Crystal Lake | 778032 | 778033 | 778034 | 778035 | - | - | - | 204494 | 365,061 | a2e0602145b2 |
| MoP Classic | 33_47 | window | Burning Steppes | 778222 | 778223 | 778224 | 778225 | - | - | - | 204532 | 306,608 | 0c89b161f9f7 |
| MoP Classic | 33_48 | window | Stone Cairn Lake | 778227 | 778228 | 778229 | 778230 | - | - | - | 204533 | 322,856 | 5f0026752bb0 |
| MoP Classic | 33_49 | window | Jasperlode Mine | 778232 | 778233 | 778234 | 778235 | - | - | - | 204534 | 326,107 | 145a90b892d6 |

### T5. Per-tile content of the scenario tiles (Retail)

| Tile | Area | Hole cells | Chunks with holes | Liquid chunks / instances | Terrain textures (MDID) | Height textures (MHID) | Chunks by layer count | MDDF (unique) | MODF (unique) |
|---|---|---|---|---|---|---|---|---|---|
| 32_50 ordinary_outdoor | Elwynn Forest | 0 | 0 | 50 / 50 | 10 | 0 | {'2': 6, '3': 63, '4': 187} | 1145 (71) | 5 (5) |
| 34_46 holes_layers | Blackrock Mountain | 256 | 10 | 52 / 53 | 9 | 0 | {'2': 66, '3': 68, '4': 122} | 160 (72) | 3 (3) |
| 29_48 water | Stormwind Harbor | 0 | 0 | 229 / 229 | 16 | 0 | {'2': 86, '3': 74, '4': 96} | 229 (48) | 5 (5) |
| 30_47 dense_structures | Stormwind Harbor | 20 | 3 | 73 / 73 | 22 | 0 | {'2': 25, '3': 81, '4': 150} | 2451 (178) | 27 (17) |
| 30_49 modern terrain material (MHID, MTXP) | Stormwind City | 4 | 1 | 37 / 37 | 11 | 2 | {'2': 28, '3': 89, '4': 139} | 506 (103) | 5 (5) |

### T6. ADT structure, measured on each client's window

**Retail** (56 tiles, MPHD flags 0x3CA)

| Measure | Result (count) |
|---|---|
| root top-level | MVER MHDR MH2O MCNKx256 MFBO (43); MVER MHDR MCNKx256 MFBO (11); MVER MHDR MH2O MCNKx256 MBMH MBBB MBMI MBNV MFBO (1); MVER MHDR MCNKx256 MBMH MBBB MBMI MBNV MFBO (1) |
| tex0 top-level | MVER MAMP MDID MHID MCNKx256 (55); MVER MAMP MDID MHID MCNKx256 MTXP (1) |
| obj0 top-level | MVER MDDF MODF MCNKx256 (55); MVER MDDF MODF MWDR MWDS MCNKx256 (1) |
| obj1 top-level | MVER MLFD MLDD MLDX MLDL MLMD MLMX (18); MVER MLFD MLDD MLDX MLMD MLMX (16); MVER MLDD MLDX MLMD MLMX (13); MVER MLDD MLDX MLDL MLMD MLMX (7); MVER MLFD MLDD MLDX MLDL MDLI MDLD MLMD MLMX (1); MVER MLFD MLDD MLDX MLDL MLMD MLMX MWDR MWDS (1) |
| lod top-level | MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV (31); MVER MLHD MLVH MLLL MLND MLVI MLSI (11); MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV (5); MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV MLLN MLLI MLLV (4); MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV (2); MVER MLHD MLVH MLLL MLND MLVI MLSI MBMH MBBB MBMI MBNV MBMB MLLD MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV (1); MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV MLLN MLLI MLLV (1); MVER MLHD MLVH MLLL MLND MLVI MLSI MBMH MBBB MBMI MBNV MBMB (1) |
| holes | flag_0x10000 (14336); low_zero_with_flag (14336); high_mask_zero (14255); high_mask_nonzero (81) |
| root MCNK sub-chunks | MCVT MCNR MCSE (11918); MCVT MCCV MCNR MCSE (2409); MCVT MCNR MCSE MCBB (6); MCVT MCCV MCNR MCSE MCBB (3) |
| root header offsets | ofs_snd=MCSE start (14336); ofs_liquid=1180 but no MCLQ (11924); ofs_mccv=MCCV start (2412); ofs_liquid=1768 but no MCLQ (2412) |
| MCNK position formula | x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C fits (14336); x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C does not fit (14336); x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C does not fit (14336) |
| MCAL decode | rle: RLE ends where the next layer starts, raw spacing 2048/4096 (13109); raw4096+rle: RLE ends where the next layer starts, raw spacing 2048/4096 (689); raw4096: raw spacing 2048/4096, first offset 0 (521) |
| tex0 MCNK sub-chunks | MCLY MCAL (14319); MCLY (17) |
| obj0 MCNK sub-chunks | MCRD (6378); (none) (5177); MCRD MCRW (2145); MCRW (636) |
| root_header_index_ok | True (14336) |
| MCNK flags values | 98304 (11924); 98368 (2412) |
| mcly_0x100_by_position | ('later', True) (32848); ('first', False) (14336) |
| root_n_layers_value | 0 (14336) |
| root_alpha_shadow_offsets_all_zero | True (14336) |
| mcsh_vs_root_flag1 | (False, False) (14336) |
| raw_alpha_spacing | 4096 (1277) |
| mcly_flags | 768 (31443); 0 (14319); 256 (1277); 1002 (28); 978 (20); 896 (20); 1008 (19); 973 (5); 1006 (4); 972 (4) |
| mddf_name | fdid:m2 (32848) |
| mddf_flags | 64 (26052); 576 (6789); 96 (7) |
| modf_name | fdid:wmo (268) |
| modf_flags | 12 (263); 76 (4); 140 (1) |

**Classic Beta** (9 tiles, MPHD flags 0x3CA)

| Measure | Result (count) |
|---|---|
| root top-level | MVER MHDR MH2O MCNKx256 (8); MVER MHDR MCNKx256 (1) |
| tex0 top-level | MVER MAMP MDID MHID MCNKx256 (9) |
| obj0 top-level | MVER MDDF MODF MCNKx256 (9) |
| obj1 top-level | MVER MLFD MLDD MLDX MLDL MLMD MLMX (8); MVER MLFD MLDD MLDX MLMD MLMX (1) |
| lod top-level | MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MASD MALP (8); MVER MLHD MLVH MLLL MLND MLVI MLSI (1) |
| holes | flag_0x10000 (2304); low_zero_with_flag (2304); high_mask_zero (2293); high_mask_nonzero (11) |
| root MCNK sub-chunks | MCVT MCCV MCNR MCSE (2299); MCVT MCCV MCNR MCSE MPTX (5) |
| root header offsets | ofs_mccv=MCCV start (2304); ofs_snd=MCSE start (2304); ofs_liquid=1768 but no MCLQ (2304) |
| MCNK position formula | x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C fits (2304); x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C does not fit (2304); x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C does not fit (2304) |
| MCAL decode | rle: RLE ends where the next layer starts, raw spacing 2048/4096 (2179); raw4096+rle: RLE ends where the next layer starts, raw spacing 2048/4096 (28); raw4096: raw spacing 2048/4096, first offset 0 (4) |
| tex0 MCNK sub-chunks | MCLY MCAL (2211); MCLY (93) |
| obj0 MCNK sub-chunks | MCRD (1338); (none) (483); MCRD MCRW (276); MCRW (207) |
| root_header_index_ok | True (2304) |
| MCNK flags values | 98368 (2298); 98370 (6) |
| mcly_0x100_by_position | ('later', True) (5147); ('first', False) (2304) |
| root_n_layers_value | 0 (2304) |
| root_alpha_shadow_offsets_all_zero | True (2304) |
| mcsh_vs_root_flag1 | (False, False) (2304) |
| mcly_flags | 768 (5110); 0 (2211); 256 (37) |
| raw_alpha_spacing | 4096 (37) |
| mddf_name | fdid:m2 (5292) |
| mddf_flags | 64 (4107); 576 (1185) |
| modf_name | fdid:wmo (41) |
| modf_flags | 12 (41) |

**Classic Era** (56 tiles, MPHD flags 0x248)

| Measure | Result (count) |
|---|---|
| root top-level | MVER MHDR MH2O MCNKx256 (35); MVER MHDR MCNKx256 (21) |
| tex0 top-level | MVER MAMP MDID MHID MCNKx256 (56) |
| obj0 top-level | MVER MDDF MODF MCNKx256 (56) |
| obj1 top-level | MVER MLFD MLDD MLDX MLMD MLMX (20); MVER MLFD MLDD MLDX MLDL MLMD MLMX (18); MVER MLDD MLDX MLMD MLMX (11); MVER MLDD MLDX MLDL MLMD MLMX (7) |
| holes | flag_0x10000 (14336); low_zero_with_flag (14336); high_mask_zero (14256); high_mask_nonzero (80) |
| root MCNK sub-chunks | MCVT MCNR MCSE (14336) |
| root header offsets | ofs_snd=MCSE start (14336); ofs_liquid=1180 but no MCLQ (14336) |
| MCNK position formula | x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C fits (14336); x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C does not fit (14336); x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C does not fit (14336) |
| MCAL decode | raw2048: raw spacing 2048/4096, first offset 0 (11167) |
| tex0 MCNK sub-chunks | MCLY MCSH MCAL (6618); MCLY MCAL (4549); MCLY (2811); MCLY MCSH (358) |
| obj0 MCNK sub-chunks | MCRD (5904); (none) (5857); MCRD MCRW (1319); MCRW (1256) |
| root_header_index_ok | True (14336) |
| MCNK flags values | 98304 (7151); 98305 (6970); 98306 (209); 98307 (6) |
| mcly_0x100_by_position | ('later', True) (25685); ('first', False) (14336) |
| root_n_layers_value | 0 (14336) |
| root_alpha_shadow_offsets_all_zero | True (14336) |
| mcsh_vs_root_flag1 | (False, False) (7360); (True, True) (6976) |
| raw_alpha_spacing | 2048 (25685) |
| mcly_flags | 256 (25511); 0 (11167); 490 (40); 384 (28); 496 (23); 466 (21); 488 (9); 498 (7); 494 (5); 461 (5) |
| modf_name | fdid:wmo (178) |
| modf_flags | 12 (174); 76 (4) |
| mddf_name | fdid:m2 (24824) |
| mddf_flags | 64 (19035); 576 (5789) |

**MoP Classic** (9 tiles, MPHD flags 0x24E)

| Measure | Result (count) |
|---|---|
| root top-level | MVER MHDR MH2O MCNKx256 MFBO (8); MVER MHDR MCNKx256 MFBO (1) |
| tex0 top-level | MVER MAMP MDID MHID MCNKx256 (8); MVER MAMP MDID MHID MCNKx256 MTXP (1) |
| obj0 top-level | MVER MDDF MODF MLMB MCNKx256 (7); MVER MDDF MODF MCNKx256 (2) |
| obj1 top-level | MVER MLFD MLDD MLDX MLDL MLDB MLMD MLMX MLMB (7); MVER MLFD MLDD MLDX MLDL MLDB MLMD MLMX (1); MVER MLFD MLDD MLDX MLMD MLMX (1) |
| holes | flag_0x10000 (2304); low_zero_with_flag (2304); high_mask_zero (2288); high_mask_nonzero (16) |
| root MCNK sub-chunks | MCVT MCNR MCSE (2005); MCVT MCCV MCNR MCSE (299) |
| root header offsets | ofs_snd=MCSE start (2304); ofs_liquid=1180 but no MCLQ (2005); ofs_mccv=MCCV start (299); ofs_liquid=1768 but no MCLQ (299) |
| MCNK position formula | x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C fits (2304); x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C does not fit (2304); x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C does not fit (2304) |
| MCAL decode | rle: RLE ends where the next layer starts, raw spacing 2048/4096 (2262); raw4096+rle: RLE ends where the next layer starts, raw spacing 2048/4096 (30); raw4096: raw spacing 2048/4096, first offset 0 (11) |
| tex0 MCNK sub-chunks | MCLY MCSH MCAL (1627); MCLY MCAL (676); MCLY (1) |
| obj0 MCNK sub-chunks | MCRD (1404); (none) (509); MCRD MCRW (323); MCRW (68) |
| root_header_index_ok | True (2304) |
| MCNK flags values | 98305 (1358); 98304 (647); 98369 (269); 98368 (30) |
| mcly_0x100_by_position | ('later', True) (5455); ('first', False) (2304) |
| root_n_layers_value | 0 (2304) |
| root_alpha_shadow_offsets_all_zero | True (2304) |
| mcsh_vs_root_flag1 | (True, True) (1627); (False, False) (677) |
| mcly_flags | 768 (5405); 0 (2301); 256 (50); 1024 (2) |
| raw_alpha_spacing | 4096 (50) |
| mddf_name | fdid:m2 (6565) |
| mddf_flags | 64 (5397); 576 (1168) |
| modf_name | fdid:wmo (46) |
| modf_flags | 12 (46) |

### T7. MH2O in each window

| Client | Tiles with MH2O | liquid_type values (count) | liquid object-or-vertex-format word (count) |
|---|---|---|---|
| Retail | 44 of 56 | 2 (1567), 5 (1215), 1 (121), 7 (116), 301 (90) | 42 (1567), 427 (759), 457 (156), 554 (121), 1709 (77), 413 (74), 557 (61), 1710 (56), 436 (36), 412 (28), 417 (23), 558 (22), 560 (21), 438 (21), 4561 (16), 1708 (14), 479 (13), 480 (9), 422 (8), 559 (7), 415 (6), 416 (4), 1668 (4), 414 (3), 440 (2), 485 (1) |
| Classic Beta | 8 of 9 | 1240 (198), 1324 (88), 1288 (50) | 20345 (88), 20335 (61), 20346 (57), 20340 (43), 5962 (28), 5966 (27), 20318 (21), 20347 (7), 20354 (4) |
| Classic Era | 35 of 56 | 5 (1062), 2 (606), 7 (125) | 42 (606), 5946 (344), 5987 (156), 5957 (150), 5942 (77), 5960 (69), 5962 (62), 5958 (60), 5963 (48), 5966 (36), 5941 (31), 5947 (23), 5961 (17), 5967 (17), 5968 (17), 6009 (13), 5980 (12), 6010 (9), 5951 (8), 5952 (8), 5945 (7), 5969 (6), 5944 (6), 5959 (4), 5943 (3), 5970 (2), 6015 (1), 5965 (1) |
| MoP Classic | 8 of 9 | 5 (367), 301 (56) | 427 (256), 557 (56), 1709 (35), 436 (27), 560 (21), 438 (21), 1710 (5), 440 (2) |

### T8. Side files of Azeroth (listed, not interpreted)

| File | Retail | Classic Beta | Classic Era | MoP Classic |
|---|---|---|---|---|
| fogs | 59,604 B: MVER, VFOG, VFEXx152, VFE2x152 | 96,452 B: MVER, VFOG, VFEXx246, VFE2x246 | 12 B: MVER | 12 B: MVER |
| lgt | 685,548 B: MVER, MPL3, MSLT, MTEX, MLTA | 439,512 B: MVER, MPL3, MSLT, MLTA | 12 B: MVER | 128,884 B: MVER, MPL2, MSLT |
| mpv | 107,980 B: MVER, PVMIx19, PVPDx19, PVBDx19 | 12 B: MVER | 12 B: MVER | 12 B: MVER |
| occ | 693,868 B: MVER, MAOI, MAOH | 434,268 B: MVER, MAOI, MAOH | 405,358 B: MVER, MAOI, MAOH | 495,038 B: MVER, MAOI, MAOH |
| tex | 14,037,432 B: TXVR, TXBT, TXMDx14171 | 5,486,716 B: TXVR, TXBT, TXMDx6464 | 2,907,588 B: TXVR, TXBT, TXMDx3730 | 5,971,872 B: TXVR, TXBT, TXMDx6831 |
| wdl | 1,389,580 B: MVER, MLDD, MLDX, MLMD, MLMX, MSSN, MSSO, MSSF, MAOF, MAREx1176, MAOEx843, MAHOx1176 | 872,364 B: MVER, MLDD, MLDX, MLMD, MLMX, MAOF, MAREx736, MAOEx459, MAHOx736 | 814,882 B: MVER, MLDD, MLDX, MLMD, MLMX, MAOF, MAREx687, MAOEx416, MAHOx687 | 994,498 B: MVER, MLDD, MLDX, MLMD, MLMX, MAOF, MAREx839, MAOEx582, MAHOx839 |

### T9. Load pressure (Retail, measured from the files; LOD0 only, placements counted once per uniqueId)

| Measure | one ADT (32_50) | 3x3 around 32_50 | dense city 3x3 around 30_47 |
|---|---|---|---|
| tiles | 1 | 9 | 9 |
| adt_files | 5 | 45 | 45 |
| adt_bytes | 2,001,204 | 18,588,647 | 18,332,186 |
| mcnk | 256 | 2,304 | 2,304 |
| terrain_vertices | 37,120 | 334,080 | 334,080 |
| terrain_triangles | 65,536 | 589,824 | 589,824 |
| terrain_alpha_layers | 693 | 6,014 | 5,008 |
| mddf_entries_before_dedup | 1,145 | 9,965 | 6,339 |
| modf_entries_before_dedup | 5 | 49 | 73 |
| m2_unique | 93 | 575 | 1,179 |
| m2_instances | 1,344 | 13,258 | 13,512 |
| m2_instances_from_wmo_doodad_sets | 199 | 3,677 | 7,261 |
| m2_bytes | 1,133,686 | 7,146,702 | 38,300,328 |
| m2_vertices_unique | 17,274 | 116,954 | 566,671 |
| m2_vertices_instanced | 156,642 | 1,848,262 | 3,553,235 |
| skin_bytes | 234,320 | 1,465,504 | 5,054,576 |
| skin_indices | 54,264 | 325,053 | 1,230,357 |
| m2_dependency_files | 0 | 0 | 0 |
| m2_dependency_bytes | 0 | 0 | 0 |
| m2_dependency_not_read | 0 | 0 | 0 |
| wmo_unique | 5 | 21 | 34 |
| wmo_instances | 5 | 38 | 53 |
| wmo_root_bytes | 20,768 | 271,464 | 511,775 |
| wmo_groups | 8 | 154 | 442 |
| wmo_group_bytes | 494,738 | 15,510,480 | 57,560,224 |
| wmo_vertices | 9,789 | 293,283 | 1,028,445 |
| wmo_indices | 26,466 | 842,424 | 3,062,307 |
| wmo_groups_further_lods | 0 | 137 | 484 |
| wmo_group_bytes_further_lods | 0 | 3,689,234 | 14,419,034 |
| textures_unique | 177 | 1,023 | 2,443 |
| texture_file_bytes | 8,537,020 | 50,824,848 | 325,858,220 |
| texture_mip_bytes | 8,329,576 | 49,625,892 | 322,995,024 |
| texture_rgba32_bytes | 51,735,268 | 376,284,396 | 1,833,511,276 |
| requests | 381 | 2,393 | 5,322 |
| bytes_total | 12,421,736 | 93,807,645 | 445,617,309 |
| gpu_geometry_bytes | 5,723,444 | 56,196,650 | 103,438,048 |
| gpu_bytes_estimate | 14,053,020 | 105,822,542 | 426,433,072 |
| gpu_bytes_with_rgba32_textures | 57,458,712 | 432,481,046 | 1,936,949,324 |
| not read (m2, wmo, textures) | {'m2': 0, 'wmo': 0, 'textures': 0} | {'m2': 0, 'wmo': 0, 'textures': 0} | {'m2': 0, 'wmo': 0, 'textures': 0} |
| textures by size (top 6) | {'256x256': 73, '128x128': 17, '128x256': 12, '64x128': 11, '128x64': 10, '64x256': 7} | {'256x256': 443, '128x128': 110, '64x64': 87, '64x128': 51, '512x512': 50, '128x256': 44} | {'256x256': 1150, '512x512': 472, '128x128': 198, '64x64': 107, '512x256': 75, '1024x1024': 71} |

### T10. Environment tables present (header only, not interpreted)

| Table | Retail | Classic Era | MoP Classic | Classic Beta |
|---|---|---|---|---|
| areatable.db2 | WDC5 9986 rec, 23 fields, layout 5DB1A3FF (1 of 2 sections under a key: 1 readable, 0 without the key) | WDC5 1212 rec, 23 fields, layout 705C911D | WDC5 4078 rec, 23 fields, layout 705C911D | WDC5 1419 rec, 24 fields, layout 9995B797 (3 of 4 sections under a key: 0 readable, 3 without the key) |
| groundeffectdoodad.db2 | WDC5 2535 rec, 9 fields, layout CFD94A21 | WDC5 404 rec, 4 fields, layout 8394ED59 | WDC5 862 rec, 4 fields, layout 8394ED59 | WDC5 428 rec, 9 fields, layout CFD94A21 |
| groundeffecttexture.db2 | WDC5 2750 rec, 5 fields, layout D93D5678 | WDC5 313 rec, 4 fields, layout E77A386D | WDC5 959 rec, 4 fields, layout E77A386D | WDC5 378 rec, 5 fields, layout D93D5678 |
| light.db2 | WDC5 5343 rec, 5 fields, layout 5F16BC84 | WDC5 512 rec, 5 fields, layout 5F16BC84 | WDC5 1503 rec, 5 fields, layout 5F16BC84 | WDC5 643 rec, 5 fields, layout 5F16BC84 (2 of 3 sections under a key: 0 readable, 2 without the key) |
| lightdata.db2 | WDC5 19866 rec, 47 fields, layout 6ABF2921 | WDC5 1795 rec, 46 fields, layout 70C6CD80 | WDC5 5690 rec, 46 fields, layout 70C6CD80 | WDC5 5607 rec, 63 fields, layout 360DA016 |
| lightparams.db2 | WDC5 1674 rec, 32 fields, layout CAE394E7 | WDC5 452 rec, 17 fields, layout 51C96BAD | WDC5 1215 rec, 17 fields, layout 51C96BAD | WDC5 379 rec, 39 fields, layout A7F31923 |
| lightparamslightshaft.db2 | WDC5 435 rec, 2 fields, layout FBB113FA | WDC5 5 rec, 2 fields, layout FBB113FA | WDC5 0 rec, 2 fields, layout FBB113FA | WDC5 8 rec, 2 fields, layout FBB113FA |
| lightskybox.db2 | WDC5 422 rec, 4 fields, layout 9D4956FF | WDC5 9 rec, 4 fields, layout 9D4956FF | WDC5 120 rec, 4 fields, layout 9D4956FF | WDC5 27 rec, 4 fields, layout 9D4956FF |
| liquidmaterial.db2 | WDC5 5 rec, 2 fields, layout 98E5D7AA | WDC5 2 rec, 2 fields, layout 98E5D7AA | WDC5 4 rec, 2 fields, layout 98E5D7AA | WDC5 4 rec, 2 fields, layout 98E5D7AA |
| liquidobject.db2 | WDC5 314 rec, 5 fields, layout CB0D39E8 | WDC5 5 rec, 5 fields, layout CB0D39E8 | WDC5 43 rec, 5 fields, layout CB0D39E8 | WDC5 57 rec, 5 fields, layout CB0D39E8 |
| liquidtype.db2 | WDC5 197 rec, 21 fields, layout D1ECEEC9 | WDC5 19 rec, 21 fields, layout D1ECEEC9 | WDC5 61 rec, 21 fields, layout D1ECEEC9 | WDC5 57 rec, 21 fields, layout D1ECEEC9 |
| liquidtypextexture.db2 | WDC5 4221 rec, 3 fields, layout 7BEECC7F | WDC5 0 rec, 3 fields, layout 7BEECC7F | WDC5 1247 rec, 3 fields, layout 7BEECC7F | WDC5 1139 rec, 3 fields, layout 7BEECC7F |
| map.db2 | WDC5 1184 rec, 25 fields, layout 75863E23 | WDC5 59 rec, 23 fields, layout B47C6F05 | WDC5 285 rec, 23 fields, layout B47C6F05 | WDC5 79 rec, 26 fields, layout D43AFAC3 (3 of 4 sections under a key: 0 readable, 3 without the key) |
| mapdifficulty.db2 | WDC5 1908 rec, 11 fields, layout 24A16AD3 | WDC5 109 rec, 11 fields, layout 24A16AD3 | WDC5 427 rec, 11 fields, layout 24A16AD3 | WDC5 144 rec, 11 fields, layout 24A16AD3 (1 of 2 sections under a key: 0 readable, 1 without the key) |
| terraintype.db2 | WDC5 23 rec, 5 fields, layout 7A6E1ECD | WDC5 11 rec, 6 fields, layout AD4FFB9A | WDC5 13 rec, 6 fields, layout AD4FFB9A | WDC5 23 rec, 5 fields, layout 7A6E1ECD |
| terraintypesounds.db2 | WDC5 20 rec, 1 fields, layout B99F5777 | WDC5 20 rec, 1 fields, layout B99F5777 | WDC5 20 rec, 1 fields, layout B99F5777 | WDC5 20 rec, 1 fields, layout B99F5777 |
| weather.db2 | WDC5 182 rec, 23 fields, layout 1628B83C | WDC5 21 rec, 22 fields, layout C8B40E80 | WDC5 42 rec, 22 fields, layout C8B40E80 | WDC5 69 rec, 23 fields, layout 1628B83C |
| zonelight.db2 | WDC5 735 rec, 8 fields, layout 94CE95E0 | WDC5 0 rec, 8 fields, layout BA8274F0 | WDC5 80 rec, 8 fields, layout BA8274F0 | WDC5 21 rec, 8 fields, layout 94CE95E0 (1 of 2 sections under a key: 0 readable, 1 without the key) |
| zonelightpoint.db2 | WDC5 16440 rec, 2 fields, layout DE2377FB | WDC5 0 rec, 2 fields, layout DE2377FB | WDC5 2692 rec, 2 fields, layout DE2377FB | WDC5 573 rec, 2 fields, layout DE2377FB |

### T11. Local availability of every file the WDTs name (status-only census of all MAID words)

| Client | Root lists / on this computer / listed but not here (whole storage) | MAID words censused | OK | NOT_IN_ROOT | NOT_LOCAL | Not OK, by MAID word | Maps affected |
|---|---|---|---|---|---|---|---|
| Retail | 1925832 FileDataIDs listed by the root, 1925246 on this computer, 586 listed but not on this computer | 324,898 | 324,203 | 695 | 0 | minimap NOT_IN_ROOT 695 | 47 |
| Classic Beta | 1242272 FileDataIDs listed by the root, 1241566 on this computer, 706 listed but not on this computer | 24,245 | 24,234 | 0 | 11 | minimap NOT_LOCAL 6, obj0 NOT_LOCAL 2, root NOT_LOCAL 1, obj1 NOT_LOCAL 1, maptexture NOT_LOCAL 1 | 8 |
| Classic Era | 157029 FileDataIDs listed by the root, 156608 on this computer, 421 listed but not on this computer | 14,141 | 14,077 | 64 | 0 | minimap NOT_IN_ROOT 64 | 1 |
| MoP Classic | 418320 FileDataIDs listed by the root, 417898 on this computer, 422 listed but not on this computer | 56,854 | 56,222 | 632 | 0 | minimap NOT_IN_ROOT 632 | 29 |

Classic Beta files that are listed but not on this computer (all of them):

| Map | Tile | MAID word | FileDataID |
|---|---|---|---|
| 2720 2720 | 37_47 | minimap | 5554873 |
| 2921 2921 | 40_27 | maptexture | 6789390 |
| 0 Azeroth | 37_41 | minimap | 204688 |
| 0 Azeroth | 29_56 | minimap | 204381 |
| 1 Kalimdor | 37_19 | minimap | 208191 |
| 1 Kalimdor | 44_24 | root | 787450 |
| 1 Kalimdor | 26_36 | minimap | 207703 |
| 30 PVPZone01 | 32_31 | obj0 | 790194 |
| 489 PVPZone03 | 28_27 | minimap | 209495 |
| 533 Stratholme Raid | 41_26 | obj0 | 824537 |
| 269 CavernsOfTime | 32_26 | obj1 | 828387 |

### T12. Read time of the load sets (Retail, CascLib, one thread, warm operating-system cache)

| Set | Files | Bytes | Storage open (ms) | Index (ms) | Read (ms), runs 1/2/3 | MB/s (best) | Files/s (best) |
|---|---|---|---|---|---|---|---|
| one ADT (32_50) | 381 | 12,421,736 | 2409/2356/2420 | 341/337/336 | 145/110/92 | 135.0 | 4,141 |
| 3x3 around 32_50 | 2,393 | 93,807,645 | 2344/2343/2350 | 332/335/338 | 806/673/652 | 143.9 | 3,670 |
| dense city 3x3 around 30_47 | 5,322 | 445,617,309 | 2343/2361/2393 | 334/340/339 | 3529/2065/2596 | 215.8 | 2,577 |

