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
// prototype's child, cloned into every instance, culled by its own renderer, never counted as a group; Stage 10 step 6: bound
// to the outdoor light, its group renderers take the world-lit copies and its liquid renderers keep the registry's material);
// a streaming session
// (frames asked once for two tiles and a world model, a type pooled when its tile closes, evicted with no pool budget, asked
// again when it comes back; a frame array held only by a pooled prototype counted in the pool, its type evicted with the
// prototype as last used when the prototype was); the IPC lines (the offer read, the report, streamState, runtimeState) --
// and after each, every liquid object, mesh, material and array gone and every tally 0. The materials' kind, blending
// (premultiplied colour) and zone-path constants. THE ZONE LIGHTS (Stage 09 tuning): the offer's "lights" read (ok, absent,
// unavailable; malformed entries dropped), the scene's lights through the lifecycle (a single load's, a world's, a streaming
// session's; another map's ignored; dropped at the clear, the zone light off at once), the shader's globals (WmvLiquidLight:
// the first sample snaps, the ease, the pinned clock, once per frame, an invalid light off, no scene off), and the shader on
// the GPU (one quad per path rendered into floats against the contract's formulas: the zone path for water and for ocean
// without a stored depth, and the fallback). THE HIGH LOOK (Stage 09 tuning): the ripple maps through the lifecycle (fetched
// with the frames once per scene, checked as a unit normal map -- a file that is not one leaves flat normals --, uploaded
// linear, bound, counted and gone with the scene: a single tile, a world load's MLIQ, a streaming session), the materials'
// tune values, the IPC's "bump" and "sky", the reflection sky's global, the HIGH path on the GPU (RenderToArray with synthetic
// grab textures bound only for the draw, orthographic so Ls is the depth below the water: the bed exactly at Ls 0, the
// transmission at Ls 1, 3.25 and 10, a flat normal reading its own pixel, an occluder in front keeping the unshifted pixel,
// Fresnel at normal incidence, a miss giving the sky, a hit the scene's colour) and the grab pass on Camera.main (it ran,
// its depth copy holds the scene, the switch is 0 afterwards, the Models view byte-identical with it and without).

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
    /// Magma (two frames, opaque); `second`: another client's, whose water has one other frame; `bump`: water and WMO water name
    /// the ripple map LqB1 (a unit normal map), the ocean LqB2 (a file that is not one).</summary>
    static WmvIpcClient.LiquidOffer LiquidTestOffer(bool second = false, bool bump = false)
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
        if (bump)
        {
            LiquidTypeView t;
            foreach (int id in new[] { 1, 13 })
                if (o.Catalog.TryGetType(id, out t))
                    t.Bump = LqB1;
            if (o.Catalog.TryGetType(2, out t))
                t.Bump = LqB2;
            o.BumpIds = 3;
        }
        return o;
    }

    /// <summary>Zone lights as the host sends them for map `map` (Wow/World/LiquidLights.cs): zone 140 (x 200..300, y 0..100) whose
    /// Light 7 has no values, then zone 139 (x and y 0..100) -> Light 204 with Durotar's values at 12:00 (params 5985), and the map
    /// default Light 191 with Kalimdor's (params 192).</summary>
    static LiquidLights LiquidTestLights(int map)
    {
        var l = new LiquidLights { Map = map, Time = 1440, Slot = 0, DefaultLight = 191, DefaultFrom = "end0", Product = "test", Build = "1" };
        l.AddZone(140, 7, -64000, 64000, 0, new double[] { 200, 0, 300, 0, 300, 100, 200, 100 });
        l.AddZone(139, 204, -64000, 64000, 0, new double[] { 0, 0, 100, 0, 100, 100, 0, 100 });
        l.AddValues(204, 5985, new float[] { 64, 62, 35 }, new float[] { 62, 54, 47 }, new float[] { 82, 90, 94 }, new float[] { 55, 61, 69 },
                    new[] { 0.4f, 0.8f }, new[] { 0.75f, 1f });
        l.AddValues(191, 192, new float[] { 88, 84, 69 }, new float[] { 43, 67, 81 }, new float[] { 16, 65, 77 }, new float[] { 0, 29, 41 },
                    new[] { 0.5f, 1f }, new[] { 0.75f, 1f });
        return l;
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
        // The High look installed for the tests, as the player has it (WmvMain installs it at start unless WMV_WATER=low): a
        // transparent water or ocean type holds its ripple map and writes depth. The pass's own two materials are left out of
        // the material tallies (they come and go with its install).
        bool grabWas = WmvWaterGrabPass.Active;
        bool grabOn = WmvWaterGrabPass.SetActive(true);
        int meshes0 = Meshes(), materials0 = Materials() - WmvWaterGrabPass.MaterialCount, arrays0 = Arrays();
        var views = new List<TestWorldView>();
        long poolGpu = WmvTerrainSession.StreamPoolGpuBudget, poolManaged = WmvTerrainSession.StreamPoolManagedBudget;
        Check(LiquidTallies() == "0/0/0/0/0/0" && grabOn, "liquids: nothing of the liquids is held before the tests (" + LiquidTallies() + "), and the " +
                                                          "High look is installed for them (" + grabOn + ")", log);
        Func<bool> clean = () => LiquidTallies() == "0/0/0/0/0/0" && Meshes() == meshes0 && Materials() - WmvWaterGrabPass.MaterialCount == materials0 &&
                                 Arrays() == arrays0 && WmvRuntimeTerrainTile.Live == 0;
        try
        {
            LiquidTileTests(log, clean);
            LiquidWorldModelTests(log, views, clean);
            LiquidStreamingTests(log, views, clean);
            LiquidIpcTests(log);
            LiquidZoneLightTests(log, clean);
            LiquidBumpTests(log, views, clean);
            LiquidHighLookHoldTests(log, clean);
            LiquidHighTests(log, clean);
        }
        catch (Exception e)
        {
            Check(false, "liquids: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            WmvLiquidLight.Off();
            WmvWaterGrabPass.ForceForTest = false;
            WmvWaterGrabPass.SetActive(grabWas);
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
        Check(water != null && water.Material.renderQueue == WmvLiquidMaterials.TransparentQueue && water.Material.GetFloat(WmvLiquidMaterials.ZWriteId) == 1f &&
              water.Material.GetFloat(WmvLiquidMaterials.ModeId) == WmvLiquidMaterials.ModeTransparent &&
              water.Material.GetFloat(WmvLiquidMaterials.CullId) == (float)UnityEngine.Rendering.CullMode.Off &&
              magma != null && magma.Material.renderQueue == WmvLiquidMaterials.OpaqueQueue && magma.Material.GetFloat(WmvLiquidMaterials.ZWriteId) == 1f &&
              magma.Material.GetFloat(WmvLiquidMaterials.ModeId) == WmvLiquidMaterials.ModeOpaque &&
              water.Material.GetVector(WmvLiquidMaterials.BaseColorId) == WmvLiquidMaterials.BaseColour(0x9F12A0D5) &&
              WmvLiquidMaterials.TransparentQueue == 2999,
              "liquids: water (material flag 0x1) is blended after the opaque world and before the M2 transparents (queue 2999, both " +
              "sides; depth written while the High look is installed: its output is opaque), magma opaque (queue 2000, depth write); the " +
              "base colour is the minimap colour as stored", log);
        Func<Material, float, float, float, float, bool> blend = (m, s, d, sa, da) =>
            m != null && m.GetFloat(WmvLiquidMaterials.SrcBlendId) == s && m.GetFloat(WmvLiquidMaterials.DstBlendId) == d &&
            m.GetFloat(WmvLiquidMaterials.SrcBlendAlphaId) == sa && m.GetFloat(WmvLiquidMaterials.DstBlendAlphaId) == da;
        const float One = (float)UnityEngine.Rendering.BlendMode.One, Zero = (float)UnityEngine.Rendering.BlendMode.Zero,
                    SrcA = (float)UnityEngine.Rendering.BlendMode.SrcAlpha, InvSrcA = (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
        Check(water != null && magma != null && ocean != null && blend(water.Material, One, InvSrcA, SrcA, InvSrcA) &&
              blend(ocean.Material, One, InvSrcA, SrcA, InvSrcA) && blend(magma.Material, One, Zero, One, Zero) &&
              water.Material.GetFloat(WmvLiquidMaterials.KindId) == 0f && ocean.Material.GetFloat(WmvLiquidMaterials.KindId) == 1f &&
              magma.Material.GetFloat(WmvLiquidMaterials.KindId) == 2f && water.Material.GetFloat(WmvLiquidMaterials.DepthFullId) == 3.25f &&
              water.Material.GetFloat(WmvLiquidMaterials.ZoneHighlightId) == 0.34f && water.Material.GetFloat(WmvLiquidMaterials.ZoneHighlightFloorId) == 0.13f &&
              water.Material.GetFloat(WmvLiquidMaterials.BedFactorId) == 0.75f,
              "liquids: the transparent materials blend premultiplied (colour One / OneMinusSrcAlpha, alpha SrcAlpha / OneMinusSrcAlpha), the " +
              "opaque ones One / Zero for both; _LiquidKind is the type's kind (water 0, ocean 1, magma 2); the zone path's constants (depth " +
              "full 3.25 yd, highlight 0.34 above 0.13, bed 0.75)", log);
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

    /// <summary>A world of tile (32, 50) without MH2O whose obj0 places the new-form liquid world model twice and the old-form
    /// one once (LiquidWmoRoot / LiquidWmoGroup), with the five frames.</summary>
    static WorldFiles LiquidWorldFiles()
    {
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
        return files;
    }

    static void LiquidWorldModelTests(Action<string> log, List<TestWorldView> views, Func<bool> clean)
    {
        // ---- a world model's MLIQ: a child of its group in the prototype, cloned into every instance, culled by its own bounds;
        // a new-form group (MOHD 0x4, type 13) and an old-form one (the constant 15, nibble 0: water, 13 by the documented rule)
        WorldFiles files = LiquidWorldFiles();
        var h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        var v = new TestWorldView();
        views.Add(v);
        h.Session.WorldView = v;
        WmvIpcClient.TerrainTileCommand c = WorldCommand(10, false);
        c.Liquids = LiquidTestOffer();
        c.Liquids.Lights = LiquidTestLights(c.MapId);
        c.Liquids.LightsState = "ok";
        h.Session.Load(c);
        files.Serve(h, 10);
        WmvIpcClient.TerrainTileReport r = h.For(10);
        Check(r != null && r.Status == "built" && h.Session.SceneLights == c.Liquids.Lights,
              "liquids: a world load's zone lights are the scene's from its commit", log);
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
        if (!WmvWorldLight.HardOff && scene != null && wmoWater != null && scene.Instances.Count > 0)
            LiquidWorldLitTests(log, v, scene, wmoWater.Material);
        h.Session.Clear("test");
        Check(clean() && WorldTallies() == "0/0/0/0/0" && h.Session.SceneLights == null && WmvWorldModels.LiveCopies == 0,
              "liquids: clearTerrain takes the world models' liquid away with their prototypes, and the scene's zone lights (" + LiquidTallies() +
              ", world " + WorldTallies() + ")", log);
    }

    /// <summary>
    /// Stage 10 step 6 (T15 of the outdoor-light cases): the liquid world models bound to the outdoor light (WmvWorldModels.RebindWorldLit, the
    /// test driving it): every group renderer holds a copy on "WMV/Map Object (world)", every liquid renderer -- a child of its
    /// group's object, interleaved in the renderers' index order -- keeps the registry's material `liquid`; a clone placed
    /// while bound holds what its prototype's first instance holds, renderer by renderer; RebindWorldLit(false) puts every
    /// group renderer back on its original (the clear that follows returns every tally to its start).
    /// </summary>
    static void LiquidWorldLitTests(Action<string> log, TestWorldView v, WmvWorldScene scene, Material liquid)
    {
        var bind = new WmvWorldLitBind { Lit = true };
        v.Models.RebindWorldLit(true, bind);
        int groupsLit = 0, liquidsKept = 0, wrong = 0, interleaved = 0;
        foreach (WmvWorldInstance i in scene.Instances)
            for (int k = 0; k < i.Renderers.Length; k++)
            {
                Renderer rr = i.Renderers[k];
                bool isLiquid = i.LiquidRenderers != null && i.LiquidRenderers[k];
                if (isLiquid)
                {
                    if (rr.sharedMaterial == liquid) liquidsKept++;
                    else wrong++;
                    if (k > 0 && (i.LiquidRenderers == null || !i.LiquidRenderers[k - 1])) interleaved++;
                }
                else if (rr.sharedMaterial != null && rr.sharedMaterial.shader.name == WmvWorldLitMaterials.WorldModelShaderName + WmvWorldLitMaterials.Suffix)
                    groupsLit++;
                else
                    wrong++;
            }
        WmvWorldInstance first = scene.Instances[0];
        WmvWorldInstance clone = v.PlaceInstance(first.Prototype, first.Placement, scene.Origin, scene.Load);
        bool sameByIndex = clone.Renderers.Length == first.Renderers.Length;
        for (int k = 0; sameByIndex && k < clone.Renderers.Length; k++)
        {
            Material[] a = clone.Renderers[k].sharedMaterials, b = first.Renderers[k].sharedMaterials;
            sameByIndex = a.Length == b.Length;
            for (int m = 0; sameByIndex && m < a.Length; m++)
                sameByIndex = a[m] == b[m];
        }
        v.DisposeInstance(clone);
        var unbind = new WmvWorldLitBind { Lit = false };
        v.Models.RebindWorldLit(false, unbind);
        int backOnOwn = 0;
        foreach (WmvWorldInstance i in scene.Instances)
            for (int k = 0; k < i.Renderers.Length; k++)
            {
                bool isLiquid = i.LiquidRenderers != null && i.LiquidRenderers[k];
                Material m = i.Renderers[k].sharedMaterial;
                if (isLiquid ? m == liquid : m != null && m.shader.name == WmvWorldLitMaterials.WorldModelShaderName)
                    backOnOwn++;
            }
        Check(groupsLit == 3 && liquidsKept == 3 && wrong == 0 && interleaved == 3 && sameByIndex && unbind.Unbound == 3 && backOnOwn == 6 &&
              bind.GroupRenderers == 3 && WmvWorldModels.LiveCopies == 2,
              "liquids T15: the liquid world models bound to the outdoor light -- the 3 group renderers hold copies on \"WMV/Map Object (world)\" " +
              "(one per prototype: " + WmvWorldModels.LiveCopies + "), the 3 liquid renderers, each right after its group's in the index order, keep " +
              "the registry's material; a clone placed while bound matches its prototype's first instance renderer by renderer; RebindWorldLit(false) " +
              "puts the 3 group renderers back (" + groupsLit + " lit, " + liquidsKept + " liquids kept, " + wrong + " wrong, " + backOnOwn + " of 6 " +
              "on their own after)", log);
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
        c.Liquids.Lights = LiquidTestLights(c.MapId);
        c.Liquids.LightsState = "ok";
        LiquidLights entryLights = c.Liquids.Lights;
        s.Load(c);
        bool settled = h.Until(() => h.H.For(20) != null && h.Settled() && s.StreamTiles == 9);
        for (int k = 0; k < 20; k++) h.Round();
        bool lightsKept = s.SceneLights == entryLights;
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
        Check(lightsKept && s.SceneLights == entryLights,
              "liquids: a streaming session's zone lights are its entry's from the commit, and stay the scene's while its tiles open and close", log);

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
        Check(clean() && !s.Streaming && WorldTallies() == "0/0/0/0/0" && s.SceneLights == null,
              "liquids: clearTerrain ends the session and takes every liquid away, and the zone lights (" + LiquidTallies() + ")", log);
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
            Check(parsed && got.Liquids.Lights == null && got.Liquids.LightsState == "" && got.Liquids.ToString().EndsWith("; no lights", StringComparison.Ordinal),
                  "liquids ipc: an offer without \"lights\" (an older host) has no zone lights: the type colours", log);

            // The zone lights inside the offer: a zone of 2 points and one of an odd coordinate count dropped, a value row with a short
            // colour dropped; the rest read as sent.
            string head = "{\"type\":\"loadTerrainTile\",\"load\":5,\"assetEpoch\":5,\"mapId\":1,\"wdtFileDataID\":782779,\"tileX\":40,\"tileY\":30," +
                          "\"maxAssetBytes\":4194304,\"liquids\":{\"maxTexturesPerLoad\":256,\"maxTextureBytes\":8388608,\"maxTextureBytesPerLoad\":67108864," +
                          "\"catalog\":{\"product\":\"wow\",\"build\":\"12.1.5.70448\",\"state\":\"ok\",\"detail\":\"\",\"types\":[]},\"lights\":";
            got = null;
            ipc.FeedLineForTest(head + "{\"state\":\"ok\",\"detail\":\"\",\"product\":\"wow\",\"build\":\"12.1.5.70448\",\"map\":1,\"time\":1440," +
                                "\"slot\":0,\"defaultLight\":191,\"defaultFrom\":\"end0\",\"zones\":[{\"zone\":139,\"light\":204,\"zmin\":-64000.0," +
                                "\"zmax\":64000.0,\"conditional\":0,\"poly\":[0,0,100,0,100,100,0,100]},{\"zone\":1,\"light\":1,\"zmin\":0,\"zmax\":1," +
                                "\"conditional\":0,\"poly\":[0,0,1,1]},{\"zone\":2,\"light\":1,\"zmin\":0,\"zmax\":1,\"conditional\":1,\"poly\":[0,0,1,0,1]}]," +
                                "\"points\":[{\"light\":2677,\"x\":1547.22,\"y\":-4162.36,\"z\":47.75,\"start\":60.4,\"end\":76.79}],\"values\":[{\"light\":204," +
                                "\"lightParams\":5985,\"riverClose\":[64,62,35],\"riverFar\":[62,54,47],\"oceanClose\":[82,90,94],\"oceanFar\":[55,61,69]," +
                                "\"waterAlpha\":[0.40,0.80],\"oceanAlpha\":[0.75,1.00]},{\"light\":191,\"lightParams\":192,\"riverClose\":[88,84]," +
                                "\"riverFar\":[43,67,81],\"oceanClose\":[16,65,77],\"oceanFar\":[0,29,41],\"waterAlpha\":[0.5,1],\"oceanAlpha\":[0.75,1]}]}}}",
                                null);
            LiquidLights lights = got != null && got.Liquids != null ? got.Liquids.Lights : null;
            LiquidLightValues v204 = null;
            LiquidLightSample at = lights != null ? lights.Evaluate(50, 50, 0) : null;
            Check(lights != null && got.Liquids.LightsState == "ok" && lights.Map == 1 && lights.Time == 1440 && lights.Slot == 0 && lights.DefaultLight == 191 &&
                  lights.DefaultFrom == "end0" && lights.Build == "12.1.5.70448" && lights.Zones.Count == 1 && lights.DroppedZones == 2 &&
                  lights.Zones[0].Zone == 139 && lights.Zones[0].PointCount == 4 && lights.Points.Count == 1 && Math.Abs(lights.Points[0].End - 76.79) < 1e-9 &&
                  lights.ValueCount == 1 && lights.DroppedValues == 1 && lights.TryGetValues(204, out v204) && v204.LightParams == 5985 &&
                  Near((float)v204.V[LiquidLightSample.RiverFar + 2], 47f / 255f) && Near((float)v204.V[LiquidLightSample.WaterAlpha], 0.4f) &&
                  at != null && at.Valid && at.Light == 204 && got.Liquids.ToString().Contains("; lights ok, map 1 (wow 12.1.5.70448)"),
                  "liquids ipc: the offer's \"lights\" are read (map 1, noon, slot 0, default 191 from end0; zone 139 -> 204 with its values; a " +
                  "point light); a zone of 2 points, one of an odd coordinate count and a value row with a short colour are dropped and counted (" +
                  (lights != null ? lights.Summary() : "no lights") + ")", log);
            // The High look's additive keys: a type's "bump" (absent: 0) and a light's "sky" (absent: none, the row kept).
            got = null;
            ipc.FeedLineForTest(head.Replace("\"types\":[]", "\"types\":[{\"id\":5,\"name\":\"Slow Water\",\"kind\":0,\"material\":1," +
                                             "\"materialFlags\":1,\"lvf\":0,\"minimapColor\":2668798165,\"float0\":1,\"float1\":0,\"frames\":[219901]," +
                                             "\"framesWanted\":30,\"bump\":463849},{\"id\":2,\"name\":\"Ocean\",\"kind\":1,\"material\":1," +
                                             "\"materialFlags\":1,\"lvf\":0,\"minimapColor\":2668798165,\"float0\":1,\"float1\":0,\"frames\":[]," +
                                             "\"framesWanted\":0}]") +
                                "{\"state\":\"ok\",\"detail\":\"\",\"product\":\"wow\",\"build\":\"12.1.5.70448\",\"map\":1,\"time\":1440," +
                                "\"slot\":0,\"defaultLight\":191,\"defaultFrom\":\"end0\",\"zones\":[],\"points\":[],\"values\":[{\"light\":204," +
                                "\"lightParams\":5985,\"riverClose\":[64,62,35],\"riverFar\":[62,54,47],\"oceanClose\":[82,90,94],\"oceanFar\":[55,61,69]," +
                                "\"waterAlpha\":[0.40,0.80],\"oceanAlpha\":[0.75,1.00],\"sky\":[102,153,204]},{\"light\":191,\"lightParams\":192," +
                                "\"riverClose\":[88,84,69],\"riverFar\":[43,67,81],\"oceanClose\":[16,65,77],\"oceanFar\":[0,29,41]," +
                                "\"waterAlpha\":[0.5,1],\"oceanAlpha\":[0.75,1]}]}}}", null);
            LiquidTypeView slow = null, plain = null;
            LiquidLightValues sky204 = null, sky191 = null;
            bool read = got != null && got.Liquids != null && got.Liquids.Catalog.TryGetType(5, out slow) && got.Liquids.Catalog.TryGetType(2, out plain) &&
                        got.Liquids.Lights != null && got.Liquids.Lights.TryGetValues(204, out sky204) && got.Liquids.Lights.TryGetValues(191, out sky191);
            Check(read && slow.Bump == 463849u && plain.Bump == 0u && got.Liquids.BumpIds == 1 && got.Liquids.ToString().Contains(", 1 type(s) with a ripple map)") &&
                  sky204.HasSky && Near((float)sky204.V[LiquidLightSample.Sky], 0.4f) && Near((float)sky204.V[LiquidLightSample.Sky + 2], 0.8f) && !sky191.HasSky &&
                  got.Liquids.Lights.ValueCount == 2 && got.Liquids.Lights.DroppedValues == 0,
                  "liquids ipc: the High look's additive keys are read -- a type's \"bump\" (463849; absent: 0) and a light's \"sky\" ((102, 153, 204) / " +
                  "255; absent: none, the row kept) (" + (got != null ? "" + got.Liquids : "nothing") + ")", log);
            got = null;
            ipc.FeedLineForTest(head + "{\"state\":\"unavailable\",\"detail\":\"test: the light tables are not read on this client\",\"map\":1," +
                                "\"zones\":[{\"zone\":139,\"light\":204,\"zmin\":0,\"zmax\":1,\"conditional\":0,\"poly\":[0,0,1,0,1,1]}]}}}", null);
            Check(got != null && got.Liquids != null && got.Liquids.Lights == null && got.Liquids.LightsState == "unavailable" &&
                  got.Liquids.LightsDetail == "test: the light tables are not read on this client" &&
                  got.Liquids.ToString().EndsWith("; lights unavailable: test: the light tables are not read on this client", StringComparison.Ordinal),
                  "liquids ipc: \"lights\" whose state is not \"ok\" give none (the type colours), the state and why kept for the log", log);
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

    /// <summary>The liquid shader's output over a cleared target: one 8 x 8 yd quad at height 50 drawn with `m`, uv1 = `uv1` at
    /// every vertex, rendered top-down into 4 x 4 floats (WmvTerrainComposite.RenderToArray: no camera, _WmvShaderEncode 0);
    /// pixel (1, 1)'s rgba, or null with why. Over 0 the blend leaves the fragment's rgb (premultiplied One) and its alpha squared
    /// (SrcAlpha on the alpha channel).</summary>
    static float[] LiquidQuadRender(Material m, Vector2 uv1, out string why)
    {
        why = null;
        var mesh = new Mesh { name = "WmvLiquid GPU test quad" };
        try
        {
            mesh.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, 8f), new Vector3(0f, 50f, 8f) });
            mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
            mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
            mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();
            float[] px = WmvTerrainComposite.RenderToArray(cmd => cmd.DrawMesh(mesh, Matrix4x4.identity, m, 0, 0), 0f, 8f, 8f, 8f, 40f, 60f, 4, 4,
                                                           false, false);
            int at = (1 * 4 + 1) * 4;
            return new[] { px[at], px[at + 1], px[at + 2], px[at + 3] };
        }
        catch (Exception e)
        {
            why = e.GetType().Name + ": " + e.Message;
            return null;
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(mesh);
        }
    }

    static bool NearRgba(float[] px, double r, double g, double b, double a, double eps = 3e-3)
    {
        return px != null && Math.Abs(px[0] - r) <= eps && Math.Abs(px[1] - g) <= eps && Math.Abs(px[2] - b) <= eps && Math.Abs(px[3] - a) <= eps;
    }

    static string Rgba(float[] px)
    {
        return px == null ? "none" : string.Format(System.Globalization.CultureInfo.InvariantCulture, "({0:0.0000}, {1:0.0000}, {2:0.0000}, {3:0.0000})",
                                                   px[0], px[1], px[2], px[3]);
    }

    /// <summary>The zone lights (Stage 09 tuning): the scene's lights through the session's lifecycle, the shader's globals
    /// (WmvLiquidLight: set, eased, snapped, once per frame, off at once when invalid or when the scene goes), and the shader's two
    /// paths on the GPU against the contract's formulas.</summary>
    static void LiquidZoneLightTests(Action<string> log, Func<bool> clean)
    {
        int zoneLight = WmvLiquidLight.ZoneLightId;
        Func<int, float> river = k => Shader.GetGlobalVector(WmvLiquidLight.RiverCloseId)[k];

        // ---- a single load: its lights are the scene's from the commit; the camera's light sets the globals; the clear drops both
        var h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        WorldFiles files = LiquidTileFiles();
        WmvIpcClient.TerrainTileCommand c = LiquidCommand(51);
        LiquidLights lights = LiquidTestLights(c.MapId);
        c.Liquids.Lights = lights;
        c.Liquids.LightsState = "ok";
        h.Session.Load(c);
        files.Serve(h, 51);
        WmvIpcClient.TerrainTileReport r = h.For(51);
        bool committed = r != null && r.Status == "built" && h.Session.SceneLights == lights;
        WmvLiquidLight.Sample(h.Session.SceneLights, 50, 50, 17.9, 0.016, true);
        Vector4 wa = Shader.GetGlobalVector(WmvLiquidLight.WaterAlphaId), oa = Shader.GetGlobalVector(WmvLiquidLight.OceanAlphaId),
                of = Shader.GetGlobalVector(WmvLiquidLight.OceanFarId), rf = Shader.GetGlobalVector(WmvLiquidLight.RiverFarId);
        bool lit = WmvLiquidLight.On && Shader.GetGlobalFloat(zoneLight) == 1f && Near(river(0), 64f / 255f) && Near(river(1), 62f / 255f) &&
                   Near(river(2), 35f / 255f) && Near(rf.z, 47f / 255f) && Near(of.x, 55f / 255f) && Near(wa.x, 0.4f) && Near(wa.y, 0.8f) &&
                   Near(oa.x, 0.75f) && Near(oa.y, 1f) && WmvLiquidLight.LastLine == "liquid light: zone 139 light 204 params 5985 (map 0)";
        Check(committed && lit,
              "zone lights: a load's lights are the scene's from its commit; at the camera (x 50, y 50) zone 139 -> light 204 sets the globals " +
              "(_WmvZoneLight 1, river (64,62,35)/(62,54,47), ocean far (55,61,69), alphas 0.40/0.80 and 0.75/1.00) and says so once (" +
              (WmvLiquidLight.LastLine ?? "no line") + ")", log);
        h.Session.Clear("test");
        Check(h.Session.SceneLights == null && !WmvLiquidLight.On && Shader.GetGlobalFloat(zoneLight) == 0f && clean(),
              "zone lights: clearTerrain drops the scene's lights and turns the zone light off at once (_WmvZoneLight 0)", log);

        // ---- another map's lights are ignored; lights "unavailable" give none
        c = LiquidCommand(52);
        c.Liquids.Lights = LiquidTestLights(c.MapId + 1);
        c.Liquids.LightsState = "ok";
        h.Session.Load(c);
        LiquidTileFiles().Serve(h, 52);
        bool otherMap = h.For(52) != null && h.For(52).Status == "built" && h.Session.SceneLights == null;
        h.Session.Clear("test");
        c = LiquidCommand(53);
        c.Liquids.LightsState = "unavailable";
        c.Liquids.LightsDetail = "test: no light tables";
        h.Session.Load(c);
        LiquidTileFiles().Serve(h, 53);
        bool unavailable = h.For(53) != null && h.For(53).Status == "built" && h.Session.SceneLights == null;
        h.Session.Clear("test");
        Check(otherMap && unavailable && clean(),
              "zone lights: lights of another map than the load's are ignored, and lights \"unavailable\" give none (the type colours)", log);

        // ---- the ease through the globals: a first sample snaps, then 1 - exp(-dt / 0.25 s) a frame, a pinned clock snaps; once per
        // frame; a base light without values off at once; no scene off
        WmvLiquidLight.Off();
        LiquidLights l = LiquidTestLights(0);
        WmvLiquidLight.Sample(l, 101, 50, 0, 0.016, false);        // outside zone 139: the map default 191 (river close R 88)
        bool first = WmvLiquidLight.On && Near(river(0), 88f / 255f) && WmvLiquidLight.Ease.Light == 191 && WmvLiquidLight.Ease.Zone == 0;
        WmvLiquidLight.Sample(l, 50, 50, 0, 0.25, false);          // 51 yd on, into zone 139 (R 64): eased
        float want = (float)(88.0 / 255 + (64.0 - 88.0) / 255 * (1 - Math.Exp(-1)));
        bool eased = Near(river(0), want);
        WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, true);          // the clock pinned: snapped
        bool pinned = Near(river(0), 64f / 255f);
        const int F = -1000;                                       // frame numbers Time.frameCount never takes
        WmvLiquidLight.Frame(F, 0.016, l, 250, 50, 0, true);       // zone 140: light 7 has no values -- off at once
        bool invalidOff = !WmvLiquidLight.On && Shader.GetGlobalFloat(zoneLight) == 0f;
        WmvLiquidLight.Frame(F, 0.016, l, 50, 50, 0, true);        // the same frame again: no second step
        bool once = !WmvLiquidLight.On;
        WmvLiquidLight.Frame(F + 1, 0.016, l, 50, 50, 0, true);    // the next frame: valid again, snapped
        bool again = WmvLiquidLight.On && Shader.GetGlobalFloat(zoneLight) == 1f && Near(river(0), 64f / 255f);
        WmvLiquidLight.Frame(F + 2, 0.016, null, 0, 0, 0, false);  // no scene
        bool noScene = !WmvLiquidLight.On && Shader.GetGlobalFloat(zoneLight) == 0f;
        Check(first && eased && pinned && invalidOff && once && again && noScene,
              "zone lights: the globals snap on the first sample (the map default 191), ease by 1 - exp(-dt / 0.25 s) (R 88 -> " +
              (want * 255f).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + " in 0.25 s), snap while the clock is pinned, " +
              "turn off at once for a base light without values, step once per frame, and are off with no scene (" + first + ", " + eased + ", " +
              pinned + ", " + invalidOff + ", " + once + ", " + again + ", " + noScene + ")", log);

        // ---- the shader on the GPU. Frame alpha 153/255 = 0.6, the depth byte 14 on the W scale (D = 14.5 / 9 = 1.6111 yd), Durotar's
        // light. Zone path, water: k = D / 3.25 = 0.4957, a = 0.4 + 0.4 k = 0.5983, h = 0.34 (0.6 - 0.13) / 0.87 = 0.1837,
        // rgb = a lerp((64,62,35), (62,54,47), k) / 255 + h = (0.3315, 0.3198, 0.2798), alpha (1 - 0.75 (1 - a))^2 = 0.6987^2 = 0.4882.
        // Zone path, ocean with no stored depth (uv1 (1, -1): k = 1): rgb = (55,61,69) / 255 + h = (0.3994, 0.4229, 0.4543), alpha 1.
        // Fallback (the zone light off), water: c = saturate((18,160,213) / 255 + 0.6 x 0.5) = (0.3706, 0.9275, 1), a = 0.45 + 0.35 x
        // 14 / 255 = 0.4692, rgb = c a = (0.1739, 0.4352, 0.4692), alpha a^2 = 0.2202.
        Shader shader = WmvLiquidMaterials.ResolveShader(null);
        Texture2DArray frames = null;
        Material water = null, ocean = null;
        float[] zoneWater = null, zoneOcean = null, fallback = null;
        string why = shader == null ? "no liquid shader" : null, why2 = null, why3 = null;
        double fa = 153 / 255.0, h0 = WmvLiquidMaterials.ZoneHighlight * Math.Min(1.0, Math.Max(0.0, (fa - WmvLiquidMaterials.ZoneHighlightFloor) /
                                                                                              (1 - WmvLiquidMaterials.ZoneHighlightFloor)));
        double d14 = LiquidFormat.DepthYardsW(14), k14 = Math.Min(1.0, d14 / WmvLiquidMaterials.DepthFull), a14 = 0.4 + (0.8 - 0.4) * k14;
        Func<double, double, double> zone = (close, far) => a14 * (close + (far - close) * k14) / 255 + h0;
        double out14 = 1 - WmvLiquidMaterials.BedFactor * (1 - a14);
        double fbA = WmvLiquidMaterials.ShallowAlpha + (WmvLiquidMaterials.DeepAlpha - WmvLiquidMaterials.ShallowAlpha) * (14 / 255.0);
        Func<double, double> fb = baseByte => Math.Min(1.0, baseByte / 255 + fa * WmvLiquidMaterials.HighlightScale) * fbA;
        try
        {
            if (shader != null)
            {
                frames = new Texture2DArray(4, 4, 1, TextureFormat.RGBA32, false, true) { name = "WmvLiquid GPU test frame" };
                var texels = new Color32[16];
                for (int k = 0; k < texels.Length; k++)
                    texels[k] = new Color32(0, 0, 0, 153);
                frames.SetPixels32(texels, 0);
                frames.Apply(false, false);
                water = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1,
                                                                                       Lvf = 0, MinimapColor = 0x9F12A0D5 });
                ocean = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 2, Name = "Ocean", Kind = 1, Material = 1, MaterialFlags = 1,
                                                                                       Lvf = 2, MinimapColor = 0x9F12A0D5 });
                foreach (Material m in new[] { water, ocean })
                {
                    m.SetTexture(WmvLiquidMaterials.FramesId, frames);
                    m.SetFloat(WmvLiquidMaterials.FrameCountId, 1f);
                    m.SetFloat(WmvLiquidMaterials.FrameId, 0f);
                    m.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
                }
                var depth14 = new Vector2(14f / 255f, (float)d14);
                WmvLiquidLight.Sample(LiquidTestLights(0), 50, 50, 0, 0.016, true);
                zoneWater = LiquidQuadRender(water, depth14, out why);
                zoneOcean = LiquidQuadRender(ocean, new Vector2(1f, -1f), out why2);
                WmvLiquidLight.Off();
                fallback = LiquidQuadRender(water, depth14, out why3);
            }
        }
        finally
        {
            WmvLiquidLight.Off();
            if (water != null) UnityEngine.Object.DestroyImmediate(water);
            if (ocean != null) UnityEngine.Object.DestroyImmediate(ocean);
            if (frames != null) UnityEngine.Object.DestroyImmediate(frames);
        }
        Check(NearRgba(zoneWater, zone(64, 62), zone(62, 54), zone(35, 47), out14 * out14),
              "zone lights on the GPU: water, the zone path (depth byte 14 W scale = 1.611 yd, frame alpha 0.6) writes a W + h premultiplied and " +
              "1 - 0.75 (1 - a) -- expected " + Rgba(new[] { (float)zone(64, 62), (float)zone(62, 54), (float)zone(35, 47), (float)(out14 * out14) }) +
              ", got " + Rgba(zoneWater) + (why != null ? " (" + why + ")" : ""), log);
        Check(NearRgba(zoneOcean, 55 / 255.0 + h0, 61 / 255.0 + h0, 69 / 255.0 + h0, 1.0),
              "zone lights on the GPU: ocean takes the ocean pair, and no stored depth (uv1 (1, -1)) is deep -- far + h, alpha 1 (got " +
              Rgba(zoneOcean) + (why2 != null ? ", " + why2 : "") + ")", log);
        Check(NearRgba(fallback, fb(18), fb(160), fb(213), fbA * fbA),
              "zone lights on the GPU: with the zone light off the water keeps the type colours exactly, premultiplied -- expected " +
              Rgba(new[] { (float)fb(18), (float)fb(160), (float)fb(213), (float)(fbA * fbA) }) + ", got " + Rgba(fallback) +
              (why3 != null ? " (" + why3 + ")" : ""), log);
        Check(clean(), "zone lights: nothing of these tests is left (" + LiquidTallies() + ")", log);
    }

    // ================================================================ the High look (Stage 09 tuning)

    const uint LqB1 = 880031, LqB2 = 880032;
    static readonly int InvVpId = Shader.PropertyToID("unity_MatrixInvVP");

    /// <summary>NormalBlp's two texels (R, G, B): the unit normals about (0.6, 0, 0.8) and (-0.3, 0.15, 0.94) as (n + 1) / 2
    /// bytes -- slopes that spread about a mean tilt (LiquidBump), the one the shader takes out.</summary>
    static readonly byte[][] NormalBlpTexels = { new byte[] { 204, 128, 230 }, new byte[] { 89, 147, 247 } };

    /// <summary>A raw BGRA BLP (encoding 3, the full mip chain) of NormalBlpTexels in a checkerboard -- a unit normal map whose
    /// slopes spread (LiquidBump) about a mean tilt.</summary>
    static byte[] NormalBlp(int w, int h)
    {
        var mips = new List<byte[]>();
        for (int m = 0; m < Levels(w, h); m++)
        {
            int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m);
            var d = new byte[mw * mh * 4];
            for (int k = 0; k < mw * mh; k++)
            {
                byte[] n = NormalBlpTexels[(k % mw + k / mw) % 2];
                d[4 * k] = n[2];                // B (the normal's z)
                d[4 * k + 1] = n[1];            // G (y)
                d[4 * k + 2] = n[0];            // R (x)
                d[4 * k + 3] = 255;
            }
            mips.Add(d);
        }
        return Blp(3, 8, 0, w, h, null, mips);
    }

    /// <summary>NormalBlp's slope (x / z, y / z) of texel `which` (0 or 1).</summary>
    static double[] NormalBlpSlope(int which)
    {
        Func<double, double> dec = b => b / 255.0 * 2.0 - 1.0;
        byte[] n = NormalBlpTexels[which];
        return new[] { dec(n[0]) / dec(n[2]), dec(n[1]) / dec(n[2]) };
    }

    /// <summary>NormalBlp's slope spread: half the two slopes' difference (LiquidBump.SlopeSpread on its 8 x 8 top mip, 32 of
    /// each).</summary>
    static double NormalBlpSpread()
    {
        double[] a = NormalBlpSlope(0), b = NormalBlpSlope(1);
        double dx = (a[0] - b[0]) / 2.0, dy = (a[1] - b[1]) / 2.0;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    /// <summary>NormalBlp's mean slope: the two slopes' average (32 of each).</summary>
    static double[] NormalBlpMean()
    {
        double[] a = NormalBlpSlope(0), b = NormalBlpSlope(1);
        return new[] { (a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0 };
    }

    /// <summary>The ripple maps of the bump offer (LiquidTestOffer(bump: true)): LqB1 a unit normal map, LqB2 colour noise.</summary>
    static void AddLiquidBumps(WorldFiles f)
    {
        f.Add(WmvTerrainSession.RoleLiquidTexture, LqB1, NormalBlp(8, 8));
        f.Add(WmvTerrainSession.RoleLiquidTexture, LqB2, RawBlp(77, 8, 8));
    }

    static int Textures2D() { return Resources.FindObjectsOfTypeAll<Texture2D>().Length; }

    /// <summary>The ripple maps (the High look): fetched under the load like the frames, one fetch per distinct file per scene,
    /// checked as a unit normal map (else flat), uploaded linear (Repeat, trilinear, the file's mips), bound on the types'
    /// materials, counted in the arrays' tallies and bytes and gone with the scene -- a single tile, a world load's MLIQ, a
    /// streaming session.</summary>
    static void LiquidBumpTests(Action<string> log, List<TestWorldView> views, Func<bool> clean)
    {
        string rl = WmvTerrainSession.RoleLiquidTexture;
        int textures0 = Textures2D();

        // ---- a single tile: water (type 1) names a unit normal map, the ocean (type 2) a file that is not one
        var h = new TerrainHarness();
        h.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        WorldFiles files = LiquidTileFiles();
        AddLiquidBumps(files);
        WmvIpcClient.TerrainTileCommand c = LiquidCommand(61);
        c.Liquids = LiquidTestOffer(false, true);
        h.Session.Load(c);
        files.Serve(h, 61);
        WmvIpcClient.TerrainTileReport r = h.For(61);
        WmvIpcClient.TerrainLiquidReport l = r != null ? r.Liquids : null;
        WmvLiquidRegistry reg = h.Session.SceneLiquids;
        WmvLiquidType water = reg != null ? reg.Get(1) : null, ocean = reg != null ? reg.Get(2) : null, magma = reg != null ? reg.Get(19) : null;
        Texture2D map = water != null && water.Bump != null ? water.Bump.BumpMap : null;
        double spread = NormalBlpSpread(), factor = LiquidBump.StrengthFactor(spread);
        Check(r != null && r.Status == "built" && l != null && l.TexturesRequested == 7 && l.TexturesLoaded == 7 && AskedFor(h, rl, LqB1) == 1 &&
              AskedFor(h, rl, LqB2) == 1 && water != null && water.HasBump && map != null &&
              water.Material.GetTexture(WmvLiquidMaterials.BumpMapId) == map && water.Material.GetFloat(WmvLiquidMaterials.HasBumpId) == 1f &&
              !UnityEngine.Experimental.Rendering.GraphicsFormatUtility.IsSRGBFormat(map.graphicsFormat) && map.wrapMode == TextureWrapMode.Repeat &&
              map.filterMode == FilterMode.Trilinear && map.mipmapCount == 4 && map.width == 8 && water.Bump.UnitShare == 1.0 && water.Bump.Sampled == 64,
              "high: a type's ripple map is fetched with its frames (7 files: 5 frames, 2 ripple maps, each asked once), checked as a unit normal " +
              "map (every texel of 64), uploaded as a linear Texture2D with the file's 4 levels, Repeat, trilinear, and bound on the material " +
              "(_BumpMap, _HasBump 1) (" + (l != null ? l.TexturesRequested + "/" + l.TexturesLoaded : "no report") + "; " +
              (map != null ? map.width + "x" + map.height + " " + map.graphicsFormat + ", " + map.mipmapCount + " level(s)" : "no map") + ")", log);
        Check(water != null && water.Bump != null && Math.Abs(water.Bump.SlopeSpread - spread) < 1e-9 && Math.Abs(water.Bump.StrengthFactor - factor) < 1e-12 &&
              factor < 1.0 && water.Material.GetFloat(WmvLiquidMaterials.HiBumpNormId) == (float)factor,
              string.Format(System.Globalization.CultureInfo.InvariantCulture,
                            "high (H7): the ripple map's slope spread is measured at upload ({0:0.0000}, expected {1:0.0000}) and its strength factor " +
                            "(the tuned map's {2} over it: {3:0.0000}) is the material's _HiBumpNorm ({4:0.0000}) -- a steeper map drawn gentler",
                            water != null && water.Bump != null ? water.Bump.SlopeSpread : -1, spread, LiquidBump.TunedSlopeSpread, factor,
                            water != null ? water.Material.GetFloat(WmvLiquidMaterials.HiBumpNormId) : -1f), log);
        double[] mean = NormalBlpMean();
        double meanShare = WmvLiquidMaterials.Tune[LiquidHighTune.BumpMean];
        var none4 = new Vector4(-9f, -9f, -9f, -9f);
        Vector4 boundMean = water != null ? water.Material.GetVector(WmvLiquidMaterials.HiBumpMeanId) : none4;
        Vector4 wantMean = water != null && water.Bump != null ? WmvLiquidMaterials.BumpMean(water.Bump.SlopeMeanX, water.Bump.SlopeMeanY) : none4;
        Vector4 flatMean = ocean != null ? ocean.Material.GetVector(WmvLiquidMaterials.HiBumpMeanId) : none4;
        Check(water != null && water.Bump != null && Math.Abs(water.Bump.SlopeMeanX - mean[0]) < 1e-9 && Math.Abs(water.Bump.SlopeMeanY - mean[1]) < 1e-9 &&
              Math.Abs(mean[0]) > 0.1 && Math.Abs(mean[1]) > 0.05 && boundMean.x == wantMean.x && boundMean.y == wantMean.y && boundMean.z == 0f &&
              boundMean.w == 0f && Math.Abs(wantMean.x - mean[0] * meanShare) < 1e-6 && Math.Abs(wantMean.y - mean[1] * meanShare) < 1e-6 &&
              flatMean.x == 0f && flatMean.y == 0f && flatMean.z == 0f && flatMean.w == 0f,
              string.Format(System.Globalization.CultureInfo.InvariantCulture,
                            "high: the ripple map's mean slope (its constant tilt) is measured at upload ({0:0.0000}, {1:0.0000}; expected {2:0.0000}, " +
                            "{3:0.0000}) and bound as the material's _HiBumpMean x the tune's bumpMean {4} ({5:0.0000}, {6:0.0000}) -- the shader takes " +
                            "it out; a type whose map is not drawn has (0, 0) ({7:0.0000}, {8:0.0000})",
                            water != null && water.Bump != null ? water.Bump.SlopeMeanX : -9, water != null && water.Bump != null ? water.Bump.SlopeMeanY : -9,
                            mean[0], mean[1], meanShare, boundMean.x, boundMean.y, flatMean.x, flatMean.y), log);
        Check(ocean != null && !ocean.HasBump && ocean.Bump != null && ocean.Bump.State == WmvLiquidFrames.StateFailed &&
              ocean.Bump.Detail.Contains("is not a unit normal map") && ocean.Material.GetFloat(WmvLiquidMaterials.HasBumpId) == 0f &&
              magma != null && magma.Bump == null && magma.Material.GetFloat(WmvLiquidMaterials.HasBumpId) == 0f,
              "high: a ripple map that is not a unit normal map is not drawn -- the ocean keeps flat normals (_HasBump 0) and says why; a type " +
              "naming none (magma) has none (" + (ocean != null && ocean.Bump != null ? ocean.Bump.Detail : "no ocean list") + ")", log);
        long frameBytes = (water != null && water.Frames != null ? water.Frames.GpuBytes : 0) + (magma != null && magma.Frames != null ? magma.Frames.GpuBytes : 0);
        long bumpBytes = water != null && water.Bump != null ? water.Bump.GpuBytes : -1;
        Check(reg != null && reg.ArrayCount == 3 && WmvLiquidRegistry.LiveArrays == 3 && bumpBytes == 4L * (64 + 16 + 4 + 1) &&
              reg.ArrayBytes == frameBytes + bumpBytes && WmvLiquidRegistry.LiveArrayBytes == reg.ArrayBytes && l != null && l.TextureBytes == reg.ArrayBytes,
              "high: the ripple map counts in the arrays' tallies and bytes (3 arrays: two frame lists and the map of " + bumpBytes + " bytes), and " +
              "in the report's texture bytes (" + (reg != null ? reg.Summary() : "no registry") + ")", log);
        h.Session.Clear("test");
        Check(clean() && Textures2D() == textures0,
              "high: clearTerrain takes the ripple map away with its type (" + LiquidTallies() + "; Texture2D " + textures0 + " -> " + Textures2D() + ")", log);

        // ---- a world load: the world model's MLIQ (type 13) shares type 1's ripple map file -- asked once, bound
        WorldFiles wf = LiquidWorldFiles();
        AddLiquidBumps(wf);
        var hw = new TerrainHarness();
        hw.Session.DestroyObject = o => UnityEngine.Object.DestroyImmediate(o);
        var v = new TestWorldView();
        views.Add(v);
        hw.Session.WorldView = v;
        WmvIpcClient.TerrainTileCommand wc = WorldCommand(62, false);
        wc.Liquids = LiquidTestOffer(false, true);
        hw.Session.Load(wc);
        wf.Serve(hw, 62);
        WmvIpcClient.TerrainTileReport wr = hw.For(62);
        WmvLiquidRegistry wreg = hw.Session.SceneLiquids;
        WmvLiquidType wmoWater = wreg != null ? wreg.Get(13) : null;
        Check(wr != null && wr.Status == "built" && wmoWater != null && wmoWater.HasBump && AskedFor(hw, rl, LqB1) == 1 &&
              wmoWater.Material.GetFloat(WmvLiquidMaterials.HasBumpId) == 1f && wr.Liquids != null && wr.Liquids.TexturesRequested == 4,
              "high: a world model's MLIQ fetches its type's ripple map under the world load like its frames (4 files: 3 frames, 1 map) and " +
              "draws with it (" + (wreg != null ? wreg.Summary() : "no registry") + ")", log);
        hw.Session.Clear("test");
        Check(clean() && WorldTallies() == "0/0/0/0/0" && Textures2D() == textures0,
              "high: clearTerrain takes the world model's ripple map away (" + LiquidTallies() + ")", log);

        // ---- a streaming session: the tiles' water and the world model's (types 1 and 13) share one ripple map for the scene
        var hh = new TerrainHarness();
        var vv = new TestWorldView();
        views.Add(vv);
        WorldFiles sf = LiquidStreamFiles();
        AddLiquidBumps(sf);
        var sh = new StreamHost(hh, vv, sf);
        WmvTerrainSession s = sh.H.Session;
        WmvIpcClient.TerrainTileCommand sc = StreamCommand(63);
        sc.Liquids = LiquidTestOffer(false, true);
        s.Load(sc);
        bool settled = sh.Until(() => sh.H.For(63) != null && sh.Settled() && s.StreamTiles == 9);
        for (int k = 0; k < 20; k++) sh.Round();
        WmvLiquidRegistry sreg = s.SceneLiquids;
        WmvLiquidType sWater = sreg != null ? sreg.Get(1) : null, sWmo = sreg != null ? sreg.Get(13) : null;
        WmvIpcClient.StreamStateReport last = sh.States.Count > 0 ? sh.States[sh.States.Count - 1] : null;
        Check(settled && sWater != null && sWmo != null && sWater.HasBump && sWmo.HasBump && sWater.Bump == sWmo.Bump && sh.Asked(rl, LqB1) == 1 &&
              sreg.ArrayCount == 3 && last != null && last.LiquidTextures == 3,
              "high: a streaming session asks for a ripple map once for the scene; the tiles' water and the world model's share it, and " +
              "streamState counts it (" + (sreg != null ? sreg.Summary() : "no registry") + ")", log);
        s.Clear("test");
        Check(clean() && !s.Streaming && WorldTallies() == "0/0/0/0/0" && Textures2D() == textures0,
              "high: clearTerrain ends the session and its ripple map goes (" + LiquidTallies() + ")", log);
    }

    /// <summary>The High look's hold rules (H4, H8) and a pooled type's ripple map evicted (H11):
    /// - the High look off (the pass removed, as WMV_WATER=low leaves it): no type holds a ripple map (nothing fetched,
    ///   nothing uploaded: the zone-light build's traffic and budgets) and no transparent material writes depth;
    /// - on: the transparent water and ocean types hold theirs and write depth; a transparent type of another kind (a "10FX"
    ///   lava naming a ripple map) holds none and writes none; an opaque type writes depth as before;
    /// - a streaming registry's type with an uploaded ripple map, released (pooled) and evicted: its map goes with it, its
    ///   bytes freed and counted, unless another type still lists the same map (then only with that type's eviction).</summary>
    static void LiquidHighLookHoldTests(Action<string> log, Func<bool> clean)
    {
        Func<Action<UnityEngine.Object>> destroy = () => o => UnityEngine.Object.DestroyImmediate(o);
        bool grabWas = WmvWaterGrabPass.Active;
        WmvIpcClient.LiquidOffer offer = LiquidTestOffer(false, true);
        offer.Catalog.Add(new LiquidTypeView { Id = 1174, Name = "10FX Lava", Kind = 2, Material = 13, MaterialFlags = 1, Lvf = 3,
                                               MinimapColor = 0xFFFF3A00, Frames = new[] { LqM1 }, FramesWanted = 1, Bump = LqB1 });
        var holder = new object();
        string lowLine = "", highLine = "";
        bool low = false, high = false;
        try
        {
            WmvWaterGrabPass.SetActive(false);
            var lowReg = new WmvLiquidRegistry(offer, false, destroy, null);
            WmvLiquidType lw = lowReg.Hold(holder, 1), lo = lowReg.Hold(holder, 2), lw13 = lowReg.Hold(holder, 13), lv = lowReg.Hold(holder, 1174);
            Func<WmvLiquidType, float> zw = x => x != null && x.Material != null ? x.Material.GetFloat(WmvLiquidMaterials.ZWriteId) : -1f;
            low = lw != null && lo != null && lw13 != null && lv != null && lw.Bump == null && lo.Bump == null && lw13.Bump == null && lv.Bump == null &&
                  zw(lw) == 0f && zw(lo) == 0f && zw(lw13) == 0f && zw(lv) == 0f && lowReg.Frame(LqB1) == null && lowReg.Frame(LqB2) == null &&
                  !WmvLiquidMaterials.HighLook(lw.View);
            lowLine = lowReg.Summary();
            lowReg.Dispose();

            WmvWaterGrabPass.SetActive(true);
            var highReg = new WmvLiquidRegistry(offer, false, destroy, null);
            WmvLiquidType hw = highReg.Hold(holder, 1), ho = highReg.Hold(holder, 2), hw13 = highReg.Hold(holder, 13), hv = highReg.Hold(holder, 1174),
                          hm = highReg.Hold(holder, 19);
            high = hw != null && ho != null && hw13 != null && hv != null && hm != null && hw.Bump != null && hw.Bump == hw13.Bump && ho.Bump != null &&
                   hv.Bump == null && hm.Bump == null && zw(hw) == 1f && zw(ho) == 1f && zw(hw13) == 1f && zw(hv) == 0f && zw(hm) == 1f &&
                   highReg.Frame(LqB1) != null && highReg.Frame(LqB2) != null && WmvLiquidMaterials.HighLook(hw.View) && !WmvLiquidMaterials.HighLook(hv.View);
            highLine = highReg.Summary();
            highReg.Dispose();
        }
        finally
        {
            WmvWaterGrabPass.SetActive(grabWas);
        }
        Check(low, "high (H4, H8): with the High look off (the pass removed, as WMV_WATER=low leaves it) no type holds its ripple map -- nothing " +
                   "to fetch -- and no transparent material writes depth (" + lowLine + ")", log);
        Check(high, "high (H4, H8): with it on, water, WMO water and ocean hold their ripple maps (one list for the file they share) and write " +
                    "depth; a transparent lava naming a ripple map holds none and writes none; magma (opaque) writes depth as before (" + highLine + ")", log);

        // ---- a pooled type's ripple map evicted (a streaming session's registry): types 1 and 13 share LqB1's map
        bool evictOk = false;
        string evictLine = "";
        WmvLiquidRegistry pool = null;
        try
        {
            pool = new WmvLiquidRegistry(LiquidTestOffer(false, true), true, destroy, null);
            object tileA = new object(), modelB = new object();
            WmvLiquidType a = pool.Hold(tileA, 1), b = pool.Hold(modelB, 13);
            bool settled = pool.FramePrepared(LqB1, PrepareTexture(LqB1, NormalBlp(8, 8)));
            long mapBytes = a != null && a.Bump != null ? a.Bump.GpuBytes : -1;
            bool bound = settled && a != null && b != null && a.HasBump && b.HasBump && a.Bump == b.Bump && pool.ArrayCount == 1 &&
                         WmvLiquidRegistry.LiveArrays == 1 && WmvLiquidRegistry.LiveArrayBytes == mapBytes && mapBytes > 0;
            pool.Release(tileA);
            pool.Release(modelB);
            bool pooled = a.Pooled && b.Pooled && !a.Disposed && pool.PooledCount == 2 && pool.PooledGpuBytes(null) == mapBytes;
            long freedA = pool.Evict(a);
            bool kept = a.Disposed && !b.Disposed && b.HasBump && freedA == 0 && pool.ArrayCount == 1 && WmvLiquidRegistry.LiveArrays == 1;
            long freedB = pool.Evict(b);
            bool gone = b.Disposed && freedB == mapBytes && pool.ArrayCount == 0 && pool.ArrayBytes == 0 && WmvLiquidRegistry.LiveArrays == 0 &&
                        WmvLiquidRegistry.LiveArrayBytes == 0 && pool.TypesEvicted == 2 && pool.TypeCount == 0 && pool.Frame(LqB1) == null;
            evictOk = bound && pooled && kept && gone;
            evictLine = string.Format("bound {0}, pooled {1}, first eviction kept the shared map {2} (freed {3}), second freed {4} of {5} bytes {6}",
                                      bound, pooled, kept, freedA, freedB, mapBytes, gone);
        }
        catch (Exception e)
        {
            evictLine = "threw " + e.GetType().Name + ": " + e.Message;
        }
        finally
        {
            if (pool != null)
                pool.Dispose();
        }
        Check(evictOk, "high (H11): a pooled type's uploaded ripple map is evicted with it -- kept while another pooled type lists the same map, " +
                       "freed and counted with the last (" + evictLine + ")", log);
        Check(clean(), "high (H4, H8, H11): nothing of the hold and eviction tests is left (" + LiquidTallies() + ")", log);
    }

    /// <summary>The HIGH path's GPU fixture: the matrices RenderToArray uses for one band over x 0..8, z 0..8, heights 40..60 at
    /// 4 x 4 pixels (2 yd a pixel, top-down orthographic), so the tests can put synthetic grab textures under them.</summary>
    sealed class HighFixture
    {
        public readonly Matrix4x4 Vp, InvVp;

        public HighFixture()
        {
            const float x0 = 0f, zNorth = 8f, extent = 8f, yMin = 40f, yMax = 60f;
            float cx = x0 + extent * 0.5f, cz = zNorth - extent * 0.5f, cy = yMax + 10f;
            var view = new Matrix4x4();
            view.SetRow(0, new Vector4(1f, 0f, 0f, -cx));
            view.SetRow(1, new Vector4(0f, 0f, 1f, -cz));
            view.SetRow(2, new Vector4(0f, 1f, 0f, -cy));
            view.SetRow(3, new Vector4(0f, 0f, 0f, 1f));
            float top = zNorth - cz, bottom = top - extent;
            Matrix4x4 ortho = Matrix4x4.Ortho(x0 - cx, x0 + extent - cx, bottom, top, 1f, (yMax - yMin) + 20f);
            Vp = GL.GetGPUProjectionMatrix(ortho, true) * view;
            InvVp = Vp.inverse;
        }

        /// <summary>The device depth of height y (orthographic, looking straight down: the same at every x, z).</summary>
        public float DepthOf(float y)
        {
            Vector4 c = Vp * new Vector4(4f, y, 4f, 1f);
            return c.z / c.w;
        }
    }

    /// <summary>A 4 x 4 linear RGBA32 grab colour, texel (column, row) -- row 0 the first stored, the south row of the fixture.</summary>
    static Texture2D HighColour(Func<int, int, Color32> texel)
    {
        var t = new Texture2D(4, 4, TextureFormat.RGBA32, false, true) { name = "WmvLiquid High test colour", filterMode = FilterMode.Bilinear,
                                                                          wrapMode = TextureWrapMode.Clamp };
        var px = new Color32[16];
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
                px[row * 4 + col] = texel(col, row);
        t.SetPixels32(px);
        t.Apply(false, false);
        return t;
    }

    /// <summary>A 4 x 4 R32F grab depth of raw device depths by (column, row).</summary>
    static Texture2D HighDepth(Func<int, int, float> raw)
    {
        var t = new Texture2D(4, 4, TextureFormat.RFloat, false, true) { name = "WmvLiquid High test depth", filterMode = FilterMode.Point,
                                                                         wrapMode = TextureWrapMode.Clamp };
        var d = new float[16];
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
                d[row * 4 + col] = raw(col, row);
        t.SetPixelData(d, 0);
        t.Apply(false, false);
        return t;
    }

    /// <summary>The HIGH fixture's water quad (height 50 over the whole view, no stored depth) drawn with `m` into 4 x 4 float32
    /// pixels (north row first), the synthetic grab bound only for this draw: _WmvWaterGrab 1, then 0 and the grab textures black
    /// again (RenderToArray puts the switch back as well). `refl`: the reflections' colour (_WmvSceneColorRefl; null: `colour`);
    /// `invVp`: the inverse view-projection the HIGH path rebuilds points with (null: the fixture's own) -- a sheared one makes
    /// the view ray oblique while the quad is still drawn top-down; `slope`: the quad rises this many yd per yd towards the east
    /// (50 - 4 slope at x 0, 50 + 4 slope at x 8).</summary>
    static float[] HighRender(HighFixture f, Material m, Texture colour, Texture blur, Texture depth, Vector4 sky, out string why, Texture refl = null,
                              Matrix4x4? invVp = null, float slope = 0f)
    {
        why = null;
        var mesh = new Mesh { name = "WmvLiquid High test quad" };
        try
        {
            var uv1 = new Vector2(1f, -1f);
            float yW = 50f - 4f * slope, yE = 50f + 4f * slope;
            mesh.SetVertices(new List<Vector3> { new Vector3(0f, yW, 0f), new Vector3(8f, yE, 0f), new Vector3(8f, yE, 8f), new Vector3(0f, yW, 8f) });
            mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
            mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
            mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();
            return WmvTerrainComposite.RenderToArray(cmd =>
            {
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorId, colour);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorReflId, refl != null ? refl : colour);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorBlurId, blur);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneDepthId, depth);
                cmd.SetGlobalVector(WmvWaterGrabPass.GrabTexelId, new Vector4(0.25f, 0.25f, 4f, 4f));
                cmd.SetGlobalMatrix(InvVpId, invVp.HasValue ? invVp.Value : f.InvVp);
                cmd.SetGlobalVector(WmvLiquidMaterials.ReflectSkyId, sky);
                cmd.SetGlobalFloat(WmvLiquidMaterials.LiquidTimeId, 0f);
                cmd.SetGlobalVector(WmvLiquidMaterials.RippleOriginId, Vector4.zero);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, 1f);
                cmd.DrawMesh(mesh, Matrix4x4.identity, m, 0, 0);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, 0f);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorReflId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorBlurId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneDepthId, Texture2D.blackTexture);
            }, 0f, 8f, 8f, 8f, 40f, 60f, 4, 4, false, false, true);
        }
        catch (Exception e)
        {
            why = e.GetType().Name + ": " + e.Message;
            return null;
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(mesh);
        }
    }

    /// <summary>Every pixel of columns `cols` (column c, north-first row i) of `px` against expect(c, i) within eps; the first
    /// miss is said in `first`.</summary>
    static bool HighPixels(float[] px, Func<int, int, double[]> expect, int[] cols, double eps, out string first)
    {
        first = px == null ? "no pixels" : "";
        if (px == null)
            return false;
        foreach (int col in cols)
            for (int i = 0; i < 4; i++)
            {
                double[] e = expect(col, i);
                int at = (i * 4 + col) * 4;
                for (int k = 0; k < 4; k++)
                    if (Math.Abs(px[at + k] - e[k]) > eps)
                    {
                        first = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                              "pixel ({0}, {1}): got ({2:0.0000}, {3:0.0000}, {4:0.0000}, {5:0.0000}), expected ({6:0.0000}, {7:0.0000}, " +
                                              "{8:0.0000}, {9:0.0000})", col, i, px[at], px[at + 1], px[at + 2], px[at + 3], e[0], e[1], e[2], e[3]);
                        return false;
                    }
            }
        return true;
    }

    static double[] Rgb1(double r, double g, double b) { return new[] { r, g, b, 1.0 }; }

    /// <summary>The High look in the self-test: the materials' TUNABLE values, the IPC's "bump" and "sky", the reflection sky's
    /// global, the pass's owner rule (H9), the HIGH path on the GPU (RenderToArray with synthetic grab textures, orthographic so
    /// Ls is the depth below the water: the bed exactly at Ls 0, T at Ls 1, 3.25 and 10, a flat normal reading its own pixel,
    /// an occluder in front keeping the unshifted pixel, a surface above the water behind the water point under an oblique ray
    /// kept out of the bed (H1), Fresnel at normal incidence, a miss giving the sky, a hit the scene's colour, a hit on a
    /// half-covered texel half the sky (H2), the type colour without a zone light saturated (H3), the reflection shares
    /// limited (H10), the output saturated, the ripple map's mean tilt taken out before the strength scale, and the HIGH path
    /// by the triangle's facing: a sloped sheet rising above the eye seen from above HIGH, seen from below never (F1)) and
    /// the grab on Camera.main (LiquidHighIntegrationTests: the asymmetric view and the reflections' coverage on the real
    /// camera path).</summary>
    static void LiquidHighTests(Action<string> log, Func<bool> clean)
    {
        LiquidHighTune tune = WmvLiquidMaterials.Tune;
        Shader shader = WmvLiquidMaterials.ResolveShader(null);

        // ---- the material: the tune's values on every liquid material, flat normals until a ripple map is bound
        Material made = shader != null ? WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 5, Name = "Slow Water", Kind = 0, Material = 1,
                                                                                                         MaterialFlags = 1, Lvf = 0, MinimapColor = 0x9F12A0D5 })
                                       : null;
        try
        {
            Check(made != null && made.GetFloat(WmvLiquidMaterials.HiRefractId) == (float)tune[LiquidHighTune.Refract] &&
                  made.GetVector(WmvLiquidMaterials.HiTFastId) == new Vector4((float)tune[LiquidHighTune.TFastA], (float)tune[LiquidHighTune.TFastL], 0f, 0f) &&
                  made.GetVector(WmvLiquidMaterials.HiTSlowId) == new Vector4((float)tune[LiquidHighTune.TSlowA], (float)tune[LiquidHighTune.TSlowL], 0f, 0f) &&
                  made.GetFloat(WmvLiquidMaterials.HiWaterGainId) == (float)tune[LiquidHighTune.WaterGain] &&
                  made.GetFloat(WmvLiquidMaterials.HiNoZoneGainId) == (float)tune[LiquidHighTune.NoZoneGain] &&
                  made.GetFloat(WmvLiquidMaterials.HiBumpNormId) == 1f &&
                  made.GetFloat(WmvLiquidMaterials.HiReflBroadId) == (float)tune[LiquidHighTune.ReflBroad] &&
                  made.GetFloat(WmvLiquidMaterials.HiReflDetailId) == (float)tune[LiquidHighTune.ReflDetail] &&
                  made.GetVector(WmvLiquidMaterials.HiBumpTileId).x == (float)tune[LiquidHighTune.BumpTileA] &&
                  made.GetFloat(WmvLiquidMaterials.HiBumpScrollId) == (float)tune[LiquidHighTune.BumpScroll] &&
                  made.GetFloat(WmvLiquidMaterials.HasBumpId) == 0f && made.HasProperty(WmvLiquidMaterials.BumpMapId) &&
                  made.HasProperty(WmvLiquidMaterials.HiBumpMeanId) && made.GetVector(WmvLiquidMaterials.HiBumpMeanId).sqrMagnitude == 0f,
                  "high: a liquid material carries the water tune's values (" + tune.Describe() + ") and flat normals (no mean tilt) until its " +
                  "ripple map is bound", log);
        }
        finally
        {
            if (made != null) UnityEngine.Object.DestroyImmediate(made);
        }

        // ---- the reflection sky's global: the zone light's sky, else the neutral (tuned) sky; off: the neutral sky
        LiquidLights lit = LiquidTestLights(0);
        lit.AddValues(204, 5985, new float[] { 64, 62, 35 }, new float[] { 62, 54, 47 }, new float[] { 82, 90, 94 }, new float[] { 55, 61, 69 },
                      new[] { 0.4f, 0.8f }, new[] { 0.75f, 1f }, new float[] { 102, 153, 204 });
        WmvLiquidLight.Sample(lit, 50, 50, 0, 0.016, true);           // zone 139 -> light 204, with a sky
        Vector4 zoneSky = Shader.GetGlobalVector(WmvLiquidMaterials.ReflectSkyId);
        WmvLiquidLight.Sample(lit, 101, 50, 0, 0.016, true);          // the map default 191: no sky
        Vector4 defaultSky = Shader.GetGlobalVector(WmvLiquidMaterials.ReflectSkyId);
        WmvLiquidLight.Off();
        Vector4 offSky = Shader.GetGlobalVector(WmvLiquidMaterials.ReflectSkyId), neutral = WmvLiquidMaterials.NeutralSky;
        Check(Near(zoneSky.x, 0.4f) && Near(zoneSky.y, 0.6f) && Near(zoneSky.z, 0.8f) && Near(defaultSky.x, neutral.x) && Near(defaultSky.z, neutral.z) &&
              Near(offSky.x, neutral.x) && Near(offSky.y, neutral.y) && Near(offSky.z, neutral.z),
              "high: _WmvReflectSky is the zone light's sky (102, 153, 204 -> 0.4, 0.6, 0.8), the neutral sky where the light gives none and " +
              "with the zone light off (" + neutral + ")", log);

        // ---- H9: only the owner (the WmvMain that installed the pass through Configure) removes it
        bool activeBefore = WmvWaterGrabPass.Active;
        bool stranger = WmvWaterGrabPass.Release(new object()), nobody = WmvWaterGrabPass.Release(null);
        Check(!stranger && !nobody && WmvWaterGrabPass.Active == activeBefore,
              "high (H9): Release by anyone but the pass's owner (another WmvMain, nobody) leaves it as it was (installed " + activeBefore + " -> " +
              WmvWaterGrabPass.Active + ")", log);

        LiquidHighGpuTests(log, shader, tune);
        LiquidHighIntegrationTests(log, shader);
        Check(clean() && Shader.GetGlobalFloat(WmvWaterGrabPass.WaterGrabId) == 0f,
              "high: nothing of these tests is left and _WmvWaterGrab is 0 (" + LiquidTallies() + ")", log);
    }

    static void LiquidHighGpuTests(Action<string> log, Shader shader, LiquidHighTune tune)
    {
        if (shader == null)
        {
            Check(false, "high on the GPU: no liquid shader", log);
            return;
        }
        var f = new HighFixture();
        float savedTime = Shader.GetGlobalFloat(WmvLiquidMaterials.LiquidTimeId);
        var made = new List<UnityEngine.Object>();
        Func<Texture2D, Texture2D> keep = t => { made.Add(t); return t; };
        Material m = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0,
                                                                                   MinimapColor = 0x9F12A0D5 });
        made.Add(m);
        double fa = tune[LiquidHighTune.TFastA], fl = tune[LiquidHighTune.TFastL], sa = tune[LiquidHighTune.TSlowA], sl = tune[LiquidHighTune.TSlowL];
        double gain = tune[LiquidHighTune.WaterGain], noZone = tune[LiquidHighTune.NoZoneGain];
        Func<double, double> T = ls => LiquidHighMath.Transmission(ls, fa, fl, sa, sl);
        double[] baseColour = { 18 / 255.0, 160 / 255.0, 213 / 255.0 };
        Func<int, int, Color32> bedTexel = (col, row) => new Color32((byte)(40 + 50 * col), (byte)(30 + 60 * row), (byte)(200 - 30 * col - 20 * row), 255);
        Func<Color32, int, double> ch = (c32, k) => (k == 0 ? c32.r : k == 1 ? c32.g : c32.b) / 255.0;
        try
        {
            m.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, 0f);
            WmvLiquidLight.Off();
            Texture2D bed = keep(HighColour(bedTexel));
            string why, miss;

            // ---- Ls 0: the bed exactly (T clamps at 1), each pixel its own texel (a flat normal: no offset)
            float[] px = HighRender(f, m, bed, bed, keep(HighDepth((col, row) => f.DepthOf(50f))), Vector4.zero, out why);
            bool ok = HighPixels(px, (col, i) => Rgb1(ch(bedTexel(col, 3 - i), 0), ch(bedTexel(col, 3 - i), 1), ch(bedTexel(col, 3 - i), 2)),
                                 new[] { 0, 1, 2, 3 }, 3e-3, out miss);
            bool mirrored = !ok && HighPixels(px, (col, i) => Rgb1(ch(bedTexel(col, i), 0), ch(bedTexel(col, i), 1), ch(bedTexel(col, i), 2)),
                                              new[] { 0, 1, 2, 3 }, 3e-3, out string ignored);
            Check(ok, "high on the GPU: at Ls 0 the water is the bed exactly, every pixel its own texel of _WmvSceneColor (a flat normal), alpha 1" +
                      (ok ? "" : " -- " + miss + (mirrored ? " (the rows come out MIRRORED: the screen uv's vertical sense)" : "") + (why != null ? " (" + why + ")" : "")),
                  log);

            // ---- Ls 1 (the zone light's river pair), 3.25 and 10 (the type colour): lerp(W, bed, T(Ls)), W = lerp(close, far, Ls / 3.25) x
            // gain under the zone light, saturate(type colour x noZoneGain) without it
            foreach (double d in new[] { 1.0, 3.25, 10.0 })
            {
                bool zone = d == 1.0;
                if (zone)
                    WmvLiquidLight.Sample(LiquidTestLights(0), 50, 50, 0, 0.016, true);
                else
                    WmvLiquidLight.Off();
                double[] close = zone ? new[] { 64 / 255.0, 62 / 255.0, 35 / 255.0 } : baseColour, far = zone ? new[] { 62 / 255.0, 54 / 255.0, 47 / 255.0 } : baseColour;
                px = HighRender(f, m, bed, bed, keep(HighDepth((col, row) => f.DepthOf(50f - (float)d))), Vector4.zero, out why);
                double t = T(d);
                Func<int, int, double[]> want = (col, i) =>
                {
                    var e = new double[4];
                    for (int k = 0; k < 3; k++)
                        e[k] = LiquidHighMath.Refracted(zone ? LiquidHighMath.Water(close[k], far[k], d, WmvLiquidMaterials.DepthFull, gain)
                                                             : LiquidHighMath.WaterNoZone(baseColour[k], noZone), ch(bedTexel(col, 3 - i), k), t);
                    e[3] = 1.0;
                    return e;
                };
                ok = HighPixels(px, want, new[] { 0, 1, 2, 3 }, 3e-3, out miss);
                Check(ok, string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                        "high on the GPU: Ls {0} yd ({1}): lerp(W, bed, T) with T = {2:0.0000} within 3e-3{3}", d,
                                        zone ? "the zone light's river pair" : "no zone light: the type colour x noZoneGain", t,
                                        ok ? "" : " -- " + miss + (why != null ? " (" + why + ")" : "")), log);
            }
            WmvLiquidLight.Off();

            // ---- an occluder in front of the offset pixel: the unshifted pixel. A ripple map of the one normal (0.6, 0, 0.8) shifts
            // the uv one pixel east (_HiRefract set for it); columns 0 and 1 hold the bed 1 yd down, 2 and 3 something 5 yd ABOVE the
            // water. Column 0 reads column 1's bed (shifted, Ls = |(2, -1)| = 2.236); column 1 would read column 2's occluder -- it
            // keeps its own (Ls 1).
            Texture2D bump = keep(HighColour((col, row) => new Color32(204, 128, 230, 255)));
            bump.wrapMode = TextureWrapMode.Repeat;
            double nx = 204 / 255.0 * 2 - 1, ny = 128 / 255.0 * 2 - 1, nz = 230 / 255.0 * 2 - 1;
            double len = Math.Sqrt(nx * nx + ny * ny + nz * nz);
            double Nx = nx / len, Nz = ny / len;            // the world normal is (x, z, y) of the map's
            float refract = (float)(0.25 / Nx);
            m.SetTexture(WmvLiquidMaterials.BumpMapId, bump);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiBumpStrengthId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiRefractId, refract);
            m.SetFloat(WmvLiquidMaterials.HiRefractRampId, 0.3f);
            Func<int, Color32> colTexel = col => new Color32((byte)(20 + 60 * col), (byte)(200 - 50 * col), 90, 255);
            Texture2D byColumn = keep(HighColour((col, row) => colTexel(col)));
            px = HighRender(f, m, byColumn, byColumn, keep(HighDepth((col, row) => f.DepthOf(col < 2 ? 49f : 55f))), Vector4.zero, out why);
            double lsShifted = Math.Sqrt(4 + 1 + Math.Pow(8 * Nz * refract, 2));
            Func<double, double[]> bedOf1 = ls =>
            {
                var e = new double[4];
                for (int k = 0; k < 3; k++)
                    e[k] = LiquidHighMath.Refracted(LiquidHighMath.WaterNoZone(baseColour[k], noZone), ch(colTexel(1), k), T(ls));
                e[3] = 1.0;
                return e;
            };
            bool shifted = HighPixels(px, (col, i) => bedOf1(lsShifted), new[] { 0 }, 3e-3, out miss);
            string miss2;
            bool unshifted = HighPixels(px, (col, i) => bedOf1(1.0), new[] { 1 }, 3e-3, out miss2);
            Check(shifted && unshifted,
                  "high on the GPU: the ripple normal shifts the bed one pixel (column 0 reads column 1's bed, Ls 2.236); where the shifted pixel's " +
                  "scene lies in front of the water (column 1 -> column 2's occluder) the unshifted pixel is kept (Ls 1)" +
                  (shifted ? "" : " -- shifted: " + miss) + (unshifted ? "" : " -- unshifted: " + miss2) + (why != null ? " (" + why + ")" : ""), log);

            // ---- the ripple map's mean tilt: the same one-normal map with its own slope, (0.6, 0.0039) / 0.8039, given as the map's
            // mean (_HiBumpMean, what LiquidBump measures on it), the bed 1 yd down under every column. Taken out BEFORE the strength
            // scale, nothing of the tilt is left at strength 1 or 3: every pixel reads its own texel (Ls 1) -- a constant tilt no
            // longer shifts the bed. With the mean 0 the same map shifts columns 0-2 onto the next column's bed (Ls 2.236).
            Texture2D bed49 = keep(HighDepth((col, row) => f.DepthOf(49f)));
            Func<int, double, double[]> tiltBed = (c, ls) =>
            {
                var e = new double[4];
                for (int k = 0; k < 3; k++)
                    e[k] = LiquidHighMath.Refracted(LiquidHighMath.WaterNoZone(baseColour[k], noZone), ch(colTexel(c), k), T(ls));
                e[3] = 1.0;
                return e;
            };
            m.SetVector(WmvLiquidMaterials.HiBumpMeanId, new Vector4((float)(nx / nz), (float)(ny / nz), 0f, 0f));
            bool tiltOut = true;
            string tiltMiss = "";
            foreach (float strength in new[] { 1f, 3f })
            {
                m.SetFloat(WmvLiquidMaterials.HiBumpStrengthId, strength);
                px = HighRender(f, m, byColumn, byColumn, bed49, Vector4.zero, out why);
                string ownMiss;
                bool own = HighPixels(px, (col, i) => tiltBed(col, 1.0), new[] { 0, 1, 2, 3 }, 3e-3, out ownMiss);
                tiltOut &= own;
                if (!own)
                    tiltMiss += " -- strength " + strength + ": " + ownMiss + (why != null ? " (" + why + ")" : "");
            }
            m.SetVector(WmvLiquidMaterials.HiBumpMeanId, Vector4.zero);
            m.SetFloat(WmvLiquidMaterials.HiBumpStrengthId, 1f);
            px = HighRender(f, m, byColumn, byColumn, bed49, Vector4.zero, out why);
            string keptMiss;
            bool tiltKept = HighPixels(px, (col, i) => tiltBed(col + 1, lsShifted), new[] { 0, 1, 2 }, 3e-3, out keptMiss);
            Check(tiltOut && tiltKept,
                  "high on the GPU: the ripple map's mean tilt (_HiBumpMean) is taken out of the slope before the strength scale -- a map of " +
                  "one tilt given as its mean draws every pixel on its own texel (Ls 1) at strength 1 and 3; with the mean 0 the same map " +
                  "shifts the bed one column (Ls 2.236)" + tiltMiss + (tiltKept ? "" : " -- mean 0: " + keptMiss + (why != null ? " (" + why + ")" : "")),
                  log);

            // ---- H1: an oblique view ray (the inverse view-projection sheared: x + (y - 50), so every ray runs down at 45 degrees
            // towards -x and still meets the water where the quad is drawn) and a ripple tilting the uv one pixel west. Column 0
            // holds a surface 0.5 yd ABOVE the water, columns 1-3 the bed 1 yd below. Column 1's shifted sample (column 0) lies
            // BEHIND its water point along the ray (dot 1 > 0) but above the water: the far bank's case -- the unshifted bed is
            // kept (Ls sqrt 2). Columns 2 and 3 shift onto the bed (Ls sqrt 10): taken.
            Texture2D west = keep(HighColour((col, row) => new Color32(51, 128, 230, 255)));
            west.wrapMode = TextureWrapMode.Repeat;
            double wx = 51 / 255.0 * 2 - 1, wlen = Math.Sqrt(wx * wx + ny * ny + nz * nz), Wx = wx / wlen, Wz = ny / wlen;
            float westRefract = (float)(0.25 / Math.Abs(Wx));
            m.SetTexture(WmvLiquidMaterials.BumpMapId, west);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiRefractId, westRefract);
            var shear = Matrix4x4.identity;
            shear.SetRow(0, new Vector4(1f, 1f, 0f, -50f));
            px = HighRender(f, m, byColumn, byColumn, keep(HighDepth((col, row) => f.DepthOf(col == 0 ? 50.5f : 49f))), Vector4.zero, out why, null,
                            shear * f.InvVp);
            double dz = 8 * Wz * westRefract;
            Func<int, double, double[]> bedOf = (c, ls) =>
            {
                var e = new double[4];
                for (int k = 0; k < 3; k++)
                    e[k] = LiquidHighMath.Refracted(LiquidHighMath.WaterNoZone(baseColour[k], noZone), ch(colTexel(c), k), T(ls));
                e[3] = 1.0;
                return e;
            };
            bool aboveKept = HighPixels(px, (col, i) => bedOf(1, Math.Sqrt(2.0)), new[] { 1 }, 3e-3, out miss);
            bool bedTaken = HighPixels(px, (col, i) => bedOf(col - 1, Math.Sqrt(10.0 + dz * dz)), new[] { 2, 3 }, 3e-3, out miss2);
            bool wouldLeak = !HighPixels(px, (col, i) => bedOf(0, Math.Sqrt(1.5 * 1.5 + 0.25 + dz * dz)), new[] { 1 }, 3e-3, out string ignored2);
            Check(aboveKept && bedTaken && wouldLeak,
                  "high on the GPU (H1): under an oblique ray a shifted sample above the water behind the water point (column 1 -> column 0's " +
                  "surface 0.5 yd up) is not the bed -- the unshifted bed is kept (Ls 1.414); a shifted sample on the bed is taken (columns 2, 3: " +
                  "Ls 3.162)" + (aboveKept ? "" : " -- column 1: " + miss) + (bedTaken ? "" : " -- columns 2, 3: " + miss2) +
                  (why != null ? " (" + why + ")" : ""), log);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 0f);

            // ---- F1: the HIGH path by the triangle's facing. A sheet rising 0.5 yd per yd towards the east (48 yd at x 0, 52 at x 8:
            // the columns' water points at 48.5, 49.5, 50.5 and 51.5), flat normals, the scene 5 yd under the unmoved sheet's middle.
            // The inverse view-projection moved 40 yd west and 19 yd down (each near-plane point 40 yd west of its pixel at height
            // 50): the rays to columns 0, 1 run down, to columns 2, 3 UP -- the sheet's far part above the eye -- yet all meet its
            // upper side (it rises faster than they do): all four HIGH, lerp(W, bed, T(Ls)) with alpha 1 (the old test, the ray
            // going down, drew columns 2, 3 on the fallback: the seam on the horizon row). Moved 40 yd EAST instead, the eye lies
            // below the sheet's plane and sees its lower side: every column the fallback (the type colour x the deep alpha,
            // premultiplied), columns 0, 1 too, whose rays run down (the old test drew them HIGH).
            const float rise = 0.5f;
            Texture2D under = keep(HighDepth((col, row) => f.DepthOf(45f)));
            Func<int, double> sheetY = col => 50.0 + rise * (1 + 2 * col - 4);
            Func<int, double, bool[]> sides = (col, eyeDx) =>
            {
                double vx = -eyeDx, vy = sheetY(col) - 50.0, len1 = Math.Sqrt(vx * vx + vy * vy);
                vx /= len1;
                vy /= len1;
                return new[] { -vy > LiquidHighMath.ViewDown, LiquidHighMath.UpperSide(vx, vy, 0, 2, 2 * rise, 0, 0, 0, -2) };
            };
            string oldWest = "", newWest = "", oldEast = "", newEast = "";
            for (int c = 0; c < 4; c++)
            {
                oldWest += sides(c, -40)[0] ? "H" : "-";
                newWest += sides(c, -40)[1] ? "H" : "-";
                oldEast += sides(c, 40)[0] ? "H" : "-";
                newEast += sides(c, 40)[1] ? "H" : "-";
            }
            px = HighRender(f, m, bed, bed, under, Vector4.zero, out why, null, Matrix4x4.Translate(new Vector3(-40f, -19f, 0f)) * f.InvVp, rise);
            string westMiss;
            bool westHigh = HighPixels(px, (col, i) =>
            {
                double ls = Math.Sqrt(40.0 * 40.0 + Math.Pow(45.0 - 19.0 - sheetY(col), 2));
                var e = new double[4];
                for (int k = 0; k < 3; k++)
                    e[k] = LiquidHighMath.Refracted(LiquidHighMath.WaterNoZone(baseColour[k], noZone), ch(bedTexel(col, 3 - i), k), T(ls));
                e[3] = 1.0;
                return e;
            }, new[] { 0, 1, 2, 3 }, 3e-3, out westMiss);
            Check(westHigh && oldWest == "HH--" && newWest == "HHHH",
                  "high on the GPU (F1): a sheet rising 0.5 yd per yd, seen from 40 yd west at the height of its middle -- the rays to its far " +
                  "half rise above the eye yet meet its upper side: every column HIGH (lerp(W, bed, T(Ls)), alpha 1); columns by the old test " +
                  oldWest + ", by the facing " + newWest + (westHigh ? "" : " -- " + westMiss) + (why != null ? " (" + why + ")" : ""), log);
            px = HighRender(f, m, bed, bed, under, Vector4.zero, out why, null, Matrix4x4.Translate(new Vector3(40f, -19f, 0f)) * f.InvVp, rise);
            double deepA = WmvLiquidMaterials.DeepAlpha;
            string eastMiss;
            bool eastFallback = HighPixels(px, (col, i) => new[] { baseColour[0] * deepA, baseColour[1] * deepA, baseColour[2] * deepA, deepA * deepA },
                                           new[] { 0, 1, 2, 3 }, 3e-3, out eastMiss);
            Check(eastFallback && oldEast == "HH--" && newEast == "----",
                  "high on the GPU (F1): the same sheet seen from 40 yd east, the eye below its plane -- its lower side: every column the " +
                  "fallback (the type colour x the deep alpha), the two whose rays run down too; columns by the old test " + oldEast +
                  ", by the facing " + newEast + (eastFallback ? "" : " -- " + eastMiss) + (why != null ? " (" + why + ")" : ""), log);

            // ---- Fresnel at normal incidence and a miss: flat normals, the bed black, the sky white-ish. The ray goes straight up and
            // leaves the depth range: a miss, the sky. out = sky x f0 (broad + detail); with the gains at 1 / 0 and f0 1: the sky itself.
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiRefractId, (float)tune[LiquidHighTune.Refract]);
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, (float)tune[LiquidHighTune.ReflBroad]);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, (float)tune[LiquidHighTune.ReflDetail]);
            Texture2D black = keep(HighColour((col, row) => new Color32(0, 0, 0, 255)));
            var sky = new Vector4(1f, 0.5f, 0.25f, 1f);
            Texture2D atWater = keep(HighDepth((col, row) => f.DepthOf(50f)));
            px = HighRender(f, m, black, black, atWater, sky, out why);
            double share = LiquidHighMath.Schlick(1, tune[LiquidHighTune.F0]) * (tune[LiquidHighTune.ReflBroad] + tune[LiquidHighTune.ReflDetail]);
            bool fresnel = HighPixels(px, (col, i) => Rgb1(sky.x * share, sky.y * share, sky.z * share), new[] { 0, 1, 2, 3 }, 1e-3, out miss);
            Check(fresnel, string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                         "high on the GPU: at normal incidence the reflection's share is f0 (broad + detail) = {0:0.00000} of the sky{1}", share,
                                         fresnel ? "" : " -- " + miss + (why != null ? " (" + why + ")" : "")), log);
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiF0Id, 1f);
            px = HighRender(f, m, bed, bed, atWater, sky, out why);
            bool skyMiss = HighPixels(px, (col, i) => Rgb1(sky.x, sky.y, sky.z), new[] { 0, 1, 2, 3 }, 1e-3, out miss);
            Check(skyMiss, "high on the GPU: a reflection that misses the scene is _WmvReflectSky, never the bed or the clear colour" +
                           (skyMiss ? "" : " -- " + miss + (why != null ? " (" + why + ")" : "")), log);

            // ---- a hit: the ripple map tilts the reflected ray east (R = (0.959, 0.284, 0.006)); columns 2 and 3 hold a surface 0.6 yd
            // above the water. From column 1 the ray passes under its edge (0.3 yd below it, within the hit thickness): the scene's
            // colour there; from column 0 it rises above it and leaves the screen: the sky (black here).
            m.SetTexture(WmvLiquidMaterials.BumpMapId, bump);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiRefractId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflBumpId, 1f);
            m.SetVector(WmvLiquidMaterials.HiReflMarchId, new Vector4(128f, 0.3f, 0.02f, 0.08f));
            var hitColour = new Color32(200, 120, 40, 255);
            Texture2D scene = keep(HighColour((col, row) => col == 0 ? new Color32(10, 250, 10, 255) : hitColour));
            px = HighRender(f, m, scene, scene, keep(HighDepth((col, row) => f.DepthOf(col < 2 ? 49f : 50.6f))), Vector4.zero, out why);
            bool hit = HighPixels(px, (col, i) => Rgb1(hitColour.r / 255.0, hitColour.g / 255.0, hitColour.b / 255.0), new[] { 1 }, 3e-3, out miss);
            bool away = HighPixels(px, (col, i) => Rgb1(0, 0, 0), new[] { 0 }, 3e-3, out miss2);
            Check(hit && away, "high on the GPU: a reflected ray that passes under a surface within the hit thickness takes the scene's colour " +
                               "there (column 1); one that rises above it and leaves the screen the sky (column 0)" + (hit ? "" : " -- hit: " + miss) +
                               (away ? "" : " -- miss: " + miss2) + (why != null ? " (" + why + ")" : ""), log);

            // ---- H2: the same hit on the reflections' colour premultiplied by a half coverage (a texel at a silhouette: half the
            // scene, half the background): c.rgb + sky (1 - a) -- the sky fills the background's share, never the clear colour.
            var hitSky = new Vector4(1f, 0.5f, 0.25f, 1f);
            var halfHit = new Color32(100, 60, 20, 128);
            Texture2D halfRefl = keep(HighColour((col, row) => col == 0 ? new Color32(10, 250, 10, 255) : halfHit));
            px = HighRender(f, m, scene, halfRefl, keep(HighDepth((col, row) => f.DepthOf(col < 2 ? 49f : 50.6f))), hitSky, out why, halfRefl);
            double share0 = 1.0 - 128 / 255.0;
            bool halfSky = HighPixels(px, (col, i) => Rgb1(100 / 255.0 + hitSky.x * share0, 60 / 255.0 + hitSky.y * share0, 20 / 255.0 + hitSky.z * share0),
                                      new[] { 1 }, 3e-3, out miss);
            bool missSky = HighPixels(px, (col, i) => Rgb1(hitSky.x, hitSky.y, hitSky.z), new[] { 0 }, 3e-3, out miss2);
            Check(halfSky && missSky, "high on the GPU (H2): a hit on a texel half covered by the scene reads half the scene and half the sky " +
                                      "(c.rgb + sky (1 - a)), a miss the sky" + (halfSky ? "" : " -- hit: " + miss) + (missSky ? "" : " -- miss: " + miss2) +
                                      (why != null ? " (" + why + ")" : ""), log);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflBumpId, (float)tune[LiquidHighTune.ReflBump]);
            m.SetVector(WmvLiquidMaterials.HiReflMarchId, new Vector4((float)tune[LiquidHighTune.ReflMaxDist], (float)tune[LiquidHighTune.ReflThick],
                                                                      (float)tune[LiquidHighTune.ReflThickPct], (float)tune[LiquidHighTune.ReflEdge]));

            // ---- H10: broad 1 + detail 1 on Schlick 1 (f0 1): the shares are scaled to 0.5 + 0.5, the refracted share 0 -- a miss
            // is the sky itself (before: 2 x sky - the bed, past 1).
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiF0Id, 1f);
            px = HighRender(f, m, black, black, atWater, hitSky, out why);
            bool limited = HighPixels(px, (col, i) => Rgb1(hitSky.x, hitSky.y, hitSky.z), new[] { 0, 1, 2, 3 }, 1e-3, out miss);
            Check(limited, "high on the GPU (H10): reflection shares summing past 1 are scaled to 1 -- broad 1 + detail 1 at Schlick 1 draws the " +
                           "sky itself, never 2 x the sky" + (limited ? "" : " -- " + miss + (why != null ? " (" + why + ")" : "")), log);
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiF0Id, (float)tune[LiquidHighTune.F0]);

            // ---- H3: Classic Beta's PBR water (MinimapStaticCol (182, 244, 245)) without a zone light, deep (Ls 100: the water colour
            // alone): saturate(colour x noZoneGain) -- the zone gain (here 1.35) never applies; with noZoneGain 1.35 green and blue
            // stop at 1, never past it.
            Material beta = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1250, Name = "PBRWater - Generic - Ocean", Kind = 1,
                                                                                          Material = 1, MaterialFlags = 1, Lvf = 0, MinimapColor = 0xFFB6F4F5 });
            made.Add(beta);
            beta.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            beta.SetFloat(WmvLiquidMaterials.HiReflBroadId, 0f);
            beta.SetFloat(WmvLiquidMaterials.HiReflDetailId, 0f);
            beta.SetFloat(WmvLiquidMaterials.HiWaterGainId, 1.35f);
            double[] betaColour = { 182 / 255.0, 244 / 255.0, 245 / 255.0 };
            Texture2D deep = keep(HighDepth((col, row) => f.DepthOf(-30f)));
            bool betaOk = true;
            string betaMiss = "";
            foreach (float g in new[] { (float)noZone, 1.35f })
            {
                beta.SetFloat(WmvLiquidMaterials.HiNoZoneGainId, g);
                px = HighRender(f, beta, bed, bed, deep, Vector4.zero, out why);
                double tDeep = T(80.0);
                bool ok1 = HighPixels(px, (col, i) =>
                {
                    var e = new double[4];
                    for (int k = 0; k < 3; k++)
                        e[k] = LiquidHighMath.Refracted(LiquidHighMath.WaterNoZone(betaColour[k], g), ch(bedTexel(col, 3 - i), k), tDeep);
                    e[3] = 1.0;
                    return e;
                }, new[] { 0, 1, 2, 3 }, 3e-3, out string m3);
                bool capped = px != null;
                if (px != null)
                    for (int k = 0; k < px.Length; k++)
                        capped &= px[k] <= 1.0f + 1e-6f;
                betaOk &= ok1 && capped;
                if (!(ok1 && capped))
                    betaMiss += string.Format(System.Globalization.CultureInfo.InvariantCulture, " gain {0}: {1}{2}", g, m3, capped ? "" : " (past 1)");
            }
            Check(betaOk, "high on the GPU (H3): without a zone light the water colour is saturate(MinimapStaticCol x noZoneGain) -- Classic Beta's " +
                          "(182, 244, 245) at the default " + noZone + " and at 1.35 (green and blue stop at 1), the zone gain never applied" +
                          betaMiss + (why != null ? " (" + why + ")" : ""), log);

            // ---- the output saturated: the zone light's river pair x a water gain of 10 (W past 2) at Ls 10 -- every channel 1.
            WmvLiquidLight.Sample(LiquidTestLights(0), 50, 50, 0, 0.016, true);
            m.SetFloat(WmvLiquidMaterials.HiWaterGainId, 10f);
            px = HighRender(f, m, bed, bed, keep(HighDepth((col, row) => f.DepthOf(40f))), Vector4.zero, out why);
            bool saturated = HighPixels(px, (col, i) => Rgb1(1, 1, 1), new[] { 0, 1, 2, 3 }, 1e-4, out miss);
            Check(saturated, "high on the GPU: the HIGH output is saturated to the authored 0..1 (a water colour past 1 draws 1, nothing to bloom)" +
                             (saturated ? "" : " -- " + miss + (why != null ? " (" + why + ")" : "")), log);
            m.SetFloat(WmvLiquidMaterials.HiWaterGainId, (float)gain);
            WmvLiquidLight.Off();
            Check(Shader.GetGlobalFloat(WmvWaterGrabPass.WaterGrabId) == 0f,
                  "high on the GPU: _WmvWaterGrab is 0 again after the command-buffer renders (pinned by WmvTerrainComposite)", log);
        }
        finally
        {
            WmvLiquidLight.Off();
            Shader.SetGlobalFloat(WmvLiquidMaterials.LiquidTimeId, savedTime);
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }

    /// <summary>The grab on Camera.main (the world view's camera): a terrain quad on layer 30 under a water quad, far from
    /// everything, rendered by Camera.main into a small target with the pass installed -- the pass ran, its _WmvSceneDepth holds
    /// the quad's depth (copied out by the pass for the test) and the background's, the water did not take the old path,
    /// _WmvWaterGrab is 0 afterwards; the Models view (every layer but 30) is byte-identical with the pass installed and not;
    /// and an ASYMMETRIC view (H11): a red floor under the north half of the picture only, the water over all of it -- the
    /// water shows the floor exactly where the floor is (a vertical flip of the grab's uv on the real camera path would swap
    /// the halves), and the reflections' colour the pass made (H2) carries no colour and no coverage on the background and
    /// full coverage on the floor. Camera.main's state, the pass's installation and every object are put back.</summary>
    static void LiquidHighIntegrationTests(Action<string> log, Shader liquidShader)
    {
        Camera cam = Camera.main;
        Shader terrainShader = WmvTerrainBuilder.ResolveShader(null);
        if (cam == null || liquidShader == null || terrainShader == null)
        {
            log("lifecycle-test SKIP: the grab on Camera.main: " + (cam == null ? "no main camera yet" : "a shader is missing"));
            return;
        }
        const int Size = 64;
        Vector3 origin = new Vector3(-20000f, 20000f, -20000f);
        bool wasActive = WmvWaterGrabPass.Active;
        Vector3 pos = cam.transform.position;
        Quaternion rot = cam.transform.rotation;
        RenderTexture target0 = cam.targetTexture;
        int mask0 = cam.cullingMask;
        float fov0 = cam.fieldOfView, near0 = cam.nearClipPlane, far0 = cam.farClipPlane;
        bool ortho0 = cam.orthographic;
        CameraClearFlags clear0 = cam.clearFlags;
        Color background0 = cam.backgroundColor;
        // Post-processing off for the test (as the host's screenshot renders it): no frame-varying dither between two renders.
        UnityEngine.Rendering.Universal.UniversalAdditionalCameraData camData;
        if (!cam.TryGetComponent(out camData))
            camData = null;
        bool post0 = camData != null && camData.renderPostProcessing;
        RenderTexture previous = RenderTexture.active;
        var made = new List<UnityEngine.Object>();
        RenderTexture target = null, depthCopy = null, reflCopy = null;
        try
        {
            Material ground = new Material(terrainShader) { name = "WmvWaterGrab test ground" };
            made.Add(ground);
            ground.SetFloat("_Cull", (float)UnityEngine.Rendering.CullMode.Off);
            Material water = WmvLiquidMaterials.CreateMaterial(liquidShader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1,
                                                                                               MaterialFlags = 1, Lvf = 0, MinimapColor = 0x9F12A0D5 });
            made.Add(water);
            Func<float, float, Material, int, GameObject> quad = (y, half, mat, layer) =>
            {
                var mesh = new Mesh { name = "WmvWaterGrab test quad" };
                made.Add(mesh);
                var uv1 = new Vector2(1f, -1f);
                mesh.SetVertices(new List<Vector3> { new Vector3(-half, y, -half), new Vector3(half, y, -half), new Vector3(half, y, half), new Vector3(-half, y, half) });
                mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
                mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
                mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
                mesh.RecalculateBounds();
                var go = new GameObject("WmvWaterGrab test quad") { layer = layer };
                made.Add(go);
                go.transform.position = origin;
                go.AddComponent<MeshFilter>().sharedMesh = mesh;
                MeshRenderer mr = go.AddComponent<MeshRenderer>();
                mr.sharedMaterial = mat;
                WmvLiquidMeshes.Quiet(mr);
                return go;
            };
            // A quad over x xa..xb, z za..zb at height y (relative to the test's origin).
            Func<float, float, float, float, float, Material, int, GameObject> rect = (y, xa, xb, za, zb, mat, layer) =>
            {
                var mesh = new Mesh { name = "WmvWaterGrab test rect" };
                made.Add(mesh);
                var uv1 = new Vector2(1f, -1f);
                mesh.SetVertices(new List<Vector3> { new Vector3(xa, y, za), new Vector3(xb, y, za), new Vector3(xb, y, zb), new Vector3(xa, y, zb) });
                mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
                mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
                mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
                mesh.RecalculateBounds();
                var go = new GameObject("WmvWaterGrab test rect") { layer = layer };
                made.Add(go);
                go.transform.position = origin;
                go.AddComponent<MeshFilter>().sharedMesh = mesh;
                MeshRenderer mr = go.AddComponent<MeshRenderer>();
                mr.sharedMaterial = mat;
                WmvLiquidMeshes.Quiet(mr);
                return go;
            };
            GameObject floor = quad(0f, 5f, ground, WmvTerrainBuilder.Layer);
            GameObject sheet = quad(1f, 3f, water, WmvTerrainBuilder.Layer);
            target = new RenderTexture(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear) { name = "WmvWaterGrab test target" };
            target.Create();
            depthCopy = new RenderTexture(Size, Size, 0, RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear) { name = "WmvWaterGrab test depth" };
            depthCopy.Create();
            cam.transform.position = origin + new Vector3(0f, 10f, 0f);
            cam.transform.rotation = Quaternion.Euler(90f, 0f, 0f);
            cam.orthographic = false;
            cam.fieldOfView = 60f;
            cam.nearClipPlane = 0.25f;
            cam.farClipPlane = 100f;
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.1f, 0.1f, 0.12f, 1f);
            cam.targetTexture = target;
            if (camData != null)
                camData.renderPostProcessing = false;
            Func<Color32[]> grab = () =>
            {
                cam.Render();
                RenderTexture.active = target;
                var tex = new Texture2D(Size, Size, TextureFormat.RGBA32, false, true);
                tex.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
                tex.Apply(false);
                RenderTexture.active = previous;
                Color32[] px = tex.GetPixels32();
                UnityEngine.Object.DestroyImmediate(tex);
                return px;
            };

            // ---- the world view: the pass installed and enqueued for Camera.main
            bool installed = WmvWaterGrabPass.SetActive(true);
            WmvWaterGrabPass.ForceForTest = true;
            WmvWaterGrabPass.CaptureDepthForTest(depthCopy);
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            int runs0 = WmvWaterGrabPass.Runs;
            Color32[] high = grab();
            int runs = WmvWaterGrabPass.Runs - runs0;
            float grabAfter = Shader.GetGlobalFloat(WmvWaterGrabPass.WaterGrabId);
            WmvWaterGrabPass.CaptureDepthForTest(null);
            RenderTexture.active = depthCopy;
            var depthTex = new Texture2D(Size, Size, TextureFormat.RFloat, false, true);
            depthTex.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
            depthTex.Apply(false);
            RenderTexture.active = previous;
            Unity.Collections.NativeArray<float> depths = depthTex.GetRawTextureData<float>();
            float centre = depths[(Size / 2) * Size + Size / 2], corner = depths[0];
            UnityEngine.Object.DestroyImmediate(depthTex);
            Matrix4x4 vp = GL.GetGPUProjectionMatrix(cam.projectionMatrix, true) * cam.worldToCameraMatrix;
            Vector4 clip = vp * new Vector4(origin.x, origin.y, origin.z, 1f);
            float want = clip.z / clip.w, farRaw = SystemInfo.usesReversedZBuffer ? 0f : 1f;
            Check(installed && runs == 1 && WmvWaterGrabPass.LastWidth == Size && WmvWaterGrabPass.LastHeight == Size && Mathf.Abs(centre - want) < 1e-4f &&
                  Mathf.Abs(corner - farRaw) < 1e-6f && grabAfter == 0f,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "high on Camera.main: the grab pass ran once for the world view ({0}, {1}x{2}); its _WmvSceneDepth holds the terrain " +
                                "quad under the water (raw {3:0.000000}, expected {4:0.000000}) and the background at the corner ({5:0.000000}); " +
                                "_WmvWaterGrab is 0 after the camera ({6})", runs, WmvWaterGrabPass.LastWidth, WmvWaterGrabPass.LastHeight, centre, want,
                                corner, grabAfter), log);

            // ---- the same view with the pass removed: the water takes its old path, so the centre differs
            WmvWaterGrabPass.SetActive(false);
            Color32[] old = grab();
            int c0 = (Size / 2) * Size + Size / 2;
            Check(!(high[c0].r == old[c0].r && high[c0].g == old[c0].g && high[c0].b == old[c0].b) && WmvWaterGrabPass.Runs - runs0 == 1,
                  "high on Camera.main: the water over the quad differs with the grab and without it (the HIGH path and the old one: " +
                  Px(high[c0]) + " and " + Px(old[c0]) + ")", log);

            // ---- the Models view (every layer but 30), the quads moved onto layer 0: byte-identical with the pass installed and not
            floor.layer = 0;
            sheet.layer = 0;
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            WmvWaterGrabPass.SetActive(true);
            int runsBefore = WmvWaterGrabPass.Runs;
            Color32[] withPass = grab();
            WmvWaterGrabPass.SetActive(false);
            Color32[] without = grab();
            int differ = 0;
            for (int i = 0; i < withPass.Length; i++)
                if (withPass[i].r != without[i].r || withPass[i].g != without[i].g || withPass[i].b != without[i].b || withPass[i].a != without[i].a)
                    differ++;
            Check(differ == 0 && WmvWaterGrabPass.Runs == runsBefore,
                  "high on Camera.main: the Models view is byte-identical with the grab pass installed and not -- never enqueued there (" + differ +
                  " of " + withPass.Length + " pixels differ)", log);

            // ---- H11: the asymmetric view. An opaque red floor (a magma-kind liquid: its base colour) under the north half of the
            // view only -- z 0..8, the top of the picture (the camera looks straight down, its up is +z) -- and the water over all
            // of it (x, z -8..8 at 1 yd). First the floor alone (its pixels: the mask), then with the water.
            floor.SetActive(false);
            sheet.SetActive(false);
            Material red = WmvLiquidMaterials.CreateMaterial(liquidShader, new LiquidTypeView { Id = 19, Name = "Magma", Kind = 2, Material = 2,
                                                                                             MaterialFlags = 0, Lvf = 1, MinimapColor = 0xFFFF0000 });
            made.Add(red);
            rect(0f, -8f, 8f, 0f, 8f, red, WmvTerrainBuilder.Layer);
            GameObject wide = rect(1f, -8f, 8f, -8f, 8f, water, WmvTerrainBuilder.Layer);
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            WmvWaterGrabPass.SetActive(true);
            wide.SetActive(false);
            Color32[] floorOnly = grab();
            wide.SetActive(true);
            reflCopy = new RenderTexture(Size, Size, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear) { name = "WmvWaterGrab test reflection" };
            reflCopy.Create();
            WmvWaterGrabPass.CaptureReflectionForTest(reflCopy);
            int runsA = WmvWaterGrabPass.Runs;
            Color32[] withWater = grab();
            int ranA = WmvWaterGrabPass.Runs - runsA;
            WmvWaterGrabPass.CaptureReflectionForTest(null);
            RenderTexture.active = reflCopy;
            var reflTex = new Texture2D(Size, Size, TextureFormat.RGBAFloat, false, true);
            reflTex.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
            reflTex.Apply(false);
            RenderTexture.active = previous;
            Color[] refl = reflTex.GetPixels();
            UnityEngine.Object.DestroyImmediate(reflTex);

            // The mask (the floor's own red pixels; the readback's row 0 is the picture's bottom), and the water's red over it and
            // elsewhere.
            var mask = new bool[floorOnly.Length];
            int floorN = 0, floorTop = 0;
            double floorR = 0, bgR = 0;
            for (int i = 0; i < floorOnly.Length; i++)
            {
                mask[i] = floorOnly[i].r > 128 && floorOnly[i].g < 60 && floorOnly[i].b < 60;
                if (mask[i])
                {
                    floorN++;
                    if (i / Size >= Size / 2 - 1)
                        floorTop++;
                    floorR += withWater[i].r;
                }
                else
                    bgR += withWater[i].r;
            }
            int all = floorOnly.Length, bgN = all - floorN;
            floorR = floorN > 0 ? floorR / floorN : 0;
            bgR = bgN > 0 ? bgR / bgN : 0;
            double mid = 0.5 * (floorR + bgR);
            int floorAbove = 0, bgBelow = 0;
            for (int i = 0; i < all; i++)
            {
                if (mask[i] && withWater[i].r > mid) floorAbove++;
                if (!mask[i] && withWater[i].r < mid) bgBelow++;
            }
            bool half = floorN > 0.35 * all && floorN < 0.65 * all && floorTop >= 0.98 * floorN;
            bool shows = floorR - bgR > 40 && floorAbove >= 0.95 * floorN && bgBelow >= 0.95 * bgN;
            bool flipped = !shows && bgR - floorR > 40;
            Check(ranA == 1 && half && shows,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "high on Camera.main (H11): an asymmetric view -- the floor under the top half only ({0} of {1} pixels, {2} in the top " +
                                "half): the water shows it exactly there (red {3:0.0} over the floor, {4:0.0} elsewhere; {5} and {6} pixels on their side " +
                                "of {7:0.0}){8}", floorN, all, floorTop, floorR, bgR, floorAbove, bgBelow, mid,
                                flipped ? " -- the halves come out SWAPPED: the grab's uv is flipped on the real camera path" : ""), log);

            // H2 on the real path: every texel of the reflections' colour is the background with no colour and no coverage, or the
            // scene with full coverage; the scene's are the floor's (red), as many as the floor's pixels.
            int none = 0, full = 0, other = 0, redFull = 0;
            foreach (Color c in refl)
            {
                if (c.r == 0f && c.g == 0f && c.b == 0f && c.a == 0f) none++;
                else if (Mathf.Abs(c.a - 1f) < 1e-6f)
                {
                    full++;
                    if (c.r > 0.5f && c.g < 0.1f && c.b < 0.1f) redFull++;
                }
                else other++;
            }
            Check(other == 0 && full == floorN && redFull == full && none == all - floorN,
                  "high on Camera.main (H2): the reflections' colour the pass made is (0, 0, 0, 0) on the background -- no clear colour, no " +
                  "coverage -- and the floor's colour with full coverage on the floor (" + none + " background, " + full + " covered (" + redFull +
                  " red), " + other + " other; the floor " + floorN + " pixels)", log);
        }
        catch (Exception e)
        {
            Check(false, "high on Camera.main: the test threw " + e.GetType().Name + ": " + e.Message, log);
        }
        finally
        {
            WmvWaterGrabPass.ForceForTest = false;
            WmvWaterGrabPass.CaptureDepthForTest(null);
            WmvWaterGrabPass.SetActive(wasActive);
            cam.targetTexture = target0;
            cam.cullingMask = mask0;
            cam.transform.position = pos;
            cam.transform.rotation = rot;
            cam.fieldOfView = fov0;
            cam.nearClipPlane = near0;
            cam.farClipPlane = far0;
            cam.orthographic = ortho0;
            cam.clearFlags = clear0;
            cam.backgroundColor = background0;
            if (camData != null)
                camData.renderPostProcessing = post0;
            RenderTexture.active = previous;
            if (target != null) { target.Release(); UnityEngine.Object.DestroyImmediate(target); }
            if (depthCopy != null) { depthCopy.Release(); UnityEngine.Object.DestroyImmediate(depthCopy); }
            WmvWaterGrabPass.CaptureReflectionForTest(null);
            if (reflCopy != null) { reflCopy.Release(); UnityEngine.Object.DestroyImmediate(reflCopy); }
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }
}
