# Changelog

All notable changes to **WoW Model Viewer: Midnight** are recorded here.
Format loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- **Classic clients: Classic Era, Classic Beta and the other Classic lines open as themselves.** A Battle.net
  install lists several products in its `.build.info` (on the test machine: `wow` 12.1.0, `wowt` 12.1.0,
  `wow_classic_era` 1.15.9, `wow_classic` 5.5.4, `wow_classic_beta` 1.60.1). Each is now read as what it is: a
  client profile with a product family (Retail, PTR, Beta, Alpha, Classic, Classic PTR, Classic Beta, Classic Era,
  Classic Era PTR, legacy MPQ) and an expansion from its own version line -- Retail by major version, Classic by
  major.minor (1.13-1.15 Vanilla, 2.5 Burning Crusade, 3.4 Wrath, 4.4 Cataclysm, 5.5 Mists of Pandaria). A version
  no line claims (the 1.60.1 Classic Beta) names no expansion rather than a guessed one. Every Battle.net product
  is CASC storage addressed by FileDataID, whatever its version (Classic files used to go to an empty MPQ
  provider). The database schema follows from the client (`ClientInstallations::resolveSchema`: its own version's
  schema if one ships, else the newest of its major version, else the newest), and every table is checked against
  the client's own file before it is read: a layout the WoWDBDefs definitions know is remapped to it, a layout the
  schema records itself (`layoutHash` on the table, which is now actually read) is read as is, a Retail patch of the
  schema's own generation keeps the schema's positions, and anything else is not read rather than read wrong (the
  log's `[schema]` line counts each kind). A column a matched layout does not have reads as empty, or as the
  schema's `absentValue` where empty would mean something (ChrCustomizationReq.RaceMasks: no race restriction,
  rather than no race allowed). The 45 definitions in `bin_support/dbd` are now installed next to the
  executable (there was no install rule for them), and `Item.SheatheType` is named as they name it. Retail 12.1
  verifies all 46 tables; Classic Era 1.15.9.70003 verifies 45 and leaves one unread (CharHairGeoSets, a layout no
  definition has). Classic Era's Browse lists its 10,433 models, Textures its 101,251 textures and
  Buildings its 6,751 world models; Characters lists its playable races, which load their own models (Human Male
  is `character/human/male/humanmale.m2`) with its own customization tables. Files the client lists but has not
  downloaded are left out of Browse (they used to be listed and fail to open). What a client can do is worked out
  when it loads (`[clientcaps]` in the log) and the commands it cannot do are greyed and refused with the reason in
  the status bar: Armory imports Retail characters only; NPCs, items and characters need their tables. The status
  bar names the loaded client ("Classic Era · Vanilla · 1.15.9 (build 70003)"). `-product <code>` picks the
  product on the command line (export jobs pass theirs on).
- **Buildings: a third viewer mode, for World Model Objects (WMO) -- buildings, dungeons, cities.** The selector
  reads "VIEWER Models | Textures | Buildings" (Lucide's building-2 icon; the tooltip names World Model Objects
  (WMO)). Buildings switches at once, before anything is picked: Browse shows (opening it if it was closed) listing
  the loaded client's world model roots in the same tree and search box, the viewport says "No building selected"
  with a Browse buildings button, and nothing of the Models viewer stays on screen. The Model panel stays (Appearance
  has the doodad set; Info the file, FileDataID, groups, doodads and bounds); the Animation panel and the Attachments
  window, which only act on a model, are put away and given back in Models as they were, shown or closed and at
  their size. A building picked in Browse is loaded by the existing world-model path (`FileControl::SelectWMOFile`:
  the root read on the host, the geometry built by the Unity player) and drawn in the viewport; the command bar
  names it (its path and FileDataID in the tooltip), the title gives its path and the status bar its groups, doodads
  and doodad sets. Every world-model load -- Browse, `-wmo` on the command line, a FileDataID -- shows it in
  Buildings, and a model, NPC, item or character loaded from a menu shows in Models. Browse lists roots only: a
  `.wmo` is a group file when its name is `<root>_NNN.wmo` or `<root>_NNN_lodN.wmo` and `<root>.wmo` is in the same
  folder. Checked against every file's own first chunks on 12.1.0.69933: of 86,178 `.wmo`, 12,930 roots are listed
  and 73,248 group files hidden, none the wrong way (20 are encrypted with no key in the client: 4 named as roots, 16
  as groups). The rule lists 101 roots the old name filter hid -- 99 `_lod1` roots with groups of their own, and 2
  roots named like groups (`11xt_rockbridge_003.wmo`). The list is made once per client, in about 0.4 s. The search
  matches path and file name (every word must occur), and a number is also a FileDataID: the building with it, the
  building a group file belongs to, or a row that looks it up (its first chunks are read: a root loads, anything else
  is said); Enter on a FileDataID opens it. Each viewer keeps its own search and tree: the same folders open, the
  same row at the top, the same row picked. The Models background colour is not applied to buildings: the player's
  clear colour follows what it draws, not the viewer -- a world model always on the viewport's default `#19191E`, a
  model on the Models colour -- so switching viewers never re-colours anything (the colour is sent with a load the
  player gets, before it). The commands that only act on a model -- the Animation panel, Attachments, View
  NPC, View Item, Load Character, Import Armory Character, Load NPC / Model, Export Model and its exporters, Swap
  Background Color -- are greyed and refused in Buildings as in Textures (they now require Models, not "not
  Textures"); Fullscreen and Screenshot work in Buildings. The canvas and the player still hold one model or one
  world model at a time: a building loaded replaces the model, and the other viewer then says "No model loaded" or
  "No building selected" rather than showing what is not its own. The player's window now lives in a frame window of
  the viewport's own, which the viewer moves off screen and back itself: the player goes with the switch's repaint
  however long it takes to handle window messages (on a desktop where it is busy presenting frames, a hide posted to it
  landed 50 ms and more later, and the model or building of the viewer left stayed on screen meanwhile -- over the
  texture view too: the player's window does not clip itself against windows above it), and comes back with the
  repaint (its resize waited for when its size has to change). Where it has gone the viewport is painted plain dark at
  once -- also when a building picked in Models loads before the repaint -- and its own window stays shown while it is
  away, so nothing is filled in under it in the Models colour. A load of the other kind is not shown until the player
  has presented it. WMO rendering is unchanged.
- **Character > Load NPC / Model...: a model by its ID, looked up in the loaded client, beside the Wowhead NPC link.**
  The command was "Import NPC from URL"; it keeps its place in the Character menu and is still a Models command
  (greyed in the Textures viewer). One compact window with two ways in under a tab strip: **URL**, a Wowhead NPC link,
  loaded exactly as before (the same importer, the same NPC, model, display and equipment -- checked against the
  previous build on four links, the viewport's captures byte-identical); and **ID**, an ID type chosen explicitly
  and a number. Nothing is guessed: the same number often means different things (3876 is a Creature Display ID
  showing a Scourge male and a Creature Model ID naming the Pyrogryph), so the type is never tried in turn. The types,
  named as the client's tables name them:
  - **M2 FileDataID** -- the file itself, on its own, as Browse shows a model, also when the listfile does not name it
    (the Unity viewport is then sent the FileDataID alone). It must be in the loaded client, be an M2 (a listfile path
    ending in `.m2`, or data starting with `MD21`/`MD20`) and be readable.
  - **Creature Display ID** -- `CreatureDisplayInfo.ID -> ModelID -> CreatureModelData.ID -> FileDataID`, shown with
    that display's appearance as View NPC shows it: its texture variations, geosets and particle colours, or, for a
    display with `ExtendedDisplayInfoID`, the race's HD character model wearing the NPC's equipment
    (`NpcModelItemSlotDisplayInfo`). Model scale is not applied: the client's scale tables are not loaded.
  - **Creature Model ID** -- `CreatureModelData.ID -> FileDataID`: the model file on its own, as Browse shows a model;
    a model row is no NPC and chooses no appearance.
  An ID is always loaded afresh, even when its file is the one on show, so it never keeps the skin or equipment that
  file was last given.
  NPC (Creature) IDs are not offered: the client's `Creature` table holds only part of the NPCs, and an NPC can have up
  to four displays whose probabilities are not loaded -- the URL page and View > View NPC cover NPCs. Everything is
  looked up offline in the loaded client. When an ID cannot be loaded the window stays open and says why, naming the
  step: no ID, not digits only, 0, negative, too large, no such row, a row with no model (`ModelID` or `FileDataID`
  0), a file not in the loaded client (or in the file list only), a file that is not an M2 (and what it is, e.g. a
  `.blp`), a file that cannot be read (encrypted with no key in the client, or not downloaded), no client loaded yet;
  the model on show is left as it was. The page and ID type last chosen are remembered until the viewer is closed;
  the ID is not. Cancel works while a link is being read (what the read brings back is dropped). View > View NPC's
  "Import from URL" uses the same window's link page alone.
- **View > Swap Background Color: the Models viewport's background colour,** in a small window that can stay open
  while the model is turned; it first opens at the viewport's top right, clear of the viewport's centre. A Models
  command: greyed in the Textures viewer, which puts the window away, and Models gives it back. A colour picker in the
  window (a saturation x brightness square and a hue strip, keyboard too; the viewport follows a drag live and the
  colour is kept when it is let go), a `#RRGGBB` field (either case, `#` optional; Enter or leaving the field applies
  it; checked as it is typed, and nothing that is not a colour is sent), built-in presets that cannot be removed --
  Default `#19191E` (the viewport's colour until now), Dark `#000000`, Slate `#202428`, Neutral Grey `#808080`, Light
  `#BBBBBB` -- and presets of your own (Save as preset; a duplicate selects the preset it duplicates; remove by
  right-click or Remove), with the preset on show marked. Undo goes back to the color the window was opened with;
  Reset goes back to `#19191E`; both keep your presets. Every change shows at once behind the model (a notice --
  nothing loaded, a player problem -- keeps its own dark). Kept in `Config.ini` as `ModelViewport/BackgroundColor` and
  `ModelViewport/BackgroundPresets`. The colour shows on screen exactly as given (measured on the viewport's own
  capture for every grey up to `#D8D8D8`; brighter ones are lifted one to three steps by the viewport's bloom, and
  `#FFFFFF` shows as 254). The Texture Viewer's backgrounds, the transparent screenshot, lighting and materials are
  unchanged, and at the default the viewport's frames are byte-identical to before; the panel under a starting player
  now paints the same `#19191E` (it was `#231F20`). The player speaks protocol 7 (`viewportBackground`); an older
  player keeps its own default.
- **Textures: a second viewer mode beside Models, for the client's BLP textures, shown in the viewport.**
  The command bar starts with the viewer selector, "Viewer: Models | Textures", one of the two always pressed (a
  third, Buildings, came later; see above).
  Textures switches at once, before anything is picked: Browse shows (opening it if it was closed) listing the
  loaded client's textures in the same tree and search box as the models, the texture view takes the viewport's
  place saying "Select a texture in Browse", and the panels that only act on a model (Animation, Model, the
  Attachments window) are put away. Models gives the viewport, the model and those panels back at once, each as it
  was, shown or closed and at its size; the layout saved on exit is always the user's. Loading a model, an NPC, an
  item or a character from a menu switches to Models too. Each mode keeps its own search, and each tree -- or
  search result -- comes back as it was left: the same folders open, the same row at the top, the same row picked
  (the model's row only while that model is still the one loaded). The list
  is filled from the app's own file index -- every `.blp` the loaded client names, 789,146 on 12.1, listed in under a
  second, once per client. Folders are filled in as they are opened, and a folder of more than 1,000 textures shows
  them in ranges of 1,000 named like a dictionary's guide words, so even textures/bakednpctextures (83,178
  textures; 17 s as plain rows) opens at once. The search matches file name, path or FileDataID: every word must
  occur, a word with `/` matches the path, a number also finds the file with that FileDataID even when the file
  list has no name for it, Enter on a FileDataID opens that file, and a FileDataID the list does not have is offered
  as a row that looks it up. It lists the first 500 matches in their folders, and clearing it opens the folders down
  to the texture on screen. Files known only by FileDataID are one group at the end. Selecting a texture shows it
  in the viewport's place -- no second window. Moving through textures has no pause and no blank frame: the name,
  folder and FileDataID change at once (size and format say "Reading..."), the texture on screen stays until the
  next is decoded and is then swapped whole, and the decode waits only for the input already queued and the
  repaint of the row, so of several quick clicks only the last is decoded. While an arrow key repeats in Browse
  the rows go by without decoding, and the row it stops on is decoded when the key is let go. A 512 texture shows
  33 ms after the click (181 ms before this change), a 4096 one 225 ms (534 ms). The last textures decoded are
  kept, up to 128 MB, so one seen again shows in 5-34 ms. A texture drawn at half its
  size or less is drawn from the smallest mip level at least as large as it is drawn (Alpha On and Only; Alpha Off
  shows level 0, whose colour under transparent pixels the smaller levels do not keep); its alpha facts still come
  from level 0, and Export PNG always writes level 0. The texture is sized by itself, with no
  zoom: one larger than the area is scaled down to fit it, a smaller one is enlarged only by a whole number, at most
  twice and not past 512 pixels (the size 95% of the client's textures are at most), so a 64 x 64 icon is shown at
  128 and a 512 texture at its own size. It is drawn over a checkerboard, dark or light background, with Alpha On /
  Off / Only (display only), its name, folder, FileDataID, size, format, mip levels, alpha and file size, Copy path /
  Copy FileDataID, and two buttons: Export PNG, the full-size texture with its alpha (Ctrl+S), and Export BLP, the
  original file byte for byte (Ctrl+Shift+S), named after the texture or `<FileDataID>.png` / `.blp`; both wait for
  a texture. Pixels come
  from the app's own texture decoder; a texture it would decode wrongly -- uncompressed BGRA, palettised with 4-bit
  alpha, DXT kinds the decoder only guesses at (among them 139 whose blocks are twice the size it would read), a
  damaged first mip level -- is not shown and gets no PNG (Export PNG is disabled), with the reason said, though its
  original still exports (2,811 of the 789,146). The model loaded before stays loaded behind the textures, and the
  texture picked stays picked behind the models: Models, or picking the model again in Browse, shows it again at
  once, without loading it again. The command bar, the status bar and the menus follow the mode. Background, alpha
  view and export folder are remembered.
- **Screenshot: a 3840 x 2160 PNG of the Unity viewport with a transparent background (protocol 6).** The command
  bar's Screenshot opens a Save As dialog (PNG Image, overwrite confirmed) named after the model and the time,
  `<model>_<yyyy-MM-dd_HHmmss>.png`, and adds `.png` when it is missing; cancelling does nothing. The host sends the
  path in `captureScreenshot` and the player renders the live camera once more, off screen and at the end of the
  frame it is showing -- the same pose, animation frame, equipment, mount, particles and ribbons, with no clock
  advancing -- into a 3840 x 2160 target cleared to transparent, reads it back and writes the PNG itself, then puts
  the camera back and answers `screenshotSaved` with the outcome and its timings. The capture is 16:9 whatever the
  viewport's shape and crops nothing the viewport shows. The status bar says "Screenshot saved: <file>" or why it
  failed. Bloom is not in the PNG: post-processing is off for the capture, because the pipeline's post pass writes
  opaque alpha. The headless lifecycle sequence gains `screenshot:<path>`, which takes the same path without the
  dialog and checks the file it writes.
- **Model > Appearance: a Mount card at the top of the page, with a searchable mount picker.** For a playable
  character the first thing on the Appearance page is a Mount card. Without a mount it says "Add a mount to this
  character" and offers Choose Mount, in bold; on one it names the mount the character rides and offers Change
  Mount and Dismount. Choose Mount and Change Mount open a floating picker anchored just below the card -- the
  Appearance page does not expand or scroll -- with a focused search field over the player mounts: the list
  filters on every keystroke (any part of the name, upper or lower case), Up and Down move the highlight, and
  Enter or a click puts the character on that mount at once and closes the picker -- there is no OK. Escape or a
  click outside the picker closes it with nothing changed. Enter with nothing highlighted takes the only mount
  left, never any other, and a search that matches nothing says "No mounts found" and stays open. Change Mount
  starts on the current mount and swaps straight to the new one; Dismount takes the character off in one click.
  The list is the Character > Mount / Dismount dialog's player mounts (a few nameless entries with no display are
  left out), read from the database once per loaded model and filtered in memory, and the card and the dialog
  mount through the same choice: `CharControl::selectMountChoice` and `dismount` hand
  `OnUpdateItem(UPDATE_MOUNT)` the dialog's own row, so each shows what the other did: an open dialog moves its
  highlight to the mount the card chose, or to None, without choosing it again. What the card shows is read from
  the host every time -- after the dialog, a load, or a creature or world model picked in Browse -- and it is
  hidden for anything that is not a playable character. While the character rides, the tab reads
  "Appearance · Mounted".
- **Embedded Unity renderer, host side: a character riding a mount is described to the player (protocol
  5).** To a player that announces protocol 5 the host keeps the character loaded (`loadWoWModel` names
  the rider, not the mount) and adds an optional `mount` to its `characterScene`: a key from a host mount
  serial raised for every mount model the mount choice installs, the mount's FileDataID, path and display,
  the attachment id with the bone and position the mount's own attachment table gives it (-1 and zero
  when it has none), the rider's scale, the mount's own texture bindings (a slot bound to nothing is left
  out, never sent as the rider's body image), its geoset flags, its display's particle colour
  replacement, and the sequence each model plays. Mounting, dismounting and swapping mounts send a new
  scene, never a new load, and the mount's skin and geosets travel only there. `characterSceneApplied`
  gains `mountKey`, `mountStatus` and `mountReason` (a mount the player cannot build is logged and never
  marks the character as failed), `modelAnimation` gains `role` and `load`, `modelAnimationState` gains
  `load`, `hasRider` and the rider's `sequenceIndex`, `playing` (the mount's pause), `timeMs`, `speed` and
  `loop`, and `runtimeState` gains `mountFileDataID`. A player older than protocol 5 keeps the "Mounted
  character" notice. Each mount ridden is logged once with `[unity-mount]`.
- **Embedded Unity renderer: a mounted character rides its mount in the Unity viewport (player,
  protocol 5).** The player keeps the dressed character it has and builds the scene's mount beside it as
  a second, separately animated model (`WmvMountedScene`), then hangs the character's body root from the
  mount bone the host resolved -- at the attachment position less the bone's pivot, on the mount's origin
  when the mount has no such attachment, or at the converted position when the build has no bone for it
  -- so the character and everything it wears follow the animated bone through the hierarchy. The mount,
  the seat and the riding sequence go on in one frame: the character's scene waits for the mount and for
  the riding sequence's `.anim` keys, and both clocks start where the host's are. Swapping mounts moves
  the character onto the new mount before the old one is disposed; a dismount takes it off without
  reloading anything; a mount that cannot be built is answered `failed` and leaves the character on
  screen; a character loaded while it rides builds its mount with the load. Every disposal takes the
  character off the mount first. Answers carry `mountKey`, `mountStatus` and `mountReason`, and
  `runtimeState` the `mountFileDataID`. The lifecycle self-test adds 70 checks, among them a skinned
  model under an animated bone of another model, compared with its bake at the origin.
- **Embedded Unity renderer: a mounted character's two models follow the Animation panel, each on its own
  clock (player, protocol 5).** An animation choice reaches the model its `role` names -- `"mount"` the
  mount the character rides, `"rider"` the character on screen or, by its `load`, the one being loaded --
  and never the model that merely shares its FileDataID, so a mount built from a playable race's body is
  told apart from its rider. A ridden `modelAnimationState` is applied in one pass: its top level to the
  mount's animator, the nested `rider` (whose `playing` is the mount's pause, as the host's tick gates the
  whole tree) to the character's. Picking a mount clip switches only the mount, through its own slot's
  track cache and `.anim` files (fetched up front when the mount goes on); picking a character clip in View
  > Attachments switches only the character. When a mount goes on, it starts from the host's newest state
  for it, and the character's riding sequence from the host's state for it or, without one, in step with
  the mount. A mount choice sent before its scene is logged and ignored, one made while the mount is being
  built is applied when it goes on, and the character's own idle selected at a dismount is held for the
  frame it comes off. `-wmvAnimTime` poses the mount, then the character. The sequence code moved from
  `WmvMain` to `WmvSlotAnimation` so both slots run it; the lifecycle self-test adds 67 checks (routing,
  the nested state, both clocks under switches of either model, the start rules, the pinned pose).
- **Embedded Unity renderer: a mounted character is framed with its mount (player, protocol 5).** When a
  mount goes on, is swapped or comes off, the camera and the shadow window are fitted to one world box
  around the mount and the character -- the character's box carried through its body root, which hangs
  from the mount's bone, so a large mount no longer runs out of the view -- and after a dismount to the
  character again. An appearance or equipment change while mounted keeps the view, nothing is re-framed
  per frame, and `-wmvFrameBounds` still pins the box. `WMV_VIEWPORT_ORBIT` now re-aims the camera after a
  model or a mounted character is framed as well as a world model, `WMV_VIEWPORT_SHOT` also captures after
  the mount under a character changes and logs the mount's emitters, `-wmvAllocCheck` waits for a mount
  being prepared and reports its animator, material bindings and emitters, and `-wmvLightCheck` measures
  both models. New diagnostic `-wmvMountCheck[=frames[:mountfirst]]` measures the LateUpdate order under a
  mounted character, how far the animation leaves the framed box and whether the two models' transparent
  draw order changes a pixel: Unity poses the character (and its items) before the mount, which leaves
  billboard facings and the shadow map at most 0.6 degrees / 0.04 units behind at 60 frames a second on
  the benchmark clips, and no draw-order-dependent pixel was found, so neither was changed. The lifecycle
  self-test adds 17 checks.
- **Embedded Unity renderer: the headless test drives a mounted character end to end.** `-unityipctest` now
  checks the mounted character it puts up by hand -- the scene answered with the mount applied and no notice,
  the player holding the character with that mount under it, and nothing left of it after the mount is taken
  off -- and a run that ends mounted no longer fails for the ridden mount's absent skin pushes (its skin
  travels in the character's scene). `WMV_IPCTEST_SEQUENCE` gains steps that act on the character already on
  the canvas, through the menus, panels and dialogs a user acts through, and load nothing: `chr:<file.chr>`,
  `mount:<displayId>`, `dismount`, `manim:`/`ranim:<animId>` (or `#<sequence>`), `equip:<slot>=<item>`,
  `custom:<option>[=<choice>]`, `sheath`, `reconnect` and `wait:<ms>`. Each requires the player's answer to the
  scene it caused and its `runtimeState` account afterwards to match what the host shows: the character on
  screen with no load in flight, the mount by key and file with exactly one mount runtime alive, the seat the
  host's resolved attachment gives, both animators on the host's sequences and the mount's emitters its own.
  No mounted step may send an ordinary `modelSkin` or `modelGeosets` either -- what a ridden mount displays
  travels in the character's scene and nowhere else -- and each one is held to how often the player fitted the
  view: exactly once for a mount going on, being swapped or coming off, and not at all for a clip change or a
  change of what the character wears. `runtimeState` gains `mountKey`, `liveMounts`, `mountsBuilt`,
  `mountSeat`, `mountSeatBone`, `modelSequence`, `mountSequence`, `mountEmitters`, `mountRibbons`,
  `mountParticles`, `bodyRebinds` and `viewFramings`, so a stale, doubled or rebuilt mount, a needless
  character re-composite and a camera that re-aims itself fail a step; an `m2:` step now also requires no
  mount to be left alive. `CharControl::selectMount` keeps its rows in `fillMountChoices`, so a test picks the
  row the dialog would (no behaviour change), and an attached item moved between attachments is logged.
  `WMV_VIEWPORT_SHOT` waits for what it was asked for -- the mount committed, a sequence switch finished, the
  pose `-wmvAnimTime` pins -- asks for its size again and re-frames the last box when the aspect changed, and
  logs the camera, both models' clocks and the seat beside the emitter counts. The lifecycle self-test adds 10
  checks for the counts it reports.
- **Embedded Unity renderer: world models (WMOs) are drawn, as static geometry.** Picking a WMO in
  Browse keeps the Unity viewport on screen instead of showing a notice. The host sends the root's
  FileDataID (`loadWoWModel` with `"kind":"wmo"`, protocol 4) and the player fetches the root, the
  full-detail groups its GFID chunk names and the material textures itself, then reports what it built
  (`mapObjectLoaded`). Doodads (so the doodad-set choice has no visible effect yet, as the Model panel
  now says), liquids, WMO lights, fog, portal culling, LOD switching and the skybox are not drawn. A
  player older than protocol 4 gets the "Unity renderer out of date" notice for a WMO, a root the host cannot read gets "World model cannot
  be read", and a WMO the player reports it could not build gets "World model could not be built".
  `-dbfromfile -wmo <root path or FileDataID> -unityipctest` checks the player's report headlessly, and
  `WMV_IPCTEST_SEQUENCE` adds a model/world-model switching sequence that checks the player holds
  exactly one runtime of the right kind after every step (asked with the new `runtimeState` question,
  so a world model left alive under a model fails too).
- **Embedded Unity renderer: world-model materials follow the client's map-object shader cases.** Each
  MOMT entry is planned by `WmoMaterialSemantics` and drawn by its own world-model shader
  (`Resources/WmvWmo.shader`), not the M2 combiner shader. Shader ids 0/16, 4 and 13 are drawn as their
  client cases (13: +0x0C and +0x18 on MOTV sets 1 and 2, lerped by the MOCV set-2 alpha). The diffuse
  parts of ids 23, 7 and 5 are drawn without their env-map emissives (U-G1, U-E2, U-E3), and id 23
  without its MOC2 byte-3 colour pull (U-23a): its four layers and height maps on MOTV sets 1-4,
  weighted by MOC2, so its +0x0C env map is no longer drawn as the surface and an empty +0x0C no longer
  turns it white. Blend 0 is opaque and blend 1 keys at 128/255 only on ids whose case alpha is the
  texture's; flag 0x04 turns culling off, 0x40/0x80 clamp texture addressing, and 0x01 bypasses the
  preview light on ids without an emissive. Blend values 2 and above, MOCV set-1 vertex colours, empty
  registers a draw weights and every other shader id stay provisional and are logged per material with
  their reason codes. Only the textures a drawn material samples are decoded, once per FileDataID, and
  uploaded GPU-only. New diagnostic switches: `-wmvWmoMaterialDiag` (one plan-and-readback line per
  drawn material), `-wmvWmoView=plan|weights|blend|va|diffuse|t0|t1`, `-wmvWmoUvOverride=N` and
  `-wmvWmoOnlyMaterials=a:b:c`.
- **Embedded Unity renderer: world-model material logs name the client's blend row and the undrawn env
  emissives.** Nothing drawn changes. For a blend value of 2 or above the material line and
  `-wmvWmoMaterialDiag` give the row of the client's blend-state table (the four factors the 12.1
  executable holds for that EGxBlend index) beside the realised state, with its evidence level: value 2
  selecting row 2 is older-client documentation, not contradicted; from 3 up that hop is contested for
  12.1 and the material gains `U-B7`, and `U-B1` now marks only values past the table's 17 rows. Ids 5,
  7 and 23 log the client equation of their env emissive, the coordinate it would need and every input
  that keeps it undrawn: `U-E2` (camera axes, now on ids 5 and 7 too), `U-E4` (the env sampler's
  addressing) and `U-P1` (whether the program the client picks adds the emissive at all) join U-G1 and
  U-E3. An F_UNLIT light bypass drawn in an interior group (MOGP flag 0x2000) gains `U-F3`, because
  older-client documentation honours the flag only for exterior-lit batches; the bypass itself is kept.
  Blend 2 and above keep their provisional drawing and the env emissives stay undrawn, by decision.
  `-wmvWmoMaterialDiag` also adds one `wmo batch` line per drawn batch (its range, submesh and material,
  the queue and depth write read back, the verdict), and `-wmvWmoView=envmask` draws the emissive masks
  of ids 5, 7 and 23 (t0.rgb * t0.a, c.rgb * c.a, mix.rgb * mix.a) without binding or sampling any env
  map.
- **Embedded Unity renderer: playable characters are drawn in the Unity viewport.** The Unity
  viewport draws the same
  character from the state the host has already resolved -- the composited body and eye textures,
  the geoset flags after every customization, equipment and helm rule, the collection armour and
  customization parts merged into the character (skinned to its bones through the host's own bone
  table), the item models attached at its attachment points (sheathed weapons and shields included)
  and the closed hand around a held weapon. Model > Appearance, equipment changes, the render toggle
  in View > Attachments (formerly Model Control) and Model > Geosets (the character's own geosets,
  its merged parts' and its items') update the viewport in place: one `characterScene` per change,
  applied whole, no reload, no camera reset and no animation restart. A mounted character, a
  character on a player build older than protocol 3, and a character the player reports it could
  not build get a notice in the viewport instead (see Changed).
- **Embedded Unity renderer: models whose skeleton lives in a separate file animate.** Every
  playable race keeps its bones, sequences and attachments in a `.skel` (the SKID chunk, and the
  SKPD parent it may defer to); the player now reads them the way the host does instead of drawing
  such a model static.
- **Embedded Unity renderer: alias sequences play.** A sequence flagged as an alias (0x40) with no
  keyframes of its own now plays the keys of the sequence its alias chain ends on, fetching that
  sequence's `.anim` file. Every HD race has seven of these, and many creatures have more; they used
  to fall back to Stand. (The host's own bone evaluation, which now only poses exports, does not
  follow aliases yet.)

### Changed
- **File > Load World of Warcraft asks one plain question: "Choose World of Warcraft".** Each installed product is
  a card -- "Classic Era", "Vanilla · 1.15.9", Installed or Not downloaded, "Last used" on the one opened last --
  and a click, Enter or Space on a card opens it (Up and Down move between cards, Tab leaves them). Installations
  are found from the configured folder, the last one used and the Battle.net registry entries, so a second
  install (a PTR in its own folder) is listed too, with its folder named on its card. A product whose game data is
  not on the computer (its encoding manifest is not in the local archive index) is listed but cannot be opened, and
  says why ("Start it once from Battle.net to download its game data"). There is no product code and no profile to
  pick: the old chooser's Profile list offered the one shipped schema, "Midnight - 12.0", and forced it on every
  product, Classic included. The technical facts -- product, version, build, region, language, folder, storage,
  whether the data is local, the schema it will use -- are under "Advanced ▸" for the card in focus. "Browse for
  another installation..." and "Open legacy installation..." (an MPQ install) are the quieter ways in. While a
  client opens, its card says "Opening..." and a window "Opening Classic Era...", then "Reading the file list...",
  "Reading game data...", "Building Browse...". A client that cannot be opened is said in plain words, the
  technical details apart, and the chooser comes back; the client loaded before stays loaded meanwhile. The chooser
  is drawn with the design system's palette in Light and Dark.
- **A modern, flat look for the main window, from one design system (`UiStyle.h`).** Spacing, control heights,
  type roles and colour roles live in one place, and the shell takes them from there instead of picking its own
  pixels and RGB values; the colours are roles (`palette().text`, `.separator`, `.accent`...), with one set of
  values for the light palette and one for the dark (see the Appearance entry below). The command bar is drawn flat (`UiToolBarArt`): "VIEWER" then Models | Textures
  as one segmented control whose selected half is filled in the accent colour, Fullscreen and Screenshot as quiet
  utilities, and the Browse / Model / Animation pane toggles at the right, a pressed toggle on a soft fill. Pane
  captions (`UiDockArt`) are flat, the pane with the keyboard named in the text colour with an accent line under its
  title and the others in grey, with a quiet close button and thin sashes; the caption, sash and border sizes are the
  ones the docked layout always had, so saved layouts and the viewport's size are unchanged. Buttons come in three
  kinds (`UiButton`, still native Windows buttons, only painted: keyboard, focus, tooltips and screen readers stay
  the system's): Primary in the accent for the action an area is for (Export PNG, Choose Mount, Play / Pause, the
  viewport notice's action), Secondary outlined (Export BLP, the equipment slots, the transport steps, the queue
  buttons), Subtle with no frame until the mouse is over it (Copy path, Copy FileDataID, Reset, an equipment slot's
  remove), each with hover, pressed, disabled and a keyboard-only focus ring, at two heights. Search fields (Browse,
  the animation filter, the geoset filter) are 28 px tall with a rounded outline that turns to the accent while the
  field has the keyboard. Browse's tree uses the Explorer style (chevrons, hover, soft selection) with 22 px rows and
  no border of its own. The Model panel's pages sit under a flat tab strip (`UiTabBar`: the selected page in
  semibold with an accent underline; Left / Right / Home / End move between them) instead of property-sheet tabs. The
  texture view's Alpha On / Off / Only is a segmented control too, its name a title, its exports a primary and a
  secondary button with an export icon. Panels are on one panel colour, section titles semibold, hints and
  counts in a secondary grey. A few icons (16 px, from the open-source Lucide set, ISC licence; see
  `docs/third-party-notices.md`) mark the modes, the utilities, the pane toggles, the transport and the exports; no
  other images were added. Native controls Windows draws well (combo boxes, check boxes, sliders, list headers,
  menus, the status bar) stay native.
- **View > Appearance: System, Light or Dark, the whole window dark in Dark -- menus and dialogs too.** System
  (the default) follows Windows' app mode (Settings > Personalization > Colors); Light and Dark keep their palette
  whatever Windows uses; the choice is kept in Config.ini (`Settings/Appearance`: 0 System, 1 Light, 2 Dark).
  Windows' high-contrast mode still wins over every choice: the shell then takes the system colours, as before.
  A dark run uses wxWidgets' dark mode, which darkens most of what Windows draws: the native controls, the
  menu bar, Settings and the viewer's own dialogs in the palette's colours; the title bars, the menus' items,
  message boxes and the Open / Save dialogs in Windows' own dark. It is decided once, as the viewer starts,
  so choosing the other of light and dark (or Windows changing its app mode under System) asks to restart:
  "Restart now" closes the viewer,
  saving as usual, and the new one starts once the old one has gone; "No" keeps this run as it is until the next
  start. A light run follows high contrast at once; a dark run asks to restart for it. The dark palette is a
  restrained near-neutral grey (window 22,22,24, panels 36,36,39, fields 46,46,50, text 230,230,232) with an
  accent blue close to the light one's (38,118,204: white text on it, and on its hover and pressed shades, at
  least 4.5:1); icons are the same drawings recoloured from the palette, and the viewport, its background and
  the texture view's own checkerboard / black / white / grey backgrounds are the same in both themes. Item names
  in the rare blue and epic purple are lightened in their own hue on the dark panel so they read. Three parts are
  drawn by the viewer in a dark run: the slider's channel (Windows has no dark slider); the status bar, which
  is wxWidgets' own there so it can take the panel colour instead of Windows' black one (3 px less tall, so the
  viewport is 3 px taller; a screen reader is still told a status bar and its fields, and a cut-off field shows
  its full text as a tooltip, but it has no size grip); and the menu bar's titles (File, View, ...). Windows draws
  a menu bar through the theme wxWidgets darkens only while the window has a title bar, so in the viewer's
  fullscreen -- how it starts -- the bar came out dark grey with black titles. The titles are owner-drawn now,
  the same in fullscreen and in a window: the palette's text, its secondary text while another window is active
  (wxWidgets' own was the disabled grey), the hover colour under the mouse and while a title's menu is open,
  on the panel colour; Windows' widths, Alt with the underlined letter and the names a screen reader reads are
  unchanged. Light and high contrast keep Windows' own status bar and menu bar.
  The Attachments pane (View > Attachments) is on the panel colours too, in both themes. What stays light in
  Dark: the colour, font, find and print dialogs (none of which the viewer opens today). On Windows 10 before
  1903 (build 18362), where wxWidgets has no dark mode, a dark choice gives a light run (and no restart is
  offered).
- **wxWidgets 3.3.3 (from 3.2.10).** The interface toolkit's official prebuilt x64 DLLs (vc14x, from the
  v3.3.3 release: headers, Dev, ReleaseDLL and ReleasePDB) go in `ThirdParty/wxWidgets33`, next to the 3.2.10
  tree in `ThirdParty/wxWidgets3`, so branches still on 3.2 keep building; the viewer needs
  `wxbase333u_vc14x_x64.dll`, `wxmsw333u_core_vc14x_x64.dll` and `wxmsw333u_aui_vc14x_x64.dll` next to it. It
  brings the dark mode above. Kept as it was: the docking manager gets wxWidgets 3.2's flags (3.3's default
  resizes live -- the Unity player at every mouse move of a sash drag -- and lays the window out unfrozen,
  which the one-step Models / Textures switch relies on it not doing); a status line or Geosets notice
  that changes keeps wrapping (3.3 skips a re-wrap at an unchanged width); the About box's icon is scaled
  as before. The search fields' magnifier and clear icons are wxWidgets 3.3's (white in a dark run). A
  layout saved before loads unchanged, but a layout saved by this version is not read by a 3.2 build (it
  then starts with the default layout once). A headless run that ends without a window now exits with code
  255 instead of -1.
- **Character > Import Armory Character: pick the region, realm and name instead of pasting a link.** The
  dialog asks for a region (Europe, Americas & Oceania, Korea, Taiwan -- the regions the importer serves),
  a realm and a character name, and imports with Enter or the bold Import character button; pasting an
  Armory link stays available underneath and reads every link shape it did before. The fields become the
  character link the importer already understands, so both go through the same request. The realm comes
  from the region's realm list when the Armory proxy serves one (typing completes from the list); otherwise
  it is typed, and a realm's name is turned into its slug the way the game's own slugs are made ("Mal'Ganis"
  -> malganis), with the result shown under the field. Looking up, imported and failed are all shown in the
  dialog, which stays open: the success line names the character, race, class, realm and region from the
  answer itself, and failures say what was wrong (no such character, realm not accepted, proxy unreachable,
  a search page instead of a character page) without a message box. Import is disabled while fields are
  empty or a request runs, and the request cannot be started twice. The last region and realm that
  imported, and the last eight characters, are remembered in Config.ini ([Armory]); realm lists are cached
  for a week in userSettings/ArmoryRealms.json. An -armory import that fails now logs why and exits instead
  of waiting on a message box.
- **Armory proxy: a realm list route, and realms with accented slugs.** `?region=<r>&realms=1` returns the
  region's realm list from Blizzard's realm index (cached for a day), and the realm check accepts accented
  slugs such as `pozzo-delleternità` and `festung-der-stürme`, which it used to refuse -- characters on
  those realms could not be imported at all. Both need the proxy redeployed (armory-proxy/README.md).
- **Model > Appearance: "Customization" is now "Character Appearance", under the Mount card.** The "Mount /
  dismount" button at the bottom of the page is gone: the Mount card replaces it. Character > Mount /
  Dismount stays.
- **Embedded Unity renderer: the player keeps a model's state in one slot.** The parsed model and its
  .m2 bytes, the selected animation, the per-sequence track caches, the `.anim` files fetched and in
  flight, the app's last playback state and the display state (geosets, particle colour) of the model on
  screen now live together in a `WmvModelSlot`, and the code that selects, switches and plays a sequence
  acts on the slot it is given, so a second model can be driven by the same code. Nothing the viewport
  shows, logs or reports changes.
- **Embedded Unity renderer: large files reach the player sooner.** The host's socket to the player now
  has a 4 MB send buffer. With the default, a 12-19 MB model or skeleton spent most of its transfer
  waiting for the next 20 ms poll; a character now reaches the Unity viewport in about a second instead
  of nearly two.
- **Nothing loads until you ask it to.** Launching opens an empty fullscreen viewer; the client is
  chosen from File > "Load World of Warcraft", which is now the only path in the application that
  loads one. Previously the picker was the first thing on screen, and briefly after that the saved
  client was loaded silently instead — both are gone in favour of the program simply opening and
  waiting.
- **Double-clicking the exe no longer opens a dialog first.** (Fixed on the way: the first cut
  of this crashed on launch, twice over. The silent load read the picker's chosen folder without
  the picker ever having committed one — that value is only settled when Load is pressed — so CASC
  was handed an empty game folder. And it then checked `isWoWLoaded` to decide whether the load had
  worked, which is a flag whose only assignment sits inside a commented-out block: always false, so
  a perfectly good load looked like a failure and the client was loaded a SECOND time on top of the
  first. Both paths now go through the dialog's own commit, and the auto-load stops where pressing
  Load stops.) The client picker was the first
  thing on screen: the main window existed but was small, the modal sat on top of it, and the app
  only filled the screen once the user had answered a question. The window now goes fullscreen and
  the renderer starts warming BEFORE anything modal, and the picker is skipped entirely when it has
  nothing to ask — it already seeds itself from the saved folder and detects the client, so pressing
  Load was the only thing left to do. It still appears when there is a real question (first run, a
  moved install), centred over a running application, and File > Client Choice is untouched.
- **Startup is viewer-first.** An interactive launch goes straight to fullscreen on the viewport (F11
  or Esc leaves it), and the Browse, Model and Animation panels come up as they were left. The
  "Starting Unity renderer..." caption is gone: a model loaded while the player starts simply
  appears when it is built, because a message that is on screen for exactly as long as it takes to
  read is worse than nothing being there, and with nothing loaded the empty viewer's prompt says what
  to do next. And the spinning placeholder cube no longer appears at all unless `-wmvPlaceholder`
  asks for it: it was there to prove the embedded player was alive, which stopped being the question
  a long time ago.
- **The Unity viewport is ready before you need it.** The renderer is started when the application
  starts rather than when the first model is picked. It is a game engine and takes about a second to
  come up (~1.1 s, measured); started on demand, that second sat between picking a creature and
  seeing it, which made loading a model look slow when the model had nothing to do with it. The
  player is also now built with its splash screen disabled -- a "Made with Unity" logo belongs in a
  game, not in the middle of a model viewer -- so the logo is gone rather than merely moved.
- **Unity is the only viewport; the OpenGL viewport is archived.** The Unity viewport is the centre
  of the window from startup, with no option to turn it off: File > Reset Layout keeps it in the
  centre, layouts saved by earlier builds are discarded once, and an old
  `Tools/UnityPrimaryViewport` setting is ignored. No menu item, setting, layout, command-line switch
  or failure path can show the OpenGL canvas any more. The canvas object is kept as a hidden internal
  service -- it still owns the GL context textures are decoded with, the loaded model and the
  animation clock the Unity viewport mirrors -- but it is never shown and never paints, and
  `-unityipctest` fails if it does. Exports no longer take their pose from the last drawn frame: the
  pose at the Animation panel's current frame is computed just before an OBJ or FBX export reads it,
  and scrubbing updates the pose without drawing anything.
- **What the Unity viewport cannot show yet gets a notice, not another viewport.** The content still
  loads -- the panels, Info and the exporters keep working on it -- and the viewport says what is
  loaded and that it cannot be shown yet: an image picked in Browse, a map tile, a mounted
  character, a model with no FileDataID (legacy clients), a character the player could not build
  (with its reason), and a character on a player build too old to dress one. A missing player build,
  one that will not start, one that crashes or loses its connection, and one that is still running
  but has not answered 30 seconds after launch -- caught by a two-second check, not at the next
  load -- get a notice with a "Restart Unity renderer" button instead of a
  dialog; View > "Restart Unity Renderer" does the same. With nothing loaded the empty viewer's
  prompt stays. A build without the Unity player (not Windows) says the viewport is not available on
  this platform.
- **Model Control is now View > "Attachments...".** It keeps what reaches the viewport: the list of
  the model and its attachments, which picks the model the Animation panel drives, and Render and
  Scale, enabled for items attached directly to a character (the only ones the viewport is sent).

### Removed
- **Browse's Show list.** Browse lists what the viewer mode shows, with no category of its own to choose: in
  Models mode the models, in Textures mode the textures, and in Buildings mode the WMO roots "WMOs (*.wmo)" listed
  (see the Buildings entry above; they were briefly in the models' tree). Viewer = Textures with Browse listing models can no longer happen. The other categories are gone:
  ADTs, WAVs, OGGs, MP3s, Shaders (*.bls), DBCs, DB2s, LUAs, XMLs and SKINs had no viewer -- picking one of their
  rows only unloaded the model on screen, and a map tile's reader has long been commented out (the Unity viewport
  cannot draw one either) -- and their only working action was the row's right-click Save... of the raw file. On
  a client with FileDataIDs any such file can still be saved as it is from Textures mode: search its FileDataID,
  pick the "Look up FileDataID" row it gets, Export original file. Browse's search box says what it searches
  ("Search models" / "Search textures"), its status line is under the tree, and the room the Show row and the
  "Search" label took goes to the tree.
- **The main-viewport toggle.** View > "Unity as main viewport" and its `Tools/UnityPrimaryViewport`
  setting, and the View > "Unity Renderer" item, which is now View > "Restart Unity Renderer".
- **Screenshots and image sequences, until the Unity viewport has a capture of its own.** File >
  Save Screenshot (F12), the command bar's Screenshot button and File > Export Image Sequence all
  captured the OpenGL viewport. Their implementation stays in the tree, unreferenced.
- **Controls that only changed the OpenGL viewport's drawing.** View > Background Color and Load
  Background (Ctrl+L), the Camera submenu (Front, Back, Side, Perspective, Reset, Use model camera)
  and the command bar's Reset camera button, Set Canvas Size, OpenGL debug info in the title bar,
  Ctrl+B (bounds), F1-F4 and Ctrl+F1-F4 (saved views), the canvas's mouse camera, numpad camera keys
  and 0-9 speed keys, the Lighting menu remnants, Options > "Always show default doodads in WMOs",
  the Settings > Display page, Settings > General's "Show Particle" and "Zero Particle", Model
  Control's alpha, bones, wireframe, bounds, texture and particle options, and the Help > Keyboard
  Shortcuts rows for the OpenGL viewport. None of them reached the Unity viewport. The settings they
  kept (canvas size, background colour and image, particle flags, `Graphics/*`, the screenshot
  counter and format) are no longer read or written; old values in `Config.ini` are ignored.
- **Batch screenshots.** Headless `-mo`, `-item`, `-npc`, `-armory` and `.chr` runs no longer write an
  `ss_*.png`; they log one line saying screenshots are not available in the Unity-only viewer. The
  `-imgseq` smoke test is gone. `-unityipctest` and `-fbxexport` still run.

### Fixed
- **Classic characters get their whole appearance.** Whether a customization choice is offered depends on the
  classes its requirement names, and the viewer judged that against Retail's 15 classes: a choice for "every class
  but Death Knight" had to name classes 1-15. Each Classic client writes those masks over its own classes (Classic
  Era and MoP Classic 0x7DF, Classic Beta 0x37DF), so every such choice was dropped -- the whole Face option of a
  Classic Era or MoP Classic character, and the Eye Color option of a Classic Beta one, whose eyes then rendered
  blank white. The classes now come from the client's own ChrClasses (Retail's are still 1-15, so nothing changes
  there): MoP Classic's Human male gets 58 of its 80 choices instead of 46, Classic Beta's 96 of 131 instead of 77,
  and the Demon Hunter checkbox is offered only by a client that has Demon Hunters. A client whose ChrClasses is
  not installed has no class context: the choices a class mask limits are left out and counted in the log, never
  judged against another client's classes; choices for every class stay.
- **Classic textures listed twice open: MoP Classic characters are no longer untextured white.** MoP Classic and
  Classic Beta list their textures twice, a high-resolution version first and the standard one after it, and
  Battle.net installs the high-resolution ones only with an optional package. The first version listed always won,
  so without that package every such texture -- skins, faces, hair, and most creature and item textures -- failed
  to open although its standard version is on disk. When only a later version of a file is installed, that one is
  now used (120,209 files in MoP Classic 5.5.4, 96,634 in Classic Beta 1.60.1; none in Retail or Classic Era, which
  list one version).
- **Classic Beta's Skyborne characters are no longer white.** A model was treated as a character only when the
  listfile put it in the character folders, and the listfile has no real name for the two Skyborne models: it lists
  them as models/creature/unk_exp00_7478487 and _7478494. Picked from Characters (High Order or Windshaper
  Skyborne), or shown as one of the Skyborne NPCs, they loaded as creatures -- no race (Windshaper came up as High
  Order), no customization, no equipment, and an untextured white model, although their skin had been composed. A
  model a race's ChrModel uses is now a character whatever its file is called; the folder rule still covers files
  the client's tables do not name. Only those two files change (none in Retail, MoP Classic or Classic Era).
- **Humanoid NPCs wear their own appearance.** An NPC whose display has extended info (Bolvar, Garrosh, a city
  guard) was shown on its race's model with the race's default -- or, with Random Looks on, a random -- face, skin,
  hair and beard; only its equipment was its own (Bolvar came up bald and beardless). The NPC's stored appearance
  (CreatureDisplayInfoOption: one choice per option) and its race, sex and class (CreatureDisplayInfoExtra) are now
  read in every client and applied: NPC-only choices and options no player is offered (Eye Style) included, without
  any of them becoming selectable in the character panel, and the options an NPC stores nothing for defaulted as a
  new character's are. The race decides which options the model has, so it is applied too: MoP Classic's Human
  NPCs no longer come up as Gilnean (the race listed first on the Human files), Classic Beta's Windshaper Skyborne no
  longer as High Order. A Demon Hunter NPC is shown in the Demon Hunter context. NPC equipment: a helmet now hides
  what it hides on a player (hair, ears), and Retail's empty equipment slot 11 no longer replaces the helmet (about
  1,340 NPCs lost theirs). The body textures now match the client's own baked NPC textures closely (Bolvar's face:
  mean difference 39 -> 10 of 255; Garrosh's 25 -> 4). A saved character names its race and sex, so a race that
  shares its model file with another (Mag'har, MoP Classic Humans, Skyborne) loads back as itself; a saved NPC keeps
  its NPC choices.
- **Equipment looks the same every time it is loaded.** A shirt, a chest piece and legs share geoset groups (the
  sleeves, the robe skirt and three more), and which item's look a shared group got depended on the order of an
  unordered set: it could change from one load to the next. Archmage Arugal and Grand Magister Rommath came up in a
  robe or in trousers, Lorlien sometimes without legs at all, and a player's long-sleeved shirt under a vest, or a
  plain shirt under a robe, came and went the same way (9 of 12 Retail NPCs with such a collision changed over six
  loads). The items are now applied shirt, legs, chest, then the rest; a group an item leaves at its default no longer
  replaces one an earlier item sets, and where two items set it the outer one wins. An NPC's chest counts as a robe
  when its display sets the robe skirt, and an NPC's clothes cover the underwear, as a player's do. Two robe faults
  are fixed for everyone: a robe no longer takes the forearms and hands off a character whose gloves slot was never
  set (a freshly loaded character, an NPC without gloves), and under a robe the boots now keep their own feet instead
  of the bare toes painted with the boots.
- **Browse no longer keeps every tree it ever built.** Each rebuild of the Models or Buildings tree -- a search,
  clearing it, another client -- left the previous hierarchy of folders and files behind, never freed (about
  90 MB for Retail's models each time: eight searches for "bear", each cleared again, grew the viewer from 2.35 to
  3.08 GB). A node now owns the nodes below it, and a hierarchy that is replaced is freed once no row of the tree
  points into it: right after the rebuild has shown its rows, so a search is as quick as before (freeing Retail's
  model tree takes about 0.1 s). The same eight searches now leave the viewer where it was, and switching Retail
  <-> Classic Era no longer adds about 110 MB per round trip.
- **Opening another client replaces everything of the one before.** Loading a second client (or the same one
  again) kept the previous one's database connection, table structures, file name index, races, character
  texture caches, mount and creature lists and the model on the canvas, and could crash or hang. Each load now
  opens the new client first (a client that fails to open leaves the loaded one untouched), then lets go of the
  old storage and database and clears what was on screen and cached from it (the Animation panel no longer lists
  the animations or the BLP skins of a model that is gone; races, NPCs and items are emptied too). The files of
  the previous product that another product of the same installation does not list are freed once Browse and
  the character controls are rebuilt, so switching back and forth does not keep them. Retail -> Classic Era ->
  Retail in one session ends with Retail exactly as at start. While a client opens, the rest of the viewer is
  disabled (its loading window lets the event loop run), so nothing can act on a client that is being replaced.
  A legacy MPQ load goes through the same reset, and a folder without MPQ archives leaves the loaded client as
  it is. A database cache another process holds (an export the viewer started, a second viewer) is no longer
  reused for another client: that client's tables are built in memory instead.
- **Classic Era opens.** Its install had no local DOWNLOAD manifest, which CascLib treated as a broken storage
  (error 1392). The manifest only lists download priorities and tags; when it is not in local storage the storage
  now opens without it. A DOWNLOAD manifest that is in local storage but cannot be read or parsed still fails the
  open.
- **An empty table no longer crashes the database load** (Classic Era's `Mount.db2` has no records) or logs an
  SQL error; a section that points outside its file is refused with an error instead of read.
- **Switching between Models and Textures happens in one step.** The switch hid and showed the panes, the texture
  view and the Unity player's window one at a time and laid the window out more than once, so the viewport
  briefly grew into the space a closed pane left, the panes folded away one after another, and on the way back
  the player's window could stay at the size it had while hidden. Now the panes are frozen, every layout change
  of the switch (panes, texture view, viewport) is committed in a single `wxAuiManager::Update`, the player's
  window keeps its place and size until that layout is final and is hidden or shown together with the one
  repaint, and the panes that were closed before the switch stay closed after it.
- **Typing in Browse's search box no longer loses the keyboard to the tree when the results come in.** Emptying
  the tree to refill it gave the Windows tree control the keyboard focus, so after the first results a key typed
  went to the tree (jumping to a row) instead of the search box; the search box (or whatever had the keyboard)
  has it again after the tree is refilled.
- **A game file whose storage read returns nothing is a failed read, not a buffer of leftover memory.** The
  shared file reader (`UnityAssetAccess::readByPath` / `readByFileDataID`) retried a storage file whose stream read
  returned zero bytes -- an encrypted file without its key, data the game has not downloaded -- in memory mode,
  whose open does not report the failure, and handed back whatever the buffer held before. It now reports
  "short read ... file may be encrypted or damaged" for such a file. The memory-mode read itself (legacy MPQ
  clients, files from a custom folder) had the same hole when an archive failed part way: `GameFile` now notes
  whether its memory-mode open read the whole file (`readComplete()`; `open()` behaves as before for every other
  caller), and the reader reports "incomplete read" instead of passing the buffer on.
- **A model no longer crashes the viewer when one of its files cannot be read.** Loading a character could
  crash with "Attachment::tick" in the log -- a mislabel: the release build names each frame after the nearest
  exported function. The game storage sometimes lists an animation (.anim) file whose size it cannot report;
  `GameFile::open` still answered "open" with no data behind it, and the bone setup
  (`Animated<T>::init`) read the keyframes through a null buffer. Such a file now counts as not opened
  (logged as `Size query for "<path>" (ID: n) failed`), and the model loads without it. The same failure
  hit elsewhere in model loading is handled the same way:
  - an animation whose file cannot be read keeps empty tracks, instead of keyframes read from the skeleton at
    offsets meant for the missing file (playing one left 219 of a dwarf's 231 bones non-finite);
  - a parent skeleton (the allied races' shared rigs) that cannot be read, or is not listed, leaves the model
    without bones or animations, as when its own skeleton does not open, instead of crashing on the unopened
    file or an `std::out_of_range`;
  - a model with no animations gets no animation manager (its clock read the first animation and crashed);
  - a model without bones gets no particle or ribbon emitters (they pointed into the empty bone list).
  Normal loads are unchanged: eight characters and creatures log exactly as before.
- **Glowing eye colours glow in the Unity viewport.** A character's eye pass is a Mod_Add material (pixel
  shader 8): the iris, plus a second texture added on top of the lit colour. That second unit reads, through the
  mesh's second UV set, a small glow cell at the bottom of the eye image. On 97 of the client's 272 eye textures --
  every blood elf, night elf, void elf and vulpera colour, and orc clan eyes such as `claneyes00_01` -- the cell
  holds a glow sprite; on the rest it is black. The renderer had the addition implemented but held it back for
  every Mod_Add material, so a glowing eye drew as a flat orange iris with a dark pupil. It is now drawn wherever
  that second unit is the character eye (M2 texture type 19). On a Mag'har Orc with `claneyes00_01`, in a
  full-body view next to the game's own render of the same character, the eye's brightest pixels go from
  (234, 187, 36) to (254, 254, 68) against the game's (255, 255, 53), and its peak luminance from 188 to 241
  against 241. On an eye colour whose cell is black nothing changes, to the pixel, and changing the eye colour
  moves the glow with it without a reload. The other Mod_Add batches -- about 1,140 on creatures, spells, items and world models -- are left as
  they were on purpose until they have references of their own: `-wmvModAddLobe=off|eyes|all` overrides the scope
  at run time, and `off` draws exactly what the renderer drew before.
- **Armory character import reads the links the Armory hands out today, and says why an import failed instead of
  importing nothing.** The Armory moved character pages to `/<locale>/worldsoul/<region>/armory/character/<realm>/<name>`
  and put a search page at `/<locale>/worldsoul/<region>/armory?q=<name>`. The importer pulled the realm and name out
  of a link by counting characters and slashes, so a search link -- the one a browser is showing while you look for
  the character -- was read as realm "eu", character "armory", and a character page with a trailing slash was read as
  having no name at all. Worse, the failure was silent: the profile API answers an unknown character with a 404 whose
  body is JSON (`{"code":404,...}`), the importer only asked whether the body parsed as JSON, and so imported a
  character with no race, no customizations and no equipment -- the model on screen simply never changed. Links are
  now read as URLs (scheme, host, path segments, query), which handles the current form, the 2023
  `/character/<region>/<realm>/<name>` form, the older `/character/<realm>/<name>` form whose region comes from the
  locale (including `pt-br` and `es-mx`, which are US), classic realms, a trailing slash, a query string, a link
  pasted without `https://`, and names with accents. The HTTP status and the payload are both checked, so an error
  can never be dressed onto the model, and each failure names itself: a search link explains where to find the
  character's own link, a 404 names the character, realm and region and mentions hidden profiles, 401/403 points at
  the proxy's access key, 429 says to wait, and an unreachable proxy reports the network error. The log records the
  request URL, the HTTP status, the response size and the reason.
- **Races that share a character model with another race are no longer missing from the race table.** It was
  keyed by the model's FileDataID, so the second race on a model file was silently dropped -- and with it went
  Mag'har Orc (on the Orc model), both faction Pandaren, and ten more: 46 of 58 races survived, 75 of 103
  race-and-sex rows. Nothing could resolve those races to a model, so a Mag'har Orc could not be loaded at
  all, and the Browse "Characters" tree never listed them. The table is now keyed by race and sex, keeping
  the HD model where a race has several; a second table, keyed by model file as before, still answers "which
  race is this model", so loading a model by file id resolves exactly as it did.
- **Picking a race in Browse > Characters loads that race, not the race that shares its model.** The race
  browser's rows name a race and a sex, but the pick carried only the model file, so Mag'har Orc loaded an
  Orc -- with Orc customization options in Model > Appearance. Each row now carries its race and sex through
  to the model load, and picking a different race on the model already loaded (Orc to Mag'har Orc) switches
  it in place instead of being ignored as "the same model".
- **An Armory import of a race that shares its model now imports that race's appearance, not the other
  race's.** A character model is read as the first race on its file, and the customization options come from
  that race's ChrModel -- so importing a Mag'har Orc applied 0 of her 9 customizations, leaving a default Orc
  wearing her gear. The import now tells the model which race it is (`WoWModel::setRaceSex`) and rebuilds the
  options before applying the appearance: the same import applies 9 of 9. An import of a race with no model
  at all still stops with a message naming the race, instead of dressing whatever was on screen -- it used to
  apply the character's customizations and equipment to the model already in the viewport, or do nothing
  whatsoever when the viewport was empty.
- **Choosing, swapping or taking off a mount no longer makes the animation jump or start over in the Unity
  viewport.** A mount choice holds the UI thread while the mount's model loads (1.5-1.9 s, measured) and starts the
  mount and the character's riding animation in that wait. The first tick after it counted the whole wait into
  both, so the viewport, which starts them when it is told, was snapped at the next heartbeat: by 1.8 s on a
  four-second mount idle and 150-800 ms on the rider, and a dismount snapped the standing character by 360-400 ms.
  The playback state sent during the wait also carried the rider's position from the tick before it (a jump of up
  to 690 ms), and swapping one mount for another started the rider's riding animation over from its first frame.
  A clock set outright -- a clip chosen, a stop, a mount choice -- now counts only from that moment, a state read
  between ticks is carried on to the present, the first tick after such a change sends the playback state at once,
  and a character already on its riding animation keeps it running when only the mount under it changes. Around
  mounting, swapping and dismounting the viewport now makes no heartbeat correction at all (the largest difference
  left is 4 ms); a clip picked again still starts from its first frame.
- **A character's animation no longer jumps back or restarts in the Unity viewport when an item or a
  customization changes, or when View NPC is opened or closed.** The viewport's animation clock kept running
  while the app's UI thread was busy, but the app's clock fell behind it in three ways, and the viewport snapped
  back to the app's time at the next heartbeat (a jump of 200-470 ms in a one-second walk cycle, measured): the
  heartbeat was sampled before the tick after the wait had advanced the clock, a looping animation that wrapped
  in that tick restarted at frame 0 and dropped the time past its end, and a state that reached the viewport
  behind a composited body image (a customization) was applied as if it had just been sampled. The tick now
  samples after advancing, the time past the end carries into the next loop, and each playback state says when
  it was sampled (`sampledAtMs`) so the viewport moves it on by the wait. View NPC also no longer refreshes the
  whole character and sends its scene again, and its list opens in 0.7 s instead of 2.8 s for 22,991 NPCs: the
  item and NPC choice dialogs no longer fill the hidden list box they replace with their own list.
- **Stretched particles drip along their motion, or scatter at their own angles, in the Unity viewport.** A
  camera-facing particle quad was always drawn square to the screen and turned only by the emitter's sprite
  rotation (particle params +124), one angle for every particle of the emitter. Two authored settings were
  ignored. Flag 0x4 (VelocityOrient in the public M2 format documentation; unnamed and never tested by the legacy
  runtime) lays the quad along the particle's velocity as the camera sees it, with the size ramp's x and the
  texture's U along the motion. Primeval Skyfriend's belly emitters author a ramp from 0.011 x 0.48 to 0.61 x 0.09
  over a droplet texture: in a reference render each particle leaves the belly as a flat blob and draws out into
  a strand hanging along its fall, round end first -- slime dripping -- where the viewport drew a needle that
  flattened into a level bar. Across the client's 13,474 creature emitters, 55 % of the 1,285 that set 0x4 author
  a stretched size ramp, against 1.9 % of the rest, and 92 % of those are long in x. The other setting is the
  per-particle start angle, a base and a variation at params +116/+120 (baseSpin and baseSpinVariation in the same
  documentation, never read by the legacy runtime): every other camera-facing quad now takes the base plus its own
  random share of +/- the variation when it spawns, so a stretched texture no longer stacks into parallel lines.
  A velocity-oriented quad takes neither angle, as the reference render shows on those belly emitters although
  they author a full turn. An emitter that sets neither draws exactly as before, random numbers included; the spin
  speed at +124 is still one fixed angle.
- **A mount, NPC or creature whose display id is also an item display id keeps its skin.** Picking a skin by
  display id (a mount, an NPC, an Armory import) passed that CreatureDisplayInfo id on as the ItemDisplayInfo id
  the per-slot material table is keyed on. Where an unrelated item had a row under the same number, that item's
  pass counted as authoritative and cleared the creature skin textures bound a moment earlier, so the mount went
  to Unity without them and drew white (Primeval Skyfriend, display 144856). `AnimControl::SetSkinByDisplayID`
  now passes the group's own item display id, which is 0 for a group built from CreatureDisplayInfo; item skins
  are unchanged.
- **A creature's fourth texture variation is applied.** CreatureDisplayInfo has four texture variations, but the
  skin list kept three, so a model slot that takes the fourth stayed unbound: Primeval Skyfriend's saddle drew
  white. The fourth variation fills texture type 5, not 14: in the client data 812 of the 946 displays that
  set it use a model declaring type 5, the others a model with no slot for it, and no creature model
  declares type 14. The skin list, the skin sent to the Unity viewport and the database fallback in
  `UnityAssetAccess` now use that mapping (`TextureGroup::textureType`) for mounts, `-mo` creatures and NPCs.
  Older clients whose table has three variations, and user skin files, still read three.
- **An item effect no longer draws as a black rectangle after the item level is changed.** A merged armour part
  the host re-creates for a new level can come back under the same key. The Unity player then only re-pointed
  its textures, but every material keeps the combiner and alpha it was built with, so a texture type the first
  level did not name (the effect texture of Chosen Bloodslayer's Fanged Grips at levels 3-6) was drawn with no
  second unit and an opaque alpha, and its alpha-blended card showed as a solid dark rectangle. A merged part
  whose filled texture slots change is now built again; a change of files in the same slots is still rebound.
- **A long equipment item name no longer makes Model > Appearance wider than the panel.** The name is cut
  short with an ellipsis where the panel ends. The full width of a name such as "Thunderfury, Blessed Blade
  of the Windseeker" used to become the page's minimum width the next time the page was laid out -- mounting
  the character did that -- which pushed the customization rows off the panel's right edge.
- **A model that fails to load no longer reads the character it replaced after it was freed.** Loading a
  model frees whatever was on the canvas before the character control is given the new one, and the
  viewport state -- which now asks whether a mounted character is on screen -- is refreshed in between when
  the new model cannot be opened. The character control's two pointers into the freed model are cleared as
  soon as the canvas may have freed them, so that window reads nothing.
- **A mount being prepared that is superseded no longer costs a second fetch.** Dropping the mount a newer
  scene replaced also forgot the files already on the wire for it, so the same mount chosen again asked for
  every one of them a second time and the first answers were reported against whatever load was in flight.
  The work is dropped; the account of what is still coming is kept, as the character dresser already did.
- **An item equipped on a mounted character is put on at once.** Picking an item for an equipment slot,
  or changing an item's level, while the character rides a mount loaded the item but refreshed the
  mount's (empty) equipment instead of the character's, so the change only appeared at the character's
  next refresh. The refresh now follows the character the controls act on.
- **Model > Appearance offers the customization options and choices the character can use.** An Undead
  male listed 1 of its 11 Jaw Features, had lost Skin Type "Bony", showed an empty "Eye Style" row, and
  offered every Skin Color, both "Rotting" Face Features and Eyesight whatever the choices they depend on.
  The requirement rows were read wrong: `ChrCustomizationReq.RaceMasks`, two uint32 in the file, was
  loaded as one 64-bit value taken from half a pallet entry with its high half copied out of uninitialised
  memory, and a heuristic then read meaning into those bits. The loader now stores `RaceMasks1` and
  `RaceMasks2` at the file's own pallet stride, widens 64-bit columns instead of copying eight bytes out of
  a four-byte value, sign-extends bitpacked signed values (a ClassMask of -1 no longer reads 65535) and
  reads short ids and values without over-reading them (ChrClasses ids are 1-15 again). It also loads
  `ReqType`, `RegionGroupMask`, `OverrideArchive`, `ChrCustomizationOption.Requirement`,
  `ChrRaces.PlayableRaceBit` and `ChrCustomizationReqChoice` (database schema 13: the cached database is
  rebuilt once).
  One evaluator decides for options and choices alike: ReqType bit 0 (a player requirement -- the classic
  races' NPC "Eye Style" and the "Transmog" placeholder are not), the race's PlayableRaceBit in RaceMasks
  (not the race ID - 1), ClassMask against the class context (the Demon Hunter checkbox, otherwise every
  class but Death Knight and Demon Hunter, so a Death Knight eye glow is no longer offered to every
  class), the achievement, quest and item unlock gates as before, and prerequisite choices (one of the
  listed choices of the option they belong to must be current; a list spanning several options is logged
  and not evaluated). OverrideArchive and RegionGroupMask are loaded but not evaluated.
  The panel builds a row only for an option the character can use, and rebuilds rows and lists when a
  choice changes them: Skin Color follows Skin Type, Face Features offers the "Rotting" the Jaw allows,
  Eyesight goes with Eye Color "Sockets". A current choice that stops being valid gives way to the first
  valid one, and a saved character is resolved as a whole once it is read. What the character wears is
  rebuilt from its current choices after every change, so changing Face, Hair Style or Eye Color no longer
  leaves the previous choice's face, scalp or iris texture in the composite; equal texture layers keep the
  order they were added in; one change refreshes the model once instead of twice. New headless check:
  `-dbfromfile -customizationtest` (the stored requirement values, the rules, and an Undead male, a Night
  Elf male, a Dracthyr and a Dark Iron Dwarf as the panel sees them).
  Still open: the default lower jaw (geoset 101) stays drawn under every Jaw Features choice, which shows on
  "Drooler"; "Sockets" still draws the eye mesh (it names geoset 3300, which the model does not have), and
  the Unity viewport keeps the previous eye texture when "Sockets" is picked live; six of a Human's skin
  tones are listed only once a Face they require is chosen; Face bone shapes (BoneSet) and
  ChrCustItemGeoModify are not applied.
- **Choosing "None" in the mount list no longer crashes.** Dismounting read the mount's scale from the
  canvas root after the mount had been detached and freed, and with no mount up it freed the character
  itself (the canvas model it was replacing was the same character). The mount's scale is now read before
  it is freed, and the character is handed back to the canvas without being deleted. The `-unityipctest`
  character check now dismounts twice, with no mount up and with a mount up, and checks the character
  is still the canvas model and is dressed in the Unity viewport again.
- **Selecting a WMO no longer builds it for OpenGL, twice.** The host opened every group file,
  compiled it into a display list for the hidden canvas, uploaded the material textures to GL, and
  then rebuilt every group again (leaking the first build) -- all for a canvas that never paints. It
  now reads the root's metadata only (what Model > Info, the doodad-set list and the status bar use).
- **Switching away from a WMO no longer writes into freed memory.** Deleting a WMO left the canvas root
  and the doodad-set list's target pointing at it, so the next load of anything, or closing the
  application, wrote into the freed object. A WMO is now detached from both before it is deleted, and
  one still loaded at exit is freed properly. A root that fails to open reports zero groups instead of
  uninitialised counts, and a WMO picked again after another model gets its doodad-set list applied
  again.
- **Browse's file-type list picks the type it names.** "OGGs" and "SKINs" were missing from the
  list the selection is matched against, so every later entry was one out: "MP3s" ran the image
  handling and "Images (*.blp)" did nothing. Picking an image now names it in the viewport's notice
  (saving one stays the right-click menu's job).
- **A model loaded from a menu replaces a map tile.** Loading an NPC, an item, a character file or
  an import after picking an ADT in Browse left the map tile loaded behind the new model.
- **Embedded Unity renderer: bone keyframes stored in `.anim` files are read again.** The emitter
  work had routed bone tracks through the model-buffer-only reader meant for emitters, so every
  sequence whose keys live in an external `.anim` file posed its bones from the wrong buffer. The
  keys are taken from the file's AFSB chunk, as the host takes them.
- **Embedded Unity renderer: global sequences ran faster with every animated model on screen.** The
  shared global clock was advanced by every animator each frame; a character with a weapon and two
  shoulder models ran them four times too fast. It now advances once per frame.
- **Embedded Unity renderer: the viewport no longer holds for about a second after every animation
  change.** The dropdown handler does three things -- `Stop()`, then select, then `Play()` -- and
  only the middle one told the renderer anything. So the state that travelled with the selection
  said "this animation, and it is NOT running", `Play()` resumed the app without saying so, and the
  Unity pane sat on the first frame until the one-per-second heartbeat came round. That is the
  half-second-to-a-second hold, and it explains why it happened on every model, why it survived the
  parse, fetch and cache work, and why no headless run ever saw it: the harness called
  `SelectAnimation` directly, which is not the path the dropdown takes.
  All three places with that shape now push the settled state after `Play()` -- the animation
  dropdown, the loop control, and the selection made while a model loads. The heartbeat goes back
  to being what it was meant to be: drift correction, not the thing that starts an animation.
  Measured after the fix, animation time resumes **on the same frame as the switch, 0 ms later**,
  and `-unityipctest` now drives the real dropdown handler and fails if the state that follows a
  pick does not say the animation is running.

- **Embedded Unity renderer: picking an animation takes effect at once.** Two things stood between
  a selection and the animation actually starting, and both were found by timestamping the whole
  path rather than reasoning about it.
  First, a sequence whose keyframes live in a .anim file fetched them on the FIRST switch to it --
  16-18 ms of round trip during which the PREVIOUS animation stayed on screen, so picking one
  looked like nothing had happened. Those files are now fetched when the model loads (Agronn: 8
  files, ~300 KB), so no switch waits on one; measured, zero switches are now deferred.
  Second, the playback state that follows a selection was DISCARDED whenever the renderer was not
  already on the sequence it named -- which is every fallback to the idle and every switch still
  resolving. The app's play/pause and speed are properties of the app, not of the sequence, so they
  are now applied regardless, and the position is applied as soon as it is known which sequence is
  playing. Before, a switch in those cases kept the previous animation's transport until the next
  heartbeat corrected it.
  For the record, the selection itself was never the delay: WMV already pushed `modelAnimation` and
  `modelAnimationState` from the same control path, and the renderer receives them **0-2 ms apart**.
  The heartbeat corrects drift; it was never what started an animation.

- **Embedded Unity renderer: animations stored in .anim files now play.** A creature sequence
  without flag 0x20 keeps its track headers in the .m2 but its keyframes in a separate .anim file,
  named by the AFID chunk. The Unity viewport used to detect that and fall back to the idle, so an
  animation that played perfectly in the OpenGL viewport did nothing in the Unity one --
  **Agronn's SitGroundDown** (sequence 37, animID 96, flags 0x81, 3533 ms) being the reported case.
  The .anim bytes now travel over the ordinary asset channel and the parser reads the entries from
  them while still reading the headers from the .m2, which is exactly the split the legacy viewport
  makes. Agronn gains 6 such animations, valkier 8, chicken2 6. Each file is fetched at most once
  per model. A sequence whose .anim cannot be served falls back to the idle, names the file, and
  leaves the previous animation running.
  Two of Agronn's sequences (36 and 40, animIDs 72 and 71) still fall back: they carry neither the
  0x20 flag nor an AFID entry, so their keyframes are in no file at all. The legacy viewport has no
  alias or replacement path either -- `NextAnimation` is only ever written to XML, never read -- so
  it has nothing to play for them either.

- **Embedded Unity renderer: switching animation no longer stutters.** Picking a different
  animation used to re-parse the entire .m2 -- vertices, textures, materials, lookups and all --
  to get at one thing: the bone tracks for the new sequence. It now reads just those, into the
  model already in memory. Nothing is re-requested, nothing is rebuilt, and the mesh, materials,
  textures, geoset selection and skeleton are left untouched; `loadWoWModel` stays the only path
  that builds anything.
  **The clock was never the whole story, which is why this needed measuring rather than guessing.**
  The old path took 2-9 ms, too little to see. What the user could see was the GARBAGE: about
  1-1.7 MB per switch, collected a frame or two later, landing as a hitch just as the new animation
  started. The largest single piece of it was the MD21 slice the parser works in, copied out of the
  file again on every selection; that slice is now cut once when the model loads and reused.
  Per switch, measured over the validation models: 2.3 -> 0.1 ms and 1072 -> 108 KB (chicken2),
  7.3 -> 0.0 ms and 512 -> 168 KB (horse3), 3.9 -> 0.4 ms and 1032 -> 244 KB (valkier),
  9.4 -> 1.0 ms and 1680 -> 648 KB (shaboss_doubt).

### Added
- **Embedded Unity renderer: it now follows the transport controls too.** The viewport already
  played the animation you picked; it now plays it the way you are playing it -- pause holds the
  pane on the same frame, resume carries on from there, the speed slider changes its speed, and
  dragging the frame slider scrubs it. A new `modelAnimationState` push carries whether the
  animation is running, how fast, and where in the sequence the app is.
  **This one had no funnel to hook.** Unlike the skin and the animation choice, playback state is
  changed from seven places -- play, pause, stop, clear, the two step buttons, the speed slider and
  the frame slider -- and the time advances every frame with no control involved at all. So it is
  pushed both ways: forced from each of those controls, and on a one-per-second heartbeat from the
  canvas tick while something is playing.
  The heartbeat is a correction channel, not a stream, and the **player** decides what to do with
  it: a time difference under about a frame (40 ms) is ignored and anything larger snaps. That split is
  the design. Snapping to every message would trade drift for a visible stutter once a second;
  never snapping would let two independently-timed renderers walk apart. A scrub or a stop arrives
  with the app's time already far from the player's, so it snaps with no special case, and every
  correction is logged with its size so a run full of them is visible as the symptom it is.
  One inherited behaviour is reproduced rather than tidied: **global sequences keep running while
  the animation is paused, and ignore the speed**. The legacy viewport advances its global clock
  before it decides whether the animation is paused, and its speed multiplier lives inside the
  animation tick alone -- so a torch keeps flickering on a creature held still, in both viewports.

### Added
- **Embedded Unity renderer: it plays the animation you picked, not its own idle.** The viewport
  now follows WMV's animation dropdown -- pick Run, Death or an emote and the Unity pane switches
  with the canvas, as it already did for skins and geoset variants. The selection travels over IPC
  as a new `modelAnimation` push carrying the **sequence index**, which is what the keyframes are
  stored under and what the app's own selector identifies a choice by (its labels end in `[n]`); an
  animID cannot pick one -- chicken2 carries animID 5 on two sequences, as sub-animations 0 and 1. It is sent from
  `AnimControl::SelectAnimation`, one funnel that the default picked while a model loads, the
  dropdown and the loop control all now route through, and again right after the model is sent so
  the renderer starts on the app's choice instead of being corrected a moment later. On the player
  side the model's .m2 bytes are kept and re-parsed for the requested sequence, because only one
  sequence's keyframes are held at a time -- a boss has 109 of them; nothing else moves, so the
  mesh, its materials, its textures and its geoset selection are untouched by which animation is
  playing.
  **Not every sequence can be played from the .m2, and finding out why was the substance of this
  change.** Bit 0x20 on a sequence means its keyframes are stored in that file; without it they are
  in a separate .anim file -- and the trap is that such a sequence's track headers are still
  present, with offsets that address the OTHER file yet land in range often enough that bounds
  checks pass and the data reads as noise. chicken2's sequence 14 is exactly that and is not even
  named by an AFID entry, so looking for the keys cannot distinguish it; only the flag can. Those
  sequences now fall back to the model's idle and log which of the two reasons applied, and a track
  that slips through anyway degrades to a still bone instead of failing the load.
  Validated by walking six animations on each of seven models through the app's own selector: the
  renderer followed every playable one (chicken2's Run, Death, AttackUnarmed, EmoteEat...; horse3's
  six; valkier's six of 109) and fell back with a reason on the two chicken2 sequences that keep
  their keys elsewhere.

### Added
- **Embedded Unity renderer: creatures now play their idle animation.** The rig built by the
  previous milestone stood perfectly still; it now loops the model's default idle. Which sequence
  that is turned out to be the first thing worth getting right: it is NOT sequence 0, but the first
  sequence whose AnimId is "Stand", falling back to sequence 0 only when a model has none -- the
  rule the OpenGL viewport itself uses. On `creature/chicken2` sequence 0 is a run cycle and the
  idle is sequence 2; on `creature/valkier` it is sequence 11. Bone tracks are evaluated the way
  the legacy evaluator does, case for case: fewer than two keys holds the first value, a time past
  the last key holds the last, and otherwise the containing span is interpolated -- stepped for
  "none", interpolated for "linear", and *slerped* rather than lerped for rotations. Hermite and
  Bezier tracks are read as linear (their tangents are not), which no bone track in any validation
  model uses. **Global sequences are implemented, not skipped**: a track bound to one loops on its
  own clock independently of what is playing, and keeps its keys at entry 0 -- `creature/valkier`
  drives 61 of its tracks that way. A global sequence of zero length, which `creature/wrathofazshara`
  actually ships, holds its first keyframe here rather than returning a default value as the legacy
  evaluator does; for a scale track that default collapses the bone to a point. Only the sequence
  being played is parsed, so a 109-sequence boss costs one sequence of keyframes, and only bones
  that actually move are driven. There is no Animator Controller and no clip: Unity's animation
  system wants assets authored at build time and a player build strips them, so the animator writes
  localPosition/localRotation/localScale straight onto the bones -- the same expression the
  skinning milestone derived, with the tracks filled in instead of left at rest, so the bind poses
  are untouched and `-wmvNoAnim` returns the model to a rest pose that still measures identical to
  the static mesh. One rotation subtlety is worth naming: the WoW-to-Unity axis map mirrors, so
  converting a rotation remaps its axis AND reverses its turn, which is checked against the matrix
  route in the parser tests rather than trusted. Measured with the new `-wmvAnimCheck`, which
  samples the idle across its length and bakes the skinned result, vertices move at most 16% of the
  model's own diagonal on chicken2 and stay bounded on every model tested. Nothing but bones is
  animated, and a model whose idle keeps its keyframes in a separate .anim file is left still and
  says so.

### Added
- **Embedded Unity renderer: models are now skinned to their own skeleton.** The renderer drew
  every M2 as a rigid mesh; the bone indices and weights each vertex carries were parsed and then
  ignored. The rig is now rebuilt as Unity transforms, the influences and bind poses are handed to
  a SkinnedMeshRenderer, and the mesh is deformed by that rig. **No animation is played yet** --
  every bone sits in its rest pose, and that is the point: the milestone's contract is that a
  skinned model at rest is indistinguishable from the static one it replaces. It holds because of
  how the format is built. The legacy viewport composes a bone as
  T(pivot) * T(translation) * R(rotation) * S(scale) * T(-pivot), composed with its parent's, and
  with every track at rest that collapses to the identity -- so the positions stored in the file
  already are the rest pose, and the bind pose only has to reproduce the identity. Each bone is
  placed at its pivot with no rotation, which is also exactly the arrangement animation needs:
  adding the translation track to that rest offset and setting the rotation and scale from their
  tracks reproduces the viewport's expression term for term. Measured with the new `-wmvSkinCheck`
  switch, which bakes the skinned result and compares it against the file's own positions, the
  largest deviation across seven validation models (rigs of 29 to 236 bones, 2 to 15 deep) is
  3.8e-6 units, and three of the seven are exactly zero. Two details of the data are easy to get
  wrong and are handled explicitly: the per-vertex bone indices are DIRECT indices into the bone
  array rather than indices through the .skin's lookup table, and a rig is a forest -- valkier has
  27 root bones out of 149 -- with out-of-range, self-referential and cyclic parents all
  normalised to roots at parse time. A model whose bones live in a separate skeleton file (the
  SKID chunk) is still drawn as a static mesh and logs why; over a spread of 300 retail creature
  models, 299 keep their bones in the .m2 itself. `-wmvNoSkin` forces the old static path for an
  A/B, and every debug switch can now also be set through the `WMV_DEBUG` environment variable,
  which is the only way to reach them in the embedded viewport since WMV builds the player's
  command line itself.

### Added
- **Embedded Unity renderer: materials now follow the model instead of an approximation of it.**
  The renderer drew almost everything opaque with a single texture, which is right for a chicken
  and wrong for most of the bestiary. Measured over a spread of 300 retail creature models (1424
  draw batches): 26.6% of batches ask for a blend mode that was being ignored -- additive glows,
  alpha-blended wings, modulated shadows, all rendered as solid geometry -- and 33.4% declare a
  second texture unit whose contribution was dropped. Both now come from the same tables the
  OpenGL viewport uses. Every M2 blend mode is applied; the alpha test keys at 128/255, where the
  legacy combiner keys it; depth write comes from the material's own flag and nothing else (the
  viewport decides it outside its blend switch, so a blended pass whose flag is clear still writes
  depth); and the texture combiners a static pose can reproduce -- the products of the two units,
  the two alpha-masked forms and the decal -- are drawn with unit 1 sampled from whichever source
  the material's vertex shader names, either stored UV set or a generated environment sphere map.
  Combiner coverage over that sample goes from 66.6% of batches to 99.8%; the three that remain
  are logged by name and drawn from unit 0 alone, as all of them were before. Ported case by case
  from the viewport's own GLSL rather than from the combiner names, which matters: several
  combiners differ from a simpler one only in a specular lobe the viewport weights at zero by
  default, so reproducing the default collapses them onto the simpler case. All of it is driven by
  uniforms on one shader variant, because a player build strips shader variants as readily as it
  strips whole shaders.

### Added
- **Embedded Unity renderer: it now shows the right geometry, not just the right texture.** A
  creature display variant can differ from another by which geosets it switches on rather than by
  its skin -- `creature/horse3/horse3.m2` has three dropdown entries sharing one texture that
  differ only in geoset 101, 102 or 103, a long mane and tail against a cropped one -- and the
  Unity viewport drew all of them identically. It now follows the same rule the OpenGL viewport
  uses: a submesh is drawn when its geoset number is 0, or when the displayed variant names that
  number. WMV sends the selected set alongside the textures it already sent, so the app stays the
  only thing that reads the client database; the renderer already knows each submesh's number from
  the .skin it parsed. Switching variants swaps which submeshes hand the mesh their triangles --
  the mesh, its vertices, its materials and its textures are all left alone.

### Fixed
- **Creature display lookup read the wrong columns on retail.** `AnimControl::UpdateCreatureModel`
  builds one of three queries depending on the client generation, but read the display id and the
  particle colour at fixed positions that only match the two OLDER layouts. The modern query
  selects a fourth texture variation the others do not, so on current retail the display id was
  actually `ParticleColorID` and the particle colour was actually
  `TextureVariationFileDataID4` -- both off by one. Consequences, all measured against retail
  12.1: the display-id -> skin map collapsed to a single entry keyed 0 for 97.8% of creature
  displays, so NPC and Armory import (`SetSkinByDisplayID`) could never find the skin a display
  names and silently left the model on whatever was already selected; per-display geoset data was
  fetched with a particle-colour id, so 16% of creature displays never received the geosets that
  define them (and 48 rows received another display's); and creature particle-colour replacement
  never ran at all, because the id it needed was never read. The column positions are now derived
  beside the query that defines them, so the three layouts cannot drift apart again.
  Two visible consequences worth expecting: `creature/chicken2/chicken2.m2` now maps all 41 of its
  displays instead of 1, and creatures whose displays differ only by geoset now offer those
  variants in the skin dropdown -- 433 of 2965 creature models gain entries, 2526 are unchanged.

### Added
- **Embedded Unity renderer: it now shows the skin you picked.** A creature normally has several
  skins -- `chicken2` offers seven -- and the renderer was resolving whichever one the database
  listed first, so the Unity viewport could show a white chicken while the OpenGL viewport showed
  the spotted one the user had selected (or the one "Random Skins" had rolled). The app now
  answers "which textures does this model use" from its own skin selector rather than from the
  database default, and pushes a `modelSkin` message whenever the selection changes -- from the
  dropdown, from the default chosen on model load, from NPC import, and from the per-slot
  folder-texture lists. The player re-uploads only the textures that actually changed and keeps
  the mesh it already built: a skin change alters which image a material samples, nothing about
  the geometry. Re-picking the skin already on screen fetches nothing. Texture metadata now
  carries the WoW texture *type* rather than a bare position, because a model's texture-variation
  order and its M2 texture-slot order need not agree. Geoset variations (a display that toggles
  geometry) remain out of scope for this static-M2 milestone.
- **Embedded Unity renderer: static WoW models now render (M2 + BLP at runtime).** The Unity
  viewport no longer just fetches bytes -- it turns them into a visible model. On `loadWoWModel`
  the player fetches the `.m2`, parses it, fetches the `.skin` profile the model names, resolves
  and fetches its texture(s), decodes the BLP in memory and builds a Unity mesh with one submesh
  per WoW draw batch, correct WoW->Unity axes and winding, a basic material (opaque / alpha-key /
  alpha, two-sided when asked) and bounds-driven camera framing. Solidity is treated as a
  correctness requirement rather than a default: BLP rows are flipped once on upload (they are
  top-down, Unity's raw texture data is bottom-up), transparency follows the WoW blend mode and
  never the texture's alpha channel -- a creature skin has one regardless, and on an opaque
  material it is discarded at upload -- and the shader is screened so it can actually be drawn
  opaque, since always-included fallbacks such as `Sprites/Default` bake alpha blending, no depth
  write and no back-face culling into the pass and silently swallow every attempt to change it.
  `Assets/Resources/WmvOpaque.shader` ships with the renderer as the shader a player build cannot
  strip. Each load logs the material state it actually produced, and `-wmvFlipV`,
  `-wmvForceOpaque`, `-wmvForceSolid` and `-wmvMatColors` isolate a visual fault without a rebuild.
  Draw batches now also honour the two things an M2 uses to say what a material really is: a batch
  loads as many textures as it declares (unit *k* is `textureComboIndex + k`, not just the first
  entry), and its shader id names the combiner and the per-unit UV routing -- chicken2's
  `Combiners_Opaque_Mod2xNA_Alpha` / `Diffuse_T1_Env` makes unit 1 an environment sphere map and the
  base texture's alpha the reflection mask rather than transparency. And a batch the model hides at
  rest by keying its colour track to zero is skipped, exactly as the OpenGL viewport does: chicken2
  ships an 18-triangle eye overlay keyed to alpha 0 whose only visible effect, if drawn, is to cover
  the eye painted into the skin underneath.
  Verified end to end against retail data in a real Unity 6 URP player build:
  `creature/chicken2/chicken2.m2` renders as 1632 vertices, 778 triangles from the one batch the
  model wants drawn, with its 256x256 DXT5 skin resolved from the creature database and its 64x64
  environment unit bound, opaque with depth write and back-face culling on. Textures a model does not name
  itself (replaceable creature skins) are resolved by the app from the client database and handed
  over as FileDataIDs; the rare legacy model that no creature display references any more falls
  back to its conventional sibling skin, explicitly labelled as such. Still runtime-only:
  nothing is exported or written to disk, and there is no OBJ/FBX/GLB step. Animation, the full
  material system, particles and the character/equipment pipeline are not part of this step.
- **Embedded Unity renderer: runtime asset access (V1).** The Unity viewport now talks to the app at
  runtime: the app hosts a localhost IPC server (started before the player launches, port passed
  on the player command line), the player connects back and announces itself, the app tells it
  which model is active (`loadWoWModel`, re-sent on every model load), and the player fetches the
  raw WoW files it needs (`getAsset` by path / `getAssetByFileDataID`) straight from the **active
  client** -- CASC or legacy MPQ, through the same file providers the rest of the app uses -- with
  byte length + SHA-1 so the player can verify what it received. Missing files, unsupported
  lookups (FileDataID on an MPQ client) and a client that is still loading come back as clear
  errors. Nothing is exported or written to disk; this is the runtime channel the Unity renderer
  will render from directly (no M2 parsing/rendering in Unity yet -- next step). Nothing in the
  build depends on Unity. New headless self-test: `-mo <model> -unityipctest` drives the
  whole exchange against the installed player (the Unity-free TestStub speaks the protocol too).
- **Embedded Unity renderer viewport (first step of the new renderer).**
  The viewer can now host a separately built Unity standalone player inside its window.
  This is the foundation of WMV's **new rendering pipeline**: the Unity viewport is the
  application's viewport (the OpenGL viewport is archived, see Changed), and every rendering
  feature (characters, equipment, maps, fog, stream features) targets it. Unity renders
  directly from WoW data -- it requests raw assets/metadata from the app over IPC (no
  OBJ/FBX/GLB export step); the app
  remains responsible for the UI, the active client/profile, CASC/MPQ access,
  databases/metadata and runtime commands. The player is launched embedded (parent-window mode),
  resizes with its pane and is shut down with the app. Nothing in the normal build depends on
  Unity; the player is needed at run time, and if no player build is found (default location
  `tools\unity-renderer\UnityRenderer.exe` next to the exe, or the `Tools/UnityRendererPath`
  setting) the viewport shows a notice saying so, with a button that tries again. The player
  project sources and a Unity-free test stub live in `Tools/UnityRendererProject/`; the direction
  and migration roadmap are in `docs/unity-renderer/`.
- **Legacy WotLK creatures and items now show their real, database-driven textures.** For a loaded
  WotLK 3.3.5 (MPQ) client, the viewer now reads the classic `.dbc` databases
  (`CreatureModelData`, `CreatureDisplayInfo`, `ItemDisplayInfo`) to resolve the skins and item
  textures the game data actually defines — so creatures show their correct default skin (and the
  full set of variations in the skin list) instead of a best-guess folder texture, and weapon /
  item components that had no embedded texture are now textured. The default is the first display
  the database lists (e.g. the plain chicken skin, not an alphabetical guess). Retail (CASC/DB2)
  loading is completely unchanged. *Still to come for legacy clients: character customization and
  equipped items.*
- **Load a legacy (pre-CASC) WoW client — File → Load Legacy MPQ Client…** You can now open an old
  MoPaQ-based install (Wrath of the Lich King 3.3.5, and the same path for TBC/Vanilla) straight from
  the menu: pick the WoW folder **or** its `Data` folder, and the viewer opens the `Data\*.MPQ` (+
  locale) archive chain, auto-detects the locale, fills the file browser, and tells you the client
  era/build, locale and how many archives loaded (with a clear message if none are found). The last
  folder you used is remembered for next time. Retail (CASC) loading is unchanged. *This first pass is
  model viewing only — character customization, equipment and the item/creature databases for legacy
  clients come in later updates.*

### Internal
- **Groundwork for loading older WoW clients (Vanilla/TBC/WotLK).** First, non-user-facing
  milestone of a versioned client architecture: a **client profile** (era/version/build,
  storage type, file-lookup mode and coarse capability flags) is now derived from the loaded
  client, and file opening goes through a small **file-provider interface** that names the
  storage backend. The modern client uses a CASC provider that forwards to the existing loader,
  so behaviour is unchanged; a **placeholder MPQ provider** marks where classic-archive support
  will slot in (not implemented yet). On load, the log now clearly reports the active profile,
  build, storage type and lookup mode. No change to model rendering, character customization or
  equipment.
- **Real legacy MPQ archive support (StormLib).** The placeholder MPQ provider is now a working
  **MoPaQ reader**: StormLib is vendored in `ThirdParty/stormlib` (built UNICODE + static, like
  CascLib) and a new `MpqFileProvider` opens a legacy install's `Data\*.MPQ` (+ locale) archive
  chain in correct **override order** (base archives, then patches, then locale patches — highest
  priority wins) and serves files **by name** (legacy clients have no FileDataID). A new
  `MpqFile` reads through StormLib; `WoWFolder` creates MPQ files on demand by path. A headless
  `-mpq <DataFolder> [locale]` flag loads a legacy client instead of CASC, logging the profile,
  `storage=MPQ`, the full archive list, locale, `lookup=Name`, and per-file open results.
  **Retail CASC loading is untouched.** This milestone is file access only — model rendering of
  old M2/SKIN/BLP, DBC, character customization and equipment are later milestones.

## [0.11.0] — 2026-07-05

Version numbering continues the official WoW Model Viewer line (following 0.10.x) rather
than the fork's earlier 0.x scheme.

### Added
- **FBX export: "Component (raw/Blender)" mode for item components.** A new checkbox in the FBX
  Export Options dialog (and the `-fbxcomponent` headless flag) exports models the way the WoW
  Model Viewer Blender add-on wants them for rigging item components — instead of baking each
  material into one flat texture, it writes a **second UV set (UV2)**, the **raw individual
  textures** for every texture unit, and an expanded material sidecar. On import, the bundled
  Blender add-on then rebuilds each material's node graph automatically: it picks UV1 vs UV2 per
  texture, treats a texture's alpha channel as a **specular/mask (ignored) rather than
  transparency** where appropriate (no more see-through blade edges), gives glow/effect planes
  their own **Emission** material driven by the UV2 scrolling glow masked by the UV1 gradient, and
  turns the model's UV scrolling into an **animated, looping Mapping node** — the whole manual
  workflow, done on import. The default (unchecked) export is unchanged: it still bakes as before.
  *After updating, re-install the Blender add-on (About → Install Blender Add-on) to get the new
  import behavior.*
- **Equipment panel: one-click item removal.** Each equipment slot now has a small **X** button that
  removes just that item (greyed out when the slot is empty), plus a **Clear all equipment** button
  that strips everything at once (the same action as the Character → Clear Equipment / F9 menu, now
  discoverable next to the slots).
- **File → Restart (Ctrl+Shift+R).** Relaunches the viewer in one click instead of quitting and
  reopening by hand. Your saved settings are kept.
- **Modern background-colour picker.** View → Background Color… now opens a Photoshop-style picker —
  a saturation/brightness square, a hue strip, new/current swatches and editable H/S/B, R/G/B and
  #RRGGBB fields — replacing the old native Windows colour dialog.
- **Model Control: the geoset list scales with the panel.** The geoset tree fills the (floating,
  resizable) Model Control window and grows when you drag its edge, instead of being a fixed small
  box you had to scroll.

- **Image Sequence Export (File → Export Image Sequence…).** Renders the animation to a numbered
  PNG / JPG / EXR frame sequence for After Effects, Premiere and DaVinci Resolve. Choose output
  folder, filename prefix, format, resolution (1080p/1440p/2160p presets, viewport, or custom with
  keep-aspect), frame rate (24/25/30/60/native/custom), frame range, number padding and start
  number. PNG and EXR keep a **clean transparent alpha channel** for compositing — the model is
  rendered over black and over white and its true coverage reconstructed, so it stays fully solid
  (no "see-through" models, which the framebuffer's own alpha would give with WoW's mixed blend
  modes) and the matte is correct for every blend mode (EXR is linear float). Numbering is contiguous
  (no skipped frames) so it imports cleanly as an image sequence. The export
  renders one frame per event-loop tick, so the window stays responsive with a live progress bar,
  current-frame readout and a Cancel button, and the viewport's animation/state is restored
  afterwards. Output colour space is sRGB (PNG/JPG); EXR is linear.

- **Blender importer add-on (About > Install Blender Add-on...).** One click installs a
  "File > Import > WoW Model Viewer FBX (.fbx)" entry into every Blender version found on the
  machine (works with Blender 3.0 through 5.0). Importing a WMV-exported FBX through it makes the
  model look like the WMV viewport out of the box: every FBX export now writes a small
  `.wmvmat.json` file next to it describing each material's real render state (opaque,
  alpha-tested, alpha-blended, additive glow, unlit, two-sided), and the add-on rebuilds the
  Blender materials from that — glows become emissive with black-is-transparent blending, cloth
  keeps its recolour, backface culling matches the game — instead of leaving Blender's generic
  FBX guesses in place. No more manually switching blend modes per material after every import.

### Removed
- **Model Bank panel** (View → Show model bank) and its "Show model bank" menu entry — removed.
- **View menu items Skybox, Show Grid, and Show Mask** — removed.
- **Effects menu** (its only item, Apply Enchants) — removed from the menu bar.
- **Model Control: the Position/Rotation fields and the "Replace particle colours" section** —
  removed. Alpha, Scale, the render/geoset toggles and the geoset list stay; creatures' own
  skin-based particle colours are unaffected.
- **File menu: Save Sized Screenshot (Ctrl+S), GIF/Sequence Export, and Export AVI** — removed
  (including the Ctrl+S shortcut). Save Screenshot (F12) and Export Image Sequence remain.

### Fixed
- **Model Control list now populates right after importing a character.** After an Armory import or
  loading a `.chr`, the equipped helm/shoulders/weapon were missing from Model Control's model list
  until you re-equipped an item; the list is now rebuilt as soon as the character is composed.
- **Equipped helmet: hiding it in Model Control now brings the hair/ears back.** Un-checking a
  helmet's Render in Model Control used to hide the helmet mesh but leave the hair/ears/horns it was
  covering hidden; the helm's geoset auto-hide now follows whether the helm is actually drawn.
- **Image Sequence Export: corrected the progress dialog and a garbled label.** The progress popup
  said "Exporting FBX / …to FBX…" for an image-sequence export (now "Exporting Image Sequence"), and
  the "Transparent background" checkbox showed mojibake from a Unicode dash (now clean text).
- **FBX export: blinking/pulsing parts are no longer randomly missing from the export.** Some
  render passes animate their opacity on a repeating cycle — e.g. a character's eye-glow that
  blinks on and off. The exporter decided whether a pass got a material and its geometry by asking
  "is it visible *right now*?", so if the export happened to fire during the split second the
  animation sat at zero, that pass was silently dropped: exporting the exact same character twice
  could produce 18 materials one time and 17 the next. Export visibility is now judged over the
  whole animation cycle (a pass exports if it is ever visible, at its peak opacity), so repeated
  exports of the same model are identical. Passes that are permanently invisible are still skipped.
- **FBX export: multi-texture "glow"/overlay effects are no longer dropped.** Some items (e.g. a
  hood whose mask has a separate glowing eye-slit overlay) combine up to four textures per pass
  using WoW's own material combiner — the viewport already renders this correctly via a GLSL
  shader, but the exporter only ever exported the FIRST texture, silently ignoring the rest, so an
  item's actual glow/overlay colour (e.g. yellow) was missing entirely and the plain, duller base
  texture (e.g. grey) was exported instead. The exporter now bakes the real combined result — the
  exact same formula and textures the viewport uses — into the exported texture for these passes,
  so Blender shows the same effect the viewport does. Ordinary single-texture materials are
  unaffected; passes using an environment/reflection map are also unaffected (unchanged behaviour).
- **FBX export: equipped items no longer revert to their default appearance.** FBX export relaunches
  WMV as a background process, which reloads the character from a snapshot (`.chr`) saved at the
  moment you clicked Export. Reloading an item first resolves its DEFAULT appearance from its item
  ID, then only corrects that for a saved variant when a simple per-item "level" index accounts for
  it — but some equipped items (e.g. Armory-imported items, or any appearance not reachable through
  that level index) have a look that mechanism can't reproduce, so the exact appearance actually
  shown in the viewport was silently discarded and the item's generic default was used instead —
  the exported FBX could show different (wrong) textures than what was equipped. The saved snapshot
  now always wins as the final, authoritative appearance for every item, matching the viewport
  exactly. (Everyday equipment, which has only one appearance, was never affected.)
- **FBX export: the splash screen no longer flashes on screen.** Exporting to FBX runs as a
  background copy of WMV itself so the main window stays responsive — but that background process
  still ran the normal startup sequence, which unconditionally shows the splash screen (centred,
  ~2s) before anything checks whether the run is headless. Only the main window was ever parked
  off-screen; the splash showed itself immediately on construction, so every export briefly
  flashed it on the real screen. The splash is now skipped entirely for a headless/background run.
- **FBX export: recolored armor/weapons no longer lose their tint in Blender.** Some equipped
  items (and some creature skins) share one base model/texture and are recolored via the M2
  "colors" animation track (e.g. purple-tinted cloth over an otherwise gold/bronze texture) — the
  viewport applies this tint every frame, but the exporter only ever wrote the raw, un-recolored
  base texture, so the same item opened flat gold/bronze in Blender. The exporter now bakes that
  same tint directly into a copy of the exported texture (only for passes that actually carry a
  tint, under a distinct filename so untinted textures are unaffected), so Blender shows the exact
  colour the viewport does. The existing self-illuminated/emissive glow (e.g. eyes) is unaffected.
- **FBX export: glowing eyes are no longer pink/blank.** Composited textures with no source file on
  disk (the character's eyes, baked skin, etc.) had no filename, so the exporter synthesised one
  from the model name — but the model name is a full game path
  (`character/bloodelf/female/bloodelffemale_hd`), so the texture path ended up pointing at
  non-existent subfolders and the image silently failed to save, leaving a 0x0 (blank) texture that
  DCCs draw as magenta — the reported "pink/purple eyes". The synthesised name now uses just the
  base name, so the real (gold) eye texture is written and embedded. Self-illuminated passes also
  drive the emissive channel so glowing parts light up.
- **Previewing an item component directly (not through an equipped character) showed no colour at
  all.** Some armor pieces (e.g. a hood/mask with a coloured cloth texture) get their actual texture
  from a database lookup rather than from the model file itself, and the viewport's "Skins"
  selector is what resolves that lookup and lets you pick between an item's different recolours.
  Its database query filtered on the wrong column — comparing a *texture's* file ID against the
  *model's* file ID, which can never match — so it always found zero candidates, silently leaving
  the piece with no texture bound at all (flat grey, no matter which recolour the game actually
  uses). The query now correctly matches on the model's own file ID, so the Skins selector is
  populated again and the correct texture (colour and all) shows immediately.
- **FBX export: glow effects no longer import as opaque black planes in Blender.** WoW draws
  glows (eye-slit beams, floating shoulder wing-blades, weapon shine) as *additive* layers:
  their bright parts add light and their black parts add nothing — invisible in-game. The
  exporter wrote these as ordinary opaque materials, so in Blender the mostly-black glow planes
  rendered as solid black geometry that covered the model behind them (black wings, a blacked-out
  face behind the hood's beam planes). The Blender add-on now renders additive passes as genuine
  additive layers (emission added over a fully transparent surface) — the model behind stays
  visible, black contributes nothing, and the same stacked glow layers the game draws accumulate
  in Blender like they do in the viewport, at the correct hue (no more oversized, over-bright or
  colour-shifted glows). Glow layers the game animates (scrolling streaks / colour modulators)
  are also no longer frozen at one arbitrary animation instant: the bake sweeps the whole
  animation cycle, rendered supersampled so accents on very thin geometry (a beam's gold tip)
  survive, and keeps each pixel's brightest result.
- **Armor glow accents (e.g. a hood's eye-slit beams) are no longer colourless.** Some armor
  pieces put their glowing accents on a second replaceable texture slot — the same slot weapon
  models use for the blade sheen. The accent geometry's UVs point at a dedicated coloured island
  inside the item's own texture, which is how each recolour of the item gets a matching (or
  contrasting) accent colour in the game. The viewer filled that slot with a generic grey
  weapon-sheen texture for every model, so those accents always rendered grey/white no matter the
  item (both equipped on a character and in a direct preview). Armor components now feed their own
  item texture into that slot — the hood's beams glow gold, and every recolour shows its intended
  accent colour. Actual weapons keep the previous blade-sheen behaviour, and this also carries
  into FBX exports automatically (the exported/baked textures use the same texture routing).
- **FBX export: the last frame of every animation no longer snaps to a broken pose.** The final
  keyframe was sampled exactly at the clip's loop point, which returns the *start* pose — so the end
  of each take jumped the whole skeleton back to the start for one frame. The final key now holds the
  true end-of-clip pose and the importing tool handles the loop itself.

### Changed
- **The camera now auto-fits the whole model on load.** The auto-frame used to size only to the
  model's height, so wide or long models (mounts, dragons, spread poses, a weapon lying flat) spilled
  off the sides. It now frames the model's full 3-D bounds, so the whole thing sits in view; Reset
  Camera (and Numpad 5) do the same.
- **M2 material rendering: explicit shader-mapping layer + material fixes.** Each render batch is now
  classified from its shader id, blend mode, texture count and flags into an explicit render variant
  (multi-texture materials keep the existing GLSL combiner; single-texture materials use the
  fixed-function path). Two blend fixes apply by default: no-alpha **additive glows** use ONE/ONE
  (were being squared/darkened), and **alpha-key cutouts** key at ~0.5 to match the game (were
  over-clipping thin hair/foliage edges). Opt-in env flags for A/B testing: `WMV_SHADERDEBUG` logs
  each batch's classification, `WMV_M2_SINGLECOMBINER` routes non-trivial single-texture materials
  through the combiner, and `WMV_M2_STAGE2` folds the combiner's env-reflection/glow lobe back in
  (metal/gem/eye sheen). Existing multi-texture (cosmic/void cape) rendering is unchanged.
- **Metal weapons now catch the light.** Reflective metal materials (swords, axes, maces and other
  gear that uses an environment-reflection texture) get two default-on touches: a **fresnel sheen**
  that strengthens their reflection toward grazing angles, and a **specular glint** — a bright
  highlight that slides across the surface as you orbit the model, the way polished metal does
  in-game. Both are applied only to genuinely reflective, lit metal (opaque/alpha-key); cloth,
  skin, self-illuminated, glow/energy and cosmic-effect materials are left exactly as they were,
  and FBX exports are unchanged. Intensity is tunable for anyone who wants more or less: `WMV_ENV_BOOST`
  (reflection sheen, default 0.45), `WMV_METAL_SPEC` (glint strength, default 0.26; 0 disables it) and
  `WMV_METAL_TIGHT` (glint size, default 55). With everything at 0 the render is byte-for-byte the old image.
- **Exporting to FBX no longer freezes the program.** A model export used to lock up the whole
  window until it finished — no way to tell how far along it was, and no way to stop it. FBX export
  now runs as a separate background job: the main window stays fully usable while it works, a small
  progress window shows the current stage (skeleton, mesh, materials, skinning, each animation, and
  writing the file) with a progress bar, and a **Cancel** button stops it cleanly. When it finishes
  you get a "completed" message; if something goes wrong you get the reason instead of a silent
  failure. Because the export runs in its own process, even a crash or a hang inside the export can
  no longer take the viewer down with it.
- Every export now writes a detailed log next to the saved file (`<name>.fbx.export.log`) for
  troubleshooting, and starting a second export of the same model to the same file while one is
  already running is politely refused instead of clobbering the first.

### Fixed
- **The Character menu's Show Ears / Show Hair / Show Facial Hair / Show Feet toggles work now.**
  They previously did nothing: toggling a menu item flipped an internal flag but never re-applied the
  character's geosets, so the model on screen didn't change. Now toggling any of them refreshes the
  model immediately. Two of them needed more than that: **Show Hair** was wired to a flag that nothing
  ever read, so it's now actually connected to the hairstyle geoset (turning it off gives a clean bald
  head, not a hole); and **Show Feet** was being reset to the race default on every refresh, which
  overwrote your choice — the default is now applied once when the character loads, so your toggle
  sticks. Show Underwear, eye-glow and the head-item auto-hide option (which shared the same broken
  path) also respond immediately now.
- **The main window can no longer get "lost" off-screen.** If a saved window position would place
  the window where you can't reach it — for example a coordinate left over from a second monitor
  that's since been unplugged — the window now re-centers itself on a connected display at startup
  instead of opening somewhere invisible (where it couldn't be moved or maximized). Background
  export jobs also no longer write their own window position into your settings.

## [0.3.2] — 2026-06-23

### Fixed
- **Characters show their full customization list again (Dracthyr visage and more).** Some models
  list a few customization options tagged one way and the rest tagged another; the viewer was only
  reading the tagged ones whenever *any* existed, and silently dropping the rest. On the worst-hit
  models that meant most of the panel went missing — the Dracthyr **visage female** showed only Skin
  Color and Eyesight and lost Face, Hair, Horns, Eye Color, Scales and Eyebrows; dragonriding drakes
  lost their entire armour wardrobe; and allied races (Vulpera, Mechagnome, Mag'har, Dark Iron, Kul
  Tiran and others) lost Eyesight and Eye Style. Every option is now loaded, so the full list shows
  for each model.
- **Underclothes now load fully clothed.** When one option controls two others — "Underclothes
  Color" drives both the top and the bottom texture — only one of the two was being applied on load,
  so a freshly loaded model could come up with the briefs textured but the bra blank. Both dependent
  textures are now resolved, so underclothes appear complete on load.
- **Eyes show their colour and glow at the same time.** The iris colour and the "Eyesight" glow are
  separate textures that target the same eye slot, so whichever applied last replaced the other —
  most visibly on Mechagnome, whose eye showed only the blue glow and not the coloured iris once
  Eyesight was set. The eye layers are now combined into one image, so the iris and the glow render
  together.
- **Mechagnome cybernetic parts no longer shimmer while animating.** The Modification, Arm and Leg
  upgrades are separate part-models merged onto the character, and they were picking up the
  character body's *animation* tracks by mistake — so the body's looping eye/idle animation scrolled
  and pulsed across the metal, making its texture crawl and shimmer non-stop (and occasionally flicker
  out) during playback. The merged parts no longer inherit the body's animation, so the metal holds
  still. (The separate, already-correct environment reflection is unaffected.)

## [0.3.1] — 2026-06-22

### Added
- **Brand-new and still-encrypted models now load.** Recently-added content -- e.g. bosses from a
  just-shipped patch or the current PTR -- keeps its database records and model files encrypted, in
  extra data sections the viewer used to skip entirely (so those NPCs/items came up missing). WMV now
  reads *all* sections of a data table, and keeps its decryption keys current automatically (refreshed
  weekly from the community key list, https://github.com/wowdev/TACTKeys), so this content appears as
  soon as its key is public. A model whose key hasn't been published yet simply doesn't show (with a
  notice, not a crash) and starts working on its own once the key lands.
- **The window title shows the loaded model.** The model's path now appears in the title bar (it was
  already in the status bar at the bottom, which is easy to miss), so it's obvious what you're viewing.

### Fixed
- **Mechagnome cybernetic parts render correctly.** The Modification, Arm Upgrade and Leg Upgrade are
  separate part-models merged onto the character, and three separate bugs left them looking wrong: the
  metal/paint (from the "Paint" customization) was discarded and the parts wore bare gnome skin; the
  upgraded limbs drew on top of — and flickered with — the body's default arm/leg; and the metal's
  reflective sheen sampled the wrong texture and smeared across the armor as the camera moved. All
  three are fixed — the paint binds to each part, the replaced body limb is hidden, and the armor's
  environment reflection now uses the correct texture — so both male and female render cleanly. The
  body-limb hide is also guarded so it never removes legitimate geometry on races where a base part
  co-exists with a merged one (e.g. Dracthyr drake body armor, Earthen hair).
- **Eye colours that change with Eye Style now apply on load.** Many races (Vulpera and around fifty
  others) have eye colours whose iris texture is selected by the separate "Eye Style" option. Those
  colours loaded with no eye texture (a blank/grey eye) until you manually re-picked the colour,
  because the viewer never recorded that Eye Color depends on Eye Style. It now discovers that
  dependency across all of an option's choices, so the eye is correct on first load.
- **Races whose ears aren't the default geoset no longer load earless.** The viewer auto-shows the
  "variant 1" geoset of each body group, but some race/sex combos use a different variant for their
  built-in ears (e.g. Gnome females) and so loaded with no ears. When ears should be visible but none
  are, the viewer now shows the model's actual ear geoset — without affecting races that customise the
  ear group (Mechagnome, dragonriding drakes, etc.).
- **Customization dropdowns no longer list the game's "Transmog" placeholder.** Many appearance
  options (Skin/Hair/Eye Color, Face, Fur Color and more) carried an extra "Transmog" entry that
  isn't a real appearance — in game it just means "this part follows the equipped transmog," which
  has no meaning in a model viewer. It's now hidden across every race and option, so the lists show
  only selectable looks (e.g. Blood Elf Skin Color, Hair Color and Eye Color each lose their dead
  Transmog slot).
- **Models with many animations no longer hang the viewer, and characters load in a fraction of the
  time.** Opening an effect-heavy creature or a customizable character (e.g. Dracthyr) used to freeze
  the window for tens of seconds — up to ~90s on models with hundreds of animations — while the
  animation list filled in, the character was re-composited dozens of times, and the same texture
  layers were decoded over and over. Now the animation list fills in the background once the model is
  on screen, the model is refreshed once per load instead of ~30 times, and each texture layer is
  decoded once and reused. A Dracthyr character that took ~12 seconds now loads in about 2.
- **PTR / Beta installs now load their own game data instead of retail.** When you pointed the viewer
  at a Public Test Realm or Beta game folder, it was silently loading the *retail* data instead — so
  anything retail and the test realm share looked fine, but brand-new test-only content (the latest
  datamined creatures, updated models, new customizations) came up missing or stale. The product you
  pick is now passed to the storage layer correctly, so the exact game version you selected is the one
  that loads. (Cause: the path/product separator passed to the storage reader was a `:` where the
  reader expected `*`, so the product code was dropped and it fell back to the first listed build.)
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
- **Importing an NPC from a newer patch no longer crashes the viewer.** Pasting a Wowhead link for an
  NPC whose model isn't in the game data you have loaded -- e.g. a PTR / next-patch creature whose
  display id doesn't exist in your data yet -- made WMV try to use a model that had failed to load and
  crash to desktop. It now detects the missing model, keeps the current view, and shows a short "NPC
  unavailable" notice explaining the NPC is most likely from a newer build than your loaded data.

- **Dropdowns no longer collapse to a thin sliver.** Several combo boxes/choices (the animation and
  skin selectors, the File List filter, the light selector) were created at a fixed small height;
  under the 64-bit/wxWidgets 3.2 move (and with display scaling) they got clamped below the native
  control height once the layout settled. They now use their natural height.

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
  the reference implementation uses: group files are opened via the root's `GFID` chunk (FileDataIDs) with the
  old `_NNN.wmo` naming as fallback; material textures are taken as FileDataIDs when no `MOTX`
  name block is present (otherwise the classic name-offset path); and doodad models are read
  from `MODI` FileDataIDs (otherwise `MODN` names). Classic WMOs still load exactly as before.
  Selecting a WMO *group* file (`<name>_NNN.wmo`, which also appears in the file tree) no
  longer crashes — only root WMOs carry the header that drives loading, so group files are
  now ignored with a log message instead of dereferencing uninitialised counts/arrays.
  Render batches now resolve their material with the modern >256-material rule (when the batch
  flag `0x2` is set the 16-bit index in the batch's second bounding box is used instead of the
  8-bit field), matching the reference implementation — previously the wrong material/texture was applied.
  The WMO file list now shows only **root** WMOs: group and LOD files (`<name>_000.wmo`,
  `..._000_lod1.wmo`, etc.) are hidden, since they aren't standalone objects (the root
  references them). Uses the reference implementation's exact filter, so the list matches its count.
  The camera now frames a WMO to fit the view when it loads (WMOs span hundreds-thousands of
  units, so they used to load filling/overflowing the screen); the max zoom-out distance was
  also raised from 150 so large WMOs can actually be framed.
  WMO orientation is fixed: the geometry was converted into an old Y-up coordinate space (a
  leftover `x,z,-y` swizzle) while the camera and M2 models are Z-up, so WMOs loaded tipped 90
  degrees. They now render directly (Z-up), upright like in the reference implementation.

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
  model size (hold **Shift** for finer steps), matching the reference implementation. Right-drag panning is now
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
  the reference implementation, which has no such structure). Also hardened every group chunk read to copy
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
