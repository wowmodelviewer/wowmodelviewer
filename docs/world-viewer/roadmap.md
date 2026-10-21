# World viewer roadmap (revised 2026-10-15; milestones 10 and 12 swapped 2026-10-21)

This replaces the branch list of `00-audit.md` sections 8.1, 13 and 15 as the plan. The audit's evidence, unknowns
(U1-U20) and risks (R1-R14) stay where they are and are referred to here.

## 1. The target experience

The owner supplied a reference video (78 s, 1920 x 1080) of the experience the World Viewer is to become. Studied
frame by frame (one frame a second, the map-selection frames at full resolution), it shows:

1. **A Map Viewer.** A list of the client's maps (ID, name, directory) with a search box, and a large overhead map of
   the selected map built from its own minimap images, with faint tile borders.
2. **Picking a position.** A click on the overhead map sets a target. A box marks the target's tile, a marker marks the
   point, and the target is shown as tile and world coordinates (`Target Tile 31, 28 X 1626.0 Y 5.2 Z 157.2`). X, Y and Z
   can also be typed. The coordinates are the game's world frame: X north, Y west, tile column x and row y, the target
   near tile 31_28's south-east corner.
3. **Load World.** A button enters the world at the target. Progress shows file counts ("Main 0 / 28", "Texture
   58 / 389").
4. **A free-flying camera.** No orbit: the camera flies forwards, turns and climbs through the environment.
5. **Assembled world geometry.** A dungeon of world models: interiors, walls, arches, stairs, doorways, a tunnel.
6. **Objects and environment.** Placed doodads (braziers, statues, rocks, crystals), particle effects (fire, smoke,
   sparks), animated liquids (green and red), coloured interior lighting, fog, and an outdoor terrain area with liquid
   and trees.
7. **Continuous exploration**, through one environment, from the dungeon into the outdoor area.

The map in the video is an instance that is mostly world models with a terrain area. So the viewer must not assume
that every map is made of ADT tiles: a map can be terrain tiles, one global world model (MPHD bit 0x1, `00-audit.md`
4.3), or terrain tiles with world models placed on them.

## 2. Where the stages stand

| Stage | State | What it gave the target |
|---|---|---|
| 01 parsers | merged | WDT, ADT (split files), DB2 readers in the player, verified on the corpus |
| 02 map catalogue | merged | every map of every client, its WDT reference and state |
| 03 terrain geometry | merged | one tile's terrain mesh in the player (protocol 12), the world-to-Unity mapping with a floating origin |
| 04 terrain materials | merged | the tile's textured composite (4/8-bit alpha, layers, holes) |
| 04B asset serving | merged | the storage-owning map asset worker, CASC lease, bounded serving |
| 05 Maps viewer | merged | see section 4 |
| 06 world models | merged | placed world models (MODF) and world-model-only maps in the world view |
| 07 doodads | merged | placed doodads (MDDF, world-model doodad sets), static |
| 08 streaming | merged | the tiles around the camera, budgets, loading progress |
| 09 liquids | merged (accepted 2026-10-21) | MH2O and MLIQ, the zone light's water colours, the High look (`09-liquids.md` 13) |

What exists in the player and is reused, not rewritten: the M2 renderer (models, animation, particle emitters in the
model viewer), the WMO renderer (Buildings: groups, materials, doodad sets of one world model), the terrain session and
builder, the orbit camera, and the IPC asset path.

## 3. The gap, feature by feature

| Feature of the video | Today (after Stage 09) | Milestone |
|---|---|---|
| Map list with search | done | 05 |
| Overhead map from minimap images | done in 05 (see 4) | 05 |
| Target position, tile and world coordinates, typed coordinates | done in 05 | 05 |
| Load World at the target | done in 08: the 3 x 3 around the target, streamed with the camera | 05 (one tile), 08 (the tiles around) |
| Loading progress by file count | done in 08: files answered of asked so far, tiles resident and loading (the Maps panel), the overhead map's tile rings | 08 (with streaming, where counts matter) |
| Free-flying camera | done in 05 | 05 |
| World-model-only maps (instances) entered from Maps | done in 06 (old-form WDTs: unsupported) | 06 |
| World models placed on terrain (MODF) | done in 06 (the loaded tile's) | 06 |
| Doodads (MDDF, WMO doodad sets) | done in 07 (the loaded tile's MDDF and the placed world models' sets; static, in their rest pose) | 07 |
| Several tiles, streaming around the camera | done in 08 (3 x 3 around the camera, kept within 2 tiles, at most 16 resident; Stormwind flown through on four clients) | 08 |
| Liquids (MH2O, WMO MLIQ) | done in 09, accepted 2026-10-21 under the owner's revised criteria: MH2O on the streamed tiles and MLIQ in placed world models on four clients, from the liquid tables' layouts settled per client (U14); water and ocean coloured by the zone light at the camera and drawn in an approximation of the game's High look (clear water over the bed, ripples from the client's ripple map, screen-space reflection; `WMV_WATER=low` keeps the Low look), validated functionally on all four clients, compared with in-game references on Retail only (one spot, the Southfury River); the original exit test superseded, not passed; Classic Era and MoP Classic visual parity unverified (accepted) (`09-liquids.md` 9, 11, 12, 13) | 09 |
| Sky, fog, outdoor light, time of day | no (Stage 09 reads the zone light's water colours and one sky colour at a fixed 12:00) | 10 |
| Animated doodads and particle effects in the world | no | 11 |
| World-model interior lighting (vertex colours, lights) | no | 12 |
| Far terrain (WDL, `_lod.adt`) | no | 13 |

## 4. What Stage 05 delivers, and what it does not

**Stage 05 delivers** (the first part is committed as "Add interactive single-tile Maps Viewer"; the second part is the
scope addition of 2026-10-15, recorded in `05-maps-viewer-ui.md` before it was built):

- the Maps viewer mode, the map catalogue in Browse with search, unavailable maps with their reasons;
- the tile grid with tile availability from the WDT and the client's storage;
- the overhead map: the map's minimap images (MAID word 7) under the grid, read and decoded off the GUI thread;
- a target position: a click on the map, or typed world X, Y and (optionally) Z, shown as tile and world coordinates;
- Enter world: the target's tile loaded through the unchanged Stage 03/04/04B pipeline, and the camera placed at the
  target (on the ground when Z is not given);
- the free camera in the world view (fly, look, climb, speed), with the tile overview as the other camera;
- display modes, scene and client switching, resource disposal, shutdown safety;
- a world view whose camera and coordinates are the game's world frame with a floating origin, so placed objects and
  more tiles join the same scene and the same camera later.

**Stage 05 does not deliver** (each belongs to a later milestone below): more than one tile, world models or doodads in
the world, world-model-only maps in the world view, liquids, lighting, particles in the world, sky, fog, far terrain,
loading progress by file count, and any claim of looking as in the game.

## 5. The milestones after Stage 05

The order follows the video. Its experience is mostly world models with doodads, so placement comes before streaming:
one tile, or one global world model, with its placed objects is a complete small world and needs no streaming. Each
milestone is one branch and one PR, with its own document in this folder, its tests, and the same rules as before:
evidence from the client files, all four clients, no claim of game parity without an in-game comparison. On 2026-10-21
the owner swapped milestones 10 and 12, so the outdoor light, sky and fog come before world-model interior lighting
(5.1).

| # | Milestone | Content | Entry | Exit (done when) |
|---|---|---|---|---|
| 06 | World models in the world | A world scene (the terrain session generalised) that holds placed objects beside the tiles, under the one floating origin. MODF placements of the loaded tile and the global world model of world-model-only maps, with the proven transform (`00-audit.md` 6.3-6.4, the DireMaul yaw), the existing WMO renderer per instance, group culling by extents. Maps enters world-model-only maps at a target inside the model's extents. | 05 merged | map 34 (Stormwind jail, world-model-only) entered and flown through; Retail 30_47's world models placed within their MODF extents; Era DireMaul's yaw applied; WMO pictures in Buildings unchanged |
| 07 | Doodads | MDDF placements of the tile and the doodad sets of placed world models (set 0 plus the placement's set, U15), static M2s with shared meshes and materials, bounds from the mesh (MLDX verifies the MDDF placement), instance budget | 06 | one city tile's doodads placed (MLDX boxes match), within a recorded memory budget; M2 pictures unchanged |
| 08 | Streaming | Tiles around the camera (from 3 x 3), residency and eviction, a prioritised cancellable scheduler, block-compressed upload, GPU and memory budgets, loading progress by file count. First: the transport-wide send cap (`04b-asset-serving.md` 18.2); the Maps entry cost was cut by the lazy Browse list (`05-maps-viewer-ui.md` 13.1) | 07 | a 3 x 3 city flown through within the budgets; frame and upload times recorded; no GUI-thread stall over 50 ms |
| 09 | Liquids | MH2O on terrain, MLIQ in world models; their tables' layouts per client | U14 settled (definitions, byte checks) | water tiles against references on Retail and Era. **Done 2026-10-21** under the owner's revised acceptance: the Retail Southfury River comparison and functional validation on four clients; this exit test superseded, not passed; Era and MoP visual parity unverified (accepted; `09-liquids.md` 13) |
| 10 | Light, sky and fog (outdoor) | First the rest of U16: the light tables' sky, light and fog columns, their units and each client's fog data, settled from the bytes and in-game comparisons. Then the light at the camera over the whole day (Stage 09's selection generalised, with a time of day), a sky drawn behind the world from the client's sky colours, the outdoor light and fog on terrain, exterior world-model groups, outdoor doodads and liquids, a pinned time of day for pictures, and a switch that reproduces Stage 09's pictures. The small foundation Stage 12 builds on: an indoor or outdoor context per world-model group and doodad. Plan and scope: `10-light-sky-fog.md` | 09 merged; settling U16's remainder is the stage's first technical work | per-client comparison against in-game references at set times of day (Classic Era and MoP Classic only if the owner can take them); Models, Buildings, Characters (on the owner's tabard-fixed host) and the Stage 07 battery unchanged; Stage 09's pictures reproduced with the switch off |
| 11 | World effects | animated doodads and their particle emitters in the world (the model viewer's emitter runtime), with an effect budget; they take Stage 10's fog and light | 07, 10 | the emitters of a tile's doodads run within the budget |
| 12 | World-model interior lighting | vertex colours (MOCV) in interior and exterior groups, the interior/exterior blend, the model's lights (MOLT, MOLR), interior ambient (MOHD), the model's fog (MFOG), the doodads of interior groups, the camera inside a model; the map's placed lights (`_lgt.wdt`) if the owner agrees | 06, 10 (the outdoor light the interiors blend with) | interiors against in-game references |
| 13 | Far terrain | `_lod.adt`, WDL heights (16 x 16 as chunk means), world-model LOD groups, under Stage 10's fog and sky | U17 settled, 08, 10 | far terrain within the WDL's 1-yard truncation |

After 13 the viewer has every element the video shows. Polish that the video implies but no milestone above names --
saved positions, a camera speed setting, a minimap of the camera's position while flying -- is listed per milestone
when it becomes cheap, never as a stage of its own.

### 5.1 Milestones 10 and 12 swapped (2026-10-21)

**The owner's reason.** Without the game's sky, outdoor light and fog, terrain, buildings and water cannot be compared
with the game: every picture differs in its light before it differs in anything else. Stage 09 met this directly. Its
remaining differences against the in-game references were classed as waiting for the light, sky and fog
(`09-liquids.md` 12.10, class B). So the outdoor light comes first, and world-model interior lighting follows it.

**What changed.** Light, sky and fog (outdoor) is now 10; world-model interior lighting is now 12. 11 and 13 keep their
content and numbers, and both gain 10 as an entry (fog and the outdoor light).

**Dependencies checked** (the evidence is in `10-light-sky-fog.md`, section 2):

- **10 needs nothing from 12 except a small foundation that 10 builds itself.** The outdoor light must reach only the
  outdoor parts of a world model. Each group is marked exterior (0x8) or interior (0x2000), exactly one of the two on
  every group in a Retail census of 531 groups, and the doodads of interior groups are known from the model's own lists.
  One material serves both kinds of group (166 of 1,010 drawn materials in that census), so the context cannot be a
  property of the shared material: it goes per renderer, or per (material, context) copy on the world's own prototypes.
  What interiors show until 12 is the owner's choice (proposed: today's look). Exterior groups take the outdoor light.
  Most of them also carry vertex colours (MOCV); on 2026-10-21 the owner asked for exterior MOCV to come into 10 if the
  client's own shader programs establish its behaviour reliably, with interior lighting, MOLT, interior ambient and
  interior fog staying in 12.
- **12 gains from the order.** Its interior/exterior blend needs the outdoor light to blend with; under the old order
  that half would have been a stand-in.
- **11 gains.** Emitters and animated doodads take 10's fog and light from the start.
- **13 gains.** Far terrain needs the fog and the sky behind it. Stage 10 draws the sky as a background and computes fog
  from the view distance and the game's own view-distance setting (a value of its own, not the camera's far plane), so 13
  can move the far plane without changing 10's picture.
- **09 is affected, not changed.** Stage 10 changes the water's inputs (the time of day, fog, the sky in reflections),
  not the water model or its tuned values. With the new light switched off, Stage 09's pictures are reproduced exactly.
  The class B differences are re-measured in 10; any water retuning is a follow-up the owner approves.

**Risk R12 restated.** The audit's R12, "scope creep into lighting/sky before its data is understood", was mitigated by
"W12 last, behind U16". It now reads "behind U16": Stage 10 starts by settling U16's remainder (the columns, units and
per-client fog data, `00-audit.md` 9), and no shading is tuned before the data it uses is established per client and
compared in game.

**Old numbers in older texts.** Documents and code written before 2026-10-21 keep their wording as the record. In
`09-liquids.md` and in the comments of `LightCatalog.h`, `UnityIpcServer.h` and `bin_support/dbd/LightData.dbd`, "Stage
12" means light, sky and fog, now Stage 10; Stage 10's own diff updates the code comments it touches anyway.
`06-world-models-in-world.md` lists lighting, sky and fog among the "later milestones (roadmap 07-10)". The audit's branch plan (`00-audit.md` 8.1,
13) has its own W-numbers: W10 (far LOD) is milestone 13, W11 (Maps mode) is 05, W12 (light, sky, fog) is 10.

## 6. Rules that hold for every milestone

- **One world scene, one camera.** Terrain, world models, doodads and liquids are parts of one scene in the game's
  world frame, positioned relative to one floating origin (double precision, `00-audit.md` 6.6). The free camera of
  Stage 05 is that camera; nothing frames a placed object.
- **No terrain assumption.** A map is entered at a position, not at a tile. Which parts exist at that position --
  terrain, world models, liquids -- is decided from the map's files.
- **Reuse.** The M2 and WMO renderers are used per instance, not copied. Shared data (meshes, materials, textures) is
  loaded once per scene and referenced by instances.
- **The host reads, the player parses.** The map asset worker reads every file off the GUI thread; the player parses
  every map format with the Stage 01 readers.
- **Budgets before breadth.** Each milestone records its memory, GPU and frame-time costs on real maps of all four
  clients before the next adds more.
