// WmvWorldModels.cs
//
// WORLD MODELS IN THE WORLD (stage 06, docs/world-viewer/06-world-models-in-world.md): the Unity side of a world scene's
// world models. WmvTerrainSession decides what is placed and when (the world load); this builds and places it.
//
//   a PROTOTYPE   one per unique WMO FileDataID of a load: WmvWmoBuilder.Build, the Buildings renderer unchanged (its
//                 parser's output, its decode policy, its materials and its shader), built INACTIVE and never shown, kept
//                 under an inactive holder. It leaves WmvRuntimeMapObject.Live at once (LeaveLiveCount): Buildings' own
//                 count stays its own, and this file keeps the world's tallies. Stage 09: its groups' liquid (MLIQ) is added
//                 after the build, on this path only (AddLiquids: a child of the group's object, drawn with the scene's
//                 shared liquid material), so the Buildings viewer's pictures do not change.
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
//                 established here). Counted for runtimeState. A group's liquid (stage 09) is culled the same way, by its
//                 own renderer's box, but it is not a group: the counts (and the placement report's groupsDrawn) leave it
//                 out, so they mean what they meant before liquids.
//   THE OUTDOOR   Stage 10 step 6 (WmvWorldLitMaterials): each LOD0 group's context from its MOGP flags (BuildPrototype,
//   LIGHT         never with WMV_LIGHT=off: an exterior group OUTDOOR, an interior one INTERIOR_INTERIM; Wow/World/ModelLight.cs).
//                 While the outdoor light is shown (RebindWorldLit(true), driven by WmvMain.WorldLightFrame) an OUTDOOR group's
//                 renderer holds the copies of its materials on "WMV/Map Object (world)"; otherwise -- and always for an
//                 interior group, a group's liquid and a material without a variant -- the builder's own. A prototype
//                 records, at its first bind (never in default mode), per renderer of its tree the context and the original
//                 materials; the copies (one per distinct original) are made when first bound and destroyed with it. An
//                 instance placed while bound is bound at once, by renderer index, and counted and timed in
//                 PlacedWhileShown (said by WmvWorldLitDriver); a rebind leaves an instance already as asked alone.
//                 Stage 10 step 7: a copy made gets its fog vector (WmvWorldLitMaterials.SetFogBatch) from its MOMT entry's
//                 F_UNFOGGED (MaterialInfo by the original's MOMT index; FallbackMaterial, which has none, fogged); each slot's
//                 fog code, with its MOMT blend 2 or above flagged, is kept beside its blend class for the binding line.
//                 Stage 10 step 8: a group's liquid is still never rebound, but while bound the liquid of a group that is not
//                 OUTDOOR (ContextOn, the rule LitInterior follows) holds a MaterialPropertyBlock with _WmvLiquidUnfogged 1, so
//                 the water's fog skips it, and its reflections keep Stage 09's sky, as step 7 leaves interior groups as they are
//                 until Stage 12 (INFERRED); cleared at the
//                 unbind (the light off, a fault) and gone with the instance; counted in LiveUnfoggedLiquids and the line.
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
    // Stage 09 (AddLiquids): its groups' liquid (MLIQ) -- one child object per group under the group's object, its mesh
    // appended to Runtime.Meshes (disposed with the prototype, counted in the tallies and GpuGeometryBytes) -- and the
    // registry whose liquid types it holds until it is disposed.
    public int LiquidObjects, LiquidMeshes, LiquidQuads;
    public long LiquidGeometryBytes;
    public WmvLiquidRegistry Liquids;
    public readonly List<int> LiquidTypes = new List<int>();
    /// <summary>Per renderer of its object tree, in GetComponentsInChildren order (an instance's clone lists its renderers in
    /// the same order): true for a liquid's (AddLiquids); null without liquid.</summary>
    public bool[] LiquidRendererMask;
    /// <summary>Stage 10 step 6: each LOD0 group's outdoor-light context from its MOGP flags (BuildPrototype; null with
    /// WMV_LIGHT=off), InteriorInterim for a group whose file was not read.</summary>
    public WorldLightContext[] GroupContexts;
    ModdContexts moddContexts;
    /// <summary>Stage 10 step 6, recorded at the prototype's first bind (WmvWorldModels.Bind), per renderer of its object tree
    /// in GetComponentsInChildren order (an instance's clone lists its renderers in the same order): what it takes
    /// (WmvWorldModels.Lit*), its original materials, and -- made when first bound -- the array holding the world-lit copies
    /// (its originals when no slot has a variant), each slot's blend class (-1 for a slot left on its original) and the first
    /// shader of a slot without a variant (null for none). Null until the first bind.</summary>
    public int[] LitKinds;
    public Material[][] LitOriginals, LitArrays;
    public int[][] LitBlends;
    /// <summary>Stage 10 step 7: each slot's fog code beside its blend class (WmvWorldLitMaterials.FogCode*; -1 for a slot left
    /// on its original).</summary>
    public int[][] LitFogCodes;
    public string[] LitNoVariant;
    /// <summary>Stage 10 step 8, recorded with LitKinds: per renderer, true for a group's liquid whose group is not OUTDOOR (by
    /// WmvWorldLitMaterials.ContextOn: interior-interim, or a group whose file was not read) -- unfogged while bound.</summary>
    public bool[] LitLiquidUnfogged;
    /// <summary>The world-lit copies, one per distinct original (FallbackMaterial included), destroyed with the prototype.</summary>
    public readonly Dictionary<Material, Material> LitCopies = new Dictionary<Material, Material>();

    public WmoRoot Root { get { return Runtime != null ? Runtime.RootData : null; } }
    public WmoGroup[] Groups { get { return Runtime != null ? Runtime.Groups : new WmoGroup[0]; } }

    /// <summary>Stage 10 step 6: its MODD records' outdoor-light contexts (WorldLightContexts.Modd on its root and groups; every
    /// record INTERIOR_INTERIM when a group file was not read), made when first asked.</summary>
    public ModdContexts ModdContexts
    {
        get
        {
            if (moddContexts == null)
                moddContexts = WorldLightContexts.Modd(Root, Groups);
            return moddContexts;
        }
    }

    /// <summary>The world-lit copies made for it (a new list: the self-test's views destroy them at once).</summary>
    public List<Material> WorldLitCopies { get { return new List<Material>(LitCopies.Values); } }
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
    /// <summary>Which of Renderers draw a group's liquid (the prototype's LiquidRendererMask; null: none): culled by their
    /// own boxes like the groups, never counted as groups.</summary>
    public bool[] LiquidRenderers;
    public int GroupRenderers;                         // Renderers that draw a group (not a liquid)
    public bool HasBounds;
    public Bounds Bounds;                              // the union of RendererBounds
    public bool Culled;                                // the whole instance was outside the frustum last frame
    public bool Disposed;
    public bool LitMismatchSaid;                       // Stage 10 step 6: its renderers are not its prototype's (said once)
    public bool LitBound;                              // Stage 10 step 6: last bound to the light (it may hold copies)
    public int UnfoggedLiquids;                        // Stage 10 step 8: its liquid renderers holding the unfogged block now
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
    /// <summary>Protocol 17: the scene's doodads (WmvWorldDoodads.cs) -- one prototype per M2, an instance per doodad, the
    /// textures they share. Never floors, never in Geometries or the scene's box.</summary>
    public readonly Dictionary<uint, WmvDoodadPrototype> DoodadPrototypes = new Dictionary<uint, WmvDoodadPrototype>();
    public readonly List<WmvDoodadInstance> Doodads = new List<WmvDoodadInstance>();
    public WmvDoodadTextures DoodadTextures;
    /// <summary>Streaming (stage 08): world-model and doodad prototypes built for the one tile whose own load could not get
    /// all of them (a group file or texture refused, stale, or past the load's count cap) -- never the session's: not in
    /// Prototypes / DoodadPrototypes (one per FileDataID, the session's), never pooled; each disposed once no job will place
    /// from it and no instance shows it (WmvTerrainSessionStream.cs, DisposeUnusedPrivate), or with the scene
    /// (DisposeWorldScene).</summary>
    public readonly List<WmvWorldPrototype> PrivatePrototypes = new List<WmvWorldPrototype>();
    public readonly List<WmvDoodadPrototype> PrivateDoodadPrototypes = new List<WmvDoodadPrototype>();
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
    /// <summary>Stage 10 step 6: the world-lit copies alive (counted in LiveMaterials too), and those with a non-white tint.</summary>
    public static int LiveCopies, LiveTintedCopies;
    /// <summary>Stage 10 step 8: the liquid renderers of interior groups holding the unfogged block now (_WmvLiquidUnfogged 1: the
    /// water fog and the dome sky skip them while the world-lit binding is on; 0 again after an unbind, a clear or a release).</summary>
    public static int LiveUnfoggedLiquids;
    public static readonly int LiquidUnfoggedId = Shader.PropertyToID("_WmvLiquidUnfogged");
    static MaterialPropertyBlock unfoggedBlock;

    /// <summary>Stage 10 step 8: the block an interior group's liquid renderer holds while bound (made once).</summary>
    public static MaterialPropertyBlock UnfoggedBlock
    {
        get
        {
            if (unfoggedBlock == null)
            {
                unfoggedBlock = new MaterialPropertyBlock();
                unfoggedBlock.SetFloat(LiquidUnfoggedId, 1f);
            }
            return unfoggedBlock;
        }
    }

    /// <summary>A renderer of a prototype's tree (LitKinds): a group's liquid (never rebound), its kind switched off
    /// (-wmvWorldModelLight=off), an interior group's (INTERIOR_INTERIM: today's look), an outdoor group's (bound while lit).</summary>
    public const int LitLiquid = 0, LitKindOff = 1, LitInterior = 2, LitWanted = 3;

    /// <summary>Stage 10 step 6: what the last RebindWorldLit asked for (Place binds a new instance by it), and whether that
    /// rebind finished (WmvWorldLitMaterials.BindFrame rebinds an unsettled manager; so does a placement whose binding
    /// failed).</summary>
    public bool WorldLitBound { get; private set; }
    public bool WorldLitSettled { get; private set; } = true;
    /// <summary>Stage 10 step 6: what Place bound while the light was shown and nobody has said yet (made at the first such
    /// placement, timed; WmvWorldLitMaterials.TakePlaced takes it), or null.</summary>
    public WmvWorldLitBind PlacedWhileShown { get; private set; }
    bool placeFaultSaid;

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
        if (!WmvWorldLight.HardOff)
        {
            // Stage 10 step 6: the groups' outdoor-light contexts, from the group files' MOGP flags.
            p.GroupContexts = new WorldLightContext[rt.Groups.Length];
            for (int gi = 0; gi < rt.Groups.Length; gi++)
                p.GroupContexts[gi] = rt.Groups[gi] != null ? WorldLightContexts.Group(rt.Groups[gi].Header.Flags) : WorldLightContext.InteriorInterim;
        }
        LivePrototypes++;
        foreach (Mesh m in rt.Meshes) if (m != null) LiveMeshes++;
        foreach (Material m in rt.Materials) if (m != null) LiveMaterials++;
        if (rt.FallbackMaterial != null) LiveMaterials++;
        LiveTextures += rt.Textures.Length;
        LiveGpuGeometryBytes += rt.GpuGeometryBytes;
        LiveGpuTextureBytes += rt.GpuTextureBytes;
        return p;
    }

    /// <summary>
    /// Stage 09 (WmvTerrainSessionLiquids.cs): the liquid of a prototype's groups, built after the prototype on the world
    /// path only (the Buildings viewer never gets here) -- `meshes` the liquid meshes made (their objects are already under
    /// the groups' objects, so they are destroyed with the root), `objects` how many, `bytes` their geometry. The meshes join
    /// Runtime.Meshes after the groups' (disposed with the prototype) and the world's tallies; GpuGeometryBytes counts them
    /// (the pools). Instances made from the prototype afterwards clone the objects and share the meshes and materials, and
    /// cull each liquid by its own renderer's bounds; `renderers` (the liquids' own) mark which of an instance's renderers are
    /// not groups (LiquidRendererMask).
    /// </summary>
    public static void AddLiquids(WmvWorldPrototype p, List<Mesh> meshes, int objects, long bytes, List<Renderer> renderers)
    {
        WmvRuntimeMapObject rt = p.Runtime;
        if (rt == null)
            return;
        if (renderers != null && renderers.Count > 0 && rt.Root != null)
        {
            var liquid = new HashSet<Renderer>(renderers);
            Renderer[] all = rt.Root.GetComponentsInChildren<Renderer>(true);
            p.LiquidRendererMask = new bool[all.Length];
            for (int i = 0; i < all.Length; i++)
                p.LiquidRendererMask[i] = liquid.Contains(all[i]);
        }
        if (meshes.Count > 0)
        {
            var all = new Mesh[rt.Meshes.Length + meshes.Count];
            System.Array.Copy(rt.Meshes, all, rt.Meshes.Length);
            for (int i = 0; i < meshes.Count; i++)
                all[rt.Meshes.Length + i] = meshes[i];
            rt.Meshes = all;
        }
        rt.GpuGeometryBytes += bytes;
        LiveMeshes += meshes.Count;
        LiveGpuGeometryBytes += bytes;
        p.LiquidObjects += objects;
        p.LiquidMeshes += meshes.Count;
        p.LiquidGeometryBytes += bytes;
        WmvLiquidMeshes.LiveObjects += objects;
        WmvLiquidMeshes.LiveMeshes += meshes.Count;
        WmvLiquidMeshes.LiveGeometryBytes += bytes;
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
        bool[] liquid = prototype.LiquidRendererMask;
        inst.LiquidRenderers = liquid != null && liquid.Length == inst.Renderers.Length ? liquid : null;
        inst.GroupRenderers = inst.Renderers.Length;
        if (inst.LiquidRenderers != null)
            foreach (bool l in inst.LiquidRenderers)
                if (l) inst.GroupRenderers--;
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
        if (WorldLitBound)
            BindPlaced(inst);             // Stage 10 step 6: placed while the outdoor light is shown
        return inst;
    }

    // ---------------------------------------------------------------- Stage 10 step 6: the outdoor light's binding

    /// <summary>
    /// Every instance's group renderers to their world-lit copies (`on`, where wanted: WmvWorldLitMaterials) or back to their
    /// originals; `bind` counts what was done. An instance already as asked (LitBound) is left alone, unless the last rebind
    /// did not finish: then every instance is set again, whatever it holds. Sets WorldLitBound first and WorldLitSettled only
    /// after the loop, so a throw mid-loop leaves the manager unsettled and the next frame rebinds every renderer. A renderer
    /// destroyed meanwhile is skipped. Nothing with WMV_LIGHT=off.
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
        foreach (WmvWorldInstance inst in instances)
            Bind(inst, on, bind, every);
        WorldLitSettled = true;
    }

    /// <summary>What Place bound while the light was shown since it was last taken (WmvWorldLitMaterials.TakePlaced), or null;
    /// the next such placement starts a new count.</summary>
    public WmvWorldLitBind TakePlacedWhileShown()
    {
        WmvWorldLitBind b = PlacedWhileShown;
        PlacedWhileShown = null;
        return b;
    }

    /// <summary>An instance placed while the light is shown, bound at once, counted and timed in PlacedWhileShown. A binding
    /// that throws leaves the instance on its originals (its placement stands), is said once, and leaves the manager unsettled,
    /// so the next frame rebinds every renderer -- or, if it throws there, the light's fault puts every one on its originals.</summary>
    void BindPlaced(WmvWorldInstance inst)
    {
        if (PlacedWhileShown == null)
            PlacedWhileShown = new WmvWorldLitBind { Lit = true, Placed = true };
        long t0 = System.Diagnostics.Stopwatch.GetTimestamp();
        try { Bind(inst, true, PlacedWhileShown, false); }
        catch (System.Exception e)
        {
            WorldLitSettled = false;
            WmvWorldPrototype p = inst.Prototype;
            for (int i = 0; p != null && i < inst.Renderers.Length; i++)
                if (inst.Renderers[i] != null && p.LitOriginals != null && i < p.LitOriginals.Length && p.LitOriginals[i] != null)
                    inst.Renderers[i].sharedMaterials = p.LitOriginals[i];
            inst.LitBound = false;
            if (!placeFaultSaid)
                Debug.LogError("WMV: [light] binding a world model placed while the outdoor light is shown failed (said once; it keeps " +
                               "its own materials and the next frame rebinds every one): " + e);
            placeFaultSaid = true;
        }
        PlacedWhileShown.AddSince(t0);
    }

    /// <summary>One instance to its copies or its originals, by renderer index (the prototype's record, made now at its first
    /// bind); nothing when it is already as asked, unless `every`. An instance whose renderers are not as many as its
    /// prototype's is left on its originals, and said once.</summary>
    void Bind(WmvWorldInstance inst, bool on, WmvWorldLitBind bind, bool every)
    {
        WmvWorldPrototype p = inst.Prototype;
        if (inst.Disposed || p == null || p.Disposed || (!every && inst.LitBound == on))
            return;
        if (p.LitOriginals == null)
        {
            if (!on)
            {
                inst.LitBound = false;
                return;                   // never bound: its renderers hold what Instantiate gave them, the originals
            }
            Record(p);
        }
        if (on)
            inst.LitBound = true;         // before the loop: a throw in it leaves the instance to the next rebind to the originals
        Renderer[] rs = inst.Renderers;
        if (rs.Length != p.LitOriginals.Length)
        {
            if (on)
            {
                bind.Mismatched++;
                if (!inst.LitMismatchSaid && WmvWorldLight.Log != null)
                    WmvWorldLight.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                    "world light: WMO {0} uniqueId {1}: {2} renderer(s), its prototype {3} -- left on its own materials",
                                                    p.FileDataID, inst.Placement != null ? inst.Placement.UniqueId : 0, rs.Length, p.LitOriginals.Length));
                inst.LitMismatchSaid = true;
            }
            else
                inst.LitBound = false;
            return;
        }
        bool any = false;
        int unfogged = 0;
        for (int i = 0; i < rs.Length; i++)
        {
            Renderer r = rs[i];
            if (r == null)
                continue;
            int kind = p.LitKinds[i];
            if (kind == LitLiquid && p.LitLiquidUnfogged != null && i < p.LitLiquidUnfogged.Length && p.LitLiquidUnfogged[i])
            {
                // Stage 10 step 8: an interior group's liquid stays unfogged while bound (its materials are never rebound).
                r.SetPropertyBlock(on ? UnfoggedBlock : null);
                if (on)
                    unfogged++;
                continue;
            }
            if (kind != LitWanted)
            {
                if (on && kind == LitInterior)
                    bind.OwnMaterials++;
                else if (on && kind == LitKindOff)
                    bind.KindOff++;
                continue;                 // it holds its originals always
            }
            Material[] arr = on ? LitArray(p, i, bind) : p.LitOriginals[i];
            if (on && p.LitNoVariant[i] != null)
                bind.AddNoVariant(p.LitNoVariant[i]);
            if (p.LitArrays[i] == null || p.LitArrays[i] == p.LitOriginals[i])
                continue;                 // never bound, or no slot has a variant: it holds its originals
            r.sharedMaterials = arr;
            if (on)
            {
                bind.GroupRenderers++;
                bind.AddBatches(p.LitBlends[i], p.LitFogCodes[i]);
                any = true;
            }
            else
                bind.Unbound++;
        }
        LiveUnfoggedLiquids += unfogged - inst.UnfoggedLiquids;   // Stage 10 step 8 (0 after an unbind)
        inst.UnfoggedLiquids = unfogged;
        bind.LiquidsUnfogged += unfogged;
        if (!on)
            inst.LitBound = false;        // after the loop: a throw in it leaves the instance to the next rebind
        if (any)
            bind.Placements++;
    }

    /// <summary>The prototype's record at its first bind (after AddLiquids, which adds renderers to its tree): per renderer, its
    /// kind -- a group's liquid, or by the context of the group whose object holds it (a renderer on no group's object keeps
    /// today's look) -- and its original materials (read once: an allocation per prototype, never per frame). Kept only once
    /// complete, so a throw part-way leaves the prototype unrecorded.</summary>
    static void Record(WmvWorldPrototype p)
    {
        WmvRuntimeMapObject rt = p.Runtime;
        Renderer[] all = rt != null && rt.Root != null ? rt.Root.GetComponentsInChildren<Renderer>(true) : new Renderer[0];
        int n = all.Length;
        var kinds = new int[n];
        var originals = new Material[n][];
        var unfogged = new bool[n];
        bool kindOn = WmvWorldLitMaterials.KindOn(false);
        for (int i = 0; i < n; i++)
        {
            originals[i] = all[i].sharedMaterials;
            if (p.LiquidRendererMask != null && i < p.LiquidRendererMask.Length && p.LiquidRendererMask[i])
            {
                kinds[i] = LitLiquid;
                // Stage 10 step 8: the liquid's group (its object's parent, AddLiquids) by the context rule of LitInterior.
                Transform parent = all[i].transform.parent;
                int li = rt != null && parent != null ? System.Array.IndexOf(rt.GroupObjects, parent.gameObject) : -1;
                WorldLightContext lc = li >= 0 && p.GroupContexts != null && li < p.GroupContexts.Length ? p.GroupContexts[li]
                                     : WorldLightContext.InteriorInterim;
                unfogged[i] = !WmvWorldLitMaterials.ContextOn(lc);
                continue;
            }
            WorldLightContext c = WorldLightContext.InteriorInterim;
            int gi = rt != null ? System.Array.IndexOf(rt.GroupObjects, all[i].gameObject) : -1;
            if (gi >= 0 && p.GroupContexts != null && gi < p.GroupContexts.Length)
                c = p.GroupContexts[gi];
            kinds[i] = !kindOn ? LitKindOff : !WmvWorldLitMaterials.ContextOn(c) ? LitInterior : LitWanted;
        }
        p.LitKinds = kinds;
        p.LitArrays = new Material[n][];
        p.LitBlends = new int[n][];
        p.LitFogCodes = new int[n][];
        p.LitNoVariant = new string[n];
        p.LitLiquidUnfogged = unfogged;
        p.LitOriginals = originals;       // last: Bind reads it as "recorded"
    }

    /// <summary>Renderer i's array with the world-lit copies, made the first time: each slot whose original has a variant
    /// holds the prototype's copy of it (made now the first time, counted in the tallies; step 7: its fog vector set from the
    /// original's MOMT entry -- F_UNFOGGED skipped, FallbackMaterial fogged), every other slot its original; the originals
    /// themselves when no slot has a variant.</summary>
    static Material[] LitArray(WmvWorldPrototype p, int i, WmvWorldLitBind bind)
    {
        if (p.LitArrays[i] != null)
            return p.LitArrays[i];
        Material[] originals = p.LitOriginals[i];
        var arr = new Material[originals.Length];
        var blends = new int[originals.Length];
        var fog = new int[originals.Length];
        bool any = false;
        for (int k = 0; k < originals.Length; k++)
        {
            Material m = originals[k];
            arr[k] = m;
            blends[k] = -1;
            fog[k] = -1;
            if (m == null)
                continue;
            Shader variant = WmvWorldLitMaterials.ResolveVariant(false, m, WmvWorldLight.Log);
            if (variant == null)
            {
                if (p.LitNoVariant[i] == null)
                    p.LitNoVariant[i] = m.shader != null ? m.shader.name : "no shader";
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
            fog[k] = FogCode(p, m, c, made);
            any = true;
        }
        p.LitArrays[i] = any ? arr : originals;
        p.LitBlends[i] = blends;
        p.LitFogCodes[i] = fog;
        return p.LitArrays[i];
    }

    /// <summary>Stage 10 step 7: the slot's fog code for the copy `c` of `original` -- its vector set now when the copy was
    /// `made` (WmvWorldLitMaterials.SetFogBatch: unfogged by the MOMT entry's F_UNFOGGED, rt.Materials and rt.MaterialInfo in step
    /// by MOMT index; FallbackMaterial, index -1, fogged), else read back -- with the entry's MOMT blend 2 or above flagged.</summary>
    static int FogCode(WmvWorldPrototype p, Material original, Material c, bool made)
    {
        WmvRuntimeMapObject rt = p.Runtime;
        int id = rt != null ? System.Array.IndexOf(rt.Materials, original) : -1;
        bool known = id >= 0 && id < rt.MaterialInfo.Length;
        bool unfogged = known && (rt.MaterialInfo[id].Flags & WmoMaterialSemantics.FlagUnfogged) != 0;
        int code = made ? WmvWorldLitMaterials.SetFogBatch(c, unfogged) : WmvWorldLitMaterials.FogCodeOf(c);
        if (known && rt.MaterialInfo[id].Blend >= 2)
            code |= WmvWorldLitMaterials.FogCodeMomtBlend2;
        return code;
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
        LiveUnfoggedLiquids -= inst.UnfoggedLiquids;    // Stage 10 step 8: its renderers (and their blocks) go with it
        inst.UnfoggedLiquids = 0;
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
        // Stage 09: its liquid off the liquid tallies (its meshes go with Runtime.Meshes below), its liquid types given back.
        WmvLiquidMeshes.LiveObjects -= p.LiquidObjects;
        WmvLiquidMeshes.LiveMeshes -= p.LiquidMeshes;
        WmvLiquidMeshes.LiveGeometryBytes -= p.LiquidGeometryBytes;
        p.LiquidObjects = p.LiquidMeshes = 0;
        p.LiquidGeometryBytes = 0;
        if (p.Liquids != null)
        {
            p.Liquids.Release(p);
            p.Liquids = null;
        }
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
        p.LitArrays = null;
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
                    bool show = any && GeometryUtility.TestPlanesAABB(planes, inst.RendererBounds[i]);
                    if (r.enabled != show)
                        r.enabled = show;
                    if (inst.LiquidRenderers != null && inst.LiquidRenderers[i])
                        continue;               // a group's liquid: culled by its own box, not a group
                    groups++;
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
