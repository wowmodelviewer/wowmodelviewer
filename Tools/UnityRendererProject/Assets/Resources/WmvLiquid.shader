// WmvLiquid.shader -- Wmv/Liquid, the LIQUID material (stage 09, docs/world-viewer/09-liquids.md 1.5): one material per
// liquid type and scene (WmvLiquidRegistry), shared by every tile (MH2O) and world model (MLIQ) that draws the type.
//
// WHAT IT DRAWS is an APPROXIMATION, every part a documented choice (WmvLiquidMaterials). Three paths, the first that applies:
//
// THE HIGH PATH (Stage 09 tuning: the game's Liquid Detail High look -- clear water, the bed seen near the camera and fading
// with distance, gentle ripples, the scene reflected): _WmvWaterGrab 1 (a global: WmvWaterGrabPass copied the scene for this
// camera, Camera.main's world view) AND the transparent mode AND the kind water or ocean AND the camera on the sheet's upper
// side, by the TRIANGLE's facing (WmvUpperSide; LiquidHighMath.UpperSide): its up normal nUp = cross(ddy(P), ddx(P)) (the
// water point's screen derivatives, taken before any branch), turned so nUp.y >= 0, against the view ray: dot(V, nUp) <
// -1e-4 |nUp| -- a sloped sheet whose far part rises above the eye is still seen from above there; where the derivatives
// give no normal (|nUp| <= 1e-12) the view ray going down (-V.y > 1e-4) decides, and on flat water the two are the same
// test. The shading below still takes the world's up. The zone light is not needed. Per fragment, with P the water point
// (world, yards), V the unit view ray (from the camera's near-plane point through P: perspective and orthographic alike),
// uv the screen uv (the pixel times _WmvGrabTexel.xy) and S(uv) the scene point _WmvSceneDepth holds there, rebuilt through
// the inverse view-projection:
//     N      = the ripple normal: the type's ripple map (_BumpMap, a unit normal map, _HasBump) sampled at two scales
//              (_HiBumpTile: yards per repeat) scrolling in opposite directions (_HiBumpScroll yd/s on the liquid clock
//              _WmvLiquidTime), weighted _HiBumpWeightA and 1 - _HiBumpWeightA; the map's mean slope _HiBumpMean.xy (its
//              constant tilt, measured at upload: LiquidBump) taken out, n.xy - _HiBumpMean.xy n.z (the blend is linear:
//              the same as out of each sample's slope), BEFORE the slope is scaled by _HiBumpStrength x _HiBumpNorm (the
//              map's strength factor: its slope spread brought to the tuned map's, LiquidBump) -- a tiled map's constant
//              tilt neither shifts the bed on the screen nor turns the reflections. Sampled at the world xz plus
//              _WmvRippleOrigin (.xy scale A, .zw scale B: the floating origin's offset modulo each tile, computed in double
//              by WmvMain), so the ripples lie on the game's axes whatever the scene's origin; the map's (x, y, z) is the
//              world's (x, z, y). Without a ripple map: flat, (0, 1, 0)
//     uvR    = uv + N.xz _HiRefract saturate(|S(uv) - P| / _HiRefractRamp)     the refraction offset (none where it is thin)
//     uvB    = uvR when S(uvR) lies behind P along V AND not above the water (S(uvR).y <= P.y + 0.02): the bed; else uv --
//              an occluder in front, or the far bank, a rock or a trunk rising out of the water behind P
//     Ls     = |S(uvB) - P|                                    the straight path to the bed, yards
//     T      = min(1, _HiTFast.x exp(-Ls / _HiTFast.y) + _HiTSlow.x exp(-Ls / _HiTSlow.y))
//     W      = under a zone light (_WmvZoneLight 1): lerp(close, far, saturate(Ls / _DepthFull)) _HiWaterGain, close and
//              far the zone light's pair (the ocean pair for the ocean kind); without one: saturate(the type's
//              MinimapStaticCol x _HiNoZoneGain) -- never past 1
//     refr   = lerp(W, bed, T)                                 bed = _WmvSceneColor at uvB (the scene as it is)
//     R      = reflect(V, normalize(lerp((0, 1, 0), N, _HiReflBump))), marched from P over _WmvSceneDepth: 32 steps at
//              _HiReflMarch.x ((k + d) / 32)^2 yards (d a fixed 4 x 4 Bayer dither: pinned captures reproduce), the first step
//              behind the scene refined by 5 halvings, a hit when it lies within _HiReflMarch.y + _HiReflMarch.z x the scene
//              point's distance of it; the hit's colours (blur: _WmvSceneColorBlur, sharp: _WmvSceneColorRefl) fade to the sky
//              over _HiReflMarch.w of the screen at its edges. Both carry the scene premultiplied by its coverage (alpha 0
//              where the depth is the far plane: the background), so a texel reads c.rgb + sky (1 - c.a) -- bilinear and
//              blurred texels at a silhouette mix in the sky, never the camera's clear colour. A miss (off the screen or the
//              depth range, never behind, too thick) is _WmvReflectSky (the zone light's sky, or the neutral sky)
//     F      = _HiF0 + (1 - _HiF0) (1 - saturate(dot(-V, N)))^5                Schlick
//     rb     = _HiReflBroad F,  rd = _HiReflDetail F, both scaled by 1 / (rb + rd) when their sum passes 1
//     out    = float4(saturate(refr (1 - rb - rd) + blur rb + sharp rd), 1)   saturated (the authored 0..1: nothing the
//              water draws can bloom); alpha 1 through the premultiplied blend: opaque
//   No sparkle highlight: the frames are not sampled on this path. The TUNABLE values (_Hi*) are the material's
//   (WmvLiquidMaterials.ApplyHigh: the start values of Wow/World/LiquidHigh.cs, -wmvWaterTune; the property defaults
//   below are the same values). MEASURED at one spot (Southfury River, type 5, High, noon) where WmvLiquidMaterials.Tune
//   says so; INFERRED for every other type. While the High look is installed the transparent water and ocean materials
//   write depth (the output is opaque: a blended doodad or particle under the water is hidden by it, as the opaque bed
//   beside it fades); the zone and fallback paths of the same material then write depth too -- a camera BELOW the surface
//   sees the water through those blended paths, and a blended object above the water behind it is depth-rejected there
//   (a known gap, WmvLiquidMaterials.HighDepthWrite).
//
// THE ZONE PATH: _WmvZoneLight 1 (a global: the scene's zone light at the camera, WmvLiquidLight) AND the type's kind is
// water or ocean (_LiquidKind 0, 1) AND the transparent mode. The colours and alphas are the zone light's (globals, authored
// 0..1: the ocean pair for the ocean kind, the river pair for water), the constants the material's (MEASURED at one spot,
// Southfury River, type 5 Slow Water; INFERRED for every other type):
//     D      = uv1.y                                   the vertex's depth in yards, -1 where none is stored
//     k      = D < 0 ? 1 : saturate(D / _DepthFull)    no stored depth: deep
//     W      = lerp(close, far, k)
//     a      = lerp(shallow alpha, deep alpha, k)
//     h      = _ZoneHighlight * saturate((frame.a - _ZoneHighlightFloor) / (1 - _ZoneHighlightFloor))   (0 without frames)
//     out    = float4(a W + h, 1 - _BedFactor (1 - a))  premultiplied: after the blend a W + h + _BedFactor (1 - a) dst --
//                                                      the highlight added after the blend, neutral, the scene behind
//                                                      (the bed) darkened
// THE FALLBACK (every other transparent case: no zone light, or magma and slime on a transparent material): the type's own
// colour, exactly as before, written premultiplied:
//   _ColourFromFrame 0 (water, ocean)
//     colour  = saturate(_BaseColor.rgb + frame.a * _Highlight)   the base colour is the type's MinimapStaticCol; the water
//                                                                 and ocean frames are near-black and carry their ripples in
//                                                                 alpha (1.5)
//   _ColourFromFrame 1 (magma, slime)
//     colour  = frame.rgb                                  the magma and slime frames are fully coloured
//   _LiquidMode 0 (transparent: the type's LiquidMaterial flag 0x1)
//     alpha   = lerp(_ShallowAlpha, _DeepAlpha, uv1.x)    uv1.x is the vertex's depth byte / 255 (1 where none is stored)
//     out     = float4(colour * alpha, alpha)
//   _LiquidMode 1 (opaque)
//     out     = float4(colour, 1)                          depth written
//   No frames (_FrameCount 0: slot 0 names none, or a frame could not be had, or they are still on their way): the base
//   colour alone (and in the transparent mode the alpha by depth).
// The colour follows the type's KIND (_ColourFromFrame), the blending its material's FLAG 0x1 (_LiquidMode): 1.5 keeps the two
// apart, and Retail's "10FX" lava, fel and swamp (materials 13, 14, 18: kind magma, flag 0x1) are blended and take their
// frame's colour.
// BLENDING: transparent materials blend premultiplied colour, One / OneMinusSrcAlpha, and the alpha channel SrcAlpha /
// OneMinusSrcAlpha (as before), no depth write (a transparent water or ocean material while the High look is installed:
// depth written, above); opaque ones One / Zero for both, depth written (WmvLiquidMaterials).
// The frame: slice _Frame of the frame array, sampled at uv0 (water: world-planar coordinates on the game's axes; magma and
// slime: the stored u/v or s/t scaled -- Wow/World/TerrainLiquidMesh.cs, Wow/WmoLiquid.cs), Repeat, trilinear. _Frame is set
// on the shared material from the liquid clock once a frame (WmvLiquidRegistry.Tick).
// UNLIT: no light rig, shadow or fog (stages 10-12); reflection and refraction only on the HIGH path. Culling off: a liquid
// is one sheet, seen from above and below (its lower side: never the HIGH path).
//
// THE COLOUR DOMAIN, as the other world shaders (Wmv/TerrainTextured, Wmv/Particle): the frames are UNorm (never sRGB), the
// fragment writes AUTHORED values -- the blend runs on them -- and the frame decode pass converts the finished frame once;
// with _WmvShaderEncode set (the per-fragment display mode) or _WmvLinearOut (the A/B display modes without the authored
// domain) the straight colours are converted here before they are premultiplied: zone a toLinear(W) + toLinear(saturate(h)),
// fallback toLinear(colour) * alpha; HIGH: W and the sky converted before the mix (the grab already holds the frame's own
// domain).
//
// HLSLPROGRAM with URP's Core.hlsl, every switch a uniform (never a keyword: the player build strips variants), every
// per-material value in the UnityPerMaterial CBUFFER (the SRP Batcher's condition), the zone light's and the grab's globals
// outside it. No
// LightMode tag, as the other Wmv shaders: drawn by the ordinary pass. The queue and RenderType are the material's
// (WmvLiquidMaterials.CreateMaterial).

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
        _LiquidKind ("Kind (0 water, 1 ocean, 2 magma, 3 slime, -1 unknown): the zone path takes water and ocean", Float) = -1
        _Highlight ("Fallback: the frame's alpha added to the base colour (transparent mode)", Float) = 0.5
        _ShallowAlpha ("Fallback: opacity at depth byte 0 (transparent mode)", Float) = 0.45
        _DeepAlpha ("Fallback: opacity at depth byte 255, and where no depth is stored (transparent mode)", Float) = 0.8
        _DepthFull ("Zone path: the depth in yards at which far and deep are reached", Float) = 3.25
        _ZoneHighlight ("Zone path: the highlight's gain, added after the blend", Float) = 0.34
        _ZoneHighlightFloor ("Zone path: the frame alpha below which no highlight is added", Float) = 0.13
        _BedFactor ("Zone path: the darkening of the scene behind, as a share of (1 - alpha)", Float) = 0.75
        _BumpMap ("High: the ripple map (a unit normal map, linear), when _HasBump", 2D) = "grey" {}
        _HasBump ("High: 1 when a ripple map is bound (0: flat normals)", Float) = 0
        _HiRefract ("High: screen-uv offset per unit of the ripple normal's horizontal part", Float) = 0.02
        _HiRefractRamp ("High: the path (yd) over which that offset reaches full", Float) = 0.3
        _HiTFast ("High: transmission, fast term (amplitude, length yd)", Vector) = (1.09, 0.154, 0, 0)
        _HiTSlow ("High: transmission, slow term (amplitude, length yd)", Vector) = (0.38, 24.9, 0, 0)
        _HiWaterGain ("High: the water colour's gain (the zone light's colours)", Float) = 1.35
        _HiNoZoneGain ("High: the water colour's gain without a zone light (the type's colour, saturated)", Float) = 0.3125
        _HiReflBroad ("High: the broad (blurred) reflection's gain on Schlick", Float) = 0.2
        _HiReflDetail ("High: the detail (sharp) reflection's gain on Schlick", Float) = 0.02
        _HiF0 ("High: Schlick's F0", Float) = 0.02
        _HiReflBump ("High: the share of the ripple normal that tilts the reflected ray", Float) = 0.25
        _HiReflMarch ("High: the reflection march (length yd, hit thickness yd, + share of the distance, screen-edge fade)", Vector) = (128, 0.3, 0.02, 0.2)
        _HiBumpTile ("High: the ripple scales A and B (yd per repeat)", Vector) = (6, 20, 0, 0)
        _HiBumpScroll ("High: the ripple scroll (yd/s; A and B in opposite directions)", Float) = 0.05
        _HiBumpWeightA ("High: ripple A's weight (B: 1 - A)", Float) = 0.4
        _HiBumpStrength ("High: the ripple slope's scale", Float) = 2
        _HiBumpNorm ("High: the ripple map's strength factor (its slope spread to the tuned map's)", Float) = 1
        _HiBumpMean ("High: the ripple map's mean slope (x, y): its constant tilt, taken out before the strength", Vector) = (0, 0, 0, 0)
        _WmvLinearOut ("Convert the authored colour to linear", Float) = 0
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend (colour)", Float) = 1      // One (premultiplied)
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend (colour)", Float) = 10     // OneMinusSrcAlpha
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlendA ("Src blend (alpha)", Float) = 5      // SrcAlpha
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlendA ("Dst blend (alpha)", Float) = 10     // OneMinusSrcAlpha
        _ZWrite ("Depth write", Float) = 0
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 0              // Off
    }

    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent-1" "IgnoreProjector" = "True" }

        Cull [_Cull]
        ZWrite [_ZWrite]
        ZTest LEqual
        Blend [_SrcBlend] [_DstBlend], [_SrcBlendA] [_DstBlendA]

        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            TEXTURE2D_ARRAY(_Frames); SAMPLER(sampler_Frames);
            TEXTURE2D(_BumpMap); SAMPLER(sampler_BumpMap);

            CBUFFER_START(UnityPerMaterial)
                float _FrameCount;
                float _Frame;
                float4 _BaseColor;
                float _LiquidMode;
                float _ColourFromFrame;
                float _LiquidKind;
                float _Highlight;
                float _ShallowAlpha;
                float _DeepAlpha;
                float _DepthFull;
                float _ZoneHighlight;
                float _ZoneHighlightFloor;
                float _BedFactor;
                float _HasBump;
                float _HiRefract;
                float _HiRefractRamp;
                float4 _HiTFast;
                float4 _HiTSlow;
                float _HiWaterGain;
                float _HiNoZoneGain;
                float _HiReflBroad;
                float _HiReflDetail;
                float _HiF0;
                float _HiReflBump;
                float4 _HiReflMarch;
                float4 _HiBumpTile;
                float _HiBumpScroll;
                float _HiBumpWeightA;
                float _HiBumpStrength;
                float _HiBumpNorm;
                float4 _HiBumpMean;
                float _WmvLinearOut;
                float _SrcBlend;
                float _DstBlend;
                float _SrcBlendA;
                float _DstBlendA;
                float _ZWrite;
                float _Cull;
            CBUFFER_END

            // Globals, never properties (a property of the same name would pin them): the per-fragment display encode, and
            // the zone light at the camera (WmvLiquidLight: _WmvZoneLight 0 or 1; rgb colours and (shallow, deep) alphas).
            float _WmvShaderEncode;
            float _WmvZoneLight;
            float4 _WmvRiverClose, _WmvRiverFar, _WmvOceanClose, _WmvOceanFar;
            float4 _WmvWaterAlpha, _WmvOceanAlpha;
            // The HIGH path's globals: the grab (WmvWaterGrabPass: the scene's colour, the same premultiplied by its coverage
            // for the reflections and its blurred 1/4, its raw device depth, their texel size and the switch), the liquid
            // clock (seconds), the sky a missed reflection takes and the ripple map's offset for the floating origin.
            TEXTURE2D(_WmvSceneColor);
            TEXTURE2D(_WmvSceneColorRefl);
            TEXTURE2D(_WmvSceneColorBlur);
            TEXTURE2D(_WmvSceneDepth);
            float4 _WmvGrabTexel;
            float _WmvWaterGrab;
            float _WmvLiquidTime;
            float4 _WmvReflectSky;
            float4 _WmvRippleOrigin;

            #define WMV_MARCH_STEPS 32
            #define WMV_MARCH_REFINE 5
            // How far above the water point (yd) a shifted refraction sample may lie and still be the bed (LiquidHighMath).
            #define WMV_ABOVE_WATER 0.02
            // The HIGH path's side test (WmvUpperSide; LiquidHighMath.ViewDown, FacingMin): how far the view ray must point into
            // the sheet, and the up normal's length at or below which the derivatives give none.
            #define WMV_VIEW_DOWN 1e-4
            #define WMV_FACING_MIN 1e-12
            static const float kWmvBayer4[16] = { 0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0 };

            struct Attributes
            {
                float4 positionOS : POSITION;
                float2 uv0        : TEXCOORD0;   // texture coordinates
                float2 uv1        : TEXCOORD1;   // (depth byte / 255, depth in yards): (1, -1) where none is stored
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float2 uv0        : TEXCOORD0;
                float2 uv1        : TEXCOORD1;
                float3 positionWS : TEXCOORD2;   // the HIGH path's water point
            };

            float3 WmvAuthoredToLinear(float3 c)
            {
                float3 lo = c * (1.0 / 12.92);
                float3 hi = pow(max((c + 0.055) * (1.0 / 1.055), 0.0), 2.4);
                return lerp(lo, hi, step(0.04045, c));
            }

            // The screen uv of normalised device coordinates as the grab textures are laid out (the hardware's own
            // mapping: they are copies of this camera's attachments).
            float2 WmvNdcToUv(float2 ndc)
            {
            #if UNITY_UV_STARTS_AT_TOP
                return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
            #else
                return ndc * 0.5 + 0.5;
            #endif
            }

            // The grab's depth at `uv` as a normalised device depth (stored [0, 1] is [-1, 1] where the near plane is at -1).
            float WmvSceneDepthAt(float2 uv)
            {
                float raw = SAMPLE_TEXTURE2D_LOD(_WmvSceneDepth, sampler_PointClamp, uv, 0).r;
            #if UNITY_REVERSED_Z
                return raw;
            #else
                return lerp(UNITY_NEAR_CLIP_VALUE, 1.0, raw);
            #endif
            }

            // The scene point the grab's depth holds at `uv` (world).
            float3 WmvScenePoint(float2 uv)
            {
                return ComputeWorldSpacePosition(uv, WmvSceneDepthAt(uv), UNITY_MATRIX_I_VP);
            }

            // World point q on the screen: its uv and normalised device depth; false when it is behind the camera, off the
            // screen or outside the depth range.
            bool WmvProject(float3 q, out float2 uv, out float depth)
            {
                float4 c = mul(UNITY_MATRIX_VP, float4(q, 1.0));
                uv = float2(0.0, 0.0);
                depth = 0.0;
                if (c.w <= 1e-5)
                    return false;
                float3 ndc = c.xyz / c.w;
                uv = WmvNdcToUv(ndc.xy);
                depth = ndc.z;
                float zLo = min(UNITY_NEAR_CLIP_VALUE, UNITY_RAW_FAR_CLIP_VALUE), zHi = max(UNITY_NEAR_CLIP_VALUE, UNITY_RAW_FAR_CLIP_VALUE);
                return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && depth >= zLo && depth <= zHi;
            }

            // On one pixel's ray: does the point of device depth `rayDepth` lie beyond the scene's `sceneDepth`?
            bool WmvBehind(float rayDepth, float sceneDepth)
            {
            #if UNITY_REVERSED_Z
                return rayDepth < sceneDepth;
            #else
                return rayDepth > sceneDepth;
            #endif
            }

            // A reflection texel of the grab: premultiplied by its coverage (0 for the background), the sky filling the rest.
            float3 WmvReflected(float4 c, float3 sky)
            {
                return c.rgb + sky * (1.0 - saturate(c.a));
            }

            // Does the camera see the sheet's upper side (header)? The triangle's up normal -- the cross of the water point's
            // screen derivatives, turned so its y is not negative -- against the view ray; where the derivatives give no normal,
            // the view ray going down. On flat water the same as -V.y > WMV_VIEW_DOWN.
            bool WmvUpperSide(float3 V, float3 pdx, float3 pdy)
            {
                float3 nUp = cross(pdy, pdx);
                nUp = nUp.y < 0.0 ? -nUp : nUp;
                float len = length(nUp);
                if (len > WMV_FACING_MIN)
                    return dot(V, nUp) < -WMV_VIEW_DOWN * len;
                return -V.y > WMV_VIEW_DOWN;
            }

            // The HIGH path (header). wxz: the water point's world xz; dx, dy: its screen derivatives (taken before any branch).
            float4 WmvHighWater(Varyings i, float2 uv, float3 nearP, float3 V, bool ocean, bool encode, float2 wxz, float2 dx, float2 dy)
            {
                float3 P = i.positionWS;
                // The ripple normal, on the game's axes (the floating origin's offset per scale).
                float3 n = float3(0.0, 0.0, 1.0);
                if (_HasBump > 0.5)
                {
                    float2 shift = float2(0.6, 0.8) * (_HiBumpScroll * _WmvLiquidTime);
                    float2 tile = max(_HiBumpTile.xy, 1e-3);
                    float2 wa = wxz + _WmvRippleOrigin.xy, wb = wxz + _WmvRippleOrigin.zw;
                    float3 na = SAMPLE_TEXTURE2D_GRAD(_BumpMap, sampler_BumpMap, (wa + shift) / tile.x, dx / tile.x, dy / tile.x).xyz * 2.0 - 1.0;
                    float3 nb = SAMPLE_TEXTURE2D_GRAD(_BumpMap, sampler_BumpMap, (wb - shift) / tile.y, dx / tile.y, dy / tile.y).xyz * 2.0 - 1.0;
                    n = na * _HiBumpWeightA + nb * (1.0 - _HiBumpWeightA);
                    // The map's mean slope out (its constant tilt), then the strength scale.
                    n.xy = (n.xy - _HiBumpMean.xy * n.z) * (_HiBumpStrength * _HiBumpNorm);
                }
                float3 N = normalize(float3(n.x, max(n.z, 1e-3), n.y));

                // Refraction: the ripple normal's world xz as a screen offset; the shifted sample is the bed only when it lies
                // behind the water point and not above the water, else the unshifted one.
                // Offset on world xz (its screen direction turns with the camera's yaw): the view-space form collapsed the bed's wobble ~10x (MEASURED).
                float3 S = WmvScenePoint(uv);
                float2 uvR = uv + N.xz * (_HiRefract * saturate(length(S - P) / max(_HiRefractRamp, 1e-4)));
                float3 SR = WmvScenePoint(uvR);
                float2 uvB = uv;
                if (dot(SR - P, V) >= 0.0 && SR.y <= P.y + WMV_ABOVE_WATER)
                {
                    uvB = uvR;
                    S = SR;
                }
                float ls = length(S - P);
                float3 bed = SAMPLE_TEXTURE2D_LOD(_WmvSceneColor, sampler_LinearClamp, uvB, 0).rgb;
                float T = min(1.0, _HiTFast.x * exp(-ls / max(_HiTFast.y, 1e-4)) + _HiTSlow.x * exp(-ls / max(_HiTSlow.y, 1e-4)));
                // The water colour: the zone light's pair x _HiWaterGain, else the type's colour x _HiNoZoneGain, saturated.
                float3 W;
                if (_WmvZoneLight > 0.5)
                {
                    float3 colClose = ocean ? _WmvOceanClose.rgb : _WmvRiverClose.rgb;
                    float3 colFar = ocean ? _WmvOceanFar.rgb : _WmvRiverFar.rgb;
                    W = lerp(colClose, colFar, saturate(ls / _DepthFull)) * _HiWaterGain;
                }
                else
                    W = saturate(_BaseColor.rgb * _HiNoZoneGain);
                float3 sky = _WmvReflectSky.rgb;
                if (encode)
                {
                    W = WmvAuthoredToLinear(W);
                    sky = WmvAuthoredToLinear(sky);
                }
                float3 refracted = lerp(W, bed, T);

                // Reflection: a screen-space march over the grab's depth.
                float3 R = reflect(V, normalize(lerp(float3(0.0, 1.0, 0.0), N, _HiReflBump)));
                R = normalize(float3(R.x, max(R.y, 1e-3), R.z));
                uint2 px = (uint2)i.positionCS.xy & 3u;
                float dither = (kWmvBayer4[px.y * 4u + px.x] + 0.5) / 16.0;
                bool hit = false;
                float2 hitUv = uv;
                float tPrev = 0.0;
                bool wasBehind = false;
                [loop]
                for (int k = 0; k < WMV_MARCH_STEPS; k++)
                {
                    float f = (k + dither) / WMV_MARCH_STEPS;
                    float t = _HiReflMarch.x * f * f;
                    float2 uq;
                    float dq;
                    if (!WmvProject(P + R * t, uq, dq))
                        break;
                    bool behind = WmvBehind(dq, WmvSceneDepthAt(uq));
                    if (behind && !wasBehind)
                    {
                        float lo = tPrev, hi = t;
                        [unroll]
                        for (int r = 0; r < WMV_MARCH_REFINE; r++)
                        {
                            float mid = 0.5 * (lo + hi);
                            float2 um;
                            float dm;
                            if (WmvProject(P + R * mid, um, dm) && WmvBehind(dm, WmvSceneDepthAt(um)))
                                hi = mid;
                            else
                                lo = mid;
                        }
                        float3 q = P + R * hi;
                        float2 uh;
                        float dh;
                        if (WmvProject(q, uh, dh))
                        {
                            float3 sh = WmvScenePoint(uh);
                            if (length(q - sh) <= _HiReflMarch.y + _HiReflMarch.z * length(sh - nearP))
                            {
                                hit = true;
                                hitUv = uh;
                                break;
                            }
                        }
                    }
                    wasBehind = behind;
                    tPrev = t;
                }
                float3 reflBlur = sky, reflSharp = sky;
                if (hit)
                {
                    float2 edge = min(hitUv, 1.0 - hitUv);
                    float fade = saturate(min(edge.x, edge.y) / max(_HiReflMarch.w, 1e-4));
                    reflBlur = lerp(sky, WmvReflected(SAMPLE_TEXTURE2D_LOD(_WmvSceneColorBlur, sampler_LinearClamp, hitUv, 0), sky), fade);
                    reflSharp = lerp(sky, WmvReflected(SAMPLE_TEXTURE2D_LOD(_WmvSceneColorRefl, sampler_LinearClamp, hitUv, 0), sky), fade);
                }

                // Fresnel and the mix (the two shares at most 1 together), saturated; opaque through the premultiplied blend.
                float F = _HiF0 + (1.0 - _HiF0) * pow(1.0 - saturate(dot(-V, N)), 5.0);
                float rb = max(_HiReflBroad * F, 0.0), rd = max(_HiReflDetail * F, 0.0);
                float rs = rb + rd;
                if (rs > 1.0)
                {
                    rb /= rs;
                    rd /= rs;
                }
                return float4(saturate(refracted * (1.0 - rb - rd) + reflBlur * rb + reflSharp * rd), 1.0);
            }

            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionCS = TransformObjectToHClip(v.positionOS.xyz);
                o.uv0 = v.uv0;
                o.uv1 = v.uv1;
                o.positionWS = TransformObjectToWorld(v.positionOS.xyz);
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
                bool encode = _WmvShaderEncode > 0.5 || _WmvLinearOut > 0.5;
                bool transparent = _LiquidMode < 0.5;
                bool water = abs(_LiquidKind) < 0.5;
                bool ocean = abs(_LiquidKind - 1.0) < 0.5;
                // (The water point's screen derivatives before any branch: the ripple map is sampled inside one, and the
                // triangle's facing is taken from them.)
                float3 pdx = ddx(i.positionWS), pdy = ddy(i.positionWS);
                float2 wxz = i.positionWS.xz;
                float2 wdx = pdx.xz, wdy = pdy.xz;
                if (_WmvWaterGrab > 0.5 && transparent && (water || ocean))
                {
                    // The HIGH path (header), for a camera on the sheet's upper side.
                    float2 uv = i.positionCS.xy * _WmvGrabTexel.xy;
                    float3 nearP = ComputeWorldSpacePosition(uv, UNITY_NEAR_CLIP_VALUE, UNITY_MATRIX_I_VP);
                    float3 V = normalize(i.positionWS - nearP);
                    if (WmvUpperSide(V, pdx, pdy))
                        return WmvHighWater(i, uv, nearP, V, ocean, encode, wxz, wdx, wdy);
                }
                if (_WmvZoneLight > 0.5 && transparent && (water || ocean))
                {
                    // The zone path (header): colour and opacity by depth in yards, the highlight after the blend.
                    float depth = i.uv1.y;
                    float k = depth < 0.0 ? 1.0 : saturate(depth / _DepthFull);
                    float3 colClose = ocean ? _WmvOceanClose.rgb : _WmvRiverClose.rgb;
                    float3 colFar = ocean ? _WmvOceanFar.rgb : _WmvRiverFar.rgb;
                    float2 sd = ocean ? _WmvOceanAlpha.xy : _WmvWaterAlpha.xy;
                    float3 w = lerp(colClose, colFar, k);
                    float a = lerp(sd.x, sd.y, k);
                    float h = animated ? _ZoneHighlight * saturate((frame.a - _ZoneHighlightFloor) / (1.0 - _ZoneHighlightFloor)) : 0.0;
                    float3 hh = float3(h, h, h);
                    float3 rgb = encode ? a * WmvAuthoredToLinear(w) + WmvAuthoredToLinear(saturate(hh)) : a * w + hh;
                    return float4(rgb, 1.0 - _BedFactor * (1.0 - a));
                }
                // The fallback: the type's own colour as before, premultiplied (opaque: alpha 1, the colour as it is).
                float3 colour;
                if (_ColourFromFrame > 0.5)
                    colour = animated ? frame.rgb : _BaseColor.rgb;
                else
                    colour = _BaseColor.rgb + (animated ? frame.a * _Highlight : 0.0);
                float alpha = transparent ? lerp(_ShallowAlpha, _DeepAlpha, saturate(i.uv1.x)) : 1.0;
                // The authored 8-bit range (the frames and the base colour are stored bytes).
                colour = saturate(colour);
                if (encode)
                    colour = WmvAuthoredToLinear(colour);
                return float4(colour * alpha, alpha);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
