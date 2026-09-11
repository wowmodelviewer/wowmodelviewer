// WmvEmitterRuntime.cs
//
// Draws an M2's particle and ribbon emitters. Until this file existed the Unity renderer parsed
// neither and drew neither: a search of the builder for "particle" or "ribbon" returned one hit,
// and it was a comment about shader names. The Lich King's four emitters, the phoenix's
// thirty-four, and the glow motes on every one of the validation weapons were simply absent.
//
// ============================================================================================
// WHAT DRIVES IT: THE MODEL'S OWN CLOCK, NEVER Time.time
// ============================================================================================
// Every quantity here advances on the animation clock WmvM2Animator already owns. Tick() is
// called from that animator's LateUpdate, after the bones are posed, with the animation's own
// advance for this frame -- dt * speed while playing, and exactly ZERO while paused. That is
// what the legacy viewport does: ModelCanvas::tick sets ddt = 0 when the animation is paused and
// passes the same ddt to both the bone tick and updateEmitters (modelcanvas.cpp:1515-1522,
// WoWModel.cpp:2647-2649). A paused model's particles hang in the air; they do not keep raining.
//
// Track values are sampled at the animator's sequence time, and tracks bound to a global
// sequence at the global clock, through the same evaluator the bones use. Nothing in this file
// reads Time.time, and the only Time.deltaTime is the one the animator already turned into an
// animation advance before calling in.
//
// ============================================================================================
// WHY THERE IS NO ParticleSystem AND NO GameObject PER PARTICLE
// ============================================================================================
// One GameObject, one Material and one Mesh PER EMITTER -- not per particle. A particle is
// sixteen floats in a flat array; a frame writes four vertices for each live one into arrays
// that were allocated when the model loaded and are never reallocated. So a 21-emitter staff is
// 21 draw calls whatever the particle count, and the steady state allocates nothing.
//
// Unity's own ParticleSystem was not used. It cannot express what these emitters do -- the
// three-stop life ramp, the M2 blend modes, tile flipbooks driven by a model clock that pauses,
// bone-local versus model-space integration chosen per emitter by a flag -- and configuring one
// per emitter at runtime would be more machinery than the 200 lines that do it directly.
//
// ============================================================================================
// WHERE THIS DIVERGES FROM Source/games/wow/particle.cpp, AND WHY
// ============================================================================================
// The legacy runtime is the reference, and it is followed except where measurement against real
// client files shows it is wrong. Every divergence is listed here so none of them is a silent
// preference:
//
//  1. GRAVITY. 82.9 % of the client's emitters set flag 0x800000, which changes how a gravity
//     KEY is encoded. particle.cpp:38 reads every gravity track as float32 regardless; on those
//     emitters that decodes to NaN or infinity 47 % of the time. See M2Parser.ReadCompressedGravity.
//  2. PARTICLE SIZE. particle.cpp:55 multiplies ramp stop i by ModelParticleParams.scales[i].
//     scales is a per-AXIS scale, not a per-stop one -- see M2ParticleEmitterDef.ParticleScale.
//     Under the legacy reading Val'anyr's 0.0556 particles come out 28.85 units across.
//  3. RAMP LENGTH. particle.cpp:48 memcpys three stops whatever the count says. Emitters that
//     author two are common, so the third stop is unauthored bytes. The count is honoured here.
//  4. HORIZONTAL SPREAD. particle.cpp:563 passes the VERTICAL range as both angles for a plane
//     emitter, leaving HorizontalRange -- a field whose own comment reads "They can do it
//     horizontally too! (range: 0 to 2*pi)" -- unread. Val'anyr's motes author pi/2 vertical and
//     2*pi horizontal, i.e. a full skirt, and get a quarter of it.
//  5. SPHERE DIRECTION. particle.cpp:711 tests flag 0x100, which is nameless and set on 7.5 % of
//     emitters, to choose between emitting radially and emitting along the bone. The format has
//     MODELPARTICLE_FLAGS_OUTWARD (0x20000) for exactly that, set on 93.3 %. The documented bit
//     is used. The two populations very nearly coincide, so this changes little and says why.
//  6. SPHERE AXIS SWAP. particle.cpp:697-699 swaps a spawn direction's y and z with no comment,
//     in a function whose neighbours are labelled "TODO: fix sphere emitters to work properly".
//     Dropped; the cone points along the bone's own axis like the plane emitter's does.
//  7. GRAVITY DIRECTION. particle.cpp:602 sets down = -Z for a plane emitter and :721 sets
//     down = +Z for a sphere, so the same authored gravity would pull one down and push the
//     other up. -Z (Unity -Y) for both.
//  8. THE FLAG SPECIAL CASES at particle.cpp:566-594 compare the WHOLE flags word against 1041,
//     25 and 17. No current-client emitter can match: Val'anyr's are 0x24021, 0x30031 and
//     0x20221. They are dead code on this data and are not reproduced.
//  9. THE TILE FLIPBOOK. particle.cpp:626 always picks a random tile. The format has
//     RANDOMTEXTURE (0x10000, 33.9 %) for that, and RANDOMSTART (0x200000, 10.1 %) -- a flag
//     that only means anything if there is a progression to start partway through. So a tile
//     advances over the particle's life unless RANDOMTEXTURE fixes it at a random one. For the
//     59.8 % of emitters that are 1x1 the two are identical.
// 10. RIBBON GEOMETRY -- see RibbonState, where the legacy's own TODO and two glm mistakes are
//     set out in full.
//
// zSource is not implemented, exactly as the legacy does not implement it. It measures as 255.0
// on the emitters that set it, and one line of header comment is not enough to build motion from.
// Spline emitters (0.8 % of the client) are not drawn, and the legacy builds no emitter for them
// either (particle.cpp:117-120).

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

/// <summary>
/// Every particle and ribbon emitter of one model. Added by WmvModelBuilder to the model root, so
/// it dies with the model and a switch cannot leave the previous model's effects on screen.
/// </summary>
public class WmvEmitterRuntime : MonoBehaviour
{
    // ---- tuning, all with a reason ----------------------------------------------------------

    /// <summary>
    /// Ceiling on the live particles ONE emitter may hold. The legacy's equivalent is 10,000 per
    /// system (particle.cpp:13); that is a per-system number and this renderer sizes per emitter
    /// from the emitter's own authored rate and lifespan, so the ceiling only has to stop a
    /// pathological track from eating memory. The 99th-percentile model has 12 emitters.
    /// </summary>
    const int MaxParticlesPerEmitter = 4096;

    /// <summary>Floor on that capacity, so a rate track that is zero in this sequence but not in
    /// the next does not have to reallocate when the sequence changes.</summary>
    const int MinParticlesPerEmitter = 32;

    /// <summary>Ceiling on a ribbon's stored edges. res * lifetime measures 5..200 across the
    /// client; 512 leaves headroom without letting a bad track run away.</summary>
    const int MaxRibbonEdges = 512;

    /// <summary>
    /// The fixed step SimulateTo uses to reach a pinned instant. 60 Hz because that is the rate
    /// a capture is taken at; the point is that a given timestamp always produces the SAME frame,
    /// so a BEFORE and an AFTER can be compared pixel for pixel.
    /// </summary>
    const float PinnedStepSeconds = 1f / 60f;

    /// <summary>How far a pinned simulation will run. Two seconds of particles at 60 Hz is 120
    /// steps; the guard is against a pin far in the future turning into a stall.</summary>
    const float PinnedMaxSeconds = 30f;

    // ---- state ------------------------------------------------------------------------------

    class ParticleState
    {
        public M2ParticleEmitterDef Def;
        public Transform Bone;
        public Matrix4x4 BindPose;          // see BindPoseFor
        public Vector3 Origin;              // Def.Position, in Unity space
        public GameObject Go;
        public Mesh Mesh;
        public Material Material;
        public MeshRenderer Renderer;

        /// <summary>
        /// Does a particle live in the bone's space or in the model's?
        ///
        /// DONOTTRAIL (0x10, 44.6 % of the client) says the particle is carried by the emitter
        /// rather than left behind in the world, and that is the whole difference: a trailing
        /// particle has the bone matrix baked into its position once, at spawn, and then moves on
        /// its own; a pinned one keeps a bone-local position and is transformed by the bone every
        /// frame. The legacy holds exactly these two cases (particle.cpp:429-432, :623-624) but
        /// hides the second behind a static switch nothing turns on, so it has always drawn the
        /// first for every emitter.
        /// </summary>
        public bool BoneLocal;

        public int Count;
        public int Capacity;
        public Vector3[] Pos;               // in BoneLocal ? bone space : model space
        public Vector3[] Vel;
        public float[] Life;
        public float[] MaxLife;
        public int[] TileSeed;              // the flipbook offset chosen at spawn
        public Vector3[] QuadRight;         // only allocated when the emitter does not billboard
        public Vector3[] QuadUp;

        public float SpawnRemainder;
        public uint Rng;

        /// <summary>Which quads each particle is drawn as. An emitter that names neither style
        /// gets a head, which is what every particle was before the styles were read.</summary>
        public bool DrawHead, DrawTail;
        public int QuadsPerParticle;

        public Vector3[] Verts;
        public Vector2[] Uvs;
        public Color32[] Colors;
        public int[] Indices;
        public int TileCount;

        /// <summary>
        /// The RGB ramp actually drawn with: the emitter's authored colour stops, or the same
        /// stops recoloured by a ParticleColor override. RampTimes is parallel to it.
        ///
        /// RGB ONLY. The alpha ramp is a SEPARATE track with its own stop count and its own
        /// times, and it is sampled separately at draw time -- see BuildParticleMesh. Folding it
        /// onto these stops loses whatever it does between them, and that is not a corner case:
        /// Val'anyr's motes author two colour stops, at 0.0 and 1.0, and an alpha ramp that is
        /// zero at both ends and 1.0 at 0.2. Resampled onto the colour's stops the whole ramp
        /// reads zero and the particles are invisible -- which is exactly what they were.
        /// </summary>
        public Color[] RampColor = new Color[0];
        public float[] RampTimes = new float[0];
    }

    class RibbonState
    {
        public M2RibbonEmitterDef Def;
        public Transform Bone;
        public Matrix4x4 BindPose;
        public Vector3 Origin;
        public GameObject Go;
        public Mesh Mesh;
        public Material Material;
        public MeshRenderer Renderer;

        // A ring of edges, oldest at Head. Each edge is a point on the ribbon's spine plus the
        // "up" it was emitted with, both in MODEL space, and the animation time it was laid down.
        public Vector3[] EdgePos;
        public Vector3[] EdgeUp;
        public float[] EdgeAge;             // seconds since the edge was laid down
        public int Head, Count, Capacity;

        public float SinceLastEdge;
        public float EdgeInterval;          // 1 / EdgesPerSecond
        public float Lifetime;              // EdgeLifetimeSeconds
        public bool HasPrevious;

        public Vector3[] Verts;
        public Vector2[] Uvs;
        public Color32[] Colors;
        public int[] Indices;
    }

    readonly List<ParticleState> particles = new List<ParticleState>();
    readonly List<RibbonState> ribbons = new List<RibbonState>();
    readonly List<Material> ownedMaterials = new List<Material>();
    readonly List<Mesh> ownedMeshes = new List<Mesh>();

    Transform modelRoot;
    Camera billboardCamera;

    /// <summary>Start/mid/end RGB per ParticleColor slot (index 0 for emitters whose
    /// ParticleColorIndex is 11, 1 for 12, 2 for 13), or null when the app supplied none.</summary>
    Color[][] particleColorOverride;

    // ---- reported numbers -------------------------------------------------------------------

    public int ParticleEmitterCount { get { return particles.Count; } }
    public int RibbonEmitterCount { get { return ribbons.Count; } }
    public int SkippedEmitterCount { get; private set; }
    public int DrawCallCount { get { return particles.Count + ribbons.Count; } }

    public int LiveParticleCount
    {
        get
        {
            int n = 0;
            for (int i = 0; i < particles.Count; i++) n += particles[i].Count;
            return n;
        }
    }

    public int RibbonSegmentCount
    {
        get
        {
            int n = 0;
            for (int i = 0; i < ribbons.Count; i++) n += ribbons[i].Count;
            return n;
        }
    }

    public int ParticleCapacity
    {
        get
        {
            int n = 0;
            for (int i = 0; i < particles.Count; i++) n += particles[i].Capacity;
            return n;
        }
    }

    public bool HasAnything { get { return particles.Count > 0 || ribbons.Count > 0; } }

    // =========================================================================================
    // Build
    // =========================================================================================

    /// <summary>
    /// Create the renderers for one model's emitters.
    ///
    /// textureFor maps an M2 texture SLOT to an uploaded Texture2D. A particle's texture field is
    /// a DIRECT index into the model's texture array, not a route through TextureLookup -- the
    /// legacy resolves it with getGLTexture(mta.texture), which indexes textures[] (WoWModel.cpp:
    /// 3596-3610). Every slot the M2 names is fetched whether a batch uses it or not
    /// (WmvMain.RequestTextures), so a particle's texture is already in hand by the time the
    /// builder gets here.
    /// </summary>
    public void Setup(M2ParsedModel model, Transform root, Transform[] boneTransforms,
                      Func<int, Texture2D> textureFor, Shader shader, string objectName,
                      Action<string> log)
    {
        Clear();
        modelRoot = root != null ? root : transform;
        SkippedEmitterCount = 0;
        if (model == null || shader == null)
            return;

        int skippedSpline = 0, skippedNoTexture = 0, skippedNoBone = 0;

        for (int i = 0; i < model.ParticleEmitters.Length; i++)
        {
            M2ParticleEmitterDef def = model.ParticleEmitters[i];
            if (!def.Supported) { skippedSpline++; continue; }

            Transform bone = BoneOrNull(boneTransforms, def.Bone);
            if (bone == null && boneTransforms.Length > 0) { skippedNoBone++; continue; }

            // MULTITEXTURE packs three 5-bit slot indices into the texture field
            // (particle.cpp:91-98). This renderer draws the first of them, which is the one the
            // legacy binds to unit 0 and modulates the others onto; the extra units are a
            // brightness detail, not the difference between drawing and not drawing.
            int slot = def.MultiTexture ? (def.TextureId & 0x1f) : def.TextureId;
            Texture2D tex = textureFor != null ? textureFor(slot) : null;
            if (tex == null) { skippedNoTexture++; continue; }

            particles.Add(BuildParticle(def, bone, tex, shader, objectName, i));
        }

        for (int i = 0; i < model.RibbonEmitters.Length; i++)
        {
            M2RibbonEmitterDef def = model.RibbonEmitters[i];
            Transform bone = BoneOrNull(boneTransforms, def.Bone);
            if (bone == null && boneTransforms.Length > 0) { skippedNoBone++; continue; }

            // "just use the first texture for now; most models I've checked only had one"
            // (particle.cpp:754). Still true of this client: no ribbon in the validation set
            // declares more than one.
            int slot = def.TextureIds.Length > 0 ? def.TextureIds[0] : -1;
            Texture2D tex = slot >= 0 && textureFor != null ? textureFor(slot) : null;
            if (tex == null) { skippedNoTexture++; continue; }

            // A ribbon's blend mode comes from the model's MATERIALS array, through the
            // second index array in the emitter (see M2RibbonEmitterDef.MaterialIndex). The
            // legacy never reads it and hardcodes SRC_ALPHA/ONE (particle.cpp:847); that is
            // right for the additive majority and wrong for the rest.
            int blend = 4;
            if (def.MaterialIndex >= 0 && def.MaterialIndex < model.Materials.Length)
                blend = model.Materials[def.MaterialIndex].BlendMode;
            ribbons.Add(BuildRibbon(def, bone, tex, shader, objectName, i, blend));
        }

        SkippedEmitterCount = skippedSpline + skippedNoTexture + skippedNoBone;

        if (log != null && (HasAnything || SkippedEmitterCount > 0))
        {
            log(string.Format("emitters: {0} particle + {1} ribbon drawn of {2} + {3} declared; "
                              + "capacity {4} particle(s)", particles.Count, ribbons.Count,
                              model.ParticleEmitterCount, model.RibbonEmitterCount, ParticleCapacity));
            if (skippedSpline > 0)
                log(string.Format("emitters: {0} spline emitter(s) skipped -- neither renderer "
                                  + "has ever drawn one (0.8 % of the client)", skippedSpline));
            if (skippedNoTexture > 0)
                log(string.Format("emitters: {0} skipped for a texture slot that did not resolve",
                                  skippedNoTexture));
            if (skippedNoBone > 0)
                log(string.Format("emitters: {0} skipped for a bone index outside the skeleton",
                                  skippedNoBone));
        }
    }

    static Transform BoneOrNull(Transform[] bones, int index)
    {
        if (bones == null || index < 0 || index >= bones.Length)
            return null;
        return bones[index];
    }

    /// <summary>
    /// The matrix that takes a point from the model's own space into a bone's local frame, as the
    /// bone sits AT REST.
    ///
    /// THIS IS THE PIECE THAT MAKES AN EMITTER SIT ON ITS BONE. An emitter's position is in MODEL
    /// space; the legacy transforms it by Bone::mat, which is a model-space-to-model-space matrix
    /// -- built as T(pivot) * T(trans) * R(rot) * S(scale) * T(-pivot) and composed through the
    /// parents, so it is the IDENTITY at rest and describes only how far the bone has MOVED.
    /// Unity's bone.localToWorldMatrix is a different animal: it maps the bone's own local frame
    /// to the world, and applying it to a model-space point displaces that point by the bone's
    /// pivot. Composing it with this rest inverse gives back exactly the legacy's quantity:
    ///
    ///     rootInverse * bone.localToWorld * bindPose  ==  identity at rest
    ///
    /// which is the same construction the SkinnedMeshRenderer's own bind poses use
    /// (WmvModelBuilder: bindPoses[i] = bones[i].worldToLocalMatrix * root.localToWorldMatrix).
    /// It is captured HERE because Setup runs while the model is still in its rest pose, before
    /// the animator exists -- reading it later would bake in whatever frame was showing.
    /// </summary>
    Matrix4x4 BindPoseFor(Transform bone)
    {
        if (bone == null || modelRoot == null)
            return Matrix4x4.identity;
        return bone.worldToLocalMatrix * modelRoot.localToWorldMatrix;
    }

    ParticleState BuildParticle(M2ParticleEmitterDef def, Transform bone, Texture2D tex,
                                Shader shader, string objectName, int index)
    {
        var s = new ParticleState();
        s.Def = def;
        s.Bone = bone;
        s.BindPose = BindPoseFor(bone);
        s.Origin = ToUnity(def.Position);
        s.BoneLocal = def.DoNotTrail || def.Pinned;
        s.TileCount = Mathf.Max(1, def.Rows * def.Cols);
        s.Capacity = CapacityFor(def);
        s.DrawTail = def.TailStyle;
        s.DrawHead = def.HeadStyle || !def.TailStyle;
        s.QuadsPerParticle = (s.DrawHead ? 1 : 0) + (s.DrawTail ? 1 : 0);
        s.Rng = (uint)(0x9E3779B9u * (uint)(index + 1) + 0x85EBCA6Bu);

        s.Pos = new Vector3[s.Capacity];
        s.Vel = new Vector3[s.Capacity];
        s.Life = new float[s.Capacity];
        s.MaxLife = new float[s.Capacity];
        s.TileSeed = new int[s.Capacity];
        if (def.DoNotBillboard)
        {
            s.QuadRight = new Vector3[s.Capacity];
            s.QuadUp = new Vector3[s.Capacity];
        }

        int quads = s.Capacity * s.QuadsPerParticle;
        s.Verts = new Vector3[quads * 4];
        s.Uvs = new Vector2[quads * 4];
        s.Colors = new Color32[quads * 4];
        s.Indices = BuildQuadIndices(quads);

        s.Go = NewChild(objectName + "_particles" + index);
        s.Mesh = NewMesh(objectName + "_particleMesh" + index, quads * 4);
        s.Material = NewMaterial(shader, tex, def.BlendMode, objectName + "_particleMat" + index);
        s.Renderer = Attach(s.Go, s.Mesh, s.Material);
        ResolveRamp(s);
        return s;
    }

    RibbonState BuildRibbon(M2RibbonEmitterDef def, Transform bone, Texture2D tex,
                            Shader shader, string objectName, int index, int blend)
    {
        var s = new RibbonState();
        s.Def = def;
        s.Bone = bone;
        s.BindPose = BindPoseFor(bone);
        s.Origin = ToUnity(def.Position);
        s.Lifetime = def.EdgeLifetimeSeconds > 0f ? def.EdgeLifetimeSeconds : 0.5f;
        float rate = def.EdgesPerSecond > 0f ? def.EdgesPerSecond : 30f;
        s.EdgeInterval = 1f / rate;
        // +2: one edge for the head that is still being extended and one so the ring never has
        // to drop an edge that is still inside its lifetime.
        s.Capacity = Mathf.Clamp(Mathf.CeilToInt(rate * s.Lifetime) + 2, 4, MaxRibbonEdges);

        s.EdgePos = new Vector3[s.Capacity];
        s.EdgeUp = new Vector3[s.Capacity];
        s.EdgeAge = new float[s.Capacity];

        s.Verts = new Vector3[s.Capacity * 2];
        s.Uvs = new Vector2[s.Capacity * 2];
        s.Colors = new Color32[s.Capacity * 2];
        s.Indices = BuildStripIndices(s.Capacity);

        s.Go = NewChild(objectName + "_ribbon" + index);
        s.Mesh = NewMesh(objectName + "_ribbonMesh" + index, s.Capacity * 2);
        s.Material = NewMaterial(shader, tex, blend, objectName + "_ribbonMat" + index);
        s.Renderer = Attach(s.Go, s.Mesh, s.Material);
        return s;
    }

    /// <summary>
    /// How many particles this emitter can have alive at once, from its own authored numbers:
    /// the largest rate it reaches times the longest lifespan it reaches, which is the steady
    /// state by definition. Sized from the data rather than from a fixed 10,000 so that a model
    /// with 231 emitters is not 231 x 10,000 slots of memory.
    /// </summary>
    static int CapacityFor(M2ParticleEmitterDef def)
    {
        float rate = TrackMax(def.EmissionRate, 0f);
        float life = TrackMax(def.Lifespan, 0f);
        int need = Mathf.CeilToInt(rate * life * 1.25f) + 4;
        return Mathf.Clamp(need, MinParticlesPerEmitter, MaxParticlesPerEmitter);
    }

    static float TrackMax(M2Track<float> t, float fallback)
    {
        if (!t.HasData)
            return fallback;
        float m = float.NegativeInfinity;
        for (int i = 0; i < t.Values.Length; i++)
            if (t.Values[i] > m) m = t.Values[i];
        return m > 0f ? m : 0f;
    }

    GameObject NewChild(string name)
    {
        var go = new GameObject(name);
        go.transform.SetParent(modelRoot, false);
        go.transform.localPosition = Vector3.zero;
        go.transform.localRotation = Quaternion.identity;
        go.transform.localScale = Vector3.one;
        return go;
    }

    Mesh NewMesh(string name, int vertexCount)
    {
        var m = new Mesh();
        m.name = name;
        m.MarkDynamic();
        // 16-bit indices top out at 65,535 vertices; the caps above stay far below that, but a
        // mesh that silently truncated would be very hard to see.
        if (vertexCount > 65000)
            m.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
        ownedMeshes.Add(m);
        return m;
    }

    MeshRenderer Attach(GameObject go, Mesh mesh, Material mat)
    {
        var mf = go.AddComponent<MeshFilter>();
        mf.sharedMesh = mesh;
        var mr = go.AddComponent<MeshRenderer>();
        mr.sharedMaterial = mat;
        mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        mr.receiveShadows = false;
        mr.lightProbeUsage = UnityEngine.Rendering.LightProbeUsage.Off;
        mr.reflectionProbeUsage = UnityEngine.Rendering.ReflectionProbeUsage.Off;
        return mr;
    }

    // =========================================================================================
    // Materials: the M2 blend modes, as the legacy sets them
    // =========================================================================================

    /// <summary>
    /// One material per emitter, from the same shader the meshes use, with the blend factors the
    /// legacy's ParticleSystem::draw sets for each mode (particle.cpp:306-353).
    ///
    /// Only four of the eight ever appear on a particle in this client: 4 (additive on alpha)
    /// 56.3 %, 2 (alpha blend) 24.9 %, 7 18.6 % and 1 (alpha test) 0.2 %. The rest are wired
    /// anyway, because they cost a switch case and a mode nobody has seen is exactly the one that
    /// would otherwise draw as garbage.
    /// </summary>
    Material NewMaterial(Shader shader, Texture2D tex, int blend, string name)
    {
        var m = new Material(shader);
        m.name = name;
        m.mainTexture = tex;

        UnityEngine.Rendering.BlendMode src, dst;
        bool alphaTest = false;
        switch (blend)
        {
            case 0:  // BM_OPAQUE
                src = UnityEngine.Rendering.BlendMode.One;
                dst = UnityEngine.Rendering.BlendMode.Zero;
                break;
            case 1:  // BM_TRANSPARENT -- ONE/ZERO with the alpha test on
                src = UnityEngine.Rendering.BlendMode.One;
                dst = UnityEngine.Rendering.BlendMode.Zero;
                alphaTest = true;
                break;
            case 3:  // BM_ADDITIVE
                src = UnityEngine.Rendering.BlendMode.SrcColor;
                dst = UnityEngine.Rendering.BlendMode.One;
                break;
            case 4:  // BM_ADDITIVE_ALPHA
                src = UnityEngine.Rendering.BlendMode.SrcAlpha;
                dst = UnityEngine.Rendering.BlendMode.One;
                break;
            case 5:  // BM_MODULATE
                src = UnityEngine.Rendering.BlendMode.DstColor;
                dst = UnityEngine.Rendering.BlendMode.Zero;
                break;
            case 6:  // BM_MODULATEX2
                src = UnityEngine.Rendering.BlendMode.DstColor;
                dst = UnityEngine.Rendering.BlendMode.SrcColor;
                break;
            case 7:  // BM_7, new in WoD
                src = UnityEngine.Rendering.BlendMode.One;
                dst = UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
                break;
            default: // 2, BM_ALPHA_BLEND -- and anything unknown, which the legacy also maps here
                src = UnityEngine.Rendering.BlendMode.SrcAlpha;
                dst = UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha;
                break;
        }
        m.SetFloat("_SrcBlend", (float)src);
        m.SetFloat("_DstBlend", (float)dst);
        m.SetFloat("_AlphaTest", alphaTest ? 1f : 0f);
        ownedMaterials.Add(m);
        return m;
    }

    // =========================================================================================
    // The ParticleColor override
    // =========================================================================================

    /// <summary>
    /// Supply the three ParticleColor stop sets the app resolved for this model, or null to drop
    /// back to what the emitters authored.
    ///
    /// An emitter opts in with ParticleColorIndex 11, 12 or 13, which select set 0, 1 and 2. The
    /// override replaces the RGB of all three ramp stops and LEAVES THE AUTHORED ALPHA ALONE,
    /// which is what the legacy does (particle.cpp:162-172) and is what makes it usable: the
    /// alpha ramp is what fades a particle in and out over its life, and a colour table has no
    /// opinion about that.
    /// </summary>
    public void SetParticleColorOverride(Color[][] sets)
    {
        particleColorOverride = sets;
        for (int i = 0; i < particles.Count; i++)
            ResolveRamp(particles[i]);
    }

    /// <summary>
    /// Build the RGB ramp this emitter draws with.
    ///
    /// Where a ParticleColor override applies it replaces the RGB and NOTHING ELSE -- the
    /// authored alpha ramp is untouched, which is what the legacy does (particle.cpp:162-172,
    /// which copies the replacement colours and then puts the authored alphas back) and what
    /// makes an override usable at all: the alpha ramp is what fades a particle in and out over
    /// its life, and a colour table has no opinion about that.
    /// </summary>
    void ResolveRamp(ParticleState s)
    {
        WowVec3[] keys = s.Def.ColorKeys;
        int idx = s.Def.ParticleColorIndex - 11;
        Color[] over = null;
        if (particleColorOverride != null && idx >= 0 && idx < particleColorOverride.Length &&
            particleColorOverride[idx] != null && particleColorOverride[idx].Length >= 3)
            over = particleColorOverride[idx];

        if (keys == null || keys.Length == 0)
        {
            // No colour ramp: white for its whole life, or the override's own ramp if there is
            // one. The alpha still reaches the particle -- it is a different track.
            s.RampTimes = new float[] { 0f, 0.5f, 1f };
            s.RampColor = over != null
                ? new[] { over[0], over[1], over[2] }
                : new[] { Color.white, Color.white, Color.white };
            return;
        }

        int n = keys.Length;
        float[] times = s.Def.ColorTimes;
        s.RampTimes = times;
        s.RampColor = new Color[n];
        for (int i = 0; i < n; i++)
        {
            float at = (times != null && i < times.Length) ? times[i] : 0f;
            s.RampColor[i] = over != null ? OverrideAt(over, at)
                                          : new Color(keys[i].X, keys[i].Y, keys[i].Z, 1f);
        }
    }

    /// <summary>A ParticleColor row is itself a start/middle/end ramp; sample it at a stop's
    /// time so an override lands on the same curve the emitter authored.</summary>
    static Color OverrideAt(Color[] set, float t)
    {
        return t <= 0.5f ? Color.Lerp(set[0], set[1], t * 2f)
                         : Color.Lerp(set[1], set[2], (t - 0.5f) * 2f);
    }

    static float SampleAlphaRamp(M2ParticleEmitterDef def, float t)
    {
        if (def.AlphaKeys == null || def.AlphaKeys.Length == 0)
            return 1f;
        int a, b;
        float r = RampSpan(def.AlphaTimes, t, out a, out b);
        if (a >= def.AlphaKeys.Length) a = def.AlphaKeys.Length - 1;
        if (b >= def.AlphaKeys.Length) b = def.AlphaKeys.Length - 1;
        return a == b ? def.AlphaKeys[a] : Mathf.Lerp(def.AlphaKeys[a], def.AlphaKeys[b], r);
    }

    // =========================================================================================
    // Per-frame
    // =========================================================================================

    /// <summary>
    /// Advance every emitter by one animation frame and rebuild its mesh.
    ///
    /// dtSeconds is the ANIMATION's advance, not the frame's: zero while paused, scaled by the
    /// playback speed otherwise. sequenceTimeMs and globalTimeMs are the two clocks the tracks
    /// are sampled on, exactly as the bones and materials sample them.
    /// </summary>
    public void Tick(float dtSeconds, float sequenceTimeMs, float globalTimeMs)
    {
        Advance(dtSeconds, sequenceTimeMs, globalTimeMs);
        Build(sequenceTimeMs, globalTimeMs);
    }

    /// <summary>
    /// Move the simulation on by one animation step, WITHOUT touching a mesh.
    ///
    /// Split from the geometry so a pinned capture can run a hundred and twenty steps to reach
    /// its instant and upload once, instead of uploading a frame nobody will ever see per step.
    /// </summary>
    public void Advance(float dtSeconds, float sequenceTimeMs, float globalTimeMs)
    {
        if (!HasAnything)
            return;
        if (dtSeconds < 0f)
            dtSeconds = -dtSeconds;      // a scrub backwards must not run the simulation backwards

        Matrix4x4 rootInverse = modelRoot.worldToLocalMatrix;

        for (int i = 0; i < particles.Count; i++)
        {
            ParticleState s = particles[i];
            AdvanceParticles(s, BoneMatrix(rootInverse, s.Bone, s.BindPose),
                             dtSeconds, sequenceTimeMs, globalTimeMs);
        }
        for (int i = 0; i < ribbons.Count; i++)
        {
            RibbonState s = ribbons[i];
            AdvanceRibbon(s, BoneMatrix(rootInverse, s.Bone, s.BindPose), dtSeconds);
        }
    }

    /// <summary>
    /// Write the current state into the meshes. Also the whole of a frame in which nothing
    /// advanced -- a paused model still has to face a camera the user is moving.
    /// </summary>
    public void Build(float sequenceTimeMs, float globalTimeMs)
    {
        if (!HasAnything)
            return;
        Matrix4x4 rootInverse = modelRoot.worldToLocalMatrix;
        Vector3 camRight, camUp;
        BillboardBasis(rootInverse, out camRight, out camUp);

        for (int i = 0; i < particles.Count; i++)
        {
            ParticleState s = particles[i];
            BuildParticleMesh(s, BoneMatrix(rootInverse, s.Bone, s.BindPose), camRight, camUp);
        }
        for (int i = 0; i < ribbons.Count; i++)
            BuildRibbonMesh(ribbons[i], sequenceTimeMs, globalTimeMs);
    }

    /// <summary>The legacy's Bone::mat, in this renderer's terms. See BindPoseFor.</summary>
    static Matrix4x4 BoneMatrix(Matrix4x4 rootInverse, Transform bone, Matrix4x4 bindPose)
    {
        if (bone == null)
            return Matrix4x4.identity;
        return rootInverse * bone.localToWorldMatrix * bindPose;
    }

    /// <summary>
    /// The billboard axes, in the model root's space.
    ///
    /// The legacy takes them from the first two rows of the GL modelview matrix
    /// (particle.cpp:406-407), which is the camera's right and up expressed in the space the
    /// particles are in. Same thing, said in Unity's vocabulary.
    /// </summary>
    void BillboardBasis(Matrix4x4 rootInverse, out Vector3 right, out Vector3 up)
    {
        if (billboardCamera == null)
            billboardCamera = Camera.main;
        if (billboardCamera == null)
        {
            right = Vector3.right;
            up = Vector3.up;
            return;
        }
        Transform t = billboardCamera.transform;
        right = rootInverse.MultiplyVector(t.right).normalized;
        up = rootInverse.MultiplyVector(t.up).normalized;
        if (right.sqrMagnitude < 1e-8f) right = Vector3.right;
        if (up.sqrMagnitude < 1e-8f) up = Vector3.up;
    }

    void AdvanceParticles(ParticleState s, Matrix4x4 boneMatrix, float dt,
                          float seqMs, float globalMs)
    {
        // ---- emission -----------------------------------------------------------------------
        // (dt * rate) + carry. emissionRate IS particles per second, so the population settles at
        // rate * lifespan.
        //
        // THIS DELIBERATELY DEPARTS FROM THE LEGACY, which divides by the lifespan
        // (particle.cpp:203-211, `ftospawn = (dt * frate / flife) + rem`) and so settles at rate
        // particles however long they live. That is a bug in the legacy, and three independent
        // witnesses say so:
        //
        //   THE CLIENT. Instrumenting Wowhead's WebGL viewer -- which loads this very file, with
        //   batches confirmed identical -- on Algalon the Observer shows its particle draw issue
        //   1866 indices, i.e. 311 live quads. Algalon authors rate 45 and lifespan 7 s. 45 * 7 is
        //   315; 45 is not 311. Measured here, the same model plateaus at exactly 315.
        //
        //   THIS FILE. CapacityFor (below) sizes the pool as "the largest rate it reaches times
        //   the longest lifespan it reaches, WHICH IS THE STEADY STATE BY DEFINITION" -- rate *
        //   life * 1.25 + 4, which is 398 for Algalon and is what the log prints. The allocator
        //   and the simulator were disagreeing with each other by exactly one lifespan: the pool
        //   was sized for 315 and never held more than 45.
        //
        //   THE RIBBON HALF. Ribbons in this same runtime read edgesPerSecond as a per-second rate
        //   and size themselves rate * lifetime (BuildRibbon). Particles are the same shape of
        //   quantity and were the only ones divided.
        //
        // What it looked like: Algalon's sparkle cloud was a seventh of its authored density -- a
        // dozen specks where the game has a few hundred -- and every emitter in the viewer was
        // thinned by its own lifespan.
        if (dt > 0f)
        {
            float rate = Sample(s.Def.EmissionRate, seqMs, globalMs, 0f);
            float lifespan = Sample(s.Def.Lifespan, seqMs, globalMs, 0f);
            bool enabled = !s.Def.EnabledIn.HasData
                           || Sample(s.Def.EnabledIn, seqMs, globalMs, 1f) != 0f;

            // The lifespan is still what decides whether anything spawns at all: a zero
            // lifespan means a particle dies the instant it is born, and the legacy spawns none
            // rather than dividing by zero (particle.cpp:206-211).
            float toSpawn = lifespan > 0f ? (dt * rate) + s.SpawnRemainder
                                          : s.SpawnRemainder;
            if (toSpawn < 1f)
            {
                s.SpawnRemainder = toSpawn > 0f ? toSpawn : 0f;
            }
            else
            {
                int n = (int)toSpawn;
                s.SpawnRemainder = toSpawn - n;
                if (enabled)
                {
                    float speed = Sample(s.Def.EmissionSpeed, seqMs, globalMs, 0f);
                    float variation = Sample(s.Def.SpeedVariation, seqMs, globalMs, 0f);
                    float vertical = Sample(s.Def.VerticalRange, seqMs, globalMs, 0f);
                    float horizontal = Sample(s.Def.HorizontalRange, seqMs, globalMs, 0f);
                    // areal/areaw are halved before use, so the jitter is +/- half the area and
                    // the emitter spans the authored width (particle.cpp:226-227).
                    float halfLength = Sample(s.Def.EmissionAreaLength, seqMs, globalMs, 0f) * 0.5f;
                    float halfWidth = Sample(s.Def.EmissionAreaWidth, seqMs, globalMs, 0f) * 0.5f;
                    for (int i = 0; i < n && s.Count < s.Capacity; i++)
                        Spawn(s, boneMatrix, lifespan, speed, variation, vertical, horizontal,
                              halfLength, halfWidth);
                }
            }
        }

        // ---- integrate ----------------------------------------------------------------------
        float gravity = Sample(s.Def.Gravity, seqMs, globalMs, 0f);
        Vector3 down = new Vector3(0f, -1f, 0f);      // WoW -Z is Unity -Y
        float slowdown = s.Def.Slowdown;

        for (int i = 0; i < s.Count; )
        {
            s.Vel[i] += down * (gravity * dt);
            float damping = slowdown > 0f ? Mathf.Exp(-slowdown * s.Life[i]) : 1f;
            s.Pos[i] += s.Vel[i] * (damping * dt);
            s.Life[i] += dt;

            if (s.MaxLife[i] <= 0f || s.Life[i] >= s.MaxLife[i])
            {
                // Swap-with-last removal: O(1), no shuffling, no allocation. Order within an
                // emitter is not meaningful -- the legacy draws its list in whatever order the
                // std::list happens to hold, and every blend mode here is order-independent
                // except alpha blend, whose particles are the same texture and colour anyway.
                int last = s.Count - 1;
                s.Pos[i] = s.Pos[last];
                s.Vel[i] = s.Vel[last];
                s.Life[i] = s.Life[last];
                s.MaxLife[i] = s.MaxLife[last];
                s.TileSeed[i] = s.TileSeed[last];
                if (s.QuadRight != null)
                {
                    s.QuadRight[i] = s.QuadRight[last];
                    s.QuadUp[i] = s.QuadUp[last];
                }
                s.Count--;
                continue;
            }
            i++;
        }
    }

    void Spawn(ParticleState s, Matrix4x4 boneMatrix, float lifespan, float speed,
               float variation, float vertical, float horizontal, float halfLength, float halfWidth)
    {
        int i = s.Count++;

        // The spread cone: tilt local +Z by a random angle within the VERTICAL range, then swing
        // it by a random angle within the HORIZONTAL range. Both ranges are full cone angles, so
        // the random half-angle is +/- range/2 -- particle.cpp:521-522.
        float tilt = RandRange(s, -vertical, vertical) * 0.5f;
        float swing = RandRange(s, -horizontal, horizontal) * 0.5f;
        float ct = Mathf.Cos(tilt), st = Mathf.Sin(tilt);
        float cs = Mathf.Cos(swing), ss = Mathf.Sin(swing);

        // Local axes in WoW terms (emit along +Z), written straight in Unity's: WoW +Z is Unity
        // +Y, WoW +X is Unity +Z, WoW +Y is Unity -X.
        Vector3 dirLocal = new Vector3(-st * ss, ct, st * cs);
        Vector3 rightLocal = new Vector3(-cs, 0f, ss);

        Vector3 posLocal;
        if (s.Def.EmitterType == (byte)M2EmitterType.Sphere)
        {
            // A sphere emitter puts the particle ON the shell it just picked a direction on, at a
            // random radius, and (when OUTWARD) sends it along that same radius.
            float radius = RandUnit(s);
            Vector3 shell = new Vector3(dirLocal.x * halfWidth, dirLocal.y * halfLength,
                                        dirLocal.z * halfWidth) * radius;
            posLocal = s.Origin + shell;
            if (!s.Def.Outward && shell.sqrMagnitude > 1e-12f)
                dirLocal = new Vector3(0f, 1f, 0f);
            else if (shell.sqrMagnitude > 1e-12f)
                dirLocal = shell.normalized;
        }
        else
        {
            // A plane emitter scatters over its rectangle. areal runs along the model's forward
            // axis and areaw across it (particle.cpp:596).
            posLocal = s.Origin + new Vector3(-RandRange(s, -halfWidth, halfWidth), 0f,
                                              RandRange(s, -halfLength, halfLength));
        }

        float scale = speed * (1f + RandRange(s, -variation, variation));

        if (s.BoneLocal)
        {
            s.Pos[i] = posLocal;
            s.Vel[i] = dirLocal * scale;
            if (s.QuadRight != null)
            {
                s.QuadRight[i] = rightLocal;
                s.QuadUp[i] = dirLocal;
            }
        }
        else
        {
            s.Pos[i] = boneMatrix.MultiplyPoint3x4(posLocal);
            s.Vel[i] = boneMatrix.MultiplyVector(dirLocal) * scale;
            if (s.QuadRight != null)
            {
                s.QuadRight[i] = boneMatrix.MultiplyVector(rightLocal);
                s.QuadUp[i] = boneMatrix.MultiplyVector(dirLocal);
            }
        }

        s.Life[i] = 0f;
        // A lifespan of zero would divide by zero in the life ramp; the legacy substitutes one
        // second (particle.cpp:619-620).
        s.MaxLife[i] = lifespan > 0f ? lifespan : 1f;
        s.TileSeed[i] = (s.Def.RandomTexture || s.Def.RandomStart)
                        ? (int)(RandUnit(s) * s.TileCount) % s.TileCount : 0;
    }

    // =========================================================================================
    // The life ramps
    // =========================================================================================
    //
    // A ramp is a list of (normalised time, value) stops evaluated piecewise-linearly against a
    // particle's own age -- NOT the legacy's three fixed stops at 0 / 0.5 / 1 (lifeRamp,
    // particle.cpp:17-24, with mid pinned at :57). 42 % of the client's ramps do not have three
    // stops, and of those that do, the middle one sits at 0.5 only 55 % of the time. See
    // M2ParticleEmitterDef.ColorTimes.

    /// <summary>Locate a normalised age among a ramp's times. Before the first stop and after the
    /// last, the ramp holds -- which is what an authored ramp bounded by 0 and 1 means.</summary>
    static float RampSpan(float[] times, float t, out int i0, out int i1)
    {
        int n = times != null ? times.Length : 0;
        if (n <= 1) { i0 = i1 = 0; return 0f; }
        if (t <= times[0]) { i0 = i1 = 0; return 0f; }
        if (t >= times[n - 1]) { i0 = i1 = n - 1; return 0f; }
        int lo = 0, hi = n - 1;
        while (hi - lo > 1)
        {
            int mid = (lo + hi) >> 1;
            if (times[mid] <= t) lo = mid; else hi = mid;
        }
        i0 = lo; i1 = hi;
        float span = times[hi] - times[lo];
        return span > 0f ? (t - times[lo]) / span : 0f;
    }

    static Color RampColorAt(float[] times, Color[] keys, float t, Color fallback)
    {
        if (keys == null || keys.Length == 0) return fallback;
        int a, b;
        float r = RampSpan(times, t, out a, out b);
        if (a >= keys.Length) a = keys.Length - 1;
        if (b >= keys.Length) b = keys.Length - 1;
        return a == b ? keys[a] : Color.Lerp(keys[a], keys[b], r);
    }

    static WowVec2 RampSizeAt(float[] times, WowVec2[] keys, float t)
    {
        if (keys == null || keys.Length == 0) return new WowVec2(1f, 1f);
        int a, b;
        float r = RampSpan(times, t, out a, out b);
        if (a >= keys.Length) a = keys.Length - 1;
        if (b >= keys.Length) b = keys.Length - 1;
        if (a == b) return keys[a];
        return new WowVec2(Mathf.Lerp(keys[a].X, keys[b].X, r), Mathf.Lerp(keys[a].Y, keys[b].Y, r));
    }

    /// <summary>
    /// The squared length, in model units, below which a tail's screen-plane projection counts as
    /// nothing and the particle is drawn as a 5 % speck instead. The client's builder tests
    /// dot(a, a) > 1e-4 in its own view space; Wowhead's viewer runs that space at 3x model
    /// units, so the same rule in model units is 1e-4 / 9.
    /// </summary>
    const float TailDegenerateSq = 1.1e-5f;

    void BuildParticleMesh(ParticleState s, Matrix4x4 boneMatrix, Vector3 camRight, Vector3 camUp)
    {
        int v = 0, quads = 0;
        Vector3 min = Vector3.positiveInfinity, max = Vector3.negativeInfinity;
        bool billboard = !s.Def.DoNotBillboard;
        float spin = s.Def.SpriteRotation;
        float spinCos = 1f, spinSin = 0f;
        if (billboard && spin != 0f)
        {
            spinCos = Mathf.Cos(spin);
            spinSin = Mathf.Sin(spin);
        }
        float slowdown = s.Def.Slowdown;

        for (int i = 0; i < s.Count; i++)
        {
            float t = Mathf.Clamp01(s.Life[i] / s.MaxLife[i]);

            Vector3 centre = s.BoneLocal ? boneMatrix.MultiplyPoint3x4(s.Pos[i]) : s.Pos[i];
            // The x stop scales the quad's right axis and the y stop its up axis. The legacy uses
            // the x for both (particle.cpp:55); real emitters author non-square sizes -- Battle
            // Magister's Orbs runs (0.173,0.138) -> (0.315,0.252) -> (0.069,0.055).
            WowVec2 size = RampSizeAt(s.Def.SizeTimes, s.Def.SizeKeys, t);
            float halfX = size.X;
            float halfY = size.Y;

            Vector3 right, up;
            if (billboard)
            {
                // The sprite rotation turns the quad in the plane facing the camera.
                right = (camRight * spinCos + camUp * spinSin) * halfX;
                up = (camUp * spinCos - camRight * spinSin) * halfY;
            }
            else
            {
                Vector3 r = s.QuadRight[i];
                Vector3 u = s.QuadUp[i];
                if (s.BoneLocal)
                {
                    r = boneMatrix.MultiplyVector(r);
                    u = boneMatrix.MultiplyVector(u);
                }
                right = r * halfX;
                up = u * halfY;
            }

            // Colour and alpha are sampled INDEPENDENTLY, each on its own track's times. See
            // ParticleState.RampColor for what folding them together costs.
            Color rgb = RampColorAt(s.RampTimes, s.RampColor, t, Color.white);
            Color32 c32 = new Color(rgb.r, rgb.g, rgb.b, SampleAlphaRamp(s.Def, t));

            // The flipbook. RANDOMTEXTURE holds the tile the particle was born with; otherwise
            // the tile advances across rows*cols over the particle's life, starting at the tile
            // RANDOMSTART picked. See divergence 9 in the file header.
            int tile;
            if (s.Def.RandomTexture)
                tile = s.TileSeed[i];
            else if (s.TileCount > 1)
                tile = (s.TileSeed[i] + (int)(t * s.TileCount)) % s.TileCount;
            else
                tile = 0;
            int col = tile % s.Def.Cols;
            int row = tile / s.Def.Cols;
            float u0 = col / (float)s.Def.Cols, u1 = (col + 1) / (float)s.Def.Cols;
            // WoW's V runs down from the top and Unity's up from the bottom, the same flip the
            // mesh UVs take (WowCoordinateConverter.ConvertTexCoord).
            float v1 = 1f - row / (float)s.Def.Rows, v0 = 1f - (row + 1) / (float)s.Def.Rows;

            if (s.DrawHead)
            {
                Vector3 p0 = centre - right - up;
                Vector3 p1 = centre + right - up;
                Vector3 p2 = centre + right + up;
                Vector3 p3 = centre - right + up;

                s.Verts[v] = p0; s.Uvs[v] = new Vector2(u0, v0); s.Colors[v++] = c32;
                s.Verts[v] = p1; s.Uvs[v] = new Vector2(u1, v0); s.Colors[v++] = c32;
                s.Verts[v] = p2; s.Uvs[v] = new Vector2(u1, v1); s.Colors[v++] = c32;
                s.Verts[v] = p3; s.Uvs[v] = new Vector2(u0, v1); s.Colors[v++] = c32;
                quads++;

                Grow(ref min, ref max, p0);
                Grow(ref min, ref max, p2);
            }

            if (s.DrawTail)
            {
                // A TAIL PARTICLE IS A STREAK, NOT A SPRITE. The quad runs from the particle back
                // along the path it just travelled: -velocity * TailLength long, and as wide as
                // the size ramp says, turned to lie in the screen plane. Its length is therefore
                // its speed, so a fresh spark is a streak and a spark that drag has stopped is
                // nothing -- which is how the game empties Algalon's cloud of the two thirds of
                // it that has stalled, and why a viewer drawing every one of them as a head
                // shows a couple of hundred bright motes hanging in the air.
                //
                // This is the client's rule as Wowhead's viewer implements it, read out of the
                // live page: tail = -v * tailLength (min(age, tailLength) under 0x400); if the
                // projection of that onto the screen plane has any length, the long half-axis is
                // half the tail and the centre moves half a tail back so the quad starts AT the
                // particle; the short half-axis is the size, perpendicular to the projection.
                // Otherwise the particle is a speck of 5 % of its size. Measured on Algalon that
                // is 0.082 units wide at birth and 0.0048 once stalled, and the switch lands at
                // 2.5 s, exactly where drag takes the projection under the threshold.
                //
                // The stored velocity is the launch velocity; the integrator damps the
                // displacement rather than the velocity (see AdvanceParticles), so the speed the
                // particle actually has now is Vel * exp(-slowdown * life).
                Vector3 vel = s.Vel[i] * (slowdown > 0f ? Mathf.Exp(-slowdown * s.Life[i]) : 1f);
                if (s.BoneLocal)
                    vel = boneMatrix.MultiplyVector(vel);
                float tailLen = s.Def.TailLength;
                if (s.Def.ClampTailToAge)
                    tailLen = Mathf.Min(s.Life[i], tailLen);
                Vector3 tail = vel * -tailLen;

                float ax = Vector3.Dot(tail, camRight), ay = Vector3.Dot(tail, camUp);
                float a2 = ax * ax + ay * ay;
                Vector3 tRight, tUp, tCentre;
                if (a2 > TailDegenerateSq)
                {
                    float inv = 1f / Mathf.Sqrt(a2);
                    float ux = ax * inv, uy = ay * inv;
                    tRight = tail * 0.5f;
                    tUp = camRight * (-uy * halfY) + camUp * (ux * halfX);
                    tCentre = centre + tail * 0.5f;
                }
                else
                {
                    tRight = camRight * (0.05f * halfX);
                    tUp = camUp * (0.05f * halfY);
                    tCentre = centre;
                }

                // U runs along the streak and V across it, the client's corner order.
                Vector3 q0 = tCentre - tRight - tUp;
                Vector3 q1 = tCentre + tRight - tUp;
                Vector3 q2 = tCentre + tRight + tUp;
                Vector3 q3 = tCentre - tRight + tUp;

                s.Verts[v] = q0; s.Uvs[v] = new Vector2(u0, v0); s.Colors[v++] = c32;
                s.Verts[v] = q1; s.Uvs[v] = new Vector2(u1, v0); s.Colors[v++] = c32;
                s.Verts[v] = q2; s.Uvs[v] = new Vector2(u1, v1); s.Colors[v++] = c32;
                s.Verts[v] = q3; s.Uvs[v] = new Vector2(u0, v1); s.Colors[v++] = c32;
                quads++;

                Grow(ref min, ref max, q0);
                Grow(ref min, ref max, q2);
            }
        }

        Upload(s.Mesh, s.Verts, s.Uvs, s.Colors, s.Indices, v, quads * 6, min, max);
    }

    // =========================================================================================
    // Ribbons
    // =========================================================================================
    //
    // A ribbon is a trail of EDGES left behind by a point on a bone. Each edge remembers where
    // that point was and which way was "up" for it; the strip between consecutive edges is the
    // ribbon. An edge is laid down every 1/res seconds and retired once it is `length` seconds
    // old, so a res of 50 and a length of 0.2 -- the Thunderblade's numbers -- is a ten-edge
    // ribbon covering the last fifth of a second of the sword's motion.
    //
    // WHY NOT THE LEGACY'S GEOMETRY. RibbonEmitter::setup (particle.cpp:773-822) reads `res` as a
    // segment COUNT and `length` as a spatial segment LENGTH, multiplies them into a total
    // spatial length, and advances the head by
    //
    //     float dlen = (ntpos-tpos).length();
    //
    // glm::vec3::length() returns the number of COMPONENTS -- 3 -- not the magnitude. So dlen is
    // the constant 3 on every frame of every ribbon ever drawn, the head advances by 3 whatever
    // the bone did, and the ribbon's extent has nothing to do with how far anything moved. The
    // "up" vector two lines above has the same class of mistake twice over: it is built as
    //
    //     ntup = parent->mat * (vec4(pos,1) + vec4(0,0,1,1));   // w becomes 2: the translation
    //     ntup -= ntpos;                                        // is added a second time
    //     glm::normalize(ntup);                                 // result discarded
    //
    // so it is neither a direction nor normalised. Above and below it, the file's own comment
    // reads "TODO: figure out actual correct way to calculate length" and gives two models it
    // gets visibly wrong in opposite directions.
    //
    // The data settles what the fields mean without having to guess: across this client `res` is
    // 30..100 and `length` is 0.1..2.0. Read as a count and a distance those are 30..100 segments
    // spanning 3..200 model units -- a ribbon longer than most models are tall. Read as edges per
    // second and a lifetime in seconds they are 5..200 edges covering a tenth of a second to two
    // seconds of motion, which is what a weapon trail is.

    void AdvanceRibbon(RibbonState s, Matrix4x4 boneMatrix, float dt)
    {
        Vector3 pos = boneMatrix.MultiplyPoint3x4(s.Origin);
        // "Up" is the emitter's own +Z carried by the bone: the ribbon is a flat band standing
        // on the spine, and this is which way it stands. WoW +Z is Unity +Y.
        Vector3 up = boneMatrix.MultiplyVector(Vector3.up);
        if (up.sqrMagnitude < 1e-12f)
            up = Vector3.up;
        else
            up = up.normalized;

        if (!s.HasPrevious)
        {
            s.Head = 0;
            s.Count = 0;
            s.SinceLastEdge = 0f;
            s.HasPrevious = true;
            PushEdge(s, pos, up);
            return;
        }

        for (int i = 0; i < s.Count; i++)
            s.EdgeAge[(s.Head + i) % s.Capacity] += dt;

        // Retire from the tail anything older than the edge lifetime. The newest edge is never
        // retired, so a ribbon on a paused model keeps the shape it had.
        while (s.Count > 1 && s.EdgeAge[s.Head] > s.Lifetime)
        {
            s.Head = (s.Head + 1) % s.Capacity;
            s.Count--;
        }

        // The head edge tracks the bone continuously; a new one is laid down once the interval
        // has passed. Without the first part a slow ribbon would visibly lag its own emitter.
        int head = (s.Head + s.Count - 1) % s.Capacity;
        s.EdgePos[head] = pos;
        s.EdgeUp[head] = up;

        s.SinceLastEdge += dt;
        while (s.SinceLastEdge >= s.EdgeInterval && s.EdgeInterval > 0f)
        {
            s.SinceLastEdge -= s.EdgeInterval;
            PushEdge(s, pos, up);
        }
    }

    static void PushEdge(RibbonState s, Vector3 pos, Vector3 up)
    {
        if (s.Count >= s.Capacity)
        {
            // Full: drop the oldest. The ring is sized from res * lifetime, so this is the
            // safety net rather than the normal path.
            s.Head = (s.Head + 1) % s.Capacity;
            s.Count--;
        }
        int slot = (s.Head + s.Count) % s.Capacity;
        s.EdgePos[slot] = pos;
        s.EdgeUp[slot] = up;
        s.EdgeAge[slot] = 0f;
        s.Count++;
    }

    void BuildRibbonMesh(RibbonState s, float seqMs, float globalMs)
    {
        Color tint = Color.white;
        if (s.Def.Color.HasData)
        {
            WowVec3 c = SampleVec3(s.Def.Color, seqMs, globalMs);
            tint = new Color(c.X, c.Y, c.Z, 1f);
        }
        tint.a = s.Def.Opacity.HasData ? Sample(s.Def.Opacity, seqMs, globalMs, 1f) : 1f;
        float above = Sample(s.Def.Above, seqMs, globalMs, 0f);
        float below = Sample(s.Def.Below, seqMs, globalMs, 0f);

        int v = 0;
        Vector3 min = Vector3.positiveInfinity, max = Vector3.negativeInfinity;
        // Walk newest first, so u = 0 is the emitter and u = 1 is the tail. That is the direction
        // a trail texture is authored in, and it matches the legacy's own walk, which runs from
        // the head of its list (particle.cpp:851-862).
        for (int i = 0; i < s.Count; i++)
        {
            int slot = (s.Head + s.Count - 1 - i) % s.Capacity;
            float age = s.Lifetime > 0f ? Mathf.Clamp01(s.EdgeAge[slot] / s.Lifetime) : 0f;
            Vector3 p = s.EdgePos[slot];
            Vector3 u = s.EdgeUp[slot];

            Vector3 top = p + u * above;
            Vector3 bottom = p - u * below;

            // The band fades out along its length rather than ending in a hard edge. The legacy
            // draws a flat colour and lets the texture do it; a texture that does not, ends
            // abruptly, and every ribbon in the validation set is a soft streak.
            Color32 c32 = new Color(tint.r, tint.g, tint.b, tint.a * (1f - age));

            s.Verts[v] = top; s.Uvs[v] = new Vector2(age, 0f); s.Colors[v++] = c32;
            s.Verts[v] = bottom; s.Uvs[v] = new Vector2(age, 1f); s.Colors[v++] = c32;

            Grow(ref min, ref max, top);
            Grow(ref min, ref max, bottom);
        }

        int quads = s.Count > 1 ? s.Count - 1 : 0;
        Upload(s.Mesh, s.Verts, s.Uvs, s.Colors, s.Indices, v, quads * 6, min, max);
    }

    // =========================================================================================
    // Mesh upload
    // =========================================================================================

    /// <summary>
    /// Push one emitter's geometry. Every array was allocated at build time and is passed with a
    /// length, so nothing here allocates: the steady state of a model with particles is zero
    /// managed allocations per frame.
    ///
    /// Clear(false) first because the vertex count shrinks as particles die, and an index buffer
    /// still describing the larger mesh would be validated against the smaller one.
    /// </summary>
    static void Upload(Mesh mesh, Vector3[] verts, Vector2[] uvs, Color32[] colors, int[] indices,
                       int vertexCount, int indexCount, Vector3 min, Vector3 max)
    {
        mesh.Clear(false);
        if (vertexCount <= 0 || indexCount <= 0)
            return;
        mesh.SetVertices(verts, 0, vertexCount);
        mesh.SetUVs(0, uvs, 0, vertexCount);
        mesh.SetColors(colors, 0, vertexCount);
        mesh.SetIndices(indices, 0, indexCount, MeshTopology.Triangles, 0, false);
        // Bounds are computed from the points that were just written rather than by
        // RecalculateBounds, which would walk the vertex array a second time.
        mesh.bounds = new Bounds((min + max) * 0.5f, max - min);
    }

    static void Grow(ref Vector3 min, ref Vector3 max, Vector3 p)
    {
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
        if (p.z > max.z) max.z = p.z;
    }

    static int[] BuildQuadIndices(int quads)
    {
        var idx = new int[quads * 6];
        for (int q = 0; q < quads; q++)
        {
            int v = q * 4, i = q * 6;
            idx[i] = v; idx[i + 1] = v + 2; idx[i + 2] = v + 1;
            idx[i + 3] = v; idx[i + 4] = v + 3; idx[i + 5] = v + 2;
        }
        return idx;
    }

    static int[] BuildStripIndices(int edges)
    {
        int quads = edges > 1 ? edges - 1 : 0;
        var idx = new int[quads * 6];
        for (int q = 0; q < quads; q++)
        {
            int v = q * 2, i = q * 6;
            idx[i] = v; idx[i + 1] = v + 2; idx[i + 2] = v + 1;
            idx[i + 3] = v + 1; idx[i + 4] = v + 2; idx[i + 5] = v + 3;
        }
        return idx;
    }

    // =========================================================================================
    // Track evaluation -- the same rules the bones and materials follow
    // =========================================================================================

    float Sample(M2Track<float> track, float seqMs, float globalMs, float fallback)
    {
        if (!track.HasData)
            return fallback;
        float t = TimeFor(track.GlobalSequence, track.IsGlobal, seqMs, globalMs);
        int i0, i1;
        float r = Locate(track.Times, t, out i0, out i1);
        if (track.Interpolation == M2Interpolation.None)
            return track.Values[i0];
        return Mathf.Lerp(track.Values[i0], track.Values[i1], r);
    }

    WowVec3 SampleVec3(M2Track<WowVec3> track, float seqMs, float globalMs)
    {
        if (!track.HasData)
            return new WowVec3(1f, 1f, 1f);
        float t = TimeFor(track.GlobalSequence, track.IsGlobal, seqMs, globalMs);
        int i0, i1;
        float r = Locate(track.Times, t, out i0, out i1);
        WowVec3 a = track.Values[i0];
        if (track.Interpolation == M2Interpolation.None || i0 == i1)
            return a;
        WowVec3 b = track.Values[i1];
        return new WowVec3(Mathf.Lerp(a.X, b.X, r), Mathf.Lerp(a.Y, b.Y, r), Mathf.Lerp(a.Z, b.Z, r));
    }

    /// <summary>
    /// Which clock a track runs on. A global-sequence track loops over its own duration on the
    /// shared clock, whatever the animation is doing -- the same rule WmvM2Animator applies, and
    /// the reason a torch keeps flickering on a paused model.
    /// </summary>
    float TimeFor(int globalSequence, bool isGlobal, float seqMs, float globalMs)
    {
        if (!isGlobal)
            return seqMs;
        uint[] gs = globalSequences;
        if (gs == null || globalSequence < 0 || globalSequence >= gs.Length || gs[globalSequence] == 0)
            return 0f;
        float d = gs[globalSequence];
        float t = globalMs - Mathf.Floor(globalMs / d) * d;
        return t;
    }

    uint[] globalSequences = new uint[0];

    /// <summary>The model's global-sequence durations, needed to evaluate a track bound to one.</summary>
    public void SetGlobalSequences(uint[] durations)
    {
        globalSequences = durations ?? new uint[0];
    }

    /// <summary>Find the keys either side of t and how far between them it sits. Past the last
    /// key a track holds its last value, which is what the legacy evaluator does
    /// (Animated::getValue, animated.h).</summary>
    static float Locate(uint[] times, float t, out int i0, out int i1)
    {
        int n = times.Length;
        if (n <= 1) { i0 = i1 = 0; return 0f; }
        if (t <= times[0]) { i0 = i1 = 0; return 0f; }
        if (t >= times[n - 1]) { i0 = i1 = n - 1; return 0f; }
        int lo = 0, hi = n - 1;
        while (hi - lo > 1)
        {
            int mid = (lo + hi) >> 1;
            if (times[mid] <= t) lo = mid; else hi = mid;
        }
        i0 = lo; i1 = hi;
        float span = times[hi] - times[lo];
        return span > 0f ? (t - times[lo]) / span : 0f;
    }

    // =========================================================================================
    // Deterministic randomness
    // =========================================================================================
    //
    // xorshift32 per emitter, seeded from the emitter index. Deterministic on purpose: a pinned
    // capture at "1.0 s" has to produce the same frame every time or a BEFORE and an AFTER of
    // the same instant cannot be compared, and UnityEngine.Random would also be sharing state
    // with whatever else the scene does.

    static uint NextRandom(ParticleState s)
    {
        uint x = s.Rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s.Rng = x;
        return x;
    }

    static float RandUnit(ParticleState s)
    {
        return (NextRandom(s) & 0xFFFFFF) / (float)0x1000000;
    }

    static float RandRange(ParticleState s, float a, float b)
    {
        return a == b ? a : a + (b - a) * RandUnit(s);
    }

    // =========================================================================================
    // Lifecycle
    // =========================================================================================

    /// <summary>
    /// Throw away every live particle and every ribbon edge, keeping the emitters themselves.
    ///
    /// For a restart of the WHOLE simulation -- a pinned capture, or the lifecycle self-test.
    /// A change of sequence resets only the ribbons; see Rebind for why the particles survive it.
    /// </summary>
    public void ResetState()
    {
        ResetParticles();
        ResetRibbons();
    }

    /// <summary>
    /// Throw away every live particle. Only for a restart of the whole simulation -- NOT for a
    /// change of animation, which is what ResetRibbons is for.
    /// </summary>
    void ResetParticles()
    {
        for (int i = 0; i < particles.Count; i++)
        {
            ParticleState s = particles[i];
            s.Count = 0;
            s.SpawnRemainder = 0f;
            s.Rng = (uint)(0x9E3779B9u * (uint)(i + 1) + 0x85EBCA6Bu);
            s.Mesh.Clear(false);
        }
    }

    /// <summary>Throw away every ribbon segment. See ResetState for why this half is different.</summary>
    void ResetRibbons()
    {
        for (int i = 0; i < ribbons.Count; i++)
        {
            RibbonState s = ribbons[i];
            s.Head = 0;
            s.Count = 0;
            s.SinceLastEdge = 0f;
            s.HasPrevious = false;
            s.Mesh.Clear(false);
        }
    }

    /// <summary>
    /// Re-point the emitters at a re-parsed model, for when the app changes the animation.
    ///
    /// The DEFINITIONS come back with their tracks narrowed to the new sequence, which is the
    /// whole reason this exists: an emitter that has no EmissionRate keys in the new sequence has
    /// to stop, and one that gains them has to start. Emitter identity is by index, and the
    /// emitter array does not depend on which sequence was parsed, so index i is the same emitter
    /// it was. Anything else -- a different model, a different count -- rebuilds instead.
    /// </summary>
    public bool Rebind(M2ParsedModel model)
    {
        if (model == null)
            return false;
        if (model.ParticleEmitters.Length != particles.Count ||
            model.RibbonEmitters.Length != ribbons.Count)
            return false;
        for (int i = 0; i < particles.Count; i++)
        {
            particles[i].Def = model.ParticleEmitters[i];
            ResolveRamp(particles[i]);
        }
        for (int i = 0; i < ribbons.Count; i++)
        {
            RibbonState s = ribbons[i];
            s.Def = model.RibbonEmitters[i];
            s.Lifetime = s.Def.EdgeLifetimeSeconds > 0f ? s.Def.EdgeLifetimeSeconds : 0.5f;
            s.EdgeInterval = 1f / (s.Def.EdgesPerSecond > 0f ? s.Def.EdgesPerSecond : 30f);
        }
        SetGlobalSequences(model.GlobalSequences);
        // THE RIBBONS RESTART; THE PARTICLES DO NOT.
        //
        // A ribbon is a trail of segments left behind by a bone, so a sequence change has to
        // clear it or the first frame of the new animation draws a straight edge from wherever
        // the bone was in the old one -- a smear across the model that no bone ever traced.
        //
        // A particle owes nothing to the previous frame's bone. It is a free body with its own
        // position, velocity and remaining life, and in the game changing animation does not put
        // a torch out: the legacy viewport holds one std::list<Particle> for the life of the
        // model (particle.h:74) and WoWModel::animate never touches it (WoWModel.cpp:2210-2224).
        // Clearing it here was costing the whole cloud on every sequence change, and because the
        // authored rate IS the number alive at once -- Algalon's emitter is 45 particles over a
        // 7-second life -- the cloud then needed seven seconds of animation to build back up.
        // Anyone who changed animation, or looked at a model in the first seconds after it
        // loaded, saw no particles at all.
        ResetRibbons();
        return true;
    }

    void Clear()
    {
        for (int i = 0; i < particles.Count; i++)
            if (particles[i].Go != null) Destroy(particles[i].Go);
        for (int i = 0; i < ribbons.Count; i++)
            if (ribbons[i].Go != null) Destroy(ribbons[i].Go);
        particles.Clear();
        ribbons.Clear();
        for (int i = 0; i < ownedMaterials.Count; i++)
            if (ownedMaterials[i] != null) Destroy(ownedMaterials[i]);
        for (int i = 0; i < ownedMeshes.Count; i++)
            if (ownedMeshes[i] != null) Destroy(ownedMeshes[i]);
        ownedMaterials.Clear();
        ownedMeshes.Clear();
    }

    void OnDestroy()
    {
        Clear();
    }

    static Vector3 ToUnity(WowVec3 v)
    {
        float x, y, z;
        WowCoordinateConverter.ConvertPosition(v, out x, out y, out z);
        return new Vector3(x, y, z);
    }

    // =========================================================================================
    // Pinned capture
    // =========================================================================================

    /// <summary>
    /// Run the simulation from a clean state to a given instant, in fixed steps.
    ///
    /// A capture taken with -wmvAnimTime holds both clocks still, which is exactly right for
    /// bones and exactly wrong for emitters: a frozen clock never spawns a particle, so a pinned
    /// AFTER would be as empty as the BEFORE it is meant to be compared with. This runs the
    /// emitters forward to the pinned instant instead, deterministically, so "1.0 s" is the same
    /// frame every time it is captured.
    ///
    /// poseAt is called before each step with that step's sequence time, so particles are emitted
    /// from where the bone actually was rather than from where it ends up.
    /// </summary>
    public void SimulateTo(float wallMs, Func<float, float> sequenceTimeAt, Action<float> poseAt)
    {
        ResetState();
        float seconds = Mathf.Clamp(wallMs / 1000f, 0f, PinnedMaxSeconds);
        int steps = Mathf.CeilToInt(seconds / PinnedStepSeconds);
        float lastSeq = sequenceTimeAt != null ? sequenceTimeAt(wallMs) : wallMs;
        for (int i = 1; i <= steps; i++)
        {
            float wall = Mathf.Min(i * PinnedStepSeconds * 1000f, wallMs);
            lastSeq = sequenceTimeAt != null ? sequenceTimeAt(wall) : wall;
            if (poseAt != null)
                poseAt(lastSeq);
            Advance(PinnedStepSeconds, lastSeq, wall);
        }
        if (steps == 0 && poseAt != null)
            poseAt(lastSeq);
        // One upload, for the instant that was asked for.
        Build(lastSeq, wallMs);
    }
}
