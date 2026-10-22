# World viewer 10 -- light, sky and fog (outdoor)

Branch `world/10-light-sky-fog`, from `develop` 678071f0 (Stage 09, PR #127). **Status: the owner approved the
direction on 2026-10-21 with the adjustments of section 0; implementation runs in the order of section 7. Steps 1-5 are
done and the early visual checkpoint ran on 2026-10-22 with a built player (13.1); the owner reviews its pictures
before steps 6-9.** This document records the owner's decisions (section 0), the investigation (sections 1-5), the
architecture and order of work (sections 6-8), the validation against in-game screenshots (section 9), the remaining
decisions (section 10) and, as work lands, its results. The roadmap change that made this milestone 10 is `roadmap.md` 5.1.

## 0. The owner's decisions (2026-10-21)

The owner reviewed the investigation and the plan, agreed with the direction, and asked for these adjustments before
implementation:

1. **Exterior world-model lighting.** Investigate the client's own shader semantics for exterior MOCV (255 of 300 Retail
   exterior groups carry it). If the behaviour can be reliably established, bring exterior MOCV into Stage 10; interior
   lighting, MOLT, interior ambient and interior fog stay in Stage 12. No guessed vertex-colour multipliers. (Replaces
   the plan's D3 recommendation; the investigation's result is in 2.2.)
2. **Shadows.** Full shadow rendering stays out of Stage 10, but the architecture must allow adding shadows later
   without rebuilding the lighting system (6.15), and the validation documents and demonstrates the visible difference
   that missing shadows cause. A result is never classed as visually equivalent to the game where shadowing causes a
   significant difference (9.6).
3. **References, simplified.** No calibration procedures and no hours of mandatory screenshots before the first results.
   A small number of Horde-friendly captures around the Southfury River comes first; further spots only when a question
   needs them. The helper addon records the metadata automatically (9.2). The PTR and Retail run different shader
   branches (4.0): the lighting-mode difference is resolved before PTR screenshots are treated as Retail's, and where an
   exact Retail comparison is not available the uncertainty is documented, never assumed away. Classic Era and MoP Classic
   get functional and data-driven validation even without live screenshots; their visual parity stays marked unverified.
4. **Order of work, for earlier visual results** (section 7): client-data mappings and shader interpretation; a
   reproducible baseline with the light off; time-of-day evaluation and the outdoor light state; an initial sky for review;
   terrain lighting with MCNR normals; outdoor world models and doodads including verified exterior MOCV; fog; water;
   validation on every client. The detailed work packages stay as supporting documentation, not as prerequisites of the
   early prototype. Protocol-20 separation, the evidence rules and the Stage 09 guarantees stay.
5. **Scope and quality.** Reproduce the game's rendering as faithfully as the evidence permits: no generic Unity
   lighting, no artistic adjustments, no speculative constants. `WMV_LIGHT=off` stays a byte-identical Stage 09 fallback.
   Models, Buildings and Characters stay as they are, including the owner's separate tabard fixes. The water tuning is
   not changed silently. Clouds, advanced skybox effects, local interior lights and other unresolved systems stay scoped
   out; no unrelated features. The project's budgets, regression tests and evidence grades apply.
6. **Next step.** Update this plan, then implement from the smallest independently testable foundation up to the early
   visual checkpoint, and show the first sky and outdoor-light comparisons before any extensive visual tuning. No push,
   no merge, no Stage 11; completed work stays on this branch.

The other decisions of section 10 follow its recommendations unless this section changes them. D1 (consent for outside
material, as hypotheses only, under the no-copy rule of 6.1) is recorded as given with the owner's approval of the plan
on 2026-10-21; if withdrawn, public column names revert to positional names and every public value to a measurement.

Evidence grades, as in `09-liquids.md`: **PROVEN_BYTES** (decided by the client's own files), **PROVEN_RUNTIME**
(observed in the running viewer or player), **PROVEN_SOURCE** (read in this repository's source), **INFERRED** (follows
from proven facts, or taken from public documentation and consistent with the bytes, but not observed directly),
**MEASURED** (read from in-game pictures, with the method stated), **UNKNOWN**. In this document a rule taken from
public documentation alone, and not yet checked against the bytes or the game, is marked **INFERRED (public)**.

**Outside sources.** The audit's risk R4 allows table definitions from outside the repository only with the owner's
consent; the owner gave it for Stage 09 on 2026-10-18. This investigation read, as hypotheses: public table
definitions, public format documentation, and the source code of public renderers (one of the modern client, one
reimplementation of an older client checked against that client's executable, and an older map editor). Nothing from
them is relied on until the bytes or an in-game comparison confirm it. No code and no constant table is copied from them:
every value the viewer uses is re-derived from the client's bytes (its tables, and the shader bytecode it ships, as the
model viewer's M2 light path already was), or measured in game. Every new table definition adapted from public
definitions gets its notice in `THIRD-PARTY-NOTICES.txt`, as Stage 09's did. The owner is asked to confirm this consent
for Stage 10 (section 10, D1).

Clients (one installation, as updated on 2026-10-21):

| Client | Product | Build | Role here |
|---|---|---|---|
| Retail | wow | 12.1.5.70448 | viewer target |
| PTR | wowt | 12.1.7.70424 | the owner's sandbox for in-game references (teleport, clock) |
| Classic Era | wow_classic_era | 1.15.9.70003 | viewer target; live realm only |
| MoP Classic | wow_classic | 5.5.4.70531 | viewer target; live realm only; light tables byte-identical to 5.5.4.70268 |
| Classic Beta | wow_classic_beta | 1.60.1.70450 | viewer target; a sandbox too |

The investigation's scripts and outputs are in `D:\wmv-10\survey\` (not in the repository): six surveys (`deps`,
`catalog`, `player`, `data`, `research`, `validation`) and an independent check of each (`verify-*`, own readers written
from the formats, fresh extracts from the installed clients). Their reports are `D:\wmv-10\survey\_results\*.md`. Where a
check corrected a survey, this document uses the correction.

## 1. The roadmap change

Milestones 10 and 12 were swapped on 2026-10-21 at the owner's request (`roadmap.md` 5.1): 10 is now light, sky and
fog (outdoor), 12 world-model interior lighting; 11 (world effects) and 13 (far terrain) keep their content and gain
10 as an entry. The old row 12's entry, "U16 settled per client", becomes this stage's first technical work (work
packages 2 and 3, section 7), because U16 is only half settled (2.8).

## 2. Dependencies and complications of the new order

### 2.1 How the world view is lit today (PROVEN_SOURCE)

| Part | Shader | Light today | Fog |
|---|---|---|---|
| Terrain (default Textured view) | `Wmv/TerrainTextured` | unlit: the height-mixed layers x MCCV/127, saturated (`WmvTerrainTextured.shader:418-462`). Chunks the material cannot draw use `Wmv/Terrain`, lit by a diagnostic Lambert 0.75 x (0.30 + 0.70 sat(n . key)) (`WmvTerrain.shader:74-78`); the "Textured, diagnostic light" mode applies the same Lambert to the composite | none |
| Terrain normals | `TerrainMesh.cs:28-36` | derived from the geometry, not MCNR (audit U9), one-sided at tile edges | -- |
| World models (exterior and interior groups alike) | `WMV/Map Object` (`WmvWmo.shader`), the Buildings materials | the model viewer's preview rig: ambient bands (zenith 0.60, horizon 0.90, ground 1.05) x (0.7 + 0.4 wrap), key 0.59 x sat(n . k) with the key fixed to world up in the world view, plus a camera-relative front fill 0.2065, so walls change with the camera's heading (`WmvWmo.shader:146-166, 577-650`; `WmvShadowRig.cs:59, 158-177, 343-350`). Up-facing surfaces reach 1.25 x albedo, unclamped | none |
| Doodads | `WMV/Opaque Textured` (`WmvOpaque.shader`), static | the same preview rig | none |
| Liquids | `Wmv/Liquid` | unlit; the zone light's water colours at 12:00, slot 0; reflection misses take `SkyBand1Color` at 12:00 (`WmvLiquidLight.cs`, `LiquidLights.cs`) | none |
| Background | camera clear | the Models viewport background (default #19191E, or View > Swap Background Color); no sky (`WmvMain.cs:2597-2634`) | -- |
| Particles | `Wmv/Particle` | not in the world yet (Stage 11); unlit; `Fog { Mode Off }` has no effect in this pipeline | none |
| Post | URP | HDR FP16, tonemapping off, LDR grading LUT, bloom on (threshold 1.0, knee 0.5 linear: sRGB byte 188 is the first above it; intensity 0.25) (`WmvMain.cs:2489-2595`; the local project's volume) | -- |

Parsed but not used in shading: WMO group exterior/interior flags (0x8 / 0x2000), MOCV set 1 (only a diagnostic switch;
the set-2 alpha is used as a layer factor), MOHD ambient, MFOG, WMO material F_UNFOGGED, M2 material NoFog
(`WmoModel.cs:62-68, 361-373`; `WmvWmoBuilder.cs:583-599, 631-632`; `WmoMaterialSemantics.cs:89-92, 472-473`;
`M2Model.cs:172`).

Every Wmv shader is driven by uniforms, never by keywords, because the player build strips variants. Unity's built-in
fog (keyword variants, one colour, Unity's own equations) and its Light components cannot reach these shaders. The
sky, light and fog of this stage are therefore the project's own uniforms and shader code, driven by the client's data:
which is also the owner's requirement.

### 2.2 World models: the small foundation Stage 12 builds on

Retail census of the audit's Stormwind window (PROVEN_BYTES; `D:\wmv-10\survey\deps`, corrected by `verify-deps`):

- 531 LOD0 groups in 52 complete roots; exactly one of MOGP 0x8 (exterior, 300) and 0x2000 (interior, 231) on every
  group. Batch range A is non-empty only in interior groups (84 of 231).
- MOCV set 1 is on 255 of 300 exterior groups (mean RGB about 30, 44 % black, alpha 255 on 99.99 % of vertices) and on 224
  of 231 interior groups (alpha mean 157).
- The builder makes one material per MOMT entry for all groups (`WmvWmoBuilder.cs:622-631`), and 166 of 1,010 drawn MOMT
  entries (in 23 of 52 roots) are drawn by both kinds of group. World instances share the prototype's materials.
- MODR lists 11,470 MODD records; 6 are listed by both kinds of group. 89 % of the model doodad references are in
  interior groups. Doodad prototypes are one per M2 per scene, and 246 of the 1,047 M2s listed by interior groups also
  stand on the terrain.

Consequences:

- **The foundation.** Stage 10 must not light interiors with the outdoor light. It needs an outdoor or indoor context
  per world-model group renderer (from MOGP 0x8 / 0x2000) and per doodad instance (outdoor for MDDF placements; from MODR
  membership for model doodads), carried per renderer or as per-(material, context) variants on world prototypes, never
  per material. With it: exterior groups and outdoor doodads take the outdoor light and fog; interior groups and
  interior doodads keep today's look until Stage 12 (proposed; the alternative is the outdoor light everywhere, D2);
  F_UNFOGGED and NoFog are honoured. That is the only Stage 12 work this stage needs (INFERRED from the census and the
  code). Groups flagged 0x2000 together with 0x40 (public: "exterior-lit") exist in Retail; how many, per client, is
  counted in work package 2, and they follow the rule the owner picks for exterior MOCV (D3).
- **Clearly Stage 12:** MOCV in either kind of group, the A-range (blend) batches, MOHD and MAVG/MAVD ambient, MOLT,
  MOLR, MNLD, MFOG and the group fog ids, MODD colours, F_EXTLIGHT, F_SIDN, F_WINDOW, the camera inside a model (portals,
  BSP), and the map's placed lights in `_lgt.wdt` (3.7).
- **Exterior MOCV: investigated 2026-10-21 (step 1), verdict "partly established"; not in Stage 10.** The client's
  world-model programs (`material3_wmo` 5446718/5446713 and `uber` 2977272/2977862, both engine branches, every program
  disassembled and executed on 14,976 input cases, then re-checked independently) fix how MOCV can enter the shading
  (PROVEN_BYTES): the vertex program passes 2 x MOCV.rgb and 1 - MOCV.a; the pixel program adds E = 2 x MOCV x k to the
  three hemisphere colours before the wrap when a per-batch lighting mode is 2, and multiplies the albedo by MOCV x 2 only
  when a CPU gate is on (otherwise by the neutral 0.5 x 2). Exterior MOCV alpha is 255 on 99.993 % of vertices. The
  census figure "mean about 30" included 77,019 never-drawn vertices filled with (127, 127, 127); drawn exterior MOCV
  averages (10, 11, 9) and 52.8 % of it is black, which rules out the tint path for exterior batches (INFERRED). What
  cannot be read: the per-batch mode, the gate, which program draws ordinary batches, and the scale k (1 only in
  `material3`; in `uber` a constant times, on Retail, a per-instance factor; on the PTR/Beta branch a further factor).
  So including it would need a constant not taken from bytes: **exterior MOCV stays out of Stage 10, as the owner's rule
  requires**, recorded as a known difference (its size: the median drawn exterior MOCV is 0, but if the batches use
  lighting mode 2 with k = 1 it would add +0.12 to +0.18 x albedo at the 75th percentile of drawn exterior MOCV and +0.35
  to +0.52 x albedo at the 95th, down-facing to up-facing surfaces; it is not small everywhere: Orgrimmar's `orgrimmar2`
  carries MOCV on 69 of its 74 exterior groups, per-model 95th percentile 49, maximum 165. Step 6 draws exterior groups
  with E = 0, the client's exact mode-1 output, equal to mode 2 wherever MOCV is black;
  `D:\wmv-10\step6\wmo\out\run_subset_summary.txt`, `session_wmo_census.txt`). One measurement could settle k for one client branch: the Ratchet warehouse's lantern wall
  (group 107919, MOCV up to (71, 32, 0)) at 12:00 and 00:00 on live Retail or MoP Classic (a PTR measurement does not
  carry over). The validation keeps to MOCV-free exteriors (the Crossroads) and classes MOCV-carrying exteriors (Razor
  Hill's burrow, Ratchet's warehouse) as B. Evidence: `D:\wmv-10\step1\mocv`, `...\mocv-verify`.
- **The interim look.** Until Stage 12, a building's outside takes the game's outdoor light while its inside keeps the
  preview rig; a doorway can show the step. The capture log and the Maps panel say so.

### 2.3 Doodads

Outdoor doodads need the scene's ambient, direct light, light direction and fog. They are drawn by the shader the
Models and Characters viewers share, so the outdoor path is a separate branch behind a world-only uniform, whose
arithmetic is unchanged when off, verified with the guard pictures (2.10). M2 materials flagged unlit keep lum 1 today and
keep it under the outdoor light (INFERRED (public): unlit means unlit in the game too).

**As built (step 6, 2026-10-22).** The "separate branch behind a world-only uniform" became 6.6's include split: the shared
shaders keep their programs token for token (offline compile check, 6.6), and the outdoor path lives only in the world-lit
variants, bound to world renderers while the light is shown. The Models, Buildings and Characters viewers draw the original
programs and never hold a copy.

### 2.4 Stage 09 (liquids): affected, not changed

- **The light reaches the player only inside the liquids offer** (`"liquids".lights`, protocol 19), host-sampled at
  12:00, slot 0, rounded per channel, and its `values` exist only for lights whose four water colours are not all 0
  (`LightCatalog.cpp:560-591`; `LiquidLights.cs:258-270`). That rule is water-specific. Lights without water colours
  still carry the other colours in their day: Classic Beta 309 of 309, Era 98 of 98, MoP 53 of 53, Retail 320 of 321
  (PROVEN_BYTES). Under it, no position on Classic Beta's maps 0 and 1 would get an outdoor light (their default and every
  zone light lack water colours).
- **Recommendation:** a separate protocol-20 light offer for the outdoor light; `liquids.lights`, its 256 KiB bound and
  the exact key-set checks (`app.cpp:9136-9148, 9625-9634, 9654-9691`) stay as they are. A protocol-19 player keeps its
  exact messages.
- **What changes on screen with the light on:** the water's colours follow the time of day, fog covers the water, and a
  reflection that misses the scene can take the drawn sky instead of the noon `SkyBand1Color`. The water model and its
  26 tuned values stay. The High look was tuned on an unlit bed under a flat background; its class B differences
  (`09-liquids.md` 12.10) are re-measured in this stage, and a retune is a follow-up only if the owner approves (D5).
- **What stays byte-identical:** with the new light switched off, every Stage 09 picture (the 48 regression viewports,
  the camera views of 13.2, the depth views, the four High cameras, the Era/MoP/Beta spot views, `WMV_WATER=low`). With
  the light on, the world view changes by design; nothing outside it may.
- **Noon rounding.** 86 Retail LightParams days have no key at 12:00. Where the water keeps the host's noon object, its
  rounding stays the host's; where it reads the player's day evaluation, it moves by design.

### 2.5 Stage 11 (world effects) gains

Emitters are unlit and unfogged today (`WmvParticle.shader:63`; nothing in `WmvEmitterRuntime.cs`). With 10 first they
take its fog function and light from the start, with blend-mode fog colours (additive towards black, modulate towards
white or grey: INFERRED (public)). One overlap: the client's skybox models are animated M2s (3.5); drawing them needs one
animated model in the world, which this stage keeps skybox-only so that Stage 11 is not begun (D7).

### 2.6 Stage 13 (far terrain), the streaming edge and the far plane

- The streamed region is 3 x 3 tiles (`STREAM_RADIUS` 1, keep 2; `UnityIpcServer.h:612-614`); the free camera's far
  plane is 3,000 yd. Resident terrain reaches at least 533 yd in every direction, 566-1,391 yd along the stands' facings
  (PROVEN_BYTES geometry). The world's edge is visible today, and a drawn sky changes its contrast.
- Whether the client's fog hides that edge is not settled: under the public modern fog equation (4.6) Retail's fog
  starts at min(far clip, 3000) x FogScaler, 450 yd at the Durotar stand; Era and MoP fog distances depend on FogEnd's
  unit (yards or yards x 36, 4.6).
- Two design rules keep Stage 13 free: the sky is drawn as a background at the far depth (never as geometry inside the
  far plane), and fog is computed from the view distance, not from depth-buffer fractions. Moving the far plane in
  Stage 13 then does not change Stage 10's picture.

### 2.7 Terrain normals

Lighting the terrain exposes its diagnostic normals. On the four Southfury tiles the two one-sided normals at shared
edge vertices differ by a mean of 6.4-11.4 deg and up to 61-90 deg: under a Lambert key up to 124 steps of 255 along
the seams (PROVEN_BYTES, `verify-player`). MCNR decoded by the public layout (145 x 3 signed bytes / 127) matches the
interior geometry normals within a mean of 1.95 deg and is continuous across all four seams (max 0.64 deg). So U9 looks
settleable from the bytes, and the client's terrain vertex program takes its normal from the vertex data (4.0). The plan
adopts MCNR with its own proof (work package 2); re-accumulating normals across loaded neighbours is the fallback.

### 2.8 U16: what is settled and what is open

| Item | State |
|---|---|
| Light, LightParams, LightData, ZoneLight, ZoneLightPoint layouts per client | settled in Stage 09 (verified layouts; read only from them) |
| Light position, falloff, map, the 8 LightParams slots; ZoneLight polygons and Z bands | settled (yards: INFERRED from the range) |
| LightData time, the four water colours; LightParams' four water alphas | settled |
| SkyBand1Color | chosen by measurement at one spot as the reflection sky; that it is a sky band is INFERRED (public) |
| Every other LightData column (direct, ambient, five sky colours, sun, clouds, shadow opacity, fog, colour grading), their units | open: names INFERRED (public), shapes PROVEN_BYTES (3.2) |
| LightParams skybox, glow, flags, sun overrides | open (3.3) |
| LightSkybox, LightDataGlobalVolumeFog (Beta) | no definition in the repository (3.5, 3.4) |
| `_lgt.wdt`, `_fogs.wdt` | chunk census only; content structure now read (3.7) |
| How the client selects, blends and times lights | INFERRED (Stage 09 rules); public alternatives differ (4.2) |
| Any sky, fog or outdoor-light comparison in game | none |

### 2.9 Complications, by severity

| # | Severity | Complication | How it is avoided |
|---|---|---|---|
| 1 | high | Shared shaders and globals vs the byte-identical guards (Models, Buildings, Characters with the owner's tabard fixes, the Stage 07 battery) | a separate outdoor branch behind a world-only uniform; globals set only for the world view's camera and reset for every other camera (the water grab's pattern); the guard pictures rerun; fallback: world-only shader variants |
| 2 | high | The outdoor/indoor context must be per renderer or variant, not per material (2.2) | the foundation of 2.2, set only on the world path |
| 3 | high | U16 half settled; the swap removes R12's "last" | U16's remainder comes first (work packages 2 and 3); no shading tuned before its inputs are established per client and compared in game |
| 4 | high/medium | A mixed look until Stage 12 (interiors on the preview rig) | outdoor light and fog only in the outdoor context; stated in the log and the panel |
| 5 | medium | Exterior MOCV unresolved (2.2) | validation on MOCV-free exteriors; one comparison of an exterior with MOCV recorded as a known difference |
| 6 | medium | Stage 09 coupling (2.4) | protocol-20 offer; the off switch; class B re-measured; retune only as a follow-up |
| 7 | medium | Terrain normals (2.7) | MCNR or neighbour normals inside this stage |
| 8 | medium | Skybox models overlap Stage 11 | colour sky for every client first; skybox models late in this stage or in 11 (D7) |
| 9 | medium | Fog units decide the streaming edge and 13's urgency | settled by in-game fog comparisons (T7, T8) before the fog is tuned |
| 10 | low/medium | Bloom on a bright sky (the noon sky reaches sRGB 255 against a knee at 188); the preview rig already feeds bloom | a world-view bloom policy (D9) |
| 11 | low | References across builds (PTR 12.1.7 vs Retail 12.1.5) | PTR shots compared with the viewer reading the PTR's own tables (identical on every shared column, 3.2) |
| 12 | low | Confusing numbers in older texts | the note in `roadmap.md` 5.1 |

### 2.10 Guards

Byte-identical on the light-off path and outside the world view: Models (the bear, MD5 52605ecc), Buildings (WMO 108631
MD5 d3db7749; Retail 108238 and 4235933, Beta 7962574, Era 107243 and 111185), the six Buildings models with liquid,
the Stage 07 battery (98 composite dumps, 6 geometry dumps, the reg pictures). Characters is a guard only with the
owner's tabard fixes (on `develop` `TabardDetails::tier` is uninitialised and the pictures vary between runs).

## 3. The client data (PROVEN_BYTES unless graded otherwise)

### 3.1 The tables per client

Every client has Light, LightParams, LightData, LightSkybox, ZoneLight and ZoneLightPoint (WDC5). Readable rows (copy
rows in parentheses):

| Table | Retail | PTR | Classic Era | MoP Classic | Classic Beta |
|---|---|---|---|---|---|
| Light (5F16BC84 everywhere) | 5,349 (+12) | 5,364 (+12) | 512 | 1,503 (+1) | 620 (+1); 15 rows in sections without a key |
| LightParams | CAE394E7: 1,676 (+2,069) | A7F31923: 1,682 (+2,078) | 51C96BAD: 452 | 51C96BAD: 1,215 | A7F31923: 395 (+478) |
| LightData | 6ABF2921: 19,881 | 360DA016: 19,932 | 70C6CD80: 1,795 | 70C6CD80: 5,690 | 360DA016: 5,910 |
| LightSkybox (9D4956FF everywhere) | 490 (422 + 68) | 491 | 9 | 203 (120 + 83) | 28 |
| ZoneLight | 94CE95E0: 735 | 94CE95E0: 737 | BA8274F0: 0 rows | BA8274F0: 80 | 94CE95E0: 24 (+1 without a key) |
| ZoneLightPoint (DE2377FB everywhere) | 16,440 | 16,448 | 0 | 2,692 | 649 |
| LightDataGlobalVolumeFog | absent | 24290E20: 0 rows | absent | absent | 24290E20: 11,200 |

Also present (no definition in the repository; names from public definitions): Weather, WeatherXParticulate,
Particulate, Lightning, WindSettings (weather: Stage 11's territory), CelestialBody and MapCelestialBody (Retail and PTR
only: two condition-gated extra bodies, not the sun or moon), SkySceneXPlayerCondition and VolumeFogCondition (phase
gates), LightParamsLightShaft/LightShaft and LightWorldShadow/WorldShadow (per-key extras, few rows), SSAOSettings,
TerrainColorGradingRamp. None is needed for the outdoor sky, light and fog this stage proposes.

Retail and PTR agree on every shared LightData column of all 19,881 shared keys (Retail fields 0-43 = PTR 1-44; the
arrays 44-46 = PTR 60-62). In LightParams, Retail fields 30-31 equal PTR fields 36-37 on all 3,745 shared rows: the PTR
layout inserts six fields at 30-35, which contradicts the public positional names and the comment in the repository's
`LightParams.dbd` ("fields 0-31 are the same"). The repository's Era/MoP LightData definition types fields 22-45 as
integers, but the bytes are floats and colours at those positions. Both are corrected when the columns are named.

### 3.2 LightData: what is populated

Shares of keys with a non-zero value (names INFERRED (public); shapes PROVEN_BYTES):

| Column (public name) | Retail / PTR | Era | MoP | Beta |
|---|---|---|---|---|
| DirectColor, AmbientColor | 99-100 % | 97-98 % | 100 % | 98-99 % |
| SkyTop / SkyMiddle / SkyBand1 / SkyBand2 / SkySmog / SkyFog colours | 91 / 98 / 98 / 98 / 98 / 99 % | 74 / 94 / 95 / 95 / 95 / 97 % | 86 / 99 / 99 / 99 / 99 / 100 % | 81 / 95 / 96 / 96 / 97 / 97 % |
| SunColor | 87 % | 93 % | 97 % | 96 % |
| Cloud colours (sun, emissive, layer 1, layer 2), CloudDensity | 77 / 36 / 71 / 27 %, 42 % | 92 / 29 / 89 / 61 %, 71 % | 99 / 43 / 88 / 63 %, 81 % | 82 / 23 / 78 / 18 %, 70 % |
| ShadowOpacity (a grey) | 55 % | 90 % | 99 % | 27 % |
| FogEnd / FogScaler / FogDensity | 46 % / 57 % / 99 % | 99 % / 86 % / 6 % | 97 % / 73 % / 0 % | 45 % / 66 % / 4 % |
| FogHeight, FogHeightScaler/Density, FogHeightCoefficients[4] | 31 %, 47-49 %, 14-16 % | 0 % | FogHeight 100000 on 82 %; others 0 % | about 1 % |
| HorizonAmbient, GroundAmbient (hemispheric ambient) | 45-46 % | 1 % | 0 % | 13 % |
| EndFogColor, EndFogColorDistance, FogStartOffset, SunFogColor, SunFogStrength, FogHeightColor | 11-49 %; SunFogStrength 99 % | about 1-2 % | 0 %; SunFogStrength constant | about 1 % |
| ColorGradingFileDataID (a LUT) | 8.5 % (93 distinct) | 0 % | 0 % | 5 % (8 distinct) |
| PTR/Beta added fields 45-59 | PTR: constants | -- | -- | Beta: varied, meaning UNKNOWN |

Colours are 0xRRGGBB in the low 24 bits, stored signed (mask to 24 bits). Floats enter the viewer's database as text
with 6 significant digits (`wdc3file.cpp:835-841`): FogEnd 9,999,999 becomes 1e+07. Colour-grading files are 32^3 LUTs
stored as 1024 x 32 uncompressed BLPs (R = x mod 32, G = y, B = x div 32; the identity LUT 1140733 matches within 1);
Beta's brighten mid-grey by 12-19 steps.

Most keys are identical across clients where they share a (LightParams, time): DirectColor equal on 79 % of Era/MoP
keys, 95 % of MoP/Retail keys, and Beta equal to Era on every shared key. Where Era differs, its Direct is brighter
(median luminance ratio about 1.7) and its Ambient dimmer (about 0.65). One formula driven by each client's own data can
therefore serve every client; whether the re-authored Era keys also expect a different shading model is UNKNOWN.

### 3.3 Time of day, slots and LightParams

- **Time.** LightData.Time is in [0, 2880) in every client (max 2870), every value a multiple of 10; PTR and Beta store
  0x10000 + t (masked to 16 bits, as `LightCatalog.cpp:199` does). The unit is the half-minute of a 2880-unit day
  (INFERRED from the range; public documentation agrees). Days hold 1-24 keys (Beta 25); 86 Retail days have no key at
  12:00 and 995 have none at 00:00, so evaluation must wrap past midnight (the repository already wraps,
  `LightCatalog.cpp:409-412`).
- **Slots.** Light.LightParamsID[8]: slot 0 on every row; slot 1 on every row; 2 and 3 on most; slot 4 on every row with
  4-7 distinct values (Retail: LightParams 3 on 5,301 of 5,361 rows); slots 5-7 rare (Retail 133 / 2 / 2, Beta 48 / 46
  / 10, Era none). Public meanings: 0 clear, 1 clear underwater, 2 storm, 3 storm underwater, 4 death (INFERRED
  (public)). The bytes fit them: slot 4's LightParams name the death skyboxes (`DeathSkybox.mdx`, `DeathClouds.mdx`),
  slots 1 and 3 have shorter fog ends than 0 and 2, and the grey LightParams 10 appears only in slot 2. Slots 5-7 have no
  known meaning.
- **LightParams.** LightSkyboxID (field 3; field 4 in 51C96BAD) always names a LightSkybox row, copy rows included.
  CloudTypeID is 0 on every row of every client. Glow is populated (median Retail 0.2, MoP 0.5, Era 0.6, Beta 0.3).
  HighlightSky is 0 or 1. Flags carry 0x100 (a sun-position override; 86 Retail rows) and 0x200 (a light-direction
  override with SunPolar/SunAzimuth; 41 rows); MoP and Era have neither. Beta varies the 12.x fields 16-38 that Retail
  holds constant; their meaning is UNKNOWN.
- **Map.TimeOfDayOverride** (the repository's `Map.dbd` Code12): -1 on 1,081 of 1,187 Retail maps, 0-1,350 on 106. All
  values fit a day in minutes [0, 1440) (public documentation says minutes; INFERRED).

### 3.4 Fog: four patterns in the data

- **Retail and PTR:** FogDensity on 99.1 % of keys (median 4.5) and FogEnd 0 on 54 %, plus height fog, end-fog colours,
  sun fog and hemispheric ambient on a third to a half of the keys; polynomial coefficients on 34 keys.
- **MoP Classic:** FogEnd and FogScaler only (FogDensity 0 on every key); FogHeight = 100000 on 82 % (an "off" value:
  INFERRED).
- **Classic Era:** mostly FogEnd and FogScaler (FogEnd non-zero on 99.2 %), but 114 keys of 14 LightParams carry
  FogDensity (2, 3 or 6) and 30 the newer fog colours.
- **Classic Beta:** a hybrid. LightDataGlobalVolumeFog (its name proven from the file's table hash) has 11,200 rows on
  3,904 of 5,910 keys, up to three rows per key indexed by a slot field (0-2, sparse); its 28 other columns have no
  public meaning. The remaining keys use FogEnd. On 12-key days only the 00:00 and 12:00 keys keep a FogEnd, which
  suggests Beta ignores FogEnd where the new table is present (INFERRED).

FogEnd's values are round in their stored unit (Era: 86.7 % multiples of 1,000, 14.5 % multiples of 36; median 12,000,
max 48,000). Read as yards they exceed any view distance; divided by 36 they give 56-1,333 yd. Public sources disagree on
the scale (one scales by 1/36, another compares the raw value with the far clip in yards). UNKNOWN until an in-game fog
comparison on Era or MoP (9.3).

### 3.5 Skyboxes

LightSkybox (Name, Flags, SkyboxFileDataID, CelestialSkyboxFileDataID: public names; shapes PROVEN_BYTES). Retail's 490
rows name 348 distinct M2 files, 346 of them with a single sequence; flags 0-31. Slot 0 names a skybox on 147 of 274
Retail map-0 Light rows (79 of them the Lost Isles sky) and 63 of 138 on map 1; MoP 94 of 265 and 57 of 190; Beta 1 of 85
and 0 of 132; Era none on maps 0 and 1 in slots 0-3. Era's 6 of 9 rows carry no FileDataID, only `.mdx` names (5 open
as `.m2` by name). None of the validation spots' lights names a skybox in any client. Behaviour (INFERRED (public)): the
skybox is drawn over the colour sky at its light's weight; flag 0x1 ties its animation to the time of day; flag 0x4 draws
the dome's lower part in the end-fog colour. The skybox materials are unlit (all 243 materials of the 12 most used
Retail skyboxes carry M2 flag 0x01; 3 write depth).

### 3.6 Sky assets

`textures/suncenter.blp`, `sunglare.blp`, the moon textures, `environments/stars/stars.m2` and star textures exist in
all five clients; the cloud textures and `sky1.blp` only in Retail, PTR and Beta; `skydomecolor.blp` everywhere but Era.
Which of them the client draws, and how, is INFERRED (public) from the names (4.4).

### 3.7 `_lgt.wdt` and `_fogs.wdt`

- `_lgt.wdt` holds placed point lights (MPL3, 56 bytes; MoP's MPL2, 52 bytes), spot lights (MSLT, 64 bytes), light
  animations (MLTA, 12 bytes) and cookie textures (MTEX), each record positioned in world yards and indexed by its tile
  (record structure PROVEN_BYTES; roles INFERRED). Retail Azeroth 12,099 point and 112 spot lights; Era none. Falloff
  medians 10-27 yd. These are local lights, the same problem as the world models' MOLT/MNLD: **Stage 12** (if the owner
  agrees, D4).
- `_fogs.wdt` holds placed fog volumes (VFOG, 104 bytes; a quaternion rotation, a centre, a colour, a shape model for
  some). Retail and PTR: 152 volumes, all in the north-east of the Eastern Kingdoms; none in Kalimdor. Beta: 195, 94 and
  7 (Eastern Kingdoms, Kalimdor, Alterac Valley). Era and MoP: none. Which field is the extent is UNKNOWN; under one
  reading Beta's Kalimdor stands lie inside 23-25 volumes. Deferring them is safe for Retail and PTR, not proven for
  Beta's outdoor look (D4).

### 3.8 The validation spots

Selections by Stage 09's rules (rules INFERRED, data PROVEN_BYTES), slot 0:

| Spot (game X, Y) | Retail / PTR | Classic Era | MoP Classic | Classic Beta |
|---|---|---|---|---|
| Southfury River (1261, -3948), the Stage 09 stand | zone 139 Durotar, Light 204, LightParams 5985 | map default 191, point 204 at weight 1.0 (LightParams 212) | zone 139, Light 204, LightParams 212 | map default 191 (LightParams 7636) |
| Crossroads (-442, -2596) | zone 153 Barrens Main, Light 193, LightParams 5944 | point 193 at 0.98 over 191 (196) | zone 153 (196) | point 193 at 0.98 over 191 (7626) |
| Durotar gate (1430, -4400) | zone 140, Light 239, LightParams 6033 | points 239 (0.54) and 204 (1.0) | zone 140 (910) | zone 3169, Light 204 (7620) |
| Goldshire (-9462, 62) | zone 2471, Light 12786, LightParams 6080 | map default Light 1 (12) | Light 1 (12) | zone 3197, Light 16849 (7747) |

At noon at Southfury the Retail light is Direct (94, 50, 0), Ambient (183, 165, 184), SkyTop (160, 99, 148), SkyMiddle
(255, 226, 175), SkyBand1 (246, 178, 88), SkyFog (120, 75, 58), FogEnd 0, FogScaler 0.15, FogDensity 3; Beta's default
is a blue sky (SkyMiddle (69, 196, 255)) with FogEnd 18,000; Era's and MoP's LightParams 212 has the same sky as Retail's
5985 and FogEnd 32,000. Full dumps at 00:00, 06:00, 12:00 and 18:00: `D:\wmv-10\survey\data\out\md_spots.md`.

## 4. How the game draws the outdoor scene (a graded model)

Each rule names its grade and, where open, the in-game test of section 9 that settles it.

### 4.0 The client's own shaders (PROVEN_BYTES)

The client ships its shaders as compiled programs in its storage; the model viewer's M2 light path was already read
this way (`WmvOpaque.shader:144-160`, `combiners_uber_3_3.bls`, FileDataID 5221414). For this stage the sky, cloud,
glow, colour-grading, fog, terrain, M2, liquid and world-model containers were extracted from all five clients,
disassembled with the operating system's shader disassembler, and checked twice by independent readers; one check
executed every terrain program numerically against the formulas below (agreement to 4e-7). Evidence:
`D:\wmv-10\design\fidelity`, `D:\wmv-10\verify-bytecode\identity` and `...\shapes` (each with its rerun steps). What the
programs do is PROVEN_BYTES; which constant holds which table column, and which variant the client binds, is decided by
CPU code that cannot be read (the executable's code is encrypted on disk) and stays INFERRED until measured in game.

- **Two engine branches, not five clients.** 16 core containers (sky 2977085/2977730, clouds 2977081/2977726, glow
  2977118, grading 2977110, terrain 2977247 and its vertex program 2977850, M2 5221414, liquid fog 2977154, and others)
  are byte-identical in Retail 12.1.5, Classic Era and MoP Classic; a second set is identical between the PTR 12.1.7 and
  Classic Beta. Of 437 shader containers Retail lists, 427 are byte-identical in Era and MoP; 9 differ, among them the
  world-model `uber.bls` pair (2977272/2977862). So Era and MoP draw terrain, doodads, sky and fog with Retail's own
  programs: their different look lies in their data and the CPU path.
- **The PTR and Beta differ from Retail.** Their programs (sky, clouds, all 316 terrain programs, M2, world models, water,
  particles) carry a constant-gated linear-light path: textures, vertex and constant colours decoded with a 2.2 power,
  and the ambient wrap constants switched to 1.1^2.2 and 0.7^2.2. Even with the gate off they combine local light
  differently (a 2.2-power sum instead of Retail's square-root sum), add a secondary-lighting buffer Retail lacks, and
  scale the light attenuation where Retail only switches it. Retail, Era and MoP have none of this.
- **Which path the references used (step 1, 2026-10-21; checked independently).** The PTR's "Lighting Mode" setting is
  the quality of secondary lighting (GI grid and samples), not the gate. The gate follows a start-up setting, `linear`
  ("0- disabled (live), 1- linear"; INFERRED from the executable's strings and the UI code), which the client writes to
  `Logs\General.log` at every start. The PTR session that took all 12 Stage 09 PTR shots logged `Linear: 0`; the Beta
  session logged `Linear: 1` (PROVEN_BYTES). On the gate-off path with no local light, the PTR's terrain and sky programs
  give Retail's output exactly (0 sRGB steps, executed), and a pixel test on five fitted PTR shots fits that path with no
  free parameter (MEASURED). So **the PTR references stand for Retail's terrain and sky shader path at the Southfury
  stand**; the PTR's CPU side (light selection, tables, fog constants) is another build, so full Retail equivalence stays
  UNKNOWN until one live Retail shot at the same stand (9.8). The Beta shots do not stand for Retail. Every capture
  records the `Linear:` line; a PTR session not at `linear 0` is a retake for Retail targets.
- **Shader family.** On DX12 the clients bind the Shader Model 6 family (`dx_6_0`; Retail's log says so, the PTR's and
  Beta's say "SM6"). The `dx_5_0` programs executed here have the same entry-to-program partition, constant rows and gates
  as their `dx_6_0` counterparts (checked for terrain and sky); they stand for them by structure (INFERRED).
- **The outdoor light** (terrain and M2, one scene-light block of up to 11 vectors). Two variants exist, and which one
  the client binds is UNKNOWN:
  - *hemisphere* (176 of 316 terrain programs, half the lit M2 programs): H = N . up >= 0 ? mix(horizon, up colour, N . up)
    : mix(horizon, ground, -N . up); A = 0.7 H + 0.4 H (0.5 + 0.5 N . L) with N . L **not** saturated, so A spans 0.7-1.1 H
    (the public description saturates it: the bytes settle that); D = direct x max(N . L, 0) x attenuation x shadow;
    colour = sqrt((albedo (A + D))^2 + albedo^2 x local light) + specular;
  - *flat* (140 of 316 terrain programs, the other half of the lit M2): colour = albedo x sat(local + direct x max(N . L, 0)
    + ambient) x (0.7 + 0.3 x shadow).
  - Terrain albedo = layers x vertex colour x 2, with 0.5 substituted when the chunk has no vertex colour, and no clamp
    before lighting; terrain specular (N . H)^20 by the layer alpha; a second directional term from a block of unknown
    source; a baked shadow read from a texture at the alpha-map UV (MCSH: INFERRED). The terrain vertex program takes its
    normal from the vertex data and never recomputes it (so MCNR, 2.7, is the INFERRED source).
  - Half the lit M2 programs take their light colours from a per-vertex light set instead of the scene block.
- **Fog is computed per material** in every terrain and M2 program: radial distance from the camera with a vertical
  pull; visibility = min(mix(e^(-k D), e^(-k_h D), hf), sat(1.4286 (1 - d / end))) with D = max(0, d - two start terms); a
  cubic height factor hf; a polynomial ("artistic") path mixed in by a scalar; an older **power-fog path** min(pow(max((end
  - d) x inv, 0), rate), 1) chosen per layer by a flag; colour mix(mix(fog, end fog, c^3), mix(height fog, end height fog,
  c), hf) with c from a start offset and a distance, then a cubic tint towards one colour around the **sun** direction; up
  to four weighted layers, all taking the first layer's colours. The power path exists in the very programs Era and MoP
  run; which path the CPU selects for them is UNKNOWN.
- **The sky** (2977085): the colour interpolated from the dome's vertices plus a cubic tint towards one colour around the
  sun, with separate strengths above and below the horizon (a ramp over the dome's lowest 0.02), and an optional
  blue-noise dither. The program holds no elevation logic: the ring elevations and colours come from the vertex buffer
  and must be measured.
- **Clouds** (2977081): a textured layer times the vertex colour, plus a sun term. **Glow** (2977118): mix(A, B, a) +
  B^2 x g. **Colour grading** (2977110): the saturated scene looked up in one 32^3 LUT (1024 x 32), with a bypass; how the
  client blends LUTs across keys is not in the shader. `ffxfogcombine` is a full-screen overlay, not distance fog;
  `fogdepth` accumulates the fog volumes of `_fogs.wdt`.
- **World models** (`material3_wmo_ps` 5446718 / `material3_wmo_vs` 5446713, listed by all five clients; the
  `mapobj*.bls` names are absent from every root): WMO vertex layout (two colours, up to five texture coordinates), the
  same scene-light block, the albedo multiplied by the first vertex colour, and a blend towards per-material colours.
  How exterior MOCV is used is read from these in work package 2.

### 4.1 Time

- t = 2 x (60 h + m) for game time h:m (INFERRED; consistent with the Stage 09 references' recorded times, 9.1).
- A day's keys interpolate linearly per channel and wrap from the last key to the first + 2880 (INFERRED (public), two
  sources; a third clamps at the ends). The bytes do not decide wrap against clamp (test T2: the Southfury stand at
  22:00-00:10).
- The sandbox clock runs at real speed and can be set by the hour (PROVEN_BYTES, the client's log); live realms follow
  the realm clock.

### 4.2 Which light, and how lights blend

- **Stage 09's rules** (INFERRED): the innermost zone polygon by area, unconditional ones first; else the map's default
  (origin) row; point lights (rows away from the origin and not named by a zone) weighted 1 inside the start radius and
  linearly to 0 at the end (3D distance), applied weakest first.
- **Public alternative** (INFERRED (public), one source): every column blends; zones over a 100-yd band centred on the
  polygon edge (weight 0.5 on the edge, 1 at 50 yd inside), the Z band likewise; zones in ascending
  ZoneLight.TransitionType (a priority: inner >= outer in 351 of 352 nested Retail pairs, PROVEN_BYTES).
- **Ties and order disagree.** Among equal-priority nests, "lowest LightID wins" (the public rule) would show the outer
  zone in 225 of 295 Retail nests and is contradicted by Beta's 9 of 11; smallest-area-wins (Stage 09) and
  highest-ZoneLight-ID-wins fit both clients. Point-light order: farthest first (an older client, checked against its
  executable), strongest first (the modern public renderer), weakest first (Stage 09): UNKNOWN for the modern client.
- **Rows at Z = 0.** Era 213 of 466 and Beta 225 of 545 positioned lights have Z = 0; 2D or 3D distance is UNKNOWN (the
  impact at the spots is under 0.02 in weight).
- **Default light.** 871 of 1,187 Retail maps have no origin row; the public fallback is map 0's default (Light 1, whose
  slot 0 is LightParams 12 on Retail, Era and MoP but 7748 on Beta). For most instances and world-model-only maps that
  fallback decides the whole picture: UNKNOWN until a check in game.
- **Tests:** T5 (the Durotar/Barrens border: Stage 09's hard switch at Y -3823 against a 100-yd ramp), T6 (overlapping
  point lights), the Crossroads radial walk on Era and Beta (9.5).

### 4.3 The sky

- **The pixel** is PROVEN_BYTES (4.0): vertex colour plus a cubic sun tint. **The dome's geometry and colour order**
  are not in the shader; the public description follows.
- **The colour dome** (INFERRED (public), two sources): camera-centred, drawn first without depth writes; a zenith pole
  and five rings at about 15.8, 8.5 and 2.2 deg elevation, then two near the horizon (1.18 and -0.42 deg in the modern
  renderer, 0.22 and -1.63 deg in the older one); colours SkyTop (zenith), SkyMiddle, SkyBand1, SkyBand2, SkySmog,
  SkyFog (last ring and below), interpolated along the faces. Modern modifiers: the lowest ring leans to EndFogColor by
  far clip / EndFogColorDistance; a cubic tint towards SunFogColor around the sun; an older client also brightens rings
  1-4 at dawn and dusk where HighlightSky is set.
- **Measured** (step 1, 2026-10-21, `D:\wmv-10\step1\mapping`): fed with the bytes at each shot's recorded time and
  not tuned, the shared dome (pole and rings 1-3) fits clean sky within 0.3-3.6 sRGB steps at 10-19 deg elevation in
  five PTR shots at 06:49, noon and 18:46; a 1-deg shift of ring 1 would move about 12 steps, so rings 1-2 sit within
  about 0.3 deg (MEASURED). The earlier report of 39-105 steps below 18 deg came from a soft cloud/haze band in the
  upper-left sky of the noon shots of cameras hi05/hi06 (corrected 2026-10-22: step 1 read it as a sunlit far hill, but
  no terrain lies in those directions and the same camera shows clean sky there at dawn and dusk, 13.1), and
  deviations above about 18 deg are clouds. The two public ring tables differ only below 2.16 deg, and the lowest sky in
  any fitted shot is at 10 deg, so the lowest two rings (and whether the last ring leans to the end-fog colour) stay open:
  one level horizon view settles them (T1). The sky program's horizon ramp works in the dome's own object units over z
  in [0, 0.02], so the viewer builds the dome in the public table's units (table A, the modern renderer's, by default).
- **Band order** (PROVEN_BYTES): the sky usually brightens from the zenith towards the horizon bands, and SkyFog (below
  the horizon) is darker; a strictly monotone order holds on only 27 % of noon days.

### 4.4 Sun, moon, stars, clouds

- **Sun disc** (INFERRED (public), one source): polar angle 100 deg at 06:00, 5 deg from 11:55 to 12:05, 100 deg at 19:00;
  a fixed azimuth towards the north-west, so the sun rises and sets on the same side. An older client's table differs
  (05:30-21:30). Striking, so it is tested (T4).
- **Moons and stars** (INFERRED (public)): fixed tables; stars from `stars.m2`, visible from 21:30 to 03:30 with ramps.
- **Clouds**: no table selects a cloud type (CloudTypeID 0 everywhere), yet the PTR noon reference shows clouds over a
  light without a skybox (MEASURED). The modern cloud algorithm is UNKNOWN; an older client's (noise driven by
  CloudDensity and the cloud colours) is public.
- LightParams flags hide the sun, moons or stars and override the sun position or the light direction (INFERRED (public);
  the overrides' columns are populated exactly on the flagged rows, PROVEN_BYTES).

### 4.5 The outdoor light

- **Direction** (INFERRED (public), two agreeing sources): the directional light does not follow the sun. It comes from
  the north-west at 37 deg elevation at 00:00 and 12:00 and 20 deg at 06:00 and 18:00, linear between, unless a
  LightParams overrides it. Not checked in game (T3: shadows at 09:00, 12:00, 15:00, 18:00).
- **Shading.** The shapes are PROVEN_BYTES (4.0, two variants). Which table column feeds which slot is INFERRED
  (public): AmbientColor the up colour, HorizonAmbient and GroundAmbient the horizon and ground (a zero one falls back to
  Ambient), DirectColor the direct light, SunColor the specular. The public description saturates N . L in the wrap; the
  client's programs do not. Public documentation adds MOCV x 2 as precomputed light in world models and MOCV alpha as the
  interior/exterior blend (Stage 12; readable now from `material3_wmo_ps`). The older engine used clamp(Ambient +
  Diffuse N . L) x MCCV x texture x 2 and MCSH as baked shadow.
- **Measured on the Stage 09 PTR shots** (step 1, 2026-10-21; Southfury, LightParams 5985, about 110,000 terrain pixels
  per shot, the game against the viewer's unlit render at the fitted cameras): the **hemisphere** variant, not the flat
  one (fit error 0.023 against 0.10-0.12); the wrap **unsaturated** as in the bytes (measured 0.710 / 0.907 / 1.104 at
  N . L = -1 / 0 / 1 against 0.7 / 0.9 / 1.1); AmbientColor as the up colour, HorizonAmbientColor as the horizon,
  DirectColor as the direct light (GroundAmbient untested: no terrain faces down); colours used as byte / 255 with an
  overall scale of 0.990-1.009 at three times of day, so **no conversion constant**; the terrain specular has
  DirectColor's hue (its scale is open); the **light direction follows the public table** (azimuth 44.5-46.3 deg,
  elevation 38-39 deg at noon against the table's 36.8, 23.7 at 06:49 against 22.3, 20.9 at 18:46 against 22.2), not the
  sun. Terrain cast shadows remove the direct light at 06:49 and 18:46 along that direction, but none appear at noon
  (mechanism UNKNOWN; cast shadows are the follow-up of D10). Evidence: `D:\wmv-10\step1\mapping`.
- **Data:** DirectColor is the warmer colour (R > B on 61-65 % of noon keys in every client, against 24-36 % for
  Ambient). Retail lowered the direct share relative to Era (3.2). HorizonAmbient and GroundAmbient exist only in modern
  data. ShadowOpacity's use is UNKNOWN.
- The player's preview rig already has the same shape (three-band ambient, wrap, one key); the outdoor path replaces its
  constants with the data, not the rig itself (2.3).

### 4.6 Fog

- **The shape is PROVEN_BYTES** (4.0); the client's programs confirm the public equation's form, including the end fade
  from 30 % of the end distance. What stays INFERRED (public) is how the table's columns become the program's constants:
- **Modern mapping** (INFERRED (public), one source): start = min(far clip, 3000) x FogScaler (clamped to [-1, 1]);
  density k = max(FogDensity, 0.9) x 0.0005 per yard; visibility = min(exp(-k max(0, d - start)), sat(1.4286 (1 - d /
  far clip))), so a linear end fade from 30 % of the far clip; height fog from FogHeight, FogHeightScaler, the
  FogHeightCoefficients cubic (stored constant first: PROVEN_BYTES pattern, 94 % of rows rise from 0 to 1) and
  FogHeightDensity; colour from SkyFog to EndFogColor by ((d - FogStartOffset) / EndFogColorDistance)^3, height colour
  mixed in, then a cubic tint towards SunFogColor around the sun, scaled by SunFogStrength and a day curve; zero colours
  fall back to SkyFog; a polynomial ("artistic") path on 34 keys.
- **What it predicts at the Southfury stand** (PTR far clip 7,000): no distance fog within 450 yd; the Stage 09
  references (scene within about 250 yd) are fog-free, and their residual differences come from light and sky. The sky
  changes: the dome's fog ring becomes EndFogColor.
- **Era and MoP:** UNKNOWN between an older power fog (end = FogEnd / 36, start = FogScaler x end; map flag [2] 0x2 is set
  on 49 of 59 Era, 75 of 285 MoP and 49 of 73 Beta maps, none in Retail) and the exponential path with a derived density
  (T8).
- **Beta:** its volume-fog table is unnamed beyond the slot field; Beta's fog can only be approximated or fitted until
  its meaning is established (D17, D18).
- **Settings matter:** the far clip changes the fog under this model; the references must record it (9.2).

### 4.7 Colour grading and glow

- **LUTs** (3.2): Retail 1,691 keys, Beta 321; Era and MoP none. How they blend across keys and lights and in which
  colour space they apply is UNKNOWN; "Darker" is darker than the main LUT on only 71 of 159 keys.
- **Glow**: the shape mix(A, B, a) + B^2 x g is PROVEN_BYTES (4.0); that A is the scene, B a reduced-resolution blur
  and g the blended LightParams.Glow is INFERRED (public, two sources). Today's viewer bloom is URP's, unrelated to the
  data (2.1).
- **Grading**: the LUT lookup is PROVEN_BYTES (4.0); the blending across keys is not in the shader (UNKNOWN).

### 4.8 By client

| | Retail / PTR | MoP Classic | Classic Era | Classic Beta |
|---|---|---|---|---|
| Zone polygons | 735 / 737 | 80 | none (point lights up to 2,647 yd) | 24 readable |
| Fog | density, height, end and sun colours | FogEnd/FogScaler only | FogEnd/FogScaler; density on 114 keys | volume-fog table on 66 % of keys; FogEnd on the rest |
| Hemispheric ambient | about 45 % of keys | none | about 1 % | 13 % |
| LUTs | 1,691 keys | none | none | 321 keys |
| Skyboxes in slot 0 (maps 0/1) | 147 / 63 Light rows | 94 / 57 | none | 1 / 0 |
| Placed lights and fog volumes | `_lgt` yes; `_fogs` Azeroth only | `_lgt` (MPL2); no `_fogs` | none | both |
| Shader programs (4.0) | Retail: its own branch; PTR: the newer branch shared with Beta | Retail's, byte-identical | Retail's, byte-identical | the PTR's branch |

## 5. The light catalogue: reuse, extend, replace

| Part (Stage 09) | Where | Verdict |
|---|---|---|
| Five-table read from verified layouts, status, counts, the LightParams copy table | `LightCatalog.cpp:76-88, 140-212, 331-365` | reuse; widen the column lists |
| Key store (every key of every day; time plus five packed colours) | `LightCatalog.h:79-87, 184`; `.cpp:246-270` | extend: every column per key |
| `sample()` (linear, wraps at 2880, rounds per channel) | `.cpp:386-427` | keep for the water; the outdoor day is sent whole and evaluated by the player |
| Zones (order, Z band), point lights, default light, copy source | `.cpp:271-328, 469-559` | reuse; the rules become the player's, with the open questions of 4.2 decided by test |
| The "values present" rule, the fixed 12:00 and slot 0, `sky` = SkyBand1 | `.cpp:560-603`; `.h:58-62` | keep for the water only; the outdoor offer has validity per group of columns, whole days and slots 0-4 |
| Slots | `database.xml:522` declares all eight | no schema change to read them; the Light struct keeps one today |
| `database.xml` light tables, dbd names | `database.xml:504-555`; `bin_support/dbd` | extend: name the established columns per layout, correct 3.1's errors, add LightSkybox; schema 22 -> 23 (every client's database rebuilds once) |
| Protocol 19 `liquids.lights` and its checks | `UnityIpcServer.h:363-407`; `app.cpp:9256-9692` | unchanged; a new protocol-20 offer beside it |
| Player `LiquidLights` zone/point geometry, `LiquidLightEase` | `LiquidLights.cs:45-399` | reuse the geometry and the ease mechanism in a general light model; the fixed 19-value water layout stays for the water |
| Player `WmvLiquidLight` | `WmvLiquidLight.cs` | keep for the water; a world-light driver beside it |

Size of a whole-day offer (estimates over the real rows under two independent proposed encodings; today's size is
exact): Retail map 0 today 134,753 bytes; all LightData columns of slot 0 over the whole day about 350-415 KB; all
slots about 480-580 KB; the worst map (PTR map 0) about 540-660 KB. The player's line limit is 192 MiB. Each map's object
is built once and kept. The wider read at client load (about 54 columns x 19,881 keys) is not measured; it runs on the
GUI thread's client-load path and is budgeted (8.3).

## 6. Proposed architecture (for review; nothing is built)

Three independent proposals (client-data fidelity first, small increments first, renderer integration first) were
merged into this one and then reviewed adversarially; the review's findings are applied below. The working files are in
`D:\wmv-10\design\_results`.

### 6.1 Rules everything else follows

1. **The light off reproduces Stage 09 exactly.** `WMV_LIGHT=off` (read once, like `WMV_WATER`) draws today's shaders
   and today's water path; every Stage 09 picture and every guard of 2.10 stays byte-identical.
2. **One evaluated light state per frame**, for the main camera in the world view only. One selection and one clock feed
   the sky, terrain, world models, doodads and water, so they cannot disagree.
3. **Sky and fog never depend on the camera's far plane.** The sky shades only pixels still at the cleared far depth and
   writes no depth (the water grab's coverage rule is kept). Fog uses the radial view distance and a **game far-clip**
   value of its own (default 7,000, the reference sessions' setting), so Stage 13 can move the 3,000-yd plane without
   changing Stage 10's picture.
4. **Shapes from the client's bytes, constants as labelled hypotheses.** Every equation shape is the client's own
   (4.0). Every constant the bytes cannot give (the light direction table, the sun path, the fog density scale and start
   mapping, the sky's ring elevations, the zone band) is a uniform set from one C# table that records its grade; each is
   measured in game or ships with its INFERRED (public) label shown in `runtimeState` and in this document. No code, vertex
   array or constant table is copied from public sources (D1).
5. **Uniforms only, no shader keywords** (the project's rule; variants are stripped).
6. **The host reads; the player selects, evaluates, eases and shades.** The host sends whole days.
7. **Opt-in while it is built** (D14): off by default until the owner accepts the stage, so deploys to the owner's build
   do not change the world view.

### 6.2 Host

- **LightCatalog** keeps everything Stage 09 has (TIME, SLOT, the key store, `sample()`, `mapJson` and its water-only
  rule) and adds beside it: a typed full-column key store (colours masked to 24 bits, floats as read); LightParams'
  Glow, LightSkyboxID, Flags, HighlightSky and sun overrides; all eight Light slots; ZoneLight TransitionType; Map
  TimeOfDayOverride and Flags; LiquidType.LightID; LightSkybox read through the database (its copy rows resolve; 96 Retail
  and 86 MoP references name copy rows only).
- **Validity per channel group** ("has keys"), not water colours. The water group keeps Stage 09's rule (6.11). A slot-1
  LightParams without keys falls back to slot 0 (Retail 3, Era 24, MoP 20, Beta 3 lights; INFERRED, logged).
- **Time budget.** The client load builds one compact fragment per LightParams day, as a measured client-load stage under
  the progress dialog (as Stage 09's catalogue is; budget +250 ms per client). The per-map object, assembled at a map's
  first load in the GUI round of `RequestLoad`, targets 10 ms on the worst map (PTR map 0), hard cap 50 ms, else it moves to
  the map asset worker. Measured in work package 5, not assumed.
- **Schema 23.** LightData columns named per layout from their byte shapes; Era/MoP fields 22-45 retyped from integer to
  float and colour; the LightParams comment corrected (the PTR layout inserts six fields at 30-35); Time masked to 16 bits
  in 360DA016; new `LightSkybox.dbd`, the Map and LiquidType names; a notice per adapted definition. Every client's
  database cache rebuilds once (about 20 s on Retail, and its disk use is checked first: F: is short of space); floats
  stay 6-significant-digit text, which the offline replica reproduces.
- **Checks** (`-terraintile`): new light pins per audited build including MoP 5.5.4.70531 (whose checks skip today);
  named-row values (for example LightData row 62960 = LightParams 5985 at 12:00); a size bound for the new object (1 MiB);
  the existing `liquids.lights` pins byte-unchanged; the stale comment at `app.cpp:9625` (129,669 bytes, now 134,753)
  corrected; Era's local file count checked before every Era run.

### 6.3 Protocol 20

`PROTOCOL_VERSION` 20 with `playerDrawsWorldLight()` beside `playerDrawsLiquids()`. New: a top-level `worldLight` object
on `loadTerrainTile` and `loadWorld` (never inside `liquids`), whole days for slots 0 and 1, JsonUtility flat arrays; one
message `setWorldLight {on, clock: pinned|running, time}`; `runtimeState` fields (light on, time and its source, zone,
light, params, fog mode, fog start and density, game far clip, sky on). A protocol-19 player receives the exact
protocol-19 messages. Streamed tiles keep the entry load's object, as `lights` does today.

### 6.4 Player light model (pure C#, `Wow/World/`, run by the console runner)

- **Selection** (`WorldLightSet`) reuses Stage 09's zone and point geometry through a shared helper (`LiquidLightTests`
  unchanged). Stage 09's rules are the default; the public alternatives (the 100-yd band with TransitionType priority,
  strongest- or farthest-first points, 2D distance, zone lights as points) exist only as debug arms for the substitution
  test, and a default changes only after an in-game test names the mechanism.
- **Where the light is evaluated:** at the camera, as Stage 09 does. Whether the game uses the camera or the character is
  UNKNOWN (test T11).
- **Interpolation** (`WorldLightDay`) on the integer half-minute, colours rounded per channel as the host's `sample()`
  does (a runner invariant), floats linear, integers from the earlier key; wrap after the last key (clamp as an arm until
  test T2).
- **Clock** (`WorldClock`): pinned by default (12:00:00 proposed, D13) or running at real speed; a map's
  TimeOfDayOverride wins; light bands step per half-minute while the sun moves continuously; independent of
  `-wmvAnimTime`.
- **Sample** (`WorldLightSample`): named channel groups (six sky colours; up, horizon and ground ambient; direct; sun;
  fog floats and colours; glow; LUT ids; skybox ids and weights; water colours and alphas). Stage 09's ease generalised to
  N channels (snap when pinned, on jumps over 60 yd and on clock jumps). Cost at most 0.05 ms a frame.
- **Model table** (`WorldLightModel`): the graded hypotheses (light direction, sun path, fog constants, ring table) and
  CPU twins of the sky and fog functions for the GPU fixtures.

### 6.5 Frame state and isolation

- A driver, `WmvWorldLight`, in `beginCameraRendering` beside `CullWorldModels` (`WmvMainTerrain.cs:149, 958-1004`),
  installed and released by owner like the water grab (the self-test runs a second `WmvMain`).
- Its globals are set only for the main camera in the world view and reset at every other camera's begin and end (the
  water grab's pattern; today's zone-light globals are not reset per camera, the new ones are). They are pinned off in
  `WmvTerrainComposite.RenderToArray`, the light check and the self-test fixtures. `RenderSettings` and the shadow rig's
  globals are never touched.
- The post state (bloom) is changed at world-view Enter and restored exactly at Leave, in versioned code only; the local
  Unity project's pipeline asset, volume profile and screenshot script are hashed for both arms of every comparison.
- In the top-down overview (orthographic) fog is pinned off and the outdoor light is off by default, so the overhead map
  stays as it is.

### 6.6 Shaders: world-lit variants by include split

Each pass body of `WmvOpaque`, `WmvWmo`, `WmvTerrain`, `WmvTerrainTextured` and `WmvLiquid` moves verbatim into an
include; today's `.shader` files keep their properties, render state and pragmas and include the body; new world-lit
variants define one symbol before the same include and add two hooks (the light, and the fog before the alpha and
encode lines). One `WmvWorldLight.hlsl` holds the light, fog and sky functions in the project's own code. World
materials are bound to the variants after the build, only on the world path.

- **Identity** is checked by compiled-bytecode hashes of every original pass before and after the split (a versioned
  editor script), and by an exactness gate: with the light on but the variant's hooks set to identity, sky off, bloom
  untouched, the water on Stage 09's noon object and the clock at 12:00, every Stage 09 world picture is byte-identical.
- **Fixtures** (the Stage 07 composite battery, the liquid GPU fixtures, the self-test cameras) draw the live material,
  so for those renders the original shader is bound and restored, and a self-test asserts which shader drew.
- **Why a split** rather than a branch inside the shared shaders: identity then rests on the source, the preview viewers
  pay nothing, and a later world shadow caster needs no edit of the Models shader. It does not copy a renderer: one body
  serves both. **Fallback:** a uniform-gated branch whose else-branch is today's text, with the guard pictures as the
  only proof.

**As built for world models and doodads (step 6, 2026-10-22).** (The terrain's world-only shader kept step 5's uniform-gated
branch.)

- **Files.** `WmvOpaque.shader` and `WmvWmo.shader` keep their header, properties, render state and pragmas and include their
  pass bodies from `WmvOpaquePass.cginc` / `WmvWmoPass.cginc`; `WmvOpaqueWorld.shader` ("WMV/Opaque Textured (world)") and
  `WmvWmoWorld.shader` ("WMV/Map Object (world)") define `WMV_WORLD_LIT` before the same include; `WmvWorldLight.hlsl` holds
  the globals, the named holds and `WmvWorldModelShade`. The hooks sit inside `#ifdef WMV_WORLD_LIT`, so without the symbol
  the bodies are HEAD's text.
- **The light.** The client's hemisphere program for M2 and world models: H and the wrap 0.7 + 0.4 x (0.5 + 0.5 N.L) on the
  normalised normal (N.L not saturated), the direct term saturated on the raw interpolated normal, colour = sqrt((base x
  F)^2 + base^2 x local) with F = A + D; local light, shadow, AO, the second directional term, the override weight, the
  MOCV light, glow and desaturation held at their neutral values, so the colour is base x F. M2 base = the combiner x the
  material colour (the vertex program's 0.5 x colour times the pixel program's 2: no extra factor); world-model base =
  albedo x (1, 0.99998, 1) (the vertex programs' gate-off COLOR0 x 2). Unlit batches (M2 flag 0x01, additive; MOMT
  F_UNLIT) return the base. No constant was added: the only numbers are the bytes' 0.7, 0.4, 0.5 and 2 x (0.5, 0.49999,
  0.5). Evidence: `D:\wmv-10\step6\m2` (2,688 lit M2 programs executed; hemisphere and flat classes), `D:\wmv-10\step6\wmo`.
- **Binding while shown.** Copies of a prototype's materials on the variants (one per distinct original, kept until the
  prototype goes) are bound per renderer only while the outdoor light is shown in the perspective world view
  (`WmvWorldLight.ObjectsLit`), at the main camera's begin, before its culling and draw; in every other state -- default
  mode, the top-down overview, no valid light, the camera not placed, the light off, after a fault -- every world renderer
  holds the builder's material, so those states draw the original programs. Interior groups and the doodads interior
  groups list keep the builder's materials always (until Stage 12). Default mode makes no copy; `WMV_LIGHT=off` loads no
  variant.
- **Binding at placement.** With the light on from the start the tile and its light are committed before the load's world
  models and doodads are placed, so every world model and doodad is bound as it is placed. Those bindings are counted and
  timed per manager and said as one line once a frame passes without a new placement (or before a rebind), marked
  "(placed while shown)"; the 50 ms budget of 8.3 applies to a rebind (a toggle), not to these sums.
- **The lines** (none with `WMV_LIGHT=off`). Per load: "load N: outdoor light contexts: ..." (world models and their groups
  by context, doodads by context and listing, MDDF doodads standing inside an interior group's box). Per rebind and per
  burst of placements: "world light: world models and doodads -> the outdoor light ..." with renderers, placements,
  doodads and batches by blend mode; "... -> their own materials (not shown: overview / no valid light / camera not placed
  / light off / not the world view / fault) ..."; and once per show "world light: first lit frame T ms (frame N, ...)",
  which includes the variants' pipeline-state creation.
- **Identity, as checked.** (1) Offline compile check (the editor's include files and Unity's implicit prelude, the SDK
  compilers): HEAD's and the split's originals give identical preprocessed tokens and identical disassembly as whole files,
  reflection headers included, for WmvOpaque vs / ps / ps with `_ALPHATEST_ON` and WmvWmo vs / ps; the variants compile with
  0 errors. (2) Unity's own compiled bytecode hashes were not taken (they need an editor script; the editor is started only
  through the checkpoint script). Identity therefore rests on (1) and on the guard pictures (13.2), which gate the commit.
  (3) Self-test: the original program under the light's globals equals itself exactly; the variants' else-branch against
  the original is reported (no picture draws it). The exactness gate above was written for variants bound whenever the world
  view draws; bound only while the light is shown, no Stage 09 world picture draws them, so the default-mode,
  light-on/models-off, overview and Models/Buildings/Characters guards serve its purpose.

### 6.7 The indoor/outdoor context (the Stage 12 foundation, kept small)

A context per world renderer: OUTDOOR (exterior groups, MDDF doodads) or INTERIOR_INTERIM (interior groups, doodads listed
by interior groups; the 6 doubly listed MODD records are logged), with two recorded bits (0x40, MOCV present) that only
feed censuses. It is set only on the world path, so Buildings and Models never carry it. The carrier is chosen by a probe:
the renderer's shader user value (present in Unity 6000.5; whether it reaches these passes and survives `Instantiate` is
UNKNOWN), else per-(material, context) copies on world prototypes (about 16 % more world-model materials in the census
window), else a property block. No Stage 12 layer or term is prebuilt; Stage 12 adds its fog layers, MOCV term and
per-model colours at the named hook points.

### 6.8 The sky stack

One render pass, enqueued only for the main camera in the world view, at an explicit event before the water grab, ZTest
against the cleared far depth, no depth writes, recorded in order:

1. **the colour dome**: a camera-centred mesh whose vertex colours come from a ring table (elevation, colour) held as
   data, starting from the public hypothesis (15.8, 8.5, 2.2 deg; the lowest two rings disputed), fitted on the P0 sky
   captures and frozen as MEASURED; pixel as 4.0 (vertex colour plus the cubic sun tint, the dither off);
2. **(gated) skybox models**, alpha by light weight, depth writes forced off;
3. **(gated) sun, moons, stars.**

The sun direction feeds the sky's and the fog's tint, so it is a core input: its path is measured in P0, not only in the
gated sun-disc package. Sky pixels keep the water grab's coverage 0; a self-test checks it.

### 6.9 Fog

One function with the 4.0 shape, layer 0 only; radial distance with the vertical pull; the game far clip; absolute height
for height fog from the floating origin (as the ripple origin is computed today). The mode is chosen per key from the
data's pattern, not the client's name: exponential where FogDensity is set (Retail, PTR); the power path for FogEnd-only
keys (Era, MoP) with FogEnd's unit a switch (D17; default off until test T8); off for Beta's volume-fog keys (class C).
WMO F_UNFOGGED and M2 NoFog are honoured; the fog colour per blend mode follows the M2 program. Applied to terrain, world
models, doodads and the water's own surface terms (never a second time to the refracted scene); particles follow in Stage
11. URP's fog is not used.

### 6.10 Outdoor shading

`WorldLit()` with the 4.0 shape. Which variant (hemisphere or flat) applies is decided by measurement (the A/D fit and
the shadow A/B pair). The column mapping and the light direction (the public table, or LightParams 0x200) are the labelled
hypotheses of 6.1. Terrain albedo = composite x MCCV x 2 with 0.5 where a chunk has none, not clamped before lighting;
normals from MCNR in a separate vertex stream, so the geometry normals and the Stage 07 dumps stay unchanged (2.7); the
specular, the second directional term, the local-light buffer and the baked shadow are held neutral unless a reference
measures them. No preview-rig front light, no 1.25 peak. Unlit world-model and doodad materials keep lum 1.

### 6.11 Water (D5)

- **Light on:** the water's colours and alphas come from the light set on the player's clock; the water channel group
  keeps Stage 09's rule (a base light without water colours at that time is invalid for water, point lights without them
  are skipped for water), so at 12:00 the water's inputs equal Stage 09's on every day of every client (a runner invariant
  over maps 0 and 1 and the spots); reflection misses sample the sky function; fog applies to the surface terms only.
- **Light off:** Stage 09's host noon object and code path exactly. No water constant is retuned in Stage 10; the class B
  items are re-measured and any retune is a follow-up the owner approves.

### 6.12 Post (D9)

With the light on, URP's bloom intensity is 0 in the world view (it is not client data, and both a noon sky and the
preview rig feed it today), restored exactly on Leave. The client's glow and LUTs are gated work packages built from 4.0.

### 6.13 Controls and names

`WMV_LIGHT=off`; `-wmvGameTime=HH:MM[:SS]`; `-wmvGameFarclip=N`; `-wmvSky=off`, `-wmvFog=off`;
`-wmvInteriorLight=rig|outdoor` for the side-by-sides; debug `-wmvLightRule`, `-wmvLightWrap`. Harness steps `time:`,
`light:`, `farclip:`, `expect:light:`. The Maps panel gets a "Game light, sky and fog" checkbox and a time row (HH:MM, run
at 1x, noon and dusk presets; "fixed by the map" under an override), sending `setWorldLight` only on change, within 50 ms
per GUI round. The labels "no WoW lighting, sky or fog" (`WmvMainTerrain.cs:1672-1679`, `MapsViewer.cpp:1481`) change.

Step 6 (read once; a refused value is a Note; none acts with `WMV_LIGHT=off`): `-wmvDoodadLight=on|off` and
`-wmvWorldModelLight=on|off` (default on; off keeps that kind on the builder's materials with the light on);
`-wmvDoodadVariant=hemisphere|flat` and `-wmvWorldModelVariant=hemisphere|flat` (default hemisphere; flat is the client's
flat class with K = 0, for the record); `-wmvInteriorLight=rig|outdoor` (default rig; outdoor also binds interior groups and
their doodads, for the side-by-sides only); DIAGNOSTIC `-wmvWorldLitView=albedo|normal` (the variants draw the albedo, or 0.5
+ 0.5 x the raw interpolated normal; valid on opaque and alpha-keyed batches only; never in the owner's pictures).

### 6.14 Stage 09 files that Stage 10 changes (in its own diff)

`LightCatalog.{h,cpp}` (extended; the water path unchanged; the comments at `.h:7, 59`), `UnityIpcServer.h` (the
protocol-20 block and the comment at `:386`), `LightData.dbd`, `LightParams.dbd`, `database.xml`, `GameDatabase.cpp`
(schema), `app.cpp` (checks, MoP pins, the comment), `WmvLiquid.shader` (through the split; fog only in the world
variant), `WmvLiquidLight.cs` (the source switch), and a shared helper extracted from `LiquidLights.cs` with no change in
behaviour. `09-liquids.md` is not edited.

### 6.15 Ready for shadows (added 2026-10-21)

Shadows are not drawn in Stage 10, but nothing in it has to be rebuilt to add them:

- **The light keeps the client's split.** `WorldLit()` takes a shadow factor as an input, applied exactly where the
  client's programs apply it (4.0). For the terrain: on the direct and specular terms of the hemisphere variant, on the
  whole colour as 0.7 + 0.3 x shadow in the flat variant. For M2 doodads and world models the programs differ (step 6,
  PROVEN_BYTES: `2977063 p_203:199-212`, `5446718 p_17:164-176`): the shadow multiplies the direct term only, in both the
  hemisphere and the flat class, and 0.7 + 0.3 x shadow appears only in lighting mode 0 (unlit batches), where today's
  unlit path returns the base and must take that factor once a shadow is fed. Stage 10 feeds it 1; a later shadow stage
  feeds it a shadow-map lookup.
- **The world-lit variants own their passes.** A shadow-caster pass and a shadow-map lookup can be added to the world
  variants (6.6) without touching the Models, Buildings or Characters shaders.
- **The inputs are already carried.** The light direction is a uniform of its own (`_WmvWL*`), separate from the sun
  direction; LightData's ShadowOpacity is read into the light state, unused until its meaning is settled; the terrain's
  baked-shadow texture slot (MCSH, INFERRED) is held at 1.
- **The difference is measured, not hidden.** The validation takes a shadows-off/shadows-on pair in game (9.8) and
  reports, beside every comparison, how much of the remaining difference cast shadows explain (9.6).

## 7. Order of work (revised 2026-10-21)

### 7.1 The order

The owner reorganised the work so that visual results come early (section 0, item 4). Each step is independently
testable, lands as its own commit on this branch, and keeps `WMV_LIGHT=off` byte-identical to Stage 09:

| Step | Content | Visible result | Built on (work packages of 7.2) |
|---|---|---|---|
| 1 | Finalise the essential client-data mappings and the shader interpretation for the sky and the terrain light; exterior MOCV and the PTR/Retail lighting mode investigated; the helper addon v3 | -- | WP 2 (parts) |
| 2 | A reproducible baseline with the light off (develop 678071f0 rebuilt, every Stage 09 set and guard re-run twice and compared) | -- | WP 1 |
| 3 | Time-of-day evaluation and the outdoor light state: the host's columns and protocol 20, the player's light model, clock and driver, no pixel change | the light state in the log and `runtimeState` | WP 5, 6, 7 (minimal) |
| 4 | An initial sky (the colour dome with the client's pixel shape) | **checkpoint, part 1: the sky against the Southfury references** | WP 8 |
| 5 | Terrain lighting with MCNR normals (the terrain world variant only) | **checkpoint, part 2: lit terrain against the references** | WP 9 (terrain), 10 |
| 6 | Outdoor world models and doodads, with exterior MOCV if established | buildings and doodads lit | WP 9, 12 |
| 7 | Fog from the client data and shader behaviour | fog | WP 11 |
| 8 | Water with the new sky, light and fog | water | WP 13 |
| 9 | Validation on every client and visual comparisons | the stage's report | WP 14, 20, 21 |

After step 5 the owner sees the first sky and outdoor-light comparisons before any extensive tuning. The early steps
use the existing Stage 09 references (9.1) and the minimal Southfury captures (9.8); no other capture is a prerequisite.
The early prototype may implement only what its step needs (for example only the terrain's world variant in step 5);
the rest of a work package follows in the step that needs it.

### 7.2 Supporting work packages

These remain the detailed plan behind the steps, not prerequisites of the early prototype. Before the owner's review
only read-only work was allowed; after it (2026-10-21) repository edits follow the steps of 7.1.

| WP | Content | Entry | Exit (done when) | Effort |
|---|---|---|---|---|
| 0 | Owner gate: consent (D1), decisions (D2-D22), capture logistics (sandbox clock, fly mode, one race on all clients, Era/MoP characters and realm clocks, a live Retail pair), the shot list | this plan | consent and answers dated in this document; P0 list frozen | agent 1 h; owner 45-60 min |
| 1 | Baseline (read-only): clean build of `develop` 678071f0 on D: with the owner's tabard patch in both arms (patch hash recorded); every guard and Stage 09 set captured and hashed; the Unity project state hashed; Unity licence checked | none (parallel with 0) | two baseline runs agree on every deterministic set | 3-6 h |
| 2 | First-party evidence: the glow kernel, liquid, volume-fog and PTR M2 programs; the world-model program's use of MOCV; MCLV, MOGP 0x40, exterior MOCV and MCSH censuses per client; the MCNR axis proof against the terrain vertex program; skybox emitter census; spot scripts (a map-0 spot, a Glow > 0 stand); the schema-23 column list; a throwaway timing of the wider read outside the repository | 0 (consent for the column names) | every element of 4.0-4.7 has its data, shape and CPU links filled or marked UNKNOWN; schema-23 list frozen; timings measured | 1-1.5 d |
| 3 | References P0a then P0b (9.8): helper addon v3 tested offline; the owner's sessions; camera fits; the clock checked against the client log; repeat floors; "Stage 10 zero" from the 20 old shots; targets, masks and tolerances frozen and hashed per metric family with a declared fit set and holdout | 0 | fits within 0.3 px; targets hashed | owner 2.5-3 h; agent about 6 h |
| 4 | Prototypes on a scratch branch, not product code: the include split's bytecode identity; the context carrier probe; the sky pass's event order and coverage; throwaway renders for the owner (the streaming edge at Southfury facing 45-60 and 210, the interim interiors, the Era/MoP fog hypotheses) | 0, 1 | AD decisions written here; the owner has seen the renders | about 1 d |
| 5 | Host: schema 23, the extended catalogue, protocol 20, the checks (6.2-6.3) | 2 | `-terraintile` on five builds: Stage 09's counts plus the new pins, 0 failures, SKIPs listed per client; `liquids.lights` byte-identical; protocol-19 messages identical; the time budgets measured | 1.5-2 d |
| 6 | Player light model and runner tests (6.4) | 5's offer schema | runner green: byte parity with the host's `sample()` on every key of maps 0 and 1 on five clients, the 12:00 water invariant, the spot selections, wrap and clamp, the override | about 1 d |
| 7 | Driver, clock and switches, no pixel change (6.5) | 6 | light off: every guard and Stage 09 set identical; non-main cameras read the light off; a 48-half-hour sweep twice, identical | 0.5-1 d |
| 8 | The sky stack's dome and the bloom policy; **owner checkpoint 1** | 7; P0 sky captures | light off identical; coverage dump unchanged; GPU fixture within 3e-3; the sky within tolerance on its fit set | about 1 d plus the fit |
| 9 | Shader foundation: the include split, the world-lit variants, binding, the context (6.6-6.7) | 4, 7 | the exactness gate of 6.6; guards identical; batch counts unchanged; original bytecode unchanged | about 1.5 d |
| 10 | MCNR normals and the outdoor light on terrain | 9; 2's MCNR proof; P0 ground views and the P1 shadow series | seams continuous within 1 deg; A and D within 10 % per channel on the fit set; light direction within 3 deg of the table, or the measured one adopted and reported | 1-1.5 d |
| 11 | Fog; **owner checkpoint 2** | 10; the P0 fog pairs and the P1 elevated view | visibility RMS 0.03 on the fit set; each far-clip arm checked for what it discriminates (9.4 T7); `-wmvFog=off` reproduces WP 10 | 1-1.5 d |
| 12 | Exterior world models, outdoor doodads, interim interiors; **owner checkpoint 3** | 9, 11 | Buildings, Models, Characters identical; the Crossroads exteriors checked (holdout, scored in 20) | 1-1.5 d |
| 13 | Water coupling (6.11), including the reflection-miss change | 8, 11 | the 12:00 invariant on every day; no Stage 09 family worse by 0.1 z beyond explained class B; a double-fog fixture | 0.5-1 d |
| 14 | Host time control, the Maps panel, reporting | 7 | the harness drives the time; no GUI round over 50 ms | 0.5-1 d |
| 15-19 | Gated by the owner: sun disc; underwater slot 1; the client's glow; colour-grading LUTs; skybox models, stars and moons (emitters and ribbons in skyboxes stay Stage 11) | each its decision and reference | each its own tolerance | 0.5-1.5 d each |
| 20 | Captures P1-P3, validation rounds on the fit sets, the holdout scored once, the owner's review by eye | 8-14 | a per-client report of every family; every difference classed (9.6); the owner's verdict recorded | 2-3 d plus sessions |
| 21 | Close-out: regression and budgets on the committed source built clean; the default per D14; this document's results; the roadmap; the changes file; deploy; one PR | 20 and the owner's acceptance | every check passes; budgets met | about 1 d |

The core (0-14, 20-21) is about 15-19 agent-days and 5-7 hours of the owner's time; the first visible picture (the sky,
WP 8) comes after about 5-7 agent-days, once the P0 sky captures exist. These are rough estimates.

## 8. Scope and budgets

### 8.1 In Stage 10

U16's remainder for the outdoor light; the client's shader bytecode as the source of every shape; the protocol-20 offer;
the player's light model and clock; the frame driver; the world-lit variants and the indoor/outdoor context; the sky
dome; the outdoor light on terrain (with MCNR normals), exterior world models and outdoor doodads; per-material fog;
the water's coupling; the world-view bloom policy; the controls; the validation of section 9. Gated by the owner, each
with its own reference: the sun disc, underwater slot 1, the client's glow, colour-grading LUTs, skybox models with stars
and moons.

### 8.2 Not in Stage 10, and where it goes

| Item | Where | Why |
|---|---|---|
| Interior lighting: interior MOCV, blend batches, MOHD/MAVG, MOLT/MOLR/MNLD, MFOG, F_EXTLIGHT/F_SIDN/F_WINDOW, the camera inside a model | Stage 12 | the owner's constraint; Stage 10 gives only the context |
| Exterior MOCV | Stage 12 (or a later Stage 10 increment if a live Retail/MoP measurement settles its scale) | investigated 2026-10-21: partly established; its scale and per-batch mode are CPU-side, so including it now would need a guessed constant (2.2) |
| `_lgt.wdt` placed lights | Stage 12 (D4) | local lights, the same problem as MOLT |
| `_fogs.wdt` fog volumes, the volume-fog programs | Stage 12 or later (D4) | local; Retail and PTR spots are clear of them |
| Beta's volume-fog table and its added columns | a later investigation | 28 unnamed fields; class C |
| Clouds | a later sky increment (D8) | no cloud-type table; the modern mesh and mapping are UNKNOWN; masked in the sky metric |
| Cast sun shadows, SSAO, sun shafts, MCSH | a named follow-up milestone after Stage 10 (D10); the architecture is ready for them (6.15) | the largest visible gap left; its share measured and shown (9.6, 9.8) |
| Weather slots 2-3, death slot 4, lightning, particulates | out | server or player state the viewer does not simulate; slots 5-7 UNKNOWN |
| Terrain specular, the second directional term, the local-light buffer | held neutral | only if a reference measures them |
| CelestialBody, SkyScene, LightShaft, WorldShadow, TerrainColorGradingRamp | out | rare, conditional or of unknown meaning |
| Far terrain, a larger stream radius | Stage 13 / Stage 08 | Stage 10's sky and fog do not depend on the far plane |
| Fog and light on particles and animated doodads | Stage 11 | it uses Stage 10's include |
| Terrain ground cover (the grass and pebbles the game generates per texture layer) | Stage 11B, after U21 (roadmap 5.2) | found missing at the checkpoint (13.1); the owner gave it a stage on 2026-10-22; it takes Stage 10's outdoor doodad light and fog |
| Retuning Stage 09's water | a follow-up the owner approves | after the class B re-measure |
| A "strong look" that alters the data | never (D11) | a strong presentation can only be a separate, labelled option |
| The Characters non-determinism on `develop` (`TabardDetails::tier`) | its own fix branch | Stage 10 baselines Characters on the tabard-fixed host |

### 8.3 Budgets (proposed, D21)

Measured on Stage 09's rig (12.9 / 12.11), three alternated runs per arm (baseline, light off, light on), with no WoW
client running and the GPU idle (checked and recorded before each run; a failed check voids the run):

| Item | Budget |
|---|---|
| GPU, world view, light on, uncapped | at most +0.30 ms a frame (sky pass at most 0.08 ms; fog and light at most 0.15 ms); each gated increment at most +0.15 ms more |
| GPU, light off | within noise of the baseline |
| Capped frame | 16.66 ms unchanged; frames over 33 ms at most the baseline's + 2 a run |
| GPU memory | at most +32 MB (MCNR stream about 2.4 MB at 16 tiles) |
| Lifecycle | five Clear/enter cycles back to about 350 MB, every tally 0 |
| Player CPU | light evaluation and globals at most 0.05 ms a frame, no per-frame allocation; offer commit at most 2 ms; parse at most 20 ms on the reader thread; the live toggle at most 50 ms once |
| Host | the catalogue extension at most +250 ms per client load (a measured stage); the per-map object at most 10 ms (50 ms hard cap); no GUI round over 50 ms; the mode switch under 50 ms |
| Payload | the new object at most 1 MiB per load (estimates 0.35-0.63 MB); `liquids.lights` unchanged |
| Database | one cache rebuild per client at schema 23 (about 20 s on Retail), disk checked first |

## 9. Validation against in-game screenshots

Nothing in this section has been run in a game client. The rigs and outputs live in `D:\wmv-10`, outside the
repository, as Stage 09's did.

### 9.1 What the Stage 09 references give

The 20 Stage 09 shots (`D:\wmv-09\refs\shots`: 12 PTR, 8 Beta; 2,560 x 1,440 JPEG, no EXIF) carry their game time, from
the helper addon's record (GetGameTime) and the client's own log ("Time set to" at every clock change) (PROVEN_BYTES):

| Shots | Game time | Light (slot 0) |
|---|---|---|
| PTR #1-#2 | 18:44-18:46 | zone 139, LightParams 5985 |
| PTR #3-#4 | 06:48-06:49 | the same |
| PTR #5-#12 (the shots Stage 09 tuned on) | 11:53-11:58 | the same |
| Beta #1-#8 | 11:35-11:36 | map default 191, LightParams 7636 |

- Stage 09's 12:00 assumption held for the shots it tuned on, within one sRGB step for the four water colours (at most
  0.97, OceanClose) and SkyBand1 (0.58), but not for SunColor (4.3 steps at 11:53), SkyTop (1.75) or SkyMiddle (1.38)
  (PROVEN_BYTES values, linear interpolation INFERRED). It did not hold for #1-#4 (up to 192 steps), which served only
  the glint check.
- The cameras of #5-#12 are fitted (residual 0.18-0.22 px); #1-#4 and the Beta shots are not.
- The shots hold little sky: at most 19 % of the frame (#9), never above about 34 deg of elevation, and never the zenith
  (MEASURED from the fitted cameras). They can serve as a first time-of-day check at one stand (three PTR times, one
  Beta time), not as a sky-gradient reference.
- Repeat noise: two captures of the same pose 2 s apart differ by p95 1.0 sRGB step in 32 x 32 block medians on land and
  sky (max 4.5); water is far noisier (p95 8).
- Settings: the clients' settings files written after the sessions hold far clip 7,000, horizon start 1,900, view
  distance 6, graphics quality 6, lighting mode 2, shadows, SSAO, sun shafts and depth effects on (PROVEN_BYTES); that they
  held during the shots is INFERRED. Gamma, brightness and contrast were not written (an older Beta file had contrast 38).
  The addon records only the overall graphics preset.

### 9.2 Capturing a chosen time of day

- **Sandboxes (PTR and Classic Beta).** On 2026-10-19 the owner set the PTR clock by the hour within seconds (the log
  shows 19:48, 18:48, 22:48, 06:48, then 0:51 ... 11:52), and the minutes kept following the wall clock; the clock ran at
  real speed (PROVEN_BYTES). Which command did it, and whether it sets minutes, is a question for the owner (WP 0).
- **Live Classic Era and MoP Classic.** No teleport, no clock: the realm's clock decides. The Retail EU realm's clock was
  within a minute of the wall clock on 2026-10-19 (one log line); the Classic realms' are UNKNOWN (MoP Classic is
  installed on the "us" branch, Era on "eu").
- **Helper addon, version 3** (revised 2026-10-21: automatic, no calibration): every screenshot records by itself the
  position, facing, map and zone, the game time to the second (the minute rollover timed), the server and local time,
  the character, and every setting that changes sky, fog and light (lighting mode, view distance, far clip, horizon start
  and clip, shadows, SSAO, sun shafts, depth effects, volume fog, glow, gamma, brightness, contrast, render scale, liquid
  detail, field of view). One command shows the session's short capture list and the next shot (pose, facing, time),
  with a facing helper that turns green within 0.5 deg. Southfury is the only required spot. No walks, no calibration.
- **Procedure.** Keep screenshot quality 10, JPEG, 2,560 x 1,440 and field of view 90. Set the hour when the wall clock's
  minute reaches the target minute; wait 10 s. Within a time series, do not move or turn. Afterwards one PowerShell line
  copies the screenshots, the addon's saved variables and `Logs\General.log` into `D:\wmv-10\refs\<client>`.

### 9.3 What is measured, and how

The camera fit reuses Stage 09's (`D:\wmv-09\tune\hi\highref`): blocks matched between the reference and the viewer's
render, rays cast into the client's terrain, the reference camera solved (gate: median residual at most 0.3 px at 2,560,
roll within 0.01 deg); one fit per pose at the noon shot, reused for that pose's other times. Comparisons are made in the
viewer's frame (the reference downscaled to the 1,665 x 956 viewport; never upscaled). Renders come from the Maps
harness on a hidden desktop with the clock pinned.

| Quantity | Capture | Measured in the reference | Compared with |
|---|---|---|---|
| Sky against elevation | a sky view (horizon in the lower third, pitch about +15 to +20 deg) and a level view at an open-sky facing; optionally the zenith | median colour per elevation band (0-1, 1-2, 2-4, 4-7, 7-11, 11-16, 16-22, 22-30, 30-40, 40-52 deg, zenith) and per azimuth sector from the sun; dE00 | the same bands in the render; the band placement is MEASURED, not assumed |
| Sun and moon | the disc at dawn and dusk; the character's shadow seen from straight above at 09:00, 12:00, 15:00, 18:00 | azimuth and elevation of the disc; azimuth and length of the shadow | the viewer's sun and light direction at the same times |
| Fog against distance | a facing whose terrain spans 50 yd to the resident limit; an aerial view if the sandbox's fly mode works | per terrain pixel its distance from the ray cast; the ratio game / viewer-without-fog, normalised at 25-75 yd, per 25-yd band; fitted start, end or density and colour | the same ratio with the fog on (flat within tolerance) |
| Outdoor light | a ground view at each pose and time; lit and shaded slopes; MOCV-free building exteriors (9.5) | light factor = game / the viewer's unlit Textured view, binned by n . L (normals from the viewer's Normals view; L measured from cast shadows, T3, not taken from the sun disc); fit A + D max(0, n . L) per channel | AmbientColor and DirectColor at the shot's time, through one global colour-domain conversion fixed at the anchor |
| Distant terrain | silhouettes at 300-550 yd | the top 5 % band's colour against distance | after the fitted fog |
| Time of day | each pose at 12:00, 16:00 and the dusk pair 21:20 + 21:25; optionally 05:00 + 05:05 and 00:30 | every measure above per time; the dusk pair's change against the change the data predicts | the pinned clock |
| Zone transitions | the Durotar/Barrens border walk and the Crossroads radial walk (9.5), at 12:00 | sky and lit-ground colour per point: where, step or ramp, how wide | the viewer's light at the same points |
| Water (Stage 09) | the Southfury High shots at 12:00 and the dusk pair | Stage 09's six score families | light on: class B items move towards the reference; light off: identical to Stage 09 |

### 9.4 In-game tests that settle the open rules

| Test | Where and when | Settles |
|---|---|---|
| T1 sky | Southfury, facing 210 (open sky; horizon at +0.7 deg) and Ratchet's sea horizon (facing 210-240), pitch +5 and +30 deg; 12:00, 06:30, 21:30 | the dome's ring elevations and colours, a dawn/dusk highlight, the cloud colours (4.3) |
| T2 midnight | Southfury, facing 80; 22:00, 23:00, 23:50, 00:10 | wrap against clamp: lit terrain at 23:00 shows DirectColor about (56, 50, 112) under wrap, (75, 52, 83) under clamp (4.1) |
| T3 light direction | Southfury or the Crossroads, the character's shadow from above; 09:00, 12:00, 15:00, 18:00 | whether the light keeps a fixed north-west azimuth (shadow always to the south-east, length 1.33 x height at noon) or follows the sun (4.5) |
| T4 sun disc | any open stand; 08:00 and 17:00, turning to find the sun | whether the disc stays in the north-west (4.4) |
| T5 zone border | the border walk at X 1261, Y -3910 to -3740 (9.5); 12:00, facing 0 | Stage 09's hard switch at Y -3823 against a 100-yd ramp (the two polygons overlap by 14.6 yd); the noon SkyBand1 differs strongly, (246, 178, 88) against (153, 220, 245) (4.2) |
| T6 point-light order | Thousand Needles (-4666, -2202, ground) on map 1, 12:00 | farthest-, weakest- or strongest-first (fog colour about 91 % against 32 % of one light) (4.2) |
| T7 modern fog | an elevated view west over the Barrens from Southfury (fly mode or a high teleport), 12:00, at far clip 7,000 and at a far clip well below 3,000 (about 1,000-1,500, after checking the setting's minimum) | the fog mapping of 4.6: at 7,000 and 3,000 the predicted start is the same 450 yd, so only a far clip below 3,000 moves the start (to 150-225 yd) and the end fade (from 300-450 yd) inside the resident terrain; a second key with a higher density or a negative FogScaler is added by script |
| T8 Era/MoP fog | the same view on live Era and MoP | FogEnd's unit and the legacy fog path (4.6) |
| T9 time override | map 861 (an override of 720) | minutes or half-minutes (3.3) |
| T10 LUT | Classic Beta Southfury, 08:00 (no LUT) and 12:00 (LUT 8286665), contrast at its default | whether a global tone shift equal to the LUT applies (4.7) |

| T11 where the light is evaluated | the ZB border: the character on one side, the camera zoomed out across it | camera or character position (the transect's tolerance depends on it) |
| T12 engine branch | one live Retail screenshot at the Southfury stand at realm noon, same pose and settings as a Stage 09 PTR shot; optionally the PTR at `linear 1` (one restart) | whether the PTR's CPU side matches Retail at this stand (the shader path is settled, 4.0); the optional shot confirms that `linear` is the gate |
| T13 uncontrolled effects | Southfury noon pairs: shadows off/on, SSAO off/on, sun shafts off/on, glow off/on (a stand with Glow > 0) | the share of each effect the viewer does not draw; the fit-set shots are then taken with them off |
| T14 sun path | the sun disc at 08:00 and 17:00, and the sky's tint at 16:00 and 21:20 | the sun vector that feeds the sky's and the fog's tint (a core input) |

The research's first sky spot (Darkspear Strand) is dropped: on Beta and Era it lies under water. Beta's light and sky
are compared at a key without a LUT (08:00 at Southfury), or the LUT read from its bytes is inverted in the comparison;
otherwise Beta's targets are class C.

### 9.5 Spots per client

All on Kalimdor (map 1), Horde or neutral. Facings in degrees counter-clockwise from north, as the game and the addon
show them. Z = ground + 1, from each client's own terrain (identical on the four clients except Razor Hill).

| Spot | Purpose | Sandbox line (PTR and Beta) | Light: PTR / Beta / Era / MoP |
|---|---|---|---|
| SFR, Southfury River (the Stage 09 stand) | time series at a fitted pose; water re-check; lit and shaded bluffs; open sky at 210; mountains at 45-60 | `!tele 1261 -3948 20 1` | zone 139 (5985) / default 191 (7636) / point 204 at 1.0 (212) / zone 139 (212) |
| ZB, the Durotar/Barrens border | a positive border test on PTR and MoP, a negative control on Beta and Era; long enough (170 yd) to tell Stage 09's hard switch at Y -3823 from a 100-yd ramp, and dense (1-2 yd) within about 20 yd of the two polygon edges (Y -3823.1 and -3837.7) | `!tele 1261 Y Z 1` for Y from -3910 to -3740 in steps of 10, and 1-2 yd steps near the edges; Z = ground + 1 per point (the list goes into the helper addon); the camera-to-character offset recorded per shot | 139 to Y -3828, 153 from -3823 / 191 / 204 at 1.0 to 0.96 / as PTR |
| XR, the Crossroads | the clean town: 7 world models within 200 yd, every exterior without MOCV; open plain at 330; hills at 105-120; shadows; aerial fog | `!tele -442 -2596 98 1` (aerial `!tele -442 -2596 196 1` if fly mode works) | zone 153 (5944) / point 193 at 0.98 over 191 / the same / zone 153 (196) |
| RW-1..7, a radial walk south-south-east of XR | a positive point-light ramp on Beta and Era (weights 0.98 down to 0.12), a negative control on PTR and MoP | `!tele -442 -2596 98 1`, `-542 -2630 98`, `-637 -2662 97`, `-731 -2694 93`, `-826 -2726 93`, `-921 -2758 96`, `-1016 -2790 96` | 153 / 193 (+205) / as Beta / 153 |
| RT, Ratchet | the sea horizon (210-240), the dawn sun; not for building light (the warehouse exterior carries MOCV, the inn has lights) | `!tele -895 -3773 13 1` | 153 / 205 + 203 over 191 / the same / 153 |

On the live Era and MoP realms: SFR (from Orgrimmar's main gate, guided by the addon), XR (the flight master's spot),
ZB on MoP only, RW on Era only; passes at noon (11:55-12:05 realm time) and the dusk pair (21:20 / 21:25), optionally
16:00.

### 9.6 Metrics, tolerances and how a difference is classified

- **Tolerance** per metric = sqrt(noise^2 + time^2 + fit^2): the repeat floor (at least 1.5 steps, or the session's own
  repeat spread), the light's rate of change times the time uncertainty (30 s with the seconds-resolution addon), and
  the colour gradient times the fit residual. Proposed acceptance (confirmed against the measured floors before use):

| Family | Acceptance (proposed) |
|---|---|
| Sky | dE00 at most max(3, tol) per band; band breakpoints within 2 deg |
| Sun | 0.3 deg (disc), 1 deg (shadow) |
| Fog | start and end within 10 % or 25 yd; colour dE00 at most 4; light-on ratio RMS at most 0.03 |
| Outdoor light | A and D within 10 % per channel |
| Distant terrain | dE00 at most 4 after fog |
| Time of day | the dusk pair's change right in sign in every channel whose predicted change exceeds 3 steps, its size within 30 %; clock offset at most 2 min |
| Transitions | the edge within 3 yd; its kind as the game's; weights within 0.1 |
| Water | no Stage 09 family worse by more than 0.1 z beyond explained class B moves |

- **Classification**, in this order: E capture (an invalid record, other settings, the time disagreeing with the log,
  weather, UI: retake); B another stage (Stage 11 effects, Stage 12 interiors and MOCV, Stage 13 terrain beyond the
  resident region, Stage 09's class C: masked and documented, never tuned); D data selection (a substitution test: the
  values at t +- 1-30 min, the other candidate lights, the other slots; if an alternative explains it and the selection
  does not, the rule is wrong); A renderer (an offline model of the viewer's formula fed the recorded time reproduces
  the reference, the viewer does not: fix); C unknown (record the evidence and what would settle it; no constant is
  changed to absorb it).
- **Missing shadows (6.15).** Every comparison reports the share of the difference that cast shadows explain, from the
  shadows-off/on pair in game (masks of the shadowed pixels, and the lit/shaded ratio with and without shadows). Where
  shadowing causes a significant difference, the result is "differs: cast shadows not drawn (follow-up milestone)",
  never "equivalent", and the side-by-side shows it.
- **PTR versus Retail.** Until the lighting-mode test (T12) shows which mode matches Retail's programs, every PTR-based
  comparison is labelled "PTR, Retail equivalence not established"; where no exact Retail comparison exists at the end,
  the uncertainty is stated in the stage's results.
- **Against score-chasing** (the owner's rule): targets, bands, masks and tolerances are frozen (hashed and dated) from
  the references alone before the first Stage 10 render is compared; every change names a class A or D difference and its
  mechanism, with the predicted effect written before the render; a change whose result contradicts its prediction is
  reverted; data columns are used as read, and any non-data constant is measured at two or more spots and times or is a
  choice the owner approves; every family reported every round; the owner judges the side-by-sides by eye and decides.
- **Fit set and holdout, declared per family before the first comparison** (each work package's exit checks its fit set
  only; every holdout is scored once, in WP 20, and a failure there is classified and reported, never tuned):

| Family | Fit set | Holdout |
|---|---|---|
| Sky | Southfury facing 210 and the Ratchet sea, PTR, P0 times | the Crossroads facing 330; Beta at its no-LUT key |
| Outdoor light | Southfury and the Crossroads ground views at two times | the Crossroads building exteriors; the map-0 spot |
| Fog | the T7 pair | the dusk pair; Era/MoP (T8) |
| Time of day | Southfury at 12:00 and 16:00 | the dusk pair; the midnight series |
| Transitions | the ZB transect | the RW walk; T6 |
| Water | Stage 09's frozen targets | -- |

### 9.7 Functional and regression validation

- **Baseline first:** a clean build of `develop` 678071f0, every set below captured before any Stage 10 change.
- **Light off** (proposed `WMV_LIGHT=off`): pixel-identical to the baseline in every Stage 09 set (2.4); with
  `WMV_WATER=low` too.
- **Other viewers, light on and off:** the guards of 2.10 pixel-identical; a mode-switch round (world, Models, Buildings,
  world) leaves Models and Buildings identical and the world light restored.
- **Four clients:** `-terraintile` with Stage 09's counts plus new light checks pinned per audited build (key times,
  sky and fog values at named rows; MoP pinned at 5.5.4.70531, whose light tables equal 70268's); the catalogue against an
  offline implementation for maps 0 and 1 on all four clients.
- **Tests:** the pure runner (2,199 plus interpolation and wrap, the half-minute index, selection, the column decode per
  layout, the sky gradient, the fog factor); the player self-test (1,055 plus GPU fixtures of the sky and fog against
  their formulas, the light-off globals, the objects released); 0 compile errors, no new warnings.
- **Acceptance, streaming, lifecycle:** Stage 09's runs on four clients, light on and off, every tally 0 after every
  Clear, no player exception, no GUI round over 50 ms; new runs: a time sweep over 48 half-hours (two runs identical), zone
  sweeps across the border and radial lines (the logged light sequence as the host's dump predicts), the free clock
  against the pinned one.
- **Budgets** (proposed, for approval): section 8.3.

### 9.8 The captures: a minimal first set, more only when a question needs it (revised 2026-10-21)

**First results need no new capture.** Steps 4 and 5 are first compared with the existing Stage 09 Southfury shots
(9.1: 06:48, 11:53-11:58, 18:44 on the PTR; 11:35 on Beta), each comparison labelled with the open lighting-mode question
until it is settled.

**The minimal first set** (Southfury on the PTR sandbox, about 10-12 screenshots, under 20 minutes; the helper addon v3
records every setting and the game time by itself, one page of instructions, `CAPTURE.md`):

1. at 12:00, the Stage 09 water pose, a level view facing 210 deg with the horizon low in the frame (the lowest sky
   rings, T1) and a sky view facing 210 deg pitched up about 15-20 deg;
2. the same three views at 18:45;
3. one shadows-off / shadows-on pair at noon (the missing-shadow difference, 6.15);
4. optional but valuable: one live Retail screenshot at the same stand at realm noon (T12; about 10 minutes from
   Orgrimmar), which settles whether the PTR stands for Retail at this stand.

The Lighting Mode pair first planned here is dropped: that setting is secondary-lighting quality, not the shader gate
(4.0). The session must run at `linear 0`, which the client's log shows.

Every other capture below is introduced only when a step needs it, and asked for then, with its own short page:

| When needed | What | For |
|---|---|---|
| step 4-5 checkpoint, if the old shots leave the sky or light open | Southfury at 16:00 and 21:20 / 21:25 (sky view, ground view); one live Retail Southfury noon pair if the lighting-mode test leaves Retail equivalence open (T12) | sky bands, time of day, Retail equivalence |
| step 5-6 | the cast-shadow series at Southfury (09:00, 12:00, 15:00, 18:00, from above) (T3); a low-sun seam shot at a tile border near Southfury; the Crossroads building exteriors at noon (MOCV-free) | light direction, normals, world models |
| step 7 | T7 (two far clips, elevated view west over the Barrens from Southfury) | fog |
| step 9, as needed | the ZB transect and T11; T6; T2 (midnight); T10 (Beta LUT); a map-0 spot; Classic Era and MoP live passes if the owner can (D16); T9; gated packages' own spots | transitions, wrap, LUTs, Era/MoP parity |

Every capture records (helper addon v3, the client's settings file and its log): game time to the second, Lighting Mode,
far clip, horizon start and clip, view distance, shadows, SSAO, sun shafts, glow, gamma, brightness, contrast, render
scale. Default settings: the 2026-10-19 sandbox profile (graphics quality 6, far clip 7,000, horizon start 1,900), the
lighting mode that matches Retail, contrast at its default. If the sandbox clock sets hours only, a shot is taken when
the wall clock's minute equals the target minute.

**Classic Era and MoP Classic without live shots** get functional validation (the host checks, the acceptance and
lifecycle runs, the light catalogue against an offline implementation, the time sweep) and data-driven validation (the
viewer's light at the spots equals the offline evaluation of their own tables); their visual parity stays "unverified".

## 10. Decisions for the owner

Each has a recommendation; everything else in this plan follows from the evidence.

**State after the owner's review of 2026-10-21 (section 0):** D3 is replaced by the exterior-MOCV investigation (bring it
into Stage 10 if reliably established); D10 stands, with the architecture shadow-ready (6.15) and the missing-shadow
difference shown in validation; D15 and D19: the lighting mode is resolved before PTR shots count as Retail's, otherwise
the uncertainty is documented; D16: Era and MoP get functional and data-driven validation now, visual parity unverified
until live shots exist; D20: the minimal first set of 9.8; D22 is replaced by the order of 7.1; D11: game-faithful (no
artistic adjustment). D1 is recorded as given (section 0). The others follow their recommendations. Still open for the
owner: the capture questions (the sandbox clock command, fly mode, one race on all clients, whether a live Retail pair is
possible).

| # | Question | Recommendation |
|---|---|---|
| D1 | Consent for outside material in Stage 10, as hypotheses only: (i) public table definitions for the new columns and tables; (ii) public format documentation for the side files and the group flags; (iii) a public renderer's CPU-side values (light direction table, sun path, fog scale and start mapping, ring elevations, zone band). Or (i) only, or none | all three, under the no-copy rule of 6.1: each public value is a labelled hypothesis paired with its measurement. Without (iii): the light direction from the shadow and n . L fits, the ring table from the captures alone, fog off until measured |
| D2 | Interior world-model groups and their doodads until Stage 12: today's preview rig plus the outdoor fog by distance; the outdoor light everywhere; or the rig without fog. And world-model-only maps: sky drawn, or the clear colour until Stage 12 | the rig plus fog, decided on side-by-sides (the Crossroads, Razor Hill, map 34) before it is frozen; world-model-only maps keep the clear colour until Stage 12 |
| D3 | Exterior-group MOCV (on 255 of 300 Retail exterior groups) | Stage 12; Stage 10 masks exteriors carrying it (class B) and validates on the MOCV-free Crossroads |
| D4 | `_lgt.wdt` placed lights and `_fogs.wdt` fog volumes | both to Stage 12 or later |
| D5 | The water under the new light | 6.11: the light set drives it with the light on; Stage 09 exactly with it off; no retune in Stage 10 |
| D6 | LightParams slots | slot 0; slot 1 (underwater, with the LiquidType override) as gated WP 16; weather and death out |
| D7 | The sky beyond the colour dome | the dome and the sun tint in the core; the sun disc once measured (WP 15); skybox models, stars and moons as gated WP 19, otherwise night is out of Stage 10's exit and labelled |
| D8 | Clouds | out of Stage 10, masked in the sky metric |
| D9 | Bloom, the client's glow and LUTs | URP bloom off in the world view with the light on; the client's glow next (gated WP 17); LUTs after T10 (WP 18) |
| D10 | Cast sun shadows, SSAO, MCSH | a named follow-up milestone right after Stage 10; their share measured now (T13); given the owner's preference for deep shadows, this will be the largest visible gap after Stage 10, and its size is reported |
| D11 | Game-faithful or strong look | game-faithful by default; a strong presentation only as a separate, labelled option if asked for after the checkpoint side-by-sides. At Southfury noon the data lights an up-facing surface at about (0.95, 0.78, 0.73) of its albedo per channel at the table's 37 deg light, (1.16, 0.91, 0.79) with the light overhead, against today's 1.25 (INFERRED: hemisphere variant, public column mapping) |
| D12 | The streaming edge against a drawn sky before Stage 13 (the 3 x 3 edge at 533-1,391 yd; at Southfury noon the fog leaves visibility about 0.88 at 533 yd) | accept it until Stage 13, decided on a headless render shown first; a labelled fade towards the fog colour only if the owner finds the edge unacceptable |
| D13 | Default time and clock | pinned at 12:00:00; run at 1x on the Maps panel; a map's override wins and is shown |
| D14 | The new light's default | off (opt-in) until the owner accepts the stage, then on; `WMV_LIGHT=off` stays. The first deploy rebuilds every client's database once even with the light off |
| D15 | Lighting-mode target (the PTR/Beta linear path, 4.0) | Retail 12.1.5's programs; the PTR references are taken at the mode P0a shows to match them |
| D16 | Classic Era and MoP Classic references (never run on this machine; MoP on the "us" branch; no sandbox) | please answer now; live passes if possible (only they settle the Era/MoP fog path), otherwise their exits are declared in advance as functional and data-driven, visual parity unverified |
| D17 | Era/MoP/Beta fog before their tests | off by default, with the hypotheses (power path at FogEnd / 36, or exponential) switchable for side-by-sides |
| D18 | Classic Beta's targets | sky colours, direct and ambient light and time of day (at a key without a LUT); fog and grading class C |
| D19 | Retail parity (the PTR is a different engine branch, 4.0) | one live Retail pair at Southfury noon (P0a); otherwise Retail parity graded INFERRED with "PTR equivalence UNKNOWN" |
| D20 | Reference settings | the 2026-10-19 sandbox profile, the P0a pairs, contrast at its default, all recorded per shot |
| D21 | Budgets | approve 8.3 |
| D22 | The order of work | as section 7: the driver, then the sky as the first picture, then the shader foundation with its exactness gate, then terrain light, fog, world models, water |

## 11. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 (high) | The include split or the binding changes a Stage 09 picture or a guard | bytecode-hash prototype first (WP 4); the exactness gate (WP 9); the guard script at every exit; fallback branch |
| R2 (high) | The CPU-side link (columns to constants, light direction, density scale, ring elevations, sun path) rests on public hypotheses that disagree in places | shapes from the client's bytes; constants as labelled uniforms; dedicated captures; fitted once on declared fit sets; substitution tests |
| R3 (high) | PTR references may not stand for Retail 12.1.5 (a different engine branch, 4.0) | P0a analysed first; a live Retail pair; Stage 09's PTR-based water targets reported, not changed |
| R4 (high) | Fog is hard to see inside the resident 3 x 3 (start about 450 yd at Southfury noon) | T7 with a far clip below 3,000; an elevated view; a second fog key |
| R5 (medium-high) | Era/MoP fog path unknown; their references may be impossible | D16 asked first; fog off by default; parity "unverified" stated in advance |
| R6 (medium) | Which lighting variant (hemisphere or flat) the client binds is unknown | the A/D fit and the shadow pair decide; both shapes implemented as a switch |
| R7 (medium) | The context carrier may not reach the passes or survive `Instantiate` | the probe; material copies as fallback |
| R8 (medium) | The sky model fits poorly on the old shots; clouds confound the upper sky | dedicated sky captures; free breakpoints; cloud masks; Beta reported, not tuned |
| R9 (medium) | Tile seams once the terrain is lit | MCNR in its own stream, proven against the terrain vertex program; a low-sun seam shot |
| R10 (medium) | A mixed look until Stage 12 (rig interiors, unresolved exterior MOCV) | D2 and D3 on side-by-sides; the Crossroads as the clean surface; documented |
| R11 (medium) | The streaming edge against a bright sky | D12 on a headless render |
| R12 (medium) | GUI-thread cost of the wider read and the per-map object | fragments at client load; measured; the asset worker as fallback |
| R13 (medium) | Global state leaking to other cameras or fixtures | the per-camera reset pattern; pins; self-tests |
| R14 (medium) | Capture logistics (hour-only clock, fly mode, one race, settings drift, the owner's time: about 5-7 h plus live slots) | the questions first; the helper addon records seconds and settings; tiers ordered by what they unblock |
| R15 (low-medium) | The unversioned Unity project shapes bloom and the baselines | post changes in versioned code with exact restore; project files hashed for both arms |
| R16 (low-medium) | Schema 23 mistypes a column; the cache rebuild on every client | per-layout shape tests and named-row values; disk checked |
| R17 (operational) | The Unity licence renewal (renewed 2026-10-22, next due about 2026-11-21; batch builds exit 198), F: space, mid-update installs, Era's install state, the owner using the machine | checked before WP 1 and each run; build on D:; hidden desktop only |

## 12. Evidence files (outside the repository)

| Folder | Contents |
|---|---|
| `D:\wmv-10\survey\_results` | the six surveys and their six independent checks, as reports |
| `D:\wmv-10\survey\{deps,catalog,player,data,research,validation}` | each survey's scripts, extracts (with sha256) and outputs; `data\README.txt` has the rerun steps |
| `D:\wmv-10\survey\verify-*` | the independent checks' own readers, extracts and outputs |
| `D:\wmv-10\design\_results` | the completeness critique, the three proposals, the merged plan and its adversarial review |
| `D:\wmv-10\design\fidelity` | the first shader extraction and disassembly |
| `D:\wmv-10\verify-bytecode\{identity,shapes}` | the two independent shader checks (own container reader, disassembly, a numerical interpreter of the terrain programs; README per folder) |
| `D:\wmv-10\step1\{mocv,lightmode,mapping,refnav}` and `...-verify` | step 1: exterior MOCV, the engine branches and `linear`, the measured sky and terrain mapping, the helper addon v3.1 (with `CAPTURE.md`) |
| `D:\wmv-10\baseline` | step 2: the light-off baseline (manifest of 258 items, `compare.py`, the rig) |
| `D:\wmv-10\step3`, `D:\wmv-10\step45` | steps 3-5: specs, implementer reports, reviews, fixes, test logs |
| `D:\wmv-10\checkpoint` | the early visual checkpoint's one-command rig and its outputs; the 2026-10-22 run in `runs\p30a` (13.1) |

## 13. Progress (2026-10-21, updated 2026-10-22)

| Step | State | Evidence |
|---|---|---|
| 1 Mappings and shader interpretation | done: exterior MOCV partly established and kept out (2.2); the references' engine path settled (4.0); the terrain light and the upper sky measured on the Stage 09 shots (4.3, 4.5); helper addon v3.1 with a 20-minute capture page | `D:\wmv-10\step1` |
| 2 Light-off baseline | done: develop 678071f0 rebuilt (host and a player from the project copy), every Stage 09 set and guard re-run twice: identical to Stage 09's outputs (one MoP magma picture has two one-pixel variants from run to run of the same build; the free-clock water flight varies as before) | `D:\wmv-10\baseline` |
| 3 Time of day and the outdoor light state | done: host 063943e0 (schema 23, the store, protocol 20, 14 new checks, 0 failures on five clients, Stage 09's dumps byte-identical); player 28b10e98 (byte parity with the host's day evaluation on five clients in the runner; in a built player the self-test's outdoor-light cases pass and, at every checkpoint camera, the player selects the zone, Light and LightParams the offline port selects) | `D:\wmv-10\step3`, 13.1 |
| 4 The first sky | done: 28b10e98 (the pixel program matches the client's operation for operation; the GPU dome equals its CPU twin within 0.0006 over 12,288 pixels); at the checkpoint within 0.3-0.6 dE00 of the game's clean sky, except the dusk shot's lowest sky (3.2-4.7 dE00 at 10-13 deg, open; 13.1) | `D:\wmv-10\step45`, 13.1 |
| 5 Lit terrain with MCNR normals | done: 28b10e98 (the branch matches the client's hemisphere path; MCNR within 1.65 deg mean of the geometry, seams within 0.64 deg; the GPU branch equals its CPU twin within 0.0005 on five probes); at the checkpoint the render equals its offline model on 99.8 % of the lit terrain within 2 sRGB steps (13.1) | `D:\wmv-10\step45`, 13.1 |
| Checkpoint | ran 2026-10-22 with player p30a (13.1); the owner approved steps 1-5 on 2026-10-22 | `D:\wmv-10\checkpoint\runs\p30a` |
| 6 Outdoor world models and doodads | done, the owner reviews its pictures before step 7: the world-lit variants by include split (6.6), the client's M2 and world-model hemisphere program; exterior groups and outdoor doodads lit while the light is shown, interior groups and their doodads on the builder's materials until Stage 12; exterior MOCV deferred; results in 13.2 | `D:\wmv-10\step6`, `D:\wmv-10\checkpoint\runs\p31a`, 13.2 |

**Offline prediction (2026-10-21, before a player could be built; not a viewer render).** The Stage 09 light-off
renders at the seven fitted Southfury cameras, lit per pixel by an offline port of the player's own formulas (the client's hemisphere terrain light with MCNR
normals, the dome with table A, the host's light state at each shot's recorded time), against the PTR references
(`linear 0`: Retail's shader path, the CPU side unverified): median terrain difference 1-3 sRGB steps per channel against
63-106 for Stage 09; sky 0.1-4.8 dE00 in the clean 10-18 deg bands against 57-84 for Stage 09 (two noon shots' bands
include a cloud/haze band, which these notes first called a far hill; corrected in 13.1). Light-facing terrain at noon is left 3-7 % dark at N . L 0.3-0.7 (the specular term,
held at 0). At dawn and dusk the remaining difference lies on terrain the game puts in cast shadow: removing the direct
light there would remove 57 % and 62 % of the squared difference, so those shots read "differs: cast shadows not drawn"
(6.15). Pictures and numbers: `D:\wmv-10\checkpoint\predicted` (`metrics.md`). The real renders of 13.1 confirm it;
its 57 % and 62 % were measured with the trees in front of the terrain lit as terrain, and read 60 % on lit terrain.

### 13.1 The early visual checkpoint (2026-10-22)

The Unity licence was renewed on 2026-10-22 (05:55; next lapse about 2026-11-21) and the checkpoint ran as one command,
`D:\wmv-10\checkpoint\run_checkpoint.ps1 -Player p30a`: player p30a built from the worktree's player files (now
28b10e98; Assembly-CSharp 860A16ADF00DF740) with the committed host. References: the Stage 09 PTR shots at Southfury
(`linear 0`: Retail's shader path; Retail's CPU side unverified), seven fitted or registered cameras, five at noon, one at
06:49 and one at 18:46. Everything below is in `D:\wmv-10\checkpoint\runs\p30a` (`out\report.md`, `out\avp.md`, the
pictures beside them).

**Functional and regression.**

| Check | Result |
|---|---|
| Build | succeeded, 0 compile errors; the main scene's script bound |
| Lifecycle self-test | 1076 passed, 0 failed, the GPU probes of the sky and the lit terrain included |
| `WMV_LIGHT=off` against the Stage 09 baseline | identical: 0 of 258 must-match items differ (the terrain, water and border guards, every view incl. depth, Buildings, Models, Characters, the tallies, the light dumps); the count items changed only by the added tests (14 `-terraintile` checks per client, 6 self-test cases); far3 varies as before; 4 timing-only info lines differ (one an integer "7 ms" the mask does not hide; life08/life08b differ between Stage 09's own runs) |
| The checkpoint's own light-off pictures | all six viewports pixel-identical to the develop player's |
| Light on: Buildings and Models | identical to the baseline (8 of 8) |
| Pure runner | 2242 passed, 0 failed (Stage 09: 2199) |
| Player log | no warning or error with the light on or off (the review's open item: no missing-channel message without UV1) |
| Light state | at every checkpoint camera the player logs zone 139, Light 204, LightParams 5985 at the shot's time, as the offline port; its first line, before the world camera flies in, is the tile-framing camera's (about 990 yd out, zone 142) |

**The render against its own model (9.6 class A).** The offline model (the Stage 09 picture lit with the client's
formulas, `rig\cpmetrics.offline_prediction`) against the render at every camera: on lit terrain, 99.8 % of the pixels
within 2 sRGB steps (mean signed difference under 1 step; 458,000-541,000 pixels per shot); the sky away from its edge
within 1.13 steps everywhere. The rest of the raw "terrain" difference is trees and doodads standing in front of the
terrain, which Stage 10 leaves unlit and the model lit as terrain: a model limitation, not a renderer one. No renderer
defect was found.

**The render against the game.**

| Family | Result | Class (9.6) |
|---|---|---|
| Sky, clean bands | noon and dawn within 0.3-0.6 dE00 over their measuring bands (Stage 09: 58-84; 2.2 dE00 in one noon shot's 16-17 deg band, which the cloud band below reaches); dusk within 0.7 at 13-18 deg | matches |
| Sky at dusk, 10-13 deg | the lowest sky the dusk shot shows (between the hills, azimuth about 105-115 deg) is redder in the game: 3.2-4.7 dE00, above the proposed 3; the render equals the client's dome formula there, so the formula's inputs or a layer Stage 10 lacks differ. Candidates: the sun tint's public day curve (weight 0 after 18:30) or its public sun position, fog or haze over the low sky, the lower edge of the dusk cloud bank | C (open; not tuned) |
| Sky, 10-16 deg in the noon shots of cameras hi05/hi06 | a soft brown cloud/haze band in the game: no silhouette, no terrain in those directions (`rig\farcast.py`: the camera's rays cast against Kalimdor tiles 34-41 x 26-32 hit nothing beyond the resident tiles), and clean sky there at dawn and dusk from the same camera; 13-15 dE00. Corrected 2026-10-22: step 1 and the first version of this section called it a far hill (class B) | out (clouds, D8) |
| Sky above about 16-18 deg | clouds: at noon brown haze patches, at dawn a faint pink glow at the top centre, at dusk a deep red-lit cloud bank behind the hilltop trees | out (clouds, D8) |
| Distant terrain | not visible from these cameras: the near hills close the horizon; no ray of cameras hi06, hi08 or hi10 (the comparison image's, the lowest-looking and the highest-looking) reaches terrain beyond the resident 3 x 3 tiles (`rig\farcast.py`) | -- (Stage 13 not exercised) |
| Terrain at noon | reference / Stage 10 scale 1.00-1.02 per channel, log spread 0.016-0.026 (step 1's figure for the client's formula: 0.023); median difference 1-2 steps (Stage 09: up to 70); light-facing slopes at N . L 0.3-0.7 are 3-7 % darker than the game, as predicted | matches; the 3-7 % is C: the terrain specular, held at 0 until a reference isolates it |
| Terrain at dawn and dusk | "differs: cast shadows not drawn" (6.15): on lit light-facing terrain, the terrain the game shadows carries 99 % of the remaining difference and the client's shadow factor would remove 60 % of the squared difference; Stage 10 is (17, 8, 0) and (17, 6, 0) steps brighter there against a direct term of (20, 9, 0) and (20, 7, 0). By eye: the hill's right flank (a sharp-edged lit patch in the game, a smooth lit stripe in Stage 10) and the slope under the acacias, 10-19 % darker in the game; the rest of the terrain within 1-4 % | shadows (follow-up milestone) |
| Shadows at noon | no terrain self-shadow, as step 1 found, but a sharp-edged shadow on the slope beside the palms and shrub (game 23 % darker than Stage 10), most likely cast by the trees | shadows (follow-up milestone) |
| Trees and doodads | unlit: Stage 09's pixels exactly. In the game they are darker than the terrain around them (luminance ratio about 0.45-0.5), in Stage 10 brighter (1.4 at noon, 2.2 at dawn, 3.1 at dusk); the canopies read bright gold, the palm trunks pale cream. The most visible difference, worst at dusk | step 6 |
| Ground cover | the game's dry grass tufts on the ridges, behind the acacias and around the palms; neither viewer stage draws ground cover | out: stage 11B, after U21 (roadmap 5.2, added 2026-10-22) |
| Haze | the game softens and blues only the far-left hill (texture contrast about 6-11 % against Stage 10's 11-14 %); the other slopes match | step 7 (fog) |
| Water | Stage 09's shading, its own light still the fixed 12:00 values in both arms; an isolation render (light on, `-wmvTerrainLight=off`) is identical to Stage 09 outside the sky apart from bright pixels 1-2 steps darker in every case (the bloom held at 0, D9), so the water's changed look in Stage 10 (12-36 % darker than Stage 09, the flecks included) is the lit river bed it blends over. The game's water shows a bright sky sheen at noon (High) and golden or copper sun glints over its right third at dawn and dusk (Low), which neither stage draws | step 8 (not tuned here) |

**Fixed after the run (the evidence rig only; no player code changed).** The side-by-sides' caption quoted the player's
first light line (the tile-framing camera's zone 142): the rig now keeps every line and quotes the last. The report adds
the sky on its measuring bands, the render against its model on lit terrain only, and the shadow split on lit terrain
only. The sky-band note is corrected (the cloud band above): the noon shots of hi05/hi06 measure from 16 deg, dawn and
dusk over all bands. Steps 6-7 re-ran on the same renders; nothing was re-rendered.

**The owner's comparison image (2026-10-22).** `D:\wmv-10\checkpoint\runs\p30a\out\stage10_owner_comparison.png`
(`rig\owner_compare.py`): rows noon (n185243, Liquid Detail High), dawn (d185020) and dusk (k184643), all camera hi06;
columns the game, Stage 09 and Stage 10. The viewer panels are 1:1 crops of the captures (x 83-1581, y 57-898 of the
viewport: the field the screenshot covers) and the screenshots are scaled onto that field by exact area averaging of
their stored values, so all nine panels are 1499 x 842 and aligned within 0.06 px (phase correlation); no colour
correction, overlay or retouching. Checked twice: the viewer panels are byte-exact, the reference panels equal an
independent area average within rounding with the same mean colour, nothing is drawn on a panel. The dusk screenshot
was saved at a lower JPEG quality than the other two (visible 8 x 8 blocking).

**Open.** The owner's review of the pictures. The dusk sky's lowest bands (C). Retail's CPU side (one live Retail shot
settles it). The terrain specular's share (C). Distant terrain needs a camera that sees past the resident tiles. Then
steps 6-9 in the order of 7.1; no tuning, fog, water or Stage 11 work has started.

### 13.2 Step 6: outdoor world models and doodads (2026-10-22)

The owner approved steps 1-5 on 2026-10-22 and asked for step 6: the terrain's verified outdoor light adapted to the M2
and world-model shader semantics the client's bytes establish, no correction for this scene; exterior groups and outdoor
doodads lit, interiors and their doodads unchanged until Stage 12; exterior MOCV deferred; the Stage 09 fallback, Models,
Buildings, Characters and the tabard fixes preserved; shadows, ground cover, clouds and water never compensated in the
light. Spec, research and reviews: `D:\wmv-10\step6` (`spec\step6_spec.md`, revision 2, with its review resolution);
implementation notes `D:\wmv-10\step6\impl`; the run `D:\wmv-10\checkpoint\runs\p31a`.

**What the client's programs say** (PROVEN_BYTES unless marked; 6.6, 6.15). Doodads are drawn by seven M2 pixel
containers, byte-identical in Retail, Classic Era and MoP Classic; all 2,688 lit programs were executed, and each matches
one of two closed forms: the hemisphere class (the terrain's ambient bands and unsaturated wrap, on the normalised normal;
the direct term saturated on the raw interpolated normal; no specular) or the flat class. The M2 base is the combiner
times the material colour (the vertex program's 0.5 x colour times the pixel program's 2). World models use the same
light core; their base is albedo x (1, 0.99998, 1) with the vertex-colour gate off, and dropping the MOCV light is the
client's exact lighting-mode-1 output. Which class and light set the CPU binds is not in the bytes: hemisphere was
MEASURED offline on the references before any code (back-facing doodad pixels, game / prediction 0.975-1.026 in every
channel at three times of day; flat 0.55-0.89) and is confirmed on the renders below.

**Built.** Player p31a (Assembly-CSharp 94415A38A0AB3003) from the step-6 sources. Build 0 errors; lifecycle self-test
1,103 passed, 0 failed (27 new cases: the variants against their CPU twins within 2.4e-4 with non-unit and downward
normals, flat arms, tints, unlit and alpha-keyed batches, two-sided faces, the copies' properties, binding and rebinding
of world models (with liquids) and doodads, placement while shown, the driver's reasons and faults); with
`WMV_LIGHT=off` 1,063 passed, 0 failed; pure runner 2,511 passed, 0 failed (the client's hemisphere and flat programs
reproduced within 5e-5 over 54 fixture values; the Retail Stormwind context census as in 2.2); offline compile check:
the original programs identical (6.6).

**Guards** (gating). The Battle.net launcher patched the Retail client from 12.1.5.70448 to 70529 during the run (storage index
rewrites at 19:07, 20:15, 20:57 and 21:07; "files not installed" went from Retail 0 / Era 0 / Beta 16 to 62,974 / 47,056 /
9,609 and back to 0 / 0 / 1,897 on the new build), so only the guard run before 19:07 is compared with the frozen Stage 09
baseline; every later guard compares the step-6 player with the approved steps 1-5 player p30a item by item, each pair
run back to back under one install state (the storage index checked before and after every pair;
`D:\wmv-10\checkpoint\paired_interleaved.ps1`, results in `runs\p31a\paired2`).

| Guard | Result |
|---|---|
| `WMV_LIGHT=off` against the Stage 09 baseline (complete 70448 install) | identical: 0 of 258 must-match items differ (terrain, water and border guards, every view incl. depth, Buildings, Models, Characters, the tallies, the light dumps); the count items changed only by the added tests |
| Light on: Buildings, Models, Characters, paired with p30a | identical: 10 of 10 (the first, unpaired run at 19:08 had failed on files the patch had made unavailable) |
| Default mode (neither `WMV_LIGHT` nor `-wmvWorldLight`), paired with p30a | identical: 0 of 166 must-match items differ (49 regress pictures, far3 varying as always; 13 of 13 tallies; 17 view counts; every view on four clients incl. the 30 depth views); 2 info lines differ by streaming order only (a world model's liquid credited to the other tile it spans; tiles closed at other moments) |
| Light-on streaming run (Stormwind life08), paired with p30a | identical: 70 / 0 / 0 checks, 6 clears with every liquid tally 0 after each; the info line differs by streaming order and GUI timing only (GUI rounds over 50 ms: p30a 0, longest 42 ms; step 6 3, longest 93 ms; earlier repeats without step 6 gave 1-3, longest 57-72 ms, so the spread between runs). Binding as instances are placed: 1,123 bursts, 197.6 ms in all, the longest 7.4 ms; no rebind (the light was on from the start); frames over 33 ms 20 against the baseline's 25 |
| Light on, doodads and world models off, at the seven Southfury cameras and the Crossroads camera (rendered on the 70448 install) | byte-identical to p30a |
| The tile framing (light shown), doodads and world models off, paired with p30a | byte-identical (0 px); p30a against its own repeat 0 px |

**At the seven Southfury cameras** (`out\objects.md`):

| Check | Result |
|---|---|
| Light on, doodads and world models off, against the steps 1-5 player | byte-identical at all seven cameras |
| Step 6 against that arm outside the world-lit pixels (+3 px) and water | byte-identical at all seven cameras: sky and terrain untouched |
| The render against its own model (the albedo and normal views x the light) | every labelled object 100.00 % within 2 sRGB steps, mean signed under 0.7 step (38,000-47,000 eligible px per shot) |
| Back-facing doodad pixels against the game (frozen thresholds) | every object at every shot "matches": k 0.975-1.05 per channel; MAD under 0.1 in red and green (the blue channel, small on these objects, up to 0.47) |
| Time of day (dawn / noon, dusk / noon) against the game | within 0.01 in red and green, up to 0.034 in the small blue channel: "matches" |
| Light-facing pixels at dawn and dusk | "differs: cast shadows not drawn (follow-up milestone)": 96 % lie in the terrain's shadow toward the light, and the client's shadow factor would remove 84-87 % of the squared difference; at noon no shadow signature on the trees (the game's leaves are darker on their sunny side: self-shadowing and SSAO, not drawn) |
| Doodad / terrain luminance ratio at camera hi06, noon / dawn / dusk | game 0.46 / 0.49 / 0.51; Stage 09 0.49; steps 1-5 1.46 / 2.19 / 3.20; step 6 0.48 / 0.52 / 0.55 (every object pixel shadowed: 0.38 / 0.42 / 0.43; the game lies between) |
| The class decider (palm trunks' blue against N . L, free of the direct light, shadows and the flat constant) | hemisphere at all three times (95 % intervals exclude the flat class's 0); the slopes 0.187 / 0.143 / 0.107 are larger than the hemisphere's 0.121 / 0.097 / 0.095 (C: SSAO, which darkens shaded sides, or the PTR's secondary light; not tuned) |
| Modulate M2 batches in view | none |

**World models.** None is visible at the Southfury cameras, so world models are "not compared with the game". At a
viewer-only Crossroads camera (the orc inn, `rig\xr_shots.json`): light on with world models and doodads off equals the
steps 1-5 player; the interior groups seen through the door (22,964 px) and the doodads interior groups list (2,962 px) are
byte-identical to it at 12:00 and 18:46; the exterior groups (468,285 px) match their own model on 99.997 %. Pictures:
`out\stage10_step6_crossroads_1200.png`, `..._1846.png`.

**The tile framing** (the harness's "camera:overview": a perspective framing of the tile from about 990 yd, not the
orthographic overview): the light is shown there, so its world models and doodads are lit and the picture differs from the
steps 1-5 player by design; with world models and doodads off it is byte-identical to the steps 1-5 player (paired, one
install state).

**The owner's pictures.** `out\stage10_step6_owner_comparison.png` (the game, Stage 09 and step 6 at noon, dawn and dusk,
camera hi06; built as the approved comparison, alignment within 0.04 px) and `out\stage10_step6_trees.png` (the trees and
palms at 2x: the game, Stage 09, steps 1-5, step 6), the offline prediction sheet beside them labelled as such.

**Documented, not compensated in the light.** Cast shadows (terrain-cast at dawn and dusk, the trees' shadows and their
self-shadowing at noon) and SSAO: the follow-up milestone of D10. Ground cover: stage 11B. Clouds: D8. Water, its
reflections and sun glints: step 8. Fog and haze: step 7. Exterior MOCV (2.2, deferred). The interim interiors: Stage 12;
a doorway shows the step.

**Known choices carried.** MDDF doodads standing inside an interior group's box take the outdoor light (counted and named
per load). MODD records listed by both kinds or by none, and every record of an incomplete world model, keep today's look.
Additive and unlit batches keep lum 1; modulate batches would be lit as the builder classifies them (none in view). The M2
tint is the Models viewer's (linearised by the engine), unchanged. The light is evaluated at the camera. Doodads: "PTR
12.1.7 (linear 0): Retail M2 equivalence not established; Retail CPU side unverified" until a live Retail shot (T12).

**Open, for the owner.** The review of the step-6 pictures before step 7. The Crossroads capture (PTR `linear 0`, facings
225 and 45 deg, noon and 18:45) would compare world models with the game and settle their class. The exterior MOCV
measurement (Ratchet's lantern wall on live Retail or MoP).

