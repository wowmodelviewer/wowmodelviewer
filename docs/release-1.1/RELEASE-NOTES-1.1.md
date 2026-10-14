# WoW Model Viewer 1.1

Version 1.1 adds Quick export to the Texture Viewer, makes Classic Beta's equipment browsable, and fixes item
replacement and blank names in some database rows. These notes list the changes since 1.0; every merged pull
request is in `changelog-pr100-pr106.txt` next to this file.

## Highlights

- **Quick export in the Texture Viewer.** A Quick export box beside Export PNG and Export BLP saves the texture at
  once, with no Save As dialog, into the texture export folder, in the texture's own folder from the game (for
  example `creature\bear\bearskinblack.png`). The folder is `Exports\Textures` in the viewer's folder at first;
  Options > Settings... > Export chooses another, with Browse..., Reset to default and Open folder. Ctrl+S,
  Ctrl+Shift+S and Browse's texture menu follow the box. After every texture export a notice in the bottom-left
  corner of the preview names the file, with Open folder; a failed export says why.
- **Classic Beta equipment.** The equipment windows of a Classic Beta character used to list only "None": the
  viewer could not read Classic Beta's item tables. They now list the client's items (18,082 on builds
  1.60.1.70338 and 1.60.1.70374), with search by name or ID, categories, equip, replace and remove. View > View
  Item opens, and Character > Load Item Set equips the sets whose pieces have names. No data from another client
  is used.

## Fixed

- **Replacing an item with one that has no appearance** in the loaded client kept the previous item's look under
  the new item's name (for example the Wolfshead Helm stayed on the head). Such an item now shows no look, and a
  hand is left open. This applies to every client.
- **Blank names.** Text in database rows stored after a game table's first section read as empty. Some NPCs (View
  NPC) and mounts (the mount list) now show their names, though these mounts still have no model the viewer can
  show; Load Item Set lists 8 more Retail transmog sets that were left out (on the test machine most of them have
  no pieces the viewer can put on).

## Changed

- **The first load of each client after the update builds its database cache again**, once: on the test machine
  about 15-20 seconds for Retail and a few seconds for a Classic client.
- **Rows the computer cannot decrypt yet** are recorded in the database cache with the key they need; when a later
  load knows the key, the cache is built again so those rows are read.
- **Loading a character or equipment file in another client than the one it was saved in**: an item saved with
  no appearance now gets its own appearance in the client that loads it, instead of being forced to show
  nothing.
- **Options > Settings... > Export** is laid out in sections and scrolls when the Settings pane is short.

## Groundwork for a map viewer

1.1 contains the first stages of a map viewer: an audit of the game's map formats, readers for them, each client's
list of maps, and the viewport's drawing of one map tile with its ground textures. None of it can be reached from
the window yet. Apart from reading each client's list of maps as it loads (a few milliseconds), it runs only in
headless test modes. The program and the viewport player were updated together for it, and the installer installs
both.

## Known issues

- The Models button of the viewer selector in the command bar sometimes shows no icon. Only the icon is
  affected.
- FBX export is not included (0.11.0 had it). OBJ export (File > Export Model) remains.
- Models from a legacy MPQ client (File > Load Legacy MPQ Client...) cannot be shown in the viewport: they have
  no FileDataID, which the viewport needs.
- Classic Beta: 10,038 of its 18,082 listed items have no name in the client's own data and are listed as
  "Unnamed item"; the item filter finds them by ID. Load Item Set equips only pieces that have a name, so 121 of
  its 536 item sets resolve fully, 32 partly and 383 not at all.
- Classic Beta lists Gilnean and Visage under Playable Races, although its data marks them not playable. Blood
  Elf is under NPC Races there and has only its High Definition model: that is what Classic Beta's own data says.
- A character or equipment file saved with 1.0 while an item without an appearance showed the previous item's
  look may still have that look saved in it: remove the item from its slot, choose it again and save the file
  anew.
- Character > Load Start Outfit lists no outfits in any client.
- The viewport player keeps a graphics cache in Windows' temporary folder (`%TEMP%\WoW Model Viewer`), which
  uninstalling does not remove; it can be deleted at any time.
- The installer is not code-signed. Windows SmartScreen may show "Windows protected your PC": choose More info,
  then Run anyway.

## Upgrading from 1.0

- Close the viewer, then run the 1.1 installer and keep the folder it suggests (1.0's folder): it updates 1.0 in
  place; there is no need to uninstall first. Your settings in `userSettings\`, your database caches, the file list
  and your exported textures are kept. Choosing another folder installs a second copy and leaves 1.0 where it
  is.
- The first load of each client builds its database cache again, once.
- Upgrading from 0.11.0 works as described in the 1.0 release notes.

## System requirements

- Windows 10 version 21H1 (build 19043) or later, or Windows 11, 64-bit (x64).
- A graphics card with DirectX 11 or 12 support and its vendor's current driver.
- World of Warcraft (Retail or Classic) installed through Battle.net, with its game data downloaded.
- An internet connection for the first client load, which downloads about 150 MB.
- About 150 MB of disk space for the program, plus about 150 MB for the file list and a database cache for each
  client build you open.
- No administrator rights: the installer installs for your Windows account only.

## What it connects to

Unchanged from 1.0. There is no update check, no account and no analytics. When a client is loaded, the viewer
checks for a newer community file list (downloaded again only when it has changed) and fetches the encryption key
list; once both have answered, this is not repeated for an hour in that session. Only when you use them: Armory
import (the WoW Model Viewer Armory proxy, which receives the region, realm and character name you enter), Wowhead
links (that page), and Armory item links (the WoW Model Viewer website). The viewport player talks only to the
viewer, on this computer.

## Download

`WoWModelViewer-Setup-1.1.exe` (41.1 MB). SHA-256:
`da82002c9406d22dbe713755cb0b833989cdf094ec78b6c93c3b25a903b7029b`

## Source

The source code is at https://github.com/wowmodelviewer/wowmodelviewer. The 1.1 viewport player, like 1.0's, was
built with the screenshot code of branch `feature/unity-screenshot-glow-alpha` (commit `a2ae7b3f`:
`Tools/UnityRendererProject/Assets/Scripts/WmvScreenshot.cs` and `Assets/Resources/WmvScreenshotMatte.shader`, the
transparent screenshot rendered over black and white); the rest of the player is `Tools/UnityRendererProject` of
the release commit.

## Licence

WoW Model Viewer is free software under the GNU General Public License, version 3 or (at your option) any later
version, and comes with no warranty. See LICENSE.txt and THIRD-PARTY-NOTICES.txt in the program folder.

WoW Model Viewer is a free, fan-made tool. It is not affiliated with or endorsed by Blizzard Entertainment. World
of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc.
