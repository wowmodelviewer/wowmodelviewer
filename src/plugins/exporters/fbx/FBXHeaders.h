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
 * FBXHeaders.h
 *
 *  Created on: 14 may 2019
 *   Copyright: 2019 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _FBXHEADERS_H_
#define _FBXHEADERS_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <qmutex.h>

// Externals
#include "fbxsdk.h"

// Other libraries
#include "logger/Logger.h"
#include "WoWModel.h"

// Current library


// Beginning of implementation
//--------------------------------------------------------------------
#define SCALE_FACTOR 50.0f

// Namespaces used
//--------------------------------------------------------------------


// Functions
//--------------------------------------------------------------------

namespace FBXHeaders
{
  bool createFBXHeaders(FbxString fileVersion, QString l_FileName, FbxManager* &l_Manager, FbxExporter* &l_Exporter, FbxScene* &l_Scene);
  FbxNode* createMesh(FbxManager* &l_manager, FbxScene* &l_scene, WoWModel* model, Matrix matix = Matrix::identity(), Vec3D offset = Vec3D());
  void createSkeleton(WoWModel* l_model, FbxScene* &l_scene, FbxNode* &l_skeletonNode, std::map<int, FbxNode*> &l_boneNodes);
  void storeBindPose(FbxScene* &l_scene, std::vector<FbxCluster*> l_boneClusters, FbxNode* l_meshNode);
  void storeRestPose(FbxScene* &l_scene, FbxNode* &l_SkeletonRoot);
  void createAnimation(WoWModel *l_model, FbxScene *& l_scene, QString animName, ModelAnimation cur_anim, std::map<int, FbxNode*>& skeleton);
}

// static members definition
#ifdef _FBXHEADERS_CPP_

#endif

#endif /* _FBXHEADERS_H_ */
