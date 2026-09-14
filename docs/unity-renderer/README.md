# Embedded Unity Renderer

## Direction (read this first)

The embedded Unity viewport is **WMV's only viewport**. The OpenGL viewport (`ModelCanvas`) is
**archived**: it cannot be shown or used, and no menu item, setting, saved layout, command-line
switch or failure path brings it back. The canvas object still exists as a hidden internal
service; what it still does is listed in "Archived OpenGL viewport" below.

Concretely:

- **Unity only.** Every rendering feature -- characters, equipment, maps, fog, capture and
  stream features -- is built in the Unity viewport. Content it cannot draw yet is not removed
  from the application: it still loads, and the viewport shows a notice saying what is loaded
  and that it cannot be shown yet (see "What the viewport shows").
- **Unity renders directly from WoW data.** There is **no OBJ / FBX / GLB export workflow**
  between WMV and Unity. Unity requests the raw WoW assets (M2, skins, BLP textures, …) and
  metadata it needs from WMV over IPC and builds the scene from them. (WMV's export plugins
  remain a separate, user-facing feature; they are not part of the render path.)
- **Responsibility split** (detailed below): **WMV provides the application UI, the active
  client/profile, CASC/MPQ access, DB/metadata and the runtime commands; Unity provides the
  modern rendering pipeline and asks WMV for assets/metadata.**
- **The player is needed at run time, not at build time.** Nothing in the normal WMV build
  depends on Unity (no Unity SDK, no binaries in the repo; the IPC server only adds the standard
  Windows socket library). Without a player build there is no picture: the application still
  runs, and the viewport shows a notice naming the path it looked at, with a button that tries
  again. Nothing ships the player yet -- neither the installer nor the CMake install rules
  include it -- so it is built locally (see "Locating the player").

## Lighting

The viewport lights models with a **fixed preview rig in the renderer's own shader**, not with the
engine's lighting. That is a deliberate choice twice over.

**The shader is chosen on purpose, not by accident.** `WmvOpaque` used to be the last rung of a
search that preferred the pipeline's Lit shader, and it won only because URP/Lit gets stripped out
of a player build. Two things were riding on that accident: the M2 combiners (a Lit shader cannot
run them, so chicken2's pixel shader 12 and every environment sheen worked only while Lit was
absent), and the entire look of the application. It is now the first choice, so both are the same
everywhere. `-wmvLitShader` asks for the pipeline's Lit shader when you want to compare — with the
combiners disabled, which is the honest comparison.

**Why not physically-based shading.** WoW's textures are hand-painted with their light and shade
already in them. PBR relights an image that has already been lit: mid-tones the artist painted go
dark, flat surfaces pick up specular they were never meant to have, and the hand-painted character
of the art is exactly what gets lost. A model viewer wants the texture legible from a fixed angle,
which is a different job from simulating a room.

**The key is the world's vertical.** `KEY_DIR` and `FILL_DIR` are view-space directions, but
`WmvShadowRig` blends them toward world-up by `WorldAnchor` every frame and publishes the result as
the one world vector that feeds the shading, the shadow map and the contact march. `WorldAnchor`
ships at 1.0, so the published direction is straight down from the sky whatever the camera does,
and the view-space tilt in `KEY_DIR` only decides the shader's fallback (below). An anchored key
is what the reference viewers measurably do:
frame analysis of a reference viewer's preview footage shows that orbiting a model there never flips
its lit side (opposite profiles at 3:40 and 4:01 shade identically) and that a camera looking up
from below finds the belly still dark (4:11—4:17) — which falsifies a fully camera-relative key,
the previous behaviour here, since that swings under with the camera and lights the underside.
Verified on ratmount2 from pitch −60 with the shipped constants: masked mean 0.189 against 0.326
from the normal angle, the belly dark as it should be; `-wmvLightPitch=N` exists because pitch is
the one axis that separates the two schemes (a vertical light is yaw-invariant under both, and the
alpaca measures 0.219—0.239 across four yaws). The shader keeps a pure view-space fallback for a
player where the rig never ran.

A lesson from the first version of this rig, before the anchor: it put the key at 53 degrees of
elevation in world space with a high floor, and almost every surface on a standing humanoid is
vertical, so they all caught it at the same shallow angle and the figure came out brighter but
completely flat, with a lit-to-shadow ratio of **1.06x** where even the old rig managed 1.55x.
The shipped key is fully vertical, which is the same geometry, and it reads as modelled anyway
because the range now comes from the floor-to-key ratio and the cast shadows rather than from the
key's angle: measured light-only ranges are 1.7—2.9x on the nine test models.

**The rig.** Every number is a `#define` at the top of `Assets/Resources/WmvOpaque.shader`, in one
block, so a tuning pass edits constants and nothing else:

| term | value | what it does |
|---|---|---|
| `KEY_DIR` | `(0.081, 0.858, 0.507)` | view space, 9 degrees round and 59 up: the fallback direction only, since the anchor is 1.0 |
| `FILL_DIR` | `(0.059, 0.998, 0.032)` | very nearly straight up: a sky fill on the shadow side |
| `KEY_FLOOR` | `0.265` | floor: the darkest any surface gets |
| `KEY_GAIN` | `1.644 * ndl` | key |
| `FILL_GAIN` | `1.5 * wrap * (1-key)` | sky-hemisphere fill (see below) |
| `RIM_GAIN` | `0.0` | off |
| `SPEC_GAIN` / `SPEC_TIGHT` | `3.0` / `49.2` | a tight preview highlight |
| `KNEE` | `0.298` | where the top end starts rolling off |
| `CEIL` | `1.0` | what the roll-off approaches |
| `SHADOW_STRENGTH` / `SHADOW_SOFT` | `1.0` / `4.0` | the map's key shadow, full strength, 4-texel blur |
| `CONTACT_STRENGTH` / `CONTACT_RANGE` | `1.0` / `0.25` | near-field occlusion, full strength, a quarter-radius reach |
| `WorldAnchor` (in `WmvShadowRig`) | `1.0` | the key is world-vertical |

**These values were settled by eye, on a live slider panel that has since been removed, and they
choose form over parity.** A footage-calibrated set preceded them (floor 0.85, key 0.46, shadows
0.20 and 0.30): measured over a background-free mask it matched a reference viewer's preview of
the same alpaca to three decimals on the masked mean, and on screen it read as lighting that had
not been switched on. The shipped set is the opposite bargain — a low floor under a strong key,
shadows at full strength — and against the same footage it is still close: alpaca mean 0.234
against 0.257, and nearer on the tails than the calibrated set ever got (p05 0.080 against 0.089,
p95 0.482 against 0.507; the held-out white rat 0.376 against 0.402, p05 0.114 against 0.118). Two
pieces of the recipe are measured rather than chosen: the anchored key above, and the FILL AS A
WRAP TERM — `0.5 + 0.5 * dot(n, up)`, a sky hemisphere rather than a second sun, because a
one-sided fill is zero on vertical surfaces and bellies, which under a vertical key are exactly
the surfaces nothing else lights.

**Additive batches are emissive and skip the rig** (shipped rig only; the legacy rig stays a
record of what drew before). An additive pass is light the surface emits, and multiplying emitted
light by received-light maths is wrong in principle: with the floor at 0.265, a lantern facing
away from the key would be drawn at about a quarter of its authored intensity. Note the gate:
`_Emissive` must
be a shader PROPERTY, not just a uniform, because the builder's SetFloat is guarded by
`Material.HasProperty` -- a first version declared only the uniform and the bypass never ran.

What the numbers encode: a low ambient floor (0.265) under a strong vertical key (1.644), a
lit-to-unlit ratio of 7.2x before the roll-off, with the wrap fill (1.5) keeping vertical surfaces
and undersides readable. `KNEE` at 0.298 puts most of the tonal range inside the roll-off, which
shapes it toward `CEIL` at exactly 1.0 without arriving, so whites read as white; measured
light-only shading ranges on the nine test models are 1.7—2.9x (the footage-calibrated set gave
1.2—1.3x), full renders 3.5—5.0x, and clipping is 0.000 % on every opaque model (the only
non-zero figures are additive pile-ups: valkier 0.14 %, the alpaca's lantern at most 0.2 %). The
cast shadows are at full strength (map 1.0, contact 1.0): an occluded key contributes nothing, and
the half-floor rule below is what keeps a shadowed strap-line readable rather than black.

`RIM_GAIN` is zero, so the rim term contributes nothing; it stays in the shader as the one knob
not yet needed. The highlight is on (gain 3.0, tightness 49), a small tight catch-light that the
map shadow removes along with the key.

### Cast shadows

The model occludes its own key light: a rein across the mount's body, a horn across a face. The
rig's dot-product terms cannot produce that — they know which way a surface faces, not what stands
between it and the light — so `WmvShadowRig` renders a depth map from the key's point of view every
frame (one orthographic camera, fitted to the model bounds, 4096 px) and `WmvShadowFactor` in the
shader compares each fragment against it with a 3x3 PCF kernel. `SHADOW_STRENGTH` says how much of
the key an occluder removes and `SHADOW_SOFT` blurs the edge; both are in the constants block.

Design points worth knowing:

- **The map attenuates the key only; the contact march also takes the fill and half the
  floor.** Each estimator removes what it actually knows about (see the split below), and the
  floor can never drop below half, so a shadowed strap-line stays readable instead of black.
- **The shadow follows the key, and the key is the world's vertical.** The rig blends the
  camera-relative key toward world-up (`WorldAnchor`, shipped at 1.0, so fully) and re-renders
  the map from that direction each frame; orbiting the model does not move the lit side, and
  looking up from below does not carry the light under it.
- **The depth pass is the ordinary render.** The shadow camera draws the model with its normal
  materials and keeps the depth buffer: alpha-keyed batches clip in the shadow pass exactly as on
  screen (hair casts shaped shadows, not slabs), and blended/additive batches write no depth and
  cast nothing, which is right for glows.
- **`WmvShadowRig.KeyDirView` must match the shader's `KEY_DIR`** — the shader's fallback
  shades with its copy, the rig places the camera with its own. There is no tool keeping them in
  step any more; change one, change the other (the `FillDirView`/`FILL_DIR` pair likewise).
- Everything is gated by `_WmvShadowValid`, which unset reads 0: a player where the rig never ran
  renders exactly as before, and the legacy rig (`-wmvRig=1`) never samples the map at all.

**Contact shadows** close the gap the map cannot: its bias is a blind zone of a few millimetres
(one texel of the map's window), so a map shadow always stops just short of the line where two
surfaces meet, and a hood whose shadow starts a centimetre below the brim reads as pasted on
rather than worn. `WmvShadowRig` therefore renders a second depth buffer each frame — the scene
from the VIEWER's pose, near/far pinched around the model — and `WmvContactFactor` marches a short
ray from each fragment toward the key light through it: any on-screen surface standing in front
of the ray within touching distance is a contact occluder, found with no bias and no texel
footprint. The two estimators answer the same question from opposite sides — the map sees the
whole model but not the near field, the march sees only the near field and only what is on
screen — and the darker verdict wins (`min`, not a product, so a rim both can see is not
double-darkened). `CONTACT_STRENGTH` and `CONTACT_RANGE` (a fraction of the model's radius) are
in the constants block.

**What each shadow removes follows what each estimator knows.** The map answers a directional
question ("does the key reach this point"), so it attenuates the key and the highlight and nothing
else. The march answers a near-field one ("is something within touching distance overhead"), which
is what sky-and-ambient occlusion is — so it also takes the sky fill and half the ambient floor,
but only within its short range. Both other assignments were tried and failed measurably: key-only
went invisible when the light was anchored near-vertical (a face's ndl against an overhead key is
~0, and blocking a light a surface never received changes nothing), and letting the map take the
fill dropped a cloaked boss model's mean by a third, because an overhead light puts the map's
shadow across everything below the shoulders. The floor can never fall below half — a shadowed
face stays readable, never black.

**The march's verdict is fractional, not binary.** A first version returned fully-occluded on
the first hit, and looked exactly as harsh and as pixelated as a binary function dithered by
per-pixel jitter must: at every shadow boundary, neighbouring pixels flipped between fully dark
and fully lit. Each hit is now weighted — by a smooth window on the depth test and by how far
along the ray the occluder sits, so a touching edge darkens fully while one at the end of the
range barely registers — and the strongest hit wins, so the value varies continuously as an
occluder recedes. Measured on the hooded test model at contact strength 0.3, the fraction of
shadowed pixels sitting on a hard edge halved (21 % to 10 %) and the mean local gradient dropped
22 %. At the shipped strength of 1.0 the same profile is scaled up three-fold, and half the
touched pixels sit on a step the metric counts as hard — so `CONTACT_STRENGTH` is the knob to
lower if contact shadows read harsh, not the march.

The parameter that makes or breaks the march is the assumed occluder THICKNESS. A depth buffer
records only front surfaces; without a bound on their assumed depth, any geometry anywhere in
front of the ray counts as blocking, and the first cut of this (thickness 0.20 R) draped a faint
grey wash over every large surface. Thickness is 0.08 R and stays fixed while the march reach is a
constant (`CONTACT_RANGE`, shipped at 0.25 R, so the ray now travels further than the window is
thick): an occluder matters only if the ray passes within touching distance behind it, however far
along the ray that happens. Verified per contributor on the hooded Saurfang (`-wmvLightYaw=210`
turns the check camera to his face): the map-only pass, the combined pass and the contact-only
difference are dumped separately. With the shipped constants the map touches 56 % of the model,
the two together 84 %, and the contact march alone adds 30 % — the face under the hood rim, the
mantle under the collar, the neck under the jaw, the belt lines.

`-wmvLightCheck` reports what the shadows did (coverage, mean darkening, centroid), and
`-wmvLightDump` writes the check's frames — shadowed, unshadowed, and the amplified difference —
as PNGs next to the player. WHERE a shadow falls is a spatial question, and the difference image
is the only reliable way to answer it: the first build shipped with a vertically mirrored map
(the D3D render-into-texture flip counted twice) and every statistic looked plausible — it took
eyes on the viewport to catch the model's own silhouette stamped across itself. The criterion
that verified the fix: with a high key, darkening belongs on UNDER-surfaces (the flank below a
saddle rim, a neck below the head, feet below the body) and the top of the back must be clean.

### How the values were settled

During this work the player carried a live slider panel over the viewport (one slider per constant
above, plus the light directions), which is how the numbers were judged on real models rather than
guessed and rebuilt. Once they were settled they were written into the `#define` block and the rig,
and the panel, its `-wmvLightPanel` flag, the override uniforms it drove and the bake script that
kept the copies in step were all removed: the shipped shader path never depended on any of it
having run, and a normal run renders from the constants alone. Tuning again means editing the
block at the top of `WmvOpaque.shader` (and the matching direction pair in `WmvShadowRig`) and
rebuilding; the measurement flags below are what says whether a change did what it looked like.

### Measuring it

`-wmvLightCheck` renders the model offscreen and reports, for every rig, mask coverage, mean, p05,
p50, p95, max, the clipped fraction, a saturation proxy and model/background contrast. Nothing is
written to disk and the viewport is not disturbed. `-wmvRig=1` draws the viewport itself with the
legacy rig for a visual A/B.

**It also measures the rig on its own.** Every figure above is taken on a real model, where paint
and light arrive as one number — and a rig that models form and a rig that lights everything flat
can produce the same mean, the same percentiles and the same contrast, because most of the spread
being measured is the texture. So each rig is rendered a second time with `_WmvFlatAlbedo` set,
which replaces every surface colour with a flat mid grey and leaves only the light. The ratio of
the bright end to the dark end of that render is the number that says whether the thing has shadows
in it: around 1.2 is an evenly-lit figure, 1.6 upwards reads as modelled form. The albedo has to be
a mid grey and not white — white is already at the top of the range before the light touches it, so
every rig with any gain saturates and reports no range at all.

This is the check that caught a rig which was better on *every other measure* — brighter at every
percentile, more contrast against the background, saturation preserved — and was flat.

Getting a number that means something took more care than the tuning did, so the guarantees are
worth spelling out:

- **The pixel set is geometric, not photometric.** The frame is drawn twice, once cleared to black
  and once to white, and a pixel counts as model where the two agree exactly — only opaque,
  depth-writing geometry can be independent of what was cleared behind it. A pixel that agrees only
  because it saturated over both clears is dropped, since an additive pile-up agrees for a reason
  that has nothing to do with coverage.
- **The mask is built under the legacy rig**, which is finished and will not be tuned again, so the
  pixel set is pinned across every candidate and every build.
- **The measurement has its own camera**, framed from the bounds alone, so viewport orbit state
  cannot leak in. Run it with `-wmvNoAnim` to pin the pose.
- **Every rig is measured in one process**, through that one camera, over that one mask.
- **Saturation is computed in linear light.** `(max-min)/max` is invariant under a scale factor only
  in the space the scaling happens in; computed on sRGB bytes it falls whenever the picture gets
  brighter, so a pure exposure change looks like a loss of colour.
- **Pin the skin.** WMV picks a random skin per load unless `Session/RandomLooks` is off in
  `userSettings/Config.ini`. The light check prints the texture FileDataIDs it measured so a
  mismatched pair is visible rather than silent.

**Why all of that.** An earlier pass concluded from measurements that a rig carrying strictly more
light at every angle had made two models *darker*. Both halves of that were measurement error.
chicken2 carries four skins spanning **2.4x** in brightness (mean 0.209 to 0.493) and a
before/after taken in two separate builds is two separate processes, so it was two different
chickens — a difference an order of magnitude larger than anything the rig does. Underneath that,
the old brightness-threshold mask defined its sample in terms of the quantity being measured: a
brighter model pushes its own dark pixels over the line, they join the sample at the bottom of it,
and the reported average falls. Measured on the real models, that mask understated the legacy-to-
shipped gain by 11 % to **98 %**, while its sample size moved by up to 63 %. With the skin pinned
and the mask geometric, three consecutive runs of the same model now agree to every printed digit.

## What the viewport shows

**The Unity viewport is always the centre of the window.** Its host panel is docked as the centre
pane when the main window is built, before any player exists, and nothing can take that place:
there is no toggle, File > Reset Layout docks it in the centre again, and a saved layout is
corrected to it when it is loaded. Layouts saved before the archive (layout version below 3, which
could name a "canvas" pane) are discarded, and an old `Tools/UnityPrimaryViewport` value left in
`Config.ini` is ignored.

**It is started when the app starts, not when a model needs it.** The player is a game engine: it
takes about a second to come up (measured at ~1.1 s to the point where it reports ready). Started on
first use, the user picked a creature and then watched that happen, with the model appearing
afterwards — so the wait was attributed to the model, which had nothing to do with it. Starting it
during app launch spends the same second while the user is still looking at an empty application,
and the first creature goes straight into a player that is already connected. With nothing loaded
the viewport shows the empty viewer's prompt (below). A drawable model loaded before the player has
reported in gets no "starting" caption: the area stays the player's own background colour until the
model is built, so the handover is not a visible flash and a caption is never on screen for exactly
as long as it takes to read.

**Nothing is loaded until you ask.** The application opens as an empty fullscreen viewer: no
client is read and no dialog is put in front of you before you have seen the program. Choosing a
client is File > "Load World of Warcraft", which opens the Client Choice dialog — and that is now
the only thing in the application that loads one.

**The application window comes up before anything else.** The client picker used to be the first
thing on screen: the frame existed but was small and unremarkable, a modal dialog sat on top of it,
and the viewer only took the screen after the user had answered. Now the window goes fullscreen and
the renderer starts warming first, and the picker is not shown at all when there is nothing to ask
— it seeds itself from the saved folder and detects the client in its constructor, so if that
worked, loading it is exactly what pressing Load would have done. It still appears for a real
question (first run, a moved install, a folder with no client in it), now centred over a running
application, and File > Client Choice is unchanged.

**Startup is viewer-first.** An interactive launch goes straight to borderless fullscreen on the
viewport. The Browse, Model and Animation panels come up as they were left (all three on a first
run): the empty viewport's prompt already says what to do next, so hiding the panels that do it is
not needed to make an empty application look tidy. There is no logo and no placeholder object. F11
or Esc leaves fullscreen, and the menu bar survives it deliberately — without the caption there
would otherwise be no visible way out, or to View > "Restart Unity Renderer".

**No test objects in a normal run.** The spinning cube that used to fill the viewport before a
model was chosen is off unless `-wmvPlaceholder` is passed. It answered "is the embedded player
alive at all?", which is no longer an open question; an empty viewer should look empty.

**Build the player with the splash screen OFF.** `PlayerSettings.SplashScreen.show = false` (and
`showUnityLogo = false`). A splash makes sense for a game; inside another application's window it is
a game engine announcing itself in the middle of a model viewer. Unity 6 makes this optional for
every licence tier, so there is no reason to keep it. A player built with it on still works — the
user just sees the logo during app startup instead of during model selection.

**The model still lives in the host.** The archived OpenGL canvas is a hidden object now, not a
viewport, but it still loads the model, still owns the animation clock, and is still what every
`Send*ToUnity` call reads from -- the Unity viewport mirrors it. See "Archived OpenGL viewport".

**What the Unity viewport cannot show yet gets a notice, not another viewport.** Content the player
cannot draw stays loaded -- the panels, the Info tab and the exporters keep working on it -- and the
host panel paints a notice over the viewport area instead: a title, a line saying what is loaded and
why it cannot be shown, and a button where there is something to do. Behind a notice the player's
window is only hidden, never closed, so the next drawable model is on screen as soon as it is built.

The decision is `ModelViewer::unityViewportNotice` (the content cases come from
`unityCanDrawCurrentModel`, which also gates the geoset pushes), applied by
`UpdateUnityViewportState` on every path that changes what is loaded -- model, NPC, item and
character loads and their failures, Browse selections, clearing -- on mount and dismount (the
canvas tick notices those), when the player announces itself or reports a character it could not
build, and when a stopped player is noticed. The first case that applies wins:

| what is loaded, or what is wrong | notice | button |
|---|---|---|
| a build without the Unity player (not Windows) | "Unity viewport unavailable" | -- |
| the GL context never initialised, so no texture can be decoded for the player | "Textures cannot be decoded" | -- |
| no player build at the configured path, an IPC listener that could not open, a player that would not start, exited unexpectedly, lost its connection or never announced itself | "The Unity renderer is not running", with the reason | Restart Unity renderer |
| an image picked in Browse (BLP) | "Image selected" | -- |
| a WMO | "World model loaded" | -- |
| a map tile (ADT) | "Map tile loaded" | -- |
| a model with no game file behind it | "Model cannot be shown" | -- |
| a character riding a mount (the loaded model is then the mount, and the player has no mount rig) | "Mounted character" | -- |
| a model with no FileDataID (a legacy MPQ client; the player addresses every asset by one) | "Legacy client model" | -- |
| a character the player reported it could not build, until the next load or a player restart | "Character could not be built", with the player's reason | -- |
| a character, with a connected player older than protocol 3 | "Unity renderer out of date" | -- |
| nothing loaded | "No model loaded" | Browse models (Load World of Warcraft... while no client is loaded) |

A player that has not connected yet is assumed to be the current build; one that then announces an
older protocol gets the out-of-date notice when it does.

**A stopped player is noticed without a load.** A player that crashes, is closed from outside or
drops its connection (before or after announcing itself), or that is still running but has not
announced itself 30 seconds after launch, is caught on the status bar's existing two-second timer
(`UnityRendererHost::checkPlayerHealth`); a player closed on purpose -- application shutdown, a
restart -- is not reported. A player that was only slow and announces itself after all clears its
"did not respond" notice. When WMV cannot open the IPC listener the player is not started at all:
without a port it could never be told what to draw. None of this opens a dialog: the reason is in the notice and in the log.
View > "Restart Unity Renderer" does what the notice's button does: it closes the player if one is
still running, launches it again and decides the notice again, and the new player is sent the
current model when it announces itself.

**Characters.** A playable character stays in the Unity viewport, drawn from the state the host
has already resolved on its own model rather than from a second customization system:

- the body's texture slots as its render passes bind them -- a file, or the body and eye images
  the host composited (`CharTexture`), sent as pixels;
- its geoset display flags, after every customization, equipment and helm rule has run;
- every model `WoWModel::refreshMerging` laid into it (collection armour, customization parts),
  each skinned to the body's bones through the bone table the host computed;
- every item model attached to it, at the attachment id the host attached it at (sheathed or not),
  on the body bone and offset of the character's own attachment table;
- the closed hand while a weapon is held (the finger key bones posed from HandsClosed).

All of it travels as one `characterScene` per change (see `UnityIpcServer.h`), sent from the hidden
canvas's clock tick when anything it describes has changed and never faster than the player
answers; the player applies a scene whole, in one frame, without reloading the body, re-framing
the camera or restarting the animation.

## Responsibility split

| WMV (wxWidgets application) | Unity (embedded player) |
|---|---|
| Application UI (menus, panes, dialogs, settings) | Modern rendering pipeline (materials, lighting, post, HDR/PBR) |
| Active client / profile (retail, PTR, classic, legacy MPQ) | Scene graph: models, attachments, equipment, maps, fog |
| CASC / MPQ access — the only component that reads game archives | Cameras, orbit/controls, capture-friendly output (transparent/solid backgrounds, stream features) |
| Databases / metadata (DB2, DBC, listfiles, customization, display info) | Animation playback / skinning on the GPU |
| Runtime commands: what to show, customization/equipment, camera, capture | Requests raw assets / metadata from WMV (`getAsset`, `getAssetByFileDataID`); announces itself (`unityReady`) |
| The archived OpenGL canvas, as a hidden internal service: GL context, loaded model, animation clock (see "Archived OpenGL viewport") | — |

Unity never parses CASC/MPQ itself and never depends on files WMV writes to disk: it
**asks WMV** for what it needs (raw file bytes by path or FileDataID, resolved metadata)
over the IPC channel, and WMV serves it from its existing file providers and database.

## Architecture

```
+----------------------------- WMV (wxWidgets) ------------------------------+
|  UnityRendererHost (wxPanel) -- THE VIEWPORT, always the centre pane       |
|  - launches UnityRenderer.exe with "-parentHWND <hwnd> delayed             |
|    -wmvPort <n>" (the player reparents itself INTO this panel; own         |
|    process + device)                                                       |
|  - resizes the embedded child window                                       |
|  - paints a notice (and hides the player's window) when it cannot show     |
|    what is loaded; notices a player that exited or disconnected            |
|  - WM_CLOSE (+terminate fallback) on app shutdown and on a restart         |
|                                                                            |
|  ModelCanvas (OpenGL) -- ARCHIVED: hidden, never painted, not a pane       |
|  - the app's single WGL context, bound to this canvas' own HWND            |
|    (texture decode/upload, character composites, exporter read-backs)      |
|  - owns the loaded model and the animation clock whose tick drives the     |
|    heartbeat and character-scene pushes                                    |
+----------------------------------------------------------------------------+
        ^ state                               | runtime commands      ^ asset/metadata requests
        | (unityReady, ...Applied)             v (loadWoWModel, ...)   | (getAsset,
        |                                                              |  getAssetByFileDataID)
        |                                                              v assetResponse (bytes)
   +----+---------------------------------------+----------------------+-----------+
   |         WMV asset-access / runtime API over local IPC (TCP, JSON lines)       |
   |     served by WMV from its CASC/MPQ file providers + GAMEDATABASE             |
   +-------------------------------------------------------------------------------+
                                   |
                                   v
   UnityRenderer.exe (separate process, own graphics device)
   - M2 / skin / .skel / .anim / BLP / DB-driven loaders that build the scene straight
     from the bytes and metadata WMV serves -- no intermediate files
   - materials, lighting and shadows of its own; animation that follows the app's selection
     and transport; characters dressed from the host's characterScene
```

Why the two never collide: on Windows the OpenGL context is a single process-global WGL
context bound to the hidden ModelCanvas's own HWND/HDC. The player runs **out of process**
and renders into **its own** child window with its own device, so the two never share a
device, context or pixel format, and nothing in `UnityRendererHost` calls into GL. The
canvas is a child window of the frame but not a wxAUI pane: the Unity panel is the only thing
in the centre.

## Archived OpenGL viewport

The OpenGL viewport cannot be shown or used, but the `ModelCanvas` object is still created at
startup as a hidden internal service. It is created hidden (`Hide()` before `Create()`) and is
never shown, in an interactive run or a headless one. It is not a wxAUI pane, so no saved
layout, reset or pane toggle can reach it (and nothing may call `GetPane(canvas)`: for a window
the manager does not know, wx hands back one shared, writable "null" pane). It never paints: its
paint handler draws nothing and logs an error the first time it is entered, and the headless
self-test fails if that ever happens. It is kept because it still does jobs nothing else does
yet:

- **It owns the only GL context.** Every texture decode and upload, the character body and eye
  composites the viewport is sent as `characterImage` pixels, and the exporters' texture
  read-backs (the FBX exporter bakes combiner textures through `RenderTexture`) need the WGL
  context bound to its window. A window that was never shown holds that context on Windows; the
  headless self-test runs with it. If the context never initialises, the viewport's notice says
  that textures cannot be decoded.
- **It owns what is loaded:** the model and its attachments (`canvas->root`, `model()`), a WMO or
  a map tile. An image picked in Browse is no longer loaded at all: the pick is only remembered,
  so the viewport can name it in its notice (saving the image is the Browse right-click menu's
  job).
- **It owns the animation clock.** Its 10 ms timer calls `tick()` and nothing else -- no redraw
  and no numpad camera. The tick sends the playback heartbeat and the character scene, and is
  where a mount or a dismount is noticed.
- **It is where exports read the pose from,** and that pose is now computed rather than left
  behind by a drawn frame: `ModelViewer::UpdateExportPose` poses the model and its attachments
  (parents first, `WoWModel::updatePose`) at the Animation panel's current animation and frame
  just before an in-app export and the out-of-process FBX export read it, and scrubbing
  (`AnimManager::ForceModelUpdate`) updates the pose without drawing.

`LightControl` is still created, and never shown, only because `ModelCanvas::InitGL` requires
one before it marks the canvas initialised -- and the clock runs only once it is.

**Archived implementation.** The rest stays in the tree, compiled but unreferenced, each marked
with an "archived: unreachable since the OpenGL viewport was archived" note:

| where | what it was |
|---|---|
| `modelcanvas.cpp/.h`: `RenderArchivedFrame`, the `Render*` routines, `RenderToBuffer`, `Screenshot`, `CaptureSequenceFrame`, the saved scene states, `OnMouse` / `OnKey` / `OnCamMenu`, `CheckMovement`, `toggleOpenGLDebug` | the on-screen frame, screenshots and capture, the mouse and keyboard camera |
| `ImageSequenceExporter.*`, `ImageSequenceDialog.*` | File > Export Image Sequence |
| `AnimExporter.*` | GIF / AVI export |
| `imagecontrol.*` | the sized-screenshot pane |
| `DisplaySettings.*` | Settings > Display: display mode, field of view, GL capabilities, environment mapping |
| `lightcontrol.*` | the lighting pane (one is still created, see above) |
| `ColorPickerDialog.*` | View > Background Color |

**What went with it.** Everything that only drove the OpenGL drawing is gone from the UI: File >
Save Screenshot (F12) and Export Image Sequence, the command bar's Reset camera and Screenshot
buttons, View > Background Color, Load Background (Ctrl+L), the Camera submenu with Use model
camera, Set Canvas Size and OpenGL debug info, Ctrl+B (bounds), F1-F4 and Ctrl+F1-F4 (saved
views), the canvas's mouse camera, numpad camera keys and 0-9 speed keys, the Lighting menu
remnants, Options > "Always show default doodads in WMOs", the Settings > Display page, Settings >
General's "Show Particle" and "Zero Particle" (they only changed the host's own particle
simulation; the player runs its own emitters), and the Keyboard Shortcuts rows for the OpenGL
viewport. The former Model Control panel is View > "Attachments...": the model/attachment list
that re-targets the Animation panel, plus Render and Scale, which are enabled only for an item
attached directly to a character, because that is what the character scene carries.

The session and config keys that only served the canvas are no longer read or written
(`Session/CanvasWidth`/`CanvasHeight`, the background colour and image, the particle flags,
`Graphics/*`, `Settings/SSCounter`, `Settings/DefaultFormat`); old values in an existing
`Config.ini` are ignored. Headless batch runs no longer write `ss_*.png` screenshots (see
"Headless self-test"), and `-imgseq` is gone.

Where this document, the player's README or the player's comments compare against "the legacy
(OpenGL) viewport", they mean this archived host code, which is still the reference for rules
such as geoset visibility, texture selection and the M2 combiners. Those comparisons can no
longer be made by eye in the application, only by reading that code.

## Locating the player

1. `Tools/UnityRendererPath` in `userSettings\Config.ini` (when non-empty), else
2. `tools\unity-renderer\UnityRenderer.exe` next to the WMV executable.

The player is required for a picture: without one at that path the viewport shows the
"not running" notice naming the path it looked at, and View > "Restart Unity Renderer" (or
the notice's button) looks again. Player logs go to `userSettings\unityRenderer.log` (next
to WMV's own log). The player is built locally from `Tools/UnityRendererProject/` — the
repository contains **no** Unity build output, and nothing in the installer or the CMake
install rules ships one yet.

## IPC (implemented; the current player announces protocol 3)

**WMV is the server.** `UnityRendererHost` starts a TCP listener bound to `127.0.0.1` on an
ephemeral port *before* launching the player and passes the port on the player's command
line (`-wmvPort <n>`); the player connects back. Localhost only, one client (the embedded
player). Transport: newline-delimited JSON, one object per line, UTF-8. Implemented by
`Source/wowmodelviewer/UnityIpcServer.*` (plain Winsock, polled from the GUI thread by a
wxTimer -- the app has no Qt event loop, and the game-file providers must be used from the
GUI thread anyway) on top of `UnityAssetAccess.*` (the narrow "raw bytes from the active
client" layer: CASC or legacy MPQ through the same `GAMEDIRECTORY` providers the rest of the
application uses). Player side: `Tools/UnityRendererProject/Assets/Scripts/WmvIpcClient.cs`.

**Player -> WMV**

```json
{ "type": "unityReady", "protocolVersion": 3 }
{ "type": "getAsset", "requestId": "abc123", "path": "creature/chicken/chicken.m2" }
{ "type": "getAssetByFileDataID", "requestId": "abc124", "fileDataID": 123456 }
{ "type": "getModelTextures", "requestId": "abc125", "fileDataID": 123200 }
```

`getModelTextures` answers with `modelTextures { requestId, ok, fileDataID, textures:[{ index,
type, fileDataID, source }] }`. It exists because a modern M2 does **not** name its replaceable
textures: a creature skin's TXID entry is 0 and its texture array carries no filename, because
the actual skin comes from the client database.

`type` is the WoW texture **type** the texture feeds -- 11, 12, 13 for the three creature skin
slots -- not a position. A model's texture-variation order and its M2 texture-slot order need not
agree, so the renderer matches this against the type each M2 texture declares.

`source` says where the answer came from, in the order WMV tries them:

- **`source: "selection"` -- what the viewport is actually showing.** A creature normally has
  several skins (`chicken2` offers seven, plus a folder texture), and the database can only say
  which is the *default*. Which one is on screen is a UI fact, so WMV answers from its own skin
  selector -- the same `TextureGroup` the host hands to `WoWModel::updateTextureList` for its own
  model. Without this the viewport would show the database default the moment the user touches
  the dropdown, or whenever "Random Skins" picks something other than the first display.
- **`source: "database"` -- the model's default skin.** `CreatureDisplayInfo` joined to
  `CreatureModelData` on the model's FileDataID: the same relation the viewer's own skin list is
  built from. Used when there is no selection to read -- a model with no skin list, or a request
  about a model that is not the displayed one.
- **`source: "convention"` -- a labelled fallback, not a substitute.** A handful of legacy
  assets are still shipped in CASC but referenced by no creature display at all (for example
  `creature/chicken/chicken.m2`, superseded by `chicken2`), so the database has nothing to say
  about them. Rather than render them untextured, WMV looks for the conventional sibling skin
  in the listfile and marks the result `convention`, so the renderer -- and anyone reading the
  logs -- can tell a naming guess from database truth. A model that resolves only this way is
  a regression case, never the proof that texture resolution works.

The response carries metadata only; bytes are still fetched with `getAssetByFileDataID`.

**WMV -> player**

```json
{ "type": "loadWoWModel", "path": "creature/chicken/chicken.m2", "fileDataID": 123200, "client": "active" }
{ "type": "assetResponse", "requestId": "abc123", "ok": true, "path": "creature/chicken/chicken.m2",
  "fileDataID": 123200, "byteLength": 101840, "sha1": "1dc88a19...", "encoding": "base64", "data": "TUQyMb..." }
{ "type": "assetResponse", "requestId": "abc123", "ok": false, "error": "not found" }
{ "type": "modelSkin", "ok": true, "fileDataID": 1521037,
  "textures": [ { "index": 0, "type": 11, "fileDataID": 1521061, "source": "selection" } ],
  "geosets": [ 101 ], "hasGeosets": true }
{ "type": "modelAnimation", "fileDataID": 1521037, "sequenceIndex": 2, "animID": 0,
  "durationMs": 2000, "loop": true }
{ "type": "modelAnimationState", "fileDataID": 1521037, "sequenceIndex": 2, "playing": true,
  "timeMs": 840, "speed": 1.0, "loop": true }
```

Semantics:

- `unityReady` is answered by a `loadWoWModel` for whatever model is loaded (and every later
  model load pushes a new one). `client` is `"active"` -- the player never chooses a client;
  WMV's active client/profile is the only data source.
- `modelSkin` is **pushed, not requested**: the skin on display changed. Same payload as a
  `modelTextures` reply, built by the same resolver, so the push and the pull cannot disagree.
  The player re-uploads only the textures that actually changed and keeps the mesh it built --
  a skin change alters which image a material samples, nothing about the geometry. It is sent
  from `AnimControl::SetSkin`, the single funnel every skin change goes through (the dropdown,
  the default chosen on model load, and NPC import), plus `SetSingleSkin` for the per-slot
  folder-texture lists.
- `modelAnimation` is **pushed, not requested**: the animation on display changed. It is sent
  from `AnimControl::SelectAnimation`, the single funnel every animation change goes through (the
  default picked while a model loads, the dropdown, and the loop control), and once more right
  after `loadWoWModel` so the player starts on the app's selection rather than on an idle it
  chose for itself.
  **`sequenceIndex` is the field that decides what plays.** It indexes the model's animation
  table, which is both how the keyframes are stored and how the app's own selector identifies a
  choice -- its dropdown labels literally end in `[n]`. Two sequences routinely share an `animID`
  (sub-animations of one action: chicken2's sequences 0 and 8 are both animID 5), so the id cannot
  pick one; it travels for the log and for recognising the idle (`animID` 0, "Stand").
  The player holds the model's `.m2` bytes and re-parses them for the requested sequence, because
  only one sequence's keyframes are read at a time -- a boss has 109 of them. Nothing else moves:
  the mesh, its materials, its textures and its geoset selection are untouched by which animation
  is playing.
- **Changing the animation reloads nothing.** `modelAnimation` re-reads one array -- the bone
  tracks for the new sequence -- into the model already in memory, and touches nothing else. No
  asset is requested for an in-file sequence, the .m2 is not parsed again, and the mesh, materials,
  textures, geoset selection and skeleton are all left exactly as they are. `loadWoWModel` remains
  the only path that builds anything. That split matters for more than tidiness: re-parsing the
  whole file per selection allocated megabytes each time on a large model, and it was that garbage
  -- collected a frame or two later -- that the viewport showed as a stutter.
  **Everything a switch reads is then cached**, per sequence: the raw tracks, and the tracks
  converted into the renderer's space. Returning to an animation already watched costs a dictionary
  lookup and no allocation, which is what the user actually does when comparing two animations.
- **Sequences whose keyframes live in a .anim file play too.** A sequence without flag 0x20 keeps
  its track HEADERS in the .m2 -- counts and offsets, per sequence, exactly where an in-file
  sequence keeps them -- but those offsets address a separate .anim file. The AFID chunk says which
  file: `animId, subAnimId, fileId`, matched on BOTH ids because two sequences routinely share an
  animId as sub-animations of one action. So playing one needs nothing but the right buffer to read
  the entries from, which is precisely the split the host's own model code makes
  (`WoWModel::readAnimsFromFile` fills a map keyed by animID, and the track reader picks the buffer
  from it). The bytes come over the existing asset channel and are cached per file, so a sequence is
  fetched at most once per model. Without them the sequence falls back to the idle and says which
  file it was waiting for. **This is what made Agronn's SitGroundDown play in the old OpenGL
  viewport and not in this one.**
  Those files are fetched **when the model loads**, not when an animation first needs one. Fetching
  on demand cost 16-18 ms of round trip on the first switch to each external animation, during
  which the PREVIOUS animation stayed on screen -- so picking one did not appear to do anything
  until it landed. A creature names a handful of them (Agronn 8, ~300 KB), they arrive while the
  user is looking at the model, and no switch waits on one.
- **The controls that change an animation also change whether it is RUNNING, and both have to be
  pushed.** `AnimControl::OnAnim` stops the model, selects, and plays again; so do the loop control
  and the load path. Only the selection used to push, so the renderer heard "not running" and
  nothing after it, and held until the next heartbeat -- a half second to a second, on every model.
  Each of those three now pushes the settled state after `Play()`. When adding a control that
  touches playback, push after the transport has settled, not in the middle of it.
- **A selection carries its playback state with it.** `modelAnimation` is followed immediately by
  a `modelAnimationState` from the same control path, so the renderer knows whether to run the
  animation it was just given, how fast, and from where -- without waiting on the heartbeat. The
  heartbeat corrects DRIFT; it is not what starts an animation. Measured end to end, the state
  arrives 0-2 ms after the selection.
  The state is also **kept** rather than dropped when it cannot be applied yet. A selection that
  falls back to the idle, or one still resolving, used to discard it entirely and leave the
  previous animation's play/pause and speed in force. Play/pause and speed are the app's state, not
  the sequence's, so they are applied regardless; only the position waits for the sequence it
  belongs to.
- `modelAnimationState` is **pushed, not requested**: it carries how that animation is being
  played -- running or held, how fast, and where in the sequence the app is.
  Unlike the skin and the animation choice there is **no single funnel** to hook: play, pause,
  stop, clear, the two step buttons, the speed slider and the frame slider each change it, and the
  time advances every frame with no control involved at all. So it is pushed two ways -- forced
  from each of those controls, and on a **one-per-second heartbeat** from the (hidden) canvas's
  clock tick while something is playing.
  The heartbeat is the correction channel for two renderers timing themselves independently. The
  **player** decides whether a given `timeMs` is worth acting on, because only it knows where its
  own clock is: a difference under about a frame (40 ms) is ignored, and anything larger snaps. That
  split is the whole design -- snapping to every message would trade drift for a visible stutter
  once a second, and never snapping would let the two drift apart. A scrub or a stop arrives with
  the app's time already far from the player's, so it snaps without needing to be marked special.
  **Global sequences keep running while the animation is paused, and ignore the speed.** That is
  the host clock's own behaviour, not an accident: it advances its global clock before it
  decides whether the animation is paused, and the speed multiplier lives inside the animation
  tick alone (`ModelCanvas::tick`, `AnimManager::Tick`). A torch keeps flickering on a creature
  held still.
- `geosets` / `hasGeosets` ride along with both `modelTextures` and `modelSkin`, because a display
  variant can differ from another by **geometry** rather than texture. `creature/horse3/horse3.m2`
  is the worked example: three of its dropdown entries share one texture and differ only in
  whether geoset 101, 102 or 103 is switched on -- a long mane and tail, or a cropped one.
  A submesh is drawn when **its geoset number is 0, or the variant switches that number on**,
  which is the host's own rule (`WoWModel::setCreatureGeosetData`: every geoset in
  `[1, 900)` is shown iff the set names it, and `setLOD` starts them at `display = (id == 0)`).
  The renderer already knows every submesh's number from the .skin it parsed, so only the SET
  travels.
  `hasGeosets` separates two things an empty list cannot: `true` with an empty list means "this
  variant switches none on" -- which hides every submesh whose number is not 0 -- while `false`
  means the host had no creature selection to report and geoset visibility must be left alone.
- `getAsset` / `getAssetByFileDataID` return the **raw, whole file** exactly as stored in the
  active client (modern `.m2` bytes start with their `MD21` chunk header, etc.). Paths are
  normalised (lower-case, forward slashes). By-FileDataID works for CASC clients; a legacy
  MPQ client (name lookup only) answers `"FileDataID lookup is not supported by the active
  client (MPQ, name lookup only)"`. Other errors: `"not found"`, `"no game client loaded"`,
  `"game client is still loading"`, `"could not open file in the active client"`,
  `"short read ... file may be encrypted or damaged"`.
- `sha1` is the hex SHA-1 of the raw bytes; the Unity client recomputes it after decoding.
- **V1 carries the bytes as base64 inside the JSON line.** Simple and debuggable (~33 %
  overhead). A binary frame -- the same JSON header followed by a length-prefixed payload --
  can replace the `encoding`/`data` pair later without touching the request side.
- Nothing is written to disk on either side; this is runtime access, not an export workflow.

WMV logs every step with the `[unityipc]` prefix: listening port, player connected,
`unityReady`, each request (path / FileDataID), the provider used (`CASC` / `MPQ`), bytes
returned or the error. The player logs the same exchange (`userSettings\unityRenderer.log`
for the TestStub) and shows it as status text in the viewport.

**Normal launch vs. self-test.** A normal launch -- the warm start when the application opens
(`ModelViewer::WarmStartUnityViewport`), or View > "Restart Unity Renderer" -- drives only the
happy path: the player connects, announces `unityReady`, receives `loadWoWModel` for whatever is
loaded and requests that model's files. The protocol's error paths are exercised only in diagnostic
mode, where WMV appends `-wmvSelfTest` to the player command line and a diagnostic-capable
player (the TestStub) additionally probes a missing asset and an unknown message type. WMV's
handling of both is always present -- only the test *requests* are gated.

**Headless self-test.** `wowmodelviewer.exe -mo creature/chicken/chicken.m2 -unityipctest`
launches the installed player (TestStub or a real build) into the Unity viewport of the
off-screen frame with `-wmvSelfTest`, through the same `ModelViewer::StartUnityRenderer` the
application uses, drives the full exchange (connect, `unityReady`, `loadWoWModel`, `getAsset`,
`assetResponse`) plus the negative probes, checks the missing-asset and by-FileDataID paths
in-process, and shuts the player down. Result lines carry the `[unityipc-test]` prefix
(`RESULT: PASS|FAIL`).

Its **viewport check** holds the archive in place. It fails unless the Unity viewport is the shown
centre pane, no menu item offers a "main viewport" choice, the canvas is neither a wxAUI pane nor
a shown window, the hidden canvas's GL context initialised (`canvas->init` and `video.render`),
the viewport decision for the loaded model is "the model" with no notice up, and the canvas's
animation clock advances while the model plays. For a character, its check also fails unless at
least one composited `characterImage` was sent. That clock is measured inside an
event loop activated for the measurement: the self-test runs inside `OnInit`, before the
application's loop exists, and a bare `wxYield` there dispatches no timer messages at all (which
is why an earlier version reported that the canvas does not tick headlessly). At the end of the
run it also fails if the canvas's paint handler was entered even once.

Other headless batch runs (`-mo`, `-item`, `-npc`, `-armory`, a `.chr` file) used to finish by
writing an `ss_*.png` screenshot of the OpenGL viewport. They now log one line saying screenshots
are not available in the Unity-only viewer, and write no image; the `-imgseq` smoke test is gone.
`-fbxexport` (the out-of-process FBX export child) is unchanged, and computes its pose with
`UpdateExportPose` before exporting.

## Status

**Implemented**

- Runtime CASC/MPQ asset access: the player asks WMV for raw WoW files over IPC and never
  touches game archives or the disk itself.
- Static M2 mesh rendering: the modern chunked M2 (MD21 / MD20 version 272 as current retail
  ships) and its .skin profile are parsed at runtime into a Unity mesh with per-batch
  submeshes, converted to Unity's coordinate system with corrected winding.
- Runtime BLP decoding for the formats the creature pipeline uses: palettized (alpha 0/1/4/8),
  DXT1 / DXT3 / DXT5 and raw BGRA, decoded straight from the received bytes into an in-memory
  texture. Anything else is reported by name instead of being decoded into garbage.
- Static materials taken from the model's own render state rather than approximated: every M2
  blend mode (opaque, alpha key, alpha blend, both additive forms, modulate, modulate 2x and
  premultiplied), the alpha test keyed where the legacy combiner keys it, depth write from the
  material's own flag alone, and two-sided when the model asks for it. Over a spread of 300
  retail creature models, 26.6% of draw batches ask for a blend mode that was previously drawn
  opaque -- an additive glow rendered opaque is a solid box where a wisp of light belongs.
- The M2 texture combiners a static pose can reproduce: the products of the two units, the two
  alpha-masked forms and the decal, with unit 1 sampled from whichever source the material's
  vertex shader names (either stored UV set, or a generated environment sphere map). That covers
  1421 of 1424 batches in the same sample; the remaining three are logged by name and drawn from
  unit 0 alone.
- Model textures the M2 does not name (replaceable creature skins) are resolved by WMV from
  the client database and handed to the player as FileDataIDs (`getModelTextures`), labelled
  `database` or -- for orphaned legacy assets only -- `convention`.
- Skinned rendering in the rest pose: the model's bone hierarchy is rebuilt as Unity transforms,
  the per-vertex influences and bind poses are handed to a SkinnedMeshRenderer, and the mesh is
  deformed by that rig instead of being drawn rigid. No animation track is evaluated yet, so
  every bone sits at rest -- and because an M2's stored vertex positions ARE that rest pose, the
  result is the static mesh it replaces to within float noise (largest measured deviation across
  the validation models: 3.8e-6 units). Models whose bones live in a separate skeleton file are
  still drawn static, and say so.
- Animation playback that follows the app: the viewport plays the animation WMV is playing,
  switching with the dropdown, and mirrors how it is being played -- play/pause, speed, and the
  position in the sequence, including scrubbing the frame slider. Bone tracks are evaluated the
  way the legacy evaluator does, including the global sequences that run on their own clock and
  keep running while the animation is held. With nothing selected yet the model's default idle
  plays -- the first sequence whose AnimId is "Stand", which is the same choice the app's own
  animation selector makes and is not sequence 0. A sequence whose keyframes are not in the .m2 falls back
  to that idle and says so. `-wmvNoAnim` returns the model to the rest pose.
- Bounds-driven camera framing, so a loaded model is visible immediately.

**Not yet implemented**

- Animation UI of the renderer's own: the viewport follows WMV's selector and transport
  (play/pause, speed, current time and looping are synced) and has no controls of its own.
  Blending between sequences and following a queued "next animation" chain are not synced.
- Material animation, in part: colour, colour alpha, texture weight and texture translation and
  scale tracks are evaluated (`WmvMaterialAnimator.cs`), but texture rotation tracks are parsed
  and not applied, and lit passes get no animated tint or opacity (see that file's header).
  Particle and ribbon emitters are drawn (`WmvEmitterRuntime.cs`).
- The rest of the WoW material system: the specular lobes the archived OpenGL renderer leaves
  unweighted by default, and the few combiners that mix more than two contributing units.
- Attachments on a model that is not a playable character. A character's items and merged
  armour are drawn (see "Characters").
- For characters: secondary (upper-body) and mouth animations, and a mount (a mounted character
  gets a notice).
- Maps, terrain, WMOs, fog; BLP images picked in Browse. Each of these loads and gets a notice.
- Full parity with the archived OpenGL renderer.

There is no fallback: the Unity viewport is the only renderer the user sees, and what it cannot
draw yet is named in the viewport's notice (see "What the viewport shows") rather than drawn by
another renderer. There is no asset-export workflow: every byte the renderer uses arrives over IPC
at runtime and nothing is written to disk.

## Migration roadmap (historical)

Kept as the record of how the renderer was built up. The cut-over at the end has happened, and not
as this list planned it: the OpenGL viewport was archived rather than kept as a fallback, ahead of
parity (see "Archived OpenGL viewport").

- **V0 (merged):** `View -> Unity Renderer` opens a dockable pane, launches the player
  embedded in it, resize/shutdown work, missing player handled gracefully; player shows a
  test scene. Unity optional at build and run time.
- **V1 (this branch): runtime asset access.** WMV hosts the IPC server, the player connects
  back, announces `unityReady`, receives `loadWoWModel` and fetches the model's raw bytes with
  `getAsset` / `getAssetByFileDataID`, served by WMV from the active client (CASC or MPQ) and
  verified by byte length + SHA-1 on the player side. No M2 parsing or mesh rendering yet.
- **V2 (this branch): direct rendering.** Unity-side M2 + skin + BLP loaders built on those
  bytes render a WoW model directly from game data as a static mesh -- parsed, converted,
  textured and framed at runtime. Primary validation target is
  `creature/chicken2/chicken2.m2`, a current, database-backed creature: 1632 vertices, 796
  triangles, 2 submeshes, 2 materials (opaque + alpha), skin resolved through
  `CreatureDisplayInfo` to a 256x256 DXT5 texture. Binary framing for asset payloads is still
  open.
- **V3+ (Unity first):** characters + customization, equipment/attachments, animation,
  maps/terrain/fog, capture-friendly backgrounds and scene/stream features -- each built on the
  Unity pipeline.
- **Cut-over (done):** Unity is the only viewport, with no toggle; the OpenGL viewport is
  archived and cannot be shown. Content Unity cannot draw yet shows a notice in the viewport
  instead of falling back to the canvas.

Explicitly deferred until the direct renderer is solid: full maps/ADT terrain, WMO
placement, volumetric fog, armory donations, equipment.

## The player project

Source-only player pieces live in `Tools/UnityRendererProject/` (see its README for build
steps). `Tools/UnityRendererProject/TestStub/` contains a tiny Win32 stand-in that honours
the same `-parentHWND` embedding contract AND speaks the asset-access part of the IPC protocol
(it announces protocol 2: `unityReady`, `getAsset` on `loadWoWModel`, `assetResponse`
decode/length checks), so both the WMV-side host and the runtime asset access can be exercised
without any Unity install -- see the headless self-test in the IPC section. It is a test tool,
not a viewer: it draws status text only, so installed as the player it shows no model, and a
character gets the out-of-date notice.
