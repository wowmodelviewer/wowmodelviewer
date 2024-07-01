#pragma once

#include <vector>
#include "Gl/glew.h"
#include "manager.h"

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

_TEXTUREMANAGER_API_ extern TextureManager TEXTUREMANAGER;
