# Changelog

All notable changes to **WoW Model Viewer: Midnight** are recorded here.
Format loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [0.3.1] — 2026-06-22

### Fixed
- **Characters render with their full appearance again.** After the move to the newer game data
  format, every customization choice came back tagged as if it needed special unlocking, so the
  viewer treated them all as unavailable — characters loaded bald, with no tattoos, jewellery,
  horns or markings, and empty customization dropdowns. WMV now reads the real unlock requirements
  (achievement / quest / collected-appearance) and only hides genuinely locked entries, so the full
  set of hairstyles, ears, eyes, skin tones and race features shows again. Verified across Blood Elf,
  Night Elf and the Dracthyr dragon (which had been rendering solid black for the same reason).
- **Helmets, shoulders and weapons show up again.** On modern character rigs every equipped
  attachment item — helm, both shoulders, and held weapons/off-hands — rendered invisible, while
  body gear (chest, legs, boots, gloves) showed fine. The animation code and the attachment code
  had drifted into using two slightly different in-memory layouts for a skeleton bone, so each
  attached item was placed with a corrupted bone transform and ended up far off-screen. Both sides
  now agree on the layout, so attachments position and pose correctly. Verified on the Synesthesia
  armory import: the gold crown, both pauldrons and the weapon now render, matching the Armory.
- **Armory import no longer mixes in your dragonriding mounts.** The character appearance API now
  returns the account's dragonriding-drake customizations alongside the character's own. The importer
  applied them all, so a drake's "Skin Color" (a companion-drake / serpent / proto-dragon scale
  texture) was painted over the character's body — e.g. a Blood Elf imported with near-black skin and
  the wrong hair colour. The importer now keeps only the customizations that belong to the character's
  own model, so imports match the in-game appearance. (Worn gear was already correct: it shows the
  transmogged appearance, which is why the item names differ from the equipped items on the Armory.)
- **Race-specific options no longer leak between races.** "Borrowed" appearances that belong to the
  newer dragon/allied races (e.g. the Evoker "Primalist" eye colours and the Dracthyr "Slit/Star/
  Glow" eye styles) are no longer offered on the classic races, while the dragon races keep them.
- **No more spurious image-decode pop-up.** A harmless "incorrect sRGB profile" notice from the
  newer image library could open a dialog the user had to dismiss; it is now logged quietly instead.
- **Hardened animation-track loading.** The bounds check when reading a bone's animation keyframes
  only validated the start of the data block, not its length, so a malformed or newer rig could read
  a few bytes past the end of the animation file. The loader now clamps to the keyframes that
  actually fit in the buffer. (Found with AddressSanitizer while tracking down the attachment bug.)

## [0.3.0] — 2026-06-21

### Added
- **WoW Model Viewer is now a 64-bit application.** The viewer was rebuilt for 64-bit Windows
  (its interface toolkit upgraded to wxWidgets 3.2), lifting the old ~4 GB memory ceiling so the
  large modern listfile and the in-memory database have plenty of room. OBJ and FBX export both
  continue to work.
- **Automatic file-list updates.** The list WMV uses to resolve model and texture paths by name is
  now refreshed automatically from the current community listfile (at most once a week), so files
  added by new client patches resolve without any manual maintenance. It runs quietly behind the
  normal "Loading file list…" step, streams straight to disk, and silently keeps the existing list
  on any problem (offline, server error, short download) so it can never break startup. There is
  intentionally no setting for it — it just keeps itself current.

### Fixed
- **New client builds (e.g. the 12.1 PTR) now load characters correctly.** A build with no exact
  `games/wow/<major>.<minor>/` data profile (only `12.0` ships) used to come up with an empty
  database — no races, no models, the race dropdown collapsed to a single blank entry. WMV now
  falls back to the newest available profile for the same major version, so a fresh patch works
  without shipping a new profile folder for it. Verified on 12.1.0.68209: all 58 races, full item
  and model data, no errors.

### Changed
- **Table layouts are now auto-detected per file.** Each DB2's column positions are matched by its
  on-disk structure fingerprint (layout hash) rather than only by the client build string, so when
  a new patch moves a column WMV corrects it automatically instead of needing a hand-edited schema.
  On a known-good build this changes nothing (it reproduces the curated positions exactly); on a
  newer build it self-heals. Curated positions remain the fallback for fields a definition doesn't
  expose.


## [0.2.5] — 2026-06-18

### Fixed
- **No longer crashes on startup with some WoW installs.** On a client whose DB2 layout didn't
  match the expected field positions — seen with multi-version installs that share one Data
  folder (Retail + Classic + Cata, etc.), where the data read for the chosen build can be
  mismatched — a table (e.g. `CreatureDisplayInfo`) could fail to populate, leaving an invalid
  model id that `RaceInfos::init` then dereferenced as a null file → hard crash on load. Two
  hardening fixes: the DB2 reader now emits a default value when a field position is out of range
  for a record (so a layout mismatch degrades a single column instead of failing the whole table),
  and race-info init skips any entry whose model file can't be resolved instead of dereferencing
  null. WMV now loads instead of crashing on such installs.


## [0.2.4] — 2026-06-18

### Fixed
- **Ear-shape customization works again (Haranir and other races).** The "Ears" option had no
  effect and the ears looked wrong, because a hardcoded ear default (`CG_EARS = 2`) was applied
  to the ear geoset group *after* the customization-choice geosets — clobbering the selected ear
  shape on every refresh. The hardcoded force is removed (the ear hide-toggle is kept), so the
  active Ears choice (geosets 702–705) now drives the ear shape and updates when you change it.
  Verified Haranir and Blood Elf ears render correctly.


## [0.2.3] — 2026-06-18

Fixes for issues reported after the public 0.2.2 release.

### Fixed
- **New races (Haranir, and other recent forms) now show their customization options.** The
  customization panel filtered options with `ChrCustomizationID != 0` but — unlike the data path —
  had no fallback when that returned nothing, and ~21 ChrModels (Haranir, Dracthyr visage, etc.)
  have options that all carry `ChrCustomizationID 0`, so they showed only the Randomise button.
  The panel now falls back to the unfiltered option set for those models (other races unchanged).
- **Armor shows up in the item browser again.** Most items (especially newer armor) were missing
  from the picker because `ItemSparse` — a sparse table read by walking the record field by field —
  had a stale leading field (a fake `AllowableRace`) for the 12.0.7 layout, which shifted the walk
  and left the item *name* (`Display_Lang`) empty for most items; the picker hides unnamed items.
  Corrected the `ItemSparse` field positions: item names read correctly again and the picker now
  lists ~110,000 equippable items (was ~9,500).
- **Armory import now applies skin and hair colour.** Skin/hair colour options are parent/child
  linked and their textures are related-gated, so applying the imported choices in a single pass
  (in the API's arbitrary order) left a stale default colour on the face/hair. The importer now
  re-resolves the imported choices in a second pass so the colours match the imported character.


## [0.2.2] — 2026-06-17

Packaging hotfix for 0.2.1. The 0.2.1 **installer** shipped a stale build-staging copy of the
12.0 `database.xml`, so the character/race/creature fixes from 0.2.1 never reached an actual
install — a fresh install built its database cache from the old field positions and came up with
an empty Characters race tree and broken character customization, even though the source was
correct. 0.2.2 makes the installer ship the 12.0 schema straight from the tracked source, and
bumps the database-cache version so the corrected schema also takes effect when installing over
a prior build (which would otherwise reuse the old cache).

### Fixed
- **Installs now actually get the 0.2.1 fixes.** The installer sources the 12.0 schema from the
  tracked `bin_support\` tree instead of the build-staging dir, so it can't ship stale positions;
  and the cache schema version is bumped so an existing (broken) cache is rebuilt on upgrade.


## [0.2.1] — 2026-06-17

Hotfix for the current retail client (**12.0.7.68235**), whose database layout is newer
than what 0.2.0 was built against. Several tables' DB2 field positions were stale on this
build, so columns were mis-read — which is what made characters and the race tree look
broken in 0.2.0.

### Fixed
- **Character models render correctly again.** On 0.2.0 characters loaded untextured (white)
  or with scrambled customization (missing hair/face, stray black bands). Two causes, both
  fixed: (1) a "nearest known build" schema fallback mis-read several tables on a client
  newer than the bundled definitions — reverted in favour of the curated positions; and
  (2) `ChrCustomizationReq` — which gates *which customization choices apply to a model's
  race/class* — changed layout in 12.0.7, so its race mask read a string offset as garbage
  and the gating broke, letting wrong choices (e.g. horns on a Blood Elf) leak onto every
  character. Its `RaceMask`/`ClassMask` positions are corrected for the new layout.
- **The Characters tree lists named races again.** `ChrRaces` failed to populate on this
  build — two fields had no position and several fell out of range, so the row insert failed
  and the table came up empty, collapsing every race into one blank node. Positions corrected;
  Playable/NPC races now list with their Male/Female models.
- **Creatures: correct skin textures and geosets.** `CreatureDisplayInfo.TextureVariationFileDataID`
  (creature skins) and `CreatureModelData.CreatureGeosetDataID` (extra geosets) were read at
  stale positions and returned garbage on this build; both are corrected (the runtime/installer
  copy of the 12.0 schema is now in sync with the tracked one, which is what had drifted).

### Changed
- The headless `-mo <model>` screenshot CLI now loads the model after the game data is ready
  (it previously ran before load and produced nothing), so automated render checks work.


## [0.2.0] — 2026-06-17

### Added
- **Startup "Client Choice" launcher.** Instead of silently auto-loading on launch, WMV now
  opens a small dialog (in the app's native style) to pick the **Folder** (with Browse), shows
  the **Detected** clients read from `.build.info`, and lets you choose the **Product** (e.g.
  `wow`, `wow_beta`) and the data **Profile** (schema directory, auto-selected to match the
  client version), then **Load**. Command-line/headless loads (`-m`, `-mo`, `-dbfromfile`,
  `.chr`) still load automatically without the dialog.
- **"Loading Client" progress window.** After pressing Load, a small progress dialog shows the
  load stages — Opening game data → Loading file list → Opening database → Building file list —
  with a percentage bar, instead of an empty window while the client loads. The bar advances
  smoothly through the two long steps — the present-file enumeration ("Opening game data") and
  the file-list parse — rather than parking at one value, and repaints reliably at each stage.
- **Import NPC from URL** is now a direct entry in the **Character** menu (next to "Import
  Armory Character"): it opens the Wowhead NPC import dialog and loads the model in one step,
  instead of the old View → View NPC → Import URL → Display detour.
- **Retail (12.x) WMO support.** World objects / buildings (`.wmo`) now load and render on
  modern WoW. Modern WMOs reference their data by FileDataID rather than by name, which the
  classic loader didn't handle, so opening one previously crashed (it read a texture name from
  a null string block using a FileDataID as an offset). The loader now follows the same rules
  wow.export uses: group files are opened via the root's `GFID` chunk (FileDataIDs) with the
  old `_NNN.wmo` naming as fallback; material textures are taken as FileDataIDs when no `MOTX`
  name block is present (otherwise the classic name-offset path); and doodad models are read
  from `MODI` FileDataIDs (otherwise `MODN` names). Classic WMOs still load exactly as before.
  Selecting a WMO *group* file (`<name>_NNN.wmo`, which also appears in the file tree) no
  longer crashes — only root WMOs carry the header that drives loading, so group files are
  now ignored with a log message instead of dereferencing uninitialised counts/arrays.
  Render batches now resolve their material with the modern >256-material rule (when the batch
  flag `0x2` is set the 16-bit index in the batch's second bounding box is used instead of the
  8-bit field), matching wow.export — previously the wrong material/texture was applied.
  The WMO file list now shows only **root** WMOs: group and LOD files (`<name>_000.wmo`,
  `..._000_lod1.wmo`, etc.) are hidden, since they aren't standalone objects (the root
  references them). Uses wow.export's exact filter, so the list matches wow.export's count.
  The camera now frames a WMO to fit the view when it loads (WMOs span hundreds-thousands of
  units, so they used to load filling/overflowing the screen); the max zoom-out distance was
  also raised from 150 so large WMOs can actually be framed.
  WMO orientation is fixed: the geometry was converted into an old Y-up coordinate space (a
  leftover `x,z,-y` swizzle) while the camera and M2 models are Z-up, so WMOs loaded tipped 90
  degrees. They now render directly (Z-up), upright like in wow.export.

### Changed
- **Customization & Randomise are much faster / no longer freeze.** Changing a
  character's appearance (especially Dracthyr, which has many attached models) used to
  unmerge and re-load *every* attached model from disk on every change — re-reading and
  re-parsing each M2 and rebuilding all merged geometry repeatedly. Each refresh now only
  touches the models that actually changed, rebuilds the merged geometry once, and keeps
  a small cache of recently-used models so toggling a piece off and back on doesn't reload
  it. Refresh time is logged (`WoWModel::refresh took N ms`) for diagnostics.
- **Armory character import works out of the box** — a default proxy is now bundled, so
  imports work with no setup (still overridable in Settings → General → Armory). The proxy
  holds the Blizzard credentials server-side; the app ships only the proxy URL.
- **Much faster startup.** Building the file list used to probe CASC once per listfile line
  (~2.1M open/close round-trips — about 6.5s of frozen UI on every launch); it now enumerates
  the storage a single time. Also removed a blind 1-second splash-screen delay.
- **Per-load queries are dramatically faster.** Added secondary SQLite indexes on the hot
  join/lookup columns (customization, equipment, creature/display). They were full scans of
  30k–220k-row tables; the indexes are added to the existing cache on next launch (no rebuild).
- **Opening a character no longer freezes** — applying the default customization now does one
  model refresh instead of ~45 (the same batching the Randomise fix already used).
- **Equipping and searching for items is no longer a multi-second freeze.** The item picker
  filled its list one row at a time with no batching — and actually built the whole list
  *twice* on open, then rebuilt it again on every keystroke in the filter. For big slots
  (weapons, "single item") that's tens of thousands of un-batched inserts each time. The list
  is now populated in a single batched pass (`Freeze`/`Thaw`), the duplicate build on open is
  gone, and filter-as-you-type is batched too, so opening a slot and searching stay responsive.
- **Equipping an item is lighter.** Two redundant full refreshes were removed: (1) merely
  *opening* a slot/set/mount picker used to run a complete model refresh (skin re-composite +
  geometry rebuild) before anything changed — now it doesn't; (2) swapping an item rebuilt the
  merged geometry during unload and then again in the refresh that immediately follows — the
  redundant unload rebuild is skipped. This also speeds up Armory/NPC imports, which set many
  items in a row. (The single necessary refresh per equip remains; collapsing its internal
  cost further is a larger change.)
- **The File List search works as you type.** It previously only ran when you pressed Enter
  (or the button). Now the results update shortly after you stop typing — debounced (~300ms)
  so the heavy ~130k-file filter + tree rebuild runs once you pause, not on every keystroke,
  and only once the term is 3+ characters (an empty box restores the default tree; Enter still
  forces a search at any length).
- **Database field positions adapt to client builds newer than the bundled definitions.** WMV
  refreshes each table's DB2 field positions from WoWDBDefs for the loaded build; if the exact
  build wasn't listed (Blizzard ships patches faster than the defs update), it fell back to the
  stale hand-set positions, which silently mis-read columns (this is what broke creature skin
  textures on 12.0.7.68235). It now falls back to the layout of the highest *known* build at or
  below the client build — the layout in effect just before this patch — so columns stay correct
  on new patches across all tables. The bundled 12.0 schema/data is also now tracked in the repo
  (`bin_support/wow/12.0/`) and shipped by the installer, like the 9.2/10.0/10.1 sets.
- **Mouse zoom/pan now scale with distance.** Zooming was a fixed step per wheel notch
  (~0.5 units), which felt fine on a character but was painfully slow on WMOs that sit
  hundreds-to-thousands of units away. The wheel (and middle-drag) now zoom *multiplicatively*
  — each notch scales the orbit distance — so it's fast far out and precise up close at any
  model size (hold **Shift** for finer steps), matching wow.export. Right-drag panning is now
  proportional to the view distance for the same reason.

### Fixed
- **Creatures render with their textures again.** The main cause was a wrong column position:
  `CreatureDisplayInfo.TextureVariationFileDataID` (the creature's skin textures) was read at
  DB2 field 24 instead of 27 for the 12.0.x layout, so it picked up `ConditionalCreatureModelID`
  (tiny/zero values) instead of the texture FileDataIDs — leaving most creatures untextured
  (white). The current client build is newer than the bundled WoWDBDefs, so the per-build
  position refresh didn't cover it and the stale base position was used; the base position is
  now corrected (the database cache rebuilds once on next launch to apply it).
  Also fixed a contributing case: the faster startup enumeration indexed only locally-cached
  files (`bFileAvailable`), dropping remote-only files (e.g. some skin textures) from the file
  list on streaming installs; it now indexes every enumerated FileDataID (CascLib streams the
  rest on demand, as the per-id probe it replaced did).
- **WMO heap corruption (crash on load) fixed.** Once retail WMOs actually started loading,
  the group-geometry loader's latent memory bugs began corrupting the heap (Windows
  `0xc0000374`). The worst was a dead, never-read `IndiceToVerts` loop whose `i <= indexCount`
  bound wrote one element past its array on the last batch; it's removed entirely (matching
  wow.export, which has no such structure). Also hardened every group chunk read to copy
  exactly the allocated element bytes instead of the raw chunk size (`MOPY/MOVT/MONR/MOTV/
  MOBA/MOCV` — previously a non-multiple chunk size, or a stale/zero vertex count, overran the
  buffer), reset all per-group counts on load, bounds-checked the render loop
  (index/vertex/material indices) and the group fog lookup, and masked the classic doodad
  name offset. WMOs now load without crashing.
- **Wowhead NPC/item import works again.** The Wowhead importer plugin wasn't being built or
  deployed (only the Armory plugin was), so no plugin handled Wowhead links and every import
  failed with "URL cannot be reached." The plugin is now built and shipped, and the importer
  also accepts links pasted without `https://` and follows redirects.

### Removed
- **In-app lighting controls** (the Lighting panel and the Lighting menu) have been removed.
  A sensible default light keeps models lit — there is simply no lighting UI to configure.


## [0.1.5] — 2026-06-15

First public release: the classic WoW Model Viewer (0.10.x) modernized for current
retail World of Warcraft (patch 12.x) and rebranded as **Midnight**.

### Added
- Support for **current retail WoW (12.x)** — modern WDC5 database format with
  DBD-driven schemas.
- **Armory character import** — load a character's race, appearance and equipment from
  a Battle.net Armory link, via a self-hosted proxy (no per-user credentials).
- **Windows installer** (per-user, no admin) with Start-menu/desktop shortcuts and an
  uninstaller.

### Changed
- Character customization: skin, faces, hair, geosets, equipment, and colour swatches.
- M2 multi-texture **combiner shaders** so layered materials (cosmic capes, glowing
  orbs) render correctly instead of solid white.
- **Randomise** is dramatically faster — applies all options then refreshes once
  (previously one full refresh per option).
- Rebranded to **WoW Model Viewer: Midnight** (name, application icon, splash).
- Removed the built-in auto-updater.

### Fixed
- **Dracthyr** (and other newer/allied races) customization — empty option lists and a
  scrambled skin caused by an over-aggressive per-choice race filter.
- **Creature particle colours** (Fyrakk and similar) under the modern ParticleColor schema.
- Blank/missing character faces; customization crashes across several races.
- A model-switch **memory leak** and out-of-range bone/light/texture-lookup reads in the
  per-frame animation/render paths.
- Blank **application / taskbar icon**.
