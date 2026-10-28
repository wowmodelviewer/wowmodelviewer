// The renderer's guaranteed fallback material, and the only one that can run an M2 combiner.
//
// A player build strips every shader no asset references, and a material built at runtime
// references nothing at build time -- so Shader.Find("Standard"), the URP/HDRP Lit names and even
// the default material a primitive is created with can ALL come back missing or as the magenta
// error shader. What is left is whatever Unity always includes, and those (Sprites/Default, the
// UI shaders) bake alpha blending, ZWrite Off and Cull Off into the pass. Those are not material
// properties, so nothing can turn them off, and an opaque WoW material renders as a stack of
// translucent shells.
//
// Anything under a Resources folder is always included in the build, so this file is the one
// shader the player can count on. It is deliberately plain: single pass, single variant, no
// pipeline-specific includes, and no lighting data from the engine (URP and HDRP do not feed the
// built-in light uniforms). The key light below is fixed in world space so the model shades the
// same way in every pipeline.
//
// EVERYTHING IS UNIFORM-DRIVEN, never a shader keyword: the blend state, the combiner, where the
// second texture unit takes its coordinates, and how the output alpha is built. A player build
// strips shader VARIANTS as readily as it strips whole shaders, so a keyword per material feature
// would be a way to lose them all. One variant, a handful of floats.
//
// THE PASS BODY IS IN WmvOpaquePass.cginc (Stage 10 step 6's include split): WmvOpaqueWorld.shader compiles the same body
// with the outdoor light's hooks; without WMV_WORLD_LIT they expand to nothing, so this shader's program is unchanged.

Shader "WMV/Opaque Textured"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _SecondTex ("Second texture (M2 unit 1)", 2D) = "black" {}
        // The M2's THIRD texture unit. Black by default, so a material that never binds one adds
        // exactly nothing: the lobe below is t2.rgb * t2.a, and both are 0 for the default.
        _ThirdTex ("Third texture (M2 unit 2)", 2D) = "black" {}

        // 0 = single texture;      1 = unit0 * unit1;          2 = unit0 * unit1 * 2;
        // 3 = mix(u0*u1, u0, u0.a); 4 = mix(u0, u1, u1.a);      12 = mix(u0*u1*2, u0, u0.a).
        // Which M2 pixel shader maps to which lives in WmvModelBuilder.PlanCombiner, next to the
        // reasoning; the shader only needs the arithmetic.
        _CombinerMode ("Combiner", Float) = 0
        // 1 on an additive batch: emitted light, which the preview rig must not dim. Declared
        // here as a PROPERTY and not only as a uniform -- Material.HasProperty answers from
        // this block, and the builder's SetFloat is guarded by it. (A first version declared
        // only the uniform, the guard was false for every material, and the bypass never ran.)
        _Emissive ("Emissive pass", Float) = 0
        // Where unit 1 samples: 0 = uv set 0, 1 = uv set 1, 2 = environment sphere map.
        _Unit1UV ("Unit 1 UV source", Float) = 2

        // The same question for UNIT 0, which used to have no answer: it always sampled mesh UV
        // set 0. A material whose vertex program is named Diffuse_Env, Diffuse_Env_T1,
        // Diffuse_Env_Env or Diffuse_EdgeFade_Env puts the ENVIRONMENT on unit 0, and sampling
        // that texture with mesh coordinates pins a reflection to the surface: the sheen slides
        // with the model instead of staying put as the view turns. The sphere map was already
        // generated for unit 1's sake; this lets unit 0 reach it.
        //
        // Default 0 (mesh UV set 0), NOT 2 like _Unit1UV, so a material that never sets this --
        // any pipeline fallback, any older path -- keeps exactly today's behaviour.
        _Unit0UV ("Unit 0 UV source", Float) = 0
        // How the M2 combiner builds its "discard alpha": 0 = 1, 1 = unit0.a, 2 = unit1.a,
        // 3 = unit0.a * unit1.a, 4 = unit0.a + unit1.a. Scaled by _AlphaScale (the x2 combiners).
        _AlphaMode ("Alpha source", Float) = 0
        _AlphaScale ("Alpha scale", Float) = 1
        // 1 when the blend mode ignores that alpha entirely (opaque and alpha-key both output 1).
        _OpaqueAlpha ("Force opaque alpha", Float) = 1

        _Color ("Tint", Color) = (1,1,1,1)
        // DIAGNOSTIC ONLY (-wmvWmoVertexColour): 1 multiplies the mesh's vertex colour into the
        // albedo. Nothing sets it in normal rendering, and a mesh with no colours is never drawn with
        // it on, so every material keeps exactly the colour it had without this.
        _VertexColour ("Diagnostic vertex colour", Float) = 0

        // THE M2 TEXTURE TRANSFORM, per unit, as a 2x2 matrix plus an offset in THIS renderer's
        // UV space (the V axis is already flipped by the mesh builder; the animator conjugates the
        // WoW-space matrix by that flip, so nothing here has to know about it). Identity by
        // default, so a material nothing animates samples exactly as it did before this existed.
        _UvXf0 ("Unit 0 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff0 ("Unit 0 UV offset", Vector) = (0,0,0,0)
        _UvXf1 ("Unit 1 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff1 ("Unit 1 UV offset", Vector) = (0,0,0,0)
        _UvXf2 ("Unit 2 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff2 ("Unit 2 UV offset", Vector) = (0,0,0,0)
        // Where unit 2 samples: 0 = uv set 0, 1 = uv set 1, 2 = environment sphere map. Default 0.
        // The M2 shader table decides this per material (Diffuse_T1_Env_T1 puts unit 2 on set 0);
        // nothing here assumes a coordinate the vertex-shader name did not name.
        _Unit2UV ("Unit 2 UV source", Float) = 0
        // 1 when this material's pixel shader adds the third unit as an authored luminous term.
        // Deliberately NOT _Emissive: that float bypasses the preview light entirely and already
        // carries UNLIT and blend modes 3/4. This one adds energy and changes nothing else.
        _FirstUnitLobe ("First-unit luminous lobe (ps24)", Float) = 0
        _FirstUnitWeight ("First-unit weight (that lobe's gain)", Float) = 1
        _SecondUnitLobe ("Second-unit luminous lobe", Float) = 0
        _SecondUnitWeight ("Second-unit weight (that lobe's gain)", Float) = 1
        _ThirdUnitLobe ("Third-unit luminous lobe", Float) = 0
        _ThirdUnitWeight ("Third-unit weight (the lobe's gain)", Float) = 1
        _Cutoff ("Alpha cutoff", Range(0,1)) = 0.5
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2      // Back
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend", Float) = 1  // One
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend", Float) = 0  // Zero
        [Toggle] _ZWrite ("ZWrite", Float) = 1
    }

    SubShader
    {
        // No LightMode tag: the built-in pipeline draws this as a normal opaque pass, and URP
        // draws it through SRPDefaultUnlit, so one SubShader covers both.
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull [_Cull]
        Blend [_SrcBlend] [_DstBlend]
        ZWrite [_ZWrite]

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            // The contact march dithers on SV_POSITION, which is a fragment-stage capability
            // Unity documents from shader model 3.0. The file carried no target at all, so it
            // built at the 2.5 default; nothing else here needs 3.0, but reading i.pos does.
            #pragma target 3.0
            #pragma multi_compile_local _ _ALPHATEST_ON
            #include "UnityCG.cginc"

            // The pass body: WmvOpaquePass.cginc (Stage 10 step 6, the include split). WmvOpaqueWorld.shader includes
            // the same body with WMV_WORLD_LIT defined; here every hook expands to nothing, so this is the program it was.
            #include "WmvOpaquePass.cginc"
            ENDCG
        }
    }

    Fallback Off
}
