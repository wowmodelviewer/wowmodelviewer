#pragma once

#include "manager.h"
#include "GL/glew.h"

typedef GLuint TextureID;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TEXTURE_API_ __declspec(dllexport)
#    else
#        define _TEXTURE_API_ __declspec(dllimport)
#    endif
#else
#    define _TEXTURE_API_
#endif

class _TEXTURE_API_ Texture : public ManagedItem 
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
