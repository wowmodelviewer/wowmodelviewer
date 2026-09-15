// WmvWmoBuilder.cs
//
// Turns a parsed world model (WmoParser: the root plus its LOD0 group files) and its decoded material
// textures into a live Unity object: one GameObject per group under one root, one Mesh per group with
// a submesh per MOBA render batch, and one material per MOMT entry the batches use.
//
// MATERIALS. Every MOMT entry is first turned into a PLAN by Wow.WmoMaterialSemantics -- the one table
// that says which pixel permutation draws it, which texture slot each sampler register reads on which
// UV channel, the blend/depth/cull/wrap state and whether all of that is established. The plan is then
// realised with the world-model shader (Resources/WmvWmo.shader), never with the M2 combiner shader:
//
//   resolved          an established client case (ids 0, 4, 13 and 16) with blend 0 or 1, cull, clamp
//                     addressing and the F_UNLIT light bypass as the plan says;
//   resolved-partial  the same, with an input the client reads whose source is not established (the
//                     reason codes say which), drawn with the established part -- every id-23, id-7 and
//                     id-5 material is one, their env emissives (and id 23's MOC2 byte-3 lerp) not being
//                     established; so is a material some of whose vertices lack a stream it reads (U-V4),
//                     and one whose F_UNLIT light bypass is drawn in an interior group (U-F3), both added
//                     by this builder once the groups are counted;
//   unresolved        the PROVISIONAL archived baseline the static stage drew -- slot +0x0C on UV
//                     channel 0, a non-zero blend drawn as the 128/255 key, flag 0x04 for culling --
//                     with the reason codes logged per material; or a labelled PROVISIONAL fallback of
//                     an established permutation -- an id-23 material with a layer but no height map
//                     (U-23b), an id-0/16/4/5 material with an empty +0x0C or an id-13/7 material with an
//                     empty layer slot (U-23b), an id-4/5/7/13/23 material with blend 2 or above (U-B2..U-B5,
//                     drawn opaque because its case alpha is 1). No blend value of 2 or more is given a
//                     guessed Src/Dst: the client's blend row is logged with its evidence, never applied.
//
// MOCV set 1, the set-2 RGB, the MOMT colours and the tail slots nobody samples stay parsed, kept in the
// runtime data and logged -- not interpreted. Two vertex streams are uploaded, each only for groups a
// material reading it draws in, because they are material inputs, not light: MOC2 (UV channel 4, the
// four-layer weights) and the alpha of MOCV set 2 (UV channel 5, the two-layer factor). -wmvWmoMaterialDiag prints
// the plan and the created material read back, one line per drawn material, and one line per drawn batch
// (range, submesh, material, queue, depth write, verdict); -wmvWmoView, -wmvWmoUvOverride and
// -wmvWmoOnlyMaterials are verification views (see WmvModelBuilder.Debug_).
//
// Geometry goes through WowCoordinateConverter exactly as an M2 does (the audit proved WMO group space
// uses the M2 convention): positions, normals, every MOTV set into UV channels 0..3, and the triangle
// winding flipped per batch.
//
// Textures are uploaded once per (file, U addressing, V addressing) for the whole load, with their alpha,
// and handed to the GPU without a CPU-readable copy: nothing on the world-model path reads a texel back,
// and a modern WMO samples hundreds of megabytes of images.
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

    /// <summary>Drop the RGBA copy and keep what describes it once it is uploaded. A modern WMO samples
    /// dozens of large textures (a four-layer material alone up to eight), and holding every decoded image
    /// for the life of the object would keep hundreds of megabytes alive for a log line.</summary>
    public void ReleasePixels()
    {
        Image = null;
    }
}

/// <summary>What the material mapping made of one MOMT entry, kept for the log and later stages.</summary>
public struct WmvWmoMaterialInfo
{
    public int Index;
    public uint Flags, Shader, Blend;
    /// <summary>The nine texture slots (+0x0C, +0x18, +0x24, +0x28 .. +0x3C) as raw u32; a slot that is
    /// not a texture reference for this shader id reads 0.</summary>
    public uint[] TextureSlots;
    public uint PrimaryTexture;       // slot +0x0C
    public bool TwoSided;             // culling off (flag 0x04)
    public bool Cutout;               // the draw discards below the 128/255 key
    public bool TrueBlend;            // blend 2 or above: its state is not established, drawn provisionally
    /// <summary>Drawn provisionally: by the archived baseline, or by a labelled fallback of an established
    /// permutation (see WmoMaterialPlan.Provisional).</summary>
    public bool Unresolved;
    /// <summary>"resolved", "resolved-partial" or "unresolved", with reason codes.</summary>
    public string Verdict;
    /// <summary>The plan's notes: every reason and deliberate choice, "" when there are none.</summary>
    public string Why;
    /// <summary>The whole plan: permutation, sampler bindings, render state, resolution.</summary>
    public WmoMaterialPlan Plan;
    /// <summary>Four-layer materials, over the vertices their batches draw (a vertex shared by two batches
    /// counts twice): all of them; those whose MOC2 byte 3 is non-zero, where the client pulls the diffuse
    /// toward a colour this viewer does not know (U-23a); and those with a non-zero stored weight on an
    /// empty layer, where the forced-zero weight may differ from the client (U-23b).</summary>
    public long Moc2Vertices, Moc2AlphaVertices, EmptyLayerWeightVertices;
    /// <summary>Two-layer materials, over the vertices their batches draw (a vertex shared by two batches
    /// counts twice): all of them; those whose set-2 alpha is below 255 (layer 2 shows) and 0 (only layer
    /// 2); and those where va weights a layer whose slot is empty (va below 255 with +0x18 empty, above 0
    /// with +0x0C empty), where the drawn white is not the client's answer (U-23b).</summary>
    public long Set2Vertices, Set2PartialVertices, Set2ZeroVertices, UnboundLayerVertices;
    /// <summary>Four-layer and two-layer materials, over the vertices their batches draw: those that lack a
    /// stream the permutation reads -- a MOC2 or MOCV set-2 entry, or the MOTV set a bound register samples --
    /// and are drawn with the stated default there (U-V4). GapStreams names which streams were missing.</summary>
    public long StreamGapVertices;
    public string GapStreams;
    /// <summary>The batches drawing this material in interior groups (MOGP flag 0x2000). Where the plan's
    /// F_UNLIT light bypass applies, any such batch adds U-F3 (WmoMaterialSemantics.NoteUnlitInInteriorGroups).</summary>
    public int InteriorBatches;
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
    /// <summary>Drawn materials by resolution (see WmoResolution); Provisional counts those drawn
    /// provisionally -- by the archived baseline or by a labelled fallback of an established permutation
    /// (Fallback: an empty register the draw weights, U-23b, or a blend of 2 or above, U-B2..U-B5) --
    /// Blended those with a non-zero blend value, TrueBlend those with blend 2 or above, FourLayer /
    /// TwoLayer those drawn by the four-layer / a two-layer permutation (id 13 or id 7, fallbacks included).
    /// EnvEmissiveOmitted counts those drawn by a permutation whose client case adds an env emissive this
    /// viewer does not draw (ids 23, 7 and 5).</summary>
    public int ResolvedMaterials, PartialMaterials, UnresolvedMaterials;
    public int ProvisionalMaterials, BlendedMaterials, TrueBlendMaterials, FallbackMaterials, FourLayerMaterials;
    public int TwoLayerMaterials, EnvEmissiveOmittedMaterials;
    /// <summary>Groups whose MOC2 was uploaded for a four-layer material, groups whose set-2 alpha was
    /// uploaded for a two-layer material, and groups where such a material reads a stream the group does
    /// not have (MOC2, MOCV set 2, or a MOTV set) -- each group counted once: drawn with a stated default
    /// there, logged per group and, as U-V4 with a vertex count, per material.</summary>
    public int Moc2Groups, Set2Groups, StreamGapGroups;
    /// <summary>Samplers whose texture slot names a file that could not be fetched or decoded (drawn white).</summary>
    public int PrimaryTexturesMissing;
    /// <summary>Samplers of drawn materials whose slot is empty (drawn with the register's white default).</summary>
    public int EmptySamplerSlots;
    /// <summary>The shader the materials were created with ("WMV/Map Object", or the baseline fallback).</summary>
    public string MaterialShader = "";
    /// <summary>What the build allocated: managed arrays (vertices, normals, UVs, colours, triangle lists),
    /// the geometry it uploaded, and the textures with their mip chains (RGBA32, 4/3 of the base level; GPU
    /// only, no CPU-readable copy is kept).</summary>
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
    /// <summary>The client's map-object alpha-test constant (128/255), which the M2 path's cutout happens
    /// to equal. Kept under this name for callers of the static stage.</summary>
    public const float CutoutThreshold = WmoMaterialSemantics.AlphaKeyThreshold;

    /// <summary>Unity has eight UV channels; the format is documented and observed with up to four
    /// MOTV sets, which are what the mesh carries. More are kept in the parsed group and logged.</summary>
    public const int MaxMeshUvSets = 4;

    /// <summary>The world-model shader under Assets/Resources (never stripped from a player build).</summary>
    public const string MapObjectShaderResource = "WmvWmo";

    /// <summary>Sampler registers the world-model shader declares (_WmoTex0 .. _WmoTex8).</summary>
    public const int ShaderRegisters = 9;

    /// <summary>The mesh UV channel that carries MOC2 as (byte 2, byte 1, byte 0, byte 3) / 255 -- the first
    /// channel after the four MOTV sets; the shader reads it as TEXCOORD4.</summary>
    public const int Moc2UvChannel = 4;

    /// <summary>The mesh UV channel that carries the alpha of MOCV set 2 as (alpha / 255, 0) -- the channel
    /// after MOC2; the shader reads it as TEXCOORD5.</summary>
    public const int Set2AlphaUvChannel = 5;

    // ------------------------------------------------------------------ material classification

    /// <summary>
    /// The plan and its verdict for one MOMT entry (see Wow.WmoMaterialSemantics). Pure, so the self-test
    /// and the fetch policy reach the same answer the builder draws.
    /// </summary>
    public static WmvWmoMaterialInfo ClassifyMaterial(WmoMaterial m)
    {
        WmoMaterialPlan plan = WmoMaterialSemantics.Plan(m);
        return new WmvWmoMaterialInfo
        {
            Index = m.Index, Flags = m.Flags, Shader = m.Shader, Blend = m.BlendMode,
            TextureSlots = plan.TextureSlots,
            PrimaryTexture = plan.TextureSlots[0],
            TwoSided = plan.CullOff,
            Cutout = plan.AlphaTest,
            TrueBlend = m.BlendMode >= 2,
            Unresolved = plan.Provisional,
            Verdict = plan.Verdict,
            Why = string.Join("; ", plan.Notes),
            Plan = plan,
        };
    }

    // ------------------------------------------------------------------ the world-model shader

    static Shader mapObjectShader;
    static bool mapObjectShaderResolved;

    /// <summary>
    /// Resources/WmvWmo.shader, once per session. Null when the player was built without it (an old
    /// Resources copy): the materials then fall back to the archived baseline on the renderer's own
    /// opaque shader, and the log says so, rather than drawing magenta.
    /// </summary>
    public static Shader ResolveMapObjectShader(Action<string> log)
    {
        if (mapObjectShaderResolved)
            return mapObjectShader;
        mapObjectShaderResolved = true;
        Shader s = Resources.Load<Shader>(MapObjectShaderResource);
        // The error shader is what a broken import hands back; it is never an answer.
        if (s != null && s.name != null && s.name.StartsWith("Hidden/"))
            s = null;
        mapObjectShader = s;
        if (log != null)
            log(s != null
                ? "wmo: materials use the world-model shader '" + s.name + "' (Resources/" + MapObjectShaderResource + ")"
                : "wmo: Resources/" + MapObjectShaderResource + ".shader is missing from this player build -- every world-model " +
                  "material falls back to the archived baseline on the renderer's opaque shader. Copy Assets/Resources/ " +
                  "into the Unity project and rebuild the player.");
        return mapObjectShader;
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
        Shader wmoShader = ResolveMapObjectShader(log);
        if (wmoShader == null)
            WmvModelBuilder.ResolveShader(log);     // the baseline fallback's shader
        rt.MaterialShader = wmoShader != null ? wmoShader.name : "baseline fallback (" + MapObjectShaderResource + " missing)";

        // ---- materials: planned up front, created on first use -----------------------------------
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

            // MOTV set k+1 into mesh UV channel k (k = 0..3), in file order; the material plan names the
            // channel each sampler register reads (sets 1..4 for id 23's layers and heights, sets 1 and 2
            // for ids 13 and 7, set 1 otherwise), so every set the group has is uploaded.
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

            // MOC2 and the set-2 alpha, each only where a batch's plan reads it (the four-layer weights, the
            // two-layer factor): every other material ignores those channels, so a group without such a
            // batch keeps exactly the streams it had. Raw bytes, no fix-up -- no source applies one to
            // MOC2, and the format documentation excludes set 2 from the colour fix-up by name.
            bool readsMoc2 = false, readsSet2 = false, streamGap = false;
            // The highest MOTV channel a bound texture of each kind reads, kept apart so a gap is reported
            // against the kind of material that actually has it.
            int highestUvFour = -1, highestUvTwo = -1;
            foreach (WmoBatch batch in g.Batches)
            {
                int id = batch.MaterialId;
                if (batch.TriangleIndexCount == 0 || id < 0 || id >= nMat)
                    continue;
                WmoMaterialPlan bp = rt.MaterialInfo[id].Plan;
                if (!bp.ReadsMoc2 && !bp.ReadsSet2Alpha)
                    continue;
                readsMoc2 |= bp.ReadsMoc2;
                readsSet2 |= bp.ReadsSet2Alpha;
                int highest = HighestSampledUvChannel(bp);
                if (bp.ReadsMoc2) highestUvFour = Math.Max(highestUvFour, highest);
                else highestUvTwo = Math.Max(highestUvTwo, highest);
            }
            if (readsSet2)
            {
                WmoColorSet set2 = g.ColorSet2;
                int entries = set2 != null ? set2.Count : 0;
                var factor = new Vector2[n];
                for (int v = 0; v < n; v++)
                    // A vertex the set does not cover reads 1, i.e. layer 1 (+0x0C), which is what the archived
                    // baseline drew there. What the client reads for an absent stream is not known (U-V4).
                    factor[v] = new Vector2(v < entries ? WmoMaterialSemantics.SetTwoAlpha(set2.Bgra[v * 4 + 3]) : 1f, 0f);
                mesh.SetUVs(Set2AlphaUvChannel, factor);
                rt.Set2Groups++;
                rt.ManagedBytes += n * 8L;
                rt.GpuGeometryBytes += n * 8L;
                if (entries < n)
                {
                    // Retail puts every id-13 batch in a group with MOCV set 2, so this is a malformed or
                    // unusual file.
                    streamGap = true;
                    if (log != null)
                        log(string.Format("wmo group {0} '{1}': a two-layer material reads MOCV set-2 alpha for {2} vertices, " +
                                          "the group has {3} -- the rest drawn with va = 1 (layer +0x0C, the archived baseline's " +
                                          "texture); what the client reads for an absent stream is unknown (U-V4)", gi, groupName, n, entries));
                }
                if (uvSets <= highestUvTwo)
                {
                    streamGap = true;
                    if (log != null)
                        log(string.Format("wmo group {0} '{1}': a two-layer material reads MOTV set {2}, the group has {3} -- drawn " +
                                          "with coordinate 0 there, what the client reads for an absent stream is unknown (U-V4)",
                                          gi, groupName, highestUvTwo + 1, uvSets));
                }
            }
            if (readsMoc2)
            {
                byte[] moc2 = g.Moc2;
                int entries = moc2 != null ? moc2.Length / 4 : 0;
                var weights = new Vector4[n];
                for (int v = 0; v < n && v < entries; v++)
                    weights[v] = new Vector4(moc2[v * 4 + 2] / 255f, moc2[v * 4 + 1] / 255f, moc2[v * 4] / 255f,
                                             moc2[v * 4 + 3] / 255f);
                mesh.SetUVs(Moc2UvChannel, weights);
                rt.Moc2Groups++;
                rt.ManagedBytes += n * 16L;
                rt.GpuGeometryBytes += n * 16L;
                if (entries < n || uvSets <= highestUvFour)
                {
                    // Retail puts every id-23 batch in a group with four MOTV sets and MOC2, so this is a
                    // malformed or unusual file. The missing stream reads zeros (all weight on layer 4, or
                    // coordinate 0); what the client would read is not known (U-V4).
                    streamGap = true;
                    if (log != null)
                        log(string.Format("wmo group {0} '{1}': a four-layer material reads {2}{3}{4} -- drawn with zeros " +
                                          "there, what the client reads for an absent stream is unknown (U-V4)", gi, groupName,
                                          entries < n ? string.Format("MOC2 for {0} vertices, the group has {1} entries", n, entries) : "",
                                          entries < n && uvSets <= highestUvFour ? " and " : "",
                                          uvSets <= highestUvFour ? string.Format("MOTV set {0}, the group has {1}", highestUvFour + 1, uvSets) : ""));
                }
            }
            if (streamGap)
                rt.StreamGapGroups++;

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
                    if ((g.Header.Flags & WmoGroupFlags.Indoor) != 0)
                        rt.MaterialInfo[id].InteriorBatches++;
                    if (rt.MaterialInfo[id].Plan.ReadsMoc2)
                        CountFourLayerInputs(rt, g, id, b, gi, tris, log);
                    if (rt.MaterialInfo[id].Plan.ReadsSet2Alpha)
                        CountTwoLayerInputs(rt, g, id, b, gi, tris, log);
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
        bool diag = WmvModelBuilder.Debug_.WmoMaterialDiag;
        int usedMaterials = 0;
        for (int i = 0; i < nMat; i++)
        {
            WmvWmoMaterialInfo info = rt.MaterialInfo[i];
            if (info.StreamGapVertices > 0 || info.InteriorBatches > 0)
            {
                // Only the data says whether a stream the permutation reads is missing, and only the groups
                // whether a light-bypassed batch is interior, so those codes join the plan here, before anything
                // counts or prints the verdict. Neither changes what is drawn.
                WmoMaterialSemantics.NoteAbsentStream(info.Plan, info.StreamGapVertices, info.GapStreams);
                WmoMaterialSemantics.NoteUnlitInInteriorGroups(info.Plan, info.InteriorBatches);
                info.Verdict = info.Plan.Verdict;
                info.Why = string.Join("; ", info.Plan.Notes);
                rt.MaterialInfo[i] = info;
            }
            if (batchUses[i] > 0)
            {
                usedMaterials++;
                switch (info.Plan.Resolution)
                {
                    case WmoResolution.Resolved: rt.ResolvedMaterials++; break;
                    case WmoResolution.ResolvedPartial: rt.PartialMaterials++; break;
                    default: rt.UnresolvedMaterials++; break;
                }
                if (info.Plan.Provisional) rt.ProvisionalMaterials++;
                if (info.Plan.ProvisionalFallback) rt.FallbackMaterials++;
                if (info.Plan.Permutation == WmoPermutation.FourLayer) rt.FourLayerMaterials++;
                if (IsTwoLayer(info.Plan.Permutation)) rt.TwoLayerMaterials++;
                if (OmitsEnvEmissive(info.Plan.Permutation)) rt.EnvEmissiveOmittedMaterials++;
                if (info.Blend != 0) rt.BlendedMaterials++;
                if (info.TrueBlend) rt.TrueBlendMaterials++;
            }
            if (log != null)
            {
                log(DescribeMaterial(info, batchUses[i], textures));
                // The diagnostic covers what is DRAWN: an entry no LOD0 batch uses has no Unity material.
                if (diag && batchUses[i] > 0)
                    log(DescribeMaterialDiag(info, rt.Materials[i], batchUses[i], textures));
            }
        }

        // ---- the per-batch record: which object draws each batch, once every verdict is final ------------
        if (log != null && diag)
            for (int gi = 0; gi < groups.Length; gi++)
                for (int k = 0; groups[gi] != null && k < rt.SubmeshBatches[gi].Length; k++)
                    log(DescribeBatchDiag(rt, groups[gi], gi, rt.SubmeshBatches[gi][k], k, rt.SubmeshMaterialIds[gi][k]));

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
                "(= draw calls per camera render), {6} vertices, {7} triangles, {8} of {9} material(s) used, drawn with " +
                "{24}: {25} resolved, {26} resolved-partial, {10} unresolved ({27} drawn " +
                "PROVISIONALLY, {29} of them by a labelled fallback), {30} four-layer (MOC2 uploaded for {31} group(s)), " +
                "{34} two-layer (MOCV set-2 alpha uploaded for {35} group(s)), {36} drawn without the env emissive of " +
                "their client case (U-G1/U-E2/U-E3/U-E4/U-P1), {32} group(s) with a stream gap (a stream such a " +
                "material reads is missing, U-V4), {11} with a non-zero blend of which {12} blend 2+ (state " +
                "unresolved, client blend row logged only), {23} sampler(s) drawn white because their texture is missing, {28} sampler(s) with an empty " +
                "slot, {13} Texture2D upload(s) (GPU only, no CPU-readable copy), {14} batch(es) with a material id past MOMT; drawn bounds {15}; MOHD " +
                "bounds converted min ({16:F2},{17:F2},{18:F2}) max ({19:F2},{20:F2},{21:F2}){22}; not applied to any " +
                "material: MOCV set-1 shading and the set-2 RGB (U-V1..U-V5), MOMT colours (U-C6, U-F2), env emissives " +
                "(U-G1, U-E2, U-E3, U-E4, U-P1), blend factors of 2 and above (U-B2..U-B5, U-B7){33}",
                objectName, rt.GroupCount, rt.GroupsMissing, rt.Renderers, rt.Batches, rt.Submeshes, rt.VertexCount,
                rt.TriangleCount, usedMaterials, nMat, rt.UnresolvedMaterials, rt.BlendedMaterials,
                rt.TrueBlendMaterials, rt.Textures.Length, outOfRangeBatches, rt.Bounds,
                Mathf.Min(x0, x1), Mathf.Min(y0, y1), Mathf.Min(z0, z1), Mathf.Max(x0, x1), Mathf.Max(y0, y1),
                Mathf.Max(z0, z1), vertexColour ? "; DIAGNOSTIC -wmvWmoVertexColour ON (MOCV set 1 multiplied in)" : "",
                rt.PrimaryTexturesMissing, rt.MaterialShader, rt.ResolvedMaterials, rt.PartialMaterials,
                rt.ProvisionalMaterials, rt.EmptySamplerSlots, rt.FallbackMaterials,
                rt.FourLayerMaterials, rt.Moc2Groups, rt.StreamGapGroups, DiagnosticViewNote(), rt.TwoLayerMaterials,
                rt.Set2Groups, rt.EnvEmissiveOmittedMaterials));
        }
    }

    /// <summary>
    /// A four-layer batch's MOC2 inputs over the vertices it draws: how many carry a non-zero byte 3 (where
    /// the client lerps the diffuse toward a colour not known here, U-23a) and how many a non-zero stored
    /// weight on a layer whose slot is empty (where the forced-zero weight may not be the client's, U-23b).
    /// Summed per material for the material line, so the log says where the open questions touch pixels;
    /// under -wmvWmoMaterialDiag one line per batch adds the mean stored weights, in the order the file
    /// stores them for layers 1..4, so the weight mapping can be checked against the data.
    /// </summary>
    static void CountFourLayerInputs(WmvRuntimeMapObject rt, WmoGroup g, int id, int batchIndex, int gi, int[] tris,
                                     Action<string> log)
    {
        WmoMaterialPlan plan = rt.MaterialInfo[id].Plan;
        byte[] moc2 = g.Moc2;
        int entries = moc2 != null ? moc2.Length / 4 : 0;
        int uvSets = Math.Min(g.TexCoordSets.Length, MaxMeshUvSets);
        bool uvGap = uvSets <= HighestSampledUvChannel(plan);
        var seen = new bool[g.VertexCount];
        long vertices = 0, withEntry = 0, alpha = 0, emptyWeight = 0, gap = 0, moc2Gap = 0;
        double sum1 = 0, sum2 = 0, sum3 = 0, sum4 = 0, sumA = 0;
        // Per layer, over the vertices whose stored weight for that layer is non-zero: the range of the MOTV
        // set the layer reads. A span of 0 means the layer samples a single texel there, which a flat-looking
        // surface otherwise cannot tell apart from a wrong set.
        var uMin = new float[] { float.MaxValue, float.MaxValue, float.MaxValue, float.MaxValue };
        var uMax = new float[] { float.MinValue, float.MinValue, float.MinValue, float.MinValue };
        var vMin = new float[] { float.MaxValue, float.MaxValue, float.MaxValue, float.MaxValue };
        var vMax = new float[] { float.MinValue, float.MinValue, float.MinValue, float.MinValue };
        bool diag = log != null && WmvModelBuilder.Debug_.WmoMaterialDiag;
        foreach (int v in tris)
        {
            if (seen[v]) continue;
            seen[v] = true;
            vertices++;
            if (v >= entries || uvGap) gap++;
            if (v >= entries) { moc2Gap++; continue; }     // reads zeros; the group line has already said so
            withEntry++;
            int w1 = moc2[v * 4 + 2], w2 = moc2[v * 4 + 1], w3 = moc2[v * 4], a = moc2[v * 4 + 3];
            int w4 = Math.Max(0, 255 - (w1 + w2 + w3));   // 1 - saturate(sum), in bytes
            if (a != 0) alpha++;
            if ((plan.LayerMask[0] == 0f && w1 != 0) || (plan.LayerMask[1] == 0f && w2 != 0) ||
                (plan.LayerMask[2] == 0f && w3 != 0) || (plan.LayerMask[3] == 0f && w4 != 0))
                emptyWeight++;
            sum1 += w1; sum2 += w2; sum3 += w3; sum4 += w4; sumA += a;
            if (diag)
            {
                int[] w = { w1, w2, w3, w4 };
                for (int k = 0; k < LayerCountForSpan; k++)
                {
                    if (w[k] == 0 || k >= g.TexCoordSets.Length || v >= g.TexCoordSets[k].Length) continue;
                    WowVec2 t = g.TexCoordSets[k][v];
                    uMin[k] = Math.Min(uMin[k], t.X); uMax[k] = Math.Max(uMax[k], t.X);
                    vMin[k] = Math.Min(vMin[k], t.Y); vMax[k] = Math.Max(vMax[k], t.Y);
                }
            }
        }
        rt.MaterialInfo[id].Moc2Vertices += vertices;
        rt.MaterialInfo[id].Moc2AlphaVertices += alpha;
        rt.MaterialInfo[id].EmptyLayerWeightVertices += emptyWeight;
        AddStreamGap(ref rt.MaterialInfo[id], gap, (moc2Gap > 0 ? "MOC2" : "") + (moc2Gap > 0 && uvGap ? " and " : "") +
                                                   (uvGap ? "MOTV set " + (HighestSampledUvChannel(plan) + 1) : ""));
        if (diag)
        {
            double k = withEntry > 0 ? 1.0 / withEntry : 0.0;
            var spans = new System.Text.StringBuilder();
            for (int l = 0; l < LayerCountForSpan; l++)
            {
                spans.Append(l > 0 ? "; " : "").Append(l + 1).Append(": ");
                if (uMax[l] < uMin[l]) spans.Append("unweighted");
                else spans.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, "{0:G4} x {1:G4}",
                                        uMax[l] - uMin[l], vMax[l] - vMin[l]);
            }
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "wmo material-diag four-layer batch g{0}.b{1} material {2}: {3} vertices ({4} with a MOC2 entry), mean stored " +
                "weights layers 1/2/3/4 = {5:F0}/{6:F0}/{7:F0}/{8:F0} of 255, byte 3 mean {9:F0}; byte 3 > 0 on {10} (U-23a), " +
                "non-zero stored weight on an empty layer on {11} (U-23b); layer mask ({12},{13},{14},{15}); UV span (u x v) of " +
                "each layer's MOTV set over the vertices that weight it [{16}]{17}",
                gi, batchIndex, id, vertices, withEntry, sum1 * k, sum2 * k, sum3 * k, sum4 * k, sumA * k, alpha, emptyWeight,
                plan.LayerMask[0], plan.LayerMask[1], plan.LayerMask[2], plan.LayerMask[3], spans,
                gap > 0 ? string.Format("; {0} vertices read an absent stream (U-V4)", gap) : ""));
        }
    }

    /// <summary>Layers whose MOTV-set span the four-layer diagnostic reports (layer k reads set k).</summary>
    const int LayerCountForSpan = 4;

    /// <summary>The highest mesh UV channel a register the plan binds to a texture reads, or -1. A register
    /// left at its white default reads a constant whatever coordinate it gets, so it cannot need a set.</summary>
    static int HighestSampledUvChannel(WmoMaterialPlan plan)
    {
        int highest = -1;
        foreach (WmoSamplerBinding sb in plan.Samplers)
            if (!sb.Unread && sb.FileDataID != 0)
                highest = Math.Max(highest, sb.UvChannel);
        return highest;
    }

    /// <summary>Add a batch's absent-stream vertices, and the streams they lack, to its material's record.</summary>
    static void AddStreamGap(ref WmvWmoMaterialInfo info, long vertices, string streams)
    {
        if (vertices <= 0)
            return;
        info.StreamGapVertices += vertices;
        if (string.IsNullOrEmpty(info.GapStreams))
            info.GapStreams = streams;
        else if (info.GapStreams.IndexOf(streams, StringComparison.Ordinal) < 0)
            info.GapStreams += ", " + streams;
    }

    /// <summary>
    /// A two-layer batch's set-2 alpha over the vertices it draws: how many show layer 2 at all (va below
    /// 255), how many only layer 2 (va 0), and how many weight a layer whose slot is empty (where the drawn
    /// white is a placeholder, U-23b). Summed per material for the material line; under
    /// -wmvWmoMaterialDiag one line per batch adds the va range and mean and how far MOTV set 2 differs
    /// from set 1 on those vertices -- a batch whose sets are equal cannot show which set layer 2 reads.
    /// </summary>
    static void CountTwoLayerInputs(WmvRuntimeMapObject rt, WmoGroup g, int id, int batchIndex, int gi, int[] tris,
                                    Action<string> log)
    {
        WmoMaterialPlan plan = rt.MaterialInfo[id].Plan;
        WmoColorSet set2 = g.ColorSet2;
        int entries = set2 != null ? set2.Count : 0;
        bool layer1Empty = plan.TextureSlots[0] == 0, layer2Empty = plan.TextureSlots[1] == 0;
        WowVec2[] uv1 = g.TexCoordSets.Length > 0 ? g.TexCoordSets[0] : null;
        WowVec2[] uv2 = g.TexCoordSets.Length > 1 ? g.TexCoordSets[1] : null;
        int uvSets = Math.Min(g.TexCoordSets.Length, MaxMeshUvSets);
        bool uvGap = uvSets <= HighestSampledUvChannel(plan);
        var seen = new bool[g.VertexCount];
        long vertices = 0, withEntry = 0, partial = 0, zero = 0, unbound = 0, gap = 0, set2Gap = 0;
        int min = 255, max = 0;
        double sum = 0, uvDiff = 0;
        foreach (int v in tris)
        {
            if (seen[v]) continue;
            seen[v] = true;
            vertices++;
            // A vertex the set does not cover is drawn with va = 1 (the group line has already said so).
            int a = v < entries ? set2.Bgra[v * 4 + 3] : 255;
            if (v < entries) withEntry++;
            else set2Gap++;
            if (v >= entries || uvGap) gap++;
            if (a < 255) partial++;
            if (a == 0) zero++;
            if ((layer2Empty && a < 255) || (layer1Empty && a > 0)) unbound++;
            min = Math.Min(min, a);
            max = Math.Max(max, a);
            sum += a;
            if (uv1 != null && uv2 != null && v < uv1.Length && v < uv2.Length)
                uvDiff = Math.Max(uvDiff, Math.Max(Math.Abs(uv2[v].X - uv1[v].X), Math.Abs(uv2[v].Y - uv1[v].Y)));
        }
        rt.MaterialInfo[id].Set2Vertices += vertices;
        rt.MaterialInfo[id].Set2PartialVertices += partial;
        rt.MaterialInfo[id].Set2ZeroVertices += zero;
        rt.MaterialInfo[id].UnboundLayerVertices += unbound;
        AddStreamGap(ref rt.MaterialInfo[id], gap, (set2Gap > 0 ? "MOCV set 2" : "") + (set2Gap > 0 && uvGap ? " and " : "") +
                                                   (uvGap ? "MOTV set " + (HighestSampledUvChannel(plan) + 1) : ""));
        if (log != null && WmvModelBuilder.Debug_.WmoMaterialDiag)
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "wmo material-diag two-layer batch g{0}.b{1} material {2}: {3} vertices ({4} with a MOCV set-2 entry), set-2 " +
                "alpha min {5} max {6} mean {7:F0} of 255; below 255 (layer 2 shows) on {8}, 0 (layer 2 only) on {9}; va weighting " +
                "an empty layer slot on {10} (U-23b); max |MOTV set 2 - set 1| {11:G4}{12}{13}",
                gi, batchIndex, id, vertices, withEntry, vertices > 0 ? min : 0, max, vertices > 0 ? sum / vertices : 0.0,
                partial, zero, unbound, uvDiff, uv2 == null ? " (the group has no MOTV set 2)" : "",
                gap > 0 ? string.Format("; {0} vertices read an absent stream (U-V4)", gap) : ""));
    }

    /// <summary>The permutations that draw the two-layer lerp: case 13, and the diffuse of case 7.</summary>
    public static bool IsTwoLayer(WmoPermutation p)
    {
        return p == WmoPermutation.TwoLayer || p == WmoPermutation.TwoLayerEnvMetal;
    }

    /// <summary>The permutations whose client case adds an env emissive that is not drawn (ids 23, 7, 5).</summary>
    public static bool OmitsEnvEmissive(WmoPermutation p)
    {
        return p == WmoPermutation.FourLayer || p == WmoPermutation.TwoLayerEnvMetal || p == WmoPermutation.EnvMetal;
    }

    static string GroupName(WmoRoot root, int gi)
    {
        if (gi < 0 || gi >= root.GroupInfos.Length)
            return "";
        return root.GetGroupName(root.GroupInfos[gi].NameOffset) ?? "";
    }

    /// <summary>
    /// Realise a material plan. With the world-model shader: each sampler register bound to its slot's
    /// texture (uploaded once per file and addressing, GPU-only), its mesh UV channel, the
    /// permutation, the render state and the light bypass -- all read from the plan, nothing decided here.
    /// Without the shader (a player built from an old Resources copy) every material takes the archived
    /// baseline on the renderer's opaque shader: a resolved rule the shader cannot express is not drawn
    /// half-way, and the build log has already said why.
    /// </summary>
    static Material CreateMaterial(WmoMaterial wm, WmvWmoMaterialInfo info, Dictionary<uint, WmvWmoTexture> textures,
                                   Dictionary<long, Texture2D> textureCache, List<Texture2D> owned, string objectName,
                                   WmvRuntimeMapObject rt, Action<string> log)
    {
        WmoMaterialPlan plan = info.Plan;
        string name = objectName + "_mat" + wm.Index;
        Shader shader = ResolveMapObjectShader(null);
        if (shader == null)
        {
            bool key = wm.BlendMode != 0;
            Texture2D baseTex = null;
            if (info.PrimaryTexture != 0)
            {
                // The renderer's opaque shader cannot be told which terms read alpha, so here an opaque
                // material's upload drops the channel, as the M2 path does.
                baseTex = UploadTexture(info.PrimaryTexture, !key, false, false, textures, textureCache, owned, objectName, rt);
                if (baseTex == null) rt.PrimaryTexturesMissing++;
            }
            return NewSurface(name, baseTex, key, (wm.Flags & WmoMaterial.FlagCullDisabled) != 0);
        }

        var m = new Material(shader) { name = name };
        int uvOverride = WmvModelBuilder.Debug_.WmoUvOverride;
        foreach (WmoSamplerBinding b in plan.Samplers)
        {
            if (b.Register < 0 || b.Register >= ShaderRegisters)
                continue;   // a plan row this shader has no register for; the self-test pins that none exists
            if (b.Unread)
                continue;   // nothing it would return reaches the pixel: not bound, not uploaded, not counted
            // DIAGNOSTIC ONLY (-wmvWmoUvOverride): every register of a four-layer or two-layer material reads
            // one channel, so a capture against the normal one shows which surfaces depend on the per-layer
            // channel.
            bool overridden = uvOverride >= 0 &&
                              (plan.Permutation == WmoPermutation.FourLayer || IsTwoLayer(plan.Permutation));
            SetIf(m, "_WmoUv" + b.Register, overridden ? uvOverride : b.UvChannel);
            if (b.FileDataID == 0)
            {
                // The register keeps its white default: the archived baseline's empty +0x0C; for a resolved
                // id, and for an id-23 height map that is missing, the plan's U-23b note says that white is
                // a placeholder.
                rt.EmptySamplerSlots++;
                continue;
            }
            // Alpha kept on every upload: WmvWmo.shader reads it only in the terms the plan enables (the key,
            // the height registers), so one upload serves a material that reads the channel and one that
            // does not, and no pixel depends on the channel where the plan says it is not read.
            Texture2D tex = UploadTexture(b.FileDataID, false, plan.ClampU, plan.ClampV, textures, textureCache,
                                          owned, objectName, rt);
            if (tex != null)
                m.SetTexture("_WmoTex" + b.Register, tex);
            else
                rt.PrimaryTexturesMissing++;
        }
        SetIf(m, "_WmoPermutation", (float)(int)plan.Permutation);
        if (m.HasProperty("_WmoLayerMask"))
            m.SetVector("_WmoLayerMask", new Vector4(plan.LayerMask[0], plan.LayerMask[1], plan.LayerMask[2], plan.LayerMask[3]));
        SetIf(m, "_WmoLightBypass", plan.LightBypass ? 1f : 0f);
        SetIf(m, "_WmoVertexColourDiag", WmvModelBuilder.Debug_.WmoVertexColour ? 1f : 0f);
        ApplyDiagnosticView(m, plan, wm.Index);
        ApplyRenderState(m, plan);
        return m;
    }

    /// <summary>
    /// The verification views, all off unless their switch is given. -wmvWmoView=plan draws every material
    /// flat in the colour of how it is drawn (red archived baseline, green diffuse, blue four-layer, orange
    /// two-layer, yellow two-layer env metal (id 7), cyan opaque, white env metal (id 5), magenta any
    /// PROVISIONAL fallback of those), so a capture is a mask of which
    /// surfaces a stage touches; =weights and =blend draw a four-layer material's stored MOC2 weights and
    /// its effective weights after the height blend (rgb = layers 1..3, black = layer 4); =va draws a
    /// two-layer material's set-2 alpha as grey; =diffuse the combiner diffuse of every material; =t0 and
    /// =t1 the first and second register as sampled; =envmask the emissive mask of ids 5, 7 and 23 (the
    /// factor the client multiplies each env map by -- no env map is bound or sampled for it).
    /// -wmvWmoOnlyMaterials=a:b:c discards every other material in every camera, so a close-up isolates the
    /// surfaces a check is about.
    /// </summary>
    static void ApplyDiagnosticView(Material m, WmoMaterialPlan plan, int materialIndex)
    {
        int view = WmvModelBuilder.Debug_.WmoView;
        SetIf(m, "_WmoDiagView", view);
        if (view > 0 && m.HasProperty("_WmoDiagColour"))
            m.SetColor("_WmoDiagColour", PlanColour(plan));
        int[] only = WmvModelBuilder.Debug_.WmoOnlyMaterials;
        SetIf(m, "_WmoDiagHide", only != null && Array.IndexOf(only, materialIndex) < 0 ? 1f : 0f);
    }

    /// <summary>The -wmvWmoView=plan colour of a material.</summary>
    public static Color PlanColour(WmoMaterialPlan plan)
    {
        if (plan.Permutation == WmoPermutation.ProvisionalBaseline)
            return new Color(1f, 0.15f, 0.15f);
        if (plan.ProvisionalFallback)
            return new Color(1f, 0.15f, 1f);
        switch (plan.Permutation)
        {
            case WmoPermutation.FourLayer: return new Color(0.15f, 0.15f, 1f);
            case WmoPermutation.TwoLayer: return new Color(1f, 0.6f, 0.1f);
            case WmoPermutation.Opaque: return new Color(0.15f, 1f, 1f);
            // Ids 7 and 5 draw what ids 13 and 4 draw; their own colours keep a stage-7/5 mask apart from
            // the id-13/4 surfaces.
            case WmoPermutation.TwoLayerEnvMetal: return new Color(1f, 1f, 0.15f);
            case WmoPermutation.EnvMetal: return new Color(1f, 1f, 1f);
            default: return new Color(0.15f, 1f, 0.15f);
        }
    }

    /// <summary>The -wmvWmoView value of the envmask view (WmvWmo.shader's _WmoDiagView 8).</summary>
    public const int EnvMaskView = 8;

    /// <summary>"" normally; the active verification switches, for the build summary, otherwise.</summary>
    static string DiagnosticViewNote()
    {
        var parts = new List<string>();
        int view = WmvModelBuilder.Debug_.WmoView;
        if (view == EnvMaskView)
            parts.Add("-wmvWmoView=" + WmvModelBuilder.Debug_.WmoViewName(view) + " (DIAGNOSTIC: client emissive masks; equations " +
                      "CLIENT, on the diffuse's texture coordinates, which assume the cb0[5].y override does not replace them " +
                      "(U-G1); env map not sampled: coordinate U-G1/U-E2, address mode U-E4, distance fade U-E3, program " +
                      "presence U-P1)");
        else if (view > 0) parts.Add("-wmvWmoView=" + WmvModelBuilder.Debug_.WmoViewName(view));
        if (WmvModelBuilder.Debug_.WmoUvOverride >= 0)
            parts.Add("-wmvWmoUvOverride=" + WmvModelBuilder.Debug_.WmoUvOverride + " (every four-layer and two-layer register on that UV channel)");
        if (WmvModelBuilder.Debug_.WmoOnlyMaterials != null)
            parts.Add("-wmvWmoOnlyMaterials=" + string.Join(":", Array.ConvertAll(WmvModelBuilder.Debug_.WmoOnlyMaterials, x => x.ToString())));
        return parts.Count > 0 ? "; DIAGNOSTIC VIEW ON: " + string.Join(", ", parts.ToArray()) : "";
    }

    /// <summary>
    /// One Texture2D per (file, U addressing, V addressing) for the whole load: two materials that sample
    /// one file with the same addressing share the upload; one that clamps gets its own, because wrap lives
    /// on the texture object, not the sampler. With the world-model shader the alpha is always kept (see
    /// CreateMaterial); only the baseline on the renderer's opaque shader drops it, and that path never
    /// shares a load with the other. The decode itself already happened once per FileDataID
    /// (WmvMainMapObject). The upload keeps no CPU-readable copy: nothing reads a world-model texel back.
    /// </summary>
    static Texture2D UploadTexture(uint fileDataID, bool dropAlpha, bool clampU, bool clampV,
                                   Dictionary<uint, WmvWmoTexture> textures, Dictionary<long, Texture2D> textureCache,
                                   List<Texture2D> owned, string objectName, WmvRuntimeMapObject rt)
    {
        WmvWmoTexture t;
        if (!textures.TryGetValue(fileDataID, out t) || t == null || t.Image == null)
            return null;
        long key = ((long)fileDataID << 3) | (dropAlpha ? 4L : 0L) | (clampU ? 2L : 0L) | (clampV ? 1L : 0L);
        Texture2D tex;
        if (textureCache.TryGetValue(key, out tex))
            return tex;
        tex = WmvModelBuilder.CreateTexture(t.Image, objectName + "_tex" + fileDataID + (dropAlpha ? "_opaque" : "") +
                                            (clampU ? "_clampU" : "") + (clampV ? "_clampV" : ""), dropAlpha, true);
        owned.Add(tex);     // owned before anything else can throw (see Build)
        // Repeat is the format's default addressing; F_CLAMP_S / F_CLAMP_T switch an axis to clamp.
        tex.wrapModeU = clampU ? TextureWrapMode.Clamp : TextureWrapMode.Repeat;
        tex.wrapModeV = clampV ? TextureWrapMode.Clamp : TextureWrapMode.Repeat;
        textureCache[key] = tex;
        rt.GpuTextureBytes += (long)t.Image.Width * t.Image.Height * 4 * 4 / 3;
        rt.ManagedBytes += (long)t.Image.Width * t.Image.Height * 4;   // CreateTexture's row-flipped copy
        return tex;
    }

    /// <summary>The plan's blend, depth, test, cull and queue on the material. Not ApplyBlendMode: that
    /// switch speaks the M2 blend enumeration, whose values mean different factors from MOMT's.</summary>
    static void ApplyRenderState(Material m, WmoMaterialPlan plan)
    {
        SetIf(m, "_SrcBlend", (float)(int)ToUnity(plan.SrcColor));
        SetIf(m, "_DstBlend", (float)(int)ToUnity(plan.DstColor));
        SetIf(m, "_SrcBlendA", (float)(int)ToUnity(plan.SrcAlpha));
        SetIf(m, "_DstBlendA", (float)(int)ToUnity(plan.DstAlpha));
        SetIf(m, "_ZWrite", plan.ZWrite ? 1f : 0f);
        SetIf(m, "_AlphaTest", plan.AlphaTest ? 1f : 0f);
        SetIf(m, "_Cutoff", plan.Cutoff);
        SetIf(m, "_Cull", (float)(plan.CullOff ? UnityEngine.Rendering.CullMode.Off : UnityEngine.Rendering.CullMode.Back));
        m.SetOverrideTag("RenderType", plan.RenderType);
        m.renderQueue = plan.RenderQueue;
    }

    /// <summary>By name rather than by cast, so the mapping cannot drift if either numbering ever does.</summary>
    static UnityEngine.Rendering.BlendMode ToUnity(WmoBlendFactor f)
    {
        switch (f)
        {
            case WmoBlendFactor.Zero: return UnityEngine.Rendering.BlendMode.Zero;
            case WmoBlendFactor.One: return UnityEngine.Rendering.BlendMode.One;
            case WmoBlendFactor.DstColor: return UnityEngine.Rendering.BlendMode.DstColor;
            case WmoBlendFactor.SrcColor: return UnityEngine.Rendering.BlendMode.SrcColor;
            case WmoBlendFactor.OneMinusDstColor: return UnityEngine.Rendering.BlendMode.OneMinusDstColor;
            case WmoBlendFactor.SrcAlpha: return UnityEngine.Rendering.BlendMode.SrcAlpha;
            case WmoBlendFactor.OneMinusSrcColor: return UnityEngine.Rendering.BlendMode.OneMinusSrcColor;
            case WmoBlendFactor.DstAlpha: return UnityEngine.Rendering.BlendMode.DstAlpha;
            case WmoBlendFactor.OneMinusDstAlpha: return UnityEngine.Rendering.BlendMode.OneMinusDstAlpha;
            case WmoBlendFactor.SrcAlphaSaturate: return UnityEngine.Rendering.BlendMode.SrcAlphaSaturate;
            default: return UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
        }
    }

    /// <summary>A batch whose material id is past MOMT: plain white, opaque, the provisional permutation.</summary>
    static Material CreateFallbackMaterial(string objectName, Action<string> log)
    {
        Shader shader = ResolveMapObjectShader(null);
        if (shader == null)
            return NewSurface(objectName + "_mat_missing", null, false, false);
        var m = new Material(shader) { name = objectName + "_mat_missing" };
        SetIf(m, "_WmoPermutation", (float)(int)WmoPermutation.ProvisionalBaseline);
        SetIf(m, "_WmoVertexColourDiag", WmvModelBuilder.Debug_.WmoVertexColour ? 1f : 0f);
        var plan = new WmoMaterialPlan();
        ApplyDiagnosticView(m, plan, -1);
        ApplyRenderState(m, plan);
        return m;
    }

    /// <summary>
    /// The archived baseline on the renderer's opaque shader, used only when the world-model shader is
    /// missing from the player: the single-texture state the M2 builder gives a one-texture opaque or
    /// alpha-keyed batch, with the M2 switch's opaque and alpha-key arms (which coincide with MOMT
    /// blend 0 and the provisional key).
    /// </summary>
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

    /// <summary>What a texture file became: decoded, header-checked only, or missing.</summary>
    static string TextureState(uint fileDataID, Dictionary<uint, WmvWmoTexture> textures)
    {
        WmvWmoTexture t;
        if (textures == null || !textures.TryGetValue(fileDataID, out t) || t == null)
            return "MISSING: no answer";
        return t.Decoded ? string.Format("{0}x{1} {2}", t.Width, t.Height, t.Encoding)
             : t.HeaderOnly ? string.Format("{0}x{1} {2}, fetched, not decoded", t.Width, t.Height, t.Encoding)
             : "MISSING: " + (t.Error ?? "no answer");
    }

    /// <summary>One line per MOMT entry, drawn or not: the slots, what the plan draws, and its verdict.</summary>
    public static string DescribeMaterial(WmvWmoMaterialInfo info, int batchUses, Dictionary<uint, WmvWmoTexture> textures)
    {
        var slots = new System.Text.StringBuilder();
        for (int s = 0; s < info.TextureSlots.Length; s++)
        {
            if (info.TextureSlots[s] == 0) continue;
            slots.Append(slots.Length > 0 ? ", " : "").Append(WmoMaterialSemantics.SlotName(s)).Append(' ')
                 .Append(info.TextureSlots[s]).Append(" (").Append(TextureState(info.TextureSlots[s], textures)).Append(')');
        }
        WmoMaterialPlan plan = info.Plan;
        var samplers = new System.Text.StringBuilder();
        foreach (WmoSamplerBinding b in plan.Samplers)
        {
            samplers.Append(samplers.Length > 0 ? ", " : "");
            if (b.Unread)
                samplers.AppendFormat("t{0} = {1} {2} not bound", b.Register, WmoMaterialSemantics.SlotName(b.Slot),
                                      b.FileDataID == 0 ? "empty" : b.FileDataID.ToString());
            else
                samplers.AppendFormat("t{0} = {1} {2} @ UV{3}", b.Register, WmoMaterialSemantics.SlotName(b.Slot),
                                      b.FileDataID == 0 ? "empty (white)" : b.FileDataID.ToString(), b.UvChannel);
        }
        string state = (plan.AlphaTest ? "alpha key 128/255" : "opaque") + ", " + (plan.CullOff ? "cull off" : "cull back") +
                       ", wrap " + (plan.ClampU ? "clamp" : "repeat") + "/" + (plan.ClampV ? "clamp" : "repeat") +
                       (plan.LightBypass ? ", light bypass" : "");
        if (plan.ReadsMoc2)
            state += string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                   ", MOC2 weights with layer mask ({0},{1},{2},{3}); of {4} drawn vertices {5} have MOC2 byte 3 > 0 " +
                                   "(U-23a: diffuse differs there) and {6} a stored weight on an empty layer (U-23b)",
                                   plan.LayerMask[0], plan.LayerMask[1], plan.LayerMask[2], plan.LayerMask[3],
                                   info.Moc2Vertices, info.Moc2AlphaVertices, info.EmptyLayerWeightVertices);
        if (plan.ReadsSet2Alpha)
            state += string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                   ", layer factor va = MOCV set-2 alpha; of {0} drawn vertices {1} have va below 1 (layer 2 " +
                                   "shows), {2} va 0, and {3} weight an empty layer slot (U-23b: drawn white there)",
                                   info.Set2Vertices, info.Set2PartialVertices, info.Set2ZeroVertices, info.UnboundLayerVertices);
        if (info.StreamGapVertices > 0)
            state += string.Format(", {0} drawn vertices lack {1} (U-V4: drawn with the stated default there)",
                                   info.StreamGapVertices, info.GapStreams);
        string verdict = batchUses > 0 ? plan.ResolutionName.ToUpperInvariant() + info.Verdict.Substring(plan.ResolutionName.Length)
                                       : info.Verdict + " (not drawn by any LOD0 batch)";
        return string.Format("wmo material {0}: shader {1} blend {2} flags 0x{3:X8} slots [{4}] -> {5}: {6}, {7}; " +
                             "{8} batch(es) -- {9}{10}",
                             info.Index, info.Shader, info.Blend, info.Flags, slots.Length > 0 ? slots.ToString() : "none",
                             plan.PermutationName, samplers, state, batchUses, verdict,
                             batchUses > 0 && info.Why.Length > 0 ? " -- " + info.Why : "");
    }

    /// <summary>
    /// -wmvWmoMaterialDiag: the plan (WmoMaterialSemantics.Describe: shader id, blend value, the realised
    /// factors, depth write and queue, the client's blend row for a blend of 2 or above, the env emissive of
    /// ids 5/7/23 with its equation, mask, env map and coordinate status, the codes and the resolution), how
    /// many batches draw it (and how many of those are interior), what each sampler's file became, and the
    /// created Unity material READ BACK -- shader, queue, tag, every render-state property, and each bound
    /// register's texture with its size and addressing -- so a claim about a material is checked against the
    /// object that draws, not against the code that meant to build it.
    /// </summary>
    public static string DescribeMaterialDiag(WmvWmoMaterialInfo info, Material m, int batchUses,
                                              Dictionary<uint, WmvWmoTexture> textures)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new System.Text.StringBuilder("wmo material-diag ");
        sb.Append(WmoMaterialSemantics.Describe(info.Plan)).Append(" | batches ").Append(batchUses);
        if (info.InteriorBatches > 0)
            sb.Append(" (").Append(info.InteriorBatches).Append(" in interior groups, MOGP 0x2000)");
        sb.Append(" | decode [");
        for (int i = 0; i < info.Plan.Samplers.Length; i++)
        {
            WmoSamplerBinding b = info.Plan.Samplers[i];
            sb.Append(i > 0 ? "; " : "").Append('t').Append(b.Register).Append(' ')
              .Append(b.Unread ? (b.FileDataID == 0 ? "not bound" : b.FileDataID + " not bound, " + TextureState(b.FileDataID, textures))
                      : b.FileDataID == 0 ? "slot empty" : b.FileDataID + " " + TextureState(b.FileDataID, textures));
        }
        sb.Append(']');
        if (m == null)
            return sb.Append(" | readback: no material").ToString();
        sb.AppendFormat(" | readback: shader '{0}', queue {1}, RenderType {2}", m.shader != null ? m.shader.name : "null",
                        m.renderQueue, m.GetTag("RenderType", false, "?"));
        foreach (string p in new[] { "_WmoPermutation", "_SrcBlend", "_DstBlend", "_SrcBlendA", "_DstBlendA", "_ZWrite", "_Cull",
                                     "_AlphaTest", "_Cutoff", "_WmoLightBypass", "_WmoVertexColourDiag", "_WmoDiagView", "_WmoDiagHide" })
            if (m.HasProperty(p))
                sb.Append(", ").Append(p).Append(' ').Append(m.GetFloat(p).ToString("0.#####", inv));
        if (m.HasProperty("_WmoLayerMask") && info.Plan.Permutation == WmoPermutation.FourLayer)
        {
            Vector4 mask = m.GetVector("_WmoLayerMask");
            sb.AppendFormat(inv, ", _WmoLayerMask ({0},{1},{2},{3})", mask.x, mask.y, mask.z, mask.w);
        }
        for (int r = 0; r < ShaderRegisters; r++)
        {
            string texProp = "_WmoTex" + r;
            if (!m.HasProperty(texProp)) continue;
            var tex = m.GetTexture(texProp) as Texture2D;
            if (tex == null) continue;
            sb.AppendFormat(", {0} '{1}' {2}x{3} wrap {4}/{5} on UV{6}", texProp, tex.name, tex.width, tex.height,
                            tex.wrapModeU, tex.wrapModeV, m.HasProperty("_WmoUv" + r) ? m.GetFloat("_WmoUv" + r).ToString(inv) : "?");
        }
        if (!m.HasProperty("_WmoPermutation") && m.mainTexture != null)
            sb.Append(", baseline fallback mainTexture '").Append(m.mainTexture.name).Append('\'');
        return sb.ToString();
    }

    /// <summary>
    /// -wmvWmoMaterialDiag, one drawn batch: "wmo batch g&lt;group&gt;.b&lt;MOBA index&gt; range A|B|C -&gt; submesh k -&gt;
    /// material m (shader, blend, queue, ZWrite, verdict)". The range is the batch's place among the group
    /// header's A/B/C counts ("?" past them); queue and depth write are READ BACK from the material that draws
    /// the submesh, so a later ordering or blend stage is checked against the object that draws.
    /// </summary>
    public static string DescribeBatchDiag(WmvRuntimeMapObject rt, WmoGroup g, int gi, int batchIndex, int submesh, int materialId)
    {
        int a = g.Header.BatchCountA, b = g.Header.BatchCountB, c = g.Header.BatchCountC;
        string range = batchIndex < a ? "A" : batchIndex < a + b ? "B" : batchIndex < a + b + c ? "C" : "?";
        bool inTable = materialId >= 0 && materialId < rt.MaterialInfo.Length && materialId < rt.Materials.Length;
        Material m = inTable ? rt.Materials[materialId] : rt.FallbackMaterial;
        string queue = m != null ? m.renderQueue.ToString() : "?";
        string zwrite = m != null && m.HasProperty("_ZWrite") ? (m.GetFloat("_ZWrite") > 0.5f ? "on" : "off") : "?";
        string head = string.Format("wmo batch g{0}.b{1} range {2} -> submesh {3} -> material {4}", gi, batchIndex, range, submesh, materialId);
        if (!inTable)
            return head + string.Format(" (past MOMT: the plain white fallback, queue {0}, ZWrite {1})", queue, zwrite);
        WmvWmoMaterialInfo info = rt.MaterialInfo[materialId];
        return head + string.Format(" (shader {0}, blend {1}, queue {2}, ZWrite {3}, {4})", info.Shader, info.Blend, queue, zwrite,
                                    info.Verdict);
    }
}
