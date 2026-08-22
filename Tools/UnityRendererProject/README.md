# UnityRendererProject — the WMV Unity viewport player

This folder holds the **source-only** pieces of the Unity standalone player that WMV embeds
via `View → Unity Renderer`. The repo contains no Unity project boilerplate and no build
output — you create the project locally with your own Unity install and drop these
scripts in.

**Direction:** this player is WMV's **new renderer foundation and intended primary
viewport**; the OpenGL canvas remains the legacy/fallback viewport during the migration.
It renders **directly from WoW data**: it requests raw assets and metadata from WMV over
IPC (no OBJ/FBX/GLB export workflow). WMV provides the app UI, the active client/profile,
CASC/MPQ access, DB/metadata and the runtime commands; the player provides the modern
rendering pipeline. See `docs/unity-renderer/README.md`. For now the player is optional at
build and run time.

## Build steps

1. Install a Unity LTS (2021.3 or newer, including Unity 6 — the scripts use only core
   UnityEngine + .NET `TcpClient`).
2. Create a new 3D project named e.g. `WmvUnityRenderer`. **Built-in Render Pipeline is the
   simplest project to test with**, but **URP (including Unity 6) and HDRP work too** — the
   builder resolves a shader per pipeline (see *Render pipelines and shader stripping* below).
3. Copy **both** `Assets/Scripts/` and `Assets/Resources/` from here into the project's
   `Assets/`. `Assets/Resources/` is not optional: it holds the renderer's own opaque shader,
   and a Resources folder is the only place Unity will not strip it from a player build (see
   *Render pipelines and shader stripping*).
4. Create an empty GameObject in the default scene and add the **WmvMain** component to
   it. (It builds the camera rig, light, test cube, status overlay and the IPC client at
   runtime — no other scene setup needed.)
5. **Player Settings** (Edit → Project Settings → Player):
   - **Resolution and Presentation → Run In Background: ON** (required — otherwise the
     player pauses whenever the WMV window has focus, which is always).
   - Fullscreen Mode: *Windowed*.
   - **Active Input Handling: any of Old / New / Both.** The camera compiles against whichever
     backend is active (`ENABLE_LEGACY_INPUT_MANAGER` / `ENABLE_INPUT_SYSTEM`); with *Both* it
     uses the legacy path. No project setting needs changing just to avoid input exceptions.
6. **File → Build Settings → Windows x86_64 → Build**, and build **into**
   `tools\unity-renderer\` next to `wowmodelviewer.exe`, with the executable named
   `UnityRenderer.exe`.
   (Alternatively build anywhere and set `Tools/UnityRendererPath` in
   `userSettings\Config.ini` to the exe's full path.)

Do **not** commit the build output (exe, `UnityRenderer_Data/`, `UnityPlayer.dll`, …) to
this repository.

## Render pipelines and shader stripping

The renderer builds its materials at runtime, so it asks `Shader.Find` for one. Two things make
that awkward in a **player build** (as opposed to the editor):

- the shader name differs per pipeline, and
- Unity strips shaders that no asset in the build references — which is why a runtime-built
  material, or even an untouched primitive, can come out **magenta** in a player while looking
  fine in the editor.

And a third thing, the one that actually broke the first working render: **a shader is not
usable just because `Shader.Find` returned it.** `Sprites/Default` is always included, so it wins
any naive search — and it bakes `Blend One OneMinusSrcAlpha`, `ZWrite Off` and `Cull Off` into the
pass. Those are *not* material properties, so `HasProperty`-guarded calls to set `_ZWrite` or
`_SrcBlend` against it do nothing at all, in silence, and an opaque WoW material draws as a stack
of translucent shells. Every candidate is therefore screened before it is taken: it must expose
the blend/depth knobs, or already be opaque (its own default render queue says which), and it must
not be one of Unity's `Hidden/` internal shaders.

`WmvModelBuilder` tries, in order:

| | Shader | |
|---|---|---|
| 1 | lit, for the active pipeline — `Standard`, `Legacy Shaders/Diffuse`, `Mobile/Diffuse` (Built-in) or `Universal Render Pipeline/Lit`, `.../Simple Lit`, `HDRP/Lit` (SRP) | best match to the OpenGL viewport |
| 2 | the shader on a primitive's default material | pipeline-correct by construction — Unity picks it from the *active* pipeline |
| 3 | **`Resources.Load("WmvOpaque")`** — `Assets/Resources/WmvOpaque.shader` | the guarantee: a Resources folder is never stripped |
| 4 | unlit — `Unlit/Texture` (Built-in) or `Universal Render Pipeline/Unlit`, `HDRP/Unlit` (SRP) | flat, but solid |
| 5 | `Sprites/Default` | last resort only; logs that the model **will** look see-through |

The names are restricted to the pipeline in use, because a shader written for one pipeline renders
magenta under another. Rungs 1, 2 and 4 are all strippable — in a real URP player build with an
otherwise empty scene, *every one of them* came back missing or as the magenta error shader. Rung 3
is why the renderer no longer depends on that going well.

It never fails the load, and it always logs which shader it took and why. Adding the shader you
want to **Project Settings → Graphics → Always Included Shaders** and rebuilding is still the way
to get full pipeline lighting.

Three consequences worth knowing:

- The **render state is set explicitly**, never inherited from the shader's defaults — and the
  shader is chosen so that setting it actually does something.
- Transparency follows the **WoW blend mode only**, never the presence of an alpha channel in
  the texture. Creature skins have one regardless: chicken2's is DXT5 with ~97% of its texels
  below 255. That channel is **discarded at upload** unless something actually reads it — a
  combiner that masks or keys with it, or a blend mode that outputs it — rather than trusted
  because it happens to be there.
- Every material load logs its real runtime state — chosen shader, render queue, `RenderType`,
  `_SrcBlend`/`_DstBlend`/`_ZWrite`/`_Cull`/`_Mode`/`_Surface`/`_AlphaClip` (or `n/a` where the
  shader does not expose them), enabled keywords, and the WoW blend/two-sided/depth-write flags
  it came from.

## Texture units, combiners and hidden geometry

These are the parts of an M2 material that are easy to miss, and missing any of them looks like a
texturing bug rather than a missing feature:

**A batch can load more than one texture.** It declares how many (`textureCount`) and where its run
starts in the texture-combo table (`textureComboIndex`); unit *k* is entry `textureComboIndex + k`.
Reading only the first entry drops every unit past the first, silently. chicken2's two batches each
declare two units.

**The material does not describe how they combine.** That is selected by its shader id: the id plus
the texture count names a Blizzard combiner (`Wow/M2ShaderTable.cs`, the same table the OpenGL
viewport resolves). chicken2's `shaderId 0x8000` resolves to
`Combiners_Opaque_Mod2xNA_Alpha` / `Diffuse_T1_Env` — unit 1 is an **environment sphere map**
generated from the view-space normal, not a stored UV set, and the combine is

```
rgb = lerp(unit0.rgb * unit1.rgb * 2, unit0.rgb, unit0.a)
```

so the **base texture's alpha channel is the reflection mask**, not transparency. On chicken2's skin
that mask is white everywhere except the eye pupil, which is exactly where the model wants a
reflective highlight.

`WmvModelBuilder.PlanCombiner` reduces each M2 pixel shader to what this renderer can draw of it,
ported case by case from the legacy viewport's own GLSL rather than from the combiner *names* —
`Combiners_Mod_Mod2x` says nothing until you read that it is `unit0 * unit1 * 2` keyed on
`unit0.a * unit1.a * 2`. What comes out is a handful of floats on one shader variant, never a
keyword per case: a player build strips shader *variants* as readily as whole shaders, so a keyword
per material feature would be a way to lose them all.

| Combiner | Arithmetic | Pixel shaders |
|---|---|---|
| 0 | `unit0` alone | 0, 1, 10, 13, 14, 16, 20, 23, 33 |
| 1 | `unit0 * unit1` | 2, 5, 6, 11, 36 |
| 2 | `unit0 * unit1 * 2` | 3, 4, 7, 9 |
| 3 | `lerp(unit0 * unit1, unit0, unit0.a)` | 22 |
| 4 | `lerp(unit0, unit1, unit1.a)` — a decal | 29 |
| 12 | `lerp(unit0 * unit1 * 2, unit0, unit0.a)` | 12, 15 |

One thing makes that table much shorter than the shader list looks: several combiners differ from a
simpler one only in a **specular lobe**, and the legacy viewport multiplies that lobe by a weight
that is **zero** unless an opt-in environment variable is set. Reproducing its default means
dropping the lobe too, which collapses those cases onto plain single-texture colour (8, 10, 13, 14,
16, 20, 23) or onto combiner 12 (15). Pixel shaders 8 and 21 still bind unit 1 anyway — their
*alpha* reads it even though their colour does not.

Anything outside the table is logged by name once per material and drawn from unit 0 alone, which is
what happened to all of them before. A combiner also needs the renderer's own `WmvOpaque` shader —
a pipeline Lit shader has one texture slot and no idea what a sphere-mapped second unit is — so on
those the second unit is dropped and the log says so. `-wmvOwnShader` forces the renderer's own
shader if you want to see it.

**Where unit 1 samples is the vertex shader's business, not the material's.** The same table names
it per unit: `T1`/`T2` are the mesh's two stored UV sets, `Env` is a sphere map generated from the
view-space normal. Over a spread of 300 retail creature models (1424 batches), 30.6% of batches
route unit 1 to `Env` and 21.9% to the second UV set, so reading either one as "the first UV set"
is wrong most of the time.

**The blend mode is not a suggestion either.** `ApplyBlendMode` sets `_SrcBlend`/`_DstBlend`/
`_ZWrite`/`_Cutoff` from the same switch the legacy viewport uses (`ModelRenderPass::init`), and two
of its readings are counter-intuitive. Mode 0 sets a blend function but never enables blending, and
mode 1 is `One`/`Zero` with an alpha test — both are opaque, which is why they were already right
before any of this. Depth write comes from the material's own `0x10` flag and **nothing else**: the
viewport sets it from one unconditional test *outside* that switch, so a blended pass whose flag is
clear still writes depth. "This mode blends, therefore no depth write" reads perfectly reasonably
and diverges on every such pass. The alpha test keys at `128/255`, the value the combiner actually
compares against, not a rounded half.

**A model hides geometry by keying an animation track to zero, not by leaving it out.** An eye
overlay, a glow, a blink. The OpenGL viewport refuses to draw a batch whose colour entry resolves to
zero alpha (`ModelRenderPass::init`), and so does this renderer. chicken2 is the worked example: it
ships an 18-triangle eye overlay pointing at a colour entry whose alpha track is 0, while the actual
eye is painted into the skin texture on the head underneath. Drawing that batch anyway covers the
painted eye with a flat red patch. `-wmvShowHidden` draws them if you want to see what is hidden.

**And a creature variant can change the geometry, not just the texture.** Each submesh carries a
geoset number (group * 100 + variant, masked to 15 bits); a submesh numbered 0 is always drawn, and
any other is drawn only when the displayed variant switches that number on. `creature/horse3/horse3.m2`
has three dropdown entries sharing one texture that differ only in whether geoset 101, 102 or 103 is
on -- a long mane and tail, or a cropped one. WMV sends the set; switching between variants swaps
which submeshes hand the mesh their triangles, so nothing is re-uploaded and no material is rebuilt.

## Skinning and the bind pose

A model is drawn through a `SkinnedMeshRenderer` when it carries a rig this renderer can
reproduce. No animation track is evaluated yet: every bone sits in its rest pose, and the point of
the milestone is that this is **indistinguishable from the static mesh it replaces**.

That is not a hope, it is a property of the format. The legacy viewport composes a bone as

```
local = T(pivot) * T(translation) * R(rotation) * S(scale) * T(-pivot)
world = parent.world * local
```

(`Bone::calcMatrix`) and skins a vertex as the weighted sum of `world * position` over its four
influences. With every track at rest that local matrix collapses to the **identity** — so the
positions stored in the file already *are* the rest pose, and a bind pose only has to reproduce
the identity. Each Unity bone is therefore placed at its pivot with no rotation and no scale, and
the bind poses are read back off those same transforms:

```
bone.localPosition = pivot - parentPivot
bindposes[i]       = bones[i].worldToLocalMatrix * renderer.localToWorldMatrix
```

The same arrangement is what animation will need. Unity composes a bone as
`parent.world * T(localPosition) * R(localRotation) * S(localScale)`, so adding the M2's
translation track to that rest `localPosition` and setting the rotation and scale from their
tracks reproduces the expression above term for term — the pivot translations telescope through
the parent chain. Nothing here will need re-deriving to make the model move.

Four things about the data are worth stating, because getting any of them wrong stays invisible
until the model deforms:

**The per-vertex bone indices are direct indices into the bone array.** There is no lookup table
in between. The `.skin` format does carry one, for its own batching purposes, and reading the
vertex indices through it produces a rig that looks plausible and skins the wrong vertices.

**Weights are `uint8`/255 and are renormalised here.** An influence pointing past the end of the
bone array is dropped, as the legacy viewport drops it — but the remaining weights are then
rescaled to sum to one rather than left short, because a vertex whose weights no longer sum to one
is dragged toward the origin. A vertex left with no influence at all is bound to bone 0 at full
weight, which at rest is the identity and so leaves it exactly where the file put it. Across the
validation models neither correction fired: every weight set summed to 255 and every index was in
range.

**A rig is a forest, not a tree.** A creature routinely has a dozen bones with no parent —
`creature/valkier` has 27 of 149 — so there is no single root to hang everything from. Parent
indices are sanitised at parse time: out of range, self-referential, or part of a cycle all become
roots, because a cycle is both an infinite parent walk and a transform Unity refuses to parent
inside its own descendants.

**Where the bones live.** Almost always in the `.m2` itself. A model can instead name a separate
skeleton file through its `SKID` chunk, and that skeleton can defer again to a parent through
`SKPD`; over a spread of 300 retail creature models, 299 kept their bones in the `.m2` and one
used an `SKPD` parent. This milestone does not fetch skeleton files, so a model that names one is
drawn as a static mesh and the log says exactly that — as it does for any other model the plan
refuses. Reading the header's bone array anyway would be worse than not skinning: the vertices are
not indexed against it.

## Debug switches

Pass these on the player command line to isolate a visual fault without rebuilding. They are also
read from the **`WMV_DEBUG`** environment variable (space-separated, same spellings), which is how
to reach them in the embedded viewport — WMV builds the player's command line itself, and a child
process inherits the environment:

| Flag | Effect |
|---|---|
| `-wmvFlipV` | invert the V texture coordinate — isolates a UV-orientation fault |
| `-wmvForceOpaque` | force every material opaque, ignoring the WoW blend mode |
| `-wmvForceSolid` | force-opaque **plus** a fully opaque alpha channel on every uploaded texture. If the model is still see-through with this on, the fault is geometry, depth or winding — not alpha |
| `-wmvMatColors` | replace textures with a flat per-material colour — separates a batch/material assignment fault from a texture fault |
| `-wmvShowHidden` | draw the batches the model hides at rest — shows *what* is hidden, and usually what it was covering |
| `-wmvOwnShader` | resolve the renderer's own `WmvOpaque` shader ahead of any pipeline shader — the only one that can run an M2 combiner |
| `-wmvNoSkin` | build every model as a static mesh, as before skinning existed — the A/B for "did the skinned path change what I see?" |
| `-wmvSkinCheck` | bake the skinned result and report the largest distance between a baked vertex and the position the file gave it. At rest that distance is the milestone's whole claim, so it is measurable rather than asserted |

Each model load also logs its UV sample, per-batch material/blend/texture-slot mapping, the alpha
treatment per texture, which batches are hidden at rest and why, the resolved combiner and per-unit
UV routing, the full runtime material state, the shader that was chosen, and the rig it was skinned
to (or why it was not).

## Scripts

| File | Role |
|---|---|
| `WmvMain.cs` | Bootstrap (camera rig, light, placeholder, status overlay) and the load pipeline: on `loadWoWModel` it fetches the .m2, parses it, fetches the .skin profile named by SFID, resolves and fetches textures, builds the mesh and frames the camera. |
| `WmvIpcClient.cs` | IPC client (protocol v1): connects back to the WMV server given by `-wmvPort`, sends `unityReady`, receives `loadWoWModel` and `modelSkin`, sends `getAsset` / `getAssetByFileDataID` / `getModelTextures`, decodes + hash-checks `assetResponse`. |
| `WmvModelBuilder.cs` | Parsed model + skin + decoded textures -> Unity `Mesh` (one submesh per WoW batch), `Material` and `Texture2D`; owns and disposes those runtime resources so repeated loads do not leak. `RebindTextures` re-uploads the textures behind the materials it already made, for when WMV's selected skin changes. |
| `Wow/M2Parser.cs` | Chunked M2 (MD21/MD20 v272): header, vertices, textures, materials, lookups, SFID/TXID. |
| `Wow/M2SkinParser.cs` | .skin profile: vertex lookup, triangles, submeshes, batches, and the two-level index resolution into model vertices. |
| `Wow/BlpDecoder.cs` | BLP2 -> RGBA32 in memory (palettized, DXT1/3/5, raw BGRA). |
| `Wow/M2ShaderTable.cs` | Shader id + texture count -> combiner name/id and per-unit UV routing. Pure data, no rendering; the same table the OpenGL viewport resolves. |
| `Wow/WowCoordinateConverter.cs` | The single WoW -> Unity axis/winding/UV conversion. |
| `Assets/Resources/WmvOpaque.shader` | The renderer's own textured shader, kept under `Resources/` so a player build cannot strip it. One variant, everything uniform-driven: `_SrcBlend`/`_DstBlend`/`_ZWrite`/`_Cull`/`_Cutoff` as real properties, plus `_CombinerMode`, `_Unit1UV`, `_AlphaMode`/`_AlphaScale` and `_OpaqueAlpha` for the second texture unit and the M2 combiners. |
| `Wow/ByteCursor.cs` | Bounds-checked little-endian reader shared by the parsers. |
| `WmvOrbitCamera.cs` | Orbit / pan / zoom controls plus bounds-driven framing of a loaded model. |

The parsing layer under `Assets/Scripts/Wow/` deliberately has no `UnityEngine` dependency, so
it can be compiled and unit-tested outside the editor -- see `Tests/WowParserTests.cs`, which is
framework-free (`WowParserTests.RunAll()` returns a failure count) and covers valid/truncated/
out-of-range M2 and skin data, the BLP encodings and the coordinate conversion.

## How embedding works

WMV launches the player with the standard standalone-player embedding arguments:

```
UnityRenderer.exe -parentHWND <decimal hwnd> delayed -logFile <...>\userSettings\unityRenderer.log -wmvPort <port>
```

The player creates its window as a child of the given WMV panel; WMV resizes that child
window whenever the pane resizes and sends it `WM_CLOSE` on shutdown.

## IPC

**WMV is the server.** It listens on `127.0.0.1` (ephemeral port) before launching the player
and passes `-wmvPort <n>` on the command line; `WmvIpcClient` connects back, sends
`unityReady { protocolVersion: 1 }`, and then requests raw WoW files with `getAsset` /
`getAssetByFileDataID` (answered by `assetResponse` with base64 bytes + SHA-1). Newline-delimited
JSON; full vocabulary and semantics in `docs/unity-renderer/README.md`. Run the player without
`-wmvPort` and it runs standalone (test scene, no WMV connection).

## TestStub

`TestStub/` contains a small Win32 program that honours the same contracts as the player:
the `-parentHWND` embedding (child window, fills the parent, exits on `WM_CLOSE`) AND the v1
IPC (connects to `-wmvPort`, sends `unityReady`, answers `loadWoWModel` with `getAsset`, logs
and displays the `assetResponse` byte length / SHA-1 / decode check). It also accepts
`-wmvSelfTest`, which WMV passes **only** for its `-unityipctest` diagnostic run: the stub then
additionally probes the negative paths (the same asset by FileDataID, a missing path, an
unknown message type). Without that flag -- i.e. every normal `View -> Unity Renderer` launch
-- it makes exactly one request per model and the viewport shows only the real exchange.
Build it with any MSVC prompt:

```
cl fake_unity_renderer.c user32.lib gdi32.lib shell32.lib ws2_32.lib /Fe:UnityRenderer.exe
```

Drop the result at `tools\unity-renderer\UnityRenderer.exe` to test the WMV side without
installing Unity -- interactively via `View -> Unity Renderer`, or headlessly with
`wowmodelviewer.exe -mo creature/chicken2/chicken2.m2 -unityipctest` (logs `[unityipc-test]
RESULT: PASS|FAIL` in `userSettings\log.txt`; the stub own log is
`userSettings\unityRenderer.log`). `creature/chicken2/chicken2.m2` is the primary target
because it is a current, database-backed creature; `creature/chicken/chicken.m2` is kept as a
regression case for the labelled `convention` texture fallback -- no creature display
references it any more.
