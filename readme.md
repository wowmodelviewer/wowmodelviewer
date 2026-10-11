# WoW Model Viewer

WoW Model Viewer is a free Windows program for viewing the 3D models of World of Warcraft from your own game
installation. It shows creatures, NPCs, items, mounts, spell effects and buildings, and playable characters with
their customization, equipment and animations. It reads Retail and Classic clients installed through Battle.net,
draws its viewport with an embedded renderer built on the Unity engine, lets you browse the game's textures, and
exports models to OBJ, textures to PNG or BLP, and screenshots to PNG.

## Download and install

Download the installer from the [Releases page](https://github.com/wowmodelviewer/wowmodelviewer/releases) and
run it.

- **Requirements:** Windows 10 (version 21H1 or later) or Windows 11, 64-bit; a graphics card with DirectX 11 or
  12 support and its vendor's current driver (the viewport needs it); World of Warcraft installed through
  Battle.net.
- **Per-user install, no administrator rights.** The installer installs for your Windows account only, under
  `%LOCALAPPDATA%\Programs` by default; you can choose another folder, as long as you can write to it (the viewer
  keeps its settings and downloads next to itself, so Program Files will not work). Running it over version 0.11.0
  upgrades that install in place, in its folder, and keeps your settings.
- **Not code-signed.** Windows SmartScreen may show "Windows protected your PC". Choose **More info**, then
  **Run anyway**. Only download the installer from the Releases page above; each release lists the installer's
  SHA-256.
- To uninstall, use Windows Settings > Apps > Installed apps.

## First steps

1. Start WoW Model Viewer from the Start menu. It opens with an empty viewport.
2. Choose **File > Load World of Warcraft**. The "Choose World of Warcraft" window shows each installed product
   as a card, found through Battle.net and the folders you used before. Click a card, or select it and press
   Enter. A product whose game data is not on the computer is listed with the reason; start it once from
   Battle.net to download its data. **Browse for another installation...** opens an install in another folder.
3. A first load takes longer, and the chooser shows its progress:
   - the first time a client is opened, the viewer downloads the community file list (about 150 MB) and the
     encryption key list, so that load needs an internet connection. Later it checks for newer lists, and
     downloads the file list again only when it has changed;
   - the first load of each client build makes a database cache for it in `dbcache\`. Later loads of the same
     build reuse it; after a game patch it is made once more.
4. Pick a model in the **Browse** panel. The viewer selector in the command bar switches between **Models**,
   **Textures** and **Buildings**. The **Character** menu loads characters, imports Armory characters and loads
   NPCs or models by ID. The **Model** panel holds appearance and equipment, the **Animation** panel the
   animations. The command bar's **Screenshot** saves a PNG of the viewport.

Supported clients: Retail with its PTR and Beta, and the Classic lines, such as Classic Era and Mists of Pandaria
Classic, each read with its own models, database and character customization. A command a client cannot support
is greyed out, with the reason in the status bar; Armory import works for Retail characters only.
**File > Load Legacy MPQ Client...** opens an older MPQ-based install, but the viewport cannot show its models
(see Known issues).

## What it connects to

WoW Model Viewer has no update check, no account and no analytics. It connects only:

- **When a client is loaded:** to check for a newer community file list (about 150 MB, downloaded again only when
  it has changed) and to fetch the encryption key list (about 1 MB). Once both have answered, this is not repeated
  for an hour in that session; after a failed check it is tried again on the next load.
- **Character > Import Armory Character:** to the WoW Model Viewer Armory proxy, which looks the character up
  through Blizzard's API. It receives the region, realm and character name you enter, and also serves the realm
  lists.
- **Loading an NPC or an item from a Wowhead link:** to that page.
- **Importing an item from an Armory item link** (Import from URL in the item picker): to the WoW Model Viewer
  website, for the item's details.

If a download fails, or stalls for 30 seconds, the viewer keeps the copies already on disk. The viewport player
(`tools\unity-renderer`) talks only to the viewer, over a connection on this computer (127.0.0.1); its Unity
analytics and diagnostics are turned off.

## Where files are kept

The viewer keeps everything it writes in its program folder, next to `wowmodelviewer.exe`:

- `userSettings\Config.ini`: settings and the window layout.
- `userSettings\log.txt`: the viewer's log (**File > View Log**), and `userSettings\unityRenderer.log`: the
  viewport player's log.
- `userSettings\ArmoryRealms.json`: realm lists for Armory import, kept for a week.
- `dbcache\`: one database cache per client build, at most six. You can delete it; it is built again on the next
  load.
- `listfile.csv` (with `listfile.csv.etag` and `listfile.csv.order`) and `extraEncryptionKeys.csv`: the
  downloaded file list and encryption keys.

The viewport player also keeps its window settings (and the Unity engine's own session counters) in the registry,
under `HKEY_CURRENT_USER\Software\WoW Model Viewer`. Uninstalling removes all of these too.

## Known issues in 1.0

- The **Models** button of the viewer selector in the command bar sometimes shows no icon. Only the icon is
  affected.
- FBX export is not in 1.0 (0.11.0 had it), and neither is the Blender add-on installer that went with it. OBJ
  export (**File > Export Model**) remains.
- Models from a legacy MPQ client cannot be shown in the viewport: they have no FileDataID, which the viewport
  needs.
- The installer is not code-signed, so SmartScreen may warn before it runs (see Download and install).

To report a problem, open an issue on the
[issue tracker](https://github.com/wowmodelviewer/wowmodelviewer/issues). Say which client and model you used,
and attach `userSettings\log.txt` and `userSettings\unityRenderer.log`.

## Building from source

The viewer is C++ built with CMake. The viewport is a separate Unity player.

You need:

- Visual Studio 2022 with the C++ desktop workload (x64), and CMake 3.16 or newer.
- Qt 5.13.2 for MSVC 2017 64-bit.
- wxWidgets 3.3.3: the official prebuilt x64 binaries for vc14x (headers, Dev and ReleaseDLL), unpacked in
  `ThirdParty\wxWidgets33`, which is not in git.
- GLEW from vcpkg, triplet `x64-windows-static-md`, and libjpeg-turbo and libpng from vcpkg, triplet `x64-windows`
  (CxImage links their `jpeg.lib` and `libpng16.lib`).

Point the build at your Qt and vcpkg installs (see the top of `CMakeLists.txt`), then:

```
cmake -S . -B build64 -G "Visual Studio 17 2022" -A x64
cmake --build build64 --config Release
```

If configuring stops on `bin_support\wow\listfile.csv`, which is not in git, put a copy of the community file
list there first. The FBX exporter in `Source\plugins\exporters\fbx` needs the Autodesk FBX SDK, which is not in
the repository; it is built only when the SDK is there (`WMV_BUILD_FBX_EXPORTER`) and is not part of 1.0.

The viewport player's scripts and shaders are in `Tools\UnityRendererProject`, whose README describes how to
build them into `tools\unity-renderer\UnityRenderer.exe` next to `wowmodelviewer.exe`. The 1.0 player is built
with Unity 6000.5.9f1 and the Universal Render Pipeline.

To make the installer, install Inno Setup 6.7 or later and run `installer\build-installer.ps1` with the build
folder, the built player and the OpenSSL 1.1.1 x64 DLLs. It stages the release folder file by file (the list in
the script is also what a build needs next to `wowmodelviewer.exe` to run) and writes
`installer\dist\WoWModelViewer-Setup-<version>.exe` with its SHA-256.

## Licence

WoW Model Viewer is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License, version 3 or (at your option) any later version. It comes with no warranty. See
[LICENSE](LICENSE).

The program includes components by other authors, among them Qt, wxWidgets and the Unity runtime, each under its
own licence. See [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt), which the installer also puts in the
program folder.

## Not affiliated with Blizzard

WoW Model Viewer is a free, fan-made tool. It is not affiliated with or endorsed by Blizzard Entertainment. World
of Warcraft and Warcraft are trademarks or registered trademarks of Blizzard Entertainment, Inc.

The viewer reads game data from your own World of Warcraft installation; the game's files are not included.

## Credits

Credits to Linghuye, nSzAbolcs, Sailesh, Terran and Cryect for their direct and indirect contributions.

Developers: Ufo_Z, Darjk, Chuanhsing, Kjasi (A.K.A. Sephiroth3D), Tob.Franke, Jeromnimo, Wain and Rasmuslnd.

THIRD-PARTY-NOTICES.txt credits the components and data the program includes.
