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
void ExporterPlugin::exportGLTexture(GLuint id, std::wstring filename, const float * tintRGB,
                                     bool alphaFromLuminance, AdditiveTransform transform) const
{
  LOG_INFO << "Exporting GL texture with id " << id << "in" << filename.c_str();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, id);

  GLint width = 0, height = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  if (std::getenv("WMV_MATDUMP"))
    LOG_INFO << "[texexport] id" << id << "GL_TEXTURE_2D level0 =" << width << "x" << height;

  unsigned char * pixels = new unsigned char[(size_t)width * (size_t)height * 4];
  glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);

  saveRGBABuffer(pixels, width, height, filename, tintRGB, alphaFromLuminance, transform);
  delete[] pixels;
}

// pixelsBGRA: GL_BGRA_EXT byte order (0=B,1=G,2=R,3=A per pixel), width*height*4 bytes -- the
// same layout exportGLTexture() reads back from a live GL texture, and what an offscreen bake
// (see FBXExporter::bakeCombinerTexture) produces via glReadPixels(..., GL_BGRA, ...). Shared so
// both a live-texture export and a baked/computed one go through identical tint + save logic.
void ExporterPlugin::saveRGBABuffer(unsigned char * pixelsBGRA, int width, int height, std::wstring filename,
                                    const float * tintRGB, bool alphaFromLuminance,
                                    AdditiveTransform transform) const
{
  if (tintRGB)
    LOG_INFO << "[texexport] baking tint (" << tintRGB[0] << "," << tintRGB[1] << "," << tintRGB[2] << ")";

  // Bake a runtime-only colour tint into the pixels. Alpha is untouched -- the tint is a colour
  // multiply, not a transparency change.
  if (tintRGB)
  {
    const float tint[3] = { tintRGB[2], tintRGB[1], tintRGB[0] }; // B,G,R order to match px[]
    const size_t pixelCount = (size_t)width * (size_t)height;
    for (size_t p = 0; p < pixelCount; ++p)
    {
      unsigned char * px = pixelsBGRA + p * 4;
      for (int c = 0; c < 3; ++c)
      {
        float v = px[c] * tint[c];
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        px[c] = (unsigned char)v;
      }
    }
  }

  // Rewrite additive-pass pixels so the file's RGB is the pass's true per-plane framebuffer
  // contribution (see AdditiveTransform in ExporterPlugin.h). Applied after the tint.
  if (transform == AdditiveTransform::PremultiplyAlpha)
  {
    const size_t pixelCount = (size_t)width * (size_t)height;
    for (size_t p = 0; p < pixelCount; ++p)
    {
      unsigned char * px = pixelsBGRA + p * 4;
      const unsigned int a = px[3];
      px[0] = (unsigned char)((px[0] * a) / 255);
      px[1] = (unsigned char)((px[1] * a) / 255);
      px[2] = (unsigned char)((px[2] * a) / 255);
    }
  }
  else if (transform == AdditiveTransform::SquareColor)
  {
    const size_t pixelCount = (size_t)width * (size_t)height;
    for (size_t p = 0; p < pixelCount; ++p)
    {
      unsigned char * px = pixelsBGRA + p * 4;
      for (int c = 0; c < 3; ++c)
        px[c] = (unsigned char)(((unsigned int)px[c] * px[c]) / 255);
    }
  }

  // Colour-additive passes: contribution = brightness, source alpha means nothing. Encode
  // brightness as the file's alpha so DCC transparency reproduces the additive look (black
  // regions vanish instead of rendering as opaque black planes). Applied after the tint so a
  // tinted glow's alpha follows its final colour.
  if (alphaFromLuminance)
  {
    const size_t pixelCount = (size_t)width * (size_t)height;
    for (size_t p = 0; p < pixelCount; ++p)
    {
      unsigned char * px = pixelsBGRA + p * 4;
      unsigned char m = px[0] > px[1] ? px[0] : px[1];
      if (px[2] > m) m = px[2];
      px[3] = m;
    }
  }

  // unfortunatelly, QImage cannot handle tga for writing, use CxImage for now
  if(filename.find(L".tga") != std::wstring::npos)
  {
    CxImage *newImage = new CxImage(0);
    newImage->CreateFromArray(pixelsBGRA, width, height, 32, (width*4), true);
    newImage->Save(filename.c_str(), CXIMAGE_FORMAT_TGA);
  }
  else
  {
    QImage texture(pixelsBGRA, width, height, QImage::Format_ARGB32);
    const bool saved = texture.save(QString::fromStdWString(filename));
    if (!saved)
      LOG_ERROR << "[texexport] QImage::save FAILED for" << QString::fromStdWString(filename);
  }
}

// Private methods
//--------------------------------------------------------------------
