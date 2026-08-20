# UnityRendererProject — the WMV Unity viewport player

This folder holds the **source-only** pieces of the Unity standalone player that WMV embeds
via `View → Unity Renderer`. The repo contains no Unity project boilerplate and no build
output — you create the project locally with your own Unity install and drop these
scripts in.

**Direction:** this player is WMV's **new renderer foundation and intended primary
viewport**; the OpenGL canvas remains the legacy/fallback viewport during the migration.
It renders **directly from WoW data**: it requests raw assets and metadata from WMV over
IPC (no OBJ/FBX/GLB export workflow). WMV provides the app UI, the active client/profile,
CASC/MPQ access, DB/metadata and the runtime commands; the player provides the modern
rendering pipeline. See `docs/unity-renderer/README.md`. For now the player is optional at
build and run time.

## Build steps

1. Install a Unity LTS (2021.3 or newer; any recent LTS works — the scripts use only
   core UnityEngine + .NET `TcpClient`).
2. Create a new **3D (Built-in Render Pipeline)** project named e.g. `WmvUnityRenderer`.
3. Copy the `Assets/Scripts/` folder from here into the project's `Assets/`.
4. Create an empty GameObject in the default scene and add the **WmvMain** component to
   it. (It builds the camera rig, light, test cube, status overlay and the IPC client at
   runtime — no other scene setup needed.)
5. **Player Settings** (Edit → Project Settings → Player):
   - **Resolution and Presentation → Run In Background: ON** (required — otherwise the
     player pauses whenever the WMV window has focus, which is always).
   - Fullscreen Mode: *Windowed*.
6. **File → Build Settings → Windows x86_64 → Build**, and build **into**
   `tools\unity-renderer\` next to `wowmodelviewer.exe`, with the executable named
   `UnityRenderer.exe`.
   (Alternatively build anywhere and set `Tools/UnityRendererPath` in
   `userSettings\Config.ini` to the exe's full path.)

Do **not** commit the build output (exe, `UnityRenderer_Data/`, `UnityPlayer.dll`, …) to
this repository.

## Scripts

| File | Role |
|---|---|
| `WmvMain.cs` | Bootstrap: camera rig, light, test cube, on-screen status text, wires the IPC handlers. On `loadWoWModel` it requests the model raw bytes from WMV and reports byte length + SHA-1 (V1: no parsing/rendering yet). |
| `WmvIpcClient.cs` | IPC client (protocol v1): connects back to the WMV server given by `-wmvPort`, sends `unityReady`, receives `loadWoWModel`, sends `getAsset` / `getAssetByFileDataID`, decodes + hash-checks `assetResponse`. |
| `WmvOrbitCamera.cs` | Orbit / pan / zoom controls for the viewport. |

## How embedding works

WMV launches the player with the standard standalone-player embedding arguments:

```
UnityRenderer.exe -parentHWND <decimal hwnd> delayed -logFile <...>\userSettings\unityRenderer.log -wmvPort <port>
```

The player creates its window as a child of the given WMV panel; WMV resizes that child
window whenever the pane resizes and sends it `WM_CLOSE` on shutdown.

## IPC

**WMV is the server.** It listens on `127.0.0.1` (ephemeral port) before launching the player
and passes `-wmvPort <n>` on the command line; `WmvIpcClient` connects back, sends
`unityReady { protocolVersion: 1 }`, and then requests raw WoW files with `getAsset` /
`getAssetByFileDataID` (answered by `assetResponse` with base64 bytes + SHA-1). Newline-delimited
JSON; full vocabulary and semantics in `docs/unity-renderer/README.md`. Run the player without
`-wmvPort` and it runs standalone (test scene, no WMV connection).

## TestStub

`TestStub/` contains a small Win32 program that honours the same contracts as the player:
the `-parentHWND` embedding (child window, fills the parent, exits on `WM_CLOSE`) AND the v1
IPC (connects to `-wmvPort`, sends `unityReady`, answers `loadWoWModel` with `getAsset`, logs
and displays the `assetResponse` byte length / SHA-1 / decode check). It also accepts
`-wmvSelfTest`, which WMV passes **only** for its `-unityipctest` diagnostic run: the stub then
additionally probes the negative paths (the same asset by FileDataID, a missing path, an
unknown message type). Without that flag -- i.e. every normal `View -> Unity Renderer` launch
-- it makes exactly one request per model and the viewport shows only the real exchange.
Build it with any MSVC prompt:

```
cl fake_unity_renderer.c user32.lib gdi32.lib shell32.lib ws2_32.lib /Fe:UnityRenderer.exe
```

Drop the result at `tools\unity-renderer\UnityRenderer.exe` to test the WMV side without
installing Unity -- interactively via `View -> Unity Renderer`, or headlessly with
`wowmodelviewer.exe -mo creature/chicken/chicken.m2 -unityipctest` (logs `[unityipc-test]
RESULT: PASS|FAIL` in `userSettings\log.txt`; the stub own log is
`userSettings\unityRenderer.log`).
