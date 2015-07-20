#ifndef RENDERTEXTURE_H
#define RENDERTEXTURE_H

// Video & graphics stuff
#include "OpenGLHeaders.h"
#include "video.h"

// WX
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _RENDERTEXTURE_API_ __declspec(dllexport)
#    else
#        define _RENDERTEXTURE_API_ __declspec(dllimport)
#    endif
#else
#    define _RENDERTEXTURE_API_
#endif

class _RENDERTEXTURE_API_ RenderTexture {
protected:
	HPBUFFERARB m_hPBuffer;
    HDC         m_hDC;
    HGLRC       m_hRC;

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
		m_frameBuffer = 0;
		m_depthRenderBuffer = 0;
		m_texFormat = 0;
	}

	~RenderTexture(){
		if (m_hRC != NULL)
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

#endif //RENDERTEXTURE_H

