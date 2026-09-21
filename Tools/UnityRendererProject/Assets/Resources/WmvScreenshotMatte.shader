// WmvScreenshotMatte.shader
//
// THE SCREENSHOT'S MATTE (WmvScreenshot.cs). One full-screen pass that turns two renders of the same frame -- one
// cleared to black, one cleared to white -- into the straight colour and the coverage of an RGBA PNG. Only the
// capture draws with it; nothing on screen does.
//
// WHY TWO RENDERS
//   The materials blend alpha with their colour factors (Blend [_SrcBlend] [_DstBlend]), so the alpha a render
//   writes is not coverage: an additive quad whose texture alpha is 1 writes 1 over its whole area, and an
//   alpha-blended fragment over a transparent clear writes a * a. What a pixel lets through of the background is
//   measured instead: over black a pixel shows only its own light B, over white it shows B plus the part of the
//   background that shows through, so W - B is that part.
//
// THE DOMAIN
//   The blends run on AUTHORED values and WmvFrameDecode decodes the finished frame into linear light, so both
//   targets hold decoded values. They are encoded back first, with the exact inverse of that decode: the difference
//   has to be the one the blend made, and the authored values are the bytes the viewport displays. Both are clamped
//   to [0, 1] first, as the display clamps them.
//
// THE MATTE, per pixel
//   coverage a = 1 - max over rgb of (W - B): a pixel that shows none of the background is opaque, one that shows
//     all of it is empty, and an alpha-blended fragment gets its own alpha;
//   raised to max over rgb of B where it is lower: light ADDED over a background has no straight-alpha form, and
//     this is the least coverage whose straight colour stays within [0, 1], so the PNG over black is the black
//     render exactly and an additive glow's alpha follows its brightness;
//   colour = B / a, divided by the coverage as the 8-bit channel will store it, so colour x alpha rebuilds B to
//     within the colour's own rounding; no coverage, no colour.
// The target is 8-bit UNORM and takes the authored values as they are: they are the PNG's bytes.

Shader "Wmv/ScreenshotMatte"
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
            Name "WmvScreenshotMatte"

            HLSLPROGRAM
            #pragma vertex Vert
            #pragma fragment Frag
            #pragma target 3.0

            // As WmvFrameDecode: the pipeline's Core.hlsl, then Blit.hlsl for the full-screen triangle (Vert).
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.core/Runtime/Utilities/Blit.hlsl"

            TEXTURE2D(_WmvOverBlack);
            TEXTURE2D(_WmvOverWhite);

            // WmvFrameDecode's WmvAuthoredToLinear inverted (the exact sRGB curve), on [0, 1].
            float3 WmvLinearToAuthored(float3 c)
            {
                c = saturate(c);
                float3 lo = c * 12.92;
                float3 hi = 1.055 * PositivePow(c, float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
                return saturate(c <= 0.0031308 ? lo : hi);
            }

            float4 Frag(Varyings input) : SV_Target
            {
                // The same pixel of both renders: they are the size of this target.
                uint2 px = uint2(input.positionCS.xy);
                float3 b = WmvLinearToAuthored(LOAD_TEXTURE2D(_WmvOverBlack, px).rgb);
                float3 w = WmvLinearToAuthored(LOAD_TEXTURE2D(_WmvOverWhite, px).rgb);
                float3 through = w - b;
                float a = max(1.0 - max(through.r, max(through.g, through.b)), max(b.r, max(b.g, b.b)));
                a = round(saturate(a) * 255.0) / 255.0;
                float3 c = a > 0.0 ? saturate(b / max(a, 1.0 / 255.0)) : float3(0.0, 0.0, 0.0);
                return float4(c, a);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
