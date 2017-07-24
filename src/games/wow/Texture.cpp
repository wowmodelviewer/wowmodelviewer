
#define _TEXTURE_CPP_
#include "Texture.h"
#undef _TEXTURE_CPP_

#include "GameFile.h"

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

struct Color {
	unsigned char r, g, b;
};


