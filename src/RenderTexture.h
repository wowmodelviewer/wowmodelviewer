#ifndef RENDERTEXTURE_H
#define RENDERTEXTURE_H

// Video & graphics stuff
#include "OpenGLHeaders.h"
#include "video.h"

// WX
#include <wx/wxprec.h>
#ifdef __BORLANDC__
    #pragma hdrstop
#endif
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#ifdef _WINDOWS
class RenderTexture {
protected:
	HPBUFFERARB m_hPBuffer;
    HDC         m_hDC;
    HGLRC       m_hRC;

	HWND		canvas_hWnd;
	HDC         canvas_hDC;
    HGLRC       canvas_hRC;

	GLuint		m_frameBuffer;
	GLuint		m_depthRenderBuffer;

	GLuint		m_texID;
	GLenum		m_texFormat;

	bool		m_FBO;

public:

	int         nWidth;
    int         nHeight;

	RenderTexture() { 
		m_hPBuffer = NULL;
		m_hDC = NULL;
		m_hRC = NULL;
		m_texID = 0;

		nWidth = 0;
		nHeight = 0;

		m_FBO = false;
	}

	~RenderTexture(){
		if (m_hRC != NULL)
			Shutdown();
	}

	void Init(HWND hWnd, int width, int height, bool fboMode);
	void Shutdown();

	void BeginRender();
	void EndRender();

	void BindTexture();
	void ReleaseTexture();
	GLenum GetTextureFormat() { return m_texFormat; };

	void SwapBuffer() { ::SwapBuffers(m_hDC); };

	void InitGL();

};
#endif

#endif //RENDERTEXTURE_H

