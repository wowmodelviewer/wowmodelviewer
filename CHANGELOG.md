# Changelog

All notable changes to **WoW Model Viewer: Midnight** are recorded here.
Format loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

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
  client** -- CASC or legacy MPQ, through the same file providers the OpenGL viewport uses -- with
  byte length + SHA-1 so the player can verify what it received. Missing files, unsupported
  lookups (FileDataID on an MPQ client) and a client that is still loading come back as clear
  errors. Nothing is exported or written to disk; this is the runtime channel the Unity renderer
  will render from directly (no M2 parsing/rendering in Unity yet -- next step). Unity remains
  optional at build and run time. New headless self-test: `-mo <model> -unityipctest` drives the
  whole exchange against the installed player (the Unity-free TestStub speaks the protocol too).
- **Embedded Unity renderer viewport — View → Unity Renderer (first step of the new renderer).**
  The viewer can now host a separately built Unity standalone player inside a dockable pane.
  This is the foundation of WMV's **new rendering pipeline**: long term the Unity viewport
  becomes the primary viewport and future rendering features (characters, equipment, maps, fog,
  OBS scenes, stream features) target it first, while the classic OpenGL viewport is kept as the
  legacy/fallback renderer during the migration. Unity renders directly from WoW data -- it
  requests raw assets/metadata from the app over IPC (no OBJ/FBX/GLB export step); the app
  remains responsible for the UI, the active client/profile, CASC/MPQ access,
  databases/metadata and runtime commands. The player is launched embedded (parent-window mode),
  resizes with its pane and is shut down with the app. **For now it is optional:** nothing in the
  normal build depends on Unity, and if no player build is found (default location
  `tools\unity-renderer\UnityRenderer.exe` next to the exe, or the `Tools/UnityRendererPath`
  setting) a clear message is shown and the app carries on with the OpenGL viewport. The player
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
