#pragma once

#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/gl.h>
#include <GL/glu.h>
#ifdef _WIN32
#include <glad/wgl.h>
#endif

struct VideoCaps
{
	int colour;
	int alpha;
	int zBuffer;
	int accum;
	int stencil;
	int aaSamples; // how many AA samples can this mode do?
	int hwAcc; // Hardware Acceleration mode?
	GLboolean sampleBuffer; // do we have an AA sample buffer?
	GLboolean doubleBuffer; // double buffered?
};

class VideoSettings
{
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
	HWND GetHandle() { return hWnd; }
	void SetHandle(HWND hwnd, int bpp);

	// Resources
	HDC hDC   = nullptr;
	HWND hWnd = nullptr;
	HGLRC hRC = nullptr;
#endif

	bool init    = false;
	bool render  = false;
	bool refresh = false;

	// OpenGL Settings
	int xRes = 0;
	int yRes = 0;
	int pixelFormat = 0;
	float fov = 45.0f;
	int desktopBPP = 0;

	// Card capabilities
	std::vector<VideoCaps> capsList;
	VideoCaps curCap{};
	int capIndex = 0;

	int AnisofilterLevel = 0;
	int numTextureUnits  = 0;

	// Card Info
	char* vendor   = nullptr;
	char* version  = nullptr;
	char* renderer = nullptr;

	// Is there hardware support?
	bool hasHardware = false;
	bool secondPass  = false;

	// Video card support for OGL Extensions
	bool supportFragProg            = false;
	bool supportVertexProg          = false;
	bool supportGLSL                = false;
	bool supportCompression         = false;
	bool supportMultiTex            = false;
	bool supportDrawRangeElements   = false;
	bool supportPointSprites        = false;
	bool supportShaders             = false;
	bool supportAntiAlias           = false;
	bool supportVBO                 = false;
	bool supportPBO                 = false;
	bool supportFBO                 = false;
	bool supportNPOT                = false;
	bool supportOGL20               = false;
	bool supportWGLPixelFormat      = false;
	bool supportTexRects            = false;

	bool useMasking    = false;
	bool useEnvMapping = true;
};

extern VideoSettings video;
