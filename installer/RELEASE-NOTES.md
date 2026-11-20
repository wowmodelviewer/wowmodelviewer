## WoW Model Viewer 1.2 preview: native on macOS, Windows and Linux

This build draws the viewport natively with **Vulkan** (MoltenVK on macOS), with no Unity player, and runs on
**macOS, Windows and Linux**. It is a preview: models, characters and buildings are drawn natively; the world viewer
(Maps) comes in a later version.

### Downloads

| System | File | First start |
|---|---|---|
| macOS 12+ (Apple silicon) | `WoWModelViewer-1.2-macOS-arm64.dmg` | Drag the app to Applications. It has an ad-hoc signature only (no Apple Developer ID, not notarized): right-click it, choose **Open**, then **Open** again (or System Settings > Privacy & Security > **Open Anyway**). |
| Windows 10/11 x64 | `WoWModelViewer-1.2-Windows-x64.zip` | Unzip anywhere you can write to and run `wowmodelviewer.exe`. SmartScreen may warn: **More info** > **Run anyway**. Settings stay in that folder. |
| Linux x86-64 | `WoW_Model_Viewer-x86_64.AppImage` | `chmod +x` it and run it. Needs a Vulkan driver (Mesa or NVIDIA) and X11 or XWayland. |

Check a download against `SHA256SUMS.txt` (`shasum -a 256 <file>` on macOS, `sha256sum` on Linux,
`Get-FileHash` on Windows).

### What is new

- **Native Vulkan viewport** on every system: creatures, characters (composited skins, eyes, geosets, equipment,
  mounts), particles, ribbons, the M2 material combiners, and buildings (WMO groups and doodads). Screenshots,
  image sequences and the GIF/AVI exporters render offscreen at any size.
- **Every installation found**: Battle.net's own list of installs on every system, `/Applications/World of Warcraft`
  on macOS, and on Linux the Wine and Proton prefixes of Steam (every library, native and Flatpak), Lutris, Heroic,
  Bottles and `~/.wine`.
- **Every product**: Retail and its PTR/Beta, Classic, Classic Era, Anniversary and WoW Forever, and any newer
  `_name_` product listed in an installation.
- Settings and caches in your own folder on macOS (`~/Library/Application Support/WoW Model Viewer`) and Linux
  (`~/.local/share/wowmodelviewer`).

### Known issues

- **Maps** (the world viewer) is not available yet; Browse still lists each client's maps.
- The viewport uses a fixed preview light; the Lighting panel's lights are not used yet.
- FBX export is not included (OBJ export is).
- Not signed with a publisher identity on any system (see First start).

Report problems on the [issue tracker](../../issues): say which system, client and model, and attach
`userSettings/log.txt` from the folder listed above (on Windows, next to the program).
