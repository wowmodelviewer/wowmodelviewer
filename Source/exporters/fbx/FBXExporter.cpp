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

#include "FBXExporter.h"

#include <filesystem>
#include <mutex>
#include <QString>

#include "FBXHeaders.h"
#include "FBXAnimExporter.h"
#include "ModelRenderPass.h"
#include "WoWModel.h"

#include "util.h" // SLASH
#include "GlobalSettings.h"

FBXExporter::FBXExporter() : m_p_manager(nullptr), m_p_scene(nullptr), m_p_model(nullptr),
							 m_p_meshNode(nullptr), m_p_skeletonNode(nullptr)
{
	m_canExportAnimation = true;
}

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

bool FBXExporter::exportModel(Model* model, std::wstring target)
{
	reset();

	m_p_model = dynamic_cast<WoWModel*>(model);

	if (!m_p_model)
		return false;

	m_filename = target;
	m_p_manager = FbxManager::Create();
	if (!m_p_manager)
	{
		LOG_ERROR << "Unable to create the FBX SDK manager";
		return false;
	}

	FbxIOSettings* ios = FbxIOSettings::Create(m_p_manager, IOSROOT);
	m_p_manager->SetIOSettings(ios);

	// ensure that textures are embed in final fbx file
	ios->SetBoolProp(EXP_FBX_MATERIAL, true);
	ios->SetBoolProp(EXP_FBX_TEXTURE, true);
	ios->SetBoolProp(EXP_FBX_EMBEDDED, true);
	ios->SetBoolProp(EXP_FBX_SHAPE, true);
	ios->SetBoolProp(EXP_FBX_GOBO, true);
	ios->SetBoolProp(EXP_FBX_ANIMATION, false);
	ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

	FbxExporter* exporter = nullptr;

	m_fileVersion = FBX_2014_00_COMPATIBLE;

	if (FBXHeaders::createFBXHeaders(m_fileVersion, std::string(m_filename.begin(), m_filename.end()), m_p_manager, exporter,
									 m_p_scene) == false)
		return false;

	// add some info to exported scene
	FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(m_p_manager, "SceneInfo");
	{
		std::string modelName = m_p_model->name();
		auto sp = modelName.rfind('/');
		if (sp != std::string::npos)
			modelName = modelName.substr(sp + 1);
		sceneInfo->mTitle = modelName.c_str();
	}
	{
		std::wstring appName = GLOBALSETTINGS.appName();
		sceneInfo->mAuthor = std::string(appName.begin(), appName.end()).c_str();
	}
	{
		std::wstring appVer = GLOBALSETTINGS.appVersion();
		sceneInfo->mRevision = std::string(appVer.begin(), appVer.end()).c_str();
	}
	m_p_scene->SetSceneInfo(sceneInfo);

	// export main model mesh
	// follow FBX SDK example (ExportScene01) for organization
	try
	{
		createMeshes();
		LOG_INFO << "Meshes successfully created";

		createMaterials();
		LOG_INFO << "Materials successfully created";

		createSkeletons();
		LOG_INFO << "Skeleton successfully created";

		linkMeshAndSkeleton();

		FBXHeaders::storeBindPose(m_p_scene, m_boneClusters, m_p_meshNode);
		//FBXHeaders::storeRestPose(m_p_scene, m_p_skeletonNode);

		for (const auto it : *m_p_model)
		{
			std::map<POSITION_SLOTS, WoWModel*> itemModels = it->models();
			if (!itemModels.empty())
			{
				for (const auto& itemModel : itemModels)
				{
					if (m_attachBoneClusters.find(itemModel.first) != m_attachBoneClusters.end() && m_attachMeshNodes.
						find(itemModel.first) != m_attachMeshNodes.end())
						FBXHeaders::storeBindPose(m_p_scene, m_attachBoneClusters[itemModel.first],
						                          m_attachMeshNodes[itemModel.first]);
					//if (m_attachSkeletonNode.find(it->first) != m_attachSkeletonNode.end())
					//FBXHeaders::storeRestPose(m_p_scene, m_attachSkeletonNode[it->first]);
				}
			}
		}
	}
	catch (const std::exception& ex)
	{
		LOG_ERROR << "Error during export:" << ex.what();
		return false;
	}

	if (!exporter->Export(m_p_scene))
	{
		LOG_ERROR << "Unable to export FBX scene";
		return false;
	}
	LOG_INFO << "Model successfully created";

	// delete texture files created during export
	for (auto it : m_texturesToExport)
		_wremove((it.first).c_str());

	// Export bulk animations
	if (m_animsToExport.size() > 0)
	{
		try
		{
			createAnimations();
			LOG_INFO << "Animations successfully created";
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR << "Error during bulk animation export:" << ex.what();
			return false;
		}
	}

	LOG_INFO << "FBX scene successfully exported";
	return true;
}

void FBXExporter::createMeshes()
{
	m_p_meshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene, m_p_model);

	FbxNode* root_node = m_p_scene->GetRootNode();
	root_node->AddChild(m_p_meshNode);

	for (const auto it : *m_p_model)
	{
		std::map<POSITION_SLOTS, WoWModel*> itemModels = it->models();
		if (!itemModels.empty())
		{
			for (const auto& It : itemModels)
			{
				WoWModel* itemModel = It.second;
				LOG_INFO << "Found attached item:" << itemModel->modelname.c_str();

				const int l = m_p_model->attLookup[It.first];
				glm::mat4 m;
				if (l > -1)
				{
					m = m_p_model->bones[m_p_model->atts[l].bone].mat;
				}

				FbxNode* itemMeshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene, itemModel, m);
				m_attachMeshNodes[It.first] = itemMeshNode;

				root_node->AddChild(itemMeshNode);
			}
		}
	}
}

void FBXExporter::createSkeletons()
{
	FBXHeaders::createSkeleton(m_p_model, m_p_scene, m_p_skeletonNode, m_boneNodes);

	FbxNode* root_node = m_p_scene->GetRootNode();
	root_node->AddChild(m_p_skeletonNode);

	for (const auto it : *m_p_model)
	{
		std::map<POSITION_SLOTS, WoWModel*> itemModels = it->models();
		if (!itemModels.empty())
		{
			for (const auto& It : itemModels)
			{
				WoWModel* itemModel = It.second;
				if (itemModel->animated == false || itemModel->bones.size() < 2)
					continue;

				FbxNode* itemSkeleton;
				std::map<int, FbxNode*> itemboneNodes;

				FBXHeaders::createSkeleton(itemModel, m_p_scene, itemSkeleton, itemboneNodes);

				m_attachSkeletonNode[It.first] = itemSkeleton;
				m_attachBoneNodes[It.first] = itemboneNodes;
				root_node->AddChild(itemSkeleton);
			}
		}
	}
}

void FBXExporter::linkMeshAndSkeleton()
{
	// create clusters
	for (const auto it : m_boneNodes)
	{
		FbxCluster* cluster = FbxCluster::Create(m_p_scene, "");
		m_boneClusters.push_back(cluster);
		cluster->SetLink(it.second);
		cluster->SetLinkMode(FbxCluster::ELinkMode::eNormalize);
	}

	// define control points
	int i = 0;
	for (const auto& it : m_p_model->origVertices)
	{
		for (size_t j = 0; j < 4; j++)
		{
			if (it.weights[j] > 0)
				m_boneClusters[it.bones[j]]->AddControlPointIndex((int)i, static_cast<double>(it.weights[j]) / 255.0);
		}
		i++;
	}

	// set initial matrices
	FbxAMatrix matrix = m_p_meshNode->EvaluateGlobalTransform();
	for (const auto it : m_boneClusters)
	{
		it->SetTransformMatrix(matrix);
	}

	// set link matrices
	std::vector<FbxCluster*>::iterator clusterIt = m_boneClusters.begin();
	for (const auto it : m_boneNodes)
	{
		matrix = it.second->EvaluateGlobalTransform();
		(*clusterIt)->SetTransformLinkMatrix(matrix);
		++clusterIt;
	}

	// add cluster to skin
	FbxGeometry* lMeshAttribute = static_cast<FbxGeometry*>(m_p_meshNode->GetNodeAttribute());
	FbxSkin* skin = FbxSkin::Create(m_p_scene, "");

	for (const auto it : m_boneClusters)
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

	LOG_INFO << "Num animations to export:" << m_animsToExport.size();

	if (!createAnimationFiles())
	{
		LOG_ERROR << "An error occured while exporting Animation files.";
		return;
	}
}

// Creates separate FBX files for each animation, in a folder with the original FBX file's name.
bool FBXExporter::createAnimationFiles()
{
	//int maxThreads = QThread::idealThreadCount();
	// Get the ideal number of threads we can run at once. This usually equals the total number of threads in a CPU.
	std::map<int, std::wstring> animsMap = m_p_model->getAnimsMap();

	/**** NOTICE! ****//*

  While the QThreadPool code DOES start the FBX animation exporting in threads, the FBX SDK itself is not thread-safe!

  We will have to develop our own threaded wrapper for FBX, or copy the FBX data, or find some other method to make the SDK work in a thread-safe manner before we can re-enable multi-threaded animation exporting.
  Without doing this, WMV will crash while exporting animations, as the data becomes cross-contaminated, and thus, invalidated.

  */ /****         ****/

	// If we allow users to set the number of threads WMV can use, we can limit it here. I would recommend using no more than 3/4ths of the total thread count! (Gotta leave some for normal CPU usage...)
	//int allocatedThreads = 5;
	//if (maxThreads > allocatedThreads)
	//  maxThreads = allocatedThreads;

	//LOG_INFO << "Exporting animations with" << maxThreads << "threads...";
	//QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);   // Use to limit the number of threads we can run at once.

	for (const auto it : m_animsToExport)
	{
		std::lock_guard<std::mutex> locker(m_mutex);
		const ModelAnimation curAnimation = m_p_model->anims[it];
		FBXAnimExporter* exporter = new FBXAnimExporter();
		exporter->setValues(m_fileVersion, std::string(m_filename.begin(), m_filename.end()),
							std::string(animsMap[curAnimation.animID].begin(), animsMap[curAnimation.animID].end()), m_p_model, m_boneClusters,
							m_p_meshNode, it);
		exporter->run();
		delete exporter;
	}

	//QThreadPool::globalInstance()->waitForDone();   // Don't finish until all the threads have been processed.
	return true;
}

// Create materials.
void FBXExporter::createMaterials()
{
	for (unsigned int i = 0; i < m_p_model->passes.size(); i++)
	{
		ModelRenderPass* pass = m_p_model->passes[i];
		if (pass->init())
		{
			// Build material name.
			std::string mn = m_p_model->name();
			auto sp = mn.rfind('/');
			if (sp != std::string::npos)
				mn = mn.substr(sp + 1);
			FbxString mtrl_name = mn.c_str();
			mtrl_name.Append("_", 1);
			char tmp[32];
			_itoa(static_cast<int>(i), tmp, 10);
			mtrl_name.Append(tmp, strlen(tmp));

			// Create material.
			FbxString shading_name = "Phong";
			FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
			material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

			std::string tex = m_p_model->getNameForTex(pass->tex);

			std::string tex_name = tex;
			auto slashPos = tex_name.rfind('/');
			if (slashPos != std::string::npos)
				tex_name = tex_name.substr(slashPos + 1);
			if (auto blpPos = tex_name.find(".blp"); blpPos != std::string::npos)
				tex_name.replace(blpPos, 4, ".png");

			std::string filenameNarrow(m_filename.begin(), m_filename.end());
			std::string tex_fullpath = filenameNarrow;
			auto bsPos = tex_fullpath.rfind('\\');
			if (bsPos != std::string::npos)
				tex_fullpath = tex_fullpath.substr(0, bsPos + 1) + tex_name;

			m_texturesToExport[std::wstring(tex_fullpath.begin(), tex_fullpath.end())] = m_p_model->getGLTexture(pass->tex);

			FbxFileTexture* texture = FbxFileTexture::Create(m_p_manager, tex_name.c_str());
			texture->SetFileName(tex_fullpath.c_str());
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

	for (const auto it : *m_p_model)
	{
		std::map<POSITION_SLOTS, WoWModel*> itemModels = it->models();
		if (!itemModels.empty())
		{
			for (const auto& itemModel : itemModels)
			{
				WoWModel* model = itemModel.second;
				for (unsigned int i = 0; i < model->passes.size(); i++)
				{
					ModelRenderPass* pass = model->passes[i];
					if (pass->init())
						{
							// Build material name.
							std::string mn2 = model->name();
							auto sp2 = mn2.rfind('/');
							if (sp2 != std::string::npos)
								mn2 = mn2.substr(sp2 + 1);
							FbxString mtrl_name = mn2.c_str();
							mtrl_name.Append("_", 1);
							char tmp[32];
							_itoa(static_cast<int>(i), tmp, 10);
							mtrl_name.Append(tmp, strlen(tmp));

							// Create material.
							FbxString shading_name = "Phong";
							FbxSurfacePhong* material = FbxSurfacePhong::Create(m_p_manager, mtrl_name.Buffer());
							material->Ambient.Set(FbxDouble3(0.7, 0.7, 0.7));

							std::string tex = model->getNameForTex(pass->tex);

							std::string tex_name = tex;
							auto slashPos2 = tex_name.rfind('/');
							if (slashPos2 != std::string::npos)
								tex_name = tex_name.substr(slashPos2 + 1);
							if (auto blpPos = tex_name.find(".blp"); blpPos != std::string::npos)
								tex_name.replace(blpPos, 4, ".png");

							std::string filenameNarrow2(m_filename.begin(), m_filename.end());
							std::string tex_fullpath2 = filenameNarrow2;
							auto bsPos2 = tex_fullpath2.rfind('\\');
							if (bsPos2 != std::string::npos)
								tex_fullpath2 = tex_fullpath2.substr(0, bsPos2 + 1) + tex_name;

							m_texturesToExport[std::wstring(tex_fullpath2.begin(), tex_fullpath2.end())] = model->getGLTexture(pass->tex);

							FbxFileTexture* texture = FbxFileTexture::Create(m_p_manager, tex_name.c_str());
							texture->SetFileName(tex_fullpath2.c_str());
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
						m_attachMeshNodes[itemModel.first]->AddMaterial(material);
					}
				}
			}
		}
	}

	for (const auto it : m_texturesToExport)
		exportGLTexture(it.second, it.first);
}

void FBXExporter::reset()
{
	if (m_p_manager)
		m_p_manager->Destroy();

	m_p_manager = nullptr;

	// scene is destroyed by manager's destroy call
	m_p_scene = nullptr;

	m_p_model = nullptr;
	m_p_meshNode = nullptr;
	m_p_skeletonNode = nullptr;

	m_filename = L"";

	m_boneNodes.clear();
	m_texturesToExport.clear();
	m_boneClusters.clear();
}
