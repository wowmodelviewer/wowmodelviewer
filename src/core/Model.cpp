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
 * Model.cpp
 *
 * Created on: 24 may 2014
 *   Copyright: 2014 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _MODEL_CPP_
#include "Model.h"

#include "Logger.h"
#undef _MODEL_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Irrlicht

// Externals

// Other libraries

// Current library
//

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors
//--------------------------------------------------------------------
Model::Model(GameFile* file)
{
  //load(file);
}

// Destructor
//--------------------------------------------------------------------
Model::~Model()
{
  for (auto* m : meshes_)
    delete m;
}


// Public methods
//--------------------------------------------------------------------
void Model::draw(ShaderProgram & shader)
{
  for (auto* m : meshes_)
    m->draw(shader);
}

void Model::addMesh(Mesh * mesh)
{
  meshes_.push_back(mesh);
}


// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
void Model::load(GameFile*)
{
  std::vector<glm::vec3> pos = {
    // position

   // front face
   {-1.0f,  1.0f,  1.0f},
   { 1.0f, -1.0f,  1.0f},
   { 1.0f,  1.0f,  1.0f},
   {-1.0f,  1.0f,  1.0f},
   {-1.0f, -1.0f,  1.0f},
   { 1.0f, -1.0f,  1.0f},

   // back face
   { 1.0f,  1.0f, -1.0f},
   { 1.0f, -1.0f, -1.0f},
   { 1.0f,  1.0f, -1.0f},
   {-1.0f,  1.0f, -1.0f},
   {-1.0f, -1.0f, -1.0f},
   { 1.0f, -1.0f, -1.0f},

   // left face
   {-1.0f,  1.0f, -1.0f},
   {-1.0f, -1.0f,  1.0f},
   {-1.0f,  1.0f,  1.0f},
   {-1.0f,  1.0f, -1.0f},
   {-1.0f, -1.0f, -1.0f},
   {-1.0f, -1.0f,  1.0f},

   // right face
   { 1.0f,  1.0f,  1.0f},
   { 1.0f, -1.0f, -1.0f},
   { 1.0f,  1.0f, -1.0f},
   { 1.0f,  1.0f,  1.0f},
   { 1.0f, -1.0f,  1.0f},
   { 1.0f, -1.0f, -1.0f},

   // top face
   {-1.0f,  1.0f, -1.0f},
   { 1.0f,  1.0f,  1.0f},
   { 1.0f,  1.0f, -1.0f},
   {-1.0f,  1.0f, -1.0f},
   {-1.0f,  1.0f,  1.0f},
   { 1.0f,  1.0f,  1.0f},

   // bottom face
   {-1.0f, -1.0f,  1.0f},
   { 1.0f, -1.0f, -1.0f},
   { 1.0f, -1.0f,  1.0f},
   {-1.0f, -1.0f,  1.0f},
   {-1.0f, -1.0f, -1.0f},
   { 1.0f, -1.0f, -1.0f},
  };

  std::vector<Vertex> vertices;
  for (auto & v : pos)
  {
    auto * vertex = new Vertex();
    vertex->position = v;
    vertices.push_back(*vertex);
  }
    
 // meshes_.push_back(*(new Mesh(vertices)));

  //std::vector<unsigned int> indices;
}

