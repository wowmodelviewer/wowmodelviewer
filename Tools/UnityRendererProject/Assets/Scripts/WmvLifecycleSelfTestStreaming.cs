// WmvLifecycleSelfTestStreaming.cs
//
// The lifecycle self-test's streaming session (protocol 18, docs/world-viewer/08-streaming.md): the terrain session against
// a fake host that opens tile loads (streamTile -> streamTileAnswer) and serves synthetic files only -- a 5 x 5 map (tiles
// 30..34 x 48..52) whose tiles share a terrain texture, a world model (two placements, one listed by two tiles), doodads (one
// listed by two tiles, one per tile) and a world model's doodad set; a corner tile with a model, a world model and a terrain
// texture of its own (the pools). Built by the real builders under the test views' inactive roots, disposed at once.
// Checked: the region of nine around the entry, each shared file asked for once, each shared placement placed once, every
// tile against the entry's origin with its edges bit-equal to its neighbours'; the credit and the priorities; the camera
// moving east and back (tiles opening and closing, what only closed tiles held destroyed, what is resident never asked
// again), and the region staying put when the camera stops flying; the pools (a closed tile's model used again without
// asking; with no pool budget evicted and asked again); a tile closed with its requests out (retired, their answers
// dropped as late, nothing built); a prototype fetch taken over when the tile fetching it closes (the entry reported
// "built" once: the session goes on); one streamTile out at a time, closes only too; a refusal for one tile's load handed
// on to another tile; a texture missing from storage asked for once; a texture or group refused for one tile's load kept
// for that tile and handed on (complete placements replacing private ones, every tile refusing it, a whichever-tile
// refusal, the single-tile picture unchanged, a repeated group ask); a load at its count cap (a model whose texture it can
// neither ask for nor take from the session kept for its tile, never pooled; one the session has settled taken without
// asking); a new load over a live session with requests out
// (the old one ended first, its answers late); the entry failing (no session); clearTerrain taking everything away -- and
// every tally back to 0.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    const int StreamEntryX = 32, StreamEntryY = 50;
    const uint StreamWdtId = 990800;
    const uint StreamWmo = 9900, StreamWmoGroup = 9901, StreamWmoTex = 9902, StreamWmoCorner = 9903;
    const uint StreamM2A = 9910, StreamSkinA = 9911, StreamM2B = 9920, StreamSkinB = 9921, StreamM2C = 9925, StreamSkinC = 9926;
    const uint StreamM2Tex = 9930, StreamTerrainTex = 9940, StreamTerrainTexCorner = 9941;
    const uint StreamWmoGroupA = 9905, StreamWmoGroupB = 9906, StreamWmoGroupC = 9907;
    const uint StreamM2TexA = 9931, StreamM2D = 9915, StreamSkinD = 9916, StreamM2E = 9917, StreamSkinE = 9918;
    const uint StreamM2TexD = 9932, StreamM2TexE = 9933;

    static uint StreamRootId(int x, int y) { return (uint)(991000 + y * 64 + x); }
    static uint StreamTex0Id(int x, int y) { return (uint)(996000 + y * 64 + x); }
    static uint StreamObj0Id(int x, int y) { return (uint)(1001000 + y * 64 + x); }

    /// <summary>The fake host of a streaming session around a TerrainHarness: it records the session's requests (with their
    /// tiles and priorities), answers streamTile a round later as the IPC would, serves the files, and holds back what a test
    /// withholds.</summary>
    sealed class StreamHost
    {
        public readonly TerrainHarness H;
        public readonly TestWorldView V;
        public readonly WorldFiles Files;
        public readonly Dictionary<string, int> Priority = new Dictionary<string, int>();
        public readonly Dictionary<string, string> TileOf = new Dictionary<string, string>();
        public readonly List<List<int[]>> Opens = new List<List<int[]>>();
        public readonly List<List<int>> Closes = new List<List<int>>();
        public readonly List<WmvIpcClient.StreamTileAnswer> ToDeliver = new List<WmvIpcClient.StreamTileAnswer>();
        public readonly List<WmvIpcClient.StreamStateReport> States = new List<WmvIpcClient.StreamStateReport>();
        public readonly HashSet<string> Answered = new HashSet<string>();
        public readonly HashSet<string> WithheldTiles = new HashSet<string>();   // "x_y": their stream requests not answered
        public readonly HashSet<string> WithheldFiles = new HashSet<string>();   // "role:fid": not answered
        public readonly HashSet<string> WithheldAt = new HashSet<string>();      // "role:fid@x_y": not answered for that tile's load
        public readonly Dictionary<int, string> LoadTile = new Dictionary<int, string>();
        public readonly HashSet<string> Refused = new HashSet<string>();         // "role:fid@x_y": refused for that tile's load
        public readonly HashSet<string> RefusedAnyTile = new HashSet<string>();  // "role:fid": refused whatever the load (single-tile too)
        public readonly HashSet<string> RefuseFirst = new HashSet<string>();     // "role:fid": its first request, whichever tile, refused
        public readonly Dictionary<string, string> RefusedAt = new Dictionary<string, string>();   // ... and the tile it was for
        public readonly HashSet<string> OncePerLoadRoles = new HashSet<string>(); // roles answered as the host's "each once per load"
        readonly HashSet<string> onceAsked = new HashSet<string>();
        public bool HoldAnswers;                                                 // streamTile answers kept back
        public int MaxUnanswered;                                                // the most streamTile messages out at once
        public double[] Camera;
        public int NextLoad = 100;
        int next;

        public StreamHost(TerrainHarness h, TestWorldView v, WorldFiles files)
        {
            H = h;
            V = v;
            Files = files;
            WmvTerrainSession s = h.Session;
            s.WorldView = v;
            v.SentCount = () => h.Sent.Count;
            s.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
            s.RequestStreamMapAsset = (role, fid, load, epoch, tx, ty, priority) =>
            {
                string id = "s" + (++next);
                h.Sent.Add(new[] { id, role, fid.ToString(), load.ToString(), epoch.ToString() });
                Priority[id] = priority;
                TileOf[id] = tx + "_" + ty;
                return id;
            };
            s.SendStreamTile = (load, open, close) =>
            {
                Opens.Add(new List<int[]>(open));
                Closes.Add(new List<int>(close));
                var a = new WmvIpcClient.StreamTileAnswer { Load = load, AssetEpoch = 7 };
                foreach (int[] t in open)
                {
                    int tl = NextLoad++;
                    LoadTile[tl] = t[0] + "_" + t[1];
                    a.Opened.Add(new WmvIpcClient.StreamTileOpened { X = t[0], Y = t[1], TileLoad = tl });
                }
                a.Closed.AddRange(close);
                ToDeliver.Add(a);
                MaxUnanswered = Math.Max(MaxUnanswered, ToDeliver.Count);
            };
            s.ReportStreamState = r => States.Add(r);
            s.StreamCamera = () => Camera;
        }

        /// <summary>One round: streamTile answers delivered, every request not withheld answered, one frame.</summary>
        public void Round()
        {
            if (!HoldAnswers)
            {
                var deliver = new List<WmvIpcClient.StreamTileAnswer>(ToDeliver);
                ToDeliver.Clear();
                foreach (WmvIpcClient.StreamTileAnswer a in deliver)
                    H.Session.OnStreamTileAnswer(a);
            }
            foreach (string[] q in H.Sent.ToArray())
            {
                if (Answered.Contains(q[0]) || WithheldFiles.Contains(q[1] + ":" + q[2]))
                    continue;
                string tile;
                bool tiled = TileOf.TryGetValue(q[0], out tile);
                string file = q[1] + ":" + q[2];
                if (RefusedAnyTile.Contains(file) || (tiled && (Refused.Contains(file + "@" + tile) || RefuseFirst.Remove(file))))
                {
                    if (tiled && !RefusedAt.ContainsKey(file)) RefusedAt[file] = tile;
                    Answered.Add(q[0]);
                    H.Answer(q[0], null, "refused", "test: refused for the tile's load (its budget)");
                    continue;
                }
                if (tiled && (WithheldTiles.Contains(tile) || WithheldAt.Contains(file + "@" + tile)))
                    continue;
                if (OncePerLoadRoles.Contains(q[1]) && !onceAsked.Add(file + ":" + q[3]))
                {
                    Answered.Add(q[0]);
                    H.Answer(q[0], null, "refused", "test: load " + q[3] + " has asked for " + q[1] + " " + q[2] + " before: each once per load");
                    continue;
                }
                Answer(q);
            }
            H.Pump(1);
        }

        public bool Answer(string[] q)
        {
            Answered.Add(q[0]);
            Dictionary<int, byte[]> byId;
            byte[] data;
            if (Files.Files.TryGetValue(q[1], out byId) && byId.TryGetValue(int.Parse(q[2]), out data) && data != null)
                return H.Answer(q[0], data);
            return H.Answer(q[0], null, "not in client storage", "test: FileDataID " + q[2] + " is not served");
        }

        /// <summary>Rounds until `done` holds (at most `rounds`); false when it never did.</summary>
        public bool Until(Func<bool> done, int rounds = 600)
        {
            for (int k = 0; k < rounds; k++)
            {
                if (done())
                    return true;
                Round();
            }
            return done();
        }

        public int Asked(string role, uint fid)
        {
            int n = 0;
            foreach (string[] q in H.Sent) if (q[1] == role && q[2] == fid.ToString()) n++;
            return n;
        }

        public int AskedRole(string role)
        {
            int n = 0;
            foreach (string[] q in H.Sent) if (q[1] == role) n++;
            return n;
        }

        /// <summary>The free camera over the centre of tile (tx, ty).</summary>
        public void Over(int tx, int ty)
        {
            WorldOrigin o = WorldToUnity.TileOrigin(tx, ty);
            double half = TerrainCoordinates.TileSize * 0.5;
            Camera = new[] { o.X - half, o.Y - half, 120.0, 0.0, -1.0 };
        }

        /// <summary>Every tile of the session settled (no job left, none waiting to open or start).</summary>
        public bool Settled()
        {
            WmvTerrainSession s = H.Session;
            if (!s.Streaming || s.StreamJobs > 0 || ToDeliver.Count > 0)
                return false;
            foreach (string t in s.StreamTileStates())
                if (!t.EndsWith(":settled", StringComparison.Ordinal) && !t.EndsWith(":failed", StringComparison.Ordinal) &&
                    !t.EndsWith(":built", StringComparison.Ordinal))
                    return false;
            return true;
        }
    }

    static WmvIpcClient.TerrainTileCommand StreamCommand(int load)
    {
        WmvIpcClient.TerrainTileCommand c = DoodadCommand(load, false);
        c.TileX = StreamEntryX;
        c.TileY = StreamEntryY;
        c.WdtFileDataID = (int)StreamWdtId;
        c.MaxTextureBytes = 8 * 1024 * 1024;
        c.MaxTextureBytesPerLoad = 128 * 1024 * 1024;
        c.MaxTexturesPerLoad = 128;
        c.Streaming = new WmvIpcClient.StreamingOffer { Radius = 1, KeepRadius = 2, MaxResidentTiles = 16, RequestCredit = 24, MaxTileLoads = 24 };
        return c;
    }

    /// <summary>The synthetic map's files (see the file header).</summary>
    static WorldFiles StreamFiles()
    {
        var f = new WorldFiles();
        var main = new List<int>();
        var maid = new byte[4096 * 32];
        for (int y = 48; y <= 52; y++)
            for (int x = 30; x <= 34; x++)
            {
                int idx = y * 64 + x;
                main.Add(idx);
                AdtSynthetic.PutU32(maid, idx * 32, StreamRootId(x, y));
                AdtSynthetic.PutU32(maid, idx * 32 + 4, StreamObj0Id(x, y));
                AdtSynthetic.PutU32(maid, idx * 32 + 12, StreamTex0Id(x, y));
                f.Add(WmvTerrainSession.RoleRootAdt, StreamRootId(x, y),
                      AdtSynthetic.RootWith(x, y, n => new RootChunkSpec { Flags = 0x18000u, BaseHeight = 50f, Height = k => 0f }));
                bool corner = x == 30 && y == 48;
                f.Add(WmvTerrainSession.RoleTex0Adt, StreamTex0Id(x, y),
                      AdtSynthetic.Tex0(new AdtSynthetic.Tex0Spec { Mdid = new[] { corner ? StreamTerrainTexCorner : StreamTerrainTex } }));
                // Every tile one doodad of its own (M2 B); 32_50 and 32_51 share doodad 600 (M2 A); 32_50 and 33_50 share world
                // model placement 500, 31_50 has 501 (the same world model); the corner a doodad of M2 C and a world model of its own.
                var mddf = new List<byte[]> { AdtSynthetic.MddfRecord(StreamM2B, (uint)(700 + idx), 100f + x, 20f, 300f + y, 0f, 0f, 0f, 1024, 0x40) };
                var modf = new List<byte[]>();
                if (x == 32 && (y == 50 || y == 51))
                    mddf.Add(AdtSynthetic.MddfRecord(StreamM2A, 600, 150f, 20f, 350f, 0f, 30f, 0f, 1024, 0x40));
                if (y == 50 && (x == 32 || x == 33))
                    modf.Add(ModfWithSet(StreamWmo, 500, 0));
                if (y == 50 && x == 31)
                    modf.Add(ModfWithSet(StreamWmo, 501, 0));
                if (corner)
                {
                    mddf.Add(AdtSynthetic.MddfRecord(StreamM2C, 800, 160f, 20f, 360f, 0f, 0f, 0f, 1024, 0x40));
                    modf.Add(ModfWithSet(StreamWmoCorner, 502, 0));
                }
                f.Add(WmvTerrainSession.RoleObj0Adt, StreamObj0Id(x, y),
                      AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Chunk("MDDF", AdtSynthetic.Cat(mddf.ToArray())),
                                       AdtSynthetic.Chunk("MODF", AdtSynthetic.Cat(modf.ToArray()))));
            }
        f.Add(WmvTerrainSession.RoleWdt, StreamWdtId, AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0),
                                                                       AdtSynthetic.MainTable(main.ToArray()), AdtSynthetic.Chunk("MAID", maid)));
        // The world model: one group, one texture, doodad set 0 naming M2 A once.
        byte[] root = WmoSynthetic.Concat(WmoSynthetic.SquaresRoot(1, StreamWmoGroup, StreamWmoTex),
                                          WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 1)),
                                          WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(StreamM2A)),
                                          WmoSynthetic.Chunk("MODD", ModdRecord(0, 2f, 2f, 0f)));
        f.Add(WmvTerrainSession.RoleWmoRoot, StreamWmo, root);
        f.Add(WmvTerrainSession.RoleWmoRoot, StreamWmoCorner, WmoSynthetic.SquaresRoot(1, StreamWmoGroup, StreamWmoTex));
        f.Add(WmvTerrainSession.RoleWmoGroup, StreamWmoGroup, WmoSynthetic.SquaresGroup(0));
        f.Add(WmvTerrainSession.RoleWmoTexture, StreamWmoTex, RawBlp(13, 8, 8));
        f.Add(WmvTerrainSession.RoleM2Model, StreamM2A, DoodadM2((int)StreamSkinA, (int)StreamM2Tex));
        f.Add(WmvTerrainSession.RoleM2Skin, StreamSkinA, M2Synthetic.GeosetSkin(new[] { 0 }));
        f.Add(WmvTerrainSession.RoleM2Model, StreamM2B, DoodadM2((int)StreamSkinB, (int)StreamM2Tex));
        f.Add(WmvTerrainSession.RoleM2Skin, StreamSkinB, M2Synthetic.GeosetSkin(new[] { 0 }));
        f.Add(WmvTerrainSession.RoleM2Model, StreamM2C, DoodadM2((int)StreamSkinC, (int)StreamM2Tex));
        f.Add(WmvTerrainSession.RoleM2Skin, StreamSkinC, M2Synthetic.GeosetSkin(new[] { 0 }));
        f.Add(WmvTerrainSession.RoleM2Texture, StreamM2Tex, RawBlp(17, 8, 8));
        f.Add(WmvTerrainSession.RoleTexture, StreamTerrainTex, RawBlp(21, 8, 8));
        f.Add(WmvTerrainSession.RoleTexture, StreamTerrainTexCorner, RawBlp(23, 8, 8));
        return f;
    }

    static void TerrainStreamingTests(Action<string> log)
    {
        var views = new List<TestWorldView>();
        long poolGpu = WmvTerrainSession.StreamPoolGpuBudget, poolManaged = WmvTerrainSession.StreamPoolManagedBudget;
        try
        {
            StreamingSessionTests(log, views);
        }
        catch (Exception e)
        {
            Check(false, "streaming: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            WmvTerrainSession.StreamPoolGpuBudget = poolGpu;
            WmvTerrainSession.StreamPoolManagedBudget = poolManaged;
            foreach (TestWorldView v in views)
                v.Destroy();
        }
        Check(DoodadTallies() == "0/0/0/0/0/0/0" && WorldTallies() == "0/0/0/0/0" && WmvRuntimeTerrainTile.Live == 0 &&
              WmvRuntimeTerrainTile.LiveTextures == 0,
              "streaming: nothing of the streaming tests is left -- doodads " + DoodadTallies() + ", world models " + WorldTallies() +
              ", terrain tiles " + WmvRuntimeTerrainTile.Live + ", terrain textures " + WmvRuntimeTerrainTile.LiveTextures, log);
    }

    static int CountPlacements(WmvWorldScene s, uint uid)
    {
        int n = 0;
        if (s != null)
            foreach (WmvWorldInstance i in s.Instances)
                if (!i.Disposed && i.Placement != null && i.Placement.UniqueId == uid) n++;
        return n;
    }

    static int CountDoodads(WmvWorldScene s, DoodadSource source, uint uid)
    {
        int n = 0;
        if (s != null)
            foreach (WmvDoodadInstance i in s.Doodads)
                if (!i.Disposed && i.Doodad != null && i.Doodad.Source == source && i.Doodad.UniqueId == uid) n++;
        return n;
    }

    /// <summary>The tiles whose loads asked for (role, fid), in the order asked.</summary>
    static List<string> TilesAsked(StreamHost h, string role, uint fid)
    {
        var l = new List<string>();
        foreach (string[] q in h.H.Sent)
            if (q[1] == role && q[2] == fid.ToString())
            {
                string t;
                l.Add(h.TileOf.TryGetValue(q[0], out t) ? t : "?");
            }
        return l;
    }

    /// <summary>The live doodads drawn from a prototype with no texture (GetTexture returns before the cache for a slot with no
    /// decoded image: WmvModelBuilder.cs 3404-3408, so TextureRefs stays empty).</summary>
    static List<WmvDoodadInstance> Untextured(WmvWorldScene s)
    {
        var l = new List<WmvDoodadInstance>();
        if (s != null)
            foreach (WmvDoodadInstance i in s.Doodads)
                if (!i.Disposed && i.Prototype != null && i.Prototype.TextureRefs.Count == 0) l.Add(i);
        return l;
    }

    /// <summary>The exact (bit-equal) vertex positions two tiles' meshes share.</summary>
    static int SharedVertices(TerrainTileMesh a, TerrainTileMesh b)
    {
        if (a == null || b == null)
            return -1;
        var seen = new HashSet<string>();
        foreach (TerrainChunkMesh c in a.Chunks)
            if (c != null && c.Positions != null)
                for (int k = 0; k + 2 < c.Positions.Length; k += 3)
                    seen.Add(BitConverter.ToString(BitConverter.GetBytes(c.Positions[k])) + BitConverter.ToString(BitConverter.GetBytes(c.Positions[k + 1])) +
                             BitConverter.ToString(BitConverter.GetBytes(c.Positions[k + 2])));
        var both = new HashSet<string>();
        foreach (TerrainChunkMesh c in b.Chunks)
            if (c != null && c.Positions != null)
                for (int k = 0; k + 2 < c.Positions.Length; k += 3)
                {
                    string key = BitConverter.ToString(BitConverter.GetBytes(c.Positions[k])) + BitConverter.ToString(BitConverter.GetBytes(c.Positions[k + 1])) +
                                 BitConverter.ToString(BitConverter.GetBytes(c.Positions[k + 2]));
                    if (seen.Contains(key)) both.Add(key);
                }
        return both.Count;
    }

    static TerrainTileMesh MeshOfTile(WmvTerrainSession s, int tx, int ty)
    {
        WorldOrigin o = WorldToUnity.TileOrigin(tx, ty);
        double half = TerrainCoordinates.TileSize * 0.5;
        WmvRuntimeTerrainTile t;
        TerrainTileMesh m;
        return s.ResidentTileAt(o.X - half, o.Y - half, out t, out m) ? m : null;
    }

    static void StreamingSessionTests(Action<string> log, List<TestWorldView> views)
    {
        string rr = WmvTerrainSession.RoleRootAdt, rw = WmvTerrainSession.RoleWmoRoot, rm = WmvTerrainSession.RoleM2Model,
               rt = WmvTerrainSession.RoleTexture, rmt = WmvTerrainSession.RoleM2Texture, rwt = WmvTerrainSession.RoleWmoTexture;
        Func<StreamHost> make = () =>
        {
            var hh = new TerrainHarness();
            var vv = new TestWorldView();
            views.Add(vv);
            return new StreamHost(hh, vv, StreamFiles());
        };
        Func<StreamHost, string> state = x => x.H.Session.StreamSummary() + "; reports " + x.H.Reports.Count + ", requests " + x.H.Sent.Count +
                                              ", dropped late " + x.H.Session.LateAnswersDropped + " unowned " + x.H.Session.UnownedAnswersDropped +
                                              "; doodads " + DoodadTallies() + ", world " + WorldTallies() + ", tiles " + WmvRuntimeTerrainTile.Live;

        // ---- the region of nine around the entry
        StreamHost h = make();
        WmvTerrainSession s = h.H.Session;
        s.Load(StreamCommand(1));
        bool settled = h.Until(() => h.H.For(1) != null && h.Settled() && s.StreamTiles == 9);
        for (int k = 0; k < 20; k++) h.Round();     // (a streamState after the last answer)
        WmvIpcClient.TerrainTileReport entry = h.H.For(1);
        WmvWorldScene scene = s.World;
        List<int[]> firstOpen = h.Opens.Count > 0 ? h.Opens[0] : new List<int[]>();
        bool nearestFirst = firstOpen.Count == 8;
        foreach (int[] t in firstOpen)
            nearestFirst &= Math.Max(Math.Abs(t[0] - StreamEntryX), Math.Abs(t[1] - StreamEntryY)) == 1;
        Check(settled && entry != null && entry.Status == "built" && h.H.Reports.Count == 1 && s.StreamTiles == 9 && WmvRuntimeTerrainTile.Live == 9 &&
              nearestFirst && h.Closes[0].Count == 0,
              "streaming: the entry is reported built once and its 8 neighbours open in one streamTile once its tile is committed; all 9 settle " +
              "with no report of their own (" + state(h) + ")", log);
        Check(h.Asked(rw, StreamWmo) == 1 && h.Asked(WmvTerrainSession.RoleWmoGroup, StreamWmoGroup) == 1 && h.Asked(rwt, StreamWmoTex) == 1 &&
              h.Asked(rm, StreamM2A) == 1 && h.Asked(rm, StreamM2B) == 1 && h.Asked(rmt, StreamM2Tex) == 1 && h.Asked(rt, StreamTerrainTex) == 1 &&
              h.AskedRole(rr) == 9 && s.StreamFilesAskedAgain == 0,
              "streaming: what the tiles share is asked for once -- the world model, its group and texture, each M2 and the M2 texture (the two " +
              "M2s may be built by different tiles: the session's image), the terrain texture all nine tiles bind -- and each tile's root once (" +
              h.Asked(rw, StreamWmo) + " world model, " + h.Asked(WmvTerrainSession.RoleWmoGroup, StreamWmoGroup) + " group, " + h.Asked(rwt, StreamWmoTex) +
              " world-model texture, " + h.Asked(rm, StreamM2A) + "/" + h.Asked(rm, StreamM2B) + " M2s, " + h.Asked(rmt, StreamM2Tex) + " M2 texture, " +
              h.Asked(rt, StreamTerrainTex) + " terrain texture, " + h.AskedRole(rr) + " roots; " + s.StreamFilesAskedAgain + " asked again, " +
              s.StreamImagesShared + " image(s) shared)", log);
        Check(CountPlacements(scene, 500) == 1 && CountPlacements(scene, 501) == 1 && scene.Instances.Count == 2 && WmvWorldModels.LivePrototypes == 1 &&
              CountDoodads(scene, DoodadSource.Mddf, 600) == 1 && scene.Doodads.Count == 9 + 1 + 2 && WmvWorldDoodads.LivePrototypes == 2 &&
              s.SceneTerrainTextures == 1 && s.StreamPrototypesKept == 0 && s.StreamPlacementsReplaced + s.StreamDoodadsReplaced == 0,
              "streaming: placement 500 (listed by two tiles) and 501 placed once each over one prototype; doodad 600 (two tiles) once, a doodad " +
              "per tile, the world models' set doodads -- 12 doodads over 2 prototypes; one terrain texture in the scene (" + state(h) + ")", log);
        bool originsShared = true;
        WorldOrigin origin0 = WorldToUnity.TileOrigin(StreamEntryX, StreamEntryY);
        for (int y = 49; y <= 51; y++)
            for (int x = 31; x <= 33; x++)
            {
                TerrainTileMesh m = MeshOfTile(s, x, y);
                originsShared &= m != null && m.Origin.X == origin0.X && m.Origin.Y == origin0.Y && m.Origin.Z == origin0.Z;
            }
        int seamEast = SharedVertices(MeshOfTile(s, 32, 50), MeshOfTile(s, 33, 50));
        int seamSouth = SharedVertices(MeshOfTile(s, 32, 50), MeshOfTile(s, 32, 51));
        Check(originsShared && seamEast == 129 && seamSouth == 129,
              "streaming: every tile is built against the entry's origin, and neighbours share their 129 edge vertices bit for bit (east " +
              seamEast + ", south " + seamSouth + ")", log);
        bool priorities = true;
        foreach (KeyValuePair<string, int> kv in h.Priority)
        {
            string[] q = null;
            foreach (string[] x in h.H.Sent) if (x[0] == kv.Key) q = x;
            if (q == null) continue;
            int ring = h.TileOf[kv.Key] == StreamEntryX + "_" + StreamEntryY ? 0 : 1;
            int family = q[1] == rm || q[1] == WmvTerrainSession.RoleM2Skin || q[1] == rmt ? 2 : q[1] == rw || q[1] == WmvTerrainSession.RoleWmoGroup || q[1] == rwt ? 1 : 0;
            priorities &= kv.Value == 1 + 3 * ring + family;
        }
        Check(s.StreamRequestsOutMax <= 24 && s.StreamRequestsOutMax > 3 && priorities && s.AnswersDropped == 0,
              "streaming: at most 24 requests out for the whole session (most " + s.StreamRequestsOutMax + "), each with its tile's priority " +
              "1 + 3 x ring + family; no answer dropped", log);
        WmvIpcClient.StreamStateReport last = h.States.Count > 0 ? h.States[h.States.Count - 1] : null;
        Check(last != null && last.TilesResident == 9 && last.WorldInstances == 2 && last.DoodadInstances == 12 && last.Tiles.Count == 9 &&
              last.Load == 1 && last.FilesAnswered == last.FilesAsked,
              "streaming: streamState says the region -- 9 tiles resident, their placements and files (" +
              (last != null ? last.TilesResident + " resident, " + last.WorldInstances + "/" + last.DoodadInstances + " instances, files " +
                              last.FilesAnswered + "/" + last.FilesAsked : "none") + ")", log);

        // ---- the camera east by one tile: the next column opens, nothing closes (the keep zone)
        int sharedFilesBefore = h.Asked(rw, StreamWmo) + h.Asked(rm, StreamM2A) + h.Asked(rm, StreamM2B) + h.Asked(rt, StreamTerrainTex);
        h.Over(33, 50);
        settled = h.Until(() => h.Settled() && s.StreamTiles == 12);
        List<int[]> east = h.Opens.Count > 1 ? h.Opens[h.Opens.Count - 1] : new List<int[]>();
        bool column34 = east.Count == 3;
        foreach (int[] t in east) column34 &= t[0] == 34;
        Check(settled && column34 && s.StreamTiles == 12 && WmvRuntimeTerrainTile.Live == 12 &&
              h.Asked(rw, StreamWmo) + h.Asked(rm, StreamM2A) + h.Asked(rm, StreamM2B) + h.Asked(rt, StreamTerrainTex) == sharedFilesBefore,
              "streaming: over tile 33_50 the column 34 opens (3 tiles), the column 31 stays (ring 2); nothing resident is asked for again (" +
              state(h) + ")", log);

        // ---- two tiles further east: the columns 31 and 32 close; what only they held goes, what 33 holds stays
        h.Over(35, 50);
        settled = h.Until(() => h.Settled() && s.StreamTiles == 6);
        List<int> closed = h.Closes.Count > 0 ? h.Closes[h.Closes.Count - 1] : new List<int>();
        bool entryClosed = closed.Contains(1);
        scene = s.World;
        Check(settled && s.StreamTiles == 6 && WmvRuntimeTerrainTile.Live == 6 && closed.Count == 6 && entryClosed && h.H.Reports.Count == 1 &&
              CountPlacements(scene, 500) == 1 && CountPlacements(scene, 501) == 0 && CountDoodads(scene, DoodadSource.Mddf, 600) == 0 &&
              scene.Doodads.Count == 6 + 1 && s.ShownTile != null && s.ShownLoad != 1,
              "streaming: over tile 35_50 the six tiles of columns 31 and 32 close (the entry's among them, no second report); placement 501 and " +
              "doodad 600 go with them, placement 500 stays (33_50 holds it) with its set doodad; another tile is the shown one (" + state(h) + ")", log);

        // ---- the camera stops flying (the overview): the region stays where it last was -- it never goes back to the entry
        int opensBefore = h.Opens.Count;
        h.Camera = null;
        for (int k = 0; k < 30; k++) h.Round();
        Check(h.Opens.Count == opensBefore && s.StreamTiles == 6 && MeshOfTile(s, 32, 50) == null && MeshOfTile(s, 34, 50) != null && h.Settled(),
              "streaming: with the camera no longer flying the region stays around tile 35_50 -- nothing opens, nothing closes (" + state(h) + ")", log);

        // ---- back to the entry: the columns reopen from what is resident, nothing shared asked again
        int m2Asked = h.AskedRole(rm), wmoAsked = h.AskedRole(rw), texAsked = h.AskedRole(rt) + h.AskedRole(rmt) + h.AskedRole(rwt);
        h.Over(32, 50);
        settled = h.Until(() => h.Settled() && s.StreamTiles == 12);
        scene = s.World;
        Check(settled && h.AskedRole(rm) == m2Asked && h.AskedRole(rw) == wmoAsked && h.AskedRole(rt) + h.AskedRole(rmt) + h.AskedRole(rwt) == texAsked &&
              CountPlacements(scene, 501) == 1 && CountDoodads(scene, DoodadSource.Mddf, 600) == 1 && h.H.Reports.Count == 1,
              "streaming: back over the entry the closed tiles open again and place 501 and 600 again from the resident prototypes and " +
              "textures -- no model or texture asked again (" + state(h) + ")", log);

        // ---- the pools: the corner tile's model, world model and terrain texture stay pooled when it closes, and are used again
        h.Over(31, 49);
        settled = h.Until(() => h.Settled() && MeshOfTile(s, 30, 48) != null);
        int cAsked = h.Asked(rm, StreamM2C), cornerWmo = h.Asked(rw, StreamWmoCorner), cornerTex = h.Asked(rt, StreamTerrainTexCorner);
        int protosWith = WmvWorldDoodads.LivePrototypes, wmosWith = WmvWorldModels.LivePrototypes;
        h.Over(34, 51);
        bool away = h.Until(() => h.Settled() && MeshOfTile(s, 30, 48) == null);
        bool pooled = WmvWorldDoodads.LivePrototypes == protosWith && WmvWorldModels.LivePrototypes == wmosWith && s.StreamPrototypesEvicted == 0;
        h.Over(31, 49);
        bool back = h.Until(() => h.Settled() && MeshOfTile(s, 30, 48) != null);
        scene = s.World;
        Check(settled && cAsked == 1 && cornerWmo == 1 && cornerTex == 1 && away && pooled && back && h.Asked(rm, StreamM2C) == 1 &&
              h.Asked(rw, StreamWmoCorner) == 1 && h.Asked(rt, StreamTerrainTexCorner) == 1 && CountDoodads(scene, DoodadSource.Mddf, 800) == 1 &&
              CountPlacements(scene, 502) == 1,
              "streaming: the corner tile's M2, world model and terrain texture stay pooled while it is closed and serve it again without asking " +
              "(" + state(h) + ")", log);

        // ---- no pool budget: the next time they are evicted, and asked for again when the corner comes back
        WmvTerrainSession.StreamPoolGpuBudget = 0;
        WmvTerrainSession.StreamPoolManagedBudget = 0;
        h.Over(34, 51);
        away = h.Until(() => h.Settled() && MeshOfTile(s, 30, 48) == null);
        h.Round();
        bool evicted = s.StreamPrototypesEvicted >= 2 && s.StreamTexturesEvicted >= 1 && WmvWorldDoodads.LivePrototypes < protosWith &&
                       WmvWorldModels.LivePrototypes < wmosWith;
        string evictedState = state(h);
        h.Over(31, 49);
        back = h.Until(() => h.Settled() && MeshOfTile(s, 30, 48) != null);
        scene = s.World;
        Check(away && evicted && back && h.Asked(rm, StreamM2C) == 2 && h.Asked(rw, StreamWmoCorner) == 2 && h.Asked(rt, StreamTerrainTexCorner) == 2 &&
              CountDoodads(scene, DoodadSource.Mddf, 800) == 1 && CountPlacements(scene, 502) == 1,
              "streaming: with no pool budget the corner's prototypes and texture are evicted when it closes (" + evictedState + ") and asked for " +
              "again when it comes back (" + state(h) + ")", log);
        WmvTerrainSession.StreamPoolGpuBudget = 256L << 20;
        WmvTerrainSession.StreamPoolManagedBudget = 128L << 20;
        s.Clear("test");
        Check(!s.Streaming && WmvRuntimeTerrainTile.Live == 0 && s.SceneTerrainTextures == 0 && DoodadTallies() == "0/0/0/0/0/0/0" &&
              WorldTallies() == "0/0/0/0/0",
              "streaming: clearTerrain ends the session and takes every tile, placement, prototype and texture away (" + state(h) + ")", log);

        // ---- a tile closed with its requests out: retired, their late answers dropped, nothing built for it
        h = make();
        s = h.H.Session;
        h.WithheldTiles.Add("33_49");
        h.WithheldTiles.Add("33_50");
        h.WithheldTiles.Add("33_51");
        s.Load(StreamCommand(2));
        settled = h.Until(() => h.H.For(2) != null && s.StreamTiles == 9 && s.StreamJobs == 3);
        int pendingOf33 = 0;
        var withheld = new List<string[]>();
        foreach (string[] q in h.H.Sent)
        {
            string tile;
            if (h.TileOf.TryGetValue(q[0], out tile) && tile.StartsWith("33_", StringComparison.Ordinal) && !h.Answered.Contains(q[0]))
            {
                pendingOf33++;
                withheld.Add(q);
            }
        }
        int retiredBefore = s.RetiredTotal, lateBefore = s.LateAnswersDropped;
        h.Over(30, 50);
        bool gone = h.Until(() => s.StreamTiles <= 9 && MeshOfTile(s, 33, 50) == null && s.StreamJobs == 0 && h.Settled());
        int lateAnswered = 0;
        foreach (string[] q in withheld)
            if (!h.Answer(q)) lateAnswered++;
        h.Round();
        Check(settled && pendingOf33 > 0 && gone && s.RetiredTotal - retiredBefore >= pendingOf33 && lateAnswered == withheld.Count &&
              s.LateAnswersDropped - lateBefore == withheld.Count && MeshOfTile(s, 33, 50) == null,
              "streaming: the column 33 closed with " + pendingOf33 + " request(s) out: each retired, its answer coming afterwards dropped as late, " +
              "nothing built for it (" + state(h) + ")", log);
        s.Clear("test");

        // ---- a prototype fetch taken over: the entry fetching the world model closes, 33_50 waiting for it fetches it itself
        h = make();
        s = h.H.Session;
        h.WithheldFiles.Add(rw + ":" + StreamWmo);
        s.Load(StreamCommand(3));
        bool waiting = h.Until(() => h.Asked(rw, StreamWmo) == 1 && s.StreamTiles == 9 && MeshOfTile(s, 33, 50) != null, 800);
        for (int k = 0; k < 20; k++) h.Round();
        int entryRequest = 0;
        foreach (string[] q in h.H.Sent) if (q[1] == rw && q[3] == "3") entryRequest++;
        h.Over(35, 50);
        bool tookOver = h.Until(() => h.Asked(rw, StreamWmo) == 2, 400);
        h.WithheldFiles.Clear();
        settled = h.Until(() => h.Settled() && s.StreamTiles == 6, 800);
        scene = s.World;
        WmvIpcClient.TerrainTileReport r3 = h.H.For(3);
        string second = "";
        foreach (string[] q in h.H.Sent) if (q[1] == rw && q[3] != "3") second = h.LoadTile.ContainsKey(int.Parse(q[3])) ? h.LoadTile[int.Parse(q[3])] : q[3];
        Check(waiting && entryRequest == 1 && tookOver && settled && r3 != null && r3.Status == "built" && r3.Reason.Contains("closed by the streaming session") && h.H.CountFor(3) == 1 &&
              second == "33_50" && CountPlacements(scene, 500) == 1,
              "streaming: the entry closes with its world-model fetch out -- reported \"built\" once (the session goes on) -- and 33_50, which waited for that " +
              "fetch, asks for the world model under its own tile load and places 500 (taken over by " + second + "; " + state(h) + ")", log);
        s.Clear("test");

        // ---- one streamTile out at a time, a message of closes only too: its answer is never taken for a later open's
        h = make();
        s = h.H.Session;
        s.Load(StreamCommand(5));
        settled = h.Until(() => h.H.For(5) != null && h.Settled() && s.StreamTiles == 9);
        int messagesBefore = h.Opens.Count;
        h.Over(36, 50);                                  // no tile there: every tile of the region closes, nothing opens
        h.HoldAnswers = true;
        for (int k = 0; k < 3; k++) h.Round();
        bool closesOnly = h.Opens.Count == messagesBefore + 1 && h.Opens[h.Opens.Count - 1].Count == 0 && h.Closes[h.Closes.Count - 1].Count == 9;
        h.Over(33, 50);                                  // back: nine tiles wanted while the answer to the closes is out
        for (int k = 0; k < 5; k++) h.Round();
        bool waited = h.Opens.Count == messagesBefore + 1;
        h.HoldAnswers = false;
        bool reopened = h.Until(() => h.Settled() && s.StreamTiles == 9 && MeshOfTile(s, 34, 50) != null);
        WmvIpcClient.StreamStateReport lastState = null;
        for (int k = 0; k < 20; k++) h.Round();
        if (h.States.Count > 0) lastState = h.States[h.States.Count - 1];
        Check(settled && closesOnly && waited && reopened && h.MaxUnanswered == 1 && lastState != null && lastState.TilesFailed == 0 &&
              s.ShownTile != null,
              "streaming: a streamTile of closes only is answered before the next open goes (never two out; " + h.MaxUnanswered + " at most), " +
              "no tile refused or failed, and the region is back -- a tile shown again after every built tile had closed (" + state(h) + ")", log);
        s.Clear("test");

        // ---- a refusal for one tile's load (its budget) is that tile's failure, not the world model's: another tile fetches it
        h = make();
        s = h.H.Session;
        h.Refused.Add(rw + ":" + StreamWmo + "@" + StreamEntryX + "_" + StreamEntryY);
        s.Load(StreamCommand(6));
        settled = h.Until(() => h.H.For(6) != null && h.Settled() && s.StreamTiles == 9);
        scene = s.World;
        Check(settled && h.Asked(rw, StreamWmo) == 2 && CountPlacements(scene, 500) == 1 && CountPlacements(scene, 501) == 1 &&
              WmvWorldModels.LivePrototypes == 1 && s.StreamPrototypesKept == 0,
              "streaming: the world model refused for the entry's load is fetched under another tile's load and built -- 500 and 501 placed " +
              "(asked " + h.Asked(rw, StreamWmo) + " times; " + state(h) + ")", log);
        s.Clear("test");

        // ---- a texture file's own failure (not in client storage) is the session's: asked for once, every tile takes it
        h = make();
        s = h.H.Session;
        h.Files.Files[rmt].Remove((int)StreamM2Tex);
        s.Load(StreamCommand(11));
        settled = h.Until(() => h.H.For(11) != null && h.Settled() && s.StreamTiles == 9);
        scene = s.World;
        Check(settled && h.Asked(rmt, StreamM2Tex) == 1 && scene != null && scene.Doodads.Count == 12 && s.StreamPrototypesKept == 0,
              "streaming: an M2 texture missing from client storage is asked for once in the session -- every tile's models take " +
              "the failure without asking, and their doodads are placed untextured (asked " + h.Asked(rmt, StreamM2Tex) + " time(s); " +
              state(h) + ")", log);
        s.Clear("test");

        // ---- a world-model texture refused for the entry's load: the entry's world model is its own (kept for its tile), the
        // fetch handed on; the session's prototype is complete, and the complete placement replaces the entry's private one
        h = make();
        s = h.H.Session;
        string entryName = StreamEntryX + "_" + StreamEntryY, rwg = WmvTerrainSession.RoleWmoGroup;
        h.Refused.Add(rwt + ":" + StreamWmoTex + "@" + entryName);
        s.Load(StreamCommand(14));
        settled = h.Until(() => h.H.For(14) != null && h.Settled() && s.StreamTiles == 9 && WmvWorldModels.LivePrototypes == 1 &&
                                s.World != null && s.World.PrivatePrototypes.Count == 0, 800);
        scene = s.World;
        List<string> wt = TilesAsked(h, rwt, StreamWmoTex);
        bool allTextured = scene != null && scene.Instances.Count == 2;
        if (scene != null)
            foreach (WmvWorldInstance i in scene.Instances)
                allTextured &= !i.Disposed && i.Prototype != null && !i.Prototype.Disposed && i.Prototype.Runtime.PrimaryTexturesMissing == 0;
        Check(settled && h.H.For(14).Status == "built" && wt.Count == 2 && wt[0] == entryName && wt[1] != entryName &&
              h.Asked(rw, StreamWmo) == 2 && h.Asked(rwg, StreamWmoGroup) == 2 && CountPlacements(scene, 500) == 1 &&
              CountPlacements(scene, 501) == 1 && allTextured && scene.Prototypes.Count == 1 && s.StreamPrototypesKept == 1 &&
              s.StreamPlacementsReplaced == 1 && s.StreamDoodadsReplaced == 0 && scene.Doodads.Count == 12,
              "streaming: a world-model texture refused for the entry's load -- the entry's world model kept for its tile, its fetch " +
              "handed on; the session's prototype complete; 500 (two tiles) placed once from it, the private one replaced and gone (" +
              state(h) + ")", log);
        s.Clear("test");

        // ---- an M2 texture refused for the entry's load, its neighbours held back until it reports: the entry draws its doodads
        // untextured from its own M2s; the neighbours fetch them whole, and their complete doodads replace the entry's shared ones
        h = make();
        s = h.H.Session;
        for (int y = 49; y <= 51; y++)
            for (int x = 31; x <= 33; x++)
                if (x != StreamEntryX || y != StreamEntryY) h.WithheldTiles.Add(x + "_" + y);
        h.Refused.Add(rmt + ":" + StreamM2Tex + "@" + entryName);
        s.Load(StreamCommand(15));
        bool alone = h.Until(() => h.H.For(15) != null, 800);
        int entryUntextured = Untextured(s.World).Count, entryPrivate = s.World != null ? s.World.PrivateDoodadPrototypes.Count : -1;
        h.WithheldTiles.Clear();
        settled = h.Until(() => h.Settled() && s.StreamTiles == 9 && WmvWorldDoodads.LivePrototypes == 3, 800);
        for (int k = 0; k < 20; k++) h.Round();      // (a streamState after the last answer)
        scene = s.World;
        WmvIpcClient.StreamStateReport st15 = h.States.Count > 0 ? h.States[h.States.Count - 1] : null;
        List<string> mt = TilesAsked(h, rmt, StreamM2Tex);
        List<WmvDoodadInstance> u = Untextured(scene);
        uint entryOwn = (uint)(700 + StreamEntryY * 64 + StreamEntryX);   // 3932, the entry's M2 B doodad (StreamFiles)
        Check(alone && h.H.For(15).Status == "built" && entryUntextured == 3 && entryPrivate == 2 && settled &&
              st15 != null && st15.TilesFailed == 0 && !string.Join(" ", s.StreamTileStates().ToArray()).Contains(":failed") &&
              mt.Count == 2 && mt[0] == entryName && mt[1] != entryName &&
              h.Asked(rm, StreamM2A) == 2 && h.Asked(rm, StreamM2B) == 2 && scene.Doodads.Count == 12 &&
              CountDoodads(scene, DoodadSource.Mddf, 600) == 1 && u.Count == 1 && u[0].Doodad.Source == DoodadSource.Mddf &&
              u[0].Doodad.UniqueId == entryOwn && scene.PrivateDoodadPrototypes.Count == 1 && s.StreamPrototypesKept == 2 &&
              s.StreamDoodadsReplaced == 2 && s.StreamPlacementsReplaced == 0,
              "streaming: an M2 texture refused for the entry's load -- the entry draws its doodads untextured alone, its two M2s " +
              "kept for it, fetched again under another tile's load; 600 and the set doodad of 500 replaced by complete ones, only " +
              "the entry's own 3932 untextured (" + state(h) + ")", log);
        int rmBefore = h.AskedRole(rm), rmtBefore = h.AskedRole(rmt);
        h.Over(35, 50);
        bool gone15 = h.Until(() => h.Settled() && s.StreamTiles == 6 && WmvWorldDoodads.LivePrototypes == 2 &&
                                    s.World.PrivateDoodadPrototypes.Count == 0, 800);
        h.Over(32, 50);
        bool back15 = h.Until(() => h.Settled() && s.StreamTiles == 12, 800);
        scene = s.World;
        Check(gone15 && back15 && h.AskedRole(rm) == rmBefore && h.AskedRole(rmt) == rmtBefore && Untextured(scene).Count == 0 &&
              CountDoodads(scene, DoodadSource.Mddf, entryOwn) == 1 && WmvWorldDoodads.LivePrototypes == 2 && s.StreamPrivatePrototypes == 0,
              "streaming: the entry's tile closed and opened again places 3932 from the session's complete M2 B -- nothing asked, " +
              "nothing private left (" + state(h) + ")", log);
        s.Clear("test");

        // ---- one of the world model's two group files refused for the entry's load: the entry's world model is its own, the
        // session's built with both groups under another tile's load
        h = make();
        s = h.H.Session;
        h.Files.Add(rw, StreamWmo, WmoSynthetic.Concat(WmoSynthetic.SquaresRoot(2, StreamWmoGroupA, StreamWmoTex),
                                                       WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 1)),
                                                       WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(StreamM2A)),
                                                       WmoSynthetic.Chunk("MODD", ModdRecord(0, 2f, 2f, 0f))));
        h.Files.Add(rwg, StreamWmoGroupA, WmoSynthetic.SquaresGroup(0));
        h.Files.Add(rwg, StreamWmoGroupB, WmoSynthetic.SquaresGroup(1));
        h.Refused.Add(rwg + ":" + StreamWmoGroupB + "@" + entryName);
        s.Load(StreamCommand(16));
        settled = h.Until(() => h.H.For(16) != null && h.Settled() && s.StreamTiles == 9 && WmvWorldModels.LivePrototypes == 1 &&
                                s.World != null && s.World.PrivatePrototypes.Count == 0, 800);
        scene = s.World;
        bool whole = scene != null && scene.Instances.Count == 2;
        if (scene != null)
            foreach (WmvWorldInstance i in scene.Instances)
                whole &= i.Prototype.Groups.Length == 2 && Array.IndexOf(i.Prototype.Groups, null) < 0 && i.Prototype.Runtime.GroupsMissing == 0;
        List<string> gt = TilesAsked(h, rwg, StreamWmoGroupB);
        Check(settled && gt.Count == 2 && gt[0] == entryName && gt[1] != entryName && h.Asked(rw, StreamWmo) == 2 &&
              h.Asked(rwg, StreamWmoGroupA) == 2 && h.Asked(rwt, StreamWmoTex) == 1 && whole && CountPlacements(scene, 500) == 1 &&
              CountPlacements(scene, 501) == 1 && s.StreamPrototypesKept == 1 && s.StreamPlacementsReplaced == 1 &&
              scene.Doodads.Count == 12,
              "streaming: a group file refused for the entry's load -- the entry's world model kept for it, the session's built with " +
              "both groups under another tile's load (" + state(h) + ")", log);
        s.Clear("test");

        // ---- the world-model texture refused for every tile that lists the world model: nothing complete exists, so nothing is
        // published or pooled, and each placement keeps one instance (a private finder takes the private instance there)
        h = make();
        s = h.H.Session;
        foreach (string t in new[] { "31_50", "32_50", "33_50" }) h.Refused.Add(rwt + ":" + StreamWmoTex + "@" + t);
        s.Load(StreamCommand(17));
        settled = h.Until(() => h.H.For(17) != null && h.Settled() && s.StreamTiles == 9 && WmvWorldModels.LivePrototypes == 2 &&
                                s.World != null && s.World.PrivatePrototypes.Count == 2, 800);
        scene = s.World;
        bool allMissing = scene != null && scene.Instances.Count == 2;
        if (scene != null)
            foreach (WmvWorldInstance i in scene.Instances) allMissing &= i.Prototype.Runtime.PrimaryTexturesMissing > 0;
        Check(settled && h.Asked(rwt, StreamWmoTex) == 3 && h.Asked(rw, StreamWmo) == 3 && CountPlacements(scene, 500) == 1 &&
              CountPlacements(scene, 501) == 1 && allMissing && scene.Prototypes.Count == 0 && s.StreamPrototypesKept == 3 &&
              s.StreamPlacementsReplaced == 0 && scene.Doodads.Count == 12,
              "streaming: a world-model texture refused for every tile that lists the world model -- each load asked once and kept " +
              "its own; 33_50 took the entry's private 500 (no second instance), its own private placed nothing and went; nothing " +
              "published (" + state(h) + ")", log);
        s.Clear("test");

        // ---- the single-tile load keeps Stage 07's picture: a texture refused for it is drawn missing, the prototype its scene's
        h = make();
        s = h.H.Session;
        h.RefusedAnyTile.Add(rwt + ":" + StreamWmoTex);
        WmvIpcClient.TerrainTileCommand one = StreamCommand(18);
        one.Streaming = null;
        s.Load(one);
        h.Until(() => h.H.For(18) != null, 400);
        scene = s.World;
        Check(h.H.For(18) != null && h.H.For(18).Status == "built" && !s.Streaming && h.Asked(rwt, StreamWmoTex) == 1 &&
              scene != null && CountPlacements(scene, 500) == 1 && scene.Instances.Count == 1 &&
              scene.Instances[0].Prototype.Runtime.PrimaryTexturesMissing > 0 && scene.Prototypes.Count == 1 &&
              scene.PrivatePrototypes.Count == 0 && WmvWorldModels.LivePrototypes == 1,
              "streaming: a world-model texture refused for a single-tile load is drawn missing as at Stage 07 -- built, shared " +
              "in its scene, nothing private (" + state(h) + ")", log);
        s.Clear("test");

        // ---- an M2 texture refused for whichever tile asks for it first (any claim order): only that tile's own doodads are
        // untextured, every M2 it built is kept for it and fetched again; with every tile closed nothing private is left
        h = make();
        s = h.H.Session;
        string m2TexKey = rmt + ":" + StreamM2Tex;
        h.RefuseFirst.Add(m2TexKey);
        s.Load(StreamCommand(19));
        settled = h.Until(() => h.H.For(19) != null && h.Settled() && s.StreamTiles == 9, 800);
        for (int k = 0; k < 40; k++) h.Round();      // (the sweep, and a streamState)
        scene = s.World;
        string refusedTile;
        bool fired = h.RefusedAt.TryGetValue(m2TexKey, out refusedTile);
        int refusedLoad = refusedTile == entryName ? 19 : -1;
        foreach (KeyValuePair<int, string> kv in h.LoadTile) if (kv.Value == refusedTile) refusedLoad = kv.Key;
        bool askedA = TilesAsked(h, rm, StreamM2A).Contains(refusedTile), askedB = TilesAsked(h, rm, StreamM2B).Contains(refusedTile);
        int untexturedElsewhere = 0;
        bool textured600 = false;
        foreach (WmvDoodadInstance i in scene.Doodads)
        {
            if (i.Disposed || i.Prototype == null) continue;
            bool bare = i.Prototype.TextureRefs.Count == 0;
            if (bare && i.Load != refusedLoad) untexturedElsewhere++;
            if (i.Doodad.Source == DoodadSource.Mddf && i.Doodad.UniqueId == 600) textured600 = !bare;
        }
        WmvIpcClient.StreamStateReport st19 = h.States.Count > 0 ? h.States[h.States.Count - 1] : null;
        Check(settled && fired && (askedA || askedB) && h.Asked(rmt, StreamM2Tex) == 2 && h.Asked(rm, StreamM2A) == (askedA ? 2 : 1) &&
              h.Asked(rm, StreamM2B) == (askedB ? 2 : 1) && s.StreamPrototypesKept == (askedA ? 1 : 0) + (askedB ? 1 : 0) &&
              untexturedElsewhere == 0 && textured600 && CountDoodads(scene, DoodadSource.Mddf, 600) == 1 && scene.Doodads.Count == 12 &&
              WmvWorldDoodads.LivePrototypes == 2 + s.StreamPrivatePrototypes && st19 != null && st19.TilesFailed == 0,
              "streaming: an M2 texture refused for the first tile that asks (" + refusedTile + ") -- only that tile's own doodads " +
              "untextured, every M2 it built kept and fetched again (" + state(h) + ")", log);
        h.Over(36, 50);                               // no tile there: all nine close
        bool away19 = h.Until(() => h.Settled() && s.StreamTiles == 0 && s.StreamPrivatePrototypes == 0, 400);
        bool keptGone = WmvWorldDoodads.LivePrototypes == 2 && WmvWorldModels.LivePrototypes == 1;
        int m2Asked19 = h.AskedRole(rm), m2Tex19 = h.AskedRole(rmt);
        h.Over(StreamEntryX, StreamEntryY);
        bool back19 = h.Until(() => h.Settled() && s.StreamTiles == 9, 800);
        scene = s.World;
        Check(away19 && keptGone && back19 && Untextured(scene).Count == 0 && scene.Doodads.Count == 12 && h.AskedRole(rm) == m2Asked19 &&
              h.AskedRole(rmt) == m2Tex19 && WmvWorldDoodads.LivePrototypes == 2,
              "streaming: with every tile closed nothing private is left; back, every doodad placed textured from the pool, nothing " +
              "asked (" + state(h) + ")", log);
        s.Clear("test");

        // ---- a repeated group ask within one load is not a gap of the load: two world models of the entry share group 9906,
        // which the host refuses the second time ("each once per load")
        h = make();
        s = h.H.Session;
        h.OncePerLoadRoles.Add(rwg);
        h.Files.Add(rw, StreamWmo, WmoSynthetic.Concat(WmoSynthetic.SquaresRoot(2, StreamWmoGroupA, StreamWmoTex),
                                                       WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 1)),
                                                       WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(StreamM2A)),
                                                       WmoSynthetic.Chunk("MODD", ModdRecord(0, 2f, 2f, 0f))));   // groups 9905, 9906
        h.Files.Add(rwg, StreamWmoGroupA, WmoSynthetic.SquaresGroup(0));
        h.Files.Add(rwg, StreamWmoGroupB, WmoSynthetic.SquaresGroup(1));
        h.Files.Add(rw, StreamWmoCorner, WmoSynthetic.SquaresRoot(2, StreamWmoGroupB, StreamWmoTex));            // groups 9906, 9907
        h.Files.Add(rwg, StreamWmoGroupC, WmoSynthetic.SquaresGroup(1));
        int eidx = StreamEntryY * 64 + StreamEntryX;
        h.Files.Add(WmvTerrainSession.RoleObj0Adt, StreamObj0Id(StreamEntryX, StreamEntryY), AdtSynthetic.Cat(AdtSynthetic.Mver(),
            AdtSynthetic.Chunk("MDDF", AdtSynthetic.Cat(
                AdtSynthetic.MddfRecord(StreamM2B, (uint)(700 + eidx), 100f + StreamEntryX, 20f, 300f + StreamEntryY, 0f, 0f, 0f, 1024, 0x40),
                AdtSynthetic.MddfRecord(StreamM2A, 600, 150f, 20f, 350f, 0f, 30f, 0f, 1024, 0x40))),
            AdtSynthetic.Chunk("MODF", AdtSynthetic.Cat(ModfWithSet(StreamWmo, 500, 0), ModfWithSet(StreamWmoCorner, 503, 0)))));
        s.Load(StreamCommand(20));
        settled = h.Until(() => h.H.For(20) != null && h.Settled() && s.StreamTiles == 9, 800);
        scene = s.World;
        Check(settled && s.StreamGroupRepeats == 1 && s.StreamPrototypesKept == 0 && h.Asked(rwg, StreamWmoGroupB) == 2 &&
              h.Asked(rw, StreamWmo) == 1 && h.Asked(rw, StreamWmoCorner) == 1 && CountPlacements(scene, 500) == 1 &&
              CountPlacements(scene, 501) == 1 && CountPlacements(scene, 503) == 1 && scene.PrivatePrototypes.Count == 0,
              "streaming: a group file two world models of the entry share is refused the second time by the host's once-per-load " +
              "rule -- counted as a repeat, not the load's gap: nothing kept, nothing fetched again (" + state(h) + ")", log);
        s.Clear("test");

        // ---- a load at its count cap (maxM2TexturesPerLoad 1): the entry places two M2s no other tile lists (D, E), each with a
        // texture of its own; it asks for one, and the other it can neither ask for nor take from the session -- that M2 is the
        // entry's gap: drawn without its texture for the entry alone, kept for it, never the session's or pooled (no other tile
        // wants it, so nothing fetches it whole). Closed, nothing of it is left; back, the complete M2 comes from the pool and the
        // other is fetched again, whole now that the load's one texture is its own
        h = make();
        s = h.H.Session;
        h.Files.Add(rm, StreamM2D, DoodadM2((int)StreamSkinD, (int)StreamM2TexD));
        h.Files.Add(WmvTerrainSession.RoleM2Skin, StreamSkinD, M2Synthetic.GeosetSkin(new[] { 0 }));
        h.Files.Add(rmt, StreamM2TexD, RawBlp(19, 8, 8));
        h.Files.Add(rm, StreamM2E, DoodadM2((int)StreamSkinE, (int)StreamM2TexE));
        h.Files.Add(WmvTerrainSession.RoleM2Skin, StreamSkinE, M2Synthetic.GeosetSkin(new[] { 0 }));
        h.Files.Add(rmt, StreamM2TexE, RawBlp(25, 8, 8));
        h.Files.Add(WmvTerrainSession.RoleObj0Adt, StreamObj0Id(StreamEntryX, StreamEntryY), AdtSynthetic.Cat(AdtSynthetic.Mver(),
            AdtSynthetic.Chunk("MDDF", AdtSynthetic.Cat(
                AdtSynthetic.MddfRecord(StreamM2D, 901, 120f + StreamEntryX, 20f, 310f + StreamEntryY, 0f, 0f, 0f, 1024, 0x40),
                AdtSynthetic.MddfRecord(StreamM2E, 902, 130f + StreamEntryX, 20f, 320f + StreamEntryY, 0f, 0f, 0f, 1024, 0x40))),
            AdtSynthetic.Chunk("MODF", new byte[0])));
        WmvIpcClient.TerrainTileCommand capped = StreamCommand(21);
        capped.MaxM2TexturesPerLoad = 1;
        s.Load(capped);
        settled = h.Until(() => h.H.For(21) != null && h.Settled() && s.StreamTiles == 9, 800);
        for (int k = 0; k < 40; k++) h.Round();      // (the sweep, and a streamState)
        scene = s.World;
        List<string> capD = TilesAsked(h, rmt, StreamM2TexD), capE = TilesAsked(h, rmt, StreamM2TexE);
        List<WmvDoodadInstance> bareCap = Untextured(scene);
        bool dAsked = capD.Count == 1;
        uint gapModel = dAsked ? StreamM2E : StreamM2D, wholeModel = dAsked ? StreamM2D : StreamM2E;
        uint gapTex = dAsked ? StreamM2TexE : StreamM2TexD, gapDoodad = dAsked ? 902u : 901u;
        WmvDoodadPrototype gapProto = bareCap.Count == 1 ? bareCap[0].Prototype : null;
        WmvIpcClient.StreamStateReport st21 = h.States.Count > 0 ? h.States[h.States.Count - 1] : null;
        Check(settled && st21 != null && st21.TilesFailed == 0 && capD.Count + capE.Count == 1 && (dAsked ? capD[0] : capE[0]) == entryName &&
              s.StreamPrototypesKept == 1 && s.StreamPrivatePrototypes == 1 && s.StreamDoodadsReplaced == 0 && bareCap.Count == 1 &&
              bareCap[0].Doodad.UniqueId == gapDoodad && gapProto != null && scene.PrivateDoodadPrototypes.Contains(gapProto) &&
              !scene.DoodadPrototypes.ContainsValue(gapProto) && h.Asked(rm, StreamM2D) == 1 && h.Asked(rm, StreamM2E) == 1,
              "streaming: at its count cap the entry can neither ask for nor take one of its two own M2s' textures -- that M2 drawn " +
              "without it for the entry alone, kept for the entry, not the session's (asked " + string.Join(" ", capD.ToArray()) + " / " +
              string.Join(" ", capE.ToArray()) + "; untextured " + bareCap.Count + "; " + state(h) + ")", log);
        h.Over(36, 50);                               // no tile there: all nine close
        bool away21 = h.Until(() => h.Settled() && s.StreamTiles == 0 && s.StreamPrivatePrototypes == 0, 400);
        h.Over(StreamEntryX, StreamEntryY);
        bool back21 = h.Until(() => h.Settled() && s.StreamTiles == 9, 800);
        for (int k = 0; k < 40; k++) h.Round();
        scene = s.World;
        Check(away21 && back21 && Untextured(scene).Count == 0 && h.Asked(rm, wholeModel) == 1 && h.Asked(rm, gapModel) == 2 &&
              h.Asked(rmt, gapTex) == 1 && s.StreamPrototypesKept == 1 && s.StreamPrivatePrototypes == 0 &&
              scene.PrivateDoodadPrototypes.Count == 0,
              "streaming: the entry closed, nothing private of it is left; back, the complete M2 is taken from the pool and the kept " +
              "one fetched again, whole now -- nothing untextured (" + state(h) + ")", log);
        s.Clear("test");

        // ---- at its count cap a load takes what the session has settled of a texture, without asking: 33_50 places an M2 C with
        // A's texture and fetches it while the entry's skin of A is held back; then the entry, at its cap, takes the image
        h = make();
        s = h.H.Session;
        h.Files.Add(rm, StreamM2A, DoodadM2((int)StreamSkinA, (int)StreamM2TexA));
        h.Files.Add(rm, StreamM2C, DoodadM2((int)StreamSkinC, (int)StreamM2TexA));
        h.Files.Add(rmt, StreamM2TexA, RawBlp(19, 8, 8));
        int nidx = StreamEntryY * 64 + StreamEntryX + 1;   // 33_50
        h.Files.Add(WmvTerrainSession.RoleObj0Adt, StreamObj0Id(StreamEntryX + 1, StreamEntryY), AdtSynthetic.Cat(AdtSynthetic.Mver(),
            AdtSynthetic.Chunk("MDDF", AdtSynthetic.Cat(
                AdtSynthetic.MddfRecord(StreamM2B, (uint)(700 + nidx), 100f + StreamEntryX + 1, 20f, 300f + StreamEntryY, 0f, 0f, 0f, 1024, 0x40),
                AdtSynthetic.MddfRecord(StreamM2C, 801, 160f, 20f, 360f, 0f, 0f, 0f, 1024, 0x40))),
            AdtSynthetic.Chunk("MODF", ModfWithSet(StreamWmo, 500, 0))));
        h.WithheldAt.Add(WmvTerrainSession.RoleM2Skin + ":" + StreamSkinA + "@" + entryName);
        capped = StreamCommand(22);
        capped.MaxM2TexturesPerLoad = 1;
        s.Load(capped);
        bool texReady = h.Until(() => h.Asked(rmt, StreamM2TexA) == 1 && h.Settled() == false &&
                                      TilesAsked(h, rmt, StreamM2TexA)[0] == (StreamEntryX + 1) + "_" + StreamEntryY &&
                                      h.Answered.Count > 0 && s.World != null && Untextured(s.World).Count == 0 &&
                                      CountDoodads(s.World, DoodadSource.Mddf, 801) == 1, 800);
        h.WithheldAt.Clear();
        settled = h.Until(() => h.H.For(22) != null && h.Settled() && s.StreamTiles == 9, 800);
        for (int k = 0; k < 40; k++) h.Round();
        scene = s.World;
        Check(texReady && settled && s.StreamPrototypesKept == 0 && s.StreamDoodadsReplaced == 0 && h.Asked(rmt, StreamM2TexA) == 1 &&
              h.Asked(rm, StreamM2A) == 1 && Untextured(scene).Count == 0 && CountDoodads(scene, DoodadSource.Mddf, 801) == 1 &&
              scene.Doodads.Count == 13 && scene.PrivateDoodadPrototypes.Count == 0,
              "streaming: at its count cap the entry takes M2 A's texture from the session (fetched for 33_50's M2 C) without " +
              "asking -- nothing kept, nothing asked twice, every doodad textured (" + state(h) + ")", log);
        s.Clear("test");

        // ---- a new load over a live session with fetches out (a new Enter world): the old session ends first -- its requests
        // retired, their late answers dropped, nothing asked for its tiles after the new load, nothing of it in the new scene
        h = make();
        s = h.H.Session;
        h.WithheldTiles.Add("33_50");
        h.WithheldTiles.Add("31_50");
        s.Load(StreamCommand(12));
        bool liveOld = h.Until(() => h.H.For(12) != null && s.StreamTiles == 9 && s.StreamJobs >= 2, 400);
        var oldLoads = new HashSet<string>();
        int oldOut = 0;
        foreach (string[] q in h.H.Sent)
        {
            oldLoads.Add(q[3]);
            if (!h.Answered.Contains(q[0])) oldOut++;
        }
        int sentBefore = h.H.Sent.Count, lateBefore12 = s.LateAnswersDropped;
        s.Load(StreamCommand(13));
        h.WithheldTiles.Clear();
        settled = h.Until(() => h.H.For(13) != null && h.Settled() && s.StreamTiles == 9);
        int oldAskedAfter = 0;
        for (int k = sentBefore; k < h.H.Sent.Count; k++)
            if (oldLoads.Contains(h.H.Sent[k][3])) oldAskedAfter++;
        scene = s.World;
        Check(liveOld && oldOut > 0 && settled && oldAskedAfter == 0 && s.LateAnswersDropped - lateBefore12 >= oldOut &&
              h.H.CountFor(12) == 1 && h.H.For(13) != null && h.H.For(13).Status == "built" && WmvRuntimeTerrainTile.Live == 9 &&
              scene != null && scene.Load == 13 && scene.Doodads.Count == 12 && CountPlacements(scene, 500) == 1,
              "streaming: a new load over a live session with " + oldOut + " request(s) out ends the old session first -- nothing asked " +
              "for its tiles afterwards (" + oldAskedAfter + "), its answers dropped as late (" + (s.LateAnswersDropped - lateBefore12) +
              "), reported once; the new session's region is whole and holds nothing of the old (" + state(h) + ")", log);
        s.Clear("test");

        // ---- the entry failing before its tile: no session, nothing opened
        h = make();
        s = h.H.Session;
        h.Files.Files[WmvTerrainSession.RoleWdt].Remove((int)StreamWdtId);
        s.Load(StreamCommand(4));
        h.Until(() => h.H.For(4) != null, 50);
        WmvIpcClient.TerrainTileReport r4 = h.H.For(4);
        Check(r4 != null && r4.Status == "failed" && !s.Streaming && h.Opens.Count == 0,
              "streaming: an entry whose WDT cannot be had fails, and no session begins (" + (r4 != null ? r4.Status + ": " + r4.Reason : "no report") + ")", log);
        s.Clear("test");

        // ---- a load without "streaming": the single-tile load, nothing opened
        h = make();
        s = h.H.Session;
        WmvIpcClient.TerrainTileCommand single = StreamCommand(5);
        single.Streaming = null;
        s.Load(single);
        h.Until(() => h.H.For(5) != null, 400);
        Check(h.H.For(5) != null && h.H.For(5).Status == "built" && !s.Streaming && h.Opens.Count == 0 && h.Priority.Count == 0,
              "streaming: a loadWorld without \"streaming\" is the single-tile load as before: no session, no tile opened, no priority (" +
              state(h) + ")", log);
        s.Clear("test");
    }
}
