WoW Model Viewer 1.1
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


NEW IN 1.1
----------

* Quick export in the Texture Viewer: with the Quick export box ticked,
  Export PNG and Export BLP save the texture at once into the texture export
  folder (Options > Settings... > Export), and a notice offers Open folder.
* Classic Beta: the equipment windows list the client's items, and View >
  View Item and Character > Load Item Set work there.
* Replacing an item with one that has no appearance in the loaded client no
  longer keeps the previous item's look.
* Some mounts and NPCs whose names were blank now show them.
* Undead male: each jaw choice shows only its own jaw; the default jaw no
  longer shows through Slackjawed, Drooler or the bone jaws.
* The first load of each client after the update builds its database cache
  again, once.

Version 1.0 brought the new viewport built on the Unity engine, Classic
clients, the client chooser, the Textures and Buildings viewers and the new
look. The full list of changes is in the release notes on the project page.


KNOWN ISSUES
------------

* The Models button of the viewer selector sometimes shows no icon. Only the
  icon is affected.
* FBX export is not included (0.11.0 had it), and neither is the Blender
  add-on installer that went with it. OBJ export (File > Export Model)
  remains.
* Classic Beta: many of its items have no name in the client's own data and
  are listed as "Unnamed item" (the item filter finds them by ID), and Load
  Item Set equips only the pieces that have a name.
* A character or equipment file saved with 1.0 while a replaced item still
  showed the previous item's look may keep that look: remove the item from
  its slot, choose it again and save the file again.
* Character > Load Start Outfit lists no outfits in any client.
* Models from a legacy MPQ client (File > Load Legacy MPQ Client...) cannot be
  shown in the viewport: they have no FileDataID, which the viewport needs.

To report a problem, open an issue on the project page. Say which client and
model you used, and attach userSettings\log.txt and
userSettings\unityRenderer.log from this folder.


WHERE FILES ARE KEPT
--------------------

Everything the viewer writes stays in this folder, apart from a graphics
cache that the viewport player keeps in Windows' temporary folder
(%TEMP%\WoW Model Viewer); it can be deleted at any time:

* userSettings\   settings, the window layout and the logs
* dbcache\        one database cache per client build (at most six); it can
                  be deleted and is built again on the next load
* listfile.csv and extraEncryptionKeys.csv, with their download bookkeeping
* Exports\Textures\  the textures the Texture Viewer's Quick export saves
                  (Options > Settings... > Export chooses another folder)

The viewport player also keeps its window settings in the registry, under
HKEY_CURRENT_USER\Software\WoW Model Viewer. Uninstalling (in Windows
Settings, under Apps) removes all of these too, except Exports\: the
textures you exported stay.


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
