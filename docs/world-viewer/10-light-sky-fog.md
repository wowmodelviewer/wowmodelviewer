# World viewer 10 -- light, sky and fog (outdoor)

Branch `world/10-light-sky-fog`, from `develop` 678071f0 (Stage 09, PR #127). **Status: the owner approved the
direction on 2026-10-21 with the adjustments of section 0; implementation runs in the order of section 7. Steps 1-5 are
done and the early visual checkpoint ran on 2026-10-22 with a built player (13.1); the owner approved them that day.
Step 6 is done (13.2). Step 7 (fog) is done, its rule corrected from in-game test T7 (13.3, 13.4); the owner accepted it
provisionally on 2026-10-27. Step 8 (water) is implemented and validated (13.5); the owner reviews its pictures before
step 9.** This document
records the owner's decisions (section 0), the investigation (sections 1-5), the
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
  unit (yards or yards x 36, 4.6). T7 (13.4) measured the PTR's fog instead: end min(0.933 x far clip, horizonStart), start
  FogScaler x that end, so 285 yd at the stand with the reference sessions' horizonStart 1,900; the fog end, 1,900 yd there,
  lies beyond the resident terrain along most facings.
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
- **Modern mapping** (INFERRED (public), one source; its start and end are superseded on the PTR by T7's measurement, 13.4):
  start = min(far clip, 3000) x FogScaler (clamped to [-1, 1]);
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

**Graded as built (step 7, 2026-10-23; 6.9, 13.3).**

- **The tail is PROVEN_BYTES and executed.** One fog function ends every terrain (316 Retail and 316 PTR programs), M2
  (2,688 lit programs) and world-model (72) program of the client; the programs were executed against the project's
  pseudo-code over every path, largest error 5.4e-7, and the viewer draws its layer 0. The `dx_6_0` programs the clients
  bind are INFERRED to equal them by structure (terrain and world models carry the same literals; the M2 `dx_6_0`
  containers were not extracted). The fog is the program's last arithmetic: after the light, the specular, the second
  directional term and, on M2, glow, desaturation and the program's own colour-grading LUT; no encode and no clamp
  follow. The sky and cloud programs apply no fog.
- **Which table column feeds which row of the record is INFERRED (public)**, one graded row per constant or rule in
  `WorldLightModel.Fog`:

| Rule | As built | Grade |
|---|---|---|
| Start | FogScaler x min(E, 3000), FogScaler as read (no clamp); E the end below (since 0a028642; before: min(far clip, 3000) x FogScaler) | MEASURED on the PTR 12.1.7 (T7, post-data, stands A and B; 13.4); the cap 3,000 INFERRED (public), it never binds at the default |
| Density | FogDensity x 0.0005 per yard, as read (the 0.9 floor not applied) | MEASURED on the PTR 12.1.7 (T7, m 0.996-1.016 at two densities; 13.4); Retail unverified |
| End | E = min(0.933 x far clip, horizonStart) (since 0a028642; before: the far clip); the linear fade from 0.3 x E | MEASURED on the PTR 12.1.7 at horizonStart 1,000 and far clip 1,000 (13.4); the factor 0.933 at other far clips and the end at horizonStart above 1,000 (so at the default 1,900) ASSUMED |
| Fog colour | SkyFogColor as byte / 255 | INFERRED (public); of the single table colours only SkyFog fits the references' far bands (MEASURED, preliminary: RMS 1.54 steps against 4.3-8.5) |
| Height fog, the end-fog and height colours, the colour ramp, the sun tint | the public rules; the zero-colour, height-curve, kh := k and ramp-length fall-backs resolved per key; the tint in the sky's order | INFERRED (public) |
| Second start term, layers, strength, interior weight | 0; one layer; 1 (the terrain's compiled-in value); 0 (the outdoor record) | held: no table gives them |

- **Where each non-data constant is measured** (9.6: measured at two spots and times, or approved by the owner). The start
  rule and the density: T7 (stand A the fit set, stand B and the dusk pairs the holdouts). The end: T7's low arm. The
  colour fall-backs: T7's measured fog colour at two stands. The height-curve fall-back: one shot at a stand the tables
  pick. kh := k, the ramp length and the polynomial switch: no planned view reaches them. The sun-tint inputs: T4 and T14.
  The blend kinds and the skip: a reference with such a batch in a fogged range. The holds: none. Until then each is a
  labelled default the owner is asked to approve (13.3, open item 2).
- **The preliminary reading** (MEASURED on the six existing PTR references; start and density not separated). The far
  bands are fogged about 1.5 times more than the default predicts. The shots constrain a start-density ridge from about
  (250 yd, density x 0.8) to (350 yd, x 1.2), flat within 0.1 step around its minimum (0.69 steps RMS at 300 yd, x 1.00);
  the 300-450 yd band holds only 258-318 px per shot, so a start near 300 yd is extrapolated, not observed; at the default
  start of 450 yd the best density scale is x 1.75, with structured residuals. Read plainly: a nearer start or a denser
  fog explains it. The default keeps the public rule, labelled, and the difference is class C ("fog start and density")
  until T7 (9.4).
- **Corrected (step 7).** The Stage 09 references are not fog-free: at every Southfury camera 0.9-2.8 % of the frame is
  terrain or trees between the predicted start (450 yd) and about 822 yd (the far-left hill and the ridge tops), and the
  game fogs it (13.3, C1); only the water (within 142 yd) and the nearer terrain lie inside the start. The dome's "fog
  ring" is not a fog term: the client's sky has none, and the lowest ring's lean to EndFogColor is the dome's vertex
  colours, which step 4 already draws, so step 7 changes no sky pixel.

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

**As built (step 7, 2026-10-23; 13.3).**

- **Where.** The client's tail, layer 0, in the project's own code: `Assets/Resources/WmvWorldFog.hlsl`, with its CPU twin
  `Wow/World/WorldFog.cs`. It runs on the lit terrain (`Wmv/TerrainTextured`'s world branch, after the outdoor light and
  before the saturate and the encode) and on outdoor doodads and exterior world-model groups (the world-lit variants of
  6.6, after the lit colour and the alpha line, before the encode). Not fogged: interior groups and the doodads they list
  (until Stage 12, D2), the water (step 8, 6.11), the sky (the client's has no fog), particles (Stage 11), the terrain's
  neutral and diagnostic paths, the overview, every non-main camera, composite and fixture. No new variant, keyword or
  host change; protocol 20.
- **The record.** `WorldFog.Pack` fills the program's 14-row record once a frame from the light state the player already
  evaluates, blends and eases. `Publish` uploads it with the eye (`_WmvWLEye`) and takes the height plane's offset from
  that same eye, so a second render of the main camera in one frame stays consistent. Radial distance from the eye with
  the vertical pull; the game far clip (6.1 rule 3), never the camera's far plane.
- **The near field is the fog-off picture's bit for bit.** The hook returns its input through a select wherever the
  visibility is 1, so every pixel nearer than both the start and 0.3 x the far clip is the pixel `-wmvFog=off` draws
  (step 6's), whatever form the compiler gives the mix (the offline compile check finds the
  select in every compiled program). The mix is not marked `precise`: the modifier would spread to the light-off path's
  instructions.
- **The mode is chosen by client, not per key** (D17, D18), which departs from the text above: Retail and the PTR draw
  the Retail pattern, each key as read (a key with neither FogDensity nor FogEnd draws only the end fade); Classic Era, MoP
  Classic, Classic Beta and any product not listed draw no fog by default. Their candidate rules are arms
  (`-wmvFogClassic=public|power36`, each rule listed and labelled, never compared with the game until T8). Why: Era's maps
  0 and 1 carry Map Flags[2] 0x2, under which the public rule does not use FogDensity as read, and Beta's volume-fog keys
  cannot be told apart per key.
- **Fall-backs per key, the tint in the sky's order.** Every fall-back only the record uses (the zero end-fog, height and
  end-height colours, the height-curve fall-back u^3, kh := k, the ramp length, the polynomial switch) is resolved per
  key as a derived channel of the light sample and blended with it, so the record is continuous in time and position
  (INFERRED (public)). The sun-tint inputs stay the sky's (one function, `SkyDome.SunTint`), so sky and fog cannot
  disagree.
- **Blend kinds**, per copy from its blend state, counted in every binding line:

| Kind | Batches | Fog | Grade |
|---|---|---|---|
| 0 | the terrain | rgb towards the fog colour | PROVEN_BYTES |
| 0 | opaque, alpha-keyed and alpha-blended M2 batches; every world-model material; any blend state not recognised (counted as "unknown blend") | rgb towards the fog colour, alpha unchanged | INFERRED |
| 1 | additive | rgb x vis (towards black) | INFERRED |
| 2 | modulate | rgb towards white | INFERRED (public) |
| 3 | modulate 2x | rgb towards grey 0.5 | INFERRED (public) |
| 4 | premultiplied (One / OneMinusSrcAlpha, M2 mode 7) | rgba x vis, the client's blend-word-4 arithmetic | INFERRED |

- **NoFog and F_UNFOGGED** (M2 material flag 0x02, MOMT flag 0x02) take the client's skip (INFERRED (public)); unlit
  batches are fogged like lit ones, as the client's lighting mode 0 is.
- **Not adopted, data as read:** the density floor 0.9, the FogScaler clamps and floors, the FogEnd floor, the FogHeight
  clamps, the far-clip clamp, map flag 0x2 -> density 1 and the derived density; each is a row of `WorldLightModel.Fog`
  marked "not applied" or "arm only". **No map-ID list and no per-map minima** exist anywhere.
- **runtimeState carries no fog field** (protocol 20's field sequence is fixed): the mode, its label and the record's
  start, density, end and colour are said in the player's state line and the capture log line; the fields join WP 14 with
  the host's reporting. Since the follow-up commit after bfb5b9c4 the player also says the whole published record once
  per settled view ("fog record as published": the 14 rows as uploaded, the eye, the camera's game Z, the origin and the
  main camera's anti-aliasing); log only (13.3, open item 6).
- **The world view's frame and URP's grading LUT** (found by the step-7 review; NOT in this commit; the owner decides,
  13.3). URP's uber post sends every frame through its internal 8-bit LDR grading LUT after the frame decode, which moves
  a value by up to about one step (bytes 40-75 by -0.6 to -1). It is pre-existing (every Stage 10 light-on picture went
  through it) and is not a fog defect. A prepared change, kept outside the repository as a patch, would put each channel
  through that table's inverse in the frame decode (`WmvFrameDecode.shader`, the arithmetic `WmvMain.ClearChannel` already
  applies to the background colour) while the main camera draws the world view with the light shown, with
  `-wmvGradingLut=keep` to keep step 6's frame; every other view, `WMV_LIGHT=off` and default mode would stay as before.

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

**As built (step 8, ca9b9314, 2026-10-27; 13.5).**

- **The data.** With the light shown, the water keeps Stage 09's selection (`LiquidLights`: the zones in the host's order,
  the default light, the point lights weakest first) and Stage 09's validity rule, evaluated at the light's clock instead
  of 12:00: `LiquidWaterDay` takes, for every light with a Stage 09 row, the four water colours of its slot-0 day at the
  half-minute the outdoor light used this frame (`WorldLightDay`: the host's `sample()` rounding), with the row's alphas
  (time-independent LightParams columns, which only Stage 09's rows carry) and its sky slot. A light whose four colours
  are 0 at that time is invalid for the water then; a point light without them is skipped. A census of every map of the
  five clients (PROVEN_BYTES, `D:\wmv-10\step8\scout\census`) found no light that is coloured at some time but not at
  12:00, so no alpha is missing; a few lights off maps 0 and 1 are all 0 for minutes (Retail and PTR maps 2285, 2361,
  2373 and 2375 around 18:00; Era and Beta map 2835 around dawn), where the water shows its type colours, as Stage 09's
  rule says. At 12:00 the water's 19 inputs equal Stage 09's bit for bit at every position (the runner, on the host's
  dumps of maps 0 and 1 of five clients). A world light without one of the four columns keeps the noon rows (counted
  and said). The ease is Stage 09's; a clock jump of the outdoor light, or a switch between the clock and the noon rows,
  snaps it. With `WMV_LIGHT=off` the water is sampled first, exactly as Stage 09 samples it; otherwise right after the
  outdoor light's frame (and again on the noon rows on its fault path).
- **Reflection misses.** A reflected ray that leaves the screen or the depth range, and the uncovered share of a blurred
  or sharp texel at a silhouette, take the sky dome's colour in the reflected direction (`WmvWaterSky.hlsl`: the ray
  against the dome's triangles in its 15-degree wedge, then the sky program's tint; from the same `SkyInputs` the sky pass
  draws, so the sky and its reflection cannot disagree; `SkyDome.TraceWedge` is its CPU twin and equals `SkyDome.Trace`).
  Without a drawn sky (`-wmvSky=off`, no sky colours) a miss keeps Stage 09's colour. The client's own water programs
  never evaluate a sky: their misses take a reflection texture or a constant (PROVEN_BYTES); what those hold is UNKNOWN.
- **Fog.** The client's water programs (procwaterabove and below, mediumwater, water, magma; PROVEN_BYTES,
  `D:\wmv-10\step8\scout\bytecode`) carry the terrain's fog tail and lerp the WHOLE finished colour towards the fog colour,
  the refraction sample included, the alpha carried. The viewer follows the owner's rule instead (a deliberate,
  documented departure): the water's own surface terms -- its colour share and the sky shares of its reflections -- take
  the fog at the water point (kind 0, `WmvWorldFog`), while the scene copy it refracts and the scene texels it reflects,
  already fogged at their own distance, are never fogged again. Where the visibility is 1 the Stage 09 expression itself
  is drawn. The blended paths (Low, the High look's lower side and fall-back) take rgb' = vis rgb + (1 - vis) fogC alpha,
  the alpha carried; opaque liquids lerp (INFERRED). Interior world-model groups' liquids keep no fog and no dome sky (as
  their groups, until Stage 12): a MaterialPropertyBlock while the world-lit binding is on; they still take the clock's
  colours, which are globals. Arms: `-wmvWaterFog=composite` (the client's literal whole-composite form), `=off`;
  `-wmvWaterSky=stage09`; `-wmvWaterLight=noon`.
- **Not changed:** every Stage 09 water constant; `WMV_LIGHT=off` (Stage 09's call order, path and globals); every camera
  but the main camera's perspective world view; the Models, Buildings and Characters viewers. Known limit: with the
  per-fragment display encode (`WMV_DISPLAY=fragment`) the water mixes its fog in linear values, the terrain beside it in
  authored values.

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

Step 7 (read once; a refused value is a Note; none acts with `WMV_LIGHT=off`): `-wmvFog=on|off` (default on; off: no fog
anywhere, and the light-on picture is step 6's byte for byte); `-wmvFogClassic=off|public|power36`
(default off; Classic Era, MoP Classic, Classic Beta and unlisted products only: the D17 candidate rules for side-by-sides,
never compared with the game until T8). `-wmvGameFarclip=N` (default 7,000) is the far clip the fog uses;
`-wmvGameHorizonStart=N` (since 0a028642; default 1,900, the reference sessions' value at view distance 6, not a client
default for every setting) is the horizonStart the fog's end and start read (13.4). Under `-wmvInteriorLight=outdoor` the
interiors that arm binds are fogged too, and the lines say so. There is no start-rule or density arm: T7 decided the start
rule (13.4), changed in 0a028642 with the owner's approval.

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
| 11 | Fog; **owner checkpoint 2** | 10; the P0 fog pairs and the P1 elevated view | visibility RMS 0.03 on the fit set; each far-clip arm checked for what it discriminates (9.4 T7); `-wmvFog=off` reproduces WP 10. **State 2026-10-24 (step 7): implemented and validated against its own model; the fit-set exit waits for T7** (13.3; `-wmvFog=off` reproduces step 6 byte for byte) | 1-1.5 d |
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
- **T7, as the step-7 capture asks for it** (2026-10-23: `D:\wmv-10\step7\capture\CAPTURE_T7.md` revision 3, sent to the
  owner with the helper addon WmvRefNav 3.2; **not captured yet**). The PTR sandbox at `linear 0` with the 2026-10-19
  profile and four changes for the whole session, against the procedure above: Shadow Quality Low, SSAO Disabled and
  `sunShafts 0` (T13's rule for fit-set shots: no shadow cascade, SSAO or sun shaft can change with the far clip, and the
  dusk pairs carry no cast-shadow class), and `screenshotFormat tga` (lossless: the pair test divides small
  differences). The far clip is set with `/console farclip`, never with the View Distance slider; the owner waits
  10 s after every change of `farclip`, `horizonStart` or `graphicsComputeEffects`; the interface is hidden for each
  shot. Stand A `!tele 1335 -3995 148 1` facing 105 and stand B `!tele 1515 -3735 178 1` facing 180, both in tile (39, 29),
  so the viewer keeps the Southfury stand's resident tiles. The noon block: A-7000, A-1000, A-1000b (15 s later: the
  settle check), A-7000b (the repeat floor and the restore), optional A-HS (`horizonStart 1000` at far clip 7,000),
  B-7000, B-1000, B-7000b, optional A-CE0 (`graphicsComputeEffects 0`) last; the dusk block (18:40-18:50, optional, the
  only density contrast): A and B at both far clips. About 13 minutes, 20 with the dusk block. A low shot counts only at a
  far clip of 1,500 or less (1,000 preferred). WmvRefNav 3.2 adds the `t7` set to 3.1: it gates every shot on the far clip
  it needs, `horizonStart`, `graphicsComputeEffects`, the effects-off values and the TGA format, refuses a pair whose pose
  moved, records every setting with each screenshot, and prints the console lines that restore the settings it noted at
  `/wmvref t7`. It was tested offline only.

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
| T7 modern fog (revised 2026-10-23 for step 7; the capture in 9.2) | two hilltops beside the Southfury stand, no fly mode: A (zone 139, LightParams 5985, facing 105) and B (zone 153, LightParams 5944, facing 180); 12:00 and the optional dusk block 18:40-18:50; far clip 7,000 and 1,000 (at most 1,500), with repeat shots; effects off, TGA | the fog mapping of 4.6 from the game's own pairs, without a viewer render: the start and how the far clip enters it (the default rule against no cap, a cap of 2,000, the horizon start, a start independent of the far clip), the end fade at the low far clip, the density at FogDensity about 3. The "second key with a higher density" is the dusk pairs (FogDensity 2.10 at A, 5.25 at B, against about 3 at noon). A negative FogScaler is not covered: no zone or point light on maps 0 and 1 of the PTR has one at 12:00 except Light 14857 (map 0, inner radius 605 yd) and Light 15855 (radius 10-17 yd), and a pair inside Light 14857 is not planned |
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
| step 7 | T7 (two hilltops beside the Southfury stand at two far clips, 9.2 and 9.4; not captured yet) | fog |
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
| D2 | Interior world-model groups and their doodads until Stage 12: today's preview rig plus the outdoor fog by distance; the outdoor light everywhere; or the rig without fog. And world-model-only maps: sky drawn, or the clear colour until Stage 12 | the rig plus fog, decided on side-by-sides (the Crossroads, Razor Hill, map 34) before it is frozen; world-model-only maps keep the clear colour until Stage 12. **Superseded for the fog (step 7, 2026-10-23):** interior groups and the doodads they list stay unfogged until Stage 12, by the owner's rule of 2026-10-21 (section 0 item 1: interior fog stays in Stage 12); this replaces "plus the outdoor fog by distance" |
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
| D19 (amended 2026-10-25: the fog rule's Retail side is graded ASSUMED, "measured on the PTR; Retail unverified", after T7) | Retail parity (the PTR is a different engine branch, 4.0) | one live Retail pair at Southfury noon (P0a); otherwise Retail parity graded INFERRED with "PTR equivalence UNKNOWN" |
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
| `D:\wmv-10\step7` | step 7 (fog, 13.3): the research (`understand\`: `programs.md`, `mapping.md`, `player.md`, `validation.md`), the spec and its review (`spec\`, `review\`), the T7 page and WmvRefNav 3.2 (`capture\`, `refnav\`), the implementation notes, reviews, fix and finish rounds (`impl\`), the grading-LUT proof (`lutproof\`), the union lineage (`union\`, `union2\`); the runs `D:\wmv-10\checkpoint\runs\p32a` and `p32b`, the freeze `D:\wmv-10\checkpoint\frozen` |

## 13. Progress (2026-10-21, updated 2026-10-27)

| Step | State | Evidence |
|---|---|---|
| 1 Mappings and shader interpretation | done: exterior MOCV partly established and kept out (2.2); the references' engine path settled (4.0); the terrain light and the upper sky measured on the Stage 09 shots (4.3, 4.5); helper addon v3.1 with a 20-minute capture page | `D:\wmv-10\step1` |
| 2 Light-off baseline | done: develop 678071f0 rebuilt (host and a player from the project copy), every Stage 09 set and guard re-run twice: identical to Stage 09's outputs (one MoP magma picture has two one-pixel variants from run to run of the same build; the free-clock water flight varies as before) | `D:\wmv-10\baseline` |
| 3 Time of day and the outdoor light state | done: host 063943e0 (schema 23, the store, protocol 20, 14 new checks, 0 failures on five clients, Stage 09's dumps byte-identical); player 28b10e98 (byte parity with the host's day evaluation on five clients in the runner; in a built player the self-test's outdoor-light cases pass and, at every checkpoint camera, the player selects the zone, Light and LightParams the offline port selects) | `D:\wmv-10\step3`, 13.1 |
| 4 The first sky | done: 28b10e98 (the pixel program matches the client's operation for operation; the GPU dome equals its CPU twin within 0.0006 over 12,288 pixels); at the checkpoint within 0.3-0.6 dE00 of the game's clean sky, except the dusk shot's lowest sky (3.2-4.7 dE00 at 10-13 deg, open; 13.1) | `D:\wmv-10\step45`, 13.1 |
| 5 Lit terrain with MCNR normals | done: 28b10e98 (the branch matches the client's hemisphere path; MCNR within 1.65 deg mean of the geometry, seams within 0.64 deg; the GPU branch equals its CPU twin within 0.0005 on five probes); at the checkpoint the render equals its offline model on 99.8 % of the lit terrain within 2 sRGB steps (13.1) | `D:\wmv-10\step45`, 13.1 |
| Checkpoint | ran 2026-10-22 with player p30a (13.1); the owner approved steps 1-5 on 2026-10-22 | `D:\wmv-10\checkpoint\runs\p30a` |
| 6 Outdoor world models and doodads | done, the owner reviews its pictures before step 7: the world-lit variants by include split (6.6), the client's M2 and world-model hemisphere program; exterior groups and outdoor doodads lit while the light is shown, interior groups and their doodads on the builder's materials until Stage 12; exterior MOCV deferred; results in 13.2 | `D:\wmv-10\step6`, `D:\wmv-10\checkpoint\runs\p31a`, 13.2 |
| 7 Fog | done, accepted provisionally by the owner on 2026-10-27: T7 captured and the start and end corrected from it (13.4; 1d1cb4ca, b8acdbf6); before that: implemented and validated against its own model; the client's fog tail, layer 0, on the lit terrain, outdoor doodads and exterior groups; the Retail pattern for Retail and the PTR, off for Era, MoP and Beta (arms only); the near field identical to fog off; URP's grading LUT found to shift the world view by up to a step (a prepared undo, not committed, waits for the owner) | `D:\wmv-10\step7`, `D:\wmv-10\checkpoint\runs\p32a`, 13.3, 13.4 |
| 8 Water | done (ca9b9314), the owner reviews its pictures before step 9: the water on the light's clock with Stage 09's selection and validity, reflection misses on the drawn sky, the client's fog on the water's surface terms only; `WMV_LIGHT=off` byte-identical to Stage 09 (262 of 263 items; the 263rd is Stage 09's own one-pixel MoP magma variant); results in 13.5 | `D:\wmv-10\step8`, 13.5 |

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
| Light-on streaming run (Stormwind life08), paired with p30a | identical: 70 / 0 / 0 checks, 6 clears with every liquid tally 0 after each. Binding as instances are placed: 1,284 bursts, 202.9 ms in all, the longest 4.7 ms (the unpaired light-on run: 1,123, 197.6 ms, 7.4 ms); no rebind (the light was on from the start); the player's frames over 33 ms 26 against p30a's 25. Info only: the geometry lines differ by one unmasked integer timing ("in 1 ms"), and the GUI rounds over 50 ms were p30a 0 (longest 42 ms), step 6 3 (longest 93 ms). Investigated 2026-10-23 (`D:\wmv-10\step6\guiround`): a GUI round is the host GUI thread's wall time (the same host binary in both arms); 92 of the 93 ms passed inside one host log-file write, with no player work there, the player's frames during the other two slow rounds were 19-21 ms, and no slow round falls at a first world-lit binding or draw; 12 interleaved repeats on one install state (4 each of p30a, step 6, step 6 with `-wmvDoodadLight=off -wmvWorldModelLight=off`) gave step 6 0 rounds over 50 ms (longest 42-49 ms) against 0 (41-46 ms) and 0-1 (41-61 ms); life08 runs without step 6 active range 0-3 rounds over 50 ms, longest 41-88 ms. Verdict: run-to-run variation of the host (two independent reviews agree); four repeats cannot exclude a small rise in the rate of rare host stalls |
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
225 and 45 deg, noon and 18:45) would compare world models with the game and settle their class; until then the
Crossroads world models are functionally validated only (against their own model), not compared with the game. The
exterior MOCV measurement (Ratchet's lantern wall on live Retail or MoP); exterior MOCV itself stays with Stage 12
(roadmap 5.1), its possible size in 2.2. The acceptance line "no GUI round over 50 ms" (9): no player meets it at the
life08 run's Models-viewer step (the bear load: over 50 ms in 14 of 36 life08 runs, Stage 09 players included, up to
88 ms), so before the stage closes it needs a reading (the world steps only?) from the owner.

### 13.3 Step 7: fog (2026-10-23/24)

The owner asked for step 7 on 2026-10-23: the fog from the client's data and the client's shader behaviour, as faithful
as the evidence permits (no generic Unity fog, no artistic adjustment, no speculative constant; a value that neither the
bytes nor the game give is off by default or its result is class C); `WMV_LIGHT=off` byte-identical to Stage 09 and
default mode to step 6; Models, Buildings and Characters (with the owner's tabard fixes) unchanged; interiors as they are
until Stage 12 and the water in step 8; clouds, cast shadows and ground cover never compensated by fog; two test lineages
kept apart (the clean Stage 10 player and the owner's union player with the screenshot matte); no deploy until the owner
approves, then the union approach with the existing backup and rollback kept. The research, the spec (revision 2, after
one review round with three lenses and 64 findings), the T7 page, the implementation notes, two code reviews, a results
review and the fix and finish rounds are in `D:\wmv-10\step7`; the runs in `D:\wmv-10\checkpoint\runs\p32a` (validated)
and `...\p32b` (the fix round's, incomplete).

**What the client's programs say** (PROVEN_BYTES, executed; `understand\programs.md`). One fog tail ends every terrain,
M2 and world-model program (4.6), reading a record of 14 rows per layer. The client has two fog behaviours: blend words
0-3 and 5 mix towards the fog colour, and blend word 4 (premultiplied) multiplies rgb and alpha by the visibility; no
Retail program fogs towards white or grey. The sky and cloud programs apply no fog. The fog position is the vector the
vertex program feeds to the projection, the camera at the origin. **Correction:** the M2 programs' own colour-grading LUT
(permutation bit 1) runs **before** the fog (`p_3.asm:312-331`, the tail from line 332), not "after lighting and fog" as
step 6's M2 report said (`D:\wmv-10\step6\m2\m2_report.md`); it stays with the LUT work package (WP 18).

**Built** (6.9 as built, 6.13). `WmvWorldFog.hlsl` (new) and three hooks (the terrain's world branch, the M2 and
world-model variants); `WorldFog.cs` (new: the record, the per-key derived channels, Pack, the CPU twin, the modes and
arms, the per-set census, the blend kinds); the derived block in `WorldLight.cs` and `WorldLightState.cs`;
`SkyDome.SunTint` extracted with identical results; the graded table in `WorldLightModel.cs`; the per-copy fog flags and
kinds in the doodad and world-model managers and one data field in the model builder; the switches and the log lines;
self-test cases in `WmvLifecycleSelfTestWorldFog.cs` (new) and the runner suite `WorldFogTests.cs` (new). The player
validated in full is **p32a** (Assembly-CSharp 4BC9F24BE2CCED24), and the committed sources are p32a's (checked file by
file against its build manifest: 104 of 104 Unity assets equal). After the reviews a **fix round** (2026-10-23) prepared
changes to eight files that are **not in this commit**: the grading-LUT undo in the frame decode with
`-wmvGradingLut=keep` (6.9), the whole published fog record and the main camera's anti-aliasing state in the log, one
formatter for the state line's fog clause, and self-test F17. They are kept outside the repository as a patch
(`D:\wmv-10\step7\commit_prep\fix2_grading_lut_undo_and_fixes.patch`) for the owner's decision on the LUT (open item 3).
Their player **p32b** (3ED075422BE90C5E) is evidence only; its checkpoint run stopped in step 5g (open item 5).

**Functional.**

| Check | Result |
|---|---|
| Build | p32a and p32b: 0 compile errors; WmvMain bound; `WmvScreenshot.cs` the repository's |
| Lifecycle self-test | p32a 1,125 passed, 0 failed (F15 SKIP: this runtime counts no allocation per thread), with `WMV_LIGHT=off` 1,064 / 0; p32b 1,127 / 0 (F17: the frame decode undoes the 32-node LUT exactly, and is the plain decode with `_WmvWLOn` 0 or no table) and 1,064 / 0 |
| Pure runner | p32a 2,296 passed, 0 failed (p31a 2,272), p32b 2,298 / 0: the client's programs re-executed against the CPU twin, the record against an independent port, the per-key channels' continuity, the switches; R12: Pack 0.016 microseconds median (budget 5), 0 bytes over 10,000 calls |
| Offline compile check | the originals (`WmvOpaque`, `WmvWmo`) identical to HEAD's as whole disassembly files; the variants and `WmvTerrainTextured` 0 errors, no new shader warning; the output select in every hook; the terrain program outside the fog branch HEAD's instruction for instruction |
| Logs | 233 player logs (light on, off and default): the only lines matching "warning" or "error" are the world-model parser's "0 warning(s)" count, as in p31a's; the per-set census and the fog clauses of the state and binding lines present |

**Guards** (8.4 A; gating). The launcher patched Retail during the work (12.1.5.70529 for p32a's run, 70575 from the fix
round on), so every comparison with a reference player is a pair run back to back under one install state (the newest
storage index read before and after). The light pins hold on 70575, and six of p32a's pictures re-rendered on it (fog on
and off at three cameras) are byte-identical to its 70529 renders.

| Guard | Result |
|---|---|
| A1 `WMV_LIGHT=off` against the Stage 09 baseline | 255 of 258 must-match items identical; the three others are install state: Classic Beta's ocean picture (290,124 px; the Beta install now lacks 1,897 files, against 16 in the manifest) and the two Retail light dumps' build string. The paired fallback (the Stage 09 player, p31a and p32a back to back) is identical, 33 of 33, all three showing the same Beta difference. p32b: the same three items, and its paired fallback identical, 33 of 33 |
| A2 default mode, paired with p31a | identical, 0 of 166 must-match items differ (p32a and p32b) |
| A3 light on with `-wmvFog=off`, against p31a | p32a byte-identical at the seven Southfury shots, the Crossroads camera at 12:00 and 18:46, the tile framing and the orthographic overview. The prepared LUT undo (not committed, open item 3) would move the light-on frame, so the identity would then hold through its `-wmvGradingLut=keep`: shown at hi06 (the patched code with keep = p32a, 0 px; the patched union player with keep = the deployed player, 0 px); its full set of keep arms (A3, A6, A11) has not run |
| A4 fog on against fog off, by class | the sky core unchanged at the seven shots and the Crossroads camera; the water unchanged at the seven shots. **FAIL as scored by the frozen gate at 14 of 18 cameras**, recorded as failures under the owner's rule and diagnosed rig side by the evidence: at five Southfury shots 9-13 near-field px (a far object seen through a near palm frond: the offline raster has no leaf holes, and the render's implied visibility there, 0.85-0.90, is the far object's model value); at the Crossroads camera 73 near and 56 outside px by the same mechanism, and 804 far pines beyond the raster's 1,000-yd range; at the tile framing 1,547 of 1,740 "interior" px (1,513 of them one raster instance, a particle model the viewer does not show, with fogged terrain behind it) and 17 sky-core px; at the T7 cameras the raster's object set. With the player's own arms the T7 sky core is 0 everywhere, and the near-field changes left are 1-step changes inside the 5-yd margin of the start (stand A), changes of 5 steps or more only within 3 px of the river (stand B, far clip 7,000) and pixels at the gate's edge (stand B, far clip 1,000) |
| A5 the orthographic overview | identical to p31a: no fog there |
| A6 interiors at the Crossroads camera | interior groups (25,867 px) and the doodads they list (2,962 px) identical at 12:00 and 18:46. Every interior pixel there lies inside the 600-yd start, so that interiors cannot fog rests on self-test F11 and the originals' identity |
| A7 light on: Buildings 6/6, Models 2/2, Characters 2/2, paired | identical (p32a and p32b) |
| A8 life08 with the light on, paired | 70 / 0 / 0, every cleared-state tally identical, no warning line; the longest binding 6.2 ms (gate 50); frames over 33 ms 23 against p31a's 22, a pass on this one pair (three more pairs under the budgets) |
| A9 | build, self-test and runner without failure; the log lines present |
| A10 refused values, hard off | `-wmvFog=maybe -wmvFogClassic=bogus` give Notes and the default picture; under `WMV_LIGHT=off` no fog line, 0 px differ |
| A11 Era, MoP and Beta with the defaults (fog off by client) | **FAIL as scored**: the frozen script compares whole windows, where 89-288 px of the Browse panel's load-time text and the status bar's memory figure differ; inside the viewport 0 px differ in 12 of 12 pictures. MoP's `-wmvFogClassic=public` equals fog off at these cameras (its start is 2,400 yd there) |

**Where the fog shows** (p32a). At camera hi06 at noon it changes 46,072 px by up to 35 steps, all in rows 0-285 of the
viewport: the far-left hill and the ridge tops (450-822 yd). The near terrain, the river, the foreground trees and the sky
do not change. The harness's tile framing (about 990 yd out) takes zone 142's light at the camera (LightParams 5951: start
15 yd, k 0.00175 /yd, colour (43, 78, 125)) and turns mostly dark blue: the light is evaluated at the camera (T11 open),
not a defect.

**The render against its own model** (8.5 B, class A: a failure is a renderer defect, fixed, never tuned). The frozen
gates: the share of eligible pixels within 2 steps, the same on the fogged subset, and the visibility domain (per 50-yd
band the median |visibility observed - model|, at most 0.02). The run exposed two limits of the frozen scoring and one
cause on the renderer's side:

- **The object set W comes from the offline raster at the tile framing and the eight T7 cameras**, where 8.4 asks for
  the player's own arms (no render arm existed there). The raster has no leaf holes, draws a particle model the viewer
  does not show, and does not hide submerged rock behind the water surface. The player's own arms were rendered under one
  install state and scored beside it.
- **URP's 8-bit LDR grading LUT** (pre-existing, outside the step-7 diff; found by the second code review). Uber post
  applies URP's internal LUT to every frame after the frame decode; it stores node k / 31 in 8 bits and interpolates
  between nodes, so a byte leaves up to about one step off (40-75 by -0.6 to -1, 83-103 by up to +0.5). The fog targets
  sit in that range (noon stand A's (113, 67, 58) moves by (-0.20, -0.93, -0.83)), and the model takes the fog-off
  picture, already through the LUT, as the lit colour: render - model grows with distance and becomes a visibility bias
  where the lit terrain is close to the fog colour (dusk). **The proof arm** (p32lut, built with the prepared LUT-undo
  patch, evidence only;
  `D:\wmv-10\step7\lutproof`): with `-wmvGradingLut=keep` it is byte-identical to p32a at the three cameras tested, and
  p32a's bytes equal the forward 8-bit LUT applied to p32lut's within 1 step on 100 % of the pixels, so the LUT is the
  only difference.

| Camera (far clip) | Frozen verdict | The player's own W | The LUT undone | Diagnosis |
|---|---|---|---|---|
| the seven Southfury shots (7,000) | PASS: 0.99998-0.999993 within 2; fogged 0.9996-0.99997; visibility worst 0.012-0.015 | (the frozen W) | n185243 (rendered): terrain fogged mean render - model (-0.021, -0.103, -0.171) -> (-0.002, -0.006, -0.001) steps | -- |
| T7 A 12:00 and 18:45 (7,000) | PASS (0.99796, 0.99846 within 2) | -- | -- | -- |
| T7 A 12:00 (1,000) | FAIL: visibility 0.032 at 950-1,000 yd (no signed bias) | FAIL (visibility) | 0.0106 in the second code review's re-scoring of the p32a pictures (not rendered) | the LUT |
| T7 A 18:45 (1,000) | FAIL: visibility worst 0.022; a signed bias growing from +0.0025 (300-350 yd) to +0.0206 (950-1,000) | FAIL (visibility 0.0224) | **PASS** with either W (rendered): worst 0.0141, the bias gone; terrain fogged mean (+0.085, -0.175, -0.180) -> (-0.014, +0.003, -0.009) | the LUT |
| T7 B 12:00 (7,000), B 12:00 (1,000), B 18:45 (1,000) | FAIL (fogged 0.973, 0.985, 0.983) | **PASS** (0.99864, 0.99778, 0.99712 within 2) | -- | the raster's W |
| T7 B 18:45 (7,000) | FAIL: fogged 0.9645; visibility 0.072 at 1,200-1,250 yd | FAIL: fogged 0.98419 (gate 0.99) | FAIL: fogged 0.98433; the terrain's G/B bias (-0.24, -0.32) -> (-0.007, -0.005); visibility worst 0.0110 -> 0.0053 | remaining: cliff rocks at 790-830 yd standing in the river (867 of 1,032 failing object px lie over the water, which is unfogged and whose High reflection differs between the arms); the rock pixels away from the river match |
| the tile framing | FAIL: 0.9354 within 2, mean (0.10, 0.45, 1.47); the terrain alone passes (0.9996) | FAIL: 0.99248 (gate 0.995); the fogged subset passes; visibility 0.046 at 200-250 yd, objects only | not rendered (re-scoring: objects 0.0459 -> 0.0414) | remaining: tree instances at 461-826 yd; 760 object px move away from the fog target, which no fog mix does; the rig's tree geometry is the leading suspect, not proven; not the LUT |

All seven deliberate mutations of the fog (density x 1.3, the plane's sign, hf forced to 0, a linear colour ramp, no end
fade, the eye at the origin, a stale eye) are caught by B. The second code review found no defect in the step-7 diff. B as
frozen passes at 9 of 16 cameras; with the player's own W at 12; with the LUT undone as well, T7 A 18:45 (1,000) passes
when rendered and A 12:00 (1,000) in the re-scoring. The two failures that remain (the river rocks, the framing's trees)
are attributed to the rig by the evidence; neither is proven.

**Against the game** (8.6 C1; PTR 12.1.7 at `linear 0`: Retail's shader path, Retail's CPU side unverified; a re-scoring
of references the research has seen, not a blind test). The frozen rule passes: at all four noon poses the fog moves every
far band of 500 px or more towards the game, on every channel.

| Noon far bands (27) | RMS | Mean signed (R, G, B) | Ratio RMS | Bands with the game more fogged |
|---|---|---|---|---|
| fog off (step 6) | 6.42 steps | (-8.57, -4.84, +3.52) | 0.0697 | 27 |
| fog on | 2.73 steps | (-3.55, -2.41, +1.41) | 0.0293 | 27 |
| the frozen prediction, fog on | 2.75 steps | (-3.58, -2.51, +1.29) | -- | -- |

At dawn the RMS falls from 4.82 to 1.88 steps and at dusk from 5.10 to 2.52 (both carry the cast-shadow class). The verdict
written before the render stands: "fog present and coloured as SkyFog; the game more fogged than the render in every far
band; start and density not separately determined: class C (fog start and density), in-game test T7". C1 was scored on
pictures that went through URP's grading LUT and is not re-scored with it undone; `out\fog.md` prints the spec's
preliminary prediction (5.95 / 2.43 steps) instead of the frozen one. The tile framing and the Crossroads camera have no
game reference.

**The union lineage** (8.7: the owner's union player against the deployed build D, build64's host and player p31u; no
deploy). p32u (p32a's sources with the owner's two-pass screenshot matte; Assembly-CSharp 1461EF88A366E695) and, for the
fix round, p32v (p32b's sources with the matte; 13ECBA51CD69F877), each alternated with D under one install state, the
union host in every arm:

| Item | p32u | p32v |
|---|---|---|
| U1 Characters 2/2 | identical in all four pairings | identical in all four |
| U2 the guild-tabard no-design cases | 9 of 9 pictures 08BF71CC, the matte path in the log | 9 of 9 08BF71CC |
| U3 Buildings 6/6, Models 2/2 | identical | identical |
| U4 self-test | 1,125 / 0 and 1,064 / 0, equal to p32a's, the same check lines | 1,127 / 0 and 1,064 / 0, equal to p32b's |
| U5 life08, light on | tallies identical in all four pairings (70 / 0 / 0) | identical |
| U6 the Maps view at hi06, light on | fog on = p32a (0 px); `-wmvFog=off` = D (0 px) | fog on = p32b (0 px); `-wmvFog=off` differs from D on 87.0 % of the viewport by at most 2 steps (the LUT undo); with `-wmvGradingLut=keep` = D (0 px) |
| U7 the world views, default mode and `WMV_LIGHT=off` | 91 of 91 identical, both | 91 of 91, both |
| U8 the FBX self-test | not needed: no host file changed | the same |
| U9 (optional) a Save-As capture through the matte | not run: no headless route reaches it; U2 proves the matte | not run |

Information: frames over 33 ms in the union's life08 runs were D 21 and 26 against p32u 28 and 25, a run-to-run spread
larger than A8's +2 margin.

**Budgets** (8.3; the step-7 spec's section 10).

| Item | Budget | Result |
|---|---|---|
| GPU, fog and light | at most +0.15 ms a frame (+0.30 with the sky) | **not measured**: the machine was never quiet (2026-10-24 02:33-03:33: GPU 11-27 % busy at its idle clock, a remote-desktop host encoding the desktop; no game client ran). The arm set now exists (`D:\wmv-10\step7\impl\finish\perf\gate_perf_s7.ps1` and `gate_analyze_s7.py`: light off, step 6, fog off, fog on, sky off; the quiet rule checked before every run), not yet run |
| GPU, light off | within noise of p31a | not measured (the same) |
| Capped frame (life08) | frames over 33 ms at most p31a's + 2 a run | **not met, marginally**: three alternated pairs gave p31a 23, 21, 24 and p32a 26, 27, 24 (pair differences +3, +6, 0; pooled +3.0, +2.5 with A8's pair; p about 0.07), the excess on loads 8 and 9; the median frame unchanged (16.79-16.81 ms). p32b's own pair: 23 against 24. Deciding it needs about eight pairs or a pooled rule |
| Player CPU | 0.05 ms a frame, no allocation | Pack 0.016 microseconds median, 0 bytes (runner R12); F15 SKIP in this runtime |
| Render thread | reported | frame p95 median 16.81 ms, 95th percentile 18.99 ms, against p31a's 16.83 / 19.64; over three pairs the 95th percentile +0.27 ms |
| Binding | a rebind at most 50 ms; the first lit frame against p31a's | longest 6.2 ms; binding totals +6 to +16 % a run; the first lit frame 19.4 ms against 22.1 (six more first entries: mean 21.5 against 21.1): met |
| Per-set census | under 1 ms | 0.663 ms at Southfury, 0.138-0.745 ms in life08 |
| GPU memory, host payload | unchanged | not measured; no host change |

**The owner's pictures** (`D:\wmv-10\checkpoint\runs\p32a\out`, mirrored to `D:\wmv-10\checkpoint\out`). The untouched
captures of p32a, built as the approved comparison: viewer panels as 1:1 crops, the game's screenshots by exact area
averaging, captions in strips outside the pictures. The frozen scripts could not carry spec 8.9's captions, so a new
composer (`D:\wmv-10\step7\impl\finish\labels\owner_sheets_s7.py`) uses the frozen construction and adds only the strips;
every panel is byte-identical to the frozen sheets' (100 of 100 proof checks; alignment within 0.04 px).

- `stage10_step7_owner_comparison.png` (6076 x 3159, with `_display.jpg` and one lossless PNG per row): the game, Stage 09,
  step 6 and step 7 at noon, dawn and dusk, camera hi06.
- `stage10_step7_farhill.png`: the far-left hill at 2x, with C1's band residuals per row.
- `stage10_step7_tile_framing.png`: step 6 and step 7 at the tile framing (zone 142, LightParams 5951).
- `stage10_step7_t7_viewer_2x.png` (new): T7 stands A and B at 12:00 and 18:45, step 7 at far clip 7,000 and 1,000 and
  fog off: "viewer only; game shots pending (T7)".
- The seven side-by-sides, and the two research pictures labelled "planning picture, not a render".

Every caption names what is not compensated, the PTR label, "fog start and density: class C until T7", the players, and
that every panel went through URP's grading LUT (p32b, which undoes it, is in none of these pictures); and one fog-kind
line. The loaded set holds colour 14,014, black (additive) 298, white 1, grey 2, fade 2, unknown blend 0, world-model MOMT
blend 2+ 0 and unfogged 38 batches. No blended pixel lies in the fogged set at hi06 or at the T7 cameras; at the tile
framing 144 do, all fogged (88 additive at 901-1,452 yd: a bonfire, a hot-air balloon, lanterns, torches, braziers, a
blue fire and a launch ramp; 56 alpha at 736-963 yd: a waterfall, a quill and fence posts), and 143 of them change, by up
to 216 steps.

**Documented, not compensated by the fog.** Cast shadows and SSAO (the follow-up milestone, D10); ground cover (11B);
clouds (D8); the water and its fog (step 8: the river stays unfogged where the terrain around it fogs, at the tile framing
and in T7's low arm; 2.4's "fog covers the water" applies from step 8); exterior MOCV (Stage 12); the streaming edge (D12:
the fog does not hide it at far clip 7,000); interiors and their fog (Stage 12); particles (Stage 11); `_fogs.wdt` volumes
and the volume composite (D4); the client's volumetric global fog; Classic Beta's volume-fog table (class C); fog layers
1-3 and the interior record; the M2 programs' own grading LUT (WP 18).

**Open, for the owner.**

1. **T7** (`CAPTURE_T7.md` revision 3 with WmvRefNav 3.2, sent on 2026-10-23) is not captured. Until it is, the fog start
   and the density are class C and WP 11's fit-set exit waits. Its frozen analysis (`t7_analysis.py`) then decides the
   start rule, which changes one row of the packer in a follow-up commit.
2. **The step-7 spec's questions 1-3** are not answered yet: proceed before T7 with the default start rule; approve the
   labelled defaults of its 2.3a; T7's capture conditions. Step 7 is built with the recommended answers, every default
   labelled in the log.
3. **The grading LUT.** The prepared patch (not committed) undoes URP's LUT in the world view while the light is shown:
   the frame then shows the client's arithmetic, and the proof arm removes the B residual the LUT caused. It is
   pre-existing and touches every Stage 10 light-on picture, so it waits for the owner. Against step 6 it moves the light-on
   frame by at most 2 steps, mostly +1, on 73-87 % of the viewport pixels (at hi06 the mean moves by (+0.01, +0.15, +0.70),
   the terrain's G and B by +0.5 and +0.68), so "light on with `-wmvFog=off` = step 6" then holds only through
   `-wmvGradingLut=keep`. Keep the undo as the default, or the LUT? The other remedy the review named (the main camera
   skips uber post while the light is shown) is not built.
4. **Rig corrections that need approval** (no frozen file is edited without it): W from the player's own arms at the tile
   framing and the T7 cameras (the fix round renders those arms; the frozen `fogmetrics.py` still scores the raster); A11
   cut to the viewport, as A3 and `compare.py` cut. Noted, not proposed: the 5-yd near-field margin is tighter than the
   raster's distance accuracy at the start, and the own-W arms take in High-path water pixels.
5. **The fix round's clean run is incomplete.** p32b's checkpoint run stopped at 2026-10-24 02:29 in step 5g (its process
   gone; the stale viewer lock was removed after checking, and RandomLooks restored). Its steps 1-4 passed (above) and its
   renders up to 5g exist, unscored; the restated A3 / A6 / A11, A4 and B with the player's own W, and the row-by-row
   check of the logged record (`rig\fix2metrics.py`) have not run.
6. **Logging the full fog record** (done in the follow-up commit, log only): "fog record as published" (the 14 rows as
   uploaded, the eye, the camera's game Z, the origin and the anti-aliasing), once per settled view. Its player p33a
   (Assembly-CSharp CED45AC730540263) is pixel-identical to p32a (32 of 32 viewport shots, paired), the self-test counts
   are p32a's, the runner gains one formatter check, and the logged rows equal the rig's `fogmodel.py` Pack at 18 of 18
   B cameras to float precision (the framing's R2.w against the player's own eye). Taken from the fix round's patch
   without its LUT undo, `-wmvGradingLut` or F17 (`D:\wmv-10\step7\verify\logging`).
7. **A bit-identical early-out** for the near field is not built: the whole tail runs for every lit pixel, while about
   93-95 % of the terrain pixels at Southfury lie nearer than the start. Measure the GPU budget first.
8. **The capped-frame gate** (A8 and the budgets): more pairs or a pooled rule.
9. **The tile framing's dark blue** comes from the light evaluated at the camera (T11). **Drawing beyond a low game far
   clip** (the viewer draws its resident terrain fully fogged where the game draws none) is decided after T7. The
   acceptance line "no GUI round over 50 ms" still needs its reading (13.2).
10. **Not reached by any planned view:** a negative FogScaler (839 Retail keys), the height-coefficient fall-back (6,639
    Retail keys; one shot at a stand the tables pick would cover it), kh := k, the end-fog colour ramp, the blend kinds 1-4
    and NoFog / F_UNFOGGED in a fogged range in game, the M2 and world-model strength. Era, MoP and Beta stay unfogged until
    T8 and the volume-fog table's meaning.
11. **Deploy:** none; build64 still holds steps 1-6. After the owner's approval it follows the union approach (the
    tabard fixes and the screenshot matte kept, a new backup, the existing backups and rollback kept).

### 13.4 T7 and the corrected fog rule (2026-10-24/25)

**The captures.** The owner took T7 twice on the PTR sandbox with WmvRefNav 3.3, which sets the session's settings and takes
each stand's shots by itself: 2026-10-24 14:15 (noon, stands A and B, 8 shots) and a retake 19:21 (noon and dusk, 13
records), `D:\wmv-10\refs\ptr_t7` and `ptr_t7_r2`. The analysis was frozen before each capture; four amendments, each frozen
before the capture it applies to, made it runnable on real captures (`D:\wmv-10\checkpoint\frozen\AMEND_T7W_step7.md`):
capture windows the sandbox clock can set and the light at each pair's recorded time; a reader for the addon's real records;
addon 3.3; and, owner-approved after the first capture, a terrain repeat control instead of the sky-band check that drifting
clouds fail. The retake was analysed blind; its stand B failed the repeat control (grass moved near the camera: class E).

**What T7 measured** (PTR 12.1.7.70493, horizonClip 7,000, stands A and B; the horizonStart analysis is post-data and
exploratory, `D:\wmv-10\step7\hs`, two independent reviews):
- The far clip and the client's **horizonStart** setting both limit the fog. The A-HS shot changes only horizonStart (1,900 to
  1,000; every other recorded setting and the pose equal, in both captures): the start moves to 148.5-149.2 yd (0.15 x 1,000)
  and an end appears at 1,000.6-1,001.9 yd. At far clip 1,000 the end is 933 yd and the start 0.15 x that end.
- **The rule** that fits every pair, and, fitted on the far-clip pairs only, predicts both A-HS shots at the noise floor:
  end E = min(0.933 x far clip, horizonStart); start = FogScaler x E; density = FogDensity x 0.0005 (measured within 1 %, at
  two densities through the dusk pair). The frozen analysis' cap of 1,880-1,886 yd is its own small bias around 1,900 (the
  difference is about 2 yd of start). The rule min(far clip, horizonStart) for both is rejected (the far clip needs 0.933).
- **The fog colour** at stand A equals the record's within dE00 0.1-0.3, the height trend included.

**Measured and assumed, by client** (this is how every grade reads):

| | Retail 12.1.5 | PTR 12.1.7 | Classic Era | MoP Classic | Classic Beta |
|---|---|---|---|---|---|
| The fog shape (the client's programs) | PROVEN_BYTES | PROVEN_BYTES | PROVEN_BYTES (Retail's programs) | PROVEN_BYTES (Retail's programs) | PROVEN_BYTES (the PTR's branch) |
| The start, FogScaler x E | ASSUMED (the PTR's CPU side; Retail unverified) | MEASURED at horizonStart 1,000 and 1,900 and at far clip 1,000 | not applied (unfogged by default, D17; arms only) | not applied (unfogged by default, D17; arms only) | not applied (volume fog, D18) |
| The end, E | ASSUMED | MEASURED at horizonStart 1,000 (far clip 7,000) and at far clip 1,000 only | -- | -- | -- |
| The factor 0.933 | ASSUMED | MEASURED at far clip 1,000 only (a factor and an offset of -67 yd fit equally) | -- | -- | -- |
| The end at horizonStart 1,900 | ASSUMED | extrapolated from horizonStart 1,000 | -- | -- | -- |
| horizonStart other than 1,000 and 1,900, horizonClip other than 7,000 | ASSUMED | not measured | -- | -- | -- |
| Density x 0.0005 | ASSUMED | MEASURED (stand A; two densities) | -- | -- | -- |
| The fog colour (SkyFog and the height colour by the public rules) | ASSUMED | MEASURED at stand A only (dE00 0.1-0.3); INFERRED (public) elsewhere | -- | -- | -- |
| The colour ramp and the polynomial path reading the far clip | ASSUMED | not measured | -- | -- | -- |

**Built** (0a028642): `WorldFog.EndOf` and `Pack` (R0.y = E, R0.x = FogScaler x min(E, 3000)), the switch
`-wmvGameHorizonStart` (default 1,900), the graded rows, the fog clause naming the far clip, horizonStart and E with their
grades; runner R15 (far clip 1,000 / 7,000 x horizonStart 1,000 / 1,900 / 2,500 / 3,500). The committed text is 1d1cb4ca
(first committed as 0a028642, its message corrected). Validated with p34a, built from 24ce4a86 plus the change minus one
sentence of the fog clause ("the end at horizonStart 1000 only, above it ASSUMED"; the runner passes 2,558 / 0 on the commit,
no player was rebuilt from it), every pair against the committed p33a under one install state: WMV_LIGHT=off, default mode
(139 of 166 items comparable on the install), -wmvFog=off, Buildings / Models / Characters, the overview and the interiors
identical; the owner's union player p34u keeps Characters and the guild-tabard cases (08BF71CC) identical to the deployed
build; self-test 1,125 / 0 and 1,064 / 0; the published record equals the rig's model at 18 of 18
cameras. The full gate table: `D:\wmv-10\checkpoint\runs\p34a\out\fog_p34a.md`.

**Against the game.** At Southfury the corrected rule (p34a) moves the far bands to 0.48 steps RMS of the game at noon
(committed rule 2.73), 0.93 at dawn (1.88) and 0.84 at dusk (2.52); it is nearer the game in 108 of 117 far-band values
(`D:\wmv-10\checkpoint\runs\p34a\out`, sheets in `out\sheets`). The Southfury references were seen before T7 but never used to
fit horizonStart or the factor. The first capture's stand B WAS used in the fit; only the retake's stand B (class E: grass
moved) was not. There is no fully independent blind holdout. The formal WP 11 metric is 0.0048 and 0.0053 on the fit set (T7
stand A, the implemented viewer at the fitted cameras), under its 0.03 (the committed rule: 0.075-0.078) -- met IN-SAMPLE:
the rule was fitted post-data on these pairs and pixels, so this shows the viewer reproduces the fit, not an independent
validation. The only T7 pair outside the fit, the retake's stand B noon (class E, information only), gives 0.0335: the near
bands where the grass moved, and a far-band miss at 900-950 yd (in the far-clip-1,000 end region, unrelated to the grass).

**Still open (UNRESOLVED):** the render-against-model check (B, amendment 1, thresholds unchanged) fails at a1200_f1000 and
a1845_f1000 (800-1,000 yd, 0.026 / 0.027) and, new with the corrected rule, in one band 950-1,000 yd at a1200_f7000 (0.0209
against 0.02). Step 7 attributed similar dark-band residuals to URP's 8-bit grading LUT (13.3); that attribution was NOT tested
for this player, so these stay unresolved, together with the retake-B 900-950 yd miss (one viewer-against-model gap near E at
far clip 1,000). The tile framing passes B only with amendment 1's C6 exclusion (0.99249 without it; documented, unresolved).
Classic MoP and Classic Beta were not comparable on the install (MoP's storage did not open; Beta left .build.info). The
performance gate is open (the machine was never idle); life08 frames over 33 ms were 25 against 22 (p34a / p33a) and 21 against
18 (the union pair), above the +2 line (information only); both players fail the same five harness steps under the install.
The grading-LUT bypass waits for the owner (opt-in, not committed).

### 13.5 Step 8: water with the new sky, light and fog (2026-10-27)

**The owner's brief** (2026-10-27, after accepting step 7 provisionally): the water on Stage 10's evaluated light with
Stage 09's selection and validity; the drawn sky for reflection misses; the client's fog on the water's surface terms,
the refracted scene never fogged twice; every Stage 09 water constant kept, no adjustment for the Southfury comparisons;
`WMV_LIGHT=off` byte-identical to Stage 09 (`WMV_WATER=low` included), Models, Buildings, Characters and the original
shaders unaffected; the noon invariant, the time of day, reflections, transparency, fog, streaming and lifecycle
validated; WoW / step 6 / step 7 / step 8 comparisons at noon, dawn and dusk with water close-ups and a visible water fog;
step 7's renderer/model mismatches kept UNRESOLVED (a short isolated grading-bypass comparison allowed); the GPU gate,
the independent fog holdout and the Classic validation carried into step 9; the tests on the exact committed source.

**Built** (ca9b9314; 6.11 as built). Evidence and scripts: `D:\wmv-10\step8` (design, scouts, the census, the client's
water programs, the review of 14 findings, 12 fixed before the commit). Every result below is from the player built from
the commit's own archive, p35a (its Assembly-CSharp 36CBA239C8A6F879 equals the reviewed dev player's), against players
built the same way from b8acdbf6 (step 7, p35s7) and f8265c6c (step 6, p35s6), and Stage 09's p30base, under one install
state per unit (Retail 12.1.5.70575, PTR 12.1.7.70493, Era 1.15.9.70576, MoP 5.5.4.70579; Classic Beta is no longer
installed).

| Check | Result |
|---|---|
| The runner | 2,598 / 0 / 0 skipped; the 12:00 invariant on the host's dumps of maps 0 and 1 of five clients: the 19 inputs bit-identical at every row and position (16,236 positions), no noon fall-back; 48 half-hours: validity never changes there; the sky's wedge twin equals `SkyDome.Trace` within 1.1e-11 over 53,871 directions per table |
| The self-test (p35a) | 1,138 / 0 (default mode) and 1,065 / 0 (`WMV_LIGHT=off`); against step 7 four check lines reworded by design, 17 new (the water-sky probe within 9e-7 of `SkyDome.Trace`; the High, zone and fall-back paths against their CPU twins, a reflection hit far in the fog, the encode; a pixel inside the fog start, the fog arm off, the fog not drawn and an interior liquid bit-identical to the light-off output; the driver on a pinned 09:00, a clock jump, the fault path, the light switched off; the interior liquids' block set while bound and released) |
| `WMV_LIGHT=off` against Stage 09 (p30base) | 262 of 263 compared items identical under one install state (41 units, 91 min): tt 4/4, the light dumps 16/16, the regress pictures 48/48 and tallies 13/13, `WMV_WATER=low` pictures 47/48 and tallies 8/8, the views (High 39/39, Low 35/35, counts 17/17), Buildings 5/5, Models 2/2. The one item, a MoP magma terrain shot from above at `WMV_WATER=low`, differs in one pixel: Stage 09's own run-to-run variant (the baseline manifest's accepted alternative, five of ten Stage 09 runs give each value); the script's verdict reads DIFFERENT and stays so. Not guards: far3 (a free clock) and a Characters tabard on the reference side (host randomness), as before. MoP compared for the first time |
| The guards (light on: Models, Buildings, Characters; the default mode) against step 7 | IDENTICAL: light on, Buildings 5/5, Models 2/2, Characters 2/2 (one Beta building has no picture on either side: not installed); the default mode 166 of 166 items, the full set for the first time (MoP and Era now render). Not guards, shared with the reference: far3, and life08b's geometry lines 165 against 166 (streaming timing; the same split in Stage 09's own baseline runs) |
| life08, light on, against step 7 | PASS (harness 70 / 0 / 0 on both; every cleared-state tally identical, liquid tallies 0 after every Clear; longest binding 4.6 ms); the liquid-light lines extended by design; frames over 33 ms 24 against 22 (information) |
| The time sweep (48 half-hours) | PASS (24 min): 48 half-hours at Southfury (hi06, hi11) and on the open sea, run twice: every picture (108 and 54) and light line identical, every sample at the time asked for; `-wmvWaterLight=noon` equals the clock at 12:00 (0 px: the noon invariant on pixels) and departs from it at every other time (at hi06 up to 32 levels darker at night, 18-37 lighter at dawn and dusk) |
| Stage 09's water scores (the four High cameras, noon, light on) | against step 7 (p35s7): colour 4.51 -> 4.77, texture 1.93 -> 2.13, reflection 1.01 -> 1.21 (each worse by more than the 0.1 z of WP 13), bed 3.44 -> 3.45, sparkle 1.18 -> 1.19, distortion 3.45 -> 3.45 (Stage 09 frozen: 4.47, 2.33, 1.48, 23.86, 0.97, 3.44). The arms trace all three to the dome sky on reflection misses (`-wmvWaterSky=stage09` restores step 7 at three cameras exactly); the colours at the shot time move each family by 0.01 at most and the fog by 0 (the river lies inside the fog start, 285 yd). The game's noon sky there has a haze band the viewer does not draw (clouds, D8): Stage 09's own table classes sky and haze B, but whether that explains these moves is the owner's decision; NOT counted as passed |
| Streaming and lifecycle (Stage 09's acceptance runs, light on and off) | PASS (72 min, 26 units paired with step 7): light on 13 of 13 and light off 13 of 13 after one paired repeat (a single 52.3 ms GUI round at one wait, 33.7 ms on the repeat: timing) -- Retail, the PTR, Era and MoP (MoP opens again); the harness counts equal the reference's, every liquid tally 0 after every Clear, no exception, error or liquid refusal, Stage 09's liquid-light lines unchanged (the clock's lines added) |
| The owner's union player (p35u: the commit plus the screenshot matte) | p35u (built from the same archive with the owner's two-pass screenshot matte) against p35a and the deployed player p31u: self-test 1,138 / 0 and 1,065 / 0 on both; with fog on the union equals the clean player and its isolation arm (`-wmvFog=off` with the three water arms) equals the deployed picture, 0 px at four pairs (step 6's picture, now tested); the guild-tabard cases 9 of 9 = 08BF71CC; Buildings 5/5, Models 2/2, Characters 2/2 and light on 9/9 identical to the deployed player; the world views 91 of 91 (rerun: the MoP install updated to 5.5.4.70631 at 15:21 during the first try); life08: the liquid-light lines extended by design, one GUI round of 59.8 ms shared with the deployed player (56.8 ms at the same step) |
| Performance (limited) | fxc: the water's pixel program 521 -> 1,635 slots (static; with the step-8 conditions false it compiles to the old program), the vertex program 12 as before; life08 frames over 33 ms 24 against 22 (within +2), stream frame p95 16.77 ms against 16.78, materials and meshes equal after every Clear, peak textures +5. The GPU pair is INCOMPLETE (the machine was not idle: a game client running): the full GPU gate goes to step 9 |

**Against the game** (untouched aligned grids, WoW / step 6 / step 7 / step 8, `D:\wmv-10\step8\rig\sheets\final`;
viewer minus game RMS on the water pixels in sRGB levels, numbers only, nothing tuned):
- **Visible water fog** (T7 stand B, Liquid Detail High): at noon the water beyond 600 yd moves from 15.29 (step 7: the
  river unfogged where the terrain around it fogs) to 11.53; at dusk the water at 667-1,127 yd from 47.81 to 38.06. The
  arms at the same cameras: the client's literal whole-composite fog 12.09 and 38.98, no water fog 15.96 and 43.31 --
  the surface-only rule is nearest the game in both (`D:\wmv-10\step8\rig\sheets\arms`).
- **Southfury** (the river lies inside the fog start at every camera, so step 6 and step 7 water are identical): noon
  (High) 20.33 -> 20.01; dawn (Low) 23.13 -> 23.29 (the water 25-60 yd 19.48 -> 17.78 and beyond 60 yd 16.66 -> 11.49, the
  nearest 25 yd 23.70 -> 24.12; its mean moves from 4 levels darker to 6 lighter than the game); dusk (Low; a JPEG at
  quality 3) 15.88 -> 19.42, lighter than the game at every distance. The close-ups: hi12 19.13 -> 19.01; hi11 17.95 ->
  17.96, its water beyond 60 yd 19.62 -> 23.90: the dome sky in the reflected direction is darker than Stage 09's noon
  SkyBand1Color, and the game's bright sheen there is not the dome's (its reflection source is UNKNOWN, 6.11).
- The data drive these moves: the colours at the shot's time, the dome sky, the fog. They are reported, not compensated
  (D5: any retune of Stage 09's water is a follow-up the owner approves).

**The grading LUT (the owner's item 7, investigation only; nothing committed).** The opt-in bypass of 13.3 rebased onto
b8acdbf6 (player p35byp, `D:\wmv-10\step8\byp`): without its switch every picture equals step 7's byte for byte; with it,
under one install state and amendment 1's scorer with its thresholds, the three B failures of 13.4 pass (a1200_f7000's
950-1,000 yd band 0.0209 -> 0.0106; a1200_f1000 and a1845_f1000 every band 800-1,000 yd 0.021-0.027 -> 0.007-0.014), both
passing controls stay passes, and step 7's frame equals the LUT applied to the bypass frame within 1 step at every pixel.
This ties those three render-against-model residuals to the LUT's 8-bit quantisation in dark, strongly fogged far bands;
for the frame as committed they stay UNRESOLVED, and nothing else (the framing's C6, the retake's 0.0335, Southfury, the
game) was tested this way.

**Still open (UNRESOLVED or carried into step 9):** step 7's B failures at the three T7 cameras (above), the framing's C6
and the retake's 0.0335; the full GPU performance gate (the machine was never idle); the independent fog holdout; Classic
Era and MoP visual validation (no references) and Classic Beta (no longer installed); the client's own water reflection
source and its below-surface fog (slot 1, WP 16); the Southfury dawn/dusk and hi11 moves above (class C until the
client's water colour mapping, TEXCOORD1 and cb1[20]/[21], is measured).
