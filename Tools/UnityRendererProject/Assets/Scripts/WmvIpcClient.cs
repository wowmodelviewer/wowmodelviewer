// WmvIpcClient.cs
//
// IPC client of the WMV Unity viewport player. WMV is the IPC SERVER: it starts a
// localhost TCP listener before launching the player and passes the port on the player
// command line ("-wmvPort <n>"); the player connects back, announces itself and then asks
// WMV for whatever it needs. Transport: newline-delimited JSON (one object per line),
// protocol version 19 (4 added world models: loadWoWModel "kind", mapObjectLoaded and runtimeState;
// 5 added mounted characters: characterScene "mount", its answer's mount fields, runtimeState's
// mountFileDataID, modelAnimation "role" and "load", and modelAnimationState "load", "hasRider" and
// "rider"; 6 added captureScreenshot and its answer screenshotSaved; 7 added viewportBackground and
// runtimeState's background fields; 8 added loadWoWModel "keepView" and runtimeState's keptViews; 9 added the asset
// cache: loadWoWModel and prefetchAssets "assetEpoch", assetResponse "cacheable", prefetchAssets, and runtimeState's
// assetCache fields -- see WmvAssetCache; 10 added binary payloads: an assetResponse's file and a characterImage's
// pixels follow their line as raw bytes, "payloadBytes" long -- see WmvStreamReader; 11 added characterScene
// "attachmentsOnly" and "load" for the equipment of an ordinary NPC model; 12 added terrain tiles: loadTerrainTile,
// clearTerrain, getMapAsset and its answer's role, load, assetEpoch, state, detail, readMs and md5, terrainTileLoaded,
// and runtimeState's terrain fields -- see WmvTerrainSession; 13 added terrain textures: loadTerrainTile's
// maxTextureBytes, maxTextureBytesPerLoad and maxTexturesPerLoad, getMapAsset's roles "tex0Adt" and "texture",
// terrainTileLoaded's requestsRetired, retiredRequestIds and material, and runtimeState's terrainTextures,
// terrainTextureBytes and terrainAlphaArrays; 14 added the Maps viewer: inspectMapTiles / mapTilesInspected and
// setTerrainView / terrainViewApplied; 15 added the minimap ids and the global WMO in the tile table, the camera on
// loadTerrainTile, and setWorldCamera / worldCameraApplied; 16 added world models in the world -- loadWorld (a tile and
// its MODF world models, or a WMO-only map's global world model), getMapAsset's roles "obj0Adt", "wmoRoot", "wmoGroup"
// and "wmoTexture", worldShown, worldProgress, terrainTileLoaded's world fields, the global record in mapTilesInspected,
// inspectWorldModel / worldModelInspected, and runtimeState's world fields -- see WmvTerrainSession and WmvWorldModels;
// 17 added doodads -- loadWorld's doodad budgets, getMapAsset's roles "m2Model", "m2Skin" and "m2Texture",
// worldProgress's doodad fields, terrainTileLoaded's world assembly and doodads, and runtimeState's doodad fields -- see
// WmvTerrainSessionDoodads and WmvWorldDoodads; 18 added streaming -- loadWorld's "streaming", streamTile /
// streamTileAnswer, streamState, testFlight / testFlightDone and runtimeState's "light" and stream fields -- see
// WmvTerrainSessionStream; 19 added liquids -- loadTerrainTile's and loadWorld's "liquids" (with its optional "lights",
// the zone lights of the water colours), getMapAsset's role "liquidTexture", terrainTileLoaded's liquids, and
// streamState's and runtimeState's liquid fields -- see WmvTerrainSessionLiquids, WmvLiquids and WmvLiquidLight).
//
// The player is WMV's new renderer foundation and renders directly from WoW data: it
// requests raw assets and metadata from WMV -- which owns the app UI, the active
// client/profile, CASC/MPQ access, DB/metadata and the runtime commands -- over this
// channel. The player never reads game archives itself and no exported files are involved.
//
// player -> WMV
//   unityReady           { protocolVersion }
//   getAsset             { requestId, path }
//   getAssetByFileDataID { requestId, fileDataID }
//   getModelTextures     { requestId, fileDataID }
//   modelGeosetsApplied  { fileDataID, revision, status:"applied"|"pending"|"rejected", reason,
//                          submeshVisible:[0|1,...], triangles, animTimeMs }
//     the answer to modelGeosets (and, with revision 0, a report after a load or a skin push
//     applied the host's per-submesh state): what the viewport now draws, so the host's
//     checkboxes can follow the renderer rather than assume it
//   characterSceneApplied { fileDataID, load, revision,
//                           status:"applied"|"pending"|"superseded"|"rejected", reason,
//                           merged, attachments, missing:[key,...], ms,
//                           mountKey, mountStatus:"applied"|"failed"|"none", mountReason }
//     the answer to characterScene: the character on screen now wears that scene (less any part
//     named in missing), or why not. load is the serial of the loadWoWModel the scene belonged to
//     (0 when it matched no load). "superseded" means a newer scene or a new load replaced it before
//     it was applied, which is not a failure; "rejected" is a real refusal. mountKey is the key of
//     the scene's mount ("" for none); mountStatus says whether that mount was put under the
//     character ("applied"), could not be built ("failed", with mountReason -- the character itself
//     is still applied) or was not applied at all ("none": the scene has no mount, or the scene was
//     not applied)
//   mapObjectLoaded       { fileDataID, load, status:"built"|"failed"|"superseded", reason,
//                           groups, groupFilesRequested, groupFilesMissing, batches, submeshes,
//                           renderers, materials, provisionalMaterials, unresolvedMaterials,
//                           blendedMaterials, texturesReferenced, texturesDecoded, texturesMissing,
//                           vertices, triangles, boundsMin:[x,y,z], boundsMax:[x,y,z],
//                           timings:{ rootMs, groupsMs, texturesMs, buildMs, totalMs },
//                           liveMapObjects, liveModels }
//     sent once per world-model load outcome. provisionalMaterials counts the drawn materials
//     that are drawn provisionally (the archived baseline or a labelled fallback), unresolvedMaterials those with any
//     open question (resolved-partial or unresolved; see Wow.WmoMaterialSemantics), blendedMaterials those with a
//     non-zero MOMT blend, texturesDecoded the files some drawn material's plan samples.
//     Bounds are Unity space; liveMapObjects / liveModels
//     count the runtimes the player holds once the outcome was adopted, so a lifecycle test can
//     prove a switch left nothing behind
//   runtimeState          { query, liveMapObjects, liveModels, modelFileDataID, mapObjectFileDataID, loading,
//                           mountFileDataID, mountKey, liveMounts, mountsBuilt, mountSeat, mountSeatBone,
//                           modelSequence, mountSequence, mountEmitters, mountRibbons, mountParticles,
//                           bodyRebinds, viewFramings, keptViews, backgroundR, backgroundG, backgroundB,
//                           assetCacheEntries, assetCacheBytes, assetCacheHits, assetCacheJoins, assetCacheEvictions,
//                           assetEpoch }
//     the answer to runtimeState: what the player holds right now -- the runtimes alive, the
//     fileDataID of the model and of the world model on screen (0 for none), whether a load of
//     either kind is in flight, and the mount the model on screen rides (0 for none) with its key,
//     the mount runtimes alive and built so far, how the model hangs from it (RuntimeReport), what
//     each animator plays, the mount's emitters and live particles, how often the character's
//     body textures were bound again, how often the view was fitted to what is on screen, and the
//     viewport background as displayed (protocol 7), and the models put on screen keeping the view (keptViews,
//     protocol 8). A test's question, answered from the main thread in
//     message order
//   screenshotSaved       { request, ok, error, path, width, height, bytes, renderMs, encodeMs, writeMs, totalMs }
//     the answer to captureScreenshot (protocol 6): the PNG was written to path (ok, with its size in bytes and
//     how long the off-screen render and readback, the PNG encode, the file write and the whole capture took,
//     in milliseconds), or why not (error). request echoes the question's number
//   getMapAsset           { requestId, load, assetEpoch, role:"wdt"|"rootAdt", fileDataID }       (protocol 12)
//     one map file for the terrain load named by load, under the client epoch it was asked under: the map's WDT
//     (the fileDataID loadTerrainTile named), then the root ADT the WDT's MAID names for the tile. Answered by an
//     assetResponse with the same requestId (never from the asset cache, never cacheable). Its requestId is
//     "map" + n, a namespace of its own (every other request's is "u" + n): an answer with such an id, or with a
//     role, goes to OnMapAssetResponse -- the terrain session -- and never to the asset cache or OnAssetResponse
//   getMapAsset           { ..., role:"tex0Adt"|"texture", fileDataID }                          (protocol 13)
//     the tile's tex0 ADT (MAID word 3), asked beside the root once the WDT is in, and the BLP textures its MDID names
//     for the chunks drawn textured -- on an MPHD 0x80 map also the height textures its MHID names for them -- each
//     distinct id once (both roles ask for role "texture"); at most WmvTerrainSession.MaxOutstanding requests of a load
//     are out at once (a texture counts until its upload data are ready). Asked only when loadTerrainTile states a
//     maxTextureBytes (a protocol-12 host states none)
//   getMapAsset           { ..., role:"obj0Adt"|"wmoRoot"|"wmoGroup"|"wmoTexture", fileDataID }  (protocol 16)
//     a world load's files: the tile's obj0 ADT (MAID word 1, its MODF), each world model's root, its LOD0 group files
//     and the textures its drawn materials sample; at most WmvTerrainSession.MaxWorldOutstanding of them out at once,
//     beside the tile's own MaxOutstanding
//   getMapAsset           { ..., role:"m2Model"|"m2Skin"|"m2Texture", fileDataID }              (protocol 17)
//     a world load's doodad files: each unique M2 its doodads name, the M2's first skin profile (SFID[0]) and the
//     textures its batches bind; at most WmvTerrainSession.MaxDoodadOutstanding of them out at once, asked only once the
//     world models are settled and only when loadWorld states the doodad budgets
//   worldShown            { load, mapId, kind:"tile"|"globalWmo", tileX, tileY, placements, models, shownMs,
//                           originWorld:[X,Y,Z] }                                                 (protocol 16)
//   worldProgress         { load, phase, placements, models, modelsRequested, modelsParsed, modelsBuilt, modelsFailed,
//                           modelsUnsupported, modelsNotRequested, placed, placementsFailed, filesAsked, filesAnswered,
//                           bytesReceived, elapsedMs }                                           (protocol 16)
//   worldProgress (protocol 17 adds) { doodadPhase:"none"|"waiting"|"requesting"|"building"|"placing"|"complete"|
//                           "partial"|"failed"|"ended"|"notOffered", doodadInstances, doodadPlaced, doodadModels,
//                           doodadModelsBuilt, doodadModelsFailed, doodadFilesAsked, doodadFilesAnswered, doodadBytes }
//     -1 where doodads were not offered
//   worldModelInspected   { inspection, status, reason, mapId, wmoFileDataID, assetEpoch, wmoBytes, md5, groups,
//                           interiorGroups, parseMs, rootBox:[6], extents:[6], groupBoxes (base64 floats),
//                           groupFlags (base64 u32) }                                            (protocol 16)
//   terrainTileLoaded     { load, status:"built"|"failed"|"superseded"|"cleared", reason, hostState, hostDetail,
//                           mapId, tileX, tileY, wdtFileDataID, rootFileDataID, wdtBytes, rootBytes, wdtMd5, rootMd5,
//                           chunks, chunksBuilt, chunksSkipped, chunksUnsupported, vertices, triangles, holeCells,
//                           boundsMin:[x,y,z], boundsMax:[x,y,z], rootOffset:[x,y,z], originWorld:[X,Y,Z],
//                           timings:{ wdtRoundTripMs, wdtParseMs, rootRoundTripMs, parseMs, buildMs, uploadMs,
//                                     firstFrameMs, totalMs },
//                           meshBytes, liveTerrainTiles, liveModels, liveMapObjects, meshObjects, materialObjects,
//                           terrainGameObjects, managedHeapBytes, nativeAllocatedBytes }                (protocol 12)
//     exactly one per loadTerrainTile (WmvTerrainSession): "built" once the frame after the one the tile was put on
//     screen in has been presented too, "failed" with the reason (hostState and hostDetail when a host answer
//     refused a file), "superseded" by a newer load (terrain or model), "cleared" by clearTerrain. Bounds are Unity
//     space relative to the tile root (absent without a triangle), originWorld the world point (game frame, yards)
//     the root stands for; a number the load did not reach is left out. The object counts and memory are sampled
//     when the report is sent
//   terrainTileLoaded (protocol 13 adds) { requestsRetired, retiredRequestIds:[...],
//                           material:{ state:"textured"|"partial"|"unavailable"|"unsupported", reason, tex0FileDataID,
//                                      tex0Bytes, tex0Md5, alphaFormat, chunkStates:{ counters },
//                                      textures:[{ fileDataID, role:"diffuse"|"height", state, detail, bytes, md5,
//                                                  width, height, format, mips, uploadMs }],
//                                      uniqueTextures, layerReferences, duplicateReferences, textureSourceBytes,
//                                      textureGpuBytes, alphaGpuBytes, materialVertexBytes, materials,
//                                      timings:{ tex0RoundTripMs, tex0ParseMs, textureFetchMs, textureUploadMs,
//                                                alphaBuildMs, materialBuildMs } } }
//     requestsRetired: how many of the load's requests were still out when it ended (each retired, at most
//     WmvTerrainSession.MaxOutstanding), their ids beside it. material: sent with every "built" report, and with any
//     other once the load had built it -- its chunk states settled with the texture failures answered so far (a texture
//     still "requested" or "queued" is no failure yet, and the reason then says how many are unanswered); a texture's
//     state is the host's, quoted, or the player's own ("no texture id", "not requested", "md5 mismatch", "decode
//     failed", "transport failure"); its role says whether a layer uses it as its diffuse texture (MDID) or its height
//     texture (MHID, MPHD 0x80 maps), one entry per role an id has
//   runtimeState (protocol 12 adds) { liveTerrainTiles, terrainLoad, terrainLoading, terrainView, cameraMask,
//                                     meshObjects, materialObjects, terrainMeshes, terrainMaterials,
//                                     terrainGameObjects, terrainPendingRequests, terrainRetiredRequests,
//                                     terrainRetiredTotal, terrainLastRetiredRequest, mapAnswersRouted,
//                                     mapAnswersDropped, mapAnswersLate, mapAnswersUnowned, terrainTimeoutMs }
//     the terrain tiles alive, the load of the tile on screen (0 for none), a terrain load in flight (not folded
//     into loading: the host's model await does not wait on terrain), the terrain view on, the main camera's culling
//     mask, the Unity Mesh and Material objects alive, and what the terrain tiles themselves hold: their Meshes,
//     Materials and GameObjects (WmvRuntimeTerrainTile's tallies -- a tile still being built included, the scene root
//     never, 0 with no tile, taken off the moment a tile is released rather than when Unity destroys its objects);
//     then the session's request bookkeeping: getMapAsset requests waiting for their answer (0 or 1), the ended
//     loads' unanswered requests it remembers (at most 32), the requests retired in all (one per load that ended
//     with its request unanswered; never capped) and the id of the last ("" for none), the map answers routed to it
//     and those it dropped (late plus unowned, then each apart: late = to a request of an ended load it still
//     remembers, unowned = any other), and the load timeout in effect in milliseconds (-wmvTerrainTimeoutMs, 30000
//     by default). Protocol 13 changes the bounds (requests waiting at most WmvTerrainSession.MaxOutstanding, ended
//     loads' requests remembered at most RetiredCapacity, 128) and adds terrainTextures (the layer textures the terrain
//     tiles hold), terrainTextureBytes (their GPU size, estimated from the formats and mips uploaded) and
//     terrainAlphaArrays (the tiles' alpha arrays)
//   getMapAsset           { ..., role:"liquidTexture", fileDataID }                               (protocol 19)
//     a frame of a liquid type's animation (texture slot 0, as the catalogue of loadTerrainTile's / loadWorld's "liquids"
//     lists it), for the liquid types the load's tile and world models draw; each id at most once per load, at most
//     WmvTerrainSession.MaxLiquidOutstanding of them out at once, within the offer's maxTexturesPerLoad ids and
//     maxTextureBytesPerLoad bytes; never asked when the load carries no "liquids"
//   terrainTileLoaded (protocol 19 adds) { liquids:{ offered, catalog:"ok"|"unavailable"|"absent", instances, layers,
//                           chunks, quads, vertices, meshes, types:[ids], texturesRequested, texturesLoaded, texturesFailed,
//                           missingTypes, formatBySize, formatDisagreements, malformed, wmoGroups, wmoQuads,
//                           geometryBytes, textureBytes } }
//     the load's liquids (WmvTerrainSessionLiquids): offered false and catalog "absent" when the load carried no
//     "liquids" (nothing drawn); the tile's MH2O -- instances (every layer), layers (the most in one chunk), chunks with
//     liquid, the quads and vertices of its meshes --, the Unity meshes made (the tile's and its world models'), every
//     liquid type id met (tile and world models), the frames this load asked for, got and could not get (refused, failed,
//     or not asked: past a budget), the types the catalogue lacks (not drawn), the MH2O instances whose format came from
//     their vertex block's size or whose rule and size disagree, the MH2O instances and MLIQ chunks not drawn as
//     malformed, the world-model groups with liquid and their quads, and the GPU bytes of the liquid geometry and frames
//   streamState (protocol 19 adds) { liquidTiles, liquidTypes, liquidTextures, liquidBytes }
//     the resident tiles that draw liquid, the scene's liquid types (pooled included), their frame arrays, and the GPU
//     bytes of those arrays and of the resident tiles' liquid geometry
//   runtimeState (protocol 19 adds) { liquidObjects, liquidMeshes, liquidMaterials, liquidArrays, liquidGpuBytes }
//     the liquid objects and meshes alive (tiles' and world-model prototypes'), the liquid materials and frame arrays
//     alive, and their GPU bytes (frames and geometry)
//
// WMV -> player
//   loadWoWModel  { path, fileDataID, client, character, load, kind, keepView }
//     character: a playable character, dressed by the characterScene that follows
//     load: the host's serial for this load (> 0), echoed in every characterSceneApplied about it
//     kind: "m2" (also when absent) or "wmo" -- a world model: path/fileDataID name the ROOT file
//     keepView (protocol 8, sent only when true): the character replaces the one on screen without moving the view
//     (its other model generation); a load before it that is not on screen yet makes it frame as any load
//   runtimeState  { query }                                                        (protocol 4)
//     asks for a runtimeState answer carrying the same query number
//   captureScreenshot { request, path, width, height }                             (protocol 6)
//     render what the viewport shows once more, off screen, at width x height with a transparent
//     background, and write it as a PNG to path (absolute, chosen and confirmed by the host's Save As);
//     answered by screenshotSaved with the same request number (see WmvScreenshot.cs)
//   viewportBackground { r, g, b }                                                 (protocol 7)
//     the Models viewport's background, opaque, as the sRGB bytes it is to display as (0..255 each; a
//     line with a channel outside that is refused). The player keeps it until the next one, a model load
//     does not reset it; the host sends it at every unityReady, and also passes it on the command line
//     ("-wmvBackground RRGGBB") so the first frame already shows it (WmvMain.ApplyViewportBackground)
//   assetResponse { requestId, ok, path, fileDataID, byteLength, encoding:"binary", payloadBytes, cacheable }
//                 <payloadBytes raw bytes><newline>                                (protocol 10)
//   assetResponse { requestId, ok, path, fileDataID, byteLength, sha1, encoding:"base64", data, cacheable }
//                                                                         (to a player before protocol 10)
//   assetResponse { requestId, ok:false, error }
//     cacheable (protocol 9, sent only when true): a file of the client's own storage, the same for as long as the
//     client is -- kept by the asset cache under the epoch of the request that asked for it. An answer the cache
//     gives is handed out in Update after the host's messages, within a time budget a frame
//   prefetchAssets { assetEpoch, fileDataIDs:[...] }                              (protocol 9)
//     the models the host expects next (the other model generation of the character on screen): fetched into the
//     asset cache, each with its skin and skeleton files, and kept with what the model on screen uses
//   assetResponse { requestId, ok, fileDataID, role, load, assetEpoch, state, detail, readMs, md5, byteLength,
//                   encoding:"binary", payloadBytes } <payloadBytes raw bytes><newline>  (protocol 12, to getMapAsset)
//     the host's decision and read: state is the catalogue's availability ("available", "not in client storage",
//     "not installed locally", "unreadable", "unsupported", "not checked") or "stale" / "refused"; ok only for
//     "available", which carries md5 (hex of the bytes served); every other state carries error = detail
//   loadTerrainTile { load, assetEpoch, mapId, mapDirectory, product, build, wdtFileDataID, wdtResolvedBy, wdtPath,
//                     tileX, tileY, maxAssetBytes }                                  (protocol 12)
//     draw one terrain tile of the active client's map: its WDT (by FileDataID, resolved by "fileDataID" or "path"),
//     then the root ADT its MAID names for (tileX, tileY), asked for by getMapAsset under load and assetEpoch. The
//     latest command wins: a model load in flight is dropped, the model on screen stays (hidden) until a loadWoWModel
//     leaves the terrain view (WmvMainTerrain)
//   loadTerrainTile (protocol 13 adds) { maxTextureBytes, maxTextureBytesPerLoad, maxTexturesPerLoad }
//     the texture limits: one texture file, the texture bytes the host reads for one load, the texture ids one load
//     may ask for. Absent (0) from a host before protocol 13: the player then asks for no tex0 and no texture, and
//     the tile's material is "unavailable" (the geometry is built as before)
//   clearTerrain  { reason }                                                         (protocol 12)
//     a terrain load in flight is answered "cleared"; a tile on screen is taken away, without an answer
//   inspectMapTiles { inspection, mapId, wdtFileDataID, assetEpoch, md5, encoding:"binary", payloadBytes }
//                   <payloadBytes raw WDT bytes><newline>                          (protocol 14, the Maps viewer)
//     what a map's WDT says of its 64 x 64 tiles, read by the verified parser (Wow/World/WdtParser.cs): answered by
//     mapTilesInspected with the same inspection number (WmvMapTiles.cs). The host read the bytes and checked them
//     against their content key; md5 is that key, checked again here before anything is parsed
//   setTerrainView { view }                                                         (protocol 14)
//     the view every terrain tile is drawn in from now on (WmvTerrainDebug.SetRuntimeView: one of its names but
//     texturedplainweights, or "auto"); the tile on screen is built again in it from what the player holds, nothing
//     asked of the host (WmvTerrainSession.RebuildShown), the camera left where it is; answered by terrainViewApplied
//   loadWorld (protocol 17 adds) { doodads:true, maxM2FileBytes, maxM2BytesPerLoad, maxM2TextureBytes,
//               maxM2TextureBytesPerLoad, maxM2ModelsPerLoad, maxM2TexturesPerLoad, maxDoodadInstancesPerLoad }
//     the doodad budgets (docs/world-viewer/07-doodads.md): sent only to a player that announced 17. Without them the
//     world loads as in protocol 16 and its report's doodads say "notOffered"
//   loadWorld { what loadTerrainTile carries, worldModels:true, globalWmo, maxWmoFileBytes, maxWmoBytesPerLoad,
//               maxWmoTextureBytes, maxWmoTextureBytesPerLoad, maxWmoTexturesPerLoad, maxWmoModelsPerLoad,
//               maxWmoGroupsPerLoad, maxWmoPlacementsPerLoad }                     (protocol 16, world models)
//     a world: the tile and the world models its obj0 ADT places (MODF), or -- globalWmo -- a WMO-only map's one world
//     model, no tile (docs/world-viewer/06-world-models-in-world.md). The player asks getMapAsset "obj0Adt" (once,
//     after the WDT), "wmoRoot", "wmoGroup" and "wmoTexture" within the budgets; answers worldShown when the scene first
//     has something on screen, worldProgress as its world models come, and exactly one terrainTileLoaded with the
//     world's outcome when every placement is settled
//   loadTerrainTile / loadWorld (protocol 19 adds) { liquids:{ maxTexturesPerLoad, maxTextureBytes, maxTextureBytesPerLoad,
//                     catalog:{ product, build, state:"ok"|"unavailable", detail, types:[{ id, name, kind, material,
//                     materialFlags, lvf, minimapColor, float0, float1, frames:[...], framesWanted, bump }] } } }
//     the load offers liquids (docs/world-viewer/09-liquids.md 1.6): the liquid frames' budgets and the host's catalogue of
//     the client's liquid types (LiquidOffer; the player reads no table). Sent only to a player that announced 19 and only
//     for a load that offers them; without it (an older host) no liquid is drawn and the report says "offered": false. A
//     streaming session's tiles take the entry's. "bump" (optional, additive): the type's ripple map's FileDataID (0 or
//     absent: none), fetched like the frames (role "liquidTexture") for the High water look
//   liquids.lights (optional, inside "liquids") { state:"ok"|"unavailable", detail, product, build, map, time, slot,
//                     defaultLight, defaultFrom, zones:[{ zone, light, zmin, zmax, conditional, poly:[x0,y0,...] }],
//                     points:[{ light, x, y, z, start, end }], values:[{ light, lightParams, riverClose:[r,g,b], riverFar,
//                     oceanClose, oceanFar, waterAlpha:[s,d], oceanAlpha:[s,d], sky:[r,g,b] }] }
//     the zone lights of the load's map at a fixed noon, clear-weather slot 0 (Wow/World/LiquidLights.cs): the water and
//     ocean colours and alphas follow the light at the camera (WmvLiquidLight). Absent (an older host) or not "ok": the
//     liquids keep their type colours. An additive key of protocol 19: a player without it ignores it. "sky" (optional,
//     additive): the light's sky colour at the same noon, which the High look's reflection misses take
//   inspectWorldModel { inspection, mapId, wmoFileDataID, assetEpoch, md5, wmoPosition, wmoRotation, wmoExtentsMin,
//                       wmoExtentsMax, wmoFlags, wmoScale, wmoUniqueId, encoding:"binary", payloadBytes }
//                     <payloadBytes raw WMO root bytes><newline>                     (protocol 16)
//     a WMO-only map's world model and the WDT record that places it: answered by worldModelInspected with its placed
//     root box and group boxes (the Maps viewer's footprint; WmvMapTiles.InspectWorldModel)
//   modelTextures { requestId, ok, fileDataID, textures:[{ index, type, fileDataID, source }] }
//   modelSkin     { fileDataID, textures:[...], geosets:[...], hasGeosets,
//                   hasSubmeshVisible, submeshCount, submeshVisible:[0|1,...] }  (pushed, no request)
//     (modelTextures replies carry the same geoset fields)
//   modelGeosets  { fileDataID, revision, submeshCount, submeshVisible:[0|1,...] } (pushed, no request)
//     the host's whole per-submesh display state for the displayed model, sent when the user
//     switches a geoset; indexed by skin submesh index (SFID[0])
//   modelAnimation { fileDataID, sequenceIndex, animID, durationMs, loop, role, load } (pushed, no request)
//   modelAnimationState { fileDataID, sequenceIndex, playing, timeMs, speed, loop, explicitState, sampledAtMs,
//                         load, hasRider, rider:{ sequenceIndex, playing, timeMs, speed, loop } } (pushed, no request)
//     explicitState: true when a control set the state (play, pause, a frame step, a scrub,
//     the start of a load), false for the heartbeat. role, load, hasRider and rider (protocol 5) are
//     sent only about a ridden mount's two models: role says which of them a selection changed
//     ("mount" or "rider" -- a FileDataID cannot, a mount model can also be a race's body), load is
//     the rider's load serial, and a state keeps the mount in its top level with the rider's state
//     nested beside it, "playing" there being the mount's pause (see WmvSlotAnimation). sampledAtMs is
//     when the host sampled the state, on the system's performance counter, which this process reads
//     too (ProjectFromSample)
//   characterImage { hash, kind, width, height, format:"bgra8", encoding:"binary", payloadBytes }
//                  <width * height * 4 raw BGRA bytes><newline>                    (protocol 10)
//   characterImage { hash, kind, width, height, format:"bgra8", encoding:"base64", data }
//                                                                         (to a player before protocol 10)
//     a host-composited texture (the body, the eyes), rows top first, bytes B,G,R,A; named by
//     hash in the scenes that follow. The player keeps the newest image of each kind for the
//     life of the connection -- the host sends a kind again only when its pixels change -- and
//     an older one for as long as a scene not yet applied still names it
//   characterScene { fileDataID, revision, body:{...}, merged:[...], attachments:[...], mount:{...} }
//     the resolved state of the character on display: per model the texture each slot binds, the
//     geoset display flags, the host's bone table for a merged model and the attachment it hangs
//     from for an attached one (see UnityCharacterScene.h in the host). mount (protocol 5) is the
//     mount the character rides, as the host resolved it: its key, file, the bone and position of
//     its attachment the character hangs from, the character's scale, its texture slots, geoset
//     flags and particle colours, and the sequence each of the two models plays. It is there when
//     its key is non-empty and it names a fileDataID (HasMount), never judged by the object being
//     absent; a scene without one while a mount is ridden is the dismount (see WmvMountedScene)
//
// getModelTextures exists because a modern M2 does not name its replaceable textures (a
// creature skin's TXID entry is 0 and its texture array carries no filename) -- the skin comes
// from the client database, which only WMV can read.
//
// Asset bytes and composited images travel as binary payloads (protocol 10): a JSON line that states
// "payloadBytes": N, then N raw bytes and a newline (WmvStreamReader). The host sends them so only to a player that
// announced protocol 10; to an older one, as base64 inside the JSON line ("encoding":"base64"), which this player
// still reads from an older host.
//
// The socket read loop runs on a background thread; everything else runs on the main
// thread (Unity API is main-thread-only) via a queue drained in Update().

using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using UnityEngine;
using Wmv.Wow;

public class WmvIpcClient : MonoBehaviour
{
    public const int ProtocolVersion = 19;

    /// <summary>The loadWoWModel kind of a world model; anything else is an M2.</summary>
    public const string KindMapObject = "wmo";

    // Raised on the main thread.
    public Action<string, int, string, bool, int, string, bool> OnLoadWoWModel;  // (path, fileDataID, client, character, load, kind, keepView)
    public Action<AssetResponse> OnAssetResponse;
    public Action<AssetResponse> OnMapAssetResponse;           // every getMapAsset answer, and only those (protocol 12)
    public Action<ModelTexturesResponse> OnModelTextures;
    public Action<ModelTexturesResponse> OnModelSkin;          // pushed when the displayed skin changes
    public Action<AnimationSelection> OnModelAnimation;        // pushed when the displayed animation changes
    public Action<AnimationState> OnModelAnimationState;       // pushed on play/pause/speed/time changes
    public Action<GeosetVisibility> OnModelGeosets;            // pushed when the user switches a geoset
    public Action<CharacterImage> OnCharacterImage;            // a host-composited texture
    public Action<CharacterScene> OnCharacterScene;            // the character's resolved state
    public Action<int> OnRuntimeState;                         // the host asks what is held (query number)
    public Action<ScreenshotRequest> OnCaptureScreenshot;      // the host asks for a transparent PNG (protocol 6)
    public Action<Color32> OnViewportBackground;               // the Models viewport's background, as displayed (protocol 7)
    public Action<TerrainTileCommand> OnLoadTerrainTile;       // draw one terrain tile (protocol 12)
    public Action<string> OnClearTerrain;                      // take the terrain away (protocol 12; the host's reason)
    public Action<MapTilesRequest> OnInspectMapTiles;          // what a WDT says of its tiles (protocol 14)
    public Action<string> OnSetTerrainView;                    // the view terrain is drawn in (protocol 14)
    public Action<WorldCameraRequest> OnSetWorldCamera;        // place the world view's camera, or switch it (protocol 15)
    public Action<WorldModelRequest> OnInspectWorldModel;      // a WMO-only map's world model: its footprint (protocol 16)
    public Action<StreamTileAnswer> OnStreamTileAnswer;        // the tile loads the host opened, refused, closed (protocol 18)
    public Action<TestFlight> OnTestFlight;                    // TEST (protocol 18): the free camera flies a path itself
    public Action<string> OnStatus;                            // human-readable connection/state text

    public bool Connected { get { return connected; } }
    public int Port { get { return port; } }

    /// <summary>Map answers routed to OnMapAssetResponse (or dropped there being no handler: MapAnswersUnhandled), never
    /// to the asset cache or OnAssetResponse.</summary>
    public int MapAnswersRouted { get; private set; }
    public int MapAnswersUnhandled { get; private set; }

    /// <summary>The prefix of getMapAsset request ids: their own namespace ("map" + n), apart from every other request's
    /// ("u" + n), so a map answer is known by its id alone.</summary>
    public const string MapRequestPrefix = "map";

    public static bool IsMapRequestId(string requestId)
    {
        return requestId != null && requestId.StartsWith(MapRequestPrefix, StringComparison.Ordinal);
    }

    /// <summary>An answer to a getMapAsset: its id is in the map namespace, or it carries a role (only those answers
    /// do). Such an answer belongs to the terrain session alone.</summary>
    public static bool IsMapAnswer(string requestId, string role)
    {
        return IsMapRequestId(requestId) || !string.IsNullOrEmpty(role);
    }

    public static bool IsMapAnswer(AssetResponse r) { return r != null && IsMapAnswer(r.requestId, r.role); }

    public class AssetResponse
    {
        public string requestId;
        public bool ok;
        public string path;
        public int fileDataID;
        public int byteLength;
        public string sha1;        // as reported by WMV for a base64 answer; empty for a binary one (protocol 10) and
                                   // for an answer from the asset cache
        public string error;
        public byte[] data;        // decoded bytes (null on error); shared with the asset cache: read, never written
        public bool fromCache;     // answered by the asset cache, or by joining the same request already out
        // Protocol 12, the answer to a getMapAsset: what the request named (role, load, assetEpoch) and what the host
        // decided -- state ("available", a catalogue availability, "stale" or "refused"), its detail, how long the
        // read took (readMs) and the md5 of the bytes served (hex, "available" only). "" / 0 on any other answer.
        public string role;
        public int load;
        public int assetEpoch;
        public string state;
        public string detail;
        public double readMs;
        public string md5;

        /// <summary>The same answer for another request (one that joined this one while it was out).</summary>
        public AssetResponse For(string otherRequestId)
        {
            var copy = (AssetResponse)MemberwiseClone();
            copy.requestId = otherRequestId;
            copy.fromCache = true;
            return copy;
        }
    }

    public class ModelTextureRef
    {
        public int index;

        /// <summary>
        /// The WoW texture TYPE this feeds -- 11, 12, 13 for the three creature skin slots. NOT a
        /// position: a model's texture-variation order and its M2 texture-slot order need not
        /// agree, so the renderer matches this against the type each M2 texture declares. 0 when
        /// the host did not say, in which case index is used as a fallback.
        /// </summary>
        public int type;

        public int fileDataID;

        /// <summary>
        /// "selection" -- the skin the WMV viewport is showing right now;
        /// "database"  -- the model's default skin from CreatureDisplayInfo;
        /// "convention"-- a listfile naming guess for a model no creature display references.
        /// </summary>
        public string source = "";
    }

    public class ModelTexturesResponse
    {
        public string requestId;
        public bool ok;
        public int fileDataID;
        public string error;
        public ModelTextureRef[] textures = new ModelTextureRef[0];

        /// <summary>
        /// The geoset numbers the displayed variant switches on. Only meaningful when
        /// <see cref="hasGeosets"/> is true -- an empty list then means "this variant switches
        /// none on", which hides every submesh whose id is not 0. When hasGeosets is false the
        /// host had no selection to report and geoset visibility must be left alone.
        /// </summary>
        public int[] geosets = new int[0];
        public bool hasGeosets;

        /// <summary>
        /// The item ParticleColor override for this model, as nine bytes:
        /// [start.r, start.g, start.b, mid.r, mid.g, mid.b, end.r, end.g, end.b], 0..255. Empty
        /// when the item display names no particle colour, which is the common case.
        ///
        /// An M2 particle emitter opts in with ParticleColorIndex 11, 12 or 13, selecting the
        /// start, middle or end set. Retail assigns the colour on ItemDisplayInfo and recolours
        /// the emitters of the models that display references; WMV has always parsed the emitter's
        /// index but never resolved a colour to put there.
        /// </summary>
        public int[] particleColor = new int[0];
        public int particleColorId;

        /// <summary>
        /// The host's per-SUBMESH display state, when it sent one (protocol 2): one entry per skin
        /// submesh, true = drawn. It is the host's final answer and decides instead of the geoset
        /// ids; null when hasSubmeshVisible is false.
        /// </summary>
        public bool hasSubmeshVisible;
        public int submeshCount;
        public bool[] submeshVisible;
    }

    /// <summary>
    /// The host's whole per-submesh display state for one model, pushed when the user switches a
    /// geoset. revision numbers the push so its acknowledgement can be matched to it.
    /// </summary>
    public struct GeosetVisibility
    {
        public int fileDataID;
        public int revision;
        public int submeshCount;
        public bool[] visible;
    }

    /// <summary>
    /// Which animation the app is showing. sequenceIndex is the one to act on -- it indexes the
    /// model's animation table, which is how the keyframes are stored; two sequences can share an
    /// animID, so the id alone cannot pick one.
    /// </summary>
    public struct AnimationSelection
    {
        public int fileDataID;
        public int sequenceIndex;
        public int animID;
        public int durationMs;
        public bool loop;
        /// <summary>Protocol 5, a ridden mount's two models: which of them the selection changed, "mount" or "rider"
        /// (WmvSlotAnimation.RoleMount / RoleRider); "" about any other model. load is the rider's load serial (0 with
        /// no role).</summary>
        public string role;
        public int load;
        public double receivedSeconds;
    }

    /// <summary>The rider's playback nested in a ridden mount's modelAnimationState (protocol 5). playing is the MOUNT's
    /// pause, which is what gates the host's whole tree.</summary>
    public struct RiderPlayback
    {
        public int sequenceIndex;
        public bool playing;
        public int timeMs;
        public float speed;
        public bool loop;
    }

    /// <summary>
    /// How the app is playing that animation. timeMs is where the app's own clock is; the player
    /// decides whether that is far enough from its own to be worth snapping to.
    /// </summary>
    public struct AnimationState
    {
        public bool flight;
        public float flightSpeed;
        public int fileDataID;
        public int sequenceIndex;
        public bool playing;
        public int timeMs;
        public float speed;
        public bool loop;
        public bool explicitState;
        public double receivedSeconds;
        /// <summary>Protocol 5, a ridden mount: the top-level fields are the mount's; hasRider says rider carries the
        /// rider's state, sampled in the same host call; load is the rider's load serial. hasRider false: no rider, and
        /// the state is about the model its FileDataID names, as before.</summary>
        public int load;
        public bool hasRider;
        public RiderPlayback rider;

        /// <summary>The nested rider's state as a state of its own: its sequence, play/pause, position, speed and loop,
        /// with this message's explicitState, arrival time and load. FileDataID 0: the wire does not name the rider's
        /// file there.</summary>
        public AnimationState RiderState()
        {
            return new AnimationState
            {
                fileDataID = 0, sequenceIndex = rider.sequenceIndex, playing = rider.playing, timeMs = rider.timeMs,
                speed = rider.speed, loop = rider.loop, explicitState = explicitState, receivedSeconds = receivedSeconds,
                load = load,
            };
        }
    }

    /// <summary>The host's captureScreenshot (protocol 6): the PNG to write, at which size. request numbers the
    /// question so its screenshotSaved answer can be matched to it.</summary>
    public struct ScreenshotRequest
    {
        public int request;
        public string path;
        public int width;
        public int height;
    }

    /// <summary>The host's loadTerrainTile (protocol 12): one tile of the active client's map, and the load and client
    /// epoch every file request of it states (see WmvTerrainSession).</summary>
    public class TerrainTileCommand
    {
        public int Load;                // the host's terrain serial (> 0), echoed in getMapAsset and terrainTileLoaded
        public int AssetEpoch;          // the host's client epoch at send time
        public int MapId;
        public string MapDirectory = "";
        public string Product = "";     // the active client (its product code), for the log and the dump
        public string Build = "";
        public int WdtFileDataID;
        public string WdtResolvedBy = "";   // "fileDataID" or "path"
        public string WdtPath = "";         // path clients only
        public int TileX, TileY;
        public int MaxAssetBytes;       // the largest map file the host will serve; a larger answer is refused
        // Protocol 13: the texture limits (0 from a host before it: no tex0 and no texture is asked for).
        public int MaxTextureBytes;         // one texture file
        public int MaxTextureBytesPerLoad;  // texture bytes the host reads for one load (the host enforces it)
        public int MaxTexturesPerLoad;      // texture ids one load may ask for
        // Protocol 15: where the world view's camera goes when this tile is put on screen (the Maps viewer's Enter world);
        // null: the tile is framed as before (the overview).
        public WorldCameraRequest Camera;
        // Protocol 16 (loadWorld, docs/world-viewer/06-world-models-in-world.md): World -- the load places world models: the
        // tile's MODF records (its obj0 ADT), or, with GlobalWmo, the WDT's global record of a WMO-only map (no tile). The
        // world-model budgets the host states and enforces; a loadTerrainTile states none and places nothing.
        public bool World, GlobalWmo;
        public int MaxWmoFileBytes;          // one world-model root or group file
        public int MaxWmoBytesPerLoad;       // root and group bytes the host reads for one load
        public int MaxWmoTextureBytes;       // one world-model texture
        public int MaxWmoTextureBytesPerLoad;
        public int MaxWmoTexturesPerLoad;    // world-model texture ids one load may ask for
        public int MaxWmoModelsPerLoad;      // unique world models (roots) one load may ask for
        public int MaxWmoGroupsPerLoad;      // group files one load may ask for
        public int MaxWmoPlacementsPerLoad;  // placements one load places
        // Protocol 17 (docs/world-viewer/07-doodads.md): the doodad budgets the host states and enforces; a protocol-16 host
        // states none and the world places no doodad.
        public bool Doodads;
        public int MaxM2FileBytes;           // one doodad model or skin file
        public int MaxM2BytesPerLoad;        // model and skin bytes the host reads for one load
        public int MaxM2TextureBytes;        // one doodad texture
        public int MaxM2TextureBytesPerLoad;
        public int MaxM2ModelsPerLoad;       // unique doodad models one load may ask for
        public int MaxM2TexturesPerLoad;     // doodad texture ids one load may ask for
        public int MaxDoodadInstancesPerLoad;   // doodads one load places
        // Protocol 18 (docs/world-viewer/08-streaming.md): the load is a streaming session's entry -- the player keeps the tiles
        // around its camera loaded, each a tile load of its own the host opens (streamTile). Null: the single-tile load.
        public StreamingOffer Streaming;
        // Protocol 19 (docs/world-viewer/09-liquids.md 1.6): the load offers liquids -- the frames' budgets and the host's
        // catalogue of the client's liquid types. Null: not offered (an older host), and no liquid is drawn.
        public LiquidOffer Liquids;

        /// <summary>The host serves terrain textures for this load (a protocol-13 host states maxTextureBytes).</summary>
        public bool TexturesOffered { get { return MaxTextureBytes > 0; } }

        /// <summary>The load places world models (a protocol-16 loadWorld with its budgets).</summary>
        public bool WorldModelsOffered { get { return World && MaxWmoModelsPerLoad > 0; } }

        /// <summary>The load places doodads (a protocol-17 loadWorld with its doodad budgets).</summary>
        public bool DoodadsOffered { get { return World && MaxM2ModelsPerLoad > 0; } }

        public override string ToString()
        {
            return string.Format("load {0} epoch {1} map {2} ({3}) {4} wdt {5} (by {6}{7}) client {8} {9} maxAssetBytes {10}{11}{12}",
                                 Load, AssetEpoch, MapId, MapDirectory, GlobalWmo ? "global world model" : "tile " + TileX + "_" + TileY,
                                 WdtFileDataID, WdtResolvedBy, string.IsNullOrEmpty(WdtPath) ? "" : " " + WdtPath, Product, Build, MaxAssetBytes,
                                 TexturesOffered ? string.Format(" maxTextureBytes {0} maxTextureBytesPerLoad {1} maxTexturesPerLoad {2}",
                                                                 MaxTextureBytes, MaxTextureBytesPerLoad, MaxTexturesPerLoad)
                                                 : " (no textures offered)",
                                 WorldModelsOffered
                                     ? string.Format("; world models: maxWmoFileBytes {0} maxWmoBytesPerLoad {1} maxWmoTextureBytes {2} " +
                                                     "maxWmoTextureBytesPerLoad {3} maxWmoTexturesPerLoad {4} maxWmoModelsPerLoad {5} " +
                                                     "maxWmoGroupsPerLoad {6} maxWmoPlacementsPerLoad {7}", MaxWmoFileBytes, MaxWmoBytesPerLoad,
                                                     MaxWmoTextureBytes, MaxWmoTextureBytesPerLoad, MaxWmoTexturesPerLoad, MaxWmoModelsPerLoad,
                                                     MaxWmoGroupsPerLoad, MaxWmoPlacementsPerLoad) +
                                       (DoodadsOffered
                                            ? string.Format("; doodads: maxM2FileBytes {0} maxM2BytesPerLoad {1} maxM2TextureBytes {2} " +
                                                            "maxM2TextureBytesPerLoad {3} maxM2ModelsPerLoad {4} maxM2TexturesPerLoad {5} " +
                                                            "maxDoodadInstancesPerLoad {6}", MaxM2FileBytes, MaxM2BytesPerLoad, MaxM2TextureBytes,
                                                            MaxM2TextureBytesPerLoad, MaxM2ModelsPerLoad, MaxM2TexturesPerLoad, MaxDoodadInstancesPerLoad) +
                                                   (Streaming != null ? "; " + Streaming : "")
                                            : "; no doodads offered")
                                     : World ? "; world models asked for, but no budget stated" : "") +
                   (Liquids != null ? "; " + Liquids : "; no liquids offered");
        }
    }

    /// <summary>loadTerrainTile's and loadWorld's "liquids" (protocol 19, docs/world-viewer/09-liquids.md 1.6): the budgets of
    /// the liquid frames (role "liquidTexture": ids a load may ask for, one file's bytes, a load's bytes -- the host enforces
    /// them) and the host's catalogue of the client's liquid types, read once per client load from its tables ("ok"; or
    /// "unavailable", with why, and no types). The catalogue is the player's view of those tables (Wow/World/LiquidFormat.cs:
    /// LiquidTypeView per LiquidType id), one per load command, replaced by the next. With it, the map's zone lights
    /// ("lights", LightsFrom): Lights only when their state is "ok"; a streaming session's tile loads share the entry's
    /// offer object, so its lights travel with it.</summary>
    public class LiquidOffer
    {
        public int MaxTexturesPerLoad, MaxTextureBytes, MaxTextureBytesPerLoad;
        public string CatalogState = "", CatalogDetail = "", CatalogProduct = "", CatalogBuild = "";
        public readonly Wmv.Wow.World.LiquidCatalog Catalog = new Wmv.Wow.World.LiquidCatalog();
        public int FrameIds;                    // the frame ids the catalogue lists, over every type
        public int BumpIds;                     // the types that name a ripple map
        // The zone lights: their state as sent ("" when the offer carried none, "ok", "unavailable") and why, and the lights
        // themselves (null unless "ok": the liquids keep their type colours).
        public string LightsState = "", LightsDetail = "";
        public Wmv.Wow.World.LiquidLights Lights;

        public override string ToString()
        {
            return string.Format("liquids: maxTexturesPerLoad {0} maxTextureBytes {1} maxTextureBytesPerLoad {2}, catalogue {3} ({4} {5}: {6} " +
                                 "type(s), {7} frame id(s){10}){8}; {9}", MaxTexturesPerLoad, MaxTextureBytes, MaxTextureBytesPerLoad, CatalogState,
                                 CatalogProduct, CatalogBuild, Catalog.Count, FrameIds,
                                 string.IsNullOrEmpty(CatalogDetail) ? "" : ": " + CatalogDetail,
                                 Lights != null ? "lights ok, " + Lights.Summary()
                                                : string.IsNullOrEmpty(LightsState) ? "no lights"
                                                                                    : "lights " + LightsState +
                                                                                      (string.IsNullOrEmpty(LightsDetail) ? "" : ": " + LightsDetail),
                                 BumpIds > 0 ? ", " + BumpIds + " type(s) with a ripple map" : "");
        }
    }

    /// <summary>inspectWorldModel (protocol 16): a WMO-only map's global world model -- its root's bytes as the map asset
    /// worker read and checked them, and the WDT's MODF record the player stated in mapTilesInspected -- for the footprint
    /// the Maps viewer draws (WmvMapTiles.InspectWorldModel). Positions as stored (file frame), angles in degrees.</summary>
    public class WorldModelRequest
    {
        public int Inspection, MapId, WmoFileDataID, AssetEpoch;
        public string Md5 = "", Encoding = "";
        public byte[] Bytes;
        public float[] Position = new float[3], Rotation = new float[3], ExtentsMin = new float[3], ExtentsMax = new float[3];
        public int Flags, ScaleRaw;
        public uint UniqueId;
    }

    /// <summary>worldModelInspected (protocol 16): the world model's placed root box and every group's placed box (world
    /// frame), for the overhead map's footprint.</summary>
    public class WorldModelInspectReport
    {
        public int Inspection, MapId, WmoFileDataID, AssetEpoch;
        public string Status = "failed", Reason = "";   // "inspected" | "failed"
        public string Md5 = "";
        public long Bytes = -1;
        public int Groups;                              // MOHD's group count
        public int InteriorGroups;
        public double[] RootBox = new double[6];        // placed MOHD box: min x, y, z, max x, y, z (world frame)
        public double[] Extents = new double[6];        // the record's extents as the world box they are
        public float[] GroupBoxes;                      // Groups x 6 floats, world frame (min then max)
        public uint[] GroupFlags;                       // Groups MOGI flags
        public double ParseMs = -1;
    }

    public void ReportWorldModelInspected(WorldModelInspectReport r) { Send(WorldModelInspectedLine(r)); }

    /// <summary>The worldModelInspected line (also for the self-test): the group boxes and flags travel as base64 of little-
    /// endian floats and u32s (at most 1,024 groups: a root with more is refused by WmvMapTiles.InspectWorldModel).</summary>
    public static string WorldModelInspectedLine(WorldModelInspectReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"worldModelInspected\",\"inspection\":").Append(r.Inspection)
          .Append(",\"status\":\"").Append(Escape(r.Status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"')
          .Append(",\"mapId\":").Append(r.MapId)
          .Append(",\"wmoFileDataID\":").Append(r.WmoFileDataID)
          .Append(",\"assetEpoch\":").Append(r.AssetEpoch)
          .Append(",\"wmoBytes\":").Append(r.Bytes)
          .Append(",\"md5\":\"").Append(Escape(r.Md5 ?? "")).Append('"')
          .Append(",\"groups\":").Append(r.Groups)
          .Append(",\"interiorGroups\":").Append(r.InteriorGroups)
          .Append(",\"parseMs\":").Append(r.ParseMs.ToString("0.###", inv));
        AppendDoubles(sb, "rootBox", r.RootBox);
        AppendDoubles(sb, "extents", r.Extents);
        if (r.GroupBoxes != null)
        {
            var b = new byte[r.GroupBoxes.Length * 4];
            Buffer.BlockCopy(r.GroupBoxes, 0, b, 0, b.Length);
            sb.Append(",\"groupBoxes\":\"").Append(Convert.ToBase64String(b)).Append('"');
        }
        if (r.GroupFlags != null)
            sb.Append(",\"groupFlags\":\"").Append(Convert.ToBase64String(U32Bytes(r.GroupFlags))).Append('"');
        sb.Append('}');
        return sb.ToString();
    }

    static void AppendDoubles(StringBuilder sb, string key, double[] v)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        sb.Append(",\"").Append(key).Append("\":[");
        // JSON has no NaN or infinity (a malformed file's number would make the whole line unreadable): null instead.
        for (int i = 0; v != null && i < v.Length; i++)
            sb.Append(i > 0 ? "," : "").Append(double.IsNaN(v[i]) || double.IsInfinity(v[i]) ? "null" : v[i].ToString("R", inv));
        sb.Append(']');
    }

    /// <summary>worldShown (protocol 16): a world load's scene has something on screen -- its tile committed, or a WMO-only
    /// map's world model placed -- and its camera placed; the load goes on until its terrainTileLoaded (the world models
    /// still loading). Sent once per load, never on a timer.</summary>
    public class WorldShownReport
    {
        public int Load, MapId, TileX = -1, TileY = -1;
        public string Kind = "tile";                    // "tile" | "globalWmo"
        public double[] Origin = new double[3];         // the scene's floating origin (world frame)
        public int Placements, Models;                  // placements planned, unique world models among them
        public double ShownMs = -1;                     // from the load's start
    }

    public void ReportWorldShown(WorldShownReport r) { Send(WorldShownLine(r)); }

    // ---------------------------------------------------------------- STREAMING (protocol 18, docs/world-viewer/08-streaming.md)

    /// <summary>loadWorld's "streaming": the region the player keeps around its camera -- the tiles within Radius of the camera's
    /// tile wanted, kept while within KeepRadius, at most MaxResidentTiles resident --, the requests it keeps out at once for
    /// the whole session (RequestCredit) and the tile loads the host keeps open at most (MaxTileLoads).</summary>
    public class StreamingOffer
    {
        public int Radius, KeepRadius, MaxResidentTiles, RequestCredit, MaxTileLoads;

        public override string ToString()
        {
            return string.Format("streaming: radius {0}, kept within {1}, at most {2} resident, {3} requests out, {4} tile loads",
                                 Radius, KeepRadius, MaxResidentTiles, RequestCredit, MaxTileLoads);
        }
    }

    /// <summary>streamTileAnswer: the tile loads the host opened for the session (with their load ids), refused (why) and
    /// closed.</summary>
    public class StreamTileAnswer
    {
        public int Load, AssetEpoch;
        public readonly List<StreamTileOpened> Opened = new List<StreamTileOpened>();
        public readonly List<StreamTileOpened> Refused = new List<StreamTileOpened>();
        public readonly List<int> Closed = new List<int>();
    }

    public class StreamTileOpened
    {
        public int X, Y, TileLoad;
        public string Refused = "";
    }

    /// <summary>streamTile: the tiles the session opens (x, y) and the tile loads it closes.</summary>
    public void SendStreamTile(int load, List<int[]> open, List<int> close)
    {
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"streamTile\",\"load\":").Append(load).Append(",\"open\":[");
        for (int i = 0; open != null && i < open.Count; i++)
            sb.Append(i > 0 ? "," : "").Append("{\"x\":").Append(open[i][0]).Append(",\"y\":").Append(open[i][1]).Append('}');
        sb.Append("],\"close\":[");
        for (int i = 0; close != null && i < close.Count; i++)
            sb.Append(i > 0 ? "," : "").Append(close[i]);
        sb.Append("]}");
        Send(sb.ToString());
    }

    /// <summary>streamState (at most every WmvTerrainSession.StreamStateIntervalMs): the session -- the camera, each tile of the
    /// region and what it has, the requests out, what is resident and pooled, the frames since the last one.</summary>
    public class StreamStateReport
    {
        public int Load;
        public double CameraX, CameraY, CameraZ;
        public int CameraTileX = -1, CameraTileY = -1;
        public readonly List<StreamStateTile> Tiles = new List<StreamStateTile>();
        public int TilesResident, TilesLoading, TilesWanted, TilesOpened, TilesClosed, TilesFailed;
        public int RequestsOut, RequestsWaiting;
        public long FilesAsked, FilesAnswered, BytesReceived;
        public int WorldPrototypes, WorldPrototypesPooled, DoodadPrototypes, DoodadPrototypesPooled, Textures, TexturesPooled;
        public int WorldInstances, DoodadInstances;
        public long GpuTextureBytes, GpuGeometryBytes, PoolBytes, PrototypesEvicted, TexturesEvicted;
        public double FrameMsMax, FrameMsP95, UploadMsMax, WorkMsMax;
        public int FramesOver33, FramesOver50;
        public long ManagedHeapBytes;
        // Protocol 19: the resident tiles that draw liquid, the scene's liquid types (pooled included), their frame arrays, and
        // the GPU bytes of those arrays and of the resident tiles' liquid geometry.
        public int LiquidTiles, LiquidTypes, LiquidTextures;
        public long LiquidBytes;
    }

    public class StreamStateTile
    {
        public int X, Y, Load;
        public string State = "";
        public int FilesAsked, FilesAnswered;
        public long Bytes;
        public int WorldPlaced, WorldPlacements, DoodadsPlaced, DoodadInstances;
    }

    public void ReportStreamState(StreamStateReport r) { Send(StreamStateLine(r)); }

    /// <summary>TEST (protocol 18, the harnesses' acceptance flight): the free camera flies the path -- world points (x, y, z,
    /// yards), at Speed yards a second, pitched Pitch degrees, facing the way it goes -- as a user would fly it, with no
    /// message from the viewer on the way; answered testFlightDone.</summary>
    public class TestFlight
    {
        public int Request;
        public double[] Points = new double[0];
        public double Speed;
        public float Pitch;
    }

    public void ReportTestFlightDone(int request, string status, string reason, double seconds, int frames, double frameMsMax, double distance)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        Send("{\"type\":\"testFlightDone\",\"request\":" + request + ",\"status\":\"" + Escape(status ?? "") + "\",\"reason\":\"" +
             Escape(reason ?? "") + "\",\"seconds\":" + seconds.ToString("0.###", inv) + ",\"frames\":" + frames + ",\"frameMsMax\":" +
             frameMsMax.ToString("0.###", inv) + ",\"distance\":" + distance.ToString("0.###", inv) + "}");
    }

    public static string StreamStateLine(StreamStateReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"streamState\",\"load\":").Append(r.Load)
          .Append(",\"cameraX\":").Append(r.CameraX.ToString("0.###", inv))
          .Append(",\"cameraY\":").Append(r.CameraY.ToString("0.###", inv))
          .Append(",\"cameraZ\":").Append(r.CameraZ.ToString("0.###", inv))
          .Append(",\"cameraTileX\":").Append(r.CameraTileX)
          .Append(",\"cameraTileY\":").Append(r.CameraTileY)
          .Append(",\"tiles\":[");
        for (int i = 0; i < r.Tiles.Count; i++)
        {
            StreamStateTile t = r.Tiles[i];
            sb.Append(i > 0 ? "," : "").Append("{\"x\":").Append(t.X).Append(",\"y\":").Append(t.Y).Append(",\"load\":").Append(t.Load)
              .Append(",\"state\":\"").Append(Escape(t.State ?? "")).Append('"')
              .Append(",\"filesAsked\":").Append(t.FilesAsked).Append(",\"filesAnswered\":").Append(t.FilesAnswered)
              .Append(",\"bytes\":").Append(t.Bytes).Append(",\"worldPlaced\":").Append(t.WorldPlaced)
              .Append(",\"worldPlacements\":").Append(t.WorldPlacements).Append(",\"doodadsPlaced\":").Append(t.DoodadsPlaced)
              .Append(",\"doodadInstances\":").Append(t.DoodadInstances).Append('}');
        }
        sb.Append(']')
          .Append(",\"tilesResident\":").Append(r.TilesResident).Append(",\"tilesLoading\":").Append(r.TilesLoading)
          .Append(",\"tilesWanted\":").Append(r.TilesWanted).Append(",\"tilesOpened\":").Append(r.TilesOpened)
          .Append(",\"tilesClosed\":").Append(r.TilesClosed).Append(",\"tilesFailed\":").Append(r.TilesFailed)
          .Append(",\"requestsOut\":").Append(r.RequestsOut).Append(",\"requestsWaiting\":").Append(r.RequestsWaiting)
          .Append(",\"filesAsked\":").Append(r.FilesAsked).Append(",\"filesAnswered\":").Append(r.FilesAnswered)
          .Append(",\"bytesReceived\":").Append(r.BytesReceived)
          .Append(",\"worldPrototypes\":").Append(r.WorldPrototypes).Append(",\"worldPrototypesPooled\":").Append(r.WorldPrototypesPooled)
          .Append(",\"doodadPrototypes\":").Append(r.DoodadPrototypes).Append(",\"doodadPrototypesPooled\":").Append(r.DoodadPrototypesPooled)
          .Append(",\"textures\":").Append(r.Textures).Append(",\"texturesPooled\":").Append(r.TexturesPooled)
          .Append(",\"worldInstances\":").Append(r.WorldInstances).Append(",\"doodadInstances\":").Append(r.DoodadInstances)
          .Append(",\"gpuTextureBytes\":").Append(r.GpuTextureBytes).Append(",\"gpuGeometryBytes\":").Append(r.GpuGeometryBytes)
          .Append(",\"poolBytes\":").Append(r.PoolBytes).Append(",\"prototypesEvicted\":").Append(r.PrototypesEvicted)
          .Append(",\"texturesEvicted\":").Append(r.TexturesEvicted)
          .Append(",\"frameMsMax\":").Append(r.FrameMsMax.ToString("0.###", inv))
          .Append(",\"frameMsP95\":").Append(r.FrameMsP95.ToString("0.###", inv))
          .Append(",\"uploadMsMax\":").Append(r.UploadMsMax.ToString("0.###", inv))
          .Append(",\"workMsMax\":").Append(r.WorkMsMax.ToString("0.###", inv))
          .Append(",\"framesOver33\":").Append(r.FramesOver33).Append(",\"framesOver50\":").Append(r.FramesOver50)
          .Append(",\"managedHeapBytes\":").Append(r.ManagedHeapBytes)
          .Append(",\"liquidTiles\":").Append(r.LiquidTiles).Append(",\"liquidTypes\":").Append(r.LiquidTypes)
          .Append(",\"liquidTextures\":").Append(r.LiquidTextures).Append(",\"liquidBytes\":").Append(r.LiquidBytes)
          .Append('}');
        return sb.ToString();
    }

    public static string WorldShownLine(WorldShownReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"worldShown\",\"load\":").Append(r.Load)
          .Append(",\"mapId\":").Append(r.MapId)
          .Append(",\"kind\":\"").Append(Escape(r.Kind ?? "")).Append('"')
          .Append(",\"tileX\":").Append(r.TileX)
          .Append(",\"tileY\":").Append(r.TileY)
          .Append(",\"placements\":").Append(r.Placements)
          .Append(",\"models\":").Append(r.Models)
          .Append(",\"shownMs\":").Append(r.ShownMs.ToString("0.###", inv));
        AppendDoubles(sb, "originWorld", r.Origin);
        sb.Append('}');
        return sb.ToString();
    }

    /// <summary>worldProgress (protocol 16): a world load's world models, counted -- sent when a phase begins and at most
    /// every WmvTerrainSession.ProgressIntervalMs while the load is in flight (never after its report).</summary>
    public class WorldProgressReport
    {
        public int Load;
        public string Phase = "";                       // the world models' phase, WmvTerrainSession.WorldPhase names
        public int Placements, Models, ModelsRequested, ModelsParsed, ModelsBuilt, ModelsFailed, ModelsUnsupported, ModelsNotRequested;
        public int Placed, PlacementsFailed;
        public int FilesAsked, FilesAnswered;
        public long BytesReceived;
        public double ElapsedMs;
        // Protocol 17: the doodads (-1 where they were not offered).
        public bool HasDoodads;
        public string DoodadPhase = "none";
        public int DoodadInstances, DoodadPlaced, DoodadModels, DoodadModelsBuilt, DoodadModelsFailed, DoodadFilesAsked, DoodadFilesAnswered;
        public long DoodadBytes;
    }

    public void ReportWorldProgress(WorldProgressReport r) { Send(WorldProgressLine(r)); }

    public static string WorldProgressLine(WorldProgressReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"worldProgress\",\"load\":").Append(r.Load)
          .Append(",\"phase\":\"").Append(Escape(r.Phase ?? "")).Append('"')
          .Append(",\"placements\":").Append(r.Placements)
          .Append(",\"models\":").Append(r.Models)
          .Append(",\"modelsRequested\":").Append(r.ModelsRequested)
          .Append(",\"modelsParsed\":").Append(r.ModelsParsed)
          .Append(",\"modelsBuilt\":").Append(r.ModelsBuilt)
          .Append(",\"modelsFailed\":").Append(r.ModelsFailed)
          .Append(",\"modelsUnsupported\":").Append(r.ModelsUnsupported)
          .Append(",\"modelsNotRequested\":").Append(r.ModelsNotRequested)
          .Append(",\"placed\":").Append(r.Placed)
          .Append(",\"placementsFailed\":").Append(r.PlacementsFailed)
          .Append(",\"filesAsked\":").Append(r.FilesAsked)
          .Append(",\"filesAnswered\":").Append(r.FilesAnswered)
          .Append(",\"bytesReceived\":").Append(r.BytesReceived)
          .Append(",\"elapsedMs\":").Append(r.ElapsedMs.ToString("0.#", inv));
        if (r.HasDoodads)
            sb.Append(",\"doodadPhase\":\"").Append(Escape(r.DoodadPhase ?? "")).Append('"')
              .Append(",\"doodadInstances\":").Append(r.DoodadInstances)
              .Append(",\"doodadPlaced\":").Append(r.DoodadPlaced)
              .Append(",\"doodadModels\":").Append(r.DoodadModels)
              .Append(",\"doodadModelsBuilt\":").Append(r.DoodadModelsBuilt)
              .Append(",\"doodadModelsFailed\":").Append(r.DoodadModelsFailed)
              .Append(",\"doodadFilesAsked\":").Append(r.DoodadFilesAsked)
              .Append(",\"doodadFilesAnswered\":").Append(r.DoodadFilesAnswered)
              .Append(",\"doodadBytes\":").Append(r.DoodadBytes);
        sb.Append('}');
        return sb.ToString();
    }

    /// <summary>The world view's camera, as the host asks for it (protocol 15: setWorldCamera, and loadTerrainTile's camera).
    /// Positions are the game's world frame (yards; x north, y west, z up), never Unity's: the player converts them with
    /// the scene's floating origin. Mode "fly" puts the free camera at (X, Y, Z) -- on the ground under (X, Y), plus
    /// EyeHeight, when HasZ is false -- facing Yaw (degrees clockwise from north) and Pitch (degrees, up positive);
    /// "overview" frames the tile on screen as Stage 03 does.</summary>
    public class WorldCameraRequest
    {
        public int Request;             // the host's serial, echoed in worldCameraApplied (0: no answer is wanted)
        public string Mode = "";        // "fly" | "overview"
        public double X, Y, Z;
        public bool HasZ;
        public float Yaw, Pitch;
        public float EyeHeight;
        // Protocol 16: "start inside" -- the camera where the world model's own geometry offers a covered floor (a WMO-only
        // map entered without a target; WorldModelGeometry.StartPoint), X, Y, Z and the angles ignored.
        public bool Start;

        public override string ToString()
        {
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            return Mode == "fly" && Start
                ? string.Format(inv, "request {0} fly at the world model's start point", Request)
                : Mode == "fly"
                ? string.Format(inv, "request {0} fly at ({1:0.###}, {2:0.###}, {3}) yaw {4:0.#} pitch {5:0.#} eye {6:0.##}", Request, X, Y,
                                HasZ ? Z.ToString("0.###", inv) : "ground", Yaw, Pitch, EyeHeight)
                : string.Format(inv, "request {0} {1}", Request, Mode);
        }
    }

    /// <summary>worldCameraApplied (protocol 15): what the player did with a WorldCameraRequest.</summary>
    public class WorldCameraReport
    {
        public int Request;
        public string Mode = "", Status = "refused", Reason = "";   // "applied" | "refused"
        public double X, Y, Z;                 // where the camera is, in the game's world frame
        public bool Ground;                    // Z came from the ground under the point
        public double GroundZ = double.NaN;    // the ground's height there (NaN: none -- a hole, or off the tile)
        public float Yaw, Pitch;
        public int Load;                       // the tile on screen, 0 for none
    }

    public void ReportWorldCameraApplied(WorldCameraReport r) { Send(WorldCameraAppliedLine(r)); }

    /// <summary>The worldCameraApplied line (also for the self-test).</summary>
    public static string WorldCameraAppliedLine(WorldCameraReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"worldCameraApplied\",\"request\":").Append(r.Request)
          .Append(",\"mode\":\"").Append(Escape(r.Mode ?? "")).Append('"')
          .Append(",\"status\":\"").Append(Escape(r.Status ?? "")).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"')
          .Append(",\"x\":").Append(r.X.ToString("R", inv))
          .Append(",\"y\":").Append(r.Y.ToString("R", inv))
          .Append(",\"z\":").Append(r.Z.ToString("R", inv))
          .Append(",\"ground\":").Append(r.Ground ? "true" : "false")
          .Append(",\"hasGroundZ\":").Append(double.IsNaN(r.GroundZ) ? "false" : "true")
          .Append(",\"groundZ\":").Append(double.IsNaN(r.GroundZ) ? "0" : r.GroundZ.ToString("R", inv))
          .Append(",\"yaw\":").Append(r.Yaw.ToString("R", inv))
          .Append(",\"pitch\":").Append(r.Pitch.ToString("R", inv))
          .Append(",\"load\":").Append(r.Load)
          .Append('}');
        return sb.ToString();
    }

    /// <summary>Three floats of a host line's array (missing entries 0).</summary>
    static float[] Floats3(float[] v)
    {
        var r = new float[3];
        for (int i = 0; v != null && i < 3 && i < v.Length; i++)
            r[i] = v[i];
        return r;
    }

    /// <summary>The camera a host line states (setWorldCamera, or loadTerrainTile's), or null when it states none.</summary>
    static WorldCameraRequest CameraFrom(Msg msg)
    {
        if (msg == null || string.IsNullOrEmpty(msg.cameraMode))
            return null;
        return new WorldCameraRequest
        {
            Request = msg.cameraRequest, Mode = msg.cameraMode, X = msg.cameraX, Y = msg.cameraY, Z = msg.cameraZ,
            HasZ = msg.cameraHasZ, Yaw = msg.cameraYaw, Pitch = msg.cameraPitch, EyeHeight = msg.cameraEyeHeight, Start = msg.cameraStart,
        };
    }

    // ---- characterScene -------------------------------------------------------------------------
    // Field names are the wire's; JsonUtility fills absent fields with defaults.

    /// <summary>One texture slot's binding: a FileDataID, or the hash of a characterImage.</summary>
    [Serializable] public class SceneTexture
    {
        public int slot;
        public int type;
        public int fileDataID;
        public string image;
    }

    [Serializable] public class SceneBody
    {
        public SceneTexture[] textures = new SceneTexture[0];
        public int submeshCount;
        public int[] submeshVisible = new int[0];
        public bool closeRightHand;
        public bool closeLeftHand;
        public int fistSequence;
        public int fistTimeMs;
        public int[] rightFingerBones = new int[0];
        public int[] leftFingerBones = new int[0];
    }

    [Serializable] public class SceneMerged
    {
        public string key;
        public int fileDataID;
        public int mergeIndex;
        public SceneTexture[] textures = new SceneTexture[0];
        public int[] handSubmeshes = new int[0];
        public SceneTexture handTexture;
        public int submeshCount;
        public int[] submeshVisible = new int[0];
        public int[] boneMap = new int[0];
    }

    [Serializable] public class SceneAttachment
    {
        public string key;
        public int fileDataID;
        public int attachmentId;
        public int bone;
        public float[] position = new float[0];
        public bool mirrored;
        public bool visible;
        public float scale;
        public SceneTexture[] textures = new SceneTexture[0];
        public int submeshCount;
        public int[] submeshVisible = new int[0];
    }

    /// <summary>
    /// The mount a character rides (protocol 5), as the host resolved it: key is "M" and the host's mount
    /// serial (a new mount model gets a new one, a scene describing the same mount again keeps it); bone
    /// and position are the entry of the MOUNT's attachment table the character's attachmentId gives (bone
    /// -1 when it has none), position in WoW model space; riderScale is the character's scale; textures
    /// are the mount's own slots (a slot bound to nothing is not listed); particleColorSets, when present,
    /// the three ParticleColor sets (emitter index 11, 12, 13) as start, mid and end RGB, 27 numbers;
    /// sequenceIndex and riderSequenceIndex what the mount and the character play when the scene was built.
    /// </summary>
    [Serializable] public class SceneMount
    {
        public string key;
        public int fileDataID;
        public string path;
        public int displayID;
        public int attachmentId;
        public int bone;
        public float[] position = new float[0];
        public float riderScale;
        public SceneTexture[] textures = new SceneTexture[0];
        public int submeshCount;
        public int[] submeshVisible = new int[0];
        public int[] particleColorSets = new int[0];
        public int sequenceIndex;
        public int riderSequenceIndex;
    }

    public class CharacterScene
    {
        public bool attachmentsOnly;
        public int load;
        public int fileDataID;
        public int revision;
        public SceneBody body;
        public SceneMerged[] merged = new SceneMerged[0];
        public SceneAttachment[] attachments = new SceneAttachment[0];
        /// <summary>The mount, as the line carried it. Whether there IS one is HasMount, never a null test:
        /// what JsonUtility gives back for an absent nested object is not something to rely on.</summary>
        public SceneMount mount;
        public double receivedSeconds;
    }

    /// <summary>Does the scene carry a mount the player can build? A non-empty key AND a fileDataID.</summary>
    public static bool HasMount(CharacterScene scene)
    {
        return scene != null && scene.mount != null && !string.IsNullOrEmpty(scene.mount.key) && scene.mount.fileDataID > 0;
    }

    /// <summary>The key of the mount a scene names, "" when it names none -- what an answer about the scene
    /// reports as mountKey.</summary>
    public static string MountKeyOf(CharacterScene scene)
    {
        return scene != null && scene.mount != null && !string.IsNullOrEmpty(scene.mount.key) ? scene.mount.key : "";
    }

    /// <summary>The mount part of a characterSceneApplied answer (protocol 5).</summary>
    public struct MountAnswer
    {
        public string Key;      // the scene's mount key, "" for none
        public string Status;   // "applied" | "failed" | "none"
        public string Reason;

        /// <summary>Nothing was put under the character for this scene: it has no mount, or it was not applied.</summary>
        public static MountAnswer None(CharacterScene scene)
        {
            return new MountAnswer { Key = MountKeyOf(scene), Status = "none", Reason = "" };
        }
    }

    public class CharacterImage
    {
        public string hash;
        public string kind;
        public Wmv.Wow.BlpImage image;     // converted to the renderer's RGBA on the reader thread
        public string error;
    }

    [Serializable] class MsgTexture
    {
        public int index;
        public int type;
        public int fileDataID;
        public string source;
    }

    [Serializable] class MsgRider
    {
        public int sequenceIndex;
        public bool playing;
        public int timeMs;
        public float speed;
        public bool loop;
    }

    [Serializable] class MsgStreaming
    {
        public int radius, keepRadius, maxResidentTiles, requestCredit, maxTileLoads;
    }

    [Serializable] class MsgStreamTile
    {
        public int x, y, tileLoad;
        public string refused;
    }

    // Protocol 19: loadTerrainTile's / loadWorld's "liquids" and its catalogue (LiquidsFrom).
    [Serializable] class MsgLiquidType
    {
        public int id;
        public string name;
        public int kind, material, materialFlags, lvf;
        public long minimapColor;     // u32 0xAARRGGBB as stored: past an int's range
        public float float0, float1;
        public int[] frames;
        public int framesWanted;
        public long bump;             // the ripple map's FileDataID (absent: 0)
    }

    [Serializable] class MsgLiquidCatalog
    {
        public string product, build, state, detail;
        public MsgLiquidType[] types;
    }

    // The zone lights inside "liquids" (LightsFrom): flat arrays and no dictionaries, as JsonUtility reads them.
    [Serializable] class MsgLightZone
    {
        public int zone, light, conditional;
        public double zmin, zmax;
        public double[] poly;
    }

    [Serializable] class MsgLightPoint
    {
        public int light;
        public double x, y, z, start, end;
    }

    [Serializable] class MsgLightValues
    {
        public int light, lightParams;
        public float[] riverClose, riverFar, oceanClose, oceanFar, waterAlpha, oceanAlpha;
        public float[] sky;           // optional (absent: empty)
    }

    [Serializable] class MsgLights
    {
        public string state, detail, product, build, defaultFrom;
        public int map, time, slot, defaultLight;
        public MsgLightZone[] zones;
        public MsgLightPoint[] points;
        public MsgLightValues[] values;
    }

    [Serializable] class MsgLiquids
    {
        public int maxTexturesPerLoad, maxTextureBytes, maxTextureBytesPerLoad;
        public MsgLiquidCatalog catalog;
        public MsgLights lights;
    }

    /// <summary>The load's liquid offer (protocol 19), null when the line carries none: a nested object always parses, so its
    /// catalogue's state says whether "liquids" was sent. Each type becomes a LiquidTypeView of the load's catalogue.</summary>
    static LiquidOffer LiquidsFrom(Msg msg)
    {
        MsgLiquids l = msg.liquids;
        if (l == null || l.catalog == null || string.IsNullOrEmpty(l.catalog.state))
            return null;
        var o = new LiquidOffer
        {
            MaxTexturesPerLoad = l.maxTexturesPerLoad, MaxTextureBytes = l.maxTextureBytes, MaxTextureBytesPerLoad = l.maxTextureBytesPerLoad,
            CatalogState = l.catalog.state, CatalogDetail = l.catalog.detail ?? "", CatalogProduct = l.catalog.product ?? "",
            CatalogBuild = l.catalog.build ?? "",
        };
        if (l.catalog.types != null)
            foreach (MsgLiquidType t in l.catalog.types)
            {
                if (t == null)
                    continue;
                var frames = new uint[t.frames != null ? t.frames.Length : 0];
                for (int i = 0; i < frames.Length; i++)
                    frames[i] = (uint)t.frames[i];
                o.FrameIds += frames.Length;
                o.Catalog.Add(new Wmv.Wow.World.LiquidTypeView
                {
                    Id = t.id, Name = t.name ?? "", Kind = t.kind, Material = t.material, MaterialFlags = t.materialFlags, Lvf = t.lvf,
                    MinimapColor = (uint)t.minimapColor, Float0 = t.float0, Float1 = t.float1, Frames = frames, FramesWanted = t.framesWanted,
                    Bump = t.bump > 0 && t.bump <= uint.MaxValue ? (uint)t.bump : 0u,
                });
                if (t.bump > 0 && t.bump <= uint.MaxValue)
                    o.BumpIds++;
            }
        MsgLights lights = l.lights;
        o.LightsState = lights != null && lights.state != null ? lights.state : "";
        o.LightsDetail = lights != null && lights.detail != null ? lights.detail : "";
        o.Lights = LightsFrom(lights);
        return o;
    }

    /// <summary>The offer's zone lights (LiquidLights), null unless their state is "ok" (absent: the nested object parses
    /// with an empty state). Zones with fewer than 3 points or an odd coordinate count, points that are not finite and
    /// value rows with short arrays are dropped and counted (LiquidLights' Add methods).</summary>
    static Wmv.Wow.World.LiquidLights LightsFrom(MsgLights m)
    {
        if (m == null || m.state != "ok")
            return null;
        var lights = new Wmv.Wow.World.LiquidLights
        {
            Product = m.product ?? "", Build = m.build ?? "", Map = m.map, Time = m.time, Slot = m.slot, DefaultLight = m.defaultLight,
            DefaultFrom = m.defaultFrom ?? "",
        };
        if (m.zones != null)
            foreach (MsgLightZone z in m.zones)
                if (z != null)
                    lights.AddZone(z.zone, z.light, z.zmin, z.zmax, z.conditional, z.poly);
                else
                    lights.DroppedZones++;
        if (m.points != null)
            foreach (MsgLightPoint p in m.points)
                if (p != null)
                    lights.AddPoint(p.light, p.x, p.y, p.z, p.start, p.end);
                else
                    lights.DroppedPoints++;
        if (m.values != null)
            foreach (MsgLightValues v in m.values)
                if (v != null)
                    lights.AddValues(v.light, v.lightParams, v.riverClose, v.riverFar, v.oceanClose, v.oceanFar, v.waterAlpha, v.oceanAlpha, v.sky);
                else
                    lights.DroppedValues++;
        return lights;
    }

    /// <summary>loadWorld's streaming offer (protocol 18), null when it carries none (a nested object always parses: its
    /// radius says whether it was sent).</summary>
    static StreamingOffer StreamingFrom(Msg msg)
    {
        MsgStreaming s = msg.streaming;
        if (s == null || s.radius <= 0)
            return null;
        return new StreamingOffer
        {
            Radius = s.radius, KeepRadius = Math.Max(s.radius, s.keepRadius), MaxResidentTiles = s.maxResidentTiles,
            RequestCredit = s.requestCredit, MaxTileLoads = s.maxTileLoads,
        };
    }

    [Serializable] class Msg
    {
        public MsgTexture[] textures;
        public int[] geosets;
        public bool hasGeosets;
        public int[] particleColor;
        public int particleColorId;
        public string type;
        public string requestId;
        public string path;
        public int fileDataID;
        public string client;
        public bool ok;
        public int byteLength;
        public string sha1;
        public string encoding;
        public string data;
        public string error;
        public string message;
        public int sequenceIndex;
        public int animID;
        public int durationMs;
        public bool loop;
        public bool explicitState;
        public double receivedSeconds;
        public bool playing;
        public int timeMs;
        public float speed;
        public double sampledAtMs;    // modelAnimationState: when the host sampled it (ProjectFromSample)
        public bool flight;
        public int revision;
        public bool hasSubmeshVisible;
        public int submeshCount;
        public int[] submeshVisible;
        public bool character;
        public bool keepView;         // loadWoWModel (protocol 8): the model replaces the one on screen without moving the view
        public int load;              // also a ridden mount's animation pushes (protocol 5)
        public int query;             // runtimeState
        public int request;           // captureScreenshot (protocol 6); its path, width and height are the fields above and below
        public int r, g, b;           // viewportBackground (protocol 7): the sRGB bytes the background displays as
        // a ridden mount's animation pushes (protocol 5)
        public string role;
        public bool hasRider;
        public MsgRider rider;
        // characterImage
        public string hash;
        public string kind;           // also loadWoWModel's "m2" / "wmo" -- the same wire name
        public int width;
        public int height;
        public string format;
        // characterScene
        public bool attachmentsOnly;
        public SceneBody body;
        public SceneMerged[] merged;
        public SceneAttachment[] attachments;
        public SceneMount mount;
        // protocol 9: the asset cache
        public int assetEpoch;        // loadWoWModel, prefetchAssets: the host's client epoch (0 from an older host)
        public bool cacheable;        // assetResponse: a file of the client's own storage
        public int[] fileDataIDs;     // prefetchAssets
        // protocol 12: terrain tiles (loadTerrainTile, clearTerrain) and the map-file answer (assetResponse)
        public int mapId;
        public string mapDirectory;
        public string product;
        public string build;
        public int wdtFileDataID;
        public string wdtResolvedBy;
        public string wdtPath;
        public int tileX;
        public int tileY;
        public int maxAssetBytes;
        public int maxTextureBytes;           // protocol 13: loadTerrainTile's texture limits (0 when absent)
        public int maxTextureBytesPerLoad;
        public int maxTexturesPerLoad;
        public string reason;         // clearTerrain
        public int inspection;        // inspectMapTiles (protocol 14)
        public string view;           // setTerrainView (protocol 14)
        // protocol 15: the world view's camera (setWorldCamera, and loadTerrainTile's optional camera); flat, not a
        // nested object, for the reason HasMount gives
        public int cameraRequest;
        public string cameraMode;
        public double cameraX, cameraY, cameraZ;
        public bool cameraHasZ;
        public float cameraYaw, cameraPitch, cameraEyeHeight;
        public bool cameraStart;      // protocol 16: the world model's start point
        // protocol 16: loadWorld's world models and budgets, and inspectWorldModel's record (flat, as the camera)
        public bool worldModels;
        public bool globalWmo;
        public int maxWmoFileBytes, maxWmoBytesPerLoad, maxWmoTextureBytes, maxWmoTextureBytesPerLoad, maxWmoTexturesPerLoad;
        public int maxWmoModelsPerLoad, maxWmoGroupsPerLoad, maxWmoPlacementsPerLoad;
        // protocol 17: loadWorld's doodad budgets
        public bool doodads;
        public int maxM2FileBytes, maxM2BytesPerLoad, maxM2TextureBytes, maxM2TextureBytesPerLoad, maxM2ModelsPerLoad, maxM2TexturesPerLoad;
        public int maxDoodadInstancesPerLoad;
        public int wmoFileDataID;
        public float[] wmoPosition;
        public float[] wmoRotation;
        public float[] wmoExtentsMin;
        public float[] wmoExtentsMax;
        public int wmoFlags;
        public int wmoScale;
        public long wmoUniqueId;
        public string state;          // assetResponse to getMapAsset: the host's decision
        // protocol 18: loadWorld's streaming offer (radius 0: none), and streamTileAnswer's lists
        public MsgStreaming streaming;
        public bool light;            // runtimeState (protocol 18): skip the whole-player object scans
        public double[] flightPoints;  // testFlight (protocol 18, a test hook); its number is "request" above
        public double flightSpeed;
        public float flightPitch;
        public MsgStreamTile[] opened;
        public MsgStreamTile[] refused;
        public int[] closed;
        // protocol 19: loadTerrainTile's / loadWorld's liquid offer and catalogue
        public MsgLiquids liquids;
        public string detail;
        public double readMs;
        public string md5;
        // protocol 10: the raw bytes that follow this line (an assetResponse's file, a characterImage's pixels)
        public int payloadBytes;
        [NonSerialized] public byte[] payload;
        [NonSerialized] public CharacterImage decodedImage;
        [NonSerialized] public byte[] assetBytes;     // assetResponse: decoded on the reader thread, or the cache's bytes
        [NonSerialized] public string decodeError;    // assetResponse: why there are no bytes (a base64 that did not
                                                      // decode, a binary answer without its payload)
        [NonSerialized] public bool fromCache;        // assetResponse: made here, from the asset cache
    }

    int port = -1;
    TcpClient client;
    NetworkStream stream;
    Thread thread;
    volatile bool stopping;
    volatile bool connected;
    volatile bool viewerGone;   // the connection to the viewer ended (or never came): Update quits
    readonly object sendLock = new object();
    readonly Queue<Msg> inbox = new Queue<Msg>();
    // The session asset cache (protocol 9): main thread only, like everything but the socket read.
    readonly WmvAssetCache cache = new WmvAssetCache();
    // Its answers, handed out in Update after the host's messages, within LocalAnswerBudgetMs a frame (at least one):
    // a load the cache answers whole would otherwise parse its model, skin and textures and build the body in one
    // frame, and the model on screen would stand still for all of it.
    readonly Queue<Msg> localAnswers = new Queue<Msg>();
    const double LocalAnswerBudgetMs = 8.0;
    // THE INBOX's budget (stage 08, docs/world-viewer/08-streaming.md 1.5): the host's messages are dispatched in order, at least
    // one a frame, then as many as fit InboxBudgetMs; the rest wait for the next frame. Counted for runtimeState.
    public const double InboxBudgetMs = 8.0;
    public static int InboxDepthMax;            // the most messages waiting at a frame's start
    /// <summary>The runtimeState being answered was asked "light" (protocol 18): its whole-player object scans are skipped.</summary>
    public static bool RuntimeStateLight;
    public static long InboxFramesDeferred;     // frames that left messages for the next

    /// <summary>The asset cache, for the self-test and the log.</summary>
    public WmvAssetCache Cache { get { return cache; } }

    /// <summary>
    /// One clock for "when did this message arrive" and "what time is it now", readable from
    /// any thread (Unity's Time is main-thread only). A message is stamped on the reader thread
    /// the moment it is parsed, so a state that waits in the inbox through a long frame -- the
    /// frame a big model is parsed or its textures decoded in -- is still projected from when
    /// the app sent it, not from when the main thread got round to it.
    /// </summary>
    static readonly System.Diagnostics.Stopwatch clock = System.Diagnostics.Stopwatch.StartNew();
    public static double NowSeconds { get { return clock.Elapsed.TotalSeconds; } }

    [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long count);
    [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long frequency);

    /// <summary>The system's performance counter in milliseconds -- the clock the host stamps a playback state with
    /// (sampledAtMs); this stopwatch counts from another start. 0 where it cannot be read.</summary>
    public static double PerformanceCounterMs()
    {
        try
        {
            long count, frequency;
            if (QueryPerformanceCounter(out count) && QueryPerformanceFrequency(out frequency) && frequency > 0)
                return count * 1000.0 / frequency;
        }
        catch (Exception) { }   // no such library on this platform
        return 0.0;
    }

    /// <summary>
    /// A playback state is read here, on the reader thread, only after every line the host sent before it -- and a
    /// composited body image sent with a customization is megabytes of base64, read and decoded first (measured: a
    /// state 126 ms old by the time it was read). Positions applied as sent were that wait behind the host, and the
    /// animator, whose own clock had kept running, snapped back to them. So a running clock's position is moved on by
    /// the time since the host sampled it, at its speed, as the host's own clock has moved on meanwhile; from here on
    /// the state is projected from when it was read (receivedSeconds), as before. A state without sampledAtMs (an
    /// older host), or with an implausible wait, is left as sent.
    /// </summary>
    static void ProjectFromSample(Msg msg)
    {
        if (msg.type != "modelAnimationState" || msg.sampledAtMs <= 0.0)
            return;
        double waitedMs = PerformanceCounterMs() - msg.sampledAtMs;
        if (waitedMs <= 0.0 || waitedMs > 10000.0)
            return;
        if (msg.playing)
            msg.timeMs += (int)Math.Round(waitedMs * Math.Max(msg.speed, 0f));
        if (msg.hasRider && msg.rider != null && msg.rider.playing)
            msg.rider.timeMs += (int)Math.Round(waitedMs * Math.Max(msg.rider.speed, 0f));
    }
    readonly Queue<string> statusQueue = new Queue<string>();
    int nextRequestId = 1;
    int nextMapRequestId = 1;

    void Start()
    {
        var args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length - 1; i++)
            if (args[i] == "-wmvPort" && int.TryParse(args[i + 1], out var p))
                port = p;

        if (port <= 0)
        {
            Status("No -wmvPort on the command line -- running standalone (no WMV connection).");
            return;
        }

        thread = new Thread(ConnectAndReadLoop) { IsBackground = true };
        thread.Start();
    }

    void ConnectAndReadLoop()
    {
        Status("Connecting to WMV on 127.0.0.1:" + port + " ...");
        try
        {
            client = new TcpClient();
            client.NoDelay = true;
            client.Connect("127.0.0.1", port);
            stream = client.GetStream();
            connected = true;
            Status("Connected to WMV (127.0.0.1:" + port + ")");

            Send("{\"type\":\"unityReady\",\"protocolVersion\":" + ProtocolVersion + "}");

            using (Stream readStream = stream)
            {
                var reader = new WmvStreamReader(readStream);
                Msg msg;
                while (!stopping && (msg = NextMessage(reader)) != null)
                    lock (inbox) inbox.Enqueue(msg);
            }
        }
        catch (Exception e)
        {
            Status("WMV connection failed/closed: " + e.Message);
        }
        // The using above disposed the NetworkStream -- stop Send from touching it.
        lock (sendLock) stream = null;
        connected = false;
        Status("Disconnected from WMV");
        viewerGone = !stopping;
    }

    void Update()
    {
        // status lines first (they may have been queued from the socket thread)
        while (true)
        {
            string s;
            lock (statusQueue)
            {
                if (statusQueue.Count == 0) break;
                s = statusQueue.Dequeue();
            }
            Debug.Log("WMV IPC: " + s);
            OnStatus?.Invoke(s);
        }

        // The player draws only for the viewer that started it and never connects again: once that connection has
        // ended (the viewer exited, crashed or was ended), quit instead of rendering on for no one.
        if (viewerGone)
        {
            viewerGone = false;
            Debug.Log("WMV IPC: the viewer's connection ended -- quitting");
            Application.Quit();
            return;
        }

        var inboxClock = System.Diagnostics.Stopwatch.StartNew();
        lock (inbox)
            InboxDepthMax = Math.Max(InboxDepthMax, inbox.Count);
        while (true)
        {
            Msg msg;
            lock (inbox)
            {
                if (inbox.Count == 0) break;
                msg = inbox.Dequeue();
            }
            try { Dispatch(msg); }
            catch (Exception e) { Debug.LogWarning("WMV IPC: handler failed: " + e.Message); }
            if (inboxClock.Elapsed.TotalMilliseconds >= InboxBudgetMs)
            {
                lock (inbox)
                    if (inbox.Count > 0) InboxFramesDeferred++;
                break;
            }
        }

        // The asset cache's answers, after the host's messages of this frame: at least one, then as many as fit the
        // budget (an answer that asks for another cached file queues it here, for this frame or the next).
        var clock = System.Diagnostics.Stopwatch.StartNew();
        while (localAnswers.Count > 0)
        {
            Msg answer = localAnswers.Dequeue();
            try { Dispatch(answer); }
            catch (Exception e) { Debug.LogWarning("WMV IPC: handler failed: " + e.Message); }
            if (clock.Elapsed.TotalMilliseconds >= LocalAnswerBudgetMs)
                break;
        }
    }

    /// <summary>
    /// The next message from the host (reader thread), or null at the end of the stream: a line parsed, then the
    /// payload it announced read -- also after a line whose JSON did not parse, which is then skipped, so the stream
    /// stays in step -- and a characterImage's pixels or an assetResponse's bytes made ready here, so the frame the
    /// message lands in only swaps a reference (a composited image is 8 MB, a character model tens of megabytes).
    /// </summary>
    Msg NextMessage(WmvStreamReader reader)
    {
        string line;
        while ((line = reader.ReadLine()) != null)
        {
            if (line.Trim().Length == 0) continue;
            Msg msg = null;
            try { msg = JsonUtility.FromJson<Msg>(line); }
            catch (Exception e) { Debug.LogWarning("WMV IPC: bad JSON line: " + e.Message); }
            int payloadBytes = msg != null ? msg.payloadBytes : WmvStreamReader.PayloadBytesOf(line);
            byte[] payload = payloadBytes > 0 ? reader.ReadPayload(payloadBytes) : null;
            if (msg == null)
                continue;
            Prepare(msg, payload);
            return msg;
        }
        return null;
    }

    /// <summary>What the reader thread does with a parsed line and the payload it announced: stamped, projected, and a
    /// characterImage's pixels or an assetResponse's bytes made ready (NextMessage and FeedLineForTest alike).</summary>
    static void Prepare(Msg msg, byte[] payload)
    {
        msg.payload = payload;
        msg.receivedSeconds = NowSeconds;
        ProjectFromSample(msg);
        if (msg.type == "characterImage")
        {
            msg.decodedImage = DecodeCharacterImage(msg);
            msg.data = null;
        }
        else if (msg.type == "assetResponse")
            DecodeAsset(msg);
    }

    /// <summary>What NextMessage made of a message, for the self-test (Msg itself stays private).</summary>
    internal class ReadForTest
    {
        public string Type, DecodeError, Encoding;
        public int PayloadBytes;
        public byte[] AssetBytes;
        public CharacterImage Image;
    }

    internal ReadForTest NextMessageForTest(WmvStreamReader reader)
    {
        Msg m = NextMessage(reader);
        return m == null ? null : new ReadForTest
        {
            Type = m.type, DecodeError = m.decodeError, Encoding = m.encoding, PayloadBytes = m.payloadBytes,
            AssetBytes = m.assetBytes, Image = m.decodedImage,
        };
    }

    /// <summary>An assetResponse's file as bytes (reader thread): its binary payload (protocol 10), or its base64
    /// decoded; the text is dropped either way. decodeError is the whole reason when there are no bytes.</summary>
    static void DecodeAsset(Msg msg)
    {
        if (msg.ok && msg.encoding == "binary")
        {
            msg.assetBytes = msg.payload;
            if (msg.assetBytes == null)
                msg.decodeError = "binary assetResponse without its payload";
        }
        else if (msg.ok && msg.encoding == "base64" && !string.IsNullOrEmpty(msg.data))
        {
            try { msg.assetBytes = Convert.FromBase64String(msg.data); }
            catch (Exception e)   // a bad payload or no memory for it: this answer fails, the connection stays
            {
                msg.assetBytes = null;
                msg.decodeError = "base64 decode failed: " + e.GetType().Name + ": " + e.Message;
            }
        }
        msg.data = null;
    }

    /// <summary>
    /// A characterImage payload as the renderer's image: B,G,R,A bytes become R,G,B,A, rows stay top
    /// first (BlpImage's order). The alpha is whatever the host composited -- a premultiplied image
    /// is taken as it is, exactly as the OpenGL upload took it.
    /// </summary>
    static CharacterImage DecodeCharacterImage(Msg msg)
    {
        var result = new CharacterImage { hash = msg.hash ?? "", kind = msg.kind ?? "" };
        try
        {
            bool binary = msg.encoding == "binary" && msg.payload != null;
            if (msg.format != "bgra8" || (!binary && (msg.encoding != "base64" || string.IsNullOrEmpty(msg.data))))
            {
                result.error = "unsupported characterImage (" + msg.encoding + ", " + msg.format + ")";
                return result;
            }
            // Its pixels: the payload (protocol 10, swizzled in place: this message owns it) or the base64 decoded.
            byte[] bytes = binary ? msg.payload : Convert.FromBase64String(msg.data);
            if (msg.width <= 0 || msg.height <= 0 || bytes.Length != msg.width * msg.height * 4)
            {
                result.error = string.Format("characterImage {0}x{1} carries {2} bytes", msg.width, msg.height, bytes.Length);
                return result;
            }
            bool alpha = false;
            for (int i = 0; i < bytes.Length; i += 4)
            {
                byte b = bytes[i];
                bytes[i] = bytes[i + 2];
                bytes[i + 2] = b;
                if (bytes[i + 3] != 255) alpha = true;
            }
            result.image = new Wmv.Wow.BlpImage
            {
                Width = msg.width, Height = msg.height, Rgba = bytes, Encoding = "host composite", HasAlpha = alpha,
            };
        }
        catch (Exception e)
        {
            result.error = "characterImage decode failed: " + e.Message;
        }
        return result;
    }

    static ModelTexturesResponse ReadTextures(Msg msg)
    {
        var r = new ModelTexturesResponse
        {
            requestId = msg.requestId, ok = msg.ok, fileDataID = msg.fileDataID, error = msg.error,
        };
        if (msg.textures != null)
        {
            r.textures = new ModelTextureRef[msg.textures.Length];
            for (int i = 0; i < msg.textures.Length; i++)
                r.textures[i] = new ModelTextureRef
                {
                    index = msg.textures[i].index,
                    type = msg.textures[i].type,
                    fileDataID = msg.textures[i].fileDataID,
                    source = msg.textures[i].source ?? "",
                };
        }
        r.hasGeosets = msg.hasGeosets;
        if (msg.geosets != null) r.geosets = msg.geosets;
        if (msg.particleColor != null) r.particleColor = msg.particleColor;
        r.particleColorId = msg.particleColorId;
        r.hasSubmeshVisible = msg.hasSubmeshVisible;
        r.submeshCount = msg.submeshCount;
        r.submeshVisible = msg.hasSubmeshVisible ? ToBools(msg.submeshVisible) : null;
        return r;
    }

    static CharacterScene ToCharacterScene(Msg msg)
    {
        return new CharacterScene
        {
            fileDataID = msg.fileDataID,
            revision = msg.revision,
            body = msg.body ?? new SceneBody(),
            attachmentsOnly = msg.attachmentsOnly,
            load = msg.load,
            merged = msg.merged ?? new SceneMerged[0],
            attachments = msg.attachments ?? new SceneAttachment[0],
            mount = msg.mount,
            receivedSeconds = msg.receivedSeconds,
        };
    }

    /// <summary>One characterScene line, parsed exactly as the reader thread and Dispatch parse it; null for
    /// any other line. For the lifecycle self-test, which checks what a line without a mount comes back as.</summary>
    public static CharacterScene ParseCharacterScene(string line)
    {
        Msg msg = JsonUtility.FromJson<Msg>(line);
        return msg != null && msg.type == "characterScene" ? ToCharacterScene(msg) : null;
    }

    static AnimationSelection ToAnimationSelection(Msg msg)
    {
        return new AnimationSelection
        {
            fileDataID = msg.fileDataID,
            sequenceIndex = msg.sequenceIndex,
            animID = msg.animID,
            durationMs = msg.durationMs,
            loop = msg.loop,
            role = msg.role ?? "",
            load = msg.load,
            receivedSeconds = msg.receivedSeconds,
        };
    }

    /// <summary>A modelAnimationState as the handlers take it. The rider is there when the line says so (hasRider),
    /// never judged by whether JsonUtility gave the nested object back.</summary>
    static AnimationState ToAnimationState(Msg msg)
    {
        var s = new AnimationState
        {
            fileDataID = msg.fileDataID,
            sequenceIndex = msg.sequenceIndex,
            playing = msg.playing,
            timeMs = msg.timeMs,
            speed = msg.speed,
            loop = msg.loop,
            explicitState = msg.explicitState,
            flight = msg.flight,
            flightSpeed = (float)msg.flightSpeed,
            receivedSeconds = msg.receivedSeconds,
            load = msg.load,
            hasRider = msg.hasRider && msg.rider != null,
        };
        if (s.hasRider)
            s.rider = new RiderPlayback
            {
                sequenceIndex = msg.rider.sequenceIndex, playing = msg.rider.playing, timeMs = msg.rider.timeMs,
                speed = msg.rider.speed, loop = msg.rider.loop,
            };
        return s;
    }

    /// <summary>One viewportBackground line, checked as Dispatch checks it; false for any other line and for a channel
    /// outside 0..255. For the lifecycle self-test.</summary>
    public static bool ParseViewportBackground(string line, out Color32 colour)
    {
        Msg msg = JsonUtility.FromJson<Msg>(line);
        bool ok = msg != null && msg.type == "viewportBackground" && IsByte(msg.r) && IsByte(msg.g) && IsByte(msg.b);
        colour = ok ? new Color32((byte)msg.r, (byte)msg.g, (byte)msg.b, 255) : new Color32(0, 0, 0, 0);
        return ok;
    }

    static bool IsByte(int v) { return v >= 0 && v <= 255; }

    /// <summary>The colour of the "-wmvBackground RRGGBB" launch argument: exactly six hex digits, either case, an
    /// optional leading '#'. False (and opaque black) for anything else.</summary>
    public static bool ParseBackgroundArgument(string text, out Color32 colour)
    {
        colour = new Color32(0, 0, 0, 255);
        if (text == null) return false;
        if (text.StartsWith("#")) text = text.Substring(1);
        if (text.Length != 6) return false;
        foreach (char c in text)
            if (!Uri.IsHexDigit(c)) return false;
        int v = Convert.ToInt32(text, 16);
        colour = new Color32((byte)(v >> 16), (byte)(v >> 8), (byte)v, 255);
        return true;
    }

    /// <summary>One modelAnimation line, parsed as Dispatch parses it; false for any other line. For the lifecycle
    /// self-test.</summary>
    public static bool ParseAnimationSelection(string line, out AnimationSelection selection)
    {
        Msg msg = JsonUtility.FromJson<Msg>(line);
        bool ok = msg != null && msg.type == "modelAnimation";
        selection = ok ? ToAnimationSelection(msg) : new AnimationSelection { role = "" };
        return ok;
    }

    /// <summary>One modelAnimationState line, parsed as Dispatch parses it; false for any other line. For the lifecycle
    /// self-test, which checks the nested rider and what a line without one comes back as.</summary>
    public static bool ParseAnimationState(string line, out AnimationState state)
    {
        Msg msg = JsonUtility.FromJson<Msg>(line);
        bool ok = msg != null && msg.type == "modelAnimationState";
        state = ok ? ToAnimationState(msg) : new AnimationState();
        return ok;
    }

    /// <summary>The wire carries 0/1 so the line stays short; JsonUtility gives an absent array
    /// back as null or empty, so both come out as an empty list.</summary>
    static bool[] ToBools(int[] values)
    {
        if (values == null) return new bool[0];
        var result = new bool[values.Length];
        for (int i = 0; i < values.Length; i++) result[i] = values[i] != 0;
        return result;
    }

    void Dispatch(Msg msg)
    {
        switch (msg.type)
        {
            case "loadWoWModel":
                // The files of the two loads before this one stay (a character switched back and forth); older ones go.
                // A host before protocol 9 states no epoch, and nothing is kept.
                cache.SetEpoch(msg.assetEpoch);
                cache.BeginGeneration(msg.fileDataID);
                // An absent kind is an M2: that is what every host before protocol 4 meant.
                OnLoadWoWModel?.Invoke(msg.path ?? "", msg.fileDataID, msg.client ?? "active", msg.character, msg.load,
                                       string.IsNullOrEmpty(msg.kind) ? "m2" : msg.kind, msg.keepView);
                break;

            case "characterImage":
                OnCharacterImage?.Invoke(msg.decodedImage ?? new CharacterImage { hash = msg.hash, error = "not decoded" });
                break;

            case "characterScene":
                OnCharacterScene?.Invoke(ToCharacterScene(msg));
                break;

            case "modelTextures":
                OnModelTextures?.Invoke(ReadTextures(msg));
                break;

            case "runtimeState":
                RuntimeStateLight = msg.light;
                OnRuntimeState?.Invoke(msg.query);
                break;

            // A terrain tile (protocol 12). The epoch is the cache's, as a model load states it; no generation begins: the
            // model on screen stays (hidden), and its files are not aged out by a load that is not a model's.
            case "loadTerrainTile":
                cache.SetEpoch(msg.assetEpoch);
                OnLoadTerrainTile?.Invoke(new TerrainTileCommand
                {
                    Load = msg.load, AssetEpoch = msg.assetEpoch, MapId = msg.mapId, MapDirectory = msg.mapDirectory ?? "",
                    Product = msg.product ?? "", Build = msg.build ?? "", WdtFileDataID = msg.wdtFileDataID,
                    WdtResolvedBy = msg.wdtResolvedBy ?? "", WdtPath = msg.wdtPath ?? "", TileX = msg.tileX, TileY = msg.tileY,
                    MaxAssetBytes = msg.maxAssetBytes, MaxTextureBytes = msg.maxTextureBytes,
                    MaxTextureBytesPerLoad = msg.maxTextureBytesPerLoad, MaxTexturesPerLoad = msg.maxTexturesPerLoad,
                    Camera = CameraFrom(msg), Liquids = LiquidsFrom(msg),
                });
                break;

            // A world (protocol 16): what loadTerrainTile carries, and the world models -- the tile's MODF records, or a WMO-only
            // map's global world model with no tile -- within the budgets the host states.
            case "loadWorld":
                cache.SetEpoch(msg.assetEpoch);
                OnLoadTerrainTile?.Invoke(new TerrainTileCommand
                {
                    Load = msg.load, AssetEpoch = msg.assetEpoch, MapId = msg.mapId, MapDirectory = msg.mapDirectory ?? "",
                    Product = msg.product ?? "", Build = msg.build ?? "", WdtFileDataID = msg.wdtFileDataID,
                    WdtResolvedBy = msg.wdtResolvedBy ?? "", WdtPath = msg.wdtPath ?? "", TileX = msg.globalWmo ? -1 : msg.tileX,
                    TileY = msg.globalWmo ? -1 : msg.tileY, MaxAssetBytes = msg.maxAssetBytes, MaxTextureBytes = msg.maxTextureBytes,
                    MaxTextureBytesPerLoad = msg.maxTextureBytesPerLoad, MaxTexturesPerLoad = msg.maxTexturesPerLoad,
                    Camera = CameraFrom(msg), World = true, GlobalWmo = msg.globalWmo, MaxWmoFileBytes = msg.maxWmoFileBytes,
                    MaxWmoBytesPerLoad = msg.maxWmoBytesPerLoad, MaxWmoTextureBytes = msg.maxWmoTextureBytes,
                    MaxWmoTextureBytesPerLoad = msg.maxWmoTextureBytesPerLoad, MaxWmoTexturesPerLoad = msg.maxWmoTexturesPerLoad,
                    MaxWmoModelsPerLoad = msg.maxWmoModelsPerLoad, MaxWmoGroupsPerLoad = msg.maxWmoGroupsPerLoad,
                    MaxWmoPlacementsPerLoad = msg.maxWmoPlacementsPerLoad,
                    Doodads = msg.doodads, MaxM2FileBytes = msg.maxM2FileBytes, MaxM2BytesPerLoad = msg.maxM2BytesPerLoad,
                    MaxM2TextureBytes = msg.maxM2TextureBytes, MaxM2TextureBytesPerLoad = msg.maxM2TextureBytesPerLoad,
                    MaxM2ModelsPerLoad = msg.doodads ? msg.maxM2ModelsPerLoad : 0, MaxM2TexturesPerLoad = msg.maxM2TexturesPerLoad,
                    MaxDoodadInstancesPerLoad = msg.maxDoodadInstancesPerLoad, Streaming = StreamingFrom(msg), Liquids = LiquidsFrom(msg),
                });
                break;

            case "testFlight":
                OnTestFlight?.Invoke(new TestFlight
                {
                    Request = msg.request, Points = msg.flightPoints ?? new double[0], Speed = msg.flightSpeed, Pitch = msg.flightPitch,
                });
                break;

            // Protocol 18: the tile loads of a streaming session.
            case "streamTileAnswer":
                {
                    var a = new StreamTileAnswer { Load = msg.load, AssetEpoch = msg.assetEpoch };
                    if (msg.opened != null)
                        foreach (MsgStreamTile t in msg.opened)
                            a.Opened.Add(new StreamTileOpened { X = t.x, Y = t.y, TileLoad = t.tileLoad });
                    if (msg.refused != null)
                        foreach (MsgStreamTile t in msg.refused)
                            a.Refused.Add(new StreamTileOpened { X = t.x, Y = t.y, Refused = t.refused ?? "" });
                    if (msg.closed != null)
                        a.Closed.AddRange(msg.closed);
                    OnStreamTileAnswer?.Invoke(a);
                }
                break;

            case "inspectWorldModel":
                OnInspectWorldModel?.Invoke(new WorldModelRequest
                {
                    Inspection = msg.inspection, MapId = msg.mapId, WmoFileDataID = msg.wmoFileDataID, AssetEpoch = msg.assetEpoch,
                    Md5 = msg.md5 ?? "", Bytes = msg.payload, Encoding = msg.encoding ?? "",
                    Position = Floats3(msg.wmoPosition), Rotation = Floats3(msg.wmoRotation), ExtentsMin = Floats3(msg.wmoExtentsMin),
                    ExtentsMax = Floats3(msg.wmoExtentsMax), Flags = msg.wmoFlags, ScaleRaw = msg.wmoScale,
                    UniqueId = (uint)msg.wmoUniqueId,
                });
                break;

            case "clearTerrain":
                OnClearTerrain?.Invoke(msg.reason ?? "");
                break;

            // The Maps viewer (protocol 14): a WDT's tiles, and the view terrain is drawn in.
            case "inspectMapTiles":
                OnInspectMapTiles?.Invoke(new MapTilesRequest
                {
                    Inspection = msg.inspection, MapId = msg.mapId, WdtFileDataID = msg.wdtFileDataID, AssetEpoch = msg.assetEpoch,
                    Md5 = msg.md5 ?? "", Bytes = msg.payload, Encoding = msg.encoding ?? "",
                });
                break;

            case "setTerrainView":
                OnSetTerrainView?.Invoke(msg.view ?? "");
                break;

            // The world view's camera (protocol 15). A line without a mode is still answered (refused), never dropped.
            case "setWorldCamera":
                OnSetWorldCamera?.Invoke(CameraFrom(msg) ?? new WorldCameraRequest { Request = msg.cameraRequest });
                break;

            case "captureScreenshot":
                OnCaptureScreenshot?.Invoke(new ScreenshotRequest
                {
                    request = msg.request, path = msg.path ?? "", width = msg.width, height = msg.height,
                });
                break;

            // The Models viewport's background (protocol 7). A channel outside 0..255 is refused rather than clamped:
            // only the host sends this, so a bad value is a fault to see in the log, not a colour to guess at.
            case "viewportBackground":
                if (IsByte(msg.r) && IsByte(msg.g) && IsByte(msg.b))
                    OnViewportBackground?.Invoke(new Color32((byte)msg.r, (byte)msg.g, (byte)msg.b, 255));
                else
                    Debug.LogWarning("WMV IPC: viewportBackground refused, a channel is outside 0..255: " + msg.r + "," +
                                     msg.g + "," + msg.b);
                break;

            // Unsolicited: the skin on display in WMV changed. Same payload as a modelTextures
            // reply, minus the requestId -- nothing asked for it.
            case "modelSkin":
                OnModelSkin?.Invoke(ReadTextures(msg));
                break;

            case "modelGeosets":
                OnModelGeosets?.Invoke(new GeosetVisibility
                {
                    fileDataID = msg.fileDataID,
                    revision = msg.revision,
                    submeshCount = msg.submeshCount,
                    visible = ToBools(msg.submeshVisible),
                });
                break;

            case "modelAnimationState":
                OnModelAnimationState?.Invoke(ToAnimationState(msg));
                break;

            case "modelAnimation":
                OnModelAnimation?.Invoke(ToAnimationSelection(msg));
                break;

            case "assetResponse":
            {
                var r = new AssetResponse
                {
                    requestId = msg.requestId, ok = msg.ok, path = msg.path, fileDataID = msg.fileDataID,
                    byteLength = msg.byteLength, sha1 = msg.sha1, error = msg.error, fromCache = msg.fromCache,
                    role = msg.role ?? "", load = msg.load, assetEpoch = msg.assetEpoch, state = msg.state ?? "",
                    detail = msg.detail ?? "", readMs = msg.readMs, md5 = msg.md5 ?? "",
                };
                if (msg.ok)
                {
                    // Decoded on the reader thread, or the cache's own bytes. JsonUtility materialises absent string
                    // fields as "" (never null).
                    if (msg.assetBytes != null)
                        r.data = msg.assetBytes;
                    else
                    {
                        // Attributed to this request instead of orphaning it.
                        r.ok = false;
                        r.error = !string.IsNullOrEmpty(msg.decodeError) ? msg.decodeError
                                : string.IsNullOrEmpty(msg.encoding) ? "assetResponse without encoding/data"
                                : "unsupported encoding: " + msg.encoding;
                    }
                }
                // A map file's answer (getMapAsset, protocol 12) goes to OnMapAssetResponse and nowhere else: not to the
                // asset cache (no flight of it exists, and none of another request's may end by it) and not to
                // OnAssetResponse -- so an answer arriving after its terrain load ended can never be read by a model,
                // character or world-model handler. Without a handler it is dropped.
                if (IsMapAnswer(msg.requestId, msg.role))
                {
                    MapAnswersRouted++;
                    if (OnMapAssetResponse != null)
                        OnMapAssetResponse(r);
                    else
                    {
                        MapAnswersUnhandled++;
                        Debug.LogWarning("WMV IPC: map answer " + r.requestId + " (" + (string.IsNullOrEmpty(r.role) ? "no role" : r.role) +
                                         ", load " + r.load + ") has no handler -- dropped");
                    }
                    break;
                }
                AnswerAsset(r, msg.fromCache ? null : cache.EndFlight(msg.requestId), msg.cacheable);
                break;
            }

            // The models the host expects next (protocol 9): fetched now, while nothing waits for them.
            case "prefetchAssets":
                cache.SetEpoch(msg.assetEpoch);
                if (msg.fileDataIDs != null)
                    foreach (int id in msg.fileDataIDs)
                        Prefetch(id, WmvAssetCache.FileKind.Model);
                break;

            default:
                Debug.LogWarning("WMV IPC: unknown message type '" + msg.type + "'");
                break;
        }
    }

    // ---- requests to WMV ----

    // Every asset request goes through these. An answer comes back through OnAssetResponse with the id returned, never
    // before the call has returned: one the asset cache gives is queued for Update like an answer from the host.
    // cacheable false keeps a request out of the cache (a world model's files: hundreds of megabytes, loaded once).

    public string RequestAsset(string path) { return RequestAsset(path, 0, true); }

    public string RequestAsset(string path, bool cacheable) { return RequestAsset(path, 0, cacheable); }

    /// <summary>A file by its path, asked for by path; knownFileDataID (0 for none) is the FileDataID the host stated
    /// for it, so the cache finds it -- and a prefetch of it still arriving -- by that.</summary>
    public string RequestAsset(string path, int knownFileDataID, bool cacheable)
    {
        var id = NewRequestId();
        if (cacheable && AnsweredHere(id, knownFileDataID > 0 ? knownFileDataID : 0, path))
            return id;
        Send("{\"type\":\"getAsset\",\"requestId\":\"" + id + "\",\"path\":\"" + Escape(path) + "\"}");
        return id;
    }

    public string RequestAssetByFileDataID(int fileDataID) { return RequestAssetByFileDataID(fileDataID, true); }

    public string RequestAssetByFileDataID(int fileDataID, bool cacheable)
    {
        var id = NewRequestId();
        if (cacheable && AnsweredHere(id, fileDataID, null))
            return id;
        SendAssetRequest(id, fileDataID);
        return id;
    }

    void SendAssetRequest(string id, int fileDataID)
    {
        Send("{\"type\":\"getAssetByFileDataID\",\"requestId\":\"" + id + "\",\"fileDataID\":" + fileDataID + "}");
    }

    /// <summary>
    /// A request the asset cache answers -- from the files it keeps (queued for Update) or by joining the same request
    /// already out -- returns true and sends nothing. Otherwise the request goes out as a flight others can join.
    /// </summary>
    bool AnsweredHere(string id, int fileDataID, string path)
    {
        if (!cache.Enabled)
            return false;
        WmvAssetCache.Entry held = cache.Take(fileDataID, path);
        if (held != null)
        {
            var msg = new Msg
            {
                type = "assetResponse", requestId = id, ok = true, fileDataID = held.FileDataID, path = held.Path ?? "",
                byteLength = held.Data.Length, assetBytes = held.Data, fromCache = true,
            };
            localAnswers.Enqueue(msg);
            return true;
        }
        if (cache.Join(fileDataID, path, id))
            return true;
        cache.BeginFlight(fileDataID, path, id, WmvAssetCache.FileKind.Other, false);
        return false;
    }

    /// <summary>
    /// An asset answer to whoever asked: the request it answers and every request that joined it while it was out. A
    /// prefetch's answer goes to nobody but the requests that joined it: it fills the cache and fetches the files its
    /// model needs with it. A prefetch that failed is asked again for the first request that joined it.
    /// </summary>
    void AnswerAsset(AssetResponse r, WmvAssetCache.Flight flight, bool cacheable)
    {
        if (flight != null && r.ok && cacheable)
            cache.Store(flight, r.fileDataID, r.path, r.data);
        if (flight != null && flight.Prefetch && !r.ok && flight.Waiters.Count > 0)
        {
            Debug.Log("WMV: asset cache: prefetch of " + flight.FileDataID + " failed (" + r.error + ") -- asked again for the load that joined it");
            string first = flight.Waiters[0];
            WmvAssetCache.Flight again = cache.BeginFlight(flight.FileDataID, flight.Path, first, flight.Kind, false);
            for (int i = 1; i < flight.Waiters.Count; i++)
                again.Waiters.Add(flight.Waiters[i]);
            SendAssetRequest(first, flight.FileDataID);
            return;
        }
        if (flight == null || !flight.Prefetch)
            OnAssetResponse?.Invoke(r);
        if (flight == null)
            return;
        foreach (string waiter in flight.Waiters)
            OnAssetResponse?.Invoke(r.For(waiter));
        if (flight.Prefetch && r.ok)
            ContinuePrefetch(flight.Kind, r.data);
    }

    /// <summary>
    /// Fetch a file into the asset cache (protocol 9 prefetchAssets), and what it needs with it: a model's first skin
    /// profile and its skeleton, a skeleton's parent. A file the cache already holds is kept for one more load instead.
    /// </summary>
    void Prefetch(int fileDataID, WmvAssetCache.FileKind kind)
    {
        if (!cache.Enabled || fileDataID <= 0)
            return;
        cache.Protect(fileDataID);
        byte[] held = cache.Touch(fileDataID);
        if (held != null)
        {
            ContinuePrefetch(kind, held);
            return;
        }
        if (cache.InFlight(fileDataID))
            return;
        var id = NewRequestId();
        cache.BeginFlight(fileDataID, null, id, kind, true);
        Debug.Log("WMV: asset cache: prefetching " + fileDataID + " (" + kind + ")");
        SendAssetRequest(id, fileDataID);
    }

    void ContinuePrefetch(WmvAssetCache.FileKind kind, byte[] data)
    {
        if (kind == WmvAssetCache.FileKind.Model)
        {
            int[] skins;
            int skeleton;
            M2Parser.ReadRelatedFileIds(data, out skins, out skeleton);
            if (skins.Length > 0)
                Prefetch(skins[0], WmvAssetCache.FileKind.Other);
            if (skeleton > 0)
                Prefetch(skeleton, WmvAssetCache.FileKind.Skeleton);
        }
        else if (kind == WmvAssetCache.FileKind.Skeleton)
        {
            int parent = M2Parser.ReadSkeletonParentId(data);
            if (parent > 0)
                Prefetch(parent, WmvAssetCache.FileKind.Other);   // the loaders read one parent level: never walk on
        }
    }

    /// <summary>Ask WMV which textures a model needs (it resolves them from the client DB).</summary>
    public string RequestModelTextures(int fileDataID)
    {
        var id = NewRequestId();
        Send("{\"type\":\"getModelTextures\",\"requestId\":\"" + id + "\",\"fileDataID\":" + fileDataID + "}");
        return id;
    }

    /// <summary>One map file for a terrain load (protocol 12 getMapAsset): role "wdt" or "rootAdt" -- protocol 13 adds
    /// "tex0Adt" and "texture" --, under the load and client epoch of the loadTerrainTile it serves. Never from or into the asset cache: the host's answer says what it
    /// decided about this load (state), which a kept file could not. Its id is "map" + n, from a counter of its own, and
    /// its answer comes back through OnMapAssetResponse only.</summary>
    public string RequestMapAsset(string role, int fileDataID, int load, int assetEpoch)
    {
        var id = MapRequestPrefix + (nextMapRequestId++);
        Send("{\"type\":\"getMapAsset\",\"requestId\":\"" + id + "\",\"load\":" + load + ",\"assetEpoch\":" + assetEpoch +
             ",\"role\":\"" + Escape(role) + "\",\"fileDataID\":" + fileDataID + "}");
        return id;
    }

    /// <summary>A streaming session's map file (protocol 18): as RequestMapAsset, under one of its tile loads, with the tile it
    /// is for and its priority (smaller is sooner: the host's queue is ordered by it).</summary>
    public string RequestStreamMapAsset(string role, int fileDataID, int load, int assetEpoch, int tileX, int tileY, int priority)
    {
        var id = MapRequestPrefix + (nextMapRequestId++);
        Send("{\"type\":\"getMapAsset\",\"requestId\":\"" + id + "\",\"load\":" + load + ",\"assetEpoch\":" + assetEpoch +
             ",\"role\":\"" + Escape(role) + "\",\"fileDataID\":" + fileDataID + ",\"tile\":{\"x\":" + tileX + ",\"y\":" + tileY +
             "},\"priority\":" + priority + "}");
        return id;
    }

    /// <summary>
    /// Tell WMV what became of its per-submesh state: "applied" (and what is now drawn), "pending"
    /// (kept for the model still loading) or "rejected" (with the reason; nothing changed).
    /// </summary>
    public void ReportGeosetsApplied(int fileDataID, int revision, string status, string reason,
                                     bool[] visible, int triangles, double animTimeMs)
    {
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"modelGeosetsApplied\",\"fileDataID\":").Append(fileDataID)
          .Append(",\"revision\":").Append(revision)
          .Append(",\"status\":\"").Append(Escape(status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(reason ?? "")).Append('"')
          .Append(",\"triangles\":").Append(triangles)
          .Append(",\"animTimeMs\":").Append(((long)animTimeMs).ToString(System.Globalization.CultureInfo.InvariantCulture));
        if (visible != null)
        {
            sb.Append(",\"submeshVisible\":[");
            for (int i = 0; i < visible.Length; i++)
                sb.Append(i > 0 ? "," : "").Append(visible[i] ? '1' : '0');
            sb.Append(']');
        }
        sb.Append('}');
        Send(sb.ToString());
    }

    /// <summary>
    /// What became of a characterScene. load is the serial of the loadWoWModel the scene belonged to
    /// (0 when it matched none), so the host can tell an answer about an earlier load from one about
    /// the character it is showing now. missing names the parts that could not be built. mountKey,
    /// mountStatus and mountReason (protocol 5) are the scene's mount: its key ("" for none), and whether
    /// it went under the character ("applied"), could not be built ("failed") or was not applied ("none").
    /// </summary>
    public void ReportCharacterSceneApplied(int fileDataID, int load, int revision, string status, string reason,
                                            int merged, int attachments, IList<string> missing, long ms,
                                            string mountKey, string mountStatus, string mountReason)
    {
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"characterSceneApplied\",\"fileDataID\":").Append(fileDataID)
          .Append(",\"load\":").Append(load)
          .Append(",\"revision\":").Append(revision)
          .Append(",\"status\":\"").Append(Escape(status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(reason ?? "")).Append('"')
          .Append(",\"merged\":").Append(merged)
          .Append(",\"attachments\":").Append(attachments)
          .Append(",\"ms\":").Append(ms)
          .Append(",\"missing\":[");
        if (missing != null)
            for (int i = 0; i < missing.Count; i++)
                sb.Append(i > 0 ? "," : "").Append('"').Append(Escape(missing[i])).Append('"');
        sb.Append("]")
          .Append(",\"mountKey\":\"").Append(Escape(mountKey ?? "")).Append('"')
          .Append(",\"mountStatus\":\"").Append(Escape(mountStatus ?? "")).Append('"')
          .Append(",\"mountReason\":\"").Append(Escape(mountReason ?? "")).Append('"')
          .Append('}');
        Send(sb.ToString());
    }

    /// <summary>
    /// The outcome of one world-model load, as mapObjectLoaded carries it. Counts the player did not
    /// reach (a load that failed before its groups arrived) stay 0; bounds are only written when
    /// HasBounds, so a failed load does not claim a box at the origin.
    /// </summary>
    public class MapObjectReport
    {
        public int FileDataID;
        public int Load;
        public string Status = "failed";     // "built" | "failed" | "superseded"
        public string Reason = "";
        public int Groups, GroupFilesRequested, GroupFilesMissing;
        public int Batches, Submeshes, Renderers;
        public int Materials, ProvisionalMaterials, UnresolvedMaterials, BlendedMaterials;
        public int TexturesReferenced, TexturesDecoded, TexturesMissing;
        public long Vertices, Triangles;
        public bool HasBounds;
        public Vector3 BoundsMin, BoundsMax;
        public double RootMs, GroupsMs, TexturesMs, BuildMs, TotalMs;
        public int LiveMapObjects, LiveModels;
    }

    /// <summary>Tell WMV what became of a world-model load (see MapObjectReport).</summary>
    public void ReportMapObjectLoaded(MapObjectReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"mapObjectLoaded\",\"fileDataID\":").Append(r.FileDataID)
          .Append(",\"load\":").Append(r.Load)
          .Append(",\"status\":\"").Append(Escape(r.Status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"')
          .Append(",\"groups\":").Append(r.Groups)
          .Append(",\"groupFilesRequested\":").Append(r.GroupFilesRequested)
          .Append(",\"groupFilesMissing\":").Append(r.GroupFilesMissing)
          .Append(",\"batches\":").Append(r.Batches)
          .Append(",\"submeshes\":").Append(r.Submeshes)
          .Append(",\"renderers\":").Append(r.Renderers)
          .Append(",\"materials\":").Append(r.Materials)
          .Append(",\"provisionalMaterials\":").Append(r.ProvisionalMaterials)
          .Append(",\"unresolvedMaterials\":").Append(r.UnresolvedMaterials)
          .Append(",\"blendedMaterials\":").Append(r.BlendedMaterials)
          .Append(",\"texturesReferenced\":").Append(r.TexturesReferenced)
          .Append(",\"texturesDecoded\":").Append(r.TexturesDecoded)
          .Append(",\"texturesMissing\":").Append(r.TexturesMissing)
          .Append(",\"vertices\":").Append(r.Vertices)
          .Append(",\"triangles\":").Append(r.Triangles);
        if (r.HasBounds)
        {
            // "R" keeps a float exact through the text; the invariant culture keeps a comma locale
            // from writing 1,5 into the JSON.
            sb.Append(",\"boundsMin\":[").Append(r.BoundsMin.x.ToString("R", inv)).Append(',')
              .Append(r.BoundsMin.y.ToString("R", inv)).Append(',').Append(r.BoundsMin.z.ToString("R", inv)).Append(']')
              .Append(",\"boundsMax\":[").Append(r.BoundsMax.x.ToString("R", inv)).Append(',')
              .Append(r.BoundsMax.y.ToString("R", inv)).Append(',').Append(r.BoundsMax.z.ToString("R", inv)).Append(']');
        }
        sb.Append(",\"timings\":{\"rootMs\":").Append(r.RootMs.ToString("0.#", inv))
          .Append(",\"groupsMs\":").Append(r.GroupsMs.ToString("0.#", inv))
          .Append(",\"texturesMs\":").Append(r.TexturesMs.ToString("0.#", inv))
          .Append(",\"buildMs\":").Append(r.BuildMs.ToString("0.#", inv))
          .Append(",\"totalMs\":").Append(r.TotalMs.ToString("0.#", inv)).Append('}')
          .Append(",\"liveMapObjects\":").Append(r.LiveMapObjects)
          .Append(",\"liveModels\":").Append(r.LiveModels)
          .Append('}');
        Send(sb.ToString());
    }

    /// <summary>
    /// The outcome of one terrain load, as terrainTileLoaded carries it (protocol 12; WmvTerrainSession fills it). A
    /// number the load did not reach is left out of the line: the byte counts while negative, the timings while
    /// negative, the mesh figures without HasMesh, the bounds without HasBounds.
    /// </summary>
    public class TerrainTileReport
    {
        public int Load;
        public string Status = "failed";     // "built" | "failed" | "superseded" | "cleared"
        public string Reason = "";
        public string HostState = "", HostDetail = "";   // the host's answer that failed the load, when one did
        public int MapId, TileX, TileY, WdtFileDataID, RootFileDataID;
        public long WdtBytes = -1, RootBytes = -1;
        public string WdtMd5 = "", RootMd5 = "";         // hex of the bytes parsed
        public bool HasMesh;
        public int Chunks, ChunksBuilt, ChunksSkipped, ChunksUnsupported, HoleCells;
        public long Vertices, Triangles, MeshBytes;
        public bool HasBounds;
        public Vector3 BoundsMin, BoundsMax;             // Unity space, relative to the tile root
        public Vector3 RootOffset;                       // the tile root's Unity position
        public double OriginX, OriginY, OriginZ;         // the world point (game frame, yards) the root stands for
        public double WdtRoundTripMs = -1, WdtParseMs = -1, RootRoundTripMs = -1, ParseMs = -1, BuildMs = -1, UploadMs = -1;
        public double FirstFrameMs = -1, TotalMs = -1;
        public int LiveTerrainTiles, LiveModels, LiveMapObjects;
        public int MeshObjects, MaterialObjects, TerrainGameObjects;   // sampled when the report is sent
        public long ManagedHeapBytes, NativeAllocatedBytes;
        // Protocol 13: the load's requests still out when it ended, each retired (WmvTerrainSession), and the material.
        public int RequestsRetired;
        public List<string> RetiredRequestIds = new List<string>();
        public TerrainMaterialReport Material;          // null: not sent (a load that ended before its material was built)
        // Protocol 14: the view the tile was drawn in (WmvTerrainDebug.ViewNames), "" when no tile was built.
        public string View = "";
        // Protocol 16: a world load's world models (nothing written for a terrain load).
        public bool World;
        public string WorldKind = "";                   // "tile" | "globalWmo"
        public string WorldState = "";                  // "complete" | "partial" | "none" | "failed" | "ended"
        public string WorldReason = "";
        public int WorldRecords, WorldPlacements, WorldPlaced, WorldFailed, WorldUnsupported, WorldNotRequested, WorldEnded;
        public int WorldModels, WorldModelsBuilt, WorldModelsFailed, WorldModelsNotRequested;
        public int WorldGroupFiles, WorldGroupFilesMissing, WorldTextures, WorldTexturesDecoded, WorldTexturesMissing;
        public long WorldFileBytes, WorldTextureBytes, WorldGpuGeometryBytes, WorldGpuTextureBytes;
        public double WorldShownMs = -1, WorldFetchMs = -1, WorldParseMs = -1, WorldDecodeMs = -1, WorldBuildMs = -1, WorldPlaceMs = -1;
        public double WorldBuildMaxMs = -1;             // the longest single prototype build (one frame's main-thread cost)
        public double[] WorldOrigin = new double[3];
        public bool WorldStart;                         // a start point was found in a global world model's geometry
        public double[] WorldStartPoint = new double[3];
        public string WorldStartReason = "";
        public List<WorldPlacementReport> WorldPlacementList = new List<WorldPlacementReport>();
        // Protocol 17: the whole world's assembly and its doodads (written when HasDoodads).
        public string WorldAssembly = "";               // "complete" | "partial" | "none" | "failed" | "ended"
        public bool HasDoodads;
        public string DoodadState = "", DoodadReason = "";   // "complete" | "partial" | "none" | "failed" | "ended" | "notOffered"
        public int DoodadMddfRecords, DoodadMddfPlacements, DoodadWmoRecords, DoodadWmoPlacements;
        public List<DoodadSetReport> DoodadSets = new List<DoodadSetReport>();
        public int DoodadSetsTotal;
        public int DoodadInstances, DoodadPlaced, DoodadFailed, DoodadUnsupported, DoodadNotRequested, DoodadEnded;
        public int DoodadModels, DoodadModelsBuilt, DoodadModelsFailed, DoodadModelsUnsupported, DoodadModelsNotRequested, DoodadModelsPartial;
        public int DoodadTextures, DoodadTexturesDecoded, DoodadTexturesMissing, DoodadTextureUses, DoodadTextureUploads;
        public long DoodadFileBytes, DoodadTextureBytes, DoodadGpuGeometryBytes, DoodadGpuTextureBytes;
        public double DoodadDiscoverMs = -1, DoodadFirstVisibleMs = -1, DoodadFetchMs = -1, DoodadParseMs = -1, DoodadDecodeMs = -1,
                      DoodadBuildMs = -1, DoodadBuildMaxMs = -1, DoodadPlaceMs = -1, DoodadPlaceMaxMs = -1, DoodadReadyMs = -1;
        public List<DoodadModelReport> DoodadList = new List<DoodadModelReport>();
        public int DoodadListTotal;
        // Protocol 19: the load's liquids (null: not written -- a report made before protocol 19, or by a test).
        public TerrainLiquidReport Liquids;
    }

    /// <summary>terrainTileLoaded's "liquids" (protocol 19; WmvTerrainSessionLiquids fills it): see the header.</summary>
    public class TerrainLiquidReport
    {
        public bool Offered;
        public string Catalog = "absent";               // "ok" | "unavailable" | "absent" (not offered)
        public int Instances, Layers, Chunks, Quads, Vertices, Meshes;
        public List<int> Types = new List<int>();
        public int TexturesRequested, TexturesLoaded, TexturesFailed, MissingTypes;
        public int FormatBySize, FormatDisagreements, Malformed, WmoGroups, WmoQuads;
        public long GeometryBytes, TextureBytes;
    }

    /// <summary>A placed world model's doodad sets in terrainTileLoaded (at most WmvTerrainSession.ReportDoodadSetsMax): the
    /// set its record names, the names of the sets drawn (set 0 plus that one), the root's set count, the MODD records drawn.</summary>
    public class DoodadSetReport
    {
        public uint UniqueId, Wmo;
        public int Set, SetCount, Active;
        public string SetName = "", Reason = "";
    }

    /// <summary>One doodad model in terrainTileLoaded (at most WmvTerrainSession.ReportDoodadModelsMax: every model not built,
    /// or built partially, first, then the most placed).</summary>
    public class DoodadModelReport
    {
        public int FileDataID, Instances, Placed;
        public string State = "", Reason = "";          // "built" | "partial" | "failed" | "unsupported" | "notRequested" | "ended"
    }

    /// <summary>One placement in terrainTileLoaded's world (at most WmvTerrainSession.ReportPlacementsMax listed): its
    /// state, the record, the world box its extents state, and the world box its built instance's renderers cover.</summary>
    public class WorldPlacementReport
    {
        public uint UniqueId;
        public int FileDataID, Records = 1;
        public string State = "", Reason = "";          // "placed" | "failed" | "unsupported" | "notRequested" | "ended"
        public double[] Position = new double[3];       // W, world frame
        public float[] Rotation = new float[3];         // as stored (degrees)
        public double Scale = 1.0;
        public double[] Extents = new double[6];        // the record's extents as a world box
        public bool HasRendered;
        public double[] Rendered = new double[6];       // the built instance's renderer bounds, back in the world frame
        public int Groups, GroupsDrawn;
    }

    /// <summary>inspectMapTiles (protocol 14): a map's WDT bytes and the content key the host checked them against.</summary>
    public class MapTilesRequest
    {
        public int Inspection, MapId, WdtFileDataID, AssetEpoch;
        public string Md5 = "", Encoding = "";
        public byte[] Bytes;
    }

    /// <summary>mapTilesInspected (protocol 14): what the WDT says of its tiles (WmvMapTiles.Inspect). Per tile, index y * 64
    /// + x (x the column, east; y the row, south -- the root ADT is named &lt;map&gt;_x_y): its MAIN flag and its MAID root ADT
    /// and tex0 ADT FileDataIDs (0: the WDT names none).</summary>
    public class MapTilesReport
    {
        public int Inspection, MapId, WdtFileDataID, AssetEpoch;
        public string Status = "failed";                // "inspected" | "failed"
        public string Reason = "";
        public string Md5 = "", Kind = "";
        public long Bytes = -1;
        public uint MphdFlags;
        public bool HasMain, HasMaid;
        public int TilesInMain, TilesWithRoot;
        public byte[] TileFlags;                        // 4096: bit 0 the MAIN flag, bit 1 MAID names a root ADT
        public uint[] RootIds, Tex0Ids;                 // 4096 each, or null without MAID
        public uint[] MinimapIds;                       // protocol 15: MAID word 7 of every tile, or null without MAID
        // Protocol 15: a world-model-only map's global WMO (the WDT's MODF records; the first one's file).
        public int GlobalWmoRecords;                    // MODF records in the WDT
        public uint GlobalWmoFileDataID;                // 0 when the record names a path, or nothing resolvable
        public string GlobalWmoPath = "";
        // Protocol 16: the first global record itself, as stored (file frame, degrees), for inspectWorldModel and the
        // footprint; HasGlobalRecord false without one.
        public bool HasGlobalRecord;
        public float[] GlobalPosition = new float[3], GlobalRotation = new float[3], GlobalExtentsMin = new float[3], GlobalExtentsMax = new float[3];
        public int GlobalFlags, GlobalScaleRaw;
        public uint GlobalUniqueId;
        public double ParseMs = -1;
    }

    /// <summary>terrainViewApplied (protocol 14): what setTerrainView did.</summary>
    public class TerrainViewReport
    {
        public string View = "", Status = "refused", Reason = "";   // "applied" | "noTile" | "failed" | "refused"
        public int Load;                                // the tile rebuilt, 0 for none
        public double RebuildMs = -1;
        public int TexturedChunks = -1, NeutralChunks = -1, Textures = -1;
        public long RetainedTextureBytes = -1;
    }

    public void ReportMapTilesInspected(MapTilesReport r) { Send(MapTilesInspectedLine(r)); }

    /// <summary>The mapTilesInspected line (also for the self-test). The tile table travels as base64: 4,096 flag bytes,
    /// and 4,096 little-endian u32 each for the root and tex0 ids.</summary>
    public static string MapTilesInspectedLine(MapTilesReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"mapTilesInspected\",\"inspection\":").Append(r.Inspection)
          .Append(",\"status\":\"").Append(Escape(r.Status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"')
          .Append(",\"mapId\":").Append(r.MapId)
          .Append(",\"wdtFileDataID\":").Append(r.WdtFileDataID)
          .Append(",\"assetEpoch\":").Append(r.AssetEpoch)
          .Append(",\"wdtBytes\":").Append(r.Bytes)
          .Append(",\"md5\":\"").Append(Escape(r.Md5 ?? "")).Append('"')
          .Append(",\"kind\":\"").Append(Escape(r.Kind ?? "")).Append('"')
          .Append(",\"mphdFlags\":").Append(r.MphdFlags)
          .Append(",\"hasMain\":").Append(r.HasMain ? "true" : "false")
          .Append(",\"hasMaid\":").Append(r.HasMaid ? "true" : "false")
          .Append(",\"tilesInMain\":").Append(r.TilesInMain)
          .Append(",\"tilesWithRoot\":").Append(r.TilesWithRoot)
          .Append(",\"parseMs\":").Append(r.ParseMs.ToString("0.###", inv));
        if (r.TileFlags != null)
            sb.Append(",\"tileFlags\":\"").Append(Convert.ToBase64String(r.TileFlags)).Append('"');
        if (r.RootIds != null)
            sb.Append(",\"rootIds\":\"").Append(Convert.ToBase64String(U32Bytes(r.RootIds))).Append('"');
        if (r.Tex0Ids != null)
            sb.Append(",\"tex0Ids\":\"").Append(Convert.ToBase64String(U32Bytes(r.Tex0Ids))).Append('"');
        if (r.MinimapIds != null)
            sb.Append(",\"minimapIds\":\"").Append(Convert.ToBase64String(U32Bytes(r.MinimapIds))).Append('"');
        sb.Append(",\"globalWmoRecords\":").Append(r.GlobalWmoRecords)
          .Append(",\"globalWmoFileDataID\":").Append(r.GlobalWmoFileDataID)
          .Append(",\"globalWmoPath\":\"").Append(Escape(r.GlobalWmoPath ?? "")).Append('"');
        if (r.HasGlobalRecord)
        {
            sb.Append(",\"globalRecord\":true");
            AppendFloats(sb, "globalPosition", r.GlobalPosition);
            AppendFloats(sb, "globalRotation", r.GlobalRotation);
            AppendFloats(sb, "globalExtentsMin", r.GlobalExtentsMin);
            AppendFloats(sb, "globalExtentsMax", r.GlobalExtentsMax);
            sb.Append(",\"globalFlags\":").Append(r.GlobalFlags)
              .Append(",\"globalScale\":").Append(r.GlobalScaleRaw)
              .Append(",\"globalUniqueId\":").Append(r.GlobalUniqueId);
        }
        sb.Append('}');
        return sb.ToString();
    }

    static void AppendFloats(StringBuilder sb, string key, float[] v)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        sb.Append(",\"").Append(key).Append("\":[");
        for (int i = 0; v != null && i < v.Length; i++)
            sb.Append(i > 0 ? "," : "").Append(float.IsNaN(v[i]) || float.IsInfinity(v[i]) ? "null" : v[i].ToString("R", inv));
        sb.Append(']');
    }

    static byte[] U32Bytes(uint[] v)
    {
        var b = new byte[v.Length * 4];
        for (int i = 0; i < v.Length; i++)
        {
            b[4 * i] = (byte)v[i];
            b[4 * i + 1] = (byte)(v[i] >> 8);
            b[4 * i + 2] = (byte)(v[i] >> 16);
            b[4 * i + 3] = (byte)(v[i] >> 24);
        }
        return b;
    }

    public void ReportTerrainViewApplied(TerrainViewReport r) { Send(TerrainViewAppliedLine(r)); }

    public static string TerrainViewAppliedLine(TerrainViewReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"terrainViewApplied\",\"view\":\"").Append(Escape(r.View ?? "")).Append('"')
          .Append(",\"status\":\"").Append(Escape(r.Status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"')
          .Append(",\"load\":").Append(r.Load)
          .Append(",\"rebuildMs\":").Append(r.RebuildMs.ToString("0.###", inv))
          .Append(",\"texturedChunks\":").Append(r.TexturedChunks)
          .Append(",\"neutralChunks\":").Append(r.NeutralChunks)
          .Append(",\"textures\":").Append(r.Textures)
          .Append(",\"retainedTextureBytes\":").Append(r.RetainedTextureBytes)
          .Append('}');
        return sb.ToString();
    }

    /// <summary>The tile's texture material as terrainTileLoaded carries it (protocol 13; WmvTerrainSession fills it). A
    /// count is always written; a byte count, a size or a timing that was not reached (negative) is left out.</summary>
    public class TerrainMaterialReport
    {
        public string State = "unavailable";            // "textured" | "partial" | "unavailable" | "unsupported"
        public string Reason = "";
        public int Tex0FileDataID;
        public long Tex0Bytes = -1;
        public string Tex0Md5 = "";
        public string AlphaFormat = "";                 // "8-bit" | "4-bit" (the WDT's announcement)
        public List<KeyValuePair<string, int>> ChunkStates = new List<KeyValuePair<string, int>>();
        public List<TerrainTextureReport> Textures = new List<TerrainTextureReport>();
        public int UniqueTextures, LayerReferences, DuplicateReferences, Materials;
        public long TextureSourceBytes, TextureGpuBytes, AlphaGpuBytes, MaterialVertexBytes;
        public double Tex0RoundTripMs = -1, Tex0ParseMs = -1, TextureFetchMs = -1, TextureUploadMs = -1, AlphaBuildMs = -1,
                      MaterialBuildMs = -1;
    }

    /// <summary>One texture of a terrain material: the host's state quoted (or the player's own) and what was uploaded.</summary>
    public class TerrainTextureReport
    {
        public int FileDataID;
        public string Role = "diffuse";                 // "diffuse" (MDID) | "height" (MHID, MPHD 0x80 maps)
        public string State = "", Detail = "";
        public long Bytes = -1;
        public string Md5 = "";
        public int Width = -1, Height = -1, Mips = -1;
        public string Format = "";
        public double UploadMs = -1;
    }

    /// <summary>Tell WMV what became of a terrain load (see TerrainTileReport).</summary>
    public void ReportTerrainTileLoaded(TerrainTileReport r)
    {
        Send(TerrainTileLoadedLine(r));
    }

    /// <summary>The terrainTileLoaded line for a report, as ReportTerrainTileLoaded sends it (also for the self-test).</summary>
    public static string TerrainTileLoadedLine(TerrainTileReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"terrainTileLoaded\",\"load\":").Append(r.Load)
          .Append(",\"status\":\"").Append(Escape(r.Status)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.Reason ?? "")).Append('"');
        if (!string.IsNullOrEmpty(r.HostState))
            sb.Append(",\"hostState\":\"").Append(Escape(r.HostState)).Append('"')
              .Append(",\"hostDetail\":\"").Append(Escape(r.HostDetail ?? "")).Append('"');
        sb.Append(",\"mapId\":").Append(r.MapId)
          .Append(",\"tileX\":").Append(r.TileX)
          .Append(",\"tileY\":").Append(r.TileY)
          .Append(",\"wdtFileDataID\":").Append(r.WdtFileDataID)
          .Append(",\"rootFileDataID\":").Append(r.RootFileDataID);
        if (r.WdtBytes >= 0)
            sb.Append(",\"wdtBytes\":").Append(r.WdtBytes);
        if (r.RootBytes >= 0)
            sb.Append(",\"rootBytes\":").Append(r.RootBytes);
        if (!string.IsNullOrEmpty(r.WdtMd5))
            sb.Append(",\"wdtMd5\":\"").Append(Escape(r.WdtMd5)).Append('"');
        if (!string.IsNullOrEmpty(r.RootMd5))
            sb.Append(",\"rootMd5\":\"").Append(Escape(r.RootMd5)).Append('"');
        if (r.HasMesh)
        {
            sb.Append(",\"chunks\":").Append(r.Chunks)
              .Append(",\"chunksBuilt\":").Append(r.ChunksBuilt)
              .Append(",\"chunksSkipped\":").Append(r.ChunksSkipped)
              .Append(",\"chunksUnsupported\":").Append(r.ChunksUnsupported)
              .Append(",\"vertices\":").Append(r.Vertices)
              .Append(",\"triangles\":").Append(r.Triangles)
              .Append(",\"holeCells\":").Append(r.HoleCells);
            // "R" keeps a float exact through the text, as mapObjectLoaded's bounds.
            if (r.HasBounds)
                sb.Append(",\"boundsMin\":[").Append(r.BoundsMin.x.ToString("R", inv)).Append(',')
                  .Append(r.BoundsMin.y.ToString("R", inv)).Append(',').Append(r.BoundsMin.z.ToString("R", inv)).Append(']')
                  .Append(",\"boundsMax\":[").Append(r.BoundsMax.x.ToString("R", inv)).Append(',')
                  .Append(r.BoundsMax.y.ToString("R", inv)).Append(',').Append(r.BoundsMax.z.ToString("R", inv)).Append(']');
            sb.Append(",\"rootOffset\":[").Append(r.RootOffset.x.ToString("R", inv)).Append(',')
              .Append(r.RootOffset.y.ToString("R", inv)).Append(',').Append(r.RootOffset.z.ToString("R", inv)).Append(']')
              .Append(",\"originWorld\":[").Append(r.OriginX.ToString("R", inv)).Append(',')
              .Append(r.OriginY.ToString("R", inv)).Append(',').Append(r.OriginZ.ToString("R", inv)).Append(']')
              .Append(",\"meshBytes\":").Append(r.MeshBytes);
        }
        sb.Append(",\"timings\":{");
        int timings = 0;
        AppendTiming(sb, ref timings, "wdtRoundTripMs", r.WdtRoundTripMs);
        AppendTiming(sb, ref timings, "wdtParseMs", r.WdtParseMs);
        AppendTiming(sb, ref timings, "rootRoundTripMs", r.RootRoundTripMs);
        AppendTiming(sb, ref timings, "parseMs", r.ParseMs);
        AppendTiming(sb, ref timings, "buildMs", r.BuildMs);
        AppendTiming(sb, ref timings, "uploadMs", r.UploadMs);
        AppendTiming(sb, ref timings, "firstFrameMs", r.FirstFrameMs);
        AppendTiming(sb, ref timings, "totalMs", r.TotalMs);
        sb.Append('}')
          .Append(",\"liveTerrainTiles\":").Append(r.LiveTerrainTiles)
          .Append(",\"liveModels\":").Append(r.LiveModels)
          .Append(",\"liveMapObjects\":").Append(r.LiveMapObjects)
          .Append(",\"meshObjects\":").Append(r.MeshObjects)
          .Append(",\"materialObjects\":").Append(r.MaterialObjects)
          .Append(",\"terrainGameObjects\":").Append(r.TerrainGameObjects)
          .Append(",\"managedHeapBytes\":").Append(r.ManagedHeapBytes)
          .Append(",\"nativeAllocatedBytes\":").Append(r.NativeAllocatedBytes);
        // Protocol 13.
        sb.Append(",\"requestsRetired\":").Append(r.RequestsRetired).Append(",\"retiredRequestIds\":[");
        for (int i = 0; r.RetiredRequestIds != null && i < r.RetiredRequestIds.Count; i++)
            sb.Append(i > 0 ? "," : "").Append('"').Append(Escape(r.RetiredRequestIds[i])).Append('"');
        sb.Append(']');
        if (r.Material != null)
            AppendMaterial(sb, r.Material);
        // Protocol 14.
        if (!string.IsNullOrEmpty(r.View))
            sb.Append(",\"view\":\"").Append(Escape(r.View)).Append('"');
        // Protocol 16.
        if (r.World)
            AppendWorld(sb, r);
        // Protocol 19.
        if (r.Liquids != null)
            AppendLiquids(sb, r.Liquids);
        sb.Append('}');
        return sb.ToString();
    }

    /// <summary>Protocol 19: the load's liquids, inside terrainTileLoaded.</summary>
    static void AppendLiquids(StringBuilder sb, TerrainLiquidReport l)
    {
        sb.Append(",\"liquids\":{\"offered\":").Append(l.Offered ? "true" : "false")
          .Append(",\"catalog\":\"").Append(Escape(l.Catalog ?? "")).Append('"')
          .Append(",\"instances\":").Append(l.Instances)
          .Append(",\"layers\":").Append(l.Layers)
          .Append(",\"chunks\":").Append(l.Chunks)
          .Append(",\"quads\":").Append(l.Quads)
          .Append(",\"vertices\":").Append(l.Vertices)
          .Append(",\"meshes\":").Append(l.Meshes)
          .Append(",\"types\":[");
        for (int i = 0; l.Types != null && i < l.Types.Count; i++)
            sb.Append(i > 0 ? "," : "").Append(l.Types[i]);
        sb.Append(']')
          .Append(",\"texturesRequested\":").Append(l.TexturesRequested)
          .Append(",\"texturesLoaded\":").Append(l.TexturesLoaded)
          .Append(",\"texturesFailed\":").Append(l.TexturesFailed)
          .Append(",\"missingTypes\":").Append(l.MissingTypes)
          .Append(",\"formatBySize\":").Append(l.FormatBySize)
          .Append(",\"formatDisagreements\":").Append(l.FormatDisagreements)
          .Append(",\"malformed\":").Append(l.Malformed)
          .Append(",\"wmoGroups\":").Append(l.WmoGroups)
          .Append(",\"wmoQuads\":").Append(l.WmoQuads)
          .Append(",\"geometryBytes\":").Append(l.GeometryBytes)
          .Append(",\"textureBytes\":").Append(l.TextureBytes)
          .Append('}');
    }

    static void AppendWorld(StringBuilder sb, TerrainTileReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        sb.Append(",\"world\":{\"kind\":\"").Append(Escape(r.WorldKind ?? "")).Append('"')
          .Append(",\"state\":\"").Append(Escape(r.WorldState ?? "")).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.WorldReason ?? "")).Append('"')
          .Append(",\"records\":").Append(r.WorldRecords)
          .Append(",\"placements\":").Append(r.WorldPlacements)
          .Append(",\"placed\":").Append(r.WorldPlaced)
          .Append(",\"failed\":").Append(r.WorldFailed)
          .Append(",\"unsupported\":").Append(r.WorldUnsupported)
          .Append(",\"notRequested\":").Append(r.WorldNotRequested)
          .Append(",\"ended\":").Append(r.WorldEnded)
          .Append(",\"models\":").Append(r.WorldModels)
          .Append(",\"modelsBuilt\":").Append(r.WorldModelsBuilt)
          .Append(",\"modelsFailed\":").Append(r.WorldModelsFailed)
          .Append(",\"modelsNotRequested\":").Append(r.WorldModelsNotRequested)
          .Append(",\"groupFiles\":").Append(r.WorldGroupFiles)
          .Append(",\"groupFilesMissing\":").Append(r.WorldGroupFilesMissing)
          .Append(",\"textures\":").Append(r.WorldTextures)
          .Append(",\"texturesDecoded\":").Append(r.WorldTexturesDecoded)
          .Append(",\"texturesMissing\":").Append(r.WorldTexturesMissing)
          .Append(",\"fileBytes\":").Append(r.WorldFileBytes)
          .Append(",\"textureBytes\":").Append(r.WorldTextureBytes)
          .Append(",\"gpuGeometryBytes\":").Append(r.WorldGpuGeometryBytes)
          .Append(",\"gpuTextureBytes\":").Append(r.WorldGpuTextureBytes);
        AppendDoubles(sb, "originWorld", r.WorldOrigin);
        sb.Append(",\"start\":").Append(r.WorldStart ? "true" : "false");
        if (r.WorldStart)
        {
            AppendDoubles(sb, "startPoint", r.WorldStartPoint);
            sb.Append(",\"startReason\":\"").Append(Escape(r.WorldStartReason ?? "")).Append('"');
        }
        sb.Append(",\"timings\":{");
        int timings = 0;
        AppendTiming(sb, ref timings, "shownMs", r.WorldShownMs);
        AppendTiming(sb, ref timings, "fetchMs", r.WorldFetchMs);
        AppendTiming(sb, ref timings, "parseMs", r.WorldParseMs);
        AppendTiming(sb, ref timings, "decodeMs", r.WorldDecodeMs);
        AppendTiming(sb, ref timings, "buildMs", r.WorldBuildMs);
        AppendTiming(sb, ref timings, "buildMaxMs", r.WorldBuildMaxMs);
        AppendTiming(sb, ref timings, "placeMs", r.WorldPlaceMs);
        sb.Append("},\"list\":[");
        for (int i = 0; r.WorldPlacementList != null && i < r.WorldPlacementList.Count; i++)
        {
            WorldPlacementReport p = r.WorldPlacementList[i];
            sb.Append(i > 0 ? ",{" : "{").Append("\"uniqueId\":").Append(p.UniqueId)
              .Append(",\"fileDataID\":").Append(p.FileDataID)
              .Append(",\"records\":").Append(p.Records)
              .Append(",\"state\":\"").Append(Escape(p.State ?? "")).Append('"')
              .Append(",\"reason\":\"").Append(Escape(p.Reason ?? "")).Append('"')
              .Append(",\"scale\":").Append(p.Scale.ToString("R", inv))
              .Append(",\"groups\":").Append(p.Groups)
              .Append(",\"groupsDrawn\":").Append(p.GroupsDrawn);
            AppendDoubles(sb, "position", p.Position);
            sb.Append(",\"rotation\":[").Append(p.Rotation[0].ToString("R", inv)).Append(',').Append(p.Rotation[1].ToString("R", inv))
              .Append(',').Append(p.Rotation[2].ToString("R", inv)).Append(']');
            AppendDoubles(sb, "extents", p.Extents);
            if (p.HasRendered)
                AppendDoubles(sb, "rendered", p.Rendered);
            sb.Append('}');
        }
        sb.Append(']');
        if (r.HasDoodads)
            AppendDoodads(sb, r);
        sb.Append('}');
    }

    /// <summary>Protocol 17: the world's assembly and its doodads, inside terrainTileLoaded's world (the lists capped, so a
    /// report stays far below the host's line limit).</summary>
    static void AppendDoodads(StringBuilder sb, TerrainTileReport r)
    {
        sb.Append(",\"assembly\":\"").Append(Escape(r.WorldAssembly ?? "")).Append('"')
          .Append(",\"doodads\":{\"state\":\"").Append(Escape(r.DoodadState ?? "")).Append('"')
          .Append(",\"reason\":\"").Append(Escape(r.DoodadReason ?? "")).Append('"')
          .Append(",\"mddfRecords\":").Append(r.DoodadMddfRecords)
          .Append(",\"mddfPlacements\":").Append(r.DoodadMddfPlacements)
          .Append(",\"wmoDoodadRecords\":").Append(r.DoodadWmoRecords)
          .Append(",\"wmoPlacementsWithDoodads\":").Append(r.DoodadWmoPlacements)
          .Append(",\"setsTotal\":").Append(r.DoodadSetsTotal)
          .Append(",\"sets\":[");
        for (int i = 0; r.DoodadSets != null && i < r.DoodadSets.Count; i++)
        {
            DoodadSetReport s = r.DoodadSets[i];
            sb.Append(i > 0 ? ",{" : "{").Append("\"uniqueId\":").Append(s.UniqueId)
              .Append(",\"wmo\":").Append(s.Wmo)
              .Append(",\"set\":").Append(s.Set)
              .Append(",\"setName\":\"").Append(Escape(s.SetName ?? "")).Append('"')
              .Append(",\"setCount\":").Append(s.SetCount)
              .Append(",\"active\":").Append(s.Active)
              .Append(",\"reason\":\"").Append(Escape(s.Reason ?? "")).Append("\"}");
        }
        sb.Append(']')
          .Append(",\"instances\":").Append(r.DoodadInstances)
          .Append(",\"placed\":").Append(r.DoodadPlaced)
          .Append(",\"failed\":").Append(r.DoodadFailed)
          .Append(",\"unsupported\":").Append(r.DoodadUnsupported)
          .Append(",\"notRequested\":").Append(r.DoodadNotRequested)
          .Append(",\"ended\":").Append(r.DoodadEnded)
          .Append(",\"models\":").Append(r.DoodadModels)
          .Append(",\"modelsBuilt\":").Append(r.DoodadModelsBuilt)
          .Append(",\"modelsFailed\":").Append(r.DoodadModelsFailed)
          .Append(",\"modelsUnsupported\":").Append(r.DoodadModelsUnsupported)
          .Append(",\"modelsNotRequested\":").Append(r.DoodadModelsNotRequested)
          .Append(",\"modelsPartial\":").Append(r.DoodadModelsPartial)
          .Append(",\"textures\":").Append(r.DoodadTextures)
          .Append(",\"texturesDecoded\":").Append(r.DoodadTexturesDecoded)
          .Append(",\"texturesMissing\":").Append(r.DoodadTexturesMissing)
          .Append(",\"textureUses\":").Append(r.DoodadTextureUses)
          .Append(",\"textureUploads\":").Append(r.DoodadTextureUploads)
          .Append(",\"fileBytes\":").Append(r.DoodadFileBytes)
          .Append(",\"textureBytes\":").Append(r.DoodadTextureBytes)
          .Append(",\"gpuGeometryBytes\":").Append(r.DoodadGpuGeometryBytes)
          .Append(",\"gpuTextureBytes\":").Append(r.DoodadGpuTextureBytes)
          .Append(",\"timings\":{");
        int timings = 0;
        AppendTiming(sb, ref timings, "discoverMs", r.DoodadDiscoverMs);
        AppendTiming(sb, ref timings, "firstVisibleMs", r.DoodadFirstVisibleMs);
        AppendTiming(sb, ref timings, "fetchMs", r.DoodadFetchMs);
        AppendTiming(sb, ref timings, "parseMs", r.DoodadParseMs);
        AppendTiming(sb, ref timings, "decodeMs", r.DoodadDecodeMs);
        AppendTiming(sb, ref timings, "buildMs", r.DoodadBuildMs);
        AppendTiming(sb, ref timings, "buildMaxMs", r.DoodadBuildMaxMs);
        AppendTiming(sb, ref timings, "placeMs", r.DoodadPlaceMs);
        AppendTiming(sb, ref timings, "placeMaxMs", r.DoodadPlaceMaxMs);
        AppendTiming(sb, ref timings, "readyMs", r.DoodadReadyMs);
        sb.Append("},\"listTotal\":").Append(r.DoodadListTotal).Append(",\"list\":[");
        for (int i = 0; r.DoodadList != null && i < r.DoodadList.Count; i++)
        {
            DoodadModelReport m = r.DoodadList[i];
            sb.Append(i > 0 ? ",{" : "{").Append("\"fileDataID\":").Append(m.FileDataID)
              .Append(",\"state\":\"").Append(Escape(m.State ?? "")).Append('"')
              .Append(",\"reason\":\"").Append(Escape(m.Reason ?? "")).Append('"')
              .Append(",\"instances\":").Append(m.Instances)
              .Append(",\"placed\":").Append(m.Placed).Append('}');
        }
        sb.Append("]}");
    }

    static void AppendMaterial(StringBuilder sb, TerrainMaterialReport m)
    {
        sb.Append(",\"material\":{\"state\":\"").Append(Escape(m.State)).Append('"')
          .Append(",\"reason\":\"").Append(Escape(m.Reason ?? "")).Append('"')
          .Append(",\"tex0FileDataID\":").Append(m.Tex0FileDataID);
        if (m.Tex0Bytes >= 0)
            sb.Append(",\"tex0Bytes\":").Append(m.Tex0Bytes);
        if (!string.IsNullOrEmpty(m.Tex0Md5))
            sb.Append(",\"tex0Md5\":\"").Append(Escape(m.Tex0Md5)).Append('"');
        sb.Append(",\"alphaFormat\":\"").Append(Escape(m.AlphaFormat ?? "")).Append('"')
          .Append(",\"chunkStates\":{");
        for (int i = 0; m.ChunkStates != null && i < m.ChunkStates.Count; i++)
            sb.Append(i > 0 ? "," : "").Append('"').Append(Escape(m.ChunkStates[i].Key)).Append("\":").Append(m.ChunkStates[i].Value);
        sb.Append("},\"textures\":[");
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        for (int i = 0; m.Textures != null && i < m.Textures.Count; i++)
        {
            TerrainTextureReport t = m.Textures[i];
            sb.Append(i > 0 ? ",{" : "{").Append("\"fileDataID\":").Append(t.FileDataID)
              .Append(",\"role\":\"").Append(Escape(string.IsNullOrEmpty(t.Role) ? "diffuse" : t.Role)).Append('"')
              .Append(",\"state\":\"").Append(Escape(t.State ?? "")).Append('"')
              .Append(",\"detail\":\"").Append(Escape(t.Detail ?? "")).Append('"');
            if (t.Bytes >= 0) sb.Append(",\"bytes\":").Append(t.Bytes);
            if (!string.IsNullOrEmpty(t.Md5)) sb.Append(",\"md5\":\"").Append(Escape(t.Md5)).Append('"');
            if (t.Width >= 0) sb.Append(",\"width\":").Append(t.Width);
            if (t.Height >= 0) sb.Append(",\"height\":").Append(t.Height);
            if (!string.IsNullOrEmpty(t.Format)) sb.Append(",\"format\":\"").Append(Escape(t.Format)).Append('"');
            if (t.Mips >= 0) sb.Append(",\"mips\":").Append(t.Mips);
            if (t.UploadMs >= 0) sb.Append(",\"uploadMs\":").Append(t.UploadMs.ToString("0.###", inv));
            sb.Append('}');
        }
        sb.Append(']')
          .Append(",\"uniqueTextures\":").Append(m.UniqueTextures)
          .Append(",\"layerReferences\":").Append(m.LayerReferences)
          .Append(",\"duplicateReferences\":").Append(m.DuplicateReferences)
          .Append(",\"textureSourceBytes\":").Append(m.TextureSourceBytes)
          .Append(",\"textureGpuBytes\":").Append(m.TextureGpuBytes)
          .Append(",\"alphaGpuBytes\":").Append(m.AlphaGpuBytes)
          .Append(",\"materialVertexBytes\":").Append(m.MaterialVertexBytes)
          .Append(",\"materials\":").Append(m.Materials)
          .Append(",\"timings\":{");
        int timings = 0;
        AppendTiming(sb, ref timings, "tex0RoundTripMs", m.Tex0RoundTripMs);
        AppendTiming(sb, ref timings, "tex0ParseMs", m.Tex0ParseMs);
        AppendTiming(sb, ref timings, "textureFetchMs", m.TextureFetchMs);
        AppendTiming(sb, ref timings, "textureUploadMs", m.TextureUploadMs);
        AppendTiming(sb, ref timings, "alphaBuildMs", m.AlphaBuildMs);
        AppendTiming(sb, ref timings, "materialBuildMs", m.MaterialBuildMs);
        sb.Append("}}");
    }

    static void AppendTiming(StringBuilder sb, ref int written, string key, double ms)
    {
        if (ms < 0.0)
            return;
        sb.Append(written++ > 0 ? "," : "").Append('"').Append(key).Append("\":")
          .Append(ms.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture));
    }

    /// <summary>What a runtimeState answer carries (ReportRuntimeState).</summary>
    public struct RuntimeReport
    {
        public int LiveMapObjects, LiveModels;   // runtimes alive
        public int ModelFileDataID;              // the model on screen, 0 for none
        public int MapObjectFileDataID;          // the world model on screen, 0 for none
        public bool Loading;                     // a load of either kind in flight
        // Protocol 5: the mount the model on screen rides, and what the host's lifecycle test checks about it.
        public int MountFileDataID;              // 0 for none
        public string MountKey;                  // "" for none
        public int LiveMounts, MountsBuilt;      // mount runtimes alive, and built since the player started
        public int MountSeat, MountSeatBone;     // WmvMountedScene.SeatCase and SeatBone, -1 when not seated
        public int ModelSequence, MountSequence; // what each animator plays, -1 for none
        public int MountEmitters, MountRibbons, MountParticles;   // the mount's emitters as drawn, its live particles
        public int BodyRebinds;                  // WmvCharacterDresser.BodyRebinds of the character on screen, 0 for none
        public int ViewFramings;                 // WmvMain.ViewFramings: times the view was fitted to what is on screen
        public int KeptViews;                    // WmvMain.KeptViews: models put on screen keeping the view (protocol 8)
        public Color32 Background;               // the viewport background the player holds, as displayed (protocol 7)
        // Protocol 12: the terrain tiles alive, the load of the tile on screen (0 for none), a terrain load in flight,
        // the terrain view on, the main camera's culling mask, the Unity Mesh and Material objects alive, and the Meshes,
        // Materials and GameObjects the terrain tiles hold (WmvRuntimeTerrainTile.LiveMeshes and friends; 0 with no tile).
        public int LiveTerrainTiles, TerrainLoad;
        public bool TerrainLoading, TerrainView;
        public int CameraMask;
        public int MeshObjects, MaterialObjects;
        public int TerrainMeshes, TerrainMaterials, TerrainGameObjects;
        // Protocol 12 (additive): the terrain session's request bookkeeping -- the requests waiting for an answer (0 or
        // 1), the ended loads' requests remembered (at most WmvTerrainSession.RetiredCapacity), the map answers it dropped
        // (late and unowned) -- and the load timeout in effect. mapAnswersRouted is the client's own count.
        public int TerrainPendingRequests, TerrainRetiredRequests, MapAnswersDropped, TerrainTimeoutMs;
        // Then, so that each retirement and each drop can be told apart: the requests retired in all (never capped), the
        // id of the one retired last ("" for none), and the dropped answers split into late and unowned (their sum is
        // MapAnswersDropped).
        public int TerrainRetiredTotal, MapAnswersLate, MapAnswersUnowned;
        public string TerrainLastRetiredRequest;
        // Protocol 13: the layer textures the terrain tiles hold, their GPU size (estimated from the formats and mips
        // uploaded) and the tiles' alpha arrays (WmvRuntimeTerrainTile.LiveTextures and friends; 0 with no tile).
        public int TerrainTextures, TerrainAlphaArrays;
        public long TerrainTextureBytes;
        // Protocol 16: the world scene's world models (WmvWorldModels' tallies; 0 with none): instances placed, the unique
        // world models (prototypes) they share, the Unity objects those hold, their GPU bytes, and the group culling as of
        // the last frame.
        public int WorldInstances, WorldModels, WorldMeshes, WorldMaterials, WorldTextures;
        public long WorldGpuGeometryBytes, WorldGpuTextureBytes;
        public int WorldGroups, WorldGroupsVisible, WorldGroupsCulled, WorldInstancesCulled;
        public int WorldLoad;                    // the world load the instances belong to, 0 for none
        public string WorldKind;                 // "tile" | "globalWmo" | "" (no world scene)
        // The world view's frame times: its last frames (at most WmvMain.WorldFrameWindow since it was last entered), their
        // median, 95th percentile and longest, in milliseconds (0 frames: the view was not on).
        public int WorldFrames;
        public double WorldFrameMsMedian, WorldFrameMsP95, WorldFrameMsMax;
        // Protocol 17: the world scene's doodads (WmvWorldDoodads' tallies; 0 with none) and the culling as of the last frame;
        // then every GameObject, Texture2D, Mesh and Material alive in the player (FindObjectsOfTypeAll).
        public int DoodadInstances, DoodadPrototypes, DoodadMeshes, DoodadMaterials, DoodadTextures;
        public long DoodadGpuGeometryBytes, DoodadGpuTextureBytes;
        public int DoodadVisible, DoodadCulled;
        public int LiveGameObjects, LiveTexture2D, LiveMeshes, LiveMaterials;
        // Protocol 18 (streaming, docs/world-viewer/08-streaming.md 1.10): the session -- its tiles, jobs and requests out, what
        // it shares and keeps, what it evicted and asked again -- and Light: the four whole-player object scans above (Mesh,
        // Material, GameObject, Texture2D) skipped, -1 (a runtimeState asked "light": cheap enough to ask often).
        public bool Streaming, Light;
        public int StreamTiles, StreamJobs, StreamRequestsOut, StreamPlacements, StreamDoodads, SceneTerrainTextures;
        public long StreamPrototypesEvicted, StreamTexturesEvicted, StreamFilesAskedAgain, StreamImagesShared;
        // Protocol 19 (docs/world-viewer/09-liquids.md): the liquid objects and meshes alive (WmvLiquidMeshes' tallies: tiles'
        // and world-model prototypes'), the liquid materials and frame arrays alive (WmvLiquidRegistry's), and the GPU bytes
        // of those arrays and of the liquid geometry.
        public int LiquidObjects, LiquidMeshes, LiquidMaterials, LiquidArrays;
        public long LiquidGpuBytes;
    }

    /// <summary>
    /// Answer a runtimeState question with what the player holds now (RuntimeReport). query echoes the question's
    /// number.
    /// </summary>
    public void ReportRuntimeState(int query, RuntimeReport r)
    {
        Send("{\"type\":\"runtimeState\",\"query\":" + query +
             ",\"liveMapObjects\":" + r.LiveMapObjects +
             ",\"liveModels\":" + r.LiveModels +
             ",\"modelFileDataID\":" + r.ModelFileDataID +
             ",\"mapObjectFileDataID\":" + r.MapObjectFileDataID +
             ",\"loading\":" + (r.Loading ? "true" : "false") +
             ",\"mountFileDataID\":" + r.MountFileDataID +
             ",\"mountKey\":\"" + Escape(r.MountKey) + "\"" +
             ",\"liveMounts\":" + r.LiveMounts +
             ",\"mountsBuilt\":" + r.MountsBuilt +
             ",\"mountSeat\":" + r.MountSeat +
             ",\"mountSeatBone\":" + r.MountSeatBone +
             ",\"modelSequence\":" + r.ModelSequence +
             ",\"mountSequence\":" + r.MountSequence +
             ",\"mountEmitters\":" + r.MountEmitters +
             ",\"mountRibbons\":" + r.MountRibbons +
             ",\"mountParticles\":" + r.MountParticles +
             ",\"bodyRebinds\":" + r.BodyRebinds +
             ",\"viewFramings\":" + r.ViewFramings +
             ",\"keptViews\":" + r.KeptViews +
             ",\"backgroundR\":" + r.Background.r +
             ",\"backgroundG\":" + r.Background.g +
             ",\"backgroundB\":" + r.Background.b +
             ",\"assetCacheEntries\":" + cache.Count +
             ",\"assetCacheBytes\":" + cache.Bytes +
             ",\"assetCacheHits\":" + cache.Hits +
             ",\"assetCacheJoins\":" + cache.Joins +
             ",\"assetCacheEvictions\":" + cache.Evictions +
             ",\"assetEpoch\":" + cache.Epoch +
             ",\"liveTerrainTiles\":" + r.LiveTerrainTiles +
             ",\"terrainLoad\":" + r.TerrainLoad +
             ",\"terrainLoading\":" + (r.TerrainLoading ? "true" : "false") +
             ",\"terrainView\":" + (r.TerrainView ? "true" : "false") +
             ",\"cameraMask\":" + r.CameraMask +
             ",\"meshObjects\":" + r.MeshObjects +
             ",\"materialObjects\":" + r.MaterialObjects +
             ",\"terrainMeshes\":" + r.TerrainMeshes +
             ",\"terrainMaterials\":" + r.TerrainMaterials +
             ",\"terrainGameObjects\":" + r.TerrainGameObjects +
             ",\"terrainPendingRequests\":" + r.TerrainPendingRequests +
             ",\"terrainRetiredRequests\":" + r.TerrainRetiredRequests +
             ",\"terrainRetiredTotal\":" + r.TerrainRetiredTotal +
             ",\"terrainLastRetiredRequest\":\"" + Escape(r.TerrainLastRetiredRequest) + "\"" +
             ",\"mapAnswersRouted\":" + MapAnswersRouted +
             ",\"mapAnswersDropped\":" + r.MapAnswersDropped +
             ",\"mapAnswersLate\":" + r.MapAnswersLate +
             ",\"mapAnswersUnowned\":" + r.MapAnswersUnowned +
             ",\"terrainTimeoutMs\":" + r.TerrainTimeoutMs +
             ",\"terrainTextures\":" + r.TerrainTextures +
             ",\"terrainTextureBytes\":" + r.TerrainTextureBytes +
             ",\"terrainAlphaArrays\":" + r.TerrainAlphaArrays +
             ",\"worldInstances\":" + r.WorldInstances +
             ",\"worldModels\":" + r.WorldModels +
             ",\"worldMeshes\":" + r.WorldMeshes +
             ",\"worldMaterials\":" + r.WorldMaterials +
             ",\"worldTextures\":" + r.WorldTextures +
             ",\"worldGpuGeometryBytes\":" + r.WorldGpuGeometryBytes +
             ",\"worldGpuTextureBytes\":" + r.WorldGpuTextureBytes +
             ",\"worldGroups\":" + r.WorldGroups +
             ",\"worldGroupsVisible\":" + r.WorldGroupsVisible +
             ",\"worldGroupsCulled\":" + r.WorldGroupsCulled +
             ",\"worldInstancesCulled\":" + r.WorldInstancesCulled +
             ",\"worldLoad\":" + r.WorldLoad +
             ",\"worldKind\":\"" + Escape(r.WorldKind ?? "") + "\"" +
             ",\"worldFrames\":" + r.WorldFrames +
             ",\"worldFrameMsMedian\":" + r.WorldFrameMsMedian.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
             ",\"worldFrameMsP95\":" + r.WorldFrameMsP95.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
             ",\"worldFrameMsMax\":" + r.WorldFrameMsMax.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
             ",\"doodadInstances\":" + r.DoodadInstances +
             ",\"doodadPrototypes\":" + r.DoodadPrototypes +
             ",\"doodadMeshes\":" + r.DoodadMeshes +
             ",\"doodadMaterials\":" + r.DoodadMaterials +
             ",\"doodadTextures\":" + r.DoodadTextures +
             ",\"doodadGpuGeometryBytes\":" + r.DoodadGpuGeometryBytes +
             ",\"doodadGpuTextureBytes\":" + r.DoodadGpuTextureBytes +
             ",\"doodadVisible\":" + r.DoodadVisible +
             ",\"doodadCulled\":" + r.DoodadCulled +
             ",\"liveGameObjects\":" + r.LiveGameObjects +
             ",\"liveTexture2D\":" + r.LiveTexture2D +
             ",\"liveMeshes\":" + r.LiveMeshes +
             ",\"liveMaterials\":" + r.LiveMaterials +
             ",\"light\":" + (r.Light ? "true" : "false") +
             ",\"streaming\":" + (r.Streaming ? "true" : "false") +
             ",\"streamTiles\":" + r.StreamTiles +
             ",\"streamJobs\":" + r.StreamJobs +
             ",\"streamRequestsOut\":" + r.StreamRequestsOut +
             ",\"streamPlacements\":" + r.StreamPlacements +
             ",\"streamDoodads\":" + r.StreamDoodads +
             ",\"sceneTerrainTextures\":" + r.SceneTerrainTextures +
             ",\"streamPrototypesEvicted\":" + r.StreamPrototypesEvicted +
             ",\"streamTexturesEvicted\":" + r.StreamTexturesEvicted +
             ",\"streamFilesAskedAgain\":" + r.StreamFilesAskedAgain +
             ",\"streamImagesShared\":" + r.StreamImagesShared +
             ",\"liquidObjects\":" + r.LiquidObjects +
             ",\"liquidMeshes\":" + r.LiquidMeshes +
             ",\"liquidMaterials\":" + r.LiquidMaterials +
             ",\"liquidArrays\":" + r.LiquidArrays +
             ",\"liquidGpuBytes\":" + r.LiquidGpuBytes + "}");
    }

    /// <summary>What a screenshotSaved answer carries (ReportScreenshotSaved). Times in milliseconds.</summary>
    public struct ScreenshotReport
    {
        public int Request;
        public bool Ok;
        public string Error;                    // "" when Ok
        public string Path;
        public int Width, Height;
        public long Bytes;                      // the PNG's size on disk, 0 when nothing was written
        public double RenderMs, EncodeMs, WriteMs, TotalMs;
    }

    /// <summary>Answer a captureScreenshot (protocol 6) with what became of it; request echoes the question's number.</summary>
    public void ReportScreenshotSaved(ScreenshotReport r)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"type\":\"screenshotSaved\",\"request\":").Append(r.Request)
          .Append(",\"ok\":").Append(r.Ok ? "true" : "false")
          .Append(",\"error\":\"").Append(Escape(r.Error ?? "")).Append('"')
          .Append(",\"path\":\"").Append(Escape(r.Path ?? "")).Append('"')
          .Append(",\"width\":").Append(r.Width)
          .Append(",\"height\":").Append(r.Height)
          .Append(",\"bytes\":").Append(r.Bytes)
          .Append(",\"renderMs\":").Append(r.RenderMs.ToString("0.#", inv))
          .Append(",\"encodeMs\":").Append(r.EncodeMs.ToString("0.#", inv))
          .Append(",\"writeMs\":").Append(r.WriteMs.ToString("0.#", inv))
          .Append(",\"totalMs\":").Append(r.TotalMs.ToString("0.#", inv))
          .Append('}');
        Send(sb.ToString());
    }

    /// <summary>The 0/1 submesh flags the host sends, as booleans.</summary>
    public static bool[] Flags(int[] values) { return ToBools(values); }

    string NewRequestId() { return "u" + (nextRequestId++); }

    static string Escape(string s)
    {
        var sb = new System.Text.StringBuilder(s?.Length ?? 0);
        foreach (var c in s ?? "")
        {
            if (c == '\\') sb.Append("\\\\");
            else if (c == '"') sb.Append("\\\"");
            else if (c < ' ') sb.Append("\\u").Append(((int)c).ToString("x4"));
            else sb.Append(c);
        }
        return sb.ToString();
    }

    // ---- self-test seams (WmvLifecycleSelfTest.AssetCacheWiringTests): no connection needed ----

    /// <summary>Every line the client would send, before it checks for a connection.</summary>
    internal Action<string> SentForTest;

    /// <summary>A message from the host, as the reader thread would hand it over (assetResponse bytes already
    /// decoded), dispatched now.</summary>
    internal void FeedForTest(string type, string requestId, bool ok, int fileDataID, string path, byte[] bytes,
                              bool cacheable, int assetEpoch, int[] fileDataIDs)
    {
        Dispatch(new Msg
        {
            type = type, requestId = requestId, ok = ok, fileDataID = fileDataID, path = path ?? "", assetBytes = bytes,
            byteLength = bytes != null ? bytes.Length : 0, encoding = "base64", cacheable = cacheable,
            assetEpoch = assetEpoch, fileDataIDs = fileDataIDs,
        });
    }

    /// <summary>One frame's Update: the host's messages, then the asset cache's answers within the budget.</summary>
    internal void PumpForTest() { Update(); }

    /// <summary>One host line exactly as the reader thread would parse it -- JSON through JsonUtility, then the payload it
    /// announced (payloadBytes) made ready as NextMessage makes it -- dispatched now. For the protocol-12 messages
    /// (loadTerrainTile, clearTerrain, an assetResponse with state, detail, readMs and md5) in the self-test.</summary>
    internal void FeedLineForTest(string json, byte[] payload)
    {
        Msg msg = JsonUtility.FromJson<Msg>(json);
        if (msg == null)
            return;
        Prepare(msg, payload);
        Dispatch(msg);
    }

    void Send(string json)
    {
        SentForTest?.Invoke(json);
        lock (sendLock)
        {
            if (stream == null) return;
            try
            {
                var bytes = Encoding.UTF8.GetBytes(json + "\n");
                stream.Write(bytes, 0, bytes.Length);
                stream.Flush();
            }
            catch (Exception e) // IOException; ObjectDisposedException after disconnect; ...
            {
                stream = null;
                Status("send failed: " + e.Message);
            }
        }
    }

    void Status(string s)
    {
        lock (statusQueue) statusQueue.Enqueue(s);
    }

    void OnDestroy()
    {
        stopping = true;
        try { stream?.Close(); } catch { }
        try { client?.Close(); } catch { }
    }
}

/// <summary>
/// THE HOST'S STREAM AS THE PLAYER READS IT (protocol 10). Newline-ended UTF-8 JSON lines, and after a line whose JSON
/// states "payloadBytes": N > 0, exactly N raw bytes and one newline. The reader goes by payloadBytes alone, never by a
/// message's type. A payload not followed by its newline means the stream is out of step: it throws, and the
/// connection ends -- the host then shows the viewport's restart notice -- rather than reading raw bytes as messages.
/// A host before protocol 10 sends lines only, a base64 asset among them tens of megabytes long; they read the same.
/// Reader thread only.
/// </summary>
public class WmvStreamReader
{
    /// <summary>A line longer than this ends the connection. The longest a host writes is a pre-10 base64 answer for
    /// its largest asset (64 MB, so about 86 MB of base64 and its JSON).</summary>
    public const int MaxLineBytes = 192 * 1024 * 1024;
    /// <summary>A payload larger than this ends the connection: twice the host's largest asset (64 MB).</summary>
    public const int MaxPayloadBytes = 128 * 1024 * 1024;

    readonly Stream stream;
    readonly byte[] buffer;
    int pos, len;
    MemoryStream longLine;   // a line that did not fit the buffer, while it is read

    public WmvStreamReader(Stream stream, int bufferSize = 1 << 20)
    {
        this.stream = stream;
        buffer = new byte[Math.Max(16, bufferSize)];
    }

    bool Fill()
    {
        pos = 0;
        len = stream.Read(buffer, 0, buffer.Length);
        if (len < 0) len = 0;
        return len > 0;
    }

    static string Decode(byte[] bytes, int start, int count)
    {
        if (count > 0 && bytes[start + count - 1] == (byte)'\r')
            count--;
        return Encoding.UTF8.GetString(bytes, start, count);
    }

    /// <summary>The next line, without its newline (or a carriage return before it); null at the end of the stream,
    /// where a last line with no newline is still returned.</summary>
    public string ReadLine()
    {
        while (true)
        {
            if (pos >= len && !Fill())
            {
                if (longLine == null)
                    return null;
                string last = Decode(longLine.GetBuffer(), 0, (int)longLine.Length);
                longLine = null;
                return last;
            }
            int newline = Array.IndexOf(buffer, (byte)'\n', pos, len - pos);
            if (newline >= 0)
            {
                string line;
                if (longLine == null)
                    line = Decode(buffer, pos, newline - pos);
                else
                {
                    longLine.Write(buffer, pos, newline - pos);
                    line = Decode(longLine.GetBuffer(), 0, (int)longLine.Length);
                    longLine = null;   // its memory goes with it: one long line does not keep tens of megabytes
                }
                pos = newline + 1;
                return line;
            }
            if (longLine == null)
                longLine = new MemoryStream();
            longLine.Write(buffer, pos, len - pos);
            pos = len;
            if (longLine.Length > MaxLineBytes)
                throw new IOException("a line longer than " + MaxLineBytes + " bytes");
        }
    }

    /// <summary>The payload a line announced: exactly count bytes, then the newline that ends it.</summary>
    public byte[] ReadPayload(int count)
    {
        if (count < 0 || count > MaxPayloadBytes)
            throw new IOException("a payload of " + count + " bytes is refused");
        var data = new byte[count];
        int filled = Math.Min(len - pos, count);
        if (filled > 0)
        {
            Buffer.BlockCopy(buffer, pos, data, 0, filled);
            pos += filled;
        }
        while (filled < count)
        {
            int n = stream.Read(data, filled, count - filled);
            if (n <= 0)
                throw new EndOfStreamException("the stream ended " + (count - filled) + " bytes short of a payload");
            filled += n;
        }
        if (pos >= len && !Fill())
            throw new EndOfStreamException("the stream ended before a payload's newline");
        if (buffer[pos] != (byte)'\n')
            throw new IOException("a payload not followed by its newline: the stream is out of step");
        pos++;
        return data;
    }

    static readonly System.Text.RegularExpressions.Regex PayloadField =
        new System.Text.RegularExpressions.Regex("\"payloadBytes\"\\s*:\\s*(\\d+)");

    /// <summary>The payloadBytes a line states, read without parsing it (a line whose JSON did not parse still has its
    /// payload read, so the stream stays in step); 0 for none.</summary>
    public static int PayloadBytesOf(string line)
    {
        var m = PayloadField.Match(line ?? "");
        int n;
        return m.Success && int.TryParse(m.Groups[1].Value, out n) ? n : 0;
    }
}

/// <summary>
/// THE SESSION ASSET CACHE (protocol 9). The asset files the player was given for its last three model loads, and the
/// ones the host asked to prefetch, kept so a request for one of them is answered here instead of by the host. A
/// character switched to its other model generation and back asks the host for nothing it already sent; the host
/// prefetches the other generation of the character on screen (prefetchAssets), so the first switch does not either.
///
/// What is kept: files of the client's own storage (the host marks them "cacheable"; a custom-folder override is not),
/// keyed by FileDataID and by the paths requests named, under the host's client epoch (assetEpoch): the same
/// FileDataID is another file in another client, so a new epoch empties the cache, and an answer to a request sent
/// under an older epoch is not kept. A host that states no epoch (before protocol 9) gets no caching at all.
///
/// For how long: each model load starts a generation (BeginGeneration); a file is kept while the load that used it is
/// the current one or one of the two before it. Switching A -> B -> A, the third load finds everything A used the
/// first time (its animation files too), and B's files for the switch after that. A prefetched file counts as used by
/// the current load.
///
/// How much: at most Budget bytes. Item, mount and texture browsing on one character starts no new load, so past the
/// budget the least recently used files go -- files of earlier loads first -- but never the model on screen or the
/// model the host prefetched for the next switch, nor the skin and skeleton files fetched with it (Protected).
///
/// Requests already out are flights: a second request for the same file joins the first and is answered with it,
/// so a switch made while its prefetch is still arriving waits for that transfer instead of starting another.
/// Main thread only. The bytes are shared with every consumer, which read them and never write them.
/// </summary>
public class WmvAssetCache
{
    public enum FileKind { Model, Skeleton, Other }

    public class Entry
    {
        public int FileDataID;
        public string Path;
        public byte[] Data;
        public int UsedInGeneration;
        public long LastUse;
        public readonly List<string> Paths = new List<string>();   // the path-index keys that name this entry
    }

    public class Flight
    {
        public string RequestId;
        public int FileDataID;
        public string Path;
        public int Epoch;
        public FileKind Kind;
        public bool Prefetch;
        public readonly List<string> Waiters = new List<string>();
        public string Key;
    }

    /// <summary>
    /// The byte budget. Measured on Classic Beta: a dressed Human male switched HD -> Classic -> HD -> Classic keeps
    /// 150 files, 67 MB (both models, their skins, the item models and textures, the animation files used); this is
    /// about four such working sets. Settable for the self-test.
    /// </summary>
    public long Budget = 256L * 1024 * 1024;

    readonly Dictionary<int, Entry> byId = new Dictionary<int, Entry>();
    readonly Dictionary<string, int> idByPath = new Dictionary<string, int>();
    readonly Dictionary<string, Flight> flightByKey = new Dictionary<string, Flight>();   // what a request can join
    readonly Dictionary<string, Flight> flightById = new Dictionary<string, Flight>();    // what an answer ends
    readonly HashSet<int> protectedIds = new HashSet<int>();   // never evicted for the budget (see Protect)
    long useClock;

    /// <summary>The host's client epoch; 0 (a host before protocol 9) keeps nothing.</summary>
    public int Epoch { get; private set; }
    public int Generation { get; private set; }
    public int Hits { get; private set; }        // requests answered from the files kept
    public int Joins { get; private set; }       // requests that joined one already out
    public int Evictions { get; private set; }   // files dropped for the budget
    public long Bytes { get; private set; }
    public int Count { get { return byId.Count; } }
    public bool Enabled { get { return Epoch > 0; } }

    static string KeyOf(int fileDataID, string path)
    {
        return fileDataID > 0 ? "f:" + fileDataID : "p:" + (path ?? "");
    }

    /// <summary>The epoch the host states. A different one empties the cache; flights already out are still answered,
    /// but nothing joins them and their answers are not kept.</summary>
    public void SetEpoch(int epoch)
    {
        if (epoch == Epoch)
            return;
        byId.Clear();
        idByPath.Clear();
        flightByKey.Clear();
        protectedIds.Clear();
        Bytes = 0;
        Epoch = epoch;
    }

    /// <summary>A model load begins (modelFileDataID, 0 when unknown): the files none of it and the two loads before it
    /// used go. The model it loads is protected from the budget; what the last load protected for its switch is not.</summary>
    public void BeginGeneration(int modelFileDataID)
    {
        Generation++;
        protectedIds.Clear();
        if (modelFileDataID > 0)
            protectedIds.Add(modelFileDataID);
        var gone = new List<int>();
        foreach (Entry e in byId.Values)
            if (e.UsedInGeneration < Generation - 2)
                gone.Add(e.FileDataID);
        foreach (int id in gone)
            Remove(id);
    }

    /// <summary>Keep a file through the budget until the next load (the model prefetched for a switch, and its skin and
    /// skeleton files).</summary>
    public void Protect(int fileDataID)
    {
        if (fileDataID > 0)
            protectedIds.Add(fileDataID);
    }

    void Remove(int fileDataID)
    {
        Entry e;
        if (!byId.TryGetValue(fileDataID, out e))
            return;
        byId.Remove(fileDataID);
        Bytes -= e.Data.Length;
        foreach (string p in e.Paths)
        {
            int id;
            if (idByPath.TryGetValue(p, out id) && id == fileDataID)
                idByPath.Remove(p);
        }
    }

    Entry Find(int fileDataID, string path)
    {
        if (!Enabled)
            return null;
        int id = fileDataID;
        if (id <= 0 && !string.IsNullOrEmpty(path) && !idByPath.TryGetValue(path, out id))
            return null;
        Entry e;
        return byId.TryGetValue(id, out e) ? e : null;
    }

    /// <summary>Whether the file is kept, without using it (no hit, no new stamp).</summary>
    public bool Holds(int fileDataID)
    {
        return Find(fileDataID, null) != null;
    }

    /// <summary>The file for a request, kept for this load too; null when it is not here.</summary>
    public Entry Take(int fileDataID, string path)
    {
        Entry e = Find(fileDataID, path);
        if (e == null)
            return null;
        e.UsedInGeneration = Generation;
        e.LastUse = ++useClock;
        Hits++;
        return e;
    }

    /// <summary>The bytes of a file kept for this load too (a prefetch of a file already here); null when not here.</summary>
    public byte[] Touch(int fileDataID)
    {
        Entry e = Find(fileDataID, null);
        if (e == null)
            return null;
        e.UsedInGeneration = Generation;
        e.LastUse = ++useClock;
        return e.Data;
    }

    public bool InFlight(int fileDataID)
    {
        return flightByKey.ContainsKey(KeyOf(fileDataID, null));
    }

    /// <summary>A request out for the file: requestId will be answered with it. A path the joining request named is
    /// kept with the flight, so the answer is found by it too.</summary>
    public bool Join(int fileDataID, string path, string requestId)
    {
        Flight f;
        if (!Enabled || !flightByKey.TryGetValue(KeyOf(fileDataID, path), out f))
            return false;
        f.Waiters.Add(requestId);
        if (string.IsNullOrEmpty(f.Path) && !string.IsNullOrEmpty(path))
            f.Path = path;
        Joins++;
        return true;
    }

    public Flight BeginFlight(int fileDataID, string path, string requestId, FileKind kind, bool prefetch)
    {
        var f = new Flight
        {
            RequestId = requestId, FileDataID = fileDataID, Path = path, Epoch = Epoch, Kind = kind, Prefetch = prefetch,
            Key = KeyOf(fileDataID, path),
        };
        flightById[requestId] = f;
        if (Enabled)
            flightByKey[f.Key] = f;
        return f;
    }

    /// <summary>The flight an answer ends, or null for an answer to a request the cache never saw.</summary>
    public Flight EndFlight(string requestId)
    {
        Flight f;
        if (requestId == null || !flightById.TryGetValue(requestId, out f))
            return null;
        flightById.Remove(requestId);
        Flight current;
        if (flightByKey.TryGetValue(f.Key, out current) && current == f)
            flightByKey.Remove(f.Key);
        return f;
    }

    void Index(Entry e, string path)
    {
        if (string.IsNullOrEmpty(path))
            return;
        int other;
        if (idByPath.TryGetValue(path, out other) && other != e.FileDataID)
        {
            Entry o;
            if (byId.TryGetValue(other, out o))
                o.Paths.Remove(path);
        }
        idByPath[path] = e.FileDataID;
        if (!e.Paths.Contains(path))
            e.Paths.Add(path);
    }

    /// <summary>Keep a file a flight brought: by its FileDataID, and by the path asked for and the path answered.
    /// Not when the flight was asked under another epoch, nor a file with no FileDataID. Past the budget, the least
    /// recently used files that are not protected go, earlier loads' first.</summary>
    public void Store(Flight f, int fileDataID, string answeredPath, byte[] data)
    {
        if (!Enabled || f == null || f.Epoch != Epoch || fileDataID <= 0 || data == null)
            return;
        Entry old;
        if (byId.TryGetValue(fileDataID, out old))
            Remove(fileDataID);
        var e = new Entry
        {
            FileDataID = fileDataID, Path = string.IsNullOrEmpty(answeredPath) ? f.Path : answeredPath, Data = data,
            UsedInGeneration = Generation, LastUse = ++useClock,
        };
        byId[fileDataID] = e;
        Bytes += data.Length;
        Index(e, f.Path);
        Index(e, answeredPath);
        KeepWithinBudget(fileDataID);
    }

    void KeepWithinBudget(int justStored)
    {
        while (Bytes > Budget)
        {
            Entry victim = null;
            foreach (Entry e in byId.Values)
            {
                if (e.FileDataID == justStored || protectedIds.Contains(e.FileDataID))
                    continue;
                bool older = e.UsedInGeneration < Generation;
                if (victim == null || (older && victim.UsedInGeneration >= Generation) ||
                    (older == (victim.UsedInGeneration < Generation) && e.LastUse < victim.LastUse))
                    victim = e;
            }
            if (victim == null)
                return;   // nothing that may go: what is protected and just stored stays, over the budget
            Remove(victim.FileDataID);
            Evictions++;
        }
    }
}
