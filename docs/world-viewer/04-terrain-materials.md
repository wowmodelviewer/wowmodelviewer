# World viewer 04: one ADT tile's terrain texture materials, verified against the game's own composite

Branch `world/04-terrain-materials`, from `develop` at `3611a550` (2026-10-13, after `world/03-terrain-geometry`, PR
#104; both stage 03 commits, `73cb7efd` and `875df52a`, are in it). The work is **the texture layers of one real ADT
tile and their blending, drawn on the stage 03 geometry through the production host asset path**: the tile's tex0 ADT
and its BLP textures fetched under protocol 13, the alpha maps decoded, the layers composited by a rule measured against
the game's own data, and a textured view beside the stage 03 diagnostic views. Not in this stage (spec section 20): a
Maps UI, multi-tile streaming, WMO placement, M2 doodads, liquids, world lighting, sky, fog, distant terrain, volumetric
effects and new character materials. The audit (`00-audit.md`), the parsers (`01-parsers.md`), the map catalogue
(`02-map-catalog.md`) and the geometry stage (`03-terrain-geometry.md`) are the specification this stage builds on.

**Push status at delivery: NOT PUSHED.**

No in-game reference capture at a verified corresponding world location exists (none was taken; section 12 lists what
one would need), so this report makes **no claim of in-game or Retail visual parity**. The numerical oracle for the
offline composite is the game's own baked map texture, MAID word 5 of the WDT: a 512 x 512 unlit, shadowless composite
of the tile's terrain layers multiplied by the vertex colour (section 2.8). Where a rule is called PROVEN "for the
baked composite", that is all it means; how the client's own shader draws terrain at run time is INFERRED from it.

Labels as in the earlier stage reports: **PROVEN** = shown on real client bytes with the numbers given; **INFERRED** =
follows from proven facts plus a stated assumption; **UNRESOLVED** = not established, with the evidence that would
settle it named (section 12). Code is cited as `file:line` against the working tree after round 3 (uncommitted on
`3611a550`; every citation re-checked for this report) (`A/` = `Tools/UnityRendererProject/Assets/`, `W/` =
`A/Scripts/Wow/World/`, `T/` = `Tools/world-audit/`). Every file named as a test input is in the git-ignored audit data
(`build/world-audit-data`), verified against the corpus pins (`T/tests/corpus_hashes.json`) or against its folder's
`manifest_sha256.tsv` row with the pinned client build, unless the text names another read-only extraction.

The stage went through these rounds, each with its own binaries: implementation and integration round 1; a fix round
(design v3: the height-mix rule, section 5.2); integration round 2 (player `Assembly-CSharp.dll` MD5 `8D93FF2E...`, host
`wowmodelviewer.exe` SHA-256 `6a3655a3...`); the final review and fix round 2 (player `52C4B107...`, host
`2B741D8D...`); round 3 (MTXP nibble 4 and chunks of 6-8 layers, section 5.4; player `74BE433B...`); the round-3
verification on its verification binaries (host `9eced8d2...`, player `74BE433B...`); and a final round (the last
checker findings, the final build from the final source, and a full re-verification of it: host `317e279c...`, player
`5C6EC342...`; section 0).

## 0. Result in brief

**Final binaries**, built from the final source and re-verified in full: host `wowmodelviewer.exe` SHA-256
`317e279cb98e70f16df7f60ebdae46a6dd3bc5ddab486ed7a8cef8ba5b46f246` (`wow.dll` `f5f99f90cd0a...`, `core.dll`
`f716b02b53d6...` unchanged), player `Assembly-CSharp.dll` MD5 `5C6EC34216D9862AC275E0D48DC91567` (protocol 13;
self-test 841 passed, 0 failed, twice). The round-3 **verification binaries** -- host
`9eced8d208494a82867d9b9c0100e18f4deb508c941d94c54639bf8a061f4857` (`wow.dll` `c07464269786...`), player
`74BE433BCA461F926595844B32EB557F` -- differ from them only by comment and message edits (the layer-count wording in
`W/AdtParser.cs`, comments in `UnityIpcServer.h` and `app.cpp`). The final re-verification reproduced the round-3
results: every dump the player wrote (the 9 golden V2 dumps, the 2923 and 30_49 V1 chunks and texels, the Era 169 25_31
composites) byte-identical to round 3's, the same suite counts (the Python suite now 129, with 2 new tests), the
C#/Python cross-check, every harness sequence, the
stage 03 sequences, casc-whole-read, wdc3-strings and the map catalogue; performance within run-to-run spread. Where the
text says "verification binaries" it means round 3's; the final binaries gave the same result unless a number is marked
otherwise. Every run was on a hidden desktop.

| Check | Result |
|---|---|
| Texture data through the production path (WDT -> MAID word 3 tex0 -> MDID / MHID -> host read -> BLP -> GPU, the player's own parse) | **PASS** on Retail, Classic Beta and Classic Era (its 8-bit maps); MoP Classic: its storage did not open during the stage (corpus-level checks only) and opened again late on 2026-10-13 -- in the final verification the stage 03 switch sequence built MoP 0 32_48 with material `textured`; a dedicated end-to-end check on the final binaries (MoP 0 32_48 and 31_48: chunk states as loaded equal the reference on all 512 chunks, 31_48 `partial` with its two cube-map chunks neutral; V1 on 3 chunks at most 0.1458/255; texels of all 18 textures PASS, every interpolant the hardware model) passes; the rule itself stays INFERRED on MoP (Azeroth names no map texture: no V2 oracle) (section 3) |
| The blend rule (the height mix) against the game's baked map texture, every documented alternative asserted worse | **PASS for the baked composite** on 9 golden tiles (Retail x 7, Beta, Era): 0.978-1.158/255 (4 x 4-block RMSE) against plain weights 1.744-4.341 and sequential layering 2.483-8.339 (section 5.3); INFERRED for the client's draw-time shader; on MoP Classic INFERRED (no map texture on Azeroth) |
| V2: the player's production composite against the map texture | **PASS** on all 9 golden tiles (verification binaries): block RMSE 0.929-1.120 against thresholds 1.228-1.408 (Retail 2923 32_24 1.034 <= 1.307), every kept pixel within 0.1235-0.1259/255 of the reference under the GPU's measured decode (gate 0.26/255); the 8 earlier dumps byte-identical to integration round 2's, and all 9 byte-identical again on the final binaries |
| V1: the GPU's composite against the CPU's, per pixel, from the GPU's own texels | **PASS**: 17 chunks of 7 tiles in validation sampling (max 0.249/255, limit 0.5) -- 4 of them on Retail 2923 32_24 (nibble 4 in three phase classes, 7 and 8 layers) at most 0.1245/255, which a reference without the phase fails -- and 1 in production sampling (0.317/255, limit 2.0) (section 15.2) |
| Texels: the GPU's decode of every uploaded texture | **PASS**: block endpoints exact; every interpolated texel equal to one named decode (this GPU's measured model on every real one; an independent check found 0 off among 10,555,902 colour and 5,059,116 DXT5 alpha interpolants in 18 dumps; Retail 2923 32_24's 32 textures: 161 checks, 53,204,170 colour and 32,119,076 alpha interpolants, all the hardware model) (section 15.3) |
| MTXP texture-scale nibble 4 (Retail map 2923) | **IMPLEMENTED** in round 3: 0.5 repeats per chunk, UV continuous across chunks (a per-chunk phase); PROVEN for the baked composite (5 tiles, 897 chunks with nibble-4 layers, best on 308 of 308 and 206 of 206 discriminating chunks, pooled 1.023 against the runner-up 3.141); the player's V2 of 32_24's nibble-4 chunks 1.043; INFERRED at draw time (section 5.4) |
| Chunks of 6-8 layers | **IMPLEMENTED** in round 3 (a second alpha slice, 8 layer and 8 height slots on 9 samplers); PROVEN for the baked composite (115 chunks of 2923 32_24 at 1.079; the player's V2 1.059); INFERRED at draw time |
| 4-bit alpha (Classic Era Azeroth) | **UNSUPPORTED**: storage PROVEN, the client's draw-time expansion not established; drawn neutral as AlphaUnsupported (section 6.2) |
| Chunk states, C# builder against the Python reference | **PASS**: 145 whole tiles (37,120 chunks, layer repeats and UV phases included), 24 crafted and 5 texture-failure cases, 0 differences under the round-3 rules (and under the fix-round-2 rules before) |
| Failure states end to end (spec 19) | **PASS** for every case with a real or harness path (section 16); HeightTextureMissing and a held height request **UNSUPPORTED** by the harness (covered by the reference, the corpus tests, synthetic tests and the self-test) |
| Client switching, superseded loads, player restart, repeated loads | **PASS**: no object of an old client left, every late answer stale and dropped, counts flat over 334 mixed loads |
| Memory | **PASS**: flat at texture scale (at most about 1 KB of player native memory per load against 0.87-3.76 MB of texture data per load; host flat) (section 18); Retail 2923 32_24 (32 textures, 90.9 MB per load) over 31 loads: the same objects on every load, the baseline after clear, no growth at texture scale |
| Performance | measured per step (section 17): a normal tile adds about 100 ms cold to stage 03's load; the heaviest, Retail 2923 32_24 with 32 textures, loads in 1,247 ms cold (texture fetch 1,080 ms) and 1,220 ms warm (final binaries, one process: 1,248 / 1,213 ms) |
| Host serving budget (stage 03's 20 ms per request) | **FAIL** for textures of 5,593,604 B: on the verification binaries all twelve such textures of Retail 2923 32_24 take 28.35-34.20 ms median per handler call (in integration round 2 four, 31.7-34.7 ms); on the final binaries 28.3-35.4 ms median, with one performance process too few requests for the checker's verdict |
| Regression (geometry, M2, WMO, parsers, lifecycle, IPC, colour) | **PASS**: stage 03 dumps byte-identical, M2 and WMO captures byte-identical; on the final binaries the C# runner, netstandard, Python suite (129 tests), self-test (841), the stage 03 late-answer, switch (with and without MoP), worst-tile and lifecycle sequences, part B's sequences, protocol compatibility, casc-whole-read, wdc3-strings and all four map-catalogue sequences PASS; map-catalogue comparison **FAIL** on one stale expectation (Beta 787450, environment) (section 19) |
| Captures (section 14) | 22 figures: 18 from integration round 2 (spec 17 items 1-10 and the map-texture comparisons) and 4 from the round-3 verification (Retail 2923 32_24 textured, its materials view, a nibble-4 chunk boundary, a top-down capture against the map texture and the V2 dump) |
| Visual parity with the running game | **NOT CLAIMED**: no in-game reference capture exists |
| Adversarial review | design review (3 reviewers, 42 findings), implementation review (26 findings; the critical one overturned the blend rule), final review (20 findings, 15 confirmed and fixed, 5 refuted), checks of fix round 2 (7 findings, fixed in round 3), checks of round 3 (6 findings, each confirmed by a verifier, all minor, fixed in the final round) (section 20) |

## 1. Files

New:
- `W/TerrainMaterial.cs` -- the pure C# material data of one tile (`TerrainMaterialBuilder`: chunk states, layers,
  repeats and UV phases, height inputs, one or two alpha slices per chunk, vertex colours, texture lists) and the CPU
  composite with the shader's exact arithmetic (`TerrainCompositor`). No UnityEngine.
- `A/Scripts/WmvTerrainMaterials.cs` -- textures (verify, prepare off the main thread, native block-compressed upload or
  the exact RGBA32 fallback), the tile's alpha `Texture2DArray`, the tile's sampler carrier, one `Wmv/TerrainTextured`
  material per chunk drawn textured.
- `A/Scripts/WmvTerrainComposite.cs` -- validation fixtures only: the CommandBuffer composite render, the tile and chunk
  composite dumps, the GPU texel dump (all streamed in bands of rows).
- `A/Scripts/WmvLifecycleSelfTestTerrainMaterials.cs` -- the player self-test of the materials.
- `A/Resources/WmvTerrainTextured.shader` -- `Wmv/TerrainTextured`.
- `Tools/UnityRendererProject/Tests/TerrainMaterialTests.cs`, `TerrainMaterialCorpusTests.cs` -- runner suites
  (synthetic and real bytes).
- `T/terrain_material_reference.py` (the independent CPU reference), `T/compare_terrain_material.py` (V1, V2, texels),
  `T/tests/test_terrain_material.py`.
- `docs/world-viewer/04-terrain-materials.md` and its 22 captures in `docs/world-viewer/04-terrain-materials/`:
  `01-before-after-retail-32_50.png`, `02-blend-closeup-retail-32_50-mcnk152.png`, `03-layers-retail-32_50-mcnk152.png`,
  `04-chunk-boundary-retail-32_50-mcnk87.png`, `05-tile-edge-retail-32_50-33_50.png`, `06-classic-beta-32_48.png`,
  `07-classic-era-2853-34_50.png`, `08-classic-era-azeroth-32_50-materials.png`, `09-retail-34_46-lava-textured.png`,
  `10-retail-34_46-materials.png`, `11-retail-30_49-height-textures.png`,
  `12a-height-mix-closeup-retail-30_49-mcnk68.png`, `12b-plain-weights-diagnostic-retail-30_49-mcnk68.png`,
  `13-missing-texture-169-24_27.png`, `14-regression-model-bear.png`, `15-regression-building-108631.png`,
  `16-topdown-vs-map-texture-retail-32_50.png`, `17-v2-dump-vs-map-texture-retail-30_49.png` (integration round 2);
  `18-heavy-tile-2923-32_24-textured.png`, `19-heavy-tile-2923-32_24-materials.png`,
  `20-nibble4-chunk-boundary-2923-32_24-mcnk20-21.png`, `21-topdown-vs-map-texture-2923-32_24.png` (round-3
  verification).

Changed:
- Host: `Source/wowmodelviewer/UnityIpcServer.h/.cpp` (protocol 13: roles, limits, the map request queue, report
  fields, the report's texture-entry bound 256 x 8 x 2 = 4,096, the per-texture log line), `modelviewer.h/.cpp`
  (decisions for the new roles, kind checks, per-load accounting, load-ended marking, content key as the answer's md5),
  `app.cpp` (the `-terraintile` harness: new steps and checks, a view-aware texture-count check, report-list checks at
  4,096 entries), `Source/games/wow/WorldMapCatalog.h/.cpp` (content key and bytes read out of `inspectFile`),
  `CASCFolder.h/.cpp` and `WoWFolder.h` (the content key out of the whole-file read).
- Player: `WmvIpcClient.cs` (protocol 13, report fields), `WmvTerrainSession.cs` (up to K = 4 requests outstanding,
  the tex0 and texture roles, retired ring 128, settling of reports that end before adoption), `WmvTerrainBuilder.cs`
  (views; textures, alpha array, sampler carrier and materials tracked per tile; release of textures no remaining
  chunk binds), `WmvMainTerrain.cs` (view selection, dumps, labels counted from what was drawn),
  `WmvLifecycleSelfTest.cs` (now a partial class; the stage 03 numbers moved to protocol 13, ring 128 and K),
  `Wow/BlpDecoder.cs` (`ReadHeader` added; `Decode` gains an opt-in exact block mode, its default path unchanged),
  `Wow/World/AdtSynthetic.cs` (tex0 builders for synthetic tests), `Wow/World/AdtParser.cs` (comment and Malformed
  message only, final round: 8 layers is the most any audited chunk has; its limit, 8, is unchanged).
- Tests and docs: runner `Program.cs` and `WowParserTestRunner.csproj`, `Tools/UnityRendererProject/Tests/README.md`,
  `T/README.md`, `docs/unity-renderer/README.md` (protocol 13).

At the final verification `git diff 3611a550 --stat` counts 55 paths: 33 text files with 18,476 insertions and 549
deletions, and 22 PNGs (this report not included). The final round's source edits are comments and messages only:
`W/AdtParser.cs`, whose layer limit is 8 (`MaxLayers`, `:381`) but whose comment and Malformed message said "at most 5
in the audited chunks" (now 8, the most any audited chunk has: Retail map 2923); the report-bound comment in
`UnityIpcServer.h` ("8 is the most observed" instead of "the most any tile has"); and a comment in `app.cpp` (`:5993`)
that still described the report list as 256 chunks of 5 layers.

Outside the repository (git-ignored audit data, read-only extractions, each folder with `manifest_sha256.tsv` = id,
bytes, SHA-256, build): `materials/<client>/stage04_core`, `stage04_reference`, `stage04_golden`,
`materials/wow/stage04_nibble4` (round 3's evidence: 48 files, 103,926,528 B -- the WDT of map 2923, the root, tex0
and map texture of tile 32_24 and its four neighbours, and their 32 textures), and the investigation's `census_blp`,
`census_sweep`, `census_full`, `refs`, `tex`, `unity_terrain_tex`, `ipc`, `docs_u1`, `era_maptex_probe`,
`modern_probe` and `host` folders. Integration round 2's extractions (Classic Era map 169 tile 25_31, Retail 2923 32_24,
2175 37_32, Azeroth 44_28) are under `F:/wmv-build-fix/terrain04/int2/`, each with the extractor's own manifest.

## 2. Investigation: the pipeline, step by step

The written investigation (six investigators: format inventory, census, the game's reference images, external format
references, the Unity pipeline, IPC) came before any shader; its gate is section 5. Per step: source, structure and
fields, offsets and flags, clients, the test asset, what existing code is reused, and the expected result that was
verified independently. The running example is Retail 12.1.0.69933, map 0 (Azeroth), tile 32_50.

### 2.1 WDT

- **Source**: the map's WDT. Retail and Classic Beta name it by Map.db2 FileDataID; Classic Era and MoP Classic by path
  `world/maps/<Directory>/<Directory>.wdt`, resolved to the storage root's own FileDataID (stages 02, 03).
- **Structure, fields**: MPHD word 0 = flags; MAIN (8 bytes per tile, flag 1 = the tile exists); MAID (4,096 entries x
  32 bytes, index y*64 + x, eight FileDataIDs: 0 root, 1 obj0, 2 obj1, **3 tex0**, 4 lod, **5 map texture**, 6 map
  texture `_n`, 7 minimap).
- **Flags used**: MPHD & 0x84 != 0 <=> the map's alpha maps are 8-bit: 112 of 112 maps classified by decoding their tex0
  (Retail 36, Era 33, Beta 43), 0 exceptions; bit 0x2, 0x4 alone and 0x80 alone each fail as single-bit rules. PROVEN
  as a correlation; the layer spacing stays the decoder's authority and a disagreement is a Malformed state (section
  6.6). MPHD 0x80 = the height textures and MTXP height values enter the composite (section 6.1). MPHD 0x100 <=> the
  tiles name words 4-6 (577 of 577 set, 0 of 430 clear, 1,007 WDTs in four clients).
- **Clients**: all four. Word 3 is non-zero on every MAID tile: Retail 44,018 tiles / 763 WDTs, Era 2,719 / 58, MoP
  9,848 / 227, Beta 3,322 / 69.
- **Test asset**: WDT 775971, 294,988 B, MD5 `ac31869061a40d1cac3d4661c3a4a859`, MPHD 0x3CA; MAID[32_50] = 778037,
  778038, 778039, **778040**, 1287952, **1293881**, 1293882, 204495.
- **Reused**: `WdtParser` (`W/WdtParser.cs:53-63` the word roles, `Tex0Adt = 3` at `:58`; `TryGetTileFiles` at `:147`).
- **Verified**: word 3 is the pinned tex0 on 130 of 130 corpus window tiles; tex0 = root + 3 on only 73.2% of Retail
  tiles, so the id is never derived; Era opens `azeroth_<x>_<y>_tex0.adt` by path to the same bytes as word 3 on 58 of
  58 tiles (a cross-check only, never the address).

### 2.2 Root ADT (MAID word 0)

- **Structure, fields**: MCNK header (128 B) then `MCVT [MCCV] MCNR ...`. Header +0 flags: 0x40 <=> MCCV present
  (Retail 2,412 = 2,412 chunks; Beta 2,304; MoP 299; Era none), 0x1 <=> the tex0 MCNK carries MCSH (Era 6,976; MoP
  1,627), 0x8000 set on every chunk ever read (33,280 corpus + 75,093 sweep chunks + 256 of map 1051 tile 31_29);
  +20 the 64-bit hole mask (stage 03); +64..+79 a 2-bit-per-cell dominant-layer table (cell = row*8 + col, least
  significant bits first). The header's layer count and alpha/shadow offsets are 0 in every split root: in split
  files the layers live only in tex0. MCCV = 145 x 4 bytes.
- **Test asset**: root 778037, 329,922 B, MD5 `101be8a2fe1bed9cc1d8aa19c7f66afb` (no MCCV chunk); 34_46 root 778417,
  433,544 B, MD5 `e9a418c3038687dddaeb2ebd2bc55219` (152 chunks with MCCV and flag 0x40).
- **Reused**: `AdtParser.ParseRoot` (stage 03), `AdtRootChunk.VertexColorsRaw` (`W/AdtModel.cs:41`).
- **Verified**: MCCV byte order B, G, R, A: the map texture fits 3.30 read B, G, R against 19.47 read R, G, B on 34_46
  (2.34 against 3.11 on 32_48); the per-channel correlation of map/prediction with MCCV/127 is 0.985-0.986 against 0.31
  for the swapped order; byte 3 is 255 in all 727,175 entries. The dominant-layer table is reproduced by the decoded
  alpha in 99.80% / 99.73% / 99.85% of cells (Retail / Beta / MoP, weights) and in 714,688 of 714,688 Era cells
  (layering with the 16n + 15 expansion): this pins alpha decode, orientation and layer order on bytes alone.

### 2.3 tex0 ADT (MAID word 3)

- **Structure**: `MVER(18) MAMP MDID MHID MCNK x 256 [MTXP]`. tex0 MCNKs have no header: `MCLY [MCSH] [MCAL]` from
  their first byte; tex0 MCNK n belongs to root MCNK n. A tex0 carries no tile identity: MAID word 3 is the only link.
- **Fields**: MAMP = 4 bytes, value 0 in all 411 tex0 files read (the audit's "1 byte + padding" is corrected by this);
  MDID = u32 diffuse FileDataIDs; MHID = u32 height FileDataIDs, always as many entries as MDID (130 of 130 window
  tiles; 38,536 of 38,536 Retail heads); MTXP = 16 bytes per MDID entry: u32 flags, f32 height scale, f32 height offset,
  u32 word 3, default (0, 0.0, 1.0, 0). No MTEX in any client (0 of 44,577 heads; 0 of 130 corpus files); MTXF in no
  corpus tex0; `_tex1` absent in all four clients.
- **MTXP flags bits 4-7, the texture-scale nibble**: over 1,801 whole tex0 files of the audit data (Retail 1,472, MoP
  11, Beta 121, Era 197) the entries hold nibbles Retail {0: 34, 1: 94, 2: 60, 3: 44, 4: 17}, MoP {0: 15}. Nibble 4
  occurs only on Retail map 2923 (17 entries in its 5 tex0 files, all flags 0x40); nibbles 5..15 never. Over the 29
  distinct MTXP tables read, the height offsets are 0.1 (3), 0.5 (3), 1.0 (322), 1.3 (2) and 2.0 (1); none is negative,
  no scale + offset is negative, none overflows float32 or is not finite.
- **Clients**: all four. MTXP occurs in the windows only in Retail 30_49 (176 B, 11 entries) and MoP 31_48 (240 B,
  15 entries).
- **Test asset**: tex0 778040, 1,286,682 B, MD5 `1125fd4e7e492bca2ea9f59c79f9df2b`; 10 MDID entries, MHID all 0, no
  MTXP. The same FileDataID is a different file in each client (Era 1,573,832 B, MoP 1,419,282 B).
- **Reused**: `AdtParser.ParseTex` (`W/AdtParser.cs:322`), `AdtTex` (`W/AdtModel.cs:169-178`). New on the host: the
  kind check, MVER (`REVM` in file byte order) at 0 and MAMP (`PMAM`) at 8 + MVER's size
  (`Source/wowmodelviewer/modelviewer.cpp:3571-3597`).
- **Verified**: 44,577 of 44,577 tex0 heads read (every readable tex0 of Retail, Era and Beta) have that form; among all
  corpus file kinds only tex0 files pass it (roots `RDHM`, object, lod files and BLPs fail).

### 2.4 Texture-layer references (MCLY)

- **Structure**: 16 bytes per layer: +0 texture index into MDID, +4 flags, +8 offset into MCAL, +12 word 12.
- **Flag semantics**: 0x100 on exactly the layers after the first (Retail 32,848; Era 25,685; MoP 5,455; Beta 5,147;
  never on a first layer); 0x200 on exactly the RLE layers (Retail 31,571; MoP 5,405; Beta 5,110; Era 0). The rest
  occur only on the Burning Steppes lava textures 186947/186949 (Retail: 0x40 on 108 layers, 0x80 on 128; Era: 146 and
  174) and on two MoP first layers (0x400, texture 303951, whose MTXP flags are 0x1); the rotation (0x7) and speed
  (0x38) bits occur only together with 0x40; no bit above 0x400 anywhere. The names animation / overbright / cube map
  come from external references and fit every observed value: a correlation, not a proof of what they draw.
- **Word 12**: non-zero on almost every layer; only 171 of 312 distinct Retail values are ids of the ground-effect
  table. Not a texturing field; not used.
- **Layer order** = MCLY order; texture = MDID[index]. Index out of range 0 times in any client; no chunk names a
  texture twice. Layers per chunk: in the corpus windows 1-4 in Retail, Era and MoP, 5 in 11 Beta chunks; outside them
  up to 8 -- Retail map 2923 tile 32_24 has 83 chunks of 6 layers, 27 of 7 and 5 of 8, map 2858 tile 40_11 8 of 6 and
  2 of 7 (section 4.4). No chunk with more than 8 was seen; `AdtParser` makes more than 8 MCLY entries Malformed.
- **Test asset**: 32_50 MCNK 87 (ix 7, iy 5): 187127 (base), 186770, 187115, 187125, flags 0x0 / 0x300 / 0x300 / 0x300.
- **Reused**: `ParseTexChunk` (`W/AdtParser.cs:383`; at most 8 layers decoded, `:381`), `TerrainLayer`.
- **Verified**: the references explain the game's baked composite with nothing fitted (section 5.3).

### 2.5 Alpha maps (MCAL)

- **Structure**: layers after the first carry 64 x 64 maps back to back from MCAL offset 0 in layer order. MCLY 0x200 =
  RLE: a count byte n, top bit set = repeat the next byte (n & 0x7F) times, clear = copy (n & 0x7F) bytes, exactly 4,096
  values. Otherwise the gap to the next map (or to the end of MCAL) decides: 4,096 bytes = 8-bit raw, 2,048 bytes =
  4-bit raw, low nibble first. Texel (row, col) = sample[row*64 + col], rows south along the vertex rows, columns east.
- **Edges**: data are sampled as stored. MCNK 0x8000 (the "do not fix" flag of external references) is set on every
  chunk, so the "copy row/column 62 into 63" rule is never exercised; copying would alter 923 of 1,277 raw Retail
  8-bit maps whose row 63 differs from row 62. Era stores row/column 63 = 62 in 25,672 of 25,672 non-uniform maps.
- **Clients**: 8-bit RLE and raw: Retail, Beta, MoP Azeroth and Era maps whose MPHD has 0x84 (2784, 2806, 2853, 2875
  and others); 4-bit raw: Era Azeroth (all 25,685 window layers) and Beta maps 2720 (0x248) and `pvpzone03` (0x24A).
  No chunk mixes 4- and 8-bit.
- **Test asset**: 32_50 MCNK 87, three RLE maps, decoded-value SHA-256 (first 12 hex) `2b3d72e4dbad`, `b78f6d4ba277`,
  `0dd9dbfdb042`; MCNK 246 raw 8-bit `7a5d7a080e8f`; Era 32_50 MCNK 51 raw 4-bit `14b3f3d32f24`, `bf8786e54409`,
  `6f03617f19f5` (values 0..15 before any expansion).
- **Reused**: `DecodeAlpha` (`W/AdtParser.cs:478`), `DecodeRle` (`:563`), `DecodeRaw` (`:616`);
  `AlphaMap.TryGetEightBit` still refuses 4-bit maps (`W/AdtModel.cs:143`).
- **Verified**: all 42,086 RLE layers of the windows decode to exactly 4,096 values, with no overshoot and no zero-count
  run, and 0 of 22.6 M runs crosses a 64-texel row; offsets run back to back in 100% of chunks with alpha. 8-bit sums
  never exceed 255 (0 of 63.6 M multi-layer texels in Retail, MoP and Beta; exactly 255 in 13.0-13.7%), while Era
  nibble sums exceed 15 in 13.6% of 37.3 M texels: 8-bit data are weights, 4-bit data overlays. The alpha hashes of the
  pinned chunks are equal in the census, the C# parser and the Python reference, and were recomputed for this report.

### 2.6 BLP textures (MDID, MHID)

- **Structure**: BLP2: magic at 0; byte 8 encoding (1 palettised, 2 block-compressed, 3 BGRA); byte 9 alpha depth;
  byte 10 alpha type (0 DXT1, 1 DXT3, 7 DXT5); width and height at 12 and 16; 16 mip offsets at 20 and 16 sizes at 84;
  a 148-byte header and a 1,024-byte palette, so mip 0 starts at 1,172 (0x494).
- **Census**: 256 x 256 DXT3 or DXT5 with 8-bit alpha and 9 levels dominate; one 1024 x 1024 DXT5 (11 levels) in the
  windows; DXT1 placeholders of 8 x 8 and 32 x 32; two palettised files in Era; elsewhere in Retail 62 ids of 2048 x
  2048 DXT5 (12 levels, 5,593,604 B). Every extracted file has a full, contiguous chain down to 1 x 1 with exact block
  sizes (217 of 217 in the Unity census; 109 of 109 in the core part's header check). 188903 is a DXT1 transparent
  black. Map 2923 tile 32_24's 32 textures (16 diffuse, 16 height) are all DXT5 with 8-bit alpha: 12 of 2048 x 2048 (12
  levels, 5,593,604 B), 16 of 1024 x 1024 (11 levels, 1,399,300 B), 4 of 512 x 512 (10 levels, 350,724 B); 90,914,944 B
  in all (counted for this report).
- **Clients**: identical bytes in Retail, Era and Beta for 29 of the 31 ids all three have (186947: DXT5 in Retail, DXT3
  in Era; 188903: 8 x 8, 32 x 32 in Beta). Listfile names (a lookup aid only): every MDID id an `_s.blp`, every MHID id
  an `_h.blp`.
- **Test asset**: 187127, 88,580 B, MD5 `0680c5af47bf9b0c1417840d8066a154`, DXT3 256 x 256, 9 levels, the same bytes in
  Retail, Era and Beta.
- **Reused**: `BlpDecoder` (`A/Scripts/Wow/BlpDecoder.cs`). New `ReadHeader` (`:112`) exposes the format and the stored
  mip table and checks every level against the file, so the stored blocks can be uploaded unchanged. `Decode`
  (`:223`) keeps its default path unchanged (truncating 565 expansion and interpolation; the runner pins its values)
  and gains an opt-in exact block mode (header `:34`; `Rgb565Replicated` `:475`): 565 endpoints by bit replication,
  colour interpolants (2a + b)/3 and DXT5 6- and 8-value alpha ramps rounded to nearest (no ties occur), the DXT1
  three-colour midpoint (a + b)/2 rounded to nearest with ties to even, index 3 of a three-colour block (0, 0, 0, 0),
  DXT3 alpha 17n. Palettised and BGRA files decode the same in both modes.
- **Verified**: the diffuse alpha channel is data, not opacity (mean 1-65 on the 64 x 64 mip; no file is all 255):
  INFERRED a specular mask from the `_s` names; it is not used for diffuse layers. The height textures' alpha is the
  height the composite reads (section 6.1). The exact block mode against hand values computed independently with
  exact fractions (runner `BlpExactBlockTests`), and against an independent decode in the self-test (section 2.7).

### 2.7 Decoded pixels

- **Production**: no CPU decode for DXT1/DXT3/DXT5. `new Texture2D(w, h, RGBA_DXT1/3/5_UNorm, mips)` with every stored
  mip set from the file's own bytes (`Upload`, `A/Scripts/WmvTerrainMaterials.cs:271-296`; format map `:254-263`). A
  palettised or BGRA file, a block-compressed file whose format the GPU does not sample, or one whose size is not whole
  4 x 4 blocks is decoded per stored mip to `R8G8B8A8_UNorm` (`Prepare`, `:158-252`; the fallback at `:197-243`), a
  block-compressed one with `BlpDecoder`'s exact block mode, and the report's detail says so ("...; decoded to RGBA32
  (bit-replicated endpoints, interpolants rounded)", `:102`). Never an sRGB format. Rows go up as stored (row 0 = v 0 =
  the north edge of each repeat), never flipped: the model path's `CreateTexture` flips rows and is not used.
- **Why not the existing RGBA32 path**: `BlpDecoder`'s default mode truncates the 565 expansion and the colour
  interpolation; against a bit-replicating decode it is about half a step darker (mean +0.61 / +0.52 / +0.43, max +1.67
  per channel on 123 Retail terrain BLPs), and that path regenerates mips that differ from the stored ones by
  7-13/255 on average (mips 1-3). The self-test compares the exact mode with the file's own independent block decode on
  every stored mip of a 6 x 6 DXT5 (103 exact values and 81 rounded interpolants, 0/255 apart); the default mode differs
  from the GPU's readback there by up to 2/255.
- **Verified**: the GPU texel dump (Load of mip 0) against the reference's decode, under the **named-decode rule**
  (section 10.4): uncompressed texels and block endpoints come back exact, and every interpolated texel of a texture
  equals exactly one named decode -- for a block upload this GPU's measured hardware model (colour: `HardwareInterp5/6`,
  the player self-test's model; DXT5 alpha: per-selector weights in /2048, refit from the real dumps and verified on
  11,700 held-out inputs) or the exact ramp; for an `R8G8B8A8_UNorm` fallback entry the 'rounded' decode (the exact
  block mode above). The earlier rule, "interpolated texels lie between their endpoints", was superseded: it passed
  snapped, swapped and reversed selectors (final review, section 20). On real data every interpolant equals the hardware
  model; the hardware model is up to 5.333/255 off the exact ramp in colour and 6.4/255 in DXT5 alpha (section 15.3).

### 2.8 Layer composition, and the oracle

- **Rule**: the height mix (section 6.1), PROVEN for the baked composite, INFERRED for the client.
- **The oracle (MAID word 5)**: BLP2 512 x 512 opaque DXT1, 10 levels; 32 px per chunk, 4 px per cell, 2 x 2 alpha
  texels per pixel; pixel (0, 0) = the tile's north-west corner, rows south, columns east (identity is the best of 8
  symmetries against the minimap on 5 tiles, whose orientation stage 03 proved). It contains MCCV and no lighting (the
  residual against the slope normals has R^2 0.0007 on 32_50, 0.006 on 32_48), and no water, doodads, buildings or
  overbright. Word 6 is a 256 x 256 terrain normal map (R east, G up, B south); word 7 the lit minimap; word 4 geometry
  LOD only.
- **Test asset**: map texture 1293881, 175,948 B, MD5 `33302042ee9dcc51ce9a0b0d958179d0` (Retail 32_50).
- **Reused**: nothing (new code: `W/TerrainMaterial.cs`, `T/terrain_material_reference.py`).
- **Verified**: the golden table (section 5.3): the rule reproduces nine tiles' map textures at 0.978-1.158/255
  (4 x 4-block RMSE) and every documented alternative fits worse.

### 2.9 Shader

- `Wmv/TerrainTextured` (`A/Resources/WmvTerrainTextured.shader`): HLSLPROGRAM with URP's `Core.hlsl`,
  `#pragma target 4.5` (`:137`), explicit float, every switch a uniform, every per-material value in the
  `UnityPerMaterial` CBUFFER (`:160-186`). Up to 8 layers: 17 textures (8 layers, 8 height textures, the alpha array)
  on 9 samplers (`:142-158`): one per layer slot and the alpha array's; height texture k is sampled (production only)
  with layer slot k's sampler (section 8.3). Neutral chunks keep stage 03's `Wmv/Terrain`; the M2 and WMO shaders are
  untouched.
- **Verified**: V1, the GPU's composite against the CPU's from the GPU's own texels: 13 chunks in validation sampling
  at most 0.249/255 (limit 0.5), one in production sampling 0.317/255 (limit 2.0) (section 15.2); the self-test's
  6- and 8-layer, nibble-4 and sampler-carrier chunks at 0.091-0.249/255 (section 5.4); on Retail 2923 32_24 four
  chunks (nibble 4 at phases (0.5, 0), (0.5, 0.5) and (0, 0.5); 7 and 8 layers) at most 0.1245/255.

### 2.10 Viewport

- The composite is written in authored values; the frame decode applies the sRGB curve once per frame (section 8.4).
- **Verified**: V2, the 512-px production composite against the map texture: section 15.1 (all 9 golden tiles PASS on
  the verification and the final binaries, block RMSE 0.929-1.120, every kept pixel within 0.1259/255 of the
  reference under the GPU's measured decode); captures: section 14.

## 3. Client support matrix

Builds are the pinned ones (`T/tests/corpus_hashes.json`, `client_builds`), equal to each installation's `.build.info`
on 2026-10-13.

| | Retail | Classic Beta | Classic Era | MoP Classic |
|---|---|---|---|---|
| Build | 12.1.0.69933 | 1.60.1.70338 | 1.15.9.70003 | 5.5.4.70268 |
| Storage on 2026-10-13 | opens (the root lists 1,925,832 ids, 70,906 not on this computer; the audit saw 586) | opens (1,368 listed ids not local) | opens (421 not local) | **did not open** during the stage ("error 2", several attempts); opened again late on 2026-10-13 (final verification: map catalogue 44/0 and 0 differences against the audit in 31,920 checks; MoP 0 32_48 built with material `textured` in the stage 03 switch sequence); then a stage 04 end-to-end check (section 3 below) |
| WDT named by | Map.db2 FileDataID | Map.db2 FileDataID | path -> storage FileDataID | path -> storage FileDataID |
| Azeroth MPHD | 0x3CA (0x80 set) | 0x3CA (0x80 set) | 0x248 | 0x24E (0x4 set, 0x80 clear) |
| tex0 | MAID word 3 (no path names) | MAID word 3 (no path names) | MAID word 3; the path gives the same bytes (58 of 58) | MAID word 3; the path gave the same bytes in the audit (32_50) |
| tex0 MCNK sub-chunks | `MCLY [MCAL]` | `MCLY [MCAL]` | `MCLY [MCSH] [MCAL]` | `MCLY [MCSH] [MCAL]` |
| Texture references | MDID, 3-23 per window tile (median 10); MHID non-zero only on 30_49 in the window; map 2923 32_24: 16 MDID, 16 MHID | MDID 6-11; MHID 0 in the window (map 2980 names MHID without MTXP) | MDID 1-19 (median 8); MHID 0; MDID entries of 0 used by layers (map 169 24_27) | MDID 6-22; MHID 0 |
| Layers per chunk | 1-4 in the window; up to 8 on map 2923 (and 7 on map 2858 40_11) | up to 5 (11 window chunks) | 1-4 | 1-4 |
| Layer metadata | 0x100, 0x200; lava bits (0x80 on 128 layers) | 0x100, 0x200 only | 0x100 only (no RLE in Azeroth); lava bits (0x80 on 174 layers) | 0x100, 0x200; 0x400 on 2 base layers (303951, MTXP flags 0x1) |
| Alpha encoding | 8-bit RLE + raw | 8-bit (Azeroth); 4-bit on maps with neither 0x4 nor 0x80 (2720, `pvpzone03`) | 4-bit raw on Azeroth; 8-bit on maps with 0x84 (2784, 2806, 2853, 2875 ...) | 8-bit RLE + raw |
| UV, texture scale | 8 repeats per chunk; MTXP scale nibbles 1-3 (30_49 #9 0x20; map 1917 tile 27_19 0x10/0x20/0x30) and **4 = 0.5 repeats** (map 2923 only) | 8 (no MTXP in the window) | 8 (no MTXP) | 8; the one MTXP non-default entry is the cube-map flag 0x1 |
| Height data | MPHD 0x80: MHID + MTXP enter the mix (30_49, map 2923) | 0x80 set, no MHID in the window | Azeroth 0x80 clear; map 2853 0x3CA with MHID all 0 | 0x80 clear: every H = 1 |
| MCCV (window chunks) | 2,412 of 14,336 | 2,304 of 2,304 (99.9% grey) | none | 299 of 2,304 |
| MCSH | none | none | 6,976 chunks | 1,627 chunks |
| Map texture (oracle) | every Azeroth tile (MPHD 0x100) | every Azeroth tile | none on Azeroth; 10 WDTs, all 8-bit (0x3CA / 0x3C8) | none on Azeroth (MAID words 4-6 are 0 on all 839 tiles); 75 WDTs name them (unreadable during the stage; not examined after the storage opened) |
| Window chunk states (reference, computed for this report; unchanged by the round-2 and round-3 rules) | 56 tiles: Textured 14,208, LayerEffectStatic 128; 47 tiles textured, 9 partial | 9 tiles: Textured 2,304 | 56 tiles: AlphaUnsupported 11,167, Textured 3,169 (single-layer chunks need no alpha); 22 tiles unsupported, 30 partial, 4 textured | 9 tiles: Textured 2,302, LayerEffectUnresolved 2 |
| Stage 04 status | drawn textured; golden-verified on 7 tiles (Azeroth x 5, map 1917, map 2923) | drawn textured, golden-verified (32_48) | 8-bit maps drawn (map 2853 34_50 golden); 4-bit chunks UNSUPPORTED (neutral) | corpus-level during the stage; one tile (0 32_48) built `textured` in the final verification; end to end on 0 32_48 and 31_48: states, V1 and texels PASS; the rule INFERRED (no oracle) |

- **Per map, not per client**: Era ships 8-bit maps and Beta 4-bit maps. Every rule is keyed on the file's flags (MPHD,
  MCNK, MCLY, MTXP), never on the client's identity.
- **Era 4-bit**: a chunk with any 4-bit layer is AlphaUnsupported; its single-layer chunks have no alpha map and are
  drawn (3,169 window chunks). Era 32_50 and 32_48 have no single-layer chunk, so those tiles are entirely neutral
  ("unsupported", 256 of 256).
- **Nibble 4 and 6-8 layers** are seen only in Retail data (map 2923; 6-7 layers also on map 2858); the rules are keyed
  on the MTXP flags and the layer count, not on the client.
- **MoP Classic, exactly what was and was not verified.** Verified, from the pinned corpus only (WDT 775971, MD5
  `55322ab73c01fdce4f7e558d69218fbb`, and the 9 window tiles' roots and tex0 files): the tex0 form and MAID word 3;
  alpha decode (5,405 RLE and 50 raw 8-bit layers, all exact); the sum property (0 of 8,323,072 multi-layer texels above
  255); MoP alpha nearly equal to Retail's (4,244 of 5,443 paired layers byte-identical, 99.98% of texels within 7); the
  chunk states, computed alike by the C# builder and the Python reference on all 9 tiles (part of the 145-tile
  cross-check); the cube-map layers (31_48 MCNK 66 and 83) classified LayerEffectUnresolved. Not verified: any MoP
  texture (no BLP could be extracted: 38 ids listed, none read), any composite against a MoP map texture (Azeroth names
  none, the others are unreadable), the rule itself on MoP (INFERRED from the sum property and Retail's 0x80-clear
  maps), any host or player run of stage 04. Nothing from another client was substituted. After the storage opened
  again (late on 2026-10-13), the final verification's stage 03 switch sequence built MoP 0 32_48 through the
  production path with material `textured`, and the MoP map catalogue matched the audit (0 differences in 31,920
  checks). A dedicated end-to-end check followed on the final binaries: the live install's WDT, roots and tex0s of
  0 32_48 and 31_48 are byte-identical to the corpus copies; the 18 textures their drawable chunks name were extracted
  read-only (`materials/wow_classic/stage04_mop`, 88,580 B each, 9 DXT3 and 9 DXT5, manifest with build 5.5.4.70268);
  both tiles loaded through the production path (harness PASS 107/0/1, the skip "every map of this client has a WDT
  reference"); the chunk states as loaded equal the reference chunk by chunk (32_48 `textured` 256; 31_48 `partial`,
  254 textured and MCNK 66 and 83 LayerEffectUnresolved for their 0x400 cube-map base layer); V1 in validation sampling
  on 32_48 MCNK 250 (MCCV, a raw 8-bit layer) and 149 and on 31_48 MCNK 67 (next to both cube-map chunks): max
  0.1458 / 0.1245 / 0.1245/255, 0 pixels over 0.5/255 (42 and 21 checks, 0 failed); texels 44 and 63 checks, 0 failed,
  every endpoint exact and every colour and DXT5-alpha interpolant equal to this GPU's measured model. What stays
  INFERRED on MoP: the composite rule itself (MoP Azeroth names no map texture, so there is no V2 oracle; MPHD 0x24E
  has 0x80 clear, H = 1).

## 4. Exact input data (controlled test cases)

Identifiers come from each client's own WDT (MAID words 0, 3, 5) and were checked against the audit data for this
report with the repository's reference (`T/terrain_material_reference.py`, read only). MD5 = of the whole file
(lowercase hex; what the host sends as the answer's content key). Alpha hashes = SHA-256 of the 4,096 decoded values,
first 12 hex digits (4-bit maps as values 0..15), as in the census.

### 4.1 Tiles

| Tile | Client, build | WDT (MD5) | MPHD | Root ADT: id, bytes, MD5 | tex0 ADT: id, bytes, MD5 | Map texture: id, MD5 | Chunk states |
|---|---|---|---|---|---|---|---|
| T1 0 32_50 | Retail 12.1.0.69933 | 775971 (`ac31869061a40d1cac3d4661c3a4a859`) | 0x3CA | 778037, 329,922, `101be8a2fe1bed9cc1d8aa19c7f66afb` | 778040, 1,286,682, `1125fd4e7e492bca2ea9f59c79f9df2b` | 1293881, `33302042ee9dcc51ce9a0b0d958179d0` | Textured 256; 10 textures |
| T2 0 33_50 | Retail | 775971 | 0x3CA | 778237, 333,943, `c2a930009e8839f403c00fcc7fbde11f` | 778240, 1,180,961, `8a4dde5d295ec5279539c95e176e2f85` | 1293889 | Textured 256 |
| T3 0 32_51 | Retail | 775971 | 0x3CA | 778042, 307,789, `b8943d0968dc70284e826e5bc0d99a3e` | 778045, 1,548,327, `f2d5dd1d572f7d6ce5ee2da9e2e02027` | 1293883 | Textured 256 |
| T4 0 34_46 | Retail | 775971 | 0x3CA | 778417, 433,544, `e9a418c3038687dddaeb2ebd2bc55219` | 778420, 1,127,585, `5ea27d22f529dc68730f9d25fb5c5c67` | 1293753, `50ef373b9d7163edf82c4d2b4c0815bd` | Textured 245, LayerEffectStatic 11; MCCV 152 |
| T5 0 34_45 | Retail | 775971 | 0x3CA | 778412, 328,364, `092823c3a3629b6ad5a342489f8aa9e7` | 778415, 1,256,473, `3f0c73ace25887e4cefda621acc50dee` | 1293591 | Textured 252, LayerEffectStatic 4 |
| T6 0 30_49 | Retail | 775971 | 0x3CA | 777632, 344,617, `57d2463dd5c38453c1e7397cb44474a7` | 777635, 1,118,957, `ad045c6bfb902a8c50b60e2153218b86` | 1293863, `a4697705cffae0b1356b91baa614726e` | Textured 256; 11 diffuse + 2 height textures; MHID #9 971415, #10 878974; MTXP #9 (0x20, 5.0, 1.0, 0), #10 (0x0, 4.0, 0.5, 0) |
| T7 0 36_51 | Retail | 775971 | 0x3CA | 778847, 317,302, `a5cc735b8cff9947690a4167610a2fc5` | 778850, 978,301, `495eb16d3828dfc82399f63d3fa8bdcf` | 1293916 | Textured 256 |
| T8 0 29_50 | Retail | 775971 | 0x3CA | 777437, 309,593, `fb77f7a24b2c642784cd66912a78e8a0` | 777440, 1,368,517, `cdf0874f9320eea08746502e6e7a8daf` | 1293857, `1d3c70eb7791360cc76ca7b37c4adc85` | Textured 256; 13 textures |
| T9 0 32_48 | Retail | 775971 | 0x3CA | 778027, 359,854, `14d6214df32890cc1317c2fec3cb8e25` | 778030, 1,151,806, `c0c42fa096c0842cb335b47959b1974d` | 1293745, `3e13a0b25948e56a77240c3a2c11f64f` | Textured 256; MCCV 57 |
| T10 0 32_50 | Classic Era 1.15.9.70003 | 775971 (`7ce81ad2a553b5114914cca9131326c1`) | 0x248 | 778037, 329,878, `a3fdcbb6fc0e27354a899525f3e337f2` | 778040, 1,573,832, `f92a9dbf398f6e8bddb573dfcb4afebb` | none (words 4-6 = 0) | AlphaUnsupported 256; MCSH 256 |
| T11 0 32_48 | Classic Era | 775971 | 0x248 | 778027, 327,605, `986d2d3a757641205c0b133cee67b4ed` | 778030, 1,434,304, `0e22b1a230417ca05cfbc34f971c7843` | none | AlphaUnsupported 256; MCSH 210 |
| T12 0 32_48 | Classic Beta 1.60.1.70338 | 775971 (`e4fb77fc12097f3b41adf2aac2d945e7`) | 0x3CA | 778027, 477,275, `2154dd73295a7fea0ce3f77baa465293` | 778030, 1,184,801, `9e590845f656181b7f0f71d32659ad76` | 1293745, `b7c871e6f58f02090a79677884ada6fc` | Textured 256; MCCV 256; 7 textures |
| T13 0 31_47 | Classic Beta | 775971 | 0x3CA | 777822, 470,584, `dabedc93a06375482ae780f515439327` | 777825, 997,833, `c9bc1263c4b3c16899d774a112bb7324` | 1293737 | Textured 256 |
| T14 0 31_48 | MoP Classic 5.5.4.70268 | 775971 (`55322ab73c01fdce4f7e558d69218fbb`) | 0x24E | 777827, 375,707, `fa5d6a748239ff4f8d84c97d3a927936` | 777830, 1,153,958, `981347748bd6b5902fc7a8d8905e32f5` | none | Textured 254, LayerEffectUnresolved 2; MCSH 171; MTXP #11 (0x1, 0.0, 1.0, 0) |
| T15 2853 34_50 | Classic Era | 6357503 (`404aaa405e2b101f56fb687de576e4b2`) | 0x3CA | 6358761, 340,654, `eae2c501fafe16c7e1c167c685a0821b` | 6358764, 1,300,450, `23b650181562f74bf0f75b087edc2e61` | 6359006, `799997761acb2a8cea6d2252814da787` | Textured 256 (8-bit Era map) |
| T16 169 24_27 | Retail | 780817 (`cef3b16d8b72f760a20585eec1d5bc8a`) | 0x2C8 | 780828, 304,212, `bca4ee0385fadfae2c43af488f4d2e25` | 780831, 76,594, `05259b2354fdbf7f04a68a97a813d7e3` | none | MissingTexture 256; MDID [0, 249604, 0, 0], 295 layers name id 0 |
| T17 169 24_27 | Classic Era | 780817 (`fd439f1b6db4b22fd971fc58ef2df207`) | 0x248 | 780828, 304,212, `ccdc2d21ce23431bca9a132331140f13` | 780831, 111,688, `940404764589d15e90e5d42bc4becc1b` | none | MissingTexture 228, AlphaUnsupported 28 |
| T18 1917 27_19 | Retail | 1923857 (`93bbc8bb10b83d6d3b9905c8d77b37b2`) | 0x34E (0x80 clear) | 1923654, 442,190, `8577c163b2c834bb2cbf1064454f85ad` | 1923657, 1,733,405, `f2517235a1aa8a13f90b9b9ff9b920d9` | 1922956, `5d17a18f310bea144ad4efbfdbea373e` | Textured 256; MHID names 10 height textures and MTXP height values, all ignored (no 0x80) |
| T19 2736 32_29 | Retail | 5660543 (`922dd60ce4e7524e4437a1f6f771abdf`) | 0x3CA | 5862452 (487,021 B as served; not in the audit data) | 5862455: "not installed locally" (re-checked live in integration rounds 1 and 2) | 5857237 | material "unavailable" |
| T20 2923 32_24 | Retail | 6838755 (`675cd7d1dd58f7dbd38dfb5750ccf6b2`, 163,908 B) | 0x3CA | 7473884, 458,168, `8f523dcd07db158af79115002fb26ae0` | 7473887, 1,996,296, `72e977e308cb25262e411005e0e657e1` | 7473747, `cebf9847a74a93e80f708427a44bef98` | Textured 256 under the round-3 rules (round 1: Malformed 188, LayerCountUnsupported 6, Textured 62); 16 diffuse + 16 height textures; layers per chunk 2: 4, 3: 38, 4: 41, 5: 58, 6: 83, 7: 27, 8: 5; MTXP scale nibble 4 on MDID 5900005, 5900000, 5900003 (height scale 16, offset 1) and 5906217 (scale 0), 188 chunks; MCCV 256; no holes |
| T21 169 25_31 | Classic Era | 780817 (`fd439f1b6db4b22fd971fc58ef2df207`) | 0x248 | 780928, 304,212, `c94aa418a43dd7a8eab93af27e2e7b4b` | 780931, 930,600, `23e0b31e295f54ecc7eb665393b1218a` | none | AlphaUnsupported 225, MissingTexture 31 with texture 249604 "not in client storage" (the reference with `--missing 249604`, and the real player) |

Verification: T1, T4, T6, T10, T12, T13, T14 against individual corpus pins; T2, T3, T5, T7, T8, T9, T11 against the
pinned corpus set digests; T15, T17 against `materials/wow_classic_era/stage04_reference`, T16 against
`materials/wow/stage04_core`, T18 against `materials/wow/stage04_golden`, T20 against `materials/wow/stage04_nibble4`
(manifest rows, build = the pinned build; its WDT against the pinned set `wdt/wow`), T21 against integration round 2's
read-only extraction (`F:/wmv-build-fix/terrain04/int2/B/census/era_tiles`, the extractor's manifest; the player's
sidecar MD5s agree).

### 4.2 Chunks

Layers are listed as `BLP id : MCLY flags : storage : alpha hash` (the base layer has no alpha map); for T20 each layer
also gives its repeats per chunk `r` (and the UV phase of the nibble-4 layers), and every T20 layer names a height
texture (MHID).

| Case (spec 16) | Tile, MCNK (ix, iy) | Layers | Layer count | Alpha format | Holes / MCCV / MCSH | State |
|---|---|---|---|---|---|---|
| One texture layer | T7, 2 (2, 0) | 188850 : 0x0 : base | 1 | none (no MCAL) | -- | Textured |
| Multiple layers, strongly visible blending | T1, 87 (7, 5) | 187127 : 0x0 : base; 186770 : 0x300 : RLE : `2b3d72e4dbad`; 187115 : 0x300 : RLE : `b78f6d4ba277`; 187125 : 0x300 : RLE : `0dd9dbfdb042` | 4 | RLE | -- | Textured |
| Raw 8-bit | T1, 246 (6, 15) | 187088 : base; 187082 : 0x100 : raw 8-bit : `7a5d7a080e8f` | 2 | raw 4,096 B | -- | Textured |
| Raw + RLE in one chunk | T4, 48 (0, 3) | 186934 : base; 186951 : 0x300 : RLE : `a03dae85e4a1`; 186947 : 0x100 : raw 8-bit : `5c4c5fe69078`; 252273 : 0x300 : RLE : `2f8ffb156f27` | 4 | RLE + raw | MCCV | Textured (the lava texture carries only 0x100 here) |
| Alpha sums at 255 | T9, 158 (14, 9) | 187138 : base; 242660 : RLE : `c2dfc3ea9be3`; 187115 : RLE : `d4389ca636fa`; 321215 : RLE : `b01d8d80119a` (all 0x300) | 4 | RLE | MCCV | Textured (2,182 texels at the cap) |
| Terrain holes | T4, 146 (2, 9) | 186934 : base; 186951 : 0x300 : RLE : `4dfda64661c4`; 188903 : 0x300 : RLE : `b1103a5971f0` | 3 | RLE | holes 0x303; MCCV | Textured (188903 = transparent black) |
| Chunk-edge transition | T1, 129 (1, 8) column 63 -> 130 (2, 8) column 0, texture 186770 | 129: 187127 : base; 186770 : RLE : `affa494747db`; 187125 : RLE : `c7e3f3f945ca`; 187115 : RLE : `00d5cb915159`. 130: 187127 : base; 186770 : RLE : `e5fef87d573f`; 187125 : RLE : `712da0e79b2c`; 187115 : RLE : `5c304163aa88` | 4 / 4 | RLE | -- | Textured; an authored step: mean abs(column 63 - next column 0) = 40.88 over 64 texels |
| Tile-edge transition, authored step | T1, 47 (15, 2) column 63 -> T2, 32 (0, 2) column 0, texture 186770 | 47: 187127 : base; 187115 : RLE : `f855e24089a9`; 186770 : RLE : `9c0a40dce68a`; 187125 : RLE : `283870230b8c`. 32: 187127 : base; 186770 : RLE : `e62cfcd94c5a`; 187125 : RLE : `53b05b08b4fd`; 187115 : RLE : `f33ea4fda893` | 4 / 4 | RLE | -- | Textured; mean step 47.42 over 61 texels (Era's converted weights step by 47.2 at the same edge) |
| Tile-edge transition, smooth | T1, 253 (13, 15) row 63 -> T3, 13 (13, 0) row 0, texture 187094 | 253: 187082 : base; 187094 : RLE : `fe7cba4e7b64`; 187088 : RLE : `33818b7a790d`. 13: 187098 : base; 187094 : RLE : `43809f63a653`; 187082 : RLE : `45f2684355ca`; 188590 : RLE : `691ea0edbf86` | 3 / 4 | RLE | -- | Textured; mean step 2.88 over 64 texels |
| 4-bit alpha | T10, 51 (3, 3) | 187127 : base; 186770 : 0x100 : raw 4-bit : `14b3f3d32f24`; 187125 : raw 4-bit : `bf8786e54409`; 187115 : raw 4-bit : `6f03617f19f5` | 4 | raw 2,048 B | MCSH | AlphaUnsupported (nibble sums reach 30) |
| Height textures | T6, 52 (4, 3) | 187127 : base; 186770 : 0x100 : raw 8-bit : `4043a9d22dfd`; 971416 (MHID 971415, 2 repeats) : 0x300 : RLE : `c685beb2f56e`; 187115 : 0x300 : RLE : `be3961467080` | 4 | RLE + raw | MCCV | Textured (height mix with heights) |
| Height textures | T6, 68 (4, 4) | 187127 : base; 971416 (MHID 971415, 2 repeats) : RLE : `b5e3443d1994`; 878975 (MHID 878974) : RLE : `126ce3d3049a`; 186770 : RLE : `6425d5ec11ea` | 4 | RLE | MCCV | Textured |
| Height texture at layer index 3 | T6, 51 (3, 3); 84 (4, 5) | 51: 187115 : base; 187138 : RLE : `fa60993dd72e`; 187127 : RLE : `1e3400f92a33`; 971416 (MHID 971415, 2 repeats) : RLE : `ec1216cfe956`. 84: 187138 : base; 187127 : RLE : `7d33bd8af3b0`; 971416 (MHID 971415, 2 repeats) : RLE : `cad67a409c77`; 878975 (MHID 878974, MTXP (4.0, 0.5)) : RLE : `bf10d7d10321` (all 0x300) | 4 / 4 | RLE | MCCV | Textured (pinned in V1 after the final review, section 15.2) |
| Five layers | T13, 48 (0, 3) | 187127 : base; 186770 : RLE : `9af2b6ce29cd`; 187125 : RLE : `a6c978e12167`; 187115 : RLE : `ceeb0c12f204`; 187138 : RLE : `9a1bdbb4e524` | 5 | RLE | MCCV | Textured |
| MTXP nibble 4, odd ix | T20, 7 (7, 0) | 5876854 : 0x0 : base : r 1; 5900005 : 0x300 : RLE : `5c846f4492c0` : r 0.5, phase (0.5, 0) | 2 | RLE | MCCV | Textured |
| MTXP nibble 4, odd ix and iy | T20, 23 (7, 1) | 5928543 : 0x0 : base : r 2; 5876854 : 0x300 : RLE : `f7f3afab7186` : r 1; 5900005 : 0x300 : RLE : `3d55fdb8c023` : r 0.5, phase (0.5, 0.5) | 3 | RLE | MCCV | Textured |
| MTXP nibble 4, even ix, odd iy | T20, 22 (6, 1) | 5928543 : 0x0 : base : r 2; 5876854 : 0x300 : RLE : `1968990a97a2` : r 1; 5900005 : 0x300 : RLE : `661eb6f25565` : r 0.5, phase (0, 0.5); 6156542 : 0x300 : RLE : `265a52957aeb` : r 4 | 4 | RLE | MCCV | Textured |
| Seven layers, a nibble-4 base layer | T20, 2 (2, 0) | 5900000 : 0x0 : base : r 0.5, phase (0, 0); 6049365 : 0x300 : RLE : `fd8bf8ddc4dd` : r 1; 5900005 : 0x300 : RLE : `3536c794a8b4` : r 0.5; 6156542 : 0x300 : RLE : `ba101777fb0a` : r 4; 5928543 : 0x300 : RLE : `292a7bbe166e` : r 2; 5876854 : 0x300 : RLE : `eaa3da90d7a9` : r 1; 5900003 : 0x300 : RLE : `464ed33567da` : r 0.5 | 7 | RLE (layers 5-6 in the second slice) | MCCV | Textured |
| Eight layers | T20, 145 (1, 9) | 5928543 : 0x0 : base : r 2; 5876854 : RLE : `1b1a6ed1c666` : r 1; 6225354 : RLE : `0fc7a425f5d5` : r 1; 6156542 : RLE : `ec3a7de032ff` : r 4; 6021899 : RLE : `a2f8ebe2991b` : r 2; 5900005 : RLE : `4d4c2896e8e6` : r 0.5, phase (0.5, 0.5); 6021896 : RLE : `b1201e329805` : r 2; 5900000 : RLE : `c76ffdab3e32` : r 0.5, phase (0.5, 0.5) (all 0x300) | 8 | RLE (layers 5-7, two of them nibble 4, in the second slice) | MCCV | Textured |
| Eight layers | T20, 209 (1, 13) | 5928543 : 0x0 : base : r 2; 6225354 : RLE : `4d066a61160e` : r 1; 6339465 : RLE : `4a8e2263d5b6` : r 2; 5900005 : RLE : `8f27edafdc14` : r 0.5, phase (0.5, 0.5); 6156542 : RLE : `aaa2f8958d32` : r 4; 5900003 : RLE : `7a59ecb54a9d` : r 0.5, phase (0.5, 0.5); 5876854 : RLE : `2ed826b2bc86` : r 1; 6021896 : RLE : `c92fb1150d6c` : r 2 (all 0x300) | 8 | RLE | MCCV | Textured |
| Classic Beta, 4 layers | T12, 162 (2, 10) | 187127 : base; 186770 : RLE : `56c3b5e5fc30`; 187125 : RLE : `123bc76d6763`; 187115 : RLE : `8a7d3ea2a3f6` | 4 | RLE | MCCV (grey) | Textured |
| Lava (MCLY 0x40, 0x80) | T5, 3 (3, 0) | 186934 : base; 186951 : 0x300 : RLE : `8cd3b1ca7d38`; 186947 : 0x3EE : RLE : `3ea74366c285`; 188903 : 0x300 : RLE : `cb623dbfc30b` | 4 | RLE | -- | LayerEffectStatic |
| Cube-map layer | T14, 66 (2, 4) | 303951 : 0x400 : base; 451145 : 0x300 : RLE : `0216596734d6`; 451147 : 0x100 : raw 8-bit : `c03a50f882f6`; 187082 : 0x300 : RLE : `35a6a07a5725` | 4 | RLE + raw | MCCV, MCSH | LayerEffectUnresolved (corpus only) |
| No texture id (MDID 0) | T16, 0 (0, 0); 254 (14, 15) | 0: id 0 : base. 254: 0 : base; 0 : 0x300 : RLE : `6f6d002fd2d4`; 249604 : 0x300 : RLE : `10b2f19d07f8`; 0 : 0x300 : RLE : `14cd0100f153` | 1; 4 | none; RLE | -- | MissingTexture (checker) |
| A texture "not in client storage" on a drawn layer | T21, 255 (15, 15); 240 (0, 15) | 255: 249604 : base (single layer); 240: id 0 : base (single layer) | 1; 1 | none | -- | MissingTexture (checker); the host answers 249604 "not in client storage" |
| Client difference, same chunk and layer list | T1, 150 (6, 9) against T10, 150 | Retail: 187127 : base; 187138 : RLE : `63b76ea9f721`; 187115 : RLE : `f0b5e5442c59`; 187125 : RLE : `a7502de45340`. Era: 187127 : base; 187138 : raw 4-bit : `b36a90c89d19`; 187115 : `8b8cb470e9e5`; 187125 : `759e998131e8` | 4 / 4 | RLE / 4-bit | Era MCSH | Retail Textured, Era AlphaUnsupported; Era nibble sums above 15 at 2,644 texels; mean abs(Retail - converted Era) 0.21 per layer |
| Client difference | T9, 97 (1, 6) against T11, 97 | Retail: 187138 : base; 187127 : 0x100 : raw 8-bit : `74eb828d95fc`; 187115 : RLE : `e808ad60c739`; 186770 : RLE : `2ae6057ba0f1`. Era: 187138 : base; 187127 : raw 4-bit : `cc8b789bc060`; 187115 : `f2dc48773ccb`; 186770 : `84f713499937` | 4 / 4 | raw + RLE / 4-bit | Era MCSH | Retail Textured, Era AlphaUnsupported (mean abs difference 0.455 per layer) |

The six T20 chunks are pinned in the C# corpus suite (state, ids, flags, storage, alpha hashes read from the slices,
height ids, repeats, and the phase checked as 0.5 * (ix % 2), 0.5 * (iy % 2) at 0.5 repeats) and in the Python suite.

### 4.3 Textures

| BLP id | Bytes | MD5 (Retail copy) | Format | Same bytes in (census) |
|---|---|---|---|---|
| 187127 | 88,580 | `0680c5af47bf9b0c1417840d8066a154` | DXT3 256, 9 levels | Retail, Era, Beta |
| 186770 | 88,580 | `1bcc39fbd2d6c236fd78ff71f292a8d4` | DXT3 256, 9 | Retail, Era, Beta |
| 187115 | 88,580 | `cb974e6547edb608e18e2e2864a206f0` | DXT3 256, 9 | Retail, Era, Beta |
| 187125 | 88,580 | `9a0be82363ddd740f7500899cacdefc2` | DXT3 256, 9 | Retail, Era, Beta |
| 187138 | 88,580 | `34f42d44fcd05847727ca6656f4859a8` | DXT3 256, 9 | Retail, Era, Beta |
| 187088 | 88,580 | `30dfc5522a1506d9041e5a43761fea07` | DXT3 256, 9 | Retail, Era, Beta |
| 187082 | 88,580 | `799857934ff45f597de6f23079d88161` | DXT3 256, 9 | Retail, Era, Beta |
| 187094 | 88,580 | `f394532c745ce970275e6009c1b59def` | DXT3 256, 9 | Retail, Era, Beta |
| 186934 | 88,580 | `7dc7dc9d130671092de123d54d831ff0` | DXT3 256, 9 | Retail, Era, Beta |
| 186951 | 88,580 | `398d3e8cc7d9c2f856e71a9e6c901607` | DXT3 256, 9 | Retail, Era, Beta |
| 186947 | 88,580 | `15c2a953adf2abcefc61e9f9a85167d4` | DXT5 256, 9 (Retail); DXT3 in Era | Retail, Beta (Era differs) |
| 252273 | 88,580 | `0e746af9d841179fb8162980262c5f1d` | DXT5 256, 9 | Retail, Beta (not in Era's storage) |
| 188903 | 1,228 | `b0fd7070529ec2bee62126a85deae3a9` | DXT1 8 x 8, 4 levels, transparent black (32 x 32 in Beta) | Retail, Era |
| 242660 | 88,580 | `ccfad0224b12d9ee700ff2ee0bd50a6d` | DXT5 256, 9 | Retail, Beta (not in Era's storage) |
| 321215 | 88,580 | `487fe63965e7c9ad80d500544b718f79` | DXT5 256, 9 | Retail, Beta (not in Era's storage) |
| 971416 (diffuse) | 1,399,300 | `7dd45e3c758d2666db6845d421367d37` | DXT5 1024, 11 | Retail, Beta (not in Era's storage) |
| 971415 (height) | 1,399,300 | `de0896d367e976a4bebc00957735b546` | DXT5 1024, 11 | Retail, Beta (not in Era's storage) |
| 878975 (diffuse) | 88,580 | `0cc8c08a6060a3e2e92b6bc68fb30f21` | DXT5 256, 9 | Retail, Beta (not in Era's storage) |
| 878974 (height) | 88,580 | `bab96c0053b06f89a57a63ba094b867b` | DXT5 256, 9 | Retail, Beta (not in Era's storage) |
| 188850 | 88,580 | `14e1804311f616d4395865c080f20340` | DXT5 256, 9 | Retail (other clients not checked) |
| 188590 | 88,580 | `4d1dad1c59b6922463d262a33c7a793c` | DXT3 256, 9 | Retail (other clients not checked) |
| 249604 | 88,580 | not in the audit data; the host served it in integration round 1 (DXT5 256 x 256, 9 levels) | DXT5 256, 9 | Retail; "not in client storage" in Era (integration round 2 status census) |
| 5900005, 5900000, 5900003, 5906217 (nibble-4 diffuse) and 5900004, 5899999, 5900002, 5906216 (their heights) | 5,593,604 each | in `materials/wow/stage04_nibble4/manifest_sha256.tsv` (e.g. 5900005 `4a60cf04aa6ee201643b5694dab631b1`) | DXT5 2048, 12 | Retail (map 2923 only) |
| the other 24 textures of T20: 6049365, 6225354 and heights 6049364, 6225353 (2048 x 2048, 5,593,604 B); 6717884, 6021896, 5876854, 6339465, 6163585, 6717887, 6021899, 6224613 and their heights (1024 x 1024, 1,399,300 B); 5928543, 6156542 and their heights (512 x 512, 350,724 B) | 90,914,944 for all 32 of T20 | the same manifest | DXT5, 12 / 11 / 10 levels | Retail |

### 4.4 Requested cases that are not available

- **A texture "not installed locally" on a drawn layer: none exists.** Integration round 2 status-checked every MDID and
  MHID id named by every readable tex0 head in the three local clients: Retail 3,391 ids (2,254 diffuse + 1,137
  height), all local; Beta 443, all local; Era 338, of which 4 are "not in client storage" (242642, 249604, 1237506,
  1237507). The earlier census (integration round 1) had covered only the MDID ids of the then-readable Retail heads.
  The "not installed locally" texture state is covered by crafted host decisions, the stand-in player and the self-test
  only.
- **A texture "not in client storage" on a drawn layer now exists**: Classic Era map 169 tile 25_31 (T21), single-layer
  MCNK 255 on 249604, the only Era tile of the 58 naming one of the four ids that draws one of them; run end to end in
  integration round 2 part B (section 16).
- **An encrypted texture**: 0 among 44,018 tex0 heads and 4,154 texture status rows; crafted test only.
- **A chunk without MCNK 0x8000**: none in 108,629 chunks read; synthetic test only.
- **More than 5 layers in a chunk is no longer unavailable**: Retail map 2923 tile 32_24 (T20; 115 chunks of 6-8 layers)
  is pinned and golden. Retail map 2858 tile 40_11 (WDT 6378051, MPHD 0x3CA), tex0 6378304, 1,147,177 B, SHA-256
  beginning `f0370fac2ec89311` as the census recorded, has 36 five-layer, 8 six-layer and 2 seven-layer chunks, all
  height-textured: under the round-3 rules those chunks are within the 8 drawn layers; the tile's root is not in the
  audit data and the tile was not run.
- **More than 8 layers in a chunk**: never observed; crafted tests only.
- **MTXP scale nibbles 5..15**: never observed (section 2.3); crafted tests only.
- **MoP Classic**: no test case beyond the corpus during the stage; the storage opened again late on 2026-10-13
  (section 3); the end-to-end check then used MoP 0 32_48 (MCNK 250, 149) and 31_48 (MCNK 67, next to the cube-map
  chunks 66 and 83) with their 18 textures extracted read-only; no MoP V2 case exists (no map texture on Azeroth).

## 5. Implementation gate, and its corrections

### 5.1 Gate A-H as reported before the production shader (design v2)

| | Verdict at the gate |
|---|---|
| A. Formats and builds | PROVEN: textures live only in tex0 = MAID word 3 (a FileDataID) in all four clients (130 of 130 corpus tiles; 44,577 tex0 heads of the form MVER, MAMP; only tex0 files pass that check). Textures are MDID FileDataIDs; no MTEX anywhere; Era and MoP need no path rule. Builds as in section 3 |
| B. Layer references | PROVEN: MCLY 16 B (texture index into MDID, flags, MCAL offset, word 12); layer order = MCLY order; 0x100 = every layer after the first, 0x200 = RLE; the baked map texture is reproduced from these references with nothing fitted |
| C. Alpha | PROVEN: 8-bit raw and RLE (exact); texel = row*64 + col, row south, column east; 8-bit iff MPHD & 0x84 (112 of 112 maps), the spacing the authority. Texel i centred at (i + 0.5)/64: PROVEN for the map generator, INFERRED for the client. 4-bit storage PROVEN, its draw-time expansion NOT established -> UNSUPPORTED |
| D. Blending | "PROVEN for 8-bit data: weights, w0 = 1 - sum(a_i), composite = sum(w_i * texture_i) in stored values, then x MCCV" -- **later overturned, 5.2** |
| E. UV | PROVEN for the map generator: 8 repeats per chunk, texel (0, 0) at the chunk's north-west corner, u east, v south, no flip; MTXP flags bits 4-7: repeats = 8 >> s (s observed 0..3); phase established to whole map pixels (1/8 repeat) -- **extended in round 3 to nibble 4, 5.4** |
| F. Unsupported, unresolved | 4-bit alpha; height-texture blending (MHID or non-default MTXP: "rule unknown"); MCLY 0x40/0x80/0x400 ("rule unknown"); the diffuse alpha channel; MCSH (format proven, effect unknown); the MCCV divisor; chunks without 0x8000; MoP (storage) |
| G. Architecture | native BC textures, one alpha array per tile, one material per drawn chunk (section 8) |
| H. CPU reference | an independent Python composer reproducing the map textures at 1.744 (Retail 32_50), 2.427 (34_46, non-lava), 2.023 (29_50), 2.058 (32_48), 1.920 (Beta 32_48), 1.834 (Era 2853 34_50) with the textures' own stored mips; sequential layering, linear blending, a transposed alpha map, a 1-px phase shift and no MCCV all fitting worse |

The gate itself went through a design review (three reviewers, section 20) before any code; every finding was folded
into the design, among them the change from box-filtered mips to the textures' own stored mips in the reference (which
moved the oracle's fit from 1.953 to 1.744 on 32_50 and tightened every threshold), the alpha-centre test, the state
table with its precedence, and the explicit neutral states for height-textured and lava chunks.

### 5.2 The disputed finding: plain weights were not the game's composite

- **The claim.** Gate D called plain weights PROVEN. Its evidence was the sum property (no 8-bit texel above 255), the
  root's dominant-layer table, and the map texture preferring weights to sequential layering and stored-value blending
  to linear blending.
- **The finding** (implementation review, dimension "texture reference interpretation", raised as critical). With the
  reference's own golden machinery and only the weight rule swapped, the height-mix cutoff that three external
  references describe -- and that the formats investigation had asked to test ("height cutoff on/off") -- fits the map
  texture better on all six golden tiles, closer on 76-84% of the blocks where the two differ. The golden thresholds
  were each the plain-weights RMSE + 0.25, so the gate passed the rule it was meant to test: the first end-to-end V2 run
  (32_50, plain weights) passed at 1.713 against 1.994.
- **Verification** by an independent verifier with its own script. The core holds. Gate D's evidence could not separate
  the two rules: the cutoff starts from the same weights (so the sum property fits both) and never changes which layer
  leads (so the dominant-layer table fits both); sequential-against-weights and linear-against-stored are other
  questions. Only the map texture discriminates, and it prefers the cutoff. Generic sharpening comes close on block RMSE
  (w^1.5 normalised 1.11-1.16; w^1.25 1.31-1.54) but loses on every tile, and only the full formula explains Retail
  30_49's height-textured chunks: plain 7.405, cutoff with default heights 5.396, full formula with the `_h` alpha and
  MTXP (5.0, 1.0) / (4.0, 0.5) **1.203** -- a dependence on each layer's own height texture that no per-pixel sharpening
  produces.
- **The part that did not hold**: tying the effect to MPHD 0x80. On three 0x80-clear Retail tiles (MPHD 0x34E, which
  have map textures) the cutoff wins too: WDT 822688 tile 30_23 plain 3.156 against cutoff 1.107, 31_24 3.261 against
  1.037, WDT 3073642 tile 28_51 3.432 against 1.126. On a 0x80-clear map the height textures and MTXP values must NOT
  enter: WDT 1923857 tile 27_19 fits at 0.978 with the cutoff, 3.027 with plain weights and 18.606 with the full
  formula.
- **Lava**: the chunks then held neutral as LayerEffectUnresolved fit the cutoff at about 1.2 against 4.6-5.9 with plain
  weights on 9 Retail tiles (34_46: 1.165 against 4.597): the baked composite shows those layers static, under the same
  rule. They became LayerEffectStatic.
- **Resolution** (design v3 delta): the height mix of section 6.1 is the production rule, implemented identically in the
  shader, the C# compositor and the Python reference; HeightBlendUnresolved removed; HeightTextureMissing and
  LayerEffectStatic added; the MHID textures requested on 0x80 maps; every golden threshold re-derived from the new
  rule; plain weights kept only as the labelled `texturedplainweights` view and as an asserted-worse alternative. The
  core part's mutants each fail (failures synthetic / corpus): red channel read instead of alpha 7 / 1; heights applied
  after the cutoff 14 / 3; equal weights as the fallback 3 / 0; height repeats ignored 5 / 0; MPHD 0x80 ignored 3 / the
  corpus run crashed; no cutoff 16 / 11. After the final review (fix round 2) how the height texture is sampled is gated
  too: heights read at 8 repeats instead of the layer's own fit 30_49's height blocks at 1.324, at 1 repeat 1.645, the
  red channel instead of alpha 2.606, against the design's 1.203, all asserted strictly worse in Python and C# (a C#
  compositor mutant that samples heights at 8 repeats now fails the corpus suite: 1.401 against 1.330).
- **Lesson**: a rule labelled PROVEN must be tested against every documented alternative on the oracle, including the
  ones the investigation itself named, and a threshold derived from the candidate's own fit can never reject that
  candidate. Every alternative in the golden table below is now asserted to fit worse.

### 5.3 The golden table (design v3, extended in fix round 2 and round 3)

4 x 4-block RMSE (0..255) of the reference's composite (the textures' own stored mips, alpha at pixel centres, x MCCV,
clipped to 8 bits) against the map texture, over Textured and LayerEffectStatic chunks without a checker layer, with
hole cells masked. "design" = the height mix; threshold = design + 0.25.

| Tile | design | threshold | plain | seq | seq + mix | w^1.25 | w^1.5 | linear | MCCV blocks: design -> no MCCV | other |
|---|---|---|---|---|---|---|---|---|---|---|
| Retail 32_50 | 1.110 | 1.360 | 1.744 | 2.787 | 2.266 | 1.314 | 1.133 | 2.337 | -- | |
| Retail 29_50 | 1.069 | 1.319 | 2.021 | 2.483 | 1.793 | 1.383 | 1.111 | 2.545 | 1.209 -> 1.210 | |
| Retail 32_48 | 1.096 | 1.346 | 2.058 | 4.552 | 3.615 | 1.427 | 1.130 | 3.322 | 0.988 -> 28.283 | |
| Retail 34_46 | 1.079 | 1.329 | 2.570 | 4.203 | 3.361 | 1.597 | 1.155 | 4.487 | 1.092 -> 27.299 | lava blocks 1.165 against plain 4.597 |
| Retail 30_49 | 1.158 | 1.408 | 2.928 | 5.437 | 4.051 | 2.015 | 1.588 | 4.710 | 1.064 -> 7.982 | height blocks 1.203; default heights 5.396; plain 7.405; heights at 8 repeats 1.324, at 1 repeat 1.645, red channel 2.606 |
| Beta 32_48 | 1.145 | 1.395 | 1.920 | 4.274 | 3.459 | 1.384 | 1.156 | 3.180 | 1.145 -> 9.451 | |
| Era 2853 34_50 | 1.107 | 1.357 | 1.834 | 2.549 | 1.904 | 1.343 | 1.135 | 2.519 | -- | |
| Retail 1917 27_19 (0x80 clear) | 0.978 | 1.228 | 3.027 | 8.339 | 7.793 | 1.572 | 1.032 | 4.545 | 1.000 -> 6.879 | full formula 18.606 |
| Retail 2923 32_24 (nibble 4, 6-8 layers; round 3) | 1.057 | 1.307 | 4.341 | 5.520 | 2.613 | 4.080 | 3.947 | 1.233 | every chunk has MCCV: 1.057 -> 14.204 | nibble-4 chunks (188, 12,032 blocks) 1.063; 6-8-layer chunks (115, 7,360 blocks) 1.079; nibble 4 as chunk-local 0.5 repeats 3.477, as 1 repeat 3.302, as 8 repeats 3.033, at 0.25 repeats tile-continuous 4.299; default heights 3.913; heights at 8 / 1 repeats 1.515 / 2.137; red channel 2.946 |

Shifted (+-0.25 texel) and i/63 alpha placements, a +-1-pixel texture phase and all 14 other symmetries of the alpha
maps and of the textures are also worse on every tile (asserted; on 2923 32_24: alpha +-0.25 texel 1.128 / 1.126, i/63
1.182, phase +-1 px 1.643-1.822, alpha symmetries 12.049-15.062, texture symmetries 4.471-4.829). The C# compositor, run
the same way at 32 px per chunk, lands within 0.036 of these values (its default decoder rounds interpolated colours
down: e.g. 30_49 height blocks 1.209, 32_50 1.130, 1917 27_19 0.963, 2923 32_24 1.065 / 1.078 / 1.092 for the tile, its
nibble-4 and its 6-8-layer chunks); the C# corpus suite asserts every 2923 alternative worse than the C# design + 0.25
(the smallest margin 3.024 against 1.065). On 29_50 the no-MCCV margin is small (1.209 against 1.210) because its one
MCCV chunk is nearly neutral. The eight earlier golden values are repr-identical before and after round 3.

### 5.4 Round 3 evidence: MTXP nibble 4 and chunks of 6-8 layers

**How it came up.** Integration round 2 part C chose Retail map 2923 tile 32_24 as the heavy tile (its textures are the
census maxima: 45.5 MB diffuse, 90.9 MB with heights) and found 194 of its 256 chunks neutral under the rules then in
force: 188 Malformed because an MTXP scale nibble above 3 was Malformed ("0..3 observed"), 6 LayerCountUnsupported
(more than 5 layers). The player, the reference and the design agreed, so the run passed, but two assumptions were false
for real data. Fix round 2 made a nibble above 3 LayerEffectUnresolved (the file is not broken; the rule is unknown) and
a dedicated investigation then measured nibble 4 against the game's map textures.

**Data.** `materials/wow/stage04_nibble4`: 48 files, 103,926,528 B, "48 read, 0 not read", build 12.1.0.69933 -- WDT
6838755 (byte-identical to the pinned `wdt/wow/6838755.bin`), the root, tex0 and map texture of 32_24 and its four
neighbours 31_24, 33_24, 32_23 and 32_25, and all 32 textures their tex0 files name. Nibble 4 occurs only in map 2923's
5 tex0 files (5 of 1,801 whole tex0 files scanned): MDID 5900005, 5900000 and 5900003 (height scale 16, offset 1) with
their height textures, and on 32_24 and 31_24 also 5906217 (height scale 0, so H = 1); MTXP flags 0x40 (nibble 4, no
other bit); all 2048 x 2048 DXT5 with 12 levels; MPHD 0x3CA (8-bit, heights enter the mix).

**Method.** The reference's map-texture mode, imported unchanged (`python -I`), with only the UV of nibble-4 layers
varied; every other layer keeps its repeats and chunk-local UV, textures read from their own stored mip at LOD
log2(texels per map pixel) (2048 at 0.5 repeats = level 5, one texel per map pixel, no inexact-level note fired), alpha
at pixel centres, the height mix with each MHID texture at the layer's own UV, x MCCV, clipped, 4 x 4-block RMSE. Two
equivalence checks PASS: the scratch pipeline's control composite equals the reference's own (1.0341168986740457 against
1.0341168986740443, per-pixel max 0.0), and model (a) recomputed inside the reference equals it on all 10 tile/set
pairs. Chunk sets over the 5 tiles: "n4" = Textured with a nibble-4 layer and at most 5 layers (543 chunks, 34,752
blocks); "n4x" = nibble-4 chunks of 6-8 layers (354 chunks); control sets without nibble 4. "dom>=0.5" keeps the blocks
where the nibble-4 layers' raw weight averages at least 0.5 (a mask independent of the model).

| Model of nibble 4 (pooled over 5 tiles) | n4, all (34,752) | n4, dom>=0.5 (6,496) | n4x, all (22,656) | n4x, dom>=0.5 (4,074) |
|---|---|---|---|---|
| **(a) 0.5 repeats, tile-continuous UV** | **1.023** | **1.100** | **1.076** | **1.112** |
| (b) 0.5 repeats, chunk-local UV | 3.661 | 7.894 | 3.694 | 8.090 |
| (c) as nibble 3 (1 repeat) | 3.431 | 7.309 | 3.521 | 7.668 |
| (d) as nibble 0 (8 repeats) | 3.141 | 6.642 | 3.231 | 7.016 |
| (e) 0.25 repeats, tile-continuous | 4.478 | 9.726 | 4.425 | 9.803 |
| (f) 0.25 repeats, chunk-local | 4.269 | 9.320 | 4.471 | 10.015 |
| (g) as nibble 2 (2 repeats) | 3.214 | 6.832 | 3.316 | 7.203 |
| (h) as nibble 1 (4 repeats) | 3.197 | 6.782 | 3.288 | 7.144 |
| (i) 0.125 repeats, tile-continuous | 4.669 | 10.150 | 4.805 | 10.778 |
| (j) the literal 8 >> 4 = 0 repeats (a flat colour: the smallest mip's mean) | 3.265 | 6.922 | 3.393 | 7.413 |

- **Per tile** (n4, all blocks), model (a) against (b) / (c) / (d) / (e): 31_24 1.047 against 3.798 / 3.669 / 3.400 /
  5.143; 32_23 1.007; 32_24 1.044; 32_25 1.046; 33_24 1.009 -- best on every tile, and within -0.044..+0.012 of each
  tile's own control (no nibble 4: 1.030-1.053). Pooled control 1.037 (17,920 blocks).
- **Per texture** (blocks where that texture weighs at least 0.5): 5900000 1.087 against the best other 7.085; 5900003
  1.110 against 7.049; 5900005 1.105 against 6.438; 5906217 (height scale 0, no height dependence) 1.145 against 8.788.
- **Per chunk** (tie-aware, chunks with at least 4 dominated blocks): (a) strictly best on **308 of 308** n4 chunks
  (smallest ratio of the best other to (a) 1.28, median 4.92) and **206 of 206** n4x chunks (smallest 1.98, median
  5.62).
- **Phase**: among the four half-repeat phases {0, 0.5} x {0, 0.5}, every chunk's best phase is the tile-continuous
  prediction (0.5 * (ix % 2), 0.5 * (iy % 2)) on 308 of 308 and 206 of 206 chunks (runner-up / best at least 1.42 and
  3.09); other chunk offsets 4.035-4.408; texel shifts of +-1 map pixel 1.456-1.514 and +-0.5 pixel 1.143-1.171, a
  parabola through -0.5 / 0 / +0.5 putting the optimum at -0.009 px (u) and -0.014 px (v); all 7 non-identity texture
  symmetries 3.945-4.500; the height texture decoupled from its layer's UV 1.728-2.072 (it follows the layer's own UV at
  nibble 4 too). `verdict.py`: RESULT: PASS (57 checks, 0 failed); a final rerun against the repository reference
  (SHA-256 `463e087e...`) reproduced every number.
- **6-8 layers**: chunks of 6-8 layers composited with all their layers by the same rule fit the map texture as well as
  the others: without nibble 4 1.066 pooled (103 chunks, 0.983-1.106 per tile), with nibble 4 under model (a) 1.076 (354
  chunks); in the round-3 golden table 2923 32_24's 115 such chunks fit at 1.079 (5.3). The baker composites up to 8
  layers by the height mix.
- **Verdict.** Nibble 4 = 0.5 repeats per chunk (one repeat per 16 cells), UV continuous across chunks: s = (ix + u) *
  0.5 with u = 0..1 across the chunk. PROVEN for the baked composite; INFERRED for the client's draw-time shader (no
  in-game capture); INFERRED for nibble 4 on maps other than 2923 (none in the audit data); nibbles 5..15 UNRESOLVED
  (never observed). Nibbles 0..4 follow one rule, one repeat per 2^n cells with continuous UV: for n <= 3 the repeats
  per chunk are whole, so chunk-local UV is the same picture, which is why the earlier rule held. Tile- and
  map-continuous UV cannot be told apart below nibble 8 (the 2-chunk period divides the 16 chunks of a tile). Chunks of
  6-8 layers: PROVEN for the baked composite, INFERRED at draw time.
- **Implementation (round 3)**: repeats = 8 / 2^n as a float (8, 4, 2, 1, 0.5 for n = 0..4; 0 for n = 5..15, which
  stay LayerEffectUnresolved), a per-chunk, per-layer phase (frac(ix * r), frac(iy * r)) added to the diffuse and the
  height coordinate, identically in the C# compositor (`TerrainMaterialBuilder.RepeatsForScaleExponent`,
  `RepeatPhase`, `W/TerrainMaterial.cs:552-568`; `TerrainCompositor.LayerCoordinate`, `:1255-1259`), the shader
  (`WMV_LAYER` / `WMV_HEIGHT`, `A/Resources/WmvTerrainTextured.shader:278-316`) and the Python reference
  (`scale_repeats`, `chunk_phase`, `T/terrain_material_reference.py:627-645`); up to 8 drawn layers with a second alpha
  slice (section 8). Every n <= 3 composite is bit-identical before and after (17 SHA-256 digests of 32-px composites,
  128-px validation grids and alpha arrays of Retail 30_49, 32_50, 34_46, map 1923857 27_19 and MoP 31_48). 2923 32_24
  is then Textured 256 in C#, in Python and in an independent expectation script; its neighbours are Textured 256 each.
  On the verification binaries the player draws all 256 chunks textured; its V2 fits the map texture at 1.034 (nibble-4
  chunks 1.043, chunks of 6-8 layers 1.059), and V1 on four chunks covering every phase class and both alpha slices
  passes at 0.1245/255, while a reference without the phase fails them (section 15).
- **Mutants**: C# (failures synthetic / corpus) -- phase ignored 5 / 12; height phase only ignored 3 / 3; repeats as
  integer 8 >> s 9 / 7; layers 5..7 read from slice 0 7 / 6; slice fill into slice 0 8 / 12; `BuildAlphaArray` first
  slice only 1 / 0; no checker for repeats 0 1 / 0; plain-only list of repeats-0 ids 2 / 0; at most 5 drawn layers 14 /
  16: 9 of 9 caught. Player self-test (a second synthetic tile with nibble-4 chunks at odd ix / iy, an 8-layer chunk
  with a height texture on every layer, a 6-layer chunk and a carrier slot): the GPU against the CPU twin within
  0.091-0.249/255 (limit 0.5); removing the phase from the shader puts chunks 1, 17 and 48 at 222.662, 142.976 and
  10.604/255; dropping layer 6's height puts the 8-layer chunk at 36.615/255; with the carrier's wrap mode set to Clamp
  the carrier chunk is 303.2/255 off in production sampling (0.249 with Repeat).
- **Disputed findings, resolved.** (1) Nibble 4: Malformed under the round-1 rule ("0..3 observed") ->
  LayerEffectUnresolved in fix round 2 (the file is not broken, the rule unknown) -> implemented in round 3 on the
  evidence above. (2) More than 5 layers: LayerCountUnsupported with the reason "never observed in a drawable chunk",
  false on maps 2923 and 2858 -> implemented up to 8 in round 3; LayerCountUnsupported now means more than 8 layers,
  which `AdtParser` already rejects as Malformed (section 6.6).

## 6. The composite and the chunk states

### 6.1 The rule (8-bit alpha)

Identical, operation for operation, in `TerrainCompositor` (`HeightMixWeights` `W/TerrainMaterial.cs:1127-1152`,
`Compose` `:1204-1250`, `LayerCoordinate` `:1255-1259`, `HeightAt` `:1265-1285`, `LayerTexel` `:1290-1309`), the shader
(`frag`, `A/Resources/WmvTerrainTextured.shader:320-466`; the mix at `:419-443`) and the Python reference (`mix`,
`T/terrain_material_reference.py:968`):

```
n <= 8 layers; layer 0 is the base layer
a_k  (k = 1..n-1)  alpha map k at the fragment, 0..1 (bilinear, clamp; section 6.5); layers 1..4 from the chunk's
                   first alpha slice (R, G, B, A), layers 5..7 from its second slice (R, G, B)
w_0 = 1 - sum_k a_k;   w_k = a_k                       (sums never exceed 1: a chunk where they do is Malformed)
s_k = uv * r_k / 8 + phase_k                            layer k's texture coordinate (repeats and phase, section 6.3)
H_k = 1                                                 when MPHD & 0x80 is clear (MHID and MTXP heights ignored)
H_k = h_k * heightScale_k + heightOffset_k              when MPHD & 0x80 is set; h_k = the ALPHA of layer k's height
                                                        texture (MHID[index]) at s_k, 0 when MHID[index] = 0;
                                                        (scale, offset) = MTXP words 1, 2, (0, 1) without
p_k = max(0, w_k) * H_k;   m = max_k p_k;   p_k = p_k * (1 - clamp(m - p_k, 0, 1));   S = sum_k p_k
colour = sum_k (p_k / S) * texture_k.rgb(s_k)  (S <= 0: sum_k max(0, w_k) * texture_k.rgb)
colour = colour * MCCV (when present), then saturated to 0..1 in the production views
```

- Stored (authored) values throughout: the layer textures are sampled without sRGB decode and blended as stored.
  PROVEN for the map generator (stored-value blending 1.98 / 2.46 / 2.73 / 3.44 / 2.34 against linear 3.27 / 8.15 /
  4.15 / 11.30 / 7.66 on 32_50, 32_48, 29_50, 34_46 and Beta 32_48, with filtering held fixed); INFERRED for the client.
- The diffuse textures' alpha channel is not read; a height texture contributes only its alpha. A height texture whose
  BLP has no alpha channel (alpha depth 0) cannot give h: its height role fails (section 6.6).
- **The height-value domain** (fix round 2). The mix is defined only where every H_k >= 0 and finite; mixed signs make S
  any sign and the weights unbounded near S = 0 (H = (0.3, -1) at w = (0.5, 0.5) gives S = -0.025). On an MPHD 0x80 map
  a layer whose MTXP entry has offset o < 0, or -- when its MHID entry is not 0 -- s + o < 0 or s + o not finite in
  float32 (s = height scale), makes the chunk **LayerEffectUnresolved** (`HeightValuesOutsideDomain`,
  `W/TerrainMaterial.cs:955-963`; Python `height_values_undefined`, `T/terrain_material_reference.py:615`). A non-finite
  s or o is Malformed on every map; on a 0x80-clear map the values are ignored (H = 1). Within the domain H stays in
  0..max(o, s + o), and only S == 0 (every p_k = 0, e.g. H_k = 0 exactly) reaches the fallback -- the contract's, itself
  unverified. No real data is outside the domain: over the 29 distinct MTXP tables read the smallest offset is 0.1 and
  no s + o is negative (section 2.3), so the fallback never triggers on real data. C# and Python agree on 52 crafted
  rows of real bytes (s + o = 0, one ulp below 0, the float32 overflow tie and just below it, -0.0, denormals, MHID 0
  with o < 0 or s = infinity) but one that Python cannot express (a diffuse-only failure of an id with both roles, which
  the player never produces).
- Status: PROVEN for the game's baked composite (nine golden tiles with and without MPHD 0x80, height-textured chunks,
  nibble-4 layers and chunks of 6-8 layers included); INFERRED for the client's draw-time shader; on MoP INFERRED (no
  oracle).

### 6.2 Alpha formats

| Storage | Status | Evidence |
|---|---|---|
| 8-bit raw (4,096 B) | IMPLEMENTED | 1,364 window layers exactly 4,096 B; decode = the stored bytes |
| RLE (MCLY 0x200) | IMPLEMENTED | 42,086 layers decode exactly (section 2.5) |
| 4-bit raw (2,048 B, low nibble first) | **UNSUPPORTED** (AlphaUnsupported, drawn neutral) | storage PROVEN (25,685 Era layers); draw-time expansion UNRESOLVED |

Why 4-bit stays unsupported. Blizzard's offline converter rule is proven: Era maps 2853 and 2875 tile 34_50 are 8-bit
copies of Era Azeroth 34_50's 4-bit layers (same layer lists in 256 of 256 chunks), and their bytes equal sequential
layering with the table T = 0, 30, 46, 62, then 16n + 15 for n >= 4, within +-1 for 90.9% of texels (17n: 70.8%; 16n:
65.4%); Retail's 8-bit maps of the same terrain equal the Era overlays converted to weights with A = (16n + 15)/255 to
2.2/255 per layer on 7.2 M overlapping texels (17n: 6.3; independent per-layer expansion: 42.4); the Era dominant-layer
table agrees only with 16n + 15 (100% of 714,688 cells; 17n 96.76%; 16n 93.40%). All of that is the **offline tool's**
rule. What the Era client does when it draws a nibble is not shown by any of it: no unlit game image of 4-bit data
exists (every WDT with map textures has MPHD 0x84: Retail 467, MoP 150, Beta 25, Era 20, counting name-opened copies),
external references state 17n (one 16n), and a paired-minimap test is not decisive (a luminance regression leans to 17n,
b = 1.05 [0.92, 1.20]; a per-channel regression to the converter, b = -0.13 [-0.36, 0.07]; the effect is below DXT1
quantisation). The candidates differ by at most 15/255 of opacity. The evidence that would settle it is in section 12;
until then no expansion is applied and the shader never sees 4-bit data.

### 6.3 Texture coordinates

- UV0 per vertex in cell units (`TerrainMaterialBuilder.VertexUv`, `W/TerrainMaterial.cs:570-579`): outer vertex
  (r, c) -> (c, r), inner -> (c + 0.5, r + 0.5); u east, v south; the same 145 values for every chunk
  (`WmvTerrainMaterials.ChunkUv`, `A/Scripts/WmvTerrainMaterials.cs:738-748`).
- Layer texture coordinate **s = uv * r / 8 + phase** (`TerrainCompositor.LayerCoordinate`,
  `W/TerrainMaterial.cs:1255-1259`). **r = 8 / 2^n as a float, n = (MTXP flags >> 4) & 0xF = 0..4** (8, 4, 2, 1, 0.5;
  exact in float; `RepeatsForScaleExponent`, `:552-557`), 8 without MTXP. **phase = (frac(ix * r), frac(iy * r))** of
  the chunk (ix = index % 16, iy = index / 16; `RepeatPhase`, `:559-568`; set in `BuildChunk`, `:754-758`): 0 for whole
  repeats, 0 or 0.5 at nibble 4. **Nibbles 5..15** give r = 0 and make the chunk LayerEffectUnresolved; in the
  plain-weights diagnostic such a layer is drawn as the missing-texture checker (the player binds no texture for it,
  the CPU twin returns the checker, its texture is neither listed nor fetched). A height texture is sampled at its
  layer's s, with its layer's repeats and phase.
- PROVEN for the map generator: 8 repeats per chunk (one per 25/6-yard cell): high-pass correlation on 32_50 at 1 / 2 /
  4 / 8 / 16 repeats 0.43 / 0.57 / 0.70 / **0.90** / 0.80, the maximum at 8 on all five tiles tested; zero phase: 0.856
  against 0.26 or less one pixel off; texel (0, 0) at the north-west corner, no flip (identity best of 8 symmetries,
  0.798-0.875 against at most 0.793 for the next). The MTXP nibble sets the repeats, not the texture's pixel size, and
  applies without MCLY 0x800/0x1000 (one external reference gates it on those bits; none is set anywhere): separating
  textures on two Khaz Algar tiles fit best at 1 (4688401, 1024 x 1024, 0x30), 2 (4704118, 512, 0x20) and 4 repeats
  (5243566, 1024, 0x10); the golden tile 1917 27_19 uses nibbles 1, 2 and 3 and fits at 0.978. Nibble 4: section 5.4.
- Neighbouring chunks and tiles: for nibbles 0..3 every repeat count is whole (8, 4, 2 or 1), so the chunk-local
  coordinate is continuous across chunk and tile edges; at nibble 4 the phase makes it continuous across chunk edges,
  and across a tile edge too (ix 15 has phase 0.5, so its east edge is at 1.0 = 0, the next tile's ix 0). Tile- and
  map-continuous UV differ only from nibble 8 (a period of a tile or more), never observed. The geometry is never
  altered.
- Slopes: UV is the horizontal grid coordinate, a top-down planar mapping. That is what the baked composite shows and
  what the external references' terrain code does (INFERRED for the client); on a steep face texels lengthen along the
  slope.

### 6.4 Vertex colour (MCCV)

- 145 x (B, G, R, A); colour = byte / 127 per channel, interpolated over each cell's four-triangle fan; applied only
  when the chunk has flag 0x40 and an MCCV chunk, and only to chunks that keep layers (drawable, LayerEffectUnresolved,
  HeightTextureMissing); NoLayers and other neutral chunks get none (`Plain`, `W/TerrainMaterial.cs:870-899`). The mesh
  carries it as a float RGBA vertex colour (16 B per vertex), so 127 is exactly 1.0 and no 8-bit rounding is added
  (`ChunkColors`, `A/Scripts/WmvTerrainMaterials.cs:751-760`).
- Order B, G, R PROVEN (section 2.2). Divisor UNRESOLVED: 127, 128 and 2/255 cannot be separated. On whole tiles the
  comparison is confounded by a global gain (on 32_50, which has no MCCV, /128 "improves" the fit only by darkening);
  restricted to MCCV chunks, 34_46 prefers /128 (SSE -14,084 [-19,300, -9,209]) and Beta 32_48 prefers /127 (SSE +19,457
  [16,286, 22,751]); 32_48 shows no preference. 127 is the neutral value the data use (the mode of every colour byte).
  The V2 per-pixel gate would catch a player that divided by 128 against a reference that divides by 127 (1.336/255
  against the 0.26 limit; a divisor of 127.25 gives 0.425, section 10.3); it cannot tell which divisor the game uses.
- MCCV takes some composites above 1 (34_46: 27 non-lava chunks, 3,277 px at 32 px per chunk, up to 1.64): the
  production views saturate (the map texture's 8-bit range; INFERRED that the game clips too), and the dumps count the
  clipped pixels.

### 6.5 Alpha placement and filtering

- Texel i of a 64-texel side is centred at (i + 0.5) / 64 of the chunk (`AlphaTexelCentre`, `W/TerrainMaterial.cs:546`).
  PROVEN for the map generator: on 252-256 of 256 chunks of Retail 32_50, Retail 32_48, Beta 32_48 and Era 2853 34_50 it
  beats +-0.5-texel shifts and the i/63 edge-to-edge layout; a fine scan on 32_50 has its minimum at 0 (per-pixel RMSE
  4.719 at 0; 4.748 / 4.753 at +-0.125; 4.870 / 4.879 at +-0.25; 5.32 / 5.34 at +-0.5). INFERRED for the client.
- Production: the hardware's bilinear filter on the alpha array, Clamp, no mips, anisotropy 0, from the chunk's first
  slice and, for a chunk of more than 5 layers, its second. Clamp per chunk is the only consistent choice: neighbouring
  chunks list their layers differently. Validation: four Loads and float weights, indices clamped to 0..63
  (`TerrainCompositor.AlphaAt`, `W/TerrainMaterial.cs:1095-1113`; shader `:240-256`).

### 6.6 The nine chunk states

Precedence top to bottom; the first that applies wins (header `W/TerrainMaterial.cs:61-86`, enum `:110-123`; conditions
in `BuildChunk` `:734-858`, `MalformedReason` `:902-943`, `HeightValuesOutsideDomain` `:955-963`; texture failures in
`Reapply` `:403-458`).

| State | When | Drawn in the textured views |
|---|---|---|
| Malformed | a malformed tex0 or root MCNK (the parser's verdict, more than 8 MCLY entries included: `W/AdtParser.cs:381`, `:407-411`); for a chunk with layers: a malformed MDID, MHID or MTXP table (repeated, not whole entries), an MHID count unlike MDID's (when MHID exists), MTXP entries != MDID entries; a texture index outside MDID; a non-finite MTXP height scale or offset; a layer without a decoded alpha map; MPHD & 0x84 disagreeing with a layer's stored depth; an 8-bit alpha sum above 255 at any texel. An unreadable MHID is never read as "no height textures" | neutral |
| AlphaUnsupported | any 4-bit layer; MCNK flag 0x8000 clear | neutral |
| LayerCountUnsupported | more than 8 layers. Unreachable from parsed files (`AdtParser` makes more than 8 MCLY entries Malformed first); kept for any other layer source and reached in the tests by appending a layer to a parsed chunk | neutral |
| LayerEffectUnresolved | a layer with an MCLY bit outside 0x100, 0x200, 0x40, 0x80, 0x7, 0x38 (0x400 cube map, never-observed bits); MTXP flag bits outside 0xF0 (0x1 = cube-map texture); MTXP word 3 != 0; an MTXP scale nibble 5..15 (never observed); on an MPHD 0x80 map, MTXP height values outside the mix's domain (6.1) | neutral (its alpha slices kept for the plain-weights diagnostic; a layer of unresolved repeats is the checker there) |
| HeightTextureMissing | MPHD 0x80 and a drawable chunk's layer names a height texture that could not be had (any state), or whose BLP has no alpha channel (alpha depth 0: the height role fails alone, "decode failed" with the detail "no alpha channel (BLP alphaDepth 0); the height is the alpha", and the same id stays usable as a diffuse texture) | neutral (slices kept) |
| NoLayers | no layer | neutral |
| LayerEffectStatic | a layer with MCLY 0x40 (with the rotation and speed bits) or 0x80 | textured, the static composite; the animation and overbright are not applied. A failed diffuse texture does not change the state (precedence): its layer draws the checker, the settle log counts the chunk ("... of them layerEffectStatic, which keeps its state"), and the golden and V2 masks exclude every chunk with a checker layer |
| MissingTexture | a layer's diffuse id is 0, or its texture could not be had | textured; the missing layer is a magenta / black checker (one square per 1/8 cell, magenta where floor(8u) + floor(8v) is even) weighted like the texture it stands for |
| Textured | everything verified | textured |

- Notes, counted, drawing unchanged: MCSH present ("shadow map ignored"); MCCV present without flag 0x40 or the reverse
  (then MCCV is not applied).
- Texture failures are applied after the fetch (`ApplyTextureFailures`, `ApplyHeightTextureFailures`,
  `W/TerrainMaterial.cs:373`, `:386`); they never renumber alpha slices. On the player, `SettleFailures`
  (`A/Scripts/WmvTerrainMaterials.cs:360`) applies them, skipping textures still requested or queued.
- Tile state: `textured` (all 256 chunks Textured), `partial` (some drawable, not all Textured), `unsupported` (a tex0
  was read, no chunk drawable), `unavailable` (no tex0 in any state, a protocol-12 host, or a material build that
  failed). The material never fails a load: the geometry is still built and drawn neutral.
- The checker aliases at whole-tile scale: in the 512-px composite of Retail 169 24_27 every checker pixel samples
  magenta (256,600 magenta, 0 black), and a viewport shot shows moire; at 64 and 2,048 px per chunk it is exact.
  A presentation note, within the design's "one square per 1/8 cell".

## 7. Feature classification

| Feature | Classification | Evidence |
|---|---|---|
| Diffuse layers (MDID) | IMPLEMENTED | sections 2.4, 5.3 |
| 8-bit raw alpha | IMPLEMENTED | 2.5 |
| RLE alpha | IMPLEMENTED | 2.5 |
| 4-bit alpha | UNSUPPORTED (expansion UNRESOLVED) | 6.2 |
| Up to 5 layers | IMPLEMENTED | Beta 31_47 MCNK 48; 11 Beta window chunks |
| 6-8 layers | IMPLEMENTED (round 3); PROVEN for the baked composite, INFERRED at draw time | 2923 32_24: 115 chunks at 1.079; 5.4 |
| More than 8 layers | NOT PRESENT (never observed; `AdtParser` makes more than 8 MCLY entries Malformed; LayerCountUnsupported for any other layer source) | 2.4, 6.6 |
| Height-mix cutoff | IMPLEMENTED | PROVEN for the baked composite, 5.2-5.3 |
| Height textures (MHID) with MTXP scale and offset | IMPLEMENTED on MPHD 0x80 maps; ignored without 0x80 | 30_49 1.203 against 7.405; 1917 27_19 full formula 18.606 against 0.978; height sampling at the layer's own repeats and phase, alpha channel (alternatives 1.324 / 1.645 / 2.606) |
| MTXP height values outside the mix's domain (o < 0; s + o < 0 or not finite with a height texture) | UNSUPPORTED (LayerEffectUnresolved); NOT PRESENT in real data | 6.1, 2.3 |
| Height texture without an alpha channel | an explicit failure (HeightTextureMissing, the height role only); no real case known | 6.6 |
| MTXP texture scale, nibbles 0..3 | IMPLEMENTED (8, 4, 2, 1 repeats) | 6.3 |
| MTXP texture scale, nibble 4 | IMPLEMENTED (round 3): 0.5 repeats, tile-continuous phase; PROVEN for the baked composite, INFERRED at draw time and on maps other than 2923 | 5.4 |
| MTXP texture scale, nibbles 5..15 | UNSUPPORTED (LayerEffectUnresolved; never observed) | 2.3 |
| MCCV vertex colour | IMPLEMENTED; divisor UNRESOLVED | 6.4 |
| MCLV vertex lighting | NOT PRESENT | 0 chunks in any client |
| MCSH shadow map | UNRESOLVED (bit layout PROVEN: 64 x 64 bits, row-major, least significant bit first: across byte boundaries the mismatch rate equals the in-byte rate, 3.1%, against 13.9% read most significant bit first; the darkening is unknown: external references disagree, 0.7 on alpha against 1/3 on colour, and the Era minimap shows no MCSH effect); counted, not applied | Era 6,976 and MoP 1,627 chunks |
| MCNR normals | UNRESOLVED (audit U9); stage 03 normals are geometry-derived; not used by the materials | 03 section 8 |
| Normal or specular textures per layer | NOT PRESENT as separate files (tex0 references only MDID and MHID); the diffuse `_s` alpha is UNRESOLVED (INFERRED specular), not used | 2.6 |
| Tile normal map (MAID word 6) | VERIFIED BUT DEFERRED (R east, G up, B south; correlation 0.97-0.99 with the slope normals on 3 tiles) | 2.8 |
| Root dominant-layer table (+64..+79), Beta MPTX | VERIFIED BUT DEFERRED (99.73-100% agreement; MPTX 64 of 64 cells in 5 of 5 chunks); used as an oracle only | 2.2 |
| MCLY 0x40 animation (0x7 rotation, 0x38 speed) | static composite IMPLEMENTED (LayerEffectStatic, about 1.2 against the baked texture); the animation UNRESOLVED (direction and speed units unknown) | 5.2 |
| MCLY 0x80 overbright | static IMPLEMENTED; the brightness factor UNRESOLVED (no reference gives one; the baked texture shows none) | 5.2 |
| MCLY 0x400 cube map, MTXP 0x1 | UNSUPPORTED (LayerEffectUnresolved, neutral); MoP only, no evidence of how it draws | T14 MCNK 66 |
| Ground effects (MCLY word 12) | UNRESOLVED; not a texturing field, outside this stage | 2.4 |
| Terrain lighting and shadows | UNRESOLVED (audit U16); outside this stage; `texturedlit` is a labelled diagnostic light only | spec 12, 20 |
| MAMP | UNRESOLVED meaning; always 4 zero bytes; not used | 2.3 |
| MTXF, MTEX | NOT PRESENT | 2.3 |
| Chunks without MCNK 0x8000 | NOT PRESENT (AlphaUnsupported if met) | 2.5 |
| Transparent-black placeholder (188903) | IMPLEMENTED as RGB black, its alpha ignored: where its weight is at least 0.8 the map's luminance is 3.9 against 6.1 predicted | design review |
| MoP Classic | corpus-level states during the stage (storage); the storage opens again since late 2026-10-13 and one tile (0 32_48) was built textured in the final verification; then end to end on 0 32_48 and 31_48: states, V1 and texels PASS (IMPLEMENTED, rule INFERRED: no oracle) | 3 |

The terrain is not visually complete: MCSH, lava animation and overbright, cube maps, 4-bit Era alpha, specular and all
lighting are absent, and no in-game reference has been compared.

## 8. Chosen architecture

### 8.1 Measured constraints (Unity 6000.5.9f1, URP 17.5.0)

| Constraint | Value | Label |
|---|---|---|
| Graphics API | Direct3D 12, feature level 12.2; shaders compiled to shader model 5.0 bytecode | PROVEN (player log, player data) |
| Project | linear colour space; URP Forward+; SRP Batcher on; HDR buffer raised to FP16 at run time; LDR grading with a 32-node LUT; MSAA off; anisotropic filtering Forced On (levels 1-9 raised to 9) | PROVEN (project settings, run-time log) |
| Samplers / textures per stage | 16 / 128; the 17th sampler and the 129th texture fail to compile | PROVEN (the system shader compiler) |
| Texture arrays | ps_4_0 and up; every slice the same size, format and flags; 2,048 slices; 16,384 maximum size | PROVEN + DOCUMENTED |
| Block formats | `TextureFormat` has no DXT3; `GraphicsFormat.RGBA_DXT1/3/5_UNorm` and the GraphicsFormat constructors exist; sampling support queried once at run time (`WmvTerrainFormatSupport.Query`, `A/Scripts/WmvTerrainMaterials.cs:59-72`) | PROVEN (engine metadata) |
| Per-renderer data | a MaterialPropertyBlock makes a renderer SRP-Batcher-incompatible; `SetShaderUserValue` exists but needs HLSLPROGRAM | DOCUMENTED + PROVEN |
| Terrain texture census | 39 of 56 Retail window tiles mix formats or sizes; per Retail tile 10 textures (median, max 23), 0.87 MB as stored (max 2.27 MB) against 3.5 MB as RGBA32 with mips (max 9.1 MB); worst Retail tile 45.5 MB stored against 181.8 MB as RGBA32 (90.9 MB with its height textures: Retail map 2923 32_24) | PROVEN |

### 8.2 Options

| Option | Measured cost | Verdict |
|---|---|---|
| **A. Conventional samplers: each layer and height texture a `Texture2D` in its native format, size and stored mips; one alpha `Texture2DArray` per tile; one material per drawn chunk** | after round 3: 8 layer slots and 8 height slots plus the alpha array = 17 of 128 textures on 9 of 16 samplers (the height textures share their layer slot's sampler); any size and format mix; no decode for DXT; texels are the GPU's decode of the stored blocks; 16 KiB of alpha per slice (4,194,304 B per full tile of chunks with at most 5 layers) | **chosen** |
| B. One RGBA32 `Texture2DArray` of the layer textures per tile | slices must share size and format, so everything is decoded to RGBA32: about 120 ms of worker CPU per median Retail tile at the player's measured 5.4 Mpx/s (INFERRED), four times the memory, the 1024 x 1024 texture resampled, the CPU decode about half a step darker than the GPU's; Beta's 5-layer chunks overflow a 32-bit per-renderer index | rejected |
| C. Block-compressed arrays bucketed by (format, size) | at most 3 buckets per tile in the corpus, but each chunk needs a 40-bit (bucket, slice) lookup; the most complex | rejected |
| D. Structured per-layer data through the per-renderer user value, one material per distinct layer tuple | 3-99 materials per Retail tile (median 37); needs HLSLPROGRAM | not adopted: one material per chunk keeps every value in `UnityPerMaterial` without a user value or a MaterialPropertyBlock, at stage 03's draw count (one renderer per chunk) |
| E. Compositing to a texture (CPU or GPU bake) | not measured in this stage. At the layers' own texel density (256 x 8 = 2,048 texels across a chunk) one RGBA8 chunk would be 2,048 x 2,048 x 4 = 16 MiB before mips, 4 GiB per tile (computed here); a lower density loses the texel detail V1 checks | not pursued; the only composite-to-texture path is the validation fixture |

### 8.3 Decision

- **Correctness**: the stored blocks and the stored mips are uploaded unchanged (`WmvTerrainTexture.Upload`,
  `A/Scripts/WmvTerrainMaterials.cs:271-296`), so the shader samples what the files hold; nothing is regenerated or
  rounded on the CPU. Only the fallback (a format the GPU does not sample, a size that is not whole blocks, palettised
  or BGRA files) decodes on the CPU, with the exact block mode (2.7).
- **Layer counts** (round 3): up to **8 layers** -- 8 layer slots `_WmvLayer0..7`, 8 height slots `_WmvHeight0..7`,
  `ShaderLayers = 8` (`A/Scripts/WmvTerrainMaterials.cs:458`) -- with up to **two alpha slices** per chunk: the first
  holds alpha layers 1..4 in R, G, B, A, a second -- only for a chunk of more than 5 layers -- layers 5..7 in R, G, B
  (A = 0; `TerrainChunkMaterial.AlphaChannelOf`, `W/TerrainMaterial.cs:202-208`). The drawable chunks' slices are
  numbered first, a chunk's second slice right after its first (`_WmvAlphaSlice`, `_WmvAlphaSlice2`). Up to 8 layers is
  every chunk observed; more is an explicit state.
- **Samplers**: 8 layers, 8 height textures and the alpha array would need 17 sampler states; D3D11 allows 16. The
  shader declares **9 samplers** -- one per layer slot and the alpha array's -- and samples height texture k with layer
  slot k's sampler (production only; validation reads by Load) (`A/Resources/WmvTerrainTextured.shader:142-158`).
  Diffuse and height textures are uploaded with identical sampler settings, so it is the sampler the height texture's
  own would be. A layer slot without a texture (a missing layer: id 0, failed, or repeats unresolved) holds the tile's
  **sampler carrier** (`CreateSamplerCarrier`, `A/Scripts/WmvTerrainMaterials.cs:599-620`): a 4 x 4 R8G8B8A8_UNorm
  texture with 3 mips (84 B), mid grey, Repeat, trilinear, anisotropy 9, mip bias 0 -- the layer textures' settings --
  never drawn (the slot's colour is the checker); an unbound slot would hand its height read Unity's default texture's
  sampler. One per tile, created with the alpha array when a chunk is drawn textured, held by the tile the moment it
  exists and released with it; not in the texture tallies (`WmvRuntimeTerrainTile.SamplerCarrier`,
  `A/Scripts/WmvTerrainBuilder.cs:344-347`, created at `:793`). The self-test shows the slot's sampler is the one used:
  with the carrier set to Clamp the carrier chunk is 303.2/255 off in production sampling, 0.249 with Repeat.
- **Uniforms**: per-layer values four to a vector, P for layers 0..3 and P47 for layers 4..7 (`_WmvRepeats`,
  `_WmvPhaseU`, `_WmvPhaseV`, `_WmvMissing`, `_WmvHasHeight`, `_WmvHeightScale`, `_WmvHeightOffset` and their `47`
  partners), all in the `UnityPerMaterial` CBUFFER with the slices, the layer count and the switches
  (`A/Resources/WmvTerrainTextured.shader:160-186`); `LayerValue` reads one back for the self-test.
- **Memory**: textures as stored (block-compressed), one per FileDataID per tile; one 64 x 64 alpha array per tile
  (`R8G8B8A8_UNorm`, rows as stored, Clamp, bilinear, anisotropy 0, no mips, no CPU copy; `BuildAlphaArray`,
  `A/Scripts/WmvTerrainMaterials.cs:570-589`) with `DrawableSlices` (+ `PlainSlices` in the plain-weights view) slices:
  4,194,304 B for a full tile of chunks with at most 5 layers; Retail 2923 32_24 needs 371 slices (256 + 115 second
  slices), 6,078,464 B.
- **Draw calls**: one renderer per chunk with triangles, as in stage 03 (256 for a full tile); one material per chunk
  drawn textured, as before; no neutral material is created when no chunk needs it.
- **Shader compatibility**: HLSLPROGRAM, explicit float, uniforms only (the player build strips keyword variants),
  `UnityPerMaterial`, no MaterialPropertyBlock on textured renderers (`A/Scripts/WmvTerrainBuilder.cs:849-858`); the SRP
  Batcher's conditions are met, whether it batches these draws is not measured. The MaterialPropertyBlock stays only on
  neutral `Wmv/Terrain` renderers (`:876-881`).
- **Future streaming**: section 13.
- **Filtering**: layer textures Repeat, trilinear, anisotropy 9 (`LayerAnisoLevel`,
  `A/Scripts/WmvTerrainMaterials.cs:449`), mip bias 0 (never set; asserted by the self-test): what every other texture
  of this player gets under the forced setting. The game's terrain anisotropy is UNKNOWN; anisotropy has no effect on
  the top-down validation renders.

### 8.4 Colour path

1. BLP blocks uploaded as `*_UNorm` (never `*_SRGB`): sampling returns the stored values.
2. The blend runs on those authored values (6.1), times MCCV, saturated.
3. The fragment writes authored values; `Wmv/FrameDecode` applies the exact sRGB curve once per frame before
   post-processing (display transform "full"). Under `_WmvShaderEncode` (the per-fragment display mode) the shader
   encodes itself; in the A/B modes without the authored domain (`notonemap`, `legacy`) a per-material `_WmvLinearOut`
   converts the composite so it displays as in the default mode (`A/Resources/WmvTerrainTextured.shader:460-461`).
4. No new tone mapping, brightness, saturation or contrast: tone mapping and vignette stay forced off; the existing
   bloom (threshold 1, intensity 0.25) and the LUT are unchanged and appear in viewport shots.
5. The validation dumps bypass all of it: a CommandBuffer render into a float target with `_WmvShaderEncode` 0, the view
   pinned to `textured` and `_WmvLinearOut` 0 for the render (`Render`, `A/Scripts/WmvTerrainComposite.cs:72-182`;
   `ChunkDraws`, `:201-252`).

### 8.5 Views

| View | What it draws |
|---|---|
| `shaded`, `chunks`, `wire`, `shadedwire`, `normals` | stage 03's geometry diagnostics, unchanged (holes visible in every view) |
| `textured` | the composite, saturated; the default when the material is `textured` or `partial` (otherwise `shaded`) |
| `texturedlit` | the composite times stage 03's fixed diagnostic Lambert light (north-west key): a labelled DIAGNOSTIC illumination, not the game's |
| `texturedplainweights` | every chunk with layers in plain weights (no cutoff, no heights) under a diagonal hatch: a labelled comparison, never the composite; a layer of unresolved repeats is the checker |
| `materials` | every chunk in its state's colour under the diagnostic light; the legend is logged |

Neutral chunks draw with `Wmv/Terrain` in every textured view (`A/Scripts/WmvTerrainBuilder.cs:97-98`, view names). The
status line and the viewport-shot log line say what was **drawn**, counted from the chunk objects shown
(`WhatTheViewShows`, `A/Scripts/WmvMainTerrain.cs:349-351`, `:367`; fix round 2 and round 3): the view's words and how
many chunks were drawn textured; "NEUTRAL: M of T chunk(s) drawn in GEOMETRY DIAGNOSTIC shading (lit grey, not WoW
material)" when neutral chunks are shown; "VALIDATION sampling (mip 0 by Load, unfiltered -- not the production look)"
under `-wmvTerrainValidate`; and, when no chunk shown is drawn textured, the reason -- the shader missing, "the chunk
shown (-wmvTerrainChunk N) is not drawn textured" (or "has no triangles"), or "no chunk of the tile is drawn textured".

## 9. IPC: protocol 13

Host `PROTOCOL_VERSION = 13` (`Source/wowmodelviewer/UnityIpcServer.h:382`), player `ProtocolVersion = 13`
(`A/Scripts/WmvIpcClient.cs:250`). The design is stage 03's bounded IPC, extended.

| Element | Rule | Where |
|---|---|---|
| Offer | to a player that announced 13, `loadTerrainTile` adds `maxTextureBytes` 8,388,608, `maxTextureBytesPerLoad` 134,217,728, `maxTexturesPerLoad` 128; `maxAssetBytes` (4 MiB) stays the limit of the WDT, the root and the tex0 | `UnityIpcServer.cpp:1369-1375`, `.h:389-394` |
| Roles | `getMapAsset` adds `tex0Adt` (after this load's WDT was answered available; once per load) and `texture` (after the tex0 was answered available; each id once; at most 128 ids; refused before reading once 128 MiB were read, else read with min(8 MiB, what is left)); every load reads at most 3 + 128 files | `modelviewer.cpp:3495-3539` |
| Kind check | after the read: tex0 = `REVM` then `PMAM` at 8 + MVER size; texture = `BLP2`; else "unreadable" with what was found; a failed tex0 does not count as served | `modelviewer.cpp:3571-3605` |
| md5 | the content key the storage read checked every byte against, never a second hash | `WorldMapCatalog.cpp:454-480`; `modelviewer.cpp:3790-3802` |
| Bytes counted | every outcome that entered the read loop counts the file's size (an upper bound for a read that failed partway); too large or not local counts 0 | `WorldMapCatalog.cpp:459-465` |
| Queue | one first-in first-out queue for every role; decided, accounted and read only when served; at most 16 waiting (`MAP_ASSET_QUEUE_MAX`), the 17th "refused: too many map requests waiting" unread; dropped with the connection | `UnityIpcServer.h:397`, `.cpp:1494-1501` |
| Per-poll budget | a poll stops serving after 2 MiB read or one file over 1 MiB; the newest 256 polls' timings kept | `UnityIpcServer.h:398-400`, `.cpp:1509-1549` |
| Load ended | a `terrainTileLoaded` for the current load ends it on the host: its later requests are answered "stale: load N was reported <status>" unread | `modelviewer.cpp:3476-3478`, `:3814-3818` |
| Player | at most K = 4 requests outstanding (the WDT alone, then root and tex0, then textures, diffuse first, a texture counting until its upload data are ready); every pending id retired at load end into a ring of 128; one 30 s timeout over the whole load; each answer checked against its role's limit | `A/Scripts/WmvTerrainSession.cs:111-117`, `:524`, `:846`, `:917`, `:952`, `:1270` |
| Report | `terrainTileLoaded` adds `requestsRetired`, `retiredRequestIds` and `material` (state, reason, tex0 id/bytes/md5, alpha format, the nine counters and notes, the texture list with `role` diffuse or height, counts, bytes, timings); the texture list is bounded at **4,096 entries (256 chunks x 8 layers x 2 roles**; 2,560 = 256 x 5 x 2 before round 3), at most 128 of them in a requested or answered state; retired ids and counters at 128 | `UnityIpcServer.h:275-287`, `:926-934` |
| runtimeState | adds `terrainTextures`, `terrainTextureBytes`, `terrainAlphaArrays` | `UnityIpcServer.h:286-287` |
| Host log | each report texture entry logged with the player's `format` text appended, never passed through `QString::arg()` (a format string from the player could shift the later arguments; final review) | `UnityIpcServer.cpp:1880-1882` |
| Harness | `-terraintile` checks the report lists at 4,096 entries (`app.cpp:6007`; on the verification binaries 4,096 entries are kept and accepted and 4,097 counted and refused, both PASS), and its texture-count check is view-aware: in a view that draws no textures (`materials`, `chunks`, the stage 03 views) the player must count none, in a textured view some for a `textured` material, none for `unavailable` (`app.cpp:6747-6783`) | `app.cpp` |

- **Per-texture states**, quoted verbatim in the report: from the host available, not in client storage, not installed
  locally, unreadable (encrypted, too large, damaged, corrupt, unverified, empty, kind), unsupported, not checked,
  stale, refused; from the player no texture id, not requested, md5 mismatch, decode failed, transport failure, and --
  only in the report of a load that ended before its textures settled -- requested and queued
  (`A/Scripts/WmvTerrainMaterials.cs:92-100`).
- **Reports of loads that end before adoption** (fix round 2): the material is settled with the answered failures
  first; textures still unanswered are named in the reason ("the load ended before its textures settled: N texture(s)
  unanswered; chunk states count the answered failures only", `A/Scripts/WmvTerrainSession.cs:1193-1206`). Such a report
  still carries the material's settled state (e.g. "textured" with 0 materials): a consumer must read `status` first.
- **Spec 4's guarantees**: client identity and epoch (every request carries `load` and `assetEpoch`; stale checks run
  before any read; the catalogue generation covers every role); map and tile identity (the tex0 only after the WDT the
  load named, taken by the player from that WDT's MAID word 3; textures only after that tex0); cancellation (supersede,
  clear, model load and client switch end the job and retire every pending id; the host's request is replaced, so later
  requests are stale); late-response isolation (`map<n>` ids and role routing, unchanged from stage 03); size limits
  (per role and per load, decided before reading and checked again on receipt); explicit failure states (above).
- **Documented limitations**: the host parses no tex0, so it cannot check that a texture id is one the tex0 names, nor
  that the tex0 is the MAID's (stage 03's root limitation, unchanged); it vouches for client, epoch, load, order, kind
  and limits.
- **Why the budget**: a 2048 x 2048 texture (5,593,604 B) cannot be served within stage 03's 20 ms per request on the
  GUI thread: read and verify measured 19.7-29.9 ms warm in the investigation, and integration round 2 measured
  31.7-34.7 ms median per handler call for the four such textures of Retail 2923 32_24 (section 17). A storage-owning
  reading thread (audit W3) is a later stage. A typical tile is cheap: in integration round 2 tex0 778040 (1,286,682 B)
  took 7.90 ms median (p95 8.50) per handler call, each 88,580-B texture 0.67 ms median (p95 0.84).
- **Compatibility, both ways**: a protocol-12 player is sent none of the new fields and never asks for the new roles;
  the harness keeps its stage 03 checks for it (retired ring 32). A protocol-13 player under a protocol-12 host finds
  no `maxTextureBytes`, asks for no tex0 or texture, builds the geometry, and reports material "unavailable", reason
  "the host serves no terrain textures (protocol 12)" (`A/Scripts/WmvTerrainSession.cs:122`, `:629`). Runs: PASS in
  integration round 2 and again on the fix-round-2 player (stage 03 player with this host 113/0/0 and its lifecycle
  sequence 225/0/0; this player with the protocol-12 host 61/0/0, material "unavailable" on all 5 loads), and with the
  same counts on the verification binaries.

## 10. The independent CPU composer and the validation design

### 10.1 Two CPU implementations

- **The Python reference** (`T/terrain_material_reference.py`): the audit's `wowfmt` chunk readers plus its own RLE and
  raw alpha decoders and its own BLP decoder (DXT1/3/5, palettised, BGRA) with three **named block decodes**
  (`DECODE_MODELS`, `:131`; `:195-245`): `exact` (bit replication, unrounded interpolation: the format), `hardware`
  (this GPU's interpolants, measured from the player's texel dumps: colour `hardware_interp5/6`, DXT5 alpha (a0 * (2048
  - W) + a1 * W + 1024) >> 11 with W = 289, 578, 892, 1156, 1470, 1759 for the eight-value mode and 385, 770, 1278, 1663
  for the six-value mode) and `rounded` (the format rounded to the nearest 8-bit value, the DXT1 midpoint's ties to
  even: what the player uploads when it decodes a block texture on the CPU). It shares no code with C#. It computes the
  states and precedence, the layers with repeats and phases, the vertex colours, the V1 composite at given positions
  from given texels (`validation_rows`), and the map-texture mode (each layer and its height texture from its own stored
  mip at LOD log2(texels per map pixel), a note for every level that is not exactly one texel per pixel, alpha at pixel
  centres, x MCCV, clipped to 8 bits). Its exact BLP decode is RGB-identical to the investigators' scratch decoder on 54
  extracted BLPs (levels 0 and 6).
- **The C# `TerrainCompositor`** (`W/TerrainMaterial.cs:1083-1345`): the shader's arithmetic in validation sampling,
  float throughout.
- **Cross-check**: on 145 whole tiles (69 Retail, 9 MoP, 9 Beta, 58 Era; 37,120 chunks) the reference and the C# builder
  agree field for field: states, diffuse and height id lists, reference counts, MCCV, MCSH and mismatch notes, drawable
  layers (0 differences; fix round 2, including the five map-2923 tiles under the fix-round-2 rules). 24 of 24 crafted
  cases (12 cases under MPHD 0x3CA and 0x34E) and 5 of 5 texture-failure cases agree; a checker's 52 crafted rows of
  real bytes (nibbles 4, 5 and 15, the height-value domain's boundaries, ids with both roles) agree on 51, the 52nd
  being a failure Python cannot express and the player never produces; 10 of 10 rows of a synthetic tile combining the
  new rules with 6 layers, 0x8000 clear, lava bits, MDID 0 and unknown MCLY bits agree. Under the round-3 rules, on the
  verification tree, the same 145 whole tiles agree field for field again, the layers compared with their repeats as
  floats and their UV phases (1,113 layers with a non-zero phase on 6 tiles): 0 differences against the repository
  reference and 0 against the round-3 independent expectation script (written by the part that made the C# change, so
  the repository reference is the part-independent comparison); 24 of 24 crafted and 5 of 5 failure cases agree; the
  five map-2923 tiles are Textured 256 on every side. On the final tree the same comparison gives 0 differences again,
  its C# outputs byte-identical to round 3's. The C# compositor against the reference's V1 on 4 real 2,048 x
  2,048 chunks: at most 0.00009/255; on hand values of 6-, 7- and 8-layer composites with MPHD 0x80 set and clear, at
  most 7.78e-8 from exact fractions. The other 264 enumerated tiles are truncated copies that neither side parses.

### 10.2 V1: GPU against CPU, per pixel

- A chunk is rendered alone (`-wmvTerrainCompositeChunk`) at N = the largest (texture size x repeats) over every layer
  it draws and every height texture it reads, each at its own layer's repeats (`ValidationSize`,
  `A/Scripts/WmvTerrainComposite.cs:276`; a whole power of two up to 8,192, otherwise "validation grid on texel
  boundary"; a 2048 texture at 0.5 repeats gives N = 1,024), by a CommandBuffer into an `R16G16B16A16_SFloat` target,
  top-down orthographic, no frame decode, bloom or LUT.
- Validation sampling (`-wmvTerrainValidate`): texels by Load at integer indices floor((u * r / 8 + phase) * W) mod W,
  alpha by four Loads with float weights. The reference composes from **the GPU's own texels** (the texel dump, Load of
  mip 0 into an `R32G32B32A32_SFloat` target), so the hardware's block decode cancels out of V1.
- Acceptance: every non-hole pixel covered and every hole pixel cleared; |dump - reference| <= **0.5/255** per channel.
  Rationale: the half-float target (the self-test measured 0.124-0.249/255 on synthetic chunks).
- Production sampling, once, only when every layer and height texture is at one texel per pixel on both axes at its own
  repeats (trilinear at LOD 0 at texel centres returns the exact texel): **2.0/255**. Rationale: the hardware's bilinear
  works on an 8-bit sub-texel grid (moving the fraction by one step changes the composite by 1.06-1.40/255 on four
  pinned chunks at 2,048 px) plus the 0.6 ULP of float-to-UNorm conversion. A saturated dump is compared with the
  clamped reference, and `clippedPixels` must equal the reference's count (pixels within the tolerance of 255 may count
  either way).
- Inputs are verified: the sidecar's build must be the pinned client build and its WDT, root and tex0 md5s (and each
  texture's md5 in the texel dump, one entry per role) must equal the verified audit files.
- Results: section 15.2.

### 10.3 V2: the production composite against the game's map texture

- The production view rendered top-down at 512 x 512 (32 px per chunk; at 8 repeats a 256 x 256 texture lands on its
  stored mip 6 at texel centres, a 2048 x 2048 texture at 0.5 repeats on mip 5) against MAID word 5, read only from a
  verified copy.
- Acceptance: 4 x 4-block RMSE over Textured and LayerEffectStatic chunks without a checker layer (holes masked) <= the
  golden threshold (5.3); the identity is the best of the 8 symmetries of the dump; zero phase better than +-1 px in x
  and in y (per-pixel RMSE); every kept pixel covered; and -- **mandatory since the final review** -- every kept pixel
  of the dump within **0.26/255** of the reference composite under the better-fitting of the three named decodes, which
  the check names (`V2_REFERENCE_TOLERANCE`; an 8-bit PNG given in place of the float dump is judged at **0.76/255**,
  `V2_PNG_REFERENCE_TOLERANCE`, the rounding to 8 bits included -- a weaker input, so named in the check text). The
  plain-weights composite's RMSE is reported.
- Rationale of the block threshold: the reference's own measured fit plus 0.25, not a generous constant: with
  box-filter thresholds (2.5 / 3.5) a regression of about 0.8 RMSE would have passed. The residual of about 1.1 lies
  below the map texture's own DXT1 steps (8/255 in red and blue, 4/255 in green). A block metric cannot see texture
  phase or orientation (a 4 x 4 block averages exactly one repeat at 8 repeats), hence the per-pixel phase check; and as
  5.2 showed, V2 alone cannot prove a rule, which is what the golden table's asserted alternatives are for.
- Rationale of the per-pixel gate, V2's own budget (not V1's: the V2 render has no sub-texel filter grid -- every layer
  and height texture lands on a texel centre of a whole mip, the alpha is sampled at fraction exactly 0.5 -- and no
  UNorm target): the dump is a float16 render whose store truncates; both sides are clamped to 0..1 before comparing and
  1.0 is stored exactly, so the store is off by less than one step of [0.5, 1), 2^-11 = 0.1245/255; before the store
  the GPU value differs from the reference by at least 0.0028/255 above it and 0.0045/255 below it on the nine golden
  dumps (lower bounds: no upper bound is measured; the source INFERRED). Budget 0.1245 + 0.0045, about 0.13/255; the
  limit is twice that, **0.26/255**, the margin covering the unmeasured pre-store part. Measured: **0.1235-0.1259/255 under the hardware model on all nine golden tiles**; under the exact
  decode alone up to **2.011/255** (Beta 32_48; 2.206 on 2923 32_24), which is why the reference must be composed under
  the GPU's measured decode. (Round 3 had set 0.5/255 on a budget that counted a 2^-10 step at 1.0, which clamped values
  never reach; its checker showed the limit was not derived from the budget, and that the 8-bit PNG input needed its own
  limit: the final round fixed both.)
- What the gate now fails (all of which passed the block threshold): a layer-3 height dropped on 30_49 (62.853 exact /
  62.821 hardware, block RMSE 1.401 still under 1.408), a -0.5 mip bias (7.052 / 7.043, block 1.236), a -1 bias and
  point sampling with a -1 bias, and an MCCV divisor of 128 against the reference's 127 (1.336, which passed at the
  round-2 default of 2/255); at 0.26/255 also a divisor of 127.25 (0.425) and 127.4 (0.609). A rounded-decode composite
  written through float16 passes, named "rounded" (0.062).
- Results: section 15.1.

### 10.4 Texels

- The GPU's decoded mip 0 of every texture against the reference's decode, dimensions equal to the BLP's mip 0, one
  entry per role (an id that is both a diffuse and a height texture has two entries naming one raw file).
- **Rule** (`T/compare_terrain_material.py`, acceptance text; fix rounds 2 and 3): textures whose BLP is not
  block-compressed, block endpoints (colour indices 0 and 1, a DXT1 three-colour block's transparent black), DXT3's
  explicit alpha and DXT5's alpha a0, a1 and its 6-value mode's 0 and 255 must come back exact (0.01/255,
  `TEXEL_ENDPOINT_TOLERANCE`: a float32 of k/255 returns k). The interpolated texels key on the entry's upload format:
  uploaded as blocks, every four-colour interpolant (all three channels) and every interpolated DXT5 alpha of a texture
  must equal **one named decode** within 0.01/255 -- this GPU's **hardware model** or the **exact ramp** -- and the
  check names it (mixed or neither: FAIL); uploaded as `R8G8B8A8_UNorm` (the player's CPU fallback), every interpolated
  value must equal the **rounded** decode and nothing else. A DXT1 three-colour midpoint of a block upload has no
  measured model: it must lie between its endpoints (+-1/255, `TEXEL_BOUND_SLACK`) and is counted as unmodelled (0 occur
  in the real data). A texture without an interpolant gets an information line, not a PASS. The deviation from the exact
  decode is reported with a histogram.
- Rationale: the design's "within 2/255 of an exact decode" assumed the hardware interpolates like the format;
  measurement disproved it (up to 5.333/255 colour, 6.4/255 DXT5 alpha off the exact ramp). Integration round 2 then
  used "endpoints exact, interpolants between their endpoints", which the final review showed accepts snapped, swapped
  and reversed selectors; the named-decode rule fails all of them. Exact endpoints prove the stored blocks are what was
  uploaded: a wrong offset, block size, format, sRGB flag or row order breaks them.
- **The hardware model is a measured property of this GPU, not of the format.** Colour: `HardwareInterp5/6` (the player
  self-test's model), 0 of 4,660 distinct channel inputs off. DXT5 alpha: the reviewer's first model was off on 1,102 of
  84,366 distinct (a0, a1, selector) inputs (its selector 2 and 7 weights were 1/2048 off); the weights were refit in
  /2048 on the Retail 30_49 dump alone (72,666 inputs; each weight the only one of 0..2048 that matches) and held out on
  Retail 34_46, Retail 32_48 and Beta 31_47 (31,964 inputs, 11,700 unseen): 0 off. Another GPU may need its own model;
  the check would then report "neither" and fail, loudly. The DXT1 four-colour model is confirmed only on the
  self-test's synthetic DXT1: no real DXT1 interpolant exists (the one DXT1 terrain texture dumped, 188903, 8 x 8, has
  none).
- The self-test goes further on its own synthetic blocks: interpolated colours equal one named decode (exact on 166
  DXT1, 171 DXT3 and 663 DXT5 interpolated texels), and the RGBA32 fallback equals an independent block decode rounded
  with `Math.Round` (section 2.7).
- Results: section 15.3.

### 10.5 Development runs before integration round 2 (for the record)

- Player `905365D9...` (plain weights), Retail 32_50: V2 1.713 against the then-threshold 1.994 (the run 5.2 describes).
- Player `F288CD8C...` (height mix), Retail 30_49: V2 1.117 <= 1.408; V1 production at most 0.86/255; texels FAILed the
  design's 2/255 (colour 5.333, alpha 6.2/255), which led to the rule of 10.4. Superseded by section 15.

## 11. Resource ownership and lifetime

- **Owner**: each tile is a `WmvRuntimeTerrainTile` that tracks every GameObject, Mesh, Material, `Texture2D` (with its
  GPU bytes) and `Texture2DArray` the moment it exists (`Track`, `A/Scripts/WmvTerrainBuilder.cs:403-423`), and holds
  its sampler carrier (`:344-347`); `Release` destroys them all and takes them off the static tallies at once
  (`:471-505`), deferred (`Dispose`) or immediate (`DisposeImmediate`, self-test). The carrier is in no texture tally
  (`terrainTextures` and the GPU byte totals count layer textures only) and is destroyed with the tile.
- **When objects exist**: Unity objects are created only inside `BuildTile`, at adoption. Until then the load's job
  holds the raw bytes and prepared upload data (managed memory), dropped with the job; a texture's prepared data are
  released right after its upload. Nothing of a load's content stays on the tile after the build (the alpha slices and
  vertex colours are on the GPU).
- **Upload failures** (the open item of the first drafts, resolved in fix round 2): `Upload` hands the texture to the
  tile the moment it exists, before any pixel data (`A/Scripts/WmvTerrainMaterials.cs:271-296`); an upload that throws
  after that is untracked and destroyed at once (`Untrack`, `A/Scripts/WmvTerrainBuilder.cs:424-439`, called in the
  upload's catch, `:717-724`). After an upload failure the failures are settled, the chunks re-picked, and every texture
  no remaining textured chunk binds is untracked, destroyed and reported with no upload time (`:740-777`): the tile's
  textures equal what its materials bind (self-test: 10 bound = 10 on the tile = 10 tracked = 10 live, 11,336 GPU bytes
  each way; the texture released "available" with uploadMs -1). The alpha array and the carrier are built only when a
  textured chunk remains.
- **Dumps**: every dump -- the tile composite, the chunk renders and the texel dump -- is streamed in bands of rows
  (`StreamTexels`, `A/Scripts/WmvTerrainComposite.cs:500`; `WriteTexelDump`, `:578`), so memory stays bounded at any
  size; the round-2 texel dump held a whole mip (a 2048 x 2048 texture cost about 144 MiB of managed memory); its raw
  bytes are unchanged by the banding (the self-test checks it).

| Event | What is released |
|---|---|
| Another tile replaces the current one | the commit disposes every other tile in the same block that makes the new one current |
| Terrain cleared | `clearTerrain`: a load in flight answered "cleared", its pending requests retired; a shown tile disposed |
| Models or Buildings take over | `loadWoWModel` supersedes the terrain load and takes the tile away |
| The active client changes | the host sends `clearTerrain` on the reset, replaces its terrain request and empties its read tracker; requests of the old epoch are answered stale |
| The Unity renderer restarts | the player's `OnDestroy` disposes the session and its tiles; the host drops its queue and held requests with the connection; the new player starts at 0 pending and 0 retired |
| A texture request fails | the texture is never uploaded; its chunks become MissingTexture or HeightTextureMissing (a LayerEffectStatic chunk keeps its state and draws the checker); its prepared data are dropped (`Fail`) |
| An upload fails | the texture is untracked and destroyed; textures no remaining chunk binds are released (above) |
| A load is superseded or times out | `Fail` releases every prepared texture; the job is dropped; worker results for an ended load are discarded; its report settles the answered failures and names the unanswered ones |

- **Caching**: one texture per FileDataID per tile, whatever its roles (a diffuse and a height use of one id is one
  upload; never observed; a height role whose file has no alpha channel fails alone): fetched once, prepared once,
  uploaded once (`rt.Textures`, `A/Scripts/WmvTerrainBuilder.cs:342`). Retail 32_50's drawable chunks make 949 layer
  references to 10 textures; Retail 2923 32_24's 1,303 diffuse and 1,303 height references to 16 + 16. There is **no
  cross-tile cache**: a replacing tile fetches its textures again. Map files and textures never enter the model asset
  cache (routing by `map<n>` ids). A texture of a layer with unresolved repeats is neither listed nor fetched.
- **Bounds**: K = 4 pending per load, 128 retired; host queue 16, records 256, poll timings 256; 128 texture ids and
  128 MiB of texture bytes read per load; report lists 4,096 and 128; the harness's per-load tables bounded by the loads
  it actually sent. Prepared data per load are bounded by the host's per-load byte budget on the bytes read; the RGBA32
  fallback (palettised, BGRA, non-block-sized files) can hold several times that
  (`A/Scripts/WmvTerrainSession.cs:52-56`).
- Measurements: sections 16 and 18.

## 12. Unresolved semantics and the evidence that would resolve them

| Item | Known | Evidence that would resolve it |
|---|---|---|
| 4-bit draw-time expansion (U19) | the offline converter's table (16n + 15 for n >= 4; 30, 46, 62 for n = 1..3) and the authoring data; external references say 17n | an unlit game image of 4-bit data (none exists in any client), or a controlled in-game capture: Classic Era, Azeroth 32_50, a flat patch where one overlay runs through nibbles 0..15 next to a fully covered area with two strongly different textures (e.g. 187127 over 187115), camera near top-down at full resolution, UI, shadows, clutter and fog off, position and facing recorded; compare ratios between neighbouring regions so the light cancels; the same capture in Retail calibrates |
| The client's draw-time rule against the baked composite | the height mix is PROVEN for the baked map texture only | a same-camera in-game capture at a verified location (coordinate mapping, orientation, field of view established, lighting cancelled by ratios) of Retail 32_50 and 30_49, compared per region with the composite |
| Nibble 4 and chunks of 6-8 layers at draw time | PROVEN for the baked composite (5 tiles of map 2923); INFERRED at draw time | the same kind of capture on Retail map 2923 32_24 (nibble-4 chunks at odd ix / iy, the 8-layer chunks 145 and 209) |
| Nibble 4 on other maps | seen only on map 2923 (5 of 1,801 tex0 files) | a map texture of another map with nibble 4, through the same model comparison |
| MTXP scale nibbles 5..15 | never observed; LayerEffectUnresolved | a file that has one, and its map texture (the rule 8 / 2^n would predict 0.25, 0.125 ... repeats) |
| Tile- against map-continuous UV | indistinguishable below nibble 8 (the period divides a tile) | a nibble of 8 or more with its map textures on two neighbouring tiles |
| How the height texture is sampled (its mip) | its repeats, phase and channel are gated (alternatives worse); its LOD is INFERRED (the layer's own) | a height texture whose size differs from its diffuse partner's, compared at several mips against the map texture |
| MCCV divisor (127, 128, 2/255) | not separable; restricted tests disagree (34_46 /128, Beta 32_48 /127) | a chunk with strongly coloured MCCV against a high-precision reference (an in-game capture or the client's own constant) |
| Alpha texel centre in the client | PROVEN for the map generator; the i/63 layout is the alternative | an in-game close-up across a chunk edge where row 63 differs from row 62 (923 raw Retail maps) |
| Lava animation and overbright (MCLY 0x7, 0x38, 0x40, 0x80) | bit layout fits every value; the baked texture shows the layers static | timed in-game captures of Burning Steppes lava (Retail 34_45 MCNK 3, 34_46) for direction, speed units and the brightness factor |
| Cube maps (MCLY 0x400, MTXP 0x1) | MoP only, two base layers (31_48 MCNK 66 and 83, texture 303951) | an in-game capture of those chunks (the MoP storage opens again since late 2026-10-13) |
| MCSH darkening | bit layout PROVEN; not visible in the Era minimap; references disagree (0.7 on alpha against 1/3 on colour) | an Era capture of shadowed against unshadowed flat ground with the sun direction known |
| The diffuse alpha channel (specular, INFERRED) | data, mean 1-65 of 255 | the lighting stage's comparison |
| The S == 0 fallback of the height mix | the contract's; never reached by real data (smallest offset 0.1) | a real chunk where every p_k = 0 |
| The small positive bias of every fit | +0.24 to +0.53/255 per channel on the V2 dumps (+0.3 to +0.67 on the reference's golden fits, final review); shared by every rule and every tile, printed on every V2 run; cause not established (truncating the oracle would be choosing a convention because it fits) | the map generator's own rounding convention |
| The 0.125/255 V2 residual | one full float16 step, where a round-to-nearest store gives 0.062; INFERRED to be the GPU's store or arithmetic | a dump into a float32 target |
| This GPU's DXT5 alpha weights | measured, exact on every real input; another GPU may differ | texel dumps from a second GPU (the check would fail loudly with "neither") |
| DXT1 four-colour interpolants and three-colour midpoints on real data | no real DXT1 terrain texture has an interpolant | a real DXT1 terrain texture with interpolated texels |
| MoP Classic | corpus only during the stage; the storage opens again since late 2026-10-13; end to end on 0 32_48 and 31_48 (states, V1, texels) PASS | a MoP map that names map textures (75 WDTs do) read against them, for a V2 oracle on MoP |
| MAMP non-zero, MTXF, MCLY bits above 0x400, a chunk without 0x8000, more than 8 layers | never observed | a file that has them (each is an explicit state or Malformed meanwhile) |

## 13. Future streaming implications

All measurements in this stage are of one tile at a time; nothing here shows the design is ready for streaming.

- **Per tile today**: one alpha array (4,194,304 B for a full tile of chunks with at most 5 layers; 6,078,464 B for
  Retail 2923 32_24, whose 115 chunks of 6-8 layers take a second slice), one sampler carrier (84 B), up to 256
  materials and 256 renderers, and the tile's own textures: no texture is shared between tiles, and a replacing tile
  fetches and uploads its textures again.
- **What a multi-tile cache would need**: neighbouring tiles share most textures (unique MDID ids in a 3 x 3 block of
  Retail tiles: median 32, max 51, against up to 23 per tile in the window), so a shared cache pays, but the same
  FileDataID can be different bytes in different clients (5 of 71 shared window textures differ between Retail and
  Era): key by client epoch and FileDataID (or by content key), count references per tile, budget it, and empty it on an
  epoch change.
- **Serving**: the host serves on the GUI thread; a 5.6 MB texture alone costs 28-35 ms there (median per handler call,
  section 17). Under the round-3 rules the heaviest tile, Retail 2923 32_24, fetches all 32 of its textures --
  90,914,944 B, twelve of them 5,593,604 B -- where integration round 2 fetched 22 (43,367,512 B, four of 5,593,604 B)
  for its then 62 textured chunks; still within the 128-texture and 128 MiB per-load budgets, but its texture fetch
  takes 1,080 ms of a 1,247-ms cold load and all twelve large textures exceed the 20 ms serving budget. Multi-tile
  volumes need the audit's storage-owning serving thread (W3) before any multi-tile view.
- **Seams**: alpha is clamped per chunk; textures restart per chunk with whole repeats (nibbles 0..3) or continue across
  chunks and tiles with the per-chunk phase (nibble 4), so a tile edge needs no neighbour data to texture; tile-edge
  alpha steps are authored (47.42 at 32_50 / 33_50). A nibble of 8 or more (never observed) would first need
  tile- against map-continuous UV settled (section 12); under map-continuous UV its phase would depend on the tile's
  position in the map. Stage 03's two-tile geometry and edge-normal requirements are unchanged.
- **Draws and batching**: one draw per chunk; the materials meet the SRP Batcher's conditions, but batching was not
  measured, and per-tile material counts multiply with the number of tiles shown.
- **Views that draw no textures still fetch them**: the session does not consult the view (a shaded view of 2923 32_24
  fetched its 43.4 MB in integration round 2); recorded, not changed.

## 14. Visual comparisons

Every picture below is a full-resolution WMV viewport capture (`WMV_VIEWPORT_SHOT`, hidden desktop, 1600 x 1440 or
1024 x 1024), a native-pixel crop of one, or a figure assembled from such captures and from the player's float dumps;
nothing is enlarged. Figures 1-17 come from integration round 2, figures 18-21 from the round-3 verification (section
0's verification binaries). Integration round 2: player `Assembly-CSharp.dll` MD5
`8D93FF2E8E676CD29E3F590A66EFF9E1` (protocol 13) unless a caption says otherwise; host `wowmodelviewer.exe` SHA-256
`6a3655a35b38...`. On the fix-round-2 player (`52C4B107...`) the four capture runs that had failed only the harness's
view check (32_50 in the materials view at two cameras, the chunks view of figure 4, 30_49 in the materials view) gave
byte-identical shots, and the V2 dumps behind figures 16 and 17 byte-identical raw files. On the verification binaries
all 8 golden V2 dumps (figures 16 and 17 among them), the 30_49 V1 composites and texel files and the Era 169 25_31
composites are byte-identical to integration round 2's again, as are the WMO and the pinned M2 captures (figures 14 and
15); the other round-2 captures were not retaken. On the final binaries every dump is byte-identical to round 3's again,
and the WMO and pinned M2 captures too. The textured views show the authored
composite only: no lighting, shadow map, water, doodads or buildings; layer animation and overbright are not applied.
The viewport's frame decode, the existing bloom and the LUT are on, as for every model.

**No in-game reference capture at a verified corresponding location exists**, so none of these pictures is compared with
the running game, and none is a claim of Retail or Classic visual parity. The comparison with the game that is made is
numerical and against the game's own baked map texture (section 15.1, figures 16 and 17).

1. **Stage 03 neutral terrain before, and the first textured tile** (Retail 0 32_50, default framing, no view switch:
   the material is `textured`, so the view resolves to `textured`). The silhouettes match pixel for pixel (0 pixels
   where only one image shows background). The left half is `03-terrain-geometry/01-shaded-tile-retail-32_50.png`.
   ![](04-terrain-materials/01-before-after-retail-32_50.png)
2. **Close-up of blending**: MCNK 152 (4 layers: grass 187127, rock 187138, dirt 187115, grass 187125, all RLE), orbit
   0:60:0.6, a native 1200 x 960 crop. Rock patches ringed by dirt fading into grass; the height mix's cutoff gives the
   sharp-edged transitions.
   ![](04-terrain-materials/02-blend-closeup-retail-32_50-mcnk152.png)
3. **Multiple texture layers**: the same chunk decomposed by the reference (rows: each layer's texture at 8 repeats; its
   plain weight, DIAGNOSTIC; its height-mix weight, the rule drawn; its contribution). On this 0x80 tile without height
   textures (MHID 0, H = 1) the cutoff is the only difference between the two weight rows.
   ![](04-terrain-materials/03-layers-retail-32_50-mcnk152.png)
4. **Chunk boundaries**: textured view around MCNK 87 (orbit 0:80:1.2), and the stage 03 `chunks` view from the same
   camera to locate the edges (centre 87, east 88 with 3 layers and no 187115, south 103 with 187138 instead of 186770).
   No seam is visible; numerically the 87|88 and 87|103 edges differ by 6.88 and 6.68/255 (mean abs between the two
   columns) against 7.60-10.42 between adjacent columns inside those chunks (section 15.4). The rock patch starting at
   103's north edge is authored: 87 has no rock layer, and the game's map texture shows the same.
   ![](04-terrain-materials/04-chunk-boundary-retail-32_50-mcnk87.png)
5. **Tile-edge diagnostic**: columns 448-511 of the 32_50 composite next to columns 0-63 of 33_50 (player 512-px dumps),
   the same strips from the game's map textures, and abs(player - map) x 8. Statistics in section 15.4.
   ![](04-terrain-materials/05-tile-edge-retail-32_50-33_50.png)
6. **Classic terrain**: Classic Beta (1.60.1.70338) 0 32_48 textured (the hatched field texture is in the game's map
   texture too); Classic Era (1.15.9.70003) map 2853 34_50, an 8-bit Era map, textured; Classic Era Azeroth 32_50 in the
   materials view, every chunk orange = AlphaUnsupported (4-bit alpha). In its default view that tile is drawn in the
   stage 03 neutral shaded view, a capture byte-identical (MD5 `b17e32b9...`) to the stage 03 picture in figure 1.
   ![](04-terrain-materials/06-classic-beta-32_48.png)
   ![](04-terrain-materials/07-classic-era-2853-34_50.png)
   ![](04-terrain-materials/08-classic-era-azeroth-32_50-materials.png)
7. **Retail terrain**: 0 34_46 textured (lava rivers; the tall sheets are the tile's own geometry, as in stage 03), the
   same tile in the materials view (green Textured, cyan the 11 LayerEffectStatic lava chunks, drawn static), and 0
   30_49 with its height textures 971415 and 878974.
   ![](04-terrain-materials/09-retail-34_46-lava-textured.png)
   ![](04-terrain-materials/10-retail-34_46-materials.png)
   ![](04-terrain-materials/11-retail-30_49-height-textures.png)
8. **Height textures, close-up**: 30_49 MCNK 68 (layers 971416 with height 971415 at MTXP (5, 1), and 878975 with height
   878974 at (4, 0.5)), orbit 0:60:0.6, native 1000 x 900 crops of the labelled figure: the composite, then the
   `texturedplainweights` DIAGNOSTIC (plain weights, no heights, under a hatch). The height mix gives crisp,
   height-driven borders between foliage, grass and the cobble layer; plain weights smear them. Plain weights fit the
   game's map texture far worse on these chunks (7.405 against 1.203, section 5.3).
   ![](04-terrain-materials/12a-height-mix-closeup-retail-30_49-mcnk68.png)
   ![](04-terrain-materials/12b-plain-weights-diagnostic-retail-30_49-mcnk68.png)
9. **Missing-texture diagnostic**: Retail map 169 24_27, where 295 of the tile's layers name MDID entry 0: every chunk
   MissingTexture, the missing layers drawn as the magenta / black checker weighted like the texture they stand for; the
   one real texture (249604) shows green where its layer weighs. At this distance the checker's 1/8-cell squares alias
   into a moire (a presentation property of the diagnostic pattern, section 6.6).
   ![](04-terrain-materials/13-missing-texture-169-24_27.png)
10. **Existing M2 and WMO regression**: `-mo creature/bear/bear.m2 -unityipctest` with `-wmvAnimTime=0` (byte-identical,
    MD5 `2627227a13a67ff34e16c7e075b1a36d`, to the same capture on the stage 03 player, and again on the fix-round-2
    player), and `-wmo 108631 -unityipctest` (byte-identical, MD5 `d3db7749...`, to
    `03-terrain-geometry/12-regression-building-stockade.png`), both again byte-identical on the fix-round-2 player and
    on the verification binaries.
   ![](04-terrain-materials/14-regression-model-bear.png)
   ![](04-terrain-materials/15-regression-building-108631.png)
11. **Against the game's map texture**: a top-down orthographic capture of 32_50 (1024 x 1024, box-averaged 2 x 2 to 512
    px, a reduction), the game's map texture 1293881 (MAID word 5, mip 0) and the player's 512-px V2 composite dump, all
    at 32 px per chunk. The capture is a screen render (trilinear and anisotropic filtering at 64 px per chunk, bloom,
    LUT), so it sits 1.78/255 (4 x 4-block RMSE) from the map texture; the dump, the oracle comparison, sits 1.071.
    Then, per golden tile, the V2 dump, the map texture and abs(difference) x 8; 30_49 shown (height textures): the
    difference is dark everywhere except texture-detail edges.
   ![](04-terrain-materials/16-topdown-vs-map-texture-retail-32_50.png)
   ![](04-terrain-materials/17-v2-dump-vs-map-texture-retail-30_49.png)
12. **The heavy tile: MTXP nibble 4 and chunks of 6-8 layers** (round-3 verification, Retail map 2923 32_24; every
    capture run harness PASS 108/0/0). First the tile textured, default framing: all 256 chunks drawn textured (the
    status line: "256 chunk(s) drawn textured"); under the rules of integration round 2, 194 of them were neutral. Then
    the materials view: one state colour, Textured, over all 256 chunks (round 2's materials view of this tile was
    mostly red Malformed with 6 yellow LayerCountUnsupported chunks). Then a native 600 x 520 crop at the boundary of
    MCNK 20 and 21 (ix 4 | 5, iy 1; orbit 0:80:1.2), where the nibble-4 layers weigh about 0.6 on both sides: the
    nibble-4 rock and void textures run on across the chunk edge at 0.5 repeats, with no seam (a chunk-local UV would
    restart them at the edge). Last, a top-down capture box-averaged 2 x 2 to 512 px (a reduction), the game's map
    texture 7473747 and the player's V2 dump: the capture sits 1.41/255 (4 x 4-block RMSE) from the map texture, being a
    screen render (filtering, bloom, LUT), the dump 1.034 (section 15.1). A difference figure of the V2 dump against the
    map texture (not reproduced here) is near black, with no chunk-grid or 2-chunk pattern.
   ![](04-terrain-materials/18-heavy-tile-2923-32_24-textured.png)
   ![](04-terrain-materials/19-heavy-tile-2923-32_24-materials.png)
   ![](04-terrain-materials/20-nibble4-chunk-boundary-2923-32_24-mcnk20-21.png)
   ![](04-terrain-materials/21-topdown-vs-map-texture-2923-32_24.png)

## 15. Numerical validation results

Integration round 2, 2026-10-13 12:24-12:53, on the binaries of the first fix round (player `8D93FF2E...`, host
`6a3655a3...`), every input verified against its pin or manifest and the pinned client build by the compare tool. The
dumps of that round were checked again with the fix-round-2 and round-3 compare tools (the mandatory V2 per-pixel gate,
the named-decode texel rule); the fix-round-2 verification re-ran the player (`52C4B107...`) on 4 golden tiles and 4 V1
chunks, its raw dumps byte-identical to integration round 2's where the runs overlap. The round-3 verification
(verification binaries, section 0; runtime `run_e2e`) re-ran V2 on all 9 golden tiles -- the 8 earlier dumps
byte-identical to integration round 2's -- and V1 and texels on Retail 2923 32_24 and on 30_49 (byte-identical again).
The final binaries reproduced all of it: the 9 V2 dumps (raw and PNG), the 2923 V1 chunks and their 32 texel files and
the 30_49 V1 chunks and texel files byte-identical to round 3's (61 files), every compare result text-identical.

### 15.1 V2: the production composite against the game's map texture

Command per tile: `-wmvTerrainView=textured -wmvTerrainCompositeDump=<name>`, then `compare_terrain_material.py v2
<name>.json --corpus <audit data>`. Every tile: harness PASS (Retail 108/0/0; Beta and Era 107/0/1, the skip being "a
map without a WDT is refused -- every map of this client has a WDT reference"), compare `RESULT: PASS` (522 checks with
the integration-round-2 tool; 523 with the per-pixel gate of the later tools, and 523, 0 failed, for each of the 9 tiles
on the verification and the final binaries at the 0.26/255 default), chunk states and layers equal to the reference on
all 256 chunks, every kept pixel covered.

| Tile | Block RMSE <= threshold (blocks) | Bias R G B | Golden design / plain | Next symmetry | Phase 0 vs best +-1 px | Dump vs exact-decode reference: block RMSE / per-pixel max | Per-pixel max against the hardware model (gate 0.26) | Clipped ref / dump |
|---|---|---|---|---|---|---|---|---|
| Retail 0 32_50 | 1.071 <= 1.360 (16,384) | +0.45 +0.45 +0.32 | 1.110 / 1.744 | 24.374 | 3.285 vs 9.890 | 0.132 / 1.060 | 0.1246 | 0 / 0 |
| Retail 0 29_50 | 1.060 <= 1.319 (16,360) | +0.46 +0.47 +0.36 | 1.069 / 2.021 | 24.899 | 3.967 vs 12.398 | 0.113 / 1.680 | 0.1251 | 0 / 0 |
| Retail 0 32_48 | 1.074 <= 1.346 (16,368) | +0.53 +0.38 +0.30 | 1.096 / 2.058 | 40.327 | 3.153 vs 9.560 | 0.115 / 1.442 | 0.1249 | 0 / 0 |
| Retail 0 34_46 (11 static lava chunks) | 1.051 <= 1.329 (16,128) | +0.34 +0.44 +0.40 | 1.079 / 2.570 | 39.162 | 3.138 vs 9.393 | 0.132 / 1.188 | 0.1259 | 4,093 (hardware model 4,102) / 4,092 |
| Retail 0 30_49 (height textures) | 1.117 <= 1.408 (16,380) | +0.52 +0.45 +0.24 | 1.158 / 2.928 | 36.960 | 3.489 vs 9.795 | 0.156 / 1.096 | 0.1252 | 0 / 0 |
| Classic Beta 0 32_48 | 1.120 <= 1.395 (16,368) | +0.45 +0.42 +0.34 | 1.145 / 1.920 | 35.276 | 3.368 vs 10.672 | 0.113 / **2.011** | 0.1250 | 0 / 0 |
| Classic Era 2853 34_50 | 1.067 <= 1.357 (16,384) | +0.49 +0.45 +0.30 | 1.107 / 1.834 | 23.146 | 3.357 vs 9.699 | 0.133 / 1.060 | 0.1246 | 0 / 0 |
| Retail 1917 27_19 (MPHD 0x80 clear) | 0.929 <= 1.228 (16,217) | +0.42 +0.41 +0.38 | 0.978 / 3.027 | 22.688 | 1.986 vs 5.686 | 0.185 / 1.392 | 0.1246 | 0 / 0 |
| Retail 2923 32_24 (nibble 4, 6-8 layers; verification binaries) | 1.034 <= 1.307 | +0.41 +0.50 +0.46 | 1.057 / 4.341 | 23.969 (transpose) | 2.357 vs 6.415 | 0.141 / 2.206 | 0.1235 | not reported |

- The gate names the hardware model on every tile; the dump's block RMSE against the hardware-model reference is
  0.025-0.045 (32_50 0.028, 29_50 0.045, 32_48 0.034, 34_46 0.025, 30_49 0.041, Beta 32_48 0.034, Era 2853 0.028, 1917
  27_19 0.027, 2923 32_24 0.025). Under the exact decode alone Beta 32_48 (2.011) and 2923 32_24 (2.206) would fail the
  0.26 gate, and under the rounded decode most tiles (up to 2.482): the exact decode is not the player's input (section
  10.3). On 34_46 the reference clips 4,093 / 4,102 / 4,097 pixels (exact / hardware / rounded), the dump 4,092.
- Retail 2923 32_24 (harness 108/0/0, checker 4/0/1): chunk states equal the reference (Textured 256), and every chunk's
  (texture, repeats) list too, 0.5 at nibble 4. Subsets of the same dump: the 188 nibble-4 chunks 1.043 (reference
  design 1.063, chunk-local alternative 4.009), the 115 chunks of 6-8 layers 1.059 (reference 1.079, chunk-local 3.678),
  the 68 without nibble 4 1.008; the nibble-4 chunks by parity class (ix % 2, iy % 2): (0, 0) 1.058 (47 chunks), (0, 1)
  1.043 (48), (1, 0) 1.039 (48), (1, 1) 1.032 (45) -- no loss on the classes with a non-zero phase.
- Subsets: 34_46's lava chunks alone 1.147 (golden 1.165, plain weights 4.597); 30_49's height-textured chunks alone
  1.191 (golden 1.203, plain weights 7.405). On 1917 27_19 the player logs that MHID's 10 height textures are ignored
  (MPHD 0x34E has no 0x80) and requests none.
- The player sits slightly closer to the map texture than the reference (1.071 against 1.110 on 32_50): the dump's
  block-compressed texels are the GPU's decode, the reference's the exact one (section 10.4). The bias (about +0.5/255,
  shared by every rule and every tile, cause not established) is printed on every run.
- Not a golden tile, for the tile edge: Retail 0 33_50, block 1.005 (design 1.036, plain 1.618), `RESULT: PASS (521
  checks)` against a threshold given by hand (1.36), with the textures given explicitly (`--corpus` could not verify
  texture 187112 then: the `census_blp` manifest names its id column `fdid`; the reference's audit-data reader has since
  learned both column names and tries every copy of a file).

### 15.2 V1: the GPU composite against the CPU composite, per pixel

`-wmvTerrainValidate -wmvTerrainCompositeChunk=<n>` (validation sampling, N = 2,048 for every chunk here but two 2923
chunks at 1,024) and the texel dump of the same run; `compare_terrain_material.py v1 <chunk dumps> --texels <texel dump>
--corpus ...`. The reference's states and layers were checked against each chunk first. Rows 1-12 are integration round
2 (player `8D93FF2E...`); the two 30_49 chunks with a height texture at layer index 3 were added after the final review
and run on the fix-round-2 player (`52C4B107...`), whose 52 and 68 composites and 13 texel raw files are byte-identical
to integration round 2's. Every earlier dump passes again under the fix-round-2 and round-3 tools (per-layer production
check, both axes), with identical maxima. On the final binaries the 2923 and 30_49 chunks are byte-identical to round
3's and pass with the same text (112 and 45 checks, 0 failed).

| Tile, MCNK | Layers, state | Max abs(dump - reference) R / G / B (/255) | clippedPixels | Result |
|---|---|---|---|---|
| Retail 32_50, 87 | Textured, 187127 + 186770, 187115, 187125 (RLE) | 0.1245 / 0.1245 / 0.1245 | 0 | PASS |
| Retail 32_50, 246 | Textured, 187088 + 187082 (raw 8-bit) | 0.1342 / 0.1391 / 0.1245 | 0 | PASS (40/0 with 87) |
| Retail 32_50, 152 | Textured, 4 layers (the close-up) | 0.1245 / 0.1245 / 0.1245 | 0 | PASS (21/0) |
| Retail 34_46, 48 | Textured, 186934 + 186951 RLE, 186947 raw, 252273 RLE; MCCV | 0.2491 / 0.2437 / 0.1124 | 1,352,568 (unclamped MCCV overshoot; reference 1,349,303..1,362,393; own count exact) | PASS |
| Retail 34_46, 146 | Textured, 3 layers incl. 188903; holes 0x303; MCCV | 0.1245 / 0.1245 / 0.1245 | 0; 262,144 hole pixels, none drawn | PASS |
| Retail 34_46, 205 | LayerEffectStatic (186949, MCLY 0x3D2) | 0.1245 / 0.1245 / 0.1245 | 0 | PASS (62/0 with 48, 146) |
| Retail 32_48, 126 | Textured, 4 layers; MCCV | 0.1245 / 0.1245 / 0.1245 | 0 | PASS (21/0) |
| Classic Beta 31_47, 48 | Textured, 5 layers; MCCV | 0.1245 / 0.1245 / 0.0622 | 0 | PASS (22/0) |
| Classic Era 2853 34_50, 44 | Textured, 4 layers | 0.1245 / 0.1245 / 0.1239 | 0 | PASS (21/0) |
| Retail 30_49, 52 | Textured; 971416 at 2 repeats with height 971415 (5, 1); MCCV | 0.1245 / 0.1245 / 0.0597 | 0 | PASS |
| Retail 30_49, 68 | Textured; 971416 / 971415 (5, 1), 878975 / 878974 (4, 0.5); MCCV | 0.2490 / 0.2490 / 0.0601 | 4,147 (reference 4,117..4,251; own count exact) | PASS (45/0 with 52) |
| Production sampling: Retail 34_46, 132 (saturated, every texture at one texel per pixel, 2/255) | Textured, 2 layers; MCCV | 0.3171 / 0.1356 / 0.1326 | 10,985 (reference 9,814..12,618) | PASS (20/0) |
| Retail 30_49, 51 (fix round 2) | Textured; 971416 with height 971415 at layer index 3 | 0.1245 / 0.1245 / 0.1245 | 0 | PASS |
| Retail 30_49, 84 (fix round 2) | Textured; 878975 with height 878974 (4, 0.5) at layer index 3 | 0.2485 / 0.2490 / 0.1244 | 8,070 (reference 8,033..8,256) | PASS (90/0 with 52, 68) |
| Retail 2923 32_24, 9 (ix 9, iy 0; verification binaries; N 1,024) | Textured, 3 layers: 5876854 at 1 repeat, 5900005 and 5900000 at 0.5 repeats, phase (0.5, 0) | 0.0623 / 0.0623 / 0.1242 | 0 | PASS |
| Retail 2923 32_24, 25 (ix 9, iy 1; N 1,024) | Textured, 4 layers, two of them nibble 4 at phase (0.5, 0.5) | 0.0623 / 0.0623 / 0.1239 | 0 | PASS |
| Retail 2923 32_24, 80 (ix 0, iy 5; N 2,048) | Textured, 7 layers; nibble 4 on layer 4 (first slice, A) and layer 6 (second slice), phase (0, 0.5) | 0.0623 / 0.0623 / 0.1245 | 0 | PASS |
| Retail 2923 32_24, 145 (ix 1, iy 9; N 2,048) | Textured, 8 layers; nibble 4 on layers 5 and 7 (second slice), phase (0.5, 0.5) | 0.1245 / 0.1243 / 0.1244 | 0 | PASS (112/0 with 9, 25, 80) |

Every chunk: every non-hole pixel covered, every hole pixel cleared, the inputs' build and MD5s (the height textures'
included, with role `height`) equal to the verified audit files. The 2923 chunks were picked from the reference (N is
1,024 on 10 of the tile's chunks, 2,048 on 246) to cover every phase class, both alpha slices and 7 and 8 layers. A
negative control, the shipped compare with only the reference's chunk phase mutated and the dump untouched: phase (0, 0)
fails all four chunks (62.9 / 55.2 / 54.0 / 46.3/255 at most); u and v swapped fails 9 and 80 (59.2, 53.2) and passes
the odd/odd 25 and 145, as it must -- the chunk set discriminates both axes and the player's dump carries the phase. On
the verification binaries 30_49's chunks 52 and 68 pass again (45 checks, 0 failed) with byte-identical composites. The
largest difference, 0.249/255, is the half-float target's rounding at values near 1 (the 0.5/255 rationale, section
10.2).

### 15.3 Texels: the GPU's decode of every uploaded texture

Every real texel dump passes the named-decode rule of section 10.4, the interpolated texels all naming the **hardware
model**: the 8 dumps of integration round 2 with the round-3 tool 41, 41, 38, 38, 44, 49, 41 and 57 checks, 0 failed
(9 with a second 32_50 dump; the 30_49 dump of the fix-round-2 verification 57 checks, 0 failed); 4,193,774 four-colour
interpolant texels over the 9 integration dumps (DXT3 2,690,652 in 75 texture entries, DXT5 1,503,122 in 15) and
1,454,790 interpolated DXT5 alphas, all equal to the hardware model. An independent checker's walker over 18 real dumps
(190 entries) found 0 off among 10,555,902 colour and 5,059,116 alpha interpolants, each DXT5 weight the only one of
0..2048 that fits. The reviewer's texel mutants now fail: snapped, swapped and mid interpolants and reversed alpha
selectors (they passed the round-2 rule), and, as before, flipped rows, flipped block rows, swapped red and blue and an
sRGB view. A rounded RGBA32 fallback dump of real textures (187125, 878974, 971415) passes named "rounded" (it failed
before the third named decode existed). On the verification binaries Retail 2923 32_24's 32 textures (16 diffuse, 16
height; 2048 x 2048, 1024 x 1024 and 512 x 512 DXT5, all uploaded as `RGBA_DXT5_UNorm`) pass, 161 checks, 0 failed, all
64 named checks naming the hardware model (53,204,170 colour and 32,119,076 DXT5-alpha interpolants); 30_49's texel dump
again 57 / 0, byte-identical. On the final binaries both texel dumps are byte-identical again (161 / 0 and 57 / 0).

What the GPU's decode is, measured on the integration-round-2 dumps (round-2 tally):

| Format | Textures | Endpoint and explicit values | Interpolated values (all equal to the hardware model) | Hardware deviation from the exact decode (max) |
|---|---|---|---|---|
| DXT1 | 1 (188903, 8 x 8) | 512, all exact | none | -- |
| DXT3 | 23 diffuse | 10,035,470, all exact | 7,003,890 | R 0.667, G 5.333, B 0.667 /255 |
| DXT5 diffuse | 11 | 3,631,503, all exact | 3,708,529 | G 5.333, A 6.400 /255 |
| DXT5 height | 971415, 878974 | 2,200,821, all exact | 2,255,627 | G 5.333, A 6.200 /255 |

NOT EXERCISED (no terrain data uses them, which is not a pass): DXT1 interpolated texels on real data (the DXT1 colour
model is confirmed on the self-test's synthetic DXT1 only), BGRA and palettised uploads, and a real RGBA32 fallback
upload (this GPU samples DXT1/3/5 and the census found no terrain texture that is not whole blocks; the fallback is
covered by the self-test's 6 x 6 DXT5 and the crafted dump above).

### 15.4 Tile and chunk edges

- Tile edge, column 511 of 32_50 against column 0 of 33_50 (512 rows, all covered): player mean abs 6.75, RMS 9.29, p95
  18.6, max 50.0 /255; the game's own map textures at the same edge 6.82, 9.63, 20, 49. Adjacent columns inside a tile:
  player 9.12 (32_50 cols 510|511) and 6.31 (33_50 cols 0|1), map 7.53 and 5.30. Player against map at the two edge
  columns: mean 2.67 and 2.45. The expectation: texture phase restarts at every chunk edge with whole repeats, and the
  alpha maps are per chunk and clamped, so the only step at a tile edge is what the authored edge texels hold; measured:
  no step beyond the in-tile baseline and no systematic offset, with the same statistics in the game's map textures.
- Chunk edges 87|88 and 87|103 (32_50): player 6.88 and 6.68 against 7.60-10.42 inside those chunks; map 5.91 and 6.66.
- Nibble 4 across chunk edges (round 3): the C# suite checks continuity across chunk edges at 0.5 repeats (the
  phase), the self-test's GPU composite of nibble-4 chunks at odd ix and iy equals the CPU twin within 0.125/255, and
  removing the phase moves them by 142.976-222.662/255 (section 5.4). On the real tile the native crop at the MCNK
  20 | 21 boundary shows the nibble-4 textures running on across the edge (figure 20); the V2 dump fits as well in every
  parity class (1.032-1.058, section 15.1), and V1 against a reference without the phase fails the four chunks of 15.2.
  No pixel statistic across 2923's chunk or tile edges was computed.

## 16. Failure tests

Integration round 2 part B, real protocol-13 player (`8D93FF2E...`) on runtime `run_hostfix`, hidden desktop, one run at
a time; every out.json through `check_terrain_run.py`: over all 15 runs **PASS 3,118 / FAIL 0 / SKIP 121** (the skips:
maps outside the corpus's map 0, and Beta/Era having a WDT for every map; none counted as a pass). The fix-round-2
verification re-ran part B's five sequences (lifecycle, late answers, Era not in storage, MDID 0, missing tex0) and the
compatibility runs on its player (`52C4B107...`): every harness and checker count equal to part B's, the Era composites
byte-identical; over its 18 terrain out.json files `check_terrain_run.py` gave PASS 1,062 / FAIL 0 / SKIP 10. On the
verification binaries part B's lifecycle (harness 170/0/0, checker 236/0/0), late-answer (302/0/0, 91/0/0) and Era
not-in-storage (116/0/1, 7/0/3; composites byte-identical) sequences and the three compatibility runs pass with every
count equal to part B's (the MDID-0 and missing-tex0 runs were not repeated); over the verification's 24 terrain
out.json files without its performance runs `check_terrain_run.py` gives PASS 1,064 / FAIL 0 / SKIP 20 (with them, the
12 serving-budget FAILs of section 17). On the final binaries all five of part B's sequences (the MDID-0 and
missing-tex0 runs included, their shots byte-identical to part B's), the three compatibility runs and the stage 03
sequences of section 19 pass with every count equal to the earlier rounds'; over the final verification's 26 terrain
out.json files `check_terrain_run.py` gives PASS 2,024 / FAIL 0 / SKIP 30 (the serving-budget groups below the 10
requests a verdict needs, section 17).

| Spec 19 case | Real case and run | Result |
|---|---|---|
| Missing texture ADT | Retail map 2736 32_29: tex0 5862455 "not installed locally" (re-checked first) | PASS: built, 256 neutral chunks, material `unavailable` quoting the host's state and detail; 0 textures, 0 alpha arrays |
| Texture file absent from the client | Classic Era map 169 tile 25_31 (T21): texture 249604 is "not in client storage" in Era and drawn by single-layer MCNK 255 (found by this round's census of every MDID and MHID id in all three local clients) | PASS: the host answers "not in client storage", the report entry quotes it, 31 chunks MissingTexture (checker), 225 AlphaUnsupported, material `partial`, equal to the reference with `--missing 249604`; chunks 255 and 240 rendered alone byte-identical 1-px checkers; the settle log (fix round 2): "1 drawable chunk(s) draw the missing-texture checker in their place (0 of them layerEffectStatic ...; 31 drawable chunk(s) draw the checker in all, id-0 layers included)" |
| Texture "not installed locally" on a drawn layer | none exists today in any local client | SKIP on real data; covered by the self-test ("not installed locally", host state quoted) and the stand-in player |
| No texture id (MDID 0) | Retail map 169 24_27 | PASS: `partial`, missingTexture 256, the "no texture id" entry, checker |
| Invalid BLP reference / wrong kind | host in-process real reads in every run: a root asked for as a tex0 ("second chunk RDHM ... not MAMP"), a tex0 asked for as a texture ("not a BLP2 ... REVM"), budget exhausted, a reported load | PASS (11 kind checks, 7 read checks per run) |
| Corrupt alpha, malformed layer count | synthetic only (no real case): runner state and precedence tests (a 9-layer chunk, alpha sum 256 at one texel, RLE truncated / crossing 4,096 / past MCAL, offsets inconsistent, 0x100 missing, MTXP entry counts, nibbles 5 and 15, height values outside the domain), Python `test_every_state_and_reason` and `test_layer_count_beyond_eight`, the player self-test's neutral draws | PASS (synthetic) |
| Unsupported alpha format | Classic Era Azeroth 32_50 (4-bit) | PASS: `unsupported`, alphaUnsupported 256, no texture requested (only wdt, rootAdt, tex0Adt served), drawn neutral |
| HeightTextureMissing end to end | no real case (every height id local, none without an alpha channel) and no harness hook refuses one FileDataID | UNSUPPORTED by the harness; covered by the reference (971415 missing -> 10 chunks), the C# corpus test, synthetic tests and the self-test (an upload failure, a height texture "not installed locally", a height texture without an alpha channel: its diffuse role uploaded, its height role "decode failed", 4 chunks HeightTextureMissing) |
| Client switch mid-load | `lateswitch` with a texture, a tex0 and a WDT held, Retail -> Beta -> Retail -> Era -> Retail, on 30_49 and 44_28 (5 + 3 height textures) | PASS: held load answered "cleared", 4 requests retired per texture hold, late answers "stale" under both epochs and dropped, read tracker emptied with the old client, no object of the old client left, the new client's tile built from first reads |
| Client switch with a height request outstanding | the hold hook holds by role only and the player asks for heights after every diffuse id; on every tile common to Retail and Beta/Era the heights were still queued at the switch | UNSUPPORTED by the harness (the queued heights were retired correctly) |
| Superseded texture requests | `latesupersede` with a texture and a tex0 held (30_49, 44_28) | PASS: superseded, 4 / 1 retired, late answers stale and dropped, the next load built with 13 textures; since fix round 2 the report of an ended load settles the answered failures and names the unanswered ones ("... 10 texture(s) unanswered ...") |
| Unity player restart | `reconnect` with a texture, a tex0 or nothing held | PASS: held request dropped with the connection, the host holds nothing, the new player starts with nothing pending, the tile is built again |
| Repeated successful loads | 122 textured 30_49 loads, 51 `unavailable` 2736 loads, 51 `partial` 169 loads | PASS: counts flat (tiles 1, meshes 256, materials 256, textures 13, 3,757,744 B, alpha arrays 1 on every 30_49 sample) |
| Repeated failed loads | 60 `failrepeat` (no tile) and 50 texture timeouts | PASS: each answered once, each timeout retiring 1, counters ending at 50, host records bounded (256) |
| Neutral geometry with the state identified | every failure above | PASS: 256 of 256 chunks built and the report naming the state and reason; a failed load leaves the tile on show unchanged |
| Protocol compatibility | stage 03 player with this host; this player with the protocol-12 host | PASS: geometry only, no texture role asked or offered; material `unavailable`, "the host serves no terrain textures (protocol 12)" |

Memory over the 334-load run: host private bytes +1.35 MB with no trend; player native memory 145.37 -> 145.92 MB from
load 121 to load 334 on the same tile.

Known limitation, unchanged: reports of loads that end before adoption still carry the settled material state (e.g.
"textured" with 0 materials) next to `status` "failed", "superseded" or "cleared"; a consumer must read `status` first.

## 17. Performance

Integration round 2 part C, 2026-10-13 13:09-13:41, alone on the machine (no game client running), player `8D93FF2E...`,
host `6a3655a3...` on runtime `run_hostfix`, hidden desktop, Ryzen 9 9950X3D, OS file cache warm. Every tile ran in
three separate processes with `repeat:4` and no dumps: load 1 of each process is "cold" (n = 3), loads 2-5 "warm"
(n = 12). Three groups per tile: **P** the stage 04 player in its production view, **S** the stage 03 player (protocol
12, geometry only) against the same host, **N** the stage 04 player with `-wmvTerrainView=shaded`. Medians in ms. The
Retail 2923 32_24 round-2 rows were measured under the rules of that round (62 chunks textured, 22 textures); round 3
makes all 256 chunks textured and the tile fetches 32 textures (section 13): its round-3 rows come from the round-3
verification (verification binaries, runtime `run_hostfix`, the same method: three processes, `repeat:4`, P group only;
harness PASS 123/0/0 each).

| Tile | P total cold / warm | S total cold / warm | Stage 04 adds (cold) | tex0 round trip | tex0 parse | texture fetch | prepare (sum) | texture upload | alpha build | material build | first frame |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Retail 0 32_50 | 168.3 / 136.6 | 68.4 / 50.2 | +99.9 | 34.8 | 12.1 | 45.9 | 6.0 | 1.0 | 4.1 | 15.2 | 5.5 |
| Retail 0 34_46 | 166.9 / 136.2 | 69.0 / 51.2 | +97.9 | 35.9 | 10.5 | 46.3 | 6.4 | 0.8 | 3.7 | 16.2 | 5.3 |
| Retail 0 30_49 (heights) | 217.2 / 185.6 | 72.6 / 50.7 | +144.6 | 20.4 | 12.6 | 106.4 | 12.0 | 1.5 | 3.8 | 14.9 | 6.5 |
| Classic Beta 0 32_48 | 152.8 / 122.1 | 72.0 / 52.4 | +80.8 | 34.7 | 11.3 | 32.7 | 5.3 | 0.8 | 3.6 | 16.1 | 5.8 |
| Classic Era 2853 34_50 | 166.5 / 137.3 | 69.7 / 51.0 | +96.8 | 36.1 | 12.1 | 46.3 | 6.8 | 0.9 | 4.1 | 15.3 | 5.3 |
| Retail 2923 32_24 (heavy; round-2 rules, 62 chunks textured) | 774.8 / 737.6 | 70.3 / 52.5 | +704.5 | 37.3 | 15.8 | 641.0 | 86.5 | 9.3 | 1.9 | 6.7 | 8.4 |
| Retail 2923 32_24 (round-3 rules, 256 chunks textured, 32 textures) | 1,247.1 / 1,219.7 | not rerun (round 2: 70.3 / 52.5; the geometry is unchanged) | about +1,177 (against round 2's S) | 22.5 | 15.6 | 1,080.4 | 179.5 | 19.3 | 5.2 | 21.7 | 25.4 |
| Retail 2175 37_32 (most textures) | 767.3 / 731.9 | 71.2 / 52.3 | +696.1 | 25.7 | 10.7 | 627.4 | 63.4 | 7.2 | 4.8 | 14.9 | 12.9 |

- Measured separately as the spec asks: texture ADT fetch (the tex0 round trip; host handler 6.3-12.4 ms of it, the rest
  the harness's poll cadence, as in stage 03), texture ADT parse and reference resolution (tex0 parse, which includes
  the layer, MHID and MTXP tables), BLP fetch (texture fetch: first texture asked to last answered and prepared), BLP
  decode / preparation (prepare: header check and, for DXT, no decode at all; per texture about 1.3 ms for a 256 x 256
  DXT3, 2.7-2.9 ms for 1,024 x 1,024, 10.8-11.8 ms for 2,048 x 2,048), texture upload, alpha decode and array build,
  material creation (worker build plus Unity materials), complete tile time (total) and the first frame.
- Spread of the cold totals (median / p95 / max): 32_50 168.3 / 169.5 / 169.6; 30_49 217.2 / 233.7 / 235.5; 2923 774.8 /
  778.2 / 778.6 (round-2 rules), 1,247.1 / 1,260.8 / 1,262.3 (round-3 rules; warm 1,219.7 / 1,233.7 / 1,233.9; its
  texture fetch cold 1,080.4 / 1,093.8 / 1,095.3). The S group reproduces stage 03's own measurement (32_50 cold 68.4
  against 69.99 ms).
- Final binaries, a sanity run of one process per tile (`repeat:4`; cold n = 1, warm n = 4): 2923 32_24 1,248.3 ms cold
  and 1,212.6 warm (texture fetch 1,064.3 / 1,058.3; round 3 1,247.1 / 1,219.7), 32_50 169.5 cold and 121.1 warm (part C
  168.3 / 136.6; the four warm loads 139.3, 121.5, 120.7 and 120.1): within run-to-run spread; sizes and counts
  identical to round 3's and part C's.
- **What stage 04 adds on a normal tile: about 100 ms cold** (tex0 round trip 35, parse 10-13, material build 15,
  texture fetch 33-46, about 10 ms more upload). On the two heavy tiles texture fetch dominates (about 630-640 ms): in
  integration round 2, 2923 32_24 asked for 22 textures, four of them 2,048 x 2,048 DXT5 of 5,593,604 B; 2175 37_32 for
  58. Under the round-3 rules 2923 32_24 asks for 32 (90,914,944 B, twelve of 5,593,604 B): texture fetch 1,080 ms
  cold (1,067 warm) of a 1,247-ms cold load; preparing the 32 textures 179.5 ms in sum (11.1-12.0 ms for each 2048 x
  2048); uploading them 19.3 ms; the first frame 25.4 ms cold, 33.2 warm.
- The N group (shaded view) still fetches the tex0 and every texture (its totals are within 5-12% of P): the session
  does not consult the view, which applies only when the tile is built. A diagnostic-view cost, recorded, not changed.
  In integration round 2 the harness's texture-count check failed the N runs on fully textured tiles (it ignored the
  view: the 15 N runs and one shaded-view dump run); the check is view-aware since then (section 9), and the four
  part-A capture runs that had failed the same way pass on the fix-round-2 binaries; the N group itself was not rerun.

**Host serving per role** (P group, all records of the 3 runs; ms median / p95 / max; "later" = not the first read of
that file in its process; first reads match):

| Tile | WDT handler | root handler | tex0 handler | texture handler | GUI time per serving poll |
|---|---|---|---|---|---|
| Retail 32_50 | 1.11 / 1.24 / 1.26 | 1.88 / 2.15 / 2.35 | 7.90 / 8.50 / 8.84 | 0.67 / 0.84 / 0.93 (n = 120) | 2.6-3.0 / 9.8-10.4 / 10.4-11.4 |
| Retail 30_49 | 1.10 / 1.26 / 1.37 | 1.93 / 2.03 / 2.05 | 6.78 / 7.32 / 7.38 | 0.66 / 8.55 / 8.90 (n = 156) | 3.0-3.6 / 10.0-10.4 / 10.0-10.4 |
| Retail 2923 32_24 (round-2 rules) | 0.67 / 0.90 / 1.08 | 2.74 / 2.98 / 3.09 | 12.40 / 13.52 / 13.95 | **8.80 / 34.63 / 37.62** (n = 264) | **9.2-9.3 / 34.8-35.5 / 36.0-38.1** |
| Retail 2923 32_24 (round-3 rules) | 0.65 / 0.73 / 0.74 | 2.80 / 3.09 / 3.14 | 12.46 / 13.08 / 13.45 | **8.99 / 34.41 / 36.46** (n = 384; first reads 9.12 / 35.06 / 36.35, n = 96) | **11.6-13.3 / 34.8-35.4 / 35.8-36.7** |
| Retail 2175 37_32 | 0.65 / 0.88 / 0.96 | 2.37 / 2.51 / 2.52 | 6.31 / 6.69 / 6.75 | 2.23 / 9.02 / 9.99 (n = 696) | 8.8-9.4 / 14.3-14.8 / 15.7-16.4 |

**FAIL against stage 03's 20 ms per-request budget: the 5,593,604-byte textures.** In integration round 2 the four such
textures of 2923 32_24 took 31.7-34.7 ms median per handler call (p95 33.0-36.2 ms; `check_terrain_run.py` "serving wow
texture <id> within 20 ms" fails for 6049364, 6049365, 6225353 and 6225354). Under the round-3 rules the tile also
fetches 5900005, 5900000, 5900003, 5906217 and their height textures 5900004, 5899999, 5900002, 5906216, all of that
size: on the verification binaries all twelve fail the check, handler medians 28.35-34.20 ms and p95 29.08-36.03 ms (n =
12 each over the three performance runs; 28.38-34.54 ms median over the 31-load memory run). The final binaries' single
performance process gives 28.31-35.40 ms median (5 requests each; texture handler p95 35.4 ms over the 128 later reads):
too few requests for the check's verdict, so that round judged nothing -- the FAIL stands on round 3's pooled verdict.
The design anticipated it (a read plus content-key check of such a file measured 19.7-29.9 ms warm, and the per-poll
budget makes such a file the poll's last), but the measured times exceed even that range. A file cannot be served in
parts on the GUI thread; the remedy is the audit's storage-owning serving thread (W3), a later stage. Every other group
is within budget (tex0 at most 13.95 ms, 1.4 MB textures at most 9.99 ms). The fix-round-2 verification's single 2923
load did not reach the 10 requests per (role, id) group the check needs, so it did not exercise the budget.

**Sizes and counts** (identical in every load of a tile):

| Tile | Material | Draw calls (chunk renderers) | Materials | Unique diffuse / height | Layer refs / duplicates | Source bytes | GPU texture bytes | Alpha array bytes | Vertex colour bytes |
|---|---|---|---|---|---|---|---|---|---|
| Retail 32_50 | textured 256 | 256 | 256 | 10 / 0 | 949 / 939 | 885,800 | 874,080 | 4,194,304 | 296,960 |
| Retail 34_46 | partial (static 11) | 254 | 254 | 9 / 0 | 824 / 815 | 709,868 | 699,320 | 4,194,304 | 642,640 |
| Retail 30_49 | textured 256 | 256 | 256 | 11 / 2 | 879 / 868 (height 12 / 10) | 3,772,980 | 3,757,744 | 4,194,304 | 380,480 |
| Classic Beta 32_48 | textured 256 | 256 | 256 | 7 / 0 | 893 / 886 | 620,060 | 611,856 | 4,194,304 | 890,880 |
| Classic Era 2853 34_50 | textured 256 | 256 | 256 | 10 / 0 | 938 / 928 | 885,800 | 874,080 | 4,194,304 | 296,960 |
| Retail 2923 32_24, round-2 rules | partial: textured 62, malformed 188, layerCountUnsupported 6 | 62 textured + 194 neutral | 62 | 11 / 11 | 219 / 208 (height 219 / 208) | 43,367,512 | 43,341,728 | 1,015,808 | 215,760 |
| Retail 2923 32_24, round-3 rules | textured 256 | 256 (all textured, 0 neutral) | 256 | 16 / 16 | 1,303 / 1,287 (height 1,303 / 1,287) | 90,914,944 | 90,877,440 | 6,078,464 (371 slices) | 890,880 |
| Retail 2175 37_32 | textured 256 | 256 | 256 | 29 / 29 | 780 / 751 (height 780 / 751) | 31,352,040 | 31,284,064 | 4,194,304 | 770,240 |

GPU texture bytes are the stored blocks with every stored mip (about 0.99 of the source bytes, the header and palette
being the rest); an RGBA32 path would have needed about four times that. The 128 MiB and 128-texture per-load budgets
are not reached: in integration round 2 the largest load was 43.4 MB and the most textures 58; under the round-3 rules
2923 32_24's 90.9 MB and 32 textures are the largest load (measured: 32 textures asked for and uploaded, 0 "not
requested"). (Under the fix-round-2 rules the same tile was textured 62, layerEffectUnresolved 79, layerCountUnsupported
115.) Its four neighbours, loaded in one run on the verification binaries, are textured 256 each: 31_24 15 / 15
textures, 88,116,344 B, 380 alpha slices; 33_24 12 / 12, 71,331,936 B, 313; 32_23 13 / 13, 74,130,536 B, 300; 32_25 11 /
11, 60,144,728 B, 373.

## 18. Memory

`repeat:100` then `clear`, three processes each for Retail 32_50 and 30_49 (integration round 2 part C, 13:23-13:26):
harness PASS 413/0/0 each, `check_terrain_run.py` PASS 9,421/0/0 over the six; 101 loads built per process, each
answered once.

| Run | Objects (every load) | Textures / GPU bytes / alpha arrays (every sample) | Player native: load 1 -> 2 -> 101 | Native, loads 52-101 | Host private bytes, sample 50 -> 100 |
|---|---|---|---|---|---|
| 32_50 x 3 | Mesh 257, Material 318, terrain GameObjects 257 | 10 / 874,080 / 1 | 143.7-144.0 -> 145.0-145.5 -> 145.6-145.8 MB | +9 to +73 KB, slope 0.6-1.0 KB per load | +0 to +8 KB |
| 30_49 x 3 | the same | 13 / 3,757,744 / 1 | 143.7-144.0 -> 145.0-145.3 -> 145.5-145.8 MB | -51 to +23 KB, slope 0.03-1.0 KB per load | -74 to +12 KB |

- After `clear` every run returns to the no-terrain baseline: Mesh 1, Material 62, terrain textures 0, 0 bytes, alpha
  arrays 0.
- Native memory rises about 1.3-1.5 MB from the first load to the second (allocator and caches) and then stays within
  0.14-0.33 MB over the last 50 loads; at most about 1 KB per load against 0.87-3.76 MB of texture data per load: no
  per-load leak at texture scale. The managed heap moves by 2.1-3.8 MB with garbage collection timing.
- Part B's 334-load mixed run (successful, failed, timed-out and other-map loads): host +1.35 MB with no trend; player
  native 145.37 -> 145.92 MB from load 121 to 334.
- Since fix round 2 the texel dump is streamed in bands (a 2048 x 2048 texture no longer costs about 144 MiB of managed
  memory to dump); since round 3 each tile also holds one 84-byte sampler carrier, outside the texture tallies.
- Not measurable through the harness: a global Texture2D count (the protocol carries terrain textures, alpha arrays and
  the Mesh / Material counts); the self-test checks that every Texture2D and Texture2DArray of a tile, the carrier
  included, is gone after release.
- Round 3, Retail 2923 32_24, `repeat:30,clear` (31 loads, verification binaries; harness PASS 203/0/0): every built
  load Mesh 257, Material 318, terrain GameObjects 257, 32 textures / 90,877,440 GPU bytes / 256 chunks drawn textured /
  371 alpha slices; after `clear` back to 0 textures, Mesh 1, Material 62. Player native memory 162.7 MB at load 1, then
  between 177.1 and 250.4 MB with a falling trend (-560 KB per load over loads 2-31, -147 KB over 16-31); the managed
  heap alternates between 31.4 and 118.2 MB with the timing of garbage collection (about 87 MB of per-load buffers): no
  growth at texture scale. Host private bytes +5.6 MB after the first load, then flat (2,341,675,008 -> 2,341,560,320
  B). 32_50 and 30_49 were not re-measured.

## 19. Regression tests

| Item | Integration round 2 (player `8D93FF2E...`) | Later rounds; final binaries |
|---|---|---|
| C# runner, corpus set | ALL RUN TESTS PASSED: WowParser 792, WorldParser 147, TerrainMesh 86, TerrainMaterial 196, WorldCorpusValidation 74, TerrainCorpus 80 (81 with the dump folder set), TerrainMaterialCorpus 187 | fix round 2: ALL PASSED, TerrainMaterial 220, TerrainMaterialCorpus 188 (the others unchanged); round 3: ALL PASSED, TerrainMaterial 250, TerrainMaterialCorpus 226; every earlier fit and pin value identical; verification: ALL PASSED, 792 / 147 / 86 / 250 / 74 / 81 (with the dump folder; 80 without) / 226; final binaries: the same, the log identical to round 3's but for timings |
| netstandard compile | 0 warnings, 0 errors | the same after fix round 2, round 3, on the verification tree and on the final tree |
| Python suite, corpus set | Ran 110, OK (skipped 2: "WMV_TERRAIN_DUMP_DIR not set"; both pass with it set) | fix round 2: Ran 119, OK (skipped 2); round 3: Ran 127, OK (skipped 2), without the corpus Ran 73, OK (skipped 19); the 8 earlier golden values repr-identical; verification: Ran 127; final: Ran 129, OK (skipped 2; both pass with the dump folder set), the 2 new tests the unresolved-repeats checker and the PNG-input limit |
| Player self-test | 827 passed, 0 failed | fix round 2: 834 / 0; round 3: 841 / 0 (twice); verification: 841 / 0; final binaries: 841 / 0 (twice) |
| Stage 03 geometry | runner dumps (10) byte-identical to stage 03; player dumps of 32_50 and 34_46 in the shaded view byte-identical to stage 03's (the textured view's dump differs only in its `meta view` line); `compare_terrain_mesh.py` PASS x 4 | runner dumps byte-identical again after fix round 2 and round 3; n <= 3 composites bit-identical across round 3 (17 digests); verification and final tree: runner dumps identical again (10 files); the player's shaded-view geometry dumps were not rerun |
| WDT / ADT parsing, holes, coordinates | covered by the runner suites and the mesh comparisons above, unchanged | unchanged |
| C#/Python chunk states | first fix round: 139 tiles (35,584 chunks), 0 differences | fix round 2: 145 tiles (37,120 chunks), 0 differences; verification and final tree (round-3 rules, repeats and phases compared): 145 tiles, 0 differences |
| Late answers (stage 03 sequence) on the stage 04 player | PASS 421/0/0 (63 loads: built 11, failed 43, superseded 3, cleared 5) | final binaries: PASS 421/0/0 (checker 189/0/0), the same 63 loads |
| Client switching (stage 03 sequence) | without MoP PASS 141/0/0; the MoP step failed because MoP Classic's storage did not open on this machine then ("error 2", confirmed by a read-only extraction): SKIP, environment | final binaries: without MoP PASS 141/0/0; with MoP PASS 152/0/0 (the storage opens again: 9 reports, 5 built, MoP 0 32_48 with material `textured`); MoP materials end to end: section 3 |
| Stage 03 worst tile (1051 31_29, repeat:10) | PASS 141/0/0 | final binaries: PASS 141/0/0 |
| Stage 03 lifecycle sequence | PASS 233/0/0 | final binaries: PASS 233/0/0 (checker 456/0/0); the stage 03 player against the final host 225/0/0 (as against the verification host) |
| M2 rendering (`-mo creature/bear/bear.m2 -unityipctest`) | PASS, 15 of 15 requests; pinned-animation capture byte-identical to the stage 03 player's | fix round 2, verification and final binaries: PASS 15/15, the pinned capture byte-identical (MD5 `2627227A...`) |
| WMO rendering (`-wmo 108631 -unityipctest`) | PASS, 44 of 44 requests; capture byte-identical to stage 03's | fix round 2, verification and final binaries: PASS 44/44, capture byte-identical (MD5 `D3DB7749...`) |
| casc-whole-read | PASS 24/0 | final binaries: PASS 24/0 |
| wdc3-strings | PASS 54/0 | final binaries: PASS 54/0 |
| Map catalogue sequences | retail_beta_retail 44/0, era_retail 27/0, retail_retail 27/0 PASS; retail_mop_retail 27/1, the MoP load failing on its storage (environment: SKIP) | final binaries: all four PASS -- retail_mop_retail 44/0 (the MoP storage opens again), retail_beta_retail 44/0, era_retail 27/0, retail_retail 27/0 |
| Map catalogue against the audit | 72,413 checks, 1 difference: Beta probe 787450 is now "available"; the stage 02 expectation says "not installed locally" and the file has since been installed (environment change, not a code fault; reported as FAIL) | final binaries: the same single difference in 72,413 checks; the MoP catalogue against the audit 0 differences in 31,920 checks (285 maps) |
| Viewport colour behaviour | unchanged for models and buildings (byte-identical captures above); terrain textures uploaded UNorm and blended in authored values, the frame decode applied once (section 8.4) | unchanged |

## 20. Adversarial review

Four review rounds and the checks of fix round 2, each finding checked by an independent verifier told to refute it; a
reviewer's "looks correct" was not accepted (every reviewer reported what it tried to break and how).

**Design review** (three reviewers before any code: gate evidence; protocol 13 and the host; Unity architecture and the
validation plan): 42 findings, 13 major and 29 minor, all folded into design v2. The major ones: height-textured chunks
drawn with plain weights would have been a silent substitution; the lava "shown as is" claim did not hold; the state
model contradicted itself; the pinned missing-texture case could not reach the not-in-storage path; the request queue
was underspecified and stage 03's late-answer checks would have given random results with several requests outstanding;
the two BLP decoders disagreed by more than the V1 tolerance (native block upload and V1 from the GPU's own texels
followed); the RGBA32 path had no memory bound; a camera render is not free of colour processing (the CommandBuffer dump
followed); the reference's box filter is not what the GPU samples (own stored mips followed); MCCV overflow; object
creation time during a load.

**Implementation review, round 1** (nine dimensions: texture references, alpha decoding, blending, UV mapping, colour
space, shader equivalence, cross-client ownership, disposal, caching): 26 findings (1 critical, 6 major, 19 minor); the
verifier upheld 22 in substance (several narrowed); 19 distinct defects, all fixed in the v3 fix round. The critical one
is the disputed blend rule (section 5.2).

**Final review** (after the v3 fix round; six dimensions, the tenth spec dimension "reported visual accuracy" among
them: rule equivalence across shader, C# and Python; height textures and states; validation tautologies; visual-accuracy
claims; lifecycle and ownership; host IPC): 20 findings, 15 confirmed (all minor after verification; the one raised as
major, "a broken height on layer index 3 or a -0.5 LOD bias passes every gate", was confirmed as a gap in the acceptance
gates and downgraded because the shipped shader was right), 5 refuted. Confirmed and fixed (fix round 2):

| Finding | Fix |
|---|---|
| V2's per-pixel comparison was information only; a layer-3 height slip (62.9/255) and a -0.5 mip bias (7.1/255) passed | the per-pixel check is mandatory, against the reference composed under named block decodes (section 10.3); both mutants now fail; the self-test has heights at layer indices 2-4 (its dropped-index-3 mutant fails at 317/255) |
| The texel check accepted any interpolant inside its endpoints (snapped, swapped, reversed selectors passed) | every interpolant must equal one named decode (section 10.4); those mutants now fail |
| Reports of loads that ended before adoption carried unsettled chunk states | answered failures are applied first; unanswered ones are named in the reason |
| Status and capture labels described the requested view, not what was drawn | labels count what was drawn textured and neutral, name validation sampling and a missing shader |
| Textures uploaded for chunks an upload failure removed stayed on the GPU | they are released and the tallies equal what the materials bind |
| The texel dump was not banded although its header said so | it is streamed in bands |
| A LayerEffectStatic chunk with a failed texture was counted as fully drawn and kept in the golden masks | it keeps the contract's state, the log counts the checker it draws, and the masks exclude it |
| A height texture without an alpha channel was read as h = 1 | it fails in the height role only (HeightTextureMissing) |
| Height values making H negative or overflowing float32 were drawn as Textured, the three implementations disagreeing | LayerEffectUnresolved in C# and Python |
| No gate checked how heights are sampled | "heights at 8 repeats / 1 repeat / red channel" asserted worse in Python and C# |
| The reference keyed repeats per FileDataID; inexact mip levels were sampled silently; both-role ids lost a role | per-layer pairs; a note for every inexact level; one dump entry per role |
| The RGBA32 fallback of block textures used a truncating decode | BlpDecoder's opt-in exact block mode; the self-test compares it with an independent decode |
| A host log line passed player text through QString::arg() | appended instead |

Refuted (no change): the 0.25 golden allowance has no rationale (it is the contract's, and the golden tests, not V2's
block threshold, separate the rules); an unreported +0.5/255 bias (printed on every run, shared by every rule, cause not
established -- truncating the oracle would be choosing a convention because it fits); the MCCV divisor labelled as
verified (the contract records it as not separable; /127 is the data's neutral value); filtering never exercised (the
contract's V1 and V2 are defined at texel centres); C# and Python counts differing after a failure (different,
documented scopes, never compared).

**Checks of fix round 2** (three checkers: the gates and tolerances; the player part; rule equivalence in C# and
Python): 7 findings, all minor, 6 distinct (two checkers found the first), all fixed in round 3: the texel check failed
the player's own rounded RGBA32 fallback (a third named decode, "rounded", for `R8G8B8A8_UNorm` entries); V2's per-pixel
limit borrowed V1's 2/255 rationale (its own float16 budget, set to 0.5/255 in round 3 and to 0.26/255 in the final
round; an MCCV /128 mutant had passed at 2/255); a
comment claimed real DXT1 coverage that does not exist (reworded, with a reproducible count); the single-chunk label
stated something false about the tile (it now names the chunk shown); an unresolved-scale layer was drawn differently by
the CPU twin (one stretched texel) and the GPU (the checker), and its texture was fetched but never bound (the twin
draws the checker; such textures are no longer listed or fetched). The checkers also confirmed what held: the hardware
model exact on every real interpolant, C# and Python equal on 51 of 52 crafted rows of real bytes and 10 of 10 synthetic
rows, the golden values and stage 03 dumps unchanged.

**Checks of round 3** (three checkers: nibble 4 and the UV phase across shader, C# and Python; the eight-layer
layout, packing, sampler budget and carrier; the gates and every new claim), each finding put to an independent
verifier: 6 findings, all confirmed, all minor (the one raised as major -- V2's new per-pixel limit failed the
player's own dump when read through the documented 8-bit PNG input -- was downgraded: it can only produce a false
FAIL, never a false PASS). What held: the shader, C# and Python agree on the phase at every chunk parity and on the
overlays of the second slice; no height texture is ever read through a default sampler and the carrier is never
drawn; layers 5..7 dropped or layers 0..3 alone fit the five map-2923 textures at 10.5 and 16.3 against 1.07, so the
eight-layer composite is discriminated. Fixed in the final round: the Python reference drew a layer of unresolved
scale (nibbles 5..15, off every gated path) as one stretched texel where C# and the GPU draw the checker -- now the
checker, its height texture refused, and the chunk dump refuses chunks that are not drawn; V2's per-pixel limit was
not derived from its stated budget (the stated 0.249/255 step can never apply to clamped values) -- restated, see
section 10.3, with its own limit for 8-bit PNG input; the host comment on the report bound claimed more than the
evidence ("the most any tile has" -> "8 is the most observed"); and the layer-count wording edited after the round-3
builds is in the final player and host, rebuilt from the final source (section 0).

**Disputed findings and their resolution**

1. Plain weights against the height-mix cutoff (implementation review, critical): resolved for the cutoff, with the
   finding's own MPHD 0x80 keying corrected (section 5.2).
2. The texel tolerance: the design's 2/255 was disproved by measurement; first replaced by "endpoints exact,
   interpolants between their endpoints", which the final review showed was too loose; resolved by the named-decode
   rule, with this GPU's decode measured (colour: exact on every real interpolant; DXT5 alpha: weights refit in /2048
   because the reviewer's first model was off on 1,102 of 84,366 inputs, then verified on 11,700 held-out inputs).
3. The report-list bound: 1,280 in the v3 delta against 2,560 in the host (height entries double the list); now 4,096
   with 8 layers.
4. MTXP scale nibble 4 on real data (map 2923): first classified Malformed by the "0..3 observed" rule, then
   LayerEffectUnresolved (the file is not broken), then established by a dedicated investigation against five map
   textures (section 5.4) and implemented in round 3.
5. More than 5 layers: "never observed in a drawable chunk" was false on maps 2923 and 2858; the baker composites up to
   8 layers by the same rule (section 5.4); implemented in round 3.
6. The V2 per-pixel limit: 2/255 (V1's rationale) against the checker's budget argument; set to 0.5/255 in round 3;
   a round-3 checker showed that limit was not derived from its own budget and failed the 8-bit PNG input; resolved
   for 0.26/255 (twice a correctly stated float16 budget) with 0.76/255 for PNG input, and a third named decode instead
   of a wider limit for a CPU-decoded fallback.

## 21. PASS / FAIL / SKIP / UNSUPPORTED summary

A SKIP is never counted as a PASS. The results hold for the final binaries, which were re-verified in full (section 0),
unless an item says otherwise.

**PASS**
- Texture data through the production path on Retail, Classic Beta and Classic Era (8-bit maps): the tex0 from MAID
  word 3, the MDID / MHID textures, host kind checks and content keys, native block upload.
- MoP Classic end to end on the final binaries, once its storage opened again: 0 32_48 and 31_48 loaded, chunk states
  equal to the reference on all 512 chunks (31_48's two cube-map chunks neutral), V1 on 3 chunks (max 0.1458/255),
  texels of 18 textures (every interpolant the hardware model); the rule INFERRED on MoP (no oracle) (section 3).
- The height mix against the game's baked map texture on 9 golden tiles, every documented alternative asserted worse
  (Python and C#), including nibble 4 (four alternatives) and chunks of 6-8 layers on Retail 2923 32_24 (5.3).
- The nibble-4 investigation (5.4): `verdict.py` 57 checks, 0 failed; both equivalence checks; the per-chunk test on
  308 of 308 and 206 of 206 chunks.
- V2 on all 9 golden tiles on the verification and the final binaries at the 0.26/255 per-pixel gate
  (0.1235-0.1259/255 under the hardware model), symmetry and phase checks; the 8 earlier dumps byte-identical to
  integration round 2's, all 9 byte-identical between round 3 and the final build; Retail 2923 32_24 1.034 <= 1.307.
- V1 on 17 chunks in validation sampling (4 of them on Retail 2923 32_24, which a reference without the phase fails) and
  1 in production sampling.
- Texels on every real dump under the named-decode rule; the reviewer's texel and V2 mutants now fail.
- Chunk states, C# against Python: 145 tiles, 24 crafted, 5 failure cases, under the fix-round-2 and the round-3 rules.
- Failure tests (16): missing tex0 (Retail 2736 32_29); a texture not in client storage on a drawn layer (Classic Era
  169 25_31); MDID 0 (Retail 169 24_27); invalid BLP and wrong-kind reads; corrupt alpha and malformed layer counts
  (synthetic); 4-bit alpha (Classic Era 32_50); client switch mid-load; superseded requests; player restart; repeated
  successful and failed loads; neutral geometry with the state identified; protocol compatibility both ways (part B's
  sequences and the compatibility runs rerun on the final binaries with equal counts).
- Memory flat over 101 repeated loads and over 334 mixed loads, and over 31 loads of Retail 2923 32_24 (18).
- Regression (19): the C# runner, netstandard, Python suite (129) and self-test in every round and on the final
  binaries; stage 03 runner dumps byte-identical; M2 and WMO captures byte-identical; on the final binaries the stage 03
  late-answer, switch (with and without MoP), worst-tile and lifecycle sequences, casc-whole-read, wdc3-strings, all
  four map-catalogue sequences, and the MoP catalogue against the audit (0 differences).
- The harness's texture-count check in views that draw no textures: it failed four capture runs in integration round 2
  (and 16 runs of part C: its 15 shaded-view runs and one shaded-view dump run); made view-aware; the four capture runs
  pass on the fix-round-2 binaries with byte-identical shots; the shaded-view group itself was not rerun.

**FAIL**
- Stage 03's 20 ms per-request serving budget for 5,593,604-byte textures: 31.7-34.7 ms median per handler call for the
  four such textures of Retail 2923 32_24 in integration round 2 (6049364, 6049365, 6225353, 6225354); on the
  verification binaries all twelve such textures of the tile (5899999, 5900000, 5900002, 5900003, 5900004, 5900005,
  5906216, 5906217, 6049364, 6049365, 6225353, 6225354), 28.35-34.20 ms median -- the pooled verdict; on the final
  binaries 28.31-35.40 ms median from one performance process, too few requests for a verdict (not a pass). Remedy: a
  storage-owning serving thread (audit W3), a later stage.
- Map catalogue against the audit: 1 difference in 72,413 checks, a stale expectation (Beta probe 787450 has since been
  installed; environment, not code); unchanged on the final binaries.

**SKIP** (not applicable here, or not available on this machine; none counted as a pass)
- MoP Classic during the stage: its storage did not open ("error 2"), so the MoP steps of integration round 2 (the
  stage 03 switch sequence's MoP pair, retail_mop_retail 27/1) were environment failures and MoP was verified at corpus
  level only. The storage opened again late on 2026-10-13: on the final binaries the MoP catalogue (44/0; 0 differences
  against the audit) and the stage 03 switch with MoP (152/0/0, MoP 0 32_48 built `textured`) pass, and the MoP
  materials check (0 32_48 and 31_48: states, V1, texels) passes -- listed under PASS; what remains is no V2 oracle
  on MoP (no map texture on Azeroth).
- Harness and checker skips: "a map without a WDT is refused" on Beta and Era (every map has a WDT reference); "the
  corpus holds Azeroth (map 0) only" for maps 169, 1917, 2175, 2736, 2853 and 2923 (compared with the reference by hand
  where the text says so).
- A texture "not installed locally" on a drawn layer: no real case in any local client (self-test and stand-in player
  only).
- An encrypted texture, a chunk without MCNK 0x8000, more than 8 layers, MTXP nibbles 5..15: no real case (crafted or
  synthetic tests only).
- Retail map 2858 40_11 (6-7 layers): its root is not in the audit data; not run.
- Retail 33_50 V2: not a golden tile; run with a threshold given by hand (informational).
- An in-game reference capture: none exists; no visual parity is claimed.

**UNSUPPORTED**
- 4-bit alpha (Classic Era Azeroth and the Beta maps 2720 and `pvpzone03`): AlphaUnsupported, drawn neutral; the
  draw-time expansion is UNRESOLVED (section 12).
- MTXP texture-scale nibbles 5..15: LayerEffectUnresolved (never observed).
- MTXP height values outside the height mix's domain, MCLY bits outside the drawn set (0x400 cube map), MTXP flag 0x1,
  MTXP word 3 != 0: LayerEffectUnresolved.
- HeightTextureMissing end to end in the harness (no hook refuses one FileDataID, and no real height texture fails).
- A client switch with a height-texture request outstanding, end to end in the harness (the hold hook holds by role
  only; the heights were still queued at every switch).
- A global Texture2D count through the harness (the protocol carries terrain textures, alpha arrays and Mesh / Material
  counts; the self-test covers it).
- Per-poll GUI time per role (a poll record has no role; per-poll time is reported per run, handler times per role).

**NOT EXERCISED** (no real data uses them; not a pass)
- DXT1 interpolated texels on real data (the model is confirmed on the self-test's synthetic DXT1 only).
- BGRA and palettised uploads, and a real RGBA32 fallback upload of a block texture (covered by the self-test's 6 x 6
  DXT5 and a crafted dump only).
- Layer-texture filtering away from texel centres (V1 and V2 are defined at texel centres; production views use the
  hardware's trilinear and anisotropic filtering unmeasured).
