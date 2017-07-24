
#define _TEXTUREMANAGER_CPP_
#include "TextureManager.h"
#undef _TEXTUREMANAGER_CPP_

#include "logger/Logger.h"

#include "GameFile.h"
#include "Texture.h"

#include "OpenGLHeaders.h"

_TEXTUREMANAGER_API_ TextureManager texturemanager;

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
		tex->load();

		do_add(name, id, tex);
		return id;
	}

	return 0;
}
//#define SAVE_BLP

void TextureManager::doDelete(GLuint id)
{
	if (glIsTexture(id)) {
		glDeleteTextures(1, &id);
	}
}
