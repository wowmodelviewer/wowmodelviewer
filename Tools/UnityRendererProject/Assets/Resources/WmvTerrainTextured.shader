// WmvTerrainTextured.shader -- Wmv/TerrainTextured, the terrain chunk's TEXTURE MATERIAL (stage 04 design, sections 1
// and 4, with the v3 delta: the height-mix composite). One material per chunk drawn textured (WmvTerrainBuilder);
// neutral chunks keep Wmv/Terrain.
//
// THE COMPOSITE (the CPU twin is Wow/World/TerrainMaterial.cs TerrainCompositor, operation for operation), up to 8
// layers (TerrainMaterialBuilder.MaxDrawnLayers):
//   uv          the mesh's UV0 in CELL units: u east, v south, 0..8 over the chunk (texel (0, 0) at its north-west corner)
//   alpha       a_k (k = 1..4: channels R, G, B, A of the tile's alpha array, slice _WmvAlphaSlice; k = 5..7: channels
//               R, G, B of the chunk's SECOND slice, _WmvAlphaSlice2, only for a chunk of more than 5 layers) at uv / 8,
//               texel i centred at (i + 0.5) / 64 of the chunk (TerrainMaterialBuilder.AlphaTexelCentre: PROVEN for the
//               game's map generator, INFERRED for the client), bilinear with clamp
//   raw weights w_0 = 1 - sum a_k (k = 1, 2, ... in that order), w_k = a_k (sums never exceed 255: a chunk where they do is
//               Malformed and never drawn here)
//   layer k     texture k at s_k = uv * repeats_k / 8 + phase_k (TerrainCompositor.LayerCoordinate: repeats = 8 / 2^n for
//               the MTXP scale nibble n = 0..4, a float -- 0.5 for nibble 4 --, phase_k = (frac(ix * repeats_k),
//               frac(iy * repeats_k)) of the chunk (ix, iy), 0 for whole repeats; both per material), rows as stored, colour
//               only (the texture's alpha channel is not used); a MISSING layer (texture id 0, the texture failed, or
//               repeats unresolved) is a checker of magenta and black, one square per 1/8 cell, weighted like the texture it
//               stands for
//   height k    H_k = 1 when the tile's MPHD has no 0x80 (_WmvUseHeights 0: MHID and the MTXP height values are not
//               read); otherwise H_k = h_k * heightScale_k + heightOffset_k (MTXP words 1 and 2 of the layer's texture,
//               (0, 1) without MTXP), h_k = the ALPHA of the layer's height texture (MHID) at the same s_k (the same repeats
//               and phase), 0 when the layer names no height texture (hasHeight_k 0)
//   height mix  p_k = max(0, w_k) * H_k; m = max_k p_k; p_k *= 1 - saturate(m - p_k); S = sum_k p_k; weight_k =
//               p_k / S, or max(0, w_k) when S <= 0 (only possible with heights)
//   colour      sum weight_k * layer_k, in AUTHORED (stored) values, times the vertex colour (MCCV / 127, from the mesh,
//               when the chunk has one); then saturated to 0..1 (the 8-bit range of the game's own map texture; MCCV
//               takes a few composites above 1 on real tiles, INFERRED to be clipped in the game too) unless the
//               validation global _WmvTerrainUnclamped is set
// The height mix is PROVEN for the game's baked map texture (golden tiles with and without MPHD 0x80, and a tile with
// height textures; chunks of 6..8 layers and nibble 4 on Retail map 2923) and INFERRED for the client's draw-time
// shader. Plain weights (weight_k = w_k) are only the labelled diagnostic below.
//
// PER-LAYER VALUES are packed four to a vector: the property P holds layers 0..3 in x, y, z, w and P47 layers 4..7
// (_WmvRepeats/_WmvRepeats47, _WmvPhaseU/_WmvPhaseU47, _WmvPhaseV/_WmvPhaseV47, _WmvMissing/_WmvMissing47,
// _WmvHasHeight/_WmvHasHeight47, _WmvHeightScale/_WmvHeightScale47, _WmvHeightOffset/_WmvHeightOffset47).
//
// SAMPLING. Production: the hardware -- the layer and height textures' own stored mips, Repeat, trilinear (the
// anisotropy the builder sets), the alpha array bilinear with Clamp and no mips. VALIDATION (the global
// _WmvTerrainValidate, set by -wmvTerrainValidate and the dumps): texels by Load at integer indices (floor(s * size)
// wrapped, a height texture in its own size), the alpha by four Loads and float weights -- exactly TerrainCompositor's
// arithmetic, so the GPU can be compared with the CPU.
//
// SAMPLERS. D3D11 gives a shader 16 sampler states; 8 layers, 8 height textures and the alpha array would be 17. So the
// shader declares nine: one per layer slot and the alpha array's. Height texture k is sampled (production only) with
// LAYER k's sampler, sampler_WmvLayerk: the builder uploads diffuse and height textures with identical settings (Repeat,
// trilinear, WmvTerrainMaterials.LayerAnisoLevel, mip bias 0), so it is the sampler the height texture's own would be. A
// layer slot that holds no texture (a missing layer: texture id 0, failed, or repeats unresolved) holds the tile's SAMPLER
// CARRIER instead (WmvTerrainMaterials.CreateSamplerCarrier: the same settings, never drawn -- the slot's colour is the
// checker), so a height texture read through that slot keeps the same sampler; an unbound slot would hand it Unity's
// default texture's sampler. Validation sampling reads by Load and uses no sampler.
//
// VIEWS (_WmvTexturedView, per material):
//   0 textured              the composite, saturated
//   1 texturedlit           the composite times the stage 03 diagnostic Lambert (Wmv/Terrain's fixed key), then
//                           saturated -- a DIAGNOSTIC light, not the game's
//   2 texturedplainweights  a material with _WmvHatch: PLAIN WEIGHTS (weight_k = w_k: no cutoff, no heights) under a
//                           diagonal hatch -- a labelled comparison, never the composite
//   3 state colour          a flat colour (_WmvStateColour) times the Lambert: the materials view's legend colour of the
//                           chunk
//   4 texel copy            layer 0's texel at mip _WmvTexelMip by Load, one repeat over the chunk, alpha included,
//                           unclamped: the texel dump's way of reading what the GPU decoded (height textures included)
//
// THE COLOUR DOMAIN, as Wmv/Terrain and Wmv/Wmo: textures are UNorm (never sRGB), so the blend runs on the stored
// values, and the fragment writes AUTHORED values; with _WmvShaderEncode set (the per-fragment display mode) it converts
// them to linear itself, otherwise the frame decode pass does it once for the frame. In the A/B display modes without
// the authored domain (WMV_DISPLAY notonemap / legacy, WmvModelBuilder.AuthoredTextureDomain false) the builder sets
// _WmvLinearOut so the authored composite is converted here and displays as in the default mode (the composite dumps
// set it to 0 for their render and put it back).
//
// THE OUTDOOR LIGHT (Stage 10 step 5, docs/world-viewer/10-light-sky-fog.md 4.5, 6.10, 6.15; Wow/World/TerrainLight.cs is the
// CPU twin and the evidence): a branch taken only in the textured view (0, not the plain-weights diagnostic) while the global
// _WmvWLOn is above 0.5 -- set by WmvWorldLight for the main camera's world view alone, 0 for every other camera, pinned 0 in
// WmvTerrainComposite's renders and the self-test's fixtures, never set with WMV_LIGHT=off -- and _WmvWLTerrain.x is (not
// -wmvTerrainLight=off). The path above is left as it is: the branch starts from the same composite (before the vertex
// colour and the saturate) and replaces the colour; the saturate and the encode below then apply as before. In the branch,
// the client's terrain program (Retail 2977247, the hemisphere variant; _WmvWLTerrain.y selects the flat class, for the
// record only):
//   albedo = composite x MCCV x 2 (the mesh carries MCCV / 127, so x 254/255 gives the client's 2 x byte / 255; 1 without
//            MCCV: the client's 0.5 x 2), NOT saturated
//   N      = the MCNR normal (TEXCOORD1: the builder's own stream, uploaded only while the light is on, so the geometry
//            normals stay as they were) as interpolated, NOT normalised: the client's program uses the MCNR / 127 vector at
//            its own length (median 0.994, MEASURED), so the /127 scale (INFERRED (public)) reaches the colour
//   H      = N.y >= 0 ? lerp(Horizon, Ambient, N.y) : lerp(Horizon, Ground, -N.y), Horizon or Ground 0 -> Ambient
//   A      = H x (0.7 + 0.4 x (0.5 + 0.5 x N.L)), N.L not saturated; D = Direct x max(N.L, 0) x attenuation x shadow
//   colour = sqrt((albedo (A + D))^2 + albedo^2 x local) + specular + second light
// with L = _WmvWLLightDir and the colours _WmvWLAmbient, _WmvWLHorizonAmbient, _WmvWLGroundAmbient, _WmvWLDirect (authored,
// the table's byte / 255); attenuation 1, shadow 1, local 0, specular 0 and the second light 0 are held (graded in
// TerrainLight.cs and WorldLightModel.Terrain).
//
// HLSLPROGRAM with URP's Core.hlsl, explicit float throughout, every switch a uniform (never a keyword: the player build
// strips variants), every per-material value in the UnityPerMaterial CBUFFER and no MaterialPropertyBlock (the SRP
// Batcher's conditions). No LightMode tag, as the other Wmv shaders: drawn by the ordinary pass. Opaque, alpha written 1.

Shader "Wmv/TerrainTextured"
{
    Properties
    {
        _WmvLayer0 ("Layer 0 (base)", 2D) = "black" {}
        _WmvLayer1 ("Layer 1", 2D) = "black" {}
        _WmvLayer2 ("Layer 2", 2D) = "black" {}
        _WmvLayer3 ("Layer 3", 2D) = "black" {}
        _WmvLayer4 ("Layer 4", 2D) = "black" {}
        _WmvLayer5 ("Layer 5", 2D) = "black" {}
        _WmvLayer6 ("Layer 6", 2D) = "black" {}
        _WmvLayer7 ("Layer 7", 2D) = "black" {}
        _WmvHeight0 ("Height texture of layer 0 (its alpha)", 2D) = "black" {}
        _WmvHeight1 ("Height texture of layer 1", 2D) = "black" {}
        _WmvHeight2 ("Height texture of layer 2", 2D) = "black" {}
        _WmvHeight3 ("Height texture of layer 3", 2D) = "black" {}
        _WmvHeight4 ("Height texture of layer 4", 2D) = "black" {}
        _WmvHeight5 ("Height texture of layer 5", 2D) = "black" {}
        _WmvHeight6 ("Height texture of layer 6", 2D) = "black" {}
        _WmvHeight7 ("Height texture of layer 7", 2D) = "black" {}
        _WmvAlpha ("Alpha array (layers 1..4 in R, G, B, A; layers 5..7 in a second slice's R, G, B)", 2DArray) = "" {}
        _WmvAlphaSlice ("Alpha slice (layers 1..4)", Float) = 0
        _WmvAlphaSlice2 ("Second alpha slice (layers 5..7)", Float) = 0
        _WmvLayerCount ("Layers (1..8)", Float) = 1
        _WmvRepeats ("Repeats per chunk, layers 0..3", Vector) = (8, 8, 8, 8)
        _WmvRepeats47 ("Repeats per chunk, layers 4..7", Vector) = (8, 8, 8, 8)
        _WmvPhaseU ("UV phase u (repeats), layers 0..3", Vector) = (0, 0, 0, 0)
        _WmvPhaseU47 ("UV phase u (repeats), layers 4..7", Vector) = (0, 0, 0, 0)
        _WmvPhaseV ("UV phase v (repeats), layers 0..3", Vector) = (0, 0, 0, 0)
        _WmvPhaseV47 ("UV phase v (repeats), layers 4..7", Vector) = (0, 0, 0, 0)
        _WmvMissing ("Missing (checker), layers 0..3", Vector) = (0, 0, 0, 0)
        _WmvMissing47 ("Missing (checker), layers 4..7", Vector) = (0, 0, 0, 0)
        _WmvUseHeights ("MPHD 0x80: the height textures and MTXP height values enter the mix", Float) = 0
        _WmvHasHeight ("A height texture is bound, layers 0..3", Vector) = (0, 0, 0, 0)
        _WmvHasHeight47 ("A height texture is bound, layers 4..7", Vector) = (0, 0, 0, 0)
        _WmvHeightScale ("MTXP height scale, layers 0..3", Vector) = (0, 0, 0, 0)
        _WmvHeightScale47 ("MTXP height scale, layers 4..7", Vector) = (0, 0, 0, 0)
        _WmvHeightOffset ("MTXP height offset, layers 0..3", Vector) = (1, 1, 1, 1)
        _WmvHeightOffset47 ("MTXP height offset, layers 4..7", Vector) = (1, 1, 1, 1)
        _WmvTexturedView ("View (0 textured, 1 lit, 2 plain weights, 3 state colour, 4 texel copy)", Float) = 0
        _WmvHatch ("Plain-weights diagnostic (no cutoff, no heights, hatched)", Float) = 0
        _WmvVertexColors ("Vertex colours (0 none, 1 MCCV)", Float) = 0
        _WmvStateColour ("State colour (authored, set as a vector)", Vector) = (1, 1, 1, 1)
        _WmvTexelMip ("Texel copy mip", Float) = 0
        _WmvLinearOut ("Convert the authored composite to linear", Float) = 0
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2    // Back
    }

    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull [_Cull]
        ZWrite On
        ZTest LEqual
        Blend Off

        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            // Nine samplers (see SAMPLERS above): one per layer slot, the alpha array's. The height textures have none of
            // their own: height texture k is sampled with sampler_WmvLayerk.
            TEXTURE2D(_WmvLayer0); SAMPLER(sampler_WmvLayer0);
            TEXTURE2D(_WmvLayer1); SAMPLER(sampler_WmvLayer1);
            TEXTURE2D(_WmvLayer2); SAMPLER(sampler_WmvLayer2);
            TEXTURE2D(_WmvLayer3); SAMPLER(sampler_WmvLayer3);
            TEXTURE2D(_WmvLayer4); SAMPLER(sampler_WmvLayer4);
            TEXTURE2D(_WmvLayer5); SAMPLER(sampler_WmvLayer5);
            TEXTURE2D(_WmvLayer6); SAMPLER(sampler_WmvLayer6);
            TEXTURE2D(_WmvLayer7); SAMPLER(sampler_WmvLayer7);
            TEXTURE2D(_WmvHeight0);
            TEXTURE2D(_WmvHeight1);
            TEXTURE2D(_WmvHeight2);
            TEXTURE2D(_WmvHeight3);
            TEXTURE2D(_WmvHeight4);
            TEXTURE2D(_WmvHeight5);
            TEXTURE2D(_WmvHeight6);
            TEXTURE2D(_WmvHeight7);
            TEXTURE2D_ARRAY(_WmvAlpha); SAMPLER(sampler_WmvAlpha);

            CBUFFER_START(UnityPerMaterial)
                float _WmvAlphaSlice;
                float _WmvAlphaSlice2;
                float _WmvLayerCount;
                float4 _WmvRepeats;
                float4 _WmvRepeats47;
                float4 _WmvPhaseU;
                float4 _WmvPhaseU47;
                float4 _WmvPhaseV;
                float4 _WmvPhaseV47;
                float4 _WmvMissing;
                float4 _WmvMissing47;
                float _WmvUseHeights;
                float4 _WmvHasHeight;
                float4 _WmvHasHeight47;
                float4 _WmvHeightScale;
                float4 _WmvHeightScale47;
                float4 _WmvHeightOffset;
                float4 _WmvHeightOffset47;
                float _WmvTexturedView;
                float _WmvHatch;
                float _WmvVertexColors;
                float4 _WmvStateColour;
                float _WmvTexelMip;
                float _WmvLinearOut;
                float _Cull;
            CBUFFER_END

            // Globals, never properties (a property of the same name would pin them): the per-fragment display encode
            // (WmvMain), and the validation switches the dumps and the self-test set (WmvTerrainComposite).
            float _WmvShaderEncode;
            float _WmvTerrainValidate;
            float _WmvTerrainUnclamped;

            // Stage 10: the outdoor light (WmvWorldLight's globals; see THE OUTDOOR LIGHT above).
            float _WmvWLOn;
            float4 _WmvWLTerrain;          // x: the terrain lit (not -wmvTerrainLight=off); y: the flat variant
            float4 _WmvWLLightDir;         // towards the light, Unity world
            float4 _WmvWLAmbient;
            float4 _WmvWLHorizonAmbient;
            float4 _WmvWLGroundAmbient;
            float4 _WmvWLDirect;

            // The client's terrain program's constants (PROVEN_BYTES, 2977247; MEASURED in step 1) and the terms Stage 10
            // holds neutral (TerrainLight.cs): the hemisphere wrap, the flat class's shadow mix, the albedo's vertex-colour
            // conversion (the mesh's byte / 127 to the client's 2 x byte / 255).
            #define WMV_WRAP_BASE       0.7
            #define WMV_WRAP_SCALE      0.4
            #define WMV_WRAP_HALF       0.5
            #define WMV_FLAT_SHADOW_BASE  0.7
            #define WMV_FLAT_SHADOW_SCALE 0.3
            #define WMV_MCCV_TO_CLIENT  (254.0 / 255.0)
            #define WMV_ATTENUATION     1.0
            #define WMV_SHADOW          1.0
            #define WMV_LOCAL           0.0
            #define WMV_SPECULAR        0.0
            #define WMV_SECOND_LIGHT    0.0

            // Wmv/Terrain's diagnostic light: toward the key, north-west and 45 degrees up (Unity x east, y up, z north).
            #define TERRAIN_KEY     float3(-0.5, 0.70710678, 0.5)
            #define TERRAIN_AMBIENT 0.30
            #define TERRAIN_DIFFUSE 0.70

            struct Attributes
            {
                float4 positionOS : POSITION;
                float3 normalOS   : NORMAL;
                float2 uv0        : TEXCOORD0;
                float3 lightNormalOS : TEXCOORD1;   // Stage 10: the MCNR normal (WmvTerrainBuilder's own stream)
                float4 color      : COLOR;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float2 uv         : TEXCOORD0;   // cells
                float3 normalWS   : TEXCOORD1;
                float3 color      : TEXCOORD2;
                float3 lightNormalWS : TEXCOORD3;   // Stage 10: the MCNR normal, not normalised (nor is it in the pixel)
            };

            float3 WmvAuthoredToLinear(float3 c)
            {
                float3 lo = c * (1.0 / 12.92);
                float3 hi = pow(max((c + 0.055) * (1.0 / 1.055), 0.0), 2.4);
                return lerp(lo, hi, step(0.04045, c));
            }

            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionCS = TransformObjectToHClip(v.positionOS.xyz);
                o.uv = v.uv0;
                o.normalWS = TransformObjectToWorldNormal(v.normalOS);
                o.color = _WmvVertexColors > 0.5 ? v.color.rgb : float3(1.0, 1.0, 1.0);
                o.lightNormalWS = TransformObjectToWorldDir(v.lightNormalOS, false);
                return o;
            }

            // Stage 10: the client's terrain light on an albedo (see THE OUTDOOR LIGHT above; TerrainOutdoorLight.Shade).
            float3 WmvOutdoorLight(float3 albedo, float3 normalWS)
            {
                float3 n = normalWS;               // as the client: the interpolated MCNR / 127 vector, not normalised
                float nl = dot(n, _WmvWLLightDir.xyz);
                float lit = max(nl, 0.0);
                if (_WmvWLTerrain.y > 0.5)
                    return albedo * saturate(WMV_LOCAL + _WmvWLDirect.rgb * lit + _WmvWLAmbient.rgb) *
                           (WMV_FLAT_SHADOW_BASE + WMV_FLAT_SHADOW_SCALE * WMV_SHADOW);
                float nu = n.y;
                float3 horizon = any(_WmvWLHorizonAmbient.rgb != 0.0) ? _WmvWLHorizonAmbient.rgb : _WmvWLAmbient.rgb;
                float3 ground = any(_WmvWLGroundAmbient.rgb != 0.0) ? _WmvWLGroundAmbient.rgb : _WmvWLAmbient.rgb;
                float3 h = nu >= 0.0 ? lerp(horizon, _WmvWLAmbient.rgb, nu) : lerp(horizon, ground, -nu);
                float3 a = h * (WMV_WRAP_BASE + WMV_WRAP_SCALE * (WMV_WRAP_HALF + WMV_WRAP_HALF * nl));
                float3 d = _WmvWLDirect.rgb * lit * WMV_ATTENUATION * WMV_SHADOW;
                float3 x = albedo * (a + d);
                return sqrt(x * x + albedo * albedo * WMV_LOCAL) + WMV_SPECULAR + WMV_SECOND_LIGHT;
            }

            // TerrainCompositor.TexelIndex: floor(s * size) wrapped into 0..size-1.
            int TexelIndex(float s, float size)
            {
                float p = floor(s * size);
                p = p - size * floor(p / size);
                return (int)p;
            }

            // TerrainCompositor.AlphaAt: texel i at (i + 0.5) / 64, four Loads, clamp, x then y (slice `slice`).
            float4 AlphaLoad(float2 uv, int slice)
            {
                float2 x = (uv / 8.0) * 64.0 - 0.5;
                float2 f0 = floor(x);
                float2 fr = x - f0;
                int2 i0 = clamp((int2)f0, 0, 63);
                int2 i1 = clamp((int2)f0 + 1, 0, 63);
                float4 a00 = LOAD_TEXTURE2D_ARRAY(_WmvAlpha, int2(i0.x, i0.y), slice);
                float4 a10 = LOAD_TEXTURE2D_ARRAY(_WmvAlpha, int2(i1.x, i0.y), slice);
                float4 a01 = LOAD_TEXTURE2D_ARRAY(_WmvAlpha, int2(i0.x, i1.y), slice);
                float4 a11 = LOAD_TEXTURE2D_ARRAY(_WmvAlpha, int2(i1.x, i1.y), slice);
                float4 top = a00 + (a10 - a00) * fr.x;
                float4 bottom = a01 + (a11 - a01) * fr.x;
                return top + (bottom - top) * fr.y;
            }

            // The four alpha channels of slice `sliceValue` (a material float) at uv.
            float4 AlphaAt(float2 uv, float sliceValue)
            {
                int slice = (int)(sliceValue + 0.5);
                float4 a;
                if (_WmvTerrainValidate > 0.5)
                    a = AlphaLoad(uv, slice);
                else
                    a = SAMPLE_TEXTURE2D_ARRAY(_WmvAlpha, sampler_WmvAlpha, uv / 8.0, slice);
                return a;
            }

            // TerrainCompositor.CheckerMagenta: magenta where floor(8u) + floor(8v) is even.
            float3 Checker(float2 uv)
            {
                float c = floor(uv.x * 8.0) + floor(uv.y * 8.0);
                return frac(c * 0.5) < 0.25 ? float3(1.0, 0.0, 1.0) : float3(0.0, 0.0, 0.0);
            }

            // TerrainCompositor.LayerTexel: the layer's texel (or the checker) at TerrainCompositor.LayerCoordinate,
            // s = uv * repeats / 8 + phase.
            #define WMV_LAYER(TEX, SAMP, REPEATS, PHASE, MISSING, UV, OUT)                            \
                if (MISSING > 0.5)                                                                     \
                    OUT = Checker(UV);                                                                 \
                else                                                                                   \
                {                                                                                      \
                    float2 s = UV * (REPEATS) / 8.0 + (PHASE);                                         \
                    if (_WmvTerrainValidate > 0.5)                                                     \
                    {                                                                                  \
                        float w, h;                                                                    \
                        TEX.GetDimensions(w, h);                                                       \
                        OUT = LOAD_TEXTURE2D(TEX, int2(TexelIndex(s.x, w), TexelIndex(s.y, h))).rgb;   \
                    }                                                                                  \
                    else                                                                               \
                        OUT = SAMPLE_TEXTURE2D(TEX, SAMP, s).rgb;                                      \
                }

            // TerrainCompositor.HeightAt: 1 without MPHD 0x80; otherwise h * scale + offset, h = the height texture's
            // ALPHA at the layer's coordinate (its repeats and phase; by index in the height texture's own size in
            // validation sampling, with the LAYER slot's sampler in production), 0 without one.
            #define WMV_HEIGHT(TEX, SAMP, REPEATS, PHASE, HAS, SCALE, OFFSET, UV, OUT)                    \
                {                                                                                          \
                    float hv = 0.0;                                                                        \
                    if (HAS > 0.5)                                                                         \
                    {                                                                                      \
                        float2 s = UV * (REPEATS) / 8.0 + (PHASE);                                         \
                        if (_WmvTerrainValidate > 0.5)                                                     \
                        {                                                                                  \
                            float w, h;                                                                    \
                            TEX.GetDimensions(w, h);                                                       \
                            hv = LOAD_TEXTURE2D(TEX, int2(TexelIndex(s.x, w), TexelIndex(s.y, h))).a;      \
                        }                                                                                  \
                        else                                                                               \
                            hv = SAMPLE_TEXTURE2D(TEX, SAMP, s).a;                                         \
                    }                                                                                      \
                    OUT = _WmvUseHeights > 0.5 ? hv * (SCALE) + (OFFSET) : 1.0;                            \
                }

            float Lambert(float3 n)
            {
                return TERRAIN_AMBIENT + TERRAIN_DIFFUSE * saturate(dot(normalize(n), TERRAIN_KEY));
            }

            float4 frag(Varyings i) : SV_Target
            {
                float2 uv = i.uv;
                float3 colour;
                if (_WmvTexturedView > 3.5)
                {
                    // Texel copy: one repeat of layer 0 over the chunk, by Load at mip _WmvTexelMip, alpha included.
                    float w, h, levels;
                    int mip = (int)(_WmvTexelMip + 0.5);
                    _WmvLayer0.GetDimensions(mip, w, h, levels);
                    int2 at = int2(clamp(floor(uv.x / 8.0 * w), 0.0, w - 1.0), clamp(floor(uv.y / 8.0 * h), 0.0, h - 1.0));
                    return LOAD_TEXTURE2D_LOD(_WmvLayer0, at, mip);
                }
                if (_WmvTexturedView > 2.5)
                    colour = _WmvStateColour.rgb * Lambert(i.normalWS);
                else
                {
                    int n = (int)(_WmvLayerCount + 0.5);
                    bool plain = _WmvHatch > 0.5;
                    float4 a = n > 1 ? AlphaAt(uv, _WmvAlphaSlice) : float4(0.0, 0.0, 0.0, 0.0);
                    float4 b = n > 5 ? AlphaAt(uv, _WmvAlphaSlice2) : float4(0.0, 0.0, 0.0, 0.0);
                    // Channels past the chunk's layers are 0 in the slices; the sum runs in TerrainCompositor's order.
                    float sum = 0.0;
                    if (n > 1) sum += a.r;
                    if (n > 2) sum += a.g;
                    if (n > 3) sum += a.b;
                    if (n > 4) sum += a.a;
                    if (n > 5) sum += b.r;
                    if (n > 6) sum += b.g;
                    if (n > 7) sum += b.b;
                    float raw[8];
                    raw[0] = 1.0 - sum;
                    raw[1] = a.r;
                    raw[2] = a.g;
                    raw[3] = a.b;
                    raw[4] = a.a;
                    raw[5] = b.r;
                    raw[6] = b.g;
                    raw[7] = b.b;
                    float3 layer[8];
                    float hk[8];
                    [unroll] for (int z = 0; z < 8; z++)
                    {
                        layer[z] = float3(0.0, 0.0, 0.0);
                        hk[z] = 1.0;
                    }
                    WMV_LAYER(_WmvLayer0, sampler_WmvLayer0, _WmvRepeats.x, float2(_WmvPhaseU.x, _WmvPhaseV.x), _WmvMissing.x, uv, layer[0]);
                    if (!plain)
                        WMV_HEIGHT(_WmvHeight0, sampler_WmvLayer0, _WmvRepeats.x, float2(_WmvPhaseU.x, _WmvPhaseV.x), _WmvHasHeight.x,
                                   _WmvHeightScale.x, _WmvHeightOffset.x, uv, hk[0]);
                    if (n > 1)
                    {
                        WMV_LAYER(_WmvLayer1, sampler_WmvLayer1, _WmvRepeats.y, float2(_WmvPhaseU.y, _WmvPhaseV.y), _WmvMissing.y, uv, layer[1]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight1, sampler_WmvLayer1, _WmvRepeats.y, float2(_WmvPhaseU.y, _WmvPhaseV.y), _WmvHasHeight.y,
                                       _WmvHeightScale.y, _WmvHeightOffset.y, uv, hk[1]);
                    }
                    if (n > 2)
                    {
                        WMV_LAYER(_WmvLayer2, sampler_WmvLayer2, _WmvRepeats.z, float2(_WmvPhaseU.z, _WmvPhaseV.z), _WmvMissing.z, uv, layer[2]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight2, sampler_WmvLayer2, _WmvRepeats.z, float2(_WmvPhaseU.z, _WmvPhaseV.z), _WmvHasHeight.z,
                                       _WmvHeightScale.z, _WmvHeightOffset.z, uv, hk[2]);
                    }
                    if (n > 3)
                    {
                        WMV_LAYER(_WmvLayer3, sampler_WmvLayer3, _WmvRepeats.w, float2(_WmvPhaseU.w, _WmvPhaseV.w), _WmvMissing.w, uv, layer[3]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight3, sampler_WmvLayer3, _WmvRepeats.w, float2(_WmvPhaseU.w, _WmvPhaseV.w), _WmvHasHeight.w,
                                       _WmvHeightScale.w, _WmvHeightOffset.w, uv, hk[3]);
                    }
                    if (n > 4)
                    {
                        WMV_LAYER(_WmvLayer4, sampler_WmvLayer4, _WmvRepeats47.x, float2(_WmvPhaseU47.x, _WmvPhaseV47.x), _WmvMissing47.x, uv, layer[4]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight4, sampler_WmvLayer4, _WmvRepeats47.x, float2(_WmvPhaseU47.x, _WmvPhaseV47.x), _WmvHasHeight47.x,
                                       _WmvHeightScale47.x, _WmvHeightOffset47.x, uv, hk[4]);
                    }
                    if (n > 5)
                    {
                        WMV_LAYER(_WmvLayer5, sampler_WmvLayer5, _WmvRepeats47.y, float2(_WmvPhaseU47.y, _WmvPhaseV47.y), _WmvMissing47.y, uv, layer[5]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight5, sampler_WmvLayer5, _WmvRepeats47.y, float2(_WmvPhaseU47.y, _WmvPhaseV47.y), _WmvHasHeight47.y,
                                       _WmvHeightScale47.y, _WmvHeightOffset47.y, uv, hk[5]);
                    }
                    if (n > 6)
                    {
                        WMV_LAYER(_WmvLayer6, sampler_WmvLayer6, _WmvRepeats47.z, float2(_WmvPhaseU47.z, _WmvPhaseV47.z), _WmvMissing47.z, uv, layer[6]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight6, sampler_WmvLayer6, _WmvRepeats47.z, float2(_WmvPhaseU47.z, _WmvPhaseV47.z), _WmvHasHeight47.z,
                                       _WmvHeightScale47.z, _WmvHeightOffset47.z, uv, hk[6]);
                    }
                    if (n > 7)
                    {
                        WMV_LAYER(_WmvLayer7, sampler_WmvLayer7, _WmvRepeats47.w, float2(_WmvPhaseU47.w, _WmvPhaseV47.w), _WmvMissing47.w, uv, layer[7]);
                        if (!plain)
                            WMV_HEIGHT(_WmvHeight7, sampler_WmvLayer7, _WmvRepeats47.w, float2(_WmvPhaseU47.w, _WmvPhaseV47.w), _WmvHasHeight47.w,
                                       _WmvHeightScale47.w, _WmvHeightOffset47.w, uv, hk[7]);
                    }
                    // TerrainCompositor.HeightMixWeights, in its order of operations (or plain weights for the diagnostic).
                    float weight[8];
                    if (plain)
                    {
                        [unroll] for (int k = 0; k < 8; k++)
                            weight[k] = raw[k];
                    }
                    else
                    {
                        float p[8];
                        [unroll] for (int k = 0; k < 8; k++)
                            p[k] = max(0.0, raw[k]) * hk[k];
                        float m = p[0];
                        [unroll] for (int k1 = 1; k1 < 8; k1++)
                            if (k1 < n) m = max(m, p[k1]);
                        float S = 0.0;
                        [unroll] for (int k2 = 0; k2 < 8; k2++)
                            if (k2 < n)
                            {
                                p[k2] = p[k2] * (1.0 - saturate(m - p[k2]));
                                S += p[k2];
                            }
                        [unroll] for (int k3 = 0; k3 < 8; k3++)
                            weight[k3] = S > 0.0 ? p[k3] / S : max(0.0, raw[k3]);
                    }
                    colour = weight[0] * layer[0];
                    [unroll] for (int k4 = 1; k4 < 8; k4++)
                        if (k4 < n) colour += weight[k4] * layer[k4];
                    float3 composite = colour;
                    colour *= i.color;
                    if (_WmvTexturedView > 0.5 && _WmvTexturedView < 1.5)
                        colour *= Lambert(i.normalWS);
                    if (plain)
                    {
                        // A diagonal hatch, two lines per cell: plain weights, never the composite.
                        float hatch = frac((uv.x + uv.y) * 2.0);
                        if (hatch < 0.14)
                            colour = lerp(colour, float3(0.08, 0.08, 0.09), 0.65);
                    }
                    // Stage 10: the outdoor light (the world view's main camera only; see THE OUTDOOR LIGHT above). A real
                    // branch on uniforms (no derivative inside): with the light off the pixel runs Stage 09's instructions only.
                    [branch]
                    if (_WmvWLOn > 0.5 && _WmvWLTerrain.x > 0.5 && _WmvTexturedView < 0.5 && !plain)
                        colour = WmvOutdoorLight(composite * (_WmvVertexColors > 0.5 ? i.color * WMV_MCCV_TO_CLIENT : float3(1.0, 1.0, 1.0)),
                                                 i.lightNormalWS);
                }
                if (_WmvTerrainUnclamped < 0.5)
                    colour = saturate(colour);
                if (_WmvShaderEncode > 0.5 || _WmvLinearOut > 0.5)
                    colour = WmvAuthoredToLinear(colour);
                return float4(colour, 1.0);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
