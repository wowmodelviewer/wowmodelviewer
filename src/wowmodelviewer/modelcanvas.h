#ifndef MODELCANVAS_H
#define MODELCANVAS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// gl
#include "OpenGLHeaders.h"

// wx
#include <wx/glcanvas.h>
#include <wx/window.h>

// stl
#include <string>

// our headers
#include "AnimExporter.h"
#include "ArcBallCamera.h"
#if defined (_WINDOWS)
#include "AVIGenerator.h"
#endif
#include "BaseCanvas.h"
#include "enums.h"
#include "lightcontrol.h"
#include "maptile.h"
#include "RenderTexture.h"
#include "util.h"
#include "wmo.h"
#include "WoWModel.h"

// custom objects
class ArcBallCameraControl;

class Attachment;

class AnimControl;
class GifExporter;
class LightControl;

class ModelViewer;
class ModelCanvas;

// 100 fps?
const int TIME_STEP = 10; // 10 millisecs between each frame


struct SceneState {
	Vec3D pos;	// Model Position
	Vec3D rot;	// Model Rotation

	float fov;  // OpenGL Field of View
};


class ModelCanvas:
#ifdef _WINDOWS
		public wxWindow
#else
		public wxGLCanvas
#endif
		, public BaseCanvas
//class ModelCanvas: public wxGLCanvas
{
	DECLARE_CLASS(ModelCanvas)
    DECLARE_EVENT_TABLE()

public:
	ModelCanvas(wxWindow *parent, VideoCaps *cap = NULL);
	~ModelCanvas();

	// GUI Control Panels
	AnimControl *animControl;
	GifExporter *gifExporter;

	RenderTexture *rt;

	// Event Handlers
	void OnPaint(wxPaintEvent& WXUNUSED(event));
	void OnSize(wxSizeEvent& event);
	void OnMouse(wxMouseEvent& event);
	void OnKey(wxKeyEvent &event);

	//void OnIdle(wxIdleEvent& event);
	void OnEraseBackground(wxEraseEvent& event);
	void OnTimer(wxTimerEvent& event);
	void tick();
	wxTimer timer;

	// OGL related functions
	void InitGL();
	void InitView();
	void InitShaders();
	void UninitShaders();
	void ResetView();
	void ResetViewWMO(int id);

	// Main render routines which call the sub routines
	void Render();
	void RenderToTexture();
	void RenderModel();
	void RenderWMO();
	void RenderADT();
	void RenderToBuffer();
	void RenderWMOToBuffer();
	void RenderLight(Light *l);

	// Render sub routines
	void RenderSkybox();
	void RenderObjects();
	void RenderBackground();
	void RenderGrid();
	//void GenerateShadowMap();

	void Screenshot(const wxString fn, int x=0, int y=0);
	void SaveSceneState(int id);
	void LoadSceneState(int id);

	void SetCurrent();
	void SwapBuffers();

	void CheckMovement();	// move the character
	
	Attachment* LoadModel(GameFile *);
	Attachment* LoadCharModel(GameFile *);
#if 0
	Attachment* AddModel(const char *fn);
#endif
	void LoadWMO(wxString fn);
	void LoadADT(wxString fn);
	//void TogglePause();
	
	// Various toggles
	bool init;
	bool initShaders;
	bool drawLightDir, drawBackground, drawSky, drawGrid, drawAVIBackground;
	bool useCamera; //, useLights;

	int lightType;	// MODEL / AMBIENCE / DYNAMIC
	int ignoreMouse;

	// Models / Attachments
	WoWModel *skyModel;
	WMO *wmo;
	MapTile *adt;

	Attachment *root;
	Attachment *sky;
	Attachment *curAtt;

	// Attachment related functions
	void clearAttachments();
	//int addAttachment(const char *model, Attachment *parent, int id, int slot);
	//void deleteSlot(int slot);

	// Background colour
	Vec3D vecBGColor;

	// Backgroun image stuff
	GLuint uiBGTexture;
	void LoadBackground(wxString filename);
#if defined(_WINDOWS)
	CAVIGenerator cAvi;
#endif

private:
  float time, modelsize;
  DWORD lastTime;
  //DWORD pauseTime;
  SceneState sceneState[4]; // 4 scene states for F1-F4

  GLuint fogTex;

  bool fxBlur, fxGlow, fxFog;

  ArcBallCameraControl * m_p_cameraCtrl;
  ArcBallCamera arcCamera;
};


#endif

