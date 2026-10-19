// WmvLiquid.shader -- Wmv/Liquid, the LIQUID material (stage 09, docs/world-viewer/09-liquids.md 1.5): one material per
// liquid type and scene (WmvLiquidRegistry), shared by every tile (MH2O) and world model (MLIQ) that draws the type.
//
// WHAT IT DRAWS is an APPROXIMATION, every part a documented choice (WmvLiquidMaterials), none claimed as the game's.
// The colour follows the type's KIND (_ColourFromFrame), the blending its material's FLAG 0x1 (_LiquidMode): 1.5 keeps the
// two apart, and Retail's "10FX" lava, fel and swamp (materials 13, 14, 18: kind magma, flag 0x1) are blended and take
// their frame's colour.
//   _ColourFromFrame 0 (water, ocean)
//     colour  = _BaseColor.rgb + frame.a * _Highlight     the base colour is the type's MinimapStaticCol, standing in for
//                                                         the zone's light colours until stage 12; the water and ocean
//                                                         frames are near-black and carry their ripples in alpha (1.5)
//   _ColourFromFrame 1 (magma, slime)
//     colour  = frame.rgb                                  the magma and slime frames are fully coloured
//   _LiquidMode 0 (transparent: the type's LiquidMaterial flag 0x1)
//     alpha   = lerp(_ShallowAlpha, _DeepAlpha, uv1.x)    uv1.x is the vertex's depth byte / 255 (1 where none is stored:
//                                                         deep); blended SrcAlpha / OneMinusSrcAlpha, no depth write
//   _LiquidMode 1 (opaque)
//     alpha   = 1                                          depth written
//   No frames (_FrameCount 0: slot 0 names none, or a frame could not be had, or they are still on their way): the base
//   colour alone (and in the transparent mode the alpha by depth).
// The frame: slice _Frame of the frame array, sampled at uv0 (water: world-planar coordinates; magma and slime: the stored
// u/v or s/t scaled -- Wow/World/TerrainLiquidMesh.cs, Wow/WmoLiquid.cs), Repeat, trilinear. _Frame is set on the shared
// material from the liquid clock once a frame (WmvLiquidRegistry.Tick).
// UNLIT: no light rig, shadow, fog, reflection or refraction (stages 10-12). Culling off: a liquid is one sheet, seen from
// above and below.
//
// THE COLOUR DOMAIN, as the other world shaders (Wmv/TerrainTextured, Wmv/Particle): the frames are UNorm (never sRGB), the
// fragment writes AUTHORED values -- the blend runs on them -- and the frame decode pass converts the finished frame once;
// with _WmvShaderEncode set (the per-fragment display mode) or _WmvLinearOut (the A/B display modes without the authored
// domain) the colour is converted here instead.
//
// HLSLPROGRAM with URP's Core.hlsl, every switch a uniform (never a keyword: the player build strips variants), every
// per-material value in the UnityPerMaterial CBUFFER (the SRP Batcher's condition). No LightMode tag, as the other Wmv
// shaders: drawn by the ordinary pass. The queue and RenderType are the material's (WmvLiquidMaterials.CreateMaterial).

Shader "Wmv/Liquid"
{
    Properties
    {
        _Frames ("Frames (texture slot 0, in frame order)", 2DArray) = "" {}
        _FrameCount ("Frames in the array (0: the base colour alone)", Float) = 0
        _Frame ("The frame drawn (the liquid clock)", Float) = 0
        _BaseColor ("Base colour (MinimapStaticCol, authored rgba)", Vector) = (0, 0, 0, 1)
        _LiquidMode ("Mode (0 transparent: the material's flag 0x1; 1 opaque)", Float) = 0
        _ColourFromFrame ("Colour from the frame (1: magma, slime; 0: water, ocean, base colour + highlight)", Float) = 0
        _Highlight ("The frame's alpha added to the base colour (transparent mode)", Float) = 0.5
        _ShallowAlpha ("Opacity at depth byte 0 (transparent mode)", Float) = 0.45
        _DeepAlpha ("Opacity at depth byte 255, and where no depth is stored (transparent mode)", Float) = 0.8
        _WmvLinearOut ("Convert the authored colour to linear", Float) = 0
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend", Float) = 5    // SrcAlpha
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend", Float) = 10   // OneMinusSrcAlpha
        _ZWrite ("Depth write", Float) = 0
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 0              // Off
    }

    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent-1" "IgnoreProjector" = "True" }

        Cull [_Cull]
        ZWrite [_ZWrite]
        ZTest LEqual
        Blend [_SrcBlend] [_DstBlend]

        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            TEXTURE2D_ARRAY(_Frames); SAMPLER(sampler_Frames);

            CBUFFER_START(UnityPerMaterial)
                float _FrameCount;
                float _Frame;
                float4 _BaseColor;
                float _LiquidMode;
                float _ColourFromFrame;
                float _Highlight;
                float _ShallowAlpha;
                float _DeepAlpha;
                float _WmvLinearOut;
                float _SrcBlend;
                float _DstBlend;
                float _ZWrite;
                float _Cull;
            CBUFFER_END

            // A global, never a property (a property of the same name would pin it): the per-fragment display encode.
            float _WmvShaderEncode;

            struct Attributes
            {
                float4 positionOS : POSITION;
                float2 uv0        : TEXCOORD0;   // texture coordinates
                float2 uv1        : TEXCOORD1;   // (depth / 255, has depth)
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float2 uv0        : TEXCOORD0;
                float2 uv1        : TEXCOORD1;
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
                o.uv0 = v.uv0;
                o.uv1 = v.uv1;
                return o;
            }

            float4 frag(Varyings i) : SV_Target
            {
                bool animated = _FrameCount > 0.5;
                float4 frame = float4(0.0, 0.0, 0.0, 0.0);
                if (animated)
                {
                    float slice = clamp(floor(_Frame + 0.5), 0.0, _FrameCount - 1.0);
                    frame = SAMPLE_TEXTURE2D_ARRAY(_Frames, sampler_Frames, i.uv0, slice);
                }
                float3 colour;
                if (_ColourFromFrame > 0.5)
                    colour = animated ? frame.rgb : _BaseColor.rgb;
                else
                    colour = _BaseColor.rgb + (animated ? frame.a * _Highlight : 0.0);
                float alpha = _LiquidMode > 0.5 ? 1.0 : lerp(_ShallowAlpha, _DeepAlpha, saturate(i.uv1.x));
                // The authored 8-bit range (the frames and the base colour are stored bytes).
                colour = saturate(colour);
                if (_WmvShaderEncode > 0.5 || _WmvLinearOut > 0.5)
                    colour = WmvAuthoredToLinear(colour);
                return float4(colour, alpha);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
