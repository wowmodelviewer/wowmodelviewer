#ifndef MODELCANVAS_H
#define MODELCANVAS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// wx
#include <wx/window.h>

// stl
#include <memory>
#include <string>
#include <vector>

// our headers
#include "AnimExporter.h"
#if defined (_WINDOWS)
#include "AVIGenerator.h"
#endif
#include "lightcontrol.h"
#include "OrbitCamera.h"
#include "RenderList.h"
#include "util.h"
#include "video.h"
#include "VulkanViewport.h"
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

  float fov;  // Field of View
};


// THE VIEWPORT. The scene -- the loaded model and its attachments, a WMO, the sky -- its camera and its animation
// clock, drawn natively: every frame the scene records what it draws into a wmv::RenderList (the model code does,
// where it once called OpenGL) and the Vulkan viewport renders it. Screenshots and image sequences render the same
// list offscreen at the size asked for.
class ModelCanvas : public wxWindow
{
  DECLARE_CLASS(ModelCanvas)
  DECLARE_EVENT_TABLE()

public:
  explicit ModelCanvas(wxWindow *parent);
  ~ModelCanvas();

  // GUI Control Panels
  AnimControl *animControl;
  GifExporter *gifExporter;

  void OnPaint(wxPaintEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnMouse(wxMouseEvent& event);
  void OnKey(wxKeyEvent &event);
  void OnCamMenu(wxCommandEvent &event);

  void OnEraseBackground(wxEraseEvent& event);
  void OnTimer(wxTimerEvent& event);
  void tick();
  wxTimer timer;

  // Draws a frame now, if the viewport is on screen.
  void RenderFrame();

  // Ticks of the animation clock so far (the headless harness checks it runs).
  static unsigned long s_clockTicks;

  // Writes the current view at x by y pixels (0: the viewport's size) to fn; the format follows the extension.
  bool Screenshot(const wxString fn, int x=0, int y=0);
  // One image-sequence frame at w by h: 0 = PNG (RGBA), 1 = JPG, 2 = EXR. Transparent: over a true matte.
  bool CaptureSequenceFrame(const wxString & fn, int w, int h, int imgFormat, bool transparent);
  // The current view at w by h, RGBA, top row first, over the background colour (or a transparent black when
  // 'transparent'). The animation exporters read frames through this.
  bool captureRGBA(int w, int h, bool transparent, std::vector<unsigned char> & rgba);

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

  // Replaces any WMO with the root at fn (fileDataID: the root's, when known).
  void LoadWMO(wxString fn, int fileDataID = 0);
  // Deletes the WMO, if any, and every pointer to it first: canvas->root (whose next setModel or
  // destructor writes into the model it holds) and g_selWMO (the doodad-set list's target). The only
  // way a WMO may be deleted.
  void ClearWMO();

  // Various toggles
  bool init;   // the canvas is built (textures and models need no graphics context any more)
  bool drawLightDir, drawBackground, drawSky, drawGrid, drawAVIBackground;
  bool useCamera;

  int lightType;  // MODEL / AMBIENCE / DYNAMIC
  int ignoreMouse;

  // Models / Attachments
  WoWModel *skyModel;
  WMO *wmo;

  Attachment *root;
  Attachment *sky;

  // Attachment related functions
  void clearAttachments();

  // Background colour
  glm::vec3 vecBGColor;

  // Background image
  TextureID uiBGTexture;
  void LoadBackground(wxString filename);
#if defined(_WINDOWS)
  CAVIGenerator cAvi;
#endif

  void toggleOpenGLDebug();

  // The renderer: started when the window first has a native surface and a size. Null until then, or when Vulkan
  // could not start (rendererError says why).
  wmv::vk::Viewport * renderer() const { return m_renderer.get(); }
  const wxString & rendererError() const { return m_rendererError; }
  // The viewport's size in backing pixels (twice the window's on a Retina display).
  wxSize pixelSize() const;

private:
  bool ensureRenderer();
  // Records the scene for a view of the given aspect ratio. backgroundImage: the background picture (screen views;
  // captures of the model alone leave it out).
  void buildFrame(wmv::RenderList & list, float aspect, bool backgroundImage);
  void addGrid(wmv::RenderList & list);
  void addBackground(wmv::RenderList & list);
  wmv::vk::FrameSettings frameSettings(bool transparent) const;
  void displayDebugInfos() const;

  float time;
  unsigned long lastTime;
  // Set by LoadModel; the first tick after it starts the animation clock from that tick
  // instead of charging the model the wall time its load took. See tick().
  bool restartClock = false;
  SceneState sceneState[4]; // 4 scene states for F1-F4

  OrbitCamera camera;

  WoWModel * model_;
  bool openGLDebug_;

  std::unique_ptr<wmv::vk::Viewport> m_renderer;
  bool m_rendererTried = false;
  wxString m_rendererError;
  wmv::RenderList m_frame;
};


#endif
