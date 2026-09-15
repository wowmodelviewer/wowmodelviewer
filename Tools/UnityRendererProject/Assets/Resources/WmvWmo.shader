// WmvWmo.shader -- the world-model (WMO) material shader.
//
// WHY A SEPARATE SHADER. WmvOpaque.shader is the M2 combiner shader: its unit/UV plumbing, combiner modes,
// alpha modes and luminous lobes are M2 semantics, and a map-object material routed through them could
// only ever be an M2 material in disguise. The client draws map objects with its own uber shader, whose
// per-id cases (Wow/WmoMaterialSemantics.cs holds the table and its evidence) share nothing with the M2
// combiners but the idea of textures. This file is that path: a sampler REGISTER per client register,
// each fed from one MOMT texture slot on one mesh UV channel, and a pixel PERMUTATION per client case.
//
// WHAT IS SHARED WITH THE M2 PATH, AND WHY IT IS COPIED RATHER THAN INCLUDED. The preview light rig --
// the three-band ambient, the key, the cast and contact shadows, the authored-domain encode -- is the
// user's scene light, not WoW's, and a world model must be lit by exactly the same numbers as a model.
// The blocks between the "COPIED FROM WmvOpaque.shader" markers are the code of WmvOpaque.shader
// (its explanatory comments left behind: read them there), copied instead of moved into an include so
// the M2 shader, and therefore every M2 frame, is byte-for-byte untouched by the world-model work. A
// change to the rig must be made in both files; the WMO capture of a plain diffuse building against
// the same building drawn before this shader existed is the check that they still agree.
//
// EVERYTHING IS UNIFORM-DRIVEN, as in WmvOpaque.shader: a player build strips shader variants, so the
// permutation, the alpha test and the render state are floats and properties, never keywords.
//
// PERMUTATIONS (_WmoPermutation; values fixed by Wow.WmoPermutation):
//   0  PROVISIONAL baseline -- the archived static-stage drawing: t0.rgb, optional 128/255 key on t0.a
//   1  client pixel case 0 (and 16, byte-identical): diffuse = t0.rgb, caseAlpha = t0.a
//   2  client pixel case 23 without its env emissive and without the MOC2 byte-3 lerp: layers t1..t4,
//      height alphas in registers 5..8 (the client's t17..t20), MOC2 weights, caseAlpha = 1
//   3  client pixel case 13: lerp(t1.rgb, t0.rgb, MOCV set-2 alpha), caseAlpha = 1
//   4  client pixel case 4: diffuse = t0.rgb, caseAlpha = 1
//   5  client pixel case 7 without its env emissive: the diffuse is case 13's lerp, caseAlpha = 1
//   6  client pixel case 5 without its env emissive: the diffuse is case 4's t0.rgb, caseAlpha = 1
// 0 and 1 share their arithmetic today, 4 differs from them only in its case alpha, and 5 / 6 draw exactly
// what 3 / 4 draw; they are kept apart so the log and any later divergence (a provisional material must
// never silently inherit a resolved rule, and an env stage must be able to add the emissive of 5 and 6
// without touching 3 and 4) stay explicit.
//
// Blend 0/1 draw with blending off (One/Zero for colour and alpha) and depth writes on; the alpha test
// is a uniform so one variant serves both. The four-factor Blend is wired now so a later stage that
// establishes separate alpha factors changes properties, not this file.

Shader "WMV/Map Object"
{
    Properties
    {
        // Sampler registers, numbered like the client's t-registers of the permutation. Unbound ones read
        // white, which is also what the archived baseline drew for an empty slot +0x0C.
        _WmoTex0 ("Register t0", 2D) = "white" {}
        _WmoTex1 ("Register t1", 2D) = "white" {}
        _WmoTex2 ("Register t2", 2D) = "white" {}
        _WmoTex3 ("Register t3", 2D) = "white" {}
        _WmoTex4 ("Register t4", 2D) = "white" {}
        _WmoTex5 ("Register t5", 2D) = "white" {}
        _WmoTex6 ("Register t6", 2D) = "white" {}
        _WmoTex7 ("Register t7", 2D) = "white" {}
        _WmoTex8 ("Register t8", 2D) = "white" {}
        // The mesh UV channel each register samples: channel k carries MOTV set k + 1 (V already flipped
        // by the mesh builder). Only channels 0..3 exist.
        _WmoUv0 ("t0 UV channel", Float) = 0
        _WmoUv1 ("t1 UV channel", Float) = 0
        _WmoUv2 ("t2 UV channel", Float) = 0
        _WmoUv3 ("t3 UV channel", Float) = 0
        _WmoUv4 ("t4 UV channel", Float) = 0
        _WmoUv5 ("t5 UV channel", Float) = 0
        _WmoUv6 ("t6 UV channel", Float) = 0
        _WmoUv7 ("t7 UV channel", Float) = 0
        _WmoUv8 ("t8 UV channel", Float) = 0

        _WmoPermutation ("Pixel permutation", Float) = 0
        // Permutation 2: 1 per layer whose slot names a texture, 0 for an empty one (its weight is forced
        // to 0 so the register's white default never stands in for a texture).
        _WmoLayerMask ("Four-layer mask", Vector) = (1,1,1,1)
        // 1 on a material whose F_UNLIT flag is honoured: the preview rig's light is not applied.
        _WmoLightBypass ("Preview light bypass (F_UNLIT)", Float) = 0
        // DIAGNOSTIC ONLY (-wmvWmoVertexColour): multiply MOCV set 1 into the albedo. Never set otherwise.
        _WmoVertexColourDiag ("Diagnostic vertex colour", Float) = 0
        // DIAGNOSTIC ONLY (-wmvWmoView, -wmvWmoOnlyMaterials): 0 draws normally. 1 flat plan colour,
        // 2 stored MOC2 weights, 3 effective four-layer weights, 4 the two-layer factor va, 5 the combiner
        // diffuse, 6 register t0 as sampled, 7 register t1 as sampled (two-layer) -- all unlit;
        // _WmoDiagHide 1 discards the material in every camera. Never set otherwise.
        _WmoDiagView ("Diagnostic view", Float) = 0
        _WmoDiagColour ("Diagnostic plan colour", Color) = (1,1,1,1)
        _WmoDiagHide ("Diagnostic hide", Float) = 0

        _AlphaTest ("Alpha test", Float) = 0
        _Cutoff ("Alpha cutoff", Range(0,1)) = 0.5019608
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2               // Back
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend", Float) = 1     // One
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend", Float) = 0     // Zero
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlendA ("Src blend alpha", Float) = 1
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlendA ("Dst blend alpha", Float) = 0
        [Toggle] _ZWrite ("ZWrite", Float) = 1
    }

    SubShader
    {
        // No LightMode tag, as WmvOpaque.shader: drawn as a plain pass in the built-in pipeline and through
        // SRPDefaultUnlit in URP. The builder overrides RenderType and the queue per material.
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull [_Cull]
        Blend [_SrcBlend] [_DstBlend], [_SrcBlendA] [_DstBlendA]
        ZWrite [_ZWrite]

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            // The contact march dithers on SV_POSITION (shader model 3.0), exactly as in WmvOpaque.shader.
            #pragma target 3.0
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                // Every MOTV set the mesh builder uploads. A group with fewer sets reads zeros in the
                // missing channels, and no established permutation samples a channel its data lacks.
                float2 uv0    : TEXCOORD0;
                float2 uv1    : TEXCOORD1;
                float2 uv2    : TEXCOORD2;
                float2 uv3    : TEXCOORD3;
                // MOC2 as (byte 2, byte 1, byte 0, byte 3) / 255, uploaded only for groups a four-layer
                // material draws in; any other group reads zeros, which no other permutation uses.
                float4 moc2   : TEXCOORD4;
                // The alpha of MOCV set 2 as (alpha / 255, 0), uploaded only for groups a two-layer
                // material (permutation 3 or 5) draws in; any other group reads zeros, which no other
                // permutation uses.
                float2 set2   : TEXCOORD5;
                fixed4 color  : COLOR;       // read only by the _WmoVertexColourDiag diagnostic
            };

            struct v2f
            {
                float4 pos    : SV_POSITION;
                float4 uv01   : TEXCOORD0;   // mesh UV channels 0 (xy) and 1 (zw)
                float4 uv23   : TEXCOORD1;   // mesh UV channels 2 (xy) and 3 (zw)
                float3 normal : TEXCOORD2;   // world space
                half3  viewN  : TEXCOORD3;   // view space
                float3 wpos   : TEXCOORD4;   // world position, for the shadow lookups
                fixed4 vcol   : TEXCOORD5;   // diagnostic only
                float4 moc2   : TEXCOORD6;   // four-layer weights, interpolated like the client's TEXCOORD4
                float  va     : TEXCOORD7;   // two-layer factor, interpolated like the client's second colour
            };

            // ---- COPIED FROM WmvOpaque.shader: preview rig constants -------------------------------
            #define AMB_ZENITH  0.60h     // placeholder magnitude, graded -- see WmvOpaque.shader
            #define AMB_HORIZON 0.90h     // placeholder magnitude, graded -- see WmvOpaque.shader
            #define AMB_GROUND  1.05h     // placeholder magnitude, graded -- see WmvOpaque.shader
            #define LIGHT_LEVEL 0.59h     // derived: 1.25 - AMB_ZENITH * 1.1
            #define AMB_BASE    0.7h      // RETAIL-DERIVED literal
            #define AMB_WRAP    0.4h      // RETAIL-DERIVED literal
            #define KEY_DIR     half3(0.081, 0.858, 0.507)
            #define FILL_DIR    half3(0.059, 0.998, 0.032)
            #define SHADOW_STRENGTH 1.0h
            #define SHADOW_SOFT     4.0h
            #define CONTACT_STRENGTH 0.4h
            #define CONTACT_RANGE    0.36666667
            #define CONTACT_SOFTNESS 0.25
            #define CONTACT_TAPS     8
            // ---- END COPY ---------------------------------------------------------------------------

            sampler2D _WmoTex0, _WmoTex1, _WmoTex2, _WmoTex3, _WmoTex4, _WmoTex5, _WmoTex6, _WmoTex7, _WmoTex8;
            float _WmoUv0, _WmoUv1, _WmoUv2, _WmoUv3, _WmoUv4, _WmoUv5, _WmoUv6, _WmoUv7, _WmoUv8;
            float _WmoPermutation, _WmoLightBypass, _WmoVertexColourDiag, _AlphaTest;
            float4 _WmoLayerMask;
            float _WmoDiagView, _WmoDiagHide;
            fixed4 _WmoDiagColour;
            fixed _Cutoff;

            // Preview globals, set by WmvMain / WmvShadowRig. GLOBALS, never properties: a property of the
            // same name would shadow the global and pin its value (see WmvParticle.shader).
            float _WmvShaderEncode;
            float _WmvRig;
            float _WmvFlatAlbedo;

            // ---- COPIED FROM WmvOpaque.shader: cast shadows, contact shadows, authored-domain encode --
            float     _WmvShadowValid;
            float4x4  _WmvShadowMatrix;
            sampler2D_float _WmvShadowMap;
            float     _WmvShadowTexel;        // 1 / map size
            float     _WmvShadowDepthBias;    // in [0,1] depth units
            float     _WmvShadowNormalBias;   // world units, along the surface normal

            half WmvShadowFactor(float3 wpos, half3 nrmWorld, half soft)
            {
                if (_WmvShadowValid < 0.5h)
                    return 1.0h;

                float4 sp = mul(_WmvShadowMatrix,
                                float4(wpos + nrmWorld * _WmvShadowNormalBias, 1.0));
                float2 uv = sp.xy * 0.5 + 0.5;
                if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
                    return 1.0h;              // outside the map: nothing recorded, so lit

                float lit = 0.0;
                float r = _WmvShadowTexel * soft;
                [unroll]
                for (int y = -1; y <= 1; y++)
                    [unroll]
                    for (int x = -1; x <= 1; x++)
                    {
                        float stored = tex2D(_WmvShadowMap, uv + float2(x, y) * r).r;
            #if UNITY_REVERSED_Z
                        lit += (sp.z >= stored - _WmvShadowDepthBias) ? 1.0 : 0.0;
            #else
                        lit += ((sp.z * 0.5 + 0.5) <= stored + _WmvShadowDepthBias) ? 1.0 : 0.0;
            #endif
                    }
                return (half)(lit / 9.0);
            }

            float     _WmvContactValid;
            float4x4  _WmvViewDepthMatrix;    // world -> the view-depth camera's clip space
            sampler2D_float _WmvViewDepth;
            float4    _WmvKeyDirWorld;        // toward the light; w unused
            float4    _WmvFillDirWorld;       // the sky fill, same handling; w unused
            float     _WmvModelRadius;        // world units; scales the march to the model
            float     _WmvContactEps;         // self-hit guard, WORLD UNITS
            float     _WmvContactThick;       // occluder thickness assumption, WORLD UNITS
            float4    _WmvViewDepthParams;    // (near, far, far - near, near * far), world units

            float WmvLinearViewDepth(float d)
            {
            #if UNITY_REVERSED_Z
                return _WmvViewDepthParams.w
                     / (_WmvViewDepthParams.z * d + _WmvViewDepthParams.x);
            #else
                return _WmvViewDepthParams.w
                     / (_WmvViewDepthParams.y - _WmvViewDepthParams.z * d);
            #endif
            }

            float WmvStepPhase(float2 pix)
            {
                return frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))));
            }

            half WmvContactFactor(float3 wpos, half3 nrmWorld, float range, float2 pix)
            {
                if (_WmvContactValid < 0.5h)
                    return 1.0h;

                float3 dir = _WmvKeyDirWorld.xyz;

                float jitter = WmvStepPhase(pix);

                const int STEPS = 32;                // samples along the reach
                const int TAPS = CONTACT_TAPS;

                float3 camF = float3(_WmvViewDepthMatrix[3][0], _WmvViewDepthMatrix[3][1],
                                     _WmvViewDepthMatrix[3][2]);
                float3 bLat = cross(dir, camF);
                float bLen = dot(bLat, bLat);
                bLat = (bLen > 1e-8) ? bLat * rsqrt(bLen) : float3(1.0, 0.0, 0.0);
                float4 mbLat = mul(_WmvViewDepthMatrix, float4(bLat, 0.0));

                float occ = 0.0;
                [loop]
                for (int s = 0; s < STEPS; s++)
                {
                    float t = (s + jitter) / STEPS;
                    float3 pw = wpos + nrmWorld * ((0.045 + 0.10 * t) * range)
                              + dir * (max(t, 0.015) * range);
                    float4 cp = mul(_WmvViewDepthMatrix, float4(pw, 1.0));
                    if (cp.w <= _WmvViewDepthParams.x)
                        break;                       // in front of the near plane: nothing there

                    float rad = CONTACT_SOFTNESS * t * range;
                    float cov = 0.0;                 // occluded fraction of the cone's width
                    float covW = 0.0;                // how much of it could be sampled at all
                    [unroll]
                    for (int k = 0; k < TAPS; k++)
                    {
                        float off = rad * (2.0 * (k + 0.5) / TAPS - 1.0);
                        float4 cpk = cp + mbLat * off;   // cpk.w == cp.w, by construction
                        float2 uvk = cpk.xy / cpk.w * 0.5 + 0.5;
                        float2 edge = saturate(min(uvk, 1.0 - uvk) * 20.0);
                        float border = edge.x * edge.y;
                        if (border <= 0.0)
                            continue;                // this tap saw nothing; the others may
                        float stored = tex2D(_WmvViewDepth, uvk).r;
                        float infront = cpk.w - WmvLinearViewDepth(stored);
                        cov += border
                             * smoothstep(_WmvContactEps * 0.5, _WmvContactEps * 1.5, infront)
                             * (1.0 - smoothstep(_WmvContactThick * 0.6, _WmvContactThick,
                                                 infront));
                        covW += border;
                    }
                    if (covW <= 0.0)
                        break;                       // the whole cone is off-screen

                    float tFade = (s + 0.5) / STEPS;
                    float w = (cov / covW) * (1.0 - tFade) * (covW / TAPS);
                    occ = max(occ, w);
                }
                return (half)(1.0 - occ);
            }

            half3 WmvAuthoredToLinear(half3 c)
            {
                half3 lo = c * (1.0h / 12.92h);
                half3 hi = pow(max((c + 0.055h) * (1.0h / 1.055h), 0.0h), 2.4h);
                return lerp(lo, hi, step(0.04045h, c));
            }
            // ---- END COPY ---------------------------------------------------------------------------

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv01 = float4(v.uv0, v.uv1);
                o.uv23 = float4(v.uv2, v.uv3);
                o.normal = UnityObjectToWorldNormal(v.normal);
                o.wpos = mul(unity_ObjectToWorld, v.vertex).xyz;
                o.vcol = v.color;
                o.moc2 = v.moc2;
                o.va = v.set2.x;
                // The view-space normal the rig's fallback key reads, computed as WmvOpaque.shader does.
                o.viewN = normalize(mul((float3x3)UNITY_MATRIX_IT_MV, v.normal));
                return o;
            }

            // A register's coordinate: the mesh UV channel the material plan names. A selection, not a
            // blend, so the value reaching the sampler is the stored coordinate bit for bit.
            float2 WmoUv(float channel, v2f i)
            {
                return channel < 0.5 ? i.uv01.xy
                     : channel < 1.5 ? i.uv01.zw
                     : channel < 2.5 ? i.uv23.xy
                     :                 i.uv23.zw;
            }

            // The same selection for a pair of packed values (a coordinate or one of its derivatives).
            float2 WmoPick(float channel, float4 a01, float4 a23)
            {
                return channel < 0.5 ? a01.xy
                     : channel < 1.5 ? a01.zw
                     : channel < 2.5 ? a23.xy
                     :                 a23.zw;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                // DIAGNOSTIC ONLY (-wmvWmoOnlyMaterials): a hidden material draws in no camera at all.
                if (_WmoDiagHide > 0.5)
                    clip(-1.0);

                fixed4 t0 = tex2D(_WmoTex0, WmoUv(_WmoUv0, i));

                // THE PERMUTATION. Permutation 1 is the client's pixel case 0 (case 16 is the same bytes):
                // diffuse = t0.rgb, no emissive, and t0.a is the case alpha. Permutation 0, the provisional
                // baseline, draws the same arithmetic on whatever texture the plan bound to t0.
                fixed3 albedo = t0.rgb;
                fixed caseAlpha = t0.a;
                float4 layerW = 0.0;     // stored weights (w1..w4), kept for the diagnostic views
                float4 layerB = 0.0;     // effective weights after heights and sharpening
                fixed4 t1 = 0.0;         // the two-layer permutations' second register, kept for the views
                // Permutations 3 (case 13) and 5 (case 7's diffuse) draw the same two-layer lerp.
                bool twoLayer = abs(_WmoPermutation - 3.0) < 0.5 || abs(_WmoPermutation - 5.0) < 0.5;

                // Permutation 2, the client's pixel case 23. The derivatives are taken here, outside the
                // branch, because a branch that samples with implicit derivatives cannot be a real branch;
                // with explicit ones the other permutations never pay for the eight extra samples.
                float4 dx01 = ddx(i.uv01), dy01 = ddy(i.uv01), dx23 = ddx(i.uv23), dy23 = ddy(i.uv23);
                [branch]
                if (_WmoPermutation > 1.5 && _WmoPermutation < 2.5)
                {
                    // Layer k and its height map read the MOTV set the plan names for their registers
                    // (set k for both).
                    float4 l1 = tex2Dgrad(_WmoTex1, WmoPick(_WmoUv1, i.uv01, i.uv23), WmoPick(_WmoUv1, dx01, dx23), WmoPick(_WmoUv1, dy01, dy23));
                    float4 l2 = tex2Dgrad(_WmoTex2, WmoPick(_WmoUv2, i.uv01, i.uv23), WmoPick(_WmoUv2, dx01, dx23), WmoPick(_WmoUv2, dy01, dy23));
                    float4 l3 = tex2Dgrad(_WmoTex3, WmoPick(_WmoUv3, i.uv01, i.uv23), WmoPick(_WmoUv3, dx01, dx23), WmoPick(_WmoUv3, dy01, dy23));
                    float4 l4 = tex2Dgrad(_WmoTex4, WmoPick(_WmoUv4, i.uv01, i.uv23), WmoPick(_WmoUv4, dx01, dx23), WmoPick(_WmoUv4, dy01, dy23));
                    float h1 = tex2Dgrad(_WmoTex5, WmoPick(_WmoUv5, i.uv01, i.uv23), WmoPick(_WmoUv5, dx01, dx23), WmoPick(_WmoUv5, dy01, dy23)).a;
                    float h2 = tex2Dgrad(_WmoTex6, WmoPick(_WmoUv6, i.uv01, i.uv23), WmoPick(_WmoUv6, dx01, dx23), WmoPick(_WmoUv6, dy01, dy23)).a;
                    float h3 = tex2Dgrad(_WmoTex7, WmoPick(_WmoUv7, i.uv01, i.uv23), WmoPick(_WmoUv7, dx01, dx23), WmoPick(_WmoUv7, dy01, dy23)).a;
                    float h4 = tex2Dgrad(_WmoTex8, WmoPick(_WmoUv8, i.uv01, i.uv23), WmoPick(_WmoUv8, dx01, dx23), WmoPick(_WmoUv8, dy01, dy23)).a;

                    // The client's weights: MOC2 bytes 2, 1, 0 for layers 1..3 and the remainder for layer 4.
                    // The viewer's mask then zeroes an empty layer (client-equal where its stored weight is 0).
                    float3 s = i.moc2.xyz;
                    layerW = float4(s, 1.0 - saturate(s.x + s.y + s.z));
                    float4 aw = layerW * _WmoLayerMask * max(float4(h1, h2, h3, h4), 0.004);
                    // All four components enter the maximum, as in the client.
                    float m = max(max(max(aw.y, aw.x), aw.z), aw.w);
                    float4 b = (1.0 - saturate(m - aw)) * aw;
                    float total = b.x + b.y + b.z + b.w;
                    // The client's weights never all vanish (layer 4 takes the remainder); a masked layer
                    // set can, and then this draws black rather than dividing by zero. A floor on the
                    // divisor, not a ternary, so no NaN can be computed at all.
                    layerB = b / max(total, 1e-6);
                    float4 mixc = l1 * layerB.x + l2 * layerB.y + l3 * layerB.z + l4 * layerB.w;
                    // Diffuse = the weighted sum. Not applied: the client's lerp toward an exe-side colour
                    // by MOC2 byte 3 (U-23a) and the env emissive env.rgb * mix.rgb * mix.a (U-E2, U-E3).
                    albedo = mixc.rgb;
                    caseAlpha = 1.0;
                }

                // Permutation 3, the client's pixel case 13: layer 2 (+0x18) on the register's own UV
                // channel (MOTV set 2), lerped toward layer 1 (t0, +0x0C) by the interpolated MOCV set-2
                // alpha -- va 1 draws layer 1, va 0 layer 2, rgb only. Explicit derivatives for the same
                // reason as above: the sample sits inside a real branch.
                // Permutation 5, the client's pixel case 7, lerps the same registers by the same factor (rgba;
                // only its emissive reads the alpha). That emissive -- the lerped colour times its alpha times
                // an env map on a generated coordinate, added after light -- is NOT added: the generator
                // (U-G1) and the distance fade (U-E3) are exe-side, so its register is never bound.
                [branch]
                if (twoLayer)
                {
                    t1 = tex2Dgrad(_WmoTex1, WmoPick(_WmoUv1, i.uv01, i.uv23), WmoPick(_WmoUv1, dx01, dx23), WmoPick(_WmoUv1, dy01, dy23));
                    albedo = lerp(t1.rgb, t0.rgb, i.va);
                    caseAlpha = 1.0;
                }

                // Permutation 4, the client's pixel case 4: t0.rgb as permutation 1 draws it, but the case
                // alpha is the constant 1, so the texture's alpha can never reach the test. Permutation 6, the
                // client's pixel case 5, has the same diffuse and alpha; its emissive t0.rgb * t0.a * env is
                // NOT added (U-G1, U-E3), so t0's alpha (the reflectivity mask) is read by nothing here.
                // Permutation 5 already set its case alpha above.
                if (_WmoPermutation > 3.5)
                    caseAlpha = 1.0;

                // Blend 1 (and the provisional key): discard below the client's 128/255. The discard
                // happens in the ordinary pass, so the shadow and contact cameras, which render these same
                // materials, see the same silhouette.
                if (_AlphaTest > 0.5)
                    clip(caseAlpha - _Cutoff);

                // DIAGNOSTIC ONLY (-wmvWmoView): unlit views of the plan, of the four-layer weights (rgb =
                // layers 1..3, black = layer 4), of the two-layer factor va (grey level, permutations 3 and 5), of the combiner
                // diffuse and of registers t0 / t1 as sampled. Materials a view does not apply to draw dark
                // grey.
                if (_WmoDiagView > 0.5)
                {
                    bool fourLayer = _WmoPermutation > 1.5 && _WmoPermutation < 2.5;
                    fixed3 na = fixed3(0.1, 0.1, 0.1);
                    fixed3 v = _WmoDiagView < 1.5 ? _WmoDiagColour.rgb
                             : _WmoDiagView < 2.5 ? (fourLayer ? layerW.xyz : na)
                             : _WmoDiagView < 3.5 ? (fourLayer ? layerB.xyz : na)
                             : _WmoDiagView < 4.5 ? (twoLayer ? (fixed3)i.va : na)
                             : _WmoDiagView < 5.5 ? albedo
                             : _WmoDiagView < 6.5 ? t0.rgb
                             :                      (twoLayer ? t1.rgb : na);
                    return fixed4(_WmvShaderEncode > 0.5 ? WmvAuthoredToLinear(v) : v, 1.0);
                }

                // Albedo times 1: the client's vertex-colour tint is off by default (a neutral 0.5 x 2),
                // and every MOCV set-1 use is exe-gated and unresolved, so no vertex colour enters.
                fixed4 c = fixed4(albedo, 1.0);
                if (_WmoVertexColourDiag > 0.5)
                    c.rgb *= i.vcol.rgb;

                // ---- COPIED FROM WmvOpaque.shader: the preview light rig (produces lum and spec) ------
                half3 n  = normalize(i.normal);      // world space -- the legacy rig used this
                half3 vn = normalize(i.viewN);       // view space  -- the current rig lives here
                half  rim = 1.0h - saturate(vn.z);

                half3 kDir   = KEY_DIR,    fDir     = FILL_DIR;
                half  shStr  = SHADOW_STRENGTH, shSoft = SHADOW_SOFT;
                half  cStr   = CONTACT_STRENGTH;
                float cRange = CONTACT_RANGE;

                half  lum;
                half3 spec = 0.0h;
                if (_WmvRig > 0.5)
                {
                    lum = 0.45h + 0.75h * saturate(dot(n, normalize(half3(0.35, 0.80, -0.50))));
                }
                else
                {
                    half  ndl, ndlSigned;
                    half3 kw = _WmvKeyDirWorld.xyz;
                    if (dot(kw, kw) > 0.5h)
                        ndlSigned = dot(n, kw);
                    else
                        ndlSigned = dot(vn, normalize(kDir));
                    ndl = saturate(ndlSigned);
                    half shadowSide = 1.0h - ndl;

                    half castKey = 1.0h;                // the key's directional shadow
                    half occ = 1.0h;                    // near-field sky/ambient occlusion
                    if (shStr > 0.0h)
                        castKey = 1.0h - shStr * (1.0h - WmvShadowFactor(i.wpos, n, shSoft));
                    if (cStr > 0.0h)
                    {
                        half contact = WmvContactFactor(i.wpos, n, cRange * _WmvModelRadius,
                                                        i.pos.xy);
                        occ = 1.0h - cStr * (1.0h - contact);
                        castKey = min(castKey, occ);    // whatever is that close blocks the key too
                    }

                    half nUp  = dot(n, half3(0.0h, 1.0h, 0.0h));
                    half band = (nUp >= 0.0h) ? lerp(AMB_HORIZON, AMB_ZENITH, nUp)
                                              : lerp(AMB_HORIZON, AMB_GROUND, -nUp);
                    half ambient = band * (AMB_BASE + AMB_WRAP * (0.5h + 0.5h * ndlSigned));
                    half direct  = ndl * LIGHT_LEVEL;
                    lum = ambient * occ + direct * castKey;
                }
                // ---- END COPY -----------------------------------------------------------------------

                if (_WmvFlatAlbedo > 0.0)
                    c.rgb = (half3)_WmvFlatAlbedo;

                // F_UNLIT on an id without an emissive term: the client's unlit mode returns the albedo,
                // so the preview light is bypassed the way WmvOpaque.shader bypasses it for an emissive
                // pass -- and, as there, only under the shipped rig, so the legacy rig stays a record.
                if (_WmoLightBypass > 0.5 && _WmvRig < 0.5)
                {
                    lum  = 1.0h;
                    spec = 0.0h;
                }

                c.rgb = c.rgb * lum + spec;

                // Blend 0 and 1 output alpha 1 (blending is off for both).
                c.a = 1.0;

                if (_WmvShaderEncode > 0.5)
                    c.rgb = WmvAuthoredToLinear(c.rgb);
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
