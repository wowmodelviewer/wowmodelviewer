#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <vector>
#include "manager.h"
#include "vec3d.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TEXTUREMANAGER_API_ __declspec(dllexport)
#    else
#        define _TEXTUREMANAGER_API_ __declspec(dllimport)
#    endif
#else
#    define _TEXTUREMANAGER_API_
#endif

class GameFile;
class Texture;

class _TEXTUREMANAGER_API_ TextureManager : public Manager<GLuint> 
{
public:
	virtual GLuint add(GameFile *);
	void doDelete(GLuint id);

};

_TEXTUREMANAGER_API_ extern TextureManager texturemanager;

#endif

