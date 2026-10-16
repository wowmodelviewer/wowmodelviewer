# World viewer 07 -- doodads

Branch `world/07-doodads`, from `develop` c223ea97 (Stage 06, PR #120). This document is written in the order the work
was done: the architecture first (section 1, written before any code), then the evidence, the implementation and its
verification.

Evidence grades: **PROVEN_RUNTIME** (observed in the running viewer or player on real client data), **PROVEN_SOURCE**
(read in the source or the client's own files), **INFERRED** (follows from proven facts, not observed directly),
**UNKNOWN**. Test outcomes: **PASS**, **FAIL**, **SKIP** (not run, said why), **UNSUPPORTED** (the case cannot be run on
this data or by this design, said why).

Clients (the same installation throughout):

| Client | Product | Build |
|---|---|---|
| Retail | wow | 12.1.5.70391 |
| Classic Era | wow_classic_era | 1.15.9.70003 |
| MoP Classic | wow_classic | 5.5.4.70268 |
| Classic Beta | wow_classic_beta | 1.60.1.70425 |

## 1. Architecture (proposed before implementation)

### 1.1 Starting point

- `develop` = c223ea97 (PR #120 merged); Stage 06 (aa1c3cab), the Maps icon fix (#118, fe5cec34) and the lazy Browse
  list (#119, aa57c0f0) are its ancestors. The branch is cut from it, not from the deleted Stage 06 branch.
- The owner's build (`build64\Source\wowmodelviewer\Release`): exe SHA-256 5C1347C7, wow.dll 0ED07B2C, core.dll
  8472119A, player Assembly-CSharp 8E8C7D06 -- the Stage 06 deploy, built from a local integration branch
  (tmp/w06-deploy-integration f16c5d72 = develop's tree plus two uncommitted wow.dll fixes, the bulky-belt tabard and the
  .chr tabard hang, still uncommitted in their own worktrees). A Stage 07 deploy must carry both fixes again the same
  way, never by merging them into this branch.

### 1.2 What exists and what is reused

Investigated before planning (the findings are in sections 2-4):

| Component | Today | Stage 07 |
|---|---|---|
| `M2Parser`, `M2SkinParser`, `BlpDecoder` | pure functions of bytes, no Unity calls; the model path runs them on the main thread | reused unchanged, on the session's worker threads |
| `WmvModelBuilder.Build` | one M2 from (model, skin, decoded textures); skinned when the model has bones; emitters; a per-build texture cache; a CPU copy of each texture; counted in `WmvRuntimeModel.Live` | reused through **opt-in build options** (static mesh, no emitters or animators, textures not kept readable, textures from a shared provider, outside the model count, cleanup on failure). Callers that do not set them build byte-identical models (Models, Characters, mounts) |
| Model path (`loadWoWModel`, `getAsset`, the model slot) | GUI-thread reads, no content-key check, adoption disposes the scene | **not used** for doodads: it would end the world scene |
| `AdtParser.ParseObj0` | gives every MDDF record (`AdtObj0.Doodads`); Stage 06 logs the count and drops them | the MDDF records become doodad placements |
| `WmoParser` | parses MODS, MODI, MODD (quaternion x, y, z, w) and keeps MODR raw; the root stays alive with each world prototype | reused; MODR decoded for group membership |
| `PlacementTransform` (Tile and Global frames), `WorldToUnity.PositionD`, `WmvWorldModels.UnityRotation` | MODF and the WDT record, proven in Stage 06 | MDDF through the Tile frame (the audit proved the same rule on MLDX boxes); WMO doodads composed with their world model's placement in double |
| Stage 06 world scene (`WmvWorldScene`, `WmvWorldModels`) | prototypes, instances, culling, disposal, tallies | a second family beside it: M2 prototypes, doodad instances, a shared texture cache, disposed with the scene |
| Stage 04B/06 map asset serving (`getMapAsset`, the host's worker, decisions, budgets) | roles wdt, rootAdt, tex0Adt, texture, obj0Adt, wmoRoot, wmoGroup, wmoTexture | three new roles: `m2Model`, `m2Skin`, `m2Texture`, with their own order rules, kind checks, caps and byte budgets |
| Free camera and floor rules | terrain and world-model floors | unchanged: doodads are never floors |

### 1.3 The plan

1. **M2 assets.** Per unique M2 FileDataID of a load: `m2Model` (kind: `MD21` with an `MD20` payload), parsed on the
   worker; its first skin (`SFID[0]`, `m2Skin`, kind `SKIN`); every non-zero texture (`TXID`, `m2Texture`, kind
   `BLP2`), each FileDataID read once per load and decoded once. A slot without a file (a replaceable texture) has no
   source for a doodad: the model is built without it and counted as partially supported.
2. **Prototypes.** One static prototype per unique M2: `WmvModelBuilder.Build` with the new options, inactive, on the
   world layer (30), out of the model count. Its textures come from the scene's texture cache, keyed by file and render
   state (alpha kept or dropped, wrap per axis), so models that share a texture share one upload. Prototypes are built
   under a per-frame time budget, not one per frame.
3. **Outdoor placements (MDDF).** The tile's obj0 records, deduplicated by uniqueId, flag 0x40 (FileDataID) required,
   placed by the Tile frame `W = (32T - p2, 32T - p0, p1)`, `R = Rz(a1+180) Ry(a0) Rx(a2)`, scale `u16/1024` -- the
   audit's byte proof (00-audit 6.4) is replayed on the four clients through the player's own code.
4. **WMO doodads.** For every placed world-model instance: the active sets are set 0 plus the placement's doodad-set
   index (MODF word 58) when it names another set; each MODD record of those ranges names its M2 through MODI. Its world
   transform is composed in double: `W_d = W + s R p`, `R_d = R R(q)`, `s_d = s s_q` (p, q, s_q from MODD, q stored x, y,
   z, w; the convention is measured, section 3). A WMO placement that was not placed contributes no doodads (its doodads
   are not planned, so not counted); a doodad that only a group file that could not be read lists is "unsupported".
5. **Instances.** One GameObject per doodad (a `MeshFilter` and a `MeshRenderer` sharing the prototype's mesh and
   materials), flat under a doodad holder in the scene root, positioned from the double world transform and the scene's
   floating origin -- one origin for terrain, world models and doodads. Placement runs under a per-frame budget.
6. **Ownership.** The scene owns its doodad instances, M2 prototypes and texture cache; disposal destroys instances, then
   prototypes, then cached textures (an instance never destroys shared resources). A load that ends before its scene is
   committed builds nothing; a client switch, Clear terrain, a model load, a newer load and shutdown all dispose the scene.
7. **Order and responsiveness.** Terrain (or the WMO-only map's world model) first, then the world models, then -- once
   the world models are settled -- the doodads. The camera is placed when the world models are settled (its floors), not
   after the doodads; nothing re-frames when a doodad appears. Exactly one report per load, after the doodads settle,
   with their own state beside the world models' and an overall assembly state.
8. **Culling.** Each doodad instance is frustum-tested by its own bounds with the Stage 06 group culling; no portal rule.
9. **Protocol 17**, host and player together; an older player is never offered doodads and the host says so.

What does **not** change: the terrain's mesh, materials and their reference outputs; the world models' placement; the
Buildings and Models paths (their regression captures stay byte-identical); the camera's floor rules.

## 2. The formats, as the four clients store them

Read from the client files of the Stage 07 corpus (`Tools/world-audit/world07_corpus.py`: per client the WDTs of maps 0,
34 and 429, tiles 30_47 and 31_48 -- root, obj0, obj1 --, every world model they place with its LOD0 groups, every M2
their MDDF and MODI name, each M2's first skin and its textures; a manifest of SHA-256s and `census.json`). Retail 3,945
files, Classic Era 1,990, MoP Classic 2,702, Classic Beta 2,259.

### 2.1 MDDF (obj0) -- PROVEN_SOURCE

36 bytes: u32 name @0, u32 uniqueId @4, f32 x3 position @8 (file frame), f32 x3 rotation @20 (degrees a0, a1, a2), u16
scale @32 (scale / 1024), u16 flags @34. Every record of the four clients' obj0 files names its M2 by FileDataID (flag
0x40); no obj0 has MMDX or MMID. A record repeated in another tile's obj0 is the same doodad (the same uniqueId and
contents): the plan keeps one per uniqueId. On the corpus tiles no uniqueId repeats inside one obj0 with different contents.

| Client | 30_47 records | 30_47 M2s | most of one M2 | scale not 1 | tilted | 31_48 records |
|---|---|---|---|---|---|---|
| Retail | 2,451 | 178 | 291 | 1,279 | 1,022 | 650 |
| MoP Classic | 2,291 | 152 | 229 | 1,194 | 941 | 636 |
| Classic Beta | 290 | 37 | 63 | 211 | 101 | 230 |
| Classic Era | 0 | 0 | 0 | 0 | 0 | 219 |

### 2.2 MODS, MODI, MODD, MODR (world models) -- PROVEN_SOURCE

- **MODS** (root), 32 bytes per set: name (20 bytes, NUL-terminated), u32 first MODD record, u32 count, u32 unused. The
  sets are contiguous ranges of MODD; set 0 is named "Set_$DefaultGlobal" on most roots and "DefaultGlobal" on three
  (Retail 3389421 and Classic Beta 106965, both multi-set; Classic Beta 7962574, a single set).
- **MODI** (root): u32 FileDataID per entry. No root of the four clients has MODN (a name table); MODD names its M2
  through MODI only.
- **MODD** (root), 40 bytes: u32 @0 = MODI index (low 24 bits) and flags (high 8), f32 x3 position @4 (the world
  model's own space), f32 x4 quaternion @16 stored x, y, z, w (unit length to 1e-3 on every record; 77-80 % turn about z
  only), f32 scale @32, u32 colour @36 (BGRA). The archived host C++ reads w first and requires MODI before MODD: it is
  wrong and never ran (no doodad was ever drawn by the OpenGL viewer of these clients); the player's parser reads x, y,
  z, w (`WmoParser`, `WmoDoodadDef`).
- **MODR** (group): u16 MODD indices (absolute, into the root's MODD) of the doodads that stand in that group.
- **MOHD and MOGI boxes do not bound doodads**: MOHD's box is the union of the MOGI boxes, and a MOGI box is its group's
  vertex box (Stage 06); doodads stand inside or beside them.

### 2.3 The doodads' M2 files -- PROVEN_SOURCE

| Client | M2s | first chunk MD21 | versions | skin profiles | SFID / TXID | SKID, BFID, AFID | replaceable slots | missing files |
|---|---|---|---|---|---|---|---|---|
| Retail | 1,126 | 1,126 | 272: 961, 274: 165 | 1: 1,119; 2: 5; 4: 2 | 1,126 / 1,126 | none | none | none |
| MoP Classic | 732 | 732 | 274: 732 | 1: 732 | 732 / 732 | none | none | none |
| Classic Beta | 582 | 582 | 272: 573, 274: 9 | 1: 579; 2: 3 | 582 / 582 | none | none | texture 137237 |
| Classic Era | 513 | 513 | 272: 2, 274: 511 | 1: 510; 2: 3 | 513 / 511 | none | model 201660 (type 11) | none |

Every doodad M2 is the chunked form (MD21, its MD20 payload at byte 8) with SFID; none needs a separate skeleton or
animation file. The largest files: an M2 of 2,423,632 bytes, a skin of 214,960, a texture of 1,399,300 (all Retail).

## 3. Transform evidence

### 3.1 MDDF: the Tile frame, on four clients -- PROVEN_BYTES, replayed through the player's code

The audit (00-audit 6.4) proved on Retail that an MDDF record places its M2 by the MODF rule: W = (32T - p2, 32T - p0,
p1), R = Rz(a1 + 180) Ry(a0) Rx(a2), scale u16/1024 -- the client's own obj1 MLDX box of each doodad is its M2's header box
(MD20 + 0xA0) placed by that rule. `Tests/DoodadCorpusTests.cs` replays it through `DoodadPlan.FromMddf` and
`WorldDoodad.PlaceBox` (the code the player places doodads with) on every MDDF doodad of both corpus tiles of all four
clients, MLDX matched through MLDD by uniqueId:

| Client | Tile | Doodads compared | Within 0.5 yd of MLDX | Tilted, within | Boxes the half turn moves > 1 yd | fit with +180 | fit without |
|---|---|---|---|---|---|---|---|
| Retail | 30_47 | 2,451 | 2,451 | 1,022 / 1,022 | 380 | 380 | 0 |
| Retail | 31_48 | 650 | 650 | 249 / 249 | 134 | 134 | 0 |
| MoP Classic | 30_47 | 2,291 | 2,291 | 941 / 941 | 362 | 362 | 0 |
| MoP Classic | 31_48 | 636 | 636 | 238 / 238 | 132 | 132 | 0 |
| Classic Beta | 30_47 | 290 | 290 | 101 / 101 | 146 | 146 | 0 |
| Classic Beta | 31_48 | 230 | 230 | 35 / 35 | 75 | 75 | 0 |
| Classic Era | 31_48 | 219 | 219 | 31 / 31 | 66 | 66 | 0 |

(Classic Era's 30_47 has no MDDF record.) Every placed box lands within 0.5 yd of the client's own box; a box centred on
its model's origin places the same without the rule's half turn, so the half turn is judged on the boxes it moves, where
the rule fits all of them and its absence none. **MDDF position, rotation and scale: PROVEN_BYTES on four clients.**

### 3.2 MODD composition -- PROVEN_SOURCE (the arithmetic), INFERRED (that the client composes so)

A MODD record stands in its world model's own space; the world model's placement (Stage 06, proven) takes that space to
the world. Composed in double: W_d = W + s R p, R_d = R R(q), s_d = s s_q (W, R, s the placement's; p, q, s_q the
record's). Checked by construction (`Tests/DoodadTests.cs`: a doodad's model point lands where the world model's own
transform takes the MODD-transformed point, for a moved, tilted and scaled world model) and against independent arithmetic
on real records (3.6). No client file stores a doodad's world transform, so the composition itself is INFERRED -- the
only reading under which a doodad keeps its place in its world model wherever the world model is placed, and the one the
Stockade and Stormwind pictures show (section 9).

### 3.3 The MODD rotation convention: R(q), no turn after it -- PROVEN_BYTES (statistical), four clients

A quaternion can be applied as R(q) or as its transpose (the inverse turn); a turn about z only differs between the two
by the sign of its angle, so a wrong reading turns most doodads the wrong way about their own origin. The client files
hold no doodad box to compare with (no MLDX for world-model doodads), so the convention is measured against the world
models' own walls: for every MODD record whose M2 box is elongated (one horizontal side at least 1.5 times the other) or
off-centre (its centre at least 30 % of its size from the model's origin), the box is placed in its world model under
each candidate, and horizontal segments from its centre to its corners at 40 % and 75 % of its height are tested against
the world model's wall triangles (|n.z| below half |n|); a box whose segment ends more than a quarter of its half-size
past a wall counts as "in a wall". Doodads are authored against walls, so the convention the client uses puts the fewest
there. In-wall boxes per candidate (`Tests/DoodadCorpusTests.cs`; the same numbers as an independent probe written beside
it):

| Client | Elongated | R(q) | R(q)^T | only R(q) | only R(q)^T | Off-centre | R(q) | R(q)^T | R(q)Rz90 | R(q)Rz180 | R(q)Rz270 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Retail | 1,735 | 310 | 493 | 33 | 216 | 462 | 65 | 139 | 211 | 137 | 191 |
| Classic Era | 2,023 | 215 | 493 | 43 | 321 | 772 | 91 | 178 | 292 | 161 | 255 |
| MoP Classic | 1,595 | 250 | 415 | 31 | 196 | 440 | 55 | 127 | 195 | 127 | 179 |
| Classic Beta | 2,633 | 354 | 660 | 82 | 388 | 782 | 93 | 183 | 293 | 170 | 262 |

R(q) puts fewer boxes in walls than its transpose on every client (where the two disagree, R(q) is wrong 4 to 7 times
less often). A half turn after R(q) leaves a box centred on its model's origin where it was, so the elongated boxes cannot
tell R(q) from R(q)Rz180 (on Classic Beta R(q)Rz180 is even 5 boxes better, 349 against 354); the off-centre boxes can,
and on them R(q) beats its transpose and every turned variant on every client. Quarter turns move every elongated box and
lose on both families. Boxes smaller than 0.6 yd across or 0.4 yd high are not tested (too small for the rule). No
per-model correction is applied. The residual "in a wall" counts are boxes that cross thin walls (shelves, banners, beams
set into walls) under the test's simple rule; most of them are the same under every candidate.

### 3.4 Doodad sets: which are drawn -- the rule INFERRED (U15), its consequences checked

The sets drawn for a placement are set 0 plus the set its MODF record names (word 58, the u16 the audit left as U15),
when that is another set of the root; a word naming no set draws set 0 alone, said in the report. Which sets the game
draws is not stored in any file; the evidence:

- **Set 0 adds, it is never one of the alternatives**: on every multi-set root of the four clients (Retail 6 of 24 roots,
  Classic Era 1 of 7, MoP Classic 2 of 21, Classic Beta 2 of 9) no record of another set stands where a set-0 record
  stands with the same model. The other sets overlap one another on two roots -- 106965 (the same 66 records on Retail,
  Classic Era and MoP Classic) and 106973 (212 of Classic Beta's 278 overlapping records) --: alternatives of one place,
  set 0 the part every alternative shares. On the roots whose placements choose a set on the corpus tiles the non-global
  sets do not overlap (892927: 0 of 69 records; 1725413: 0 of 109; 106899: 0 of 183), so the overlap evidence rests on two
  roots.
- **Roots whose set 0 is empty** (Retail 880031 "Set_Stormwind", 948620 "SET_1": set 0 empty, set 1 every record) draw
  nothing without the placement's set: the word must name it, and it does (1 on both placements).
- **The words name sets the roots have**: every placement of the corpus tiles names a set its root has (0 remarks on all
  four clients). The WMO-only maps' global records name set 0 (word 58 is 0 on all eight), and their roots have one set.
- **What tells "set 0 plus the named set" from "the named set alone"**: a placement naming another set of a root whose set 0
  is not empty. On the corpus tiles that is Retail 892927 (a set 0 of 14 records: lanterns, glows, smoke, the hearth's
  logs) -- small sets 0 that the alternative rule would drop. The rule matters there, and nothing in the files decides it.
- The placements that choose a set on tile 30_47: Retail 892927 x2 (set 2 "Set_InteriorDoodads" of 3; "Set_Exterior"
  not drawn), 1725413 (set 1 "Set_Embassy"), 880031 and 948620 (set 1); MoP Classic 106965 x2 (set 3 "Set_E" of 6) and
  106899 (set 2 "Set_A" of 3); Classic Beta 106965 (set 2 "Set_C" of 6) and 106973 (set 1 "Set_A" of 6).

Graded INFERRED: consistent with all of the above and with every source in the repository (the audit's counter, the
archived host code), not observed in the game; the evidence that tells it from the alternative is thin (above). A diagnostic, `-wmvDoodadSets=all`, draws every set for comparison
(section 9's set pictures).

### 3.5 MODR: group membership -- PROVEN_SOURCE, not used for drawing

On every LOD0 group of the corpus (Retail 457, Classic Era 437, MoP Classic 477, Classic Beta 428) MODR is present
exactly when the group's flag 0x800 is set (0 differ) and every index lies inside its root's MODD (0 outside). Every
MODD record is listed by some group (0 unlisted); some by several (Retail 63 of 8,970, Classic Era 139 of 10,439, MoP
Classic 66 of 8,589, Classic Beta 312 of 11,057), and a few groups list records of several sets (4, 1, 3, 6). The plan
decodes it (`DoodadPlan.GroupMembership`, tested) but draws no doodad by it: with no portal or indoor/outdoor rule in this
stage (none is established), a doodad is drawn when its own box is in the camera's view (section 5).

### 3.6 Reference cases -- PROVEN (independent arithmetic)

Computed from the raw bytes by a separate script (its own chunk walk and matrices, not the player's code) and pinned in
`Tests/DoodadCorpusTests.cs` to 1e-6 yd, Retail tile 30_47:

| Doodad | M2 | W (world) | scale | model (1, 0, 0) placed at |
|---|---|---|---|---|
| MDDF[0], uniqueId 5496790 | 189929 | (-8028.0794, 608.2780, 85.1336) | 1.12890625 | (-8027.5845, 607.2633, 85.1336) |
| WMO 892927 uniqueId 19042379, MODD 29 (set 2) | 307027 | (-8047.2322, 824.5371, 68.5623) | 0.346324 | (-8047.0667, 824.5086, 68.8652) |
| WMO 892927 uniqueId 19042630, MODD 29 (set 2) | 307027 | (-8254.3597, 766.3078, 79.9045) | 0.346324 | (-8254.2209, 766.4023, 80.2074) |

The same MODD record under the world model's two placements: two doodads, each composed with its own placement.

## 4. Census and budgets

Per load, the corpus's doodads (`census.json`; the plan's own counts reproduce it exactly on every load:
`Tests/DoodadCorpusTests.cs`). "Textures" counts every TXID entry of the load's M2s; the player fetches only those their
batches bind (Retail 30_47: 522 of 578).

| Client | Load | MDDF doodads | WMO-set doodads | In all | M2s | Textures | Model + skin bytes | Texture bytes | Vertices | Triangles |
|---|---|---|---|---|---|---|---|---|---|---|
| Retail | 30_47 | 2,451 | 3,269 | 5,720 | 616 | 578 | 16,338,810 | 58,576,912 | 212,958 | 190,163 |
| Retail | 31_48 | 650 | 2,907 | 3,557 | 333 | 354 | 8,280,176 | 20,991,104 | 94,184 | 85,948 |
| Retail | map 34 | 0 | 748 | 748 | 24 | 38 | 313,356 | 2,618,352 | 4,400 | 2,930 |
| Retail | map 429 | 0 | 3,543 | 3,543 | 186 | 190 | 2,581,788 | 7,072,472 | 31,888 | 32,447 |
| Classic Era | 30_47 | 0 | 6,026 | 6,026 | 279 | 276 | 3,205,854 | 7,760,968 | 43,364 | 37,072 |
| Classic Era | 31_48 | 219 | 6,062 | 6,281 | 298 | 291 | 3,320,698 | 8,017,836 | 44,721 | 38,240 |
| Classic Era | map 34 | 0 | 740 | 740 | 20 | 28 | 153,294 | 545,024 | 1,904 | 1,632 |
| Classic Era | map 429 | 0 | 3,541 | 3,541 | 186 | 178 | 2,562,846 | 5,836,400 | 31,524 | 32,292 |
| MoP Classic | 30_47 | 2,291 | 2,888 | 5,179 | 414 | 413 | 10,662,036 | 24,727,228 | 120,415 | 122,333 |
| MoP Classic | 31_48 | 636 | 3,091 | 3,727 | 326 | 331 | 7,421,496 | 14,463,564 | 79,289 | 81,164 |
| MoP Classic | map 34 | 0 | 740 | 740 | 20 | 27 | 216,436 | 568,052 | 2,979 | 2,628 |
| MoP Classic | map 429 | 0 | 3,543 | 3,543 | 186 | 184 | 2,584,766 | 6,370,496 | 32,339 | 32,842 |
| Classic Beta | 30_47 | 290 | 6,248 | 6,538 | 354 | 386 | 5,788,800 | 16,213,284 | 84,000 | 55,360 |
| Classic Beta | 31_48 | 230 | 6,142 | 6,372 | 330 | 355 | 4,897,180 | 13,203,952 | 70,839 | 47,921 |
| Classic Beta | map 34 | 0 | 748 | 748 | 24 | 32 | 213,114 | 2,540,568 | 2,796 | 2,254 |
| Classic Beta | map 429 | 0 | 3,543 | 3,543 | 186 | 198 | 2,920,902 | 8,319,800 | 38,291 | 32,353 |

The budgets (host constants `UnityIpcServer::MAX_M2_*` and `MAX_DOODAD_INSTANCES_PER_LOAD`, sent with every loadWorld to a
protocol-17 player; the host enforces the files and bytes, the player the instances):

| Budget | Value | Largest measured |
|---|---|---|
| one model or skin file | 8 MiB | 2,423,632 bytes (an M2), 214,960 (a skin) |
| model and skin bytes per load | 128 MiB | 16.3 MB (Retail 30_47) |
| one texture | 8 MiB | 1,399,300 bytes |
| texture bytes per load | 128 MiB | 58.6 MB (Retail 30_47) |
| unique models (and skins) per load | 1,024 | 616 (Retail 30_47) |
| texture ids per load | 2,048 | 578 (Retail 30_47) |
| doodads placed per load | 16,384 | 6,538 (Classic Beta 30_47) |

The player's own limits (not on the wire): 4 doodad requests out at once (a parse or decode counts until done), 8 models
fetched and not yet built, prototype builds 6 ms and placements 4 ms of a frame (at least one each), the doodads'
timeout 180 s from the world models' settle, and GPU budgets of 1 GiB for doodad textures and 256 MiB for doodad geometry
(estimated before a prototype is built). Every budget is at least twice the largest load measured.

## 5. Implementation

### 5.1 The pure layer (`Assets/Scripts/Wow/World/DoodadScene.cs`) -- no UnityEngine

- `DoodadPlan.FromMddf(records, frame, notes)`: one `WorldDoodad` per uniqueId (the first record kept, the others counted,
  a repeat with different contents noted), placed by `PlacementTransform` (the Tile frame for an obj0). A record without
  flag 0x40, naming FileDataID 0 or one past what a request carries is kept, `Unsupported` set.
- `DoodadPlan.SelectSets(root, uniqueId, wmo, requested)`: set 0 plus the requested set (3.4), each range clamped to MODD,
  remarks for a set past the root's or a range past MODD. `DoodadPlan.AllSets`: the `-wmvDoodadSets=all` diagnostic.
- `DoodadPlan.FromWmo(placement, owner, root, selection)`: every MODD record of the active sets, its M2 through MODI,
  composed with the placement in double (3.2, 3.3); an index past MODI, a MODI entry 0 or a scale not above 0 is
  `Unsupported`.
- `DoodadPlan.QuaternionMatrix(q)`: R(q), q normalised (a zero or non-finite quaternion is the identity).
- `DoodadPlan.GroupMembership(root, groups)`: MODR decoded per MODD record (3.5).

### 5.2 Reuse of the Models renderer (`WmvModelBuilder`)

A doodad prototype is built by `WmvModelBuilder.Build` -- the parser's output, the batch and material rules, the combiner,
the blend modes and the shader of the Models view -- with new opt-in `WmvBuildOptions`:

| Option | Effect | Default (every other caller) |
|---|---|---|
| `Static` | a MeshFilter and MeshRenderer whatever bones the model has (the rest pose the file stores: the builder's own bind pose), no emitters, no animator; the material animator still applies the rest state (time 0) once | off: skinned when the model has bones, emitters, animator |
| `TextureProvider` | the builder asks it for each texture (slot, image, alpha treatment, wrap) instead of uploading one; the provider owns it | null: the builder uploads its own |
| `ReleaseOnFailure` | a build that throws destroys its mesh, materials, textures and objects and leaves the model count as it was | off |

`WmvRuntimeModel.LeaveLiveCount()` takes a prototype out of `WmvRuntimeModel.Live` (the Models view's count), as
`WmvRuntimeMapObject.LeaveLiveCount` does for world-model prototypes. `WmvModelBuilder.BatchTextureSlots(model, skin)`
lists the slots a skin's batches bind (the textures a doodad fetches). With no option set the builder's path is the one it
was: the Models and Characters captures are compared byte for byte (section 10).

### 5.3 Dependency resolution

Per unique M2 of a load: `m2Model` -> `M2Parser.Parse` on the worker (the bytes' md5 checked against the host's content
key; the kind checked: MD21 with an MD20 payload) -> its first skin profile, `SFID[0]` (`m2Skin`, `M2SkinParser.Parse`
on the worker; kind SKIN) -> the textures its batches bind, by their `TXID` FileDataIDs (`m2Texture`, `BlpDecoder.Decode`
on the worker; kind BLP2), each id once per load, shared by every model that binds it. An M2 with no SFID entry is
"unsupported"; a skin with no batch (a model of emitters alone) "unsupported"; a slot with no FileDataID -- a replaceable
texture, or a texture an M2 without TXID names by path -- is left without its texture and the model is "partial", named.
A doodad whose MODD record only group files that could not be read list (MODR) is "unsupported" (it would stand where no
geometry was built); an MDDF record of scale 0 likewise.

### 5.4 Ownership

The world scene (`WmvWorldScene`) owns its doodads: `DoodadPrototypes` (one per M2), `Doodads` (one instance per placed
doodad) and `DoodadTextures` (the scene's texture cache). Disposal (`IWmvWorldModelView.DisposeDoodads`, from
`DisposeWorldScene`, before the world models): every instance (its own GameObject only), then every prototype (its root,
mesh and materials; `WmvRuntimeModel.Dispose`), then the cache's textures. Nothing of a doodad outlives its scene; a load
that ends before its world models settle has none. Tallies (`WmvWorldDoodads.Live*`) count what is alive, apart from the
Models view's and the world models'.

### 5.5 Shared textures and materials

- A texture is fetched and decoded once per load; its decoded pixels go when the last model waiting for it is built, and
  its file's bytes are kept to decode it again for a model that binds it later (the host serves an id once per load);
  everything goes when the doodads settle.
- An upload is made once per scene per (file, alpha treatment, wrap) -- `WmvDoodadTextures`, mip-chained RGBA32, never kept
  readable -- and handed to every prototype that binds it (Retail 30_47: 548 uploads, 1,096 uses). The GPU texture budget
  is checked against the cache's actual uploads (every variant) plus one upload of each file a model is the first to bind.
- A prototype's rest state is evaluated at time 0 for every track: the global-sequence clock the Models view shares
  (`WmvM2Animator.GlobalTimeMs`) is held at 0 for the build and restored.
- Materials are per prototype, as the Models builder makes them; every instance shares its prototype's `sharedMaterials`
  and `sharedMesh` (no copy, no component of the prototype cloned).

### 5.6 Instances and culling

An instance is a bare GameObject with a MeshFilter and a MeshRenderer, on layer 30 under the scene root: position
`WorldToUnity.PositionD(W, origin)` (the scene's one floating origin, as terrain and world models), rotation U = M R M^T
(`WmvWorldModels.UnityRotation`, the converter's model space is the same for M2 and WMO), uniform scale. Its box is the
prototype's mesh box placed (static). Once per frame in the world view each instance box is tested against the camera's
frustum; a renderer outside is disabled (counted: `doodadVisible`, `doodadCulled`). Doodads are never floors: they are
in neither `Geometries` nor the scene's box, so the camera's floor rules and the world's framing are unchanged.

### 5.7 The load's order and responsiveness

Terrain (or the WMO-only map's world model) first, then the world models, then -- once those are settled -- the doodads.
The camera is placed at the world models' settle (`IWmvWorldModelView.WorldModelsSettled`, before the first doodad request)
and nothing re-frames as doodads appear. Prototype builds and placements run under per-frame time budgets (6 ms and 4 ms,
at least one each). Exactly one `terrainTileLoaded` per load, two frames after the last placement, with the doodads beside
the world models and the whole world's assembly.

### 5.8 Protocol 17

| Message | Added |
|---|---|
| `loadWorld` | `doodads:true` and the seven budgets (section 4), only to a player that announced 17 |
| `getMapAsset` | roles `m2Model`, `m2Skin`, `m2Texture` |
| `worldProgress` | `doodadPhase`, `doodadInstances`, `doodadPlaced`, `doodadModels`, `doodadModelsBuilt`, `doodadModelsFailed`, `doodadFilesAsked`, `doodadFilesAnswered`, `doodadBytes` (-1 where not offered) |
| `terrainTileLoaded.world` | `assembly`; `doodads` {state, reason, mddfRecords, mddfPlacements, wmoDoodadRecords, wmoPlacementsWithDoodads, setsTotal, sets[<=64], instances, placed, failed, unsupported, notRequested, ended, models, modelsBuilt, modelsFailed, modelsUnsupported, modelsNotRequested, modelsPartial, textures, texturesDecoded, texturesMissing, textureUses, textureUploads, fileBytes, textureBytes, gpuGeometryBytes, gpuTextureBytes, timings{discoverMs, firstVisibleMs, fetchMs, parseMs, decodeMs, buildMs, buildMaxMs, placeMs, placeMaxMs, readyMs}, listTotal, list[<=128]} |
| `runtimeState` | `doodadInstances`, `doodadPrototypes`, `doodadMeshes`, `doodadMaterials`, `doodadTextures`, `doodadGpuGeometryBytes`, `doodadGpuTextureBytes`, `doodadVisible`, `doodadCulled`, `liveGameObjects`, `liveTexture2D`, `liveMeshes`, `liveMaterials` |

The host (`ModelViewer::decideMapAssetRequest`) serves the three roles only to a load that carries the budgets: a model
once the load's obj0 or one of its world-model roots was served, a skin or texture once a model was; each id once; the
count caps; a third read family (2 x models + textures) beside the terrain's and the world models'; the byte budgets with
the reads in flight reserved; the kind of the bytes read (`mapAssetKindProblem`). A player of protocol 16 is sent the
protocol-16 `loadWorld` and is served no doodad file; the Maps panel says "Doodads: not drawn by this Unity renderer build
(protocol 16; doodads need 17)." The Maps panel's world line gains the doodads ("Doodads: P of N placed (M models, K
textures); F failed, U unsupported, R not requested."), and its progress line their phase.

### 5.9 States

| Doodad | Meaning |
|---|---|
| placed | on screen |
| failed | its model could not be had or built (the host's answer quoted, a parse failure, a build that threw, the timeout) |
| unsupported | its record names no FileDataID, or scale 0; it stands in group files that could not be read; or its M2 has no skin profile or no batch |
| notRequested | past a budget: instances or models (nothing fetched); the GPU budgets (fetched, but not built: the reason says so) |
| ended | the load ended before it was placed |

The doodads' state: "complete" (every doodad placed, no model partial), "partial", "none" (nothing to place), "failed"
(nothing placed), "ended", "notOffered". The assembly: "complete" only when the world models and the doodads are complete
(or have nothing) and doodads were offered; "partial" when anything was placed and something was not; "none" when nothing
was placed.

## 6. Tests and how to run them

Every GUI run is on a separate, never-shown Win32 desktop (the established harness: the viewer and the player it starts
run there; nothing reaches the owner's screens, focus or input). The final build: host `wowmodelviewer.exe` SHA-256
538ED9F3, player Assembly-CSharp MD5 3B202B69 (p17d).

| Suite | How | What it covers (Stage 07) | Result |
|---|---|---|---|
| Pure tests, synthetic | `dotnet run -c Release --project Tools/UnityRendererProject/Tests/Runner/WowParserTestRunner.csproj` | `DoodadTests`: MDDF de-duplication and the Tile frame with worked numbers (a1 90 is Rz(270), a0 90 tips z, the half turn), scale u16/1024 and scale 0, name-table and FileDataID-0 records; set selection (set 0 only, set 0 plus another, a set past the root's, ranges past MODD, an empty set 0, the diagnostic); MODD composition with a moved, turned, tilted and scaled world model (doodad(x) == wmo(p + s_q R(q) x)); R(q) against its transpose, component order, normalisation, determinant; unsupported MODD records; MODR membership and doodads of unreadable groups | **PASS**, 54 checks |
| Pure tests, real client bytes | the same, `WMV_WORLD_AUDIT_CORPUS` set | `DoodadCorpusTests`: section 3.1's MLDX replay on four clients, the plan against the census on every load, the set and MODR facts, the R(q) wall test with the independent probe's counts pinned, section 3.6's reference positions | **PASS**, 77 checks (with the Stage 01-06 suites: 0 failures, 1,932 checks) |
| Player self-test | `UnityRenderer.exe -wmvLifecycleTest -logFile <log>` | `TerrainDoodadTests`: section 9's doodad suite (below) beside the Stage 03-06 suites | **PASS**, 921 passed, 0 failed |
| Host in-process tests | `wowmodelviewer -terraintile 0 30 49 <out.json> -product wow` | the three roles' decisions (37 cases: a protocol-16 load refused every role, the order, once per id, the count caps, the byte budgets with reservations, the doodad read family beside the terrain's and the world models'), the accounting, 11 kind checks, the report's doodad fields read by their wire names | **PASS**, 218 passed, 0 failed, 1 skipped |
| Maps UI, four clients | `-mapsuitest`, section 7 | each client's tiles 30_47 and 31_48 and maps 34 and 429 entered through the viewer's own Maps panel | **PASS**, 4 x 56 checks |
| Lifecycle, keyboard, pictures, memory | `-mapsuitest`, sections 8-10 | | **PASS** (all runs) |
| Regression battery | section 11 | | **PASS**, but for one run that fails identically in Stage 06 (section 11) |

The player self-test's doodad suite (synthetic files only: a tile whose obj0 places M2s, a world model whose root has
three doodad sets, chunked M2s with SFID and TXID, their skins, a raw BLP2 they share): a tile's 4 MDDF records give 3
doodads, its world model's set 0 and Set_A 3 more (Set_B not drawn), all placed over 2 prototypes and one texture upload
used twice; the report's set entry; the instance at its Tile-frame position, Rz(270), scale 2, layer 30; the camera's
moment once and before any doodad request; each M2, skin and texture asked for once; terrainTileLoaded's assembly and
doodads; worldProgress's doodad phase; a protocol-16 load asks for no M2 ("notOffered", assembly "partial"); the model and
instance budgets with an unsupported record (every doodad counted once); a file that is not an M2; a prototype build that
throws; the doodads' timeout (one report, the models "timed out", their requests retired); the load ended while an M2 read
is out, between a decode and its result, between prototype builds, between placements, and by a newer load of another
epoch -- each reported once, every late answer dropped, nothing left; a WMO-only map's set-0 doodad; and a build that
throws inside the builder releasing what it made (`ReleaseOnFailure`). After each case every tally is back to 0.

## 7. The four-client matrix (PROVEN_RUNTIME)

Each client entered at Retail-frame target (-8515, 850) on tile 30_47 and (-8900, 400) on 31_48, and the WMO-only maps 34
and 429 at their footprints, through the viewer's own Maps panel (the final build; the client builds of the table in the
header). Every doodad count equals the census (section 4) and the plan's own counts in the pure tests.

| Client | Load | Doodads | MDDF | WMO-set | Placed | M2s built | Textures | Uploads / uses | Files MB (M2+skin / textures) | GPU MB (geometry / textures) | First visible ms | Builds s | Ready s | Assembly |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Retail | 30_47 | complete | 2451 | 3269 | 5720 of 5720 | 616 of 616 | 522 (0 missing) | 548 / 1096 | 16.3 / 56.4 | 9.2 / 283.0 | 175 | 0.4 | 17.7 | complete |
| Retail | 31_48 | complete | 650 | 2907 | 3557 of 3557 | 333 of 333 | 333 (0 missing) | 353 / 552 | 8.3 / 19.9 | 4.1 / 112.4 | 139 | 0.2 | 9.5 | complete |
| Retail | map 34 | complete | 0 | 748 | 748 of 748 | 24 of 24 | 29 (0 missing) | 28 / 31 | 0.3 / 1.9 | 0.2 / 9.8 | 155 | 0.0 | 0.8 | complete |
| Retail | map 429 | complete | 0 | 3543 | 3543 of 3543 | 186 of 186 | 160 (0 missing) | 165 / 313 | 2.6 / 5.9 | 1.4 / 33.0 | 137 | 0.1 | 5.0 | complete |
| Classic Era | 30_47 | partial | 0 | 6026 | 6026 of 6026 | 279 of 279 (1 partial) | 261 (0 missing) | 276 / 434 | 3.2 / 7.3 | 1.9 / 48.9 | 173 | 0.1 | 7.7 | partial |
| Classic Era | 31_48 | partial | 219 | 6062 | 6281 of 6281 | 298 of 298 (1 partial) | 277 (0 missing) | 293 / 463 | 3.3 / 7.6 | 1.9 / 50.2 | 140 | 0.1 | 8.3 | partial |
| Classic Era | map 34 | complete | 0 | 740 | 740 of 740 | 20 of 20 | 25 (0 missing) | 25 / 28 | 0.2 / 0.5 | 0.1 / 3.0 | 155 | 0.0 | 0.7 | complete |
| Classic Era | map 429 | partial | 0 | 3541 | 3541 of 3541 | 186 of 186 (2 partial) | 156 (0 missing) | 161 / 317 | 2.6 / 5.3 | 1.4 / 30.9 | 137 | 0.1 | 5.2 | partial |
| MoP Classic | 30_47 | complete | 2291 | 2888 | 5179 of 5179 | 414 of 414 | 382 (0 missing) | 409 / 727 | 10.7 / 23.8 | 5.3 / 135.7 | 174 | 0.2 | 11.9 | complete |
| MoP Classic | 31_48 | complete | 636 | 3091 | 3727 of 3727 | 326 of 326 | 316 (0 missing) | 340 / 542 | 7.4 / 14.0 | 3.5 / 86.4 | 105 | 0.2 | 9.0 | complete |
| MoP Classic | map 34 | complete | 0 | 740 | 740 of 740 | 20 of 20 | 24 (0 missing) | 24 / 27 | 0.2 / 0.5 | 0.1 / 3.5 | 169 | 0.0 | 0.6 | complete |
| MoP Classic | map 429 | complete | 0 | 3543 | 3543 of 3543 | 186 of 186 | 159 (0 missing) | 165 / 315 | 2.6 / 5.7 | 1.4 / 32.2 | 138 | 0.1 | 5.1 | complete |
| Classic Beta | 30_47 | partial | 290 | 6248 | 6538 of 6538 | 354 of 354 (1 partial) | 353 (1 missing) | 378 / 577 | 5.8 / 14.7 | 3.5 / 85.8 | 159 | 0.2 | 10.0 | partial |
| Classic Beta | 31_48 | partial | 230 | 6142 | 6372 of 6372 | 330 of 330 (1 partial) | 322 (1 missing) | 343 / 518 | 4.9 / 11.7 | 3.0 / 71.7 | 151 | 0.1 | 8.9 | partial |
| Classic Beta | map 34 | complete | 0 | 748 | 748 of 748 | 24 of 24 | 27 (0 missing) | 27 / 30 | 0.2 / 2.1 | 0.1 / 9.6 | 140 | 0.0 | 0.8 | complete |
| Classic Beta | map 429 | partial | 0 | 3543 | 3537 of 3543 | 185 of 186 (1 unsupported) | 163 (0 missing) | 169 / 321 | 2.9 / 7.2 | 1.6 / 38.2 | 156 | 0.1 | 5.0 | partial |

"First visible": from the world models' settle to the first doodad placed. "Ready": from the settle to the last placement.
The partial states are what the files say: Classic Era's M2 201660 has a replaceable texture slot (type 11), and 190104
(a rowboat) and 202305 (a herb node) name their textures by path (no TXID chunk); Classic Beta's texture 137237 is listed
by the client's storage but not downloaded ("not installed locally", quoted); Classic Beta's 202363 on map 429 is a model
of particle emitters alone (6 doodads, "unsupported"). No doodad failed on any client.

## 8. Lifecycle (PROVEN_RUNTIME)

The viewer's own window on the hidden desktop, the final build:

| Run | What | Result |
|---|---|---|
| Retail battery | map 34 (748 doodads) entered; Back to map (the world stays, 748); entered again; map 0 picked (0 left); 30_47 (5,720); 31_47; Clear (0 doodads, 0 world models, no terrain); 30_47 again; Models (0 left); Maps; 30_47; Buildings with WMO 108631 (0 left); map 34 again (748) | **PASS**, 68 checks |
| Client switch during the doodads | Retail 30_47 switched to Classic Era with 4,090 of 5,720 doodads placed: the load answered "cleared", its doodad requests retired and their answers dropped as late, 0 doodads, prototypes and textures left; Era's 30_47 then complete (6,026); map 34 switched to MoP Classic: 0 left | **PASS**, 30 checks |
| The window closed with an M2 read held | `holdclose:m2Model`, `holdclose:m2Skin` (Retail 30_47), `holdclose:m2Texture` (map 34): the read held in the host's worker, the window closed | **PASS**, each 8 checks and the shutdown check 9 of 9 |
| The window closed while the doodads are requesting, building, placing | `closeworld:doodadRequesting`, `doodadBuilding`, `doodadPlacing` (Retail), `doodadBuilding` (Classic Beta) | **PASS**, each 6 checks |
| The window closed while placing, Classic Era | `closeworld:doodadPlacing` | **PASS** (life2 run, the same build's player data) |

Clear, a model load, Buildings, a newer load and a client switch dispose the scene's doodads before its world models
and tiles; Back to map keeps the world on screen (Stage 06's behaviour). The self-test (section 6) ends a load at every
stage of the doodads deterministically.

## 9. Pictures (PROVEN_RUNTIME; what is drawn, not a claim of the game's look)

Taken by the harness through the viewer's own window (PrintWindow) on the hidden desktop, the final build. The doodads
are drawn by the Models renderer's materials under its preview key and fill, without shadow maps, static in their rest
pose; no WoW lighting, fog or sky. No claim of in-game appearance is made (no in-game reference of these places).

| # | Picture | What it shows |
|---|---|---|
| 01 | `S_before_01_outdoor_before_doodads.png` | Retail 30_47, Lion's Rest, camera (-8015, 800) on the ground, yaw 217: the Stage 06 player (protocol 16): terrain and world models, no doodad |
| 02 | `S_retail_02_outdoor_with_doodads.png` | the same camera with doodads: Elwynn trees, bushes, fences, the farm's lantern |
| 03 | `S_retail_03_ground_outdoor_trees_objects.png` | Retail 31_48, Old Town at street level: lamp posts, crates, a tree in its planter |
| 04 | `S_retail_04_one_m2_many_instances.png` | the Cathedral graveyard: hundreds of instances of a few gravestone M2s, each turned and scaled by its own record |
| 05 | `S_retail_05_dense_outdoor_with_wmos.png` | Stormwind from above: the city's world models with 5,720 doodads |
| 06 | `S_retail_06_stockade_interior.png` | map 34, the Stockade: its set-0 doodads (banners, torches, racks) |
| 07 | `S_retail_07_stockade_other_angle.png` | the Stockade from another room: iron maidens, barrels |
| 08 | `S_retail_08_interior_set_selection.png` | the Lion's Rest farm (WMO 892927): its placement names set 2, "Set_InteriorDoodads" -- the bunk bed, the desk, the lantern |
| 09a / 09b | `S9_default_09a_close_default_sets.png` / `S9_allsets_09b_close_all_sets_DIAGNOSTIC.png` | the farm from outside, the same camera: the default sets (0 + 2), and the `-wmvDoodadSets=all` diagnostic, which also draws "Set_ExteriorDoodads": here two planks leaning on the wall, a pile of blue shingles and a short fence section (the panel: 5,750 doodads over 623 models against the default's 5,720 over 616). The inactive set's doodads are absent from the default picture |
| 10 | `S_era_10_era_outdoor.png` | Classic Era, Old Town at the same camera as 03 |
| 11 | `S_mop_11_mop_outdoor.png` | MoP Classic, the same |
| 12 | `S_beta_12_beta_outdoor.png` | Classic Beta, the same |
| 13 | `S_retail_13_m2_materials_closeup.png` | the farm's table: bread, cheese, fruit, a mug, chairs -- M2 materials and textures close up |
| 14 | `S_retail_14_models_after_maps.png` | the Models view after leaving Maps: no world object left in it |
| 15 | `S_retail_15_buildings_after_maps.png` | Buildings after leaving Maps, WMO 108631 alone |
| 16 | `S_retail_16_partially_loaded.png` | Retail 30_47 during the doodads, the panel's progress line: "doodads: 2566 of 5720 placed (156 of 616 models built); placing" |

## 10. Performance and memory (PROVEN_RUNTIME)

The final build through the real window on the hidden desktop, nothing else running, with a sampler reading the player's
and the viewer's memory about once a second (process private bytes and working set; GPU dedicated memory per process).
The display's refresh is 60 Hz and the player is capped to it, so a steady frame is 16.67 ms.

**Loading** (the player's report of each load; the doodads' times from the world models' settle):

| Scene | Doodads | First visible | Reads (fetch) | Parse / decode (worker, summed) | Builds (summed / longest) | Placement (summed / longest frame) | Ready |
|---|---|---|---|---|---|---|---|
| Retail 30_47, 12 entries | 5,720 over 616 M2s, 522 textures (548 uploads), 16.3 MB of M2 and skin files, 56.4 MB of textures | 138-190 ms | 17.2-17.7 s | 0.34-0.41 s / 3.7-4.1 s | 345-382 ms / 5.8-19.0 ms | 37-54 ms / 1.9-4.1 ms | 17.2-17.8 s |
| Map 34, 12 entries | 748 over 24 M2s | 121-194 ms | 0.74-0.86 s | 12-48 ms / 78-110 ms | 11-27 ms / 1.4-15.1 ms | 3.8-6.4 ms / 2.0-3.2 ms | 0.75-0.88 s |
| Classic Era 30_47 | 6,026 over 279 M2s | 141 ms | 7.6 s | 0.10 s / 0.44 s | 99 ms / 3.7 ms | 38 ms / 4.0 ms | 7.6 s |

The terrain and the world models are on screen first (Stage 06's times, unchanged: Retail 30_47's world models settle about
5.4 s after Enter world); the camera is placed then, and doodads appear from 0.12-0.19 s later, model by model. The time
is in the reads: at most four doodad requests are out at once, each a storage read on the host's worker, a content-key
check, the transfer and the player's own check -- on Retail 30_47 1,754 files (616 models, 616 skins, 522 textures, each
once) in about 17 s. Parsing and decoding run on worker threads; the main thread's share is the prototype builds (at most
6 ms of a frame, at least one build: the longest single build 19 ms) and the placements (at most 4 ms of a frame: the
longest frame's placements 4.1 ms). 431 of Retail 30_47's 522 textures are decoded a second time for a model fetched after
their pixels went (their file bytes kept); the decoded pixels held peak at 9.4 MB.

**Frames** (the world view's last 600 frames, runtimeState): with Retail 30_47's 5,720 doodads on screen -- at street
level, above the city, in the overview -- median 16.67 ms, 95th percentile 16.67 ms, longest 16.9-17.6 ms: no frame
dropped. The 600 frames before each of the 12 reports (the doodads' second half) have a longest frame of 17.3-19.3 ms;
the same window in Stage 06 (its world models building) had 48-55 ms. On map 34 the window holds the world model's own
build frame: 53-82 ms, as Stage 06 (51-75 ms without doodads).

**Culling** (the last frame): Retail 30_47 at street level 2,779 of 5,720 doodads drawn (2,941 culled); above the city
4,110 (1,610 culled); the overview all 5,720; Classic Era 30_47 3,045 of 6,026; inside the Stockade 158 of 748.

**GPU memory of the doodads** (the player's tallies): Retail 30_47 9.2 MB of geometry and 283.0 MB of textures (548
uploads of 522 files: RGBA32 with mips; the uploads are not block-compressed, Stage 08's subject); Classic Era 30_47 1.9 MB
and 48.9 MB; map 34 0.2 MB and 9.8 MB. Within the 256 MiB and 1 GiB budgets.

**Process memory, 12 entry/Clear cycles** (the sampler; MB):

| Step | Player private | Player working set | Player GPU dedicated | Viewer private |
|---|---|---|---|---|
| Maps, before any world | 818 | 549 | 220 | 2,230 |
| Retail 30_47 entered (12 entries) | 1,573-1,908 | 987-1,080 | 285-782 | 2,250-2,251 |
| after each Clear (12) | 1,269-1,392 (1st 1,333, 12th 1,345) | 937-1,052 | 285-291 | 2,250-2,251 |
| Models after the 12th | 1,337 | 1,013 | 285 | 2,248 |
| Map 34 entered (12 entries) | 1,015-1,030 | 643-660 | 284-309 | 2,236-2,237 |
| after each Clear (12) | 981-1,003 (1st 990, 12th 991) | 639-660 | 284 | 2,236-2,237 |
| Retail 30_47 with the Stage 06 player (no doodads), 12 entries | 1,356-1,440 | 828-908 | 279-478 | 2,248-2,250 |
| after each Clear (12) | 1,167-1,260 (1st 1,167, 12th 1,238) | 814-885 | 279-285 | 2,245-2,250 |

After every Clear, Models, Buildings and client switch the player's doodad tallies are 0 (instances, prototypes, meshes,
materials, textures, GPU bytes), and so are Unity's own object counts back to where they started: in the performance run
GameObjects 7 -> 7,175 with 30_47's world -> 9 in Models, Texture2D 78 -> 1,007 -> 78, Mesh 1 -> 1,127 -> 1, Material
62 -> 1,720 -> 62. The process's private bytes after a Clear do not grow over 12 cycles (Retail: 1,333 MB after the first,
1,345 MB after the twelfth, between 1,269 and 1,392 in between; map 34: 990 and 991): what the managed heap and the
engine's allocators keep for reuse after the first heavy load, not a leak. The same 12 cycles with the Stage 06 player (no doodads) keep 1,167 MB after the first Clear and 1,238 MB after the
twelfth: the doodads raise the plateau the allocators keep by about 100-170 MB and add no growth of their own. The GPU dedicated reading of a loaded 30_47
moves between 285 and 782 MB with the driver's residency (the same scene, sampled at different times); after a Clear it
is 285 MB. The viewer's private bytes follow the client (2.23-2.26 GB for Retail, 1.2-1.75 GB for Classic Era) and do
not grow with world loads.

**GUI thread**: across the 12 Retail entries' 55,175 GUI rounds the longest was 104 ms; 10 rounds took over 50 ms. The
longest rounds of the whole campaign are the mode switches that predate this stage (Textures 650 ms, Buildings 360-392
ms; Stage 06: 625 and 412 ms). A world load's doodads run on the host's map asset worker and the player; the GUI thread
only answers and records.

**Camera responsiveness**: the keyboard run (section 8's sequence of Stage 06, now in the Stockade with its 748 doodads):
every one of the 14 segments moves the free camera as expected and stops when the key is released or the keyboard leaves
the view (79 checks, PASS).

## 11. Regressions and deployment

The Stage 06 battery, run on the final build (host and protocol-17 player) and compared file by file with Stage 06's
battery on the same client builds:

| Set | What | Result |
|---|---|---|
| Terrain geometry dumps | 6 tiles: Retail map 0 32_50 and 34_46, map 1051 31_29, map 2923 32_24; Classic Era 32_50; Classic Beta 32_48 | **PASS**, 6 of 6 byte-identical |
| Composite dumps | 11 loads on Retail, Classic Era and Classic Beta | **PASS**, 98 of 98 files identical |
| Models view | creature/bear/bear.m2 at animation time 0, 1600 px | **PASS**, byte-identical (MD5 52605ecc) |
| Buildings view | WMO 108631, 1600 px | **PASS**, byte-identical (MD5 d3db7749) |
| H battery | 12 runs: late answers, client switches with and without MoP Classic, the worst tile, lifecycles, Classic Era files not in storage, MDID 0, a missing texture, a protocol-12 player | **PASS** 11 of 12; `H_B_missingtex0` fails one check, identically in Stage 06's battery: it expects map 2736 tile 32_29's material "unavailable", and on today's client data the tile is textured |
| W battery | 6 runs: reads, the heaviest tile, a switch, Classic Beta in process, memory | **PASS**, 6 of 6 (71, 31, 67, 22, 132 and 323 checks) |
| Shutdown with a read held | four clients | **PASS**, 9 of 9 each |
| Map catalogue sequences | four clients | **PASS**, 27, 44, 44 and 27 checks |
| Host in-process tests | `-terraintile 0 30 49`, the final host | **PASS**, 218 passed, 0 failed, 1 skipped |

Stage 03-06 outputs are therefore unchanged on the same data; the Models view and Buildings draw exactly as before (the
builder's default path is untouched), and the protocol-17 player takes every older message unchanged.

**Deployment.** The owner's build (`build64\Source\wowmodelviewer\Release`) receives the tested host exe and player:

| File | Deployed (SHA-256) | Before |
|---|---|---|
| `wowmodelviewer.exe` (and its `.pdb`) | 538ED9F3 (the tested host) | 5C1347C7 (the Stage 06 integration build) |
| `tools\unity-renderer\UnityRenderer_Data` | Assembly-CSharp MD5 3B202B69 (player p17d); 0 data files differing from the tested player's | Assembly-CSharp MD5 37B96198 (Stage 06) |
| `wow.dll`, `core.dll` | not replaced: 0ED07B2C, 8472119A | the same |

`wow.dll` and `core.dll` are not replaced: the owner's are the Stage 06 integration build (local branch
`tmp/w06-deploy-integration` f16c5d72: Stage 06 plus the owner's bulky-belt tabard and .chr tabard-hang fixes, both still
uncommitted in their own worktrees), byte for byte the ones every Stage 07 run above used (wow.dll 0ED07B2C, core.dll
8472119A). Stage 07 changes neither; the two fixes' header changes (`WoWItem::clipsLowerTabard`, an inline function, and
`WoWModel::hasGeoset`, a non-virtual declaration) change no class layout, so the Stage 07 exe built without them is
binary-compatible with that wow.dll -- and it is that combination that was tested. Nothing of the fixes is merged into
`world/07-doodads`. The replaced files are backed up in `_backup_world07_2026-10-16_2041`.


## 12. Adversarial review

Three independent reviewers, read-only, each given the full diff, this document and the run logs, tried to disprove the
work on the specification's points 1-12 and 14; points 13, 15 and 16 are judged from the runtime evidence of sections
7-10. What they found, and what was done (every fix rebuilt and re-run: the pure tests, the self-test and the final
campaign of sections 6-11 all ran after the fixes):

| # | Point | Verdict | Finding and action |
|---|---|---|---|
| 1 | MDDF position | no defect | Formula, de-duplication and unsupported cases confirmed on the corpus (no repeated uniqueId, no scale 0). The missing scale-0 check was added anyway (a record of scale 0 is unsupported; test). |
| 2 | MDDF rotation and scale | no defect | M2 and WMO vertices share `ConvertPosition`'s model space, so U = M R M^T places both; the transform is composed in double and cast once relative to the floating origin (about 1e-4 yd at 1,000 yd). |
| 3 | Set selection | doc defects | The rule is implemented as described; three statements of 3.4 and 2.2 were wrong or overstated (the WMO-only maps' records name set 0; three roots name set 0 "DefaultGlobal"; the overlap evidence rests on two roots). Corrected. INFERRED stands. |
| 4 | WMO-local composition | no defect | Component order x, y, z, w confirmed (the identity is stored (0, 0, 0, 1) 424/311/460/460 times, never (1, 0, 0, 0)); 3.3's wording on quarter turns and the small-box filter corrected. |
| 5 | Group membership and visibility | defect, fixed | A world model built without some group files placed the doodads only those groups list, floating where no geometry was built. Now unsupported, said (`DoodadPlan.MarkMissingGroups`, test). The plan also no longer claims to count the doodads of world models that were not placed. |
| 6 | M2 rendering | one limitation, one fix | The rest state of global-sequence tracks took the Models view's clock: it is now held at 0 for a prototype's build. Transparent queue ranks are per model (the Models view's order inside one model); across instances Unity sorts transparent renderers by queue before distance -- section 13. |
| 7 | Shared ownership | budget defect, fixed | No ownership defect. The GPU texture budget counted one upload per file while the cache uploads per (file, alpha, wrap): it is now checked against the cache's actual bytes. |
| 8 | Client isolation | no defect | Answers applied only to the pending request of the live load; every exit retires its requests; a protocol-16 player is served no doodad file. A switch mid-load retired 4 doodad requests, their answers dropped as late. |
| 9 | Worker and IPC lifetime | reporting defect, fixed | The host counted the capped arrays, not the player's totals, so its "more listed than kept" line could never fire: it now reads `setsTotal` and `listTotal`. |
| 10 | Limits | defects, fixed or documented | The GPU texture budget (point 7). The GPU budgets are checked when a model is about to be built, after its files were fetched: such a model is "notRequested" with the reason "fetched, but not built". Two different M2s naming one skin file would be refused the second time by the host's once-per-id rule (not seen in the corpus: section 13). |
| 11 | Cancellation | no defect | Every exit -- Clear, supersede, model load, client switch, shutdown, the doodads' timeout -- ends with one report, every request retired, every queue cleared, nothing left (self-test and host runs). |
| 12 | Cleanup | test gap, closed | `ReleaseOnFailure` was not exercised by a test: the self-test now drives a real throwing build (a batch naming a missing submesh after the first batch made its material and texture) and checks the release, the model count and the cache. Two narrow leak paths (a throw after `Build` returned, or after an instance's object was created) now clean up. |
| 14 | Models and Buildings preserved | no defect | With no new option set every changed line of `WmvModelBuilder` is a no-op; no other caller sets them; `WmvWmoBuilder` is untouched. The pictures are compared byte for byte in section 11. |

## 13. Known limitations

- **Static doodads in their rest pose.** A doodad is drawn as its M2's file stores it (the builder's bind pose): no
  animation, no particle or ribbon emitter, no material animation per frame. A model of emitters alone (Classic Beta
  202363, Dire Maul: 6 doodads) is "unsupported". Animated doodads and their effects are milestone 11.
- **Geosets.** A doodad draws its geoset-0 submeshes (the builder's default without a variant). Which variant the game
  shows for a doodad with others is UNKNOWN.
- **Textures named by path.** Two Classic Era M2s (190104, a rowboat; 202305, a herb node) have no TXID chunk and name
  their textures by path; map files are asked for by FileDataID only, so they are drawn without them ("partial", named).
- **Transparent order across instances.** Each blended batch of a model has its own render queue (the Models view's pass
  order); Unity sorts transparent renderers by queue before distance, so two blended doodads overlapping on screen can
  blend in queue order rather than back to front. Alpha-keyed foliage (most doodads) is not affected.
- **Doodad sets** (3.4): INFERRED.
- **Visibility**: per-instance frustum culling only; no portal, BSP, indoor/outdoor or MODR visibility.
- **Lighting**: the Models renderer's preview light and fill, no WoW lighting, MOCV, fog or sky.
- **Skins shared between M2s**: the host serves an id once per load; two M2s naming the same skin file would leave the
  second without it (not seen in the corpus).
- **Missing client files**: a texture the client lists but has not downloaded (Classic Beta 137237) is drawn as missing,
  its model "partial", the host's answer quoted.

## 14. Implications for Stage 08 (streaming)

- **Reads dominate.** A doodad-heavy tile spends 9-17 s in M2, skin and texture reads (section 10); with 3 x 3 tiles the
  doodads need the transport-wide send cap and a prioritised, cancellable scheduler -- doodads near the camera first.
- **Sharing across tiles.** Prototypes and the texture cache belong to one scene today; streaming should share them
  between the tiles of the scene (an M2 placed on several tiles built once) with reference counts, disposed when the last
  tile using them is evicted.
- **Budgets per scene, not per load.** The instance, model and GPU budgets are per load; they become residency budgets.
- **Group visibility.** MODR is decoded and tested; a portal or group visibility pass can use it to skip interior doodads.
- **Transparent sorting** (section 13) matters more with many tiles' doodads on screen.

## 15. Acceptance gate

| # | Criterion | Evidence | Result |
|---|---|---|---|
| 1 | The Maps viewer works | the matrix, lifecycle and picture runs drive it on four clients (sections 7-9) | **PASS** |
| 2 | MDDF doodads from real data | 2,451 / 650 / 2,291 / 636 / 290 / 230 / 219 MDDF doodads placed on the four clients' tiles (section 7) | **PASS** |
| 3 | Correct positions | every MDDF doodad's placed box within 0.5 yd of the client's own MLDX box on four clients (3.1); independent reference positions (3.6) | **PASS** (PROVEN_BYTES) |
| 4 | Rotations and scales verified | MDDF: the MLDX replay, tilted doodads included, the half turn decided (3.1); MODD: R(q) by the wall test on four clients, the independent probe's counts reproduced (3.3) | **PASS** (MDDF PROVEN_BYTES; MODD statistical) |
| 5 | M2 geometry and materials correct | the Models builder reused (5.2), its default path byte-identical (section 11); pictures 02-13 | **PASS** for what the Models view draws at rest; no in-game comparison |
| 6 | WMO doodad sets read and selected | MODS/MODI/MODD on four clients; set 0 plus the placement's set (3.4); the report's sets | **PASS** (the rule INFERRED) |
| 7 | Interior decorations positioned | the Stockade and the Lion's Rest farm (pictures 06-08, 13); the composition (3.2) | **PASS** |
| 8 | Inactive sets excluded | Set_B not drawn in the self-test; the farm's Set_ExteriorDoodads absent by default and drawn by the diagnostic (picture 09) | **PASS** |
| 9 | Shared reuse | one prototype per M2, one upload per texture variant: 5,720 instances over 616 prototypes and 548 uploads (Retail 30_47) | **PASS** |
| 10 | Same scene and origin | instances under the scene root, placed relative to its floating origin (5.6); the global map's doodads in the Global frame (self-test, map 34/429) | **PASS** |
| 11 | Camera unaffected | the camera placed at the world models' settle, before any doodad request (self-test; the log order); doodads are not floors | **PASS** |
| 12 | Bounded and responsive | the budgets (section 4); per-frame time budgets; frames during the doodads' second half at most 19.3 ms, steady 16.67 ms (section 10) | **PASS** |
| 13 | Failures accounted | failed, unsupported, notRequested and ended counted per doodad and per model, the list of models not built, the reasons quoted (sections 5.9, 7) | **PASS** |
| 14 | Clear disposes | 0 doodads, prototypes, textures and Unity objects back to their start after every Clear (sections 8, 10) | **PASS** |
| 15 | Client switch leaves nothing stale | a switch mid-doodads: "cleared", requests retired, late answers dropped, 0 left (section 8) | **PASS** |
| 16 | Four clients validated | section 7: 16 loads, every count equal to the census | **PASS** |
| 17 | Regressions correct | section 11 | **PASS** (one pre-existing failure, identical in Stage 06: section 11) |
| 18 | Screenshots of populated outdoor and interior | section 9 | **PASS** |
| 19 | No leaks on repeated loads | 12 entry/Clear cycles of Retail 30_47 and of map 34: tallies 0, private bytes flat (section 10) | **PASS** |
| 20 | Safe shutdown during doodad loading | the window closed with an M2 model, skin and texture read held, and while requesting, building and placing (section 8) | **PASS** |

Grades used above: PROVEN_BYTES (the client's own files decide), PROVEN_RUNTIME (observed in the running viewer), INFERRED
(the doodad-set rule; the MODD composition), UNKNOWN (the game's own look of these scenes: no in-game reference).
