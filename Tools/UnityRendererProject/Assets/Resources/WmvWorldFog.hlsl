// WmvWorldFog.hlsl -- Stage 10 step 7: the client's per-pixel fog, layer 0 (docs/world-viewer/10-light-sky-fog.md 4.0, 4.6,
// 6.9; Wow/World/WorldFog.cs is the CPU twin and holds the evidence and the table-to-record mapping, with grades).
// Included by WmvWorldLight.hlsl (the world-lit variants, under WMV_WORLD_LIT) and by WmvTerrainTextured.shader. Plain float
// HLSL, an include guard, no include of its own. THE SHAPE is the client's fog tail, read and executed from its programs
// (Retail terrain 2977247 p_1:126-271; the same tail in every M2 and world-model program; PROVEN_BYTES). THE RECORD is row k
// of layer 0 of the client's fog block (cb5[14 L + k]); WorldFog.Pack fills it each frame. GLOBALS, never properties.
#ifndef WMV_WORLD_FOG_HLSL
#define WMV_WORLD_FOG_HLSL
float4 _WmvWLFogR0, _WmvWLFogR1, _WmvWLFogR2, _WmvWLFogR3, _WmvWLFogR4, _WmvWLFogR5, _WmvWLFogR6;
float4 _WmvWLFogR7, _WmvWLFogR8, _WmvWLFogR9, _WmvWLFogR10, _WmvWLFogR11, _WmvWLFogR12, _WmvWLFogR13;
float4 _WmvWLEye;          // the main camera, Unity world (xyz): the client's fog position is camera-relative

#define WMV_FOG_END_FADE  1.4285715     // 1 / 0.7: p_1:190 (dx_6_0 literal 0x3FF6DB6DC0000000)
#define WMV_FOG_ART_LEAD  0.3           // p_1:211
#define WMV_FOG_ART_FADE  3.3333333     // 1 / 0.3: p_1:225 (dx_6_0 literal 0x400AAAAAA0000000)
static const float3 WMV_FOG_UP = float3(0.0, 1.0, 0.0);   // the program's up row (cb5[172], its fog space); Unity world up

// c.x x^3 + c.y x^2 + c.z x + c.w in the program's order (p_1:175-180)
float WmvFogCubic(float4 c, float x)
{
    float x2 = x * x;
    return (c.z * x + (c.x * (x * x2) + x2 * c.y)) + c.w;
}

// The record's visibility and height factor at the fog position p (camera-relative, yards), raw = |p|. Two arms and no
// early return, so both outputs are written on every path.
void WmvFogVisibility(float3 p, float raw, out float vis, out float hf)
{
    [branch]
    if (_WmvWLFogR1.x != 0.0)                         // the power path (p_1:236-244): raw distance, no height fog
    {
        vis = min(pow(max((_WmvWLFogR1.y - raw) * _WmvWLFogR1.z, 0.0), _WmvWLFogR1.w), 1.0);
        hf = 0.0;
    }
    else
    {
        float m = max(_WmvWLFogR8.w, 0.0);                                         // the vertical pull (p_1:141, 155-163)
        float d = length(p - WMV_FOG_UP * clamp(dot(WMV_FOG_UP, p), -m, m));
        float D = max(d - _WmvWLFogR0.w - _WmvWLFogR0.x, 0.0);                     // the two start terms (p_1:164-166)
        float u = 1.0 - saturate((dot(_WmvWLFogR2.xyz, p) + _WmvWLFogR2.w) * _WmvWLFogR3.w);   // the height plane (171-174)
        hf = 1.0 - saturate(WmvFogCubic(_WmvWLFogR9, u));                         // p_1:175-181
        float eMain = exp(-_WmvWLFogR0.z * D);                                     // p_1:183-186
        float eHeight = exp(-_WmvWLFogR4.x * D);                                   // p_1:167-170
        float leg = min(eMain + hf * (eHeight - eMain),                            // p_1:187-188
                        saturate((1.0 - d / _WmvWLFogR0.y) * WMV_FOG_END_FADE));   // the end fade (p_1:182, 189-191)
        vis = leg;
        [branch]
        if (_WmvWLFogR12.z < 1.0)                      // the polynomial path, mixed in by R12.z (p_1:192-233)
        {
            float tt = saturate((d - _WmvWLFogR12.y) / (_WmvWLFogR12.x - _WmvWLFogR12.y));
            float q = saturate(d / _WmvWLFogR12.x);
            float a10 = 1.0 - saturate(WmvFogCubic(_WmvWLFogR10, tt));
            float a11 = 1.0 - saturate(WmvFogCubic(_WmvWLFogR11, tt));
            float A = a10 + hf * (a11 - a10);
            float e = saturate(_WmvWLFogR0.y / _WmvWLFogR12.x) - WMV_FOG_ART_LEAD;
            float b10 = saturate(WmvFogCubic(_WmvWLFogR10, e));
            float b11 = saturate(WmvFogCubic(_WmvWLFogR11, e));
            float B = b10 + hf * (b11 - b10);
            float w = saturate((q - e) * WMV_FOG_ART_FADE);
            float art = min(A + w * (saturate(1.0 - (B + w * (1.0 - B))) - A), A);
            vis = art + _WmvWLFogR12.z * (leg - art);
        }
    }
    // The client's strength 1 - s (1 - vis) is held at s = 1 (the terrain's compiled-in value; the M2/WMO cb0[23].z per
    // material is UNKNOWN: WorldFog.cs). At s = 1 the expression is vis, so it is not written.
}

// The record's fog colour at p (p_1:246-265).
float3 WmvFogColour(float3 p, float raw, float hf)
{
    float c = saturate((raw - _WmvWLFogR13.w) / _WmvWLFogR6.w);                    // raw distance (p_1:246-247)
    float3 baseC = _WmvWLFogR3.xyz + (c * (c * c)) * (_WmvWLFogR4.yzw - _WmvWLFogR3.xyz);   // c^3 (p_1:248-249, 251)
    float3 hgtC = _WmvWLFogR6.xyz + c * (_WmvWLFogR13.xyz - _WmvWLFogR6.xyz);     // c (p_1:250)
    float3 fc = baseC + hf * (hgtC - baseC);                                       // p_1:252-253
    float x = raw > 0.0 ? saturate(saturate(dot(p / raw, _WmvWLFogR8.xyz)) - _WmvWLFogR5.x) : 0.0;  // p_1:254, 257
                                                     // (p / raw: p_1:88-90); the guard at the eye (raw 0) is explicit
    float s = x / (1.0 - _WmvWLFogR5.x);                                           // p_1:259-261
    float3 tinted = fc + (s * (s * s)) * (_WmvWLFogR7.x * (_WmvWLFogR5.yzw - fc)); // p_1:255-256, 262-264: NOT saturated
    return x > 0.0 ? tinted : fc;                    // a select, as p_1:258, 265: an undefined s where x is 0 (R5.x = 1:
                                                     // SunFogColor zero, 80.8 % of Retail keys) is discarded
}

// The fogged colour of `c` (rgba, after the light, the lobes and the alpha) at world position wpos. `kind` is the batch's
// fog target (WorldFog.KindOf): 0 the fog colour (the client's general path), 1 black, 2 white, 3 grey 0.5, 4 black and
// transparent (rgba x vis: the client's blend-word-4 path).
float4 WmvWorldFog(float4 c, float3 wpos, float kind)
{
    float3 p = wpos - _WmvWLEye.xyz;                 // fragment - camera, as the program's fog position (p_1:88, 135)
    float raw = length(p);
    float vis, hf;
    WmvFogVisibility(p, raw, vis, hf);
    float3 target = kind < 0.5 ? WmvFogColour(p, raw, hf)
                  : kind < 1.5 ? float3(0.0, 0.0, 0.0)
                  : kind < 2.5 ? float3(1.0, 1.0, 1.0)
                  : kind < 3.5 ? float3(0.5, 0.5, 0.5) : float3(0.0, 0.0, 0.0);
    // = the program's lerp(fc, lit, vis) (p_1:266-267). Not `precise`: that would mark every instruction computing c too (the
    // light, the lobes, the terrain's composite) and so change the programs outside the fog.
    float3 mixed = c.rgb * vis + target * (1.0 - vis);
    float4 o;
    o.rgb = vis < 1.0 ? mixed : c.rgb;               // a select: where vis is 1 (the near field) the input itself, in
                                                     // whatever form the compiler gives the mix
    o.a = (kind > 3.5 && vis < 1.0) ? c.a * vis : c.a;
    return o;
}
#endif
