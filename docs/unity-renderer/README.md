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
```

**WMV -> player**

```json
{ "type": "loadWoWModel", "path": "creature/chicken/chicken.m2", "fileDataID": 123200, "client": "active" }
{ "type": "assetResponse", "requestId": "abc123", "ok": true, "path": "creature/chicken/chicken.m2",
  "fileDataID": 123200, "byteLength": 101840, "sha1": "1dc88a19...", "encoding": "base64", "data": "TUQyMb..." }
{ "type": "assetResponse", "requestId": "abc123", "ok": false, "error": "not found" }
```

Semantics:

- `unityReady` is answered by a `loadWoWModel` for whatever is on the canvas (and every later
  model load pushes a new one). `client` is `"active"` -- the player never chooses a client;
  WMV's active client/profile is the only data source.
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

## Migration roadmap

- **V0 (merged):** `View -> Unity Renderer` opens a dockable pane, launches the player
  embedded in it, resize/shutdown work, missing player handled gracefully; player shows a
  test scene. Unity optional at build and run time.
- **V1 (this branch): runtime asset access.** WMV hosts the IPC server, the player connects
  back, announces `unityReady`, receives `loadWoWModel` and fetches the model's raw bytes with
  `getAsset` / `getAssetByFileDataID`, served by WMV from the active client (CASC or MPQ) and
  verified by byte length + SHA-1 on the player side. No M2 parsing or mesh rendering yet.
- **V2: direct rendering.** Unity-side M2 + skin + BLP loaders built on these bytes (plus
  metadata requests added to the same channel as needed) render
  `creature/chicken/chicken.m2` directly from WoW data. Binary framing for asset payloads.
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
