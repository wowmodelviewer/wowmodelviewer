/*
 * UnityIpcServer.h
 *
 * Localhost IPC server for the embedded Unity renderer (protocol v17). WMV is the SERVER:
 * UnityRendererHost starts this listener BEFORE launching the player and passes the port on
 * the player's command line (-wmvPort <n>); the player connects back, announces itself with
 * unityReady and then asks WMV for the raw WoW assets/metadata it renders from. This is the
 * channel that lets the Unity viewport render directly from WoW data -- WMV keeps the app UI,
 * the active client/profile, CASC/MPQ access, DB/metadata and the runtime commands; the player
 * only ever sees bytes served here. No files are written to disk; there is no export workflow.
 *
 * Transport: TCP bound to 127.0.0.1 only (ephemeral port), newline-delimited JSON (one
 * object per line, UTF-8). BINARY PAYLOADS (protocol 10): to a player that announced protocol 10, an assetResponse's
 * file and a characterImage's pixels follow their line as raw bytes -- the line states "encoding":"binary" and
 * "payloadBytes": N, and is followed by exactly N bytes and one newline (queueFrame). The player goes by payloadBytes
 * alone, and a payload not followed by its newline ends the connection (out of step). An older player gets them
 * base64-encoded inside the line ("encoding":"base64"), as before.
 *
 * ASSET CACHE (protocol 9). The player keeps the asset files of its last three model loads (and prefetched ones) and
 * answers repeat requests itself: every loadWoWModel states "assetEpoch" (UnityAssetAccess::clientEpoch, changed by
 * ResetClientState), an assetResponse for a file of the client's own storage says "cacheable":true, and
 * prefetchAssets { assetEpoch, fileDataIDs } names the models it is likely to load next -- sent once per load, after a
 * paired character's scene is applied, with its other model generation (ModelViewer::HintVariantPartner). See
 * docs/unity-renderer/README.md, "The asset cache".
 *
 *   player -> WMV
 *     { "type":"unityReady", "protocolVersion":11 }
 *     { "type":"getAsset",             "requestId":"abc123", "path":"creature/chicken/chicken.m2" }
 *     { "type":"getAssetByFileDataID", "requestId":"abc124", "fileDataID":123456 }
 *     { "type":"getModelTextures",     "requestId":"abc125", "fileDataID":123200 }
 *     { "type":"modelGeosetsApplied", "fileDataID":1521037, "revision":7, "status":"applied",
 *       "reason":"", "submeshVisible":[1,0,1], "triangles":2364, "animTimeMs":840 }
 *     { "type":"characterSceneApplied", "fileDataID":1011653, "revision":4, "load":12, "status":"applied",
 *       "reason":"", "merged":3, "attachments":4, "missing":[], "ms":212,
 *       "mountKey":"M3", "mountStatus":"applied", "mountReason":"" }
 *     { "type":"mapObjectLoaded", "fileDataID":115058, "load":13, "status":"built", "reason":"",
 *       "groups":1, "groupFilesRequested":1, "groupFilesMissing":0, "batches":3, "submeshes":3, "renderers":1,
 *       "materials":3, "provisionalMaterials":0, "unresolvedMaterials":0, "blendedMaterials":0,
 *       "texturesReferenced":3, "texturesDecoded":3, "texturesMissing":0, "vertices":1234, "triangles":987,
 *       "boundsMin":[x,y,z], "boundsMax":[x,y,z],
 *       "timings":{"rootMs":5,"groupsMs":40,"texturesMs":60,"buildMs":12,"totalMs":130},
 *       "liveMapObjects":1, "liveModels":0 }
 *     { "type":"runtimeState", "query":3, "liveMapObjects":0, "liveModels":2, "modelFileDataID":1011653,
 *       "mapObjectFileDataID":0, "loading":false, "mountFileDataID":126407, "mountKey":"M3", "liveMounts":1,
 *       "mountsBuilt":1, "mountSeat":1, "mountSeatBone":50, "modelSequence":145, "mountSequence":1,
 *       "mountEmitters":0, "mountRibbons":0, "mountParticles":0, "bodyRebinds":0, "viewFramings":2 }
 *     { "type":"screenshotSaved", "request":1, "ok":true, "error":"", "path":"C:/Shots/bear.png", "width":3840,
 *       "height":2160, "bytes":1545651, "renderMs":53.6, "encodeMs":183.8, "writeMs":4.3, "totalMs":248.1 }
 *   WMV -> player
 *     { "type":"loadWoWModel", "path":"creature/chicken/chicken.m2", "fileDataID":0, "client":"active",
 *       "character":false, "load":12, "kind":"m2" }
 *     { "type":"loadWoWModel", "path":"world/wmo/.../it_trollhouse03.wmo", "fileDataID":115058,
 *       "client":"active", "character":false, "load":13, "kind":"wmo" }
 *     { "type":"runtimeState", "query":3 }
 *     { "type":"captureScreenshot", "request":1, "path":"C:/Shots/bear.png", "width":3840, "height":2160 }
 *     { "type":"assetResponse", "requestId":"abc123", "ok":true, "path":"...", "fileDataID":n,
 *       "byteLength":123456, "encoding":"binary", "payloadBytes":123456, "cacheable":true }
 *       <123456 raw bytes><newline>                                             (protocol 10)
 *     { "type":"assetResponse", "requestId":"abc123", "ok":true, "path":"...", "fileDataID":n,
 *       "byteLength":123456, "sha1":"...", "encoding":"base64", "data":"..." }   (a player before protocol 10)
 *     { "type":"assetResponse", "requestId":"abc123", "ok":false, "error":"not found" }
 *     { "type":"modelTextures", "requestId":"abc125", "ok":true, "fileDataID":123200,
 *       "textures":[ { "index":0, "type":11, "fileDataID":123199, "source":"selection" } ] }
 *     { "type":"modelSkin", "fileDataID":1521037,
 *       "textures":[ { "index":0, "type":11, "fileDataID":1521061, "source":"selection" } ],
 *       "geosets":[ 101 ], "hasGeosets":true,
 *       "hasSubmeshVisible":true, "submeshCount":3, "submeshVisible":[1,1,0] }
 *     { "type":"modelGeosets", "fileDataID":1521037, "revision":7, "submeshCount":3,
 *       "submeshVisible":[1,0,1] }
 *     { "type":"modelAnimation", "fileDataID":1521037, "sequenceIndex":2, "animID":0,
 *       "durationMs":2000, "loop":true }
 *     { "type":"modelAnimation", "fileDataID":126407, "sequenceIndex":1, "animID":0,
 *       "durationMs":4000, "loop":true, "role":"mount", "load":12 }
 *     { "type":"modelAnimationState", "fileDataID":1521037, "sequenceIndex":2, "playing":true,
 *       "timeMs":840, "speed":1.0, "loop":true, "explicitState":false, "sampledAtMs":3629698.6 }
 *     { "type":"modelAnimationState", "fileDataID":126407, "sequenceIndex":1, "playing":true,
 *       "timeMs":1840, "speed":1.0, "loop":true, "explicitState":false, "sampledAtMs":3629698.6,
 *       "load":12, "hasRider":true,
 *       "rider":{ "sequenceIndex":145, "playing":true, "timeMs":840, "speed":1.0, "loop":true } }
 *     { "type":"characterImage", "hash":"body-3", "kind":"body", "width":2048, "height":1024, "format":"bgra8",
 *       "encoding":"binary", "payloadBytes":8388608 }
 *       <8388608 raw BGRA bytes><newline>                                       (protocol 10)
 *     { "type":"characterImage", "hash":"body-3", "kind":"body", "width":2048, "height":1024, "format":"bgra8",
 *       "encoding":"base64", "data":"..." }                                     (a player before protocol 10)
 *     { "type":"characterScene", "fileDataID":1011653, "revision":4,
 *       "body":{ "textures":[ {"slot":0,"type":1,"image":"body-3"}, {"slot":1,"type":6,"fileDataID":1234} ],
 *                "submeshCount":120, "submeshVisible":[1,0,...], "closeRightHand":true, "closeLeftHand":false },
 *       "merged":[ {"key":"m4353217","fileDataID":4353217,"mergeIndex":1,"textures":[...],
 *                   "submeshCount":6,"submeshVisible":[...],"boneMap":[0,1,2,...]} ],
 *       "attachments":[ {"key":"a11:1234567","fileDataID":1234567,"attachmentId":11,"mirrored":false,
 *                        "visible":true,"textures":[...],"submeshCount":2,"submeshVisible":[1,1]} ],
 *       "mount":{ "key":"M3","fileDataID":126407,"path":"creature/warhorse/warhorse.m2","displayID":8469,
 *                 "attachmentId":0,"bone":50,"position":[0.2131,0.0,1.9284],"riderScale":1.0,
 *                 "textures":[ {"slot":...,"type":11,"fileDataID":126406} ],"submeshCount":...,
 *                 "submeshVisible":[...],"sequenceIndex":1,"riderSequenceIndex":145 } }
 *
 * getModelTextures exists because modern M2s do NOT name their replaceable textures (a
 * creature skin's TXID entry is 0 and the texture array carries no filename) -- the skin comes
 * from the client database, which only WMV can read. It returns metadata only; the renderer
 * still fetches the bytes with getAssetByFileDataID.
 *
 * "type" is the WoW texture TYPE the texture feeds (11/12/13 for the three creature skin slots),
 * NOT a position: variation order and M2 texture-slot order need not agree, so the renderer maps
 * the type onto the slots that declare it. "source" says where the answer came from --
 * "selection" is the skin the viewport is showing right now, "database" only the model's default.
 *
 * modelSkin is pushed whenever the displayed skin changes (the dropdown, or the default chosen on
 * model load). Same payload as modelTextures, no request: the player swaps the texture and keeps
 * the mesh it already built.
 *
 * GEOSETS. "geosets" is the id summary the renderer has always taken (non-zero ids on); protocol 2
 * adds "submeshVisible", the host's own display flag for each of the displayed model's submeshes in
 * skin order, which the renderer uses instead when it is present. modelSkin and the modelTextures
 * reply carry both, so a load, a skin change and a reconnect all deliver the current state.
 * modelGeosets carries only the per-submesh state and is pushed when the user switches a geoset
 * (Model > Geosets): no texture or particle-colour lookup, applied to the built mesh at once. The
 * player answers each with modelGeosetsApplied -- "applied" (with what it now draws), "pending"
 * (kept for the model still loading) or "rejected" (with why; nothing changed) -- and sends the same
 * report with revision 0 after a load or a skin push applied a per-submesh state, so the host always
 * knows what the renderer is drawing rather than assuming it.
 *
 * CHARACTERS (protocol 3). A playable character is not one model: it is a body with a host-composited
 * texture, the collection armour and customization parts refreshMerging laid into it, and the item
 * models attached at its attachment points. loadWoWModel carries
 * "character":true for one, and characterScene carries the RESOLVED state of all of it (see
 * UnityCharacterScene.h): per model, the texture each slot binds -- a FileDataID, or an image the host
 * composited -- the geoset display flags, the host's bone table for a merged model, and the attachment
 * id for an attached one. It is sent whole whenever any of that changes (a customization, equipment,
 * a render toggle, a geoset checkbox); the player dresses the body it built from the same load and
 * applies each scene as a whole, and answers characterSceneApplied: "applied" (with the parts it could
 * not build in "missing"), "rejected" (a real refusal: the load failed -- the reason then starts with
 * "load failed" -- the scene is not about the character on screen or being loaded, or it does not fit)
 * or "superseded" (dropped because a newer scene or a new load replaced it, which is answered in its
 * own right; not a failure). "load" in loadWoWModel is the host's load serial, increasing with every
 * load and never 0; each characterSceneApplied names the serial of the load its scene belonged to (0
 * when it belonged to none), so an answer about an earlier load -- often of the same body model, and
 * so the same fileDataID -- is never taken for one about the load on display. characterImage carries a
 * composited image once, before the first scene that names it; a scene naming an image the player
 * already holds does not resend the pixels. Rows are top row first, bytes B,G,R,A -- the memory
 * layout of the QImage the host's GL texture was uploaded from.
 *
 * WORLD MODELS (protocol 4). A WMO travels as a loadWoWModel with "kind":"wmo" naming the ROOT file
 * ("kind" absent, or "m2", is a model as before); the host sends it only to a player that announced
 * protocol 4 or later, and an older player gets the out-of-date notice instead. The player fetches the
 * root, its LOD0 group files (the first MOHD group-count entries of GFID) and the material textures
 * with getAssetByFileDataID -- the host sends no geometry -- and answers each load once with
 * mapObjectLoaded: "built", "failed" (with the reason) or "superseded" (a newer load of either kind
 * replaced it before it was built; not a failure). "load" is the serial of the loadWoWModel it answers.
 * The counts describe what was built (bounds in Unity space; timings in milliseconds), and
 * liveMapObjects / liveModels are the runtime roots alive in the player after the outcome was adopted,
 * which is how a lifecycle test proves nothing leaked or doubled. No skin, animation or geoset message
 * is sent about a WMO: it has none. runtimeState (also protocol 4) is a test's question: the player
 * answers it with the runtimes alive right now and the fileDataID of the model and of the world model on
 * screen (0 for none), "query" echoing the question's number. It is what shows a world model left alive
 * under a model -- a step that ends on a model has no mapObjectLoaded, and the next world-model adoption
 * disposes whatever is left before it counts.
 *
 * MOUNTED CHARACTERS (protocol 5). A character riding a mount is still the character: the host's mount
 * choice hangs the character's node (CharControl::charAtt) under the canvas root and puts the mount on
 * that root and on the canvas, without loading anything. So to a player that announced protocol 5,
 * loadWoWModel keeps naming the RIDER ("character":true), and the mount travels as an optional "mount" in
 * that character's scene, resolved as the host draws it: "key" is "M" and the host's mount serial, raised
 * for every mount model the mount choice installs (never the model's address: the old mount is freed
 * before its replacement is allocated, so an address can repeat), and unchanged when the same mount is
 * described again; "fileDataID" and "path" are the mount model's; "displayID" the CreatureDisplayInfo it
 * was chosen by (0 for a creature file; informational); "attachmentId" is the rider node's id, "bone" and
 * "position" the entry the MOUNT's attachment lookup gives for it (WoWModel::setupAtt: -1 and [0,0,0]
 * when it has none -- the rider then sits at the mount's origin); "riderScale" the rider model's scale;
 * "textures" each mount slot's own binding, where a slot bound to nothing (GL name 0 or none) is left out
 * rather than read as the character's body image; "submeshCount"/"submeshVisible" the mount's own geoset
 * flags; "particleColorSets", present only while the mount's display replaces particle colours, the
 * three ParticleColor sets (emitter ParticleColorIndex 11, 12, 13) as start, mid and end RGB, 0..255, 27
 * numbers in that order; "sequenceIndex" and "riderSequenceIndex" what the mount and the rider are
 * playing when the scene is built -- for the rider, whatever the mount choice selected from its animation
 * lookup. Presence is "key" non-empty (and "fileDataID" > 0 for a mount the player can fetch), never the
 * JSON object being absent. The mount's display state travels only there: no modelSkin or modelGeosets
 * is sent for it while it is ridden. A scene without "mount" while one is ridden is the dismount.
 * characterSceneApplied adds "mountKey" (the key of the mount in the scene it answers, "" for none),
 * "mountStatus" ("applied", "failed" or "none") and "mountReason"; a mount that could not be built is
 * reported there and never turns the character's own answer into "load failed". Two models animate, so
 * modelAnimation adds "role" ("mount" or "rider": which of them the Animation panel changed -- a
 * FileDataID cannot say, a mount model can also be a playable race's body) and "load" (the rider's load
 * serial); modelAnimationState keeps describing the canvas model, the mount, and adds "load", "hasRider"
 * and "rider" (the rider's sequenceIndex, timeMs, speed and loop, with "playing" following the MOUNT's
 * pause, which is the host's clock gate for both). None of these fields is sent about a model that is not
 * ridden, and none of it goes to an older player: it gets the mounted-character notice, as before.
 * runtimeState adds "mountFileDataID", the mount the model on screen rides in the player (0 for none), and
 * what a lifecycle test needs to tell a mount on screen from a stale or doubled one: "mountKey", "liveMounts"
 * (mount runtimes alive, including one built for a scene not applied yet), "mountsBuilt" (since the player
 * started), "mountSeat" and "mountSeatBone" (how the model hangs from it: 0 at the mount's origin, 1 under
 * that bone, 2 on the mount's root at the attachment's position; -1 not seated), "modelSequence" and
 * "mountSequence" (what each animator plays), "mountEmitters", "mountRibbons" and "mountParticles" (the
 * mount's emitters as drawn and its live particles), "bodyRebinds" (how often the character's scenes
 * bound its body textures again) and "viewFramings" (how often the player fitted the view to what is on
 * screen: once per model, world model or mount change, and never for anything else).
 *
 * SCREENSHOTS (protocol 6). captureScreenshot asks the player to render what the viewport shows once more, off screen,
 * at width x height with a transparent background, and to write it as an RGBA PNG to path -- an absolute path the
 * host's Save As dialog chose and confirmed. The pixels never travel over this channel: the player writes the file
 * itself and answers screenshotSaved with the same request number, "ok" and the file's size in bytes, or "ok":false
 * and why ("error"), with how long the render and readback, the PNG encode, the write and the whole capture took (ms).
 * The player's side is Tools/UnityRendererProject/Assets/Scripts/WmvScreenshot.cs. Nothing is sent to an older player.
 *
 * VIEWPORT BACKGROUND (protocol 7). viewportBackground { r, g, b } sets the Models viewport's background: opaque, as
 * the sRGB bytes it is to display as (0..255 each; the player refuses a line with a channel outside that). The player
 * keeps it until the next one -- a model load does not reset it -- and works out the camera clear that shows exactly
 * those bytes (WmvMain.ApplyViewportBackground). The host sends it whenever the user's choice changes and again at every
 * unityReady, and passes it on the player's command line too ("-wmvBackground RRGGBB"), so a started or restarted
 * player shows it from its first frame. runtimeState adds "backgroundR", "backgroundG" and "backgroundB", the colour
 * the player holds. Nothing is sent to an older player, which keeps its own default.
 *
 * NPC EQUIPMENT (protocol 11). An NPC on its own model (not a playable race's body) stays an ordinary loadWoWModel
 * ("character":false); the weapons it holds travel in a characterScene with "attachmentsOnly":true and that load's
 * "load" serial. The player puts only the attachments on the model -- no body textures, merged parts, fist pose or
 * mount -- and answers characterSceneApplied as for a character; an attachment point it cannot resolve is named in
 * "missing" and nothing is left at its key. An older player gets no such scene and shows the NPC without its weapons,
 * as before. See docs/unity-renderer/README.md, "Equipment on exclusive NPC models".
 *
 * TERRAIN (protocol 12). One ADT terrain tile of a map, drawn in a terrain view of its own beside the model session,
 * which it leaves alone (the model on screen stays loaded, hidden). The host names the tile and the map's WDT; the
 * player fetches the WDT and the tile's root ADT and parses them itself -- the host parses neither:
 *   WMV -> player  { "type":"loadTerrainTile", "load":3, "assetEpoch":1, "mapId":0, "mapDirectory":"Azeroth",
 *                    "product":"wow", "build":"12.1.0.69933", "wdtFileDataID":775971, "wdtResolvedBy":"fileDataID",
 *                    "tileX":32, "tileY":50, "maxAssetBytes":4194304 }   ("wdtResolvedBy":"path" adds "wdtPath")
 *                  { "type":"clearTerrain", "reason":"..." }
 *   player -> WMV  { "type":"getMapAsset", "requestId":"u7", "load":3, "assetEpoch":1, "role":"wdt", "fileDataID":775971 }
 *                  { "type":"terrainTileLoaded", "load":3, "status":"built", "reason":"", "mapId":0, "tileX":32, ...,
 *                    "wdtMd5":"...", "rootMd5":"...", "chunks":256, "chunksBuilt":256, "vertices":37120, ...,
 *                    "timings":{...}, "liveTerrainTiles":1, "meshObjects":..., "materialObjects":... }
 * "load" is the host's terrain load serial (its own, not the model's), raised for every loadTerrainTile sent; in this
 * protocol only the latest load is valid. getMapAsset is answered with an assetResponse as for any file (binary frame),
 * plus "role", "load", "assetEpoch", "state" (the map catalogue's availability name, or "stale" / "refused"), "detail",
 * "readMs" and "md5" (of the bytes served); "ok" is true only for "available", every other answer carries the detail
 * as "error", and none is cacheable. The host serves only the current load's WDT, and its root ADT once that WDT was
 * served, read through the client's own storage with the content key checked (WorldMapCatalog::inspectFile, at most
 * maxAssetBytes); it cannot tell whether the root is one the WDT names (it parses no WDT). Every load is answered once:
 * terrainTileLoaded "built" (once the frames after the one that adopted it have begun), "failed" (the reason, and the
 * host's state and detail when an answer made it fail), "superseded" (a newer loadTerrainTile or a loadWoWModel
 * replaced it) or "cleared" (clearTerrain while it was building). clearTerrain also takes a built tile away, unanswered.
 * A loadWoWModel leaves the terrain view first. runtimeState adds "liveTerrainTiles", "terrainLoad" (the load shown, 0
 * for none), "terrainLoading", "terrainView", "cameraMask", "meshObjects", "materialObjects", and what the terrain
 * itself holds: "terrainMeshes" and "terrainMaterials" (the Meshes and Materials of the live terrain runtimes) and
 * "terrainGameObjects" (the GameObjects under the terrain root), each 0 when no tile is shown; and what the player keeps
 * of its map-file requests: "terrainPendingRequests" (0 or 1; protocol 13: up to its outstanding limit),
 * "terrainRetiredRequests" (requests of loads that ended unanswered, at most 32; protocol 13: 128), "terrainRetiredTotal"
 * (every one retired since the player started, not capped),
 * "terrainLastRetiredRequest" (the id retired last, "" for none), "mapAnswersRouted", "mapAnswersDropped" (late or
 * unowned answers), its parts "mapAnswersLate" and "mapAnswersUnowned", and "terrainTimeoutMs". An older player is sent
 * none of it. The host keeps the most recent MAP_ASSET_RECORDS_KEPT getMapAsset records and counts the rest, and
 * remembers the files it read for the loaded client only (MapAssetReadTracker, emptied when the client is replaced):
 * nothing it keeps about map files grows with a player's life.
 *
 * TERRAIN TEXTURES (protocol 13). The tile's texture layers: the tile's tex0 ADT (MAID word 3 of the WDT, a FileDataID in
 * every client) and the BLP textures its MDID names (and, on a map whose WDT sets MPHD flag 0x80, the height textures its
 * MHID names, asked for with the same role "texture" within the same limits), fetched like the WDT and the root -- the
 * host parses neither. To a player that announced protocol 13, loadTerrainTile adds "maxTextureBytes" (8 MiB, per
 * texture file), "maxTextureBytesPerLoad" (128 MiB of texture bytes read for one load) and "maxTexturesPerLoad" (128
 * texture ids);
 * "maxAssetBytes" (4 MiB) stays the limit of the WDT, the root and the tex0. A player before protocol 13 is sent none of
 * them and never asks for the new roles; a protocol-13 player that receives no maxTextureBytes (a protocol-12 host)
 * builds the geometry alone. getMapAsset adds the roles "tex0Adt" and "texture", decided by the host when the request is
 * served: a tex0 ADT only after this load's WDT was answered "available", at most once per load, read at most
 * maxAssetBytes; a texture only after this load's tex0 was answered "available", each id once per load, at most
 * maxTexturesPerLoad ids, and at most maxTextureBytesPerLoad bytes read for the load's textures -- a request is refused
 * before reading once that many were read, and otherwise read with the smaller of maxTextureBytes and what is left (a
 * file over it is "unreadable", the detail naming the limit applied). Every load reads at most 3 + maxTexturesPerLoad
 * files, whatever their roles. After the read the host checks the KIND of the bytes, as it checks a WDT's first chunk:
 * a tex0 ADT starts with the MVER chunk ("REVM" in file byte order) followed, at 8 + MVER's size, by MAMP ("PMAM"); a
 * texture starts "BLP2". A file that fails is answered "unreadable" with what was found ("not a tex0 ADT: second chunk
 * RDHM (hex 52 44 48 4d)", "not a BLP2 texture: ..."), and a tex0 that fails does not count as served. The host cannot
 * tell whether a texture id is one the tex0 names (it parses no tex0): it vouches for the client, the epoch, the load,
 * the order, the kind and the limits. "md5" is the content key the storage read checked every byte against (never a
 * second hash). A terrainTileLoaded for the current load ends that load on the host: its later requests are answered
 * "stale" ("load N was reported <status>") without a read.
 *   terrainTileLoaded adds "requestsRetired" (how many of the load's requests were still out when it ended, each
 *   remembered by the player as retired) and "retiredRequestIds" (their ids, at most the player's outstanding limit), and
 *   "material": { "state" ("textured", "partial", "unavailable", "unsupported"), "reason", "tex0FileDataID", "tex0Bytes",
 *   "tex0Md5", "alphaFormat", "chunkStates" { counters }, "textures" [ { "fileDataID", "role" ("diffuse" or "height":
 *   a layer's texture or its height texture), "state" (the host's state quoted, or the player's own: "no texture id",
 *   "not requested", "md5 mismatch", "decode failed", "transport failure"), "detail", "bytes", "md5", "width", "height",
 *   "format", "mips", "uploadMs" } ] (every distinct texture of the chunks it draws, the ones past maxTexturesPerLoad
 *   "not requested", and one "no texture id" entry for the layers whose MDID entry is 0: at most maxTexturesPerLoad
 *   entries asked for, and at most TERRAIN_REPORT_TEXTURES_MAX in all), "uniqueTextures", "layerReferences",
 *   "duplicateReferences", "textureSourceBytes", "textureGpuBytes", "alphaGpuBytes", "materialVertexBytes",
 *   "materials", "timings" { "tex0RoundTripMs", "tex0ParseMs", "textureFetchMs", "textureUploadMs", "alphaBuildMs",
 *   "materialBuildMs" } }. runtimeState adds "terrainTextures" (live terrain textures), "terrainTextureBytes" (their GPU
 *   size, estimated) and "terrainAlphaArrays".
 *
 * WORLD MODELS IN THE WORLD (protocol 16, docs/world-viewer/06-world-models-in-world.md). A world load places WMOs:
 *   WMV -> player  { "type":"loadWorld", <every field of loadTerrainTile>, "worldModels":true, "globalWmo":false,
 *                    "maxWmoFileBytes":..., "maxWmoBytesPerLoad":..., "maxWmoTextureBytes":..., "maxWmoTextureBytesPerLoad":...,
 *                    "maxWmoTexturesPerLoad":..., "maxWmoModelsPerLoad":..., "maxWmoGroupsPerLoad":...,
 *                    "maxWmoPlacementsPerLoad":... }
 *   player -> WMV  { "type":"worldShown", "load":3, "mapId":0, "kind":"tile"|"globalWmo", "tileX":30, "tileY":47,
 *                    "placements":27, "models":17, "shownMs":..., "originWorld":[X,Y,Z] }
 *                  { "type":"worldProgress", "load":3, "phase":"...", "placements":..., "models":..., "modelsRequested":...,
 *                    "modelsParsed":..., "modelsBuilt":..., "modelsFailed":..., "modelsUnsupported":...,
 *                    "modelsNotRequested":..., "placed":..., "placementsFailed":..., "filesAsked":..., "filesAnswered":...,
 *                    "bytesReceived":..., "elapsedMs":... }
 *                  terrainTileLoaded adds "world":{ "kind", "state":"complete"|"partial"|"none"|"failed"|"ended", "reason",
 *                    "records", "placements", "placed", "failed", "unsupported", "notRequested", "ended", "models",
 *                    "modelsBuilt", "modelsFailed", "modelsNotRequested", "groupFiles", "groupFilesMissing", "textures",
 *                    "texturesDecoded", "texturesMissing", "fileBytes", "textureBytes", "gpuGeometryBytes",
 *                    "gpuTextureBytes", "originWorld", "start", "startPoint", "startReason", "timings":{...},
 *                    "list":[{ uniqueId, fileDataID, records, state, reason, scale, groups, groupsDrawn, position,
 *                    rotation, extents, rendered }] }
 * A tile load ("globalWmo" false) is a loadTerrainTile that also places the world models its obj0 ADT's MODF names; a
 * WMO-only map's load ("globalWmo" true) has no tile: the WDT's one global world model, placed in the global frame. The
 * player asks getMapAsset "obj0Adt" (once, after the WDT; a tile load only), "wmoRoot" (after the obj0, or after the WDT
 * for a WMO-only map -- then only the WDT's world model, which the host names), "wmoGroup" and "wmoTexture" (after a root
 * was served; each id once), within the budgets the load states and the host enforces (MAX_WMO_* below; the bytes of
 * reads in flight reserved, as terrain textures'). worldShown is sent once, when the scene first has something on
 * screen (the tile's commit, or the world model's instance) and the camera the load carried is placed: the host gives
 * the world the viewport then -- the world models may still be loading. The load still gets exactly one
 * terrainTileLoaded, when every placement is settled. A WorldCamera may ask for "start" (cameraStart): the world model's
 * own start point (a WMO-only map entered without a target). inspectWorldModel { inspection, mapId, wmoFileDataID,
 * assetEpoch, md5, wmoPosition, wmoRotation, wmoExtentsMin, wmoExtentsMax, wmoFlags, wmoScale, wmoUniqueId, binary
 * payload: the root } -- a WMO-only map's world model and the WDT record that places it -- is answered by
 * worldModelInspected { inspection, status, reason, mapId, wmoFileDataID, assetEpoch, wmoBytes, md5, groups,
 * interiorGroups, parseMs, rootBox[6], extents[6], groupBoxes (base64 floats, 6 per group), groupFlags (base64 u32) }:
 * the Maps viewer's footprint. mapTilesInspected adds the WDT's global record ("globalRecord", "globalPosition",
 * "globalRotation", "globalExtentsMin", "globalExtentsMax", "globalFlags", "globalScale", "globalUniqueId"). runtimeState
 * adds worldInstances, worldModels, worldMeshes, worldMaterials, worldTextures, worldGpuGeometryBytes,
 * worldGpuTextureBytes, worldGroups, worldGroupsVisible, worldGroupsCulled, worldInstancesCulled, worldLoad, worldKind.
 * An older player is sent loadTerrainTile (no world models: the Stage 05 behaviour) and no inspectWorldModel.
 *
 * DOODADS (protocol 17, docs/world-viewer/07-doodads.md). A world load places the doodads too: the M2 models its obj0
 * ADT's MDDF names and those its world models' doodad sets name. To a player that announced protocol 17, loadWorld adds
 *                  "doodads":true, "maxM2FileBytes":..., "maxM2BytesPerLoad":..., "maxM2TextureBytes":...,
 *                  "maxM2TextureBytesPerLoad":..., "maxM2ModelsPerLoad":..., "maxM2TexturesPerLoad":...,
 *                  "maxDoodadInstancesPerLoad":...
 * (MAX_M2_* and MAX_DOODAD_INSTANCES_PER_LOAD below), and getMapAsset adds the roles "m2Model" (once the load's obj0 or
 * one of its world-model roots was served: a WMO-only map's doodads come from its world model's sets alone), "m2Skin"
 * and "m2Texture" (once a doodad model was served), each id once per load, at most maxM2ModelsPerLoad models, as many
 * skins and maxM2TexturesPerLoad textures; model and skin bytes within maxM2BytesPerLoad, texture bytes within
 * maxM2TextureBytesPerLoad (the reads in flight reserved); the doodad roles' reads capped apart from the terrain's and the
 * world models' (2 x maxM2ModelsPerLoad + maxM2TexturesPerLoad). The KIND check: a doodad model starts "MD21" with "MD20"
 * at byte 8 (a bare MD20 file is refused: the player asks for a model's skin and textures by the FileDataIDs of its SFID
 * and TXID chunks, which only the chunked form carries), a skin "SKIN", a texture "BLP2". The host parses
 * no MDDF and no MODD, so it cannot tell whether a model is one they name: as for groups, it vouches for the order, the
 * kind, each id once and the budgets. The instance budget is the player's alone (the host never sees a placement).
 *   player -> WMV  worldProgress adds "doodadPhase" ("none", "waiting", "discovering", "requesting", "building", "placing",
 *                    "complete", "partial", "failed", "ended", "notOffered"), "doodadInstances", "doodadPlaced",
 *                    "doodadModels", "doodadModelsBuilt", "doodadModelsFailed", "doodadFilesAsked", "doodadFilesAnswered",
 *                    "doodadBytes" (each -1 when doodads were not offered)
 *                  terrainTileLoaded's "world" adds "assembly" ("complete", "partial", "none", "failed", "ended": the whole
 *                    world, its world models and its doodads; "state" stays the world models' own) and "doodads":{ "state"
 *                    ("complete", "partial", "none", "failed", "ended", "notOffered"), "reason", "mddfRecords",
 *                    "mddfPlacements", "wmoDoodadRecords", "wmoPlacementsWithDoodads", "sets":[{ uniqueId, wmo, set,
 *                    setName, setCount, active, reason }] (at most 64), "instances", "placed", "failed", "unsupported",
 *                    "notRequested", "ended", "models", "modelsBuilt", "modelsFailed", "modelsUnsupported",
 *                    "modelsNotRequested", "modelsPartial", "textures", "texturesDecoded", "texturesMissing", "textureUses",
 *                    "textureUploads", "fileBytes", "textureBytes", "gpuGeometryBytes", "gpuTextureBytes", "timings":{
 *                    discoverMs, firstVisibleMs, fetchMs, parseMs, decodeMs, buildMs, buildMaxMs, placeMs, placeMaxMs,
 *                    readyMs }, "list":[{ fileDataID, state, reason, instances, placed }] (at most 128: every model not
 *                    built first, then the most placed) }
 * runtimeState adds doodadInstances, doodadPrototypes, doodadMeshes, doodadMaterials, doodadTextures,
 * doodadGpuGeometryBytes, doodadGpuTextureBytes, doodadVisible, doodadCulled, and the scene's whole tallies
 * liveGameObjects, liveTexture2D, liveMeshes, liveMaterials. A protocol-16 player is sent the protocol-16 loadWorld (no
 * doodad field) and every doodad role is refused for its load; a protocol-17 player sent no doodad budgets (a protocol-16
 * host) loads the world as before and reports its doodads "notOffered".
 *
 * THE MAP ASSET QUEUE (protocol 12 and 13, every role). getMapAsset requests are served in the order they came, from one
 * queue, at most MAP_ASSET_QUEUE_MAX waiting (one beyond is answered "refused", "too many map requests waiting", without
 * a read). A request is decided and accounted only when it is served, so one whose load ended while it waited is
 * answered "stale" without a read. THE MAP ASSET WORKER (docs/world-viewer/04b-asset-serving.md) reads the files: no
 * storage read runs on this thread. A request the viewer lets through to a read goes to the worker, at most
 * MAP_ASSET_READS_IN_FLIGHT at a time and none while more than MAP_ASSET_SEND_HIGH_WATER bytes wait to be sent; its
 * result is taken back in a later poll (the one its result wakes: wakeOnMapResults), checked again by the viewer (the
 * client, the load: a read whose load or client has gone is answered "stale", never sent), and answered. Each poll
 * takes results back until MAP_ASSET_POLL_BUDGET_BYTES were read for them, or one file over MAP_ASSET_LARGE_FILE_BYTES
 * was; the rest waits for the next poll, which another wake brings at once. Answers can leave in another order than
 * the requests came (a refusal decided while reads are in flight): the player matches them
 * by requestId. The queue is dropped, never answered, wherever held requests are (a new connection, a dropped client,
 * start, stop), and so are the reads in flight of that connection (their results are dropped when taken back). Each
 * answer's record states queuedMs (from its arrival to its dispatch), handlerMs (the GUI thread's time on it: its
 * dispatch and its answer), readMs (the worker's read), the worker's outcome and the timings of every step; the most
 * recent MAP_ASSET_POLL_TIMINGS_KEPT polls that served any are kept with their GUI-thread time.
 *
 * modelAnimation is pushed the same way whenever the animation on display changes, and once after
 * loadWoWModel so the player starts on the animation the app is showing rather than on its own
 * idle. "sequenceIndex" is what the player must act on: it indexes the model's animation table,
 * which is both how the keyframes are stored and how the app's own selector identifies a choice.
 * Two sequences routinely share an "animID" (sub-animations of one action), so the id alone
 * cannot pick one; it is carried for the log and for recognising the idle (animID 0, "Stand").
 *
 * modelAnimationState carries the PLAYBACK state of that animation: whether it is running, how
 * fast, and where in the sequence it currently is. Unlike the skin and the animation choice there
 * is no single funnel for it -- play/pause, the speed slider, the frame slider, stop, step and
 * clear each change it, and the time advances every frame with no control involved at all -- so it
 * is pushed on every control change AND on a slow heartbeat while playing. The heartbeat is the
 * only correction channel for clock drift between two independently-timed renderers; the player
 * decides whether a given "timeMs" is worth snapping to, because only it knows where its own
 * clock is. "sampledAtMs" is when the host sampled the state, in milliseconds of the system's performance
 * counter (QueryPerformanceCounter), which the player reads too: a line that waited behind other traffic -- a
 * composited image is megabytes -- is moved on by that wait before it is applied.
 *
 * Implementation: plain Winsock2, non-blocking, polled from the GUI thread by a wxTimer (the
 * app has no Qt event loop, so QTcpServer signals would never fire; and GAMEDIRECTORY must be
 * used from the GUI thread anyway). One client at a time (the embedded player). Windows only,
 * like the host itself. The map asset worker is the one other thread: it reads map files through
 * its own reference to the client's storage and hands results back; it touches nothing here but
 * the wake it posts (wakeOnMapResults: a wxThreadEvent, so a result is answered at once, not at
 * the next tick).
 */

#ifndef UNITYIPCSERVER_H
#define UNITYIPCSERVER_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <atomic>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "MapAssetWorker.h"
#include "UnityAssetAccess.h"

// The FileDataIDs getMapAsset has read from the loaded client's storage (UnityIpcServer::MapAssetServed::first): the
// first read of each is measured apart from the rest. It holds the ids of ONE client epoch (another client's file of the
// same id is other bytes in another storage). When the client is replaced the host empties it at once (reset, from
// ModelViewer::ResetClientState through UnityIpcServer::noteClientReplaced), so nothing of a client that is gone is
// kept. As a backstop a read under an epoch other than its own (one moved on without that call) empties it first too,
// so first reads never mix clients. At most capacity ids: once it is full a new id is not tracked -- Untracked, never a
// first read it may not be -- and counted.
class MapAssetReadTracker
{
public:
  enum class Read { First, Again, Untracked };
  explicit MapAssetReadTracker(size_t capacity) : m_capacity(capacity) {}
  // A file read under epoch: First the first time since the epoch began, Again after that, Untracked when it is not
  // tracked and the tracker is full.
  Read note(int epoch, int fileDataID);
  // The client was replaced, the new one's epoch is epoch: every id is forgotten now, not at the next read. The count of
  // untracked reads is kept.
  void reset(int epoch);
  int epoch() const { return m_epoch; }                   // the epoch of the last reset or read, whichever came later
                                                          // (0: neither yet)
  size_t size() const { return m_ids.size(); }
  bool holds(int fileDataID) const { return m_ids.count(fileDataID) != 0; }
  size_t capacity() const { return m_capacity; }
  long long untracked() const { return m_untracked; }     // reads left untracked for the capacity, every epoch
private:
  size_t m_capacity;
  int m_epoch = 0;
  std::unordered_set<int> m_ids;
  long long m_untracked = 0;
};

class UnityIpcServer : public wxEvtHandler
{
public:
  static const int PROTOCOL_VERSION = 18;
  // The largest map file (a WDT, a root ADT or a tex0 ADT) served to getMapAsset, and the limit loadTerrainTile states to
  // the player (TERRAIN above): 4 MiB. The size census in docs/world-viewer/03-terrain-geometry.md ("Asset sizes") puts
  // the largest root ADT of the four audited clients at 1,462,916 bytes (FileDataID 811303, map 1051 tile 31_29, Retail
  // and MoP) and the largest WDT of the audited maps at 294,988 (Retail Azeroth, 775971); the largest tex0 ADT named by
  // any MAID tile is 3,116,288 bytes (Retail 6387900, map 2736 tile 27_25).
  static const int MAX_MAP_ASSET_BYTES = 4 * 1024 * 1024;
  // TERRAIN TEXTURES (protocol 13): one texture file at most 8 MiB (the largest terrain BLP of any client is 5,593,604
  // bytes, 2048 x 2048 with all 12 mips, 62 Retail ids); a load at most 128 texture ids and 128 MiB of texture bytes read
  // (no real tile names more than 29 diffuse textures or 45.5 MB of them; with height textures 58 and 90.9 MB).
  static const int MAX_TERRAIN_TEXTURE_BYTES = 8 * 1024 * 1024;
  static const int MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD = 128 * 1024 * 1024;
  static const int MAX_TERRAIN_TEXTURES_PER_LOAD = 128;
  // WORLD MODELS IN THE WORLD (protocol 16): the budgets a world load states and the host enforces, from the census in
  // docs/world-viewer/06-world-models-in-world.md section 6 (the four clients' tile 30_47 and the WMO-only maps 34 and
  // 429): one root or group file at most 4 MiB (the largest group read is 2,432,598 bytes, Classic Beta 7962574's; the
  // largest root 438,824), a load's root and group bytes at most 128 MiB (the most is about 53 MB, Classic Beta 30_47);
  // one texture at most 8 MiB (the largest is 2,098,324), a load's texture bytes at most 128 MiB (the most is about 48 MB,
  // Retail 30_47, counted per world model) and 1,024 texture ids (the most is 727); 64 unique world models (the most is
  // 17), 1,024 group files (the most is 306, Classic Era's Stormwind) and 256 placements (the most is 27).
  static const int MAX_WMO_FILE_BYTES = 4 * 1024 * 1024;
  static const int MAX_WMO_BYTES_PER_LOAD = 128 * 1024 * 1024;
  static const int MAX_WMO_TEXTURE_BYTES = 8 * 1024 * 1024;
  static const int MAX_WMO_TEXTURE_BYTES_PER_LOAD = 128 * 1024 * 1024;
  static const int MAX_WMO_TEXTURES_PER_LOAD = 1024;
  static const int MAX_WMO_MODELS_PER_LOAD = 64;
  static const int MAX_WMO_GROUPS_PER_LOAD = 1024;
  static const int MAX_WMO_PLACEMENTS_PER_LOAD = 256;
  // DOODADS (protocol 17): the budgets a world load states to a player that places doodads, and the host enforces (but the
  // instances: the host never sees a placement), from the census in docs/world-viewer/07-doodads.md section 4 (the four
  // clients' tiles 30_47 and 31_48, the WMO-only maps 34 and 429): one model or skin file at most 8 MiB (the largest M2
  // read is 2,423,632 bytes, the largest skin 214,960, both Retail), a load's model and skin bytes at most 128 MiB (the
  // most is 16.4 MB, Retail 30_47); one texture at most 8 MiB (the largest is 1,399,300, Retail), a load's texture bytes
  // at most 128 MiB (the most is 58.6 MB, Retail 30_47); 1,024 unique models, and as many skins (the most is 616 models,
  // Retail 30_47), 2,048 texture ids (the most is 578) and 16,384 instances (the most is 6,538, Classic Beta 30_47).
  static const int MAX_M2_FILE_BYTES = 8 * 1024 * 1024;
  static const int MAX_M2_BYTES_PER_LOAD = 128 * 1024 * 1024;
  static const int MAX_M2_TEXTURE_BYTES = 8 * 1024 * 1024;
  static const int MAX_M2_TEXTURE_BYTES_PER_LOAD = 128 * 1024 * 1024;
  static const int MAX_M2_MODELS_PER_LOAD = 1024;
  static const int MAX_M2_TEXTURES_PER_LOAD = 2048;
  static const int MAX_DOODAD_INSTANCES_PER_LOAD = 16384;
  // THE MAP ASSET QUEUE: requests waiting at most; the bytes a poll reads before it stops serving; the size over which a
  // file read ends its poll's serving; the polls whose timing is kept.
  static const int MAP_ASSET_QUEUE_MAX = 16;
  static const int MAP_ASSET_POLL_BUDGET_BYTES = 2 * 1024 * 1024;
  // The viewer's own results a poll takes back at most (the Maps viewer's minimap pictures: small, many, each handed to
  // the viewer): the rest waits for the next poll, so a run of them never holds this thread (the wake burst rule then
  // lets the window paint between runs).
  static const int MAP_VIEWER_RESULTS_PER_POLL = 8;
  static const int MAP_ASSET_LARGE_FILE_BYTES = 1024 * 1024;
  static const int MAP_ASSET_POLL_TIMINGS_KEPT = 256;
  // The getMapAsset records kept (mapAssetsServed): the most recent 256; the rest are counted, not kept.
  static const int MAP_ASSET_RECORDS_KEPT = 256;
  // The FileDataIDs the read tracker holds for one client epoch (MapAssetReadTracker).
  static const int MAP_ASSET_READS_TRACKED = 8192;
  // The getMapAsset requests the test hook holds unanswered at most (testHoldNextMapAsset).
  static const int MAP_ASSETS_HELD_MAX = 8;
  // THE MAP ASSET WORKER (docs/world-viewer/04b-asset-serving.md): reads dispatched to it and not yet answered, at most
  // (the worker reads one while the next waits, so it is not idle between polls; the bytes they hold are at most this
  // many times a read's limit); and the bytes waiting to be sent above which no read is dispatched (a player that does
  // not read stops the reads).
  static const int MAP_ASSET_READS_IN_FLIGHT = 2;
  static const int MAP_ASSET_SEND_HIGH_WATER = 16 * 1024 * 1024;
  // THE WAKE: wake polls that follow one another (each starting within MAP_ASSET_WAKE_BURST_GAP_US of the last one's
  // end: one pass of the event queue) spend at most about MAP_ASSET_WAKE_BURST_MS of the GUI thread; the wakes after
  // that leave their results to the timer's poll, so the window gets its turn.
  static const int MAP_ASSET_WAKE_BURST_MS = 8;
  static const int MAP_ASSET_WAKE_BURST_GAP_US = 1000;
  // THE TRANSPORT CAP (docs/world-viewer/08-streaming.md 1.5), for every message, not only map files. Above
  // TRANSPORT_RECEIVE_PAUSE_BYTES waiting to be sent nothing more is received -- no request can add an answer, and TCP
  // pushes back on the client -- until it falls below TRANSPORT_RECEIVE_RESUME_BYTES. A client with more than
  // TRANSPORT_HARD_CAP_BYTES waiting, or whose bytes waited TRANSPORT_STALL_DROP_MS with no send making progress, does
  // not read and is dropped: the message that would pass the cap is not queued, nothing more goes to that connection,
  // and the drop happens at the next safe point of a poll. A poll handles at most TRANSPORT_LINES_PER_POLL lines; the
  // rest wait in the receive buffer for the next poll.
  static const long long TRANSPORT_RECEIVE_PAUSE_BYTES = 32LL * 1024 * 1024;
  static const long long TRANSPORT_RECEIVE_RESUME_BYTES = 8LL * 1024 * 1024;
  static const long long TRANSPORT_HARD_CAP_BYTES = 256LL * 1024 * 1024;
  static const int TRANSPORT_STALL_DROP_MS = 30000;
  static const int TRANSPORT_LINES_PER_POLL = 64;
  // STREAMING (protocol 18, docs/world-viewer/08-streaming.md 1.7): the region the player keeps around its camera -- the
  // tiles within STREAM_RADIUS of the camera's tile wanted, kept while within STREAM_KEEP_RADIUS, at most
  // STREAM_MAX_RESIDENT_TILES resident -- the requests it keeps out at once for the whole session (STREAM_REQUEST_CREDIT),
  // and the tile loads the host keeps open at most. While a session lasts the map asset queue holds STREAM_QUEUE_MAX
  // requests (more than the credit: a request of the shipped player is never refused for room) and STREAM_READS_IN_FLIGHT
  // reads are in flight; a single-tile load keeps MAP_ASSET_QUEUE_MAX and MAP_ASSET_READS_IN_FLIGHT. A request's
  // "priority" orders the queue (smaller first; equal ones first come first served).
  static const int STREAM_RADIUS = 1;
  static const int STREAM_KEEP_RADIUS = 2;
  static const int STREAM_MAX_RESIDENT_TILES = 16;
  static const int STREAM_REQUEST_CREDIT = 24;
  static const int STREAM_MAX_TILE_LOADS = 24;
  static const int STREAM_QUEUE_MAX = 64;
  static const int STREAM_READS_IN_FLIGHT = 4;

  UnityIpcServer();
  ~UnityIpcServer();

  // Bind 127.0.0.1 on an ephemeral port and start polling. False (with a logged reason) if
  // the socket cannot be created; the host then launches the player without IPC.
  bool start();
  void stop();

  bool isListening() const { return m_listen != 0; }
  // THE VIEWER'S SHUTDOWN (docs/world-viewer/04b-asset-serving.md 18.1, docs/world-viewer/05-maps-viewer-ui.md): the
  // first thing ModelViewer's destructor does. The map asset worker's result notifier is cleared under the worker's lock
  // (no result made from now on -- the read stop() lets finish among them -- posts a wake), the timer stops, the map
  // callbacks into the viewer are cleared, and every poll from now on returns at its first check, whoever calls it: the
  // timer, a wake already queued (run by wx's message hook from a pump later in the destructor), a pump that polls by
  // itself. Nothing is accepted, received (no player message handled), dispatched, completed or sent any more. The
  // connection stays open until stop(): UnityRendererHost::shutdown closes the player first. For good: start() does not
  // undo it.
  void detachForShutdown();
  bool detachedForShutdown() const { return m_detached; }
  // What this server's polls did since it was made (the -terraintile harness's shutdown check compares two samples):
  // polls that got past their check -- each goes on to accept, receive, serve the map queue and send --, player messages
  // handled, map callbacks run (onDispatchMapAsset, onCompleteMapAsset, onMapAssetStale, onMapAssetWait) and wakes
  // handled (onMapResultWake, whether or not its poll served).
  struct Activity
  {
    long long pollsServed = 0;
    long long messagesHandled = 0;
    long long mapCallbacks = 0;
    long long wakesHandled = 0;
  };
  const Activity & activity() const { return m_activity; }
  // A wake waits in the event queue (posted by the worker's result notifier, not handled yet).
  bool wakeWaiting() const { return m_wakePosted.load(); }
  bool isConnected() const { return m_client != 0; }
  bool isUnityReady() const { return m_unityReady; }
  int  port() const { return m_port; }
  // The protocol the connected player announced in unityReady (0 until then). A player older than
  // protocol 2 cannot switch submeshes: sendModelGeosets sends it nothing, since it would never answer.
  int  playerProtocolVersion() const { return m_playerProtocol; }
  bool playerSwitchesSubmeshes() const { return m_client && m_unityReady && m_playerProtocol >= 2; }
  // The player can dress a character from a characterScene (protocol 3).
  bool playerDressesCharacters() const { return m_client && m_unityReady && m_playerProtocol >= 3; }
  // The player draws world models: it takes loadWoWModel "kind":"wmo" and answers mapObjectLoaded (protocol 4).
  bool playerDrawsMapObjects() const { return m_client && m_unityReady && m_playerProtocol >= 4; }
  // The player seats a character on a mount: it takes a characterScene's "mount", the role and rider fields of
  // the animation pushes, and answers the mount in characterSceneApplied (protocol 5; MOUNTED CHARACTERS above).
  bool playerRidesMounts() const { return m_client && m_unityReady && m_playerProtocol >= 5; }
  // The player writes viewport screenshots: it takes captureScreenshot and answers screenshotSaved (protocol 6).
  bool playerTakesScreenshots() const { return m_client && m_unityReady && m_playerProtocol >= 6; }
  // The player clears the Models viewport to the colour the host sends: it takes viewportBackground (protocol 7).
  bool playerPaintsBackground() const { return m_client && m_unityReady && m_playerProtocol >= 7; }
  // Protocol 8: loadWoWModel can ask the player to keep its view (a character rebuilt on its other model generation).
  bool playerKeepsView() const { return m_client && m_unityReady && m_playerProtocol >= 8; }
  // Protocol 9: the player keeps the asset files of its last two loads (and prefetched ones) under the client epoch
  // loadWoWModel states, and takes prefetchAssets.
  bool playerCachesAssets() const { return m_client && m_unityReady && m_playerProtocol >= 9; }
  // Protocol 10: the player reads binary payloads (TRANSPORT above), so files and images go raw, not as base64.
  bool playerTakesBinaryFrames() const { return m_client && m_unityReady && m_playerProtocol >= 10; }
  // Protocol 11: the player puts an ordinary NPC model's weapons on it from a characterScene "attachmentsOnly" (NPC
  // EQUIPMENT above).
  bool playerAttachesNpcEquipment() const { return m_client && m_unityReady && m_playerProtocol >= 11; }
  // Protocol 12: the player draws a terrain tile: it takes loadTerrainTile and clearTerrain, asks getMapAsset and answers
  // terrainTileLoaded (TERRAIN above).
  bool playerDrawsTerrain() const { return m_client && m_unityReady && m_playerProtocol >= 12; }
  // Protocol 13: the player textures a terrain tile: loadTerrainTile states the texture limits, and it asks for the tile's
  // tex0 ADT and textures (getMapAsset "tex0Adt", "texture") and reports its material (TERRAIN TEXTURES above).
  bool playerDrawsTerrainTextures() const { return m_client && m_unityReady && m_playerProtocol >= 13; }
  // Protocol 14: the Maps viewer (docs/world-viewer/05-maps-viewer-ui.md). The player reads a WDT's tile table with its
  // verified parser (inspectMapTiles -> mapTilesInspected; the host never parses a WDT) and draws terrain in the view the
  // host chooses, the tile on screen built again in it from what it holds (setTerrainView -> terrainViewApplied).
  bool playerInspectsMapTiles() const { return m_client && m_unityReady && m_playerProtocol >= 14; }
  // Protocol 15: the Maps viewer's overhead map (the minimap ids in the tile table) and the world view's free camera.
  bool playerFliesWorld() const { return m_client && m_unityReady && m_playerProtocol >= 15; }
  // Protocol 16: world models in the world (WORLD MODELS IN THE WORLD above): loadWorld, worldShown, worldProgress,
  // inspectWorldModel, the world-model getMapAsset roles and the camera's start point.
  bool playerPlacesWorldModels() const { return m_client && m_unityReady && m_playerProtocol >= 16; }
  // Protocol 17: doodads in the world (DOODADS above): loadWorld's doodad budgets, the doodad getMapAsset roles, and the
  // doodads in worldProgress, terrainTileLoaded and runtimeState.
  bool playerPlacesDoodads() const { return m_client && m_unityReady && m_playerProtocol >= 17; }
  // Protocol 18 (docs/world-viewer/08-streaming.md): the player streams the tiles around its camera in a world load that
  // asks for it (loadWorld "streaming"), opening and closing tile loads of its own (streamTile) and reporting the region
  // (streamState). An older player gets the single-tile loadWorld, as before.
  bool playerStreams() const { return m_client && m_unityReady && m_playerProtocol >= 18; }

  // Runtime command: tell the player which model is active. Either path or fileDataID may be
  // empty/0. Queued if the player is connected; dropped (logged) otherwise.
  // character: the model is a playable character, dressed by the characterScene that follows.
  // load: the host's load serial for this load (> 0), which the player's characterSceneApplied and
  // mapObjectLoaded echo.
  // kind: "m2" (a model) or "wmo" (a world model ROOT, fileDataID required; see WORLD MODELS above).
  // Callers send "wmo" only when playerDrawsMapObjects().
  // keepView (protocol 8): the model replaces the one on screen without moving the view -- a character rebuilt on
  // its other model generation. Sent only when true; callers set it only when playerKeepsView().
  // Every loadWoWModel also states "assetEpoch" (protocol 9, UnityAssetAccess::clientEpoch()): the player keeps asset
  // files under it and empties its cache when it changes; an older player skips the name.
  void sendLoadWoWModel(const QString & path, int fileDataID, const QString & client = QStringLiteral("active"),
                        bool character = false, int load = 0, const QString & kind = QStringLiteral("m2"),
                        bool keepView = false);

  // Runtime command: the resolved state of the character on display (UnityCharacterScene::build).
  // False when the player cannot dress characters or nothing was sent.
  bool sendCharacterScene(int m2FileDataID, int revision, const QJsonObject & scene);

  // The id a composited image travels under. kind names the image's role ("body", "eyes"); while
  // its pixels are the ones last sent under that kind the same id comes back and nothing is sent,
  // otherwise the image goes out (characterImage) under a new id first.
  QString shareCharacterImage(const QString & kind, const QImage & image);

  // Runtime command: the skin on display changed. Resolves the model's textures the same way
  // getModelTextures does -- so the push and the reply can never disagree -- and sends them
  // unasked. No-op when the player is not connected, or when nothing can be resolved.
  void sendModelSkin(int m2FileDataID);

  // Runtime command: the animation on display changed. sequenceIndex indexes the model's
  // animation table (what the app's own selector picks); animID and durationMs come from that
  // entry. No-op when the player is not connected.
  // role: for a ridden mount only (protocol 5), "mount" or "rider" -- which of the two models the selection
  // changed -- sent with load, the rider's load serial. Empty: neither field is sent, as before.
  void sendModelAnimation(int m2FileDataID, int sequenceIndex, int animID, int durationMs, bool loop,
                          const QString & role = QString(), int load = 0);

  // The rider's playback, sent inside a ridden mount's modelAnimationState (protocol 5).
  struct RiderState
  {
    int sequenceIndex = -1;
    bool playing = false;          // the mount's pause gate, not the rider's own flag (MOUNTED CHARACTERS)
    int timeMs = 0;
    float speed = 1.0f;
    bool loop = true;
  };

  // Runtime command: the playback state of that animation changed (or a heartbeat while it runs).
  // timeMs is the app's current position in the sequence. explicitState is true for a push made
  // by a control -- play, pause, a frame step, the frame or speed slider, the start of a load --
  // and false for the heartbeat: the player applies an explicit position as given, however small
  // the step, and holds heartbeats to a dead band so transport jitter cannot make it twitch.
  // rider: while the model is a ridden mount (protocol 5), the rider's state, sent as "rider" with "hasRider"
  // and load, the rider's load serial; null sends the message exactly as for any other model.
  // No-op when the player is not connected.
  void sendModelAnimationState(int m2FileDataID, int sequenceIndex, bool playing, int timeMs,
                               float speed, bool loop, bool explicitState,
                               const RiderState * rider = nullptr, int load = 0);

  // Runtime command: the displayed model's per-submesh display state changed (a Geosets
  // checkbox). Sends the WHOLE current state (UnityAssetAccess::displayedSubmeshVisibility), numbered
  // by revision so the player's modelGeosetsApplied answer can be matched to it. False (nothing
  // sent) when the player is not connected and ready, speaks a protocol older than 2, or
  // m2FileDataID is not the canvas model.
  bool sendModelGeosets(int m2FileDataID, int revision);

  // The player's answer to modelGeosets, or its report after a load / skin push (revision 0).
  struct GeosetAck
  {
    int fileDataID = 0;
    int revision = 0;
    QString status;              // "applied" / "pending" / "rejected"
    QString reason;
    bool hasVisible = false;
    std::vector<bool> visible;   // what the renderer now switches on, per skin submesh
    int triangles = 0;
    long long animTimeMs = 0;
  };
  // Raised on the GUI thread for every modelGeosetsApplied.
  std::function<void(const GeosetAck &)> onGeosetsApplied;

  // The player's answer to a characterScene.
  struct SceneAck
  {
    int fileDataID = 0;
    int revision = 0;
    int load = 0;                // the load serial the scene belonged to (0: none the player knew)
    QString status;              // "applied" / "pending" / "rejected" / "superseded"
    QString reason;
    int merged = 0;
    int attachments = 0;
    QStringList missing;         // parts the player could not build (key)
    int ms = 0;
    // Protocol 5: the mount of the scene answered. An older player sends none of these (all empty).
    QString mountKey;            // the scene's mount key, "" when it had none
    QString mountStatus;         // "applied" / "failed" / "none"
    QString mountReason;
  };
  std::function<void(const SceneAck &)> onCharacterSceneApplied;

  // The player's report on a world-model load (mapObjectLoaded). Every field of the message; a count
  // the player did not send reads -1, so a check can tell "absent" from zero.
  struct MapObjectReport
  {
    int fileDataID = 0;
    int load = 0;                  // the loadWoWModel serial this answers
    QString status;                // "built" / "failed" / "superseded"
    QString reason;
    int groups = -1;
    int groupFilesRequested = -1;
    int groupFilesMissing = -1;
    int batches = -1;
    int submeshes = -1;
    int renderers = -1;
    int materials = -1;
    int provisionalMaterials = -1;
    int unresolvedMaterials = -1;
    int blendedMaterials = -1;
    int texturesReferenced = -1;
    int texturesDecoded = -1;
    int texturesMissing = -1;
    long long vertices = -1;
    long long triangles = -1;
    bool hasBounds = false;        // both arrays present with three numbers each
    double boundsMin[3] = { 0.0, 0.0, 0.0 };
    double boundsMax[3] = { 0.0, 0.0, 0.0 };
    double rootMs = -1.0;
    double groupsMs = -1.0;
    double texturesMs = -1.0;
    double buildMs = -1.0;
    double totalMs = -1.0;
    int liveMapObjects = -1;       // world-model runtime roots alive in the player after adoption
    int liveModels = -1;           // model/character runtime roots alive in the player after adoption
    // The whole report on one line, every field, for logs.
    QString describe() const;
  };
  // Raised on the GUI thread for every mapObjectLoaded.
  std::function<void(const MapObjectReport &)> onMapObjectLoaded;

  // The player's answer to runtimeState: what it holds at the moment it answered. A field the player did
  // not send reads -1.
  struct RuntimeState
  {
    int query = 0;                 // the number of the question this answers
    int liveMapObjects = -1;       // world-model runtimes alive
    int liveModels = -1;           // model runtimes alive (a character's parts included)
    int modelFileDataID = -1;      // the model on screen, 0 for none
    int mapObjectFileDataID = -1;  // the world model on screen, 0 for none
    bool loading = false;          // a load of either kind in flight
    int mountFileDataID = -1;      // the mount the model on screen rides, 0 for none (protocol 5)
    // Protocol 5 too, what the lifecycle test's mounted-character steps check (MOUNTED CHARACTERS above).
    QString mountKey;              // that mount's key ("M<serial>"), "" for none
    int liveMounts = -1;           // mount runtimes alive: on screen, or built for a scene not applied yet
    int mountsBuilt = -1;          // mount runtimes built since the player started
    int mountSeat = -1;            // how the model on screen hangs from its mount: 0 at the mount's origin (no
                                   // such attachment), 1 under the attachment's bone, 2 at the attachment's
                                   // position on the mount's root (no Transform for that bone); -1 not seated
    int mountSeatBone = -1;        // the mount bone it hangs from (seat 1), otherwise -1
    int modelSequence = -1;        // the sequence the model on screen's animator plays, -1 for none
    int mountSequence = -1;        // the sequence the mount's animator plays, -1 for none
    int mountEmitters = -1;        // the mount's particle emitters, as drawn
    int mountRibbons = -1;         // the mount's ribbon emitters, as drawn
    int mountParticles = -1;       // the mount's live particles
    int bodyRebinds = -1;          // times the character on screen's body textures were bound again by its scenes
    int viewFramings = -1;         // times the player fitted the view to what is on screen (a model, a world model or
                                   // a mount change); an appearance change on a riding character must not raise it
    int keptViews = -1;            // models put on screen keeping the view (protocol 8 keepView)
    // Protocol 9: the player's asset cache.
    int assetCacheEntries = -1;    // files kept
    int assetCacheBytes = -1;      // their size in bytes
    int assetCacheHits = -1;       // requests answered from the files kept, since the player started
    int assetCacheJoins = -1;      // requests that joined one already out
    int assetCacheEvictions = -1;  // files dropped for the cache's byte budget
    int assetEpoch = -1;           // the client epoch the files are kept under
    int backgroundR = -1;          // the viewport background the player holds, as displayed (protocol 7)
    int backgroundG = -1;
    int backgroundB = -1;
    // Protocol 12 (TERRAIN above). Not part of "loading": a terrain load is answered by terrainTileLoaded.
    int liveTerrainTiles = -1;     // terrain tile runtimes alive
    int terrainLoad = -1;          // the terrain load shown, 0 for none
    int terrainLoading = -1;       // 1 while a terrain load is being built, else 0
    int terrainView = -1;          // 1 while the viewport shows the terrain view, else 0
    bool hasCameraMask = false;    // the player sent cameraMask (every 32-bit value is a mask, so no number means absent)
    long long cameraMask = 0;      // the viewport camera's culling mask, as the player sent it
    int meshObjects = -1;          // Mesh objects alive in the player
    int materialObjects = -1;      // Material objects alive in the player
    int terrainMeshes = -1;        // Meshes held by the live terrain runtimes, 0 when no tile is shown
    int terrainMaterials = -1;     // Materials held by them
    int terrainGameObjects = -1;   // GameObjects under the terrain root
    // Protocol 12 too, added later (a player before them reads -1): what the player keeps of its map-file requests.
    int terrainPendingRequests = -1;  // the current terrain load's requests awaiting their answers: 0 or 1 (protocol
                                      // 12), at most the player's outstanding limit (protocol 13)
    int terrainRetiredRequests = -1;  // requests of loads that ended unanswered, kept to recognise a late answer (<= 32
                                      // in a protocol-12 player, <= 128 in a protocol-13 one)
    int mapAnswersRouted = -1;        // map-file answers routed to the terrain session since the player started
    int mapAnswersDropped = -1;       // ... of which dropped: late (their load had ended) or owned by no load
    int terrainTimeoutMs = -1;        // the time a terrain load may wait for its files before it fails
    int terrainRetiredTotal = -1;     // requests retired since the player started (not capped: it goes on past 32)
    QString terrainLastRetiredRequest;          // the id of the request retired last ("" for none, or not sent)
    bool hasTerrainLastRetiredRequest = false;  // the player sent terrainLastRetiredRequest (a text: "" is no absence)
    int mapAnswersLate = -1;          // mapAnswersDropped's late part: answers to a request in the retired ring
    int mapAnswersUnowned = -1;       // ... and its unowned part: answers to a request no load waits for nor the ring holds
    // Protocol 13 (TERRAIN TEXTURES above).
    int terrainTextures = -1;              // terrain textures alive (the shown tile's), 0 with no tile
    long long terrainTextureBytes = -1;    // their GPU size, as the player estimates it
    int terrainAlphaArrays = -1;           // terrain alpha arrays alive
    // Protocol 16 (WORLD MODELS IN THE WORLD above): the world models' own tallies (never the Buildings model's).
    int worldInstances = -1;               // world-model instances alive (placed)
    int worldModels = -1;                  // world-model prototypes alive (one per unique world model)
    int worldMeshes = -1;                  // their Meshes, Materials and Textures (shared by the instances)
    int worldMaterials = -1;
    int worldTextures = -1;
    long long worldGpuGeometryBytes = -1;  // the GPU size of their geometry and textures, as the player estimates it
    long long worldGpuTextureBytes = -1;
    int worldGroups = -1;                  // group renderers of the instances, and of them drawn and culled last frame
    int worldGroupsVisible = -1;
    int worldGroupsCulled = -1;
    int worldInstancesCulled = -1;
    int worldLoad = -1;                    // the world load the instances belong to, 0 for none
    QString worldKind;                     // "tile" / "globalWmo" / "" (none)
    int worldFrames = -1;                  // the world view's last frames kept (at most 600 since it was entered)
    double worldFrameMsMedian = -1.0, worldFrameMsP95 = -1.0, worldFrameMsMax = -1.0;   // their frame times
    // Protocol 17 (DOODADS above): the doodads' own tallies (never the Models viewer's), and the scene's whole ones -- every
    // GameObject (FindObjectsOfTypeAll), Texture2D, Mesh and Material alive in the player: what a leak check counts back.
    int doodadInstances = -1;              // doodad instances alive (placed)
    int doodadPrototypes = -1;             // doodad prototypes alive (one per unique M2 built)
    int doodadMeshes = -1;                 // their Meshes, Materials and Textures (shared by the instances)
    int doodadMaterials = -1;
    int doodadTextures = -1;
    long long doodadGpuGeometryBytes = -1; // the GPU size of their geometry and textures, as the player estimates it
    long long doodadGpuTextureBytes = -1;
    // Protocol 18 (STREAMING): the session (-1 from an older player), and whether the whole-player scans were skipped (a
    // runtimeState asked light: liveGameObjects, liveTexture2D, liveMeshes, liveMaterials, meshObjects, materialObjects -1).
    bool light = false;
    int streaming = -1;                    // 1: a session is on, 0: none
    int streamTiles = -1, streamJobs = -1, streamRequestsOut = -1, streamPlacements = -1, streamDoodads = -1, sceneTerrainTextures = -1;
    long long streamPrototypesEvicted = -1, streamTexturesEvicted = -1, streamFilesAskedAgain = -1, streamImagesShared = -1;
    int doodadVisible = -1;                // doodad instances drawn and culled last frame
    int doodadCulled = -1;
    int liveGameObjects = -1;
    int liveTexture2D = -1;
    int liveMeshes = -1;
    int liveMaterials = -1;
    QString describe() const;
  };
  // Ask the player what it holds (runtimeState). Returns the question's number, which the answer echoes,
  // or 0 when nothing was sent (no player, or one older than protocol 4).
  // light (protocol 18): the player skips its whole-player object scans (cheap enough to ask while a world streams).
  int requestRuntimeState(bool light = false);
  // Raised on the GUI thread for every runtimeState answer.
  std::function<void(const RuntimeState &)> onRuntimeState;

  // Ask the player for a width x height PNG of the viewport with a transparent background, written to path (absolute;
  // see SCREENSHOTS above). Returns the request's number, which the answer echoes, or 0 when nothing was sent (no
  // player, or one older than protocol 6).
  int requestScreenshot(const QString & path, int width, int height);

  // Runtime command: the Models viewport's background, as the sRGB bytes it is to display as (VIEWPORT BACKGROUND
  // above). False (nothing sent) when the player is not connected and ready, or speaks a protocol older than 7.
  bool sendViewportBackground(int r, int g, int b);

  // Runtime command: the models the player is likely to load next (the other model generation of the character on
  // screen), to fetch into its asset cache while nothing waits for them (protocol 9 prefetchAssets). False (nothing
  // sent) when the player is not connected and ready, or speaks a protocol older than 9.
  bool sendPrefetchAssets(const std::vector<int> & fileDataIDs);
  // The player's answer to captureScreenshot. A number the player did not send reads -1.
  struct ScreenshotResult
  {
    int request = 0;
    bool ok = false;
    QString error;                 // why not, when !ok
    QString path;
    int width = -1;
    int height = -1;
    long long bytes = -1;          // the PNG's size on disk
    double renderMs = -1.0;        // the off-screen render and the readback
    double encodeMs = -1.0;        // the PNG encode
    double writeMs = -1.0;         // the file write
    double totalMs = -1.0;         // the whole capture, from the end of the frame it was taken in
    QString describe() const;
  };
  // Raised on the GUI thread for every screenshotSaved.
  std::function<void(const ScreenshotResult &)> onScreenshotSaved;

  // Runtime command (TERRAIN above): one terrain tile, and the map's WDT the player fetches first. False (nothing sent)
  // when the player is not connected and ready, or speaks a protocol older than 12. The caller raises its terrain load
  // serial only when this returns true.
  // THE WORLD VIEW'S CAMERA (protocol 15, docs/world-viewer/05-maps-viewer-ui.md section 21). Positions are the game's
  // world frame (yards; x north, y west, z up): the player converts them with the scene's floating origin. "fly": the free
  // camera at (x, y, z), or on the ground under (x, y) plus eyeHeight when hasZ is false, facing yaw (degrees clockwise from
  // north) and pitch (degrees, up positive); "overview": the tile framed as Stage 03 frames it.
  struct WorldCamera
  {
    int request = 0;               // the host's serial, echoed in worldCameraApplied
    QString mode;                  // "fly" / "overview"; empty: no camera
    double x = 0.0, y = 0.0, z = 0.0;
    bool hasZ = false;
    double yaw = 0.0, pitch = 0.0;
    double eyeHeight = 2.0;
    // Protocol 16: "fly" at the world model's own start point (a WMO-only map entered without a target); x, y, z and the
    // angles are then the player's.
    bool start = false;
  };

  struct TerrainTileCommand
  {
    int load = 0;                  // the host's terrain load serial (> 0)
    int assetEpoch = 0;            // UnityAssetAccess::clientEpoch() when sent
    int mapId = -1;
    QString mapDirectory;
    QString product;               // the client the map catalogue was built from
    QString build;
    int wdtFileDataID = 0;         // the WDT the player asks for (getMapAsset role "wdt")
    QString wdtResolvedBy;         // "fileDataID" (Map.db2's) or "path" (the storage root's id for the directory path)
    QString wdtPath;               // "path" only
    int tileX = -1;
    int tileY = -1;
    int maxAssetBytes = MAX_MAP_ASSET_BYTES;
    // TERRAIN TEXTURES (protocol 13): sent only when maxTextureBytes > 0 and the player draws terrain textures; 0 offers
    // the load no textures (a player before protocol 13).
    int maxTextureBytes = 0;
    int maxTextureBytesPerLoad = 0;
    int maxTexturesPerLoad = 0;
    // Protocol 15: where the world view's camera goes when this load's tile is put on screen (sent only when camera.mode
    // is set and the player flies the world; otherwise the tile is framed as before).
    WorldCamera camera;
    // Protocol 16 (WORLD MODELS IN THE WORLD above): a world load -- sent as loadWorld by sendLoadWorld, never by
    // sendLoadTerrainTile. globalWmo: a WMO-only map's world model, no tile (tileX and tileY then -1).
    bool world = false;
    bool globalWmo = false;
    int maxWmoFileBytes = MAX_WMO_FILE_BYTES;
    int maxWmoBytesPerLoad = MAX_WMO_BYTES_PER_LOAD;
    int maxWmoTextureBytes = MAX_WMO_TEXTURE_BYTES;
    int maxWmoTextureBytesPerLoad = MAX_WMO_TEXTURE_BYTES_PER_LOAD;
    int maxWmoTexturesPerLoad = MAX_WMO_TEXTURES_PER_LOAD;
    int maxWmoModelsPerLoad = MAX_WMO_MODELS_PER_LOAD;
    int maxWmoGroupsPerLoad = MAX_WMO_GROUPS_PER_LOAD;
    int maxWmoPlacementsPerLoad = MAX_WMO_PLACEMENTS_PER_LOAD;
    // Protocol 17 (DOODADS above): the world load offers the doodads -- sent ("doodads":true and these budgets) only when
    // it is set and the player places doodads; otherwise the load is the protocol-16 loadWorld.
    bool doodads = false;
    // Protocol 18 (STREAMING above): the world load is a streaming session's entry -- sent ("streaming" and the region's
    // numbers) only when it is set and the player streams; otherwise the load is the single-tile loadWorld.
    bool streaming = false;
    int maxM2FileBytes = MAX_M2_FILE_BYTES;
    int maxM2BytesPerLoad = MAX_M2_BYTES_PER_LOAD;
    int maxM2TextureBytes = MAX_M2_TEXTURE_BYTES;
    int maxM2TextureBytesPerLoad = MAX_M2_TEXTURE_BYTES_PER_LOAD;
    int maxM2ModelsPerLoad = MAX_M2_MODELS_PER_LOAD;
    int maxM2TexturesPerLoad = MAX_M2_TEXTURES_PER_LOAD;
    int maxDoodadInstancesPerLoad = MAX_DOODAD_INSTANCES_PER_LOAD;
  };
  bool sendLoadTerrainTile(const TerrainTileCommand & command);
  // Runtime command (protocol 16): a world load (WORLD MODELS IN THE WORLD above). False (nothing sent) without a
  // protocol-16 player, a load serial or command.world; the caller raises its terrain load serial only when it returns true.
  // The doodad budgets (protocol 17, DOODADS above) go with it only when command.doodads and the player places doodads.
  bool sendLoadWorld(const TerrainTileCommand & command);
  // Runtime command: take the terrain away (a load being built is answered "cleared"; a tile shown is disposed and the
  // terrain view left, unanswered). False (nothing sent) as for sendLoadTerrainTile.
  bool sendClearTerrain(const QString & reason);

  // THE MAPS VIEWER (protocol 14). inspectMapTiles: a map's WDT bytes, as the map asset worker read and checked them
  // against their content key (md5), for the player's parser; answered by mapTilesInspected with the same inspection
  // number. One line and the bytes (at most MAX_MAP_ASSET_BYTES), sent once per inspection the user's choice of a map
  // asks for -- never on a timer; refused (false, nothing sent) without a protocol-14 player, or while more than
  // MAP_ASSET_SEND_HIGH_WATER bytes wait to be sent (a player that does not read gets nothing more queued).
  bool sendInspectMapTiles(int inspection, int mapId, int wdtFileDataID, int assetEpoch, const QString & md5,
                           const QByteArray & bytes);
  struct MapTilesInspection
  {
    int inspection = 0;
    QString status;                // "inspected" / "failed"
    QString reason;
    int mapId = -1;
    int wdtFileDataID = 0;
    int assetEpoch = 0;
    long long wdtBytes = -1;
    QString md5;
    QString kind;                  // the parser's WdtKind: TiledTerrain, GlobalWmo, Empty, TiledTerrainWithGlobalWmo
    quint32 mphdFlags = 0;
    bool hasMain = false;
    bool hasMaid = false;
    int tilesInMain = -1;
    int tilesWithRoot = -1;
    double parseMs = -1.0;
    // The table, index y * 64 + x (x the column, y the row: the root ADT is <map>_<x>_<y>.adt): bit 0 the MAIN flag, bit 1
    // MAID names a root ADT; MAID words 0 and 3 (0: none), empty without MAID. tableValid: the table decoded to its sizes
    // (4,096 flags; 4,096 ids each) -- else tableProblem says what was wrong, and nothing of it is used.
    QByteArray tileFlags;
    std::vector<quint32> rootIds;
    std::vector<quint32> tex0Ids;
    std::vector<quint32> minimapIds;   // protocol 15: MAID word 7 of every tile (the minimap image), empty without MAID
    bool tableValid = false;
    QString tableProblem;
    // Protocol 15: a world-model-only map's global WMO -- the WDT's MODF records, and the first one's file.
    int globalWmoRecords = 0;
    quint32 globalWmoFileDataID = 0;
    QString globalWmoPath;
    // Protocol 16: that first record as stored (file frame: position, angles in degrees, extents), for inspectWorldModel.
    struct GlobalRecord
    {
      bool present = false;
      double position[3] = { 0.0, 0.0, 0.0 };
      double rotation[3] = { 0.0, 0.0, 0.0 };
      double extentsMin[3] = { 0.0, 0.0, 0.0 };
      double extentsMax[3] = { 0.0, 0.0, 0.0 };
      int flags = 0;
      int scale = 0;
      quint32 uniqueId = 0;
    };
    GlobalRecord globalRecord;
  };
  std::function<void(const MapTilesInspection &)> onMapTilesInspected;
  // setTerrainView: the view terrain is drawn in from now on -- "auto" (textured when the tile's material has drawable
  // chunks, else shaded) or one of the player's views but texturedplainweights -- and the tile on screen built again in
  // it by the player, nothing read. False (nothing sent) without a protocol-14 player.
  bool sendSetTerrainView(const QString & view);
  struct TerrainViewApplied
  {
    QString view;
    QString status;                // "applied" (rebuilt) / "noTile" (for the next tile) / "failed" / "refused"
    QString reason;
    int load = 0;
    double rebuildMs = -1.0;
    int texturedChunks = -1;
    int neutralChunks = -1;
    int textures = -1;
    long long retainedTextureBytes = -1;
  };
  std::function<void(const TerrainViewApplied &)> onTerrainViewApplied;
  // setWorldCamera (protocol 15): place the world view's camera, or switch between the free camera and the overview, without
  // a reload; answered by worldCameraApplied with the same request. False (nothing sent) without a protocol-15 player or a
  // camera mode. Sent once per choice of the user's -- never on a timer.
  bool sendSetWorldCamera(const WorldCamera & camera);
  struct WorldCameraApplied
  {
    int request = 0;
    QString mode;
    QString status;                // "applied" / "refused"
    QString reason;
    double x = 0.0, y = 0.0, z = 0.0;   // where the camera is (game frame)
    bool ground = false;           // z came from the ground under the point
    bool hasGroundZ = false;
    double groundZ = 0.0;
    double yaw = 0.0, pitch = 0.0;
    int load = 0;                  // the tile on screen
  };
  std::function<void(const WorldCameraApplied &)> onWorldCameraApplied;

  // WORLD MODELS IN THE WORLD (protocol 16). inspectWorldModel: a WMO-only map's world model -- its root's bytes as the map
  // asset worker read and checked them against their content key (md5), and the WDT record that places it -- for the
  // player's parser; answered by worldModelInspected with the same inspection. Sent once per map the user picks; refused
  // (false, nothing sent) as sendInspectMapTiles is, and over MAX_WMO_FILE_BYTES.
  bool sendInspectWorldModel(int inspection, int mapId, int wmoFileDataID, int assetEpoch, const QString & md5,
                             const QByteArray & bytes, const MapTilesInspection::GlobalRecord & record);
  struct WorldModelInspection
  {
    int inspection = 0;
    QString status;                // "inspected" / "failed"
    QString reason;
    int mapId = -1;
    int wmoFileDataID = 0;
    int assetEpoch = 0;
    long long wmoBytes = -1;
    QString md5;
    int groups = -1;
    int interiorGroups = -1;
    double parseMs = -1.0;
    // World frame (yards; x north, y west, z up): the placed MOHD box, the record's extents, and every group's placed box
    // (min x, y, z, max x, y, z), with its MOGI flags. valid: the boxes decoded to their sizes (else problem says why).
    double rootBox[6] = { 0, 0, 0, 0, 0, 0 };
    double extents[6] = { 0, 0, 0, 0, 0, 0 };
    std::vector<float> groupBoxes;
    std::vector<quint32> groupFlags;
    bool valid = false;
    QString problem;
  };
  std::function<void(const WorldModelInspection &)> onWorldModelInspected;
  // worldShown: a world load's scene first on screen, its camera placed (the world models may still be loading).
  struct WorldShown
  {
    int load = 0;
    int mapId = -1;
    QString kind;                  // "tile" / "globalWmo"
    int tileX = -1, tileY = -1;
    int placements = -1, models = -1;
    double shownMs = -1.0;
    bool hasOriginWorld = false;
    double originWorld[3] = { 0.0, 0.0, 0.0 };
  };
  std::function<void(const WorldShown &)> onWorldShown;
  // worldProgress: a world load's world models, counted (at most every quarter second while it is in flight).
  struct WorldProgress
  {
    int load = 0;
    QString phase;
    int placements = -1, models = -1, modelsRequested = -1, modelsParsed = -1, modelsBuilt = -1, modelsFailed = -1;
    int modelsUnsupported = -1, modelsNotRequested = -1, placed = -1, placementsFailed = -1;
    int filesAsked = -1, filesAnswered = -1;
    long long bytesReceived = -1;
    double elapsedMs = -1.0;
    // Protocol 17 (DOODADS above): the doodads' phase and counts, apart from the world models' (empty and -1 from an older
    // player; -1 from a protocol-17 one when the load offered no doodads).
    QString doodadPhase;
    int doodadInstances = -1, doodadPlaced = -1, doodadModels = -1, doodadModelsBuilt = -1, doodadModelsFailed = -1;
    int doodadFilesAsked = -1, doodadFilesAnswered = -1;
    long long doodadBytes = -1;
  };
  std::function<void(const WorldProgress &)> onWorldProgress;
  // streamState (protocol 18): the player's streaming session, at most every quarter second -- where the camera is, each
  // tile of the region and what it has, the requests out, what is resident and pooled, and the frames since the last one
  // (docs/world-viewer/08-streaming.md 1.10). A number the player did not send reads -1.
  struct StreamTile
  {
    int x = -1, y = -1;
    int load = 0;                    // its tile load (0: not opened yet)
    QString state;                   // "wanted", "opening", "loading", "built", "settled", "failed", "closing"
    int filesAsked = 0, filesAnswered = 0;
    long long bytes = 0;
    int worldPlaced = 0, worldPlacements = 0, doodadsPlaced = 0, doodadInstances = 0;
  };
  struct StreamState
  {
    int load = 0;
    double cameraX = 0.0, cameraY = 0.0, cameraZ = 0.0;
    int cameraTileX = -1, cameraTileY = -1;
    std::vector<StreamTile> tiles;
    int tilesResident = -1, tilesLoading = -1, tilesWanted = -1, tilesOpened = -1, tilesClosed = -1, tilesFailed = -1;
    int requestsOut = -1, requestsWaiting = -1;
    long long filesAsked = -1, filesAnswered = -1, bytesReceived = -1;
    int worldPrototypes = -1, worldPrototypesPooled = -1, doodadPrototypes = -1, doodadPrototypesPooled = -1;
    int textures = -1, texturesPooled = -1;
    int worldInstances = -1, doodadInstances = -1;
    long long gpuTextureBytes = -1, gpuGeometryBytes = -1, poolBytes = -1;
    long long prototypesEvicted = -1, texturesEvicted = -1;
    double frameMsMax = -1.0, frameMsP95 = -1.0, uploadMsMax = -1.0, workMsMax = -1.0;
    int framesOver33 = -1, framesOver50 = -1;
    long long managedHeapBytes = -1;
    QJsonObject raw;                 // the message as received (the harness keeps it)
    QString describe() const;
  };
  std::function<void(const StreamState &)> onStreamState;
  const StreamState & lastStreamState() const { return m_lastStreamState; }
  long long streamStatesReceived() const { return m_streamStatesReceived; }
  // TEST (protocol 18, the harnesses' acceptance flight): the player's free camera flies the path itself -- points (x, y, z
  // in yards, world frame), speed in yards a second, pitch in degrees -- answered testFlightDone; 0 to a player that does
  // not stream.
  int sendTestFlight(const std::vector<double> & points, double speed, double pitch);
  struct TestFlightDone
  {
    int request = 0;
    QString status, reason;                // "done" | "refused" | "stopped"
    double seconds = -1.0, frameMsMax = -1.0, distance = -1.0;
    int frames = -1;
  };
  std::function<void(const TestFlightDone &)> onTestFlightDone;
  // streamTile (protocol 18): the player opens and closes tile loads of its session; the viewer answers which it opened
  // (with their load ids), refused (why) and closed -- the answer is queued as the reply (empty: none).
  std::function<QJsonObject(const QJsonObject &)> onStreamTile;

  // THE VIEWER'S OWN READS (the Maps viewer's WDT reads, docs/world-viewer/05-maps-viewer-ui.md): a job the viewer gives
  // the map asset worker itself, decided and leased by the viewer. Its result comes back through this server's polls
  // like those of the reads it dispatches -- in the worker's order, waiting while a client loads, with the same wake --
  // and is handed to onViewerMapRead instead of being answered to the player. The viewer keeps at most one in flight; a
  // newer one cancels it (cancelViewerReads), and a client replacement cancels it with the server's (cancelMapReads).
  // False when there is no worker or it has stopped.
  bool submitViewerRead(MapAssetWorker::Job & job);
  void cancelViewerReads();
  size_t viewerReadsInFlight() const { return m_viewerReads.size(); }
  std::function<void(MapAssetWorker::Result & result)> onViewerMapRead;

  // The host's answer to one getMapAsset, from the viewer (onDispatchMapAsset, onCompleteMapAsset: the server knows
  // nothing of maps): state is the map catalogue's availability name, "stale" or "refused"; bytes only for "available".
  struct MapAssetAnswer
  {
    QString state;
    QString detail;
    QByteArray bytes;
    double readMs = -1.0;          // the storage read (-1: nothing was read)
    QString md5;                   // hex MD5 of bytes (the file's content key); empty unless available
    long long bytesRead = 0;       // bytes read from the storage for this answer, whatever came of them (a file that
                                   // failed its kind check was read all the same; one whose read failed partway counts
                                   // its size, an upper bound: WdtInspection::readBytes); what the queue's poll budget
                                   // and the load's texture byte budget count
    QJsonObject timings;           // where the answer's time went, by step, in ms (the decision, the storage root, each
                                   // step of the read, the kind check): reported in the record beside handlerMs
  };
  // THE MAP ASSET WORKER. A request is decided by the viewer when the queue serves it (onDispatchMapAsset, GUI thread):
  // either the answer now (refused, stale: nothing to read), or a read -- the file, its limit, and the client epoch and
  // storage generation it was decided under -- which the worker carries out on its own thread. Its result comes back to
  // the viewer (onCompleteMapAsset, GUI thread), which checks it again (the client, the load) and makes the answer.
  // onMapAssetStale is asked every poll for every read in flight: "" while it is still wanted, else why not (the read is
  // cancelled: skipped when not begun; finished, whole, and answered stale otherwise). Without the dispatch handler
  // every getMapAsset is refused.
  struct MapAssetDispatch
  {
    bool read = false;             // false: answer is the answer, now
    MapAssetAnswer answer;         // the answer; with a read, only its timings (the decision's)
    int fileDataID = 0;
    qint64 maxBytes = 0;
    int epoch = 0;
    quint64 generation = 0;
  };
  std::function<MapAssetDispatch(const QJsonObject & request)> onDispatchMapAsset;
  std::function<MapAssetAnswer(const QJsonObject & request, MapAssetWorker::Result & result)> onCompleteMapAsset;
  std::function<QString(const QJsonObject & request)> onMapAssetStale;
  // While this says so (a client is being loaded: its storage and catalogue are being replaced), reads in flight are
  // neither checked nor answered: their results wait with the worker, to be checked once the load is over -- answered
  // stale when it replaced the client, as read when it failed and the client stayed.
  std::function<bool()> onMapAssetWait;
  // The worker the reads go to (the viewer's; shared, it outlives a stop and start of the server).
  void setMapAssetWorker(const std::shared_ptr<MapAssetWorker> & worker);
  // THE WAKE: every result the worker makes posts this server a wake (a wxThreadEvent, at most one waiting), and the GUI
  // thread polls at once instead of at the next tick -- a read's answer leaves as soon as it is read. The viewer's
  // server only; the in-process checks drive their servers' polls themselves. Undone by setMapAssetWorker and the
  // destructor, before anything it would wake goes.
  void wakeOnMapResults();
  // The harness's pump: waits up to ms, less as soon as the worker has made a result since the last poll looked (the
  // wake, for a pump that polls by itself). Without a worker, sleeps ms.
  void waitForMapResult(int ms);
  const std::shared_ptr<MapAssetWorker> & mapAssetWorker() const { return m_mapAssetWorker; }
  // Every read in flight is not wanted any more (the client was replaced): cancelled as onMapAssetStale would.
  void cancelMapReads(const QString & reason);
  size_t mapReadsInFlight() const { return m_mapReadsInFlight.size(); }
  // The server's accounting of the reads, since the process began.
  struct MapReadStats
  {
    long long dispatched = 0;       // reads handed to the worker
    long long answered = 0;         // ... answered from their result
    long long answeredStale = 0;    // ... of which "stale" (the viewer's check at completion)
    long long cancelled = 0;        // cancelled by the viewer while in flight
    long long orphaned = 0;         // their connection ended: dropped unanswered
    long long droppedUnanswered = 0;// ... their results taken back and dropped (dispatched = answered + dropped + in flight)
    long long refusedNoWorker = 0;  // refused for want of a running worker (shutting down)
    long long dispatchHeldBack = 0; // polls whose dispatch stopped at MAP_ASSET_SEND_HIGH_WATER
    int inFlightMax = 0;            // the most reads in flight at once
    long long unsentBytesMax = 0;   // the most bytes waiting to be sent, seen at the end of a serving poll
    long long wakes = 0;            // wakes handled (THE WAKE)
    long long wakesDeferred = 0;    // ... left to the timer: their run of wakes had spent MAP_ASSET_WAKE_BURST_MS
    double wakeBurstMsMax = 0.0;    // the most GUI time one run of wakes spent
    long long takenOnWake = 0;      // ... the results their polls took back
  };
  const MapReadStats & mapReadStats() const { return m_mapReadStats; }
  long long unsentBytes() const;
  // THE TRANSPORT CAP's accounting, since the process began.
  struct TransportStats
  {
    long long receivePauses = 0;       // times receiving was paused (TRANSPORT_RECEIVE_PAUSE_BYTES)
    long long pollsPaused = 0;         // polls that received nothing for it
    long long linesHandled = 0;        // lines handled
    long long pollsLinesDeferred = 0;  // polls that left complete lines for the next (TRANSPORT_LINES_PER_POLL)
    long long dropsHardCap = 0;        // clients dropped over TRANSPORT_HARD_CAP_BYTES
    long long dropsStalled = 0;        // ... whose bytes waited TRANSPORT_STALL_DROP_MS with no send progress
    long long messagesNotQueued = 0;   // messages not queued: their connection was being dropped
    long long unsentBytesMax = 0;      // the most bytes waiting to be sent, after any message was queued
  };
  const TransportStats & transportStats() const { return m_transportStats; }
  bool receivePaused() const { return m_receivePaused; }
  // STREAMING: a session begins or ends -- the queue's room and the reads in flight (STREAM_QUEUE_MAX,
  // STREAM_READS_IN_FLIGHT), and the answers' log: one line per answer outside a session, during one a summary every two
  // seconds (WMV_LOG_MAP_ANSWERS=1 keeps the line per answer).
  void setStreaming(bool on);
  // STREAMING: every request waiting whose load has ended (onMapAssetStale) answered stale now; returns how many.
  int answerStaleWaiting();
  bool streaming() const { return m_streaming; }
  int mapAssetQueueMax() const { return m_mapAssetQueueMax; }
  int mapReadsInFlightMax() const { return m_mapReadsInFlightMax; }
  // One getMapAsset answered: what was asked and what went back.
  struct MapAssetServed
  {
    QString requestId;
    int load = 0;                  // as the request named them
    int assetEpoch = 0;
    int epochNow = 0;              // UnityAssetAccess::clientEpoch() when it was answered
    QString role;
    int fileDataID = 0;
    long long bytes = -1;          // bytes served, -1 for none
    long long bytesRead = 0;       // bytes read from the storage for it (MapAssetAnswer::bytesRead)
    QString state;
    QString detail;
    QString md5;
    double readMs = -1.0;
    double handlerMs = -1.0;       // validation, read and queueing the answer (what the GUI thread spent on it)
    double queuedMs = -1.0;        // from its arrival to the start of its serving (THE MAP ASSET QUEUE); -1: not queued
                                   // (a held request released, a test's own)
    bool first = false;            // the first read of this FileDataID from this client's storage by a getMapAsset of
                                   // this process (MapAssetReadTracker: client epoch and id): for a root ADT
                                   // the cold-read candidate; for a WDT not one, ResolveTerrainTile's catalog->inspect()
                                   // having read that WDT whole on this thread just before the load was sent
    bool firstTracked = true;      // false: a file served when the read tracker was full and did not hold its id, so
                                   // whether this was its first read is not known (first is false then)
    QString readOutcome;           // the worker's outcome for a request that went to it (MapAssetWorker::outcomeName),
                                   // "" for one answered at its dispatch
    QString readState;             // ... and, when it was read, what the read found (an availability name): a read
                                   // answered "stale" afterwards says here what it had read
    QJsonObject timings;           // MapAssetAnswer::timings, plus the answer's own steps: serializeMs (its header line),
                                   // appendMs (header and payload into the send buffer), sendMs (what the socket took at
                                   // once), logMs (its log line, written after handlerMs ends)
    QString describe() const;
  };
  // The most recent getMapAsset answers, in order (at most MAP_ASSET_RECORDS_KEPT), and how many there were since the
  // server started: a player's life answers without end, and only what is recent is worth keeping. Every answer is also
  // handed to onMapAssetServed, for a test that wants all of them.
  const std::deque<MapAssetServed> & mapAssetsServed() const { return m_mapAssetsServed; }
  long long mapAssetsServedTotal() const { return m_mapAssetsServedTotal; }
  long long mapAssetRecordsDropped() const { return m_mapAssetRecordsDropped; }
  std::function<void(const MapAssetServed &)> onMapAssetServed;
  // Keeps one answer's record (and calls onMapAssetServed): what every getMapAsset answer does; a test feeds made-up ones.
  void recordMapAssetServed(const MapAssetServed & served);
  // Which files getMapAsset has read from the loaded client's storage (MapAssetServed::first). Kept across player
  // restarts -- the storage is the same -- and emptied when the client is replaced (noteClientReplaced).
  const MapAssetReadTracker & mapAssetReads() const { return m_mapAssetReads; }
  // The loaded client was replaced, the new one's epoch is epoch (UnityAssetAccess::noteClientReplaced, then
  // UnityAssetAccess::clientEpoch()): the read tracker forgets the files of the client before at once.
  void noteClientReplaced(int epoch) { m_mapAssetReads.reset(epoch); }

  // THE MAP ASSET QUEUE (above). One poll that served map requests: how many, the bytes read, the GUI-thread time spent
  // serving them, and what it stopped for.
  struct MapAssetPoll
  {
    int served = 0;                // requests answered in the poll
    long long bytesRead = 0;       // bytes read from the storage for them
    int largeFiles = 0;            // files over MAP_ASSET_LARGE_FILE_BYTES among them (0 or 1)
    int viewerResults = 0;         // the viewer's own results taken back (at most MAP_VIEWER_RESULTS_PER_POLL)
    double serveMs = -1.0;         // the serving, from the first request taken to the last answer queued
    int waitingAfter = 0;          // requests still waiting when the poll stopped serving
    bool stoppedForBudget = false; // it stopped taking results back: MAP_ASSET_POLL_BUDGET_BYTES read for them, or a
                                   // large file
    int completed = 0;             // of served, the reads answered from the worker's results
    int dispatched = 0;            // reads handed to the worker in the poll
    int inFlightAfter = 0;         // reads in flight when the poll ended
    double recordMs = 0.0;         // of serveMs, the time spent in onMapAssetServed (a test's record keeping)
  };
  // The requests waiting now, in order of arrival.
  size_t mapAssetsWaiting() const { return m_mapAssetQueue.size(); }
  // The most recent MAP_ASSET_POLL_TIMINGS_KEPT polls that served any (oldest first), and how many there were since the
  // server started; every one is also handed to onMapAssetPoll.
  const std::deque<MapAssetPoll> & mapAssetPolls() const { return m_mapAssetPolls; }
  long long mapAssetPollsTotal() const { return m_mapAssetPollsTotal; }
  std::function<void(const MapAssetPoll &)> onMapAssetPoll;
  // Every poll: how long it ran on this thread, accept to send, the time since the one before began, and how many map
  // requests it answered.
  struct PollTiming
  {
    double pollMs = 0.0;
    double receiveMs = 0.0;        // ... of it, reading and handling the player's messages (their handlers included)
    double serveMs = 0.0;          // ... the map asset queue and worker (serveMapAssetQueue)
    double recordMs = 0.0;         // ... of serveMs, onMapAssetServed (a test's record keeping)
    double sendMs = 0.0;           // ... what the socket took of the send buffer at the end
    double sincePreviousMs = -1.0; // -1 for the first poll
    int mapAnswered = 0;
  };
  std::function<void(const PollTiming &)> onPollTiming;
  // Requests received since the server started: queued, refused for a full queue, and dropped unanswered with the queue
  // (its connection ended); the most that waited at once.
  long long mapAssetsQueued() const { return m_mapAssetsQueued; }
  long long mapAssetsRefusedQueueFull() const { return m_mapAssetsRefusedQueueFull; }
  long long mapAssetsQueueDropped() const { return m_mapAssetsQueueDropped; }   // since the process began
  // STREAMING: answered stale while they waited (their load ended: a tile closed), not at the queue's front.
  long long mapAssetsAnsweredStaleWaiting() const { return m_mapAssetsAnsweredStaleWaiting; }
  int mapAssetQueueDepthMax() const { return m_mapAssetQueueDepthMax; }

  // TEST HOOKS (the -terraintile test only; inert unless it sets them). The next getMapAsset (of role, or of any role
  // when role is empty) is held unanswered; release puts every held one back at the front of the queue, in order, to be
  // decided and read as any -- by then the viewer usually calls it "stale": a late answer.
  // At most MAP_ASSETS_HELD_MAX are held (a request beyond that is answered as usual), and held requests are dropped,
  // never answered, when the player's connection ends: no answer meant for one player may reach another.
  void testHoldNextMapAsset(const QString & role);
  void testCancelMapAssetHold();
  bool testMapAssetHoldArmed() const { return m_holdNextMapAsset; }
  int testReleaseHeldMapAssets();          // returns how many were answered
  const std::vector<QJsonObject> & mapAssetsHeld() const { return m_mapAssetsHeld; }
  long long mapAssetsHeldDropped() const { return m_mapAssetsHeldDropped; }   // dropped unanswered, since the process began
  // The queue without a player (the -terraintile in-process checks, on a server of their own): a request taken as if it
  // had arrived (held, queued, or refused for a full queue), one poll's serving, and the queue dropped as when a
  // connection ends. Answers go nowhere without a client; their records are kept as any.
  void testReceiveMapAsset(const QJsonObject & request) { handleGetMapAsset(request); }
  int testServeMapAssetQueue() { return serveMapAssetQueue(); }   // returns how many were answered
  void testDropMapAssetQueue() { dropHeldMapAssets("the test dropped them"); }
  // TEST HOOKS of the worker's reads: the next read dispatched of role (any when empty) is held in the worker with its
  // file open (MapAssetWorker::Job::testHold) until testReleaseMapRead, or has one byte changed before its content key
  // is checked; every read from now on waits ms before it begins (0: none). testMapReadHeld: a read held now, and its
  // request. testWaitMapReads: every read dispatched has its result ready (the in-process queue check).
  void testHoldNextMapRead(const QString & role, bool beforeOpen = false);
  void testCancelMapReadHold();
  void testCorruptNextMapRead(const QString & role);
  void testDelayMapReads(int ms);
  bool testMapReadHeld(QJsonObject * request = nullptr) const;
  bool testMapReadHookArmed() const { return m_holdNextMapRead || m_corruptNextMapRead; }
  // TEST HOOK: the next terrainTileLoaded of load is held, not handled -- the host still takes the load as current --
  // until testReleaseTerrainReport handles it (dropped with the connection): with it a read finished after the player
  // gave the load up is answered as the file, a genuine late file answer. testFakeUnsentBytes: the send buffer counts
  // that many more bytes waiting than it holds (the in-process queue check of MAP_ASSET_SEND_HIGH_WATER).
  void testHoldTerrainReport(int load) { m_holdReportLoad = load; }
  bool testTerrainReportHeld() const { return !m_heldReport.isEmpty(); }
  void testReleaseTerrainReport();
  void testFakeUnsentBytes(long long bytes) { m_fakeUnsentBytes = bytes; }
  // TEST HOOKS of the transport cap (the in-process check with a real socket): smaller marks, a smaller kernel send
  // buffer for the connections accepted from now on (0: the usual), and bytes queued as a message of their own -- the
  // same append every viewer push takes.
  void testSetTransportLimits(long long pauseBytes, long long resumeBytes, long long hardCapBytes, int stallMs,
                              int linesPerPoll, int sendBufferBytes);
  void testPushBytes(long long bytes);
  void testReleaseMapRead();
  bool testWaitMapReads(int ms);
  // A terrainTileLoaded taken as if the player had sent it (the -terraintile in-process checks of what a report is read
  // into, on a server of their own): read, counted, logged and handed to onTerrainTileLoaded as any.
  void testReceiveTerrainTileLoaded(const QJsonObject & msg) { handleTerrainTileLoaded(msg); }

  // The player's report on a terrain load (terrainTileLoaded): every field of the message; a number the player did not
  // send reads -1, so a check can tell "absent" from zero.
  struct TerrainTileReport
  {
    int load = 0;                  // the loadTerrainTile serial this answers
    QString status;                // "built" / "failed" / "superseded" / "cleared"
    QString reason;
    QString hostState;             // a failure caused by a getMapAsset answer: its state and detail
    QString hostDetail;
    int mapId = -1;
    int tileX = -1;
    int tileY = -1;
    int wdtFileDataID = -1;
    int rootFileDataID = -1;
    long long wdtBytes = -1;
    long long rootBytes = -1;
    QString wdtMd5;                // hex MD5 of the bytes the player parsed
    QString rootMd5;
    int chunks = -1;
    int chunksBuilt = -1;
    int chunksSkipped = -1;
    int chunksUnsupported = -1;
    long long vertices = -1;
    long long triangles = -1;
    int holeCells = -1;
    bool hasBounds = false;        // boundsMin/boundsMax present (Unity space, root-local)
    double boundsMin[3] = { 0.0, 0.0, 0.0 };
    double boundsMax[3] = { 0.0, 0.0, 0.0 };
    bool hasRootOffset = false;
    double rootOffset[3] = { 0.0, 0.0, 0.0 };
    bool hasOriginWorld = false;   // world yards the vertices are relative to
    double originWorld[3] = { 0.0, 0.0, 0.0 };
    double wdtRoundTripMs = -1.0;
    double wdtParseMs = -1.0;
    double rootRoundTripMs = -1.0;
    double parseMs = -1.0;
    double buildMs = -1.0;
    double uploadMs = -1.0;
    double firstFrameMs = -1.0;
    double totalMs = -1.0;
    long long meshBytes = -1;
    int liveTerrainTiles = -1;
    int liveModels = -1;
    int liveMapObjects = -1;
    int meshObjects = -1;
    int materialObjects = -1;
    int terrainGameObjects = -1;
    long long managedHeapBytes = -1;
    long long nativeAllocatedBytes = -1;
    // Protocol 13 (TERRAIN TEXTURES above). A player before it sends none: requestsRetired -1, hasMaterial false.
    int requestsRetired = -1;      // the load's requests still out when it ended, retired by the player
    QStringList retiredRequestIds; // their ids, as sent (at most TERRAIN_REPORT_LIST_MAX kept)
    struct Texture
    {
      int fileDataID = -1;
      QString role;                // "diffuse" (a layer's texture) or "height" (its height texture); empty when not sent
      QString state;               // the host's state quoted, or the player's own
      QString detail;
      long long bytes = -1;
      QString md5;
      int width = -1;
      int height = -1;
      QString format;
      int mips = -1;
      double uploadMs = -1.0;
    };
    bool hasMaterial = false;      // the report carries a "material" object
    QString materialState;         // "textured" / "partial" / "unavailable" / "unsupported"
    QString materialReason;
    int tex0FileDataID = -1;
    long long tex0Bytes = -1;
    QString tex0Md5;
    QString alphaFormat;
    std::vector<std::pair<QString, int>> chunkStates;   // the counters as sent, in the message's (sorted) key order
    std::vector<Texture> textures;                      // at most TERRAIN_REPORT_TEXTURES_MAX kept
    int texturesSent = -1;         // the entries the message's textures array held (-1: no array)
    int uniqueTextures = -1;
    int layerReferences = -1;
    int duplicateReferences = -1;
    long long textureSourceBytes = -1;
    long long textureGpuBytes = -1;
    long long alphaGpuBytes = -1;
    long long materialVertexBytes = -1;
    int materials = -1;
    double tex0RoundTripMs = -1.0;
    double tex0ParseMs = -1.0;
    double textureFetchMs = -1.0;
    double textureUploadMs = -1.0;
    double alphaBuildMs = -1.0;
    double materialBuildMs = -1.0;
    // A chunk-state counter by name (-1 when not sent).
    int chunkState(const QString & name) const;
    QJsonObject message;           // the message as received
    // Protocol 14: the view the tile was drawn in (the player's view names), empty when not sent.
    QString view;
    // Protocol 16 (WORLD MODELS IN THE WORLD above): the world's outcome, when the report carries "world".
    bool hasWorld = false;
    QString worldKind;             // "tile" / "globalWmo"
    QString worldState;            // "complete" / "partial" / "none" / "failed" / "ended"
    QString worldReason;
    int worldRecords = -1, worldPlacements = -1, worldPlaced = -1, worldFailed = -1, worldUnsupported = -1;
    int worldNotRequested = -1, worldEnded = -1;
    int worldModels = -1, worldModelsBuilt = -1, worldModelsFailed = -1, worldModelsNotRequested = -1;
    int worldGroupFiles = -1, worldGroupFilesMissing = -1, worldTextures = -1, worldTexturesDecoded = -1;
    int worldTexturesMissing = -1;
    long long worldFileBytes = -1, worldTextureBytes = -1, worldGpuGeometryBytes = -1, worldGpuTextureBytes = -1;
    bool worldHasOrigin = false;
    double worldOrigin[3] = { 0.0, 0.0, 0.0 };
    bool worldStart = false;       // a start point was found in the global world model's geometry
    double worldStartPoint[3] = { 0.0, 0.0, 0.0 };
    QString worldStartReason;
    double worldShownMs = -1.0, worldFetchMs = -1.0, worldParseMs = -1.0, worldDecodeMs = -1.0, worldBuildMs = -1.0;
    double worldBuildMaxMs = -1.0, worldPlaceMs = -1.0;
    struct Placement
    {
      quint32 uniqueId = 0;
      int fileDataID = -1;
      int records = -1;
      QString state;               // "placed" / "failed" / "unsupported" / "notRequested" / "ended"
      QString reason;
      double scale = 1.0;
      int groups = -1, groupsDrawn = -1;
      double position[3] = { 0.0, 0.0, 0.0 };
      double rotation[3] = { 0.0, 0.0, 0.0 };
      double extents[6] = { 0, 0, 0, 0, 0, 0 };
      bool hasRendered = false;    // the instance's drawn box, world frame
      double rendered[6] = { 0, 0, 0, 0, 0, 0 };
    };
    std::vector<Placement> worldList;   // at most TERRAIN_REPORT_PLACEMENTS_MAX kept
    int worldListSent = -1;
    // Protocol 17 (DOODADS above): the whole world's assembly ("complete" / "partial" / "none" / "failed" / "ended"; empty
    // from an older player), and the doodads' outcome, when "world" carries "doodads".
    QString worldAssembly;
    bool hasDoodads = false;
    QString doodadState;           // "complete" / "partial" / "none" / "failed" / "ended" / "notOffered"
    QString doodadReason;
    int doodadMddfRecords = -1, doodadMddfPlacements = -1;          // the obj0's MDDF records, and unique by uniqueId
    int doodadWmoRecords = -1;     // "wmoDoodadRecords": MODD records in the active sets of the placed world models
    int doodadWmoPlacements = -1;  // "wmoPlacementsWithDoodads"
    int doodadInstances = -1;      // planned instances (MDDF and the world models' sets), after the instance budget
    int doodadPlaced = -1, doodadFailed = -1, doodadUnsupported = -1, doodadNotRequested = -1, doodadEnded = -1;
    int doodadModels = -1, doodadModelsBuilt = -1, doodadModelsFailed = -1, doodadModelsUnsupported = -1;
    int doodadModelsNotRequested = -1, doodadModelsPartial = -1;
    int doodadTextures = -1, doodadTexturesDecoded = -1, doodadTexturesMissing = -1, doodadTextureUses = -1;
    int doodadTextureUploads = -1;
    long long doodadFileBytes = -1, doodadTextureBytes = -1, doodadGpuGeometryBytes = -1, doodadGpuTextureBytes = -1;
    double doodadDiscoverMs = -1.0, doodadFirstVisibleMs = -1.0, doodadFetchMs = -1.0, doodadParseMs = -1.0;
    double doodadDecodeMs = -1.0, doodadBuildMs = -1.0, doodadBuildMaxMs = -1.0, doodadPlaceMs = -1.0;
    double doodadPlaceMaxMs = -1.0, doodadReadyMs = -1.0;
    // A world model placement's doodad set, as the player chose it (set 0 and the placement's own: U15, not verified).
    struct DoodadSet
    {
      quint32 uniqueId = 0;        // the world model placement's
      int wmo = -1;                // its FileDataID
      int set = -1;                // the set index chosen
      QString setName;
      int setCount = -1;           // the MODD records the set names
      int active = -1;             // ... of them placed in this load (the active sets')
      QString reason;
    };
    std::vector<DoodadSet> doodadSets;   // at most TERRAIN_REPORT_DOODAD_SETS_MAX kept
    int doodadSetsSent = -1;
    // One doodad model of the load: its outcome, and how many instances it had and placed.
    struct DoodadModel
    {
      int fileDataID = -1;
      QString state;               // "built" / "partial" / "failed" / "unsupported" / "notRequested" / "ended"
      QString reason;
      int instances = -1;
      int placed = -1;
    };
    std::vector<DoodadModel> doodadList; // at most TERRAIN_REPORT_DOODAD_MODELS_MAX kept
    int doodadListSent = -1;
    QString describe() const;
    // The world's outcome in one line (empty without "world"), its doodads in brief.
    QString describeWorld() const;
    // The doodads' outcome in one line, every count and timing (empty without "doodads").
    QString describeDoodads() const;
  };
  // What a report is read into at most; the rest is counted, not kept. The retired ids and the chunk-state counters:
  // TERRAIN_REPORT_LIST_MAX (a protocol-13 player retires at most its outstanding limit of ids, and sends about a dozen
  // counters). The texture entries: TERRAIN_REPORT_TEXTURES_MAX. The player lists every distinct texture of the chunks it
  // draws, asked for or not ("not requested" past maxTexturesPerLoad), and one "no texture id" entry for the layers whose
  // MDID entry is 0. A drawn chunk has at most 8 layers (AdtParser makes more than 8 MCLY entries Malformed; 8 is the
  // most observed), each naming a diffuse texture and, on a map with height textures, a height texture: 256 chunks x 8
  // layers x 2 roles. A layer naming id 0 adds no distinct id of its role, so the "no texture id" entry fits within
  // that count.
  static const int TERRAIN_REPORT_LIST_MAX = MAX_TERRAIN_TEXTURES_PER_LOAD;
  static const int TERRAIN_REPORT_TEXTURES_MAX = 256 * 8 * 2;
  // The placements a report is read into at most (the player lists at most MAX_WMO_PLACEMENTS_PER_LOAD).
  static const int TERRAIN_REPORT_PLACEMENTS_MAX = MAX_WMO_PLACEMENTS_PER_LOAD;
  // The doodads' lists a report is read into at most (protocol 17): the sets and the models the player lists at most (64
  // and 128, which keep a report's line far below the 1 MiB a player's line may have: about 60 KB, estimated, not
  // measured), and the model entries logged one per line (the rest counted by state).
  static const int TERRAIN_REPORT_DOODAD_SETS_MAX = 64;
  static const int TERRAIN_REPORT_DOODAD_MODELS_MAX = 128;
  static const int TERRAIN_REPORT_DOODAD_LOG_MAX = 32;
  // Raised on the GUI thread for every terrainTileLoaded.
  std::function<void(const TerrainTileReport &)> onTerrainTileLoaded;

  // Raised (on the GUI thread) when the player's unityReady arrives -- the host uses it to
  // push the currently displayed model.
  std::function<void()> onUnityReady;

  // One accept/receive/send pass. Driven by the wxTimer normally; the headless self-test calls
  // it directly because no event loop runs there.
  void poll();

  // Diagnostics (also logged with the [unityipc] prefix): counters for self-tests/reports.
  struct Stats
  {
    int connections = 0;
    int requests = 0;       // getAsset + getAssetByFileDataID received
    int responsesOk = 0;
    int responsesError = 0;
    long long bytesServed = 0;
    int skinPushes = 0;     // modelSkin messages sent (the displayed skin changed)
    int animPushes = 0;     // modelAnimation messages sent (the displayed animation changed)
    int statePushes = 0;    // modelAnimationState messages sent (play/pause/speed/time)
    int geosetPushes = 0;   // modelGeosets messages sent (a geoset was switched)
    int geosetAcks = 0;     // modelGeosetsApplied received, any status
    int geosetRejects = 0;  // ... of which "rejected"
    QString lastGeosets;    // "rev <n> hidden [i,j]" of the last modelGeosets sent
    QString lastGeosetAck;  // "rev <n> <status> <reason>" of the last answer
    int scenePushes = 0;    // characterScene messages sent
    int imagePushes = 0;    // characterImage messages sent
    long long imageBytes = 0;   // pixel bytes of the characterImage messages sent (before any base64)
    int sceneAcks = 0;      // characterSceneApplied received, any status
    int sceneApplied = 0;   // ... of which "applied"
    QString lastScene;      // "rev <n>: <merged> merged, <attachments> attached"
    QString lastSceneAck;   // "rev <n> <status> <merged>/<attachments> <reason>"
    int mountScenes = 0;          // characterScene messages sent with a "mount"
    int mountApplied = 0;         // characterSceneApplied received with mountStatus "applied"
    int mountFailed = 0;          // ... with mountStatus "failed"
    QString lastMountScene;       // "<key> <fileDataID> bone <b> seq <s> rider seq <r>" of the last mount sent
    QString lastMountAck;         // "<key> <status> <reason>" of the last answer that named a mount status
    int rolePushes = 0;           // modelAnimation messages sent with a role (a ridden mount's models)
    int riderStatePushes = 0;     // modelAnimationState messages sent with the rider's state
    QString lastRiderState;       // "seq <n> playing|paused <ms>ms x<speed>" of the last rider state sent
    int mapObjectLoads = 0;       // loadWoWModel "kind":"wmo" sent
    int mapObjectReports = 0;     // mapObjectLoaded received, any status
    int mapObjectBuilt = 0;       // ... of which "built"
    int mapObjectFailed = 0;      // ... of which "failed"
    int mapObjectSuperseded = 0;  // ... of which "superseded"
    QString lastMapObject;        // "<fileDataID> load <n> <status> <groups> groups <reason>" of the last report
    int backgroundPushes = 0;     // viewportBackground messages sent
    QString lastBackground;       // "#RRGGBB" of the last viewportBackground sent
    int terrainLoads = 0;         // loadTerrainTile sent
    int terrainClears = 0;        // clearTerrain sent
    int mapAssetRequests = 0;     // getMapAsset received
    int mapAssetsAvailable = 0;   // ... answered with the file ("available")
    int terrainReports = 0;       // terrainTileLoaded received, any status
    int terrainBuilt = 0;         // ... of which "built"
    int terrainFailed = 0;        // ... of which "failed"
    QString lastTerrain;          // "load <n> <status> map <m> tile <x>_<y> <reason>" of the last report
    int worldLoads = 0;           // loadWorld sent (protocol 16)
    int worldShown = 0;           // worldShown received
    int worldProgress = 0;        // worldProgress received
    int worldReports = 0;         // terrainTileLoaded received with "world"
    int doodadReports = 0;        // ... of them with "doodads" (protocol 17)
    int worldInspections = 0;     // inspectWorldModel sent
    int worldInspected = 0;       // worldModelInspected received, any status
    QString lastRequest;    // "path" or "fileDataID n"
    QString lastProvider;   // "CASC" / "MPQ" / ""
    QString lastError;
    QString lastSkin;       // "<fileDataID> (<source>)" of the last skin pushed
    QString lastAnimation;  // "seq <n> animID <id> <ms>ms" of the last animation pushed
    QString lastState;      // "playing|paused <ms>ms x<speed>" of the last state pushed
  };
  const Stats & stats() const { return m_stats; }

private:
  void onPoll(wxTimerEvent & event);
  void onMapResultWake(wxThreadEvent & event);
  void pollAccept();
  void pollReceive();
  void pollSend();
  void handleLine(const std::string & line);
  void handleGetAsset(const QJsonObject & msg, bool byFileDataID);
  void handleGetModelTextures(const QJsonObject & msg);
  void handleGeosetsApplied(const QJsonObject & msg);
  void handleCharacterSceneApplied(const QJsonObject & msg);
  void handleMapObjectLoaded(const QJsonObject & msg);
  void handleRuntimeState(const QJsonObject & msg);
  void handleScreenshotSaved(const QJsonObject & msg);
  void handleGetMapAsset(const QJsonObject & msg);
  // A read dispatched to the worker, until its result is taken back.
  struct MapReadInFlight
  {
    quint64 ticket = 0;
    QJsonObject request;
    qint64 arrivedNs = 0;
    qint64 dispatchedNs = 0;
    double queuedMs = -1.0;        // from its arrival to its dispatch
    double dispatchMs = 0.0;       // the GUI thread's time on its dispatch (the viewer's decision and the submit)
    QJsonObject timings;           // the dispatch's (the decision's)
    unsigned long long connection = 0;   // m_connectionSerial when dispatched
    std::shared_ptr<std::atomic<bool>> cancelled;   // shared with the worker's job
    QString cancelReason;          // why the viewer no longer wanted it ("" while wanted)
    bool orphaned = false;         // its connection ended: dropped unanswered when its result comes
    bool testHold = false;
    bool testCorrupt = false;
  };
  // Answers one request with the answer given -- decided at its dispatch (read null), or at the completion of read
  // (result its worker's result) -- and records it. guiTimer: running since the GUI work on it began in this poll.
  // Returns the bytes read for it.
  long long answerMapAsset(const QJsonObject & msg, double queuedMs, const MapAssetAnswer & answer,
                           const MapReadInFlight * read = nullptr, const MapAssetWorker::Result * result = nullptr,
                           const QElapsedTimer & guiTimer = QElapsedTimer());
  // One poll's serving (THE MAP ASSET QUEUE AND WORKER, in UnityIpcServer.cpp). Returns how many were answered.
  int serveMapAssetQueue();
  // The held requests (TEST HOOKS) and the queue dropped unanswered, and the hold disarmed: the connection they came over
  // has ended.
  void dropHeldMapAssets(const char * why);
  void handleTerrainTileLoaded(const QJsonObject & msg);
  void handleMapTilesInspected(const QJsonObject & msg);
  void handleTerrainViewApplied(const QJsonObject & msg);
  void handleWorldCameraApplied(const QJsonObject & msg);
  void handleWorldShown(const QJsonObject & msg);
  void handleWorldProgress(const QJsonObject & msg);
  void handleWorldModelInspected(const QJsonObject & msg);
  // loadTerrainTile's fields (shared by loadWorld); false when the camera was not put (no protocol-15 player, no mode).
  bool putTerrainCommand(QJsonObject & msg, const TerrainTileCommand & command, bool & textures) const;
  static void putCamera(QJsonObject & msg, const WorldCamera & camera);
  static QString describeCamera(const WorldCamera & camera);
  void queueLine(const QByteArray & line);
  // One line assembled from pieces straight in the send buffer: a characterImage line is ~11 MB, and
  // joining it into one QByteArray first would copy all of it once more.
  void queueLineParts(std::initializer_list<QByteArray> parts);
  // A line and the binary payload it announces (protocol 10): header (one JSON object, no newline, stating
  // "payloadBytes": payload.size()), a newline, the payload, a newline. Only to a player that takes binary frames.
  void queueFrame(const QByteArray & header, const QByteArray & payload, double * appendMs = nullptr,
                  double * sendMs = nullptr);
  static QJsonArray textureArray(const std::vector<UnityAssetAccess::ModelTexture> & textures);
  // Adds "geosets"/"hasGeosets" to a message about one model, when a selection is known.
  static void addGeosets(QJsonObject & msg, int m2FileDataID);

  // The item ParticleColor override, when the displayed model has one. Attached to the same two
  // messages as the geosets, so a renderer that has the textures always has the colours that go
  // with them and the two can never describe different states.
  static void addParticleColor(QJsonObject & msg, int m2FileDataID);
  void queueJson(const QJsonObject & obj);
  void dropClient(const char * why);
  // THE TRANSPORT CAP: the receive pause updated from the bytes waiting; complete lines of the receive buffer handled
  // while the poll's budget lasts (false: a handler dropped the client); a message admitted to the send buffer or not
  // (the hard cap); the stall of the bytes waiting checked; the client dropped at a safe point of a poll when asked.
  void handleStreamState(const QJsonObject & msg);
  void logMapAnswerSummary(bool force);
  void updateReceivePause();
  bool handleBufferedLines(int & budget);
  bool admitQueued(size_t bytes);
  void noteQueued();
  void checkSendStall();
  void dropPendingClient();

  wxTimer m_timer;
  unsigned long long m_listen = 0;   // SOCKET (kept opaque here to keep winsock out of the header)
  unsigned long long m_client = 0;
  int m_port = 0;
  bool m_unityReady = false;
  int m_playerProtocol = 0;          // protocolVersion from the player's unityReady
  // The composited images this connection has been sent, by kind, and the id each went under.
  std::map<QString, QImage> m_sentImages;
  std::map<QString, QString> m_sentImageIds;
  int m_imageSerial = 0;
  int m_runtimeQuery = 0;            // the last runtimeState question's number
  int m_screenshotRequest = 0;       // the last captureScreenshot request's number
  std::string m_inBuf;              // partial incoming line
  std::string m_outBuf;              // pending bytes to send (partial sends are normal for big assets)
  // How much of m_outBuf has gone out. Sent bytes are dropped from the front only when they are more
  // than half the buffer (or all of it): erasing them after every send moved the whole unsent rest
  // each time, which for an 11 MB image line sent 256 KB at a time was hundreds of MB of memmove.
  size_t m_outPos = 0;
  Stats m_stats;
  std::deque<MapAssetServed> m_mapAssetsServed;       // the most recent MAP_ASSET_RECORDS_KEPT, emptied by start()
  long long m_mapAssetsServedTotal = 0;               // answers since start()
  long long m_mapAssetRecordsDropped = 0;             // ... no longer kept
  MapAssetReadTracker m_mapAssetReads{MAP_ASSET_READS_TRACKED};
  bool m_holdNextMapAsset = false;                    // TEST HOOKS
  QString m_holdMapAssetRole;
  std::vector<QJsonObject> m_mapAssetsHeld;
  long long m_mapAssetsHeldDropped = 0;
  // THE MAP ASSET QUEUE: each request with when it arrived (m_clock), at most MAP_ASSET_QUEUE_MAX.
  struct QueuedMapAsset
  {
    QJsonObject request;
    qint64 arrivedNs = 0;
  };
  std::deque<QueuedMapAsset> m_mapAssetQueue;
  // THE MAP ASSET WORKER: the viewer's worker, the reads in flight (at most MAP_ASSET_READS_IN_FLIGHT), their accounting,
  // the connection count that tells one connection's reads from the next's, and the test hooks of the reads.
  std::shared_ptr<MapAssetWorker> m_mapAssetWorker;
  std::deque<MapReadInFlight> m_mapReadsInFlight;
  MapReadStats m_mapReadStats;
  bool m_wakeOnResults = false;
  std::atomic<bool> m_wakePosted{false};   // a wake waits in the event queue (set by the worker, lowered by the GUI)
  Activity m_activity;                     // activity()
  // THE VIEWER'S OWN READS: their tickets and cancel flags, until their results are taken back.
  struct ViewerRead
  {
    quint64 ticket = 0;
    std::shared_ptr<std::atomic<bool>> cancelled;
  };
  std::vector<ViewerRead> m_viewerReads;
  bool m_detached = false;                 // detachForShutdown()
  quint64 m_resultsSeen = 0;               // the worker's results made, as the last serving poll saw them
  qint64 m_wakeBurstEndNs = -1;            // when the last wake poll ended (m_clock)
  long long m_viewerResultsTaken = 0;      // the viewer's own results taken back, ever (a wake counts them as taken)
  double m_wakeBurstMs = 0.0;              // GUI time of the run of wakes it belongs to
  unsigned long long m_connectionSerial = 0;
  bool m_holdNextMapRead = false;
  bool m_holdMapReadBeforeOpen = false;
  QString m_holdMapReadRole;
  bool m_corruptNextMapRead = false;
  QString m_corruptMapReadRole;
  int m_mapReadDelayMs = 0;
  int m_holdReportLoad = 0;                           // TEST HOOKS: testHoldTerrainReport
  QJsonObject m_heldReport;
  long long m_fakeUnsentBytes = 0;                    // TEST HOOKS: testFakeUnsentBytes
  QElapsedTimer m_clock;                              // started with the server object: the queue's clock
  qint64 m_lastPollNs = -1;                           // m_clock when the last poll began (onPollTiming)
  int m_pollMapAnswered = 0;                          // map requests answered in the poll running now
  double m_pollRecordMs = 0.0;                        // time in onMapAssetServed in the poll running now
  std::deque<MapAssetPoll> m_mapAssetPolls;           // the most recent MAP_ASSET_POLL_TIMINGS_KEPT, emptied by start()
  long long m_mapAssetPollsTotal = 0;
  long long m_mapAssetsQueued = 0;
  long long m_mapAssetsRefusedQueueFull = 0;
  long long m_mapAssetsAnsweredStaleWaiting = 0;
  long long m_mapAssetsQueueDropped = 0;
  int m_mapAssetQueueDepthMax = 0;
  // THE TRANSPORT CAP
  TransportStats m_transportStats;
  bool m_receivePaused = false;
  std::string m_dropPending;            // why the client is to be dropped at the next safe point of a poll ("": not)
  qint64 m_sendProgressNs = -1;         // m_clock when a send last made progress, or bytes began to wait
  long long m_receivePauseBytes = TRANSPORT_RECEIVE_PAUSE_BYTES;
  long long m_receiveResumeBytes = TRANSPORT_RECEIVE_RESUME_BYTES;
  long long m_hardCapBytes = TRANSPORT_HARD_CAP_BYTES;
  int m_stallDropMs = TRANSPORT_STALL_DROP_MS;
  int m_linesPerPoll = TRANSPORT_LINES_PER_POLL;
  int m_sendBufferBytes = 64 * 1024 * 1024;
  // STREAMING
  bool m_streaming = false;
  int m_mapAssetQueueMax = MAP_ASSET_QUEUE_MAX;
  int m_mapReadsInFlightMax = MAP_ASSET_READS_IN_FLIGHT;
  bool m_logEachMapAnswer = true;
  long long m_answersSinceSummary = 0, m_answeredAvailableSinceSummary = 0, m_answeredStaleSinceSummary = 0;
  long long m_answeredOtherSinceSummary = 0, m_answerBytesSinceSummary = 0;
  qint64 m_answerSummaryNs = -1;
  StreamState m_lastStreamState;
  long long m_streamStatesReceived = 0;
  qint64 m_streamStateLogNs = -1;
};

// TEST HOOK (docs/world-viewer/08-streaming.md 1.5): a raw local client of a server's port, for the in-process check of
// the transport cap with a real socket. It connects with a receive buffer of the given size, writes text whole, and
// reads only when told to.
class UnityIpcTestClient
{
public:
  UnityIpcTestClient() = default;
  ~UnityIpcTestClient();
  UnityIpcTestClient(const UnityIpcTestClient &) = delete;
  UnityIpcTestClient & operator=(const UnityIpcTestClient &) = delete;
  bool connectTo(int port, int receiveBufferBytes);
  bool sendText(const std::string & text);                  // blocking, whole
  long long readAvailable(std::string * into = nullptr);    // what has arrived (non-blocking); -1 once the server closed
  void close();
  bool connected() const { return m_socket != 0; }

private:
  unsigned long long m_socket = 0;
};

#endif // UNITYIPCSERVER_H
