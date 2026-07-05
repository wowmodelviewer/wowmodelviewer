/*
 * ModelRenderPass.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelRenderPass.h"

#include "ModelColor.h"
#include "ModelTransparency.h"
#include "TextureAnim.h"
#include "video.h"
#include "wow_enums.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include "GL/glew.h"
#include "glm/gtc/type_ptr.hpp"

// ---------------------------------------------------------------------------
// M2 multi-texture combiner (GLSL).
//
// War Within+ "cosmic/void" materials combine up to four textures per material via
// a Blizzard combiner shader (selected by setLOD into ModelRenderPass::pixelShader).
// WMV's fixed-function path can only show one texture per material, so those capes
// and orb halos rendered solid white. Here we provide a small *fragment-only* GLSL
// program implementing the M2 multi-texture pixel-combiner that reproduces the
// combine. Being fragment-only keeps WMV's entire fixed-function vertex pipeline
// intact: transform, environment/sphere-map texgen, texture-matrix UV animation and
// per-vertex lighting all still run and feed gl_TexCoord[0..3] / gl_Color into the
// combiner. The program is only bound for op_count>1 passes, so ordinary single-
// texture rendering is completely unchanged.
//
// GLSL 1.20 is used (guaranteed on any GL2.1 context) - it has no switch statement,
// so the combiner is expressed as an if/else chain.
// ---------------------------------------------------------------------------
namespace
{
  GLuint g_combinerProg = 0;
  bool   g_combinerTried = false;
  GLint  g_uTex0 = -1, g_uTex1 = -1, g_uTex2 = -1, g_uTex3 = -1;
  GLint  g_uPixelShader = -1, g_uBlendMode = -1, g_uAlphaTest = -1, g_uTexSampleAlpha = -1;
  GLint  g_uSpecularWeight = -1; // Stage 2: 0 drops the specular/glow lobe (legacy), 1 folds it in
  GLint  g_uEnvBoost = -1;       // Metal sheen: fresnel gain on the sphere-map reflection (0 = identity)
  GLint  g_uMetalSpec = -1;      // Metal specular hotspot intensity (0 = off)
  GLint  g_uMetalTight = -1;     // Metal specular hotspot tightness

  // raw M2 blend mode (0..7) -> the "EGX" blend mode used by the combiner's
  // final-opacity logic (matches M2BLEND_TO_EGX in M2RendererGL.js).
  int egxBlendMode(int raw)
  {
    static const int t[8] = { 0, 1, 2, 10, 3, 4, 5, 13 };
    return (raw >= 0 && raw < 8) ? t[raw] : 0;
  }

  // Pixel shaders whose second texture feeds the ADDITIVE / specular lobe rather than a diffuse
  // reflection: these are glow / energy / cloth-effect materials (e.g. War Within cosmic capes),
  // NOT reflective metal. They may still carry an Env unit (uvSource[1]==2), so the metal specular
  // hotspot -- which is added on top of the output -- must be suppressed for them, or a cloth cape
  // would pick up a metal glint. Reflective metals (mod2x tex2 in the diffuse term: ps 2-7,9,11,12,
  // 15,19,22,25,27,...) are NOT listed and do get the hotspot.
  bool isAdditiveEnvPixelShader(int ps)
  {
    return ps == 8 || ps == 10 || ps == 13 || ps == 14 || ps == 16 ||
           ps == 17 || ps == 20 || ps == 21 || ps == 23 || ps == 24;
  }

  static const char * COMBINER_FRAGMENT_SRC =
    "#version 120\n"
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform sampler2D u_tex2;\n"
    "uniform sampler2D u_tex3;\n"
    "uniform int   u_pixel_shader;\n"
    "uniform int   u_blend_mode;\n"      // EGX blend mode
    "uniform float u_alpha_test;\n"
    "uniform vec3  u_tex_sample_alpha;\n"
    "uniform float u_specular_weight;\n"  // Stage 2: fold the specular/env-reflection/glow lobe (1) or drop it (0, legacy)
    "uniform float u_env_boost;\n"        // Metal sheen: fresnel-weighted gain on the sphere-map reflection (0 = identity)
    "uniform float u_metal_spec;\n"       // Metal specular hotspot intensity (0 = off / identity)
    "uniform float u_metal_tight;\n"      // Hotspot tightness (gaussian falloff exponent)
    "void main() {\n"
    "  vec2 uv1 = gl_TexCoord[0].xy;\n"
    "  vec2 uv2 = gl_TexCoord[1].xy;\n"
    "  vec2 uv3 = gl_TexCoord[2].xy;\n"
    "  if (u_pixel_shader == 26 || u_pixel_shader == 27 || u_pixel_shader == 28) { uv2 = uv1; uv3 = uv1; }\n"
    "  vec4 tex1 = texture2D(u_tex0, uv1);\n"
    "  vec4 tex2 = texture2D(u_tex1, uv2);\n"
    "  vec4 tex3 = texture2D(u_tex2, uv3);\n"
    "  vec4 tex4 = texture2D(u_tex3, gl_TexCoord[1].xy);\n"
    // Metal sheen: on env-reflection materials (unit 1 is a sphere-map Env unit, uvSource[1]==2)
    // tex2 is the reflection. Boost it with a grazing-angle (fresnel-like) weight derived from the
    // sphere-map coord: 0 head-on, ->1 at the silhouette. u_env_boost is 0 for every non-env pass,
    // so this multiply is an exact identity (x1) except on the metals we intend to enhance.
    "  float env_fres = clamp(pow(length(gl_TexCoord[1].xy * 2.0 - 1.0), 1.5), 0.0, 1.0);\n"
    "  tex2.rgb *= (1.0 + u_env_boost * (0.30 + 0.70 * env_fres));\n"
    "  vec3 mesh_color = gl_Color.rgb;\n"          // already lit by the fixed pipeline
    "  float mesh_opacity = gl_Color.a;\n"
    "  vec3 mat_diffuse = vec3(0.0);\n"
    "  vec3 specular = vec3(0.0);\n"
    "  float discard_alpha = 1.0;\n"
    "  bool can_discard = false;\n"
    "  int ps = u_pixel_shader;\n"
    "  vec3 g0 = vec3(1.0);\n"
    "  if (ps == 0) { mat_diffuse = mesh_color * tex1.rgb; }\n"
    "  else if (ps == 1) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 2) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex2.a; can_discard = true; }\n"
    "  else if (ps == 3) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex2.a * 2.0; can_discard = true; }\n"
    "  else if (ps == 4) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; }\n"
    "  else if (ps == 5) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; }\n"
    "  else if (ps == 6) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }\n"
    "  else if (ps == 7) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a * tex2.a * 2.0; can_discard = true; }\n"
    "  else if (ps == 8) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb; }\n"
    "  else if (ps == 9) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb * 2.0; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 10) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb; }\n"
    "  else if (ps == 11) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 12) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 13) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a; }\n"
    "  else if (ps == 14) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }\n"
    "  else if (ps == 15) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)); specular = tex3.rgb * tex3.a * u_tex_sample_alpha.b; }\n"
    "  else if (ps == 16) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a; }\n"
    "  else if (ps == 17) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a * (0.3 * tex2.r + 0.59 * tex2.g + 0.11 * tex2.b); can_discard = true; specular = tex2.rgb * tex2.a * (1.0 - tex1.a); }\n"
    "  else if (ps == 18) { mat_diffuse = mesh_color * mix(mix(tex1.rgb, tex2.rgb, vec3(tex2.a)), tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 19) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)); }\n"
    "  else if (ps == 20) { mat_diffuse = mesh_color * tex1.rgb; specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g; }\n"
    "  else if (ps == 21) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a + tex2.a; can_discard = true; specular = tex2.rgb * (1.0 - tex1.a); }\n"
    "  else if (ps == 22) { mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb, tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 23) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g; }\n"
    "  else if (ps == 24) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); specular = tex1.rgb * tex1.a * u_tex_sample_alpha.r; }\n"
    "  else if (ps == 25) { float go = clamp(tex3.a * u_tex_sample_alpha.b, 0.0, 1.0); mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)) * (1.0 - go); specular = tex3.rgb * go; }\n"
    "  else if (ps == 26) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a; can_discard = true; }\n"
    "  else if (ps == 27) { mat_diffuse = mesh_color * mix(mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)), tex1.rgb, vec3(tex1.a)); }\n"
    "  else if (ps == 28) { vec4 mx = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0))); mat_diffuse = mesh_color * mx.rgb; discard_alpha = mx.a * tex4.a; can_discard = true; }\n"
    "  else if (ps == 29) { mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a)); }\n"
    "  else if (ps == 30) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 31) { mat_diffuse = mesh_color * tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)); discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 32) { mat_diffuse = mesh_color * mix(tex1.rgb * mix(g0, tex2.rgb, vec3(tex2.a)), tex3.rgb, vec3(tex3.a)); }\n"
    "  else if (ps == 33) { mat_diffuse = mesh_color * tex1.rgb; discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 34) { discard_alpha = tex1.a; can_discard = true; }\n"
    "  else if (ps == 35) { vec4 combined = tex1 * tex2 * tex3; mat_diffuse = mesh_color * combined.rgb; discard_alpha = combined.a; can_discard = true; }\n"
    "  else if (ps == 36) { mat_diffuse = mesh_color * tex1.rgb * tex2.rgb; discard_alpha = tex1.a * tex2.a; can_discard = true; }\n"
    "  else { mat_diffuse = mesh_color * tex1.rgb; }\n"
    "  float final_opacity;\n"
    "  bool do_discard = false;\n"
    "  if (u_blend_mode == 13) { final_opacity = discard_alpha * mesh_opacity; }\n"
    "  else if (u_blend_mode == 1) { final_opacity = mesh_opacity; if (can_discard && discard_alpha < u_alpha_test) do_discard = true; }\n"
    "  else if (u_blend_mode == 0) { final_opacity = mesh_opacity; }\n"
    "  else if (u_blend_mode == 4 || u_blend_mode == 5) { final_opacity = discard_alpha * mesh_opacity; if (can_discard && discard_alpha < u_alpha_test) do_discard = true; }\n"
    "  else { final_opacity = discard_alpha * mesh_opacity; }\n"
    "  if (do_discard) discard;\n"
    // Stage 2: the specular lobe is the additive env-reflection / rim-glow term (metals, gems,
    // eyes, glowing runes). The real client adds it; we were dropping it. u_specular_weight is 0
    // by default (legacy: lobe dropped, shipped materials unchanged), 1 when Stage 2 is enabled.
    "  gl_FragColor = vec4(mat_diffuse + specular * u_specular_weight, final_opacity);\n"
    // Metal specular hotspot: a virtual studio light at a fixed sphere-map position. The env unit's
    // sphere-map coord (gl_TexCoord[1]) encodes this fragment's eye-space normal, so a gaussian
    // falloff around the light spot produces a crisp view-dependent glint that slides across the
    // surface as the model orbits -- a real environment specular. Added on top (white). u_metal_spec
    // is 0 for every non-lit-metal pass, so this is an exact identity except on the metals we target.
    "  float glint_d = distance(gl_TexCoord[1].xy, vec2(0.60, 0.68));\n"
    "  gl_FragColor.rgb += u_metal_spec * exp(-glint_d * glint_d * u_metal_tight);\n"
    "}\n";

  // Lazily compile + link the fragment-only combiner program. Returns 0 on failure
  // (caller then falls back to the legacy fixed-function path).
  GLuint ensureCombinerProgram()
  {
    if (g_combinerTried)
      return g_combinerProg;
    g_combinerTried = true;

    if (!video.supportGLSL || !video.supportOGL20)
    {
      LOG_INFO << "M2 combiner shader disabled (no GLSL / OpenGL 2.0 support).";
      return 0;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &COMBINER_FRAGMENT_SRC, NULL);
    glCompileShader(fs);

    GLint ok = GL_FALSE;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
      char buf[2048]; GLsizei len = 0;
      glGetShaderInfoLog(fs, sizeof(buf), &len, buf);
      LOG_ERROR << "M2 combiner fragment shader failed to compile:" << buf;
      glDeleteShader(fs);
      return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(fs); // flagged for deletion once detached at program delete

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
      char buf[2048]; GLsizei len = 0;
      glGetProgramInfoLog(prog, sizeof(buf), &len, buf);
      LOG_ERROR << "M2 combiner program failed to link:" << buf;
      glDeleteProgram(prog);
      return 0;
    }

    g_uTex0 = glGetUniformLocation(prog, "u_tex0");
    g_uTex1 = glGetUniformLocation(prog, "u_tex1");
    g_uTex2 = glGetUniformLocation(prog, "u_tex2");
    g_uTex3 = glGetUniformLocation(prog, "u_tex3");
    g_uPixelShader = glGetUniformLocation(prog, "u_pixel_shader");
    g_uBlendMode = glGetUniformLocation(prog, "u_blend_mode");
    g_uAlphaTest = glGetUniformLocation(prog, "u_alpha_test");
    g_uTexSampleAlpha = glGetUniformLocation(prog, "u_tex_sample_alpha");
    g_uSpecularWeight = glGetUniformLocation(prog, "u_specular_weight");
    g_uEnvBoost = glGetUniformLocation(prog, "u_env_boost");
    g_uMetalSpec = glGetUniformLocation(prog, "u_metal_spec");
    g_uMetalTight = glGetUniformLocation(prog, "u_metal_tight");

    g_combinerProg = prog;
    LOG_INFO << "M2 combiner shader compiled (program" << prog << ").";
    return prog;
  }
}

ModelRenderPass::ModelRenderPass(WoWModel * m, int geo):
  useTex2(false), useEnvMap(false), cull(false), trans(false),
  unlit(false), noZWrite(false), billboard(false),
  texanim(-1), color(-1), opacity(-1), blendmode(-1), tex(INVALID_TEX),
  swrap(false), twrap(false), ocol(0.0f, 0.0f, 0.0f, 0.0f), ecol(0.0f, 0.0f, 0.0f, 0.0f),
  model(m), geoIndex(geo), specialTex(-1),
  textureCount(1), pixelShader(-1), vertexShader(-1),
  tex2(INVALID_TEX), tex3(INVALID_TEX), tex4(INVALID_TEX), texanim2(-1), combinerActive(false),
  m2Variant(M2SV_FALLBACK)
{
  uvSource[0] = 0; uvSource[1] = 1; uvSource[2] = 3; uvSource[3] = 3;
}

const char * ModelRenderPass::m2VariantName(int v)
{
  switch (v)
  {
  case M2SV_FIXED_OPAQUE:    return "FixedOpaque";
  case M2SV_FIXED_ALPHA:     return "FixedAlpha";
  case M2SV_FIXED_ADD:       return "FixedAdditive";
  case M2SV_FIXED_ENV:       return "FixedEnv";
  case M2SV_COMBINER:        return "Combiner";
  case M2SV_COMBINER_SINGLE: return "CombinerSingle";
  default:                   return "Fallback";
  }
}

void ModelRenderPass::deinit()
{
  // Tear down the multi-texture combiner state first, so the rest of this function
  // (and the next pass) sees a clean single-texture-unit, fixed-function pipeline.
  if (combinerActive)
  {
    glUseProgram(0);
    for (int u = 1; u < 4; u++)
    {
      glActiveTexture(GL_TEXTURE0 + u);
      // pop this unit's animated texture matrix if init() pushed one
      if (u == 1 && texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[u] != 2)
      {
        glMatrixMode(GL_TEXTURE);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
      }
      glDisable(GL_TEXTURE_GEN_S);
      glDisable(GL_TEXTURE_GEN_T);
      glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);
    if (uvSource[0] == 2)
    {
      glDisable(GL_TEXTURE_GEN_S);
      glDisable(GL_TEXTURE_GEN_T);
    }
    combinerActive = false;
  }

  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  // Only BM_TRANSPARENT dirtied the alpha ref (to 0.5). Restore the per-frame default (0.8, set
  // in modelcanvas RenderModel) just for those passes -- symmetric with init(), and so the 0.5
  // can't leak into particles/other draws that enable GL_ALPHA_TEST without their own glAlphaFunc.
  if (blendmode == BM_TRANSPARENT)
    glAlphaFunc(GL_GEQUAL, 0.8f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (noZWrite)
    glDepthMask(GL_TRUE);

  // pop unit 0's animated texture matrix (pushed in init()). Be explicit about the
  // active unit + matrix mode: the combiner teardown above may have left either in a
  // different state, and popping the wrong stack would corrupt the modelview matrix.
  if (texanim!=-1)
  {
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

  if (unlit)
    glEnable(GL_LIGHTING);

  //if (billboard)
  //  glPopMatrix();

  if (cull)
    glDisable(GL_CULL_FACE);

  if (useEnvMap)
  {
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
  }

  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /*
    if (useTex2)
    {
      glDisable(GL_TEXTURE_2D);
      glActiveTextureARB(GL_TEXTURE0);
    }
   */

  if (opacity!=-1 || color!=-1)
  {
    GLfloat czero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glMaterialfv(GL_FRONT, GL_EMISSION, czero);

    //glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_AMBIENT, ocol);
    //ocol = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_DIFFUSE, ocol);
  }
}


namespace
{
  // Peak value an AnimatedShort track reaches over its animation cycle: the maximum of its
  // keyframes, exact for NONE/LINEAR interpolation (the only types opacity tracks use). Mirrors
  // getValue()'s sequence routing -- a global-sequence track reads track 0 (and a zero-length
  // global sequence never plays, yielding the 0 default), a plain track reads the requested
  // animation. Used by init(forExport = true); see the header.
  float trackPeakValue(const AnimatedShort & track, ssize_t anim)
  {
    if (track.seq >= 0 && track.seq < (ssize_t)track.globals.size())
    {
      if (!track.globals[track.seq])
        return 0.0f;
      anim = 0;
    }
    if (anim < 0 || anim >= MAX_ANIMATED || track.data[anim].empty())
      return 0.0f;
    float peak = track.data[anim][0];
    for (const float v : track.data[anim])
      if (v > peak)
        peak = v;
    return peak;
  }
}

bool ModelRenderPass::init(bool forExport)
{
  // Forensic-only escape hatch (compare with WMV_MATDUMP / WMV_TEST_GLOBALTIME in the FBX
  // exporter): force the legacy instant-only visibility gating even for exports, so the
  // "pass dropped because its opacity animation was at 0 at the export instant" behaviour
  // can be reproduced deterministically for testing. Never set in normal use.
  static const bool instantGateForced = (getenv("WMV_TEST_INSTANT_GATE") != NULL);
  if (instantGateForced)
    forExport = false;

  // May as well check that we're going to render the geoset before doing all this crap.
  if (!model || geoIndex == -1 || !model->geosets[geoIndex]->display)
    return false;

  // COLOUR
  // Get the colour and transparency and check that we should even render
  ocol = glm::vec4(1.0f, 1.0f, 1.0f, model->trans);
  ecol = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

  // emissive colors
  if (color != -1 && color < (int16)model->colors.size() && model->colors[color].color.uses(0))
  {
    glm::vec3 c;
    /* Alfred 2008.10.02 buggy opacity make model invisible, TODO */
    c = model->colors[color].color.getValue(0, model->animtime);
    if (model->colors[color].opacity.uses(model->anim))
    {
      ocol.w = forExport ? trackPeakValue(model->colors[color].opacity, model->anim)
                         : model->colors[color].opacity.getValue(model->anim, model->animtime);

      // Forensic (WMV_MATDUMP, export path only): dump the colour-opacity track, like the
      // transparency-track dump below -- this is the other animated input to the visibility
      // gate (ecol.w), and e.g. eye-glow blinks can be keyed on either track.
      static const bool matdumpCol = (getenv("WMV_MATDUMP") != NULL);
      if (forExport && matdumpCol)
      {
        const AnimatedShort & op = model->colors[color].opacity;
        const bool glob = (op.seq >= 0 && op.seq < (ssize_t)op.globals.size());
        const size_t a = (op.seq > -1) ? 0 : (size_t)model->anim;
        QString keys;
        if (a < MAX_ANIMATED)
          for (size_t k = 0; k < op.data[a].size() && k < 32; k++)
            keys += QString(" %1:%2").arg(k < op.times[a].size() ? (qulonglong)op.times[a][k] : 0)
                                     .arg(op.data[a][k]);
        LOG_INFO << "[matdump-colopacity] geoIndex" << geoIndex << "colorIdx" << (int)color
                 << "seq" << (int)op.seq << "period" << (glob ? (qulonglong)op.globals[op.seq] : 0)
                 << "keys(t:v)" << qPrintable(keys);
      }
    }

    if (unlit)
    {
      ocol.x = c.x; ocol.y = c.y; ocol.z = c.z;
    }
    else
      ocol.x = ocol.y = ocol.z = 0;

    ecol = glm::vec4(c, ocol.w);
    glMaterialfv(GL_FRONT, GL_EMISSION, glm::value_ptr(ecol));
  }

  // opacity
  if (opacity != -1 &&
      opacity < (int16)model->transparency.size() &&
      model->transparency[opacity].trans.uses(0))
  {
    // Alfred 2008.10.02 buggy opacity make model invisible, TODO
    ocol.w *= forExport ? trackPeakValue(model->transparency[opacity].trans, 0)
                        : model->transparency[opacity].trans.getValue(0, model->animtime);

    // Forensic (WMV_MATDUMP, export path only): dump this pass's opacity track -- sequence,
    // cycle period and keyframes -- so a test can see exactly where the cycle's zero-windows
    // are (e.g. to pin WMV_TEST_GLOBALTIME inside an eye-glow blink). transparency is
    // friend-only, so this lives here rather than in the exporter's matdump block.
    static const bool matdump = (getenv("WMV_MATDUMP") != NULL);
    if (forExport && matdump)
    {
      const AnimatedShort & tr = model->transparency[opacity].trans;
      const bool glob = (tr.seq >= 0 && tr.seq < (ssize_t)tr.globals.size());
      QString keys;
      for (size_t k = 0; k < tr.data[0].size() && k < 32; k++)
        keys += QString(" %1:%2").arg(k < tr.times[0].size() ? (qulonglong)tr.times[0][k] : 0)
                                 .arg(tr.data[0][k]);
      LOG_INFO << "[matdump-opacity] geoIndex" << geoIndex << "opacityIdx" << (int)opacity
               << "seq" << (int)tr.seq << "period" << (glob ? (qulonglong)tr.globals[tr.seq] : 0)
               << "keys(t:v)" << qPrintable(keys);
    }
  }

  // exit and return false before affecting the opengl render state
  // (with forExport, ocol/ecol.w hold cycle peaks, so this only rejects passes
  // that are invisible at every instant of their animation)
  if (!((ocol.w > 0) && (color == -1 || ecol.w > 0)))
    return false;


  // TEXTURE
  // bind to our texture
  GLuint texId = model->getGLTexture(tex);
  if (texId != INVALID_TEX)
    glBindTexture(GL_TEXTURE_2D, texId);

  // ALPHA BLENDING
  // blend mode
  
  switch (blendmode)
  {
  case BM_OPAQUE:           // 0
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_TRANSPARENT:      // 1
    glEnable(GL_ALPHA_TEST);
    // Alpha-key cutout: the game keys at ~0.5. The GLSL combiner already uses 0.5, but the
    // fixed path used to inherit a global glAlphaFunc of 0.8/0.9, which over-clipped thin
    // foliage/hair edges. Pin it here to match (deinit disables the test again).
    glAlphaFunc(GL_GEQUAL, 0.5f);
    glBlendFunc(GL_ONE, GL_ZERO);
    break;
  case BM_ALPHA_BLEND:      // 2
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_ADDITIVE:         // 3
    glEnable(GL_BLEND);
    // No-alpha additive glow. The game uses ONE/ONE; the old SRC_COLOR/ONE squared the
    // source, darkening/desaturating glows (Ragnaros, emissive runes, spell orbs).
    glBlendFunc(GL_ONE, GL_ONE);
    break;
  case BM_ADDITIVE_ALPHA:   // 4
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    break;
  case BM_MODULATE:           // 5
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    break;
  case BM_MODULATEX2:      // 6
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    break;
  case BM_7:                 // 7, new in WoD
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    break;
  default:
    LOG_ERROR << "Unknown blendmode:" << blendmode;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  if (cull)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  // no writing to the depth buffer.
  if (noZWrite)
    glDepthMask(GL_FALSE);
  else
    glDepthMask(GL_TRUE);

  // Texture wrapping around the geometry
  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  // Environmental mapping, material, and effects
  if (useEnvMap)
  {
    // Turn on the 'reflection' shine, using 18.0f as that is what WoW uses based on the reverse engineering
    // This is now set in InitGL(); - no need to call it every render.
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);

    // env mapping
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);

    const GLint maptype = GL_SPHERE_MAP;
    //const GLint maptype = GL_REFLECTION_MAP_ARB;

    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, maptype);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, maptype);
  }

  if (texanim != -1 && 
      texanim < (int16)model->texAnims.size())
  {
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();

    model->texAnims[texanim].setup(texanim);
  }

  // color
  glColor4fv(glm::value_ptr(ocol));
  //glMaterialfv(GL_FRONT, GL_SPECULAR, ocol);

  // don't use lighting on the surface
  if (unlit)
    glDisable(GL_LIGHTING);

  if (blendmode<=1 && ocol.w<1.0f)
    glEnable(GL_BLEND);

  // --- GLSL M2 combiner path ----------------------------------------------------
  // Armed for materials the shader-mapping layer routed to the combiner: M2SV_COMBINER
  // (op_count>1 cosmic/void materials, always) and M2SV_COMBINER_SINGLE (a non-trivial
  // single-texture material, only when the opt-in single-texture combiner is enabled --
  // otherwise single-texture passes are mapped to a fixed variant and take the legacy
  // fixed-function path below, unchanged). The single-texture arm fills units 1..3 from
  // unit 0, and render() emits only unit-0 UVs for textureCount==1, so it is well-defined.
  combinerActive = false;
  static const bool combinerDisabled = (getenv("WMV_NO_COMBINER") != NULL);
  if ((m2Variant == M2SV_COMBINER || m2Variant == M2SV_COMBINER_SINGLE) &&
      pixelShader >= 0 && video.supportMultiTex && !combinerDisabled)
  {
    GLuint prog = ensureCombinerProgram();
    if (prog)
    {
      combinerActive = true;

      // Bind units 1..3. Unused units are filled with unit-0's texture so every
      // sampler the fragment shader might read is well-defined.
      for (int u = 1; u < 4; u++)
      {
        uint16 tIndex = tex;
        if (u < textureCount)
          tIndex = (u == 1) ? tex2 : ((u == 2) ? tex3 : tex4);
        GLuint glTex = model->getGLTexture(tIndex);

        glActiveTexture(GL_TEXTURE0 + u);
        glEnable(GL_TEXTURE_2D);
        if (glTex != ModelRenderPass::INVALID_TEX)
          glBindTexture(GL_TEXTURE_2D, glTex);

        if (u < textureCount && uvSource[u] == 2)
        {
          // environment / sphere map for this unit
          glEnable(GL_TEXTURE_GEN_S);
          glEnable(GL_TEXTURE_GEN_T);
          glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
          glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        }
        else
        {
          glDisable(GL_TEXTURE_GEN_S);
          glDisable(GL_TEXTURE_GEN_T);
        }

        // Animate this unit's UVs. War Within cosmic/void materials scroll their glow
        // layer via a per-unit texture transform (e.g. lichlord's green energy). The
        // active texture unit is GL_TEXTURE0+u, so the GL_TEXTURE matrix we load here is
        // unit u's. Env units use generated sphere-map coords and are left unanimated,
        // (the transform is skipped for env channels).
        if (u == 1 && texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[u] != 2)
        {
          glMatrixMode(GL_TEXTURE);
          glPushMatrix();
          model->texAnims[texanim2].setup(texanim2);
          glMatrixMode(GL_MODELVIEW);
        }
      }

      glActiveTexture(GL_TEXTURE0);
      if (uvSource[0] == 2)
      {
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
      }

      glUseProgram(prog);
      if (g_uTex0 >= 0) glUniform1i(g_uTex0, 0);
      if (g_uTex1 >= 0) glUniform1i(g_uTex1, 1);
      if (g_uTex2 >= 0) glUniform1i(g_uTex2, 2);
      if (g_uTex3 >= 0) glUniform1i(g_uTex3, 3);
      if (g_uPixelShader >= 0) glUniform1i(g_uPixelShader, pixelShader);
      if (g_uBlendMode >= 0) glUniform1i(g_uBlendMode, egxBlendMode(blendmode));
      if (g_uAlphaTest >= 0) glUniform1f(g_uAlphaTest, 0.501960814f);
      if (g_uTexSampleAlpha >= 0) glUniform3f(g_uTexSampleAlpha, 1.0f, 1.0f, 1.0f);
      // Stage 2 (opt-in via env WMV_M2_STAGE2): fold the combiner's specular/env-reflection/glow
      // lobe into the output so metal/gem/eye env sheen and rim-glow appear. Default 0 keeps the
      // legacy behaviour (lobe dropped). Shipped void/cosmic capes compute no specular either way,
      // so they are unaffected; this only changes materials whose pixel shader has a specular term.
      static const bool stage2 = (getenv("WMV_M2_STAGE2") != NULL);
      if (g_uSpecularWeight >= 0) glUniform1f(g_uSpecularWeight, stage2 ? 1.0f : 0.0f);

      // Metal sheen (default-ON): fresnel-boost the sphere-map reflection on env-reflection metals
      // (unit 1 is an Env unit, uvSource[1]==2) for opaque / alpha-key passes -- the shiny plate &
      // weapon look. It is an exact identity for every other material (envBoostForPass 0 -> the
      // shader multiply becomes x1), and is disabled during FBX export bakes so exported textures
      // stay byte-identical. Tunable via WMV_ENV_BOOST (0 reproduces the legacy image exactly).
      static const float envBoost = (getenv("WMV_ENV_BOOST") ? (float)atof(getenv("WMV_ENV_BOOST")) : 0.45f);
      float envBoostForPass = 0.0f;
      if (!forExport && uvSource[1] == 2 && (blendmode == BM_OPAQUE || blendmode == BM_TRANSPARENT))
        envBoostForPass = envBoost;
      if (g_uEnvBoost >= 0) glUniform1f(g_uEnvBoost, envBoostForPass);

      // Metal specular hotspot (default-ON): a crisp light-driven glint on LIT env-reflection metals
      // (opaque / alpha-key). Off for self-illuminated (unlit) parts, every non-env material, and FBX
      // export bakes. u_metal_spec 0 makes the shader's hotspot term an exact identity (+0). Tunable
      // via WMV_METAL_SPEC (0 = no glint); WMV_METAL_TIGHT tunes the hotspot size.
      static const float metalSpec = (getenv("WMV_METAL_SPEC") ? (float)atof(getenv("WMV_METAL_SPEC")) : 0.26f);
      static const float metalTight = (getenv("WMV_METAL_TIGHT") ? (float)atof(getenv("WMV_METAL_TIGHT")) : 55.0f);
      float metalSpecForPass = 0.0f;
      if (!forExport && !unlit && uvSource[1] == 2 && !isAdditiveEnvPixelShader(pixelShader) &&
          (blendmode == BM_OPAQUE || blendmode == BM_TRANSPARENT))
        metalSpecForPass = metalSpec;
      if (g_uMetalSpec >= 0) glUniform1f(g_uMetalSpec, metalSpecForPass);
      if (g_uMetalTight >= 0) glUniform1f(g_uMetalTight, metalTight);
    }
  }

  return true;
}

void ModelRenderPass::render(bool animated, bool bakeUVSpace)
{
  ModelGeosetHD * geoset = model->geosets[geoIndex];

  // Multi-texture combiner pass: draw in immediate mode so we can hand every texture
  // unit its own UV (uv set 0, uv set 1, or - for env units - texgen-generated coords).
  // WIP_DH_SUPPORT forces non-VBO mode, so model->vertices/normals are valid CPU arrays.
  if (combinerActive)
  {
    glBegin(GL_TRIANGLES);
    for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
    {
      uint32 a = model->indices[b];
      const ModelVertex & ov = model->origVertices[a];
      const float u0 = ov.texcoords.x, v0 = ov.texcoords.y;
      // uv set 1 lives in the two trailing ints of the modern 48-byte M2 vertex.
      const float u1 = *reinterpret_cast<const float *>(&ov.unk1);
      const float v1 = *reinterpret_cast<const float *>(&ov.unk2);
      for (int unit = 0; unit < textureCount && unit < 4; unit++)
      {
        if (uvSource[unit] == 2)
          continue; // env: coordinates come from sphere-map texgen
        if (uvSource[unit] == 1)
          glMultiTexCoord2f(GL_TEXTURE0 + unit, u1, v1);
        else
          glMultiTexCoord2f(GL_TEXTURE0 + unit, u0, v0);
      }
      glNormal3fv(glm::value_ptr(model->normals[a]));
      if (bakeUVSpace)
      {
        // Map into texture-unit-0's UV space -> clip space ([-1,1]): with an identity MVP and a
        // viewport matching the target texture, this rasterizes per-texel instead of at the
        // mesh's real 3D position. Wrap into [0,1) first (floor, not fmod, so negative UVs wrap
        // correctly too) -- WoW UVs can extend outside [0,1] for tiling/repeat addressing, which
        // the GPU's normal texture sampling wraps for free but a raw position mapping would just
        // clip away, leaving only whatever sliver of the mesh happened to already sit in [0,1].
        const float wu = u0 - floorf(u0);
        const float wv = v0 - floorf(v0);
        glVertex3f(wu * 2.0f - 1.0f, wv * 2.0f - 1.0f, 0.0f);
      }
      else
        glVertex3fv(glm::value_ptr(model->vertices[a]));
    }
    glEnd();
    return;
  }

  // we don't want to render completely transparent parts
  // render
  if (animated)
  {
    
    //glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_SHORT, indices + p.indexStart);
    // a GDC OpenGL Performace Tuning paper recommended glDrawRangeElements over glDrawElements
    // I can't notice a difference but I guess it can't hurt
    if (video.supportVBO && video.supportDrawRangeElements)
    {
      glDrawRangeElements(GL_TRIANGLES, geoset->vstart, geoset->vstart + geoset->vcount, geoset->icount, GL_UNSIGNED_SHORT, &model->indices[geoset->istart]);
    }
    else
    {
      glBegin(GL_TRIANGLES);
      for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
      {
        uint32 a = model->indices[b];
        glNormal3fv(glm::value_ptr(model->normals[a]));
        glTexCoord2fv(glm::value_ptr(model->origVertices[a].texcoords));
        glVertex3fv(glm::value_ptr(model->vertices[a]));
        /*
        if (geoset->id == 2401 && k < 10)
        {
          LOG_INFO << "b" << b;
          LOG_INFO << "a" << model->indices[b] << a;
          LOG_INFO << "model->normals[a]" << model->normals[a].x << model->normals[a].y << model->normals[a].z;
          LOG_INFO << "model->vertices[a]" << model->vertices[a].x << model->vertices[a].y << model->vertices[a].z;
        }
        */

      }
      glEnd();
    }
  }
  else
  {
    glBegin(GL_TRIANGLES);
    for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
    {
      uint16 a = model->indices[b];
      glNormal3fv(glm::value_ptr(model->normals[a]));
      glTexCoord2fv(glm::value_ptr(model->origVertices[a].texcoords));
      glVertex3fv(glm::value_ptr(model->vertices[a]));
    }
    glEnd();
  }
}

// See the declaration in ModelRenderPass.h.
void ModelRenderPass::setMaxBlendEquation(bool enable)
{
  glBlendEquation(enable ? GL_MAX : GL_FUNC_ADD);
}

// See the declaration in ModelRenderPass.h. init() applied these matrices from the values the
// live animation loop last computed; this recomputes them for an arbitrary time and reloads the
// matrices in place (setup() starts with glLoadIdentity, so the reload is idempotent against the
// push/pop balance init()/deinit() maintain).
void ModelRenderPass::updateTexAnimMatrices(size_t time, bool resetToIdentity)
{
  // Texture scrolls usually run on GLOBAL sequences, whose evaluation IGNORES the time passed
  // to calc() and reads the module-wide animation clock instead (animated.h getValue: time =
  // globalTime % globals[seq]) -- pin that clock to the requested instant for the recompute,
  // then restore it so the live viewport's own timing is unaffected.
  const size_t savedGlobalTime = globalTime;
  globalTime = time;

  // Units with NO animation track are forced to IDENTITY rather than skipped: the exporter's
  // material loop calls init() without the paired deinit(), so whatever texture matrix an
  // EARLIER animated pass loaded is still in place -- inheriting it silently shifts this pass's
  // sampling (observed as an accent island sampling a neighbouring grey region).
  glMatrixMode(GL_TEXTURE);
  if (!resetToIdentity && texanim >= 0 && texanim < (int16)model->texAnims.size())
  {
    model->texAnims[texanim].calc(model->anim, time);
    model->texAnims[texanim].setup(texanim);
  }
  else
    glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);

  if (combinerActive)
  {
    glActiveTexture(GL_TEXTURE1);
    glMatrixMode(GL_TEXTURE);
    if (!resetToIdentity && texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[1] != 2)
    {
      model->texAnims[texanim2].calc(model->anim, time);
      model->texAnims[texanim2].setup(texanim2);
    }
    else
      glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glActiveTexture(GL_TEXTURE0);
  }

  globalTime = savedGlobalTime;
}