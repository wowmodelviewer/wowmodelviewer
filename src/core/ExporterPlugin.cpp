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
 * ExporterPlugin.cpp
 *
 *  Created on: 17 feb. 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _EXPORTERPLUGIN_CPP_
#include "ExporterPlugin.h"
#undef _EXPORTERPLUGIN_CPP_


// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <string>

// Qt 
#include <QtGui/QImage>

// Externals
#include "ximage.h"

// Other libraries

// Current library


// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------


// Protected methods
//--------------------------------------------------------------------
void ExporterPlugin::exportGLTexture(GLuint id, std::string filename) const
{
  LOG_INFO << "Exporting GL texture with id " << id << "in" << filename.c_str();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, id);
  unsigned char *pixels = NULL;

  GLint width, height;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

  pixels = new unsigned char[width * height * 4];

  glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);

  // unfortunatelly, QImage cannot handle tga for writing, use CxImage for now
  if(filename.find(".tga") != std::string::npos)
  {
    CxImage *newImage = new CxImage(0);
    newImage->CreateFromArray(pixels, width, height, 32, (width*4), true);
    newImage->Save(filename.c_str(), CXIMAGE_FORMAT_TGA);
  }
  else
  {
    QImage texture(pixels, width, height, QImage::Format_ARGB32);
    texture.save(filename.c_str());
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

// Private methods
//--------------------------------------------------------------------
