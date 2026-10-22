// WmvWorldDoodads.cs
//
// DOODADS IN THE WORLD (stage 07, docs/world-viewer/07-doodads.md): the Unity side of a world scene's M2 doodads.
// WmvTerrainSession decides what is placed and when (WmvTerrainSessionDoodads.cs); this builds and places it.
//
//   a PROTOTYPE   one per unique M2 FileDataID of a load: WmvModelBuilder.Build -- the Models renderer's builder, its parser's
//                 output, its materials and its shader -- with WmvBuildOptions.Static (a static mesh in the rest pose the file
//                 stores, no emitters, no animator; the material animator applies the rest state once and is then removed),
//                 its textures from the scene's shared cache (WmvDoodadTextures), built INACTIVE under an inactive holder and
//                 never shown. It leaves WmvRuntimeModel.Live at once (LeaveLiveCount): the Models count stays the Models'.
//                 The mesh's CPU copy is released once it is built (nothing reads it back).
//   an INSTANCE   one per doodad: a bare GameObject with a MeshFilter and a MeshRenderer holding the prototype's sharedMesh
//                 and sharedMaterials (nothing copied, no component of the prototype cloned), on layer 30, under the scene
//                 root, at the doodad's transform relative to the scene's floating origin:
//                   localPosition = WorldToUnity.PositionD(W, origin)          (double, then the float cast)
//                   localRotation = U = M R M^T (WmvWorldModels.UnityRotation)  (R composed in double, DoodadScene.cs)
//                   localScale    = the doodad's scale (uniform)
//   TEXTURES      one upload per (file, alpha treatment, wrap) per scene -- per (file, wrap) for an image of DXT blocks
//                 (stage 08: its alpha needs no opaque variant) -- shared by every prototype that samples it, never kept
//                 readable; counted by reference (a prototype acquires what its build is handed and releases it when it
//                 goes); destroyed with the scene, after the prototypes.
//   DISPOSAL      instances first (their own GameObjects only), then prototypes (mesh, materials, objects), then the scene's
//                 textures. Nothing here outlives the scene that owns it.
//   CULLING       once per frame in the world view: each instance's box (its mesh box placed) against the camera frustum; a
//                 renderer outside is disabled, one inside enabled. Counted for runtimeState.
//   THE OUTDOOR   Stage 10 step 6 (WmvWorldLitMaterials): a doodad's context is its WorldDoodad's (an MDDF doodad OUTDOOR, a
//   LIGHT         world model's from its MODD contexts: WmvTerrainSessionDoodads.cs). While the outdoor light is shown
//                 (RebindWorldLit(true), driven by WmvMain.WorldLightFrame) an OUTDOOR doodad's renderer holds the prototype's
//                 world-lit materials -- copies of its originals on "WMV/Opaque Textured (world)", made when first bound, one
//                 per distinct original, destroyed with the prototype; otherwise, and always for an interior-interim doodad,
//                 the prototype's own. A doodad placed while bound is bound at once, and counted and timed in
//                 PlacedWhileShown (said by WmvWorldLitDriver); a rebind leaves a doodad already as asked alone.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

/// <summary>An M2 built once for a world scene: its instances share its mesh, materials and textures.</summary>
public sealed class WmvDoodadPrototype
{
    public uint FileDataID;
    public WmvRuntimeModel Runtime;       // inactive, never shown, out of WmvRuntimeModel.Live
    public Mesh Mesh;
    public Material[] Materials = new Material[0];
    public Bounds LocalBounds;            // the mesh's box (Unity, model space)
    public UnityEngine.Rendering.ShadowCastingMode Shadows;
    public bool ReceiveShadows;
    public int Instances;                 // live instances made from it
    public int Vertices, Triangles, Submeshes;
    public long GpuGeometryBytes;         // vertices x 40 bytes plus the indices (an estimate of what was uploaded)
    /// <summary>The scene cache's textures this prototype's build was handed (one reference each), released with it.</summary>
    public readonly List<Texture2D> TextureRefs = new List<Texture2D>();
    public double BuildMs;
    public bool Disposed;
    /// <summary>Stage 10 step 6 (WmvWorldDoodads.WorldLitMaterials, made when an instance that wants it is first bound):
    /// Materials with each original that has a world-lit variant replaced by the prototype's copy of it (null kept null), each
    /// slot's blend class (-1 for a slot left on its original), whether any slot holds a copy, and the first shader of a slot
    /// without a variant (null for none). The copies, one per distinct original, are destroyed with the prototype.</summary>
    public Material[] WorldLit;
    public int[] WorldLitBlends;
    public bool WorldLitAny;
    public string WorldLitNoVariant;
    public readonly Dictionary<Material, Material> LitCopies = new Dictionary<Material, Material>();

    /// <summary>The world-lit copies made for it (a new list: the self-test's views destroy them at once).</summary>
    public List<Material> WorldLitCopies { get { return new List<Material>(LitCopies.Values); } }
}

/// <summary>One doodad placed in the world scene.</summary>
public sealed class WmvDoodadInstance
{
    public WmvDoodadPrototype Prototype;
    public WorldDoodad Doodad;
    public int Load;
    public GameObject Root;
    public MeshRenderer Renderer;
    public Bounds Bounds;                 // Unity world space, as placed (static)
    public bool Culled;
    public bool Disposed;
    public bool LitBound;                 // Stage 10 step 6: last bound to the light (it may hold the world-lit materials)
}

/// <summary>A world scene's doodad textures: one upload per (file, alpha treatment, wrap) -- per (file, wrap) for DXT blocks --
/// shared by its prototypes, counted by reference.</summary>
public sealed class WmvDoodadTextures
{
    sealed class Upload
    {
        public Texture2D Texture;
        public long Bytes;
        public int Refs;
        public bool Blocks;
        public long LastUse;                  // streaming: the cache's use counter when last taken or given back
        public long Key;
    }

    readonly Dictionary<long, Upload> uploads = new Dictionary<long, Upload>();
    long use;
    readonly Dictionary<Texture2D, Upload> byTexture = new Dictionary<Texture2D, Upload>();
    readonly Dictionary<uint, int> files = new Dictionary<uint, int>();   // uploads per file
    public int Uploads { get { return uploads.Count; } }
    /// <summary>Times a prototype was handed a texture (a use of an upload already made counts too).</summary>
    public int Uses { get; private set; }
    public long GpuBytes { get; private set; }
    /// <summary>Uploads of an image's own DXT blocks, and of decoded RGBA32 (stage 08), with their bytes.</summary>
    public int BlockUploads { get; private set; }
    public int RgbaUploads { get; private set; }
    public long BlockBytes { get; private set; }
    public long RgbaBytes { get; private set; }
    public bool Disposed { get; private set; }
    /// <summary>An upload of this file exists (any alpha or wrap variant).</summary>
    public bool HasFile(uint fileDataID)
    {
        return files.ContainsKey(fileDataID);
    }

    /// <summary>The references held on every upload now (released ones not counted).</summary>
    public int References
    {
        get
        {
            int n = 0;
            foreach (Upload u in uploads.Values) n += u.Refs;
            return n;
        }
    }

    /// <summary>Every upload (a copy of the list; the self-test destroys them at once).</summary>
    public List<Texture2D> All
    {
        get
        {
            var l = new List<Texture2D>();
            foreach (Upload u in uploads.Values) l.Add(u.Texture);
            return l;
        }
    }

    /// <summary>The upload for this file, alpha treatment and wrap -- made the first time -- with one reference taken
    /// (Release gives it back).</summary>
    public Texture2D Get(uint fileDataID, BlpImage image, bool dropAlpha, bool wrapX, bool wrapY)
    {
        if (Disposed || image == null || (image.Rgba == null && image.Blocks == null))
            return null;
        bool blocks = image.Blocks != null;
        long key = ((long)fileDataID << 3) | (dropAlpha && !blocks ? 4L : 0L) | (wrapX ? 2L : 0L) | (wrapY ? 1L : 0L);
        Uses++;
        Upload u;
        if (uploads.TryGetValue(key, out u))
        {
            u.Refs++;
            u.LastUse = ++use;
            return u.Texture;
        }
        string name = "M2 texture " + fileDataID + (dropAlpha && !blocks ? "_opaque" : "") + (blocks ? "_" + image.Encoding : "");
        Texture2D tex = blocks ? WmvModelBuilder.CreateBlockTexture(image.Blocks, name) : WmvModelBuilder.CreateTexture(image, name, dropAlpha, true);
        tex.wrapModeU = wrapX ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        tex.wrapModeV = wrapY ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        // Exact: the stored levels of a block upload; RGBA32 with its full mip chain otherwise.
        long b = BlpBlocks.UploadBytes(image);
        u = new Upload { Texture = tex, Bytes = b, Refs = 1, Blocks = blocks, LastUse = ++use, Key = key };
        uploads[key] = u;
        byTexture[tex] = u;
        int n;
        files[fileDataID] = files.TryGetValue(fileDataID, out n) ? n + 1 : 1;
        GpuBytes += b;
        if (blocks)
        {
            BlockUploads++;
            BlockBytes += b;
        }
        else
        {
            RgbaUploads++;
            RgbaBytes += b;
        }
        WmvWorldDoodads.LiveTextures++;
        WmvWorldDoodads.LiveGpuTextureBytes += b;
        return tex;
    }

    /// <summary>A reference taken by Get given back. The upload stays until the scene's textures go.</summary>
    public void Release(Texture2D texture)
    {
        Upload u;
        if (texture != null && byTexture.TryGetValue(texture, out u) && u.Refs > 0)
        {
            u.Refs--;
            u.LastUse = ++use;
        }
    }

    /// <summary>Streaming (stage 08): the uploads no prototype holds now -- the pool -- and their bytes.</summary>
    public long PooledBytes
    {
        get
        {
            long n = 0;
            foreach (Upload u in uploads.Values) if (u.Refs <= 0) n += u.Bytes;
            return n;
        }
    }

    /// <summary>Streaming: the references held on this texture's upload (0: none, or not this cache's).</summary>
    public int RefsOf(Texture2D texture)
    {
        Upload u;
        return texture != null && byTexture.TryGetValue(texture, out u) ? u.Refs : 0;
    }

    /// <summary>Streaming: the bytes of this texture's upload (0: not this cache's).</summary>
    public long BytesOf(Texture2D texture)
    {
        Upload u;
        return texture != null && byTexture.TryGetValue(texture, out u) ? u.Bytes : 0;
    }

    /// <summary>Streaming: of these textures, the uploads no prototype holds any more go now (an evicted prototype's);
    /// returns their bytes, `count` how many. `destroy` destroys a texture (Object.Destroy when null).</summary>
    public long EvictUnreferenced(List<Texture2D> textures, out int count, System.Action<Object> destroy = null)
    {
        count = 0;
        long freed = 0;
        foreach (Texture2D t in textures)
        {
            Upload u;
            if (t == null || !byTexture.TryGetValue(t, out u) || u.Refs > 0)
                continue;
            Remove(u.Key, u, destroy);
            freed += u.Bytes;
            count++;
        }
        return freed;
    }

    public int PooledCount
    {
        get
        {
            int n = 0;
            foreach (Upload u in uploads.Values) if (u.Refs <= 0) n++;
            return n;
        }
    }

    /// <summary>Streaming: the pooled uploads go, least recently used first, until those left hold at most `keepBytes`;
    /// returns how many went. `destroy` destroys a texture (Object.Destroy when null).</summary>
    public int EvictPooled(long keepBytes, System.Action<Object> destroy = null)
    {
        var pooled = new List<KeyValuePair<long, Upload>>();
        long held = 0;
        foreach (KeyValuePair<long, Upload> kv in uploads)
            if (kv.Value.Refs <= 0)
            {
                pooled.Add(kv);
                held += kv.Value.Bytes;
            }
        pooled.Sort((a, b) => a.Value.LastUse.CompareTo(b.Value.LastUse));
        int n = 0;
        foreach (KeyValuePair<long, Upload> kv in pooled)
        {
            if (held <= keepBytes)
                break;
            Remove(kv.Key, kv.Value, destroy);
            held -= kv.Value.Bytes;
            n++;
        }
        return n;
    }

    /// <summary>An upload off every list and tally, its texture destroyed.</summary>
    void Remove(long key, Upload u, System.Action<Object> destroy)
    {
        uploads.Remove(key);
        if (u.Texture != null)
            byTexture.Remove(u.Texture);
        uint file = (uint)(key >> 3);
        int c;
        if (files.TryGetValue(file, out c))
        {
            if (c <= 1) files.Remove(file);
            else files[file] = c - 1;
        }
        GpuBytes -= u.Bytes;
        if (u.Blocks)
        {
            BlockUploads--;
            BlockBytes -= u.Bytes;
        }
        else
        {
            RgbaUploads--;
            RgbaBytes -= u.Bytes;
        }
        WmvWorldDoodads.LiveTextures--;
        WmvWorldDoodads.LiveGpuTextureBytes -= u.Bytes;
        if (u.Texture != null)
        {
            if (destroy != null) destroy(u.Texture);
            else Object.Destroy(u.Texture);
        }
    }

    public void Dispose()
    {
        if (Disposed)
            return;
        Disposed = true;
        foreach (Upload u in uploads.Values)
        {
            WmvWorldDoodads.LiveTextures--;
            WmvWorldDoodads.LiveGpuTextureBytes -= u.Bytes;
            if (u.Texture != null)
                Object.Destroy(u.Texture);
        }
        uploads.Clear();
        byTexture.Clear();
        files.Clear();
        GpuBytes = 0;
    }
}

public sealed class WmvWorldDoodads
{
    // The doodads' tallies, over the whole player (the self-test's and runtimeState's): what is alive now.
    public static int LivePrototypes, LiveInstances, LiveMeshes, LiveMaterials, LiveTextures;
    public static long LiveGpuGeometryBytes, LiveGpuTextureBytes;
    /// <summary>Stage 10 step 6: the world-lit copies alive (counted in LiveMaterials too), and those with a non-white tint.</summary>
    public static int LiveCopies, LiveTintedCopies;

    /// <summary>Stage 10 step 6: what the last RebindWorldLit asked for (PlaceOn binds a new doodad by it), and whether that
    /// rebind finished (WmvWorldLitMaterials.BindFrame rebinds an unsettled manager; so does a placement whose binding
    /// failed).</summary>
    public bool WorldLitBound { get; private set; }
    public bool WorldLitSettled { get; private set; } = true;
    /// <summary>Stage 10 step 6: what PlaceOn bound while the light was shown and nobody has said yet (made at the first such
    /// placement, timed; WmvWorldLitMaterials.TakePlaced takes it), or null.</summary>
    public WmvWorldLitBind PlacedWhileShown { get; private set; }
    bool placeFaultSaid;

    // The last frame's culling.
    public int Visible { get; private set; }
    public int Culled { get; private set; }

    readonly Transform parent;          // the scene root (_WmvTerrain): instances hang under it, at the identity
    GameObject holder;                  // inactive: the prototypes' roots
    readonly List<WmvDoodadInstance> instances = new List<WmvDoodadInstance>();
    readonly Plane[] planes = new Plane[6];

    public WmvWorldDoodads(Transform parent)
    {
        this.parent = parent;
    }

    Transform Holder()
    {
        if (holder == null)
        {
            holder = new GameObject("_WmvDoodadPrototypes");
            holder.SetActive(false);
            holder.transform.SetParent(parent, false);
            holder.layer = WmvTerrainBuilder.Layer;
        }
        return holder.transform;
    }

    public int InstanceCount { get { return instances.Count; } }

    /// <summary>
    /// Build a prototype: WmvModelBuilder.Build with WmvBuildOptions.Static, its textures from `textures` (texturesBySlot: the
    /// decoded image of each slot that has one; slotFiles: each slot's FileDataID, the cache's key). Throws when the build
    /// throws (the builder has destroyed what it made, ReleaseOnFailure).
    /// </summary>
    public WmvDoodadPrototype BuildPrototype(uint fileDataID, M2ParsedModel model, M2ParsedSkin skin, Dictionary<int, BlpImage> texturesBySlot,
                                             Dictionary<int, uint> slotFiles, WmvDoodadTextures textures)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        var acquired = new List<Texture2D>();   // the cache's references this build takes: the prototype's, or given back
        var options = new WmvBuildOptions
        {
            Static = true,
            ReleaseOnFailure = true,
            TextureProvider = (slot, image, dropAlpha, wrapX, wrapY) =>
            {
                uint id;
                Texture2D t = textures != null && slotFiles != null && slotFiles.TryGetValue(slot, out id)
                                  ? textures.Get(id, image, dropAlpha, wrapX, wrapY) : null;
                if (t != null)
                    acquired.Add(t);
                return t;
            },
        };
        // The rest state at time 0 for every track, global sequences included: their shared clock (WmvM2Animator.GlobalTimeMs,
        // the Models view's) is held at 0 for the build, so a prototype does not freeze at whatever phase that clock had reached.
        double globalWas = WmvM2Animator.GlobalTimeMs;
        WmvRuntimeModel rt;
        WmvM2Animator.GlobalTimeMs = 0.0;
        try { rt = WmvModelBuilder.Build(model, skin, texturesBySlot, "M2 " + fileDataID + " (doodad prototype)", null, null, null, options); }
        catch
        {
            foreach (Texture2D t in acquired) textures.Release(t);
            throw;
        }
        finally { WmvM2Animator.GlobalTimeMs = globalWas; }
        rt.LeaveLiveCount();
        WmvDoodadPrototype p;
        try { p = Adopt(fileDataID, rt, clock); }
        catch
        {
            rt.Dispose();             // its root, mesh and materials (the cache's textures stay the cache's)
            foreach (Texture2D t in acquired) textures.Release(t);
            throw;
        }
        p.TextureRefs.AddRange(acquired);
        return p;
    }

    WmvDoodadPrototype Adopt(uint fileDataID, WmvRuntimeModel rt, System.Diagnostics.Stopwatch clock)
    {
        // The rest state is applied (WmvMaterialAnimator.Setup ran Apply(0)); nothing animates a prototype that is never shown,
        // and its instances carry no component of it: the animator goes, and with it the parsed model it held.
        if (rt.MaterialAnimator != null)
        {
            Object.Destroy(rt.MaterialAnimator);
            rt.MaterialAnimator = null;
        }
        long indices = 0;
        foreach (int[] t in rt.SubmeshTriangles)
            if (t != null) indices += t.Length;
        rt.SubmeshTriangles = new int[0][];
        rt.TextureUploads = new Dictionary<int, WmvTextureUpload>();   // the decoded images it was built from: not kept
        rt.Root.transform.SetParent(Holder(), false);
        SetLayer(rt.Root, WmvTerrainBuilder.Layer);
        MeshRenderer mr = rt.Root.GetComponent<MeshRenderer>();
        var p = new WmvDoodadPrototype
        {
            FileDataID = fileDataID, Runtime = rt, Mesh = rt.Mesh, Materials = rt.Materials, LocalBounds = rt.Mesh != null ? rt.Mesh.bounds : new Bounds(),
            Shadows = mr != null ? mr.shadowCastingMode : UnityEngine.Rendering.ShadowCastingMode.On, ReceiveShadows = mr == null || mr.receiveShadows,
            Vertices = rt.VertexCount, Triangles = rt.TriangleCount, Submeshes = rt.SubmeshCount,
        };
        if (rt.Mesh != null)
        {
            p.GpuGeometryBytes = (long)rt.VertexCount * 40L + indices * (rt.Mesh.indexFormat == UnityEngine.Rendering.IndexFormat.UInt32 ? 4L : 2L);
            rt.Mesh.UploadMeshData(true);
        }
        p.BuildMs = clock.Elapsed.TotalMilliseconds;
        LivePrototypes++;
        if (rt.Mesh != null) LiveMeshes++;
        foreach (Material m in rt.Materials) if (m != null) LiveMaterials++;
        LiveGpuGeometryBytes += p.GpuGeometryBytes;
        return p;
    }

    /// <summary>Place an instance of `prototype` (active, on layer 30) at `doodad`, relative to `origin`.</summary>
    public WmvDoodadInstance Place(WmvDoodadPrototype prototype, WorldDoodad doodad, WorldOrigin origin, int load)
    {
        var go = new GameObject(doodad.Source == DoodadSource.Mddf
                                    ? "M2 " + prototype.FileDataID + " uniqueId " + doodad.UniqueId + " (load " + load + ")"
                                    : "M2 " + prototype.FileDataID + " of WMO " + doodad.WmoFileDataID + " MODD " + doodad.ModdIndex + " (load " + load + ")");
        try { return PlaceOn(go, prototype, doodad, origin, load); }
        catch
        {
            Object.Destroy(go);       // nothing of a placement that threw is left under the scene root
            throw;
        }
    }

    WmvDoodadInstance PlaceOn(GameObject go, WmvDoodadPrototype prototype, WorldDoodad doodad, WorldOrigin origin, int load)
    {
        go.layer = WmvTerrainBuilder.Layer;
        go.transform.SetParent(parent, false);
        double ux, uy, uz;
        WorldToUnity.PositionD(doodad.Position.X, doodad.Position.Y, doodad.Position.Z, origin, out ux, out uy, out uz);
        go.transform.localPosition = new Vector3((float)ux, (float)uy, (float)uz);
        go.transform.localRotation = WmvWorldModels.UnityRotation(doodad.Rotation);
        go.transform.localScale = Vector3.one * (float)doodad.Scale;
        go.AddComponent<MeshFilter>().sharedMesh = prototype.Mesh;
        MeshRenderer mr = go.AddComponent<MeshRenderer>();
        bool lit = false;
        mr.sharedMaterials = WorldLitBound ? PlacedMaterials(prototype, doodad, out lit) : prototype.Materials;
        mr.shadowCastingMode = prototype.Shadows;
        mr.receiveShadows = prototype.ReceiveShadows;
        var inst = new WmvDoodadInstance { Prototype = prototype, Doodad = doodad, Load = load, Root = go, Renderer = mr, LitBound = lit };
        inst.Bounds = PlacedBounds(prototype.LocalBounds, go.transform.localToWorldMatrix);
        prototype.Instances++;
        instances.Add(inst);
        LiveInstances++;
        return inst;
    }

    // ---------------------------------------------------------------- Stage 10 step 6: the outdoor light's binding

    /// <summary>
    /// Every doodad's renderer to its prototype's world-lit materials (`on`, where wanted: WmvWorldLitMaterials) or back to
    /// the prototype's own; `bind` counts what was done. A doodad already as asked (LitBound) is left alone, unless the last
    /// rebind did not finish: then every doodad is set again, whatever it holds. Sets WorldLitBound first and WorldLitSettled
    /// only after the loop, so a throw mid-loop leaves the manager unsettled and the next frame rebinds every doodad. A doodad
    /// whose renderer is gone is skipped. Nothing with WMV_LIGHT=off.
    /// </summary>
    public void RebindWorldLit(bool on, WmvWorldLitBind bind)
    {
        if (WmvWorldLight.HardOff)
            return;
        bool every = !WorldLitSettled;
        WorldLitBound = on;
        WorldLitSettled = false;
        if (bind == null)
            bind = new WmvWorldLitBind { Lit = on };
        foreach (WmvDoodadInstance inst in instances)
        {
            MeshRenderer r = inst.Renderer;
            WmvDoodadPrototype p = inst.Prototype;
            if (inst.Disposed || r == null || p == null || p.Disposed || (!every && inst.LitBound == on))
                continue;
            if (on)
            {
                inst.LitBound = true;     // before the set: a throw leaves the doodad to the next rebind to the originals
                Material[] m = BoundMaterials(p, inst.Doodad, bind);
                if (m != p.Materials)
                    r.sharedMaterials = m;
            }
            else
            {
                if (p.WorldLitAny && Wants(inst.Doodad))
                {
                    r.sharedMaterials = p.Materials;
                    bind.Unbound++;
                }
                inst.LitBound = false;
            }
        }
        WorldLitSettled = true;
    }

    /// <summary>What PlaceOn bound while the light was shown since it was last taken (WmvWorldLitMaterials.TakePlaced), or
    /// null; the next such placement starts a new count.</summary>
    public WmvWorldLitBind TakePlacedWhileShown()
    {
        WmvWorldLitBind b = PlacedWhileShown;
        PlacedWhileShown = null;
        return b;
    }

    /// <summary>What a doodad placed while the light is shown holds (BoundMaterials), counted and timed in PlacedWhileShown;
    /// `bound` false when its binding threw: it then keeps the prototype's own materials (its placement stands), the fault
    /// is said once, and the manager is left unsettled so the next frame rebinds every doodad -- or, if it throws there, the
    /// light's fault puts every one on its own.</summary>
    Material[] PlacedMaterials(WmvDoodadPrototype prototype, WorldDoodad doodad, out bool bound)
    {
        if (PlacedWhileShown == null)
            PlacedWhileShown = new WmvWorldLitBind { Lit = true, Placed = true };
        long t0 = System.Diagnostics.Stopwatch.GetTimestamp();
        Material[] m;
        try
        {
            m = BoundMaterials(prototype, doodad, PlacedWhileShown);
            bound = true;
        }
        catch (System.Exception e)
        {
            WorldLitSettled = false;
            m = prototype.Materials;
            bound = false;
            if (!placeFaultSaid)
                Debug.LogError("WMV: [light] binding a doodad placed while the outdoor light is shown failed (said once; it keeps its own " +
                               "materials and the next frame rebinds every one): " + e);
            placeFaultSaid = true;
        }
        PlacedWhileShown.AddSince(t0);
        return m;
    }

    /// <summary>The kind's switch and the doodad's context (not the shaders: BoundMaterials keeps a slot without a variant on
    /// its original).</summary>
    static bool Wants(WorldDoodad doodad)
    {
        return WmvWorldLitMaterials.KindOn(true) && WmvWorldLitMaterials.ContextOn(doodad != null ? doodad.Context : WorldLightContext.InteriorInterim);
    }

    /// <summary>What a doodad holds while the outdoor light is shown: the prototype's world-lit materials when it wants them
    /// and some slot has a variant, the prototype's own otherwise; counted in `bind` (when given).</summary>
    Material[] BoundMaterials(WmvDoodadPrototype p, WorldDoodad doodad, WmvWorldLitBind bind)
    {
        if (!WmvWorldLitMaterials.KindOn(true))
        {
            if (bind != null)
                bind.KindOff++;
            return p.Materials;
        }
        if (!WmvWorldLitMaterials.ContextOn(doodad != null ? doodad.Context : WorldLightContext.InteriorInterim))
        {
            if (bind != null)
                bind.OwnMaterials++;
            return p.Materials;
        }
        Material[] lit = WorldLitMaterials(p, bind);
        if (bind != null)
        {
            if (p.WorldLitNoVariant != null)
                bind.AddNoVariant(p.WorldLitNoVariant);
            if (p.WorldLitAny)
            {
                bind.Doodads++;
                bind.AddBatches(p.WorldLitBlends);
            }
        }
        return p.WorldLitAny ? lit : p.Materials;
    }

    /// <summary>The prototype's world-lit materials (WmvDoodadPrototype.WorldLit), made the first time: each slot whose
    /// original has a variant holds the prototype's copy of it (made now the first time, counted in the tallies), every other
    /// slot its original, null kept null.</summary>
    public Material[] WorldLitMaterials(WmvDoodadPrototype p, WmvWorldLitBind bind)
    {
        if (p.WorldLit != null)
            return p.WorldLit;
        Material[] originals = p.Materials;
        var arr = new Material[originals.Length];
        var blends = new int[originals.Length];
        for (int k = 0; k < originals.Length; k++)
        {
            Material m = originals[k];
            arr[k] = m;
            blends[k] = -1;
            if (m == null)
                continue;
            Shader variant = WmvWorldLitMaterials.ResolveVariant(true, m, WmvWorldLight.Log);
            if (variant == null)
            {
                if (p.WorldLitNoVariant == null)
                    p.WorldLitNoVariant = m.shader != null ? m.shader.name : "no shader";
                continue;
            }
            bool made;
            Material c = WmvWorldLitMaterials.CopyOf(p.LitCopies, m, variant, bind, out made);
            if (made)
            {
                LiveMaterials++;
                LiveCopies++;
                if (WmvWorldLitMaterials.Tinted(c))
                    LiveTintedCopies++;
            }
            arr[k] = c;
            blends[k] = WmvWorldLitMaterials.BlendOf(c);
            p.WorldLitAny = true;
        }
        p.WorldLitBlends = blends;
        p.WorldLit = arr;
        return arr;
    }

    /// <summary>A model-space box placed by a matrix (all eight corners): the instance's box for culling, valid whether or
    /// not its objects are active.</summary>
    static Bounds PlacedBounds(Bounds local, Matrix4x4 m)
    {
        Vector3 mn = local.min, mx = local.max;
        var b = new Bounds(m.MultiplyPoint3x4(mn), Vector3.zero);
        for (int k = 1; k < 8; k++)
            b.Encapsulate(m.MultiplyPoint3x4(new Vector3((k & 1) == 0 ? mn.x : mx.x, (k & 2) == 0 ? mn.y : mx.y, (k & 4) == 0 ? mn.z : mx.z)));
        return b;
    }

    public void DisposeInstance(WmvDoodadInstance inst)
    {
        if (inst == null || inst.Disposed)
            return;
        inst.Disposed = true;
        if (inst.Root != null)
            Object.Destroy(inst.Root);
        inst.Root = null;
        inst.Renderer = null;
        if (inst.Prototype != null)
            inst.Prototype.Instances--;
        instances.Remove(inst);
        LiveInstances--;
        if (instances.Count == 0)
            Visible = Culled = 0;
    }

    /// <summary>Every instance at once (the scene goes): their own objects, the list cleared in one step. `later` (streaming: a
    /// closed tile's doodads) takes their objects to destroy over the next frames instead of now.</summary>
    public void DisposeInstances(List<WmvDoodadInstance> list, List<Object> later = null)
    {
        if (list == null || list.Count == 0)
            return;
        var gone = new HashSet<WmvDoodadInstance>();
        foreach (WmvDoodadInstance inst in list)
        {
            if (inst == null || inst.Disposed)
                continue;
            inst.Disposed = true;
            if (inst.Root != null)
            {
                if (later != null)
                {
                    // Off the culling list from now: hidden at once (its one renderer off -- cheaper than deactivating
                    // thousands of roots in one frame), destroyed over the next frames.
                    if (inst.Renderer != null)
                        inst.Renderer.enabled = false;
                    later.Add(inst.Root);
                }
                else Object.Destroy(inst.Root);
            }
            inst.Root = null;
            inst.Renderer = null;
            if (inst.Prototype != null)
                inst.Prototype.Instances--;
            gone.Add(inst);
            LiveInstances--;
        }
        instances.RemoveAll(i => gone.Contains(i));
        if (instances.Count == 0)
            Visible = Culled = 0;
    }

    /// <summary>A prototype goes, its references on `textures` given back (the uploads stay the cache's).</summary>
    public void DisposePrototype(WmvDoodadPrototype p, WmvDoodadTextures textures)
    {
        if (p != null && !p.Disposed && textures != null)
            foreach (Texture2D t in p.TextureRefs)
                textures.Release(t);
        if (p != null)
            p.TextureRefs.Clear();
        DisposePrototype(p);
    }

    public void DisposePrototype(WmvDoodadPrototype p)
    {
        if (p == null || p.Disposed)
            return;
        p.Disposed = true;
        WmvRuntimeModel rt = p.Runtime;
        if (rt != null)
        {
            if (rt.Mesh != null) LiveMeshes--;
            foreach (Material m in rt.Materials) if (m != null) LiveMaterials--;
            rt.Dispose();                 // its root, mesh and materials; its Textures are none (the scene's cache owns them)
        }
        // Stage 10 step 6: its world-lit copies (the self-test's views destroy them at once as well).
        foreach (Material c in p.LitCopies.Values)
        {
            if (c != null)
            {
                if (WmvWorldLitMaterials.Tinted(c))
                    LiveTintedCopies--;
                Object.Destroy(c);
            }
            LiveMaterials--;
            LiveCopies--;
        }
        p.LitCopies.Clear();
        p.WorldLit = null;
        LiveGpuGeometryBytes -= p.GpuGeometryBytes;
        LivePrototypes--;
        p.Runtime = null;
        p.Mesh = null;
        p.Materials = new Material[0];
    }

    /// <summary>The culling for this frame (see the header); with no camera or no instance, every count is 0.</summary>
    public void Cull(Camera cam)
    {
        int visible = 0, culled = 0;
        if (cam != null && instances.Count > 0)
        {
            GeometryUtility.CalculateFrustumPlanes(cam, planes);
            foreach (WmvDoodadInstance inst in instances)
            {
                if (inst.Disposed || inst.Renderer == null)
                    continue;
                bool show = GeometryUtility.TestPlanesAABB(planes, inst.Bounds);
                inst.Culled = !show;
                if (inst.Renderer.enabled != show)
                    inst.Renderer.enabled = show;
                if (show) visible++;
                else culled++;
            }
        }
        Visible = visible;
        Culled = culled;
    }

    /// <summary>Every instance's renderer on again (the world view left).</summary>
    public void Uncull()
    {
        foreach (WmvDoodadInstance inst in instances)
            if (inst.Renderer != null && !inst.Renderer.enabled)
                inst.Renderer.enabled = true;
    }

    /// <summary>The instance's box back in the game's world frame (see WmvWorldModels.RenderedWorldBox).</summary>
    public static double[] RenderedWorldBox(WmvDoodadInstance inst, WorldOrigin o)
    {
        Vector3 mn = inst.Bounds.min, mx = inst.Bounds.max;
        return new[] { mn.z + o.X, o.Y - mx.x, mn.y + o.Z, mx.z + o.X, o.Y - mn.x, mx.y + o.Z };
    }

    static void SetLayer(GameObject go, int layer)
    {
        go.layer = layer;
        foreach (Transform t in go.GetComponentsInChildren<Transform>(true))
            t.gameObject.layer = layer;
    }
}
