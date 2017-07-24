
#define _TEXTUREMANAGER_CPP_
#include "TextureManager.h"
#undef _TEXTUREMANAGER_CPP_

#include "logger/Logger.h"

#include "GameFile.h"
#include "Texture.h"
#include "video.h"

#include <QImage>

#include "OpenGLHeaders.h"

GLuint TextureManager::add(GameFile * file)
{
	GLuint id = 0;

	if(!file)
	  return 0;

	QString name = file->fullname();

	// if the item already exists, return the existing ID
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		return id;
	}

	// Else, create the texture

	Texture *tex = new Texture(file);
	if (tex) {
		// clear old texture memory from vid card
		glDeleteTextures(1, &id);
		// create new texture and put it in memory
		glGenTextures(1, &id);

		tex->id = id;
		LoadBLP(id, tex);

		do_add(name, id, tex);
		return id;
	}

	return 0;
}
//#define SAVE_BLP

void TextureManager::LoadBLP(GLuint id, Texture *tex)
{
	// Vars
	int offsets[16], sizes[16], type=0;
	size_t w=0, h=0;
	GLint format = 0;
	char attr[4];

	// bind the texture
	glBindTexture(GL_TEXTURE_2D, id);
	
	GameFile * f = tex->file;

	if (!f || !f->open() || f->isEof()) {
		tex->id = 0;
		return;
	} else {
		//tex->id = id; // I don't see the id being set anywhere,  should I set it now?
	}

	f->seek(4);
	f->read(&type,4);
	f->read(attr,4);
	f->read(&w,4);
	f->read(&h,4);
	f->read(offsets,4*16);
	f->read(sizes,4*16);

	tex->w = w;
	tex->h = h;

	bool hasmipmaps = (attr[3]>0);
	size_t mipmax = hasmipmaps ? 16 : 1;

	/*
	reference: http://en.wikipedia.org/wiki/.BLP
	*/
	if (type == 0) { // JPEG compression
		/*
		 * DWORD JpegHeaderSize;
		 * BYTE[JpegHeaderSize] JpegHeader;
		 * struct MipMap[16]
		 * {
		 *     BYTE[???] JpegData;
		 * }
		 */
		LOG_ERROR << __FILE__ << __FUNCTION__ << __LINE__ << "type="<< type;

		BYTE *buffer = NULL;
		unsigned char *buf = new unsigned char[sizes[0]];

		f->seek(offsets[0]);
		f->read(buf,sizes[0]);

		QImage image;

		if(!image.loadFromData(buf, sizes[0],"jpg"))
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
	} else if (type == 1) {
		if (attr[0] == 2) {
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
				ucbuf = new unsigned char[w*h*4];
		
			format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			int blocksize = 8;
			
			// guesswork here :(
			// new alpha bit depth == 4 for DXT3, alfred 2008/10/11
			if (attr[1]==8 || attr[1]==4) {
				format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
				blocksize = 16;
			}

			// Fix to the BLP2 format required in WoW 2.0 thanks to Linghuye (creator of MyWarCraftStudio)
			if (attr[1]==8 && attr[2]==7) {
				format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				blocksize = 16;
			}

			tex->compressed = true;

			unsigned char *buf = new unsigned char[sizes[0]];

			// do every mipmap level
			for (size_t i=0; i<mipmax; i++) {
				if (w==0) w = 1;
				if (h==0) h = 1;
				if (offsets[i] && sizes[i]) {
					f->seek(offsets[i]);
					f->read(buf,sizes[i]);

					int size = ((w+3)/4) * ((h+3)/4) * blocksize;

					if (video.supportCompression) {
						glCompressedTexImage2DARB(GL_TEXTURE_2D, (GLint)i, format, w, h, 0, size, buf);
					} else {
						decompressDXTC(format, w, h, size, buf, ucbuf);					
						glTexImage2D(GL_TEXTURE_2D, (GLint)i, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ucbuf);
					}
					
				} else break;
				w >>= 1;
				h >>= 1;
			}

			delete buf;
			buf = 0;
			if (!video.supportCompression)
			{
				delete ucbuf;
				ucbuf = 0;
			}

		} else if (attr[0]==1) {
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
			f->read(pal, 1024);

			unsigned char *buf = new unsigned char[sizes[0]];
			unsigned int *buf2 = new unsigned int[w*h];
			unsigned int *p = NULL;
			unsigned char *c = NULL, *a = NULL;

			int alphabits = attr[1];
			bool hasalpha = (alphabits!=0);

			tex->compressed = false;

			for (size_t i=0; i<mipmax; i++) {
				if (w==0) w = 1;
				if (h==0) h = 1;
				if (offsets[i] && sizes[i]) {
					f->seek(offsets[i]);
					f->read(buf,sizes[i]);

					int cnt = 0;
					int alpha = 0;

					p = buf2;
					c = buf;
					a = buf + w*h;
					for (size_t y=0; y<h; y++) {
						for (size_t x=0; x<w; x++) {
							unsigned int k = pal[*c++];

							k = ((k&0x00FF0000)>>16) | ((k&0x0000FF00)) | ((k& 0x000000FF)<<16);

							if (hasalpha) {
								if (alphabits == 8) {
									alpha = (*a++);
								} else if (alphabits == 4) {
									alpha = (*a & (0xf << cnt++)) * 0x11;
									if (cnt == 2) {
										cnt = 0;
										a++;
									}
								} else if (alphabits == 1) {
									//alpha = (*a & (128 >> cnt++)) ? 0xff : 0;
									alpha = (*a & (1 << cnt++)) ? 0xff : 0;
									if (cnt == 8) {
										cnt = 0;
										a++;
									}
								}
							} else alpha = 0xff;

							k |= alpha << 24;
							*p++ = k;
						}
					}

					glTexImage2D(GL_TEXTURE_2D, (GLint)i, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf2);
					
				} else break;

				w >>= 1;
				h >>= 1;
			}

			delete buf2;
			buf2 = 0;
			delete buf;
			buf = 0;
		} else {
			LOG_ERROR << __FILE__ << __FUNCTION__ << __LINE__ << "type=" << type << "attr[0]=" << attr[0];
		}
	}

	f->close();
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

void TextureManager::doDelete(GLuint id)
{
	if (glIsTexture(id)) {
		glDeleteTextures(1, &id);
	}
}

