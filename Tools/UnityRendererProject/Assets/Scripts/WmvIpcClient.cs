// WmvIpcClient.cs
//
// IPC client of the WMV Unity viewport player. WMV is the IPC SERVER: it starts a
// localhost TCP listener before launching the player and passes the port on the player
// command line ("-wmvPort <n>"); the player connects back, announces itself and then asks
// WMV for whatever it needs. Transport: newline-delimited JSON (one object per line),
// protocol version 11 (4 added world models: loadWoWModel "kind", mapObjectLoaded and runtimeState;
// 5 added mounted characters: characterScene "mount", its answer's mount fields, runtimeState's
// mountFileDataID, modelAnimation "role" and "load", and modelAnimationState "load", "hasRider" and
// "rider"; 6 added captureScreenshot and its answer screenshotSaved; 7 added viewportBackground and
// runtimeState's background fields; 8 added loadWoWModel "keepView" and runtimeState's keptViews; 9 added the asset
// cache: loadWoWModel and prefetchAssets "assetEpoch", assetResponse "cacheable", prefetchAssets, and runtimeState's
// assetCache fields -- see WmvAssetCache; 10 added binary payloads: an assetResponse's file and a characterImage's
// pixels follow their line as raw bytes, "payloadBytes" long -- see WmvStreamReader; 11 added characterScene
// "attachmentsOnly" and "load" for the equipment of an ordinary NPC model).
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
    public const int ProtocolVersion = 11;

    /// <summary>The loadWoWModel kind of a world model; anything else is an M2.</summary>
    public const string KindMapObject = "wmo";

    // Raised on the main thread.
    public Action<string, int, string, bool, int, string, bool> OnLoadWoWModel;  // (path, fileDataID, client, character, load, kind, keepView)
    public Action<AssetResponse> OnAssetResponse;
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
    public Action<string> OnStatus;                            // human-readable connection/state text

    public bool Connected { get { return connected; } }
    public int Port { get { return port; } }

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
    readonly object sendLock = new object();
    readonly Queue<Msg> inbox = new Queue<Msg>();
    // The session asset cache (protocol 9): main thread only, like everything but the socket read.
    readonly WmvAssetCache cache = new WmvAssetCache();
    // Its answers, handed out in Update after the host's messages, within LocalAnswerBudgetMs a frame (at least one):
    // a load the cache answers whole would otherwise parse its model, skin and textures and build the body in one
    // frame, and the model on screen would stand still for all of it.
    readonly Queue<Msg> localAnswers = new Queue<Msg>();
    const double LocalAnswerBudgetMs = 8.0;

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
            return msg;
        }
        return null;
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
                OnRuntimeState?.Invoke(msg.query);
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
             ",\"assetEpoch\":" + cache.Epoch + "}");
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
