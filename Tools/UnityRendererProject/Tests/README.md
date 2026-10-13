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
