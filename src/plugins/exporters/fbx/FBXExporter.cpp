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
#include <sstream>

// Qt

// Externals

// Other libraries
#include "Bone.h"
#include "globalvars.h"
#include "WoWModel.h"

#include "util.h" // SLASH

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/arrstr.h>
#include <wx/choicdlg.h>


// Current library

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------
#define SCALE_FACTOR 50.0f

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

std::string FBXExporter::menuLabel() const
{
  return "FBX...";
}

std::string FBXExporter::fileSaveTitle() const
{
  return "Save FBX file";
}

std::string FBXExporter::fileSaveFilter() const
{
  return "FBX files (*.fbx)|*.fbx";
}

bool FBXExporter::exportModel(Model * model, std::string target)
{
  reset();

  m_p_model = dynamic_cast<WoWModel *>(model);

  if(!m_p_model)
      return false;

  m_filename = target;

  m_p_manager = FbxManager::Create();

  if (!m_p_manager)
  {
    LOG_ERROR << "Unable to create the FBX SDK manager";
    return false;
  }
  LOG_INFO << "FBX SDK manager successfully created";

  // create an IOSettings object
  FbxIOSettings *ios = FbxIOSettings::Create(m_p_manager, IOSROOT);
  m_p_manager->SetIOSettings(ios);

  // ensure that textures are embed in final fbx file
  ios->SetBoolProp(EXP_FBX_EMBEDDED, true);

  // Create an exporter.
  FbxExporter* exporter = FbxExporter::Create(m_p_manager, "");

  // Initialize the exporter.
  if(!exporter->Initialize(target.c_str(), -1, m_p_manager->GetIOSettings()))
  {
    LOG_ERROR << "Unable to create the FBX SDK exporter";
    return false;
  }
  LOG_INFO << "FBX SDK exporter successfully created";

  // make file compatible with older fbx versions
  exporter->SetFileExportVersion(FBX_2014_00_COMPATIBLE);

  m_p_scene = FbxScene::Create(m_p_manager, "My Scene");
  if(!m_p_scene)
  {
    LOG_ERROR << "Unable to create FBX scene";
    return false;
  }
  LOG_INFO << "FBX SDK scene successfully created";

  // add some info to exported scene
  FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(m_p_manager,"SceneInfo");
  sceneInfo->mTitle = m_p_model->name().toStdString().c_str();
  sceneInfo->mAuthor = GLOBALSETTINGS.appName().c_str();
  sceneInfo->mRevision = GLOBALSETTINGS.appVersion().c_str();
  m_p_scene->SetSceneInfo(sceneInfo);


  // export main model mesh
  // follow FBX SDK example (ExportScene01) for organization
  try
  {
    createMesh();
    LOG_INFO << "Mesh successfully created";

    createMaterials();
    LOG_INFO << "Materials successfully created";

    createSkeleton();
    LOG_INFO << "Skeleton successfully created";

    // add all those things to the scene
    FbxNode* root_node = m_p_scene->GetRootNode();
    root_node->AddChild(m_p_meshNode);
    root_node->AddChild(m_p_skeletonNode);

    linkMeshAndSkeleton();

    storeBindPose();

    createAnimations();
    LOG_INFO << "Animations successfully created";


  }
  catch(const std::exception& ex)
  {
    LOG_ERROR << "Error during export : " << ex.what();
    return false;
  }

  /*
  // export equipped item
  Iterator<WoWItem> itemIt(model);
  for(itemIt.begin(); !itemIt.ended(); itemIt++)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*itemIt)->itemModels;
    for(std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin() ;
        it != itemModels.end();
        ++it)
    {
      createMesh(m_p_manager, scene, it->second);
    }
  }
*/

  if(!exporter->Export(m_p_scene))
  {
    LOG_ERROR << "Unable to export FBX scene";
    return false;
  }

  // delete texture files created during export
  for(std::map<std::string, GLuint>::iterator it = m_texturesToExport.begin();
      it != m_texturesToExport.end();
      ++it)
  {
   remove((it->first).c_str());
  }

  LOG_INFO << "FBX scene successfully exported";
  return true;
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
// Create mesh.
void FBXExporter::createMesh()
{
  // Create a node for the mesh.
  m_p_meshNode = FbxNode::Create(m_p_manager, m_p_model->name().toStdString().c_str());

  // Create mesh.
  size_t num_of_vertices = m_p_model->header.nVertices;
  FbxMesh* mesh = FbxMesh::Create(m_p_manager, m_p_model->name().toStdString().c_str());
  mesh->InitControlPoints((int)num_of_vertices);
  FbxVector4* vertices = mesh->GetControlPoints();

  // Set the normals on Layer 0.
  FbxLayer* layer = mesh->GetLayer(0);
  if (layer == 0)
  {
    mesh->CreateLayer();
    layer = mesh->GetLayer(0);
  }

  // We want to have one normal for each vertex (or control point),
  // so we set the mapping mode to eBY_CONTROL_POINT.
  FbxLayerElementNormal* layer_normal= FbxLayerElementNormal::Create(mesh, "");
  layer_normal->SetMappingMode(FbxLayerElement::eByControlPoint );
  layer_normal->SetReferenceMode(FbxLayerElement::eDirect);
  layer->SetNormals(layer_normal);

  // Create UV for Diffuse channel.
  FbxLayerElementUV* layer_texcoord = FbxLayerElementUV::Create(mesh, "DiffuseUV");
  layer_texcoord->SetMappingMode(FbxLayerElement::eByControlPoint);
  layer_texcoord->SetReferenceMode(FbxLayerElement::eDirect);
  layer->SetUVs(layer_texcoord, FbxLayerElement::eTextureDiffuse);

  // Fill data.
  for (size_t i = 0; i < num_of_vertices; i++)
  {
    ModelVertex &v = m_p_model->origVertices[i];
    vertices[i].Set(v.pos.x * SCALE_FACTOR, v.pos.y * SCALE_FACTOR, v.pos.z * SCALE_FACTOR);
    layer_normal->GetDirectArray().Add(FbxVector4(v.normal.x, v.normal.y, v.normal.z));
    layer_texcoord->GetDirectArray().Add(FbxVector2(v.texcoords.x, 1.0 - v.texcoords.y));
  }

  // Create polygons.
  size_t num_of_passes = m_p_model->passes.size();
  FbxLayerElementMaterial* layer_material=FbxLayerElementMaterial::Create(mesh, "");
  layer_material->SetMappingMode(FbxLayerElement::eByPolygon);
  layer_material->SetReferenceMode(FbxLayerElement::eIndexToDirect);
  layer->SetMaterials(layer_material);

  int mtrl_index = 0;
  for (size_t i = 0; i < num_of_passes; i++)
  {
    ModelRenderPass& p = m_p_model->passes[i];
    if (p.init(m_p_model))
    {
      // Build material name.
      FbxString mtrl_name = "testToChange";
      mtrl_name.Append("_", 1);
      char tmp[32];
      _itoa((int)i, tmp, 10);
      mtrl_name.Append(tmp, strlen(tmp));
      FbxSurfaceMaterial* material = m_p_scene->GetMaterial(mtrl_name.Buffer());
      m_p_meshNode->AddMaterial(material);

      ModelGeosetHD g = m_p_model->geosets[p.geoset];
      size_t num_of_faces = g.icount / 3;
      for (size_t j = 0; j < num_of_faces; j++)
      {
        mesh->BeginPolygon(mtrl_index);
        mesh->AddPolygon(m_p_model->indices[g.istart + j * 3]);
        mesh->AddPolygon(m_p_model->indices[g.istart + j * 3 + 1]);
        mesh->AddPolygon(m_p_model->indices[g.istart + j * 3 + 2]);
        mesh->EndPolygon();
      }

      mtrl_index++;
    }
  }

  // Set mesh smoothness.
  mesh->SetMeshSmoothness(FbxMesh::eFine);

  // Set the mesh as the node attribute of the node.
  m_p_meshNode->SetNodeAttribute(mesh);

  // Set the shading mode to view texture.
  m_p_meshNode->SetShadingMode(FbxNode::eTextureShading);
}

void FBXExporter::createSkeleton()
{
  m_p_skeletonNode = FbxNode::Create(m_p_scene, m_p_model->name().toStdString().c_str());
  FbxSkeleton* bone_group_skeleton_attribute = FbxSkeleton::Create(m_p_scene, "");
  bone_group_skeleton_attribute->SetSkeletonType(FbxSkeleton::eRoot);
  bone_group_skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
  m_p_skeletonNode->SetNodeAttribute(bone_group_skeleton_attribute);

  std::vector<FbxSkeleton::EType> bone_types;
  size_t num_of_bones = m_p_model->header.nBones;

  // Set bone type.
  std::vector<bool> has_children;
  has_children.resize(num_of_bones);
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone bone = m_p_model->bones[i];
    if(bone.parent != -1)
      has_children[bone.parent] = true;
  }

  bone_types.resize(num_of_bones);
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone bone = m_p_model->bones[i];

    if (bone.parent == -1)
    {
      bone_types[i] = FbxSkeleton::eRoot;
    }
    else if(has_children[i])
    {
      bone_types[i] = FbxSkeleton::eLimb;
    }
    else
    {
      bone_types[i] = FbxSkeleton::eLimbNode;
    }
  }

  // filter out bones without any vertex attached
  size_t num_of_vertices = m_p_model->header.nVertices;
  std::vector<bool> has_vertex;
  has_vertex.resize(num_of_bones);

  for (size_t i = 0; i < num_of_vertices; i++)
  {
    ModelVertex& vertex = m_p_model->origVertices[i];
    for (size_t j = 0; j < 4; j++)
    {
      if ((vertex.bones[j] == 0) && (vertex.weights[j] == 0))
        continue;
      has_vertex[vertex.bones[j]] = true;
    }
  }

  // Create bone.
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    if(!has_vertex[i] && !has_children[i])
      continue;

    Bone &bone = m_p_model->bones[i];
    Vec3D trans = bone.pivot;

    int pid = bone.parent;
    if (pid > -1)
      trans -= m_p_model->bones[pid].pivot;

    FbxString bone_name(m_p_model->name().toStdString().c_str());
    bone_name += "_bone_";
    bone_name += static_cast<int>(i);

    FbxNode* skeleton_node = FbxNode::Create(m_p_scene, bone_name);
    m_boneNodes[i] = skeleton_node;
    skeleton_node->LclTranslation.Set(FbxVector4(trans.x * SCALE_FACTOR, trans.y * SCALE_FACTOR, trans.z * SCALE_FACTOR));

    FbxSkeleton* skeleton_attribute = FbxSkeleton::Create(m_p_scene, bone_name);
    skeleton_attribute->SetSkeletonType(bone_types[i]);

    if (bone_types[i] == FbxSkeleton::eRoot)
    {
      skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
      m_p_skeletonNode->AddChild(skeleton_node);
    }
    else if (bone_types[i] == FbxSkeleton::eLimb)
    {
      skeleton_attribute->LimbLength.Set(100.0 * SCALE_FACTOR * (sqrtf(trans.x * trans.x + trans.y * trans.y + trans.z * trans.z)));
      m_boneNodes[pid]->AddChild(skeleton_node);
    }
    else
    {
      skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
      m_boneNodes[pid]->AddChild(skeleton_node);
    }

    skeleton_node->SetNodeAttribute(skeleton_attribute);
  }
}

void FBXExporter::linkMeshAndSkeleton()
{
  // create clusters
  for(std::map<int,FbxNode*>::iterator it = m_boneNodes.begin() ;
            it != m_boneNodes.end();
            ++it)
  {
    FbxCluster* cluster = FbxCluster::Create(m_p_scene, "");
    m_boneClusters.push_back(cluster);
    cluster->SetLink(it->second);
    cluster->SetLinkMode(FbxCluster::eTotalOne);
  }

  // set initial matrices
  FbxAMatrix matrix = m_p_meshNode->EvaluateGlobalTransform();
  for(std::vector<FbxCluster*>::iterator it = m_boneClusters.begin() ;
              it != m_boneClusters.end();
              ++it)
  {
    (*it)->SetTransformMatrix(matrix);
  }

  // set link matrices
  std::vector<FbxCluster*>::iterator clusterIt = m_boneClusters.begin();
  for(std::map<int,FbxNode*>::iterator it = m_boneNodes.begin() ;
              it != m_boneNodes.end();
              ++it, ++clusterIt)
  {
    matrix = it->second->EvaluateGlobalTransform();
    (*clusterIt)->SetTransformLinkMatrix(matrix);
  }

  // define control points
  size_t num_of_vertices = m_p_model->header.nVertices;
  for (size_t i = 0; i < num_of_vertices; i++)
  {
    ModelVertex& vertex = m_p_model->origVertices[i];
    for (size_t j = 0; j < 4; j++)
    {
      if ((vertex.bones[j] == 0) && (vertex.weights[j] == 0))
        continue;
      m_boneClusters[vertex.bones[j]]->AddControlPointIndex((int)i, static_cast<double>(vertex.weights[j]) / 255.0);
    }
  }

  // add cluster to skin
  FbxGeometry* lMeshAttribute = (FbxGeometry*) m_p_meshNode->GetNodeAttribute();
  FbxSkin* skin = FbxSkin::Create(m_p_scene, "");

  for(std::vector<FbxCluster*>::iterator it = m_boneClusters.begin() ;
      it != m_boneClusters.end();
      ++it)
  {
    skin->AddCluster(*it);
  }

  lMeshAttribute->AddDeformer(skin);
}

void FBXExporter::createAnimations()
{
  if(m_boneNodes.empty())
  {
    LOG_INFO << "No bone in skeleton, so no animation will be exported";
    return;
  }

  std::map<int, std::string> animsMap = m_p_model->getAnimsMap();
  for (unsigned int anim=0; anim<m_p_model->header.nAnimations; anim++)
  {
    ModelAnimation cur_anim = m_p_model->anims[anim];

    if(std::find(m_animsToExport.begin(), m_animsToExport.end(), cur_anim.Index) == m_animsToExport.end())
      continue;

    std::stringstream ss;
    ss << animsMap[cur_anim.animID];
    ss << " [";
    ss << cur_anim.Index;
    ss << "]";

    std::string anim_name =  ss.str();

    // Animation stack and layer.
    FbxAnimStack* anim_stack = FbxAnimStack::Create(m_p_scene, anim_name.c_str());
    FbxAnimLayer* anim_layer = FbxAnimLayer::Create(m_p_scene, anim_name.c_str());
    anim_stack->AddMember(anim_layer);

    uint32 timeInc = (cur_anim.timeEnd - cur_anim.timeStart)/60;

    FbxTime::SetGlobalTimeMode(FbxTime::eFrames60);

    for(uint32 t = cur_anim.timeStart ; t < cur_anim.timeEnd ; t += timeInc)
    {
      FbxTime time;
      time.SetSecondDouble((float)t / 1000.0);

      for(std::map<int,FbxNode*>::iterator it = m_boneNodes.begin() ;
          it != m_boneNodes.end();
          ++it)

      {
        int b = it->first;
        Bone& bone = m_p_model->bones[b];

        bool rot = bone.rot.uses(cur_anim.Index);
        bool scale = bone.scale.uses(cur_anim.Index);
        bool trans = bone.trans.uses(cur_anim.Index);

        if(!rot && !scale && !trans) // bone is not animated, skip it
          continue;

        FbxAnimCurve* t_curve_x = m_boneNodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* t_curve_y = m_boneNodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* t_curve_z = m_boneNodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
        FbxAnimCurve* r_curve_x = m_boneNodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* r_curve_y = m_boneNodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* r_curve_z = m_boneNodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
        FbxAnimCurve* s_curve_x = m_boneNodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* s_curve_y = m_boneNodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* s_curve_z = m_boneNodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

        if (trans)
        {
          Vec3D v = bone.trans.getValue(cur_anim.Index,t);

          if (bone.parent != -1)
          {
            Bone& parent_bone = m_p_model->bones[bone.parent];
            v += (bone.pivot - parent_bone.pivot);
          }

          t_curve_x->KeyModifyBegin();
          int key_index = t_curve_x->KeyAdd(time);
          t_curve_x->KeySetValue(key_index, v.x * SCALE_FACTOR);
          t_curve_x->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          t_curve_x->KeyModifyEnd();

          t_curve_y->KeyModifyBegin();
          key_index = t_curve_y->KeyAdd(time);
          t_curve_y->KeySetValue(key_index, v.y * SCALE_FACTOR);
          t_curve_y->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          t_curve_y->KeyModifyEnd();

          t_curve_z->KeyModifyBegin();
          key_index = t_curve_z->KeyAdd(time);
          t_curve_z->KeySetValue(key_index, v.z * SCALE_FACTOR);
          t_curve_z->KeySetInterpolation(key_index, bone.trans.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          t_curve_z->KeyModifyEnd();
        }

        if (rot)
        {
          float x, y, z;

          Quaternion q = bone.rot.getValue(cur_anim.Index, t);
          Quaternion tq;
          tq.x = q.w; tq.y = q.x; tq.z = q.y; tq.w = q.z;

          Vec3D rot = tq.toEulerXYZ();

          x = rot.x * -(180.0f / PI);
          y = rot.y * -(180.0f / PI);
          z = rot.z * -(180.0f / PI);

          r_curve_x->KeyModifyBegin();
          int key_index = r_curve_x->KeyAdd(time);
          r_curve_x->KeySetValue(key_index,x);
          r_curve_x->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          r_curve_x->KeyModifyEnd();

          r_curve_y->KeyModifyBegin();
          key_index = r_curve_y->KeyAdd(time);
          r_curve_y->KeySetValue(key_index,y);
          r_curve_y->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          r_curve_y->KeyModifyEnd();

          r_curve_z->KeyModifyBegin();
          key_index = r_curve_z->KeyAdd(time);
          r_curve_z->KeySetValue(key_index,z);
          r_curve_z->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          r_curve_z->KeyModifyEnd();
        }

        if (scale)
        {
          Vec3D v = bone.scale.getValue(cur_anim.Index, t);

          s_curve_x->KeyModifyBegin();
          int key_index = s_curve_x->KeyAdd(time);
          s_curve_x->KeySetValue(key_index, v.x);
          s_curve_x->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          s_curve_x->KeyModifyEnd();

          s_curve_y->KeyModifyBegin();
          key_index = s_curve_y->KeyAdd(time);
          s_curve_y->KeySetValue(key_index, v.y);
          s_curve_y->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          s_curve_y->KeyModifyEnd();

          s_curve_z->KeyModifyBegin();
          key_index = s_curve_z->KeyAdd(time);
          s_curve_z->KeySetValue(key_index, v.z);
          s_curve_z->KeySetInterpolation(key_index, bone.scale.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
          s_curve_z->KeyModifyEnd();
        }
      }
    }
  }
}

// Create materials.
void FBXExporter::createMaterials()
{
  for (unsigned int i = 0; i < m_p_model->passes.size(); i++)
  {
    ModelRenderPass& pass = m_p_model->passes[i];
    if (pass.init(m_p_model))
    {
      // Build material name.
      FbxString mtrl_name = m_p_model->name().toStdString().c_str();
      mtrl_name.Append("_", 1);
      char tmp[32];
      _itoa((int)i, tmp, 10);
      mtrl_name.Append(tmp, strlen(tmp));

      // Create material.
      FbxString shading_name = "Phong";
      FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
      material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

      wxString tex = m_p_model->TextureList[pass.tex]->fullname().toStdString().c_str();
      std::string tex_name = tex.AfterLast(SLASH).BeforeLast('.').c_str();
      tex_name += ".png";
      wxString tex_fullpath_filename = wxString(m_filename.c_str()).BeforeLast(SLASH) + wxT(SLASH) + tex_name.c_str();

      if(tex_name.find("Body") != std::string::npos)
        m_texturesToExport[tex_fullpath_filename.c_str()] = m_p_model->replaceTextures[TEXTURE_BODY];
      else
        m_texturesToExport[tex_fullpath_filename.c_str()] = texturemanager.get(tex.c_str());

      FbxFileTexture* texture = FbxFileTexture::Create(m_p_manager, tex_name.c_str());
      texture->SetFileName(tex_fullpath_filename.c_str());
      texture->SetTextureUse(FbxTexture::eStandard);
      texture->SetMappingType(FbxTexture::eUV);
      texture->SetMaterialUse(FbxFileTexture::eModelMaterial);
      texture->SetSwapUV(false);
      texture->SetTranslation(0.0, 0.0);
      texture->SetScale(1.0, 1.0);
      texture->SetRotation(0.0, 0.0);
      texture->UVSet.Set(FbxString("DiffuseUV"));
      material->Diffuse.ConnectSrcObject(texture);

      // Add material to the scene.
      m_p_meshNode->AddMaterial(material);
    }
  }
  for(std::map<std::string, GLuint>::iterator it = m_texturesToExport.begin();
      it != m_texturesToExport.end();
      ++it)
  {
    exportGLTexture(it->second, it->first);
  }

}

void FBXExporter::storeBindPose()
{
  FbxPose* pose = FbxPose::Create(m_p_scene,"Bind Pose");
  pose->SetIsBindPose(true);

  for(std::vector<FbxCluster*>::iterator it = m_boneClusters.begin() ;
      it != m_boneClusters.end();
      ++it)
  {
    FbxNode*  node   = (*it)->GetLink();
    FbxMatrix matrix = node->EvaluateGlobalTransform();
    pose->Add(node, matrix);
  }

  pose->Add(m_p_meshNode, m_p_meshNode->EvaluateGlobalTransform());

  m_p_scene->AddPose(pose);
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

  m_filename = "";

  m_boneNodes.clear();
  m_texturesToExport.clear();
  m_boneClusters.clear();
}
