#ifndef VIDEO_H
#define VIDEO_H

#include <vector>
#include "GL/glew.h"
#include "GL/wglew.h"

struct VideoCaps
{
  int  colour;
  int alpha;
  int zBuffer;
  int accum;
  int stencil;
  int aaSamples;  // how many AA samples can this mode do?
  int hwAcc;  // Hardware Acceleration mode?
  GLboolean sampleBuffer;  // do we have an AA sample buffer?
  GLboolean doubleBuffer;  // double buffered?
};

class VideoSettings {

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

extern VideoSettings video;

#endif

