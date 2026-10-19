// WmvTerrainSessionStream.cs
//
// STREAMING (protocol 18, docs/world-viewer/08-streaming.md 1.8): the part of WmvTerrainSession that keeps the tiles around
// the camera loaded. A loadWorld that carries "streaming" is a session's ENTRY: its tile is loaded as any world load's (WDT ->
// root, tex0, obj0 -> mesh, material, textures -> THE COMMIT -> world models -> doodads -> terrainTileLoaded), and once its
// tile is committed the REGION follows the free camera:
//
//   region     each frame the camera's tile (the entry's while the free camera is not flying); the tiles within Radius of it
//              that the session's WDT has are WANTED, a tile is kept while within KeepRadius, and at most MaxResidentTiles are
//              open (the farthest beyond Radius closed first). Tiles open nearest first (in front of the camera first among
//              equals). streamTile asks the host to open and close them; the host answers each tile it opens with a tile load
//              of its own (streamTileAnswer), under which that tile's files are asked.
//   a tile     one job per opened tile: the entry's pipeline with the session's WDT (parsed once) -- root, tex0, obj0 -> its
//              terrain, built against the SCENE'S ORIGIN (the entry tile's north-west corner: every tile's vertices in one
//              frame) -> added to the scene (nothing else disposed) -> its world models -> its doodads -> SETTLED (no report:
//              streamState says what it has). Its files carry its tile and a priority, smaller sooner: 1 + 3 x ring + family
//              (the tile's own files 0, world models 1, doodads 2), so a near tile's doodads come before a far tile's terrain.
//   sharing    by reference, across the session's tiles: a PLACEMENT (a world model's, an MDDF doodad's: FileDataID and
//              uniqueId; a world model's MODD doodad: its placement and MODD index) is one instance whatever tiles list it,
//              held by every tile that placed or found it and destroyed with the last; a PROTOTYPE (world model, M2) is built
//              once per FileDataID -- a tile that needs one another tile is fetching waits for it, takes the fetch over when
//              that tile closes first, and a near tile takes over a far tile's fetch not yet begun; a prototype built without
//              part of it that its own tile's load could not get (refused, stale, past its count cap) is that tile's PRIVATE
//              one -- placed for it alone, never shared or pooled -- and its fetch is handed on; a complete placement replaces
//              a private one under the same key; a TERRAIN TEXTURE is uploaded once per FileDataID and bound by every tile that
//              uses it (a tile asks only for what is not resident); a world-model or doodad TEXTURE is fetched and decoded once
//              per FileDataID for every tile whose prototypes sample it (one another tile is fetching is waited for, its fetch
//              taken over when that tile closes first), its image kept within StreamImageBudget for the prototypes built later.
//   residency  what no tile holds any more stays POOLED (prototypes, terrain textures, M2 texture uploads) while the pools fit
//              StreamPoolGpuBudget and StreamPoolManagedBudget, least recently used out first; a tile coming back uses what
//              is pooled without asking the host.
//   credit     at most RequestCredit requests out for the whole session; a tile beyond ring 0 leaves a reserve for the nearer
//              ones (StreamReserveRing1, StreamReserveFar), and the jobs advance nearest first every frame.
//   frames     the session's main-thread work -- a tile's Unity objects, a prototype build, placements -- shares one budget of
//              StreamFrameBudgetMs a frame (at least one item a frame, nearest tile first).
//   closing    a tile leaving the keep zone: its job (if running) ends -- its requests retired, a prototype fetch it owned
//              handed to a tile that waits for it -- its terrain goes, its references on placements, prototypes and textures
//              are given back; the host is told (streamTile close), which makes what it still had queued or in flight stale.
//   timeouts   a tile's job fails when TimeoutMs passes with no answer to its own requests (its terrain), and its world
//              models' and doodads' timeouts restart with each of their answers: a tile waiting for the credit is not late.
// A load without "streaming" never meets any of this: it runs exactly as before.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using Wmv.Wow;
using Wmv.Wow.World;

public sealed partial class WmvTerrainSession
{
    /// <summary>The session's main-thread work a frame (at least one item a frame).</summary>
    public const double StreamFrameBudgetMs = 8.0;
    /// <summary>streamState at most this often.</summary>
    public const double StreamStateIntervalMs = 250.0;
    const double PoolCheckIntervalMs = 500.0;   // the pools looked at at least this often (EvictPools)
    /// <summary>Credit a tile of ring 1, and of ring 2 or more, leaves for the nearer ones.</summary>
    public const int StreamReserveRing1 = 4, StreamReserveFar = 8;
    /// <summary>A tile the host refused, or whose job failed, is tried again after this long while it is still wanted.</summary>
    public const double StreamRetryMs = 10000.0;
    /// <summary>The scene's GPU budgets while streaming (checked before a doodad prototype is built, as Stage 07's are).</summary>
    public const long StreamGpuTextureBudget = 1536L << 20, StreamGpuGeometryBudget = 512L << 20;
    /// <summary>The pools: unreferenced prototypes and textures kept within these (fields so the self-test can set them).</summary>
    public static long StreamPoolGpuBudget = 256L << 20, StreamPoolManagedBudget = 128L << 20;
    /// <summary>The decoded world-model and doodad texture images the session keeps for the prototypes built later (their
    /// managed bytes; least recently used out first).</summary>
    public static long StreamImageBudget = 96L << 20;

    public const string StreamWanted = "wanted", StreamOpening = "opening", StreamQueued = "queued", StreamLoading = "loading",
                        StreamBuilt = "built", StreamSettled = "settled", StreamFailed = "failed";

    /// <summary>One tile of the region.</summary>
    sealed class StreamTile
    {
        public int X, Y;
        public long Key;
        public bool Entry;
        public int TileLoad;                   // the host's tile load (0 until it is opened)
        public string State = StreamWanted;
        public string Reason = "";
        public Job Job;                        // running (null before it starts and once it is settled or failed)
        public WmvRuntimeTerrainTile Tile;     // its terrain, once committed
        public TerrainTileMesh Mesh;
        public WmvTerrainTileContent Content;  // its material and textures as settled (another view draws it again from them)
        public bool RebuildDue;                // setTerrainView: drawn again in the view now in effect, in its turn
        public int Ring;                       // its distance in tiles (Chebyshev) to the camera's tile, at the last update
        public double Distance;                // to the camera point, for the order
        public double Facing;                  // in front of the camera (> 0) or behind
        public double RetryAtMs = -1;
        public readonly List<uint> TerrainTextures = new List<uint>();      // the scene's terrain textures it holds
        public readonly List<PlacementEntry> Placements = new List<PlacementEntry>();
        public readonly List<DoodadEntry> Doodads = new List<DoodadEntry>();
        public int FilesAsked, FilesAnswered;
        public long Bytes;
        public int WorldPlacements = -1, DoodadInstances = -1;   // what its job planned (at its settle)
        public double OpenedMs = -1, BuiltMs = -1, SettledMs = -1;
        public bool HostClosed;                // its tile load was closed on the host (streamTile close sent)
        public string Name { get { return X + "_" + Y; } }
    }

    const string ProtoPrivate = "private";     // a tile's own prototype record (KeepWorldModelForTheTile): never in Wmos or M2s

    static bool IsPrivate(ProtoEntry e) { return e != null && e.State == ProtoPrivate; }

    /// <summary>A prototype of the session (world model or M2), by FileDataID.</summary>
    sealed class ProtoEntry
    {
        public bool Doodad;
        public uint FileDataID;
        public string State = "fetching";      // "fetching" | "built" | "failed" | "private"
        public string Reason = "";
        public int FailState = -1;             // the doodad model state it failed in (unsupported stays unsupported)
        public Model Owner;                    // the world model fetching it (null: none -- the next waiting one takes it)
        public DoodadModel DOwner;
        public readonly List<Model> Borrowers = new List<Model>();
        public readonly List<DoodadModel> DBorrowers = new List<DoodadModel>();
        public WmvWorldPrototype Wmo;
        public WmvDoodadPrototype M2;
        public int Pins;                       // models of live jobs that hold it until their placements are done
        public long LastUse;                   // the session's use counter when it was last held
        public long ManagedBytes;              // a world model's parsed files (kept for the floors): their bytes
        public int Instances { get { return Wmo != null ? Wmo.Instances : M2 != null ? M2.Instances : 0; } }
        public bool Pooled { get { return State == "built" && Pins <= 0 && Instances <= 0; } }
        public long GpuBytes
        {
            get
            {
                if (Wmo != null && Wmo.Runtime != null) return Wmo.Runtime.GpuGeometryBytes + Wmo.Runtime.GpuTextureBytes;
                return M2 != null ? M2.GpuGeometryBytes : 0;
            }
        }
    }

    /// <summary>A world-model placement of the session, held by the tiles that placed or found it.</summary>
    sealed class PlacementEntry
    {
        public long Key;
        public WmvWorldInstance Instance;
        public WorldModelGeometry Geometry;
        public ProtoEntry Proto;
        public StreamTile PlacedBy;
        public readonly List<StreamTile> Holders = new List<StreamTile>(2);
    }

    /// <summary>A doodad of the session (MDDF: kind 1, FileDataID, uniqueId; a world model's MODD doodad: kind 2, the
    /// placement's FileDataID and uniqueId, the MODD index).</summary>
    struct DoodadKey : IEquatable<DoodadKey>
    {
        public byte Kind;
        public uint A, B;
        public int C;
        public bool Equals(DoodadKey o) { return Kind == o.Kind && A == o.A && B == o.B && C == o.C; }
        public override bool Equals(object o) { return o is DoodadKey && Equals((DoodadKey)o); }
        public override int GetHashCode() { unchecked { return ((((int)Kind * 397) ^ (int)A) * 397 ^ (int)B) * 397 ^ C; } }
    }

    sealed class DoodadEntry
    {
        public DoodadKey Key;
        public WmvDoodadInstance Instance;
        public ProtoEntry Proto;
        public readonly List<StreamTile> Holders = new List<StreamTile>(2);
    }

    /// <summary>A world-model or doodad texture of the session, by kind and FileDataID: fetched and decoded once (its owner's
    /// request), waited for by the other tiles' jobs that need it meanwhile, its image kept for those that come later.</summary>
    sealed class ImageEntry
    {
        public bool Wmo;
        public uint Id;
        public string State = "fetching";      // "fetching" | "ready" | "failed"
        public BlpImage Image;
        public string Error = "";
        public Job Owner;                      // the job whose request fetches it (null: the next waiting one takes it)
        public readonly List<Job> WaitJobs = new List<Job>();
        // Jobs that took the image without its file's bytes: kept for them while they live (a later model of theirs may
        // sample it again, and they have nothing to decode it from).
        public readonly List<Job> Takers = new List<Job>();
        public long LastUse;
        public long Bytes { get { return Image != null ? Image.HeldBytes : 0; } }
    }

    /// <summary>A terrain texture of the scene, uploaded once and bound by every tile that uses it.</summary>
    sealed class TerrainTexEntry
    {
        public uint Id;
        public UnityEngine.Texture2D Texture;
        public long GpuBytes;
        public WmvTerrainTexture Meta;         // what it was prepared as (its data gone)
        public int Refs;                       // tiles that bind it, and jobs that will
        public long LastUse;
    }

    sealed class StreamWork
    {
        public int EntryLoad, MapId, Epoch;
        public WmvIpcClient.TerrainTileCommand EntryCmd;
        public WmvIpcClient.StreamingOffer Offer;
        public WorldOrigin Origin;
        public WmvTerrainFormatSupport Support;
        public WdtFile Wdt;
        public long WdtBytes = -1;
        public string WdtMd5 = "";
        public bool Live;                      // the entry's tile is committed: the region follows the camera
        public bool Ended;
        public readonly Dictionary<long, StreamTile> Tiles = new Dictionary<long, StreamTile>();
        public readonly Dictionary<int, Job> Jobs = new Dictionary<int, Job>();
        public readonly Dictionary<string, Job> PendingById = new Dictionary<string, Job>();
        public int RequestsOut, RequestsOutMax;
        // Files asked for more than once in the session (by role and FileDataID): a file evicted and needed again, a fetch
        // taken over from a closed tile, or a texture two tiles' prototypes fetched each.
        public readonly HashSet<long> AskedFiles = new HashSet<long>();
        public long FilesAskedAgain;
        public readonly Dictionary<string, long> AskedAgainByRole = new Dictionary<string, long>();
        public bool AnswerAwaited;             // a streamTile is out (an open or only closes), its answer not in yet
        public readonly List<long> AwaitingOpen = new List<long>();
        public readonly List<int> CloseQueue = new List<int>();
        public readonly Dictionary<uint, ProtoEntry> Wmos = new Dictionary<uint, ProtoEntry>();
        public readonly Dictionary<uint, ProtoEntry> M2s = new Dictionary<uint, ProtoEntry>();
        public readonly Dictionary<long, PlacementEntry> Placements = new Dictionary<long, PlacementEntry>();
        public readonly Dictionary<DoodadKey, DoodadEntry> Doodads = new Dictionary<DoodadKey, DoodadEntry>();
        public readonly Dictionary<long, ImageEntry> Images = new Dictionary<long, ImageEntry>();
        public long ImagesShared, ImagesWaited, ImagesEvicted, ImageTakeovers;
        // Prototypes kept for the one tile whose load could not get all of them (KeepWorldModelForTheTile): the live records,
        // each holding its prototype and the pin of the model that built it; one goes once it has neither (DisposeUnusedPrivate).
        public readonly List<ProtoEntry> Private = new List<ProtoEntry>();
        public readonly Dictionary<long, int> KeptBuilds = new Dictionary<long, int>();   // (doodad << 32 | FileDataID): times kept
        public long PrototypesKept, PrivateDisposed, PlacementsReplaced, DoodadsReplaced, GroupRepeats;
        public bool HasCamera;
        public bool PoolsDirty = true;         // something may have joined the pools, or an eviction is unfinished
        public double PoolsCheckedMs = -1;
        public readonly float[] FrameRing = new float[120];   // the last frame times (p95 in streamState)
        public int FrameRingNext, FrameRingCount;
        public bool CameraKnown;               // the region has been centred once (the camera tile below is its centre)
        public double CameraX, CameraY, CameraZ, ForwardX, ForwardY;
        public int CameraTileX = -1, CameraTileY = -1;
        public int TilesOpened, TilesClosed, TilesFailed, TilesRefused;
        public long FilesAsked, FilesAnswered, BytesReceived;
        public long PrototypesEvicted, TexturesEvicted, PlacementsShared, DoodadsShared, PrototypesShared, PrototypeTakeovers;
        public long TerrainTexturesShared, TerrainTexturesUploaded;
        public long Use;                       // the use counter (least recently used first out)
        public double LastStateMs = -1, LastPumpMs = -1;
        public readonly List<float> FrameMs = new List<float>();
        public double WorkMsMax, UploadMsMax;
        public long FramesOver33, FramesOver50, Frames;
        public double FrameMsMaxAll, WorkMsMaxAll, UploadMsMaxAll;
        public readonly Stopwatch WorkClock = new Stopwatch();
        public bool WorkedThisFrame;
        public readonly Stopwatch Clock = Stopwatch.StartNew();
        // Protocol 19: the session's liquid registry (its entry's: every tile and world model of the session draws from it,
        // unheld types pooled; WmvTerrainSessionLiquids.cs), null when the entry carried no "liquids".
        public WmvLiquidRegistry Liquids;
        public int Credit { get { return Offer.RequestCredit > 0 ? Offer.RequestCredit : 24; } }
    }

    StreamWork stream;
    /// <summary>How the session destroys what it owns itself (the scene's terrain textures, evicted texture uploads):
    /// Object.Destroy in the player, DestroyImmediate in the self-test (which counts objects within a frame).</summary>
    public Action<UnityEngine.Object> DestroyObject = o => UnityEngine.Object.Destroy(o);
    // THE SCENE'S TERRAIN TEXTURES (streaming): uploaded once per FileDataID, held by the tiles that bind them; they go with
    // the scene's tiles (DisposeTiles).
    readonly Dictionary<uint, TerrainTexEntry> terrainTextures = new Dictionary<uint, TerrainTexEntry>();
    bool streamTransientFailure;           // a model settles because its job ends or times out: not the file's fault

    /// <summary>getMapAsset of a streaming session: (role, fileDataID, load, assetEpoch, tileX, tileY, priority) -> requestId.
    /// Null: no load streams (a loadWorld with "streaming" is then the single-tile load).</summary>
    public Func<string, int, int, int, int, int, int, string> RequestStreamMapAsset;
    /// <summary>streamTile (load, tiles to open as (x, y), tile loads to close).</summary>
    public Action<int, List<int[]>, List<int>> SendStreamTile;
    public Action<WmvIpcClient.StreamStateReport> ReportStreamState;
    /// <summary>Where the free camera is (the game's world frame: x, y, z, then its forward x, y), or null when it is not
    /// flying (the region then stays around the entry's tile).</summary>
    public Func<double[]> StreamCamera;

    // ---------------------------------------------------------------- state (runtimeState, the self-test)

    /// <summary>A streaming session is on.</summary>
    public bool Streaming { get { return stream != null; } }
    public int StreamTiles { get { return stream != null ? stream.Tiles.Count : 0; } }
    public int StreamJobs { get { return stream != null ? stream.Jobs.Count : 0; } }
    public int StreamRequestsOut { get { return stream != null ? stream.RequestsOut : 0; } }
    public int StreamRequestsOutMax { get { return stream != null ? stream.RequestsOutMax : 0; } }
    public int SceneTerrainTextures { get { return terrainTextures.Count; } }

    /// <summary>The GPU bytes of the scene's terrain textures (bound by the tiles, off their own tallies: WmvRuntimeTerrainTile.Hand).</summary>
    public long SceneTerrainTextureBytes
    {
        get
        {
            long n = 0;
            foreach (TerrainTexEntry t in terrainTextures.Values) n += t.GpuBytes;
            return n;
        }
    }

    /// <summary>The GPU bytes of the scene's terrain textures a streamed tile binds (its material report counts them).</summary>
    long SceneTextureBytesOf(Job j)
    {
        if (j.Stream == null)
            return 0;
        long n = 0;
        foreach (uint id in j.Stream.TerrainTextures)
        {
            TerrainTexEntry e;
            if (terrainTextures.TryGetValue(id, out e))
                n += e.GpuBytes;
        }
        return n;
    }
    public int StreamPlacements { get { return stream != null ? stream.Placements.Count : 0; } }
    public int StreamDoodads { get { return stream != null ? stream.Doodads.Count : 0; } }
    public long StreamPrototypesEvicted { get { return stream != null ? stream.PrototypesEvicted : 0; } }
    public long StreamTexturesEvicted { get { return stream != null ? stream.TexturesEvicted : 0; } }
    public long StreamImagesShared { get { return stream != null ? stream.ImagesShared : 0; } }
    public long StreamFilesAskedAgain { get { return stream != null ? stream.FilesAskedAgain : 0; } }
    public long StreamPrototypesKept { get { return stream != null ? stream.PrototypesKept : 0; } }
    public int StreamPrivatePrototypes { get { return stream != null ? stream.Private.Count : 0; } }
    public long StreamPlacementsReplaced { get { return stream != null ? stream.PlacementsReplaced : 0; } }
    public long StreamDoodadsReplaced { get { return stream != null ? stream.DoodadsReplaced : 0; } }
    public long StreamGroupRepeats { get { return stream != null ? stream.GroupRepeats : 0; } }

    /// <summary>The resident tiles (x, y, load, state), nearest first: the self-test's and the log's.</summary>
    public List<string> StreamTileStates()
    {
        var l = new List<string>();
        if (stream == null)
            return l;
        foreach (StreamTile st in Ordered())
            l.Add(st.Name + ":" + st.TileLoad + ":" + st.State);
        return l;
    }

    /// <summary>The resident tile whose terrain is under world point (x, y), with its mesh; the shown tile without a session.</summary>
    public bool ResidentTileAt(double x, double y, out WmvRuntimeTerrainTile tile, out TerrainTileMesh mesh)
    {
        tile = null;
        mesh = null;
        int tx, ty;
        TileOfPoint(x, y, out tx, out ty);
        if (stream != null)
        {
            StreamTile st;
            if (stream.Tiles.TryGetValue(KeyOf(stream.MapId, tx, ty), out st) && st.Tile != null && !st.Tile.Disposed && st.Mesh != null)
            {
                tile = st.Tile;
                mesh = st.Mesh;
                return true;
            }
            return false;
        }
        if (shown != null && !shown.Disposed && shownMesh != null && shown.TileX == tx && shown.TileY == ty)
        {
            tile = shown;
            mesh = shownMesh;
            return true;
        }
        return false;
    }

    /// <summary>The tile under world point (x, y): x in ((32 - ty - 1) T, (32 - ty) T], y likewise with tx (OnTile's rule).</summary>
    public static void TileOfPoint(double x, double y, out int tx, out int ty)
    {
        double t = TerrainCoordinates.TileSize;
        ty = (int)Math.Floor(32.0 - x / t);
        tx = (int)Math.Floor(32.0 - y / t);
    }

    // ---------------------------------------------------------------- the session

    /// <summary>Load: a loadWorld with "streaming" (a tile map's, with doodads, to a player wired for it) begins a session.</summary>
    bool StreamOffered(WmvIpcClient.TerrainTileCommand cmd)
    {
        return cmd.Streaming != null && cmd.World && !cmd.GlobalWmo && cmd.DoodadsOffered && RequestStreamMapAsset != null &&
               SendStreamTile != null;
    }

    void BeginStream(Job j)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        stream = new StreamWork
        {
            EntryLoad = cmd.Load, MapId = cmd.MapId, Epoch = cmd.AssetEpoch, EntryCmd = cmd, Offer = cmd.Streaming,
            Origin = WorldToUnity.TileOrigin(cmd.TileX, cmd.TileY), Support = j.Support,
        };
        var st = new StreamTile
        {
            X = cmd.TileX, Y = cmd.TileY, Key = KeyOf(cmd.MapId, cmd.TileX, cmd.TileY), Entry = true, TileLoad = cmd.Load,
            State = StreamLoading, Job = j, OpenedMs = 0,
        };
        j.Stream = st;
        stream.Tiles[st.Key] = st;
        stream.Jobs[cmd.Load] = j;
        Log("load {0}: a streaming session begins at tile {1}_{2} of map {3}: {4}; the scene's origin {5}", cmd.Load, cmd.TileX, cmd.TileY,
            cmd.MapId, cmd.Streaming, stream.Origin);
    }

    /// <summary>The session ends (a new load, clearTerrain, a model load, shutdown, the entry failed before its tile): every
    /// job but `current` (the caller reports it) ends unreported, its requests retired. What is on screen stays with the
    /// scene's tiles (the next commit or the clear disposes them).</summary>
    /// <summary>Another tile's job advanced from inside a hand-over (a fetch, an image): a model it fails there for good fails
    /// for good, whatever the context handing over (a timeout's transient failure) was.</summary>
    void AdvanceOther(Action advance)
    {
        bool was = streamTransientFailure;
        streamTransientFailure = false;
        try { advance(); }
        finally { streamTransientFailure = was; }
    }

    /// <summary>The session is about to end (a new load, a clear, a model load, shutdown): marked so before the entry is
    /// reported or retired, so nothing it hands on (a fetch, an image) is taken up by another tile that ends with it -- no
    /// request is sent for a tile about to go.</summary>
    void StreamEnding()
    {
        if (stream != null)
            stream.Ended = true;
    }

    void EndStream(string why)
    {
        StreamWork s = stream;
        if (s == null)
            return;
        s.Ended = true;
        int jobs = 0;
        foreach (Job j in new List<Job>(s.Jobs.Values))
        {
            if (j == current)
                continue;
            j.Dead = true;
            Retire(j, why);
            ReleasePrepared(j);
            ReleaseJobTerrainTextures(j);
            jobs++;
        }
        Log("load {0}: the streaming session ends ({1}): {2} tile(s), {3} job(s) ended unreported; {4} tile(s) opened, {5} closed, {6} failed, " +
            "{7} refused in all; {8} file(s) asked, {9} answered, {10} bytes; {11} placement(s) shared, {12} doodad(s) shared, {13} prototype(s) " +
            "shared ({14} fetches taken over), {15} evicted; {16} terrain texture(s) shared, {17} uploaded, {18} M2/terrain texture(s) evicted; " +
            "most requests out at once {19}; {20} file(s) asked more than once ({21}); model texture images: {22} shared, {23} waited for, {24} " +
            "fetches taken over, {25} evicted; {26} prototype(s) kept for their tile ({27} live, {28} disposed), {29} placement(s) and " +
            "{30} doodad(s) replaced by complete ones, {31} repeated group ask(s)", s.EntryLoad, why, s.Tiles.Count, jobs, s.TilesOpened,
            s.TilesClosed, s.TilesFailed, s.TilesRefused, s.FilesAsked, s.FilesAnswered, s.BytesReceived, s.PlacementsShared, s.DoodadsShared,
            s.PrototypesShared, s.PrototypeTakeovers, s.PrototypesEvicted, s.TerrainTexturesShared, s.TerrainTexturesUploaded, s.TexturesEvicted,
            s.RequestsOutMax, s.FilesAskedAgain, AskedAgainLine(s), s.ImagesShared, s.ImagesWaited, s.ImageTakeovers, s.ImagesEvicted,
            s.PrototypesKept, s.Private.Count, s.PrivateDisposed, s.PlacementsReplaced, s.DoodadsReplaced, s.GroupRepeats);
        s.Jobs.Clear();
        s.PendingById.Clear();
        s.RequestsOut = 0;
        // Protocol 19: the session's liquids go with it unless they are the scene's (its tiles stay on screen until the next
        // commit or the clear, which takes them).
        if (s.Liquids != null && s.Liquids != sceneLiquids && !s.Liquids.Disposed)
            s.Liquids.Dispose();
        stream = null;
    }

    static string AskedAgainLine(StreamWork s)
    {
        var parts = new List<string>();
        foreach (KeyValuePair<string, long> kv in s.AskedAgainByRole)
            parts.Add(kv.Key + " " + kv.Value);
        return parts.Count > 0 ? string.Join(", ", parts.ToArray()) : "none";
    }

    /// <summary>The job is one the session still drives: the load in flight (current), or a live tile of the session.</summary>
    bool Live(Job j)
    {
        if (j == null)
            return false;
        if (j == current)
            return true;
        if (j.Stream == null || j.Dead || stream == null || stream.Ended)
            return false;
        Job x;
        return stream.Jobs.TryGetValue(j.Cmd.Load, out x) && x == j;
    }

    /// <summary>The world scene is the job's: its own load's, or (streaming) the session's.</summary>
    bool OwnsScene(Job j)
    {
        return world != null && (world.Load == j.Cmd.Load || (j.Stream != null && stream != null && world.Load == stream.EntryLoad));
    }

    // ---------------------------------------------------------------- requests: credit, priority, routing

    /// <summary>The credit a request of `j` may take now (always, outside a session).</summary>
    bool StreamCredit(Job j, int n = 1)
    {
        return j.Stream == null || stream == null || StreamCreditForRing(j.Stream.Ring, n);
    }

    bool StreamCreditForRing(int ring, int n)
    {
        int reserve = ring <= 0 ? 0 : ring == 1 ? StreamReserveRing1 : StreamReserveFar;
        return stream.RequestsOut + n <= stream.Credit - reserve;
    }

    /// <summary>getMapAsset for `j`: its tile and priority, routed back to it by id, counted against the credit (a session's);
    /// as before otherwise.</summary>
    string SendMapRequest(Job j, string role, int fileDataID)
    {
        if (j.Stream == null || stream == null)
            return requestMapAsset(role, fileDataID, j.Cmd.Load, j.Cmd.AssetEpoch);
        int family = role == RoleM2Model || role == RoleM2Skin || role == RoleM2Texture ? 2
                   : role == RoleWmoRoot || role == RoleWmoGroup || role == RoleWmoTexture ? 1 : 0;
        int priority = 1 + 3 * Math.Max(0, j.Stream.Ring) + family;
        string id = RequestStreamMapAsset(role, fileDataID, j.Cmd.Load, j.Cmd.AssetEpoch, j.Cmd.TileX, j.Cmd.TileY, priority);
        stream.PendingById[id] = j;
        stream.RequestsOut++;
        stream.RequestsOutMax = Math.Max(stream.RequestsOutMax, stream.RequestsOut);
        stream.FilesAsked++;
        j.Stream.FilesAsked++;
        long key = ((long)(uint)role.GetHashCode() << 32) | (uint)fileDataID;
        if (!stream.AskedFiles.Add(key))
        {
            stream.FilesAskedAgain++;
            long n;
            stream.AskedAgainByRole[role] = stream.AskedAgainByRole.TryGetValue(role, out n) ? n + 1 : 1;
        }
        return id;
    }

    /// <summary>A request of the session answered or retired: off the routing and the credit.</summary>
    void StreamRequestDone(string id, WmvIpcClient.AssetResponse answer)
    {
        Job j;
        if (stream == null || id == null || !stream.PendingById.TryGetValue(id, out j))
            return;
        stream.PendingById.Remove(id);
        stream.RequestsOut--;
        if (answer == null)
            return;
        stream.FilesAnswered++;
        if (j.Stream != null)
            j.Stream.FilesAnswered++;
        if (answer.data != null)
        {
            stream.BytesReceived += answer.data.Length;
            if (j.Stream != null)
                j.Stream.Bytes += answer.data.Length;
        }
        // Progress: the tile's own timeout restarts with each answer to it.
        j.StartedAtMs = double.NaN;
    }

    /// <summary>The job a session's request belongs to (null: none of the session's).</summary>
    Job StreamJobOf(string id)
    {
        Job j;
        return stream != null && id != null && stream.PendingById.TryGetValue(id, out j) ? j : null;
    }

    // ---------------------------------------------------------------- frames

    /// <summary>Another item of the session's main-thread work may run this frame.</summary>
    bool StreamMayWork()
    {
        return stream == null || !stream.WorkedThisFrame || stream.WorkClock.Elapsed.TotalMilliseconds < StreamFrameBudgetMs;
    }

    /// <summary>A heavy item -- a tile's Unity objects (added or rebuilt), a world-model prototype built from its files -- runs
    /// only as the frame's first item: items no longer stack past the budget behind one another (a frame is at most its
    /// heaviest item, or the 8 ms budget and one light item).</summary>
    bool StreamMayWorkHeavy()
    {
        return stream == null || !stream.WorkedThisFrame;
    }

    void StreamWorked()
    {
        if (stream != null)
            stream.WorkedThisFrame = true;
    }

    /// <summary>One frame of the session (Pump, after the worker's results): the jobs nearest first, the region, the pools, the
    /// state.</summary>
    void PumpStream(double nowMs)
    {
        StreamWork s = stream;
        if (s.LastPumpMs >= 0)
        {
            float ms = (float)(nowMs - s.LastPumpMs);
            s.FrameMs.Add(ms);
            s.FrameRing[s.FrameRingNext] = ms;
            s.FrameRingNext = (s.FrameRingNext + 1) % s.FrameRing.Length;
            if (s.FrameRingCount < s.FrameRing.Length) s.FrameRingCount++;
            s.Frames++;
            if (ms > 33.4f) s.FramesOver33++;
            if (ms > 50f) s.FramesOver50++;
            s.FrameMsMaxAll = Math.Max(s.FrameMsMaxAll, ms);
        }
        s.LastPumpMs = nowMs;
        s.WorkClock.Reset();
        s.WorkClock.Start();
        s.WorkedThisFrame = false;
        foreach (StreamTile st in Ordered())
        {
            Job j = st.Job;
            if (j == null || !Live(j))
                continue;
            PumpStreamJob(j, nowMs);
            if (stream != s)
                return;                         // the entry failed before its tile: the session ended
        }
        if (stream != s)
            return;
        if (s.Live)
        {
            UpdateRegion(nowMs);
            StartQueuedTiles(nowMs);
            // The pools are looked at when something may have joined them (a tile closed, a job ended) or an eviction is
            // unfinished, and twice a second besides -- not every frame.
            if (s.PoolsDirty || s.PoolsCheckedMs < 0 || nowMs - s.PoolsCheckedMs >= PoolCheckIntervalMs)
            {
                s.PoolsCheckedMs = nowMs;
                EvictPools();
            }
            if (StreamMayWorkHeavy())
                RebuildNextStreamTile();
        }
        double work = s.WorkClock.Elapsed.TotalMilliseconds;
        s.WorkMsMax = Math.Max(s.WorkMsMax, work);
        s.WorkMsMaxAll = Math.Max(s.WorkMsMaxAll, work);
        if (s.LastStateMs < 0 || nowMs - s.LastStateMs >= StreamStateIntervalMs)
        {
            s.LastStateMs = nowMs;
            SendStreamState();
        }
    }

    /// <summary>One frame of one tile's job: what Pump does for the load in flight, under the session's frame budget.</summary>
    void PumpStreamJob(Job j, double nowMs)
    {
        if (double.IsNaN(j.StartedAtMs) || j.Pending.Count == 0)
        {
            // A tile with nothing of its own asked and unanswered is not late -- it waits for the credit, the frame budget,
            // its workers or another tile's fetch: its timeouts run only while its own requests are out.
            j.StartedAtMs = nowMs;
            if (j.Pending.Count == 0 && j.World != null)
            {
                if (!double.IsNaN(j.World.TimeoutStartedAtMs))
                    j.World.TimeoutStartedAtMs = nowMs;
                if (j.World.Doodads != null && !double.IsNaN(j.World.Doodads.TimeoutStartedAtMs))
                    j.World.Doodads.TimeoutStartedAtMs = nowMs;
            }
        }
        if (j.Phase == Phase.Wdt || j.Phase == Phase.Files)
        {
            if (j.AdoptReady && StreamMayWorkHeavy())
            {
                j.AdoptReady = false;
                AdoptStreamTile(j);
                StreamWorked();
                return;
            }
            if (nowMs - j.StartedAtMs > timeoutMs)
                Fail(j, "timed out: no answer for " + (timeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                        " s while waiting for " + WaitingFor(j));
            else
            {
                Advance(j);
                AskTextures(j);
                AskLiquidFrames(j);             // (protocol 19: the tile does not wait for them)
            }
            return;
        }
        if (j.World != null && !PumpWorld(j, nowMs))
            return;
        if (!Live(j))
            return;
        // Protocol 19: the tile's liquid frames; it settles once they have.
        bool liquids = PumpLiquids(j, nowMs);
        if (!Live(j))
            return;
        if (liquids && frame >= j.AdoptFrame + 2 && (j.World == null || (WorldSettled(j.World) && frame >= WorldLastFrame(j.World) + 2)))
        {
            if (j == current)
            {
                FinishBuilt(j);
                StreamJobEnded(j, StreamSettled, "");
            }
            else
                SettleStreamJob(j);
        }
    }

    /// <summary>The tiles, nearest the camera first (ring, then distance, then in front first).</summary>
    List<StreamTile> Ordered()
    {
        var l = new List<StreamTile>(stream.Tiles.Values);
        l.Sort((a, b) => a.Ring != b.Ring ? a.Ring.CompareTo(b.Ring)
                       : a.Distance != b.Distance ? a.Distance.CompareTo(b.Distance) : b.Facing.CompareTo(a.Facing));
        return l;
    }

    // ---------------------------------------------------------------- the region

    /// <summary>The camera's tile, the rings, what opens and what closes; one streamTile out at a time.</summary>
    void UpdateRegion(double nowMs)
    {
        StreamWork s = stream;
        WmvIpcClient.StreamingOffer o = s.Offer;
        double[] cam = StreamCamera != null ? StreamCamera() : null;
        int cx, cy;
        if (cam != null && cam.Length >= 3 && !double.IsNaN(cam[0]) && !double.IsNaN(cam[1]))
        {
            s.HasCamera = true;
            s.CameraX = cam[0];
            s.CameraY = cam[1];
            s.CameraZ = cam[2];
            s.ForwardX = cam.Length >= 5 ? cam[3] : 0;
            s.ForwardY = cam.Length >= 5 ? cam[4] : 0;
            TileOfPoint(cam[0], cam[1], out cx, out cy);
        }
        else if (s.CameraKnown)
        {
            // Not flying (the overview, or the free camera left): the region stays where the camera last was -- the tile the
            // overview frames is one of it.
            cx = s.CameraTileX;
            cy = s.CameraTileY;
            s.HasCamera = false;
        }
        else
        {
            // Never flown yet: around the entry's tile, its centre the point.
            cx = s.EntryCmd.TileX;
            cy = s.EntryCmd.TileY;
            WorldOrigin c = WorldToUnity.TileOrigin(cx, cy);
            s.HasCamera = false;
            s.CameraX = c.X - TerrainCoordinates.TileSize * 0.5;
            s.CameraY = c.Y - TerrainCoordinates.TileSize * 0.5;
            s.ForwardX = s.ForwardY = 0;
        }
        s.CameraKnown = true;
        s.CameraTileX = cx;
        s.CameraTileY = cy;
        foreach (StreamTile st in s.Tiles.Values)
            Measure(st, cx, cy);
        var toClose = new List<StreamTile>();
        foreach (StreamTile st in s.Tiles.Values)
            if (st.Ring > o.KeepRadius)
                toClose.Add(st);
        // The wanted tiles not yet in the region.
        var toOpen = new List<StreamTile>();
        for (int dy = -o.Radius; dy <= o.Radius; dy++)
            for (int dx = -o.Radius; dx <= o.Radius; dx++)
            {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= TerrainCoordinates.TilesPerSide || y >= TerrainCoordinates.TilesPerSide)
                    continue;
                if (s.Wdt == null || !s.Wdt.TileExists(x, y))
                    continue;
                long key = KeyOf(s.MapId, x, y);
                StreamTile st;
                if (s.Tiles.TryGetValue(key, out st))
                {
                    // A tile that failed is tried again once its retry time has come (its tile load closed when it failed).
                    if (st.State == StreamFailed && st.Job == null && st.Tile == null && st.RetryAtMs >= 0 &&
                        s.Clock.Elapsed.TotalMilliseconds >= st.RetryAtMs && !s.AnswerAwaited)
                    {
                        Log("load {0}: tile {1} is tried again ({2})", s.EntryLoad, st.Name, st.Reason);
                        CloseHostLoad(st);
                        st.TileLoad = 0;
                        st.HostClosed = false;
                        st.State = StreamWanted;
                        st.Reason = "";
                        st.RetryAtMs = -1;
                        toOpen.Add(st);
                    }
                    else if (st.State == StreamWanted)
                        toOpen.Add(st);
                    continue;
                }
                st = new StreamTile { X = x, Y = y, Key = key };
                Measure(st, cx, cy);
                s.Tiles[key] = st;
                toOpen.Add(st);
            }
        // Too many: the farthest beyond the radius go first.
        int resident = 0;
        foreach (StreamTile st in s.Tiles.Values)
            if (!toClose.Contains(st))
                resident++;
        if (o.MaxResidentTiles > 0 && resident > o.MaxResidentTiles)
        {
            var beyond = new List<StreamTile>();
            foreach (StreamTile st in s.Tiles.Values)
                if (st.Ring > o.Radius && !toClose.Contains(st))
                    beyond.Add(st);
            beyond.Sort((a, b) => b.Ring != a.Ring ? b.Ring.CompareTo(a.Ring) : b.Distance.CompareTo(a.Distance));
            for (int i = 0; i < beyond.Count && resident > o.MaxResidentTiles; i++, resident--)
                toClose.Add(beyond[i]);
        }
        foreach (StreamTile st in toClose)
            CloseStreamTile(st, string.Format("it left the region (ring {0} around tile {1}_{2})", st.Ring, cx, cy));
        // One streamTile out at a time: the open of those still wanted, the closes queued.
        if (s.AnswerAwaited || stream != s)
            return;
        toOpen.RemoveAll(t => t.State != StreamWanted);
        toOpen.Sort((a, b) => a.Ring != b.Ring ? a.Ring.CompareTo(b.Ring)
                            : a.Distance != b.Distance ? a.Distance.CompareTo(b.Distance) : b.Facing.CompareTo(a.Facing));
        if (toOpen.Count == 0 && s.CloseQueue.Count == 0)
            return;
        var open = new List<int[]>();
        foreach (StreamTile st in toOpen)
        {
            st.State = StreamOpening;
            st.OpenedMs = s.Clock.Elapsed.TotalMilliseconds;
            open.Add(new[] { st.X, st.Y });
            s.AwaitingOpen.Add(st.Key);
        }
        var close = new List<int>(s.CloseQueue);
        s.CloseQueue.Clear();
        // One streamTile out at a time, a message of closes only too: its answer must not be taken for a later open's.
        s.AnswerAwaited = true;
        var names = new StringBuilder();
        foreach (int[] t in open)
            names.Append(names.Length > 0 ? " " : "").Append(t[0]).Append('_').Append(t[1]);
        Log("load {0}: streamTile around tile {1}_{2}{3}: open [{4}], close [{5}]", s.EntryLoad, cx, cy,
            s.HasCamera ? string.Format(System.Globalization.CultureInfo.InvariantCulture, " (camera at {0:0.#}, {1:0.#})", s.CameraX, s.CameraY) : "",
            names, string.Join(" ", close.ConvertAll(x => x.ToString()).ToArray()));
        SendStreamTile(s.EntryLoad, open, close);
    }

    /// <summary>The tile's ring around the camera's tile, its distance to the camera point and whether it is in front.</summary>
    void Measure(StreamTile st, int cx, int cy)
    {
        st.Ring = Math.Max(Math.Abs(st.X - cx), Math.Abs(st.Y - cy));
        WorldOrigin o = WorldToUnity.TileOrigin(st.X, st.Y);
        double half = TerrainCoordinates.TileSize * 0.5;
        double dx = (o.X - half) - stream.CameraX, dy = (o.Y - half) - stream.CameraY;
        st.Distance = Math.Sqrt(dx * dx + dy * dy);
        st.Facing = dx * stream.ForwardX + dy * stream.ForwardY;
    }

    /// <summary>streamTileAnswer: each opened tile becomes a job (started when the credit allows), each refusal a failed tile
    /// (tried again later), and a tile opened for a tile the region dropped meanwhile is closed again.</summary>
    public void OnStreamTileAnswer(WmvIpcClient.StreamTileAnswer a)
    {
        StreamWork s = stream;
        if (a == null || s == null || disposed || a.Load != s.EntryLoad)
        {
            if (a != null)
            {
                Log("streamTileAnswer of load {0} with no such session ({1} opened, {2} refused, {3} closed) -- its opened tiles are not used",
                    a.Load, a.Opened.Count, a.Refused.Count, a.Closed.Count);
                // A tile load opened for a session that has gone is the host's to end with that session (it ended it with it).
            }
            return;
        }
        s.AnswerAwaited = false;
        foreach (WmvIpcClient.StreamTileOpened t in a.Opened)
        {
            long key = KeyOf(s.MapId, t.X, t.Y);
            StreamTile st;
            s.AwaitingOpen.Remove(key);
            if (!s.Tiles.TryGetValue(key, out st) || st.State != StreamOpening)
            {
                // The region dropped it meanwhile: closed again at once.
                s.CloseQueue.Add(t.TileLoad);
                Log("load {0}: tile {1}_{2} was opened as load {3} after the region dropped it: closed again", s.EntryLoad, t.X, t.Y, t.TileLoad);
                continue;
            }
            st.TileLoad = t.TileLoad;
            st.State = StreamQueued;
            s.TilesOpened++;
        }
        foreach (WmvIpcClient.StreamTileOpened t in a.Refused)
        {
            long key = KeyOf(s.MapId, t.X, t.Y);
            StreamTile st;
            s.AwaitingOpen.Remove(key);
            s.TilesRefused++;
            if (s.Tiles.TryGetValue(key, out st) && st.State == StreamOpening)
            {
                st.State = StreamFailed;
                st.Reason = "the host refused to open it: " + t.Refused;
                st.RetryAtMs = s.Clock.Elapsed.TotalMilliseconds + StreamRetryMs;
            }
            Log("load {0}: tile {1}_{2} not opened: {3}", s.EntryLoad, t.X, t.Y, t.Refused);
        }
        // A tile asked for and neither opened nor refused (an answer that left it out): wanted again.
        foreach (long key in new List<long>(s.AwaitingOpen))
        {
            StreamTile st;
            if (s.Tiles.TryGetValue(key, out st) && st.State == StreamOpening)
                st.State = StreamWanted;
        }
        s.AwaitingOpen.Clear();
    }

    /// <summary>Opened tiles start, nearest first, while the credit allows their first three requests (root, tex0, obj0).</summary>
    void StartQueuedTiles(double nowMs)
    {
        foreach (StreamTile st in Ordered())
        {
            if (stream == null)
                return;
            if (st.State != StreamQueued || st.Job != null)
                continue;
            if (!StreamCreditForRing(st.Ring, 3))
                return;
            StartStreamJob(st, nowMs);
        }
    }

    void StartStreamJob(StreamTile st, double nowMs)
    {
        StreamWork s = stream;
        WmvIpcClient.TerrainTileCommand e = s.EntryCmd;
        var cmd = new WmvIpcClient.TerrainTileCommand
        {
            Load = st.TileLoad, AssetEpoch = e.AssetEpoch, MapId = e.MapId, MapDirectory = e.MapDirectory, Product = e.Product, Build = e.Build,
            WdtFileDataID = e.WdtFileDataID, WdtResolvedBy = e.WdtResolvedBy, WdtPath = e.WdtPath, TileX = st.X, TileY = st.Y,
            MaxAssetBytes = e.MaxAssetBytes, MaxTextureBytes = e.MaxTextureBytes, MaxTextureBytesPerLoad = e.MaxTextureBytesPerLoad,
            MaxTexturesPerLoad = e.MaxTexturesPerLoad, World = true, MaxWmoFileBytes = e.MaxWmoFileBytes, MaxWmoBytesPerLoad = e.MaxWmoBytesPerLoad,
            MaxWmoTextureBytes = e.MaxWmoTextureBytes, MaxWmoTextureBytesPerLoad = e.MaxWmoTextureBytesPerLoad,
            MaxWmoTexturesPerLoad = e.MaxWmoTexturesPerLoad, MaxWmoModelsPerLoad = e.MaxWmoModelsPerLoad, MaxWmoGroupsPerLoad = e.MaxWmoGroupsPerLoad,
            MaxWmoPlacementsPerLoad = e.MaxWmoPlacementsPerLoad, Doodads = e.Doodads, MaxM2FileBytes = e.MaxM2FileBytes,
            MaxM2BytesPerLoad = e.MaxM2BytesPerLoad, MaxM2TextureBytes = e.MaxM2TextureBytes, MaxM2TextureBytesPerLoad = e.MaxM2TextureBytesPerLoad,
            MaxM2ModelsPerLoad = e.MaxM2ModelsPerLoad, MaxM2TexturesPerLoad = e.MaxM2TexturesPerLoad,
            MaxDoodadInstancesPerLoad = e.MaxDoodadInstancesPerLoad,
            // Protocol 19: the entry's liquid offer -- budgets and catalogue -- as the host gives the tile load the entry's.
            Liquids = e.Liquids,
        };
        var j = new Job { Cmd = cmd, MaxBytes = cmd.MaxAssetBytes > 0 ? cmd.MaxAssetBytes : DefaultMaxAssetBytes, Stream = st, Support = s.Support };
        j.World = new WorldWork { Global = false, Frame = PlacementFrame.Tile, Doodads = new DoodadsWork { Offered = cmd.DoodadsOffered } };
        // The session's WDT, read once (the entry's).
        j.WdtBytes = s.WdtBytes;
        j.WdtMd5 = s.WdtMd5;
        j.WdtRoundTripMs = 0;
        j.WdtParseMs = 0;
        j.StartedAtMs = nowMs;
        st.Job = j;
        st.State = StreamLoading;
        s.Jobs[cmd.Load] = j;
        BeginLiquids(j);                        // (the session's registry)
        Log("load {0}: tile {1} starts as load {2} (ring {3}); {4} request(s) out", s.EntryLoad, st.Name, cmd.Load, st.Ring, s.RequestsOut);
        StartTileFiles(j, s.Wdt);
    }

    // ---------------------------------------------------------------- a tile's terrain

    /// <summary>A resident terrain texture taken for `j`'s tile (a reference held until its tile binds it, or the job ends):
    /// the job's entry is a resident copy, nothing is asked for. False when the scene has none (or the session is not live,
    /// or `j` is the entry: its commit may reset the scene).</summary>
    bool TakeResidentTerrainTexture(Job j, uint id)
    {
        if (j.Stream == null || j.Stream.Entry || stream == null || !stream.Live)
            return false;
        TerrainTexEntry e;
        if (!terrainTextures.TryGetValue(id, out e) || e.Texture == null)
            return false;
        e.Refs++;
        e.LastUse = ++stream.Use;
        j.ResidentTextures.Add(id);
        j.Textures[id] = e.Meta.ResidentCopy();
        stream.TerrainTexturesShared++;
        return true;
    }

    /// <summary>The references a job took on resident terrain textures its tile did not bind (or never got) given back.</summary>
    void ReleaseJobTerrainTextures(Job j)
    {
        foreach (uint id in j.ResidentTextures)
            ReleaseTerrainTexture(id);
        j.ResidentTextures.Clear();
    }

    void ReleaseTerrainTexture(uint id)
    {
        TerrainTexEntry e;
        if (!terrainTextures.TryGetValue(id, out e))
            return;
        if (e.Refs > 0)
            e.Refs--;
        if (stream != null)
            e.LastUse = ++stream.Use;
    }

    /// <summary>A committed tile's textures become the scene's: what it uploaded is handed over (off the tile's lists, not
    /// destroyed) with the tile's reference; what it bound from the scene keeps the job's reference as the tile's.</summary>
    void HandTileTextures(StreamTile st, WmvRuntimeTerrainTile tile, Job j)
    {
        foreach (KeyValuePair<uint, UnityEngine.Texture2D> kv in new List<KeyValuePair<uint, UnityEngine.Texture2D>>(tile.Textures))
        {
            TerrainTexEntry e;
            if (terrainTextures.TryGetValue(kv.Key, out e) && e.Texture == kv.Value)
            {
                if (j.ResidentTextures.Remove(kv.Key))
                    st.TerrainTextures.Add(kv.Key);
                continue;
            }
            if (e != null)
                continue;                       // another tile uploaded it first: this tile keeps its own (destroyed with it)
            long gpu;
            if (!tile.Hand(kv.Value, out gpu))
                continue;
            tile.Textures[kv.Key] = kv.Value;   // still bound by its chunks (Hand took it off the map)
            WmvTerrainTexture meta;
            j.Textures.TryGetValue(kv.Key, out meta);
            terrainTextures[kv.Key] = new TerrainTexEntry
            {
                Id = kv.Key, Texture = kv.Value, GpuBytes = gpu, Meta = meta != null ? meta.ResidentCopy() : new WmvTerrainTexture { FileDataID = kv.Key, State = WmvTerrainTexture.StateAvailable },
                Refs = 1, LastUse = stream != null ? ++stream.Use : 0,
            };
            st.TerrainTextures.Add(kv.Key);
            if (stream != null)
                stream.TerrainTexturesUploaded++;
        }
        ReleaseJobTerrainTextures(j);
    }

    /// <summary>The scene's terrain textures go (DisposeTiles: the scene goes).</summary>
    void DisposeTerrainTextures()
    {
        foreach (TerrainTexEntry e in terrainTextures.Values)
            if (e.Texture != null)
                DestroyObject(e.Texture);
        terrainTextures.Clear();
    }

    /// <summary>The entry's tile committed (Adopt): its textures the scene's, the region begins.</summary>
    void StreamEntryCommitted(Job j, WmvRuntimeTerrainTile tile)
    {
        StreamTile st = j.Stream;
        st.Tile = tile;
        st.Mesh = j.Mesh;
        st.Content = j.Content;
        st.State = StreamBuilt;
        st.BuiltMs = stream.Clock.Elapsed.TotalMilliseconds;
        HandTileTextures(st, tile, j);
        stream.Live = true;
        Log("load {0}: the entry's tile {1} is committed: the region follows the camera from now on ({2} terrain texture(s) in the scene)",
            j.Cmd.Load, st.Name, terrainTextures.Count);
    }

    /// <summary>A tile of the session (not the entry) is ready: its Unity objects built (under the frame budget) and ADDED to
    /// the scene -- nothing else disposed --, its textures the scene's, then its world models.</summary>
    void AdoptStreamTile(Job j)
    {
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        StreamTile st = j.Stream;
        j.Content = new WmvTerrainTileContent
        {
            Material = j.Material ?? TerrainTileMaterial.Unavailable(j.MaterialUnavailable ?? "no material", j.MphdFlags),
            Textures = j.Textures, Tex0FileDataID = j.Tex0FileDataID, MapTextureFileDataID = j.MapTextureFileDataID, MphdFlags = j.MphdFlags,
            Build = cmd.Build ?? "", WdtMd5 = j.WdtMd5 ?? "", RootMd5 = j.RootMd5 ?? "", Tex0Md5 = j.Tex0Md5 ?? "",
            LiquidTile = j.Liquid != null ? j.Liquid.Tile : null, Liquids = j.Liquid != null ? j.Liquid.Registry : null,
        };
        SettleTextures(j.Content, Log, cmd.Load);
        var tile = new WmvRuntimeTerrainTile(cmd.MapId, cmd.TileX, cmd.TileY, cmd.Load);
        foreach (uint id in j.ResidentTextures)
        {
            TerrainTexEntry e;
            if (terrainTextures.TryGetValue(id, out e) && e.Texture != null)
                tile.Textures[id] = e.Texture;  // bound, not tracked: the scene's
        }
        try
        {
            var sw = Stopwatch.StartNew();
            view.BuildTile(tile, j.Mesh, j.Content);
            j.UploadMs = sw.Elapsed.TotalMilliseconds;
            stream.UploadMsMax = Math.Max(stream.UploadMsMax, j.UploadMs);
            stream.UploadMsMaxAll = Math.Max(stream.UploadMsMaxAll, j.UploadMs);
        }
        catch (Exception ex)
        {
            foreach (uint id in j.ResidentTextures)
                tile.Textures.Remove(id);
            try { view.DisposeTile(tile); }
            catch (Exception e2) { Log("load {0}: disposing the unfinished tile failed: {1}", cmd.Load, e2.Message); }
            ReleasePrepared(j);
            Fail(j, "building the tile's Unity objects failed: " + ex.GetType().Name + ": " + ex.Message);
            return;
        }
        long key = KeyOf(cmd.MapId, cmd.TileX, cmd.TileY);
        WmvRuntimeTerrainTile old;
        if (tiles.TryGetValue(key, out old) && old != tile)
        {
            view.DisposeTile(old);              // (a tile of the same key from an ended job: never two)
            tiles.Remove(key);
        }
        tiles[key] = tile;
        tile.Root.SetActive(true);
        j.Tile = tile;
        st.Tile = tile;
        st.Mesh = j.Mesh;
        st.Content = j.Content;
        st.State = StreamBuilt;
        st.BuiltMs = stream.Clock.Elapsed.TotalMilliseconds;
        HandTileTextures(st, tile, j);
        KeepOnlyWhatAViewMayUpload(tile, j.Content);
        if (shown == null || shown.Disposed)
        {
            // Every built tile had closed (the camera flew on past them): this one is "shown" now.
            shown = tile;
            shownLoad = cmd.Load;
            shownMesh = j.Mesh;
            shownContent = j.Content;
        }
        j.Phase = Phase.Adopted;
        j.AdoptFrame = frame;
        j.AdoptedMs = j.Clock.Elapsed.TotalMilliseconds;
        Log("load {0}: tile {1} of the session added to the scene in {2:0.###} ms ({3} chunk object(s), {4} texture(s) bound, {5} of them the " +
            "scene's); live terrain tiles {6}, scene terrain textures {7}", cmd.Load, st.Name, j.UploadMs, tile.ChunkObjectCount, tile.Textures.Count,
            st.TerrainTextures.Count, WmvRuntimeTerrainTile.Live, terrainTextures.Count);
        if (j.World != null)
        {
            j.World.Committed = true;
            j.World.BeganMs = j.Clock.Elapsed.TotalMilliseconds;
            j.World.ShownMs = j.World.BeganMs;
            AdvanceWorld(j);
        }
    }

    // ---------------------------------------------------------------- ends of a tile

    /// <summary>A tile's job is done (its world models and doodads settled two frames ago): what it placed stays, held by the
    /// tile; the job goes (its parsed data and texture bytes with it).</summary>
    void SettleStreamJob(Job j)
    {
        StreamTile st = j.Stream;
        WorldWork w = j.World;
        int placed = 0, shared = 0;
        if (w != null)
        {
            st.WorldPlacements = w.Placements.Count;
            foreach (PlacementWork pw in w.Placements)
                if (pw.State == "placed")
                {
                    placed++;
                    if (pw.Shared) shared++;
                }
        }
        int dplaced = 0, dshared = 0;
        if (w != null && w.Doodads != null)
        {
            st.DoodadInstances = w.Doodads.Instances.Count;
            foreach (DoodadWork dw in w.Doodads.Instances)
                if (dw.State == "placed")
                {
                    dplaced++;
                    if (dw.Shared) dshared++;
                }
        }
        st.SettledMs = stream.Clock.Elapsed.TotalMilliseconds;
        j.Dead = true;
        Retire(j, "settled");
        Log("load {0}: tile {1} (load {2}) settled after {3:0.#} ms: terrain {4:0.###} ms; {5} of {6} world-model placement(s) placed ({7} " +
            "shared), {8} of {9} doodad(s) placed ({10} shared); {11} file(s) asked, {12} answered, {13} bytes; {14} request(s) out in the session; " +
            "{15} file(s) of the session asked more than once ({16}); doodad builds {17:0.###} ms (at most {18:0.###} in one), placements " +
            "{19:0.###} ms (at most {20:0.###})",
            stream.EntryLoad, st.Name, j.Cmd.Load, j.Clock.Elapsed.TotalMilliseconds, j.UploadMs, placed, st.WorldPlacements, shared, dplaced,
            st.DoodadInstances, dshared, st.FilesAsked, st.FilesAnswered, st.Bytes, stream.RequestsOut, stream.FilesAskedAgain, AskedAgainLine(stream),
            w != null && w.Doodads != null ? w.Doodads.BuildMs : 0, w != null && w.Doodads != null ? w.Doodads.BuildMaxMs : 0,
            w != null && w.Doodads != null ? w.Doodads.PlaceMs : 0, w != null && w.Doodads != null ? w.Doodads.PlaceMaxMs : 0);
        StreamJobEnded(j, StreamSettled, "");
    }

    /// <summary>The job of a tile is no longer the session's: settled (StreamSettled), failed (StreamFailed: a tile with no
    /// terrain is tried again later, one with terrain keeps it and what was placed; its tile load is closed on the host
    /// either way), or closed (null: the tile goes).</summary>
    void StreamJobEnded(Job j, string state, string reason)
    {
        if (stream != null)
            stream.PoolsDirty = true;           // (what its models pinned may be pooled now)
        StreamTile st = j.Stream;
        if (st == null || stream == null)
            return;
        Job x;
        if (stream.Jobs.TryGetValue(j.Cmd.Load, out x) && x == j)
            stream.Jobs.Remove(j.Cmd.Load);
        if (st.Job == j)
            st.Job = null;
        ReleaseJobTerrainTextures(j);
        if (state == StreamSettled)
            st.State = StreamSettled;
        else if (state == StreamFailed)
        {
            stream.TilesFailed++;
            st.Reason = reason ?? "";
            if (st.Tile != null)
                st.State = StreamBuilt;
            else
            {
                st.State = StreamFailed;
                st.RetryAtMs = stream.Clock.Elapsed.TotalMilliseconds + StreamRetryMs;
            }
            CloseHostLoad(st);
        }
    }

    /// <summary>The tile's load closed on the host (once): what it still had queued or in flight is stale from then on.</summary>
    void CloseHostLoad(StreamTile st)
    {
        if (st.TileLoad <= 0 || st.HostClosed || stream == null)
            return;
        st.HostClosed = true;
        stream.CloseQueue.Add(st.TileLoad);
        stream.TilesClosed++;
    }

    /// <summary>A tile's job failed (Fail of a job that is not the entry): no report -- streamState says it -- its requests
    /// retired; its tile, when it was committed, stays with what was placed.</summary>
    void FailStreamJob(Job j, string reason)
    {
        if (j.Dead)
            return;
        Log("load {0}: tile {1} (load {2}) FAILED: {3}", stream != null ? stream.EntryLoad : 0, j.Stream.Name, j.Cmd.Load, reason);
        j.Dead = true;
        Retire(j, "failed: " + reason);
        // (Adopted: its prepared texture data are its tile's content now -- what a later view may still upload.)
        if (j.Tile == null)
            ReleasePrepared(j);
        StreamJobEnded(j, StreamFailed, reason);
    }

    /// <summary>A tile leaves the region: its job ends (the entry's in flight is reported "superseded"), what it holds is given
    /// back -- an instance no other tile holds is destroyed --, its terrain goes, the host is told.</summary>
    void CloseStreamTile(StreamTile st, string why)
    {
        StreamWork s = stream;
        if (s != null)
            s.PoolsDirty = true;                // (what only it held is pooled now)
        Job j = st.Job;
        if (j != null)
        {
            if (j == current && s.Live && j.Tile != null && OwnsScene(j))
            {
                // The entry, committed (its tile was on screen), its world models or doodads still coming: reported built --
                // the session goes on (a "superseded" entry ends it for the host) -- and the camera it carried placed now
                // (it waited for the world models it will not get).
                current = null;
                j.FirstFrameMs = j.Clock.Elapsed.TotalMilliseconds - j.AdoptedMs;
                try { worldView.WorldModelsSettled(world, j.Cmd.Load); }
                catch (Exception e) { Log("load {0}: placing the camera as the entry closed failed: {1}", j.Cmd.Load, e.Message); }
                Report(j, "built", "on screen, then closed by the streaming session before its world models and doodads settled: " + why);
            }
            else if (j == current)
                Supersede(j, "closed by the streaming session: " + why);
            else if (!j.Dead)
            {
                j.Dead = true;
                Retire(j, "closed: " + why);
            }
            ReleasePrepared(j);
            StreamJobEnded(j, null, null);
            if (stream != s)
                return;
        }
        int placements = st.Placements.Count, doodads = st.Doodads.Count;
        int destroyed = ReleaseTileHoldings(st);
        foreach (uint id in st.TerrainTextures)
            ReleaseTerrainTexture(id);
        st.TerrainTextures.Clear();
        if (st.Tile != null)
        {
            WmvRuntimeTerrainTile t = st.Tile;
            foreach (uint id in new List<uint>(t.Textures.Keys))
            {
                TerrainTexEntry e;
                if (terrainTextures.TryGetValue(id, out e) && e.Texture == t.Textures[id])
                    t.Textures.Remove(id);      // the scene's: never destroyed with the tile
            }
            view.DisposeTileLater(t);
            WmvRuntimeTerrainTile x;
            if (tiles.TryGetValue(st.Key, out x) && x == t)
                tiles.Remove(st.Key);
            if (shown == t)
                PickShown();
        }
        if (st.Content != null)
            foreach (WmvTerrainTexture tx in st.Content.Textures.Values)
                tx.ReleaseUploadData();
        st.Tile = null;
        st.Mesh = null;
        st.Content = null;
        s.Tiles.Remove(st.Key);
        s.AwaitingOpen.Remove(st.Key);
        CloseHostLoad(st);
        Log("load {0}: tile {1} (load {2}) closed: {3}; it held {4} placement(s) and {5} doodad(s), {6} instance(s) destroyed with it; live " +
            "terrain tiles {7}, world instances {8}, doodads {9}", s.EntryLoad, st.Name, st.TileLoad, why, placements, doodads, destroyed,
            WmvRuntimeTerrainTile.Live, world != null ? world.Instances.Count : 0, world != null ? world.Doodads.Count : 0);
    }

    /// <summary>setTerrainView in a session: every resident tile but `done` (the shown one, rebuilt already) is drawn again in
    /// the view now in effect, one a frame (RebuildNextStreamTile), nearest first.</summary>
    void QueueStreamRebuild(WmvRuntimeTerrainTile done)
    {
        if (stream == null)
            return;
        int n = 0;
        foreach (StreamTile st in stream.Tiles.Values)
            if (st.Tile != null && !st.Tile.Disposed && st.Tile != done)
            {
                st.RebuildDue = true;
                n++;
            }
        if (n > 0)
            Log("load {0}: {1} more tile(s) of the session are drawn again in the view now in effect, one a frame", stream.EntryLoad, n);
    }

    /// <summary>One resident tile drawn again in the view now in effect, from its kept mesh and content (RebuildShown's way: the
    /// textures it binds handed to the new tile or left the scene's; the old tile goes with what it alone holds).</summary>
    void RebuildNextStreamTile()
    {
        StreamTile next = null;
        foreach (StreamTile st in Ordered())
            if (st.RebuildDue)
            {
                next = st;
                break;
            }
        if (next == null)
            return;
        next.RebuildDue = false;
        WmvRuntimeTerrainTile old = next.Tile;
        if (old == null || old.Disposed || next.Mesh == null || next.Content == null)
            return;
        var clock = Stopwatch.StartNew();
        var tile = new WmvRuntimeTerrainTile(old.MapId, old.TileX, old.TileY, old.Load);
        foreach (KeyValuePair<uint, UnityEngine.Texture2D> kv in old.Textures)
            tile.Textures[kv.Key] = kv.Value;    // bound, not tracked: the old tile's or the scene's
        try
        {
            view.BuildTile(tile, next.Mesh, next.Content);
        }
        catch (Exception e)
        {
            tile.Textures.Clear();
            try { view.DisposeTile(tile); }
            catch (Exception e2) { Log("rebuild of tile {0}: disposing the unfinished tile failed: {1}", next.Name, e2.Message); }
            Log("rebuild of tile {0} failed: {1}: {2}; it stays as it was", next.Name, e.GetType().Name, e.Message);
            return;
        }
        StreamWorked();
        foreach (KeyValuePair<uint, UnityEngine.Texture2D> kv in new List<KeyValuePair<uint, UnityEngine.Texture2D>>(tile.Textures))
        {
            long gpu;
            if (old.Hand(kv.Value, out gpu))
                tile.Track(kv.Value, gpu);
        }
        view.DisposeTile(old);
        WmvRuntimeTerrainTile x;
        if (tiles.TryGetValue(next.Key, out x) && x == old)
            tiles[next.Key] = tile;
        next.Tile = tile;
        if (shown == old)
            shown = tile;
        tile.Root.SetActive(true);
        KeepOnlyWhatAViewMayUpload(tile, next.Content);
        Log("rebuild of tile {0} (load {1}): drawn {2} in {3:0.###} ms", next.Name, next.TileLoad, WmvTerrainDebug.ViewNames[(int)tile.View],
            clock.Elapsed.TotalMilliseconds);
    }

    /// <summary>The entry's tile closed: the nearest resident tile is the one "shown" (the camera's frame and the capture's
    /// name); none when no tile is resident.</summary>
    void PickShown()
    {
        // (The entry's inputs are its tile's own: the tile has gone, and its content with it.)
        shownContent = null;
        ForgetShownInputs();
        shown = null;
        shownLoad = 0;
        foreach (StreamTile st in Ordered())
            if (st.Tile != null && !st.Tile.Disposed)
            {
                shown = st.Tile;
                shownLoad = st.TileLoad;
                shownMesh = st.Mesh;
                shownContent = st.Content;
                return;
            }
    }

    /// <summary>A tile gives back the placements and doodads it holds; returns the instances destroyed (no tile held them).</summary>
    int ReleaseTileHoldings(StreamTile st)
    {
        int destroyed = 0;
        var goneDoodads = new List<WmvDoodadInstance>();
        foreach (DoodadEntry e in st.Doodads)
        {
            e.Holders.Remove(st);
            if (e.Holders.Count > 0)
                continue;
            DoodadEntry registered;
            if (stream.Doodads.TryGetValue(e.Key, out registered) && registered == e)
                stream.Doodads.Remove(e.Key);
            if (e.Instance != null)
                goneDoodads.Add(e.Instance);
            if (e.Proto != null)
                e.Proto.LastUse = ++stream.Use;
        }
        st.Doodads.Clear();
        if (goneDoodads.Count > 0 && world != null)
        {
            var set = new HashSet<WmvDoodadInstance>(goneDoodads);
            world.Doodads.RemoveAll(d => set.Contains(d));
            try { worldView.DisposeDoodadInstances(goneDoodads); }
            catch (Exception ex) { Log("disposing doodads failed: {0}", ex.Message); }
            destroyed += goneDoodads.Count;
        }
        foreach (PlacementEntry e in st.Placements)
        {
            e.Holders.Remove(st);
            if (e.Holders.Count > 0)
                continue;
            PlacementEntry registeredPlacement;
            if (stream.Placements.TryGetValue(e.Key, out registeredPlacement) && registeredPlacement == e)
                stream.Placements.Remove(e.Key);
            if (world != null)
            {
                if (e.Instance != null)
                    world.Instances.Remove(e.Instance);
                if (e.Geometry != null)
                    world.Geometries.Remove(e.Geometry);
            }
            if (e.Instance != null)
            {
                try { worldView.DisposeInstance(e.Instance); }
                catch (Exception ex) { Log("disposing a world-model instance failed: {0}", ex.Message); }
                destroyed++;
            }
            if (e.Proto != null)
                e.Proto.LastUse = ++stream.Use;
        }
        st.Placements.Clear();
        return destroyed;
    }

    // ---------------------------------------------------------------- shared prototypes

    static ProtoEntry Find(Dictionary<uint, ProtoEntry> d, uint id)
    {
        ProtoEntry e;
        return d.TryGetValue(id, out e) ? e : null;
    }

    static int RingOf(Job j) { return j != null && j.Stream != null ? j.Stream.Ring : 0; }

    void Pin(ProtoEntry e)
    {
        e.Pins++;
        e.LastUse = ++stream.Use;
    }

    void Unpin(ProtoEntry e)
    {
        if (e == null)
            return;
        if (e.Pins > 0)
            e.Pins--;
        if (stream != null)
        {
            e.LastUse = ++stream.Use;
            if (IsPrivate(e) && e.Pins <= 0)
                stream.PoolsDirty = true;      // (no job places from it now: it goes once no instance shows it)
        }
    }

    /// <summary>A job's queued world models meet the session's prototypes before anything is fetched: one built is placed from
    /// (no fetch), one another tile fetches is waited for (a near tile takes over a far tile's fetch not yet begun), one that
    /// failed for good fails here too; the first to need one fetches it.</summary>
    void ClaimWorldModels(Job j)
    {
        WorldWork w = j.World;
        foreach (Model m in w.Models)
        {
            if (m.State != ModelState.Queued || m.Claimed)
                continue;
            m.Claimed = true;
            ProtoEntry e = Find(stream.Wmos, m.FileDataID);
            if (e == null)
            {
                e = new ProtoEntry { FileDataID = m.FileDataID, Owner = m };
                stream.Wmos[m.FileDataID] = e;
                Pin(e);
                m.Shared = e;
                continue;
            }
            m.Shared = e;
            Pin(e);
            if (e.State == "built" && e.Wmo != null && !e.Wmo.Disposed)
            {
                stream.PrototypesShared++;
                m.Built = e.Wmo;
                m.State = ModelState.Ready;
                w.BuildQueue.Enqueue(m);
            }
            else if (e.State == "failed")
            {
                FailModel(j, m, e.Reason + " (as fetched for another tile of the session)");
            }
            else if (e.Owner == null || !Live(e.Owner.Job))
            {
                e.Owner = m;
                stream.PrototypeTakeovers++;
            }
            else if (e.Owner.State == ModelState.Queued && RingOf(j) < RingOf(e.Owner.Job))
            {
                // The owner has not begun and is farther: this tile fetches, the owner waits.
                Model was = e.Owner;
                e.Owner = m;
                was.State = ModelState.Borrowed;
                e.Borrowers.Add(was);
                stream.PrototypeTakeovers++;
            }
            else
            {
                m.State = ModelState.Borrowed;
                e.Borrowers.Add(m);
                stream.PrototypesShared++;
            }
        }
    }

    /// <summary>The owner built its prototype: the session's now; every model waiting for it is placed from it.</summary>
    void WorldPrototypeBuilt(Model m, WmvWorldPrototype proto)
    {
        ProtoEntry e = m.Shared;
        if (e == null || e.Owner != m)
            return;
        e.State = "built";
        e.Wmo = proto;
        e.Owner = null;
        e.ManagedBytes = m.Bytes;
        foreach (Model b in e.Borrowers)
        {
            if (b.State != ModelState.Borrowed || !Live(b.Job))
                continue;
            b.Built = proto;
            b.State = ModelState.Ready;
            b.Job.World.BuildQueue.Enqueue(b);
        }
        e.Borrowers.Clear();
    }

    /// <summary>Streaming: the host's answer says nothing about the file itself -- refused for the tile's load (its budgets,
    /// the queue's room) or stale --, so a model failing on it fails for that tile only: its fetch is handed on, never
    /// remembered as the model's failure for the session.</summary>
    static bool RefusedForTheLoad(WmvIpcClient.AssetResponse r)
    {
        return r != null && !r.ok && (r.state == "refused" || r.state == "stale");
    }

    /// <summary>A failure that is the tile's, not the file's (see RefusedForTheLoad): handed on as a timed-out fetch is.</summary>
    void FailForTheLoad(Action fail)
    {
        bool was = streamTransientFailure;
        streamTransientFailure = true;
        try { fail(); }
        finally { streamTransientFailure = was; }
    }

    /// <summary>A world model of a session's job settles without a prototype (SettleModel): as the owner, its fetch is handed
    /// to a waiting model (it ended, timed out or passed its job's budget) or fails them all (the file's own failure); as a
    /// borrower it stops waiting.</summary>
    void WorldModelSettled(Job j, Model m, ModelState state, string reason)
    {
        ProtoEntry e = m.Shared;
        if (e == null || stream == null)
            return;
        e.Borrowers.Remove(m);
        if (e.Owner == m)
        {
            e.Owner = null;
            bool transient = streamTransientFailure || state == ModelState.NotRequested || j.Dead || !Live(j);
            if (!transient)
            {
                e.State = "failed";
                e.Reason = reason;
                var waiting = new List<Model>(e.Borrowers);
                e.Borrowers.Clear();
                foreach (Model b in waiting)
                    if (Live(b.Job) && b.State == ModelState.Borrowed)
                        FailModel(b.Job, b, reason + " (as fetched for another tile of the session)");
            }
            else
                HandOverWorldFetch(e);
        }
        if (!m.Unpinned)
        {
            m.Unpinned = true;
            Unpin(e);
        }
    }

    /// <summary>A fetch whose owner went: the first live waiting model takes it (queued again in its job); none -- the entry
    /// goes (nobody needs it).</summary>
    void HandOverWorldFetch(ProtoEntry e)
    {
        while (e.Borrowers.Count > 0)
        {
            Model b = e.Borrowers[0];
            e.Borrowers.RemoveAt(0);
            if (!Live(b.Job) || b.State != ModelState.Borrowed)
                continue;
            e.Owner = b;
            b.State = ModelState.Queued;
            stream.PrototypeTakeovers++;
            Log("load {0}: world model {1}: its fetch is taken over by tile {2} (load {3})", stream.EntryLoad, e.FileDataID, b.Job.Stream.Name,
                b.Job.Cmd.Load);
            AdvanceOther(() => AdvanceWorld(b.Job));
            return;
        }
        if (e.State == "fetching" && Find(stream.Wmos, e.FileDataID) == e)
            stream.Wmos.Remove(e.FileDataID);
    }

    void DoodadClaim(Job j, DoodadModel m)
    {
        DoodadsWork d = j.World.Doodads;
        m.Claimed = true;
        ProtoEntry e = Find(stream.M2s, m.FileDataID);
        if (e == null)
        {
            e = new ProtoEntry { Doodad = true, FileDataID = m.FileDataID, DOwner = m };
            stream.M2s[m.FileDataID] = e;
            Pin(e);
            m.Shared = e;
            return;
        }
        m.Shared = e;
        Pin(e);
        if (e.State == "built" && e.M2 != null && !e.M2.Disposed)
        {
            stream.PrototypesShared++;
            m.Built = e.M2;
            m.State = DoodadModelState.Ready;
            d.BuildQueue.Enqueue(m);
        }
        else if (e.State == "failed")
            SettleDoodadModel(j, m, e.FailState >= 0 ? (DoodadModelState)e.FailState : DoodadModelState.Failed,
                              e.Reason + " (as fetched for another tile of the session)");
        else if (e.DOwner == null || !Live(e.DOwner.Job))
        {
            e.DOwner = m;
            stream.PrototypeTakeovers++;
        }
        else if (e.DOwner.State == DoodadModelState.Queued && RingOf(j) < RingOf(e.DOwner.Job))
        {
            DoodadModel was = e.DOwner;
            e.DOwner = m;
            was.State = DoodadModelState.Borrowed;
            e.DBorrowers.Add(was);
            stream.PrototypeTakeovers++;
        }
        else
        {
            m.State = DoodadModelState.Borrowed;
            e.DBorrowers.Add(m);
            stream.PrototypesShared++;
        }
    }

    void ClaimDoodadModels(Job j)
    {
        foreach (DoodadModel m in j.World.Doodads.Models)
            if (m.State == DoodadModelState.Queued && !m.Claimed)
                DoodadClaim(j, m);
    }

    void DoodadPrototypeBuilt(DoodadModel m, WmvDoodadPrototype proto)
    {
        ProtoEntry e = m.Shared;
        if (e == null || e.DOwner != m)
            return;
        e.State = "built";
        e.M2 = proto;
        e.DOwner = null;
        foreach (DoodadModel b in e.DBorrowers)
        {
            if (b.State != DoodadModelState.Borrowed || !Live(b.Job))
                continue;
            b.Built = proto;
            b.State = DoodadModelState.Ready;
            b.Job.World.Doodads.BuildQueue.Enqueue(b);
        }
        e.DBorrowers.Clear();
    }

    void DoodadModelSettledShared(Job j, DoodadModel m, DoodadModelState state, string reason)
    {
        ProtoEntry e = m.Shared;
        if (e == null || stream == null)
            return;
        e.DBorrowers.Remove(m);
        if (e.DOwner == m)
        {
            e.DOwner = null;
            bool transient = streamTransientFailure || state == DoodadModelState.NotRequested || j.Dead || !Live(j);
            if (!transient)
            {
                e.State = "failed";
                e.Reason = reason;
                e.FailState = (int)state;
                var waiting = new List<DoodadModel>(e.DBorrowers);
                e.DBorrowers.Clear();
                foreach (DoodadModel b in waiting)
                    if (Live(b.Job) && b.State == DoodadModelState.Borrowed)
                        SettleDoodadModel(b.Job, b, state, reason + " (as fetched for another tile of the session)");
            }
            else
                HandOverDoodadFetch(e);
        }
        if (!m.Unpinned)
        {
            m.Unpinned = true;
            Unpin(e);
        }
    }

    void HandOverDoodadFetch(ProtoEntry e)
    {
        while (e.DBorrowers.Count > 0)
        {
            DoodadModel b = e.DBorrowers[0];
            e.DBorrowers.RemoveAt(0);
            if (!Live(b.Job) || b.State != DoodadModelState.Borrowed)
                continue;
            e.DOwner = b;
            b.State = DoodadModelState.Queued;
            stream.PrototypeTakeovers++;
            AdvanceOther(() => AdvanceDoodads(b.Job));
            return;
        }
        if (e.State == "fetching" && Find(stream.M2s, e.FileDataID) == e)
            stream.M2s.Remove(e.FileDataID);
    }

    /// <summary>Streaming: the owner built its world model without part of it that its own tile's load could not get (a group
    /// file or texture refused or stale for that load, or past its count cap). The prototype is that tile's alone -- drawn as
    /// the single-tile load draws it, placed for that tile only (its placements register private), never in the scene's
    /// dictionary, never published, never pooled. The model's one pin moves to a private record (released where the model's
    /// pin is: BuildModel, ReleaseJobModels); the session's fetch is handed on: a waiting model fetches the whole model under
    /// its own load; none -- the "fetching" entry goes, and whoever needs the model next fetches it. The record goes once it
    /// has neither pin nor instance (DisposeUnusedPrivate), or with the scene. Called before PlaceAll.</summary>
    void KeepWorldModelForTheTile(Job j, Model m, WmvWorldPrototype proto, string why)
    {
        ProtoEntry e = m.Shared;
        bool pinned = !m.Unpinned;
        var p = new ProtoEntry { FileDataID = m.FileDataID, State = ProtoPrivate, Reason = why, Wmo = proto, Pins = pinned ? 1 : 0,
                                 LastUse = ++stream.Use };
        // On the scene's list and the model's record BEFORE the hand-over: that advances another tile's job at once.
        stream.Private.Add(p);
        world.PrivatePrototypes.Add(proto);
        stream.PrototypesKept++;
        int times = NoteKeptBuild(false, m.FileDataID);
        m.Shared = p;
        m.Reason = why;
        string next = "no tile waits for it";
        if (e != null)
        {
            e.Borrowers.Remove(m);
            if (pinned)
                Unpin(e);                       // (its pin is the private record's now; m.Unpinned stays false for it)
            if (e.Owner == m)
            {
                e.Owner = null;
                HandOverWorldFetch(e);
                if (e.Owner != null && e.Owner.Job != null && e.Owner.Job.Stream != null)
                    next = "its fetch taken over by tile " + e.Owner.Job.Stream.Name;
            }
        }
        Log("load {0}: world model {1} kept for tile {2} alone ({3}); {4}{5}", stream.EntryLoad, m.FileDataID, j.Stream.Name, why, next,
            times > 2 ? "; kept " + times + " times in this session -- no tile's load so far could have it whole" : "");
    }

    /// <summary>As KeepWorldModelForTheTile, for an M2: its doodads register private; its pin goes at CheckDoodadsDone (its
    /// doodads are placed over later frames) or ReleaseJobModels, as before.</summary>
    void KeepDoodadModelForTheTile(Job j, DoodadModel m, WmvDoodadPrototype proto, string why)
    {
        ProtoEntry e = m.Shared;
        bool pinned = !m.Unpinned;
        var p = new ProtoEntry { Doodad = true, FileDataID = m.FileDataID, State = ProtoPrivate, Reason = why, M2 = proto,
                                 Pins = pinned ? 1 : 0, LastUse = ++stream.Use };
        stream.Private.Add(p);
        world.PrivateDoodadPrototypes.Add(proto);
        stream.PrototypesKept++;
        int times = NoteKeptBuild(true, m.FileDataID);
        m.Shared = p;
        string next = "no tile waits for it";
        if (e != null)
        {
            e.DBorrowers.Remove(m);
            if (pinned)
                Unpin(e);
            if (e.DOwner == m)
            {
                e.DOwner = null;
                HandOverDoodadFetch(e);
                if (e.DOwner != null && e.DOwner.Job != null && e.DOwner.Job.Stream != null)
                    next = "its fetch taken over by tile " + e.DOwner.Job.Stream.Name;
            }
        }
        Log("load {0}: doodad model {1} kept for tile {2} alone ({3}); {4}{5}", stream.EntryLoad, m.FileDataID, j.Stream.Name, why, next,
            times > 2 ? "; kept " + times + " times in this session -- no tile's load so far could have it whole" : "");
    }

    /// <summary>How many times this session kept a prototype of the FileDataID for its tile (the evidence for a model no
    /// single load can have whole).</summary>
    int NoteKeptBuild(bool doodad, uint fileDataID)
    {
        long key = ((doodad ? 1L : 0L) << 32) | fileDataID;
        int n;
        stream.KeptBuilds.TryGetValue(key, out n);
        stream.KeptBuilds[key] = ++n;
        return n;
    }

    /// <summary>A job ends (Retire -> EndWorld): every model of it lets go of the session's prototypes -- a fetch it owned is
    /// handed on, a wait ends, its pins go.</summary>
    void ReleaseJobModels(Job j)
    {
        if (stream == null || j.World == null)
            return;
        ReleaseJobImages(j);
        bool was = streamTransientFailure;
        streamTransientFailure = true;
        try
        {
            foreach (Model m in j.World.Models)
            {
                ProtoEntry e = m.Shared;
                if (e == null)
                    continue;
                e.Borrowers.Remove(m);
                if (e.Owner == m)
                {
                    e.Owner = null;
                    HandOverWorldFetch(e);
                }
                if (!m.Unpinned)
                {
                    m.Unpinned = true;
                    Unpin(e);
                }
            }
            if (j.World.Doodads != null)
                foreach (DoodadModel m in j.World.Doodads.Models)
                {
                    ProtoEntry e = m.Shared;
                    if (e == null)
                        continue;
                    e.DBorrowers.Remove(m);
                    if (e.DOwner == m)
                    {
                        e.DOwner = null;
                        HandOverDoodadFetch(e);
                    }
                    if (!m.Unpinned)
                    {
                        m.Unpinned = true;
                        Unpin(e);
                    }
                }
        }
        finally { streamTransientFailure = was; }
    }

    // ---------------------------------------------------------------- shared placements

    static long PlacementKey(WorldModelPlacement p) { return ((long)p.FileDataID << 32) | p.UniqueId; }

    /// <summary>A placement of `j`'s tile another tile has placed already: found (held by this tile too), nothing made. One
    /// placed from a tile's private prototype (KeepWorldModelForTheTile) is found only by a tile whose own prototype is
    /// private too (`complete` false: one instance per placement, whatever the gaps); a tile with the complete prototype
    /// places its own, and its registration replaces that one (`replace`, RegisterPlacement).</summary>
    bool FindSharedPlacement(Job j, PlacementWork pw, bool complete, out PlacementEntry replace)
    {
        replace = null;
        PlacementEntry e;
        if (!stream.Placements.TryGetValue(PlacementKey(pw.P), out e) || e.Instance == null || e.Instance.Disposed)
            return false;
        if (complete && IsPrivate(e.Proto))
        {
            replace = e;
            return false;
        }
        pw.Instance = e.Instance;
        pw.State = "placed";
        pw.Shared = true;
        pw.Reason = "placed for tile " + (e.PlacedBy != null ? e.PlacedBy.Name : "?") + " of the session";
        if (!e.Holders.Contains(j.Stream))
        {
            e.Holders.Add(j.Stream);
            j.Stream.Placements.Add(e);
        }
        stream.PlacementsShared++;
        return true;
    }

    void RegisterPlacement(Job j, PlacementWork pw, WorldModelGeometry g, ProtoEntry proto, PlacementEntry replace)
    {
        var e = new PlacementEntry { Key = PlacementKey(pw.P), Instance = pw.Instance, Geometry = g, Proto = proto, PlacedBy = j.Stream };
        e.Holders.Add(j.Stream);
        PlacementEntry was;
        if (replace == null && stream.Placements.TryGetValue(e.Key, out was) && was.Instance != null && !was.Instance.Disposed)
            Log("load {0}: placement {1} of world model {2} registered over a live one it does not replace (tile {3}) -- a fault of " +
                "this viewer", stream.EntryLoad, pw.P.UniqueId, pw.P.FileDataID, j.Stream.Name);
        stream.Placements[e.Key] = e;
        j.Stream.Placements.Add(e);
        if (replace != null)
            ReplacePlacement(j, replace, e);
    }

    /// <summary>A complete placement registered over one placed from a tile's private prototype: every tile that held that one
    /// holds this one (a live job's plan pointed at it: StartDoodads, the report), and that one is destroyed.</summary>
    void ReplacePlacement(Job j, PlacementEntry old, PlacementEntry e)
    {
        foreach (StreamTile h in old.Holders)
        {
            int at = h.Placements.IndexOf(old);
            if (e.Holders.Contains(h))
            {
                if (at >= 0) h.Placements.RemoveAt(at);
            }
            else
            {
                e.Holders.Add(h);
                if (at >= 0) h.Placements[at] = e;
                else h.Placements.Add(e);         // (Holders and the tile's list always mirror each other)
            }
            Job hj = h.Job;
            if (hj != null && Live(hj) && hj.World != null)
                foreach (PlacementWork other in hj.World.Placements)
                    if (other.Instance == old.Instance)
                    {
                        other.Instance = e.Instance;
                        other.Shared = true;
                        other.Reason = "placed for tile " + j.Stream.Name + " of the session from the complete world model, in " +
                                       "place of this tile's own (built without what its load could not get)";
                    }
        }
        old.Holders.Clear();
        if (world != null)
        {
            world.Instances.Remove(old.Instance);
            if (old.Geometry != null)
                world.Geometries.Remove(old.Geometry);
        }
        try { worldView.DisposeInstance(old.Instance); }
        catch (Exception ex) { Log("replacing a world-model instance failed: {0}", ex.Message); }
        old.Instance = null;
        stream.PlacementsReplaced++;
        stream.PoolsDirty = true;              // (its private prototype may show nothing now: DisposeUnusedPrivate)
    }

    static DoodadKey KeyOfDoodad(WorldDoodad d)
    {
        if (d.Source == DoodadSource.Mddf)
            return new DoodadKey { Kind = 1, A = d.FileDataID, B = d.UniqueId, C = 0 };
        return new DoodadKey { Kind = 2, A = d.WmoFileDataID, B = d.UniqueId, C = d.ModdIndex };
    }

    bool FindSharedDoodad(Job j, DoodadWork dw, bool complete, out DoodadEntry replace)
    {
        replace = null;
        DoodadEntry e;
        if (!stream.Doodads.TryGetValue(KeyOfDoodad(dw.D), out e) || e.Instance == null || e.Instance.Disposed)
            return false;
        if (complete && IsPrivate(e.Proto))
        {
            replace = e;
            return false;
        }
        dw.Instance = e.Instance;
        dw.State = "placed";
        dw.Shared = true;
        if (!e.Holders.Contains(j.Stream))
        {
            e.Holders.Add(j.Stream);
            j.Stream.Doodads.Add(e);
        }
        stream.DoodadsShared++;
        return true;
    }

    void RegisterDoodad(Job j, DoodadWork dw, DoodadEntry replace)
    {
        var e = new DoodadEntry { Key = KeyOfDoodad(dw.D), Instance = dw.Instance, Proto = dw.Model != null ? dw.Model.Shared : null };
        e.Holders.Add(j.Stream);
        stream.Doodads[e.Key] = e;
        j.Stream.Doodads.Add(e);
        if (replace != null)
            ReplaceDoodad(replace, e);
    }

    /// <summary>As ReplacePlacement, for a doodad. (DoodadWork.Instance is never read after RegisterDoodad: no plan to
    /// re-point.)</summary>
    void ReplaceDoodad(DoodadEntry old, DoodadEntry e)
    {
        foreach (StreamTile h in old.Holders)
        {
            int at = h.Doodads.IndexOf(old);
            if (e.Holders.Contains(h))
            {
                if (at >= 0) h.Doodads.RemoveAt(at);
            }
            else
            {
                e.Holders.Add(h);
                if (at >= 0) h.Doodads[at] = e;
                else h.Doodads.Add(e);
            }
        }
        old.Holders.Clear();
        if (world != null)
            world.Doodads.Remove(old.Instance);
        try { worldView.DisposeDoodadInstances(new List<WmvDoodadInstance> { old.Instance }); }
        catch (Exception ex) { Log("replacing a doodad failed: {0}", ex.Message); }
        old.Instance = null;
        stream.DoodadsReplaced++;
        stream.PoolsDirty = true;
    }

    // ---------------------------------------------------------------- shared texture images

    static long ImageKey(bool wmo, uint id) { return (wmo ? 1L << 32 : 0L) | id; }

    /// <summary>A world-model texture a job's model needs, not yet in the job: true when the job asks nothing for it -- the
    /// session's image attached (t.Done), its failure taken, or another tile's fetch waited for (t.Asked, not done) --,
    /// false when the job fetches it (it is the owner now).</summary>
    bool ClaimWmoTexture(Job j, SharedTexture t)
    {
        if (j.Stream == null || stream == null)
            return false;
        long key = ImageKey(true, t.Id);
        ImageEntry e;
        if (!stream.Images.TryGetValue(key, out e))
        {
            stream.Images[key] = new ImageEntry { Wmo = true, Id = t.Id, Owner = j, LastUse = ++stream.Use };
            return false;
        }
        e.LastUse = ++stream.Use;
        if (TakeSettledWmoImage(j, t, e))
            return true;
        if (e.Owner == null || !Live(e.Owner))
        {
            e.Owner = j;
            stream.ImageTakeovers++;
            return false;
        }
        if (!e.WaitJobs.Contains(j))
            e.WaitJobs.Add(j);
        t.Asked = true;
        stream.ImagesWaited++;
        return true;
    }

    /// <summary>The session's settled image of a world-model texture taken without asking: "ready" -- attached (done,
    /// decoded); "failed" -- its file's failure taken (done, missing). False, nothing changed, while it is still fetching.
    /// (The one implementation: ClaimWmoTexture, and TakeSessionWmoImage at the load's count cap.)</summary>
    bool TakeSettledWmoImage(Job j, SharedTexture t, ImageEntry e)
    {
        if (e.State == "ready" && e.Image != null)
        {
            AttachWmoImage(j, t, e);
            t.Done = true;
            j.World.TexturesDecoded++;
            stream.ImagesShared++;
            return true;
        }
        if (e.State == "failed")
        {
            t.Texture.Error = e.Error + " (as fetched for another tile of the session)";
            t.Done = true;
            j.World.TexturesMissing++;
            return true;
        }
        return false;
    }

    /// <summary>Streaming, at the load's count cap (maxWmoTexturesPerLoad): what the session has settled of the texture -- its
    /// image or its file's failure -- taken without asking (nothing is asked, so the cap does not apply); false when it has
    /// neither (and outside a session): the texture is not requested.</summary>
    bool TakeSessionWmoImage(Job j, SharedTexture t)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(true, t.Id), out e) || !TakeSettledWmoImage(j, t, e))
            return false;
        e.LastUse = ++stream.Use;
        return true;
    }

    /// <summary>A texture whose pixels the job let go, needed again by a later model of the job: the session's image when it
    /// keeps one (true), else false.</summary>
    bool ReattachWmoImage(Job j, SharedTexture t)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(true, t.Id), out e) || e.State != "ready" || e.Image == null)
            return false;
        e.LastUse = ++stream.Use;
        AttachWmoImage(j, t, e);
        return true;
    }

    void AttachWmoImage(Job j, SharedTexture t, ImageEntry e)
    {
        WmvWmoTexture tex = t.Texture;
        tex.Image = e.Image;
        tex.Error = null;
        tex.Decoded = true;
        tex.Width = e.Image.Width;
        tex.Height = e.Image.Height;
        tex.Encoding = e.Image.Encoding;
        if (!e.Takers.Contains(j))
            e.Takers.Add(j);
        j.World.PixelBytesHeld += e.Image.HeldBytes;
        j.World.PixelBytesHeldMax = Math.Max(j.World.PixelBytesHeldMax, j.World.PixelBytesHeld);
    }

    /// <summary>The owner's world-model texture is in (decoded, or not): the session's now, and every job waiting for it
    /// goes on. A fetch the host did not serve (fetched false) is handed to the next waiting job, which asks under its own
    /// tile load; a file that came and could not be decoded fails them all.</summary>
    void PublishWmoImage(Job j, SharedTexture t, BlpImage img, string error, bool fetched)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(true, t.Id), out e) || e.Owner != j)
            return;
        e.Owner = null;
        e.LastUse = ++stream.Use;
        if (img == null && !fetched)
        {
            HandOverImage(e);
            return;
        }
        e.State = img != null ? "ready" : "failed";
        e.Image = img;
        e.Error = error ?? "";
        var waiting = new List<Job>(e.WaitJobs);
        e.WaitJobs.Clear();
        foreach (Job wj in waiting)
        {
            SharedTexture wt;
            if (!Live(wj) || wj.World == null || !wj.World.Textures.TryGetValue(t.Id, out wt) || wt.Done)
                continue;
            if (img != null)
                AttachWmoImage(wj, wt, e);
            else
                wt.Texture.Error = e.Error + " (as fetched for another tile of the session)";
            TextureDone(wj, wt);
            AdvanceOther(() => AdvanceWorld(wj));
        }
    }

    bool ClaimDoodadTexture(Job j, DoodadTexture t)
    {
        if (j.Stream == null || stream == null)
            return false;
        long key = ImageKey(false, t.Id);
        ImageEntry e;
        if (!stream.Images.TryGetValue(key, out e))
        {
            stream.Images[key] = new ImageEntry { Wmo = false, Id = t.Id, Owner = j, LastUse = ++stream.Use };
            return false;
        }
        e.LastUse = ++stream.Use;
        if (TakeSettledDoodadImage(j, t, e))
            return true;
        if (e.Owner == null || !Live(e.Owner))
        {
            e.Owner = j;
            stream.ImageTakeovers++;
            return false;
        }
        if (!e.WaitJobs.Contains(j))
            e.WaitJobs.Add(j);
        t.Asked = true;
        stream.ImagesWaited++;
        return true;
    }

    /// <summary>As TakeSettledWmoImage, for a doodad texture (ClaimDoodadTexture, TakeSessionDoodadImage).</summary>
    bool TakeSettledDoodadImage(Job j, DoodadTexture t, ImageEntry e)
    {
        if (e.State == "ready" && e.Image != null)
        {
            AttachDoodadImage(j, t, e);
            t.Done = true;
            j.World.Doodads.TexturesDecoded++;
            stream.ImagesShared++;
            return true;
        }
        if (e.State == "failed")
        {
            t.Error = e.Error + " (as fetched for another tile of the session)";
            t.Done = true;
            j.World.Doodads.TexturesMissing++;
            return true;
        }
        return false;
    }

    /// <summary>As TakeSessionWmoImage, at maxM2TexturesPerLoad.</summary>
    bool TakeSessionDoodadImage(Job j, DoodadTexture t)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(false, t.Id), out e) || !TakeSettledDoodadImage(j, t, e))
            return false;
        e.LastUse = ++stream.Use;
        return true;
    }

    bool ReattachDoodadImage(Job j, DoodadTexture t)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(false, t.Id), out e) || e.State != "ready" || e.Image == null)
            return false;
        e.LastUse = ++stream.Use;
        AttachDoodadImage(j, t, e);
        return true;
    }

    void AttachDoodadImage(Job j, DoodadTexture t, ImageEntry e)
    {
        t.Image = e.Image;
        t.Error = null;
        t.Decoded = true;
        if (!e.Takers.Contains(j))
            e.Takers.Add(j);
        HoldPixels(j.World.Doodads, e.Image);
    }

    void PublishDoodadImage(Job j, DoodadTexture t, BlpImage img, string error, bool fetched)
    {
        ImageEntry e;
        if (j.Stream == null || stream == null || !stream.Images.TryGetValue(ImageKey(false, t.Id), out e) || e.Owner != j)
            return;
        e.Owner = null;
        e.LastUse = ++stream.Use;
        if (img == null && !fetched)
        {
            HandOverImage(e);
            return;
        }
        e.State = img != null ? "ready" : "failed";
        e.Image = img;
        e.Error = error ?? "";
        var waiting = new List<Job>(e.WaitJobs);
        e.WaitJobs.Clear();
        foreach (Job wj in waiting)
        {
            DoodadTexture wt;
            if (!Live(wj) || wj.World == null || wj.World.Doodads == null || !wj.World.Doodads.Textures.TryGetValue(t.Id, out wt) || wt.Done)
                continue;
            if (img != null)
                AttachDoodadImage(wj, wt, e);
            else
                wt.Error = e.Error + " (as fetched for another tile of the session)";
            DoodadTextureDone(wj, wt, true);
            AdvanceOther(() => AdvanceDoodads(wj));
        }
    }

    /// <summary>A texture fetch whose owner went (it ended, timed out, or the host did not serve it): the first live waiting job
    /// fetches it under its own tile load; none -- the entry goes (whoever needs it next fetches it).</summary>
    void HandOverImage(ImageEntry e)
    {
        while (e.WaitJobs.Count > 0)
        {
            Job wj = e.WaitJobs[0];
            e.WaitJobs.RemoveAt(0);
            if (!Live(wj) || wj.World == null)
                continue;
            if (e.Wmo)
            {
                SharedTexture wt;
                if (!wj.World.Textures.TryGetValue(e.Id, out wt) || wt.Done)
                    continue;
                e.Owner = wj;
                stream.ImageTakeovers++;
                wj.World.TextureQueue.Enqueue(e.Id);
                AdvanceOther(() => AdvanceWorld(wj));
                return;
            }
            DoodadTexture dt;
            if (wj.World.Doodads == null || !wj.World.Doodads.Textures.TryGetValue(e.Id, out dt) || dt.Done)
                continue;
            e.Owner = wj;
            stream.ImageTakeovers++;
            wj.World.Doodads.TextureQueue.Enqueue(e.Id);
            AdvanceOther(() => AdvanceDoodads(wj));
            return;
        }
        if (e.State == "fetching")
            stream.Images.Remove(ImageKey(e.Wmo, e.Id));
    }

    /// <summary>A job ends or its fetches time out: the texture fetches it owns are handed on, its waits end.</summary>
    void ReleaseJobImages(Job j)
    {
        if (stream == null)
            return;
        foreach (ImageEntry e in new List<ImageEntry>(stream.Images.Values))
        {
            e.WaitJobs.Remove(j);
            e.Takers.Remove(j);
            if (e.Owner == j && e.State == "fetching")
            {
                e.Owner = null;
                HandOverImage(e);
            }
        }
    }

    /// <summary>The kept images within StreamImageBudget, least recently used out first (nothing waits on a ready one); never
    /// one a live job took without its bytes.</summary>
    void EvictImages()
    {
        long held = 0;
        foreach (ImageEntry e in stream.Images.Values)
            held += e.Bytes;
        if (held <= StreamImageBudget)
            return;
        var ready = new List<ImageEntry>();
        foreach (ImageEntry e in stream.Images.Values)
        {
            e.Takers.RemoveAll(x => !Live(x));
            if (e.State == "ready" && e.Takers.Count == 0)
                ready.Add(e);
        }
        ready.Sort((a, b) => a.LastUse.CompareTo(b.LastUse));
        foreach (ImageEntry e in ready)
        {
            if (held <= StreamImageBudget)
                break;
            held -= e.Bytes;
            stream.Images.Remove(ImageKey(e.Wmo, e.Id));
            stream.ImagesEvicted++;
        }
    }

    // ---------------------------------------------------------------- pools

    /// <summary>The private prototypes (KeepWorldModelForTheTile) no job will place from (no pin) and no instance shows:
    /// disposed, never pooled -- under the session's frame budget (at least one a frame; the rest next frame). Pins AND
    /// instances: an M2's doodads are placed over later frames, so a private M2 with no instance yet is still in use until
    /// its tile's CheckDoodadsDone.</summary>
    void DisposeUnusedPrivate()
    {
        int n = 0;
        for (int i = stream.Private.Count - 1; i >= 0; i--)
        {
            ProtoEntry p = stream.Private[i];
            if (p.Pins > 0 || p.Instances > 0)
                continue;
            if (n > 0 && !StreamMayWork())
            {
                stream.PoolsDirty = true;
                break;                          // (the pools' pass goes on: EvictPools is not cut short)
            }
            StreamWorked();
            stream.Private.RemoveAt(i);
            if (p.Wmo != null)
            {
                world.PrivatePrototypes.Remove(p.Wmo);
                try { worldView.DisposePrototype(p.Wmo); }
                catch (Exception ex) { Log("disposing world model {0} kept for its tile failed: {1}", p.FileDataID, ex.Message); }
                p.Wmo = null;
            }
            if (p.M2 != null)
            {
                world.PrivateDoodadPrototypes.Remove(p.M2);
                try { worldView.DisposeDoodadPrototype(world, p.M2); }   // (its texture references given back to the cache)
                catch (Exception ex) { Log("disposing doodad model {0} kept for its tile failed: {1}", p.FileDataID, ex.Message); }
                p.M2 = null;
            }
            stream.PrivateDisposed++;
            n++;
        }
        if (n > 0)
            Log("load {0}: {1} prototype(s) kept for their tile disposed (no instance, no pin left); {2} still live", stream.EntryLoad, n,
                stream.Private.Count);
    }

    /// <summary>What no tile holds stays while the pools fit their budgets: one for their GPU bytes -- world-model prototypes
    /// with their textures, M2 prototypes' geometry, the M2 texture uploads no live prototype holds, terrain textures -- and
    /// one for the world models' parsed files. Over budget, the uploads no prototype holds go first (their prototypes have
    /// gone), then the least recently used prototypes and terrain textures (an M2 prototype with the uploads only it held);
    /// over the parsed files' budget alone, only world models (nothing else holds parsed files).</summary>
    void EvictPools()
    {
        if (stream == null)
            return;
        stream.PoolsDirty = false;
        EvictImages();
        if (world == null)
            return;
        if (stream.Private.Count > 0)
            DisposeUnusedPrivate();             // (private prototypes are never pooled: they go whatever the pools hold)
        WmvDoodadTextures cache = world.DoodadTextures;
        long managed = 0;
        foreach (ProtoEntry e in stream.Wmos.Values)
            if (e.Pooled && e.Wmo != null)
                managed += e.ManagedBytes;
        long gpu = PooledGpuBytes();
        if (gpu <= StreamPoolGpuBudget && managed <= StreamPoolManagedBudget)
            return;
        int prototypes = 0, textures = 0;
        if (cache != null && gpu > StreamPoolGpuBudget)
        {
            long before = cache.PooledBytes;
            int n = cache.EvictPooled(Math.Max(0, before - (gpu - StreamPoolGpuBudget)), DestroyObject);
            gpu -= before - cache.PooledBytes;
            stream.TexturesEvicted += n;
            textures += n;
        }
        var order = new List<object>();
        foreach (ProtoEntry e in stream.Wmos.Values) if (e.Pooled && e.Wmo != null) order.Add(e);
        foreach (ProtoEntry e in stream.M2s.Values) if (e.Pooled && e.M2 != null) order.Add(e);
        foreach (TerrainTexEntry t in terrainTextures.Values) if (t.Refs <= 0) order.Add(t);
        // Protocol 19: the liquid types no tile or prototype holds (their material, and their frames when no held type lists
        // them).
        WmvLiquidRegistry liquids = StreamLiquids;
        if (liquids != null)
            foreach (WmvLiquidType t in liquids.PooledTypes()) order.Add(t);
        order.Sort((a, b) => LastUseOf(a).CompareTo(LastUseOf(b)));
        for (int oi = 0; oi < order.Count; oi++)
        {
            object o = order[oi];
            bool overGpu = gpu > StreamPoolGpuBudget;
            if (!overGpu && managed <= StreamPoolManagedBudget)
                break;
            // Under the session's frame budget like any other work (at least one eviction a frame): the rest go in the next
            // frames -- the pools' budget is soft, a few frames over it cost nothing.
            if (prototypes + textures > 0 && !StreamMayWork())
            {
                stream.PoolsDirty = true;
                break;
            }
            var e = o as ProtoEntry;
            if (!overGpu && (e == null || e.Doodad))
                continue;
            StreamWorked();
            if (e != null)
            {
                if (!e.Doodad)
                {
                    gpu -= e.GpuBytes;
                    managed -= e.ManagedBytes;
                    stream.Wmos.Remove(e.FileDataID);
                    world.Prototypes.Remove(e.FileDataID);
                    // Protocol 19: its liquid types given back as last used when it was (never as the newest use); those it
                    // alone held are pooled now and join this pass in their place, least recently used first -- their frames
                    // were counted in the pool already when only pooled prototypes held them (PooledGpuBytes).
                    if (liquids != null && e.Wmo.Liquids == liquids)
                        foreach (WmvLiquidType released in liquids.Release(e.Wmo, e.LastUse))
                        {
                            int at = oi + 1;
                            while (at < order.Count && LastUseOf(order[at]) <= released.LastUse)
                                at++;
                            order.Insert(at, released);
                        }
                    try { worldView.DisposePrototype(e.Wmo); }
                    catch (Exception ex) { Log("evicting world model {0} failed: {1}", e.FileDataID, ex.Message); }
                    e.Wmo = null;
                }
                else
                {
                    // Its geometry, and the uploads only it held (they go with it -- counted in the pool already); an upload a
                    // live prototype still holds stays, and was never counted.
                    var held = new List<UnityEngine.Texture2D>(e.M2.TextureRefs);
                    gpu -= e.M2.GpuGeometryBytes;
                    stream.M2s.Remove(e.FileDataID);
                    world.DoodadPrototypes.Remove(e.FileDataID);
                    try { worldView.DisposeDoodadPrototype(world, e.M2); }
                    catch (Exception ex) { Log("evicting doodad model {0} failed: {1}", e.FileDataID, ex.Message); }
                    e.M2 = null;
                    if (cache != null)
                    {
                        int n;
                        gpu -= cache.EvictUnreferenced(held, out n, DestroyObject);
                        stream.TexturesEvicted += n;
                        textures += n;
                    }
                }
                stream.PrototypesEvicted++;
                prototypes++;
                continue;
            }
            var lt = o as WmvLiquidType;
            if (lt != null)
            {
                gpu -= liquids.Evict(lt);
                stream.TexturesEvicted++;
                textures++;
                continue;
            }
            var t = (TerrainTexEntry)o;
            gpu -= t.GpuBytes;
            terrainTextures.Remove(t.Id);
            if (t.Texture != null)
                DestroyObject(t.Texture);
            stream.TexturesEvicted++;
            textures++;
        }
        if (prototypes + textures > 0)
            Log("load {0}: pools over budget: {1} prototype(s) and {2} texture(s) evicted, least recently used first; pooled now {3:0.0} MB GPU, " +
                "{4:0.0} MB parsed", stream.EntryLoad, prototypes, textures, PooledGpuBytes() / 1048576.0, managed / 1048576.0);
    }

    readonly Dictionary<UnityEngine.Texture2D, int> pooledRefsScratch = new Dictionary<UnityEngine.Texture2D, int>();

    static long LastUseOf(object o)
    {
        var e = o as ProtoEntry;
        if (e != null)
            return e.LastUse;
        var l = o as WmvLiquidType;
        return l != null ? l.LastUse : ((TerrainTexEntry)o).LastUse;
    }

    /// <summary>What the pools hold on the GPU: pooled world-model prototypes (with their textures), pooled M2 prototypes'
    /// geometry, the M2 texture uploads no live prototype holds (every reference on them a pooled prototype's, or none),
    /// unreferenced terrain textures, and (protocol 19) the liquid frame arrays no live holder needs (LiquidPoolBytes).</summary>
    long PooledGpuBytes()
    {
        long gpu = 0;
        if (stream == null)
            return 0;
        WmvDoodadTextures cache = world != null ? world.DoodadTextures : null;
        foreach (ProtoEntry e in stream.Wmos.Values) if (e.Pooled && e.Wmo != null) gpu += e.GpuBytes;
        Dictionary<UnityEngine.Texture2D, int> pooledRefs = pooledRefsScratch;   // (reused: no garbage a call)
        pooledRefs.Clear();
        foreach (ProtoEntry e in stream.M2s.Values)
            if (e.Pooled && e.M2 != null)
            {
                gpu += e.M2.GpuGeometryBytes;
                foreach (UnityEngine.Texture2D t in e.M2.TextureRefs)
                {
                    int n;
                    pooledRefs[t] = pooledRefs.TryGetValue(t, out n) ? n + 1 : 1;
                }
            }
        if (cache != null)
        {
            foreach (KeyValuePair<UnityEngine.Texture2D, int> kv in pooledRefs)
                if (kv.Value >= cache.RefsOf(kv.Key))
                    gpu += cache.BytesOf(kv.Key);
            gpu += cache.PooledBytes;
        }
        foreach (TerrainTexEntry t in terrainTextures.Values) if (t.Refs <= 0) gpu += t.GpuBytes;
        // Protocol 19: the liquid frame arrays no live holder needs.
        gpu += LiquidPoolBytes;
        return gpu;
    }

    /// <summary>Protocol 19: the pool's share of the session's liquid frame arrays -- an array every type listing it is pooled
    /// or held only by pooled world-model prototypes (the rule of the M2 texture uploads above: a prototype's array is the
    /// pool's while the prototype is).</summary>
    public long LiquidPoolBytes
    {
        get
        {
            WmvLiquidRegistry liquids = StreamLiquids;
            if (liquids == null || stream == null)
                return 0;
            if (pooledPrototype == null)
                pooledPrototype = IsPooledPrototype;   // (made once: no garbage a call)
            return liquids.PooledGpuBytes(pooledPrototype);
        }
    }

    Func<object, bool> pooledPrototype;

    /// <summary>Is `holder` a world-model prototype of the session's pools (built, no pin, no instance)?</summary>
    bool IsPooledPrototype(object holder)
    {
        var p = holder as WmvWorldPrototype;
        ProtoEntry e;
        return p != null && stream != null && stream.Wmos.TryGetValue(p.FileDataID, out e) && e.Wmo == p && e.Pooled;
    }

    /// <summary>The scene's GPU bytes now (streaming): terrain (tiles' own and the scene's textures, alpha arrays), world models
    /// and doodads (geometry and textures).</summary>
    void SceneGpuBytes(out long textures, out long geometry)
    {
        textures = WmvRuntimeTerrainTile.LiveTextureBytes + WmvWorldModels.LiveGpuTextureBytes + WmvWorldDoodads.LiveGpuTextureBytes;
        foreach (TerrainTexEntry t in terrainTextures.Values)
            textures += t.GpuBytes;
        geometry = WmvWorldModels.LiveGpuGeometryBytes + WmvWorldDoodads.LiveGpuGeometryBytes;
        // The resident tiles' alpha arrays (texture memory, never in LiveTextureBytes) and their meshes and material vertices --
        // and (protocol 19) their liquid geometry; the session's liquid frame arrays (a world model's liquid geometry is in
        // its prototype's, above).
        if (stream != null)
            foreach (StreamTile st in stream.Tiles.Values)
                if (st.Tile != null && !st.Tile.Disposed)
                {
                    textures += st.Tile.AlphaGpuBytes;
                    geometry += (st.Mesh != null ? st.Mesh.MeshBytes : 0) + st.Tile.MaterialVertexBytes + st.Tile.LiquidGeometryBytes;
                }
        WmvLiquidRegistry liquids = StreamLiquids;
        if (liquids != null)
            textures += liquids.ArrayBytes;
    }

    // ---------------------------------------------------------------- streamState

    void SendStreamState()
    {
        StreamWork s = stream;
        if (ReportStreamState == null || s == null)
        {
            if (s != null) s.FrameMs.Clear();
            return;
        }
        var r = new WmvIpcClient.StreamStateReport
        {
            Load = s.EntryLoad, CameraX = s.CameraX, CameraY = s.CameraY, CameraZ = s.CameraZ, CameraTileX = s.CameraTileX, CameraTileY = s.CameraTileY,
            TilesOpened = s.TilesOpened, TilesClosed = s.TilesClosed, TilesFailed = s.TilesFailed + s.TilesRefused, RequestsOut = s.RequestsOut,
            FilesAsked = s.FilesAsked, FilesAnswered = s.FilesAnswered, BytesReceived = s.BytesReceived,
            PrototypesEvicted = s.PrototypesEvicted, TexturesEvicted = s.TexturesEvicted, FramesOver33 = (int)s.FramesOver33,
            FramesOver50 = (int)s.FramesOver50, ManagedHeapBytes = GC.GetTotalMemory(false),
        };
        foreach (StreamTile st in Ordered())
        {
            if (st.Tile != null) r.TilesResident++;
            if (st.Job != null || st.State == StreamQueued || st.State == StreamOpening) r.TilesLoading++;
            if (st.Ring <= s.Offer.Radius) r.TilesWanted++;
            int wplaced = st.Placements.Count, dplaced = st.Doodads.Count;
            int wplan = st.Job != null && st.Job.World != null ? st.Job.World.Placements.Count : st.WorldPlacements;
            int dplan = st.Job != null && st.Job.World != null && st.Job.World.Doodads != null ? st.Job.World.Doodads.Instances.Count : st.DoodadInstances;
            r.Tiles.Add(new WmvIpcClient.StreamStateTile
            {
                X = st.X, Y = st.Y, Load = st.TileLoad, State = st.State, FilesAsked = st.FilesAsked, FilesAnswered = st.FilesAnswered, Bytes = st.Bytes,
                WorldPlaced = wplaced, WorldPlacements = wplan, DoodadsPlaced = dplaced, DoodadInstances = dplan,
            });
            if (st.State == StreamQueued) r.RequestsWaiting++;
        }
        foreach (ProtoEntry e in s.Wmos.Values)
            if (e.State == "built" && e.Wmo != null) { r.WorldPrototypes++; if (e.Pooled) r.WorldPrototypesPooled++; }
        foreach (ProtoEntry e in s.M2s.Values)
            if (e.State == "built" && e.M2 != null) { r.DoodadPrototypes++; if (e.Pooled) r.DoodadPrototypesPooled++; }
        foreach (ProtoEntry e in s.Private)     // (resident, never pooled)
        {
            if (e.Wmo != null) r.WorldPrototypes++;
            else if (e.M2 != null) r.DoodadPrototypes++;
        }
        r.Textures = terrainTextures.Count + (world != null && world.DoodadTextures != null ? world.DoodadTextures.Uploads : 0);
        foreach (TerrainTexEntry t in terrainTextures.Values)
            if (t.Refs <= 0) r.TexturesPooled++;
        if (world != null && world.DoodadTextures != null)
            r.TexturesPooled += world.DoodadTextures.PooledCount;
        r.WorldInstances = world != null ? world.Instances.Count : 0;
        r.DoodadInstances = world != null ? world.Doodads.Count : 0;
        long tex, geo;
        SceneGpuBytes(out tex, out geo);
        r.GpuTextureBytes = tex;
        r.GpuGeometryBytes = geo;
        r.PoolBytes = PooledGpuBytes();
        if (s.FrameMs.Count > 0)
        {
            float max = 0;
            foreach (float f in s.FrameMs) max = Math.Max(max, f);
            r.FrameMsMax = max;
        }
        // The 95th percentile of the last frames (about 2 s at 60 a second), not of the ~15 since the last streamState (of
        // which it would be the largest); -1 until 20 frames have been seen.
        r.FrameMsP95 = -1;
        if (s.FrameRingCount >= 20)
        {
            var a = new float[s.FrameRingCount];
            Array.Copy(s.FrameRing, a, s.FrameRingCount);
            Array.Sort(a);
            r.FrameMsP95 = a[(int)Math.Ceiling(0.95 * a.Length) - 1];
        }
        r.UploadMsMax = s.UploadMsMax;
        r.WorkMsMax = s.WorkMsMax;
        FillLiquidStreamState(r);
        s.FrameMs.Clear();
        s.UploadMsMax = 0;
        s.WorkMsMax = 0;
        try { ReportStreamState(r); }
        catch (Exception e) { Log("sending streamState failed: {0}", e.Message); }
    }

    /// <summary>The session in a line (the self-test's and runtimeState's): tiles, jobs, requests, what is shared.</summary>
    public string StreamSummary()
    {
        if (stream == null)
            return "no streaming session";
        return string.Format("entry load {0}, camera tile {1}_{2}, {3} tile(s) [{4}], {5} job(s), {6} request(s) out (most {7}), {8} placement(s), " +
                             "{9} doodad(s), {10} world prototype(s), {11} M2 prototype(s), {12} scene terrain texture(s); {13} placements and {14} " +
                             "doodads shared, {15} prototypes shared, {16} fetches taken over, {17} prototypes and {18} textures evicted; " +
                             "{19} prototype(s) kept for their tile ({20} live), {21} placement(s) and {22} doodad(s) replaced",
                             stream.EntryLoad, stream.CameraTileX, stream.CameraTileY, stream.Tiles.Count, string.Join(" ", StreamTileStates().ToArray()),
                             stream.Jobs.Count, stream.RequestsOut, stream.RequestsOutMax, stream.Placements.Count, stream.Doodads.Count,
                             stream.Wmos.Count, stream.M2s.Count, terrainTextures.Count, stream.PlacementsShared, stream.DoodadsShared,
                             stream.PrototypesShared, stream.PrototypeTakeovers, stream.PrototypesEvicted, stream.TexturesEvicted,
                             stream.PrototypesKept, stream.Private.Count, stream.PlacementsReplaced, stream.DoodadsReplaced);
    }
}
