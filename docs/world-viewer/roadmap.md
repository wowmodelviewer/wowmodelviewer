# World viewer roadmap (revised 2026-10-15)

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
| **05 Maps viewer** | this branch | see section 4 |

What exists in the player and is reused, not rewritten: the M2 renderer (models, animation, particle emitters in the
model viewer), the WMO renderer (Buildings: groups, materials, doodad sets of one world model), the terrain session and
builder, the orbit camera, and the IPC asset path.

## 3. The gap, feature by feature

| Feature of the video | Today (after Stage 05) | Milestone |
|---|---|---|
| Map list with search | done | 05 |
| Overhead map from minimap images | done in 05 (see 4) | 05 |
| Target position, tile and world coordinates, typed coordinates | done in 05 | 05 |
| Load World at the target | one tile at the target | 05 (one tile), 08 (the tiles around) |
| Loading progress by file count | the state line only | 08 (with streaming, where counts matter) |
| Free-flying camera | done in 05 | 05 |
| World-model-only maps (instances) entered from Maps | listed, not enterable | 06 |
| World models placed on terrain (MODF) | no | 06 |
| Doodads (MDDF, WMO doodad sets) | no | 07 |
| Several tiles, streaming around the camera | no (single tile) | 08 |
| Liquids (MH2O, WMO MLIQ) | no | 09 |
| World-model interior lighting (vertex colours, lights) | no | 10 |
| Animated doodads and particle effects in the world | no | 11 |
| Sky, fog, outdoor light | no | 12 |
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
evidence from the client files, all four clients, no claim of game parity without an in-game comparison.

| # | Milestone | Content | Entry | Exit (done when) |
|---|---|---|---|---|
| 06 | World models in the world | A world scene (the terrain session generalised) that holds placed objects beside the tiles, under the one floating origin. MODF placements of the loaded tile and the global world model of world-model-only maps, with the proven transform (`00-audit.md` 6.3-6.4, the DireMaul yaw), the existing WMO renderer per instance, group culling by extents. Maps enters world-model-only maps at a target inside the model's extents. | 05 merged | map 34 (Stormwind jail, world-model-only) entered and flown through; Retail 30_47's world models placed within their MODF extents; Era DireMaul's yaw applied; WMO pictures in Buildings unchanged |
| 07 | Doodads | MDDF placements of the tile and the doodad sets of placed world models (set 0 plus the placement's set, U15), static M2s with shared meshes and materials, bounds from MLDX, instance budget | 06 | one city tile's doodads placed (MLDX boxes match), within a recorded memory budget; M2 pictures unchanged |
| 08 | Streaming | Tiles around the camera (from 3 x 3), residency and eviction, a prioritised cancellable scheduler, block-compressed upload, GPU and memory budgets, loading progress by file count. First: the transport-wide send cap (`04b-asset-serving.md` 18.2); the Maps entry cost was cut by the lazy Browse list (`05-maps-viewer-ui.md` 13.1) | 07 | a 3 x 3 city flown through within the budgets; frame and upload times recorded; no GUI-thread stall over 50 ms |
| 09 | Liquids | MH2O on terrain, MLIQ in world models; their tables' layouts per client | U14 settled (definitions, byte checks) | water tiles against references on Retail and Era |
| 10 | World-model lighting | vertex colours (MOCV), interior and exterior groups, the model's lights (MOLT), ambient | 06 | interiors against in-game references |
| 11 | World effects | animated doodads and their particle emitters in the world (the model viewer's emitter runtime), with an effect budget | 07 | the emitters of a tile's doodads run within the budget |
| 12 | Light, sky and fog | Light tables, `_lgt.wdt`, `_fogs.wdt`, sky | U16 settled per client | per-client comparison against in-game references |
| 13 | Far terrain | `_lod.adt`, WDL heights (16 x 16 as chunk means), world-model LOD groups | U17 settled, 08 | far terrain within the WDL's 1-yard truncation |

After 13 the viewer has every element the video shows. Polish that the video implies but no milestone above names --
saved positions, a camera speed setting, a minimap of the camera's position while flying -- is listed per milestone
when it becomes cheap, never as a stage of its own.

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
