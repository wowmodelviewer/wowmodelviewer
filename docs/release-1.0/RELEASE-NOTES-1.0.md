# WoW Model Viewer 1.0

Version 1.0 replaces the old OpenGL viewport with a new renderer, opens Classic clients, adds viewers for
textures and buildings, and gives the window a new look. These notes list the changes since 0.11.0.

## Highlights

- **A new viewport.** Models are drawn by an embedded renderer built on the Unity engine, which reads the game
  data directly. It replaces the OpenGL viewport. It draws creatures with their skins and variants, items, NPCs,
  and playable characters with their customization, equipment and attached items, and shows their animations
  (including those kept in separate `.anim` and `.skel` files), particles and glowing eyes. If the renderer
  stops, the viewport offers a restart button; View > Restart Unity Renderer does the same.
- **Characters on mounts.** A character can ride a mount in the viewport. Model > Appearance starts with a Mount
  card, which opens a searchable mount picker and dismounts in one click.
- **Classic clients.** Classic Era, Mists of Pandaria Classic, Classic Beta and the other Classic lines open as
  themselves, with their own models, database tables and character customization. The status bar names the
  loaded client, and a command the client cannot support is greyed out with the reason.
- **A new client chooser.** File > Load World of Warcraft shows each installed product as a card, found through
  Battle.net and the folders used before, including a second install in its own folder. A product whose game
  data is not downloaded is listed with the reason. Technical details are under "Advanced", and the loading
  progress is shown in the same window.
- **Faster loading.** The game database of each client build is cached in `dbcache\` and reused on later loads.
  The file list is read once per session and the file index is built in one pass. The file list and encryption
  keys are checked for updates once per session instead of on every load.
- **Textures viewer.** A second viewer mode lists every BLP texture of the loaded client and shows the one you
  pick in place of the viewport: alpha on, off or only, over a checkerboard, dark or light background, with its
  size, format and FileDataID. Export PNG writes the texture with its alpha; Export BLP writes the original file.
- **Buildings viewer.** A third viewer mode lists the client's world models (WMO: buildings, dungeons, cities)
  and draws the one you pick in the viewport, as static geometry.
- **Load NPC / Model.** Character > Load NPC / Model... loads an NPC from a Wowhead link, or a model by its M2
  FileDataID, Creature Display ID or Creature Model ID, looked up in the loaded client. When an ID cannot be
  loaded, the window says why.
- **Armory import by name.** Character > Import Armory Character asks for the region, realm and character name,
  with realm names completed from the region's realm list. It reads the Armory's current links, says why an
  import failed, and accepts realms with accented names. Pasting a link still works.
- **Character and NPC appearance.** Classic characters get all their customization choices. Humanoid NPCs such
  as Bolvar and Garrosh wear their own face, hair and skin. Equipment looks the same every time it is loaded,
  races that share a model with another race load and import as themselves, and creatures get their fourth
  texture variation.
- **Item sets.** Character > Load Item Set includes transmog sets and filters by armour type. Where a set
  offers more than one item or appearance for a slot, you choose which. Item pickers also find items by their
  ItemID.
- **Screenshot.** The command bar's Screenshot saves a 3840 x 2160 PNG of the viewport with a transparent
  background.
- **Background colour.** View > Swap Background Color sets the colour behind models, with a colour picker, a hex
  field and presets of your own.
- **A new look and a dark theme.** The main window has a flat, modern design. View > Appearance offers System,
  Light or Dark; in Dark the whole window is dark, menus and dialogs included. The interface now uses
  wxWidgets 3.3.3.
- **A normal window.** The viewer opens maximised with its title bar. View > Fullscreen (F11) gives the
  borderless mode, and Esc leaves it.
- **Stability.** Fixes for crashes when a model file cannot be read or when choosing None in the mount list,
  and for memory errors when switching away from a building. Loading another client now replaces everything of
  the one before, and Browse no longer keeps every tree it built in memory. The viewport player quits with the
  viewer: it no longer keeps running in the background after the viewer crashed or was ended.

## Removed

- **FBX export**, and the Install Blender Add-on command that went with it. OBJ export (File > Export Model)
  remains. If you rely on FBX export, keep using 0.11.0.
- **The OpenGL viewport** and the controls that only changed it: View > Background Color and Load Background,
  the Camera menu and the saved views (F1-F4), Set Canvas Size, and the Settings > Display page.
- **File > Save Screenshot (F12) and File > Export Image Sequence.** The command bar's Screenshot button
  replaces the first; image sequences are not available.
- **Browse's Show list.** Browse lists what the selected viewer shows. Sound, database, script, shader and map
  tile files are no longer listed; such a file can still be saved as it is from the Textures viewer by searching
  for its FileDataID.

## Known issues

- The Models button of the viewer selector in the command bar sometimes shows no icon. Only the icon is
  affected.
- FBX export is not in 1.0 (see Removed).
- Models from a legacy MPQ client (File > Load Legacy MPQ Client...) cannot be shown in the viewport: they have
  no FileDataID, which the viewport needs.
- The installer is not code-signed. Windows SmartScreen may show "Windows protected your PC": choose More info,
  then Run anyway.

## Upgrading from 0.11.0

- Close the viewer, then run the 1.0 installer; there is no need to uninstall 0.11.0 first. The folder page
  suggests 0.11.0's folder: keep it to update in place (the Start menu entry is renamed "WoW Model Viewer"). To
  move to another folder, uninstall 0.11.0 first. (If Windows no longer lists 0.11.0 under installed apps, Setup
  suggests a new folder and 0.11.0 stays where it is.)
- You can install into any folder you can write to. Program Files will not work: the viewer keeps its settings
  and downloads next to itself.
- Your settings in `userSettings\` are kept. The window layout saved by 0.11.0 is reset once to the new default,
  and settings that only applied to the old viewport are ignored.
- The installer removes 0.11.0's FBX exporter and the libraries 1.0 no longer uses.
- The first client load after the upgrade builds the new database cache, and downloads the file list if yours is
  out of date.

## System requirements

- Windows 10 version 21H1 (build 19043) or later, or Windows 11, 64-bit (x64).
- A graphics card with DirectX 11 or 12 support and its vendor's current driver.
- World of Warcraft (Retail or Classic) installed through Battle.net, with its game data downloaded.
- An internet connection for the first client load, which downloads about 150 MB.
- About 150 MB of disk space for the program, plus about 150 MB for the file list and a database cache for each
  client build you open.
- No administrator rights: the installer installs for your Windows account only.

## What it connects to

There is no update check, no account and no analytics. When a client is loaded, the viewer checks for a newer
community file list (downloaded again only when it has changed) and fetches the encryption key list; once both have
answered, this is not repeated for an hour in that session. Only when you use them: Armory import (the WoW Model
Viewer Armory proxy, which receives the region, realm and character name you enter), Wowhead links (that page), and
Armory item links (the WoW Model Viewer website). The viewport player talks only to the viewer, on this computer.

## Download

`WoWModelViewer-Setup-1.0.exe` (40.7 MB). SHA-256:
`d0a9ebfe9853e2e90fd0040f57c3a5ba266eb9669ce94fce6b22b1e8e9ad65a9`

## Licence

WoW Model Viewer is free software under the GNU General Public License, version 3 or (at your option) any later
version, and comes with no warranty. See LICENSE.txt and THIRD-PARTY-NOTICES.txt in the program folder.

WoW Model Viewer is a free, fan-made tool. It is not affiliated with or endorsed by Blizzard Entertainment. World
of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc.
