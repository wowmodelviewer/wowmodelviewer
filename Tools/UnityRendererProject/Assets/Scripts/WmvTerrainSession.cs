// WmvTerrainSession.cs
//
// THE TERRAIN LOAD AND ITS SCENE (protocol 12; stage 03 design, section 5): one tile of the active client's map,
// fetched from the host, parsed and built by the verified pure-C# code, and drawn on its own layer apart from the
// model and world-model jobs.
//
//   loadTerrainTile { load, assetEpoch, mapId, wdtFileDataID, tileX, tileY, maxAssetBytes }
//     -> getMapAsset role "wdt"        the map's WDT, never from the asset cache; the host checks the load, the epoch
//                                      and the catalogue before it reads, and says what it decided (state, detail)
//     -> WdtParser.Parse               on the main thread (a WDT is at most 0.3 MB): the tile's MAIN flag, then MAID
//                                      word 0 -- the root ADT's FileDataID in all four audited clients
//     -> getMapAsset role "rootAdt"
//     -> worker thread                 md5, AdtParser.ParseRoot, TerrainMeshBuilder.Build (Wow/World/TerrainMesh.cs);
//                                      the closure captures only immutable inputs (the bytes, x, y, the origin)
//     -> main thread (Pump)            the Unity objects, built INACTIVE (IWmvTerrainView.BuildTile), then THE COMMIT
//                                      -- the new tile becomes the scene's tile in the same block that disposes every
//                                      other one -- then shown (activated, the terrain view entered, framed)
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
//   - OWNERSHIP OF THE MAP ANSWERS IS BY ROUTING, NOT BY MEMORY. getMapAsset ids are the client's own "map<n>"
//     namespace, and WmvIpcClient hands every map answer (that id, or a role) to ClaimMapAssetResponse and to nothing
//     else -- never the asset cache, never the model, character or world-model handlers. So the session keeps no table
//     of its requests: the load in flight waits for ONE request (its PendingRequest), and an answer is applied only
//     when it names that request (and that load, when it states one). When a load ends by any path (a report of any
//     kind, the timeout included, or Dispose) its unanswered request moves to a ring of the last RetiredCapacity (32)
//     ended requests, oldest dropped first, so that an answer coming after its load ended is dropped and said as
//     "late" (or "unowned" when the ring no longer holds it, or never did). Whatever the host does -- never answer,
//     answer late, answer twice -- the session holds at most one pending and 32 retired requests. Counters say what
//     happened: RetiredTotal counts every retirement (it does not stop at the ring's 32), LastRetiredRequest names the
//     newest, and the late and unowned drops are counted apart.
//   - TimeoutMs (30 s; -wmvTerrainTimeoutMs, 1..120 s) after the first Pump that saw it, a load not yet adopted fails
//     "timed out after N s".
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

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Wmv.Wow.World;

/// <summary>The Unity side of the terrain session (WmvMainTerrain in the player, a test view in the self-test).</summary>
public interface IWmvTerrainView
{
    /// <summary>Build the tile's Unity objects, inactive, adding each to `tile` as it is created. May throw: the session
    /// then disposes `tile`.</summary>
    void BuildTile(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh);
    /// <summary>The committed tile goes on screen: activated, the terrain view entered (when it is not yet), framed.</summary>
    void ShowTile(WmvRuntimeTerrainTile tile);
    /// <summary>No tile is shown any more: the model view comes back as it was left. forModelLoad: a model load took the
    /// tile away (LeaveForModelLoad), and that load frames what it shows; otherwise clearTerrain did.</summary>
    void LeaveTerrainView(string why, bool forModelLoad);
    void DisposeTile(WmvRuntimeTerrainTile tile);
    /// <summary>Fill the counters of a report (live runtimes, Unity objects, memory), sampled now.</summary>
    void Sample(WmvIpcClient.TerrainTileReport report);
    /// <summary>A load is about to be reported "built" (its tile on screen two frames): the dump, the capture.</summary>
    void TileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvIpcClient.TerrainTileCommand command, int rootFileDataID);
}

public sealed class WmvTerrainSession
{
    /// <summary>A load not adopted this long after the first Pump that saw it fails (the default; -wmvTerrainTimeoutMs
    /// sets another within MinTimeoutMs..MaxTimeoutMs).</summary>
    public const double DefaultTimeoutMs = 30000.0, MinTimeoutMs = 1000.0, MaxTimeoutMs = 120000.0;
    /// <summary>How many ended loads' unanswered requests are remembered, to tell a late answer from an unowned one.</summary>
    public const int RetiredCapacity = 32;
    /// <summary>The host's MAX_MAP_ASSET_BYTES (4 MiB), for a command that states no limit.</summary>
    public const int DefaultMaxAssetBytes = 4 * 1024 * 1024;
    public const string RoleWdt = "wdt", RoleRootAdt = "rootAdt";

    enum Phase { Wdt, Root, Worker, Adopted }

    sealed class Job
    {
        public WmvIpcClient.TerrainTileCommand Cmd;
        public Phase Phase;
        public volatile bool Dead;            // no answer or worker result is applied any more
        public bool Reported;
        public string PendingRequest, PendingRole;   // the one request the load waits for (null for none)
        public readonly Stopwatch Clock = Stopwatch.StartNew();
        public double StartedAtMs = double.NaN;   // the Pump clock when the timeout began
        public int MaxBytes;
        public double WdtAskedMs, RootAskedMs, AdoptedMs;
        public long AdoptFrame;
        public int RootFileDataID;
        public long WdtBytes = -1, RootBytes = -1;
        public string WdtMd5 = "", RootMd5 = "";
        public double WdtRoundTripMs = -1, WdtParseMs = -1, RootRoundTripMs = -1, ParseMs = -1, BuildMs = -1, UploadMs = -1;
        public double FirstFrameMs = -1;
        public string HostState = "", HostDetail = "";
        public TerrainTileMesh Mesh;
        public WmvRuntimeTerrainTile Tile;
        public string AdoptionNote;           // after the commit: what went wrong finishing it

        public string Tile2 { get { return Cmd.TileX + "_" + Cmd.TileY; } }
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
    /// <summary>Requests waiting for their answer: the load in flight's one, or none (0 or 1).</summary>
    public int PendingRequests { get { return current != null && current.PendingRequest != null ? 1 : 0; } }
    /// <summary>Ended loads' unanswered requests remembered (at most RetiredCapacity).</summary>
    public int RetiredRequests { get { return retiredCount; } }
    /// <summary>Requests retired since the session began: one per load that ended with its request unanswered. Unlike
    /// RetiredRequests it never stops at RetiredCapacity, so every retirement shows as a step of one.</summary>
    public int RetiredTotal { get; private set; }
    /// <summary>The id of the request retired last, "" for none.</summary>
    public string LastRetiredRequest { get { return lastRetired; } }
    /// <summary>Map answers dropped: to a request of a load that had ended (the retired ring), and to any other request
    /// (one the ring no longer holds or never held, or the pending one under another load).</summary>
    public int LateAnswersDropped { get; private set; }
    public int UnownedAnswersDropped { get; private set; }
    public int AnswersDropped { get { return LateAnswersDropped + UnownedAnswersDropped; } }

    // ---------------------------------------------------------------- commands

    public void Load(WmvIpcClient.TerrainTileCommand cmd)
    {
        if (cmd == null || disposed)
            return;
        Log("load {0}: loadTerrainTile {1}", cmd.Load, cmd);
        if (current != null)
            Supersede(current, "superseded by terrain load " + cmd.Load);
        var j = new Job { Cmd = cmd, MaxBytes = cmd.MaxAssetBytes > 0 ? cmd.MaxAssetBytes : DefaultMaxAssetBytes };
        current = j;
        string bad = CommandProblem(cmd);
        if (bad != null)
        {
            Fail(j, bad);
            return;
        }
        Ask(j, Phase.Wdt, RoleWdt, cmd.WdtFileDataID);
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
        if (j != null)
        {
            current = null;
            Report(j, "cleared", why);
        }
        TakeTheTileAway(why, false);
    }

    /// <summary>A model load (any kind) arrived: the latest command wins. A load in flight is answered "superseded", the
    /// tile and the terrain view go, before the model load proceeds.</summary>
    public void LeaveForModelLoad(string reason)
    {
        if (disposed)
            return;
        Job j = current;
        if (j == null && tiles.Count == 0 && !viewActive)
            return;
        Log("a model load: {0}", reason);
        if (j != null)
            Supersede(j, reason);
        TakeTheTileAway(reason, true);
    }

    void TakeTheTileAway(string why, bool forModelLoad)
    {
        int n = DisposeTiles(null);
        if (n > 0)
            Log("{0} tile(s) disposed ({1}); live terrain tiles {2}", n, why, WmvRuntimeTerrainTile.Live);
        shown = null;
        shownLoad = 0;
        if (viewActive)
        {
            viewActive = false;
            view.LeaveTerrainView(why, forModelLoad);
        }
    }

    /// <summary>Shutdown: no report goes out (the connection is ending); the load in flight stops taking results, its
    /// request is retired, and the tiles are released.</summary>
    public void Dispose()
    {
        if (disposed)
            return;
        disposed = true;
        if (current != null)
        {
            current.Dead = true;
            Log("load {0}: dropped at shutdown, not reported", current.Cmd.Load);
            Retire(current, "dropped at shutdown");
            current = null;
        }
        int n = DisposeTiles(null);
        Log("session disposed: {0} tile(s) released", n);
        shown = null;
        shownLoad = 0;
    }

    // ---------------------------------------------------------------- answers and frames

    /// <summary>
    /// A map-file answer: WmvIpcClient routes every getMapAsset answer here (OnMapAssetResponse), and nowhere else, so
    /// the session takes each one. It is applied only when it answers the one request the load in flight waits for --
    /// that id, and that load when the answer states one (load > 0) -- and the load is alive. Anything else is dropped and
    /// said: "late" when the request belonged to a load that has ended (the retired ring), "unowned" otherwise. Returns
    /// whether the answer was applied.
    /// </summary>
    public bool ClaimMapAssetResponse(WmvIpcClient.AssetResponse r)
    {
        if (r == null)
            return false;
        Job j = current;
        string id = r.requestId ?? "";
        bool waitedFor = !disposed && j != null && !j.Dead && j.PendingRequest != null && j.PendingRequest == id;
        if (!waitedFor || (r.load > 0 && r.load != j.Cmd.Load))
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
                if (waitedFor)
                    Log("load {0}: the answer to its request {1} ({2}) names another load -- dropped; the request still waits", j.Cmd.Load,
                        id, what);
                else
                    Log("an answer to request {0} ({1}) that no load waits for, nor any of the last {2} ended -- dropped", id, what,
                        RetiredCapacity);
            }
            return false;
        }
        j.PendingRequest = null;
        j.PendingRole = null;
        try
        {
            if (j.Phase == Phase.Wdt) OnWdt(j, r);
            else OnRoot(j, r);
        }
        catch (Exception e)
        {
            Fail(j, "internal error handling the " + (j.Phase == Phase.Wdt ? "WDT" : "root ADT") + " answer: " +
                    e.GetType().Name + ": " + e.Message);
        }
        return true;
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
        Job j = current;
        if (j == null || disposed)
            return;
        if (double.IsNaN(j.StartedAtMs))
            j.StartedAtMs = nowMs;
        if (j.Phase != Phase.Adopted)
        {
            if (nowMs - j.StartedAtMs > timeoutMs)
                Fail(j, "timed out after " + (timeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                        " s waiting for " + WaitingFor(j));
            return;
        }
        if (frame >= j.AdoptFrame + 2)
            FinishBuilt(j);
    }

    static string WaitingFor(Job j)
    {
        switch (j.Phase)
        {
            case Phase.Wdt: return "the WDT answer (request " + j.PendingRequest + ")";
            case Phase.Root: return "the root ADT answer (request " + j.PendingRequest + ", FileDataID " + j.RootFileDataID + ")";
            default: return "the root ADT's parse and build on the worker thread";
        }
    }

    // ---------------------------------------------------------------- the steps

    static string CommandProblem(WmvIpcClient.TerrainTileCommand cmd)
    {
        if (cmd.Load <= 0)
            return "loadTerrainTile carries no load serial (" + cmd.Load + ")";
        if (cmd.TileX < 0 || cmd.TileX >= TerrainCoordinates.TilesPerSide || cmd.TileY < 0 || cmd.TileY >= TerrainCoordinates.TilesPerSide)
            return string.Format("tile {0}_{1} is outside 0..63", cmd.TileX, cmd.TileY);
        if (cmd.WdtFileDataID <= 0)
            return "loadTerrainTile names no WDT FileDataID (" + cmd.WdtFileDataID + ")";
        return null;
    }

    void Ask(Job j, Phase phase, string role, int fileDataID)
    {
        j.Phase = phase;
        double at = j.Clock.Elapsed.TotalMilliseconds;
        if (phase == Phase.Wdt) j.WdtAskedMs = at;
        else j.RootAskedMs = at;
        string id = requestMapAsset(role, fileDataID, j.Cmd.Load, j.Cmd.AssetEpoch);
        j.PendingRequest = id;
        j.PendingRole = role;
        Log("load {0}: getMapAsset {1} {2} (FileDataID {3}, epoch {4})", j.Cmd.Load, id, role, fileDataID, j.Cmd.AssetEpoch);
    }

    void LogAnswer(Job j, string what, WmvIpcClient.AssetResponse r)
    {
        Log("load {0}: {1} answer {2}: {3}, state \"{4}\"{5}, {6} bytes, md5 {7}, host read {8:0.###} ms, round trip {9:0.#} ms",
            j.Cmd.Load, what, r.requestId, r.ok ? "ok" : "failed", r.state ?? "",
            string.IsNullOrEmpty(r.detail) && string.IsNullOrEmpty(r.error) ? "" : " (" + (string.IsNullOrEmpty(r.detail) ? r.error : r.detail) + ")",
            r.data != null ? r.data.Length : 0, string.IsNullOrEmpty(r.md5) ? "-" : r.md5, r.readMs,
            what == "WDT" ? j.WdtRoundTripMs : j.RootRoundTripMs);
    }

    /// <summary>Why an answer cannot be used, or null. A host refusal fills the load's hostState and hostDetail.</summary>
    static string AnswerProblem(Job j, WmvIpcClient.AssetResponse r, string what, int fileDataID)
    {
        if (!r.ok)
        {
            // An answer the host called "available" whose bytes did not arrive is the transport's failure, not the host's.
            if (!string.IsNullOrEmpty(r.state) && r.state != "available")
            {
                j.HostState = r.state;
                j.HostDetail = !string.IsNullOrEmpty(r.detail) ? r.detail : (r.error ?? "");
                return string.Format("the host answered the {0} request (FileDataID {1}) \"{2}\": {3}", what, fileDataID, r.state,
                                     string.IsNullOrEmpty(j.HostDetail) ? "no detail" : j.HostDetail);
            }
            return string.Format("the {0} request (FileDataID {1}) failed: {2}", what, fileDataID,
                                 string.IsNullOrEmpty(r.error) ? "no reason given" : r.error);
        }
        if (r.data == null)
            return string.Format("the {0} answer (FileDataID {1}) carries no bytes", what, fileDataID);
        if (r.fileDataID > 0 && r.fileDataID != fileDataID)
            return string.Format("the {0} answer names FileDataID {1}, the request {2}", what, r.fileDataID, fileDataID);
        if (r.data.Length > j.MaxBytes)
            return string.Format("answer of {0} bytes exceeds maxAssetBytes {1} ({2}, FileDataID {3})", r.data.Length, j.MaxBytes,
                                 what, fileDataID);
        return null;
    }

    void OnWdt(Job j, WmvIpcClient.AssetResponse r)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        j.WdtRoundTripMs = j.Clock.Elapsed.TotalMilliseconds - j.WdtAskedMs;
        LogAnswer(j, "WDT", r);
        string why = AnswerProblem(j, r, "WDT", cmd.WdtFileDataID);
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
        Log("load {0}: WDT {1} parsed in {2:0.###} ms: {3}, {4} tile(s), {5} issue(s); MAID names root ADT {6} for {7}",
            cmd.Load, cmd.WdtFileDataID, j.WdtParseMs, wdt.Kind, wdt.TileCount, wdt.Issues.Count, j.RootFileDataID, j.Tile2);
        Ask(j, Phase.Root, RoleRootAdt, j.RootFileDataID);
    }

    void OnRoot(Job j, WmvIpcClient.AssetResponse r)
    {
        j.RootRoundTripMs = j.Clock.Elapsed.TotalMilliseconds - j.RootAskedMs;
        LogAnswer(j, "root ADT", r);
        string why = AnswerProblem(j, r, "root ADT", j.RootFileDataID);
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
        WorldOrigin origin = WorldToUnity.TileOrigin(x, y);
        string what = string.Format("root ADT {0} ({1}_{2})", j.RootFileDataID, x, y);
        j.Phase = Phase.Worker;
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
                try { mesh = TerrainMeshBuilder.Build(root, x, y, origin); }
                catch (TerrainBuildException e) { error = e.Message; }
                catch (Exception e) { error = "terrain mesh build failed: " + e.GetType().Name + ": " + e.Message; }
                buildMs = sw.Elapsed.TotalMilliseconds;
            }
            int issues = root != null ? root.Issues.Count : 0;
            return () => OnBuilt(j, md5, hostMd5, mesh, parseMs, buildMs, error, issues);
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
                    if (j.Dead || j != current)
                    {
                        Log("load {0}: its worker result came after the load ended -- dropped", j.Cmd.Load);
                        return;
                    }
                    apply();
                });
        });
    }

    void OnBuilt(Job j, string md5, string hostMd5, TerrainTileMesh mesh, double parseMs, double buildMs, string error, int issues)
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
        Log("load {0}: root ADT {1} ({2} bytes, md5 {3}) parsed in {4:0.###} ms ({5} issue(s)), mesh built in {6:0.###} ms on the worker: " +
            "{7} chunk(s) built, {8} skipped, {9} unsupported, {10} vertices, {11} triangles, {12} hole cell(s), {13} mesh bytes",
            j.Cmd.Load, j.RootFileDataID, j.RootBytes, md5, parseMs, issues, buildMs, mesh.ChunksBuilt, mesh.ChunksSkipped,
            mesh.ChunksUnsupported, mesh.Vertices, mesh.Triangles, mesh.HoleCells, mesh.MeshBytes);
        foreach (TerrainChunkMesh c in mesh.Chunks)
            if (c.State != TerrainChunkState.Built)
                Log("load {0}: chunk {1} {2}: {3}", j.Cmd.Load, c.Index, c.State, c.Reason);
        Adopt(j);
    }

    void Adopt(Job j)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        var tile = new WmvRuntimeTerrainTile(cmd.MapId, cmd.TileX, cmd.TileY, cmd.Load);
        bool committed = false;
        try
        {
            var sw = Stopwatch.StartNew();
            view.BuildTile(tile, j.Mesh);
            j.UploadMs = sw.Elapsed.TotalMilliseconds;

            // THE COMMIT. The new tile is the scene's in the same block that disposes every other one (and one under the
            // same key first); nothing can fail between the two.
            bool reset = sceneEpoch != cmd.AssetEpoch || sceneMapId != cmd.MapId;
            int replaced = DisposeTiles(tile);
            tiles[KeyOf(cmd.MapId, cmd.TileX, cmd.TileY)] = tile;
            shown = tile;
            shownLoad = cmd.Load;
            int fromEpoch = sceneEpoch, fromMap = sceneMapId;
            sceneEpoch = cmd.AssetEpoch;
            sceneMapId = cmd.MapId;
            j.Tile = tile;
            committed = true;
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
                Fail(j, "building the tile's Unity objects failed: " + what);
                return;
            }
            j.AdoptionNote = "adopted, but finishing the adoption failed: " + what;
            Log("load {0}: {1}", cmd.Load, j.AdoptionNote);
        }
        finally
        {
            j.Dead = true;    // the load takes no answer or worker result any more
        }
        j.Phase = Phase.Adopted;
        j.AdoptFrame = frame;
        j.AdoptedMs = j.Clock.Elapsed.TotalMilliseconds;
        Log("load {0}: on screen from frame {1}; reported after frames {2} and {3} have started", cmd.Load, frame, frame + 1, frame + 2);
    }

    void FinishBuilt(Job j)
    {
        j.FirstFrameMs = j.Clock.Elapsed.TotalMilliseconds - j.AdoptedMs;
        current = null;
        if (shown != j.Tile || j.Tile == null || j.Tile.Disposed)
        {
            Report(j, "superseded", "the tile was replaced before its second frame");
            return;
        }
        string reason = j.AdoptionNote ?? "";
        try { view.TileBuilt(j.Tile, j.Mesh, j.Cmd, j.RootFileDataID); }
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
        if (current == j)
            current = null;
        if (j.Reported)
            return;
        Log("load {0}: FAILED: {1}", j.Cmd.Load, reason);
        Report(j, "failed", reason);
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
        try { view.Sample(r); }
        catch (Exception e) { Log("load {0}: sampling the counters failed: {1}", cmd.Load, e.Message); }
        ReportsSent++;
        Log("load {0} reported {1}{2}: tile {3} map {4}, wdt {5} ({6} bytes), root {7} ({8} bytes){9}; timings wdt {10:0.#} + parse {11:0.###}, " +
            "root {12:0.#} + parse {13:0.###} + build {14:0.###}, upload {15:0.###}, first frame {16:0.#}, total {17:0.#} ms; live terrain " +
            "tiles {18}, models {19}, world models {20}; Mesh objects {21}, Material objects {22}, terrain GameObjects {23}; managed heap " +
            "{24} bytes, native {25} bytes",
            cmd.Load, status, string.IsNullOrEmpty(r.Reason) ? "" : " (" + r.Reason + ")", j.Tile2, cmd.MapId, cmd.WdtFileDataID,
            r.WdtBytes, r.RootFileDataID, r.RootBytes,
            r.HasMesh ? string.Format(", {0} vertices, {1} triangles, {2} hole cells", r.Vertices, r.Triangles, r.HoleCells) : "",
            r.WdtRoundTripMs, r.WdtParseMs, r.RootRoundTripMs, r.ParseMs, r.BuildMs, r.UploadMs, r.FirstFrameMs, r.TotalMs,
            r.LiveTerrainTiles, r.LiveModels, r.LiveMapObjects, r.MeshObjects, r.MaterialObjects, r.TerrainGameObjects,
            r.ManagedHeapBytes, r.NativeAllocatedBytes);
        report(r);
    }

    /// <summary>The load ended: the request it still waits for, if any, goes to the retired ring (the oldest entry
    /// dropped when the ring is full), so that its answer, should it come, is dropped as late.</summary>
    void Retire(Job j, string ended)
    {
        if (j.PendingRequest == null)
            return;
        retired[retiredNext] = new RetiredRequest { RequestId = j.PendingRequest, Role = j.PendingRole ?? "", Load = j.Cmd.Load, Ended = ended };
        retiredNext = (retiredNext + 1) % RetiredCapacity;
        if (retiredCount < RetiredCapacity)
            retiredCount++;
        RetiredTotal++;
        lastRetired = j.PendingRequest;
        Log("load {0}: request {1} ({2}) retired unanswered (the load ended \"{3}\"); {4} retired request(s) remembered, {5} retired in all",
            j.Cmd.Load, j.PendingRequest, j.PendingRole, ended, retiredCount, RetiredTotal);
        j.PendingRequest = null;
        j.PendingRole = null;
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

    /// <summary>Dispose every tile of the scene except `keep`; returns how many went.</summary>
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
