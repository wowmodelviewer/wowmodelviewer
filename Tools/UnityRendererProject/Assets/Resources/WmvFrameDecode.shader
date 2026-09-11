// WmvFrameDecode.shader
//
// One full-screen pass that turns the whole frame from the AUTHORED domain into linear light,
// once, after every model and emitter has been composited and before post-processing.
//
// WHY THE FRAME AND NOT THE FRAGMENT
//   The M2 shaders work in the authored (display) domain -- textures are uploaded undecoded and the
//   maths is the client's -- and the swapchain expects linear. Converting at the END OF EACH
//   FRAGMENT was exact for a lone fragment and wrong for every stack: the hardware blend then
//   executed on linear numbers, while Wowhead's viewer, the legacy OpenGL viewport and the game
//   all blend the authored values themselves. Two additive layers authored at 0.5 reach 255 in
//   all three references and 175 when summed in linear; a lone additive particle fading at
//   alpha a displays a in the references and OETF(a) here (0.735 for 0.5).
//
//   So the fragments now write authored values, the blend runs on them, and THIS pass decodes the
//   finished composite exactly once. A lone fragment lands at exactly the same byte as before --
//   the same EOTF, applied to the same value, just later -- and a stack lands where the references
//   put it.
//
// THE CURVE
//   The exact sRGB EOTF, not the pow(2.2) approximation: it has to invert the swapchain's encode
//   exactly or every mid-tone shifts. Values above 1 are deliberately left unclamped so an authored
//   glow can still blow out and can still feed bloom, which runs after this pass and therefore sees
//   what it saw before for unblended pixels.
//
// Drawn by WmvFrameDecodePass through RenderGraph's material blit; the vertex stage is URP's own
// full-screen triangle and the source is bound as _BlitTexture.

Shader "Wmv/FrameDecode"
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
            Name "WmvFrameDecode"

            HLSLPROGRAM
            #pragma vertex Vert
            #pragma fragment Frag
            #pragma target 3.0

            // The pipeline's Core.hlsl first, as URP's own CoreBlit.shader does: it brings in the
            // platform API header and the TEXTURE2D_X macros that Blit.hlsl is written against.
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.core/Runtime/Utilities/Blit.hlsl"

            float3 WmvAuthoredToLinear(float3 c)
            {
                float3 lo = c * (1.0 / 12.92);
                float3 hi = pow(max((c + 0.055) * (1.0 / 1.055), 0.0), 2.4);
                return lerp(lo, hi, step(0.04045, c));
            }

            float4 Frag(Varyings input) : SV_Target
            {
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                float4 c = SAMPLE_TEXTURE2D_X(_BlitTexture, sampler_PointClamp, input.texcoord);
                c.rgb = WmvAuthoredToLinear(c.rgb);
                return c;
            }
            ENDHLSL
        }
    }

    Fallback Off
}
