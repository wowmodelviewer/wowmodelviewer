#define _TEXTURE_CPP_
#include "Texture.h"
#undef _TEXTURE_CPP_

#include "GameFile.h"

#include <QImage>

#include <algorithm>
#include <cstring>

#include "BlockDecode.h"

namespace
{
  // The GL enums each BLP encoding used to be uploaded as: what the texture info panels name it by.
  unsigned int glFormatOf(wmv::BlockFormat f)
  {
    switch (f)
    {
    case wmv::BLOCK_DXT1_RGB:  return 0x83F0; // GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    case wmv::BLOCK_DXT1_RGBA: return 0x83F1; // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    case wmv::BLOCK_DXT3:      return 0x83F2; // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    default:                   return 0x83F3; // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    }
  }
}

Texture::Texture(GameFile * f)
: ManagedItem(f->fullname()), w(0), h(0), id(0), compressed(false), sourceFormat(0x8058), file(f), generation(0)
{
}

Texture::Texture(const QString & name)
: ManagedItem(name), w(0), h(0), id(0), compressed(false), sourceFormat(0x8058), file(nullptr), generation(0)
{
}

void Texture::getPixels(unsigned char * buf, unsigned int format) const
{
  if (mips.empty() || !buf)
    return;
  const std::vector<unsigned char> & level = mips[0];
  if (format == TEXTURE_BGRA)
  {
    for (size_t i = 0; i + 3 < level.size(); i += 4)
    {
      buf[i] = level[i + 2]; buf[i + 1] = level[i + 1]; buf[i + 2] = level[i]; buf[i + 3] = level[i + 3];
    }
  }
  else
    std::memcpy(buf, level.data(), level.size());
}

void Texture::setPixels(int width, int height, const unsigned char * pixels, unsigned int format)
{
  w = width;
  h = height;
  compressed = false;
  mips.assign(1, std::vector<unsigned char>());
  if (width > 0 && height > 0 && pixels)
  {
    std::vector<unsigned char> & level = mips[0];
    level.assign(pixels, pixels + (size_t)width * height * 4);
    if (format == TEXTURE_BGRA)
      for (size_t i = 0; i + 3 < level.size(); i += 4)
        std::swap(level[i], level[i + 2]);
  }
  generation++;
}

void Texture::load()
{
  // Vars
  int offsets[16], sizes[16], type = 0;
  uint width = 0, height = 0;
  char attr[4];
  mips.clear();

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

  LOG_INFO << "Loading texture" << itemName() << "type" << type << "attrs" << (uint)attr[0] << (uint)attr[1] << (uint)attr[2] << (uint)attr[3] << "width" << width << "height" << height;

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

    w = image.width();
    h = image.height();
    mips.emplace_back(image.constBits(), image.constBits() + (size_t)w * h * 4);
    delete [] buf;
    buf = nullptr;
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
      wmv::BlockFormat format = wmv::BLOCK_DXT1_RGBA;
      int blocksize = 8;

      // guesswork here :(
      // new alpha bit depth == 4 for DXT3, alfred 2008/10/11
      if (attr[1] == 8 || attr[1] == 4) 
      {
        format = wmv::BLOCK_DXT3;
        blocksize = 16;
      }
     
      if(attr[1] == 0 && attr[2] == 0)
	  {
        format = wmv::BLOCK_DXT1_RGB;
      }
      

      // Fix to the BLP2 format required in WoW 2.0 thanks to Linghuye (creator of MyWarCraftStudio)
      if (attr[1] == 8 && attr[2] == 7) 
      {
        format = wmv::BLOCK_DXT5;
        blocksize = 16;
      }

      compressed = true;
      sourceFormat = glFormatOf(format);

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

          const size_t size = (size_t)((width + 3) / 4) * ((height + 3) / 4) * blocksize;
          if (size > (size_t)sizes[i])
            break; // a truncated level: keep the ones before it

          mips.emplace_back((size_t)width * height * 4);
          wmv::decodeBlocks(format, buf, width, height, mips.back().data());

        }
        else
        {
          break;
        }
        width >>= 1;
        height >>= 1;
      }

      delete [] buf;
      buf = nullptr;

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

          const unsigned char * bytes = reinterpret_cast<const unsigned char *>(buf2);
          mips.emplace_back(bytes, bytes + (size_t)width * height * 4);

        }
        else break;

        width >>= 1;
        height >>= 1;
      }

      delete [] buf2;
      buf2 = nullptr;
      delete [] buf;
      buf = nullptr;
    }
    else 
    {
      LOG_ERROR << __FILE__ << __FUNCTION__ << __LINE__ << "type=" << type << "attr[0]=" << attr[0];
    }
  }

  file->close();
  generation++;
}
