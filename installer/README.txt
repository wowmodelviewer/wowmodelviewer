WoW Model Viewer 1.0
====================

WoW Model Viewer shows the 3D models of World of Warcraft from your own game
installation: creatures, NPCs, items, mounts, spell effects and buildings, and
playable characters with their customization, equipment and animations. It
reads Retail and Classic clients installed through Battle.net, lets you browse
the game's textures, and exports models to OBJ, textures to PNG or BLP, and
screenshots to PNG.

Project page, releases and issue tracker:
https://github.com/wowmodelviewer/wowmodelviewer


FIRST STEPS
-----------

1. Choose File > Load World of Warcraft. Each installed product is shown as a
   card; click one. A product whose game data is not on the computer is listed
   with the reason: start it once from Battle.net to download its data.
2. A first load takes longer. The first time a client is opened, the viewer
   downloads the community file list (about 150 MB) and the encryption key
   list, so that load needs an internet connection; later it downloads the
   file list again only when it has changed. The first load of each client
   build also makes a database cache for it, which later loads reuse.
3. Pick a model in the Browse panel. The viewer selector in the command bar
   switches between Models, Textures and Buildings. The Character menu loads
   characters, imports Armory characters and loads NPCs or models by ID. The
   command bar's Screenshot saves a PNG of the viewport.


NEW IN 1.0
----------

* A new viewport, drawn by an embedded renderer built on the Unity engine.
* Classic clients (Classic Era, Mists of Pandaria Classic, Classic Beta and
  the other Classic lines) open as themselves.
* A client chooser, faster loading with a database cache per client build,
  and the loading progress shown in the chooser.
* Textures and Buildings viewers next to Models.
* Load NPC / Model by Wowhead link or by ID, Armory import by region, realm
  and name, characters on mounts, item sets with transmog sets.
* A new look with System, Light and Dark appearances.

The full list of changes is in the release notes on the project page.


KNOWN ISSUES
------------

* The Models button of the viewer selector sometimes shows no icon. Only the
  icon is affected.
* FBX export is not in 1.0 (0.11.0 had it), and neither is the Blender add-on
  installer that went with it. OBJ export (File > Export Model) remains.
* Models from a legacy MPQ client (File > Load Legacy MPQ Client...) cannot be
  shown in the viewport: they have no FileDataID, which the viewport needs.

To report a problem, open an issue on the project page. Say which client and
model you used, and attach userSettings\log.txt and
userSettings\unityRenderer.log from this folder.


WHERE FILES ARE KEPT
--------------------

Everything the viewer writes stays in this folder:

* userSettings\   settings, the window layout and the logs
* dbcache\        one database cache per client build (at most six); it can
                  be deleted and is built again on the next load
* listfile.csv and extraEncryptionKeys.csv, with their download bookkeeping

The viewport player also keeps its window settings in the registry, under
HKEY_CURRENT_USER\Software\WoW Model Viewer. Uninstalling (in Windows
Settings, under Apps) removes all of these too.


WHAT IT CONNECTS TO
-------------------

There is no update check, no account and no analytics. The viewer connects
only:

* when a client is loaded, to check for a newer community file list
  (downloaded again only when it has changed) and to fetch the encryption
  key list; once both have answered, this is not repeated for an hour in
  that session, and after a failed check it is tried again on the next load;
* for Character > Import Armory Character, to the WoW Model Viewer Armory
  proxy, which looks the character up through Blizzard's API; it receives the
  region, realm and character name you enter, and serves the realm lists;
* for a Wowhead link (an NPC or an item), to that page;
* for an Armory item link, to the WoW Model Viewer website, for the item's
  details.

The viewport player (tools\unity-renderer) talks only to the viewer, over a
connection on this computer; its Unity analytics and diagnostics are off.


LICENCE
-------

WoW Model Viewer is free software under the GNU General Public License,
version 3 or (at your option) any later version, and comes with ABSOLUTELY NO
WARRANTY. See LICENSE.txt. The program includes components by other authors,
each under its own licence: see THIRD-PARTY-NOTICES.txt.

WoW Model Viewer is a free, fan-made tool. It is not affiliated with or
endorsed by Blizzard Entertainment. World of Warcraft and Warcraft are
trademarks or registered trademarks of Blizzard Entertainment, Inc.
