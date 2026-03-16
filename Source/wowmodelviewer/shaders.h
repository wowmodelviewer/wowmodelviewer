#pragma once

#include <glad/gl.h>

extern bool supportShaders;

void OldinitShaders();
void OldreloadShaders();

class Shader
{
	GLenum target;
	GLuint id;

public:
	bool ok;

	Shader(GLenum target, const char* program, bool fromFile = false);
	virtual ~Shader();

	virtual void bind();
	virtual void unbind();
};

class ShaderPair
{
	Shader* vertex;
	Shader* fragment;

public:
	ShaderPair(): vertex(nullptr), fragment(nullptr)
	{
	}

	ShaderPair(Shader* vs, Shader* ps): vertex(vs), fragment(ps)
	{
	}

	ShaderPair(const char* vprog, const char* fprog, bool fromFile = false);

	void bind();
	void unbind();
};

extern ShaderPair *terrainShaders[4], *wmoShader, *waterShaders[1];
