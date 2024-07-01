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
 * FBXAnimExporter.h
 *
 *  Created on: 14 may 2019
 *   Copyright: 2019 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <vector>

#include <QString>
#include <QRunnable>
#include <QMutex>

#include "fbxsdk.h"

class WoWModel;
struct ModelAnimation;

class FBXAnimExporter : public QRunnable
{
public:
	void run() override;
	void setValues(FbxString fileVersion, QString fn, QString an, WoWModel* m, std::vector<FbxCluster*> bc,
	               FbxNode* & meshnode, int aID, bool uan = false);

private:
	FbxString l_fileVersion;
	QString srcfileName;
	QString animationName;
	WoWModel* l_model;
	std::vector<FbxCluster*> l_boneClusters;
	FbxNode* l_meshNode;
	int animID;
	bool useAltNaming = false;
	mutable QMutex m_mutex;
};
