#pragma once

#include <glad/gl.h>
#include <glad/wgl.h>

#define _RENDERTEXTURE_API_

class _RENDERTEXTURE_API_ RenderTexture
{
protected:
	HPBUFFERARB m_hPBuffer;
	HDC m_hDC;
	HGLRC m_hRC;

	HDC canvas_hDC;
	HGLRC canvas_hRC;

	GLuint m_frameBuffer;
	GLuint m_depthRenderBuffer;

	GLuint m_texID;
	GLenum m_texFormat;

	bool m_FBO;

public:
	int nWidth;
	int nHeight;

	RenderTexture()
	{
		m_hPBuffer = nullptr;
		m_hDC = nullptr;
		m_hRC = nullptr;
		canvas_hDC = nullptr;
		canvas_hRC = nullptr;
		m_texID = 0;

		nWidth = 0;
		nHeight = 0;

		m_FBO = false;
		m_frameBuffer = 0;
		m_depthRenderBuffer = 0;
		m_texFormat = 0;
	}

	~RenderTexture()
	{
		if (m_hRC != nullptr)
			Shutdown();
	}

	void Init(int width, int height, bool fboMode);
	void Shutdown();

	void BeginRender();
	void EndRender();

	void BindTexture();
	void ReleaseTexture();
	GLenum GetTextureFormat() { return m_texFormat; };

	void InitGL();
};
