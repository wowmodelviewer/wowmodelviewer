# World viewer 09 -- liquids

Branch `world/09-liquids`, from `develop` 3e11ee52 (Stage 08, PRs #125 and #126). This document is written in the order
the work was done: the investigation and the architecture first (section 1, written before any code), then the
implementation, its tests and measurements.

Evidence grades: **PROVEN_BYTES** (decided by the client's own files), **PROVEN_RUNTIME** (observed in the running viewer
or player on real client data), **PROVEN_SOURCE** (read in this repository's source), **INFERRED** (follows from proven
facts, or taken from public documentation and consistent with the bytes, but not observed directly), **UNKNOWN**. Test
outcomes: **PASS**, **FAIL**, **SKIP** (not run, said why), **UNSUPPORTED** (cannot be run on this data or by this
design, said why).

**Outside sources.** The audit's risk R4 allowed table definitions from outside the repository only with the owner's
consent. The owner gave it on 2026-10-18 for this stage, as hypotheses: public table definitions and public format
documentation were read, and nothing from them is relied on until the clients' bytes confirm it. Where a meaning comes
only from that documentation and the bytes cannot decide it, it is graded INFERRED and said so.

Clients (one installation, as updated on 2026-10-18):

| Client | Product | Build |
|---|---|---|
| Retail | wow | 12.1.5.70448 |
| Classic Era | wow_classic_era | 1.15.9.70003 |
| MoP Classic | wow_classic | 5.5.4.70449 |
| Classic Beta | wow_classic_beta | 1.60.1.70450 |

The investigation's scripts and outputs are in `D:\wmv-09\inv\` (not in the repository): `mh2o-bytes` (every root ADT of
every client), `mliq-bytes` (every named WMO's LOD0 groups), `liquid-tables` (the five liquid tables, schema-free),
`host-cpp` and `unity-code` (code reading).

## 1. Architecture (proposed before implementation)

### 1.1 Starting point

The roadmap's row 09: "MH2O on terrain, MLIQ in world models; their tables' layouts per client", entry "U14 settled
(definitions, byte checks)", exit "water tiles against references on Retail and Era" (`roadmap.md` 5; the audit names
the tiles: Retail 29_48 Stormwind Harbor, Era 36_49 Lake Everstill, `00-audit.md` 15).

What exists (PROVEN_SOURCE, develop 3e11ee52):

- **Player, MH2O.** `AdtParser.ParseLiquids` (`Wow/World/AdtParser.cs:246-318`) already reads the 256-entry header table
  and the 24-byte instance records of every root it parses, raw and unnamed (`AdtModel.cs:49-84`: "unverified, U14").
  It never reads attributes, exists bitmaps or vertex data, and nothing consumes the result.
- **Player, MLIQ.** `WmoParser.ParseGroup` lists MLIQ among a group's sub-chunks and keeps the group's bytes
  (`WmoModel.cs:473, 530-539`); the MOGP header is kept whole, `GroupLiquid` at +0x34 (`WmoParser.cs:502`), flag 0x1000
  "iff MLIQ" (`WmoModel.cs:366`). Nothing decodes MLIQ.
- **Player, no DB2.** The player has no DB2 reader (the roadmap's rule: the host reads tables, the player parses map
  formats).
- **Host.** No liquid table is in `database.xml`, and no `.dbd` describes one. The legacy OpenGL liquid code was deleted
  with the dead map-tile code (`cc1e14c5`); it was never drawn and is no evidence of the game's behaviour.
- **Rendering.** No player shader animates over time, and the world-model shader has no real transparency
  (`WmvWmo.shader`; blend >= 2 is drawn opaque). The closest translucent pass is the particle shader.

### 1.2 MH2O, from the bytes

Census: every root ADT of the maps the Stage 02 catalogue lists, read on 2026-10-18 (Retail 44,018 roots, 25,202 with
MH2O, 4,596,540 instances; Era 2,719 / 1,603 / 243,283; MoP 9,848 / 6,956 / 1,209,447; Beta 3,322 / 2,047 / 285,312). No
malformed MH2O was found in 59,907 roots.

| What | Rule | Grade |
|---|---|---|
| Position | the third top-level chunk (`MVER MHDR MH2O MCNK...`); MHDR data +40 holds its offset (64), 0 without MH2O | PROVEN_BYTES |
| Header table | 256 x 12 bytes; entry n belongs to MCNK n (x = n % 16, y = n / 16): u32 instance offset, u32 layer count, u32 attribute offset (0 = none); an empty entry is all zero | PROVEN_BYTES |
| Instances | 24 bytes each, contiguous from offset 3,072 in chunk order; layer counts 1-4 (Retail 11,186 multi-layer chunks) | PROVEN_BYTES |
| Instance fields | +0 u16 liquid type (a LiquidType id in 100%); +2 u16 "object or format"; +4 f32 min height; +8 f32 max height (min <= max); +12 u8 x, +13 u8 y, +14 u8 width, +15 u8 height (x + w <= 8, y + h <= 8, w, h >= 1 in 100%); +16 u32 exists-bitmap offset; +20 u32 vertex-data offset (0 = none) | PROVEN_BYTES |
| Heights | absolute world heights (the same frame as the terrain's z); an instance without vertex data is flat at min (= max in 100%) | PROVEN_BYTES |
| Data region | in chunk order: attributes (16 bytes), then per layer its bitmap, then its vertex data; packed with no gaps and no alignment (unaligned reads) | PROVEN_BYTES |
| Exists bitmap | ceil(w*h/8) bytes; quad (row r, column c) of the instance is bit r*w + c, LSB first; bits past w*h must be masked (132 Retail bitmaps set them); no bitmap = every quad exists | PROVEN_BYTES (the last INFERRED, strong: 99.87-99.97% of such quads lie above the terrain) |
| Attributes | two u64 masks, cell (r, c) = bit r*8 + c; the second is a subset of the first. Called "fishable" and "deep" in public documentation | PROVEN_BYTES (layout); INFERRED (names) |
| Vertex data | n = (w+1)(h+1) vertices, row-major, row i on terrain row y+i, column j on column x+j. Four layouts, by format code: 0 = f32 height[n] then u8 depth[n] (5 B/vertex); 1 = f32 height[n] then (u16 u, u16 v)[n] (8); 2 = u8 depth[n] (1); 3 = f32 height[n], (u16, u16)[n], u8 depth[n] (9). Heights at vertices that no existing quad touches are leftovers | PROVEN_BYTES (layout and order: seam tests, min/max agreement in 100%) |
| Format code | if the second word is 0, 1 or 2, it is the code; else if the liquid type is 2 ("Ocean") the code is 2 (or no data); else LiquidMaterial[LiquidType[type].MaterialID].LVF. Matches 6,334,582 of 6,334,582 instances | PROVEN_BYTES |
| Second word >= 43 | a LiquidObject id (copy-table rows included); 42 is never a LiquidObject id (the ocean's word) | PROVEN_BYTES |
| Seams | same-type liquid matches at 99.6-100% of chunk and tile seams (within 0.01 yd); heights are not bit-identical across tiles | PROVEN_BYTES |
| Exit tiles | Retail 29_48: 229 chunks, all type 2 "Ocean", flat at 0.0 (136 depth-only, 93 without data). Era 36_49: 132 chunks, all type 5 "Slow Water", format 0, flat at 56.1615, 50 bitmaps | PROVEN_BYTES |

What the bytes cannot decide: why type 2 is depth-only when other ocean-class types (Retail 947, 1100, 1142) carry
heights (UNKNOWN; public documentation describes the same special case); the depth byte's scale (ratios cluster at 9.0
and 1.7 bytes per yard: lakes and oceans; INFERRED); the u16 uv's scale (about 42.5 per quad, wrapping; UNKNOWN). Partial
instances (anything but 0, 0, 8, 8) exist only in Retail (1,993 instances in 114 roots). The audit's "second word 42 or
larger" (`00-audit.md` 5.7, T7) is wrong on Retail (51,791 instances use 0, 1 or 2), and its Era puzzle ("5 records,
27 values") is the LiquidObject copy table (5 records + 899 copies).

### 1.3 MLIQ, from the bytes

Census: every WMO root the listfile names and all their LOD0 groups (Retail 36,215 groups, 1,400 with MLIQ; Era 5,784 /
310; MoP 15,251 / 629; Beta 8,837 / 462).

| What | Rule | Grade |
|---|---|---|
| Presence | MOGP flag 0x1000 <=> exactly one MLIQ sub-chunk, in every group of every client; the root's MOGI cannot tell (0x1000 is never set there); LOD1-3 group files carry no liquid | PROVEN_BYTES |
| Header (30 bytes) | +0 i32 X = vertices along local x, +4 i32 Y, +8 i32 A = tiles along x (= X-1), +12 i32 B (= Y-1), +16 f32 x 3 corner, +28 u16 material index (into MOMT; 0xFFFF twice) | PROVEN_BYTES (size = 30 + 8XY + AB in 2,801 of 2,801) |
| Grid | vertex (i, j) = corner + (i S, j S, height) in the group's own frame (the MOVT frame), S = 100/24 = 4.1667; x varies fastest (vertex j X + i, tile j A + i); tile (i, j) uses vertices (i, j), (i+1, j), (i, j+1), (i+1, j+1) | PROVEN_BYTES (98% of depth bytes match the group's own floor below under this reading; the transposed reading 34%) |
| Vertex (8 bytes) | +4 f32 height; +0..3: for water and ocean a depth byte then three zero bytes; for magma and slime two i16 (s, t) smooth over the grid | PROVEN_BYTES (that s, t are texture coordinates: INFERRED; their scale UNKNOWN) |
| Tile byte | low nibble 0xF = no liquid in this tile (vertices of absent tiles are zero); otherwise one nibble value per MLIQ; bits 0x40 and 0x80 vary, meaning UNKNOWN (public documentation: "fishable", "shared") | PROVEN_BYTES (emptiness); UNKNOWN (high bits) |
| Liquid type, new files | root MOHD flag 0x4 set: MOGP +0x34 is a LiquidType id (Retail 1,324 of 1,326 valid; misses are ids that exist only in another client's table) | PROVEN_BYTES |
| Liquid type, old files | MOHD 0x4 clear (297 of 310 Era liquid groups; 74 Retail): MOGP +0x34 is the constant 15 on every group, liquid or not; the tile nibble carries the identity: nibble & 3 is the kind (0 water, 2 magma, 3 slime), and LiquidType id = nibble + 1 in 196 of 210 groups re-authored in the newer clients | PROVEN_BYTES (kind); INFERRED (the exact row) |
| Bounds | the liquid lies outside the MOGP box in 22 groups; culling must use the liquid's own extent | PROVEN_BYTES |
| Exit tiles | neither has WMO liquid (Retail 29_48's and Era 36_49's placed WMOs have no 0x1000 group) | PROVEN_BYTES |
| Examples | Era 29_48 Stormwind (uid 10047, root 107243): 22 indoor canal groups, old format, nibble 4; Beta Stormwind 7962574 group 7964878 "Canals" (outdoor, type 1344); Blackrock 108238 lava (33-34 x 45-47); md_mushroomcave 111185 (35-36 x 46-48); Dire Maul (map 429) group 109369 | PROVEN_BYTES |

Public documentation gives a rule for the group's liquid type that remaps the basic ids (below 21) to the "WMO" rows
(13 WMO Water, 14 WMO Ocean, 19 WMO Magma, 20 WMO Slime) by kind, and uses the tile nibble for old files. It agrees with
the bytes on the kind in every case the bytes can test; which exact row the client uses is not decided by the bytes
(the candidate rows share their first texture in all four clients). This stage uses the documented rule, graded
INFERRED.

### 1.4 The liquid tables, from the bytes

Fresh copies (2026-10-18; Retail and Beta changed since the audit's T10, layout hashes unchanged): LiquidType D1ECEEC9
(Retail 199 rows + 2 copies, Era 19, MoP 61, Beta 57), LiquidMaterial 98E5D7AA (5 + 6 copies / 2 / 4 + 3 / 4 + 1),
LiquidObject CB0D39E8 (315 + 7,783 / 5 + 899 / 43 + 1,531 / 57 + 1,213), LiquidTypeXTexture 7BEECC7F (4,289 / **0** /
1,247 / 1,139). Each hash is the same in all four clients. The public table definitions list exactly these hashes for
these four builds, and every column the bytes can test agrees with them:

| Column | Bytes say | Grade |
|---|---|---|
| LiquidType field 0, Name | a string ("Water", "Ocean", "Magma", "Slime", "Slow Water", ...) | PROVEN_BYTES |
| LiquidType field 1, Texture[6] | six path templates (`XTextures\river\lake_a.%d.blp`, `proceduralRiverDepthTex`, `XTextures\ocean\newoceanbump.blp`, ...) | PROVEN_BYTES |
| LiquidType field 3, SoundBank | 2 bits: 0 on Water, 1 Ocean, 2 Magma, 3 Slime and on every variant of each; the vertex patterns of MLIQ follow it | PROVEN_BYTES (the kind); the name is the public one |
| LiquidType field 14, MaterialID | every value is a LiquidMaterial id | PROVEN_BYTES |
| LiquidType field 15, MinimapStaticCol | a colour: water 0x9F12A0D5, magma 0xFFD06800, slime 0xFF4ABA3C, green lava 0x9F68D000 | INFERRED (a colour; its use as the minimap's) |
| LiquidType field 16, FrameCountTexture[6] | the low byte of each element is the frame count of that texture slot (30 for lake_a, 16 for fast_a); the pallet's upper 24 bits are a constant that is not part of the value | PROVEN_BYTES |
| LiquidType field 18, Float[38] | floats; public documentation names the first two "texture tiles per block" and "rotation" for water and "animation x/y" for magma | INFERRED (names) |
| LiquidMaterial field 0, Flags | 1 on water and fel, 0 on magma and slime, 3 on fog; public documentation: 0x1 "is transparent", 0x2 "is visual only" | INFERRED (meaning) |
| LiquidMaterial field 1, LVF | the vertex-format code (0, 1, 3 here; 2 is never a material's) | PROVEN_BYTES |
| LiquidObject field 2, LiquidTypeID | 16 bits, every value a LiquidType id; equals the MH2O instance's own type in all but 186 Retail instances, where the instance's type decides the format | PROVEN_BYTES |
| LiquidTypeXTexture | relationship = LiquidType id; field 0 = texture FileDataID (0 on procedural rows); field 1 = order within the type (0..n-1); field 2 = -1, or the procedural texture (0 ocean depth, 1 river depth, 2 WMO water) | PROVEN_BYTES (procedural meaning: INFERRED, matching the type's own strings) |
| LiquidTypeXTexture vs Texture[6] | the rows are the six templates expanded: a `%d` template gives frames 1..FrameCountTexture[k] in that order, a plain path one row, an empty slot none (MoP 61 of 61 types, Beta 57 of 57; Retail 194 of 199, the five swamp types omit 30 frames of slot 5) | PROVEN_BYTES |
| Era | no LiquidTypeXTexture rows: textures come from the templates, by path (all 177 expanded names resolve in Era's root, e.g. `lake_a.1.blp` to `lake_a.30.blp`; `lake_a.31.blp` does not exist; an extension-less name needs `.blp`) | PROVEN_RUNTIME (by-name probes) |

Other columns stay UNKNOWN and unused. Two reader traps (PROVEN_BYTES): the file records no width for a compressed
column, and pallet words carry constant upper bits that are not part of the value (LiquidObject field 2,
FrameCountTexture, LiquidType field 10), so every read must be masked to the column's width.

### 1.5 Rendering: what is established, and the approximation

What the textures are (PROVEN_BYTES, decoded): the water and ocean frames (`lake_a.N`, `ocean_h.N`, 256 x 256) are
near-black in colour (RGB at most 40) and carry a ripple-and-sparkle pattern in their alpha channel (median 51 of 255).
The magma frames and `Magma0` are fully coloured (orange). So the game's water colour does not come from its texture.
Public documentation puts the water and ocean colours ("close" and "far") and their shallow/deep alpha in the zone's
Light tables; reading those is Stage 12's work (light, sky and fog). Nothing else about the game's liquid shading is
established by the bytes.

The approximation this stage implements, every part a documented choice, none claimed as the game's:

| Part | Choice | Basis |
|---|---|---|
| Category | the type's SoundBank kind: water, ocean, magma, slime | PROVEN_BYTES (the kind) |
| Transparency | the material's flag 0x1: transparent (alpha-blended, no depth write, drawn after opaque geometry) or opaque | INFERRED (flag meaning) |
| Base colour | the type's MinimapStaticCol (water blue, magma orange, slime green), standing in for the zone's Light colours until Stage 12 | a documented stand-in: data per type, not one colour for all |
| Texture | slot 0's animated frames (a Texture2DArray, one per liquid type and scene); slots 1-5 (procedural depth, bump, foam, `Magma0..2`) are not drawn: their roles are UNKNOWN | PROVEN_BYTES (frame lists); the slot roles UNKNOWN |
| Water and ocean | colour = base colour + the frame's alpha as a highlight; opacity from the depth byte (shallow more transparent, deep less), between two constants chosen against the in-game references | approximation, tuned by eye against the references |
| Magma and slime | opaque, unlit, the frame's colour; texture coordinates from the stored u/v (MH2O) or s/t (MLIQ) | approximation; the uv scale chosen against a reference |
| Water texture coordinates | projected from the world position: FloatParam0 repeats per chunk (documented as "texture tiles per block"), rotated by FloatParam1 degrees; computed on the CPU in the game's world frame and wrapped, so the floating origin never moves them | INFERRED (names); the scale checked against the references |
| Animation | frame = floor(t x rate) mod frameCount on one scene clock; the rate is UNKNOWN (no proven column) and chosen as a documented constant; `-wmvAnimTime=0` pins it for reproducible pictures | approximation |
| Lighting, fog, sky, reflections, refraction, particles | none (Stages 10-12) | out of scope |

If a texture is missing or refused, the liquid is drawn in its base colour alone, flat, and the report says so. Nothing
is drawn in one colour for every liquid type.

### 1.6 The host and protocol 19

The host reads the four liquid tables through its existing database system and sends the player what it needs. The
player parses MH2O and MLIQ; the host never does (the roadmap's rule).

- **Definitions.** New `bin_support/dbd/LiquidType.dbd`, `LiquidMaterial.dbd`, `LiquidObject.dbd` and
  `LiquidTypeXTexture.dbd`, one LAYOUT block each (the hashes above), with only the columns this stage relies on named
  as in public definitions and confirmed by the bytes, the rest named by position. The tables enter `database.xml` with
  `verifiedLayoutOnly="yes"` (Stage 02's mechanism: a layout not listed is not read). `SCHEMA_VERSION` 19 -> 20.
- **The liquid catalogue.** Built once per client load from the tables (SQL, values masked to their widths): for each
  LiquidType id its name, kind, material id, the material's flags and LVF, MinimapStaticCol, FloatParam0-1, and slot 0's
  frames as FileDataIDs. Retail, MoP and Beta take the FileDataIDs from LiquidTypeXTexture; Era expands the templates
  with the frame counts and resolves each name in its storage root (names only, no reads). A type whose frames cannot
  all be resolved keeps the ones that can, and says so.
- **Protocol 19.** `loadWorld` and `loadTerrainTile` carry the catalogue and a `liquids` offer (budgets) to a protocol-19
  player only; an older player gets the old commands byte for byte. A new map-asset role `liquidTexture`: allowed once
  the load's WDT (a terrain tile) or a world-model root was served; the id must be one of the catalogue's frames (any
  other id is refused); kind check `BLP2`; its own per-file limit (8 MiB), per-load count and byte budgets, so liquid
  frames never use the terrain, world-model or doodad caps. Streamed tiles copy the liquid budgets from the entry load
  (the Stage 08 trap: a field not copied in `OnStreamTile` reads 0).
- **Reports.** `terrainTileLoaded` gains a `liquids` block (instances, quads, types, textures, failures); `streamState`
  and `runtimeState` gain liquid counters. A protocol-18 player reports none and the panel says liquids were not
  offered.

### 1.7 The player

- **Parsing (pure C#, `Wow/World`).** `AdtParser.ParseLiquids` decodes the attributes, bitmaps and vertex data; the
  format comes from the rule of 1.2 when the catalogue gives the material's LVF, and from the vertex block's size (the
  gap to the next structure: 1, 5, 8 or 9 bytes per vertex, proven unique) otherwise; the two must agree, and a
  disagreement is reported. A new `WmoLiquidParser` decodes MLIQ (size equation checked). Both are bounds-checked,
  report malformed data per chunk or group and never fail the terrain or the world model.
- **Geometry (pure C#).** `TerrainLiquidMesh` builds, per tile and per liquid type, one mesh of the existing quads of
  every instance and layer: positions through `WorldToUnity` with the scene's origin (the terrain's), the world-frame uv
  of 1.5, the depth (or u/v) per vertex; vertices are not shared across chunks or instances (seams follow the data). A
  `WmoLiquidMesh` builds a group's liquid in the group's frame (`WowCoordinateConverter`, as MOVT), so the world model's
  placement transform carries it like the group.
- **Unity objects.** The tile's liquid objects are built inside `WmvTerrainBuilder.Build`, under the tile root, through
  the tile's tracking, so the tile's disposal, deferred destroy, tallies and the self-test's "every object gone" gate
  cover them. MLIQ is built only on the world path: a liquid child under the prototype's group object, made after
  `WmvWmoBuilder` builds the prototype, its mesh in the prototype's arrays; instances share it through `Instantiate` and
  cull it with the group's renderers. The Buildings viewer does not change (its pictures must stay byte-identical).
- **Materials and textures.** `Resources/WmvLiquid.shader`: two passes by material kind (transparent water, opaque
  magma), no keywords, the authored colour domain of the other world shaders. One material and one Texture2DArray per
  liquid type and scene, built from block uploads when the frames' formats allow (Stage 08's `BlpBlocks`), shared by
  every tile and world model that uses the type, counted in `SceneGpuBytes`.
- **Clock.** One world clock for the liquids' animation, advanced by the player's frame time and pinned by
  `-wmvAnimTime=0`.

### 1.8 Streaming and lifecycle

- A tile's liquid geometry comes from the root its job already parses on the worker; the textures its types need are
  asked for under that tile's load (role `liquidTexture`) and land in the scene's liquid registry (key: liquid type id;
  holders: the tiles and world-model prototypes that use it; pooled when unheld, evicted least recently used under the
  Stage 08 pools). A tile is shown when its terrain is ready; liquid types whose frames are still on their way are
  drawn in their base colour until the frames arrive, then switched (the tile is not held back).
- Closing a tile releases its holds; the liquid objects go with the tile's deferred destroy. A world-model prototype
  releases its types when it is disposed. Clear, a client switch and shutdown dispose every liquid object, material
  and array; the self-test's tallies must be 0 after each.
- A late or stale `liquidTexture` answer is dropped like every other map answer (request id and load checked); a frame
  that arrives for a type no longer held is not uploaded.

### 1.9 Budgets (to be measured)

Liquid geometry per tile at most 256 chunks x 4 layers x 81 vertices (82,944; one UInt32 mesh per type when over
65,535); a full ocean tile is about 20,736 vertices. Frames: 30 x 88,580 bytes (`lake_a`, BLP) per water type; on the GPU
about 30 x 256 x 256 at 1 byte per texel as DXT5, about 2 MB per type. Per-load offer: 256 liquid textures, 64 MiB.
Measured on the exit tiles and a liquid-rich tile per client: geometry bytes, texture bytes, build time per tile, frame
times while flying over water, GUI rounds (no new round over 50 ms), and the Stage 08 pools.

### 1.10 Tests and pictures

- Pure tests: synthetic MH2O (every format, partial instances, bitmaps with spare bits, multi-layer, malformed offsets)
  and MLIQ (both vertex kinds, absent tiles, the size equation, bad sizes); corpus tests over the pinned roots and the
  investigation's WMO examples (every instance's format by both rules, seam agreement).
- Host checks (`-terraintile`): the catalogue on each client (row counts, the exit tiles' types, Era's resolved names),
  the role's decisions (gate, membership, budgets, kind).
- Player self-test (`-wmvLifecycleTest`): synthetic tiles and world models with liquid through load, stream, evict,
  Clear and shutdown, with every liquid object gone after each.
- Harness runs (`-mapsuitest`) on the four clients: the exit tiles, a tile seam with liquid on both sides, a WMO with
  interior liquid, magma and slime; protocol 18 both ways; the Stage 08 acceptance and the regression battery.
- Pictures: the player's own capture at full resolution, before (develop) and after, of the exit tiles, a shoreline,
  a liquid seam between streamed tiles, the Stormwind canals (Era) and a lava pool; the in-game references beside them.

### 1.11 In-game references

The owner takes them in the game, at positions given by world coordinates and facing, so the viewer's camera can stand
in the same place. Requested: Retail Stormwind Harbor (29_48) and Era Lake Everstill (36_49), the roadmap's exit tiles;
optionally Era Stormwind's canals and a lava pool, for the parts of the approximation that only those can check.

### 1.12 Not in this stage

The zone Light tables (water colour and alpha), fog, sky, reflection, refraction, normal maps, foam, waterfalls, liquid
particles and sounds; MCLQ (absent from every client, `00-audit.md` 5.7); far liquid (`_lod.adt`, Stage 13); liquids in
the Buildings viewer; per-tile flow textures (MAI2, U4); the Beta-only FFT tables.

### 1.13 Risks

| Risk | Mitigation |
|---|---|
| The approximation reads as wrong next to the game | Tuned against the owner's references, documented as an approximation; no claim of parity |
| A liquid type whose frames cannot be resolved (Retail ids missing from another client's table; Era names) | Base colour, reported per type |
| Transparent liquid sorting against world-model and doodad transparents | Liquids drawn after opaque geometry, before the M2 transparents; measured on the exit tiles and the canals |
| Extra GPU and upload cost while streaming | Shared per type and scene, block uploads, counted in the Stage 08 pools |
| A new GUI round over 50 ms (catalogue build, reports) | Catalogue built once per client load off the per-tile path; measured with the harness |
| Old-format WMO liquid type rule | Documented rule, INFERRED; the textures of the candidate rows are the same |

### 1.14 Order of work

1. Pure parsers (MH2O decode, MLIQ) with synthetic and corpus tests.
2. Host: definitions, tables, catalogue, protocol 19 and the `liquidTexture` role, with in-process checks.
3. Player: liquid meshes, shader, registry, terrain and world-model integration, streaming lifecycle, reports.
4. Self-tests, harness steps, real-data runs on four clients, pictures, references.
5. Measurements, regressions, the adversarial review, fixes, this document's results, the roadmap.

## 2. The parsers (player, pure C#)

Built as section 1.7 proposed, with five refinements found while building (each recorded below with its reason).

| Part | Where | What it does |
|---|---|---|
| MH2O decode | `Wow/World/AdtParser.cs` (`ParseLiquids`), `AdtModel.cs` | the raw records become `LiquidInstance` (decoded fields plus the raw words for diagnostics), `LiquidAttributes` per chunk; exists bitmaps instance-local, spare bits masked; vertex data in the four layouts; the format from the gap to the next structure (the data region is packed, 1.2); every structure checked against the header table, the instance tables, its neighbours, shared offsets and the end of the chunk, and against the packing (1.2): a start out of chunk order, or inside a vertex block whose instance's own rule (word 0/1/2, ocean type) gives its size, is its owner's problem and never re-sizes the neighbour; a problem marks that instance or chunk malformed and never fails the terrain |
| Format | `Wow/World/LiquidFormat.cs` | `LiquidFormat.Decide` (the rule of 1.2) and `Resolve` (the rule when the catalogue knows the type, else the format by size); the catalogue view `ILiquidCatalog`/`LiquidTypeView` the player fills from the host's catalogue; the plain-array `LiquidSurface` |
| Tile mesh | `Wow/World/TerrainLiquidMesh.cs` | per tile and liquid type, the existing quads of every instance and layer, on the terrain's own vertex grid (`VertexWorldXY`, `WorldToUnity`), facing up; uv0 world-planar for water and ocean or the stored u/v, uv1 = (depth / 255, 1) or (1, 0) without depth; returns `TerrainLiquidTile` (surfaces and the report's counters) |
| MLIQ | `Wow/WmoLiquid.cs` | the 30-byte header with the size equation and X = A+1, Y = B+1 enforced; `Present(i, j)`; both readings of the 4 vertex bytes; the group's liquid type (`GroupLiquidType`); `WmoLiquidMesh` in the group frame through `WowCoordinateConverter` |
| Synthetic bytes | `Wow/World/AdtSynthetic.cs` (`Mh2o`), `Wow/WmoSynthetic.cs` (`Mliq`) | every format, partial instances, bitmaps with spare bits, layers, attributes, offset overrides, wrong-size blocks; MLIQ of both vertex kinds |

Refinements against section 1 and the contract:

1. **MLIQ's vertex kind.** s/t is read for material LVF 1 **or 3**, not 1 alone: Retail's material 13 (LVF 3, the "10FX"
   lava) carries the i16 pattern (group 4274647: 3,843 of 3,844 used vertices with bytes 1-3 set; PROVEN_BYTES).
   Without a material the kind decides (magma and slime s/t, else depth).
2. **No type.** `GroupLiquidType` returns 0 (no liquid) for MOHD 0x4 with +0x34 = 0, an out-of-range value, or an old
   file's 15 without a present tile; the documented expression would have wrapped 0 to slime.
3. **Disagreement.** When the catalogue's LVF and the block's size disagree, the geometry follows the bytes and the
   disagreement is counted; when the instance's own rule (its word 0, 1 or 2, or the ocean type) and the size disagree,
   the instance is malformed (adversarial review). Neither occurs in the corpus.
4. **Stored uv.** One documented scale, 1/256 per stored unit, with the u16 wrap joined across an instance; the largest
   step inside a triangle in the corpus is 1,027 units (Retail 34_46, slow magma). The scale is an approximation, to be
   checked against a reference.
5. **Status.** A well-formed MH2O stays `PartiallyDecoded` in the corpus report (the object word and the scales are
   still raw); each instance carries its own decoded or malformed status.

Tests (pure runner, net10, and the netstandard2.1 compile check: 0 errors, 0 warnings):

| Suite | Result |
|---|---|
| `LiquidTests` (synthetic, new) | 89 PASS, 0 FAIL |
| `LiquidCorpusTests` (new; the pinned corpus and the investigation's WMO examples) | 83 PASS, 0 FAIL |
| Every earlier suite (e.g. `WorldCorpusValidation` 74/0, `TerrainCorpusTests` 80/0) | unchanged; the corpus report is identical to before apart from timings |
| Runner total | 2,129 PASS (1,957 before), 0 FAIL |

Corpus facts (PROVEN_RUNTIME): the audit windows' 3,109 Retail, 1,793 Era, 423 MoP and 336 Beta instances decode with 0
malformed; every instance whose format the rule decides from the instance alone (word 0/1/2, type 2: Retail 1,567, Era
606) agrees with its size; every used height lies in [min, max] (Retail 194,885, Era 104,732, MoP 20,780, Beta 16,003).
Exit tiles: Retail 29_48 decodes to 229 instances (93 without data, 136 depth-only, 24 bitmaps, 130 attribute blocks), a
mesh of 13,896 quads and 17,718 vertices (662,856 bytes), chunk 0's 81 vertices bit-exactly on the terrain grid and all
3,528 seam points equal; Era 36_49 to 132 instances (format 0, flat at 56.16147, 50 bitmaps), 6,958 quads and 9,030
vertices (336,336 bytes), all 1,791 seam points equal. The 41 MLIQ example groups match the census in every field
(Era canal203 exactly). A liquid mesh takes a median 0.08 ms per tile (0.6 ms for Retail 29_48) on the worker; the root
parse is about 40% slower over the Retail window (6.55 to 9.4 ms for 56 roots) for the decoded liquids.

## 3. The host: tables, catalogue, protocol 19

| Part | Where | What it does |
|---|---|---|
| Definitions | `bin_support/dbd/LiquidType.dbd`, `LiquidMaterial.dbd`, `LiquidObject.dbd`, `LiquidTypeXTexture.dbd` | one LAYOUT block each (D1ECEEC9, 98E5D7AA, CB0D39E8, 7BEECC7F), every field in file order; the columns the catalogue reads carry their public names and what the bytes prove about them, the others are named by field index (`Field_NN`) and never read |
| Tables | `bin_support/wow/12.0/database.xml` | the four tables, `verifiedLayoutOnly="yes"`: a layout not listed is not read; `SCHEMA_VERSION` 19 -> 20 (`Source/core/GameDatabase.cpp`) |
| Catalogue | `Source/games/wow/LiquidCatalog.{h,cpp}` | built once per client load: per type its name, kind, material (flags, LVF; -1 when the type has no material row), minimap colour, Float[0..1], and slot 0's frames as FileDataIDs -- from LiquidTypeXTexture where the type has rows, else by expanding Texture[0] with FrameCountTexture[0] (masked to 8 bits) and resolving each name in the storage root (no reads); the contract's JSON built once; one log line per load |
| Protocol 19 | `Source/wowmodelviewer/UnityIpcServer.{h,cpp}`, `modelviewer.{h,cpp}` | the `liquids` offer (budgets and the catalogue) on `loadTerrainTile` and `loadWorld` to a protocol-19 player only (gate `playerDrawsLiquids`); the role `liquidTexture` (below); streamed tiles copy the entry's liquid budgets and catalogue; the reports' liquid fields parsed and logged |
| Panel | `Source/wowmodelviewer/MapsViewer.{h,cpp}` | a liquids line; "No liquids" only when the load had none |

The role `liquidTexture` is refused, with the reason, for a load without a liquids offer, before the load's WDT was
served, for an id that is not a frame of the catalogue the load was sent with (the host vouches for the catalogue's
files and no other), for an id asked for before in the load, and past 256 ids; a read is limited to 8 MiB per file and
64 MiB per load (a read cut to what is left is `refused`, a larger file `unreadable`, the Stage 08 rule); a frame must
start `BLP2`. The liquid reads are a family of their own: they never use the terrain, world-model or doodad caps.

Three reader facts met on the way (PROVEN_RUNTIME): the host's DB2 reader resolves only a string array's first element
correctly (an offset counted from the field's start), so the catalogue reads Texture[0] alone, which is all it needs,
rather than changing the global reader; pallet values are masked by the catalogue's own reads (FrameCountTexture & 0xFF,
LiquidObject's type & 0xFFFF), not in the global reader (no change for the tables Models and Characters read); floats
are written to the database with six significant digits (the existing reader's behaviour), so Float[0..1] carry that.

`-terraintile 0 30 49` (the in-process checks, before any player starts), private runtime `D:\wmv-09\run`, the database
built anew and then read from the cache, each client both ways:

| Client | Result | Catalogue (log line) |
|---|---|---|
| Retail 12.1.5.70448 | PASS 307 / 0, 2 SKIP | 201 types, 1,723 frames, 0 unresolved, 10.5 ms; frames from LiquidTypeXTexture for 200 types |
| Classic Era 1.15.9.70003 | PASS 306 / 0, 3 SKIP | 19 types, 556 frames, 0 unresolved, 0.54 ms; all by name |
| MoP Classic 5.5.4.70449 | PASS 307 / 0, 2 SKIP | 61 types, 1,071 frames, 0 unresolved, 3.8 ms; every table frame agrees with its name (1,071 of 1,071) |
| Classic Beta 1.60.1.70450 | PASS 308 / 0, 1 SKIP | 57 types, 598 frames, 0 unresolved, 2.8 ms |

The Stage 08 checks (262) all pass; 45 are new: 26 on the role's decisions, budgets and accounting, 15 on the catalogue
and real frame reads (frame 219901 served whole with its content key, refused the second time; a WDT asked for as a
liquid texture refused unread), 3 kind checks, 1 stream-load check, 1 report-parse check. The new SKIP is the
frames-against-names check where the root names no files (Retail, Beta) or the table has no rows (Era). The catalogue
as sent is 47,266 bytes (Retail), 6,957 (Era), 17,565 (MoP), 14,352 (Beta). A protocol-18 player gets both commands
byte for byte as before ("no liquids (a protocol 18 player)").

## 4. The player: liquid objects, shader, registry, lifecycle

| Part | Where | What it does |
|---|---|---|
| Materials, registry, meshes | `WmvLiquids.cs` (new) | `WmvLiquidMaterials`: the shader (loaded with `Resources.Load`, no keywords), the approximation constants, one material per liquid type; `WmvLiquidRegistry`: the scene's liquid types by id, their holders (tiles and world-model prototypes), pooling and eviction, one shared `Texture2DArray` per distinct frame list (types that list the same frames share it), the clock, the tallies; `WmvLiquidMeshes`: a surface to a Unity mesh, a tile's liquid objects |
| Session | `WmvTerrainSessionLiquids.cs` (new), `WmvTerrainSession.cs`, `WmvTerrainSessionStream.cs`, `WmvTerrainSessionWorld.cs` | the MH2O mesh built on the worker next to the terrain mesh; the per-load liquid work (frame requests under role `liquidTexture`, answers, the timeout); the session-wide registry while streaming (streamed tiles take the entry's offer and catalogue); MLIQ added to a world model after its prototype is built, on the world path only; the report block, the `[liquids]` log lines, the streamState counters |
| Tile objects | `WmvTerrainBuilder.cs` | the liquid objects built in `Build`, under the tile root, on layer 30, tracked through the tile (`TrackLiquid`), so the tile's disposal, deferred destroy, tallies and the self-test's gate cover them; the tile releases the types it held; their height range is kept apart from the terrain's bounds and widens the `-wmvTerrainTopDown` clip range (an ocean above its seabed) |
| World-model objects | `WmvWorldModels.cs` | `AddLiquids`: a child under the prototype's group object; its meshes join the prototype's arrays (`Runtime.Meshes`), the world-model tallies and the GPU geometry bytes of the pools; released when the prototype is disposed (private and pooled prototypes too; an evicted pooled prototype gives its types back as last used when it was, and its frame arrays are the pools' while only pooled prototypes hold them, the rule of the Stage 08 M2 texture uploads); instances share the meshes through `Instantiate` and are culled through their renderers' own bounds, never counted as groups (`worldGroups`, `groupsDrawn` keep their Stage 06 meaning) |
| Shader | `Resources/WmvLiquid.shader` ("Wmv/Liquid") | one shader, set by uniforms (no keywords): the blending by the material's flag 0x1 -- transparent (`Blend SrcAlpha OneMinusSrcAlpha`, `ZWrite Off`, `Cull Off`, `ZTest LEqual`) or opaque (depth written); the colour by the kind (1.5) -- water and ocean the base colour plus the frame's alpha, magma and slime the frame's colour, so Retail's material 13, 14 and 18 lava, fel and swamp are blended and take their frame's colour; writes authored colour values like the other world shaders (the frame decode pass converts them); unlit |
| Upload | `WmvTerrainMaterials.cs` | `UploadSlice` and `ConvertToRgba`: a type's frames go into one array through the terrain texture path (DXT blocks as stored, rows not flipped); frames of one size but another format or mip count are decoded to RGBA32; a frame of another size, or one that cannot be had, leaves the type in its base colour |
| Clock, state | `WmvMainTerrain.cs` | the liquid clock (pinned by `-wmvAnimTime`), the runtimeState liquid fields, the liquids label of the shot log |
| IPC | `WmvIpcClient.cs` | protocol 19; the `liquids` offer parsed into the catalogue; the report block and the streamState and runtimeState counters |
| Buildings | `WmvWmoBuilder.cs` | one comment only: the Buildings viewer's path draws no liquid and is unchanged |

The approximation's constants (`WmvLiquids.cs`), all provisional until the in-game references:

| Constant | Value | Why |
|---|---|---|
| `HighlightScale` | 0.5 | water colour = base colour + frame alpha x 0.5: the frames are near-black and carry the ripples in their alpha (1.5) |
| `ShallowAlpha`, `DeepAlpha` | 0.45, 0.8 | opacity at depth byte 0 and 255, and 0.8 where no depth is stored (an ocean instance without data); the depth byte's scale is UNKNOWN |
| `FramesPerSecond` | 15 | no proven rate column: a 30-frame loop in 2 s |
| `TransparentQueue` | 2999 | after opaque and alpha-tested geometry (at most 2450), before the M2 transparents (3000 and up) and the particles (3900) |
| `OpaqueQueue` | 2000 | magma and slime draw as opaque geometry |

Refinements against section 1, found while building:

1. **A tile's frames upload together.** The array's format can only be chosen from all of a type's frames, so they are
   uploaded once the whole list has settled (0.3-0.9 ms per list), not slice by slice.
2. **The load stays open after adoption** until its frames settle: its report (or a streamed tile's settle) waits for
   them, with the load's timeout counted from the last liquid progress. The tile is on screen meanwhile, its liquid in
   the base colour.
3. **A type the catalogue lacks is not drawn** (it has no base colour) and is counted in `missingTypes`.
4. **Liquids are drawn in every terrain view** (the textured view and the diagnostics alike).
5. **Each frame is asked for once per session**: a tile whose types' frames a neighbour already fetched asks for none
   (Era 36_49 asked for 24 of its type's 30 in the smoke run).

Player self-test (`-wmvLifecycleTest`, player p19a, Assembly-CSharp MD5 D0A803FA7C6A3B1049E20CD87D0D8BE9): **992 passed,
0 failed** (the 965 of Stage 08 and 27 new: a single load with liquid, a world model with MLIQ, streaming, the IPC
shapes). Every liquid mesh, material, array and object is gone, and every tally 0, after each scenario.

First real-data run (protocol-19 host and player, streaming radius 1; PROVEN_RUNTIME):

| | Retail 29_48 | Era 36_49 |
|---|---|---|
| Instances / chunks / most layers in a chunk | 229 / 229 / 1 | 132 / 132 / 1 |
| Quads / vertices | 13,896 / 17,718 | 6,958 / 9,030 |
| Meshes / types | 1 / [2] | 1 / [5] |
| Frames asked / loaded / failed | 30 / 30 / 0 | 24 / 24 / 0 (6 fetched by neighbour tiles) |
| Geometry / texture bytes | 662,856 / 2,622,240 (30 x 256 x 256 DXT3, one array) | 336,336 / 2,622,240 |
| Worker MH2O, Unity objects, array upload (ms) | 9.4, 2.75, 0.55 | 7.6, 1.10, 0.84 |
| Session (9 tiles) | 9 liquid tiles, 3 types (5 and 301 share one array), 12 objects, 9,745,604 GPU bytes | 6 liquid tiles, 2 types, 1 array, 10 objects, 3,238,456 GPU bytes |

Era also drew world-model liquid in that run: md_mushroomcave (111185, on tile 36_48), 4 groups, 150 quads, type 13,
built in 1.8 ms. After Clear every liquid tally was 0 in both runs. The geometry matches section 2's pure figures
exactly.

## 5. Acceptance on real data, four clients

Host F85B31A9 (core 3FE59407, wow.dll 5A727EDC; the worktree built in `F:\wmv-build-06`), player p19a (Assembly-CSharp
MD5 D0A803FA...), the `-mapsuitest` harness on a hidden desktop; cameras placed from the clients' own bytes (a shore
quad of the MH2O data, a world model's MLIQ through the player's own placement transform). Rig `D:\wmv-09\rig\c1`,
results `D:\wmv-09\out\c1`. Every liquid line of every player log: 0 malformed, 0 failed frames, 0 dropped answers, 0
types the catalogue lacks; no `liquidTexture` refusal in any host log.

| Run | Checks | What the entry tile's report says (PROVEN_RUNTIME) |
|---|---|---|
| Retail (`acc_wow`) | PASS 79 / 0 | 29_48: 229 instances, 13,896 quads, 17,718 vertices, type [2], 30 / 30 frames, 662,856 bytes of geometry, 2,622,240 of frames. 33_46: 62 instances, 2 layers, types [7, 19], 2 world-model groups with 3,912 quads (Blackrock's lava). 32_33: 43 instances, types [4, 5, 2] |
| Classic Era (`acc_era`) | PASS 79 / 0 | 36_49: 132 instances, 6,958 quads, 9,030 vertices, type [5], all 30 frames over the session. Stormwind (entered on 30_48): world model 107243, 22 canal groups, 2,875 quads, type 13. 33_46: types [7, 19] |
| MoP Classic (`acc_mop`) | PASS 41 / 0 | 33_46 as Retail's; 29_48: 226 instances, 13,464 quads, type [2], 30 / 30 frames |
| Classic Beta (`acc_beta`) | PASS 51 / 0 | 29_48: 189 instances, types [1250, 13, 1344]; Stormwind's canals (world model 7962574): 2 groups, 2,702 quads. Kalimdor 34_44: 91 instances, types [1284, 1297] |
| Dire Maul, map 429 (`dm_wow`, `dm_era`) | PASS 20 / 0 each | a world-model-only map: 1 group, 144 quads, type 13, 30 / 30 frames |

Further cases (all PASS): flights across the 29_48 / 30_48 and 36_49 / 37_49 seams, liquid on both sides in the
pictures; a protocol-18 host (Stage 08's, 24FE3DA6) with the new player: "no liquids offered", no frame asked, the
picture pixel-identical to Stage 08's; a pooled liquid type evicted on a 24,000-yard flight (below), the ocean drawn
again on the way back; a world-model-only map with liquid; a view switch (chunk borders and back): the liquid objects
stay, tiles streamed in that view get theirs, and the textured picture after is pixel-identical to the one before.
Which tile load asks for which frame varies between runs; each frame is asked for once per session in every run.

After the review's fixes (section 8) the runs above were repeated on player p19b (2026-10-19, `D:\wmv-09\out\c1b`):
Retail 79 / 0, Classic Era 79 / 0, MoP Classic 41 / 0, Classic Beta 51 / 0, Dire Maul 20 / 0 on both clients, the long
flight with eviction 20 / 0, `life08` 70 / 0, `life08b` 12 / 0: all PASS. Their only rounds over 50 ms are a start round
(228.5 ms, before step 1, as on Stage 08) and the model load right after a switch into Models in `life08` (61.2 ms; 62-69
ms on Stage 08).

### 5.1 Pictures

`D:\wmv-09\out\pics\` (index `index.md`): 38 before | after pairs at 1:1 (the Unity viewport, 1,665 x 956, cut from the
window's picture), the window pictures, the clock pinned (`-wmvAnimTime=0`); two after-runs of the Retail set are
pixel-identical in 11 of 11 viewports. Before = Stage 08 (develop 3e11ee52, host 24FE3DA6, player p18o). They show:
Stormwind Harbor (29_48) from the water, along a shore both ways and straight down; the 29_48 / 30_48 seam (ocean and
the slow-water canal above the waterfall); Lake Everstill (36_49) along the shore and down; the 36_49 / 37_49 seam;
Stormwind's canals in Era (interior world-model liquid, the water at the canal's level between its walls) and Beta;
slow magma on 33_46 (terrain, 8-byte vertices with u/v) on three clients; Blackrock's lava (world model) on three
clients; slime beside slow water on Retail 32_33; Dire Maul's interior water; Beta's PBR ocean and Un'Goro types.

What the pictures show about the approximation (to be weighed against the in-game references, section 6):

- Water and ocean read as a bright, translucent cyan (the minimap colour standing in for the zone's water colour), the
  sparkle pattern of the frames on top, the ground visible below.
- Slime and terrain magma (stored u/v at 1/256 per unit) show staircase bands: neighbouring vertices' u/v differ by a
  median of 22 units and up to 258 (slime) and 7,149 (magma), so a steep band gets about one repeat per quad. World-
  model lava (Blackrock, s/t) shows a radial pinch. The stored coordinates' scale is UNKNOWN; these pictures say 1/256
  is not it everywhere.
- Beta's PBR water types (1250, 1344; slot 0 a single 1,024 x 1,024 RGBA texture, not a ripple frame list) draw pale,
  type 1284 dark grey: the approximation does not fit them.
- Era's terrain is mostly grey in both builds on these tiles (208 of 256 chunks of 36_49 use an alpha format the
  terrain shader does not support): pre-existing (Stage 04), unchanged.

### 5.2 Memory and lifecycle

| Measure | Value (PROVEN_RUNTIME) |
|---|---|
| A full ocean tile (Retail, MoP, Beta 28_48) | 16,384 quads, 20,736 vertices, 777,216 bytes |
| Era's richest tile (29_49) | 730,528 bytes |
| A 30-frame array, 256 x 256 | 2,622,240 bytes as DXT3 (water), 1,311,120 as DXT1 (magma, slime) |
| Beta's single-frame types | 5,592,404 bytes (1,024 x 1,024 RGBA8: 1250, 1240, 1306), 4,194,304 (1267), 349,552 (1284) |
| World-model liquid | Blackrock 162,276 bytes, Era Stormwind 132,276, Beta canals 119,112 |
| Retail 29_48 with its 9 tiles, scene GPU bytes after minus before | textures +5,244,480 (exactly the 2 arrays), geometry +4,501,124 (exactly the 9 tiles' liquid meshes); 12 liquid objects, 3 materials, 9,745,604 bytes |

Four Enter / settle / Clear cycles at 29_48: every liquid tally 0 after each Clear, and the live Unity objects (9
objects, 1 mesh, 62 materials, 78 textures) as on Stage 08; private bytes after each Clear: player 1,895 / 1,997 /
2,022 / 1,943 MB, viewer 2,253 / 2,253 / 2,254 / 2,254 MB (no growth). Streaming: three far flights (65 / 86 / 146 tiles
opened); liquid types resident rose from 3 to 7-8, arrays from 2 to 3, liquid bytes 6.3-11.4 MB; on the longest a
pooled liquid type was evicted in the same pass as 139 world-model prototypes and 103 textures (the three arrays stayed:
a held type lists the same frames); after Clear every liquid tally 0.

### 5.3 Timing

Measured alone on the machine (no game client, no other viewer: each run logs the machine's state), the new build
(host F85B31A9, player p19b) against Stage 08's final (host 24FE3DA6, player p18o), alternated: new, base, base, new for
the acceptance, the water flights and the one-tile loads; new, base, new, base twice more for the switch cycles. Rig
`D:\wmv-09\rig\c3`, results `D:\wmv-09\out\c3` (`report.txt`, `report_cyc.txt`).

| Measure | Stage 09 | Stage 08 |
|---|---|---|
| Retail acceptance (six loops, far flight, Overview, Fly): GUI rounds over 50 ms, the start round left out | 0 and 0 (two runs) | 0 and 7 (the second baseline run had stalls of 54-894 ms in its loops, polls of 0.01-0.3 ms: the machine, not the viewer) |
| Its longest round per step, start left out (ms) | mode:maps 29.1 / 30.8, pick 32.5 / 8.0, enter 15.0 / 11.2, waitworld 21.8 / 19.8, clear 26.4 / 26.1 | 36.4 / 35.1, 6.8 / 30.8, 20.9 / 11.1, 20.7 / 21.0, 27.5 / 26.0 |
| Its six loops: frames over 33 ms, the longest flight frame | 4 / 4, 38.6 / 36.0 ms | 5 / 17, 36.0 / 105.7 ms |
| Region settle after a loop | 2.4-2.8 s | 2.3-2.7 s |
| Classic Era, MoP, Beta (one loop): rounds over 50 ms, longest | 0, 29.7 / 33.7 / 36.2 ms | 0, 26.3 / 24.1 / 29.5 ms |
| Switch cycles, 4 clients x 3 runs each (74 switches a run) | 2 of the first 4 runs failed (below); the next 8 PASS, 0 rounds over 50 ms, switch rounds median about 11 ms | 12 of 12 PASS, 0 over 50 ms, median about 11 ms |
| Flight over Retail's ocean (29_48, 8 pieces of 420 yards at 30 yards up), frame-capped | window medians 16.67 ms, at most 1 frame over 33 ms, none over 50; GUI rounds at most 2.2 ms | the same (1 frame over 33 ms in each run) |
| The same, uncapped (`-wmvFrameRate=0`): mean frame | 1.785 / 1.710 ms | 1.695 / 1.665 ms |
| Flight over Era's Lake Everstill (36_49, 11 pieces), uncapped mean frame | 1.656 / 1.571 ms | 1.614 / 1.580 ms |
| One tile alone (`WMV_STREAMING=0`), Enter world to built: Retail 29_48 | 8,199-8,310 ms (with its 229 liquid instances) | 8,284-8,380 ms |
| The same, Era 36_49 | 4,968-5,108 ms (132 instances) | 5,093-5,169 ms |

The first new-build run of the switch cycles (the evening before, `new_r1`) had a round of 800 ms in a plain wait (a busy
stretch of 1 ms: the thread was not running) and a switch whose two turns took 117.5 and 125.6 ms, where the same turns
take 7-16 ms in every other run: the whole machine was slow for a moment. The eight runs alternated with the baseline the
next morning reproduce none of it. No GUI round over 50 ms is new.

Liquid build costs (the player's log, all runs): the MH2O mesh on the worker a median 0.3 ms per tile (at most 12.4 ms);
the tile's liquid objects on the main thread a median 0.09 ms (at most 4.0 ms); a frame array's upload at most 3.3 ms;
a world model's MLIQ meshes at most 3.6 ms; every frame answer `ok`. With liquid, the uncapped frame over water costs
about 0.05-0.09 ms more on Retail (3-5%) and up to 0.04 ms on Era; the capped frame rate does not change. The Retail
acceptance holds 6-8 MB of liquid on 9-15 liquid tiles; player memory after its loops 2.76-2.83 GB against 2.71-2.83 GB.

## 6. In-game references

The owner was asked (2026-10-18) for reference pictures at four spots: Retail Stormwind Harbor, Era Lake Everstill
(the roadmap's exit tiles), optionally Era Stormwind's canals and a lava pool, each with the position and facing the
game prints, so the viewer's camera (`target`, `look`) stands in the same place.

**Not compared yet.** No reference had arrived when the stage was committed (2026-10-19); the owner chose to commit
with the look recorded as an untuned approximation and to tune it later against the references (the colour, opacity
and highlight constants of section 4, the stored-uv scale), in a follow-up change. So the roadmap's exit test, water
tiles against references on Retail and Era, is open: this stage proves the data, the placement and the lifecycle, not
the look.

## 7. Regressions

Same host and player as section 5; baseline Stage 08 (24FE3DA6 + p18o). Rig `D:\wmv-09\rig\c2`, results
`D:\wmv-09\out\c2`.

| Run | Result | Stage 08 |
|---|---|---|
| `-terraintile 0 30 49`, Retail / Era / MoP / Beta | PASS 307 / 306 / 307 / 308, 0 failed | 262 / 0 (Retail) |
| Player self-test | 992 / 0 (998 / 0 after the review fixes, section 8) | 965 / 0 |
| New host + protocol-18 player (p18o) | PASS 15 / 0; "no liquids (a protocol 18 player)", every liquid counter absent | -- |
| Stage 08 host + new player | PASS 15 / 0; nothing drawn, no frame asked | -- |
| New host + protocol-17 player | PASS 15 / 0 | 15 / 0 |
| `life08`, `life08b` (session endings, cancellation) | PASS 70 / 0, 12 / 0; every liquid tally 0 after every Clear | 70 / 0, 12 / 0 |
| Map catalogue sequences | 44 / 0, 44 / 0, 27 / 0, 27 / 0 | the same |
| Stage 07 battery: 98 e2e dumps, 6 geo dumps, the two reg pictures (bear, Buildings WMO 108631) | byte-identical to Stage 08's (no dump carries liquid data); every set the Stage 08 outcome; `H_B_missingtex0` fails the same step as at Stages 07 and 08 | -- |
| Buildings: six world models WITH liquid (Era Stormwind 107243, Era and Retail Blackrock 108238, Retail 4235933, Beta Stormwind 7962574, Era 111185), three takes each | byte-identical before and after; no liquid line in any Buildings run (liquids are drawn on the world path only) | -- |
| Characters (the three .chr cases) | vary between runs on BOTH builds (below) | -- |
| Pure runner | 2,129 / 0 (2,136 / 0 after the review fixes) | 1,957 / 0 |

The Characters pictures vary from run to run on develop too: `TabardDetails::tier` is never initialised
(`TabardDetails.h:79`) and the character control first draws a random tabard design, so the tabard's background and
border depend on leftover memory. Not Stage 09's; the owner's uncommitted guild-tabard change initialises it. The
`-terraintile` runs now load liquids too (tile 30_49: 37-44 instances per client, 0 malformed). One checker run
(`H_B_lifecycle`) saw a 55 ms poll while a log line was written under heavy load (two other runs on the machine); its
two quiet reruns pass with polls of at most 1.1 ms. A single load logs each map answer (a streaming session counts them
for a summary, Stage 08's rule), so a single load with liquid logs its frames' answers as well.

## 8. Adversarial review

An independent review of the whole uncommitted change, in six dimensions (parsing, transforms, rendering, resource
ownership, streaming lifecycle and IPC, regressions), each finding then handed to a separate skeptic told to refute it
from the code paths. 13 claims; 9 confirmed (7 distinct defects: two pairs were the same defect seen from two
dimensions), 4 refuted.

| # | Defect (severity) | Fix | Proof |
|---|---|---|---|
| 1 | MLIQ: the size check could be passed through a 64-bit overflow, after which X x Y overflowed and the parse threw instead of reporting the chunk malformed (low; crafted input only) | `WmoLiquid.Parse` bounds X x Y by the payload before any product | a 102-byte crafted payload returns no liquid and a problem, no exception |
| 2 | MH2O: a malformed offset pointing inside a well-formed neighbour's vertex block silently changed the neighbour's format by size (low; malformed input only) | the proven chunk-order packing and the instance's own format rule are enforced (`FindMisplaced`): the misplaced offset is its owner's problem and is dropped from the layout, so the neighbour keeps its block | offender at +81 and +324 inside a neighbour, with and without a structure between, and word and ocean conflicts: the offender malformed, the neighbour format 0 with its heights |
| 3 | The top-down capture's clip range came from the terrain alone, cutting liquid above the terrain's top (low) | a liquid height range kept per tile widens the top-down clip range only (`tile.Bounds`, the orbit framing and the logged bounds unchanged) | clip range 0-61 for ocean at 0 and magma at 61, terrain bounds unchanged |
| 4 | World models' culling and report counters counted each MLIQ liquid renderer as a group (low) | a liquid-renderer mask per prototype: every renderer still culled by its own box, liquids left out of the group counts | 3 placements report 1 group of 1, 3 liquid renderers culled beside them |
| 5 | The colour source followed the transparency flag, not the kind: Retail's lava, fel and swamp on transparent materials 13, 14 and 18 lost their frames' colour (medium) | colour by kind (magma and slime: the frame's colour; water and ocean: base colour + highlight), the flag for blending, depth write and queue only | a type of kind magma with flag 1 and LVF 3 is blended and coloured from its frame |
| 6 | Liquid frame arrays held only by pooled world-model prototypes were missing from the pool's GPU bytes and its least-recently-used order (low) | Stage 08's M2-texture rule applied: such arrays count as pooled, and a type released by an evicted prototype takes the prototype's last use | all array bytes counted while only a pooled prototype holds the type; the evicted type older than the stamp before eviction |
| 7 | A frame arriving after its list had settled kept its prepared data for good (low) | dropped when no list holding it still fetches | frame data dropped on arrival after its list settled; the type still animates |

Refuted: a single-load frame list said never to settle (the code re-wants a frame reset to "wanted"); the self-test said
not to cover shutdown (it does, through the same disposal path); the third-party notice said to misattribute the new
definitions (the definitions are adaptations of the public ones; the notice was nevertheless brought up to date, below);
streamed liquid frames said to be served before the same ring's world models (the priorities are the tile's own).

After the fixes: player p19b (Assembly-CSharp MD5 EC9ECE72...), self-test **998 passed, 0 failed**; pure runner **2,136
PASS, 0 FAIL** (`LiquidTests` 96, `LiquidCorpusTests` 83); `-terraintile` Retail 307 / 0, Era 306 / 0, as before. The
host binaries did not change. `THIRD-PARTY-NOTICES.txt` now names the four new definition files as adaptations of the
public table definitions (CC BY-SA 4.0).

Left by the fix of defect 2: one malformed case the bytes cannot decide. When a block's size comes only from the
catalogue (its word is 3 or more and it is not ocean) and no structure lies between it and the next in chunk order, a
bad offset that splits the gap into two valid-looking sizes goes undetected. No client file has a malformed MH2O.

## 9. Unresolved and limitations

Marked **new** (this stage's own) or **pre-existing**.

- **The look is an approximation, not the game's** (new): the base colour is the liquid type's minimap colour standing
  in for the zone's water colours, which come from the Light tables (Stage 12); the highlight scale (0.5), the shallow
  and deep opacity (0.45, 0.8), the frame rate (15 a second) and the stored-uv scale (1/256) are chosen constants
  (section 4). Visual parity is NOT claimed; section 6 records the comparison with the in-game references.
- **Stored texture coordinates** (new): slime and terrain magma show staircase bands and world-model lava a radial
  pinch at 1/256 per unit (section 5.1); the scale of MH2O's u/v and MLIQ's s/t is UNKNOWN.
- **Texture slots 1-5 are not drawn** (new): procedural depth, bump, foam and `Magma0-2`; their roles are UNKNOWN.
- **Beta's PBR water types** (new): their slot 0 is a single 1,024 x 1,024 texture, not a ripple frame list; they draw
  pale (1250, 1344) or dark grey (1284). The Beta-only FFT tables are not read.
- **MLIQ water uv is in the group's frame** (new): all placements of a world model share it, so its pattern does not
  line up with the world's; MH2O's is world-aligned.
- **Ocean (type 2) is depth-only** (PROVEN_BYTES, mechanism UNKNOWN): other ocean-class types carry heights.
- **Old-format world-model liquid type** (INFERRED): the documented rule maps the tile nibble's kind to the "WMO" rows
  (13, 14, 19, 20); the bytes agree on the kind, not on the row (the rows share their first texture).
- **Not drawn** (new): a world-model group with a liquid type but no MLIQ (documented as filled to its top; 31 Retail,
  2 Era, 23 MoP, 12 Beta groups); the MH2O attributes and the MLIQ tile bits 0x10-0x80 (meanings UNKNOWN); liquids in
  the Buildings viewer (the world path only; its pictures stay byte-identical); liquid in LOD groups and far terrain
  (Stage 13); MCLQ (absent from every client).
- **Report timing** (new): a load stays open after its tile is adopted until its liquid frames settle (its report, or a
  streamed tile's settle, waits for them); the tile is on screen meanwhile with its liquid in the base colour.
- **Logging** (pre-existing rule): a single load logs every map answer, its liquid frames' included (a streaming
  session logs a summary every two seconds); one log write of 55 ms was seen under heavy load (section 7).
- **Era terrain on these tiles is mostly grey** (pre-existing, Stage 04): 208 of 256 chunks of 36_49 use an alpha format
  the terrain shader does not support.
- **Characters vary between runs** (pre-existing, develop): `TabardDetails::tier` is not initialised (section 7).
- **Audit corrections**: `00-audit.md` 5.7 and T7 ("second word 42 or larger") are wrong on Retail, the Beta row of T7
  and the Retail row of T10 are stale (section 1.2, 1.4); the audit is left as it was, corrected here.

## 10. Files

Host (`Source/`): `games/wow/LiquidCatalog.{h,cpp}` (new: the catalogue), `games/wow/CMakeLists.txt`;
`core/GameDatabase.cpp` (`SCHEMA_VERSION` 20); `wowmodelviewer/UnityIpcServer.{h,cpp}` (protocol 19, the offer, the
reports), `modelviewer.{h,cpp}` (the catalogue's lifetime, the role `liquidTexture`, the offer in `RequestLoad`, the copy
in `OnStreamTile`), `MapsViewer.{h,cpp}` (the liquids line), `app.cpp` (the in-process checks, liquid counters in the
harness samples). Data: `bin_support/dbd/LiquidType.dbd`, `LiquidMaterial.dbd`, `LiquidObject.dbd`,
`LiquidTypeXTexture.dbd` (new), `bin_support/wow/12.0/database.xml`; `THIRD-PARTY-NOTICES.txt`.

Player (`Tools/UnityRendererProject/Assets/`): `Scripts/Wow/World/AdtModel.cs`, `AdtParser.cs`, `AdtSynthetic.cs`,
`LiquidFormat.cs` (new), `TerrainLiquidMesh.cs` (new); `Scripts/Wow/WmoLiquid.cs` (new), `WmoSynthetic.cs`;
`Scripts/WmvLiquids.cs` (new), `WmvTerrainSessionLiquids.cs` (new), `WmvTerrainSession.cs`, `WmvTerrainSessionStream.cs`,
`WmvTerrainSessionWorld.cs`, `WmvTerrainBuilder.cs`, `WmvTerrainMaterials.cs`, `WmvWorldModels.cs`, `WmvMainTerrain.cs`,
`WmvIpcClient.cs`, `WmvWmoBuilder.cs` (a comment), `WmvLifecycleSelfTest.cs`, `WmvLifecycleSelfTestLiquids.cs` (new);
`Resources/WmvLiquid.shader` (new). Tests (`Tools/UnityRendererProject/Tests/`): `LiquidTests.cs`, `LiquidCorpusTests.cs`
(new), `WorldParserTests.cs`, `WorldCorpusValidation.cs`, `TerrainCorpusTests.cs`, `Runner/Program.cs`,
`Runner/WowParserTestRunner.csproj`, `README.md` (`WMV_LIQUID_EXAMPLES`).

Documents: this file; `roadmap.md` (row 09).
