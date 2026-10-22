// WmvSky.shader -- Wmv/Sky, the sky's COLOUR DOME (Stage 10 step 4, docs/world-viewer/10-light-sky-fog.md 4.0, 4.3, 6.8).
// Drawn by WmvWorldSky's pass for the main camera's world view only, after the opaque world and before the water's grab;
// the mesh and every input are Wow/World/SkyDome.cs's (its CPU twin, SkyDome.Trace, is what the self-test compares with).
//
// VERTEX: the dome is camera-centred and world-aligned: its position (the table's object units, y up) is turned by the
// view's ROTATION only (no translation), projected, and put on the far plane (z = UNITY_RAW_FAR_CLIP_VALUE x w, reversed-Z
// aware), so with ZTest LEqual (GEqual under reversed Z) it fills only the pixels still at the cleared far depth, and with
// ZWrite Off it leaves the depth as it was (the water grab's coverage, 0 at the far depth, unchanged). Passed on: the vertex
// colour (its slot's: UV0.x), the dome position (the sun tint's direction) and the RAW table height (the horizon ramp).
// Interpolated perspective-correctly, as the client's program interpolates its COLOR and o2.
//
// PIXEL (the client's sky program 2977085, PROVEN_BYTES): rgb = v + sat(x^3) mix(wBelow, wAbove, sat(50 z)) (C - v),
// x = sat(dot(normalize(dir), normalize(sun)) - a) / (1 - a), applied only where x > 0 (a of 1 or more: no tint); no dither.
// The inputs (a = SunFogAngle, C = SunFogColor, the strengths, the sun) are SkyDome.Inputs' (INFERRED (public), graded
// there).
//
// THE COLOUR DOMAIN, as the other Wmv shaders: the inputs are AUTHORED values (the table's byte / 255, set as vectors), the
// fragment writes authored values; with _WmvShaderEncode set (the per-fragment display mode) or the material's _WmvLinearOut
// (the A/B modes without the authored domain) it converts them to linear itself, otherwise the frame decode pass does it once.
//
// HLSLPROGRAM with URP's Core.hlsl, explicit float, every value a uniform (no keyword), the per-material values in the
// UnityPerMaterial CBUFFER. Cull Off (the dome is seen from inside), opaque, alpha written 1.

Shader "Wmv/Sky"
{
    Properties
    {
        _WmvSkyColour0 ("Top pole (SkyTopColor), authored", Vector) = (0, 0, 0, 1)
        _WmvSkyColour1 ("Ring 1 (SkyMiddleColor)", Vector) = (0, 0, 0, 1)
        _WmvSkyColour2 ("Ring 2 (SkyBand1Color)", Vector) = (0, 0, 0, 1)
        _WmvSkyColour3 ("Ring 3 (SkyBand2Color)", Vector) = (0, 0, 0, 1)
        _WmvSkyColour4 ("Ring 4 (SkySmogColor)", Vector) = (0, 0, 0, 1)
        _WmvSkyColour5 ("Ring 5 and the bottom pole (SkyFogColor or its lean to EndFogColor)", Vector) = (0, 0, 0, 1)
        _WmvSkyTint ("Sun tint colour (rgb) and angle a (w; 1 or more: no tint)", Vector) = (0, 0, 0, 1)
        _WmvSkyWeights ("Tint strength above (x) and below (y) the horizon ramp", Vector) = (0, 0, 0, 0)
        _WmvSkySun ("Towards the sun (Unity world)", Vector) = (0, 1, 0, 0)
        _WmvLinearOut ("Convert the authored colour to linear", Float) = 0
    }

    SubShader
    {
        Tags { "RenderType" = "Background" "Queue" = "Background" "IgnoreProjector" = "True" }

        Cull Off
        ZWrite Off
        ZTest LEqual
        Blend Off

        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            CBUFFER_START(UnityPerMaterial)
                float4 _WmvSkyColour0;
                float4 _WmvSkyColour1;
                float4 _WmvSkyColour2;
                float4 _WmvSkyColour3;
                float4 _WmvSkyColour4;
                float4 _WmvSkyColour5;
                float4 _WmvSkyTint;
                float4 _WmvSkyWeights;
                float4 _WmvSkySun;
                float _WmvLinearOut;
            CBUFFER_END

            // A global, never a property: the per-fragment display encode (WmvMain).
            float _WmvShaderEncode;

            struct Attributes
            {
                float3 positionOS : POSITION;
                float2 uv0        : TEXCOORD0;   // x: the colour slot (SkyDome.SlotOf)
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float3 colour     : TEXCOORD0;
                float3 dir        : TEXCOORD1;   // the dome position (world-aligned)
                float  z          : TEXCOORD2;   // the raw table height
            };

            float3 WmvAuthoredToLinear(float3 c)
            {
                float3 lo = c * (1.0 / 12.92);
                float3 hi = pow(max((c + 0.055) * (1.0 / 1.055), 0.0), 2.4);
                return lerp(lo, hi, step(0.04045, c));
            }

            float3 SlotColour(float slot)
            {
                int k = (int)(slot + 0.5);
                if (k <= 0) return _WmvSkyColour0.rgb;
                if (k == 1) return _WmvSkyColour1.rgb;
                if (k == 2) return _WmvSkyColour2.rgb;
                if (k == 3) return _WmvSkyColour3.rgb;
                if (k == 4) return _WmvSkyColour4.rgb;
                return _WmvSkyColour5.rgb;
            }

            Varyings vert(Attributes v)
            {
                Varyings o;
                float3 view = mul((float3x3)UNITY_MATRIX_V, v.positionOS);
                o.positionCS = mul(UNITY_MATRIX_P, float4(view, 1.0));
                o.positionCS.z = UNITY_RAW_FAR_CLIP_VALUE * o.positionCS.w;
                o.colour = SlotColour(v.uv0.x);
                o.dir = v.positionOS;
                o.z = v.positionOS.y;
                return o;
            }

            float4 frag(Varyings i) : SV_Target
            {
                float3 v = i.colour;
                float3 rgb = v;
                float a = _WmvSkyTint.w;
                if (a < 1.0)
                {
                    float x = saturate(dot(normalize(i.dir), normalize(_WmvSkySun.xyz)) - a) / (1.0 - a);
                    if (x > 0.0)
                    {
                        float w = lerp(_WmvSkyWeights.y, _WmvSkyWeights.x, saturate(50.0 * i.z));
                        rgb = v + saturate(x * x * x) * w * (_WmvSkyTint.rgb - v);
                    }
                }
                if (_WmvShaderEncode > 0.5 || _WmvLinearOut > 0.5)
                    rgb = WmvAuthoredToLinear(rgb);
                return float4(rgb, 1.0);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
