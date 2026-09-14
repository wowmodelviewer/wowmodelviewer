// WmvWmoBuilder.cs
//
// Turns a parsed world model (WmoParser: the root plus its LOD0 group files) and its decoded material
// textures into a live Unity object: one GameObject per group under one root, one Mesh per group with
// a submesh per MOBA render batch, and one PROVISIONAL material per MOMT entry the batches use.
//
// WHAT THIS IS, AND WHAT IT IS NOT. This is the static foundation for world models: it proves the
// geometry, the group placement, the batch/material boundaries and the primary texture fetch. The
// material is deliberately the archived OpenGL baseline and nothing more:
//
//   texture slot +0x0C when it is non-zero and decoded, otherwise plain white;
//   material flag 0x04 turns culling off;
//   a non-zero blend value is drawn as an alpha key, with the M2 path's cutout (128/255);
//   everything else is opaque.
//
// The client's WMO shader ids, its later texture slots (shader 23 keeps its real textures in the
// tail), its true blend values (2/3/5/6), the material colours and the MOCV vertex colours are all
// parsed, kept in the runtime data and logged per material -- NOT interpreted. A material the baseline
// cannot represent is logged as "unresolved" rather than drawn white in silence, and nothing here maps
// a WMO shader id onto an M2 combiner.
//
// Geometry goes through WowCoordinateConverter exactly as an M2 does (the audit proved WMO group space
// uses the M2 convention): positions, normals, every MOTV set into UV channels 0..3, and the triangle
// winding flipped per batch.
//
// Everything created is tracked by WmvRuntimeMapObject and destroyed by its Dispose: meshes, materials
// and textures are not collected by Unity on their own.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

/// <summary>One material texture of a world-model load: decoded, or why not.</summary>
public class WmvWmoTexture
{
    public uint FileDataID;

    /// <summary>The decoded pixels, or null when the file was missing or could not be decoded. The
    /// load releases the pixels once the build has uploaded what it uses (see ReleasePixels).</summary>
    public BlpImage Image;

    /// <summary>Why Image is null: the request's error or the decoder's.</summary>
    public string Error;

    public int Width, Height;
    public string Encoding = "";
    public bool Decoded;
    /// <summary>Fetched and its BLP header checked, deliberately not decoded: no material samples it yet.</summary>
    public bool HeaderOnly;
    public double DecodeMs;

    /// <summary>
    /// Check a BLP2 file's header the way BlpDecoder does before it decodes -- magic, version, plausible
    /// dimensions, a known colour encoding, a mip 0 inside the file -- and record what it describes.
    /// False, with the reason, for anything the decoder would refuse.
    /// </summary>
    public static bool ReadHeader(byte[] file, WmvWmoTexture into, out string error)
    {
        error = null;
        const int HeaderSize = 0x494;
        if (file == null || file.Length < HeaderSize)
        {
            error = string.Format("header check failed: {0} bytes, smaller than the BLP2 header", file == null ? 0 : file.Length);
            return false;
        }
        if (file[0] != (byte)'B' || file[1] != (byte)'L' || file[2] != (byte)'P' || file[3] != (byte)'2')
        {
            error = "header check failed: not a BLP2 file";
            return false;
        }
        uint version = BitConverter.ToUInt32(file, 4);
        byte colorEncoding = file[8], alphaSize = file[9], preferredFormat = file[10];
        int width = (int)BitConverter.ToUInt32(file, 12), height = (int)BitConverter.ToUInt32(file, 16);
        uint mip0Offset = BitConverter.ToUInt32(file, 20), mip0Size = BitConverter.ToUInt32(file, 20 + 16 * 4);
        if (version != 1 || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
            colorEncoding < 1 || colorEncoding > 3 || mip0Size == 0 || (long)mip0Offset + mip0Size > file.Length)
        {
            error = string.Format("header check failed: version {0}, {1}x{2}, colour encoding {3}, mip 0 at {4}+{5} of {6} bytes",
                                  version, width, height, colorEncoding, mip0Offset, mip0Size, file.Length);
            return false;
        }
        into.Width = width;
        into.Height = height;
        into.Encoding = colorEncoding == 1 ? "palettized/a" + alphaSize
                      : colorEncoding == 3 ? "bgra8888"
                      : (preferredFormat == 0 || alphaSize <= 1 ? "dxt1" : preferredFormat == 1 ? "dxt3" : "dxt5");
        return true;
    }

    /// <summary>Drop the RGBA copy and keep what describes it. A modern WMO names dozens of textures
    /// the provisional material never samples (shader 23's tail), and holding every decoded image for
    /// the life of the object would keep hundreds of megabytes alive for a log line.</summary>
    public void ReleasePixels()
    {
        Image = null;
    }
}

/// <summary>What the provisional material made of one MOMT entry, kept for the log and later stages.</summary>
public struct WmvWmoMaterialInfo
{
    public int Index;
    public uint Flags, Shader, Blend;
    /// <summary>The nine texture slots (+0x0C, +0x18, +0x24, +0x28 .. +0x3C) as raw u32; a slot that is
    /// not a texture reference for this shader id reads 0.</summary>
    public uint[] TextureSlots;
    public uint PrimaryTexture;       // slot +0x0C
    public bool TwoSided;             // flag 0x04
    public bool Cutout;               // blend != 0
    public bool TrueBlend;            // blend other than 0/1: not understood, drawn as the key
    public bool Unresolved;
    public string Verdict;            // "baseline" | "unresolved"
    public string Why;                // the unresolved reasons, "" for baseline
}

/// <summary>A built world model and everything it owns.</summary>
public class WmvRuntimeMapObject
{
    public GameObject Root;
    /// <summary>One per LOD0 group, in GFID order; a group with no render geometry has an object and no
    /// renderer, a group whose file could not be read has null.</summary>
    public GameObject[] GroupObjects = new GameObject[0];
    public Mesh[] Meshes = new Mesh[0];                 // parallel to GroupObjects; null without geometry
    /// <summary>Per group, per submesh: the triangle list as uploaded (winding already flipped).</summary>
    public int[][][] SubmeshTriangles = new int[0][][];
    /// <summary>Per group, per submesh: the MOBA batch index and the resolved MOMT material id.</summary>
    public int[][] SubmeshBatches = new int[0][];
    public int[][] SubmeshMaterialIds = new int[0][];
    /// <summary>Unity materials by MOMT index (null when no drawn batch uses that entry), plus the
    /// fallback for a batch whose material id is past MOMT.</summary>
    public Material[] Materials = new Material[0];
    public Material FallbackMaterial;
    public Texture2D[] Textures = new Texture2D[0];

    /// <summary>The parsed data, kept whole: every UV set, both MOCV colour sets, MOC2, MOPY/MPY2, the
    /// complete 64-byte MOMT records and the undecoded chunk lists. Later material stages read it from
    /// here instead of fetching and parsing again.</summary>
    public WmoRoot RootData;
    public WmoGroup[] Groups = new WmoGroup[0];
    public WmvWmoMaterialInfo[] MaterialInfo = new WmvWmoMaterialInfo[0];
    public Dictionary<uint, WmvWmoTexture> TextureInfo = new Dictionary<uint, WmvWmoTexture>();

    public Bounds Bounds;
    public bool HasBounds;
    public int GroupCount, GroupsMissing, Batches, Submeshes, Renderers;
    public long VertexCount, TriangleCount;
    public int ProvisionalMaterials, UnresolvedMaterials, BlendedMaterials, TrueBlendMaterials;
    public int PrimaryTexturesMissing;
    /// <summary>What the build allocated: managed arrays (vertices, normals, UVs, colours, triangle lists),
    /// the geometry it uploaded, and the textures with their mip chains (RGBA32, 4/3 of the base level).</summary>
    public long ManagedBytes, GpuGeometryBytes, GpuTextureBytes;

    /// <summary>World models built and not yet disposed (see WmvRuntimeModel.Live for why a count).</summary>
    public static int Live { get { return live; } }
    static int live;
    bool disposed;

    public WmvRuntimeMapObject() { live++; }

    public void Dispose()
    {
        if (!disposed) { disposed = true; live--; }
        if (Root != null) UnityEngine.Object.Destroy(Root);
        foreach (var m in Meshes) if (m != null) UnityEngine.Object.Destroy(m);
        foreach (var m in Materials) if (m != null) UnityEngine.Object.Destroy(m);
        if (FallbackMaterial != null) UnityEngine.Object.Destroy(FallbackMaterial);
        foreach (var t in Textures) if (t != null) UnityEngine.Object.Destroy(t);
        Root = null;
        GroupObjects = new GameObject[0];
        Meshes = new Mesh[0];
        Materials = new Material[0];
        FallbackMaterial = null;
        Textures = new Texture2D[0];
    }
}

public static class WmvWmoBuilder
{
    /// <summary>The alpha key of a non-zero blend value: the M2 path's cutout (the legacy combiner keys
    /// at 128/255). The archived WMO path keyed at 0 or 0.3 by flag; neither value is explained, and a
    /// deliberate choice of threshold belongs to the material stage.</summary>
    public const float CutoutThreshold = 128f / 255f;

    /// <summary>Unity has eight UV channels; the format is documented and observed with up to four
    /// MOTV sets, which are what the mesh carries. More are kept in the parsed group and logged.</summary>
    public const int MaxMeshUvSets = 4;

    static readonly string[] SlotNames = { "+0x0C", "+0x18", "+0x24", "+0x28", "+0x2C", "+0x30", "+0x34", "+0x38", "+0x3C" };

    // ------------------------------------------------------------------ material classification

    /// <summary>
    /// The provisional rule and its verdict for one MOMT entry. Pure, so the self-test can reach it.
    ///
    /// "baseline" is data the archived single-texture path describes: textures only in slot +0x0C (or
    /// none) and blend 0 or 1. "unresolved" is everything the baseline visibly cannot represent: any
    /// texture in a later slot (the shader reads more than one texture -- shader 23 with an empty +0x0C
    /// is the known case), or a blend value that is a real blend. Both are drawn with the same provisional
    /// material; the verdict only says whether that drawing can be trusted.
    /// </summary>
    public static WmvWmoMaterialInfo ClassifyMaterial(WmoMaterial m)
    {
        var info = new WmvWmoMaterialInfo
        {
            Index = m.Index, Flags = m.Flags, Shader = m.Shader, Blend = m.BlendMode,
            TextureSlots = new uint[WmoMaterial.TextureSlotOffsets.Length],
            TwoSided = (m.Flags & WmoMaterial.FlagCullDisabled) != 0,
            Cutout = m.BlendMode != 0,
            TrueBlend = m.BlendMode != 0 && m.BlendMode != 1,
        };
        bool later = false;
        for (int s = 0; s < info.TextureSlots.Length; s++)
        {
            info.TextureSlots[s] = m.IsTextureSlot(s) ? m.GetSlot(s) : 0u;
            if (s > 0 && info.TextureSlots[s] != 0) later = true;
        }
        info.PrimaryTexture = info.TextureSlots[0];

        var why = new List<string>();
        if (later && info.PrimaryTexture == 0)
            why.Add("texture slot +0x0C is empty while later slots name textures (drawn white)");
        else if (later)
            why.Add("shader " + m.Shader + " names textures in slots past +0x0C, which the provisional material does not sample");
        if (info.TrueBlend)
            why.Add("blend " + m.BlendMode + " is a blend the baseline does not understand (drawn as an alpha key)");
        info.Unresolved = why.Count > 0;
        info.Verdict = info.Unresolved ? "unresolved" : "baseline";
        info.Why = string.Join("; ", why.ToArray());
        return info;
    }

    /// <summary>A batch's triangle list as the mesh takes it: MOVI[start .. start + count), whole
    /// triangles only, winding flipped for the handedness change. The parser has already proven every
    /// index in the range is a valid vertex.</summary>
    public static int[] BatchTriangles(WmoGroup group, WmoBatch batch)
    {
        int count = batch.TriangleIndexCount;
        var tris = new int[count];
        int start = (int)batch.StartIndex;
        for (int i = 0; i < count; i++)
            tris[i] = group.Indices[start + i];
        WowCoordinateConverter.FlipWinding(tris);
        return tris;
    }

    // ------------------------------------------------------------------ build

    /// <summary>
    /// Build the world model. groups[i] is LOD0 group i (GFID[i]), or null when its file could not be
    /// fetched or parsed; textures maps a texture FileDataID to its decoded image or failure. The root
    /// object is returned INACTIVE: the caller shows it when it replaces what is on screen, so the switch
    /// happens in one frame. A failure destroys whatever was built and rethrows.
    /// </summary>
    public static WmvRuntimeMapObject Build(WmoRoot root, WmoGroup[] groups, Dictionary<uint, WmvWmoTexture> textures,
                                            string objectName, Action<string> log)
    {
        if (root == null)
            throw new WowParseException("wmo builder: no root");
        groups = groups ?? new WmoGroup[0];
        textures = textures ?? new Dictionary<uint, WmvWmoTexture>();

        var rt = new WmvRuntimeMapObject();
        // Every Texture2D the build uploads, from the moment it exists. rt.Textures is only assigned at the
        // end of a successful build, so a failure part-way through would otherwise dispose a runtime that
        // owns none of the textures already uploaded -- and nothing else holds them.
        var ownedTextures = new List<Texture2D>();
        try
        {
            BuildInto(rt, root, groups, textures, objectName, log, ownedTextures);
            return rt;
        }
        catch
        {
            rt.Textures = ownedTextures.ToArray();
            rt.Dispose();
            throw;
        }
    }

    static void BuildInto(WmvRuntimeMapObject rt, WmoRoot root, WmoGroup[] groups,
                          Dictionary<uint, WmvWmoTexture> textures, string objectName, Action<string> log,
                          List<Texture2D> ownedTextures)
    {
        rt.RootData = root;
        rt.Groups = groups;
        rt.TextureInfo = textures;
        rt.GroupCount = groups.Length;
        rt.Root = new GameObject(objectName);
        rt.Root.SetActive(false);

        bool vertexColour = WmvModelBuilder.Debug_.WmoVertexColour;
        WmvModelBuilder.ResolveShader(log);

        // ---- materials: classified up front, created on first use -------------------------------
        int nMat = root.Materials.Length;
        rt.MaterialInfo = new WmvWmoMaterialInfo[nMat];
        for (int i = 0; i < nMat; i++)
            rt.MaterialInfo[i] = ClassifyMaterial(root.Materials[i]);
        rt.Materials = new Material[nMat];
        var batchUses = new int[nMat];
        var textureCache = new Dictionary<long, Texture2D>();
        int outOfRangeBatches = 0;

        rt.GroupObjects = new GameObject[groups.Length];
        rt.Meshes = new Mesh[groups.Length];
        rt.SubmeshTriangles = new int[groups.Length][][];
        rt.SubmeshBatches = new int[groups.Length][];
        rt.SubmeshMaterialIds = new int[groups.Length][];

        bool haveBounds = false;
        Vector3 bmin = Vector3.zero, bmax = Vector3.zero;

        for (int gi = 0; gi < groups.Length; gi++)
        {
            rt.SubmeshTriangles[gi] = new int[0][];
            rt.SubmeshBatches[gi] = new int[0];
            rt.SubmeshMaterialIds[gi] = new int[0];
            WmoGroup g = groups[gi];
            string groupName = GroupName(root, gi);
            if (g == null)
            {
                rt.GroupsMissing++;
                if (log != null)
                    log(string.Format("wmo group {0} '{1}': file missing -- nothing built for it", gi, groupName));
                continue;
            }

            var go = new GameObject("group" + gi + (groupName.Length > 0 ? " " + groupName : ""));
            go.transform.SetParent(rt.Root.transform, false);
            rt.GroupObjects[gi] = go;
            rt.Batches += g.Batches.Length;

            if (!g.HasRenderGeometry)
            {
                if (log != null)
                    log(string.Format("wmo group {0} '{1}': {2} vertices, {3} batches, flags 0x{4:X8} -- no render " +
                                      "geometry, no renderer", gi, groupName, g.VertexCount, g.Batches.Length,
                                      g.Header.Flags));
                continue;
            }

            // ---- vertices ---------------------------------------------------------------------
            int n = g.Positions.Length;
            var positions = new Vector3[n];
            for (int v = 0; v < n; v++)
            {
                float x, y, z;
                WowCoordinateConverter.ConvertPosition(g.Positions[v], out x, out y, out z);
                positions[v] = new Vector3(x, y, z);
            }
            var mesh = new Mesh { name = objectName + "_group" + gi };
            rt.Meshes[gi] = mesh;
            mesh.indexFormat = n > 65535 ? UnityEngine.Rendering.IndexFormat.UInt32 : UnityEngine.Rendering.IndexFormat.UInt16;
            mesh.vertices = positions;
            rt.ManagedBytes += n * 12L + n;              // positions, and the used-vertex mask below
            rt.GpuGeometryBytes += n * 12L;
            bool hasNormals = g.Normals.Length == n;
            if (hasNormals)
            {
                var normals = new Vector3[n];
                for (int v = 0; v < n; v++)
                {
                    float x, y, z;
                    WowCoordinateConverter.ConvertNormal(g.Normals[v], out x, out y, out z);
                    normals[v] = new Vector3(x, y, z);
                }
                mesh.normals = normals;
                rt.ManagedBytes += n * 12L;
            }
            rt.GpuGeometryBytes += n * 12L;             // normals, recalculated or not

            // Every MOTV set, in file order, into its own channel: set 0 is what the provisional
            // material samples; the rest are uploaded so a later material stage needs no rebuild.
            int uvSets = Math.Min(g.TexCoordSets.Length, MaxMeshUvSets);
            for (int s = 0; s < uvSets; s++)
            {
                WowVec2[] src = g.TexCoordSets[s];
                var uvs = new Vector2[n];
                for (int v = 0; v < n && v < src.Length; v++)
                {
                    float u, vv;
                    WowCoordinateConverter.ConvertTexCoord(src[v], out u, out vv);
                    uvs[v] = new Vector2(u, vv);
                }
                mesh.SetUVs(s, uvs);
                rt.ManagedBytes += n * 8L;
                rt.GpuGeometryBytes += n * 8L;
            }

            if (vertexColour)
            {
                // DIAGNOSTIC ONLY. Every drawn group carries colours while the switch is on, white where
                // the group has no set 1, so the multiply never reads an undefined attribute.
                var colors = new Color32[n];
                WmoColorSet set1 = g.ColorSet1;
                for (int v = 0; v < n; v++)
                {
                    if (set1 != null && set1.Count == n)
                        colors[v] = new Color32(set1.Bgra[v * 4 + 2], set1.Bgra[v * 4 + 1], set1.Bgra[v * 4], set1.Bgra[v * 4 + 3]);
                    else
                        colors[v] = new Color32(255, 255, 255, 255);
                }
                mesh.colors32 = colors;
                rt.ManagedBytes += n * 4L;
                rt.GpuGeometryBytes += n * 4L;
            }

            // ---- one submesh per MOBA batch -----------------------------------------------------
            var triSets = new List<int[]>();
            var batchIdx = new List<int>();
            var matIds = new List<int>();
            var mats = new List<Material>();
            bool[] used = new bool[n];
            long groupTris = 0;
            for (int b = 0; b < g.Batches.Length; b++)
            {
                WmoBatch batch = g.Batches[b];
                if (batch.TriangleIndexCount == 0)
                {
                    if (log != null)
                        log(string.Format("wmo group {0} batch {1}: {2} index(es) -- no whole triangle, no submesh",
                                          gi, b, batch.IndexCount));
                    continue;
                }
                int[] tris = BatchTriangles(g, batch);
                rt.ManagedBytes += tris.Length * 4L;
                rt.GpuGeometryBytes += tris.Length * (n > 65535 ? 4L : 2L);
                for (int t = 0; t < tris.Length; t++) used[tris[t]] = true;
                Material mat;
                int id = batch.MaterialId;
                if (id >= 0 && id < nMat)
                {
                    if (rt.Materials[id] == null)
                        rt.Materials[id] = CreateMaterial(root.Materials[id], rt.MaterialInfo[id], textures, textureCache,
                                                          ownedTextures, objectName, rt, log);
                    mat = rt.Materials[id];
                    batchUses[id]++;
                }
                else
                {
                    outOfRangeBatches++;
                    if (rt.FallbackMaterial == null)
                        rt.FallbackMaterial = CreateFallbackMaterial(objectName, log);
                    mat = rt.FallbackMaterial;
                    if (log != null)
                        log(string.Format("wmo group {0} batch {1}: material id {2} is past MOMT ({3} entries) -- " +
                                          "drawn with a plain white opaque material", gi, b, id, nMat));
                }
                triSets.Add(tris);
                batchIdx.Add(b);
                matIds.Add(id);
                mats.Add(mat);
                groupTris += tris.Length / 3;
            }

            if (triSets.Count == 0)
            {
                // Every batch was shorter than a triangle: nothing to draw, so no mesh is kept either.
                UnityEngine.Object.Destroy(mesh);
                rt.Meshes[gi] = null;
                if (log != null)
                    log(string.Format("wmo group {0} '{1}': no batch holds a whole triangle -- no renderer", gi, groupName));
                continue;
            }
            mesh.subMeshCount = triSets.Count;
            for (int s = 0; s < triSets.Count; s++)
                mesh.SetTriangles(triSets[s], s, false);
            if (!hasNormals)
            {
                mesh.RecalculateNormals();
                if (log != null)
                    log(string.Format("wmo group {0}: no MONR -- normals recalculated from the triangles", gi));
            }

            // Bounds of what is DRAWN: the batch vertices, converted, then min/max taken again. A group's
            // MOGP box also covers collision-only geometry, which the viewer does not show.
            bool gAny = false;
            Vector3 gmin = Vector3.zero, gmax = Vector3.zero;
            for (int v = 0; v < n; v++)
            {
                if (!used[v]) continue;
                if (!gAny) { gmin = gmax = positions[v]; gAny = true; }
                else { gmin = Vector3.Min(gmin, positions[v]); gmax = Vector3.Max(gmax, positions[v]); }
            }
            if (gAny)
            {
                var gb = new Bounds();
                gb.SetMinMax(gmin, gmax);
                mesh.bounds = gb;
                if (!haveBounds) { bmin = gmin; bmax = gmax; haveBounds = true; }
                else { bmin = Vector3.Min(bmin, gmin); bmax = Vector3.Max(bmax, gmax); }
            }

            rt.SubmeshTriangles[gi] = triSets.ToArray();
            rt.SubmeshBatches[gi] = batchIdx.ToArray();
            rt.SubmeshMaterialIds[gi] = matIds.ToArray();
            if (triSets.Count > 0)
            {
                go.AddComponent<MeshFilter>().sharedMesh = mesh;
                var renderer = go.AddComponent<MeshRenderer>();
                renderer.sharedMaterials = mats.ToArray();
                rt.Renderers++;
            }
            rt.Submeshes += triSets.Count;
            rt.VertexCount += n;
            rt.TriangleCount += groupTris;

            if (log != null)
            {
                var sets = new System.Text.StringBuilder();
                foreach (WmoColorSet cs in g.ColorSets)
                    sets.Append(sets.Length > 0 ? ", " : "").Append("set ").Append(cs.SetNumber).Append(':').Append(cs.Count);
                log(string.Format(
                    "wmo group {0} '{1}': {2} vertices, {3} batches (A {4} B {5} C {6}) -> {7} submeshes, {8} triangles, " +
                    "UV sets {9}{10} (mesh channels 0..{11}), colour sets [{12}], MOC2 {13}, MOPY {14}, MPY2 {15}, " +
                    "normals {16}, flags 0x{17:X8}, drawn bounds min {18} max {19}",
                    gi, groupName, n, g.Batches.Length, g.Header.BatchCountA, g.Header.BatchCountB, g.Header.BatchCountC,
                    triSets.Count, groupTris, g.TexCoordSets.Length,
                    g.TexCoordSets.Length > MaxMeshUvSets ? " (more than the mesh carries; kept in the data)" : "",
                    Math.Max(uvSets - 1, 0), sets, g.Moc2 != null ? g.Moc2.Length / 4 + " entries" : "no",
                    g.HasMopy ? g.PolyMaterials.Length.ToString() : "no", g.HasMpy2 ? g.PolyMaterials2.Length.ToString() : "no",
                    hasNormals ? "MONR" : "recalculated", g.Header.Flags, gmin, gmax));
            }
        }

        // ---- the per-material record: every MOMT entry, used or not -----------------------------
        for (int i = 0; i < nMat; i++)
        {
            WmvWmoMaterialInfo info = rt.MaterialInfo[i];
            if (batchUses[i] > 0)
            {
                rt.ProvisionalMaterials++;
                if (info.Unresolved) rt.UnresolvedMaterials++;
                if (info.Cutout) rt.BlendedMaterials++;
                if (info.TrueBlend) rt.TrueBlendMaterials++;
            }
            if (log != null)
                log(DescribeMaterial(info, batchUses[i], textures));
        }

        rt.Textures = ownedTextures.ToArray();
        rt.HasBounds = haveBounds;
        if (haveBounds)
        {
            var b = new Bounds();
            b.SetMinMax(bmin, bmax);
            rt.Bounds = b;
        }
        else
            rt.Bounds = new Bounds(Vector3.zero, Vector3.one);

        if (log != null)
        {
            // The root header's box, converted corner by corner, beside the drawn one: a root box much
            // larger than the geometry is a WMO whose MOHD also covers doodads or a skybox.
            float x0, y0, z0, x1, y1, z1;
            WowCoordinateConverter.ConvertPosition(root.Header.BoundsMin, out x0, out y0, out z0);
            WowCoordinateConverter.ConvertPosition(root.Header.BoundsMax, out x1, out y1, out z1);
            log(string.Format(
                "wmo built '{0}': {1} group(s) ({2} missing), {3} renderer(s), {4} batch(es) -> {5} submesh(es) " +
                "(= draw calls per camera render), {6} vertices, {7} triangles, {8} of {9} material(s) used: " +
                "{10} unresolved, {11} alpha-keyed (blend != 0) of which {12} true blend(s), {23} drawn white " +
                "because their +0x0C texture is missing, {13} Texture2D upload(s), {14} batch(es) with a material " +
                "id past MOMT; drawn bounds {15}; MOHD bounds converted min ({16:F2},{17:F2},{18:F2}) max " +
                "({19:F2},{20:F2},{21:F2}){22}",
                objectName, rt.GroupCount, rt.GroupsMissing, rt.Renderers, rt.Batches, rt.Submeshes, rt.VertexCount,
                rt.TriangleCount, rt.ProvisionalMaterials, nMat, rt.UnresolvedMaterials, rt.BlendedMaterials,
                rt.TrueBlendMaterials, rt.Textures.Length, outOfRangeBatches, rt.Bounds,
                Mathf.Min(x0, x1), Mathf.Min(y0, y1), Mathf.Min(z0, z1), Mathf.Max(x0, x1), Mathf.Max(y0, y1),
                Mathf.Max(z0, z1), vertexColour ? "; DIAGNOSTIC -wmvWmoVertexColour ON (MOCV set 1 multiplied in)" : "",
                rt.PrimaryTexturesMissing));
        }
    }

    static string GroupName(WmoRoot root, int gi)
    {
        if (gi < 0 || gi >= root.GroupInfos.Length)
            return "";
        return root.GetGroupName(root.GroupInfos[gi].NameOffset) ?? "";
    }

    /// <summary>
    /// The provisional surface: the renderer's own shader in its plain single-texture state -- no
    /// combiner, no lobes, unit 0 on UV set 0 -- which is the state the M2 builder gives a one-texture
    /// opaque or alpha-keyed batch. The two render states come from the M2 path's own switch
    /// (ApplyBlendMode) for EXACTLY its opaque and alpha-key arms; no other WMO blend value is mapped.
    /// </summary>
    static Material CreateMaterial(WmoMaterial wm, WmvWmoMaterialInfo info, Dictionary<uint, WmvWmoTexture> textures,
                                   Dictionary<long, Texture2D> textureCache, List<Texture2D> owned, string objectName,
                                   WmvRuntimeMapObject rt, Action<string> log)
    {
        Texture2D tex = null;
        if (info.PrimaryTexture != 0)
        {
            WmvWmoTexture t;
            if (textures.TryGetValue(info.PrimaryTexture, out t) && t != null && t.Image != null)
            {
                // The alpha channel only matters to the key. An opaque material discards it on upload,
                // as the M2 path does, so nothing downstream can mistake it for transparency. One upload
                // per (file, alpha treatment); every upload repeats (WMO UVs run far outside 0..1).
                bool dropAlpha = !info.Cutout;
                long key = ((long)info.PrimaryTexture << 1) | (dropAlpha ? 1L : 0L);
                if (!textureCache.TryGetValue(key, out tex))
                {
                    tex = WmvModelBuilder.CreateTexture(t.Image, objectName + "_tex" + info.PrimaryTexture +
                                                        (dropAlpha ? "_opaque" : ""), dropAlpha);
                    owned.Add(tex);     // owned before anything else can throw (see Build)
                    tex.wrapModeU = TextureWrapMode.Repeat;
                    tex.wrapModeV = TextureWrapMode.Repeat;
                    textureCache[key] = tex;
                    rt.GpuTextureBytes += (long)t.Image.Width * t.Image.Height * 4 * 4 / 3;
                    rt.ManagedBytes += (long)t.Image.Width * t.Image.Height * 4;   // CreateTexture's row-flipped copy
                }
            }
            else
                rt.PrimaryTexturesMissing++;
        }

        Material m = NewSurface(objectName + "_mat" + wm.Index, tex, info.Cutout, info.TwoSided);
        return m;
    }

    static Material CreateFallbackMaterial(string objectName, Action<string> log)
    {
        return NewSurface(objectName + "_mat_missing", null, false, false);
    }

    static Material NewSurface(string name, Texture2D tex, bool cutout, bool twoSided)
    {
        Shader shader = WmvModelBuilder.ResolveShader(null);
        var m = new Material(shader != null ? shader : Shader.Find("Hidden/InternalErrorShader")) { name = name };
        if (tex != null) m.mainTexture = tex;
        if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness", 0f);
        else if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness", 0f);
        SetIf(m, "_CombinerMode", 0f);
        SetIf(m, "_FirstUnitLobe", 0f);
        SetIf(m, "_SecondUnitLobe", 0f);
        SetIf(m, "_ThirdUnitLobe", 0f);
        SetIf(m, "_Unit0UV", 0f);
        // The key reads the texture's own alpha; opaque ignores it (and its upload dropped it).
        SetIf(m, "_AlphaMode", cutout ? 1f : 0f);
        SetIf(m, "_AlphaScale", 1f);
        string treatedAs;
        WmvModelBuilder.ApplyBlendMode(m, cutout ? M2BlendMode.AlphaKey : M2BlendMode.Opaque, false,
                                       CutoutThreshold, out treatedAs);
        SetIf(m, "_Cull", (float)(twoSided ? UnityEngine.Rendering.CullMode.Off : UnityEngine.Rendering.CullMode.Back));
        SetIf(m, "_VertexColour", WmvModelBuilder.Debug_.WmoVertexColour ? 1f : 0f);
        return m;
    }

    static void SetIf(Material m, string property, float value)
    {
        if (m.HasProperty(property)) m.SetFloat(property, value);
    }

    /// <summary>One line per MOMT entry: everything later material work needs to see about it.</summary>
    public static string DescribeMaterial(WmvWmoMaterialInfo info, int batchUses, Dictionary<uint, WmvWmoTexture> textures)
    {
        var slots = new System.Text.StringBuilder();
        for (int s = 0; s < info.TextureSlots.Length; s++)
        {
            if (info.TextureSlots[s] == 0) continue;
            slots.Append(slots.Length > 0 ? ", " : "").Append(SlotNames[s]).Append(' ').Append(info.TextureSlots[s]);
            WmvWmoTexture t;
            if (textures != null && textures.TryGetValue(info.TextureSlots[s], out t) && t != null)
                slots.Append(t.Decoded ? string.Format(" ({0}x{1} {2})", t.Width, t.Height, t.Encoding)
                             : t.HeaderOnly ? string.Format(" ({0}x{1} {2}, fetched, not decoded)", t.Width, t.Height, t.Encoding)
                             : " (MISSING: " + (t.Error ?? "no answer") + ")");
        }
        string surface;
        if (info.PrimaryTexture == 0)
            surface = "white (slot +0x0C empty)";
        else
        {
            WmvWmoTexture t;
            if (textures == null || !textures.TryGetValue(info.PrimaryTexture, out t) || t == null)
                t = null;
            if (t != null && t.Decoded)
                surface = "texture " + info.PrimaryTexture;
            else if (t != null && t.HeaderOnly)
                surface = "texture " + info.PrimaryTexture + " (not decoded: nothing draws this material)";
            else
                surface = "white (texture " + info.PrimaryTexture + " missing)";
        }
        return string.Format(
            "wmo material {0}: shader {1} blend {2} flags 0x{3:X8} slots [{4}] -> provisional {5}, {6}, {7}; " +
            "{8} batch(es) -- {9}{10}",
            info.Index, info.Shader, info.Blend, info.Flags, slots.Length > 0 ? slots.ToString() : "none",
            surface, info.Cutout ? "alpha key 128/255" : "opaque", info.TwoSided ? "cull off" : "cull back",
            batchUses, batchUses > 0 ? info.Verdict.ToUpperInvariant() : info.Verdict + " (not drawn by any LOD0 batch)",
            info.Unresolved ? ": " + info.Why : "");
    }
}
