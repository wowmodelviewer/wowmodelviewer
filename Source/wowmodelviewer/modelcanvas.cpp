#include "modelcanvas.h"

#include "MiniExr.h"
#include "ximage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include <QImage>
#include <QImageReader>

#include <wx/dcclient.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/window.h>

#include "animcontrol.h"
#include "Attachment.h"
#include "GlobalSettings.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "Texture.h"
#include "TextureManager.h"
#include "video.h"
#include "ViewportHost.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "logger/Logger.h"


IMPLEMENT_CLASS(ModelCanvas, wxWindow)
BEGIN_EVENT_TABLE(ModelCanvas, wxWindow)
  EVT_SIZE(ModelCanvas::OnSize)
  EVT_PAINT(ModelCanvas::OnPaint)
  EVT_ERASE_BACKGROUND(ModelCanvas::OnEraseBackground)
  EVT_TIMER(ID_TIMER, ModelCanvas::OnTimer)
  EVT_MOUSE_EVENTS(ModelCanvas::OnMouse)
  EVT_KEY_DOWN(ModelCanvas::OnKey)
END_EVENT_TABLE()

namespace
{
  // Milliseconds of a steady clock (what timeGetTime gave the animation clock on Windows).
  unsigned long wmvMillis()
  {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - start).count();
  }

  const float NEAR_CLIP = 0.1f;
  const float FAR_CLIP = 1280.0f * 5.0f;
}

unsigned long ModelCanvas::s_clockTicks = 0;

ModelCanvas::ModelCanvas(wxWindow *parent)
{
  LOG_INFO << "Creating the viewport...";

  init = false;

  // Init time related stuff
  srand((unsigned)wmvMillis());
  time = 0;
  lastTime = wmvMillis();

  // Set all our pointers to null
  skyModel = nullptr;    // SkyBox Model
  wmo = nullptr;      // world map object model
  animControl = nullptr;
  gifExporter = nullptr;
  root = new Attachment(nullptr, nullptr, -1, -1);
  sky = new Attachment(nullptr, nullptr, -1, -1);
  model_ = nullptr;
  uiBGTexture = 0;
  ignoreMouse = 0;
  mx = my = 0;

  lightType = LIGHT_DYNAMIC;

  // Setup our default colour values.
  vecBGColor = glm::vec3((float)(71.0/255),(float)(95.0/255),(float)(121.0/255));

  drawLightDir = false;
  drawBackground = false;
  drawAVIBackground = false;
  drawSky = false;
  drawGrid = false;
  useCamera = false;

  openGLDebug_ = false;

  // A plain native child window: the Vulkan surface is made on it once it is realised (ensureRenderer). The
  // renderer paints every pixel, so nothing is erased first.
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE,
              wxT("ModelCanvas")))
  {
    LOG_ERROR << "Unable to create the viewport window.";
    return;
  }

  // The animation clock, which also draws the frames while the viewport is on screen.
  timer.SetOwner(this, ID_TIMER);
  timer.Start(TIME_STEP);

  video.render = true;
  init = true;
}

ModelCanvas::~ModelCanvas()
{
  timer.Stop();
#if defined(_WINDOWS)
  cAvi.ReleaseEngine();
#endif

  // The renderer goes first: it holds GPU copies of textures the manager is about to free.
  m_renderer.reset();

  if (uiBGTexture)
    TEXTUREMANAGER.del(uiBGTexture);
  uiBGTexture = 0;

  // Clear remaining textures.
  TEXTUREMANAGER.clear();

  // Clear model attachments
  clearAttachments();

  // A WMO still loaded at exit: detached from the root before the root goes (~Attachment writes into
  // its model) and freed.
  ClearWMO();

  wxDELETE(root);
  wxDELETE(sky);
}

void ModelCanvas::OnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
  // the renderer paints every pixel
}

void ModelCanvas::OnSize(wxSizeEvent& event)
{
  event.Skip();
  const wxSize s = GetClientSize();
  video.xRes = s.x;
  video.yRes = s.y;
  Refresh(false);
}

void ModelCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
  wxPaintDC dc(this); // validates the paint; the frame itself goes through the swapchain
  RenderFrame();
}

void ModelCanvas::OnTimer(wxTimerEvent& WXUNUSED(event))
{
  if (!video.render || !init)
    return;
  s_clockTicks++;
  tick();
  RenderFrame();
}

wxSize ModelCanvas::pixelSize() const
{
  const wxSize s = GetClientSize();
  const double scale = GetContentScaleFactor();
  return wxSize((int)std::lround(s.x * scale), (int)std::lround(s.y * scale));
}

bool ModelCanvas::ensureRenderer()
{
  if (m_renderer)
    return true;
  if (m_rendererTried)
    return false;
  if (!IsShownOnScreen() || GetClientSize().x <= 0 || GetClientSize().y <= 0)
    return false; // not realised yet: tried again on the next frame

  m_rendererTried = true;
  void * native = nullptr;
#if defined(__WXOSX__)
  native = (__bridge void *)GetHandle(); // the NSView
#elif defined(__WXMSW__)
  native = GetHWND();
#elif defined(__WXGTK__)
  native = (void *)GTKGetDrawingWindow();  // its GdkWindow (opaque here: GTK's headers clash with Qt's macros)
#endif
  std::string error;
  m_renderer = wmv::vk::Viewport::create(native, error);
  if (!m_renderer)
  {
    m_rendererError = wxString::FromUTF8(error.c_str());
    LOG_ERROR << "The viewport could not start:" << QString::fromStdString(error);
    if (ViewportHost * host = wxDynamicCast(GetParent(), ViewportHost))
      host->setNotice(_("The viewport could not start"),
                      m_rendererError + wxT("\n") +
                      _("WoW Model Viewer draws with Vulkan (MoltenVK on macOS). Update your graphics driver, then restart the viewer."));
    return false;
  }
  LOG_INFO << "Viewport renderer:" << QString::fromStdString(m_renderer->description());
  return true;
}

wmv::vk::FrameSettings ModelCanvas::frameSettings(bool transparent) const
{
  wmv::vk::FrameSettings s;
  if (video.useMasking)
  {
    s.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    s.lightingOff = true;
  }
  else
  {
    s.clearColor = glm::vec4(vecBGColor, transparent ? 0.0f : 1.0f);
    if (transparent)
      s.clearColor = glm::vec4(0.0f);
  }
  return s;
}

void ModelCanvas::addGrid(wmv::RenderList & list)
{
  // The archived viewport's floor: a 41 x 41 checkerboard of one-unit squares at z = 0, unlit.
  wmv::Material m;
  m.unlit = true;
  m.textured = false;
  const uint32_t first = list.indexMark();
  int count = 0;
  for (float i = -20.0f; i <= 20.0f; i += 1.0f)
    for (float j = -20.0f; j <= 20.0f; j += 1.0f, count++)
    {
      const float c = (count % 2) == 0 ? 1.0f : 0.2f;
      wmv::Vertex v[4];
      const glm::vec3 p[4] = { { j, i, 0 }, { j, i + 1, 0 }, { j + 1, i + 1, 0 }, { j + 1, i, 0 } };
      for (int k = 0; k < 4; k++)
      {
        v[k].pos = p[k];
        v[k].normal = glm::vec3(0, 0, 1);
        v[k].uv0 = v[k].uv1 = glm::vec2(0.0f);
        v[k].color = glm::vec4(c, c, c, 1.0f);
      }
      const uint32_t base = list.addVertices(v, 4);
      const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
      for (uint32_t q : quad)
        list.addIndex(base + q);
    }
  list.loadIdentity();
  list.draw(m, first);
}

void ModelCanvas::addBackground(wmv::RenderList & list)
{
  if (!uiBGTexture)
    return;
  // A full-viewport quad behind everything (clip space, no depth).
  wmv::Material m;
  m.unlit = true;
  m.depthTest = false;
  m.depthWrite = false;
  m.screenSpace = true;
  m.tex[0] = uiBGTexture;
  m.wrapS = m.wrapT = false;
  const glm::vec2 c[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
  const glm::vec2 t[4] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
  wmv::Vertex v[4];
  for (int k = 0; k < 4; k++)
  {
    v[k].pos = glm::vec3(c[k], 0.0f);
    v[k].normal = glm::vec3(0, 0, 1);
    v[k].uv0 = v[k].uv1 = t[k];
    v[k].color = glm::vec4(1.0f);
  }
  const uint32_t first = list.indexMark();
  const uint32_t base = list.addVertices(v, 4);
  const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
  for (uint32_t q : quad)
    list.addIndex(base + q);
  list.push();
  list.loadIdentity();
  list.draw(m, first, wmv::PRIM_TRIANGLES, -10);
  list.pop();
}

void ModelCanvas::buildFrame(wmv::RenderList & list, float aspect, bool backgroundImage)
{
  list.clear();
  wmv::RenderList::Scope scope(list);

  list.projection = glm::perspective(glm::radians(video.fov), aspect, NEAR_CLIP, FAR_CLIP);
  list.view = camera.getViewMatrix();
  if (useCamera && model_ && model_->hasCamera && !model_->cam.empty())
    const_cast<WoWModel *>(model_)->cam[0].setup(aspect);

  if (!video.useMasking)
  {
    if (backgroundImage && drawBackground)
      addBackground(list);
    if (drawGrid)
      addGrid(list);
  }

  root->draw();
  if (!video.useMasking)
    root->drawParticles();
}

void ModelCanvas::RenderFrame()
{
  if (!IsShownOnScreen())
    return;
  if (!ensureRenderer())
    return;
  const wxSize px = pixelSize();
  if (px.x <= 0 || px.y <= 0)
    return;

  if (openGLDebug_)
    displayDebugInfos();

  buildFrame(m_frame, (float)px.x / (float)px.y, true);
  m_renderer->render(m_frame, frameSettings(false), px.x, px.y);
}

bool ModelCanvas::captureRGBA(int w, int h, bool transparent, std::vector<unsigned char> & rgba)
{
  if (!m_renderer)
  {
    // A capture before the viewport was ever shown (the headless harness): an offscreen-only renderer.
    if (!m_rendererTried || !m_renderer)
    {
      std::string error;
      m_renderer = wmv::vk::Viewport::create(nullptr, error);
      if (!m_renderer)
      {
        LOG_ERROR << "No renderer for the capture:" << QString::fromStdString(error);
        return false;
      }
    }
  }
  if (w <= 0 || h <= 0)
  {
    const wxSize px = pixelSize();
    w = px.x > 0 ? px.x : 1024;
    h = px.y > 0 ? px.y : 768;
  }
  wmv::RenderList list;
  buildFrame(list, (float)w / (float)h, !transparent);
  static const bool debug = getenv("WMV_RENDER_DEBUG") != nullptr;
  if (debug)
  {
    const glm::vec3 eye = camera.position(), look = camera.lookAt();
    LOG_INFO << "[render] capture" << w << "x" << h << "draws" << (int)list.draws.size() << "vertices"
             << (int)list.vertices.size() << "indices" << (int)list.indices.size() << "camera" << eye.x << eye.y << eye.z
             << "->" << look.x << look.y << look.z << "model" << (model_ ? 1 : 0)
             << "showModel" << (model_ ? (int)model_->showModel : -1);
    for (size_t i = 0; i < list.draws.size() && i < 12; i++)
    {
      const wmv::Draw & d = list.draws[i];
      LOG_INFO << "[render]   draw" << (int)i << "indices" << (int)d.indexCount << "blend" << (int)d.material.blend
               << "tex" << d.material.tex[0] << "ps" << d.material.pixelShader << "alpha" << d.material.color.w
               << "cull" << d.material.cull;
    }
  }
  return m_renderer->capture(list, frameSettings(transparent), w, h, rgba);
}

bool ModelCanvas::Screenshot(const wxString fn, int x, int y)
{
  std::vector<unsigned char> rgba;
  const bool transparent = false;
  if (!captureRGBA(x, y, transparent, rgba))
  {
    LOG_ERROR << "Screenshot failed: the frame could not be drawn.";
    return false;
  }
  const wxSize size = (x > 0 && y > 0) ? wxSize(x, y) : pixelSize();
  const int w = (x > 0 && y > 0) ? x : (size.x > 0 ? size.x : 1024);
  const int h = (x > 0 && y > 0) ? y : (size.y > 0 ? size.y : 768);

  LOG_INFO << "Saving screenshot in : " << QString::fromWCharArray(fn.c_str());
  wxFileName temp(fn, wxPATH_NATIVE);
  if (temp.GetExt().Lower() == wxT("tga")) // Qt does not write tga
  {
    std::vector<unsigned char> bgra(rgba.size());
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
      bgra[i] = rgba[i + 2]; bgra[i + 1] = rgba[i + 1]; bgra[i + 2] = rgba[i]; bgra[i + 3] = rgba[i + 3];
    }
    CxImage newImage;
    newImage.CreateFromArray(bgra.data(), w, h, 32, w * 4, true); // top row first
    return newImage.Save(fn.fn_str(), CXIMAGE_FORMAT_TGA);
  }
  const QImage image(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888);
  return image.convertToFormat(QImage::Format_RGB32).save(QString::fromWCharArray(fn.c_str()));
}

// Render the current model off-screen at an arbitrary resolution and write one image-sequence
// frame (0=PNG/RGBA, 1=JPG/RGB, 2=EXR/float).
//
// Transparency: WoW models draw with MANY blend modes, so the framebuffer's own alpha channel is
// NOT a clean matte -- trusting it makes opaque pixels semi-transparent and the model looks
// "see-through". So for a transparent export we never read that alpha; instead we render the model
// twice (once over BLACK, once over WHITE) and reconstruct true coverage and straight (un-
// premultiplied) colour: over black a pixel = a*C, over white = a*C + (1-a), so
// a = 1 - (white - black) and C = black / a. This is correct for every blend mode.
bool ModelCanvas::CaptureSequenceFrame(const wxString & fn, int w, int h, int imgFormat, bool transparent)
{
  if (!model_)
    return false;

  const bool savedMask = video.useMasking;
  const glm::vec3 savedBg = vecBGColor;
  const bool savedDrawBg = drawBackground, savedDrawSky = drawSky;
  drawBackground = false; drawSky = false;
  video.useMasking = false;

  auto renderOver = [&](float c, std::vector<unsigned char> & out) {
    vecBGColor = glm::vec3(c, c, c);
    return captureRGBA(w, h, false, out);
  };

  std::vector<unsigned char> cb, cw;
  bool ok = renderOver(0.0f, cb);
  const bool matte = ok && transparent && imgFormat != 1;
  if (matte)
    ok = renderOver(1.0f, cw);

  drawBackground = savedDrawBg; drawSky = savedDrawSky;
  video.useMasking = savedMask; vecBGColor = savedBg;
  if (!ok)
  {
    LOG_ERROR << "[imgseq] the frame could not be drawn:" << QString::fromWCharArray(fn.c_str());
    return false;
  }

  const size_t N = (size_t)w * h;
  if (imgFormat == 2)
  {
    std::vector<float> px(N * 4);
    for (size_t i = 0; i < N; i++)
    {
      float r = cb[i*4] / 255.f, g = cb[i*4+1] / 255.f, b = cb[i*4+2] / 255.f, a = 1.0f;
      if (matte)
      {
        a = 1.0f - ((cw[i*4]/255.f - r) + (cw[i*4+1]/255.f - g) + (cw[i*4+2]/255.f - b)) / 3.0f;
        a = a < 0 ? 0 : (a > 1 ? 1 : a);
        if (a > 1e-4f) { r /= a; g /= a; b /= a; } else { r = g = b = 0; }
      }
      px[i*4] = r; px[i*4+1] = g; px[i*4+2] = b; px[i*4+3] = a;
    }
    ok = MiniExr::writeRGBAF(fn.ToStdWstring(), w, h, px.data(), false /* top row first */);
  }
  else
  {
    QImage image(w, h, (imgFormat == 1) ? QImage::Format_RGB32 : QImage::Format_ARGB32);
    for (int y = 0; y < h; y++)
    {
      unsigned char * out = image.scanLine(y);
      for (int x = 0; x < w; x++)
      {
        const size_t i = (size_t)y * w + x;
        float R = cb[i*4] / 255.f, G = cb[i*4+1] / 255.f, B = cb[i*4+2] / 255.f, a = 1.0f;
        if (matte)
        {
          a = 1.0f - ((cw[i*4]/255.f - R) + (cw[i*4+1]/255.f - G) + (cw[i*4+2]/255.f - B)) / 3.0f;
          a = a < 0 ? 0 : (a > 1 ? 1 : a);
          if (a > 4e-3f) { R /= a; G /= a; B /= a; }
        }
        out[x*4+0] = (unsigned char)(std::min(1.f, B) * 255.f + 0.5f);
        out[x*4+1] = (unsigned char)(std::min(1.f, G) * 255.f + 0.5f);
        out[x*4+2] = (unsigned char)(std::min(1.f, R) * 255.f + 0.5f);
        out[x*4+3] = (unsigned char)(a * 255.f + 0.5f);
      }
    }
    ok = image.save(QString::fromWCharArray(fn.c_str()));
  }
  if (!ok)
    LOG_ERROR << "[imgseq] failed to write frame:" << QString::fromWCharArray(fn.c_str());
  return ok;
}

void ModelCanvas::LoadWMO(wxString fn, int fileDataID)
{
  // A WMO still here would be replaced without anything being told (its pointer was simply
  // overwritten before): drop it the one safe way first.
  ClearWMO();

  // Fully loaded: its groups record their geometry for the viewport (WMOGroup::initDisplayList).
  wmo = new WMO(QString::fromWCharArray(fn.c_str()), fileDataID > 0 ? (uint32)fileDataID : 0, false);
  root->setModel(wmo);

  if (wmo->ok && wmo->nGroups > 0) {
    const glm::vec3 mn = glm::min(wmo->v1, wmo->v2);
    const glm::vec3 mx = glm::max(wmo->v1, wmo->v2);
    const glm::vec3 center = (mn + mx) * 0.5f;
    const glm::vec3 d = mx - center;
    const float radius = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (radius > 0.0f)
      camera.frameBounds(center, radius);
  }
}

void ModelCanvas::LoadBackground(wxString filename)
{
  if (!wxFile::Exists(filename))
    return;

  bgImagePath = filename;

  QImage image;
  if (!image.load(QString::fromWCharArray(filename.c_str())))
  {
    LOG_ERROR << "Failed to load background" << QString::fromWCharArray(filename.c_str());
    LOG_INFO << "Supported formats:" << QImageReader::supportedImageFormats();
    return;
  }
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (!uiBGTexture)
    uiBGTexture = TEXTUREMANAGER.create("Viewport background");
  TEXTUREMANAGER.setPixels(uiBGTexture, image.width(), image.height(), image.constBits(), TEXTURE_RGBA);
  drawBackground = true;
}

void ModelCanvas::LoadSceneState(int id)
{
  if (!model_)
    return;

  WoWModel * m = const_cast<WoWModel *>(model_);
  if (id > -1 && id < 4) {
    video.fov = sceneState[id].fov;
    m->pos_ = sceneState[id].pos;
    m->rot_ = sceneState[id].rot;
  }
}

Attachment* ModelCanvas::LoadModel(GameFile * file)
{
  clearAttachments();
  root->setModel(nullptr);
  ClearWMO();

  // Free the previously displayed model. clearAttachments()/setModel(nullptr) only
  // detach it and delete the Attachment wrappers -- the WoWModel itself was never
  // freed, so every model switch leaked the whole model + its textures/geometry,
  // eventually exhausting the 32-bit address space (OOM crash). Deleting it here is
  // safe now that it's detached and the merge path no longer shares its geoset/pass
  // pointers (see refreshMerging). delete on nullptr (first load) is a no-op.
  delete model_;
  model_ = nullptr;

  // Create new one
  model_ = new WoWModel(file, true);
  if (!model_->ok)
  {
    LOG_INFO << "Model is not OK !";
    model_ = nullptr;
    return nullptr;
  }

  auto *att = root->addChild(model_, 0, -1);

  camera.reset(model_);

  // The model's clock starts on the first tick after this load, not now: see tick().
  restartClock = true;

  return att;
}

void ModelCanvas::ClearWMO()
{
  if (!wmo)
    return;
  // Detach the root first: Attachment::setModel and ~Attachment both write into the model they hold,
  // so a root left on a deleted WMO was a use-after-free write on the next load of anything, or at exit.
  if (root && root->model() == wmo)
    root->setModel(nullptr);
  // The doodad-set list acts on g_selWMO; one left on a deleted WMO dangled until the next WMO.
  if (g_selWMO == wmo)
    g_selWMO = nullptr;
  delete wmo;
  wmo = nullptr;
}

void ModelCanvas::clearAttachments()
{
  if (root)
    root->delChildren();

  if (sky)
    sky->delChildren();
}

void ModelCanvas::OnMouse(wxMouseEvent& event)
{
  // mul = multiplier in which to multiply everything to achieve a sense of control over the amount to move stuff by
  float mul = 1.0f;
  if (event.m_shiftDown)
    mul /= 10;
  /*
  if (event.m_controlDown)
    mul *= 10;
  if (event.m_altDown)
    mul *= 50;
   */

  static glm::vec2 lastMousePos = glm::vec2(0.0f, 0.0f);
  const float MOUSE_SENSITIVITY = 0.25f;

  if (event.GetEventType() == wxEVT_MOUSEWHEEL)
  {
    // Multiplicative zoom (dolly): each notch SCALES the orbit distance, so zooming stays fast
    // far out and precise up close regardless of model size. The old additive step (~0.5 units
    // per notch) was unusably slow on WMOs, which sit hundreds-to-thousands of units away.
    const int wheelDelta = event.GetWheelDelta() ? event.GetWheelDelta() : 120;
    const float notches = (float)event.GetWheelRotation() / (float)wheelDelta;
    const float perNotch = event.m_shiftDown ? 0.95f : 0.82f; // Shift = finer steps
    camera.setRadius(camera.radius() * powf(perNotch, notches));
  }
  else if (event.Dragging())
  {
    const auto deltax = ((float)event.GetX() - lastMousePos.x) * MOUSE_SENSITIVITY * mul;
    const auto deltay = ((float)event.GetY() - lastMousePos.y) * MOUSE_SENSITIVITY * mul;

    if (event.LeftIsDown())
    {
      const auto yaw = -deltax;
      const auto pitch = -deltay;
      camera.setYawAndPitch(camera.yaw() + yaw, camera.pitch() + pitch);
    }
    else if(event.RightIsDown())
    {
      // Pan proportionally to the orbit distance, so it's usable on big WMOs (a fixed step
      // barely nudged them) while staying fine on small models.
      const auto x = deltax * camera.radius() * 0.0025f;
      const auto y = deltay * camera.radius() * 0.0025f;

      const auto look = camera.lookAt();
      const auto right = camera.right();

      camera.setLookAt(glm::vec3(look.x + right.x * -x, look.y + right.y * -x, look.z + y));
    }
    else if(event.MiddleIsDown())
    {
      // Multiplicative dolly (drag down = zoom out), matching the wheel.
      camera.setRadius(camera.radius() * powf(1.01f, deltay));
    }
  }
  
  lastMousePos.x = event.GetX();
  lastMousePos.y = event.GetY();
}

void ModelCanvas::OnKey(wxKeyEvent &event)
{
  // error checking
  if(!model_) 
    return;
  
  // --
  const auto keycode = event.GetKeyCode();

  if (keycode == '0')
    animControl->SetAnimSpeed(1.0f);
  else if (keycode == '1')
    animControl->SetAnimSpeed(0.1f);
  else if (keycode == '2')
    animControl->SetAnimSpeed(0.2f);
  else if (keycode == '3')
    animControl->SetAnimSpeed(0.3f);
  else if (keycode == '4')
    animControl->SetAnimSpeed(0.4f);
  else if (keycode == '5')
    animControl->SetAnimSpeed(0.5f);
  else if (keycode == '6')
    animControl->SetAnimSpeed(0.6f);
  else if (keycode == '7')
    animControl->SetAnimSpeed(0.7f);
  else if (keycode == '8')
    animControl->SetAnimSpeed(0.8f);
  else if (keycode == '9')
    animControl->SetAnimSpeed(0.9f);
}

void ModelCanvas::OnCamMenu(wxCommandEvent &event)
{
  const auto id = event.GetId();
  
  if (id == ID_CAM_FRONT)
    camera.setYawAndPitch(0., 90.);
  else if (id == ID_CAM_BACK)
    camera.setYawAndPitch(180., 90.);
  else if (id == ID_CAM_SIDE)
    camera.setYawAndPitch(270., 90.);
  else if (id == ID_CAM_ISO)
    camera.setYawAndPitch(315., 90.);
  else if (id == ID_CAM_RESET)
    camera.reset(model_);
}

void ModelCanvas::CheckMovement()
{
  // Make sure its the canvas that has focus before continuing
  wxWindow *win = wxWindow::FindFocus();
  if(!win)
    return;

  // Its no longer an opengl canvas window, its now just a standard window.
  // wxWindow *gl = wxDynamicCast(win, wxGLCanvas);
  wxWindow *wintest = wxDynamicCast(win, wxWindow);
  if(!wintest)
    return;

  if (wxGetKeyState(WXK_NUMPAD4))  // Rotate left
    camera.setYaw(camera.yaw() + 1.0f);
  if (wxGetKeyState(WXK_NUMPAD6))  // Rotate right
    camera.setYaw(camera.yaw() - 1.0f);
  if (wxGetKeyState(WXK_NUMPAD8))  // Rotate back
    camera.setPitch(camera.pitch() + 1.0f);
  if (wxGetKeyState(WXK_NUMPAD2))  // Rotate fornt
    camera.setPitch(camera.pitch() - 1.0f);
  if (wxGetKeyState(WXK_NUMPAD5))  // Reset Camera
    camera.reset(model_);
  if (wxGetKeyState(WXK_NUMPAD7))  // Look upper
    camera.setLookAt(glm::vec3(camera.lookAt().x, camera.lookAt().y, camera.lookAt().z + 0.2f));
  if (wxGetKeyState(WXK_NUMPAD9))  // Reset lower
    camera.setLookAt(glm::vec3(camera.lookAt().x, camera.lookAt().y, camera.lookAt().z - 0.2f));
  if (wxGetKeyState(WXK_NUMPAD1))  // Look left
  {
    const auto look = camera.lookAt();
    const auto right = camera.right();

    camera.setLookAt(glm::vec3(look.x + right.x * -0.2, look.y + right.y * -0.2, look.z));
  }
  if (wxGetKeyState(WXK_NUMPAD3))  // Look right
  {
    const auto look = camera.lookAt();
    const auto right = camera.right();

    camera.setLookAt(glm::vec3(look.x + right.x * 0.2, look.y + right.y * 0.2, look.z));
  }
}

void ModelCanvas::SaveSceneState(int id)
{
  if (!model_)
    return;

  // bounds check
  if (id > -1 && id < 4) {
    sceneState[id].pos = model_->pos_;
    sceneState[id].rot = model_->rot_;
    sceneState[id].fov = video.fov;
  }
}

void ModelCanvas::displayDebugInfos() const
{
  static wxString appTitle = GLOBALSETTINGS.appTitle();
  static int frameCount = 0;

  static DWORD previousTime = 0;
  const auto currentTime = wmvMillis();
  const auto elapsedTime = (currentTime - previousTime) / 1000.0;

  if(elapsedTime > 0.25) // update 4 times per second
  {
    previousTime = currentTime;
    const auto fps = (double)frameCount / elapsedTime;
    const auto msPerFrame = 1000.0 / fps;

    wxString title;
    const auto look = camera.lookAt();
    const auto pos = camera.position();
    const auto right = camera.right();
    title.Printf(L"%s - FPS: %0.1f - Frame Time: %0.1f (ms) - Camera: Y=%0.1f P=%0.1f r=%0.1f LookAt: %0.1f/%0.1f/%0.1f Pos: %0.1f/%0.1f/%0.1f Right: %0.1f/%0.1f/%0.1f", 
      appTitle, fps, msPerFrame, camera.yaw(), camera.pitch(), camera.radius(), look.x, look.y, look.z, pos.x, pos.y, pos.z, right.x, right.y, right.z);
    
    ((wxTopLevelWindow*)wxTheApp->GetTopWindow())->SetTitle(title);

    frameCount = 0;
  }

  frameCount++;
}

void ModelCanvas::toggleOpenGLDebug()
{
  openGLDebug_ = !openGLDebug_;

  if (!openGLDebug_)
    ((wxTopLevelWindow*)wxTheApp->GetTopWindow())->SetTitle(GLOBALSETTINGS.appTitle());
}

void ModelCanvas::setModel(WoWModel * m, bool keepPrevious)
{
  if(!keepPrevious)
    delete model_;

  model_ = m;
}

void ModelCanvas::tick()
{
  size_t ddt = 0;

  // Time stuff
  //time = float();
  ddt = (wmvMillis() - lastTime);// * animSpeed;
  lastTime = wmvMillis();
  AnimManager::StartTick();
  // --

  if (restartClock)
  {
    // THE FIRST TICK AFTER A LOAD. lastTime dates from before it, so ddt is the whole wall time
    // the load took -- reading the model, half a second of database work in the animation
    // control, the layout. Loading is not animation time: the animation starts from this tick, at
    // the frame it was left at (0).
    ddt = 0;
    restartClock = false;
  }

  globalTime += (ddt);

  if (model_) {
    if (model_->animManager && !wmo) {
      if (model_->animManager->IsPaused())
        ddt = 0;
/*    
      if (!model_->animManager->IsParticlePaused())
        ddt = model_->animManager->GetTimeDiff();
*/
    }
    
    root->tick(ddt);
  }

  if (drawSky && sky && skyModel) {
    sky->tick(ddt);
  }

}
