#version 450
// The viewport's one fragment shader.
//
//  - Lighting: the preview rig the Unity viewport used (docs/unity-renderer/README.md, "Lighting"): an ambient
//    floor, a key straight down from the world's vertical and a weaker front light from the camera that fills only
//    what the key misses, under a ceiling. A pass that is unlit takes its colour as it is.
//  - The fixed-function path (pixelShader < 0): GL_MODULATE of the texture by the lit colour, plus the alpha test,
//    as the archived OpenGL viewport drew.
//  - The M2 pixel combiners (pixelShader >= 0): ModelRenderPass's GLSL 1.20 combiner, ported line for line, with
//    the lit colour as its mesh colour.

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D sTex0;
layout(set = 1, binding = 1) uniform sampler2D sTex1;
layout(set = 1, binding = 2) uniform sampler2D sTex2;
layout(set = 1, binding = 3) uniform sampler2D sTex3;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vEyePos;
layout(location = 2) in vec2 vTc[4];
layout(location = 6) in vec4 vColor;

layout(location = 0) out vec4 outColor;

vec3 light(DrawData d, vec3 n)
{
  if ((d.flags & FLAG_UNLIT) != 0 || frame.options.x > 0.5)
    return vec3(1.0);
  float key = max(dot(n, frame.keyDirView.xyz), 0.0);
  float front = max(n.z, 0.0); // towards the camera, which looks down -z
  float l = frame.lightLevels.x + frame.lightLevels.y * key + frame.lightLevels.z * front * (1.0 - key);
  return frame.lightColor.rgb * min(l, frame.lightLevels.w);
}

void main()
{
  DrawData d = draws[pc.drawIndex];

  vec3 n = normalize(vNormal);
  if ((d.flags & FLAG_TWO_SIDED) != 0 && !gl_FrontFacing)
    n = -n;

  // The mesh colour: the material's (or a vertex's) colour, lit, with the emission on top (GL_EMISSION).
  vec4 base = d.color * vColor;
  vec4 mesh;
  if ((d.flags & FLAG_UNLIT) != 0)
    mesh = base;
  else
    mesh = vec4(base.rgb * light(d, n) + d.emissive.rgb, base.a);

  bool textured = (d.flags & FLAG_TEXTURED) != 0;
  vec4 t1 = textured ? texture(sTex0, vTc[0]) : vec4(1.0);

  if ((d.flags & FLAG_PARTICLE_MULTITEX) != 0)
  {
    // GL_COMBINE / GL_MODULATE with RGB_SCALE and ALPHA_SCALE 4 over the particle's units.
    vec4 c = mesh * t1;
    if (d.textureCount > 1 && textured) c *= texture(sTex1, vTc[1]);
    if (d.textureCount > 2 && textured) c *= texture(sTex2, vTc[2]);
    outColor = clamp(c * 4.0, 0.0, 1.0);
    return;
  }

  if (d.pixelShader < 0)
  {
    vec4 c = mesh * t1;
    if ((d.flags & FLAG_ALPHA_TEST) != 0 && (c.a < d.alphaRef || c.a <= 0.0))
      discard;
    outColor = c;
    return;
  }

  // ---- M2 pixel combiner (ModelRenderPass COMBINER_FRAGMENT_SRC) ----
  vec2 uv1 = vTc[0];
  vec2 uv2 = vTc[1];
  vec2 uv3 = vTc[2];
  int ps = d.pixelShader;
  if (ps == 26 || ps == 27 || ps == 28) { uv2 = uv1; uv3 = uv1; }
  vec4 tex1 = textured ? texture(sTex0, uv1) : vec4(1.0);
  vec4 tex2 = textured ? texture(sTex1, uv2) : vec4(1.0);
  vec4 tex3 = textured ? texture(sTex2, uv3) : vec4(1.0);
  vec4 tex4 = textured ? texture(sTex3, vTc[1]) : vec4(1.0);
  float env_fres = clamp(pow(length(vTc[1] * 2.0 - 1.0), 1.5), 0.0, 1.0);
  tex2.rgb *= (1.0 + d.envBoost * (0.30 + 0.70 * env_fres));
  vec3 mesh_color = mesh.rgb;
  float mesh_opacity = mesh.a;
  vec3 mat_diffuse = vec3(0.0);
  vec3 specular = vec3(0.0);
  float discard_alpha = 1.0;
  bool can_discard = false;
  vec3 g0 = vec3(1.0);
  vec3 tsa = vec3(1.0); // u_tex_sample_alpha: always 1 in the host

  if (ps == 0) { mat_diffuse = mesh_color * tex1.rgb; }
  else if (ps == 1) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 2) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex2.a; can_discard = true; }
  else if (ps == 3) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex2.a * 2.0; can_discard = true; }
  else if (ps == 4) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; }
  else if (ps == 5) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; }
  else if (ps == 6) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }
  else if (ps == 7) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a * tex2.a * 2.0; can_discard = true; }
  else if (ps == 8) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb; }
  else if (ps == 9) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 10) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb; }
  else if (ps == 11) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 12) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); }
  else if (ps == 13) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a; }
  else if (ps == 14) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }
  else if (ps == 15) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); specular = tex3.rgb * tex3.a * tsa.b; }
  else if (ps == 16) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a; }
  else if (ps == 17) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a * (0.3 * tex2.r + 0.59 * tex2.g + 0.11 * tex2.b); can_discard = true; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }
  else if (ps == 18) { mat_diffuse = mesh_color * mix(mix(tex1.rgb, tex2.rgb, vec3(tex2.a)), tex1.rgb, vec3(tex1.a)); }
  else if (ps == 19) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)); }
  else if (ps == 20) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * tsa.g; }
  else if (ps == 21) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb * (1.0 - tex1.a); }
  else if (ps == 22) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb, tex1.rgb, vec3(tex1.a)); }
  else if (ps == 23) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a * tsa.g; }
  else if (ps == 24) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); specular = tex1.rgb * tex1.a * tsa.r; }
  else if (ps == 25) { float go = clamp(tex3.a * tsa.b, 0.0, 1.0); mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)) * (1.0 - go); specular = tex3.rgb * go; }
  else if (ps == 26) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(tsa.g, 0.0, 1.0))), tex3, vec4(clamp(tsa.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a; can_discard = true; }
  else if (ps == 27) { mat_diffuse = mesh_color * mix(mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)), tex1.rgb, vec3(tex1.a)); }
  else if (ps == 28) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(tsa.g, 0.0, 1.0))), tex3, vec4(clamp(tsa.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a * tex4.a; can_discard = true; }
  else if (ps == 29) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); }
  else if (ps == 30) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 31) { mat_diffuse = mesh_color * tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)); discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 32) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); }
  else if (ps == 33) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 34) { discard_alpha = tex1.a; can_discard = true; }
  else if (ps == 35) { vec4 combined = tex1 * tex2 * tex3; mat_diffuse = mesh_color * combined.rgb; discard_alpha = combined.a; can_discard = true; }
  else if (ps == 36) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }
  else { mat_diffuse = mesh_color * tex1.rgb; }

  float final_opacity;
  bool do_discard = false;
  int bm = d.egxBlendMode;
  if (bm == 13) { final_opacity = discard_alpha * mesh_opacity; }
  else if (bm == 1) { final_opacity = mesh_opacity; if (can_discard && discard_alpha < d.combinerAlphaTest) do_discard = true; }
  else if (bm == 0) { final_opacity = mesh_opacity; }
  else if (bm == 4 || bm == 5) { final_opacity = discard_alpha * mesh_opacity; if (can_discard && discard_alpha < d.combinerAlphaTest) do_discard = true; }
  else { final_opacity = discard_alpha * mesh_opacity; }
  if (do_discard) discard;

  vec4 c = vec4(mat_diffuse + specular * d.specularWeight, final_opacity);
  float glint_d = distance(vTc[1], vec2(0.60, 0.68));
  c.rgb += d.metalSpec * exp(-glint_d * glint_d * d.metalTight);
  outColor = c;
}
