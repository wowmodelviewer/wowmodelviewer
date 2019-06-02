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
 * FBXAnimExporter.cpp
 *
 *  Created on: 14 may 2019
 *   Copyright: 2019 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _FBXANIMEXPORTER_CPP_
#include "FBXAnimExporter.h"
#undef _FBXANIMEXPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <sstream>

// Qt
#include <qdir.h>

// Externals
#include <fbxsdk.h>
#include <fbxsdk/fileio/fbxiosettings.h>

// Other libraries
#include "GlobalSettings.h"
#include "FBXHeaders.h"
#include "WoWModel.h"
#include "util.h" // SLASH


// Current library

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------

// Constructors
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------

void FBXAnimExporter::run()
{
  QMutexLocker locker(&m_mutex);
  ModelAnimation curAnimation = l_model->anims[animID];
  if (srcfileName.isNull() || srcfileName.isEmpty())
  {
    LOG_ERROR << "Unable to get FBX Animation Source Filename.";
    return;
  }
  
  // LOG_INFO << "FBX Animation Thead Source Filename: " << qPrintable(srcfileName);
  srcfileName = srcfileName.mid(0, srcfileName.lastIndexOf(".fbx"));
  QString srcPath = srcfileName.mid(0, srcfileName.lastIndexOf(SLASH));
  QString justfileName = srcfileName.mid(srcfileName.lastIndexOf(SLASH) + 1);
  QString anim_name = QString("%1 [%2]").arg(animationName).arg(curAnimation.Index);
  QString file_name = QString("%1_%5/%2_%3_%4.fbx").arg(srcfileName).arg(justfileName).arg(animationName).arg(curAnimation.Index).arg(wxT("Animations"));
  if (useAltNaming)
  {
    file_name = QString("%1_%5/%2_%4_%3.fbx").arg(srcfileName).arg(justfileName).arg(animationName).arg(curAnimation.Index).arg(wxT("Animations"));
  }
  LOG_INFO << "FBX Animation Filename: " << qPrintable(file_name);
  QDir dir(file_name.mid(0, file_name.lastIndexOf('/')));
  if (dir.exists() == false)
  {
    dir.mkpath(file_name.mid(0, file_name.lastIndexOf('/')));
  }
  // LOG_INFO << "FBX Animation File Path: " << qPrintable(dir.absolutePath());

  FbxManager* lSdkManager = FbxManager::Create();
  if (!lSdkManager || lSdkManager == nullptr)
  {
    LOG_ERROR << "Unable to create the FBX SDK manager for the animation exporter";
    return;
  }

  FbxIOSettings * ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
  lSdkManager->SetIOSettings(ios);

  ios->SetBoolProp(EXP_FBX_MATERIAL, false);
  ios->SetBoolProp(EXP_FBX_TEXTURE, false);
  ios->SetBoolProp(EXP_FBX_EMBEDDED, false);
  ios->SetBoolProp(EXP_FBX_SHAPE, true);
  ios->SetBoolProp(EXP_FBX_GOBO, true);
  ios->SetBoolProp(EXP_FBX_ANIMATION, true);
  ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

  fbxsdk::FbxExporter *exporter = 0;
  FbxScene *l_animscene = 0;
  if (!FBXHeaders::createFBXHeaders(l_fileVersion, file_name, lSdkManager, exporter, l_animscene))
  {
    LOG_ERROR << "Unable to create Animation Headers. Aborting Export...";
    if (lSdkManager)
      lSdkManager->Destroy();
    return;
  }
  //LOG_INFO << "Animated FBX headers were successfully created. Building scene info...";

  FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(lSdkManager, "SceneInfo");
  sceneInfo->mTitle = qPrintable(QString("%1").arg(anim_name));
  sceneInfo->mAuthor = qPrintable(QString::fromStdWString(GLOBALSETTINGS.appName()));
  sceneInfo->mRevision = qPrintable(QString::fromStdWString(GLOBALSETTINGS.appVersion()));
  l_animscene->SetSceneInfo(sceneInfo);
  //LOG_INFO << "Scene Info added to animated scene...";

  std::map<int, FbxNode*> l_boneNodes;
  FbxNode* l_skeletonNode = 0;
  FBXHeaders::createSkeleton(l_model, l_animscene, l_skeletonNode, l_boneNodes);
  //LOG_INFO << "Skeleton created for animation...";

  FbxNode* root_node = l_animscene->GetRootNode();
  root_node->AddChild(l_skeletonNode);
  //LOG_INFO << "Skeleton added to root node...";

  FBXHeaders::storeBindPose(l_animscene, l_boneClusters, l_meshNode);
  //LOG_INFO << "Skeleton successfully bound...";

  // Add this animation to our new FBX file.
  FBXHeaders::createAnimation(l_model, l_animscene, anim_name, curAnimation, l_boneNodes);
  //LOG_INFO << "Animation successfully created...";

  if (!exporter->Export(l_animscene))
  {
    LOG_ERROR << "Unable to export FBX animation scene.";
    if (lSdkManager)
      lSdkManager->Destroy();
    return;
  }
  LOG_INFO << "FBX Animation for" << qPrintable(anim_name) << "successfully exported!";
  if (lSdkManager)
    lSdkManager->Destroy();
}

void FBXAnimExporter::setValues(FbxString fileVersion, QString fn, QString an, WoWModel *m, std::vector<FbxCluster*> bc, FbxNode* &meshnode, int aID, bool uan)
{
  QMutexLocker locker(&m_mutex);
  l_fileVersion = fileVersion;
  srcfileName = fn;
  animationName = an;
  l_model = m;
  l_boneClusters = bc;
  l_meshNode = meshnode;
  animID = aID;
  useAltNaming = uan;
}
