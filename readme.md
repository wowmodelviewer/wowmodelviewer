# WoW Model Viewer

WoW Model Viewer is a free program for macOS, Windows and Linux for viewing the 3D models of World of Warcraft from
your own game installation. It shows creatures, NPCs, items, mounts, spell effects and buildings, and playable
characters with their customization, equipment and animations. It reads Retail, Classic, Classic Era, Anniversary and
WoW Forever clients installed through Battle.net (on Linux: through Wine or Proton), draws its viewport natively with
Vulkan (MoltenVK on macOS), lets you browse the game's textures, and exports models to OBJ, textures to PNG or BLP, and
screenshots to PNG.

## Download and install

Download the build for your system from the [Releases page](https://github.com/hsmith-dev/wowmodelviewer/releases).
Nothing is code-signed with a publisher identity, so each system asks once whether to trust it. Only download from the
Releases page; each release lists the SHA-256 of every file.

- **macOS** (12 Monterey or later, Apple silicon; an Intel Mac can build from source): open the `.dmg` and drag **WoW Model Viewer** to
  Applications. The app carries an ad-hoc signature only (no Apple Developer ID, no notarization), so the first time,
  right-click it and choose **Open**, then **Open** again (or System Settings > Privacy & Security > **Open Anyway**).
- **Windows** (10 21H1 or later, 64-bit): run the installer. It installs for your account only, under
  `%LOCALAPPDATA%\Programs`. SmartScreen may show "Windows protected your PC": choose **More info**, then **Run
  anyway**. Uninstall from Settings > Apps > Installed apps.
- **Linux** (x86-64, with X11 or XWayland): make the `.AppImage` executable (`chmod +x`) and run it. A Vulkan driver
  for your GPU is needed (Mesa's RADV/ANV, or NVIDIA's), as for running the game itself under Proton.

All systems need a GPU with a Vulkan driver (on macOS, any Mac that runs Monterey: MoltenVK translates to Metal) and World
of Warcraft installed.

**Which installations are found.** File > Load World of Warcraft lists every client it finds: Retail and its PTR/Beta,
Classic (Mists of Pandaria Classic...), Classic Era, Anniversary and WoW Forever -- any product in an installation's
`.build.info`, each in its `_name_` folder. It looks in Battle.net's own list of installs, in the usual folders
(`/Applications/World of Warcraft` on macOS, `Program Files` on Windows) and, on Linux, in every Wine or Proton prefix
it can find: `$WINEPREFIX`, `~/.wine`, Steam's Proton prefixes in every Steam library (native and Flatpak), Lutris
(`~/Games`), Heroic and Bottles. **Browse for another installation...** opens any other folder.


## First steps

1. Start WoW Model Viewer (the Start menu, Applications, or the AppImage). It opens with an empty viewport.
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

If a download fails, or stalls for 30 seconds, the viewer keeps the copies already on disk. The viewport is part of the
program itself and connects to nothing.

## Where files are kept

Everything the viewer writes is kept in one folder of your own:

- **macOS:** `~/Library/Application Support/WoW Model Viewer`
- **Linux:** `~/.local/share/wowmodelviewer` (`$XDG_DATA_HOME`), or the program's own folder when a file named
  `portable` sits next to it
- **Windows:** the program folder, next to `wowmodelviewer.exe`

In it:

- `userSettings/Config.ini`: settings and the window layout; `userSettings/log.txt`: the log (**File > View Log**).
- `userSettings/ArmoryRealms.json`: realm lists for Armory import, kept for a week.
- `dbcache/`: one database cache per client build, at most six. You can delete it; it is built again on the next load.
- `listfile.csv` (with `listfile.csv.etag` and `listfile.csv.order`) and `extraEncryptionKeys.csv`: the downloaded
  file list and encryption keys.
- `Exports/Textures/`: what the Texture Viewer's **Quick export** saves. **Options > Settings... > Export** chooses
  another folder.

On macOS and Linux the folder also holds links (`games`, `dbd`) to the database schemas inside the program. To remove
everything, delete the folder.


## Known issues

- **Maps** (the world viewer) is not available yet: the viewport rebuilt natively draws models, characters and
  buildings; terrain, liquids, sky and fog follow in a later version. Browse still lists each client's maps.
- The viewport lights models with a fixed preview rig; the Lighting panel's lights are kept but not used.
- FBX export is not included (it needs Autodesk's SDK). OBJ export (**File > Export Model**) remains.
- Classic Beta and WoW Forever (the 1.60 clients): items without a name in the client's own data are listed as
  "Unnamed item".
- **Character > Load Start Outfit** lists no outfits in any client.
- Nothing is signed with a publisher identity (see Download and install).

To report a problem, open an issue on the [issue tracker](https://github.com/hsmith-dev/wowmodelviewer/issues). Say
which system, client and model you used, and attach `userSettings/log.txt`.


## Building from source

The viewer is C++17 built with CMake (3.21 or newer): wxWidgets 3.3 for the interface, Qt 5 (Core, Gui, Network, Xml,
Widgets) for data and networking, and Vulkan for the viewport (its shaders are compiled with `glslc` at build time).
CascLib, StormLib and CxImage are in `ThirdParty`. `.github/workflows/build.yml` builds all three systems and is the
reference for the exact steps.

**macOS** (Xcode command line tools, Homebrew):

```
brew install cmake ninja qt@5 wxwidgets molten-vk vulkan-loader vulkan-headers shaderc libpng jpeg-turbo
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open "build/Source/wowmodelviewer/WoW Model Viewer.app"
installer/macos/make-bundle.sh build dist     # the self-contained, ad-hoc-signed app and its .dmg
```

**Linux** (Ubuntu 24.04 package names; wxWidgets 3.3 built from source when the distribution has 3.2):

```
sudo apt install cmake ninja-build qtbase5-dev libgtk-3-dev libx11-dev libvulkan-dev glslc libpng-dev libjpeg-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DwxWidgets_CONFIG_EXECUTABLE=<wx prefix>/bin/wx-config
cmake --build build
installer/linux/make-appimage.sh build <wx prefix> dist
```

**Windows** (Visual Studio 2022, x64): Qt 5.15 for MSVC, the official wxWidgets 3.3 binaries (headers, Dev and
ReleaseDLL) unpacked in `ThirdParty\wxWidgets33`, the LunarG Vulkan SDK, and libpng/libjpeg from vcpkg:

```
vcpkg install libpng:x64-windows libjpeg-turbo:x64-windows zlib:x64-windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
      -DQT_LOCATION=<Qt>/5.15.2/msvc2019_64 -DWMV_VCPKG_X64=<vcpkg>/installed/x64-windows
cmake --build build --config Release
```

A headless run renders a model to a picture, which is how the viewport is checked without a window:
`wowmodelviewer -mo creature/chicken/chicken.m2 -screenshot chicken.png` (it loads the first client found).
`WMV_RENDER_DEBUG=1` logs what each capture draws; `WMV_VULKAN_VALIDATION=1` turns on Vulkan's validation layer.
See `docs/native-port/README.md` for how the viewport is built.


## Licence

WoW Model Viewer is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License, version 3 or (at your option) any later version. It comes with no warranty. See
[LICENSE](LICENSE).

The program includes components by other authors, among them Qt, wxWidgets, the Vulkan loader and MoltenVK, each under
its own licence. See [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt), which every package also carries.

## Not affiliated with Blizzard

WoW Model Viewer is a free, fan-made tool. It is not affiliated with or endorsed by Blizzard Entertainment. World
of Warcraft and Warcraft are trademarks or registered trademarks of Blizzard Entertainment, Inc.

The viewer reads game data from your own World of Warcraft installation; the game's files are not included.

## Credits

Credits to Linghuye, nSzAbolcs, Sailesh, Terran and Cryect for their direct and indirect contributions.

Developers: Ufo_Z, Darjk, Chuanhsing, Kjasi (A.K.A. Sephiroth3D), Tob.Franke, Jeromnimo, Wain and Rasmuslnd.

THIRD-PARTY-NOTICES.txt credits the components and data the program includes.
