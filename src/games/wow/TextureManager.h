#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <vector>
#include "Gl/glew.h"

#include "manager.h"

class GameFile;
class Texture;

class TextureManager : public Manager<GLuint> 
{
public:
	virtual GLuint add(GameFile *);
	void doDelete(GLuint id);

};

extern TextureManager TEXTUREMANAGER;

#endif

