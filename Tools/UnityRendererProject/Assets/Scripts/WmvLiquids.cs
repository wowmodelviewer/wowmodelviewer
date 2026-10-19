// WmvLiquids.cs
//
// LIQUIDS (stage 09, docs/world-viewer/09-liquids.md 1.5-1.8): the Unity side of a scene's liquids. The pure code makes
// the meshes (Wow/World/TerrainLiquidMesh.cs for a tile's MH2O, Wow/WmoLiquid.cs for a world model's MLIQ) and the host
// sends the catalogue of the client's liquid types (WmvIpcClient.LiquidOffer); WmvTerrainSession decides what is built
// and fetched (WmvTerrainSessionLiquids.cs). This file holds what both meet in:
//
//   WmvLiquidRegistry   the scene's liquid types, keyed by LiquidType id: per type one material (Resources/WmvLiquid.shader)
//                       and, per distinct frame list, one Texture2DArray of texture slot 0's frames shared by every type
//                       that lists the same frames (WmvLiquidFrames). Each type is HELD by what draws or fetches it -- a
//                       terrain tile, a world-model prototype, a load's job (object identity) -- and given back when that
//                       goes. A single load's registry disposes a type the moment nothing holds it; a streaming session's
//                       (Pooled) keeps it, pooled, until the session's pools evict it, least recently used first.
//   frames              fetched by FileDataID (role "liquidTexture"), prepared off the main thread as the terrain's textures
//                       are (WmvTerrainTexture.Prepare: native DXT blocks as stored when the GPU samples them, else RGBA32),
//                       and uploaded into the type's array ONCE EVERY FRAME OF THE LIST HAS SETTLED -- the array's format is
//                       chosen from all of them: every frame the same size, mip count and format, that format; the same
//                       size otherwise, every frame as RGBA32 with the levels they all have; another size, not drawn
//                       animated. Until then, and for good when a frame cannot be had, the type is drawn in its base colour
//                       alone (09-liquids.md 1.5).
//   the clock           Tick sets each animated type's frame once a frame on its shared material: frame = floor(t x
//                       FramesPerSecond) mod frames (WmvLiquidMaterials.FramesPerSecond, a documented choice).
//   WmvLiquidMeshes     a LiquidSurface as a Unity Mesh, a tile's liquid objects (WmvTerrainBuilder.Build), and the tallies
//                       of every liquid object and mesh alive (runtimeState's liquidObjects, liquidMeshes).
//
// Every material and array is destroyed through the registry's own hook (Object.Destroy in the player, DestroyImmediate in
// the self-test, which counts objects within a frame) and counted in the registry tallies the moment it exists.

using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using Wmv.Wow.World;

/// <summary>
/// The liquid material (Resources/WmvLiquid.shader, "Wmv/Liquid") and the approximation's constants (09-liquids.md 1.5:
/// every one a documented choice, none the game's; the colours and opacities to be tuned against the in-game references).
/// </summary>
public static class WmvLiquidMaterials
{
    public const string ShaderResource = "WmvLiquid";
    public const string ShaderName = "Wmv/Liquid";

    /// <summary>APPROXIMATION: water and ocean colour = base colour + the frame's alpha x HighlightScale (the frames carry
    /// their ripples and sparkles in alpha, near-black colour: 09-liquids.md 1.5). Provisional until the references.</summary>
    public const float HighlightScale = 0.5f;
    /// <summary>APPROXIMATION: water and ocean opacity at depth byte 0 (and at a vertex with no stored depth: DeepAlpha). The
    /// depth byte's scale is UNKNOWN; opacity = lerp(ShallowAlpha, DeepAlpha, depth / 255). Provisional until the references.</summary>
    public const float ShallowAlpha = 0.45f;
    /// <summary>APPROXIMATION: water and ocean opacity at depth byte 255, and where no depth is stored (ocean data-less
    /// instances). Provisional until the references.</summary>
    public const float DeepAlpha = 0.8f;
    /// <summary>APPROXIMATION: the animation rate. No column of the liquid tables is proven to hold it (UNKNOWN, 09-liquids.md
    /// 1.5); 15 frames a second plays a 30-frame water loop in 2 s.</summary>
    public const double FramesPerSecond = 15.0;
    /// <summary>The transparent liquids' queue: after every opaque and alpha-tested surface (Geometry 2000, AlphaTest 2450:
    /// the terrain, the world models' materials -- blend 2 and above is drawn opaque there -- and the M2 opaque and cutout
    /// materials), before the M2 transparents (Transparent 3000 + their order, WmvModelBuilder.ApplyBlendMode) and the
    /// particles (3900): a doodad's glass or leaves blend over the water, never under it.</summary>
    public const int TransparentQueue = (int)UnityEngine.Rendering.RenderQueue.Transparent - 1;
    /// <summary>Magma and slime (and any opaque type): drawn with the opaque world geometry, writing depth.</summary>
    public const int OpaqueQueue = (int)UnityEngine.Rendering.RenderQueue.Geometry;
    /// <summary>LiquidMaterial.Flags 0x1: "is transparent" (public documentation; INFERRED, 09-liquids.md 1.4).</summary>
    public const int MaterialFlagTransparent = 0x1;

    public static readonly int FramesId = Shader.PropertyToID("_Frames"), FrameCountId = Shader.PropertyToID("_FrameCount"),
                               FrameId = Shader.PropertyToID("_Frame"), BaseColorId = Shader.PropertyToID("_BaseColor"),
                               ModeId = Shader.PropertyToID("_LiquidMode"), HighlightId = Shader.PropertyToID("_Highlight"),
                               ShallowAlphaId = Shader.PropertyToID("_ShallowAlpha"), DeepAlphaId = Shader.PropertyToID("_DeepAlpha"),
                               LinearOutId = Shader.PropertyToID("_WmvLinearOut"), SrcBlendId = Shader.PropertyToID("_SrcBlend"),
                               DstBlendId = Shader.PropertyToID("_DstBlend"), ZWriteId = Shader.PropertyToID("_ZWrite"),
                               CullId = Shader.PropertyToID("_Cull"), ColourFromFrameId = Shader.PropertyToID("_ColourFromFrame");
    /// <summary>_LiquidMode: alpha-blended over the scene (the material's flag 0x1), or opaque.</summary>
    public const float ModeTransparent = 0f, ModeOpaque = 1f;

    static Shader shader;
    static bool shaderResolved;

    /// <summary>Resources/WmvLiquid.shader, once per session; null when the player was built without it (no liquid is drawn,
    /// and the log says so).</summary>
    public static Shader ResolveShader(Action<string> log)
    {
        if (shaderResolved)
            return shader;
        shaderResolved = true;
        Shader s = Resources.Load<Shader>(ShaderResource);
        // The error shader is what a broken import hands back; it is never an answer.
        if (s != null && s.name != null && s.name.StartsWith("Hidden/", StringComparison.Ordinal))
            s = null;
        if (s != null && !s.isSupported)
        {
            if (log != null)
                log("liquid shader '" + s.name + "' is not supported on this GPU -- no liquid is drawn");
            s = null;
        }
        shader = s;
        if (log != null)
            log(s != null ? "liquid shader '" + s.name + "' (Resources/" + ShaderResource + ")"
                          : "Resources/" + ShaderResource + ".shader is missing from this player build -- no liquid is drawn");
        return shader;
    }

    /// <summary>Is the type drawn alpha-blended? Its material's flag 0x1 (INFERRED "transparent"); a type without a
    /// LiquidMaterial row (MaterialFlags -1, never on the four clients) by its kind: water and ocean are, magma and slime not.</summary>
    public static bool Transparent(LiquidTypeView t)
    {
        if (t.MaterialFlags >= 0)
            return (t.MaterialFlags & MaterialFlagTransparent) != 0;
        return t.IsWaterOrOcean;
    }

    /// <summary>Does the type take its colour from its frames? By its kind, never by the transparency flag (09-liquids.md 1.5:
    /// the category is the SoundBank kind): magma and slime frames are coloured -- the frame's colour; water and ocean frames
    /// are near-black with the ripples in alpha -- the base colour plus that alpha as a highlight. Retail's "10FX" lava, fel
    /// and swamp types (materials 13, 14 and 18, kind magma) are blended AND take their frame's colour. A type without a kind
    /// (-1, never on the four clients) as its blending: an opaque one the frame's colour.</summary>
    public static bool ColourFromFrame(LiquidTypeView t)
    {
        if (t.Kind >= 0)
            return t.Kind == (int)LiquidKind.Magma || t.Kind == (int)LiquidKind.Slime;
        return !Transparent(t);
    }

    /// <summary>MinimapStaticCol (0xAARRGGBB as stored) as the authored rgba the shader takes. A vector, not a colour: a
    /// material colour is converted from sRGB in a linear project, and this one is authored.</summary>
    public static Vector4 BaseColour(uint argb)
    {
        return new Vector4(((argb >> 16) & 0xFF) / 255f, ((argb >> 8) & 0xFF) / 255f, (argb & 0xFF) / 255f, ((argb >> 24) & 0xFF) / 255f);
    }

    /// <summary>A type's material: its base colour, its colour source (ColourFromFrame: by kind), its mode and the render
    /// state that goes with it (by the flag -- transparent: blended, no depth write, TransparentQueue; opaque: no blend, depth
    /// write, OpaqueQueue), culling off (a liquid is one sheet, seen from above and below), no frames yet (the base colour
    /// alone until its array is bound).</summary>
    public static Material CreateMaterial(Shader s, LiquidTypeView t)
    {
        bool transparent = Transparent(t);
        var m = new Material(s) { name = "WmvLiquid type " + t.Id + " (" + (t.Name ?? "") + ")" };
        m.SetVector(BaseColorId, BaseColour(t.MinimapColor));
        m.SetFloat(ModeId, transparent ? ModeTransparent : ModeOpaque);
        m.SetFloat(ColourFromFrameId, ColourFromFrame(t) ? 1f : 0f);
        m.SetFloat(HighlightId, HighlightScale);
        m.SetFloat(ShallowAlphaId, ShallowAlpha);
        m.SetFloat(DeepAlphaId, DeepAlpha);
        m.SetFloat(FrameCountId, 0f);
        m.SetFloat(FrameId, 0f);
        m.SetFloat(LinearOutId, WmvModelBuilder.AuthoredTextureDomain ? 0f : 1f);
        m.SetFloat(CullId, (float)UnityEngine.Rendering.CullMode.Off);
        if (transparent)
        {
            m.SetFloat(SrcBlendId, (float)UnityEngine.Rendering.BlendMode.SrcAlpha);
            m.SetFloat(DstBlendId, (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
            m.SetFloat(ZWriteId, 0f);
            m.SetOverrideTag("RenderType", "Transparent");
            m.renderQueue = TransparentQueue;
        }
        else
        {
            m.SetFloat(SrcBlendId, (float)UnityEngine.Rendering.BlendMode.One);
            m.SetFloat(DstBlendId, (float)UnityEngine.Rendering.BlendMode.Zero);
            m.SetFloat(ZWriteId, 1f);
            m.SetOverrideTag("RenderType", "Opaque");
            m.renderQueue = OpaqueQueue;
        }
        return m;
    }
}

/// <summary>One liquid type of a scene: its catalogue row, its material (null: the type is not drawn -- the catalogue lacks
/// it, or the shader is missing), its frames (null: slot 0 names none) and who holds it.</summary>
public sealed class WmvLiquidType
{
    public int Id;
    public LiquidTypeView View;
    public Material Material;
    public WmvLiquidFrames Frames;
    public readonly HashSet<object> Holders = new HashSet<object>();
    public long LastUse;
    public int ShownFrame = -1;          // the frame its material holds now (Tick)
    public bool Disposed;

    public bool Pooled { get { return Holders.Count == 0; } }
    public bool Animated { get { return Frames != null && Frames.Array != null && Frames.Count > 0; } }
}

/// <summary>One distinct frame list (texture slot 0 of one or more types): its array once every frame has settled.</summary>
public sealed class WmvLiquidFrames
{
    public const string StateFetching = "fetching", StateReady = "ready", StateFailed = "failed";

    public string Key;
    public uint[] Fids = new uint[0];
    public string State = StateFetching;
    public string Detail = "";
    public Texture2DArray Array;
    public int Count;                    // slices in Array
    public long GpuBytes;
    public double UploadMs = -1;
    public int Refs;                     // types that list it
}

/// <summary>One frame file of the registry, by FileDataID.</summary>
public sealed class WmvLiquidFrame
{
    public const string StateWanted = "wanted", StateAsking = "asking", StateReady = "ready", StateFailed = "failed";

    public uint Fid;
    public string State = StateWanted;
    public object Owner;                 // the job whose request fetches it (StateAsking)
    public WmvTerrainTexture Prepared;   // StateReady: its prepared data until every list that needs it is uploaded
    public string FailState = "", Detail = "";
    public readonly List<WmvLiquidFrames> Lists = new List<WmvLiquidFrames>(1);
}

/// <summary>A scene's liquid types (see the file header).</summary>
public sealed class WmvLiquidRegistry
{
    // Across the player: the registries' materials and arrays alive, and the arrays' GPU bytes (runtimeState).
    public static int LiveMaterials, LiveArrays;
    public static long LiveArrayBytes;

    public readonly WmvIpcClient.LiquidOffer Offer;
    /// <summary>A streaming session's: a type nothing holds stays (pooled) until Evict.</summary>
    public readonly bool Pooled;
    /// <summary>The use counter a pooled type is stamped with (the session's, so the pools order types among prototypes and
    /// textures); a counter of its own when not set.</summary>
    public Func<long> NextUse;
    readonly Func<Action<UnityEngine.Object>> destroy;
    readonly Action<string> log;
    readonly Dictionary<int, WmvLiquidType> types = new Dictionary<int, WmvLiquidType>();
    readonly Dictionary<string, WmvLiquidFrames> lists = new Dictionary<string, WmvLiquidFrames>();
    readonly Dictionary<uint, WmvLiquidFrame> frames = new Dictionary<uint, WmvLiquidFrame>();
    readonly Dictionary<object, List<int>> holds = new Dictionary<object, List<int>>();
    long use;
    bool disposed;
    // Counters for the log and the reports.
    public long TypesEvicted, ArraysBuilt, FramesDropped;

    /// <param name="destroy">How the registry's objects are destroyed: asked for at each destroy (the session's hook).</param>
    public WmvLiquidRegistry(WmvIpcClient.LiquidOffer offer, bool pooled, Func<Action<UnityEngine.Object>> destroy, Action<string> log)
    {
        if (offer == null) throw new ArgumentNullException("offer");
        Offer = offer;
        Pooled = pooled;
        this.destroy = destroy ?? (() => o => UnityEngine.Object.Destroy(o));
        this.log = log ?? (s => { });
    }

    public ILiquidCatalog Catalog { get { return Offer.Catalog; } }
    public bool Disposed { get { return disposed; } }
    public int TypeCount { get { return types.Count; } }
    public IEnumerable<WmvLiquidType> Types { get { return types.Values; } }

    /// <summary>The arrays built (lists with an array) and their GPU bytes.</summary>
    public int ArrayCount
    {
        get
        {
            int n = 0;
            foreach (WmvLiquidFrames l in lists.Values) if (l.Array != null) n++;
            return n;
        }
    }

    public long ArrayBytes
    {
        get
        {
            long n = 0;
            foreach (WmvLiquidFrames l in lists.Values) if (l.Array != null) n += l.GpuBytes;
            return n;
        }
    }

    public int PooledCount
    {
        get
        {
            int n = 0;
            foreach (WmvLiquidType t in types.Values) if (t.Pooled) n++;
            return n;
        }
    }

    /// <summary>The GPU bytes the pools hold: the arrays no live holder needs -- every type that lists one is pooled, or
    /// held only by holders `pooledHolder` says are pooled themselves (a streaming session's pooled world-model prototypes:
    /// the Stage 08 rule of the M2 texture uploads). Null: only pooled types count.</summary>
    public long PooledGpuBytes(Func<object, bool> pooledHolder)
    {
        long n = 0;
        foreach (WmvLiquidFrames l in lists.Values)
            if (l.Array != null && !Held(l, pooledHolder))
                n += l.GpuBytes;
        return n;
    }

    bool Held(WmvLiquidFrames l, Func<object, bool> pooledHolder)
    {
        foreach (WmvLiquidType t in types.Values)
            if (t.Frames == l)
                foreach (object h in t.Holders)
                    if (pooledHolder == null || !pooledHolder(h))
                        return true;
        return false;
    }

    public WmvLiquidType Get(int typeId)
    {
        WmvLiquidType t;
        return types.TryGetValue(typeId, out t) ? t : null;
    }

    public WmvLiquidFrame Frame(uint fid)
    {
        WmvLiquidFrame f;
        return frames.TryGetValue(fid, out f) ? f : null;
    }

    long Stamp() { return NextUse != null ? NextUse() : ++use; }

    /// <summary>`holder` holds type `typeId` (made on first use: its material and its frame list); returns it, or null when
    /// the catalogue lacks the type or the registry is disposed. Holding twice is holding once.</summary>
    public WmvLiquidType Hold(object holder, int typeId)
    {
        if (disposed || holder == null)
            return null;
        LiquidTypeView view;
        if (Catalog == null || !Catalog.TryGetType(typeId, out view) || view == null)
            return null;
        WmvLiquidType t;
        if (!types.TryGetValue(typeId, out t))
        {
            t = new WmvLiquidType { Id = typeId, View = view };
            types[typeId] = t;
            Shader s = WmvLiquidMaterials.ResolveShader(log);
            if (s != null)
            {
                t.Material = WmvLiquidMaterials.CreateMaterial(s, view);
                LiveMaterials++;
            }
            if (view.Frames != null && view.Frames.Length > 0)
                t.Frames = ListOf(view.Frames);
            if (t.Frames != null && t.Material != null && t.Frames.Array != null)
                Bind(t);
        }
        if (t.Holders.Add(holder))
        {
            List<int> held;
            if (!holds.TryGetValue(holder, out held))
                holds[holder] = held = new List<int>(2);
            held.Add(typeId);
        }
        t.LastUse = Stamp();
        return t;
    }

    /// <summary>Everything `holder` holds given back now: a type nothing holds any more is disposed (a single load's registry)
    /// or pooled (a streaming session's).</summary>
    public void Release(object holder)
    {
        Release(holder, -1);
    }

    /// <summary>Release, `lastUse` the holder's own last use (the session's counter; -1: now): a pooled world-model prototype
    /// evicted gives its types back as last used when IT was, not as the newest use -- the pools evict least recently used
    /// first. Returns the types this left pooled (a streaming session's; a single load's are disposed).</summary>
    public List<WmvLiquidType> Release(object holder, long lastUse)
    {
        var pooled = new List<WmvLiquidType>();
        List<int> held;
        if (holder == null || !holds.TryGetValue(holder, out held))
            return pooled;
        holds.Remove(holder);
        foreach (int id in held)
        {
            WmvLiquidType t;
            if (!types.TryGetValue(id, out t))
                continue;
            t.Holders.Remove(holder);
            // Used by that holder until it gave the type back (now, or its own last use).
            t.LastUse = lastUse >= 0 ? Math.Max(t.LastUse, lastUse) : Stamp();
            if (!t.Pooled)
                continue;
            if (!Pooled)
                DisposeType(t);
            else
                pooled.Add(t);
        }
        return pooled;
    }

    /// <summary>The frame list of `fids` (one per distinct list, shared), its frames registered.</summary>
    WmvLiquidFrames ListOf(uint[] fids)
    {
        string key = string.Join(",", Array.ConvertAll(fids, x => x.ToString(System.Globalization.CultureInfo.InvariantCulture)));
        WmvLiquidFrames l;
        if (lists.TryGetValue(key, out l))
        {
            l.Refs++;
            return l;
        }
        l = new WmvLiquidFrames { Key = key, Fids = (uint[])fids.Clone(), Refs = 1 };
        lists[key] = l;
        foreach (uint fid in l.Fids)
        {
            WmvLiquidFrame f;
            if (!frames.TryGetValue(fid, out f))
                frames[fid] = f = new WmvLiquidFrame { Fid = fid };
            else if (f.State == WmvLiquidFrame.StateReady && (f.Prepared == null || !f.Prepared.Ready))
            {
                // Uploaded for another list and its data dropped: fetched again for this one (a load serves an id once, so
                // within one load this frame fails; another load of a streaming session may ask for it).
                f.State = WmvLiquidFrame.StateWanted;
                f.Prepared = null;
            }
            if (!f.Lists.Contains(l))
                f.Lists.Add(l);
        }
        // Every frame settled already (failed ones, or prepared and still held for another list): settle it now.
        Complete(l);
        return l;
    }

    /// <summary>A request for frame `fid` is out, `owner`'s.</summary>
    public void Asking(uint fid, object owner)
    {
        WmvLiquidFrame f;
        if (!frames.TryGetValue(fid, out f))
            return;
        f.State = WmvLiquidFrame.StateAsking;
        f.Owner = owner;
    }

    /// <summary>`owner` ended: the frames it was fetching are wanted again (another holder may ask for them).</summary>
    public void OwnerEnded(object owner)
    {
        foreach (WmvLiquidFrame f in frames.Values)
            if (f.State == WmvLiquidFrame.StateAsking && f.Owner == owner)
            {
                f.State = WmvLiquidFrame.StateWanted;
                f.Owner = null;
            }
    }

    /// <summary>Frame `fid` arrived and was prepared: every list that needs it is completed when its last frame settles.
    /// False when no list of the registry lists it any more (its types went): dropped, never uploaded. When no list it is
    /// on still fetches (they settled before it came: another frame failed, or the fetching one went), nothing will upload
    /// it: its data are dropped at once (a list that names it later fetches it again, ListOf).</summary>
    public bool FramePrepared(uint fid, WmvTerrainTexture p)
    {
        WmvLiquidFrame f;
        if (disposed || !frames.TryGetValue(fid, out f) || f.Lists.Count == 0)
        {
            FramesDropped++;
            if (p != null) p.ReleaseUploadData();
            return false;
        }
        f.State = WmvLiquidFrame.StateReady;
        f.Owner = null;
        f.Prepared = p;
        foreach (WmvLiquidFrames l in new List<WmvLiquidFrames>(f.Lists))
            Complete(l);
        ReleaseUnlessFetching(f);
        return true;
    }

    /// <summary>A frame's prepared data dropped when no list it is on still fetches (Settled's rule; releasing twice is
    /// harmless).</summary>
    static void ReleaseUnlessFetching(WmvLiquidFrame f)
    {
        if (f.Prepared == null)
            return;
        foreach (WmvLiquidFrames l in f.Lists)
            if (l.State == WmvLiquidFrames.StateFetching)
                return;
        f.Prepared.ReleaseUploadData();
    }

    /// <summary>Frame `fid` cannot be had: for good (`retry` false -- the lists that need it are drawn in their base colour),
    /// or only for the load that asked (`retry` true, a streaming session: refused or stale for that tile's load -- wanted
    /// again, another tile may ask for it).</summary>
    public void FrameFailed(uint fid, string state, string detail, bool retry)
    {
        WmvLiquidFrame f;
        if (disposed || !frames.TryGetValue(fid, out f))
            return;
        f.Owner = null;
        if (retry)
        {
            f.State = WmvLiquidFrame.StateWanted;
            return;
        }
        f.State = WmvLiquidFrame.StateFailed;
        f.FailState = state ?? "";
        f.Detail = detail ?? "";
        f.Prepared = null;
        foreach (WmvLiquidFrames l in new List<WmvLiquidFrames>(f.Lists))
            Complete(l);
    }

    /// <summary>A list whose frames have all settled: its array built (every frame prepared and of one size: in their own
    /// format when they share it and their mip count, else as RGBA32 with the levels they all have), or the list failed (a
    /// frame that cannot be had, or frames of another size) -- its types drawn in their base colour. Then the prepared data
    /// no list still waits for are dropped.</summary>
    void Complete(WmvLiquidFrames l)
    {
        if (l.State != WmvLiquidFrames.StateFetching)
            return;
        var prepared = new WmvTerrainTexture[l.Fids.Length];
        for (int k = 0; k < l.Fids.Length; k++)
        {
            WmvLiquidFrame f = frames[l.Fids[k]];
            if (f.State == WmvLiquidFrame.StateWanted || f.State == WmvLiquidFrame.StateAsking)
                return;
            if (f.State == WmvLiquidFrame.StateFailed)
            {
                l.State = WmvLiquidFrames.StateFailed;
                l.Detail = "frame " + (k + 1) + " (FileDataID " + f.Fid + ") " + f.FailState + (f.Detail.Length > 0 ? ": " + f.Detail : "");
                Settled(l);
                return;
            }
            prepared[k] = f.Prepared;
        }
        WmvTerrainTexture first = prepared[0];
        bool uniform = true;
        int levels = first.Mips;
        foreach (WmvTerrainTexture p in prepared)
        {
            if (p == null || !p.Ready || p.Width != first.Width || p.Height != first.Height)
            {
                l.State = WmvLiquidFrames.StateFailed;
                l.Detail = p == null || !p.Ready
                    ? "a frame's prepared data are gone"
                    : string.Format("the frames differ in size ({0}x{1} and {2}x{3}): one array cannot hold them", first.Width, first.Height,
                                    p.Width, p.Height);
                Settled(l);
                return;
            }
            uniform &= p.Mips == first.Mips && p.GraphicsFormat == first.GraphicsFormat;
            levels = Math.Min(levels, p.Mips);
        }
        var clock = System.Diagnostics.Stopwatch.StartNew();
        // Frames of one size but another format or mip count: every frame as RGBA32 with the levels they all have (decoded
        // here, on the main thread -- never seen in a client's frames).
        if (!uniform)
            foreach (WmvTerrainTexture p in prepared)
                if (!p.ConvertToRgba(levels))
                {
                    l.State = WmvLiquidFrames.StateFailed;
                    l.Detail = "frame FileDataID " + p.FileDataID + " could not be decoded to RGBA32 to share an array with frames of another format";
                    Settled(l);
                    return;
                }
        first = prepared[0];
        int n = prepared.Length, mips = first.Mips;
        var flags = TextureCreationFlags.DontInitializePixels | (mips > 1 ? TextureCreationFlags.MipChain : TextureCreationFlags.None);
        var a = new Texture2DArray(first.Width, first.Height, n, first.GraphicsFormat, flags, mips)
        {
            name = "WmvLiquid frames " + l.Fids[0] + ".. (" + n + ")",
        };
        long bytes = 0;
        foreach (WmvTerrainTexture p in prepared)
            bytes += p.GpuBytes;
        // Counted the moment it exists, so an upload that throws still destroys it with the list.
        l.Array = a;
        l.GpuBytes = bytes;
        LiveArrays++;
        LiveArrayBytes += bytes;
        try
        {
            for (int k = 0; k < n; k++)
                prepared[k].UploadSlice(a, k);
            a.wrapMode = TextureWrapMode.Repeat;
            a.filterMode = FilterMode.Trilinear;
            a.anisoLevel = WmvTerrainMaterials.LayerAnisoLevel;
            a.Apply(false, true);
        }
        catch (Exception e)
        {
            DestroyArray(l);
            l.State = WmvLiquidFrames.StateFailed;
            l.Detail = "upload failed: " + e.GetType().Name + ": " + e.Message;
            Settled(l);
            return;
        }
        l.Count = n;
        l.State = WmvLiquidFrames.StateReady;
        l.UploadMs = clock.Elapsed.TotalMilliseconds;
        ArraysBuilt++;
        foreach (WmvLiquidType t in types.Values)
            if (t.Frames == l)
                Bind(t);
        Settled(l);
    }

    /// <summary>The prepared data of `l`'s frames dropped where no list still fetching needs them.</summary>
    void Settled(WmvLiquidFrames l)
    {
        string types = "";
        foreach (WmvLiquidType t in this.types.Values)
            if (t.Frames == l)
                types += (types.Length > 0 ? ", " : "") + t.Id;
        log(l.State == WmvLiquidFrames.StateReady
            ? string.Format(System.Globalization.CultureInfo.InvariantCulture, "frames of type(s) {0}: {1} frame(s) uploaded as one array, {2}x{3} {4}, " +
                            "{5} GPU bytes, {6:0.###} ms", types, l.Count, l.Array.width, l.Array.height, l.Array.graphicsFormat, l.GpuBytes, l.UploadMs)
            : "frames of type(s) " + types + ": not animated, drawn in the base colour -- " + l.Detail);
        foreach (uint fid in l.Fids)
        {
            WmvLiquidFrame f;
            if (frames.TryGetValue(fid, out f))
                ReleaseUnlessFetching(f);
        }
    }

    void Bind(WmvLiquidType t)
    {
        if (t.Material == null || t.Frames == null || t.Frames.Array == null)
            return;
        t.Material.SetTexture(WmvLiquidMaterials.FramesId, t.Frames.Array);
        t.Material.SetFloat(WmvLiquidMaterials.FrameCountId, t.Frames.Count);
        t.ShownFrame = -1;
    }

    /// <summary>The clock (seconds, the liquid clock of the world view): each animated type's material shows frame
    /// floor(seconds x FramesPerSecond) mod its frames; set only when it changes.</summary>
    public void Tick(double seconds)
    {
        if (disposed || double.IsNaN(seconds) || double.IsInfinity(seconds))
            return;
        long step = (long)Math.Floor(Math.Max(0.0, seconds) * WmvLiquidMaterials.FramesPerSecond);
        foreach (WmvLiquidType t in types.Values)
        {
            if (!t.Animated || t.Material == null)
                continue;
            int f = (int)(step % t.Frames.Count);
            if (f == t.ShownFrame)
                continue;
            t.ShownFrame = f;
            t.Material.SetFloat(WmvLiquidMaterials.FrameId, f);
        }
    }

    /// <summary>The pooled types, least recently used first (the session's pools).</summary>
    public List<WmvLiquidType> PooledTypes()
    {
        var l = new List<WmvLiquidType>();
        foreach (WmvLiquidType t in types.Values)
            if (t.Pooled)
                l.Add(t);
        l.Sort((a, b) => a.LastUse.CompareTo(b.LastUse));
        return l;
    }

    /// <summary>A pooled type evicted: its material and, when no other type lists them, its frames go; returns the GPU bytes
    /// freed (0 when it is held, or its array is another type's too).</summary>
    public long Evict(WmvLiquidType t)
    {
        if (t == null || !t.Pooled || t.Disposed)
            return 0;
        long before = ArrayBytes;
        DisposeType(t);
        TypesEvicted++;
        return before - ArrayBytes;
    }

    void DisposeType(WmvLiquidType t)
    {
        if (t.Disposed)
            return;
        t.Disposed = true;
        types.Remove(t.Id);
        if (t.Material != null)
        {
            destroy()(t.Material);
            LiveMaterials--;
            t.Material = null;
        }
        WmvLiquidFrames l = t.Frames;
        t.Frames = null;
        if (l == null || --l.Refs > 0)
            return;
        lists.Remove(l.Key);
        DestroyArray(l);
        // Its frames: off every list; a frame no list needs goes (an answer for it is dropped, never uploaded).
        foreach (uint fid in l.Fids)
        {
            WmvLiquidFrame f;
            if (!frames.TryGetValue(fid, out f))
                continue;
            f.Lists.Remove(l);
            if (f.Lists.Count > 0)
            {
                ReleaseUnlessFetching(f);       // (the list that went may have been the only one still waiting for it)
                continue;
            }
            if (f.Prepared != null)
                f.Prepared.ReleaseUploadData();
            frames.Remove(fid);
        }
    }

    void DestroyArray(WmvLiquidFrames l)
    {
        if (l.Array == null)
            return;
        destroy()(l.Array);
        LiveArrays--;
        LiveArrayBytes -= l.GpuBytes;
        l.Array = null;
        l.Count = 0;
    }

    /// <summary>Every type, material and array goes (the scene goes, or a load that never became the scene's ended).</summary>
    public void Dispose()
    {
        if (disposed)
            return;
        foreach (WmvLiquidType t in new List<WmvLiquidType>(types.Values))
            DisposeType(t);
        foreach (WmvLiquidFrames l in lists.Values)
            DestroyArray(l);
        lists.Clear();
        foreach (WmvLiquidFrame f in frames.Values)
            if (f.Prepared != null)
                f.Prepared.ReleaseUploadData();
        frames.Clear();
        holds.Clear();
        disposed = true;
    }

    /// <summary>The registry in a line (the log, the self-test).</summary>
    public string Summary()
    {
        int wanted = 0, asking = 0, ready = 0, failed = 0;
        foreach (WmvLiquidFrame f in frames.Values)
        {
            if (f.State == WmvLiquidFrame.StateWanted) wanted++;
            else if (f.State == WmvLiquidFrame.StateAsking) asking++;
            else if (f.State == WmvLiquidFrame.StateReady) ready++;
            else failed++;
        }
        return string.Format("{0} type(s) ({1} pooled), {2} frame list(s), {3} array(s) ({4} GPU bytes, {5} built in all); frames {6} wanted, " +
                             "{7} asked, {8} ready, {9} failed, {10} dropped (their types gone); {11} type(s) evicted", types.Count, PooledCount,
                             lists.Count, ArrayCount, ArrayBytes, ArraysBuilt, wanted, asking, ready, failed, FramesDropped, TypesEvicted);
    }
}

/// <summary>Liquid meshes and objects, and the tallies of every one alive (tiles' and world-model prototypes').</summary>
public static class WmvLiquidMeshes
{
    // Across the player: the liquid GameObjects and Meshes alive (a tile's, a world-model prototype's -- an instance's clones
    // are not counted), and their geometry's GPU bytes (LiquidSurface.MeshBytes).
    public static int LiveObjects, LiveMeshes;
    public static long LiveGeometryBytes;

    /// <summary>`s` as a Unity Mesh: positions, uv0 (texture coordinates), uv1 (depth, has-depth), its triangles (16-bit
    /// indices up to 65,535 vertices, 32-bit above), the surface's own bounds; the CPU copy released unless `keepReadable`.</summary>
    public static Mesh Create(LiquidSurface s, string name, bool keepReadable)
    {
        int n = s.VertexCount;
        var positions = new Vector3[n];
        var uv0 = new Vector2[n];
        var uv1 = new Vector2[n];
        for (int k = 0; k < n; k++)
        {
            positions[k] = new Vector3(s.Positions[3 * k], s.Positions[3 * k + 1], s.Positions[3 * k + 2]);
            uv0[k] = new Vector2(s.Uv0[2 * k], s.Uv0[2 * k + 1]);
            uv1[k] = new Vector2(s.Uv1[2 * k], s.Uv1[2 * k + 1]);
        }
        var m = new Mesh { name = name };
        m.indexFormat = n > 65535 ? UnityEngine.Rendering.IndexFormat.UInt32 : UnityEngine.Rendering.IndexFormat.UInt16;
        m.SetVertices(positions);
        m.SetUVs(0, uv0);
        m.SetUVs(1, uv1);
        m.SetIndices(s.Indices, MeshTopology.Triangles, 0, false);
        if (s.BoundsMin != null && s.BoundsMax != null)
            m.bounds = WmvTerrainBuilder.BoundsOf(s.BoundsMin, s.BoundsMax);
        if (!keepReadable)
            m.UploadMeshData(true);
        return m;
    }

    /// <summary>A liquid casts and receives nothing and takes no probe (unlit; the shadow rig never sees layer 30).</summary>
    public static void Quiet(MeshRenderer r)
    {
        r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        r.receiveShadows = false;
        r.lightProbeUsage = UnityEngine.Rendering.LightProbeUsage.Off;
        r.reflectionProbeUsage = UnityEngine.Rendering.ReflectionProbeUsage.Off;
        r.motionVectorGenerationMode = MotionVectorGenerationMode.ForceNoMotion;
    }

    /// <summary>
    /// A tile's liquid objects (WmvTerrainBuilder.Build, under the tile root, on layer 30): one GameObject, Mesh and
    /// MeshRenderer per surface (liquid type) the catalogue has, drawn with the type's shared material, each tracked by the
    /// tile the moment it exists (its disposal, deferred destroy and tallies cover them); the tile holds each type it draws
    /// until it is released. A surface of a type the catalogue lacks is not drawn (the report counts it missing). Returns
    /// the surfaces drawn.
    /// </summary>
    public static int BuildTileLiquids(WmvRuntimeTerrainTile rt, TerrainLiquidTile tile, WmvLiquidRegistry registry, Transform root,
                                       bool keepReadable)
    {
        if (tile == null || registry == null || registry.Disposed)
            return 0;
        int drawn = 0;
        foreach (LiquidSurface s in tile.Surfaces)
        {
            if (s.QuadCount == 0)
                continue;
            if (rt.Liquids == null)
                rt.Liquids = registry;
            WmvLiquidType t = registry.Hold(rt, s.LiquidType);
            if (t == null || t.Material == null)
                continue;
            var go = new GameObject("MH2O liquid type " + s.LiquidType);
            Mesh m = null;
            try
            {
                go.layer = WmvTerrainBuilder.Layer;
                go.transform.SetParent(root, false);
                m = Create(s, "WmvLiquid MH2O " + rt.TileX + "_" + rt.TileY + " type " + s.LiquidType, keepReadable);
            }
            finally
            {
                rt.TrackLiquid(go, m, m != null ? s.MeshBytes : 0);
            }
            go.AddComponent<MeshFilter>().sharedMesh = m;
            MeshRenderer r = go.AddComponent<MeshRenderer>();
            Quiet(r);
            r.sharedMaterial = t.Material;
            if (!rt.LiquidTypes.Contains(s.LiquidType))
                rt.LiquidTypes.Add(s.LiquidType);
            if (s.BoundsMin != null && s.BoundsMax != null)
            {
                Bounds b = WmvTerrainBuilder.BoundsOf(s.BoundsMin, s.BoundsMax);
                if (rt.HasLiquidBounds)
                    rt.LiquidBounds.Encapsulate(b);
                else
                {
                    rt.LiquidBounds = b;
                    rt.HasLiquidBounds = true;
                }
            }
            drawn++;
        }
        return drawn;
    }
}
