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
 * FBXHeaders.cpp
 *
 *  Created on: 14 may 2019
 *   Copyright: 2019 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _FBXHEADERS_CPP_
#include "FBXHeaders.h"
#undef _FBXHEADERS_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <qthreadpool.h>

// Externals
#include "fbxsdk.h"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/transform.hpp"


// Other libraries
#include "FBXAnimExporter.h"
#include "ModelRenderPass.h"
#include "WoWModel.h"

#include "util.h" // SLASH

// Current library


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------

// Functions
//--------------------------------------------------------------------

bool FBXHeaders::createFBXHeaders(FbxString fileVersion, QString l_FileName, FbxManager* &l_Manager, FbxExporter* &l_Exporter, FbxScene* &l_Scene)
{
  //LOG_INFO << "Building FBX Header...";

  // Create an exporter.
  l_Exporter = 0;
  l_Exporter = FbxExporter::Create(l_Manager, "");

  if (!l_Exporter->Initialize(l_FileName.toStdString().c_str(), -1, l_Manager->GetIOSettings()))
  {
    LOG_ERROR << "Unable to create the FBX SDK exporter";
    return false;
  }
  //LOG_INFO << "FBX SDK exporter successfully created";

  // make file compatible with older fbx versions
  l_Exporter->SetFileExportVersion(fileVersion);

  l_Scene = FbxScene::Create(l_Manager, "My Scene");
  if (!l_Scene)
  {
    LOG_ERROR << "Unable to create FBX scene";
    return false;
  }

  // WoW is a Z-up, right-handed world, so declare the scene Z-up. The scene is still empty
  // here, so this only writes axis metadata (no data is transformed); importers (Blender/Max
  // are Z-up, Unity/Unreal convert from the declared axis) use it to orient the model. Done
  // before any geometry is added so nothing gets double-transformed.
  FbxAxisSystem conv(FbxAxisSystem::eMayaZUp);
  conv.ConvertScene(l_Scene);

  // Declare the unit explicitly. The geometry/skeleton are written pre-scaled by SCALE_FACTOR
  // and the FBX default unit is centimetres; stating it removes the "undeclared unit" guess
  // that makes some importers silently apply a 100x scale. Unreal is natively cm (1:1); Unity
  // and Blender convert from this declared unit on import.
  // 1.0 == centimetres. Constructed explicitly rather than via the FbxSystemUnit::cm static,
  // which this FBX SDK build does not export (link error). Sets metadata only (scene is empty),
  // so nothing is rescaled.
  l_Scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit(1.0));

  //LOG_INFO << "FBX SDK scene successfully created";
  return true;
}

// Create mesh.
FbxNode * FBXHeaders::createMesh(FbxManager* &l_manager, FbxScene* &l_scene, WoWModel * model, const glm::mat4 & matrix, const glm::vec3 & offset, bool addUV2)
{
  // Create a node for the mesh.
  FbxNode *meshNode = FbxNode::Create(l_manager, qPrintable(model->name()));

  // Create new Matrix Data
  const auto m = glm::scale(glm::vec3(matrix[0][0], matrix[1][1], matrix[2][2]));

  // Create mesh.
  const auto num_of_vertices = model->origVertices.size();
  FbxMesh* mesh = FbxMesh::Create(l_manager, model->name().toStdString().c_str());
  mesh->InitControlPoints((int)num_of_vertices);
  FbxVector4* vertices = mesh->GetControlPoints();

  // Set the normals on Layer 0.
  auto layer = mesh->GetLayer(0);
  if (layer == nullptr)
  {
    mesh->CreateLayer();
    layer = mesh->GetLayer(0);
  }

  // We want to have one normal for each vertex (or control point),
  // so we set the mapping mode to eBY_CONTROL_POINT.
  FbxLayerElementNormal* layer_normal = FbxLayerElementNormal::Create(mesh, "");
  layer_normal->SetMappingMode(FbxLayerElement::eByControlPoint);
  layer_normal->SetReferenceMode(FbxLayerElement::eDirect);

  // Create UV for Diffuse channel.
  FbxLayerElementUV* layer_texcoord = FbxLayerElementUV::Create(mesh, "DiffuseUV");
  layer_texcoord->SetMappingMode(FbxLayerElement::eByControlPoint);
  layer_texcoord->SetReferenceMode(FbxLayerElement::eDirect);
  layer->SetUVs(layer_texcoord, FbxLayerElement::eTextureDiffuse);

  // Optional second UV set (component / raw node-based mode). Modern M2 vertices carry UV2 in the
  // trailing two ints of the 48-byte vertex, reinterpreted as float (see ModelRenderPass's combiner
  // loop). The item-component workflow needs it: scrolling-glow layers and gradient masks live on
  // UV2. Added on a distinct FBX texture channel (Emissive) so Blender imports it as its own
  // uv_layer named "UV2Map". Same V-flip as UV1 so the two sets stay consistent.
  FbxLayerElementUV* layer_texcoord2 = nullptr;
  if (addUV2)
  {
    // A second UV set must live on its OWN layer's diffuse slot for Blender's FBX importer to read
    // it as a distinct uv_layer (a second UV element on layer 0's *emissive* channel is silently
    // dropped on import). Create a fresh layer and put UV2Map on its eTextureDiffuse.
    const int uv2LayerIndex = mesh->CreateLayer();
    FbxLayer* layer2 = mesh->GetLayer(uv2LayerIndex);
    layer_texcoord2 = FbxLayerElementUV::Create(mesh, "UV2Map");
    layer_texcoord2->SetMappingMode(FbxLayerElement::eByControlPoint);
    layer_texcoord2->SetReferenceMode(FbxLayerElement::eDirect);
    layer2->SetUVs(layer_texcoord2, FbxLayerElement::eTextureDiffuse);
  }

  // Fill data.
  LOG_INFO << "Adding main mesh Verts...";
  for (size_t i = 0; i < num_of_vertices; i++)
  {
    ModelVertex &v = model->origVertices[i];
    glm::vec3 Position = glm::vec3(m * glm::vec4((v.pos + offset), 1.0f));
    vertices[i].Set(Position.x * SCALE_FACTOR, Position.y * SCALE_FACTOR, Position.z * SCALE_FACTOR);
    glm::vec3 vn = glm::normalize(v.normal);
    layer_normal->GetDirectArray().Add(FbxVector4(vn.x, vn.y, vn.z));
    layer_texcoord->GetDirectArray().Add(FbxVector2(v.texcoords.x, 1.0 - v.texcoords.y));
    if (layer_texcoord2)
    {
      const float u2 = *reinterpret_cast<const float *>(&v.unk1);
      const float v2 = *reinterpret_cast<const float *>(&v.unk2);
      layer_texcoord2->GetDirectArray().Add(FbxVector2(u2, 1.0 - v2));
    }
  }

  // Create polygons.
  size_t num_of_passes = model->passes.size();
  FbxLayerElementMaterial* layer_material = FbxLayerElementMaterial::Create(mesh, "");
  layer_material->SetMappingMode(FbxLayerElement::eByPolygon);
  layer_material->SetReferenceMode(FbxLayerElement::eIndexToDirect);
  layer->SetMaterials(layer_material);

  int mtrl_index = 0;
  LOG_INFO << "Setting main mesh Polys...";
  for (size_t i = 0; i < num_of_passes; i++)
  {
    ModelRenderPass * p = model->passes[i];
    // init(true): export-time visibility (a pass invisible only at this INSTANT of its opacity
    // animation still exports). MUST match the gating FBXExporter::createMaterials() uses, since
    // mtrl_index below is matched to that loop's material order purely by counting.
    if (p->init(true))
    {
      // The actual FbxSurfaceMaterial objects are created and added to this node, in this same
      // per-pass order, by FBXExporter::createMaterials(); here we only emit the per-polygon
      // material index (mtrl_index) that the eIndexToDirect material layer maps to them. (The
      // old code looked materials up by a placeholder name "testToChange_<i>" that never existed
      // and AddMaterial(null)'d the result -- dead code that happened to be harmless only because
      // AddMaterial ignores a null.)
      ModelGeosetHD * g = model->geosets[p->geoIndex];
      size_t num_of_faces = g->icount / 3;
      for (size_t j = 0; j < num_of_faces; j++)
      {
        mesh->BeginPolygon(mtrl_index);
        mesh->AddPolygon(model->indices[g->istart + j * 3]);
        mesh->AddPolygon(model->indices[g->istart + j * 3 + 1]);
        mesh->AddPolygon(model->indices[g->istart + j * 3 + 2]);
        mesh->EndPolygon();
      }

      mtrl_index++;
    }
  }

  layer->SetNormals(layer_normal);

  // --- Tangents (computed; WoW M2 vertices carry no tangent) --------------------------------
  // Per-control-point tangent from positions + UVs + normals (standard Lengyel accumulation +
  // Gram-Schmidt orthonormalisation). The 4th component is the handedness sign Blender/Unity/
  // Unreal use to rebuild the bitangent for normal mapping. UVs use the SAME V-flip as the
  // exported UV set so the tangent basis is consistent with it.
  {
    FbxLayerElementTangent* layer_tangent = FbxLayerElementTangent::Create(mesh, "tangents");
    layer_tangent->SetMappingMode(FbxLayerElement::eByControlPoint);
    layer_tangent->SetReferenceMode(FbxLayerElement::eDirect);

    std::vector<glm::vec3> tan(num_of_vertices, glm::vec3(0.0f));
    std::vector<glm::vec3> bitan(num_of_vertices, glm::vec3(0.0f));

    for (size_t i = 0; i < num_of_passes; i++)
    {
      ModelRenderPass * p = model->passes[i];
      if (!p->init(true)) // export-time visibility, same gating as the polygon loop above
        continue;
      ModelGeosetHD * g = model->geosets[p->geoIndex];
      const size_t nf = g->icount / 3;
      for (size_t j = 0; j < nf; j++)
      {
        const uint32 a = model->indices[g->istart + j * 3];
        const uint32 b = model->indices[g->istart + j * 3 + 1];
        const uint32 c = model->indices[g->istart + j * 3 + 2];
        const ModelVertex & v0 = model->origVertices[a];
        const ModelVertex & v1 = model->origVertices[b];
        const ModelVertex & v2 = model->origVertices[c];

        const glm::vec3 e1 = v1.pos - v0.pos;
        const glm::vec3 e2 = v2.pos - v0.pos;
        const glm::vec2 uv0(v0.texcoords.x, 1.0f - v0.texcoords.y);
        const glm::vec2 duv1 = glm::vec2(v1.texcoords.x, 1.0f - v1.texcoords.y) - uv0;
        const glm::vec2 duv2 = glm::vec2(v2.texcoords.x, 1.0f - v2.texcoords.y) - uv0;

        const float denom = duv1.x * duv2.y - duv2.x * duv1.y;
        if (fabsf(denom) < 1e-10f) // degenerate UVs -> skip this triangle's contribution
          continue;
        const float r = 1.0f / denom;
        const glm::vec3 sdir = (e1 * duv2.y - e2 * duv1.y) * r;
        const glm::vec3 tdir = (e2 * duv1.x - e1 * duv2.x) * r;

        tan[a] += sdir; tan[b] += sdir; tan[c] += sdir;
        bitan[a] += tdir; bitan[b] += tdir; bitan[c] += tdir;
      }
    }

    for (size_t i = 0; i < num_of_vertices; i++)
    {
      const glm::vec3 n = glm::normalize(model->origVertices[i].normal);
      glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]); // Gram-Schmidt against the normal
      const float len = glm::length(t);
      const glm::vec3 tt = (len > 1e-6f) ? (t / len) : glm::vec3(1.0f, 0.0f, 0.0f);
      const float handed = (glm::dot(glm::cross(n, tt), bitan[i]) < 0.0f) ? -1.0f : 1.0f;
      layer_tangent->GetDirectArray().Add(FbxVector4(tt.x, tt.y, tt.z, handed));
    }

    layer->SetTangents(layer_tangent);
  }

  FbxGeometryConverter lGeometryConverter(l_manager);
  lGeometryConverter.ComputeEdgeSmoothingFromNormals(mesh);
  //convert soft/hard edge info to smoothing group info
  lGeometryConverter.ComputePolygonSmoothingFromEdgeSmoothing(mesh);

  // Set the mesh as the node attribute of the node.
  meshNode->SetNodeAttribute(mesh);

  // Set the shading mode to view texture.
  meshNode->SetShadingMode(FbxNode::eTextureShading);

  return meshNode;
}

void FBXHeaders::createSkeleton(WoWModel * l_model, FbxScene *& l_scene, FbxNode *& l_skeletonNode, std::map<int, FbxNode*>& l_boneNodes)
{
  // Grouping node that holds the rig. It is a PLAIN transform node, NOT a skeleton joint:
  // giving it an FbxSkeleton(eRoot) attribute (as before) injected a fake extra root into the
  // joint list, which DCCs import as a duplicate/parent root bone above the real skeleton
  // (a retargeting hazard in Unreal/Unity). The single real WoW bone with parent==-1 is the
  // one and only eRoot joint (typed below).
  l_skeletonNode = FbxNode::Create(l_scene, qPrintable(QString("%1_rig").arg(l_model->name())));

  std::vector<FbxSkeleton::EType> bone_types;
  size_t num_of_bones = l_model->bones.size();

  // Set bone type.
  std::vector<bool> has_children;
  has_children.resize(num_of_bones);
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone bone = l_model->bones[i];
    if (bone.parent != -1)
      has_children[bone.parent] = true;
  }

  bone_types.resize(num_of_bones);
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone bone = l_model->bones[i];

    if (bone.parent == -1)
    {
      bone_types[i] = FbxSkeleton::eRoot;
    }
    else if (has_children[i])
    {
      bone_types[i] = FbxSkeleton::eLimb;
    }
    else
    {
      bone_types[i] = FbxSkeleton::eLimbNode;
    }
  }

  // Create bone.
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone &bone = l_model->bones[i];
    glm::vec3 trans = bone.pivot;

    int pid = bone.parent;
    if (pid > -1)
      trans -= l_model->bones[pid].pivot;

    // Unique, decimal bone name. The old `FbxString += static_cast<int>(i)` appended the
    // CHARACTER whose code is i (garbage, and non-unique/empty for many i), so bones lost their
    // names -- breaking retargeting and humanoid mapping in every DCC. Build it from the index.
    FbxString bone_name(qPrintable(QString("%1_bone_%2").arg(l_model->name()).arg((int)i)));

    FbxNode* skeleton_node = FbxNode::Create(l_scene, bone_name);
    l_boneNodes[i] = skeleton_node;
    skeleton_node->LclTranslation.Set(FbxVector4(trans.x * SCALE_FACTOR, trans.y * SCALE_FACTOR, trans.z * SCALE_FACTOR));

    FbxSkeleton* skeleton_attribute = FbxSkeleton::Create(l_scene, bone_name);
    skeleton_attribute->SetSkeletonType(bone_types[i]);

    if (bone_types[i] == FbxSkeleton::eRoot)
    {
      skeleton_attribute->Size.Set(10.0 * SCALE_FACTOR);
      l_skeletonNode->AddChild(skeleton_node);
    }
    else if (bone_types[i] == FbxSkeleton::eLimb)
    {
      skeleton_attribute->LimbLength.Set(5.0 * SCALE_FACTOR * (sqrtf(trans.x * trans.x + trans.y * trans.y + trans.z * trans.z)));
      l_boneNodes[pid]->AddChild(skeleton_node);
    }
    else
    {
      skeleton_attribute->Size.Set(1.0 * SCALE_FACTOR);
      l_boneNodes[pid]->AddChild(skeleton_node);
    }

    skeleton_node->SetNodeAttribute(skeleton_attribute);
  }
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------

void FBXHeaders::storeBindPose(FbxScene* &l_scene, std::vector<FbxCluster*> l_boneClusters, FbxNode* l_meshNode)
{
  FbxPose* pose = FbxPose::Create(l_scene, "Bind Pose");
  pose->SetIsBindPose(true);

  for (auto it : l_boneClusters)
  {
    FbxNode*  node = it->GetLink();
    FbxMatrix matrix = node->EvaluateGlobalTransform();
    pose->Add(node, matrix);
  }

  pose->Add(l_meshNode, l_meshNode->EvaluateGlobalTransform());

  l_scene->AddPose(pose);
}

void FBXHeaders::storeRestPose(FbxScene* &l_scene, std::map<int, FbxNode*>& l_boneNodes)
{
  if (l_boneNodes.empty())
    return;

  // A reference "rest pose" snapshot of the skeleton at its default (un-animated) transform.
  // For WoW the default pose coincides with the bind pose, but it is stored here as a separate
  // NON-bind FbxPose because some tools/pipelines expect an explicit rest pose alongside the
  // bind pose. Each bone is recorded with its LOCAL default transform (the rest the skeleton
  // was built with in createSkeleton), so this stays correct regardless of bone count.
  // (Replaces the old stub, which returned immediately and left copy-pasted SDK demo code that
  // hard-coded three fake joints at arbitrary positions.)
  FbxPose* pose = FbxPose::Create(l_scene, "Rest Pose");
  pose->SetIsBindPose(false);

  for (auto& it : l_boneNodes)
  {
    FbxNode* node = it.second;
    if (!node)
      continue;
    FbxMatrix local = node->EvaluateLocalTransform();
    pose->Add(node, local, true /* local matrix */);
  }

  l_scene->AddPose(pose);
}

void FBXHeaders::createAnimation(WoWModel * l_model, FbxScene *& l_scene, QString animName, ModelAnimation cur_anim, std::map<int, FbxNode*>& skeleton)
{
  if (skeleton.empty())
  {
    LOG_ERROR << "No bones in skeleton, so animation will not be exported";
    return;
  }

  // Animation stack (== "take" / clip) and a single layer. Naming the stack with the clip
  // name is what Blender/Max/Unity/Unreal surface as the clip/action/take name.
  FbxAnimStack* anim_stack = FbxAnimStack::Create(l_scene, qPrintable(animName));
  FbxAnimLayer* anim_layer = FbxAnimLayer::Create(l_scene, qPrintable(animName));
  anim_stack->AddMember(anim_layer);

  // Looping: FBX has no standard "loop" flag on a take, and the SDK data-type globals used to
  // author a custom bool property are not exported by this SDK build. WoW sequences loop by
  // default (and the source flag bit 0x20 marks looped ones); since there is no portable field
  // to carry it, the clip's full [0, length] range is written and the importing DCC/engine sets
  // looping per its own clip settings. (Documented as a known limitation.)

  // Bake at a fixed, real frame rate. WoW stores per-bone keys at arbitrary millisecond times
  // with mixed interpolation (linear/hermite/bezier); sampling onto a uniform 30 fps grid and
  // writing linear keys reproduces the motion identically in every DCC and sidesteps curve-type
  // mismatches. The clip's true duration is preserved via the stack time span below.
  const double fps = 30.0;
  const double dt = 1000.0 / fps;                  // ms per frame
  const uint32 length = (cur_anim.length > 0) ? cur_anim.length : 1;
  l_scene->GetGlobalSettings().SetTimeMode(FbxTime::eFrames30);

  FbxTime startT, endT;
  startT.SetSecondDouble(0.0);
  endT.SetSecondDouble(length / 1000.0);
  anim_stack->SetLocalTimeSpan(FbxTimeSpan(startT, endT));

  // Frame sample times in ms: 0, dt, 2dt, ... and always a final key exactly at length so the
  // last pose and the clip duration are exact.
  std::vector<double> sampleMs;
  for (double t = 0.0; t < (double)length; t += dt)
    sampleMs.push_back(t);
  sampleMs.push_back((double)length);

  // Remember which bones got rotation curves so the unroll filter can fix Euler flips after.
  std::vector<int> rotatedBones;

  for (auto& it : skeleton)
  {
    int b = it.first;
    Bone& bone = l_model->bones[b];

    const bool hasRot = bone.rot.uses(cur_anim.Index);
    const bool hasScale = bone.scale.uses(cur_anim.Index);
    const bool hasTrans = bone.trans.uses(cur_anim.Index);

    if (!hasRot && !hasScale && !hasTrans) // bone is static for this clip -> keeps its rest transform
      continue;

    FbxNode* node = it.second;

    FbxAnimCurve* tx = hasTrans ? node->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true) : nullptr;
    FbxAnimCurve* ty = hasTrans ? node->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true) : nullptr;
    FbxAnimCurve* tz = hasTrans ? node->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true) : nullptr;
    FbxAnimCurve* rx = hasRot ? node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true) : nullptr;
    FbxAnimCurve* ry = hasRot ? node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true) : nullptr;
    FbxAnimCurve* rz = hasRot ? node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true) : nullptr;
    FbxAnimCurve* sx = hasScale ? node->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true) : nullptr;
    FbxAnimCurve* sy = hasScale ? node->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true) : nullptr;
    FbxAnimCurve* sz = hasScale ? node->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true) : nullptr;

    if (tx) { tx->KeyModifyBegin(); ty->KeyModifyBegin(); tz->KeyModifyBegin(); }
    if (rx) { rx->KeyModifyBegin(); ry->KeyModifyBegin(); rz->KeyModifyBegin(); }
    if (sx) { sx->KeyModifyBegin(); sy->KeyModifyBegin(); sz->KeyModifyBegin(); }

    for (double ms : sampleMs)
    {
      FbxTime time;
      time.SetSecondDouble(ms / 1000.0);
      // The final sample sits at exactly `length`, which is the loop WRAP point: getValue(length)
      // returns the START pose, not the end-of-clip pose. Writing that as the last keyframe snaps
      // the whole skeleton back to the start on the last frame (a severe 1-frame distortion at the
      // end of every take). Clamp the sampled time just below the wrap so the final key holds the
      // true end pose; the importing DCC handles the loop back to frame 0 on its own.
      uint32 wowT = (uint32)(ms + 0.5);
      if (length > 1 && wowT >= length)
        wowT = length - 1;

      if (hasTrans)
      {
        // Local translation relative to the parent bone = rest pivot offset + animated track,
        // matching Bone::calcMatrix's T(pivot)*T(animTrans)*...*T(-pivot) composition.
        glm::vec3 v = bone.trans.getValue(cur_anim.Index, wowT);
        if (bone.parent != -1)
          v += (bone.pivot - l_model->bones[bone.parent].pivot);
        int k;
        k = tx->KeyAdd(time); tx->KeySetValue(k, v.x * SCALE_FACTOR); tx->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = ty->KeyAdd(time); ty->KeySetValue(k, v.y * SCALE_FACTOR); ty->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = tz->KeyAdd(time); tz->KeySetValue(k, v.z * SCALE_FACTOR); tz->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
      }

      if (hasRot)
      {
        // Convert the WoW local quaternion to Euler degrees in the SAME order FBX nodes use by
        // default (eEulerXYZ) by round-tripping through an FbxAMatrix. This is exact and avoids
        // glm::eulerAngles' order/range ambiguity that produced flipped bones.
        glm::fquat gq = bone.rot.getValue(cur_anim.Index, wowT);
        FbxQuaternion fq(gq.x, gq.y, gq.z, gq.w);
        FbxAMatrix rm; rm.SetQ(fq);
        FbxVector4 e = rm.GetR();
        int k;
        k = rx->KeyAdd(time); rx->KeySetValue(k, (float)e[0]); rx->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = ry->KeyAdd(time); ry->KeySetValue(k, (float)e[1]); ry->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = rz->KeyAdd(time); rz->KeySetValue(k, (float)e[2]); rz->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
      }

      if (hasScale)
      {
        glm::vec3 v = bone.scale.getValue(cur_anim.Index, wowT);
        int k;
        k = sx->KeyAdd(time); sx->KeySetValue(k, v.x); sx->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = sy->KeyAdd(time); sy->KeySetValue(k, v.y); sy->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
        k = sz->KeyAdd(time); sz->KeySetValue(k, v.z); sz->KeySetInterpolation(k, FbxAnimCurveDef::eInterpolationLinear);
      }
    }

    if (tx) { tx->KeyModifyEnd(); ty->KeyModifyEnd(); tz->KeyModifyEnd(); }
    if (rx) { rx->KeyModifyEnd(); ry->KeyModifyEnd(); rz->KeyModifyEnd(); rotatedBones.push_back(b); }
    if (sx) { sx->KeyModifyEnd(); sy->KeyModifyEnd(); sz->KeyModifyEnd(); }
  }

  // Euler curves sampled per-frame can jump (e.g. 179deg -> -179deg) and make a joint spin the
  // wrong way between keys. The unroll filter rewrites each joint's 3 rotation curves to be
  // continuous, removing those flips without changing the represented orientation.
  FbxAnimCurveFilterUnroll unroll;
  for (int b : rotatedBones)
  {
    FbxNode* node = skeleton[b];
    FbxAnimCurve* r[3] = {
      node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X),
      node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y),
      node->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z)
    };
    if (r[0] && r[1] && r[2])
      unroll.Apply(r, 3);
  }
}