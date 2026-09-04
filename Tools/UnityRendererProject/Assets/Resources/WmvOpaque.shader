// The renderer's guaranteed fallback material, and the only one that can run an M2 combiner.
//
// A player build strips every shader no asset references, and a material built at runtime
// references nothing at build time -- so Shader.Find("Standard"), the URP/HDRP Lit names and even
// the default material a primitive is created with can ALL come back missing or as the magenta
// error shader. What is left is whatever Unity always includes, and those (Sprites/Default, the
// UI shaders) bake alpha blending, ZWrite Off and Cull Off into the pass. Those are not material
// properties, so nothing can turn them off, and an opaque WoW material renders as a stack of
// translucent shells.
//
// Anything under a Resources folder is always included in the build, so this file is the one
// shader the player can count on. It is deliberately plain: single pass, single variant, no
// pipeline-specific includes, and no lighting data from the engine (URP and HDRP do not feed the
// built-in light uniforms). The key light below is fixed in world space so the model shades the
// same way in every pipeline.
//
// EVERYTHING IS UNIFORM-DRIVEN, never a shader keyword: the blend state, the combiner, where the
// second texture unit takes its coordinates, and how the output alpha is built. A player build
// strips shader VARIANTS as readily as it strips whole shaders, so a keyword per material feature
// would be a way to lose them all. One variant, a handful of floats.

Shader "WMV/Opaque Textured"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _SecondTex ("Second texture (M2 unit 1)", 2D) = "black" {}

        // 0 = single texture;      1 = unit0 * unit1;          2 = unit0 * unit1 * 2;
        // 3 = mix(u0*u1, u0, u0.a); 4 = mix(u0, u1, u1.a);      12 = mix(u0*u1*2, u0, u0.a).
        // Which M2 pixel shader maps to which lives in WmvModelBuilder.PlanCombiner, next to the
        // reasoning; the shader only needs the arithmetic.
        _CombinerMode ("Combiner", Float) = 0
        // 1 on an additive batch: emitted light, which the preview rig must not dim. Declared
        // here as a PROPERTY and not only as a uniform -- Material.HasProperty answers from
        // this block, and the builder's SetFloat is guarded by it. (A first version declared
        // only the uniform, the guard was false for every material, and the bypass never ran.)
        _Emissive ("Emissive pass", Float) = 0
        // Where unit 1 samples: 0 = uv set 0, 1 = uv set 1, 2 = environment sphere map.
        _Unit1UV ("Unit 1 UV source", Float) = 2

        // The same question for UNIT 0, which used to have no answer: it always sampled mesh UV
        // set 0. A material whose vertex program is named Diffuse_Env, Diffuse_Env_T1,
        // Diffuse_Env_Env or Diffuse_EdgeFade_Env puts the ENVIRONMENT on unit 0, and sampling
        // that texture with mesh coordinates pins a reflection to the surface: the sheen slides
        // with the model instead of staying put as the view turns. The sphere map was already
        // generated for unit 1's sake; this lets unit 0 reach it.
        //
        // Default 0 (mesh UV set 0), NOT 2 like _Unit1UV, so a material that never sets this --
        // any pipeline fallback, any older path -- keeps exactly today's behaviour.
        _Unit0UV ("Unit 0 UV source", Float) = 0
        // How the M2 combiner builds its "discard alpha": 0 = 1, 1 = unit0.a, 2 = unit1.a,
        // 3 = unit0.a * unit1.a, 4 = unit0.a + unit1.a. Scaled by _AlphaScale (the x2 combiners).
        _AlphaMode ("Alpha source", Float) = 0
        _AlphaScale ("Alpha scale", Float) = 1
        // 1 when the blend mode ignores that alpha entirely (opaque and alpha-key both output 1).
        _OpaqueAlpha ("Force opaque alpha", Float) = 1

        _Color ("Tint", Color) = (1,1,1,1)
        _Cutoff ("Alpha cutoff", Range(0,1)) = 0.5
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2      // Back
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend", Float) = 1  // One
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend", Float) = 0  // Zero
        [Toggle] _ZWrite ("ZWrite", Float) = 1
    }

    SubShader
    {
        // No LightMode tag: the built-in pipeline draws this as a normal opaque pass, and URP
        // draws it through SRPDefaultUnlit, so one SubShader covers both.
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "IgnoreProjector" = "True" }

        Cull [_Cull]
        Blend [_SrcBlend] [_DstBlend]
        ZWrite [_ZWrite]

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_local _ _ALPHATEST_ON
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                float2 uv     : TEXCOORD0;
                float2 uv2    : TEXCOORD1;
            };

            struct v2f
            {
                float4 pos    : SV_POSITION;
                float2 uv     : TEXCOORD0;
                float3 normal : TEXCOORD1;
                float2 env    : TEXCOORD2;
                float2 uv1    : TEXCOORD3;
                half3  viewN  : TEXCOORD4;   // view-space normal: z is the facing ratio
                float3 wpos   : TEXCOORD5;   // world position, for the cast-shadow lookup
            };

            // ---- PREVIEW RIG CONSTANTS -------------------------------------------------
            // Every number the look depends on, in one block, so a tuning pass edits numbers
            // and nothing else. What each one does to the picture is measured by -wmvLightCheck.
            #define KEY_FLOOR   0.265h    // darkest a surface gets (ambient floor)
            #define KEY_GAIN    1.644h    // key light; FLOOR+GAIN deliberately exceeds 1.0
            #define FILL_GAIN   1.5h      // opposite low fill, shadow side only
            #define RIM_GAIN    0.0h      // edge separation, shadow side only
            #define SPEC_GAIN   3.0h      // preview highlight strength
            #define SPEC_TIGHT  49.215h   // preview highlight tightness
            #define KNEE        0.298h    // where the highlight roll-off starts
            // What the roll-off approaches. The curve only reaches its asymptote at infinity, so
            // with CEIL at exactly 1.0 nothing the rig produces can pass white, and a texel lands
            // on 255 only when it is bright enough to round there anyway. (An earlier value of
            // 0.97 kept one step of headroom below the point where a channel counts as clipped;
            // the measured cost of giving it up is 0.000 % clipping on every opaque test model.)
            #define CEIL        1.0h
            // Key and fill directions, in VIEW space: x right, y up, z toward the viewer. How far
            // the key sits off the view axis is what decides whether a model reads as modelled or
            // as evenly lit -- a key close to the axis lights everything the viewer can see by
            // roughly the same amount, which is a flat picture however bright it is. Note that
            // WmvShadowRig anchors the published key fully to the world's vertical (WorldAnchor
            // 1.0), so these two only shade in the fallback below; the rig's copies must match.
            #define KEY_DIR     half3(0.081, 0.858, 0.507)
            #define FILL_DIR    half3(0.059, 0.998, 0.032)
            // Cast shadows: how much of the key light an occluder removes (0 = shadows off),
            // and the blur radius of the shadow edge, in shadow-map texels.
            #define SHADOW_STRENGTH 1.0h
            #define SHADOW_SOFT     4.0h
            // Contact shadows: the strength of the screen-space near-field shadow, and how far
            // it reaches, as a fraction of the model's bounding radius. This is what puts the
            // hood's shadow ON the face right up to where they touch -- the shadow map's bias
            // makes it blind for the last few millimetres before a contact, and a shadow that
            // stops short of the contact line reads as floating.
            #define CONTACT_STRENGTH 1.0h
            #define CONTACT_RANGE    0.25h
            // ----------------------------------------------------------------------------

            sampler2D _MainTex;
            float4 _MainTex_ST;
            sampler2D _SecondTex;
            float _CombinerMode, _Unit1UV, _Unit0UV, _AlphaMode, _AlphaScale, _OpaqueAlpha;

            // 1 on an ADDITIVE batch. An additive pass is light the surface EMITS -- a lantern
            // flame, an eye glow, rune fire -- and multiplying emitted light by the preview rig
            // is wrong twice over: dimensionally (the rig models received light), and visibly,
            // because with the near-vertical anchored key a side-facing glow's lum is barely
            // above the ambient floor, so lanterns that blaze in the reference footage rendered
            // here as faint smudges at a fifth of their authored intensity. Emissive passes keep
            // their authored colour; only the roll-off still applies, so a stacked glow cannot
            // run away past white.
            float _Emissive;

            // Which preview light rig to use. A GLOBAL (Shader.SetGlobalFloat), not a material
            // property, so one draw of one model can be repeated under several rigs without
            // touching a single material. 0 is the shipped rig, so an unset global -- a player
            // that never asks -- gets the real thing regardless of initialisation order.
            float _WmvRig;

            // Replace every surface colour with this flat grey, so a render shows the LIGHT RIG
            // ALONE with no texture in it. -wmvLightCheck uses it to measure how much shape the
            // rig actually produces: on a real texture, light and paint arrive as one number and
            // there is no way to tell a rig that models form from one that lights everything flat.
            //
            // A VALUE, not a switch, and 0 means off. It has to be a mid grey: a white albedo is
            // already at the top of the range before the light touches it, so every rig with any
            // gain in it saturates and reports no range at all. 0.25 linear -- about sRGB 0.54 --
            // is a representative WoW texture value and sits where the roll-off does its shaping.
            float _WmvFlatAlbedo;

            // ---- CAST SHADOWS -----------------------------------------------------------
            // A depth map rendered from the key light's point of view (WmvShadowRig.cs): one
            // part of the model in front of another, as seen by the light, is exactly what a
            // cast shadow is, and no amount of normal-based lighting can produce it -- the
            // saddle rope across the mount's body needs to know what is BETWEEN the surface
            // and the light, not which way the surface faces.
            //
            // _WmvShadowValid gates everything and an unset global reads 0, so a build where
            // the rig never ran -- the TestStub, a fallback shader path, an old player --
            // renders exactly as before. The matrix maps world space to the light's clip
            // space; the biases are computed by the rig from the map's texel size, not tuned
            // by hand here.
            float     _WmvShadowValid;
            float4x4  _WmvShadowMatrix;
            sampler2D_float _WmvShadowMap;
            float     _WmvShadowTexel;        // 1 / map size
            float     _WmvShadowDepthBias;    // in [0,1] depth units
            float     _WmvShadowNormalBias;   // world units, along the surface normal

            // 1 = fully lit, 0 = fully occluded (before strength is applied). 3x3 PCF: nine
            // depth comparisons averaged, spread by "soft" texels, so the edge of the rope's
            // shadow is a small gradient instead of a hard stairstep.
            half WmvShadowFactor(float3 wpos, half3 nrmWorld, half soft)
            {
                if (_WmvShadowValid < 0.5h)
                    return 1.0h;

                // Push the sample point out along the normal before projecting: a surface
                // otherwise compares against its own depth and speckles ("acne"). The offset
                // scales with the map's texel footprint, so it is as small as it can be.
                float4 sp = mul(_WmvShadowMatrix,
                                float4(wpos + nrmWorld * _WmvShadowNormalBias, 1.0));
                // The light camera is orthographic, so w is 1 -- no divide needed.
                float2 uv = sp.xy * 0.5 + 0.5;
                if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
                    return 1.0h;              // outside the map: nothing recorded, so lit

                // No UV flip here -- but only because the matrix was built WITHOUT the
                // render-into-texture flip (see WmvShadowRig). On D3D the rasteriser's flip and
                // the sampler's top-down v cancel; a matrix that re-adds the flip mirrors every
                // lookup vertically, and the artefact is unmistakable once seen: the model's own
                // silhouette stamped upside-down across itself.

                float lit = 0.0;
                float r = _WmvShadowTexel * soft;
                [unroll]
                for (int y = -1; y <= 1; y++)
                    [unroll]
                    for (int x = -1; x <= 1; x++)
                    {
                        float stored = tex2D(_WmvShadowMap, uv + float2(x, y) * r).r;
                        // Depth convention differs per platform; UNITY_REVERSED_Z is the same
                        // switch the projection matrix was built under (GetGPUProjectionMatrix),
                        // so the two always agree.
            #if UNITY_REVERSED_Z
                        lit += (sp.z >= stored - _WmvShadowDepthBias) ? 1.0 : 0.0;
            #else
                        lit += ((sp.z * 0.5 + 0.5) <= stored + _WmvShadowDepthBias) ? 1.0 : 0.0;
            #endif
                    }
                return (half)(lit / 9.0);
            }
            // -----------------------------------------------------------------------------

            // ---- CONTACT SHADOWS --------------------------------------------------------
            // A depth buffer of the CURRENT view (WmvShadowRig renders it alongside the light's
            // map, from the viewer camera's pose with near/far pinched around the model), plus
            // the key direction in world space, so a fragment can march toward the light and ask
            // "is anything I can see standing in the way, within touching distance?".
            float     _WmvContactValid;
            float4x4  _WmvViewDepthMatrix;    // world -> the view-depth camera's clip space
            sampler2D_float _WmvViewDepth;
            float4    _WmvKeyDirWorld;        // toward the light; w unused
            float4    _WmvFillDirWorld;       // the sky fill, same handling; w unused
            float     _WmvModelRadius;        // world units; scales the march to the model
            float     _WmvContactEps;         // self-hit guard, [0,1] depth units
            float     _WmvContactThick;       // occluder thickness assumption, [0,1] depth units

            // 1 = unoccluded, down to 0 for a hard nearby occluder -- FRACTIONAL, not a
            // binary verdict. The first version returned 0 on the first hit, and the result
            // looked exactly as harsh and as pixelated as a binary function dithered by
            // per-pixel jitter must: at every shadow boundary, neighbouring pixels flipped
            // between fully dark and fully lit. Three things make it a gradient instead:
            //
            //   * each hit is WEIGHTED -- by a smooth window on the depth test, so there is no
            //     knife-edge at the guard or the thickness bound, and by how far along the ray
            //     the occluder sits, so an edge touching the surface darkens fully while one at
            //     the end of the range barely registers, which is what a real penumbra does;
            //   * the strongest hit wins (max), so the value varies continuously as an
            //     occluder recedes from a surface;
            //   * the jitter now dithers a CONTINUOUS value, which reads as fine shading
            //     rather than on/off speckle.
            //
            // The thickness bound still matters as much as the depth test: a depth buffer
            // records only front surfaces, and without it anything anywhere in front of the
            // ray would count as touching.
            half WmvContactFactor(float3 wpos, half3 nrmWorld, float range)
            {
                if (_WmvContactValid < 0.5h)
                    return 1.0h;

                float3 dir = _WmvKeyDirWorld.xyz;

                // Per-fragment phase for the steps, from the world position: deterministic,
                // and stable while the camera moves.
                float jitter = frac(dot(wpos, float3(37.9521, 41.4133, 45.9271)));

                const int STEPS = 12;
                float occ = 0.0;
                [loop]
                for (int s = 0; s < STEPS; s++)
                {
                    float t = (s + jitter) / STEPS;
                    // The normal push keeps the ray off its own surface; it grows with t
                    // because a surface curving toward the light drifts back under the ray.
                    // Both pushes scale with `range`, so CONTACT_RANGE also sets the blind
                    // zone next to a contact: at 0.25 R the clearance is 0.015-0.04 R along
                    // the normal, four times what the march was first validated with (0.06 R).
                    // Tightening it means anchoring these fractions to a fixed share of the
                    // model radius rather than to the reach -- a look change, not a fix.
                    float3 pw = wpos + nrmWorld * ((0.06 + 0.10 * t) * range)
                              + dir * (max(t, 0.02) * range);
                    float4 cp = mul(_WmvViewDepthMatrix, float4(pw, 1.0));
                    if (cp.w <= 0.001)
                        break;                       // marched behind the camera: stop
                    float2 uv = cp.xy / cp.w * 0.5 + 0.5;
                    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
                        break;                       // off-screen: nothing recorded out there
                    float rayZ = cp.z / cp.w;
                    float stored = tex2D(_WmvViewDepth, uv).r;
            #if UNITY_REVERSED_Z
                    // Reversed Z: nearer = larger.
                    float infront = stored - rayZ;
            #else
                    float infront = (rayZ * 0.5 + 0.5) - stored;
            #endif
                    float w = smoothstep(_WmvContactEps * 0.5, _WmvContactEps * 1.5, infront)
                            * (1.0 - smoothstep(_WmvContactThick * 0.6, _WmvContactThick,
                                                infront));
                    w *= 1.0 - 0.75 * t;             // near contacts dark, far ones faint
                    occ = max(occ, w);
                }
                return (half)(1.0 - occ);
            }
            // -----------------------------------------------------------------------------
            fixed4 _Color;
            fixed _Cutoff;

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                o.uv1 = v.uv2;
                o.normal = UnityObjectToWorldNormal(v.normal);
                o.wpos = mul(unity_ObjectToWorld, v.vertex).xyz;

                // ENVIRONMENT UNIT. The M2 vertex shader for a combiner material names where each
                // texture unit takes its coordinates from, and "Env" means the unit is fed by a
                // sphere map generated from the view-space normal -- not by any UV set stored in
                // the mesh. This reproduces fixed-function GL_SPHERE_MAP, which is what the legacy
                // viewport uses for the same unit.
                float3 viewPos = UnityObjectToViewPos(v.vertex);
                float3 viewNrm = normalize(mul((float3x3)UNITY_MATRIX_IT_MV, v.normal));
                float3 r = reflect(normalize(viewPos), viewNrm);
                float m = max(2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0)), 1e-4);
                // V is inverted relative to the GL formula: a BLP's rows are uploaded flipped so
                // that ordinary Unity UVs work, which puts this generated coordinate the other way
                // up from the OpenGL viewport's. Without the flip the sheen sits on the wrong side.
                o.env = float2(r.x / m + 0.5, 0.5 - r.y / m);

                // Free: viewNrm is already here for the sphere map. Its z is how square-on the
                // surface is to the camera (the rim), and the vector itself gives the highlight a
                // stable frame that does not move when the model is orbited.
                o.viewN = viewNrm;
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                // Unit 0's coordinate. Anything outside the three cases below falls back to mesh
                // UV set 0, which is what this sampled before the choice existed -- an unknown
                // source must not invent a coordinate.
                float2 uv0 = i.uv;
                if (_Unit0UV > 1.5 && _Unit0UV < 2.5)       uv0 = i.env;   // environment sphere map
                else if (_Unit0UV > 0.5 && _Unit0UV < 1.5)  uv0 = i.uv1;   // mesh UV set 1
                fixed4 t1 = tex2D(_MainTex, uv0);

                // Unit 1's coordinates, per the material's vertex shader name.
                float2 uv1 = (_Unit1UV < 0.5) ? i.uv : ((_Unit1UV < 1.5) ? i.uv1 : i.env);
                fixed4 t2 = tex2D(_SecondTex, uv1);

                // COLOUR. The M2 combiners this milestone implements are all products of the two
                // units, except pixel shader 12, whose second unit is masked by the FIRST unit's
                // alpha (that channel is a reflection mask on a creature skin, not transparency).
                fixed3 rgb = t1.rgb;
                if (_CombinerMode > 11.5)                       // 12
                    rgb = lerp(t1.rgb * t2.rgb * 2.0, t1.rgb, t1.a);
                else if (_CombinerMode > 3.5)                   // 4: decal
                    rgb = lerp(t1.rgb, t2.rgb, t2.a);
                else if (_CombinerMode > 2.5)                   // 3: masked modulate
                    rgb = lerp(t1.rgb * t2.rgb, t1.rgb, t1.a);
                else if (_CombinerMode > 1.5)                   // 2: unit0 * unit1 * 2
                    rgb = t1.rgb * t2.rgb * 2.0;
                else if (_CombinerMode > 0.5)                   // 1: unit0 * unit1
                    rgb = t1.rgb * t2.rgb;
                fixed4 c = fixed4(rgb * _Color.rgb, 1.0);

                // ALPHA. The combiner builds a "discard alpha" the blend mode then either uses as
                // the output opacity or only tests against. Opaque and alpha-key output 1 and the
                // key discards below the cutoff; every other mode outputs it.
                fixed a = 1.0;
                if (_AlphaMode > 3.5)      a = t1.a + t2.a;
                else if (_AlphaMode > 2.5) a = t1.a * t2.a;
                else if (_AlphaMode > 1.5) a = t2.a;
                else if (_AlphaMode > 0.5) a = t1.a;
                a = saturate(a * _AlphaScale);

                #ifdef _ALPHATEST_ON
                    clip(a - _Cutoff);
                #endif

                // PREVIEW LIGHT RIG.
                //
                // A model viewer is not a scene. The job is to show what a texture artist painted,
                // from a fixed angle, with both sides of the model readable -- not to simulate a
                // room. So this is a few cheap terms and no engine lighting at all (URP and HDRP
                // do not feed the built-in light uniforms, and a shader that read them would look
                // different per pipeline).
                //
                // WHY THE RIG IS SELECTABLE. Lighting is the one change here whose effect cannot
                // be read off a log, so it has to be measured -- and a measurement that compares
                // two SEPARATE BUILDS compares two of everything: two camera framings, two poses,
                // two pixel sets. That is how a rig carrying strictly more light came back
                // measuring darker. With the rig behind a global, -wmvLightCheck renders the same
                // model, at the same instant, through the same camera, and swaps only this one
                // number between passes; -wmvRig=N forces one rig for a visual A/B in the GUI.
                //
                //   0  shipped     the rig below
                //   1  legacy      what WMV shipped before this work: 0.45 + 0.75*ndl, nothing
                //                  else, hard-clipped by the framebuffer at 1.0
                //
                // 0 rather than 1 is the shipped rig deliberately: an unset global reads as 0.
                half3 n  = normalize(i.normal);      // world space -- the legacy rig used this
                half3 vn = normalize(i.viewN);       // view space  -- the current rig lives here
                half  rim = 1.0h - saturate(vn.z);

                // Resolve the rig constants once, up here, because rollKnee/rollCeil are needed
                // by the roll-off further down, outside the rig branch.
                half  kFloor = KEY_FLOOR,  kGain    = KEY_GAIN;
                half  fGain  = FILL_GAIN,  rGain    = RIM_GAIN;
                half  sGain  = SPEC_GAIN,  sTight   = SPEC_TIGHT;
                half  rollKnee = KNEE,     rollCeil = CEIL;
                half3 kDir   = KEY_DIR,    fDir     = FILL_DIR;
                half  shStr  = SHADOW_STRENGTH, shSoft = SHADOW_SOFT;
                half  cStr   = CONTACT_STRENGTH, cRange = CONTACT_RANGE;

                half  lum;
                half3 spec = 0.0h;
                if (_WmvRig > 0.5)
                {
                    // LEGACY, untouched: world-fixed key, nothing else.
                    lum = 0.45h + 0.75h * saturate(dot(n, normalize(half3(0.35, 0.80, -0.50))));
                }
                else
                {
                    // THE KEY'S VERTICAL IS ANCHORED TO THE WORLD; ONLY ITS TILT FOLLOWS
                    // THE CAMERA.
                    //
                    // The direction arrives from WmvShadowRig, which blends the camera-relative
                    // KEY_DIR toward world-up each frame -- one vector feeding the shading, the
                    // shadow camera and the contact march, so they cannot disagree. The blend is
                    // what the reference viewers measurably do (frame analysis of preview
                    // footage): orbiting a model there never flips its lit side, and a
                    // camera looking up from below finds the belly still dark. A fully
                    // camera-relative key -- this shader's previous behaviour -- passes the
                    // first test but fails the second: it swings under with the camera and
                    // lights the underside. A near-vertical world anchor passes both, and it is
                    // also why preview lighting looks so still: yaw barely changes any surface's
                    // angle to a vertical light.
                    //
                    // The view-space fallback below is for a player where the rig never ran (the
                    // TestStub, -wmvPlaceholder before a model): the world globals read zero
                    // there, and normalize(0) would paint the model black.
                    // The fill is a WRAP term -- a sky hemisphere, not a second sun. A plain
                    // dot(n, up) is zero on every vertical surface and every belly, which is
                    // exactly where the anchored key is also zero: the surfaces that need fill
                    // most were the only ones not getting it, and measured against reference
                    // footage of the same model our under-side sat at 0.163 versus their 0.208
                    // with everything else already matched. Half-wrapped, the fill grades from
                    // full strength on up-facing surfaces to half on vertical ones to nothing
                    // only on surfaces facing straight down.
                    half  ndl, fill;
                    half3 kw = _WmvKeyDirWorld.xyz;
                    if (dot(kw, kw) > 0.5h)
                    {
                        ndl  = saturate(dot(n, kw));
                        fill = saturate(0.5h + 0.5h * dot(n, _WmvFillDirWorld.xyz));
                    }
                    else
                    {
                        ndl  = saturate(dot(vn, normalize(kDir)));
                        fill = saturate(0.5h + 0.5h * dot(vn, normalize(fDir)));
                    }
                    half shadowSide = 1.0h - ndl;

                    // CAST SHADOWS.
                    //
                    // Two estimators, one question. The map sees the whole model but is blind
                    // for the last few millimetres before a contact (its bias); the screen-space
                    // march is exact at contact range but sees only what is on screen. Each has
                    // its own strength, and where both claim occlusion the darker verdict wins
                    // -- min(), not a product, because they are measuring the same light and an
                    // area both can see would otherwise be double-darkened along a rim.
                    // WHAT EACH SHADOW IS ALLOWED TO REMOVE -- a split that took three
                    // versions to get right.
                    //
                    // Key only (version one) went invisible when the light was anchored
                    // near-vertical: a face is a vertical surface, its ndl against an overhead
                    // key is ~0, and blocking a light a surface never received changes nothing
                    // -- the under-hood face stopped darkening at all. Both-shadows-take-
                    // everything (version two) restored it and then flattened whole models: an
                    // overhead light puts the MAP's shadow across everything below a cloak's
                    // shoulders, and removing sky fill and ambient over that whole span dropped
                    // a boss model's mean by a third.
                    //
                    // The split that works follows what each estimator actually knows. The map
                    // answers "does the KEY reach this point" -- a directional question, so it
                    // attenuates the key (and the key-driven highlight) and nothing else. The
                    // contact march answers "is something within touching distance overhead" --
                    // a NEAR-FIELD question, which is precisely what sky-and-ambient occlusion
                    // is, so it takes the fill and half the floor as well, but only within its
                    // short range. An under-hood face darkens because the hood is near it; a
                    // torso under a distant cloak keeps its sky light. The floor's half-bound
                    // means no shadow can push a surface below half ambient -- readable, never
                    // black. This matches how the reference viewers read: their under-hood
                    // darkness is local occlusion, not a blocked sun.
                    half castKey = 1.0h;                // the key's directional shadow
                    half occ = 1.0h;                    // near-field sky/ambient occlusion
                    if (shStr > 0.0h)
                        castKey = 1.0h - shStr * (1.0h - WmvShadowFactor(i.wpos, n, shSoft));
                    if (cStr > 0.0h)
                    {
                        half contact = WmvContactFactor(i.wpos, n, cRange * _WmvModelRadius);
                        occ = 1.0h - cStr * (1.0h - contact);
                        castKey = min(castKey, occ);    // whatever is that close blocks the key too
                    }

                    lum = kFloor * (0.5h + 0.5h * occ)
                        + kGain * ndl * castKey
                        + fGain * fill * shadowSide * occ
                        + rGain * rim  * rim * shadowSide;

                    // PREVIEW HIGHLIGHT, scaled by how bright the texture already is.
                    //
                    // Not a material property -- nothing in an M2 says "this is metal". But the
                    // art already encodes it: gold trim, gems and polished metal are painted
                    // bright, and leather and fur are not. Weighting a narrow highlight by the
                    // texture luminance therefore puts it on the buckles and the gems and leaves
                    // the hide alone, without a specular map and without pretending to be
                    // physically based.
                    // Tinted by the surface, not white. A white highlight is achromatic light
                    // added on top of a colour, which drags it toward grey -- measurably, and
                    // worst on the saturated gold and red trim the highlight is meant to show
                    // off. Multiplying by the texture colour instead keeps the glint the colour
                    // of the metal it is sitting on, so brightness goes up and saturation does
                    // not go down.
                    half3 alb   = _WmvFlatAlbedo > 0.0 ? (half3)_WmvFlatAlbedo : t1.rgb;
                    half3 hv    = normalize(half3(0.30, 0.55, 1.0));   // view-space half vector
                    half  ndh   = saturate(dot(vn, hv));
                    half  texLu = dot(alb, half3(0.2126h, 0.7152h, 0.0722h));
                    spec = sGain * pow(ndh, sTight) * texLu * alb * castKey;
                }

                if (_WmvFlatAlbedo > 0.0)
                    c.rgb = (half3)_WmvFlatAlbedo;

                // Shipped rig only: the legacy rig is a record of what WMV drew before this
                // work, additive batches included, and must stay byte-identical to it. (The
                // bypass leaking into the legacy path was measurable -- a model with glow
                // batches moved by +0.002 mean under -wmvRig=1 -- and it was also the proof
                // that the property finally reached the material.)
                if (_Emissive > 0.5h && _WmvRig < 0.5)
                {
                    lum  = 1.0h;
                    spec = 0.0h;
                }

                c.rgb = c.rgb * lum + spec;

                if (_WmvRig < 0.5)
                {
                    // SHOULDER, not a ceiling.
                    //
                    // Capping the light so the product can never pass 1.0 does stop whites blowing
                    // out -- by guaranteeing nothing is ever brighter than its own texture. White
                    // fur then reads grey, because that is what a white texture at 70 % light IS.
                    // So the light goes well past 1.0 and the top end is rolled off instead:
                    // below the knee nothing is touched, which is where the painted colour lives,
                    // and above it the curve bends over and approaches CEIL asymptotically.
                    //
                    // ON THE BRIGHTEST CHANNEL, NOT PER CHANNEL. Rolling each channel off
                    // separately compresses a bright channel harder than a dim one, which pulls
                    // the three together -- so the roll-off itself desaturates, worst exactly on
                    // the saturated reds and golds that are supposed to be the ones that pop.
                    // Rolling off the maximum and scaling all three by the same factor keeps
                    // every channel ratio, so hue and saturation come out untouched and only
                    // brightness is shaped. Measured: the per-channel form cost 0.008-0.012
                    // chroma against the old rig; this form does not.
                    half mx  = max(c.r, max(c.g, c.b));
                    half ov  = max(mx - rollKnee, 0.0h);
                    half rolled = min(mx, rollKnee)
                                + (rollCeil - rollKnee) * (1.0h - exp(-ov / (rollCeil - rollKnee)));
                    c.rgb *= mx > 0.0001h ? rolled / mx : 1.0h;
                }

                c.a = (_OpaqueAlpha > 0.5) ? 1.0 : a;
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
