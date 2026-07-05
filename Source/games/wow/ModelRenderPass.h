/*
 * ModelRenderPass.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELRENDERPASS_H_
#define _MODELRENDERPASS_H_

#include "types.h"

#include "glm/glm.hpp"

class WoWModel;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MODELRENDERPASS_API_ __declspec(dllexport)
#    else
#        define _MODELRENDERPASS_API_ __declspec(dllimport)
#    endif
#else
#    define _MODELRENDERPASS_API_
#endif


// Explicit "shader variant" an M2 render batch is mapped to, derived from the material's
// shader_id, blend mode, texture-unit count and render flags. The variant drives which render
// path ModelRenderPass::init() takes. Any combination we do not confidently map falls back to
// M2SV_FALLBACK, which keeps the legacy fixed-function behaviour so existing rendering is
// preserved and the change stays safe.
enum M2ShaderVariant
{
  M2SV_FALLBACK = 0,     // unrecognised material -> legacy fixed-function behaviour, unchanged
  M2SV_FIXED_OPAQUE,     // fixed-function modulate, opaque
  M2SV_FIXED_ALPHA,      // fixed-function alpha blend / alpha-key cutout
  M2SV_FIXED_ADD,        // fixed-function additive glow / emissive
  M2SV_FIXED_ENV,        // fixed-function environment / sphere map
  M2SV_COMBINER,         // GLSL multi-texture combiner (op_count > 1)
  M2SV_COMBINER_SINGLE   // GLSL combiner for a non-trivial single-texture material (opt-in)
};

class _MODELRENDERPASS_API_ ModelRenderPass
{
public:

  ModelRenderPass(WoWModel *, int geo);

  // Explicit shader variant this batch was mapped to (see M2ShaderVariant). Chosen once in
  // WoWModel::setLOD and read by init() to pick the render path. Defaults to M2SV_FALLBACK.
  int m2Variant;
  static const char * m2VariantName(int v);

  bool useTex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;

  int16 texanim, color, opacity, blendmode, specialTex;
  uint16 tex;

  // Multi-texture combiner support (War Within+ cosmic/void materials use up to 4
  // textures combined by a Blizzard "M2 combiner shader"). For op_count==1 these stay
  // at their defaults and the legacy fixed-function path is used unchanged. For
  // op_count>1 we resolve the combiner + per-unit UV routing and render through a small
  // GLSL fragment shader implementing the M2 pixel-combiner so the extra textures are combined
  // instead of dropped (which used to leave capes/orbs white).
  int textureCount;            // op_count: number of textures in this unit (1..4)
  int pixelShader;             // combiner id (0..36), -1 if unresolved / single texture
  int vertexShader;            // vertex-combiner id (drives per-unit UV routing), -1 if none
  uint16 tex2, tex3, tex4;     // extra texture indices (model texture-array indices)
  int16 texanim2;              // UV-animation index for texture unit 1 (-1 if none).
                               // unit 0's animation stays in 'texanim' (legacy path).
  // Per texture-unit UV source: 0 = uv set 0 (T1), 1 = uv set 1 (T2),
  // 2 = environment / sphere map (Env), 3 = uv set 2 (T3).
  int8 uvSource[4];

  // runtime flag: set by init() when the GLSL combiner path is active for this pass,
  // read by render()/deinit() so they emit multi-texture coords and tear down state.
  bool combinerActive;

  // texture wrapping
  bool swrap, twrap;

  // colours
  glm::vec4 ocol, ecol;

  WoWModel * model;

  int geoIndex;

  // forExport: evaluate animated visibility over the pass's WHOLE opacity cycle instead of at
  // the current instant. A pass whose opacity animation merely happens to sit at 0 right now --
  // e.g. an eye-glow blinking via a global-sequence transparency track -- is still a visible
  // part of the model, but an exporter sampling one arbitrary instant would nondeterministically
  // drop its material AND its geometry (the same export produced 18 materials in one run and 17
  // in another, depending on where the blink was when the export fired). With forExport set, the
  // opacity/colour-opacity tracks are evaluated at their cycle MAXIMUM, so any ever-visible pass
  // initialises (at peak opacity in ocol/ecol); passes invisible at EVERY instant (track constant
  // 0) are still rejected. The live render path never sets this.
  bool init(bool forExport = false);
  int BlendValueForMode(int mode);

  // bakeUVSpace: only affects the multi-texture combiner path (combinerActive). When true, each
  // vertex's clip-space position is remapped to its own texture-unit-0 UV coordinate instead of
  // its real 3D position, so an offscreen FBO capture rasterizes per-texel into texture space
  // (see FBXExporter::bakeCombinerTexture, which renders this into a texture to export the
  // combiner's real per-pixel output instead of dropping every texture but unit 0). Has no effect
  // outside the combiner path.
  void render(bool animated, bool bakeUVSpace = false);

  // Recompute this pass's texture-animation matrices (unit 0 'texanim', combiner unit 1
  // 'texanim2') for an arbitrary animation time and reload them into the GL texture matrices,
  // exactly as the live path does per frame. resetToIdentity loads plain identity instead --
  // the REST state, i.e. the un-scrolled look a static render (or a viewport whose animation
  // hasn't ticked) shows. Only valid between init() and deinit(). Used by the exporter's
  // multi-instant combiner bake to sweep an animated (scrolling) overlay across its whole
  // cycle -- and deliberately a member here: it calls glActiveTexture, which resolves through
  // GLEW and so must run from code compiled into this module (GLEW's function pointers are
  // per-module and only ever initialised here; a plugin calling it directly crashes).
  void updateTexAnimMatrices(size_t time, bool resetToIdentity = false);

  // Switch the blend equation between per-channel MAX and the normal ADD. Used by the exporter's
  // combiner bake so overlapping additive planes keep their brightest fragment instead of either
  // overwriting each other (blend off) or clipping to white (accumulate). A member here for the
  // same GLEW-per-module reason as updateTexAnimMatrices: glBlendEquation resolves through GLEW.
  static void setMaxBlendEquation(bool enable);

  void deinit();

  static const uint16 INVALID_TEX = 50000;
  
/*
  bool operator< (const ModelRenderPass &m) const
  {
    // Probably not 100% right, but seems to work better than just geoset sorting.
    // Blend mode mostly takes into account transparency and material - Wain
    if (trans == m.trans)
    {
      if (blendmode == m.blendmode)
        return (geoIndex < m.geoIndex);
      return blendmode < m.blendmode;
    }
    return (trans < m.trans);
  }
*/

};


#endif /* _MODELRENDERPASS_H_ */
