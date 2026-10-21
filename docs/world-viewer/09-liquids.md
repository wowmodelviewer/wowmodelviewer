# World viewer 09 -- liquids

Branch `world/09-liquids`, from `develop` 3e11ee52 (Stage 08, PRs #125 and #126). This document is written in the order
the work was done: the investigation and the architecture first (section 1, written before any code), then the
implementation, its tests and measurements, the tuning against the in-game references (section 11) and the High look
(section 12), and last the acceptance that closed the stage (section 13).

Evidence grades: **PROVEN_BYTES** (decided by the client's own files), **PROVEN_RUNTIME** (observed in the running viewer
or player on real client data), **PROVEN_SOURCE** (read in this repository's source), **INFERRED** (follows from proven
facts, or taken from public documentation and consistent with the bytes, but not observed directly), **MEASURED** (read
from pictures, with the method stated: section 11), **UNKNOWN**. Test outcomes: **PASS**, **FAIL**, **SKIP** (not run,
said why), **UNSUPPORTED** (cannot be run on this data or by this design, said why).

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

**Superseded in part (2026-10-20).** The base colour, the water and ocean colour, highlight and opacity, and the water
texture coordinates of this table are replaced by section 11 (the zone light's water colours and alphas, opacity by
depth in yards, a neutral highlight after the blend, twice the repeats on the game's axes); the minimap colour and the
old constants remain only as the fallback where no zone light applies. The other rows stand. Section 12 replaces the
look again for transparent water and ocean in the world view (the High look: no frames, a ripple map, refraction and
reflection); the look of section 11 stays with `WMV_WATER=low` and wherever the High look does not apply.

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

Since 2026-10-20 the zone Light tables' water colours and alphas are read (section 11), and the High look (section 12)
draws a normal map (the type's ripple map), refraction and screen-space reflection, with the zone light's sky colour
where a reflection misses; fog, the sky itself, foam, waterfalls, particles, sounds and the rest of this list are still
not in this stage.

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
| Tile mesh | `Wow/World/TerrainLiquidMesh.cs` | per tile and liquid type, the existing quads of every instance and layer, on the terrain's own vertex grid (`VertexWorldXY`, `WorldToUnity`), facing up; uv0 world-planar for water and ocean or the stored u/v, uv1 = (depth / 255, 1) or (1, 0) without depth (since section 11: (depth / 255, the depth in yards) or (1, -1), and the planar uv on the game's axes at twice the repeats); returns `TerrainLiquidTile` (surfaces and the report's counters) |
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
| Light definitions (section 11) | `bin_support/dbd/Light.dbd`, `LightParams.dbd`, `LightData.dbd`, `ZoneLight.dbd`, `ZoneLightPoint.dbd` | one LAYOUT block per layout (Light 5F16BC84; LightParams CAE394E7, A7F31923, 51C96BAD; LightData 6ABF2921, 360DA016, 70C6CD80; ZoneLight 94CE95E0, BA8274F0; ZoneLightPoint DE2377FB), every field in file order; the columns the light catalogue reads carry their public names and what the bytes prove about them, the others carry neutral names and are never read |
| Light tables (section 11) | `bin_support/wow/12.0/database.xml`, `Source/core/GameDatabase.cpp` | the five tables, `verifiedLayoutOnly="yes"`; `SCHEMA_VERSION` 20 -> 21 |
| Light catalogue (section 11) | `Source/games/wow/LightCatalog.{h,cpp}`, `Source/games/wow/wdc3file.{h,cpp}` | built once per client load from the five tables, each read whole once (LightParams' copy table through `WDC3File::copyRows()`, the only link from a copied row to its source's LightData); per map, on its first load, the `lights` object (zones, points, default light, each light's water colours and alphas at 12:00, slot 0), then kept; one log line per load |
| The `lights` key (section 11) | `Source/wowmodelviewer/UnityIpcServer.{h,cpp}`, `modelviewer.{h,cpp}` | the `liquids` offer of `loadTerrainTile` and `loadWorld` carries `lights` as its fifth key (state `ok`, or `unavailable` with the reason); protocol 19 kept (an additive key); streamed tiles copy it with the offer |

The role `liquidTexture` is refused, with the reason, for a load without a liquids offer, before the load's WDT was
served, for an id that is not a frame of the catalogue the load was sent with (the host vouches for the catalogue's
files and no other), for an id asked for before in the load, and past 256 ids; a read is limited to 8 MiB per file and
64 MiB per load (a read cut to what is left is `refused`, a larger file `unreadable`, the Stage 08 rule); a frame must
start `BLP2`. The liquid reads are a family of their own: they never use the terrain, world-model or doodad caps.

Three reader facts met on the way (PROVEN_RUNTIME): the host's DB2 reader resolves only a string array's first element
correctly (an offset counted from the field's start), so the catalogue reads Texture[0] alone, which is all it needs,
rather than changing the global reader (the High look needs the other elements: section 12.4 fixes the reader); pallet
values are masked by the catalogue's own reads (FrameCountTexture & 0xFF, LiquidObject's type & 0xFFFF), not in the
global reader (no change for the tables Models and Characters read); floats are written to the database with six
significant digits (the existing reader's behaviour), so Float[0..1] carry that.

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

With the tuning (section 11), `checkLightCatalog` (`app.cpp`) runs after the liquid catalogue's checks: 14 checks on
Retail and 11 on Classic Beta (the tables' layouts and rows, what the build found in them, the key sampler on made-up
days, the spots and on Retail the map defaults, maps 0 and 1 as sent, the copy-source case on Retail, a map with no
lights and the unavailable shape, the offer's keys), every expected number counted offline from the clients' own
tables. `-terraintile 0 30 49` then passes 321 / 0 (Retail, 2 SKIP) and 319 / 0 (Classic Beta, 2 SKIP); Classic Era
and MoP Classic could not be run on this computer's installs (11.7).

## 4. The player: liquid objects, shader, registry, lifecycle

| Part | Where | What it does |
|---|---|---|
| Materials, registry, meshes | `WmvLiquids.cs` (new) | `WmvLiquidMaterials`: the shader (loaded with `Resources.Load`, no keywords), the approximation constants, one material per liquid type; `WmvLiquidRegistry`: the scene's liquid types by id, their holders (tiles and world-model prototypes), pooling and eviction, one shared `Texture2DArray` per distinct frame list (types that list the same frames share it), the clock, the tallies; `WmvLiquidMeshes`: a surface to a Unity mesh, a tile's liquid objects |
| Session | `WmvTerrainSessionLiquids.cs` (new), `WmvTerrainSession.cs`, `WmvTerrainSessionStream.cs`, `WmvTerrainSessionWorld.cs` | the MH2O mesh built on the worker next to the terrain mesh; the per-load liquid work (frame requests under role `liquidTexture`, answers, the timeout); the session-wide registry while streaming (streamed tiles take the entry's offer and catalogue); MLIQ added to a world model after its prototype is built, on the world path only; the report block, the `[liquids]` log lines, the streamState counters |
| Tile objects | `WmvTerrainBuilder.cs` | the liquid objects built in `Build`, under the tile root, on layer 30, tracked through the tile (`TrackLiquid`), so the tile's disposal, deferred destroy, tallies and the self-test's gate cover them; the tile releases the types it held; their height range is kept apart from the terrain's bounds and widens the `-wmvTerrainTopDown` clip range (an ocean above its seabed) |
| World-model objects | `WmvWorldModels.cs` | `AddLiquids`: a child under the prototype's group object; its meshes join the prototype's arrays (`Runtime.Meshes`), the world-model tallies and the GPU geometry bytes of the pools; released when the prototype is disposed (private and pooled prototypes too; an evicted pooled prototype gives its types back as last used when it was, and its frame arrays are the pools' while only pooled prototypes hold them, the rule of the Stage 08 M2 texture uploads); instances share the meshes through `Instantiate` and are culled through their renderers' own bounds, never counted as groups (`worldGroups`, `groupsDrawn` keep their Stage 06 meaning) |
| Shader | `Resources/WmvLiquid.shader` ("Wmv/Liquid") | one shader, set by uniforms (no keywords): the blending by the material's flag 0x1 -- transparent (`Blend SrcAlpha OneMinusSrcAlpha`, `ZWrite Off`, `Cull Off`, `ZTest LEqual`) or opaque (depth written); the colour by the kind (1.5) -- water and ocean the base colour plus the frame's alpha, magma and slime the frame's colour, so Retail's material 13, 14 and 18 lava, fel and swamp are blended and take their frame's colour; writes authored colour values like the other world shaders (the frame decode pass converts them); unlit. Section 11 adds the zone path for water and ocean and blends transparent materials premultiplied (colour One / OneMinusSrcAlpha), the old maths kept as the fallback |
| Upload | `WmvTerrainMaterials.cs` | `UploadSlice` and `ConvertToRgba`: a type's frames go into one array through the terrain texture path (DXT blocks as stored, rows not flipped); frames of one size but another format or mip count are decoded to RGBA32; a frame of another size, or one that cannot be had, leaves the type in its base colour |
| Clock, state | `WmvMainTerrain.cs` | the liquid clock (pinned by `-wmvAnimTime`), the runtimeState liquid fields, the liquids label of the shot log |
| IPC | `WmvIpcClient.cs` | protocol 19; the `liquids` offer parsed into the catalogue; the report block and the streamState and runtimeState counters |
| Buildings | `WmvWmoBuilder.cs` | one comment only: the Buildings viewer's path draws no liquid and is unchanged |

The approximation's constants (`WmvLiquids.cs`), all provisional until the in-game references; the tuning (section 11)
added the rows so marked and left the first three to the fallback (no zone light):

| Constant | Value | Why |
|---|---|---|
| `HighlightScale` | 0.5 | the fallback's since section 11: water colour = base colour + frame alpha x 0.5: the frames are near-black and carry the ripples in their alpha (1.5) |
| `ShallowAlpha`, `DeepAlpha` | 0.45, 0.8 | the fallback's since section 11: opacity at depth byte 0 and 255, and 0.8 where no depth is stored (an ocean instance without data); the depth byte's scale is UNKNOWN |
| `DepthFull` | 3.25 | section 11: the depth in yards at which the zone path's colour and opacity reach their deep values; MEASURED at one spot |
| `ZoneHighlight`, `ZoneHighlightFloor` | 0.34, 0.13 | section 11: the neutral highlight added after the blend, 0.34 x saturate((frame alpha - 0.13) / 0.87); MEASURED at one spot |
| `BedFactor` | 0.75 | section 11: what is behind the water is kept at (1 - alpha) x 0.75; MEASURED at one spot, mechanism UNKNOWN |
| `PlanarRepeatFactor` (`Wow/World/TerrainLiquidMesh.cs`) | 2 | section 11: water and ocean repeat 2 x Float0 times per chunk, on the game's axes; MEASURED for type 5 |
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

**Compared at one spot (2026-10-19/20, section 11).** No reference had arrived when the stage was committed
(2026-10-19); the owner chose to commit with the look recorded as an untuned approximation and to tune it later. The
references then came from the Southfury River by Orgrimmar (Retail PTR), not from the four spots above: the owner plays
Horde, and took them on a sandbox server. Section 11 compares them and tunes the water's colour, opacity, highlight and
planar uv; the stored-uv scale stays untested. The roadmap's exit tiles (Retail 29_48 Stormwind Harbor, Era 36_49 Lake
Everstill) are still NOT compared with references, so its exit test, water tiles against references on Retail and Era,
is open: one Retail spot is compared, no Era spot. The final merge-readiness pass (12.11) leaves it open and says what
would close it. On 2026-10-21 the owner approved revised acceptance criteria that supersede that test (section 13): it
is not passed, and the stage is closed without it.

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

- **The High look is tuned at one spot, not the game's** (new, section 12): the transmission, the water colour's gains,
  the reflection's strengths and the ripples' scales and strength are MEASURED at the one Retail spot (type 5, one zone
  light, noon, Liquid Detail High), `noZoneGain` at the same spot in Classic Beta (type 1240, no zone light); every
  other water and ocean type and place takes them as INFERRED. Visual parity is NOT claimed; what is still different is
  in 12.10 (the hue, the far waterline, the reflection's fall-off, the near pebbles, Beta's PBR water), classified in
  12.11: differences of the scene, which wait for Stage 12's light, terrain and sky, and the water's own, whose
  mechanisms are UNKNOWN.
- **The bed's shift** (new; FIXED in p20e, 12.11): the viewer drew the bed shifted by a constant, the wrong way (+1.2 to
  +2.3 px at 2,560 on p20d); that was the ripple map's own mean tilt carried into the refraction offset, not refraction,
  and p20e takes the tilt out (the shift about 0, the wobble's spread on target as before). A view-space offset (12.7,
  H6) was tried and reverted (12.10). The game's own shift, the bed drawn lower than in air (12.2), is not drawn; its
  mechanism is UNKNOWN.
- **Sloped water** (new; FIXED in p20e, 12.11 F1): the HIGH path was chosen by the view ray going down, so a river
  rising above the eye split into a High and a Low part on the horizon row; it is now chosen by the triangle's own
  facing. The shading still takes the world's up (the Fresnel, the mirror and the path to the bed), so the rising part
  of such a sheet reads as a pale haze (12.11); how the game shades sloped water is UNKNOWN.
- **Screen-space reflection** (new, by design): only what is on screen is reflected, up to 128 yd; in a steep view only
  the far water near the top of the frame mirrors anything (12.10).
- **A camera under the surface** (new, PROVEN_SOURCE; MEASURED at Stormwind Harbor, 12.11 F2): the Low paths draw the
  water there, and while the High look is installed the material writes depth, so a blended object behind the water
  (above it) is hidden: from under the harbor's water its waterfall and a ship's mast are missing. Low draws such
  objects over the surface at full strength, which is not the game's either. Kept as a documented approximation: a fix
  would split the draw into two materials and double the vertex work. Blended objects under the opaque High water are
  hidden too (12.7, H8; 12.10).
- **The grab's cost** (new, MEASURED): about 0.14 ms of GPU a frame uncapped and 35 MB of GPU memory on the deployed
  pair (12.11: +0.140 / +0.132 ms over Low, ocean / river; p20c's means in 12.9 +0.141 / +0.122), the capped frame rate
  unchanged; it runs while any liquid object is alive, in view or not. Skipping it on frames without water in view would
  save 0.06-0.09 ms of GPU, would seldom apply with the 3,000 yd far plane, and would let the render graph free and
  re-create its textures: investigated, not built (12.11).
- **What the look's review could not explain** (new; class C of 12.11, mechanisms UNKNOWN): the water's own warmth
  (short of red even with the game's scene), a pale extra term of the far water (lighter than the bank it mirrors), the
  far waterline's light line beyond what the bank gives, the reflection's fall-off below the far waterline, the ripple
  ovals' tint, and the game's own downward bed shift. A High shot facing east from the Southfury stand and one at
  another time of day would settle most of them.
- **The Low look is tuned at one spot, not the game's** (new, section 11; since section 12 drawn only with
  `WMV_WATER=low` and where the High look does not apply): water and ocean take the zone light's colours and alphas at
  the camera (slot 0, a fixed 12:00; the selection rules INFERRED); the opacity's depth scale (3.25 yd), the highlight
  (0.34 above 0.13), the bed factor (0.75) and the planar uv (2 x Float0 on the game's axes) are MEASURED at one Retail
  spot (the Southfury River: type 5, one zone light, noon, Liquid Detail Low) and applied to every water and ocean type
  as INFERRED; the frame rate (15 a second) and the stored-uv scale (1/256) are still chosen constants. Without a zone
  light the old look stays (the minimap colour, 0.45 / 0.8, highlight 0.5). Visual parity is NOT claimed; the roadmap's
  exit tiles are not compared (section 6); terrain light, fog and sky are Stage 12's.
- **A bright glint sector** (new, the Low look, MEASURED; mechanism UNKNOWN): the game's brightest highlights at the
  spot lie in one world direction from the stand; the viewer has no such term, and no global constant can match it
  (section 11.6).
- **The river bed's fine contrast** (new, the Low look, MEASURED; mechanism UNKNOWN): near the bank the bed shows
  through the water with about twice the game's contrast (section 11.6).
- **Lights without water colour** (new): a light whose four water colours are 0 at 12:00 leaves the water in the old
  look: Classic Beta almost everywhere (its few point lights with colour are ignored as well, since the base light has
  none) and three Retail zone lights (Burning Steppes, Badlands, Searing Gorge), where the water switches at once at the
  border. How the game draws them is UNKNOWN (section 11.4). On the High path such water takes saturate(MinimapStaticCol
  x 0.3125) (12.5): MEASURED at one Classic Beta spot, the type's hue kept (12.10).
- **World-model water takes the camera's zone light** (new, UNKNOWN in the game): MLIQ water is coloured by the light
  at the camera like terrain water; Dire Maul's interior water is now olive, from map 429's default light (11.7).
- **Classic Era without a reference** (new): its install was in a failed update (29,447 of the build's files and 31
  tables missing at the suite's run) until Battle.net's own update of 2026-10-21 restored it (12.11). Since then it
  passed on the deployed pair: `-terraintile` 322 / 0, `acc_era` 79 / 0, `dm_era` 20 / 0, `life08` 70 / 0 with its Era
  tail, its light catalogue against the offline reference (the 12 tables it shares with the audit copy byte-identical to
  it, the sky equal to the tables in 178 of 178 values), its world models in Buildings as in section 7, and the High
  look at the Durotar spot, Lake Everstill and the sloped river with no defect seen. There is no Era in-game reference:
  the warm terracotta water at Durotar (point light 204) and the teal lake with an olive rim (map 0's default light 1)
  are the tables' colours drawn by the Retail-tuned model (INFERRED), and whether the game draws them so is UNKNOWN.
  Its visual parity stays UNVERIFIED: accepted on 2026-10-21 as an explicit limitation, kept for a future comparison
  (section 13).
- **MoP Classic without a reference** (new): its storage did not open until a Battle.net update on 2026-10-20; in the
  final pass (12.11) it passed on the deployed pair (`-terraintile` 315 / 0 on 5.5.4.70469 and 5.5.4.70531; on 70531
  `acc_mop` 41 / 0, its light catalogue against the offline reference, and the High look at the Durotar spot with no
  defect seen), but there is no MoP in-game reference to compare with: its visual parity is UNVERIFIED too (accepted,
  section 13).
- **Stored texture coordinates** (new): slime and terrain magma show staircase bands and world-model lava a radial
  pinch at 1/256 per unit (section 5.1); the scale of MH2O's u/v and MLIQ's s/t is UNKNOWN.
- **Texture slots 1-5 are not drawn** (new), but for the ripple map: the High look draws the first file of slots 1-5
  whose name contains "bump" as a normal map (the rule INFERRED, 12.3); procedural depth, foam and `Magma0-2` are not
  drawn, their roles UNKNOWN.
- **Beta's PBR water types** (new): their slot 0 is a single 1,024 x 1,024 texture, not a ripple frame list; they draw
  pale (1250, 1344) or dark grey (1284). The Beta-only FFT tables are not read. On the High path they draw flat (no
  ripple map), with Retail's transmission and their MinimapStaticCol x 0.3125: at the Southfury spot too dark, teal-grey
  and too opaque near the camera against the game's clear water and warm reflections (12.10).
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

The tuning (section 11). Host (`Source/`): `games/wow/LightCatalog.{h,cpp}` (new: the light catalogue),
`games/wow/CMakeLists.txt`, `games/wow/wdc3file.{h,cpp}` (`copyRows()`), `core/GameDatabase.cpp` (`SCHEMA_VERSION`
21), `wowmodelviewer/UnityIpcServer.{h,cpp}` (the `lights` key), `modelviewer.{h,cpp}` (the light catalogue's
lifetime, the lights in `RequestLoad`, the load's log line), `app.cpp` (`checkLightCatalog`). Data:
`bin_support/dbd/Light.dbd`, `LightParams.dbd`, `LightData.dbd`, `ZoneLight.dbd`, `ZoneLightPoint.dbd` (new),
`bin_support/wow/12.0/database.xml`; `THIRD-PARTY-NOTICES.txt` (the five light definitions, adaptations of the public
table definitions). Player (`Tools/UnityRendererProject/Assets/`): `Scripts/WmvLiquidLight.cs` (new),
`Scripts/Wow/World/LiquidLights.cs` (new), `Scripts/WmvLiquids.cs`, `WmvIpcClient.cs`, `WmvMainTerrain.cs`,
`WmvTerrainSession.cs`, `WmvTerrainSessionLiquids.cs`, `WmvTerrainSessionStream.cs` (a comment),
`WmvTerrainSessionWorld.cs`, `WmvLifecycleSelfTest.cs`, `WmvLifecycleSelfTestLiquids.cs`,
`Scripts/Wow/World/LiquidFormat.cs`, `TerrainLiquidMesh.cs`, `AdtModel.cs` (a comment), `Scripts/Wow/WmoLiquid.cs`;
`Resources/WmvLiquid.shader`. Tests (`Tools/UnityRendererProject/Tests/`): `LiquidLightTests.cs` (new),
`LiquidTests.cs`, `LiquidCorpusTests.cs`, `Runner/Program.cs`, `Runner/WowParserTestRunner.csproj`, `README.md`.

The High look (section 12). Host (`Source/`): `games/wow/LiquidCatalog.{h,cpp}` (the ripple map per type,
`textureIds()`), `games/wow/wdc3file.cpp` (string-array elements), `games/wow/LightCatalog.{h,cpp}` (the sky),
`core/GameDatabase.cpp` (`SCHEMA_VERSION` 22), `wowmodelviewer/modelviewer.{h,cpp}` (the served set),
`UnityIpcServer.{h,cpp}` (the `bump` and `sky` keys), `app.cpp` (the checks). Data: `bin_support/wow/12.0/database.xml`
(`Texture` as an array, `SkyBand1Color`), `bin_support/dbd/LightData.dbd`. Player
(`Tools/UnityRendererProject/Assets/`): `Scripts/WmvWaterGrabPass.cs` (new), `Resources/WmvWaterGrabDepth.shader` (new),
`Resources/WmvWaterGrabBlur.shader` (new), `Scripts/Wow/World/LiquidHigh.cs` (new), `Resources/WmvLiquid.shader`,
`Scripts/WmvLiquids.cs`, `WmvTerrainSessionLiquids.cs`, `WmvTerrainMaterials.cs`, `WmvIpcClient.cs`,
`WmvLiquidLight.cs`, `WmvMainTerrain.cs`, `WmvModelBuilder.cs` (`-wmvWaterTune`), `WmvTerrainComposite.cs`,
`WmvLifecycleSelfTest.cs`, `WmvLifecycleSelfTestLiquids.cs`, `Scripts/Wow/World/LiquidFormat.cs`, `LiquidLights.cs`.
Tests (`Tools/UnityRendererProject/Tests/`): `LiquidHighTests.cs` (new), `LiquidLightTests.cs`, `Runner/Program.cs`,
`Runner/WowParserTestRunner.csproj`, `README.md`.

The final pass (12.11) added no file and changed none outside these lists: its fixes are in
`Resources/WmvLiquid.shader`, `Scripts/Wow/World/LiquidHigh.cs`, `Scripts/WmvLiquids.cs`,
`Scripts/WmvLifecycleSelfTestLiquids.cs`, `Tests/LiquidHighTests.cs` and `Tests/README.md`.

Documents: this file; `roadmap.md` (the Liquids row of section 3 and milestone 09 of section 5).

## 11. Tuning against the in-game references (2026-10-20)

**Superseded look (2026-10-20).** Section 12 (the High look) supersedes this section's look wherever its HIGH path
draws; this look remains with `WMV_WATER=low` and wherever that path does not apply.

The owner's references arrived on 2026-10-19, and the owner chose the full tuning ("zone-light colours + constants").
This section replaces the colour, opacity, highlight and planar-uv choices of 1.5 and 4, and adds the light tables to
the host and to the liquids offer (11.3); the parsing, the liquid geometry's positions, the streaming lifecycle, magma
and slime are unchanged. The zone light's rules, which the code comments refer to (`LightCatalog`, `checkLightCatalog`,
the light definitions, `database.xml`), are those of 11.2 to 11.5. The comparison rigs and their outputs are outside the
repository (`D:\wmv-09\tune`).

### 11.1 The references and the camera fit

The owner took them on a sandbox server running the Retail PTR client 12.1.7.70424 (the owner plays Horde: the spot is
not one of the roadmap's exit tiles, section 6), with a small helper addon that records each shot's position, character
facing, game time, build and graphics settings. One spot: the Southfury River bank west of Orgrimmar, Kalimdor (map 1),
X 1261, Y -3948, ground 18.15-18.20, Slow Water (type 5) at 17.881. Four shots at Liquid Detail **Low**, screenshot
quality 10, game time 11:53-11:57: #5 ACROSS, #7 NEAR, #8 WATERLINE (facing 80-81 degrees, counter-clockwise from north)
and #9 LANDMARK (facing 71). The High shots (#6, #10-#12) show another path (clear shallows, the far bank mirrored) and
are not used here (section 12 uses them); the earlier Low shots #1-#4 (game time 18:44-18:46 and 06:48-06:49) serve only
the glint-sector check of 11.6. A Classic Beta set at the same spot (8 shots) is not used: Beta's tables carry no water
colour there (11.4).

The viewer's camera stands where the game's did: eye 3.32 / 2.82 / 3.27 / 3.27 yd over the water, yaw and pitch fitted
(the yaw within 0.2 degrees of 360 - facing), the geometry matching to about 1 px (palms and acacias placed by the
client's own MDDF). The game's focal length is 1,403 px at 2,560 (vertical field 54.3 degrees, MEASURED from the scale
of single-depth objects between reference and render), the viewer's vertical field 60 degrees, so the references are
compared downscaled by 0.590 (never upscaled) to the viewer's 1,665 x 956 viewport, with the same measurement tool on
both sides (CIELAB by forward-distance band; highlights = blobs at least dL* 10 above the local water).

### 11.2 Where the game's water colour comes from

The water and ocean frames are near-black with the ripples in alpha (1.5), so the colour is not in the texture. At the
spot the light is the ZoneLight polygon "Durotar" (ZoneLight 139 -> Light 204; the spot is 123 yd inside it), its
clear-weather slot LightParams 5985, at 12:00: river close (64, 62, 35), river far (62, 54, 47), ocean close (82, 90,
94), ocean far (55, 61, 69), water shallow / deep alpha 0.40 / 0.80, ocean 0.75 / 1.00 (PROVEN_BYTES; the column names
from public table definitions, checked against the bytes: day cycle, value ranges, the pairs; which pair is ocean and
which river is INFERRED and supported by this spot). Retail 12.1.5 and the PTR agree on every value used there. One
model fits every band of #5 and #8 to 1.7 bytes RMS: the close/far colour and the 0.40 / 0.80 alpha lerped by depth,
blended over the river bed (fitted as (104, 67, 40), a darker, wetter version of the dry bank); the Kalimdor default
light cannot fit (4.3, with an impossible bed), the minimap colour (cyan) not at all.

### 11.3 What changed

| Part | Before | Now | Grade |
|---|---|---|---|
| Water and ocean colour | the type's MinimapStaticCol (cyan for Water, Ocean and Slow Water) | the zone light's close/far pair at the camera: the river pair for the water kind, the ocean pair for the ocean kind, lerped by depth | values PROVEN_BYTES; the selection rule INFERRED (11.4) |
| Opacity | lerp(0.45, 0.80, depth byte / 255) | lerp(shallow, deep, k) with the zone light's alphas, k = saturate(depth / 3.25 yd); k = 1 where no depth is stored | alphas PROVEN_BYTES; 3.25 yd MEASURED at one spot (2.5-3.75 with the eye height) |
| Depth | the byte / 255 | the depth in yards, decoded by the instance's second word (11.5), in uv1.y; uv1.x keeps the byte / 255 for the fallback; (1, -1) where none is stored | PROVEN_BYTES (census) |
| Highlights | base + frame alpha x 0.5, before the blend (weak, clipped yellow-green over the bed) | h = 0.34 x saturate((frame alpha - 0.13) / 0.87), neutral, added after the blend | MEASURED at one spot (coverage, tails, peak contrast, tint) |
| River bed under the water | blended with weight (1 - alpha) | (1 - alpha) x 0.75 (premultiplied output: rgb = alpha W + h, output alpha 1 - 0.75 (1 - alpha)) | MEASURED at one spot; mechanism UNKNOWN |
| Planar uv (water, ocean) | (east, north) / 33.33 yd x Float0 | (game X, game Y) / 33.33 yd x 2 Float0, then Float1 and the floor shift as before (MLIQ: the group frame's x, y, as before) | MEASURED for type 5 (density, blob size, autocorrelation, streak orientation); the factor 2 agrees with public format documentation of an editor; other types INFERRED; the 180-degree sense UNKNOWN |
| No zone light (an older host, unread tables, a light without water colours) | -- | exactly the old colour, opacity and highlight (in the premultiplied form), with the new uv | PROVEN_RUNTIME (GPU self-test, Beta pictures, 11.7) |

Not changed: magma and slime (the frame's colour, opaque or blended by the flag), the frame rate (15 a second,
UNKNOWN). Not added: time of day (the colours are read at a fixed 12:00), weather (the clear-weather slot 0 only), the
underwater slot, fog, sky and terrain light (Stage 12), the river bed's fine contrast under the water (11.6).

Host: `LightCatalog` (new) reads Light, LightParams, LightData, ZoneLight and ZoneLightPoint once per client load
(`verifiedLayoutOnly`, five new dbd files; `SCHEMA_VERSION` 20 -> 21) and makes, per map on its first load, the
`lights` object that the `liquids` offer of `loadTerrainTile` and `loadWorld` now carries (protocol 19 kept: an
additive key, which a protocol-19 player without this change ignores; streamed tiles copy the entry's offer, lights
included). `WDC3File` keeps section 0's copy table (`copyRows()`), the only link from a copied LightParams row to the
LightData of its source. Retail map 0's object is the largest of all Retail maps (129,669 bytes; an offline pass over
583 maps); the host's send cap (256 MiB unsent) and the player's line limit (192 MiB) are far above it. Player:
`Wow/World/LiquidLights.cs` (pure: the selection, the ease), `WmvLiquidLight.cs` (once per rendered frame, the main
camera's game position -> the globals `_WmvZoneLight`, `_WmvRiverClose/Far`, `_WmvOceanClose/Far`, `_WmvWaterAlpha`,
`_WmvOceanAlpha`), the shader's zone path, the materials' kind and blend factors (colour One / OneMinusSrcAlpha, alpha
SrcAlpha / OneMinusSrcAlpha), the meshes' depth and uv.

### 11.4 The light at the camera (INFERRED rules, PROVEN_BYTES data)

- Zones: the map's ZoneLight polygons (Pos = game X, Y: city polygons contain their flight masters, PROVEN_BYTES), Z
  band applied; polygons nest (357 pairs in Retail), so the innermost wins (sent smallest area first, ties by zone id),
  unconditional ones before those with a PlayerCondition (39 Retail polygons, 33 of them the 12.x Eversong and
  Zul'Aman ones on map 0). A zone's Light row is not filtered by its map (76 Retail polygons use another map's Light).
  Winding is mixed in the data; the inside test is even-odd.
- Points: Light rows of the map away from the origin and not named by a ZoneLight (a zone's own Light row is not used
  as a point: UNKNOWN in the client); positions and falloffs are yards (PROVEN_BYTES); weight 1 inside the start
  radius, linear to 0 at the end (3D distance), applied weakest first (ties by light id) over the base light. They are
  applied only when the base light has values: where it has none (Classic Beta), the point lights that do carry water
  colours (6 on Beta's map 0, 1 on map 1) are ignored too.
- Default: the map's origin row (falloff end 0 first), else Light 1 (the client's own fallback is UNKNOWN; 227 Retail
  maps have no origin row).
- Values: slot 0 at half-minute 1440 (12:00): the key there, else linear between its neighbours (every light at the
  spots has a key at 1440); colours masked to 24 bits (stored signed); LightData by the param id or its copy source. A
  light whose four water colours are all 0 at 12:00 has no values: the water there takes the fallback look. That is
  Classic Beta's tables almost everywhere (every map default and zone light of Eastern Kingdoms and Kalimdor; 6 of 85
  and 1 of 132 of their lights carry a water colour; where Beta's water colour comes from is UNKNOWN), and three Retail
  zone lights on map 0 (Lights 7, 21 and 72; the zones are Burning Steppes, Badlands and Searing Gorge), where the water
  turns to the old look at once at the border. How the game draws an all-zero water colour is UNKNOWN, so the fallback
  is kept.
- Where: once per rendered frame at the main camera, in the terrain view (single tile, world and streaming scenes).
  World-model (MLIQ) water takes the light at the camera like terrain water (UNKNOWN in the game; Dire Maul's interior
  water now takes map 429's default light, 11.7).
- On a change the colours ease (factor 1 - exp(-dt / 0.25 s) a frame); they switch at once on the first sample, a
  camera jump over 60 yd, another lights object, a change of validity and whenever the clock is pinned
  (`-wmvAnimTime`), so pinned pictures are reproducible. The time constant and the jump distance are documented
  choices, not the game's.

Per client: Retail and the PTR read their own layouts (LightParams CAE394E7 / A7F31923, LightData 6ABF2921 /
360DA016); Classic Beta shares the PTR's; Classic Era and MoP Classic share 51C96BAD / 70C6CD80 and ZoneLight
BA8274F0 (column mapping PROVEN_BYTES from rows shared with Retail; Era's WaterDeepAlpha column INFERRED from the same
layout). Light (5F16BC84) and ZoneLightPoint (DE2377FB) are the same in every client. Era has no ZoneLight rows: its
zones are lit by large point lights (the Durotar spot: Light 204 at full weight, 903 yd away inside its 1,023 yd start
radius; river (158, 97, 71) / (113, 82, 63), alpha 0.50 / 1.00 -- redder and brighter than Retail; computed offline,
not run in the viewer, no Era reference to compare). MoP Classic 5.5.4.70449's layouts were not seen (its storage does
not open on this computer); the definitions list 70268's.

### 11.5 Depth in yards

The MH2O depth byte is a quantised depth (PROVEN_BYTES: every root of 1.2's census that has a depth byte -- Retail 12.1.5
15,384 roots, Era 839 of its 1,127 (the other 288 are not on this computer), Beta 2,022; MoP's storage does not open):
with D the liquid vertex height minus the terrain's outer vertex under it, byte = clamp(floor(9 D), 0, 255) for most
instances (99.70 % of Retail
vertices, 99.85 % of Era's, 99.89 % of Beta's) and clamp(ceil(12 D / 7), 0, 255) where the instance's second word is 2
or 42 (ocean-class instances: 99.51 % / 98.81 % / 99.93 %). The other word values (0, or a LiquidObject id) take the
first scale, whatever the type. The player decodes the byte back to yards by that rule (the middle of the step:
(b + 0.5) / 9, or (b - 0.5) x 7 / 12 and 0 for byte 0), MLIQ by the first scale (its water carries the same
quantisation, 98.5 % Era / 89.0 % Retail where the group has a floor). So the opacity reaches the deep value at the same
3.25 yd in a river and in the sea; with k = byte / 28 (the first proposal) the sea would have needed about 16 yd. 0.44 %
of Retail's vertex rows carry bytes unrelated to depth (most alternate 0 / 255); none on Azeroth or Kalimdor.

### 11.6 Results at the spot

Player p19c (Assembly-CSharp MD5 B3654AE7...), host runtime exe 0FCFEAF7 / core 558E0FB6 / wow.dll 2055A4DB, the four
fitted cameras, clock pinned; the player logs `liquid light: zone 139 light 204 params 5985 (map 1)` once the camera
stands at the spot. Only the water changed: outside it the pictures are pixel-identical to Stage 09's (p19b), and the
share of changed pixels equals p19b's water share exactly.

| Metric (whole water, four shots) | Reference (Low) | Stage 09 (p19b) | Now (p19c) |
|---|---|---|---|
| Base colour sRGB | 82-92, 64-72, 46-48 | 99, 148-150, 148-151 | 86-90, 73-76, 50-52 |
| L* / a* / b* | 28.3-31.5 / 4.8-5.2 / 14.0-17.7 | 57.8-58.6 / -16.8..-16.5 / -6.0..-5.4 | 31.5-32.7 / 1.5-1.7 / 14.8-17.7 |
| Near (2-3 yd) -> far (24-32 yd) L* | 35.3 -> 26.0 | 55.8 -> 59.0 | 33.5 -> 29.0 |
| Highlight coverage dL* 10 | 8.0-11.0 % | 3.1-7.5 % (1.7-2.3 with the bed blurred out) | 8.5-13.0 % |
| Coverage dL* 15 / 20 / 30 | 4.3-7.1 / 2.5-4.7 / 0.6-2.2 % | 0.5-1.4 / 0-0.2 / 0 % | 4.9-6.9 / 3.0-4.1 / 0.9-1.4 % |
| Peak contrast p50 / p95 (dL*) | 16.1-17.7 / 32.4-36.4 | 12.8-13.8 / 18.8-21.1 | 13.6-16.4 / 32.5-34.0 |
| Brightest peak L* | 86-94 | 85-87 | 75-78 |
| Highlight tint against the water (da*, db*) | -0.8..-0.6, -0.9..-0.6 | -3.4..-0.9, +3.4..+11.3 | -1.1..-0.9, -1.3..-1.0 |
| Density per 100 yd2 at 16-24 / 24-32 / 32-48 yd | 169 / 90 / 31 | 66 / 38 / 16 (33 / 26 / 10 with the bed blurred out) | 168 / 91 / 34 |
| Own-bank edge (#7), bank -> water L* | 46.3 -> 38.0 | 51.5 -> 57.8 | 51.5 -> 37.2 |

Pictures (reference | Stage 09 | now): `D:\wmv-09\tune\zl\render\r1\pictures`. The same cameras against the Stage 09
host (no `lights`): the old colours ((98-99, 148-150, 148-151), L* 57.6-58.6) with the new uv (densities 135 / 46 / 13
at 16-48 yd against p19b's 66 / 38 / 16), `D:\wmv-09\tune\zl\render\r1_fallback\pictures`.

What is still different, and why it was not tuned away:

- **A bright glint sector** (MEASURED): the reference's brightest highlights (L* 86-94) lie in one fixed world direction
  from the stand (92-100 % of its blobs of peak dL* 35 or more at 2-16 yd lie on rays at 295 degrees or more clockwise
  from north, median 313-316), strong within 16 yd, weak at 16-24 yd, gone beyond 24 yd. The direction is tied to the
  world, not the screen (#9, turned 10 degrees further, keeps the median ray at 316 degrees), and is the same in the
  dusk and dawn shots #1-#4; only the colour follows the time. Outside that sector the viewer's highlights are already
  brighter than the game's (brightest +7 to +13 L* per band; coverage at dL* 30 1.20 % against 0.11 %), so no global
  gain, floor or curve can match it; the constants stay 0.34 / 0.13. Its mechanism is UNKNOWN (one stand: a direction
  and a patch of water cannot be told apart).
- **The river bed's fine contrast** shows about twice as much through the water near the own bank (0.50-0.52 of the dry
  bank's texture against 0.28-0.29); the mechanism is UNKNOWN and there is no cheap path; not changed.
- **Terrain light** (Stage 12): the viewer's banks and bed are brighter and less red than the game's, so the water's a*
  stays about 3.4 lower, its base 3.1-3.7 L* brighter outside the glint sector, and the far waterline's first water row
  bright (+7.7..+7.9 L*) where the game's is dark (-2.9..-2.1); an offline re-composite with the bed darkened to the
  game's bank light closes the a* gap, not the bright row (INFERRED).
- **One spot, one type, one light, noon, Low.** The depth scale, the highlight gain and floor, the bed factor and the
  uv factor and axes are measured there only; a second water body (ocean, another zone) would test them.

### 11.7 Tests

| Test | Result |
|---|---|
| Host `-terraintile 0 30 49`, Retail 12.1.5.70448 (player p18o; again with p19b, whose loads carry `lights`) | PASS 321 / 0, 2 SKIP (307 before + 14 new: `checkLightCatalog`) |
| Host, Classic Beta 1.60.1.70450 | PASS 319 / 0, 2 SKIP (308 + 11 new; the new SKIP: the copy-source check is counted for Retail only) |
| Host, Classic Era 1.15.9.70003 | not runnable: 49,294 of the build's files are not on this computer (the viewer's count: 43,634 viewer files and 31 tables, Map, LiquidType and Light among them): FAIL 252 / 7, 4 SKIP, the seventh failure the new "light catalogue is ready"; the unchanged Stage 09 host fails the same way (252 / 6 / 4) |
| Host, MoP Classic 5.5.4.70449 | not runnable: the storage does not open (CASC error 2) |
| Light catalogue against an independent offline implementation (Retail and Beta maps 0 and 1) | identical (coordinates within 0.05 yd, colours exact) |
| Pure runner (the final code) | 2,168 passed, 0 failed (2,136 + 32: 30 in the new `LiquidLightTests`, 2 in `LiquidTests`); 2,167 on p19c's code, before the point-light order check was added |
| Player self-test (`-wmvLifecycleTest`, p19c) | 1,012 passed, 0 failed (one SKIP, as before); the GPU checks of the zone path (water, ocean without depth) and of the fallback within 3e-3 of the formulas |
| Regressions: runs | private copies (`D:\wmv-09\tune\zl\regress`): new = exe 0FCFEAF7 + p19c, old = Stage 09 (F85B31A9 + p19b), no-liquid masks = F85B31A9 + p18o; hidden desktops, no game client running. Retail acceptance 79 / 0, Dire Maul 20 / 0, the 24,000-yard flight with eviction 20 / 0, `life08b` 12 / 0, Stormwind Harbor 17 / 0, the zone-border runs 56 / 0 each (clock pinned and free), three ocean flights 23 / 0 each; Classic Beta acceptance 51 / 0: all PASS, Stage 09's counts. `life08` 64 / 6, every failure in its Classic Era tail (steps 86-94); its Retail part passes with every liquid tally 0 after each Clear; the Stage 09 build gives 65 / 5 there today (Era, below). Every new log: 0 malformed, failed, dropped or unknown-type liquid counts, 0 exceptions, 0 `liquidTexture` refusals |
| Regressions: pictures | 20 before / after pairs at 1:1 (`D:\wmv-09\tune\zl\regress\pics\index.md`); the Stage 09 build is pixel-identical to its section 5 run (`c1b`) in all 20. Only liquid pixels changed (masks from the no-liquid player): outside the old liquid area only 1-step waterline edge pixels (at most 3,245 px), and up to 37 steps on Beta's canals where water over a near-white floor rounds differently; the liquid geometry log lines are identical. Retail ocean and slow water take the zone path (the harbor's ocean (85, 142, 148) -> (10, 39, 51), dark and nearly opaque: zone 1859, light 9651, params 6080). Magma (stored u/v), Blackrock's lava (world model) and slime: 0 changed pixels. Classic Beta: the fallback colours everywhere (no zone or default light of maps 0 and 1 has water colours) with the new planar pattern (PBR ocean (160, 186, 177) -> (163, 186, 177); type 1284: 5 and 1,306 px). Dire Maul's interior water now takes map 429's default light 179 (params 175): cyan (92, 156, 188) -> olive (79, 93, 61); whether the game colours interior world-model water from the map's light is UNKNOWN |
| Regressions: Buildings | six world models with liquid, Stage 09 / new / new again: Retail 108238 and 4235933 and Beta 7962574 byte-identical, and equal to section 7's; the Era ones blocked by the install (107243 and 111185 identical across the three but not to section 7's, their group files no longer on this computer; 108238 not loaded); no liquid line in any Buildings log. Characters: the known `TabardDetails::tier` variance on both builds (section 7) |
| Zone-border crossing (Retail map 1, `border`, clock pinned) | the player logs, in order: zone 142 (light 208, params 5951: the tile's framing, before the first camera request), zone 139 (204 / 5985: the spot), zone 153 (193 / 5944: the Barrens side, 1261, -3820), zone 139 (1261, -3826), zone 140 (239 / 6033: the Durotar Org side, 1408, -4144), then 139 / 153 and 139 / 140 on two flights across the borders, and "none -- no scene with zone lights" at Clear; the borders lie where the host's dump puts them (Barrens Main from Y -3823.0 going west, Durotar Org from 1400.9, -4134.5). The river patch goes from (84, 72, 56) on the Durotar side to (64, 85, 88) on the Barrens side; the shot right after a crossing is pixel-identical to the settled one, and the way back gives the first visit's picture exactly. With the clock free (`bordere`) the sequence is the same and the immediate shot is already settled ((64.2, 85.1, 87.8) against (64.2, 85.0, 87.7)). Stormwind Harbor logs zone 1859 (light 9651, params 6080) in four runs; the long flight logs 17 light changes over Elwynn and Burning Steppes, no error |
| Timing | `LiquidLights.Evaluate` in .NET 10 on Retail map 0 (82 zones, 206 points): 0.33 microseconds per call in a zone, 0.43 in none (all 82 polygons tested), 0.53-0.54 with six point lights applied, 0.44 over 4,096 random points; map 1 0.18-0.26 (MEASURED). p19c's version allocated 272 bytes per call where point lights applied; the final code reuses one list, in the same order, and allocates nothing (re-measured on the final code: 0 bytes per call with six point lights, 0.35-0.58 microseconds). In the player it runs once a frame; Unity's runtime is slower (a few microseconds a frame, INFERRED). Frame times unchanged: Retail ocean flight with the clock free, uncapped mean 1.709 / 1.742 ms against 1.696 / 1.764 ms; capped medians and 95th percentiles 16.67 ms on both, no frame over 33 ms; the 24,000-yard flight 6 / 3 frames over 33 / 50 ms against 7 / 3, the longest 55.9 against 112.4 ms. No GUI round over 50 ms in any Retail or Beta run |
| Classic Era, MoP Classic | not run with the tuning. Era: the install of the host row. With `SCHEMA_VERSION` 21 the host cannot reuse its schema-20 Era database cache (built while Map.db2 was on disk), rebuilds the database from the install and finds 0 maps, so `acc_era` and `dm_era` fail from the map list on ("no row for MapID"); an install fault: the Stage 09 host fails the same way without its old cache (the host row above), and its `dm_era` fails today too (a world model not installed). MoP: the storage does not open |
| Review | one independent pass, three low findings: a code comment overclaimed Beta's zero colours (reworded); the all-zero Retail zone lights (documented, 11.4); this document (updated). After p19c was built, `Evaluate` was made allocation-free (Timing) and the header comment of `LiquidLights.cs` reworded; the pure runner and the allocation re-measure ran on the final code; the self-test, the pictures, the regression runs and the first timing on p19c's |

## 12. The High look (2026-10-20)

Section 11 tuned the water to the references taken at Liquid Detail Low. On 2026-10-20 the owner judged that look awful
and chose the game's Liquid Detail High instead, "High with reflections": clear water with the river bed seen near the
camera and fading with distance, gentle ripples and no sparkle blobs, the far bank mirrored, and a plain sky in the
reflections until Stage 12 brings one. This section is that look, in the order the work was done: the references and
what they measure (12.1, 12.2), the client data it uses (12.3), the design (12.4), the tuned values (12.5) and their
scores (12.6), the review and its fixes (12.7), the tests (12.8), the frame time (12.9), what is still different (12.10)
and the final merge-readiness pass with its fixes and the validation of the deployed build (12.11). Like section 11 it
is the viewer's approximation of the picture, MEASURED at one spot and applied elsewhere as INFERRED; nothing in it is
claimed as the game's shader. The rigs and their outputs are outside the repository (`D:\wmv-09\tune\hi`).

### 12.1 The references and the camera fit

The High shots of the set of 11.1, at the same stand on the Southfury River bank (Retail PTR 12.1.7.70424, Slow Water at
17.881): #6 ACROSS, #10 ACROSS, #11 NEAR and #12 WATERLINE, at Liquid Detail High (the records' settings:
graphicsLiquidDetail 3, waterDetail 3, rippleDetail 2, reflectionMode 3, where the Low shots have 0, 0, 0, 0;
PROVEN_RUNTIME). Their cameras were fitted anew and absolutely (MEASURED): 1,300-1,430 land blocks 53-235 yd away
matched between each reference and a no-liquid render, their 3D points from the client's terrain, robust least squares
with the focal length free.

| Shot | Camera X, Y, Z | Eye above the ground (yd) | Yaw | Pitch |
|---|---|---|---|---|
| #10 ACROSS | 1261.107, -3947.586, 20.710 | 2.560 | 279.542 | -2.567 |
| #11 NEAR | the same | 2.560 | 279.510 | -25.534 |
| #12 WATERLINE | the same | 2.560 | 280.185 | -8.117 |
| #6 ACROSS | 1261.000, -3948.000, 20.760 | 2.561 | 279.059 | -5.615 |

Residuals median 0.18-0.22 px at 2,560 wide (90th percentile 0.46-0.57), roll 0.000 +- 0.002 degrees, the yaw within
0.0005 degrees of 360 - facing in all four; the rendered far waterline lies on the predicted shoreline (0.00 px). The
game's focal length is 1,413.05 px at 2,560 (1,413.00-1,413.15 over ten fits; vertical field 54.0 degrees), not 11.1's
1,403, and the eye stands 2.56 yd above the ground at every pitch. With the focal length fixed at 1,403 every fit needs
a roll of about -0.2 degrees and leaves 2-4 px residuals, and #11, which sees both #7's bank and #8's far bank, shows
that #7 and #8 shared one camera centre: 11.1's Low cameras (an eye 3.0 yd above the ground) stood 0.44 yd too high, so
the band distances measured with them come out about 15 % too far. Corrected Low poses exist
(`highref\cameras_low_refit.json`); section 11's numbers were not redone with them. The references are compared
downscaled by 0.586 (never upscaled) to the viewer's 1,665 x 956 viewport.

### 12.2 What the High shots measure

All MEASURED on #6, #10 and #12 (and #11 where said) with the tools of 11.1: CIELAB by forward-distance band F, D the
depth under the surface, theta the view ray's angle below the horizontal, the bed's own contrast from a no-liquid render
normalised on dry land. The targets and the scorer are in `D:\wmv-09\tune\hi\highref` (`targets.json`, `score_high.py`;
the tolerances from the spread over #6, #10 and #12).

| Quantity | High |
|---|---|
| Colour at F 4-8 yd | sRGB (131, 95-97, 61-64), L* 43-44, a* about 10, b* 24-25 |
| Colour at F 16-32 yd | (109-114, 78-82, 57-58), L* 36-38 |
| Colour at F 48-64 yd (against the far bank) | (130, 94, 67-69), L* 43 |
| Colour model | a linear-light lerp from (145, 110, 69) to (107, 75, 57) by (1 - sin theta)^2.4: RMS 4.1 sRGB over 18 bands |
| The bed's share of its in-air contrast (4-8 px scale) at D 0.03 / 0.12 / 0.25-0.57 / 1.1 / 2.2 / 3.4 yd | 1.07 / 0.57 / 0.33 / 0.25 / 0.16 / 0.10 (2-4 px scale: 1.06 / 0.36 / 0.19 / 0.13 / 0.08 / 0.04) |
| Bed share model | 1.09 exp(-Ls / 0.154) + 0.34 exp(-Ls / 24.9), Ls the straight path from the surface to the bed: RMS 0.026 (the same form in D 0.037, along the refracted path 0.039) |
| Bed shift | drawn lower than in air: 0.9 px at D 0.16, 2.4-4.1 px at D 0.85 and more (at 2,560 wide); opposite to physical refraction, mechanism UNKNOWN |
| Bed wobble | MAD 1.6-4.0 px; correlation half-lengths 0.15-0.18 yd along the view, 0.24-0.36 across; spectral peak 0.45-0.56 yd |
| Reflection, broad (blurred, sigma 48 px) | r = 0.495 x Schlick(theta), RMS 0.093 (r 0.37 at 3.4 degrees, 0.18 at 10.9) |
| Reflection, detail (sigma 8 px) | r = 0.168 x Schlick(theta), RMS 0.049; sharp detail only in the first 10 px below the far waterline (r 0.12-0.19) |
| What is reflected | the bank and the hills: mirrored rays hit terrain beyond about 16 yd; sky only where theta passes 12 degrees, where r is small |
| Ripples | surface-pattern L* RMS 0.8-2.0, no single spectral peak, mean wavelengths 1.0-1.5 yd along the view and 1.2-2.4 across |
| Sparkle blobs | 11 (#10), 30 (#12), 30 (#6), 57 (#11): all bed pebbles closer than 8 yd, none beyond (the Low shots: 1,327-1,512) |

What the numbers miss, seen in the pictures: no hard shoreline (the bank's pebbles go on under the water and fade into a
milky tan within about 0.2 yd of depth); soft, wide, horizontally stretched lighter ovals 1-2 yd across in mid water,
and no glints; under the palms only a soft dark smear about 70 px long; water at 24-48 yd a darker, greyer band that
brightens in its last ~10 px to a thin light line at the far waterline; #11 shows the same far water 10-13 sRGB lighter
at the top of its frame (the tone depends on the screen position: INFERRED, a screen-space effect); the pebbles wobble
by about +-3 px while the reflection carries no ripple structure.

Measured the same way at the High cameras, the Low tuning of section 11 (player p19d) is far from this: L* 34 -> 28 from
near to far against 43-44 -> 36-37, a* 1-2.7 against 9-13, 2,500-3,469 sparkle blobs, the bed's share a flat 0.25-0.30
at F 2-8 yd against 0.84 at 2-3 yd and 0.06-0.20 at 4-16 yd, no reflection and no distortion.

### 12.3 The client data used

Texture slots of the water types (PROVEN_BYTES; the PTR's LiquidType, LiquidTypeXTexture and LiquidMaterial are
byte-identical to Retail 12.1.5's apart from the build in the header):

| Slot | Types 1 Water, 5 Slow Water, 13 WMO Water | Types 2 Ocean, 14 WMO Ocean |
|---|---|---|
| 0 | `lake_a.%d`, 30 frames (FileDataIDs 219901-219930) | `ocean_h.%d`, 30 frames (219855-219884) |
| 1 | procedural, no file (`proceduralRiverDepthTex`; type 13 `proceduralWmoWaterTex`) | procedural, no file (`proceduralOceanDepthTex`) |
| 2 | `newoceanbump.blp` (463849) | the same |
| 3 | `waterchop7bw.blp` (317230) | the same |
| 4, 5 | empty | empty |

- **`newoceanbump.blp`** (MEASURED on its decoded top mip): 512 x 512 raw BGRA with 10 mips, a unit tangent-space normal
  map (length 1.000 +- 0.002, z = sqrt(1 - x^2 - y^2) within 0.002), gentle (tilt mean 5.2 degrees, 99th percentile
  13.6, largest 17.6; RMS slope 0.053 in x, 0.080 in y), tiling cleanly, soft cells with a dominant wavelength of 32-46
  texels. It is the only file type 5 names whose content resembles the High shots' fine, low-contrast ripples (by eye).
  The viewer draws it as the ripple map; the client data gives neither its tiling nor its scroll (UNKNOWN; tuned, 12.5).
- **Slot 0's frames** carry sparse bright specks in their alpha: the Low shots' blobs, which section 11's highlight
  draws. The High path does not draw them.
- **Not used:** `waterchop7bw` (greyscale, a foam or chop mask) and the procedural slot 1 (the viewer would have to
  build it). The same rule picks other ripple maps for other types: `oceanbump.blp` (335362, on the procedural types)
  is much steeper (tilt mean 20 degrees, largest 58), `mercurybump.blp` gentler (12.7, H7). Classic Beta's PBR types
  (1250, 1344) name six 1,024 x 1,024 files that are foam, rim and depth masks, not normal maps, and Retail ships none
  of them; Beta links those types to FFT tiles (LiquidTypeXFFTTile, 65 rows), which the viewer does not read.

The ripple-map rule (host, `LiquidCatalog`; the names PROVEN_BYTES, the rule INFERRED): a type's `bump` is the first
file of slots 1-5 whose file name (the last part of its path) contains "bump", in any case; 0 when none. Clients with
LiquidTypeXTexture rows name the files through the listfile; Era expands Texture[1..5] like slot 0 and looks the names
up in its storage root (a name the root does not list is counted as unresolved). Counted offline with the same rule:
Retail 12.1.5 116 types with a ripple map and 4 distinct files, Classic Beta 10 / 1, MoP Classic 5.5.4.70449 42 / 3,
Classic Era 9 / 1, none unresolved. The viewer's catalogue finds the same on Retail ("bumps for 116 type(s), 4 distinct,
0 unresolved") and on Classic Beta (PROVEN_RUNTIME); Era's count was made with the listfile in place of its root, and
neither Era nor MoP could be run then (11.7); since their installs opened, MoP Classic's catalogue finds the same 42 / 3
and Classic Era's, from its root, the same 9 / 1, 0 unresolved (12.11).

The sky for the reflections: the LightData row of the zone light carries sky colours besides the water's (their names
from public table definitions, INFERRED; their order PROVEN_BYTES by the day cycle). At the spot (LightParams 5985 at
12:00; Retail and the PTR agree), against the references' sky (the median of 1,530 px 4-12 px above the hills, columns
300-469 at full resolution: #10 (249, 193, 116), #6 (215, 147, 81)):

| Column | sRGB | Distance to #10's sky | Distance to #6's sky |
|---|---|---|---|
| SkyTop | (160, 99, 148) | 133.3 | 99.1 |
| SkyMiddle | (255, 226, 175) | 67.9 | 129.1 |
| SkyBand1 | (246, 178, 88) | 31.9 | 44.4 |
| SkyBand2 | (193, 99, 78) | 115.8 | 52.9 |
| SkySmog | (179, 96, 77) | 125.8 | 62.6 |
| SkyFog | (120, 75, 58) | 184.2 | 121.4 |

SkyBand1 is the nearest in both (MEASURED at one spot), so `SkyBand1Color` is the sky the reflections take. The column
is the same in every layout: on shared keys where Retail's value differs from its other colour columns, the PTR's equals
it in 16,898 of 16,898, Classic Beta's in 1,228 of 1,327, Classic Era's in 1,222 of 1,321 and MoP Classic's in 4,350 of
4,491, the best other column in 0-3 (PROVEN_BYTES). The 410 sky values the host sends for Retail's and Beta's maps 0 and
1 all equal an offline sample, and so do MoP Classic's 431 (5.5.4.70531) and Classic Era's 178 (12.11).

### 12.4 The design

**Where the HIGH path draws** (`Resources/WmvLiquid.shader`, a uniform branch, no keywords): when the grab exists for
the camera this frame (`_WmvWaterGrab` 1: Camera.main, a game camera whose culling mask shows the world layer 30, a
liquid object alive, no multisampling, `WMV_WATER` not `low`), the material is transparent (flag 0x1), the type's kind
is water or ocean, and the camera sees the sheet's upper side (since p20e by the triangle's own facing, 12.11 F1;
before, by the view ray going down, the same test on flat water). The zone light is not needed: without one the colour
comes from the type (below). Everything else draws as before: the zone path of section 11 under a zone light, else the
fallback of 1.5. Magma and slime, the Buildings viewer and the other viewers (whose cameras never show layer 30) never
take the HIGH path.

**The grab** (`WmvWaterGrabPass`, new): a render pass enqueued per camera from `beginCameraRendering` as
`WmvFrameDecodePass` is (the renderer asset is not in the repository), at BeforeRenderingTransparents (after the opaque
world and the sky, before the water's queue) with intermediate textures required, so the attachments are readable. URP's
own textures cannot serve: this player's `_CameraDepthTexture` is filled by a depth-normals prepass that no Wmv shader
draws in (INFERRED from the pipeline's source; the self-test shows `_WmvSceneDepth` holds the scene), and the pipeline
asset's opaque texture is half resolution. No URP asset or project setting changes. Once a frame it makes:

| Global | What |
|---|---|
| `_WmvSceneColor` | the colour attachment at full resolution, in the authored domain (the frame decode runs later): the bed the water refracts |
| `_WmvSceneDepth` | the raw device depth as R32F (`WmvWaterGrabDepth.shader`, pass 0): the scene point behind the water, and the reflection's march |
| `_WmvSceneColorRefl` | the scene colour premultiplied by its coverage, the coverage in alpha (0 where the depth is the far plane, the background; pass 1): the sharp reflection (12.7, H2) |
| `_WmvSceneColorBlur` | that at 1/4 resolution (two bilinear halvings) and one four-tap blur (`WmvWaterGrabBlur.shader`, its tap spacing `grabBlur`): the broad reflection |
| `_WmvGrabTexel`, `_WmvWaterGrab` | the copies' texel size; 1 for this camera once the copies exist, 0 at every camera's start and end, and pinned 0 in `RenderToArray` |

Only the `WmvMain` that installed the pass removes it (12.7, H9), and its two materials go with it. While it is
installed the player has 64 live materials instead of 62 (MEASURED in every viewer; constant, not a leak).

**The HIGH branch**, per fragment, with P the water point, V the view ray, uv the screen uv and S(uv) the scene point
`_WmvSceneDepth` holds there, rebuilt through the inverse view-projection (the shader's header gives the full form; the
values are the tuned ones of 12.5):

| Term | Formula | Grade |
|---|---|---|
| Ripple normal N | the type's ripple map sampled at two scales (6 and 20 yd per repeat) scrolling in opposite directions (0.05 yd/s on the liquid clock), weighted 0.4 / 0.6, the slope x 2 x the map's strength factor (12.7, H7); on the game's axes whatever the floating origin (`_WmvRippleOrigin`, H5); since p20e the map's mean slope (its constant tilt, measured at upload) taken out of each sample before the strength scale (12.11); flat without a ripple map | the scales MEASURED, the scroll and the tilt's removal choices |
| Refraction | uvR = uv + N.xz x 0.02 x saturate(Ls / 0.3 yd): the ripple normal's world xz, so the offset's screen direction turns with the camera's yaw (a view-space form was tried and reverted, 12.7 H6); the shifted sample is taken only when its scene point lies behind P along V and not above the water (y <= P.y + 0.02 yd, H1), else the unshifted one | the gain MEASURED by the tuning; the game's own shift not drawn (12.10, 12.11) |
| Path Ls | the straight path from P to the bed's scene point, yards | MEASURED as the best driver (12.2) |
| Transmission T | min(1, 1.09 exp(-Ls / 0.154) + 0.38 exp(-Ls / 24.9)) | MEASURED (the fit of 12.2; 0.38 by the tuning) |
| Water colour W | under a zone light: lerp(close, far, saturate(Ls / 3.25)) x 1.35, the zone light's river pair (the ocean pair for the ocean kind); without one: saturate(MinimapStaticCol x 0.3125) | MEASURED (12.5) |
| Refracted colour | lerp(W, bed, T), the bed `_WmvSceneColor` at the uv taken | -- |
| Reflection | the view ray reflected about normalize(lerp(up, N, 0.25)) and marched over `_WmvSceneDepth`: 32 steps at 128 ((k + d) / 32)^2 yd (d a fixed 4 x 4 Bayer dither, so pinned captures reproduce), the first step behind the scene refined by 5 halvings, a hit within 0.3 yd + 2 % of the scene point's distance; its colour from `_WmvSceneColorBlur` (broad) and `_WmvSceneColorRefl` (sharp), each read as c.rgb + sky (1 - c.a), fading to the sky over 0.2 of the screen at its edges; a miss (off the screen or the depth range) takes the sky | the strengths MEASURED (12.5), the march a choice |
| Sky | the zone light's `SkyBand1Color` at 12:00, eased with its other values (`_WmvReflectSky`); without one a neutral (0.5, 0.6, 0.7) | the column MEASURED (12.3), the neutral sky a choice |
| Fresnel F | Schlick at dot(-V, N), F0 0.02 | a choice (water's) |
| Output | rb = 0.2 F and rd = 0.02 F, both scaled down when their sum passes 1 (H10); saturate(refracted (1 - rb - rd) + broad rb + sharp rd), alpha 1 | -- |

No sparkle highlight: slot 0's frames are not used on this path. The output is opaque and in the authored 0..1 (nothing
the water draws can bloom), and the material writes depth while the High look is installed (H8). In the display modes W
and the sky are converted like the zone path's colours; the copies already hold the frame's own domain.

**Host and protocol.** The catalogue's types carry `bump` (the ripple map's FileDataID, 0 when none) and each light's
values `sky` (`SkyBand1Color` at 12:00, interpolated like the water colours; a light still gets values only for its
water colours): additive keys, protocol 19 kept. The role `liquidTexture` serves the catalogue's frames and its ripple
maps and nothing else (`textureIds()`), under the same budgets. Reading Texture[1..5] needed a reader fix:
`WDC3File::get` now counts a string-array element's offset from the element's own place (before, only element 0 read
correctly, section 3); LiquidType's `Texture` is the only text array in any `database.xml` (9.2, 10.0, 10.1, 12.0), so
no other table's values change (PROVEN_SOURCE). `database.xml` reads `Texture` as an array of 6 and LightData's
`SkyBand1Color`; `LightData.dbd` names the column in its three layouts. `SCHEMA_VERSION` 21 -> 22: a schema-21 cache
lacks the new columns, and the liquid and light queries would fail on it. `-terraintile` has four new checks (type 5's
six templates read as stored; types 1, 2, 5, 13 and 14 -> 463849; the ripple-map counts per audited build; type 5's
ripple map served through the host's own handler: BLP2, 1,399,272 bytes) and checks the sky at every audited spot.

**Player.** A type's ripple map is a list of its own in the registry, held only while the High look is installed and
only for a transparent water or ocean type (H4); fetched once per scene and FileDataID like the frames, within the same
budgets; checked as a unit normal map on its top mip (|n| within 0.9-1.1 for at least 95 % of up to 64 x 64 sampled
texels, else flat normals, said); its slope spread measured there (H7); uploaded as a linear Texture2D (the file's
format and mips, Repeat, trilinear); counted with the frame arrays in the tallies and bytes; disposed with its types.
Retail's log line: "ripple map of type(s) 5: FileDataID 463849, 512x512 R8G8B8A8_UNorm, 10 level(s), 1398100 GPU bytes,
a unit normal map (100.0 % of 4096 sampled texels)", since the fixes ending "slope spread 0.0957 -> strength factor
1.0004" and, since p20e, "mean slope (-0.0105, -0.0423) taken out x 1". The liquid clock reaches the shader as
`_WmvLiquidTime` (pinned by `-wmvAnimTime`).

**Switches.** `WMV_WATER=low` (environment, read once at start) turns the High look off: no grab, no ripple map fetched,
no depth write, the zone and fallback paths only; the player logs "water: WMV_WATER=low -- the High look is off". It is
the A/B switch and a safety switch (12.8: pixel-identical to the zone-light build). `-wmvWaterTune=key:value,...` in
`WMV_DEBUG` overrides any of the 26 tunable values (25 before p20e's `bumpMean`) at run time (`Wow/World/LiquidHigh.cs`,
`LiquidHighTune`: each key's default and range; an unknown key, a value that is not a number or one out of range is
ignored and said; since `WMV_DEBUG` splits on commas, the tokens after it that do not start with `-` are its further
pairs); the player logs "water tune: ..." once. The tuning renders of 12.5 needed no rebuild.

### 12.5 The tuned values

Fourteen variants were rendered at the four High cameras (player p20a, hidden desktop, the clock pinned, each through
`-wmvWaterTune`) and scored (12.6); the last, v14, became the defaults (player p20b: its viewport is pixel-identical to
v14's in all four shots). Each value is MEASURED at the one spot through the four shots, or a documented choice; every
one is applied to every other water and ocean type as INFERRED.

| Key | Start -> final | Evidence |
|---|---|---|
| `reflBroad` | 0.495 -> 0.2 | at the measured gain the drawn r was 2-3 times the target (broad r 0.83-1.09 against 0.12-0.53 just below the far waterline), and r scales almost linearly with the gain; at 0.2 broad r is on target 10-55 px below the waterline in #6, #10 and #12 (reflection score 4.38 -> 1.01); 0.25 mirrored the far bank's red rocks, which the reference does not show |
| `reflDetail` | 0.168 -> 0.02 | detail r 0.12-0.15 3-10 px below the waterline against 0.12-0.19; at 0.05 the palm trunks still showed as mirrored lines |
| `reflBump` | 1 -> 0.25 | the reflected ray tilted by half the map's tilt (with `bumpStrength` 2): the speckled sharp copy of the palms becomes a soft dark smear, as in #10 and #12 |
| `bumpStrength`, `refract` | 1 -> 2, 0.012 -> 0.02 | an offset of 0.04 per unit of tilt; ripple L* RMS at 8-24 yd 0.62-0.73 -> 0.9-1.1 (target 0.8-1.4), texture score 2.33 -> 1.57; the bed's wobble 0.55-0.64 -> 1.9-2.3 px (target 1.6-2.8); a strength of 2.5 scored the same and made #11 too busy |
| `bumpWeightA` | 0.6 -> 0.4 | the wobble's spread nearer its target (#6 at 5-10 yd 1.68-1.86 -> 2.0-2.3 px), the texture score unchanged |
| `waterGain` | 1 -> 1.35 | judged where the bed adds least (bed share 0.04-0.08 at 16-48 yd): L* 38.6-39.6 against 36.8-37.7 (#6, #10, #12) and 39.9-40.8 against 40.2-41.6 (#11); the near water's shortfall is the bed's, not the water colour's |
| `tSlowA` | 0.34 -> 0.38 | the wobble lowers the measured bed share (0.17 -> 0.10 at 4-6 yd); 0.38 brings it back to 0.12-0.13 with 142-428 pebble blobs at 4-6 yd against 83-185 (the scorer's density); 0.45 gave 545-1,128 and 0.62 about 3,000 |
| `reflEdge` | 0.08 -> 0.2 | #11's water at 32-48 yd L* 39.4 -> 40.8 (target 40.2), its broad r 10-20 px below the waterline 0.34 -> 0.20 (target 0.12); removes the speckled palm reflection where the palms meet #11's top edge (a faded hit falls back to the zone light's warm sky) |
| `noZoneGain` | 0.5 (provisional, 12.7) -> 0.3125 | fitted at the same spot in Classic Beta (below) |
| `grabBlur` | 1 | 3 looked the same and scored slightly worse (1.04 against 1.01) |
| `tFastA`, `tFastL`, `tSlowL` | 1.09, 0.154 yd, 24.9 yd | the fit of 12.2, kept |
| `bumpTileA`, `bumpTileB` | 6, 20 yd | MEASURED: the wobble and oval scales and the map's cells, kept |
| `bumpScroll` | 0.05 yd/s | a choice: the shots are stills |
| `bumpMean` | (new in p20e) 1 | the share of the ripple map's mean tilt taken out before the strength scale (0: the map as stored); a choice: a tiled map's constant tilt is no ripple, and it shifted the bed (12.11) |
| `f0`, `refractRamp`, `reflMaxDist`, `reflThick`, `reflThickPct`, `skyR/G/B` | 0.02, 0.3 yd, 128 yd, 0.3 yd, 0.02, (0.5, 0.6, 0.7) | choices (the neutral sky is not measured: the spot has a zone light) |

**Without a zone light** (`noZoneGain`, 12.7 H3). At the spot Classic Beta 1.60.1.70450 draws LiquidType 1240
"PBRWater - Generic - Lake" (Kalimdor tile 39_29, chunk rows 8-11, flat at 17.810; PROVEN_BYTES), whose
MinimapStaticCol is (182, 244, 245) (PROVEN_BYTES), and no light gives it water colours: the stand lies outside all
nine of Beta's map-1 zone polygons, no point light reaches it, and the map's default light (191) has all-zero water
colours (PROVEN_RUNTIME; 11.4). Four of the owner's eight Beta shots at the stand (ACROSS and WATERLINE, at Liquid
Detail 0 and 3, which agree within 3 bytes in the far water) give the far water (16-33 yd, water pixels told by their
colour, in bands below the far waterline, at full resolution) a mean median of (86.2, 69.8, 52.2) sRGB over twelve
bands. A least-squares fit of W = MinimapStaticCol x k on linear luminance gives k = 0.3125 (W (57, 76, 77), its
luminance 0.066 as measured; residual RMS 0.0049 between bands; per shot 0.310-0.315; with the Retail set's waterline
rule 0.305, the darker 40 % 0.283, the teal 8-16 yd band 0.320): MEASURED at that spot, INFERRED for every other type
and place. A scalar keeps the type's hue: the game's far water there is warm brown (mostly the orange bank mirrored),
the fitted W teal-grey, off by (-29, +7, +24) sRGB per channel.

### 12.6 Scores

The scorer's groups: the RMS over every band of every shot of (viewer - target) / tolerance (z; lower is better), the
four High cameras, the clock pinned (`score_high.py --distort`; each column's `OVERALL` line).

| Group | p19d (the Low tuning) | p20a (start values) | p20b (tuned) | p20c (after the fixes) | p20d (H6 reverted) | p20e (final: F1, the tilt out; 12.11) |
|---|---|---|---|---|---|---|
| colour | 8.86 | 4.74 | 4.46 | 4.51 | 4.48 | 4.47 |
| bed | 3.49 | 2.78 | 2.32 | 2.23 | 2.24 | 2.33 |
| texture (ripple pattern) | 14.10 | 4.04 | 1.48 | 1.55 | 1.44 | 1.48 |
| sparkle | 65.33 | 18.51 | 22.00 | 23.07 | 25.14 | 23.86 |
| reflection | 2.75 | 4.38 | 1.01 | 0.93 | 0.95 | 0.97 |
| distortion (the bed's shift and wobble) | 4.05 | 4.17 | 5.07 | 4.01 | 5.15 | 3.44 |

At the start values (p20a) the structure was the High look already: the sparkle blobs gone (#10: 2,500 -> 5 against 11),
the near water clear with the bed fading, the far bank mirrored; the reflection 2-3 times too strong, the near water too
dark and the far water too bright and green. The tuning (12.5) brought the reflection, the texture and the bed near
their targets; sparkle and distortion got worse for two reasons that are not the water colour's (12.10: the viewer's own
pebbles, the shift's sign). The fixes (12.7) change the pictures a little (the ripple phase, H5; the refraction offset,
H6; the reflection copy, H2; newoceanbump's factor is 1.0004): taken as the RMS of the four per-shot values, colour
+0.05, bed -0.09, texture +0.09, sparkle +0.98 (#11's pebble blobs at 2-4 yd, INFERRED from a crisper bed), reflection
-0.08 and distortion -1.06 (`render\fixed\score_vs_p20b.txt`), the last no real improvement: the wrongly signed shift
fell to near zero (12.10); p20d reverts H6, which brings the wobble and the near ripple contrast back and the distortion
score to p20b's (`render\fixed2\score.txt`; sparkle +2.1 from #11's pebble blobs at 2-4 yd, cause UNKNOWN). From p20b to
p20c the changed pixels stay on the water (against a no-liquid render, 4-6 px elsewhere in #6, #10 and #12; in #11 8,368
px of the own bank's shallows, INFERRED to be water whose p20b output equalled the bed); no such check was made between
p20c and p20d. p20e, the deployed player, takes the ripple map's mean tilt out (12.11): the distortion score falls from
5.15 to 3.44 (the bed's constant shift gone, the wobble's spread unchanged) and sparkle by 1.28, and the other groups
move by 0.09 or less; p20d -> p20e changes 771,339 / 1,170,908 / 904,017 / 842,950 px of #10 / #11 / #12 / #06, all but
6 / 188 / 5 / 6 of them on the water (`D:\wmv-09\tune\fin\fix\vpcmp.txt`; the scores `fin\fix\high\score.txt`). Pictures
(the reference downscaled by 0.586 next to the builds at 1:1): `D:\wmv-09\tune\hi\render\final\pictures` (p19d, p20b),
`D:\wmv-09\tune\hi\render\fixed\pictures` (p20b, p20c), `D:\wmv-09\tune\hi\render\fixed2\pictures` (p20c, p20d),
`D:\wmv-09\tune\fin\fix\pictures` (p20d, p20e); every variant in `D:\wmv-09\tune\hi\tune\log.md`.

### 12.7 The review and the fixes

One independent review of the High change (code only), with the regressions of 12.8 beside it: 0 high, 3 medium and 11
low findings. Each was fixed as decided, or is this document (H13) or the measurement of 12.9 (H14).

| # | Finding (severity) | Fix | Proof |
|---|---|---|---|
| H1 | the refraction took any shifted scene point behind the water point, so the far bank, rocks and trunks rising out of the water leaked into the refracted bed (medium; PROVEN_SOURCE) | the shifted sample only when it is behind P along V AND not above the water (y <= P.y + 0.02 yd; `LiquidHighMath.AcceptRefracted`) | GPU fixture: a surface above the water under an oblique ray gives the unshifted pixel |
| H2 | reflection hits were coloured from copies that hold the camera's clear colour beside silhouettes (medium; INFERRED) | coverage in alpha: the reflection copies carry the scene premultiplied by its coverage (0 at the far plane), read as c.rgb + sky (1 - c.a); the refraction still reads the scene as it is | GPU fixture (a half-covered texel) and Camera.main (the copy's coverage on the real camera path); about 13 MB more GPU memory (12.9) |
| H3 | without a zone light W was MinimapStaticCol x 1.35: Classic Beta's water clipped to white and its bloom brightened the scene around it (medium; MEASURED, 12.8) | `waterGain` on the zone light's colours only; without one W = saturate(MinimapStaticCol x `noZoneGain`), fitted to 0.3125 (12.5); the output saturated | GPU fixture: Beta's colour saturated at 0.3125 and at 1.35; Beta at the spot: no clipping, no bloom (12.8) |
| H4 | ripple maps were fetched even with `WMV_WATER=low` and for types the HIGH path never draws (low) | a ripple map is held only while the High look is installed and for transparent water and ocean | self-test hold rules; with low, the tallies equal the zone-light build's (12.8) |
| H5 | the ripples moved with the scene's floating origin, so pinned captures from different entry paths differed (low) | `_WmvRippleOrigin`: per scale, the origin's game position modulo that scale's tile, in double, added before sampling (each tile's own modulo rather than a common period: exact for any tuned tile) | pure test; not rendered from two entry paths |
| H6 | the refraction offset used the world normal's xz, so its screen direction turned with the camera's yaw (low; a candidate cause of the shift's wrong sign) | tried in p20c: the ripple's tilt in view space, with the raster sense from the projection, the flat surface's own term left out; REVERTED in p20d to the world xz offset | measured: the view-space form collapsed the wobble about ten times and left the shift's sign wrong (12.10); the world xz form keeps the wobble on target; the yaw dependence stays (12.10) |
| H7 | the strength was tuned on newoceanbump but applied to steeper maps (low) | a strength factor per map, 0.0957 (newoceanbump's slope spread on the player's grid) / the map's spread, at most 2, measured at upload, logged, bound as `_HiBumpNorm`: newoceanbump 1.0004, oceanbump 0.207, mercurybump 1.27 (spread 0.0753: gentler than the tuned map, so scaled up) | pure tests; self-test on a synthetic map (spread 0.7463, factor 0.1282); the real log line |
| H8 | the opaque HIGH water wrote no depth, so blended doodads and particles under it drew over it at full strength (low) | transparent water and ocean write depth while the High look is installed (`HighDepthWrite`); the Low paths unchanged | self-test depth rules; the gap it leaves is in 12.10 |
| H9 | any `WmvMain`'s disposal removed the one grab pass (the self-test's own disposed the player's) (low) | `Configure` records who installed it; `Release` removes it for that owner only | self-test; the two tests that dispose their own `WmvMain` find the player's pass untouched |
| H10 | the tune ranges allowed rb + rd > 1 and so negative colour (low) | the two shares scaled to a sum of at most 1, the output saturated | pure and GPU fixtures |
| H11 | tests that did not pin what they claimed (low) | an asymmetric Camera.main view (the floor under the north half only: the uv's orientation on the real camera path), a pooled type's ripple map evicted (kept while a second pooled type uses it), pure tests of the `-wmvWaterTune` token rule (`LiquidHighTune.TuneTokens`) and of H1, H2, H3, H5, H7, H10 | all pass (12.8) |
| H12 | the type-5 template and 463849 checks failed instead of skipping on unaudited builds (low) | they skip outside the four audited builds, as the counts check does; the shape check asks for a ripple map only if type 5 has one | `-terraintile` unchanged on Retail and Beta |
| H13 | this document did not describe the High look (low) | this section, 9, 10 and the roadmap | -- |
| H14 | the GPU cost was unmeasured (low) | measured (12.9); the grab still runs while any liquid object is alive, in view or not | -- |

### 12.8 Tests and regressions

| Test | Result |
|---|---|
| Pure runner | 2,195 passed, 0 failed (2,168 at section 11; 2,187 with the High look: 15 in the new `LiquidHighTests`, 4 sky checks in `LiquidLightTests`; 8 more with the fixes); 2,199 passed, 0 failed with the final pass's fixes (12.11) |
| Compile | the player's C# and the netstandard2.1 build of the pure layer: 0 errors, 0 warnings; the three shaders compiled offline against the project's URP includes (vs and ps 5.0 and 6.0): no errors or warnings, the liquid pixel shader about 521 instruction slots on p20e, 6 textures and 4 samplers (about 532 on p20c and 510 on p20d; 494 slots and 5 textures before the fixes); the host builds with the same warnings as before |
| Host `-terraintile 0 30 49` (player p18o) | Retail 12.1.5.70448 PASS 325 / 0, 2 SKIP (321 + 4 new); Classic Beta 1.60.1.70450 PASS 323 / 0, 2 SKIP (319 + 4); the same after the fixes (runtime exe 258629E6, core C855777B, wow.dll A1D547A1: the worktree without the owner's tabard patches); on the deployed host (exe 5A916E4E) the same counts with p18o and with p20e, and MoP Classic 315 / 0, 9 SKIP (12.11) |
| Player self-test (`-wmvLifecycleTest`) | p20e, the deployed player (Assembly-CSharp MD5 B1B2E633...): 1,055 passed, 0 failed, the one old SKIP (12.11: 4 more for the final pass's fixes). p20d (MD5 96953D73...): 1,051 passed, 0 failed, the one old SKIP (p20c the same) (1,012 at p19c; 1,036 on p20a and p20b with 24 High checks: T, Fresnel, sky and hit fixtures through `RenderToArray`, and on Camera.main the pass run once, its depth copy right to 6 digits and the Models view byte-identical with the pass and without; 15 more for the fixes). p20a's first run failed 3 checks: the grab's two materials outlived the tests (176 -> 178); they are now destroyed with the pass |
| The High cameras | p20c and p20d with the High host runtime (superseded: p20e on the deployed host, 12.11): maps test 25 / 0, no round over 50 ms; the player logs "water tune: none", the High look, ripple map 463849 with factor 1.0004, zone 139 (light 204, params 5985); no exception. Pinned captures reproduce (p20a: a repeat render byte-identical in all four shots) |
| Regressions, p20b (private copies, hidden desktops, `D:\wmv-09\tune\hi\regress`; superseded by the final validation on the deployed pair, 12.11) | Retail acceptance 79 / 0, Classic Beta acceptance 51 / 0, Dire Maul 20 / 0, Stormwind Harbor 17 / 0, the zone-border runs 56 / 0, the far flight 20 / 0, `life08b` 12 / 0, the ocean flights 23 / 0 twice: the counts of section 11. `life08` 64 / 6, the same six Classic Era failures as the zone-light build (11.7). Every liquid tally 0 after every Clear; 0 exceptions, 0 malformed or failed liquid counts, 0 `liquidTexture` refusals; the liquid geometry lines and the zone-light sequences as before. A second run of three of them pixel-identical in 20 of 20 viewports |
| Regressions, p20c (`D:\wmv-09\tune\hi\rebuild\regress`; superseded, 12.11) | Retail acceptance, Classic Beta acceptance, Dire Maul, Stormwind Harbor and the zone-border runs, High and Low: the same counts; the tallies 0 after Clear; with `WMV_WATER=low` no ripple map is fetched, and the arrays and bytes equal the zone-light build's (Retail acceptance 2 arrays, 10,079,060 bytes; 3 and 11,477,160 under High); every ripple map logs factor 1.0004 |
| Pictures (masks from the no-liquid player; superseded for p20e, 12.11) | only the water changes. p19d -> p20b: Stormwind Harbor's ocean dark navy with sparkles (10, 39, 51) -> clear teal (30, 57, 62), the bed and a rock seen near the shore, the dock, ship and lighthouse softly mirrored; Dire Maul (79, 93, 61) -> (104, 126, 77); the Durotar river (110, 91, 65) and the Barrens side (86, 108, 107), the shot right after a crossing equal to the settled one; magma, Blackrock's lava and slime 0 px, Beta's tar pits 5 and 0 px. p20b -> p20c: changes outside the water within 2 px of its edge (a few 1-step pixels up to 3 px further); Beta ocean (182, 194, 191) -> (59, 72, 69), Beta swamp 1297 mustard (172, 164, 34) -> dark olive (58, 58, 15); on Beta's canals 29,068 and 39,827 px change where p20b's over-bright water had equalled the no-liquid picture (CORRECTED, 12.11 F3: a hole in p20b's water mask, not floor grout hidden by the depth-writing water; the grout still shows through the High water) |
| `WMV_WATER=low` | 30 of 30 world viewports pixel-identical to the zone-light build (p19d), on p20b, on p20c and on p20e (12.11); the low line logged, no grab (62 live materials) |
| Other viewers | equal on the zone-light build, p20b and p20c (and on p20e with the deployed host, 12.11, but for `fmt_details`: there CB4E6736, the host's tabard patches): Buildings 108238 CAC3F60F, 4235933 926D6C24, Beta 7962574 B3B1E3C8, wmo108631 D3DB7749; Models (the bear) 52605ECC; Characters `fmt_details` 0B3BD1A5 and a new pinned case without a tabard, `belt_race`, 28C0F9B7 |
| Classic Beta at the spot (the PTR poses; the Beta pose not fitted; p20d and p20e byte-identical to p20c, 12.11) | p20b: 51.8-70.8 % of the water at G and B >= 245 (91-99 % in the acceptance pictures), and 171,586-189,923 non-water pixels per shot brighter than Low by up to 19 steps (bloom). p20c: 0.00 % clipped, the water's median (80-82, 88-89, 76-78), its 99th percentile at most (145, 120, 94); 57-62 non-water pixels per shot differ from Low, each darker by one step, about half within 2 px of the water's edge. The look: 12.10 |
| The host's screenshot | the host's Screenshot command is refused while the world view is shown (PROVEN_SOURCE), so the High water never reaches a host screenshot today; in the window captures the water meets the sky in one row with no matte line |

### 12.9 Frame time

A frame-time rig on the pieces of 5.3's flight over Retail's ocean (29_48, 8 pieces) and 4 pieces along the Southfury
River (8 yd above the water, pitch -10, 40 yd/s), one run each per arm and mode, twice, alternated (12 runs, every one
38 / 0, nothing else running): High (p20c), Low (p20c with `WMV_WATER=low`) and the zone-light build (p19d), uncapped
(`-wmvFrameRate=0`) and at the default cap. Frame times are ETW present-to-present intervals; GPU is the player's busy
time per frame; the differences are per piece, averaged over the runs (MEASURED, `D:\wmv-09\tune\hi\rebuild\perf`).

| Measure (ms unless said) | Ocean | River |
|---|---|---|
| Uncapped mean frame: High / Low / zone-light build | 2.010 / 1.820 / 1.942 | 2.760 / 2.704 / 2.640 |
| Uncapped GPU per frame: High / Low / zone-light build | 0.681 / 0.540 / 0.547 | 0.830 / 0.708 / 0.698 |
| High - Low per piece: frame, GPU (p20b before the fixes) | +0.146, +0.122 (-0.043, +0.111) | +0.060, +0.121 (+0.062, +0.140) |
| High - zone-light build per piece: frame, GPU | +0.026, +0.128 | +0.111, +0.135 |
| Capped mean frame: High / Low / zone-light build | 16.666 / 16.664 / 16.664 | 16.664 / 16.662 / 16.666 |
| Capped frames over 33 / 50 ms: High, Low, zone-light build | 2 / 0, 4 / 0, 1 / 0 | 1 / 0, 0 / 0, 3 / 0 |
| GPU memory, MB: High / Low / zone-light build | 738 / 703 / 703 | 801 / 766 / 766 |

The High look costs about 0.12 ms of GPU a frame, the same on both pieces (p20b's +0.11-0.14 ms); the frame's own
difference (-0.04 to +0.15 ms) is of the size of the run-to-run spread (the zone-light build's uncapped mean over the
ocean: 1.814 ms in the p20b batch, 1.942 in this one), and the capped frame rate does not change. It holds about 35 MB
more GPU memory than Low, 13 MB more than p20b's High (INFERRED: H2's full-resolution reflection copy). The GUI rounds
over 50 ms of these runs (0-3 a run, the zone-light build's included) are all at the pick, entry and wait of the second
map, none in the flights. Superseded by the final validation on the deployed pair (12.11): three runs per arm on p20e
give +0.140 / +0.132 ms of GPU over Low (ocean / river, the means of three rounds; the means in the table above give
+0.141 / +0.122 for p20c) and the same 35 MB.

### 12.10 What is still different, and why

- **The hue** (MEASURED): the near water is short of red by 20-25 sRGB and the far water has too much blue (colour z
  4.5). The colour is the zone light's pair scaled by one gain, and the bed is the viewer's terrain, brighter and less
  red than the game's (11.6). Corrected by the final pass's review (12.11): terrain light explains only part of it. Fed
  the game's scene, the far and mid water's excess green, blue and brightness goes (class B: the viewer's brighter,
  yellower bank reflected; Stage 12), but the water's own colour stays short of red (R -21..-27 near, -12..-15 far;
  class C, UNKNOWN). The model was not bent to chase the hue.
- **The refraction's shift and wobble** (MEASURED; mechanism UNKNOWN): the game draws the bed lower than in air (-0.09
  to -4.08 px at 2,560 by band). With the world-space offset (p20b) the bed moved the other way, +1.2 to +2.1 px, with
  the wobble's spread on target (1.87-2.30 px against mostly 1.6-2.8); no tune key can add a view-relative shift. With
  H6's view-space offset (p20c) the shift is +0.09 to +0.13 px (#11 at 4-5 yd -0.00), still not the game's sense, and
  the spread collapsed to 0.14-0.22 px (#11 0.16-0.71): the vertical part of a view-space tilt scales about with
  sin(pitch), small at these near-level cameras (INFERRED). The near water's ripple contrast fell with it (#10 at 4-6 yd
  1.67 -> 1.49 against 1.77). So p20d returns to the world-space offset: the spread is on target again (1.57-2.27 px),
  the near ripple contrast back (#10 1.67, #12 1.73 at 4-6 yd), the shift +1.23 to +2.31 px, the wrong sense as in p20b.
  Corrected (12.11): the viewer's shift was mostly the ripple map's own constant tilt carried into the offset, not
  refraction, and H6's view-space form had moved the bed 2.2-2.5 px sideways, which was not reported here; p20e takes
  the tilt out, so the shift is about 0 (+0.03 to +0.20 px, pooled at 4-10 yd) with the spread unchanged. A shift of the
  game's sense needs a mechanism the pictures do not show (UNKNOWN; a High shot facing east would tell whether it is the
  same tilt applied in the world's frame).
- **The reflection's fall-off below the waterline** (MEASURED): the game's broad r falls from 0.50 to 0.23 between 3 and
  35 px below the far waterline, the viewer's stays nearly flat (0.30 -> 0.24): Schlick hardly changes over that degree
  of angle, and no value gives a blur that grows with distance. The rows nearest the waterline stay under target
  (0.25-0.31 against 0.37-0.53). Class C (12.11): mechanism UNKNOWN.
- **The far waterline's light line** (MEASURED): the game's water at 48-64 yd brightens to a thin light line (L* 43, bed
  share 0.26-0.43); the viewer's stays at L* 39-40, bed share 0.05-0.14. The transmission follows the straight path,
  long at grazing angles; a longer fast length (`tFastL` 0.4) did not move it, a stronger reflection mirrors rocks
  instead. Corrected (12.11): it does not call for a depth-driven term. The water there is 2.0-3.5 yd deep and its raw
  bed slope equals the viewer's (0.138 against 0.141-0.155: the 0.40 target is a normalisation effect), and the game's
  water is lighter than the bank it mirrors, a pale extra term (G +13..+16, B +20..+22; class C, UNKNOWN); of the light
  line itself, the viewer's larger step comes from its brighter bank (B), the rest is C.
- **The near pebbles** (MEASURED): the remaining sparkle score is almost all #11's pebble blobs at 2-4 yd, and they are
  the viewer's own terrain: the dry bed alone, scored as if water, has 913 and 1,528 blobs per 100 yd2 at 2-3 and 3-4 yd
  in #11 and about 7,000 at 4-6 yd in #10, while the bed's share there is already on target (0.77 against 0.84).
  Matching both waits for the Stage 12 terrain. The mid water's ovals have the right size and RMS but are softer and
  weaker than the reference's crisp-edged ones.
- **Screen-space reflection** (by design): only what is on screen can be reflected; a ray that leaves the screen or the
  depth range takes the sky, so in a steep view (#11) only the far water near the top of the frame mirrors the scene,
  the bank behind or above the camera never appears, and whether the game reflects what lies above the screen is
  UNKNOWN. The march ends at 128 yd (its steps about 8 yd apart there; beyond, the sky); the refraction's offset is in
  screen space; a second water layer refracts the scene as copied before any water draws (INFERRED: one copy a frame). A
  planar reflection (a second render of the world, estimated 2-6 ms of CPU a frame, INFERRED) was not chosen.
- **A camera under the surface** (PROVEN_SOURCE): the HIGH path needs the camera on the sheet's upper side (12.11 F1);
  below the surface the zone or fallback path draws the water, and since the material writes depth while the High look
  is installed (H8), a blended object behind the water (above it) is hidden there: from under Stormwind Harbor's water
  its waterfall and a ship's mast are missing (12.11 F2). Under the opaque High water, blended objects are hidden too.
  (The canal grout cited here before was a mask artefact, 12.11 F3: the grout shows through the High water.)
- **Water without a zone light** (MEASURED at one Beta spot, INFERRED elsewhere): `noZoneGain` keeps the type's hue: at
  the Beta spot the game's far water is warm brown, the viewer's teal-grey; Beta's swamp 1297 turns dark olive (58, 58,
  15), and in the three Retail zones whose lights have no water colours (11.4) W would be a dark blue for types 1, 2 and
  5, (6, 50, 67) (not rendered).
- **Beta's PBR water is another system** (approximated, not matched): its types name foam, rim and depth masks and FFT
  tiles and no ripple map, so the High path draws them flat, with Retail's transmission and the scalar colour. At the
  spot (p20c) the far band just below the waterline is (74-81, 85-87, 74-75) against the game's (61-83, 62-75, 48-72),
  similar in brightness but teal-grey where the game is brown-olive with warm streaks of the bank mirrored; the near and
  mid water stay teal-grey (72-90, 85-90, 69-79) where the game shows clear water over a warm bed (118-176, 104-128,
  48-73), and the bed shows only in the last ~150 rows of the NEAR shot. Beta's water is much clearer than the
  Retail-tuned transmission (INFERRED). Triptychs: `D:\wmv-09\tune\hi\render\beta_hi\pictures`.
- **Classic Era and MoP Classic** ran with the High look in the final pass (12.11), each once Battle.net's updates had
  restored its install: no defect seen, and no reference to compare with. On Era the host's catalogue now finds the
  ripple maps counted offline (9 types, 1 distinct, 0 unresolved) and sends the sky column equal to the tables (12.3);
  its water draws warm terracotta at Durotar and teal with an olive rim at Lake Everstill, from the tables' colours
  (12.11), which only an Era reference could confirm or refute.

### 12.11 The final merge-readiness pass (2026-10-20)

Before the merge the owner approved one more pass over the build as deployed: five independent investigations (which
build each result came from, the depth handling, the look against the references, a gate for the grab, the Classic
installs), the fixes two of them called for (player p20e), and the validation again on the deployed pair, with its frame
time and memory and, once their installs opened, MoP Classic and Classic Era. It ran on 2026-10-20 and 21. The reports,
rigs and outputs are outside the repository, in `D:\wmv-09\tune\fin`: the investigations (`inv_*.md` and their folders),
the fixes (`fix`), the validation (`suite`, `perf`, `mop`, `era`).

**Provenance** (`inv_b4557810.md`; PROVEN_BYTES). Until this pass no result had run on the deployed pair. 12.6 to 12.10
came from several builds: the pure runner (2,195) from the day's final source; the self-test (1,051) and the High
cameras (12.6's p20d column) from p20d, but with the High host runtime `run_hi` (exe 258629E6: the worktree without the
owner's tabard patches); `-terraintile` from `run_hi` with p18o, and on the deployed host only with p20c (Retail); the
acceptance and regression runs, `WMV_WATER=low`, the other viewers, the Beta spot and the frame time (12.9) from p20c or
p20b; `life08`, `life08b`, the far flight and the ocean flights from p20b with the host before H12's fix (exe 1CA5EC4D);
section 11's results from p19c and p19d. The deployed host (exe 5A916E4E, core 8F66B3AA, wow.dll C6602732,
`database.xml` 2405014F) is the worktree plus the owner's two uncommitted tabard patches, built on its own: 51 of its 56
changed paths equal the worktree's, and the other five (`TabardDetails.cpp`, `TabardDetails.h`, `WoWItem.cpp`,
`charcontrol.cpp`, one line of `modelviewer.cpp`) differ only by the tabard hunks. The player is built from a separate
Unity project, whose scripts and resources equal the worktree's but for `WmvScreenshot.cs` (and an extra
`WmvScreenshotMatte.shader`): an unmerged screenshot change of another branch (a2ae7b3f), which p20d therefore carries
(p20e is built from the same project). The final validation ran on the deployed pair: host 5A916E4E and player p20e
(Assembly-CSharp SHA256 545F7573..., MD5 B1B2E633..., 1,643,520 bytes), every hash checked before and after the runs and
unchanged. The deployed build and the validation's private copy agree in 88 host files and in 187 of 187 player files;
only `libfbxsdk.dll` differs, which the FBX exporter alone loads and no run used.

**The depth review** (`inv_6f7b3f30.md`): p20d with `run_hi` against the same player with `WMV_WATER=low`, the
zone-light build and the no-liquid build, the clock pinned. Three findings, and four areas found clean.

**F1, sloped water** (medium; FIXED in p20e). The HIGH path was taken where the view ray goes down (-V.y > 1e-4): below
the eye, not on the side of the sheet the camera sees. On a sloped river (Kalimdor 37_26, chunk 210: type 5 falling from
200.78 to 184.56, about 0.3 yd per yd; PROVEN_BYTES) the water rising above the eye was drawn by the zone path, blended
and without reflection, behind a straight seam on the horizon row (predicted rows 478 / 536 / 507 / 302, observed 477 /
535 / 506 / 301 at four poses; MEASURED). p20e decides by the triangle's own facing: its up normal nUp = cross(ddy(P),
ddx(P)), turned so that nUp.y >= 0, against the view ray, HIGH where dot(V, nUp) < -1e-4 |nUp|; where the derivatives
give no normal the old test decides (`LiquidHighMath.UpperSide`, with pure and GPU checks). On flat water it is the old
test exactly: at the four High cameras it alone changes 0 px (`fix\vpcmp.txt`). Water pixels the zone path draws at the
four poses (`fix\rap`; the validation repeated p20e's pictures byte for byte, twice):

| Pose | p20d | p20e |
|---|---|---|
| `r_low` | 452,812 (289 rows with more than 100) | 8 |
| `r_low2` | 444,025 (283 rows) | 14 |
| `r_high` (9.4 yd above the local water) | 96,335 (145 rows) | 10 |
| `r_side` | 320,224 (376 rows) | 20 |

p20e's are isolated one-step pixels where only the no-liquid picture differs, and 0-2 per pose at the water's edge; no
row holds more than 100. What remains is the shading: it still takes the world's up (the Fresnel, the mirror and the
path to the bed), so the rising part of the sheet, now drawn HIGH, reads as a pale haze
(`fix\rap\crop_r_high_p20e_top_1to1.png`); how the game shades sloped water is UNKNOWN (no reference).

**F2, blended objects above the water, seen from below** (low to medium; kept as a documented approximation). While the
High look is installed, transparent water and ocean write depth on every path (H8); under the surface the zone path
draws them, so blended objects above the water fail the depth test. At Stormwind Harbor 2 yd under the surface (camera
-8700, 1140, -2, yaw 90, pitch 15 and 3) the waterfall and the ship's mast are missing: 43,541 and 46,721 water pixels
differ from Low, 15,450 and 17,549 of them where Low equals the no-liquid picture (MEASURED). Low is not right either:
it draws the waterfall over the surface at full strength. At Durotar 1 yd under the surface, with no blended object
about, High, Low and the zone-light build are byte-identical in five views. A fix would split the draw into two
materials (the HIGH one writing depth, the zone one not, each discarding the other's pixels) and double the vertex work;
F1's fix already limits the gap to cameras truly under the surface. p20e draws these views byte for byte as p20d.

**F3, the canal grout** (this document; CORRECTED in 12.8 and 12.10). The "29,000-40,000 px of floor grout under the
water now hidden" on Beta's canals (12.8) are the pixels where p20b's over-bright water happened to equal the no-liquid
picture (39,827 in shot 03, 29,068 in shot 04): a hole in p20b's water mask. All of them lie inside p20c's water, and
the grout still shows through the High water, about (104-108, 117-121, 115-118) against the water's (81-85, 95-99,
93-97) (MEASURED). The real-data example of the depth-write gap is F2.

**Checked and found clean** (PROVEN_SOURCE, unless said): nothing transparent draws between the grab and the water
(model blended materials at queue 3000 and up, particles at 3900, the water at 2999, world-model materials opaque or
alpha-tested; from above the harbor 0 px changed outside the water, MEASURED); overlapping liquids (of 133 Retail tiles
of Kalimdor and the Eastern Kingdoms with multi-layer chunks, 2 chunks overlap, in one cell each; Stormwind's 14
multi-layer chunks lie side by side; one grab is taken per camera, so a far layer is never in it; layers at the same
height could z-fight, INFERRED, none located or rendered); the fallback (the zone and fallback code byte-identical to
the code before the High look; with `WMV_WATER=low` 15 of 15 depth views byte-identical to the zone-light build, the
underwater ones included, MEASURED); the depth maths (reversed Z, the depth-range checks, the uv's orientation, the
far-plane coverage test, the near-plane view ray of an orthographic camera).

**The visual review** (`inv_92808b3a.md`; p20d's High-camera renders against #6, #10, #11 and #12; nothing run). An
offline model of the HIGH formula, fed the viewer's own scene, matches p20d's band medians within 1-3 sRGB; fed the
game's scene instead (the reflection sampled from the reference at the mirror hit, the game's sky, the bed brought to
the game's light by the ratio measured on dry land), what is left belongs to the water. The game's light is not the
viewer's (MEASURED, all four shots agree): the own bank at 1.5-6 yd has a linear game / viewer ratio of 1.01 / 0.64 /
0.58 (R / G / B; the game's (149, 93, 51), L* 45, a* 18.8, the viewer's (149, 115, 68), L* 51, a* 7.1), the far bank at
40-70 yd 0.39-0.44, the hills at 150-250 yd 0.25-0.36.

One difference was a defect of the water, class A: **the ripple map's constant tilt** (FIXED in p20e).
`newoceanbump.blp` is tilted on average (mean slope -0.0102, -0.0406 on its top mip; PROVEN_BYTES), and the refraction
offset carried that tilt to the screen unchanged, the same whatever the camera's yaw: at 4-10 yd p20d's bed was shifted
by (-0.57..-0.62, +1.68..+1.85) px against a predicted (-0.68, +1.55), and p20c's view-space form (H6) had shifted it
2.2-2.5 px sideways, which 12.10 did not report. The same tilt turned every reflection by about 1.2 degrees (INFERRED).
p20e measures the map's mean slope at upload, beside its spread, and takes it out of every sample before the strength
scale (`_HiBumpMean`; the tune key `bumpMean`, 1 by default, 0 draws the map as stored); the ripple map's log line now
ends "mean slope (-0.0105, -0.0423) taken out x 1" (PROVEN_RUNTIME). At the four High cameras (F 4-10 yd pooled,
MEASURED, `fix\shiftband.txt`) the shift went from dx -0.57 to -0.68 and dy +1.68 to +1.89 px to dx +0.01 to +0.14 and
dy +0.03 to +0.20 px, the wobble's spread unchanged (along y 2.08-2.14 -> 2.02-2.20 px); the scores are 12.6's p20e
column.

The other differences, classified (B: the scene, which Stage 12's light, terrain and sky decide; C: the water's own,
mechanism UNKNOWN; each MEASURED as stated):

| # | Difference | Class | Evidence | What would settle it |
|---|---|---|---|---|
| 1 | The game's own bed shift, lower than in air (12.2) | C | not drawn: the viewer's part was the tilt (class A, fixed); the game's could be the same tilt applied in the world's frame, but all four references face west | a High shot facing east from the same stand |
| 2 | The near bed's hue: terracotta in the game, yellow-tan in the viewer | B | the shallows at D below 0.06 yd are (150-152, 96, 53) against (148-150, 116, 69): the dry bank's ratio | Stage 12's terrain light |
| 3 | Pebble blobs at 2-4 yd | B | on dry land too, with no water: the game's own bank (#11, 1.5-6 yd) has 3 blobs (1.7 %), the viewer's 33 (50 %) | Stage 12's terrain |
| 4 | Far water too green and blue, mid water too bright (16-48 yd) | B | fed the game's scene, G +6..+11 -> -2..-5, B +7..+10 -> +2..+3, L* 35-37 against the game's 36-37 (p20d 39-41): the viewer's brighter, yellower bank reflected | Stage 12 |
| 5 | Sky and haze | B | the viewer draws no sky (the clear colour (25, 25, 30)); the game's distant land is darker, so no fog lightens the water here | Stage 12 |
| 6 | The water's own warmth | C | with the game's scene the model is still short of red, R -21..-27 near and -12..-15 far, where the bank's R ratio is 1.01; the shallows settle to (130-134, 91-99, 56-66) against (108-113, 91-97, 58-66), at the same depth in both (D 0.06-0.1 yd, settled by 0.2-0.3) | a High shot at another time of day |
| 7 | The far water right of the palms is light (the scorer's 48-64 yd band) | C | the game's water there (132-139, 96-101, 70-73) is lighter than the bank its mirror rays hit (all of them; (133-135, 82-87, 49-51)): a pale extra term, G +13..+16, B +20..+22, at D 2.0-3.5 yd; the raw bed slopes are equal (0.138 against 0.141-0.155) | a High shot facing east |
| 8 | The light line at the far waterline | B and C | the viewer's step is +3.8 L* against +2.5 because its bank is brighter (L* 52.7 against 36): B; the game's line is lighter than both its bank and its water: C | Stage 12 (B); none named (C) |
| 9 | The reflection's fall-off below the far waterline | C | broad r 0.50 -> 0.23 over 3-35 px in the game, about 0.30 throughout in the viewer | none named |
| 10 | Crisp ripple ovals | C | the contrast matches (L* RMS 0.84-0.93 against 0.85-0.86), the tint does not: b* moves 0.39-0.48 per unit of L* in the game, 1.02 in the viewer (yellow, from the bed and bank); likely #7's unknown | a High shot facing east |

The palms' reflection is a dark smear in both at 1:1 and needs nothing. 12.10 is corrected in place where the review
contradicts it: the hue is only partly terrain light (#4 is B, #6 C), the far waterline does not call for a depth-driven
term (#7), and the viewer's shift was mostly the map's tilt, not refraction (class A).

**The grab gate** (`inv_8884c4aa.md`; designed, not built). The grab could be skipped on frames without water in view by
a same-frame test in `WmvWaterGrabPass.Wants`: the liquid renderers' boxes, 1 yd wider, against this frame's frustum,
taken after the camera has moved and the tiles have been activated, so any water Unity draws opens it in the same frame
and no water flashes Low; viewport shots and the self-test's quads keep the grab (PROVEN_SOURCE). Measured on p20d
(Durotar, uncapped, the clock pinned, three High and three Low runs alternated): High - Low GPU +0.093 and +0.111 ms a
frame in the water view (A, A2) and +0.056 and +0.086 ms in two views without water (B, D), whose pictures are
byte-identical High and Low. So a skipped frame would save 0.06-0.09 ms of GPU (10-13 % of the player's GPU time there,
about 0.5 % at the 60 fps cap) and no measurable frame time (the player is CPU-bound). It would seldom skip: the fly
camera's far plane is 3,000 yd and 7 of the 9 resident tiles carry water, so 53-98 % of the nearby tiles' chunk cells
lie in the frustum even in B and D (INFERRED). And it brings a risk: the render graph frees a pooled texture unused for
more than 10 frames, so the grab's 34-43 MB would be freed and re-created whenever water returns (cost UNKNOWN). Not
built, for those reasons; section 9's "runs while any liquid object is alive" stands. On p20e the same cameras give
+0.070, +0.074, +0.018 and +0.047 ms (below).

**The installs** (`inv_41d36803.md`; read only). Classic Era and MoP Classic could not be validated at first because of
their installs, not the viewer: both were in a failed-update state. Battle.net's agent tried to update both at 17:32 UTC
on 2026-10-19, failed with "container locked" while a game client (Classic Beta) ran, and did not retry (PROVEN_RUNTIME,
its logs). Era opens the right build (1.15.9.70003) and its storage index is intact, but 49,294 of the build's 157,029
FileDataIDs were not on this computer (Map, LiquidType, Light, LightParams and ZoneLight among them), the viewer's own
count; MoP's ROOT, VFS-ROOT and INSTALL were missing, hence CASC error 2. Why the data keeps disappearing is UNKNOWN
(INFERRED: running clients of other WoW versions evicting little-used data from the shared storage; on 2026-10-19 it was
lost again within about an hour). Era was still in that state at the suite's run (2026-10-20, evening): 29,447 of the
build's 157,029 files and 31 tables (Map, LiquidType and Light among them) not on this computer. Both installs were then
restored by Battle.net's own updates, with no action of this work: MoP by the updates of 2026-10-20 (16:29-16:37 UTC,
build 5.5.4.70469) and 2026-10-21 (about 04:36 UTC, 5.5.4.70531), both builds opening with 418,044 files present and 422
missing; Era by its update of 2026-10-21, 04:36-04:37 UTC, the build unchanged (1.15.9.70003): its root lists 157,029
FileDataIDs, 156,608 of them on this computer and 421 not, as before 2026-10-19, and the host's client load reports 0
files and 0 tables not installed (PROVEN_RUNTIME, `era\log_era_e.txt`). All three builds were validated below (70469 by
`-terraintile` only).

What the owner was asked to do (the record of the diagnosis; RESOLVED by those updates): close every WoW client (Retail,
the PTR, Classic Beta and the sandbox's launcher; a running client locks the shared storage, the sandbox server may stay
open); in Battle.net select WoW Classic Era and click Update, or, where it shows Play, Scan and Repair from the gear
menu; then start no WoW client until Era's validation has run. MoP needs nothing more. The validation then resumes with
read-only checks (about 421 of Era's files missing, as before 2026-10-19; the eleven tables present; Battle.net
reporting the install playable and its update complete), then, on the deployed pair, `-terraintile` on Era with no
failure, Era's light dump against the offline reference, `acc_era` 79 / 0, `dm_era`, the Era tail of `life08`, the Era
world models in Buildings (107243, 111185, 108238) and the High render at the Durotar spot. Every run listed here has
since been made (below). The advice stands for the future: a running WoW client locks the shared storage, so an update
Battle.net starts meanwhile can fail with "container locked" and is not retried; if an install stops opening again,
close every client and update or repair it as above.

The Retail log lines "Reading "dbfilesclient/item.db2" failed. Error 0" are pre-existing and harmless: every Retail run
that rebuilds the Item table from the game files logs the same (12 item.db2 lines on 12.1.5.70448, in every such run
since 2026-10-17), and runs that read the viewer's cache log none. item.db2 has 50 sections, 49 of them encrypted, 12
under keys the viewer does not have (74 of 60,025 records; PROVEN_BYTES); each such read fails once and the reader
zeroes and skips the section. "Error 0" is a logging artefact (the error code is read after the logger has started;
INFERRED).

**The final validation**, on the deployed pair, 2026-10-20 and 21 (`suite`, `mop`, `perf`, `era`; hidden desktops, the
viewer lock, no game client running), covers all four clients: Retail 12.1.5.70448, Classic Era 1.15.9.70003 (on
2026-10-21, after its restore), MoP Classic 5.5.4.70531 (70469 by `-terraintile` only) and Classic Beta 1.60.1.70450.
The baselines: the zone-light build (exe 0FCFEAF7 with p19d), p20d and the no-liquid player p18o. The Era runs hashed 14
files before and after (the deployed pair, build64 and the baselines), unchanged; the installs, build64, the worktree
and the main checkout were only read.

| Check | Result |
|---|---|
| Pure runner, netstandard build, `cc.sh` | 2,199 passed, 0 failed (`LiquidCorpusTests` 83, `WorldCorpusValidation` 74, `TerrainCorpusTests` 80, `TerrainMaterialCorpusTests` 226, none failed); 0 warnings, 0 errors; exit 0, no error or warning |
| Player self-test (p20e) | 1,055 passed, 0 failed, the one old SKIP (1,051 on p20d) |
| `-terraintile 0 30 49` | Retail 12.1.5.70448 PASS 325 / 0, 2 SKIP and Classic Beta 1.60.1.70450 PASS 323 / 0, 2 SKIP, with p18o (protocol 18) and with p20e; MoP Classic PASS 315 / 0, 9 SKIP on 5.5.4.70469 and on 5.5.4.70531; Classic Era 1.15.9.70003 FAIL 252 / 7, 4 SKIP for its install on 2026-10-20 (the liquid and light catalogues "not installed on this computer" and five steps that need the map catalogue), PASS 322 / 0, 4 SKIP after its restore on 2026-10-21 (twice; the zone-light build 318 / 0, 4 SKIP; `era\log_era_e.txt`: "files not installed 0", "tables not installed 0", liquids 19 types, 556 frames, bumps for 9 types and 1 distinct; the lights 512 lights, 452 LightParams, 0 zone polygons); 0 player exception or error lines |
| MoP's 9 SKIPs | the checks pinned to audited builds (neither MoP build is audited): six would pass on 70531 by the logged values (the liquid and light row counts, type 5's six templates, types 1, 2, 5, 13 and 14 -> 463849, 42 types with a ripple map and 3 distinct, the Durotar spot), three do not apply (pinned to Retail's copy source and to Beta's encrypted file; all 285 MoP maps resolve, so there is no map without a WDT to refuse) |
| Era's pinned checks and 4 SKIPs (1.15.9.70003) | 14 checks pinned to the build ran and passed, in both of the day's runs: the liquid rows (19 / 2 / 904 / 0), type 5's six templates, 463849 on types 1, 2, 5, 13 and 14, ripple maps on 9 types with 1 distinct and 0 unresolved, type 5's ripple map served through the host; the light layouts, the rows (512 / 452 / 1,795 / 0 / 0), the orphan counts, slot 0 with the copy rows and masked colours, the Durotar spot, the defaults of maps 0 and 1 and their offline counts (map 0: 0 zones, 80 points, 56 values; map 1: 0, 126, 122). Four do not apply: the LightParams copy source (pinned to Retail 12.1.5.70448), the encrypted-file worker (Classic Beta's Map.db2), LiquidTypeXTexture's agreement (Era has no rows), a map without a WDT (every Era map has one). Each log's 47 host ERROR lines are the self-test's 45 "unknown message type" and 2 of its deliberate made-up failure; no encrypted read |
| MoP's light catalogue (70531) | the light, liquid, map and area tables byte-identical to the 70268 audit copy (12 of 12 compared); the dump of maps 0 and 1 matches the offline reference (map 0: 12 zones, 251 points, 242 values, default light 1, worst coordinate gap 0.047 yd; map 1: 15, 173, 189, default 191, 0.046 yd), the sky equal to SkyBand1Color at 12:00 in 431 of 431; the Durotar spot zone 139, light 204, params 212, river close (64, 62, 35) and far (50, 59, 66), alphas 0.5 / 1.0 (the pinned expectation); Stormwind Harbor in no zone, so map 0's default light 1 (params 12) |
| Era's light catalogue | 20 files extracted read only, 0 not read; the 12 also in the 2026-10-11 audit copy byte-identical to it (Light, LightParams, LightData, ZoneLight, ZoneLightPoint, LiquidType, Map and AreaTable among them), `newoceanbump.blp` (463849) MD5 b16a5a6a... as on MoP and Retail; the offline reference rebuilt from them byte-identical to `ref_era_map0.json` and `ref_era_map1.json`; the dump of maps 0 and 1 matches it (map 0: default light 1, worst coordinate gap 0.044 yd; map 1: default 191, 0.033 yd), the zone-light build's dump the same but for the sky, which equals SkyBand1Color at 12:00 in 56 of 56 and 122 of 122. The Durotar spot (PROVEN_RUNTIME, the host's check; MEASURED, the dump): zone 0, base light 191 (params 192), point light 204 at 903.1 yd, inside its full-weight radius of 1,023.1 yd, so params 212: river close (158, 97, 71) and far (113, 82, 63), ocean (114, 92, 79) and (86, 56, 97), alphas 0.5 / 1.0 and 0.75 / 1.0, sky (246, 178, 88), the pinned expectation. Stormwind Harbor, the Lake Everstill stand (-9297.4, -2384.8) and `acc_era`'s lake camera (MEASURED): map 0's default light 1 (params 12), river (79, 93, 20) and (51, 82, 85), ocean (17, 75, 89) and (0, 29, 41), alphas 0.5 / 1.0 and 0.75 / 1.0, sky (153, 220, 245), no point light |
| Acceptance and streaming, Retail and Beta | `acc_wow` 79 / 0, `acc_beta` 51 / 0, `dm_wow` 20 / 0, `harbor` 17 / 0, `border` 56 / 0, `far3_wow` 20 / 0, `life08b` 12 / 0, `wateru_wow` 23 / 0 twice, `life08` 64 / 6 (the Classic Era tail, steps 86-94, as since 11.7; 70 / 0 once Era was restored, below): p20b's counts. One GUI round over 50 ms, in `life08`: 51.33 ms at step 34, the bear loaded in the Models view (its repeat 49.24 ms, with none over 50; earlier runs 46.0-48.0 and 60.93 ms there; not a liquid round, INFERRED) |
| `acc_mop` (70531) | High 41 / 0 twice, the zone-light build 41 / 0, `WMV_WATER=low` 41 / 0, the no-liquid build 41 / 0; no round over 50 ms (the longest 33.19 ms); peak 12 objects, 3 arrays, 11,129,176 bytes, exactly 1,398,100 (the ripple map) above the zone-light build's; 0 after Clear; the same ripple-map line as Retail's |
| `acc_era`, `dm_era`, `life08` (Era restored, 2026-10-21) | `acc_era` 79 / 0 with High twice, the zone-light build, `WMV_WATER=low` and the no-liquid build; `dm_era` (map 429) 20 / 0 in the four arms; `life08` 70 / 0 (64 / 6 in the suite, while Era was broken): its Era tail, steps 84-96, switches to Era, picks map 0, enters tile 31_48 with the liquid light "map default light 1 params 12" and the ripple map, and clears. No GUI round over 50 ms but one in `life08`, 54.51 ms at step 34, the bear loaded in the Models view (the suite's 51.33 ms there; not a liquid step). Peaks 8,828,460 bytes (`acc_era`) and 4,026,828 (`dm_era`), each exactly 1,398,100 (the ripple map) above the zone-light build's; every liquid tally 0 after every Clear (six in `life08`); 0 player exception or error lines, 0 malformed, failed or dropped liquid counts, 0 `liquidTexture` refusals; host ERROR lines 0 but `life08`'s two about the bear ("KeyBone", "race infos"), also in the suite's; the ripple-map line as Retail's (types 5 and 2; type 13 in `dm_era`), none in the Low arms |
| Liquid state and logs (every run) | every liquid tally 0 after every Clear; the peaks as p20c's and p20d's (Retail acceptance 17 objects, 3 arrays, 11,477,160 bytes; Beta 20,779,632; the harbor 11,143,704); 0 player exception or error lines, 0 malformed, failed or dropped liquid counts, 0 `liquidTexture` refusals; the zone-light sequences as p20b's, p19d's and p20c's in all 10 runs; the liquid geometry lines as before (worker order aside), but in `life08b`, where which load the fast flight closes differs, as between p20b and p19d; the shots right after a zone crossing equal to the settled ones; the ocean flights no frame over 33 ms (mean 1.947 / 1.953 ms uncapped) |
| Pictures p20d -> p20e (masks: p20d's water against the no-liquid build) | Retail: 24,750-706,798 px a shot change, 0-68 of them off the water (at most 363, `dm_wow_01`) and at most 3 further than 2 px from it, one step each; the water's mean colour moves at most about 2.5 steps (the harbor (25, 56, 64) -> (23, 54, 63)). Magma, Blackrock's lava, Beta's tar pits (1284), its swamp (1297) and every other Beta shot: 0 px. The slime shots: 332 and 162,276 px, in the river beside the slime (0 and 62 px off the water, at its edge) |
| Against the Low look (p19d, its pictures identical to its earlier run in 30 of 30) | Retail: 0-15 px a shot off the water, one step each. Beta's canals: 35,197 and 42,247 px off Low's water, of which 16,870 and 22,399 are one step darker (Low's bloom glow, absent from High; INFERRED) and 18,327 and 19,848 are floor-grout decals that Low draws over its water; not new (p20d -> p20e: 0 px on Beta) |
| MoP pictures (70531) | `acc_mop`, High against Low: magma and lava 0, 1 and 0 px (the 1 px is p19d's own), the ocean 609,678 and 748,235 px, all on the water, 0 clipped, its mean (17, 54, 68) and (14, 42, 53) against Low's (10, 38, 50) and (9, 38, 50); High repeats in 5 of 5, the terrain as on 70449 (no-liquid, 5 of 5). The Durotar spot (four arms 25 / 0): 0 px changed off Low's water, 0 % of the water clipped, zone 139 (light 204, params 212); clear water with the bed seen, a clean far waterline with the palms mirrored, no seam; the water's median about (98-104, 94-97, 82-85) against Retail's (108-112, 90-93, 65): bluer and greyer, as MoP's river far colour (50, 59, 66) and alphas 0.5 / 1.0 against Retail's (62, 54, 47) and 0.4 / 0.8 would make it (INFERRED; no MoP reference); no defect seen |
| Era pictures (`acc_era` 11, `dm_era` 2, the High cameras 4, the Lake Everstill stand 4, the sloped river 4) | `WMV_WATER=low` pixel-identical to the zone-light build in 25 of 25; High repeats in 11 of 11; the no-liquid build equal to its 2026-10-18 pictures in 13 of 13 (the terrain unchanged); High against Low off Low's water: 0 px in 23, 8 px in `dm_era_01` and 2 in the sloped river's `r_low`, one step each; magma and lava (`acc_era` 09-11) 0 px between High, Low and the zone-light build; 0-0.03 % of the water clipped |
| F1 and F2 | the sloped poses as the table above, p20e byte-identical to the fix's run, twice; the harbor from under the water byte-identical to p20d (F2 as documented) |
| `WMV_WATER=low` (p20e) | 30 of 30 world viewports pixel-identical to the zone-light build (p19d) and to its earlier run; the tallies exactly the zone-light build's (Retail acceptance 2 arrays, 10,079,060 bytes); the low line logged, no ripple map; the 15 depth views identical too, and the five Durotar views under the surface byte-identical High and Low; on MoP 5 of 5 |
| Other viewers | Buildings 108238 CAC3F60F, 4235933 926D6C24, Beta 7962574 B3B1E3C8; Models wmo108631 D3DB7749 and the bear 52605ECC; Characters `belt_race` 28C0F9B7: as before, on p20e twice and on p19d with the same host. Characters `fmt_details` CB4E6736 with both players, against 0B3BD1A5 on `run_hi`: the host's tabard patches (INFERRED) |
| Era's world models in Buildings | Stormwind 107243 71750380, 111185 B9FD0666, Blackrock 108238 ADF41C72: on p20e twice and on p19d with the same host, each equal to section 7's picture of 2026-10-18 (in 11.7 the damaged install had changed two of them and not loaded the third) |
| The High cameras | maps test 25 / 0; the four viewports byte-identical to the fix's renders and the scores identical (12.6's p20e column) |
| Classic Beta at the spot | p20e, p20d and p20c byte-identical in 4 of 4, the low arm equal to p20c's; 0 % of the water clipped, its median about (80-82, 88-89, 76-78); 57-62 non-water pixels a shot one step darker than Low |
| Classic Era at the High cameras, Lake Everstill and the sloped river (every arm 25 / 0, no round over 50 ms) | Durotar: "map default light 191 params 192" logged (the player logs the base light; point light 204 is applied in its blend); warm terracotta water, median about (160-164, 127-135, 104-114) against Low's (140-141, 107-109, 92-95) and Retail's (108-112, 90-93, 65), as light 204's river colours and orange sky would make it, not the base light's (INFERRED); 0 % clipped, 0 px changed off the water; a clean far waterline, the palms mirrored, no seam; on hi11 24,983 px of Low's water at the near shore drawn as the bed, the shallow fade (MoP 16,021). Lake Everstill (the E1b stand, four cameras, `era\rig\cameras_e1b.json`): teal, median (83-93, 114-124, 117-125) against Low's (57-80, 89-96, 73-90); an olive rim at the shore and the dock post, from river close (79, 93, 20); the post mirrored, no seam, a clean far waterline; the bed's hard-edged patches are Era's partly textured terrain (section 9), the same in the no-liquid picture. The sloped river (Kalimdor 37_26): the sheet continuous, no row with more than 100 px of water drawn as the bed (50-1,468 px a pose, scattered); the rising part the pale haze of F1. No defect seen; whether these looks are the game's is UNKNOWN (no Era reference) |
| Long GUI rounds outside the checks (MEASURED; machine load INFERRED) | the sloped-river run `rap_He`: 225.9 and 68.8 ms in the entry's stream wait, no poll work in either (its repeat: none over 50 ms); the baseline arms too: 67.7 ms (the zone-light build's Retail acceptance, at pick), 71.4 ms (the harbor depth run, at mode:maps), 69.7 ms (Beta's low arm, at start) |

**Frame time and memory** (`perf`): three runs per arm, alternated: High = p20e, Low = p20e with `WMV_WATER=low`, the
zone-light build = p19d with its host; the flights of 12.9, uncapped and at the default cap; ETW present-to-present
intervals and the player's GPU busy time (MEASURED).

| Measure (ms unless said) | Ocean | River |
|---|---|---|
| Uncapped mean frame: High / Low / zone-light build | 1.847 / 1.730 / 1.760 | 2.580 / 2.526 / 2.583 |
| Uncapped p99: High / Low / zone-light build | 7.519 / 7.231 / 7.236 | 7.085 / 7.123 / 7.248 |
| Uncapped GPU per frame: High / Low / zone-light build | 0.637 / 0.497 / 0.517 | 0.751 / 0.619 / 0.668 |
| High - Low per round, three rounds: GPU; frame | +0.138 to +0.144 (mean +0.140); +0.081 to +0.181 (mean +0.120) | +0.096 to +0.182 (mean +0.132); -0.060 to +0.136 (mean +0.055) |
| Capped mean frame: High / Low / zone-light build | 16.662 / 16.663 / 16.667 | 16.660 / 16.661 / 16.685 |
| Capped frames over 33 / 50 ms: High, Low, zone-light build | 3 / 0, 4 / 0, 3 / 0 | 0 / 0, 0 / 0, 5 / 1 |
| GPU memory, MB (uncapped): High / Low / zone-light build | 738 / 703 / 703 (peak 813 against 778) | 801 / 766 / 766 |
| Liquid tallies settled, High; Low and the zone-light build: objects, arrays, bytes | 15, 3, 13,475,352; 15, 2, 12,077,252 | 15, 2, 4,911,660; 15, 1, 3,513,560 |

The High look costs about 0.14 ms of GPU a frame over Low uncapped, as p20c did (the means of 12.9's table give +0.141 /
+0.122 ms; its per-piece average, +0.12, weighs the pieces equally), and 35 MB of GPU memory (one more texture, the
ripple map of 1,398,100 bytes, and the grab's copies). The frame's own difference lies within the session's spread (the
zone-light build's uncapped ocean mean, on the same binaries, was 1.814, 1.942 and 1.760 ms in the p20b, p20c and p20e
batches): uncapped, the player is CPU-bound (7.3-7.9 cores busy, the GPU 25-35 %); at the cap the frame rate does not
change. The gate's fixed cameras (Durotar, the clock pinned, two runs each): High - Low GPU +0.070 / +0.074 ms with
water in view (A, A2) and +0.018 / +0.047 ms without (B, D, byte-identical pictures), the frame time not measurably
different (-0.074 to -0.007 ms). Five Clear / enter cycles per arm (capped, 46 / 0 each, no GUI round over 33.5 ms): the
same tallies in every cycle (High 22 objects, 4 arrays, 9,199,888 bytes; Low and the zone-light build 22, 3, 7,801,788),
all 0 after each Clear, GPU memory back to about 350 MB, the viewer flat (2,256-2,259 MB). The player's private bytes 3
s after a Clear drift up by 118-251 MB from cycle 1 to 5 in all three arms, the zone-light build included, and 15 s
after the last Clear stand at 2,116 / 2,129 / 2,120 MB, at or below the first cycle's: memory released late, not a leak
(INFERRED). The runs' only failures are two harness checks of a GUI round over 50 ms in each of two flights (High `HC_2`
134.4 ms; Low `LU_3` 105.8, 111.8 and 100.7 ms), both after a remote-desktop session of the owner began (about 07:05;
none of the ten flights before it had a flight round over 12.7 ms), with the harness's own work in those rounds
0.004-0.016 ms: the machine, not the High look (INFERRED; one High and one Low run on the same host).

**The exit criterion stays OPEN.** Stage 09's exit test (`roadmap.md` 5: "water tiles against references on Retail and
Era"; the audit names the tiles, Retail 29_48 Stormwind Harbor and Era 36_49 Lake Everstill, `00-audit.md` 15) is not
met. The functional validation now runs on all four clients (Retail, Classic Era, MoP Classic 5.5.4.70531 and Classic
Beta, above), but the water is compared with in-game references only on Retail, at one spot, the Southfury River
(sections 11 and 12), not at its exit tile: Stormwind Harbor is rendered and regression-tested but compared with no
reference. Classic Era has no reference at all: it now runs, and its water at Lake Everstill and at the Durotar spot is
described above, but whether it looks like the game's is UNKNOWN. What would close it: the owner's Classic Era
screenshots at Liquid Detail High and at Low at a stated spot (Lake Everstill 36_49, or a spot the owner names, with the
position and facing the game prints); the owner has no Era sandbox, so they must come from playing the live client (Era
had never been launched on this computer, `inv_41d36803.md`); and for Retail a Stormwind Harbor reference or the owner's
acceptance of the Southfury spot in its place; or any alternative the owner explicitly approves. **Superseded on
2026-10-21:** the owner approved such an alternative, the revised acceptance of section 13, which closes the stage
without this exit test; the test is not passed.

## 13. Acceptance and completion (2026-10-21)

**The decision.** On 2026-10-21 the owner approved revised acceptance criteria for this stage: the visual comparison with
the in-game references at the Retail Southfury River spot (sections 11 and 12), together with the functional validation
on Retail, Classic Era, MoP Classic and Classic Beta (12.11, and again on the committed source, 13.2), closes Stage 09.
The roadmap's original exit test, water tiles against references on Retail and Era (`roadmap.md` 5; the exit tiles
Retail 29_48 Stormwind Harbor and Era 36_49 Lake Everstill, section 6), is SUPERSEDED by that approval, NOT passed: both
tiles are rendered and regression-tested, and neither is compared with a reference. Stage 09 is complete under the
revised criteria. Nothing here claims that the viewer's water looks like the game's: the one comparison made is at one
Retail spot (one type, Slow Water; one zone light; noon), with the differences of 12.10 and 12.11.

**Classic Era's visual parity is UNVERIFIED: an accepted limitation.** Classic Era runs (12.11, 13.2), but its water has
never been compared with the game: the warm terracotta water at the Durotar spot and the teal lake with an olive rim at
Lake Everstill (12.11) are its tables' colours drawn by the Retail-tuned model (INFERRED), and whether the game draws
them so is UNKNOWN. The owner accepted this explicitly on 2026-10-21 and keeps it for a future comparison; it is not a
passed check. The plan for that comparison is outside the repository (`D:\wmv-09\refs\REFERENCE_PLAN.md`): the same spot
as the Retail references, the Durotar bank of the Southfury River about 450 yd west of Orgrimmar's front gate (the
helper addon's `/wmvref dur`: X 1261, Y -3948, map 1), first person at the facing the addon confirms, a Low pass (ACROSS,
NEAR, WATERLINE, LANDMARK) and a High pass (ACROSS, NEAR, WATERLINE), at about noon game time (the viewer reads the light
at a fixed 12:00, 11.4; the Retail shots were taken at 11:53-11:57, 11.1). The owner has no Era sandbox, so the shots
must come from the live client (12.11), where the plan's teleport line does not apply. MoP Classic's visual parity is
UNVERIFIED as well: it runs with the High look at the Durotar spot with no defect seen (12.11), and there is no MoP
reference.

### 13.1 Completed functionality

What the stage delivers, run on all four clients unless said (sections 5, 12.11 and 13.2):

- **Liquid geometry.** MH2O on terrain (every instance and layer, the exists bitmaps, heights, depths and stored uv in
  the four vertex layouts; malformed data marks its instance or chunk and never fails the terrain) and MLIQ in placed
  world models (both vertex kinds, the group's liquid type), parsed in the player (section 2) and drawn on the streamed
  tiles and in the placed world models of the world path, world-model-only maps included (Dire Maul on Retail and
  Classic Era, section 5). The Buildings viewer draws no liquid (section 9).
- **The liquid tables per client.** LiquidType, LiquidMaterial, LiquidObject and LiquidTypeXTexture with their layouts
  settled per client from the bytes (U14; 1.4, section 3); the host's liquid catalogue, protocol 19's `liquids` offer
  and the role `liquidTexture` under the Stage 08 budgets (section 3).
- **Frames and lifecycle.** A type's frames in one shared array, animated on the liquid clock; magma and slime in their
  frames' colour; the liquid objects owned by their tiles and world-model prototypes, streamed, pooled and evicted with
  them, every liquid tally 0 after every Clear (sections 4 and 5.2).
- **The zone-light colours** (section 11). The light tables (Light, LightParams, LightData, ZoneLight, ZoneLightPoint)
  per client, the host's light catalogue and the offer's `lights`; water and ocean coloured by the zone light at the
  camera (slot 0 at a fixed 12:00; the selection rules INFERRED, 11.4); without one, the old look.
- **The High look** (section 12), drawn by default: the viewer's approximation of the game's Liquid Detail High (clear
  water over the bed, ripples from the type's ripple map, screen-space reflection with the zone light's sky), its values
  MEASURED at the one Retail spot and applied elsewhere as INFERRED. `WMV_WATER=low` turns it off and keeps the Low look
  of section 11 (pixel-identical to the zone-light build: 12.8, 12.11, 13.2); `-wmvWaterTune` in `WMV_DEBUG` overrides
  any of its 26 tunable values at run time (12.4).

### 13.2 Tested behaviour: the committed source, built clean (2026-10-21)

The validation of 12.11 ran on the owner's deployed pair, whose host carries the owner's two uncommitted tabard patches
and whose player carries an unmerged screenshot change of another branch (12.11, provenance). So the committed source was
built again on its own and validated on 2026-10-21; the outputs are outside the repository, in `D:\wmv-09\tune\final09`
(the host's `cbuild09_*.log` and `run_clean09_manifest.txt`; the player's `unity_build_p21c.log`, `manifest_p21c.tsv`,
`il` and `selftest`; the runs in `tests`). The deployed build, the owner's checkouts and the Unity project were not
changed (the clean worktree adds only its own entry to the repository's worktree list); the viewer and the player ran
on hidden desktops only.

- **Source.** fed928c3 in a detached worktree (`D:\wmv-09\clean09`): 4,223 tracked files, none modified, each
  byte-identical to the branch's. Neither tabard patch is in it: the deployed host's source differs from it in 7 of the
  4,223 files, the five the patches touch (applying the two patches to the clean files gives the deployed ones byte for
  byte) and older copies of this document and `roadmap.md`. The closing documentation commit changes only these two
  documents, so the branch's code at its merge is the code verified here.
- **Host.** Configured as the deployed build (its CMake cache identical once the paths are normalised), built Release:
  0 errors, all 10 targets (the FBX exporter included; FBX export was not run). 77 distinct warning lines, those of the
  deployed build with line numbers left out, none in a file Stage 09 touched. SHA256: `wowmodelviewer.exe`
  1C36DB44..., `wow.dll` B39CA85A..., `core.dll` 61072CF7.... Against the deployed binaries (a PE comparison that
  ignores build stamps): the deployed `wow.dll` exports two more names (`TabardDetails::clearDesign` and `hasDesign`)
  and its exe imports `clearDesign`; no other export or import differs, and the other libraries' code is the same but
  for the paths and import hints those changes shift.
- **Player p21c.** Built from a copy of the Unity project with its scripts and resources set to fed928c3's 83 files
  (only `WmvScreenshot.cs` differed in content; the extra `WmvScreenshotMatte.shader` removed): 0 errors and p20e's 4
  warnings (CS0618). Assembly-CSharp SHA256 DDF84D21..., MD5 A2D6FD44..., 1,640,448 bytes. Against p20e, 181 of 187
  files are identical, the engine binaries among them; the two assemblies, decompiled, differ only in the members
  `WmvScreenshot.cs` declares and in the project path the compiler embeds; the other five files differ by the matte
  shader's absence, the build GUID, a link timestamp or the project path.

The results on the clean pair (host 1C36DB44 with p21c; `-terraintile` also with the protocol-18 player p18o), against
the deployed pair's of 12.11:

| Check | Result |
|---|---|
| Pure runner, netstandard build | 2,199 passed, 0 failed (`LiquidCorpusTests` 83, `WorldCorpusValidation` 74, `TerrainCorpusTests` 80, `TerrainMaterialCorpusTests` 226), its PASS lines those of 12.11 with timings aside; 0 warnings, 0 errors |
| Player self-test (p21c) | 1,055 passed, 0 failed, the one old SKIP: p20e's check lines but for two values that vary between runs |
| `-terraintile 0 30 49` (p18o and p21c) | Retail 12.1.5.70448 PASS 325 / 0, 2 SKIP; Classic Era 1.15.9.70003 PASS 322 / 0, 4 SKIP; MoP Classic 5.5.4.70531 PASS 315 / 0, 9 SKIP; Classic Beta 1.60.1.70450 PASS 323 / 0, 2 SKIP: 12.11's counts, the check lines the same but for per-run timings and counts; the light dumps byte-identical to earlier ones; host ERROR lines the checks' 45 unknown-message and 2 deliberate ones (Beta also its two Section Layout lines), and DB2 read errors only in the run that built a client's database cache (Retail 47, MoP 8, Beta 19, Era 0 lines), as in the deployed logs |
| Acceptance and streaming | `acc_wow` 79 / 0, `acc_era` 79 / 0, `acc_mop` 41 / 0, `acc_beta` 51 / 0, `dm_wow` and `dm_era` 20 / 0, `harbor` 17 / 0, `border` 56 / 0, `far3_wow` 20 / 0, `wateru_wow` 23 / 0 twice; the peaks, the light sequences and the liquid geometry lines those of the deployed pair |
| Lifecycle | `life08` 70 / 0 (its Era tail included), `life08b` 12 / 0 (its geometry lines vary as before); every liquid tally 0 after every Clear (six in `life08`) |
| Logs (the acceptance, streaming and lifecycle runs) | 0 player exception or error lines, 0 liquid lines with a bad count, 0 `liquidTexture` refusals; host ERROR lines only the known ones (Beta's two Section Layout lines, `life08`'s two about the bear); the ripple-map line in every High run, strength factor 1.0004, mean slope (-0.0105, -0.0423) |
| GUI rounds over 50 ms | two, neither repeated: `far3_wow` 56.50 ms in its 200 yd/s flight (its repeat at most 34.05 ms) and `life08` 50.77 ms at step 17, serving one map answer (read in 0.76 ms, served in 50.68 ms; its repeat at most 45.48 ms); not liquid steps, machine or IPC timing (INFERRED) |
| Pictures, High | the regression viewports 48 of 49 pixel-identical to the deployed pair's (`far3_wow`'s, its clock not pinned, differs only inside the water, as two runs of one build do there); the camera views (the High cameras, the depth poses, Beta, MoP and Era) 47 of 47; the High cameras' scores 12.6's p20e column |
| Pictures, `WMV_WATER=low` | 48 of 48 and 39 of 39 pixel-identical to the deployed pair's; the regression viewports 48 of 48 and the camera views 31 of 31 pixel-identical to the zone-light build's |
| Other viewers | Buildings (Retail 108238 and 4235933, Beta 7962574, Era 107243, 111185 and 108238) and Models (wmo108631, the bear) equal to the deployed pair's. Characters differ, explained by a host-by-player run: the host's tabard patches decide `fmt_details` (the clean host with p20e gives 0B3BD1A5, 12.8's value without the patches), the player's screenshot script the rest (p21c against the earlier players: at most 1 RGB step on about 41,000-44,000 px, and 53 alpha px) |
| Ocean flights, uncapped | mean frame 1.721 / 1.745 ms (the deployed pair's 1.947 / 1.953), the longest frame 15.1 / 17.3 ms |

Frame time, GPU cost and memory were measured on the deployed pair (12.11: the High look about 0.14 ms of GPU a frame
over Low uncapped and 35 MB of GPU memory, the capped frame rate unchanged, the same tallies in each of five Clear /
enter cycles) and not again: the clean pair differs from it only in the tabard and screenshot code, which leaves them
unchanged (INFERRED; the ocean flights above show no slowdown).

### 13.3 Accepted approximations and limitations

The stage closes with these, each documented where named; none is claimed to match the game.

- **Visual parity on Classic Era and MoP Classic: UNVERIFIED** (above; section 9). On Retail the comparison is one spot;
  the High and Low looks' values are MEASURED there and INFERRED for every other type and place (section 9, 12.5).
- **Under the water, and the depth write** (12.11 F2, 12.10, section 9). From a camera under the surface the Low paths
  draw the water, and while the High look is installed the water writes depth, so blended objects above the water are
  hidden (under Stormwind Harbor's water its waterfall and a ship's mast are missing); blended objects under the opaque
  High water are hidden too (12.7, H8). Kept: a fix would split the draw into two materials and double the vertex work.
- **Sloped water** (12.11 F1, section 9). Drawn by the HIGH path on its upper side since p20e, but shaded with the
  world's up, so the rising part of a sloped sheet reads as a pale haze; how the game shades it is UNKNOWN.
- **The remaining reflection, refraction and colour differences** (12.10 and 12.11's table). Class B, the scene's (the
  near bed's hue, the pebbles, the far and mid water's excess green, blue and brightness, sky and haze), waits for Stage
  12; class C, the water's own, has mechanisms UNKNOWN (the game's downward bed shift, not drawn; the water's own
  warmth, short of red; the far water's pale extra term and the far waterline's light line; the reflection's fall-off
  below the far waterline; the ripple ovals' tint). The reflection is screen-space only, up to 128 yd (by design).
- **Lighting, sky and fog are Stage 12's** (`roadmap.md` 5). The light is read at a fixed 12:00 in the clear-weather
  slot only; the viewer draws no sky (the reflections take the zone light's `SkyBand1Color`) and no fog; the terrain's
  light is not the game's (12.11: the own bank's game / viewer ratio 1.01 / 0.64 / 0.58); world-model water takes the
  light at the camera (UNKNOWN in the game).
- **Classic Beta's water is another system** (12.10, section 9). Its PBR types (foam, rim and depth masks, FFT tiles, no
  ripple map) are approximated, not matched: drawn flat with Retail's transmission and MinimapStaticCol x 0.3125
  (`noZoneGain`, MEASURED at one Beta spot); the FFT tables are not read. At the Southfury spot that water is too dark,
  teal-grey and too opaque near the camera against the game's clear water.
- **MoP Classic 5.5.4.70531's build-pinned checks** (12.11). `-terraintile` skips 9 checks pinned to the audited builds
  (neither MoP build is audited): 6 of them would pass on 70531 by the logged values, 3 do not apply.
- **The rest of section 9 stands**: the stored texture coordinates' scale, the texture slots not drawn, the lights
  without water colour, MLIQ uv in the group's frame, the groups and attributes not drawn, the report timing, the Low
  look's glint sector and bed contrast, the grab's cost (about 0.14 ms of GPU and 35 MB while any liquid object is
  alive), and the pre-existing items (Era's grey terrain chunks, the Characters' tabard variance on develop).

What would settle more of them, from 12.11: the Era comparison above (and one for MoP Classic); Stage 12's light,
terrain and sky, after which class B is to be measured again; and for class C a High shot facing east from the Southfury
stand and one at another time of day.

**Commit state.** Stage 09 is the commits be19066b (the liquids, with this document's first version), a40d1bf7 (the
zone-light colours and the High look: code, data and tests), fed928c3 (this document's sections 11 and 12, with their
updates to the earlier sections, and `roadmap.md`) and the closing documentation commit (this section, its notes in
sections 6, 9, 10 and 12.11, and `roadmap.md`), on top of develop 3e11ee52; world/09-liquids is merged into develop by
pull request.
