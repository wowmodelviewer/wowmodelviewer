// WmvLifecycleSelfTestDoodads.cs
//
// The lifecycle self-test's doodads (protocol 17, docs/world-viewer/07-doodads.md): the terrain session against a fake host
// with synthetic files only -- a tile whose obj0 places M2 doodads (MDDF) and a world model whose root carries doodad sets
// (MODS / MODI / MODD), synthetic M2s (MD21 with SFID and TXID), their skins and a raw BLP2 texture they share. The doodads
// are built by the real WmvWorldDoodads (the Models builder, static) under the test view's inactive root, disposed at once.
// Checked: the plan (MDDF by uniqueId, the placement's doodad set beside set 0, an unsupported record), one prototype per M2
// and one upload per texture, the camera's moment before any doodad request, the report's doodads and the world's assembly,
// the transforms; a protocol-16 load (no doodad budgets: nothing asked, "notOffered"); the budgets; a model that is not an
// M2; a build that throws; the doodads' timeout; a WMO-only map's doodads; a world model built without one of its group files
// (the doodad no read group lists is "unsupported"); and the load ended at every stage -- an M2 read
// out, a decode applied after the load ended, between prototype builds, between placements, a newer load -- each reported
// once, every late answer dropped, and every tally back to 0.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    static string DoodadTallies()
    {
        return WmvWorldDoodads.LiveInstances + "/" + WmvWorldDoodads.LivePrototypes + "/" + WmvWorldDoodads.LiveMeshes + "/" +
               WmvWorldDoodads.LiveMaterials + "/" + WmvWorldDoodads.LiveTextures + "/" + WmvWorldDoodads.LiveGpuGeometryBytes + "/" +
               WmvWorldDoodads.LiveGpuTextureBytes;
    }

    static WmvIpcClient.TerrainTileCommand DoodadCommand(int load, bool global, int maxModels = 1024, int maxInstances = 16384, int maxTextures = 2048)
    {
        WmvIpcClient.TerrainTileCommand c = WorldCommand(load, global);
        c.Doodads = true;
        c.MaxM2FileBytes = 8 * 1024 * 1024;
        c.MaxM2BytesPerLoad = 128 * 1024 * 1024;
        c.MaxM2TextureBytes = 8 * 1024 * 1024;
        c.MaxM2TextureBytesPerLoad = 128 * 1024 * 1024;
        c.MaxM2ModelsPerLoad = maxModels;
        c.MaxM2TexturesPerLoad = maxTextures;
        c.MaxDoodadInstancesPerLoad = maxInstances;
        return c;
    }

    /// <summary>A chunked M2: MD21 holding a one-triangle model (M2Synthetic.GeosetModel), SFID naming its skin, TXID its
    /// texture.</summary>
    static byte[] DoodadM2(int skinId, int textureId, int submeshes = 1)
    {
        byte[] md20 = M2Synthetic.GeosetModel(submeshes);
        var parts = new List<byte>();
        Action<string, byte[]> chunk = (tag, payload) =>
        {
            foreach (char ch in tag) parts.Add((byte)ch);
            parts.AddRange(BitConverter.GetBytes(payload.Length));
            parts.AddRange(payload);
        };
        chunk("MD21", md20);
        chunk("SFID", BitConverter.GetBytes(skinId));
        chunk("TXID", BitConverter.GetBytes(textureId));
        return parts.ToArray();
    }

    static byte[] ModsRecord(string name, uint start, uint count)
    {
        var r = new byte[32];
        for (int i = 0; i < name.Length && i < 19; i++) r[i] = (byte)name[i];
        BitConverter.GetBytes(start).CopyTo(r, 20);
        BitConverter.GetBytes(count).CopyTo(r, 24);
        return r;
    }

    static byte[] ModdRecord(uint nameIndex, float x, float y, float z, float scale = 1f)
    {
        var r = new byte[40];
        BitConverter.GetBytes(nameIndex).CopyTo(r, 0);
        BitConverter.GetBytes(x).CopyTo(r, 4);
        BitConverter.GetBytes(y).CopyTo(r, 8);
        BitConverter.GetBytes(z).CopyTo(r, 12);
        BitConverter.GetBytes(1f).CopyTo(r, 28);              // quaternion x, y, z, w = (0, 0, 0, 1)
        BitConverter.GetBytes(scale).CopyTo(r, 32);
        return r;
    }

    /// <summary>A MODF record placing `wmo` with doodad set `set` (word 58).</summary>
    static byte[] ModfWithSet(uint wmo, uint uid, ushort set)
    {
        byte[] r = AdtSynthetic.ModfRecord(wmo, uid, 0x8, 1024);
        r[58] = (byte)set;
        r[59] = (byte)(set >> 8);
        return r;
    }

    static void TerrainDoodadTests(Action<string> log)
    {
        var views = new List<TestWorldView>();
        double buildBudget = WmvTerrainSession.DoodadBuildBudgetMs, placeBudget = WmvTerrainSession.DoodadPlaceBudgetMs;
        try
        {
            DoodadSessionTests(log, views);
        }
        catch (Exception e)
        {
            Check(false, "doodads: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            WmvTerrainSession.DoodadBuildBudgetMs = buildBudget;
            WmvTerrainSession.DoodadPlaceBudgetMs = placeBudget;
            foreach (TestWorldView v in views)
                v.Destroy();
        }
        Check(DoodadTallies() == "0/0/0/0/0/0/0" && WorldTallies() == "0/0/0/0/0",
              "doodads: nothing of the doodad tests is left -- instances/prototypes/meshes/materials/textures/GPU bytes " + DoodadTallies() +
              ", world models " + WorldTallies(), log);
        try { DoodadReleaseOnFailureTest(log); }
        catch (Exception e) { Check(false, "doodads: the failed-build test threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
    }

    /// <summary>A prototype build that throws INSIDE the builder -- a second batch naming a submesh the skin does not have, after
    /// the first made its material and its texture (through the scene's cache) -- with WmvBuildOptions.ReleaseOnFailure: the
    /// exception comes out, what the build made is released (WmvBuildScratch), the model count is as it was, no prototype is
    /// counted, and the cache keeps its upload until the scene's textures go. Run last: the released objects are destroyed at
    /// the end of the frame, so they are destroyed here at once for the counts that follow.</summary>
    static void DoodadReleaseOnFailureTest(Action<string> log)
    {
        int models0 = WmvRuntimeModel.Live, releases0 = WmvBuildScratch.Releases;
        var holder = new GameObject("_WmvDoodadReleaseTest");
        holder.SetActive(false);
        var doodads = new WmvWorldDoodads(holder.transform);
        var cache = new WmvDoodadTextures();
        M2ParsedModel model = M2Parser.Parse(DoodadM2(9901, 9950, 2));
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.GeosetSkin(new[] { 0, 0 }));
        skin.Batches[1].SubmeshIndex = 999;
        BlpImage image = BlpDecoder.Decode(RawBlp(17, 8, 8));
        string threw = null;
        try
        {
            doodads.BuildPrototype(9900, model, skin, new Dictionary<int, BlpImage> { { 0, image } }, new Dictionary<int, uint> { { 0, 9950u } }, cache);
        }
        catch (Exception e) { threw = e.GetType().Name; }
        Check(threw != null && WmvBuildScratch.Releases == releases0 + 1 && WmvBuildScratch.LastReleasedObjects >= 2 && WmvRuntimeModel.Live == models0 &&
              WmvWorldDoodads.LivePrototypes == 0 && WmvWorldDoodads.LiveMeshes == 0 && WmvWorldDoodads.LiveMaterials == 0 &&
              cache.Uploads == 1 && WmvWorldDoodads.LiveTextures == 1,
              "doodads: a prototype build that throws inside the builder (" + (threw ?? "no exception") + ") releases what it made (" +
              WmvBuildScratch.LastReleasedObjects + " object(s): its mesh and the first batch's material), the model count is as it was, no prototype is " +
              "counted, and the cache keeps its upload (" + cache.Uploads + ")", log);
        cache.Dispose();
        Check(WmvWorldDoodads.LiveTextures == 0 && WmvWorldDoodads.LiveGpuTextureBytes == 0, "doodads: and the cache's upload goes with the cache", log);
        // The released objects (destroyed at the end of the frame) and the cache's texture, destroyed now for the counts that follow.
        foreach (UnityEngine.Object o in Resources.FindObjectsOfTypeAll<UnityEngine.Object>())
            if (o != null && (o.name.StartsWith("M2 9900 (doodad prototype)", StringComparison.Ordinal) || o.name.StartsWith("M2 texture 9950", StringComparison.Ordinal)))
                UnityEngine.Object.DestroyImmediate(o);
        UnityEngine.Object.DestroyImmediate(holder);
    }

    static void DoodadSessionTests(Action<string> log, List<TestWorldView> views)
    {
        int meshes0 = Meshes(), materials0 = Materials(), textures0 = Resources.FindObjectsOfTypeAll<Texture2D>().Length, models0 = WmvRuntimeModel.Live;
        Check(DoodadTallies() == "0/0/0/0/0/0/0", "doodads: no doodad is held before the tests (" + DoodadTallies() + ")", log);
        Func<bool> clean = () => DoodadTallies() == "0/0/0/0/0/0/0" && WorldTallies() == "0/0/0/0/0" && Meshes() == meshes0 &&
                                 Materials() == materials0 && Resources.FindObjectsOfTypeAll<Texture2D>().Length == textures0 &&
                                 WmvRuntimeTerrainTile.Live == 0 && WmvRuntimeModel.Live == models0;
        Func<string> state = () => "doodads " + DoodadTallies() + ", world " + WorldTallies() + ", meshes " + (Meshes() - meshes0) + ", materials " +
                                   (Materials() - materials0) + ", textures " + (Resources.FindObjectsOfTypeAll<Texture2D>().Length - textures0) +
                                   ", models " + (WmvRuntimeModel.Live - models0);
        Func<TerrainHarness> make = () =>
        {
            var hh = new TerrainHarness();
            var vv = new TestWorldView();
            views.Add(vv);
            vv.SentCount = () => hh.Sent.Count;
            hh.Session.WorldView = vv;
            return hh;
        };
        Func<TerrainHarness, string, int> asked = (hh, role) =>
        {
            int n = 0;
            foreach (string[] q in hh.Sent) if (q[1] == role) n++;
            return n;
        };

        // The files: a world model whose root has three doodad sets -- set 0 (one record), Set_A (two), Set_B (one) -- naming M2s
        // A and B through MODI; the MODF record names set 1 (Set_A). The obj0's MDDF: A twice under one uniqueId, A again, B.
        const uint wmo = 9700, wmoGroup = 9701, wmoTexture = 9750;
        const uint m2A = 9800, skinA = 9801, m2B = 9810, skinB = 9811, texS = 9850;
        byte[] root = WmoSynthetic.Concat(WmoSynthetic.SquaresRoot(1, wmoGroup, wmoTexture),
                                          WmoSynthetic.Chunk("MODS", WmoSynthetic.Concat(ModsRecord("Set_$DefaultGlobal", 0, 1), ModsRecord("Set_A", 1, 2),
                                                                                         ModsRecord("Set_B", 3, 1))),
                                          WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(m2B, m2A)),
                                          WmoSynthetic.Chunk("MODD", WmoSynthetic.Concat(ModdRecord(0, 2f, 2f, 0f), ModdRecord(1, 4f, 4f, 0f, 2f),
                                                                                         ModdRecord(0, 6f, 6f, 0f), ModdRecord(1, 8f, 8f, 0f))));
        int idx = WorldTestTileY * 64 + WorldTestTileX;
        byte[] tileWdt = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(idx),
                                          AdtSynthetic.Maid(idx, TerrainTestRoot, WorldTestObj0));
        Func<byte[][], byte[]> obj0Of = mddf => AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Chunk("MDDF", AdtSynthetic.Cat(mddf)),
                                                                 AdtSynthetic.Chunk("MODF", ModfWithSet(wmo, 31, 1)));
        byte[] obj0 = obj0Of(new[]
        {
            AdtSynthetic.MddfRecord(m2A, 21, 100f, 20f, 300f, 0f, 90f, 0f, 2048, 0x40),
            AdtSynthetic.MddfRecord(m2A, 21, 100f, 20f, 300f, 0f, 90f, 0f, 2048, 0x40),
            AdtSynthetic.MddfRecord(m2A, 22, 110f, 21f, 310f, 0f, 0f, 0f, 1024, 0x40),
            AdtSynthetic.MddfRecord(m2B, 23, 120f, 22f, 320f, 0f, 45f, 0f, 1024, 0x40),
        });
        Func<byte[], WorldFiles> filesWith = o =>
        {
            var f = new WorldFiles();
            f.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, tileWdt);
            f.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
            f.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, o);
            f.Add(WmvTerrainSession.RoleWmoRoot, wmo, root);
            f.Add(WmvTerrainSession.RoleWmoGroup, wmoGroup, WmoSynthetic.SquaresGroup(0));
            f.Add(WmvTerrainSession.RoleWmoTexture, wmoTexture, RawBlp(13, 8, 8));
            f.Add(WmvTerrainSession.RoleM2Model, m2A, DoodadM2((int)skinA, (int)texS));
            f.Add(WmvTerrainSession.RoleM2Skin, skinA, M2Synthetic.GeosetSkin(new[] { 0 }));
            f.Add(WmvTerrainSession.RoleM2Model, m2B, DoodadM2((int)skinB, (int)texS));
            f.Add(WmvTerrainSession.RoleM2Skin, skinB, M2Synthetic.GeosetSkin(new[] { 0 }));
            f.Add(WmvTerrainSession.RoleM2Texture, texS, RawBlp(17, 8, 8));
            return f;
        };
        WorldFiles files = filesWith(obj0);

        // ---- a tile's doodads: MDDF by uniqueId, set 0 plus the placement's set, one prototype per M2, one upload per texture
        TerrainHarness h = make();
        TestWorldView v = (TestWorldView)h.Session.WorldView;
        h.Session.Load(DoodadCommand(1, false));
        files.Serve(h, 1);
        WmvIpcClient.TerrainTileReport r = h.For(1);
        WmvWorldScene scene = h.Session.World;
        // MDDF: uniqueIds 21 (two records), 22, 23 -> 3; WMO: set 0 (record 0, B) + Set_A (records 1 A, 2 B) -> 3; Set_B not drawn.
        Check(r != null && r.Status == "built" && r.HasDoodads && r.DoodadState == "complete" && r.WorldAssembly == "complete" &&
              r.DoodadMddfRecords == 4 && r.DoodadMddfPlacements == 3 && r.DoodadWmoRecords == 3 && r.DoodadWmoPlacements == 1 &&
              r.DoodadInstances == 6 && r.DoodadPlaced == 6 && r.DoodadModels == 2 && r.DoodadModelsBuilt == 2 && r.DoodadTextures == 1 &&
              r.DoodadTexturesDecoded == 1 && r.DoodadTextureUploads == 1 && r.DoodadTextureUses == 2 && h.CountFor(1) == 1,
              "doodads: a tile's 4 MDDF records give 3 doodads by uniqueId, its world model's set 0 and Set_A 3 more (Set_B not drawn) -- all 6 placed " +
              "over 2 M2s, one texture uploaded once for both; doodads and assembly \"complete\" (" +
              (r != null ? r.Status + ", doodads " + r.DoodadState + " " + r.DoodadPlaced + "/" + r.DoodadInstances + ", MDDF " + r.DoodadMddfRecords +
                           "->" + r.DoodadMddfPlacements + ", WMO " + r.DoodadWmoRecords + ", models " + r.DoodadModelsBuilt + "/" + r.DoodadModels +
                           ", uploads " + r.DoodadTextureUploads + ", uses " + r.DoodadTextureUses + ", assembly " + r.WorldAssembly + "; " + r.DoodadReason
                         : "no report") + ")", log);
        Check(r != null && r.DoodadSets.Count == 1 && r.DoodadSets[0].Set == 1 && r.DoodadSets[0].SetCount == 3 && r.DoodadSets[0].Active == 3 &&
              r.DoodadSets[0].SetName == "Set_$DefaultGlobal+Set_A",
              "doodads: the report names the placement's set: 1 of 3, set 0 plus Set_A, 3 MODD records (" +
              (r != null && r.DoodadSets.Count > 0 ? r.DoodadSets[0].Set + " of " + r.DoodadSets[0].SetCount + ", " + r.DoodadSets[0].SetName + ", " +
                                                     r.DoodadSets[0].Active : "none") + ")", log);
        bool shared = scene != null && scene.Doodads.Count == 6 && scene.DoodadPrototypes.Count == 2;
        WmvDoodadInstance a21 = null;
        if (shared)
        {
            var meshes = new HashSet<Mesh>();
            foreach (WmvDoodadInstance i in scene.Doodads)
            {
                MeshFilter mf = i.Root != null ? i.Root.GetComponent<MeshFilter>() : null;
                if (mf != null) meshes.Add(mf.sharedMesh);
                if (i.Doodad.Source == DoodadSource.Mddf && i.Doodad.UniqueId == 21) a21 = i;
            }
            shared = meshes.Count == 2;
        }
        Check(shared && DoodadTallies().StartsWith("6/2/2/", StringComparison.Ordinal),
              "doodads: 6 instances of 2 prototypes, sharing their meshes (" + DoodadTallies() + ")", log);
        // MDDF uniqueId 21: file (100, 20, 300), a1 90, scale 2 -> W = (32T - 300, 32T - 100, 20), R = Rz(270).
        bool placedRight = false;
        if (a21 != null && scene != null)
        {
            double h32 = PlacementTransform.MapHalf, ux, uy, uz;
            WorldToUnity.PositionD(h32 - 300.0, h32 - 100.0, 20.0, scene.Origin, out ux, out uy, out uz);
            Quaternion want = WmvWorldModels.UnityRotation(PlacementTransform.Rot(2, 270.0));
            Transform t = a21.Root.transform;
            placedRight = (t.localPosition - new Vector3((float)ux, (float)uy, (float)uz)).magnitude < 1e-3f && Quaternion.Angle(t.localRotation, want) < 0.01f &&
                          Mathf.Abs(t.localScale.x - 2f) < 1e-6f && t.gameObject.layer == WmvTerrainBuilder.Layer;
        }
        Check(placedRight, "doodads: MDDF uniqueId 21 stands at its Tile-frame position relative to the scene's origin, turned Rz(270), scale 2, " +
                           "on the world layer", log);
        bool cameraFirst = v.SettledLoads.Count == 1 && v.SettledLoads[0] == 1 && v.SentAtSettle.Count == 1;
        if (cameraFirst)
            for (int k = 0; k < v.SentAtSettle[0] && k < h.Sent.Count; k++)
                if (h.Sent[k][1] == WmvTerrainSession.RoleM2Model || h.Sent[k][1] == WmvTerrainSession.RoleM2Skin || h.Sent[k][1] == WmvTerrainSession.RoleM2Texture)
                    cameraFirst = false;
        Check(cameraFirst && asked(h, WmvTerrainSession.RoleM2Model) == 2 && asked(h, WmvTerrainSession.RoleM2Skin) == 2 &&
              asked(h, WmvTerrainSession.RoleM2Texture) == 1,
              "doodads: the camera's moment (the world models settled) comes once, before any doodad request; each M2, skin and texture asked " +
              "for once (" + asked(h, WmvTerrainSession.RoleM2Model) + " models, " + asked(h, WmvTerrainSession.RoleM2Skin) + " skins, " +
              asked(h, WmvTerrainSession.RoleM2Texture) + " texture)", log);
        string line = r != null ? WmvIpcClient.TerrainTileLoadedLine(r) : "";
        Check(line.Contains("\"assembly\":\"complete\"") && line.Contains("\"doodads\":{\"state\":\"complete\"") && line.Contains("\"placed\":6") &&
              line.Length < 60000,
              "doodads: terrainTileLoaded carries the assembly and the doodads (" + line.Length + " characters)", log);
        bool progress = false;
        foreach (WmvIpcClient.WorldProgressReport p in v.Progress)
            if (p.HasDoodads && p.DoodadPhase == "complete" && p.DoodadPlaced == 6 && p.DoodadInstances == 6)
                progress = true;
        Check(progress, "doodads: worldProgress reports the doodads' phase and counts (the last: complete, 6 of 6)", log);
        h.Session.Clear("test");
        Check(clean() && h.Session.World == null, "doodads: clearTerrain takes the doodads away -- instances, prototypes, textures (" + state() + ")", log);

        // ---- a protocol-16 load: no doodad budgets -- nothing asked, the world as in Stage 06, the doodads "notOffered"
        h = make();
        h.Session.Load(WorldCommand(2, false));
        filesWith(obj0).Serve(h, 2);
        r = h.For(2);
        Check(r != null && r.Status == "built" && r.WorldState == "complete" && r.DoodadState == "notOffered" && r.WorldAssembly == "partial" &&
              asked(h, WmvTerrainSession.RoleM2Model) == 0 && DoodadTallies().StartsWith("0/0/", StringComparison.Ordinal),
              "doodads: a load without doodad budgets asks for no M2, its world complete, its doodads \"notOffered\", the assembly \"partial\" (" +
              (r != null ? r.Status + ", world " + r.WorldState + ", doodads " + r.DoodadState + ", assembly " + r.WorldAssembly : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);

        // ---- the budgets: one model, two doodads -- the rest "notRequested"; an unsupported record
        byte[] withUnsupported = obj0Of(new[]
        {
            AdtSynthetic.MddfRecord(m2A, 21, 100f, 20f, 300f, 0f, 90f, 0f, 2048, 0x40),
            AdtSynthetic.MddfRecord(m2B, 23, 120f, 22f, 320f, 0f, 45f, 0f, 1024, 0x40),
            AdtSynthetic.MddfRecord(m2A, 24, 130f, 23f, 330f, 0f, 0f, 0f, 1024, 0x0),
        });
        h = make();
        h.Session.Load(DoodadCommand(3, false, 1, 2));
        filesWith(withUnsupported).Serve(h, 3);
        r = h.For(3);
        // In plan order: MDDF 21 (A: planned), 23 (B: past the models budget), 24 (unsupported); WMO record 0 (B: models budget),
        // record 1 (A: planned, the 2nd), record 2 (B: past the instance budget) -- 2 placed, 3 not requested, 1 unsupported.
        Check(r != null && r.Status == "built" && r.DoodadInstances == 6 && r.DoodadUnsupported == 1 && r.DoodadNotRequested == 3 &&
              r.DoodadModels == 1 && r.DoodadModelsBuilt == 1 && r.DoodadPlaced == 2 && r.DoodadState == "partial" && r.WorldAssembly == "partial" &&
              r.DoodadPlaced + r.DoodadFailed + r.DoodadUnsupported + r.DoodadNotRequested + r.DoodadEnded == r.DoodadInstances,
              "doodads: maxM2ModelsPerLoad 1 and maxDoodadInstancesPerLoad 2 leave the rest \"notRequested\", a record without flag 0x40 " +
              "\"unsupported\", every doodad counted once (" + (r != null ? r.DoodadPlaced + " placed, " + r.DoodadNotRequested + " not requested, " +
                                                                        r.DoodadUnsupported + " unsupported of " + r.DoodadInstances + "; " + r.DoodadState
                                                                      : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);

        // ---- a model that is not an M2, and a build that throws: those models fail, the load is still reported once
        h = make();
        h.Session.Load(DoodadCommand(4, false));
        WorldFiles badFiles = filesWith(obj0);
        badFiles.Files[WmvTerrainSession.RoleM2Model][(int)m2A] = WmoSynthetic.SquaresGroup(0);
        badFiles.Serve(h, 4);
        r = h.For(4);
        Check(r != null && r.Status == "built" && r.DoodadModelsFailed == 1 && r.DoodadModelsBuilt == 1 && r.DoodadState == "partial" &&
              r.DoodadList.Count > 0 && r.DoodadList[0].FileDataID == (int)m2A && r.DoodadList[0].State == "failed" &&
              r.DoodadList[0].Reason.Contains("not an M2"),
              "doodads: a model file that is not an M2 fails that model only, named first in the list (" +
              (r != null && r.DoodadList.Count > 0 ? r.DoodadList[0].State + ": " + r.DoodadList[0].Reason : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);
        h = make();
        v = (TestWorldView)h.Session.WorldView;
        v.FailDoodadBuilds = true;
        h.Session.Load(DoodadCommand(5, false));
        filesWith(obj0).Serve(h, 5);
        r = h.For(5);
        Check(r != null && r.Status == "built" && r.DoodadModelsFailed == 2 && r.DoodadPlaced == 0 && r.DoodadState == "failed" &&
              r.WorldAssembly == "partial" && DoodadTallies().StartsWith("0/0/", StringComparison.Ordinal),
              "doodads: a prototype build that throws fails its model and leaves nothing built (" +
              (r != null ? r.DoodadState + ", " + r.DoodadModelsFailed + " failed, assembly " + r.WorldAssembly : "no report") + "; " + DoodadTallies() + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);

        // ---- the doodads' timeout: a skin never answered -- the load reported once, what was placed kept until it is cleared
        h = make();
        h.Session.Load(DoodadCommand(6, false));
        WorldFiles silent = filesWith(obj0);
        silent.Withheld.Add(WmvTerrainSession.RoleM2Skin);
        silent.Serve(h, 6, 40);
        bool waiting = h.For(6) == null && h.Last(WmvTerrainSession.RoleM2Skin) != null;
        h.Now += WmvTerrainSession.DoodadTimeoutMs + 1000.0;
        h.Pump(4);
        r = h.For(6);
        Check(waiting && r != null && r.Status == "built" && r.DoodadState == "failed" && r.DoodadModelsFailed == 2 &&
              r.DoodadList.Count > 0 && r.DoodadList[0].Reason.Contains("timed out") && r.RequestsRetired >= 1 && h.CountFor(6) == 1,
              "doodads: skins never answered time the doodads out -- one \"built\" report, the models failed \"timed out\", their requests retired (" +
              (r != null ? r.DoodadState + "; " + (r.DoodadList.Count > 0 ? r.DoodadList[0].Reason : "") + "; retired " + r.RequestsRetired : "no report") +
              ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);

        // ---- the load ended while an M2 read is out: "cleared" once, the late answer dropped, nothing left
        h = make();
        h.Session.Load(DoodadCommand(7, false));
        WorldFiles held = filesWith(obj0);
        held.Withheld.Add(WmvTerrainSession.RoleM2Model);
        held.Serve(h, 7, 40);
        string m2Ask = h.Last(WmvTerrainSession.RoleM2Model);
        h.Session.Clear("test: during an M2 read");
        bool late = m2Ask != null && h.Answer(m2Ask, DoodadM2((int)skinA, (int)texS));
        h.Pump(3);
        r = h.For(7);
        Check(m2Ask != null && r != null && r.Status == "cleared" && h.CountFor(7) == 1 && !late && r.DoodadState == "ended" && clean(),
              "doodads: cleared while an M2 read is out -- reported once \"cleared\", the late answer dropped, nothing left (" +
              (r != null ? r.Status + ", doodads " + r.DoodadState : "no report") + "; " + state() + ")", log);

        // ---- the load ended between a texture's decode and its result: the result dropped, nothing left
        h = make();
        h.Session.Load(DoodadCommand(8, false));
        WorldFiles decode = filesWith(obj0);
        decode.Withheld.Add(WmvTerrainSession.RoleM2Texture);
        decode.Serve(h, 8, 60);
        string texAsk = h.Last(WmvTerrainSession.RoleM2Texture);
        bool applied = texAsk != null && h.Answer(texAsk, RawBlp(17, 8, 8));   // decoded at once (the test's worker); its result waits for Pump
        h.Session.LeaveForModelLoad("test: during a decode");
        h.Pump(3);
        r = h.For(8);
        Check(texAsk != null && applied && r != null && r.Status == "superseded" && h.CountFor(8) == 1 && clean(),
              "doodads: a model load between a texture's decode and its result -- \"superseded\" once, the decoded result dropped, nothing left (" +
              (r != null ? r.Status : "no report") + "; " + state() + ")", log);

        // ---- the load ended between prototype builds (one a frame): nothing of it left
        WmvTerrainSession.DoodadBuildBudgetMs = 0.0;
        h = make();
        h.Session.Load(DoodadCommand(9, false));
        WorldFiles building = filesWith(obj0);
        bool between = false;
        for (int k = 0; k < 400 && h.For(9) == null && !between; k++)
        {
            building.Serve(h, 9, 1);
            between = h.Session.World != null && h.Session.World.DoodadPrototypes.Count == 1;
        }
        h.Session.Clear("test: between prototype builds");
        r = h.For(9);
        WmvTerrainSession.DoodadBuildBudgetMs = buildBudgetDefault;
        Check(between && r != null && r.Status == "cleared" && h.CountFor(9) == 1 && clean(),
              "doodads: cleared with one prototype built and the next to come -- reported once, nothing left (" +
              (r != null ? r.Status : "no report") + "; " + state() + ")", log);

        // ---- the load ended between placements (one a frame): nothing of it left
        WmvTerrainSession.DoodadPlaceBudgetMs = 0.0;
        h = make();
        h.Session.Load(DoodadCommand(10, false));
        WorldFiles placing = filesWith(obj0);
        bool midway = false;
        for (int k = 0; k < 400 && h.For(10) == null && !midway; k++)
        {
            placing.Serve(h, 10, 1);
            midway = h.Session.World != null && h.Session.World.Doodads.Count >= 2 && h.Session.World.Doodads.Count < 6;
        }
        h.Session.Clear("test: between placements");
        r = h.For(10);
        WmvTerrainSession.DoodadPlaceBudgetMs = placeBudgetDefault;
        Check(midway && r != null && r.Status == "cleared" && h.CountFor(10) == 1 && r.DoodadEnded > 0 && clean(),
              "doodads: cleared with some doodads placed and the rest to come -- reported once, the rest \"ended\", nothing left (" +
              (r != null ? r.Status + ", " + r.DoodadPlaced + " placed, " + r.DoodadEnded + " ended" : "no report") + "; " + state() + ")", log);

        // ---- a newer load (another client epoch) while doodads are coming: the first "superseded", the second complete alone
        h = make();
        h.Session.Load(DoodadCommand(11, false));
        WorldFiles first = filesWith(obj0);
        first.Withheld.Add(WmvTerrainSession.RoleM2Texture);
        first.Serve(h, 11, 60);
        string pending = h.Last(WmvTerrainSession.RoleM2Texture);
        WmvIpcClient.TerrainTileCommand next = DoodadCommand(12, false);
        next.AssetEpoch += 1;
        h.Session.Load(next);
        WmvIpcClient.TerrainTileReport superseded = h.For(11);
        bool lateApplied = pending != null && h.Answer(pending, RawBlp(17, 8, 8));
        filesWith(obj0).Serve(h, 12);
        r = h.For(12);
        Check(pending != null && superseded != null && superseded.Status == "superseded" && h.CountFor(11) == 1 && !lateApplied &&
              r != null && r.Status == "built" && r.DoodadState == "complete" && DoodadTallies().StartsWith("6/2/", StringComparison.Ordinal),
              "doodads: a newer load (another epoch) supersedes one with a texture out -- reported once, the late answer dropped, only the newer " +
              "load's doodads on screen (" + (superseded != null ? superseded.Status : "no report") + ", " + DoodadTallies() + ")", log);
        h.Session.LeaveForModelLoad("test: a model load");
        Check(clean(), "doodads: a model load takes the doodads away (" + state() + ")", log);

        // ---- a WMO-only map: its global world model's set 0 doodad, placed in the Global frame
        h = make();
        var global = new WorldFiles();
        global.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, GlobalWdt(wmo));
        global.Add(WmvTerrainSession.RoleWmoRoot, wmo, root);
        global.Add(WmvTerrainSession.RoleWmoGroup, wmoGroup, WmoSynthetic.SquaresGroup(0));
        global.Add(WmvTerrainSession.RoleWmoTexture, wmoTexture, RawBlp(13, 8, 8));
        global.Add(WmvTerrainSession.RoleM2Model, m2B, DoodadM2((int)skinB, (int)texS));
        global.Add(WmvTerrainSession.RoleM2Skin, skinB, M2Synthetic.GeosetSkin(new[] { 0 }));
        global.Add(WmvTerrainSession.RoleM2Texture, texS, RawBlp(17, 8, 8));
        h.Session.Load(DoodadCommand(13, true));
        global.Serve(h, 13);
        r = h.For(13);
        scene = h.Session.World;
        // The global record names set 3 (GlobalWdt's record), which the root does not have: set 0 only, said -- record 0, M2 B.
        bool atRecord = scene != null && scene.Doodads.Count == 1 && scene.Doodads[0].Root != null;
        if (atRecord)
        {
            double ux, uy, uz;
            WorldPoint w0 = scene.Doodads[0].Doodad.Position;
            WorldToUnity.PositionD(w0.X, w0.Y, w0.Z, scene.Origin, out ux, out uy, out uz);
            atRecord = (scene.Doodads[0].Root.transform.localPosition - new Vector3((float)ux, (float)uy, (float)uz)).magnitude < 1e-3f;
        }
        Check(r != null && r.Status == "built" && r.WorldKind == "globalWmo" && r.DoodadWmoRecords == 1 && r.DoodadPlaced == 1 &&
              r.DoodadState == "complete" && r.WorldAssembly == "complete" && atRecord && r.DoodadSets.Count == 1 &&
              r.DoodadSets[0].Reason.Contains("set 3"),
              "doodads: a WMO-only map's world model places its set-0 doodad, reported with it (" +
              (r != null ? r.Status + ", " + r.WorldKind + ", doodads " + r.DoodadState + " " + r.DoodadPlaced + "/" + r.DoodadInstances : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left after the clear (" + state() + ")", log);

        // ---- a world model built without one of its two group files: its MODD record 0 (group 0 lists it, MODR) is placed;
        // record 1, which no group that was read lists, is "unsupported" -- not placed where no geometry was built. The world
        // stays "complete" (Stage 06 rates placements; the missing file is counted apart); the doodads and the assembly are
        // "partial".
        const uint wmoTwo = 9720, wmoTwoGroup = 9721;               // group 1's file (9722) is not served
        byte[] twoGroups = WmoSynthetic.Concat(WmoSynthetic.SquaresRoot(2, wmoTwoGroup, wmoTexture),
                                               WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 2)),
                                               WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(m2B)),
                                               WmoSynthetic.Chunk("MODD", WmoSynthetic.Concat(ModdRecord(0, 2f, 2f, 0f), ModdRecord(0, 14f, 2f, 0f))));
        h = make();
        var partialFiles = new WorldFiles();
        partialFiles.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, tileWdt);
        partialFiles.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
        partialFiles.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, AdtSynthetic.Cat(AdtSynthetic.Mver(),
                                                                                        AdtSynthetic.Chunk("MODF", ModfWithSet(wmoTwo, 32, 0))));
        partialFiles.Add(WmvTerrainSession.RoleWmoRoot, wmoTwo, twoGroups);
        partialFiles.Add(WmvTerrainSession.RoleWmoGroup, wmoTwoGroup, WmoSynthetic.SquaresGroup(0, new ushort[] { 0 }));
        partialFiles.Add(WmvTerrainSession.RoleWmoTexture, wmoTexture, RawBlp(13, 8, 8));
        partialFiles.Add(WmvTerrainSession.RoleM2Model, m2B, DoodadM2((int)skinB, (int)texS));
        partialFiles.Add(WmvTerrainSession.RoleM2Skin, skinB, M2Synthetic.GeosetSkin(new[] { 0 }));
        partialFiles.Add(WmvTerrainSession.RoleM2Texture, texS, RawBlp(17, 8, 8));
        h.Session.Load(DoodadCommand(14, false));
        partialFiles.Serve(h, 14);
        r = h.For(14);
        scene = h.Session.World;
        bool recordZero = scene != null && scene.Doodads.Count == 1 && scene.Doodads[0].Doodad.ModdIndex == 0;
        Check(r != null && r.Status == "built" && r.WorldState == "complete" && r.WorldGroupFilesMissing == 1 && r.DoodadWmoRecords == 2 && r.DoodadPlaced == 1 &&
              r.DoodadUnsupported == 1 && r.DoodadState == "partial" && r.WorldAssembly == "partial" && recordZero,
              "doodads: a world model built without one of its group files places the doodad a read group lists and leaves the one no read " +
              "group lists \"unsupported\" (" + (r != null ? r.Status + ", world " + r.WorldState + " (" + r.WorldGroupFilesMissing + " group file missing), doodads " + r.DoodadState + " " + r.DoodadPlaced +
                                                                     " placed, " + r.DoodadUnsupported + " unsupported of " + r.DoodadWmoRecords : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "doodads: and nothing is left (" + state() + ")", log);
    }

    const double buildBudgetDefault = 6.0, placeBudgetDefault = 4.0;
}
