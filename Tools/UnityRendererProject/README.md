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
   core UnityEngine + .NET `TcpListener`).
2. Create a new **3D (Built-in Render Pipeline)** project named e.g. `WmvUnityRenderer`.
3. Copy the `Assets/Scripts/` folder from here into the project's `Assets/`.
4. Create an empty GameObject in the default scene and add the **WmvMain** component to
   it. (It builds the camera rig, light, test cube and starts the IPC server at runtime —
   no other scene setup needed.)
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
| `WmvMain.cs` | Bootstrap: camera rig, light, V0 test cube, wires the IPC handlers. `loadWoWModel` is acknowledged with "not implemented yet" until the V1 loaders land. |
| `WmvIpcServer.cs` | IPC skeleton: runtime commands from WMV (`clearScene`, `loadWoWModel`, `setCamera`), state to WMV (`unityReady`, `loaded`, `error`), and the asset-channel requests to WMV (`getAsset`, `getAssetByFileDataID`). |
| `WmvOrbitCamera.cs` | Orbit / pan / zoom controls for the viewport. |

## How embedding works

WMV launches the player with the standard standalone-player embedding arguments:

```
UnityRenderer.exe -parentHWND <decimal hwnd> delayed -logFile <...>\userSettings\unityRenderer.log
```

The player creates its window as a child of the given WMV panel; WMV resizes that child
window whenever the pane resizes and sends it `WM_CLOSE` on shutdown.

## IPC

`WmvIpcServer` listens on `127.0.0.1:9500` (override with `-wmvPort <n>` on the player
command line) and speaks newline-delimited JSON — see `docs/unity-renderer/README.md` for
the full vocabulary. In V0 the player starts the listener and sends `unityReady` to
whoever connects; the WMV-side client and the asset serving land with V1.

## TestStub

`TestStub/` contains a ~150-line Win32 program that honours the same `-parentHWND`
contract (child window, fills the parent, exits on `WM_CLOSE`). Build it with any MSVC
prompt:

```
cl fake_unity_renderer.c user32.lib gdi32.lib shell32.lib /Fe:UnityRenderer.exe
```

Drop the result at `tools\unity-renderer\UnityRenderer.exe` to test the WMV-side
embedding (launch, docking, resize, shutdown) without installing Unity.
