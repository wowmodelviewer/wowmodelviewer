// WmvWaterSky.hlsl -- Stage 10 step 8: the sky dome's colour in a direction, for the High water's reflections that miss the
// scene (docs/world-viewer/10-light-sky-fog.md 6.11; Wow/World/SkyDome.cs is the dome, its construction and grades, and
// SkyDome.TraceWedge the C# twin of WmvWaterSkyDir, equal to SkyDome.Trace on both tables within 1e-9 in the runner; the
// self-test's probe (Resources/WmvWaterSkyProbe.shader) holds this GPU function to SkyDome.Trace within 3e-3, as the sky's own
// fixture). Included by WmvLiquid.shader and the probe. Plain float HLSL, an include guard, no include of its own.
//
// THE DOME is camera-centred and world-aligned (Resources/WmvSky.shader turns it by the view's rotation only), so its colour
// depends on the direction alone. WmvWaterSkyDir(d): the ray from the dome's centre along d against the 10 triangles of the
// 15-deg longitude wedge d falls in -- the top fan's, the four ring quads' (a c b, b c d each) and the bottom fan's, their
// vertices from the profile (radius, height) at the wedge's two longitudes as SkyDome.Build makes them, in its order --, the
// nearest hit's barycentric colour (each vertex its slot's colour: the top pole SkyTop, rings 1-4 SkyMiddle, SkyBand1, SkyBand2,
// SkySmog, ring 5 and the bottom pole SkyFog or its lean to EndFog) and position, then the sky program's tint (SkyDome.Shade,
// the client's 2977085, PROVEN_BYTES): rgb = v + sat(x^3) mix(wBelow, wAbove, sat(50 z)) (C - v), x = sat(dot(normalize(hit),
// normalize(sun)) - a) / (1 - a) where a < 1 and x > 0, z the hit's raw table height. Each wedge's triangles cover exactly its
// longitudes; at its edges (rounding) the two neighbouring wedges are tried, and the final fall-back (not met for a direction of
// finite length) is the colour of the profile point nearest in elevation, unshaded.
// THE INPUTS are GLOBALS (never properties), published by WmvWorldLight.Sky from the SAME SkyInputs it gives the sky's
// material (one source: the sky and its reflection cannot disagree), only while the sky is drawn for that render
// (_WmvWaterSkyOn 1); colours AUTHORED (the table's byte / 255), as the sky's.
#ifndef WMV_WATER_SKY_HLSL
#define WMV_WATER_SKY_HLSL
float _WmvWaterSkyOn;                 // 1: the sky was drawn for this render and -wmvWaterSky is dome
float4 _WmvWaterSky0, _WmvWaterSky1, _WmvWaterSky2, _WmvWaterSky3, _WmvWaterSky4, _WmvWaterSky5;   // the six slot colours (rgb)
float4 _WmvWaterSkyTint;              // C (rgb), a (w; 1 or more: no tint)
float4 _WmvWaterSkyWeights;           // the tint's strength above (x) and below (y) the horizon ramp
float4 _WmvWaterSkySun;               // towards the sun (xyz, Unity world)
float4 _WmvWaterSkyRadius0, _WmvWaterSkyRadius1;   // the profile's radii: r0..r3, r4..r6 (SkyDome.Profile of the table drawn)
float4 _WmvWaterSkyHeight0, _WmvWaterSkyHeight1;   // the profile's heights: h0..h3, h4..h6

#define WMV_SKY_SEGMENTS 24
#define WMV_SKY_RINGS 5
#define WMV_SKY_POINTS 7
#define WMV_SKY_STEP 0.26179938779914943      // 2 pi / 24: 15 deg
#define WMV_SKY_TOL 1e-5                       // the barycentric tolerance (float; the C# twin's 1e-9 in double)

float WmvSkyRadius(int k)
{
    float4 a = _WmvWaterSkyRadius0, b = _WmvWaterSkyRadius1;
    return k == 0 ? a.x : k == 1 ? a.y : k == 2 ? a.z : k == 3 ? a.w : k == 4 ? b.x : k == 5 ? b.y : b.z;
}

float WmvSkyHeight(int k)
{
    float4 a = _WmvWaterSkyHeight0, b = _WmvWaterSkyHeight1;
    return k == 0 ? a.x : k == 1 ? a.y : k == 2 ? a.z : k == 3 ? a.w : k == 4 ? b.x : k == 5 ? b.y : b.z;
}

// The colour of profile point k (SkyDome.SlotOf: 0, 1, 2, 3, 4, 5, 5).
float3 WmvSkyPointColour(int k)
{
    return k <= 0 ? _WmvWaterSky0.rgb : k == 1 ? _WmvWaterSky1.rgb : k == 2 ? _WmvWaterSky2.rgb : k == 3 ? _WmvWaterSky3.rgb
         : k == 4 ? _WmvWaterSky4.rgb : _WmvWaterSky5.rgb;
}

// Profile point k at the longitude whose (cos, sin) is `cs` (the poles on the axis), as SkyDome.Build places it.
float3 WmvSkyVertex(int k, float2 cs)
{
    if (k == 0 || k == WMV_SKY_POINTS - 1)
        return float3(0.0, WmvSkyHeight(k), 0.0);
    float r = WmvSkyRadius(k);
    return float3(r * cs.x, WmvSkyHeight(k), r * cs.y);
}

// One wedge's ten triangles: the nearest hit's colour and position; false when none is met.
bool WmvSkyWedge(float3 d, int j, out float3 colour, out float3 hitPos)
{
    int j1 = j + 1 < WMV_SKY_SEGMENTS ? j + 1 : 0;              // (no integer modulus: the compiler warns)
    // The wedge's two longitudes' (cos, sin), once (the same values every vertex took before).
    float lon0 = j * WMV_SKY_STEP, lon1 = j1 * WMV_SKY_STEP;
    float2 cs0 = float2(cos(lon0), sin(lon0)), cs1 = float2(cos(lon1), sin(lon1));
    float bestT = 3.0e38;
    bool any = false;
    colour = float3(0.0, 0.0, 0.0);
    hitPos = float3(0.0, 0.0, 0.0);
    [loop]
    for (int t = 0; t < 2 * WMV_SKY_RINGS; t++)
    {
        int p0, q0, p1, q1, p2, q2;
        if (t == 0)
        {
            p0 = 0; q0 = j; p1 = 1; q1 = j; p2 = 1; q2 = j1;
        }
        else if (t == 2 * WMV_SKY_RINGS - 1)
        {
            p0 = WMV_SKY_RINGS; q0 = j; p1 = WMV_SKY_POINTS - 1; q1 = j; p2 = WMV_SKY_RINGS; q2 = j1;
        }
        else
        {
            int ring = 1 + ((t - 1) >> 1);
            if (((t - 1) & 1) == 0)
            {
                p0 = ring; q0 = j; p1 = ring + 1; q1 = j; p2 = ring; q2 = j1;          // a c b
            }
            else
            {
                p0 = ring; q0 = j1; p1 = ring + 1; q1 = j; p2 = ring + 1; q2 = j1;     // b c d
            }
        }
        float3 a = WmvSkyVertex(p0, q0 == j ? cs0 : cs1), b = WmvSkyVertex(p1, q1 == j ? cs0 : cs1), c = WmvSkyVertex(p2, q2 == j ? cs0 : cs1);
        float3 e1 = b - a, e2 = c - a;
        float3 pv = cross(d, e2);
        float det = dot(e1, pv);
        if (abs(det) < 1e-12)
            continue;
        float inv = 1.0 / det;
        float3 tv = -a;                                // the ray's origin (the dome's centre) minus a
        float u = dot(tv, pv) * inv;
        if (u < -WMV_SKY_TOL || u > 1.0 + WMV_SKY_TOL)
            continue;
        float3 qv = cross(tv, e1);
        float w = dot(d, qv) * inv;
        if (w < -WMV_SKY_TOL || u + w > 1.0 + WMV_SKY_TOL)
            continue;
        float dist = dot(e2, qv) * inv;
        if (dist <= 0.0 || dist >= bestT)
            continue;
        bestT = dist;
        any = true;
        float w0 = 1.0 - u - w;
        hitPos = w0 * a + u * b + w * c;
        colour = w0 * WmvSkyPointColour(p0) + u * WmvSkyPointColour(p1) + w * WmvSkyPointColour(p2);
    }
    return any;
}

// The sky program's tint on the interpolated colour v at the dome position `pos` (SkyDome.Shade).
float3 WmvWaterSkyShade(float3 v, float3 pos)
{
    float a = _WmvWaterSkyTint.w;
    if (a < 1.0)
    {
        float x = saturate(dot(normalize(pos), normalize(_WmvWaterSkySun.xyz)) - a) / (1.0 - a);
        if (x > 0.0)
        {
            float wt = lerp(_WmvWaterSkyWeights.y, _WmvWaterSkyWeights.x, saturate(50.0 * pos.y));
            return v + saturate(x * x * x) * wt * (_WmvWaterSkyTint.rgb - v);
        }
    }
    return v;
}

// The dome's colour (authored) in world direction `dirWorld` (any length above 0).
float3 WmvWaterSkyDir(float3 dirWorld)
{
    float3 d = dirWorld;
    float lon = abs(d.x) + abs(d.z) > 0.0 ? atan2(d.z, d.x) : 0.0;   // (a pole: atan2(0, 0) is not defined on every GPU)
    lon = lon < 0.0 ? lon + 6.283185307179586 : lon;
    int j = clamp((int)floor(lon / WMV_SKY_STEP), 0, WMV_SKY_SEGMENTS - 1);
    float3 colour, pos;
    // (HLSL's || does not short-circuit: each wedge is tried only while none was met, so a later one cannot overwrite a hit.)
    bool met = WmvSkyWedge(d, j, colour, pos);
    if (!met)
        met = WmvSkyWedge(d, j > 0 ? j - 1 : WMV_SKY_SEGMENTS - 1, colour, pos);
    if (!met)
        met = WmvSkyWedge(d, j + 1 < WMV_SKY_SEGMENTS ? j + 1 : 0, colour, pos);
    if (met)
        return WmvWaterSkyShade(colour, pos);
    // Not met for a direction of finite length: the profile point nearest in elevation, unshaded.
    float e = atan2(d.y, length(d.xz)), best = 1.0e9;
    int slot = 0;
    [unroll]
    for (int k = 0; k < WMV_SKY_POINTS; k++)
    {
        float r = WmvSkyRadius(k), h = WmvSkyHeight(k);
        float ek = r > 0.0 ? atan2(h, r) : (h >= 0.0 ? 1.5707963 : -1.5707963);
        if (abs(ek - e) < best)
        {
            best = abs(ek - e);
            slot = k;
        }
    }
    return WmvSkyPointColour(slot);
}
#endif
