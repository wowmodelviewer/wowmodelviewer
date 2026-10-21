// WmvTerrainSessionLiquids.cs
//
// LIQUIDS (protocol 19, docs/world-viewer/09-liquids.md 1.6-1.8): the part of WmvTerrainSession that draws a load's liquids
// -- its tile's MH2O and its world models' MLIQ -- and fetches their animation frames. A load that carries no "liquids" (an
// older host) never meets any of this: nothing is drawn and its report says "offered": false, "catalog": "absent".
//
//   the registry  one per scene (WmvLiquidRegistry, WmvLiquids.cs): a single load's own -- made with the load, the scene's
//                 from its commit, disposed with the scene (DisposeTiles), or with the load when it never commits; a
//                 streaming session's -- its entry's, shared by every tile and world model of the session, a type nothing
//                 holds kept POOLED and evicted with the session's pools (EvictPools), least recently used first.
//   the tile      on the worker, beside the terrain mesh, from the same parsed root and the same origin (a single load's
//                 tile, a session's stream.Origin): TerrainLiquidMesh.Build with the offer's catalogue. When it lands the
//                 job holds the types its surfaces name and asks for their frames at once; the tile is built as before
//                 (WmvTerrainBuilder.Build draws one object per type from the content's LiquidTile and registry) and holds
//                 its types until it is released -- every view rebuild draws them again from the kept content.
//   world models  after a prototype is built from its files (BuildModel; never in the Buildings viewer's path), each LOD0
//                 group whose MOGP flag 0x1000 is set: its MLIQ parsed from the group's bytes (WmoLiquid.FromGroup), its
//                 type by the documented rule (WmoLiquid.GroupLiquidType with the root's MOHD flags, INFERRED), its mesh in
//                 the group's frame (WmoLiquidMesh.Build) as a child of the group's object, drawn with the type's shared
//                 material; the prototype holds its types until it is disposed (pooled and private prototypes alike).
//   frames        role "liquidTexture", by FileDataID, under the load whose tile or world model needs them (a session's
//                 tile load) -- a type's ripple map ("bump", the High look) the same way, one more file of the type: at
//                 most MaxLiquidOutstanding out at once per load (apart from the tile's K, the world
//                 models' and the doodads' own), each id at most once per load, within the offer's maxTexturesPerLoad ids
//                 and maxTextureBytesPerLoad bytes, a frame the scene holds or another load fetches never asked. Answers
//                 are routed and checked as every map answer (request id and load); a late one is dropped. Prepared on the
//                 worker (WmvTerrainTexture.Prepare), uploaded by the registry once the type's frame list has settled; a
//                 frame that arrives for a type no longer held is dropped. A TILE NEVER WAITS FOR ITS FRAMES: it is
//                 adopted when its terrain is ready and its liquid drawn in the base colour until they come; the load's
//                 REPORT (a session's tile: its settling) waits for them, within the load timeout from the last progress.
//                 A frame refused or stale for a session's tile load is wanted again (another tile may get it); any other
//                 failure, and in a single load every one, settles the type in its base colour.
//   the report    terrainTileLoaded's "liquids" (WmvIpcClient.TerrainLiquidReport) and one "[liquids]" log line per tile
//                 and per world model with liquid; streamState and runtimeState count what is resident.
//   zone lights   the offer's "lights" (Wow/World/LiquidLights.cs), the scene's beside its liquids: set at the commit (a
//                 lights object of another map than the load's is ignored, and said), dropped with the scene
//                 (DisposeTiles), which also turns the shader's zone light off at once (WmvLiquidLight.Off). A streaming
//                 session's tile loads change nothing: the scene keeps its entry's. The player reads them at the camera
//                 once a rendered frame (WmvMain.LiquidLightFrame).

using System;
using System.Collections.Generic;
using Wmv.Wow;
using Wmv.Wow.World;

public sealed partial class WmvTerrainSession
{
    public const string RoleLiquidTexture = "liquidTexture";
    /// <summary>A load's liquid frame requests out at once (a frame being prepared on the worker counts until it is done),
    /// beside the tile's K and the world models' and doodads' own.</summary>
    public const int MaxLiquidOutstanding = 4;

    /// <summary>What a load does about liquids (null: its command carried no "liquids").</summary>
    sealed class LiquidWork
    {
        public WmvLiquidRegistry Registry;
        public TerrainLiquidTile Tile;          // the tile's MH2O as the worker built it (null: none yet, no tile, or it threw)
        /// <summary>Every liquid type id the load met (its tile's, its world models'), in first-met order, and those it
        /// fetches frames for (the types its tile and world models draw).</summary>
        public readonly List<int> Met = new List<int>();
        public readonly HashSet<int> Types = new HashSet<int>();
        public readonly HashSet<uint> Asked = new HashSet<uint>();          // frame ids asked under this load
        public readonly HashSet<uint> NotRequested = new HashSet<uint>();   // frame ids it could not ask (budgets)
        public int Pending, Preparing;
        public int Requested, Loaded, Failed;
        public long Bytes;
        public double ProgressAtMs = double.NaN;  // the Pump clock at the last ask or answer (its timeout)
        public int WmoGroups, WmoQuads, WmoMeshes, WmoMissing, WmoMalformed;
        public long WmoBytes;
        public readonly HashSet<int> WmoMissingTypes = new HashSet<int>();
        public bool Ended;
        public int Out { get { return Pending + Preparing; } }
    }

    WmvLiquidRegistry sceneLiquids;
    LiquidLights sceneLights;

    /// <summary>The scene's liquid registry (the shown tile's, a session's), null when it has none.</summary>
    public WmvLiquidRegistry SceneLiquids { get { return sceneLiquids != null && !sceneLiquids.Disposed ? sceneLiquids : null; } }
    /// <summary>The scene's zone lights (its load's offer's, when "ok" and of the load's map), null when it has none.</summary>
    public LiquidLights SceneLights { get { return sceneLights; } }

    /// <summary>The commit: the load's zone lights become the scene's -- none when its offer carried none (or not "ok"), or
    /// when they are another map's (ignored, and said).</summary>
    void CommitSceneLights(Job j)
    {
        WmvIpcClient.LiquidOffer offer = j.Cmd.Liquids;
        LiquidLights l = offer != null ? offer.Lights : null;
        sceneLights = null;
        if (offer == null)
            return;
        if (l == null)
        {
            if (!string.IsNullOrEmpty(offer.LightsState))
                LiquidLog("load {0}: zone lights {1}{2}: the liquids keep their type colours", j.Cmd.Load, offer.LightsState,
                          !string.IsNullOrEmpty(offer.LightsDetail) ? " (" + offer.LightsDetail + ")" : "");
            return;
        }
        if (l.Map != j.Cmd.MapId)
        {
            LiquidLog("load {0}: the zone lights are map {1}'s, not the load's map {2}: ignored, the liquids keep their type colours",
                      j.Cmd.Load, l.Map, j.Cmd.MapId);
            return;
        }
        sceneLights = l;
        LiquidLog("load {0}: zone lights {1}", j.Cmd.Load, l.Summary());
    }

    /// <summary>DisposeTiles: the scene's zone lights go with it, and the shader's zone light is off at once.</summary>
    void DropSceneLights()
    {
        sceneLights = null;
        WmvLiquidLight.Off();
    }

    /// <summary>The liquid frame requests the load in flight has out (the self-test's).</summary>
    public int LiquidRequestsOut { get { return current != null && current.Liquid != null ? current.Liquid.Pending : 0; } }

    /// <summary>A load begins: its liquid work when the command offers liquids -- a new registry (a single load's; a
    /// streaming session's entry makes the session's), or a session's tile the session's.</summary>
    void BeginLiquids(Job j)
    {
        WmvIpcClient.LiquidOffer offer = j.Cmd.Liquids;
        if (offer == null)
            return;
        var w = new LiquidWork();
        if (j.Stream != null && stream != null && !j.Stream.Entry && stream.Liquids != null)
            w.Registry = stream.Liquids;
        else
        {
            bool pooled = j.Stream != null && stream != null;
            w.Registry = new WmvLiquidRegistry(offer, pooled, () => DestroyObject, s => LiquidLog("{0}", s));
            if (pooled)
            {
                stream.Liquids = w.Registry;
                w.Registry.NextUse = () => stream != null ? ++stream.Use : 0;
            }
        }
        j.Liquid = w;
    }

    void LiquidLog(string format, params object[] args)
    {
        log("[liquids] " + string.Format(System.Globalization.CultureInfo.InvariantCulture, format, args));
    }

    /// <summary>The root's worker result, just before OnBuilt (whose Advance may adopt the tile at once): the tile's liquid as
    /// the worker built it -- the types its surfaces draw held by the load, their frames asked for (the tile does not wait for
    /// them).</summary>
    void LiquidsParsed(Job j, TerrainLiquidTile tile, double ms, string error)
    {
        LiquidWork w = j.Liquid;
        if (w == null)
            return;
        if (error != null)
        {
            LiquidLog("tile {0} (load {1}): the liquid could not be built ({2}); the terrain is drawn without it", j.Tile2, j.Cmd.Load, error);
            return;
        }
        w.Tile = tile;
        if (tile == null)
            return;
        foreach (int id in tile.Types)
            Meet(w, id);
        foreach (LiquidSurface s in tile.Surfaces)
            if (s.Type != null && w.Registry.Hold(j, s.LiquidType) != null)
                w.Types.Add(s.LiquidType);
        if (tile.Instances > 0)
            LiquidLog("tile {0} (load {1}): MH2O built on the worker in {2:0.###} ms: {3} instance(s) in {4} chunk(s) (at most {5} layer(s)), " +
                      "{6} surface(s), {7} quads, {8} vertices, {9} mesh byte(s); types [{10}] ({11} the catalogue lacks); {12} malformed, {13} " +
                      "format(s) from the block size, {14} disagreement(s)", j.Tile2, j.Cmd.Load, ms, tile.Instances, tile.ChunksWithLiquid,
                      tile.MaxLayers, tile.Surfaces.Count, tile.Quads, tile.Vertices, tile.GeometryBytes, Ids(tile.Types), tile.MissingTypes,
                      tile.MalformedInstances, tile.FormatFromSize, tile.FormatDisagreements);
        AskLiquidFrames(j);
    }

    static void Meet(LiquidWork w, int id)
    {
        if (!w.Met.Contains(id))
            w.Met.Add(id);
    }

    static string Ids(IEnumerable<int> ids)
    {
        var parts = new List<string>();
        foreach (int id in ids)
            parts.Add(id.ToString(System.Globalization.CultureInfo.InvariantCulture));
        return string.Join(",", parts.ToArray());
    }

    /// <summary>
    /// BuildModel: a prototype built from its files gets its groups' liquid -- each LOD0 group with MOGP flag 0x1000, its
    /// MLIQ (WmoLiquid.FromGroup), its type (WmoLiquid.GroupLiquidType, the root's MOHD flags), its mesh in the group's frame
    /// (WmoLiquidMesh.Build) as a child of the group's object on layer 30, drawn with the type's shared material. The
    /// prototype holds the types (WmvWorldModels.AddLiquids; given back when it is disposed), the load fetches their frames.
    /// A group whose MLIQ is malformed, or names no type, or a type the catalogue lacks, is not drawn and is counted; a
    /// failure here never fails the world model.
    /// </summary>
    void AddWorldModelLiquids(Job j, Model m, WmvWorldPrototype proto)
    {
        LiquidWork w = j.Liquid;
        if (w == null || proto == null || proto.Runtime == null || proto.Root == null || w.Registry.Disposed)
            return;
        WmoGroup[] groups = proto.Groups;
        UnityEngine.GameObject[] objects = proto.Runtime.GroupObjects ?? new UnityEngine.GameObject[0];
        var meshes = new List<UnityEngine.Mesh>();
        var renderers = new List<UnityEngine.Renderer>();
        int made = 0, groupsWith = 0, quads = 0, malformed = 0, missing = 0, unnamed = 0;
        long bytes = 0;
        var types = new List<int>();
        var clock = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            for (int gi = 0; gi < groups.Length; gi++)
            {
                WmoGroup g = groups[gi];
                if (g == null)
                    continue;
                string problem;
                WmoLiquid liq = WmoLiquid.FromGroup(g, out problem);
                if (liq == null)
                {
                    if (problem != null)
                    {
                        malformed++;
                        LiquidLog("world model {0} group {1} (load {2}): MLIQ not drawn: {3}", m.FileDataID, gi, j.Cmd.Load, problem);
                    }
                    continue;
                }
                int type = WmoLiquid.GroupLiquidType(proto.Root.Header.Flags, g.Header.GroupLiquid, g.Header.Flags, liq.Nibble);
                if (type == 0)
                {
                    unnamed++;
                    LiquidLog("world model {0} group {1} (load {2}): MLIQ of {3} present tile(s) names no liquid type (MOHD 0x{4:X}, MOGP +0x34 {5}, " +
                              "nibble {6}) -- not drawn", m.FileDataID, gi, j.Cmd.Load, liq.PresentTiles, proto.Root.Header.Flags,
                              g.Header.GroupLiquid, liq.Nibble);
                    continue;
                }
                Meet(w, type);
                LiquidSurface s = WmoLiquidMesh.Build(liq, type, w.Registry.Catalog);
                if (s.QuadCount == 0)
                    continue;
                groupsWith++;
                quads += s.QuadCount;
                if (!types.Contains(type))
                    types.Add(type);
                UnityEngine.GameObject parent = gi < objects.Length ? objects[gi] : null;   // (null: its file could not be read)
                WmvLiquidType t = null;
                if (s.Type != null && parent != null)
                {
                    proto.Liquids = w.Registry;            // (what it holds is given back when it is disposed)
                    t = w.Registry.Hold(proto, type);
                }
                if (t == null || t.Material == null)
                {
                    if (s.Type == null)
                    {
                        missing++;
                        w.WmoMissingTypes.Add(type);
                    }
                    continue;
                }
                if (!proto.LiquidTypes.Contains(type))
                    proto.LiquidTypes.Add(type);
                var go = new UnityEngine.GameObject("MLIQ liquid type " + type);
                made++;                         // (counted before anything can throw: it is under the group, destroyed with it)
                go.layer = WmvTerrainBuilder.Layer;
                go.transform.SetParent(parent.transform, false);
                UnityEngine.Mesh mesh = WmvLiquidMeshes.Create(s, "WmvLiquid MLIQ " + m.FileDataID + " group " + gi + " type " + type, false);
                meshes.Add(mesh);
                bytes += s.MeshBytes;
                go.AddComponent<UnityEngine.MeshFilter>().sharedMesh = mesh;
                UnityEngine.MeshRenderer r = go.AddComponent<UnityEngine.MeshRenderer>();
                renderers.Add(r);
                WmvLiquidMeshes.Quiet(r);
                r.sharedMaterial = t.Material;
                w.Types.Add(type);
            }
        }
        catch (Exception e)
        {
            LiquidLog("world model {0} (load {1}): building its liquid failed: {2}: {3} -- the world model is drawn without the rest", m.FileDataID,
                      j.Cmd.Load, e.GetType().Name, e.Message);
        }
        finally
        {
            // What was made is the prototype's whatever happened (its objects are under the groups' objects).
            WmvWorldModels.AddLiquids(proto, meshes, made, bytes, renderers);
        }
        proto.LiquidQuads += quads;
        w.WmoGroups += groupsWith;
        w.WmoQuads += quads;
        w.WmoMeshes += meshes.Count;
        w.WmoBytes += bytes;
        w.WmoMissing += missing;
        w.WmoMalformed += malformed;
        if (groupsWith > 0 || malformed > 0 || unnamed > 0)
        {
            LiquidLog("world model {0} (load {1}): {2} group(s) with liquid, {3} quads, {4} mesh(es), {5} mesh byte(s) in {6:0.###} ms; types [{7}] " +
                      "({8} group(s) of a type the catalogue lacks); {9} malformed, {10} naming no type", m.FileDataID, j.Cmd.Load, groupsWith, quads,
                      meshes.Count, bytes, clock.Elapsed.TotalMilliseconds, Ids(types), missing, malformed, unnamed);
        }
        AskLiquidFrames(j);
    }

    /// <summary>Streaming: a tile placing from the session's prototype (built for another tile, or pooled) fetches the frames
    /// of its liquid types that the scene does not hold yet, as the tile that built it would have.</summary>
    void SharedWorldModelLiquids(Job j, WmvWorldPrototype proto)
    {
        LiquidWork w = j.Liquid;
        if (w == null || proto == null || proto.LiquidTypes.Count == 0)
            return;
        foreach (int id in proto.LiquidTypes)
        {
            Meet(w, id);
            w.Types.Add(id);
        }
        AskLiquidFrames(j);
    }

    /// <summary>Why `j` cannot ask for frame `fid`, or null: asked under this load already (the host serves an id once per
    /// load), or past the offer's budgets.</summary>
    static string CannotAskLiquid(Job j, uint fid)
    {
        LiquidWork w = j.Liquid;
        WmvIpcClient.LiquidOffer o = j.Cmd.Liquids;
        if (w.Asked.Contains(fid))
            return "asked for under this load already (the host serves an id once per load)";
        if (fid > int.MaxValue)
            return "FileDataID " + fid + " is beyond what a request can carry";
        if (o.MaxTexturesPerLoad <= 0 || w.Requested >= o.MaxTexturesPerLoad)
            return "this load may ask for at most " + o.MaxTexturesPerLoad + " liquid textures (maxTexturesPerLoad)";
        if (o.MaxTextureBytesPerLoad > 0 && w.Bytes >= o.MaxTextureBytesPerLoad)
            return "this load has read its " + o.MaxTextureBytesPerLoad + " liquid texture bytes (maxTextureBytesPerLoad)";
        return null;
    }

    /// <summary>A type's lists the loads fetch for: 0 its frames, 1 its ripple map (null when it names none).</summary>
    static WmvLiquidFrames ListOfType(WmvLiquidType t, int which)
    {
        return which == 0 ? t.Frames : t.Bump;
    }

    /// <summary>The frames (and ripple maps) of the load's liquid types the scene neither holds nor fetches, asked for while
    /// fewer than MaxLiquidOutstanding are out (and, in a session, the credit allows). A frame this load cannot ask for
    /// (asked already, past a budget): in a single load it is settled as failed (no other load of the scene can get it), in
    /// a session it stays wanted for another tile.</summary>
    void AskLiquidFrames(Job j)
    {
        LiquidWork w = j.Liquid;
        if (w == null || w.Ended || j.Dead || !Live(j) || w.Registry.Disposed || w.Types.Count == 0)
            return;
        bool session = j.Stream != null && stream != null;
        foreach (int id in new List<int>(w.Types))
        {
            WmvLiquidType t = w.Registry.Get(id);
            for (int which = 0; t != null && which < 2; which++)
            {
                WmvLiquidFrames list = ListOfType(t, which);
                if (list == null || list.State != WmvLiquidFrames.StateFetching)
                    continue;
                foreach (uint fid in list.Fids)
                {
                    WmvLiquidFrame f = w.Registry.Frame(fid);
                    if (f == null || f.State != WmvLiquidFrame.StateWanted || w.Asked.Contains(fid))
                        continue;                   // (asked under this load already: its answer was counted)
                    string why = CannotAskLiquid(j, fid);
                    if (why != null)
                    {
                        if (w.NotRequested.Add(fid))
                        {
                            w.Failed++;
                            LiquidLog("load {0}: liquid texture {1} (type {2}) not asked for: {3}", j.Cmd.Load, fid, id, why);
                        }
                        if (!session)
                            w.Registry.FrameFailed(fid, WmvTerrainTexture.StateNotRequested, why, false);
                        continue;
                    }
                    if (w.Out >= MaxLiquidOutstanding || !StreamCredit(j))
                        return;
                    double at = j.Clock.Elapsed.TotalMilliseconds;
                    string rid = SendMapRequest(j, RoleLiquidTexture, (int)fid);
                    j.Pending.Add(new Request { Id = rid, Role = RoleLiquidTexture, FileDataID = (int)fid, AskedMs = at, Liquid = true });
                    w.Registry.Asking(fid, j);
                    w.Asked.Add(fid);
                    w.Pending++;
                    w.Requested++;
                    w.ProgressAtMs = double.NaN;
                    OutstandingPeak = Math.Max(Outstanding, OutstandingPeak);
                    if (w.Registry.Disposed || j.Dead)
                        return;
                }
            }
        }
    }

    void OnLiquidTexture(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        LiquidWork w = j.Liquid;
        uint fid = (uint)q.FileDataID;
        w.Pending--;
        w.ProgressAtMs = double.NaN;
        double roundTrip = j.Clock.Elapsed.TotalMilliseconds - q.AskedMs;
        LogAnswer(j, "liquid texture " + fid, r, roundTrip);
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
        else if (j.Cmd.Liquids.MaxTextureBytes > 0 && r.data.Length > j.Cmd.Liquids.MaxTextureBytes)
            problem = "answer of " + r.data.Length + " bytes exceeds maxTextureBytes " + j.Cmd.Liquids.MaxTextureBytes;
        if (r.data != null)
            w.Bytes += r.data.Length;
        if (problem != null)
        {
            w.Failed++;
            // A session's tile load refused or went stale: not the file's fault -- another tile may get it.
            bool retry = j.Stream != null && stream != null && RefusedForTheLoad(r);
            w.Registry.FrameFailed(fid, state, problem, retry);
            LiquidLog("load {0}: liquid texture {1}: {2}: {3}{4}", j.Cmd.Load, fid, state, problem, retry ? " (wanted again: another tile may get it)" : "");
            AskLiquidFrames(j);
            return;
        }
        // Prepared into a new entry on the worker (verified against the host's md5, then DXT blocks or RGBA32), handed to the
        // registry on the main thread; it counts against MaxLiquidOutstanding until then.
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        WmvTerrainFormatSupport support = j.Support;
        w.Preparing++;
        RunWorker(j, () =>
        {
            var p = new WmvTerrainTexture { FileDataID = fid, RoundTripMs = roundTrip };
            p.Prepare(bytes, hostMd5, support);
            return () => OnLiquidFramePrepared(j, p);
        });
    }

    void OnLiquidFramePrepared(Job j, WmvTerrainTexture p)
    {
        LiquidWork w = j.Liquid;
        w.Preparing--;
        w.ProgressAtMs = double.NaN;
        if (p.Available)
        {
            w.Loaded++;
            if (!w.Registry.FramePrepared(p.FileDataID, p))
                LiquidLog("load {0}: liquid texture {1} arrived for a type no longer held -- dropped, not uploaded", j.Cmd.Load, p.FileDataID);
        }
        else
        {
            w.Failed++;
            w.Registry.FrameFailed(p.FileDataID, p.State, p.Detail, false);
            LiquidLog("load {0}: liquid texture {1}: {2}: {3}", j.Cmd.Load, p.FileDataID, p.State, p.Detail);
        }
        AskLiquidFrames(j);
    }

    /// <summary>The load's liquid frames are settled: none of its own out, and every frame (and ripple map) of its types
    /// ready, failed, or one it cannot ask for (asked already, past a budget) -- a frame another load of the session fetches
    /// is waited for.</summary>
    bool LiquidsSettled(Job j)
    {
        LiquidWork w = j.Liquid;
        if (w == null || w.Ended || w.Registry.Disposed)
            return true;
        if (w.Out > 0)
            return false;
        foreach (int id in w.Types)
        {
            WmvLiquidType t = w.Registry.Get(id);
            for (int which = 0; t != null && which < 2; which++)
            {
                WmvLiquidFrames list = ListOfType(t, which);
                if (list == null || list.State != WmvLiquidFrames.StateFetching)
                    continue;
                foreach (uint fid in list.Fids)
                {
                    WmvLiquidFrame f = w.Registry.Frame(fid);
                    if (f == null || f.State == WmvLiquidFrame.StateReady || f.State == WmvLiquidFrame.StateFailed)
                        continue;
                    if (f.State == WmvLiquidFrame.StateAsking || CannotAskLiquid(j, fid) == null)
                        return false;
                }
            }
        }
        return true;
    }

    /// <summary>One frame of a load's liquids: its frames asked for, and its timeout -- the load timeout after the last ask or
    /// answer while frames of its own are out. True when its liquids are settled (the report, the settle may follow).</summary>
    bool PumpLiquids(Job j, double nowMs)
    {
        LiquidWork w = j.Liquid;
        if (w == null || w.Ended)
            return true;
        AskLiquidFrames(j);
        if (!Live(j) || w.Ended)
            return true;
        if (LiquidsSettled(j))
            return true;
        if (w.Out == 0)
        {
            w.ProgressAtMs = double.NaN;      // waiting for another load's fetch: not late
            return false;
        }
        if (double.IsNaN(w.ProgressAtMs))
            w.ProgressAtMs = nowMs;
        if (nowMs - w.ProgressAtMs > timeoutMs)
            TimeOutLiquids(j);
        return LiquidsSettled(j);
    }

    /// <summary>The load's frame requests out are retired (their answers late from now on): in a single load their frames
    /// fail ("timed out"), in a session they are wanted again (another tile may get them).</summary>
    void TimeOutLiquids(Job j)
    {
        LiquidWork w = j.Liquid;
        string why = "timed out after " + (timeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                     " s without an answer";
        bool session = j.Stream != null && stream != null;
        var keep = new List<Request>();
        int retired = 0;
        foreach (Request q in j.Pending)
        {
            if (!q.Liquid)
            {
                keep.Add(q);
                continue;
            }
            RetireRequest(j, q, "its liquid textures timed out");
            w.Registry.FrameFailed((uint)q.FileDataID, "timed out", why, session);
            retired++;
        }
        j.Pending.Clear();
        j.Pending.AddRange(keep);
        w.Pending = 0;
        w.Failed += retired;
        w.ProgressAtMs = double.NaN;
        LiquidLog("load {0}: {1} liquid texture request(s) {2}{3}", j.Cmd.Load, retired, why,
                  session ? " -- wanted again: another tile may get them" : " -- their types are drawn in their base colour");
    }

    /// <summary>The load ended (Retire, by any path): what it held is given back, a fetch it had out is wanted again (a
    /// session's other tiles may get it), and a registry that never became the scene's goes. Its "[liquids]" line.</summary>
    void EndLiquids(Job j, string ended)
    {
        LiquidWork w = j.Liquid;
        if (w == null || w.Ended)
            return;
        w.Ended = true;
        w.Pending = w.Preparing = 0;
        WmvLiquidRegistry reg = w.Registry;
        if (!reg.Disposed)
        {
            reg.Release(j);
            reg.OwnerEnded(j);
        }
        if ((w.Tile != null && w.Tile.Instances > 0) || w.WmoGroups > 0)
            LiquidLog("tile {0} (load {1}, ended \"{2}\"): {3} instance(s), {4} type(s) [{5}], {6} quads, {7} vertices; world models: {8} group(s) " +
                      "with liquid, {9} quads; textures {10} requested, {11} loaded, {12} failed; {13} mesh byte(s); registry: {14}", j.Tile2,
                      j.Cmd.Load, ended, w.Tile != null ? w.Tile.Instances : 0, w.Met.Count, Ids(w.Met), w.Tile != null ? w.Tile.Quads : 0,
                      w.Tile != null ? w.Tile.Vertices : 0, w.WmoGroups, w.WmoQuads, w.Requested, w.Loaded, w.Failed,
                      (w.Tile != null ? w.Tile.GeometryBytes : 0) + w.WmoBytes, reg.Disposed ? "disposed" : reg.Summary());
        if (!reg.Disposed && reg != sceneLiquids && (stream == null || reg != stream.Liquids))
            reg.Dispose();
    }

    /// <summary>terrainTileLoaded's "liquids": what the load drew and fetched (offered false, catalogue "absent", when it
    /// carried no "liquids").</summary>
    WmvIpcClient.TerrainLiquidReport LiquidReport(Job j)
    {
        LiquidWork w = j.Liquid;
        var r = new WmvIpcClient.TerrainLiquidReport();
        WmvIpcClient.LiquidOffer offer = j.Cmd.Liquids;
        if (offer != null)
        {
            r.Offered = true;
            r.Catalog = string.IsNullOrEmpty(offer.CatalogState) ? "absent" : offer.CatalogState;
        }
        if (w == null)
            return r;                       // (not offered, or the load failed before it began)
        TerrainLiquidTile t = w.Tile;
        if (t != null)
        {
            r.Instances = t.Instances;
            r.Layers = t.MaxLayers;
            r.Chunks = t.ChunksWithLiquid;
            r.Quads = t.Quads;
            r.Vertices = t.Vertices;
            r.MissingTypes = t.MissingTypes;
            r.FormatBySize = t.FormatFromSize;
            r.FormatDisagreements = t.FormatDisagreements;
            r.Malformed = t.MalformedInstances;
        }
        r.Types.AddRange(w.Met);
        // The types the world models' groups name that the catalogue lacks and the tile did not already count.
        foreach (int id in w.WmoMissingTypes)
            if (t == null || !t.Types.Contains(id))
                r.MissingTypes++;
        r.Malformed += w.WmoMalformed;
        r.WmoGroups = w.WmoGroups;
        r.WmoQuads = w.WmoQuads;
        WmvRuntimeTerrainTile tile = j.Tile;
        int tileMeshes = tile != null && !tile.Disposed ? tile.LiquidMeshCount : 0;
        long tileBytes = tile != null && !tile.Disposed ? tile.LiquidGeometryBytes : 0;
        r.Meshes = tileMeshes + w.WmoMeshes;
        r.GeometryBytes = tileBytes + w.WmoBytes;
        r.TexturesRequested = w.Requested;
        r.TexturesLoaded = w.Loaded;
        r.TexturesFailed = w.Failed;
        // The frames and ripple maps of its types resident now (each array once).
        if (!w.Registry.Disposed)
        {
            var seen = new HashSet<WmvLiquidFrames>();
            foreach (int id in w.Met)
            {
                WmvLiquidType lt = w.Registry.Get(id);
                for (int which = 0; lt != null && which < 2; which++)
                {
                    WmvLiquidFrames list = ListOfType(lt, which);
                    if (list != null && list.Resident && seen.Add(list))
                        r.TextureBytes += list.GpuBytes;
                }
            }
        }
        return r;
    }

    /// <summary>DisposeTiles: the scene's liquid registry goes with the scene (not when `keep` draws from it).</summary>
    void DisposeSceneLiquids(WmvRuntimeTerrainTile keep)
    {
        WmvLiquidRegistry reg = sceneLiquids;
        if (reg == null || (keep != null && keep.Liquids == reg))
            return;
        sceneLiquids = null;
        if (!reg.Disposed && (stream == null || reg != stream.Liquids || stream.Ended))
        {
            LiquidLog("the scene's liquids go: {0}", reg.Summary());
            reg.Dispose();
        }
    }

    /// <summary>The scene's liquid clock (seconds): each animated type's frame (WmvLiquidRegistry.Tick).</summary>
    public void TickLiquids(double seconds)
    {
        if (sceneLiquids != null && !sceneLiquids.Disposed)
            sceneLiquids.Tick(seconds);
    }

    // ---------------------------------------------------------------- streaming: the pools, the scene's bytes, streamState

    /// <summary>The session's liquid registry, live (null outside a session).</summary>
    WmvLiquidRegistry StreamLiquids { get { return stream != null && stream.Liquids != null && !stream.Liquids.Disposed ? stream.Liquids : null; } }

    /// <summary>The GPU bytes of the resident tiles' liquid geometry.</summary>
    long ResidentLiquidGeometryBytes()
    {
        long n = 0;
        if (stream != null)
            foreach (StreamTile st in stream.Tiles.Values)
                if (st.Tile != null && !st.Tile.Disposed)
                    n += st.Tile.LiquidGeometryBytes;
        return n;
    }

    void FillLiquidStreamState(WmvIpcClient.StreamStateReport r)
    {
        WmvLiquidRegistry reg = StreamLiquids;
        if (stream != null)
            foreach (StreamTile st in stream.Tiles.Values)
                if (st.Tile != null && !st.Tile.Disposed && st.Tile.LiquidObjectCount > 0)
                    r.LiquidTiles++;
        r.LiquidTypes = reg != null ? reg.TypeCount : 0;
        r.LiquidTextures = reg != null ? reg.ArrayCount : 0;
        r.LiquidBytes = (reg != null ? reg.ArrayBytes : 0) + ResidentLiquidGeometryBytes();
    }
}
