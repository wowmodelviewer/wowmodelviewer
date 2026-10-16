// WmvWorldModels.cs
//
// WORLD MODELS IN THE WORLD (stage 06, docs/world-viewer/06-world-models-in-world.md): the Unity side of a world scene's
// world models. WmvTerrainSession decides what is placed and when (the world load); this builds and places it.
//
//   a PROTOTYPE   one per unique WMO FileDataID of a load: WmvWmoBuilder.Build, the Buildings renderer unchanged (its
//                 parser's output, its decode policy, its materials and its shader), built INACTIVE and never shown, kept
//                 under an inactive holder. It leaves WmvRuntimeMapObject.Live at once (LeaveLiveCount): Buildings' own
//                 count stays its own, and this file keeps the world's tallies.
//   an INSTANCE   one per placement: a clone of the prototype's object tree (Object.Instantiate keeps every renderer's
//                 sharedMesh and sharedMaterials), so the meshes, materials and textures are the prototype's, never copied;
//                 on layer 30 (the world view's only layer) with its whole hierarchy; under the scene root, at the
//                 placement's transform relative to the scene's floating origin:
//                   localPosition = WorldToUnity.PositionD(W, origin)                      (double, then the float cast)
//                   localRotation = the rotation U = M R M^T (PlacementTransform.UnityRotation), as LookRotation of its
//                                   z and y columns (U is proper: det +1, so a converted mesh keeps its winding)
//                   localScale    = the record's scale (uniform)
//   DISPOSAL      an instance destroys only its own GameObjects; a prototype (its meshes, materials, textures) is destroyed
//                 after its instances, with the scene. Nothing here outlives the scene that owns it.
//   GROUP CULLING once per frame in the world view: each instance's box (the union of its renderers' boxes) against the
//                 camera frustum, then each group renderer's box; a renderer outside is disabled, one inside enabled. The
//                 boxes are the groups' own drawn-vertex bounds placed (static), so the test is exact for what is drawn and
//                 never hides a wall that is on screen. No portal, BSP or indoor/outdoor rule is applied (none is
//                 established here). Counted for runtimeState.
//
// The prototype's root data (WmoRoot, WmoGroup[]) stay with it: the session's floor and start-point queries read them.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

/// <summary>A world model built once for a world scene: the instances share its meshes, materials and textures.</summary>
public sealed class WmvWorldPrototype
{
    public uint FileDataID;
    public WmvRuntimeMapObject Runtime;   // inactive, never shown
    public int Instances;                 // live instances made from it
    public double BuildMs;
    public bool Disposed;

    public WmoRoot Root { get { return Runtime != null ? Runtime.RootData : null; } }
    public WmoGroup[] Groups { get { return Runtime != null ? Runtime.Groups : new WmoGroup[0]; } }
}

/// <summary>One placement of a prototype in the world scene.</summary>
public sealed class WmvWorldInstance
{
    public WmvWorldPrototype Prototype;
    public WorldModelPlacement Placement;
    public int Load;
    public GameObject Root;
    public Renderer[] Renderers = new Renderer[0];
    public Bounds[] RendererBounds = new Bounds[0];   // Unity world space, as placed (static)
    public bool HasBounds;
    public Bounds Bounds;                              // the union of RendererBounds
    public bool Culled;                                // the whole instance was outside the frustum last frame
    public bool Disposed;
}

/// <summary>What a world scene holds beside its tile: its world models, its origin, and their geometry for the camera.</summary>
public sealed class WmvWorldScene
{
    public int Load, MapId, Epoch;
    public string Kind = "tile";                       // "tile" | "globalWmo"
    public WorldOrigin Origin;
    public readonly Dictionary<uint, WmvWorldPrototype> Prototypes = new Dictionary<uint, WmvWorldPrototype>();
    public readonly List<WmvWorldInstance> Instances = new List<WmvWorldInstance>();
    /// <summary>Each placed world model's geometry (the floor and start-point queries; WorldModelScene.cs).</summary>
    public readonly List<WorldModelGeometry> Geometries = new List<WorldModelGeometry>();
    /// <summary>The world box of what is placed (game frame): a WMO-only scene's targets and its overview.</summary>
    public bool HasBox;
    public WorldPoint BoxMin, BoxMax;

    public void AddBox(WorldPoint min, WorldPoint max)
    {
        if (!HasBox)
        {
            BoxMin = min;
            BoxMax = max;
            HasBox = true;
            return;
        }
        BoxMin = new WorldPoint(System.Math.Min(BoxMin.X, min.X), System.Math.Min(BoxMin.Y, min.Y), System.Math.Min(BoxMin.Z, min.Z));
        BoxMax = new WorldPoint(System.Math.Max(BoxMax.X, max.X), System.Math.Max(BoxMax.Y, max.Y), System.Math.Max(BoxMax.Z, max.Z));
    }

    /// <summary>Is (x, y) inside the scene's box (game frame)?</summary>
    /// <summary>(x, y) inside the scene's box, give or take a yard (the host's target box is the placed MOHD box, which
    /// agrees with the record's extents to a hundredth of a yard on the audited maps).</summary>
    public bool Contains(double x, double y)
    {
        const double slack = 1.0;
        return HasBox && x >= BoxMin.X - slack && x <= BoxMax.X + slack && y >= BoxMin.Y - slack && y <= BoxMax.Y + slack;
    }
}

public sealed class WmvWorldModels
{
    // The world's tallies, over the whole player (the self-test's and runtimeState's): what is alive now.
    public static int LivePrototypes, LiveInstances, LiveMeshes, LiveMaterials, LiveTextures;
    public static long LiveGpuGeometryBytes, LiveGpuTextureBytes;

    // The last frame's culling.
    public int Groups { get; private set; }
    public int GroupsVisible { get; private set; }
    public int GroupsCulled { get; private set; }
    public int InstancesCulled { get; private set; }

    readonly Transform parent;          // the scene root (_WmvTerrain): instances hang under it, at the identity
    GameObject holder;                  // inactive: the prototypes' roots
    readonly List<WmvWorldInstance> instances = new List<WmvWorldInstance>();
    readonly Plane[] planes = new Plane[6];

    public WmvWorldModels(Transform parent)
    {
        this.parent = parent;
    }

    Transform Holder()
    {
        if (holder == null)
        {
            holder = new GameObject("_WmvWorldPrototypes");
            holder.SetActive(false);
            holder.transform.SetParent(parent, false);
            holder.layer = WmvTerrainBuilder.Layer;
        }
        return holder.transform;
    }

    /// <summary>Build a prototype: the Buildings builder unchanged, its root kept inactive under the holder. Throws when
    /// the build throws (the builder has destroyed what it made).</summary>
    public WmvWorldPrototype BuildPrototype(uint fileDataID, WmoRoot root, WmoGroup[] groups, Dictionary<uint, WmvWmoTexture> textures)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WMO " + fileDataID + " (prototype)", null);
        rt.LeaveLiveCount();
        rt.Root.transform.SetParent(Holder(), false);
        SetLayer(rt.Root, WmvTerrainBuilder.Layer);
        var p = new WmvWorldPrototype { FileDataID = fileDataID, Runtime = rt, BuildMs = clock.Elapsed.TotalMilliseconds };
        LivePrototypes++;
        foreach (Mesh m in rt.Meshes) if (m != null) LiveMeshes++;
        foreach (Material m in rt.Materials) if (m != null) LiveMaterials++;
        if (rt.FallbackMaterial != null) LiveMaterials++;
        LiveTextures += rt.Textures.Length;
        LiveGpuGeometryBytes += rt.GpuGeometryBytes;
        LiveGpuTextureBytes += rt.GpuTextureBytes;
        return p;
    }

    /// <summary>Place an instance of `prototype` (active, on layer 30) relative to `origin`.</summary>
    public WmvWorldInstance Place(WmvWorldPrototype prototype, WorldModelPlacement placement, WorldOrigin origin, int load)
    {
        GameObject go = Object.Instantiate(prototype.Runtime.Root, parent, false);
        go.name = string.Format("WMO {0} uniqueId {1} (load {2})", prototype.FileDataID, placement.UniqueId, load);
        SetLayer(go, WmvTerrainBuilder.Layer);
        double ux, uy, uz;
        WorldToUnity.PositionD(placement.Position.X, placement.Position.Y, placement.Position.Z, origin, out ux, out uy, out uz);
        go.transform.localPosition = new Vector3((float)ux, (float)uy, (float)uz);
        go.transform.localRotation = UnityRotation(placement.Rotation);
        go.transform.localScale = Vector3.one * (float)placement.Scale;
        go.SetActive(true);
        var inst = new WmvWorldInstance { Prototype = prototype, Placement = placement, Load = load, Root = go };
        inst.Renderers = go.GetComponentsInChildren<Renderer>(true);
        inst.RendererBounds = new Bounds[inst.Renderers.Length];
        for (int i = 0; i < inst.Renderers.Length; i++)
        {
            Bounds b = inst.Renderers[i].bounds;
            inst.RendererBounds[i] = b;
            if (!inst.HasBounds)
            {
                inst.Bounds = b;
                inst.HasBounds = true;
            }
            else
                inst.Bounds.Encapsulate(b);
        }
        prototype.Instances++;
        instances.Add(inst);
        LiveInstances++;
        return inst;
    }

    /// <summary>U = M R M^T as a Unity rotation: the quaternion whose z and y axes are U's third and second columns.</summary>
    public static Quaternion UnityRotation(double[] worldRotation)
    {
        double[] u = PlacementTransform.UnityRotation(worldRotation);
        var forward = new Vector3((float)u[2], (float)u[5], (float)u[8]);
        var up = new Vector3((float)u[1], (float)u[4], (float)u[7]);
        return Quaternion.LookRotation(forward, up);
    }

    public void DisposeInstance(WmvWorldInstance inst)
    {
        if (inst == null || inst.Disposed)
            return;
        inst.Disposed = true;
        if (inst.Root != null)
            Object.Destroy(inst.Root);
        inst.Root = null;
        inst.Renderers = new Renderer[0];
        if (inst.Prototype != null)
            inst.Prototype.Instances--;
        instances.Remove(inst);
        LiveInstances--;
    }

    public void DisposePrototype(WmvWorldPrototype p)
    {
        if (p == null || p.Disposed)
            return;
        p.Disposed = true;
        WmvRuntimeMapObject rt = p.Runtime;
        if (rt != null)
        {
            foreach (Mesh m in rt.Meshes) if (m != null) LiveMeshes--;
            foreach (Material m in rt.Materials) if (m != null) LiveMaterials--;
            if (rt.FallbackMaterial != null) LiveMaterials--;
            LiveTextures -= rt.Textures.Length;
            LiveGpuGeometryBytes -= rt.GpuGeometryBytes;
            LiveGpuTextureBytes -= rt.GpuTextureBytes;
            rt.Dispose();
        }
        LivePrototypes--;
    }

    /// <summary>The group culling for this frame (see the header); with no camera or no instance, every count is 0.</summary>
    public void Cull(Camera cam)
    {
        int groups = 0, visible = 0, culled = 0, instancesCulled = 0;
        if (cam != null && instances.Count > 0)
        {
            GeometryUtility.CalculateFrustumPlanes(cam, planes);
            foreach (WmvWorldInstance inst in instances)
            {
                if (inst.Disposed || inst.Root == null)
                    continue;
                bool any = inst.HasBounds && GeometryUtility.TestPlanesAABB(planes, inst.Bounds);
                inst.Culled = !any;
                if (!any)
                    instancesCulled++;
                for (int i = 0; i < inst.Renderers.Length; i++)
                {
                    Renderer r = inst.Renderers[i];
                    if (r == null)
                        continue;
                    groups++;
                    bool show = any && GeometryUtility.TestPlanesAABB(planes, inst.RendererBounds[i]);
                    if (r.enabled != show)
                        r.enabled = show;
                    if (show) visible++;
                    else culled++;
                }
            }
        }
        Groups = groups;
        GroupsVisible = visible;
        GroupsCulled = culled;
        InstancesCulled = instancesCulled;
    }

    /// <summary>Every renderer of every instance on again (the world view left: what is shown next is not culled by a
    /// camera that no longer looks).</summary>
    public void Uncull()
    {
        foreach (WmvWorldInstance inst in instances)
            foreach (Renderer r in inst.Renderers)
                if (r != null && !r.enabled)
                    r.enabled = true;
    }

    public int InstanceCount { get { return instances.Count; } }

    /// <summary>The built instance's renderer bounds back in the game's world frame (relative to the scene root at the
    /// identity): Unity (ux, uy, uz) is (-(y - O.Y), z - O.Z, x - O.X), so x = uz + O.X, y = O.Y - ux, z = uy + O.Z.</summary>
    public static double[] RenderedWorldBox(WmvWorldInstance inst, WorldOrigin o)
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
