// WmvIpcClient.cs
//
// IPC client of the WMV Unity viewport player. WMV is the IPC SERVER: it starts a
// localhost TCP listener before launching the player and passes the port on the player
// command line ("-wmvPort <n>"); the player connects back, announces itself and then asks
// WMV for whatever it needs. Transport: newline-delimited JSON (one object per line),
// protocol version 6 (4 added world models: loadWoWModel "kind", mapObjectLoaded and runtimeState;
// 5 added mounted characters: characterScene "mount", its answer's mount fields, runtimeState's
// mountFileDataID, modelAnimation "role" and "load", and modelAnimationState "load", "hasRider" and
// "rider"; 6 added captureScreenshot and its answer screenshotSaved).
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
//                           bodyRebinds, viewFramings }
//     the answer to runtimeState: what the player holds right now -- the runtimes alive, the
//     fileDataID of the model and of the world model on screen (0 for none), whether a load of
//     either kind is in flight, and the mount the model on screen rides (0 for none) with its key,
//     the mount runtimes alive and built so far, how the model hangs from it (RuntimeReport), what
//     each animator plays, the mount's emitters and live particles, how often the character's
//     body textures were bound again, and how often the view was fitted to what is on screen.
//     A test's question, answered from the main thread in message order
//   screenshotSaved       { request, ok, error, path, width, height, bytes, renderMs, encodeMs, writeMs, totalMs }
//     the answer to captureScreenshot (protocol 6): the PNG was written to path (ok, with its size in bytes and
//     how long the off-screen render and readback, the PNG encode, the file write and the whole capture took,
//     in milliseconds), or why not (error). request echoes the question's number
//
// WMV -> player
//   loadWoWModel  { path, fileDataID, client, character, load, kind }
//     character: a playable character, dressed by the characterScene that follows
//     load: the host's serial for this load (> 0), echoed in every characterSceneApplied about it
//     kind: "m2" (also when absent) or "wmo" -- a world model: path/fileDataID name the ROOT file
//   runtimeState  { query }                                                        (protocol 4)
//     asks for a runtimeState answer carrying the same query number
//   captureScreenshot { request, path, width, height }                             (protocol 6)
//     render what the viewport shows once more, off screen, at width x height with a transparent
//     background, and write it as a PNG to path (absolute, chosen and confirmed by the host's Save As);
//     answered by screenshotSaved with the same request number (see WmvScreenshot.cs)
//   assetResponse { requestId, ok, path, fileDataID, byteLength, sha1, encoding:"base64", data }
//   assetResponse { requestId, ok:false, error }
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
//   characterImage { hash, kind, width, height, format:"bgra8", encoding:"base64", data }
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
// V1 carries asset bytes as base64 inside the JSON line (simple, debuggable). A binary
// frame (JSON header + length-prefixed payload) can replace it later without changing
// the request side.
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

public class WmvIpcClient : MonoBehaviour
{
    public const int ProtocolVersion = 6;

    /// <summary>The loadWoWModel kind of a world model; anything else is an M2.</summary>
    public const string KindMapObject = "wmo";

    // Raised on the main thread.
    public Action<string, int, string, bool, int, string> OnLoadWoWModel;  // (path, fileDataID, client, character, load, kind)
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
        public string sha1;        // as reported by WMV
        public string error;
        public byte[] data;        // decoded bytes (null on error)
        public string localSha1;   // computed here over the decoded bytes
        public bool hashMatches { get { return ok && data != null && localSha1 == (sha1 ?? "").ToLowerInvariant(); } }
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
        public int load;              // also a ridden mount's animation pushes (protocol 5)
        public int query;             // runtimeState
        public int request;           // captureScreenshot (protocol 6); its path, width and height are the fields above and below
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
        public SceneBody body;
        public SceneMerged[] merged;
        public SceneAttachment[] attachments;
        public SceneMount mount;
        [NonSerialized] public CharacterImage decodedImage;
    }

    int port = -1;
    TcpClient client;
    NetworkStream stream;
    Thread thread;
    volatile bool stopping;
    volatile bool connected;
    readonly object sendLock = new object();
    readonly Queue<Msg> inbox = new Queue<Msg>();

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

            using (var reader = new StreamReader(stream, Encoding.UTF8))
            {
                string line;
                while (!stopping && (line = reader.ReadLine()) != null)
                {
                    if (line.Trim().Length == 0) continue;
                    Msg msg = null;
                    try { msg = JsonUtility.FromJson<Msg>(line); }
                    catch (Exception e) { Debug.LogWarning("WMV IPC: bad JSON line: " + e.Message); }
                    if (msg != null)
                    {
                        msg.receivedSeconds = NowSeconds;
                        ProjectFromSample(msg);
                        // A composited image is megabytes of base64. Decoded HERE, on the reader
                        // thread, so the frame it lands in only swaps a reference.
                        if (msg.type == "characterImage")
                        {
                            msg.decodedImage = DecodeCharacterImage(msg);
                            msg.data = null;
                        }
                        lock (inbox) inbox.Enqueue(msg);
                    }
                }
            }
        }
        catch (Exception e)
        {
            Status("WMV connection failed/closed: " + e.Message);
        }
        // The using(StreamReader) above disposed the NetworkStream -- stop Send from touching it.
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
                if (inbox.Count == 0) return;
                msg = inbox.Dequeue();
            }
            try { Dispatch(msg); }
            catch (Exception e) { Debug.LogWarning("WMV IPC: handler failed: " + e.Message); }
        }
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
            if (msg.encoding != "base64" || msg.format != "bgra8" || string.IsNullOrEmpty(msg.data))
            {
                result.error = "unsupported characterImage (" + msg.encoding + ", " + msg.format + ")";
                return result;
            }
            byte[] bytes = Convert.FromBase64String(msg.data);
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
                // An absent kind is an M2: that is what every host before protocol 4 meant.
                OnLoadWoWModel?.Invoke(msg.path ?? "", msg.fileDataID, msg.client ?? "active", msg.character, msg.load,
                                       string.IsNullOrEmpty(msg.kind) ? "m2" : msg.kind);
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
                    byteLength = msg.byteLength, sha1 = msg.sha1, error = msg.error,
                };
                if (msg.ok)
                {
                    // JsonUtility materialises absent string fields as "" (never null).
                    if (msg.encoding == "base64" && !string.IsNullOrEmpty(msg.data))
                    {
                        try
                        {
                            r.data = Convert.FromBase64String(msg.data);
                            using (var sha = SHA1.Create())
                                r.localSha1 = BitConverter.ToString(sha.ComputeHash(r.data)).Replace("-", "").ToLowerInvariant();
                        }
                        catch (FormatException e)
                        {
                            // Attribute the failure to this request instead of orphaning it.
                            r.ok = false;
                            r.data = null;
                            r.error = "base64 decode failed: " + e.Message;
                        }
                    }
                    else
                    {
                        r.ok = false;
                        r.error = string.IsNullOrEmpty(msg.encoding)
                            ? "assetResponse without encoding/data"
                            : "unsupported encoding: " + msg.encoding;
                    }
                }
                OnAssetResponse?.Invoke(r);
                break;
            }

            default:
                Debug.LogWarning("WMV IPC: unknown message type '" + msg.type + "'");
                break;
        }
    }

    // ---- requests to WMV ----

    public string RequestAsset(string path)
    {
        var id = NewRequestId();
        Send("{\"type\":\"getAsset\",\"requestId\":\"" + id + "\",\"path\":\"" + Escape(path) + "\"}");
        return id;
    }

    public string RequestAssetByFileDataID(int fileDataID)
    {
        var id = NewRequestId();
        Send("{\"type\":\"getAssetByFileDataID\",\"requestId\":\"" + id + "\",\"fileDataID\":" + fileDataID + "}");
        return id;
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
             ",\"viewFramings\":" + r.ViewFramings + "}");
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

    void Send(string json)
    {
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
