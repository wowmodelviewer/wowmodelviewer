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
  build and run time: nothing in the normal WMV build depends on Unity (no SDK, no binaries
  in the repo, no new link dependencies), and a missing player build produces a clear
  message while the app — and the legacy OpenGL viewport — carry on unchanged. "Optional"
  describes the current migration stage, not the destination.

## Responsibility split

| WMV (wxWidgets application) | Unity (embedded player) |
|---|---|
| Application UI (menus, panes, dialogs, settings) | Modern rendering pipeline (materials, lighting, post, HDR/PBR) |
| Active client / profile (retail, PTR, classic, legacy MPQ) | Scene graph: models, attachments, equipment, maps, fog |
| CASC / MPQ access — the only component that reads game archives | Cameras, orbit/controls, capture-friendly output (OBS, transparent/solid backgrounds, stream features) |
| Databases / metadata (DB2, DBC, listfiles, customization, display info) | Animation playback / skinning on the GPU |
| Runtime commands: what to show, customization/equipment, camera, capture | Requests raw assets / metadata from WMV; reports state back (unityReady / loaded / error) |
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
|  - app's single WGL context bound  |    "-parentHWND <hwnd> delayed"       |
|    to this canvas' own HWND        |    (player reparents itself INTO      |
|                                    |    this panel, own process + device)  |
|                                    |  - resizes the embedded child window  |
|                                    |  - WM_CLOSE (+terminate fallback)     |
|                                    |    on app shutdown                    |
+----------------------------------------------------------------------------+
        ^ state                               | runtime commands      ^ asset/metadata
        | (unityReady, loaded,                v (clearScene,          | requests
        |  error)                               loadWoWModel,         | (getAsset,
        |                                       setCamera)            |  getAssetByFileDataID)
   +----+---------------------------------------+----------------------+-----------+
   |         WMV asset-access / runtime API over local IPC (TCP, JSON lines)       |
   |     served by WMV from its CASC/MPQ file providers + GAMEDATABASE             |
   +-------------------------------------------------------------------------------+
                                   |
                                   v
   UnityRenderer.exe (separate process, own graphics device)
   - V0: blank scene + test cube (embedding proof) + IPC skeleton
   - V1+: M2 / skin / BLP / DB-driven loaders that build the scene straight from the
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

## IPC

Transport: **localhost TCP** (default port `9500`, player argument `-wmvPort <n>` to
override), newline-delimited JSON. The player hosts the listener; WMV connects (the
WMV-side client lands with V1 — in V0 the player only starts the listener and announces
itself to whoever connects). Vocabulary is future-facing from V0 on:

**Runtime commands — WMV → player**

```json
{ "type": "clearScene" }
{ "type": "loadWoWModel", "sourcePath": "creature/chicken/chicken.m2", "fileDataID": 123456 }
{ "type": "setCamera", "position": [x, y, z], "rotation": [rx, ry, rz] }
```

V0 answers `loadWoWModel` with an `error` reading *"loadWoWModel not implemented yet"*;
V1 implements it by requesting the model's assets (below) and building the scene.

**State — player → WMV**

```json
{ "type": "unityReady" }
{ "type": "loaded" }
{ "type": "error", "message": "..." }
```

**Asset / metadata requests — player → WMV** (served by WMV from CASC/MPQ + database)

```json
{ "type": "getAsset", "path": "creature/chicken/chicken.m2" }
{ "type": "getAssetByFileDataID", "fileDataID": 123457 }
```

**Asset replies — WMV → player** (V1; payload framing — e.g. a JSON header followed by a
length-prefixed binary frame — to be finalised with V1)

```json
{ "type": "asset", "path": "creature/chicken/chicken.m2", "size": 183204 }   // + raw bytes
```

Metadata requests (resolved skins / display info / customization lookups) will be added to
the same channel as the loaders need them. The important property is that **the player only
ever sees what WMV serves at runtime** — the same data the legacy viewport renders from.

## Migration roadmap

- **V0 (this branch):** `View → Unity Renderer` opens a dockable pane, launches the player
  embedded in it, resize/shutdown work, missing player handled gracefully; player shows a
  test scene and hosts the IPC skeleton. Unity optional at build and run time.
- **V1:** WMV-side IPC client + asset-access/runtime API (`getAsset`,
  `getAssetByFileDataID`, metadata); Unity-side M2 + skin + BLP loaders render
  `creature/chicken/chicken.m2` **directly from WoW data served by WMV**.
- **V2+ (Unity first):** characters + customization, equipment/attachments, animation,
  maps/terrain/fog, OBS-friendly backgrounds and scene/stream features — each built on the
  Unity pipeline, with the legacy OpenGL viewport kept as fallback until parity.
- **Cut-over:** once Unity covers the baseline feature set, it becomes the default /
  primary viewport; the OpenGL canvas remains available as legacy/fallback.

Explicitly deferred until V0/V1 are solid: full maps/ADT terrain, WMO placement,
volumetric fog, armory donations, equipment.

## The player project

Source-only player pieces live in `Tools/UnityRendererProject/` (see its README for build
steps). `Tools/UnityRendererProject/TestStub/` contains a tiny Win32 stand-in that honours
the same `-parentHWND` embedding contract, so the WMV-side host can be exercised without
any Unity install.
