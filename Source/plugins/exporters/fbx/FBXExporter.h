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
 * FBXExporter.h
 *
 *  Created on: 13 june 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <map>
#include <string>

#include <QtPlugin>
#include <qlist.h>
#include <qmutex.h>

#include "fbxsdk.h"

class WoWModel;
struct ModelAnimation;

#define _EXPORTERPLUGIN_CPP_ // to define interface
#include "ExporterPlugin.h"
#undef _EXPORTERPLUGIN_CPP_

class FBXExporter : public ExporterPlugin
{
	Q_INTERFACES(ExporterPlugin)
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "wowmodelviewer.exporters.FBXExporter" FILE "fbxexporter.json")

public:
	FBXExporter();

	~FBXExporter()
	{
	}

	std::wstring menuLabel() const;
	std::wstring fileSaveTitle() const;
	std::wstring fileSaveFilter() const;

	bool exportModel(Model*, std::wstring file);

private:
	void createMaterials();
	void createMeshes();
	void createSkeletons();
	void linkMeshAndSkeleton();
	void createAnimations();
	bool createAnimationFiles();
	void reset();

	FbxManager* m_p_manager;
	FbxScene* m_p_scene;
	WoWModel* m_p_model;
	FbxNode* m_p_meshNode;
	FbxNode* m_p_skeletonNode;
	QList<WoWModel*> m_p_attachedModels;

	mutable QMutex m_mutex;
	bool useAltAnimNaming = false;
	FbxString m_fileVersion;
	std::wstring m_filename;
	std::map<int, FbxNode*> m_boneNodes;
	std::vector<FbxCluster*> m_boneClusters;

	std::map<int, FbxNode*> m_attachSkeletonNode;
	std::map<int, FbxNode*> m_attachMeshNodes;
	std::map<int, std::map<int, FbxNode*>> m_attachBoneNodes;
	std::map<int, std::vector<FbxCluster*>> m_attachBoneClusters;

	std::map<std::wstring, GLuint> m_texturesToExport;
};
