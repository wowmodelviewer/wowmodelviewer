# World viewer 03: one ADT terrain tile, verified geometry

Branch `world/03-terrain-geometry`, from `develop` at `193f2548` (2026-10-12, after `world/02-map-catalog`, PR #103).
The work is **one real ADT tile's untextured terrain geometry, drawn in the Unity viewport through the production
host asset path**, with proven topology, coordinates, holes, seams inside the tile and resource lifetime. There is no
Maps viewer mode, no streaming, no texturing, no doodads, buildings, liquids, sky, fog or lighting parity. The audit
(`00-audit.md`), the parsers (`01-parsers.md`) and the map catalogue (`02-map-catalog.md`) are the specification.

**Push status at delivery: NOT PUSHED.**

Every picture in this report is a geometry diagnostic (neutral grey Lambert from geometry-derived normals, or a
false-colour view). None is a claim about the game's terrain materials, lighting or environment.

## 0. Result in brief

Final binaries: host `wowmodelviewer.exe` SHA-256 `cbb9080c8a95...` (probe build; the deploy build is the same source),
player `Assembly-CSharp.dll` MD5 `67c0d3e67449...` (protocol 12). Every run below was on a hidden desktop.

| Check | Result |
|---|---|
| One real tile through the production path (Map.db2 -> WDT -> host read -> player WDT parse -> MAID root -> host read -> player ADT parse -> mesh) | **PASS**: Retail, Classic Era, MoP Classic and Classic Beta, 6 pinned tiles (section 9) |
| Geometry against an independent CPU reference (Python, `wowfmt` reader) | **PASS**: the meshes read back out of Unity are bit-exact on all 6 tiles (111,360 positions and 111,360 normal values per full tile; indices, counts, bounds, layer, transforms); the C# builder alone is bit-exact on all 130 corpus window tiles (14,476,800 positions and normals) |
| 145-vertex topology, centre fan, counts | **PASS**: 145 vertices and 256 triangles per chunk, 37,120 / 65,536 per full tile, triangles = 65,536 - 4 x hole cells on every tile |
| Holes (64-bit mask, bit row*8+col, LSB first) | **PASS**: exact triangles omitted (64/64 single bits; 16,384/16,384 cells against the masks in top-down captures of 34_46, 29_50 and MoP 32_48); a hole across a chunk edge (29_50 n=164/180) drawn as one 6 x 4-cell opening |
| Seams between chunks | **PASS**: x/z bit-identical and normals bit-equal at every shared point; no background pixel inside the terrain, including at a 20-degree grazing view |
| Orientation and chirality on screen (game minimap against a top-down capture, 8 symmetries) | **PASS**: identity wins by 9.18x (32_50) and 3.50x (34_46) on the real captures; every mirrored or rotated variant fails |
| Origin-local precision | **PASS**: <= 2.0345e-5 yd from the double expression on every tile, bit-exact with the float cast; absolute coordinates would be 6.51e-4 yd at map edges |
| Real failure states end to end | **PASS**: not in client storage (Retail map 1534, MoP map 35), not installed locally (Classic Beta 787450), no tile (MAIN flag clear, 0_0), unsupported (low-resolution hole masks, map 1051 31_29), stale (client switch, epoch, load), refused (crafted requests) |
| Client switching | **PASS**: every in-flight load across Retail -> Era -> MoP -> Beta -> Retail answered "cleared", its WDT request answered "stale" (epochs E / E+1), no terrain left; each later tile built from that client's own bytes (MD5 = corpus) |
| Resource lifetime | **PASS**: terrain Meshes / Materials / GameObjects 256/1/257 while a tile is shown, 0/0/0 after clear, model load, client switch and every failed load; global Mesh/Material counts back to their references; no per-load growth over 10 repeated loads |
| Host responsiveness (GUI-thread time per request) | **PASS**: worst file (1,462,916-byte root) median 9.45 ms, p95 9.74 ms over 30 warm requests; budget 20 ms; no transport change needed |
| Player self-test (`-wmvLifecycleTest`) | **PASS**: 767 passed, 0 failed (631 before this branch; 730 at the first stage 03 commit) |
| Regression (C# parsers, Python audit, map catalogue, customization x4 clients, Models, Buildings, texture decode, old-host and old-player compatibility, casc-whole-read, wdc3-strings) | **PASS**: identical to `develop` where comparable (section 14) |
| Bounded terrain IPC and diagnostic bookkeeping (follow-up, section 17) | **PASS**: no request or record table grows however many answers never come; a late map answer can only reach the terrain session, which drops it; host records and first-read tracking bounded and scoped to one client; memory flat over 553-load runs |
| Adversarial review (design, core + host, player, end to end) | design review by 3 reviewers, then two code rounds: 38 findings each checked by an independent verifier, 26 distinct ones confirmed and fixed, 10 refuted; end-to-end findings fixed or recorded (section 15) |

## 1. Files

New (`Tools/UnityRendererProject/Assets/` is `A/`):
- `A/Scripts/Wow/World/TerrainMesh.cs` -- the pure C# mesh builder and the single game-to-Unity conversion.
- `A/Scripts/Wow/World/TerrainDump.cs` -- the mesh dump format (a test fixture) shared by the runner and the player.
- `A/Scripts/Wow/World/AdtSynthetic.cs` -- the synthetic ADT kit (moved out of the tests, public) used by the runner
  and the player self-test.
- `A/Scripts/WmvTerrainSession.cs` -- the terrain load and scene state machine (testable without the player's main
  object).
- `A/Scripts/WmvTerrainBuilder.cs` -- Unity objects for a tile, `WmvRuntimeTerrainTile`, layer and masks, debug flags.
- `A/Scripts/WmvMainTerrain.cs` -- wiring into `WmvMain`: view mode, camera, capture, dump, runtimeState fields.
- `A/Resources/WmvTerrain.shader` -- `Wmv/Terrain`, the diagnostic shader.
- `Tools/UnityRendererProject/Tests/TerrainMeshTests.cs`, `TerrainCorpusTests.cs` -- runner suites.
- `Tools/world-audit/terrain_reference.py`, `compare_terrain_mesh.py`, `minimap_orientation.py`,
  `check_terrain_run.py`, `tests/test_terrain_tools.py`.
- `docs/world-viewer/03-terrain-geometry.md` and the captures in `docs/world-viewer/03-terrain-geometry/`.

Changed: `Source/wowmodelviewer/UnityIpcServer.h/.cpp` (protocol 12), `modelviewer.h/.cpp` (terrain requests,
validation, PlayerContent), `app.cpp` (`-terraintile`), `Source/games/wow/WorldMapCatalog.h/.cpp`, `WoWFolder.h`,
`CASCFolder.h/.cpp` (read limit and size out), player `WmvIpcClient.cs`, `WmvMain.cs`, `WmvMainMapObject.cs`,
`WmvOrbitCamera.cs`, `WmvShadowRig.cs`, `WmvMountProbe.cs`, `WmvLifecycleSelfTest.cs`, the runner project and
`WorldParserTests.cs`, `scripts/tests/casc-whole-read/` (a TooLarge case), `docs/unity-renderer/README.md` (protocol
12), the TooLarge detail text in `02-map-catalog.md`, and the low-resolution hole corrections in `00-audit.md` and
`01-parsers.md`.

## 2. Data path

1. **Host, `ModelViewer::RequestTerrainTile`** (`modelviewer.cpp:3559`, resolution in `ResolveTerrainTile` at `:3505`).
   It refuses unless a client is loaded and none is loading, the player speaks protocol 12, the catalogue is Ready and
   belongs to the loaded client, and the tile is inside 0..63. `find(mapId)` gives the WDT reference, and
   `catalog->inspect(mapId)` must say Available for every client family. Retail and Classic Beta name the WDT by
   Map.db2 FileDataID. Era and MoP name it by path, and the WDT id sent is the storage root's own FileDataID for that
   path (`storageFileDataId`, never manufactured; refused when it is not > 0). The host then ends its model await
   state (the player drops those jobs), raises the terrain load serial only when the message is actually sent, and
   keeps the current request.
2. **Host -> player** `loadTerrainTile` (`UnityIpcServer.cpp:1289`).
3. **Player -> host** `getMapAsset` role `wdt`. The host decides (`decideMapAssetRequest`, `modelviewer.cpp:3454`) and
   reads through the catalogue's verified reader (`WorldMapCatalog::inspectFile`, `WorldMapCatalog.cpp:388`: content
   key checked, states classified, 4 MiB limit decided before reading). The answer is an ordinary `assetResponse`
   with a binary payload (`UnityIpcServer.cpp:1344`).
4. **Player**: `WdtParser.Parse`, `TileExists(x, y)` and `TryGetTileFiles(x, y)` (`WdtParser.cs:144-147`) -> the root
   FileDataID, MAID word 0 (PROVEN a FileDataID in all four clients, 02 section 6).
5. **Player -> host** `getMapAsset` role `rootAdt`, served as in 3.
6. **Player worker thread**: MD5 of the bytes, `AdtParser.ParseRoot(bytes, x, y)` (`AdtParser.cs:44`),
   `TerrainMeshBuilder.Build` (`TerrainMesh.cs:248`). **Main thread**: Unity objects (`WmvTerrainBuilder.cs`).
7. **Player -> host** `terrainTileLoaded`, exactly once per load.

No WDT or ADT is parsed in C++. Nothing is written to disk in production (the dump is a test fixture behind a flag).
The player never opens an installation or reads DB2.

## 3. IPC: protocol 12

**Why new messages.** A terrain kind on `loadWoWModel` would never reach an older player (every kind is gated), but
`loadWoWModel` has side effects that must not touch the model session: it supersedes the WMO job, abandons character,
dresser and mount jobs, resets the slot and starts a new asset-cache generation. The existing asset request reads
through the file index (custom overrides, no content-key check, coarse reasons) and carries no client identity, so
it can neither tell not-in-root, not-local, unreadable and unsupported apart nor notice a request from a replaced
client. `getMapAsset` rather than new fields on `getAssetByFileDataID` is a clarity choice: a protocol-12 player only
sends it in answer to a protocol-12 host.

| Message | Direction | Fields |
|---|---|---|
| `loadTerrainTile` | host -> player | `load`, `assetEpoch`, `mapId`, `mapDirectory`, `product`, `build`, `wdtFileDataID`, `wdtResolvedBy` (`fileDataID` or `path`), `wdtPath`, `tileX`, `tileY`, `maxAssetBytes` (4,194,304) |
| `clearTerrain` | host -> player | `reason` |
| `getMapAsset` | player -> host | `requestId`, `load`, `assetEpoch`, `role` (`wdt` or `rootAdt`), `fileDataID` |
| `assetResponse` (to `getMapAsset`) | host -> player | the usual fields and binary payload, plus `role`, `load`, `assetEpoch`, `state`, `detail`, `readMs`, `md5`; `error` = `detail` when not ok; never cacheable |
| `terrainTileLoaded` | player -> host | `load`, `status` (`built`, `failed`, `superseded`, `cleared`), `reason`, `hostState`, `hostDetail`, map/tile, WDT and root ids, bytes and MD5s of the bytes parsed, chunk counts, vertices, triangles, hole cells, bounds (Unity, root-local), `rootOffset`, `originWorld`, timings, `meshBytes`, live and object counts, managed and native memory |
| `runtimeState` additions | player -> host | `liveTerrainTiles`, `terrainLoad`, `terrainLoading`, `terrainView`, `cameraMask`, `meshObjects`, `materialObjects`, `terrainMeshes`, `terrainMaterials`, `terrainGameObjects`; request bookkeeping (section 17): `terrainPendingRequests`, `terrainRetiredRequests`, `terrainRetiredTotal`, `terrainLastRetiredRequest`, `mapAnswersRouted`, `mapAnswersDropped`, `mapAnswersLate`, `mapAnswersUnowned`, `terrainTimeoutMs` |

**Host validation, before any read** (`decideMapAssetRequest`): a client being loaded -> `refused`; no `load` or
`load` <= 0 -> `refused`; no current request, another load, another client epoch, or a catalogue that no longer
belongs to the loaded client -> `stale` (the detail names every reason that applies); an unknown role or
`fileDataID` <= 0 -> `refused`; role `wdt` with an id other than the load's WDT -> `refused`; role `rootAdt` before
this load's WDT was served, or a second, different root -> `refused`. Then the catalogue read with the 4 MiB limit;
the answer's state is the catalogue's own (`available`, `not in client storage`, `not installed locally`,
`unreadable`, `unsupported`).

**Limits.** 4 MiB per map file (largest root ADT in any client 1,462,916 B, largest WDT of the audited maps 294,988 B;
section 10), enforced by the host before reading (`CASCFolder.cpp:260`, TooLarge with the size) and again by the
player on receipt. The player times a load out after 30 s.

**Compatibility, run.** New host + protocol-11 player: in-process checks pass, the run ends "player too old", and
nothing terrain-related is sent (the host's only message after `unityReady` was `viewportBackground`; "getMapAsset
served 0"). Old host (`develop` before this branch) + protocol-12 player: `-mo bear -unityipctest` PASS; the old host
logs its version notice and carries on. The protocol-2 TestStub is never sent terrain.

**Documented limitation.** The host guarantees that the bytes are the current client's file with the requested id
and that the root follows this load's WDT; it has no WDT parser, so it cannot check MAID membership. The player's
tile-identity check (section 5) catches a root of another tile but not of another map at the same x_y.

## 4. Client-specific resolution (real runs)

| Client (build) | Map 0 WDT | Resolved by | WDT bytes, MD5 | Tile | Root id (MAID word 0), bytes, MD5 |
|---|---|---|---|---|---|
| Retail (12.1.0.69933) | 775971 | Map.db2 FileDataID | 294,988, `ac31869061a40d1cac3d4661c3a4a859` | 32_50 | 778037, 329,922, `101be8a2fe1bed9cc1d8aa19c7f66afb` |
| Retail | 775971 | Map.db2 FileDataID | same | 34_46 | 778417, 433,544, `e9a418c3038687dddaeb2ebd2bc55219` |
| Retail | 775971 | Map.db2 FileDataID | same | 29_50 | 777437, 309,593, `fb77f7a24b2c642784cd66912a78e8a0` |
| Classic Era (1.15.9.70003) | 775971 | path `world/maps/Azeroth/Azeroth.wdt` -> storage id | 163,908, `7ce81ad2a553b5114914cca9131326c1` | 32_50 | 778037, 329,878, `a3fdcbb6fc0e27354a899525f3e337f2` |
| MoP Classic (5.5.4.70268) | 775971 | path -> storage id | 163,908, `55322ab73c01fdce4f7e558d69218fbb` | 32_48 | 778027, 359,854, `74fd4b05701218492bf4e9dc20b68045` |
| Classic Beta (1.60.1.70338) | 775971 | Map.db2 FileDataID | 294,988, `e4fb77fc12097f3b41adf2aac2d945e7` | 32_48 | 778027, 477,275, `2154dd73295a7fea0ce3f77baa465293` |
| Retail | 809976 (map 1051) | Map.db2 FileDataID | 163,908, `5d88cbcf3f9ed8d15e25df65c1c9d472` | 31_29 | 811303, 1,462,916, `78354c0cc6ad949ecd99850ca6acc169` |

In every run the MD5 the player computed of the bytes it parsed equals the MD5 the host logged for the bytes it
served, and both equal the audit corpus file of the same client and build (map 0; map 1051 is not in the corpus, its
root was compared with a separate read-only extraction). Retail and MoP 778027 have the same size and different
bytes, and the reported MD5s differ, so a request answered from the wrong client would be detected. Retail and Era
32_50 have different bytes and identical geometry; MoP and Beta 32_48 differ in 7,950 heights (up to 34.6 yd), and
each matches its own corpus file.

**States, distinguished end to end:**
- *not in client storage*: Retail map 1534 ("the client's storage root does not list FileDataID 2449139") and MoP
  map 35 (path not in the storage root), refused by the host before a load is sent (`expectfail` steps PASS).
- *not installed locally*: Classic Beta Kalimdor 44_24, root 787450 (precondition checked first): the player fails
  the load with `hostState` "not installed locally" and the earlier tile stays.
- *no tile*: Azeroth 0_0 (MAIN word 0 = 2, the has-ADT bit clear in all four clients): "no tile at 0_0 (MAIN flag
  clear)".
- *unsupported*: map 1051 31_29, 5 chunks with a low-resolution hole mask (section 7): those chunks are reported and
  not drawn.
- *malformed*: synthetic only (runner and self-test: short MCNK, bad MCNR/MCCV sizes, wrong stored positions, a WDT
  the parser rejects, a root with no decodable chunk); no malformed real file was met.
- *stale / refused*: client switch, `staleepoch`, `staleload` and 19 crafted requests (section 12).

## 5. Coordinates: the single conversion

PROVEN in the audit (6.1-6.4) and the parsers: world x = (32-ty)T - iy*C - r*U, y = (32-tx)T - ix*C - c*U, z up,
T = 1600/3, C = T/16, U = C/8; z = stored C + MCVT height; outer vertex k = 17r+c, inner k = 17r+9+c. PROVEN in the
renderer: model space -> Unity is M(x, y, z) = (-y, z, x), scale 1, determinant -1 (`WowCoordinateConverter.cs:34`).

**Pinned expressions** (`TerrainMesh.cs:230`; `terrain_reference.py` uses the same):

```
T = 1600.0/3.0;  U = T/128.0                                    (doubles; U is exact, 128 is a power of two)
outer (r, c) of chunk (ix, iy) of tile (tx, ty):  R = 128 ty + 8 iy + r;  Cc = 128 tx + 8 ix + c
    X = (4096 - R) U;        Y = (4096 - Cc) U
inner (r, c):                X = (4095.5 - R) U;      Y = (4095.5 - Cc) U
Z = (double) stored C + (double) MCVT[k]
tile origin (north-west corner):  X0 = (4096 - 128 ty) U;  Y0 = (4096 - 128 tx) U;  Z0 = 0
Unity, origin-local:  ux = (float)(-(Y - Y0));  uy = (float)(Z - Z0);  uz = (float)(X - X0)
```

Tests (runner): X0 = (32-ty)T exactly for all 65 values; every vertex equals `TerrainCoordinates.OuterVertex/
InnerVertex` bit for bit (185,600 vertices); `WorldToUnity` equals `WowCoordinateConverter` on the same inputs; the
first row and column of local coordinates are exactly 0 (-0.0 on column 0, compared numerically).

**Why M and not another map.** Placed models (MDDF/MODF, proper rotations) must land on the ground: converting their
geometry with M and the world with anything else would mirror or rotate them against it.

**Orientation, proven, not looked at.** Every geometric check is invariant under "mirror the horizontal map and
reverse the index order", so the orientation is proven against the game's own minimap (MAID word 7, DXT1 512 x 512, one
4 x 4 block per cell). Its luminance is fitted against the per-cell surface normals for all 8 image symmetries:

| R^2 by symmetry | 32_50 runner | 34_46 runner | 32_50 player capture | 34_46 player capture |
|---|---|---|---|---|
| identity | **0.2184** | **0.4607** | **0.2144** | **0.3655** |
| best other | 0.0212 (transpose) | 0.0945 (transpose) | 0.0233 (transpose) | 0.1045 (transpose) |
| ratio (needs >= 2) | 10.3 | 4.88 | 9.18 | 3.50 |

The player columns come from an orthographic top-down capture (camera rotation Euler(90, 0, 0): screen up = Unity +Z =
north, screen right = Unity +X = east) of the `normals` view, so they prove M, the index order, the camera and the
on-screen chirality end to end. Every mirrored or rotated variant fed to both modes fails (ratios 0.01-0.27). The fit
puts the minimap's light in the north-west. Separately the captured normal colours per cell match the dump within
about 2 of 255, against 26-93 under a mirrored or transposed mapping. On 32_48 the 2x rule is not met (Retail ratio
1.63, MoP 1.40) although identity still ranks first, so the rule is applied to 32_50 and 34_46 only.

**Precision.** Origin = the shown tile's north-west corner; this stage re-bases on every replacement (the root is at
0) and the scene belongs to (client epoch, map). The stored float x/z differ from the double expression by at most
2.0345e-5 yd on every tile and equal its float cast bit for bit; heights are the game's own float sum (0 to
9.5e-7 yd from the double). In absolute coordinates the error would be 3.26e-4 yd at 32_50 and 6.51e-4 yd at the map
edges (0.30 / 0.61 px at 1 yd from a 60-degree 1080p camera, against 0.019 px origin-local). "Exact multiples of T in
float" is not claimed: T is not representable, and neighbouring float roots disagree by up to 1.95e-3 yd, which is
why a multi-tile stage must compute every tile's vertices against one origin in double (section 16).

## 6. Topology and winding

- 256 chunks in file order, chunk n at (n % 16, n / 16). Every built chunk keeps all 145 vertices (81 outer, 64 inner)
  in MCVT order; per cell (r, c) with a = Outer(r, c), b = Outer(r, c+1), cc = Outer(r+1, c), d = Outer(r+1, c+1),
  m = Inner(r, c), the Unity index order is (m, a, b), (m, b, d), (m, d, cc), (m, cc, a) (`TerrainMesh.cs:188`). Unity's
  front face is where Cross(v1-v0, v2-v0) points; these point up.
- No Unity Terrain, no 257 x 257 grid, no stitching: one Mesh per chunk with triangles, 145 vertices, 16-bit indices,
  one sub-mesh, Triangles topology (`WmvTerrainBuilder.cs:392`).
- Checks that can fail on data (the Cross.y of a fan triangle is fixed by the grid at U^2/2 = 8.680556 whatever the
  heights, so "Cross.y > 0" alone proves nothing): every triangle's |Cross.y - U^2/2| <= 1e-3 (largest 2.12e-4); each
  cell's four triangles start with its inner index and their projected areas sum to U^2; no duplicate triangles;
  indices equal the reference exactly.
- The real proof of winding is the culling render. Self-test: a terrain chunk seen from above with back-face culling
  covers pixels, from below none. Captures: the drawn pixels of 8 real views (3 from below) equal the projected
  footprint of the camera-facing triangles within 1 px at edges (for the default view 276,130 drawn against 276,131
  predicted). From below at -45 degrees, 2,513 pixels are drawn: exactly the 1,298 steep faces that do face such a
  camera (`09-from-below-retail-32_50.png`); from straight below at three times the distance, none.
- Counts on every pinned tile: 37,120 vertices and 65,536 triangles for a full tile; 34_46 36,830 / 64,512 (two all-hole
  chunks get no mesh); 29_50 37,120 / 65,440; 32_48 37,120 / 65,472.

## 7. Holes

- Rule (PROVEN, 01): MCNK flag 0x10000 set -> 64-bit mask at header +20, cell (r, c) is a hole when bit r*8+c is set,
  least significant bit first. A hole cell loses exactly its four triangles; its vertices stay in the vertex buffer
  but are not referenced and do not count in the bounds (`TerrainMesh.cs:296`, bounds at `:437`). No smoothing, no
  filling.
- Tests: no holes; each of the 64 single bits removes exactly the four triangles that start with Inner(bit/8, bit%8)
  (64/64); a full mask gives no triangles and no GameObject; the Unity-side hole render (self-test): the hole cell's
  pixels uncovered, everything else covered.
- Real tiles: 34_46 ten hole chunks (n = 3, 4, 5, 19, 20, 21, 36, 37, 145, 146; n = 4 and 20 full, n = 3 0xC0C0, n = 21
  0x3F3F3F3F3F3F3F3F), 256 cells; 29_50 n = 164 0x3F3F000000000000 and n = 180 0x3F3F, 24 cells forming one 6 x 4-cell
  opening across the chunk edge; 32_48 (all four clients) n = 51 0xF0F0000000000000 and n = 67 0xF0F0, 16 cells.
  Top-down captures agree with the masks on 16,384 of 16,384 cells for 34_46, 29_50 and MoP 32_48.
- **Low-resolution masks occur in real data.** Every chunk of Retail map 1051 tile 31_29 has flag 0x10000 clear (the
  parser's 256 "Unexpected" notes), and five carry a non-zero 16-bit mask at +60: chunk 51 0x8000, 67 0x0008, 68 0x6311,
  84 0x008C, 85 0x0010. The audit had seen only zero masks, so its layout is not established and those five chunks are
  reported Unsupported and not drawn (`10-unsupported-lowres-holes-1051-31_29.png`). A lead, not a proof: read as a 4 x 4
  grid (bit row*4+col, least significant first, each bit 2 x 2 cells) the five masks form one contiguous opening across
  the five chunks; the transposed and reversed readings do not. The earlier "never seen" claims are corrected in
  `00-audit.md` and `01-parsers.md`.

## 8. Normals

MCNR's channel interpretation is still unresolved (audit U9), so no authentic normal mapping is claimed. The normals
are **geometry-derived diagnostics**: face normal = Cross(p1-p0, p2-p0) on the double Unity-space positions in the
mesh's own index order, area-weighted, summed per vertex over the kept triangles, normalised, cast. Outer vertices are
shared tile-wide on the 129 x 129 grid, so both sides of a chunk border get the same normal (bit-equal at all 4,320
shared points per tile; per-chunk normals would differ there by up to 116 degrees). A vertex with no kept triangle gets
(0, 1, 0). Tile-edge normals are one-sided. A trap is pinned by a test: converting a world-frame cross product with the
axis map (determinant -1) points every normal down.

## 9. Mesh lifetime and the terrain scene

- **Ownership.** One persistent root `_WmvTerrain`; each tile is a `WmvRuntimeTerrainTile` that tracks every
  GameObject, Mesh and Material the moment it exists (`WmvTerrainBuilder.cs:208-210`), so a mid-build exception
  disposes them. `Dispose()` is the deferred `Object.Destroy`; `DisposeImmediate()` is for tests. Static tallies (live
  tiles, meshes, materials, GameObjects) count from creation, not from the deferred destroy.
- **Scene.** A dictionary keyed (map, x, y): one tile at a time in this stage, but the structure holds several. The
  commit point (`WmvTerrainSession.cs:530-560`) makes the new tile current in the same block that disposes every other
  one; before it a failure disposes only the new tile and leaves the previous tile and view as they were; after it a
  failure still reports "built", with the reason.
- **Isolation.** Layer 30 on every terrain GameObject; two fixed camera masks, model view = everything but 30, terrain
  view = 30 only (`WmvTerrainBuilder.cs:263-267`), set only by the view-mode owner. The model session is hidden, never
  disposed. The shadow rig, the light check and the mount probe never see layer 30 and skip work when nothing else is
  left (`WmvShadowRig.cs:161`). Entering the view snapshots the full orbit state (`WmvOrbitCamera.cs:514-525`); leaving
  restores it. A mount re-fit skipped during the terrain view is done when the view is left.
- **Commands: the latest wins.** `loadTerrainTile` supersedes a terrain job ("superseded") and drops model loads in
  flight as a model load would; it does not cancel the on-screen character's own pending scene or mount, which commit
  while hidden. `loadWoWModel` first supersedes a terrain job and takes a shown tile away. `clearTerrain`: a load in
  flight is answered "cleared" once; a shown tile is taken away with no report.
- **Report.** "built" only after frames N+1 and N+2 have started following the adoption in frame N (the player renders
  on its own render thread; the same guarantee as the host's two-question uncover fence), and only if the tile is still
  current, otherwise "superseded". One report per load.
- **Evidence.** Tallies 256/1/257 while a full tile is shown, 0/0/0 after clear, model load, client switch and every
  failed or superseded load (34 loads in the main lifecycle run, each answered exactly once). Over 10 repeated loads
  the Mesh/Material counts stay constant (257/63; 258/65 with a hidden model), native memory settles after the first
  three loads (+8 to 203 KB over the last six, against 1.28 MB per tile), and the managed heap is flat. A failed load
  with a model on screen leaves the model shown and no terrain objects.

## 10. Asset sizes

Largest root ADT of any client: 1,462,916 B (FileDataID 811303, map 1051 tile 31_29, Retail and MoP Classic). Largest
WDT of the audited maps: 294,988 B (Retail Azeroth, 775971). The 4 MiB limit covers both with margin; the catalogue's
own 64 MiB default stays for its other reads, and the one oversized file the catalogue meets (1467245, 178,795,700 B)
is refused unread at either limit.

## 11. Host responsiveness

The host serves map files synchronously on the GUI thread (polled every 20 ms). Measured per request (validation,
read, MD5, queueing the answer), warm groups over three or more processes:

| File | Requests | Handler ms median / p95 / max | Read ms median |
|---|---|---|---|
| WDT 775971 (294,988 B) | 68 | 1.47 / 1.62 / 1.87 | 0.88 |
| Root 778037 (329,922 B) | 61 | 2.35 / 2.47 / 2.60 | 1.71 |
| WDT 809976 (163,908 B) | 40 | 0.85 / 0.98 / 0.99 | 0.46 |
| Root 811303 (1,462,916 B) | 40 | 9.48 / 10.15 / 10.41 | 7.11 |

The first read of each file in a process is set aside: root 778037 2.72 ms median (max 2.80), root 811303 9.61 ms (max
9.85). The WDT is read once more on the GUI thread just before a load is sent (the catalogue's inspection: 0.87 ms for
775971, 0.45 ms for 809976). The budget (median and p95 <= 20 ms) holds with a wide margin, so no transport change is
made in this stage; the audit's storage-owning serving thread (W3) stays the answer for multi-tile request volumes.
The OS file cache was warm in these runs; a cold disk read was not measured.

## 12. Test entry point

`wowmodelviewer.exe -terraintile <mapId> <x> <y> [<out.json>] -product <p>` (headless; `app.cpp:4978`) loads the client,
runs 29 in-process checks (19 crafted `getMapAsset` decisions; the WDT read at a 1000-byte, the map-file and the default
limit; the WDT the load names; refusals of a tile outside the grid, an unknown map and a map without a WDT; three
failed-load bookkeeping cases), starts the player, requires protocol
12, requests the tile through the production path and waits for its report. `WMV_TERRAINTEST_SEQUENCE` adds steps:
`tile`, `repeat`, `clear` (also in flight), `supersede`, `staleepoch`, `staleload`, `expectfail`, `notlocal`
(precondition-checked), `switch`, `model` (also superseding a building tile), `modelthentile`, `wait`, and the late-answer
steps of section 17 (`timeout`, `latesupersede`, `lateclear`, `lateswitch`, `reconnect`, `failrepeat`). Invariants:
every load answered exactly once, MD5 parsed = MD5 served, terrain objects 0 after clear / model / switch, counts back
to the named reference, no load in flight at the end. Player switches: `-wmvTerrainView=shaded|chunks|wire|
shadedwire|normals`, `-wmvTerrainChunk=N`, `-wmvTerrainFocus=N`, `-wmvTerrainTopDown`, `-wmvTerrainDump=<name>`. There is
no user-facing entry point: the Maps mode is a later stage.

## 13. Visual validation

All captures are real Unity viewport frames taken through `WMV_VIEWPORT_SHOT` on a hidden desktop.

| Capture | What it shows |
|---|---|
| ![](03-terrain-geometry/01-shaded-tile-retail-32_50.png) `01` | The whole Retail 32_50 tile (Elwynn), neutral diagnostic shading; Era 32_50 gives a byte-identical frame |
| ![](03-terrain-geometry/02-topology-retail-32_50.png) `02` | Wireframe over shading around chunk 119: square cells, each a four-triangle fan around its inner vertex |
| ![](03-terrain-geometry/03-single-mcnk-119.png) `03` | Chunk 119 alone: 8 x 8 cells, 145 vertices, 256 triangles |
| ![](03-terrain-geometry/04a-holes-retail-34_46.png) `04a` | Retail 34_46 around chunks 20 (all holes) and 21 (0x3F3F3F3F3F3F3F3F): openings cut cell by cell |
| ![](03-terrain-geometry/04b-hole-across-chunks-retail-29_50.png) `04b` | Retail 29_50: one 6 x 4-cell opening across the border of chunks 164 and 180 |
| ![](03-terrain-geometry/04c-holes-topdown-retail-34_46.png) `04c` | 34_46 from straight above: the hole cells open exactly where the masks say |
| ![](03-terrain-geometry/05-chunk-seams-retail-32_50.png) `05` | Per-chunk tint around chunk 119: closed seams between chunks |
| ![](03-terrain-geometry/06-second-angle-retail-32_50.png) `06` | The same tile from yaw 315, pitch 25 |
| ![](03-terrain-geometry/07-mop-classic-32_48.png) `07` | MoP Classic 32_48 (another client family; holes at chunks 51/67) |
| ![](03-terrain-geometry/08-topdown-normals-retail-32_50.png) `08` | The `normals` view from straight above, the input of the orientation fit |
| ![](03-terrain-geometry/09-from-below-retail-32_50.png) `09` | From below (pitch -45): only the steep faces that face the camera are drawn |
| ![](03-terrain-geometry/10-unsupported-lowres-holes-1051-31_29.png) `10` | Map 1051 31_29 from above: the five chunks with low-resolution hole masks are not drawn |
| ![](03-terrain-geometry/11-regression-model-bear.png) `11` | Models regression: the bear through the new host and player |
| ![](03-terrain-geometry/12-regression-building-stockade.png) `12` | Buildings regression: the Stockade through the new host and player |

Two capture artefacts, both outside this branch's code: the `normals` view goes through the player's bloom (the
background reads 26-28 instead of 25, terrain about 1-1.6 of 255 higher; the orientation verdict is unaffected), and
one capture of 30 came out undersized because the screen resize had not landed within the capture's wait (a retake
was correct).

## 14. Regression and tests

| Test | Result |
|---|---|
| C# runner (`WowParserTestRunner`, corpus set) | ALL RUN TESTS PASSED: WowParserTests 792, WorldParserTests 147, TerrainMeshTests 86 (new), WorldCorpusValidation 74, TerrainCorpusTests 81 (new); all 130 window tiles 84/84 with `WMV_TERRAIN_ALL_TILES=1` |
| netstandard 2.1 compile check | 0 warnings, 0 errors |
| Python audit tests | 54 tests OK (20 new; 18 skip without the corpus) |
| `compare_terrain_mesh.py` (player dumps, 6 tiles) | 15/15 checks each, bit-exact |
| `check_terrain_run.py` over every terrain run (57 runs) | PASS: 2,379 passed, 0 failed, 58 skipped (10 harness precondition skips, 48 map-1051 corpus comparisons: the corpus holds map 0 only) |
| Player self-test | 767 passed, 0 failed (final follow-up player) |
| `-terraintile` lifecycle (34 loads), client switch (4 switches), Beta not-local, worst case x 11, not-in-root x 2 | PASS (131/0, 74/0, 39/0/1, 60/0, 39/0 and 38/0/1) |
| Old player (protocol 11) + new host | in-process checks PASS, then "player too old" (expected) |
| Old host + new player, `-mo bear -unityipctest` | PASS |
| `-mo bear` / `-wmo 108631` `-unityipctest`, new host + new player | PASS / PASS (27 groups) |
| Map catalogue sequences (4 clients) + comparator | PASS 44, 44, 27, 27; 104,333 checks, 0 differences |
| `develop` against this branch: customization x4 clients, model, building, texture decode | identical results on both; texture PNG byte-identical |
| `scripts/tests/casc-whole-read` | 24 passed (21 before; the new TooLarge case reads nothing) |
| `scripts/tests/wdc3-strings` | 54 passed |

Performance (player, Retail 32_50; first load of a process, n = 7 / repeated loads, n = 30, medians, ms): WDT round
trip 17.2 / 15.8, WDT parse 2.5 / 0.19, root round trip 7.2 / 15.2, root parse 7.7 / 2.7, mesh build 11.1 / 8.5, Unity
upload 6.1 / 2.7, adoption to the start of frame N+2 3.2 / 2.3, total 69.2 / 49.8 (repeated: p95 51.0, max 51.6).
Map 1051 31_29 (1.46 MB root): total 100.3 / 80.2. Mesh bytes 1,284,096 per full tile (positions and normals, 16-bit
indices). The round trips are mostly the harness's poll interval (about 15 ms of a 1.5-2.3 ms handler); the viewer
polls every 20 ms. The C# builder on .NET: parse 0.08-0.19 ms, build median 0.98-1.46 ms per tile.

## 15. Adversarial review

- **Design review** (3 reviewers, before implementation). The normal recipe as first written gave downward normals
  (M has determinant -1); no listed test could detect a mirrored world (the minimap test was added); real-tile winding
  tests were data-independent; the identity tolerance had thin margin (0.002 -> 0.01, a mismatch now fails the whole
  load); "exact multiples of T" was false; plus 20 IPC and player-integration findings (stale semantics, parked
  player, client identity in the report, testable validation, camera and mask ownership, layer assignment, report
  timing, commit point, lifetime proof). All folded into the design before code.
- **Core and host** (5 dimensions, 26 findings, 18 confirmed, all minor, all fixed): NaN and short-record holes in the
  comparison tool; the reference skipping fewer malformed chunks than C#; a precision test that never read the built
  vertices; the "first read" timing marking a warm WDT; unreachable in-flight clear/model steps; leak checks that passed
  without counters or by re-taking a baseline; a check that could not prove "unread"; a cleared model failure notice; a
  wrong citation; run-script clean-up by path.
- **Player** (4 dimensions, 12 verdicts, 8 distinct confirmed, fixed): the runtimeState terrain counts the harness
  requires were missing (major); the on-screen character's pending scene was cancelled by a terrain load; a mount
  re-fit skipped during the terrain view was lost; the wire nudge's cost statement was wrong; no Unity-side hole test,
  no occluder test for the wire view, loose failure-reason tests, no evidence for the deferred-destroy path.
- **End to end**: the "nothing drawn from below" expectation was wrong (steep faces do face such a camera; replaced by
  the footprint comparison of section 6); low-resolution hole masks exist in real data (section 7; earlier claims
  corrected); a "not installed locally" answer was timed as a first read and the run summary printed -1 placeholders
  (fixed); the checker counted harness skips as passes and pooled two clients' timings (fixed).

## 16. Unresolved, deferred, and the next stage's interface

**Unresolved data fields**
- MCNR channel interpretation (U9): not used; normals are geometry-derived.
- The 16-bit low-resolution hole layout: five real chunks Unsupported (section 7), with a contiguity lead to verify
  against the game's own picture before drawing them.
- The meaning of header +20..27 when flag 0x10000 is clear (read as nothing).

**Known limits of this stage**
- Tile-edge normals are one-sided; seams between two tiles are not built or tested.
- The host cannot check MAID membership of a root; a root of another map at the same x_y would pass the tile check.
- The wire views' depth nudge costs about k*D^2/near (k = 1e-4): edges far behind a ridge can show through at close
  zoom. Debug views only.
- The comparison tool's mesh-bounds tolerance (1e-4 yd plus two float ulps) assumes Unity only round-trips the bounds
  it is given.
- The Python reference copies the C# parser's malformed-chunk rules by hand.
- Round trips in a real viewer (20 ms poll) and a cold disk read were not measured.
- The out.json `passed` count is one below the log's RESULT line (the "results are written" check comes after it).

**Deferred items of the audit's gate G4**: two-tile seams -> the multi-tile stage (audit W6); a tilted WMO and a doodad
against MODF extents and MLDX in Unity space -> the placement stages (W7/W8).

**Interface for the next stage**
- `TerrainMeshBuilder.Build(root, tileX, tileY, origin)` takes the origin: a multi-tile scene passes one session
  origin for every tile, computes vertices in double against it and keeps every root at identity, so shared tile edges
  are the same expression and the same float on both sides; re-base by rebuilding when the camera leaves a radius.
  Edge normals must then be accumulated across loaded neighbours.
- `WmvTerrainSession` already keys tiles by (map, x, y); the host's one-load policy becomes a set of outstanding loads
  with no message change (each load names one tile).
- Layer 30 is the world layer; placed models need their layer set recursively.
- Serving many files needs the audit's storage-owning thread (W3) before any multi-tile view.
- The Maps viewer mode (W11) is the first user-facing entry point; `PlayerContent::Terrain` and the viewport notice
  rules already exist for it.

## 17. Follow-up: bounded terrain IPC and diagnostic bookkeeping

On top of the first stage 03 commit (`73cb7efd`), the terrain request and diagnostic bookkeeping was made bounded and
late answers were proven isolated. Terrain geometry did not change: the player's dumps of Retail 32_50 and 34_46, Era
32_50 and Beta 32_48 are byte-identical to the stage 03 dumps and bit-exact against the reference.

### 17.1 What was wrong (shown on `73cb7efd` before any fix)

| Finding | Where | Measured on the old code |
|---|---|---|
| **P.** The player's terrain session kept a `requests` table entry per request until its answer arrived; no load end (built, failed, timed out, superseded, cleared, model load, shutdown) removed it | `WmvTerrainSession.cs` (old :110, :253, :343) | 1,000 loads whose answers never arrive leave 1,000 entries in every ending; 438,120 B retained after 1,000 timed-out loads, 43.5 MB after 100,000 |
| Removing entries at load end alone would be wrong: map requests shared the ordinary request-id counter (`u<n>`) and the one answer callback, so an unclaimed late map answer would fall through to the world-model, dresser, mount, skin, anim and M2 paths | `WmvIpcClient.cs` (old :1744, `AnswerAsset`), `WmvMain.cs` `HandleAssetResponse` | an unknown map answer (role `wdt`, id `u999`) is not claimed and reaches those handlers |
| **H1.** The host kept a full record of every served map file for a player's whole life | `UnityIpcServer` (old `m_mapAssetsServed`, emptied only in `start()`) | 302 records after 150 loads, 2 per load |
| **H2.** The host's "first read" set was process-global and never emptied, keeping files of client epochs that were gone | `UnityIpcServer.cpp` (old :52 `g_mapAssetsRead`) | after two client switches it held 6 entries over epochs 2, 3 and 4 |

### 17.2 Design

- **Ownership by routing, not by memory.** Map requests use ids of their own, `map<n>` (`WmvIpcClient.cs:251`). An
  answer with such an id, or with a `role` (only `getMapAsset` answers carry one; `IsMapAnswer`, `:260`), goes only to
  `OnMapAssetResponse` (`:1269`): never into the asset cache, `AnswerAsset` or `OnAssetResponse`. `HandleAssetResponse`
  drops and counts any map answer that still reaches it, before every model path (`WmvMain.cs:399`).
- **One pending request, a bounded memory of ended ones.** The session has no request table. A load waits for one
  request at a time; when a load ends by any path, its unanswered request moves to a ring of the last 32 (`Retire`,
  `WmvTerrainSession.cs:759`; `RetiredCapacity`, `:85`). `ClaimMapAssetResponse` (`:303`) applies an answer only to the
  live load whose pending id it names and whose load serial it states; anything else is dropped and counted as late
  (its request is in the ring) or unowned. Exactly one `terrainTileLoaded` per load is unchanged. The 30 s timeout is
  unchanged (`-wmvTerrainTimeoutMs`, clamped to 1000-120000, shortens it for tests).
- **Host records.** The newest 256 records are kept (`MAP_ASSET_RECORDS_KEPT`, `UnityIpcServer.h:342`;
  `recordMapAssetServed`, `.cpp:1382`) with totals of all and of those no longer kept; the `-terraintile` harness keeps
  its own complete copy through `onMapAssetServed`.
- **First reads.** `MapAssetReadTracker` (`UnityIpcServer.h:308`) holds the FileDataIDs read under one client epoch, at
  most 8,192 (past that a read is reported untracked, never as a false first read). It is emptied when the client is
  replaced (`modelviewer.cpp:4388`), and a read under a newer epoch empties it as a backstop, so first reads never mix
  clients. It survives player restarts (the host's storage is the same).
- **Test hooks** (harness only, inert unless armed): the host can hold a request unanswered (at most 8) and answer it
  later through the normal decision and read; held requests are dropped, never sent, when the player connection ends.

### 17.3 Tests

- **Player self-test, 767 passed** (37 new): successful answers; an answer that never arrives (timeout); clear,
  supersede, model load, client replaced and shutdown before the answer; late answers after each; an answer naming the
  right request but another load; 1,200 failing loads with, after every load, the ended load's request in the ring and
  the retired total exactly one higher, conservation (requests sent = answers applied + retired + pending: 1,400 = 400 +
  1,000 + 0), and no collection of the session or of the load in flight growing (every collection type counted);
  routing through the real IPC client (with and without a role, with and without a handler) and through a real
  `WmvMain`, where a late map answer never reaches a model job even when it carries that job's own request id. Seven
  deliberate mutants (role-only routing, a guard without its id half, a `HashSet` request table, a cleared load not
  retired, a saturating total, no last-retired id, late and unowned swapped) each fail.
- **End to end** (final binaries, hidden desktop): every late step (`timeout`, `latesupersede`, `lateclear` for the WDT
  and the root ADT, with and without a model on screen; `lateswitch` for all six ordered pairs of Retail, Classic Era
  and Classic Beta) requires, against a sample taken before the held request: the retired total +1, the ring
  min(before + 1, 32), the last retired id = the held request, the late count +1, the unowned count unchanged, no second
  report, the model on screen unchanged: PASS 325/0 and 194/0. 34 timeouts in one player take the ring past its cap (32
  kept, 34 in all): PASS 595/0. `reconnect` with a held load: the held request is dropped unanswered, the tracker
  survives, the new player starts at 0. `failrepeat:40`: every load answered once, nothing retired. Host-side mutants
  (no reset on a client switch; late answers under a foreign id; no clear on a switch) each fail.
- **Stage sequences, final binaries**: the main lifecycle sequence PASS 141/0, all six client-switch pairs PASS 104/0,
  map 1051 31_29 x11 PASS 70/0; `check_terrain_run.py` over all 16 final runs PASS (17,298 passed, 0 failed, 15 skipped:
  11 map-1051 corpus comparisons, 4 Era/Beta precondition skips).
- **Regression**: `-mo bear` and `-wmo 108631 -unityipctest` PASS; the C# runner (1,179 checks) and the Python tests
  (54) pass.

### 17.4 Real memory

Three runs, each a fresh viewer and player: 200 successful loads, 200 failing loads, 20 timeouts, three Classic Beta and
three Classic Era round trips, four reconnects, then 100 more loads (553 loads per run). MB = 10^6 bytes.

| Phase | Host private bytes change (3 runs) | Player |
|---|---|---|
| 200 successful loads | +0.00 / +0.00 / +0.00 MB; +0.00 over the last 50 loads | native 142.6-143.3 MB, last 25 vs first 25 loads within 0.03 MB; managed median about 8.0 MB |
| 200 failing loads | +0.30 / +0.79 / +0.79 MB; +0.00 over the last 50 | native within 0.002 MB |
| 20 timeouts | +0.89 / -0.69 / +0.48 MB | retired 20, late 20 |
| 12 client switches | +51.1 / +89.4 / +60.1 MB (see below) | |
| 4 reconnects | +0.00 MB | each new player starts at 0 pending, 0 retired |
| 100 more loads | +0.00 / +0.30 / +0.18 MB; +0.00 over the last 50 | native within 0.03 MB |

The bookkeeping stayed bounded throughout: host records at most 256 (672 served to the first player), tracker at most
2 files, held at most 1, player ring at most 32; Mesh / Material counts 257 / 63 and terrain meshes / materials /
GameObjects 256 / 1 / 257 at the end of every phase, 1 / 62 and 0 / 0 / 0 after the final clear. The growth at client
switches is the clients' own load memory (the first Beta round trip +105 to +108 MB, later trips smaller, the Era trips
within 2.5 MB after a drop); it is not this bookkeeping, and it is not explained here.

### 17.5 Remaining

- MoP Classic could not be exercised: its storage does not open on this machine ("Opening ... failed. Error 2").
- The memory a client switch leaves (17.4) is outside this follow-up and unexplained.
- `MapAnswersDroppedOnAssetPath` (the model-path guard's count) is in the player log only, not in `runtimeState`; no
  end-to-end run can reach that guard (routing sends map answers elsewhere first); the self-test covers it.
- The harness's ring constant (32) must follow the player's `RetiredCapacity`.
