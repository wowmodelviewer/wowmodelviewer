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
        // The M2's THIRD texture unit. Black by default, so a material that never binds one adds
        // exactly nothing: the lobe below is t2.rgb * t2.a, and both are 0 for the default.
        _ThirdTex ("Third texture (M2 unit 2)", 2D) = "black" {}

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

        // THE M2 TEXTURE TRANSFORM, per unit, as a 2x2 matrix plus an offset in THIS renderer's
        // UV space (the V axis is already flipped by the mesh builder; the animator conjugates the
        // WoW-space matrix by that flip, so nothing here has to know about it). Identity by
        // default, so a material nothing animates samples exactly as it did before this existed.
        _UvXf0 ("Unit 0 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff0 ("Unit 0 UV offset", Vector) = (0,0,0,0)
        _UvXf1 ("Unit 1 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff1 ("Unit 1 UV offset", Vector) = (0,0,0,0)
        _UvXf2 ("Unit 2 UV transform (m00,m01,m10,m11)", Vector) = (1,0,0,1)
        _UvOff2 ("Unit 2 UV offset", Vector) = (0,0,0,0)
        // Where unit 2 samples: 0 = uv set 0, 1 = uv set 1, 2 = environment sphere map. Default 0.
        // The M2 shader table decides this per material (Diffuse_T1_Env_T1 puts unit 2 on set 0);
        // nothing here assumes a coordinate the vertex-shader name did not name.
        _Unit2UV ("Unit 2 UV source", Float) = 0
        // 1 when this material's pixel shader adds the third unit as an authored luminous term.
        // Deliberately NOT _Emissive: that float bypasses the preview light entirely and already
        // carries UNLIT and blend modes 3/4. This one adds energy and changes nothing else.
        _FirstUnitLobe ("First-unit luminous lobe (ps24)", Float) = 0
        _FirstUnitWeight ("First-unit weight (that lobe's gain)", Float) = 1
        _SecondUnitLobe ("Second-unit luminous lobe", Float) = 0
        _SecondUnitWeight ("Second-unit weight (that lobe's gain)", Float) = 1
        _ThirdUnitLobe ("Third-unit luminous lobe", Float) = 0
        _ThirdUnitWeight ("Third-unit weight (the lobe's gain)", Float) = 1
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
            // ---- RETAIL M2 LIGHTING, STRUCTURE AND MAGNITUDES ------------------------------
            //
            // THE STRUCTURE IS RETAIL-DERIVED, read out of the shader the game actually runs for
            // this material: combiners_uber_3_3.bls (FileDataID 5221414), program 0, the lit path
            // at uber33_0.asm:338-398. In full:
            //
            //   band    = N.up >= 0 ? lerp(horizon, zenith, N.up)
            //                       : lerp(horizon, ground, -N.up)          three-band sky
            //   ambient = band*0.7 + band*0.4*(0.5 + 0.5*N.L)               == band*(0.9 + 0.2*N.L)
            //   direct  = saturate(N.L) * lightColour * rangeAttenuation    ONE directional
            //   light   = ambient + direct ;  colour = albedo * light
            //
            // and there is NO SPECULAR and NO SECOND DIRECTIONAL anywhere in it. The 0.7 and 0.4
            // are literals in the bytecode (`mul r13, r13, l(0.4,...)`, `mad r4, r12, l(0.7,...),
            // r13`); the wrapped term is `mad r1.w, r0.x, l(0.5), l(0.5)`; the direct term is
            // `dp3_sat r1.w, v4.xyzx, -cb8[3].xyzx`.
            //
            // THE MAGNITUDES ARE NOT RETAIL-DERIVED, and are labelled below. Retail reads its
            // band and light colours from scene lighting constants (cb8) that are set per draw by
            // CPU code we cannot read -- Wow.exe's .text is encrypted on disk. So these are
            // placeholders chosen for PROVENANCE rather than by eye:
            //
            //   AMB_* = 0.20 and LIGHT_LEVEL = 0.80 are the OpenGL fixed-function MATERIAL
            //   defaults (GL_AMBIENT 0.2, GL_DIFFUSE 0.8) that this repository's own legacy
            //   viewport relies on -- it never sets either for the M2 path, so those defaults are
            //   what it has always drawn with. They also put a fully lit surface at
            //   0.20*1.1 + 0.80 = 1.02, which is where retail's ambient+light structure sits.
            //   IN-REPO PROVENANCE, NOT RETAIL-DERIVED.
            //
            //   The three bands are set EQUAL because no sourced sky colours exist for a preview.
            //   The three-band SHAPE is real and wired; the colours are the placeholder. Giving
            //   them real values later is one edit per line, and they should become float3 when
            //   that happens -- retail's are colours, these are greys.
            //   THE MAGNITUDES NOW COME FROM THIS APPLICATION'S OWN LEGACY RIG, and they had to
            //   move when the working space was fixed. 0.20 / 0.80 were the OpenGL fixed-function
            //   MATERIAL defaults, and they were chosen while this shader still evaluated its
            //   light in LINEAR -- where an ambient floor of 0.20 displays as 0.48 of albedo. In
            //   the authored domain the same 0.20 displays as 0.20, and every model went muddy:
            //   measured on the first render after the domain fix, horse3's peak fell from 108 to
            //   89 and item 281566's from 136 to 112.
            //
            //   The legacy rig -- lum = 0.45 + 0.75*N.L, WmvOpaque.shader's own -wmvRig=1 arm --
            //   was authored for and validated in exactly this domain, in this repository, over
            //   years of use. Its envelope is 0.45 .. 1.20. These constants reproduce that
            //   envelope (0.405 .. 1.245) while keeping RETAIL'S STRUCTURE: three ambient bands
            //   selected by the normal's up-component, wrapped by N.L, plus one directional.
            //   In-repo provenance for the numbers, retail provenance for the shape. Neither is
            //   fitted to a screenshot.
            //   THE BANDS ARE GRADED, AND THAT IS WHERE THE SHADOW SIDE COMES BACK.
            //
            //   Setting all three equal threw away the only directional freedom retail's structure
            //   has. It also left the preview measurably darker than what this application drew
            //   before the working space was fixed: on the validation set a model with no emissive
            //   content lost 28 % of its mean (horse3 76.75 -> 55.20, item 281566 70.94 -> 51.05),
            //   because an ambient of 0.265 evaluated in LINEAR displays as 0.552 of albedo while
            //   0.405 evaluated in the AUTHORED domain displays as 0.405 -- the same-looking
            //   number, a quarter less light -- and because the old rig's second fill light, which
            //   lit precisely the surfaces the key misses, went away with it.
            //
            //   Retail's own answer to that is the three bands. The key here is anchored near
            //   world-vertical, so a surface the key misses is facing sideways or down, and those
            //   are exactly the surfaces the HORIZON and GROUND bands serve. Grading them upward
            //   restores the shadow-side readability the fill used to give WITHOUT adding a second
            //   sun that retail does not have. Measured on the same set: within 12-14 % of the old
            //   rig's mean on unlit-content models and within 2 % on the benchmark shoulder, with
            //   more surface form than the old rig, which was flatter.
            //
            //   These magnitudes remain PLACEHOLDERS. Retail reads its band colours from scene
            //   lighting constants set per draw by CPU code we cannot read, and a preview scene
            //   chooses them for readability rather than realism. The SHAPE is retail's; the
            //   numbers are a preview-exposure choice, judged on the validation renders. They are
            //   greys, and should widen to float3 whenever real colours turn up.
            //
            //   LIGHT_LEVEL is derived, not chosen: 1.25 - AMB_ZENITH * 1.1, which holds the lit
            //   peak at 1.25 (the legacy rig's envelope topped out at 1.20) so raising the floor
            //   cannot blow the highlights.
            #define AMB_ZENITH  0.60h     // placeholder magnitude, graded -- see above
            #define AMB_HORIZON 0.90h     // placeholder magnitude, graded -- see above
            #define AMB_GROUND  1.05h     // placeholder magnitude, graded -- see above
            #define LIGHT_LEVEL 0.59h     // derived: 1.25 - AMB_ZENITH * 1.1
            #define AMB_BASE    0.7h      // RETAIL-DERIVED literal
            #define AMB_WRAP    0.4h      // RETAIL-DERIVED literal
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
            sampler2D _ThirdTex;
            float _CombinerMode, _Unit1UV, _Unit0UV, _AlphaMode, _AlphaScale, _OpaqueAlpha;
            // 1 = this shader works in the AUTHORED domain and encodes once at the end (shipped).
            // 0 = the old behaviour, kept only so the two can be rendered from one build. See the
            // block above frag() and WmvMain.ConfigureDisplayTransform.
            float _WmvAuthoredDomain;
            // 1 = convert to linear at the end of this fragment (the behaviour before the frame
            // decode existed; WMV_DISPLAY=fragment). 0 = write the authored value and let
            // WmvFrameDecodePass convert the finished frame once, so blending happens in the
            // authored domain like the references. Set by ConfigureDisplayTransform.
            float _WmvShaderEncode;
            float _Unit2UV, _ThirdUnitLobe, _ThirdUnitWeight, _SecondUnitLobe, _SecondUnitWeight;
            float _FirstUnitLobe, _FirstUnitWeight;
            float4 _UvXf0, _UvOff0, _UvXf1, _UvOff1, _UvXf2, _UvOff2;

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

            // ---- THE WORKING SPACE ------------------------------------------------------
            //
            // Everything below -- the combiner, the light multiply, the authored additive glow --
            // is computed on the values the ARTIST PAINTED: the bytes stored in the BLP,
            // undecoded. That is not a preference, it is what both renderers of record do.
            //
            //   retail  the M2 uber combiner (combiners_uber_3_3.bls, FileDataID 5221414) never
            //           linearises a model texture. The only transfer-function operations in the
            //           whole program are an sRGB decode immediately followed by an sRGB encode,
            //           applied by hand to ONE sample to build a luminance coordinate for a
            //           recolour ramp. The decode would be redundant if the resource views were
            //           sRGB, and the encode back would be wrong if the shader worked in linear.
            //   legacy  this application's own OpenGL viewport uploads GL_RGBA8, never GL_SRGB*,
            //           and never enables GL_FRAMEBUFFER_SRGB. Fixed-function lighting multiplies
            //           the stored bytes directly.
            //
            // The Unity project is in LINEAR colour space, so the framebuffer holds linear light
            // and the swapchain applies the sRGB curve on write. The textures are therefore
            // uploaded UNDECODED (WmvModelBuilder.CreateTexture, which passes linear:true -- in
            // Unity's vocabulary that means "hand the shader the stored value"), the whole shader
            // runs in the authored domain, and the result is converted ONCE, here at the end, by
            // the exact sRGB EOTF. That is the inverse of what the swapchain is about to apply,
            // so the round trip is exact, and bloom downstream still sees real linear light.
            //
            // WHY THIS IS NOT BOOKKEEPING. An additive term is not scale-invariant between the two
            // domains. An authored emissive of 0.8 over a base lit at 0.55 reaches 253/255 added
            // in the authored domain and 212/255 added in linear -- and 173/255 once the Neutral
            // tone curve has had it as well. "The glow does not read like the game" IS that gap.
            //
            // WHERE THE CONVERSION NOW HAPPENS. Converting at the end of each fragment closed the
            // gap inside a fragment and left it open between fragments: the hardware blend then
            // ran on linear numbers, while Wowhead's viewer, the legacy OpenGL viewport and the
            // game all blend the authored values themselves. Two additive layers authored at 0.5
            // reach 255 in the references and 175 summed in linear. So the shipped path writes
            // the authored value from this fragment and WmvFrameDecodePass converts the finished
            // frame once, before post-processing; this function is kept for WMV_DISPLAY=fragment,
            // the per-fragment behaviour, so the two can be differenced from one build.
            //
            // The exact curve, not the pow(2.2) approximation: it has to invert the swapchain's
            // encode exactly, or every mid-tone shifts. Values above 1 are deliberately left
            // unclamped so an authored glow can still blow out and can still feed bloom.
            half3 WmvAuthoredToLinear(half3 c)
            {
                half3 lo = c * (1.0h / 12.92h);
                half3 hi = pow(max((c + 0.055h) * (1.0h / 1.055h), 0.0h), 2.4h);
                return lerp(lo, hi, step(0.04045h, c));
            }

            fixed4 frag (v2f i) : SV_Target
            {
                // Unit 0's coordinate. Anything outside the three cases below falls back to mesh
                // UV set 0, which is what this sampled before the choice existed -- an unknown
                // source must not invent a coordinate.
                float2 uv0 = i.uv;
                bool unit0Env = (_Unit0UV > 1.5 && _Unit0UV < 2.5);
                if (unit0Env)                               uv0 = i.env;   // environment sphere map
                else if (_Unit0UV > 0.5 && _Unit0UV < 1.5)  uv0 = i.uv1;   // mesh UV set 1
                // The M2 texture transform, applied AFTER the source is chosen and only to a
                // STORED coordinate. A sphere-map coordinate is generated from the view-space
                // normal and is not something the model authored a scroll for: the legacy
                // viewport skips the texture matrix on an environment unit for exactly that
                // reason (Source/games/wow/ModelRenderPass.cpp:623-636), and the animator never
                // binds one to such a unit either. This branch is the second lock on that door.
                if (!unit0Env)
                    uv0 = float2(dot(_UvXf0.xy, uv0), dot(_UvXf0.zw, uv0)) + _UvOff0.xy;
                fixed4 t1 = tex2D(_MainTex, uv0);

                // Unit 1's coordinates, per the material's vertex shader name.
                bool unit1Env = (_Unit1UV >= 1.5);
                float2 uv1 = (_Unit1UV < 0.5) ? i.uv : ((_Unit1UV < 1.5) ? i.uv1 : i.env);
                if (!unit1Env)
                    uv1 = float2(dot(_UvXf1.xy, uv1), dot(_UvXf1.zw, uv1)) + _UvOff1.xy;
                fixed4 t2 = tex2D(_SecondTex, uv1);

                // Unit 2, sampled only where a combiner asked for it. Its coordinate comes from
                // the same resolved vertex-shader name as the other two, and its texture carries
                // its OWN address mode (set on the Texture2D by the builder), so a unit that
                // clamps still clamps even when it shares an image with a unit that repeats.
                fixed4 t3 = fixed4(0, 0, 0, 0);
                if (_ThirdUnitLobe > 0.5)
                {
                    bool unit2Env = (_Unit2UV >= 1.5);
                    float2 uv2 = (_Unit2UV < 0.5) ? i.uv : ((_Unit2UV < 1.5) ? i.uv1 : i.env);
                    if (!unit2Env)
                        uv2 = float2(dot(_UvXf2.xy, uv2), dot(_UvXf2.zw, uv2)) + _UvOff2.xy;
                    t3 = tex2D(_ThirdTex, uv2);
                }

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
                //
                // Mode 5 is ps17's, and it is the only one that reads the lobe texture's COLOUR
                // into the alpha: tex1.a + tex2.a * luminance(tex2), with the gamma-domain NTSC
                // weights (0.30, 0.59, 0.11). Retail spells it `dp3 r1.y, r6.xyzx, l(0.3, 0.59,
                // 0.11)` then `mad r4.w, r6.w, r1.y, r3.w`; the legacy has the same three
                // constants at ModelRenderPass.cpp:124. Those are the NTSC luma weights, not the
                // linear Rec.709 ones, which is the correct pair for a shader working on authored
                // values -- see the WORKING SPACE block above.
                fixed a = 1.0;
                if (_AlphaMode > 4.5)      a = t1.a + t2.a * dot(t2.rgb, fixed3(0.30, 0.59, 0.11));
                else if (_AlphaMode > 3.5) a = t1.a + t2.a;
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

                // Resolve the rig constants once, up here.
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
                    // ndlSigned keeps the sign for retail's WRAPPED ambient; ndl is the
                    // saturated one the directional uses. The fill direction is no longer read:
                    // retail has one directional light, and the second fill contribution is gone.
                    half  ndl, ndlSigned;
                    half3 kw = _WmvKeyDirWorld.xyz;
                    if (dot(kw, kw) > 0.5h)
                        ndlSigned = dot(n, kw);
                    else
                        ndlSigned = dot(vn, normalize(kDir));
                    ndl = saturate(ndlSigned);
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

                    // RETAIL'S THREE-BAND AMBIENT plus ONE DIRECTIONAL. See the header block.
                    //
                    // ndl here is the SIGNED dot product, because retail's ambient term wraps it
                    // (0.5 + 0.5*N.L) rather than saturating -- that is what lets a surface
                    // facing away from the light still receive the ground band instead of
                    // dropping to a flat floor. The DIRECT term saturates, as retail does.
                    //
                    // The two shadow estimators keep the roles they already had: the cast shadow
                    // is a directional question and attenuates the directional light; the contact
                    // march is a near-field occlusion question and attenuates the ambient.
                    half nUp  = dot(n, half3(0.0h, 1.0h, 0.0h));
                    half band = (nUp >= 0.0h) ? lerp(AMB_HORIZON, AMB_ZENITH, nUp)
                                              : lerp(AMB_HORIZON, AMB_GROUND, -nUp);
                    half ambient = band * (AMB_BASE + AMB_WRAP * (0.5h + 0.5h * ndlSigned));
                    half direct  = ndl * LIGHT_LEVEL;
                    lum = ambient * occ + direct * castKey;

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
                    // THE PREVIEW HIGHLIGHT IS GONE. Retail's M2 lit path has no specular
                    // term of any kind -- there is no glint anywhere in uber33_0.asm's lit
                    // branch, for ps15 or for any other combiner it serves. spec stays declared
                    // and zero so the final combine below is untouched.
                }

                if (_WmvFlatAlbedo > 0.0)
                    c.rgb = (half3)_WmvFlatAlbedo;

                // Shipped rig only: the legacy rig is a record of what WMV drew before this
                // work, additive batches included, and must stay byte-identical to it. (The
                // bypass leaking into the legacy path was measurable -- a model with glow
                // batches moved by +0.002 mean under -wmvRig=1 -- and it was also the proof
                // that the property finally reached the material.)
                bool rigApplied = true;
                if (_Emissive > 0.5h && _WmvRig < 0.5)
                {
                    lum  = 1.0h;
                    spec = 0.0h;
                    rigApplied = false;   // no preview light in this pass -- see the roll-off below
                }

                c.rgb = c.rgb * lum + spec;

                // THE THIRD UNIT'S LUMINOUS LOBE -- ADDED HERE, AFTER THE LIGHT, ON PURPOSE.
                //
                // For Combiners_Opaque_Mod2xNA_Alpha_Add the model authors a second term beside
                // the diffuse one, and both renderers of record add it to the SHADED colour
                // rather than through it:
                //
                //   retail  combiners_uber_3_3.bls, case 15 -- r7 = t2.rgb * t2.a * cb0[6].z is
                //           written inside the switch and read exactly once, at
                //           "add r2.xyz, r7.xyzx, r0.xzwx", where r0 already holds the lit,
                //           post-sqrt colour. Nothing multiplies r7 by light colour, by N.L, by
                //           ambient or by the vertex colour.
                //   legacy  ModelRenderPass.cpp:156 -- mat_diffuse + specular * u_specular_weight,
                //           where mesh_color (the lit fixed-pipeline colour, :99) appears only in
                //           the mat_diffuse arms and never in a "specular" assignment.
                //
                // So it goes after the lum multiply. Putting it before would let the preview rig
                // dim an authored glow, which is the whole defect this restores.
                //
                // SCALE 1.0. Retail scales by the runtime constant cb0[6].z, whose draw-time value
                // is not readable from the shader binary; the legacy pins its equivalent
                // (u_tex_sample_alpha.b) to 1.0 at ModelRenderPass.cpp:656. 1.0 is therefore the
                // only value with a source behind it -- no tuning constant is invented here.
                //
                // Alpha is untouched: the lobe contributes RGB, and t3.a is its mask, not an
                // opacity. c.a is decided below exactly as before.
                // THE GAIN IS THE MODEL'S OWN, NOT A CONSTANT.
                //
                // Retail multiplies this term by cb0[6].z and the legacy viewport by
                // u_tex_sample_alpha.b -- the same value, and both are the batch's per-texture-unit
                // weight vector, indexed .r/.g/.b for units 1/2/3
                // (Source/games/wow/ModelRenderPass.cpp:122,127,130,131,132). WMV never supplied
                // it: :656 stubs the uniform to (1,1,1). This shader hard-coded 1 for the same
                // reason -- 1 was the only value with a source behind it while the real one was
                // thought to live in the client's CPU code.
                //
                // It does not. It lives in the model. texture_weight_combos names one weight track
                // PER TEXTURE UNIT, and unit 2's is a separate track from the pass opacity; the
                // animator evaluates it and writes it here every frame. On Drakestalker's Trophy
                // Pauldrons it is a 14-key linear track on a 3.3 s global sequence running between
                // 0.582 and 1.000, so the embers are authored to PULSE and we were holding them at
                // a flat maximum.
                if (_ThirdUnitLobe > 0.5)
                    c.rgb += t3.rgb * t3.a * _ThirdUnitWeight;

                // THE SECOND UNIT'S LUMINOUS LOBE -- the same idea, one unit over, and far commoner.
                //
                // Eight M2 combiners add their SECOND texture as an additive term rather than
                // combining it into the diffuse. Two of them are wired here:
                //
                //   1  ps13  Combiners_Opaque_AddAlpha        += t2.rgb * t2.a
                //   2  ps14  Combiners_Opaque_AddAlpha_Alpha  += t2.rgb * t2.a * (1 - t1.a)
                //   1  ps16  Combiners_Mod_AddAlpha            += t2.rgb * t2.a, discarding pass
                //   3  ps21  Combiners_Mod_Add_Alpha           += t2.rgb * (1 - t1.a)   NO t2.a
                //   1  ps20  Combiners_Opaque_AddAlpha_Wgt    += t2.rgb * t2.a * weight(unit 1)
                //   1  ps23  Combiners_Mod_AddAlpha_Wgt       += the same, with an alpha discard
                //   4  ps8   Combiners_Mod_Add                += t2.rgb          raw
                //   4  ps10  Combiners_Mod_AddNA              += t2.rgb          raw
                //
                // Shape 3 is not shape 2 with a typo and shape 4 is not shape 1 with one. ps21's
                // reference genuinely omits the lobe texture's own alpha where every other masked
                // one multiplies by it, and ps8/ps10 attenuate by nothing whatsoever.
                //
                // ALL SIX ARE NOW CONFIRMED AGAINST RETAIL BYTECODE, not just against the legacy
                // GLSL: combiners_uber_2_2.bls (FileDataID 5221412) switches on cb1[0].x with one
                // case per combiner, and cases 8, 10, 13, 14, 16, 20, 21 and 23 decode to exactly
                // these expressions. Its lobe register is added to the lit colour at the end, the
                // same shape as the three-texture shader's -- so the insertion point is right too.
                //
                // read from ModelRenderPass.cpp:120-121, which is this repository's own renderer of
                // record for them. Neither multiplies by a texture weight -- unlike ps15, whose
                // gain is the model's unit-2 weight track, the reference here multiplies by nothing,
                // so there is no track to resolve and no constant to guess.
                //
                // Added AFTER the light for the same reason the third unit's lobe is: the legacy
                // writes gl_FragColor = mat_diffuse + specular, where mat_diffuse is the lit colour
                // and specular never touches mesh_color (ModelRenderPass.cpp:156).
                //
                // ON ps14 THIS IS THE WHOLE MATERIAL. Its combiner is otherwise identical to plain
                // Combiners_Opaque, so without the lobe the batch is an inert flat surface. 6,482
                // batches on 5,036 models were drawing that way.
                // _SecondUnitWeight is unit 1's own texture weight, which 20 and 23 scale their
                // lobe by and 13 and 14 do not -- so it stays at its default of 1 for those two.
                // It is the same per-unit vector ps15 reads at unit 2, one channel over: the
                // legacy names it u_tex_sample_alpha and indexes .r/.g/.b for units 1/2/3
                // (ModelRenderPass.cpp:122,127,130,131,132), then pins the whole thing to (1,1,1)
                // at :656. On these two combiners that pin is not a small error: of the 1,130
                // batches in the client, 697 have an animated weight, exactly one constant equals
                // 1.0, and 17 are authored at 0.0 -- the artist switching the lobe off.
                // THE FIRST UNIT'S LUMINOUS LOBE -- ps24 only, and the only lobe in the table
                // that is not on the batch's last texture unit.
                //
                //   ps24  Combiners_Opaque_Alpha_Alpha
                //           diffuse = lerp(t1.rgb, t2.rgb, t2.a)      -- combiner mode 4
                //           lobe    = t1.rgb * t1.a * weight(unit 0)
                //
                // Retail: `mul r6.xyz, r3.wwww, r3.xyzx` then `mul r4.xyz, r6.xyzx, cb0[22].xxxx`
                // (combiners_uber_2_2.bls program 0, :251-253). cb0[22].x is unit 0's entry of the
                // same per-unit weight vector whose .y feeds ps20/23 and whose .z, in the
                // three-texture shader's cb0[6], feeds ps15. The legacy names it
                // u_tex_sample_alpha.r at ModelRenderPass.cpp:131.
                if (_FirstUnitLobe > 0.5)
                    c.rgb += t1.rgb * t1.a * _FirstUnitWeight;

                if (_SecondUnitLobe > 0.5)
                {
                    // la: the lobe texture's own alpha -- every shape but 3 uses it.
                    // lm: the inverse of the diffuse alpha -- shapes 2 and 3 use it.
                    // la: the lobe texture's own alpha -- shapes 1 and 2 only.
                    // lm: the inverse of the diffuse alpha -- shapes 2 and 3 only.
                    half la = (_SecondUnitLobe > 2.5) ? 1.0h : t2.a;
                    half lm = (_SecondUnitLobe > 1.5 && _SecondUnitLobe < 3.5)
                              ? (1.0h - t1.a) : 1.0h;
                    c.rgb += t2.rgb * la * lm * _SecondUnitWeight;
                }

                // THE TOP END IS SHAPED ONCE, BY THE TONE MAP -- NOT TWICE.
                //
                // A roll-off used to sit here. 125f3781 added it for one reason, in its own
                // words: the rig's terms deliberately sum past 1.0, and capping the light instead
                // would make a white texture read grey, so "the top end is rolled off instead".
                // That was written when this shader's output went more or less straight to an
                // 8-bit target and nothing else was going to shape it.
                //
                // It is not true any more. The viewport renders into an HDR colour buffer and
                // UberPost applies Neutral tonemapping, which is a shoulder -- exactly the job
                // the curve was written to do, done once, globally, and after blending rather
                // than per fragment. Running both compressed the top end TWICE, and that is what
                // flattened authored highlights: on Drakestalker's Trophy Pauldrons the mouth
                // reached 0.743 here against 0.950 in this application's own OpenGL viewport,
                // and the cracks 0.601 against 0.915. The OpenGL renderer is the reference that
                // settles it: ModelRenderPass.cpp applies no clamp, no curve and no tone map of
                // its own -- it saturates at the framebuffer -- and its highlights are the ones
                // that read as hot.
                //
                // WHAT REMOVING IT ACTUALLY MOVES. Only the top of the range. The curve was the
                // identity below its knee, which is where most of a model's surface sits, so on
                // ordinary models the median and the 90th percentile do not move at all
                // (chicken2 0.326 / 0.430 before and after; drustvarbeastman 0.363 / 0.559;
                // horse3 0.252 / 0.496, all to three decimals) and only the 99th percentile
                // rises, by 0.002 to 0.018. On the benchmark it lifts the mouth from 2.20x the
                // model's median surface to 2.43x and the cracks from 1.78x to 2.02x, against
                // 2.51x and 2.42x in the OpenGL viewport.
                //
                // This reverses the decision 125f3781 made and 0880a08d narrowed. Both were
                // taken before the viewport had a tone map to compare against, and neither had
                // an OpenGL reference render to measure the top end against.
                c.a = (_OpaqueAlpha > 0.5) ? 1.0 : a;

                // Authored domain -> linear here ONLY when the frame is not decoded as a whole
                // (WMV_DISPLAY=fragment). See the block above frag(), and WmvFrameDecodePass for
                // why the frame-level decode is the shipped one.
                if (_WmvShaderEncode > 0.5)
                    c.rgb = WmvAuthoredToLinear(c.rgb);
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
