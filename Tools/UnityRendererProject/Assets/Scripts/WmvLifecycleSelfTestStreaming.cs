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
// on to another tile; the entry failing (no session); clearTerrain taking everything away -- and every tally back to 0.

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
        public readonly Dictionary<int, string> LoadTile = new Dictionary<int, string>();
        public readonly HashSet<string> Refused = new HashSet<string>();         // "role:fid@x_y": refused for that tile's load
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
                if (tiled && Refused.Contains(q[1] + ":" + q[2] + "@" + tile))
                {
                    Answered.Add(q[0]);
                    H.Answer(q[0], null, "refused", "test: refused for the tile's load (its budget)");
                    continue;
                }
                if (tiled && WithheldTiles.Contains(tile))
                    continue;
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
              s.SceneTerrainTextures == 1,
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
              WmvWorldModels.LivePrototypes == 1,
              "streaming: the world model refused for the entry's load is fetched under another tile's load and built -- 500 and 501 placed " +
              "(asked " + h.Asked(rw, StreamWmo) + " times; " + state(h) + ")", log);
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
