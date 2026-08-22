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

Shader "WMV/Opaque Textured"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _SecondTex ("Second texture (M2 unit 1)", 2D) = "black" {}

        // 0 = single texture;      1 = unit0 * unit1;          2 = unit0 * unit1 * 2;
        // 3 = mix(u0*u1, u0, u0.a); 4 = mix(u0, u1, u1.a);      12 = mix(u0*u1*2, u0, u0.a).
        // Which M2 pixel shader maps to which lives in WmvModelBuilder.PlanCombiner, next to the
        // reasoning; the shader only needs the arithmetic.
        _CombinerMode ("Combiner", Float) = 0
        // Where unit 1 samples: 0 = uv set 0, 1 = uv set 1, 2 = environment sphere map.
        _Unit1UV ("Unit 1 UV source", Float) = 2
        // How the M2 combiner builds its "discard alpha": 0 = 1, 1 = unit0.a, 2 = unit1.a,
        // 3 = unit0.a * unit1.a, 4 = unit0.a + unit1.a. Scaled by _AlphaScale (the x2 combiners).
        _AlphaMode ("Alpha source", Float) = 0
        _AlphaScale ("Alpha scale", Float) = 1
        // 1 when the blend mode ignores that alpha entirely (opaque and alpha-key both output 1).
        _OpaqueAlpha ("Force opaque alpha", Float) = 1

        _Color ("Tint", Color) = (1,1,1,1)
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
            #pragma multi_compile_local _ _ALPHATEST_ON
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                float2 uv     : TEXCOORD0;
                float2 uv2    : TEXCOORD1;
            };

            struct v2f
            {
                float4 pos    : SV_POSITION;
                float2 uv     : TEXCOORD0;
                float3 normal : TEXCOORD1;
                float2 env    : TEXCOORD2;
                float2 uv1    : TEXCOORD3;
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            sampler2D _SecondTex;
            float _CombinerMode, _Unit1UV, _AlphaMode, _AlphaScale, _OpaqueAlpha;
            fixed4 _Color;
            fixed _Cutoff;

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                o.uv1 = v.uv2;
                o.normal = UnityObjectToWorldNormal(v.normal);

                // ENVIRONMENT UNIT. The M2 vertex shader for a combiner material names where each
                // texture unit takes its coordinates from, and "Env" means the unit is fed by a
                // sphere map generated from the view-space normal -- not by any UV set stored in
                // the mesh. This reproduces fixed-function GL_SPHERE_MAP, which is what the legacy
                // viewport uses for the same unit.
                float3 viewPos = UnityObjectToViewPos(v.vertex);
                float3 viewNrm = normalize(mul((float3x3)UNITY_MATRIX_IT_MV, v.normal));
                float3 r = reflect(normalize(viewPos), viewNrm);
                float m = max(2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0)), 1e-4);
                // V is inverted relative to the GL formula: a BLP's rows are uploaded flipped so
                // that ordinary Unity UVs work, which puts this generated coordinate the other way
                // up from the OpenGL viewport's. Without the flip the sheen sits on the wrong side.
                o.env = float2(r.x / m + 0.5, 0.5 - r.y / m);
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                fixed4 t1 = tex2D(_MainTex, i.uv);

                // Unit 1's coordinates, per the material's vertex shader name.
                float2 uv1 = (_Unit1UV < 0.5) ? i.uv : ((_Unit1UV < 1.5) ? i.uv1 : i.env);
                fixed4 t2 = tex2D(_SecondTex, uv1);

                // COLOUR. The M2 combiners this milestone implements are all products of the two
                // units, except pixel shader 12, whose second unit is masked by the FIRST unit's
                // alpha (that channel is a reflection mask on a creature skin, not transparency).
                fixed3 rgb = t1.rgb;
                if (_CombinerMode > 11.5)                       // 12
                    rgb = lerp(t1.rgb * t2.rgb * 2.0, t1.rgb, t1.a);
                else if (_CombinerMode > 3.5)                   // 4: decal
                    rgb = lerp(t1.rgb, t2.rgb, t2.a);
                else if (_CombinerMode > 2.5)                   // 3: masked modulate
                    rgb = lerp(t1.rgb * t2.rgb, t1.rgb, t1.a);
                else if (_CombinerMode > 1.5)                   // 2: unit0 * unit1 * 2
                    rgb = t1.rgb * t2.rgb * 2.0;
                else if (_CombinerMode > 0.5)                   // 1: unit0 * unit1
                    rgb = t1.rgb * t2.rgb;
                fixed4 c = fixed4(rgb * _Color.rgb, 1.0);

                // ALPHA. The combiner builds a "discard alpha" the blend mode then either uses as
                // the output opacity or only tests against. Opaque and alpha-key output 1 and the
                // key discards below the cutoff; every other mode outputs it.
                fixed a = 1.0;
                if (_AlphaMode > 3.5)      a = t1.a + t2.a;
                else if (_AlphaMode > 2.5) a = t1.a * t2.a;
                else if (_AlphaMode > 1.5) a = t2.a;
                else if (_AlphaMode > 0.5) a = t1.a;
                a = saturate(a * _AlphaScale);

                #ifdef _ALPHATEST_ON
                    clip(a - _Cutoff);
                #endif

                // Fixed key light + ambient. Engine light data is unavailable across pipelines,
                // so this is a viewer light, not the scene's -- it only has to read as shape.
                half3 n = normalize(i.normal);
                half ndl = saturate(dot(n, normalize(half3(0.35, 0.80, -0.50))));
                c.rgb *= 0.45h + 0.75h * ndl;

                c.a = (_OpaqueAlpha > 0.5) ? 1.0 : a;
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
