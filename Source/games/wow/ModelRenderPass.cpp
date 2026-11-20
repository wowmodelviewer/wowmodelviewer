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
#include "wow_enums.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <cmath>

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
  int egxBlendMode(int raw)
  {
    static const int t[8] = { 0, 1, 2, 10, 3, 4, 5, 13 };
    return (raw >= 0 && raw < 8) ? t[raw] : 0;
  }

  bool isAdditiveEnvPixelShader(int ps)
  {
    return ps == 8 || ps == 10 || ps == 13 || ps == 14 || ps == 16 ||
           ps == 17 || ps == 20 || ps == 21 || ps == 23 || ps == 24;
  }

  float envFloat(const char * name, float fallback)
  {
    const char * v = getenv(name);
    return v ? (float)atof(v) : fallback;
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
  // init() records its state in 'material' rather than in a GL context, so there is nothing to undo.
  combinerActive = false;
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
  wmv::Material & m = material;
  m = wmv::Material();
  m.tex[0] = model->getTexture(tex);
  m.color = ocol;
  m.emissive = (color != -1) ? ecol : glm::vec4(0.0f);
  m.cull = cull;
  m.depthWrite = !noZWrite;
  m.unlit = unlit;
  m.wrapS = swrap;
  m.wrapT = twrap;
  m.uvSource[0] = useEnvMap ? wmv::UV_SPHERE_MAP : wmv::UV_SET0;

  switch (blendmode)
  {
  case BM_OPAQUE:          m.blend = wmv::BLEND_OPAQUE; break;
  case BM_TRANSPARENT:     m.blend = wmv::BLEND_ALPHA_KEY; m.alphaTest = true; m.alphaRef = 0.5f; break;
  case BM_ALPHA_BLEND:     m.blend = wmv::BLEND_ALPHA; m.blendEnabled = true; break;
  case BM_ADDITIVE:        m.blend = wmv::BLEND_ADD; m.blendEnabled = true; break;
  case BM_ADDITIVE_ALPHA:  m.blend = wmv::BLEND_ADD_ALPHA; m.blendEnabled = true; break;
  case BM_MODULATE:        m.blend = wmv::BLEND_MODULATE; m.blendEnabled = true; break;
  case BM_MODULATEX2:      m.blend = wmv::BLEND_MODULATE_2X; m.blendEnabled = true; break;
  case BM_7:               m.blend = wmv::BLEND_7; m.blendEnabled = true; break;
  default:
    LOG_ERROR << "Unknown blendmode:" << blendmode;
    m.blend = wmv::BLEND_ALPHA; m.blendEnabled = true;
  }

  // The GL path turned blending on for an opaque or alpha-keyed pass that is fading out.
  if (blendmode <= 1 && ocol.w < 1.0f)
    m.blendEnabled = true;

  if (texanim != -1 && texanim < (int16)model->texAnims.size())
    m.texMatrix[0] = model->texAnims[texanim].matrix(texanim);

  combinerActive = false;
  static const bool combinerDisabled = (getenv("WMV_NO_COMBINER") != NULL);
  if ((m2Variant == M2SV_COMBINER || m2Variant == M2SV_COMBINER_SINGLE) && pixelShader >= 0 && !combinerDisabled)
  {
    combinerActive = true;
    m.pixelShader = pixelShader;
    m.textureCount = textureCount;
    for (int u = 0; u < 4; u++)
      m.uvSource[u] = uvSource[u];
    for (int u = 1; u < 4; u++)
    {
      uint16 tIndex = tex;
      if (u < textureCount)
        tIndex = (u == 1) ? tex2 : ((u == 2) ? tex3 : tex4);
      m.tex[u] = model->getTexture(tIndex);
    }
    if (texanim2 >= 0 && texanim2 < (int16)model->texAnims.size() && uvSource[1] != 2)
      m.texMatrix[1] = model->texAnims[texanim2].matrix(texanim2);

    m.egxBlendMode = egxBlendMode(blendmode);
    m.combinerAlphaTest = 0.501960814f;
    static const bool stage2 = (getenv("WMV_M2_STAGE2") != NULL);
    m.specularWeight = stage2 ? 1.0f : 0.0f;

    static const float envBoost = envFloat("WMV_ENV_BOOST", 0.45f);
    m.envBoost = (!forExport && uvSource[1] == 2 && (blendmode == BM_OPAQUE || blendmode == BM_TRANSPARENT))
               ? envBoost : 0.0f;

    static const float metalSpec = envFloat("WMV_METAL_SPEC", 0.26f);
    static const float metalTight = envFloat("WMV_METAL_TIGHT", 55.0f);
    m.metalSpec = (!forExport && !unlit && uvSource[1] == 2 && !isAdditiveEnvPixelShader(pixelShader) &&
                   (blendmode == BM_OPAQUE || blendmode == BM_TRANSPARENT)) ? metalSpec : 0.0f;
    m.metalTight = metalTight;
  }

  return true;
}

void ModelRenderPass::render(bool /*animated*/, bool bakeUVSpace)
{
  wmv::RenderList * list = wmv::RenderList::current();
  if (!list)
    return;

  ModelGeosetHD * geoset = model->geosets[geoIndex];
  if (geoset->istart + geoset->icount > model->indices.size())
    return;

  // The geoset's vertices, as the model last animated them, then its triangles.
  const uint32_t vstart = geoset->vstart;
  const uint32_t vcount = std::min<uint32_t>(geoset->vcount, (uint32_t)model->origVertices.size() - std::min<uint32_t>(vstart, (uint32_t)model->origVertices.size()));
  const uint32_t base = (uint32_t)list->vertices.size();
  list->vertices.reserve(base + vcount);
  for (uint32_t i = 0; i < vcount; i++)
  {
    const uint32_t a = vstart + i;
    const ModelVertex & ov = model->origVertices[a];
    wmv::Vertex v;
    v.pos = model->vertices[a];
    v.normal = model->normals[a];
    v.uv0 = ov.texcoords;
    v.uv1 = glm::vec2(*reinterpret_cast<const float *>(&ov.unk1), *reinterpret_cast<const float *>(&ov.unk2));
    v.color = glm::vec4(1.0f);
    if (bakeUVSpace)
      v.pos = glm::vec3((v.uv0.x - floorf(v.uv0.x)) * 2.0f - 1.0f, (v.uv0.y - floorf(v.uv0.y)) * 2.0f - 1.0f, 0.0f);
    list->vertices.push_back(v);
  }

  const uint32_t first = list->indexMark();
  for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
  {
    const uint32_t a = model->indices[b];
    if (a < vstart || a >= vstart + vcount)
      continue; // an index outside its geoset's range: GL's DrawRangeElements was given the same range
    list->addIndex(base + (a - vstart));
  }

  wmv::Material m = material;
  m.frontFaceCW = model->mirrored_;
  m.wireframe = model->showWireframe;
  m.textured = model->showTexture;
  list->draw(m, first);
}
