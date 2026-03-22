#pragma once

#include <vector>
#include <glad/gl.h>
#include "manager.h"

class GameFile;
class Texture;

/// @brief Manages OpenGL texture lifetimes with reference-counted caching.
///
/// Textures are loaded from GameFile sources and cached by name.  Duplicate
/// requests return the existing GL texture ID instead of re-uploading.
class TextureManager : public Manager<GLuint>
{
public:
	virtual GLuint add(GameFile *);
	void doDelete(GLuint id);

};

extern TextureManager TEXTUREMANAGER;
