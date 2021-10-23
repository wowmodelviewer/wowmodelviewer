#ifndef MODELCANVAS_H
#define MODELCANVAS_H

#include "GL/glew.h"
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// wx
#include <wx/glcanvas.h>

// stl
#include <string>

// our headers
#include "AnimExporter.h"
#if defined (_WINDOWS)
#include "AVIGenerator.h"
#endif
#include "lightcontrol.h"
#include "maptile.h"
#include "OrbitCamera.h"
#include "RenderTexture.h"
#include "ShaderProgram.h"
#include "util.h"
#include "wmo.h"
#include "WoWModel.h"

#include "glm/glm.hpp"

// custom objects
class Attachment;

class AnimControl;
class GifExporter;
class LightControl;

class ModelViewer;
class ModelCanvas;

// 100 fps?
const int TIME_STEP = 10; // 10 millisecs between each frame


struct SceneState {
  glm::vec3 pos;  // Model Position
  glm::vec3 rot;  // Model Rotation

  float fov;  // OpenGL Field of View
};


class ModelCanvas: public wxGLCanvas
{
  DECLARE_CLASS(ModelCanvas)
  DECLARE_EVENT_TABLE()

public:
  ModelCanvas(wxWindow *parent, int * args);
  ~ModelCanvas();

  // GUI Control Panels
  AnimControl *animControl;
  GifExporter *gifExporter;

  RenderTexture *rt;

  // Event Handlers
  void Render(wxPaintEvent& WXUNUSED(event));
  void OnSize(wxSizeEvent& event);
  void OnMouse(wxMouseEvent& event);
  void OnKey(wxKeyEvent &event);
  void OnCamMenu(wxCommandEvent &event);

  //void OnIdle(wxIdleEvent& event);
  void OnEraseBackground(wxEraseEvent& event);
  void OnTimer(wxTimerEvent& event);
  void tick();
  wxTimer timer;

  // OGL related functions
  void InitView();

  // Main render routines which call the sub routines
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

  void setModel(WoWModel * m, bool keepPrevious = false);
  WoWModel const * model() const { return model_; }

  // view:
  glm::vec3 vRot0;
  glm::vec3 vPos0;
  wxCoord mx, my;

  void CheckMovement();  // move the character
  
  Attachment* LoadModel(GameFile *);

  void LoadWMO(wxString fn);
  void LoadADT(wxString fn);
  //void TogglePause();

  void testGL();
  
  // Various toggles
  bool init;
  bool drawLightDir, drawBackground, drawSky, drawGrid, drawAVIBackground;
  bool useCamera; //, useLights;

  int lightType;  // MODEL / AMBIENCE / DYNAMIC
  int ignoreMouse;

  // Models / Attachments
  WoWModel *skyModel;
  WMO *wmo;
  MapTile *adt;

  Attachment *root;
  Attachment *sky;

  // Attachment related functions
  void clearAttachments();
  //int addAttachment(const char *model, Attachment *parent, int id, int slot);
  //void deleteSlot(int slot);

  // Background colour
  glm::vec3 vecBGColor;

  // Backgroun image stuff
  GLuint uiBGTexture;
  void LoadBackground(wxString filename);
#if defined(_WINDOWS)
  CAVIGenerator cAvi;
#endif

  void toggleOpenGLDebug();

private:
  void displayDebugInfos() const;
  int getWidth() const;
  int getHeight() const;

  void InitGL();

  float time;
  DWORD lastTime;
  //DWORD pauseTime;
  SceneState sceneState[4]; // 4 scene states for F1-F4

  GLuint fogTex;

  OrbitCamera camera;

  WoWModel * model_;
  bool openGLDebug_;


  Model * testModel_;
  wxGLContext* context_;
  ShaderProgram shaderProgram_;
  GLuint vao_;
};


#endif

