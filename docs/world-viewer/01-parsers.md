# World viewer 01: WDT and split-ADT parsers

Branch `world/01-parsers`, from `develop` at `2f35172d` (2026-10-11, the merged audit). **Parsers only**: no rendering,
no meshes, no UI, no IPC change, no change to the viewer's C++ side. The audit
(`docs/world-viewer/00-audit.md`) is the specification; every implemented field cites it, and every rule is checked
against the pinned real-client corpus and against the audit's independent Python readers.

**Push status at delivery: NOT PUSHED.** The branch was delivered as a local commit and pushed for review on
request, as a pull request against `develop`.

## 0. Result in brief

| Check | Result |
|---|---|
| Parsers compile in the Unity Editor 6000.5.9f1 (whole `Assets/Scripts`, URP 17.5.0 project settings, final sources) | **PASS**: 0 errors, 0 new warnings |
| Same, as a .NET Standard 2.1 library at C# 9 (the player's API profile) | **PASS**: 0 warnings |
| Synthetic and malformed-input tests (`WorldParserTests`, 147 checks), in the console runner and inside the Unity Editor | **PASS**: 0 failures in both |
| Existing M2/WMO/BLP parser tests (`WowParserTests`, 792 checks, file unchanged), console and Unity Editor | **PASS**: 0 failures in both |
| Pinned inputs (243 files by SHA-256, 19 file-set digests) | **PASS** |
| Real-corpus verdicts (`WorldCorpusValidation`, 74 checks: 2 pin checks, then Retail 20, Beta 17, Era 18, MoP 17) | **PASS**: 74 of 74 |
| C# results vs the audit's Python readers on the same bytes (`compare_csharp.py`) | **PASS**: 2,551 comparisons covering 11,907 values, 0 mismatches |
| Audit Python tests (`Tools/world-audit/tests`, 28 tests), with the corpus | **PASS**: 28 of 28 (12 of them are SKIPPED without the corpus) |
| Adversarial review: ten claims, each attacked and then verified from scratch | 5 survived, 4 weakened, 1 refuted. All 43 confirmed findings fixed (section 13) |
| WMW C++ build | **SKIP**: no C++, CMake or IPC file changed (diff in section 12) |
| Unity player build and `-wmvLifecycleTest` | **SKIP**: no player code changed and nothing calls the new parsers; the Editor compile covers compilation |

Builds tested: Retail 12.1.0.69933, Classic Beta 1.60.1.70338, Classic Era 1.15.9.70003, MoP Classic 5.5.4.70268. These
are the builds the corpus was pinned from, and still the installed builds on 2026-10-11 (`.build.info`).

## 1. Source inventory

| File | Content |
|---|---|
| `Tools/UnityRendererProject/Assets/Scripts/Wow/World/WorldChunks.cs` | Chunk walk, the chunk-bounded reader `WorldSpan`, `WorldParseException` (file, chunk, offset, reason), `WorldIssue` / `WorldDataStatus` for non-fatal findings |
| `.../World/WdtParser.cs` | `WdtParser.Parse` -> `WdtFile` (MVER, MPHD, MAIN, MAID, MWMO, MODF; MAI2/MANM listed) |
| `.../World/AdtModel.cs` | Result types of the ADT parts, with per-field evidence |
| `.../World/AdtParser.cs` | `ParseRoot`, `ParseTex`, `ParseObj0`, `ParseObj1`, `ParseLod`, `ParseTile`; alpha decoding (`DecodeRle`, `DecodeRaw`, `DecodeAlpha`) |
| `.../World/TerrainLayout.cs` | `TerrainCoordinates` (double, integer-index formulas), `TerrainVertexLayout` (145-entry layout), `TerrainHoles` |
| `.../World/WorldPlacements.cs` | MDDF/MLDD, MODF and MLDX records; `PlacementTransform` (the proven transform, double, game frame) |
| `Tools/UnityRendererProject/Tests/WorldParserTests.cs` | 147 synthetic and malformed-input checks |
| `Tools/UnityRendererProject/Tests/WorldCorpusValidation.cs` | Real-corpus validation (pins, tables, verdicts, performance) |
| `Tools/UnityRendererProject/Tests/Runner/` | Console runner (`WowParserTestRunner.csproj`, `Program.cs`) and the .NET Standard 2.1 compile check (`WowParsingNetStandard.csproj`); output goes to the git-ignored `build/` |
| `Tools/UnityRendererProject/Tests/README.md` | How to run the tests |
| `Tools/world-audit/compare_csharp.py` | Key-by-key comparison of the C# results with the Python results |
| `Tools/world-audit/wowfmt/adt.py`, `coords.py`, `tests/test_wowfmt.py` | Review fixes to the audit's Python reader (section 13.3) |
| `docs/world-viewer/00-audit.md` | Evidence corrections found by the review (section 13.3) |

Namespace `Wmv.Wow.World`. No `UnityEngine` reference, no static mutable state, no file or network IO: the parsers take a
`byte[]` from their caller and return validated structures.

## 2. Architectural boundary and the database question

- **Parsing only.** No GameObject, Mesh, Material, Texture, camera, IPC message or host change. Nothing in the player
  calls the new code yet (a repository-wide search finds no reference outside `World/` and its tests), so normal viewer
  operation is unchanged.
- **No file resolution.** The parsers never open CASC. `WdtFile` returns what the WDT states: FileDataIDs (MAID, MPHD,
  modern MODF) or an MWMO path (old WMO-only form, `WorldFileReference`). Resolving them is the host's job. Retail and
  Beta resolve by FileDataID only; Era and MoP can also resolve by path (audit 4.1). The resolver is not built or
  changed here. `WdtFile.TileExists` reports the MAIN flag only: it says nothing about whether the client has the files
  (Beta Kalimdor 44_24's root is `NOT_LOCAL`). Availability is reported per part as `AdtPartState.NotSupplied`.
- **No second database system.** The host does not read Map.db2 today: its 12.0 schema (`bin_support/wow/12.0/database.xml`)
  defines 56 tables, none of them Map, AreaTable, Light or Liquid (audit 2.2). The map catalogue (Map.db2 field 21 =
  WdtFileDataID in Retail/Beta, the directory path rule in Era/MoP) belongs to the host's database and profile
  system: W2 adds it there (a table definition plus the column proof of audit 3.1). No C# DB2 reader was written,
  because WDT/ADT parsing needs none. The corpus validation reads WDTs by FileDataID from the pinned folder.

## 3. Structures implemented, with evidence

All integers are little-endian and unsigned unless marked. Offsets are within the record or chunk payload. "Proven"
cites the audit section and the corpus check that re-proves it here (section 6). "Raw" means kept as stored with no
meaning asserted. "Unexpected" is a non-fatal `WorldIssue` for well-formed data the audit never saw.

### 3.1 Container (WDT, every ADT part)

| Structure | Field | Offset | Width | Status | Evidence |
|---|---|---|---|---|---|
| Chunk | tag | 0 | 4 bytes, stored reversed | proven | audit 4.2, 5.1; every audited file walks without error (C#: every WDT of four clients and every window part) |
| Chunk | payload size | 4 | u32 | proven; checked against the container before use | same |
| MVER | version | 0 | u32 | proven = 18 for WDT and all ADT parts (other values: Unexpected) | census of all window parts and WDTs |

### 3.2 WDT

| Chunk | Field | Offset | Width | Count source | Status | Evidence |
|---|---|---|---|---|---|---|
| MPHD | flags | 0 | u32 | 1 | raw; bit 0x1 <=> WMO-only (100%, all WDTs of four clients) | audit 4.2; C# `wdt_flags` comparison |
| MPHD | `_lgt/_occ/_fogs/_mpv/.tex/.wdl` FileDataIDs | 4..24 | u32 x6 | 1 | proven on Azeroth: Retail by each side file's chunk ids; the 12-byte MVER-only side files (Beta `_mpv`, MoP `_fogs/_mpv`, Era `_lgt/_fogs/_mpv`) by MVER version and Retail's FileDataID. Other maps: listfile names | audit 4.2 (corrected), T8 |
| MPHD | word 7 | 28 | u32 | 1 | raw (non-zero exactly on modern WMO-only WDTs; target never read) | audit 4.2 |
| MAIN | flags | 8i | u32 | 4096, index y*64 + x | values 0, 1, 2 only; value 1 <=> MAID names files (proven). The parser uses bit 0x1 and raises Unexpected, at the entry, for any other value. Flag 2 meaning unknown | audit 4.2; 100% in C# census |
| MAIN | word 1 | 8i + 4 | u32 | 4096 | raw (0 in every entry; non-zero = Unexpected) | audit 4.2 |
| MAID | root, obj0, obj1, tex0, lod, map texture, map texture _n, minimap | 32i + 4k | u32 x8 | 4096, index y*64 + x | words 0-3 by chunk content in all four clients; word 4 by content in Retail/Beta; words 5-7 by BLP2 size, mip count and decoded image in Retail/Beta; Era/MoP words 4-6 by listfile only (zero in their windows) | audit 4.2 (corrected), T3 |
| MWMO | names | 0.. | NUL-separated, one byte per char | chunk size | proven present on the 193 old Retail WMO-only WDTs, one name at byte 0 | audit 4.3 |
| MODF | see 3.5 | | 64-byte records | size / 64 (non-multiple rejected) | proven | audit 4.3, 6.4 |
| MAI2, MANM | -- | | | | unsupported: listed, not decoded | audit U4, U5 |

`WdtKind`: TiledTerrain (a tile with MAIN bit 0x1), GlobalWmo (MODF and no tile), Empty (neither). Tiles plus MODF
together never occur in the audited clients, so the parser keeps both and raises Unexpected.
`TryGetGlobalWmoReference` refuses a flag-0x8 MODF with FileDataID 0 (0 names no file, as in MAID), and resolves an
old-form MWMO name only when NameId is 0 and the single name starts at byte 0, where the index and byte-offset
readings agree. Either case raises an issue.

### 3.3 ADT root (`AdtRoot`)

| Chunk | Field | Offset | Width | Status | Evidence |
|---|---|---|---|---|---|
| MCNK x256 | header | 0..127 | 128 bytes, kept whole as `RawHeader` | proven size | audit 5.1 |
| MCNK header | Flags | 0 | u32 | raw; 0x40 <=> MCCV, 0x1 <=> tex0 MCSH, 0x10000 = high-resolution holes (100% correlations) | audit 5.2 |
| MCNK header | IndexX, IndexY | 4, 8 | u32 | proven = n % 16, n / 16 (100% of 33,280 chunks; a mismatch marks the chunk Malformed) | audit 5.1 |
| MCNK header | high-resolution hole mask | 20 | u64 (when flag 0x10000) | proven: bit = row*8 + col, LSB first. Flag clear (never seen): Unexpected, +20 not read as holes | audit 5.4, 6.5 |
| MCNK header | AreaId | 52 | u32 | proven (AreaTable ids) | audit 5.2 |
| MCNK header | low-resolution holes | 60 | u16 | raw; bit layout not established (always 0) | audit 5.2 |
| MCNK header | StoredPosition | 104 | f32 x3 | proven within 1/768 yd (0.0013021, reached); NOT used for x/y (see 4) | audit 6.1 (corrected), 6.2 |
| MCNK header | other words (12, 16, 24..48, 56, 62..100, 116..124) | | | raw in `RawHeader`; the census of 88/96/116 (= MCSE/MCSE/MCCV offsets) is reproduced | audit 5.2 |
| MCVT | heights | 0 | f32 x145 (580 bytes exactly) | proven layout (see 4) | audit 6.2 |
| MCNR | normals | 0 | 435 signed bytes + 13 bytes | raw: component order and scale not established (U9). Wrong size: chunk Malformed | audit 5.1, 5.3 |
| MCCV | vertex colours | 0 | 145 x 4 bytes (580 exactly) | raw: channel order not established. Wrong size: chunk Malformed | audit 5.5 |
| MCSE | -- | | size only (0 in every chunk) | recorded; a non-empty or repeated MCSE is listed as Unsupported with an issue | audit 5.1 |
| MCBB, MPTX, MHDR, MFBO, MBMH/MBBB/MBMI/MBNV | -- | | | unsupported, listed | audit 5.8 |
| MH2O | header table | 12n | u32 instances offset, u32 count, u32 attributes offset (from the MH2O payload start) | partially decoded. Every instance table must start after the 3,072-byte header table, lie inside MH2O and not overlap another; all are checked before anything is allocated | audit 5.7 |
| MH2O | instance | 24-byte records | u16, u16, f32, f32, u8 x4, u32, u32 | raw; vertex data, bitmaps and attributes not decoded (U14) | audit 5.7 |

### 3.4 ADT tex0 (`AdtTex`)

| Chunk | Field | Offset | Width | Status | Evidence |
|---|---|---|---|---|---|
| MDID / MHID | texture FileDataIDs | 0.. | u32 array | proven (diffuse; height textures' blending rule unknown, U11). Wrong size: issue, empty table | audit 5.5 |
| MAMP, MTXP | -- | | | raw (U11, U12) | audit 5.5 |
| MCNK x256 | sub-chunks from byte 0 | | | proven | audit 5.1 |
| MCLY | TextureIndex | 0 | u32 | proven index into MDID (out of range = invalid reference, chunk Malformed) | audit 5.4 |
| MCLY | Flags | 4 | u32 | raw; 0x100 on exactly the layers after the first (asserted per chunk), 0x200 = compressed | audit 5.4 |
| MCLY | AlphaOffset | 8 | u32 | proven: maps back to back from 0 | audit 5.4 |
| MCLY | word 12 | 12 | u32 | raw (no meaning established) | audit 5.4 |
| MCLY | layer count | | size / 16 | at most 8 decoded (Beta has 11 five-layer chunks; more than 8 = Malformed) | audit 8 (corrected) |
| MCSH | -- | | 512 bytes | raw: bit layout not established | audit 5.4 |
| MCAL | alpha maps | | see 5 | proven storage | audit 5.4, 6.5 |

### 3.5 Placements (obj0, obj1, WDT)

| Record | Field | Offset | Width | Status | Evidence |
|---|---|---|---|---|---|
| MDDF / MLDD (36) | NameId | 0 | u32 | FileDataID when flag 0x40 (every audited record). Unflagged in a file without name tables: issue | audit 5.6 |
| | UniqueId | 4 | u32 | proven identity (copies identical, 6.5) | audit 5.6; C# dedup report |
| | Position | 8 | f32 x3, file frame | proven | audit 6.3 |
| | Rotation | 20 | f32 x3, degrees | proven | audit 6.4 |
| | Scale | 32 | u16, /1024 | proven (MLDX; u16/1000 fits 365 doodads at 0.01 yd, u16/1024 fits 23,873) | audit 6.4; review 13 |
| | Flags | 34 | u16 | raw (only 0x40 correlated) | audit 5.6 |
| MODF (64) | NameId, UniqueId, Position, Rotation | 0..31 | as MDDF | FileDataID when flag 0x8; else an MWMO entry (old WDT form) | audit 4.3, 5.6 |
| | ExtentsMin, ExtentsMax | 32, 44 | f32 x3 each, file frame | proven exact world box (201/202 from the MOHD box, 202/202 from the group-box union) | audit 6.4; section 6.5 |
| | Flags | 56 | u16 | raw (0x4, 0x8 correlated) | audit 5.6 |
| | words 58, 60 | 58, 60 | u16 | raw (doodad set / name set by name only, U15) | audit 4.3 |
| | Scale | 62 | u16 | raw. `Scale` = u16/1024 when flag 0x4 is set (every ADT MODF: 1024), else 1.0 (WDT: 1024 with flag 0x8, or 0 in the old form; INFERRED) | audit 5.6, 4.3 |
| MLDX (28) | Min, Max | 0, 12 | f32 x3 each, world frame | proven (25,621/25,648). Box i belongs to MLDD record i | audit 6.4 |
| | value 6 | 24 | f32 | raw (not the M2 radius, radius x scale or box diagonal) | audit 5.6; review 13 |
| obj0 MCNK | MCRD, MCRW | 0.. | u32 arrays | raw (counts measured only) | audit 5.6 |

MLDD holds the same records as obj0's MDDF, matched by uniqueId, not by position: on Retail 29_48 and 30_48 the order
is a permutation. Every record is kept as stored, with its source (tile, container, record index). Nothing is
de-duplicated during parsing (section 8).

## 4. Terrain geometry (coordinates, vertex layout, holes)

- **Coordinates** (`TerrainCoordinates`): T = 1600/3, C = T/16, U = C/8. Chunk origin x = (32 - ty)T - iy*C,
  y = (32 - tx)T - ix*C, evaluated as (4096 - R)*U from the integer global vertex row R = ty*128 + iy*8 + r (and the
  column likewise), in double. The two sides of every seam therefore evaluate the same expression and agree bit for
  bit; that is a property of the formula, tested over a whole map row and column, not a finding about the data. The
  stored float32 position (within 1/768 yd of the formula) is kept but not used for x/y: it is not seam-consistent
  (audit 6.2). `XOfRow`/`YOfColumn` are the unchecked linear formulas; the other entry points check their ranges.
- **Vertex layout** (`TerrainVertexLayout`): outer vertex (r, c), r, c in 0..8, is MCVT[17r + c]; inner vertex (r, c),
  r, c in 0..7, is MCVT[17r + 9 + c] at the cell centre. The 81 outer and 64 inner vertices stay distinct: no
  regular-grid reinterpretation. Height = stored position z + MCVT value.
- **Holes** (`TerrainHoles`): kind HighResolution when flag 0x10000 is set (bit = row*8 + col, LSB first). Otherwise
  LowResolutionUnsupported: `IsHole` returns null for a non-zero mask instead of guessing a layout. A malformed MCNK
  whose header was not read has kind Unknown, and `IsHole`/`HasAnyHole` return null rather than "no holes".
- The game coordinate system is preserved; conversion to Unity belongs to W4 (audit 6.6, INFERRED, gate G4).

## 5. Alpha maps

- Layers after the first carry maps, back to back from MCAL offset 0, in layer order. The parser asserts that MCLY
  flag 0x100 marks exactly those layers (the audit's 100% correlation). A chunk where it does not is reported, at the
  layer's flags word, and not decoded.
- Compressed (0x200): count byte n, top bit set = repeat the next byte (n & 0x7F) times, clear = copy (n & 0x7F) bytes,
  exactly 4096 values, ending exactly where the next map starts. A run that would cross value 4096 or read past MCAL
  is rejected.
- Uncompressed: the gap to the next map (or to the end of MCAL) decides the storage. 4096 bytes = 8-bit;
  2048 = 4-bit, low nibble first. Any other gap is rejected. MPHD flags are never consulted. A chunk that mixes 4-bit
  with 8-bit or compressed maps cannot come from a client that sets the depth per map; it is decoded and flagged
  Unexpected (none exists in the four clients).
- Texel (row, col) = sample[row*64 + col].
- **4-bit samples stay 0..15.** `AlphaMap.TryGetEightBit` returns false for 4-bit maps, with the unresolved requirement
  (audit U19), and never a guessed 17n or 16n. For 8-bit maps it returns the map's own `Samples` array, not a copy:
  callers treat it as read-only. Era's repeated row and column 63 are data, not altered (section 6.6).

## 6. Validation on the real corpus

### 6.1 Inputs

`WorldCorpusValidation` hashes every input before using it. Both checks pass:
- the 243 pinned files match their SHA-256;
- the 19 file sets (9,441 files) match their digests.

On a changed input it stops without comparing. Client builds of the pinned bytes: Retail 12.1.0.69933, Beta
1.60.1.70338, Era 1.15.9.70003, MoP 5.5.4.70268, unchanged in the installation on 2026-10-11.

### 6.2 WDT

| Client | WDTs parsed | Kinds | Issues | MAIN bit 0x1 <=> MAID | flag 2 with MAID words | MAIN values other than 0/1/2 |
|---|---|---|---|---|---|---|
| Retail | 969 of 969 | 728 tiled, 228 global WMO, 13 empty | 0 | 0 mismatches | 0 | 0 |
| Classic Beta | 69 of 69 | 48 tiled, 21 global WMO | 0 | 0 | 0 | 0 |
| Classic Era | 58 of 58 | 37 tiled, 21 global WMO | 0 | 0 | 0 | 0 |
| MoP Classic | 227 of 227 | 194 tiled, 33 global WMO | 0 | 0 | 0 | 0 |

The MAID roles found from content (`RoleOf`: MHDR, MDDF, MLDD, MAMP, MLHD, BLP2 headers) equal the audit's in all four
clients.

### 6.3 ADT parts

| Client | Window tiles | root | tex0 | obj0 | obj1 | lod | Malformed or Unexpected issues |
|---|---|---|---|---|---|---|---|
| Retail | 56 (14,336 MCNKs) | 56 parsed | 56 | 56 | 56 | 56 (inventory, Unsupported note) | 0 |
| Classic Beta | 9 (2,304) | 9 | 9 | 9 | 9 | 9 (inventory) | 0 |
| Classic Era | 56 (14,336) | 56 | 56 | 56 | 56 | NotNamed (MAID word 0) | 0 |
| MoP Classic | 9 (2,304) | 9 | 9 | 9 | 9 | NotNamed | 0 |

### 6.4 Coordinates, seams and holes

| Client | Tile pairs | Chunk edges (across tiles) | Shared points | Max height difference | Mismatches |
|---|---|---|---|---|---|
| Retail | 97 | 28,432 (1,552) | 255,888 | 0.0000153 yd | 0 |
| Classic Beta | 12 | 4,512 (192) | 40,608 | 0.0000076 yd | 0 |
| Classic Era | 97 | 28,432 (1,552) | 255,888 | 0.0000458 yd | 0 |
| MoP Classic | 12 | 4,512 (192) | 40,608 | 0 | 0 |

The heights are the corpus evidence: every shared edge point carries one height (within 1 mm), across chunk and tile
borders. Seam x/y agree bit for bit by construction (section 4), so the report lists them separately and does not
count them as evidence. The stored positions are within 1/768 yd of the formula, and float32 seams built from them
are never bit-equal, in every client. Nothing is smoothed or averaged.

The `coords.py` verdicts are all reproduced exactly (6.7): outer order against the 8 symmetries, inner vertex medians,
placement height on the exact fan surface, the hole bit order, alpha orientation (8 symmetries, both edges) and the Era
nibble order.

**Hole bit order.** The candidates are the 8 symmetries of the cell grid. The audit's ninth, whole-word MSB-first, is
the same as "rotate 180" (bit 63-(8r+c) = 8(7-r)+(7-c)). Four non-symmetric readings were added, and all lose both
tests:

| Reading | Discriminating cells inside a WMO box (Retail / Beta / Era / MoP) | Hole adjacencies across chunk edges |
|---|---|---|
| row*8+col, LSB first (parser) | 132 of 132 / 28 of 28 / 132 of 132 / 28 of 28 | 170 / 19 / 200 / 26 |
| u32 halves swapped | 96 / 4 / 92 / 4 | 105 / 6 / 126 / 10 |
| nibbles swapped | 112 / 24 / 108 / 24 | 95 / 13 / 116 / 16 |
| u64 read at +16 | 28 of 32 / 4 of 8 / 28 of 36 / 4 of 8 | 38 / 2 / 40 / 4 |
| u64 read at +24 | 68 of 100 / 0 of 20 / 64 of 96 / 0 of 20 | 45 / 4 / 60 / 6 |

### 6.5 Placements

| Client | MDDF records / unique ids (repeated) | MODF records / unique ids (repeated) | ids with differing copies |
|---|---|---|---|
| Retail | 32,848 / 31,667 (1,095) | 268 / 202 (42) | 0 |
| Classic Beta | 5,292 / 5,154 (132) | 41 / 25 (8) | 0 |
| Classic Era | 24,824 / 23,780 (968) | 178 / 128 (28) | 0 |
| MoP Classic | 6,565 / 6,411 (148) | 46 / 31 (11) | 0 |

Retail rotation, through the parser's `PlacementTransform`:
- **MODF:** 201 of 202 unique placements within 0.1 yd of their stored extents, 11 of 11 tilted. The misfit is uniqueId
  2848560 (WMO 112871): its root box (MOHD) is 14.05 yd off, the union of its group boxes (MOGI) 0.00099 yd. With the
  group-box union all 202 fit, so MODF extents are exact world bounds. The full 2,880-candidate sweep, recomputed in C#,
  ranks the same convention first, with identical counts and median error.
- **MLDX:** 25,621 of 25,648 doodads within 0.5 yd, 8,300 of 8,305 tilted. The 128-candidate sweep is identical to
  Python's. The rotation order is checked too: Rz Ry Rx (the parser's) fits 8,300 tilted doodads, Rz Rx Ry 7,915.
  MLDD = MDDF by uniqueId for all 32,848 records.

### 6.6 Alpha, liquids

- **Alpha maps decoded:**

  | Client | Maps | Storage |
  |---|---|---|
  | Retail | 32,848 | 31,571 compressed, 1,277 raw 8-bit |
  | Beta | 5,147 | 5,110 compressed, 37 raw 8-bit |
  | Era | 25,685 | all 4-bit |
  | MoP | 5,455 | 5,405 compressed, 50 raw 8-bit |

  No layer gap other than 2048 or 4096 occurs, no RLE run crosses 4096 (no run crosses a 64-texel row), no chunk mixes
  bit depths, and every map ends exactly where the next begins.
- **Era's last row and column:** all 25,672 non-uniform Era maps repeat row and column 63 (data, preserved).
- **MH2O:** every instance of all four windows is in bounds and no instance tables overlap (Retail 44 tiles / 3,109
  instances, Era 35 / 1,793, MoP 8 / 423, Beta 8 / 336). Measured on the raw bytes, not asserted as meaning:
  byte12 + byte14 <= 8 and byte13 + byte15 <= 8 for every instance, and float4 <= float8 for every instance.

### 6.7 Comparison with the Python readers

`compare_csharp.py` compares the C# JSON with the audit scripts' results.
- **Fresh baseline.** The Python scripts were re-run on the pinned bytes, and all 23 result files came out identical to
  the stored audit results. After the review's fix to the Python RLE reader (13.3), the three scripts that decode alpha
  were re-run again: all 16 outputs are byte-identical.
- **Result.** 2,551 comparisons covering 11,907 values, 0 mismatches (re-run on the final code).
- **Not vacuous.** Four planted mutations were all caught, including a 1e-6 change under a 1e-9 tolerance.

| Table / source | Compared | Mismatches |
|---|---|---|
| T2: WDT chunk orders, MPHD flag values, WDTs read (4 clients) | 12 | 0 |
| T3: MAID word non-zero / zero counts (8 words x 4 clients) | 32 | 0 |
| `wdt_flags.py`: MAIN vs MAID, MPHD bits, bit 0x1, word 7, WDT MODF (4 clients) | 24 | 0 |
| `maid_content.py`: MAID roles by content | 4 | 0 |
| T6 / `audit_adt.py`: per-tile chunk orders, sub-chunk sets, holes, header offsets, position fit, MH2O, MCAL verdicts, layer counts (2,364); the global stats: flags, MCLY flags, 0x100 by position, raw spacing, MCSH vs flag 0x1, ... (40) | 2,404 | 0 |
| `coords.py`: every verdict input (4 clients) | 29 | 0 |
| `alpha_edges.py` | 29 | 0 |
| `rotation.py`, `mldx.py` (Retail, full sweeps) | 17 | 0 |

The C# side never reads a Python result. Its window is found from each client's own WDT MAID, and every number comes
from the C# parse of the original files.

## 7. Malformed input

**Failure policy.**
- **Fatal for the part** (`WorldParseException` with file, chunk tag, absolute offset and reason):
  - the chunk walk;
  - a missing or short MVER;
  - an MCNK count other than 256 (root, tex0);
  - the placement records an obj file exists for: an MDDF/MODF/MLDD that is repeated, not a whole number of records,
    or holds a non-finite position, rotation or extent.
- **WDT:** fatal for a missing or wrong-size MPHD/MAIN/MAID/MODF.
- **Everything else is non-fatal.** A malformed MCNK, MH2O, MLDX or MDID/MHID, or a repeated optional chunk, gives a
  `WorldIssue` (the first copy is used), marks the structure Malformed, and the rest of the part is returned.
- **Unexpected.** Well-formed data never seen in the audit is kept and reported as Unexpected: MAIN values, a
  non-empty MCSE, flag 0x10000 clear, mixed alpha depths.
- **Context.** Every issue cites the structure it is about (MAIN entry, MPHD, MCNK, MCLY flags word, MH2O header entry,
  MLDX value), never offset 0 for a structure that has a position.
- **Empty reads.** An empty buffer for a named part is `NotSupplied` ("0 bytes supplied, an empty read"), not a
  malformed file.

`WorldParserTests` (147 checks):
- **Chunk walk:** truncated headers, sizes past the end and 0xFFFFFFFF sizes are reported with file, chunk tag and
  absolute offset. `TagAt` outside the buffer is a `WorldParseException`.
- **WDT:** wrong MAIN/MAID/MPHD/MODF sizes, missing or duplicate required chunks, empty and null input, unseen MAIN
  values, `TryGetTileFiles` outside 0..63 (false), FileDataID 0 and an ambiguous MWMO (refused, flagged).
- **ADT root:** MCNK count not 256, MCNK smaller than its header (holes Unknown), header index mismatch, MCVT of the
  wrong size, a sub-chunk running past its MCNK, a non-finite height, flag/MCCV disagreement, a rejected MCCV
  (Malformed), flag 0x10000 clear, non-empty and repeated MCSE.
- **MH2O:** shorter than its table, instance count 0xFFFFFFFF, instance past the end, an instance table inside the
  header table, overlapping instance tables (rejected before allocating), a repeated MH2O.
- **Alpha:** RLE truncation, a run crossing 4096, a copy past MCAL, zero-count runs (they terminate), gaps of 3000 bytes,
  maps not starting at 0, trailing MCAL bytes, flag 0x100 on the wrong layer, a texture index outside MDID, an MCLY of
  20 bytes, 5 layers (decoded), more than 8 layers (rejected), mixed 4/8-bit (flagged), a repeated MAMP, and the
  decoding helpers called with arguments outside the buffer (null with a reason).
- **Placements:** MDDF of 37 bytes, a non-finite position, MLDX not 28 bytes per record, a non-finite MLDX value (issue,
  MLDD kept), unflagged names without name tables, `WmoPlacement.Scale`.
- **Tile assembly:** a malformed obj0 next to a valid root, a missing tex0, an unnamed part, an empty buffer.
- **Fuzzing:** every prefix of a WDT and 600 random 8-byte corruptions of a root and a tex0 only ever raise
  `WorldParseException`. The review's fuzzing (2,353,744 parses, section 13) found no other exception type.

## 8. De-duplication

Records are never dropped. Each keeps its uniqueId and its source (tile, container, record index).
`DoodadPlacement.SamePlacementAs` / `WmoPlacement.SamePlacementAs` compare records bit for bit. In every window every
repeated uniqueId has identical copies, and no uniqueId repeats within one tile (section 6.5). A scene builder (W7/W8)
decides which copies are one instance: the evidence supports "same uniqueId within a map = same instance" for these
windows.

## 9. Performance and memory

Measured by `WorldCorpusValidation` on the final code (best of three passes, one thread, .NET 10 release build, files
already in memory). "Retained" is the GC heap growth while the results of all files are held
(`GC.GetTotalMemory(true)` before and after, each pass in its own method so no earlier result is still referenced).
It agrees with the review's independent measurement (Retail root 22.6 vs 22.9 MB, obj0 4.3 vs 4.35 MB).

| Client, part | Files | Input | Chunks + records | Parse (all files) | Throughput | Allocated per pass | Retained by results |
|---|---|---|---|---|---|---|---|
| Retail root | 56 | 19.5 MB | 71,909 | 5.5-17.3 ms (JIT warm-up, see below) | 1,125-3,520 MB/s | 33.9 MB | 22.6 MB (1.16x) |
| Retail tex0 | 56 | 67.9 MB | 61,745 | 164 ms | 414 MB/s | 149.9 MB | 139.9 MB (2.06x: decoded alpha) |
| Retail obj0 | 56 | 1.7 MB | 47,622 | 4.2 ms | 405 MB/s | 9.9 MB | 4.3 MB (2.5x) |
| Retail obj1 | 56 | 2.2 MB | 33,195 | 2.5 ms | 885 MB/s | 3.1 MB | 3.1 MB (1.37x) |
| Retail lod | 56 | 19.6 MB | 665 | 0.07 ms | (inventory) | 0.1 MB | 0.05 MB |
| Era root | 56 | 17.8 MB | 71,827 | 5.0 ms | 3,533 MB/s | 32.0 MB | 21.0 MB (1.18x) |
| Era tex0 (4-bit) | 56 | 57.2 MB | 54,581 | 38.9 ms | 1,472 MB/s | 124.2 MB | 113.8 MB (1.99x) |
| Beta tex0 | 9 | 10.1 MB | 9,791 | 23.5 ms | 429 MB/s | 23.5 MB | 21.9 MB (2.17x) |
| MoP tex0 | 9 | 11.5 MB | 10,100 | 24.1 ms | 478 MB/s | 25.9 MB | 24.0 MB (2.09x) |
| WDT Azeroth (Retail) | 1 | 295 KB | -- | 0.07 ms | -- | -- | 164 KB |

- **Timing caveat.** Retail root is the first part measured in the run, so it includes JIT warm-up: 5.5 to 17.3 ms
  across four runs on the same bytes since the float-read fix. Era's root, the same size, takes 5.0 ms. The machine
  was in normal use during the runs.
- **Per tile.** Per Retail tile that is about 0.1 ms root, 2.9 ms tex0 and 0.12 ms obj0+obj1. RLE decoding dominates
  tex0.
- **One optimisation.** The first version read each float through `BitConverter.GetBytes` (one allocation per float).
  Replacing that with `BitConverter.Int32BitsToSingle` cut root parsing from 24.8 ms to 5.5 ms and its allocations
  from 100 MB to 34 MB, with identical results (the comparison was re-run: 0 mismatches). No other optimisation was
  made: clear, checked parsing first.
- **Bounded allocation.** MH2O instance tables may not overlap, and at most 8 layers per chunk are decoded. Without
  those checks the review built a 1.36 MB root that allocated 268 MB, and a 4.5 MB one that ran a 1 GB process out of
  memory (section 13).

**Ownership.** Every result owns its arrays. No result references the caller's buffer, so the caller may reuse it
immediately (verified on 603 real files and 42 assembled tiles, section 13). The results are plain objects, with no
unmanaged memory and no pointers. The dominant retained cost is tex0's decoded alpha (about 2.5 MB per Retail tile). A
streaming viewer that keeps many tiles should upload the maps to GPU arrays and drop the parse result, or keep
compressed bytes and decode on demand (W5/W6). Located but undecoded data (MH2O vertex data, bitmaps and attributes;
chunks listed as Unsupported) is kept only as offsets into the input, so a caller that will decode it later must keep
that buffer.

## 10. Per-build support matrix

| Structure | Retail 12.1.0.69933 | Classic Beta 1.60.1.70338 | Classic Era 1.15.9.70003 | MoP Classic 5.5.4.70268 |
|---|---|---|---|---|
| WDT tiled (MAID) | decoded (728) | decoded (48) | decoded (37) | decoded (194) |
| WDT global WMO, modern (FileDataID) | decoded (35) | decoded (21) | decoded (21, DireMaul yaw 180) | decoded (33) |
| WDT global WMO, old (MWMO path) | decoded (193) | absent | absent | absent |
| MAI2 / MANM | unsupported (listed) | MAI2 listed | MAI2 listed | absent |
| root MCNK, MCVT, holes, area | decoded | decoded | decoded | decoded |
| MCNR / MCCV | raw / raw (2,412 chunks) | raw / raw (all chunks) | raw / absent | raw / raw (299) |
| MCBB / MPTX | unsupported (9) / absent | absent / unsupported (5) | absent | absent |
| MH2O | partial (44 tiles) | partial (8) | partial (35) | partial (8) |
| MFBO, blend meshes | unsupported | absent | absent | MFBO unsupported |
| tex0 MCLY / MCAL | decoded (RLE + raw 8-bit) | decoded (RLE + raw 8-bit; up to 5 layers) | decoded (4-bit raw) | decoded (RLE + raw 8-bit) |
| MCSH | absent | absent | raw (6,976 chunks) | raw (1,627) |
| MTXP | raw (1 tile) | absent | absent | raw (1 tile) |
| obj0 MDDF / MODF | decoded | decoded | decoded | decoded (MLMB unsupported) |
| obj1 MLDD / MLDX | decoded | decoded | decoded | decoded (MLDB, MLMB unsupported) |
| `_lod.adt` | inventory only | inventory only (MASD, MALP) | not named | not named |
| `_tex1.adt` | absent in all four (audit 5.1) | | | |

## 11. Known unknowns (not implemented, by design)

- **Flag meanings:**
  - MPHD flag bits other than 0x1 (U1);
  - MAIN flag 2 (U3);
  - MCNK flag bits other than 0x1/0x40/0x10000 (U7);
  - MCLY bits other than 0x100/0x200, and MCLY word 12 (U10);
  - MDDF/MODF flag bits beyond 0x40/0x8 (U13); the MODF scale for flag 0x4 clear (INFERRED 1.0).
- **Holes:** the low-resolution hole layout; the meaning of +20..27 when flag 0x10000 is clear (never seen in the
  audited corpus; stage 03 met it on a real tile: every chunk of Retail map 1051 tile 31_29 has flag 0x10000 clear,
  five with a non-zero low-resolution mask -- `03-terrain-geometry.md`).
- **Raw per-chunk data:**
  - MCNR decoding (U9);
  - MCCV channel order;
  - MCSH bit layout.
- **Textures:**
  - the 4-bit to 8-bit alpha expansion (U19);
  - MAMP (U12);
  - MTXP/MHID height blending (U11).
- **Placements:**
  - MODF words 58/60, the doodad sets (U15);
  - the 7th MLDX value;
  - MCRD/MCRW meaning.
- **Liquids:** MH2O fields beyond its boundaries, its vertex data, bitmaps and attributes (U14).
- **Other chunks:**
  - obj1 chunks beyond MLDD/MLDX and every `_lod.adt` chunk (U17);
  - MFBO, blend meshes, MCBB, MPTX, MLMB, MLDB, MASD, MALP (U18);
  - MAI2, MANM (U4, U5).
- **Evidence limits:**
  - map-edge tiles are covered by the formula only (no edge-tile ADT is pinned);
  - Era/MoP MAID words 4-6 rest on listfile names;
  - MPHD side files are content-checked on Azeroth only.
- **Not in this branch:** the WDL (proven in the audit, 4.4) is not parsed. W10 adds it.

## 12. Regression

- **Diff against `develop`:**
  - New files under `Tools/UnityRendererProject/Assets/Scripts/Wow/World/`, `Tools/UnityRendererProject/Tests/`
    (tests, runner, README), `Tools/world-audit/compare_csharp.py` and this report.
  - Existing files changed:
    - two README paragraphs;
    - `docs/world-viewer/00-audit.md` (evidence corrections, 13.3);
    - the audit's Python reader: `wowfmt/adt.py` (RLE copy bound), `coords.py` (docstring) and
      `tests/test_wowfmt.py` (one test).
  - Not changed: any file under `Source/`, any CMake file, any IPC file, and any existing C# file, including
    `WowParserTests.cs`.
- **Existing parser tests:** `WowParserTests` passes, 792 checks, 0 failures, in the console runner and inside the
  Unity Editor.
- **Unity Editor:** the final sources compile with 0 errors. The whole `Assets/Scripts` folder is compiled, existing
  files and new. The only warnings are 2 pre-existing deprecation warnings in `WmvMain.cs`.
- **Runtime messages and visuals:** unchanged. Nothing calls the new code.
- **Audit tools:** the Python suite passes with the corpus (28 of 28), and the re-run audit outputs are unchanged
  (6.7).

## 13. Adversarial review

### 13.1 Method

The parsers were stated as ten claims, in five groups (WDT, coordinates and holes, alpha, placements and clients,
memory and ownership). For each group, a reviewer tried to break the claims with probes of its own. The probes use their
own chunk walkers and raw byte reads: neither `WorldChunkWalker`/`WorldSpan` nor the audit's `wowfmt`. The inputs were
the pinned corpus, crafted files and large-scale fuzzing. A separate verifier then re-derived every finding from
scratch, labelling it REAL, PARTLY or FALSE. In all, 48 agents ran. The verifiers labelled 43 findings: 29 REAL,
14 PARTLY, 0 FALSE. Probes and their outputs are kept under `build/review/` (git-ignored).

### 13.2 Claims

**1. MAIN/MAID indexing: entry i = (i % 64, i / 64); MAIN bit 0x1 <=> MAID names files; the eight words in role
order. WEAKENED.**
- *Input:* all 1,323 FileDataID-named WDTs; the 130 window roots reached through `TryGetTileFiles`; the files behind
  every returned word; crafted edge tiles and MAIN values.
- *Expected:* each tile's words equal raw entry y*64+x; the MCNK positions put each root at (x, y); each word's file
  has its role's content.
- *C#:* 59,907 tiles identical to the raw bytes, word for word; MCNK positions give (x, y) on 130/130 tiles and
  (y, x) on 0; words 0-4 are root/obj0/obj1/tex0/lod on every Retail/Beta tile.
- *Baseline:* a swapped reader gives a different tile set in 999 of 1,007 tiled WDTs, so the test discriminates. The
  decoded word 5 and 7 images separate terrain-only from the minimap.
- *Weakened by:* the rule the data prove is "value 1", while the parser used bit 0x1 silently on unseen values. The
  evidence comment overstated Era/MoP words 4-6, and `TryGetTileFiles` threw outside 0..63.

**2. WDT FileDataIDs: MPHD words 1-6 name the side files; MODF names a FileDataID when flag 0x8 is set, else an
MWMO entry. WEAKENED.**
- *Input:* every pinned side file; MODF targets against their own WMO root boxes; crafted MODF/MWMO cases.
- *Expected:* each side file's content matches its word; extents equal the named WMO's MOHD box in the file frame.
- *C#:* header words equal the raw words on all 1,323 WDTs; all 110 flag-0x8 records and all 193 MWMO records
  resolve.
- *Baseline:* both bounding boxes match exactly; every MWMO holds one name at byte 0.
- *Weakened by:* only Azeroth's side files are pinned, and several are 12-byte MVER-only files, so "chunk ids confirm
  each word" overstated it. FileDataID 0 and an MWMO name not at byte 0 were accepted silently.

**3. MCNK coordinates: (4096 - R)U from integer indices; heights = stored z + MCVT; outer 17r+c, inner 17r+9+c.
SURVIVED.**
- *Input:* all 33,280 window MCNKs; 16,389 values of `XOfRow`; every seam point; alternative layouts.
- *Expected:* stored positions equal the formula up to float32 error; seam heights equal; inner vertices close to
  their corner mean.
- *C#:* max |stored - origin| 1.302e-3 yd; `XOfRow` within 2.4e-12 yd of the exact rational; 0 header or height
  mismatches against raw reads.
- *Baseline:* exact rational arithmetic gives every deviation as 0, 1/3072, 1/1536 or 1/768 yd. So the documented
  "0.0013" bound was slightly low: it is now 1/768, which is reached. Heights without stored z differ at 254,448 of
  255,888 points; a transposed MCVT at 255,027. Inner medians 0.119 / 2.64 / 1.10 yd (identity / transposed /
  shifted).
- *Notes:* the seam x/y equality is a property of the formula, not evidence (now reported so). Edge tiles are checked
  by formula only.

**4. Hole bits: with flag 0x10000, bit = row*8 + col at +20, LSB first. SURVIVED.**
- *Input:* all 33,280 chunks; 302,558 triangles of the real WMOs over the hole cells (33.4 M samples in Retail);
  crafted masks straddling the two u32 halves.
- *Expected:* hole cells hold WMO geometry; holes continue across chunk edges; +16/+24 readings fail.
- *C#:* 0 mismatches against raw bit arithmetic over all 64 cells of every chunk; crafted bits 7/31/32/56 map to the
  right cells.
- *Baseline:* identity hole cells contain WMO geometry in 214/214 (Retail), 88/88, 60/60, 16/16. Every other order
  48-83% (Retail).
- *Notes:* MSB-first is rotate 180, so there are 8 distinct orders, not 9. Non-symmetric readings were added (6.4).
  Flag-clear chunks are never seen and are now flagged.

**5. 4-bit alpha: low nibble first, texel (r, c) = sample[r*64 + c], depth from the layer gap, no expansion.
SURVIVED.**
- *Input:* all 25,685 Era 4-bit maps; 23,786 Retail maps of the same terrain and texture; chunk-edge continuity.
- *Expected:* low-first is continuous across edges and matches Retail's own 8-bit re-encoding.
- *C#:* all 25,685 maps equal an independent low-first decode.
- *Baseline:* Retail >> 4 equals the low-first nibble at 86.5% of texels, the high-first one at 64.2%. A transposed
  layout steps 4.8 across edges against 0.95.
- *Notes:* mixed bit depths in one chunk were accepted silently (now flagged).

**6. RLE: count byte, fill/copy by the top bit, exactly 4096 values, ending at the next layer. SURVIVED.**
- *Input:* 43,470 real RLE maps (Retail, MoP, Beta); 23.3 M runs; alternative readings; crafted edge cases.
- *Expected:* every stream ends exactly at the next offset with 4096 values.
- *C#:* all 43,470 identical to an independent decoder; crafted crossing, short and padded streams rejected with
  their reason.
- *Baseline:* 0 crossing runs; no run crosses a 64-texel row. A flipped top bit fails on 100% of maps; count+1 and
  PackBits fail on all but one.
- *Notes:* the audit's Python reader let a copy run read past MCAL (fixed, 13.3).

**7. Placements: record layouts, scale u16/1024, file frame, Rz(a1+180) Ry(a0) Rx(a2), extents and MLDX boxes.
WEAKENED.**
- *Input:* every MDDF/MODF/MLDD/MLDX record of four clients; 202 unique WMOs with their roots; 25,648 doodads with
  their M2 boxes.
- *Expected:* raw fields equal; the transform puts the model boxes on the stored extents.
- *C#:* 0 field mismatches; `PlacementTransform` equals an independent closed form within 1.8e-12; 201/202 and
  25,621/25,648.
- *Baseline:* a1+90 fits 22, Rz(-a1) 41, swapped axes 190 (tilted 0/11). u16/1000 fits 365 doodads at 0.01 yd,
  u16/1024 fits 23,873. The MOGI union fits 202/202.
- *Weakened by:* "MLDD repeats MDDF record for record" is true only by uniqueId (Retail 29_48 and 30_48 are
  permutations). A NaN in the optional MLDX discarded the whole obj1. The C# MLDX sweep never varied the rotation order
  (now checked, 6.5).

**8. Memory bounds: no read outside the buffer or chunk; impossible sizes and counts rejected; malformed optional data
does not fail the part. REFUTED.**
- *Input:* 2,353,744 mutation-fuzzed parses of 70 real seed files; crafted MH2O overflows; containment differentials;
  hostile helper arguments.
- *Expected:* a result or a `WorldParseException` with correct context; bounded allocation; optional data never fatal.
- *C#:* 0 other exception types and 0 wrong contexts; 1,485 containment checks unchanged; all wrapping MH2O entries
  rejected.
- *Baseline:* the reviewer's own walker agrees on every accepted file.
- *Refuted by:*
  - malformed optional data failed the whole part (an MLDX NaN, an MDID of 4n+1 bytes, a repeated MAMP/MDID/MHID/
    MLDX/MH2O/MWMO);
  - overlapping MH2O instance tables amplified allocation 198-236x, and a 4.5 MB root ran a 1 GB process out of memory;
  - the public helpers threw on hostile arguments, and `DecodeRaw` with a negative offset read before MCAL;
  - several issues cited offset 0.

**9. One parser for all four clients; every part of every window parses; unsupported structures are listed, not
dropped. WEAKENED.**
- *Input:* all five parts of every window tile; decoded content against raw bytes; crafted MCSE and MCCV.
- *Expected:* every chunk decoded or listed; content bit-identical.
- *C#:* 0 unaccounted top-level chunks or sub-chunks; 0 content mismatches (heights, MCNR, MCCV, MCSH, MH2O, alpha).
- *Baseline:* the reviewer's census agrees with audit 5.1 and T6.
- *Weakened by:* a non-empty or repeated MCSE vanished without a trace, and a rejected MCNR/MCCV left the chunk marked
  Decoded.

**10. Ownership: no result references the caller's buffer; results own their arrays. SURVIVED.**
- *Input:* 603 real files and 42 assembled tiles, parsed and then overwritten; a reflection snapshot of every
  reachable field.
- *Expected:* snapshots and query output unchanged; no shared arrays.
- *C#:* 0 changes, 0 input references, 0 shared non-empty arrays.
- *Baseline:* allocation ratios match the branch's own to 0.01.
- *Notes:* `TryGetEightBit` returns the map's own array (now documented). The documented retention was an estimate
  that ran low (now measured, section 9). Undecoded data needs the input buffer (now documented, sections 9 and 14).

### 13.3 Fixes

Every finding was fixed or documented; none was waived. Tests for each are in `WorldParserTests`' "Adversarial review
findings" group (29 checks) and in the corpus verdicts.

| Area | Finding | Fix |
|---|---|---|
| Failure policy | Malformed optional data (MLDX NaN, MDID/MHID size, repeated MAMP/MTXP/MDID/MHID/MLDX/MH2O/MWMO) failed the whole part | Non-fatal issue; the first copy is used; MLDX boxes null on a bad value, MLDD kept |
| Allocation | Overlapping MH2O instance tables; unbounded MCLY layer count | MH2O tables must start after the header table, lie inside MH2O and not overlap, checked before allocating; at most 8 layers decoded (Beta has 5) |
| Helpers | `DecodeRaw`, `DecodeRle`, `DecodeAlpha`, `TagAt` threw or read outside MCAL on hostile arguments | Argument validation: null with a reason, or `WorldParseException` |
| Context | Issues at offset 0 (MAIN/MAID mismatch, MPHD, MODF, MCLY flag 0x100, "no MCAL", MCNK count) | Each cites its structure: the MAIN entry, MPHD, MODF, the layer's flags word, the MCNK |
| Silent data | Non-empty/repeated MCSE dropped; rejected MCNR/MCCV left Decoded; unflagged placement names | MCSE listed as Unsupported with an issue; the chunk is marked Malformed; issue for unresolvable names |
| Unseen data | MAIN values other than 0/1/2, word 1; flag 0x10000 clear; mixed alpha depths | Unexpected issues; the hole mask has a new Unknown kind for unread headers |
| References | `TryGetTileFiles` threw out of range; FileDataID 0 and an MWMO name not at byte 0 accepted | Returns false; both refused and flagged |
| API | `WmoPlacement` had no scale; `TileExists` read as availability; `TryGetEightBit` aliasing; `XOfRow` unchecked | `Scale` accessor; documented |
| Empty read | An empty buffer was reported as a malformed file | `NotSupplied`, "an empty read" |
| Validation | Seam positions counted as evidence; MSB-first counted as a ninth order; MLDX order not varied; MAIN check by value vs parser by bit; retention estimated | Heights only as evidence; four non-symmetric hole readings; Rz Rx Ry checked; one predicate; measured retention, each pass in its own method |
| Python reader | `rle_alpha` copy runs could read past MCAL into the next chunk | Bounded (returns None), with a test; the three alpha scripts re-run: outputs byte-identical |
| Audit text | 0.0013 yd bound; nine hole orders; MAID/MPHD evidence; "up to 4 layers"; MLDD order | Corrected in `00-audit.md`: 1/768 yd, 8 distinct orders, evidence per word and client, 5 layers in 11 Beta chunks, matched by uniqueId |

## 14. Future runtime integration contract

1. **WDT.** The host resolves a map's WDT (field 21 FileDataID in Retail/Beta, the directory path in Era/MoP) and
   supplies its bytes. `WdtParser.Parse` gives the kind, the tiles and each tile's eight MAID FileDataIDs.
2. **Tile files.** The player requests each named part by FileDataID through the existing
   `getAssetByFileDataID` (no new message is needed to fetch bytes; audit H7). A part the client lacks (`NOT_IN_ROOT`
   / `NOT_LOCAL`, audit 3.3) is passed as null or empty and becomes `NotSupplied`. A part MAID does not name is
   `NotNamed`. Neither is an error.
3. **Parse.** `AdtParser.ParseTile` gives independent per-part results. The parsers are static with no shared state,
   so they can run on worker threads (the WMO path already parses on the ThreadPool). Results own their arrays, so the
   byte buffers can be returned to a pool at once, unless MH2O vertex data or an Unsupported chunk will be decoded
   later: those are offsets into the buffer.
4. **Geometry.** Vertex x/y come from `TerrainCoordinates`, z from stored z + MCVT, triangles four per cell fanned from
   the inner vertex, holes from `TerrainHoles`. All in the game frame, in double. The Unity conversion and the floating
   origin are W4's, behind gate G4.
5. **Materials.** 8-bit alpha maps are ready (read-only). 4-bit maps need the U19 decision (W5) before any 8-bit value
   is produced.
6. **Placements.** `PlacementTransform` gives world boxes and points. MODF extents and MLDX boxes give culling bounds
   directly; pair MLDX with MLDD by index and MLDD with MDDF by uniqueId. The scene builder de-duplicates by uniqueId.

## 15. Status

Branch `world/01-parsers`, base `develop` `2f35172d`. Delivered as a local commit (push status at delivery: NOT
PUSHED), then pushed for review on request as a pull request against `develop`. The next world-viewer branch waits
for that review.
