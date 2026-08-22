# Embedded Unity Renderer

## Direction (read this first)

The embedded Unity viewport is the **new renderer foundation for WMV** and its **intended
primary viewport**. The existing OpenGL viewport (`ModelCanvas`) remains the **legacy /
fallback renderer during the migration** and is not where new rendering work goes.

Concretely:

- **Unity first.** Future rendering features — characters, equipment, maps, fog, OBS
  scenes and stream features — target the Unity viewport first. The OpenGL path receives
  maintenance only.
- **Unity renders directly from WoW data.** There is **no OBJ / FBX / GLB export workflow**
  between WMV and Unity. Unity requests the raw WoW assets (M2, skins, BLP textures, …) and
  metadata it needs from WMV over IPC and builds the scene from them. (WMV's export plugins
  remain a separate, user-facing feature; they are not part of the render path.)
- **Responsibility split** (detailed below): **WMV provides the application UI, the active
  client/profile, CASC/MPQ access, DB/metadata and the runtime commands; Unity provides the
  modern rendering pipeline and asks WMV for assets/metadata.**
- **Optional for now.** While the migration is in progress the Unity viewport is optional at
  build and run time: nothing in the normal WMV build depends on Unity (no Unity SDK, no
  binaries in the repo; the IPC server only adds the standard Windows socket library), and a
  missing player build produces a clear
  message while the app — and the legacy OpenGL viewport — carry on unchanged. "Optional"
  describes the current migration stage, not the destination.

## Responsibility split

| WMV (wxWidgets application) | Unity (embedded player) |
|---|---|
| Application UI (menus, panes, dialogs, settings) | Modern rendering pipeline (materials, lighting, post, HDR/PBR) |
| Active client / profile (retail, PTR, classic, legacy MPQ) | Scene graph: models, attachments, equipment, maps, fog |
| CASC / MPQ access — the only component that reads game archives | Cameras, orbit/controls, capture-friendly output (OBS, transparent/solid backgrounds, stream features) |
| Databases / metadata (DB2, DBC, listfiles, customization, display info) | Animation playback / skinning on the GPU |
| Runtime commands: what to show, customization/equipment, camera, capture | Requests raw assets / metadata from WMV (`getAsset`, `getAssetByFileDataID`); announces itself (`unityReady`) |
| Legacy OpenGL viewport (fallback during migration) | — |

Unity never parses CASC/MPQ itself and never depends on files WMV writes to disk: it
**asks WMV** for what it needs (raw file bytes by path or FileDataID, resolved metadata)
over the IPC channel, and WMV serves it from its existing file providers and database.

## Architecture

```
+----------------------------- WMV (wxWidgets) ------------------------------+
|  ModelCanvas (OpenGL)              |  UnityRendererHost (wxPanel)          |
|  LEGACY / FALLBACK during the      |  NEW RENDERER FOUNDATION              |
|  migration; maintenance only       |  - launches UnityRenderer.exe with    |
|  - app's single WGL context bound  |    "-parentHWND <hwnd> delayed        |
|    to this canvas' own HWND        |    -wmvPort <n>" (player reparents    |
|                                    |    itself INTO this panel, own        |
|                                    |    process + device)                  |
|                                    |  - resizes the embedded child window  |
|                                    |  - WM_CLOSE (+terminate fallback)     |
|                                    |    on app shutdown                    |
+----------------------------------------------------------------------------+
        ^ state                               | runtime commands      ^ asset/metadata requests
        | (unityReady)                         v (loadWoWModel)        | (getAsset,
        |                                                              |  getAssetByFileDataID)
        |                                                              v assetResponse (bytes)
   +----+---------------------------------------+----------------------+-----------+
   |         WMV asset-access / runtime API over local IPC (TCP, JSON lines)       |
   |     served by WMV from its CASC/MPQ file providers + GAMEDATABASE             |
   +-------------------------------------------------------------------------------+
                                   |
                                   v
   UnityRenderer.exe (separate process, own graphics device)
   - V0: blank scene + test cube (embedding proof)
   - V1: connects back, fetches the active model's raw bytes via getAsset (verified by
     byte length + SHA-1) -- runtime asset access proven, no parsing yet
   - V2+: M2 / skin / BLP / DB-driven loaders that build the scene straight from the
     bytes and metadata WMV serves -- no intermediate files
```

Why this is safe for the legacy viewport today: on Windows the OpenGL context is a single
process-global WGL context bound to the ModelCanvas's own HWND/HDC. The player runs
**out of process** and renders into **its own** child window with its own device, so the
two never share a device, context or pixel format. The Unity panel is a *sibling* of the
canvas in the wxAUI layout (the canvas stays the CenterPane for now; as the migration
progresses the Unity pane is expected to take over the primary viewport position, with
the OpenGL canvas retained as a fallback).

## Locating the player (current, optional stage)

1. `Tools/UnityRendererPath` in `userSettings\Config.ini` (when non-empty), else
2. `tools\unity-renderer\UnityRenderer.exe` next to the WMV executable.

Player logs go to `userSettings\unityRenderer.log` (next to WMV's own log). The player is
built locally from `Tools/UnityRendererProject/` — the repository contains **no** Unity
build output.

## IPC (protocol v1 -- implemented)

**WMV is the server.** `UnityRendererHost` starts a TCP listener bound to `127.0.0.1` on an
ephemeral port *before* launching the player and passes the port on the player's command
line (`-wmvPort <n>`); the player connects back. Localhost only, one client (the embedded
player). Transport: newline-delimited JSON, one object per line, UTF-8. Implemented by
`Source/wowmodelviewer/UnityIpcServer.*` (plain Winsock, polled from the GUI thread by a
wxTimer -- the app has no Qt event loop, and the game-file providers must be used from the
GUI thread anyway) on top of `UnityAssetAccess.*` (the narrow "raw bytes from the active
client" layer: CASC or legacy MPQ through the same `GAMEDIRECTORY` providers the OpenGL
viewport uses). Player side: `Tools/UnityRendererProject/Assets/Scripts/WmvIpcClient.cs`.

**Player -> WMV**

```json
{ "type": "unityReady", "protocolVersion": 1 }
{ "type": "getAsset", "requestId": "abc123", "path": "creature/chicken/chicken.m2" }
{ "type": "getAssetByFileDataID", "requestId": "abc124", "fileDataID": 123456 }
{ "type": "getModelTextures", "requestId": "abc125", "fileDataID": 123200 }
```

`getModelTextures` answers with `modelTextures { requestId, ok, fileDataID, textures:[{ index,
type, fileDataID, source }] }`. It exists because a modern M2 does **not** name its replaceable
textures: a creature skin's TXID entry is 0 and its texture array carries no filename, because
the actual skin comes from the client database.

`type` is the WoW texture **type** the texture feeds -- 11, 12, 13 for the three creature skin
slots -- not a position. A model's texture-variation order and its M2 texture-slot order need not
agree, so the renderer matches this against the type each M2 texture declares.

`source` says where the answer came from, in the order WMV tries them:

- **`source: "selection"` -- what the viewport is actually showing.** A creature normally has
  several skins (`chicken2` offers seven, plus a folder texture), and the database can only say
  which is the *default*. Which one is on screen is a UI fact, so WMV answers from its own skin
  selector -- the same `TextureGroup` the OpenGL viewport hands to `WoWModel::updateTextureList`.
  Without this the two viewports disagree the moment the user touches the dropdown, or whenever
  "Random Skins" picks something other than the first display.
- **`source: "database"` -- the model's default skin.** `CreatureDisplayInfo` joined to
  `CreatureModelData` on the model's FileDataID: the same relation the viewer's own skin list is
  built from. Used when there is no selection to read -- a model with no skin list, or a request
  about a model that is not the displayed one.
- **`source: "convention"` -- a labelled fallback, not a substitute.** A handful of legacy
  assets are still shipped in CASC but referenced by no creature display at all (for example
  `creature/chicken/chicken.m2`, superseded by `chicken2`), so the database has nothing to say
  about them. Rather than render them untextured, WMV looks for the conventional sibling skin
  in the listfile and marks the result `convention`, so the renderer -- and anyone reading the
  logs -- can tell a naming guess from database truth. A model that resolves only this way is
  a regression case, never the proof that texture resolution works.

The response carries metadata only; bytes are still fetched with `getAssetByFileDataID`.

**WMV -> player**

```json
{ "type": "loadWoWModel", "path": "creature/chicken/chicken.m2", "fileDataID": 123200, "client": "active" }
{ "type": "assetResponse", "requestId": "abc123", "ok": true, "path": "creature/chicken/chicken.m2",
  "fileDataID": 123200, "byteLength": 101840, "sha1": "1dc88a19...", "encoding": "base64", "data": "TUQyMb..." }
{ "type": "assetResponse", "requestId": "abc123", "ok": false, "error": "not found" }
{ "type": "modelSkin", "ok": true, "fileDataID": 1521037,
  "textures": [ { "index": 0, "type": 11, "fileDataID": 1521061, "source": "selection" } ],
  "geosets": [ 101 ], "hasGeosets": true }
```

Semantics:

- `unityReady` is answered by a `loadWoWModel` for whatever is on the canvas (and every later
  model load pushes a new one). `client` is `"active"` -- the player never chooses a client;
  WMV's active client/profile is the only data source.
- `modelSkin` is **pushed, not requested**: the skin on display changed. Same payload as a
  `modelTextures` reply, built by the same resolver, so the push and the pull cannot disagree.
  The player re-uploads only the textures that actually changed and keeps the mesh it built --
  a skin change alters which image a material samples, nothing about the geometry. It is sent
  from `AnimControl::SetSkin`, the single funnel every skin change goes through (the dropdown,
  the default chosen on model load, and NPC import), plus `SetSingleSkin` for the per-slot
  folder-texture lists.
- `geosets` / `hasGeosets` ride along with both `modelTextures` and `modelSkin`, because a display
  variant can differ from another by **geometry** rather than texture. `creature/horse3/horse3.m2`
  is the worked example: three of its dropdown entries share one texture and differ only in
  whether geoset 101, 102 or 103 is switched on -- a long mane and tail, or a cropped one.
  A submesh is drawn when **its geoset number is 0, or the variant switches that number on**,
  which is the legacy viewport's own rule (`WoWModel::setCreatureGeosetData`: every geoset in
  `[1, 900)` is shown iff the set names it, and `setLOD` starts them at `display = (id == 0)`).
  The renderer already knows every submesh's number from the .skin it parsed, so only the SET
  travels.
  `hasGeosets` separates two things an empty list cannot: `true` with an empty list means "this
  variant switches none on" -- which hides every submesh whose number is not 0 -- while `false`
  means the host had no creature selection to report and geoset visibility must be left alone.
- `getAsset` / `getAssetByFileDataID` return the **raw, whole file** exactly as stored in the
  active client (modern `.m2` bytes start with their `MD21` chunk header, etc.). Paths are
  normalised (lower-case, forward slashes). By-FileDataID works for CASC clients; a legacy
  MPQ client (name lookup only) answers `"FileDataID lookup is not supported by the active
  client (MPQ, name lookup only)"`. Other errors: `"not found"`, `"no game client loaded"`,
  `"game client is still loading"`, `"could not open file in the active client"`,
  `"short read ... file may be encrypted or damaged"`.
- `sha1` is the hex SHA-1 of the raw bytes; the Unity client recomputes it after decoding.
- **V1 carries the bytes as base64 inside the JSON line.** Simple and debuggable (~33 %
  overhead). A binary frame -- the same JSON header followed by a length-prefixed payload --
  can replace the `encoding`/`data` pair later without touching the request side.
- Nothing is written to disk on either side; this is runtime access, not an export workflow.

WMV logs every step with the `[unityipc]` prefix: listening port, player connected,
`unityReady`, each request (path / FileDataID), the provider used (`CASC` / `MPQ`), bytes
returned or the error. The player logs the same exchange (`userSettings\unityRenderer.log`
for the TestStub) and shows it as status text in the viewport.

**Normal launch vs. self-test.** A normal `View -> Unity Renderer` launch drives only the
happy path: the player connects, announces `unityReady`, receives `loadWoWModel` and issues a
single `getAsset` for that model. The protocol's error paths are exercised only in diagnostic
mode, where WMV appends `-wmvSelfTest` to the player command line and a diagnostic-capable
player (the TestStub) additionally probes a missing asset and an unknown message type. WMV's
handling of both is always present -- only the test *requests* are gated.

**Headless self-test.** `wowmodelviewer.exe -mo creature/chicken/chicken.m2 -unityipctest`
launches the installed player (TestStub or a real build) embedded in the off-screen frame with
`-wmvSelfTest`, drives the full exchange (connect, `unityReady`, `loadWoWModel`, `getAsset`,
`assetResponse`) plus the negative probes, checks the missing-asset and by-FileDataID paths
in-process, and shuts the player down. Result lines carry the `[unityipc-test]` prefix
(`RESULT: PASS|FAIL`).

## Status

**Implemented**

- Runtime CASC/MPQ asset access: the player asks WMV for raw WoW files over IPC and never
  touches game archives or the disk itself.
- Static M2 mesh rendering: the modern chunked M2 (MD21 / MD20 version 272 as current retail
  ships) and its .skin profile are parsed at runtime into a Unity mesh with per-batch
  submeshes, converted to Unity's coordinate system with corrected winding.
- Runtime BLP decoding for the formats the creature pipeline uses: palettized (alpha 0/1/4/8),
  DXT1 / DXT3 / DXT5 and raw BGRA, decoded straight from the received bytes into an in-memory
  texture. Anything else is reported by name instead of being decoded into garbage.
- Static materials taken from the model's own render state rather than approximated: every M2
  blend mode (opaque, alpha key, alpha blend, both additive forms, modulate, modulate 2x and
  premultiplied), the alpha test keyed where the legacy combiner keys it, depth write from the
  material's own flag alone, and two-sided when the model asks for it. Over a spread of 300
  retail creature models, 26.6% of draw batches ask for a blend mode that was previously drawn
  opaque -- an additive glow rendered opaque is a solid box where a wisp of light belongs.
- The M2 texture combiners a static pose can reproduce: the products of the two units, the two
  alpha-masked forms and the decal, with unit 1 sampled from whichever source the material's
  vertex shader names (either stored UV set, or a generated environment sphere map). That covers
  1421 of 1424 batches in the same sample; the remaining three are logged by name and drawn from
  unit 0 alone.
- Model textures the M2 does not name (replaceable creature skins) are resolved by WMV from
  the client database and handed to the player as FileDataIDs (`getModelTextures`), labelled
  `database` or -- for orphaned legacy assets only -- `convention`.
- Skinned rendering in the rest pose: the model's bone hierarchy is rebuilt as Unity transforms,
  the per-vertex influences and bind poses are handed to a SkinnedMeshRenderer, and the mesh is
  deformed by that rig instead of being drawn rigid. No animation track is evaluated yet, so
  every bone sits at rest -- and because an M2's stored vertex positions ARE that rest pose, the
  result is the static mesh it replaces to within float noise (largest measured deviation across
  the validation models: 3.8e-6 units). Models whose bones live in a separate skeleton file are
  still drawn static, and say so.
- Bounds-driven camera framing, so a loaded model is visible immediately.

**Not yet implemented**

- Animation playback. The rig is built and the mesh is skinned to it, but no animation track is
  read, so nothing moves; sequences, interpolation, timing and an idle loop are the next step.
- Bones from a separate skeleton file (the SKID chunk, and the SKPD parent it can defer to).
  Over a spread of 300 retail creature models, 299 keep their bones in the .m2 itself and one
  does not; that one is drawn unskinned.
- The rest of the WoW material system: texture animation, colour and transparency tracks beyond
  the rest-pose visibility gate, the specular lobes the legacy viewport leaves unweighted by
  default, and the few combiners that mix more than two contributing units.
- Particles and ribbons.
- The character / equipment pipeline (customization, attachments, geoset rules).
- Maps, terrain, WMOs, fog.
- Full parity with the legacy OpenGL renderer.

Unity remains the migration target and the intended primary renderer; the OpenGL viewport
remains the legacy/fallback renderer during the migration. There is no asset-export workflow:
every byte the renderer uses arrives over IPC at runtime and nothing is written to disk.

## Migration roadmap

- **V0 (merged):** `View -> Unity Renderer` opens a dockable pane, launches the player
  embedded in it, resize/shutdown work, missing player handled gracefully; player shows a
  test scene. Unity optional at build and run time.
- **V1 (this branch): runtime asset access.** WMV hosts the IPC server, the player connects
  back, announces `unityReady`, receives `loadWoWModel` and fetches the model's raw bytes with
  `getAsset` / `getAssetByFileDataID`, served by WMV from the active client (CASC or MPQ) and
  verified by byte length + SHA-1 on the player side. No M2 parsing or mesh rendering yet.
- **V2 (this branch): direct rendering.** Unity-side M2 + skin + BLP loaders built on those
  bytes render a WoW model directly from game data as a static mesh -- parsed, converted,
  textured and framed at runtime. Primary validation target is
  `creature/chicken2/chicken2.m2`, a current, database-backed creature: 1632 vertices, 796
  triangles, 2 submeshes, 2 materials (opaque + alpha), skin resolved through
  `CreatureDisplayInfo` to a 256x256 DXT5 texture. Binary framing for asset payloads is still
  open.
- **V3+ (Unity first):** characters + customization, equipment/attachments, animation,
  maps/terrain/fog, OBS-friendly backgrounds and scene/stream features -- each built on the
  Unity pipeline, with the legacy OpenGL viewport kept as fallback until parity.
- **Cut-over:** once Unity covers the baseline feature set, it becomes the default /
  primary viewport; the OpenGL canvas remains available as legacy/fallback.

Explicitly deferred until the direct renderer is solid: full maps/ADT terrain, WMO
placement, volumetric fog, armory donations, equipment.

## The player project

Source-only player pieces live in `Tools/UnityRendererProject/` (see its README for build
steps). `Tools/UnityRendererProject/TestStub/` contains a tiny Win32 stand-in that honours
the same `-parentHWND` embedding contract AND speaks the v1 IPC protocol (`unityReady`,
`getAsset` on `loadWoWModel`, `assetResponse` decode/length checks), so both the WMV-side
host and the runtime asset access can be exercised without any Unity install -- see the
headless self-test in the IPC section.
