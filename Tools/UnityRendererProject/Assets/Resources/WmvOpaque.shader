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
// The render state is exposed as properties, which is the point: WmvModelBuilder sets _SrcBlend,
// _DstBlend, _ZWrite, _Cull and _Cutoff explicitly, and here those calls actually take effect.
//
// SECOND TEXTURE UNIT. An M2 material can combine several textures, and which arithmetic it uses
// is selected by the material's shader id rather than described in the file. _CombinerMode carries
// the resolved combiner id (the same numbering the legacy OpenGL viewport's GLSL combiner uses);
// 0 means "single texture", which is the default and the behaviour of every material this shader
// does not have a case for. It is a plain float uniform, not a shader keyword, so no extra shader
// variants exist for a player build to strip.

Shader "WMV/Opaque Textured"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _SecondTex ("Second texture (M2 unit 1)", 2D) = "black" {}
        _CombinerMode ("Combiner id (0 = single texture)", Float) = 0
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
            };

            struct v2f
            {
                float4 pos    : SV_POSITION;
                float2 uv     : TEXCOORD0;
                float3 normal : TEXCOORD1;
                float2 env    : TEXCOORD2;
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            sampler2D _SecondTex;
            float _CombinerMode;
            fixed4 _Color;
            fixed _Cutoff;

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
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
                fixed4 c = t1 * _Color;
                #ifdef _ALPHATEST_ON
                    clip(c.a - _Cutoff);
                #endif

                // Combiner 12, "Combiners_Opaque_Mod2xNA_Alpha". The BASE texture's alpha is the
                // mask that decides where the second unit is folded in at 2x -- it is not
                // transparency. On a creature skin that mask is white almost everywhere and dark
                // only on small features (chicken2 uses it for the eye pupil), so the second unit
                // shows up exactly where the model wants a reflective highlight.
                if (_CombinerMode > 11.5 && _CombinerMode < 12.5)
                {
                    fixed3 t2 = tex2D(_SecondTex, i.env).rgb;
                    c.rgb = lerp(t1.rgb * t2 * 2.0, t1.rgb, t1.a) * _Color.rgb;
                }

                // Fixed key light + ambient. Engine light data is unavailable across pipelines,
                // so this is a viewer light, not the scene's -- it only has to read as shape.
                half3 n = normalize(i.normal);
                half ndl = saturate(dot(n, normalize(half3(0.35, 0.80, -0.50))));
                c.rgb *= 0.45h + 0.75h * ndl;

                // Opaque output: the alpha channel of a WoW skin is not transparency.
                c.a = 1.0h;
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
