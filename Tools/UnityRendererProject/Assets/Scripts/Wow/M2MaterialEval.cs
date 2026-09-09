// M2MaterialEval.cs
//
// The material-track evaluator, with no Unity type in it: given a model's parsed tracks, a batch's
// colour / weight / texture-transform indices, the sequence time and the global clock, it produces
// the COMPLETE state of that batch's material at that instant -- colour, the two opacities, the
// visibility gate and both units' UV transforms -- exactly as the legacy viewport computes them
// every frame in ModelRenderPass::init and TextureAnim::calc/setup.
//
// Why "complete": the legacy re-derives every one of these from the CURRENT sequence's tracks on
// every pass of every frame, with fixed defaults for a track that has no keys for that sequence
// (Animated::uses is false):
//
//   colour RGB        read at animation 0 always (ModelRenderPass.cpp:396-400); no keys there
//                     -> the whole colour block is skipped, ecol stays 0 and the gate hides it
//   colour alpha      read at the CURRENT animation (:401-404); no keys -> ocol.w keeps 1
//   texture weight    read at animation 0 always (:438-443); no keys -> ocol.w keeps 1
//   UV translation    applied only if the track uses the animation (TextureAnim.cpp:24-36,
//   UV scale          after glLoadIdentity) -> identity for a component with no keys
//
// So a sequence change never leaves anything behind in the legacy: whatever sequence A wrote is
// recomputed from sequence B's keys, or falls back to these defaults. The renderer that consumes
// this state must therefore write everything it applies from it whenever the bindings are re-read
// (a sequence change) and, for a binding it evaluates per frame, every frame -- never "only what
// has keys". Which fields it applies is the renderer's decision: WmvMaterialAnimator applies the
// gate and the UV transforms to every pass and the colour and opacity to UNLIT passes only; the
// evaluator computes all of them regardless, because the gate needs ocol.w and ecol.w on every
// pass. That is what the regression tests in WowParserTests.MaterialSequenceTests check against
// synthetic two-sequence models, and what WmvMaterialAnimator does on the real ones.
//
// Interpolation: linear or none, as the legacy (Animated::getValue, Animated.h:176-228): past the
// last key the last key is held, before the first key the first span's slope is extrapolated,
// a single key is returned as is. Hermite/Bezier tracks degrade to linear here as the bone tracks
// already do. Global sequences: time = globalTimeMs % length; a zero-length global sequence is
// held at key 0 (a deliberate deviation from the legacy, which returns 0 there and so collapses a
// scale track to 0 -- see the notes) and counted so a log can say how often it happened.

using System;

namespace Wmv.Wow
{
    /// <summary>Everything a batch's material needs at one instant. Value type: no allocation.</summary>
    public struct M2MaterialState
    {
        /// <summary>The colour block ran (RGB keys at animation 0).</summary>
        public bool HasColor;
        public float R, G, B;
        /// <summary>The legacy ocol.w: colour alpha (this sequence, default 1) times weight (animation 0, default 1).</summary>
        public float OcolW;
        /// <summary>The legacy ecol.w: the colour alpha when the colour block ran, 0 when it did not.</summary>
        public float EcolW;
        /// <summary>The texture weight at animation 0, or 1 when the track has no keys.</summary>
        public float Weight;
        /// <summary>
        /// UNIT 1's OWN texture weight, reached through the same lookup table one entry along. For
        /// pixel shaders 20 and 23 it is the gain on the second unit's additive lobe. 1 when
        /// nothing is bound.
        /// </summary>
        public float Weight1;
        /// <summary>
        /// UNIT 2's OWN texture weight -- a separate track from the one above, reached through the
        /// same lookup table one entry further along. It is the opacity of the third texture unit,
        /// and for pixel shader 15 it is the gain on the additive lobe. 1 when nothing is bound.
        /// </summary>
        public float Weight2;
        /// <summary>The colour-alpha track had keys for this sequence (else 1 was assumed).</summary>
        public bool AlphaFromTrack;
        /// <summary>The legacy gate: ocol.w &gt; 0 &amp;&amp; (no colour entry || ecol.w &gt; 0).</summary>
        public bool Drawn;
        /// <summary>Unit 0 / 1 / 2 have a transform bound whose translation or scale has keys here.</summary>
        public bool Uv0Applied, Uv1Applied, Uv2Applied;
        /// <summary>WoW-space translation and scale per unit; (0,0) and (1,1) when not applied.</summary>
        public float T0x, T0y, S0x, S0y, T1x, T1y, S1x, S1y, T2x, T2y, S2x, S2y;

        public static M2MaterialState Identity
        {
            get
            {
                var s = new M2MaterialState();
                s.R = s.G = s.B = 1f;
                s.OcolW = 1f; s.EcolW = 0f; s.Weight = 1f; s.Weight1 = 1f; s.Weight2 = 1f;
                s.S0x = s.S0y = s.S1x = s.S1y = s.S2x = s.S2y = 1f;
                return s;
            }
        }
    }

    public static class M2MaterialEval
    {
        /// <summary>
        /// The batch's texture-weight track: THROUGH THE LOOKUP TABLE, as the legacy does and
        /// nothing else -- pass->opacity = transLookup[transid] (WoWModel.cpp:1542, :1807). A
        /// combo index past the table, an absent table, a 0xFFFF entry or an entry past the
        /// tracks all resolve to "none". The legacy indexes the raw table without a bounds check;
        /// this refuses instead of reading past it. No model in the measured client (7596
        /// readable) has weights without a table, a combo past its table, or an entry out of range.
        /// </summary>
        public static int ResolveWeightIndex(M2ParsedModel model, M2Batch batch)
        {
            if (batch.TextureWeightComboIndex >= model.TextureWeightLookup.Length)
                return -1;
            int w = model.TextureWeightLookup[batch.TextureWeightComboIndex];
            return w >= 0 && w < model.TextureWeightTracks.Length ? w : -1;
        }

        /// <summary>
        /// The weight track of ONE TEXTURE UNIT of the batch.
        ///
        /// texture_weight_combos is indexed the same way texture_transform_combos is: the batch's
        /// combo index names the FIRST entry and each further texture unit reads the next one. The
        /// two-argument overload above is that table read at unit 0, which is the pass opacity and
        /// is all the legacy viewport's transparency path ever wanted. A three-unit combiner has
        /// two more, and they are not decoration:
        ///
        ///   The legacy's own GLSL indexes them per unit -- u_tex_sample_alpha.r for unit 1
        ///   (ps 24), .g for unit 2 (ps 20/23/26/28), .b for unit 3 (ps 15/25/26/28), see
        ///   Source/games/wow/ModelRenderPass.cpp:122,127,130,131,132. Retail reads the same
        ///   vector as cb0[6].xyz. WMV has never supplied it: ModelRenderPass.cpp:656 stubs the
        ///   uniform to (1,1,1) and a comment concedes the lobe is dropped.
        ///
        /// So for Combiners_Opaque_Mod2xNA_Alpha_Add the third unit's weight IS the gain on the
        /// glow, and on the models that use it, it is animated. Hard-coding 1 makes an authored
        /// pulse a constant at its maximum.
        /// </summary>
        public static int ResolveWeightIndex(M2ParsedModel model, M2Batch batch, int unit)
        {
            int idx = batch.TextureWeightComboIndex + unit;
            if (idx < 0 || idx >= model.TextureWeightLookup.Length)
                return -1;
            int w = model.TextureWeightLookup[idx];
            return w >= 0 && w < model.TextureWeightTracks.Length ? w : -1;
        }

        /// <summary>The unit's texture transform through the transform lookup (header 0x98), or -1.</summary>
        public static int ResolveTransformIndex(M2ParsedModel model, M2Batch batch, int unit)
        {
            int idx = batch.TextureTransformComboIndex + unit;
            if (idx < 0 || idx >= model.TextureTransformLookup.Length)
                return -1;
            int a = model.TextureTransformLookup[idx];
            return a < model.TextureTransforms.Length ? a : -1;
        }

        /// <summary>
        /// A batch hidden at rest by a constant-zero colour alpha that another sequence can show
        /// (M2ColorDef.OpacityMayOpenElsewhere) -- unless something sequence-independent hides it
        /// anyway: no RGB keys at animation 0 (the legacy skips the colour block and the gate
        /// hides it in every sequence), or a texture weight that is zero at animation 0 (read at
        /// index 0 whatever plays, ModelRenderPass.cpp:438-443).
        /// </summary>
        public static bool GateMayOpenElsewhere(M2ParsedModel model, M2Batch batch)
        {
            if (!batch.HasColor || batch.ColorIndex >= model.Colors.Length)
                return false;
            M2ColorDef cd = model.Colors[batch.ColorIndex];
            if (!cd.Color.HasData || !cd.OpacityMayOpenElsewhere)
                return false;
            int w = ResolveWeightIndex(model, batch);
            if (w >= 0)
            {
                M2Track<float> t = model.TextureWeightTracks[w];
                if (t.HasData && t.Values.Length == 1 && !t.IsGlobal && t.Values[0] <= 0f)
                    return false;
                if (t.HasData && t.IsGlobal)
                {
                    bool any = false;
                    for (int i = 0; i < t.Values.Length; i++) if (t.Values[i] > 0f) any = true;
                    if (!any) return false;
                }
            }
            return true;
        }

        /// <summary>
        /// The time a track is read at: the sequence time, or the global clock modulo the global
        /// sequence's length. A zero-length (or out-of-range) global sequence reads key 0.
        /// </summary>
        public static float TrackTime(uint[] globalSequences, int globalSequence, float t,
                                      double globalTimeMs, ref int zeroLengthGlobal)
        {
            if (globalSequence < 0)
                return t;
            if (globalSequences == null || globalSequence >= globalSequences.Length ||
                globalSequences[globalSequence] == 0)
            {
                zeroLengthGlobal++;
                return 0f;
            }
            return (float)(globalTimeMs % globalSequences[globalSequence]);
        }

        /// <summary>
        /// Where t falls among the keys, as Animated::getValue: past the last key the last key is
        /// held; before the first key the first span's slope is extrapolated (the legacy computes
        /// a negative r and interpolates with it); a single key is returned as is. False when
        /// there is nothing to interpolate (index then names the key to return).
        /// </summary>
        public static bool Span(uint[] times, float t, out int index, out float r)
        {
            index = 0;
            r = 0f;
            int n = times == null ? 0 : times.Length;
            if (n < 2)
                return false;
            if (t > times[n - 1])
            {
                index = n - 1;
                return false;
            }
            int pos = 0;
            for (int i = 0; i < n - 1; i++)
            {
                if (t >= times[i] && t < times[i + 1]) { pos = i; break; }
            }
            index = pos;
            float span = (float)times[pos + 1] - times[pos];
            r = span > 0f ? (t - times[pos]) / span : 0f;
            return true;
        }

        public static float EvalFloat(M2Track<float> track, float t)
        {
            int i; float r;
            if (!Span(track.Times, t, out i, out r))
                return track.Values[Math.Min(i, track.Values.Length - 1)];
            if (track.Interpolation == M2Interpolation.None)
                return track.Values[i];
            return track.Values[i] + (track.Values[i + 1] - track.Values[i]) * r;
        }

        public static WowVec3 EvalVec3(M2Track<WowVec3> track, float t)
        {
            int i; float r;
            if (!Span(track.Times, t, out i, out r))
                return track.Values[Math.Min(i, track.Values.Length - 1)];
            WowVec3 a = track.Values[i];
            if (track.Interpolation == M2Interpolation.None)
                return a;
            WowVec3 b = track.Values[i + 1];
            var v = new WowVec3();
            v.X = a.X + (b.X - a.X) * r;
            v.Y = a.Y + (b.Y - a.Y) * r;
            v.Z = a.Z + (b.Z - a.Z) * r;
            return v;
        }

        /// <summary>
        /// The complete material state of one batch at sequence time t (ms) and global clock
        /// globalTimeMs, from the model's CURRENT per-sequence tracks. color / weight / xf0 / xf1
        /// / xf2 are indices into model.Colors, model.TextureWeightTracks and
        /// model.TextureTransforms, or -1 when the batch has none (STATIC and environment units
        /// arrive here as -1).
        /// </summary>
        public static void Evaluate(M2ParsedModel m, int color, int weight, int xf0, int xf1,
                                    int xf2, float t, double globalTimeMs, ref M2MaterialState s,
                                    ref int zeroLengthGlobal)
        {
            Evaluate(m, color, weight, -1, -1, xf0, xf1, xf2, t, globalTimeMs, ref s, ref zeroLengthGlobal);
        }

        /// <summary>As above, plus units 1 and 2's OWN weight tracks (see ResolveWeightIndex(model,
        /// batch, unit)). They are written to s.Weight1 and s.Weight2 and, unlike the pass weight,
        /// are NOT folded into ocol.w: each is one sampler's gain, not the pass's opacity, and
        /// neither must open or shut the visibility gate.</summary>
        public static void Evaluate(M2ParsedModel m, int color, int weight, int weight1, int weight2,
                                    int xf0, int xf1, int xf2, float t, double globalTimeMs,
                                    ref M2MaterialState s, ref int zeroLengthGlobal)
        {
            s = M2MaterialState.Identity;
            uint[] g = m.GlobalSequences;

            // ---- colour and the two opacities: the legacy ocol / ecol -------------------------
            if (color >= 0 && color < m.Colors.Length)
            {
                M2ColorDef cd = m.Colors[color];
                if (cd.Color.HasData)
                {
                    s.HasColor = true;
                    WowVec3 c = EvalVec3(cd.Color, TrackTime(g, cd.Color.GlobalSequence, t, globalTimeMs, ref zeroLengthGlobal));
                    s.R = c.X; s.G = c.Y; s.B = c.Z;
                    if (cd.Opacity.HasData)
                    {
                        s.OcolW = EvalFloat(cd.Opacity, TrackTime(g, cd.Opacity.GlobalSequence, t, globalTimeMs, ref zeroLengthGlobal));
                        s.AlphaFromTrack = true;
                    }
                    s.EcolW = s.OcolW;
                }
                // else: the legacy block is skipped, ecol stays 0, and the gate below hides it
            }
            if (weight >= 0 && weight < m.TextureWeightTracks.Length)
            {
                M2Track<float> w = m.TextureWeightTracks[weight];
                if (w.HasData)
                {
                    s.Weight = EvalFloat(w, TrackTime(g, w.GlobalSequence, t, globalTimeMs, ref zeroLengthGlobal));
                    s.OcolW *= s.Weight;
                }
            }
            if (weight1 >= 0 && weight1 < m.TextureWeightTracks.Length)
            {
                M2Track<float> w1 = m.TextureWeightTracks[weight1];
                if (w1.HasData)
                    s.Weight1 = EvalFloat(w1, TrackTime(g, w1.GlobalSequence, t, globalTimeMs,
                                                        ref zeroLengthGlobal));
            }
            if (weight2 >= 0 && weight2 < m.TextureWeightTracks.Length)
            {
                M2Track<float> w2 = m.TextureWeightTracks[weight2];
                if (w2.HasData)
                    s.Weight2 = EvalFloat(w2, TrackTime(g, w2.GlobalSequence, t, globalTimeMs,
                                                        ref zeroLengthGlobal));
            }
            s.Drawn = s.OcolW > 0f && (color < 0 || s.EcolW > 0f);

            // ---- texture transforms: per component, identity when the sequence has no keys ----
            if (xf0 >= 0 && xf0 < m.TextureTransforms.Length)
                s.Uv0Applied = Unit(m.TextureTransforms[xf0], g, t, globalTimeMs, ref zeroLengthGlobal,
                                    out s.T0x, out s.T0y, out s.S0x, out s.S0y);
            if (xf1 >= 0 && xf1 < m.TextureTransforms.Length)
                s.Uv1Applied = Unit(m.TextureTransforms[xf1], g, t, globalTimeMs, ref zeroLengthGlobal,
                                    out s.T1x, out s.T1y, out s.S1x, out s.S1y);
            if (xf2 >= 0 && xf2 < m.TextureTransforms.Length)
                s.Uv2Applied = Unit(m.TextureTransforms[xf2], g, t, globalTimeMs, ref zeroLengthGlobal,
                                    out s.T2x, out s.T2y, out s.S2x, out s.S2y);
        }

        static bool Unit(M2TextureTransform x, uint[] g, float t, double globalTimeMs,
                         ref int zeroLengthGlobal, out float tx, out float ty, out float sx, out float sy)
        {
            tx = 0f; ty = 0f; sx = 1f; sy = 1f;
            bool any = false;
            if (x.Translation.HasData)
            {
                WowVec3 v = EvalVec3(x.Translation, TrackTime(g, x.Translation.GlobalSequence, t, globalTimeMs, ref zeroLengthGlobal));
                tx = v.X; ty = v.Y; any = true;
            }
            if (x.Scale.HasData)
            {
                WowVec3 v = EvalVec3(x.Scale, TrackTime(g, x.Scale.GlobalSequence, t, globalTimeMs, ref zeroLengthGlobal));
                sx = v.X; sy = v.Y; any = true;
            }
            // rotation: parsed, counted, not applied (no correct reference in the legacy)
            return any;
        }
    }
}
