// WmvParticle.shader
//
// The pass every M2 particle and ribbon is drawn with. One shader, one material per emitter,
// everything that differs between emitters carried as a uniform -- the same discipline
// WmvOpaque.shader follows, and for the same reason: variants are what a player build strips.
//
// WHAT IT COMPUTES
//   texture * vertex colour, and nothing else. The legacy viewport binds a particle's texture
//   with GL_MODULATE and supplies the life-ramped colour through glColor4fv
//   (Source/games/wow/particle.cpp:360, :427), so the fragment is exactly that product. There is
//   no lighting: a particle is emissive art, and the legacy has GL_LIGHTING off for the ribbons
//   (:843) and never enables a light for the particle quads either.
//
// COLOUR SPACE -- the same rule as WmvOpaque, and it is not bookkeeping
//   The project renders in LINEAR space, so the framebuffer holds linear light and the swapchain
//   applies the sRGB curve on write. Model textures are uploaded UNDECODED (WmvModelBuilder.
//   CreateTexture passes linear:true, which in Unity's vocabulary means "hand the shader the
//   stored value"), so the whole shader runs in the AUTHORED domain and converts once, at the
//   end, with the exact sRGB EOTF. Doing the multiply in the authored domain matters here more
//   than anywhere: 56 % of the client's particle emitters are additive, and an additive term is
//   not scale-invariant between the two domains -- the same authored value lands at a different
//   framebuffer level depending on which space the sum is taken in.
//
//   Values above 1 are left unclamped so an additive stack can still blow out and still feed
//   bloom, which is what makes a dense emitter read as bright rather than as flat white.
//
// BLEND MODES
//   _SrcBlend / _DstBlend are set per emitter from the M2 blend field, with the factors the
//   legacy's ParticleSystem::draw uses for each mode (particle.cpp:306-353). _AlphaTest covers
//   mode 1, the only one that discards.
//
// DEPTH
//   ZWrite is off and the queue is Transparent: a particle must not occlude the model or another
//   particle. The legacy is the same -- glDepthMask(GL_FALSE) for ribbons (:846), and the
//   particle pass runs after the model with blending on.

Shader "Wmv/Particle"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src blend", Float) = 5   // SrcAlpha
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst blend", Float) = 1   // One
        [Toggle] _AlphaTest ("Alpha test", Float) = 0
        _Cutoff ("Alpha cutoff", Range(0,1)) = 0.5

        /// Matches WmvOpaque's switch of the same name, so both passes can be put back into the
        /// linear-composited behaviour together if the project's colour space ever changes.
        [Toggle] _WmvAuthoredDomain ("Composite in the authored domain", Float) = 1
    }

    SubShader
    {
        // No LightMode tag, for the same reason WmvOpaque has none: the built-in pipeline draws
        // this as an ordinary transparent pass and URP draws it through SRPDefaultUnlit, so one
        // SubShader covers both.
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "IgnoreProjector" = "True" }

        Cull Off
        Blend [_SrcBlend] [_DstBlend]
        ZWrite Off
        ZTest LEqual
        Lighting Off
        Fog { Mode Off }

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv     : TEXCOORD0;
                fixed4 color  : COLOR;
            };

            struct v2f
            {
                float4 pos   : SV_POSITION;
                float2 uv    : TEXCOORD0;
                fixed4 color : COLOR;
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            half _AlphaTest;
            half _Cutoff;
            half _WmvAuthoredDomain;

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                // Straight through. There is no COLOR0 doubling convention here: that belongs to
                // the M2 *model* vertex shaders, whose 0.5 at the vertex and 2 at the pixel
                // cancel. This colour is the life ramp, written by the renderer, meant as it is.
                o.color = v.color;
                return o;
            }

            // The exact sRGB EOTF, not the pow(2.2) approximation: it has to invert the
            // swapchain's encode exactly or every mid-tone shifts. Unclamped above 1 on purpose.
            half3 WmvAuthoredToLinear(half3 c)
            {
                half3 lo = c * (1.0h / 12.92h);
                half3 hi = pow(max((c + 0.055h) * (1.0h / 1.055h), 0.0h), 2.4h);
                return lerp(lo, hi, step(0.04045h, c));
            }

            fixed4 frag (v2f i) : SV_Target
            {
                fixed4 tex = tex2D(_MainTex, i.uv);
                fixed4 c = tex * i.color;

                // Blend mode 1 is the only M2 particle mode that discards; the legacy turns the
                // alpha test on for it and leaves the blend at ONE/ZERO (particle.cpp:313-317).
                if (_AlphaTest > 0.5 && c.a < _Cutoff)
                    discard;

                if (_WmvAuthoredDomain > 0.5)
                    c.rgb = WmvAuthoredToLinear(c.rgb);
                return c;
            }
            ENDCG
        }
    }

    Fallback Off
}
