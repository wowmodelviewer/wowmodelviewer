// WmvLifecycleSelfTestWorldModels.cs
//
// The lifecycle self-test's world loads (protocol 16, docs/world-viewer/06-world-models-in-world.md): the terrain session
// against a fake host, with synthetic files only -- a WMO-only map's WDT (one global MODF record), a tile's WDT whose MAID
// names its obj0 ADT, an obj0 with MODF records, and the synthetic WMO (WmoSynthetic.SquaresRoot / SquaresGroup) with a
// raw BLP2 texture. The world models are built by the real WmvWorldModels (the Buildings builder) under an inactive root of
// the test's, so nothing is drawn; disposed at once (DestroyImmediate) so the terrain block's counts, taken in this frame,
// hold. Checked: a WMO-only map's world model built, shown once and reported "built" with its world; a newer load
// superseding one whose world-model root is out, and the late answer dropped; a tile's MODF records placed one per
// uniqueId with one shared prototype, an unsupported record and the models budget; the world models' timeout; a missing
// group file; a root that is not a root; a model load and a clear taking the world away -- and every tally back to 0.
// Stage 10 step 6: the views' disposal destroys a prototype's world-lit copies at once too (WmvLifecycleSelfTestWorldLight.cs
// binds these managers to the outdoor light).

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    /// <summary>The world models' Unity side in the self-test: the real WmvWorldModels under an inactive root; disposal at
    /// once (DestroyImmediate), the tallies kept by WmvWorldModels itself.</summary>
    sealed class TestWorldView : IWmvWorldModelView
    {
        public readonly GameObject Root;
        public readonly WmvWorldModels Models;
        public int Shown, Built;
        public readonly List<WmvIpcClient.WorldShownReport> ShownReports = new List<WmvIpcClient.WorldShownReport>();
        public readonly List<WmvIpcClient.WorldProgressReport> Progress = new List<WmvIpcClient.WorldProgressReport>();

        public TestWorldView()
        {
            Root = new GameObject("_WmvWorldSelfTest");
            Root.SetActive(false);
            Models = new WmvWorldModels(Root.transform);
        }

        public WmvWorldPrototype BuildPrototype(uint fileDataID, WmoRoot root, WmoGroup[] groups, Dictionary<uint, WmvWmoTexture> textures)
        {
            return Models.BuildPrototype(fileDataID, root, groups, textures);
        }

        public WmvWorldInstance PlaceInstance(WmvWorldPrototype prototype, WorldModelPlacement placement, WorldOrigin origin, int load)
        {
            return Models.Place(prototype, placement, origin, load);
        }

        public void DisposeInstance(WmvWorldInstance instance)
        {
            GameObject go = instance != null ? instance.Root : null;
            Models.DisposeInstance(instance);
            if (go != null) UnityEngine.Object.DestroyImmediate(go);
        }

        public void DisposePrototype(WmvWorldPrototype prototype)
        {
            var owned = new List<UnityEngine.Object>();
            WmvRuntimeMapObject rt = prototype != null ? prototype.Runtime : null;
            if (rt != null)
            {
                owned.Add(rt.Root);
                owned.AddRange(rt.Meshes);
                owned.AddRange(rt.Materials);
                owned.Add(rt.FallbackMaterial);
                owned.AddRange(rt.Textures);
            }
            if (prototype != null)
                owned.AddRange(prototype.WorldLitCopies);
            Models.DisposePrototype(prototype);
            foreach (UnityEngine.Object o in owned)
                if (o != null) UnityEngine.Object.DestroyImmediate(o);
        }

        public void ShowWorld(WmvWorldScene scene) { Shown++; }
        public void WorldBuilt(WmvWorldScene scene) { Built++; }
        public void ReportWorldShown(WmvIpcClient.WorldShownReport report) { ShownReports.Add(report); }
        public void ReportWorldProgress(WmvIpcClient.WorldProgressReport report) { Progress.Add(report); }

        // Protocol 17: the doodads, by the real WmvWorldDoodads under the same inactive root; disposal at once (DestroyImmediate).
        public WmvWorldDoodads Doodads;
        public readonly List<int> SettledLoads = new List<int>();
        /// <summary>Requests the harness had sent when the world models settled (the camera's moment), per settle.</summary>
        public readonly List<int> SentAtSettle = new List<int>();
        public Func<int> SentCount;
        /// <summary>A doodad prototype build throws (the build failure path).</summary>
        public bool FailDoodadBuilds;

        public void WorldModelsSettled(WmvWorldScene scene, int load)
        {
            SettledLoads.Add(load);
            SentAtSettle.Add(SentCount != null ? SentCount() : -1);
        }

        public WmvDoodadPrototype BuildDoodadPrototype(WmvWorldScene scene, uint fileDataID, M2ParsedModel model, M2ParsedSkin skin,
                                                       Dictionary<int, BlpImage> texturesBySlot, Dictionary<int, uint> slotFiles)
        {
            if (Doodads == null)
                Doodads = new WmvWorldDoodads(Root.transform);
            if (FailDoodadBuilds)
                throw new InvalidOperationException("test: doodad builds fail");
            if (scene.DoodadTextures == null)
                scene.DoodadTextures = new WmvDoodadTextures();
            return Doodads.BuildPrototype(fileDataID, model, skin, texturesBySlot, slotFiles, scene.DoodadTextures);
        }

        public WmvDoodadInstance PlaceDoodad(WmvDoodadPrototype prototype, WorldDoodad doodad, WorldOrigin origin, int load)
        {
            return Doodads.Place(prototype, doodad, origin, load);
        }

        public void DisposeDoodads(WmvWorldScene scene)
        {
            var owned = new List<UnityEngine.Object>();
            foreach (WmvDoodadInstance i in scene.Doodads)
                if (i != null && i.Root != null) owned.Add(i.Root);
            foreach (WmvDoodadPrototype p in scene.DoodadPrototypes.Values)
                if (p != null && p.Runtime != null)
                {
                    owned.Add(p.Runtime.Root);
                    owned.Add(p.Runtime.Mesh);
                    owned.AddRange(p.Runtime.Materials);
                    owned.AddRange(p.WorldLitCopies);
                }
            if (scene.DoodadTextures != null)
                foreach (Texture2D t in scene.DoodadTextures.All)
                    owned.Add(t);
            if (Doodads != null)
            {
                Doodads.DisposeInstances(scene.Doodads);
                foreach (WmvDoodadPrototype p in scene.DoodadPrototypes.Values)
                    Doodads.DisposePrototype(p, scene.DoodadTextures);
            }
            scene.Doodads.Clear();
            scene.DoodadPrototypes.Clear();
            if (scene.DoodadTextures != null)
                scene.DoodadTextures.Dispose();
            scene.DoodadTextures = null;
            foreach (UnityEngine.Object o in owned)
                if (o != null) UnityEngine.Object.DestroyImmediate(o);
        }

        // Streaming (stage 08): one doodad prototype evicted, the instances of a closing tile.
        public void DisposeDoodadPrototype(WmvWorldScene scene, WmvDoodadPrototype p)
        {
            var owned = new List<UnityEngine.Object>();
            if (p != null && p.Runtime != null)
            {
                owned.Add(p.Runtime.Root);
                owned.Add(p.Runtime.Mesh);
                owned.AddRange(p.Runtime.Materials);
                owned.AddRange(p.WorldLitCopies);
            }
            if (Doodads != null)
                Doodads.DisposePrototype(p, scene != null ? scene.DoodadTextures : null);
            foreach (UnityEngine.Object o in owned)
                if (o != null) UnityEngine.Object.DestroyImmediate(o);
        }

        public void DisposeDoodadInstances(List<WmvDoodadInstance> instances)
        {
            var owned = new List<UnityEngine.Object>();
            foreach (WmvDoodadInstance i in instances)
                if (i != null && i.Root != null) owned.Add(i.Root);
            if (Doodads != null)
                Doodads.DisposeInstances(instances);
            foreach (UnityEngine.Object o in owned)
                if (o != null) UnityEngine.Object.DestroyImmediate(o);
        }

        public void Destroy() { UnityEngine.Object.DestroyImmediate(Root); }
    }

    const int WorldTestTileX = 32, WorldTestTileY = 50;
    const uint WorldTestObj0 = 777623;

    static string WorldTallies()
    {
        return WmvWorldModels.LiveInstances + "/" + WmvWorldModels.LivePrototypes + "/" + WmvWorldModels.LiveMeshes + "/" +
               WmvWorldModels.LiveMaterials + "/" + WmvWorldModels.LiveTextures;
    }

    static WmvIpcClient.TerrainTileCommand WorldCommand(int load, bool global, int maxModels = 64)
    {
        WmvIpcClient.TerrainTileCommand c = TerrainCommand(load, global ? -1 : WorldTestTileX, global ? -1 : WorldTestTileY);
        c.World = true;
        c.GlobalWmo = global;
        c.MaxWmoFileBytes = 4 * 1024 * 1024;
        c.MaxWmoBytesPerLoad = 128 * 1024 * 1024;
        c.MaxWmoTextureBytes = 8 * 1024 * 1024;
        c.MaxWmoTextureBytesPerLoad = 128 * 1024 * 1024;
        c.MaxWmoTexturesPerLoad = 1024;
        c.MaxWmoModelsPerLoad = maxModels;
        c.MaxWmoGroupsPerLoad = 1024;
        c.MaxWmoPlacementsPerLoad = 256;
        return c;
    }

    /// <summary>A WMO-only map's WDT, modern form: no tile, one global MODF record naming FileDataID `wmo`.</summary>
    static byte[] GlobalWdt(uint wmo)
    {
        return AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(1), AdtSynthetic.MainTable(), AdtSynthetic.Chunk("MAID", new byte[4096 * 32]),
                                AdtSynthetic.Chunk("MODF", AdtSynthetic.ModfRecord(wmo, 0xFFFFFFFF, 0x8, 1024)));
    }

    /// <summary>The world-model files a fake host serves, by role and FileDataID; what it does not hold is answered "not in
    /// client storage" (or `missing`'s state for an id listed there).</summary>
    sealed class WorldFiles
    {
        public readonly Dictionary<string, Dictionary<int, byte[]>> Files = new Dictionary<string, Dictionary<int, byte[]>>();
        public readonly HashSet<string> Answered = new HashSet<string>();
        public readonly HashSet<string> Withheld = new HashSet<string>();   // roles left unanswered

        public void Add(string role, uint id, byte[] data)
        {
            Dictionary<int, byte[]> byId;
            if (!Files.TryGetValue(role, out byId))
                Files[role] = byId = new Dictionary<int, byte[]>();
            byId[(int)id] = data;
        }

        /// <summary>Every request sent and not answered yet is answered; frames pumped; until load's report has come.</summary>
        public void Serve(TerrainHarness h, int load, int rounds = 400)
        {
            for (int k = 0; k < rounds && h.For(load) == null; k++)
            {
                foreach (string[] s in h.Sent.ToArray())
                {
                    if (Answered.Contains(s[0]) || Withheld.Contains(s[1]) || s[3] != load.ToString()) continue;
                    Answered.Add(s[0]);
                    Dictionary<int, byte[]> byId;
                    byte[] data;
                    if (Files.TryGetValue(s[1], out byId) && byId.TryGetValue(int.Parse(s[2]), out data) && data != null)
                        h.Answer(s[0], data);
                    else
                        h.Answer(s[0], null, "not in client storage", "test: FileDataID " + s[2] + " is not served");
                }
                h.Pump(1);
            }
        }
    }

    static void TerrainWorldModelTests(Action<string> log)
    {
        var views = new List<TestWorldView>();
        try
        {
            WorldModelSessionTests(log, views);
        }
        catch (Exception e)
        {
            Check(false, "world models: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            foreach (TestWorldView v in views)
                v.Destroy();
        }
        Check(WorldTallies() == "0/0/0/0/0",
              "world models: nothing of the world tests is left -- instances/prototypes/meshes/materials/textures " + WorldTallies(), log);
    }

    static void WorldModelSessionTests(Action<string> log, List<TestWorldView> views)
    {
        int meshes0 = Meshes(), materials0 = Materials(), mapObjects0 = WmvRuntimeMapObject.Live;
        Check(WorldTallies() == "0/0/0/0/0", "world models: no world model is held before the tests (" + WorldTallies() + ")", log);
        Func<bool> clean = () => WorldTallies() == "0/0/0/0/0" && Meshes() == meshes0 && Materials() == materials0 &&
                                 WmvRuntimeTerrainTile.Live == 0 && WmvRuntimeMapObject.Live == mapObjects0;
        Func<TerrainHarness> make = () =>
        {
            var hh = new TerrainHarness();
            var vv = new TestWorldView();
            views.Add(vv);
            hh.Session.WorldView = vv;
            return hh;
        };
        const uint gWmo = 9100, gGroup = 9101, gTexture = 9150;
        var global = new WorldFiles();
        global.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, GlobalWdt(gWmo));
        global.Add(WmvTerrainSession.RoleWmoRoot, gWmo, WmoSynthetic.SquaresRoot(2, gGroup, gTexture));
        global.Add(WmvTerrainSession.RoleWmoGroup, gGroup, WmoSynthetic.SquaresGroup(0));
        global.Add(WmvTerrainSession.RoleWmoGroup, gGroup + 1, WmoSynthetic.SquaresGroup(1));
        global.Add(WmvTerrainSession.RoleWmoTexture, gTexture, RawBlp(7, 8, 8));

        // ---- a WMO-only map: its world model, no tile -- shown once, reported "built" with its world, at the scene's origin
        TerrainHarness h = make();
        TestWorldView v = (TestWorldView)h.Session.WorldView;
        h.Session.Load(WorldCommand(1, true));
        global.Serve(h, 1);
        WmvIpcClient.TerrainTileReport r = h.For(1);
        WmvWorldScene scene = h.Session.World;
        Check(r != null && r.Status == "built" && r.World && r.WorldKind == "globalWmo" && r.WorldState == "complete" && r.WorldPlaced == 1 &&
              r.WorldPlacements == 1 && r.WorldModels == 1 && r.WorldModelsBuilt == 1 && h.CountFor(1) == 1,
              "world models: a WMO-only map's world load is reported once, \"built\", its world complete (" +
              (r != null ? r.Status + ", " + r.WorldKind + " " + r.WorldState + ", placed " + r.WorldPlaced + " of " + r.WorldPlacements + "; " + r.Reason
                         : "no report") + ")", log);
        Check(v.ShownReports.Count == 1 && v.ShownReports[0].Kind == "globalWmo" && v.ShownReports[0].Load == 1 && v.Shown == 1 &&
              v.Built == 1 && h.View.Shown == 0 && WmvRuntimeTerrainTile.Live == 0,
              "world models: worldShown once (kind globalWmo), the scene shown and built once, no tile (" + v.ShownReports.Count + " worldShown, " +
              h.View.Shown + " tile shown)", log);
        Check(scene != null && scene.Kind == "globalWmo" && scene.Instances.Count == 1 && scene.Prototypes.Count == 1 &&
              scene.Instances[0].Root != null && scene.Instances[0].Root.transform.localPosition == Vector3.zero &&
              WorldTallies().StartsWith("1/1/", StringComparison.Ordinal),
              "world models: the world model stands at the scene's origin -- its own placement's position -- one instance of one prototype (" +
              WorldTallies() + ")", log);
        h.Session.Clear("test");
        Check(clean() && h.Session.World == null,
              "world models: clearTerrain takes the world model away -- instance, prototype, meshes, materials and textures (" + WorldTallies() + ")", log);

        // ---- a newer load while a world-model root is out: the first answered "superseded" once, its late answer dropped
        h = make();
        v = (TestWorldView)h.Session.WorldView;
        h.Session.Load(WorldCommand(2, true));
        h.Answer(h.Last(WmvTerrainSession.RoleWdt), GlobalWdt(gWmo));
        h.Pump(2);
        string rootAsk = h.Last(WmvTerrainSession.RoleWmoRoot);
        h.Session.Load(WorldCommand(3, true));
        WmvIpcClient.TerrainTileReport superseded = h.For(2);
        bool lateApplied = rootAsk != null && h.Answer(rootAsk, WmoSynthetic.SquaresRoot(2, gGroup, gTexture));
        var second = new WorldFiles();
        foreach (var kv in global.Files) second.Files[kv.Key] = kv.Value;
        second.Serve(h, 3);
        WmvIpcClient.TerrainTileReport third = h.For(3);
        Check(rootAsk != null && superseded != null && superseded.Status == "superseded" && h.CountFor(2) == 1 && !lateApplied &&
              third != null && third.Status == "built" && WorldTallies().StartsWith("1/1/", StringComparison.Ordinal) && v.ShownReports.Count == 1 &&
              v.ShownReports[0].Load == 3,
              "world models: a newer load supersedes one whose world-model root is out -- reported once, the late answer dropped, only the newer " +
              "one shown (" + (superseded != null ? superseded.Status : "no report") + ", late answer " + (lateApplied ? "APPLIED" : "dropped") +
              ", " + WorldTallies() + ")", log);
        h.Session.LeaveForModelLoad("test: a model load");
        Check(clean() && h.Session.World == null,
              "world models: a model load takes the world model away (" + WorldTallies() + ")", log);

        // ---- a tile: MODF records placed one per uniqueId, one shared prototype; an unsupported record; the models budget
        const uint tWmo = 9200, tGroup = 9201, tTexture = 9250, tOther = 9300, tNameTable = 9400;
        int idx = WorldTestTileY * 64 + WorldTestTileX;
        byte[] tileWdt = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(idx),
                                          AdtSynthetic.Maid(idx, TerrainTestRoot, WorldTestObj0));
        byte[] obj0 = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Chunk("MDDF", new byte[0]),
                                       AdtSynthetic.Chunk("MODF", AdtSynthetic.Cat(AdtSynthetic.ModfRecord(tWmo, 1, 0x8, 1024),
                                                                                   AdtSynthetic.ModfRecord(tWmo, 2, 0x8, 2048),
                                                                                   AdtSynthetic.ModfRecord(tWmo, 1, 0x8, 1024),
                                                                                   AdtSynthetic.ModfRecord(tOther, 3, 0x8, 1024),
                                                                                   AdtSynthetic.ModfRecord(tNameTable, 4, 0x0, 1024))));
        var tile = new WorldFiles();
        tile.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, tileWdt);
        tile.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
        tile.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, obj0);
        tile.Add(WmvTerrainSession.RoleWmoRoot, tWmo, WmoSynthetic.SquaresRoot(1, tGroup, tTexture));
        tile.Add(WmvTerrainSession.RoleWmoGroup, tGroup, WmoSynthetic.SquaresGroup(0));
        tile.Add(WmvTerrainSession.RoleWmoTexture, tTexture, RawBlp(9, 8, 8));
        h = make();
        v = (TestWorldView)h.Session.WorldView;
        h.Session.Load(WorldCommand(4, false, 1));
        tile.Serve(h, 4);
        r = h.For(4);
        scene = h.Session.World;
        bool shared = scene != null && scene.Instances.Count == 2 && scene.Instances[0].Root != null && scene.Instances[1].Root != null;
        if (shared)
        {
            MeshFilter a = scene.Instances[0].Root.GetComponentInChildren<MeshFilter>(true);
            MeshFilter b = scene.Instances[1].Root.GetComponentInChildren<MeshFilter>(true);
            shared = a != null && b != null && a.sharedMesh != null && a.sharedMesh == b.sharedMesh &&
                     scene.Instances[0].Prototype == scene.Instances[1].Prototype;
        }
        Check(r != null && r.Status == "built" && r.World && r.WorldKind == "tile" && r.WorldRecords == 5 && r.WorldPlacements == 4 &&
              r.WorldPlaced == 2 && r.WorldUnsupported == 1 && r.WorldNotRequested == 1 && r.WorldModels == 1 && r.WorldState == "partial" &&
              h.View.Shown == 1 && v.ShownReports.Count == 1 && v.ShownReports[0].Kind == "tile",
              "world models: a tile's 5 MODF records give 4 placements by uniqueId -- 2 placed, 1 unsupported (a name-table record), 1 not " +
              "requested (maxWmoModelsPerLoad 1); world \"partial\" (" + (r != null ? r.Status + " " + r.WorldState + ": records " + r.WorldRecords +
              ", placements " + r.WorldPlacements + ", placed " + r.WorldPlaced + ", unsupported " + r.WorldUnsupported + ", not requested " +
              r.WorldNotRequested + "; " + r.Reason : "no report") + ")", log);
        Check(shared && WorldTallies().StartsWith("2/1/", StringComparison.Ordinal),
              "world models: the two placements of one world model are two instances of one prototype, sharing its meshes (" + WorldTallies() + ")", log);
        h.Session.Clear("test");
        Check(clean(), "world models: clearTerrain takes the tile and its world models away (" + WorldTallies() + ")", log);

        // ---- a texture shared by two world models, the second fetched only after the first is built: both keep it (the host
        // serves an id once per load, so the first model's build must not release pixels the second still needs)
        const uint sA = 9500, sB = 9600, sTexture = 9550;
        byte[] sharedObj0 = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Chunk("MDDF", new byte[0]),
                                             AdtSynthetic.Chunk("MODF", AdtSynthetic.Cat(AdtSynthetic.ModfRecord(sA, 11, 0x8, 1024),
                                                                                         AdtSynthetic.ModfRecord(sB, 12, 0x8, 1024))));
        var sharedFiles = new WorldFiles();
        sharedFiles.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, tileWdt);
        sharedFiles.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
        sharedFiles.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, sharedObj0);
        sharedFiles.Add(WmvTerrainSession.RoleWmoRoot, sA, WmoSynthetic.SquaresRoot(1, sA + 1, sTexture));
        sharedFiles.Add(WmvTerrainSession.RoleWmoRoot, sB, WmoSynthetic.SquaresRoot(1, sB + 1, sTexture));
        sharedFiles.Add(WmvTerrainSession.RoleWmoGroup, sA + 1, WmoSynthetic.SquaresGroup(0));
        sharedFiles.Add(WmvTerrainSession.RoleWmoGroup, sB + 1, WmoSynthetic.SquaresGroup(0));
        sharedFiles.Add(WmvTerrainSession.RoleWmoTexture, sTexture, RawBlp(11, 8, 8));
        h = make();
        h.Session.Load(WorldCommand(8, false));
        // Model B's group is held back until model A is built.
        sharedFiles.Withheld.Add(WmvTerrainSession.RoleWmoGroup);
        for (int k = 0; k < 200 && (h.Session.World == null || !h.Session.World.Prototypes.ContainsKey(sA)); k++)
        {
            foreach (string[] q in h.Sent.ToArray())
                if (q[1] == WmvTerrainSession.RoleWmoGroup && q[2] == (sA + 1).ToString() && !sharedFiles.Answered.Contains(q[0]))
                {
                    sharedFiles.Answered.Add(q[0]);
                    h.Answer(q[0], WmoSynthetic.SquaresGroup(0));
                }
            sharedFiles.Serve(h, 8, 1);
        }
        bool aFirst = h.Session.World != null && h.Session.World.Prototypes.ContainsKey(sA) && !h.Session.World.Prototypes.ContainsKey(sB);
        sharedFiles.Withheld.Clear();
        sharedFiles.Serve(h, 8);
        r = h.For(8);
        scene = h.Session.World;
        WmvWorldPrototype pa, pb;
        bool both = scene != null && scene.Prototypes.TryGetValue(sA, out pa) && scene.Prototypes.TryGetValue(sB, out pb) &&
                    pa.Runtime.PrimaryTexturesMissing == 0 && pb.Runtime.PrimaryTexturesMissing == 0 && pb.Runtime.Textures.Length > 0 &&
                    pb.Runtime.Textures[0] != null;
        Check(aFirst && r != null && r.Status == "built" && r.WorldState == "complete" && both,
              "world models: a texture shared by two world models, the second fetched after the first was built -- both drawn with it (" +
              (aFirst ? "A built first" : "A NOT built first") + "; " + (r != null ? r.Status + " " + r.WorldState : "no report") + "; " +
              (both ? "both textured" : "a texture missing") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "world models: and nothing of it is left after the clear (" + WorldTallies() + ")", log);

        // ---- the world models' timeout: a root never answered -- the load reported once, nothing left
        h = make();
        h.Session.Load(WorldCommand(5, true));
        var silent = new WorldFiles();
        foreach (var kv in global.Files) silent.Files[kv.Key] = kv.Value;
        silent.Withheld.Add(WmvTerrainSession.RoleWmoRoot);
        silent.Serve(h, 5, 20);
        bool waiting = h.For(5) == null && h.Last(WmvTerrainSession.RoleWmoRoot) != null;
        h.Now += WmvTerrainSession.WorldTimeoutMs + 1000.0;
        h.Pump(4);
        r = h.For(5);
        Check(waiting && r != null && r.Status == "failed" && r.Reason.Contains("timed out") && h.CountFor(5) == 1 && clean(),
              "world models: a world-model root never answered times out -- one \"failed\" report (" + (r != null ? r.Reason : "no report") +
              "), nothing left (" + WorldTallies() + ")", log);

        // ---- a missing group file: the world model is built without it, and the report says so
        h = make();
        h.Session.Load(WorldCommand(6, true));
        var missing = new WorldFiles();
        foreach (var kv in global.Files) missing.Files[kv.Key] = new Dictionary<int, byte[]>(kv.Value);
        missing.Files[WmvTerrainSession.RoleWmoGroup].Remove((int)gGroup + 1);
        missing.Serve(h, 6);
        r = h.For(6);
        Check(r != null && r.Status == "built" && r.WorldPlaced == 1 && r.WorldGroupFilesMissing == 1,
              "world models: a group file the host does not have leaves the world model built without it, counted (" +
              (r != null ? r.Status + ", placed " + r.WorldPlaced + ", group files missing " + r.WorldGroupFilesMissing : "no report") + ")", log);
        h.Session.Clear("test");
        Check(clean(), "world models: and nothing of it is left after the clear (" + WorldTallies() + ")", log);

        // ---- a root that is not a root: a WMO-only map's world model cannot be built -- "failed", nothing shown or left
        h = make();
        v = (TestWorldView)h.Session.WorldView;
        h.Session.Load(WorldCommand(7, true));
        var bad = new WorldFiles();
        foreach (var kv in global.Files) bad.Files[kv.Key] = new Dictionary<int, byte[]>(kv.Value);
        bad.Files[WmvTerrainSession.RoleWmoRoot][(int)gWmo] = WmoSynthetic.SquaresGroup(0);
        bad.Serve(h, 7);
        r = h.For(7);
        Check(r != null && r.Status == "failed" && v.ShownReports.Count == 0 && clean(),
              "world models: a root that is not a world-model root fails a WMO-only map's load, nothing shown or left (" +
              (r != null ? r.Status + ": " + r.Reason : "no report") + ")", log);
    }
}
