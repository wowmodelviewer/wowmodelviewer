#pragma once

#include "manager.h"
#include <glad/gl.h>

typedef GLuint TextureID;

class Texture : public ManagedItem 
{
public:
	int w,h;
	GLuint id;
	bool compressed;
	GameFile * file;

	Texture(GameFile *);
	void getPixels(unsigned char *buff, unsigned int format=GL_RGBA);
  void load();

private:
	void decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest);
};
