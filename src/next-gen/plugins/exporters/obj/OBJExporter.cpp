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

// Externals

// Other libraries
#include "WoWModel.h"

#include "core/GlobalSettings.h"
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
void MakeModelFaceForwards(Vec3D &vect, bool flipZ = false){
  Vec3D Temp;

  Temp.x = 0-vect.z;
  Temp.y = vect.y;
  Temp.z = vect.x;
  if (flipZ==true){
    Temp.z = -Temp.z;
    Temp.x = -Temp.x;
  }

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


bool OBJExporter::exportModel(WoWModel * model, std::string target) const
{
  if(!model)
    return false;

  LOG_INFO << "exporting" << model->modelname.mb_str() << "in" << target.c_str();

  std::ofstream file(target.c_str(),std::ofstream::out);

  file << "# Wavefront OBJ exported by" << GLOBALSETTINGS.appName() << " " << GLOBALSETTINGS.appVersion() << std::endl;

  /*
  bool vertMsg = false;
  // output all the vertice data
  int vertics = 0;
  for (size_t i=0; i<model->passes.size(); i++)
  {
    ModelRenderPass &p = model->passes[i];

    if (p.init(model))
    {
      //f << "# Chunk Indice Count: " << p.indexCount << endl;

      for (size_t k=0, b=p.indexStart; k<p.indexCount; k++,b++)
      {
        uint16 a = model->indices[b];
        Vec3D vert;
        if ((model->animated == true) && (model->vertices))
        {
          if (vertMsg == false)
          {
            LOG_INFO << "Using Verticies";
            vertMsg = true;
          }
          vert = model->vertices[a];
        }
        else
        {
          if (vertMsg == false)
          {
            LOG_INFO << "Using Original Verticies";
            vertMsg = true;
          }
          vert = model->origVertices[a].pos;
        }
        MakeModelFaceForwards(vert,false);
        vert *= 1.0;
        file << wxString::Format(wxT("v %.06f %.06f %.06f"), vert.x, vert.y, vert.z).mb_str() << std::endl;

        vertics ++;
      }
    }
  }

  file << "# " << vertics << " vertices" << std::endl << std::endl;
  */
  file.close();

  return true;
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
