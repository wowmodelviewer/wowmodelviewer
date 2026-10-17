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
//   TEXTURES      one upload per (file, alpha treatment, wrap) per scene, shared by every prototype that samples it, never
//                 kept readable; destroyed with the scene, after the prototypes.
//   DISPOSAL      instances first (their own GameObjects only), then prototypes (mesh, materials, objects), then the scene's
//                 textures. Nothing here outlives the scene that owns it.
//   CULLING       once per frame in the world view: each instance's box (its mesh box placed) against the camera frustum; a
//                 renderer outside is disabled, one inside enabled. Counted for runtimeState.

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
    public double BuildMs;
    public bool Disposed;
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
}

/// <summary>A world scene's doodad textures: one upload per (file, alpha treatment, wrap), shared by its prototypes.</summary>
public sealed class WmvDoodadTextures
{
    readonly Dictionary<long, Texture2D> uploads = new Dictionary<long, Texture2D>();
    readonly Dictionary<long, long> bytes = new Dictionary<long, long>();
    public int Uploads { get { return uploads.Count; } }
    /// <summary>Times a prototype was handed a texture (a use of an upload already made counts too).</summary>
    public int Uses { get; private set; }
    public long GpuBytes { get; private set; }
    public bool Disposed { get; private set; }
    /// <summary>An upload of this file exists (any alpha or wrap variant).</summary>
    public bool HasFile(uint fileDataID)
    {
        foreach (long key in uploads.Keys)
            if ((uint)(key >> 3) == fileDataID)
                return true;
        return false;
    }

    /// <summary>Every upload (a copy of the list; the self-test destroys them at once).</summary>
    public List<Texture2D> All { get { return new List<Texture2D>(uploads.Values); } }

    public Texture2D Get(uint fileDataID, BlpImage image, bool dropAlpha, bool wrapX, bool wrapY)
    {
        if (Disposed || image == null || image.Rgba == null)
            return null;
        long key = ((long)fileDataID << 3) | (dropAlpha ? 4L : 0L) | (wrapX ? 2L : 0L) | (wrapY ? 1L : 0L);
        Uses++;
        Texture2D tex;
        if (uploads.TryGetValue(key, out tex))
            return tex;
        tex = WmvModelBuilder.CreateTexture(image, "M2 texture " + fileDataID + (dropAlpha ? "_opaque" : ""), dropAlpha, true);
        tex.wrapModeU = wrapX ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        tex.wrapModeV = wrapY ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        uploads[key] = tex;
        // RGBA32 with its mip chain: four thirds of the top level.
        long b = (long)image.Width * image.Height * 4L * 4L / 3L;
        bytes[key] = b;
        GpuBytes += b;
        WmvWorldDoodads.LiveTextures++;
        WmvWorldDoodads.LiveGpuTextureBytes += b;
        return tex;
    }

    public void Dispose()
    {
        if (Disposed)
            return;
        Disposed = true;
        foreach (KeyValuePair<long, Texture2D> kv in uploads)
        {
            WmvWorldDoodads.LiveTextures--;
            WmvWorldDoodads.LiveGpuTextureBytes -= bytes[kv.Key];
            if (kv.Value != null)
                Object.Destroy(kv.Value);
        }
        uploads.Clear();
        bytes.Clear();
        GpuBytes = 0;
    }
}

public sealed class WmvWorldDoodads
{
    // The doodads' tallies, over the whole player (the self-test's and runtimeState's): what is alive now.
    public static int LivePrototypes, LiveInstances, LiveMeshes, LiveMaterials, LiveTextures;
    public static long LiveGpuGeometryBytes, LiveGpuTextureBytes;

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
        var options = new WmvBuildOptions
        {
            Static = true,
            ReleaseOnFailure = true,
            TextureProvider = (slot, image, dropAlpha, wrapX, wrapY) =>
            {
                uint id;
                return textures != null && slotFiles != null && slotFiles.TryGetValue(slot, out id) ? textures.Get(id, image, dropAlpha, wrapX, wrapY) : null;
            },
        };
        // The rest state at time 0 for every track, global sequences included: their shared clock (WmvM2Animator.GlobalTimeMs,
        // the Models view's) is held at 0 for the build, so a prototype does not freeze at whatever phase that clock had reached.
        double globalWas = WmvM2Animator.GlobalTimeMs;
        WmvRuntimeModel rt;
        WmvM2Animator.GlobalTimeMs = 0.0;
        try { rt = WmvModelBuilder.Build(model, skin, texturesBySlot, "M2 " + fileDataID + " (doodad prototype)", null, null, null, options); }
        finally { WmvM2Animator.GlobalTimeMs = globalWas; }
        rt.LeaveLiveCount();
        try { return Adopt(fileDataID, rt, clock); }
        catch
        {
            rt.Dispose();             // its root, mesh and materials (the cache's textures stay the cache's)
            throw;
        }
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
        mr.sharedMaterials = prototype.Materials;
        mr.shadowCastingMode = prototype.Shadows;
        mr.receiveShadows = prototype.ReceiveShadows;
        var inst = new WmvDoodadInstance { Prototype = prototype, Doodad = doodad, Load = load, Root = go, Renderer = mr };
        inst.Bounds = PlacedBounds(prototype.LocalBounds, go.transform.localToWorldMatrix);
        prototype.Instances++;
        instances.Add(inst);
        LiveInstances++;
        return inst;
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

    /// <summary>Every instance at once (the scene goes): their own objects, the list cleared in one step.</summary>
    public void DisposeInstances(List<WmvDoodadInstance> list)
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
                Object.Destroy(inst.Root);
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
