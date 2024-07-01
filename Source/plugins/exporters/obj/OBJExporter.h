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
 * OBJExporter.h
 *
 *  Created on: 17 feb. 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <QtPlugin>

// Externals
class WoWModel;

// Other libraries
#include "glm/glm.hpp"

#define _EXPORTERPLUGIN_CPP_ // to define interface
#include "ExporterPlugin.h"
#undef _EXPORTERPLUGIN_CPP_

class OBJExporter : public ExporterPlugin
{
	Q_INTERFACES(ExporterPlugin)
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "wowmodelviewer.exporters.OBJExporter" FILE "objexporter.json")

public:
	OBJExporter()
	{
	}

	~OBJExporter()
	{
	}

	std::wstring menuLabel() const;
	std::wstring fileSaveTitle() const;
	std::wstring fileSaveFilter() const;

	bool exportModel(Model*, std::wstring file);

private:
	bool exportModelVertices(WoWModel* model, QTextStream& file, int& counter, glm::mat4 m = glm::mat4(1.0),
	                         glm::vec3 pos = glm::vec3(0.0f)) const;
	bool exportModelMaterials(WoWModel* model, QTextStream& file, QString mtlFile) const;
};
