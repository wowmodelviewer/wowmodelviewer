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
 * OBJExporter.cpp
 *
 *  Created on: 17 feb. 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#include "OBJExporter.h"

#include <cstdio>
#include <filesystem>

#include "glm/glm.hpp"

#include "Bone.h"
#include "ModelRenderPass.h"
#include "WoWModel.h"

#include "GlobalSettings.h"
#include "logger/Logger.h"

OBJExporter::OBJExporter() = default;

// Change a glm::vec4 so it now faces forwards
void MakeModelFaceForwards(glm::vec4& vect)
{
	glm::vec4 Temp;

	Temp.x = vect.x;
	Temp.y = vect.z;
	Temp.z = -vect.y;

	vect = Temp;
}

std::wstring OBJExporter::menuLabel() const
{
	return L"OBJ...";
}

std::wstring OBJExporter::fileSaveTitle() const
{
	return L"Save OBJ file";
}

std::wstring OBJExporter::fileSaveFilter() const
{
	return L"OBJ files (*.obj)|*.obj";
}

bool OBJExporter::exportModel(Model* m, std::wstring target)
{
	WoWModel* model = dynamic_cast<WoWModel*>(m);

	if (!model)
		return false;

	namespace fs = std::filesystem;

	// prepare obj file
	const fs::path targetPath(target);

	std::ofstream file(targetPath);
	if (!file.is_open())
	{
		LOG_ERROR << "Unable to open" << target;
		return false;
	}

	LOG_INFO << "Exporting" << model->modelname.c_str() << "in" << target;

	// prepare mtl file
	const fs::path matPath = targetPath.parent_path() / (targetPath.stem().wstring() + L".mtl");

	LOG_INFO << "Exporting" << model->modelname.c_str() << "materials in" << matPath.wstring();

	std::ofstream matFile(matPath);
	if (!matFile.is_open())
	{
		LOG_ERROR << "Unable to open" << matPath.wstring();
		return false;
	}

	std::ofstream& obj = file;
	std::ofstream& mtl = matFile;

	obj << "# Wavefront OBJ exported by ";
	{
		std::wstring appName = GLOBALSETTINGS.appName();
		std::wstring appVer = GLOBALSETTINGS.appVersion();
		obj << fs::path(appName).string() << " "
			<< fs::path(appVer).string() << "\n";
	}
	obj << "\n";
	obj << "mtllib " << matPath.filename().string() << "\n";
	obj << "\n";


	mtl << "#" << "\n";
	mtl << "# mtl file for " << targetPath.filename().string() << " obj file" << "\n";
	mtl << "#" << "\n";
	mtl << "\n";

	int counter = 1;

	// export main model
	if (!exportModelVertices(model, obj, counter))
	{
		LOG_ERROR << "Error during obj export for model" << model->modelname.c_str();
		return false;
	}

	if (!exportModelMaterials(model, mtl, matPath.string()))
	{
		LOG_ERROR << "Error during materials export for model" << model->modelname.c_str();
		return false;
	}

	// export equipped items
	if (!GLOBALSETTINGS.bInitPoseOnlyExport)
	{
		for (WoWModel::iterator it = model->begin();
			 it != model->end();
			 ++it)
		{
			std::map<POSITION_SLOTS, WoWModel*> itemModels = (*it)->models();
			if (!itemModels.empty())
			{
				obj << "# " << "\n";
					obj << "# " << (*it)->name() << "\n";
					obj << "# " << "\n";
				for (const auto& It : itemModels)
				{
					WoWModel* itemModel = It.second;
					LOG_INFO << "Exporting attached item" << itemModel->modelname.c_str();

					// find matrix
					const int l = model->attLookup[It.first];
					glm::mat4 M;
					glm::vec3 pos;
					if (l > -1)
					{
						M = model->bones[model->atts[l].bone].mat;
						pos = model->atts[l].pos;
					}

					if (!exportModelVertices(itemModel, obj, counter, M, pos))
					{
						LOG_ERROR << "Error during obj export for model" << itemModel->modelname.c_str();
						return false;
					}

					if (!exportModelMaterials(itemModel, mtl, matPath.string()))
					{
						LOG_ERROR << "Error during materials export for model" << itemModel->modelname.c_str();
						return false;
					}
				}
			}
		}
	}

	file.close();
	matFile.close();

	return true;
}

bool OBJExporter::exportModelVertices(WoWModel* model, std::ofstream& file, int& counter, glm::mat4 mat,
									  glm::vec3 pos) const
{
	bool vertMsg = false;
	// output all the vertice data
	int vertics = 0;
	char buf[128];
	for (size_t i = 0; i < model->passes.size(); i++)
	{
		ModelRenderPass* p = model->passes[i];

		if (p->init())
		{
			ModelGeosetHD* geoset = model->geosets[p->geoIndex];
			for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
			{
				uint32 a = model->indices[b];
				glm::vec4 vert;
				if ((model->animated == true) && (model->vertices) && !GLOBALSETTINGS.bInitPoseOnlyExport)
				{
					if (vertMsg == false)
					{
						LOG_INFO << "Using Verticies";
						vertMsg = true;
					}
					vert = mat * glm::vec4((model->vertices[a] + pos), 1.0);
				}
				else
				{
					if (vertMsg == false)
					{
						LOG_INFO << "Using Original Verticies";
						vertMsg = true;
					}
					vert = mat * glm::vec4((model->origVertices[a].pos + pos), 1.0);
				}
				MakeModelFaceForwards(vert);
				vert *= 1.0;
				snprintf(buf, sizeof(buf), "v %.06f %.06f %.06f", vert.x, vert.y, vert.z);
				file << buf << "\n";

				vertics++;
			}
		}
	}

	file << "# " << vertics << " vertices" << "\n" << "\n";
	file << "\n";
	// output all the texture coordinate data
	int textures = 0;
	for (size_t i = 0; i < model->passes.size(); i++)
	{
		ModelRenderPass* p = model->passes[i];
		// we don't want to render completely transparent parts
		if (p->init())
		{
			ModelGeosetHD* geoset = model->geosets[p->geoIndex];
			for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
			{
				uint32 a = model->indices[b];
				glm::vec2 tc = model->origVertices[a].texcoords;
				snprintf(buf, sizeof(buf), "vt %.06f %.06f", tc.x, 1 - tc.y);
				file << buf << "\n";
				textures++;
			}
		}
	}

	// output all the vertice normals data
	int normals = 0;
	for (size_t i = 0; i < model->passes.size(); i++)
	{
		ModelRenderPass* p = model->passes[i];
		if (p->init())
		{
			ModelGeosetHD* geoset = model->geosets[p->geoIndex];
			for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
			{
				uint16 a = model->indices[b];
				glm::vec3 n = model->origVertices[a].normal;
				snprintf(buf, sizeof(buf), "vn %.06f %.06f %.06f", n.x, n.y, n.z);
				file << buf << "\n";
				normals++;
			}
		}
	}

	file << "\n";
	uint32 pointnum = 0;
	// Polygon Data
	int triangles_total = 0;
	for (size_t i = 0; i < model->passes.size(); i++)
	{
		ModelRenderPass* p = model->passes[i];

		if (p->init())
		{
			ModelGeosetHD* geoset = model->geosets[p->geoIndex];
			// Build Vert2Point DB
			uint16* Vert2Point = new uint16[geoset->vstart + geoset->vcount];
			for (uint16 v = geoset->vstart; v < (geoset->vstart + geoset->vcount); v++, pointnum++)
				Vert2Point[v] = pointnum;

			int g = geoset->id;

			snprintf(buf, sizeof(buf), "Geoset_%03i", g);
			std::string val(buf);
			std::string matName = model->modelname + "_" + val;
			std::replace(matName.begin(), matName.end(), '\\', '_');
			std::string partName = matName;

			if (p->unlit == true)
				matName = matName + "_Lum";

			if (!p->cull)
				matName = matName + "_Dbl";

			// Part Names
			int mesh = g / 100;

			std::string cgGroupName = WoWModel::getCGGroupName(static_cast<CharGeosets>(mesh));

			if ((model->modelType == MT_CHAR) && (cgGroupName != ""))
				partName += "-" + cgGroupName;

			file << "g " << partName << "\n";
			file << "usemtl " << matName << "\n";
			file << "s 1" << "\n";
			int triangles = 0;
			for (size_t k = 0; k < geoset->icount; k += 3)
			{
				file << "f ";
				file << counter << "/" << counter << "/" << counter << " ";
				counter++;
				file << counter << "/" << counter << "/" << counter << " ";
				counter++;
				file << counter << "/" << counter << "/" << counter << "\n";
				counter++;
				triangles++;
			}
			file << "# " << triangles << " triangles in group" << "\n" << "\n";

			// Check for potential overflow
			if (triangles_total > UINT64_MAX - triangles)
			{
				LOG_ERROR << "Overflow detected in triangles_total!";
				return false; // Handle overflow gracefully
			}

			triangles_total += triangles;
		}
	}
	file << "# " << triangles_total << " triangles total" << "\n" << "\n";
	return true;
}

bool OBJExporter::exportModelMaterials(WoWModel* model, std::ofstream& file, std::string mtlFile) const
{
	namespace fs = std::filesystem;

	std::map<std::wstring, GLuint> texToExport;
	char buf[128];

	for (size_t i = 0; i < model->passes.size(); i++)
	{
		ModelRenderPass* p = model->passes[i];

		if (p->init())
		{
			std::string tex = model->getNameForTex(p->tex);
			std::string texStem = fs::path(tex).stem().string();
			std::string mtlStem = fs::path(mtlFile).stem().string();
			std::string texFile = mtlStem + "_" + texStem + ".png";

			float amb = 0.25f;
			glm::vec4 diff = p->ocol;

			snprintf(buf, sizeof(buf), "Geoset_%03i", model->geosets[p->geoIndex]->id);
			std::string material = model->modelname + "_" + buf;
			std::replace(material.begin(), material.end(), '\\', '_');
			if (p->unlit == true)
			{
				// Add Lum, just in case there's a non-luminous surface with the same name.
				material = material + "_Lum";
				amb = 1.0f;
				diff = glm::vec4(0, 0, 0, 0);
			}

			// If Doublesided
			if (!p->cull)
			{
				material = material + "_Dbl";
			}

			file << "newmtl " << material << "\n";
			file << "illum 2" << "\n";
			snprintf(buf, sizeof(buf), "Kd %.06f %.06f %.06f", diff.x, diff.y, diff.z);
			file << buf << "\n";
			snprintf(buf, sizeof(buf), "Ka %.06f %.06f %.06f", amb, amb, amb);
			file << buf << "\n";
			snprintf(buf, sizeof(buf), "Ks %.06f %.06f %.06f", p->ecol.x, p->ecol.y, p->ecol.z);
			file << buf << "\n";
			file << "Ke 0.000000 0.000000 0.000000" << "\n";
			snprintf(buf, sizeof(buf), "Ns %0.6f", 0.0f);
			file << buf << "\n";

			file << "map_Kd " << texFile << "\n";
			std::string fullTexPath = fs::path(mtlFile).parent_path().string() + "\\" + texFile;
			texToExport[fs::path(fullTexPath).wstring()] = model->getGLTexture(p->tex);
		}
	}

	LOG_INFO << "nb textures to export :" << texToExport.size();

	for (const auto& it : texToExport)
		exportGLTexture(it.second, it.first);

	return true;
}
