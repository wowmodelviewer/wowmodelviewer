/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * FBXExporter.h
 *
 *  Created on: 13 june 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _FBXEXPORTER_H_
#define _FBXEXPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <map>
#include <set>
#include <string>
#include <vector>

// Qt
#include <QtPlugin>
#include <qlist.h>
#include <qmutex.h>

// Externals
#include "fbxsdk.h"

// Other libraries
class WoWModel;
class ModelRenderPass;
struct ModelAnimation;

#define _EXPORTERPLUGIN_CPP_ // to define interface
#include "ExporterPlugin.h"
#undef _EXPORTERPLUGIN_CPP_

// Current library


// Namespaces used
//--------------------------------------------------------------------


// RGB colour multiplier baked into an exported texture at write time (see
// FBXExporter::createMaterials()). Keyed by the same filename used in m_texturesToExport.
struct FBXTextureTint { float r, g, b; };

// One keyframe of an M2 animated track, sampled in milliseconds. Used for UV scroll (u,v) and
// for animated pass opacity (v only). Times are absolute ms within the track's cycle.
struct FBXAnimKey { int t = 0; float u = 0.0f; float v = 0.0f; };

// A per-texture-unit UV-scroll track, reconstructed from the M2 texanim translation curve. dx/dy
// is the net offset over one period (a consumer can drive a constant linear velocity instead of
// keyframes). period_ms is the global-sequence cycle length (or the track's own span).
struct FBXScrollTrack { int periodMs = 0; float dx = 0.0f; float dy = 0.0f; std::vector<FBXAnimKey> keys; };

// One texture unit of a component/raw-mode material. file is the exported PNG basename ("" for an
// environment unit, which has no source file). uvSource: 0=UV1(T1),1=UV2(T2),2=Env,3=T3. role is
// a coarse classification the add-on uses to build the glow node graph: base|mask|glow|env.
struct FBXUnitMeta {
  std::string file;
  int uvSource = 0;
  bool wrapU = true, wrapV = true;
  std::string role = "base";
  bool hasScroll = false;
  FBXScrollTrack scroll;
};

// Per-material render-state metadata mirroring the viewport pass that produced it.
// Written as a "<export>.wmvmat.json" sidecar next to the FBX so the bundled Blender
// importer add-on can rebuild each material's node graph to match the viewport exactly
// (blend mode, unlit/emissive, additive transparency, backface culling) -- none of which
// survives a generic FBX round-trip. The trailing fields are populated only in component /
// raw node-based export (sidecar version 2); bake mode leaves them at defaults and writes v1.
struct FBXMaterialMeta {
  std::string materialName;   // FBX material name (add-on matches, tolerating .00N suffixes)
  std::string textureFile;    // basename of the exported/embedded texture
  int blendMode = 0;          // raw M2 blend mode 0..7
  bool unlit = false;
  bool brightnessAlpha = false; // texture alpha was rewritten to brightness (additive glow)
  bool twoSided = false;
  float emissiveR = 1.0f, emissiveG = 1.0f, emissiveB = 1.0f; // tint on emissive passes

  // --- component / raw node-based mode (sidecar v2) ---
  int shaderId = -1;          // pass->pixelShader (0..36, -1 unresolved)
  int vertexShader = -1;      // pass->vertexShader
  int textureCount = 1;       // op_count 1..4
  bool noZWrite = false;
  bool billboard = false;
  bool isGlow = false;        // unlit || additive -> add-on builds an Emission material
  std::string alphaUsage = "opaque"; // opaque|specular_ignore|alpha_clip|alpha_blend|additive
  std::vector<FBXUnitMeta> units;    // per-texture-unit raw data (empty in bake mode)
  bool hasAnimatedOpacity = false;
  FBXScrollTrack animatedOpacity;    // keys use .v (opacity 0..1); .u unused
};

// A texture computed on the fly (not read back from a single live GL texture) -- see
// FBXExporter::bakeCombinerTexture(). pixelsBGRA is width*height*4 bytes, GL_BGRA_EXT byte order.
struct FBXBakedTexture { std::vector<unsigned char> pixelsBGRA; int width; int height; };

// Class Declaration
//--------------------------------------------------------------------
class FBXExporter : public ExporterPlugin
{
    Q_INTERFACES(ExporterPlugin)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "wowmodelviewer.exporters.FBXExporter" FILE "fbxexporter.json")

  public :
    // Constants / Enums

    // Constructors
    FBXExporter();

    // Destructors
    ~FBXExporter() {}

    // Methods
   std::wstring menuLabel() const;

   std::wstring fileSaveTitle() const;
   std::wstring fileSaveFilter() const;

   bool exportModel(Model *, std::wstring file);

   // Read-only forensic dump of an existing FBX (per-mesh verts/weights/clusters/bones/parent/
   // bind-pose + scene unit/axis/takes) to the log. See ExporterPlugin::dumpForensics.
   void dumpForensics(const std::wstring & file) const override;

   // Source-vs-exported per-bone world-pose comparison for one animation. See ExporterPlugin.
   void dumpSourcePose(Model * model, const std::wstring & animName) const override;

    // Members

  protected :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods

    // Members

  private :
    // Constants / Enums

    // Constructors

    // Destructors
    
    // Methods
    void createMaterials();
    void createMeshes();
    void createSkeletons();
    void linkMeshAndSkeleton();
    void createAnimations();
    void reset();

    // Bakes a multi-texture-unit M2 combiner pass (see ModelRenderPass::init()/render(), the
    // "War Within+ cosmic/void materials" GLSL combiner) into a single flat RGBA texture, by
    // rendering the SAME geometry into an offscreen FBO with vertex positions remapped into the
    // pass's own texture-unit-0 UV space (so rasterization happens per-texel) while feeding the
    // REAL per-vertex UV1/UV2 data to the other texture units -- reusing the exact GLSL program,
    // bound textures and uniforms pass->init() already set up, so every pixel_shader combine mode
    // is reproduced exactly with no per-mode reimplementation in C++. Lighting is excluded from
    // the bake (forced white, unlit) so the result is a pure diffuse/emissive texture Blender can
    // re-light normally -- it is NOT a snapshot of how the pass looks under the viewport's current
    // lights. Returns false (does not touch outPixelsBGRA) if any active unit uses environment/
    // sphere-map UVs (uvSource==2), since those depend on the real 3D position/normal and can't be
    // reproduced from a flat UV-space render; the caller falls back to the existing single-texture
    // export for that pass. Must be called immediately after pass->init() returns true, before any
    // other pass's init() rebinds the combiner's textures/uniforms.
    bool bakeCombinerTexture(WoWModel * model, ModelRenderPass * pass,
                             std::vector<unsigned char> & outPixelsBGRA, int & outW, int & outH);

    // Populates the component / raw-mode (sidecar v2) fields of a material's metadata from its
    // render pass: shader ids, texture-unit count, extra render flags, glow classification,
    // alphaUsage, and (stage 3) the per-unit raw texture data (registering each raw texture for
    // export). Called from BOTH material loops (main model + attached items) when component/raw
    // export is on, so the v2 data can never drift between them. passIndex disambiguates filenames.
    void fillComponentMeta(FBXMaterialMeta & meta, WoWModel * model, ModelRenderPass * pass, int passIndex);

    // Component/raw mode: export each of the pass's texture units (tex/tex2/tex3/tex4) as its OWN
    // untransformed PNG (no combiner bake, no tint/additive rewrite), skipping environment units
    // (uvSource==2, which have no source file). Registers each real unit in m_texturesToExport and
    // returns per-unit metadata (filename, uvSource, wrap, role) for meta.units. baseName is the
    // material basename; unit files are "<baseName>_unit<N>.png".
    std::vector<FBXUnitMeta> exportRawUnits(WoWModel * model, ModelRenderPass * pass, const std::string & baseName);

    // Writes "<fbx path>.wmvmat.json" describing every material's viewport render state
    // (from m_materialMeta) for the bundled Blender importer add-on. A failure to write is
    // logged but never fails the export -- the FBX itself is complete without it. Writes schema
    // version 2 (with per-unit raw data) in component mode, version 1 otherwise.
    void writeMaterialSidecar() const;

    // Re-imports the just-written FBX with the SDK and logs PASS/FAIL for each expected piece
    // (mesh control points, skeleton bones, skin clusters, one anim stack per selected clip,
    // and that each selected clip name is present). The project has no unit-test target, so this
    // is the automated "does the FBX reimport correctly and contain what was selected" check.
    // Runs only when the WMV_FBX_SELFTEST environment variable is set.
    void selfTest(const std::wstring & file, bool expectMesh, bool expectSkeleton, bool expectSkinning) const;


    // Members
    FbxManager        * m_p_manager;
    FbxScene          * m_p_scene;
    WoWModel          * m_p_model;
    FbxNode           * m_p_meshNode;
    FbxNode           * m_p_skeletonNode;
    QList<WoWModel*>    m_p_attachedModels;

    mutable QMutex m_mutex;
    bool useAltAnimNaming = false;
    FbxString m_fileVersion;
    std::wstring m_filename;
    std::map<int,FbxNode*> m_boneNodes;
    std::vector<FbxCluster*> m_boneClusters;

    // Names of the animation takes actually written this export (after de-duplication), used by
    // selfTest() to confirm exactly the selected clips made it into the file.
    std::vector<std::string> m_exportedClipNames;

    std::map<int, FbxNode*> m_attachSkeletonNode;
    std::map<int, FbxNode*> m_attachMeshNodes;
    std::map<int, std::map<int, FbxNode*>> m_attachBoneNodes;
    std::map<int, std::vector<FbxCluster*>> m_attachBoneClusters;

    std::map<std::wstring, GLuint> m_texturesToExport;
    // Present only for filenames that need a runtime colour tint baked in at export time.
    std::map<std::wstring, FBXTextureTint> m_textureTints;
    // Present for filenames whose pixels were computed via bakeCombinerTexture() rather than read
    // back from a single live GL texture; exported instead of / in addition to m_texturesToExport
    // for that filename (a tint in m_textureTints still applies on top).
    std::map<std::wstring, FBXBakedTexture> m_bakedTextures;
    // Filenames belonging to ADDITIVE passes, mapped to the pass's raw blend mode (3 or 4).
    // At save time these get their per-plane framebuffer contribution baked into RGB
    // (AdditiveTransform: premultiply for 4, square for 3) and brightness written as alpha
    // (see saveRGBABuffer) -- the Blender add-on then renders them as pure emission over
    // transparency and lets the mesh's own stacked planes accumulate like the game does.
    std::map<std::wstring, int> m_additiveTextures;

    // One entry per created material; serialized as the .wmvmat.json sidecar (see
    // FBXMaterialMeta above and writeMaterialSidecar()).
    std::vector<FBXMaterialMeta> m_materialMeta;

    // friend class declarations

};

// static members definition
#ifdef _FBXEXPORTER_CPP_

#endif

#endif /* _FBXEXPORTER_H_ */
