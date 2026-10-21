// WmvWaterGrabDepth.shader
//
// The High water look's depth copy and reflection colour (WmvWaterGrabPass), two passes:
//   0  the camera's depth attachment, after the opaque world, written as its RAW device depth into an R32F target of the
//      same size -- _WmvSceneDepth, which the water's HIGH path rebuilds the scene point from (through the inverse
//      view-projection) and marches its reflection over. Point sampled, one texel to one texel: nothing is filtered or
//      linearised here.
//   1  the scene colour (_WmvSceneColor, the source) premultiplied by its coverage, the coverage in alpha: (rgb, 1) where
//      _WmvSceneDepth holds the scene, (0, 0, 0, 0) where it holds the far plane (the background: the camera's clear
//      colour) -- _WmvSceneColorRefl, the reflections' colour and the source of their blurred 1/4. A filtered or
//      blurred texel then holds sum(coverage x colour) and the coverage's mean, so the water reads c.rgb + sky (1 - c.a):
//      exactly the scene with the background replaced by the sky, never the clear colour. Point sampled, one to one.
//
// Drawn through the Blitter (the source bound as _BlitTexture); the vertex stage is URP's own full-screen triangle, as in
// Wmv/FrameDecode.

Shader "Wmv/WaterGrabDepth"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }
        ZWrite Off
        ZTest Always
        Cull Off
        Blend Off

        Pass
        {
            Name "WmvWaterGrabDepth"

            HLSLPROGRAM
            #pragma vertex Vert
            #pragma fragment Frag
            #pragma target 3.0

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.core/Runtime/Utilities/Blit.hlsl"

            float4 Frag(Varyings input) : SV_Target
            {
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                float raw = SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_PointClamp, input.texcoord, 0).r;
                return float4(raw, 0.0, 0.0, 1.0);
            }
            ENDHLSL
        }

        Pass
        {
            Name "WmvWaterGrabCoverage"

            HLSLPROGRAM
            #pragma vertex Vert
            #pragma fragment Frag
            #pragma target 3.0

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.core/Runtime/Utilities/Blit.hlsl"

            // The depth copy of pass 0 (a global, set after that pass).
            TEXTURE2D(_WmvSceneDepth);

            float4 Frag(Varyings input) : SV_Target
            {
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                float4 c = SAMPLE_TEXTURE2D_X_LOD(_BlitTexture, sampler_PointClamp, input.texcoord, 0);
                float raw = SAMPLE_TEXTURE2D_LOD(_WmvSceneDepth, sampler_PointClamp, input.texcoord, 0).r;
                // The depth buffer is cleared to the far plane and no geometry lies on it: exactly the background.
                float coverage = raw == UNITY_RAW_FAR_CLIP_VALUE ? 0.0 : 1.0;
                return float4(c.rgb * coverage, coverage);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
