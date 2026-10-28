// WmvWaterSkyProbe.shader -- Wmv/WaterSkyProbe, the lifecycle self-test's probe of Stage 10 step 8's water sky
// (WmvWaterSky.hlsl's WmvWaterSkyDir, which the High water's reflections that miss take; docs/world-viewer/10-light-sky-fog.md
// 6.11). Never drawn by the viewer: the self-test draws a quad with it inside WmvTerrainComposite's render, one pixel per unit of
// the window, and compares every pixel with the CPU twin (Wow/World/SkyDome.cs: SkyDome.Trace on the dome's own arrays) within
// 3e-3, as the sky's own fixture.
//
// THE GRID (the global _WmvWaterSkyProbeGrid: x columns, y rows): the pixel of column c = floor(world x) and row r = floor(-world
// z) -- the window's x 0.. and z 0.. southwards, one unit a pixel, row 0 the north row -- looks along longitude 2 pi c / x (from
// +x towards +z, the dome's sense: every fourth column of 96 exactly on a wedge edge) and elevation pi r / (y - 1) - pi / 2 (the
// first row the bottom pole, the last the top one). It writes WmvWaterSkyDir of that direction, authored, alpha 1; the water
// sky's globals are the test's (set in its command buffer).
//
// HLSLPROGRAM with URP's Core.hlsl, explicit float, no keyword; opaque, no depth.

Shader "Wmv/WaterSkyProbe"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull Off
        ZWrite Off
        ZTest Always
        Blend Off

        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "WmvWaterSky.hlsl"

            // A global, never a property: the grid's columns (x) and rows (y).
            float4 _WmvWaterSkyProbeGrid;

            struct Attributes
            {
                float3 positionOS : POSITION;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float3 positionWS : TEXCOORD0;
            };

            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionWS = TransformObjectToWorld(v.positionOS);
                o.positionCS = TransformWorldToHClip(o.positionWS);
                return o;
            }

            float4 frag(Varyings i) : SV_Target
            {
                float c = floor(i.positionWS.x), r = floor(-i.positionWS.z);
                float lon = 6.283185307179586 * c / max(_WmvWaterSkyProbeGrid.x, 1.0);
                float e = 3.141592653589793 * r / max(_WmvWaterSkyProbeGrid.y - 1.0, 1.0) - 1.5707963267948966;
                float3 d = float3(cos(e) * cos(lon), sin(e), cos(e) * sin(lon));
                return float4(WmvWaterSkyDir(d), 1.0);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
