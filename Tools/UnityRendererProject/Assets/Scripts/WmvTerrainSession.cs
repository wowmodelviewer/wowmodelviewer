// WmvTerrainSession.cs
//
// THE TERRAIN LOAD AND ITS SCENE (protocol 12; stage 03 design, section 5; protocol 13 textures: stage 04 design,
// sections 2 and 4): one tile of the active client's map, fetched from the host, parsed and built by the verified
// pure-C# code, and drawn on its own layer apart from the model and world-model jobs.
//
//   loadTerrainTile { load, assetEpoch, mapId, wdtFileDataID, tileX, tileY, maxAssetBytes,
//                     maxTextureBytes, maxTextureBytesPerLoad, maxTexturesPerLoad }
//     -> getMapAsset role "wdt"        the map's WDT, never from the asset cache; the host checks the load, the epoch
//                                      and the catalogue before it reads, and says what it decided (state, detail)
//     -> WdtParser.Parse               on the main thread (a WDT is at most 0.3 MB): the tile's MAIN flag, then MAID
//                                      word 0 -- the root ADT's FileDataID in all four audited clients -- and word 3,
//                                      the tex0 ADT's, and MPHD word 0 (the alpha depth the map announces)
//     -> getMapAsset role "rootAdt"    and, beside it, role "tex0Adt" when the host offers textures (protocol 13:
//                                      loadTerrainTile states maxTextureBytes; a protocol-12 host states none)
//     -> worker thread                 root: md5, AdtParser.ParseRoot, TerrainMeshBuilder.Build (Wow/World/TerrainMesh.cs);
//                                      tex0: md5, AdtParser.ParseTex; then TerrainMaterialBuilder.Build (the chunk
//                                      states, layers, alpha slices, vertex colours: Wow/World/TerrainMaterial.cs)
//     -> getMapAsset role "texture"    each distinct texture of the chunks drawn textured, once -- their diffuse textures
//                                      (MDID) and, on an MPHD 0x80 map, their height textures (MHID: the height mix
//                                      reads them), diffuse first; answered textures are verified and prepared on the
//                                      worker (WmvTerrainTexture.Prepare)
//     -> main thread (Pump)            the Unity objects, built INACTIVE (IWmvTerrainView.BuildTile: meshes, textures,
//                                      alpha array, materials), then THE COMMIT -- the new tile becomes the scene's tile
//                                      in the same block that disposes every other one -- then shown (activated, the
//                                      terrain view entered, framed)
//     -> terrainTileLoaded             "built" once frames N+1 and N+2 have started after the adoption in frame N
//                                      (the guarantee the host's two-question uncover fence gives a model)
//
// Rules:
//   - EXACTLY ONE REPORT PER LOAD: built | failed | superseded | cleared. A load is "in flight" until it is reported,
//     including the two frames after its adoption; a second report is never sent (one flag per load).
//   - LATEST COMMAND WINS: a new loadTerrainTile answers the load in flight "superseded"; a model load does the same
//     (LeaveForModelLoad) and takes the tile and the terrain view away; clearTerrain answers a load in flight
//     "cleared" and takes a tile on screen away WITHOUT a report (its load was already reported).
//   - BEFORE THE COMMIT any failure releases what the new tile built (Live back) and reports "failed"; the previous
//     tile and the view stay as they were. AFTER IT a failure (showing, framing) reports "built" with the reason
//     "adopted, but finishing the adoption failed: ...". Either way the load takes no answer or worker result any more.
//   - THE MATERIAL NEVER FAILS A LOAD. A tex0 that is not available (any host state, a bad md5, a parse failure), a
//     material build that throws, or a host that offers no textures leaves the tile "built" with its material
//     "unavailable" and the reason, drawn neutral. A diffuse texture that cannot be had (any state) is drawn as the
//     missing-texture checker in its place in every drawable chunk that uses it, and makes the Textured ones among them
//     MissingTexture (TerrainTileMaterial.ApplyTextureFailures); a chunk in a state above MissingTexture keeps it (the
//     contract's precedence: a LayerEffectStatic chunk stays LayerEffectStatic and draws the checker too -- the settle
//     log counts every drawable chunk that draws it). A height texture that cannot be had (or has no alpha channel)
//     makes the chunks whose height mix reads it HeightTextureMissing (drawn neutral; WmvTerrainTileContent.SettleFailures).
//     A load that ends before its adoption reports its material settled the same way with the texture failures already
//     answered; textures still requested or queued are no failure yet, and the material's reason says how many.
//   - OWNERSHIP OF THE MAP ANSWERS IS BY ROUTING, NOT BY MEMORY. getMapAsset ids are the client's own "map<n>"
//     namespace, and WmvIpcClient hands every map answer (that id, or a role) to ClaimMapAssetResponse and to nothing
//     else -- never the asset cache, never the model, character or world-model handlers. The load in flight waits for
//     AT MOST MaxOutstanding (K = 4) requests at once -- the WDT alone, then the root and the tex0, then textures; a
//     texture counts against K from its request until its upload data are ready (so at most K textures are fetched or
//     decoded at once); prepared data then wait in the job until BuildTile, their total bounded by the host's
//     maxTextureBytesPerLoad on the bytes read (the RGBA32 fallback for palettized, raw or non-block-sized files can hold
//     several times that) -- and an answer is applied only when it names one of them (and that load, when it states
//     one). The textures asked for are capped by maxTexturesPerLoad, counted in report ENTRIES (an id with both roles
//     counts twice), so the report never lists more entries in a request state than the host allows.
//     When a load ends by any path (a report of any kind, the timeout included, or Dispose) EVERY request it still waits
//     for moves to a ring of the last RetiredCapacity (128) ended requests, oldest dropped first, so that an answer
//     coming after its load ended is dropped and said as "late" (or "unowned" when the ring no longer holds it, or never
//     did). Whatever the host does -- never answer, answer late, answer twice -- the session holds at most K pending and
//     128 retired requests. Counters say what happened: RetiredTotal counts every retirement (it does not stop at the
//     ring's 128), LastRetiredRequest names the newest, the report of the load names how many it retired and which
//     (requestsRetired, retiredRequestIds), and the late and unowned drops are counted apart.
//   - TimeoutMs (30 s; -wmvTerrainTimeoutMs, 1..120 s) after the first Pump that saw it, a load not yet adopted fails
//     "timed out after N s" -- one timeout over the whole load, its textures included.
//   - Failure reasons are precise: the host's state and detail (hostState, hostDetail), the parser's exception text,
//     "no tile at x_y (MAIN flag clear)", "MAID names no root ADT for x_y", "root ADT does not belong to tile x_y ...",
//     "no decodable chunk ...", "answer of N bytes exceeds maxAssetBytes M", a hash that differs from the host's md5.
//   - One tile at a time in this stage, but the scene is a dictionary keyed (mapId, tileX, tileY): a commit disposes
//     every entry other than the new tile (one with the same key first), and a commit for another client epoch or map
//     is a scene reset (logged). Every tile is built relative to its own north-west corner with its root at 0.
//
// Testable without WmvMain: the host requests, the report, the log, the worker and the Unity side are injected; the
// self-test (WmvLifecycleSelfTest.TerrainTests) runs the worker synchronously and simulates frames by calling Pump.
// Every step is logged with the "[terrain]" prefix.
//
// WORLD LOADS (protocol 16, stage 06): loadWorld places world models too -- the tile's MODF records (its obj0 ADT) or a
// WMO-only map's global record with no tile. WmvTerrainSessionWorld.cs has that half of the session and its rules; this
// file keeps the tile's, with the hooks the world needs (the obj0 request, the world's requests beside the tile's K, the
// scene's world models disposed with its tiles, the report waiting for the world).
//
// DOODADS (protocol 17, stage 07): a loadWorld with the doodad budgets places the tile's MDDF doodads and the placed world
// models' doodad sets once the world models are settled (WmvTerrainSessionDoodads.cs); the report waits for them too.
//
// LIQUIDS (protocol 19, stage 09): a load whose command carries "liquids" draws its tile's MH2O (built on the worker beside
// the terrain mesh) and its world models' MLIQ, and fetches their frames (role "liquidTexture", WmvTerrainSessionLiquids.cs);
// the tile is adopted without waiting for them, its report waits for them.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Wmv.Wow.World;

/// <summary>The Unity side of the terrain session (WmvMainTerrain in the player, a test view in the self-test).</summary>
public interface IWmvTerrainView
{
    /// <summary>Build the tile's Unity objects, inactive, adding each to `tile` as it is created. `content` is the load's
    /// material and textures (never null: its material is "unavailable" when the load has none). May throw: the session
    /// then disposes `tile`.</summary>
    void BuildTile(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content);
    /// <summary>The committed tile goes on screen: activated, the terrain view entered (when it is not yet), framed.</summary>
    void ShowTile(WmvRuntimeTerrainTile tile);
    /// <summary>No tile is shown any more: the model view comes back as it was left. forModelLoad: a model load took the
    /// tile away (LeaveForModelLoad), and that load frames what it shows; otherwise clearTerrain did.</summary>
    void LeaveTerrainView(string why, bool forModelLoad);
    void DisposeTile(WmvRuntimeTerrainTile tile);
    /// <summary>Streaming (stage 08): a closed tile -- released now, hidden, its objects destroyed over the next frames.</summary>
    void DisposeTileLater(WmvRuntimeTerrainTile tile);
    /// <summary>Fill the counters of a report (live runtimes, Unity objects, memory), sampled now.</summary>
    void Sample(WmvIpcClient.TerrainTileReport report);
    /// <summary>A load is about to be reported "built" (its tile on screen two frames): the dumps, the capture.</summary>
    void TileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand command,
                   int rootFileDataID);
    /// <summary>The view draws the plain-weights diagnostic (texturedplainweights), which also draws the plain-diagnostic
    /// chunks: their textures are wanted too.</summary>
    bool DrawsPlainDiagnostic { get; }
}

public sealed partial class WmvTerrainSession
{
    /// <summary>A load not adopted this long after the first Pump that saw it fails (the default; -wmvTerrainTimeoutMs
    /// sets another within MinTimeoutMs..MaxTimeoutMs).</summary>
    public const double DefaultTimeoutMs = 30000.0, MinTimeoutMs = 1000.0, MaxTimeoutMs = 120000.0;
    /// <summary>How many ended loads' unanswered requests are remembered, to tell a late answer from an unowned one.</summary>
    public const int RetiredCapacity = 128;
    /// <summary>K: the requests a load may have out at once (a texture counts until its upload data are ready).</summary>
    public const int MaxOutstanding = 4;
    /// <summary>The host's MAX_MAP_ASSET_BYTES (4 MiB), for a command that states no limit.</summary>
    public const int DefaultMaxAssetBytes = 4 * 1024 * 1024;
    public const string RoleWdt = "wdt", RoleRootAdt = "rootAdt", RoleTex0Adt = "tex0Adt", RoleTexture = "texture";
    /// <summary>The material reason of a load whose host states no texture limits (a protocol-12 host).</summary>
    public const string NoTexturesOffered = "the host serves no terrain textures (protocol 12)";

    // Wdt, then Files (the tile's) and Adopted (the tile committed); a WMO-only map's world load goes from Wdt to World
    // (no tile: its world model is being fetched and built).
    enum Phase { Wdt, Files, Adopted, World }

    /// <summary>One request the load waits for.</summary>
    sealed class Request
    {
        public string Id, Role;
        public int FileDataID;
        public double AskedMs;
        // A world load's world-model request (counted against MaxWorldOutstanding, not K), and what it is for.
        public bool World;
        public Model Model;
        public int Group = -1;
        public bool AskedBefore;               // a world-model group id this job had asked for already (AskWorld)
        // A world load's doodad request (protocol 17: counted against MaxDoodadOutstanding), and its model.
        public bool Doodad;
        public DoodadModel DModel;
        // A liquid frame (protocol 19: counted against MaxLiquidOutstanding, never against the tile's K).
        public bool Liquid;
    }

    sealed class Job
    {
        public WmvIpcClient.TerrainTileCommand Cmd;
        public Phase Phase;
        public volatile bool Dead;            // no answer or worker result is applied any more
        public bool Reported;
        public readonly List<Request> Pending = new List<Request>(MaxOutstanding);   // at most MaxOutstanding
        public int Preparing;                 // textures answered and being prepared on the worker (count against K)
        public readonly Stopwatch Clock = Stopwatch.StartNew();
        public double StartedAtMs = double.NaN;   // the Pump clock when the timeout began
        public int MaxBytes;
        public double AdoptedMs;
        public long AdoptFrame;
        public int RootFileDataID;
        public long WdtBytes = -1, RootBytes = -1;
        public string WdtMd5 = "", RootMd5 = "";
        public double WdtRoundTripMs = -1, WdtParseMs = -1, RootRoundTripMs = -1, ParseMs = -1, BuildMs = -1, UploadMs = -1;
        public double FirstFrameMs = -1;
        public string HostState = "", HostDetail = "";
        public TerrainTileMesh Mesh;
        public AdtRoot Root;                  // the parsed root (the material build reads it)
        public WmvRuntimeTerrainTile Tile;
        public string AdoptionNote;           // after the commit: what went wrong finishing it
        // Protocol 13: the tex0, the material and the textures.
        public WmvTerrainFormatSupport Support;
        public uint MphdFlags;
        public int Tex0FileDataID, MapTextureFileDataID;
        public long Tex0Bytes = -1;
        public string Tex0Md5 = "";
        public double Tex0RoundTripMs = -1, Tex0ParseMs = -1, MaterialBuildMs = -1;
        public AdtTex Tex;
        public string MaterialUnavailable;    // why the tile has no material, or null
        public bool MaterialBuilding, MaterialDone;
        public TerrainTileMaterial Material;
        public readonly Dictionary<uint, WmvTerrainTexture> Textures = new Dictionary<uint, WmvTerrainTexture>();
        /// <summary>The report's texture entries in order, (id, role): the diffuse ids, the "no texture id" entry, the height
        /// ids (an id with both roles has two entries over one Textures object).</summary>
        public readonly List<KeyValuePair<uint, string>> TextureEntries = new List<KeyValuePair<uint, string>>();
        public readonly Queue<uint> ToAsk = new Queue<uint>();
        public double TexturesFirstAskedMs = -1, TexturesLastDoneMs = -1;
        public WmvTerrainTileContent Content;
        public List<string> RetiredIds = new List<string>();
        // Protocol 16: the load's world models (null for a terrain load).
        public WorldWork World;
        // Stage 08 (streaming, WmvTerrainSessionStream.cs): the tile of the session this job loads (null outside a session),
        // the scene's terrain textures it holds a reference on until its tile binds them, and a tile ready to be added (the
        // session adds it under its frame budget).
        public StreamTile Stream;
        public readonly List<uint> ResidentTextures = new List<uint>();
        public bool AdoptReady;
        // Protocol 19: the load's liquids (null: its command carried no "liquids"; WmvTerrainSessionLiquids.cs).
        public LiquidWork Liquid;

        public string Tile2 { get { return Cmd.GlobalWmo ? "(no tile: a world model)" : Cmd.TileX + "_" + Cmd.TileY; } }
    }

    /// <summary>An unanswered request of a load that has ended: its id, its load and role, and how the load ended (the
    /// status it was reported with, or "dropped at shutdown").</summary>
    public struct RetiredRequest
    {
        public string RequestId, Role, Ended;
        public int Load;
    }

    readonly IWmvTerrainView view;
    readonly Func<string, int, int, int, string> requestMapAsset;   // (role, fileDataID, load, assetEpoch) -> requestId
    readonly Action<WmvIpcClient.TerrainTileReport> report;
    readonly Action<string> log;
    readonly Action<Action> runOffThread;
    readonly double timeoutMs;

    Job current;                                                    // the load in flight (until it is reported)
    readonly RetiredRequest[] retired = new RetiredRequest[RetiredCapacity];   // a ring: the next slot is retiredNext
    int retiredNext, retiredCount;
    string lastRetired = "";                                        // the newest retired request's id, "" for none
    readonly Queue<Action> results = new Queue<Action>();          // worker continuations, applied in Pump
    readonly Dictionary<long, WmvRuntimeTerrainTile> tiles = new Dictionary<long, WmvRuntimeTerrainTile>();
    WmvRuntimeTerrainTile shown;
    int shownLoad;
    // THE SHOWN TILE'S INPUTS (protocol 14, setTerrainView): what its load parsed and settled -- the mesh, the material
    // and the textures (WmvTerrainTileContent; the prepared data of a texture the view did not upload stay with it) --
    // kept while the tile is on screen, so another view rebuilds it here, from what the player holds, without asking the
    // host for anything (RebuildShown). Dropped with the tile.
    TerrainTileMesh shownMesh;
    WmvTerrainTileContent shownContent;
    int sceneEpoch = int.MinValue, sceneMapId = int.MinValue;
    bool viewActive;
    long frame;
    bool disposed;

    /// <param name="requestMapAsset">Send getMapAsset (role, fileDataID, load, assetEpoch); returns the request id.</param>
    /// <param name="report">Send terrainTileLoaded.</param>
    /// <param name="runOffThread">Run an action on a worker thread (the ThreadPool in the player; synchronously in the
    /// self-test). The session queues the action's main-thread continuation for Pump.</param>
    /// <param name="timeoutMs">The load timeout (ClampTimeout keeps it within MinTimeoutMs..MaxTimeoutMs).</param>
    public WmvTerrainSession(IWmvTerrainView view, Func<string, int, int, int, string> requestMapAsset,
                             Action<WmvIpcClient.TerrainTileReport> report, Action<string> log, Action<Action> runOffThread,
                             double timeoutMs = DefaultTimeoutMs)
    {
        if (view == null) throw new ArgumentNullException("view");
        if (requestMapAsset == null) throw new ArgumentNullException("requestMapAsset");
        if (report == null) throw new ArgumentNullException("report");
        if (runOffThread == null) throw new ArgumentNullException("runOffThread");
        this.view = view;
        this.requestMapAsset = requestMapAsset;
        this.report = report;
        this.log = log ?? (s => { });
        this.runOffThread = runOffThread;
        this.timeoutMs = ClampTimeout(timeoutMs);
    }

    /// <summary>A timeout within MinTimeoutMs..MaxTimeoutMs (DefaultTimeoutMs for a value that is not a number).</summary>
    public static double ClampTimeout(double ms)
    {
        if (double.IsNaN(ms)) return DefaultTimeoutMs;
        return Math.Max(MinTimeoutMs, Math.Min(MaxTimeoutMs, ms));
    }

    // ---------------------------------------------------------------- state (runtimeState, the self-test)

    /// <summary>The load of the tile on screen, 0 for none.</summary>
    public int ShownLoad { get { return shown != null ? shownLoad : 0; } }
    public WmvRuntimeTerrainTile ShownTile { get { return shown; } }
    /// <summary>The mesh data of the tile on screen (the world view's ground: TerrainHeight), null when none is kept.</summary>
    public TerrainTileMesh ShownMesh { get { return shown != null ? shownMesh : null; } }
    /// <summary>A terrain load is in flight (not yet reported).</summary>
    public bool Loading { get { return current != null; } }
    /// <summary>The load in flight, 0 for none.</summary>
    public int LoadInFlight { get { return current != null ? current.Cmd.Load : 0; } }
    /// <summary>The session has the terrain view on (a tile was shown and nothing has left it since).</summary>
    public bool ViewActive { get { return viewActive; } }
    /// <summary>Terrain tiles alive in the whole player (WmvRuntimeTerrainTile.Live).</summary>
    public int LiveTiles { get { return WmvRuntimeTerrainTile.Live; } }
    /// <summary>Tiles the scene holds.</summary>
    public int SceneTiles { get { return tiles.Count; } }
    /// <summary>Reports sent, and reports refused because their load had already been reported.</summary>
    public int ReportsSent { get; private set; }
    public int SecondReportsRefused { get; private set; }
    /// <summary>The load timeout in effect (milliseconds).</summary>
    public double TimeoutMs { get { return timeoutMs; } }
    /// <summary>Requests waiting for their answer: the load in flight's, at most MaxOutstanding (0 with no load).</summary>
    public int PendingRequests { get { return current != null ? current.Pending.Count : 0; } }
    /// <summary>Textures of the load in flight answered and still being prepared on the worker (they count against K).</summary>
    public int PreparingTextures { get { return current != null ? current.Preparing : 0; } }
    /// <summary>What counts against K now: PendingRequests + PreparingTextures (never above MaxOutstanding).</summary>
    public int Outstanding { get { return PendingRequests + PreparingTextures; } }
    /// <summary>The most Outstanding ever was, since the session began (the self-test's K check).</summary>
    public int OutstandingPeak { get; private set; }
    /// <summary>Ended loads' unanswered requests remembered (at most RetiredCapacity).</summary>
    public int RetiredRequests { get { return retiredCount; } }
    /// <summary>Requests retired since the session began: every request a load still waited for when it ended. Unlike
    /// RetiredRequests it never stops at RetiredCapacity, so every retirement shows as a step.</summary>
    public int RetiredTotal { get; private set; }
    /// <summary>The id of the request retired last, "" for none.</summary>
    public string LastRetiredRequest { get { return lastRetired; } }
    /// <summary>Map answers dropped: to a request of a load that had ended (the retired ring), and to any other request
    /// (one the ring no longer holds or never held, or one of the load in flight's under another load).</summary>
    public int LateAnswersDropped { get; private set; }
    public int UnownedAnswersDropped { get; private set; }
    public int AnswersDropped { get { return LateAnswersDropped + UnownedAnswersDropped; } }
    /// <summary>The ids the load in flight waits for (a copy; the self-test's).</summary>
    public List<string> PendingIds()
    {
        var ids = new List<string>();
        if (current != null)
            foreach (Request q in current.Pending)
                ids.Add(q.Id);
        return ids;
    }

    // ---------------------------------------------------------------- commands

    public void Load(WmvIpcClient.TerrainTileCommand cmd)
    {
        if (cmd == null || disposed)
            return;
        Log("load {0}: loadTerrainTile {1}", cmd.Load, cmd);
        StreamEnding();
        if (current != null)
            Supersede(current, "superseded by terrain load " + cmd.Load);
        EndStream("superseded by terrain load " + cmd.Load);
        var j = new Job { Cmd = cmd, MaxBytes = cmd.MaxAssetBytes > 0 ? cmd.MaxAssetBytes : DefaultMaxAssetBytes };
        // The GPU's block formats: the tile's textures, (stage 08) the world models' and doodads' block uploads, and (stage 09)
        // the liquid frames.
        if (cmd.TexturesOffered || cmd.World || cmd.Liquids != null)
            j.Support = WmvTerrainFormatSupport.Query();
        if (cmd.World)
            j.World = new WorldWork
            {
                Global = cmd.GlobalWmo, Frame = cmd.GlobalWmo ? PlacementFrame.Global : PlacementFrame.Tile,
                Doodads = new DoodadsWork { Offered = cmd.DoodadsOffered },
            };
        current = j;
        string bad = CommandProblem(cmd);
        if (bad == null && cmd.World && worldView == null)
            bad = "this player has no world-model view (WorldView is not set)";
        if (bad != null)
        {
            Fail(j, bad);
            return;
        }
        // Protocol 18: a loadWorld with "streaming" is a session's entry (WmvTerrainSessionStream.cs).
        if (StreamOffered(cmd))
            BeginStream(j);
        // Protocol 19: its liquids' registry -- the load's own, or (a session's entry) the session's.
        BeginLiquids(j);
        j.Phase = Phase.Wdt;
        Ask(j, RoleWdt, cmd.WdtFileDataID);
    }

    /// <summary>clearTerrain: a load in flight is answered "cleared" (once); a tile on screen is taken away and the view
    /// left, without a report; with neither, nothing happens.</summary>
    public void Clear(string reason)
    {
        if (disposed)
            return;
        string why = string.IsNullOrEmpty(reason) ? "clearTerrain" : "clearTerrain: " + reason;
        Log("{0} -- {1}", why, current != null ? "load " + current.Cmd.Load + " in flight" : "no load in flight");
        Job j = current;
        StreamEnding();
        if (j != null)
        {
            current = null;
            Report(j, "cleared", why);
        }
        EndStream(why);
        TakeTheTileAway(why, false);
    }

    /// <summary>A model load (any kind) arrived: the latest command wins. A load in flight is answered "superseded", the
    /// tile and the terrain view go, before the model load proceeds.</summary>
    public void LeaveForModelLoad(string reason)
    {
        if (disposed)
            return;
        Job j = current;
        if (j == null && tiles.Count == 0 && !viewActive && stream == null)
            return;
        Log("a model load: {0}", reason);
        StreamEnding();
        if (j != null)
            Supersede(j, reason);
        EndStream(reason);
        TakeTheTileAway(reason, true);
    }

    void TakeTheTileAway(string why, bool forModelLoad)
    {
        int n = DisposeTiles(null);
        if (n > 0)
            Log("{0} tile(s) disposed ({1}); live terrain tiles {2}", n, why, WmvRuntimeTerrainTile.Live);
        shown = null;
        shownLoad = 0;
        ForgetShownInputs();
        if (viewActive)
        {
            viewActive = false;
            view.LeaveTerrainView(why, forModelLoad);
        }
    }

    /// <summary>The shown tile's inputs go (with the tile): their prepared texture data are dropped now.</summary>
    void ForgetShownInputs()
    {
        if (shownContent != null)
            foreach (WmvTerrainTexture t in shownContent.Textures.Values)
                t.ReleaseUploadData();
        shownMesh = null;
        shownContent = null;
    }

    /// <summary>A tile built in a textured view (textured, texturedlit) uploaded every texture any view setTerrainView can
    /// choose binds: the prepared data of the rest (a texture only a chunk drawn neutral uses) are dropped. In any other
    /// view nothing was uploaded, and every texture's data stay for a textured rebuild.</summary>
    static void KeepOnlyWhatAViewMayUpload(WmvRuntimeTerrainTile tile, WmvTerrainTileContent content)
    {
        // Kept only when the host chose the view (setTerrainView: it may choose another): a view set on the command line,
        // or the material's own choice, is the tile's for good, and its data go at once, as before protocol 14.
        if (tile == null || content == null ||
            (WmvTerrainDebug.RuntimeViewName != "auto" && tile.View != WmvTerrainView.Textured && tile.View != WmvTerrainView.TexturedLit))
            return;
        foreach (WmvTerrainTexture t in content.Textures.Values)
            t.ReleaseUploadData();
    }

    /// <summary>The prepared texture bytes the shown tile's inputs still hold (textures its view did not upload).</summary>
    public long RetainedTextureBytes
    {
        get
        {
            long n = 0;
            if (shownContent != null)
                foreach (WmvTerrainTexture t in shownContent.Textures.Values)
                    n += t.PreparedBytes;
            return n;
        }
    }

    /// <summary>
    /// setTerrainView (protocol 14): the tile on screen built again in the view now in effect (WmvTerrainDebug.EffectiveView),
    /// from its own inputs -- the mesh, the material and the textures its load settled -- and nothing asked of the host.
    /// The new tile is built inactive beside the old one, which it shares the uploaded textures with (they are handed over
    /// at the commit, never uploaded twice); a texture the old view did not upload is uploaded from the data kept for it.
    /// Then the commit: the old tile disposed, the new one shown, the camera where the user left it (no framing). A build
    /// that throws disposes the new tile only: the old one stays on screen, as it was, its textures with it. A load in
    /// flight is not touched: it is built in the new view when it is adopted. Returns "applied" (rebuilt), "noTile"
    /// (nothing on screen: the view is for the next load) or "failed" (with the reason).
    /// </summary>
    public string RebuildShown(string why, out string reason, out double ms, out int load)
    {
        reason = "";
        ms = 0;
        load = 0;
        var clock = Stopwatch.StartNew();
        WmvRuntimeTerrainTile old = shown;
        if (disposed || old == null || old.Disposed || shownMesh == null || shownContent == null)
        {
            reason = "no terrain tile is on screen: the view applies to the next one";
            // A streaming session's resident tiles are drawn again in it all the same, one a frame.
            QueueStreamRebuild(null);
            return "noTile";
        }
        load = old.Load;
        var tile = new WmvRuntimeTerrainTile(old.MapId, old.TileX, old.TileY, old.Load);
        foreach (KeyValuePair<uint, UnityEngine.Texture2D> kv in old.Textures)
            tile.Textures[kv.Key] = kv.Value;        // shared, not tracked: the new tile's build binds them
        try
        {
            view.BuildTile(tile, shownMesh, shownContent);
        }
        catch (Exception e)
        {
            reason = "building the tile in the view failed: " + e.GetType().Name + ": " + e.Message;
            tile.Textures.Clear();                   // the old tile's: never destroyed with the new one
            try { view.DisposeTile(tile); }
            catch (Exception e2) { Log("rebuild of load {0}: disposing the unfinished tile failed: {1}", load, e2.Message); }
            ms = clock.Elapsed.TotalMilliseconds;
            Log("rebuild of load {0} ({1}): {2}; the tile on screen stays as it was", load, why, reason);
            return "failed";
        }
        // THE COMMIT. The textures the new tile still binds change hands (off the old tile's lists, onto the new one's,
        // never destroyed); the old tile goes with what it alone holds.
        int handed = 0;
        foreach (KeyValuePair<uint, UnityEngine.Texture2D> kv in new List<KeyValuePair<uint, UnityEngine.Texture2D>>(tile.Textures))
        {
            long gpu;
            if (old.Hand(kv.Value, out gpu))
            {
                tile.Track(kv.Value, gpu);
                handed++;
            }
        }
        long key = KeyOf(old.MapId, old.TileX, old.TileY);
        view.DisposeTile(old);
        tiles.Remove(key);
        tiles[key] = tile;
        shown = tile;
        StreamTile rebuilt;
        if (stream != null && stream.Tiles.TryGetValue(key, out rebuilt) && rebuilt.Tile == old)
            rebuilt.Tile = tile;
        tile.Root.SetActive(true);
        KeepOnlyWhatAViewMayUpload(tile, shownContent);
        // A streaming session: the other resident tiles follow, one a frame.
        QueueStreamRebuild(tile);
        ms = clock.Elapsed.TotalMilliseconds;
        Log("rebuild of load {0} ({1}): tile {2}_{3} drawn {4} in {5:0.###} ms, {6} texture(s) handed over, {7} uploaded; live terrain tiles {8}",
            load, why, tile.TileX, tile.TileY, WmvTerrainDebug.ViewNames[(int)tile.View], ms, handed, tile.Textures.Count - handed,
            WmvRuntimeTerrainTile.Live);
        return "applied";
    }

    /// <summary>Shutdown: no report goes out (the connection is ending); the load in flight stops taking results, its
    /// requests are retired, and the tiles are released.</summary>
    public void Dispose()
    {
        if (disposed)
            return;
        disposed = true;
        StreamEnding();
        if (current != null)
        {
            current.Dead = true;
            Log("load {0}: dropped at shutdown, not reported", current.Cmd.Load);
            Retire(current, "dropped at shutdown");
            current = null;
        }
        EndStream("shutdown");
        int n = DisposeTiles(null);
        Log("session disposed: {0} tile(s) released", n);
        shown = null;
        shownLoad = 0;
        ForgetShownInputs();
    }

    // ---------------------------------------------------------------- answers and frames

    /// <summary>
    /// A map-file answer: WmvIpcClient routes every getMapAsset answer here (OnMapAssetResponse), and nowhere else, so
    /// the session takes each one. It is applied only when it answers one of the requests the load in flight waits for
    /// -- that id, and that load when the answer states one (load > 0) -- and the load is alive. Anything else is
    /// dropped and said: "late" when the request belonged to a load that has ended (the retired ring), "unowned"
    /// otherwise. Returns whether the answer was applied.
    /// </summary>
    public bool ClaimMapAssetResponse(WmvIpcClient.AssetResponse r)
    {
        if (r == null)
            return false;
        Job j = current;
        string id = r.requestId ?? "";
        // Streaming: a session's requests are routed to their own tile's job by id (WmvTerrainSessionStream.cs).
        Job sj = StreamJobOf(id);
        if (sj != null)
            j = sj;
        Request q = !disposed && j != null && !j.Dead ? Find(j, id) : null;
        if (q == null || (r.load > 0 && r.load != j.Cmd.Load))
        {
            string what = string.Format("{0}, {1}, load {2}", string.IsNullOrEmpty(r.role) ? "map file" : r.role,
                                        r.ok ? "available" : (string.IsNullOrEmpty(r.state) ? "failed" : r.state), r.load);
            RetiredRequest ended;
            if (TryGetRetired(id, out ended))
            {
                LateAnswersDropped++;
                Log("load {0}: the answer to request {1} ({2}) is late -- the load ended \"{3}\" -- dropped", ended.Load, id, what,
                    ended.Ended);
            }
            else
            {
                UnownedAnswersDropped++;
                if (q != null)
                    Log("load {0}: the answer to its request {1} ({2}) names another load -- dropped; the request still waits", j.Cmd.Load,
                        id, what);
                else
                    Log("an answer to request {0} ({1}) that no load waits for, nor any of the last {2} ended -- dropped", id, what,
                        RetiredCapacity);
            }
            return false;
        }
        j.Pending.Remove(q);
        StreamRequestDone(id, r);
        try
        {
            switch (q.Role)
            {
                case RoleWdt: OnWdt(j, r, q); break;
                case RoleRootAdt: OnRoot(j, r, q); break;
                case RoleTex0Adt: OnTex0(j, r, q); break;
                case RoleObj0Adt: OnObj0(j, r, q); break;
                case RoleWmoRoot: OnWmoRoot(j, r, q); break;
                case RoleWmoGroup: OnWmoGroup(j, r, q); break;
                case RoleWmoTexture: OnWmoTexture(j, r, q); break;
                case RoleM2Model: OnM2Model(j, r, q); break;
                case RoleM2Skin: OnM2Skin(j, r, q); break;
                case RoleM2Texture: OnM2Texture(j, r, q); break;
                case RoleLiquidTexture: OnLiquidTexture(j, r, q); break;
                case RoleTexture: OnTexture(j, r, q); break;
                // Every request is the session's own, so a role it never asks for is an internal error, never a terrain
                // texture by default.
                default: throw new InvalidOperationException("an answer to request " + q.Id + " of role \"" + q.Role + "\", which this session never asks for");
            }
            Advance(j);
            AdvanceWorld(j);
            AdvanceDoodads(j);
        }
        catch (Exception e)
        {
            Fail(j, "internal error handling the " + RoleName(q.Role) + " answer: " + e.GetType().Name + ": " + e.Message);
        }
        return true;
    }

    static Request Find(Job j, string id)
    {
        foreach (Request q in j.Pending)
            if (q.Id == id)
                return q;
        return null;
    }

    /// <summary>One frame (call once per frame, from Update): apply the worker's results, start or check the load's
    /// timeout, and report a load whose tile has been on screen since two frames ago.</summary>
    public void Pump(double nowMs)
    {
        frame++;
        while (true)
        {
            Action a;
            lock (results)
            {
                if (results.Count == 0) break;
                a = results.Dequeue();
            }
            try { a(); }
            catch (Exception e)
            {
                string what = e.GetType().Name + ": " + e.Message;
                Log("applying a worker result failed: {0}", what);
                if (current != null && current.Phase != Phase.Adopted)
                    Fail(current, "internal error applying the terrain worker's result: " + what);
            }
        }
        // A streaming session pumps its tiles' jobs, the entry's among them, nearest first (WmvTerrainSessionStream.cs).
        if (stream != null && !disposed)
        {
            PumpStream(nowMs);
            return;
        }
        Job j = current;
        if (j == null || disposed)
            return;
        if (double.IsNaN(j.StartedAtMs))
            j.StartedAtMs = nowMs;
        if (j.Phase == Phase.Wdt || j.Phase == Phase.Files)
        {
            if (nowMs - j.StartedAtMs > timeoutMs)
                Fail(j, "timed out after " + (timeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                        " s waiting for " + WaitingFor(j));
            else
                AskLiquidFrames(j);            // (protocol 19: the tile does not wait for them)
            return;
        }
        // A world load's world models: one prototype built a frame, the world's own timeout, its progress (the report
        // waits for every placement to settle).
        if (j.World != null && !PumpWorld(j, nowMs))
            return;
        if (j != current)
            return;
        // Protocol 19: the load's liquid frames (asked, their timeout); the report waits for them.
        bool liquids = PumpLiquids(j, nowMs);
        if (j != current)
            return;
        if (j.Phase == Phase.World)
        {
            // A WMO-only map's world model (and its doodads): reported once its scene has been on screen two frames after the
            // last of them.
            if (liquids && WorldSettled(j.World) && frame >= WorldLastFrame(j.World) + 2)
                FinishWorld(j);
            return;
        }
        if (liquids && frame >= j.AdoptFrame + 2 && (j.World == null || (WorldSettled(j.World) && frame >= WorldLastFrame(j.World) + 2)))
            FinishBuilt(j);
    }

    static string RoleName(string role)
    {
        switch (role)
        {
            case RoleWdt: return "WDT";
            case RoleRootAdt: return "root ADT";
            case RoleTex0Adt: return "tex0 ADT";
            case RoleObj0Adt: return "obj0 ADT";
            case RoleWmoRoot: return "world-model root";
            case RoleWmoGroup: return "world-model group";
            case RoleWmoTexture: return "world-model texture";
            case RoleM2Model: return "doodad model";
            case RoleM2Skin: return "doodad skin";
            case RoleM2Texture: return "doodad texture";
            case RoleLiquidTexture: return "liquid texture";
            default: return "texture";
        }
    }

    static string WaitingFor(Job j)
    {
        if (j.Phase == Phase.World && j.World != null)
            return WorldWaitingFor(j);
        if (j.Pending.Count > 0)
        {
            var parts = new List<string>();
            foreach (Request q in j.Pending)
                parts.Add(q.Role == RoleWdt ? "the WDT answer (request " + q.Id + ")"
                                            : "the " + RoleName(q.Role) + " answer (request " + q.Id + ", FileDataID " + q.FileDataID + ")");
            return string.Join(" and ", parts.ToArray());
        }
        if (j.Preparing > 0)
            return j.Preparing + " texture(s) being prepared on the worker thread";
        if (j.Mesh == null)
            return "the root ADT's parse and build on the worker thread";
        if (j.MaterialBuilding)
            return "the material build on the worker thread";
        return "the tex0 ADT's parse on the worker thread";
    }

    // ---------------------------------------------------------------- the steps

    static string CommandProblem(WmvIpcClient.TerrainTileCommand cmd)
    {
        if (cmd.Load <= 0)
            return "loadTerrainTile carries no load serial (" + cmd.Load + ")";
        if (cmd.World && !cmd.WorldModelsOffered)
            return "loadWorld states no world-model budget (maxWmoModelsPerLoad)";
        if (!cmd.GlobalWmo &&
            (cmd.TileX < 0 || cmd.TileX >= TerrainCoordinates.TilesPerSide || cmd.TileY < 0 || cmd.TileY >= TerrainCoordinates.TilesPerSide))
            return string.Format("tile {0}_{1} is outside 0..63", cmd.TileX, cmd.TileY);
        if (cmd.WdtFileDataID <= 0)
            return "loadTerrainTile names no WDT FileDataID (" + cmd.WdtFileDataID + ")";
        return null;
    }

    /// <summary>The tile's requests out (its K): pending tile requests and textures being prepared -- a world load's
    /// world-model requests are counted apart (MaxWorldOutstanding), and its liquid frames (MaxLiquidOutstanding: the tile
    /// never waits for them).</summary>
    static int TileOut(Job j)
    {
        int n = j.Preparing;
        foreach (Request q in j.Pending)
            if (!q.World && !q.Liquid)
                n++;
        return n;
    }

    void Ask(Job j, string role, int fileDataID)
    {
        if (TileOut(j) >= MaxOutstanding)
            throw new InvalidOperationException("load " + j.Cmd.Load + " already has " + MaxOutstanding + " requests out");
        double at = j.Clock.Elapsed.TotalMilliseconds;
        string id = SendMapRequest(j, role, fileDataID);
        j.Pending.Add(new Request { Id = id, Role = role, FileDataID = fileDataID, AskedMs = at });
        OutstandingPeak = Math.Max(Outstanding, OutstandingPeak);
        Log("load {0}: getMapAsset {1} {2} (FileDataID {3}, epoch {4}); {5} out", j.Cmd.Load, id, role, fileDataID, j.Cmd.AssetEpoch,
            j.Pending.Count + j.Preparing);
    }

    void LogAnswer(Job j, string what, WmvIpcClient.AssetResponse r, double roundTripMs)
    {
        Log("load {0}: {1} answer {2}: {3}, state \"{4}\"{5}, {6} bytes, md5 {7}, host read {8:0.###} ms, round trip {9:0.#} ms",
            j.Cmd.Load, what, r.requestId, r.ok ? "ok" : "failed", r.state ?? "",
            string.IsNullOrEmpty(r.detail) && string.IsNullOrEmpty(r.error) ? "" : " (" + (string.IsNullOrEmpty(r.detail) ? r.error : r.detail) + ")",
            r.data != null ? r.data.Length : 0, string.IsNullOrEmpty(r.md5) ? "-" : r.md5, r.readMs, roundTripMs);
    }

    /// <summary>Why an answer cannot be used, or null. A host refusal fills the load's hostState and hostDetail when
    /// `failsLoad` (the WDT and the root: their refusal fails the load).</summary>
    static string AnswerProblem(Job j, WmvIpcClient.AssetResponse r, string what, int fileDataID, int maxBytes, string limitName, bool failsLoad)
    {
        if (!r.ok)
        {
            // An answer the host called "available" whose bytes did not arrive is the transport's failure, not the host's.
            if (!string.IsNullOrEmpty(r.state) && r.state != "available")
            {
                string detail = !string.IsNullOrEmpty(r.detail) ? r.detail : (r.error ?? "");
                if (failsLoad)
                {
                    j.HostState = r.state;
                    j.HostDetail = detail;
                }
                return string.Format("the host answered the {0} request (FileDataID {1}) \"{2}\": {3}", what, fileDataID, r.state,
                                     string.IsNullOrEmpty(detail) ? "no detail" : detail);
            }
            return string.Format("the {0} request (FileDataID {1}) failed: {2}", what, fileDataID,
                                 string.IsNullOrEmpty(r.error) ? "no reason given" : r.error);
        }
        if (r.data == null)
            return string.Format("the {0} answer (FileDataID {1}) carries no bytes", what, fileDataID);
        if (r.fileDataID > 0 && r.fileDataID != fileDataID)
            return string.Format("the {0} answer names FileDataID {1}, the request {2}", what, r.fileDataID, fileDataID);
        if (r.data.Length > maxBytes)
            return string.Format("answer of {0} bytes exceeds {1} {2} ({3}, FileDataID {4})", r.data.Length, limitName, maxBytes,
                                 what, fileDataID);
        return null;
    }

    void OnWdt(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        j.WdtRoundTripMs = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        LogAnswer(j, "WDT", r, j.WdtRoundTripMs);
        string why = AnswerProblem(j, r, "WDT", cmd.WdtFileDataID, j.MaxBytes, "maxAssetBytes", true);
        if (why != null)
        {
            Fail(j, why);
            return;
        }
        byte[] bytes = r.data;
        j.WdtBytes = bytes.Length;
        j.WdtMd5 = Md5Hex(bytes);
        if (!string.IsNullOrEmpty(r.md5) && !string.Equals(r.md5, j.WdtMd5, StringComparison.OrdinalIgnoreCase))
        {
            Fail(j, string.Format("the WDT bytes received hash to {0}, the host served {1}", j.WdtMd5, r.md5));
            return;
        }
        var sw = Stopwatch.StartNew();
        WdtFile wdt;
        try { wdt = WdtParser.Parse(bytes, string.Format("wdt {0} (map {1})", cmd.WdtFileDataID, cmd.MapId)); }
        catch (Exception e)
        {
            j.WdtParseMs = sw.Elapsed.TotalMilliseconds;
            Fail(j, "WDT parse failed: " + e.Message);
            return;
        }
        j.WdtParseMs = sw.Elapsed.TotalMilliseconds;
        if (cmd.GlobalWmo)
        {
            // A WMO-only map: no tile, its WDT's global record placed (WmvTerrainSessionWorld.cs).
            StartGlobalWorld(j, wdt);
            return;
        }
        // Streaming: the session's WDT, kept for its other tiles (read once).
        if (j.Stream != null && stream != null)
        {
            stream.Wdt = wdt;
            stream.WdtBytes = j.WdtBytes;
            stream.WdtMd5 = j.WdtMd5;
        }
        StartTileFiles(j, wdt);
    }

    /// <summary>The tile's files after its WDT (OnWdt; a streaming session's other tiles with the session's WDT): its root,
    /// beside it its tex0 when the host offers textures, and a world load's obj0.</summary>
    void StartTileFiles(Job j, WdtFile wdt)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        if (!wdt.TileExists(cmd.TileX, cmd.TileY))
        {
            Fail(j, "no tile at " + j.Tile2 + " (MAIN flag clear)");
            return;
        }
        WdtTileFiles files;
        if (!wdt.TryGetTileFiles(cmd.TileX, cmd.TileY, out files) || files[WdtTileFileRole.RootAdt] == 0)
        {
            Fail(j, "MAID names no root ADT for " + j.Tile2 + (wdt.HasMaid ? "" : " (the WDT has no MAID)"));
            return;
        }
        uint rootId = files[WdtTileFileRole.RootAdt];
        if (rootId > int.MaxValue)
        {
            Fail(j, "MAID names root ADT FileDataID " + rootId + " for " + j.Tile2 + ", beyond what a request can carry");
            return;
        }
        j.RootFileDataID = (int)rootId;
        j.MphdFlags = wdt.Header.Words != null && wdt.Header.Words.Length > 0 ? wdt.Header.Flags : 0u;
        uint tex0 = files[WdtTileFileRole.Tex0Adt];
        uint map = files[WdtTileFileRole.MapTexture];
        j.MapTextureFileDataID = map <= int.MaxValue ? (int)map : 0;
        Log("load {0}: WDT {1} parsed in {2:0.###} ms: {3}, {4} tile(s), {5} issue(s), MPHD 0x{6:X}; MAID names root ADT {7} and tex0 ADT {8} for {9}",
            cmd.Load, cmd.WdtFileDataID, j.WdtParseMs, wdt.Kind, wdt.TileCount, wdt.Issues.Count, j.MphdFlags, j.RootFileDataID, tex0, j.Tile2);
        j.Phase = Phase.Files;
        Ask(j, RoleRootAdt, j.RootFileDataID);
        if (!cmd.TexturesOffered)
            j.MaterialUnavailable = NoTexturesOffered;
        else if (tex0 == 0)
            j.MaterialUnavailable = "MAID names no tex0 ADT for " + j.Tile2;
        else if (tex0 > int.MaxValue)
            j.MaterialUnavailable = "MAID names tex0 ADT FileDataID " + tex0 + " for " + j.Tile2 + ", beyond what a request can carry";
        else
        {
            j.Tex0FileDataID = (int)tex0;
            Ask(j, RoleTex0Adt, j.Tex0FileDataID);
        }
        if (j.MaterialUnavailable != null)
            Log("load {0}: no tex0 asked for: {1}", cmd.Load, j.MaterialUnavailable);
        // A world load: the tile's obj0 ADT (MAID word 1: its MODF records), beside the root and the tex0.
        if (j.World != null)
            AskObj0(j, files[WdtTileFileRole.Obj0Adt]);
    }

    void OnRoot(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        j.RootRoundTripMs = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        LogAnswer(j, "root ADT", r, j.RootRoundTripMs);
        string why = AnswerProblem(j, r, "root ADT", j.RootFileDataID, j.MaxBytes, "maxAssetBytes", true);
        if (why != null)
        {
            Fail(j, why);
            return;
        }
        // Everything the worker reads is immutable from here on: the bytes (the answer's, never written), the tile and
        // the origin are values.
        byte[] bytes = r.data;
        j.RootBytes = bytes.Length;
        string hostMd5 = r.md5 ?? "";
        int x = j.Cmd.TileX, y = j.Cmd.TileY;
        // Streaming: every tile of the session against the scene's origin (the entry tile's north-west corner).
        WorldOrigin origin = j.Stream != null && stream != null ? stream.Origin : WorldToUnity.TileOrigin(x, y);
        string what = string.Format("root ADT {0} ({1}_{2})", j.RootFileDataID, x, y);
        // Protocol 19: the tile's liquid against the same origin, with the offer's catalogue (immutable from here on).
        ILiquidCatalog catalog = j.Liquid != null ? j.Liquid.Registry.Catalog : null;
        bool liquids = j.Liquid != null;
        // Stage 10: the chunks' MCNR bytes kept with the mesh only while the outdoor light's normals may be wanted (not
        // WMV_LIGHT=off, not -wmvTerrainLight=off): a tile built while the light is off is drawn with them once it is on.
        bool keepMcnr = WmvWorldLight.TerrainNormalsWanted;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            AdtRoot root = null;
            TerrainTileMesh mesh = null;
            string error = null;
            try { root = AdtParser.ParseRoot(bytes, x, y, what); }
            catch (Exception e) { error = "root ADT parse failed: " + e.Message; }
            double parseMs = sw.Elapsed.TotalMilliseconds, buildMs = -1;
            if (error == null)
            {
                sw.Reset();
                sw.Start();
                // The builder's own failures say what they are ("root ADT does not belong to tile x_y ...", "no decodable
                // chunk ..."); anything else is named by its type.
                try { mesh = TerrainMeshBuilder.Build(root, x, y, origin, keepMcnr); }
                catch (TerrainBuildException e) { error = e.Message; }
                catch (Exception e) { error = "terrain mesh build failed: " + e.GetType().Name + ": " + e.Message; }
                buildMs = sw.Elapsed.TotalMilliseconds;
            }
            TerrainLiquidTile liquid = null;
            string liquidError = null;
            double liquidMs = -1;
            if (error == null && liquids)
            {
                sw.Reset();
                sw.Start();
                // A liquid that cannot be built never fails the terrain.
                try { liquid = TerrainLiquidMesh.Build(root.Liquids, x, y, origin, catalog); }
                catch (Exception e) { liquidError = e.GetType().Name + ": " + e.Message; }
                liquidMs = sw.Elapsed.TotalMilliseconds;
            }
            int issues = root != null ? root.Issues.Count : 0;
            bool sound = error == null && (string.IsNullOrEmpty(hostMd5) || string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase));
            return () =>
            {
                // The liquid first: OnBuilt's Advance may adopt the tile at once, and the tile draws its liquid from it.
                if (sound && liquids)
                    LiquidsParsed(j, liquid, liquidMs, liquidError);
                OnBuilt(j, md5, hostMd5, root, mesh, parseMs, buildMs, error, issues);
            };
        });
    }

    void RunWorker(Job j, Func<Action> work)
    {
        runOffThread(() =>
        {
            if (j.Dead)
                return;
            Action apply;
            try { apply = work(); }
            catch (Exception e)
            {
                string message = e.GetType().Name + ": " + e.Message;
                apply = () => Fail(j, "terrain worker failed: " + message);
            }
            lock (results)
                results.Enqueue(() =>
                {
                    if (j.Dead || !Live(j))
                    {
                        Log("load {0}: its worker result came after the load ended -- dropped", j.Cmd.Load);
                        return;
                    }
                    if (j.Stream != null && j != current)
                    {
                        // A tile of a streaming session: its own failure, never the load in flight's.
                        try { apply(); }
                        catch (Exception e)
                        {
                            FailStreamJob(j, "internal error applying the terrain worker's result: " + e.GetType().Name + ": " + e.Message);
                        }
                        return;
                    }
                    apply();
                });
        });
    }

    void OnBuilt(Job j, string md5, string hostMd5, AdtRoot root, TerrainTileMesh mesh, double parseMs, double buildMs, string error, int issues)
    {
        j.RootMd5 = md5;
        j.ParseMs = parseMs;
        j.BuildMs = buildMs;
        j.Mesh = mesh;
        if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
        {
            Fail(j, string.Format("the root ADT bytes received hash to {0}, the host served {1}", md5, hostMd5));
            return;
        }
        if (error != null)
        {
            Fail(j, error);
            return;
        }
        j.Root = root;
        Log("load {0}: root ADT {1} ({2} bytes, md5 {3}) parsed in {4:0.###} ms ({5} issue(s)), mesh built in {6:0.###} ms on the worker: " +
            "{7} chunk(s) built, {8} skipped, {9} unsupported, {10} vertices, {11} triangles, {12} hole cell(s), {13} mesh bytes{14}",
            j.Cmd.Load, j.RootFileDataID, j.RootBytes, md5, parseMs, issues, buildMs, mesh.ChunksBuilt, mesh.ChunksSkipped,
            mesh.ChunksUnsupported, mesh.Vertices, mesh.Triangles, mesh.HoleCells, mesh.MeshBytes,
            mesh.McnrBytes > 0 ? "; " + mesh.McnrBytes + " MCNR bytes kept for the outdoor light's normals (managed, apart from the mesh bytes)" : "");
        foreach (TerrainChunkMesh c in mesh.Chunks)
            if (c.State != TerrainChunkState.Built)
                Log("load {0}: chunk {1} {2}: {3}", j.Cmd.Load, c.Index, c.State, c.Reason);
        Advance(j);
    }

    // ---------------------------------------------------------------- tex0, material, textures (protocol 13)

    void OnTex0(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        j.Tex0RoundTripMs = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        LogAnswer(j, "tex0 ADT", r, j.Tex0RoundTripMs);
        string why = AnswerProblem(j, r, "tex0 ADT", j.Tex0FileDataID, j.MaxBytes, "maxAssetBytes", false);
        if (why != null)
        {
            j.MaterialUnavailable = why;
            return;
        }
        byte[] bytes = r.data;
        j.Tex0Bytes = bytes.Length;
        string hostMd5 = r.md5 ?? "";
        string what = string.Format("tex0 ADT {0} ({1})", j.Tex0FileDataID, j.Tile2);
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            AdtTex tex = null;
            string error = null;
            try { tex = AdtParser.ParseTex(bytes, what); }
            catch (Exception e) { error = "tex0 ADT parse failed: " + e.Message; }
            double parseMs = sw.Elapsed.TotalMilliseconds;
            return () => OnTex0Parsed(j, md5, hostMd5, tex, parseMs, error);
        });
    }

    void OnTex0Parsed(Job j, string md5, string hostMd5, AdtTex tex, double parseMs, string error)
    {
        j.Tex0Md5 = md5;
        j.Tex0ParseMs = parseMs;
        if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
            j.MaterialUnavailable = string.Format("the tex0 ADT bytes received hash to {0}, the host served {1}", md5, hostMd5);
        else if (error != null)
            j.MaterialUnavailable = error;
        else
        {
            j.Tex = tex;
            Log("load {0}: tex0 ADT {1} ({2} bytes, md5 {3}) parsed in {4:0.###} ms on the worker: {5} MDID, {6} MHID, {7} issue(s)",
                j.Cmd.Load, j.Tex0FileDataID, j.Tex0Bytes, md5, parseMs, tex.DiffuseTextureIds != null ? tex.DiffuseTextureIds.Length : 0,
                tex.HeightTextureIds != null ? tex.HeightTextureIds.Length : 0, tex.Issues.Count);
        }
        Advance(j);
    }

    /// <summary>The load's next step, whenever something arrived: settle or build the material once the root and the
    /// tex0 are in, ask for textures while fewer than K are out, and adopt once the mesh, the material and every texture
    /// are settled.</summary>
    void Advance(Job j)
    {
        if (!Live(j) || j.Dead || j.Phase != Phase.Files)
            return;
        if (!j.MaterialDone && !j.MaterialBuilding)
        {
            if (j.MaterialUnavailable != null)
                SettleUnavailable(j);
            else if (j.Tex != null && j.Root != null)
                StartMaterialBuild(j);
        }
        if (j.MaterialDone)
            AskTextures(j);
        if (j.Mesh != null && j.Root != null && j.MaterialDone && j.ToAsk.Count == 0 && TileOut(j) == 0)
        {
            // A tile of a streaming session is added under the session's frame budget (PumpStreamJob).
            if (j.Stream != null && j != current)
                j.AdoptReady = true;
            else
                Adopt(j);
        }
    }

    void SettleUnavailable(Job j)
    {
        j.Material = TerrainTileMaterial.Unavailable(j.MaterialUnavailable, j.MphdFlags);
        j.MaterialDone = true;
        Log("load {0}: material unavailable: {1}", j.Cmd.Load, j.MaterialUnavailable);
    }

    void StartMaterialBuild(Job j)
    {
        j.MaterialBuilding = true;
        AdtRoot root = j.Root;
        AdtTex tex = j.Tex;
        uint mphd = j.MphdFlags;
        RunWorker(j, () =>
        {
            var sw = Stopwatch.StartNew();
            TerrainTileMaterial m = null;
            string error = null;
            try { m = TerrainMaterialBuilder.Build(root, tex, mphd); }
            catch (Exception e) { error = "material build failed: " + e.GetType().Name + ": " + e.Message; }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () => OnMaterialBuilt(j, m, ms, error);
        });
    }

    void OnMaterialBuilt(Job j, TerrainTileMaterial m, double ms, string error)
    {
        j.MaterialBuilding = false;
        j.MaterialBuildMs = ms;
        if (error != null)
        {
            j.MaterialUnavailable = error;
            SettleUnavailable(j);
            Advance(j);
            return;
        }
        j.Material = m;
        j.MaterialDone = true;
        // The report's entries: the diffuse ids (those the plain-weights view adds after them), the "no texture id" entry,
        // then the height ids (empty unless MPHD 0x80). One request per distinct id, diffuse first.
        var diffuse = new List<uint>(m.TextureIds);
        bool plain = view.DrawsPlainDiagnostic;
        if (plain)
            diffuse.AddRange(m.PlainOnlyTextureIds);
        foreach (uint id in diffuse)
            j.TextureEntries.Add(new KeyValuePair<uint, string>(id, WmvTerrainTileContent.RoleDiffuse));
        if (m.ZeroIdReferences > 0)
        {
            // MDID 0: the player's own state, one entry for every layer that names it.
            var none = new WmvTerrainTexture { FileDataID = 0 };
            none.Fail(WmvTerrainTexture.StateNoTextureId, m.ZeroIdReferences + " layer(s) of the drawable chunks name an MDID entry that holds 0");
            j.Textures[0] = none;
            j.TextureEntries.Add(new KeyValuePair<uint, string>(0, WmvTerrainTileContent.RoleDiffuse));
        }
        foreach (uint id in m.HeightTextureIds)
            j.TextureEntries.Add(new KeyValuePair<uint, string>(id, WmvTerrainTileContent.RoleHeight));
        // The cap counts entries: an id with both roles (never observed) is one request and two entries in a request state.
        var entriesOf = new Dictionary<uint, int>();
        var unique = new List<uint>();
        foreach (KeyValuePair<uint, string> en in j.TextureEntries)
        {
            if (en.Key == 0)
                continue;
            int c;
            if (!entriesOf.TryGetValue(en.Key, out c))
                unique.Add(en.Key);
            entriesOf[en.Key] = c + 1;
        }
        int cap = j.Cmd.MaxTexturesPerLoad > 0 ? j.Cmd.MaxTexturesPerLoad : int.MaxValue;
        int askedEntries = 0, notRequested = 0;
        foreach (uint id in unique)
        {
            var t = new WmvTerrainTexture { FileDataID = id };
            j.Textures[id] = t;
            int entries = entriesOf[id];
            if (askedEntries + entries > cap)
            {
                t.Fail(WmvTerrainTexture.StateNotRequested, "this load may ask for at most " + cap + " textures (maxTexturesPerLoad)");
                notRequested++;
            }
            else if (id > int.MaxValue)
            {
                t.Fail(WmvTerrainTexture.StateNotRequested, "FileDataID " + id + " is beyond what a request can carry");
                notRequested++;
            }
            else if (TakeResidentTerrainTexture(j, id))
            {
                // Streaming: uploaded for another tile and resident in the scene: bound from there, nothing asked.
            }
            else
            {
                j.ToAsk.Enqueue(id);
                askedEntries += entries;
            }
        }
        var counts = new StringBuilder();
        foreach (KeyValuePair<string, int> kv in m.CounterPairs())
            if (kv.Value > 0)
                counts.Append(counts.Length > 0 ? ", " : "").Append(kv.Key).Append(' ').Append(kv.Value);
        Log("load {0}: material built in {1:0.###} ms on the worker: {2}{3}; {4} ({5} alpha, MPHD 0x{6:X}: {7}); {8} texture(s) of the drawable " +
            "chunks{9} ({10} layer references, {11} naming id 0, {12} repeats), {13} height texture(s) ({14} layer references, {15} repeats); " +
            "{16} drawable and {17} plain-diagnostic slice(s); {18} to ask for, {19} not requested",
            j.Cmd.Load, ms, TerrainTileMaterial.TileStateName(m.State), m.Reason != null ? " (" + m.Reason + ")" : "", counts,
            m.AlphaFormat, m.MphdFlags, m.UseHeightTextures ? "the height textures and MTXP heights enter the mix" : "no height textures (H = 1)",
            m.TextureIds.Length, plain ? " and " + m.PlainOnlyTextureIds.Length + " the plain-weights view adds" : "",
            m.LayerReferences, m.ZeroIdReferences, m.DuplicateReferences, m.HeightTextureIds.Length, m.HeightLayerReferences,
            m.HeightDuplicateReferences, m.DrawableSlices, m.PlainSlices, j.ToAsk.Count, notRequested);
        foreach (string note in m.Notes)
            Log("load {0}: material note: {1}", j.Cmd.Load, note);
        Advance(j);
    }

    void AskTextures(Job j)
    {
        while (j.ToAsk.Count > 0 && TileOut(j) < MaxOutstanding && StreamCredit(j))
        {
            uint id = j.ToAsk.Dequeue();
            if (j.TexturesFirstAskedMs < 0)
                j.TexturesFirstAskedMs = j.Clock.Elapsed.TotalMilliseconds;
            Ask(j, RoleTexture, (int)id);
            j.Textures[id].State = WmvTerrainTexture.StateRequested;
        }
    }

    void OnTexture(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        uint id = (uint)q.FileDataID;
        WmvTerrainTexture t;
        if (!j.Textures.TryGetValue(id, out t))
            throw new InvalidOperationException("texture " + id + " was asked for but is not in the load's list");
        double roundTrip = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        t.RoundTripMs = roundTrip;
        LogAnswer(j, "texture " + id, r, roundTrip);
        string problem = null, state = WmvTerrainTexture.StateTransportFailure;
        if (!r.ok)
        {
            if (!string.IsNullOrEmpty(r.state) && r.state != "available")
            {
                state = r.state;                                   // the host's state and detail, verbatim
                problem = !string.IsNullOrEmpty(r.detail) ? r.detail : (r.error ?? "");
            }
            else
                problem = string.IsNullOrEmpty(r.error) ? "no reason given" : r.error;
        }
        else if (r.data == null)
            problem = "the answer carries no bytes";
        else if (r.fileDataID > 0 && r.fileDataID != q.FileDataID)
            problem = "the answer names FileDataID " + r.fileDataID + ", the request " + q.FileDataID;
        else if (j.Cmd.MaxTextureBytes > 0 && r.data.Length > j.Cmd.MaxTextureBytes)
            problem = "answer of " + r.data.Length + " bytes exceeds maxTextureBytes " + j.Cmd.MaxTextureBytes;
        if (problem != null)
        {
            if (r.data != null) t.Bytes = r.data.Length;
            t.Fail(state, problem);
            j.TexturesLastDoneMs = j.Clock.Elapsed.TotalMilliseconds;
            Log("load {0}: texture {1}: {2}: {3}", j.Cmd.Load, id, state, problem);
            return;
        }
        // Prepared into a new entry on the worker, swapped in on the main thread: nothing the main thread reads is
        // written off it. Counted against K until then.
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        WmvTerrainFormatSupport support = j.Support;
        j.Preparing++;
        RunWorker(j, () =>
        {
            var p = new WmvTerrainTexture { FileDataID = id, RoundTripMs = roundTrip };
            p.Prepare(bytes, hostMd5, support);
            return () => OnTexturePrepared(j, p);
        });
    }

    void OnTexturePrepared(Job j, WmvTerrainTexture p)
    {
        j.Preparing--;
        j.Textures[p.FileDataID] = p;
        j.TexturesLastDoneMs = j.Clock.Elapsed.TotalMilliseconds;
        Log("load {0}: texture {1}: {2}{3}: {4} bytes, md5 {5}, {6}x{7} {8} as {9}, {10} mip(s), prepared in {11:0.###} ms on the worker",
            j.Cmd.Load, p.FileDataID, p.State, string.IsNullOrEmpty(p.Detail) ? "" : " (" + p.Detail + ")", p.Bytes, p.Md5, p.Width, p.Height,
            p.Encoding, string.IsNullOrEmpty(p.Format) ? "-" : p.Format, p.Mips, p.PrepareMs);
        Advance(j);
    }

    /// <summary>Every texture settled: the layers that use a diffuse texture that cannot be had draw the missing-texture
    /// checker (their Textured chunks become MissingTexture; a LayerEffectStatic chunk keeps its state), the chunks whose
    /// height mix reads a height texture that cannot be had become HeightTextureMissing.</summary>
    static void SettleTextures(WmvTerrainTileContent content, Action<string, object[]> log, int load)
    {
        TerrainTileMaterial m = content.Material;
        if (m == null || m.State == TerrainTileMaterialState.Unavailable)
            return;
        TerrainTileMaterialState before = m.State;
        List<uint> diffuse, height;
        content.SettleFailures(out diffuse, out height);
        if (diffuse.Count == 0 && height.Count == 0)
            return;
        int inPlace, staticInPlace, checker;
        content.CheckerChunks(diffuse, out inPlace, out staticInPlace, out checker);
        log("load {0}: {1} diffuse texture(s) cannot be had ({2}): {3} drawable chunk(s) draw the missing-texture checker in their place " +
            "({4} of them layerEffectStatic, which keeps its state; {5} drawable chunk(s) draw the checker in all, id-0 layers included); " +
            "{6} height texture(s) cannot be had ({7}): {8} chunk(s) heightTextureMissing, drawn neutral; material {9} -> {10}",
            new object[]
            {
                load, diffuse.Count, string.Join(", ", diffuse), inPlace, staticInPlace, checker, height.Count,
                string.Join(", ", height), m.Count(TerrainMaterialState.HeightTextureMissing), TerrainTileMaterial.TileStateName(before),
                TerrainTileMaterial.TileStateName(m.State),
            });
    }

    // ---------------------------------------------------------------- adoption and reports

    void Adopt(Job j)
    {
        if (j.Stream != null && j != current)
        {
            AdoptStreamTile(j);
            return;
        }
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        j.Content = new WmvTerrainTileContent
        {
            Material = j.Material ?? TerrainTileMaterial.Unavailable(j.MaterialUnavailable ?? "no material", j.MphdFlags),
            Textures = j.Textures, Tex0FileDataID = j.Tex0FileDataID, MapTextureFileDataID = j.MapTextureFileDataID, MphdFlags = j.MphdFlags,
            Build = cmd.Build ?? "", WdtMd5 = j.WdtMd5 ?? "", RootMd5 = j.RootMd5 ?? "", Tex0Md5 = j.Tex0Md5 ?? "",
            LiquidTile = j.Liquid != null ? j.Liquid.Tile : null, Liquids = j.Liquid != null ? j.Liquid.Registry : null,
        };
        SettleTextures(j.Content, Log, cmd.Load);
        var tile = new WmvRuntimeTerrainTile(cmd.MapId, cmd.TileX, cmd.TileY, cmd.Load);
        bool committed = false;
        try
        {
            var sw = Stopwatch.StartNew();
            view.BuildTile(tile, j.Mesh, j.Content);
            j.UploadMs = sw.Elapsed.TotalMilliseconds;

            // THE COMMIT. The new tile is the scene's in the same block that disposes every other one (and one under the
            // same key first); nothing can fail between the two.
            bool reset = sceneEpoch != cmd.AssetEpoch || sceneMapId != cmd.MapId;
            int replaced = DisposeTiles(tile);
            tiles[KeyOf(cmd.MapId, cmd.TileX, cmd.TileY)] = tile;
            shown = tile;
            shownLoad = cmd.Load;
            // Protocol 19: the load's liquid registry and zone lights are the scene's (DisposeTiles took the last scene's).
            sceneLiquids = j.Liquid != null ? j.Liquid.Registry : null;
            CommitSceneLights(j);
            // Protocol 20: the load's outdoor light is the scene's too (WmvTerrainSessionWorldLight.cs).
            CommitSceneWorldLight(j);
            int fromEpoch = sceneEpoch, fromMap = sceneMapId;
            sceneEpoch = cmd.AssetEpoch;
            sceneMapId = cmd.MapId;
            j.Tile = tile;
            ForgetShownInputs();
            shownMesh = j.Mesh;
            shownContent = j.Content;
            // A world load's scene: its world models are placed beside the tile, relative to the tile's own origin (the
            // scene's one floating origin), as they come (WmvTerrainSessionWorld.cs). DisposeTiles above took the last
            // scene's world models with its tiles.
            if (j.World != null)
                BeginTileWorld(j);
            committed = true;
            // Protocol 18: a streaming session's entry -- its textures the scene's, the region begins.
            if (j.Stream != null && stream != null)
                StreamEntryCommitted(j, tile);
            Log("load {0}: committed tile {1} of map {2} ({3} replaced{4}); Unity objects built in {5:0.###} ms; live terrain tiles {6}",
                cmd.Load, j.Tile2, cmd.MapId, replaced,
                reset ? (fromEpoch == int.MinValue ? ", a new scene" : string.Format(", the scene reset: epoch {0} -> {1}, map {2} -> {3}",
                                                                                     fromEpoch, sceneEpoch, fromMap, sceneMapId)) : "",
                j.UploadMs, WmvRuntimeTerrainTile.Live);
            viewActive = true;
            view.ShowTile(tile);
        }
        catch (Exception e)
        {
            string what = e.GetType().Name + ": " + e.Message;
            if (!committed)
            {
                // Never shown: release what it made (Live back) and report; the previous tile and the view are untouched.
                try { view.DisposeTile(tile); }
                catch (Exception e2) { Log("load {0}: disposing the unfinished tile failed: {1}", cmd.Load, e2.Message); }
                ReleasePrepared(j);
                Fail(j, "building the tile's Unity objects failed: " + what);
                return;
            }
            j.AdoptionNote = "adopted, but finishing the adoption failed: " + what;
            Log("load {0}: {1}", cmd.Load, j.AdoptionNote);
        }
        finally
        {
            // The load takes no answer or worker result any more -- but a world load's world models, and (protocol 19) a
            // load's liquid frames, still do, until its report (their requests are its own, and every one is retired when it
            // ends).
            if ((j.World == null && j.Liquid == null) || !committed)
                j.Dead = true;
        }
        // The textures the build uploaded dropped their prepared data as they went up (WmvTerrainTexture.Upload); the
        // others' stay with the shown tile's inputs while a view may still draw them (RebuildShown), and go with the tile.
        if (!committed)
            ReleasePrepared(j);
        else
            KeepOnlyWhatAViewMayUpload(j.Tile, j.Content);
        j.Phase = Phase.Adopted;
        j.AdoptFrame = frame;
        j.AdoptedMs = j.Clock.Elapsed.TotalMilliseconds;
        Log("load {0}: on screen from frame {1}; reported after frames {2} and {3} have started{4}", cmd.Load, frame, frame + 1, frame + 2,
            j.World != null ? ", and once its world models are settled" : "");
        if (j.World != null && committed)
            TileWorldShown(j);
    }

    /// <summary>The prepared texture data are the tile's now (or nobody's): dropped, so the job holds no file bytes.</summary>
    static void ReleasePrepared(Job j)
    {
        foreach (WmvTerrainTexture t in j.Textures.Values)
            t.ReleaseUploadData();
    }

    void FinishBuilt(Job j)
    {
        j.FirstFrameMs = j.Clock.Elapsed.TotalMilliseconds - j.AdoptedMs;
        current = null;
        // A view switch in the two frames before the report built the tile again (RebuildShown): the tile on screen is
        // still this load's, under the same load number -- its rebuild, not a replacement.
        if (shown != null && !shown.Disposed && shown != j.Tile && shownLoad == j.Cmd.Load && j.Tile != null && j.Tile.Disposed &&
            shown.MapId == j.Cmd.MapId && shown.TileX == j.Cmd.TileX && shown.TileY == j.Cmd.TileY)
            j.Tile = shown;
        if (shown != j.Tile || j.Tile == null || j.Tile.Disposed)
        {
            Report(j, "superseded", "the tile was replaced before its second frame");
            return;
        }
        string reason = j.AdoptionNote ?? "";
        try { view.TileBuilt(j.Tile, j.Mesh, j.Content, j.Cmd, j.RootFileDataID); }
        catch (Exception e)
        {
            string what = "the after-build step (dump, capture) failed: " + e.GetType().Name + ": " + e.Message;
            Log("load {0}: {1}", j.Cmd.Load, what);
            reason = reason.Length > 0 ? reason + "; " + what : what;
        }
        Report(j, "built", reason);
    }

    void Supersede(Job j, string reason)
    {
        if (current == j)
            current = null;
        Report(j, "superseded", reason);
    }

    void Fail(Job j, string reason)
    {
        // A tile of a streaming session (not its entry in flight): no report of its own (WmvTerrainSessionStream.cs).
        if (j.Stream != null && j != current)
        {
            FailStreamJob(j, reason);
            return;
        }
        if (current == j)
            current = null;
        if (j.Reported)
            return;
        Log("load {0}: FAILED: {1}", j.Cmd.Load, reason);
        if (j.Stream != null && stream != null && stream.Live && j.Tile != null && OwnsScene(j))
        {
            // A streaming session's entry, committed (its tile was on screen): reported built with why -- the session goes
            // on (an entry not built ends it for the host) -- and the camera it carried placed now.
            j.FirstFrameMs = j.Clock.Elapsed.TotalMilliseconds - j.AdoptedMs;
            try { worldView.WorldModelsSettled(world, j.Cmd.Load); }
            catch (Exception e) { Log("load {0}: placing the camera as the entry failed: {1}", j.Cmd.Load, e.Message); }
            Report(j, "built", "on screen, then failed: " + reason);
        }
        else
            Report(j, "failed", reason);
        if (j.Stream != null && stream != null)
        {
            if (!stream.Live)
                EndStream("its entry failed before its tile was committed: " + reason);
            else
                StreamJobEnded(j, StreamFailed, reason);
        }
    }

    void Report(Job j, string status, string reason)
    {
        j.Dead = true;
        Retire(j, status);
        if (j.Reported)
        {
            SecondReportsRefused++;
            Log("load {0}: already reported -- a second report ({1}) is not sent", j.Cmd.Load, status);
            return;
        }
        j.Reported = true;
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        var r = new WmvIpcClient.TerrainTileReport
        {
            Load = cmd.Load, Status = status, Reason = reason ?? "", HostState = j.HostState, HostDetail = j.HostDetail,
            MapId = cmd.MapId, TileX = cmd.TileX, TileY = cmd.TileY, WdtFileDataID = cmd.WdtFileDataID,
            RootFileDataID = j.RootFileDataID, WdtBytes = j.WdtBytes, RootBytes = j.RootBytes, WdtMd5 = j.WdtMd5, RootMd5 = j.RootMd5,
            WdtRoundTripMs = j.WdtRoundTripMs, WdtParseMs = j.WdtParseMs, RootRoundTripMs = j.RootRoundTripMs, ParseMs = j.ParseMs,
            BuildMs = j.BuildMs, UploadMs = j.UploadMs, FirstFrameMs = status == "built" ? j.FirstFrameMs : -1,
            TotalMs = j.Clock.Elapsed.TotalMilliseconds,
            RequestsRetired = j.RetiredIds.Count, RetiredRequestIds = new List<string>(j.RetiredIds),
            View = j.Tile != null ? WmvTerrainDebug.ViewNames[(int)j.Tile.View] : "",
        };
        TerrainTileMesh m = j.Mesh;
        if (m != null)
        {
            r.HasMesh = true;
            r.Chunks = m.Chunks.Length;
            r.ChunksBuilt = m.ChunksBuilt;
            r.ChunksSkipped = m.ChunksSkipped;
            r.ChunksUnsupported = m.ChunksUnsupported;
            r.Vertices = m.Vertices;
            r.Triangles = m.Triangles;
            r.HoleCells = m.HoleCells;
            r.MeshBytes = m.MeshBytes;
            r.HasBounds = m.BoundsMin != null && m.BoundsMax != null;
            if (r.HasBounds)
            {
                r.BoundsMin = new UnityEngine.Vector3(m.BoundsMin[0], m.BoundsMin[1], m.BoundsMin[2]);
                r.BoundsMax = new UnityEngine.Vector3(m.BoundsMax[0], m.BoundsMax[1], m.BoundsMax[2]);
            }
            r.RootOffset = j.Tile != null && j.Tile.Root != null ? j.Tile.Root.transform.localPosition : UnityEngine.Vector3.zero;
            r.OriginX = m.Origin.X;
            r.OriginY = m.Origin.Y;
            r.OriginZ = m.Origin.Z;
        }
        if (j.MaterialDone || status == "built")
            r.Material = MaterialReport(j);
        if (j.World != null)
            FillWorldReport(j, r, status);
        // Protocol 19: the load's liquids (offered false when its command carried none).
        r.Liquids = LiquidReport(j);
        try { view.Sample(r); }
        catch (Exception e) { Log("load {0}: sampling the counters failed: {1}", cmd.Load, e.Message); }
        ReportsSent++;
        Log("load {0} reported {1}{2}: tile {3} map {4}, wdt {5} ({6} bytes), root {7} ({8} bytes){9}; timings wdt {10:0.#} + parse {11:0.###}, " +
            "root {12:0.#} + parse {13:0.###} + build {14:0.###}, upload {15:0.###}, first frame {16:0.#}, total {17:0.#} ms; live terrain " +
            "tiles {18}, models {19}, world models {20}; Mesh objects {21}, Material objects {22}, terrain GameObjects {23}; managed heap " +
            "{24} bytes, native {25} bytes; {26} request(s) retired{27}",
            cmd.Load, status, string.IsNullOrEmpty(r.Reason) ? "" : " (" + r.Reason + ")", j.Tile2, cmd.MapId, cmd.WdtFileDataID,
            r.WdtBytes, r.RootFileDataID, r.RootBytes,
            r.HasMesh ? string.Format(", {0} vertices, {1} triangles, {2} hole cells", r.Vertices, r.Triangles, r.HoleCells) : "",
            r.WdtRoundTripMs, r.WdtParseMs, r.RootRoundTripMs, r.ParseMs, r.BuildMs, r.UploadMs, r.FirstFrameMs, r.TotalMs,
            r.LiveTerrainTiles, r.LiveModels, r.LiveMapObjects, r.MeshObjects, r.MaterialObjects, r.TerrainGameObjects,
            r.ManagedHeapBytes, r.NativeAllocatedBytes, r.RequestsRetired,
            r.RequestsRetired > 0 ? " (" + string.Join(", ", r.RetiredRequestIds.ToArray()) + ")" : "");
        if (r.Material != null)
            LogMaterial(cmd.Load, r.Material);
        if (r.World)
            LogWorld(cmd.Load, r);
        report(r);
    }

    /// <summary>The material of the report. A load that ended before its adoption (superseded, cleared, timed out,
    /// failed) never ran SettleTextures: its material is settled here with the texture failures already answered (a
    /// failed diffuse texture's layers the checker, a failed or alpha-less height texture's chunks HeightTextureMissing),
    /// so the chunk states agree with the texture entries beside them. A texture still requested (asked and not yet
    /// answered or prepared) or queued (not yet asked) is no failure yet; when there are such, the reason says how many
    /// ("unanswered") and that the chunk states count the answered failures only. Built loads are settled at adoption.</summary>
    WmvIpcClient.TerrainMaterialReport MaterialReport(Job j)
    {
        TerrainTileMaterial m = j.Material ?? TerrainTileMaterial.Unavailable(j.MaterialUnavailable ?? "the load ended before its material", j.MphdFlags);
        string unsettledNote = null;
        if (j.Content == null && j.Material != null && j.Material.State != TerrainTileMaterialState.Unavailable)
        {
            var settle = new WmvTerrainTileContent { Material = j.Material, Textures = j.Textures, MphdFlags = j.MphdFlags };
            List<uint> diffuse, height;
            settle.SettleFailures(out diffuse, out height);
            int unanswered = settle.Unsettled();
            if (unanswered > 0)
                unsettledNote = "the load ended before its textures settled: " + unanswered +
                                " texture(s) unanswered; chunk states count the answered failures only";
        }
        string reason = m.Reason ?? "";
        if (unsettledNote != null)
            reason = reason.Length > 0 ? reason + "; " + unsettledNote : unsettledNote;
        var r = new WmvIpcClient.TerrainMaterialReport
        {
            State = TerrainTileMaterial.TileStateName(m.State), Reason = reason, Tex0FileDataID = j.Tex0FileDataID,
            Tex0Bytes = j.Tex0Bytes, Tex0Md5 = j.Tex0Md5, AlphaFormat = m.AlphaFormat, ChunkStates = m.CounterPairs(),
            UniqueTextures = m.TextureIds.Length, LayerReferences = m.LayerReferences, DuplicateReferences = m.DuplicateReferences,
            Tex0RoundTripMs = j.Tex0RoundTripMs, Tex0ParseMs = j.Tex0ParseMs, MaterialBuildMs = j.MaterialBuildMs,
        };
        long source = 0;
        double upload = 0;
        bool uploaded = false;
        var counted = new HashSet<uint>();
        foreach (KeyValuePair<uint, string> en in j.TextureEntries)
        {
            WmvTerrainTexture t = j.Textures[en.Key];
            r.Textures.Add(t.ToReport(en.Value));
            if (!counted.Add(en.Key))
                continue;                                   // one file: its bytes and upload once
            if (t.Bytes > 0) source += t.Bytes;
            if (t.UploadMs >= 0) { upload += t.UploadMs; uploaded = true; }
        }
        r.TextureSourceBytes = source;
        r.TextureUploadMs = uploaded ? upload : -1;
        r.TextureFetchMs = j.TexturesFirstAskedMs >= 0 && j.TexturesLastDoneMs >= j.TexturesFirstAskedMs ? j.TexturesLastDoneMs - j.TexturesFirstAskedMs : -1;
        WmvRuntimeTerrainTile tile = j.Tile;
        if (tile != null && !tile.Disposed)
        {
            r.TextureGpuBytes = tile.TextureGpuBytes + SceneTextureBytesOf(j);   // (streaming: the scene's it binds too)
            r.AlphaGpuBytes = tile.AlphaGpuBytes;
            r.MaterialVertexBytes = tile.MaterialVertexBytes;
            r.Materials = tile.TexturedMaterialCount;
            r.AlphaBuildMs = tile.AlphaBuildMs;
            if (r.MaterialBuildMs >= 0 && tile.MaterialCreateMs >= 0)
                r.MaterialBuildMs += tile.MaterialCreateMs;
        }
        return r;
    }

    void LogMaterial(int load, WmvIpcClient.TerrainMaterialReport m)
    {
        var states = new StringBuilder();
        foreach (KeyValuePair<string, int> kv in m.ChunkStates)
            if (kv.Value > 0)
                states.Append(states.Length > 0 ? ", " : "").Append(kv.Key).Append(' ').Append(kv.Value);
        Log("load {0}: material {1}{2}: tex0 {3} ({4} bytes), {5} alpha; chunks {6}; {7} unique texture(s), {8} layer reference(s), {9} repeat(s); " +
            "texture bytes {10} received, {11} on the GPU; alpha {12} GPU bytes; material vertex bytes {13}; {14} material(s); timings tex0 {15:0.#} + " +
            "parse {16:0.###}, textures {17:0.#}, upload {18:0.###}, alpha {19:0.###}, material {20:0.###} ms",
            load, m.State, string.IsNullOrEmpty(m.Reason) ? "" : " (" + m.Reason + ")", m.Tex0FileDataID, m.Tex0Bytes, m.AlphaFormat,
            states.Length > 0 ? states.ToString() : "none", m.UniqueTextures, m.LayerReferences, m.DuplicateReferences, m.TextureSourceBytes,
            m.TextureGpuBytes, m.AlphaGpuBytes, m.MaterialVertexBytes, m.Materials, m.Tex0RoundTripMs, m.Tex0ParseMs, m.TextureFetchMs,
            m.TextureUploadMs, m.AlphaBuildMs, m.MaterialBuildMs);
        foreach (WmvIpcClient.TerrainTextureReport t in m.Textures)
            Log("load {0}:   texture {1} ({2}): {3}{4}{5}", load, t.FileDataID, t.Role, t.State, string.IsNullOrEmpty(t.Detail) ? "" : " (" + t.Detail + ")",
                t.Width >= 0 ? string.Format(", {0} bytes, md5 {1}, {2}x{3} {4}, {5} mip(s), upload {6:0.###} ms", t.Bytes, t.Md5, t.Width, t.Height,
                                             t.Format, t.Mips, t.UploadMs) : "");
    }

    /// <summary>The load ended: every request it still waits for goes to the retired ring (the oldest entry dropped when
    /// the ring is full), so that its answer, should it come, is dropped as late. The ids retired are the load's
    /// (requestsRetired in its report).</summary>
    void Retire(Job j, string ended)
    {
        foreach (Request q in j.Pending)
            RetireRequest(j, q, ended);
        j.Pending.Clear();
        j.ToAsk.Clear();
        if (j.World != null)
            EndWorld(j, ended);
        // Protocol 19: what it held of the liquids given back, its fetches wanted again (WmvTerrainSessionLiquids.cs).
        EndLiquids(j, ended);
    }

    /// <summary>One unanswered request into the retired ring (the caller takes it off the load's pending list).</summary>
    void RetireRequest(Job j, Request q, string ended)
    {
        retired[retiredNext] = new RetiredRequest { RequestId = q.Id, Role = q.Role ?? "", Load = j.Cmd.Load, Ended = ended };
        retiredNext = (retiredNext + 1) % RetiredCapacity;
        if (retiredCount < RetiredCapacity)
            retiredCount++;
        RetiredTotal++;
        lastRetired = q.Id;
        j.RetiredIds.Add(q.Id);
        StreamRequestDone(q.Id, null);
        Log("load {0}: request {1} ({2}) retired unanswered (the load ended \"{3}\"); {4} retired request(s) remembered, {5} retired in all",
            j.Cmd.Load, q.Id, q.Role, ended, retiredCount, RetiredTotal);
    }

    /// <summary>The retired request with this id, newest first; false when the ring does not hold it.</summary>
    public bool TryGetRetired(string requestId, out RetiredRequest found)
    {
        for (int k = 1; k <= retiredCount; k++)
        {
            RetiredRequest e = retired[(retiredNext - k + RetiredCapacity) % RetiredCapacity];
            if (e.RequestId == requestId)
            {
                found = e;
                return true;
            }
        }
        found = new RetiredRequest();
        return false;
    }

    /// <summary>Dispose every tile of the scene except `keep`, and the scene's world models with them; returns how many
    /// tiles went.</summary>
    int DisposeTiles(WmvRuntimeTerrainTile keep)
    {
        int n = 0;
        foreach (WmvRuntimeTerrainTile t in tiles.Values)
        {
            if (t == keep)
                continue;
            view.DisposeTile(t);
            n++;
        }
        tiles.Clear();
        DisposeWorldScene();
        DisposeTerrainTextures();
        DisposeSceneLiquids(keep);
        DropSceneLights();
        DropSceneWorldLight();
        return n;
    }

    static long KeyOf(int mapId, int tileX, int tileY) { return ((long)mapId << 12) | ((long)tileY << 6) | (long)tileX; }

    /// <summary>The md5 of the bytes, as lower-case hex (the host's md5 is the CASC content key, compared case-insensitively).</summary>
    public static string Md5Hex(byte[] data)
    {
        using (MD5 md5 = MD5.Create())
        {
            byte[] h = md5.ComputeHash(data);
            var sb = new StringBuilder(h.Length * 2);
            foreach (byte b in h)
                sb.Append(b.ToString("x2", System.Globalization.CultureInfo.InvariantCulture));
            return sb.ToString();
        }
    }

    void Log(string format, params object[] args)
    {
        log("[terrain] " + string.Format(System.Globalization.CultureInfo.InvariantCulture, format, args));
    }
}
