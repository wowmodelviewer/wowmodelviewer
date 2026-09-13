// WmvIpcClient.cs
//
// IPC client of the WMV Unity viewport player. WMV is the IPC SERVER: it starts a
// localhost TCP listener before launching the player and passes the port on the player
// command line ("-wmvPort <n>"); the player connects back, announces itself and then asks
// WMV for whatever it needs. Transport: newline-delimited JSON (one object per line),
// protocol version 3.
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
//                           merged, attachments, missing:[key,...], ms }
//     the answer to characterScene: the character on screen now wears that scene (less any part
//     named in missing), or why not. load is the serial of the loadWoWModel the scene belonged to
//     (0 when it matched no load). "superseded" means a newer scene or a new load replaced it before
//     it was applied, which is not a failure; "rejected" is a real refusal
//
// WMV -> player
//   loadWoWModel  { path, fileDataID, client, character, load }
//     character: a playable character, dressed by the characterScene that follows
//     load: the host's serial for this load (> 0), echoed in every characterSceneApplied about it
//   assetResponse { requestId, ok, path, fileDataID, byteLength, sha1, encoding:"base64", data }
//   assetResponse { requestId, ok:false, error }
//   modelTextures { requestId, ok, fileDataID, textures:[{ index, type, fileDataID, source }] }
//   modelSkin     { fileDataID, textures:[...], geosets:[...], hasGeosets,
//                   hasSubmeshVisible, submeshCount, submeshVisible:[0|1,...] }  (pushed, no request)
//     (modelTextures replies carry the same geoset fields)
//   modelGeosets  { fileDataID, revision, submeshCount, submeshVisible:[0|1,...] } (pushed, no request)
//     the host's whole per-submesh display state for the displayed model, sent when the user
//     switches a geoset; indexed by skin submesh index (SFID[0])
//   modelAnimation { fileDataID, sequenceIndex, animID, durationMs, loop }         (pushed, no request)
//   modelAnimationState { fileDataID, sequenceIndex, playing, timeMs, speed, loop, explicitState } (pushed, no request)
//     explicitState: true when a control set the state (play, pause, a frame step, a scrub,
//     the start of a load), false for the heartbeat
//   characterImage { hash, kind, width, height, format:"bgra8", encoding:"base64", data }
//     a host-composited texture (the body, the eyes), rows top first, bytes B,G,R,A; named by
//     hash in the scenes that follow. The player keeps the newest image of each kind for the
//     life of the connection -- the host sends a kind again only when its pixels change -- and
//     an older one for as long as a scene not yet applied still names it
//   characterScene { fileDataID, revision, body:{...}, merged:[...], attachments:[...] }
//     the resolved state of the character on display: per model the texture each slot binds, the
//     geoset display flags, the host's bone table for a merged model and the attachment it hangs
//     from for an attached one (see UnityCharacterScene.h in the host)
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
    public const int ProtocolVersion = 3;

    // Raised on the main thread.
    public Action<string, int, string, bool, int> OnLoadWoWModel;  // (path, fileDataID, client, character, load)
    public Action<AssetResponse> OnAssetResponse;
    public Action<ModelTexturesResponse> OnModelTextures;
    public Action<ModelTexturesResponse> OnModelSkin;          // pushed when the displayed skin changes
    public Action<AnimationSelection> OnModelAnimation;        // pushed when the displayed animation changes
    public Action<AnimationState> OnModelAnimationState;       // pushed on play/pause/speed/time changes
    public Action<GeosetVisibility> OnModelGeosets;            // pushed when the user switches a geoset
    public Action<CharacterImage> OnCharacterImage;            // a host-composited texture
    public Action<CharacterScene> OnCharacterScene;            // the character's resolved state
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

    public class CharacterScene
    {
        public int fileDataID;
        public int revision;
        public SceneBody body;
        public SceneMerged[] merged = new SceneMerged[0];
        public SceneAttachment[] attachments = new SceneAttachment[0];
        public double receivedSeconds;
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
        public int revision;
        public bool hasSubmeshVisible;
        public int submeshCount;
        public int[] submeshVisible;
        public bool character;
        public int load;
        // characterImage
        public string hash;
        public string kind;
        public int width;
        public int height;
        public string format;
        // characterScene
        public SceneBody body;
        public SceneMerged[] merged;
        public SceneAttachment[] attachments;
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
                OnLoadWoWModel?.Invoke(msg.path ?? "", msg.fileDataID, msg.client ?? "active", msg.character, msg.load);
                break;

            case "characterImage":
                OnCharacterImage?.Invoke(msg.decodedImage ?? new CharacterImage { hash = msg.hash, error = "not decoded" });
                break;

            case "characterScene":
                OnCharacterScene?.Invoke(new CharacterScene
                {
                    fileDataID = msg.fileDataID,
                    revision = msg.revision,
                    body = msg.body ?? new SceneBody(),
                    merged = msg.merged ?? new SceneMerged[0],
                    attachments = msg.attachments ?? new SceneAttachment[0],
                    receivedSeconds = msg.receivedSeconds,
                });
                break;

            case "modelTextures":
                OnModelTextures?.Invoke(ReadTextures(msg));
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
                OnModelAnimationState?.Invoke(new AnimationState
                {
                    fileDataID = msg.fileDataID,
                    sequenceIndex = msg.sequenceIndex,
                    playing = msg.playing,
                    timeMs = msg.timeMs,
                    speed = msg.speed,
                    loop = msg.loop,
                    explicitState = msg.explicitState,
                    receivedSeconds = msg.receivedSeconds,
                });
                break;

            case "modelAnimation":
                OnModelAnimation?.Invoke(new AnimationSelection
                {
                    fileDataID = msg.fileDataID,
                    sequenceIndex = msg.sequenceIndex,
                    animID = msg.animID,
                    durationMs = msg.durationMs,
                    loop = msg.loop,
                });
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
    /// the character it is showing now. missing names the parts that could not be built.
    /// </summary>
    public void ReportCharacterSceneApplied(int fileDataID, int load, int revision, string status, string reason,
                                            int merged, int attachments, IList<string> missing, long ms)
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
        sb.Append("]}");
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
