// WmvWorldLitMaterials.cs
//
// THE OUTDOOR LIGHT ON WORLD MODELS AND DOODADS: THE MATERIALS (Stage 10 step 6, docs/world-viewer/10-light-sky-fog.md 6.6,
// 6.7, 6.13): which world renderers draw with the world-lit variants, and with what. The builders are not touched -- a world
// prototype keeps the builder's materials and its own object tree is never rebound -- and an OUTDOOR renderer (an exterior
// world-model group, an outdoor doodad: Wow/World/ModelLight.cs's context rule) is bound, while the outdoor light is shown,
// to a COPY of each of its originals on the variant:
//   the variants  "WMV/Opaque Textured (world)" (Resources/WmvOpaqueWorld.shader) for a doodad's "WMV/Opaque Textured", and
//                 "WMV/Map Object (world)" (Resources/WmvWmoWorld.shader) for a world-model group's "WMV/Map Object" -- by
//                 (kind, shader name); any other shader (the NewSurface fallback, including a group's materials made on the
//                 M2 shader when the world-model shader is missing; -wmvLitShader; another pipeline's) keeps its own material
//                 and is counted. Each is loaded once, when first wanted (VariantLoads; never with WMV_LIGHT=off or in default
//                 mode); a missing or unsupported variant is said once and treated as not wanted: today's look.
//   a copy        new Material(variant) with the original's properties, its RenderType tag, render queue and keywords
//                 (by name), named "<original> (world)" (Copy). One per distinct original per prototype, made the first time a
//                 renderer that wants it is bound, kept until the prototype is disposed; counted in the world's tallies by
//                 the managers that own it (WmvWorldModels, WmvWorldDoodads), never by Copy.
//   wanted        the kind's switch on (-wmvDoodadLight, -wmvWorldModelLight), the context OUTDOOR (or -wmvInteriorLight=outdoor),
//                 and a variant for the original's shader. Not WmvWorldLight.Enabled: the managers' bound flag carries the
//                 light's state.
//   the frame     WmvMain.WorldLightFrame binds every world renderer to its copies while WmvWorldLight.ObjectsLit, and to its
//                 originals otherwise (BindFrame: a manager is rebound only when that changes or when its last rebind did not
//                 finish), so every state that shows no outdoor light -- default mode, the top-down overview, no valid light,
//                 the camera not placed, after a fault -- draws the original shaders, Stage 09's programs.
//   placement     an instance placed while the light is shown (WmvWorldModels.Place, WmvWorldDoodads.Place) is bound at once,
//                 and counted and timed in its manager's PlacedWhileShown -- with the light on from the start, as in every
//                 capture, the frame's first bind meets no instance and every world model and doodad is bound here.
//   the fog      (Stage 10 step 7, Wow/World/WorldFog.cs) a copy made also gets its fog vector, _WmvFogBatch (SetFogBatch;
//                the variants' one property of their own): x 1 for a batch the client's material flags leave unfogged (M2
//                NoFog 0x02, MOMT F_UNFOGGED), y the batch's fog target by its blend state (WorldFog.KindOf). The managers keep
//                each bound slot's fog code (FogCode*) next to its blend class, so every binding line counts them.
// THE BINDING LINE (WmvWorldLitBind.Describe, said by WmvWorldLitDriver): per rebind, what was bound, by blend mode of the
// bound batches, the copies made, live and tinted, the renderers left on their own materials and why, the bound batches' fog
// (unfogged, by fog target, unknown blends, world-model MOMT blend 2+: before the time, which the rig reads last), and the
// time; and once a frame passes without a new placement (or before a rebind), the same for what was bound at placement since,
// marked "(placed while shown)", its time the binding's alone (the placements' own work is not in it). With WMV_LIGHT=off
// nothing here is reached.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;
using Wmv.Wow.World;

/// <summary>What a rebind of the world's renderers did (WmvWorldModels / WmvWorldDoodads.RebindWorldLit fill it).</summary>
public sealed class WmvWorldLitBind
{
    /// <summary>To the copies (true) or to the originals.</summary>
    public bool Lit;
    /// <summary>Bound to the world-lit variants: group renderers, the placements they belong to, doodads.</summary>
    public int GroupRenderers, Placements, Doodads;
    /// <summary>The bound batches (material slots holding a copy) by blend class (WmvWorldLitMaterials.Blend*).</summary>
    public readonly int[] Batches = new int[WmvWorldLitMaterials.BlendKinds];
    /// <summary>Stage 10 step 7: the bound batches' fog (their slots' fog codes, WmvWorldLitMaterials.SetFogBatch): unfogged
    /// (NoFog / F_UNFOGGED), the others by fog target (WorldFog.Kind*) and the unknown blends among them (kind 0, counted);
    /// the world-model batches whose MOMT blend is 2 or above (drawn One / Zero provisionally).</summary>
    public int FogUnfogged, FogUnknownBlend, MomtBlend2;
    public readonly int[] FogKinds = new int[WorldFog.Kinds];
    /// <summary>Copies made by this rebind.</summary>
    public int CopiesMade;
    /// <summary>Renderers left on their own materials: interior-interim, their kind's switch off, wanted but without a
    /// world-lit variant for some slot (with the shader names), instances whose renderers are not their prototype's.</summary>
    public int OwnMaterials, KindOff, NoVariant, Mismatched;
    public readonly List<string> NoVariantShaders = new List<string>();
    /// <summary>Renderers set back to their originals (a rebind to the originals).</summary>
    public int Unbound;
    public double Ms;
    /// <summary>What instances placed while the light was shown were bound to (a manager's PlacedWhileShown, Lit), not a
    /// rebind; Calls is how many placements it counts (WmvWorldLitDriver says it once no new one came for a frame).</summary>
    public bool Placed;
    public int Calls;

    /// <summary>Renderers bound to the copies.</summary>
    public int Bound { get { return GroupRenderers + Doodads; } }

    /// <summary>One placement's binding counted: its time since the timestamp `t0` (Stopwatch.GetTimestamp, no allocation).</summary>
    public void AddSince(long t0)
    {
        Ms += (System.Diagnostics.Stopwatch.GetTimestamp() - t0) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
        Calls++;
    }

    /// <summary>Another manager's counts added to these (the world models' and the doodads' placements in one line).</summary>
    public void Add(WmvWorldLitBind o)
    {
        if (o == null)
            return;
        GroupRenderers += o.GroupRenderers;
        Placements += o.Placements;
        Doodads += o.Doodads;
        for (int k = 0; k < Batches.Length; k++)
            Batches[k] += o.Batches[k];
        FogUnfogged += o.FogUnfogged;
        FogUnknownBlend += o.FogUnknownBlend;
        MomtBlend2 += o.MomtBlend2;
        for (int k = 0; k < FogKinds.Length; k++)
            FogKinds[k] += o.FogKinds[k];
        CopiesMade += o.CopiesMade;
        OwnMaterials += o.OwnMaterials;
        KindOff += o.KindOff;
        NoVariant += o.NoVariant;
        Mismatched += o.Mismatched;
        foreach (string s in o.NoVariantShaders)
            if (!NoVariantShaders.Contains(s))
                NoVariantShaders.Add(s);
        Unbound += o.Unbound;
        Ms += o.Ms;
        Calls += o.Calls;
    }

    /// <summary>Did the rebind look at any renderer (to the copies) or set any back (to the originals)? A rebind that touches
    /// none says nothing.</summary>
    public bool Touched { get { return Lit ? Bound + OwnMaterials + KindOff + NoVariant + Mismatched > 0 : Unbound > 0; } }

    public void AddNoVariant(string shader)
    {
        NoVariant++;
        if (shader != null && !NoVariantShaders.Contains(shader))
            NoVariantShaders.Add(shader);
    }

    public void AddBatches(int[] blends)
    {
        if (blends == null)
            return;
        foreach (int b in blends)
            if (b >= 0 && b < Batches.Length)
                Batches[b]++;
    }

    /// <summary>The bound slots' blend classes and (Stage 10 step 7) their fog codes (WmvWorldLitMaterials.FogCode*; -1 for a
    /// slot left on its original, as its blend class).</summary>
    public void AddBatches(int[] blends, int[] fogCodes)
    {
        AddBatches(blends);
        if (fogCodes == null)
            return;
        foreach (int f in fogCodes)
        {
            if (f < 0)
                continue;
            if ((f & WmvWorldLitMaterials.FogCodeUnfogged) != 0)
                FogUnfogged++;
            else
            {
                FogKinds[f & WmvWorldLitMaterials.FogCodeKindMask]++;
                if ((f & WmvWorldLitMaterials.FogCodeUnknownBlend) != 0)
                    FogUnknownBlend++;
            }
            if ((f & WmvWorldLitMaterials.FogCodeMomtBlend2) != 0)
                MomtBlend2++;
        }
    }

    /// <summary>The binding line (the file header); `why` says why the light is not shown (a rebind to the originals).</summary>
    public string Describe(string why)
    {
        if (!Lit)
            return string.Format(CultureInfo.InvariantCulture, "world light: world models and doodads -> their own materials (not shown: {0}): {1} renderer(s) " +
                                 "in {2:0.0} ms", why ?? "-", Unbound, Ms);
        var s = new StringBuilder();
        s.Append("world light: world models and doodads -> the outdoor light");
        if (Placed)
            s.AppendFormat(CultureInfo.InvariantCulture, " (placed while shown: {0} instance(s) bound as they were placed)", Calls);
        s.AppendFormat(CultureInfo.InvariantCulture, ": {0} renderer(s) bound to the world-lit " +
                       "variants ({1} group renderer(s) of {2} placement(s), {3} doodad(s); batches by blend mode: opaque {4}, alpha-key {5}, alpha {6}, " +
                       "additive {7}, modulate {8}, modulate 2x {9}, other {10}), {11} material(s) copied now ({12} copies live; {13} with a non-white " +
                       "tint), {14} renderer(s) on their own materials (interior-interim), {15} without a world-lit variant{16}",
                       Bound, GroupRenderers, Placements, Doodads, Batches[WmvWorldLitMaterials.BlendOpaque], Batches[WmvWorldLitMaterials.BlendAlphaKey],
                       Batches[WmvWorldLitMaterials.BlendAlpha], Batches[WmvWorldLitMaterials.BlendAdditive], Batches[WmvWorldLitMaterials.BlendModulate],
                       Batches[WmvWorldLitMaterials.BlendModulate2x], Batches[WmvWorldLitMaterials.BlendOther], CopiesMade,
                       WmvWorldModels.LiveCopies + WmvWorldDoodads.LiveCopies, WmvWorldModels.LiveTintedCopies + WmvWorldDoodads.LiveTintedCopies,
                       OwnMaterials, NoVariant, NoVariantShaders.Count > 0 ? " (" + string.Join(", ", NoVariantShaders.ToArray()) + ")" : "");
        if (KindOff > 0)
            s.AppendFormat(CultureInfo.InvariantCulture, ", {0} on their own materials by -wmvDoodadLight=off or -wmvWorldModelLight=off", KindOff);
        if (Mismatched > 0)
            s.AppendFormat(CultureInfo.InvariantCulture, ", {0} instance(s) whose renderers are not their prototype's left on their own materials", Mismatched);
        // Stage 10 step 7: the bound batches' fog, before the time (the rig takes the line's last "N ms" as its time).
        s.Append(WorldFog.BindingClause(FogUnfogged, FogKinds, FogUnknownBlend, MomtBlend2));
        s.AppendFormat(CultureInfo.InvariantCulture, Placed ? "; {0:0.000} ms" : "; {0:0.0} ms", Ms);
        return s.ToString();
    }
}

public static class WmvWorldLitMaterials
{
    /// <summary>The variants under Assets/Resources (never stripped from a player build).</summary>
    public const string DoodadVariantResource = "WmvOpaqueWorld", WorldModelVariantResource = "WmvWmoWorld";
    /// <summary>The originals' shaders the variants replace, by kind.</summary>
    public const string DoodadShaderName = "WMV/Opaque Textured", WorldModelShaderName = "WMV/Map Object";
    /// <summary>A variant's name is its original's plus this, and so is a copy's.</summary>
    public const string Suffix = " (world)";

    /// <summary>A bound batch's blend class (the binding line): from the material's _SrcBlend / _DstBlend and its alpha test.
    /// Defined once in Wow/World/WorldFog.cs (Stage 10 step 7: the fog target follows it); these alias it, values unchanged.</summary>
    public const int BlendOpaque = WorldFog.BlendOpaque, BlendAlphaKey = WorldFog.BlendAlphaKey, BlendAlpha = WorldFog.BlendAlpha,
                     BlendAdditive = WorldFog.BlendAdditive, BlendModulate = WorldFog.BlendModulate, BlendModulate2x = WorldFog.BlendModulate2x,
                     BlendOther = WorldFog.BlendOther, BlendKinds = WorldFog.BlendKinds;

    /// <summary>Stage 10 step 7: a copy's fog vector (the variants' _WmvFogBatch: x 1 unfogged, y the fog target kind).</summary>
    public static readonly int FogBatchId = Shader.PropertyToID("_WmvFogBatch");
    /// <summary>Stage 10 step 7: a bound slot's fog code (SetFogBatch; -1 for a slot left on its original): the fog target kind
    /// (WorldFog.Kind*) in the low bits, and the flags unfogged (NoFog / F_UNFOGGED: the hook skips the batch), an unknown
    /// blend (kind 0, counted, never faded silently) and, for a world-model slot, its material's MOMT blend 2 or above.</summary>
    public const int FogCodeKindMask = 7, FogCodeUnfogged = 8, FogCodeUnknownBlend = 16, FogCodeMomtBlend2 = 32;

    static readonly int SrcBlendId = Shader.PropertyToID("_SrcBlend"), DstBlendId = Shader.PropertyToID("_DstBlend"),
                        AlphaTestId = Shader.PropertyToID("_AlphaTest"), ColorId = Shader.PropertyToID("_Color");

    static Shader doodadVariant, worldModelVariant;
    static bool doodadResolved, worldModelResolved;

    /// <summary>Resources.Load calls made for the variants (once each at most). The hard-off self-test reads it: 0.</summary>
    public static int VariantLoads { get; private set; }
    /// <summary>Copies made by Copy over the whole player (the self-test's probes included). WmvMain watches it for the first
    /// lit frame's line.</summary>
    public static int CopiesMade { get; private set; }
    /// <summary>Stage 10 step 7: fog vectors written by SetFogBatch over the whole player. The hard-off self-test reads it: 0.</summary>
    public static int FogBatchesSet { get; private set; }

    /// <summary>The kind's switch: -wmvDoodadLight (doodads) or -wmvWorldModelLight (world-model groups). A renderer takes the
    /// copy of an original when KindOn, ContextOn and ResolveVariant all say so ("wanted", the file header); the managers ask
    /// them one at a time so the binding line can say which kept a renderer on its own materials.</summary>
    public static bool KindOn(bool doodad)
    {
        WorldLightSwitches w = WmvWorldLight.Switches;
        return doodad ? w.DoodadLight : w.WorldModelLight;
    }

    /// <summary>The context takes the outdoor light: OUTDOOR, or any with -wmvInteriorLight=outdoor (the side-by-sides).</summary>
    public static bool ContextOn(WorldLightContext context)
    {
        return context == WorldLightContext.Outdoor || WmvWorldLight.Switches.InteriorOutdoor;
    }

    /// <summary>
    /// The world-lit variant for `original` drawn by a renderer of this kind: the doodads' for "WMV/Opaque Textured", the world
    /// models' for "WMV/Map Object", null for any other shader (it keeps its own material). Loaded once, when first asked; a
    /// variant missing from the build, misnamed or not supported by the GPU is said once on `log` and null from then on.
    /// </summary>
    public static Shader ResolveVariant(bool doodad, Material original, Action<string> log)
    {
        if (original == null || original.shader == null || original.shader.name != (doodad ? DoodadShaderName : WorldModelShaderName))
            return null;
        if (doodad)
        {
            if (!doodadResolved)
            {
                doodadResolved = true;
                doodadVariant = Load(DoodadVariantResource, DoodadShaderName + Suffix, log);
            }
            return doodadVariant;
        }
        if (!worldModelResolved)
        {
            worldModelResolved = true;
            worldModelVariant = Load(WorldModelVariantResource, WorldModelShaderName + Suffix, log);
        }
        return worldModelVariant;
    }

    static Shader Load(string resource, string name, Action<string> log)
    {
        VariantLoads++;
        Shader s = Resources.Load<Shader>(resource);
        string why = s == null ? "missing from the build" : s.name != name ? "named \"" + s.name + "\"" : !s.isSupported ? "not supported by this GPU" : null;
        if (why == null)
            return s;
        if (log != null)
            log("world light: Resources/" + resource + ".shader (\"" + name + "\") is " + why + " -- the renderers it would draw keep their own " +
                "materials (Stage 09's look)");
        return null;
    }

    /// <summary>
    /// A copy of `original` on `variant`: its properties (CopyPropertiesFromMaterial: every property the two shaders share by
    /// name, textures with their scale and offset), its RenderType tag, its render queue and every keyword it has (by name),
    /// named as the original plus " (world)". Touches no tally: whoever keeps it counts it.
    /// </summary>
    public static Material Copy(Material original, Shader variant)
    {
        var m = new Material(variant);
        m.CopyPropertiesFromMaterial(original);
        m.SetOverrideTag("RenderType", original.GetTag("RenderType", false));
        m.renderQueue = original.renderQueue;
        m.name = original.name + Suffix;
        foreach (string k in original.shaderKeywords)
            m.EnableKeyword(k);
        CopiesMade++;
        return m;
    }

    /// <summary>The copy of `original` among a prototype's `copies` (one per distinct original), made now the first time
    /// (counted in `bind`, and returned through `made` for the owner's tallies).</summary>
    public static Material CopyOf(Dictionary<Material, Material> copies, Material original, Shader variant, WmvWorldLitBind bind, out bool made)
    {
        Material c;
        made = false;
        if (copies.TryGetValue(original, out c) && c != null)
            return c;
        c = Copy(original, variant);
        copies[original] = c;
        made = true;
        if (bind != null)
            bind.CopiesMade++;
        return c;
    }

    /// <summary>A material whose _Color is not white: the M2 tint, which Material.SetColor linearises in this linear-space
    /// project while the client uses it as authored. It is the albedo the Models viewer shares, not the light, so it is counted
    /// in the binding line and never changed here.</summary>
    public static bool Tinted(Material m)
    {
        if (m == null || !m.HasProperty(ColorId))
            return false;
        Color c = m.GetColor(ColorId);
        return c.r != 1f || c.g != 1f || c.b != 1f;
    }

    /// <summary>A material's blend class (the binding line): One/Zero opaque, or alpha-key with _ALPHATEST_ON or _AlphaTest;
    /// SrcAlpha/OneMinusSrcAlpha alpha; One/One and SrcAlpha/One additive; DstColor/Zero modulate; DstColor/SrcColor modulate
    /// 2x; anything else other.</summary>
    public static int BlendOf(Material m)
    {
        int src, dst;
        BlendPair(m, out src, out dst);
        bool key = m.IsKeywordEnabled("_ALPHATEST_ON") || (m.HasProperty(AlphaTestId) && m.GetFloat(AlphaTestId) > 0.5f);
        const int One = (int)UnityEngine.Rendering.BlendMode.One, Zero = (int)UnityEngine.Rendering.BlendMode.Zero,
                  SrcAlpha = (int)UnityEngine.Rendering.BlendMode.SrcAlpha, OneMinusSrcAlpha = (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha,
                  DstColor = (int)UnityEngine.Rendering.BlendMode.DstColor, SrcColor = (int)UnityEngine.Rendering.BlendMode.SrcColor;
        if (src == One && dst == Zero)
            return key ? BlendAlphaKey : BlendOpaque;
        if (src == SrcAlpha && dst == OneMinusSrcAlpha)
            return BlendAlpha;
        if (dst == One && (src == One || src == SrcAlpha))
            return BlendAdditive;
        if (src == DstColor && dst == Zero)
            return BlendModulate;
        if (src == DstColor && dst == SrcColor)
            return BlendModulate2x;
        return BlendOther;
    }

    /// <summary>A material's blend pair (UnityEngine.Rendering.BlendMode values): _SrcBlend / _DstBlend, One / Zero without them.</summary>
    static void BlendPair(Material m, out int src, out int dst)
    {
        src = m.HasProperty(SrcBlendId) ? (int)m.GetFloat(SrcBlendId) : (int)UnityEngine.Rendering.BlendMode.One;
        dst = m.HasProperty(DstBlendId) ? (int)m.GetFloat(DstBlendId) : (int)UnityEngine.Rendering.BlendMode.Zero;
    }

    /// <summary>Stage 10 step 7: a copy's fog target (WorldFog.KindOf of its blend class and pair); `unknown` for a blend state
    /// the rule does not know (kind 0, counted).</summary>
    static int FogKindOf(Material copy, out bool unknown)
    {
        int src, dst;
        BlendPair(copy, out src, out dst);
        return WorldFog.KindOf(BlendOf(copy), src, dst, out unknown);
    }

    static int FogCode(bool unfogged, int kind, bool unknown)
    {
        return kind | (unfogged ? FogCodeUnfogged : 0) | (unknown ? FogCodeUnknownBlend : 0);
    }

    /// <summary>
    /// Stage 10 step 7: a copy's fog vector, written once when the copy is made (one SetVector): _WmvFogBatch = (1 for a batch
    /// the client's material flags leave unfogged -- M2 NoFog 0x02, MOMT F_UNFOGGED -- else 0, the fog target kind of the copy's
    /// blend state (WorldFog.KindOf), 0, 0). Returns the slot's fog code (FogCode*) for the binding line.
    /// </summary>
    public static int SetFogBatch(Material copy, bool unfogged)
    {
        bool unknown;
        int kind = FogKindOf(copy, out unknown);
        copy.SetVector(FogBatchId, new Vector4(unfogged ? 1f : 0f, kind, 0f, 0f));
        FogBatchesSet++;
        return FogCode(unfogged, kind, unknown);
    }

    /// <summary>Stage 10 step 7: the fog code of a copy SetFogBatch flagged before (another slot drawing the same original):
    /// its own vector's unfogged flag and its blend state's kind.</summary>
    public static int FogCodeOf(Material copy)
    {
        bool unknown;
        int kind = FogKindOf(copy, out unknown);
        return FogCode(copy.HasProperty(FogBatchId) && copy.GetVector(FogBatchId).x > 0.5f, kind, unknown);
    }

    /// <summary>
    /// The frame's binding (WmvWorldLitDriver; the self-test drives it with managers of its own): each manager is rebound to
    /// `lit` when it is bound otherwise or its last rebind did not finish -- per frame otherwise two bool compares and no
    /// allocation. Returns what the rebinds did (timed), or null when nothing was rebound. Nothing with WMV_LIGHT=off.
    /// </summary>
    public static WmvWorldLitBind BindFrame(WmvWorldModels models, WmvWorldDoodads doodads, bool lit)
    {
        if (WmvWorldLight.HardOff)
            return null;
        bool m = models != null && (lit != models.WorldLitBound || !models.WorldLitSettled);
        bool d = doodads != null && (lit != doodads.WorldLitBound || !doodads.WorldLitSettled);
        if (!m && !d)
            return null;
        var bind = new WmvWorldLitBind { Lit = lit };
        var clock = System.Diagnostics.Stopwatch.StartNew();
        if (m)
            models.RebindWorldLit(lit, bind);
        if (d)
            doodads.RebindWorldLit(lit, bind);
        bind.Ms = clock.Elapsed.TotalMilliseconds;
        return bind;
    }

    /// <summary>The placements both managers bound while the light was shown and have not said yet, as one census (Calls: how
    /// many), each manager's taken (its next placement starts a new one); null when there are none. Allocates nothing.</summary>
    public static WmvWorldLitBind TakePlaced(WmvWorldModels models, WmvWorldDoodads doodads)
    {
        WmvWorldLitBind m = models != null ? models.TakePlacedWhileShown() : null;
        WmvWorldLitBind d = doodads != null ? doodads.TakePlacedWhileShown() : null;
        if (m == null)
            return d;
        m.Add(d);
        return m;
    }

    /// <summary>How many placements both managers have bound while the light was shown and not said yet.</summary>
    public static int PlacedCalls(WmvWorldModels models, WmvWorldDoodads doodads)
    {
        WmvWorldLitBind m = models != null ? models.PlacedWhileShown : null;
        WmvWorldLitBind d = doodads != null ? doodads.PlacedWhileShown : null;
        return (m != null ? m.Calls : 0) + (d != null ? d.Calls : 0);
    }

    /// <summary>Why the world's renderers are not shown the outdoor light for the main camera's frame (the binding line's "not
    /// shown: ..."): no camera -- the light's frame failed ("fault"); the light switched off; outside the terrain view; the
    /// top-down overview (orthographic); no scene light set ("no valid light"); the camera's game position not known
    /// ("camera not placed"); otherwise the eased light is not valid there ("no valid light"). Returns a constant string.</summary>
    public static string WhyNotShown(Camera cam, bool enabled, bool terrainView, WorldLightSet set, bool positioned)
    {
        return cam == null ? "fault" : !enabled ? "light off" : !terrainView ? "not the world view" : cam.orthographic ? "overview"
             : set == null ? "no valid light" : !positioned ? "camera not placed" : "no valid light";
    }
}

/// <summary>
/// The world's binding, frame by frame, and its lines (Stage 10 step 6): WmvMain.WorldLightFrame holds one and calls Frame at
/// each render of the main camera, after WmvWorldLight.Frame and before the sky (and with `lit` false and "fault" after a fault
/// in it); the self-test drives one of its own. In order:
///   - after a bind to the light, the frame after the first one that drew copies made since says its duration, "first lit
///     frame": Direct3D 12 makes the variants' pipeline states at their first draw, which no binding clock sees. With the
///     light on from the start that is the frame of the first placements, so their loading work is in it too;
///   - the bindings made at placement while shown (WmvWorldLitMaterials.TakePlaced) are said once a frame has passed without
///     a new one, or at once when the light is no longer shown (before that rebind's line), marked "(placed while shown)";
///   - the rebind (WmvWorldLitMaterials.BindFrame), said with `why` when it is to the originals; a rebind that touches no
///     renderer says nothing.
/// Per frame otherwise a few compares and no allocation. `say` takes each line.
/// </summary>
public sealed class WmvWorldLitDriver
{
    readonly Action<string> say;
    int placedSeen, placedSeenFrame = -1;   // the placements counted, and the frame that count was first seen
    bool firstDue;                          // bound to the light; the first frame drawing copies made since is not owed yet
    int copiesAt;                           // WmvWorldLitMaterials.CopiesMade before that bind
    int firstOwed = -1;                     // the frame whose duration the "first lit frame" line says (-1: none)

    public WmvWorldLitDriver(Action<string> say)
    {
        this.say = say;
    }

    /// <summary>One render of the main camera (`frame`: Time.frameCount; `lastFrameMs`: the duration of the frame before it,
    /// Time.unscaledDeltaTime): the lines owed, then the managers bound to `lit` (`why` it is not shown, when not). Returns
    /// what a rebind did, or null when none was due.</summary>
    public WmvWorldLitBind Frame(WmvWorldModels models, WmvWorldDoodads doodads, bool lit, string why, int frame, double lastFrameMs)
    {
        if (firstOwed >= 0 && frame > firstOwed)
        {
            Say(string.Format(CultureInfo.InvariantCulture, "world light: first lit frame {0:0.0} ms (frame {1}, the first to draw copies made " +
                              "since the light was shown, with that frame's other work)", lastFrameMs, firstOwed));
            firstOwed = -1;
        }
        int placed = WmvWorldLitMaterials.PlacedCalls(models, doodads);
        if (placed > 0 && (!lit || (placed == placedSeen && frame > placedSeenFrame)))
        {
            WmvWorldLitBind p = WmvWorldLitMaterials.TakePlaced(models, doodads);
            if (p != null && p.Touched)
                Say(p.Describe(null));
            placed = 0;
        }
        if (placed != placedSeen)
        {
            placedSeen = placed;
            placedSeenFrame = frame;
        }
        WmvWorldLitBind b = WmvWorldLitMaterials.BindFrame(models, doodads, lit);
        if (b != null)
        {
            firstDue = lit;
            copiesAt = WmvWorldLitMaterials.CopiesMade - b.CopiesMade;
            if (b.Touched)
                Say(b.Describe(lit ? null : why));
        }
        if (firstDue && lit && WmvWorldLitMaterials.CopiesMade != copiesAt)
        {
            firstDue = false;
            firstOwed = frame;
        }
        return b;
    }

    void Say(string line)
    {
        if (say != null)
            say(line);
    }
}
