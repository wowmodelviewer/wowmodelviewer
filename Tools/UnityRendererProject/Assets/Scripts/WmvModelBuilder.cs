// WmvModelBuilder.cs
//
// Turns the parsed M2 + skin + decoded textures into a live Unity object: one Mesh with a
// submesh per WoW batch, one Material each, textures uploaded from decoded RGBA bytes.
//
// Everything it creates is tracked so a later load can destroy it: meshes, materials and
// textures are not garbage-collected by Unity on their own, so a viewer that loads model after
// model would leak GPU memory without the explicit Dispose here.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

/// <summary>
/// Which textures one material was built from. Kept so the skin can be changed later without
/// rebuilding the mesh: the WMV viewport lets the user pick among a creature's skins (chicken2
/// has seven), and when they do, only these bindings need re-uploading.
/// </summary>
public struct WmvMaterialBinding
{
    public int BaseSlot;         // M2 texture slot behind the main texture, -1 when untextured
    public int EnvSlot;          // M2 texture slot bound as the combiner's second unit, -1 unused
    public bool DropAlpha;       // was the base texture's alpha discarded on upload?
    public bool Unit1DropAlpha;  // ... and the second unit's

    /// <summary>
    /// How each unit's texture addresses outside 0..1, per axis: true repeats, false clamps.
    ///
    /// It has to live here, not just at build time, because a skin change re-uploads the images
    /// through RebindTextures and would otherwise hand every texture the default. That is exactly
    /// how a wrap fix looks intermittent: right on load, wrong the moment the user picks a skin.
    /// </summary>
    public bool BaseWrapX, BaseWrapY;
    public bool Unit1WrapX, Unit1WrapY;

    /// <summary>M2 texture slot bound as the combiner's THIRD unit, or -1. Its alpha is the
    /// luminous mask, so it is never dropped; it keeps its own address mode like the others.</summary>
    public int ThirdSlot;
    public bool Unit2WrapX, Unit2WrapY;
}

/// <summary>
/// The animated inputs of ONE material, resolved once at build time from the batch's lookups,
/// exactly the way the legacy viewport assigns them to a pass (Source/games/wow/WoWModel.cpp:
/// 1805 colour, 1807 opacity, 1848-1866 the texture transforms, gated by TEXTUREUNIT_STATIC).
/// Indices into the parsed model's arrays, -1 for "none"; the animator evaluates them per frame.
/// </summary>
public struct WmvMaterialAnimBinding
{
    public int Material;          // index into WmvRuntimeModel.Materials, and the submesh index
    public int Submesh;           // the skin's submesh number, for the log
    public int Color;             // model.Colors, or -1
    public int Weight;            // model.TextureWeightTracks, or -1 -- unit 0, the pass opacity
    public int Weight1;           // ... unit 1's own weight track, or -1 -- the ps20/23 lobe's gain
    public int Weight2;           // ... unit 2's own weight track, or -1 -- the third unit's gain
    public int Transform2;        // ... unit 2
    public int Transform0;        // model.TextureTransforms for unit 0, or -1 (STATIC, env, none)
    public int Transform1;        // ... unit 1
    public bool Static;           // batch flag 0x10: the transforms above are forced to -1
    public bool Unit0Env, Unit1Env;
    public bool Opaque;           // M2 blend mode 0: an UNLIT one blends IN PLACE when its alpha drops below 1
    public bool Unlit;            // material flag 0x01: tint and animated opacity apply to THESE only
    public bool AlphaKey;         // M2 blend mode 1: alpha never reaches the frame, only the gate
    public float BaseAlphaScale;  // the combiner's own alpha scale; the animated weight multiplies it
}

/// <summary>
/// One drawn batch's place in the transparent draw order. The legacy viewport sorts its passes
/// before drawing -- blend mode first, then geoset index, then the texture's "special" type
/// (Source/games/wow/WoWModel.cpp:2011-2018) -- and this renderer drew them in skin order, which
/// is a different order whenever a model mixes blend modes across submeshes.
/// </summary>
struct WmvDrawOrderKey
{
    public int Material;     // index into the materials list
    public int Blend;        // M2 blend mode, the legacy's primary key
    public int Submesh;      // the legacy's secondary key
    public int SpecialTex;   // the legacy's tertiary key: texture type, -1 for a plain file
    public int Built;        // the order this batch was built in: the final tiebreaker
}

public class WmvRuntimeModel
{
    public GameObject Root;
    public Mesh Mesh;
    public Material[] Materials = new Material[0];
    public Texture2D[] Textures = new Texture2D[0];
    public WmvMaterialBinding[] Bindings = new WmvMaterialBinding[0];

    /// <summary>Geoset number of each submesh, parallel to Materials. 0 = always drawn.</summary>
    public int[] SubmeshGeosets = new int[0];
    /// <summary>Each built entry's skin submesh index -- what the batch log calls "submesh N".</summary>
    public int[] SubmeshIndices = new int[0];

    /// <summary>Every submesh's triangles, kept so hiding one is a matter of handing the mesh an
    /// empty list and showing it again is handing back this array -- no geometry is re-uploaded
    /// and no material is rebuilt.</summary>
    public int[][] SubmeshTriangles = new int[0][];

    /// <summary>
    /// The bone transforms behind the SkinnedMeshRenderer, in the model's own bone order, or an
    /// empty array when this model is drawn unskinned. They are children of Root, so Dispose
    /// takes them with it.
    /// </summary>
    public Transform[] Bones = new Transform[0];

    /// <summary>
    /// Each bone's rest offset from its parent, parallel to Bones. The animator adds a
    /// translation track to these, so switching to another animation needs them again -- and
    /// re-deriving them from the pivots would be a second copy of the same arithmetic.
    /// </summary>
    public Vector3[] BoneRestPositions = new Vector3[0];

    /// <summary>True when the model is drawn through a SkinnedMeshRenderer.</summary>
    public bool Skinned;

    /// <summary>The animator driving the bones, or null when nothing is playing.</summary>
    public WmvM2Animator Animator;

    /// <summary>The component evaluating the material tracks, or null when no batch has any.</summary>
    public WmvMaterialAnimator MaterialAnimator;

    /// <summary>
    /// The model's particle and ribbon emitters, or null when it declares none this renderer can
    /// draw. Its renderers are children of Root, so Dispose takes them with it, and its own
    /// OnDestroy releases the meshes and materials it made.
    /// </summary>
    public WmvEmitterRuntime Emitters;

    /// <summary>One entry per material/submesh: which tracks feed it.</summary>
    public WmvMaterialAnimBinding[] MaterialAnim = new WmvMaterialAnimBinding[0];

    /// <summary>
    /// Per submesh: hidden right now by its material's visibility gate (the legacy per-frame
    /// test at ModelRenderPass.cpp:468). Consulted by the geoset switch so a geoset that is on
    /// cannot un-hide a batch whose opacity is zero at this instant.
    /// </summary>
    public bool[] GateHidden = new bool[0];

    /// <summary>The renderer, kept so an animation change can adjust its culling.</summary>
    public SkinnedMeshRenderer Skin;

    /// <summary>The geoset numbers currently switched on, or null when the host has not said.</summary>
    public HashSet<int> Geosets;
    public Bounds Bounds;
    public int VertexCount, TriangleCount, SubmeshCount;

    /// <summary>Destroy every runtime resource this model owns.</summary>
    public void Dispose()
    {
        if (Root != null) UnityEngine.Object.Destroy(Root);
        if (Mesh != null) UnityEngine.Object.Destroy(Mesh);
        foreach (var m in Materials) if (m != null) UnityEngine.Object.Destroy(m);
        foreach (var t in Textures) if (t != null) UnityEngine.Object.Destroy(t);
        Root = null;
        Mesh = null;
        Emitters = null;
        Bones = new Transform[0];
        BoneRestPositions = new Vector3[0];
        Animator = null;
        Skin = null;
        Materials = new Material[0];
        Textures = new Texture2D[0];
    }
}

public static class WmvModelBuilder
{
    /// <summary>
    /// Diagnostic switches. They exist to isolate a visual fault without rebuilding: pass the
    /// flag and the rendering changes accordingly.
    ///
    /// Each is read from the player's own command line AND from the WMV_DEBUG environment
    /// variable (space-separated, same spellings). The environment matters because WMV builds the
    /// player's command line itself when it launches it into the viewport pane -- without it these
    /// switches would only reach a player started by hand, which is not the case anyone wants to
    /// diagnose. A child process inherits the environment, so setting WMV_DEBUG for WMV sets it
    /// for the player.
    ///
    ///   -wmvFlipV        invert the V texture coordinate (isolates a UV-orientation fault)
    ///   -wmvForceOpaque  force every material opaque, ignoring the WoW blend mode
    ///   -wmvForceSolid   the whole force-solid probe: force-opaque PLUS a fully opaque alpha
    ///                    channel on every uploaded texture, so neither the material state nor
    ///                    the texture can make anything translucent. If the model is STILL
    ///                    see-through with this on, the fault is geometry, depth or winding --
    ///                    not alpha.
    ///   -wmvMatColors    replace textures with a flat per-material colour (isolates a
    ///                    batch/material assignment fault from a texture fault)
    ///   -wmvShowHidden   draw the batches the model hides at rest (see BatchIsVisible). Useful
    ///                    for seeing WHAT is hidden; the hidden geometry usually covers the
    ///                    detail it is meant to replace.
    ///   -wmvOnlySubmesh=i:j:k
    ///                    draw ONLY these skin submeshes (the "submesh N" of the batch log), and
    ///                    withhold every other one's triangles. This is how a visible feature --
    ///                    an eye, a crack, a trailing flame -- is ATTRIBUTED to a batch rather
    ///                    than guessed at from a material name: render the whole model, then
    ///                    render one submesh, and see which part of the picture went with it.
    ///                    While it is passed it REPLACES the geoset rule, so a submesh the
    ///                    displayed variant does not switch on can still be looked at -- which is
    ///                    the whole point of being able to isolate one. It does not override the
    ///                    material visibility gate; a batch the model keys to invisible stays
    ///                    invisible (use -wmvShowHidden for that, they compose).
    ///                    Colons, because WMV_DEBUG is itself split on commas.
    ///   -wmvSkinTexture=slot:fileDataID[:slot:fileDataID...]
    ///                    OVERRIDE the file a texture slot loads. A model with several skin
    ///                    variants otherwise renders whichever one the app happened to choose,
    ///                    which is not something a controlled comparison can name; with this, the
    ///                    variant IS the identifier.
    ///                    IT IS A PIN, NOT AN INJECTOR. It rewrites the FileDataID of a slot the
    ///                    host already offered a texture for, at both the initial resolve and a
    ///                    later skin change. Naming a slot the host offered nothing for -- an
    ///                    unresolved replaceable slot, say -- does NOTHING: no request is made and
    ///                    the slot stays empty, because the request loop is driven by the host's
    ///                    list, not by this switch. Feeding such a slot needs a deliberate change
    ///                    to how slots are requested, which this is not.
    ///   -wmvOwnShader    resolve the renderer's own WmvOpaque shader before any pipeline
    ///                    shader. The pipeline's Lit shaders cannot run the M2 combiner, so this
    ///                    is how to see the second texture unit in a build where they exist.
    ///   -wmvNoSkin       build every model as a static mesh, as before skinning existed. The
    ///                    A/B for "did the skinned path change what I see?".
    ///   -wmvSkinCheck    after building a skinned model, bake the skinned result and report the
    ///                    largest distance between a baked vertex and the position the file gave
    ///                    it. At rest that distance is the whole claim of the skinning milestone,
    ///                    so it is worth being able to measure rather than assert. Only meaningful
    ///                    together with -wmvNoAnim: a moving model is not in its rest pose.
    ///   -wmvNoEmitters   draw no particles and no ribbons: the control for every emitter
    ///                    measurement, and the BEFORE of a before/after pair.
    ///   -wmvNoAnim       do not play anything. The model is still skinned, and still sits in the
    ///                    rest pose the bind poses describe, which is what the milestone before
    ///                    this one shipped.
    ///   -wmvAnimCheck    sample the idle across its length and report how far the skinned mesh
    ///                    moves away from its rest pose at each step, next to the model's own
    ///                    size. Zero everywhere means nothing is animating; a number far larger
    ///                    than the model means the rig is being applied wrongly. Both are
    ///                    invisible in a still frame and obvious in this line.
    ///   -wmvQueueProof   render a controlled two-submesh case offscreen and report which of the
    ///                    two materials reached the frame buffer last. The transparent draw order
    ///                    rests on one engine behaviour -- that a per-material render queue orders
    ///                    the submeshes of a SINGLE renderer -- and reading the render loop's
    ///                    sorting rules is not the same as measuring it. See
    ///                    WmvMain.ReportQueueOrder.
    /// </summary>
    public static class Debug_
    {
        static bool parsed;
        static bool flipV, forceOpaque, forceSolid, matColors, showHidden, ownShader;
        static bool noSkin, skinCheck, noAnim, animCheck, placeholder, overlay, litShader;
        static bool noEmitters;
        static bool lightCheck;
        static bool queueProof;
        static float animTime = -1f;
        static bool frameBoundsSet;
        static int[] seqPath = new int[0];
        static bool lifecycleTest;
        static Bounds frameBounds;
        static bool matDump;
        static bool allocCheck;
        static int[] onlySubmeshes;                      // -wmvOnlySubmesh: null = draw them all
        static int[] pinnedTextures = new int[0];        // -wmvSkinTexture: slot, file, slot, file
        static int rig;
        static bool lightDump;
        static float lightYaw = 30f;
        static float lightPitch = 15f;

        static void Parse()
        {
            if (parsed) return;
            parsed = true;
            var args = new List<string>(System.Environment.GetCommandLineArgs());
            string fromEnv = System.Environment.GetEnvironmentVariable("WMV_DEBUG");
            if (!string.IsNullOrEmpty(fromEnv))
                args.AddRange(fromEnv.Split(new[] { ' ', '\t', ',', ';' },
                                            StringSplitOptions.RemoveEmptyEntries));
            foreach (var a in args)
            {
                if (a == "-wmvFlipV") flipV = true;
                else if (a == "-wmvForceOpaque") forceOpaque = true;
                else if (a == "-wmvForceSolid") forceSolid = true;
                else if (a == "-wmvMatColors") matColors = true;
                else if (a == "-wmvShowHidden") showHidden = true;
                else if (a == "-wmvOwnShader") ownShader = true;   // kept: now the default
                else if (a == "-wmvLitShader") litShader = true;
                else if (a == "-wmvLightCheck") lightCheck = true;
                else if (a == "-wmvLightDump") lightDump = true;
                else if (a == "-wmvQueueProof") queueProof = true;
                else if (a == "-wmvMatDump") matDump = true;
                else if (a == "-wmvAllocCheck") allocCheck = true;
                else if (a == "-wmvLifecycleTest") lifecycleTest = true;
                else if (a.StartsWith("-wmvSeqPath="))
                {
                    // a:b:c -- sequences to switch through, in order, before the light check
                    string[] p = a.Substring("-wmvSeqPath=".Length).Split(':');
                    var path = new List<int>();
                    for (int i = 0; i < p.Length; i++)
                    {
                        int v;
                        if (int.TryParse(p[i], out v) && v >= 0) path.Add(v);
                    }
                    seqPath = path.ToArray();
                }
                else if (a.StartsWith("-wmvOnlySubmesh="))
                {
                    // i:j:k -- the only skin submeshes to draw. An empty or unparsable list draws
                    // nothing, which is a legitimate control (the background alone).
                    string[] p = a.Substring("-wmvOnlySubmesh=".Length).Split(':');
                    var keep = new List<int>();
                    for (int i = 0; i < p.Length; i++)
                    {
                        int v;
                        if (int.TryParse(p[i], out v) && v >= 0) keep.Add(v);
                    }
                    onlySubmeshes = keep.ToArray();
                }
                else if (a.StartsWith("-wmvSkinTexture="))
                {
                    // slot:fileDataID pairs. A slot named twice keeps the first pin.
                    string[] p = a.Substring("-wmvSkinTexture=".Length).Split(':');
                    var pin = new List<int>();
                    for (int i = 0; i + 1 < p.Length; i += 2)
                    {
                        int s, f;
                        if (int.TryParse(p[i], out s) && int.TryParse(p[i + 1], out f) &&
                            s >= 0 && f > 0)
                        { pin.Add(s); pin.Add(f); }
                    }
                    pinnedTextures = pin.ToArray();
                }
                else if (a.StartsWith("-wmvFrameBounds="))
                {
                    // cx:cy:cz:ex:ey:ez -- the light check frames from THESE bounds instead of the
                    // model's own, so two builds whose bounds differ can still be compared.
                    string[] p = a.Substring("-wmvFrameBounds=".Length).Split(':');   // colons: WMV_DEBUG itself is split on commas
                    float[] v = new float[6];
                    bool ok = p.Length == 6;
                    for (int i = 0; ok && i < 6; i++)
                        ok = float.TryParse(p[i], System.Globalization.NumberStyles.Float,
                                            System.Globalization.CultureInfo.InvariantCulture, out v[i]);
                    if (ok)
                    {
                        frameBounds = new Bounds(new Vector3(v[0], v[1], v[2]),
                                                 new Vector3(2f * v[3], 2f * v[4], 2f * v[5]));
                        frameBoundsSet = true;
                    }
                }
                else if (a.StartsWith("-wmvAnimTime="))
                {
                    float ms;
                    if (float.TryParse(a.Substring("-wmvAnimTime=".Length),
                                       System.Globalization.NumberStyles.Float,
                                       System.Globalization.CultureInfo.InvariantCulture, out ms))
                        animTime = ms;
                }
                else if (a.StartsWith("-wmvLightYaw="))
                {
                    float y;
                    if (float.TryParse(a.Substring("-wmvLightYaw=".Length),
                                       System.Globalization.NumberStyles.Float,
                                       System.Globalization.CultureInfo.InvariantCulture, out y))
                        lightYaw = y;
                }
                else if (a.StartsWith("-wmvLightPitch="))
                {
                    float pv;
                    if (float.TryParse(a.Substring("-wmvLightPitch=".Length),
                                       System.Globalization.NumberStyles.Float,
                                       System.Globalization.CultureInfo.InvariantCulture, out pv))
                        lightPitch = pv;
                }
                else if (a.StartsWith("-wmvRig="))
                {
                    int r;
                    if (int.TryParse(a.Substring("-wmvRig=".Length), out r)) rig = r;
                }
                else if (a == "-wmvNoSkin") noSkin = true;
                else if (a == "-wmvSkinCheck") skinCheck = true;
                else if (a == "-wmvNoAnim") noAnim = true;
                else if (a == "-wmvNoEmitters") noEmitters = true;
                else if (a == "-wmvAnimCheck") animCheck = true;
                else if (a == "-wmvPlaceholder") placeholder = true;
                else if (a == "-wmvOverlay") overlay = true;
            }
            if (flipV || forceOpaque || forceSolid || matColors || showHidden || ownShader ||
                noSkin || skinCheck || noAnim || animCheck || noEmitters)
                Debug.Log("WMV debug switches: flipV=" + flipV + " forceOpaque=" + forceOpaque +
                          " forceSolid=" + forceSolid + " matColors=" + matColors +
                          " showHidden=" + showHidden + " ownShader=" + ownShader +
                          " noSkin=" + noSkin + " skinCheck=" + skinCheck + " noAnim=" + noAnim +
                          " animCheck=" + animCheck);
        }

        /// <summary>The submeshes -wmvOnlySubmesh named, or null when it was not passed.</summary>
        public static int[] OnlySubmeshes { get { Parse(); return onlySubmeshes; } }

        /// <summary>
        /// Is this skin submesh drawn? True for everything unless -wmvOnlySubmesh was passed, in
        /// which case only the submeshes it named are. Indexed by the batch's SubmeshIndex -- the
        /// number the batch log prints -- not by the order batches happened to be built in.
        /// </summary>
        public static bool SubmeshAllowed(int submeshIndex)
        {
            Parse();
            if (onlySubmeshes == null) return true;
            for (int i = 0; i < onlySubmeshes.Length; i++)
                if (onlySubmeshes[i] == submeshIndex) return true;
            return false;
        }

        /// <summary>The file -wmvSkinTexture pinned to this slot, or 0 when it named no such slot.</summary>
        public static int PinnedTexture(int slot)
        {
            Parse();
            for (int i = 0; i + 1 < pinnedTextures.Length; i += 2)
                if (pinnedTextures[i] == slot) return pinnedTextures[i + 1];
            return 0;
        }

        public static bool FlipV { get { Parse(); return flipV; } }
        public static bool ForceOpaque { get { Parse(); return forceOpaque || forceSolid; } }
        public static bool ForceSolid { get { Parse(); return forceSolid; } }
        public static bool MatColors { get { Parse(); return matColors; } }
        public static bool ShowHidden { get { Parse(); return showHidden; } }
        public static bool OwnShader { get { Parse(); return ownShader; } }

        /// <summary>
        /// Use the pipeline's Lit shader instead of the viewer's own (-wmvLitShader).
        ///
        /// For comparing against physically-based shading, and nothing else: it cannot run the M2
        /// combiners, so every material that needs one renders as its base texture alone.
        /// </summary>
        public static bool LitShader { get { Parse(); return litShader; } }

        /// <summary>
        /// Render the loaded model offscreen and report what the lighting actually did
        /// (-wmvLightCheck): how bright it came out, how much of it clipped, how dark its darkest
        /// parts are. "Looks right" is not checkable from a log; those numbers are.
        /// </summary>
        public static bool LightCheck { get { Parse(); return lightCheck; } }

        /// <summary>
        /// Measure whether a per-material render queue orders the submeshes of one renderer
        /// (-wmvQueueProof).
        ///
        /// The transparent draw order this builder applies is a claim about the engine, not about
        /// WoW: the model is ONE renderer, so ranking its blended batches means giving each
        /// batch's material its own queue value and trusting Unity to draw them in that sequence
        /// rather than in submesh order. That trust is checkable, and a claim that can be measured
        /// should not be inferred. The check itself lives in WmvMain.ReportQueueOrder.
        /// </summary>
        public static bool QueueProof { get { Parse(); return queueProof; } }

        /// <summary>
        /// Hold the animation -- bones, materials and the global-sequence clock alike -- at this
        /// many milliseconds and never advance it (-wmvAnimTime=N). Negative means "not set". This
        /// is what makes a before/after pair at "t = 250 ms" the same instant on both builds.
        /// </summary>
        public static float AnimTime { get { Parse(); return animTime; } }

        /// <summary>
        /// Log every evaluated material value -- the keys, the time, the interpolated result, the
        /// final alpha and the gate -- whenever the materials are posed (-wmvMatDump). A picture
        /// says a glow faded; this says by how much and from which keys.
        /// </summary>
        public static bool MatDump { get { Parse(); return matDump; } }

        /// <summary>
        /// Measure managed allocation per frame while the animators run (-wmvAllocCheck): the
        /// material animator adds per-frame work, and "it is cheap" is a number or it is nothing.
        /// </summary>
        public static bool AllocCheck { get { Parse(); return allocCheck; } }

        /// <summary>
        /// Frame the light check from given bounds (-wmvFrameBounds=cx:cy:cz:ex:ey:ez) rather than
        /// from the model's own. Two builds that disagree about the bounds -- one keeps batches the
        /// other dropped -- would otherwise put the camera at two distances and compare two
        /// different pictures.
        /// </summary>
        public static bool HasFrameBounds { get { Parse(); return frameBoundsSet; } }
        public static Bounds FrameBounds { get { Parse(); return frameBounds; } }

        /// <summary>
        /// Switch through these sequences, in order, once the model is built and before anything
        /// measures it (-wmvSeqPath=a:b:c, in-file sequences). A frame taken after "0:5" and one
        /// taken after "5" must be the same frame: that is the sequence-change proof.
        /// </summary>
        public static int[] SeqPath { get { Parse(); return seqPath; } }

        /// <summary>
        /// Run WmvLifecycleSelfTest at start-up (-wmvLifecycleTest): the material animator's
        /// dormant / active lifecycle across sequence changes, on synthetic models, through the
        /// real build and switch paths. Result lines are logged for the regression to read.
        /// </summary>
        public static bool LifecycleTest { get { Parse(); return lifecycleTest; } }

        /// <summary>
        /// Which preview light rig the viewport draws with (-wmvRig=N), for a visual A/B:
        /// 0 shipped, 1 legacy (what WMV drew with before this work). Defaults to 0.
        /// The numeric comparison is -wmvLightCheck, which measures both in one run.
        /// </summary>
        public static int Rig { get { Parse(); return rig; } }

        /// <summary>
        /// Save the light check's frames as PNGs next to the player (-wmvLightDump), so a
        /// headless run can be inspected by eye. Some lighting questions -- WHERE a shadow
        /// falls, whether darkening is a shadow or acne speckle -- are spatial, and no summary
        /// statistic answers them faster than the picture does.
        /// </summary>
        public static bool LightDump { get { Parse(); return lightDump; } }

        /// <summary>
        /// The light check camera's yaw (-wmvLightYaw=N, default 30). Models face where their
        /// author pointed them, and a hood's shadow on a face can only be inspected from the
        /// front -- the default three-quarter view happens to be this model's back.
        /// </summary>
        public static float LightYaw { get { Parse(); return lightYaw; } }

        /// <summary>
        /// The light check camera's pitch (-wmvLightPitch=N, default 15; negative looks UP from
        /// below). Pitch is the axis that separates a camera-relative light from a
        /// world-anchored one -- under yaw the two are indistinguishable when the light is
        /// near-vertical -- so the world-anchor work is verified from down here.
        /// </summary>
        public static float LightPitch { get { Parse(); return lightPitch; } }
        public static bool NoSkin { get { Parse(); return noSkin; } }
        public static bool SkinCheck { get { Parse(); return skinCheck; } }
        public static bool NoAnim { get { Parse(); return noAnim; } }

        /// <summary>
        /// Draw no particles and no ribbons (-wmvNoEmitters).
        ///
        /// The control for every emitter measurement: the same model, the same camera, the same
        /// instant, with the emitters the only difference. It is also the BEFORE of a before/after
        /// pair without needing to check out the previous commit.
        /// </summary>
        public static bool NoEmitters { get { Parse(); return noEmitters; } }
        public static bool AnimCheck { get { Parse(); return animCheck; } }

        /// <summary>
        /// Draw the spinning proof-of-life cube while no model is loaded (-wmvPlaceholder).
        ///
        /// It existed to show that the embedded player was alive at all, back when that was the
        /// open question. It is off by default now: this viewport is what the user looks at, and
        /// a rotating grey box is a test object, not an empty viewer.
        /// </summary>
        public static bool Placeholder { get { Parse(); return placeholder; } }

        /// <summary>
        /// Draw the on-screen status lines (-wmvOverlay).
        ///
        /// They narrate what the renderer is doing -- connecting, loading, which animation --
        /// which is exactly right while bringing it up and exactly wrong in a viewer, where the
        /// first thing the user sees should be the model and not a commentary on it. The lines
        /// are still WRITTEN TO THE LOG either way, so nothing diagnostic is lost by not drawing
        /// them; this only decides whether they are painted over the viewport.
        /// </summary>
        public static bool Overlay { get { Parse(); return overlay; } }
    }

    static readonly Color[] DebugColors =
    {
        Color.red, Color.green, Color.blue, Color.yellow, Color.cyan, Color.magenta,
    };

    /// <summary>
    /// Build a renderable object. decodedTextures maps an M2 texture-slot index to its decoded
    /// pixels; a slot with no entry renders untextured (white), which is what happens for a
    /// replaceable texture the host could not resolve.
    /// </summary>
    /// <summary>
    /// Is this submesh drawn, given the geoset numbers the displayed variant switches on?
    ///
    /// This is the legacy viewport's rule, from WoWModel::setCreatureGeosetData: a submesh whose
    /// geoset number is 0 is always drawn, and every other one is drawn only if the variant names
    /// it. An EMPTY set is an answer, not a blank -- it hides everything but the id-0 submeshes,
    /// which is exactly what the viewport does for a display with no geoset data. A null set means
    /// the host had nothing to say (no creature selection at all), and then nothing is hidden.
    /// </summary>
    /// <summary>
    /// Is this submesh's geoset switched on?
    ///
    /// Geoset 0 is the always-drawn part of a model -- the body under the options -- and every
    /// other number is a variant a creature display turns on. Three cases, and the middle one is
    /// the one that used to be wrong:
    ///
    ///   * a set was reported: draw geoset 0 and whatever the set names;
    ///   * NO set was reported (null): draw geoset 0 alone. This is the legacy viewport's default
    ///     -- it builds every submesh with display = (id == 0)
    ///     (Source/games/wow/WoWModel.cpp:1607) and leaves it that way when nothing selects
    ///     otherwise (Source/games/wow/WoWModel.cpp:2845-2847). Drawing everything instead, which
    ///     is what this returned before, shows a model wearing all of its mutually exclusive
    ///     variants at once: every helmet, every fur option, every alternative limb, overlapping.
    ///   * an EMPTY set was reported: also geoset 0 alone, which the previous code already got
    ///     right -- an empty set is a known selection that switches nothing on, and is not the
    ///     same as silence.
    /// </summary>
    /// <summary>
    /// Is the built entry for this skin submesh drawn? The geoset rule below, unless
    /// -wmvOnlySubmesh named an explicit set, which replaces it (see the switch documentation).
    /// </summary>
    public static bool SubmeshDrawn(int submeshIndex, int geosetId, HashSet<int> geosets)
    {
        if (Debug_.OnlySubmeshes != null)
            return Debug_.SubmeshAllowed(submeshIndex);
        return GeosetVisible(geosetId, geosets);
    }

    public static bool GeosetVisible(int submeshId, HashSet<int> geosets)
    {
        if (submeshId == 0)
            return true;
        if (geosets == null)
            return false;
        return geosets.Contains(submeshId);
    }

    /// <summary>An upper bound on the rig this renderer will build, purely so a corrupt bone
    /// count cannot make the player allocate a GameObject per garbage entry. Real creature rigs
    /// are two orders of magnitude below it.</summary>
    const int MaxBones = 2048;

    /// <summary>Whether this model can be skinned, and if not, why not -- in words meant for the
    /// log, because an unexplained fallback to a static mesh is indistinguishable from a bug.</summary>
    struct SkinPlan
    {
        public bool CanSkin;
        public string Reason;
    }

    /// <summary>
    /// Decide whether to build a skinned mesh for this model.
    ///
    /// The bar is deliberately high: the milestone's contract is that a skinned model in its rest
    /// pose is INDISTINGUISHABLE from the static one, so anything that would make the two diverge
    /// is a reason to stay static and say so rather than to approximate.
    /// </summary>
    static SkinPlan PlanSkinning(M2ParsedModel model)
    {
        SkinPlan p;
        p.CanSkin = false;
        if (Debug_.NoSkin)
        {
            p.Reason = "-wmvNoSkin was passed";
            return p;
        }
        if (model.SkeletonFileDataID != 0)
        {
            // The bones are in a .skel named by the SKID chunk (and possibly in ITS parent, via
            // SKPD). Fetching that is another asset round-trip and another chunked format; over a
            // spread of 300 retail creature models exactly one needed it, so this milestone draws
            // those unskinned rather than guessing at the header's unused bone array.
            p.Reason = "its bones live in a separate skeleton file (SKID " +
                       model.SkeletonFileDataID + "), which this milestone does not fetch";
            return p;
        }
        if (model.Bones.Length == 0)
        {
            p.Reason = model.BoneCount > 0
                ? "its " + model.BoneCount + " bone(s) could not be read"
                : "it declares no bones";
            return p;
        }
        if (model.Bones.Length > MaxBones)
        {
            p.Reason = "it declares " + model.Bones.Length + " bones, past the " + MaxBones +
                       " this renderer will build";
            return p;
        }
        p.CanSkin = true;
        p.Reason = null;
        return p;
    }

    /// <summary>
    /// Build the Unity skeleton for a model, in the M2's own bone order.
    ///
    /// THE WHOLE THING RESTS ON ONE PROPERTY OF THE FORMAT. The legacy viewport composes a bone's
    /// matrix as
    ///
    ///     local = T(pivot) * T(translation) * R(rotation) * S(scale) * T(-pivot)
    ///     world = parent.world * local
    ///
    /// (Bone::calcMatrix), and it skins a vertex as the weighted sum of world * position over its
    /// four influences. With every track at rest that local matrix collapses to the IDENTITY, so
    /// the positions stored in the file ARE the rest pose. A bind pose therefore only has to
    /// reproduce the identity -- which is why this places each bone at its pivot with no rotation
    /// and no scale, and takes the bind poses from those same transforms.
    ///
    /// It is also exactly the arrangement animation needs later. Unity composes a bone as
    /// parent.world * T(localPosition) * R(localRotation) * S(localScale); putting the rest
    /// localPosition at (pivot - parentPivot) means that adding the M2's translation track to it,
    /// and setting the rotation and scale from their tracks, reproduces the expression above term
    /// for term -- the pivot translations telescope through the parent chain.
    /// </summary>
    static Transform[] BuildSkeleton(M2ParsedModel model, Transform root, out Transform rootBone,
                                     out Vector3[] restLocalPositions)
    {
        int nb = model.Bones.Length;
        var pivots = new Vector3[nb];
        for (int i = 0; i < nb; i++)
        {
            float x, y, z;
            WowCoordinateConverter.ConvertPosition(model.Bones[i].Pivot, out x, out y, out z);
            pivots[i] = new Vector3(x, y, z);
        }

        var bones = new Transform[nb];
        for (int i = 0; i < nb; i++)
            bones[i] = new GameObject("bone" + i).transform;

        // Parent first, place second: a bone's local offset is its pivot relative to its parent's,
        // and because every rest rotation is the identity that offset does not depend on the order
        // the transforms are visited in.
        rootBone = null;
        for (int i = 0; i < nb; i++)
        {
            int parent = model.Bones[i].Parent;
            bones[i].SetParent(parent >= 0 ? bones[parent] : root, false);
            if (parent < 0 && rootBone == null)
                rootBone = bones[i];
        }
        restLocalPositions = new Vector3[nb];
        for (int i = 0; i < nb; i++)
        {
            int parent = model.Bones[i].Parent;
            restLocalPositions[i] = parent >= 0 ? pivots[i] - pivots[parent] : pivots[i];
            bones[i].localPosition = restLocalPositions[i];
            bones[i].localRotation = Quaternion.identity;
            bones[i].localScale = Vector3.one;
        }
        if (rootBone == null)
            rootBone = root;
        return bones;
    }

    /// <summary>
    /// Per-vertex influences, converted from the M2's four (bone index, weight/255) pairs. The
    /// indices are direct indices into the bone array -- there is no lookup table in between,
    /// which is worth stating because the .skin format has one for a different purpose.
    ///
    /// Two corrections are applied, and both exist to keep the rest pose identical to the static
    /// mesh. An influence pointing past the end of the bone array is dropped -- the legacy
    /// viewport drops it too -- but the remaining weights are then renormalised rather than left
    /// short, because a vertex whose weights no longer sum to one is dragged toward the origin.
    /// A vertex left with no influence at all is bound to bone 0 at full weight, which at rest is
    /// the identity and so leaves it exactly where the file put it.
    /// </summary>
    static BoneWeight[] BuildBoneWeights(M2ParsedModel model, int boneCount,
                                         out int droppedInfluences, out int unweightedVertices)
    {
        droppedInfluences = 0;
        unweightedVertices = 0;
        int n = model.Vertices.Length;
        var weights = new BoneWeight[n];
        var idx = new int[4];
        var w = new float[4];

        for (int i = 0; i < n; i++)
        {
            M2Vertex v = model.Vertices[i];
            idx[0] = v.BoneIndex0; idx[1] = v.BoneIndex1; idx[2] = v.BoneIndex2; idx[3] = v.BoneIndex3;
            w[0] = v.BoneWeight0; w[1] = v.BoneWeight1; w[2] = v.BoneWeight2; w[3] = v.BoneWeight3;

            float sum = 0f;
            for (int k = 0; k < 4; k++)
            {
                if (w[k] <= 0f) { w[k] = 0f; idx[k] = 0; continue; }
                if (idx[k] >= boneCount) { droppedInfluences++; w[k] = 0f; idx[k] = 0; continue; }
                sum += w[k];
            }

            var bw = new BoneWeight();
            if (sum <= 0f)
            {
                unweightedVertices++;
                bw.boneIndex0 = 0;
                bw.weight0 = 1f;
            }
            else
            {
                bw.boneIndex0 = idx[0]; bw.weight0 = w[0] / sum;
                bw.boneIndex1 = idx[1]; bw.weight1 = w[1] / sum;
                bw.boneIndex2 = idx[2]; bw.weight2 = w[2] / sum;
                bw.boneIndex3 = idx[3]; bw.weight3 = w[3] / sum;
            }
            weights[i] = bw;
        }
        return weights;
    }

    public static WmvRuntimeModel Build(M2ParsedModel model, M2ParsedSkin skin,
                                        Dictionary<int, BlpImage> decodedTextures,
                                        string objectName, Action<string> log,
                                        HashSet<int> geosets = null)
    {
        if (model == null || skin == null)
            throw new WowParseException("builder: nothing to build");
        if (model.Vertices.Length == 0)
            throw new WowParseException("builder: model has no vertices");
        if (skin.Batches.Length == 0)
            throw new WowParseException("builder: skin profile has no draw batches");

        var result = new WmvRuntimeModel();

        // ---- geometry: all model vertices once, converted to Unity space --------------
        int n = model.Vertices.Length;
        var positions = new Vector3[n];
        var normals = new Vector3[n];
        var uvs = new Vector2[n];
        var uvs2 = new Vector2[n];
        for (int i = 0; i < n; i++)
        {
            float x, y, z;
            WowCoordinateConverter.ConvertPosition(model.Vertices[i].Position, out x, out y, out z);
            positions[i] = new Vector3(x, y, z);
            WowCoordinateConverter.ConvertNormal(model.Vertices[i].Normal, out x, out y, out z);
            normals[i] = new Vector3(x, y, z);
            float u, v;
            WowCoordinateConverter.ConvertTexCoord(model.Vertices[i].TexCoord0, out u, out v);
            if (Debug_.FlipV) v = 1f - v;
            uvs[i] = new Vector2(u, v);
            WowCoordinateConverter.ConvertTexCoord(model.Vertices[i].TexCoord1, out u, out v);
            if (Debug_.FlipV) v = 1f - v;
            uvs2[i] = new Vector2(u, v);
        }

        if (log != null && model.Vertices.Length > 0)
        {
            float u0, v0;
            WowCoordinateConverter.ConvertTexCoord(model.Vertices[0].TexCoord0, out u0, out v0);
            log(string.Format("uv: {0} vertices, set 0 used (M2 carries 2 sets); vertex 0 raw " +
                              "({1:F4},{2:F4}) -> unity ({3:F4},{4:F4}); V flipped by converter" +
                              (Debug_.FlipV ? " and again by -wmvFlipV" : ""),
                              model.Vertices.Length, model.Vertices[0].TexCoord0.X,
                              model.Vertices[0].TexCoord0.Y, uvs[0].x, uvs[0].y));
        }

        var mesh = new Mesh { name = objectName + "_mesh" };
        mesh.indexFormat = n > 65000
            ? UnityEngine.Rendering.IndexFormat.UInt32
            : UnityEngine.Rendering.IndexFormat.UInt16;
        mesh.vertices = positions;
        mesh.normals = normals;
        mesh.uv = uvs;
        // An M2 carries two UV sets, and a combiner unit routed to "T2" reads the second. Cheap to
        // upload and meaningless to the units that do not use it.
        mesh.uv2 = uvs2;

        // ---- one submesh per batch ----------------------------------------------------
        var materials = new List<Material>();
        var bindings = new List<WmvMaterialBinding>();
        var textures = new List<Texture2D>();
        // Keyed by slot AND by whether the alpha channel was discarded, because the same slot
        // can feed an opaque batch (alpha thrown away) and a blended one (alpha kept).
        var textureCache = new Dictionary<int, Texture2D>();
        var drawOrder = new List<WmvDrawOrderKey>();
        var animBindings = new List<WmvMaterialAnimBinding>();
        var gateHidden = new List<bool>();
        var triangleSets = new List<int[]>();
        var submeshGeosets = new List<int>();
        var submeshIndices = new List<int>();
        int totalTriangles = 0, hiddenByGeoset = 0;

        // Resolve the shader up front: whether it can run the M2 combiner decides how the base
        // texture's alpha has to be treated, which happens before any material is created.
        FindRenderShader(log);
        bool combinerAvailable = ShaderHasCombiner();

        // Which batches the model actually wants drawn. Decided before the loop so that a model
        // whose every batch is hidden still renders SOMETHING: that is far more likely to be a
        // visibility rule this milestone does not implement than a model that draws nothing.
        var visible = new bool[skin.Batches.Length];
        var hiddenReasons = new string[skin.Batches.Length];
        // A batch whose gate is ANIMATED -- its weight or colour-alpha track has more than one
        // key, or runs on a global sequence -- is invisible now and visible later, or the other
        // way round. The legacy viewport decides that per frame (ModelRenderPass.cpp:468, called
        // every frame from the draw loop); dropping such a batch at build time, as this used to,
        // made it unrepresentable for the rest of the model's life. It is built and handed to the
        // material animator, which withholds its triangles until its gate opens.
        var keptForAnimation = new bool[skin.Batches.Length];
        int hiddenCount = 0, keptCount = 0;
        for (int i = 0; i < skin.Batches.Length; i++)
        {
            visible[i] = BatchIsVisible(model, skin.Batches[i], out hiddenReasons[i]);
            if (!visible[i])
            {
                if (GateAnimates(model, skin.Batches[i]) ||
                    M2MaterialEval.GateMayOpenElsewhere(model, skin.Batches[i]))
                {
                    keptForAnimation[i] = true;
                    keptCount++;
                }
                else
                    hiddenCount++;
            }
        }
        bool drawHidden = Debug_.ShowHidden || hiddenCount + keptCount == skin.Batches.Length;
        if (hiddenCount == skin.Batches.Length && hiddenCount > 0 && log != null)
            log("every batch is hidden at rest -- drawing them all rather than nothing; this is " +
                "almost certainly a visibility rule this milestone does not implement");

        for (int batchIndex = 0; batchIndex < skin.Batches.Length; batchIndex++)
        {
            M2Batch batch = skin.Batches[batchIndex];
            M2MaterialDef mat = (batch.MaterialIndex < model.Materials.Length)
                ? model.Materials[batch.MaterialIndex]
                : default(M2MaterialDef);
            M2BlendMode mode = Debug_.ForceOpaque ? M2BlendMode.Opaque : (M2BlendMode)mat.BlendMode;

            // Does the model actually want this batch drawn right now? See BatchIsVisible.
            if (!visible[batchIndex])
            {
                if (log != null)
                    log(string.Format("batch: submesh {0} is HIDDEN at rest ({1}){2}",
                                      batch.SubmeshIndex, hiddenReasons[batchIndex],
                                      drawHidden ? " -- drawn anyway"
                                      : keptForAnimation[batchIndex]
                                          ? (GateAnimates(model, batch)
                                              ? " -- but its gate animates: kept, triangles withheld until it opens"
                                              : string.Format(" -- but another sequence can show it ({0} with keys above zero or none, {1} in unread .anim files): kept, triangles withheld",
                                                              model.Colors[batch.ColorIndex].OpacityOtherVisible,
                                                              model.Colors[batch.ColorIndex].OpacityOtherUnknown))
                                          : " -- skipped, as the legacy viewport does"));
                if (!drawHidden && !keptForAnimation[batchIndex])
                    continue;
            }

            M2Submesh submesh = skin.Submeshes[batch.SubmeshIndex];
            int[] indices = M2SkinParser.BuildTriangles(skin, submesh, n);
            WowCoordinateConverter.FlipWinding(indices);   // handedness change reverses winding
            triangleSets.Add(indices);
            submeshGeosets.Add(submesh.Id);
            submeshIndices.Add(batch.SubmeshIndex);
            // A geoset the variant does not switch on still gets its submesh and material -- only
            // its triangles are withheld -- so a later variant can switch it back on without
            // rebuilding anything. Counted through the same rule the mesh is built with, so the
            // triangle count and the summary below agree with what was actually drawn even when
            // -wmvOnlySubmesh is deciding.
            if (SubmeshDrawn(batch.SubmeshIndex, submesh.Id, geosets))
                totalTriangles += indices.Length / 3;
            else
                hiddenByGeoset++;

            // How this material combines its texture units, resolved exactly as the legacy
            // renderer does, then reduced to what this renderer can draw of it.
            var shader = M2ShaderTable.Resolve(batch.TextureCount, batch.ShaderId);
            CombinerPlan plan = PlanCombiner(shader.PixelShader);
            M2UvSource unit1Uv = batch.TextureCount >= 2 ? shader.UvSource[1] : M2UvSource.TexCoord1;

            // Unit 1 is bound when the combiner reads it AND the model actually declares it. A
            // shader that cannot run a combiner at all (a pipeline Lit shader) gets neither.
            bool useUnit1 = plan.NeedsUnit1 && batch.TextureCount >= 2 &&
                            combinerAvailable && !Debug_.ForceSolid;

            int textureSlot = ResolveTextureSlot(model, batch, 0);
            int unit1Slot = useUnit1 ? ResolveTextureSlot(model, batch, 1) : -1;
            // The third unit. Only a combiner that actually consumes it asks for it, so no other
            // material pays for the extra sampler or the extra upload.
            bool useUnit2 = plan.Lobe && batch.TextureCount >= 3 &&
                            combinerAvailable && !Debug_.ForceSolid;
            int unit2Slot = useUnit2 ? ResolveTextureSlot(model, batch, 2) : -1;

            // The base texture's ALPHA is not transparency on an opaque material -- on a creature
            // skin it is a reflection mask -- so it is discarded on upload UNLESS something
            // actually reads it: a combiner that masks with it, or a blend mode that outputs it.
            bool alphaIsRead = useUnit1 ||
                               plan.AlphaMode == 1 || plan.AlphaMode == 3 || plan.AlphaMode == 4 ||
                               mode != M2BlendMode.Opaque;
            bool dropAlpha = Debug_.ForceSolid || !alphaIsRead;

            // Unit 0's coordinate source. Read from the same resolved vertex-shader name unit 1
            // uses; before this it was resolved and thrown away, and unit 0 always took mesh UVs.
            M2UvSource unit0Uv = shader.UvSource.Length > 0 ? shader.UvSource[0] : M2UvSource.TexCoord0;
            bool unit0Env = unit0Uv == M2UvSource.Environment;
            bool baseWrapX, baseWrapY;
            TextureWrap(model, textureSlot, unit0Env, out baseWrapX, out baseWrapY);
            Texture2D tex = GetTexture(decodedTextures, textureCache, textures, textureSlot,
                                       dropAlpha, baseWrapX, baseWrapY, objectName);
            // An environment unit is sampled by generated sphere-map coordinates, which must not
            // wrap; a unit fed by a stored UV set is a normal repeating texture. Its own alpha is
            // kept whenever the combiner reads it.
            bool unit1Env = unit1Uv == M2UvSource.Environment;
            // Its alpha is kept whenever anything reads it: an alpha mode that combines it, the
            // decal combiner -- or a second-unit lobe, whose whole shape is unit1.rgb * unit1.a.
            bool unit1DropAlpha = !(plan.AlphaMode == 2 || plan.AlphaMode == 3 || plan.AlphaMode == 4 ||
                                    plan.AlphaMode == 5 || plan.Mode == 4 || plan.Lobe2 > 0);
            bool unit1WrapX, unit1WrapY;
            TextureWrap(model, unit1Slot, unit1Env, out unit1WrapX, out unit1WrapY);
            Texture2D unit1Tex = GetTexture(decodedTextures, textureCache, textures, unit1Slot,
                                            unit1DropAlpha, unit1WrapX, unit1WrapY, objectName);
            // Unit 2 keeps ITS OWN coordinate source and address mode. On the armour benchmark
            // the host binds the same file to unit 0 and unit 2, but the M2 gives them different
            // wrap flags (unit 0 repeats, unit 2 clamps), so reusing unit 0's sampler state would
            // be wrong even where the image is identical. Its alpha is the lobe's mask and is
            // never dropped.
            M2UvSource unit2Uv = shader.UvSource.Length > 2 ? shader.UvSource[2] : M2UvSource.TexCoord0;
            bool unit2Env = unit2Uv == M2UvSource.Environment;
            bool unit2WrapX, unit2WrapY;
            TextureWrap(model, unit2Slot, unit2Env, out unit2WrapX, out unit2WrapY);
            Texture2D unit2Tex = GetTexture(decodedTextures, textureCache, textures, unit2Slot,
                                            false, unit2WrapX, unit2WrapY, objectName);

            if (log != null)
            {
                log(string.Format("batch: submesh {0} ({1} tris) material {2} blend {3} flags 0x{4:X} " +
                                  "twoSided {5} depthWriteOff {6} -> texture slot {7}{8}, alpha {9}",
                                  batch.SubmeshIndex, indices.Length / 3, batch.MaterialIndex,
                                  (M2BlendMode)mat.BlendMode, mat.Flags, mat.TwoSided,
                                  mat.DepthWriteDisabled, textureSlot,
                                  tex == null ? " (no texture)" : "",
                                  dropAlpha ? "forced to 255" : "kept (something reads it)"));
                log(string.Format("batch: shaderId 0x{0:X4} -> {1} / {2} (pixel shader {3}), {4} unit(s), " +
                                  "unit 1 uv {5} -> combiner {6}, alpha mode {7}x{8}{9}",
                                  batch.ShaderId, shader.PixelShaderName, shader.VertexShaderName,
                                  shader.PixelShader, batch.TextureCount,
                                  batch.TextureCount >= 2 ? unit1Uv.ToString() : "n/a",
                                  plan.Mode, plan.AlphaMode, plan.AlphaScale,
                                  useUnit1 ? ", unit 1 slot " + unit1Slot + " bound"
                                           : (plan.NeedsUnit1 && batch.TextureCount >= 2
                                              ? ", unit 1 NOT bound (the resolved shader cannot combine)"
                                              : "")));
                // EVERY declared unit, and whether this renderer samples it. The combiner table
                // decides how many units a pass HAS; this renderer binds at most two, so a pass
                // with three declares one that never reaches the frame. That is invisible in a
                // picture and decisive in an audit, so it is stated per batch rather than implied.
                var units = new System.Text.StringBuilder();
                for (int u = 0; u < batch.TextureCount; u++)
                {
                    int comboIndex = batch.TextureComboIndex + u;
                    int lookup = (comboIndex >= 0 && comboIndex < model.TextureLookup.Length)
                                 ? model.TextureLookup[comboIndex] : -1;
                    int uslot = ResolveTextureSlot(model, batch, u);
                    string src = u < shader.UvSource.Length ? shader.UvSource[u].ToString() : "?";
                    // Unit 2 IS sampled when the combiner has a lobe and a third texture
                    // arrived. This line used to read "NOT SAMPLED -- this renderer binds two
                    // units" unconditionally, which stopped being true when the third unit landed
                    // and then sent a later reader looking for a binding bug that was not there.
                    string sampled = u == 0 ? "SAMPLED"
                                    : (u == 1 ? (useUnit1 ? "SAMPLED" : "not sampled")
                                    : (u == 2 ? (useUnit2 && plan.Lobe ? "SAMPLED" : "not sampled")
                                              : "not sampled -- this renderer binds three units"));
                    units.Append(string.Format("{0}unit {1} uv {2} -> lookup[{3}]={4} slot {5}",
                                               u == 0 ? "" : "; ", u, src, comboIndex, lookup, uslot));
                    if (uslot >= 0 && uslot < model.Textures.Length)
                        units.Append(string.Format(" (M2 type {0}, file {1})",
                                                   model.Textures[uslot].Type,
                                                   model.Textures[uslot].FileDataID));
                    units.Append(" ").Append(sampled);
                }
                log(string.Format("batch: submesh {0} geoset {1}, texture combo {2}, {3} unit(s) -- {4}",
                                  batch.SubmeshIndex, submesh.Id, batch.TextureComboIndex,
                                  batch.TextureCount, units));
                if (!plan.Known)
                    log(string.Format("material: pixel shader {0} ({1}) is not implemented -- drawing " +
                                      "unit 0 alone with its own alpha",
                                      shader.PixelShader, shader.PixelShaderName));
            }

            bindings.Add(new WmvMaterialBinding
            {
                ThirdSlot = unit2Tex != null ? unit2Slot : -1,
                Unit2WrapX = unit2WrapX, Unit2WrapY = unit2WrapY,
                BaseSlot = textureSlot, EnvSlot = unit1Slot, DropAlpha = dropAlpha,
                Unit1DropAlpha = unit1DropAlpha,
                BaseWrapX = baseWrapX, BaseWrapY = baseWrapY,
                Unit1WrapX = unit1WrapX, Unit1WrapY = unit1WrapY,
            });
            if (log != null && (unit0Env || baseWrapX || baseWrapY || unit1WrapX || unit1WrapY))
                log(string.Format("batch: submesh {0} texture addressing -- unit 0 {1} ({2}), " +
                                  "unit 1 {3}", batch.SubmeshIndex,
                                  unit0Env ? "sphere map, clamped" : (baseWrapX ? "repeat X" : "clamp X") + "/" + (baseWrapY ? "repeat Y" : "clamp Y"),
                                  unit0Uv, unit1Slot < 0 ? "-" : (unit1Env ? "sphere map, clamped"
                                      : (unit1WrapX ? "repeat X" : "clamp X") + "/" + (unit1WrapY ? "repeat Y" : "clamp Y"))));
            // The keys the legacy sorts its passes on, kept so the same order can be reproduced
            // after the loop. specialTex mirrors the legacy's own mapping: a plain file texture is
            // -1, anything database-resolved keeps its type number.
            int specialTex = (textureSlot >= 0 && textureSlot < model.Textures.Length &&
                              model.Textures[textureSlot].Type != 0)
                             ? (int)model.Textures[textureSlot].Type : -1;
            drawOrder.Add(new WmvDrawOrderKey
            {
                Material = materials.Count, Blend = (int)mode,
                Submesh = batch.SubmeshIndex, SpecialTex = specialTex, Built = drawOrder.Count,
            });
            // What animates this material. Resolved here, where the batch, its units and the
            // combiner plan are all in hand, exactly as the legacy viewport resolves a pass.
            animBindings.Add(ResolveAnimBinding(model, batch, materials.Count, mode, plan,
                                                unit0Env, unit1Env, useUnit1,
                                                unit2Env, unit2Tex != null, log));
            // A batch kept only for its animated gate starts hidden: the animator opens it.
            gateHidden.Add(keptForAnimation[batchIndex] && !drawHidden);
            materials.Add(CreateMaterial(mat, mode, plan, unit0Uv, unit1Uv, unit2Uv,
                                         tex, unit1Tex, unit2Tex,
                                         objectName + "_mat" + materials.Count, log));
        }

        if (hiddenCount > 0 && !drawHidden && log != null)
            log(string.Format("{0} of {1} batch(es) hidden at rest -- pass -wmvShowHidden to draw them",
                              hiddenCount, skin.Batches.Length));

        // ---- transparent draw order ---------------------------------------------------------
        // The whole model is ONE renderer with one submesh per batch, so nothing about the mesh
        // decides the order the blended ones reach the frame buffer: within a render queue they
        // are drawn in submesh order, which is the order the skin happens to list them. The legacy
        // viewport instead sorts every pass by blend mode, then geoset, then texture type. Giving
        // each transparent material its own queue value in that order reproduces the legacy's
        // sequence without touching the mesh, the materials' blend state, or the architecture.
        //
        // Only modes 2..7 are ranked: opaque and alpha-key already sit in earlier queues and must
        // stay there. The rank is bounded so it can never reach the next queue band.
        if (materials.Count > 0)
        {
            var ranked = new List<WmvDrawOrderKey>();
            for (int i = 0; i < drawOrder.Count; i++)
                if (drawOrder[i].Blend >= 2)
                    ranked.Add(drawOrder[i]);
            // A total order: every key compared, and the build position last, so the result is the
            // same on every run and on every machine.
            ranked.Sort(delegate(WmvDrawOrderKey a, WmvDrawOrderKey b)
            {
                if (a.Blend != b.Blend) return a.Blend.CompareTo(b.Blend);
                if (a.Submesh != b.Submesh) return a.Submesh.CompareTo(b.Submesh);
                if (a.SpecialTex != b.SpecialTex) return a.SpecialTex.CompareTo(b.SpecialTex);
                return a.Built.CompareTo(b.Built);
            });
            int transparentQueue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
            for (int r = 0; r < ranked.Count; r++)
            {
                Material m = materials[ranked[r].Material];
                if (m != null)
                    m.renderQueue = transparentQueue + Mathf.Min(r, 899);
            }
            if (log != null && ranked.Count > 1)
            {
                string order = "";
                for (int r = 0; r < ranked.Count && r < 12; r++)
                    order += (r > 0 ? " " : "") + "submesh" + ranked[r].Submesh +
                             "/blend" + ranked[r].Blend;
                log(string.Format("draw order: {0} transparent batch(es) ranked by blend mode then " +
                                  "submesh then texture type, queues {1}..{2} -- {3}{4}",
                                  ranked.Count, transparentQueue,
                                  transparentQueue + Mathf.Min(ranked.Count - 1, 899), order,
                                  ranked.Count > 12 ? " ..." : ""));
            }
        }

        mesh.subMeshCount = triangleSets.Count;
        for (int i = 0; i < triangleSets.Count; i++)
            mesh.SetTriangles(SubmeshDrawn(submeshIndices[i], submeshGeosets[i], geosets) && !gateHidden[i]
                                  ? triangleSets[i] : EmptyTriangles,
                              i, false);
        // Bounds come from the WHOLE model, not from what is currently visible, so switching a
        // variant does not make the camera jump. A batch kept for its animated gate is in
        // triangleSets like any other, so the bounds include it whether or not it is drawn yet.
        mesh.bounds = BoundsOfAll(positions, triangleSets);

        var go = new GameObject(objectName);

        // ---- material animation -------------------------------------------------------------
        result.Materials = materials.ToArray();
        result.Mesh = mesh;
        result.SubmeshTriangles = triangleSets.ToArray();
        result.SubmeshGeosets = submeshGeosets.ToArray();
        result.SubmeshIndices = submeshIndices.ToArray();
        result.Geosets = geosets;
        result.MaterialAnim = animBindings.ToArray();
        result.GateHidden = gateHidden.ToArray();
        // The material animator is created UNCONDITIONALLY -- under -wmvNoAnim as well: the state
        // at time 0 (the gate for every pass; a colour and an opacity for an unlit one) is part
        // of what the model looks like at rest,
        // and applying it once is the same picture the legacy viewport draws. -wmvNoAnim keeps
        // its meaning through the clock, not through this component: no WmvM2Animator is created
        // under it (see the skinned and static branches below), so nothing evaluates per frame.
        {
            // Created whenever any binding HAS an input at all -- a colour entry, a weight, a
            // resolved transform -- keys or not: a transform with no keys in this sequence can
            // have keys in another (the format allows it; the client sweep counted the models).
            // The animator is what a sequence change re-reads through, so it must exist before
            // the change. DORMANT when nothing evaluates (AnimatedCount 0): no clock is created
            // and nothing runs per frame until Rebind finds keys, so a dormant animator costs one
            // component and nothing else; a later sequence change in a normal animated run wakes it.
            bool anyInput = false, anyKeys = false;
            for (int i = 0; i < result.MaterialAnim.Length; i++)
            {
                WmvMaterialAnimBinding bnd = result.MaterialAnim[i];
                if (bnd.Color >= 0 || bnd.Weight >= 0 || bnd.Weight1 >= 0 || bnd.Weight2 >= 0 ||
                    bnd.Transform0 >= 0 || bnd.Transform1 >= 0)
                    anyInput = true;
                if (WmvMaterialAnimator.BindingAnimates(model, bnd))
                    anyKeys = true;
            }
            if (anyInput)
            {
                var ma = go.AddComponent<WmvMaterialAnimator>();
                ma.Setup(result, model, log);
                result.MaterialAnimator = ma;
                if (!anyKeys && log != null)
                    log("matanim: material animator created DORMANT -- inputs are bound but none has " +
                        "keys in this sequence; a sequence change can wake it");
            }
            else if (log != null)
                log("matanim: no material on this model binds a colour entry, a texture weight " +
                    "or a texture transform");
        }

        if (log != null)
        {
            // What the skin's index starts looked like. Printed on every load, not only when
            // something is odd, because "no model I tried exercised this" is itself a result and
            // only a line that always prints can establish it.
            M2SkinIndexSurvey ix = skin.IndexSurvey;
            log(string.Format(
                "skin indices: {0} submesh(es) over {1} index/es; {2} carry a Level word; " +
                "largest start {3}{4}",
                ix.Submeshes, ix.TotalIndices, ix.NonZeroLevel, ix.MaxIndexStart,
                ix.ExercisesWideStarts ? " -- this model needs starts wider than 16 bits" : ""));
            if (ix.CumulativeChecked && !ix.CumulativeAgrees)
                log(string.Format(
                    "skin indices: WARNING the format's start and the legacy viewport's running " +
                    "sum disagree, first at submesh {0} (format {1}, running sum {2}). One of the " +
                    "two renderers is drawing the wrong triangles here.",
                    ix.DisagreeAt, ix.DisagreeExpanded, ix.DisagreeCumulative));

            // Which rule decided what is on screen. See SubmeshDrawn / GeosetVisible.
            if (Debug_.OnlySubmeshes != null)
                log("geosets: -wmvOnlySubmesh is in force -- the geoset rule is replaced by the " +
                    "named submesh list for this run");
            int shownSets = 0;
            for (int i = 0; i < submeshGeosets.Count; i++)
                if (SubmeshDrawn(submeshIndices[i], submeshGeosets[i], geosets))
                    shownSets++;
            string how = geosets != null
                ? "the displayed variant [" + GeosetList(geosets) + "]"
                : "no variant reported -- geoset 0 only, as the legacy viewport defaults";
            log(string.Format("geosets: {0} of {1} submesh(es) drawn, {2} hidden, by {3}; " +
                              "{4} of the skin's submeshes carry geoset 0",
                              shownSets, triangleSets.Count, hiddenByGeoset, how, ix.GeosetZero));
            if (shownSets == 0 && triangleSets.Count > 0 && Debug_.OnlySubmeshes == null)
                log("geosets: NOTHING is drawn -- this model has no geoset 0 submesh and no " +
                    "variant switched anything on. The legacy viewport shows nothing here too; " +
                    "reported rather than worked around, because a fallback would be a guess at " +
                    "what the model meant.");
            else if (shownSets == 0 && triangleSets.Count > 0)
                log("geosets: nothing is drawn, because -wmvOnlySubmesh named no submesh this " +
                    "model has. That is the switch, not the model.");
        }

        // ---- scene object -------------------------------------------------------------
        // Skinned when the model carries a rig this renderer can reproduce, static otherwise. The
        // two paths share everything above: the same vertices, the same submesh-per-batch split,
        // the same materials. Only how the mesh reaches the scene differs, and in the rest pose
        // the result is the same geometry -- see BuildSkeleton for why that is exact rather than
        // approximate.

        SkinPlan skinPlan = PlanSkinning(model);
        var boneTransforms = new Transform[0];
        var restPositions = new Vector3[0];

        if (skinPlan.CanSkin)
        {
            Transform rootBone;
            Vector3[] restLocalPositions;
            boneTransforms = BuildSkeleton(model, go.transform, out rootBone, out restLocalPositions);
            restPositions = restLocalPositions;

            int dropped, unweighted;
            mesh.boneWeights = BuildBoneWeights(model, boneTransforms.Length, out dropped, out unweighted);

            // The canonical bind pose: the matrix that takes a vertex from the renderer's space
            // into the bone's. Taken from the transforms just built rather than from the pivots
            // again, so the two can never disagree about where a bone is.
            var bindPoses = new Matrix4x4[boneTransforms.Length];
            for (int i = 0; i < boneTransforms.Length; i++)
                bindPoses[i] = boneTransforms[i].worldToLocalMatrix * go.transform.localToWorldMatrix;
            mesh.bindposes = bindPoses;

            var smr = go.AddComponent<SkinnedMeshRenderer>();
            smr.sharedMesh = mesh;
            smr.bones = boneTransforms;
            smr.rootBone = rootBone;
            smr.sharedMaterials = materials.ToArray();
            // A skinned renderer culls against localBounds, not against the mesh's own bounds, and
            // an unset one is a zero-size box at the origin -- the model would flicker out the
            // moment the camera moved. Bounds of the whole model, as above, so switching a geoset
            // variant does not resize it.
            smr.localBounds = mesh.bounds;
            smr.updateWhenOffscreen = false;

            // Emitters BEFORE the animation block, because whether the model has any is one of
            // the reasons to keep an animator: the emitters run on its clock, so a model whose
            // idle moves no bone but which trails fire still needs one.
            BuildEmitters(result, model, go, boneTransforms, decodedTextures, textureCache,
                          textures, objectName, log);

            if (log != null)
            {
                log(string.Format("skin: {0} bone(s), {1} root(s), max depth {2}; " +
                                  "bind pose = rest pose (every bone at its pivot, no rotation)",
                                  boneTransforms.Length, CountRoots(model), MaxDepth(model)));
                if (dropped > 0 || unweighted > 0)
                    log(string.Format("skin: {0} influence(s) pointed past the bone array and were " +
                                      "dropped; {1} vertex/vertices were left with none and are bound " +
                                      "to bone 0 (identity at rest)", dropped, unweighted));
            }

            if (Debug_.SkinCheck)
                ReportSkinDeviation(smr, positions, log);

            // ---- animation ------------------------------------------------------------
            // The bind poses above are read from the rest pose, so they must be taken BEFORE
            // anything moves a bone. Adding the animator last keeps that ordering obvious.
            if (Debug_.NoAnim)
            {
                if (log != null)
                    log("anim: not playing -- -wmvNoAnim was passed");
            }
            else if (model.AnimatedSequence >= 0 && model.AnimatedSequence < model.Sequences.Length)
            {
                var animator = go.AddComponent<WmvM2Animator>();
                animator.Setup(model, boneTransforms, restLocalPositions, log);
                animator.Materials = result.MaterialAnimator;
                animator.Emitters = result.Emitters;
                if (animator.AnimatedBoneCount == 0 && !NeedsClock(result))
                {
                    // Nothing in the idle actually moves; the component would burn a LateUpdate
                    // per frame to write nothing.
                    UnityEngine.Object.Destroy(animator);
                    if (log != null)
                        log("anim: the idle sequence moves no bones -- staying in the rest pose");
                }
                else if (animator.AnimatedBoneCount == 0)
                {
                    // No bone moves, but a material does: the animator stays for its clock. It is
                    // the ONE clock in this renderer, and the materials follow it -- pause, scrub
                    // and sequence change included -- rather than keeping a timer of their own.
                    result.Animator = animator;
                    if (log != null)
                        log("anim: the idle sequence moves no bones, but materials animate -- the " +
                            "animator stays as their clock");
                }
                else
                {
                    // A skinned renderer culls against localBounds, which was measured from the
                    // rest pose. An animation moves vertices outside it, so let Unity measure the
                    // real bounds each frame instead of clipping the model as it moves.
                    smr.updateWhenOffscreen = true;
                    result.Animator = animator;
                    if (Debug_.AnimCheck)
                        ReportAnimationRange(smr, animator, positions, mesh.bounds, log);
                }
                result.Skin = smr;
            }
            else if (log != null)
            {
                log("anim: not playing -- " + (model.AnimationSkipReason ?? "no idle sequence was resolved"));
            }
        }
        else
        {
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            var renderer = go.AddComponent<MeshRenderer>();
            renderer.sharedMaterials = materials.ToArray();
            if (log != null)
                log("skin: drawn as a static mesh -- " + skinPlan.Reason);
            // No bones: an emitter still draws, at its authored model-space offset, with the
            // identity where a bone matrix would be.
            BuildEmitters(result, model, go, boneTransforms, decodedTextures, textureCache,
                          textures, objectName, log);
            // A static mesh has no bones to drive, but its materials may still move. The same
            // animator supplies the clock, with nothing to pose.
            if (!Debug_.NoAnim && NeedsClock(result) &&
                model.AnimatedSequence >= 0 && model.AnimatedSequence < model.Sequences.Length)
            {
                var animator = go.AddComponent<WmvM2Animator>();
                animator.Setup(model, new Transform[0], new Vector3[0], log);
                animator.Materials = result.MaterialAnimator;
                animator.Emitters = result.Emitters;
                result.Animator = animator;
                if (log != null)
                    log("anim: static mesh, but materials or emitters animate -- an animator "
                        + "supplies the clock");
            }
        }

        result.Root = go;
        result.Bones = boneTransforms;
        result.BoneRestPositions = restPositions;
        result.Skinned = skinPlan.CanSkin;
        result.Bindings = bindings.ToArray();
        result.Textures = textures.ToArray();
        result.Bounds = mesh.bounds;
        result.VertexCount = n;
        result.TriangleCount = totalTriangles;
        result.SubmeshCount = triangleSets.Count;
        return result;
    }

    /// <summary>
    /// Bake the skinned mesh and report how far its vertices moved from where the file put them.
    ///
    /// This is the milestone's contract expressed as a number. The skinned and the static path
    /// share their vertex array, so if the bind poses and the bone placement agree the baked
    /// result is the input, and the largest deviation is float noise. A large one would mean the
    /// rig is being applied rather than cancelled -- which is exactly the failure that is hard to
    /// see by eye on a model you have not memorised.
    /// </summary>
    static void ReportSkinDeviation(SkinnedMeshRenderer smr, Vector3[] positions, Action<string> log)
    {
        if (log == null || smr == null)
            return;
        var baked = new Mesh();
        try
        {
            smr.BakeMesh(baked, true);
            Vector3[] after = baked.vertices;
            if (after.Length != positions.Length)
            {
                log(string.Format("skin check: baked {0} vertices, expected {1}", after.Length, positions.Length));
                return;
            }
            float worst = 0f;
            int worstAt = -1;
            for (int i = 0; i < after.Length; i++)
            {
                float dx = after[i].x - positions[i].x;
                float dy = after[i].y - positions[i].y;
                float dz = after[i].z - positions[i].z;
                float dist = Mathf.Sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > worst) { worst = dist; worstAt = i; }
            }
            log(string.Format("skin check: {0} vertices baked from the rest pose; largest deviation " +
                              "{1:E3} units at vertex {2}", after.Length, worst, worstAt));
        }
        finally
        {
            UnityEngine.Object.Destroy(baked);
        }
    }

    /// <summary>
    /// Sample the idle across its length and report how far the skinned mesh leaves its rest pose.
    ///
    /// This is the animation counterpart of ReportSkinDeviation, and it exists for the same
    /// reason: the two ways this can be wrong -- nothing moves, or everything flies apart -- both
    /// look like a perfectly ordinary still frame in a log. Displacements are printed next to the
    /// model's own diagonal, because "0.4 units" means nothing until you know the chicken is 0.7
    /// units across.
    /// </summary>
    static void ReportAnimationRange(SkinnedMeshRenderer smr, WmvM2Animator animator,
                                     Vector3[] rest, Bounds bounds, Action<string> log)
    {
        if (log == null || smr == null || animator == null)
            return;
        const int Samples = 8;
        var baked = new Mesh();
        try
        {
            float worst = 0f, mean = 0f;
            var perSample = new float[Samples];
            for (int s = 0; s < Samples; s++)
            {
                animator.ApplyPose(animator.LengthMs * s / Samples);
                smr.BakeMesh(baked, true);
                Vector3[] now = baked.vertices;
                float sampleWorst = 0f;
                double sum = 0.0;
                int n = Mathf.Min(now.Length, rest.Length);
                for (int i = 0; i < n; i++)
                {
                    float dx = now[i].x - rest[i].x, dy = now[i].y - rest[i].y, dz = now[i].z - rest[i].z;
                    float dist = Mathf.Sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > sampleWorst) sampleWorst = dist;
                    sum += dist;
                }
                perSample[s] = sampleWorst;
                if (sampleWorst > worst) worst = sampleWorst;
                mean += n > 0 ? (float)(sum / n) : 0f;
            }
            mean /= Samples;

            var sb = new System.Text.StringBuilder();
            for (int s = 0; s < Samples; s++)
                sb.Append(s > 0 ? ", " : "").Append(perSample[s].ToString("F3"));
            log(string.Format("anim check: over {0} samples of the {1} ms idle, vertices move at " +
                              "most {2:F3} and on average {3:F3} units; the model is {4:F3} units " +
                              "across. Per sample: {5}",
                              Samples, animator.LengthMs, worst, mean, bounds.size.magnitude, sb));
        }
        finally
        {
            UnityEngine.Object.Destroy(baked);
            animator.RestorePose();
        }
    }

    /// <summary>How many bones have no parent. Diagnostic only: an M2 rig is a forest, not a
    /// tree, and a creature routinely has a dozen roots.</summary>
    static int CountRoots(M2ParsedModel model)
    {
        int roots = 0;
        for (int i = 0; i < model.Bones.Length; i++)
            if (model.Bones[i].Parent < 0) roots++;
        return roots;
    }

    /// <summary>Deepest parent chain in the rig. Diagnostic only; the walk is bounded by the bone
    /// count so a cycle in malformed data cannot hang the load.</summary>
    static int MaxDepth(M2ParsedModel model)
    {
        int deepest = 0;
        for (int i = 0; i < model.Bones.Length; i++)
        {
            int d = 0, p = model.Bones[i].Parent, guard = 0;
            while (p >= 0 && guard++ < model.Bones.Length) { d++; p = model.Bones[p].Parent; }
            if (d > deepest) deepest = d;
        }
        return deepest;
    }

    /// <summary>
    /// Play a different animation on a model already on screen.
    ///
    /// The mesh, its materials, its textures and its geoset selection are all untouched: an
    /// animation change moves bones and nothing else. The caller supplies the SAME model re-parsed
    /// for the new sequence -- only one sequence's keyframes are ever read, so a different one
    /// means reading again -- and everything else here comes from what the build already made.
    ///
    /// Returns false when the re-parsed model has nothing to play, in which case the model keeps
    /// whatever it was doing rather than freezing halfway.
    /// </summary>
    public static bool ApplySequence(WmvRuntimeModel runtime, M2ParsedModel model, Action<string> log)
    {
        if (runtime == null || model == null)
            return false;
        // The material tracks that follow the sequence were re-read into the model; rebind them
        // before the bones, so a model whose ONLY animation is material still switches.
        if (runtime.MaterialAnimator != null)
            runtime.MaterialAnimator.Rebind(model, log);

        // The emitters' tracks were re-read for the new sequence too, and that is not cosmetic:
        // an emitter with no EmissionRate keys in the new sequence has to stop and one that gains
        // them has to start (see M2ParsedModel.ParticleEmitters). Rebind also drops every live
        // particle and every ribbon edge, so a trail cannot smear from where the bone was in the
        // old animation to where it is in the new one.
        if (runtime.Emitters != null && !runtime.Emitters.Rebind(model) && log != null)
            log("emitters: the re-parsed model has a different emitter count -- the existing "
                + "emitters were left as they were");
        {
            // (-wmvNoAnim never reaches here -- SwitchToSequence returns first -- but the rule
            // "no clock under -wmvNoAnim" is kept explicit rather than implied.)
            if (!Debug_.NoAnim && NeedsClock(runtime))
            {
                // A static mesh whose materials or emitters animate in THIS sequence: the clock
                // must exist and point at it. It is created here when the build had nothing to drive (the
                // materials were dormant then) and merely re-pointed otherwise.
                if (runtime.Animator == null)
                {
                    runtime.Animator = runtime.Root.AddComponent<WmvM2Animator>();
                    if (log != null)
                        log("anim: static mesh, materials now animate -- an animator supplies the clock");
                }
                runtime.Animator.Setup(model, new Transform[0], new Vector3[0], log);
                runtime.Animator.Materials = runtime.MaterialAnimator;
                runtime.Animator.Emitters = runtime.Emitters;
                return true;
            }
            if (runtime.Animator != null)
            {
                // Materials stopped animating in this sequence and no bone exists: nothing needs a
                // clock, so no per-frame work is left running.
                UnityEngine.Object.Destroy(runtime.Animator);
                runtime.Animator = null;
                if (log != null)
                    log("anim: static mesh, materials no longer animate in this sequence -- the clock is dropped");
            }
            // Nothing to animate: the model is drawn as a static mesh, and the build already said
            // why. Saying it again here keeps the two halves of the story in one log.
            if (log != null)
                log("anim: nothing to animate -- this model is drawn as a static mesh");
            return false;
        }
        if (Debug_.NoAnim)
        {
            if (log != null)
                log("anim: selection ignored -- -wmvNoAnim was passed");
            return false;
        }
        if (model.AnimatedSequence < 0 || model.AnimatedSequence >= model.Sequences.Length)
        {
            if (log != null)
                log("anim: cannot play the selected animation -- " +
                    (model.AnimationSkipReason ?? "no sequence was resolved"));
            return false;
        }

        WmvM2Animator animator = runtime.Animator;
        if (animator != null)
        {
            // Undo the pose the OLD animation left behind, while the animator still knows which
            // bones it moved. A bone the new sequence does not touch would otherwise keep the last
            // frame of the previous one for as long as the model is on screen.
            animator.RestorePose();
        }
        else
        {
            // The model was built without an animator (its idle moved nothing, or the app had not
            // said what to play yet). Nothing about the skeleton or the bind poses changes here.
            animator = runtime.Root.AddComponent<WmvM2Animator>();
            runtime.Animator = animator;
        }

        animator.Setup(model, runtime.Bones, runtime.BoneRestPositions, log);
        animator.Materials = runtime.MaterialAnimator;
        animator.Emitters = runtime.Emitters;
        if (animator.AnimatedBoneCount == 0 && !NeedsClock(runtime))
        {
            // A real sequence that happens to move nothing: the bones are already back at rest.
            UnityEngine.Object.Destroy(animator);
            runtime.Animator = null;
            if (runtime.Skin != null)
                runtime.Skin.updateWhenOffscreen = false;
            if (log != null)
                log("anim: the selected sequence moves no bones -- back to the rest pose");
            return true;
        }
        if (animator.AnimatedBoneCount == 0)
        {
            runtime.Animator = animator;
            if (runtime.Skin != null)
                runtime.Skin.updateWhenOffscreen = false;
            if (log != null)
                log("anim: the selected sequence moves no bones, but materials animate -- the " +
                    "animator stays as their clock");
            return true;
        }
        if (runtime.Skin != null)
            runtime.Skin.updateWhenOffscreen = true;
        return true;
    }

    static readonly int[] EmptyTriangles = new int[0];

    /// <summary>Bounds over every triangle the model has, visible or not.</summary>
    static Bounds BoundsOfAll(Vector3[] positions, List<int[]> triangleSets)
    {
        bool any = false;
        Vector3 min = Vector3.zero, max = Vector3.zero;
        foreach (var tris in triangleSets)
        {
            foreach (int i in tris)
            {
                Vector3 p = positions[i];
                if (!any) { min = max = p; any = true; continue; }
                min = new Vector3(Mathf.Min(min.x, p.x), Mathf.Min(min.y, p.y), Mathf.Min(min.z, p.z));
                max = new Vector3(Mathf.Max(max.x, p.x), Mathf.Max(max.y, p.y), Mathf.Max(max.z, p.z));
            }
        }
        var b = new Bounds();
        b.center = (min + max) * 0.5f;
        b.size = max - min;
        return b;
    }

    static string GeosetList(HashSet<int> geosets)
    {
        if (geosets == null) return "not reported";
        if (geosets.Count == 0) return "none";
        var parts = new List<string>();
        foreach (int g in geosets) parts.Add(g.ToString());
        parts.Sort();
        return string.Join(",", parts.ToArray());
    }

    /// <summary>
    /// Switch which geosets the model shows. The mesh, its vertices, its materials and its
    /// textures are all untouched: a variant change only decides which submeshes hand the mesh
    /// their triangles. Returns the number of triangles now drawn.
    /// </summary>
    public static int ApplyGeosets(WmvRuntimeModel runtime, HashSet<int> geosets, Action<string> log)
    {
        if (runtime == null || runtime.Mesh == null ||
            runtime.SubmeshTriangles.Length != runtime.Mesh.subMeshCount)
            return 0;

        runtime.Geosets = geosets;
        int visible = 0, shown = 0, hidden = 0;
        for (int i = 0; i < runtime.SubmeshTriangles.Length; i++)
        {
            bool on = (i < runtime.SubmeshIndices.Length
                       ? SubmeshDrawn(runtime.SubmeshIndices[i], runtime.SubmeshGeosets[i], geosets)
                       : GeosetVisible(runtime.SubmeshGeosets[i], geosets))
                      && !(i < runtime.GateHidden.Length && runtime.GateHidden[i]);
            runtime.Mesh.SetTriangles(on ? runtime.SubmeshTriangles[i] : EmptyTriangles, i, false);
            if (on) { visible += runtime.SubmeshTriangles[i].Length / 3; shown++; }
            else hidden++;
        }
        runtime.TriangleCount = visible;
        if (log != null)
            log(string.Format("geosets [{0}]: {1} submesh(es) shown, {2} hidden, {3} triangles drawn",
                              GeosetList(geosets), shown, hidden, visible));
        return visible;
    }

    /// <summary>
    /// Re-upload the textures of an already-built model and hand them to the materials it already
    /// has. The mesh, the GameObject and the materials themselves survive: a skin change in WMV
    /// swaps which image a material samples, nothing about the geometry.
    ///
    /// decodedTextures is the model's CURRENT texture set keyed by M2 slot -- the caller replaces
    /// the entries the new skin changed and leaves the rest alone, so slots the skin does not
    /// touch (an environment map named by the M2 itself, say) are simply re-uploaded unchanged.
    /// </summary>
    public static void RebindTextures(WmvRuntimeModel runtime, Dictionary<int, BlpImage> decodedTextures,
                                      string objectName, Action<string> log)
    {
        if (runtime == null || runtime.Materials.Length == 0)
            return;
        if (runtime.Bindings.Length != runtime.Materials.Length)
        {
            if (log != null)
                log("rebind: this model was built without texture bindings -- nothing to re-bind");
            return;
        }

        var fresh = new List<Texture2D>();
        var cache = new Dictionary<int, Texture2D>();

        for (int i = 0; i < runtime.Materials.Length; i++)
        {
            Material m = runtime.Materials[i];
            if (m == null)
                continue;
            WmvMaterialBinding b = runtime.Bindings[i];

            Texture2D tex = GetTexture(decodedTextures, cache, fresh, b.BaseSlot, b.DropAlpha,
                                       b.BaseWrapX, b.BaseWrapY, objectName);
            if (tex != null && !Debug_.MatColors)
                m.mainTexture = tex;

            // Unit 1 keeps whatever combiner the material was built with; only the image behind
            // it is re-uploaded. Re-deriving the plan here would risk the two paths drifting.
            if (b.EnvSlot >= 0 && m.HasProperty(CombinerModeProperty))
            {
                Texture2D unit1 = GetTexture(decodedTextures, cache, fresh, b.EnvSlot,
                                             b.Unit1DropAlpha, b.Unit1WrapX, b.Unit1WrapY, objectName);
                if (unit1 != null && !Debug_.MatColors)
                    m.SetTexture(SecondTexProperty, unit1);
            }

            // Unit 2 moves with the skin as well: on armour, texture type 3 IS the item skin,
            // so a skin change re-points it exactly as it re-points unit 0.
            if (b.ThirdSlot >= 0 && m.HasProperty(ThirdTexProperty))
            {
                Texture2D unit2 = GetTexture(decodedTextures, cache, fresh, b.ThirdSlot,
                                             false, b.Unit2WrapX, b.Unit2WrapY, objectName);
                if (unit2 != null && !Debug_.MatColors)
                    m.SetTexture(ThirdTexProperty, unit2);
            }

            if (log != null)
                log(string.Format("rebind: material '{0}' <- slot {1} (alpha {2}){3}{4}",
                                  m.name, b.BaseSlot, b.DropAlpha ? "forced to 255" : "kept (combiner mask)",
                                  b.EnvSlot >= 0 ? ", env slot " + b.EnvSlot : "",
                                  b.ThirdSlot >= 0 ? ", third slot " + b.ThirdSlot : ""));
        }

        // Only now destroy the old uploads: a material that ended up keeping its texture would
        // otherwise be left pointing at a destroyed one.
        foreach (var old in runtime.Textures)
            if (old != null && !fresh.Contains(old))
                UnityEngine.Object.Destroy(old);
        runtime.Textures = fresh.ToArray();
    }

    /// <summary>
    /// How much of one M2 combiner this renderer can draw. The shader takes the arithmetic as a
    /// few floats rather than a keyword per case, so widening coverage is a table entry here.
    /// </summary>
    struct CombinerPlan
    {
        public int Mode;          // _CombinerMode: 0 single, 1 u0*u1, 2 u0*u1*2, 3/4 masked/decal, 12 ps12
        public int AlphaMode;     // _AlphaMode: 0 one, 1 u0.a, 2 u1.a, 3 u0.a*u1.a, 4 u0.a+u1.a
        public float AlphaScale;
        public bool NeedsUnit1;   // the second texture is sampled, for colour or for alpha
        public bool Lobe;         // the THIRD unit contributes an additive luminous term
        /// <summary>
        /// The SECOND unit contributes an additive lobe:
        ///   0  none
        ///   1  unit1.rgb * unit1.a                     ps13, and ps16/20/23
        ///   2  unit1.rgb * unit1.a * (1 - unit0.a)     ps14
        ///   3  unit1.rgb * (1 - unit0.a)               ps21 -- NO unit1.a
        ///   4  unit1.rgb                               ps8, ps10 -- raw, nothing at all
        /// A different unit from Lobe, and much commoner: eight combiners have one where exactly
        /// one has a third-unit lobe. Shape 3 exists because ps21's reference genuinely omits the
        /// lobe texture's own alpha; giving it shape 2 would be a new bug, not a simplification.
        /// </summary>
        public int Lobe2;
        /// <summary>
        /// The second unit's lobe is scaled by UNIT 1's own texture weight -- the legacy's
        /// u_tex_sample_alpha.g, retail's cb0[6].y. True for pixel shaders 20 and 23 and false
        /// for 13 and 14, whose reference multiplies the lobe by nothing at all.
        /// </summary>
        public bool Lobe2Weighted;
        /// <summary>
        /// The FIRST unit contributes an additive lobe: unit0.rgb * unit0.a, scaled by unit 0's
        /// own texture weight. ps24 alone, and it is the only lobe in the table that does not sit
        /// on the batch's LAST texture unit -- retail reads cb0[22].x for it where every other
        /// weighted lobe reads .y or .z.
        /// </summary>
        public bool Lobe0;
        public bool Known;        // false: not implemented -- drawn from unit 0 alone, and logged
    }

    static CombinerPlan Plan(int mode, int alphaMode, float scale, bool unit1)
    {
        return Plan(mode, alphaMode, scale, unit1, false, 0);
    }

    /// <summary>As Plan, and the pixel shader also adds the third unit as a luminous lobe.</summary>
    static CombinerPlan Plan(int mode, int alphaMode, float scale, bool unit1, bool lobe)
    {
        return Plan(mode, alphaMode, scale, unit1, lobe, 0);
    }

    /// <summary>As Plan, with a SECOND-unit additive lobe: 1 = unit1.rgb * unit1.a,
    /// 2 = that masked by (1 - unit0.a).</summary>
    static CombinerPlan Plan(int mode, int alphaMode, float scale, bool unit1, bool lobe, int lobe2)
    {
        return Plan(mode, alphaMode, scale, unit1, lobe, lobe2, false);
    }

    /// <summary>As Plan, and the second unit's lobe is scaled by unit 1's own texture weight.</summary>
    static CombinerPlan Plan(int mode, int alphaMode, float scale, bool unit1, bool lobe, int lobe2,
                             bool lobe2Weighted)
    {
        CombinerPlan p;
        p.Mode = mode; p.AlphaMode = alphaMode; p.AlphaScale = scale;
        p.NeedsUnit1 = unit1; p.Lobe = lobe; p.Lobe2 = lobe2;
        p.Lobe2Weighted = lobe2Weighted; p.Lobe0 = false; p.Known = true;
        return p;
    }

    /// <summary>The same plan, plus the FIRST unit's weighted lobe. One caller: ps24.</summary>
    static CombinerPlan WithLobe0(CombinerPlan p) { p.Lobe0 = true; return p; }

    /// <summary>
    /// What one M2 pixel shader reduces to here. Ported case by case from the legacy viewport's
    /// own GLSL combiner (ModelRenderPass.cpp), not from the shader NAMES -- "Combiners_Mod_Mod2x"
    /// says nothing until you read that it is unit0 * unit1 * 2 with a discard of u0.a * u1.a * 2.
    ///
    /// Several combiners differ from a simpler one only in an ADDITIVE lobe, and the legacy
    /// viewport multiplies that lobe by a weight pinned to ZERO unless an opt-in environment
    /// variable is set (ModelRenderPass.cpp:661-662). This table used to reproduce that default
    /// and drop the lobe, collapsing those cases onto plain single-texture colour. That is a
    /// default of the legacy viewport, not a property of the material, and the lobes are being
    /// restored one at a time. DONE: 15 (third unit, weighted at unit 2), 8, 10, 13, 14, 16 and
    /// 21 (second unit, unweighted), 20 and 23 (second unit, weighted at unit 1). STILL
    /// COLLAPSING: only 8 and 10, and those are held back on purpose rather than undecoded --
    /// see their cases below. Every other combiner in this table that carries an additive term now
    /// computes it. What remains unimplemented (18, 26, 28, 30, 31, 32, 34, 35, 36 and the rest of
    /// the default arm) has no dropped lobe -- those are diffuse shapes we have not written, not
    /// terms we are throwing away.
    /// </summary>
    static CombinerPlan PlanCombiner(int pixelShader)
    {
        switch (pixelShader)
        {
            // colour from unit 0 alone
            case 0:  return Plan(0, 0, 1f, false);   // Combiners_Opaque
            case 1:  return Plan(0, 1, 1f, false);   // Combiners_Mod
            // ..._AddAlpha. The diffuse is unit 0 alone; the "_AddAlpha" is the SECOND unit
            // added as a luminous lobe, and on 14 it is masked by the inverse of unit 0's alpha.
            //
            //   ps13   mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a;
            //   ps14   ... ; specular = tex2.rgb * tex2.a * (1.0 - tex1.a);
            //   (Source/games/wow/ModelRenderPass.cpp:120-121)
            //
            // Neither multiplies by a texture weight -- unlike ps15, ps20, ps23 and ps24, the
            // reference multiplies by nothing at all, so the gain is literally 1 and there is no
            // track to resolve. The whole gap was the dropped term.
            //
            // ON ps14 THE LOBE IS THE ENTIRE MATERIAL. Without it the combiner collapses to plain
            // Combiners_Opaque and the batch draws as an inert flat surface -- which is what 6,482
            // batches on 5,036 models have been doing. The legacy viewport calls these
            // "glow / energy / cloth-effect materials ... NOT reflective metal"
            // (ModelRenderPass.cpp:58-64, isAdditiveEnvPixelShader), and its own reason for
            // dropping them was that it multiplies the lobe by a weight pinned to zero unless an
            // opt-in environment variable is set (:661-662) -- a default, not a statement about
            // the material.
            case 13: return Plan(0, 0, 1f, true, false, 1);
            case 14: return Plan(0, 0, 1f, true, false, 2);
            // ..._Wgt. ps13's lobe, scaled by UNIT 1's own texture weight -- the legacy's
            // u_tex_sample_alpha.g, retail's cb0[6].y, one channel over from the .b that ps15
            // reads. Both arms are otherwise identical to 13 and 16 respectively:
            //   ps20  specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g   (:127)
            //   ps23  ... and discard_alpha = tex1.a, can_discard           (:130)
            //
            // THE WEIGHT IS NOT OPTIONAL HERE, and that is why it lands with the lobe rather than
            // after it. Across all 723 models in the client that carry these two combiners, 697 of
            // 1,130 batches (61.7 %) have an ANIMATED weight track, exactly ONE constant in the
            // whole set equals 1.0 -- the value the shader would default to -- and 17 are authored
            // at 0.0, i.e. the artist switched the lobe off. Shipping the lobe without the weight
            // would put content on screen that the model says to hide.
            case 20: return Plan(0, 0, 1f, true, false, 1, true);
            // Mod_AddNA: the raw second texture added, with unit 0's alpha as the discard.
            //   specular = tex2.rgb   (ModelRenderPass.cpp:117)
            // Confirmed against retail: combiners_uber_2_2.bls case l(10) leaves the t1 sample in
            // the lobe register untouched and sets the diffuse to t0 -- no alpha, no mask, no
            // weight -- and its alpha arm is an empty `break`, i.e. unit 0's alpha stands.
            // Mod_AddNA. The diffuse is unit 0 alone; the "_AddNA" is the second unit added
            // as a luminous lobe, with no alpha of its own:
            //   ps10  mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a;
            //         can_discard = true; specular = tex2.rgb;
            //   (Source/games/wow/ModelRenderPass.cpp:117)
            // scaled by unit 1's own texture weight -- see the note above case 8 for why.
            //
            // WITHOUT THE LOBE THIS COMBINER LOSES A WHOLE TEXTURE. On the layered translucent
            // materials it lives on -- Algalon the Observer, the Celestial mounts, Val'kier --
            // unit 0 is the dim inner detail and unit 1 IS the luminous layer, so dropping it
            // leaves a dark, nearly opaque figure where the game draws a glowing translucent one.
            case 10: return Plan(0, 1, 1f, true, false, 4, true);   // Mod_AddNA
            // Mod_AddAlpha: ps13's lobe on a discarding pass.
            //   specular = tex2.rgb * tex2.a; discard_alpha = tex1.a  (ModelRenderPass.cpp:123)
            // No weight -- the reference multiplies the lobe by nothing.
            // _AlphaMode is unchanged by this: it was already 1 through the !combining fallback
            // and is now 1 through the plan, so the discard behaves exactly as before.
            case 16: return Plan(0, 1, 1f, true, false, 1);
            case 23: return Plan(0, 1, 1f, true, false, 1, true);   // Mod_AddAlpha_Wgt
            case 33: return Plan(0, 1, 1f, false);   // Mod_Depth

            // the alpha, but not the colour, needs unit 1
            // Mod_Add: the same raw lobe, with both alphas summed for the discard.
            //   specular = tex2.rgb; discard_alpha = tex1.a + tex2.a   (:115)
            // Retail agrees on both halves: case l(8) is `mov r4.xyz, r6.xyzx` (the raw t1 sample)
            // beside `mov r5.xyz, r3.xyzx`, and its alpha arm is `add r1.w, r1.w, r2.w`.
            //
            // THIS IS THE LARGEST-MAGNITUDE TERM IN THE TABLE -- a whole texture added with
            // nothing attenuating it -- and it is also the widest-reaching: only 1,275 batches,
            // but 52,180 of 120,978 CreatureDisplayInfo rows (43.13 %) resolve to a model that
            // carries it, because humanoid NPCs reuse about 217 player-race bodies. It is the
            // character EYES geoset (3301 -- the iris, not the 1700-1799 eye-glow).
            // ps8 AND ps10 CARRY THE SAME LOBE -- `specular = tex2.rgb`, the raw second
            // texture, no alpha -- confirmed in retail in all three of combiners_uber_2_2's
            // switch blocks (case l(8) at asm 2_2/0.asm:186 and :309, case l(10) at :194 and
            // :320). Both were held back after implementing exactly that produced a white blowout
            // across the blade of knife_1h_naxx25_d_01.
            //
            // THE DIAGNOSIS THAT KEPT THEM BACK WAS WRONG, and its own witness disproves it. It
            // blamed a missing multiply by retail's v1 -- the M2 material colour -- reasoning
            // that the artist tones these stacked additive layers down with it. But
            // knife_1h_naxx25_d_01 HAS NO COLOUR BLOCKS AT ALL: nColors is 0, and its ps10 batch
            // (skin 490678 batch 3, submesh 1 layer 1) has colorIndex -1. The multiply would have
            // been white there and could never have attenuated anything. That multiply does now
            // exist -- WmvMaterialAnimator writes _Color on every pass, lit or unlit -- and it
            // makes no difference to this model, which is exactly what its data predicts.
            //
            // WHAT THE KNIFE ACTUALLY AUTHORS is a texture weight of 0.5 on UNIT 1 -- the very
            // unit whose colour this lobe adds -- through textureWeightComboIndex 0 + unit 1 ->
            // weight lookup[1] -> track 1. The reference transcriptions do not read it for these
            // two combiners, and for a long time that looked like the end of the matter. The
            // client's own authoring says otherwise: of the 219 ps10 batches that bind a CONSTANT
            // unit-1 weight, only 21 are exactly 1.0 -- 90.4 % author something else, spread
            // continuously from 0.15 to 0.9 -- and another 37 animate it with 8 more driven by a
            // global sequence. ps8 is the same picture: 262 of its 293 constants (89.4 %) are not
            // 1.0. A field set deliberately on nine batches out of ten, to values that only make
            // sense as a dimmer, is not dead data.
            //
            // So the lobe is restored WEIGHTED BY UNIT 1'S OWN TEXTURE WEIGHT, which is what ps20
            // and ps23 already do here (Lobe2Weighted) and on the same kind of evidence. Measured:
            // on Algalon, both Celestial Serpents, the Celestial Fox Wyvern and Val'kier -- five
            // models whose ps10 batches bind NO unit-1 weight, so the weight is 1 -- the weighted
            // and raw forms are identical to the pixel. On the knife the clipped fraction goes
            // 5.75 % raw to 1.90 %, against 0.77 % with no lobe at all, and the blade's engraving
            // stays legible instead of washing out.
            //
            // ps8 IS STILL HELD BACK, and now for a reason that is about coverage rather than
            // correctness: its 1,277 batches include the character EYES geoset (3301), which
            // 52,180 of 120,978 CreatureDisplayInfo rows resolve to, and none of that has been
            // looked at. The shape is identical -- Plan(0, 4, 1f, true, false, 4, true) -- and the
            // weight evidence above is as strong for it. It is a validation job, not a decode one.
            case 8:  return Plan(0, 4, 1f, true);    // Mod_Add   -- lobe held back, see above
            // Mod_Add_Alpha. THE ONE THAT DOES NOT FOLLOW THE PATTERN:
            //   specular = tex2.rgb * (1.0 - tex1.a)   (ModelRenderPass.cpp:128)
            // There is no tex2.a in it. Every other second-unit lobe in the table multiplies by
            // the lobe texture's own alpha and this one does not, so it gets its own shape rather
            // than ps14's. Unit 1 was already bound here for the alpha mode (tex1.a + tex2.a).
            case 21: return Plan(0, 4, 1f, true, false, 3);

            // products of the two units
            case 5:  return Plan(1, 0, 1f, true);    // Opaque_Opaque
            case 2:  return Plan(1, 2, 1f, true);    // Opaque_Mod
            case 6:  return Plan(1, 3, 1f, true);    // Mod_Mod
            case 36: return Plan(1, 3, 1f, true);    // Mod_Mod_Depth
            case 11: return Plan(1, 1, 1f, true);    // Mod_Opaque
            case 4:  return Plan(2, 0, 1f, true);    // Opaque_Mod2xNA
            case 3:  return Plan(2, 2, 2f, true);    // Opaque_Mod2x
            case 7:  return Plan(2, 3, 2f, true);    // Mod_Mod2x
            case 9:  return Plan(2, 1, 1f, true);    // Mod_Mod2xNA

            // masked by unit 0's own alpha -- on a creature skin that channel is a reflection
            // mask, not transparency
            case 12: return Plan(12, 0, 1f, true);   // Opaque_Mod2xNA_Alpha
            // ..._Add. The diffuse half is ps12's; the "_Add" half is a THIRD unit added as an
            // authored luminous term. Retail computes both -- combiners_uber_3_3.bls case 15 is
            // mix(t0*t1*2, t0, t0.a) into r6 and t2.rgb*t2.a*cb0[6].z into r7, added at the end --
            // and the legacy viewport has the same two terms at ModelRenderPass.cpp:122 and :156.
            case 15: return Plan(12, 0, 1f, true, true);
            // Mod_AddAlpha_Alpha. ps14's lobe with a LUMINANCE-WEIGHTED discard alpha, and that
            // alpha is why this one sat in the default arm rather than beside 14:
            //   specular      = tex2.rgb * tex2.a * (1 - tex1.a)
            //   discard_alpha = tex1.a + tex2.a * (0.3*r + 0.59*g + 0.11*b)   (:124)
            // Retail agrees in both switches: the colour arm at asm 2_2/0.asm:215 is byte-for-byte
            // ps14's, and the alpha is `dp3 r1.y, r6.xyzx, l(0.3, 0.59, 0.11)` then
            // `mad r4.w, r6.w, r1.y, r3.w` (:347-352). Those are the gamma-domain NTSC weights,
            // which is the right domain for this shader since the working-space fix.
            case 17: return Plan(0, 5, 1f, true, false, 2);
            case 22: return Plan(3, 0, 1f, true);    // Opaque_ModNA_Alpha
            case 29: return Plan(4, 0, 1f, true);    // Opaque_Alpha (decal)
            // Opaque_Alpha_Alpha: ps29's decal diffuse, plus a lobe on the FIRST unit.
            //   mat_diffuse = mix(tex1.rgb, tex2.rgb, tex2.a)
            //   specular    = tex1.rgb * tex1.a * u_tex_sample_alpha.r    (:131)
            // Retail: `mad r5.xyz, r4.wwww, r6.xyzx, r3.xyzx` is the decal mix, then
            // `mul r6.xyz, r3.wwww, r3.xyzx` / `mul r4.xyz, r6.xyzx, cb0[22].xxxx` is the lobe --
            // unit 0's own colour and alpha, scaled by unit 0's weight (asm 2_2/0.asm:249-253,
            // and identically at :386-391). Alpha is 1.
            //
            // Unit 0's weight is the SAME track that feeds the pass opacity, so this value is used
            // twice on a ps24 batch. That is not double-counting an error: the legacy does the
            // same -- WoWModel.cpp:1807 assigns transLookup[transid] to pass->opacity, and its
            // ps24 arm multiplies the lobe by u_tex_sample_alpha.r, which is that same entry.
            case 24: return WithLobe0(Plan(4, 0, 1f, true));

            default:
            {
                // Not implemented: draw unit 0 with its own alpha, which is what every one of
                // these did before any combiner existed, and say so once per material.
                CombinerPlan p = Plan(0, 1, 1f, false);
                p.Known = false;
                return p;
            }
        }
    }

    /// <summary>
    /// The GL state one M2 blend mode produces, read off the legacy viewport's own switch
    /// (ModelRenderPass::init). Mode 0 sets a blend function but never enables blending, and mode
    /// 1 sets One/Zero with an alpha test -- both are opaque, which is why they were already right
    /// before this. The rest were being drawn opaque too, which they are not: an additive glow
    /// rendered opaque is a solid box where a wisp of light belongs.
    ///
    /// opaqueAlpha follows the combiner's own final-opacity rule: only modes 0 and 1 ignore the
    /// alpha the combiner built (they output mesh opacity, 1 for a static pose); every other mode
    /// outputs it.
    /// </summary>
    static void ApplyBlendMode(Material m, M2BlendMode mode, bool depthWriteDisabled,
                               float cutoff, out string described)
    {
        var src = UnityEngine.Rendering.BlendMode.One;
        var dst = UnityEngine.Rendering.BlendMode.Zero;
        int queue = (int)UnityEngine.Rendering.RenderQueue.Geometry;
        string renderType = "Opaque";
        bool alphaTest = false, opaqueAlpha = true;
        // Additive batches are emissive: the preview rig must not dim them (see _Emissive in
        // WmvOpaque.shader). Alpha-blended batches are ordinary lit surfaces seen through
        // transparency and stay lit.
        bool emissive = false;

        switch (mode)
        {
            case M2BlendMode.Opaque:                                       // One/Zero, no blending
                described = "opaque";
                break;

            case M2BlendMode.AlphaKey:                                     // One/Zero + alpha test
                alphaTest = m.HasProperty("_Cutoff") || m.HasProperty("_AlphaCutoff");
                if (alphaTest)
                {
                    queue = (int)UnityEngine.Rendering.RenderQueue.AlphaTest;
                    renderType = "TransparentCutout";
                }
                described = alphaTest ? "alpha clip (cutoff " + cutoff.ToString("0.###") + ")"
                                      : "opaque (the shader exposes no alpha cutoff)";
                break;

            case M2BlendMode.Alpha:                                        // SrcAlpha/OneMinusSrcAlpha
                src = UnityEngine.Rendering.BlendMode.SrcAlpha;
                dst = UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                described = "alpha blend";
                break;

            case M2BlendMode.NoAlphaAdd:                                     // One/One
                src = UnityEngine.Rendering.BlendMode.One;
                dst = UnityEngine.Rendering.BlendMode.One;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                emissive = true;
                described = "additive";
                break;

            case M2BlendMode.Add:                                // SrcAlpha/One
                src = UnityEngine.Rendering.BlendMode.SrcAlpha;
                dst = UnityEngine.Rendering.BlendMode.One;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                emissive = true;
                described = "additive (alpha-weighted)";
                break;

            case M2BlendMode.Mod:                                     // DstColor/Zero
                src = UnityEngine.Rendering.BlendMode.DstColor;
                dst = UnityEngine.Rendering.BlendMode.Zero;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                described = "modulate";
                break;

            case M2BlendMode.Mod2x:                                   // DstColor/SrcColor
                src = UnityEngine.Rendering.BlendMode.DstColor;
                dst = UnityEngine.Rendering.BlendMode.SrcColor;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                described = "modulate 2x";
                break;

            default:                                                       // mode 7: One/OneMinusSrcAlpha
                src = UnityEngine.Rendering.BlendMode.One;
                dst = UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
                opaqueAlpha = false;
                queue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
                renderType = "Transparent";
                described = "premultiplied blend";
                break;
        }

        // Depth write is the model's own flag and NOTHING else. The legacy viewport sets it from
        // one unconditional test outside its blend switch, so a blended pass whose flag is clear
        // still writes depth. Inferring "this mode blends, therefore no depth write" would look
        // reasonable and diverge on every such pass.
        bool zwrite = !depthWriteDisabled;

        if (m.HasProperty("_Mode")) m.SetFloat("_Mode", alphaTest ? 1f : 0f);
        if (m.HasProperty("_Surface")) m.SetFloat("_Surface", opaqueAlpha ? 0f : 1f);
        if (m.HasProperty("_Blend")) m.SetFloat("_Blend", 0f);
        if (m.HasProperty("_AlphaClip")) m.SetFloat("_AlphaClip", alphaTest ? 1f : 0f);
        if (m.HasProperty("_SrcBlend")) m.SetInt("_SrcBlend", (int)src);
        if (m.HasProperty("_DstBlend")) m.SetInt("_DstBlend", (int)dst);
        if (m.HasProperty("_Emissive")) m.SetFloat("_Emissive", emissive ? 1f : 0f);
        if (m.HasProperty("_ZWrite")) m.SetInt("_ZWrite", zwrite ? 1 : 0);
        if (m.HasProperty("_OpaqueAlpha")) m.SetFloat("_OpaqueAlpha", opaqueAlpha ? 1f : 0f);
        if (m.HasProperty("_Cutoff")) m.SetFloat("_Cutoff", cutoff);
        if (m.HasProperty("_AlphaCutoff")) m.SetFloat("_AlphaCutoff", cutoff);

        m.DisableKeyword("_ALPHATEST_ON");
        m.DisableKeyword("_ALPHABLEND_ON");
        m.DisableKeyword("_ALPHAPREMULTIPLY_ON");
        m.DisableKeyword("_SURFACE_TYPE_TRANSPARENT");
        if (alphaTest) m.EnableKeyword("_ALPHATEST_ON");
        if (!opaqueAlpha) m.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");

        m.SetOverrideTag("RenderType", renderType);
        m.renderQueue = queue;

        if (depthWriteDisabled)
            described += ", no depth write (model flag)";
    }

    /// <summary>
    /// Resolve one TEXTURE UNIT of a batch to an M2 texture slot.
    ///
    /// A batch declares how many textures it loads (TextureCount) and where its run starts in the
    /// model's texture-combo table (TextureComboIndex); unit k is entry TextureComboIndex + k.
    /// Reading only the first entry -- which is what this used to do -- silently discards every
    /// unit past the first, and with it every multi-texture effect the model asks for.
    ///
    /// Unit 0 falls back to slot 0 when the lookup is malformed, because a model with no texture
    /// at all is worse than a model with the wrong one. A higher unit gets no such courtesy:
    /// guessing a texture for it would invent an effect the model never asked for.
    /// </summary>
    static int ResolveTextureSlot(M2ParsedModel model, M2Batch batch, int unit)
    {
        if (unit < 0 || unit >= batch.TextureCount)
            return -1;

        int comboIndex = batch.TextureComboIndex + unit;
        if (comboIndex >= 0 && comboIndex < model.TextureLookup.Length)
        {
            int slot = model.TextureLookup[comboIndex];
            if (slot >= 0 && slot < model.Textures.Length)
                return slot;
        }
        return (unit == 0 && model.Textures.Length > 0) ? 0 : -1;
    }

    /// <summary>
    /// Does the model want this batch drawn in its rest pose?
    ///
    /// A model hides geometry it is not currently using by keying an animation track to zero
    /// rather than by leaving the geometry out: an eye overlay, a glow, a blink. chicken2 is the
    /// worked example -- its 18-triangle eye batch points at a colour entry whose alpha track is
    /// 0, and the actual eye is painted into the skin texture on the head underneath. Drawing
    /// that batch anyway covers the painted eye with a flat red patch.
    ///
    /// This mirrors the legacy viewport's own gate (ModelRenderPass::init):
    ///
    ///     ocol.w &gt; 0  &amp;&amp;  (colorIndex == -1 || ecol.w &gt; 0)
    ///
    /// where ocol.w comes from the texture-weight track and ecol.w from the colour entry's alpha
    /// track -- and, note, ecol stays zero when the colour entry has no RGB track at all, so such
    /// a batch is hidden too. Only animation 0 at time 0 is considered: this milestone renders a
    /// static pose. When the model carries none of these tracks every batch is visible, which is
    /// the behaviour before any of this was read.
    /// </summary>
    /// <summary>
    /// Which array entry a batch's texture weight resolves to, the way the legacy viewport does it
    /// (opacity = transLookup[transid], then a range check; WoWModel.cpp:1807 and
    /// ModelRenderPass.cpp:438-440). -1 when the batch has none.
    /// </summary>
    static int ResolveWeightIndex(M2ParsedModel model, M2Batch batch)
    {
        // Through the lookup table only, as the legacy does (WoWModel.cpp:1807); the resolution
        // lives with the evaluator so the parser tests can reach it.
        return M2MaterialEval.ResolveWeightIndex(model, batch);
    }

    /// <summary>
    /// Which texture transform a unit resolves to: lookup[combo + unit], valid only when the
    /// value indexes the array (WoWModel.cpp:1850-1866: "if (a0 &lt; nAnims)"). -1 otherwise.
    /// </summary>
    static int ResolveTransformIndex(M2ParsedModel model, M2Batch batch, int unit)
    {
        return M2MaterialEval.ResolveTransformIndex(model, batch, unit);
    }

    /// <summary>
    /// Does this batch's visibility gate change over time? A weight or colour-alpha track with
    /// more than one key, or one on a global sequence, can open and close; a single key cannot.
    /// </summary>
    static bool GateAnimates(M2ParsedModel model, M2Batch batch)
    {
        int w = ResolveWeightIndex(model, batch);
        if (w >= 0)
        {
            M2Track<float> t = model.TextureWeightTracks[w];
            if (t.HasData && (t.Values.Length > 1 || t.IsGlobal))
                return true;
        }
        if (batch.HasColor && batch.ColorIndex < model.Colors.Length)
        {
            M2Track<float> o = model.Colors[batch.ColorIndex].Opacity;
            if (o.HasData && (o.Values.Length > 1 || o.IsGlobal))
                return true;
        }
        return false;
    }

    static WmvMaterialAnimBinding ResolveAnimBinding(M2ParsedModel model, M2Batch batch, int materialIndex,
                                                     M2BlendMode mode, CombinerPlan plan,
                                                     bool unit0Env, bool unit1Env, bool useUnit1,
                                                     bool unit2Env, bool useUnit2,
                                                     Action<string> log)
    {
        var b = new WmvMaterialAnimBinding();
        b.Material = materialIndex;
        b.Submesh = batch.SubmeshIndex;
        b.Color = batch.HasColor && batch.ColorIndex < model.Colors.Length ? batch.ColorIndex : -1;
        b.Weight = ResolveWeightIndex(model, batch);
        // Unit 2's own weight, for a combiner that actually has a third unit. Resolved only when
        // the lobe will consume it, so a two-unit material's animator wake-up condition does not
        // change. See M2MaterialEval.ResolveWeightIndex(model, batch, unit).
        b.Weight1 = plan.Lobe2Weighted && useUnit1
                    ? M2MaterialEval.ResolveWeightIndex(model, batch, 1) : -1;
        b.Weight2 = plan.Lobe && useUnit2 ? M2MaterialEval.ResolveWeightIndex(model, batch, 2) : -1;
        b.Static = batch.Static;
        b.Unit0Env = unit0Env;
        b.Unit1Env = unit1Env;
        b.Opaque = mode == M2BlendMode.Opaque;
        b.AlphaKey = mode == M2BlendMode.AlphaKey;
        b.Unlit = batch.MaterialIndex < model.Materials.Length && model.Materials[batch.MaterialIndex].Unlit;
        b.BaseAlphaScale = plan.NeedsUnit1 && useUnit1 ? plan.AlphaScale : 1f;
        // The legacy viewport assigns a transform only when the STATIC bit is clear
        // (WoWModel.cpp:1848), and skips loading it on an environment unit (ModelRenderPass.cpp
        // :630). Unit 1 exists only for a two-unit material (WoWModel.cpp:1861 "shaderTexCount > 1").
        b.Transform0 = -1;
        b.Transform1 = -1;
        b.Transform2 = -1;
        if (!b.Static)
        {
            int t0 = ResolveTransformIndex(model, batch, 0);
            int t1 = batch.TextureCount >= 2 ? ResolveTransformIndex(model, batch, 1) : -1;
            // UNIT 2 TAKES UNIT 0's MATRIX, NOT ITS OWN COMBO ENTRY.
            //
            // The M2 reserves a texture_transform_combos slot for a third unit, and reading it is
            // the obvious thing to do -- but retail does not. All 40 vertex programs of the
            // Diffuse_T1_Env_T1 family compute ONE texture matrix and write the same register to
            // both texcoord outputs:
            //     dp3 r1.x, cb0[3].xywx, r0.xyzx
            //     dp3 r1.y, cb0[4].xywx, r0.xyzx
            //     mov o5.xy, r1.xyxx      ; unit 0's coordinate
            //     mov o6.xy, r1.xyxx      ; unit 2's -- the same one
            // (combiners_uber_3_3.bls, vertex program 0, :81-84). The pixel shader then samples t0
            // at that interpolant and t2 at v6.xy. There is no third matrix anywhere in the program.
            //
            // On the benchmark shoulder this changes nothing -- its texture_transform_combos are
            // [65535, 0, 65535] and combo 0 resolves both ways to "none" -- so it is a latent
            // correctness fix, for a ps15 material that scrolls its unit 0.
            int t2 = batch.TextureCount >= 3 ? t0 : -1;
            b.Transform0 = unit0Env ? -1 : t0;
            b.Transform1 = unit1Env || !useUnit1 ? -1 : t1;
            b.Transform2 = unit2Env || !useUnit2 ? -1 : t2;
            if (log != null && (t0 >= 0 && unit0Env || t1 >= 0 && unit1Env))
                log(string.Format("matanim: submesh {0} has a texture transform on an ENVIRONMENT unit " +
                                  "(unit 0 -> {1}, unit 1 -> {2}); not applied -- a sphere map is not " +
                                  "a stored coordinate", batch.SubmeshIndex, t0, t1));
        }
        else if (log != null && ResolveTransformIndex(model, batch, 0) >= 0)
            log(string.Format("matanim: submesh {0} is STATIC (flag 0x10): its texture transform {1} " +
                              "is not applied, as the legacy viewport does not assign it",
                              batch.SubmeshIndex, ResolveTransformIndex(model, batch, 0)));

        // The resolution itself, for any batch that could have been animated: which combo index
        // it carried, what the lookup said, and what that became. A batch that "should scroll"
        // but does not is answered from this line, not by re-deriving it.
        if (log != null && (b.Color >= 0 || b.Weight >= 0 || b.Transform0 >= 0 || b.Transform1 >= 0 ||
                            batch.TextureTransformComboIndex != 0xFFFF))
        {
            int combo = batch.TextureTransformComboIndex;
            string l0 = combo < model.TextureTransformLookup.Length
                ? model.TextureTransformLookup[combo].ToString() : "past lookup";
            string l1 = combo + 1 < model.TextureTransformLookup.Length
                ? model.TextureTransformLookup[combo + 1].ToString() : "past lookup";
            // The unit-2 weight is stated with its track shape, because it is the ps15 lobe's
            // gain and "which track, animated or not" is the whole question about it.
            string w2 = "none";
            if (b.Weight2 >= 0 && b.Weight2 < model.TextureWeightTracks.Length)
            {
                M2Track<float> wt = model.TextureWeightTracks[b.Weight2];
                float lo = 1f, hi = 1f;
                for (int k = 0; k < wt.Values.Length; k++)
                {
                    if (k == 0 || wt.Values[k] < lo) lo = wt.Values[k];
                    if (k == 0 || wt.Values[k] > hi) hi = wt.Values[k];
                }
                w2 = string.Format("track {0}, {1} key(s), {2}, range {3:F4}..{4:F4}",
                                   b.Weight2, wt.Values.Length,
                                   wt.IsGlobal ? "global sequence " + wt.GlobalSequence : "sequence-local",
                                   lo, hi);
            }
            int wcombo = batch.TextureWeightComboIndex;
            string wl = wcombo + 2 < model.TextureWeightLookup.Length
                ? model.TextureWeightLookup[wcombo + 2].ToString() : "past lookup";
            log(string.Format("matanim: submesh {0} binds colour {1} weight {2} xf0 {3} xf1 {4} " +
                              "(combo {5} -> lookup[{5}]={6}, lookup[{7}]={8}; {9} transform(s) in " +
                              "the model; units {10}; static {11}; unit0 {12}; unit1 {13})",
                              batch.SubmeshIndex, b.Color, b.Weight, b.Transform0, b.Transform1,
                              combo, l0, combo + 1, l1, model.TextureTransforms.Length,
                              batch.TextureCount, b.Static ? "yes" : "no",
                              unit0Env ? "env" : "stored", unit1Env ? "env" : (useUnit1 ? "stored" : "unused")));
            log(string.Format("matanim: submesh {0} unit-2 weight (the ps15 lobe's gain): {1} " +
                              "(weight combo {2} -> lookup[{3}]={4})",
                              batch.SubmeshIndex, w2, wcombo, wcombo + 2, wl));
            if (plan.Lobe2Weighted)
            {
                string w1 = "none";
                if (b.Weight1 >= 0 && b.Weight1 < model.TextureWeightTracks.Length)
                {
                    M2Track<float> wt1 = model.TextureWeightTracks[b.Weight1];
                    float lo1 = 1f, hi1 = 1f;
                    for (int k = 0; k < wt1.Values.Length; k++)
                    {
                        if (k == 0 || wt1.Values[k] < lo1) lo1 = wt1.Values[k];
                        if (k == 0 || wt1.Values[k] > hi1) hi1 = wt1.Values[k];
                    }
                    w1 = string.Format("track {0}, {1} key(s), {2}, range {3:F4}..{4:F4}",
                                       b.Weight1, wt1.Values.Length,
                                       wt1.IsGlobal ? "global sequence " + wt1.GlobalSequence : "sequence-local",
                                       lo1, hi1);
                }
                string wl1 = wcombo + 1 < model.TextureWeightLookup.Length
                    ? model.TextureWeightLookup[wcombo + 1].ToString() : "past lookup";
                log(string.Format("matanim: submesh {0} unit-1 weight (the ps20/23 lobe's gain): {1} " +
                                  "(weight combo {2} -> lookup[{3}]={4})",
                                  batch.SubmeshIndex, w1, wcombo, wcombo + 1, wl1));
            }
        }
        return b;
    }

    /// <summary>Is submesh i switched on by the current geoset selection?</summary>
    public static bool GeosetVisibleFor(WmvRuntimeModel runtime, int i)
    {
        if (runtime == null || i < 0 || i >= runtime.SubmeshGeosets.Length)
            return true;
        if (i < runtime.SubmeshIndices.Length)
            return SubmeshDrawn(runtime.SubmeshIndices[i], runtime.SubmeshGeosets[i], runtime.Geosets);
        return GeosetVisible(runtime.SubmeshGeosets[i], runtime.Geosets);
    }

    static bool MaterialsAnimate(WmvRuntimeModel runtime)
    {
        return runtime.MaterialAnimator != null && runtime.MaterialAnimator.AnimatedCount > 0;
    }

    /// <summary>Does anything on this model need the animator's clock -- a material track, or an
    /// emitter? A model with neither is posed once and left alone.</summary>
    static bool NeedsClock(WmvRuntimeModel runtime)
    {
        return MaterialsAnimate(runtime) || HasEmitters(runtime);
    }

    static bool HasEmitters(WmvRuntimeModel runtime)
    {
        return runtime.Emitters != null && runtime.Emitters.HasAnything;
    }

    /// <summary>
    /// Add the emitter runtime to a model that has emitters, and nothing at all to one that does
    /// not -- no component, no GameObject, no per-frame call. That is the "models with no
    /// particles or ribbons cost nothing" property, and it is structural rather than a fast path
    /// inside a component that runs anyway.
    /// </summary>
    static void BuildEmitters(WmvRuntimeModel result, M2ParsedModel model, GameObject go,
                              Transform[] boneTransforms,
                              Dictionary<int, BlpImage> decodedTextures,
                              Dictionary<int, Texture2D> textureCache, List<Texture2D> owned,
                              string objectName, Action<string> log)
    {
        if (Debug_.NoEmitters)
        {
            if (log != null && (model.ParticleEmitterCount > 0 || model.RibbonEmitterCount > 0))
                log("emitters: not drawn -- -wmvNoEmitters was passed");
            return;
        }
        if (model.ParticleEmitters.Length == 0 && model.RibbonEmitters.Length == 0)
        {
            // Say so only when the model DECLARED some and none survived the parse, so a model
            // with no emitters at all stays silent instead of adding a line to every load.
            if (log != null && (model.ParticleEmitterCount > 0 || model.RibbonEmitterCount > 0))
                log(string.Format("emitters: {0} particle and {1} ribbon emitter(s) declared, none "
                                  + "readable", model.ParticleEmitterCount, model.RibbonEmitterCount));
            return;
        }

        Shader shader = FindParticleShader(log);
        if (shader == null)
            return;

        var runtime = go.AddComponent<WmvEmitterRuntime>();
        // A particle's texture field indexes the model's texture array DIRECTLY -- the legacy
        // resolves it with getGLTexture(mta.texture), which is textures[Tex] (WoWModel.cpp:3596).
        // Not through TextureLookup, which is the batch's route and a different table.
        //
        // Address mode comes from the texture's own two flag bits, the same rule the mesh
        // materials follow, except that a flipbook must never wrap: a tile's edge samples would
        // pull in the opposite side of the sheet.
        Func<int, Texture2D> textureFor = slot =>
        {
            if (slot < 0 || slot >= model.Textures.Length)
                return null;
            bool wrapX = model.Textures[slot].WrapX;
            bool wrapY = model.Textures[slot].WrapY;
            return GetTexture(decodedTextures, textureCache, owned, slot, false, wrapX, wrapY,
                              objectName);
        };
        runtime.SetGlobalSequences(model.GlobalSequences);
        runtime.Setup(model, go.transform, boneTransforms, textureFor, shader, objectName, log);
        if (!runtime.HasAnything)
        {
            UnityEngine.Object.Destroy(runtime);
            return;
        }
        result.Emitters = runtime;
    }

    /// <summary>
    /// The particle pass, from Resources so a player build cannot strip it.
    ///
    /// No fallback chain, deliberately. The model shader can fall back to a pipeline Lit shader
    /// and still show the model; a particle shader cannot, because a substitute bakes its own
    /// blend state into the pass and every M2 blend mode this renderer sets would be ignored --
    /// 56 % of emitters are additive and would come out as opaque squares over the model. Missing
    /// is better than wrong, and it is said in the log.
    /// </summary>
    const string ParticleShaderResource = "WmvParticle";
    static Shader cachedParticleShader;
    static bool particleShaderResolved;

    static Shader FindParticleShader(Action<string> log)
    {
        if (particleShaderResolved)
            return cachedParticleShader;
        particleShaderResolved = true;
        cachedParticleShader = Resources.Load<Shader>(ParticleShaderResource);
        if (cachedParticleShader == null && log != null)
            log("emitters: not drawn -- the '" + ParticleShaderResource + "' shader is not in this "
                + "build, and a substitute would bake its own blend state over every M2 blend mode");
        return cachedParticleShader;
    }

    /// <summary>
    /// Put the whole model -- bones and materials -- at one instant of its animation and hold it
    /// there. For the offscreen captures: two builds compared at "250 ms" must both mean the
    /// same 250 ms, on the same two clocks.
    /// </summary>
    public static void PoseAt(WmvRuntimeModel runtime, float timeMs)
    {
        if (runtime == null)
            return;
        WmvM2Animator.GlobalTimeMs = timeMs;
        if (Debug_.MatDump && runtime.MaterialAnimator != null)
            runtime.MaterialAnimator.DumpNext();      // the dump describes THIS instant, not the build's t = 0
        if (runtime.Animator != null)
            runtime.Animator.ApplyPose(runtime.Animator.SequenceTimeAt(timeMs));   // drives the materials through its hook
        else if (runtime.MaterialAnimator != null)
            runtime.MaterialAnimator.Apply(timeMs);

        // Bones and materials are functions of the instant; emitters are not. A particle at
        // 250 ms exists because of what happened over the 250 ms before it, so holding the clock
        // still leaves an emitter empty. Run it forward to the instant instead, deterministically,
        // which is what makes two captures at "250 ms" comparable rather than merely labelled the
        // same. See WmvEmitterRuntime.SimulateTo.
        if (runtime.Emitters != null && runtime.Emitters.HasAnything && runtime.Animator != null)
        {
            WmvM2Animator anim = runtime.Animator;
            runtime.Emitters.SimulateTo(timeMs, anim.SequenceTimeAt, anim.ApplyPose);
            anim.ApplyPose(anim.SequenceTimeAt(timeMs));   // the simulation left the bones mid-run
        }
    }

    static bool BatchIsVisible(M2ParsedModel model, M2Batch batch, out string reason)
    {
        reason = null;

        // texture weight (the transparency track), through the lookup table only
        if (model.TextureWeights.Length > 0)
        {
            int weightIndex = batch.TextureWeightComboIndex < model.TextureWeightLookup.Length
                ? model.TextureWeightLookup[batch.TextureWeightComboIndex] : -1;

            if (weightIndex >= 0 && weightIndex < model.TextureWeights.Length &&
                model.TextureWeights[weightIndex] <= 0f)
            {
                reason = "texture weight " + weightIndex + " is 0";
                return false;
            }
        }

        // colour entry alpha
        if (batch.HasColor && model.Colors.Length > 0)
        {
            if (batch.ColorIndex >= model.Colors.Length)
                return true;                       // out of range: not our call to hide it
            M2ColorDef c = model.Colors[batch.ColorIndex];
            if (!c.HasColorTrack)
            {
                reason = "colour " + batch.ColorIndex + " has no track for animation 0";
                return false;
            }
            if (c.Alpha <= 0f)
            {
                reason = "colour " + batch.ColorIndex + " alpha is 0";
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// Upload (or reuse) one texture slot. Slots are shared between batches, so the cache is keyed
    /// by slot AND by how the alpha channel was treated -- the same slot can feed one batch whose
    /// alpha is the combiner mask and another where it is discarded.
    /// </summary>
    static Texture2D GetTexture(Dictionary<int, BlpImage> decodedTextures,
                                Dictionary<int, Texture2D> cache, List<Texture2D> owned,
                                int slot, bool dropAlpha, bool wrapX, bool wrapY, string objectName)
    {
        if (slot < 0 || decodedTextures == null)
            return null;
        BlpImage decoded;
        if (!decodedTextures.TryGetValue(slot, out decoded) || decoded == null)
            return null;

        // The cache key carries the address mode as well, because the same M2 slot can feed one
        // batch that repeats it and another that clamps it. Keyed on the alpha treatment alone,
        // whichever batch was built first would silently decide the wrap for both.
        int key = slot * 8 + (dropAlpha ? 4 : 0) + (wrapX ? 2 : 0) + (wrapY ? 1 : 0);
        Texture2D tex;
        if (cache.TryGetValue(key, out tex))
            return tex;

        tex = CreateTexture(decoded, objectName + "_tex" + slot + (dropAlpha ? "_opaque" : ""),
                            dropAlpha);
        // Per axis, from the texture's own flags: the legacy viewport sets GL_REPEAT on the axis
        // whose bit is set and restores GL_CLAMP_TO_EDGE otherwise
        // (Source/games/wow/ModelRenderPass.cpp:537-540 and :324-328), reading the same two bits
        // (Source/games/wow/WoWModel.cpp:1836-1837, TEXTURE_WRAPX = 1, TEXTURE_WRAPY = 2).
        tex.wrapModeU = wrapX ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        tex.wrapModeV = wrapY ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
        cache[key] = tex;
        owned.Add(tex);
        return tex;
    }

    /// <summary>
    /// How one texture unit should address outside 0..1.
    ///
    /// From the texture's own two flag bits, except for a unit fed by the generated sphere map:
    /// that coordinate is not a stored UV set and must never wrap, whatever the texture says,
    /// or the reflection tiles at the silhouette.
    /// </summary>
    static void TextureWrap(M2ParsedModel model, int slot, bool env, out bool wrapX, out bool wrapY)
    {
        wrapX = wrapY = false;
        if (env || slot < 0 || model == null || slot >= model.Textures.Length)
            return;
        wrapX = model.Textures[slot].WrapX;
        wrapY = model.Textures[slot].WrapY;
    }

    /// <summary>
    /// Can the resolved shader run the M2 combiner? Only the renderer's own WmvOpaque shader can;
    /// a pipeline Lit shader has one texture slot and no idea what a sphere-mapped second unit is.
    /// Probed by property rather than by name so a future shader gets the same treatment for free.
    /// </summary>
    static bool ShaderHasCombiner()
    {
        if (combinerProbed)
            return combinerAvailable;
        combinerProbed = true;
        Shader s = FindRenderShader(null);
        if (s == null)
            return false;
        var probe = new Material(s);
        combinerAvailable = probe.HasProperty(CombinerModeProperty);
        UnityEngine.Object.Destroy(probe);
        return combinerAvailable;
    }

    const string CombinerModeProperty = "_CombinerMode";
    const string SecondTexProperty = "_SecondTex";
    const string FirstUnitLobeProperty = "_FirstUnitLobe";
    const string FirstUnitWeightProperty = "_FirstUnitWeight";
    const string SecondUnitLobeProperty = "_SecondUnitLobe";
    const string SecondUnitWeightProperty = "_SecondUnitWeight";
    const string ThirdTexProperty = "_ThirdTex";
    const string ThirdUnitLobeProperty = "_ThirdUnitLobe";
    const string ThirdUnitWeightProperty = "_ThirdUnitWeight";

    /// <summary>
    /// IS THE ps15 LUMINOUS LOBE ADDED TO THE FRAME? Currently NO, deliberately.
    ///
    /// Everything the lobe needs is built and bound: the host forwards the real texture-type-3
    /// binding it already made, unit 2 is resolved with its own coordinate source, address mode
    /// and transform, and _ThirdTex carries it. Only the last step -- adding
    /// t2.rgb * t2.a * scale to the shaded colour -- is withheld, because the SCALE is not known.
    ///
    /// Retail scales that term by cb0[6].z, a per-material colour constant (the same cb0[6] is
    /// the "Const" of Combiners_Mod_Mod_Mod_Const in the same program). Its draw-time value is
    /// not recoverable from the shader binary, and 1.0 -- the only value with a source behind it,
    /// this repository's own u_tex_sample_alpha placeholder at ModelRenderPass.cpp:656 -- was
    /// measured to roughly DOUBLE the albedo of the armour benchmark, because on armour the host
    /// binds texture type 3 to the item skin and ps15 samples it at unit 0's UV set, so
    /// t2 == t0 and that skin's alpha averages 229/255. Broad doubling is not the authored glow.
    ///
    /// So the transport lands and the arithmetic waits. Flipping this to true is the whole of
    /// re-enabling it, once the retail scale and the retail type-3 binding are settled.
    /// See parity/ps15-third-unit-implementation.txt.
    /// </summary>
    const bool ThirdUnitLobeArmed = true;
    static bool combinerProbed, combinerAvailable;

    /// <summary>
    /// Upload textures undecoded (see CreateTexture). False restores the old sRGB-sampled
    /// behaviour so both can be rendered from one build; WmvMain.ConfigureDisplayTransform sets
    /// it from WMV_DISPLAY.
    /// </summary>
    public static bool AuthoredTextureDomain = true;

    static Texture2D CreateTexture(BlpImage img, string name, bool dropAlpha)
    {
        // ROW ORDER: a BLP stores its rows top-down (row 0 = top of the image), but a Unity
        // texture's raw data starts at the BOTTOM-left. Uploading the decoded bytes as-is
        // therefore lands the image upside down -- and since the UV converter already flips V
        // for Unity's bottom-up convention, the two flips cancel out and the model comes back
        // textured with the wrong part of the sheet. Flip the rows once, here, and the standard
        // Unity convention holds everywhere downstream.
        int stride = img.Width * 4;
        var pixels = new byte[img.Rgba.Length];
        for (int y = 0; y < img.Height; y++)
            Buffer.BlockCopy(img.Rgba, y * stride, pixels, (img.Height - 1 - y) * stride, stride);

        // ALPHA: on an opaque WoW material the alpha channel is not transparency, and on a real
        // creature skin it is nowhere near solid -- chicken2's is DXT5 with ~97% of its texels
        // below 255 (min 15, mean 240). WoW ignores it; anything on this side that lets it reach
        // a blend or an alpha test instead draws the bird as overlapping translucent shells. So
        // for an opaque material the channel is discarded here rather than trusted downstream.
        if (dropAlpha)
            for (int i = 3; i < pixels.Length; i += 4) pixels[i] = 255;

        // The decoder hands over mip 0 only. Upload it with SetPixelData(level 0) and let
        // Apply(updateMipmaps: true) build the rest -- LoadRawTextureData on a mip-chained
        // texture would expect data for every level and reject a mip-0-sized array.
        // COLOUR DOMAIN. linear:true does NOT mean "this image is linear light" -- it means
        // "do not apply a transfer function when sampling", i.e. hand the shader the byte the
        // artist painted. That is what WoW's own combiner and this application's OpenGL viewport
        // both operate on, and WmvOpaque.shader is written for that domain and encodes once at
        // the end. Leaving it sRGB silently decoded every texel and made every authored additive
        // term land at roughly two thirds of its intended screen value.
        //
        // It also fixes mip generation: Unity averages the stored values, which is what the
        // legacy viewport's GL_RGBA8 mip chain does too. Under the sRGB flag the averaging
        // happened in a different domain from the one the material is defined in.
        var tex = new Texture2D(img.Width, img.Height, TextureFormat.RGBA32, true,
                                AuthoredTextureDomain) { name = name };
        tex.SetPixelData(pixels, 0);
        tex.Apply(true, false);
        tex.wrapMode = TextureWrapMode.Repeat;
        tex.filterMode = FilterMode.Bilinear;
        return tex;
    }

    /// <summary>
    /// Minimum viable material for this milestone. Two rules matter more than fidelity here:
    ///
    ///   * the WoW blend mode decides transparency, never the texture. A creature skin carries an
    ///     alpha channel whether or not the model wants blending, so keying off the texture turns
    ///     a solid bird see-through.
    ///   * the render state is set EXPLICITLY rather than inherited from the shader's defaults,
    ///     AND the shader is chosen so that setting it actually does something -- see
    ///     FindRenderShader. A shader that bakes its blending into the pass ignores every
    ///     property written here without complaining.
    ///
    /// This is deliberately not a full WoW material system; see ApplyBlendMode and PlanCombiner
    /// for exactly how much of one it is.
    /// </summary>
    static Material CreateMaterial(M2MaterialDef def, M2BlendMode mode, CombinerPlan plan,
                                   M2UvSource unit0Uv, M2UvSource unit1Uv, M2UvSource unit2Uv,
                                   Texture2D tex, Texture2D unit1Tex, Texture2D unit2Tex,
                                   string name, Action<string> log)
    {
        var shader = FindRenderShader(log);
        // Material(null) throws; Unity's error shader is always present and at least draws
        // geometry, so a missing shader degrades to a visible (magenta) mesh, not a failed load.
        var m = new Material(shader != null ? shader : Shader.Find("Hidden/InternalErrorShader"))
        {
            name = name
        };
        if (tex != null && !Debug_.MatColors) m.mainTexture = tex;
        if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness", 0f);      // Built-in
        else if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness", 0f); // URP/HDRP

        // The combiner properties exist only on the renderer's own shader. On a pipeline Lit
        // shader every one of these is a no-op and the material keeps its single texture, which
        // is the honest outcome: that shader cannot run an M2 combiner.
        bool combining = plan.NeedsUnit1 && unit1Tex != null && !Debug_.MatColors &&
                         m.HasProperty(CombinerModeProperty);
        if (m.HasProperty(CombinerModeProperty))
        {
            if (combining) m.SetTexture(SecondTexProperty, unit1Tex);
            m.SetFloat(CombinerModeProperty, combining ? plan.Mode : 0);
            // The second unit's additive lobe rides on the same binding as the combiner's second
            // texture, so it is armed by the same condition and dies with it.
            if (m.HasProperty(FirstUnitLobeProperty))
                m.SetFloat(FirstUnitLobeProperty, combining && plan.Lobe0 ? 1f : 0f);
            if (m.HasProperty(FirstUnitWeightProperty))
                m.SetFloat(FirstUnitWeightProperty, 1f);
            if (m.HasProperty(SecondUnitLobeProperty))
                m.SetFloat(SecondUnitLobeProperty, combining ? plan.Lobe2 : 0);
            // 1 until the animator writes the model's own track over it, which it does for every
            // binding at Setup and again on every sequence change. Only a material with no
            // animator binding at all keeps this value.
            if (m.HasProperty(SecondUnitWeightProperty))
                m.SetFloat(SecondUnitWeightProperty, 1f);
            // ALPHA-KEY KEYS ON ITS OWN TEXTURE, whatever combiner it resolved to.
            //
            // Without the second clause an alpha-keyed batch whose combiner yields alpha mode 0 --
            // Combiners_Opaque, which is exactly what a cutout batch normally carries, since the
            // key comes from the blend mode and not from the combiner -- writes _AlphaMode 0. The
            // shader's alpha then keeps its 1.0 default and the clip can never fire, so hair
            // cards, fur flaps and foliage draw as opaque rectangles and cast matching shadows.
            // The texture's alpha was there all along: alphaIsRead keeps it for every non-opaque
            // blend mode.
            //
            // Confined to the !combining arm ON PURPOSE. That arm is the set of batches the
            // legacy viewport sends to its fixed-function path, where it keys unconditionally on
            // the base texture's alpha. The combining arm keeps the plan's own value, because
            // several two-unit combiners deliberately do not discard, and on those the base
            // texture's alpha is a reflection mask rather than transparency -- forcing a key
            // there would punch holes in creature skins.
            m.SetFloat("_AlphaMode", combining
                ? plan.AlphaMode
                : ((plan.AlphaMode == 1 || mode == M2BlendMode.AlphaKey) ? 1 : 0));
            m.SetFloat("_AlphaScale", combining ? plan.AlphaScale : 1f);
            m.SetFloat("_Unit1UV", (float)(int)unit1Uv);
            m.SetFloat("_Unit0UV", (float)(int)unit0Uv);

            // THE THIRD UNIT'S LUMINOUS LOBE.
            //
            // Armed only when the combiner asks for it AND a real third texture arrived. There is
            // deliberately no fallback to unit 0: the same M2 batch shape appears on armour, where
            // the host binds the item skin to texture type 3, and on weapons, where it binds
            // ArmorReflect4 -- and nothing the renderer can see tells those apart. Substituting
            // unit 0 would paint a weapon's own diffuse on as a glow. The host forwards the
            // binding it already made, and this samples exactly that.
            bool lobe = ThirdUnitLobeArmed &&
                        plan.Lobe && unit2Tex != null && !Debug_.MatColors &&
                        m.HasProperty(ThirdUnitLobeProperty);
            if (m.HasProperty(ThirdTexProperty) && lobe)
                m.SetTexture(ThirdTexProperty, unit2Tex);
            if (m.HasProperty(ThirdUnitLobeProperty))
                m.SetFloat(ThirdUnitLobeProperty, lobe ? 1f : 0f);
            // The lobe's gain. 1 until the animator writes the model's own track over it, which it
            // does for every binding at Setup and again on every sequence change -- so this value
            // only survives on a material with no animator binding at all.
            if (m.HasProperty(ThirdUnitWeightProperty))
                m.SetFloat(ThirdUnitWeightProperty, 1f);
            if (m.HasProperty("_Unit2UV"))
                m.SetFloat("_Unit2UV", (float)(int)unit2Uv);
        }

        string treatedAs;
        // The legacy combiner keys at 128/255, not at a rounded half.
        ApplyBlendMode(m, mode, def.DepthWriteDisabled, 128f / 255f, out treatedAs);

        // THE UNLIT RENDER FLAG, honoured at last.
        //
        // An unlit material is light the surface EMITS, not light it receives: an eye, a rune, a
        // lantern flame, a spirit's body. The flag has been parsed since the parser was written
        // and read by nothing, so such a batch went through the preview rig and shaded down to
        // its ambient floor -- a self-lit thing rendered as a dark painted patch. Additive
        // batches already took the emissive path; this adds the other half of the legacy's own
        // definition, which disables lighting for exactly these passes.
        //
        // It must come AFTER ApplyBlendMode, which writes _Emissive unconditionally and would
        // otherwise overwrite this with 0. It changes no rig constant: _Emissive is the existing
        // bypass, and it is already gated so the legacy A/B rig is untouched.
        if (def.Unlit && m.HasProperty("_Emissive"))
            m.SetFloat("_Emissive", 1f);

        // Only the model's own flag may disable culling; nothing here turns it off globally.
        if (m.HasProperty("_Cull"))
            m.SetFloat("_Cull", (float)(def.TwoSided
                ? UnityEngine.Rendering.CullMode.Off
                : UnityEngine.Rendering.CullMode.Back));

        if (Debug_.MatColors)
        {
            var c = DebugColors[Mathf.Abs(name.GetHashCode()) % DebugColors.Length];
            if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
            if (m.HasProperty("_Color")) m.SetColor("_Color", c);
        }

        LogMaterialState(m, def, mode, treatedAs, log);
        return m;
    }

    /// <summary>
    /// Report what the material ACTUALLY is at runtime, not what it was asked to be. A property
    /// the shader does not expose reads "n/a", which is the interesting case: it could not be set,
    /// so whatever the shader bakes into its pass is what gets drawn.
    /// </summary>
    static void LogMaterialState(Material m, M2MaterialDef def, M2BlendMode mode,
                                 string treatedAs, Action<string> log)
    {
        if (log == null) return;
        Shader s = m.shader;
        log(string.Format(
            "material '{0}': shader '{1}' (shader default queue {2}) -> treated as {3}; " +
            "renderQueue {4} RenderType '{5}' _Mode {6} _Surface {7} _AlphaClip {8} _SrcBlend {9} " +
            "_DstBlend {10} _ZWrite {11} _Cull {12} _CombinerMode {13} keywords [{14}]; " +
            // The three surface-state values this stage decides. Without them in the log a run
            // cannot say whether an alpha-key batch can actually clip, whether an unlit batch
            // took the emissive path, or which coordinate unit 0 sampled -- and "the numbers did
            // not move" is then indistinguishable from "the fix never reached the material".
            "_AlphaMode {18} _Emissive {19} _Unit0UV {20} _Unit1UV {21}; " +
            // The third unit: which coordinate it samples, and whether its luminous lobe is armed.
            // _ThirdUnitLobe 1 means a real host-forwarded type-3 texture reached this material.
            "_Unit2UV {23} _ThirdUnitLobe {24}; " +
            "WoW blend {15} depthWriteOff {16} twoSided {17} unlit {22}",
            m.name, s != null ? s.name : "<none>", s != null ? s.renderQueue : -1, treatedAs,
            m.renderQueue, m.GetTag("RenderType", false, "<unset>"),
            Prop(m, "_Mode"), Prop(m, "_Surface"), Prop(m, "_AlphaClip"), Prop(m, "_SrcBlend"),
            Prop(m, "_DstBlend"), Prop(m, "_ZWrite"), Prop(m, "_Cull"), Prop(m, CombinerModeProperty),
            string.Join(",", m.shaderKeywords), mode, def.DepthWriteDisabled, def.TwoSided,
            Prop(m, "_AlphaMode"), Prop(m, "_Emissive"), Prop(m, "_Unit0UV"), Prop(m, "_Unit1UV"),
            def.Unlit, Prop(m, "_Unit2UV"), Prop(m, ThirdUnitLobeProperty)));

        if (!shaderCanRenderOpaque)
            log("material '" + m.name + "': the resolved shader bakes blending, depth and culling " +
                "into its pass, so the state above is ADVISORY ONLY -- it cannot be applied and " +
                "the model will draw see-through. Add 'Standard' (or your pipeline's Lit shader) " +
                "to Project Settings > Graphics > Always Included Shaders and rebuild the player.");
    }

    /// <summary>A property's value, or "n/a" when the shader does not expose it.</summary>
    static string Prop(Material m, string name)
    {
        return m.HasProperty(name) ? m.GetFloat(name).ToString("0.##") : "n/a";
    }

    /// <summary>
    /// Name of the shader shipped in Assets/Resources/. A Resources folder is never stripped from
    /// a player build, which makes this the renderer's only guaranteed-present shader.
    /// </summary>
    const string OpaqueShaderResource = "WmvOpaque";

    // Resolved once per session; Shader.Find is not free and the answer cannot change.
    static Shader cachedShader;
    static bool shaderResolved;
    static bool shaderCanRenderOpaque;

    /// <summary>
    /// A shader to draw with, whatever pipeline the project uses and whatever survived build
    /// stripping.
    ///
    /// Two things make this harder in a PLAYER than in the editor: Shader.Find only sees shaders
    /// actually included in the build (a shader no asset references is stripped -- which is why a
    /// runtime-built material can come out magenta even though the project looks fine in the
    /// editor), and the name differs per render pipeline.
    ///
    /// The third thing, and the one that actually broke the render: a candidate is not usable just
    /// because Shader.Find returned it. Sprites/Default is always included, so it always "wins" a
    /// naive search -- and it bakes Blend One OneMinusSrcAlpha, ZWrite Off and Cull Off into its
    /// pass. Those are not material properties, so nothing this class writes can undo them, and an
    /// opaque WoW material silently renders as overlapping translucent shells. Every candidate is
    /// therefore screened by Accept() before it is taken, and the search starts from the active
    /// pipeline's own default material rather than from a name.
    ///
    /// Never throws: drawing the mesh with a fallback shader beats failing the load. If nothing
    /// opaque-capable exists, that is said plainly in the log rather than rendered quietly wrong.
    /// </summary>
    public static Shader ResolveShader(Action<string> log) { return FindRenderShader(log); }

    static Shader FindRenderShader(Action<string> log)
    {
        if (shaderResolved)
            return cachedShader;
        shaderResolved = true;

        // A shader written for one render pipeline draws MAGENTA under another, so the pipeline
        // decides which names may even be tried. null here means the built-in pipeline.
        bool builtIn = UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline == null;

        // THE VIEWER'S OWN SHADER IS THE FIRST CHOICE, not the last resort.
        //
        // It used to be the bottom rung, tried only once every pipeline Lit shader had been
        // stripped out of the build -- which is what happens in practice, so this is the shader
        // that has been drawing every model all along. That made two things accidental that
        // should not be:
        //
        //   THE M2 COMBINER. A pipeline Lit shader cannot run it. Every combiner case this
        //   renderer implements -- chicken2's pixel shader 12, the environment sheen on metal,
        //   the masked modulates -- worked only because URP/Lit happened to be absent. A build
        //   that kept it would have rendered those materials plainly and silently.
        //
        //   THE LOOK. Lit means physically-based: specular response, tonemapping, scene lights.
        //   WoW's textures are hand-painted with their shading already in them, so PBR relights
        //   what is effectively an already-lit painting, and the result is shiny where it should
        //   be matte and dark where the artist put mid-tones. It also made the viewer's
        //   appearance depend on what a given build stripped.
        //
        // So the choice is now deliberate and identical everywhere. The Lit and Unlit rungs
        // below are kept as a real fallback for a build where Resources somehow did not ship,
        // and -wmvLitShader asks for the pipeline's Lit shader explicitly for comparison.
        if (!Debug_.LitShader &&
            Accept(Resources.Load<Shader>(OpaqueShaderResource),
                   "using the renderer's own '" + OpaqueShaderResource + "' shader (M2 combiners "
                   + "and the preview light rig; pass -wmvLitShader for the pipeline's Lit shader)",
                   log))
            return cachedShader;

        // Fallback only, or -wmvLitShader. Shader.Find only sees what survived build stripping,
        // and a runtime-built material references nothing at build time, so any of these can come
        // back null in a player that looks fine in the editor.
        string[] lit = builtIn
            ? new[] { "Standard", "Legacy Shaders/Diffuse", "Mobile/Diffuse" }
            : new[] { "Universal Render Pipeline/Lit", "Universal Render Pipeline/Simple Lit",
                      "HDRP/Lit" };
        foreach (var name in lit)
            if (Accept(Shader.Find(name), "using shader '" + name + "'", log))
                return cachedShader;

        // Next: the material Unity gives a new primitive. Unity picks it from the ACTIVE pipeline,
        // so it is pipeline-correct by construction -- when it exists at all. In a stripped player
        // it can come back as the magenta error shader, which Accept() rejects.
        var probe = GameObject.CreatePrimitive(PrimitiveType.Quad);
        var probeRenderer = probe != null ? probe.GetComponent<MeshRenderer>() : null;
        var probeMaterial = probeRenderer != null ? probeRenderer.sharedMaterial : null;
        var probeShader = probeMaterial != null ? probeMaterial.shader : null;
        if (probe != null) UnityEngine.Object.Destroy(probe);
        if (Accept(probeShader, "no named lit shader survived build stripping -- using the active " +
                                "pipeline's default material shader '" +
                                (probeShader != null ? probeShader.name : "?") + "'", log))
            return cachedShader;

        // The guarantee. Assets/Resources/WmvOpaque.shader ships with the renderer and anything
        // under a Resources folder is always included in the build, so this rung cannot be
        // stripped away like the named lookups above. Unlit, but opaque, textured and with its
        // render state exposed as real properties -- which is what actually matters here.
        if (Accept(Resources.Load<Shader>(OpaqueShaderResource),
                   "falling back to the renderer's own '" + OpaqueShaderResource + "' shader", log))
            return cachedShader;

        // Unlit but still opaque: flat lighting, correct solidity.
        string[] unlit = builtIn
            ? new[] { "Unlit/Texture" }
            : new[] { "Universal Render Pipeline/Unlit", "HDRP/Unlit" };
        foreach (var name in unlit)
            if (Accept(Shader.Find(name),
                       "falling back to the unlit shader '" + name + "'", log))
                return cachedShader;

        // Nothing opaque-capable left -- which should now mean Assets/Resources went missing.
        // Take a blended shader only so that something is visible at all, and say exactly what it
        // will look like and how to fix it.
        cachedShader = Shader.Find("Sprites/Default");
        shaderCanRenderOpaque = false;
        if (log != null)
            log(cachedShader != null
                ? "no opaque-capable shader found in this build -- falling back to 'Sprites/Default', " +
                  "which forces alpha blending, no depth write and no back-face culling. The model " +
                  "WILL look see-through. Copy Assets/Resources/ into the Unity project (it holds " +
                  "the renderer's own opaque shader) and rebuild the player."
                : "no shader could be resolved at all -- the model will draw with Unity's error " +
                  "shader (magenta). Copy Assets/Resources/ into the Unity project and rebuild " +
                  "the player.");
        return cachedShader;
    }

    /// <summary>
    /// Take a candidate only if an opaque WoW material can actually be drawn solid with it.
    /// A shader either exposes the blend/depth knobs (_ZWrite, _SrcBlend, _Surface, _Mode) so the
    /// state can be set, or it is opaque already -- which its own default render queue reports:
    /// opaque shaders sort in Geometry, baked-transparent ones in Transparent. A shader that is
    /// neither controllable nor opaque (Sprites/Default, the UI and particle shaders) would
    /// swallow every state call in silence, so it is skipped and the reason logged.
    /// </summary>
    static bool Accept(Shader s, string message, Action<string> log)
    {
        if (s == null)
            return false;

        // The error shader draws magenta by definition, and it sorts in the geometry queue, so
        // the opacity test below would happily wave it through. It is never an answer.
        if (s.name != null && s.name.StartsWith("Hidden/"))
        {
            if (log != null)
                log("skipping shader '" + s.name + "': it is one of Unity's internal shaders " +
                    "(the magenta error shader is what a stripped build hands back here)");
            return false;
        }

        var probe = new Material(s);
        bool controllable = probe.HasProperty("_ZWrite") || probe.HasProperty("_SrcBlend") ||
                            probe.HasProperty("_Surface") || probe.HasProperty("_Mode");
        UnityEngine.Object.Destroy(probe);

        bool naturallyOpaque = s.renderQueue < (int)UnityEngine.Rendering.RenderQueue.AlphaTest;
        if (!controllable && !naturallyOpaque)
        {
            if (log != null)
                log("skipping shader '" + s.name + "': it bakes transparency into its pass " +
                    "(default queue " + s.renderQueue + ", no blend or depth properties), so an " +
                    "opaque WoW material could not be drawn solid with it");
            return false;
        }

        cachedShader = s;
        shaderCanRenderOpaque = true;
        if (log != null) log(message);
        return true;
    }
}
