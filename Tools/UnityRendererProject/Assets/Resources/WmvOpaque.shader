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
        // DIAGNOSTIC ONLY (-wmvWmoVertexColour): 1 multiplies the mesh's vertex colour into the
        // albedo. Nothing sets it in normal rendering, and a mesh with no colours is never drawn with
        // it on, so every material keeps exactly the colour it had without this.
        _VertexColour ("Diagnostic vertex colour", Float) = 0

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
            // The contact march dithers on SV_POSITION, which is a fragment-stage capability
            // Unity documents from shader model 3.0. The file carried no target at all, so it
            // built at the 2.5 default; nothing else here needs 3.0, but reading i.pos does.
            #pragma target 3.0
            #pragma multi_compile_local _ _ALPHATEST_ON
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                float2 uv     : TEXCOORD0;
                float2 uv2    : TEXCOORD1;
                fixed4 color  : COLOR;       // read only by the _VertexColour diagnostic
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
                fixed4 vcol   : TEXCOORD6;   // vertex colour, for the _VertexColour diagnostic only
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
            // A CAST SHADOW'S EDGE WIDENS WITH ITS OCCLUDER'S HEIGHT, AND A LONG SHADOW IS A LITTLE PALER.
            //
            // NOT RETAIL-DERIVED -- a preview choice, like the front light below. A light of any size
            // draws a sharp shadow right under its occluder and a soft one far below it. The fixed
            // kernel drew every cast shadow with the same few-texel edge, so the head's shadow on the
            // chest -- cast from 25 cm above -- ended in a hard, stair-stepped line around a dark
            // patch, and a rope lying on a saddle got exactly the same edge. WmvShadowFactor measures
            // how far above the receiver a real occluder stands and uses that height twice:
            //
            //   SHADOW_PENUMBRA  the key's angular radius, as a tangent: the edge's radius is the
            //                    height times this, never narrower than SHADOW_SOFT texels. 0.20 (about
            //                    11 degrees) gives the head's shadow on the chest an edge ~10 cm wide on a
            //                    full-size character, and leaves an occluder within ~1.5 cm of its
            //                    receiver there, or ~5 cm on a mount -- a strap, a rope on a saddle --
            //                    on the fixed edge (the texel, and so the fixed edge, scales with R).
            //   SHADOW_SEARCH    the widest edge looked for, as a fraction of the model radius R: 0.05 R
            //                    is ~100 texels of the map at every model size.
            //   SHADOW_FADE      the mean occluder height, as a fraction of R, at which a shadow keeps
            //                    half its depth: every real occluder in reach is scaled by
            //                    1 / (1 + mean height / (SHADOW_FADE * R)). Four times the contact march's
            //                    reach, so the fade is weak: on a full-size character an occluder 3 cm
            //                    above the skin keeps ~99 % of its shadow, and the head, which the map reads
            //                    ~27 cm above the chest, ~90 %. Framed on a head-sized box, where R is a
            //                    third as large, the head keeps ~73 %.
            //
            // Measured on the blood elf female against the level front light alone, its ceiling still
            // judged on the key before this shadow (below): the upper chest under the head sits at 0.76
            // of the lit chest beside it, framed on a head-sized box and at the full body's radius alike,
            // against 0.67 and 0.65 -- about 70 % of that contrast kept, under a soft edge with no band
            // along it. At the full body's radius the edge does most of the softening. Elsewhere the fade
            // costs a little depth: the light check's [map] darkening sits 9-15 % below the front light
            // alone on her and on a hooded model, and within 1 % on the rat mount. With the ceiling judged
            // after this shadow, as it is now, the chest sits at 0.82 and 0.80.
            #define SHADOW_PENUMBRA 0.20
            #define SHADOW_SEARCH   0.05
            #define SHADOW_FADE     (4.0 * CONTACT_RANGE)
            // Contact shadows: the screen-space near-field shadow. This is what puts the
            // hood's shadow ON the face right up to where they touch -- the shadow map's bias
            // makes it blind for the last few millimetres before a contact, and a shadow that
            // stops short of the contact line reads as floating.
            //
            // These five were briefly uniforms, driven by a slider panel in the application, so
            // that a look could be found by moving something and watching rather than by
            // rebuilding a shader. They are the numbers that panel arrived at, and the panel is
            // gone: a normal run draws from the constants alone.
            //
            // STRENGTH is how much light one contact removes. It is 0.4 rather than the 1.0 this
            // shipped with because at 1.0 a full contact took the whole ambient term AND the
            // whole directional with it (castKey = min(castKey, occ) below), so the deepest part
            // of every contact shadow went to black and every boundary in it was a hundred-plus
            // code values wide. RANGE is the total reach, as a fraction of the model radius, and
            // it is a float rather than a half because 0.36666667 is not representable in fp16
            // and the rung positions are derived from it. SOFTNESS is the tangent of the
            // occlusion cone's half-angle, TAPS the samples across that cone, and the march's
            // sample count lives with the march.
            #define CONTACT_STRENGTH 0.4h
            #define CONTACT_RANGE    0.36666667
            #define CONTACT_SOFTNESS 0.25
            #define CONTACT_TAPS     8
            // THE FRONT LIGHT: A SECOND, WEAKER DIRECTIONAL THAT FOLLOWS THE CAMERA.
            //
            // NOT RETAIL-DERIVED -- a preview choice. Retail's lit path has one directional light
            // (the header above), and so has the rest of this rig: the key, anchored to the
            // world's vertical. But a preview camera looks at the FRONT of a model, and what it
            // shows most closely -- a face above all -- is vertical. N.L against a vertical key is
            // ~0 there, so a face gets the ambient bands and nothing else while the shoulders and
            // the top of the chest right below it take the key. The bands cannot fix that: they
            // follow the normal's up component alone, and a face sits on the horizon band
            // whichever way it turns. On a head-and-shoulders view it read as a grey face on a
            // bright body.
            //
            // IT COMES STRAIGHT FROM THE CAMERA, LEVEL -- A CHOICE, NOT A MEASUREMENT. FRONT_DIR is
            // (0, 0, 1) of the camera's LEVEL frame: the horizontal direction toward the viewer. A
            // reference preview viewer's lit model shader, read while it ran, lights a model with an
            // ambient of 0.35, a primary light of 1.0 on normalize(5,-3,3) of view space and two
            // secondaries of 0.35 on normalize(1,1,1) and on its opposite, all fixed to the camera and
            // summed under a clamp to 1. This light first took that secondary's bearing, 45 degrees to
            // the viewer's right and 35.3 up; it lifted a face by only 6 % and its right half more than
            // its left. Straight from the camera it lifts a face evenly -- on the blood elf female's
            // head-and-shoulders captures the brow x1.25 and the cheeks x1.16-1.18, which puts the face
            // at the brightness of the shoulder tops -- and it was chosen by eye over the others.
            //
            //   FRONT_LEVEL  NOT MEASURED: 0.35 * LIGHT_LEVEL, the reference secondary's share of its
            //                primary applied to THIS rig's key. A choice; that viewer's level is 0.35
            //                itself, and under the ceiling below the two gave the same light check.
            //
            // IT IS NOT HOW THAT VIEWER LIGHTS A FACE. A face turned to its camera gets 0.35 of
            // ambient, 0.457 from the primary and 0.202 from a secondary, clamped to 1.0: most of its
            // light is the primary, which comes from 59 degrees to the viewer's right and 27 degrees
            // BELOW eye level. That light is left out on purpose -- this rig keeps its overhead key as
            // the one primary -- so the front light carries only the smaller share.
            //
            // FOUR RULES FIT IT TO THIS RIG. The clipping figures come from the blood elf female's
            // head-and-shoulders light check seen from the front (-wmvLightYaw=180), where the key alone
            // clips 3.9 % of the model's pixels.
            //
            //   * THE ELEVATION IS THE WORLD'S. FRONT_DIR is read in the camera's LEVEL frame -- x the
            //     camera's right, y the world's up, z toward the viewer along the ground -- so the light
            //     turns with the camera's yaw but stays on the horizon whatever its pitch. Fixed to the
            //     view, it would swing under the model whenever the camera looks up from below and light
            //     the belly, which is what anchoring the key was for (see WmvShadowRig). Level, it cannot
            //     reach a surface that faces straight down.
            //   * IT LIGHTS WHAT THE KEY MISSES: it is scaled by the key's shadow side, 1 - N.L, as this
            //     rig's old sky fill was. Unscaled, the same light raised the lit peak from 1.25 to ~1.32
            //     and clipped 23 % of the model; scaled, the peak stays at 1.25 (see LIGHT_LEVEL), and a
            //     vertical face loses none of the light to the scale.
            //   * IT NEVER LIFTS A SURFACE PAST 1.0, the texture's own colour -- the reference's clamp,
            //     applied to this light alone, so every value this rig already puts above 1.0 stays
            //     exactly as chosen. Scaled but not capped, it still clipped 11.1 %; capped, 4.2 %. It
            //     fades out over the last FRONT_LEVEL below 1.0 rather than stopping there, so it leaves
            //     no flat band.
            //   * NOTHING THE CAMERA SEES IS HIDDEN FROM IT. The contact march asks whether something
            //     within touching distance stands between a surface and the KEY; a light at the camera
            //     reaches every surface the camera can see, so near-field occlusion does not scale this
            //     light, and the ceiling is judged on the light that arrives (below). Under a fringe, a
            //     jaw or a hood the sky is partly blocked, and this light fills toward what an open
            //     surface gets -- never past 1.0. Occluded like the ambient it barely reached those
            //     places, because they tilt up into the key and their unoccluded light was already near
            //     the ceiling: the skin under the eyes x1.06-1.10 of the key alone on a head-sized
            //     framing, x1.23-1.25 with this rule; the neck under the jaw at the full body's radius
            //     x1.03, x1.23. A contact shadow keeps its shape and loses about an eighth of its depth
            //     on surfaces facing the camera (light check [contact adds] 0.0588 -> 0.0511 on the rat
            //     mount seen three-quarter); a belly seen from below gets none of this light.
            //
            // THE CEILING COUNTS THE LIGHT THAT ARRIVES: THE AMBIENT AFTER OCCLUSION, THE KEY AFTER ITS
            // CAST SHADOW. Judged on the key before its shadow, it took this light from exactly the skin
            // a cast shadow darkens. The upper cheeks tilt up into the key, and under a brow they lie in
            // its shadow: the ceiling counted a key they never got and left them about a fifth of this
            // light, so the ambient alone lit them -- and the ambient barely follows the normal. On the
            // night elf female's face seen close up, that was a flat grey band from eye to eye between
            // the key-lit forehead and the front-lit lower face: the upper cheeks came up x1.08-1.09 of
            // the key alone where the lower cheeks came up x1.17-1.19. Judged on what arrives, they come
            // up x1.19-1.20, with the shape this light gives them. The shadow-side scale still follows
            // the surface's own N.L, so a shadowed surface gets less of this light the more it faces the
            // key, and one the key cannot reach anyway -- a face under a hood -- gets practically what it
            // got before. The price is on surfaces that face the key: the head's shadow on the blood elf
            // female's chest, zoomed in at the full body's radius, sits at 0.80 of the lit chest
            // against 0.76, and the light check's [map] darkening drops 4-15 % on her, 5-14 % on the rat
            // mount seen from the front and three-quarter (a third seen from below, where its belly keeps
            // its flat-albedo p05) and 1 % on a hooded model. How soft a cast shadow is belongs to the
            // map -- see SHADOW_PENUMBRA.
            #define FRONT_LEVEL  0.2065h                           // derived: 0.35 * LIGHT_LEVEL
            #define FRONT_DIR    half3(0.0, 0.0, 1.0)              // the camera's level frame
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
            // Shared with the contact march below.
            float4    _WmvKeyDirWorld;        // toward the light; w unused
            float     _WmvModelRadius;        // world units; the map's half-window and the march's scale

            float WmvStepPhase(float2 pix);           // the per-pixel phase, with the contact march below

            // 1 = fully lit, 0 = fully occluded (before strength is applied).
            //
            // THE FIXED KERNEL'S NINE TAPS STAY, AND WITH NO REAL OCCLUDER IN REACH THEIR VERDICT IS
            // RETURNED BIT FOR BIT. A 3x3 PCF spread by "soft" texels compares every tap with the
            // receiver's own depth, so on a surface tilted away from the key the taps on its uphill
            // side find the surface itself standing above the sample point and count it: an open
            // slope keeps only part of its key (0.72 on the blood elf female's lit chest, where the
            // same taps with her own surface counted out give 1.0). The rig's exposure was settled
            // with that in -- counting it out clipped 12 % of her head box seen from the front,
            // against 4 % -- so it stays. A tap is a REAL OCCLUDER when the fixed test counts it and
            // it also stands above the receiver's own tangent plane along the key; its height above
            // that plane is what the constants block calls the height.
            //
            // THAT SHARE IS THE RECEIVER'S, SO IT STAYS UNDER AN OCCLUDER. Where a real occluder
            // covers a tap, the map no longer shows what the receiver's own surface does there, and
            // the receiver's plane answers instead: the tap is its own when the plane there stands
            // higher above the sample point than the fixed test's bias. The inside of a shadow, its
            // edge and the open slope beside it therefore carry the same share. Counted only on taps
            // without an occluder -- the first version -- it went missing from the whole inside of a
            // shadow and stayed on the lit side of the edge, so a faded shadow came out brighter than
            // the skin just outside it, with a dark band along the edge.
            //
            // A REAL OCCLUDER'S SHADOW GETS AN EDGE AS WIDE AS ITS HEIGHT ALLOWS (SHADOW_PENUMBRA),
            // AND IS SHALLOWER THE HIGHER IT STANDS (SHADOW_FADE). Eight more taps search a disc as
            // wide as the widest edge (SHADOW_SEARCH) for real occluders; their mean height times the
            // key's tangent is the edge's radius, and thirty-two taps over a disc of that radius give
            // the occluded fraction. Both discs are golden-angle spirals turned per pixel by the
            // contact march's screen-space phase, which turns their taps into a fine grain instead of
            // banded copies of the occluder. Where the edge is no wider than the fixed spread the
            // nine taps decide alone, and they hand over to the wide filter as the edge grows to twice
            // that spread, so there is no seam.
            //
            // ONE FADE PER PIXEL, FROM THE MEAN HEIGHT. The map holds the TOP of whatever stands over
            // the receiver: under a head it reads the crown, and at the edge of the head's shadow the
            // side of the head or a strand of hair, far lower. Faded tap by tap, the inside of the
            // head's shadow lightened more than its edge, which kept a dark band; every real occluder
            // in reach is therefore faded alike, by 1 / (1 + mean height / (SHADOW_FADE * R)). The
            // same limit is why the fade is weak: a surface under a tall column -- a foot under its
            // leg, a lip under a hood -- reads the column's top, and fades as if its shadow were cast
            // from there.
            //
            // The cost: the nine reads it always took, eight more on every surface the key reaches,
            // and thirty-two more only where a real occluder stands high enough to widen the edge. A
            // surface facing away from the key takes none: its direct term is zero, so no shadow can
            // change it.
            half WmvShadowFactor(float3 wpos, half3 nrmWorld, half soft, float2 pix)
            {
                if (_WmvShadowValid < 0.5h)
                    return 1.0h;

                float nl = dot((float3)nrmWorld, _WmvKeyDirWorld.xyz);
                if (nl <= 0.0)
                    return 1.0h;              // the key cannot reach it: nothing to shadow

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

                // DEPTH AS HEIGHT. The light camera is orthographic, its window and its near-to-far
                // span both 2R wide around the model (R = _WmvModelRadius, see WmvShadowRig), so one
                // unit of stored depth and one unit of uv are each 2R world units along their axes.
                // zs makes "the stored surface is nearer the light" positive under either depth
                // convention.
            #if UNITY_REVERSED_Z
                float zr = sp.z;
                float zs = 1.0;
            #else
                float zr = sp.z * 0.5 + 0.5;
                float zs = -1.0;
            #endif
                float R     = max(_WmvModelRadius, 1e-4);
                float span  = 2.0 * R;
                float biasW = _WmvShadowDepthBias * span;   // the fixed depth bias, in world units
                float fadeW = SHADOW_FADE * R;

                // THE RECEIVER'S OWN PLANE. Rows 0 and 1 of the orthographic matrix are the map's uv
                // axes in world space, each 1/R long, so a uv offset d is the world offset
                // o = 2 R^2 (d.x row0 + d.y row1), and the tangent plane n.(o + L h) = 0 stands
                // h = -n.o / n.L above it along the key L. A grazing surface is held at n.L = 0.2: the
                // key barely lights it, and the height would run away. The surface itself runs below
                // the pushed sample point by the normal push seen along L (sink).
                float nlc   = max(nl, 0.2);
                float3 axisU = 2.0 * R * R * _WmvShadowMatrix[0].xyz;
                float3 axisV = 2.0 * R * R * _WmvShadowMatrix[1].xyz;
                float2 plane = float2(-dot((float3)nrmWorld, axisU), -dot((float3)nrmWorld, axisV))
                             / nlc;
                float sink  = _WmvShadowNormalBias / nlc;

                // THE NINE TAPS: the fixed verdict exactly as it always was (lit); how many taps the
                // receiver's own surface hides (own); the taps the fixed test leaves lit (open); and,
                // among the real occluders' taps, those the receiver itself would leave lit (under).
                float lit = 0.0, own = 0.0, open = 0.0, under = 0.0, occN = 0.0, heightSum = 0.0,
                      found = 0.0;
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
                        bool hit = !(sp.z >= stored - _WmvShadowDepthBias);
            #else
                        bool hit = !((sp.z * 0.5 + 0.5) <= stored + _WmvShadowDepthBias);
            #endif
                        lit += hit ? 0.0 : 1.0;
                        float2 d = float2(x, y) * r;
                        float rise = dot(d, plane);             // the receiver's plane at this tap
                        float height = zs * (stored - zr) * span - rise;
                        if (height > biasW && hit)
                        {
                            float ownHere = (rise - sink > biasW) ? 1.0 : 0.0;
                            own       += ownHere;
                            under     += 1.0 - ownHere;
                            occN      += 1.0;
                            heightSum += height;
                            found     += 1.0;
                        }
                        else
                        {
                            own  += hit ? 1.0 : 0.0;
                            open += hit ? 0.0 : 1.0;
                        }
                    }
                float fixedLit = lit / 9.0;

                // THE SEARCH: eight taps over a disc as wide as the widest edge, real occluders only.
                float searchUV = SHADOW_SEARCH * 0.5;       // SHADOW_SEARCH * R over the 2R window
                float turn = WmvStepPhase(pix + float2(23.0, 11.0)) * 6.2831853;
                float cs = cos(turn), sn = sin(turn);
                [unroll]
                for (int k = 0; k < 8; k++)
                {
                    float a = k * 2.3999632;                // the golden angle
                    float2 d0 = float2(cos(a), sin(a)) * sqrt((k + 0.5) / 8.0);
                    float2 d = float2(d0.x * cs - d0.y * sn, d0.x * sn + d0.y * cs) * searchUV;
                    float stored = tex2Dlod(_WmvShadowMap, float4(uv + d, 0.0, 0.0)).r;
                    float above = zs * (stored - zr) * span;
                    float height = above - dot(d, plane);
                    if (height > biasW && above > biasW)
                    {
                        heightSum += height;
                        found     += 1.0;
                    }
                }
                if (found < 0.5)
                    return (half)fixedLit;            // no real occluder in reach: the fixed verdict

                // THE FADE: the share of the key a real occluder still removes, from their mean height.
                float fade = 1.0 / (1.0 + heightSum / found / fadeW);
                float nearLit = (occN < 0.5) ? fixedLit : (open + under * (1.0 - fade)) / 9.0;

                // THE EDGE: the mean height times the key's tangent, as a uv radius.
                float radius = heightSum / found * SHADOW_PENUMBRA / span;
                float widen = saturate((radius - r) / r);
                if (widen <= 0.0)
                    return (half)nearLit;
                radius = min(radius, searchUV);

                float turn2 = WmvStepPhase(pix + float2(5.0, 37.0)) * 6.2831853;
                float c2 = cos(turn2), s2 = sin(turn2);
                float shade = 0.0;
                [unroll]
                for (int j = 0; j < 32; j++)
                {
                    float a = j * 2.3999632;
                    float2 d0 = float2(cos(a), sin(a)) * sqrt((j + 0.5) / 32.0);
                    float2 d = float2(d0.x * c2 - d0.y * s2, d0.x * s2 + d0.y * c2) * radius;
                    float stored = tex2Dlod(_WmvShadowMap, float4(uv + d, 0.0, 0.0)).r;
                    float above = zs * (stored - zr) * span;
                    float height = above - dot(d, plane);
                    if (height > biasW && above > biasW)
                        shade += fade;
                }
                float wideLit = (1.0 - shade / 32.0) * (1.0 - own / 9.0);
                return (half)lerp(nearLit, wideLit, widen);
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
            float4    _WmvFillDirWorld;       // the sky fill, same handling as the key; w unused
            float     _WmvContactEps;         // self-hit guard, WORLD UNITS
            float     _WmvContactThick;       // occluder thickness assumption, WORLD UNITS
            float4    _WmvViewDepthParams;    // (near, far, far - near, near * far), world units

            // Device depth -> distance from the camera, in WORLD UNITS.
            //
            // THIS IS THE WHOLE FIX. The march used to compare RAW DEVICE DEPTHS and test the
            // difference against fixed [0,1] constants, on the stated assumption that a pinched
            // near/far makes those "correspond to a roughly constant world thickness across the
            // model". Perspective depth is hyperbolic, so it does not. For reversed Z,
            //
            //     d(z) = n*f/((f-n)*z) - n/(f-n)      =>   |dd/dz| = n*f / ((f-n) * z^2)
            //
            // and with the rig's n = D-R, f = D+R, evaluated at the model (z = D), that is
            // (1 - R^2/D^2)/(2R) against the assumed 1/(2R). The bracket is ~0.99 when the
            // camera sits ten radii out and 0.17 at 1.1 radii, so the SAME physical gap produced
            // a depth difference that shrank as the camera approached until it fell under the
            // self-hit guard and every hit was rejected -- the shadows vanished on zoom-in. The
            // same term also varies ACROSS the image, as ((D+z)/(D-z))^2 with z the distance
            // from the camera: at the shipped framing distance that is 5.4x between the near and
            // the far side of the GEOMETRY (D = 2.5e for a bounds radius e) and 12.6x across the
            // whole frustum. So one fixed window was far too permissive at the back: unrelated
            // geometry counted as touching, which is the long-range self-shadowing and the
            // wedges, while at the front the same window was too tight to catch real contact.
            //
            // Inverting the mapping puts the test back into metres, where a thickness is a
            // thickness whatever the camera is doing. NOT _ZBufferParams: that describes the
            // MAIN camera, and this buffer belongs to a private one whose planes are pinched
            // around the model every frame.
            float WmvLinearViewDepth(float d)
            {
            #if UNITY_REVERSED_Z
                // d = 1 at the near plane, 0 at the far plane. Check: d=1 -> nf/f = n,
                // d=0 -> nf/n = f. The denominator is >= n > 0 for every d in [0,1], so it
                // needs no clamp, and a CLEARED texel (d = 0) linearises to the far plane,
                // which the acceptance test below rejects on its own.
                return _WmvViewDepthParams.w
                     / (_WmvViewDepthParams.z * d + _WmvViewDepthParams.x);
            #else
                // Forward Z: d = 1 - d_reversed, so the same inverse with the roles swapped.
                return _WmvViewDepthParams.w
                     / (_WmvViewDepthParams.y - _WmvViewDepthParams.z * d);
            #endif
            }

            // The march's per-fragment step phase: interleaved gradient noise (Jimenez, "Next
            // Generation Post Processing in Call of Duty: Advanced Warfare", 2014) on the pixel
            // centre.
            //
            // WHY THE PHASE IS NOT A FUNCTION OF WORLD POSITION. It used to be
            // frac(dot(wpos, k)) -- linear in world position, so on any flat panel a sawtooth
            // with a period of 1/|k| = 1.4 cm along the gradient: literal parallel fringes
            // locked to the surface, which is the reported striping. Hashing wpos fixes the
            // LINEARITY but not the underlying problem: any function of world position
            // decorrelates neighbouring pixels only while one pixel spans enough world distance
            // for the function to change, and a pixel spans 2*D*tan(fov/2)/H world units. That
            // is a proper dither on a framed creature and a smooth ramp on an item component
            // zoomed in -- and a smooth ramp under the loop's max() is a contour band. A phase
            // that lives in screen space has no scale to be wrong about: one period per pixel,
            // at every camera distance and every model size, so the model slides under a fixed
            // fine grain instead of dragging a pattern of breathing width along with it.
            float WmvStepPhase(float2 pix)
            {
                return frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))));
            }

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
            half WmvContactFactor(float3 wpos, half3 nrmWorld, float range, float2 pix)
            {
                if (_WmvContactValid < 0.5h)
                    return 1.0h;

                float3 dir = _WmvKeyDirWorld.xyz;

                // Per-fragment phase for the steps. See WmvStepPhase.
                float jitter = WmvStepPhase(pix);

                // ONE NUMBER IS THE REACH, AND THE FADE RUNS OUT OVER IT.
                //
                // This used to be a ladder of 24 rungs walked 32 times with a fade of
                // `1 - 0.75*(s+0.5)/24`, three constants that only line up at those exact
                // values: 0.75*(s+0.5)/24 IS (s+0.5)/32, so the fade reached zero at rung 31.5
                // and walking 32 rungs was what stopped it being truncated mid-ramp -- which it
                // had been, at 0.2656, drawing a hard straight line across the model at a fixed
                // distance from the caster where no geometry is. Written as `1 - (s+0.5)/STEPS`
                // over a reach of 0.3333 R the arithmetic is the same to the last bit at the
                // shipped values (rung spacing 0.0104167 R either way) and the coupling is gone:
                // the fade always lands on zero at the last sample, whatever STEPS is, so STEPS
                // is a pure sampling rate and the reach is one number a person can set.
                //
                // SAMPLE COUNT. Twelve was not enough and showed as a CHECKERBOARD on thin,
                // steeply inclined geometry: where the sampled depth moves several times faster
                // along the ray than the ray does, the acceptance window is stepped clean over
                // and finding it becomes a coin toss on the phase. 32 resolves it; more changes
                // little. It is a sampling rate, not a strength.
                const int STEPS = 32;                // samples along the reach
                const int TAPS = CONTACT_TAPS;

                // THE CONE'S LATERAL AXIS. cross(march, camera forward) is perpendicular to the
                // march AND to the view axis, so the projection's w row -- which IS the camera
                // forward -- is orthogonal to it, and mul(M, float4(bLat,0)).w is exactly zero.
                // A tap along bLat therefore changes WHICH TEXEL is read and leaves cp.w, the
                // ray point's own distance from the camera, untouched: the acceptance test sees
                // the same reference depth for every tap, which is what makes this a cone and
                // not a slow walk out of the thickness window. Offsetting in the whole plane
                // perpendicular to the march would not do: with the march anchored to world up
                // that plane is the world's horizontal one, and one of its axes points into the
                // screen, so those taps would shift the reference depth by up to the tap radius
                // -- and by an amount that changes with camera yaw.
                //
                // One axis is enough. The along-march direction is already graded by the rung
                // ladder above; only the perpendicular needs width, and in screen space there is
                // exactly one direction perpendicular to the projected march.
                float3 camF = float3(_WmvViewDepthMatrix[3][0], _WmvViewDepthMatrix[3][1],
                                     _WmvViewDepthMatrix[3][2]);
                float3 bLat = cross(dir, camF);
                float bLen = dot(bLat, bLat);
                bLat = (bLen > 1e-8) ? bLat * rsqrt(bLen) : float3(1.0, 0.0, 0.0);
                float4 mbLat = mul(_WmvViewDepthMatrix, float4(bLat, 0.0));

                float occ = 0.0;
                [loop]
                for (int s = 0; s < STEPS; s++)
                {
                    float t = (s + jitter) / STEPS;
                    // The normal push keeps the ray off its own surface; it grows with t
                    // because a surface curving toward the light drifts back under the ray.
                    // Both pushes scale with `range`, so CONTACT_RANGE also sets the blind
                    // zone next to a contact: at 1/3 R the clearance is 0.015-0.04 R along
                    // the normal, four times what the march was first validated with (0.06 R).
                    // Tightening it means anchoring these fractions to a fixed share of the
                    // model radius rather than to the reach -- a look change, not a fix.
                    float3 pw = wpos + nrmWorld * ((0.045 + 0.10 * t) * range)
                              + dir * (max(t, 0.015) * range);
                    float4 cp = mul(_WmvViewDepthMatrix, float4(pw, 1.0));
                    if (cp.w <= _WmvViewDepthParams.x)
                        break;                       // in front of the near plane: nothing there

                    // The cone's radius at this rung, in WORLD units, so the softness means the
                    // same thing at every camera distance and on every model size.
                    float rad = CONTACT_SOFTNESS * t * range;
                    float cov = 0.0;                 // occluded fraction of the cone's width
                    float covW = 0.0;                // how much of it could be sampled at all
                    [unroll]
                    for (int k = 0; k < TAPS; k++)
                    {
                        // Stratified across the cone, not a ring: a ring puts every tap at the
                        // same radius, which does not blur an edge, it rings it.
                        float off = rad * (2.0 * (k + 0.5) / TAPS - 1.0);
                        float4 cpk = cp + mbLat * off;   // cpk.w == cp.w, by construction
                        float2 uvk = cpk.xy / cpk.w * 0.5 + 0.5;
                        // THE SCREEN EDGE, FADED RATHER THAN CUT. A screen-space march can only
                        // see what is on screen, and a hard cutoff there is a SECOND zoom-in
                        // fade: the model overflows a 60 degree fov once asin(e/D) > 30 degrees,
                        // i.e. from about one wheel notch in from the framed distance, after
                        // which rays that climb away from the surface walk off the frame and the
                        // march silently reports "lit" with no transition at all. Ramping over a
                        // 5 % border makes that boundary a gradient. It is not a strength knob:
                        // at the framed distance no ray reaches the border.
                        float2 edge = saturate(min(uvk, 1.0 - uvk) * 20.0);
                        float border = edge.x * edge.y;
                        if (border <= 0.0)
                            continue;                // this tap saw nothing; the others may
                        // Both sides in WORLD UNITS. cpk.w is the ray point's distance from the
                        // camera directly -- the projection's w row IS the view-space depth,
                        // which is also what makes the near-plane test above a plain comparison
                        // -- and the stored device depth is inverted back to the same units.
                        // `infront` is then how far IN FRONT of the ray the nearest recorded
                        // surface sits, in metres, and it means the same thing at every camera
                        // distance and everywhere on screen. The two reversed-Z branches
                        // collapse into one: the convention is handled inside the linearisation,
                        // not duplicated at the comparison.
                        float stored = tex2D(_WmvViewDepth, uvk).r;
                        float infront = cpk.w - WmvLinearViewDepth(stored);
                        cov += border
                             * smoothstep(_WmvContactEps * 0.5, _WmvContactEps * 1.5, infront)
                             * (1.0 - smoothstep(_WmvContactThick * 0.6, _WmvContactThick,
                                                 infront));
                        covW += border;
                    }
                    if (covW <= 0.0)
                        break;                       // the whole cone is off-screen

                    // THE LADDER IS FIXED; ONLY THE PROBE IS JITTERED. The depth window is
                    // effectively a step in t (the buffer is point-sampled and one march step
                    // spans many depth texels), so the loop's max() is attained at the first
                    // lattice point past the crossing -- and if the fade read the JITTERED t,
                    // the result inherited that phase as a sawtooth of 1/STEPS of full
                    // occlusion, hard-edged, which was the striping's amplitude. Reading the
                    // rung instead makes the fade identical for every pixel, so the phase
                    // survives only as WHICH rung is hit. `t` still drives the two pushes
                    // above, which is right: those have to follow the probe.
                    //
                    // covW/TAPS is the mean border weight over the cone; dividing cov by covW
                    // and multiplying it back is not a no-op, it restores the 5 % screen-edge
                    // ramp that normalising the coverage would otherwise cancel out.
                    float tFade = (s + 0.5) / STEPS;
                    float w = (cov / covW) * (1.0 - tFade) * (covW / TAPS);
                    occ = max(occ, w);
                }
                return (half)(1.0 - occ);
            }
            // -----------------------------------------------------------------------------
            fixed4 _Color;
            fixed _Cutoff;
            fixed _VertexColour;

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                o.uv1 = v.uv2;
                o.normal = UnityObjectToWorldNormal(v.normal);
                o.wpos = mul(unity_ObjectToWorld, v.vertex).xyz;
                o.vcol = v.color;

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
                if (_VertexColour > 0.5)
                    c.rgb *= i.vcol.rgb;

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
                half  cStr   = CONTACT_STRENGTH;
                float cRange = CONTACT_RANGE;

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
                        castKey = 1.0h - shStr * (1.0h - WmvShadowFactor(i.wpos, n, shSoft,
                                                                         i.pos.xy));
                    if (cStr > 0.0h)
                    {
                        half contact = WmvContactFactor(i.wpos, n, cRange * _WmvModelRadius,
                                                        i.pos.xy);
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

                    // THE FRONT LIGHT. What it is, where its numbers come from and why it is
                    // scaled the way it is: the constants block. It is built from the RENDERING
                    // camera's view matrix, so every camera that draws the model -- the viewport,
                    // a capture, the light check's own -- lights it from its own side, and nothing
                    // is published from the CPU: where the rig never ran it applies unchanged,
                    // scaled by the fallback key's N.L above.
                    //
                    // The level frame from the view matrix's rows (right, up, toward the viewer):
                    // back*up.y - up*back.y is the toward-viewer axis with the pitch taken out,
                    // horizontal and of unit length at every pitch of a camera without roll --
                    // straight down included, where the forward vector has no horizontal part
                    // left to normalise. The normalise matters only for a rolled camera.
                    //
                    // The ceiling is judged on lum as it stands here -- the ambient after near-field
                    // occlusion and the key after its cast shadow, the light that actually arrives --
                    // and the light itself is not occluded: a light at the camera reaches everything
                    // the camera sees. The shadow-side scale stays on the surface's own N.L.
                    float3 camUp      = UNITY_MATRIX_V[1].xyz;
                    float3 camBack    = UNITY_MATRIX_V[2].xyz;
                    float3 levelBack  = camBack * camUp.y - camUp * camBack.y;
                    levelBack.y = 0.0;
                    levelBack *= rsqrt(max(dot(levelBack, levelBack), 1e-8));
                    float3 levelRight = cross(levelBack, float3(0.0, 1.0, 0.0));
                    float3 frontDir   = FRONT_DIR.x * levelRight + FRONT_DIR.z * levelBack
                                      + float3(0.0, FRONT_DIR.y, 0.0);
                    half facing   = saturate(dot(n, (half3)frontDir));
                    half headroom = saturate((1.0h - lum) / FRONT_LEVEL);
                    lum += FRONT_LEVEL * facing * shadowSide * headroom;

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
