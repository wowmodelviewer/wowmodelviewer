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

/// @brief Describes a single video mode's capabilities (colour depth, AA, etc.).
struct VideoCaps
{
	int colour;          ///< Colour buffer bit depth.
	int alpha;           ///< Alpha buffer bit depth.
	int zBuffer;         ///< Depth buffer bit depth.
	int accum;           ///< Accumulation buffer bit depth.
	int stencil;         ///< Stencil buffer bit depth.
	int aaSamples;       ///< Number of anti-aliasing samples.
	int hwAcc;           ///< Hardware acceleration mode.
	GLboolean sampleBuffer; ///< True if an AA sample buffer is available.
	GLboolean doubleBuffer; ///< True if double-buffered.
};

/// @brief Legacy OpenGL video-settings manager.
///
/// Enumerates display modes, negotiates pixel formats, and tracks
/// GPU capabilities.  Retained for backward compatibility with the
/// fixed-function rendering path.
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
