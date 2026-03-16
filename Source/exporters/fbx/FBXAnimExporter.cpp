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

#include "FBXAnimExporter.h"
#include <filesystem>
#include <mutex>
#include <sstream>
#include <fbxsdk.h>
#include <fbxsdk/fileio/fbxiosettings.h>
#include "GlobalSettings.h"
#include "FBXHeaders.h"
#include "WoWModel.h"

void FBXAnimExporter::run()
{
	std::lock_guard<std::mutex> locker(m_mutex);
	const ModelAnimation curAnimation = l_model->anims[animID];
	if (srcfileName.empty())
	{
		LOG_ERROR << "Unable to get FBX Animation Source Filename.";
		return;
	}

	// Remove .fbx extension
	auto fbxPos = srcfileName.rfind(".fbx");
	if (fbxPos != std::string::npos)
		srcfileName = srcfileName.substr(0, fbxPos);

	std::string justfileName = srcfileName;
	auto slashPos = justfileName.rfind('\\');
	if (slashPos == std::string::npos)
		slashPos = justfileName.rfind('/');
	if (slashPos != std::string::npos)
		justfileName = justfileName.substr(slashPos + 1);

	std::string anim_name = animationName + " [" + std::to_string(curAnimation.Index) + "]";
	std::string file_name;
	if (useAltNaming)
	{
		file_name = srcfileName + "_Animations/" + justfileName + "_" +
					std::to_string(curAnimation.Index) + "_" + animationName + ".fbx";
	}
	else
	{
		file_name = srcfileName + "_Animations/" + justfileName + "_" +
					animationName + "_" + std::to_string(curAnimation.Index) + ".fbx";
	}
	LOG_INFO << "FBX Animation Filename: " << file_name.c_str();
	auto lastSlash = file_name.rfind('/');
	if (lastSlash != std::string::npos)
	{
		std::string dirPath = file_name.substr(0, lastSlash);
		std::filesystem::create_directories(dirPath);
	}

	FbxManager* lSdkManager = FbxManager::Create();
	if (!lSdkManager)
	{
		LOG_ERROR << "Unable to create the FBX SDK manager for the animation exporter";
		return;
	}

	FbxIOSettings* ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
	lSdkManager->SetIOSettings(ios);

	ios->SetBoolProp(EXP_FBX_MATERIAL, false);
	ios->SetBoolProp(EXP_FBX_TEXTURE, false);
	ios->SetBoolProp(EXP_FBX_EMBEDDED, false);
	ios->SetBoolProp(EXP_FBX_SHAPE, true);
	ios->SetBoolProp(EXP_FBX_GOBO, true);
	ios->SetBoolProp(EXP_FBX_ANIMATION, true);
	ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

	fbxsdk::FbxExporter* exporter = nullptr;
	FbxScene* l_animscene = nullptr;
	if (!FBXHeaders::createFBXHeaders(l_fileVersion, file_name, lSdkManager, exporter, l_animscene))
	{
		LOG_ERROR << "Unable to create Animation Headers. Aborting Export...";
		if (lSdkManager)
			lSdkManager->Destroy();
		return;
	}

	FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(lSdkManager, "SceneInfo");
	sceneInfo->mTitle = anim_name.c_str();
	{
		std::wstring appName = GLOBALSETTINGS.appName();
		std::string appNameUtf8(appName.begin(), appName.end());
		sceneInfo->mAuthor = appNameUtf8.c_str();
	}
	{
		std::wstring appVer = GLOBALSETTINGS.appVersion();
		std::string appVerUtf8(appVer.begin(), appVer.end());
		sceneInfo->mRevision = appVerUtf8.c_str();
	}
	l_animscene->SetSceneInfo(sceneInfo);

	std::map<int, FbxNode*> l_boneNodes;
	FbxNode* l_skeletonNode = nullptr;
	FBXHeaders::createSkeleton(l_model, l_animscene, l_skeletonNode, l_boneNodes);

	FbxNode* root_node = l_animscene->GetRootNode();
	root_node->AddChild(l_skeletonNode);

	FBXHeaders::storeBindPose(l_animscene, l_boneClusters, l_meshNode);

	// Add this animation to our new FBX file.
	FBXHeaders::createAnimation(l_model, l_animscene, anim_name, curAnimation, l_boneNodes);

	if (!exporter->Export(l_animscene))
	{
		LOG_ERROR << "Unable to export FBX animation scene.";
		if (lSdkManager)
			lSdkManager->Destroy();
		return;
	}
	LOG_INFO << "FBX Animation for" << anim_name.c_str() << "successfully exported!";
	if (lSdkManager)
		lSdkManager->Destroy();
}

void FBXAnimExporter::setValues(FbxString fileVersion, std::string fn, std::string an, WoWModel* m, std::vector<FbxCluster*> bc,
								FbxNode* & meshnode, int aID, bool uan)
{
	std::lock_guard<std::mutex> locker(m_mutex);
	l_fileVersion = fileVersion;
	srcfileName = std::move(fn);
	animationName = std::move(an);
	l_model = m;
	l_boneClusters = bc;
	l_meshNode = meshnode;
	animID = aID;
	useAltNaming = uan;
}
