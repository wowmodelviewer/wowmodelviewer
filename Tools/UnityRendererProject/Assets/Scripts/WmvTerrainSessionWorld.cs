// WmvTerrainSessionWorld.cs
//
// THE WORLD LOAD (protocol 16, docs/world-viewer/06-world-models-in-world.md): the half of WmvTerrainSession that places
// world models. loadWorld is a terrain load that also places them:
//
//   a tile      WDT -> root + tex0 + obj0 (MAID word 1) -> the tile as before -> THE COMMIT (the scene: the tile, and
//               an empty set of world models at the tile's origin) -> worldShown
//               obj0 -> AdtParser.ParseObj0 (worker) -> the plan: MODF records, one placement per uniqueId, in the Tile
//               frame (WorldModelPlan) -> per unique world model, after the commit: wmoRoot -> WmoParser.ParseRoot
//               (worker) -> wmoGroup for each LOD0 group (worker parse) -> the textures its drawn materials sample
//               (WorldModelPlan.SampledTextures: the Buildings decode policy), wmoTexture each once per load, decoded on
//               the worker -> the prototype built on the main thread (one per frame) -> an instance per placement
//   a WMO-only  WDT -> its global MODF record, in the Global frame -> the world model as above -> THE COMMIT (the scene:
//   map         no tile, the world model at the scene's origin, the record's own position) -> worldShown
//
// Rules (beside the tile's, WmvTerrainSession.cs):
//   - EXACTLY ONE REPORT PER LOAD still: terrainTileLoaded comes when every placement has settled -- placed, failed,
//     unsupported (the record names no FileDataID), not requested (past a budget) or ended (the load ended first) -- and
//     the scene has been on screen two frames after its last placement; its "world" says how many of each and whether the
//     world is "complete" (every placement placed), "partial", "none" (the tile places nothing), "failed" (nothing
//     placed) or "ended". A WMO-only map whose world model cannot be built fails ("failed": nothing is shown, the scene
//     on screen stays).
//   - THE LOAD STAYS OPEN after the tile's commit: its world-model requests are answered under its load until the report.
//     At most MaxWorldOutstanding of them are out at once (a parse or decode on the worker counts until it is done),
//     beside the tile's K; at most MaxActiveModels world models are being fetched at once (their parsed data held).
//   - BUDGETS: the plan asks for at most maxWmoModelsPerLoad world models and places at most maxWmoPlacementsPerLoad
//     placements (the rest "notRequested", in record order); a world model whose groups would pass maxWmoGroupsPerLoad is
//     not fetched further ("notRequested"); textures past maxWmoTexturesPerLoad are not asked for (drawn white, as the
//     builder draws a missing one). The host enforces the byte budgets and refuses past them.
//   - THE WORLD'S TIMEOUT: WorldTimeoutMs after its world models began, any not yet built fail ("timed out"), their
//     requests are retired, and the load is reported with what was placed.
//   - THE COMMIT disposes every tile and world model of the scene before (DisposeTiles takes both), in one block; a
//     tile load's world models then join the scene as they are built, each placement on screen the frame it is placed.
//   - Shared data: one prototype per world model per load, every placement an instance of it; a texture is fetched and
//     decoded once per load for every world model that samples it, its pixels released once the last of them is built.
//   - DOODADS (protocol 17, WmvTerrainSessionDoodads.cs): once the world models are settled, a load that carries the doodad
//     budgets places the tile's MDDF doodads and the placed world models' doodad sets; the report waits for them too.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using Wmv.Wow;
using Wmv.Wow.World;

/// <summary>The Unity side of a world load's world models (WmvMainTerrain's MainWorldView in the player; a test view in the
/// self-test).</summary>
public interface IWmvWorldModelView
{
    /// <summary>A prototype: built inactive, never shown. May throw (nothing is left built).</summary>
    WmvWorldPrototype BuildPrototype(uint fileDataID, WmoRoot root, WmoGroup[] groups, Dictionary<uint, WmvWmoTexture> textures);
    /// <summary>An instance of `prototype` at `placement`, relative to `origin`, active, on the world layer. May throw.</summary>
    WmvWorldInstance PlaceInstance(WmvWorldPrototype prototype, WorldModelPlacement placement, WorldOrigin origin, int load);
    void DisposeInstance(WmvWorldInstance instance);
    void DisposePrototype(WmvWorldPrototype prototype);
    /// <summary>A scene with no tile was committed (a WMO-only map): the world view entered, the scene framed, the camera its
    /// load carries placed.</summary>
    void ShowWorld(WmvWorldScene scene);
    /// <summary>A WMO-only map's load is about to be reported built: the capture.</summary>
    void WorldBuilt(WmvWorldScene scene);
    void ReportWorldShown(WmvIpcClient.WorldShownReport report);
    void ReportWorldProgress(WmvIpcClient.WorldProgressReport report);
    /// <summary>The load's world models are settled (stage 07): the moment its camera is placed -- it stands on their floors --
    /// before any doodad is fetched.</summary>
    void WorldModelsSettled(WmvWorldScene scene, int load);
    /// <summary>A doodad prototype for `scene` (its textures from the scene's shared cache): built inactive, never shown. May
    /// throw (nothing is left built).</summary>
    WmvDoodadPrototype BuildDoodadPrototype(WmvWorldScene scene, uint fileDataID, M2ParsedModel model, M2ParsedSkin skin,
                                            Dictionary<int, BlpImage> texturesBySlot, Dictionary<int, uint> slotFiles);
    /// <summary>An instance of `prototype` at `doodad`, relative to `origin`, active, on the world layer. May throw.</summary>
    WmvDoodadInstance PlaceDoodad(WmvDoodadPrototype prototype, WorldDoodad doodad, WorldOrigin origin, int load);
    /// <summary>Every doodad of `scene`: its instances, then its prototypes, then its shared textures.</summary>
    void DisposeDoodads(WmvWorldScene scene);
    /// <summary>Streaming (stage 08): one doodad prototype evicted from the session's pool, its references on the scene's
    /// textures given back (the caller has taken it out of the scene's dictionary), or a tile's private one (disposed unused;
    /// or, at the scene's end, after DisposeDoodads -- the cache gone, nothing to give back).</summary>
    void DisposeDoodadPrototype(WmvWorldScene scene, WmvDoodadPrototype prototype);
    /// <summary>Streaming: the doodads a closing tile held alone, their own objects (the caller has taken them out of the
    /// scene's list).</summary>
    void DisposeDoodadInstances(List<WmvDoodadInstance> instances);
}

public sealed partial class WmvTerrainSession
{
    public const string RoleObj0Adt = "obj0Adt", RoleWmoRoot = "wmoRoot", RoleWmoGroup = "wmoGroup", RoleWmoTexture = "wmoTexture";
    /// <summary>A world load's world-model requests out at once (a parse or decode counts until done), beside the tile's K.</summary>
    public const int MaxWorldOutstanding = 4;
    /// <summary>World models being fetched at once: their parsed root, groups and textures are held until they are built.</summary>
    public const int MaxActiveModels = 4;
    /// <summary>worldProgress at most this often while a world load is in flight (and when its phase changes).</summary>
    public const double ProgressIntervalMs = 250.0;
    /// <summary>The placements terrainTileLoaded lists at most (the rest are counted).</summary>
    public const int ReportPlacementsMax = 256;
    /// <summary>The world models' own timeout, from the moment they began (the tile's commit, or a WMO-only map's WDT).</summary>
    public const double WorldTimeoutMs = 120000.0;

    // Borrowed (streaming): waiting for the prototype another tile of the session is fetching.
    enum ModelState { Queued, Root, Groups, Textures, Ready, Built, Failed, NotRequested, Borrowed }

    static string ModelStateName(ModelState s)
    {
        switch (s)
        {
            case ModelState.Queued: return "queued";
            case ModelState.Root: return "root";
            case ModelState.Groups: return "groups";
            case ModelState.Textures: return "textures";
            case ModelState.Ready: return "ready";
            case ModelState.Built: return "built";
            case ModelState.Failed: return "failed";
            case ModelState.Borrowed: return "waiting";
            default: return "notRequested";
        }
    }

    /// <summary>One unique world model of a load: its placements, its files and its prototype.</summary>
    sealed class Model
    {
        public uint FileDataID;
        public ModelState State = ModelState.Queued;
        public string Reason = "";
        public readonly List<PlacementWork> Placements = new List<PlacementWork>();
        public WmoRoot Root;
        public uint[] GroupIds = new uint[0];
        public WmoGroup[] Groups = new WmoGroup[0];
        public string[] GroupErrors = new string[0];
        public int NextGroup, GroupsLanded;
        public int GroupsRefused;               // streaming: group files the host refused for the tile's load
        public int GroupsRepeated;              // streaming: group asks refused as repeats of one this load asked (the host's
                                                // once-per-load rule)
        public readonly List<uint> TextureIds = new List<uint>();
        public readonly HashSet<uint> TexturesWaiting = new HashSet<uint>();
        public bool Active;                    // counted against MaxActiveModels
        public long Bytes;
        public WmvWorldPrototype Built;
        // Streaming (WmvTerrainSessionStream.cs): its job, the session's prototype entry it fetches or waits for, whether it
        // met the session's prototypes yet, and whether it let go of its pin.
        public Job Job;
        public ProtoEntry Shared;
        public bool Claimed, Unpinned;
    }

    /// <summary>A placement of the plan and what became of it.</summary>
    sealed class PlacementWork
    {
        public WorldModelPlacement P;
        public string State = "planned";       // "planned" | "placed" | "failed" | "unsupported" | "notRequested" | "ended"
        public string Reason = "";
        public WmvWorldInstance Instance;
        public bool Shared;                    // streaming: placed for another tile of the session, found here
    }

    /// <summary>A texture of the load's world models: fetched and decoded once, shared by every model that samples it.</summary>
    sealed class SharedTexture
    {
        public uint Id;
        public WmvWmoTexture Texture;
        public bool Asked, Done;
        public int Users;                      // models not yet built that sample it
        public byte[] Raw;                     // the file's bytes, checked: decoded again for a model that comes after the
                                               // pixels were released (null: none -- not fetched, or refused)
        public bool Decoding;                  // a decode of Raw is on the worker
        public bool ForTheLoad;                // streaming: missing for this tile's load only (refused, stale, past its count
                                               // cap): a prototype built without it is the tile's own
        public readonly List<Model> Waiting = new List<Model>();
    }

    sealed class WorldWork
    {
        public bool Global;
        public PlacementFrame Frame;
        public int Obj0FileDataID;
        public bool Obj0Asked, Obj0Answered, PlanReady, Committed, Done;
        public string PlanProblem;             // why there is no plan (an obj0 that could not be had or parsed)
        public int Records;
        public readonly List<PlacementWork> Placements = new List<PlacementWork>();
        public readonly List<Model> Models = new List<Model>();
        public readonly Dictionary<uint, SharedTexture> Textures = new Dictionary<uint, SharedTexture>();
        public readonly Queue<uint> TextureQueue = new Queue<uint>();
        public readonly Queue<SharedTexture> RedecodeQueue = new Queue<SharedTexture>();   // decoded again, within the gate
        public readonly Queue<Model> BuildQueue = new Queue<Model>();
        public readonly HashSet<uint> GroupIdsAsked = new HashSet<uint>();     // group ids this load asked for (AskWorld)
        public readonly HashSet<uint> GroupIdsRefused = new HashSet<uint>();   // ... of them answered refused or stale for it
        public int Pending, Preparing;         // world requests out; parses and decodes on the worker
        public int ModelsAsked, GroupFilesAsked, GroupFilesReserved, TexturesAsked, TexturesNotRequested;
        public bool TimedOut;                  // the world's timeout fired: worker results still out are dropped
        public int FilesAsked, FilesAnswered;
        public long FileBytes, TextureBytes;
        public int GroupFilesMissing, TexturesDecoded, TexturesMissing;
        // The decoded texture pixels held now, and the most held at once (a texture's pixels go when its last waiting model
        // is built; its file's bytes are kept to decode it again for a model fetched later -- the host serves an id once).
        public long PixelBytesHeld, PixelBytesHeldMax;
        public int TexturesRedecoded;          // decodes of a texture whose pixels had gone (Redecode)
        public int TexturesAsBlocks, TexturesDecodedRgba;   // stage 08: prepared as their own DXT blocks, or decoded
        public double BeganMs = -1, ShownMs = -1, LastAnswerMs = -1;
        public double ParseCpuMs, DecodeCpuMs, BuildMs, BuildMaxMs, PlaceMs;
        public double TimeoutStartedAtMs = double.NaN;
        public long LastCommitFrame;
        public double LastProgressMs = -1;
        public string LastPhase = "", LastDoodadPhase = "";
        public bool HasStart;
        public WorldPoint Start;
        public string StartReason = "";
        public string Failure;                 // a WMO-only map's world model could not be built
        public DoodadsWork Doodads;            // protocol 17: the load's doodads (WmvTerrainSessionDoodads.cs)
        public int Out { get { return Pending + Preparing; } }
    }

    IWmvWorldModelView worldView;
    WmvWorldScene world;

    /// <summary>The Unity side of world loads (protocol 16); a loadWorld fails without one.</summary>
    public IWmvWorldModelView WorldView { get { return worldView; } set { worldView = value; } }
    /// <summary>The scene's world models: what is placed, its origin and its geometry (null when the scene has none).</summary>
    public WmvWorldScene World { get { return world; } }

    // ---------------------------------------------------------------- the plan

    void AskObj0(Job j, uint obj0)
    {
        WorldWork w = j.World;
        if (obj0 == 0 || obj0 > int.MaxValue)
        {
            w.PlanProblem = obj0 == 0 ? "MAID names no obj0 ADT for " + j.Tile2 + ": the tile places no world model"
                                      : "MAID names obj0 ADT FileDataID " + obj0 + ", beyond what a request can carry";
            w.PlanReady = true;
            Log("load {0}: no obj0 asked for: {1}", j.Cmd.Load, w.PlanProblem);
            return;
        }
        w.Obj0FileDataID = (int)obj0;
        w.Obj0Asked = true;
        Ask(j, RoleObj0Adt, w.Obj0FileDataID);
    }

    void OnObj0(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        WorldWork w = j.World;
        w.Obj0Answered = true;
        double roundTrip = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        LogAnswer(j, "obj0 ADT", r, roundTrip);
        string why = AnswerProblem(j, r, "obj0 ADT", w.Obj0FileDataID, j.MaxBytes, "maxAssetBytes", false);
        if (why != null)
        {
            w.PlanProblem = why;
            w.PlanReady = true;
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        int x = j.Cmd.TileX, y = j.Cmd.TileY, fid = w.Obj0FileDataID;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            AdtObj0 obj0 = null;
            string error = null;
            try { obj0 = AdtParser.ParseObj0(bytes, x, y, "obj0 ADT " + fid); }
            catch (Exception e) { error = "obj0 ADT parse failed: " + e.Message; }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () => OnObj0Parsed(j, md5, hostMd5, obj0, ms, error, bytes.Length);
        });
    }

    void OnObj0Parsed(Job j, string md5, string hostMd5, AdtObj0 obj0, double ms, string error, int bytes)
    {
        WorldWork w = j.World;
        if (w.TimedOut)
            return;                // the world models timed out first: settled without a plan
        if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
            error = string.Format("the obj0 ADT bytes received hash to {0}, the host served {1}", md5, hostMd5);
        if (error != null)
        {
            w.PlanProblem = error;
            w.PlanReady = true;
            Log("load {0}: no world model is placed: {1}", j.Cmd.Load, error);
            AdvanceWorld(j);
            return;
        }
        w.FileBytes += bytes;
        Log("load {0}: obj0 ADT {1} ({2} bytes, md5 {3}) parsed in {4:0.###} ms on the worker: {5} MODF record(s), {6} MDDF record(s), " +
            "{7} issue(s)", j.Cmd.Load, w.Obj0FileDataID, bytes, md5, ms, obj0.Wmos.Length, obj0.Doodads.Length, obj0.Issues.Count);
        Plan(j, obj0.Wmos);
        // The MDDF records wait for the world models to settle (WmvTerrainSessionDoodads.cs).
        if (w.Doodads != null && w.Doodads.Offered)
        {
            w.Doodads.Mddf = obj0.Doodads;
            w.Doodads.HaveMddf = true;
        }
        AdvanceWorld(j);
    }

    /// <summary>A WMO-only map's world load after its WDT: the global record is the plan; there is no tile.</summary>
    void StartGlobalWorld(Job j, WdtFile wdt)
    {
        WorldWork w = j.World;
        if (wdt.GlobalWmos == null || wdt.GlobalWmos.Length == 0)
        {
            Fail(j, "the WDT is " + wdt.Kind + ": it names no global world model (no MODF record)");
            return;
        }
        j.MphdFlags = wdt.Header.Words != null && wdt.Header.Words.Length > 0 ? wdt.Header.Flags : 0u;
        Log("load {0}: WDT {1} parsed in {2:0.###} ms: {3}, MPHD 0x{4:X}, {5} global MODF record(s)", j.Cmd.Load, j.Cmd.WdtFileDataID,
            j.WdtParseMs, wdt.Kind, j.MphdFlags, wdt.GlobalWmos.Length);
        j.Phase = Phase.World;
        Plan(j, wdt.GlobalWmos);
        if (w.Models.Count == 0)
        {
            Fail(j, "the map's world model cannot be placed: " + (w.Placements.Count > 0 ? w.Placements[0].Reason : "no placement"));
            return;
        }
        w.BeganMs = j.Clock.Elapsed.TotalMilliseconds;
        AdvanceWorld(j);
    }

    /// <summary>The plan from MODF records: one placement per uniqueId (WorldModelPlan), the budgets applied in record order.</summary>
    void Plan(Job j, IList<WmoPlacement> records)
    {
        WorldWork w = j.World;
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        var notes = new List<string>();
        List<WorldModelPlacement> plan = WorldModelPlan.Build(records, w.Frame, notes);
        w.Records = records != null ? records.Count : 0;
        var byId = new Dictionary<uint, Model>();
        int placementsAllowed = cmd.MaxWmoPlacementsPerLoad > 0 ? cmd.MaxWmoPlacementsPerLoad : int.MaxValue;
        int modelsAllowed = cmd.MaxWmoModelsPerLoad;
        int placed = 0;
        foreach (WorldModelPlacement p in plan)
        {
            var pw = new PlacementWork { P = p };
            w.Placements.Add(pw);
            if (p.Unsupported != null)
            {
                pw.State = "unsupported";
                pw.Reason = p.Unsupported;
                continue;
            }
            if (placed >= placementsAllowed)
            {
                pw.State = "notRequested";
                pw.Reason = "this load places at most " + placementsAllowed + " world models (maxWmoPlacementsPerLoad)";
                continue;
            }
            Model m;
            if (!byId.TryGetValue(p.FileDataID, out m))
            {
                if (byId.Count >= modelsAllowed)
                {
                    pw.State = "notRequested";
                    pw.Reason = "this load asks for at most " + modelsAllowed + " world models (maxWmoModelsPerLoad)";
                    continue;
                }
                m = new Model { FileDataID = p.FileDataID, Job = j };
                byId[p.FileDataID] = m;
                w.Models.Add(m);
            }
            m.Placements.Add(pw);
            placed++;
        }
        w.PlanReady = true;
        foreach (string n in notes)
            Log("load {0}: plan note: {1}", cmd.Load, n);
        int unsupported = 0, notRequested = 0;
        foreach (PlacementWork pw in w.Placements)
        {
            if (pw.State == "unsupported") unsupported++;
            else if (pw.State == "notRequested") notRequested++;
        }
        Log("load {0}: world plan ({1} frame): {2} MODF record(s), {3} placement(s) by uniqueId, {4} to place over {5} unique world model(s), " +
            "{6} unsupported, {7} not requested", cmd.Load, w.Frame, w.Records, w.Placements.Count, placed, w.Models.Count, unsupported, notRequested);
        foreach (PlacementWork pw in w.Placements)
            Log("load {0}:   {1}{2}", cmd.Load, pw.P, pw.State != "planned" ? " -- " + pw.State + ": " + pw.Reason : "");
    }

    // ---------------------------------------------------------------- the scene

    /// <summary>The tile's commit of a world load (Adopt, inside the commit block): the scene's world models begin, empty,
    /// at the tile's origin.</summary>
    void BeginTileWorld(Job j)
    {
        WorldWork w = j.World;
        world = new WmvWorldScene
        {
            Load = j.Cmd.Load, MapId = j.Cmd.MapId, Epoch = j.Cmd.AssetEpoch, Kind = "tile", Origin = j.Mesh.Origin,
        };
        w.Committed = true;
        w.BeganMs = j.Clock.Elapsed.TotalMilliseconds;
    }

    /// <summary>After the tile's commit and its camera: worldShown, then the world models are fetched.</summary>
    void TileWorldShown(Job j)
    {
        WorldWork w = j.World;
        w.ShownMs = j.Clock.Elapsed.TotalMilliseconds;
        SendShown(j, "tile");
        AdvanceWorld(j);
    }

    void SendShown(Job j, string kind)
    {
        WorldWork w = j.World;
        var r = new WmvIpcClient.WorldShownReport
        {
            Load = j.Cmd.Load, MapId = j.Cmd.MapId, TileX = j.Cmd.TileX, TileY = j.Cmd.TileY, Kind = kind,
            Origin = world != null ? new[] { world.Origin.X, world.Origin.Y, world.Origin.Z } : new double[3],
            Placements = w.Placements.Count, Models = w.Models.Count, ShownMs = w.ShownMs,
        };
        Log("load {0}: worldShown ({1}) after {2:0.#} ms, origin {3}; {4} placement(s) over {5} world model(s) planned so far", j.Cmd.Load, kind,
            w.ShownMs, world != null ? world.Origin.ToString() : "-", r.Placements, r.Models);
        try { worldView.ReportWorldShown(r); }
        catch (Exception e) { Log("load {0}: sending worldShown failed: {1}", j.Cmd.Load, e.Message); }
    }

    /// <summary>The scene's world models go (with its tiles: DisposeTiles): its doodads first (instances, prototypes, textures),
    /// then every world-model instance, then every world-model prototype.</summary>
    void DisposeWorldScene()
    {
        WmvWorldScene s = world;
        world = null;
        if (s == null || worldView == null)
            return;
        int doodads = s.Doodads.Count, doodadPrototypes = s.DoodadPrototypes.Count + s.PrivateDoodadPrototypes.Count;
        try { worldView.DisposeDoodads(s); }
        catch (Exception e) { Log("disposing the doodads failed: {0}", e.Message); }
        // Streaming: the doodad prototypes kept for one tile (not in DoodadPrototypes) -- their instances went above, and the
        // scene's texture cache with them (nothing is given back to it now).
        foreach (WmvDoodadPrototype p in s.PrivateDoodadPrototypes)
        {
            try { worldView.DisposeDoodadPrototype(s, p); }
            catch (Exception e) { Log("disposing a doodad prototype kept for its tile failed: {0}", e.Message); }
        }
        s.PrivateDoodadPrototypes.Clear();
        if (doodads > 0 || doodadPrototypes > 0)
            Log("the doodads of load {0} ({1}) disposed: {2} instance(s), {3} prototype(s); live doodad instances {4}, prototypes {5}, textures {6}",
                s.Load, s.Kind, doodads, doodadPrototypes, WmvWorldDoodads.LiveInstances, WmvWorldDoodads.LivePrototypes, WmvWorldDoodads.LiveTextures);
        int instances = s.Instances.Count, prototypes = s.Prototypes.Count + s.PrivatePrototypes.Count;
        foreach (WmvWorldInstance i in s.Instances)
        {
            try { worldView.DisposeInstance(i); }
            catch (Exception e) { Log("disposing a world-model instance failed: {0}", e.Message); }
        }
        foreach (WmvWorldPrototype p in s.Prototypes.Values)
        {
            try { worldView.DisposePrototype(p); }
            catch (Exception e) { Log("disposing a world-model prototype failed: {0}", e.Message); }
        }
        foreach (WmvWorldPrototype p in s.PrivatePrototypes)
        {
            try { worldView.DisposePrototype(p); }
            catch (Exception e) { Log("disposing a world-model prototype kept for its tile failed: {0}", e.Message); }
        }
        s.PrivatePrototypes.Clear();
        Log("the world models of load {0} ({1}) disposed: {2} instance(s), {3} prototype(s); live world instances {4}, prototypes {5}",
            s.Load, s.Kind, instances, prototypes, WmvWorldModels.LiveInstances, WmvWorldModels.LivePrototypes);
    }

    // ---------------------------------------------------------------- fetching

    /// <summary>The world models' next requests, whenever something arrived: textures of the models in flight first, then
    /// their groups, then the next models' roots -- within MaxWorldOutstanding and MaxActiveModels; then whether the world
    /// is done.</summary>
    void AdvanceWorld(Job j)
    {
        WorldWork w = j.World;
        if (w == null || !Live(j) || j.Dead || !w.PlanReady)
            return;
        // A tile's world models wait for its commit; a WMO-only map's begin after its WDT.
        if (!w.Global && !w.Committed)
            return;
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        // Streaming: the session's prototypes first (built ones placed from, fetches waited for: WmvTerrainSessionStream.cs).
        if (j.Stream != null && stream != null)
            ClaimWorldModels(j);
        while (w.Out < MaxWorldOutstanding && StreamCredit(j))
        {
            // A texture decoded again counts against the same gate as a fetch, so a model sampling many textures does not
            // start them all on the worker at once.
            if (w.RedecodeQueue.Count > 0)
            {
                Redecode(j, w.RedecodeQueue.Dequeue());
                continue;
            }
            if (w.TextureQueue.Count > 0)
            {
                uint id = w.TextureQueue.Dequeue();
                AskWorld(j, RoleWmoTexture, (int)id, null, -1);
                continue;
            }
            Model next = null;
            foreach (Model m in w.Models)
                if (m.State == ModelState.Groups && m.NextGroup < m.GroupIds.Length)
                {
                    next = m;
                    break;
                }
            if (next != null)
            {
                int gi = next.NextGroup++;
                if (next.GroupIds[gi] == 0 || next.GroupIds[gi] > int.MaxValue)
                {
                    next.GroupErrors[gi] = next.GroupIds[gi] == 0 ? "GFID entry is 0" : "FileDataID beyond what a request can carry";
                    GroupLanded(j, next);
                    continue;
                }
                AskWorld(j, RoleWmoGroup, (int)next.GroupIds[gi], next, gi);
                continue;
            }
            int active = 0;
            foreach (Model m in w.Models)
                if (m.Active) active++;
            if (active >= MaxActiveModels)
                break;
            foreach (Model m in w.Models)
                if (m.State == ModelState.Queued)
                {
                    next = m;
                    break;
                }
            if (next == null)
                break;
            next.Active = true;
            next.State = ModelState.Root;
            w.ModelsAsked++;
            AskWorld(j, RoleWmoRoot, (int)next.FileDataID, next, -1);
        }
        CheckWorldDone(j);
    }

    void AskWorld(Job j, string role, int fileDataID, Model m, int group)
    {
        WorldWork w = j.World;
        double at = j.Clock.Elapsed.TotalMilliseconds;
        string id = SendMapRequest(j, role, fileDataID);
        // A group id this load asked for already (another model's, or listed twice): the host refuses it once read for the
        // load ("each once per load", modelviewer.cpp decideMapAssetRequest) -- see OnWmoGroup.
        bool before = role == RoleWmoGroup && !w.GroupIdsAsked.Add((uint)fileDataID);
        j.Pending.Add(new Request { Id = id, Role = role, FileDataID = fileDataID, AskedMs = at, World = true, Model = m, Group = group,
                                    AskedBefore = before });
        w.Pending++;
        w.FilesAsked++;
        if (role == RoleWmoGroup) w.GroupFilesAsked++;
        else if (role == RoleWmoTexture) w.TexturesAsked++;
        OutstandingPeak = Math.Max(Outstanding, OutstandingPeak);
    }

    /// <summary>Why a world-model answer cannot be used, or null (the host's state quoted, a byte limit, a wrong file).</summary>
    static string WorldAnswerProblem(WmvIpcClient.AssetResponse r, string what, int fileDataID, int maxBytes)
    {
        if (!r.ok)
        {
            string detail = !string.IsNullOrEmpty(r.detail) ? r.detail : (r.error ?? "");
            return string.Format("the host answered the {0} request (FileDataID {1}) \"{2}\": {3}", what, fileDataID,
                                 string.IsNullOrEmpty(r.state) ? "failed" : r.state, string.IsNullOrEmpty(detail) ? "no detail" : detail);
        }
        if (r.data == null)
            return string.Format("the {0} answer (FileDataID {1}) carries no bytes", what, fileDataID);
        if (r.fileDataID > 0 && r.fileDataID != fileDataID)
            return string.Format("the {0} answer names FileDataID {1}, the request {2}", what, r.fileDataID, fileDataID);
        if (maxBytes > 0 && r.data.Length > maxBytes)
            return string.Format("answer of {0} bytes exceeds {1} ({2}, FileDataID {3})", r.data.Length, maxBytes, what, fileDataID);
        return null;
    }

    void WorldAnswered(Job j, WmvIpcClient.AssetResponse r)
    {
        WorldWork w = j.World;
        w.Pending--;
        w.FilesAnswered++;
        w.LastAnswerMs = j.Clock.Elapsed.TotalMilliseconds;
        // Streaming: the world's timeout counts from its last answer (a tile waiting for the credit is not late).
        if (j.Stream != null)
            w.TimeoutStartedAtMs = double.NaN;
    }

    void OnWmoRoot(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        WorldWork w = j.World;
        WorldAnswered(j, r);
        Model m = q.Model;
        string why = WorldAnswerProblem(r, "world-model root", q.FileDataID, j.Cmd.MaxWmoFileBytes);
        if (why != null)
        {
            if (j.Stream != null && RefusedForTheLoad(r))
                FailForTheLoad(() => FailModel(j, m, why));
            else
                FailModel(j, m, why);
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        w.FileBytes += bytes.Length;
        m.Bytes += bytes.Length;
        w.Preparing++;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            WmoRoot root = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the world-model root bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else
            {
                try { root = WmoParser.ParseRoot(bytes, "world-model root " + m.FileDataID); }
                catch (Exception e) { error = "world-model root parse failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () => OnWmoRootParsed(j, m, root, ms, error);
        });
    }

    void OnWmoRootParsed(Job j, Model m, WmoRoot root, double ms, string error)
    {
        WorldWork w = j.World;
        if (w.TimedOut)
            return;            // settled by the world's timeout
        w.Preparing--;
        w.ParseCpuMs += ms;
        if (error == null && root.GroupCount <= 0)
            error = "the world-model root declares no groups";
        if (error != null)
        {
            FailModel(j, m, error);
            AdvanceWorld(j);
            return;
        }
        m.Root = root;
        m.GroupIds = root.Lod0GroupFileDataIDs;
        if (j.Cmd.MaxWmoGroupsPerLoad > 0 && w.GroupFilesReserved + m.GroupIds.Length > j.Cmd.MaxWmoGroupsPerLoad)
        {
            SettleModel(j, m, ModelState.NotRequested, string.Format("its {0} group file(s) would pass the load's budget of {1} " +
                                                                     "(maxWmoGroupsPerLoad; {2} taken by the world models before it)",
                                                                     m.GroupIds.Length, j.Cmd.MaxWmoGroupsPerLoad, w.GroupFilesReserved));
            AdvanceWorld(j);
            return;
        }
        w.GroupFilesReserved += m.GroupIds.Length;
        m.Groups = new WmoGroup[m.GroupIds.Length];
        m.GroupErrors = new string[m.GroupIds.Length];
        m.State = ModelState.Groups;
        Log("load {0}: world model {1}: root parsed in {2:0.###} ms on the worker: {3} LOD0 group(s), {4} material(s), {5} placement(s) of it",
            j.Cmd.Load, m.FileDataID, ms, m.GroupIds.Length, root.Materials.Length, m.Placements.Count);
        AdvanceWorld(j);
    }

    void OnWmoGroup(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        WorldWork w = j.World;
        WorldAnswered(j, r);
        Model m = q.Model;
        int gi = q.Group;
        string why = WorldAnswerProblem(r, "world-model group", q.FileDataID, j.Cmd.MaxWmoFileBytes);
        if (why != null)
        {
            if (RefusedForTheLoad(r))
            {
                uint gid = (uint)q.FileDataID;
                // A repeat of a group id this load asked for and was not refused: the host's once-per-load rule, which every load
                // listing both alike meets -- not a gap of this load's budget (counted apart: built without it and shared, as
                // before). A repeat of one refused for the load is a refusal for the load too (the host never recorded it).
                if (q.AskedBefore && !w.GroupIdsRefused.Contains(gid))
                {
                    m.GroupsRepeated++;
                    if (j.Stream != null && stream != null)
                        stream.GroupRepeats++;
                }
                else
                {
                    m.GroupsRefused++;
                    w.GroupIdsRefused.Add(gid);
                }
            }
            m.GroupErrors[gi] = why;
            GroupLanded(j, m);
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        w.FileBytes += bytes.Length;
        m.Bytes += bytes.Length;
        w.Preparing++;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            WmoGroup g = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the group bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else
            {
                try { g = WmoParser.ParseGroup(bytes, string.Format("world model {0} group {1} (FileDataID {2})", m.FileDataID, gi, q.FileDataID), gi); }
                catch (Exception e) { error = "group parse failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () =>
            {
                if (w.TimedOut)
                    return;
                w.Preparing--;
                w.ParseCpuMs += ms;
                m.Groups[gi] = g;
                m.GroupErrors[gi] = error;
                GroupLanded(j, m);
                AdvanceWorld(j);
            };
        });
    }

    /// <summary>A group file arrived, failed or finished parsing; the last one settles the model's textures.</summary>
    void GroupLanded(Job j, Model m)
    {
        WorldWork w = j.World;
        m.GroupsLanded++;
        if (m.GroupsLanded < m.GroupIds.Length || m.State != ModelState.Groups)
            return;
        int missing = 0;
        var reasons = new List<string>();
        for (int i = 0; i < m.Groups.Length; i++)
            if (m.Groups[i] == null)
            {
                missing++;
                reasons.Add("group " + i + " (FileDataID " + m.GroupIds[i] + "): " + (m.GroupErrors[i] ?? "missing"));
            }
        w.GroupFilesMissing += missing;
        if (missing == m.Groups.Length)
        {
            string none = "no group file could be read: " + string.Join("; ", reasons.ToArray());
            if (j.Stream != null && (m.GroupsRefused > 0 || m.GroupsRepeated > 0))
                FailForTheLoad(() => FailModel(j, m, none));
            else
                FailModel(j, m, none);
            return;
        }
        if (missing > 0)
        {
            m.Reason = missing + " of " + m.Groups.Length + " group file(s) missing: " + string.Join("; ", reasons.ToArray());
            Log("load {0}: world model {1}: {2}", j.Cmd.Load, m.FileDataID, m.Reason);
        }
        // The textures its drawn materials sample (the Buildings decode policy), each fetched once per load.
        HashSet<uint> sampled = WorldModelPlan.SampledTextures(m.Root, m.Groups);
        int cap = j.Cmd.MaxWmoTexturesPerLoad > 0 ? j.Cmd.MaxWmoTexturesPerLoad : int.MaxValue;
        foreach (uint id in sampled)
        {
            m.TextureIds.Add(id);
            SharedTexture t;
            if (!w.Textures.TryGetValue(id, out t))
            {
                t = new SharedTexture { Id = id, Texture = new WmvWmoTexture { FileDataID = id } };
                w.Textures[id] = t;
                if (w.TexturesAsked + w.TextureQueue.Count >= cap)
                {
                    // Streaming: what the session has settled of it (its image, or its file's failure) is taken without asking;
                    // else it is not requested -- for this load only (ForTheLoad).
                    if (!TakeSessionWmoImage(j, t))
                    {
                        t.Texture.Error = "not requested: this load asks for at most " + cap + " world-model textures (maxWmoTexturesPerLoad)";
                        t.Done = true;
                        t.ForTheLoad = j.Stream != null;
                        w.TexturesNotRequested++;
                    }
                }
                else if (id > int.MaxValue)
                {
                    t.Texture.Error = "not requested: FileDataID " + id + " is beyond what a request can carry";
                    t.Done = true;
                    w.TexturesNotRequested++;
                }
                else if (ClaimWmoTexture(j, t))
                {
                    // Streaming: the session's image attached, its failure taken, or another tile's fetch waited for.
                }
                else
                {
                    t.Asked = true;
                    w.TextureQueue.Enqueue(id);
                }
            }
            t.Users++;
            if (!t.Done)
            {
                m.TexturesWaiting.Add(id);
                t.Waiting.Add(m);
            }
            else if (t.Texture.Decoded && t.Texture.Image == null && t.Raw == null && ReattachWmoImage(j, t))
            {
                // Streaming: its pixels went with an earlier model of this job; the session keeps its image.
            }
            else if (t.Texture.Decoded && t.Texture.Image == null && t.Raw != null)
            {
                // Its pixels went with the last model built before this one sampled it: decoded again from its bytes, in
                // turn (AdvanceWorld).
                m.TexturesWaiting.Add(id);
                t.Waiting.Add(m);
                t.Decoding = true;
                t.Done = false;
                w.RedecodeQueue.Enqueue(t);
            }
        }
        m.State = ModelState.Textures;
        Log("load {0}: world model {1}: groups in ({2} missing); its drawn materials sample {3} texture(s), {4} still to come", j.Cmd.Load,
            m.FileDataID, missing, sampled.Count, m.TexturesWaiting.Count);
        if (m.TexturesWaiting.Count == 0)
            ModelReady(j, m);
    }

    void OnWmoTexture(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        WorldWork w = j.World;
        WorldAnswered(j, r);
        uint id = (uint)q.FileDataID;
        SharedTexture t;
        if (!w.Textures.TryGetValue(id, out t))
            throw new InvalidOperationException("world-model texture " + id + " was asked for but is not in the load's list");
        int limit = j.Cmd.MaxWmoTextureBytes;
        string why = WorldAnswerProblem(r, "world-model texture", q.FileDataID, limit);
        if (why != null)
        {
            t.Texture.Error = why;
            // Streaming: refused for this tile's load, or stale -- this load's gap, not the file's: a prototype built without it
            // is the tile's own (BuildModel, KeepWorldModelForTheTile); the image's fetch is handed on below.
            if (j.Stream != null && RefusedForTheLoad(r))
                t.ForTheLoad = true;
            TextureDone(j, t);
            // (A refusal for this tile's load, or a stale answer, hands the fetch on; the file's own failure -- not in storage,
            // over its per-file limit, no bytes -- is the session's: every tile takes it without asking.)
            PublishWmoImage(j, t, null, why, !RefusedForTheLoad(r));
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        w.TextureBytes += bytes.Length;
        w.Preparing++;
        WmvWmoTexture tex = t.Texture;
        t.Raw = bytes;
        WmvTerrainFormatSupport support = j.Support;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            BlpImage img = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the texture bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else
            {
                // Stage 08: its own DXT blocks when it qualifies, else decoded (BlpBlocks.PrepareForModels).
                try { img = BlpBlocks.PrepareForModels(bytes, support.Supports); }
                catch (Exception e) { error = "decode failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () =>
            {
                if (w.TimedOut)
                    return;
                w.Preparing--;
                w.DecodeCpuMs += ms;
                tex.DecodeMs = ms;
                tex.Image = img;
                tex.Error = error;
                tex.Decoded = img != null;
                if (img == null)
                    t.Raw = null;
                if (img != null)
                {
                    tex.Width = img.Width;
                    tex.Height = img.Height;
                    tex.Encoding = img.Encoding;
                    w.PixelBytesHeld += img.HeldBytes;
                    w.PixelBytesHeldMax = Math.Max(w.PixelBytesHeldMax, w.PixelBytesHeld);
                    if (img.Blocks != null) w.TexturesAsBlocks++;
                    else w.TexturesDecodedRgba++;
                }
                TextureDone(j, t);
                PublishWmoImage(j, t, img, error, true);
                AdvanceWorld(j);
            };
        });
    }

    /// <summary>A texture whose pixels went is decoded again from its file's bytes (checked when they came) on the worker
    /// (queued by the model that needs it: RedecodeQueue).</summary>
    void Redecode(Job j, SharedTexture t)
    {
        WorldWork w = j.World;
        w.Preparing++;
        w.TexturesRedecoded++;
        byte[] bytes = t.Raw;
        WmvWmoTexture tex = t.Texture;
        WmvTerrainFormatSupport support = j.Support;
        RunWorker(j, () =>
        {
            var sw = Stopwatch.StartNew();
            BlpImage img = null;
            string error = null;
            try { img = BlpBlocks.PrepareForModels(bytes, support.Supports); }
            catch (Exception e) { error = "decode failed: " + e.Message; }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () =>
            {
                if (w.TimedOut)
                    return;
                w.Preparing--;
                w.DecodeCpuMs += ms;
                t.Decoding = false;
                tex.Image = img;
                if (img != null)
                {
                    w.PixelBytesHeld += img.HeldBytes;
                    w.PixelBytesHeldMax = Math.Max(w.PixelBytesHeldMax, w.PixelBytesHeld);
                }
                else
                {
                    tex.Error = error;
                    tex.Decoded = false;
                }
                TextureDone(j, t, false);
                AdvanceWorld(j);
            };
        });
    }

    void TextureDone(Job j, SharedTexture t, bool count = true)
    {
        WorldWork w = j.World;
        t.Done = true;
        if (count)
        {
            if (t.Texture.Decoded) w.TexturesDecoded++;
            else w.TexturesMissing++;
        }
        foreach (Model m in t.Waiting)
        {
            m.TexturesWaiting.Remove(t.Id);
            if (m.TexturesWaiting.Count == 0 && m.State == ModelState.Textures)
                ModelReady(j, m);
        }
        t.Waiting.Clear();
        // Every model that wanted it settled without it meanwhile (failed): its pixels are not kept for nobody.
        if (t.Users <= 0 && t.Texture.Image != null)
            ReleasePixels(w, t);
    }

    void ModelReady(Job j, Model m)
    {
        m.State = ModelState.Ready;
        j.World.BuildQueue.Enqueue(m);
    }

    void FailModel(Job j, Model m, string reason)
    {
        SettleModel(j, m, ModelState.Failed, reason);
    }

    /// <summary>A model ends without a prototype: failed or not requested, its placements with it, its textures released.</summary>
    void SettleModel(Job j, Model m, ModelState state, string reason)
    {
        if (m.Shared != null)
            WorldModelSettled(j, m, state, reason);
        WorldWork w = j.World;
        m.State = state;
        m.Reason = reason;
        m.Active = false;
        foreach (PlacementWork pw in m.Placements)
        {
            pw.State = state == ModelState.Failed ? "failed" : "notRequested";
            pw.Reason = reason;
        }
        ReleaseModelTextures(w, m);
        m.Root = null;
        m.Groups = new WmoGroup[0];
        Log("load {0}: world model {1} {2}: {3} ({4} placement(s))", j.Cmd.Load, m.FileDataID, ModelStateName(state), reason, m.Placements.Count);
    }

    static void ReleaseModelTextures(WorldWork w, Model m)
    {
        foreach (uint id in m.TextureIds)
        {
            SharedTexture t;
            if (!w.Textures.TryGetValue(id, out t))
                continue;
            t.Waiting.Remove(m);
            if (--t.Users <= 0)
                ReleasePixels(w, t);
        }
        m.TextureIds.Clear();
        m.TexturesWaiting.Clear();
    }

    static void ReleasePixels(WorldWork w, SharedTexture t)
    {
        BlpImage img = t.Texture.Image;
        if (img != null)
            w.PixelBytesHeld -= img.HeldBytes;
        t.Texture.ReleasePixels();
    }

    // ---------------------------------------------------------------- frames: building and placing

    /// <summary>
    /// One frame of a world load's world models: one ready model built and its placements placed (the main thread's cost is
    /// one prototype a frame), the world's timeout, worldProgress. False when the load ended here (failed or reported).
    /// </summary>
    bool PumpWorld(Job j, double nowMs)
    {
        WorldWork w = j.World;
        if (!w.Global && !w.Committed)
            return true;
        if (double.IsNaN(w.TimeoutStartedAtMs))
            w.TimeoutStartedAtMs = nowMs;
        if (!w.Done && nowMs - w.TimeoutStartedAtMs > WorldTimeoutMs)
        {
            TimeOutWorld(j);
            if (!Live(j))
                return false;
        }
        // Streaming: one model a frame within the session's frame budget (the nearest tiles first); one built from its files
        // (not placed from the session's prototype) only as the frame's first item.
        if (w.BuildQueue.Count > 0 &&
            (j.Stream == null || (w.BuildQueue.Peek().Built != null && w.BuildQueue.Peek().Shared != null ? StreamMayWork() : StreamMayWorkHeavy())))
        {
            Model m = w.BuildQueue.Dequeue();
            BuildModel(j, m);
            StreamWorked();
            if (!Live(j))
                return false;
        }
        AdvanceWorld(j);
        if (!Live(j))
            return false;
        // The doodads, once the world models are settled (WmvTerrainSessionDoodads.cs).
        if (w.Done && w.Doodads != null && w.Doodads.Started && !w.Doodads.Done)
        {
            PumpDoodads(j, nowMs);
            if (!Live(j))
                return false;
        }
        SendProgress(j, false);
        return true;
    }

    void BuildModel(Job j, Model m)
    {
        WorldWork w = j.World;
        if (m.Built != null && m.Shared != null)
        {
            // Streaming: the session's prototype (built for another tile, or pooled): placed from, never built again.
            m.State = ModelState.Built;
            m.Active = false;
            PlaceAll(j, m, m.Built);
            // Protocol 19: the frames of its liquid types the scene does not hold yet (WmvTerrainSessionLiquids.cs).
            SharedWorldModelLiquids(j, m.Built);
            if (!m.Unpinned)
            {
                m.Unpinned = true;
                Unpin(m.Shared);
            }
            Log("load {0}: world model {1}: the session's prototype placed from ({2} placement(s) of this tile)", j.Cmd.Load, m.FileDataID,
                m.Placements.Count);
            CheckWorldDone(j);
            return;
        }
        var textures = new Dictionary<uint, WmvWmoTexture>();
        var forTheLoad = new List<string>();     // streaming: what of it this tile's load could not get
        foreach (uint id in m.TextureIds)
        {
            SharedTexture t;
            if (!w.Textures.TryGetValue(id, out t))
                continue;
            textures[id] = t.Texture;
            if (t.ForTheLoad && !t.Texture.Decoded)
                forTheLoad.Add("texture " + id);
        }
        if (m.GroupsRefused > 0)                 // (refused or stale; every group missing never gets here: GroupLanded)
            forTheLoad.Add(m.GroupsRefused + " group file(s)");
        bool keep = forTheLoad.Count > 0 && m.Shared != null && j.Stream != null && stream != null;
        var clock = Stopwatch.StartNew();
        WmvWorldPrototype proto;
        try { proto = worldView.BuildPrototype(m.FileDataID, m.Root, m.Groups, textures); }
        catch (Exception e)
        {
            FailModel(j, m, "building the world model failed: " + e.GetType().Name + ": " + e.Message);
            if (w.Global)
                FailGlobal(j, m.Reason);
            return;
        }
        // Protocol 19: its groups' liquid (MLIQ), on this path only -- before any instance is made from it.
        AddWorldModelLiquids(j, m, proto);
        double buildMs = clock.Elapsed.TotalMilliseconds;
        w.BuildMs += buildMs;
        w.BuildMaxMs = Math.Max(w.BuildMaxMs, buildMs);
        m.Built = proto;
        m.State = ModelState.Built;
        m.Active = false;
        ReleaseModelTextures(w, m);
        if (w.Global && !w.Committed)
        {
            if (!CommitGlobal(j, m, proto))
                return;
        }
        else
        {
            if (keep)
                KeepWorldModelForTheTile(j, m, proto, "built without " + string.Join(", ", forTheLoad.ToArray()) + ", which tile " +
                                                      j.Stream.Name + "'s load could not get");
            else
            {
                world.Prototypes[m.FileDataID] = proto;
                if (m.Shared != null)
                    WorldPrototypeBuilt(m, proto);
            }
            PlaceAll(j, m, proto);              // (kept: its placements register the private record)
            if (m.Shared != null && !m.Unpinned)
            {
                m.Unpinned = true;
                Unpin(m.Shared);                // (kept: the private record's pin -- nothing places from it after this)
            }
        }
        Log("load {0}: world model {1} built in {2:0.###} ms ({3} group(s), {4} renderer(s), {5} KB geometry, {6} KB textures); {7} placement(s) placed; " +
            "live world instances {8}, prototypes {9}", j.Cmd.Load, m.FileDataID, buildMs, proto.Runtime.GroupCount, proto.Runtime.Renderers,
            proto.Runtime.GpuGeometryBytes / 1024, proto.Runtime.GpuTextureBytes / 1024, m.Placements.Count, WmvWorldModels.LiveInstances,
            WmvWorldModels.LivePrototypes);
        CheckWorldDone(j);
    }

    void PlaceAll(Job j, Model m, WmvWorldPrototype proto)
    {
        WorldWork w = j.World;
        var clock = Stopwatch.StartNew();
        foreach (PlacementWork pw in m.Placements)
        {
            // Streaming: a placement another tile of the session placed is found, never placed twice -- one placed from a tile's
            // private prototype is replaced by this one when this one is complete (WmvTerrainSessionStream.cs).
            PlacementEntry replace = null;
            if (j.Stream != null && stream != null && FindSharedPlacement(j, pw, !IsPrivate(m.Shared), out replace))
                continue;
            try
            {
                pw.Instance = worldView.PlaceInstance(proto, pw.P, world.Origin, j.Cmd.Load);
                world.Instances.Add(pw.Instance);
                var geometry = new WorldModelGeometry(pw.P, proto.Root, proto.Groups);
                world.Geometries.Add(geometry);
                world.AddBox(pw.P.ExtentsMin, pw.P.ExtentsMax);
                pw.State = "placed";
                if (j.Stream != null && stream != null)
                    RegisterPlacement(j, pw, geometry, m.Shared, replace);
            }
            catch (Exception e)
            {
                pw.State = "failed";
                pw.Reason = "placing it failed: " + e.GetType().Name + ": " + e.Message;
                Log("load {0}: placement {1}: {2}", j.Cmd.Load, pw.P.UniqueId, pw.Reason);
            }
        }
        w.PlaceMs += clock.Elapsed.TotalMilliseconds;
        w.LastCommitFrame = frame;
    }

    /// <summary>A WMO-only map's world model is built: THE COMMIT -- every tile and world model of the scene disposed, the
    /// new scene (no tile; its origin the placement's position), the instance, the world view entered with the camera,
    /// worldShown. A placement that throws before anything is shown fails the load and leaves the scene as it was.</summary>
    bool CommitGlobal(Job j, Model m, WmvWorldPrototype proto)
    {
        WorldWork w = j.World;
        WorldModelPlacement first = m.Placements[0].P;
        var scene = new WmvWorldScene
        {
            Load = j.Cmd.Load, MapId = j.Cmd.MapId, Epoch = j.Cmd.AssetEpoch, Kind = "globalWmo",
            Origin = new WorldOrigin(first.Position.X, first.Position.Y, first.Position.Z),
        };
        // The instances first, into the new scene, before the old one goes: a placement that throws leaves the old scene.
        var instances = new List<WmvWorldInstance>();
        try
        {
            foreach (PlacementWork pw in m.Placements)
                instances.Add(worldView.PlaceInstance(proto, pw.P, scene.Origin, j.Cmd.Load));
        }
        catch (Exception e)
        {
            foreach (WmvWorldInstance i in instances)
                worldView.DisposeInstance(i);
            worldView.DisposePrototype(proto);
            m.Built = null;
            FailModel(j, m, "placing the world model failed: " + e.GetType().Name + ": " + e.Message);
            FailGlobal(j, m.Reason);
            return false;
        }
        int replaced = DisposeTiles(null);
        shown = null;
        shownLoad = 0;
        ForgetShownInputs();
        world = scene;
        world.Prototypes[m.FileDataID] = proto;
        // Protocol 19: the load's liquid registry (its world model's liquid types) and zone lights are the scene's.
        sceneLiquids = j.Liquid != null ? j.Liquid.Registry : null;
        CommitSceneLights(j);
        for (int k = 0; k < m.Placements.Count; k++)
        {
            PlacementWork pw = m.Placements[k];
            pw.Instance = instances[k];
            pw.State = "placed";
            world.Instances.Add(pw.Instance);
            world.Geometries.Add(new WorldModelGeometry(pw.P, proto.Root, proto.Groups));
            world.AddBox(pw.P.ExtentsMin, pw.P.ExtentsMax);
        }
        int fromEpoch = sceneEpoch, fromMap = sceneMapId;
        sceneEpoch = j.Cmd.AssetEpoch;
        sceneMapId = j.Cmd.MapId;
        w.Committed = true;
        w.LastCommitFrame = frame;
        // The start point inside the world model (the camera's when its load names no target), from its own geometry.
        WorldPoint start;
        double yaw;
        int group;
        string why;
        if (world.Geometries.Count > 0 && world.Geometries[0].StartPoint(2.0, out start, out yaw, out group, out why))
        {
            w.HasStart = true;
            w.Start = start;
            w.StartReason = why;
        }
        else
            w.StartReason = world.Geometries.Count > 0 ? "no start point: " + "the geometry offers no floor with headroom" : "no geometry";
        Log("load {0}: committed the world model {1} of map {2} (no tile; {3} tile(s) replaced{4}); origin {5}; box {6} .. {7}; start {8}",
            j.Cmd.Load, m.FileDataID, j.Cmd.MapId, replaced,
            fromEpoch == int.MinValue ? ", a new scene" : (fromEpoch != sceneEpoch || fromMap != sceneMapId
                ? string.Format(", the scene reset: epoch {0} -> {1}, map {2} -> {3}", fromEpoch, sceneEpoch, fromMap, sceneMapId) : ""),
            world.Origin, world.BoxMin, world.BoxMax, w.HasStart ? w.Start + " (" + w.StartReason + ")" : w.StartReason);
        viewActive = true;
        try { worldView.ShowWorld(world); }
        catch (Exception e)
        {
            j.AdoptionNote = "committed, but showing the world model failed: " + e.GetType().Name + ": " + e.Message;
            Log("load {0}: {1}", j.Cmd.Load, j.AdoptionNote);
        }
        w.ShownMs = j.Clock.Elapsed.TotalMilliseconds;
        SendShown(j, "globalWmo");
        return true;
    }

    /// <summary>The start point of the scene's global world model (its load's), when one was found, and its heading (degrees,
    /// the free camera's yaw: along the start room's longer side).</summary>
    public bool WorldStart(out WorldPoint start, out double yaw, out string reason)
    {
        start = new WorldPoint();
        yaw = 0.0;
        reason = "no world model is on screen";
        if (world == null || world.Geometries.Count == 0)
            return false;
        int group;
        return world.Geometries[0].StartPoint(2.0, out start, out yaw, out group, out reason);
    }

    void FailGlobal(Job j, string reason)
    {
        if (j.World.Committed)
            return;            // shown already: the report says what failed
        Fail(j, "the map's world model could not be built: " + reason);
    }

    void TimeOutWorld(Job j)
    {
        WorldWork w = j.World;
        string why = "timed out after " + (WorldTimeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                     " s of the world models (" + WorldWaitingFor(j) + ")";
        // Its requests are retired (an answer to one is late), its worker results dropped as they come (the model is settled).
        var keep = new List<Request>();
        foreach (Request q in j.Pending)
        {
            if (q.World)
                RetireRequest(j, q, "the world models timed out");
            else
                keep.Add(q);
        }
        j.Pending.Clear();
        j.Pending.AddRange(keep);
        w.Pending = 0;
        w.TextureQueue.Clear();
        w.RedecodeQueue.Clear();
        w.BuildQueue.Clear();
        bool wasTransient = streamTransientFailure;
        streamTransientFailure = true;          // (streaming: a fetch that timed out is handed on, not failed for every tile)
        try
        {
            foreach (Model m in w.Models)
                if (m.State != ModelState.Built && m.State != ModelState.Failed && m.State != ModelState.NotRequested)
                    FailModel(j, m, why);
        }
        finally { streamTransientFailure = wasTransient; }
        // No plan yet (the obj0 not answered or parsed): there is none now, said, so the load can be reported.
        if (!w.PlanReady)
        {
            w.PlanReady = true;
            w.PlanProblem = "no world model is placed: " + why;
        }
        // Worker results still out are dropped when they come (TimedOut): the models are settled, Preparing no longer waited for.
        w.TimedOut = true;
        w.Preparing = 0;
        if (j.Stream != null)
            ReleaseJobImages(j);
        Log("load {0}: world models {1}", j.Cmd.Load, why);
        if (w.Global && !w.Committed)
        {
            Fail(j, "the map's world model " + why);
            return;
        }
        CheckWorldDone(j);
    }

    void CheckWorldDone(Job j)
    {
        WorldWork w = j.World;
        if (w == null || w.Done || !w.PlanReady)
            return;
        if (!w.Global && !w.Committed)
            return;
        if (w.Out > 0 || w.BuildQueue.Count > 0 || w.TextureQueue.Count > 0)
            return;
        foreach (Model m in w.Models)
            if (m.State != ModelState.Built && m.State != ModelState.Failed && m.State != ModelState.NotRequested)
                return;
        if (w.Global && !w.Committed)
        {
            FailGlobal(j, w.Models.Count > 0 ? w.Models[0].Reason : "no world model");
            return;
        }
        w.Done = true;
        if (w.LastCommitFrame == 0)
            w.LastCommitFrame = frame;
        Log("load {0}: the world models are settled ({1}); decoded texture pixels held at most {2:0.0} MB, {3:0.0} MB now; " +
            "{4} texture(s) decoded, {5} decoded again for a later world model{6}",
            j.Cmd.Load, WorldStateName(w), w.PixelBytesHeldMax / 1048576.0, w.PixelBytesHeld / 1048576.0,
            w.TexturesDecoded, w.TexturesRedecoded,
            w.Doodads != null && w.Doodads.Offered ? "; the doodads begin" : "; reported after frame " + (w.LastCommitFrame + 2));
        // The camera's moment, then the doodads (WmvTerrainSessionDoodads.cs); the report waits for them.
        StartDoodads(j);
        if (!Live(j))
            return;
        SendProgress(j, true);
    }

    /// <summary>A WMO-only map's world load: reported built once its scene has been on screen two frames (FinishBuilt's
    /// counterpart for a scene with no tile).</summary>
    void FinishWorld(Job j)
    {
        j.FirstFrameMs = j.Clock.Elapsed.TotalMilliseconds - j.World.ShownMs;
        current = null;
        if (world == null || world.Load != j.Cmd.Load)
        {
            Report(j, "superseded", "the world model was replaced before its second frame");
            return;
        }
        string reason = j.AdoptionNote ?? "";
        try { worldView.WorldBuilt(world); }
        catch (Exception e)
        {
            string what = "the after-build step (capture) failed: " + e.GetType().Name + ": " + e.Message;
            Log("load {0}: {1}", j.Cmd.Load, what);
            reason = reason.Length > 0 ? reason + "; " + what : what;
        }
        Report(j, "built", reason);
    }

    /// <summary>The load ended (any report, the timeout, Dispose): the world's queues go, every texture's pixels with them.</summary>
    void EndWorld(Job j, string ended)
    {
        WorldWork w = j.World;
        w.Pending = 0;
        w.TextureQueue.Clear();
        w.RedecodeQueue.Clear();
        w.BuildQueue.Clear();
        foreach (SharedTexture t in w.Textures.Values)
        {
            ReleasePixels(w, t);
            t.Raw = null;
        }
        foreach (PlacementWork pw in w.Placements)
            if (pw.State == "planned")
            {
                pw.State = "ended";
                pw.Reason = "the load ended (" + ended + ") before it was placed";
            }
        foreach (Model m in w.Models)
        {
            m.Root = null;
            m.Groups = new WmoGroup[0];
        }
        EndDoodads(j, ended);
        // Streaming: its fetches handed on, its waits and pins let go (WmvTerrainSessionStream.cs).
        if (j.Stream != null)
            ReleaseJobModels(j);
    }

    // ---------------------------------------------------------------- progress and the report

    static string WorldStateName(WorldWork w)
    {
        int placed = 0, failed = 0, other = 0, ended = 0;
        foreach (PlacementWork pw in w.Placements)
        {
            if (pw.State == "placed") placed++;
            else if (pw.State == "failed") failed++;
            else if (pw.State == "ended" || pw.State == "planned") ended++;
            else other++;
        }
        if (ended > 0)
            return "ended";
        if (w.Placements.Count == 0)
            return w.PlanProblem != null ? "failed" : "none";
        if (placed == w.Placements.Count)
            return "complete";
        if (placed == 0)
            return "failed";
        return "partial";
    }

    string WorldPhase(WorldWork w)
    {
        if (w.Done)
            return WorldStateName(w);
        if (!w.PlanReady)
            return "obj0";
        if (w.BuildQueue.Count > 0)
            return "building";
        if (w.Preparing > 0)
            return "parsing";
        if (w.Pending > 0)
            return "requesting";
        return "placing";
    }

    void SendProgress(Job j, bool force)
    {
        WorldWork w = j.World;
        if (worldView == null)
            return;
        // Streaming: the entry's progress alone (the other tiles say theirs in streamState).
        if (j.Stream != null && !j.Stream.Entry)
            return;
        double now = j.Clock.Elapsed.TotalMilliseconds;
        string phase = WorldPhase(w), doodadPhase = DoodadPhase(w.Doodads);
        // A phase change goes out at once -- the doodads' alone at most every DoodadPhaseIntervalMs (it can change every
        // frame) -- and otherwise at most every ProgressIntervalMs.
        if (!force && w.LastProgressMs >= 0)
        {
            double since = now - w.LastProgressMs;
            bool doodadChange = doodadPhase != w.LastDoodadPhase && since >= DoodadPhaseIntervalMs;
            if (phase == w.LastPhase && !doodadChange && since < ProgressIntervalMs)
                return;
        }
        w.LastPhase = phase;
        w.LastDoodadPhase = doodadPhase;
        w.LastProgressMs = now;
        var r = new WmvIpcClient.WorldProgressReport
        {
            Load = j.Cmd.Load, Phase = phase, Placements = w.Placements.Count, Models = w.Models.Count, ModelsRequested = w.ModelsAsked,
            FilesAsked = w.FilesAsked, FilesAnswered = w.FilesAnswered, BytesReceived = w.FileBytes + w.TextureBytes, ElapsedMs = now,
        };
        foreach (Model m in w.Models)
        {
            if (m.State == ModelState.Built) r.ModelsBuilt++;
            else if (m.State == ModelState.Failed) r.ModelsFailed++;
            else if (m.State == ModelState.NotRequested) r.ModelsNotRequested++;
            if (m.State >= ModelState.Groups && m.State != ModelState.NotRequested && m.State != ModelState.Borrowed) r.ModelsParsed++;
        }
        foreach (PlacementWork pw in w.Placements)
        {
            if (pw.State == "placed") r.Placed++;
            else if (pw.State == "failed") r.PlacementsFailed++;
            else if (pw.State == "unsupported") r.ModelsUnsupported++;
        }
        FillDoodadProgress(w, r);
        try { worldView.ReportWorldProgress(r); }
        catch (Exception e) { Log("load {0}: sending worldProgress failed: {1}", j.Cmd.Load, e.Message); }
    }

    static string WorldWaitingFor(Job j)
    {
        WorldWork w = j.World;
        if (!w.PlanReady)
            return "the obj0 ADT";
        var sb = new StringBuilder();
        int waiting = 0;
        foreach (Model m in w.Models)
            if (m.State != ModelState.Built && m.State != ModelState.Failed && m.State != ModelState.NotRequested)
            {
                if (waiting++ < 4)
                    sb.Append(sb.Length > 0 ? ", " : "").Append("world model ").Append(m.FileDataID).Append(" (").Append(ModelStateName(m.State))
                      .Append(m.State == ModelState.Groups ? string.Format(", {0} of {1} group(s) in", m.GroupsLanded, m.GroupIds.Length)
                              : m.State == ModelState.Textures ? string.Format(", {0} texture(s) to come", m.TexturesWaiting.Count) : "").Append(')');
            }
        return string.Format("{0} world model(s) not built{1}; {2} request(s) out, {3} on the worker", waiting,
                             sb.Length > 0 ? ": " + sb + (waiting > 4 ? ", ..." : "") : "", w.Pending, w.Preparing);
    }

    void FillWorldReport(Job j, WmvIpcClient.TerrainTileReport r, string status)
    {
        WorldWork w = j.World;
        r.World = true;
        r.WorldKind = w.Global ? "globalWmo" : "tile";
        r.WorldState = WorldStateName(w);
        r.WorldReason = w.PlanProblem ?? (w.Failure ?? "");
        r.WorldRecords = w.Records;
        r.WorldPlacements = w.Placements.Count;
        foreach (PlacementWork pw in w.Placements)
        {
            switch (pw.State)
            {
                case "placed": r.WorldPlaced++; break;
                case "failed": r.WorldFailed++; break;
                case "unsupported": r.WorldUnsupported++; break;
                case "notRequested": r.WorldNotRequested++; break;
                default: r.WorldEnded++; break;
            }
        }
        r.WorldModels = w.Models.Count;
        foreach (Model m in w.Models)
        {
            if (m.State == ModelState.Built) r.WorldModelsBuilt++;
            else if (m.State == ModelState.Failed) r.WorldModelsFailed++;
            else if (m.State == ModelState.NotRequested) r.WorldModelsNotRequested++;
            if (m.Built != null && !m.Built.Disposed && m.Built.Runtime != null)
            {
                r.WorldGpuGeometryBytes += m.Built.Runtime.GpuGeometryBytes;
                r.WorldGpuTextureBytes += m.Built.Runtime.GpuTextureBytes;
            }
        }
        r.WorldGroupFiles = w.GroupFilesAsked;
        r.WorldGroupFilesMissing = w.GroupFilesMissing;
        r.WorldTextures = w.Textures.Count;
        r.WorldTexturesDecoded = w.TexturesDecoded;
        r.WorldTexturesMissing = w.TexturesMissing + w.TexturesNotRequested;
        r.WorldFileBytes = w.FileBytes;
        r.WorldTextureBytes = w.TextureBytes;
        r.WorldShownMs = w.ShownMs;
        r.WorldFetchMs = w.BeganMs >= 0 && w.LastAnswerMs >= w.BeganMs ? w.LastAnswerMs - w.BeganMs : -1;
        r.WorldParseMs = w.ParseCpuMs;
        r.WorldDecodeMs = w.DecodeCpuMs;
        r.WorldBuildMs = w.BuildMs;
        r.WorldBuildMaxMs = w.BuildMaxMs;
        r.WorldPlaceMs = w.PlaceMs;
        WorldOrigin o = OwnsScene(j) ? world.Origin : (j.Mesh != null ? j.Mesh.Origin : new WorldOrigin(0, 0, 0));
        r.WorldOrigin = new[] { o.X, o.Y, o.Z };
        r.WorldStart = w.HasStart;
        r.WorldStartPoint = new[] { w.Start.X, w.Start.Y, w.Start.Z };
        r.WorldStartReason = w.StartReason ?? "";
        if (w.Global)
        {
            r.OriginX = o.X;
            r.OriginY = o.Y;
            r.OriginZ = o.Z;
        }
        int listed = 0;
        foreach (PlacementWork pw in w.Placements)
        {
            if (listed++ >= ReportPlacementsMax)
                break;
            WorldModelPlacement p = pw.P;
            var e = new WmvIpcClient.WorldPlacementReport
            {
                UniqueId = p.UniqueId, FileDataID = (int)p.FileDataID, Records = p.Records, State = pw.State, Reason = pw.Reason ?? "",
                Position = new[] { p.Position.X, p.Position.Y, p.Position.Z },
                Rotation = new[] { p.Record.Rotation.A, p.Record.Rotation.B, p.Record.Rotation.C }, Scale = p.Scale,
                Extents = new[] { p.ExtentsMin.X, p.ExtentsMin.Y, p.ExtentsMin.Z, p.ExtentsMax.X, p.ExtentsMax.Y, p.ExtentsMax.Z },
            };
            WmvWorldInstance inst = pw.Instance;
            if (inst != null && !inst.Disposed && inst.HasBounds && world != null)
            {
                e.HasRendered = true;
                e.Rendered = WmvWorldModels.RenderedWorldBox(inst, world.Origin);
                e.Groups = inst.Prototype != null && inst.Prototype.Runtime != null ? inst.Prototype.Runtime.GroupCount : 0;
                e.GroupsDrawn = inst.GroupRenderers;          // (a group's liquid renderer is not a group)
            }
            r.WorldPlacementList.Add(e);
        }
        // Protocol 17: the doodads, and the whole world's assembly.
        if (w.Doodads != null)
        {
            FillDoodadReport(j, r);
            r.WorldAssembly = AssemblyState(r.WorldState, r.DoodadState, r.WorldPlaced + r.DoodadPlaced > 0);
        }
    }

    void LogWorld(int load, WmvIpcClient.TerrainTileReport r)
    {
        Log("load {0}: world ({1}) {2}{3}: {4} record(s), {5} placement(s): {6} placed, {7} failed, {8} unsupported, {9} not requested, {10} ended; " +
            "{11} world model(s): {12} built, {13} failed, {14} not requested; {15} group file(s) ({16} missing), {17} texture(s) ({18} decoded, " +
            "{19} missing); {20} file bytes, {21} texture bytes; GPU {22} KB geometry, {23} KB textures; timings shown {24:0.#}, fetch {25:0.#}, " +
            "parse {26:0.#} (worker), decode {27:0.#} (worker), build {28:0.#} (longest {29:0.#}), place {30:0.###} ms",
            load, r.WorldKind, r.WorldState, string.IsNullOrEmpty(r.WorldReason) ? "" : " (" + r.WorldReason + ")", r.WorldRecords,
            r.WorldPlacements, r.WorldPlaced, r.WorldFailed, r.WorldUnsupported, r.WorldNotRequested, r.WorldEnded, r.WorldModels,
            r.WorldModelsBuilt, r.WorldModelsFailed, r.WorldModelsNotRequested, r.WorldGroupFiles, r.WorldGroupFilesMissing, r.WorldTextures,
            r.WorldTexturesDecoded, r.WorldTexturesMissing, r.WorldFileBytes, r.WorldTextureBytes, r.WorldGpuGeometryBytes / 1024,
            r.WorldGpuTextureBytes / 1024, r.WorldShownMs, r.WorldFetchMs, r.WorldParseMs, r.WorldDecodeMs, r.WorldBuildMs, r.WorldBuildMaxMs,
            r.WorldPlaceMs);
        foreach (WmvIpcClient.WorldPlacementReport p in r.WorldPlacementList)
            Log("load {0}:   uniqueId {1} WMO {2}: {3}{4}; extents ({5:0.##}, {6:0.##}, {7:0.##})..({8:0.##}, {9:0.##}, {10:0.##}){11}", load, p.UniqueId,
                p.FileDataID, p.State, string.IsNullOrEmpty(p.Reason) ? "" : " (" + p.Reason + ")", p.Extents[0], p.Extents[1], p.Extents[2],
                p.Extents[3], p.Extents[4], p.Extents[5],
                p.HasRendered ? string.Format(System.Globalization.CultureInfo.InvariantCulture, ", rendered ({0:0.##}, {1:0.##}, {2:0.##})..({3:0.##}, {4:0.##}, {5:0.##}), {6} group renderer(s)",
                                              p.Rendered[0], p.Rendered[1], p.Rendered[2], p.Rendered[3], p.Rendered[4], p.Rendered[5], p.GroupsDrawn) : "");
        if (r.HasDoodads)
            LogDoodads(load, r);
    }
}
