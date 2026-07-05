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
 * FBXExporter.cpp
 *
 *  Created on: 13 june 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _FBXEXPORTER_CPP_
#include "FBXExporter.h"
#undef _FBXEXPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

// Qt
#include <qthreadpool.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Externals

// Other libraries
#include "animated.h" // globalTime (WMV_TEST_GLOBALTIME forensic pin)
#include "FBXHeaders.h"
#include "ModelRenderPass.h"
#include "WoWModel.h"
#include "RenderTexture.h"
#include "video.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

#include "util.h" // SLASH

// Current library


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------

// Machine-parseable progress line on stdout, drained by the parent process' monitor (see the
// out-of-process export). fflush so the parent sees each line promptly (redirected stdout is
// block-buffered). Harmless when stdout isn't redirected (in-process / no console).
static void wmvProgress(const std::string & line)
{
  std::printf("WMVEXPORT-PROGRESS: %s\n", line.c_str());
  std::fflush(stdout);
}

// Constructors
//--------------------------------------------------------------------
FBXExporter::FBXExporter():
 m_p_manager(0), m_p_scene(0), m_p_model(0), m_p_meshNode(0)
{
  m_canExportAnimation = true;
}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------

std::wstring FBXExporter::menuLabel() const
{
  return L"FBX...";
}

std::wstring FBXExporter::fileSaveTitle() const
{
  return L"Save FBX file";
}

std::wstring FBXExporter::fileSaveFilter() const
{
  return L"FBX files (*.fbx)|*.fbx";
}

bool FBXExporter::exportModel(Model * model, std::wstring target)
{
  reset();
  m_lastError.clear();

  m_p_model = dynamic_cast<WoWModel *>(model);

  if(!m_p_model)
  {
    m_lastError = L"Internal error: the selected item is not an exportable model.";
    return false;
  }

  // Forensic-only (like WMV_MATDUMP): pin the global-sequence animation clock to a fixed
  // instant for the whole export, so runs are reproducible at a CHOSEN phase of every
  // global-sequence track. Lets a test force the export to happen exactly "mid-blink" of a
  // cyclically animated pass (e.g. the character eye-glow's opacity track) instead of at
  // whatever instant the viewport clock happened to be.
  if (const char * pin = std::getenv("WMV_TEST_GLOBALTIME"))
  {
    globalTime = (size_t)strtoull(pin, nullptr, 10);
    LOG_INFO << "WMV_TEST_GLOBALTIME: globalTime pinned to" << (int)globalTime << "for this export";
  }

  // ---- Resolve the requested content + enforce dependencies -------------------------------
  // Skinning needs a mesh AND a skeleton; animation needs a skeleton. Rather than silently
  // dropping data, auto-enable the prerequisites (and validate the rest below).
  bool wantMesh = m_exportMesh;
  bool wantSkeleton = m_exportSkeleton;
  bool wantSkinning = m_exportSkinning;
  bool wantAnimation = m_exportAnimation && !m_animsToExport.empty();

  if (wantSkinning) { wantMesh = true; wantSkeleton = true; }
  if (wantAnimation) { wantSkeleton = true; }

  // ---- Validation -> actionable errors via lastError() ------------------------------------
  if (!wantMesh && !wantSkeleton)
  {
    m_lastError = L"Nothing was selected to export. Enable at least \"Export Mesh\" or \"Export Skeleton\".";
    return false;
  }
  const bool modelHasSkeleton = (m_p_model->bones.size() >= 1);
  if (wantSkeleton && !modelHasSkeleton)
  {
    m_lastError = L"This model has no skeleton, so Skeleton, Skinning and Animation cannot be exported.\nDisable those options and export the mesh only.";
    return false;
  }
  if (wantAnimation && !m_p_model->animated)
  {
    LOG_INFO << "Model is not animated; no animation clips will be written.";
    wantAnimation = false;
  }

  m_filename = target;
  m_p_manager = FbxManager::Create();
  if (!m_p_manager)
  {
    m_lastError = L"Unable to create the FBX SDK manager.";
    LOG_ERROR << "Unable to create the FBX SDK manager";
    return false;
  }

  FbxIOSettings *ios = FbxIOSettings::Create(m_p_manager, IOSROOT);
  m_p_manager->SetIOSettings(ios);

  // Embed materials/textures only when a mesh is exported; enable the animation channel only
  // when clips will actually be written (this is what makes the takes land in the file).
  ios->SetBoolProp(EXP_FBX_MATERIAL, wantMesh);
  ios->SetBoolProp(EXP_FBX_TEXTURE, wantMesh);
  ios->SetBoolProp(EXP_FBX_EMBEDDED, wantMesh);
  ios->SetBoolProp(EXP_FBX_SHAPE, true);
  ios->SetBoolProp(EXP_FBX_GOBO, true);
  ios->SetBoolProp(EXP_FBX_ANIMATION, wantAnimation);
  ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

  FbxExporter* exporter = 0;

  m_fileVersion = FBX_2014_00_COMPATIBLE;

  if (FBXHeaders::createFBXHeaders(m_fileVersion, QString::fromWCharArray(m_filename.c_str()), m_p_manager, exporter, m_p_scene) == false)
  {
    m_lastError = L"Unable to initialise the FBX exporter (check that the output folder is writable).";
    return false;
  }

  // add some info to exported scene
  FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(m_p_manager,"SceneInfo");
  sceneInfo->mTitle = m_p_model->name().toStdString().c_str();
  sceneInfo->mAuthor = QString::fromStdWString(GLOBALSETTINGS.appName()).toStdString().c_str();
  sceneInfo->mRevision = QString::fromStdWString(GLOBALSETTINGS.appVersion()).toStdString().c_str();
  m_p_scene->SetSceneInfo(sceneInfo);

  // Build one self-contained scene: mesh (+materials) + skeleton + skin + bind/rest pose +
  // every selected animation clip as its own FbxAnimStack (take). This single-file layout is
  // what Blender / 3ds Max / Unity / Unreal expect for a rigged, animated character.
  try
  {
    // Skeleton FIRST: createMeshes() rigidly parents attached items (weapons/helm/shoulders/...)
    // under their attachment bone, so the bone nodes must already exist.
    if (wantSkeleton)
    {
      wmvProgress("STAGE SKELETON");
      createSkeletons();
      LOG_INFO << "Skeleton successfully created";
    }

    if (wantMesh)
    {
      wmvProgress("STAGE MESH");
      createMeshes();
      LOG_INFO << "Meshes successfully created";
      wmvProgress("STAGE MATERIALS");
      createMaterials();
      LOG_INFO << "Materials successfully created";
    }

    if (wantSkinning)
    {
      wmvProgress("STAGE SKIN");
      linkMeshAndSkeleton();
      FBXHeaders::storeBindPose(m_p_scene, m_boneClusters, m_p_meshNode);
      // Attached items are rigid props parented to a bone (see createMeshes), not skinned, so
      // they need no skin cluster / bind-pose entry of their own.
    }

    if (wantSkeleton)
      FBXHeaders::storeRestPose(m_p_scene, m_boneNodes);

    if (wantAnimation)
    {
      createAnimations();
      LOG_INFO << "Animation clips (takes) successfully created";
    }
  }
  catch(const std::exception& ex)
  {
    m_lastError = L"An error occurred while building the FBX scene. See the log for details.";
    LOG_ERROR << "Error during export:" << ex.what();
    return false;
  }

  wmvProgress("STAGE WRITE");
  if(!exporter->Export(m_p_scene))
  {
    m_lastError = L"Unable to write the FBX file (check disk space and that the file is not open elsewhere).";
    LOG_ERROR << "Unable to export FBX scene";
    return false;
  }
  LOG_INFO << "Model successfully created";

  // Material render-state sidecar for the bundled Blender importer add-on.
  writeMaterialSidecar();

  // Delete the intermediate texture files created during export. In BAKE mode the FBX embeds its
  // textures, so the loose PNGs are redundant and cleaned up. In COMPONENT (raw/node) mode the
  // Blender add-on loads the raw per-unit PNGs from disk next to the FBX, so they MUST be kept --
  // deleting them left the importer with no glow/mask textures to reconstruct the effect from.
  if (!std::getenv("WMV_KEEPTEX") && !m_exportComponentRaw)
  {
    for (auto it : m_texturesToExport)
      _wremove((it.first).c_str());
    for (auto it : m_bakedTextures)
      _wremove((it.first).c_str());
  }

  // Optional automated validation: re-open the file we just wrote and confirm it actually
  // contains the mesh / skeleton / skin / selected clips. Off unless WMV_FBX_SELFTEST is set.
  if (std::getenv("WMV_FBX_SELFTEST"))
    selfTest(m_filename, wantMesh, wantSkeleton, wantSkinning);

  LOG_INFO << "FBX scene successfully exported";
  return true;
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------

void FBXExporter::createMeshes()
{
  m_p_meshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene, m_p_model, glm::mat4(1.0f), glm::vec3(0.0f), m_exportComponentRaw);

  FbxNode* root_node = m_p_scene->GetRootNode();
  root_node->AddChild(m_p_meshNode);

  for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
    if (!itemModels.empty())
    {
      for (std::map<POSITION_SLOTS, WoWModel *>::iterator It = itemModels.begin(); It != itemModels.end(); ++It)
      {
        WoWModel * itemModel = It->second;
        LOG_INFO << "Found attached item:" << itemModel->modelname.c_str();

        // Build the item mesh in its OWN local space (identity matrix). We position it by
        // rigidly parenting it to its attachment bone -- NOT by baking a transform into the
        // vertices. (The old code passed the bone matrix to createMesh, which kept only its
        // scale diagonal and dropped the hand's position/rotation, so every weapon/helm/
        // shoulder ended up an un-parented mesh floating at the scene origin.)
        FbxNode* itemMeshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene, itemModel, glm::mat4(1.0f), glm::vec3(0.0f), m_exportComponentRaw);
        m_attachMeshNodes[It->first] = itemMeshNode;

        const int l = m_p_model->attLookup[It->first];
        bool attached = false;
        if (l > -1 && l < (int)m_p_model->atts.size())
        {
          const int attBone = m_p_model->atts[l].bone;
          if (attBone >= 0 && attBone < (int)m_p_model->bones.size() &&
              m_boneNodes.find(attBone) != m_boneNodes.end())
          {
            // Rigid bone attachment. Parent the item under its attachment bone and offset it by
            // the attachment point relative to that bone's pivot. This reproduces WMV's render
            // and the OBJ exporter (attachment world = bone.mat * (vert + pos)): the FBX bone
            // node's bind transform already sits at the bone pivot, so the local offset is
            // (pos - pivot). The item then follows the bone through every animation take.
            const glm::vec3 off = (m_p_model->atts[l].pos - m_p_model->bones[attBone].pivot) * (float)SCALE_FACTOR;
            itemMeshNode->LclTranslation.Set(FbxVector4(off.x, off.y, off.z));
            m_boneNodes[attBone]->AddChild(itemMeshNode);
            attached = true;
          }
        }
        if (!attached)
          root_node->AddChild(itemMeshNode); // no skeleton available (mesh-only export) -> root
      }
    }
  }
}

void FBXExporter::createSkeletons()
{
  FBXHeaders::createSkeleton(m_p_model, m_p_scene, m_p_skeletonNode, m_boneNodes);

  FbxNode* root_node = m_p_scene->GetRootNode();
  root_node->AddChild(m_p_skeletonNode);

  // Attached items (weapons / armour pieces) are NOT given their own separate skeleton. They are
  // rigid props parented directly to the character's attachment bone in createMeshes(). Emitting
  // a second FbxSkeleton root per item (as the old code did) produced dangling, unused skeletons
  // and risked "multiple roots" / duplicate-skeleton warnings in Unreal/Unity.
}

void FBXExporter::linkMeshAndSkeleton()
{
  // create clusters
  for(auto it : m_boneNodes)
  {
    FbxCluster* cluster = FbxCluster::Create(m_p_scene, "");
    m_boneClusters.push_back(cluster);
    cluster->SetLink(it.second);
    cluster->SetLinkMode(FbxCluster::ELinkMode::eNormalize);
  }

  // define control points (per-vertex bone weights, up to 4 influences, byte weights -> 0..1)
  const size_t clusterCount = m_boneClusters.size();
  int i = 0;
  for (auto it : m_p_model->origVertices)
  {
    bool weighted = false;
    for (size_t j = 0; j < 4; j++)
    {
      if (it.weights[j] > 0 && it.bones[j] < clusterCount)
      {
        m_boneClusters[it.bones[j]]->AddControlPointIndex((int)i, static_cast<double>(it.weights[j]) / 255.0);
        weighted = true;
      }
    }
    // A vertex with no usable weight would be left unskinned and collapse to the origin in the
    // DCC ("detached mesh" / "lost weights"). Pin it fully to its first listed bone (or bone 0)
    // so it follows the skeleton like every other vertex.
    if (!weighted && clusterCount > 0)
    {
      const int b = (it.bones[0] < clusterCount) ? it.bones[0] : 0;
      m_boneClusters[b]->AddControlPointIndex((int)i, 1.0);
    }
    i++;
  }

  // set initial matrices
  FbxAMatrix matrix = m_p_meshNode->EvaluateGlobalTransform();
  for(auto it : m_boneClusters)
  {
    it->SetTransformMatrix(matrix);
  }

  // set link matrices
  std::vector<FbxCluster*>::iterator clusterIt = m_boneClusters.begin();
  for(auto it : m_boneNodes)
  {
    matrix = it.second->EvaluateGlobalTransform();
    (*clusterIt)->SetTransformLinkMatrix(matrix);
    ++clusterIt;
  }

  // add cluster to skin
  FbxGeometry* lMeshAttribute = (FbxGeometry*) m_p_meshNode->GetNodeAttribute();
  FbxSkin* skin = FbxSkin::Create(m_p_scene, "");

  for(auto it : m_boneClusters)
    skin->AddCluster(it);

  lMeshAttribute->AddDeformer(skin);
}

void FBXExporter::createAnimations()
{
  if (m_boneNodes.empty())
  {
    LOG_ERROR << "No bone in skeleton, so no animation will be exported";
    return;
  }

  LOG_INFO << "Num animation clips to export:" << m_animsToExport.size();
  wmvProgress("TOTAL_CLIPS " + std::to_string(m_animsToExport.size()));
  int clipNum = 0;

  std::map<int, std::wstring> animsMap = m_p_model->getAnimsMap();
  std::set<std::string> usedNames;

  // Each selected clip becomes its own FbxAnimStack (take) bound to the SAME skeleton already
  // in the scene -- so the mesh, rig and every clip live in ONE file (the standard rigged-
  // character FBX). Take names come from the WoW AnimationData name (Stand/Walk/Run/Attack/
  // Death/...), de-duplicated so same-named variants don't collide; the take name drives the
  // imported action/clip/sequence name in Blender/Max/Unity/Unreal.
  for (int animIdx : m_animsToExport)
  {
    if (animIdx < 0 || animIdx >= (int)m_p_model->anims.size())
    {
      LOG_ERROR << "Skipping out-of-range animation index" << animIdx;
      continue;
    }
    ModelAnimation anim = m_p_model->anims[animIdx];

    QString base = QString::fromStdWString(animsMap[anim.animID]);
    if (base.isEmpty())
      base = QString("Anim_%1").arg(anim.animID);

    QString name = base;
    int dupe = 1;
    while (usedNames.count(name.toStdString()))
      name = QString("%1_%2").arg(base).arg(dupe++);
    usedNames.insert(name.toStdString());
    m_exportedClipNames.push_back(name.toStdString());

    wmvProgress("CLIP " + std::to_string(++clipNum) + " " + std::to_string(m_animsToExport.size()) + " " + name.toStdString());
    FBXHeaders::createAnimation(m_p_model, m_p_scene, name, anim, m_boneNodes);
  }
}

// See the declaration in FBXExporter.h for the full rationale. Must be called immediately after
// pass->init() returns true (with combinerActive set), while its GLSL program/textures/uniforms
// are still the current GL state.
bool FBXExporter::bakeCombinerTexture(WoWModel * model, ModelRenderPass * pass,
                                       std::vector<unsigned char> & outPixelsBGRA, int & outW, int & outH)
{
  const bool dbg = (std::getenv("WMV_MATDUMP") != nullptr);

  if (!pass->combinerActive)
    return false;

  // Env/sphere-map units depend on the real 3D position/normal (via glTexGeni), which this
  // flat UV-space bake can't reproduce.
  for (int u = 0; u < pass->textureCount && u < 4; ++u)
    if (pass->uvSource[u] == 2)
    {
      if (dbg) LOG_INFO << "[matdump] bakeCombinerTexture: bail, unit" << u << "is env/sphere-map";
      return false;
    }

  if (pass->geoIndex < 0 || pass->geoIndex >= (int)model->geosets.size())
  {
    if (dbg) LOG_INFO << "[matdump] bakeCombinerTexture: bail, geoIndex" << pass->geoIndex << "out of range";
    return false;
  }
  ModelGeosetHD * geoset = model->geosets[pass->geoIndex];
  if (geoset->icount == 0)
  {
    if (dbg) LOG_INFO << "[matdump] bakeCombinerTexture: bail, geoset icount==0";
    return false;
  }

  // Output resolution: the largest bound texture unit, so no detail is lost, clamped to a sane
  // maximum (these are small effect textures in practice).
  int w = 0, h = 0;
  for (int u = 0; u < pass->textureCount && u < 4; ++u)
  {
    uint16 tIndex = (u == 0) ? pass->tex : (u == 1) ? pass->tex2 : (u == 2) ? pass->tex3 : pass->tex4;
    GLuint glTex = model->getGLTexture(tIndex);
    if (glTex == ModelRenderPass::INVALID_TEX)
      continue;
    glBindTexture(GL_TEXTURE_2D, glTex);
    GLint tw = 0, th = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    if (dbg) LOG_INFO << "[matdump] bakeCombinerTexture: unit" << u << "tIndex" << tIndex << "glTex" << glTex << "size" << tw << "x" << th;
    if (tw > w) w = tw;
    if (th > h) h = th;
  }
  if (w <= 0 || h <= 0)
  {
    if (dbg) LOG_INFO << "[matdump] bakeCombinerTexture: bail, no valid texture unit found (w/h<=0)";
    return false;
  }
  static const int MAX_BAKE_DIM = 2048;
  if (w > MAX_BAKE_DIM) w = MAX_BAKE_DIM;
  if (h > MAX_BAKE_DIM) h = MAX_BAKE_DIM;

  // SUPERSAMPLE the UV-space rasterization. Glow accents often live on needle-thin
  // geometry (a beam tip mapping a 1-texel-wide sliver of its island): at 1:1 texel
  // scale those slivers cover no pixel centre and simply drop out of the bake. Render
  // at up to 4x the output size and max-downsample -- max, not average, because a
  // glow's envelope must keep its peak (averaging would dim a sliver by its coverage).
  static const int MAX_RENDER_DIM = 4096;
  int ss = 4;
  while (ss > 1 && (w * ss > MAX_RENDER_DIM || h * ss > MAX_RENDER_DIM))
    ss /= 2;
  const int renderW = w * ss;
  const int renderH = h * ss;

  GLint savedViewport[4];
  glGetIntegerv(GL_VIEWPORT, savedViewport);
  glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

  RenderTexture rt;
  rt.Init(renderW, renderH, video.supportFBO);
  rt.BeginRender();

  glViewport(0, 0, rt.nWidth, rt.nHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  const bool maxBlend = (pass->blendmode == 3 || pass->blendmode == 4);
  if (maxBlend)
  {
    // Additive pass: its planes routinely OVERLAP on the same UV island (e.g. several stacked
    // beam quads sampling one accent region). With blending off the last-drawn plane simply
    // REPLACES the earlier ones -- a dark-sampling plane erases a bright/accent fragment
    // underneath; accumulating instead clips coloured accents to white. Per-channel MAX keeps
    // every fragment's brightest value: the accent colours survive at their true hue.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // factors are ignored by the MAX equation
    ModelRenderPass::setMaxBlendEquation(true); // GLEW call -> wow.dll delegate
  }
  else
    glDisable(GL_BLEND); // want the raw combiner output, not blended against the empty FBO

  // pass->init() (already called by the caller) left the combiner program bound with all 4
  // texture units and uniforms set exactly as the live render uses them -- reuse that state as-
  // is. Force white/unlit so gl_Color contributes nothing: the bake is a pure, lighting-
  // independent texture (Blender applies its own lighting on top of it).
  glDisable(GL_LIGHTING);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  // The actual multi-texture draw (glMultiTexCoord2f etc.) is GLEW-extension GL, and GLEW is
  // statically linked per-module: glewInit() only ever runs in games/wow's video.cpp, so THIS
  // module (fbxexporter.dll) has null GLEW function pointers -- calling glMultiTexCoord2f directly
  // here crashes instantly. ModelRenderPass::render()'s combinerActive branch (in wow.dll, where
  // GLEW IS valid) already draws this exact geometry+texcoord loop; bakeUVSpace=true just swaps
  // its final vertex position for the UV0-mapped one, so delegate to it instead of duplicating
  // that loop here. (geoset is otherwise unused in this function now -- kept for the icount==0
  // bail-out check above.)
  //
  // UV-ANIMATED units (a scrolling overlay streak / colour modulator) make a single instant
  // unrepresentative: whatever region the animation happens to be over at bake time is all the
  // bake sees (verified to miss an accent colour entirely). Sweep the animation across several
  // instants and keep the per-pixel MAX -- the glow's time-envelope, which is what the effect
  // reads as when watched (and what a static DCC frame should show).
  // Accumulate at the SUPERSAMPLED size; the max-downsample to (w, h) happens after
  // the sample loop (see the supersampling note at the size computation above).
  // If the FBO implementation adjusted the requested size, drop supersampling and
  // bake 1:1 at whatever we actually got -- correctness over resolution.
  int effRenderW = renderW, effRenderH = renderH, effSs = ss;
  if (rt.nWidth != renderW || rt.nHeight != renderH)
  {
    effRenderW = rt.nWidth;
    effRenderH = rt.nHeight;
    effSs = 1;
  }
  outW = effRenderW / effSs;
  outH = effRenderH / effSs;
  const size_t renderBytes = (size_t)effRenderW * (size_t)effRenderH * 4;
  std::vector<unsigned char> accum(renderBytes);

  // The bake rasterizes in UV0 space, where a unit whose OWN uv range is large relative to the
  // drawn area gets huge texcoord derivatives -- the GPU then samples a deep, averaged-out mip
  // level that on screen (sane derivatives) never shows. Force non-mipmapped sampling on every
  // texture object this pass binds for the duration of the bake. Texture parameters live on the
  // OBJECT (all units see the change), and glBindTexture/glTexParameteri are core GL 1.1, so
  // this is safe to do from this plugin module (no GLEW).
  GLint savedUnit0Binding = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedUnit0Binding);
  std::map<GLuint, GLint> savedMinFilters;
  {
    const uint16 unitIdx[4] = { pass->tex, pass->tex2, pass->tex3, pass->tex4 };
    for (int u = 0; u < pass->textureCount && u < 4; ++u)
    {
      const GLuint glTex = model->getGLTexture(unitIdx[u]);
      if (glTex == ModelRenderPass::INVALID_TEX || savedMinFilters.count(glTex))
        continue;
      glBindTexture(GL_TEXTURE_2D, glTex);
      GLint minFilter = 0;
      glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
      savedMinFilters[glTex] = minFilter;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)savedUnit0Binding);
  }

  // Sample 0 is the REST state (identity scroll matrices) -- what a static render (the Armory,
  // or a viewport whose animation hasn't ticked) shows; artists park the accent-coloured look
  // there. The remaining samples sweep the scroll cycle with a PRIME ms step (a round step
  // aliases against round periods), ~20s span for long cycles.
  const bool uvAnimated = (pass->texanim >= 0) || (pass->texanim2 >= 0);
  const int nSamples = uvAnimated ? 49 : 1;
  const size_t timeStep = 431;
  std::vector<unsigned char> frame;
  if (nSamples > 1)
    frame.resize(renderBytes);

  for (int s = 0; s < nSamples; ++s)
  {
    // Called even for non-animated passes: it also forces IDENTITY texture matrices on units
    // with no animation track, clearing whatever transform an earlier pass leaked (the exporter
    // never calls the deinit() that pops it on the live path). wow.dll delegate (GLEW-safe).
    pass->updateTexAnimMatrices((size_t)(s > 0 ? (s - 1) * timeStep : 0), s == 0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pass->render(false, true);

    if (s == 0)
      glReadPixels(0, 0, effRenderW, effRenderH, GL_BGRA, GL_UNSIGNED_BYTE, accum.data());
    else
    {
      glReadPixels(0, 0, effRenderW, effRenderH, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
      for (size_t p = 0; p < accum.size(); ++p)
        if (frame[p] > accum[p]) accum[p] = frame[p];
    }

    if (dbg && uvAnimated)
    {
      const std::vector<unsigned char> & buf = (s == 0) ? accum : frame;
      size_t lit = 0;
      for (size_t p = 0; p < buf.size(); p += 4)
        if (buf[p] > 10 || buf[p + 1] > 10 || buf[p + 2] > 10) lit++;
      LOG_INFO << "[matdump] bake sample" << s << "t=" << (int)(s * timeStep) << "lit px" << (int)lit;
    }
  }

  if (maxBlend)
    ModelRenderPass::setMaxBlendEquation(false);

  // Restore the min filters changed for the bake (see above) and unit 0's binding.
  for (auto it : savedMinFilters)
  {
    glBindTexture(GL_TEXTURE_2D, it.first);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, it.second);
  }
  glBindTexture(GL_TEXTURE_2D, (GLuint)savedUnit0Binding);

  // Max-downsample the supersampled accumulation to the output size (see the
  // supersampling note above: max keeps thin glows' peaks; average would dim them).
  outPixelsBGRA.assign((size_t)outW * (size_t)outH * 4, 0);
  for (int y = 0; y < effRenderH; ++y)
  {
    const int oy = (y / effSs) < outH ? (y / effSs) : outH - 1;
    for (int x = 0; x < effRenderW; ++x)
    {
      const int ox = (x / effSs) < outW ? (x / effSs) : outW - 1;
      const unsigned char * sp = &accum[((size_t)y * effRenderW + x) * 4];
      unsigned char * dp = &outPixelsBGRA[((size_t)oy * outW + ox) * 4];
      for (int c = 0; c < 4; ++c)
        if (sp[c] > dp[c]) dp[c] = sp[c];
    }
  }

  rt.ReleaseTexture();
  rt.EndRender();
  rt.Shutdown();

  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  glPopAttrib();
  glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);

  return true;
}

// See the declaration in FBXExporter.h. Sidecar format (version 1):
// {
//   "generator": "...", "version": 1,
//   "materials": [ { "name", "texture", "blendMode", "unlit",
//                    "brightnessAlpha", "twoSided", "emissive": [r,g,b] } ]
// }
// The Blender add-on matches Blender's imported materials to "name"
// (tolerating Blender's ".001" de-duplication suffixes) and rebuilds each
// node graph to reproduce the viewport: blend 0/1 -> opaque/alpha-clip,
// 2 -> alpha blend, 3/4 -> emissive additive with the file's alpha driving
// transparency (which for additive passes is per-pixel brightness).
// Extract a texture-animation's UV translation track into a sidecar scroll track. Handles
// global-sequence tracks (period = the sequence's own loop duration, independent of the skeletal
// clip) and plain animation tracks (period = the track's own span). The V axis is negated to match
// the exporter's UV V-flip (exported UVs use 1-v, so a +v scroll becomes -v). Returns false if the
// track does not actually animate (fewer than 2 keys and no net offset).
static bool extractScrollTrack(const TextureAnim & ta, ssize_t modelAnim, FBXScrollTrack & out)
{
  const Animated<glm::vec3> & tr = ta.trans;
  ssize_t anim;
  int periodMs = 0;
  if (tr.seq >= 0 && tr.seq < (ssize_t)tr.globals.size())
  {
    if (tr.globals[tr.seq] == 0) return false; // zero-length global sequence never plays
    anim = 0;                                   // global-sequence tracks read animation slot 0
    periodMs = (int)tr.globals[tr.seq];
  }
  else
  {
    anim = modelAnim;
    if (anim < 0 || anim >= MAX_ANIMATED || tr.times[anim].empty()) return false;
    periodMs = (int)tr.times[anim].back();
  }
  if (anim < 0 || anim >= MAX_ANIMATED || tr.data[anim].empty()) return false;

  out.keys.clear();
  for (size_t k = 0; k < tr.data[anim].size(); k++)
  {
    FBXAnimKey key;
    key.t = (k < tr.times[anim].size()) ? (int)tr.times[anim][k] : 0;
    key.u = tr.data[anim][k].x;
    key.v = -tr.data[anim][k].y;
    out.keys.push_back(key);
  }
  out.periodMs = periodMs;
  out.dx = tr.data[anim].back().x - tr.data[anim].front().x;
  out.dy = -(tr.data[anim].back().y - tr.data[anim].front().y);
  return out.keys.size() >= 2 || out.dx != 0.0f || out.dy != 0.0f;
}

// Classify how a pass's texture alpha channel should be treated in Blender. In WoW an alpha channel
// is only sometimes transparency; on opaque materials it is typically a specular/mask channel that
// must be IGNORED (the tutorial's "set Alpha to None" fix), or it drives additive brightness.
static std::string deriveAlphaUsage(int blend)
{
  switch (blend)
  {
    case 1: return "alpha_clip";                 // alpha-key cutout -> CLIP@0.5
    case 2: case 5: case 6: return "alpha_blend"; // alpha blend / modulate -> BLEND
    case 3: case 4: return "additive";            // additive glow -> emission over transparent
    default: return "opaque";                     // 0/7: ignore alpha (it is a spec/mask, not opacity)
  }
}

std::vector<FBXUnitMeta> FBXExporter::exportRawUnits(WoWModel * model, ModelRenderPass * pass, const std::string & baseName)
{
  std::vector<FBXUnitMeta> units;
  const uint16 unitTex[4] = { pass->tex, pass->tex2, pass->tex3, pass->tex4 };
  int n = pass->textureCount;
  if (n < 1) n = 1; if (n > 4) n = 4;

  const QString dir = QString::fromStdWString(m_filename);
  // Accept either separator: the GUI hands us backslash paths, but a forward-slash path (headless
  // / programmatic export) must still resolve the FBX's folder, not silently fall back to the CWD.
  const QString dirPrefix = dir.left(qMax(dir.lastIndexOf('\\'), dir.lastIndexOf('/')) + 1);
  const bool glow = pass->unlit || pass->blendmode == 3 || pass->blendmode == 4;

  for (int u = 0; u < n; u++)
  {
    FBXUnitMeta um;
    um.uvSource = (int)pass->uvSource[u];
    um.wrapU = pass->swrap; // pass carries unit-0's wrap flags; a good default for the others
    um.wrapV = pass->twrap;

    // role: env (sphere-map, no file) | glow (a glow pass's UV2 scrolling layer) | mask (its UV1
    // gradient) | base. Drives the add-on's per-pass glow node graph (stage 5).
    if (um.uvSource == 2)      um.role = "env";
    else if (glow && um.uvSource == 1) um.role = "glow";
    else if (glow && um.uvSource == 0) um.role = "mask";
    else                       um.role = "base";

    // UV scroll: unit 0 is driven by pass->texanim, unit 1 by pass->texanim2 (the two texture-
    // animation tracks a render pass resolves). Global-sequence timed, so it loops independently
    // of the skeletal animation.
    const int16 texanimIdx = (u == 0) ? pass->texanim : (u == 1 ? pass->texanim2 : (int16)-1);
    const std::vector<TextureAnim> & tas = model->getTexAnims();
    if (texanimIdx >= 0 && texanimIdx < (int16)tas.size())
      um.hasScroll = extractScrollTrack(tas[texanimIdx], (ssize_t)model->anim, um.scroll);

    // Environment units have generated coords, no source texture -> record fileless and continue.
    if (um.uvSource == 2)
    {
      units.push_back(um);
      continue;
    }

    const uint16 texIdx = unitTex[u];
    const GLuint glid = model->getGLTexture(texIdx);
    if (texIdx == ModelRenderPass::INVALID_TEX || glid == ModelRenderPass::INVALID_TEX)
    {
      units.push_back(um); // no texture for this unit; keep it for uvSource/role alignment
      continue;
    }

    const QString fname = QString::fromStdString(baseName) + QString("_unit%1.png").arg(u);
    m_texturesToExport[(dirPrefix + fname).toStdWString()] = glid; // RAW: no tint/additive transform
    um.file = fname.toStdString();
    units.push_back(um);
  }
  return units;
}

void FBXExporter::fillComponentMeta(FBXMaterialMeta & meta, WoWModel * /*model*/, ModelRenderPass * pass, int /*passIndex*/)
{
  meta.shaderId = pass->pixelShader;
  meta.vertexShader = pass->vertexShader;
  meta.textureCount = pass->textureCount;
  meta.noZWrite = pass->noZWrite;
  meta.billboard = pass->billboard;
  meta.isGlow = pass->unlit || pass->blendmode == 3 || pass->blendmode == 4;
  meta.alphaUsage = deriveAlphaUsage(pass->blendmode);
  // meta.units[] (raw per-unit textures) and meta.animatedOpacity are populated in later stages
  // (raw multi-texture export, UV scroll) -- kept in this one helper so both material loops share it.
}

void FBXExporter::writeMaterialSidecar() const
{
  const bool v2 = m_exportComponentRaw;
  QJsonArray materials;
  for (const FBXMaterialMeta & meta : m_materialMeta)
  {
    QJsonObject entry;
    entry["name"] = QString::fromStdString(meta.materialName);
    entry["texture"] = QString::fromStdString(meta.textureFile);
    entry["blendMode"] = meta.blendMode;
    entry["unlit"] = meta.unlit;
    entry["brightnessAlpha"] = meta.brightnessAlpha;
    entry["twoSided"] = meta.twoSided;
    entry["emissive"] = QJsonArray{meta.emissiveR, meta.emissiveG, meta.emissiveB};
    if (v2)
    {
      entry["shaderId"] = meta.shaderId;
      entry["vertexShader"] = meta.vertexShader;
      entry["textureCount"] = meta.textureCount;
      entry["noZWrite"] = meta.noZWrite;
      entry["billboard"] = meta.billboard;
      entry["isGlow"] = meta.isGlow;
      entry["alphaUsage"] = QString::fromStdString(meta.alphaUsage);
      QJsonArray units;
      for (const FBXUnitMeta & u : meta.units)
      {
        QJsonObject uo;
        uo["file"] = QString::fromStdString(u.file);
        uo["uvSource"] = u.uvSource;
        uo["wrapU"] = u.wrapU;
        uo["wrapV"] = u.wrapV;
        uo["role"] = QString::fromStdString(u.role);
        if (u.hasScroll)
        {
          QJsonObject sc;
          sc["period_ms"] = u.scroll.periodMs;
          sc["dx"] = u.scroll.dx;
          sc["dy"] = u.scroll.dy;
          QJsonArray keys;
          for (const FBXAnimKey & k : u.scroll.keys)
            keys.append(QJsonObject{{"t", k.t}, {"u", k.u}, {"v", k.v}});
          sc["keys"] = keys;
          uo["texScroll"] = sc;
        }
        units.append(uo);
      }
      entry["units"] = units;
      if (meta.hasAnimatedOpacity)
      {
        QJsonObject ao;
        ao["period_ms"] = meta.animatedOpacity.periodMs;
        QJsonArray keys;
        for (const FBXAnimKey & k : meta.animatedOpacity.keys)
          keys.append(QJsonObject{{"t", k.t}, {"v", k.v}});
        ao["keys"] = keys;
        entry["animatedOpacity"] = ao;
      }
    }
    materials.append(entry);
  }

  QJsonObject root;
  root["generator"] = "WoW Model Viewer Midnight";
  root["version"] = v2 ? 2 : 1;
  if (v2)
    root["uv2_set"] = "UV2Map";
  root["materials"] = materials;

  const QString path = QString::fromStdWString(m_filename) + ".wmvmat.json";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    LOG_ERROR << "Could not write material sidecar" << path << "- Blender add-on import will fall back to generic materials";
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  LOG_INFO << "Material sidecar written:" << path << "(" << (int)m_materialMeta.size() << "materials )";
}

// Create materials.
void FBXExporter::createMaterials()
{
  if (std::getenv("WMV_MATDUMP"))
    LOG_INFO << "[matdump] main model" << qPrintable(m_p_model->name()) << "total passes"
             << (int)m_p_model->passes.size() << "total geosets" << (int)m_p_model->geosets.size();
  for (unsigned int i = 0; i < m_p_model->passes.size(); i++)
  {
    ModelRenderPass * pass = m_p_model->passes[i];
    // init(true): export-time visibility. A pass whose opacity animation happens to be at 0 at
    // this instant (a blinking eye-glow on a global-sequence track) must still get its material,
    // or the export nondeterministically loses it depending on when the export fires. Must match
    // the gating FBXHeaders::createMesh uses, or per-polygon material indices misalign.
    if (pass->init(true))
    {
      // Forensic per-pass dump (set WMV_MATDUMP=1). Shows the SOURCE render data the viewer uses
      // for each pass, so it can be diffed against the FBX material this loop emits.
      if (std::getenv("WMV_MATDUMP"))
      {
        const int gi = pass->geoIndex;
        const int gid = (gi >= 0 && gi < (int)m_p_model->geosets.size()) ? (int)m_p_model->geosets[gi]->id : -1;
        // pass->ecol / ocol are already set by init() from the color animation track.
        const GLuint glid = m_p_model->getGLTexture(pass->tex);
        LOG_INFO << "[matdump] pass" << (int)i << "geoset" << gid << "(idx" << gi << ")"
                 << " tex=" << qPrintable(m_p_model->getNameForTex(pass->tex)) << "(idx" << (int)pass->tex << ")"
                 << " glId=" << (unsigned)glid << (glid == ModelRenderPass::INVALID_TEX ? "(INVALID)" : "")
                 << " blend=" << (int)pass->blendmode << " unlit=" << (int)pass->unlit
                 << " colorIdx=" << (int)pass->color
                 << " ecol=(" << pass->ecol.r << "," << pass->ecol.g << "," << pass->ecol.b << ")"
                 << " textureCount=" << pass->textureCount << " pixelShader=" << pass->pixelShader
                 << " combinerActive=" << (int)pass->combinerActive
                 << " tex2=" << qPrintable(m_p_model->getNameForTex(pass->tex2)) << "(idx" << (int)pass->tex2 << ")"
                 << " tex3=" << qPrintable(m_p_model->getNameForTex(pass->tex3)) << "(idx" << (int)pass->tex3 << ")"
                 << " tex4=" << qPrintable(m_p_model->getNameForTex(pass->tex4)) << "(idx" << (int)pass->tex4 << ")";
      }

      // Build material name.
      // Material name = model BASENAME + pass index. Full game paths exceed
      // Blender's 63-character name limit, which silently truncates + hashes
      // the name and breaks the sidecar match-up in the importer add-on.
      FbxString mtrl_name = QString(m_p_model->name()).section('/', -1).section('\\', -1)
                              .toStdString().c_str();
      mtrl_name.Append("_", 1);
      char tmp[32];
      _itoa((int)i, tmp, 10);
      mtrl_name.Append(tmp, strlen(tmp));

      // Create material.
      FbxString shading_name = "Phong";
      FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
      material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

      QString tex = m_p_model->getNameForTex(pass->tex);

      QString tex_name = tex.mid(tex.lastIndexOf('/') + 1);
      tex_name = tex_name.replace(".blp", ".png");

      // Composited / dynamically-generated textures (character skin, EYES, hair, baked jewelry)
      // have no source file path, so getNameForTex returns empty -- the texture was then written
      // with no filename and DCCs showed a blank/magenta default (the reported "pink/purple eyes").
      // Give every pass a unique, valid filename derived from the material so its baked GL texture
      // is actually exported and re-loaded. Use only the BASENAME of the model name: the name is a
      // full game path ("character/bloodelf/female/bloodelffemale_hd"), so using it verbatim made a
      // filename with non-existent subdirectories ("character/bloodelf/.../..._105.png") that
      // QImage::save() silently failed to write -- leaving the eye texture empty (0x0 -> magenta).
      if (tex_name.isEmpty() || !tex_name.contains('.'))
        tex_name = QString::fromLatin1(mtrl_name.Buffer()).section('/', -1).section('\\', -1) + ".png";

      // Component / raw node-based mode: export each texture unit as its own untransformed PNG (no
      // combiner bake, no tint/additive rewrite) and drive the FBX diffuse from unit 0's file. The
      // per-unit data goes into the sidecar so the Blender add-on can rebuild the node graph. This
      // makes the bake/tint/additive blocks below no-ops (they are gated on !m_exportComponentRaw).
      std::vector<FBXUnitMeta> componentUnits;
      if (m_exportComponentRaw)
      {
        componentUnits = exportRawUnits(m_p_model, pass, std::string(mtrl_name.Buffer()));
        for (const FBXUnitMeta & um : componentUnits)
          if (!um.file.empty()) { tex_name = QString::fromStdString(um.file); break; }
      }

      // A multi-texture "combiner" pass (War Within+ cosmic/void materials -- see
      // ModelRenderPass::init()/render() and the M2 combiner GLSL shader) blends up to 4 textures
      // per Blizzard's own per-pixel formula (37 known combine modes). The exporter used to connect
      // ONLY pass->tex as the diffuse texture, silently dropping tex2/tex3/tex4 -- e.g. a hood's
      // grey base with a separate additive "beam"/glow overlay texture providing its actual visible
      // colour (a yellow glow) exported as the plain grey base, since the glow layer was never
      // read at all. Bake the real combiner output (every texture unit, exact per-pixel formula,
      // reusing the live GLSL program) into a texture and export THAT instead, when possible.
      bool baked = false;
      std::vector<unsigned char> bakedPixelsBGRA;
      int bakedW = 0, bakedH = 0;
      if (!m_exportComponentRaw && pass->combinerActive)
        baked = bakeCombinerTexture(m_p_model, pass, bakedPixelsBGRA, bakedW, bakedH);
      if (std::getenv("WMV_MATDUMP") && pass->combinerActive)
      {
        LOG_INFO << "[matdump] pass" << (int)i << "bakeCombinerTexture ->" << (int)baked
                 << (baked ? QString(" (%1x%2)").arg(bakedW).arg(bakedH).toStdString().c_str() : "");
        // Forensic-only: dump every raw texture unit standalone (no combiner math), so the SOURCE
        // asset can be inspected directly regardless of whatever the combiner does with it.
        if (std::getenv("WMV_DUMPRAWTEX"))
        {
          uint16 units[4] = { pass->tex, pass->tex2, pass->tex3, pass->tex4 };
          for (int u = 0; u < 4; u++)
          {
            if (units[u] == ModelRenderPass::INVALID_TEX) continue;
            const GLuint glid = m_p_model->getGLTexture(units[u]);
            if (glid == ModelRenderPass::INVALID_TEX) continue;
            QString rawName = QString("rawtex_pass%1_unit%2_%3.png").arg(i).arg(u)
              .arg(QString(m_p_model->getNameForTex(units[u])).section('/', -1).section('\\', -1).replace(".blp",""));
            QString rawFullpath = QString::fromStdWString(m_filename);
            rawFullpath = rawFullpath.left(rawFullpath.lastIndexOf('\\') + 1) + rawName;
            exportGLTexture(glid, rawFullpath.toStdWString());
            LOG_INFO << "[matdump] dumped raw unit" << u << "->" << qPrintable(rawFullpath);
          }
        }
      }

      // A LIT, non-additive pass can still carry a WoW M2 "colors" track recolour (e.g. one shared
      // cloth/armor model recoloured per item -- see ModelRenderPass::init(), which samples
      // model->colors[pass->color] into pass->ecol whenever the track is valid, regardless of
      // unlit/blendmode). That's not a glow -- the emissive branch below only fires for
      // unlit/additive passes -- so without this, the exported texture is the model's raw,
      // un-recoloured base and Blender shows the wrong colour even though the file and material are
      // otherwise correct (this is the "purple armor renders gold/bronze in Blender" bug). pass->
      // ecol is already exactly the colour the viewer multiplies in (pass->init() sampled it above);
      // it is the unconditional (0,0,0,0) default unless that guard passed, so testing it non-zero
      // here is equivalent to re-deriving the guard, without reaching into WoWModel::colors (private,
      // friended only to ModelRenderPass). Give the pass a distinct filename so untinted passes
      // sharing the same base texture are unaffected, and bake the tint into the exported pixels
      // (an FBX material colour factor is not reliably honoured by DCC importers when a diffuse
      // texture is also connected, so the exported file must already BE the tinted result).
      const bool hasColorTint = !m_exportComponentRaw &&
        !(pass->unlit || pass->blendmode == 3 || pass->blendmode == 4) &&
        pass->color != -1 &&
        (pass->ecol.r != 0.0f || pass->ecol.g != 0.0f || pass->ecol.b != 0.0f);
      // Additive pass: the exported file gets the pass's true per-plane framebuffer
      // contribution baked into RGB at save time (premultiply / square per blend mode --
      // see AdditiveTransform) plus brightness-as-alpha. The Blender add-on renders these
      // as pure emission over transparency, and the mesh's own stacked glow planes then
      // accumulate exactly like the game's additive blending -- so no brightness boost is
      // applied anywhere. Distinct filename, since the same source texture may also export
      // unmodified for another pass.
      const bool additive = !m_exportComponentRaw && ((pass->blendmode == 3) || (pass->blendmode == 4));
      if (baked || hasColorTint || additive)
      {
        const int dot = tex_name.lastIndexOf('.');
        QString suffix;
        if (baked) suffix += QString("_baked%1").arg(i);
        if (hasColorTint) suffix += QString("_tinted%1").arg(i);
        if (additive && !baked) suffix += QString("_add%1").arg(i);
        tex_name = tex_name.left(dot) + suffix + tex_name.mid(dot);
      }

      QString tex_fullpath_filename = QString::fromStdWString(m_filename);
      tex_fullpath_filename = tex_fullpath_filename.left(tex_fullpath_filename.lastIndexOf('\\') + 1) + tex_name;

      if (baked)
        m_bakedTextures[tex_fullpath_filename.toStdWString()] = { std::move(bakedPixelsBGRA), bakedW, bakedH };
      else
        m_texturesToExport[tex_fullpath_filename.toStdWString()] = m_p_model->getGLTexture(pass->tex);
      if (hasColorTint)
        m_textureTints[tex_fullpath_filename.toStdWString()] = { pass->ecol.r, pass->ecol.g, pass->ecol.b };
      if (additive)
        m_additiveTextures[tex_fullpath_filename.toStdWString()] = pass->blendmode;

      FbxFileTexture* texture = FbxFileTexture::Create(m_p_manager, tex_name.toStdString().c_str());
      texture->SetFileName(tex_fullpath_filename.toStdString().c_str());
      texture->SetTextureUse(FbxTexture::eStandard);
      texture->SetMappingType(FbxTexture::eUV);
      texture->SetMaterialUse(FbxFileTexture::eModelMaterial);
      texture->SetSwapUV(false);
      texture->SetTranslation(0.0, 0.0);
      texture->SetScale(1.0, 1.0);
      texture->SetRotation(0.0, 0.0);
      texture->UVSet.Set(FbxString("DiffuseUV"));
      material->Diffuse.ConnectSrcObject(texture);

      // Self-illuminated passes (WoW "unlit" flag, additive blend) glow in the viewer -- e.g. the
      // gold emissive eyes. As a plain diffuse material that glow is lost and the eye renders flat
      // (or as the missing-texture default). Drive the emissive channel from the same texture so
      // those passes light up; ecol carries the pass's emissive tint when present.
      if (pass->unlit || pass->blendmode == 3 /*ADD*/ || pass->blendmode == 4 /*ADD_ALPHA*/)
      {
        const double er = pass->ecol.r > 0.0f ? pass->ecol.r : 1.0;
        const double eg = pass->ecol.g > 0.0f ? pass->ecol.g : 1.0;
        const double eb = pass->ecol.b > 0.0f ? pass->ecol.b : 1.0;
        material->Emissive.Set(FbxDouble3(er, eg, eb));
        material->EmissiveFactor.Set(1.0);
        material->Emissive.ConnectSrcObject(texture);
      }

      // Additive passes draw brightness ON TOP of what's behind them in-game -- their dark
      // regions contribute nothing. A DCC import with no transparency renders those planes as
      // opaque black geometry that OCCLUDES the mesh behind (black wings, a shadowed face behind
      // glow planes). Drive transparency from the texture so dark equals see-through: for
      // colour-additive (3) the file's alpha was rewritten to brightness above; for
      // alpha-additive (4) the source alpha already IS the contribution mask.
      if (pass->blendmode == 3 || pass->blendmode == 4)
      {
        material->TransparentColor.ConnectSrcObject(texture);
        material->TransparencyFactor.Set(1.0);
      }

      // Record the viewport render state for the Blender add-on sidecar.
      {
        FBXMaterialMeta meta;
        meta.materialName = mtrl_name.Buffer();
        meta.textureFile = tex_name.toStdString();
        meta.blendMode = pass->blendmode;
        meta.unlit = pass->unlit;
        meta.brightnessAlpha = additive;
        meta.twoSided = !pass->cull;
        meta.emissiveR = pass->ecol.r > 0.0f ? pass->ecol.r : 1.0f;
        meta.emissiveG = pass->ecol.g > 0.0f ? pass->ecol.g : 1.0f;
        meta.emissiveB = pass->ecol.b > 0.0f ? pass->ecol.b : 1.0f;
        if (m_exportComponentRaw)
        {
          fillComponentMeta(meta, m_p_model, pass, (int)i);
          meta.units = std::move(componentUnits);
        }
        m_materialMeta.push_back(std::move(meta));
      }

      // Add material to the scene.
      m_p_meshNode->AddMaterial(material);
    }
    else if (std::getenv("WMV_MATDUMP"))
    {
      const int gi = pass->geoIndex;
      const bool geoValid = (gi >= 0 && gi < (int)m_p_model->geosets.size());
      LOG_INFO << "[matdump] pass" << (int)i << "SKIPPED (init() returned false) geoIndex" << gi
               << "geosetDisplay" << (geoValid ? (int)m_p_model->geosets[gi]->display : -1)
               << "geosetId" << (geoValid ? (int)m_p_model->geosets[gi]->id : -1);
    }
  }

  for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
    if (!itemModels.empty())
    {
      for (std::map<POSITION_SLOTS, WoWModel *>::iterator It = itemModels.begin(); It != itemModels.end(); ++It)
      {
        WoWModel* model = It->second;
        for (unsigned int i = 0; i < model->passes.size(); i++)
        {
          ModelRenderPass * pass = model->passes[i];
          // init(true): export-time visibility -- see the main-model loop above.
          if (pass->init(true))
          {
            if (std::getenv("WMV_MATDUMP"))
            {
              // UV-set-0 bounding box of this pass's geoset: shows WHICH island of the bound
              // texture the geometry actually samples (forensic for replaceable-slot routing).
              float uMin = 1e9f, uMax = -1e9f, vMin = 1e9f, vMax = -1e9f;
              if (pass->geoIndex >= 0 && pass->geoIndex < (int)model->geosets.size())
              {
                ModelGeosetHD * gs = model->geosets[pass->geoIndex];
                for (size_t k = 0, b = gs->istart; k < gs->icount; k++, b++)
                {
                  const glm::vec2 & tc = model->origVertices[model->indices[b]].texcoords;
                  uMin = (std::min)(uMin, tc.x); uMax = (std::max)(uMax, tc.x);
                  vMin = (std::min)(vMin, tc.y); vMax = (std::max)(vMax, tc.y);
                }
              }
              // Forensic-only: per-vertex UV pairs of this pass's geoset (uv set 0 drives unit 0,
              // uv set 1 drives unit 1) -- lets the combiner product be recomputed offline.
              if (std::getenv("WMV_DUMPUV") && pass->geoIndex >= 0 && pass->geoIndex < (int)model->geosets.size())
              {
                ModelGeosetHD * gs = model->geosets[pass->geoIndex];
                std::set<uint32> seen;
                for (size_t k = 0, b = gs->istart; k < gs->icount; k++, b++)
                {
                  const uint32 a = model->indices[b];
                  if (!seen.insert(a).second)
                    continue;
                  const ModelVertex & ov = model->origVertices[a];
                  const float u1f = *reinterpret_cast<const float *>(&ov.unk1);
                  const float v1f = *reinterpret_cast<const float *>(&ov.unk2);
                  LOG_INFO << "[dumpuv] pass" << (int)i << "v" << (int)a
                           << "uv0=(" << ov.texcoords.x << "," << ov.texcoords.y << ")"
                           << "uv1=(" << u1f << "," << v1f << ")";
                }
              }
              LOG_INFO << "[matdump-item] slot" << (int)It->first << " model=" << qPrintable(model->name())
                       << " pass" << (int)i
                       << " tex=" << qPrintable(model->getNameForTex(pass->tex)) << "(idx" << (int)pass->tex << ", glId" << (unsigned)model->getGLTexture(pass->tex) << ")"
                       << " TEX2=" << qPrintable(model->getNameForTex(pass->tex2)) << "(idx" << (int)pass->tex2 << ", glId" << (unsigned)model->getGLTexture(pass->tex2) << ")"
                       << " tex3=" << (int)pass->tex3 << " tex4=" << (int)pass->tex4
                       << " combiner=" << (int)pass->combinerActive
                       << " textureCount=" << pass->textureCount << " pixelShader=" << pass->pixelShader
                       << " uvSource=(" << (int)pass->uvSource[0] << "," << (int)pass->uvSource[1] << ","
                       << (int)pass->uvSource[2] << "," << (int)pass->uvSource[3] << ")"
                       << " geoIndex=" << pass->geoIndex
                       << " geosetDisplay=" << (pass->geoIndex >= 0 && pass->geoIndex < (int)model->geosets.size() ? (int)model->geosets[pass->geoIndex]->display : -1)
                       << " blend=" << (int)pass->blendmode << " unlit=" << (int)pass->unlit
                       << " colorIdx=" << (int)pass->color
                       << " ecol=(" << pass->ecol.r << "," << pass->ecol.g << "," << pass->ecol.b << ")"
                       << " texanim=" << (int)pass->texanim << " texanim2=" << (int)pass->texanim2
                       << " uv0bbox=(" << uMin << "," << vMin << ")-(" << uMax << "," << vMax << ")";
            }

            // Build material name.
            // Basename + pass index -- see the main-model loop for rationale
            // (Blender's 63-char material name limit).
            FbxString mtrl_name = QString(model->name()).section('/', -1).section('\\', -1)
                                    .toStdString().c_str();
            mtrl_name.Append("_", 1);
            char tmp[32];
            _itoa((int)i, tmp, 10);
            mtrl_name.Append(tmp, strlen(tmp));

            // Create material.
            FbxString shading_name = "Phong";
            FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
            material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

            QString tex = model->getNameForTex(pass->tex);

            QString tex_name = tex.mid(tex.lastIndexOf('/') + 1);
            tex_name = tex_name.replace(".blp", ".png");

            // Composited / nameless textures get a unique filename (see main-model loop above),
            // from the BASENAME so the path-shaped model name doesn't create a bad subdir filename.
            if (tex_name.isEmpty() || !tex_name.contains('.'))
              tex_name = QString::fromLatin1(mtrl_name.Buffer()).section('/', -1).section('\\', -1) + ".png";

            // Component / raw node-based mode -- see the main-model loop above. Export each unit as
            // its own untransformed PNG and drive the diffuse from unit 0; skip bake/tint/additive.
            std::vector<FBXUnitMeta> componentUnits;
            if (m_exportComponentRaw)
            {
              componentUnits = exportRawUnits(model, pass, std::string(mtrl_name.Buffer()));
              for (const FBXUnitMeta & um : componentUnits)
                if (!um.file.empty()) { tex_name = QString::fromStdString(um.file); break; }
            }

            // Multi-texture combiner pass -- see the main-model loop above for the full
            // explanation. Attached items (this weapon/helm/etc. loop) carry this exact same
            // mechanism, e.g. a helm's grey base cloth with a separate additive glow-overlay
            // texture (tex2/tex3/tex4) providing its actual visible colour.
            bool baked = false;
            std::vector<unsigned char> bakedPixelsBGRA;
            int bakedW = 0, bakedH = 0;
            if (!m_exportComponentRaw && pass->combinerActive)
              baked = bakeCombinerTexture(model, pass, bakedPixelsBGRA, bakedW, bakedH);

            // Lit, non-additive M2 "colors" track recolour -- see the main-model loop above for the
            // full explanation. Attached items (this weapon/helm/etc. loop) carry this exact same
            // recolour mechanism, e.g. a shared weapon model tinted per item.
            const bool hasColorTint = !m_exportComponentRaw &&
              !(pass->unlit || pass->blendmode == 3 || pass->blendmode == 4) &&
              pass->color != -1 &&
              (pass->ecol.r != 0.0f || pass->ecol.g != 0.0f || pass->ecol.b != 0.0f);
            // Additive pass: contribution-in-RGB rewrite at save time (see main-model loop).
            const bool additive = !m_exportComponentRaw && ((pass->blendmode == 3) || (pass->blendmode == 4));
            if (baked || hasColorTint || additive)
            {
              const int dot = tex_name.lastIndexOf('.');
              QString suffix;
              if (baked) suffix += QString("_baked%1").arg(i);
              if (hasColorTint) suffix += QString("_tinted%1").arg(i);
              if (additive && !baked) suffix += QString("_add%1").arg(i);
              tex_name = tex_name.left(dot) + suffix + tex_name.mid(dot);
            }

            QString tex_fullpath_filename = QString::fromStdWString(m_filename);
            tex_fullpath_filename = tex_fullpath_filename.left(tex_fullpath_filename.lastIndexOf('\\') + 1) + tex_name;

            if (baked)
              m_bakedTextures[tex_fullpath_filename.toStdWString()] = { std::move(bakedPixelsBGRA), bakedW, bakedH };
            else
              m_texturesToExport[tex_fullpath_filename.toStdWString()] = model->getGLTexture(pass->tex);
            if (hasColorTint)
              m_textureTints[tex_fullpath_filename.toStdWString()] = { pass->ecol.r, pass->ecol.g, pass->ecol.b };
            if (additive)
              m_additiveTextures[tex_fullpath_filename.toStdWString()] = pass->blendmode;

            FbxFileTexture* texture = FbxFileTexture::Create(m_p_manager, tex_name.toStdString().c_str());
            texture->SetFileName(tex_fullpath_filename.toStdString().c_str());
            texture->SetTextureUse(FbxTexture::eStandard);
            texture->SetMappingType(FbxTexture::eUV);
            texture->SetMaterialUse(FbxFileTexture::eModelMaterial);
            texture->SetSwapUV(false);
            texture->SetTranslation(0.0, 0.0);
            texture->SetScale(1.0, 1.0);
            texture->SetRotation(0.0, 0.0);
            texture->UVSet.Set(FbxString("DiffuseUV"));
            material->Diffuse.ConnectSrcObject(texture);

            // Self-illuminated / additive passes glow (matches the main-model loop).
            if (pass->unlit || pass->blendmode == 3 || pass->blendmode == 4)
            {
              const double er = pass->ecol.r > 0.0f ? pass->ecol.r : 1.0;
              const double eg = pass->ecol.g > 0.0f ? pass->ecol.g : 1.0;
              const double eb = pass->ecol.b > 0.0f ? pass->ecol.b : 1.0;
              material->Emissive.Set(FbxDouble3(er, eg, eb));
              material->EmissiveFactor.Set(1.0);
              material->Emissive.ConnectSrcObject(texture);
            }

            // Additive passes: dark equals see-through (matches the main-model loop).
            if (pass->blendmode == 3 || pass->blendmode == 4)
            {
              material->TransparentColor.ConnectSrcObject(texture);
              material->TransparencyFactor.Set(1.0);
            }

            // Record the viewport render state for the Blender add-on sidecar.
            {
              FBXMaterialMeta meta;
              meta.materialName = mtrl_name.Buffer();
              meta.textureFile = tex_name.toStdString();
              meta.blendMode = pass->blendmode;
              meta.unlit = pass->unlit;
              meta.brightnessAlpha = additive;
              meta.twoSided = !pass->cull;
              meta.emissiveR = pass->ecol.r > 0.0f ? pass->ecol.r : 1.0f;
              meta.emissiveG = pass->ecol.g > 0.0f ? pass->ecol.g : 1.0f;
              meta.emissiveB = pass->ecol.b > 0.0f ? pass->ecol.b : 1.0f;
              if (m_exportComponentRaw)
              {
                fillComponentMeta(meta, model, pass, (int)i);
                meta.units = std::move(componentUnits);
              }
              m_materialMeta.push_back(std::move(meta));
            }

            // Add material to the scene.
            m_attachMeshNodes[It->first]->AddMaterial(material);
          }
        }
      }
    }
  }

  // Additive files keep their FULL-STRENGTH colours (no premultiply/square): a DCC decodes
  // the PNG from sRGB to linear and tone-maps the render, which dims dim glows far below how
  // the game's gamma-space additive blending shows them -- mathematically exact per-plane
  // contributions were measured to disappear entirely after that pipeline. Full-strength
  // colours with the sRGB decode land closest to the viewport look. (AdditiveTransform stays
  // available in ExporterPlugin for pipelines that want the exact-contribution variant.)
  const auto additiveTransformFor = [](const std::wstring &) -> AdditiveTransform {
    return AdditiveTransform::None;
  };

  for (auto it : m_texturesToExport)
  {
    const AdditiveTransform transform = additiveTransformFor(it.first);
    const bool additive = m_additiveTextures.count(it.first) != 0;
    auto tintIt = m_textureTints.find(it.first);
    if (tintIt != m_textureTints.end())
    {
      const float tintRGB[3] = { tintIt->second.r, tintIt->second.g, tintIt->second.b };
      exportGLTexture(it.second, it.first, tintRGB, additive, transform);
    }
    else
    {
      exportGLTexture(it.second, it.first, nullptr, additive, transform);
    }
  }

  // Combiner-baked textures (see bakeCombinerTexture) already hold their final pixels -- no GL
  // readback needed, just save (with the same optional tint, in case a pass somehow carried both).
  for (auto it : m_bakedTextures)
  {
    auto tintIt = m_textureTints.find(it.first);
    const float * tintRGB = nullptr;
    float tintBuf[3];
    if (tintIt != m_textureTints.end())
    {
      tintBuf[0] = tintIt->second.r; tintBuf[1] = tintIt->second.g; tintBuf[2] = tintIt->second.b;
      tintRGB = tintBuf;
    }
    const AdditiveTransform transform = additiveTransformFor(it.first);
    saveRGBABuffer(it.second.pixelsBGRA.data(), it.second.width, it.second.height, it.first, tintRGB,
                   m_additiveTextures.count(it.first) != 0, transform);
  }
}


void FBXExporter::reset()
{
  if(m_p_manager)
    m_p_manager->Destroy();

  m_p_manager = 0;

  // scene is destroyed by manager's destroy call
  m_p_scene = 0;

  m_p_model = 0;
  m_p_meshNode = 0;
  m_p_skeletonNode = 0;

  m_filename = L"";

  m_boneNodes.clear();
  m_texturesToExport.clear();
  m_textureTints.clear();
  m_bakedTextures.clear();
  m_additiveTextures.clear();
  m_materialMeta.clear();
  m_boneClusters.clear();
  m_exportedClipNames.clear();
}

// Recursively count mesh control points, skeleton bones and skin clusters in a re-imported scene.
namespace
{
  void walkCountNodes(FbxNode* n, int& ctrlPts, int& bones, int& clusters)
  {
    if (!n)
      return;
    if (FbxNodeAttribute* a = n->GetNodeAttribute())
    {
      const FbxNodeAttribute::EType t = a->GetAttributeType();
      if (t == FbxNodeAttribute::eMesh)
      {
        FbxMesh* m = static_cast<FbxMesh*>(a);
        ctrlPts += m->GetControlPointsCount();
        const int nd = m->GetDeformerCount(FbxDeformer::eSkin);
        for (int d = 0; d < nd; d++)
        {
          FbxSkin* s = static_cast<FbxSkin*>(m->GetDeformer(d, FbxDeformer::eSkin));
          if (s)
            clusters += s->GetClusterCount();
        }
      }
      else if (t == FbxNodeAttribute::eSkeleton)
      {
        bones++;
      }
    }
    for (int c = 0; c < n->GetChildCount(); c++)
      walkCountNodes(n->GetChild(c), ctrlPts, bones, clusters);
  }

  // Recursively log per-mesh forensics: name, vertex count, weighted-vertex count, influencing
  // bones, skin cluster count, parent node (+ kind), and the node's bind-pose translation.
  void dumpMeshForensics(FbxNode* n, FbxScene* scene)
  {
    if (!n)
      return;
    FbxNodeAttribute* a = n->GetNodeAttribute();
    if (a && a->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
      FbxMesh* mesh = static_cast<FbxMesh*>(a);
      const int cps = mesh->GetControlPointsCount();

      FbxNode* parent = n->GetParent();
      std::string parentName = "(none)";
      std::string parentKind = "-";
      if (parent)
      {
        parentName = (parent == scene->GetRootNode()) ? "(scene root)" : parent->GetName();
        if (FbxNodeAttribute* pa = parent->GetNodeAttribute())
        {
          switch (pa->GetAttributeType())
          {
          case FbxNodeAttribute::eSkeleton: parentKind = "BONE"; break;
          case FbxNodeAttribute::eMesh:     parentKind = "mesh"; break;
          case FbxNodeAttribute::eNull:     parentKind = "null"; break;
          default:                          parentKind = "other"; break;
          }
        }
        else
          parentKind = (parent == scene->GetRootNode()) ? "root" : "transform";
      }

      const int skinCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
      int clusterTotal = 0;
      std::set<int> weightedCPs;
      std::set<std::string> bones;
      for (int d = 0; d < skinCount; d++)
      {
        FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(d, FbxDeformer::eSkin));
        const int cc = skin->GetClusterCount();
        clusterTotal += cc;
        for (int c = 0; c < cc; c++)
        {
          FbxCluster* cl = skin->GetCluster(c);
          if (cl->GetLink())
            bones.insert(cl->GetLink()->GetName());
          const int* idx = cl->GetControlPointIndices();
          const int ni = cl->GetControlPointIndicesCount();
          for (int k = 0; k < ni; k++)
            weightedCPs.insert(idx[k]);
        }
      }

      std::string bind = "NO bind-pose entry";
      for (int p = 0; p < scene->GetPoseCount(); p++)
      {
        FbxPose* pose = scene->GetPose(p);
        if (!pose->IsBindPose())
          continue;
        const int pi = pose->Find(n);
        if (pi >= 0)
        {
          FbxVector4 t = pose->GetMatrix(pi).GetRow(3);
          bind = "bindT=(" + std::to_string(t[0]) + ", " + std::to_string(t[1]) + ", " + std::to_string(t[2]) + ")";
          break;
        }
      }

      LOG_INFO << "[fbxinspect] MESH '" << n->GetName() << "' verts=" << cps
               << " weightedVerts=" << (int)weightedCPs.size()
               << " skins=" << skinCount << " clusters=" << clusterTotal
               << " influencingBones=" << (int)bones.size()
               << " parent='" << parentName.c_str() << "'(" << parentKind.c_str() << ") " << bind.c_str();

      if (!bones.empty())
      {
        std::string bl;
        int cnt = 0;
        for (const auto & b : bones)
        {
          if (cnt++ >= 16) { bl += "..."; break; }
          bl += b; bl += " ";
        }
        LOG_INFO << "[fbxinspect]    bones:" << bl.c_str();
      }
    }
    for (int c = 0; c < n->GetChildCount(); c++)
      dumpMeshForensics(n->GetChild(c), scene);
  }

  // Dump each material's diffuse-texture filename + emissive colour/factor (forensics for the
  // gold->pink eye). Empty/invalid filename or zero emissive on the eye material confirms the cause.
  void dumpMaterials(FbxNode* n, std::set<std::string>& seen)
  {
    if (!n) return;
    for (int m = 0; m < n->GetMaterialCount(); m++)
    {
      FbxSurfaceMaterial* mat = n->GetMaterial(m);
      if (!mat || seen.count(mat->GetName())) continue;
      seen.insert(mat->GetName());

      FbxDouble3 emis(0,0,0); double ef = 0.0;
      FbxProperty ep = mat->FindProperty("EmissiveColor");
      FbxProperty efp = mat->FindProperty("EmissiveFactor");
      if (ep.IsValid())  emis = ep.Get<FbxDouble3>();
      if (efp.IsValid()) ef = efp.Get<double>();

      std::string texfile = "(NO texture)";
      FbxProperty dp = mat->FindProperty("DiffuseColor");
      if (dp.IsValid())
      {
        for (int t = 0; t < dp.GetSrcObjectCount(); t++)
        {
          FbxObject* o = dp.GetSrcObject(t);
          if (o && strstr(o->GetClassId().GetName(), "Texture"))
          {
            FbxFileTexture* ft = (FbxFileTexture*)o;
            const char* fn = ft->GetFileName();
            const char* rel = ft->GetRelativeFileName();
            texfile = std::string("file='") + (fn ? fn : "") + "' rel='" + (rel ? rel : "") + "'";
            break;
          }
        }
      }
      LOG_INFO << "[fbxinspect] MAT '" << mat->GetName() << "' emissive=(" << emis[0] << "," << emis[1]
               << "," << emis[2] << ") emFactor=" << ef << " diffuse=" << texfile.c_str();
    }
    for (int c = 0; c < n->GetChildCount(); c++)
      dumpMaterials(n->GetChild(c), seen);
  }

  // Scan one anim layer's LclRotation curves per bone: report any bone whose euler range exceeds
  // 350deg on an axis (a spiral) or has duplicate-time keys -- the unroll/duplicate-key symptoms.
  void dumpNodeRotCurves(FbxNode* n, FbxAnimLayer* layer, int& flagged, int& animated)
  {
    if (!n) return;
    FbxNodeAttribute* a = n->GetNodeAttribute();
    if (a && a->GetAttributeType() == FbxNodeAttribute::eSkeleton)
    {
      FbxAnimCurve* cc[3] = {
        n->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X),
        n->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y),
        n->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z) };
      if (cc[0] || cc[1] || cc[2])
      {
        animated++;
        const char* ax[3] = {"X","Y","Z"};
        std::string s; bool spiral = false; int dup = 0;
        for (int i = 0; i < 3; i++)
        {
          if (!cc[i]) { s += std::string(ax[i]) + "=(none) "; continue; }
          const int kc = cc[i]->KeyGetCount();
          float mn = 1e9f, mx = -1e9f; FbxTime prev; bool first = true;
          for (int k = 0; k < kc; k++)
          {
            const float v = cc[i]->KeyGetValue(k);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            const FbxTime kt = cc[i]->KeyGetTime(k);
            if (!first && kt == prev) dup++;
            prev = kt; first = false;
          }
          s += std::string(ax[i]) + "=[" + std::to_string((int)mn) + "," + std::to_string((int)mx) + "](n" + std::to_string(kc) + ") ";
          if ((mx - mn) > 350.0f) spiral = true;
        }
        if (spiral || dup > 0)
        {
          flagged++;
          LOG_INFO << "[fbxinspect] ROT " << (spiral ? "SPIRAL" : "dupKey") << " '" << n->GetName()
                   << "' " << s.c_str() << "dupTimes=" << dup;
        }
      }
    }
    for (int c = 0; c < n->GetChildCount(); c++)
      dumpNodeRotCurves(n->GetChild(c), layer, flagged, animated);
  }

  void dumpRotationCurves(FbxScene* scene)
  {
    // Find the first anim stack + its first layer without the templated GetSrcObject<FbxAnimStack>
    // (un-exported ClassId static in this SDK build). GetClassId() (member fn) links fine.
    FbxAnimStack* stack = nullptr;
    for (int i = 0; i < scene->GetSrcObjectCount(); i++)
    {
      FbxObject* o = scene->GetSrcObject(i);
      if (o && strcmp(o->GetClassId().GetName(), "FbxAnimStack") == 0) { stack = (FbxAnimStack*)o; break; }
    }
    FbxAnimLayer* layer = nullptr;
    if (stack)
      for (int m = 0; m < stack->GetMemberCount(); m++)
      {
        FbxObject* o = stack->GetMember(m);
        if (o && strcmp(o->GetClassId().GetName(), "FbxAnimLayer") == 0) { layer = (FbxAnimLayer*)o; break; }
      }
    if (!stack || !layer) { LOG_ERROR << "[fbxinspect] no anim stack/layer to scan"; return; }

    LOG_INFO << "[fbxinspect] --- rotation-curve scan, take '" << stack->GetName() << "' (flagging euler range>350deg or dup key times) ---";
    int flagged = 0, animated = 0;
    dumpNodeRotCurves(scene->GetRootNode(), layer, flagged, animated);
    LOG_INFO << "[fbxinspect] rotation scan: " << flagged << " flagged of " << animated << " animated bones";
  }

  // Replicates Bone::calcMatrix (which wow.dll does not export) so the plugin can compute the
  // SOURCE world matrix of a bone at (anim, time): m = T(pivot)*T(animTrans)*R*S*T(-pivot),
  // world = parent.world * m. Memoised per call. (Billboard is skipped -- not used by skeleton bones.)
  glm::mat4 srcBoneWorld(WoWModel* mdl, int b, ssize_t anim, size_t time,
                         std::vector<glm::mat4>& cache, std::vector<char>& done)
  {
    if (b < 0 || b >= (int)mdl->bones.size()) return glm::mat4(1.0f);
    if (done[b]) return cache[b];
    done[b] = 1;
    Bone& bone = mdl->bones[b];
    glm::mat4 mm(1.0f);
    const bool tr = bone.rot.uses(anim) || bone.scale.uses(anim) || bone.trans.uses(anim);
    if (tr)
    {
      mm = glm::translate(mm, bone.pivot);
      if (bone.trans.uses(anim)) mm = glm::translate(mm, bone.trans.getValue(anim, time));
      if (bone.rot.uses(anim))   mm = mm * glm::toMat4(bone.rot.getValue(anim, time));
      if (bone.scale.uses(anim)) mm = glm::scale(mm, bone.scale.getValue(anim, time));
      mm = glm::translate(mm, bone.pivot * -1.0f);
    }
    cache[b] = (bone.parent > -1) ? srcBoneWorld(mdl, bone.parent, anim, time, cache, done) * mm : mm;
    return cache[b];
  }
}

void FBXExporter::selfTest(const std::wstring & file, bool expectMesh, bool expectSkeleton, bool expectSkinning) const
{
  LOG_INFO << "[FBX self-test] re-importing" << qPrintable(QString::fromStdWString(file));

  FbxManager* mgr = FbxManager::Create();
  if (!mgr)
  {
    LOG_ERROR << "[FBX self-test] FAIL: cannot create FBX manager";
    return;
  }
  FbxIOSettings* ios = FbxIOSettings::Create(mgr, IOSROOT);
  mgr->SetIOSettings(ios);

  FbxImporter* imp = FbxImporter::Create(mgr, "");
  FbxScene* scene = FbxScene::Create(mgr, "selftest");

  bool ok = imp->Initialize(QString::fromStdWString(file).toStdString().c_str(), -1, ios);
  if (ok)
    ok = imp->Import(scene);

  if (!ok)
  {
    LOG_ERROR << "[FBX self-test] FAIL: re-import failed:" << imp->GetStatus().GetErrorString();
    mgr->Destroy();
    return;
  }

  int ctrlPts = 0, bones = 0, clusters = 0;
  walkCountNodes(scene->GetRootNode(), ctrlPts, bones, clusters);

  // Read the animation takes from the IMPORTER (file header) rather than via the templated
  // scene query GetSrcObject<FbxAnimStack>(), which links against the un-exported
  // FbxAnimStack::ClassId static in this SDK build.
  const int stacks = imp->GetAnimStackCount();
  std::set<std::string> stackNames;
  for (int i = 0; i < stacks; i++)
  {
    FbxTakeInfo* ti = imp->GetTakeInfo(i);
    if (ti)
      stackNames.insert(ti->mName.Buffer());
  }

  bool allPass = true;
  auto report = [&allPass](const char* label, bool pass)
  {
    if (pass) { LOG_INFO << "[FBX self-test] PASS:" << label; }
    else      { LOG_ERROR << "[FBX self-test] FAIL:" << label; allPass = false; }
  };

  report("re-import succeeds", true);
  report("mesh control points present", !expectMesh || ctrlPts > 0);
  report("skeleton bones present", !expectSkeleton || bones > 0);
  report("skin clusters present", !expectSkinning || clusters > 0);
  report("anim stack count == selected clip count", stacks == (int)m_exportedClipNames.size());
  for (const auto & nm : m_exportedClipNames)
    report((std::string("selected clip present: ") + nm).c_str(), stackNames.count(nm) > 0);

  LOG_INFO << "[FBX self-test] summary:" << (allPass ? "ALL PASS" : "FAILURES ABOVE")
           << "| ctrlPts=" << ctrlPts << "bones=" << bones << "clusters=" << clusters
           << "stacks=" << stacks << "expectedClips=" << (int)m_exportedClipNames.size();

  mgr->Destroy();
}

void FBXExporter::dumpForensics(const std::wstring & file) const
{
  LOG_INFO << "[fbxinspect] === forensic dump:" << qPrintable(QString::fromStdWString(file)) << "===";

  FbxManager* mgr = FbxManager::Create();
  if (!mgr)
  {
    LOG_ERROR << "[fbxinspect] cannot create FBX manager";
    return;
  }
  FbxIOSettings* ios = FbxIOSettings::Create(mgr, IOSROOT);
  mgr->SetIOSettings(ios);
  FbxImporter* imp = FbxImporter::Create(mgr, "");
  FbxScene* scene = FbxScene::Create(mgr, "inspect");

  bool ok = imp->Initialize(QString::fromStdWString(file).toStdString().c_str(), -1, ios);
  if (ok)
    ok = imp->Import(scene);
  if (!ok)
  {
    LOG_ERROR << "[fbxinspect] cannot import file:" << imp->GetStatus().GetErrorString();
    mgr->Destroy();
    return;
  }

  FbxGlobalSettings& gs = scene->GetGlobalSettings();
  LOG_INFO << "[fbxinspect] unit scale-to-cm =" << gs.GetSystemUnit().GetScaleFactor();
  int upSign = 0;
  const int up = gs.GetAxisSystem().GetUpVector(upSign);
  LOG_INFO << "[fbxinspect] up-axis =" << up << "(sign " << upSign << ") [1=X 2=Y 3=Z]";

  const int stacks = imp->GetAnimStackCount();
  LOG_INFO << "[fbxinspect] animation takes =" << stacks;
  for (int i = 0; i < stacks; i++)
  {
    FbxTakeInfo* ti = imp->GetTakeInfo(i);
    if (ti)
      LOG_INFO << "[fbxinspect]    take:" << ti->mName.Buffer();
  }

  int binds = 0;
  for (int p = 0; p < scene->GetPoseCount(); p++)
    if (scene->GetPose(p)->IsBindPose())
      binds++;
  LOG_INFO << "[fbxinspect] poses =" << scene->GetPoseCount() << "(bind poses =" << binds << ")";

  LOG_INFO << "[fbxinspect] --- per-mesh ---";
  dumpMeshForensics(scene->GetRootNode(), scene);

  // (materials + rotation scan appended below)

  LOG_INFO << "[fbxinspect] --- materials ---";
  { std::set<std::string> seenMat; dumpMaterials(scene->GetRootNode(), seenMat); }

  dumpRotationCurves(scene);

  LOG_INFO << "[fbxinspect] === end ===";

  mgr->Destroy();
}

void FBXExporter::dumpSourcePose(Model * model, const std::wstring & animName) const
{
  WoWModel * m = dynamic_cast<WoWModel *>(model);
  if (!m || m->bones.empty())
  {
    LOG_ERROR << "[animdump] no model/bones";
    return;
  }

  // Resolve the animation by name.
  std::map<int, std::wstring> am = m->getAnimsMap();
  const QString want = QString::fromStdWString(animName);
  int ai = -1;
  for (size_t i = 0; i < m->anims.size(); i++)
  {
    if (QString::fromStdWString(am[m->anims[i].animID]).compare(want, Qt::CaseInsensitive) == 0) { ai = (int)i; break; }
  }
  if (ai < 0)
  {
    LOG_ERROR << "[animdump] animation not found: " << qPrintable(want);
    return;
  }
  ModelAnimation anim = m->anims[ai];
  LOG_INFO << "[animdump] anim '" << qPrintable(want) << "' arrayIdx=" << ai << " trackIdx=" << anim.Index << " length=" << anim.length << "ms";

  // Build a throwaway scene with ONLY the skeleton + this one animation, using the REAL export
  // code (createSkeleton/createAnimation -- unroll filter and all). Then we evaluate the exported
  // bone nodes at the same times as the source pose and diff world positions.
  FbxManager * mgr = FbxManager::Create();
  if (!mgr) { LOG_ERROR << "[animdump] no manager"; return; }
  FbxIOSettings * ios = FbxIOSettings::Create(mgr, IOSROOT);
  mgr->SetIOSettings(ios);
  FbxScene * scene = FbxScene::Create(mgr, "animdump");

  FbxNode * skelRoot = nullptr;
  std::map<int, FbxNode*> boneNodes;
  FBXHeaders::createSkeleton(m, scene, skelRoot, boneNodes);
  scene->GetRootNode()->AddChild(skelRoot);
  FBXHeaders::createAnimation(m, scene, want, anim, boneNodes);

  FbxAnimStack * stack = nullptr;
  for (int i = 0; i < scene->GetSrcObjectCount(); i++)
  {
    FbxObject * o = scene->GetSrcObject(i);
    if (o && strcmp(o->GetClassId().GetName(), "FbxAnimStack") == 0) { stack = (FbxAnimStack*)o; break; }
  }
  if (stack)
    scene->SetCurrentAnimationStack(stack);

  const uint32 frames[3] = { 0u, anim.length / 2u, anim.length };
  for (int fi = 0; fi < 3; fi++)
  {
    const uint32 t = frames[fi];

    // SOURCE pose: replicate Bone::calcMatrix for all bones at this time (wow.dll doesn't export it).
    std::vector<glm::mat4> world(m->bones.size());
    std::vector<char> wdone(m->bones.size(), 0);
    for (size_t b = 0; b < m->bones.size(); b++)
      srcBoneWorld(m, (int)b, anim.Index, (size_t)t, world, wdone);

    FbxTime ft; ft.SetSecondDouble((double)t / 1000.0);

    double maxDiff = 0.0; int worst = -1;
    double srcWorst[3] = {0,0,0}, expWorst[3] = {0,0,0};
    for (auto & it : boneNodes)
    {
      const int b = it.first;
      // source posed pivot (world * pivot) in export units
      const glm::vec3 sw = glm::vec3(world[b] * glm::vec4(m->bones[b].pivot, 1.0f)) * (float)SCALE_FACTOR;
      // exported animated world translation
      FbxAMatrix g = it.second->EvaluateGlobalTransform(ft);
      FbxVector4 ew = g.GetT();
      const double dx = sw.x - ew[0], dy = sw.y - ew[1], dz = sw.z - ew[2];
      const double diff = std::sqrt(dx*dx + dy*dy + dz*dz);
      if (diff > maxDiff) { maxDiff = diff; worst = b; srcWorst[0]=sw.x;srcWorst[1]=sw.y;srcWorst[2]=sw.z; expWorst[0]=ew[0];expWorst[1]=ew[1];expWorst[2]=ew[2]; }
      if (b < 3 || diff > 1.0)
        LOG_INFO << "[animdump] f" << t << " b" << b << " src=(" << sw.x << "," << sw.y << "," << sw.z
                 << ") exp=(" << ew[0] << "," << ew[1] << "," << ew[2] << ") diff=" << diff;
    }
    LOG_INFO << "[animdump] frame t=" << t << "ms : MAX world diff=" << maxDiff << " at bone " << worst
             << " src=(" << srcWorst[0] << "," << srcWorst[1] << "," << srcWorst[2] << ") exp=("
             << expWorst[0] << "," << expWorst[1] << "," << expWorst[2] << ")";
  }

  mgr->Destroy();
}
