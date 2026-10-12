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
