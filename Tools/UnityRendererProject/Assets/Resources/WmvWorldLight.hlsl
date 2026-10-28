// WmvWorldLight.hlsl -- the outdoor light on world models and doodads (Stage 10 step 6, docs/world-viewer/10-light-sky-fog.md
// 6.6, 6.10, 6.15; Wow/World/ModelLight.cs is the CPU twin and holds the evidence).
//
// Included only by the world-lit variants' pass bodies, under WMV_WORLD_LIT (WmvOpaquePass.cginc, WmvWmoPass.cginc), so the
// original programs declare none of this. Plain float HLSL with an include guard, so CG and HLSL passes can both include it;
// its one include is step 7: the fog (WmvWorldFog.hlsl, plain float HLSL with no include of its own), so the variants get it
// under WMV_WORLD_LIT too.
//
// THE GLOBALS are WmvWorldLight's (Publish), the same the terrain's world-lit branch reads: GLOBALS, never properties (a
// property of the same name would pin them). _WmvWLOn is above 0.5 only for the main camera's world view while the light is
// shown (pinned 0 everywhere else); the colours are authored (the table's byte / 255); L points towards the light (Unity world,
// unit). _WmvWLModels: x the doodads' flat arm, y the world models' flat arm (both for the record), z the diagnostic view
// (0 none, 1 the albedo, 2 the raw normal: -wmvWorldLitView), w the fog on (step 7: 1 while the fog is drawn, 0 with -wmvFog=off
// or a client the fog is off for).
//
// THE LIGHT is the client's M2 and world-model hemisphere programs' common core (PROVEN_BYTES, executed; M2 combiners_opaque
// 2977063 p_1, world model material3_wmo 5446718 p_1): H and the wrap on the NORMALISED normal, N.L not saturated in the wrap;
// the direct term dp3_sat on the RAW interpolant; colour = sqrt((base (A + D))^2 + base^2 local) + the second directional term,
// with no saturate. Stage 10 holds the client's inputs at their neutral values (below), so the colour is base x (A + D).
#ifndef WMV_WORLD_LIGHT_HLSL
#define WMV_WORLD_LIGHT_HLSL
#include "WmvWorldFog.hlsl"
float  _WmvWLOn;           // GLOBALS, never properties (a property of the same name would pin them)
float4 _WmvWLModels;       // x doodads flat arm, y world models flat arm, z view (0 none, 1 albedo, 2 normal), w the fog on
                           // (step 7: 1 while the fog is drawn, 0 with -wmvFog=off or a client the fog is off for)
float4 _WmvWLLightDir;     // towards the light, Unity world, unit
float4 _WmvWLAmbient, _WmvWLHorizonAmbient, _WmvWLGroundAmbient, _WmvWLDirect;   // authored byte / 255

// The client's M2 / world-model programs (PROVEN_BYTES; sources in each line's comment):
#define WMV_WL_WRAP_BASE    0.7    // M2 2977063 p_1:219-222; world model 5446718 p_1:185-188
#define WMV_WL_WRAP_SCALE   0.4
#define WMV_WL_WRAP_HALF    0.5
// Stage 10's holds (plan 6.6, 6.15). Each is the client's own input at its neutral value.
#define WMV_WL_ATTENUATION  1.0
#define WMV_WL_SHADOW       1.0    // multiplies D ONLY in the M2 and world-model programs (2977063 p_203:199-212,
                                   // 5446718 p_17:164-176) -- not the terrain flat class's 0.7 + 0.3 x shadow
#define WMV_WL_AO           1.0
#define WMV_WL_LOCAL        0.0    // the local light: sqrt((base F)^2 + base^2 local), M2 p_1:278-279, WMO p_1:247-248
#define WMV_WL_SECOND_LIGHT 0.0    // the second directional term, added after the sqrt (M2 p_1:255-277, :302)

// The colour of the client's lit modes for `base`, with E = 0 (exterior MOCV deferred), K = 0 for the flat class, the
// override weight 0, the glow and desaturation 0.
float3 WmvWorldModelShade(float3 base, float3 nRaw, bool flatClass, float shadow, float ao, float local)
{
    float lit = saturate(dot(nRaw, _WmvWLLightDir.xyz));            // direct: dp3_sat on the RAW interpolant
    float3 d = _WmvWLDirect.rgb * lit * WMV_WL_ATTENUATION * shadow;
    if (flatClass)                                                  // the flat class, K = 0: for the record only
        return base * saturate(local + _WmvWLAmbient.rgb * ao + d); // (AO on the ambient: lighting mode 2, p_203:205-207)
    float3 n = normalize(nRaw);                                     // H and the wrap: normalised
    float nl = dot(n, _WmvWLLightDir.xyz);                          // NOT saturated
    float nu = n.y;
    float3 hor = any(_WmvWLHorizonAmbient.rgb != 0.0) ? _WmvWLHorizonAmbient.rgb : _WmvWLAmbient.rgb;
    float3 gnd = any(_WmvWLGroundAmbient.rgb != 0.0) ? _WmvWLGroundAmbient.rgb : _WmvWLAmbient.rgb;
    float3 h = nu >= 0.0 ? lerp(hor, _WmvWLAmbient.rgb, nu) : lerp(hor, gnd, -nu);
    float3 a = h * (WMV_WL_WRAP_BASE + WMV_WL_WRAP_SCALE * (WMV_WL_WRAP_HALF + WMV_WL_WRAP_HALF * nl)) * ao;
    float3 x = base * (a + d);
    return sqrt(x * x + base * base * local) + WMV_WL_SECOND_LIGHT;
}
#endif
