
#define _TEXTURE_CPP_
#include "Texture.h"
#undef _TEXTURE_CPP_

#include "GameFile.h"
#include "video.h"

#include <QImage>

#include "OpenGLHeaders.h"

Texture::Texture(GameFile * f)
: ManagedItem(f->fullname()), w(0), h(0), id(0), compressed(false), file(f)
{
}

void Texture::getPixels(unsigned char* buf, unsigned int format)
{
	glBindTexture(GL_TEXTURE_2D, id);
	glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, buf);
}

void Texture::load()
{
  // Vars
  int offsets[16], sizes[16], type = 0;
  uint width = 0, height = 0;
  GLint format = 0;
  char attr[4];

  // bind the texture
  glBindTexture(GL_TEXTURE_2D, id);

  if (!file || !file->open() || file->isEof()) 
  {
    id = 0;
    return;
  }
  else {
    //tex->id = id; // I don't see the id being set anywhere,  should I set it now?
  }

  file->seek(4);
  file->read(&type, 4);
  file->read(attr, 4);
  file->read(&width, 4);
  file->read(&height, 4);
  file->read(offsets, 4 * 16);
  file->read(sizes, 4 * 16);

  bool hasmipmaps = (attr[3]>0);
  size_t mipmax = hasmipmaps ? 16 : 1;

  w = width;
  h = height;

  /*
  reference: http://en.wikipedia.org/wiki/.BLP
  */
  if (type == 0) // JPEG compression
  { 
    /*
    * DWORD JpegHeaderSize;
    * BYTE[JpegHeaderSize] JpegHeader;
    * struct MipMap[16]
    * {
    *     BYTE[???] JpegData;
    * }
    */
    LOG_ERROR << __FILE__ << __FUNCTION__ << __LINE__ << "type=" << type;

    BYTE *buffer = NULL;
    unsigned char *buf = new unsigned char[sizes[0]];

    file->seek(offsets[0]);
    file->read(buf, sizes[0]);

    QImage image;

    if (!image.loadFromData(buf, sizes[0], "jpg"))
    {
      LOG_ERROR << __FUNCTION__ << __LINE__ << "Failed to load texture";
    }

    image = image.mirrored();
    image = image.convertToFormat(QImage::Format_RGBA8888);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

    delete buffer;
    buffer = 0;
    delete buf;
    buf = 0;
  }
  else if (type == 1) 
  {
    if (attr[0] == 2) 
    {
      /*
      Type 1 Encoding 2 AlphaDepth 0 (DXT1 no alpha)
      The image data is formatted using DXT1 compression with no alpha channel.

      Type 1 Encoding 2 AlphaDepth 1 (DXT1 one bit alpha)
      The image data is formatted using DXT1 compression with a one-bit alpha channel.

      Type 1 Encoding 2 AlphaDepth 8 (DXT3)
      The image data is formatted using DXT3 compression.

      Type 1 Encoding 2 AlphaDepth 8 AlphaEncoding 7 (DXT5)
      The image data are formatted using DXT5 compression.
      */
      // encoding 2, directx compressed
      unsigned char *ucbuf = NULL;
      if (!video.supportCompression)
        ucbuf = new unsigned char[width*height * 4];

      format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
      int blocksize = 8;

      // guesswork here :(
      // new alpha bit depth == 4 for DXT3, alfred 2008/10/11
      if (attr[1] == 8 || attr[1] == 4) 
      {
        format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        blocksize = 16;
      }

      // Fix to the BLP2 format required in WoW 2.0 thanks to Linghuye (creator of MyWarCraftStudio)
      if (attr[1] == 8 && attr[2] == 7) 
      {
        format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        blocksize = 16;
      }

      compressed = true;

      unsigned char *buf = new unsigned char[sizes[0]];

      // do every mipmap level
      for (size_t i = 0; i<mipmax; i++) 
      {
        if (width == 0) width = 1;
        if (height == 0) height = 1;
        if (offsets[i] && sizes[i]) 
        {
          file->seek(offsets[i]);
          file->read(buf, sizes[i]);

          int size = ((width + 3) / 4) * ((height + 3) / 4) * blocksize;

          if (video.supportCompression) 
          {
            glCompressedTexImage2DARB(GL_TEXTURE_2D, (GLint)i, format, width, height, 0, size, buf);
          }
          else 
          {
            decompressDXTC(format, width, height, size, buf, ucbuf);
            glTexImage2D(GL_TEXTURE_2D, (GLint)i, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ucbuf);
          }

        }
        else
        {
          break;
        }
        width >>= 1;
        height >>= 1;
      }

      delete buf;
      buf = 0;
      if (!video.supportCompression)
      {
        delete ucbuf;
        ucbuf = 0;
      }

    }
    else if (attr[0] == 1) 
    {
      /*
      Type 1 Encoding 0 AlphaDepth 0 (uncompressed paletted image with no alpha)
      Each by of the image data is an index into Palette which contains the actual RGB value for the pixel. Although the palette entries are 32-bits, the alpha value of each Palette entry may contain garbage and should be discarded.

      Type 1 Encoding 1 AlphaDepth 1 (uncompressed paletted image with 1-bit alpha)
      This is the same as Type 1 Encoding 1 AlphaDepth 0 except that immediately following the index array is a second image array containing 1-bit alpha values for each pixel. The first byte of the array is for pixels 0 through 7, the second byte for pixels 8 through 15 and so on. Bit 0 of each byte corresponds to the first pixel (leftmost) in the group, bit 7 to the rightmost. A set bit indicates the pixel is opaque while a zero bit indicates a transparent pixel.

      Type 1 Encoding 1 AlphaDepth 8(uncompressed paletted image with 8-bit alpha)
      This is the same as Type 1 Encoding 1 AlphaDepth 0 except that immediately following the index array is a second image array containing the actual 8-bit alpha values for each pixel. This second array starts at BLP2Header.Offset[0] + BLP2Header.Width * BLP2Header.Height.
      */

      // encoding 1, uncompressed
      unsigned int pal[256];
      file->read(pal, 1024);

      unsigned char *buf = new unsigned char[sizes[0]];
      unsigned int *buf2 = new unsigned int[width*height];
      unsigned int *p = NULL;
      unsigned char *c = NULL, *a = NULL;

      int alphabits = attr[1];
      bool hasalpha = (alphabits != 0);

      compressed = false;

      for (size_t i = 0; i<mipmax; i++) 
      {
        if (width == 0) width = 1;
        if (height == 0) height = 1;
        if (offsets[i] && sizes[i]) 
        {
          file->seek(offsets[i]);
          file->read(buf, sizes[i]);

          int cnt = 0;
          int alpha = 0;

          p = buf2;
          c = buf;
          a = buf + width*height;
          for (uint y = 0; y<height; y++)
          {
            for (uint x = 0; x<width; x++)
            {
              uint k = pal[*c++];

              k = ((k & 0x00FF0000) >> 16) | ((k & 0x0000FF00)) | ((k & 0x000000FF) << 16);

              if (hasalpha) 
              {
                if (alphabits == 8)
                {
                  alpha = (*a++);
                }
                else if (alphabits == 4) 
                {
                  alpha = (*a & (0xf << cnt++)) * 0x11;
                  if (cnt == 2) 
                  {
                    cnt = 0;
                    a++;
                  }
                }
                else if (alphabits == 1) 
                {
                  //alpha = (*a & (128 >> cnt++)) ? 0xff : 0;
                  alpha = (*a & (1 << cnt++)) ? 0xff : 0;
                  if (cnt == 8) 
                  {
                    cnt = 0;
                    a++;
                  }
                }
              }
              else alpha = 0xff;

              k |= alpha << 24;
              *p++ = k;
            }
          }

          glTexImage2D(GL_TEXTURE_2D, (GLint)i, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf2);

        }
        else break;

        width >>= 1;
        height >>= 1;
      }

      delete buf2;
      buf2 = 0;
      delete buf;
      buf = 0;
    }
    else 
    {
      LOG_ERROR << __FILE__ << __FUNCTION__ << __LINE__ << "type=" << type << "attr[0]=" << attr[0];
    }
  }

  file->close();
  /*
  // TODO: Add proper support for mipmaps
  if (hasmipmaps) {
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  } else {
  */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  //}
}

/*
struct Color {
unsigned char r, g, b;
};
*/

void Texture::decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest)
{
  // DXT1 Textures, currently being handles by our routine below
  if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) 
  {
    DDSDecompressDXT1(src, w, h, dest);
    return;
  }

  // DXT3 Textures
  if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) 
  {
    DDSDecompressDXT3(src, w, h, dest);
    return;
  }

  // DXT5 Textures
  if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)	
  {
    //DXT5UnpackAlphaValues(src, w, h, dest);
    DDSDecompressDXT5(src, w, h, dest);
    return;
  }

  /*
  // sort of copied from linghuye
  int bsx = (w<4) ? w : 4;
  int bsy = (h<4) ? h : 4;

  for(int y=0; y<h; y += bsy) {
  for(int x=0; x<w; x += bsx) {
  //unsigned long alpha = 0;
  //unsigned int a0 = 0, a1 = 0;

  unsigned int c0 = *(unsigned short*)(src + 0);
  unsigned int c1 = *(unsigned short*)(src + 2);
  src += 4;

  Color color[4];
  color[0].b = (unsigned char) ((c0 >> 11) & 0x1f) << 3;
  color[0].g = (unsigned char) ((c0 >>  5) & 0x3f) << 2;
  color[0].r = (unsigned char) ((c0      ) & 0x1f) << 3;
  color[1].b = (unsigned char) ((c1 >> 11) & 0x1f) << 3;
  color[1].g = (unsigned char) ((c1 >>  5) & 0x3f) << 2;
  color[1].r = (unsigned char) ((c1      ) & 0x1f) << 3;

  if(c0 > c1 || format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
  color[2].r = (color[0].r * 2 + color[1].r) / 3;
  color[2].g = (color[0].g * 2 + color[1].g) / 3;
  color[2].b = (color[0].b * 2 + color[1].b) / 3;
  color[3].r = (color[0].r + color[1].r * 2) / 3;
  color[3].g = (color[0].g + color[1].g * 2) / 3;
  color[3].b = (color[0].b + color[1].b * 2) / 3;
  } else {
  color[2].r = (color[0].r + color[1].r) / 2;
  color[2].g = (color[0].g + color[1].g) / 2;
  color[2].b = (color[0].b + color[1].b) / 2;
  color[3].r = 0;
  color[3].g = 0;
  color[3].b = 0;
  }

  for (ssize_t j=0; j<bsy; j++) {
  unsigned int index = *src++;
  unsigned char* dd = dest + (w*(y+j)+x)*4;
  for (size_t i=0; i<bsx; i++) {
  *dd++ = color[index & 0x03].b;
  *dd++ = color[index & 0x03].g;
  *dd++ = color[index & 0x03].r;
  //if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)	{
  *dd++ = ((index & 0x03) == 3 && c0 <= c1) ? 0 : 255;
  //}
  index >>= 2;
  }
  }
  }
  }
  */
}