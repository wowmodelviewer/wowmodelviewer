// WmvMaterialAnimator.cs
//
// Evaluates the M2 material tracks -- colour, colour alpha, texture weight, texture transform --
// and writes the result onto the materials every frame. One component per model, on the model
// root, so it dies with the model.
//
// WHAT IT REPRODUCES, and from where. Every rule here is the legacy viewport's, with the line:
//
//   ocol = (1,1,1,1); ecol = 0
//   if colour entry and its RGB track has keys at animation 0        ModelRenderPass.cpp:396
//       c      = rgb(0, t)                                            :400  (index 0, always)
//       ocol.w = alpha(anim, t) if that track has keys, else 1        :401-404
//       ecol   = (c, ocol.w)                                          :433
//   if weight entry and its track has keys at animation 0             :438-440
//       ocol.w *= weight(0, t)                                        :443  (index 0, always)
//   drawn  = ocol.w > 0 && (no colour entry || ecol.w > 0)           :468
//   unlit: fragment = texture * c, alpha = combiner(mode) * ocol.w    :570-575, :100-151
//   unlit opaque with ocol.w < 1: blending on, IN PLACE               :577
//   texture matrix = T * Rz * S, no pivot                             TextureAnim.cpp:24-36
//
// WHICH PASSES GET WHAT. The rules above are applied in full to UNLIT passes only (material flag
// 0x01, WmvMaterialAnimBinding.Unlit): the colour tint (texture x c), the animated opacity and the
// in-place blend flip, the visibility gate and the texture transforms. A LIT pass gets the gate
// and the texture transforms and NOTHING ELSE from this branch: no tint, no animated opacity; it
// keeps the behaviour it had before Stage B. This is evidence-based, not an oversight: a
// client-wide sweep found 229 models with lit tinted batches and 913 with lit animated-opacity
// batches; the legacy's own lit behaviour is a fixed-function artefact (c enters as GL_EMISSION and
// ocol.w never reaches a lit fragment, ModelRenderPass.cpp:431-434, video.cpp:237-241), and the
// modern client's intended lit tint / opacity was not established from anything in this
// repository. Until a reference-backed investigation settles it, lit tint and lit animated
// opacity stay UNRESOLVED and untouched. UNLIT still decides whether the preview rig applies,
// exactly as Stage A left it.
//
// WHAT IT DOES NOT REPRODUCE, deliberately, and says so in the log:
//   - the legacy rotation, which reads a quaternion as a vec3 and rotates by its x in degrees
//     ("this is wrong", TextureAnim.cpp:33). Rotation tracks are parsed and counted, not applied.
//   - the legacy lit path (above): neither its emission artefact nor a texture x colour rule is
//     applied to a lit pass.
//
// COMPLETE STATE, ALWAYS. The values come from M2MaterialEval (Wow/, no Unity type in it) and
// every one that applies to the pass -- the gate and both UV transforms on every pass, the colour
// and the opacity on an UNLIT pass only (see Write) -- is written on every apply, and for every
// binding on every sequence change (WriteAll): nothing a previous sequence wrote can survive,
// exactly as in the legacy, which re-derives all of it from the current sequence's keys each frame.
//
// ONE CLOCK. This never reads Time. WmvM2Animator.ApplyPose(t) calls Apply(t) with the same t
// the bones get, and global-sequence tracks read WmvM2Animator.GlobalTimeMs, the same static the
// bones read. Pause, scrub, speed, looping and -wmvAnimTime therefore cover materials for free.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvMaterialAnimator : MonoBehaviour
{
    WmvRuntimeModel runtime;
    M2ParsedModel model;
    WmvMaterialAnimBinding[] bindings = new WmvMaterialAnimBinding[0];
    int[] animated = new int[0];          // indices into bindings that have something to evaluate
    bool[] blendingInPlace = new bool[0]; // per binding: an opaque material currently blending
    uint[] globalSequences = new uint[0];

    static readonly int IdColor = Shader.PropertyToID("_Color");
    static readonly int IdAlphaScale = Shader.PropertyToID("_AlphaScale");
    static readonly int IdSrcBlend = Shader.PropertyToID("_SrcBlend");
    static readonly int IdDstBlend = Shader.PropertyToID("_DstBlend");
    static readonly int IdOpaqueAlpha = Shader.PropertyToID("_OpaqueAlpha");
    static readonly int IdUvXf0 = Shader.PropertyToID("_UvXf0");
    static readonly int IdUvOff0 = Shader.PropertyToID("_UvOff0");
    static readonly int IdUvXf1 = Shader.PropertyToID("_UvXf1");
    static readonly int IdUvOff1 = Shader.PropertyToID("_UvOff1");
    static readonly int[] EmptyTriangles = new int[0];

    /// <summary>How many bindings are evaluated per frame, and of what kind.</summary>
    public int AnimatedCount { get; private set; }
    public int ColorCount { get; private set; }
    public int OpacityCount { get; private set; }
    public int WeightCount { get; private set; }
    public int TransformCount { get; private set; }
    public int RotationIgnored { get; private set; }
    public int GateToggles { get; private set; }
    int zeroLengthGlobal;
    public int ZeroLengthGlobal { get { return zeroLengthGlobal; } }
    M2MaterialState state;

    Action<string> dumpLog;
    bool dumpNext;

    public void Setup(WmvRuntimeModel rt, M2ParsedModel m, Action<string> log)
    {
        runtime = rt;
        model = m;
        bindings = rt.MaterialAnim;
        blendingInPlace = new bool[bindings.Length];
        if (rt.GateHidden == null || rt.GateHidden.Length != bindings.Length)
            rt.GateHidden = new bool[bindings.Length];
        Rebind(m, log);
        // The state at time 0 is part of the rest picture; apply it once whatever the clock does.
        // A pinned capture (-wmvAnimTime) dumps at the pinned instant instead, from PoseAt, so
        // the values written beside a frame are the values that frame was drawn with.
        dumpLog = log;
        dumpNext = WmvModelBuilder.Debug_.MatDump && WmvModelBuilder.Debug_.AnimTime < 0f;
        Apply(0f);
    }

    /// <summary>
    /// Decide again which bindings have anything to evaluate. Run at build and after every
    /// sequence change, because the colour-alpha and transform tracks were re-read for the new
    /// sequence and a material that held still in the idle may move in a walk.
    /// </summary>
    public void Rebind(M2ParsedModel m, Action<string> log)
    {
        model = m;
        globalSequences = m.GlobalSequences;
        var list = new List<int>();
        int colors = 0, opacities = 0, weights = 0, transforms = 0, rotations = 0;
        for (int i = 0; i < bindings.Length; i++)
        {
            if (!BindingAnimates(m, bindings[i]))
                continue;
            list.Add(i);
            WmvMaterialAnimBinding b = bindings[i];
            if (b.Color >= 0 && m.Colors[b.Color].Color.HasData) colors++;
            if (b.Color >= 0 && m.Colors[b.Color].Opacity.HasData) opacities++;
            if (b.Weight >= 0 && m.TextureWeightTracks[b.Weight].HasData) weights++;
            if (b.Transform0 >= 0 && m.TextureTransforms[b.Transform0].IsAnimated) transforms++;
            if (b.Transform1 >= 0 && m.TextureTransforms[b.Transform1].IsAnimated) transforms++;
            if (b.Transform0 >= 0 && m.TextureTransforms[b.Transform0].Rotation.HasData) rotations++;
            if (b.Transform1 >= 0 && m.TextureTransforms[b.Transform1].Rotation.HasData) rotations++;
        }
        animated = list.ToArray();
        AnimatedCount = animated.Length;
        // Every binding, animated or not, gets this sequence's state now -- whatever applies to
        // its pass (Write): the gate and the UV transforms for every pass, the colour and the
        // opacity for an unlit one. See WriteAll.
        WriteAll(0f);
        ColorCount = colors; OpacityCount = opacities; WeightCount = weights;
        TransformCount = transforms; RotationIgnored = rotations;
        if (log != null)
        {
            log(string.Format("matanim: {0} of {1} material(s) evaluated per frame -- {2} colour, " +
                              "{3} colour-alpha, {4} weight, {5} texture-transform binding(s); " +
                              "{6} rotation track(s) present and NOT applied (no correct reference " +
                              "in the legacy source)",
                              AnimatedCount, bindings.Length, colors, opacities, weights, transforms,
                              rotations));
            M2MaterialTrackSurvey s = m.MaterialSurvey;
            log(string.Format("matanim: parsed {0} transform(s) ({1} animated), {2} colour(s) " +
                              "({3} RGB, {4} alpha tracks), {5} weight(s) ({6} animated); {7} weight " +
                              "track(s) carry per-sequence keys the legacy index-0 rule never reads " +
                              "(of {8} checked); {9} array(s) rejected as malformed",
                              s.TextureTransforms, s.TransformsAnimated, s.Colors, s.ColorRgbTracks,
                              s.ColorOpacityTracks, s.TextureWeightTracks, s.WeightsAnimated,
                              s.WeightPerSequenceDiffers, s.WeightPerSequenceChecked, s.Rejected));
        }
    }

    /// <summary>Has this binding anything to evaluate at all, even a constant?</summary>
    public static bool BindingAnimates(M2ParsedModel m, WmvMaterialAnimBinding b)
    {
        if (b.Color >= 0 && b.Color < m.Colors.Length &&
            (m.Colors[b.Color].Color.HasData || m.Colors[b.Color].Opacity.HasData))
            return true;
        if (b.Weight >= 0 && b.Weight < m.TextureWeightTracks.Length && m.TextureWeightTracks[b.Weight].HasData)
            return true;
        if (b.Transform0 >= 0 && b.Transform0 < m.TextureTransforms.Length && m.TextureTransforms[b.Transform0].IsAnimated)
            return true;
        if (b.Transform1 >= 0 && b.Transform1 < m.TextureTransforms.Length && m.TextureTransforms[b.Transform1].IsAnimated)
            return true;
        return false;
    }

    /// <summary>Log the next Apply in full (the -wmvMatDump line set).</summary>
    public void DumpNext(Action<string> log) { dumpLog = log; dumpNext = true; }

    /// <summary>Log the next Apply in full through the logger Setup was given.</summary>
    public void DumpNext() { dumpNext = dumpLog != null; }

    /// <summary>
    /// Evaluate every binding that has something to evaluate at sequence time t (milliseconds)
    /// and write the result onto its material: the gate and both UV transforms for every pass,
    /// and -- on an UNLIT pass only -- the colour, the alpha and the blend state. Everything that
    /// applies, every time: a unit whose transform has no keys in this sequence gets
    /// the identity written, not skipped, because the legacy re-derives every value from the
    /// current sequence's keys each frame and a value sequence A wrote must not survive into B.
    /// No allocation on this path: the arrays were sized in Setup, the property ids are static,
    /// and the state is a value type.
    /// </summary>
    public void Apply(float t)
    {
        if (runtime == null || runtime.Mesh == null)
            return;
        bool dump = dumpNext && dumpLog != null;
        dumpNext = false;
        if (dump)
            dumpLog(string.Format("matdump: t = {0:F1} ms, global clock {1:F1} ms", t, WmvM2Animator.GlobalTimeMs));
        for (int k = 0; k < animated.Length; k++)
            Write(animated[k], t, dump);
    }

    /// <summary>
    /// Write the state of EVERY binding at sequence time t, animated or not. Run whenever the
    /// bindings are re-read (Setup, and every sequence change through Rebind): a binding that
    /// animated in the previous sequence and has nothing to evaluate in this one leaves the
    /// per-frame list, and this is what puts its material back to the state the legacy would
    /// compute for it -- identity UV, the gate open or shut by this sequence's keys and, on an
    /// unlit pass, the default opacity with blending off -- instead of leaving whatever the last
    /// frame of the old sequence wrote.
    /// </summary>
    public void WriteAll(float t)
    {
        if (runtime == null || runtime.Mesh == null)
            return;
        for (int i = 0; i < bindings.Length; i++)
            Write(i, t, false);
    }

    void Write(int i, float t, bool dump)
    {
        WmvMaterialAnimBinding b = bindings[i];
        Material mat = i < runtime.Materials.Length ? runtime.Materials[i] : null;
        if (mat == null)
            return;

        M2MaterialEval.Evaluate(model, b.Color, b.Weight, b.Transform0, b.Transform1, t,
                                WmvM2Animator.GlobalTimeMs, ref state, ref zeroLengthGlobal);

        // ---- the colour: the legacy c, read at animation 0 (sequence-independent) -------------
        // UNLIT ONLY. For a lit pass the legacy feeds c into fixed-function emission and never
        // lets ocol.w reach the fragment (see the file header); what the game does there is not
        // established by anything in this repository, and the client is full of such batches
        // (229 models with a lit tint, 913 with a lit animated opacity). Until that is settled a
        // lit pass keeps the behaviour it had before this branch: no tint, no animated opacity,
        // the gate and the texture transforms only.
        if (state.HasColor && b.Unlit)
            mat.SetColor(IdColor, new Color(state.R, state.G, state.B, 1f));

        // ---- the alpha: UNLIT ONLY, for the same reason as the colour above -------------------
        // Alpha-key: the legacy's blend func is ONE/ZERO there, so ocol.w never reaches the
        // frame -- only the gate does. Opaque (unlit): the legacy turns blending on IN PLACE when
        // ocol.w < 1 (ModelRenderPass.cpp:577), with the func already SrcAlpha /
        // OneMinusSrcAlpha (:483); the queue, the depth write and the draw order do not move.
        // Everything else (unlit): the combiner alpha times ocol.w (:145-151). A lit pass keeps
        // the alpha scale and blend state CreateMaterial gave it.
        if (!b.AlphaKey && b.Unlit)
        {
            float a = Mathf.Clamp01(state.OcolW);
            if (b.Opaque)
            {
                bool blend = a < 1f;
                if (blend != blendingInPlace[i])
                {
                    blendingInPlace[i] = blend;
                    mat.SetFloat(IdSrcBlend, (float)(blend ? UnityEngine.Rendering.BlendMode.SrcAlpha
                                                          : UnityEngine.Rendering.BlendMode.One));
                    mat.SetFloat(IdDstBlend, (float)(blend ? UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha
                                                          : UnityEngine.Rendering.BlendMode.Zero));
                    mat.SetFloat(IdOpaqueAlpha, blend ? 0f : 1f);
                }
                mat.SetFloat(IdAlphaScale, blend ? a : b.BaseAlphaScale);
            }
            else
                mat.SetFloat(IdAlphaScale, b.BaseAlphaScale * a);
        }

        // ---- the gate: withhold or hand back the triangles -----------------------------------
        bool gateHidden = !state.Drawn;
        if (i < runtime.GateHidden.Length && runtime.GateHidden[i] != gateHidden)
        {
            runtime.GateHidden[i] = gateHidden;
            bool geosetOn = WmvModelBuilder.GeosetVisibleFor(runtime, i);
            if (i < runtime.Mesh.subMeshCount && i < runtime.SubmeshTriangles.Length)
                runtime.Mesh.SetTriangles(geosetOn && !gateHidden ? runtime.SubmeshTriangles[i]
                                                                   : EmptyTriangles, i, false);
            GateToggles++;
        }

        // ---- the texture transforms: written whenever a transform is bound, identity included --
        if (b.Transform0 >= 0)
        {
            Vector4 xf, off;
            UvVectors(state.T0x, state.T0y, state.S0x, state.S0y, out xf, out off);
            mat.SetVector(IdUvXf0, xf);
            mat.SetVector(IdUvOff0, off);
        }
        if (b.Transform1 >= 0)
        {
            Vector4 xf, off;
            UvVectors(state.T1x, state.T1y, state.S1x, state.S1y, out xf, out off);
            mat.SetVector(IdUvXf1, xf);
            mat.SetVector(IdUvOff1, off);
        }

        if (dump)
            DumpBinding(b, t, ref state);
    }

    void DumpBinding(WmvMaterialAnimBinding b, float t, ref M2MaterialState st)
    {
        var s = new System.Text.StringBuilder();
        s.AppendFormat("matdump: submesh {0} material {1}:", b.Submesh, b.Material);
        if (b.Color >= 0)
        {
            M2ColorDef cd = model.Colors[b.Color];
            s.AppendFormat(" colour[{0}] rgb {1} keys {2} -> ({3:F4},{4:F4},{5:F4}); alpha {6} keys {7} -> {8:F4};",
                           b.Color, Where(cd.Color.GlobalSequence), Keys3(cd.Color), st.R, st.G, st.B,
                           Where(cd.Opacity.GlobalSequence), KeysF(cd.Opacity), st.EcolW);
            if (!st.HasColor) s.Append(" (no RGB keys at animation 0: legacy hides it)");
        }
        if (b.Weight >= 0)
        {
            M2Track<float> w = model.TextureWeightTracks[b.Weight];
            s.AppendFormat(" weight[{0}] {1} keys {2} -> {3:F4};", b.Weight, Where(w.GlobalSequence), KeysF(w), st.Weight);
        }
        s.AppendFormat(" ocol.w {0:F4} -> {1}; drawn {2}",
                       st.OcolW, !b.Unlit ? "lit: gate only (tint and opacity not applied)"
                                 : b.AlphaKey ? "alpha-key: gate only" : b.Opaque ? (st.OcolW < 1f ? "opaque, blending in place" : "opaque")
                                                                     : string.Format("alpha {0:F4}", b.BaseAlphaScale * Mathf.Clamp01(st.OcolW)),
                       st.Drawn);
        if (st.Uv0Applied)
            s.AppendFormat("; uv0 transform[{0}] translation ({1:F4},{2:F4}) scale ({3:F4},{4:F4}) {5}",
                           b.Transform0, st.T0x, st.T0y, st.S0x, st.S0y, XfDetail(model.TextureTransforms[b.Transform0], t));
        else if (b.Transform0 >= 0)
            s.AppendFormat("; uv0 transform[{0}] identity (no keys in this sequence)", b.Transform0);
        if (st.Uv1Applied)
            s.AppendFormat("; uv1 transform[{0}] translation ({1:F4},{2:F4}) scale ({3:F4},{4:F4}) {5}",
                           b.Transform1, st.T1x, st.T1y, st.S1x, st.S1y, XfDetail(model.TextureTransforms[b.Transform1], t));
        else if (b.Transform1 >= 0)
            s.AppendFormat("; uv1 transform[{0}] identity (no keys in this sequence)", b.Transform1);
        dumpLog(s.ToString());
    }

    string Where(int globalSequence)
    {
        if (globalSequence < 0) return "seq";
        if (globalSequence >= globalSequences.Length) return "global#" + globalSequence + "(out of range)";
        return string.Format("global#{0}/{1}ms@{2:F1}{3}", globalSequence, globalSequences[globalSequence],
                             globalSequences[globalSequence] == 0 ? 0.0 : WmvM2Animator.GlobalTimeMs % globalSequences[globalSequence],
                             globalSequences[globalSequence] == 0 ? "(held at key 0)" : "");
    }

    string XfDetail(M2TextureTransform x, float t)
    {
        var s = new System.Text.StringBuilder();
        if (x.Translation.HasData)
            s.AppendFormat("[T {0} {1}]", Where(x.Translation.GlobalSequence), Keys3(x.Translation));
        if (x.Scale.HasData)
            s.AppendFormat("[S {0} {1}]", Where(x.Scale.GlobalSequence), Keys3(x.Scale));
        if (x.Rotation.HasData)
            s.AppendFormat("[R {0} keys, NOT applied]", x.Rotation.Values.Length);
        return s.ToString();
    }

    static string KeysF(M2Track<float> t)
    {
        var s = new System.Text.StringBuilder();
        int n = t.Times.Length;
        for (int i = 0; i < n; i++) s.AppendFormat("{0}{1}:{2:F3}", i > 0 ? " " : "", t.Times[i], t.Values[i]);
        return "{" + s + "}";
    }

    static string Keys3(M2Track<WowVec3> t)
    {
        var s = new System.Text.StringBuilder();
        int n = t.Times.Length;
        for (int i = 0; i < n; i++)
            s.AppendFormat("{0}{1}:({2:F3},{3:F3},{4:F3})", i > 0 ? " " : "", t.Times[i], t.Values[i].X, t.Values[i].Y, t.Values[i].Z);
        return "{" + s + "}";
    }

    /// <summary>
    /// A unit's UV matrix in this renderer's UV space, from the WoW-space translation and scale.
    ///
    /// In WoW's UV space the legacy composes M = T * R * S (TextureAnim.cpp:24-36, GL
    /// post-multiplication): uv' = T(R(S(uv))). With no rotation applied (see the file header),
    /// that is u' = sx*u + tx, v' = sy*v + ty. The mesh builder stores V flipped
    /// (v_unity = 1 - v_wow), so the matrix is conjugated by that flip, F = [[1,0,0],[0,-1,1]],
    /// M_unity = F * M * F, which for a general 2x3 [[m00,m01,m02],[m10,m11,m12]] is
    /// [[m00, -m01, m01 + m02], [-m10, m11, 1 - m11 - m12]]. Derived, not tuned: with sy = 1 a WoW
    /// scroll of +ty becomes -ty here, which is what a flipped axis must do. Identity in, identity
    /// out: (1,0,0,1) and (0,0).
    /// </summary>
    static void UvVectors(float tx, float ty, float sx, float sy, out Vector4 xf, out Vector4 off)
    {
        float m00 = sx, m01 = 0f, m02 = tx;
        float m10 = 0f, m11 = sy, m12 = ty;
        xf = new Vector4(m00, -m01, -m10, m11);
        off = new Vector4(m01 + m02, 1f - m11 - m12, 0f, 0f);
    }
}
