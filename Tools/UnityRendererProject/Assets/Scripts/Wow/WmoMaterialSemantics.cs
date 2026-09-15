// WmoMaterialSemantics.cs
//
// The world-model (WMO) material mapping layer: one MOMT record in, one PLAN out -- which pixel program
// of WmvWmo.shader draws it, which MOMT texture slot each sampler register reads and on which mesh UV
// channel, whether that texture's alpha is read, the blend/depth/cull/wrap state, the preview-light
// bypass, and whether all of that is established, partly established, or not established at all (with
// the reason codes of the material research).
//
// WHY A SEPARATE, PURE TABLE. The same answer decides three things that must never disagree: which
// texture files the load decodes (WmvMainMapObject), what the material binds (WmvWmoBuilder) and what
// the diagnostic prints. When each of those hard-coded "slot +0x0C" on its own they could only stay in
// step by accident. Kept free of UnityEngine so the parser tests can pin every row without a player.
//
// WHAT IS IMPLEMENTED, AND ON WHAT EVIDENCE. Only rows whose behaviour is read from the client's own
// map-object uber shader (pixel permutation 32, vertex permutation 0), or documented and confirmed by
// a census of retail files, are drawn as resolved:
//
//   ids 0 and 16  the client's pixel cases 0 and 16 are byte-identical: diffuse = t0.rgb, no emissive,
//                 t0.a is the only alpha. t0 is slot +0x0C on MOTV set 1 (mesh UV channel 0). With
//                 +0x0C empty the whole output is whatever the exe binds to that register (U-23b), so
//                 such a material is a labelled provisional fallback drawing the register's white.
//   id 23         the client's pixel case 23 with its dedicated raw-UV vertex path: four layers
//                 (+0x18, +0x24, +0x28, +0x2C in registers t1..t4) and their height maps (+0x30..+0x3C
//                 in t17..t20, alpha only), layer k and height k both on MOTV set k with no texture
//                 matrix; per-vertex weights from MOC2 (bytes 2, 1, 0 for layers 1..3, layer 4 = 1 -
//                 saturate(sum)); each weight times its height, sharpened against the largest of all
//                 four, normalised; diffuse = the weighted layer sum; case alpha 1. Its env emissive
//                 (+0x0C on a per-pixel sphere map, times the weighted layer colour and its alpha) and
//                 the lerp of the diffuse toward an exe-side colour by MOC2 byte 3 are NOT drawn (U-E2,
//                 U-E3, U-E4, U-P1, U-23a). An empty layer has its weight forced to 0 -- the client's
//                 result wherever the stored weight is 0, a viewer rule elsewhere, so it carries U-23b. A
//                 material with a layer but no height texture for it is not established (U-23b) and is
//                 drawn by a labelled provisional fallback of the same arithmetic; so is one whose blend
//                 value is 2 or above (U-B2..U-B5, and U-B7 from 3). Its +0x0C env map is
//                 never drawn as a diffuse, not even by the baseline an id-23 material with no layer at
//                 all falls back to.
//   id 13         the client's pixel case 13: two layers, +0x0C on MOTV set 1 and +0x18 on MOTV set 2,
//                 lerped by the per-vertex MOCV set-2 alpha (stored byte / 255, no fix-up): 1 draws
//                 +0x0C, 0 draws +0x18. No emissive, case alpha 1. A material with either slot empty
//                 weights a register the client binds to something unknown (U-23b) and is drawn by a
//                 labelled provisional fallback of the same arithmetic.
//   id 4          the client's pixel case 4: diffuse = t0.rgb of +0x0C on MOTV set 1, no emissive,
//                 case alpha 1 -- the texture's alpha is never read. An empty +0x0C is the same labelled
//                 fallback as for id 0 (U-23b).
//   id 7          the DIFFUSE PART of the client's pixel case 7: the case-13 lerp of +0x0C (MOTV set 1)
//                 and +0x18 (MOTV set 2) by the MOCV set-2 alpha, case alpha 1. Its emissive -- the
//                 lerped colour times its alpha times the env map +0x24 on a generated coordinate, added
//                 after light -- is NOT drawn and +0x24 is not decoded: which coordinate generator the
//                 client selects for that register (U-G1), the env sampler's addressing (U-E4), the
//                 distance fade it is scaled by (U-E3) and whether the program the client picks adds it
//                 at all (U-P1) are exe-side, and the camera axes a reflection or planar generator would
//                 depend on (U-E2) are not established. So every id-7 material is at best
//                 resolved-partial; one with either layer slot empty is the same labelled provisional
//                 fallback as id 13 (U-23b).
//   id 5          the DIFFUSE PART of the client's pixel case 5: t0.rgb of +0x0C on MOTV set 1, case
//                 alpha 1. Its emissive t0.rgb * t0.a * env(+0x18) is not drawn for the same reasons
//                 (U-G1, U-E2, U-E3, U-E4, U-P1), so t0's alpha (the reflectivity mask) and +0x18 are not
//                 read. An empty +0x0C is the labelled fallback of id 4 (U-23b).
//                 The emissive masks of ids 5, 7 and 23 (the factor each env map is multiplied by) are
//                 transcribed below for the tests and the envmask diagnostic view only.
//   blend 0 / 1   opaque, depth write on; 1 additionally discards where that alpha is below 128/255,
//                 the only map-object alpha-test constant in the client shader (forward, alpha-to-
//                 coverage and alpha-tested prepass permutations all use it). Ids whose case alpha is
//                 1 (4, 5, 7, 13, 23) never reach the threshold, so they are drawn without the test.
//   flag 0x04     culling off, else back faces culled.
//   flags 0x40/80 clamp texture addressing on U / V for every texture of the material (default repeat).
//   flag 0x01     the preview light rig is bypassed on ids whose combiner has no emissive term: the
//                 client's unlit lighting mode returns the albedo unchanged. Older-client documentation
//                 honours the flag only for exterior-lit batches, so a material the load finds drawn in
//                 an interior group keeps the bypass and gains U-F3 (NoteUnlitInInteriorGroups).
//
// Everything else keeps the ARCHIVED BASELINE the static stage drew (slot +0x0C on UV channel 0, a
// non-zero blend drawn as the 128/255 key, flag 0x04 for culling, nothing more) and is labelled
// PROVISIONAL, with the reasons it is not resolved:
//
//   unresolved   blend values 2 and above: the client's blend-state table gives the four factors of
//                every row, and older-client documentation maps MOMT value n to row n (not contradicted
//                for 2; contested for 3 and above in 12.1, U-B7). Depth write (U-B2), draw order (U-B3),
//                the output-alpha modifier (U-B4) and the discard rule (U-B5) are not established, so
//                the row is only logged and no Src/Dst mapping is applied. Ids 4, 5, 7, 13 and 23 with
//                such a blend keep their established case arithmetic in a labelled fallback drawn opaque
//                (their case alpha is 1, so the baseline's key would discard what the client never
//                discards); ids 0 and 16 keep the baseline, whose +0x0C is their diffuse. Every other
//                shader id is outside the plan.
//
// All six staged ids (0/16, 23, 13, 4, 7, 5) have their established part wired; what each still lacks is
// a reason code on the material, never a later stage.
//
// Nothing here reads a MOMT colour or a material name, and no rule depends on a particular model or
// file. The only vertex streams read are material inputs, not light: MOC2 (id 23's layer weights) and
// the alpha of MOCV set 2 (the layer factor of ids 13 and 7). MOCV set 1 and the set-2 RGB are never
// read.

using System;
using System.Collections.Generic;

namespace Wmv.Wow
{
    /// <summary>The pixel program of WmvWmo.shader a material runs. The value is what the shader's
    /// _WmoPermutation switches on, so it must not be renumbered.</summary>
    public enum WmoPermutation
    {
        /// <summary>The archived single-texture baseline: +0x0C on UV channel 0, optional 128/255 key.</summary>
        ProvisionalBaseline = 0,
        /// <summary>Client pixel case 0 (and 16, byte-identical): diffuse = t0.rgb, caseAlpha = t0.a.</summary>
        Diffuse = 1,
        /// <summary>Client pixel case 23 without its emissive and without the byte-3 lerp: four layers
        /// blended by MOC2 weights times height alpha; caseAlpha = 1.</summary>
        FourLayer = 2,
        /// <summary>Client pixel case 13: lerp(t1.rgb, t0.rgb, MOCV set-2 alpha); caseAlpha = 1.</summary>
        TwoLayer = 3,
        /// <summary>Client pixel case 4: diffuse = t0.rgb; caseAlpha = 1 (the texture's alpha is not read).</summary>
        Opaque = 4,
        /// <summary>Client pixel case 7 without its env emissive: the diffuse lerp(t1.rgb, t0.rgb, MOCV set-2
        /// alpha), caseAlpha = 1. Today the same arithmetic as TwoLayer; its own value so an env stage can
        /// add the emissive here without touching id 13, and so the log never calls an id-7 draw complete.</summary>
        TwoLayerEnvMetal = 5,
        /// <summary>Client pixel case 5 without its env emissive: diffuse = t0.rgb, caseAlpha = 1. Today the
        /// same arithmetic as Opaque; kept apart for the same reason as TwoLayerEnvMetal.</summary>
        EnvMetal = 6,
    }

    /// <summary>How far a material's drawing is established.</summary>
    public enum WmoResolution
    {
        /// <summary>Every input the draw uses has a client or documented+census source.</summary>
        Resolved,
        /// <summary>The combiner and state are established, but some input the client reads is not (the
        /// reason codes say which); drawn with the established part.</summary>
        ResolvedPartial,
        /// <summary>Not established; drawn with the provisional baseline or a labelled provisional fallback.</summary>
        Unresolved,
    }

    /// <summary>A blend factor, numbered exactly like UnityEngine.Rendering.BlendMode so the builder can
    /// hand the value to a material property; duplicated to keep this file free of UnityEngine.</summary>
    public enum WmoBlendFactor
    {
        Zero = 0, One = 1, DstColor = 2, SrcColor = 3, OneMinusDstColor = 4, SrcAlpha = 5,
        OneMinusSrcColor = 6, DstAlpha = 7, OneMinusDstAlpha = 8, SrcAlphaSaturate = 9, OneMinusSrcAlpha = 10,
    }

    /// <summary>One row of the client's blend-state table (EGxBlend): the colour and alpha factors the
    /// executable's read-only data holds for that index, under the client's factor names. For the log only --
    /// the factors are strings, not WmoBlendFactor values, so no row can reach a material's render state.</summary>
    public struct WmoClientBlendRow
    {
        /// <summary>The EGxBlend index (0..16), not a MOMT blend value.</summary>
        public int Index;
        /// <summary>The documented enumeration name of rows 0..13; "" for rows 14..16, which no source names.</summary>
        public string Name;
        public string SrcColor, DstColor, SrcAlpha, DstAlpha;
    }

    /// <summary>One sampler register of the material and what feeds it.</summary>
    public struct WmoSamplerBinding
    {
        /// <summary>The shader register: _WmoTex{Register} / _WmoUv{Register}. Numbered like the
        /// client's t-registers for the permutation (t0 = 0) where those fit in 0..8; see ClientRegister.</summary>
        public int Register;
        /// <summary>The client's t-register the sampler stands for, for the log. Equal to Register except
        /// for the id-23 height maps, which the client samples from t17..t20 and this shader from 5..8.</summary>
        public int ClientRegister;
        /// <summary>What the client does with the sample, for the log ("diffuse", "provisional diffuse").</summary>
        public string Role;
        /// <summary>MOMT texture slot index into WmoMaterial.TextureSlotOffsets (0 = +0x0C).</summary>
        public int Slot;
        /// <summary>The slot's texture FileDataID; 0 when the slot is empty.</summary>
        public uint FileDataID;
        /// <summary>Mesh UV channel the sampler reads (channel k = MOTV set k + 1).</summary>
        public int UvChannel;
        /// <summary>Whether a term the permutation draws reads the texture's alpha: the 128/255 key of
        /// ids 0/16 and of the baseline, or an id-23 height map. WmvWmo.shader reads alpha only in exactly
        /// those terms (the key is enabled by the plan's AlphaTest, the height registers only exist in
        /// the four-layer permutation), so the builder can upload every file with its alpha and share one
        /// upload between a material that reads the channel and one that does not; this flag is what the
        /// log reports ("alpha read" / "alpha not read").</summary>
        public bool KeepAlpha;
        /// <summary>The register is bound to nothing and nothing it would return reaches the pixel: an
        /// empty id-23 layer, whose weight the shader forces to 0, and that layer's height; or the env map
        /// of ids 5, 7 and 23, which feeds only an emissive that is not drawn. Such a binding is listed for
        /// the log only -- never decoded, uploaded or counted as an empty slot.</summary>
        public bool Unread;
    }

    /// <summary>Everything the viewer decides about one MOMT entry.</summary>
    public sealed class WmoMaterialPlan
    {
        public int Index;
        public uint Shader, Blend, Flags;
        /// <summary>The nine raw texture slots (+0x0C, +0x18, +0x24, +0x28 .. +0x3C); a slot that is not a
        /// texture reference for this shader id reads 0.</summary>
        public uint[] TextureSlots = new uint[WmoMaterial.TextureSlotOffsets.Length];

        public WmoPermutation Permutation;
        /// <summary>The permutation with its client source, for the log.</summary>
        public string PermutationName = "";
        public WmoSamplerBinding[] Samplers = new WmoSamplerBinding[0];
        /// <summary>Which vertex-colour stream the draw reads: "none", the MOC2 layer weights of id 23, or the
        /// MOCV set-2 alpha of ids 13 and 7.</summary>
        public string VertexColour = "none";
        /// <summary>The draw reads MOC2 (id 23): the mesh builder uploads it on WmvWmoBuilder.Moc2UvChannel
        /// for every group one of this material's batches draws in.</summary>
        public bool ReadsMoc2;
        /// <summary>The draw reads the alpha of MOCV set 2 (the layer factor of ids 13 and 7): the mesh builder
        /// uploads it on WmvWmoBuilder.Set2AlphaUvChannel for every group one of this material's batches draws in.</summary>
        public bool ReadsSet2Alpha;
        /// <summary>Id 23: 1 for a layer whose slot names a texture, 0 for an empty one. The shader
        /// multiplies the stored weights by it before the height blend, so an empty layer never draws the
        /// register's default in place of a texture.</summary>
        public float[] LayerMask = { 1f, 1f, 1f, 1f };
        /// <summary>Drawn by a labelled provisional fallback of an established permutation rather than by
        /// the archived baseline: an id-23 material with a layer but no height texture for it (U-23b), an
        /// id-0/16/4/5 material whose +0x0C is empty or an id-13/7 material with an empty layer slot
        /// (U-23b: the register's white default stands in for what the exe binds), or an id-4/5/7/13/23
        /// material whose blend value is 2 or above (U-B2..U-B5).</summary>
        public bool ProvisionalFallback;

        public WmoBlendFactor SrcColor = WmoBlendFactor.One, DstColor = WmoBlendFactor.Zero;
        public WmoBlendFactor SrcAlpha = WmoBlendFactor.One, DstAlpha = WmoBlendFactor.Zero;
        public bool ZWrite = true;
        public bool AlphaTest;
        public float Cutoff = WmoMaterialSemantics.AlphaKeyThreshold;
        public int RenderQueue = WmoMaterialSemantics.QueueGeometry;
        public string RenderType = "Opaque";
        public bool CullOff, ClampU, ClampV, LightBypass;

        public WmoResolution Resolution;
        /// <summary>Reason codes of the material research (U-xx) that apply to this material, in order.</summary>
        public string[] Codes = new string[0];
        /// <summary>One sentence per reason or deliberate choice, for the log.</summary>
        public string[] Notes = new string[0];

        /// <summary>Drawn provisionally: by the archived baseline, or by a labelled fallback of an
        /// established permutation whose inputs are not all established (ProvisionalFallback).</summary>
        public bool Provisional { get { return Permutation == WmoPermutation.ProvisionalBaseline || ProvisionalFallback; } }

        public string ResolutionName
        {
            get
            {
                switch (Resolution)
                {
                    case WmoResolution.Resolved: return "resolved";
                    case WmoResolution.ResolvedPartial: return "resolved-partial";
                    default: return "unresolved";
                }
            }
        }

        /// <summary>"resolved", or e.g. "unresolved: U-B2,U-B3,U-B4,U-B5".</summary>
        public string Verdict
        {
            get { return Codes.Length > 0 ? ResolutionName + ": " + string.Join(",", Codes) : ResolutionName; }
        }
    }

    public static class WmoMaterialSemantics
    {
        /// <summary>The client map-object alpha-test constant, 128/255 (0.501961 in the bytecode).</summary>
        public const float AlphaKeyThreshold = 128f / 255f;

        /// <summary>Unity's Geometry and AlphaTest queue values.</summary>
        public const int QueueGeometry = 2000, QueueAlphaTest = 2450;

        // MOMT +0x00 flag bits, named as the format documentation names them.
        public const uint FlagUnlit = 0x01, FlagUnfogged = 0x02, FlagUnculled = 0x04, FlagExtLight = 0x08,
                          FlagSidn = 0x10, FlagWindow = 0x20, FlagClampS = 0x40, FlagClampT = 0x80, Flag100 = 0x100;

        /// <summary>Rows in the 12.1 client's blend-state table (EGxBlend 0..16).</summary>
        public const int ClientBlendRowCount = 17;

        /// <summary>
        /// The client's blend-state table, keyed by EGxBlend index: source colour, destination colour, source
        /// alpha and destination alpha of every row, as the 12.1 executable's read-only data stores them (its
        /// D3D and Metal encodings agree on all 68 cells). The names of rows 0..13 are the documented
        /// enumeration names of a 7.x client; rows 14..16 have no named source. That MOMT blend value n selects
        /// row n is older-client documentation only -- not contradicted for 2, contested for 3 and above in
        /// 12.1 (U-B7) -- so the table is logged for blend 2 and above (ClientBlendNote) and never read by
        /// ApplyEstablishedBlend or ApplyUnresolvedBlend.
        /// </summary>
        static readonly WmoClientBlendRow[] ClientBlend =
        {
            BlendRow(0, "Opaque", "ONE", "ZERO", "ONE", "ZERO"),
            BlendRow(1, "AlphaKey", "ONE", "ZERO", "ONE", "ZERO"),
            BlendRow(2, "Alpha", "SRC_ALPHA", "INV_SRC_ALPHA", "ONE", "INV_SRC_ALPHA"),
            BlendRow(3, "Add", "SRC_ALPHA", "ONE", "ZERO", "ONE"),
            BlendRow(4, "Mod", "DEST_COLOR", "ZERO", "DEST_ALPHA", "ZERO"),
            BlendRow(5, "Mod2x", "DEST_COLOR", "SRC_COLOR", "DEST_ALPHA", "SRC_ALPHA"),
            BlendRow(6, "ModAdd", "DEST_COLOR", "ONE", "DEST_ALPHA", "ONE"),
            BlendRow(7, "InvSrcAlphaAdd", "INV_SRC_ALPHA", "ONE", "INV_SRC_ALPHA", "ONE"),
            BlendRow(8, "InvSrcAlphaOpaque", "INV_SRC_ALPHA", "ZERO", "INV_SRC_ALPHA", "ZERO"),
            BlendRow(9, "SrcAlphaOpaque", "SRC_ALPHA", "ZERO", "SRC_ALPHA", "ZERO"),
            BlendRow(10, "NoAlphaAdd", "ONE", "ONE", "ZERO", "ONE"),
            BlendRow(11, "ConstantAlpha", "BLEND_FACTOR", "INV_BLEND_FACTOR", "BLEND_FACTOR", "INV_BLEND_FACTOR"),
            BlendRow(12, "Screen", "INV_DEST_COLOR", "ONE", "ONE", "ZERO"),
            BlendRow(13, "BlendAdd", "ONE", "INV_SRC_ALPHA", "ONE", "INV_SRC_ALPHA"),
            BlendRow(14, "", "ONE", "ONE", "ONE", "ONE"),
            BlendRow(15, "", "DEST_ALPHA", "INV_SRC_ALPHA", "ONE", "ONE"),
            BlendRow(16, "", "ZERO", "SRC_COLOR", "ZERO", "SRC_ALPHA"),
        };

        static WmoClientBlendRow BlendRow(int index, string name, string srcColor, string dstColor, string srcAlpha, string dstAlpha)
        {
            return new WmoClientBlendRow { Index = index, Name = name, SrcColor = srcColor, DstColor = dstColor,
                                           SrcAlpha = srcAlpha, DstAlpha = dstAlpha };
        }

        /// <summary>Row `index` of the client's blend-state table; false past its last row (16).</summary>
        public static bool TryClientBlend(uint index, out WmoClientBlendRow row)
        {
            if (index < ClientBlend.Length)
            {
                row = ClientBlend[index];
                return true;
            }
            row = default(WmoClientBlendRow);
            return false;
        }

        /// <summary>
        /// What the log says about the client's blend row for a blend value of 2 or above, with its evidence:
        /// the factor row is the client's own, the hop from the MOMT value to that row is older-client
        /// documentation (contested from 3 up, U-B7), and nothing of it is applied. "" below 2.
        /// </summary>
        public static string ClientBlendNote(uint blend)
        {
            if (blend < 2)
                return "";
            WmoClientBlendRow row;
            if (!TryClientBlend(blend, out row))
                return "no client EGxBlend row " + blend + " (the 12.1 table has " + ClientBlendRowCount + " rows)";
            const string notApplied = "not applied: depth write, discard, order and output alpha are not established";
            string factors = "Src " + row.SrcColor + " Dst " + row.DstColor + " SrcA " + row.SrcAlpha + " DstA " + row.DstAlpha;
            if (blend == 2)
                return "client EGxBlend row 2 " + row.Name + ": " + factors + " (factor row CLIENT 12.1; value 2 -> row 2 per " +
                       "older-client documentation, not contradicted; " + notApplied + ")";
            return "candidate client EGxBlend row " + blend + " " + (row.Name.Length > 0 ? row.Name : "unnamed") + ": " + factors +
                   " (factor row CLIENT 12.1; value " + blend + " -> row " + blend + " only per older-client documentation, " +
                   "contested for 12.1: U-B7; " + notApplied + ")";
        }

        static readonly string[] SlotNames = { "+0x0C", "+0x18", "+0x24", "+0x28", "+0x2C", "+0x30", "+0x34", "+0x38", "+0x3C" };

        /// <summary>The name of texture slot 0..8 ("+0x0C" ...), or "?" outside the range.</summary>
        public static string SlotName(int slot)
        {
            return slot >= 0 && slot < SlotNames.Length ? SlotNames[slot] : "?";
        }

        /// <summary>The plan for one MOMT record.</summary>
        public static WmoMaterialPlan Plan(WmoMaterial m)
        {
            var p = new WmoMaterialPlan { Index = m.Index, Shader = m.Shader, Blend = m.BlendMode, Flags = m.Flags };
            for (int s = 0; s < p.TextureSlots.Length; s++)
                p.TextureSlots[s] = m.IsTextureSlot(s) ? m.GetSlot(s) : 0u;

            bool diffuseId = m.Shader == 0 || m.Shader == 16;
            bool blendEstablished = m.BlendMode == 0 || m.BlendMode == 1;
            if (diffuseId && blendEstablished)
                PlanDiffuse(p, m);
            else if (m.Shader == 23 && AnyLayerPresent(p))
                // Whatever the blend: the baseline would draw the +0x0C env map as a flat diffuse, which
                // is exactly the misreading this table exists to prevent. A blend of 2 or above is a
                // labelled fallback of the case arithmetic, as for ids 4/5/7/13.
                PlanFourLayer(p);
            else if (m.Shader == 13 || m.Shader == 7)
                // Case 7's diffuse is case 13's lerp; only its emissive (not drawn) differs.
                PlanTwoLayer(p, m, m.Shader == 7);
            else if (m.Shader == 4 || m.Shader == 5)
                // Case 5's diffuse is case 4's t0.rgb with alpha 1; only its emissive (not drawn) differs.
                PlanOpaque(p, m, m.Shader == 5);
            else
                PlanProvisional(p);
            return p;
        }

        /// <summary>Add every non-zero texture FileDataID the plan samples: the only files the load needs
        /// to decode for this material.</summary>
        public static void CollectSampledTextures(WmoMaterialPlan plan, ICollection<uint> into)
        {
            if (plan == null || into == null) return;
            foreach (WmoSamplerBinding b in plan.Samplers)
                if (b.FileDataID != 0 && !b.Unread && !into.Contains(b.FileDataID))
                    into.Add(b.FileDataID);
        }

        /// <summary>
        /// A reason code a plan cannot know by itself: the load found that some vertices a draw covers
        /// lack a stream the permutation reads (MOC2, the MOCV set-2 alpha, or a MOTV set), and fed the stated
        /// default there. What the client reads for an absent stream is not established (U-V4), so the code
        /// and a note with the vertex count go on the material, and a resolved material becomes
        /// resolved-partial -- the established part is still what the other vertices draw. Called once per
        /// material by the builder, after every batch was counted; no-op for 0 vertices.
        /// </summary>
        public static void NoteAbsentStream(WmoMaterialPlan p, long vertices, string streams)
        {
            if (p == null || vertices <= 0)
                return;
            if (Array.IndexOf(p.Codes, "U-V4") < 0)
            {
                var codes = new List<string>(p.Codes) { "U-V4" };
                p.Codes = codes.ToArray();
            }
            var notes = new List<string>(p.Notes)
            {
                string.Format("{0} drawn vertex(es) lack {1}: drawn with the stated default there; what the client reads " +
                              "for an absent stream is unknown (U-V4)", vertices, streams),
            };
            p.Notes = notes.ToArray();
            if (p.Resolution == WmoResolution.Resolved)
                p.Resolution = WmoResolution.ResolvedPartial;
        }

        /// <summary>
        /// A second reason code a plan cannot know by itself: the load found batches drawing a material whose
        /// F_UNLIT preview-light bypass applies (ids 0, 4, 13, 16) in interior groups (MOGP flag 0x2000).
        /// Older-client documentation honours F_UNLIT only for exterior-lit batches, the interior branch
        /// ignoring it; what the 12.1 client does in an interior group is not established (U-F3). The bypass
        /// stays as drawn: the code and a note with the batch count go on the material, and a resolved
        /// material becomes resolved-partial, as with U-V4. Called once per material by the builder, after
        /// every batch was counted; no-op without the bypass or for 0 batches.
        /// </summary>
        public static void NoteUnlitInInteriorGroups(WmoMaterialPlan p, int interiorBatches)
        {
            if (p == null || !p.LightBypass || interiorBatches <= 0)
                return;
            if (Array.IndexOf(p.Codes, "U-F3") < 0)
            {
                var codes = new List<string>(p.Codes) { "U-F3" };
                p.Codes = codes.ToArray();
            }
            var notes = new List<string>(p.Notes)
            {
                string.Format("flag 0x01 F_UNLIT bypass kept on {0} drawn batch(es) in interior groups (MOGP 0x2000): " +
                              "older-client documentation limits F_UNLIT to exterior-lit batches; its scope in 12.1 is " +
                              "not established (U-F3)", interiorBatches),
            };
            p.Notes = notes.ToArray();
            if (p.Resolution == WmoResolution.Resolved)
                p.Resolution = WmoResolution.ResolvedPartial;
        }

        // ------------------------------------------------------------------ ids 0 and 16

        static void PlanDiffuse(WmoMaterialPlan p, WmoMaterial m)
        {
            var codes = new List<string>();
            var notes = new List<string>();
            p.Permutation = WmoPermutation.Diffuse;
            p.PermutationName = m.Shader == 16
                ? "diffuse (client pixel case 16, byte-identical to case 0)"
                : "diffuse (client pixel case 0)";

            // t0 is the only register the case samples, and its alpha feeds nothing but the key and the
            // blend output -- so an opaque material drops it and the key keeps it.
            bool key = p.Blend == 1;
            bool empty = p.TextureSlots[0] == 0;
            p.Samplers = new[]
            {
                new WmoSamplerBinding { Register = 0, Role = empty ? "diffuse MISSING (PROVISIONAL: the register's white default)" : "diffuse",
                                        Slot = 0, FileDataID = p.TextureSlots[0], UvChannel = 0, KeepAlpha = key },
            };
            ApplyEstablishedBlend(p, key);
            ApplyEstablishedFlags(p, notes);

            if (empty)
                NoteEmptyWholeOutputRegister(p, codes, notes);
            NoteUnestablishedFields(p, m, codes, notes);
            if (key)
                notes.Add("blend 1 -> mode mapping and alpha-to-coverage under MSAA are exe-side (U-B1, non-blocking at one sample)");
            if ((p.Flags & FlagUnfogged) != 0)
                notes.Add("flag 0x02 F_UNFOGGED: no effect, the preview has no fog");

            if (empty)
                p.PermutationName = "PROVISIONAL diffuse fallback (client pixel case " + (m.Shader == 16 ? "16" : "0") +
                                    " arithmetic; an empty +0x0C reads white)";
            p.Resolution = p.ProvisionalFallback ? WmoResolution.Unresolved
                         : codes.Count > 0 ? WmoResolution.ResolvedPartial : WmoResolution.Resolved;
            p.Codes = codes.ToArray();
            p.Notes = notes.ToArray();
        }

        /// <summary>
        /// Ids 0, 16, 4 and 5 with +0x0C empty: t0 is the only register their drawn diffuse reads, so the
        /// WHOLE output is whatever the exe binds to an unused register (U-23b), which no source states.
        /// The shader's white default stands in for it, and the material is a labelled provisional
        /// fallback -- never counted as established, exactly like an id-13/7 material whose empty layer
        /// is only weighted on some vertices.
        /// </summary>
        static void NoteEmptyWholeOutputRegister(WmoMaterialPlan p, List<string> codes, List<string> notes)
        {
            p.ProvisionalFallback = true;
            codes.Add("U-23b");
            notes.Add("PROVISIONAL fallback: slot +0x0C is empty; what the client binds to an unused register is unknown " +
                      "(U-23b), so the whole surface is drawn with the register's white default");
        }

        /// <summary>
        /// Flags 0x08/0x10/0x20/0x100 and a non-zero +0x28 on an id that does not read +0x28 as a texture:
        /// stored, with no established effect (U-F2). One code however many causes, every cause in a note.
        /// </summary>
        static void NoteUnestablishedFields(WmoMaterialPlan p, WmoMaterial m, List<string> codes, List<string> notes)
        {
            uint ignoredFlags = p.Flags & (FlagExtLight | FlagSidn | FlagWindow | Flag100);
            if (ignoredFlags != 0)
            {
                codes.Add("U-F2");
                notes.Add("flag(s) " + FlagNames(ignoredFlags) + " have no established effect (U-F2); not applied");
            }
            uint plus28 = m.GetSlot(3);   // +0x28 is a texture slot only for ids 22/23, but the value is stored
            if (plus28 != 0)
            {
                if (!codes.Contains("U-F2")) codes.Add("U-F2");
                notes.Add(string.Format("+0x28 holds 0x{0:X8}, whose meaning is unknown (U-F2); not applied", plus28));
            }
        }

        // ------------------------------------------------------------------ ids 13 and 4

        /// <summary>
        /// Blend 2 and above on an id whose case alpha is 1 (4, 5, 7, 13, 23): the case arithmetic is established,
        /// the blend state is not. Drawn by a labelled fallback in the same opaque state the archived
        /// baseline uses -- One/Zero, depth write -- but without its 128/255 key, which on these ids would
        /// discard pixels the client's case never discards. No Src/Dst mapping is chosen; the client's blend
        /// row is logged with its evidence (ClientBlendNote), U-B7 from 3 up, U-B1 only past the table's last row.
        /// </summary>
        static void ApplyUnresolvedBlend(WmoMaterialPlan p, List<string> codes, List<string> notes)
        {
            AddUnresolvedBlendCodes(p, codes);
            notes.Add("blend " + p.Blend + ": depth write (U-B2), draw order (U-B3), output alpha (U-B4) and discard (U-B5) are " +
                      "not established; PROVISIONAL fallback: the client case drawn opaque with depth write, no alpha test " +
                      "(case alpha 1), no blend factors guessed");
            notes.Add(ClientBlendNote(p.Blend));
        }

        /// <summary>The codes of an unresolved blend value (2 and above), in log order: U-B1 when even the
        /// older-client reading finds no row (past 16), U-B2..U-B5, and U-B7 when the value's row is contested (3 up).</summary>
        static void AddUnresolvedBlendCodes(WmoMaterialPlan p, List<string> codes)
        {
            if (p.Blend >= ClientBlendRowCount) codes.Add("U-B1");
            codes.Add("U-B2"); codes.Add("U-B3"); codes.Add("U-B4"); codes.Add("U-B5");
            if (p.Blend >= 3) codes.Add("U-B7");
        }

        /// <summary>
        /// Id 13, the client's pixel case 13: t0 = +0x0C on MOTV set 1, t1 = +0x18 on MOTV set 2,
        /// diffuse = lerp(t1.rgb, t0.rgb, va) with va the alpha of MOCV set 2, case alpha 1.
        /// Id 7 (envMetal), the diffuse part of the client's pixel case 7: the same two registers, the same
        /// lerp (case 7 lerps rgba, and only the emissive reads the lerped alpha), case alpha 1. Its emissive
        /// -- the lerped colour times its alpha times the env map +0x24 (t2) on a generated coordinate, added
        /// after light -- is not drawn, so t2 is listed unread and every id-7 material carries U-G1, U-E2,
        /// U-E3, U-E4 and U-P1.
        /// </summary>
        static void PlanTwoLayer(WmoMaterialPlan p, WmoMaterial m, bool envMetal)
        {
            var codes = new List<string>();
            var notes = new List<string>();
            uint layer1 = p.TextureSlots[0], layer2 = p.TextureSlots[1], env = p.TextureSlots[2];
            string caseName = envMetal ? "case 7" : "case 13";
            p.Permutation = envMetal ? WmoPermutation.TwoLayerEnvMetal : WmoPermutation.TwoLayer;
            // Neither texture's alpha is read by what is drawn: the diffuse lerps rgb only and the case alpha
            // is the constant 1. (Case 7's emissive reads the lerped alpha, but that emissive is not drawn.)
            var samplers = new List<WmoSamplerBinding>
            {
                new WmoSamplerBinding
                {
                    Register = 0, ClientRegister = 0, Slot = 0, FileDataID = layer1, UvChannel = 0, KeepAlpha = false,
                    Role = layer1 != 0 ? "layer 1 (va = 1)" : "layer 1 (va = 1) MISSING (PROVISIONAL: the register's white default)",
                },
                // MOTV set 2, not set 1: every retail id-13 and id-7 batch sits in a group with at least two
                // sets, and the documented texture-coordinate count of both ids is 2.
                new WmoSamplerBinding
                {
                    Register = 1, ClientRegister = 1, Slot = 1, FileDataID = layer2, UvChannel = 1, KeepAlpha = false,
                    Role = layer2 != 0 ? "layer 2 (va = 0)" : "layer 2 (va = 0) MISSING (PROVISIONAL: the register's white default)",
                },
            };
            if (envMetal)
            {
                // Listed so the log shows the file; it feeds only the emissive, which is not drawn -- so it is
                // neither bound nor decoded. Its coordinate is generated (most id-7 batches have no MOTV set
                // 3), and which generator the client selects is exe-side (U-G1).
                samplers.Add(new WmoSamplerBinding
                {
                    Register = 2, ClientRegister = 2, Slot = 2, FileDataID = env, UvChannel = 0, KeepAlpha = false, Unread = true,
                    Role = "env map (emissive, not drawn)",
                });
            }
            p.Samplers = samplers.ToArray();
            p.ReadsSet2Alpha = true;
            p.VertexColour = "MOCV set-2 alpha as the layer factor va (stored byte / 255, no fix-up; va 1 -> +0x0C, " +
                             "0 -> +0x18; set 1 and the set-2 RGB not read)";

            // Case alpha is 1, so the client's 128/255 test never discards: blend 1 draws exactly as 0.
            ApplyEstablishedBlend(p, false);
            ApplyEstablishedFlags(p, notes);

            bool blendUnresolved = p.Blend >= 2;
            if (blendUnresolved)
                ApplyUnresolvedBlend(p, codes, notes);
            bool missing = layer1 == 0 || layer2 == 0;
            if (missing)
            {
                // The client lerps between both registers whatever the slots hold, so wherever va weights
                // an empty one the result is whatever the exe binds there. The same arithmetic is drawn with
                // the register's white default and labelled; the builder counts the vertices it touches.
                codes.Add("U-23b");
                notes.Add("PROVISIONAL fallback: " + (layer1 == 0 && layer2 == 0 ? "+0x0C and +0x18 are" : layer1 == 0 ? "+0x0C is" : "+0x18 is") +
                          " empty; what the client binds to an unused register is unknown (U-23b), so it is drawn with the " +
                          "register's white default wherever va weights it");
            }
            if (envMetal)
                NoteEnvEmissiveNotDrawn(p, codes, notes, "+0x24", TwoLayerEnvEmissive, "VS generator cb2[1].z (U-G1)", env != 0);
            NoteUnestablishedFields(p, m, codes, notes);
            if (p.Blend == 1)
                notes.Add("blend 1: " + caseName + "'s alpha is 1, so the client's 128/255 test never discards; drawn untested, blending off");
            if ((p.Flags & FlagUnfogged) != 0)
                notes.Add("flag 0x02 F_UNFOGGED: no effect, the preview has no fog");

            p.ProvisionalFallback = missing || blendUnresolved;
            string what = envMetal ? "two-layer env metal" : "two-layer";
            p.PermutationName = p.ProvisionalFallback
                ? "PROVISIONAL " + what + " fallback (client pixel " + caseName + (envMetal ? " diffuse" : "") + " arithmetic; " +
                  (missing ? "an empty layer slot reads white" : "blend state not established") + ")"
                : envMetal ? "two-layer env metal, diffuse part (client pixel case 7 without its env emissive)"
                           : "two-layer (client pixel case 13)";
            p.Resolution = p.ProvisionalFallback ? WmoResolution.Unresolved
                         : codes.Count > 0 ? WmoResolution.ResolvedPartial : WmoResolution.Resolved;
            p.Codes = codes.ToArray();
            p.Notes = notes.ToArray();
        }

        /// <summary>
        /// Ids 5 and 7: the env emissive the client adds after light is not drawn. Its texture's coordinate
        /// comes from a generator the exe selects per register (U-G1) and, for every reflection or planar
        /// generator, depends on the client's camera axes (U-E2); the env sampler's addressing is exe-side
        /// (U-E4); it is scaled by a distance fade whose constants are exe-side (U-E3); and the client may
        /// draw the batch with a program that has no emissive or tints it (U-P1). Every such material carries
        /// all five codes. F_UNLIT is not honoured on these ids either: the client's unlit mode still adds the
        /// emissive, which the preview-light bypass alone would not reproduce (U-F1).
        /// </summary>
        static void NoteEnvEmissiveNotDrawn(WmoMaterialPlan p, List<string> codes, List<string> notes, string envSlot,
                                            string emissive, string coordinate, bool envPresent)
        {
            codes.Add("U-G1");
            codes.Add("U-E2");
            codes.Add("U-E3");
            codes.Add("U-E4");
            codes.Add("U-P1");
            notes.Add(EnvEmissiveNote(envSlot, emissive, coordinate + ", camera axes U-E2", "U-G1, U-E2, U-E3, U-E4, U-P1", envPresent));
            if ((p.Flags & FlagUnlit) != 0)
            {
                codes.Add("U-F1");
                notes.Add("flag 0x01 F_UNLIT on an id with an emissive term: not applied (U-F1)");
            }
        }

        // The client's emissive equations (pixel cases 5, 7 and 23), in register terms, for the log.
        const string EnvMetalEmissive = "t0.rgb * t0.a * env(t1).rgb";
        const string TwoLayerEnvEmissive = "c.rgb * c.a * env(t2).rgb, c = lerp(t1, t0, va) as rgba";
        const string FourLayerEnvEmissive = "env(t0).rgb * mix.rgb * mix.a, mix = the weighted four-layer rgba";

        /// <summary>
        /// The note on an env emissive that is not drawn (ids 5, 7 and 23): the client's equation, the coordinate
        /// it would be sampled at, and every input that is not established, each with its code.
        /// </summary>
        static string EnvEmissiveNote(string envSlot, string emissive, string coordinate, string codes, bool envPresent)
        {
            if (!envPresent)
                return envSlot + " is empty: no env map to draw (the emissive " + emissive + " is not drawn in any case: " + codes +
                       "; what the client binds to the empty register is unknown, U-23b)";
            return "emissive " + emissive + " (CLIENT), added after light: NOT drawn, and " + envSlot + " is not decoded -- " +
                   "coordinate " + coordinate + "; env sampler address mode exe-side (U-E4); distance fade saturate(z*s+b) per " +
                   "instance (U-E3); the client may draw the batch with a program without the emissive (bit6, or the " +
                   "single-texture family), or tint or replace it via the edge selector (U-P1)";
        }

        /// <summary>
        /// One channel of id 13's diffuse, as client pixel case 13 computes it: layer2 + va * (layer1 - layer2),
        /// where va is the MOCV set-2 alpha as 0..1 (SetTwoAlpha). A reference transcription, not called by
        /// the draw: the parser tests pin its polarity (va 1 is +0x0C, va 0 is +0x18), and the lifecycle
        /// self-test renders permutation 3 of WmvWmo.shader on a synthetic quad and compares the pixel with
        /// this function, so a change to either side shows up against the other.
        /// </summary>
        public static float TwoLayerMix(float layer1, float layer2, float va)
        {
            return layer2 + va * (layer1 - layer2);
        }

        /// <summary>The layer factor of id 13 from a stored MOCV set-2 alpha byte: raw, no fix-up.</summary>
        public static float SetTwoAlpha(byte stored)
        {
            return stored / 255f;
        }

        // The emissive MASKS of client pixel cases 5, 7 and 23: the factor each case multiplies its env map's
        // rgb by before adding it after light. Reference transcriptions of the client's equations, not called
        // by the draw (no emissive is drawn): the parser tests pin them on hand vectors, and the lifecycle
        // self-test renders the envmask view (8) of permutations 6, 5 and 2 against them. Each returns rgb.

        /// <summary>Case 5 (id 5): t0.rgb * t0.a -- the +0x0C colour times its alpha.</summary>
        public static float[] EnvMetalMask(float r, float g, float b, float a)
        {
            return new[] { r * a, g * a, b * a };
        }

        /// <summary>Case 7 (id 7): c.rgb * c.a with c = layer2 + va * (layer1 - layer2) per channel, alpha included
        /// (layer1 = +0x0C, layer2 = +0x18, each rgba; va the MOCV set-2 alpha as 0..1) -- so both layer alphas count.</summary>
        public static float[] TwoLayerEnvMask(float[] layer1, float[] layer2, float va)
        {
            float a = TwoLayerMix(layer1[3], layer2[3], va);
            return new[] { TwoLayerMix(layer1[0], layer2[0], va) * a, TwoLayerMix(layer1[1], layer2[1], va) * a,
                           TwoLayerMix(layer1[2], layer2[2], va) * a };
        }

        /// <summary>Case 23 (id 23): mix.rgb * mix.a with mix the weighted four-layer rgba (the layers times
        /// FourLayerWeights, before the client's byte-3 lerp).</summary>
        public static float[] FourLayerEnvMask(float[] mix)
        {
            return new[] { mix[0] * mix[3], mix[1] * mix[3], mix[2] * mix[3] };
        }

        /// <summary>
        /// Id 4, the client's pixel case 4: diffuse = t0.rgb of +0x0C on MOTV set 1, case alpha 1.
        /// Id 5 (envMetal), the diffuse part of the client's pixel case 5: the same t0.rgb, case alpha 1. Its
        /// emissive -- t0.rgb times t0.a (the reflectivity mask) times the env map +0x18 (t1) on a generated
        /// coordinate, added after light -- is not drawn, so t0's alpha is dropped, t1 is listed unread and
        /// every id-5 material carries U-G1, U-E2, U-E3, U-E4 and U-P1.
        /// </summary>
        static void PlanOpaque(WmoMaterialPlan p, WmoMaterial m, bool envMetal)
        {
            var codes = new List<string>();
            var notes = new List<string>();
            string caseName = envMetal ? "case 5" : "case 4";
            bool empty = p.TextureSlots[0] == 0;
            p.Permutation = envMetal ? WmoPermutation.EnvMetal : WmoPermutation.Opaque;
            var samplers = new List<WmoSamplerBinding>
            {
                new WmoSamplerBinding
                {
                    Register = 0, Slot = 0, FileDataID = p.TextureSlots[0], UvChannel = 0, KeepAlpha = false,
                    Role = empty ? "diffuse MISSING (PROVISIONAL: the register's white default)"
                         : envMetal ? "diffuse (alpha = reflectivity mask, read only by the emissive: not read)" : "diffuse (alpha not read)",
                },
            };
            if (envMetal)
            {
                // Listed so the log shows the file; it feeds only the emissive, which is not drawn. Its
                // coordinate is generated (a quarter of retail id-5 batches sit in groups with one MOTV set),
                // and which generator the client selects is exe-side (U-G1).
                samplers.Add(new WmoSamplerBinding
                {
                    Register = 1, ClientRegister = 1, Slot = 1, FileDataID = p.TextureSlots[1], UvChannel = 0, KeepAlpha = false,
                    Unread = true, Role = "env map (emissive, not drawn)",
                });
            }
            p.Samplers = samplers.ToArray();
            ApplyEstablishedBlend(p, false);
            ApplyEstablishedFlags(p, notes);

            bool blendUnresolved = p.Blend >= 2;
            if (blendUnresolved)
                ApplyUnresolvedBlend(p, codes, notes);
            if (empty)
                // As for ids 0/16: the register is the whole output, and what the client binds there is
                // exe-side.
                NoteEmptyWholeOutputRegister(p, codes, notes);
            if (envMetal)
                NoteEnvEmissiveNotDrawn(p, codes, notes, "+0x18", EnvMetalEmissive,
                                        "VS generator cb2[1].y or the cb0[5].y PS sphere map (U-G1)", p.TextureSlots[1] != 0);
            NoteUnestablishedFields(p, m, codes, notes);
            if (p.Blend == 1)
                notes.Add("blend 1: " + caseName + "'s alpha is 1, so the client's 128/255 test never discards; drawn untested, blending off");
            if ((p.Flags & FlagUnfogged) != 0)
                notes.Add("flag 0x02 F_UNFOGGED: no effect, the preview has no fog");

            p.ProvisionalFallback = empty || blendUnresolved;
            string why = empty ? "an empty +0x0C reads white" : "blend state not established";
            p.PermutationName = p.ProvisionalFallback
                ? (envMetal ? "PROVISIONAL env metal fallback (client pixel case 5 diffuse arithmetic; " + why + ")"
                            : "PROVISIONAL opaque fallback (client pixel case 4 arithmetic; " + why + ")")
                : envMetal ? "env metal, diffuse part (client pixel case 5 without its env emissive)"
                           : "opaque (client pixel case 4)";
            p.Resolution = p.ProvisionalFallback ? WmoResolution.Unresolved
                         : codes.Count > 0 ? WmoResolution.ResolvedPartial : WmoResolution.Resolved;
            p.Codes = codes.ToArray();
            p.Notes = notes.ToArray();
        }

        // ------------------------------------------------------------------ id 23

        /// <summary>Id 23: layer k (k = 0..3) is texture slot FirstLayerSlot + k (+0x18, +0x24, +0x28,
        /// +0x2C) in shader register 1 + k; its height map is slot FirstHeightSlot + k (+0x30 .. +0x3C) in
        /// shader register 5 + k (client t17 + k). Both read mesh UV channel k.</summary>
        public const int FirstLayerSlot = 1, FirstHeightSlot = 5, LayerCount = 4;

        /// <summary>The client clamps each height alpha to at least this before weighting (case 23).</summary>
        public const float MinLayerHeight = 0.004f;

        static bool AnyLayerPresent(WmoMaterialPlan p)
        {
            for (int k = 0; k < LayerCount; k++)
                if (p.TextureSlots[FirstLayerSlot + k] != 0) return true;
            return false;
        }

        static void PlanFourLayer(WmoMaterialPlan p)
        {
            var codes = new List<string>();
            var notes = new List<string>();
            var emptyLayers = new List<string>();
            var missingHeights = new List<string>();
            var samplers = new WmoSamplerBinding[1 + 2 * LayerCount];

            // Register 0 is the env map. It is listed so the log shows the file, but it feeds only the
            // emissive, which is not drawn -- so it is neither bound nor decoded.
            samplers[0] = new WmoSamplerBinding
            {
                Register = 0, ClientRegister = 0, Slot = 0, FileDataID = p.TextureSlots[0], UvChannel = 0,
                KeepAlpha = false, Unread = true, Role = "env map (emissive, not drawn)",
            };
            for (int k = 0; k < LayerCount; k++)
            {
                uint layer = p.TextureSlots[FirstLayerSlot + k];
                uint height = p.TextureSlots[FirstHeightSlot + k];
                bool present = layer != 0;
                p.LayerMask[k] = present ? 1f : 0f;
                if (!present) emptyLayers.Add((k + 1).ToString());
                else if (height == 0) missingHeights.Add((k + 1).ToString());
                // Layer k and height k both read MOTV set k: in mode 23 the client's vertex program copies
                // TEXCOORD0..3 through with no texture matrix, and the pixel case samples t1 and t17 with
                // the first, t2 and t18 with the second, t3/t19 and t4/t20 with the third and fourth.
                samplers[1 + k] = new WmoSamplerBinding
                {
                    Register = 1 + k, ClientRegister = 1 + k, Slot = FirstLayerSlot + k, FileDataID = layer, UvChannel = k,
                    // The weighted layer alpha feeds only the env emissive, which is not drawn, so it is not
                    // read. (A file used both as a layer and as a height map -- common -- still shares one
                    // upload: uploads keep the channel whether or not a term reads it.)
                    KeepAlpha = false, Unread = !present,
                    Role = present ? "layer " + (k + 1) : "layer " + (k + 1) + " (empty: weight forced to 0)",
                };
                samplers[1 + LayerCount + k] = new WmoSamplerBinding
                {
                    Register = 1 + LayerCount + k, ClientRegister = 17 + k, Slot = FirstHeightSlot + k, FileDataID = height,
                    UvChannel = k, KeepAlpha = true, Unread = !present,
                    Role = !present ? "height " + (k + 1) + " (its layer is empty: not read)"
                         : height == 0 ? "height " + (k + 1) + " MISSING (PROVISIONAL: the register's white default, alpha 1)"
                         : "height " + (k + 1) + " (alpha only)",
                };
            }
            p.Samplers = samplers;
            p.Permutation = WmoPermutation.FourLayer;
            p.ReadsMoc2 = true;
            p.VertexColour = "MOC2 layer weights (bytes 2,1,0 -> layers 1,2,3; layer 4 = 1 - saturate(sum); byte 3 not read)";

            // Case alpha is 1, so the client's 128/255 test can never discard: blend 1 draws exactly as 0.
            ApplyEstablishedBlend(p, false);
            ApplyEstablishedFlags(p, notes);

            bool blendUnresolved = p.Blend >= 2;
            if (blendUnresolved)
                ApplyUnresolvedBlend(p, codes, notes);
            bool missingHeight = missingHeights.Count > 0;
            if (missingHeight)
            {
                // The client samples t17..t20 whatever the slots hold. What an unbound register returns is
                // exe-side, and it decides how such a layer blends (an alpha of 1 sharpens the transition,
                // an alpha near 0 makes it almost linear) -- so the material is not established. The same
                // arithmetic is drawn with the register's white default and labelled as a fallback.
                codes.Add("U-23b");
                notes.Add("PROVISIONAL fallback: layer(s) " + string.Join(", ", missingHeights.ToArray()) +
                          " have a texture but no height map; what the client binds to that height register is unknown " +
                          "(U-23b), so it is drawn with the register's white default (alpha 1)");
            }
            if (emptyLayers.Count > 0 && !codes.Contains("U-23b"))
                // Forcing an empty layer's weight to 0 is a viewer rule: it equals the client only where the
                // stored weight of that layer is 0 (almost always, but not everywhere -- the layer-4 remainder
                // of a 1110 material is non-zero on about a tenth of its vertices). Elsewhere the client weights
                // whatever the exe binds to the register, so the code goes on the material, and the builder
                // counts the vertices it touches.
                codes.Add("U-23b");
            bool fallback = missingHeight || blendUnresolved;
            p.ProvisionalFallback = fallback;
            codes.Add("U-23a");
            codes.Add("U-E2");
            codes.Add("U-E3");
            codes.Add("U-E4");
            codes.Add("U-P1");
            notes.Add("diffuse = the weighted layer sum; the client also lerps it toward an exe-side colour by MOC2 byte 3 " +
                      "-- not applied (U-23a)");
            // Always sampled by the client's case, on a per-pixel sphere map of the transformed position and its
            // normal (that their space is camera space is inference): no generator question (U-G1), but the camera
            // axes, the addressing, the fade and the program are open.
            notes.Add(EnvEmissiveNote("+0x0C", FourLayerEnvEmissive, "per-pixel sphere map (camera axes U-E2)",
                                      "U-E2, U-E3, U-E4, U-P1", p.TextureSlots[0] != 0));
            if (emptyLayers.Count > 0)
                notes.Add("layer(s) " + string.Join(", ", emptyLayers.ToArray()) + " empty: weight forced to 0 before the " +
                          "height blend (equal to the client wherever the stored MOC2 weight of that layer is 0; elsewhere " +
                          "the client weights an unbound register, U-23b)");
            if (p.Blend == 1)
                notes.Add("blend 1: case 23's alpha is 1, so the client's 128/255 test never discards; drawn untested, blending off");
            if ((p.Flags & FlagUnlit) != 0)
            {
                // The unlit mode still adds the emissive, which this permutation does not draw (U-F1).
                codes.Add("U-F1");
                notes.Add("flag 0x01 F_UNLIT on an id with an emissive term: not applied (U-F1)");
            }
            uint ignoredFlags = p.Flags & (FlagExtLight | FlagSidn | FlagWindow | Flag100);
            if (ignoredFlags != 0)
            {
                codes.Add("U-F2");
                notes.Add("flag(s) " + FlagNames(ignoredFlags) + " have no established effect (U-F2); not applied");
            }
            if ((p.Flags & FlagUnfogged) != 0)
                notes.Add("flag 0x02 F_UNFOGGED: no effect, the preview has no fog");

            p.PermutationName = fallback
                ? "PROVISIONAL four-layer fallback (client pixel case 23 arithmetic; " +
                  (missingHeight ? "a missing height map reads alpha 1" : "blend state not established") + ")"
                : "four-layer (client pixel case 23, without the env emissive and the byte-3 lerp)";
            p.Resolution = fallback ? WmoResolution.Unresolved : WmoResolution.ResolvedPartial;
            p.Codes = codes.ToArray();
            p.Notes = notes.ToArray();
        }

        /// <summary>
        /// The id-23 blend weights of one sample -- the arithmetic of client pixel case 23, plus the viewer's
        /// empty-layer mask -- as a reference transcription the draw does not call: the parser tests pin it on
        /// hand vectors, and the lifecycle self-test renders permutation 2 of WmvWmo.shader on a synthetic
        /// quad and compares the effective weights and the mixed colour with it, so a change to either side
        /// shows up against the other. w1..w3 are MOC2 bytes 2, 1,
        /// 0 as 0..1, heights the four height-map alphas, mask the plan's LayerMask. Returns b1..b4 summing to
        /// 1, or all 0 when every masked weight is 0 (the shader then draws black instead of dividing by 0:
        /// both floor the divisor at FourLayerWeightEpsilon).
        /// </summary>
        public static float[] FourLayerWeights(float w1, float w2, float w3, float[] heights, float[] mask)
        {
            float[] w = { w1, w2, w3, 1f - Saturate(w1 + w2 + w3) };
            var aw = new float[LayerCount];
            float max = 0f;
            for (int k = 0; k < LayerCount; k++)
            {
                aw[k] = w[k] * mask[k] * Math.Max(heights[k], MinLayerHeight);
                max = Math.Max(max, aw[k]);
            }
            var b = new float[LayerCount];
            float total = 0f;
            for (int k = 0; k < LayerCount; k++)
            {
                // A layer keeps its weight in proportion to how close it comes to the strongest one; a layer
                // a full unit below it drops out. Then the four are normalised.
                b[k] = (1f - Saturate(max - aw[k])) * aw[k];
                total += b[k];
            }
            float divisor = Math.Max(total, FourLayerWeightEpsilon);
            for (int k = 0; k < LayerCount; k++)
                b[k] /= divisor;
            return b;
        }

        /// <summary>The floor of the four-layer weight total's divisor (shader and C#), so no blend divides by 0.</summary>
        public const float FourLayerWeightEpsilon = 1e-6f;

        static float Saturate(float v)
        {
            return v < 0f ? 0f : v > 1f ? 1f : v;
        }

        /// <summary>Blend 0 and 1: the client draws both with blending off and depth writes on; 1 discards
        /// where the case alpha is below 128/255.</summary>
        static void ApplyEstablishedBlend(WmoMaterialPlan p, bool key)
        {
            p.SrcColor = WmoBlendFactor.One;  p.DstColor = WmoBlendFactor.Zero;
            p.SrcAlpha = WmoBlendFactor.One;  p.DstAlpha = WmoBlendFactor.Zero;
            p.ZWrite = true;
            p.AlphaTest = key;
            p.Cutoff = AlphaKeyThreshold;
            p.RenderQueue = key ? QueueAlphaTest : QueueGeometry;
            p.RenderType = key ? "TransparentCutout" : "Opaque";
        }

        static void ApplyEstablishedFlags(WmoMaterialPlan p, List<string> notes)
        {
            p.CullOff = (p.Flags & FlagUnculled) != 0;
            p.ClampU = (p.Flags & FlagClampS) != 0;
            p.ClampV = (p.Flags & FlagClampT) != 0;
            // Only on ids whose combiner has no emissive: with an emissive the client still adds it in
            // the unlit mode, which the bypass alone would not reproduce (U-F1).
            p.LightBypass = (p.Flags & FlagUnlit) != 0 &&
                            (p.Shader == 0 || p.Shader == 4 || p.Shader == 13 || p.Shader == 16);
            if (p.LightBypass)
                notes.Add("flag 0x01 F_UNLIT: the preview light rig is bypassed (lum = 1)");
        }

        // ------------------------------------------------------------------ everything else

        static void PlanProvisional(WmoMaterialPlan p)
        {
            var codes = new List<string>();
            var notes = new List<string>();
            p.Permutation = WmoPermutation.ProvisionalBaseline;
            p.PermutationName = "PROVISIONAL archived baseline (+0x0C on UV channel 0; non-zero blend drawn as the 128/255 key)";

            // Exactly the static stage's drawing, so nothing that is not established changes on screen:
            // the key reads +0x0C's alpha whenever the blend is non-zero, culling follows 0x04 and nothing
            // else is applied.
            bool key = p.Blend != 0;
            // Id 23 reaches the baseline only with no layer texture at all. Its +0x0C is the env map of an
            // emissive that is not drawn, never a diffuse, so it is listed unread: not bound, not decoded,
            // and t0 keeps the register's white default.
            bool envOnly = p.Shader == 23;
            p.Samplers = new[]
            {
                new WmoSamplerBinding { Register = 0, Slot = 0, FileDataID = p.TextureSlots[0], UvChannel = 0,
                                        KeepAlpha = key && !envOnly, Unread = envOnly,
                                        Role = envOnly ? "env map (emissive, not drawn; t0 reads the register's white default)"
                                                       : "provisional diffuse" },
            };
            ApplyEstablishedBlend(p, key);
            p.CullOff = (p.Flags & FlagUnculled) != 0;

            bool blendUnresolved = p.Blend >= 2;
            if (blendUnresolved)
            {
                // The client's factor rows are known, which row a value selects only from older-client
                // documentation (contested from 3 up, U-B7), and the state around them not at all. No Src/Dst
                // is applied: the provisional key stays until U-B2..U-B5 are settled, and the row is logged.
                AddUnresolvedBlendCodes(p, codes);
                notes.Add("blend " + p.Blend + ": depth write (U-B2), draw order (U-B3), output alpha (U-B4) and discard " +
                          "(U-B5) are not established; PROVISIONAL alpha key 128/255 on t0's alpha, depth write on");
                notes.Add(ClientBlendNote(p.Blend));
            }

            // Ids 4, 5, 7, 13 and 23-with-a-layer keep their case arithmetic even with an unresolved blend, so
            // what remains here is id 0/16 held back by its blend state, id 23 without any layer, or an id
            // outside the plan.
            if (p.Shader == 0 || p.Shader == 16)
            {
                // The combiner is established; only the blend state above holds the material back.
                notes.Add("id " + p.Shader + ": the client case is established (diffuse = t0.rgb); only its blend state is not");
                if (p.TextureSlots[0] == 0)
                {
                    codes.Add("U-23b");
                    notes.Add("slot +0x0C is empty: what the client binds to an unused register is unknown (U-23b); drawn white");
                }
            }
            else if (p.Shader == 23)
            {
                // No layer texture at all: every register case 23 weights is unbound, so the whole result
                // is what the exe binds there.
                codes.Add("U-23b");
                notes.Add("id 23 with no layer texture (+0x18, +0x24, +0x28 and +0x2C all empty): every register the " +
                          "client case weights is unbound (U-23b) -- PROVISIONAL baseline drawing the register's white " +
                          "default; its +0x0C env map is neither drawn as a diffuse nor decoded");
            }
            else
            {
                foreach (string c in OutOfPlanCodes(p.Shader))
                    if (!codes.Contains(c)) codes.Add(c);
                notes.Add(OutOfPlanNote(p.Shader));
            }

            uint notApplied = p.Flags & (FlagUnlit | FlagClampS | FlagClampT);
            if (notApplied != 0)
                notes.Add("flag(s) " + FlagNames(notApplied) + " not applied: the material keeps the archived baseline");

            p.Resolution = WmoResolution.Unresolved;
            p.Codes = codes.ToArray();
            p.Notes = notes.ToArray();
        }

        /// <summary>
        /// Codes for an id outside the staged plan. OUT-OF-PLAN is a scope label, not an evidence question:
        /// the client combiner of most of these ids is known, the plan simply does not wire it yet. The
        /// research codes that would still block the id follow it.
        /// </summary>
        static string[] OutOfPlanCodes(uint shader)
        {
            switch (shader)
            {
                case 3: case 11: case 12: case 17: return new[] { OutOfPlan, "U-G1", "U-E2" };   // env coordinate and axes
                case 8: return new[] { OutOfPlan, "U-G1" };                                     // planar coordinate sign
                case 19: return new[] { OutOfPlan, "U-V4" };                                    // absent set-2 default
                default: return shader > 24 ? new[] { OutOfPlan, "U-C1" } : new[] { OutOfPlan };
            }
        }

        /// <summary>The scope label of an id the staged plan does not cover.</summary>
        public const string OutOfPlan = "OUT-OF-PLAN";

        static string OutOfPlanNote(uint shader)
        {
            if (shader == 6)
                return "id 6: established combiner, outside the staged plan -- PROVISIONAL baseline";
            if (shader == 10 || shader == 14)
                return "id " + shader + ": a water/submarine window effect, out of scope -- PROVISIONAL baseline";
            if (shader == 21 || shader == 22)
                return "id " + shader + ": out of scope for this plan (" + (shader == 21 ? "LOD material" : "parallax") +
                       ") -- PROVISIONAL baseline";
            if (shader > 24)
                return "id " + shader + ": no client case is known for it (U-C1) -- PROVISIONAL baseline";
            return "id " + shader + ": client combiner known but outside the staged plan -- PROVISIONAL baseline";
        }

        /// <summary>"0x10 F_SIDN, 0x20 F_WINDOW" for a flag mask.</summary>
        public static string FlagNames(uint mask)
        {
            var parts = new List<string>();
            if ((mask & FlagUnlit) != 0) parts.Add("0x01 F_UNLIT");
            if ((mask & FlagUnfogged) != 0) parts.Add("0x02 F_UNFOGGED");
            if ((mask & FlagUnculled) != 0) parts.Add("0x04 F_UNCULLED");
            if ((mask & FlagExtLight) != 0) parts.Add("0x08 F_EXTLIGHT");
            if ((mask & FlagSidn) != 0) parts.Add("0x10 F_SIDN");
            if ((mask & FlagWindow) != 0) parts.Add("0x20 F_WINDOW");
            if ((mask & FlagClampS) != 0) parts.Add("0x40 F_CLAMP_S");
            if ((mask & FlagClampT) != 0) parts.Add("0x80 F_CLAMP_T");
            if ((mask & Flag100) != 0) parts.Add("0x100");
            uint rest = mask & ~0x1FFu;
            if (rest != 0) parts.Add(string.Format("0x{0:X}", rest));
            return string.Join(", ", parts.ToArray());
        }

        /// <summary>
        /// The Describe text of an env permutation's emissive (ids 5, 7 and 23), "" for any other permutation: the
        /// client's equation; its mask, with the coordinate assumption the mask rests on; the env map and that it is
        /// neither decoded nor drawn; what is known of its coordinate; and the codes that keep it undrawn.
        /// </summary>
        static string DescribeEnvEmissive(WmoMaterialPlan p)
        {
            string emissive, mask, coordinate, codes;
            int envSlot;
            switch (p.Permutation)
            {
                case WmoPermutation.EnvMetal:
                    emissive = EnvMetalEmissive;
                    envSlot = 1;
                    mask = "t0.rgb * t0.a on the MOTV set the diffuse reads, assuming the cb0[5].y override does not replace uvA (U-G1)";
                    coordinate = "VS generator cb2[1].y or the cb0[5].y per-pixel sphere map, not established (U-G1), camera axes U-E2, " +
                                 "address mode U-E4";
                    codes = "U-G1, U-E2, U-E3, U-E4, U-P1";
                    break;
                case WmoPermutation.TwoLayerEnvMetal:
                    emissive = TwoLayerEnvEmissive;
                    envSlot = 2;
                    mask = "c.rgb * c.a on the MOTV sets the diffuse reads, assuming the cb0[5].y override replaces neither uvA nor uvB (U-G1)";
                    coordinate = "VS generator cb2[1].z, not established (U-G1), camera axes U-E2, address mode U-E4";
                    codes = "U-G1, U-E2, U-E3, U-E4, U-P1";
                    break;
                case WmoPermutation.FourLayer:
                    emissive = FourLayerEnvEmissive;
                    envSlot = 0;
                    mask = "mix.rgb * mix.a on the MOTV sets the layers read, assuming the cb0[5].y override replaces neither uvA nor " +
                           "uvB, which layers 1 and 2 read (U-G1)";
                    coordinate = "per-pixel sphere map of the transformed position and its normal (CLIENT equation; that the space " +
                                 "is camera space is inference), its Unity coordinate not established (camera axes U-E2, address " +
                                 "mode U-E4)";
                    codes = "U-E2, U-E3, U-E4, U-P1";
                    break;
                default:
                    return "";
            }
            uint env = p.TextureSlots[envSlot];
            return "emissive " + emissive + " (CLIENT); mask " + mask + "; env map " + SlotName(envSlot) + " " +
                   (env != 0 ? env + ": not decoded, not drawn" : "empty: nothing to decode or draw (U-23b)") +
                   "; env coordinate: " + coordinate + "; NOT drawn (" + codes + ")";
        }

        /// <summary>
        /// The semantic half of the material diagnostic, one line: shader id, blend, flags, every texture
        /// slot, the permutation, each sampler's role, slot, UV channel and whether its alpha is read, the vertex-colour
        /// use, the combiner and -- for ids 5, 7 and 23 -- the emissive that is not drawn, the render state, the
        /// client's blend row for a blend of 2 or above, and the verdict with its reason codes. The builder appends
        /// what only the player knows (decode outcome, the created material read back).
        /// </summary>
        public static string Describe(WmoMaterialPlan p)
        {
            var sb = new System.Text.StringBuilder();
            sb.AppendFormat("material {0}: shader {1} blend {2} flags 0x{3:X8}", p.Index, p.Shader, p.Blend, p.Flags);
            sb.Append(" | textures [");
            bool any = false;
            for (int s = 0; s < p.TextureSlots.Length; s++)
            {
                if (p.TextureSlots[s] == 0) continue;
                sb.Append(any ? ", " : "").Append(SlotNames[s]).Append(' ').Append(p.TextureSlots[s]);
                any = true;
            }
            sb.Append(any ? "]" : "none]");
            sb.Append(" | permutation ").Append((int)p.Permutation).Append(' ').Append(p.PermutationName);
            sb.Append(" | samplers [");
            for (int i = 0; i < p.Samplers.Length; i++)
            {
                WmoSamplerBinding b = p.Samplers[i];
                sb.Append(i > 0 ? "; " : "").Append('t').Append(b.Register);
                if (b.ClientRegister != b.Register) sb.Append(" (client t").Append(b.ClientRegister).Append(')');
                sb.Append(' ').Append(b.Role).Append(" <- ").Append(SlotName(b.Slot)).Append(' ')
                  .Append(b.FileDataID == 0 ? "empty" : b.FileDataID.ToString());
                if (b.Unread)
                    sb.Append(", not bound");
                else
                    sb.AppendFormat(" @ UV{0} (MOTV set {1}), alpha {2}", b.UvChannel, b.UvChannel + 1, b.KeepAlpha ? "read" : "not read");
            }
            sb.Append("] | vertex colour ").Append(p.VertexColour);
            if (p.Permutation == WmoPermutation.FourLayer)
                sb.AppendFormat(" | layer mask ({0},{1},{2},{3})", p.LayerMask[0], p.LayerMask[1], p.LayerMask[2], p.LayerMask[3]);
            if (p.ReadsSet2Alpha)
                sb.Append(" | combiner diffuse = lerp(t1.rgb, t0.rgb, va), case alpha 1");
            else if (p.Permutation == WmoPermutation.Opaque || p.Permutation == WmoPermutation.EnvMetal)
                sb.Append(" | combiner diffuse = t0.rgb, case alpha 1");
            else if (p.Permutation == WmoPermutation.FourLayer)
                sb.Append(" | combiner diffuse = the weighted layer sum mix.rgb, case alpha 1");
            // Said on the combiner itself, so a reader never takes an env id's drawing for the whole case.
            string emissive = DescribeEnvEmissive(p);
            if (emissive.Length > 0)
                sb.Append("; ").Append(emissive);
            sb.AppendFormat(" | blend Src {0} Dst {1} SrcA {2} DstA {3}, ZWrite {4}, alpha test {5}, cull {6}, wrap U {7} V {8}, " +
                            "queue {9} {10}, light bypass {11}",
                p.SrcColor, p.DstColor, p.SrcAlpha, p.DstAlpha, p.ZWrite ? "on" : "off",
                p.AlphaTest ? "clip < " + p.Cutoff.ToString("0.#####", System.Globalization.CultureInfo.InvariantCulture) : "off",
                p.CullOff ? "off" : "back", p.ClampU ? "clamp" : "repeat", p.ClampV ? "clamp" : "repeat",
                p.RenderQueue, p.RenderType, p.LightBypass ? "on" : "off");
            // Beside the realised state, so the two are never read as one: the client's row is logged, not applied.
            string blendNote = ClientBlendNote(p.Blend);
            if (blendNote.Length > 0)
                sb.Append(" | client blend: ").Append(blendNote);
            // The resolution word in capitals so a reader scanning the log finds it; the codes as written.
            sb.Append(" | ").Append(p.ResolutionName.ToUpperInvariant());
            if (p.ProvisionalFallback) sb.Append(" (PROVISIONAL fallback)");
            if (p.Codes.Length > 0) sb.Append(": ").Append(string.Join(",", p.Codes));
            bool firstNote = true;
            foreach (string note in p.Notes)
            {
                if (blendNote.Length > 0 && note == blendNote)
                    continue;   // already printed as the client blend field
                sb.Append(firstNote ? " -- " : "; ").Append(note);
                firstNote = false;
            }
            return sb.ToString();
        }
    }
}
