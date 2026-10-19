// WmvLifecycleSelfTestLiquids.cs
//
// The lifecycle self-test's LIQUIDS (protocol 19, docs/world-viewer/09-liquids.md): the terrain session against fake hosts
// with synthetic files only -- a root ADT whose MH2O holds water (with depth), magma (with stored u/v), a frameless ocean and
// a type the catalogue lacks; a world model whose group carries an MLIQ (new form: MOHD 0x4, its type in MOGP; old form: the
// constant 15 and the tile nibble); raw BLP2 frames -- and a test catalogue as the host would send it (types 1 and 13 list the
// same frames). Built by the real builders under the test views' inactive roots, disposed at once (DestroyImmediate).
// Checked: a single tile drawn per type, shown before its frames arrive (base colour) and reported once they have, the
// frames asked once and uploaded as one array per frame list, the materials' modes and queues, the clock; a frame that cannot
// be had (base colour, counted); frames of another mip count (one RGBA32 array) or size (base colour); a type without frames;
// a late frame answer dropped; a frame that arrives after its list settled (its data dropped); a load without "liquids"
// (nothing drawn, "offered" false); a client switch (the old scene's liquids gone at the new commit); the colour by kind and
// the blending by flag (a blended lava); the top-down clip range over a liquid above the terrain; a world model's MLIQ (its
// prototype's child, cloned into every instance, culled by its own renderer, never counted as a group); a streaming session
// (frames asked once for two tiles and a world model, a type pooled when its tile closes, evicted with no pool budget, asked
// again when it comes back; a frame array held only by a pooled prototype counted in the pool, its type evicted with the
// prototype as last used when the prototype was); the IPC lines (the offer read, the report, streamState, runtimeState) --
// and after each, every liquid object, mesh, material and array gone and every tally 0.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    const uint LqF1 = 880001, LqF2 = 880002, LqF3 = 880003, LqM1 = 880011, LqM2 = 880012, LqG1 = 880021;
    const uint LqWmoNew = 9700, LqWmoNewGroup = 9701, LqWmoOld = 9800, LqWmoOldGroup = 9801, LqWmoTex = 9750;

    /// <summary>The liquid tallies, as "objects/meshes/materials/arrays/geometry bytes/array bytes".</summary>
    static string LiquidTallies()
    {
        return WmvLiquidMeshes.LiveObjects + "/" + WmvLiquidMeshes.LiveMeshes + "/" + WmvLiquidRegistry.LiveMaterials + "/" +
               WmvLiquidRegistry.LiveArrays + "/" + WmvLiquidMeshes.LiveGeometryBytes + "/" + WmvLiquidRegistry.LiveArrayBytes;
    }

    static int Arrays() { return Resources.FindObjectsOfTypeAll<Texture2DArray>().Length; }

    /// <summary>A catalogue as the host sends it: 1 Water and 13 WMO Water (the same three frames), 2 Ocean (no frames), 19
    /// Magma (two frames, opaque); `second`: another client's, whose water has one other frame.</summary>
    static WmvIpcClient.LiquidOffer LiquidTestOffer(bool second = false)
    {
        var o = new WmvIpcClient.LiquidOffer
        {
            MaxTexturesPerLoad = 256, MaxTextureBytes = 8 << 20, MaxTextureBytesPerLoad = 64 << 20, CatalogState = "ok", CatalogProduct = "test",
            CatalogBuild = second ? "2" : "1",
        };
        uint[] water = second ? new[] { LqG1 } : new[] { LqF1, LqF2, LqF3 };
        o.Catalog.Add(new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0, MinimapColor = 0x9F12A0D5,
                                           Float0 = 1f, Frames = water, FramesWanted = water.Length });
        o.Catalog.Add(new LiquidTypeView { Id = 13, Name = "WMO Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0, MinimapColor = 0x9F12A0D5,
                                           Float0 = 1f, Frames = water, FramesWanted = water.Length });
        o.Catalog.Add(new LiquidTypeView { Id = 2, Name = "Ocean", Kind = 1, Material = 1, MaterialFlags = 1, Lvf = 2, MinimapColor = 0x9F1060A0,
                                           Float0 = 1f, Frames = new uint[0], FramesWanted = 0 });
        o.Catalog.Add(new LiquidTypeView { Id = 19, Name = "Magma", Kind = 2, Material = 2, MaterialFlags = 0, Lvf = 1, MinimapColor = 0xFFD06800,
                                           Frames = new[] { LqM1, LqM2 }, FramesWanted = 2 });
        o.FrameIds = water.Length * 2 + 2;
        return o;
    }

    /// <summary>A root of tile (x, y) at height 50 whose MH2O is `chunk`'s.</summary>
    static byte[] LiquidRoot(int tx, int ty, Func<int, AdtSynthetic.LiquidChunkSpec> chunk)
    {
        return AdtSynthetic.Root(tx, ty, i => AdtSynthetic.RootMcnk(i, tx, ty, new RootChunkSpec { Flags = 0x18000u, BaseHeight = 50f, Height = k => 0f }),
                                 AdtSynthetic.Mh2o(chunk));
    }

    /// <summary>Chunk 0 water (format 0: heights and depth), 1 magma (format 1: heights and u/v), 2 a flat ocean with no vertex
    /// data, 3 type 7 (the catalogue lacks it); each a whole chunk.</summary>
    static AdtSynthetic.LiquidChunkSpec FourLiquids(int n)
    {
        switch (n)
        {
            case 0: return AdtSynthetic.LiquidChunkSpec.Of(new AdtSynthetic.LiquidInstanceSpec { LiquidType = 1, Format = 0, HeightAt = (i, j) => 60f,
                                                                                                 DepthAt = (i, j) => (byte)(30 * i) });
            case 1: return AdtSynthetic.LiquidChunkSpec.Of(new AdtSynthetic.LiquidInstanceSpec { LiquidType = 19, Format = 1, HeightAt = (i, j) => 61f,
                                                                                                 UAt = (i, j) => (ushort)(42 * j), VAt = (i, j) => (ushort)(42 * i) });
            case 2: return AdtSynthetic.LiquidChunkSpec.Of(new AdtSynthetic.LiquidInstanceSpec { LiquidType = 2, Format = -1, MinHeight = 0f, MaxHeight = 0f });
            case 3: return AdtSynthetic.LiquidChunkSpec.Of(new AdtSynthetic.LiquidInstanceSpec { LiquidType = 7, Format = -1 });
            default: return null;
        }
    }

    /// <summary>Water (type 1) over the whole of chunk n of the tile (n &lt; count).</summary>
    static Func<int, AdtSynthetic.LiquidChunkSpec> WaterChunks(int count, ushort type = 1)
    {
        return n => n < count ? AdtSynthetic.LiquidChunkSpec.Of(new AdtSynthetic.LiquidInstanceSpec { LiquidType = type, Format = 0, HeightAt = (i, j) => 60f })
                              : null;
    }

    static WmvIpcClient.TerrainTileCommand LiquidCommand(int load, int tileX = TerrainTestTileX, int tileY = TerrainTestTileY, bool second = false)
    {
        WmvIpcClient.TerrainTileCommand c = TerrainCommand(load, tileX, tileY);
        c.Liquids = LiquidTestOffer(second);
        if (second)
            c.AssetEpoch = 8;
        return c;
    }

    /// <summary>The files of a single liquid tile: its WDT, its root (FourLiquids) and the five frames (`except`'s left out).</summary>
    static WorldFiles LiquidTileFiles(uint except = 0)
    {
        var f = new WorldFiles();
        f.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, TerrainWdt(true, TerrainTestTileX, TerrainTestTileY, (int)TerrainTestRoot));
        f.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, LiquidRoot(TerrainTestTileX, TerrainTestTileY, FourLiquids));
        AddLiquidFrames(f, except);
        return f;
    }

    static void AddLiquidFrames(WorldFiles f, uint except = 0)
    {
        int seed = 40;
        foreach (uint id in new[] { LqF1, LqF2, LqF3, LqM1, LqM2, LqG1 })
            if (id != except)
                f.Add(WmvTerrainSession.RoleLiquidTexture, id, RawBlp(seed++, 8, 8));
    }

    /// <summary>A world model of one 10-yard square group (SquaresGroup 0's geometry) whose group carries an MLIQ of 2 x 2
    /// tiles at height 5: new form (MOHD 0x4: MOGP +0x34 is the type) or old (MOHD 0x4 clear, +0x34 the constant 15, the tile
    /// nibble the identity).</summary>
    static byte[] LiquidWmoRoot(uint group, ushort flags, uint texture = LqWmoTex)
    {
        return WmoSynthetic.BuildRoot(new WmoSynthetic.RootSpec
        {
            Materials = new[] { WmoSynthetic.Material(0, 0, 0, texture) },
            GroupInfos = new[] { WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(0f, 0f, 0f), new WowVec3(10f, 10f, 0f), 1) },
            GroupFileDataIDs = new[] { group }, GroupNames = new[] { "pool" }, Flags = flags,
            BoundsMin = new WowVec3(0f, 0f, 0f), BoundsMax = new WowVec3(10f, 10f, 5f),
        });
    }

    static byte[] LiquidWmoGroup(uint groupLiquid, byte tile)
    {
        var spec = new WmoSynthetic.GroupSpec
        {
            Flags = WmoGroupFlags.Outdoor | WmoGroupFlags.Liquid, GroupLiquid = groupLiquid,
            Positions = new[] { 0f, 0f, 0f, 10f, 0f, 0f, 10f, 10f, 0f, 0f, 10f, 0f },
            Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
            Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
            Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, 0) },
            Mpy2 = new byte[] { 0x20, 0, 0, 0, 0x20, 0, 0, 0 },
        };
        spec.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
        spec.ExtraChunks.Add(new KeyValuePair<string, byte[]>("MLIQ", WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec
        {
            VerticesX = 3, VerticesY = 3, Corner = new WowVec3(0f, 0f, 5f), Tile = (i, j) => tile, Word = (i, j) => WmoSynthetic.LiquidDepthWord(200),
        })));
        return WmoSynthetic.BuildGroup(spec);
    }

    static void TerrainLiquidTests(Action<string> log)
    {
        int meshes0 = Meshes(), materials0 = Materials(), arrays0 = Arrays();
        var views = new List<TestWorldView>();
        long poolGpu = WmvTerrainSession.StreamPoolGpuBudget, poolManaged = WmvTerrainSession.StreamPoolManagedBudget;
        Check(LiquidTallies() == "0/0/0/0/0/0", "liquids: nothing of the liquids is held before the tests (" + LiquidTallies() + ")", log);
        Func<bool> clean = () => LiquidTallies() == "0/0/0/0/0/0" && Meshes() == meshes0 && Materials() == materials0 && Arrays() == arrays0 &&
                                 WmvRuntimeTerrainTile.Live == 0;
        try
        {
            LiquidTileTests(log, clean);
            LiquidWorldModelTests(log, views, clean);
            LiquidStreamingTests(log, views, clean);
            LiquidIpcTests(log);
        }
        catch (Exception e)
        {
            Check(false, "liquids: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            WmvTerrainSession.StreamPoolGpuBudget = poolGpu;
            WmvTerrainSession.StreamPoolManagedBudget = poolManaged;
            foreach (TestWorldView v in views)
                v.Destroy();
        }
        Check(clean(), "liquids: every liquid object, mesh, material and array these tests made is gone and every tally is 0 (" + LiquidTallies() +
                       "; meshes " + meshes0 + " -> " + Meshes() + ", materials " + materials0 + " -> " + Materials() + ", arrays " + arrays0 + " -> " +
                       Arrays() + ")", log);
    }

    static int AskedFor(TerrainHarness h, string role, uint fid)
    {
        int n = 0;
        foreach (string[] q in h.Sent) if (q[1] == role && q[2] == fid.ToString()) n++;
        return n;
    }

    static float FrameCountOf(WmvLiquidRegistry reg, int type)
    {
        WmvLiquidType t = reg != null ? reg.Get(type) : null;
        return t != null && t.Material != null ? t.Material.GetFloat(WmvLiquidMaterials.FrameCountId) : -1f;
    }

    static string Types(WmvIpcClient.TerrainLiquidReport l)
    {
        return l == null ? "-" : string.Join(",", l.Types.ConvertAll(x => x.ToString()).ToArray());
    }

    static void LiquidTileTests(Action<string> log, Func<bool> clean)
    {
        string rl = WmvTerrainSession.RoleLiquidTexture;

        // ---- one tile: drawn per type, shown before its frames, reported once they are in
        var h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        WorldFiles files = LiquidTileFiles();
        files.Withheld.Add(rl);
        h.Session.Load(LiquidCommand(1));
        files.Serve(h, 1, 30);
        WmvRuntimeTerrainTile tile = h.Session.ShownTile;
        WmvLiquidRegistry reg = h.Session.SceneLiquids;
        int liquidChildren = 0, onLayer = 0;
        if (tile != null && tile.Root != null)
            foreach (Transform c in tile.Root.transform)
                if (c.name.StartsWith("MH2O liquid type ", StringComparison.Ordinal))
                {
                    liquidChildren++;
                    if (c.gameObject.layer == WmvTerrainBuilder.Layer) onLayer++;
                }
        Check(h.View.Shown == 1 && h.For(1) == null && tile != null && reg != null && tile.LiquidObjectCount == 3 && liquidChildren == 3 && onLayer == 3 &&
              h.Session.LiquidRequestsOut == WmvTerrainSession.MaxLiquidOutstanding && FrameCountOf(reg, 1) == 0f && FrameCountOf(reg, 19) == 0f,
              "liquids: a tile is shown before its frames arrive -- three liquid objects on layer 30 (water, magma, the frameless ocean; the type " +
              "the catalogue lacks not drawn) in their base colour, " + WmvTerrainSession.MaxLiquidOutstanding + " frame requests out, the report " +
              "waiting (" + (tile != null ? tile.LiquidObjectCount + " objects, " : "no tile, ") + h.Session.LiquidRequestsOut + " out, " +
              (h.For(1) != null ? "REPORTED" : "not reported") + ")", log);
        float topLo = float.NaN, topHi = float.NaN;
        if (tile != null)
            WmvMain.TopDownRange(tile, out topLo, out topHi);
        Check(tile != null && tile.HasBounds && tile.Bounds.min.y == 50f && tile.Bounds.max.y == 50f && tile.HasLiquidBounds &&
              tile.LiquidBounds.min.y == 0f && tile.LiquidBounds.max.y == 61f && topLo == 0f && topHi == 61f,
              "liquids: the top-down capture's clip range takes the liquid above and below the terrain (heights " + topLo + " .. " + topHi +
              ", the ocean at 0 and the magma at 61 around terrain at 50); the tile's own bounds stay the terrain's (" +
              (tile != null ? tile.Bounds.min.y + " .. " + tile.Bounds.max.y : "no tile") + ")", log);
        files.Withheld.Clear();
        files.Serve(h, 1);
        WmvIpcClient.TerrainTileReport r = h.For(1);
        WmvIpcClient.TerrainLiquidReport l = r != null ? r.Liquids : null;
        long geometry = 3L * (81 * 28 + 384 * 2);
        Check(r != null && r.Status == "built" && h.CountFor(1) == 1 && l != null && l.Offered && l.Catalog == "ok" && l.Instances == 4 && l.Layers == 1 &&
              l.Chunks == 4 && l.Quads == 256 && l.Vertices == 324 && l.Meshes == 3 && Types(l) == "1,19,2,7" && l.MissingTypes == 1 &&
              l.TexturesRequested == 5 && l.TexturesLoaded == 5 && l.TexturesFailed == 0 && l.FormatBySize == 0 && l.FormatDisagreements == 0 &&
              l.Malformed == 0 && l.GeometryBytes == geometry && l.TextureBytes > 0,
              "liquids: the tile is reported built once its frames are in -- 4 instances in 4 chunks, 256 quads, 324 vertices, 3 meshes, types " +
              "[1,19,2,7] (7 missing), 5 frames requested and loaded, " + geometry + " geometry bytes (" +
              (l != null ? l.Instances + "/" + l.Chunks + "/" + l.Quads + "/" + l.Vertices + "/" + l.Meshes + ", types [" + Types(l) + "], missing " +
                           l.MissingTypes + ", textures " + l.TexturesRequested + "/" + l.TexturesLoaded + "/" + l.TexturesFailed + ", bytes " +
                           l.GeometryBytes + "/" + l.TextureBytes : "no liquids in the report") + ")", log);
        int asked = 0;
        foreach (uint id in new[] { LqF1, LqF2, LqF3, LqM1, LqM2 })
            asked += AskedFor(h, rl, id) == 1 ? 1 : 0;
        WmvLiquidType water = reg != null ? reg.Get(1) : null, magma = reg != null ? reg.Get(19) : null, ocean = reg != null ? reg.Get(2) : null;
        Check(asked == 5 && h.Sent.FindAll(q => q[1] == rl).Count == 5 && reg.ArrayCount == 2 && water != null && water.Animated &&
              water.Frames.Array.depth == 3 && magma != null && magma.Animated && magma.Frames.Array.depth == 2 && FrameCountOf(reg, 1) == 3f &&
              FrameCountOf(reg, 19) == 2f && ocean != null && !ocean.Animated && FrameCountOf(reg, 2) == 0f,
              "liquids: each frame asked once; water's three and magma's two uploaded as one array each and bound; the ocean (no frames) " +
              "stays in its base colour (" + (reg != null ? reg.Summary() : "no registry") + ")", log);
        Check(water != null && water.Material.renderQueue == WmvLiquidMaterials.TransparentQueue && water.Material.GetFloat(WmvLiquidMaterials.ZWriteId) == 0f &&
              water.Material.GetFloat(WmvLiquidMaterials.ModeId) == WmvLiquidMaterials.ModeTransparent &&
              water.Material.GetFloat(WmvLiquidMaterials.CullId) == (float)UnityEngine.Rendering.CullMode.Off &&
              magma != null && magma.Material.renderQueue == WmvLiquidMaterials.OpaqueQueue && magma.Material.GetFloat(WmvLiquidMaterials.ZWriteId) == 1f &&
              magma.Material.GetFloat(WmvLiquidMaterials.ModeId) == WmvLiquidMaterials.ModeOpaque &&
              water.Material.GetVector(WmvLiquidMaterials.BaseColorId) == WmvLiquidMaterials.BaseColour(0x9F12A0D5) &&
              WmvLiquidMaterials.TransparentQueue == 2999,
              "liquids: water (material flag 0x1) is blended after the opaque world and before the M2 transparents (queue 2999, no depth " +
              "write, both sides), magma opaque (queue 2000, depth write); the base colour is the minimap colour as stored", log);
        // Retail's "10FX" lava: kind magma on a transparent material (13: flag 0x1, LVF 3) -- blended by its flag, coloured by its
        // kind (the frame's colour), never the water's base colour + highlight.
        Shader liquidShader = WmvLiquidMaterials.ResolveShader(null);
        var lavaView = new LiquidTypeView { Id = 1174, Name = "10FX Lava", Kind = 2, Material = 13, MaterialFlags = 1, Lvf = 3, MinimapColor = 0xFFFF3A00 };
        Material lava = liquidShader != null ? WmvLiquidMaterials.CreateMaterial(liquidShader, lavaView) : null;
        try
        {
            Check(lava != null && lava.HasProperty(WmvLiquidMaterials.ColourFromFrameId) && lava.GetFloat(WmvLiquidMaterials.ModeId) == WmvLiquidMaterials.ModeTransparent &&
                  lava.renderQueue == WmvLiquidMaterials.TransparentQueue && lava.GetFloat(WmvLiquidMaterials.ZWriteId) == 0f &&
                  lava.GetFloat(WmvLiquidMaterials.ColourFromFrameId) == 1f && water != null && water.Material.GetFloat(WmvLiquidMaterials.ColourFromFrameId) == 0f &&
                  magma != null && magma.Material.GetFloat(WmvLiquidMaterials.ColourFromFrameId) == 1f,
                  "liquids: the colour follows the kind, the blending the flag -- a magma-kind type on a transparent material (Retail's 10FX lava, " +
                  "material 13) is blended (queue 2999, no depth write) and takes its frame's colour; water the base colour + highlight, magma " +
                  "the frame's (" + (lava != null ? "lava mode " + lava.GetFloat(WmvLiquidMaterials.ModeId) + ", from frame " +
                                                    lava.GetFloat(WmvLiquidMaterials.ColourFromFrameId) : "no shader") + ")", log);
        }
        finally
        {
            if (lava != null)
                UnityEngine.Object.DestroyImmediate(lava);
        }
        h.Session.TickLiquids(0.2);
        bool at3 = water != null && water.Material.GetFloat(WmvLiquidMaterials.FrameId) == 0f && magma.Material.GetFloat(WmvLiquidMaterials.FrameId) == 1f;
        h.Session.TickLiquids(1.0 / WmvLiquidMaterials.FramesPerSecond + 1e-6);
        bool at1 = water != null && water.Material.GetFloat(WmvLiquidMaterials.FrameId) == 1f && magma.Material.GetFloat(WmvLiquidMaterials.FrameId) == 1f;
        h.Session.TickLiquids(0.0);
        bool at0 = water != null && water.Material.GetFloat(WmvLiquidMaterials.FrameId) == 0f && magma.Material.GetFloat(WmvLiquidMaterials.FrameId) == 0f;
        Check(at3 && at1 && at0,
              "liquids: the clock sets each type's frame on its shared material -- floor(t x " + WmvLiquidMaterials.FramesPerSecond + ") mod its " +
              "frames (0.2 s: water 0 of 3, magma 1 of 2; one step: 1 and 1; pinned at 0: 0 and 0)", log);
        string line = r != null ? WmvIpcClient.TerrainTileLoadedLine(r) : "";
        Check(line.Contains("\"liquids\":{\"offered\":true,\"catalog\":\"ok\",\"instances\":4,\"layers\":1,\"chunks\":4,\"quads\":256,\"vertices\":324," +
                            "\"meshes\":3,\"types\":[1,19,2,7],\"texturesRequested\":5,\"texturesLoaded\":5,\"texturesFailed\":0,\"missingTypes\":1,") &&
              JsonUtility.FromJson<TerrainLineProbe>(line).load == 1,
              "liquids: terrainTileLoaded carries them under the contract's names (" +
              line.Substring(Math.Max(0, line.IndexOf("\"liquids\"", StringComparison.Ordinal))) + ")", log);
        // A view rebuild draws the liquid again from the kept content, and hands the types over (nothing fetched again).
        string reason;
        double ms;
        int load;
        string applied = h.Session.RebuildShown("test", out reason, out ms, out load);
        WmvRuntimeTerrainTile rebuilt = h.Session.ShownTile;
        Check(applied == "applied" && rebuilt != null && rebuilt != tile && rebuilt.LiquidObjectCount == 3 && WmvLiquidMeshes.LiveObjects == 3 &&
              WmvLiquidRegistry.LiveMaterials == 3 && WmvLiquidRegistry.LiveArrays == 2 && h.Sent.FindAll(q => q[1] == rl).Count == 5 &&
              FrameCountOf(h.Session.SceneLiquids, 1) == 3f,
              "liquids: a view rebuild draws the liquid again from the kept content -- the same three types, materials and arrays, nothing asked " +
              "again (" + applied + ", " + LiquidTallies() + ")", log);
        h.Session.Clear("test");
        Check(clean(), "liquids: clearTerrain takes the tile's liquid away -- objects, meshes, materials, arrays (" + LiquidTallies() + ")", log);

        // ---- a late frame answer: the load superseded with its frames out, the answer dropped, nothing uploaded
        h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        files = LiquidTileFiles();
        files.Withheld.Add(rl);
        h.Session.Load(LiquidCommand(2));
        files.Serve(h, 2, 30);
        string[] pending = h.Sent.Find(q => q[1] == rl && q[3] == "2" && !files.Answered.Contains(q[0]));
        h.Session.Load(LiquidCommand(3));
        WmvIpcClient.TerrainTileReport superseded = h.For(2);
        int lateBefore = h.Session.LateAnswersDropped, arraysBefore = WmvLiquidRegistry.LiveArrays;
        bool applied2 = pending != null && h.Answer(pending[0], RawBlp(40, 8, 8));
        Check(pending != null && superseded != null && superseded.Status == "superseded" && superseded.Liquids != null &&
              superseded.Liquids.TexturesRequested == WmvTerrainSession.MaxLiquidOutstanding && superseded.Liquids.TexturesLoaded == 0 && !applied2 &&
              h.Session.LateAnswersDropped == lateBefore + 1 && WmvLiquidRegistry.LiveArrays == arraysBefore,
              "liquids: a frame answer that comes after its load was superseded is dropped as late, nothing uploaded (" +
              (superseded != null ? superseded.Status : "no report") + ", applied " + applied2 + ")", log);
        files.Withheld.Clear();
        files.Serve(h, 3);
        r = h.For(3);
        Check(r != null && r.Status == "built" && r.Liquids != null && r.Liquids.TexturesLoaded == 5 && WmvLiquidRegistry.LiveMaterials == 3 &&
              WmvLiquidRegistry.LiveArrays == 2 && WmvLiquidMeshes.LiveObjects == 3,
              "liquids: the newer load draws its own -- the superseded load's materials and objects went with its tile at the commit (" +
              LiquidTallies() + ")", log);

        // ---- a client switch: the new client's catalogue, the old scene's liquids gone at the new commit
        var second = new WorldFiles();
        second.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, TerrainWdt(true, TerrainTestTileX, TerrainTestTileY, (int)TerrainTestRoot));
        second.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, LiquidRoot(TerrainTestTileX, TerrainTestTileY, WaterChunks(2)));
        AddLiquidFrames(second);
        h.Session.Load(LiquidCommand(4, second: true));
        second.Serve(h, 4);
        r = h.For(4);
        reg = h.Session.SceneLiquids;
        water = reg != null ? reg.Get(1) : null;
        Check(r != null && r.Status == "built" && r.Liquids != null && r.Liquids.TexturesRequested == 1 && water != null && water.Frames.Fids.Length == 1 &&
              water.Frames.Fids[0] == LqG1 && FrameCountOf(reg, 1) == 1f && WmvLiquidRegistry.LiveMaterials == 1 && WmvLiquidRegistry.LiveArrays == 1 &&
              WmvLiquidMeshes.LiveObjects == 1 && AskedFor(h, rl, LqG1) == 1,
              "liquids: another client's load draws its water with that client's frames; the old client's materials and arrays are gone (" +
              LiquidTallies() + ")", log);
        h.Session.Clear("test");
        Check(clean(), "liquids: and the clear takes it away (" + LiquidTallies() + ")", log);

        // ---- a frame that cannot be had: its type is drawn in its base colour, the report counts it
        h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        files = LiquidTileFiles(LqM2);
        h.Session.Load(LiquidCommand(5));
        files.Serve(h, 5);
        r = h.For(5);
        reg = h.Session.SceneLiquids;
        magma = reg != null ? reg.Get(19) : null;
        Check(r != null && r.Status == "built" && r.Liquids != null && r.Liquids.TexturesRequested == 5 && r.Liquids.TexturesLoaded == 4 &&
              r.Liquids.TexturesFailed == 1 && magma != null && !magma.Animated && magma.Frames.State == WmvLiquidFrames.StateFailed &&
              FrameCountOf(reg, 19) == 0f && FrameCountOf(reg, 1) == 3f && WmvLiquidRegistry.LiveArrays == 1,
              "liquids: a frame the host does not have leaves its type (magma) in its base colour, counted failed; the water is animated (" +
              (magma != null ? magma.Frames.Detail : "no magma") + ")", log);
        h.Session.Clear("test");

        // ---- a frame that comes after its list settled: water's first frame fails for good while its second is out -- the list
        // is drawn in its base colour, and the second frame's prepared data, which nothing will upload, are dropped when it lands
        h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        files = LiquidTileFiles();
        files.Withheld.Add(rl);
        h.Session.Load(LiquidCommand(8));
        files.Serve(h, 8, 30);
        reg = h.Session.SceneLiquids;
        string[] askF1 = h.Sent.Find(q => q[1] == rl && q[2] == LqF1.ToString()), askF2 = h.Sent.Find(q => q[1] == rl && q[2] == LqF2.ToString());
        bool lateFailed = askF1 != null && askF2 != null && h.Answer(askF1[0], null, "missing", "test: frame " + LqF1 + " is gone") &&
                          reg != null && reg.Get(1) != null && reg.Get(1).Frames.State == WmvLiquidFrames.StateFailed;
        bool lateApplied = askF2 != null && h.Answer(askF2[0], RawBlp(41, 8, 8));
        h.Pump(2);
        if (askF1 != null) files.Answered.Add(askF1[0]);
        if (askF2 != null) files.Answered.Add(askF2[0]);
        WmvLiquidFrame lateFrame = reg != null ? reg.Frame(LqF2) : null;
        bool lateDropped = lateFrame != null && lateFrame.State == WmvLiquidFrame.StateReady && (lateFrame.Prepared == null || !lateFrame.Prepared.Ready);
        files.Withheld.Clear();
        files.Serve(h, 8);
        r = h.For(8);
        water = reg != null ? reg.Get(1) : null;
        magma = reg != null ? reg.Get(19) : null;
        WmvLiquidFrame lateF3 = reg != null ? reg.Frame(LqF3) : null;
        Check(lateFailed && lateApplied && lateDropped && r != null && r.Status == "built" && water != null && !water.Animated && magma != null &&
              magma.Animated && lateF3 != null && (lateF3.Prepared == null || !lateF3.Prepared.Ready) && WmvLiquidRegistry.LiveArrays == 1,
              "liquids: a frame that lands after its list settled (an earlier frame of the list failed for good while it was out) is not " +
              "uploaded and its prepared data are dropped at once; the list stays in its base colour, magma animated (" +
              (water != null ? water.Frames.Detail : "no water") + "; dropped " + lateDropped + ")", log);
        h.Session.Clear("test");

        // ---- frames of one list that differ: in mip count (one RGBA32 array with the levels they all have), in size (none)
        h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        files = LiquidTileFiles();
        files.Add(rl, LqF2, RawBlp(41, 8, 8, 2));
        files.Add(rl, LqM2, RawBlp(44, 16, 16));
        h.Session.Load(LiquidCommand(7));
        files.Serve(h, 7);
        r = h.For(7);
        reg = h.Session.SceneLiquids;
        water = reg != null ? reg.Get(1) : null;
        magma = reg != null ? reg.Get(19) : null;
        Check(r != null && r.Status == "built" && water != null && water.Animated && water.Frames.Array.mipmapCount == 2 && water.Frames.Array.depth == 3 &&
              water.Frames.Array.graphicsFormat == UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_UNorm && magma != null && !magma.Animated &&
              magma.Frames.Detail.Contains("differ in size") && FrameCountOf(reg, 19) == 0f && WmvLiquidRegistry.LiveArrays == 1,
              "liquids: frames of one size but another mip count go up as one RGBA32 array with the levels they all have; frames of another " +
              "size are not animated (base colour) (" + (water != null && water.Frames.Array != null ? water.Frames.Array.mipmapCount + " level(s)" : "no array") +
              "; " + (magma != null ? magma.Frames.Detail : "no magma") + ")", log);
        h.Session.Clear("test");

        // ---- a host that offers no liquids (protocol 18): nothing drawn, nothing asked, "offered" false
        h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        files = LiquidTileFiles();
        h.Session.Load(TerrainCommand(6, TerrainTestTileX, TerrainTestTileY));
        files.Serve(h, 6);
        r = h.For(6);
        Check(r != null && r.Status == "built" && r.Liquids != null && !r.Liquids.Offered && r.Liquids.Catalog == "absent" && r.Liquids.Instances == 0 &&
              h.Sent.FindAll(q => q[1] == rl).Count == 0 && WmvLiquidMeshes.LiveObjects == 0 && WmvLiquidRegistry.LiveMaterials == 0 &&
              h.Session.SceneLiquids == null,
              "liquids: a load without \"liquids\" (an older host) draws none and asks for none, its report says offered false, catalogue absent", log);
        h.Session.Clear("test");
        Check(clean(), "liquids: and nothing of the tile tests is left (" + LiquidTallies() + ")", log);
    }

    static void LiquidWorldModelTests(Action<string> log, List<TestWorldView> views, Func<bool> clean)
    {
        // ---- a world model's MLIQ: a child of its group in the prototype, cloned into every instance, culled by its own bounds;
        // a new-form group (MOHD 0x4, type 13) and an old-form one (the constant 15, nibble 0: water, 13 by the documented rule)
        int idx = WorldTestTileY * 64 + WorldTestTileX;
        byte[] wdt = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(idx),
                                      AdtSynthetic.Maid(idx, TerrainTestRoot, WorldTestObj0));
        byte[] obj0 = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Chunk("MDDF", new byte[0]),
                                       AdtSynthetic.Chunk("MODF", AdtSynthetic.Cat(AdtSynthetic.ModfRecord(LqWmoNew, 1, 0x8, 1024),
                                                                                   AdtSynthetic.ModfRecord(LqWmoNew, 2, 0x8, 1024),
                                                                                   AdtSynthetic.ModfRecord(LqWmoOld, 3, 0x8, 1024))));
        var files = new WorldFiles();
        files.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, wdt);
        files.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
        files.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, obj0);
        files.Add(WmvTerrainSession.RoleWmoRoot, LqWmoNew, LiquidWmoRoot(LqWmoNewGroup, WmoLiquid.RootFlagLiquidTypeInGroup));
        files.Add(WmvTerrainSession.RoleWmoGroup, LqWmoNewGroup, LiquidWmoGroup(13, 0x04));
        files.Add(WmvTerrainSession.RoleWmoRoot, LqWmoOld, LiquidWmoRoot(LqWmoOldGroup, 0));
        files.Add(WmvTerrainSession.RoleWmoGroup, LqWmoOldGroup, LiquidWmoGroup(15, 0x00));
        files.Add(WmvTerrainSession.RoleWmoTexture, LqWmoTex, RawBlp(29, 8, 8));
        AddLiquidFrames(files);
        var h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        var v = new TestWorldView();
        views.Add(v);
        h.Session.WorldView = v;
        WmvIpcClient.TerrainTileCommand c = WorldCommand(10, false);
        c.Liquids = LiquidTestOffer();
        h.Session.Load(c);
        files.Serve(h, 10);
        WmvIpcClient.TerrainTileReport r = h.For(10);
        WmvIpcClient.TerrainLiquidReport l = r != null ? r.Liquids : null;
        WmvWorldScene scene = h.Session.World;
        WmvLiquidRegistry reg = h.Session.SceneLiquids;
        WmvLiquidType wmoWater = reg != null ? reg.Get(13) : null;
        int instancesWithLiquid = 0;
        bool culledByOwn = true;
        if (scene != null && wmoWater != null)
            foreach (WmvWorldInstance i in scene.Instances)
                foreach (Renderer rr in i.Renderers)
                    if (rr != null && rr.sharedMaterial == wmoWater.Material)
                    {
                        instancesWithLiquid++;
                        // The instance culls it as one of its renderers, by its own mesh's box: the liquid at height 5 over two
                        // tiles of 100/24 yd, not the group's floor at 0 (the MOGP box).
                        MeshFilter mf = rr.GetComponent<MeshFilter>();
                        Bounds b = mf != null && mf.sharedMesh != null ? mf.sharedMesh.bounds : new Bounds();
                        culledByOwn &= mf != null && Math.Abs(b.center.y - 5f) < 1e-4f && b.size.y < 1e-4f &&
                                       Math.Abs(b.size.x - (float)(2 * WmoLiquid.TileSize)) < 1e-3f;
                    }
        Check(r != null && r.Status == "built" && l != null && l.WmoGroups == 2 && l.WmoQuads == 8 && l.Meshes == 2 && Types(l) == "13" &&
              l.TexturesRequested == 3 && l.TexturesLoaded == 3 && scene != null && scene.Instances.Count == 3 && instancesWithLiquid == 3 && culledByOwn &&
              WmvLiquidMeshes.LiveObjects == 2 && WmvLiquidMeshes.LiveMeshes == 2 && FrameCountOf(reg, 13) == 3f,
              "liquids: a world model's MLIQ is a child of its group's object in the prototype (new form: type 13 from MOGP; old form: the " +
              "constant 15 and nibble 0, water, 13), cloned into each of the 3 instances with its own renderer bounds; its frames fetched (" +
              (l != null ? "groups " + l.WmoGroups + ", quads " + l.WmoQuads + ", meshes " + l.Meshes + ", types [" + Types(l) + "], textures " +
                           l.TexturesRequested + "/" + l.TexturesLoaded : "no liquids in the report") + ", " + instancesWithLiquid + " instance(s) with " +
              "liquid, " + LiquidTallies() + ")", log);
        // Its liquid is not a group: each instance draws 1 group and 1 liquid; the placement report says 1 group drawn of 1, the
        // culling counts 3 groups for the 3 instances (each liquid culled by its own box all the same).
        int reportedOk = 0, liquidRenderers = 0, groupRenderers = 0;
        if (r != null)
            foreach (WmvIpcClient.WorldPlacementReport p in r.WorldPlacementList)
                if (p.Groups == 1 && p.GroupsDrawn == 1) reportedOk++;
        if (scene != null)
            foreach (WmvWorldInstance i in scene.Instances)
            {
                groupRenderers += i.GroupRenderers;
                if (i.LiquidRenderers != null)
                    foreach (bool b in i.LiquidRenderers) if (b) liquidRenderers++;
            }
        var cullCamera = new GameObject("LiquidCullTestCamera");
        int culledGroups = -1, visibleGroups = -1, outGroups = -1;
        try
        {
            Camera cam = cullCamera.AddComponent<Camera>();
            cam.enabled = false;
            v.Models.Cull(cam);
            culledGroups = v.Models.Groups;
            visibleGroups = v.Models.GroupsVisible;
            outGroups = v.Models.GroupsCulled;
            v.Models.Uncull();
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(cullCamera);
        }
        Check(reportedOk == 3 && liquidRenderers == 3 && groupRenderers == 3 && culledGroups == 3 && visibleGroups + outGroups == 3,
              "liquids: a group's liquid renderer is culled by its own box but never counted as a group -- 3 placements report 1 group drawn " +
              "of 1, the culling counts 3 groups (" + reportedOk + " placement(s) right, " + groupRenderers + " group and " + liquidRenderers +
              " liquid renderer(s), culling " + culledGroups + " = " + visibleGroups + " + " + outGroups + ")", log);
        h.Session.Clear("test");
        Check(clean() && WorldTallies() == "0/0/0/0/0",
              "liquids: clearTerrain takes the world models' liquid away with their prototypes (" + LiquidTallies() + ", world " + WorldTallies() + ")", log);
    }

    /// <summary>The streaming map (StreamFiles) with liquid: tiles 32_50 and 33_50 water (type 1), 31_50 magma (type 19), the
    /// world model of placements 500 and 501 an MLIQ of type 13 (the same frames as type 1).</summary>
    static WorldFiles LiquidStreamFiles()
    {
        WorldFiles f = StreamFiles();
        f.Add(WmvTerrainSession.RoleRootAdt, StreamRootId(32, 50), LiquidRoot(32, 50, WaterChunks(3)));
        f.Add(WmvTerrainSession.RoleRootAdt, StreamRootId(33, 50), LiquidRoot(33, 50, WaterChunks(3)));
        f.Add(WmvTerrainSession.RoleRootAdt, StreamRootId(31, 50), LiquidRoot(31, 50, n => n == 0 ? FourLiquids(1) : null));
        byte[] root = WmoSynthetic.Concat(LiquidWmoRoot(StreamWmoGroup, WmoLiquid.RootFlagLiquidTypeInGroup, StreamWmoTex),
                                          WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 1)),
                                          WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(StreamM2A)),
                                          WmoSynthetic.Chunk("MODD", ModdRecord(0, 2f, 2f, 0f)));
        f.Add(WmvTerrainSession.RoleWmoRoot, StreamWmo, root);
        f.Add(WmvTerrainSession.RoleWmoGroup, StreamWmoGroup, LiquidWmoGroup(13, 0x04));
        AddLiquidFrames(f);
        return f;
    }

    static void LiquidStreamingTests(Action<string> log, List<TestWorldView> views, Func<bool> clean)
    {
        string rl = WmvTerrainSession.RoleLiquidTexture;
        var hh = new TerrainHarness();
        var vv = new TestWorldView();
        views.Add(vv);
        var h = new StreamHost(hh, vv, LiquidStreamFiles());
        WmvTerrainSession s = h.H.Session;
        WmvIpcClient.TerrainTileCommand c = StreamCommand(20);
        c.Liquids = LiquidTestOffer();
        s.Load(c);
        bool settled = h.Until(() => h.H.For(20) != null && h.Settled() && s.StreamTiles == 9);
        for (int k = 0; k < 20; k++) h.Round();
        WmvLiquidRegistry reg = s.SceneLiquids;
        WmvLiquidType water = reg != null ? reg.Get(1) : null, wmoWater = reg != null ? reg.Get(13) : null, magma = reg != null ? reg.Get(19) : null;
        int waterTiles = 0;
        foreach (string t in new[] { "32_50", "33_50", "31_50" })
        {
            int tx = int.Parse(t.Substring(0, 2)), ty = int.Parse(t.Substring(3, 2));
            WorldOrigin o = WorldToUnity.TileOrigin(tx, ty);
            WmvRuntimeTerrainTile tile;
            TerrainTileMesh mesh;
            if (s.ResidentTileAt(o.X - 10, o.Y - 10, out tile, out mesh) && tile.LiquidObjectCount == 1) waterTiles++;
        }
        WmvIpcClient.StreamStateReport last = h.States.Count > 0 ? h.States[h.States.Count - 1] : null;
        Check(settled && reg != null && reg.Pooled && water != null && wmoWater != null && magma != null && water.Frames == wmoWater.Frames &&
              water.Animated && magma.Animated && reg.ArrayCount == 2 && h.Asked(rl, LqF1) == 1 && h.Asked(rl, LqF2) == 1 && h.Asked(rl, LqF3) == 1 &&
              h.Asked(rl, LqM1) == 1 && h.Asked(rl, LqM2) == 1 && waterTiles == 3 && last != null && last.LiquidTiles == 3 && last.LiquidTypes == 3 &&
              last.LiquidTextures == 2 && last.LiquidBytes > 0,
              "liquids: a streaming session asks each frame once for the whole scene -- the water of two tiles and the world model's (types 1 " +
              "and 13, one frame list) share one array; magma's tile its own; streamState counts them (" + (reg != null ? reg.Summary() : "no registry") +
              "; streamState " + (last != null ? last.LiquidTiles + " tiles, " + last.LiquidTypes + " types, " + last.LiquidTextures + " arrays, " +
                                                   last.LiquidBytes + " bytes" : "none") + ")", log);

        // ---- the magma tile closes: its type is pooled (nothing holds it), kept while the pools fit
        h.Over(35, 50);
        bool away = h.Until(() => h.Settled() && s.StreamTiles == 6);
        magma = reg != null ? reg.Get(19) : null;
        Check(away && magma != null && magma.Pooled && magma.Animated && reg.PooledCount == 1 && WmvLiquidRegistry.LiveArrays == 2,
              "liquids: when the magma tile closes its type is pooled -- material and frames kept, nothing holds them (" + (reg != null ? reg.Summary() : "-") + ")",
              log);

        // ---- no pool budget: evicted; the tile coming back asks for its frames again
        WmvTerrainSession.StreamPoolGpuBudget = 0;
        WmvTerrainSession.StreamPoolManagedBudget = 0;
        // (The pools are looked at twice a second: 40 rounds of 16 ms.)
        for (int k = 0; k < 40; k++) h.Round();
        bool evicted = reg != null && reg.Get(19) == null && reg.TypesEvicted >= 1 && WmvLiquidRegistry.LiveArrays == 1;
        string evictedState = reg != null ? reg.Summary() : "-";
        WmvTerrainSession.StreamPoolGpuBudget = 256L << 20;
        WmvTerrainSession.StreamPoolManagedBudget = 128L << 20;
        h.Over(32, 50);
        bool back = h.Until(() => h.Settled() && s.StreamTiles == 12);
        for (int k = 0; k < 10; k++) h.Round();
        magma = reg != null ? reg.Get(19) : null;
        Check(evicted && back && magma != null && magma.Animated && h.Asked(rl, LqM1) == 2 && h.Asked(rl, LqM2) == 2 && h.Asked(rl, LqF1) == 1,
              "liquids: with no pool budget the pooled magma type is evicted (" + evictedState + ") and its frames asked for again when its tile " +
              "comes back; the water, held throughout, never (" + (reg != null ? reg.Summary() : "-") + ")", log);

        // ---- every tile away: the world model's prototype is pooled and still holds type 13, the only other type on its frames
        // (water) is pooled -- the array is the pool's (the Stage 08 rule of the M2 texture uploads); with no budget the
        // prototype goes, and type 13 with it in the same pass, as last used when the prototype was (never the newest use)
        h.Over(36, 50);
        bool allAway = h.Until(() => h.Settled() && CountPlacements(s.World, 500) == 0 && CountPlacements(s.World, 501) == 0 && reg != null &&
                                     reg.Get(1) != null && reg.Get(1).Pooled && reg.Get(19) != null && reg.Get(19).Pooled, 800);
        for (int k = 0; k < 10; k++) h.Round();
        water = reg != null ? reg.Get(1) : null;
        wmoWater = reg != null ? reg.Get(13) : null;
        magma = reg != null ? reg.Get(19) : null;
        long expected = 0;
        var arrays = new HashSet<WmvLiquidFrames>();
        foreach (WmvLiquidType t in new[] { water, wmoWater, magma })
            if (t != null && t.Frames != null && t.Frames.Array != null && arrays.Add(t.Frames))
                expected += t.Frames.GpuBytes;
        long poolBytes = s.LiquidPoolBytes;
        Check(allAway && water != null && water.Pooled && magma != null && magma.Pooled && wmoWater != null && wmoWater.Holders.Count == 1 &&
              wmoWater.Frames == water.Frames && expected > 0 && poolBytes == expected && expected == reg.ArrayBytes,
              "liquids: with the liquid tiles away the frames of a type held only by a pooled world-model prototype are counted in the pools -- " +
              "every array (" + poolBytes + " of " + expected + " bytes, " + (reg != null ? reg.ArrayBytes : -1) + " in arrays; type 13 held by " +
              (wmoWater != null ? wmoWater.Holders.Count : -1) + "; away " + allAway + ", " + s.StreamTiles + " tile(s) resident, water pooled " +
              (water != null && water.Pooled) + ", magma pooled " + (magma != null && magma.Pooled) + ")", log);
        long stampBefore = reg != null && reg.NextUse != null ? reg.NextUse() : 0;
        WmvTerrainSession.StreamPoolGpuBudget = 0;
        WmvTerrainSession.StreamPoolManagedBudget = 0;
        bool emptied = h.Until(() => reg != null && reg.TypeCount == 0 && WmvLiquidRegistry.LiveArrays == 0, 400);
        WmvTerrainSession.StreamPoolGpuBudget = 256L << 20;
        WmvTerrainSession.StreamPoolManagedBudget = 128L << 20;
        Check(emptied && wmoWater != null && wmoWater.Disposed && wmoWater.LastUse < stampBefore && s.LiquidPoolBytes == 0,
              "liquids: with no pool budget the pooled prototype is evicted and its type with it, stamped with the prototype's last use (" +
              (wmoWater != null ? wmoWater.LastUse + " < " + stampBefore : "no type 13") + "), not as the newest use; nothing liquid is left (" +
              (reg != null ? reg.Summary() : "-") + ")", log);
        s.Clear("test");
        Check(clean() && !s.Streaming && WorldTallies() == "0/0/0/0/0",
              "liquids: clearTerrain ends the session and takes every liquid away (" + LiquidTallies() + ")", log);
    }

    static void LiquidIpcTests(Action<string> log)
    {
        var go = new GameObject("LiquidIpcTest");
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            WmvIpcClient.TerrainTileCommand got = null;
            ipc.OnLoadTerrainTile = c => got = c;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":3,\"assetEpoch\":5,\"mapId\":0,\"mapDirectory\":\"Azeroth\",\"product\":\"wow\"," +
                                "\"build\":\"12.1.5.70448\",\"wdtFileDataID\":775971,\"wdtResolvedBy\":\"fileDataID\",\"tileX\":29,\"tileY\":48," +
                                "\"maxAssetBytes\":4194304,\"liquids\":{\"maxTexturesPerLoad\":256,\"maxTextureBytes\":8388608," +
                                "\"maxTextureBytesPerLoad\":67108864,\"catalog\":{\"product\":\"wow\",\"build\":\"12.1.5.70448\",\"state\":\"ok\",\"detail\":\"\"," +
                                "\"types\":[{\"id\":2,\"name\":\"Ocean\",\"kind\":1,\"material\":1,\"materialFlags\":1,\"lvf\":0,\"minimapColor\":2668798165," +
                                "\"float0\":1.5,\"float1\":30.0,\"frames\":[219901,219902],\"framesWanted\":30},{\"id\":406,\"name\":\"Grid\",\"kind\":0," +
                                "\"material\":-1,\"materialFlags\":-1,\"lvf\":-1,\"minimapColor\":0,\"float0\":0,\"float1\":0,\"frames\":[],\"framesWanted\":0}]}}}",
                                null);
            LiquidTypeView ocean = null, grid = null;
            bool parsed = got != null && got.Liquids != null && got.Liquids.Catalog.TryGetType(2, out ocean) && got.Liquids.Catalog.TryGetType(406, out grid);
            Check(parsed && got.Liquids.MaxTexturesPerLoad == 256 && got.Liquids.MaxTextureBytes == 8388608 && got.Liquids.MaxTextureBytesPerLoad == 67108864 &&
                  got.Liquids.CatalogState == "ok" && got.Liquids.Catalog.Count == 2 && ocean.Kind == 1 && ocean.MaterialFlags == 1 && ocean.Lvf == 0 &&
                  ocean.MinimapColor == 2668798165u && ocean.Float0 == 1.5f && ocean.Float1 == 30f && ocean.Frames.Length == 2 && ocean.Frames[1] == 219902u &&
                  ocean.FramesWanted == 30 && ocean.Name == "Ocean" && grid.Lvf == -1 && grid.MaterialFlags == -1 && grid.Frames.Length == 0,
                  "liquids ipc: loadTerrainTile's \"liquids\" is read whole -- the budgets and the catalogue (a u32 colour, -1 for a type without a " +
                  "material row, the frames in order) (" + (got != null ? "" + got.Liquids : "nothing") + ")", log);
            got = null;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":4,\"assetEpoch\":5,\"mapId\":0,\"wdtFileDataID\":775971,\"tileX\":29," +
                                "\"tileY\":48,\"maxAssetBytes\":4194304}", null);
            Check(got != null && got.Liquids == null && got.ToString().EndsWith("; no liquids offered", StringComparison.Ordinal),
                  "liquids ipc: a line without \"liquids\" (an older host) offers none", log);
            var state = new WmvIpcClient.StreamStateReport { Load = 1, LiquidTiles = 2, LiquidTypes = 3, LiquidTextures = 1, LiquidBytes = 4096 };
            string stateLine = WmvIpcClient.StreamStateLine(state);
            var sent = new List<string>();
            ipc.SentForTest = x => sent.Add(x);
            ipc.ReportRuntimeState(11, new WmvIpcClient.RuntimeReport
            {
                MountKey = "", LiquidObjects = 4, LiquidMeshes = 4, LiquidMaterials = 2, LiquidArrays = 1, LiquidGpuBytes = 123456,
            });
            string runtimeLine = sent.Count == 1 ? sent[0] : "";
            Check(stateLine.EndsWith(",\"liquidTiles\":2,\"liquidTypes\":3,\"liquidTextures\":1,\"liquidBytes\":4096}", StringComparison.Ordinal) &&
                  runtimeLine.EndsWith(",\"liquidObjects\":4,\"liquidMeshes\":4,\"liquidMaterials\":2,\"liquidArrays\":1,\"liquidGpuBytes\":123456}",
                                       StringComparison.Ordinal),
                  "liquids ipc: streamState and runtimeState carry the liquid counters under the contract's names", log);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }
}
