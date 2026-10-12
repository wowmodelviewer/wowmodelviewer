// WmvTerrain.shader -- Wmv/Terrain, the terrain tile's GEOMETRY DIAGNOSTIC shader (stage 03).
//
// WHAT IT IS NOT. Not WoW's terrain material: no texture layer, no alpha map, no vertex colour, no MCNR normal, no fog
// or sky light. It shows the geometry the verified builder made (Wow/World/TerrainMesh.cs) and nothing else, and every
// capture of it is labelled a geometry diagnostic.
//
// VIEWS (_WmvTerrainView, set per material by WmvTerrainBuilder; -wmvTerrainView picks them):
//   0 shaded   a neutral grey Lambert from one fixed key plus ambient, from the builder's geometry-derived DIAGNOSTIC
//              normals. The key comes from the north-west, 45 degrees up (Unity +X is east, +Z north), the usual
//              direction a relief map is lit from; it is a presentation choice, not the game's light.
//   1 chunks   the same light over a per-chunk tint (_WmvChunkIndex, per renderer), so chunk borders show
//   2 normals  the diagnostic normal as colour, n * 0.5 + 0.5 (the top-down orientation capture of design 2.3b reads it)
//   3 lines    a flat line colour (_WmvLineColour) for the edge meshes of the wire views. Their depth is moved toward
//              the camera by _WmvLineNudge of clip-space w -- in clip space, so it is the same in normalised device
//              units at every distance (in yards it grows with the square of the depth: WmvTerrainBuilder.
//              LineDepthNudge states the price), with the sign the platform's depth direction needs (reversed-Z on
//              D3D) -- because a line lies exactly on its triangles' edge and would otherwise fight the surface for the
//              depth test. Not ShaderLab Offset: its units and sign under reversed-Z are the platform's, not stated here.
//   _ColorMask 0 makes a surface write depth only (the wire view: the background shows through, hidden edges stay
//   hidden).
//
// THE COLOUR DOMAIN. Like the world-model diagnostic view (WmvWmo.shader): the fragment writes authored (display-
// encoded) values; with _WmvShaderEncode set (the per-fragment display mode) it converts them to linear itself,
// otherwise the frame decode pass does it once for the whole frame. No LightMode tag, as WmvOpaque / WmvWmo: drawn by
// the ordinary pass (SRPDefaultUnlit in URP). Back faces are culled: the builder's front faces look up.

Shader "Wmv/Terrain"
{
    Properties
    {
        _WmvTerrainView ("View (0 shaded, 1 chunks, 2 normals, 3 lines)", Float) = 0
        _WmvChunkIndex ("Chunk index (per renderer)", Float) = 0
        _WmvLineColour ("Line colour", Color) = (0.08, 0.08, 0.09, 1)
        _WmvLineNudge ("Line depth nudge (fraction of clip w)", Float) = 0.0001
        _ColorMask ("Colour write mask", Float) = 15
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2    // Back
    }

    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull [_Cull]
        ZWrite On
        ZTest LEqual
        ColorMask [_ColorMask]

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
            };

            struct v2f
            {
                float4 pos    : SV_POSITION;
                float3 normal : TEXCOORD0;   // world space
            };

            float _WmvTerrainView;
            float _WmvChunkIndex;
            fixed4 _WmvLineColour;
            float _WmvLineNudge;

            // A preview global set by WmvMain, never a property (a property of the same name would pin it).
            float _WmvShaderEncode;

            // Toward the key: azimuth north-west, 45 degrees above the horizon, in Unity axes (x east, y up, z north).
            #define TERRAIN_KEY     half3(-0.5, 0.70710678, 0.5)
            #define TERRAIN_AMBIENT 0.30h
            #define TERRAIN_DIFFUSE 0.70h
            #define TERRAIN_ALBEDO  0.75h

            half3 WmvAuthoredToLinear(half3 c)
            {
                half3 lo = c * (1.0h / 12.92h);
                half3 hi = pow(max((c + 0.055h) * (1.0h / 1.055h), 0.0h), 2.4h);
                return lerp(lo, hi, step(0.04045h, c));
            }

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                if (_WmvTerrainView > 2.5)
                {
                #if defined(UNITY_REVERSED_Z)
                    o.pos.z += _WmvLineNudge * o.pos.w;
                #else
                    o.pos.z -= _WmvLineNudge * o.pos.w;
                #endif
                }
                o.normal = UnityObjectToWorldNormal(v.normal);
                return o;
            }

            // A tint per chunk index: hues spread by the golden ratio, so neighbours differ.
            half3 ChunkTint(float index)
            {
                float h = frac(index * 0.61803399);
                return 0.55 + 0.45 * cos(6.2831853 * (h + float3(0.0, 0.33333333, 0.66666667)));
            }

            fixed4 frag (v2f i) : SV_Target
            {
                half3 v;
                if (_WmvTerrainView > 2.5)
                    v = _WmvLineColour.rgb;
                else
                {
                    half3 n = normalize(i.normal);
                    if (_WmvTerrainView > 1.5)
                        v = n * 0.5h + 0.5h;
                    else
                    {
                        half lit = TERRAIN_AMBIENT + TERRAIN_DIFFUSE * saturate(dot(n, TERRAIN_KEY));
                        half3 albedo = _WmvTerrainView > 0.5 ? ChunkTint(_WmvChunkIndex) : half3(TERRAIN_ALBEDO, TERRAIN_ALBEDO, TERRAIN_ALBEDO);
                        v = albedo * lit;
                    }
                }
                return fixed4(_WmvShaderEncode > 0.5 ? WmvAuthoredToLinear(v) : v, 1.0);
            }
            ENDCG
        }
    }
}
