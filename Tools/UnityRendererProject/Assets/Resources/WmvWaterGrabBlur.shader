// WmvWaterGrabBlur.shader
//
// The High water look's one small blur (WmvWaterGrabPass): the scene colour at 1/4 resolution (already a 4 x 4 box of the
// full-resolution copy) blurred once more into _WmvSceneColorBlur, the broad reflection's source. Four bilinear taps at
// (+-s/2, +-s/2) texels, s = _WmvGrabBlurStep (TUNABLE grabBlur; 1: exactly the 3 x 3 tent [1 2 1] x [1 2 1] / 16). The
// texel size is the destination's own pixel step (source and destination are the same size), so no size is passed in.
//
// Drawn through the Blitter (the source bound as _BlitTexture); the vertex stage is URP's own full-screen triangle, as in
// Wmv/FrameDecode. Colours are copied in whatever domain the frame holds (the authored one before the frame decode).

Shader "Wmv/WaterGrabBlur"
{
    Properties
    {
        _WmvGrabBlurStep ("Tap spacing in texels (1: the 3 x 3 tent)", Float) = 1
    }

    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }
        ZWrite Off
        ZTest Always
        Cull Off
        Blend Off

        Pass
        {
            Name "WmvWaterGrabBlur"

            HLSLPROGRAM
            #pragma vertex Vert
            #pragma fragment Frag
            #pragma target 3.0

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.core/Runtime/Utilities/Blit.hlsl"

            float _WmvGrabBlurStep;

            float4 Frag(Varyings input) : SV_Target
            {
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                float2 uv = input.texcoord;
                float2 h = float2(abs(ddx(uv.x)), abs(ddy(uv.y))) * (0.5 * _WmvGrabBlurStep);
                float4 c = SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_LinearClamp, uv + float2(-h.x, -h.y), 0)
                         + SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_LinearClamp, uv + float2(h.x, -h.y), 0)
                         + SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_LinearClamp, uv + float2(-h.x, h.y), 0)
                         + SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_LinearClamp, uv + float2(h.x, h.y), 0);
                return c * 0.25;
            }
            ENDHLSL
        }
    }

    Fallback Off
}
