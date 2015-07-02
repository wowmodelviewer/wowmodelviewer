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

// Qt

// Externals

// Other libraries
#include "Bone.h"
#include "globalvars.h"
#include "WoWModel.h"

#include "metaclasses/Iterator.h"

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
  reset();
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

// Create mesh.
void FBXExporter::createMesh()
{
  // Get the scene��s root node.
  FbxNode* root_node = m_p_scene->GetRootNode();
  // Create a node for the mesh.
  m_p_meshNode = FbxNode::Create(m_p_manager, m_p_model->name().c_str());

  // Set the node as a child of the scene��s root node.
  root_node->AddChild(m_p_meshNode);

  // Create mesh.
  size_t num_of_vertices = m_p_model->header.nVertices;
  FbxMesh* mesh = FbxMesh::Create(m_p_manager, m_p_model->name().c_str());
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


bool uses_anim(Bone &b, size_t anim_index)
{
  return (b.trans.uses(anim_index) || b.rot.uses(anim_index) || b.scale.uses(anim_index));
}

bool has_anim(WoWModel* m, size_t anim_index)
{
  for (size_t n = 0; n < m->header.nBones; n++)
  {
    Bone& b = m->bones[n];
    if (uses_anim(b, anim_index))
      return true;
  }
  return false;
}
typedef size_t      TimeT;
typedef map<TimeT, int> Timeline;

static const int KEY_TRANSLATE  = 1;
static const int KEY_ROTATE   = 2;
static const int KEY_SCALE    = 4;

void updateTimeline(Timeline &timeline, vector<TimeT> &times, int keyMask)
{
  size_t numTimes = times.size();
  for (size_t n = 0; n < numTimes; n++)
  {
    TimeT time = times[n];
    Timeline::iterator it = timeline.find(time);
    if (it != timeline.end())
      it->second |= keyMask;
    else
      timeline[time] = keyMask;
  }
}

void FBXExporter::createSkeleton()
{
  // Get the scene��s root node.
  FbxNode* root_node = m_p_scene->GetRootNode();

  FbxNode* bone_group_node = FbxNode::Create(m_p_scene, m_p_model->name().c_str());
  FbxSkeleton* bone_group_skeleton_attribute = FbxSkeleton::Create(m_p_scene, "");
  bone_group_skeleton_attribute->SetSkeletonType(FbxSkeleton::eRoot);
  bone_group_skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
  bone_group_node->SetNodeAttribute(bone_group_skeleton_attribute);
  root_node->AddChild(bone_group_node);
  FbxAMatrix matrix;

  FbxSkin* skin = FbxSkin::Create(m_p_scene, "");
  std::vector<FbxNode*> bone_nodes;
  std::vector<FbxCluster*> bone_clusters;
  std::vector<FbxSkeleton::EType> bone_types;
  size_t num_of_bones = m_p_model->header.nBones;

  // Set bone type.
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone bone = m_p_model->bones[i];
    if (bone.parent == -1)
    {
      bone_types.push_back(FbxSkeleton::eRoot);
    }
    else
    {
      if (bone_types[bone.parent] != FbxSkeleton::eRoot)
      {
        bone_types[bone.parent] = FbxSkeleton::eLimb;
      }
      bone_types.push_back(FbxSkeleton::eLimbNode);
    }
  }

  // Create bone.
  for (size_t i = 0; i < num_of_bones; ++i)
  {
    Bone &bone = m_p_model->bones[i];
    Vec3D trans = bone.pivot;
    int pid = bone.parent;
    if (pid > -1)
      trans -= m_p_model->bones[pid].pivot;

    FbxString bone_name(m_p_model->name().c_str());
    bone_name += "_bone_";
    bone_name += static_cast<int>(i);

    FbxNode* skeleton_node = FbxNode::Create(m_p_scene, bone_name);
    bone_nodes.push_back(skeleton_node);
    skeleton_node->LclTranslation.Set(FbxVector4(trans.x * SCALE_FACTOR, trans.y * SCALE_FACTOR, trans.z * SCALE_FACTOR));

    FbxSkeleton* skeleton_attribute = FbxSkeleton::Create(m_p_scene, bone_name);

    if (bone_types[i] == FbxSkeleton::eRoot)
    {
      skeleton_attribute->SetSkeletonType(FbxSkeleton::eRoot);
      skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
      bone_group_node->AddChild(skeleton_node);
    }
    else if (bone_types[i] == FbxSkeleton::eLimb)
    {
      skeleton_attribute->SetSkeletonType(FbxSkeleton::eLimb);
      skeleton_attribute->LimbLength.Set(100.0 * SCALE_FACTOR * (sqrtf(trans.x * trans.x + trans.y * trans.y + trans.z * trans.z)));
      bone_nodes[pid]->AddChild(skeleton_node);
    }
    else
    {
      skeleton_attribute->SetSkeletonType(FbxSkeleton::eLimbNode);
      skeleton_attribute->Size.Set(100.0 * SCALE_FACTOR);
      bone_nodes[pid]->AddChild(skeleton_node);
    }

    skeleton_node->SetNodeAttribute(skeleton_attribute);

    FbxCluster* cluster = FbxCluster::Create(m_p_scene, "");
    bone_clusters.push_back(cluster);
    cluster->SetLink(skeleton_node);
    cluster->SetLinkMode(FbxCluster::eTotalOne);

    matrix = m_p_meshNode->EvaluateGlobalTransform();
    cluster->SetTransformMatrix(matrix);
    matrix = skeleton_node->EvaluateGlobalTransform();
    cluster->SetTransformLinkMatrix(matrix);
    skin->AddCluster(bone_clusters[i]);
  }

  size_t num_of_vertices = m_p_model->header.nVertices;
  for (size_t i = 0; i < num_of_vertices; i++)
  {
    ModelVertex& vertex = m_p_model->origVertices[i];
    for (size_t j = 0; j < 4; j++)
    {
      if ((vertex.bones[j] == 0) && (vertex.weights[j] == 0))
        continue;
      bone_clusters[vertex.bones[j]]->AddControlPointIndex((int)i, static_cast<double>(vertex.weights[j]) / 255.0);
    }
  }

  skin->SetGeometry(m_p_meshNode->GetMesh());
  LOG_INFO << "Skeleton successfully created";



  std::map<int, std::string> animsMap = m_p_model->getAnimsMap();
  for (unsigned int i=0; i<m_p_model->header.nAnimations; i++)
  {
    std::string anim_name = animsMap[m_p_model->anims[i].animID];
    if(std::find(m_animsToExport.begin(), m_animsToExport.end(), m_p_model->anims[i].animID) == m_animsToExport.end())
         continue;

    // Animation stack and layer.

    FbxAnimStack* anim_stack = FbxAnimStack::Create(m_p_scene, anim_name.c_str());
    FbxAnimLayer* anim_layer = FbxAnimLayer::Create(m_p_scene, anim_name.c_str());
    anim_stack->AddMember(anim_layer);

    for (unsigned int b = 0; b < num_of_bones; b++)
    {
      Bone& bone = m_p_model->bones[b];
      if (uses_anim(bone, i))
      {
        Timeline timeline;
        updateTimeline(timeline, bone.rot.times[i], KEY_ROTATE);
        updateTimeline(timeline, bone.scale.times[i], KEY_SCALE);
        size_t ntrans = 0;
        size_t nrot = 0;
        size_t nscale = 0;

        FbxAnimCurve* t_curve_x = bone_nodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* t_curve_y = bone_nodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* t_curve_z = bone_nodes[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
        FbxAnimCurve* r_curve_x = bone_nodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* r_curve_y = bone_nodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* r_curve_z = bone_nodes[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
        FbxAnimCurve* s_curve_x = bone_nodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* s_curve_y = bone_nodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* s_curve_z = bone_nodes[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

        FbxTime time;
        for (Timeline::iterator it = timeline.begin(); it != timeline.end(); it++)
        {
          time.SetSecondDouble(static_cast<double>(it->first) / 1000.0);
          if ((it->second & KEY_TRANSLATE) && (ntrans < bone.trans.data[i].size()))
          {
            Vec3D v = bone.trans.getValue(i, it->first);
            if (bone.parent >= 0)
            {
              Bone& parent_bone = m_p_model->bones[bone.parent];
              v += (bone.pivot - parent_bone.pivot);
            }
            ntrans++;

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
          if (it->second & KEY_ROTATE)
          {
            float x, y, z;

            Quaternion q = bone.rot.getValue(i, it->first);
            FbxQuaternion quat(q.x, q.y, q.z, q.w);
            FbxVector4 angle = quat.DecomposeSphericalXYZ();

            x = angle[0];
            y = angle[1];
            z = angle[2];

            r_curve_x->KeyModifyBegin();
            int key_index = r_curve_x->KeyAdd(time);
            r_curve_x->KeySetValue(key_index,-x);
            r_curve_x->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
            r_curve_x->KeyModifyEnd();

            r_curve_y->KeyModifyBegin();
            key_index = r_curve_y->KeyAdd(time);
            r_curve_y->KeySetValue(key_index,-y);
            r_curve_y->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
            r_curve_y->KeyModifyEnd();

            r_curve_z->KeyModifyBegin();
            key_index = r_curve_z->KeyAdd(time);
            r_curve_z->KeySetValue(key_index,-z);
            r_curve_z->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
            r_curve_z->KeyModifyEnd();
          }
          if (it->second & KEY_SCALE)
          {
            Vec3D& v = bone.scale.getValue(i, it->first);

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
}

// Get Proper Texture Names for an M2 File
wxString GetM2TextureName(WoWModel *m, ModelRenderPass p, size_t PassNumber)
{
  wxString texName;
  if ((int)m->TextureList.size() > p.tex)
    texName = m->TextureList[p.tex].BeforeLast(wxT('.')).AfterLast(SLASH);

  if (texName.Len() == 0)
    texName = m->modelname.BeforeLast(wxT('.')).AfterLast(SLASH) + wxString::Format(wxT("_Image_%03i"),PassNumber);

  return texName;
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
      FbxString mtrl_name = m_p_model->name().c_str();
      mtrl_name.Append("_", 1);
      char tmp[32];
      _itoa((int)i, tmp, 10);
      mtrl_name.Append(tmp, strlen(tmp));

      // Create material.
      FbxString shading_name = "Phong";
      FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
      material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

      wxString tex = m_p_model->TextureList[pass.tex];
      std::string tex_name = tex.AfterLast(SLASH).BeforeLast('.').c_str();
      tex_name += ".png";
      wxString tex_fullpath_filename = wxString(m_filename.c_str()).BeforeLast(SLASH) + wxT(SLASH) + tex_name.c_str();

      if(tex_name.find("Body") != std::string::npos)
        m_texturesToExport[tex_fullpath_filename.c_str()] = m_p_model->replaceTextures[TEXTURE_BODY];
      else
        m_texturesToExport[tex_fullpath_filename.c_str()] = texturemanager.add(tex.c_str());

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


bool FBXExporter::exportModel(WoWModel * model, std::string target)
{
  if(!model)
    return false;

  m_p_model = model;
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


  m_p_scene = FbxScene::Create(m_p_manager, "My Scene");
  if(!m_p_scene)
  {
    LOG_ERROR << "Unable to create FBX scene";
    return false;
  }
  LOG_INFO << "FBX SDK scene successfully created";

  // export main model mesh
  try
  {
    createMesh();
    LOG_INFO << "Mesh successfully created";
    createMaterials();
    LOG_INFO << "Materials successfully created";
    createSkeleton();
    LOG_INFO << "Skeleton successfully created";
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
void FBXExporter::reset()
{
  if(m_p_manager)
    m_p_manager->Destroy();
  m_p_manager = 0;

  if(m_p_scene)
    m_p_scene->Destroy();
  m_p_scene = 0;

  m_p_model = 0;
  m_p_meshNode = 0;

  m_filename = "";
  m_texturesToExport.clear();
}
