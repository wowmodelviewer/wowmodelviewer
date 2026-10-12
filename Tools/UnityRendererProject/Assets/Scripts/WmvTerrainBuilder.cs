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
//
// Debug switches (WmvTerrainDebug; command line or the space-separated WMV_DEBUG, as the other -wmv*
// switches): -wmvTerrainView=shaded|chunks|wire|shadedwire|normals, -wmvTerrainChunk=N (only chunk N is
// shown), -wmvTerrainFocus=N (frame chunk N), -wmvTerrainTopDown (orthographic top-down camera, design
// 2.3b) and -wmvTerrainDump=<bare name> (the meshes read back into <player folder>/<name>.txt, the
// TerrainDump format, for Tools/world-audit/compare_terrain_mesh.py).

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow.World;

/// <summary>How the terrain tile is drawn (-wmvTerrainView). Every view is a geometry diagnostic.</summary>
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
}

/// <summary>
/// The terrain debug switches, read the way WmvModelBuilder.Debug_ reads the others: from the player's command line
/// AND from the WMV_DEBUG environment variable, split on space, tab, comma and semicolon (the host-launched player
/// inherits WMV_DEBUG). Read once.
/// </summary>
public static class WmvTerrainDebug
{
    public static readonly string[] ViewNames = { "shaded", "chunks", "wire", "shadedwire", "normals" };

    static bool parsed;
    static WmvTerrainView view = WmvTerrainView.Shaded;
    static int onlyChunk = -1, focus = -1;
    static bool topDown;
    static string dumpName;
    static readonly List<string> notes = new List<string>();

    public static WmvTerrainView View { get { Parse(); return view; } }
    public static string ViewName { get { return ViewNames[(int)View]; } }
    /// <summary>-wmvTerrainChunk=N: only chunk N is shown (the others are built, and inactive); -1 = all.</summary>
    public static int OnlyChunk { get { Parse(); return onlyChunk; } }
    /// <summary>-wmvTerrainFocus=N: the camera frames chunk N instead of the tile; -1 = the tile.</summary>
    public static int Focus { get { Parse(); return focus; } }
    /// <summary>-wmvTerrainTopDown: the orthographic top-down camera of design 2.3b.</summary>
    public static bool TopDown { get { Parse(); return topDown; } }
    /// <summary>-wmvTerrainDump=name: a bare file name (no folder), or null when not asked for.</summary>
    public static string DumpName { get { Parse(); return dumpName; } }
    /// <summary>What the parse refused (a bad value is said once in the log, never guessed at).</summary>
    public static IList<string> Notes { get { Parse(); return notes; } }

    public static string Describe()
    {
        Parse();
        return string.Format("view {0}{1}{2}{3}{4}", ViewName, onlyChunk >= 0 ? ", only chunk " + onlyChunk : "",
                             focus >= 0 ? ", focus chunk " + focus : "", topDown ? ", top-down" : "",
                             dumpName != null ? ", dump " + dumpName : "");
    }

    static void Parse()
    {
        if (parsed) return;
        parsed = true;
        var args = new List<string>(Environment.GetCommandLineArgs());
        string fromEnv = Environment.GetEnvironmentVariable("WMV_DEBUG");
        if (!string.IsNullOrEmpty(fromEnv))
            args.AddRange(fromEnv.Split(new[] { ' ', '\t', ',', ';' }, StringSplitOptions.RemoveEmptyEntries));
        foreach (string a in args)
        {
            if (a.StartsWith("-wmvTerrainView=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainView=".Length);
                int i = Array.IndexOf(ViewNames, v);
                if (i >= 0) view = (WmvTerrainView)i;
                else notes.Add("-wmvTerrainView=" + v + " is not one of " + string.Join("|", ViewNames) + " -- the shaded view is drawn");
            }
            else if (a.StartsWith("-wmvTerrainChunk=", StringComparison.Ordinal))
                onlyChunk = ChunkIndex(a, "-wmvTerrainChunk=");
            else if (a.StartsWith("-wmvTerrainFocus=", StringComparison.Ordinal))
                focus = ChunkIndex(a, "-wmvTerrainFocus=");
            else if (a == "-wmvTerrainTopDown")
                topDown = true;
            else if (a.StartsWith("-wmvTerrainDump=", StringComparison.Ordinal))
            {
                string v = a.Substring("-wmvTerrainDump=".Length);
                if (v.Length > 0 && v.IndexOfAny(new[] { '/', '\\', ':' }) < 0 && v != "." && v != "..")
                    dumpName = v;
                else
                    notes.Add("-wmvTerrainDump=" + v + " is not a bare file name -- no dump is written");
            }
        }
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
/// triangles, the line objects of a wire view, and its materials. WmvTerrainBuilder adds each object the moment it
/// creates it (Track), so Dispose releases exactly what exists whenever it is called.
/// </summary>
public class WmvRuntimeTerrainTile
{
    public readonly int MapId, TileX, TileY, Load;
    public GameObject Root;
    public readonly GameObject[] ChunkObjects = new GameObject[AdtParser.ChunksPerTile];   // null: no triangles
    public readonly Mesh[] ChunkMeshes = new Mesh[AdtParser.ChunksPerTile];
    public readonly GameObject[] LineObjects = new GameObject[AdtParser.ChunksPerTile];   // wire views only
    public readonly Bounds[] ChunkBounds = new Bounds[AdtParser.ChunksPerTile];            // valid where ChunkMeshes is set
    public Material SurfaceMaterial, LineMaterial;
    /// <summary>The tile's bounds (Unity, root-local) from the vertices its triangles reference; HasBounds false when
    /// it has no triangle.</summary>
    public Bounds Bounds;
    public bool HasBounds;
    public WmvTerrainView View;
    public bool Readable;
    public int ChunkObjectCount, LineObjectCount;
    /// <summary>Bytes handed to the GPU: positions and normals (24 per vertex), 16-bit triangle and line indices.</summary>
    public long UploadedBytes;

    readonly List<GameObject> objects = new List<GameObject>();
    readonly List<Mesh> meshes = new List<Mesh>();
    readonly List<Material> materials = new List<Material>();

    public int GameObjectCount { get { return objects.Count; } }
    public int MeshCount { get { return meshes.Count; } }
    public int MaterialCount { get { return materials.Count; } }

    /// <summary>Terrain tiles built and not yet disposed, across the player (see WmvRuntimeModel.Live for why a count:
    /// Object.Destroy is deferred to the end of the frame).</summary>
    public static int Live { get { return live; } }
    static int live;

    /// <summary>
    /// The Meshes, Materials and GameObjects every terrain tile not yet disposed holds, across the player: runtimeState's
    /// terrainMeshes, terrainMaterials and terrainGameObjects. Each tile adds an object the moment it tracks it (so a
    /// tile still being built, before the session commits it, is counted) and takes all of them off when it is
    /// released (Dispose or DisposeImmediate), so the tallies never wait for a deferred Object.Destroy and are 0 when no
    /// tile is held. The persistent scene root _WmvTerrain is no tile's, so it is never counted.
    /// </summary>
    public static int LiveMeshes { get { return liveMeshes; } }
    public static int LiveMaterials { get { return liveMaterials; } }
    public static int LiveGameObjects { get { return liveGameObjects; } }
    static int liveMeshes, liveMaterials, liveGameObjects;
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

    void Release(bool immediate)
    {
        if (!disposed) { disposed = true; live--; }
        // Off the tallies now, whether or not the Destroy below is deferred: what is listed is released here.
        liveGameObjects -= objects.Count;
        liveMeshes -= meshes.Count;
        liveMaterials -= materials.Count;
        // Children were created after their parents: newest first, so DestroyImmediate never meets a child its parent
        // already took with it (a destroyed object compares equal to null and is skipped either way).
        for (int i = objects.Count - 1; i >= 0; i--)
            Destroy(objects[i], immediate);
        foreach (Mesh m in meshes) Destroy(m, immediate);
        foreach (Material m in materials) Destroy(m, immediate);
        objects.Clear();
        meshes.Clear();
        materials.Clear();
        Root = null;
        SurfaceMaterial = LineMaterial = null;
        Array.Clear(ChunkObjects, 0, ChunkObjects.Length);
        Array.Clear(ChunkMeshes, 0, ChunkMeshes.Length);
        Array.Clear(LineObjects, 0, LineObjects.Length);
    }

    static void Destroy(UnityEngine.Object o, bool immediate)
    {
        if (o == null) return;
        if (immediate) UnityEngine.Object.DestroyImmediate(o);
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
    /// caller disposes `rt` (WmvTerrainSession does), which releases exactly what was made.
    /// </summary>
    public static void Build(WmvRuntimeTerrainTile rt, TerrainTileMesh mesh, Transform parent, WmvTerrainBuildOptions o,
                             Action<string> log)
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
        bool wire = o.View == WmvTerrainView.Wire || o.View == WmvTerrainView.ShadedWire;
        rt.View = o.View;
        rt.Readable = o.KeepReadable;

        var root = new GameObject(string.Format("Terrain map {0} tile {1}_{2} (load {3})", rt.MapId, rt.TileX, rt.TileY, rt.Load));
        rt.Track(root);
        root.layer = Layer;
        root.SetActive(false);
        if (parent != null)
            root.transform.SetParent(parent, false);
        rt.Root = root;

        var surface = new Material(s) { name = "WmvTerrain surface (" + WmvTerrainDebug.ViewNames[(int)o.View] + ")" };
        rt.Track(surface);
        rt.SurfaceMaterial = surface;
        surface.SetFloat("_WmvTerrainView", o.View == WmvTerrainView.Chunks ? ViewChunks
                                          : o.View == WmvTerrainView.Normals ? ViewNormals : ViewShaded);
        // The wire view's surfaces write depth only: the background shows through them, the edges behind a ridge stay
        // hidden.
        surface.SetFloat("_ColorMask", o.View == WmvTerrainView.Wire ? 0f : 15f);
        Material lines = null;
        if (wire)
        {
            lines = new Material(s) { name = "WmvTerrain edges" };
            rt.Track(lines);
            rt.LineMaterial = lines;
            lines.SetFloat("_WmvTerrainView", ViewLines);
            lines.SetFloat("_WmvLineNudge", LineDepthNudge);
            lines.SetColor("_WmvLineColour", o.View == WmvTerrainView.Wire ? LineOnClear : LineOnShaded);
            // After every opaque surface, so an edge is tested against the finished depth of the tile.
            lines.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Geometry + 1;
        }

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
            Bounds b = BoundsOf(c.BoundsMin, c.BoundsMax);
            m.bounds = b;
            rt.ChunkMeshes[n] = m;
            rt.ChunkBounds[n] = b;
            rt.UploadedBytes += TerrainMeshBuilder.VerticesPerChunk * 24L + c.Indices.Length * 2L;

            go.AddComponent<MeshFilter>().sharedMesh = m;
            MeshRenderer mr = go.AddComponent<MeshRenderer>();
            Quiet(mr);
            mr.sharedMaterial = surface;
            block.SetFloat("_WmvChunkIndex", n);
            mr.SetPropertyBlock(block);
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
        rt.HasBounds = mesh.BoundsMin != null && mesh.BoundsMax != null;
        rt.Bounds = rt.HasBounds ? BoundsOf(mesh.BoundsMin, mesh.BoundsMax) : new Bounds();
        if (log != null)
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "built tile {0}_{1} of map {2} (load {3}) inactive in {4:F1} ms: {5} chunk object(s){6}, {7} mesh(es), {8} material(s), " +
                "{9} game object(s) on layer {10}, {11} bytes uploaded{12}; view {13}{14}{15}",
                rt.TileX, rt.TileY, rt.MapId, rt.Load, clock.Elapsed.TotalMilliseconds, rt.ChunkObjectCount,
                wire ? " and " + rt.LineObjectCount + " edge object(s) (" + edges + " unique edges)" : "", rt.MeshCount,
                rt.MaterialCount, rt.GameObjectCount, Layer, rt.UploadedBytes,
                o.KeepReadable ? " (CPU copies kept for the dump)" : " (CPU copies released)", WmvTerrainDebug.ViewNames[(int)o.View],
                o.OnlyChunk >= 0 ? ", only chunk " + o.OnlyChunk + (rt.ChunkObjects[o.OnlyChunk] != null ? "" : " (it has no triangles: nothing is shown)") : "",
                rt.HasBounds ? ", bounds " + rt.Bounds.min.ToString("F3") + " .. " + rt.Bounds.max.ToString("F3") : ", no triangle"));
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
