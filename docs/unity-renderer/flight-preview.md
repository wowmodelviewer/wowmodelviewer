# Flight preview

The Animation panel has a **Flight** checkbox and **Flight speed** control below
the playback speed row. It previews continuous straight flight of an animated M2
model while the view follows the model. Orbit, pan and zoom remain available.

- Starts off, with speed **20.0 model units/s**, adjustable from 0 to 200.
- Play/Pause and Stop use the existing playback clock. Playback speed scales
  the flight and emitter simulation together; zero flight speed leaves the
  model's normal animation playing.
- Changing or unloading a model resets Flight to off and speed to 20.0.
  Changing its animation does not turn Flight off.
- No settings are saved and no name-based or database-based projectile
  classification is performed. Twenty is a preview default, not a claim about
  the speed of every spell in the game.
- Applies to the main M2 model's emitters. Buildings, terrain and ridden mounts
  do not offer this control. It does not simulate a complete spell, target,
  impact, attached equipment scene, acceleration or curved trajectory.
- Exports, native animation-event sidecars and consumer projects are unaffected.

## Runtime

The camera follows a straight projectile path along native -X / Unity -Z,
opposite the character-facing convention. This is the preview direction for
these spell effects, not automatic detection of every possible model's facing.
The simulation uses the corresponding moving reference frame: existing trailing
particles and ribbon edges move toward native +X / Unity +Z by
`flightSpeed * animationDelta` each
step, before new particles are emitted. The model and camera stay near the origin,
avoiding precision loss after long playback. This is translation-equivalent to
moving the model and camera together; no physical path or ground motion is drawn.

Particles carrying `DONOTTRAIL` stay attached to their emitter. Other particle
velocities, lifetimes, emission rates and ribbon lifetimes keep their authored
values. Disabling Flight stops the extra translation; existing history expires
normally. There is no extra draw call or per-particle object.

`modelAnimationState` adds optional `flight` and `flightSpeed` fields. Old hosts
leave Flight off and old players ignore these fields. Both updated components
are needed to use the control. Loading and animation-switch paths preserve the
current model's state; new runtime instances start off. Speeds are checked for
finiteness and bounded in the player as well as by the UI.

## Verification

The `-wmvLifecycleTest` suite includes numerical checks for 0, 20 and 40 units/s,
attached versus trailing particles, pause, ribbon extension, defaults, malformed
speeds and disposal, plus IPC decoding with and without the optional fields.

The hidden host's `WMV_IPCTEST_SEQUENCE` accepts `flight:20`, `flight:40` and
`flight:off`. These use the Animation panel setter and verify that the checkbox
and speed field fit inside the panel and have the correct enabled state.
Combine with `wait:1500`, `screenshot:<path>` and `m2:<path>` to check a real
model's trail and reset. Screenshot logs include Flight state and speed.
Keep extracted assets and captures outside Git.

Validated on Retail `wow` 12.1.5.70391 on 24 October 2026: Release x64 host and
Unity player built successfully; 1,104 lifecycle checks passed. Hidden real-model
runs passed for `cfx_mage_fireball_missile`, `cfx_mage_frostbolt_missile` and
`cfx_mage_arcanemissiles_missile`, at off/20/40/off and after a model reload.
Each run checked control containment/enabling and captured five frames; the
capture logs confirmed the received state and speed. Effect captures were
inspected; this is not a client-gameplay comparison or manual UI interaction.

Direction correction, 24 October 2026: Mauricio's Frostbolt check exposed the
inverted initial +X travel direction. The preview now travels along -X, with
history rebased toward +Z Unity. The numerical test asserts that the trailing
particle stays behind a projectile moving in that direction. The 1,104 checks
and all three real-model runs passed again; corrected effect captures were
inspected. An initial Arcane run failed the pre-Flight legacy canvas-clock sample
(0 -> 0) while all 16 lifecycle steps passed; its isolated repeat passed fully.
Both logs are retained in the local direction-correction evidence.

### Frostbolt ribbon geometry

Frostbolt (FileDataID 1598570, Retail 12.1.5.70391) declares five ribbon
emitters: three orbiting emitters on bones 8–10, and two central layers on
bones 11–12 using texture slots 4–5. The latter share a parent with the native-X
cylindrical billboard flag (0x10).

Ribbon width follows native +Y (Unity -X). Native +Z is the longitudinal axis;
using it as width collapses the central layers into almost zero-area triangles.
For the first resident rotation key in this model the old width's cross product
with the flight axis has magnitude 0.000061, versus approximately 1 for +Y.
This distinction agrees with the independent viewer's ribbon matrix columns:
column 1 supplies width, column 2 supplies the interpolation tangent.

The animator also handles flag 0x10 by preserving the parent's native X axis
(Unity Z) and turning the transverse frame toward the viewing direction. It
reconstructs that frame each time, avoiding cumulative rotation, and retains the
parent frame for an exactly axial view. Spherical billboard behavior is unchanged;
flags 0x20/0x40 remain unsupported. No Frostbolt-specific identifier checks or
replacement textures are involved. Existing ribbon edges retain their recorded
orientation, as they do for ordinary animated bones.

Regression coverage checks actual triangle area for a forward-facing emitter,
authored width, pause, parent rotations, axial-view fallback and application of
0x10 by the animator at four camera angles. Counting edges alone is insufficient
to detect this failure.

The opaque central layer also authors opacity 0.5. Its material now enables
alpha blending while that track is below 1, following the renderer's existing
opaque-batch fade convention; it returns to opaque blending at full opacity.
This prevents the recovered layer from painting an opaque black rectangle.
Tests cover half/full/zero opacity and replay back to half opacity.

Validation: Unity Release build and 1,135 lifecycle checks passed. Real Frostbolt
runs at yaw 0/45/90/180 passed; after the final opaque-opacity adjustment, yaw
45/90 were repeated and visually inspected. Fireball and Arcane regression runs
passed before that final adjustment; it cannot affect their paths (no ribbons
and three blend-7 ribbons respectively). No Blizzard gameplay comparison or
manual UI test was performed. Evidence: `C:/Users/mauri/WMVDev/frostbolt-ribbons-20261024/`.
The daily installation was updated with two replacements and 171 SHA-256 checks,
and WMV was reopened normally. The initial launch with `-mo` exited normally
after its headless load; that invocation was corrected to a launch without arguments.
