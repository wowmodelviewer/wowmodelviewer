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
#include <qthreadpool.h>

// Externals

// Other libraries
#include "FBXHeaders.h"
#include "FBXAnimExporter.h"
#include "ModelRenderPass.h"
#include "WoWModel.h"

#include "util.h" // SLASH

// Current library


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------

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

  FbxIOSettings *ios = FbxIOSettings::Create(m_p_manager, IOSROOT);
  m_p_manager->SetIOSettings(ios);

  // ensure that textures are embed in final fbx file
  ios->SetBoolProp(EXP_FBX_MATERIAL, true);
  ios->SetBoolProp(EXP_FBX_TEXTURE, true);
  ios->SetBoolProp(EXP_FBX_EMBEDDED, true);
  ios->SetBoolProp(EXP_FBX_SHAPE, true);
  ios->SetBoolProp(EXP_FBX_GOBO, true);
  ios->SetBoolProp(EXP_FBX_ANIMATION, false);
  ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

  FbxExporter* exporter = 0;

  m_fileVersion = FBX_2014_00_COMPATIBLE;

  if (FBXHeaders::createFBXHeaders(m_fileVersion, QString::fromWCharArray(m_filename.c_str()), m_p_manager, exporter, m_p_scene) == false)
    return false;

  // add some info to exported scene
  FbxDocumentInfo* sceneInfo = FbxDocumentInfo::Create(m_p_manager,"SceneInfo");
  sceneInfo->mTitle = m_p_model->name().toStdString().c_str();
  sceneInfo->mAuthor = QString::fromStdWString(GLOBALSETTINGS.appName()).toStdString().c_str();
  sceneInfo->mRevision = QString::fromStdWString(GLOBALSETTINGS.appVersion()).toStdString().c_str();
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
    FBXHeaders::storeRestPose(m_p_scene, m_p_skeletonNode);

    for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
    {
      std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
      if (!itemModels.empty())
      {
        for (std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin(); it != itemModels.end(); ++it)
        {
          if (m_attachBoneClusters.find(it->first) != m_attachBoneClusters.end() && m_attachMeshNodes.find(it->first) != m_attachMeshNodes.end())
            FBXHeaders::storeBindPose(m_p_scene, m_attachBoneClusters[it->first], m_attachMeshNodes[it->first]);
          //if (m_attachSkeletonNode.find(it->first) != m_attachSkeletonNode.end())
            //FBXHeaders::storeRestPose(m_p_scene, m_attachSkeletonNode[it->first]);
        }
      }
    }
  }
  catch(const std::exception& ex)
  {
    LOG_ERROR << "Error during export:" << ex.what();
    return false;
  }

  if(!exporter->Export(m_p_scene))
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

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------

void FBXExporter::createMeshes()
{
  m_p_meshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene ,m_p_model);

  FbxNode* root_node = m_p_scene->GetRootNode();
  root_node->AddChild(m_p_meshNode);

  for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
    if (!itemModels.empty())
    {
      for (std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin(); it != itemModels.end(); ++it)
      {
        WoWModel * itemModel = it->second;
        LOG_INFO << "Found attached item:" << itemModel->modelname.c_str();

        int l = m_p_model->attLookup[it->first];
        Matrix m;
        Vec3D pos;
        if (l > -1)
        {
          m = m_p_model->bones[m_p_model->atts[l].bone].mat;
        }

        FbxNode* itemMeshNode = FBXHeaders::createMesh(m_p_manager, m_p_scene, itemModel, m);
        m_attachMeshNodes[it->first] = itemMeshNode;

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

  for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
    if (!itemModels.empty())
    {
      for (std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin(); it != itemModels.end(); ++it)
      {
        WoWModel * itemModel = it->second;
        if (itemModel->animated == false || itemModel->bones.size() < 2)
          continue;

        FbxNode* itemSkeleton;
        std::map<int, FbxNode*> itemboneNodes;

        FBXHeaders::createSkeleton(itemModel, m_p_scene, itemSkeleton, itemboneNodes);

        m_attachSkeletonNode[it->first] = itemSkeleton;
        m_attachBoneNodes[it->first] = itemboneNodes;
        root_node->AddChild(itemSkeleton);
      }
    }
  }
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

  // define control points
  int i = 0;
  for (auto it : m_p_model->origVertices)
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
  int maxThreads = QThread::idealThreadCount();   // Get the ideal number of threads we can run at once. This usually equals the total number of threads in a CPU.
  std::map<int, std::wstring> animsMap = m_p_model->getAnimsMap();

  /**** NOTICE! ****//*

  While the QThreadPool code DOES start the FBX animation exporting in threads, the FBX SDK itself is not thread-safe!

  We will have to develop our own threaded wrapper for FBX, or copy the FBX data, or find some other method to make the SDK work in a thread-safe manner before we can re-enable multi-threaded animation exporting.
  Without doing this, WMV will crash while exporting animations, as the data becomes cross-contaminated, and thus, invalidated.

  *//****         ****/

  // If we allow users to set the number of threads WMV can use, we can limit it here. I would recommend using no more than 3/4ths of the total thread count! (Gotta leave some for normal CPU usage...)
  //int allocatedThreads = 5;
  //if (maxThreads > allocatedThreads)
  //  maxThreads = allocatedThreads;

  //LOG_INFO << "Exporting animations with" << maxThreads << "threads...";
  //QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);   // Use to limit the number of threads we can run at once.

  for (auto it : m_animsToExport)
  {
    QMutexLocker locker(&m_mutex);
    ModelAnimation curAnimation = m_p_model->anims[it];
    FBXAnimExporter *exporter = new FBXAnimExporter();
    exporter->setValues(m_fileVersion, QString::fromWCharArray(m_filename.c_str()), QString::fromWCharArray(animsMap[curAnimation.animID].c_str()), m_p_model, m_boneClusters, m_p_meshNode, it);
    exporter->setAutoDelete(true);
    exporter->run();    // Remove this line before adding to the QThreadPool!
    //QThreadPool::globalInstance()->start(exporter);   // Queue this exporter for threaded execution. Automatically starts the run() function of an FBXAnimExporter when a thread is free.
  }

  //QThreadPool::globalInstance()->waitForDone();   // Don't finish until all the threads have been processed.
  return true;
}

// Create materials.
void FBXExporter::createMaterials()
{
  for (unsigned int i = 0; i < m_p_model->passes.size(); i++)
  {
    ModelRenderPass * pass = m_p_model->passes[i];
    if (pass->init())
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

      QString tex = m_p_model->getNameForTex(pass->tex);

      QString tex_name = tex.mid(tex.lastIndexOf('/') + 1);
      tex_name = tex_name.replace(".blp", ".png");

      QString tex_fullpath_filename = QString::fromStdWString(m_filename);
      tex_fullpath_filename = tex_fullpath_filename.left(tex_fullpath_filename.lastIndexOf('\\') + 1) + tex_name;

      m_texturesToExport[tex_fullpath_filename.toStdWString()] = m_p_model->getGLTexture(pass->tex);

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

      // Add material to the scene.
      m_p_meshNode->AddMaterial(material);
    }
  }

  for (WoWModel::iterator it = m_p_model->begin(); it != m_p_model->end(); ++it)
  {
    std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->models();
    if (!itemModels.empty())
    {
      for (std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin(); it != itemModels.end(); ++it)
      {
        WoWModel* model = it->second;
        for (unsigned int i = 0; i < model->passes.size(); i++)
        {
          ModelRenderPass * pass = model->passes[i];
          if (pass->init())
          {
            // Build material name.
            FbxString mtrl_name = model->name().toStdString().c_str();
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

            QString tex_fullpath_filename = QString::fromStdWString(m_filename);
            tex_fullpath_filename = tex_fullpath_filename.left(tex_fullpath_filename.lastIndexOf('\\') + 1) + tex_name;

            m_texturesToExport[tex_fullpath_filename.toStdWString()] = model->getGLTexture(pass->tex);

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

            // Add material to the scene.
            m_attachMeshNodes[it->first]->AddMaterial(material);
          }
        }
      }
    }
  }

  for(auto it : m_texturesToExport)
    exportGLTexture(it.second, it.first);
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
  m_boneClusters.clear();
}
