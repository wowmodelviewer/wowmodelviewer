#pragma once

#include <vector>
#include "Gl/glew.h"
#include "manager.h"

#define _TEXTUREMANAGER_API_

class GameFile;
class Texture;

class _TEXTUREMANAGER_API_ TextureManager : public Manager<GLuint> 
{
public:
	virtual GLuint add(GameFile *);
	void doDelete(GLuint id);

};

_TEXTUREMANAGER_API_ extern TextureManager TEXTUREMANAGER;
