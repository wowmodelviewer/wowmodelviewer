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
  //LOG_INFO << "FBX SDK scene successfully created";
  return true;
}

void FBXHeaders::createSkeleton(WoWModel * l_model, FbxScene *& l_scene, FbxNode *& l_skeletonNode, std::map<int, FbxNode*>& l_boneNodes)
{
  l_skeletonNode = FbxNode::Create(l_scene, l_model->name().toStdString().c_str());
  FbxSkeleton* bone_group_skeleton_attribute = FbxSkeleton::Create(l_scene, "");
  bone_group_skeleton_attribute->SetSkeletonType(FbxSkeleton::eRoot);
  bone_group_skeleton_attribute->Size.Set(10.0 * SCALE_FACTOR);
  l_skeletonNode->SetNodeAttribute(bone_group_skeleton_attribute);

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
    Vec3D trans = bone.pivot;

    int pid = bone.parent;
    if (pid > -1)
      trans -= l_model->bones[pid].pivot;

    FbxString bone_name(l_model->name().toStdString().c_str());
    bone_name += "_bone_";
    bone_name += static_cast<int>(i);

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

void FBXHeaders::createAnimation(WoWModel * l_model, FbxScene *& l_scene, QString animName, ModelAnimation cur_anim, std::map<int, FbxNode*>& skeleton)
{
  if (skeleton.empty())
  {
    LOG_ERROR << "No bones in skeleton, so animation will not be exported";
    return;
  }

  // Animation stack and layer.
  FbxAnimStack* anim_stack = FbxAnimStack::Create(l_scene, qPrintable(animName));
  FbxAnimLayer* anim_layer = FbxAnimLayer::Create(l_scene, qPrintable(animName));
  anim_stack->AddMember(anim_layer);

  //LOG_INFO << "Animation length:" << cur_anim.length;
  float timeInc = cur_anim.length / 60;
  if (timeInc < 1.0f)
  {
    timeInc = cur_anim.length;
  }
  FbxTime::SetGlobalTimeMode(FbxTime::eFrames60);

  //LOG_INFO << "Starting frame loop...";
  for (uint32 t = 0; t < cur_anim.length; t += timeInc)
  {
    //LOG_INFO << "Starting frame" << t;
    FbxTime time;
    time.SetSecondDouble((float)t / 1000.0);

    //LOG_INFO << "Skeleton count:" << skeleton.size();
    for (auto it : skeleton)
    {
      int b = it.first;
      Bone& bone = l_model->bones[b];

      bool rot = bone.rot.uses(cur_anim.Index);
      bool scale = bone.scale.uses(cur_anim.Index);
      bool trans = bone.trans.uses(cur_anim.Index);

      if (!rot && !scale && !trans) // bone is not animated, skip it
        continue;

      if (trans)
      {
        FbxAnimCurve* t_curve_x = skeleton[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* t_curve_y = skeleton[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* t_curve_z = skeleton[b]->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

        Vec3D v = bone.trans.getValue(cur_anim.Index, t);

        if (bone.parent != -1)
        {
          Bone& parent_bone = l_model->bones[bone.parent];
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

        t_curve_x->Destroy();
        t_curve_y->Destroy();
        t_curve_z->Destroy();
      }

      if (rot)
      {
        FbxAnimCurve* r_curve_x = skeleton[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* r_curve_y = skeleton[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* r_curve_z = skeleton[b]->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

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
        r_curve_x->KeySetValue(key_index, x);
        r_curve_x->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
        r_curve_x->KeyModifyEnd();

        r_curve_y->KeyModifyBegin();
        key_index = r_curve_y->KeyAdd(time);
        r_curve_y->KeySetValue(key_index, y);
        r_curve_y->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
        r_curve_y->KeyModifyEnd();

        r_curve_z->KeyModifyBegin();
        key_index = r_curve_z->KeyAdd(time);
        r_curve_z->KeySetValue(key_index, z);
        r_curve_z->KeySetInterpolation(key_index, bone.rot.type == INTERPOLATION_LINEAR ? FbxAnimCurveDef::eInterpolationLinear : FbxAnimCurveDef::eInterpolationCubic);
        r_curve_z->KeyModifyEnd();

        r_curve_x->Destroy();
        r_curve_y->Destroy();
        r_curve_z->Destroy();
      }

      if (scale)
      {
        FbxAnimCurve* s_curve_x = skeleton[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
        FbxAnimCurve* s_curve_y = skeleton[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
        FbxAnimCurve* s_curve_z = skeleton[b]->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

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

        s_curve_x->Destroy();
        s_curve_y->Destroy();
        s_curve_z->Destroy();
      }
    }

    //LOG_INFO << "Ended frame" << t;
  }

  anim_stack->Destroy(true);
}