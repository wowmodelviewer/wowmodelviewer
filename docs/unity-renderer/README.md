# Embedded Unity Renderer

## Direction (read this first)

The embedded Unity viewport is the **new renderer foundation for WMV** and its **intended
primary viewport**. The existing OpenGL viewport (`ModelCanvas`) remains the **legacy /
fallback renderer during the migration** and is not where new rendering work goes.

Concretely:

- **Unity first.** Future rendering features — characters, equipment, maps, fog, OBS
  scenes and stream features — target the Unity viewport first. The OpenGL path receives
  maintenance only.
- **Unity renders directly from WoW data.** There is **no OBJ / FBX / GLB export workflow**
  between WMV and Unity. Unity requests the raw WoW assets (M2, skins, BLP textures, …) and
  metadata it needs from WMV over IPC and builds the scene from them. (WMV's export plugins
  remain a separate, user-facing feature; they are not part of the render path.)
- **Responsibility split** (detailed below): **WMV provides the application UI, the active
  client/profile, CASC/MPQ access, DB/metadata and the runtime commands; Unity provides the
  modern rendering pipeline and asks WMV for assets/metadata.**
- **Optional for now.** While the migration is in progress the Unity viewport is optional at
  build and run time: nothing in the normal WMV build depends on Unity (no Unity SDK, no
  binaries in the repo; the IPC server only adds the standard Windows socket library), and a
  missing player build produces a clear
  message while the app — and the legacy OpenGL viewport — carry on unchanged. "Optional"
  describes the current migration stage, not the destination.

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

## Which viewport owns the centre

The Unity renderer is the **main viewport for the models it supports** -- creature M2s. Loading one
puts it in the centre of the window; View > "Unity as main viewport" turns that off and hands the
centre back to the OpenGL canvas, and the choice is remembered (`Tools/UnityPrimaryViewport` in
Config.ini).

**It is started when the app starts, not when a model needs it.** The player is a game engine: it
takes about a second to come up (measured at ~1.1 s to the point where it reports ready). Started on
first use, the user picked a creature and then watched that happen, with the model appearing
afterwards — so the wait was attributed to the model, which had nothing to do with it. Starting it
during app launch spends the same second while the user is still looking at an empty application,
and the first creature goes straight into a player that is already connected. Until it reports in,
the pane paints "Starting Unity renderer..." on the player's own background colour, so the handover
is not a visible flash and an empty viewport never looks broken.

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

**Startup is viewer-first.** With the Unity viewport primary, an interactive launch opens
maximised showing the viewport and nothing else: the file tree, animation controls and character
pane start hidden, and the viewport area is plain dark until the player is up — no caption, no
logo, no placeholder object. Every hidden pane is one item away on the View menu and stays for the
session; the next launch starts clean again, which is the point of the mode. Launch goes straight to borderless fullscreen; F11 or Esc leaves it, and the menu bar
survives it deliberately — without the caption there would otherwise be no visible way out, or
back to View > "Unity as main viewport".

**No test objects in a normal run.** The spinning cube that used to fill the viewport before a
model was chosen is off unless `-wmvPlaceholder` is passed. It answered "is the embedded player
alive at all?", which is no longer an open question; an empty viewer should look empty.

**Build the player with the splash screen OFF.** `PlayerSettings.SplashScreen.show = false` (and
`showUnityLogo = false`). A splash makes sense for a game; inside another application's window it is
a game engine announcing itself in the middle of a model viewer. Unity 6 makes this optional for
every licence tier, so there is no reason to keep it. A player built with it on still works — the
user just sees the logo during app startup instead of during model selection.

**The OpenGL canvas is never torn down, only uncovered.** It still loads the model, still owns the
animation clock, and is still what every `Send*ToUnity` call reads from — the Unity viewport
mirrors it rather than replacing it. Promoting it is therefore a routing change of one pane, not a
renderer migration, and that is what makes the fallback trivial: the canvas is a `Show(true)` away.

Deliberately still OpenGL:

| | why |
|---|---|
| characters | no equipment pipeline in the Unity renderer yet |
| WMOs, anything not an M2 | not modelled by the Unity renderer at all |
| screenshots, image sequences, `-mo` runs | they render through the canvas; a non-interactive run keeps the viewport it was given |
| comparison and debugging | one menu item away, on purpose |

If the player cannot be launched (no build, or a broken one) the centre stays with the OpenGL
canvas and the log says so — a missing Unity build costs you the new viewport, not the app.

## Responsibility split

| WMV (wxWidgets application) | Unity (embedded player) |
|---|---|
| Application UI (menus, panes, dialogs, settings) | Modern rendering pipeline (materials, lighting, post, HDR/PBR) |
| Active client / profile (retail, PTR, classic, legacy MPQ) | Scene graph: models, attachments, equipment, maps, fog |
| CASC / MPQ access — the only component that reads game archives | Cameras, orbit/controls, capture-friendly output (OBS, transparent/solid backgrounds, stream features) |
| Databases / metadata (DB2, DBC, listfiles, customization, display info) | Animation playback / skinning on the GPU |
| Runtime commands: what to show, customization/equipment, camera, capture | Requests raw assets / metadata from WMV (`getAsset`, `getAssetByFileDataID`); announces itself (`unityReady`) |
| Legacy OpenGL viewport (fallback during migration) | — |

Unity never parses CASC/MPQ itself and never depends on files WMV writes to disk: it
**asks WMV** for what it needs (raw file bytes by path or FileDataID, resolved metadata)
over the IPC channel, and WMV serves it from its existing file providers and database.

## Architecture

```
+----------------------------- WMV (wxWidgets) ------------------------------+
|  ModelCanvas (OpenGL)              |  UnityRendererHost (wxPanel)          |
|  LEGACY / FALLBACK during the      |  NEW RENDERER FOUNDATION              |
|  migration; maintenance only       |  - launches UnityRenderer.exe with    |
|  - app's single WGL context bound  |    "-parentHWND <hwnd> delayed        |
|    to this canvas' own HWND        |    -wmvPort <n>" (player reparents    |
|                                    |    itself INTO this panel, own        |
|                                    |    process + device)                  |
|                                    |  - resizes the embedded child window  |
|                                    |  - WM_CLOSE (+terminate fallback)     |
|                                    |    on app shutdown                    |
+----------------------------------------------------------------------------+
        ^ state                               | runtime commands      ^ asset/metadata requests
        | (unityReady)                         v (loadWoWModel)        | (getAsset,
        |                                                              |  getAssetByFileDataID)
        |                                                              v assetResponse (bytes)
   +----+---------------------------------------+----------------------+-----------+
   |         WMV asset-access / runtime API over local IPC (TCP, JSON lines)       |
   |     served by WMV from its CASC/MPQ file providers + GAMEDATABASE             |
   +-------------------------------------------------------------------------------+
                                   |
                                   v
   UnityRenderer.exe (separate process, own graphics device)
   - V0: blank scene + test cube (embedding proof)
   - V1: connects back, fetches the active model's raw bytes via getAsset (verified by
     byte length + SHA-1) -- runtime asset access proven, no parsing yet
   - V2+: M2 / skin / BLP / DB-driven loaders that build the scene straight from the
     bytes and metadata WMV serves -- no intermediate files
```

Why this is safe for the legacy viewport today: on Windows the OpenGL context is a single
process-global WGL context bound to the ModelCanvas's own HWND/HDC. The player runs
**out of process** and renders into **its own** child window with its own device, so the
two never share a device, context or pixel format. The Unity panel is a *sibling* of the
canvas in the wxAUI layout (the canvas stays the CenterPane for now; as the migration
progresses the Unity pane is expected to take over the primary viewport position, with
the OpenGL canvas retained as a fallback).

## Locating the player (current, optional stage)

1. `Tools/UnityRendererPath` in `userSettings\Config.ini` (when non-empty), else
2. `tools\unity-renderer\UnityRenderer.exe` next to the WMV executable.

Player logs go to `userSettings\unityRenderer.log` (next to WMV's own log). The player is
built locally from `Tools/UnityRendererProject/` — the repository contains **no** Unity
build output.

## IPC (protocol v1 -- implemented)

**WMV is the server.** `UnityRendererHost` starts a TCP listener bound to `127.0.0.1` on an
ephemeral port *before* launching the player and passes the port on the player's command
line (`-wmvPort <n>`); the player connects back. Localhost only, one client (the embedded
player). Transport: newline-delimited JSON, one object per line, UTF-8. Implemented by
`Source/wowmodelviewer/UnityIpcServer.*` (plain Winsock, polled from the GUI thread by a
wxTimer -- the app has no Qt event loop, and the game-file providers must be used from the
GUI thread anyway) on top of `UnityAssetAccess.*` (the narrow "raw bytes from the active
client" layer: CASC or legacy MPQ through the same `GAMEDIRECTORY` providers the OpenGL
viewport uses). Player side: `Tools/UnityRendererProject/Assets/Scripts/WmvIpcClient.cs`.

**Player -> WMV**

```json
{ "type": "unityReady", "protocolVersion": 1 }
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
  selector -- the same `TextureGroup` the OpenGL viewport hands to `WoWModel::updateTextureList`.
  Without this the two viewports disagree the moment the user touches the dropdown, or whenever
  "Random Skins" picks something other than the first display.
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

- `unityReady` is answered by a `loadWoWModel` for whatever is on the canvas (and every later
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
  the entries from, which is precisely the split the legacy viewport makes
  (`WoWModel::readAnimsFromFile` fills a map keyed by animID, and the track reader picks the buffer
  from it). The bytes come over the existing asset channel and are cached per file, so a sequence is
  fetched at most once per model. Without them the sequence falls back to the idle and says which
  file it was waiting for. **This is what made Agronn's SitGroundDown play in the OpenGL viewport
  and not in this one.**
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
  from each of those controls, and on a **one-per-second heartbeat** from the canvas tick while
  something is playing.
  The heartbeat is the correction channel for two renderers timing themselves independently. The
  **player** decides whether a given `timeMs` is worth acting on, because only it knows where its
  own clock is: a difference under about a frame (40 ms) is ignored, and anything larger snaps. That
  split is the whole design -- snapping to every message would trade drift for a visible stutter
  once a second, and never snapping would let the two drift apart. A scrub or a stop arrives with
  the app's time already far from the player's, so it snaps without needing to be marked special.
  **Global sequences keep running while the animation is paused, and ignore the speed.** That is
  the legacy viewport's own behaviour, not an accident: it advances its global clock before it
  decides whether the animation is paused, and the speed multiplier lives inside the animation
  tick alone (`ModelCanvas::tick`, `AnimManager::Tick`). A torch keeps flickering on a creature
  held still.
- `geosets` / `hasGeosets` ride along with both `modelTextures` and `modelSkin`, because a display
  variant can differ from another by **geometry** rather than texture. `creature/horse3/horse3.m2`
  is the worked example: three of its dropdown entries share one texture and differ only in
  whether geoset 101, 102 or 103 is switched on -- a long mane and tail, or a cropped one.
  A submesh is drawn when **its geoset number is 0, or the variant switches that number on**,
  which is the legacy viewport's own rule (`WoWModel::setCreatureGeosetData`: every geoset in
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

**Normal launch vs. self-test.** A normal `View -> Unity Renderer` launch drives only the
happy path: the player connects, announces `unityReady`, receives `loadWoWModel` and issues a
single `getAsset` for that model. The protocol's error paths are exercised only in diagnostic
mode, where WMV appends `-wmvSelfTest` to the player command line and a diagnostic-capable
player (the TestStub) additionally probes a missing asset and an unknown message type. WMV's
handling of both is always present -- only the test *requests* are gated.

**Headless self-test.** `wowmodelviewer.exe -mo creature/chicken/chicken.m2 -unityipctest`
launches the installed player (TestStub or a real build) embedded in the off-screen frame with
`-wmvSelfTest`, drives the full exchange (connect, `unityReady`, `loadWoWModel`, `getAsset`,
`assetResponse`) plus the negative probes, checks the missing-asset and by-FileDataID paths
in-process, and shuts the player down. Result lines carry the `[unityipc-test]` prefix
(`RESULT: PASS|FAIL`).

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
  plays -- the first sequence whose AnimId is "Stand", which is the same choice the OpenGL
  viewport makes and is not sequence 0. A sequence whose keyframes are not in the .m2 falls back
  to that idle and says so. `-wmvNoAnim` returns the model to the rest pose.
- Bounds-driven camera framing, so a loaded model is visible immediately.

**Not yet implemented**

- Animation UI of the renderer's own: the viewport follows WMV's selector and transport
  (play/pause, speed, current time and looping are synced) and has no controls of its own.
  Blending between sequences and following a queued "next animation" chain are not synced.
- Bones from a separate skeleton file (the SKID chunk and the SKPD parent it can defer to). A
  model that needs one is drawn from what it does have -- unskinned, or skinned but still -- and
  says which. (Keyframes in external .anim files named by the AFID chunk ARE read.)
- Animation of anything but bones: texture animation, colour and transparency tracks beyond the
  rest-pose visibility gate, particles, ribbons and attachments.
- The rest of the WoW material system: texture animation, colour and transparency tracks beyond
  the rest-pose visibility gate, the specular lobes the legacy viewport leaves unweighted by
  default, and the few combiners that mix more than two contributing units.
- Particles and ribbons.
- The character / equipment pipeline (customization, attachments, geoset rules).
- Maps, terrain, WMOs, fog.
- Full parity with the legacy OpenGL renderer.

Unity remains the migration target and the intended primary renderer; the OpenGL viewport
remains the legacy/fallback renderer during the migration. There is no asset-export workflow:
every byte the renderer uses arrives over IPC at runtime and nothing is written to disk.

## Migration roadmap

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
  maps/terrain/fog, OBS-friendly backgrounds and scene/stream features -- each built on the
  Unity pipeline, with the legacy OpenGL viewport kept as fallback until parity.
- **Cut-over:** once Unity covers the baseline feature set, it becomes the default /
  primary viewport; the OpenGL canvas remains available as legacy/fallback.

Explicitly deferred until the direct renderer is solid: full maps/ADT terrain, WMO
placement, volumetric fog, armory donations, equipment.

## The player project

Source-only player pieces live in `Tools/UnityRendererProject/` (see its README for build
steps). `Tools/UnityRendererProject/TestStub/` contains a tiny Win32 stand-in that honours
the same `-parentHWND` embedding contract AND speaks the v1 IPC protocol (`unityReady`,
`getAsset` on `loadWoWModel`, `assetResponse` decode/length checks), so both the WMV-side
host and the runtime asset access can be exercised without any Unity install -- see the
headless self-test in the IPC section.
