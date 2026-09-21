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

**A front light fills what the key misses.** A face is vertical, so its N·L against the
world-vertical key is about zero: under the key alone it gets only the ambient bands while the
shoulders and the top of the chest right below it take the key, and a head-and-shoulders view showed
a grey face on a bright body. `WmvOpaque` (and its copy in `WmvWmo`) therefore adds one weaker
directional, `FRONT_LEVEL` and `FRONT_DIR` in the constants block, built in the shader from the
camera that draws the model. It comes straight from the camera and level with the ground, and that
direction is a choice made by eye, not a measurement. A reference preview viewer's lit model shader,
read while it ran, lights a model with an ambient of 0.35, a primary light of 1.0 and two secondaries
of 0.35, all fixed to the camera and summed under a clamp to 1; the front light first took the
viewer-side secondary's bearing (45 degrees to the viewer's right, 35.3 degrees up), which lifted a
face by only 6 % and one side of it more than the other. Its level is a choice too: 0.35 of this
rig's key (0.35 × `LIGHT_LEVEL` = 0.2065), where that viewer's secondary is 0.35 itself. Nor is it
how that viewer lights a face: a face turned to its camera gets 0.457 from the primary, which comes
from below eye level, and this rig leaves the primary out to keep its overhead key. Four rules fit the
light to this rig. It is level in the world, so a camera below the model still finds the belly dark.
It is scaled by the key's shadow side (1 − N·L), so the 1.25 peak does not move. It never lifts a
surface past 1.0, the texture's own colour. And it is not occluded, because a light at the camera
reaches everything the camera sees: contact occlusion does not scale it, and its ceiling is judged on
the light that arrives, the ambient after that occlusion and the key after its cast shadow. It fills a
contact shadow toward what an open surface gets, and a cast shadow as far as the shadowed surface's own
shadow side allows, so a surface that faces the key keeps most of the map's depth. (Occluded like the
ambient, it barely reached the skin under a fringe or a jaw: those surfaces tilt up into the key, so
their unoccluded light already sat at the ceiling. With the ceiling judged on the key before its cast
shadow, the upper cheeks under a brow, which tilt up into the key but lie in the brow's shadow, kept
about a fifth of this light and were lit by the ambient alone: close up, a flat grey band ran from eye
to eye between the key-lit forehead and the front-lit lower face.)

**Cast shadows soften with their occluder's height.** The map's fixed 3x3 kernel gave every cast
shadow the same few-texel edge, so the head's shadow on the chest, cast from 25 cm above, ended in a
hard, stair-stepped line around a dark patch. `WmvShadowFactor` now judges each of the kernel's nine
taps twice: as before, and against the receiver's own tangent plane along the key. A tap the fixed test
counts that also stands above that plane is a real occluder, and its height is known. The fixed kernel
also counts the receiver's own surface on any slope (a lit slope keeps about three quarters of its key),
and the rig's exposure depends on that: counting it out clipped 12 % of the blood elf female's head box
against 4 %. So with no real occluder in reach the fixed verdict is returned bit for bit, and where an
occluder hides a tap the receiver's plane says what its own surface would have done there, so the inside
of a shadow, its edge and the open slope beside it keep the same share. (The first version counted that
share only on taps without an occluder: a faded shadow came out brighter than the lit skin at its edge,
with a dark band along the edge.) Where an occluder is found, eight more taps search a disc
`SHADOW_SEARCH` of the model radius wide, the edge's radius is the occluders' mean height times
`SHADOW_PENUMBRA` (0.20, the key's angular radius as a tangent), and thirty-two taps over that disc give
the occluded fraction. Every real occluder in reach is faded alike, by
1 / (1 + mean height / (`SHADOW_FADE` × R)), with `SHADOW_FADE` four times the contact march's reach. The
fade is one number per pixel, and weak, because the map holds the top of whatever stands over the
receiver: under a head it reads the crown and at the shadow's edge the side of the head, so a fade judged
tap by tap left a dark band along the edge, and a surface under a tall column (a foot under its leg, a
lip under a hood) fades as if its shadow were cast from the column's top. The discs turn per pixel with
the contact march's screen-space phase, which leaves a fine grain in wide edges. A surface facing away
from the key skips the map entirely, since its direct term is zero.

Measured on the blood elf female against the develop build, with the head framed on a head-sized box
and, in brackets, zoomed in at the full body's radius as the viewport does: the face goes from 0.92
(0.96) to 1.05 (1.09) of the shoulder tops, the brow comes up ×1.25, the skin under the eyes ×1.27–1.28
(×1.18–1.20), the neck under the jaw ×1.20 (×1.27) and the upper chest under the head ×1.20 (×1.17). The
shadowed chest sits at 0.82 (0.80) of the lit chest beside it, against 0.67 (0.65): the head's shadow
keeps a little over half of its contrast, under a soft edge. About 2,200–2,300 pixels per head capture
newly clip, nearly all of them the fringe's hair strands in the red channel, two thirds of which the
front light alone already clips, while 1,800–3,000 stop clipping. The light check's map darkening sits
12–27 % below the level front light alone on the blood elf female, 16 % on a hooded model and 5–15 % on
the rat mount (a third seen from below, where its belly keeps its flat-albedo p05, 0.1176); Algalon's
capture is byte-identical. The price is form: the light-only range (`-wmvLightCheck`, flat albedo) of
the full body seen from the front goes from 1.62x to 1.39x and of the rat mount seen three-quarter from
2.42x to 1.93x, where the level front light alone gave 1.52x and 2.13x. The front light's contact fill
alone takes them to 1.49x and 1.98x, the soft edge takes the full body to 1.42x, the fade adds nothing
to either, and counting the cast shadow in the front light's ceiling takes them to 1.39x and 1.93x.

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
shader compares each fragment against it with a 3x3 PCF kernel, widened into a soft edge where the
occluder stands high above the receiver (see "Cast shadows soften with their occluder's height"
above). `SHADOW_STRENGTH` says how much of the key an occluder removes and `SHADOW_SOFT` blurs the
edge; both are in the constants block.

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
canvas tick notices those), when the player announces itself or reports a character or a world model
it could not build, and when a stopped player is noticed. The first case that applies wins:

| what is loaded, or what is wrong | notice | button |
|---|---|---|
| a build without the Unity player (not Windows) | "Unity viewport unavailable" | -- |
| the GL context never initialised, so no texture can be decoded for the player | "Textures cannot be decoded" | -- |
| no player build at the configured path, an IPC listener that could not open, a player that would not start, exited unexpectedly, lost its connection or never announced itself | "The Unity renderer is not running", with the reason | Restart Unity renderer |
| an image picked in Browse (BLP) | "Image selected" | -- |
| a WMO whose root the host could not read (the file will not open, or it is not a root) | "World model cannot be read" | -- |
| a WMO with no FileDataID (a legacy client; the player fetches the root and its groups by FileDataID) | "Legacy client world model" | -- |
| a WMO the player reported it could not build, while that load is the one on display (until the next load or a player restart) | "World model could not be built", with the player's reason | -- |
| a WMO, with a connected player older than protocol 4 | "Unity renderer out of date" | -- |
| a map tile (ADT) | "Map tile loaded" | -- |
| a model with no game file behind it | "Model cannot be shown" | -- |
| a character riding a mount, with a connected player older than protocol 5, or whose rider is not a character model with a FileDataID (the canvas model is then the mount) | "Mounted character" | -- |
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

**Mounted characters: the host side (protocol 5).** The mount choice (`CharControl::OnUpdateItem`,
`UPDATE_MOUNT`) loads nothing for the character: it puts the mount model on the canvas root and on
the canvas, and the character's node -- already a child of that root -- hangs from the mount's
attachment 0 from then on. The canvas model is the mount, so the host asks for the character
through the rider accessor (`ModelViewer::riderModel`, the model of `CharControl::charAtt`), and
`canvasShowsMountedCharacter` holds while the context is a character, the rider is a character model
with a FileDataID and a mount is on its node's parent. Every way of choosing a mount reaches that one
choice with a row of the mount dialog's list (`CharControl::fillMountChoices`): Character > Mount /
Dismount passes the row picked in its dialog, and the Model panel's Mount card passes the dialog's row for
the mount picked in its own searchable list (`CharControl::selectMountChoice` and `dismount`, over the
player mounts of `CharControl::mountChoices`). To a player that announced protocol 5:

- `loadWoWModel` keeps naming the rider as a character, so mounting, dismounting and swapping mounts
  never send a new load; the next canvas tick sends a scene instead, because its fingerprint includes
  the mount;
- the scene gains an optional `mount`: `key` ("M" and a host serial raised for every mount model the
  mount choice installs -- never its address, which the next mount can reuse -- and kept when the
  same mount is described again), the mount's `fileDataID`, `path` and `displayID` (0 for a creature
  file), the rider node's `attachmentId` with the `bone` and `position` the MOUNT's attachment lookup
  gives it (`WoWModel::setupAtt`; -1 and zero when the mount has no such entry, which puts the rider at
  the mount's origin), `riderScale`, the mount's own `textures` (a slot bound to nothing, GL name 0 or
  none, is left out rather than read as the rider's body composite), `submeshCount` /
  `submeshVisible`, `particleColorSets` while its display replaces particle colours (three sets for
  emitter indices 11-13, start, mid and end RGB, 27 bytes, the alpha left to the emitter as the host
  does), and the `sequenceIndex` and `riderSequenceIndex` each model plays when the scene is built --
  the rider's being what the mount choice selected from its animation lookup, read back, not looked up
  again. Presence is a non-empty `key`, never the object's absence; a scene without `mount` while one
  was ridden is the dismount;
- `characterSceneApplied` adds `mountKey`, `mountStatus` (`"applied"`, `"failed"`, `"none"`) and
  `mountReason`; answers are still matched by load and revision, and a failed mount is logged and
  never turns the character into "load failed";
- no `modelSkin` or `modelGeosets` is sent for the ridden mount: its display state has one channel,
  the scene;
- `modelAnimation` adds `role` (`"mount"` or `"rider"`: whichever of the two `g_selModel` is -- the
  mount after the mount choice, the rider once View > Attachments picks it; any other pick is not
  pushed) and `load`; `modelAnimationState` keeps the mount in its top-level fields and adds `load`,
  `hasRider` and `rider { sequenceIndex, playing, timeMs, speed, loop }`, sampled in the same call.
  The rider's `playing` is the mount's: the canvas tick stops the whole tree's time when the canvas
  model is paused and never reads the rider's own flag, which the mount choice leaves set;
- `runtimeState` answers carry `mountFileDataID`, `mountKey`, `liveMounts`, `mountsBuilt`, `mountSeat`,
  `mountSeatBone`, `modelSequence`, `mountSequence`, `mountEmitters`, `mountRibbons`, `mountParticles` and
  `bodyRebinds`.

A player older than protocol 5 is sent exactly what it was before (the mount as the loaded model,
behind the "Mounted character" notice). Every mount the character rides is logged once, whatever the
player can do with it (`[unity-mount] the character ... rides M<n> <path> fileDataID=... bone=...
position=(...) riderScale=... sequenceIndex=... riderSequenceIndex=...`), and so is every scene that
carries one.
While mounted, an equipment slot pick or an item level change refreshes the rider (it used to refresh
the mount's empty equipment, so the item appeared only at the rider's next refresh), and the Geosets
tab no longer refuses a change to the rider's parts behind no notice.

**Mounted characters: the player side (protocol 5).** The player announces protocol 5 and keeps the
character exactly as it is: the model on screen and its dresser stay the rider's, with everything it
wears. The mount a scene describes is a second, separately animated model, held by
`WmvMountedScene` (`WmvMountedScene.cs`), and the character's body root hangs from it in the Unity
hierarchy, so the rider and its equipment follow the animated mount bone with no per-frame copying:

- **Presence** is the sentinel -- a non-empty `key` and a `fileDataID` above 0 -- never a null test:
  the lifecycle self-test parses a scene line without `mount`, and `JsonUtility` hands the absent
  object back as a default-filled one (key null, fileDataID 0), not as null. A key with no FileDataID
  is answered `failed`;
- **the mount is built beside the character.** Its .m2, skeleton (and the skeleton's parent), the
  `.anim` of the sequence the host's mount plays, its first skin profile and the textures the scene's
  slots name are fetched under request ids the mounted scene owns, then built standalone -- skeleton,
  bind poses, animator, emitters, posed bounds -- and kept inactive. A description of the same key and
  file again (a newer revision, or the host re-sending after its wait) keeps what was fetched, parsed
  or built; a new key starts over, beside the mount still on screen. Textures bind by the host's slot:
  a slot the host bound to nothing is not listed and binds nothing. The geoset flags go to the build
  (or, for the same mount described again, through the triangle arrays it keeps), and
  `particleColorSets` to the mount's emitters (`WmvEmitterRuntime.SetParticleColorOverride`);
- **one frame.** The character's dresser applies a scene only when its own parts are ready AND its
  commit gate says so (`WmvCharacterDresser.CommitGate`): for a scene with a mount, once the mount is
  built or has failed for good and the keys of the character's `riderSequenceIndex` are here (fetched
  into the character's slot when they live in a `.anim`). A mount file arriving pumps the dresser
  again. In the frame the scene is applied (`OnCommitted`) the mount is activated, the character is
  seated, the character switches to `riderSequenceIndex` -- the host's own result, applied as given --
  and both clocks start where the host's are (see "the animation of the two models" below). A scene
  without a mount commits exactly as before;
- **the seat**, from the host's resolved attachment: `bone` below 0 (the mount's table has no entry, as
  on the Whelpling) puts the body root on the mount's root at zero; a bone the build has a Transform
  for puts it under that bone at the attachment position less the bone's pivot, both converted -- the
  same arithmetic as an attached item -- which reproduces the host's bone matrix times the attachment's
  translation; a bone the build has no Transform for (a mount drawn as a static mesh) puts it on the
  mount's root at the converted position. Rotation is the identity, the scale `riderScale` on all three
  axes, and the case is logged (`mount: M<n>: the character hangs from attachment 0 -- ...`);
- **swap, dismount, failure.** A new key moves the character onto the new mount before the old one is
  disposed; a scene without a mount takes the character off (`SetParent(null)`, identity local
  transform) and disposes the mount, leaving the character, its dresser and its animation alone; a
  mount that cannot be built leaves the character on screen off any mount and is answered `failed` with
  the reason, never as a failed character;
- **a character loaded while it rides** (a player restarted while mounted, or the host sending the load
  again) builds its scene's mount for that load (`LoadJob.Mount`, beside `LoadJob.Dresser`): the mount
  on screen goes with the character it carries when the new character is adopted, and only then does
  the load's mount take its place and receive the new character, in the same frame. A load that is
  superseded or fails releases its mount and every request out for it;
- **disposal order**, everywhere a mount goes -- a dismount, a swap, a new model, a world model, a new
  character, a dropped load, shutdown: the character is taken off first, so a mount's root is never
  destroyed with the character under it;
- `characterSceneApplied` carries the mount's `mountKey`, and `mountStatus` `applied`, `failed` or `none`
  (no mount, or the scene was not applied); `runtimeState` carries `mountFileDataID`, the mount the
  character on screen rides (0 for none), with the mount's key, the mount runtimes alive and built, the
  seat, both animators' sequences, the mount's emitters and the character's body texture binds.

The lifecycle self-test (`-wmvLifecycleTest`) checks the joints on synthetic models: a skinned body
parented under an animated, turning bone of another model bakes to the same vertices in its own space
as at the origin and, in the world, to the at-origin bake carried through the parent bone and its
offset (two instants, two scales); a scene line with and without a mount; the three seats; and the
mounted scene's fetch, build, commit, swap, dismount, failure, disposal and a load's own mount,
answered by a stand-in asset channel, with the live model count back where it started. Framing, the
capture hooks and the measurements are in "Mounted characters: framing, capture and measurement" below.

**Mounted characters: the animation of the two models (protocol 5).** The character and its mount
animate on their own clocks -- one `WmvM2Animator` each, over each model's own slot and sequence caches
-- and never share a frame counter; the global-sequence clock still moves once per frame. The host's
pushes say which of the two they are about, and the player routes them (`WmvSlotAnimation.cs`, which
holds the sequence code every slot runs):

- **`modelAnimation` with a `role`.** `"rider"` is the character -- the one on screen, or the one being
  loaded when `load` is that load's serial -- and plays or waits as any selection for it does;
  `"mount"` is the mount the character rides on screen. The role decides, never the FileDataID (a mount
  model can also be a playable race's body); the FileDataID only has to name the file of the model the
  role picks. A mount role while the character rides no mount of that file -- the push the mount choice
  sends a tick before the scene that carries the mount -- is ignored and logged; one naming a mount still
  being prepared, for the character on screen or for a load, is kept and switched to in the frame that
  mount goes on. A push without a role is about the model it names, as before, with one exception: the
  character's own selection while it rides on screen is the host taking it off its mount, sent a tick
  before the scene without the mount, and is held and played in the frame that scene is applied, so the
  character's idle starts on the ground rather than on the mount's back;
- **`modelAnimationState` with `hasRider`** is applied in one pass: the top level to the mount's animator
  (when a mount of that file is on screen), the nested `rider` to the character's, each exactly as any
  state is applied (explicit positions as given, heartbeats held to the dead band, play/pause and speed
  as sent). The rider's `playing` is taken as sent -- the mount's pause, which on the host stops the whole
  tree, while the rider's own animation manager is not consulted. For a character being loaded, the
  rider's half waits for its animator as any load's state does;
- **the clocks when a mount goes on.** The mount starts from the newest ridden state naming its file
  (sampled after the host put that mount on), projected to now. The character switches to
  `riderSequenceIndex` and starts from a state for that sequence sent after the scene first describing the
  mount arrived -- a character loaded while it rides, or a heartbeat during a long build -- or, without
  one, in step with the mount at its own speed: the host set both at their first frames in the one mount
  choice and runs both on the same ticks. A state from before that scene describes the clip the character
  played before, and only its play/pause and speed carry over; a selection made for the character after
  the scene is kept;
- **later switches** of either model use that model's slot: its cached tracks, its `.anim` files (the
  mount's are fetched up front in the frame it goes on, as the character's are at its load) and its
  fetches. A switch on one model never re-binds or moves the other's clock;
- `-wmvAnimTime` poses the mount and then the character in the frame a mount goes on or the character
  comes off, so the character's billboard bones take their facing under the posed mount bone.

The lifecycle self-test checks the wire lines (a selection and a state with and without the rider fields),
every routing case (no role, the dismount hold, a mount and a rider built from one file, a load in flight,
a mount being prepared, pushes for a character that is not there), one ridden state applied to two slots
built from one file, a frame advancing each clock at its own speed and pause, `.anim`-keyed switches on
each model leaving the other's sequence, clock, caches and fetches alone, a cached switch asking for
nothing, the start rules and the pinned pose. Known gap: the host's first canvas tick after a mount
choice charges the choice's own wall time (loading the mount model and its display records) to both of
its clocks, and no message carries that time, so the first heartbeat after mounting corrects both player
clocks together by about that much (measured headless: about 600 ms on the Warhorse, 125 ms on the Highland
Drake).

**Mounted characters: framing, capture and measurement (protocol 5).** Once the character's body root
hangs from a mount's bone its box is no longer a box in the world, so the view is fitted to both models as
one world box (`WmvMountedScene.UnionBounds`): the mount's box carried through its root, and the
character's body box carried corner by corner through `Body.Root.localToWorldMatrix` -- the mount bone as
posed at that instant, the seat's offset and the rider scale -- with nothing allocated.

- **When.** In the frame the mount under the character changes (`WmvMain.FrameRiddenScene`): a mount goes
  on, a swap puts another in its place, the character comes off (a dismount, or a new mount that could not
  be built taking it off the one it rode), and for a character loaded while it rides, in the frame its
  mount goes on, right after the character's own framing. The camera is framed by the usual rule
  (`WmvOrbitCamera.Frame`) and the shadow window takes the same box; after a dismount both are fitted to the
  character alone again. A newer description of the same mount -- an appearance or equipment change while
  mounted -- keeps the view, and nothing is framed per frame. `-wmvFrameBounds` still pins the box. The
  archived viewport kept the character's framing when a mount went on, so a large mount ran out of the
  view; here the mount is framed with the character. The camera controls are unchanged.
- **What the box does not follow.** It is measured once, from the character's box (the posed range of the
  sequence it was built with) and the mount's (the posed range of the sequence it went on with).
  Measured with `-wmvMountCheck` over the frames after each change: on the Warhorse Walk the mount's
  drawn bounds reach at most 0.62 past the box (radius 2.90) and the character's 0.20; on the Golden
  Gryphon's Fly and MountFlightIdle the gryphon's drawn bounds reach 3.8 past the box (radius 3.16) and the
  character's box carried through its root 1.2; on the Whelpling the seated character's drawn bounds reach
  0.97 past it (radius 2.02); on the Brutosaur, Highland Drake, Argent Charger, Little Red Riding Goat,
  Bloodfang Widow and Grand Expedition Yak less than 0.6. The view is not fitted again as the animation
  goes on.
- **The capture hooks and probes cover both models.** `WMV_VIEWPORT_SHOT` captures 40 frames after the
  mount under the character changes, as after a load (once, when a character is adopted and seated in the
  same frame), and again after a sequence switch or a newer scene while it rides; it waits for the commit and
  for the clip it was asked for, and its log line adds the mount's emitter counts, the seat, both models'
  clocks and the camera ("Capture hooks" below). `WMV_VIEWPORT_ORBIT` re-aims the camera
  after this framing too. `-wmvAllocCheck` starts a window ten frames after the change, waits while a mount
  is still being prepared, and reports the mount's animator, material bindings and emitters beside the
  character's (measured: 12-16 KB of managed heap per 60 frames and no gen-0 collection with the Warhorse,
  the Flameward Hippogryph and its 35 emitters, the Brutosaur or the Highland Drake under the character --
  the same as the character alone). `-wmvLightCheck` measures the box of both models, one frame after the
  change: in the frame of a swap the mount it replaced is only destroyed at the end of the frame and was
  drawn into the check too.
- **The order of the LateUpdates** (`-wmvMountCheck`, `WmvMountProbe.cs`). In every measured frame Unity ran
  the character's animator, and the animators of what it wears, before the mount's, and the shadow rig
  before every animator. What follows the mount bone through the hierarchy when the frame is drawn cannot
  lag -- skinning, attachments, particle and ribbon positions -- so a late mount costs only what those
  LateUpdates compute from world transforms: the world facing of billboard bones, the billboard axes of the
  character's and its items' particles, and the shadow map, which already lags every animated model by a
  frame. Measured with the equipped reference character: the mount's attachment bone turns at most 24
  degrees a second on the Warhorse Walk, 35 on the Gryphon's Fly and 24 on its MountFlightIdle, so the
  facing lags by at most 0.6 degrees at 60 frames a second (0.2 at the headless run's ~550), and the shadow
  map places the character's farthest bone at most 0.04 units behind; with the mount posed before every
  other LateUpdate (`-wmvMountCheck=N:mountfirst`) all of it is 0. The larger single-frame steps are the
  mount's own pose jumps -- its first frame on, a heartbeat's clock correction -- not motion. The measured
  body (humanmale_hd) has no billboard bone. No ordering was added.
- **The transparent draw order between the two models.** Each model ranks its blended batches from queue
  3000 and its emitters sit at 3900, so the two models' transparent draws share one band. Drawn offscreen
  from the viewport's pose with the mount's blended batches wholly before the character's and wholly after,
  and the two models' emitters likewise, the Flameward Hippogryph, Argent Charger, Highland Drake, Whelpling
  and Bloodfang Widow under the equipped reference character changed no pixel from four views; the controls
  (either model's transparent materials drawn before the opaque ones, run on two of the views) changed up to
  2,107 pixels, so the check sees what the queues decide. No queue was changed.

The lifecycle self-test checks the framing on synthetic models: a box under a moved, turned and scaled
parent against its eight corners carried by hand, a skinned character on the turned, moved bone of an
animated mount framed with the mount (not the two boxes in their own spaces), at another instant of the
mount's clock and under a moved mount root, the character alone once it is off, and the same through a
mounted scene's commit and dismount.

**World models (WMOs).** A WMO picked in Browse stays in the Unity viewport too, as **static
geometry**. This is the foundation stage, and it is deliberately narrow:

- **What is drawn:** every full-detail (LOD0) group of the root, found through the root's GFID
  chunk -- the first MOHD group-count entries, never the later LOD blocks and never a group file
  name -- with one mesh per group and a submesh per render batch, in the same coordinate
  conversion as M2 models.
- **Materials follow a material plan, drawn by a world-model shader.** Every MOMT entry is
  turned into a plan by `Wow/WmoMaterialSemantics.cs` (pure C#, covered by the parser tests) and
  drawn by `Resources/WmvWmo.shader` ("WMV/Map Object"), never by the M2 combiner shader. The
  shader copies the preview light rig of `WmvOpaque.shader` code for code, so a world model is lit
  exactly like a model and `WmvOpaque.shader` itself is untouched. Established so far, from the
  client's own map-object shader: **shader ids 0 and 16** (diffuse = texture slot +0x0C on MOTV set
  1), **blend 0** (opaque, depth write) and **blend 1** (the same plus a discard below 128/255 on
  that texture's alpha), flag **0x04** (two-sided), flags **0x40 / 0x80** (clamp texture addressing
  on U / V) and flag **0x01** on those ids (the preview light is not applied). An id-0/16 material
  whose +0x0C is empty is `unresolved: U-23b` -- its whole surface would be whatever the client binds
  to an unused register -- and drawn white as a labelled **provisional fallback**. **Shader id 23**
  (the modern four-layer material) is drawn as the client's pixel case 23 without its
  emissive: layers +0x18/+0x24/+0x28/+0x2C, each with the alpha of its height map
  +0x30/+0x34/+0x38/+0x3C, layer k and height k on MOTV set k; per-vertex weights from MOC2 (bytes 2,
  1, 0 for layers 1-3, layer 4 the remainder), each times its height, sharpened against the largest
  and normalised; never alpha-tested. An empty layer gets weight 0, which equals the client only where
  its stored weight is 0, so such a material also carries U-23b. Its +0x0C is an env map for an
  emissive that is **not drawn** (camera-space axes, sampler addressing, fade and presence in the
  client's program not established, U-E2/U-E4/U-E3/U-P1), is never
  drawn as a diffuse and is not decoded, and the client's pull of the diffuse toward an unknown colour
  by MOC2 byte 3 is not applied (U-23a) -- so every id-23 material is at best `resolved-partial`. One
  with a layer but no height map for it is `unresolved: U-23b` and drawn by the same arithmetic with
  the missing height reading 1, labelled a provisional fallback. **Shader id 13** (two-layer opaque) is drawn as the client's pixel case
  13: +0x0C on MOTV set 1 and +0x18 on MOTV set 2, lerped per vertex by the alpha of MOCV colour set 2
  (the stored byte / 255, no fix-up; 1 draws +0x0C, 0 draws +0x18); its alpha is 1, so blend 1 is never
  alpha-tested. One with an empty +0x0C or +0x18 is `unresolved: U-23b` (what the client binds to an
  unused register is unknown) and drawn by the same arithmetic with that register white, labelled a
  provisional fallback. **Shader id 4** (opaque) is the client's pixel case 4: +0x0C on MOTV set 1,
  its alpha never read or tested; with +0x0C empty it is the same labelled fallback as id 0. **Shader id 7** (two-layer env metal) draws the diffuse part of the
  client's pixel case 7, which is case 13's lerp of +0x0C and +0x18 by the set-2 alpha, never
  alpha-tested; **shader id 5** (env metal) draws the diffuse part of case 5, which is case 4's +0x0C
  with its alpha (a reflectivity mask) unread. Their env emissives -- the diffuse times its alpha times
  the env map (+0x24 for id 7, +0x18 for id 5) on a generated coordinate, added after light -- are
  **not drawn** and those env maps are not decoded: the client's coordinate generator (U-G1), the
  camera axes a reflection or planar coordinate depends on (U-E2), the env sampler's addressing (U-E4),
  the distance fade (U-E3) and whether the program the client picks for the batch adds the emissive at
  all (U-P1: a permutation without it, the single-texture map-object family, or the edge selector that
  tints or replaces it) are not established, so every id-7 and id-5 material is `resolved-partial:
  U-G1,U-E2,U-E3,U-E4,U-P1`, and F_UNLIT is not honoured on them (U-F1). Id 23's emissive (its +0x0C on
  a per-pixel sphere map, times the weighted layer colour and alpha) carries U-E2, U-E3, U-E4 and U-P1
  the same way. **These emissives stay undrawn by decision**: their client equations are logged, pinned
  by the tests and shown by the `envmask` view, and nothing more. An id-7 material with an empty +0x0C or +0x18
  is `unresolved: U-23b` and drawn by the same labelled fallback as id 13, and an id-5 material with an
  empty +0x0C by the fallback of id 4. Ids 4, 5, 7, 13 and 23 with blend 2 or above are `unresolved:
  U-B2..U-B5` and keep their case arithmetic in a provisional fallback drawn opaque (no blend factors
  are applied). Every other material is drawn by the **provisional** archived baseline -- slot +0x0C
  or white, a non-zero blend as the 128/255 key, flag 0x04 for culling -- and says why: `unresolved`
  with the research reason codes for blend values 2 and above on ids 0 and 16 (U-B2..U-B5: no blend
  factors are applied), for id 23 without any layer texture (U-23b; drawn white, its env map not
  bound) and for ids outside the plan (OUT-OF-PLAN). **Blend 2 and above also stays provisional by
  decision**, drawn exactly as before; what is logged for it is the row of the client's blend-state
  table (the four factors the 12.1 executable's read-only data holds for that EGxBlend index) with its
  evidence level. That MOMT value n selects row n is documented only for older clients: not contradicted
  for 2, contested for 3 and above in 12.1, which adds `U-B7`; `U-B1` is added only past the table's 17
  rows. MOC2 is uploaded (UV channel 4) only for groups a
  four-layer material draws in, and the MOCV set-2 alpha (UV channel 5) only for groups an id-13 or id-7
  material draws in; where a group lacks a stream such a material reads (MOC2, MOCV set 2 or a MOTV
  set) it draws a stated default, logs the group, and the material gains `U-V4` with the vertex count
  (a resolved one becomes `resolved-partial`). The F_UNLIT light bypass (ids 0, 4, 13, 16) is kept
  wherever it is drawn, but older-client documentation honours the flag only for exterior-lit batches,
  so a material the load finds drawn in an interior group (MOGP flag 0x2000) gains `U-F3` with the batch
  count (a resolved one becomes `resolved-partial`). MOCV colour set 1, the set-2 RGB and MOMT colours are kept
  but not used.
- **One line per material** in the player log: its index, shader id, blend value, flags, every
  non-zero texture slot with what became of the file, the permutation and sampler bindings the plan
  chose, the render state, and a verdict -- `resolved`, `resolved-partial` (an input the client reads
  has no established source, e.g. an env emissive: U-G1,U-E2,U-E3,U-E4,U-P1) or `unresolved` (the
  baseline or a labelled fallback) -- with its reason codes and notes: for blend 2 and above the client
  blend row with its evidence level, for ids 5, 7 and 23 the emissive's client equation, the coordinate
  it would need and every open input, and U-F3 with its interior batch count.
- **Textures.** Every non-zero texture FileDataID of the root is fetched once, after the groups.
  Only the textures a drawn material's plan samples are decoded -- once per FileDataID, on worker
  threads, a few at a time; every other file is fetched and its BLP header checked. Decoding them
  all cost the Blood Elf tower 67 s of CPU and 380 MB of heap for the 19 images it draws, and an
  86-group cave 2,550 s of CPU and 2.2 GB for one. Uploads are shared per (file, U addressing, V
  addressing) and keep the alpha channel, because the world-model shader reads alpha only in the terms
  a material's plan enables; they are GPU-only, with no CPU-readable copy.
- **Framing.** From the drawn geometry's bounds, as a model is, but from a three-quarter view 30
  degrees above the horizon (the audit's OpenGL reference view). The far clip plane reaches past
  the far side of the whole object at every zoom, so there is no fixed ceiling like the archived
  far plane of 6400, and the wheel zooms in to 0.1 % of the framing distance; loading a model
  restores a model's framing, zoom range and clip planes. The cast-shadow map keeps its 4096 texels
  over the whole object, so on a very large WMO its shadows are coarse (about 7 units a texel at a
  10,000-unit radius); the light rig itself is unchanged.
- **Diagnostic only:** `-wmvWmoVertexColour` multiplies MOCV colour set 1 into every world-model
  material; normal rendering never uses vertex colours. `-wmvWmoMaterialDiag` (also through
  `WMV_DEBUG`) adds one `wmo material-diag` line per drawn material: shader id, blend, flags, every
  texture FileDataID, the permutation, each sampler's role, slot and UV channel, the vertex-colour
  use, the realised Src/Dst for colour and alpha, ZWrite, the alpha test, cull, addressing, queue, the
  light bypass; for blend 2 and above the client's EGxBlend row (`client blend: ...`, its four factors
  with the evidence level: the factor row is the client's own, the value-to-row hop older-client
  documentation, contested from 3 up, U-B7) beside the realised state it is not applied to; for ids 5,
  7 and 23 the emissive that is not drawn -- its client equation, its mask and the texture coordinates
  the mask assumes, the env map's FileDataID ("not decoded, not drawn"), what is known of the env
  coordinate and the codes that keep it undrawn; how many of the material's batches sit in interior
  groups; the resolution with its reason codes, what each sampled file became, and the created
  material read back. It also adds one `wmo batch g<group>.b<MOBA index> range A|B|C -> submesh k ->
  material m (shader, blend, queue, ZWrite, verdict)` line per drawn batch, with the queue and depth
  write read back from the material that draws it, so an ordering or blend question can be checked
  against the object that draws. For a four-layer batch also its mean stored MOC2 weights and how many of its
  vertices carry a non-zero byte 3 (U-23a) or a weight on an empty layer (U-23b), and the UV span of
  each layer's MOTV set over the vertices that weight it (a span of 0 samples one texel); for a two-layer
  batch its set-2 alpha range and mean, how many vertices show layer 2 or weight an empty slot, and
  how far MOTV set 2 differs from set 1 there. It only logs.
  Verification views, all unlit and off by default: `-wmvWmoView=plan` draws each material flat in
  the colour of how it is drawn (red archived baseline, green diffuse, blue four-layer, orange
  two-layer, yellow two-layer env metal (id 7), cyan opaque, white env metal (id 5), magenta a
  provisional fallback of those); `-wmvWmoView=weights` / `=blend`
  draw a four-layer material's stored MOC2 weights / its effective weights after the height blend
  (red, green, blue = layers 1-3, black = layer 4); `=va` an id-13 or id-7 material's set-2 alpha as grey;
  `=diffuse` every material's combiner diffuse; `=t0` / `=t1` the first / second register as sampled;
  `=envmask` the emissive mask of ids 5, 7 and 23 -- the factor the client multiplies each env map by:
  t0.rgb * t0.a (id 5), c.rgb * c.a with c the rgba lerp of the two layers (id 7), mix.rgb * mix.a with
  mix the weighted layers (id 23) -- on the texture coordinates the diffuse uses, which assume the
  client's cb0[5].y override replaces none of them (U-G1); the env map itself is never bound or sampled
  (its coordinate, addressing, fade and presence are U-G1/U-E2, U-E4, U-E3, U-P1), every other material
  draws dark grey, and the build summary names the view;
  `-wmvWmoUvOverride=N` makes every four-layer and two-layer register read UV channel N (the swap test
  for per-layer UV sets); `-wmvWmoOnlyMaterials=a:b:c` draws only those MOMT entries.
- **What is not drawn yet:** doodads (so the doodad-set choice in Model > Appearance has no visible
  effect, and the panel says so), liquids, WMO lights, fog, portal culling, LOD switching and the
  skybox.
- **The host sends no geometry.** It sends the root's FileDataID (`loadWoWModel` with
  `"kind":"wmo"`), and the player fetches the root, its LOD0 group files and the material textures
  with `getAssetByFileDataID`, then answers `mapObjectLoaded` (see the IPC section). No skin,
  animation or geoset message is ever sent about a WMO.
- **The host keeps only the root's metadata.** Selecting a WMO used to open every group file,
  compile it into an OpenGL display list for the hidden canvas, upload the material textures to GL,
  and then do all of it a second time. Now the host parses the root chunks alone (`WMO` with
  `metadataOnly`): counts, bounds, materials, group info, GFID, doodad sets and placements, lights,
  fog and portals -- what Model > Info, the doodad-set list and the status bar read. No group file is
  opened, no display list is built and no texture is uploaded for a selection. No host feature
  needs a WMO's group geometry today (no exporter reads a WMO); one that does must build its own
  full `WMO` on demand.
- **Switching is clean on the host.** A WMO is deleted only through `ModelCanvas::ClearWMO`, which
  first detaches the canvas root and clears `g_selWMO`: both used to be left pointing at the freed
  WMO, so the next load of anything (or application exit) wrote into freed memory. The counts a
  root that fails to open reports are now zero rather than uninitialised, and a WMO picked again
  after another model gets its doodad-set list applied again.

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
- **It owns what is loaded:** the model and its attachments (`canvas->root`, `model()`), a WMO (its
  root metadata only; see "World models") or a map tile. An image picked in Browse is no longer loaded at all: the pick is only remembered,
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

## IPC (implemented; protocol 5)

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
{ "type": "unityReady", "protocolVersion": 5 }
{ "type": "getAsset", "requestId": "abc123", "path": "creature/chicken/chicken.m2" }
{ "type": "getAssetByFileDataID", "requestId": "abc124", "fileDataID": 123456 }
{ "type": "getModelTextures", "requestId": "abc125", "fileDataID": 123200 }
{ "type": "mapObjectLoaded", "fileDataID": 115058, "load": 1, "status": "built", "reason": "",
  "groups": 1, "groupFilesRequested": 1, "groupFilesMissing": 0, "batches": 3, "submeshes": 3,
  "renderers": 1, "materials": 3, "provisionalMaterials": 0, "unresolvedMaterials": 0,
  "blendedMaterials": 0, "texturesReferenced": 3, "texturesDecoded": 3, "texturesMissing": 0,
  "vertices": 946, "triangles": 854, "boundsMin": [-11.75, -10.41, -12.54], "boundsMax": [11.72, 13.60, 11.85],
  "timings": { "rootMs": 45.8, "groupsMs": 41.7, "texturesMs": 47.0, "buildMs": 17.6, "totalMs": 158.9 },
  "liveMapObjects": 1, "liveModels": 0 }
{ "type": "runtimeState", "query": 3, "liveMapObjects": 1, "liveModels": 0, "modelFileDataID": 0,
  "mapObjectFileDataID": 115058, "loading": false, "mountFileDataID": 0, "mountKey": "", "liveMounts": 0,
  "mountsBuilt": 0, "mountSeat": -1, "mountSeatBone": -1, "modelSequence": -1, "mountSequence": -1,
  "mountEmitters": 0, "mountRibbons": 0, "mountParticles": 0, "bodyRebinds": 0, "viewFramings": 1 }
{ "type": "characterSceneApplied", "fileDataID": 1011653, "revision": 4, "load": 12, "status": "applied",
  "reason": "", "merged": 3, "attachments": 4, "missing": [], "ms": 212,
  "mountKey": "M3", "mountStatus": "applied", "mountReason": "" }
```

(The `mapObjectLoaded` line is a logged report of a headless run on `it_trollhouse03.wmo`, bounds
rounded to two decimals as the host logs them.)

`mapObjectLoaded` answers each world-model load once: `"built"`, `"failed"` (with `reason`) or
`"superseded"` (a newer load of either kind replaced it first -- not a failure). `load` is the serial
of the `loadWoWModel` it answers, so a late answer about an earlier load is never taken for the one
on display. The counts describe what was built (bounds in Unity space, timings in milliseconds):
`materials` is the root's MOMT count; of the entries LOD0 batches draw, `provisionalMaterials` counts
those drawn provisionally (by the archived baseline or a labelled fallback), `unresolvedMaterials`
those with any open question (verdict `resolved-partial` or `unresolved`), and `blendedMaterials`
those with a non-zero MOMT blend value; `vertices` counts every group vertex uploaded and
`triangles` only the batch triangles; `texturesDecoded` counts decoded images only (the files some
drawn material's plan samples), and `texturesMissing` a fetch, decode or header check that failed; `groupsMs`
runs from the group requests to the last group parsed and `texturesMs` from the texture requests
(sent once the groups are parsed) to the last texture landed.
`liveMapObjects` and `liveModels` are the world models and models (a character's parts included)
built and not yet disposed in the player once the outcome was adopted, which is how a lifecycle
test proves that nothing leaked or doubled. The host logs every
field (`[unityipc] <- mapObjectLoaded ...`); a count the player leaves out reads -1. A `"failed"`
report about the WMO on display puts the "World model could not be built" notice up.

`runtimeState` (protocol 4) answers the host's `runtimeState { query }` question with what the player
holds at that moment: `liveMapObjects` and `liveModels` as above, `modelFileDataID` and
`mapObjectFileDataID` of the model and the world model on screen (0 for none), and whether a load of
either kind is in flight (`loading`); `query` echoes the question's number. From protocol 5 it also
carries `mountFileDataID`, the mount the model on screen rides (0 for none; the host logs -1 when the
player does not send it), and what a lifecycle test needs to tell that mount from a stale or doubled one:
`mountKey` ("" for none), `liveMounts` (mount runtimes alive, one built for a scene not applied yet
included), `mountsBuilt` since the player started, `mountSeat` and `mountSeatBone` (how the model hangs
from it: 0 at the mount's origin, 1 under that bone, 2 on the mount's root at the attachment's position;
-1 not seated), `modelSequence` and `mountSequence` (what each animator plays), `mountEmitters`,
`mountRibbons` and `mountParticles` (the mount's emitters as drawn and its live particles),
`bodyRebinds` (how often a character's scenes bound its body textures again) and `viewFramings` (how
often the player fitted the view to what is on screen -- once for each model and world model put up,
once for each change of the mount under a character, and never for anything else, so a test can hold a
step to "the camera did not move"). The mount fields of `characterSceneApplied` (protocol 5) are described under
"Mounted characters: the host side" above. The player answers in
message order on its main thread, so a question sent after an answer about a build sees that build
adopted. Only the headless self-test asks it (see the lifecycle sequence below), and only a player that
announced protocol 4.

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
{ "type": "loadWoWModel", "path": "creature/chicken/chicken.m2", "fileDataID": 123200, "client": "active",
  "character": false, "load": 12, "kind": "m2" }
{ "type": "loadWoWModel", "path": "world/wmo/northrend/buildings/icetroll/it_trollhouse03.wmo",
  "fileDataID": 115058, "client": "active", "character": false, "load": 13, "kind": "wmo" }
{ "type": "runtimeState", "query": 3 }
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
{ "type": "modelAnimation", "fileDataID": 126407, "sequenceIndex": 1, "animID": 0, "durationMs": 4000,
  "loop": true, "role": "mount", "load": 12 }
{ "type": "modelAnimationState", "fileDataID": 126407, "sequenceIndex": 1, "playing": true, "timeMs": 1840,
  "speed": 1.0, "loop": true, "explicitState": false, "load": 12, "hasRider": true,
  "rider": { "sequenceIndex": 145, "playing": true, "timeMs": 840, "speed": 1.0, "loop": true } }
```

(The last two are a ridden mount's pushes, protocol 5: see "Mounted characters: the host side".)

Semantics:

- `unityReady` is answered by a `loadWoWModel` for whatever model is loaded (and every later
  model load pushes a new one). `client` is `"active"` -- the player never chooses a client;
  WMV's active client/profile is the only data source.
- `kind` says what `loadWoWModel` names: `"m2"` (a model; also what an absent field means) or
  `"wmo"` (a world-model ROOT, by FileDataID). A WMO load goes only to a player that announced
  protocol 4 or later; an older player gets the out-of-date notice instead, and ignores the field
  for models, which is what it assumed anyway. The player fetches the root, then the LOD0 group
  files the root's GFID names (the first MOHD group-count entries), then every non-zero material
  texture, all with `getAssetByFileDataID`, and answers `mapObjectLoaded`. A new load of either kind
  replaces a world-model load still in flight, and the old runtime is destroyed only when the new
  one is ready.
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
  The player keeps those caches per model, in the model's slot (`WmvModelSlot`), together with the
  .m2 bytes, the selection, the `.anim` files and the app's last playback state; the code that
  selects, switches and plays a sequence (`WmvSlotAnimation`) acts on the slot it is given -- the model
  on screen's, or a ridden mount's -- and a new load empties the slot's caches.
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
- **A ridden mount's pushes (protocol 5)** carry `role` and `load` on the selection, and `load`,
  `hasRider` and the nested `rider` on the state; the player applies the top level to the mount and
  the rider to the character, each on its own clock, and never tells the two apart by FileDataID. See
  "Mounted characters: the animation of the two models" above.
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
least one composited `characterImage` was sent.

**World models in the self-test.** `wowmodelviewer.exe -dbfromfile -wmo 115058 -unityipctest` (a root
listfile path works too) selects the WMO through `FileControl::SelectWMOFile`, the code a pick under
Browse's WMO filter runs, and its **world-model check** waits for the player's `mapObjectLoaded` for
that load, logs every field, and fails unless it is `"built"`, names the root, built the root
header's group count with no group file missing, and left the player holding exactly one world model
and no model (`liveMapObjects` 1, `liveModels` 0); the host side must also have loaded no group
geometry and must point the canvas root and `g_selWMO` at the WMO. A player older than protocol 4
fails this check (and gets the out-of-date notice).

**Lifecycle sequence (opt-in).** With `WMV_IPCTEST_SEQUENCE` set, after every other check the test
selects each entry exactly as Browse does and checks the outcome, for example
`WMV_IPCTEST_SEQUENCE="m2:creature/bear/bear.m2;wmo:115058;m2:creature/bear/bear.m2;wmo:108538;wmo:248820"`.
Entries are `m2:` or `wmo:` followed by a listfile path or a FileDataID; the mounted-character steps
below act on the character already on the canvas and load nothing:

- a `wmo:` step passes the same checks as the world-model check for its own load, with no notice up,
  and the player's `runtimeState` answer afterwards must name the root as the world model on screen,
  no model, `liveMapObjects` 1 and `liveModels` 0;
- an `m2:` step requires the host to hold the model and no WMO (`canvas->wmo`, `g_selWMO` and the
  canvas root all cleared), the player to confirm the model is built where it can -- a character by
  its scene answer for the load, any other model with geosets by answering a geoset state for its
  FileDataID `"applied"` -- and then, for every model, the player's `runtimeState` answer to name the
  model as the one on screen with no world model and no mount alive, `liveMapObjects` 0 and `liveModels` 1
  (a character: at least 1, its parts count too). That answer is the only evidence that the step left no
  world model alive: the next `wmo:` step cannot stand in for it, because adopting a world model disposes any model
  and any world model still alive before the counts in `mapObjectLoaded` are taken;
- `m2!:` / `wmo!:` is a quick step: selected and left at once, so the next load replaces one still
  in flight; the next waited step also requires an answer for each quick world-model load
  (`"superseded"`, or `"built"` if it finished first -- never `"failed"` or none). A sequence that
  ends on a quick step fails, since nothing would check it;
- no step may produce a `"failed"` world-model report.

**Mounted characters in the sequence (protocol 5).** These steps act through the menus, panels and dialogs a
user acts through. Each one requires the player's answer to the scene it caused (when it causes one) and the
`runtimeState` account afterwards to match what the host shows: the character on screen with no load in
flight, the host's mount under it by key and file with exactly one mount runtime alive, the seat the host's
resolved attachment gives (at the mount's origin when the mount has no such attachment, otherwise under the
named bone), both animators on the host's sequences, the mount's emitters those the mount declares -- with
live particles while the clock runs -- and no notice up. No step may send an ordinary `modelSkin` or
`modelGeosets` either: what a ridden mount displays travels in the character's scene and nowhere else, so a
second channel for the same state fails the step that opened it.

- `chr:<file.chr>` loads a saved character (`ModelViewer::LoadChar`); its own scene answers, riding nothing;
- `mount:<displayId>` picks the mount dialog's row for that `CreatureDisplayInfo` id the way the dialog picks
  it (`CharControl::fillMountChoices`, then `OnUpdateItem(UPDATE_MOUNT, row)`): the host raises its mount
  serial, keeps the display and sends no load; the scene is answered with the new key `applied`; the player
  built exactly one mount more, holds one alive, fitted the view exactly once (to the mount and the character
  together), and the character's runtimes, body texture binds and composited images are as they were;
- `dismount` picks the dialog's `---- None ----` row: the scene is answered with mount `none`, no mount
  runtime is alive, none was built, the view was fitted once to the character alone and the character is
  unchanged; with nothing ridden the choice changes nothing, no scene follows and the view is not fitted;
- `manim:<animId>` picks the mount's first sequence with that animation id in the Animation panel, moving the
  panel onto the mount first when it is on the character, as `View > Attachments` moves it; `ranim:<animId>`
  does the same for the character, and `#<n>` names sequence n instead. The other model must stay on its own
  sequence, no mount may be built and the view must not be fitted again;
- `equip:<slot>=<item>` picks an equipment slot's item (`OnUpdateItem(UPDATE_ITEM)`; item 0 takes it off),
  `custom:<option>[=<choice>]` sets an Appearance choice (`CharDetails::set`, by default the option's next
  choice) and `sheath` toggles `Character > Sheathe weapons`: each must reach the player in a scene that keeps
  the mount's key, with no mount built and the view not fitted again, and the step line reports the
  character's runtimes, body texture binds and composited images before and after (sheathing must leave all
  three as they were);
- `reconnect` restarts the player (`View > Restart Unity Renderer`): the new player's load must name the
  character as a character, its scene must answer with the same mount key, and it must hold what the player
  before it held, having built exactly the one mount and fitted the view twice -- once for the model it put on
  screen, once for the mount that went under it. The log of the player before it is kept beside the new
  one's as `unityRenderer.before-reconnect-<n>.log`;
- `wait:<ms>` pumps the host with the canvas ticking, which lets a `WMV_VIEWPORT_SHOT` capture land before
  the test ends.

For example, on `-mo character/human/male/humanmale_hd.m2`:
`WMV_IPCTEST_SEQUENCE="mount:8469;dismount;mount:8469;mount:17697;mount:83632;mount:8469;dismount"`.

Each step logs one `[unityipc-test]   step n/N ...` line with the host's state after it.

**Capture hooks (validation only).** Three environment variables let a headless run capture the real
viewport without a window on screen; unset, none of them changes anything. `WMV_VIEWPORT_SHOT=<name>`
writes `<name>.png` beside the player's data folder, 40 frames after the last thing that changed what is on
screen: a model or world model put up, the mount under a character changed, and while a character rides, a
sequence switch completed on either model or a newer scene applied to the character. A newer request moves a
capture that is waiting on, and the capture waits while a load, a character's scene, a mount being prepared or
a sequence switch waiting for its keyframe file is still on its way (at most 30 seconds, after which it is
taken and the log says what it was waiting for) -- so the image shows the mount committed and the clip asked
for, or the pose `-wmvAnimTime` pins, not a moment before them. It asks for its size again first, and frames
the last box again when that changed the camera's aspect. Its log line records the camera position, pivot,
yaw, pitch, distance, field of view and aspect, each model's clock (sequence, animation id, position, length,
playing or paused, speed) and, for a character on a mount, the mount's key and file, the seat case, its bone,
local position and scale. With it, `WMV_VIEWPORT_SIZE=<n>` (128 to 4096, default 1024) asks for an n x n
screen before anything is framed (the operating system may clamp it to the display, and the host resizes the
player to its pane again when its layout changes -- seen after a saved character was loaded, which is why a
capture asks for the size again), and `WMV_VIEWPORT_ORBIT="yaw:pitch[:distanceScale]"` re-aims the camera
after it frames a world model, a model or a character with its mount (angles in the player's orbit terms,
the distance as a multiple of the framing distance), so a capture can be taken from a named view: the WMO
audit's reference views are `135:30` and `315:30`, and the archived viewport's yaw Y and pitch P are
`180-Y:90-P` (its mount references' iso view, yaw 315 at pitch 90, is `225:0`; only the angles map -- that
camera looks at the mount's own box and has a different field of view).

The canvas animation clock of the viewport check is measured inside an
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
- Static WMO geometry (protocol 4): the LOD0 groups a root's GFID names, one mesh per group with a
  submesh per render batch, framed from the WMO's bounds, with materials from the WMO material plan
  (shader ids 0/16, 4 and 13 resolved; the diffuse parts of 5, 7 and 23 resolved-partial, their env
  emissives not drawn; blend 0/1, cull, clamp and unlit flags established; blend 2 and above, MOCV set-1
  shading and other ids provisional and logged). The host keeps only the root's metadata and sends only its FileDataID. See "World models".

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
- For characters: secondary (upper-body) and mouth animations. A mounted character rides its mount
  in a player of protocol 5 or later (see "Mounted characters"); an older player gets a notice.
- Maps, terrain, fog; BLP images picked in Browse. Each of these loads and gets a notice.
- For WMOs: doodads and doodad sets, liquids, WMO lights, fog, portal culling, LOD switching, the
  skybox, and the rest of the WMO material system (shader ids other than 0/4/5/7/13/16/23, the env-map
  emissives of ids 5, 7 and 23, the MOC2 byte-3 colour pull of id 23, blend values 2 and above, MOCV
  set-1 vertex colours). Those materials are drawn provisionally or without the missing term, and the
  player logs them as resolved-partial or unresolved with the reason codes. The env emissives stay
  undrawn and blend 2 and above provisional by decision: their open inputs (U-G1, U-E2, U-E3, U-E4,
  U-P1; U-B2..U-B5, U-B7) are per-draw client state that no available source establishes.
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
character or a WMO gets the out-of-date notice.
