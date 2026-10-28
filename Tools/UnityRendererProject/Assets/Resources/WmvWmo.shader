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
//
// THE PASS BODY IS IN WmvWmoPass.cginc (Stage 10 step 6's include split): WmvWmoWorld.shader compiles the same body with the
// outdoor light's hooks; without WMV_WORLD_LIT they expand to nothing, so this shader's program is unchanged.

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
        // diffuse, 6 register t0 as sampled, 7 register t1 as sampled (two-layer), 8 the emissive mask of
        // permutations 6, 5 and 2 (the factor the client multiplies its env map by; no env map is sampled)
        // -- all unlit; _WmoDiagHide 1 discards the material in every camera. Never set otherwise.
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

            // The pass body: WmvWmoPass.cginc (Stage 10 step 6, the include split). WmvWmoWorld.shader includes
            // the same body with WMV_WORLD_LIT defined; here every hook expands to nothing, so this is the program it was.
            #include "WmvWmoPass.cginc"
            ENDCG
        }
    }

    Fallback Off
}
