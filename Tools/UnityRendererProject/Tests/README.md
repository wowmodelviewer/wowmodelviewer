# Parser tests

Framework-free tests of the Unity project's pure parsing layer (`Assets/Scripts/Wow`, no UnityEngine):

- `WowParserTests.cs`: M2, skin, BLP, WMO and the model-space coordinate conversion.
- `WorldParserTests.cs`: the world parsers (`Assets/Scripts/Wow/World`) on hand-built bytes: WDT, split ADT,
  terrain coordinates and vertex layout, holes, alpha storage, placements, tile assembly, and truncated or
  randomly corrupted input. Its byte builders live in `Assets/Scripts/Wow/World/AdtSynthetic.cs`, shared with the
  terrain tests and the player's self-test.
- `TerrainMeshTests.cs`: the terrain mesh builder (`Assets/Scripts/Wow/World/TerrainMesh.cs`) and the mesh dump writer
  (`TerrainDump.cs`) on synthetic roots: the pinned coordinate expressions and the world-to-Unity map, counts, every
  single hole bit, cell structure and winding, asymmetric ramps (orientation), normals against an independent
  recomputation, referenced-vertex bounds, the tile-identity tolerance, every rejection path, thread safety.
- `TerrainCorpusTests.cs`: the same builder on the pinned tiles of the real client bytes (stage 03 design, section 7):
  inputs checked against the pins, root and minimap FileDataIDs taken from the WDT, exact counts and hole masks, the
  structural checks on real heights, and parse/build times. It runs when `WMV_WORLD_AUDIT_CORPUS` is set (otherwise
  SKIPPED). With `WMV_TERRAIN_DUMP_DIR` set it writes each pinned tile's mesh there (`runner_<client>_<x>_<y>.txt`)
  for `Tools/world-audit/compare_terrain_mesh.py` and `minimap_orientation.py`; with `WMV_TERRAIN_ALL_TILES=1` it
  also builds, checks (and dumps) every root tile of every client's corpus window.
- `TerrainMaterialTests.cs`: the terrain material data (`Assets/Scripts/Wow/World/TerrainMaterial.cs`, stage 04 design
  sections 1 and 3 with the v3 delta) and the BLP header API (`BlpDecoder.ReadHeader`) on synthetic tex0/root/BLP bytes
  from `AdtSynthetic.cs`: every chunk state alone and the precedence over all combinations of the layer conditions (as
  built and with a failed height texture), 1..8 layers drawn and 9 (appended to a parsed chunk, since the parser rejects
  more than 8) as layerCountUnsupported, RLE / raw 8-bit / raw 4-bit alpha, alpha sums of 255 and 256, the MPHD 0x84
  cross-check and the 0x80 height-texture switch, the effect rule (MCLY and MTXP bits), the MDID / MHID / MTXP table
  rules, MCCV (B, G, R / 127, flag 0x40, none for neutral chunks), texture scales 0..4 (8 / 2^s repeats per chunk as a
  float, with the chunk's UV phase (frac(ix * r), frac(iy * r)) on every chunk), an MTXP scale nibble above 4 and MTXP
  height values outside the height mix's domain (H below 0 or a float32 overflow, MPHD 0x80 only) as
  layerEffectUnresolved with their precedence and boundaries, missing textures and height textures (id 0 and failed
  reads), the diffuse and height texture ids in first use (plain-only ids without layers of unresolved repeats), the
  alpha slice layout and orientation (a second slice for alpha layers 5..7 of a chunk of 6..8 layers) and slice
  numbering, and the compositor's height mix against hand-computed values (cutoff, heights on and off, 6..8 layers,
  ties, S == 0, S < 0 at the weights function, a single layer, the height texture's alpha at the layer's coordinate,
  nibble 4 at even and odd ix and iy, plain weights as the labelled alternative, the checker for a layer of unresolved
  repeats).
  It also checks `BlpDecoder.Decode`'s opt-in exact-blocks DXT decode (bit-replicated 565, interpolants and DXT5 alpha
  ramps rounded to nearest, DXT1 three-colour midpoint ties to even) against hand values, with the default decode
  pinned unchanged.
- `TerrainMaterialCorpusTests.cs`: the same builder on the pinned tiles and chunks of the real client bytes (stage 04
  design section 7, census alpha SHA-256 values): inputs checked against the pins (or, for tiles extracted for stage 04
  under `<corpus>/materials/<client>/stage04_core`, `stage04_golden`, `stage04_nibble4` and `modern_probe`, against
  pinned SHA-256s and the extraction manifest with its build), the MAID FileDataIDs and MPHD flags from the WDT, chunk
  states, counters, texture and height-texture lists, slices, pinned chunks (on Retail map 2923 32_24 with the layers'
  repeats and UV phase and the alpha of layers 5..7 from the second slice), the height textures of Retail 30_49, the
  composite rule against the game's baked map textures (the height mix against plain weights and the opposite MPHD 0x80
  reading, 4x4-block RMSE; on Retail 30_49's height-textured chunks also against heights sampled at 8 repeats instead
  of the layer's MTXP repeats, which must fit strictly worse; on Retail map 2923 32_24, over every drawable chunk, the
  chunks with an MTXP scale nibble 4 layer and the chunks of 6..8 layers, also against nibble 4 with chunk-local UV, as
  1 repeat and as 8 repeats, which must fit worse by more than the allowance), build times, and the header and mip table
  of every extracted terrain BLP. It runs when `WMV_WORLD_AUDIT_CORPUS` is set (otherwise SKIPPED).
- `WorldCorpusValidation.cs`: the world parsers on the real client bytes of the world-format audit
  (`docs/world-viewer/00-audit.md`). It first checks every input against
  `Tools/world-audit/tests/corpus_hashes.json` (243 files, 19 file sets). It then recomputes the audit's tables and
  verdicts from the C# parse results and writes them to `<corpus>/results/csharp_parsers.json`. It runs only when
  `WMV_WORLD_AUDIT_CORPUS` names the extracted audit data; otherwise it reports SKIPPED, which is not a pass.
- `WorldModelTests.cs` and `WorldModelCorpusTests.cs`: the world-model placement layer of stage 06
  (`docs/world-viewer/06-world-models-in-world.md`), synthetic and on the world06 corpus.
- `DoodadTests.cs`: the doodad placement layer of stage 07 (`Assets/Scripts/Wow/World/DoodadScene.cs`,
  `docs/world-viewer/07-doodads.md`) on hand-built records: MDDF by uniqueId and the Tile frame with worked numbers, the
  doodad-set selection, the composition of a MODD record with a moved, turned and scaled world model, R(q) against its
  transpose, unsupported records, MODR membership and doodads of group files that could not be read.
- `DoodadCorpusTests.cs`: the same on the world07 corpus (`Tools/world-audit/world07_corpus.py`, under
  `<WMV_WORLD_AUDIT_CORPUS>/world07`): every MDDF doodad's placed M2 box against the client's MLDX box on four clients,
  the plan's counts against the census, the doodad-set and MODR facts, the MODD rotation convention measured against the
  world models' walls (the counts of an independent probe pinned), and reference positions computed independently.
- `LiquidTests.cs`: the liquid layer of stage 09 (`docs/world-viewer/09-liquids.md`) on hand-built bytes from
  `AdtSynthetic.Mh2o` and `WmoSynthetic.Mliq`: the MH2O decode (the four vertex formats by the vertex block's size, exists
  bitmaps with spare bits, partial and multi-layer instances, attributes, every malformed structure reported per chunk or
  instance, random corruption), the format rule (`LiquidFormat`), the terrain liquid mesh (positions on the terrain's grid,
  winding, uv0 planar on the game's axes and stored with the u16 wrap, uv1 with the depth in yards by the second word, seams,
  counters), and MLIQ (the size equation, tiles, the group liquid type rule, the group-frame mesh).
- `LiquidCorpusTests.cs`: the same on real client bytes: every MH2O instance of the pinned corpus windows, the exit tiles
  (Retail 29_48, Era 36_49) with exact counts, a byte walkthrough, the terrain grid and the seams; and, when
  `WMV_LIQUID_EXAMPLES` names the stage 09 investigation's MLIQ examples folder (`manifest.json` plus
  `<client>/<FileDataID>.bin`, pinned by the manifest's SHA-256), every example group against the investigation's census.
  It runs when `WMV_WORLD_AUDIT_CORPUS` is set (otherwise SKIPPED); without `WMV_LIQUID_EXAMPLES` the MLIQ part is SKIPPED.
- `LiquidLightTests.cs`: the zone light the water and ocean colours follow (`Assets/Scripts/Wow/World/LiquidLights.cs`): the
  polygon test with both windings and a concave polygon, the Z band, the zone order, the default light, an invalid base light,
  point lights weakest first and their weight ramp, the Durotar values at 12:00 from a small fixture of the client's rows, the
  validation of the host's entries, the ease with its snap rules, and the High look's optional sky (read, carried through the
  selection by the base light and the point lights that carry one, eased, snapped when it comes or goes); and the mesh inputs of
  the shader's zone path: the depth
  byte's W and O scales (`LiquidFormat.DepthYards`, bytes 0 and 255 included) and the planar uv on the game's axes at twice the
  repeats (`TerrainLiquidMesh.PlanarUv` pinned).
- `LiquidHighTests.cs`: the High water look's pure parts (`Assets/Scripts/Wow/World/LiquidHigh.cs`): the HIGH path's formulas on
  numbers (the transmission T(Ls) with the start values against hand-computed values, the water colour's depth ramp and gain under
  a zone light and, without one, the type's colour times the no-zone gain saturated, Schlick, the refraction ramp and the rule that
  accepts the shifted sample, the refracted colour, a reflection texel's coverage, the reflection mix with its shares limited and
  the output saturated, and the ripple map's offset for the floating origin), the `-wmvWaterTune` string (start values, overrides,
  every reason a token is ignored, the log line) and which command-line and `WMV_DEBUG` tokens are its pairs, which side of a sheet
  the camera sees (the HIGH path by the triangle's facing: a sloped sheet rising above the eye, flat water, no normal), and the
  ripple map's unit-normal check with its slope spread, strength factor and mean slope (taken out: mean 0 after, the spread kept).
- `WorldLightTests.cs`: Stage 10's outdoor light state (`docs/world-viewer/10-light-sky-fog.md` 6.4; `Assets/Scripts/Wow/World/`
  `LightGeometry.cs`, `WorldLight.cs`, `WorldLightState.cs`, `WorldLightModel.cs`): the zone and point geometry shared with the
  liquids (moved from `LiquidLights.cs` unchanged), the host's "worldLight" object as the player holds it (the column map, unknown
  and absent columns, the 24-bit colour mask, refused days and params), the day on the integer half-minute (the host's
  `LightCatalog::sample()` rounding floor(a + (b - a) f + 0.5) by hand, wrap past midnight against the clamp arm, a single key,
  floats linear, integers from the earlier key), a small fixture of the client's rows (Retail LightParams 5985, the Southfury
  stand's day: the plan's named values), the selection with Stage 09's rules (zone order and Z band, the default light, an invalid
  light without keys, the slot fallback, copy rows -- two LightParams naming one day --, point lights weakest first and the
  debug arms, on a case where all three orders differ), the sample's channels and groups, the clock (the half-minute from
  HH:MM:SS, running and wrapping, the map's TimeOfDayOverride and its 16-bit -1), the ease, the switches (steps 3-6) and the
  graded hypothesis tables (the light direction, the sun path, the dome's rings, the sky, the terrain light and the model light). With
  `WMV_WORLD_LIGHT_FIXTURES` naming the offline fixtures (`<client>_map<m>.json` in the spec's format,
  `parity_<client>_map<m>.json`, the survey's `spots.json` and `manifest.json`, whose SHA-256 is pinned in the test and which
  pins every other file; written outside the repository from the extracted DB2s) it also checks, on maps 0 and 1 of the five
  clients, byte parity of every light's slot-0 day with the generator's offline port of the host's `sample()` (not the host
  itself) for its five colours (1440 among ten half-minutes) and the spot selections and every column of every light at
  00:00, 06:00, 12:00 and 18:00 against the survey's dumps (otherwise SKIPPED); with `WMV_WORLD_LIGHT_HOST_DUMPS` naming a
  folder of the host's own objects, each is read through the same path, checked against the host's own Stage 09 `sample()`
  rows and compared with the fixture of its client and map.
- `SkyTerrainLightTests.cs`: Stage 10 steps 4 and 5 (`docs/world-viewer/10-light-sky-fog.md` 4.3, 4.5, 6.8, 6.10;
  `Assets/Scripts/Wow/World/SkyDome.cs`, `TerrainLight.cs`): the sky's colour dome (the cubic construction against the public
  table A the spec lists and table B, 122 vertices and 240 triangles, each ring's elevation and horizon ramp, closed around its
  centre), the day curve, the sky's inputs (the end-fog lean, the zero-colour rules), the sky program and its CPU twin (against
  the profile's interpolation along a vertex longitude), the MCNR decode (on synthetic bytes and through the parser and the
  mesh builder -- the bytes kept only when the build asks, and counted --, and the fallback to the geometry normals) and the
  client's terrain light (the unsaturated wrap, the horizon and ground colours and their zero fallbacks, the normal used at
  its own length, the flat variant). With `WMV_MCNR_TILES` naming a folder holding the four
  Southfury root ADTs of Retail 12.1.5 (`786420.bin`, `786640.bin`, `786425.bin`, `786645.bin`: Kalimdor 39_29, 40_29, 39_30,
  40_30, each pinned by its SHA-256) it proves the MCNR mapping again against the tiles' geometry normals (mean under 3 deg,
  the best of the 48 axis orders and signs), measures the decoded lengths the light uses, and checks their four seams (under
  1 deg: the data's continuity and the vertex match, not the decode); otherwise that part is SKIPPED.
- `WorldModelLightTests.cs`: Stage 10 step 6 (`docs/world-viewer/10-light-sky-fog.md` 2.2, 6.6, 6.7, 6.15;
  `Assets/Scripts/Wow/World/ModelLight.cs`): the outdoor light on doodads and exterior world-model groups against the client's
  own M2 programs executed at the Southfury light (the hemisphere and the flat class, 54 rows each at 11:57, 06:49 and 18:46,
  embedded to four decimals), the doodad light equal to the terrain's with a unit normal and to base x Factor with every hold
  neutral, a raw interpolated normal shorter than 1 (H and the wrap normalised, the direct term raw), the zero-colour fallbacks,
  N.L < 0, unlit, the shadow on the direct term only (not the terrain flat class's placement), AO, the local light's combine,
  the world model's base albedo x (1, 0.99998, 1) and its flat arm; and the context rule on synthetic world models
  (`WmoSynthetic.ContextRoot` / `ContextGroup`: exterior, interior, both and neither flags, MODD records listed by each kind,
  by both, by none, an incomplete world model, MDDF, the census, MDDF doodads inside an interior under a turned, tilted and
  scaled placement). With `WMV_WORLD_AUDIT_CORPUS` it prints the census of every client's world models in the world07
  corpus and checks the Retail asset window (`<corpus>/assets/wow`, its file set checked against `corpus_hashes.json`
  first) against the plan's 2.2: 52 complete roots, 531 LOD0 groups (300 exterior, 231 interior), 11,470 MODD records
  listed, 6 by both kinds; otherwise that part is SKIPPED.
- `WorldFogTests.cs`: Stage 10 step 7 (`docs/world-viewer/10-light-sky-fog.md` 4.0, 4.6, 6.9, 6.13;
  `Assets/Scripts/Wow/World/WorldFog.cs`): the client's per-pixel fog against its own programs re-executed offline (the Retail
  terrain program 2977247 p_1 on 59 one-record cases covering every regime, the M2 program 2977063 p_1 with blend words 0 and 4;
  `WorldFog.Apply` within 2e-6), the record `WorldFog.Pack` fills from the light state against an independent Python port of the
  mapping on the client's keys (LightParams 5985 at three times, 5951, 5944, 6033, a Z key) and on one key per fall-back, blends of
  a key with and without each fall-back (every row continuous), the sky's shared tint inputs (`SkyDome.Inputs` equal to HEAD's), the
  modes by client, the columns each needs and the `-wmvFogClassic` arms (each public rule switched off alone), the twin's invariants,
  the switches and their texts, the graded table's fog rows, the census on a synthetic set, no allocation in `Pack`, the light
  sample's existing channels equal to HEAD's evaluator with the derived block after them, and `WmoSynthetic.ContextRoot`'s old
  bytes. The fixtures are embedded
  (`D:\wmv-10\step7\impl\fixtures\make_fog_fixtures.py` wrote them; its inputs are named with their SHA-256 in the file). With
  `WMV_FOG_KEYS` naming the key export of every client's LightData (`make_fog_keys.py`, written outside the repository from the
  table extracts; its `manifest.json` pinned) it checks the census counts on every key; with `WMV_WORLD_LIGHT_FIXTURES` the
  record's continuity across every half-minute of every day of maps 0 and 1 of the five clients and across an ease step (the
  largest jumps reported); otherwise those parts are SKIPPED. `WMV_FOG_PARITY_OUT` names a file for the parity grid the rig
  compares with its Python port.
- `MapContractCheck.cs`: the world-map runtime contract at the data level (`docs/world-viewer/02-map-catalog.md`). The
  viewer's map catalogue test writes chosen maps' WDTs, read through the catalogue
  (`wowmodelviewer -mapcatalogtest <out.json> -mapcatalogwdt <folder> <map ids>`), and this step parses them with the
  verified WDT parser into `<folder>/manifest.json` (kind, tiles with their MAID FileDataIDs, global world model
  references), for the viewer to resolve next. It runs only when `WMV_MAP_CONTRACT_DIR` names that folder.

## Run

From the repository root (.NET SDK installed; output goes to the git-ignored `build/unity-parser-tests`):

```
set WMV_WORLD_AUDIT_CORPUS=F:/wowmodelviewer/build/world-audit-data
dotnet run -c Release --project Tools/UnityRendererProject/Tests/Runner/WowParserTestRunner.csproj
```

The exit code is the number of failures.

```
dotnet build -c Release Tools/UnityRendererProject/Tests/Runner/WowParsingNetStandard.csproj
```

This compiles `Assets/Scripts/Wow` as a .NET Standard 2.1 library at C# 9: the API profile and language level of the
Unity player. The Unity Editor compile remains the authoritative check (see `docs/world-viewer/01-parsers.md`).

Compare the corpus results with the audit's independent Python readers:

```
python -I Tools/world-audit/compare_csharp.py <corpus>/results/csharp_parsers.json <python results folder> <corpus>/catalog <out.json>
```

The Python results come from the audit scripts run on the same pinned bytes; see `Tools/world-audit/README.md`.

Compare the terrain mesh dumps with the independent Python reference, and check their orientation against the game's
minimap tiles:

```
set WMV_TERRAIN_DUMP_DIR=<folder>
dotnet run -c Release --project Tools/UnityRendererProject/Tests/Runner/WowParserTestRunner.csproj
python -I Tools/world-audit/compare_terrain_mesh.py <folder>/runner_wow_32_50.txt --corpus <corpus>
python -I Tools/world-audit/minimap_orientation.py dump <folder>/runner_wow_32_50.txt --corpus <corpus>
```
