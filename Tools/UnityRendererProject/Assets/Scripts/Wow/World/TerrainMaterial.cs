// TerrainMaterial.cs
//
// Terrain texture materials of one split-ADT tile (stage 04 design, sections 1 and 3, with the v3 delta:
// the height-mix composite rule and its states): what each MCNK draws, from the parsed root
// (AdtParser.ParseRoot) and tex0 (AdtParser.ParseTex) and the WDT's MPHD flags. Pure C#, no UnityEngine:
// the player turns the arrays into textures and materials; the console runner checks them on synthetic
// and real bytes.
//
// Evidence (stage 04 design, section 0, and the v3 delta, section 1; labels as there):
//   - Textures are MDID FileDataIDs; MCLY (16 bytes) names an MDID index, its flags and its MCAL
//     offset; the layer order is the MCLY order (PROVEN). MHID[i] is the height texture of MDID[i];
//     MTXP[i] = (flags, height scale, height offset, word 3) of MDID[i].
//   - Alpha: 8-bit maps (raw 4096 bytes or RLE), texel (row, col) = row*64 + col, row south, column
//     east, as stored (no 62 -> 63 copy). 8-bit iff MPHD & 0x84 (112 of 112 maps); the layer spacing
//     stays the authority and a disagreement is a state. 4-bit maps: storage proven, the draw-time
//     expansion is NOT established (U19) -> unsupported.
//   - The composite rule is the HEIGHT MIX (PROVEN for the game's baked map texture on MPHD 0x80 maps:
//     six golden tiles and Retail 30_49 with its height textures; the cutoff part also on 0x80-clear
//     8-bit Retail maps 1923857, 822688 and 3073642; INFERRED for the client's draw-time shader; MoP
//     INFERRED: no map texture to compare with, its storage was unavailable; plain weights, sequential
//     layering, linear blending, no MCCV and, on 0x80-clear maps, the height textures all fit worse):
//       a_k (k >= 1) = alpha 0..1; w_0 = 1 - sum a_k; w_k = a_k
//       H_k = 1 when MPHD & 0x80 is clear (MHID and the MTXP height values are ignored), else
//       H_k = h_k * heightScale_k + heightOffset_k, h_k = the height texture's ALPHA at the layer's UV
//             with the layer's repeats (0 when the layer's MHID entry is 0); (scale, offset) = MTXP
//             words 1, 2 of the layer's MDID entry, (0.0, 1.0) without MTXP
//       p_k = max(0, w_k) * H_k; m = max p_k; p_k *= 1 - clamp(m - p_k, 0, 1); S = sum p_k
//       colour = sum (p_k / S) * texture_k.rgb, or sum max(0, w_k) * texture_k.rgb when S <= 0
//       then x MCCV (the production view saturates to 0..1).
//     The mix is defined for H_k >= 0 and finite only: a layer whose stored MTXP height values let H
//     fall below 0 or overflow float32 makes its chunk LayerEffectUnresolved (states below), so on a
//     drawn chunk S >= 0 and only S == 0 (every p_k = 0, e.g. H_k = 0 exactly) reaches the S <= 0
//     fallback -- the contract's fallback, itself unverified.
//     Stored (authored) values throughout; the texture's alpha channel is not used for diffuse layers.
//   - Alpha placement: texel i is centred at (i + 0.5) / 64 of the chunk (AlphaTexelCentre): PROVEN for
//     the game's map generator, INFERRED for the client.
//   - UV: 8 repeats per chunk (one per cell), texel (0, 0) at the chunk's north-west corner, u east,
//     v south; MTXP flags bits 4-7 = s: one texture repeat per 2^s cells with the texture coordinate
//     continuous across chunks, i.e. r = 8 / 2^s repeats per chunk as a float (8, 4, 2, 1, 0.5) and chunk
//     (ix, iy) = (n % 16, n / 16) adds the phase (frac(ix * r), frac(iy * r)) to the layer's texture
//     coordinate uv * r / 8 (RepeatsPerChunk, PhaseU, PhaseV). s 0..3: PROVEN on the stage's golden tiles; r is
//     whole there, so the phase is 0 and chunk-local UV is the same rule. s = 4 (r = 0.5, phase 0 or 0.5):
//     PROVEN for the game's baked map texture on Retail map 2923 (5 tiles, 897 chunks; chunk-local UV, 1
//     and 8 repeats all fit worse), INFERRED for the client's draw time and for other maps (2923 is the
//     only map with it in the audit data). Tile- and map-continuous UV agree for s <= 7 (the period
//     divides a tile). s 5..15 were never observed: unresolved. A height texture is sampled at its
//     layer's coordinate (the same repeats and phase).
//   - MCCV: 145 x (B, G, R, A); a colour multiplies by value / 127 (127 = neutral); applied only when the
//     chunk flag 0x40 and the MCCV chunk are both present, and only to chunks that get an alpha slice
//     (never to neutral ones, NoLayers included). The divisor 127 vs 128 is not separable (section 0-F).
//   - Layer effects: MCLY 0x40 (animation, with the rotation bits 0x7 and speed bits 0x38) and 0x80
//     (overbright) chunks are drawn with the verified STATIC composite (what the game's map texture shows,
//     ~1.2 RMS); the animation and overbright are NOT applied (units and factor unknown). MCLY 0x400
//     (cube map), every never-observed MCLY bit, MTXP flag bits other than the scale nibble (0x1 = the
//     cube-map texture), a non-zero MTXP word 3, a scale nibble above 4 and (MPHD 0x80 maps) height values
//     outside the height mix's domain are unresolved. MCSH is counted ("shadow map ignored").
//   - Layer count: up to 8 layers are drawn, all by the one rule (PROVEN for the game's baked map texture:
//     the chunks of 6..8 layers of Retail map 2923 fit it as well as the others; 8 is the most observed;
//     INFERRED for the client's draw time). AdtParser rejects more than 8 MCLY entries (Malformed).
//
// Chunk states, precedence top to bottom (the first that applies wins; v3 delta, section 2):
//   Malformed             a parser-Malformed tex0 or root MCNK (the root's has no geometry); for a chunk with
//                         layers: a tile-level Malformed MDID, MHID or MTXP table (repeated, or not whole
//                         entries), an MHID entry count unlike MDID's (when MHID is present), MTXP entries
//                         != MDID entries; a texture index outside MDID, an MTXP height scale or offset
//                         that is not a finite number, an alpha sum > 255 at any texel, MPHD 0x84
//                         disagreeing with a layer's stored bit depth. An unreadable MHID is never read as
//                         "no height textures".
//   AlphaUnsupported      any 4-bit layer; MCNK flag 0x8000 clear
//   LayerCountUnsupported more than 8 layers (8 is the most observed; only reached by a layer list that
//                         AdtParser did not produce, since it makes more than 8 MCLY entries Malformed)
//   LayerEffectUnresolved a layer with an MCLY bit outside 0x100/0x200/0x40/0x80/0x7/0x38 (0x400 cube map,
//                         never-observed bits), MTXP flag bits outside 0xF0, MTXP word 3 != 0, an MTXP
//                         texture-scale nibble above 4 (the file is not broken; nibbles 5..15 were never
//                         observed, 0..4 are verified), or -- on an MPHD 0x80 map only -- MTXP height values outside
//                         the domain of the height mix: offset o < 0, or, for a layer whose MHID entry is
//                         not 0, s + o < 0 or s + o not finite in float32 (s = height scale; H = h*s + o
//                         with h in 0..1 then stays in 0..max(o, s + o) and finite)
//   HeightTextureMissing  MPHD 0x80 and a layer's MHID texture failed (ApplyHeightTextureFailures)
//   NoLayers              no layer
//   LayerEffectStatic     a layer with MCLY 0x40, 0x80 or rotation/speed bits: drawn with the static composite
//   MissingTexture        a layer's texture id is 0 or its texture failed (ApplyTextureFailures)
//   Textured              everything verified
// Textured, MissingTexture and LayerEffectStatic chunks are DRAWABLE (textured views). LayerEffectUnresolved
// and HeightTextureMissing chunks carry layers and an alpha slice too, for a labelled diagnostic view that
// draws them with plain weights (TerrainBlendRule.PlainWeights); every other chunk is drawn neutral.
//
// Alpha slices: 64 x 64 texels x 4 bytes, texel (row, col) at 4 * (row*64 + col), rows as stored (row 0 =
// north), never flipped. Alpha layer k (k = 1..7) is in the chunk's slice (k - 1) / 4, channel (k - 1) % 4:
// the first slice holds layers 1..4 (R, G, B, A), a SECOND slice -- only for a chunk of more than 5 layers --
// holds layers 5..7 (R, G, B; A = 0); unused channels 0. A chunk's Alpha holds its one or two slices back to
// back. Slices are numbered at Build: the drawable chunks first (in chunk order; a chunk's second slice right
// after its first), then the LayerEffectUnresolved ones the same way; texture failures never renumber them
// (a chunk that becomes HeightTextureMissing keeps its slices).
//
// TerrainCompositor: the composite of one chunk at chunk positions (u, v) in cell units (u east, v south,
// 0..8 -- the mesh's UV0), with the shader's math in VALIDATION sampling: layer and height texels by
// integer index at the layer's coordinate (repeats and phase), alpha by a manual bilinear at the
// AlphaTexelCentre alignment with clamp, MCCV interpolated over the cell's four-triangle fan. float
// arithmetic throughout, float output.
//
// Thread safety: no mutable static state; Build and the compositor may run on any thread.

using System;
using System.Collections.Generic;
using System.Text;

namespace Wmv.Wow.World
{
    /// <summary>A chunk's material state (v3 delta, section 2). The order is the precedence: when several
    /// apply, the first wins.</summary>
    public enum TerrainMaterialState
    {
        Malformed = 0,
        AlphaUnsupported,
        LayerCountUnsupported,
        LayerEffectUnresolved,
        HeightTextureMissing,
        NoLayers,
        LayerEffectStatic,
        MissingTexture,
        Textured,
    }

    /// <summary>The tile's material as a whole (design section 2, terrainTileLoaded.material.state).</summary>
    public enum TerrainTileMaterialState
    {
        /// <summary>All 256 chunks Textured.</summary>
        Textured,
        /// <summary>Some chunks drawable, not all of them Textured.</summary>
        Partial,
        /// <summary>No tex0 (any failure state): no material at all; the caller gives the reason.</summary>
        Unavailable,
        /// <summary>A tex0 was read, but no chunk is drawable.</summary>
        Unsupported,
    }

    /// <summary>Which weights the compositor uses.</summary>
    public enum TerrainBlendRule
    {
        /// <summary>The verified rule (file header): the height mix. Production.</summary>
        HeightMix,
        /// <summary>w_0 = 1 - sum a_k, w_k = a_k, no cutoff, no heights: a labelled comparison/diagnostic only.</summary>
        PlainWeights,
    }

    /// <summary>One MCLY layer as the material uses it.</summary>
    public sealed class TerrainMaterialLayer
    {
        public int Layer;                       // position in MCLY (0 = base layer)
        public int MdidIndex;                   // MCLY texture index
        public bool IndexInRange;               // MdidIndex < MDID entries (otherwise the chunk is Malformed)
        public uint TextureFileDataID;          // MDID[MdidIndex]; 0 = no texture id
        public uint Flags;                      // MCLY flags as stored
        public bool HasMtxp;                    // the tile has a well-formed MTXP
        public uint MtxpFlags;                  // MTXP word 0 (0 without MTXP)
        public int ScaleExponent;               // (MtxpFlags >> 4) & 0xF
        /// <summary>Texture repeats per chunk: 8 / 2^ScaleExponent (8, 4, 2, 1, 0.5), a float; 8 without MTXP; 0 when
        /// the exponent is above TerrainMaterialBuilder.MaxScaleExponent (unresolved: drawn as the checker).</summary>
        public float RepeatsPerChunk;
        /// <summary>This chunk's UV phase of the layer, in texture repeats (added to s = uv * RepeatsPerChunk / 8):
        /// (frac(ix * RepeatsPerChunk), frac(iy * RepeatsPerChunk)), ix = chunk index % 16 (east), iy = chunk index
        /// / 16 (south). 0 for whole repeats (nibbles 0..3) and for unresolved ones; 0 or 0.5 for nibble 4.</summary>
        public float PhaseU, PhaseV;
        public float HeightScale;               // MTXP word 1 (0.0 without MTXP); used only on MPHD 0x80 maps
        public float HeightOffset = 1f;         // MTXP word 2 (1.0 without MTXP); used only on MPHD 0x80 maps
        public uint MtxpWord3;                  // MTXP word 3 (0 in every audited entry; otherwise unresolved)
        public uint HeightTextureFileDataID;    // MHID[MdidIndex] (0 = no height texture, or no MHID chunk)
        public AlphaStorage? Storage;           // null for the base layer
        public bool Missing;                    // drawn as the missing-texture checker (id 0, or its texture failed)
        public bool HeightMissing;              // its height texture failed (the chunk is HeightTextureMissing)
    }

    /// <summary>One MCNK's material.</summary>
    public sealed class TerrainChunkMaterial
    {
        public const int AlphaSliceBytes = 64 * 64 * 4;
        /// <summary>Alpha layers per slice (R, G, B, A).</summary>
        public const int LayersPerSlice = 4;

        public int Index;
        public TerrainMaterialState State;
        /// <summary>Why the chunk is not Textured (null when it is).</summary>
        public string Reason;
        /// <summary>Every MCLY layer (empty when the chunk has none or its MCLY is unreadable).</summary>
        public TerrainMaterialLayer[] Layers = new TerrainMaterialLayer[0];
        /// <summary>The chunk's alpha slices back to back, AlphaSliceCount x AlphaSliceBytes (see the file header:
        /// layers 1..4 in the first slice, layers 5..7 in a second one for a chunk of more than 5 layers), or null
        /// for chunks that are neither drawable nor plain-diagnostic at Build.</summary>
        public byte[] Alpha;
        /// <summary>Index of the chunk's first slice (alpha layers 1..4) in the tile's alpha array, -1 without a slice.</summary>
        public int AlphaSlice = -1;
        /// <summary>Index of the chunk's second slice (alpha layers 5..7) in the tile's alpha array, -1 when the chunk
        /// has none (5 layers or fewer, or no slice at all).</summary>
        public int AlphaSlice2 = -1;

        /// <summary>The number of slices in Alpha: 0, 1, or 2 (more than 5 layers).</summary>
        public int AlphaSliceCount { get { return Alpha == null ? 0 : Alpha.Length / AlphaSliceBytes; } }

        /// <summary>The slice (0 = the chunk's first, 1 = its second) and channel (0..3 = R, G, B, A) of alpha layer
        /// `layer` (1..7).</summary>
        public static void AlphaChannelOf(int layer, out int slice, out int channel)
        {
            if (layer < 1 || layer > TerrainMaterialBuilder.MaxDrawnLayers - 1)
                throw new ArgumentOutOfRangeException("layer", "alpha layers are 1.." + (TerrainMaterialBuilder.MaxDrawnLayers - 1));
            slice = (layer - 1) / LayersPerSlice;
            channel = (layer - 1) % LayersPerSlice;
        }
        /// <summary>145 x (R, G, B) = MCCV bytes (2, 1, 0) / 127 in MCVT vertex order, or null (neutral 1).</summary>
        public float[] VertexColors;
        /// <summary>The root's 64-bit hole mask (bit row*8 + col), 0 when the chunk has no high-resolution mask.</summary>
        public ulong HoleMask;
        /// <summary>Note: tex0 MCSH present (counted; the shadow map is not applied).</summary>
        public bool ShadowMapIgnored;
        /// <summary>Note: MCCV present without chunk flag 0x40, or the flag without MCCV (MCCV not applied).</summary>
        public bool VertexColorMismatch;
        /// <summary>The tile's MPHD has 0x80: the layers' height textures and MTXP height values enter the
        /// height mix (H_k); otherwise H_k = 1.</summary>
        public bool UseHeightTextures;

        internal TerrainMaterialState BaseState;
        internal string BaseReason;

        /// <summary>Textured, MissingTexture or LayerEffectStatic: drawn textured.</summary>
        public bool Drawable
        {
            get
            {
                return State == TerrainMaterialState.Textured || State == TerrainMaterialState.MissingTexture ||
                       State == TerrainMaterialState.LayerEffectStatic;
            }
        }

        /// <summary>LayerEffectUnresolved or HeightTextureMissing: neutral, but with layers and an alpha slice
        /// for the labelled plain-weights diagnostic view.</summary>
        public bool PlainDiagnostic
        {
            get { return State == TerrainMaterialState.LayerEffectUnresolved || State == TerrainMaterialState.HeightTextureMissing; }
        }

        /// <summary>The height mix of this chunk reads at least one height texture (MPHD 0x80 and a layer
        /// whose MHID entry is not 0).</summary>
        public bool NeedsHeightTextures
        {
            get
            {
                if (!UseHeightTextures)
                    return false;
                foreach (TerrainMaterialLayer l in Layers)
                    if (l.HeightTextureFileDataID != 0)
                        return true;
                return false;
            }
        }
    }

    /// <summary>A tile's materials: 256 chunks, counters and the textures the drawable chunks need.</summary>
    public sealed class TerrainTileMaterial
    {
        public const int StateCount = 9;

        public TerrainTileMaterialState State;
        public string Reason;
        public uint MphdFlags;
        /// <summary>8 when MPHD has 0x4 or 0x80, else 4: the alpha depth the WDT announces.</summary>
        public int AlphaBitDepth;
        /// <summary>MPHD & 0x80: height textures (MHID) and the MTXP height values enter the height mix.</summary>
        public bool UseHeightTextures;
        /// <summary>256 chunks in file order; empty when State is Unavailable.</summary>
        public TerrainChunkMaterial[] Chunks = new TerrainChunkMaterial[0];
        /// <summary>Diffuse texture FileDataIDs (MDID) of the chunks DRAWABLE at Build, first use (chunk order,
        /// then layer order), without 0.</summary>
        public uint[] TextureIds = new uint[0];
        /// <summary>Height texture FileDataIDs (MHID) of the chunks drawable at Build, first use, without 0:
        /// empty unless UseHeightTextures. Kept apart from TextureIds; an id in both lists (never observed) is
        /// one file, to be read once.</summary>
        public uint[] HeightTextureIds = new uint[0];
        /// <summary>Diffuse texture FileDataIDs only the LayerEffectUnresolved chunks use (first use, without 0), from
        /// their layers whose repeats are resolved (RepeatsPerChunk > 0): a layer of unresolved repeats is drawn as
        /// the checker, so its texture is never bound.</summary>
        public uint[] PlainOnlyTextureIds = new uint[0];
        /// <summary>Layers of the drawable chunks, the ones naming texture id 0, and the ones naming an id
        /// already listed (LayerReferences = TextureIds.Length + ZeroIdReferences + DuplicateReferences).</summary>
        public int LayerReferences, ZeroIdReferences, DuplicateReferences;
        /// <summary>Layers of the drawable chunks naming a height texture (UseHeightTextures only), and the ones
        /// naming a height id already listed (HeightLayerReferences = HeightTextureIds.Length + HeightDuplicateReferences).</summary>
        public int HeightLayerReferences, HeightDuplicateReferences;
        /// <summary>Alpha slices of the drawable chunks (numbered first) and of the LayerEffectUnresolved chunks,
        /// as numbered at Build: one per chunk, two for a chunk of more than 5 layers.</summary>
        public int DrawableSlices, PlainSlices;
        /// <summary>Alpha layers by storage, over every chunk with decoded alpha.</summary>
        public int Raw8Layers, RleLayers, Raw4Layers;
        /// <summary>Notes: chunks with MCSH, with MCCV/flag 0x40 disagreeing, with vertex colours applied.</summary>
        public int ShadowMapsIgnored, VertexColorMismatches, VertexColorChunks;
        /// <summary>Tile-level findings that are not chunk states (e.g. height textures named on a map
        /// whose MPHD has no 0x80, which are ignored).</summary>
        public List<string> Notes = new List<string>();
        /// <summary>The diffuse ids ApplyTextureFailures was last given (distinct, in the order given).</summary>
        public uint[] FailedTextureIds = new uint[0];
        /// <summary>The height ids ApplyHeightTextureFailures was last given (distinct, in the order given).</summary>
        public uint[] FailedHeightTextureIds = new uint[0];

        readonly int[] counts = new int[StateCount];

        public int Count(TerrainMaterialState state) { return counts[(int)state]; }
        public int DrawableChunks
        {
            get { return Count(TerrainMaterialState.Textured) + Count(TerrainMaterialState.MissingTexture) + Count(TerrainMaterialState.LayerEffectStatic); }
        }
        /// <summary>"8-bit" or "4-bit" (the WDT's announcement, MPHD 0x84).</summary>
        public string AlphaFormat { get { return AlphaBitDepth == 8 ? "8-bit" : "4-bit"; } }

        /// <summary>A tile whose tex0 is not available (any state): no chunk material; `reason` says why.</summary>
        public static TerrainTileMaterial Unavailable(string reason, uint mphdFlags)
        {
            return new TerrainTileMaterial
            {
                State = TerrainTileMaterialState.Unavailable,
                Reason = reason ?? "no tex0",
                MphdFlags = mphdFlags,
                AlphaBitDepth = (mphdFlags & 0x84) != 0 ? 8 : 4,
                UseHeightTextures = (mphdFlags & TerrainMaterialBuilder.HeightTextureMphdFlag) != 0,
            };
        }

        /// <summary>The section 2 counters and notes as (name, value), names as in the report.</summary>
        public List<KeyValuePair<string, int>> CounterPairs()
        {
            var list = new List<KeyValuePair<string, int>>();
            for (int s = 0; s < counts.Length; s++)
                list.Add(new KeyValuePair<string, int>(StateName((TerrainMaterialState)s), counts[s]));
            list.Add(new KeyValuePair<string, int>("shadowMapIgnored", ShadowMapsIgnored));
            list.Add(new KeyValuePair<string, int>("vertexColorMismatch", VertexColorMismatches));
            return list;
        }

        public static string StateName(TerrainMaterialState s)
        {
            switch (s)
            {
                case TerrainMaterialState.Malformed: return "malformed";
                case TerrainMaterialState.AlphaUnsupported: return "alphaUnsupported";
                case TerrainMaterialState.LayerCountUnsupported: return "layerCountUnsupported";
                case TerrainMaterialState.LayerEffectUnresolved: return "layerEffectUnresolved";
                case TerrainMaterialState.HeightTextureMissing: return "heightTextureMissing";
                case TerrainMaterialState.NoLayers: return "noLayers";
                case TerrainMaterialState.LayerEffectStatic: return "layerEffectStatic";
                case TerrainMaterialState.MissingTexture: return "missingTexture";
                default: return "textured";
            }
        }

        public static string TileStateName(TerrainTileMaterialState s)
        {
            switch (s)
            {
                case TerrainTileMaterialState.Textured: return "textured";
                case TerrainTileMaterialState.Partial: return "partial";
                case TerrainTileMaterialState.Unavailable: return "unavailable";
                default: return "unsupported";
            }
        }

        /// <summary>
        /// Mark the drawable chunks that use a DIFFUSE texture which could not be had (any per-texture
        /// failure state) as MissingTexture (a LayerEffectStatic chunk keeps its state, which takes
        /// precedence); their failed layers draw as the missing-texture checker (the flag is set on the
        /// plain-diagnostic chunks' layers too). Each call starts again from the states Build gave, keeping
        /// the height failures last given to ApplyHeightTextureFailures, so it may be called again with a
        /// longer list; an empty list (or null) clears the diffuse failures. Id 0 in the list is ignored (id 0
        /// is always missing). Slices and texture lists never change.
        /// </summary>
        public void ApplyTextureFailures(IEnumerable<uint> failed)
        {
            FailedTextureIds = Distinct(failed);
            Reapply();
        }

        /// <summary>
        /// Mark the drawable chunks of an MPHD 0x80 map that read a HEIGHT texture which could not be had (any
        /// per-texture failure state) as HeightTextureMissing (drawn neutral: the height mix needs every
        /// layer's height texture); the layers whose height texture failed get HeightMissing. Same rules as
        /// ApplyTextureFailures (a new list replaces the old one, the diffuse failures are kept, null or empty
        /// clears). On a map without 0x80 height textures are never read and the list changes no state.
        /// </summary>
        public void ApplyHeightTextureFailures(IEnumerable<uint> failed)
        {
            FailedHeightTextureIds = Distinct(failed);
            Reapply();
        }

        static uint[] Distinct(IEnumerable<uint> ids)
        {
            var set = new HashSet<uint>();
            var order = new List<uint>();
            if (ids != null)
                foreach (uint id in ids)
                    if (id != 0 && set.Add(id))
                        order.Add(id);
            return order.ToArray();
        }

        void Reapply()
        {
            var diffuse = new HashSet<uint>(FailedTextureIds);
            var height = new HashSet<uint>(FailedHeightTextureIds);
            foreach (TerrainChunkMaterial c in Chunks)
            {
                c.State = c.BaseState;
                c.Reason = c.BaseReason;
                foreach (TerrainMaterialLayer l in c.Layers)
                {
                    l.Missing = l.IndexInRange && l.TextureFileDataID == 0;
                    l.HeightMissing = false;
                }
                if (c.Alpha == null)
                    continue;           // neutral at Build: no layer is drawn, nothing to mark
                var failedHere = new List<uint>();
                var heightHere = new List<uint>();
                foreach (TerrainMaterialLayer l in c.Layers)
                {
                    if (diffuse.Contains(l.TextureFileDataID))
                    {
                        l.Missing = true;
                        if (!failedHere.Contains(l.TextureFileDataID))
                            failedHere.Add(l.TextureFileDataID);
                    }
                    if (c.UseHeightTextures && height.Contains(l.HeightTextureFileDataID))
                    {
                        l.HeightMissing = true;
                        if (!heightHere.Contains(l.HeightTextureFileDataID))
                            heightHere.Add(l.HeightTextureFileDataID);
                    }
                }
                if (!c.Drawable)
                    continue;
                string diffuseText = failedHere.Count > 0
                    ? "texture(s) " + string.Join(", ", failedHere) + " failed; the layer(s) draw as the missing-texture checker" : null;
                if (heightHere.Count > 0)
                {
                    c.State = TerrainMaterialState.HeightTextureMissing;
                    c.Reason = "height texture(s) " + string.Join(", ", heightHere) +
                               " failed: on an MPHD 0x80 map the height mix needs every layer's height texture (MHID); drawn neutral" +
                               (diffuseText != null ? "; " + diffuseText : "");
                }
                else if (diffuseText != null)
                {
                    if (c.BaseState == TerrainMaterialState.Textured)
                    {
                        c.State = TerrainMaterialState.MissingTexture;
                        c.Reason = diffuseText;
                    }
                    else
                        c.Reason = c.BaseReason + "; " + diffuseText;
                }
            }
            Recount();
        }

        /// <summary>The tile's alpha array: the drawable chunks' slices (and, when asked, the
        /// LayerEffectUnresolved chunks' after them), AlphaSliceBytes each, in slice order: each chunk's first
        /// slice at AlphaSlice and, when it has one, its second at AlphaSlice2.</summary>
        public byte[] BuildAlphaArray(bool includePlainDiagnostic)
        {
            int slices = DrawableSlices + (includePlainDiagnostic ? PlainSlices : 0);
            var bytes = new byte[slices * TerrainChunkMaterial.AlphaSliceBytes];
            foreach (TerrainChunkMaterial c in Chunks)
            {
                if (c.Alpha == null)
                    continue;
                int[] at = { c.AlphaSlice, c.AlphaSlice2 };
                for (int s = 0; s < c.AlphaSliceCount && s < at.Length; s++)
                    if (at[s] >= 0 && at[s] < slices)
                        Buffer.BlockCopy(c.Alpha, s * TerrainChunkMaterial.AlphaSliceBytes, bytes, at[s] * TerrainChunkMaterial.AlphaSliceBytes,
                                         TerrainChunkMaterial.AlphaSliceBytes);
            }
            return bytes;
        }

        internal void Recount()
        {
            Array.Clear(counts, 0, counts.Length);
            foreach (TerrainChunkMaterial c in Chunks)
                counts[(int)c.State]++;
            if (State == TerrainTileMaterialState.Unavailable)
                return;
            int drawable = DrawableChunks;
            if (Count(TerrainMaterialState.Textured) == Chunks.Length)
            {
                State = TerrainTileMaterialState.Textured;
                Reason = null;
                return;
            }
            var sb = new StringBuilder();
            for (int s = 0; s < counts.Length; s++)
                if (counts[s] > 0 && s != (int)TerrainMaterialState.Textured)
                    sb.Append(sb.Length > 0 ? ", " : "").Append(StateName((TerrainMaterialState)s)).Append(' ').Append(counts[s]);
            if (drawable == 0)
            {
                State = TerrainTileMaterialState.Unsupported;
                Reason = "no chunk can be drawn textured: " + sb;
            }
            else
            {
                State = TerrainTileMaterialState.Partial;
                Reason = Count(TerrainMaterialState.Textured) + " of " + Chunks.Length + " chunks textured; " + sb;
            }
        }
    }

    public static class TerrainMaterialBuilder
    {
        /// <summary>The most layers a chunk draws (the base layer and up to 7 alpha layers in two slices): 8 is the
        /// most observed (Retail map 2923); a chunk with more is LayerCountUnsupported.</summary>
        public const int MaxDrawnLayers = 8;
        public const int RepeatsPerChunkDefault = 8;
        public const int MtxpEntryBytes = 16;
        /// <summary>The MCLY flag bits of the alpha storage: 0x100 (has an alpha map) and 0x200 (RLE).</summary>
        public const uint KnownLayerFlags = 0x100 | 0x200;
        /// <summary>The MCLY effect bits drawn with the static composite (LayerEffectStatic): rotation 0x7,
        /// speed 0x38, animation 0x40, overbright 0x80. The effects themselves are not applied.</summary>
        public const uint StaticEffectLayerFlags = 0x7 | 0x38 | 0x40 | 0x80;
        /// <summary>Every MCLY bit with a drawing rule; any other (0x400 cube map, never-observed bits) makes the
        /// chunk LayerEffectUnresolved.</summary>
        public const uint DrawnLayerFlags = KnownLayerFlags | StaticEffectLayerFlags;
        /// <summary>The MTXP flag bits with a drawing rule: bits 4-7, the texture-scale exponent. 0x1 is the
        /// cube-map texture flag (drawing it as a plain diffuse texture is not established); the other bits
        /// were never observed. Any of them makes the chunk LayerEffectUnresolved.</summary>
        public const uint KnownMtxpFlags = 0xF0;
        /// <summary>The largest MTXP texture-scale nibble with a verified rule (8 / 2^s repeats per chunk with the
        /// UV continuous across chunks, s = 0..4; see the file header). Above it (never observed) the chunk is
        /// LayerEffectUnresolved.</summary>
        public const int MaxScaleExponent = 4;
        /// <summary>MPHD flag that puts the height textures (MHID) and the MTXP height values into the height mix.</summary>
        public const uint HeightTextureMphdFlag = 0x80;
        /// <summary>MCNK flag that says the alpha maps are stored 64x64 as drawn (set on every chunk ever read).</summary>
        public const uint DoNotFixAlphaFlag = 0x8000;
        public const uint VertexColorFlag = 0x40;

        /// <summary>
        /// The INFERRED client alignment of alpha texel i (0..63) along a chunk side, as a fraction of the
        /// side: (i + 0.5) / 64 -- texel centres, the first half a texel in from the edge. PROVEN for the
        /// game's map generator (it beats +-0.25/0.5-texel shifts and the i/63 layout on 252-256 of 256
        /// chunks of 4 tiles); the alternative a client could use is i / 63 (63 intervals edge to edge).
        /// </summary>
        public static double AlphaTexelCentre(int i) { return (i + 0.5) / 64.0; }

        /// <summary>The continuous alpha texel coordinate of chunk fraction t (0..1) at that alignment, the
        /// inverse of AlphaTexelCentre: t * 64 - 0.5 (texel i at exactly i). The compositor's bilinear uses it.</summary>
        public static float AlphaTexelCoordinate(float t) { return t * 64f - 0.5f; }

        /// <summary>Texture repeats per chunk of MTXP texture-scale nibble s: 8 / 2^s for s = 0..MaxScaleExponent
        /// (8, 4, 2, 1, 0.5; exact in float), 0 above it (unresolved).</summary>
        public static float RepeatsForScaleExponent(int s)
        {
            return s >= 0 && s <= MaxScaleExponent ? RepeatsPerChunkDefault / (float)(1 << s) : 0f;
        }

        /// <summary>The UV phase, in texture repeats, of the chunk at index `i` along one axis (ix = n % 16 for u,
        /// iy = n / 16 for v) for a layer of `repeats` per chunk: frac(i * repeats). 0 for whole repeats and for
        /// unresolved ones (0), 0 or 0.5 for 0.5 repeats; exact in float for every resolved nibble.</summary>
        public static float RepeatPhase(int i, float repeats)
        {
            if (repeats <= 0f)
                return 0f;
            float p = i * repeats;
            return p - (float)Math.Floor(p);
        }

        /// <summary>UV0 of MCVT vertex k in cell units: outer (r, c) -> (c, r), inner (r, c) -> (c + 0.5, r + 0.5);
        /// u runs east (columns), v south (rows).</summary>
        public static void VertexUv(int k, out float u, out float v)
        {
            if (k < 0 || k >= TerrainVertexLayout.VertexCount)
                throw new ArgumentOutOfRangeException("k", "MCVT vertex index must be 0..144");
            int r = k / 17, c = k % 17;
            if (c < 9) { u = c; v = r; }
            else { u = c - 9 + 0.5f; v = r + 0.5f; }
        }

        /// <summary>
        /// The materials of one tile. `root` and `tex` are the parsed root and tex0 of the same tile (both
        /// required: a tile without a tex0 is TerrainTileMaterial.Unavailable); `mphdFlags` is the WDT's
        /// MPHD word 0.
        /// </summary>
        public static TerrainTileMaterial Build(AdtRoot root, AdtTex tex, uint mphdFlags)
        {
            if (root == null) throw new ArgumentNullException("root");
            if (tex == null) throw new ArgumentNullException("tex");
            if (root.Mcnk == null || root.Mcnk.Length != AdtParser.ChunksPerTile || tex.Mcnk == null || tex.Mcnk.Length != AdtParser.ChunksPerTile)
                throw new ArgumentException("the root and the tex0 must each have 256 MCNKs");

            var t = new TerrainTileMaterial
            {
                MphdFlags = mphdFlags,
                AlphaBitDepth = (mphdFlags & 0x84) != 0 ? 8 : 4,
                UseHeightTextures = (mphdFlags & HeightTextureMphdFlag) != 0,
                Chunks = new TerrainChunkMaterial[AdtParser.ChunksPerTile],
            };
            uint[] mdid = tex.DiffuseTextureIds ?? new uint[0];
            uint[] mhid = tex.HeightTextureIds ?? new uint[0];
            // Tile-level table problems (the parser's own findings first): they make every chunk with layers
            // Malformed, so an unreadable or ambiguous table is never read as "no height textures".
            string tableProblem = TableProblem(tex.Issues);
            if (tableProblem == null && HasTopChunk(tex, "MHID") && mhid.Length != mdid.Length)
                tableProblem = string.Format("MHID has {0} entries and MDID {1} (equal in every audited tile): the layers' height textures are not known",
                                             mhid.Length, mdid.Length);
            string mtxpProblem = null;
            uint[] mtxpFlags = null, mtxpWord3 = null;
            float[] mtxpScale = null, mtxpOffset = null;
            if (tex.MtxpRaw != null)
            {
                if (tex.MtxpRaw.Length % MtxpEntryBytes != 0 || tex.MtxpRaw.Length / MtxpEntryBytes != mdid.Length)
                    mtxpProblem = string.Format("MTXP has {0} byte(s), not {1} entries of 16 bytes (one per MDID entry)", tex.MtxpRaw.Length, mdid.Length);
                else
                {
                    mtxpFlags = new uint[mdid.Length];
                    mtxpScale = new float[mdid.Length];
                    mtxpOffset = new float[mdid.Length];
                    mtxpWord3 = new uint[mdid.Length];
                    for (int i = 0; i < mdid.Length; i++)
                    {
                        mtxpFlags[i] = BitConverter.ToUInt32(tex.MtxpRaw, MtxpEntryBytes * i);
                        mtxpScale[i] = BitConverter.ToSingle(tex.MtxpRaw, MtxpEntryBytes * i + 4);
                        mtxpOffset[i] = BitConverter.ToSingle(tex.MtxpRaw, MtxpEntryBytes * i + 8);
                        mtxpWord3[i] = BitConverter.ToUInt32(tex.MtxpRaw, MtxpEntryBytes * i + 12);
                    }
                }
            }
            if (!t.UseHeightTextures && tableProblem == null)
            {
                int named = 0;
                foreach (uint id in mhid)
                    if (id != 0)
                        named++;
                if (named > 0)
                    t.Notes.Add(string.Format("MHID names {0} height texture(s), ignored: MPHD 0x{1:X} has no 0x80 (H = 1 for every layer)", named, mphdFlags));
            }

            var tables = new MtxpTables { Flags = mtxpFlags, Scale = mtxpScale, Offset = mtxpOffset, Word3 = mtxpWord3 };
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                TerrainChunkMaterial c = BuildChunk(n, root.Mcnk[n], tex.Mcnk[n], mdid, mhid, tables, tableProblem, mtxpProblem,
                                                    mphdFlags, root.Issues, tex.Issues, t);
                c.UseHeightTextures = t.UseHeightTextures;
                c.BaseState = c.State;
                c.BaseReason = c.Reason;
                if (c.VertexColors != null)
                    t.VertexColorChunks++;
                t.Chunks[n] = c;
            }

            // Slices: drawable chunks first, then the plain-diagnostic ones (a chunk's second slice right after its
            // first); texture lists in first use.
            int slice = 0;
            var listed = new HashSet<uint>();
            var ids = new List<uint>();
            var heightListed = new HashSet<uint>();
            var heightIds = new List<uint>();
            foreach (TerrainChunkMaterial c in t.Chunks)
            {
                if (!c.Drawable)
                    continue;
                c.AlphaSlice = slice++;
                if (c.AlphaSliceCount > 1)
                    c.AlphaSlice2 = slice++;
                foreach (TerrainMaterialLayer l in c.Layers)
                {
                    t.LayerReferences++;
                    if (l.TextureFileDataID == 0)
                        t.ZeroIdReferences++;
                    else if (listed.Add(l.TextureFileDataID))
                        ids.Add(l.TextureFileDataID);
                    else
                        t.DuplicateReferences++;
                    if (!t.UseHeightTextures || l.HeightTextureFileDataID == 0)
                        continue;
                    t.HeightLayerReferences++;
                    if (heightListed.Add(l.HeightTextureFileDataID))
                        heightIds.Add(l.HeightTextureFileDataID);
                    else
                        t.HeightDuplicateReferences++;
                }
            }
            t.DrawableSlices = slice;
            var plainIds = new List<uint>();
            foreach (TerrainChunkMaterial c in t.Chunks)
            {
                if (!c.PlainDiagnostic)
                    continue;
                c.AlphaSlice = slice++;
                if (c.AlphaSliceCount > 1)
                    c.AlphaSlice2 = slice++;
                // a layer of unresolved repeats is drawn as the checker: its texture is never bound, so not listed
                foreach (TerrainMaterialLayer l in c.Layers)
                    if (l.TextureFileDataID != 0 && l.RepeatsPerChunk > 0f && listed.Add(l.TextureFileDataID))
                        plainIds.Add(l.TextureFileDataID);
            }
            t.PlainSlices = slice - t.DrawableSlices;
            t.TextureIds = ids.ToArray();
            t.HeightTextureIds = heightIds.ToArray();
            t.PlainOnlyTextureIds = plainIds.ToArray();
            t.Recount();
            return t;
        }

        sealed class MtxpTables
        {
            public uint[] Flags, Word3;
            public float[] Scale, Offset;
        }

        /// <summary>The parser's tile-level Malformed finding on MDID, MHID or MTXP (a repeated chunk, or a
        /// FileDataID table that is not whole u32s), or null. Tile-level issues carry the bare tag as their
        /// chunk label; MCNK issues are labelled "tex0 MCNK n ...".</summary>
        static string TableProblem(List<WorldIssue> issues)
        {
            if (issues != null)
                foreach (WorldIssue i in issues)
                    if (i.Kind == WorldIssueKind.Malformed && (i.Chunk == "MDID" || i.Chunk == "MHID" || i.Chunk == "MTXP"))
                        return "the tex0's " + i.Chunk + " table is malformed: " + i.Reason;
            return null;
        }

        static bool HasTopChunk(AdtTex tex, string tag)
        {
            if (tex.Chunks != null)
                foreach (WorldChunk k in tex.Chunks)
                    if (k.Tag == tag)
                        return true;
            return false;
        }

        static TerrainChunkMaterial BuildChunk(int n, AdtRootChunk rc, AdtTexChunk tc, uint[] mdid, uint[] mhid, MtxpTables mtxp,
                                               string tableProblem, string mtxpProblem, uint mphdFlags,
                                               List<WorldIssue> rootIssues, List<WorldIssue> texIssues, TerrainTileMaterial t)
        {
            var c = new TerrainChunkMaterial { Index = n };
            TerrainLayer[] mcly = tc != null && tc.Layers != null ? tc.Layers : new TerrainLayer[0];
            c.Layers = new TerrainMaterialLayer[mcly.Length];
            for (int k = 0; k < mcly.Length; k++)
            {
                var l = new TerrainMaterialLayer { Layer = k, MdidIndex = (int)Math.Min(mcly[k].TextureIndex, int.MaxValue), Flags = mcly[k].Flags };
                l.IndexInRange = mcly[k].TextureIndex < (uint)mdid.Length;
                l.RepeatsPerChunk = RepeatsPerChunkDefault;
                if (l.IndexInRange)
                {
                    l.TextureFileDataID = mdid[l.MdidIndex];
                    if (tableProblem == null && l.MdidIndex < mhid.Length)
                        l.HeightTextureFileDataID = mhid[l.MdidIndex];
                    if (mtxp.Flags != null)
                    {
                        l.HasMtxp = true;
                        l.MtxpFlags = mtxp.Flags[l.MdidIndex];
                        l.ScaleExponent = (int)((l.MtxpFlags >> 4) & 0xF);
                        l.RepeatsPerChunk = RepeatsForScaleExponent(l.ScaleExponent);
                        l.PhaseU = RepeatPhase(n % 16, l.RepeatsPerChunk);
                        l.PhaseV = RepeatPhase(n / 16, l.RepeatsPerChunk);
                        l.HeightScale = mtxp.Scale[l.MdidIndex];
                        l.HeightOffset = mtxp.Offset[l.MdidIndex];
                        l.MtxpWord3 = mtxp.Word3[l.MdidIndex];
                    }
                    l.Missing = l.TextureFileDataID == 0;
                }
                if (k >= 1 && tc.Alpha != null && k < tc.Alpha.Length && tc.Alpha[k] != null)
                    l.Storage = tc.Alpha[k].Storage;
                c.Layers[k] = l;
            }
            c.ShadowMapIgnored = tc != null && tc.ShadowRaw != null;
            if (c.ShadowMapIgnored) t.ShadowMapsIgnored++;
            if (rc != null)
            {
                // Only a Decoded root chunk says reliably whether MCCV is there (a malformed MCCV is dropped).
                c.VertexColorMismatch = rc.Status == WorldDataStatus.Decoded && ((rc.Flags & VertexColorFlag) != 0) != rc.HasVertexColors;
                if (c.VertexColorMismatch) t.VertexColorMismatches++;
                if (rc.Holes.Kind == HoleMaskKind.HighResolution)
                    c.HoleMask = rc.Holes.HighResolutionBits;
            }
            if (tc != null && tc.Status == WorldDataStatus.Decoded && tc.Alpha != null)
                for (int k = 1; k < tc.Alpha.Length; k++)
                    if (tc.Alpha[k] != null)
                    {
                        if (tc.Alpha[k].Storage == AlphaStorage.Compressed) t.RleLayers++;
                        else if (tc.Alpha[k].Storage == AlphaStorage.Uncompressed8) t.Raw8Layers++;
                        else t.Raw4Layers++;
                    }

            c.Reason = MalformedReason(n, rc, tc, c.Layers, mdid.Length, tableProblem, mtxpProblem, mphdFlags, rootIssues, texIssues);
            if (c.Reason != null)
            {
                c.State = TerrainMaterialState.Malformed;
                return c;
            }
            // From here: both MCNKs Decoded, the tables readable, every index in MDID, alpha decoded for layers
            // 1.. with sums <= 255 and bit depths that agree with MPHD.
            int fourBit = -1;
            for (int k = 1; k < c.Layers.Length; k++)
                if (c.Layers[k].Storage == AlphaStorage.Uncompressed4) { fourBit = k; break; }
            if (fourBit >= 0)
                return State(c, TerrainMaterialState.AlphaUnsupported, string.Format(
                    "layer {0} has 4-bit alpha (MPHD 0x{1:X} has neither 0x4 nor 0x80): the client's draw-time 4-bit expansion is not established (U19)",
                    fourBit, mphdFlags));
            if ((rc.Flags & DoNotFixAlphaFlag) == 0)
                return State(c, TerrainMaterialState.AlphaUnsupported, string.Format(
                    "MCNK flag 0x8000 is clear (flags 0x{0:X}; set on every chunk ever read): how such alpha maps are drawn is not established",
                    rc.Flags));
            if (c.Layers.Length > MaxDrawnLayers)
                return State(c, TerrainMaterialState.LayerCountUnsupported, string.Format(
                    "{0} layers; at most {1} are drawn ({1} is the most observed)", c.Layers.Length, MaxDrawnLayers));
            foreach (TerrainMaterialLayer l in c.Layers)
            {
                if ((l.Flags & ~DrawnLayerFlags) != 0)
                    return Plain(c, rc, tc, TerrainMaterialState.LayerEffectUnresolved, string.Format(
                        "layer {0} (texture {1}) has MCLY flags 0x{2:X} ({3}): the effect's rendering is not established (U10)",
                        l.Layer, l.TextureFileDataID, l.Flags, EffectNames(l.Flags & ~KnownLayerFlags)));
                if (l.HasMtxp && (l.MtxpFlags & ~KnownMtxpFlags) != 0)
                    return Plain(c, rc, tc, TerrainMaterialState.LayerEffectUnresolved, string.Format(
                        "layer {0} (texture {1}): MTXP entry {2} has flags 0x{3:X} ({4}): drawing it as a plain diffuse texture is not established",
                        l.Layer, l.TextureFileDataID, l.MdidIndex, l.MtxpFlags, MtxpFlagNames(l.MtxpFlags & ~KnownMtxpFlags)));
                if (l.HasMtxp && l.MtxpWord3 != 0)
                    return Plain(c, rc, tc, TerrainMaterialState.LayerEffectUnresolved, string.Format(
                        "layer {0} (texture {1}): MTXP entry {2} word 3 is 0x{3:X} (0 in every audited entry): its meaning is not established",
                        l.Layer, l.TextureFileDataID, l.MdidIndex, l.MtxpWord3));
                if (l.HasMtxp && l.ScaleExponent > MaxScaleExponent)
                    return Plain(c, rc, tc, TerrainMaterialState.LayerEffectUnresolved, string.Format(
                        "layer {0} (texture {1}): MTXP entry {2} has flags 0x{3:X}: MTXP texture scale nibble {4}: never observed (0..{5} verified)",
                        l.Layer, l.TextureFileDataID, l.MdidIndex, l.MtxpFlags, l.ScaleExponent, MaxScaleExponent));
                if ((mphdFlags & HeightTextureMphdFlag) != 0 && l.HasMtxp && HeightValuesOutsideDomain(l))
                    return Plain(c, rc, tc, TerrainMaterialState.LayerEffectUnresolved, string.Format(
                        "layer {0} (texture {1}): MTXP entry {2} has height scale s = {3} and offset o = {4} ({5}): MTXP height values let H = h*s + o fall below 0 or overflow float32; the height mix is not defined there",
                        l.Layer, l.TextureFileDataID, l.MdidIndex, Fmt(l.HeightScale), Fmt(l.HeightOffset),
                        l.HeightTextureFileDataID != 0 ? "height texture " + l.HeightTextureFileDataID : "MHID entry 0: H = o"));
            }
            if (c.Layers.Length == 0)
                return State(c, TerrainMaterialState.NoLayers, "no layer (MCLY empty or absent)");
            Plain(c, rc, tc, TerrainMaterialState.Textured, null);
            var zero = new List<int>();
            foreach (TerrainMaterialLayer l in c.Layers)
                if (l.TextureFileDataID == 0)
                    zero.Add(l.Layer);
            string zeroText = zero.Count > 0
                ? "layer(s) " + string.Join(", ", zero) + " name texture id 0 (no texture id); drawn as the missing-texture checker" : null;
            foreach (TerrainMaterialLayer l in c.Layers)
                if ((l.Flags & StaticEffectLayerFlags) != 0)
                {
                    c.State = TerrainMaterialState.LayerEffectStatic;
                    c.Reason = string.Format(
                        "layer {0} (texture {1}) has MCLY flags 0x{2:X} ({3}): drawn with the static composite; the animation and overbright are not applied (their units and factor are not established)",
                        l.Layer, l.TextureFileDataID, l.Flags, EffectNames(l.Flags & StaticEffectLayerFlags)) +
                        (zeroText != null ? "; " + zeroText : "");
                    return c;
                }
            if (zeroText != null)
            {
                c.State = TerrainMaterialState.MissingTexture;
                c.Reason = zeroText;
            }
            return c;
        }

        static TerrainChunkMaterial State(TerrainChunkMaterial c, TerrainMaterialState s, string reason)
        {
            c.State = s;
            c.Reason = reason;
            return c;
        }

        /// <summary>A chunk whose alpha is fully verified (at most MaxDrawnLayers layers): fill its slice(s) -- a second
        /// one only for more than 5 layers -- and its vertex colours.</summary>
        static TerrainChunkMaterial Plain(TerrainChunkMaterial c, AdtRootChunk rc, AdtTexChunk tc, TerrainMaterialState s, string reason)
        {
            c.State = s;
            c.Reason = reason;
            int slices = c.Layers.Length > 1 + TerrainChunkMaterial.LayersPerSlice ? 2 : 1;
            var alpha = new byte[slices * TerrainChunkMaterial.AlphaSliceBytes];
            for (int k = 1; k < c.Layers.Length && k < MaxDrawnLayers; k++)
            {
                int sl, ch;
                TerrainChunkMaterial.AlphaChannelOf(k, out sl, out ch);
                int at = sl * TerrainChunkMaterial.AlphaSliceBytes + ch;
                byte[] a = tc.Alpha[k].Samples;
                for (int i = 0; i < AdtParser.AlphaTexels; i++)
                    alpha[at + 4 * i] = a[i];
            }
            c.Alpha = alpha;
            if ((rc.Flags & VertexColorFlag) != 0 && rc.VertexColorsRaw != null && rc.VertexColorsRaw.Length >= AdtParser.VertexColorBytes)
            {
                var col = new float[3 * TerrainVertexLayout.VertexCount];
                byte[] raw = rc.VertexColorsRaw;
                for (int v = 0; v < TerrainVertexLayout.VertexCount; v++)
                {
                    col[3 * v + 0] = raw[4 * v + 2] / 127f;
                    col[3 * v + 1] = raw[4 * v + 1] / 127f;
                    col[3 * v + 2] = raw[4 * v + 0] / 127f;
                }
                c.VertexColors = col;
            }
            return c;
        }

        /// <summary>The first Malformed condition of section 2 that holds, or null.</summary>
        static string MalformedReason(int n, AdtRootChunk rc, AdtTexChunk tc, TerrainMaterialLayer[] layers, int mdidCount, string tableProblem,
                                      string mtxpProblem, uint mphdFlags, List<WorldIssue> rootIssues, List<WorldIssue> texIssues)
        {
            if (tc == null || tc.Status != WorldDataStatus.Decoded)
                return "tex0 MCNK " + n + " is malformed: " + IssueFor(texIssues, "tex0 MCNK " + n);
            if (rc == null || rc.Status != WorldDataStatus.Decoded)
                return "root MCNK " + n + " is malformed (its geometry is not built): " + IssueFor(rootIssues, "MCNK " + n);
            if (layers.Length > 0 && tableProblem != null)
                return tableProblem;
            foreach (TerrainMaterialLayer l in layers)
                if (!l.IndexInRange)
                    return string.Format("layer {0} names MDID entry {1}, but MDID has {2}", l.Layer, l.MdidIndex, mdidCount);
            if (layers.Length > 0 && mtxpProblem != null)
                return mtxpProblem;
            foreach (TerrainMaterialLayer l in layers)
                if (l.HasMtxp && (!IsFinite(l.HeightScale) || !IsFinite(l.HeightOffset)))
                    return string.Format("layer {0}: MTXP entry {1} has height scale {2} and offset {3} (not finite numbers)",
                                         l.Layer, l.MdidIndex, Fmt(l.HeightScale), Fmt(l.HeightOffset));
            int want = (mphdFlags & 0x84) != 0 ? 8 : 4;
            for (int k = 1; k < layers.Length; k++)
            {
                AlphaMap a = tc.Alpha != null && k < tc.Alpha.Length ? tc.Alpha[k] : null;
                if (a == null || a.Samples == null || a.Samples.Length != AdtParser.AlphaTexels)
                    return "layer " + k + " has no decoded alpha map";
                if (a.BitDepth != want)
                    return string.Format("MPHD 0x{0:X} announces {1}-bit alpha, but layer {2} is stored {3} ({4}-bit)",
                                         mphdFlags, want, k, StorageName(a.Storage), a.BitDepth);
            }
            if (want == 8 && layers.Length > 2)
            {
                for (int i = 0; i < AdtParser.AlphaTexels; i++)
                {
                    int sum = 0;
                    for (int k = 1; k < layers.Length; k++)
                        sum += tc.Alpha[k].Samples[i];
                    if (sum > 255)
                        return string.Format("alpha layers 1..{0} sum to {1} at texel (row {2}, column {3}); weights never exceed 255",
                                             layers.Length - 1, sum, i / 64, i % 64);
                }
            }
            return null;
        }

        static bool IsFinite(float f) { return !float.IsNaN(f) && !float.IsInfinity(f); }

        /// <summary>
        /// MTXP height values outside the domain where the height mix is defined (call on an MPHD 0x80 map, for
        /// a layer with MTXP whose values are finite -- non-finite ones are Malformed first). With s = height
        /// scale and o = height offset as stored (float32) and h in 0..1: offset o &lt; 0 (H = o where h = 0,
        /// and H = o everywhere when the MHID entry is 0), or, for a layer whose MHID entry is not 0, s + o &lt; 0
        /// or s + o not finite in float32 (H = s + o where h = 1). Otherwise H stays in 0..max(o, s + o), finite.
        /// H = 0 exactly is inside the domain (the S == 0 fallback stays as the contract has it).
        /// </summary>
        static bool HeightValuesOutsideDomain(TerrainMaterialLayer l)
        {
            if (l.HeightOffset < 0f)
                return true;
            if (l.HeightTextureFileDataID == 0)
                return false;
            float top = l.HeightScale + l.HeightOffset;
            return top < 0f || !IsFinite(top);
        }

        static string IssueFor(List<WorldIssue> issues, string label)
        {
            if (issues != null)
                foreach (WorldIssue i in issues)
                    if (i.Chunk != null && (i.Chunk == label || i.Chunk.StartsWith(label + " ", StringComparison.Ordinal)))
                        return i.Reason;
            return "the parser marked it Malformed (see the file's issues)";
        }

        static string StorageName(AlphaStorage s)
        {
            return s == AlphaStorage.Compressed ? "RLE" : s == AlphaStorage.Uncompressed8 ? "raw 4096 bytes" : "raw 2048 bytes";
        }

        static string EffectNames(uint bits)
        {
            var names = new List<string>();
            if ((bits & 0x7) != 0) names.Add("rotation 0x" + (bits & 0x7).ToString("X"));
            if ((bits & 0x38) != 0) names.Add("speed 0x" + (bits & 0x38).ToString("X"));
            if ((bits & 0x40) != 0) names.Add("animation 0x40");
            if ((bits & 0x80) != 0) names.Add("overbright 0x80");
            if ((bits & 0x400) != 0) names.Add("cube map 0x400");
            uint other = bits & ~0x4FFu;
            if (other != 0) names.Add("never observed 0x" + other.ToString("X"));
            return string.Join(", ", names);
        }

        static string MtxpFlagNames(uint bits)
        {
            var names = new List<string>();
            if ((bits & 0x1) != 0) names.Add("0x1 = cube-map texture");
            uint other = bits & ~0x1u;
            if (other != 0) names.Add("never observed 0x" + other.ToString("X"));
            return string.Join(", ", names);
        }

        static string Fmt(float f) { return f.ToString("R", System.Globalization.CultureInfo.InvariantCulture); }
    }

    /// <summary>Texels of one layer or height texture for the compositor: Width x Height x (R, G, B) in 0..1,
    /// and the alpha channel A in 0..1 when known (a height texture needs it), row 0 first (as stored: v runs
    /// south).</summary>
    public sealed class TerrainTexels
    {
        public int Width, Height;
        public float[] Rgb;
        /// <summary>Width x Height alpha values in 0..1, or null (only height textures read it).</summary>
        public float[] A;

        /// <summary>From RGBA32 bytes (value / 255, alpha kept in A), row 0 first.</summary>
        public static TerrainTexels FromRgba32(int width, int height, byte[] rgba)
        {
            if (width <= 0 || height <= 0 || rgba == null || rgba.Length < width * height * 4)
                throw new ArgumentException("RGBA32 texels need width * height * 4 bytes");
            var t = new TerrainTexels { Width = width, Height = height, Rgb = new float[width * height * 3], A = new float[width * height] };
            for (int i = 0; i < width * height; i++)
            {
                t.Rgb[3 * i] = rgba[4 * i] / 255f;
                t.Rgb[3 * i + 1] = rgba[4 * i + 1] / 255f;
                t.Rgb[3 * i + 2] = rgba[4 * i + 2] / 255f;
                t.A[i] = rgba[4 * i + 3] / 255f;
            }
            return t;
        }

        /// <summary>From float RGB texels (copied), row 0 first; no alpha.</summary>
        public static TerrainTexels FromRgb(int width, int height, float[] rgb)
        {
            if (width <= 0 || height <= 0 || rgb == null || rgb.Length < width * height * 3)
                throw new ArgumentException("RGB texels need width * height * 3 floats");
            var t = new TerrainTexels { Width = width, Height = height, Rgb = new float[width * height * 3] };
            Array.Copy(rgb, t.Rgb, width * height * 3);
            return t;
        }

        /// <summary>From float RGBA texels (copied), row 0 first.</summary>
        public static TerrainTexels FromRgba(int width, int height, float[] rgba)
        {
            if (width <= 0 || height <= 0 || rgba == null || rgba.Length < width * height * 4)
                throw new ArgumentException("RGBA texels need width * height * 4 floats");
            var t = new TerrainTexels { Width = width, Height = height, Rgb = new float[width * height * 3], A = new float[width * height] };
            for (int i = 0; i < width * height; i++)
            {
                t.Rgb[3 * i] = rgba[4 * i];
                t.Rgb[3 * i + 1] = rgba[4 * i + 1];
                t.Rgb[3 * i + 2] = rgba[4 * i + 2];
                t.A[i] = rgba[4 * i + 3];
            }
            return t;
        }

        public static TerrainTexels Constant(float r, float g, float b)
        {
            return new TerrainTexels { Width = 1, Height = 1, Rgb = new[] { r, g, b } };
        }

        public static TerrainTexels Constant(float r, float g, float b, float a)
        {
            return new TerrainTexels { Width = 1, Height = 1, Rgb = new[] { r, g, b }, A = new[] { a } };
        }
    }

    /// <summary>
    /// The composite of one chunk with the terrain shader's math (design sections 3 and 4, v3 delta section
    /// 1) in validation sampling, on the CPU. At chunk position (u, v) in cells (0..8; u east, v south):
    ///   alpha     a_k = bilinear(the chunk's slice (k - 1) / 4, channel (k - 1) % 4) at x = (u / 8) * 64 - 0.5,
    ///             y = (v / 8) * 64 - 0.5 (texel i centred at AlphaTexelCentre(i)), indices clamped to 0..63,
    ///             value byte / 255; lerp along x on the two rows, then along y
    ///   weights   w_0 = 1 - sum a_k (k = 1..), w_k = a_k; then the height mix (HeightMixWeights) with
    ///             H_k = HeightAt (1 when the tile's MPHD has no 0x80), or plain weights when asked
    ///   layer k   texel (floor(s_u * W) mod W, floor(s_v * H) mod H), s = (u, v) * repeats_k / 8 + (PhaseU_k,
    ///             PhaseV_k); a Missing layer, or one of unresolved repeats (RepeatsPerChunk 0), is the checker:
    ///             magenta (1, 0, 1) where floor(8u) + floor(8v) is even, black elsewhere (one square per 1/8 cell)
    ///   height k  the height texture's ALPHA at the same s, by index in its own size: h_k; H_k = h_k *
    ///             heightScale_k + heightOffset_k (h_k = 0 when the layer's MHID entry is 0)
    ///   colour    sum weight_k * layer_k.rgb, x the vertex colour interpolated over the cell's four-triangle
    ///             fan (1 without MCCV); saturated to 0..1 only when asked (the production view's 8-bit range).
    /// </summary>
    public static class TerrainCompositor
    {
        /// <summary>Texel index at texture coordinate s (in repeats): floor(s * size) wrapped into 0..size-1.</summary>
        public static int TexelIndex(float s, int size)
        {
            int i = (int)Math.Floor(s * size) % size;
            return i < 0 ? i + size : i;
        }

        /// <summary>Alpha layer `layer` (1..7) of a chunk's alpha (its one or two 64x64x4 slices back to back:
        /// TerrainChunkMaterial.Alpha; layers 5..7 need the second slice) at chunk position (u, v), manual bilinear
        /// with clamp at the AlphaTexelCentre alignment.</summary>
        public static float AlphaAt(byte[] alpha, int layer, float u, float v)
        {
            int sl, ch;
            TerrainChunkMaterial.AlphaChannelOf(layer, out sl, out ch);
            if (alpha == null || alpha.Length < TerrainChunkMaterial.AlphaSliceBytes)
                throw new ArgumentException("an alpha slice has 64 * 64 * 4 bytes");
            if (alpha.Length < (sl + 1) * TerrainChunkMaterial.AlphaSliceBytes)
                throw new ArgumentException("alpha layer " + layer + " is in the chunk's second slice; the alpha given has one slice");
            float x = TerrainMaterialBuilder.AlphaTexelCoordinate(u / 8f), y = TerrainMaterialBuilder.AlphaTexelCoordinate(v / 8f);
            float fx0 = (float)Math.Floor(x), fy0 = (float)Math.Floor(y);
            float fx = x - fx0, fy = y - fy0;
            int x0 = Clamp((int)fx0), x1 = Clamp((int)fx0 + 1), y0 = Clamp((int)fy0), y1 = Clamp((int)fy0 + 1);
            ch += sl * TerrainChunkMaterial.AlphaSliceBytes;
            float a00 = alpha[4 * (y0 * 64 + x0) + ch] / 255f, a10 = alpha[4 * (y0 * 64 + x1) + ch] / 255f;
            float a01 = alpha[4 * (y1 * 64 + x0) + ch] / 255f, a11 = alpha[4 * (y1 * 64 + x1) + ch] / 255f;
            float top = a00 + (a10 - a00) * fx;
            float bottom = a01 + (a11 - a01) * fx;
            return top + (bottom - top) * fy;
        }

        static int Clamp(int i) { return i < 0 ? 0 : i > 63 ? 63 : i; }

        /// <summary>
        /// The height-mix weights (file header; the order of operations of the verified rule): p_k =
        /// max(0, w_k) * H_k; m = max p_k; p_k *= 1 - clamp(m - p_k, 0, 1); S = sum p_k (k = 0 first);
        /// result_k = p_k / S. When S <= 0 the result is max(0, w_k) and the method returns false. S < 0 is
        /// reachable whenever some H_k < 0, even with another H_k > 0 where w_k > 0 (H = (0.3, -1) at w = (0.5,
        /// 0.5) gives S = -0.025), and near such poles the weights are unbounded. The builder makes every layer
        /// whose MTXP height values let H fall below 0 or overflow float32 LayerEffectUnresolved, so on a drawn
        /// chunk H_k >= 0, S >= 0, and only S == 0 (every p_k = 0, e.g. H_k = 0 exactly) reaches the fallback:
        /// the contract's fallback, itself unverified. `w` and `h` hold n values; `result` gets n.
        /// </summary>
        public static bool HeightMixWeights(float[] w, float[] h, int n, float[] result)
        {
            if (w == null || h == null || result == null || n < 1 || w.Length < n || h.Length < n || result.Length < n)
                throw new ArgumentException("height mix: n >= 1 weights, heights and result slots");
            float m = float.NegativeInfinity;
            for (int k = 0; k < n; k++)
            {
                result[k] = Math.Max(0f, w[k]) * h[k];
                m = Math.Max(m, result[k]);
            }
            float s = 0f;
            for (int k = 0; k < n; k++)
            {
                result[k] = result[k] * (1f - Saturate(m - result[k]));
                s += result[k];
            }
            if (s > 0f)
            {
                for (int k = 0; k < n; k++)
                    result[k] = result[k] / s;
                return true;
            }
            for (int k = 0; k < n; k++)
                result[k] = Math.Max(0f, w[k]);
            return false;
        }

        /// <summary>Vertex colour at chunk position (u, v): barycentric over the four-triangle fan of the cell
        /// that holds it (outer corners and the inner centre vertex). `colors` = 145 x RGB or null (1, 1, 1).</summary>
        public static void VertexColorAt(float[] colors, float u, float v, out float r, out float g, out float b)
        {
            if (colors == null) { r = g = b = 1f; return; }
            int col = Math.Min(7, Math.Max(0, (int)Math.Floor(u)));
            int row = Math.Min(7, Math.Max(0, (int)Math.Floor(v)));
            float fu = u - col, fv = v - row;
            int ia = TerrainVertexLayout.Outer(row, col), ib = TerrainVertexLayout.Outer(row, col + 1);
            int id = TerrainVertexLayout.Outer(row + 1, col), ie = TerrainVertexLayout.Outer(row + 1, col + 1);
            int im = TerrainVertexLayout.Inner(row, col);
            // the triangle nearest edge: north, east, south, west (ties go to the first; values agree there)
            float dn = fv, de = 1f - fu, ds = 1f - fv, dw = fu;
            float m = Math.Min(Math.Min(dn, de), Math.Min(ds, dw));
            int p, q;
            float wp, wq, wc;
            if (dn == m) { wc = 2f * fv; wq = fu - fv; wp = 1f - wc - wq; p = ia; q = ib; }
            else if (de == m) { wc = 2f * (1f - fu); wq = fv - (1f - fu); wp = 1f - wc - wq; p = ib; q = ie; }
            else if (ds == m) { wc = 2f * (1f - fv); wq = fu - (1f - fv); wp = 1f - wc - wq; p = id; q = ie; }
            else { wc = 2f * fu; wq = fv - fu; wp = 1f - wc - wq; p = ia; q = id; }
            r = wp * colors[3 * p] + wq * colors[3 * q] + wc * colors[3 * im];
            g = wp * colors[3 * p + 1] + wq * colors[3 * q + 1] + wc * colors[3 * im + 1];
            b = wp * colors[3 * p + 2] + wq * colors[3 * q + 2] + wc * colors[3 * im + 2];
        }

        /// <summary>True where the missing-texture checker is magenta: floor(8u) + floor(8v) even.</summary>
        public static bool CheckerMagenta(float u, float v)
        {
            long s = (long)Math.Floor(u * 8f) + (long)Math.Floor(v * 8f);
            return (s & 1) == 0;
        }

        /// <summary>Is chunk position (u, v) inside a hole cell of the chunk's mask?</summary>
        public static bool IsHole(ulong holeMask, float u, float v)
        {
            int col = Math.Min(7, Math.Max(0, (int)Math.Floor(u)));
            int row = Math.Min(7, Math.Max(0, (int)Math.Floor(v)));
            return ((holeMask >> (row * 8 + col)) & 1UL) != 0;
        }

        /// <summary>The production composite (the height mix) at chunk position (u, v). The chunk must have
        /// layers and an alpha slice; `textures` gives the texels of each non-missing layer and, on an MPHD
        /// 0x80 map, of each height texture, by FileDataID.</summary>
        public static void Compose(TerrainChunkMaterial chunk, Func<uint, TerrainTexels> textures, float u, float v, bool saturate,
                                   out float r, out float g, out float b)
        {
            Compose(chunk, textures, u, v, saturate, TerrainBlendRule.HeightMix, out r, out g, out b);
        }

        /// <summary>The composite at chunk position (u, v) with the given weights rule.</summary>
        public static void Compose(TerrainChunkMaterial chunk, Func<uint, TerrainTexels> textures, float u, float v, bool saturate,
                                   TerrainBlendRule rule, out float r, out float g, out float b)
        {
            if (chunk == null) throw new ArgumentNullException("chunk");
            if (chunk.Alpha == null || chunk.Layers.Length == 0 || chunk.Layers.Length > TerrainMaterialBuilder.MaxDrawnLayers)
                throw new ArgumentException("chunk " + chunk.Index + " is " + TerrainTileMaterial.StateName(chunk.State) + ": it has no composite");
            if (rule == TerrainBlendRule.HeightMix && chunk.State == TerrainMaterialState.HeightTextureMissing)
                throw new ArgumentException("chunk " + chunk.Index + " is heightTextureMissing: it has no height-mix composite (" + chunk.Reason + ")");
            int n = chunk.Layers.Length;
            var w = new float[n];
            float sum = 0f;
            for (int k = 1; k < n; k++)
            {
                w[k] = AlphaAt(chunk.Alpha, k, u, v);
                sum += w[k];
            }
            w[0] = 1f - sum;
            float[] weight = w;
            if (rule == TerrainBlendRule.HeightMix)
            {
                var h = new float[n];
                for (int k = 0; k < n; k++)
                    h[k] = HeightAt(chunk, chunk.Layers[k], textures, u, v);
                weight = new float[n];
                HeightMixWeights(w, h, n, weight);
            }
            float cr = 0f, cg = 0f, cb = 0f;
            for (int k = 0; k < n; k++)
            {
                float lr, lg, lb;
                LayerTexel(chunk.Layers[k], textures, u, v, out lr, out lg, out lb);
                cr += weight[k] * lr;
                cg += weight[k] * lg;
                cb += weight[k] * lb;
            }
            float vr, vg, vb;
            VertexColorAt(chunk.VertexColors, u, v, out vr, out vg, out vb);
            r = cr * vr;
            g = cg * vg;
            b = cb * vb;
            if (saturate)
            {
                r = Saturate(r);
                g = Saturate(g);
                b = Saturate(b);
            }
        }

        /// <summary>The texture coordinate (in repeats) of layer `l` at chunk position (u, v): s = (u, v) *
        /// RepeatsPerChunk / 8 + (PhaseU, PhaseV). The phase is 0 for whole repeats, so there s is exactly the
        /// chunk-local u * r / 8.</summary>
        public static void LayerCoordinate(TerrainMaterialLayer l, float u, float v, out float su, out float sv)
        {
            su = u * l.RepeatsPerChunk / 8f + l.PhaseU;
            sv = v * l.RepeatsPerChunk / 8f + l.PhaseV;
        }

        /// <summary>H_k of layer `l` at (u, v): 1 when the chunk's tile has no MPHD 0x80; otherwise h * scale +
        /// offset, h = the height texture's alpha at the layer's texture coordinate (LayerCoordinate: its repeats
        /// and phase; by index in the height texture's own size), 0 when the layer's MHID entry is 0. A layer of
        /// unresolved repeats (RepeatsPerChunk 0) has no coordinate: reading its height texture is an error.</summary>
        public static float HeightAt(TerrainChunkMaterial chunk, TerrainMaterialLayer l, Func<uint, TerrainTexels> textures, float u, float v)
        {
            if (!chunk.UseHeightTextures)
                return 1f;
            float h = 0f;
            if (l.HeightTextureFileDataID != 0)
            {
                if (l.RepeatsPerChunk <= 0f)
                    throw new ArgumentException("layer " + l.Layer + " has unresolved repeats (MTXP texture scale nibble " + l.ScaleExponent +
                                                "): its height texture " + l.HeightTextureFileDataID + " has no defined coordinate");
                TerrainTexels t = textures != null ? textures(l.HeightTextureFileDataID) : null;
                if (t == null)
                    throw new ArgumentException("no texels for height texture " + l.HeightTextureFileDataID + " (layer " + l.Layer + ")");
                if (t.A == null)
                    throw new ArgumentException("height texture " + l.HeightTextureFileDataID + " has no alpha texels (the height is its alpha channel)");
                float su, sv;
                LayerCoordinate(l, u, v, out su, out sv);
                h = t.A[TexelIndex(sv, t.Height) * t.Width + TexelIndex(su, t.Width)];
            }
            return h * l.HeightScale + l.HeightOffset;
        }

        /// <summary>The colour of layer `l` at (u, v) before weighting: its texel at LayerCoordinate, or the
        /// checker for a Missing layer and for a layer of unresolved repeats (RepeatsPerChunk 0, an MTXP scale
        /// nibble above the verified ones), whose texture the player never binds.</summary>
        public static void LayerTexel(TerrainMaterialLayer l, Func<uint, TerrainTexels> textures, float u, float v, out float r, out float g, out float b)
        {
            if (l.Missing || l.RepeatsPerChunk <= 0f)
            {
                bool magenta = CheckerMagenta(u, v);
                r = magenta ? 1f : 0f;
                g = 0f;
                b = magenta ? 1f : 0f;
                return;
            }
            TerrainTexels t = textures != null ? textures(l.TextureFileDataID) : null;
            if (t == null)
                throw new ArgumentException("no texels for texture " + l.TextureFileDataID + " (layer " + l.Layer + ")");
            float su, sv;
            LayerCoordinate(l, u, v, out su, out sv);
            int i = TexelIndex(sv, t.Height) * t.Width + TexelIndex(su, t.Width);
            r = t.Rgb[3 * i];
            g = t.Rgb[3 * i + 1];
            b = t.Rgb[3 * i + 2];
        }

        /// <summary>
        /// The chunk rendered alone at size x size pixels with the height mix: pixel (py, px) at (u, v) =
        /// ((px + 0.5) * 8 / size, (py + 0.5) * 8 / size), north row first; float RGBA, alpha 1, or RGBA all 0
        /// in a hole cell.
        /// </summary>
        public static float[] ComposeGrid(TerrainChunkMaterial chunk, Func<uint, TerrainTexels> textures, int size, bool saturate)
        {
            return ComposeGrid(chunk, textures, size, saturate, TerrainBlendRule.HeightMix);
        }

        /// <summary>ComposeGrid with the given weights rule.</summary>
        public static float[] ComposeGrid(TerrainChunkMaterial chunk, Func<uint, TerrainTexels> textures, int size, bool saturate, TerrainBlendRule rule)
        {
            if (size <= 0) throw new ArgumentOutOfRangeException("size");
            var px = new float[size * size * 4];
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    float u = (x + 0.5f) * 8f / size, v = (y + 0.5f) * 8f / size;
                    if (IsHole(chunk.HoleMask, u, v))
                        continue;
                    float r, g, b;
                    Compose(chunk, textures, u, v, saturate, rule, out r, out g, out b);
                    int o = 4 * (y * size + x);
                    px[o] = r;
                    px[o + 1] = g;
                    px[o + 2] = b;
                    px[o + 3] = 1f;
                }
            return px;
        }

        public static float Saturate(float f) { return f < 0f ? 0f : f > 1f ? 1f : f; }
    }
}
