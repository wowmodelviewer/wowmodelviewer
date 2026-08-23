// WmvIpcClient.cs
//
// IPC client of the WMV Unity viewport player. WMV is the IPC SERVER: it starts a
// localhost TCP listener before launching the player and passes the port on the player
// command line ("-wmvPort <n>"); the player connects back, announces itself and then asks
// WMV for whatever it needs. Transport: newline-delimited JSON (one object per line),
// protocol version 1.
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
//
// WMV -> player
//   loadWoWModel  { path, fileDataID, client }
//   assetResponse { requestId, ok, path, fileDataID, byteLength, sha1, encoding:"base64", data }
//   assetResponse { requestId, ok:false, error }
//   modelTextures { requestId, ok, fileDataID, textures:[{ index, type, fileDataID, source }] }
//   modelSkin     { fileDataID, textures:[...], geosets:[...], hasGeosets }        (pushed, no request)
//   modelAnimation { fileDataID, sequenceIndex, animID, durationMs, loop }         (pushed, no request)
//   modelAnimationState { fileDataID, sequenceIndex, playing, timeMs, speed, loop } (pushed, no request)
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
    public const int ProtocolVersion = 1;

    // Raised on the main thread.
    public Action<string, int, string> OnLoadWoWModel;        // (path, fileDataID, client)
    public Action<AssetResponse> OnAssetResponse;
    public Action<ModelTexturesResponse> OnModelTextures;
    public Action<ModelTexturesResponse> OnModelSkin;          // pushed when the displayed skin changes
    public Action<AnimationSelection> OnModelAnimation;        // pushed when the displayed animation changes
    public Action<AnimationState> OnModelAnimationState;       // pushed on play/pause/speed/time changes
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
        public bool playing;
        public int timeMs;
        public float speed;
    }

    int port = -1;
    TcpClient client;
    NetworkStream stream;
    Thread thread;
    volatile bool stopping;
    volatile bool connected;
    readonly object sendLock = new object();
    readonly Queue<Msg> inbox = new Queue<Msg>();
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
                        lock (inbox) inbox.Enqueue(msg);
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
        return r;
    }

    void Dispatch(Msg msg)
    {
        switch (msg.type)
        {
            case "loadWoWModel":
                OnLoadWoWModel?.Invoke(msg.path ?? "", msg.fileDataID, msg.client ?? "active");
                break;

            case "modelTextures":
                OnModelTextures?.Invoke(ReadTextures(msg));
                break;

            // Unsolicited: the skin on display in WMV changed. Same payload as a modelTextures
            // reply, minus the requestId -- nothing asked for it.
            case "modelSkin":
                OnModelSkin?.Invoke(ReadTextures(msg));
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
