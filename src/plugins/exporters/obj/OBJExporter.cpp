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

#define _OBJEXPORTER_CPP_
#include "OBJExporter.h"
#undef _OBJEXPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QFileInfo>
#include <QImage>

// Externals

// Other libraries
#include "Bone.h"
#include "WoWModel.h"

#include "GlobalSettings.h"
#include "logger/Logger.h"

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
// Change a Vec3D so it now faces forwards
void MakeModelFaceForwards(Vec3D &vect)
{
  Vec3D Temp;

  Temp.x = 0-vect.z;
  Temp.y = vect.y;
  Temp.z = vect.x;

  vect = Temp;
}

std::string OBJExporter::menuLabel() const
{
  return "OBJ...";
}

std::string OBJExporter::fileSaveTitle() const
{
  return "Save OBJ file";
}

std::string OBJExporter::fileSaveFilter() const
{
  return "OBJ files (*.obj)|*.obj";
}


bool OBJExporter::exportModel(Model * m, std::string target)
{
  WoWModel * model = dynamic_cast<WoWModel *>(m);

  if(!model)
    return false;

  // prepare obj file
  QString targetFile = QString::fromStdString(target);

  QFile file(targetFile);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    LOG_ERROR << "Unable to open" << targetFile;
    return false;
  }

  LOG_INFO << "Exporting" << model->modelname.c_str() << "in" << targetFile;

  // prepare mtl file
  QString matFilename = QFileInfo(target.c_str()).completeBaseName();
  matFilename += ".mtl";
  matFilename = QFileInfo(target.c_str()).absolutePath () + "/" + matFilename;

  LOG_INFO << "Exporting" << model->modelname.c_str() << "materials in" << matFilename;

  QFile matFile(matFilename);
  if (!matFile.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    LOG_ERROR << "Unable to open" << matFilename;
    return false;
  }

  QTextStream obj(&file);
  QTextStream mtl(&matFile);

  obj << "# Wavefront OBJ exported by " << QString::fromStdString(GLOBALSETTINGS.appName()) << " " << QString::fromStdString(GLOBALSETTINGS.appVersion()) << "\n";
  obj << "\n";
  obj << "mtllib " <<  QFileInfo(matFile).fileName() << "\n";
  obj << "\n";


  mtl << "#" << "\n";
  mtl << "# mtl file for " << QFileInfo(targetFile).fileName() << " obj file" << "\n";
  mtl << "#" << "\n";
  mtl << "\n";

  int counter=1;

  // export main model
  if(!exportModelVertices(model, obj, counter))
  {
    LOG_ERROR << "Error during obj export for model" << model->modelname.c_str();
    return false;
  }

  if(!exportModelMaterials(model, mtl, matFilename))
  {
    LOG_ERROR << "Error during materials export for model" << model->modelname.c_str();
    return false;
  }

  // export equipped items
  if(!GLOBALSETTINGS.bInitPoseOnlyExport)
  {

    for(WoWModel::iterator it = model->begin();
        it != model->end();
        ++it)
    {
      std::map<POSITION_SLOTS, WoWModel *> itemModels = (*it)->itemModels;
      if(!itemModels.empty())
      {
        obj << "# " << "\n";
        obj << "# " << (*it)->name() << "\n";
        obj << "# " << "\n";
        for(std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin() ;
            it != itemModels.end();
            ++it)
        {
          WoWModel * itemModel = it->second;
          LOG_INFO << "Exporting attached item" << itemModel->modelname.c_str();

          // find matrix
          int l = model->attLookup[it->first];
          Matrix m;
          Vec3D pos;
          if (l>-1)
          {
            m = model->bones[model->atts[l].bone].mat;
            pos = model->atts[l].pos;
          }

          if(!exportModelVertices(itemModel, obj, counter, m, pos))
          {
            LOG_ERROR << "Error during obj export for model" << itemModel->modelname.c_str();
            return false;
          }

          if(!exportModelMaterials(itemModel, mtl, matFilename))
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

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
bool OBJExporter::exportModelVertices(WoWModel * model, QTextStream & file, int & counter, Matrix mat, Vec3D pos) const
{
  //@TODO : do better than that
  QString meshes[NUM_GEOSETS] =
     {"Hairstyles", "Facial1", "Facial2", "Facial3", "Braces",
      "Boots", "", "Ears", "Wristbands",  "Kneepads",
      "Pants", "Pants2", "Tarbard", "Trousers", "Tarbard2",
      "Cape", "Feet", "Eyeglows", "Belt", "Tail" };

  bool vertMsg = false;
  // output all the vertice data
  int vertics = 0;
  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];

    if (p.init(model))
    {
      for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++)
      {
        uint16 a = model->indices[b];
        Vec3D vert;
        if ((model->animated == true) && (model->vertices) && !GLOBALSETTINGS.bInitPoseOnlyExport)
        {
          if (vertMsg == false)
          {
            LOG_INFO << "Using Verticies";
            vertMsg = true;
          }
          vert = mat * (model->vertices[a] + pos);
        }
        else
        {
          if (vertMsg == false)
          {
            LOG_INFO << "Using Original Verticies";
            vertMsg = true;
          }
          vert = mat * (model->origVertices[a].pos + pos);
        }
        MakeModelFaceForwards(vert);
        vert *= 1.0;
        QString val;
        val.sprintf("v %.06f %.06f %.06f",vert.x, vert.y, vert.z);
        file << val << "\n";

        vertics ++;
      }
    }
  }

  file << "# " << vertics << " vertices" << "\n" << "\n";
  file << "\n";
  // output all the texture coordinate data
  int textures = 0;
  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];
    // we don't want to render completely transparent parts
    if (p.init(model))
    {
      for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++)
      {
        uint16 a = model->indices[b];
        Vec2D tc =  model->origVertices[a].texcoords;
        QString val;
        val.sprintf("vt %.06f %.06f", tc.x, 1-tc.y);
        file << val << "\n";
        textures ++;
      }
    }
  }

  // output all the vertice normals data
  int normals = 0;
  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];
    if (p.init(model))
    {
      for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++)
      {
        uint16 a = model->indices[b];
        Vec3D n = model->origVertices[a].normal;
        QString val;
        val.sprintf("vn %.06f %.06f %.06f", n.x, n.y, n.z);
        file << val << "\n";
        normals ++;
      }
    }
  }

  file << "\n";
  uint32 pointnum = 0;
  // Polygon Data
  int triangles_total = 0;
  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];

    if (p.init(model))
    {
      // Build Vert2Point DB
      size_t *Vert2Point = new size_t[p.vertexEnd];
      for (size_t v=p.vertexStart; v<p.vertexEnd; v++, pointnum++)
        Vert2Point[v] = pointnum;

      int g = p.geoset;

      QString val;
      val.sprintf("Geoset_%03i",g);
      QString matName = QString(model->modelname.c_str()) + "_" + val;
      matName.replace("\\","_");
      QString partName = matName;

      if (p.unlit == true)
        matName = matName + "_Lum";

      if (!p.cull)
        matName = matName + "_Dbl";

      // Part Names
      int mesh = model->geosets[g].id / 100;


      if (model->modelType == MT_CHAR && mesh < 19 && meshes[mesh] != "")
      {
        QString msh = meshes[mesh];
        msh.replace(" ", "_");

        partName += QString("-%1").arg(msh);
      }

      file << "g " << partName << "\n";
      file << "usemtl " << matName << "\n";
      file << "s 1" << "\n";
      int triangles = 0;
      for (size_t k=0; k<p.indexCount; k+=3)
      {
        file << "f ";
        file << QString("%1/%1/%1 ").arg(counter);
        counter ++;
        file << QString("%1/%1/%1 ").arg(counter);
        counter ++;
        file << QString("%1/%1/%1\n").arg(counter);
        counter ++;
        triangles ++;
      }
      file << "# " << triangles << " triangles in group" << "\n" << "\n";
      triangles_total += triangles;
    }
  }
  file << "# " << triangles_total << " triangles total" << "\n" << "\n";
  return true;
}

bool OBJExporter::exportModelMaterials(WoWModel * model, QTextStream & file, QString mtlFile) const
{
  std::map<std::string, std::string> texToExport;

  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];

    if (p.init(model))
    {
      QString tex = model->TextureList[p.tex]->fullname();
      QString texfile = QFileInfo(tex).completeBaseName();
      tex = QFileInfo(mtlFile).completeBaseName() + "_" + texfile + ".png";

      float amb = 0.25f;
      Vec4D diff = p.ocol;

      QString val;
      val.sprintf("Geoset_%03i",p.geoset);
      QString material = QString(model->modelname.c_str()) + "_" + val;
      material.replace("\\","_");
      if (p.unlit == true)
      {
        // Add Lum, just in case there's a non-luminous surface with the same name.
        material = material + "_Lum";
        amb = 1.0f;
        diff = Vec4D(0,0,0,0);
      }

      // If Doublesided
      if (!p.cull)
      {
        material = material + "_Dbl";
      }

      file << "newmtl " << material << "\n";
      file << "illum 2" << "\n";
      val.sprintf("Kd %.06f %.06f %.06f", diff.x, diff.y, diff.z);
      file << val << "\n";
      val.sprintf("Ka %.06f %.06f %.06f", amb, amb, amb);
      file << val << "\n";
      val.sprintf("Ks %.06f %.06f %.06f", p.ecol.x, p.ecol.y, p.ecol.z);
      file << val << "\n";
      file << "Ke 0.000000 0.000000 0.000000" << "\n";
      val.sprintf("Ns %0.6f", 0.0f);
      file << val << "\n";

      file << "map_Kd " << tex << "\n";
      tex = QFileInfo(mtlFile).absolutePath() + "\\" + tex;
      texToExport[tex.toStdString()] = model->TextureList[p.tex]->fullname().toStdString();
    }
  }

  LOG_INFO << "nb textures to export :" << texToExport.size();

  for(std::map<std::string, std::string>::iterator it = texToExport.begin();
      it != texToExport.end();
      ++it)
  {
    if(it->second.find("Body") != std::string::npos)
    {
      exportGLTexture(model->replaceTextures[TEXTURE_BODY], it->first);
    }
    else
    {
      GLuint texID = texturemanager.get(it->second.c_str());
      exportGLTexture(texID, it->first);
    }
  }

  return true;
}
