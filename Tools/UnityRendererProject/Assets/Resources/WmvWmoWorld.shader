// WmvWmoWorld.shader -- "WMV/Map Object (world)": WmvWmo.shader's program with WMV_WORLD_LIT defined (Stage 10 step 6,
// docs/world-viewer/10-light-sky-fog.md 6.6: the include split, both compile WmvWmoPass.cginc).
//
// While the outdoor light is shown in the perspective world view, the world binds a copy of an EXTERIOR group's material to
// this shader (the copy carries the original's properties by name). The light is then the client's world-model hemisphere
// program (WmvWorldLight.hlsl; Wow/World/ModelLight.cs is the CPU twin and the evidence) in place of the preview rig: base =
// the albedo x (1, 0.99998, 1) (the gate-off vertex colour x 2; exterior MOCV deferred), colour = base x (A + D) with Stage
// 10's holds, no saturate; F_UNLIT materials keep the albedo. Step 7 ends it with the client's per-pixel fog (WmvWorldFog.hlsl)
// while _WmvWLModels.w is set, towards the target _WmvFogBatch names, unless the material is F_UNFOGGED. In every other state
// the world holds the original material, so this program's else-branch (the rig, with _WmvWLOn 0) is drawn by no picture.
// Interior groups and the Buildings viewer never use it.
//
// The Properties, tags, render state and pragmas are WmvWmo.shader's and must stay identical to them (change both together),
// with one documented exception: _WmvFogBatch (step 7), the copy's fog vector, which WmvWmo.shader does not declare (its
// program has no fog); read that file for everything else.

Shader "WMV/Map Object (world)"
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
        // Stage 10 step 7, this variant only (see the header): set per copy by WmvWorldLitMaterials.SetFogBatch.
        _WmvFogBatch ("Stage 10 fog: x 1 = unfogged (NoFog / F_UNFOGGED), y the fog target kind", Vector) = (0, 0, 0, 0)
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

            // The pass body of WmvWmo.shader with the outdoor light's and the fog's hooks compiled in.
            #define WMV_WORLD_LIT 1
            #include "WmvWmoPass.cginc"
            ENDCG
        }
    }

    Fallback Off
}
