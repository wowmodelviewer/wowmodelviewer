// WmvTerrainBuilder.cs
//
// The Unity objects of one terrain tile (stage 03): WmvTerrainBuilder turns the pure mesh data of
// Wow/World/TerrainMesh.cs (TerrainMeshBuilder, already in Unity space and relative to the tile's
// north-west corner) into one GameObject, Mesh and MeshRenderer per MCNK that has triangles, under one
// tile root; WmvRuntimeTerrainTile owns all of it. WmvTerrainSession decides when a tile is built,
// shown and disposed; WmvMainTerrain wires both into the player.
//
//   - LAYER 30, never resolved by name (the repository carries no ProjectSettings): every GameObject the
//     builder makes -- the tile root, each chunk object, each line object -- gets Layer when it is
//     created. The main camera shows ModelViewMask (everything but 30) or TerrainViewMask (30 only),
//     and nothing else assigns either (WmvMainTerrain's SetViewMode); the shadow rig, the light check
//     and the mount probe render WithoutTerrain(their view's mask) and nothing when that is 0.
//   - BUILT INACTIVE: the root is deactivated before anything is put under it, so no frame can draw a
//     half-built tile; the session activates it in the frame it commits it. Every GameObject, Mesh and
//     Material is handed to the runtime (Track) the moment it exists, so an exception half-way through
//     disposes exactly what was made.
//   - ONE MESH PER CHUNK WITH TRIANGLES: 145 vertices (positions and the builder's geometry-derived
//     DIAGNOSTIC normals, not MCNR), 16-bit indices, one sub-mesh, Triangles topology, and the bounds
//     the builder computed from the vertices its triangles reference. A chunk without triangles (all
//     holes, skipped, unsupported) gets no object at all. UploadMeshData(true) frees the CPU copy --
//     except when -wmvTerrainDump asks for the meshes to be read back.
//   - IDENTITY TRANSFORMS: the root and every chunk object stay at the identity under the scene root
//     (_WmvTerrain, itself at the identity); the tile's position is in its vertices (re-based on the
//     tile's own corner, design section 2.4).
//   - SHADING (Resources/WmvTerrain.shader, "Wmv/Terrain") is a GEOMETRY DIAGNOSTIC, not WoW material
//     or lighting: a neutral grey Lambert from a fixed key, per-chunk tints, the normals as colour, or a
//     wireframe of each chunk's unique triangle edges. Every capture of it is labelled so.
//   - TEXTURE MATERIALS (stage 04; Resources/WmvTerrainTextured.shader, "Wmv/TerrainTextured", built by
//     WmvTerrainMaterials): in the textured views a chunk the material calls drawable (Textured, MissingTexture
//     or LayerEffectStatic -- the last drawn with the static composite, its animation and overbright not
//     applied) gets a material of its own -- its layer textures (up to 8), repeats, UV phases, alpha slice(s),
//     missing flags and, on an MPHD 0x80 map, its layers' height textures and MTXP heights for the height mix -- and
//     its mesh gains UV0 (cell coordinates) and, with MCCV, float vertex colours; positions, normals, indices and the
//     mesh byte count stay exactly the stage 03 geometry (the material's vertex bytes are counted apart,
//     MaterialVertexBytes). Every other chunk (Malformed, AlphaUnsupported, LayerCountUnsupported,
//     LayerEffectUnresolved, HeightTextureMissing, NoLayers) is drawn NEUTRAL with Wmv/Terrain's shaded view, as
//     in stage 03 (v3 delta section 2). The tile's textures (one per FileDataID, diffuse and height alike), its
//     one alpha array (a second slice for each chunk of more than 5 layers) and its one sampler carrier (bound into
//     the layer slots without a texture: WmvTerrainMaterials.CreateSamplerCarrier) are created here and tracked the
//     moment they exist.
//   - LIQUID (stage 09, WmvLiquids.cs): the tile's MH2O as one GameObject, Mesh and MeshRenderer per liquid type under
//     the root, in every view, tracked like the chunks, drawn with the type's shared material from the scene's liquid
//     registry, which the tile holds until it is released.
//
// Debug switches (WmvTerrainDebug; command line or the space-separated WMV_DEBUG, as the other -wmv*
// switches): -wmvTerrainView=shaded|chunks|wire|shadedwire|normals|textured|texturedlit|texturedplainweights|materials
// (not given: textured when the tile's material is textured or partial, shaded otherwise), -wmvTerrainChunk=N
// (only chunk N is shown), -wmvTerrainFocus=N (frame chunk N), -wmvTerrainTopDown (orthographic top-down
// camera, design 2.3b), -wmvTerrainDump=<bare name> (the meshes read back into <player folder>/<name>.txt,
// the TerrainDump format, for Tools/world-audit/compare_terrain_mesh.py), -wmvTerrainTimeoutMs=<n> (the load
// timeout, 1000..120000 ms, 30000 by default: the host's end-to-end timeout tests), and the stage 04
// VALIDATION FIXTURES (never production; WmvTerrainComposite): -wmvTerrainValidate (validation sampling:
// texels by Load, the alpha by a manual bilinear), -wmvTerrainCompositeDump=<bare name>[:<size>] (the tile's
// composite rendered top-down by a CommandBuffer into a float target, size 512 by default, written as
// <name>.raw, <name>.json and, up to 2048 px, <name>.png), -wmvTerrainCompositeChunk=<n>[,<n>...] (each chunk
// rendered alone at its validation size into <dump name>_c<n>.*) and -wmvTerrainTexelDump=<bare name> (each
// texture's mip 0 as the GPU decodes it, by Load, with its role) -- the formats of
// Tools/world-audit/compare_terrain_material.py. The composite dumps are taken in the textured view only (any other
// view is refused: it is not the authored composite). Stage 07: -wmvDoodadSets=all, a DIAGNOSTIC that draws every doodad
// set of every placed world model (the inactive ones too, for comparison; said in the log and the report).

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow.World;

/// <summary>How the terrain tile is drawn (-wmvTerrainView). The first five are the stage 03 geometry diagnostics; the
/// textured views draw the chunks the material calls drawable with their textures and every other chunk neutral.</summary>
public enum WmvTerrainView
{
    /// <summary>Neutral grey Lambert from a fixed key plus ambient, from the diagnostic normals.</summary>
    Shaded = 0,
    /// <summary>The same light over a per-chunk tint, so chunk borders show.</summary>
    Chunks = 1,
    /// <summary>Each chunk's unique triangle edges over surfaces that only write depth (hidden lines stay hidden).</summary>
    Wire = 2,
    /// <summary>The shaded surfaces with the edges drawn over them.</summary>
    ShadedWire = 3,
    /// <summary>The diagnostic normal as colour, n * 0.5 + 0.5 (the top-down orientation capture, design 2.3b).</summary>
    Normals = 4,
    /// <summary>The texture composite (stage 04): the default when the tile's material is textured or partial.</summary>
    Textured = 5,
    /// <summary>The composite times the stage 03 diagnostic Lambert (a diagnostic light, not the game's).</summary>
    TexturedLit = 6,
    /// <summary>A labelled comparison: every chunk with layers (drawable, and the plain-diagnostic LayerEffectUnresolved and
    /// HeightTextureMissing ones) in PLAIN WEIGHTS (w_0 = 1 - sum a_k, no cutoff, no heights) under a hatch -- never the
    /// composite. A layer whose repeats are unresolved (an MTXP scale nibble above 4) is drawn as the checker.</summary>
    TexturedPlainWeights = 7,
    /// <summary>Every chunk in its material state's colour (the legend is logged).</summary>
    Materials = 8,
}

/// <summary>
/// The terrain debug switches, read the way WmvModelBuilder.Debug_ reads the others: from the player's command line
/// AND from the WMV_DEBUG environment variable, split on space, tab, comma and semicolon (the host-launched player
/// inherits WMV_DEBUG). Read once.
/// </summary>
public static class WmvTerrainDebug
{
    public static readonly string[] ViewNames = { "shaded", "chunks", "wire", "shadedwire", "normals", "textured", "texturedlit",
                                                  "texturedplainweights", "materials" };
    /// <summary>The largest composite or chunk render (pixels per side).</summary>
    public const int MaxCompositeSize = 8192;

    static bool parsed;
    static WmvTerrainView view = WmvTerrainView.Shaded;
    static bool viewGiven;
    static WmvTerrainView runtimeView = WmvTerrainView.Shaded;
    static bool runtimeViewSet;

    /// <summary>The views setTerrainView takes (protocol 14): every view but the plain-weights comparison, which fetches
    /// textures of its own and is a validation fixture, not a display mode; "auto" gives the choice back to the command
    /// line and the material (EffectiveView).</summary>
    public static bool IsRuntimeView(string name)
    {
        return name == "auto" || (Array.IndexOf(ViewNames, name) >= 0 && name != "texturedplainweights");
    }

    /// <summary>setTerrainView: the view every tile is drawn in from now on (false: the name is not one IsRuntimeView
    /// takes, and nothing changes).</summary>
    public static bool SetRuntimeView(string name)
    {
        if (!IsRuntimeView(name))
            return false;
        runtimeViewSet = name != "auto";
        runtimeView = runtimeViewSet ? (WmvTerrainView)Array.IndexOf(ViewNames, name) : WmvTerrainView.Shaded;
        return true;
    }

    /// <summary>The host's view, "auto" when it chose none.</summary>
    public static string RuntimeViewName { get { return runtimeViewSet ? ViewNames[(int)runtimeView] : "auto"; } }
    static int onlyChunk = -1, focus = -1;
    static bool topDown, validate;
    static string dumpName, compositeName, texelName;
    static int compositeSize = WmvTerrainComposite.DefaultTileSize;
    static readonly List<int> compositeChunks = new List<int>();
    static double timeoutMs = WmvTerrainSession.DefaultTimeoutMs;
    static bool allDoodadSets;
    static readonly List<string> notes = new List<string>();

    /// <summary>-wmvTerrainView: the view asked for; Shaded when none was (see ViewGiven and EffectiveView).</summary>
    public static WmvTerrainView View { get { Parse(); return view; } }
    /// <summary>-wmvTerrainView was given (and valid); without it the view follows the tile's material.</summary>
    public static bool ViewGiven { get { Parse(); return viewGiven; } }
    public static string ViewName { get { return ViewGiven ? ViewNames[(int)View] : "auto"; } }
    /// <summary>The view a tile is drawn in: the host's (setTerrainView, protocol 14) when it chose one; else the one asked
    /// for on the command line; else -- none asked for -- textured when the tile's material is textured or partial (design
    /// section 4) and the stage 03 shaded view otherwise.</summary>
    public static WmvTerrainView EffectiveView(WmvTerrainTileContent content)
    {
        if (runtimeViewSet)
            return runtimeView;
        if (ViewGiven)
            return View;
        return content != null && content.HasDrawableChunks ? WmvTerrainView.Textured : WmvTerrainView.Shaded;
    }
    /// <summary>The view draws the plain-weights diagnostic, plain-diagnostic chunks included: their textures are fetched too.</summary>
    public static bool DrawsPlainDiagnostic { get { return ViewGiven && View == WmvTerrainView.TexturedPlainWeights; } }
    /// <summary>-wmvTerrainChunk=N: only chunk N is shown (the others are built, and inactive); -1 = all.</summary>
    public static int OnlyChunk { get { Parse(); return onlyChunk; } }
    /// <summary>-wmvTerrainFocus=N: the camera frames chunk N instead of the tile; -1 = the tile.</summary>
    public static int Focus { get { Parse(); return focus; } }
    /// <summary>-wmvTerrainTopDown: the orthographic top-down camera of design 2.3b.</summary>
    public static bool TopDown { get { Parse(); return topDown; } }
    /// <summary>-wmvTerrainDump=name: a bare file name (no folder), or null when not asked for.</summary>
    public static string DumpName { get { Parse(); return dumpName; } }
    /// <summary>-wmvTerrainValidate: the textured shader samples by Load (validation sampling) in every frame and dump.</summary>
    public static bool Validate { get { Parse(); return validate; } }
    /// <summary>-wmvTerrainCompositeDump=name[:size]: the tile composite's bare name, or null; its size (512 by default).</summary>
    public static string CompositeDumpName { get { Parse(); return compositeName; } }
    public static int CompositeDumpSize { get { Parse(); return compositeSize; } }
    /// <summary>-wmvTerrainCompositeChunk=n[,n...]: the chunks rendered alone (written under the composite dump's name).</summary>
    public static IList<int> CompositeChunks { get { Parse(); return compositeChunks; } }
    /// <summary>-wmvTerrainTexelDump=name: the texel dump's bare name, or null.</summary>
    public static string TexelDumpName { get { Parse(); return texelName; } }
    /// <summary>Any stage 04 validation fixture asked for (a dump written when a tile is built).</summary>
    public static bool AnyMaterialDump { get { return CompositeDumpName != null || TexelDumpName != null || CompositeChunks.Count > 0; } }
    /// <summary>-wmvTerrainTimeoutMs=n: the session's load timeout, clamped to WmvTerrainSession.MinTimeoutMs..MaxTimeoutMs;
    /// WmvTerrainSession.DefaultTimeoutMs when not given. The timeout in effect is in the player's "[terrain] ready" line
    /// and in runtimeState (terrainTimeoutMs); a value refused or clamped is one of the Notes.</summary>
    public static double TimeoutMs { get { Parse(); return timeoutMs; } }
    /// <summary>What the parse refused (a bad value is said once in the log, never guessed at).</summary>
    public static IList<string> Notes { get { Parse(); return notes; } }
    /// <summary>-wmvDoodadSets=all (stage 07): a DIAGNOSTIC -- every doodad set of every placed world model is drawn, not set 0
    /// plus the one its placement names (DoodadPlan.AllSets).</summary>
    public static bool AllDoodadSets { get { Parse(); return allDoodadSets; } }

    public static string Describe()
    {
        Parse();
        return string.Format("view {0}{1}{2}{3}{4}{5}{6}{7}{8}{9}",
                             viewGiven ? ViewName : "auto (textured when the tile's material is textured or partial, else shaded)",
                             onlyChunk >= 0 ? ", only chunk " + onlyChunk : "", focus >= 0 ? ", focus chunk " + focus : "",
                             topDown ? ", top-down" : "", dumpName != null ? ", dump " + dumpName : "",
                             validate ? ", VALIDATION sampling" : "",
                             compositeName != null ? ", composite dump " + compositeName + " at " + compositeSize + " px" : "",
                             compositeChunks.Count > 0 ? ", chunk renders " + string.Join(",", compositeChunks) : "",
                             texelName != null ? ", texel dump " + texelName : "",
                             allDoodadSets ? ", EVERY DOODAD SET DRAWN (DIAGNOSTIC)" : "");
    }

    static bool BareName(string v) { return v.Length > 0 && v.IndexOfAny(new[] { '/', '\\', ':' }) < 0 && v != "." && v != ".."; }

    static void Parse()
    {
        if (parsed) return;
        parsed = true;
        var args = new List<string>(Environment.GetCommandLineArgs());
        string fromEnv = Environment.GetEnvironmentVariable("WMV_DEBUG");
        if (!string.IsNullOrEmpty(fromEnv))
            args.AddRange(fromEnv.Split(new[] { ' ', '\t', ',', ';' }, StringSplitOptions.RemoveEmptyEntries));
        // -wmvTerrainCompositeChunk takes a comma-separated list, which the WMV_DEBUG split cuts apart: the plain numbers
        // that follow it belong to it.
        bool inChunkList = false;
        foreach (string a in args)
        {
            int listed;
            if (inChunkList && int.TryParse(a, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out listed))
            {
                AddChunk(a);
                continue;
            }
            inChunkList = false;
            if (a.StartsWith("-wmvTerrainView=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainView=".Length);
                int i = Array.IndexOf(ViewNames, v);
                if (i >= 0)
                {
                    view = (WmvTerrainView)i;
                    viewGiven = true;
                }
                else notes.Add("-wmvTerrainView=" + v + " is not one of " + string.Join("|", ViewNames) + " -- the view follows the material");
            }
            else if (a.StartsWith("-wmvTerrainChunk=", StringComparison.Ordinal))
                onlyChunk = ChunkIndex(a, "-wmvTerrainChunk=");
            else if (a.StartsWith("-wmvTerrainFocus=", StringComparison.Ordinal))
                focus = ChunkIndex(a, "-wmvTerrainFocus=");
            else if (a == "-wmvTerrainTopDown")
                topDown = true;
            else if (a == "-wmvTerrainValidate")
                validate = true;
            else if (a == "-wmvDoodadSets=all")
                allDoodadSets = true;
            else if (a.StartsWith("-wmvDoodadSets=", StringComparison.Ordinal))
                notes.Add(a + " is not -wmvDoodadSets=all -- the doodad sets follow the placements");
            else if (a.StartsWith("-wmvTerrainDump=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainDump=".Length);
                if (BareName(v))
                    dumpName = v;
                else
                    notes.Add("-wmvTerrainDump=" + v + " is not a bare file name -- no dump is written");
            }
            else if (a.StartsWith("-wmvTerrainCompositeDump=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainCompositeDump=".Length);
                string name = v;
                int size = WmvTerrainComposite.DefaultTileSize;
                int colon = v.LastIndexOf(':');
                bool sizeOk = true;
                if (colon >= 0)
                {
                    name = v.Substring(0, colon);
                    sizeOk = int.TryParse(v.Substring(colon + 1), System.Globalization.NumberStyles.Integer,
                                          System.Globalization.CultureInfo.InvariantCulture, out size) &&
                             size >= 16 && size <= MaxCompositeSize && size % 16 == 0;
                }
                if (!BareName(name))
                    notes.Add("-wmvTerrainCompositeDump=" + v + " is not a bare file name -- no composite is written");
                else if (!sizeOk)
                    notes.Add("-wmvTerrainCompositeDump=" + v + ": the size must be a multiple of 16 within 16.." + MaxCompositeSize +
                              " -- no composite is written");
                else
                {
                    compositeName = name;
                    compositeSize = size;
                }
            }
            else if (a.StartsWith("-wmvTerrainCompositeChunk=", StringComparison.Ordinal))
            {
                foreach (string part in a.Substring("-wmvTerrainCompositeChunk=".Length).Split(','))
                    if (part.Length > 0)
                        AddChunk(part);
                inChunkList = true;
            }
            else if (a.StartsWith("-wmvTerrainTexelDump=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainTexelDump=".Length);
                if (BareName(v))
                    texelName = v;
                else
                    notes.Add("-wmvTerrainTexelDump=" + v + " is not a bare file name -- no texel dump is written");
            }
            else if (a.StartsWith("-wmvTerrainTimeoutMs=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainTimeoutMs=".Length);
                int ms;
                if (int.TryParse(v, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out ms))
                {
                    timeoutMs = WmvTerrainSession.ClampTimeout(ms);
                    if (timeoutMs != ms)
                        notes.Add("-wmvTerrainTimeoutMs=" + v + " is outside " + WmvTerrainSession.MinTimeoutMs + ".." +
                                  WmvTerrainSession.MaxTimeoutMs + " -- " + timeoutMs + " ms is used");
                }
                else
                    notes.Add("-wmvTerrainTimeoutMs=" + v + " is not a whole number of milliseconds -- the default " +
                              WmvTerrainSession.DefaultTimeoutMs + " ms is used");
            }
        }
        if (compositeChunks.Count > 0 && compositeName == null)
            notes.Add("-wmvTerrainCompositeChunk needs -wmvTerrainCompositeDump=<name> for its file names -- no chunk is rendered");
    }

    static void AddChunk(string part)
    {
        int n;
        if (int.TryParse(part, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out n) &&
            n >= 0 && n < AdtParser.ChunksPerTile)
        {
            if (!compositeChunks.Contains(n))
                compositeChunks.Add(n);
        }
        else
            notes.Add("-wmvTerrainCompositeChunk entry " + part + " is not a chunk index 0..255 -- ignored");
    }

    static int ChunkIndex(string arg, string prefix)
    {
        int n;
        if (int.TryParse(arg.Substring(prefix.Length), out n) && n >= 0 && n < AdtParser.ChunksPerTile)
            return n;
        notes.Add(arg + " is not a chunk index 0..255 -- ignored");
        return -1;
    }
}

/// <summary>How WmvTerrainBuilder builds a tile.</summary>
public struct WmvTerrainBuildOptions
{
    public WmvTerrainView View;
    /// <summary>Only this chunk's object is active (-wmvTerrainChunk); -1 = all.</summary>
    public int OnlyChunk;
    /// <summary>Keep the meshes' CPU copies (no UploadMeshData(true)), so they can be read back (-wmvTerrainDump).</summary>
    public bool KeepReadable;
}

/// <summary>
/// One built terrain tile and everything it owns: its root, a GameObject, Mesh and MeshRenderer per chunk with
/// triangles, the line objects of a wire view, its materials, and (stage 04) its layer textures and alpha array.
/// WmvTerrainBuilder adds each object the moment it creates it (Track), so Dispose releases exactly what exists
/// whenever it is called.
/// </summary>
public class WmvRuntimeTerrainTile
{
    public readonly int MapId, TileX, TileY, Load;
    public GameObject Root;
    public readonly GameObject[] ChunkObjects = new GameObject[AdtParser.ChunksPerTile];   // null: no triangles
    public readonly Mesh[] ChunkMeshes = new Mesh[AdtParser.ChunksPerTile];
    public readonly GameObject[] LineObjects = new GameObject[AdtParser.ChunksPerTile];   // wire views only
    public readonly Bounds[] ChunkBounds = new Bounds[AdtParser.ChunksPerTile];            // valid where ChunkMeshes is set
    /// <summary>The material each chunk object draws with (null where there is no object).</summary>
    public readonly Material[] ChunkMaterials = new Material[AdtParser.ChunksPerTile];
    /// <summary>The chunk is drawn by Wmv/TerrainTextured (textured, plain-weights or state colour), not Wmv/Terrain.</summary>
    public readonly bool[] ChunkTextured = new bool[AdtParser.ChunksPerTile];
    public Material SurfaceMaterial, LineMaterial;
    /// <summary>The tile's bounds (Unity, root-local) from the vertices its triangles reference; HasBounds false when
    /// it has no triangle.</summary>
    public Bounds Bounds;
    public bool HasBounds;
    public WmvTerrainView View;
    public bool Readable;
    public int ChunkObjectCount, LineObjectCount;
    /// <summary>Bytes handed to the GPU: positions and normals (24 per vertex), 16-bit triangle and line indices, and
    /// the material's vertex data (MaterialVertexBytes).</summary>
    public long UploadedBytes;
    // Stage 04: the material. (The load's content -- material, textures, alpha slices, vertex colours -- is not kept here:
    // once built it is on the GPU, and the job that held it is dropped after its report.)
    /// <summary>The textures by FileDataID (one per id, uploaded once, diffuse and height alike) and the alpha array (null
    /// without one).</summary>
    public readonly Dictionary<uint, Texture2D> Textures = new Dictionary<uint, Texture2D>();
    public Texture2DArray AlphaArray;
    /// <summary>The tile's sampler carrier (WmvTerrainMaterials.CreateSamplerCarrier: bound into every layer slot without a
    /// texture, never drawn), null when no chunk is drawn textured. Not a layer texture: it is in none of the texture
    /// tallies (TextureCount, TextureGpuBytes, LiveTextures); released with the tile.</summary>
    public Texture2D SamplerCarrier;
    /// <summary>-wmvTerrainChunk as built (WmvTerrainBuildOptions.OnlyChunk): only this chunk's object is active; -1 = all.</summary>
    public int OnlyChunk = -1;
    /// <summary>GPU bytes of the layer textures (every mip as uploaded) and of the alpha array; vertex bytes the material
    /// added (UV0 and MCCV); the Wmv/TerrainTextured materials made; the times the alpha array and the materials took.</summary>
    public long TextureGpuBytes, AlphaGpuBytes, MaterialVertexBytes;
    public int TexturedMaterialCount;
    public double AlphaBuildMs = -1, MaterialCreateMs = -1;
    // Stage 09: the tile's liquid (WmvLiquidMeshes.BuildTileLiquids): one object and mesh per liquid type drawn, under the
    // root, tracked like every other object (so in the tallies above too) and counted apart here and in WmvLiquidMeshes'
    // tallies; the registry whose types the tile holds, given back when it is released; the liquid types it draws.
    public int LiquidObjectCount, LiquidMeshCount;
    public long LiquidGeometryBytes;
    public WmvLiquidRegistry Liquids;
    public readonly List<int> LiquidTypes = new List<int>();
    /// <summary>The drawn liquid's bounds (Unity, root-local), apart from Bounds (the terrain's: the orbit's framing and the
    /// logged bounds); HasLiquidBounds false without drawn liquid. A liquid can lie above the terrain's top (an ocean over
    /// its seabed): the top-down capture's clip range takes both (WmvMain.TopDownRange).</summary>
    public Bounds LiquidBounds;
    public bool HasLiquidBounds;

    readonly List<GameObject> objects = new List<GameObject>();
    readonly List<Mesh> meshes = new List<Mesh>();
    readonly List<Material> materials = new List<Material>();
    readonly List<Texture2D> textures = new List<Texture2D>();
    readonly List<long> textureBytes = new List<long>();
    readonly List<Texture2DArray> alphaArrays = new List<Texture2DArray>();

    public int GameObjectCount { get { return objects.Count; } }
    public int MeshCount { get { return meshes.Count; } }
    public int MaterialCount { get { return materials.Count; } }
    public int TextureCount { get { return textures.Count; } }
    public int AlphaArrayCount { get { return alphaArrays.Count; } }

    /// <summary>Terrain tiles built and not yet disposed, across the player (see WmvRuntimeModel.Live for why a count:
    /// Object.Destroy is deferred to the end of the frame).</summary>
    public static int Live { get { return live; } }
    static int live;

    /// <summary>
    /// The Meshes, Materials and GameObjects every terrain tile not yet disposed holds, across the player: runtimeState's
    /// terrainMeshes, terrainMaterials and terrainGameObjects -- and (protocol 13) its layer textures, their GPU bytes
    /// and its alpha arrays: terrainTextures, terrainTextureBytes, terrainAlphaArrays. Each tile adds an object the moment
    /// it tracks it (so a tile still being built, before the session commits it, is counted) and takes all of them off
    /// when it is released (Dispose or DisposeImmediate), so the tallies never wait for a deferred Object.Destroy and are
    /// 0 when no tile is held. The persistent scene root _WmvTerrain is no tile's, so it is never counted.
    /// </summary>
    public static int LiveMeshes { get { return liveMeshes; } }
    public static int LiveMaterials { get { return liveMaterials; } }
    public static int LiveGameObjects { get { return liveGameObjects; } }
    public static int LiveTextures { get { return liveTextures; } }
    public static long LiveTextureBytes { get { return liveTextureBytes; } }
    public static int LiveAlphaArrays { get { return liveAlphaArrays; } }
    static int liveMeshes, liveMaterials, liveGameObjects, liveTextures, liveAlphaArrays;
    static long liveTextureBytes;
    bool disposed;

    public WmvRuntimeTerrainTile(int mapId, int tileX, int tileY, int load)
    {
        MapId = mapId;
        TileX = tileX;
        TileY = tileY;
        Load = load;
        live++;
    }

    public bool Disposed { get { return disposed; } }

    public void Track(GameObject go) { if (go != null) { objects.Add(go); liveGameObjects++; } }
    public void Track(Mesh m) { if (m != null) { meshes.Add(m); liveMeshes++; } }
    public void Track(Material m) { if (m != null) { materials.Add(m); liveMaterials++; } }
    /// <summary>A layer texture and its GPU bytes (WmvTerrainTexture.GpuBytes).</summary>
    public void Track(Texture2D t, long gpuBytes)
    {
        if (t == null) return;
        textures.Add(t);
        textureBytes.Add(gpuBytes);
        liveTextures++;
        liveTextureBytes += gpuBytes;
        TextureGpuBytes += gpuBytes;
    }
    public void Track(Texture2DArray a, long gpuBytes)
    {
        if (a == null) return;
        alphaArrays.Add(a);
        liveAlphaArrays++;
        AlphaGpuBytes += gpuBytes;
    }

    /// <summary>Stage 09: a liquid object and its mesh (null when making it threw), tracked as every object is and counted as
    /// the tile's liquid; `bytes` its geometry's GPU bytes.</summary>
    public void TrackLiquid(GameObject go, Mesh m, long bytes)
    {
        if (go != null)
        {
            Track(go);
            LiquidObjectCount++;
            WmvLiquidMeshes.LiveObjects++;
        }
        if (m != null)
        {
            Track(m);
            LiquidMeshCount++;
            LiquidGeometryBytes += bytes;
            WmvLiquidMeshes.LiveMeshes++;
            WmvLiquidMeshes.LiveGeometryBytes += bytes;
        }
    }

    /// <summary>A layer texture taken off the tile (WmvTerrainBuilder.Build, after an upload failure: a texture no chunk
    /// drawn textured binds any more): off its lists and every tally (TextureGpuBytes, LiveTextures, LiveTextureBytes) at
    /// once, and destroyed now (it is bound by nothing). False when the tile does not hold it.</summary>
    public bool Untrack(Texture2D t)
    {
        int i = t != null ? textures.IndexOf(t) : -1;
        if (i < 0) return false;
        long b = textureBytes[i];
        textures.RemoveAt(i);
        textureBytes.RemoveAt(i);
        liveTextures--;
        liveTextureBytes -= b;
        TextureGpuBytes -= b;
        UnityEngine.Object.DestroyImmediate(t);
        return true;
    }

    /// <summary>A layer texture handed to another tile (WmvTerrainSession.RebuildShown: the tile built again in another
    /// view, which binds it): off this tile's lists and every tally at once, NOT destroyed -- the other tile tracks it
    /// next. False when the tile does not hold it.</summary>
    public bool Hand(Texture2D t, out long gpuBytes)
    {
        gpuBytes = 0;
        int i = t != null ? textures.IndexOf(t) : -1;
        if (i < 0) return false;
        gpuBytes = textureBytes[i];
        textures.RemoveAt(i);
        textureBytes.RemoveAt(i);
        liveTextures--;
        liveTextureBytes -= gpuBytes;
        TextureGpuBytes -= gpuBytes;
        foreach (KeyValuePair<uint, Texture2D> kv in new List<KeyValuePair<uint, Texture2D>>(Textures))
            if (kv.Value == t)
                Textures.Remove(kv.Key);
        return true;
    }

    /// <summary>Every Unity object the tile holds, newest last (the self-test's deferred disposal releases them itself).</summary>
    public List<UnityEngine.Object> OwnedObjects()
    {
        var all = new List<UnityEngine.Object>();
        foreach (GameObject g in objects) all.Add(g);
        foreach (Mesh m in meshes) all.Add(m);
        foreach (Material m in materials) all.Add(m);
        foreach (Texture2D t in textures) all.Add(t);
        foreach (Texture2DArray a in alphaArrays) all.Add(a);
        if (SamplerCarrier != null) all.Add(SamplerCarrier);
        return all;
    }

    /// <summary>The bounds of chunk n's mesh, false when the chunk has no triangles (and so no mesh).</summary>
    public bool TryGetChunkBounds(int n, out Bounds b)
    {
        b = new Bounds();
        if (n < 0 || n >= ChunkMeshes.Length || ChunkMeshes[n] == null)
            return false;
        b = ChunkBounds[n];
        return true;
    }

    /// <summary>Release everything with Object.Destroy (deferred to the end of the frame): production.</summary>
    public void Dispose() { Release(false); }

    /// <summary>Release everything with Object.DestroyImmediate: the self-test, which counts Unity objects within one
    /// frame and needs them gone when it counts.</summary>
    public void DisposeImmediate() { Release(true); }

    /// <summary>Streaming (stage 08): released now -- off every tally, its root hidden -- and its objects handed to `later`, which
    /// destroys them over the next frames (a closed tile's hundreds of objects never in one frame).</summary>
    public void DisposeLater(List<UnityEngine.Object> later)
    {
        if (Root != null)
            Root.SetActive(false);
        laterSink = later;
        Release(false);
        laterSink = null;
    }

    List<UnityEngine.Object> laterSink;

    void Release(bool immediate)
    {
        if (!disposed) { disposed = true; live--; }
        // Off the tallies now, whether or not the Destroy below is deferred: what is listed is released here.
        liveGameObjects -= objects.Count;
        liveMeshes -= meshes.Count;
        liveMaterials -= materials.Count;
        liveTextures -= textures.Count;
        foreach (long b in textureBytes) liveTextureBytes -= b;
        liveAlphaArrays -= alphaArrays.Count;
        // Stage 09: its liquid off the liquid tallies, and the liquid types it held given back to their registry.
        WmvLiquidMeshes.LiveObjects -= LiquidObjectCount;
        WmvLiquidMeshes.LiveMeshes -= LiquidMeshCount;
        WmvLiquidMeshes.LiveGeometryBytes -= LiquidGeometryBytes;
        LiquidObjectCount = LiquidMeshCount = 0;
        LiquidGeometryBytes = 0;
        if (Liquids != null)
        {
            Liquids.Release(this);
            Liquids = null;
        }
        // Children were created after their parents: newest first, so DestroyImmediate never meets a child its parent
        // already took with it (a destroyed object compares equal to null and is skipped either way).
        for (int i = objects.Count - 1; i >= 0; i--)
            Destroy(objects[i], immediate);
        foreach (Mesh m in meshes) Destroy(m, immediate);
        foreach (Material m in materials) Destroy(m, immediate);
        foreach (Texture2D t in textures) Destroy(t, immediate);
        foreach (Texture2DArray a in alphaArrays) Destroy(a, immediate);
        Destroy(SamplerCarrier, immediate);
        SamplerCarrier = null;
        objects.Clear();
        meshes.Clear();
        materials.Clear();
        textures.Clear();
        textureBytes.Clear();
        alphaArrays.Clear();
        Textures.Clear();
        AlphaArray = null;
        Root = null;
        SurfaceMaterial = LineMaterial = null;
        Array.Clear(ChunkObjects, 0, ChunkObjects.Length);
        Array.Clear(ChunkMeshes, 0, ChunkMeshes.Length);
        Array.Clear(LineObjects, 0, LineObjects.Length);
        Array.Clear(ChunkMaterials, 0, ChunkMaterials.Length);
    }

    void Destroy(UnityEngine.Object o, bool immediate)
    {
        if (o == null) return;
        if (laterSink != null) laterSink.Add(o);
        else if (immediate) UnityEngine.Object.DestroyImmediate(o);
        else UnityEngine.Object.Destroy(o);
    }
}

public static class WmvTerrainBuilder
{
    /// <summary>The world layer. A number, never a name: the repository carries no ProjectSettings.</summary>
    public const int Layer = 30;
    /// <summary>The main camera in the terrain view: the terrain layer only.</summary>
    public const int TerrainViewMask = 1 << Layer;
    /// <summary>The main camera in the model view (set in WmvMain.Awake): everything but the terrain layer.</summary>
    public const int ModelViewMask = ~(1 << Layer);

    /// <summary>A view's mask less the terrain layer: what the shadow rig, the light check and the mount probe render.</summary>
    public static int WithoutTerrain(int mask) { return mask & ~TerrainViewMask; }

    /// <summary>The terrain shader under Assets/Resources (never stripped from a player build).</summary>
    public const string ShaderResource = "WmvTerrain";
    public const string ShaderName = "Wmv/Terrain";

    /// <summary>
    /// How far the line pass moves its depth toward the camera, as a fraction of clip-space w (so this much of the depth
    /// range in normalised device units, whatever the distance). Lines lie exactly on their triangles' edges; without
    /// it they fight the surface for the depth test. Measured by the self-test (TerrainTests, a flat tile -- every
    /// triangle coplanar -- at 512 px; edge pixels shown over their own surfaces): the tile framed as the terrain view
    /// frames it needs 1e-5, a focused chunk 5e-5, a chunk seen 10 degrees above the horizon 1e-4 (0 shows 98 %, 63 %
    /// and 70 % of them).
    /// THE PRICE grows with the square of the depth. Under reversed-Z the device depth of a point at view depth D is
    /// about near / D, so a constant k added to it moves an edge at depth D toward the camera by about k * D^2 / near.
    /// The orbit's near plane is 1 % of its pivot distance d (and never below far * 1e-4, which takes over at the zoom
    /// floor), so at this k that is about 1 % * (D / d) of D: 1 % at the pivot, more beyond it. Worked through the
    /// orbit's own plane rule: the tile framed (d 952, near 9.52) moves an edge about 3.5 yd at its nearest corner, 9 yd
    /// at the pivot and 17.5 yd at its far corner (about 1300 yd away); a focused chunk (d 59, near 0.59) moves it 0.6 yd
    /// at the chunk but 14.5 yd at 300 yd and 39 yd at 500 yd for the tile behind it; at the zoom floor the shift is
    /// tens to over a hundred yards. An edge that far behind a ridge shows through it. Kept because only the wire
    /// views (debug views) draw lines; TerrainEdgeTests checks that edges behind a wall well beyond that price stay
    /// hidden at this value, and that the check fails at 100 times it.
    /// </summary>
    public const float LineDepthNudge = 1e-4f;

    // Shader values (Resources/WmvTerrain.shader).
    const float ViewShaded = 0f, ViewChunks = 1f, ViewNormals = 2f, ViewLines = 3f;
    static readonly Color LineOnShaded = new Color(0.08f, 0.08f, 0.09f, 1f);    // dark edges over the grey surfaces
    static readonly Color LineOnClear = new Color(0.85f, 0.85f, 0.8f, 1f);      // light edges over the background

    static Shader shader;
    static bool shaderResolved;

    /// <summary>Resources/WmvTerrain.shader, once per session; null when the player was built without it.</summary>
    public static Shader ResolveShader(Action<string> log)
    {
        if (shaderResolved)
            return shader;
        shaderResolved = true;
        Shader s = Resources.Load<Shader>(ShaderResource);
        // The error shader is what a broken import hands back; it is never an answer.
        if (s != null && s.name != null && s.name.StartsWith("Hidden/", StringComparison.Ordinal))
            s = null;
        shader = s;
        if (log != null)
            log(s != null ? "terrain shader '" + s.name + "' (Resources/" + ShaderResource + ")"
                          : "Resources/" + ShaderResource + ".shader is missing from this player build -- no terrain tile can be drawn");
        return shader;
    }

    /// <summary>
    /// Build the Unity objects of `mesh` into `rt`, under `parent` (null = the scene root), INACTIVE: the root is
    /// deactivated before anything is put under it. Every object is added to `rt` as it is created; on an exception the
    /// caller disposes `rt` (WmvTerrainSession does), which releases exactly what was made. No material: every chunk is
    /// drawn as stage 03 draws it.
    /// </summary>
    public static void Build(WmvRuntimeTerrainTile rt, TerrainTileMesh mesh, Transform parent, WmvTerrainBuildOptions o,
                             Action<string> log)
    {
        Build(rt, mesh, null, parent, o, log);
    }

    /// <summary>
    /// As Build above, with the load's material and textures (`content`, null for none). In the textured views
    /// (textured, texturedlit, texturedplainweights) every chunk the material calls drawable -- and in
    /// texturedplainweights also every plain-diagnostic chunk -- is drawn by Wmv/TerrainTextured with a material of its
    /// own, its textures uploaded once per FileDataID (in textured and texturedlit on an MPHD 0x80 map also its layers'
    /// height textures) and its alpha slice (two for a chunk of more than 5 layers) in the tile's one alpha array, the
    /// layer slots without a texture holding the tile's sampler carrier; every other chunk is drawn neutral
    /// (Wmv/Terrain, shaded). In the materials view every chunk is drawn in its state's colour. A texture whose upload
    /// throws is set "decode failed" and the material's chunk states are settled again (a failed diffuse texture's
    /// layers draw the missing-texture checker; a failed height texture's chunks become HeightTextureMissing, neutral);
    /// the chunks drawn textured are picked again, every texture none of them binds any more is released (destroyed,
    /// untracked, its uploadMs reset), and the alpha array is made only when a chunk drawn textured is left.
    /// </summary>
    public static void Build(WmvRuntimeTerrainTile rt, TerrainTileMesh mesh, WmvTerrainTileContent content, Transform parent,
                             WmvTerrainBuildOptions o, Action<string> log)
    {
        if (rt == null) throw new ArgumentNullException("rt");
        if (mesh == null) throw new ArgumentNullException("mesh");
        if (mesh.Chunks == null || mesh.Chunks.Length != AdtParser.ChunksPerTile)
            throw new ArgumentException("a terrain tile mesh has 256 chunks", "mesh");
        Shader s = ResolveShader(log);
        if (s == null)
            throw new InvalidOperationException("Resources/" + ShaderResource + ".shader is missing from this player build -- " +
                                                "copy Assets/Resources into the Unity project and rebuild the player");
        var clock = System.Diagnostics.Stopwatch.StartNew();
        WmvTerrainView view = o.View;
        bool texturedView = view == WmvTerrainView.Textured || view == WmvTerrainView.TexturedLit || view == WmvTerrainView.TexturedPlainWeights;
        bool materialsView = view == WmvTerrainView.Materials;
        bool plainView = view == WmvTerrainView.TexturedPlainWeights;
        bool wire = view == WmvTerrainView.Wire || view == WmvTerrainView.ShadedWire;
        TerrainTileMaterial mat = content != null ? content.Material : null;
        bool haveChunks = mat != null && mat.Chunks != null && mat.Chunks.Length == AdtParser.ChunksPerTile;
        Shader ts = texturedView || materialsView ? WmvTerrainMaterials.ResolveShader(log) : null;
        rt.View = view;
        rt.OnlyChunk = o.OnlyChunk;
        rt.Readable = o.KeepReadable;

        // The chunks Wmv/TerrainTextured draws with their textures (again after an upload failure: it can only shrink).
        var textured = new bool[AdtParser.ChunksPerTile];
        int texturedChunks = 0;
        Action pickTextured = () =>
        {
            texturedChunks = 0;
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                textured[n] = false;
                if (ts == null || !texturedView || !haveChunks)
                    continue;
                TerrainChunkMaterial c = mat.Chunks[n];
                TerrainChunkMesh cm = mesh.Chunks[n];
                if (cm == null || cm.State != TerrainChunkState.Built || cm.Triangles == 0)
                    continue;
                if (c.Drawable || (plainView && c.PlainDiagnostic))
                {
                    textured[n] = true;
                    texturedChunks++;
                }
            }
        };
        pickTextured();

        var root = new GameObject(string.Format("Terrain map {0} tile {1}_{2} (load {3})", rt.MapId, rt.TileX, rt.TileY, rt.Load));
        rt.Track(root);
        root.layer = Layer;
        root.SetActive(false);
        if (parent != null)
            root.transform.SetParent(parent, false);
        rt.Root = root;

        // The neutral surface: always in the stage 03 views, in the others only when a chunk is drawn neutral.
        Material surface = null;
        Func<Material> neutral = () =>
        {
            if (surface != null)
                return surface;
            surface = new Material(s) { name = "WmvTerrain surface (" + WmvTerrainDebug.ViewNames[(int)(texturedView || materialsView ? WmvTerrainView.Shaded : view)] + ")" };
            rt.Track(surface);
            rt.SurfaceMaterial = surface;
            surface.SetFloat("_WmvTerrainView", view == WmvTerrainView.Chunks ? ViewChunks
                                              : view == WmvTerrainView.Normals ? ViewNormals : ViewShaded);
            // The wire view's surfaces write depth only: the background shows through them, the edges behind a ridge stay
            // hidden.
            surface.SetFloat("_ColorMask", view == WmvTerrainView.Wire ? 0f : 15f);
            return surface;
        };
        if (!texturedView && !materialsView || (ts == null))
            neutral();
        Material lines = null;
        if (wire)
        {
            lines = new Material(s) { name = "WmvTerrain edges" };
            rt.Track(lines);
            rt.LineMaterial = lines;
            lines.SetFloat("_WmvTerrainView", ViewLines);
            lines.SetFloat("_WmvLineNudge", LineDepthNudge);
            lines.SetColor("_WmvLineColour", view == WmvTerrainView.Wire ? LineOnClear : LineOnShaded);
            // After every opaque surface, so an edge is tested against the finished depth of the tile.
            lines.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Geometry + 1;
        }

        // The textures (one per FileDataID: the layers' diffuse textures and, where the height mix is drawn, their height
        // textures) and the alpha array of the chunks drawn textured.
        Texture2DArray alpha = null;
        int uploadFailures = 0, heightTextures = 0, unbound = 0;
        bool heightView = !plainView;
        // The texture of a layer in its role: a height texture also needs an alpha channel (the height is its alpha).
        Func<uint, Texture2D> uploaded = id =>
        {
            Texture2D x;
            return rt.Textures.TryGetValue(id, out x) ? x : null;
        };
        Func<uint, Texture2D> uploadedHeight = id => content != null && content.UsableAsHeight(id) != null ? uploaded(id) : null;
        if (texturedChunks > 0)
        {
            var asHeight = new HashSet<uint>();
            Action<uint, bool> upload = (id, height) =>
            {
                if (id == 0 || rt.Textures.ContainsKey(id))
                    return;
                WmvTerrainTexture t = height ? content.UsableAsHeight(id) : content.Usable(id);
                if (t == null || !t.Ready)
                    return;
                Texture2D made = null;
                try
                {
                    long gpu = t.GpuBytes;
                    rt.Textures[id] = t.Upload(x => { made = x; rt.Track(x, gpu); });
                    if (height && asHeight.Add(id)) heightTextures++;
                }
                catch (Exception e)
                {
                    // A texture the upload made before it threw is bound by nothing: off the tile and its tallies now.
                    if (made != null)
                        rt.Untrack(made);
                    t.Fail(WmvTerrainTexture.StateDecodeFailed, "upload failed: " + e.GetType().Name + ": " + e.Message);
                    uploadFailures++;
                }
            };
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                if (!textured[n])
                    continue;
                TerrainChunkMaterial c = mat.Chunks[n];
                for (int k = 0; k < c.Layers.Length && k < TerrainMaterialBuilder.MaxDrawnLayers; k++)
                {
                    TerrainMaterialLayer l = c.Layers[k];
                    if (!l.Missing && l.RepeatsPerChunk > 0)          // repeats 0 (unresolved): drawn as the checker
                        upload(l.TextureFileDataID, false);
                    if (heightView && c.UseHeightTextures && c.Drawable)
                        upload(l.HeightTextureFileDataID, true);
                }
            }
            if (uploadFailures > 0)
            {
                List<uint> failedDiffuse, failedHeight;
                content.SettleFailures(out failedDiffuse, out failedHeight);
                pickTextured();
                // The re-pick dropped every chunk that reads the failed texture; a texture uploaded for those chunks alone is
                // bound by no chunk drawn textured now. It goes -- destroyed, off the tile's lists and tallies, its upload time
                // reset -- so the GPU contents and the report are those of the same failure seen at fetch time (nothing
                // uploaded for those chunks: "available", no uploadMs).
                var bound = new HashSet<uint>();
                for (int n = 0; n < AdtParser.ChunksPerTile; n++)
                {
                    if (!textured[n])
                        continue;
                    TerrainChunkMaterial c = mat.Chunks[n];
                    for (int k = 0; k < c.Layers.Length && k < TerrainMaterialBuilder.MaxDrawnLayers; k++)
                    {
                        TerrainMaterialLayer l = c.Layers[k];
                        if (!l.Missing && l.RepeatsPerChunk > 0 && l.TextureFileDataID != 0)
                            bound.Add(l.TextureFileDataID);
                        if (heightView && c.UseHeightTextures && c.Drawable && l.HeightTextureFileDataID != 0)
                            bound.Add(l.HeightTextureFileDataID);
                    }
                }
                foreach (uint id in new List<uint>(rt.Textures.Keys))
                {
                    if (bound.Contains(id))
                        continue;
                    rt.Untrack(rt.Textures[id]);
                    rt.Textures.Remove(id);
                    WmvTerrainTexture t;
                    if (content.Textures.TryGetValue(id, out t))
                        t.UploadMs = -1;
                    if (asHeight.Remove(id))
                        heightTextures--;
                    unbound++;
                }
            }
            // The alpha array only for chunks drawn textured (none may be left after a re-pick).
            if (texturedChunks > 0)
            {
                var alphaClock = System.Diagnostics.Stopwatch.StartNew();
                int slices = mat.DrawableSlices + (plainView ? mat.PlainSlices : 0);
                if (slices > 0)
                {
                    byte[] bytes = mat.BuildAlphaArray(plainView);
                    long alphaBytes = (long)slices * TerrainChunkMaterial.AlphaSliceBytes;
                    alpha = WmvTerrainMaterials.BuildAlphaArray(bytes, slices, a => rt.Track(a, alphaBytes));
                    rt.AlphaArray = alpha;
                }
                rt.AlphaBuildMs = alphaClock.Elapsed.TotalMilliseconds;
                // The layer slots without a texture hold the tile's sampler carrier (their height texture is sampled with
                // the slot's sampler); held by the tile the moment it exists.
                WmvTerrainMaterials.CreateSamplerCarrier(x => rt.SamplerCarrier = x);
            }
        }
        float texturedViewValue = view == WmvTerrainView.TexturedLit ? WmvTerrainMaterials.ViewLit
                                : plainView ? WmvTerrainMaterials.ViewPlainWeights : WmvTerrainMaterials.ViewTextured;
        var stateMaterials = new Dictionary<int, Material>();
        Vector2[] uv = texturedChunks > 0 ? WmvTerrainMaterials.ChunkUv() : null;
        var materialClock = new System.Diagnostics.Stopwatch();

        var block = new MaterialPropertyBlock();
        var positions = new Vector3[TerrainMeshBuilder.VerticesPerChunk];
        var normals = new Vector3[TerrainMeshBuilder.VerticesPerChunk];
        int edges = 0;
        for (int n = 0; n < AdtParser.ChunksPerTile; n++)
        {
            TerrainChunkMesh c = mesh.Chunks[n];
            if (c == null || c.State != TerrainChunkState.Built || c.Triangles == 0)
                continue;                                     // no triangles: no object
            var go = new GameObject("MCNK " + n + " (" + c.IndexX + "," + c.IndexY + ")");
            rt.Track(go);
            go.layer = Layer;
            go.transform.SetParent(root.transform, false);

            for (int k = 0; k < TerrainMeshBuilder.VerticesPerChunk; k++)
            {
                positions[k] = new Vector3(c.Positions[3 * k], c.Positions[3 * k + 1], c.Positions[3 * k + 2]);
                normals[k] = new Vector3(c.Normals[3 * k], c.Normals[3 * k + 1], c.Normals[3 * k + 2]);
            }
            var m = new Mesh { name = "WmvTerrain MCNK " + n };
            rt.Track(m);
            m.indexFormat = UnityEngine.Rendering.IndexFormat.UInt16;
            m.SetVertices(positions);
            m.SetNormals(normals);
            m.SetIndices(c.Indices, MeshTopology.Triangles, 0, false);
            if (textured[n])
            {
                // The material's vertex data, apart from the geometry: UV0 in cells and, with MCCV, float colours.
                Color[] colours = WmvTerrainMaterials.ChunkColors(mat.Chunks[n]);
                m.SetUVs(0, uv);
                if (colours != null)
                    m.SetColors(colours);
                long vb = WmvTerrainMaterials.VertexBytes(colours != null);
                rt.MaterialVertexBytes += vb;
                rt.UploadedBytes += vb;
            }
            Bounds b = BoundsOf(c.BoundsMin, c.BoundsMax);
            m.bounds = b;
            rt.ChunkMeshes[n] = m;
            rt.ChunkBounds[n] = b;
            rt.UploadedBytes += TerrainMeshBuilder.VerticesPerChunk * 24L + c.Indices.Length * 2L;

            go.AddComponent<MeshFilter>().sharedMesh = m;
            MeshRenderer mr = go.AddComponent<MeshRenderer>();
            Quiet(mr);
            if (textured[n])
            {
                // One material per chunk, every value in it (no MaterialPropertyBlock: the SRP Batcher's condition).
                TerrainChunkMaterial cmat = mat.Chunks[n];
                materialClock.Start();
                Material cm = WmvTerrainMaterials.ChunkMaterial(ts, cmat, uploaded, alpha, rt.SamplerCarrier, texturedViewValue, plainView,
                                                                uploadedHeight);
                rt.Track(cm);
                materialClock.Stop();
                rt.TexturedMaterialCount++;
                mr.sharedMaterial = cm;
                rt.ChunkTextured[n] = true;
            }
            else if (materialsView && ts != null)
            {
                int key = haveChunks ? (int)mat.Chunks[n].State : -1;
                Material sm;
                if (!stateMaterials.TryGetValue(key, out sm))
                {
                    sm = WmvTerrainMaterials.StateMaterial(ts, key >= 0 ? TerrainTileMaterial.StateName((TerrainMaterialState)key) : "no material",
                                                           key >= 0 ? WmvTerrainMaterials.StateColour((TerrainMaterialState)key)
                                                                    : WmvTerrainMaterials.NoMaterialColour);
                    rt.Track(sm);
                    rt.TexturedMaterialCount++;
                    stateMaterials[key] = sm;
                }
                mr.sharedMaterial = sm;
                rt.ChunkTextured[n] = true;
            }
            else
            {
                mr.sharedMaterial = neutral();
                block.SetFloat("_WmvChunkIndex", n);
                mr.SetPropertyBlock(block);
            }
            rt.ChunkMaterials[n] = mr.sharedMaterial;
            rt.ChunkObjects[n] = go;
            rt.ChunkObjectCount++;

            if (wire)
            {
                ushort[] pairs = UniqueEdges(c.Indices);
                var lgo = new GameObject("edges");
                rt.Track(lgo);
                lgo.layer = Layer;
                lgo.transform.SetParent(go.transform, false);
                var lm = new Mesh { name = "WmvTerrain edges MCNK " + n };
                rt.Track(lm);
                lm.indexFormat = UnityEngine.Rendering.IndexFormat.UInt16;
                lm.SetVertices(positions);
                lm.SetNormals(normals);
                lm.SetIndices(pairs, MeshTopology.Lines, 0, false);
                lm.bounds = b;
                lgo.AddComponent<MeshFilter>().sharedMesh = lm;
                MeshRenderer lr = lgo.AddComponent<MeshRenderer>();
                Quiet(lr);
                lr.sharedMaterial = lines;
                rt.LineObjects[n] = lgo;
                rt.LineObjectCount++;
                rt.UploadedBytes += TerrainMeshBuilder.VerticesPerChunk * 24L + pairs.Length * 2L;
                edges += pairs.Length / 2;
                if (!o.KeepReadable)
                    lm.UploadMeshData(true);
            }
            if (!o.KeepReadable)
                m.UploadMeshData(true);
            if (o.OnlyChunk >= 0 && n != o.OnlyChunk)
                go.SetActive(false);
        }
        rt.MaterialCreateMs = rt.TexturedMaterialCount > 0 ? materialClock.Elapsed.TotalMilliseconds : -1;
        rt.HasBounds = mesh.BoundsMin != null && mesh.BoundsMax != null;
        rt.Bounds = rt.HasBounds ? BoundsOf(mesh.BoundsMin, mesh.BoundsMax) : new Bounds();
        // Stage 09: the tile's liquid (its MH2O, built on the worker with the terrain: content.LiquidTile), one object per liquid
        // type under the root, whatever the terrain view (a liquid is its own object, never part of a chunk's material).
        TerrainLiquidTile liquid = content != null ? content.LiquidTile : null;
        var liquidClock = System.Diagnostics.Stopwatch.StartNew();
        int liquidDrawn = content != null ? WmvLiquidMeshes.BuildTileLiquids(rt, liquid, content.Liquids, root.transform, o.KeepReadable) : 0;
        if (log != null && liquid != null && liquid.Surfaces.Count > 0)
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "tile {0}_{1} (load {2}) liquid: {3} of {4} surface(s) drawn in {9:0.###} ms, {5} object(s), {6} mesh byte(s); types [{7}]{8}",
                rt.TileX, rt.TileY, rt.Load, liquidDrawn, liquid.Surfaces.Count, rt.LiquidObjectCount, rt.LiquidGeometryBytes,
                string.Join(",", rt.LiquidTypes.ConvertAll(x => x.ToString(System.Globalization.CultureInfo.InvariantCulture)).ToArray()),
                liquidDrawn < liquid.Surfaces.Count ? " (a type the catalogue lacks, or the liquid shader missing, is not drawn)" : "",
                liquidClock.Elapsed.TotalMilliseconds));
        if (log != null)
        {
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "built tile {0}_{1} of map {2} (load {3}) inactive in {4:F1} ms: {5} chunk object(s){6}, {7} mesh(es), {8} material(s), " +
                "{9} game object(s) on layer {10}, {11} bytes uploaded{12}; view {13}{14}{15}",
                rt.TileX, rt.TileY, rt.MapId, rt.Load, clock.Elapsed.TotalMilliseconds, rt.ChunkObjectCount,
                wire ? " and " + rt.LineObjectCount + " edge object(s) (" + edges + " unique edges)" : "", rt.MeshCount,
                rt.MaterialCount, rt.GameObjectCount, Layer, rt.UploadedBytes,
                o.KeepReadable ? " (CPU copies kept for the dump)" : " (CPU copies released)", WmvTerrainDebug.ViewNames[(int)view],
                o.OnlyChunk >= 0 ? ", only chunk " + o.OnlyChunk + (rt.ChunkObjects[o.OnlyChunk] != null ? "" : " (it has no triangles: nothing is shown)") : "",
                rt.HasBounds ? ", bounds " + rt.Bounds.min.ToString("F3") + " .. " + rt.Bounds.max.ToString("F3") : ", no triangle"));
            if (texturedView || materialsView)
                log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "tile {0}_{1} (load {2}) material {3}: {4} chunk(s) drawn by Wmv/TerrainTextured{5}, {6} neutral; {7} texture(s) " +
                    "uploaded ({8} GPU bytes{9}), alpha array {10} ({11} GPU bytes, {12:0.###} ms){17}, {13} material vertex bytes, {14} " +
                    "textured material(s) made in {15:0.###} ms{16}",
                    rt.TileX, rt.TileY, rt.Load, mat != null ? TerrainTileMaterial.TileStateName(mat.State) : "none",
                    materialsView ? rt.ChunkObjectCount : texturedChunks,
                    plainView ? " (PLAIN WEIGHTS, hatched: a comparison, not the composite)"
                              : mat != null && mat.UseHeightTextures
                                  ? string.Format(" (the height mix: MPHD 0x{0:X} has 0x80, {1} height texture(s))", mat.MphdFlags, heightTextures)
                                  : string.Format(" (the height mix, H = 1: MPHD 0x{0:X} has no 0x80)", mat != null ? mat.MphdFlags : 0u),
                    rt.ChunkObjectCount - (materialsView && ts != null ? rt.ChunkObjectCount : texturedChunks), rt.Textures.Count,
                    rt.TextureGpuBytes, uploadFailures > 0 ? ", " + uploadFailures + " upload(s) FAILED, " + unbound +
                                                             " texture(s) no chunk drawn textured binds any more released" : "",
                    alpha != null ? alpha.depth + " slice(s)" : "none", rt.AlphaGpuBytes, rt.AlphaBuildMs, rt.MaterialVertexBytes,
                    rt.TexturedMaterialCount, rt.MaterialCreateMs,
                    ts == null ? " -- Resources/" + WmvTerrainMaterials.ShaderResource + ".shader is missing: every chunk drawn neutral" : "",
                    rt.SamplerCarrier != null ? ", sampler carrier " + rt.SamplerCarrier.width + "x" + rt.SamplerCarrier.height +
                                                " (the layer slots without a texture; never drawn)" : ""));
            if (materialsView && ts != null)
                log("materials view legend: " + WmvTerrainMaterials.Legend());
        }
    }

    /// <summary>A geometry diagnostic casts and receives nothing and takes no probe.</summary>
    static void Quiet(MeshRenderer r)
    {
        r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        r.receiveShadows = false;
        r.lightProbeUsage = UnityEngine.Rendering.LightProbeUsage.Off;
        r.reflectionProbeUsage = UnityEngine.Rendering.ReflectionProbeUsage.Off;
        r.motionVectorGenerationMode = MotionVectorGenerationMode.ForceNoMotion;
    }

    public static Bounds BoundsOf(float[] min, float[] max)
    {
        var b = new Bounds();
        b.SetMinMax(new Vector3(min[0], min[1], min[2]), new Vector3(max[0], max[1], max[2]));
        return b;
    }

    /// <summary>Each edge of a triangle list once, as index pairs (lower index first), in the order first met.</summary>
    public static ushort[] UniqueEdges(ushort[] triangles)
    {
        var seen = new HashSet<int>();
        var pairs = new List<ushort>(triangles.Length);
        for (int t = 0; t + 2 < triangles.Length; t += 3)
            for (int e = 0; e < 3; e++)
            {
                int a = triangles[t + e], b = triangles[t + (e + 1) % 3];
                int lo = Math.Min(a, b), hi = Math.Max(a, b);
                if (seen.Add(lo * 65536 + hi))
                {
                    pairs.Add((ushort)lo);
                    pairs.Add((ushort)hi);
                }
            }
        return pairs.ToArray();
    }

    /// <summary>The counters a terrainTileLoaded report carries, sampled now: the runtimes alive, the Unity Mesh and
    /// Material objects alive, the GameObjects the terrain tiles hold (the tiles' own tally, as runtimeState's
    /// terrainGameObjects: tile roots, chunk and edge objects, never the scene root _WmvTerrain), the managed heap and
    /// Unity's native allocations.</summary>
    public static void SampleCounters(WmvIpcClient.TerrainTileReport r)
    {
        r.LiveTerrainTiles = WmvRuntimeTerrainTile.Live;
        r.LiveModels = WmvRuntimeModel.Live;
        r.LiveMapObjects = WmvRuntimeMapObject.Live;
        r.MeshObjects = Resources.FindObjectsOfTypeAll<Mesh>().Length;
        r.MaterialObjects = Resources.FindObjectsOfTypeAll<Material>().Length;
        r.TerrainGameObjects = WmvRuntimeTerrainTile.LiveGameObjects;
        r.ManagedHeapBytes = GC.GetTotalMemory(false);
        r.NativeAllocatedBytes = UnityEngine.Profiling.Profiler.GetTotalAllocatedMemoryLong();
    }
}
