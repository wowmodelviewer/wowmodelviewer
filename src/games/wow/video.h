#ifndef VIDEO_H
#define VIDEO_H

#include <vector>
#include "ddslib.h"
#include "manager.h"
#include "RenderTexture.h"
#include "vec3d.h"

typedef GLuint TextureID;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _VIDEO_API_ __declspec(dllexport)
#    else
#        define _VIDEO_API_ __declspec(dllimport)
#    endif
#else
#    define _VIDEO_API_
#endif


class _VIDEO_API_ Texture : public ManagedItem {
public:
	int w,h;
	GLuint id;
	bool compressed;
	GameFile * file;

	Texture(GameFile *);
	void getPixels(unsigned char *buff, unsigned int format=GL_RGBA);

};


class _VIDEO_API_ TextureManager : public Manager<GLuint> {
	
public:
	virtual GLuint add(GameFile *);
	void doDelete(GLuint id);

	void LoadBLP(GLuint id, Texture *tex);
};

struct VideoCaps
{
	int	colour;
	int alpha;
	int zBuffer;
	int accum;
	int stencil;
	int aaSamples;	// how many AA samples can this mode do?
	int hwAcc;	// Hardware Acceleration mode?
	GLboolean sampleBuffer;	// do we have an AA sample buffer?
	GLboolean doubleBuffer;	// double buffered?
};

class _VIDEO_API_ VideoSettings {

public:
	VideoSettings();
	~VideoSettings();

	// Functions
	bool Init();
	void InitGL();
	void EnumDisplayModes();

	bool GetCompatibleWinMode(VideoCaps caps);
	bool GetAvailableMode();
	void ResizeGLScene(int width, int height);
	void SetMode();
	void SetCurrent();
	void SwapBuffers();
	void Release();
	
#ifdef _WINDOWS
	HWND GetHandle() {return hWnd;}
	void SetHandle(HWND hwnd, int bpp);

	// Resources
	HDC hDC;
	HWND hWnd;
	HGLRC hRC;
#endif

	bool init; 
	bool render;
	bool refresh;

	// OpenGL Settings
	int xRes, yRes;
	int pixelFormat;
	float fov;
	int desktopBPP;

	// Card capabilities
	std::vector<VideoCaps> capsList;
	VideoCaps curCap;
	int capIndex;

	int AnisofilterLevel;
	int numTextureUnits;

	// Card Info
	char *vendor;
	char *version;
	char *renderer;
	
	// Is there hardware support?
	bool hasHardware;
	bool secondPass;

	// Video card support for OGL Extensions
	bool supportFragProg;
	bool supportVertexProg;
	bool supportGLSL;
	bool supportCompression;// = false;
	bool supportMultiTex;//  = false;
	bool supportDrawRangeElements;//  = false;
	bool supportPointSprites;//  = false;
	bool supportShaders;//  = false;
	bool supportAntiAlias;//  = false;
	bool supportVBO;//  = false;
	bool supportPBO;//  = false;
	bool supportFBO;//  = false;
	bool supportNPOT;//  = false;
	bool supportOGL20;//  = false;
	bool supportWGLPixelFormat; 
	bool supportTexRects;

	bool useMasking;
	bool useEnvMapping;//  = true;
};

void getTextureData(GLuint tex, unsigned char *buf);

_VIDEO_API_ extern VideoSettings video;
_VIDEO_API_ extern TextureManager texturemanager;

void decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest);


#endif

