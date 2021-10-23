#include "modelcanvas.h"

#include "ximage.h"
#include <QImage>
#include <QImageWriter>
#include <QImageReader>
#include <glm/gtc/type_ptr.hpp>

#include <wx/display.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/window.h>

#include "animcontrol.h"
#include "Attachment.h"
#include "GlobalSettings.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "video.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "Logger.h"
#include "ModelRenderPass.h"

IMPLEMENT_CLASS(ModelCanvas, wxGLCanvas)
BEGIN_EVENT_TABLE(ModelCanvas, wxGLCanvas)
  EVT_SIZE(ModelCanvas::OnSize)
  EVT_PAINT(ModelCanvas::Render)
  EVT_ERASE_BACKGROUND(ModelCanvas::OnEraseBackground)
  EVT_TIMER(ID_TIMER, ModelCanvas::OnTimer)
  EVT_MOUSE_EVENTS(ModelCanvas::OnMouse)
  EVT_KEY_DOWN(ModelCanvas::OnKey)
END_EVENT_TABLE()

void ModelCanvas::testGL()
{
  shaderProgram_.loadShaders("shaders/basic.vs", "shaders/basic.fs");
}


void drawPoint(const glm::vec3 & coord, const glm::vec3 & color)
{
  glPushMatrix();
  glTranslatef(coord.x, coord.y, coord.z);
  glBegin(GL_LINES);

  glColor3f(color.x, color.y, color.z);
  glVertex3f(-0.05f, 0.0, 0.0);
  glVertex3f(0.05f, 0.0, 0.0);

  glColor3f(color.x, color.y, color.z);
  glVertex3f(0.0, -0.05f, 0.0);
  glVertex3f(0.0, 0.05f, 0.0);

  glColor3f(color.x, color.y, color.z);
  glVertex3f(0.0, 0.0, -0.05f);
  glVertex3f(0.0, 0.0, 0.05f);
  glEnd();

  glPopMatrix();
}

void drawAxis(const glm::vec3 & coord, float size, const glm::vec3 & xcolor, const glm::vec3 & ycolor, const glm::vec3 & zcolor)
{
  glPushMatrix();
  glTranslatef(coord.x, coord.y, coord.z);
  glBegin(GL_LINES);

  // X axis
  glColor3f(xcolor.x, xcolor.y, xcolor.z);
  glVertex3f(0.0, 0.0, 0.0);
  glVertex3f(size, 0.0, 0.0);

  // Y axis
  glColor3f(ycolor.x, ycolor.y, ycolor.z);
  glVertex3f(0.0, 0.0, 0.0);
  glVertex3f(0.0, size, 0.0);

  // Z axis
  glColor3f(zcolor.x, zcolor.y, zcolor.z);
  glVertex3f(0.0, 0.0, 0.0);
  glVertex3f(0.0, 0.0, size);
  glEnd();

  glPopMatrix();
}



ModelCanvas::ModelCanvas(wxWindow *parent, int * args)
: wxGLCanvas(parent, wxID_ANY, args)
{
  LOG_INFO << "Creating OpenGL Canvas...";

  context_ = new wxGLContext(this);

  init = false;

  // Init time related stuff
  srand(timeGetTime());
  time = 0;
  lastTime = timeGetTime();

  // Set all our pointers to null
  skyModel = nullptr;    // SkyBox Model
  wmo = nullptr;      // world map object model
  adt = nullptr;      // ADT
  animControl = nullptr;
  gifExporter = nullptr;
  rt = nullptr;        // RenderToTexture class
  root = new Attachment(nullptr, nullptr, -1, -1);
  sky = new Attachment(nullptr, nullptr, -1, -1);
  model_ = nullptr;

  fogTex = 0;

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
  
  Show(true);

  testModel_ = nullptr;

  // Initiate the timer that handles our animation and setting the canvas to redraw
  timer.SetOwner(this, ID_TIMER);
  timer.Start(TIME_STEP);
}

ModelCanvas::~ModelCanvas()
{
  // Release our avi engine
#if defined(_WINDOWS)
  cAvi.ReleaseEngine();
#endif

  // Clear remaining textures.
  TEXTUREMANAGER.clear();

  // Clear model attachments
  clearAttachments();

  wxDELETE(root);
  wxDELETE(sky);
  //wxDELETE(wmo);
  //wxDELETE(model);

#ifdef _WINDOWS
  if (rt) {
    rt->Shutdown();
    wxDELETE(rt);
  }
#endif
}

void ModelCanvas::OnEraseBackground(wxEraseEvent& event)
{
    event.Skip();
}

void ModelCanvas::OnSize(wxSizeEvent& event)
{
  event.Skip();

  Refresh();
  //if (init) 
  //  InitView();

  if(g_modelViewer)
    g_modelViewer->UpdateCanvasStatus();
}

void ModelCanvas::InitView()
{
  // set GL viewport
  int w=0, h=0;
  GetClientSize(&w, &h);
#if 0
  SetCurrent(); // 2009.07.02 Alfred cause crash
#endif

  // update projection
  video.ResizeGLScene(w, h);
  video.xRes = w;
  video.yRes = h;
}


Attachment* ModelCanvas::LoadModel(GameFile * file)
{
  const auto * m = new WoWModel(file, true);

  delete testModel_;

  testModel_ = new Model(nullptr);
  for (const auto & p : m->passes)
  {
    const auto* g = m->geosets[p->geoIndex];

    std::vector<Vertex> vertices;
    for (auto i = g->vertexStart; i < (g->vertexStart + g->vertexCount); i++)
    {
      auto * v = new Vertex();
      v->position = m->origVertices[i].pos;
      v->texCoords = m->origVertices[i].tex_coords[0];
      v->normal = m->origVertices[i].normal;
      vertices.push_back(*v);
    }

    std::vector<unsigned int> indices;
    for (auto i = g->indexStart; i < (g->indexStart + g->indexCount); i++)
      indices.push_back((m->indices[i] - g->vertexStart));

      std::vector<unsigned int> textures;
    if (p->blendmode == 0)
    {
    
      textures.push_back(m->getGLTexture(p->tex));
    }

    LOG_INFO << p->blendmode;
    LOG_INFO << p->useTex2;

    auto * mesh = new Mesh(vertices, indices, textures);
    mesh->blendmode_ = p->blendmode;

    testModel_->addMesh(mesh);
  }

  return nullptr;

  clearAttachments();
  root->setModel(nullptr);
  delete wmo;
  wmo = nullptr;

  // Create new one
 
  if (!model_->ok)
  {
    LOG_INFO << "Model is not OK !";
    model_ = nullptr;
    return nullptr;
  }

  auto *att = root->addChild(model_, 0, -1);

  camera.reset(model_);
  return att;
}

void ModelCanvas::LoadADT(wxString fn)
{
  root->setModel(0);
  wxDELETE (adt);

  if (!adt) {
    adt = new MapTile(fn);
    if (adt->ok) {
      glm::vec3 vc = adt->topnode.vmax;
      if (vc.y < 0) vc.y = 0;
      adt->viewpos.y = vc.y + 50.0f;
      adt->viewpos.x = adt->xbase;
      adt->viewpos.z = adt->zbase;
      root->setModel(adt);
    } else
      wxDELETE(adt);
  }
}

void ModelCanvas::LoadWMO(wxString fn)
{
  if (!wmo) {
    wmo = new WMO(QString::fromWCharArray(fn.c_str()));
    root->setModel(wmo);
  }
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
    const auto zoom = -event.GetWheelRotation() / 240.f * mul;
    camera.setRadius(camera.radius() + zoom);
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
      const auto x = deltax * 0.025;
      const auto y = deltay * 0.025;

      const auto look = camera.lookAt();
      const auto right = camera.right();

      camera.setLookAt(glm::vec3(look.x + right.x * -x, look.y + right.y * -x, look.z + y));
    }
    else if(event.MiddleIsDown())
    {
      camera.setRadius(camera.radius() + deltay/10.f);
    }
  }
  
  lastMousePos.x = event.GetX();
  lastMousePos.y = event.GetY();
}

void ModelCanvas::InitGL()
{
  if (init)
    return;

  // Initialize GLEW
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "Failed to initialize GLEW" << std::endl;
    return;
  }

  LOG_INFO << "GLEW successfully initiated.";

  glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);

  glEnable(GL_DEPTH_TEST);

  // If no g_modelViewer->lightControl object, exit for now
  //if (!g_modelViewer || !g_modelViewer->lightControl)
  //  return;

  // Setup lighting
  //g_modelViewer->lightControl->Init();
  //g_modelViewer->lightControl->UpdateGL();

  testGL();

  init = true;
}

inline void ModelCanvas::RenderGrid() 
{
  return;
  int count = 0;

  const GLfloat white[] = {1.0f, 1.0f, 1.0f, 1.0f};
  const GLfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_LIGHTING);
  //glEnable(GL_COLOR);
  
   glBegin(GL_QUADS);

  for(float i=-20.0f; i<=20.0f; i+=1.0f) {
    for(float j=-20.0f; j<=20.0f; j+=1.0f) {
      if((count%2) == 0) {
        //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
        glColor3f(1.0f, 1.0f, 1.0f);
      } else {
        //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);  
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
        glColor3f(0.2f, 0.2f, 0.2f);
      }

      glNormal3f(0, 0, 1);

      glVertex3f(j,   i,   0);
      glVertex3f(j,   i+1, 0);
      glVertex3f(j+1, i+1, 0);
      glVertex3f(j+1, i,   0);
      count++;
    }
  }

  glEnd();

  glEnable(GL_LIGHTING);
  glEnable(GL_TEXTURE_2D);
  //glDisable(GL_COLOR);
}

inline void ModelCanvas::RenderLight(Light *l)
{
  return;
  GLUquadricObj *quadratic = gluNewQuadric();    // Storage For Our Quadratic Object & // Create A Pointer To The Quadric Object
  gluQuadricNormals(quadratic, GLU_SMOOTH);    // Create Smooth Normals

  glPushMatrix();

  //glEnable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  
  //glLightModelfv(GL_LIGHT_MODEL_AMBIENT, l->diffuse);
  glColor4f(l->diffuse.x, l->diffuse.y, l->diffuse.z, 0.5f);

  glTranslatef(l->pos.x, l->pos.y, l->pos.z);

  // rotate the objects to point in the right direction
  //glm::vec3 rot(l->pos.x, l->pos.y, l->pos.z);
  //float theta = rot.thetaXZ(l->target);
  //glRotatef(theta * rad2deg, 0.0f, 1.0f, 0.0f);
  
  gluSphere(quadratic, 0.15, 8, 8);

  if (l->type == LIGHT_DIRECTIONAL) { // Directional light
    glBegin(GL_LINES);
    glVertex3f(0, 0, 0);
    glVertex3f(l->target.x, l->target.y, l->target.z);
    glEnd();

  } else if (l->type == LIGHT_POSITIONAL) {  // Positional Light
    
  } else {  // Spot light
    
  }

  glEnable(GL_LIGHTING);
  glPopMatrix();
}

inline void ModelCanvas::RenderSkybox()
{
  // ************** SKYBOX *************
  glPushMatrix();    // Save the current modelview matrix
  glLoadIdentity();  // Reset it
  
  float fScale = 64.0f / skyModel->rad;
  
  glTranslatef(0.0f, 0.0f, -5.0f);  // Position the sky box
  glScalef(fScale, fScale, fScale);  // Scale it so it looks appropriate
  sky->draw();          // Render the skybox

  glPopMatrix();            // load the old modelview matrix that we saved previously
  // ====================================
}

inline void ModelCanvas::RenderObjects()
{
  return;
  // ***************** MODEL RENDERING **********************
  // ************* Setup our render state *********
  //glEnable(GL_COLOR_MATERIAL);
  if (video.useMasking) {
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
  } else {

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    //glEnable(GL_CULL_FACE);

  }
  // ===============================================
  
  //model->animcalc = false;
    
  //glEnable(GL_NORMALIZE);
  root->draw();
  //glDisable(GL_NORMALIZE);
  
  //if (!video.useMasking)
  {
    // render our particles, we do this afterwards so that all the particles display "OK" without having things like shields "overwriting" the particles.
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    glDepthMask(GL_FALSE);
    //glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    
    root->drawParticles();
    
    glDisable(GL_BLEND);
    //glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
  }
  // ========================================    
}

inline void ModelCanvas::RenderBackground()
{
  return;
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  //glOrtho(0, video.xRes, 0, video.yRes, -1.0, 1.0);
  gluOrtho2D(0, 1, 0, 1);

  // save previous state
  GLboolean texture2DState = glIsEnabled(GL_TEXTURE_2D);
  GLboolean depthTestState = glIsEnabled(GL_DEPTH_TEST);
  GLboolean lightningState = glIsEnabled(GL_LIGHTING);

  glEnable(GL_TEXTURE_2D);          // Enable 2D Texture Mapping
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  
  glBindTexture(GL_TEXTURE_2D, uiBGTexture);

#if defined(_WINDOWS)
  // If its an AVI background, increment the frame
  if (drawAVIBackground)
    cAvi.GetFrame();
#endif

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0, 0);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1, 0);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1, 1);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0, 1);
  glEnd();

  // ModelView
  glPopMatrix();
  // Projection
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  
  // restore state
  if(texture2DState)
    glEnable(GL_TEXTURE_2D);
  else
    glDisable(GL_TEXTURE_2D);

  if(depthTestState)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);

  if(lightningState)
    glEnable(GL_LIGHTING);
  else
    glDisable(GL_LIGHTING);

  // Set back to modelview for rendering
  glMatrixMode(GL_MODELVIEW);
}

void ModelCanvas::Render(wxPaintEvent& WXUNUSED(event))
{
  wxPaintDC dc(this);

  // Set this window handler as the reference to draw to.
  wxGLCanvas::SetCurrent(*context_);


  if (!init)
    InitGL();

  if(openGLDebug_)
    displayDebugInfos();

  int w = 0, h = 0;
  GetClientSize(&w, &h);
  glViewport(0, 0, w, h);
  // Sets the "clear" colour.  Without this you get the "ghosting" effecting 
  // as the buffer doesn't get set/cleared.
 // if (video.useMasking)
 //   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
 // else
  
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  const glm::mat4 model(1.0);

  // setup projection (use perspective camera)
  //glMatrixMode(GL_PROJECTION); 
  //glLoadIdentity();

  const auto projection = glm::perspective(glm::radians(60.0f), (float)getWidth() / (float)getHeight(), 0.1f, 100.f);
  //glMultMatrixf((glm::value_ptr(projection)));

  // setup camera
  //glMatrixMode(GL_MODELVIEW);
  //glLoadIdentity();

  const auto view = camera.getViewMatrix();
  //glMultMatrixf((glm::value_ptr(view)));

  shaderProgram_.use();

  // Pass the matrices to the shader
  shaderProgram_.setUniform("model", model);
  shaderProgram_.setUniform("view", view);
  shaderProgram_.setUniform("projection", projection);

  if(testModel_)
    testModel_->draw(shaderProgram_);

  // If masking isn't enabled
  ////if (!video.useMasking) 
  {
    if (openGLDebug_)
    {
      // draw origin axis
      drawAxis(glm::vec3(0.0, 0.0, 0.0), 1.0f, glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0));

      // draw lookAt axis
      drawAxis(camera.lookAt(), 0.5f, glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
    }
    /*
    // Draw the background image if any
    if(drawBackground)
      RenderBackground();

    // render the skybox, if any
    if (drawSky && skyModel && sky->model())
      RenderSkybox();

    if (drawGrid)
      RenderGrid();
      */
  }

  //RenderObjects();
  glFlush();
  SwapBuffers();
}

inline void ModelCanvas::RenderModel()
{
  // Sets the "clear" colour.  Without this you get the "ghosting" effecting 
  // as the buffer doesn't get set/cleared.
  if (video.useMasking)
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  else
    glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  // (re)set the view
//  InitView();

  // If masking isn't enabled
  //if (!video.useMasking)
  {
    // Draw the background image if any
    if(drawBackground)
      RenderBackground();
        
    // render the skybox, if any
    if (drawSky && skyModel && sky->model())
      RenderSkybox();
  }

 // camera.Setup();

  // This is redundant and no longer needed.
  // all lighting stuff needs to be reorganised
  // ************* Absolute Lighting *******************
  // All our lighting related rendering code
  // Use model lighting?
  if (model_ && (lightType==LIGHT_MODEL_ONLY)) {
    glm::vec4 la;

    if (model_->nbLights() > 0) {
      la = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
      la = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Set the Model Ambience lighting.
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(la));

  // Dynamic
  } else if (lightType == LIGHT_DYNAMIC) {
    for (size_t i=0; i<MAX_LIGHTS; i++) {
      if (g_modelViewer->lightControl->lights[i].enabled && !g_modelViewer->lightControl->lights[i].relative) {
        glLightfv(GL_LIGHT0 + (GLenum)i, GL_POSITION, glm::value_ptr(g_modelViewer->lightControl->lights[i].pos));

        // Draw our 'light cone' to represent the light.
        if (drawLightDir)
          RenderLight(&g_modelViewer->lightControl->lights[i]);
      }
    }
    
  // Ambient lighting is just a single colour applied to all rendered vertices.
  } else if (lightType==LIGHT_AMBIENT) {
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(g_modelViewer->lightControl->lights[0].diffuse));  // use diffuse, as thats our main 'colour setter'
  }
  // ==============================================
  // This is also redundant
  // The camera class should be taking over this crap
  // *************************
  // setup the view/projection
  /*
  if (model_) {
    if (useCamera && model_->hasCamera) {
      WoWModel * m = const_cast<WoWModel *>(model_);
      m->cam[0].setup();
    }
    else {
      // TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
      glTranslatef(model_->pos.x, model_->pos.y, -model_->pos.z);
      glRotatef(model_->rot.x, 1.0f, 0.0f, 0.0f);
      glRotatef(model_->rot.y, 0.0f, 1.0f, 0.0f);
      glRotatef(model_->rot.z, 0.0f, 0.0f, 1.0f);
      // --==--
    }
  }
  */
  // ==========================

  // As above for lighting
  // ************* Relative Lighting *******************
  // More lighting code, this is to setup the g_modelViewer->lightControl->lights that are 'relative' to the model.
  if (model_ && (lightType==LIGHT_DYNAMIC)) { // Else, for all our models, we use the new "lighting control", IF we're not using model only lighting    
    // loop through the g_modelViewer->lightControl->lights of our lighting system checking to see if they are turned on
    // and if so to apply their settings.
    for (size_t i=0; i<MAX_LIGHTS; i++) {
      if (g_modelViewer->lightControl->lights[i].enabled && g_modelViewer->lightControl->lights[i].relative) {
        glLightfv(GL_LIGHT0 + (GLenum)i, GL_POSITION, glm::value_ptr(g_modelViewer->lightControl->lights[i].pos));
        
        // Draw our 'light cone' to represent the light.
        if (drawLightDir)
          RenderLight(&g_modelViewer->lightControl->lights[i]);
      }
    }
  }
  // ==============================================
      
  // Render the grid if wanted and masking isn't enabled
  if (drawGrid && !video.useMasking)
    RenderGrid();

  // render our main model
  if (model_) {
    glEnable(GL_NORMALIZE);
    RenderObjects();
    glDisable(GL_NORMALIZE);
  }

  
  // Finished rendering, swap it into our front buffer (to the screen)
  //glFlush();
  //glFinish();
  SwapBuffers();
}

inline void ModelCanvas::RenderToTexture()
{

  /*
  // -------------------------------------------
  // Render to Texture
  // -------------------------------------------
  glBindTexture(GL_TEXTURE_2D, 0);
  rtt[0]->BeginRender();

  glPushAttrib(GL_VIEWPORT_BIT | GL_POLYGON_BIT);

  glViewport( 0, 0, rtt[0]->nWidth, rtt[0]->nHeight); 
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  glMatrixMode(GL_PROJECTION);            // Select The Projection Matrix
  glPushMatrix();
  glLoadIdentity();                  // Reset The Projection Matrix

  // Calculate The Aspect Ratio Of The Window
  gluPerspective(video.fov, (float)rtt[0]->nWidth/(float)rtt[0]->nHeight, 0.1f, 128.0f*5);

  glMatrixMode(GL_MODELVIEW);              // Select The Modelview Matrix
  glPushMatrix();
  glLoadIdentity();                  // Reset The Modelview Matrix

  if (model) {
    if (useCamera && model->hasCamera) {
      model->cam.setup();
    } else {
      glTranslatef(model->pos.x, model->pos.y, -model->pos.z);
      glRotatef(model->rot.x, 1.0f, 0.0f, 0.0f);
      glRotatef(model->rot.y, 0.0f, 1.0f, 0.0f);
      glRotatef(model->rot.z, 0.0f, 0.0f, 1.0f);
      // --==--
    }
  }
  // adding little scale to model. This will make effect to be more noticable
  //glScalef(1.05f, 1.05f, 1.05f);    

  // render our main model
  if (model)
    RenderObjects();

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  
  glPopAttrib();
  rtt[0]->EndRender();

  int buff_index=0;

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();

  glOrtho(0, 1, 0, 1, 0.01, 100);
  
  glPushAttrib(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glViewport( 0, 0, rtt[0]->nWidth, rtt[0]->nHeight);
  glDisable(GL_DEPTH_TEST); 

  // =============================================================
  glPopAttrib();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();

  glPopMatrix();
  */
}

inline void ModelCanvas::RenderWMO()
{
  if (!init)
    InitGL();

  glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  InitView();

  // Lighting
  glm::vec4 la;
  // From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
  for (size_t i=0; i<4; i++) {
    GLuint light = GL_LIGHT0 + (GLuint)i;
    glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
    glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
    glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
    glDisable(light);
  }
  la = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);

  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(la));
  glColor3f(1.0f, 1.0f, 1.0f);
  
  
  // --==--
  // TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
  // View
  if (model_) {
    glTranslatef(model_->pos_.x, model_->pos_.y, -model_->pos_.z);
    glRotatef(model_->rot_.x, 1.0f, 0.0f, 0.0f);
    glRotatef(model_->rot_.y, 0.0f, 1.0f, 0.0f);
    glRotatef(model_->rot_.z, 0.0f, 0.0f, 1.0f);
    // --==--
  }

  //camera.Setup();

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  root->draw();
  //root->drawParticles(true);

  //glFlush();
  //glFinish();
  SwapBuffers();
}

inline void ModelCanvas::RenderADT()
{
  if (!init)
    InitGL();

  glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  InitView();

  // Lighting
  glm::vec4 la;
  // From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
  for (size_t i=0; i<4; i++) {
    GLuint light = GL_LIGHT0 + (GLuint)i;
    glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
    glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
    glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
    glDisable(light);
  }
  la = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);

  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(la));
  glColor3f(1.0f, 1.0f, 1.0f);
  // --==--

  /*
  // TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
  // View
  if (model) {
    glTranslatef(model->pos.x, model->pos.y, -model->pos.z);
    glRotatef(model->rot.x, 1.0f, 0.0f, 0.0f);
    glRotatef(model->rot.y, 0.0f, 1.0f, 0.0f);
    glRotatef(model->rot.z, 0.0f, 0.0f, 1.0f);
    // --==--
  }
  */

  // camera.Setup();


  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  root->draw();
  //root->drawParticles(true);

  //glFlush();
  //glFinish();
  SwapBuffers();

  // cleanup
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


inline void ModelCanvas::RenderWMOToBuffer()
{
  if (!rt)
    return;

  if (!init || video.supportFBO)
    InitGL();

  glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (video.supportFBO && video.supportPBO) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective( 45.0f, rt->nWidth / rt->nHeight, 3.0f, 1500.0f*5 );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
  }

  // Lighting
  glm::vec4 la;
  // From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
  for (size_t i=0; i<4; i++) {
    GLuint light = GL_LIGHT0 + (GLuint)i;
    glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
    glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
    glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
    glDisable(light);
  }
  la = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);

  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(la));
  glColor3f(1.0f, 1.0f, 1.0f);
  // --==--

  // View
  // TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
  if (model_) {
    glTranslatef(model_->pos_.x, model_->pos_.y, -model_->pos_.z);
    glRotatef(model_->rot_.x, 1.0f, 0.0f, 0.0f);
    glRotatef(model_->rot_.y, 0.0f, 1.0f, 0.0f);
    glRotatef(model_->rot_.z, 0.0f, 0.0f, 1.0f);
  }
  // --==--

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  root->draw();
  //root->drawParticles(true);
}

void ModelCanvas::RenderToBuffer()
{
  if (!init || !video.supportFBO) {
    InitGL();
    g_modelViewer->lightControl->UpdateGL();
  }
  
  // --==--
  // Reset the render state
  glLoadMatrixf(glm::value_ptr(glm::mat4(1.0f)));

  if (video.supportDrawRangeElements || video.supportVBO) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  } else {
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  }

  
  if(video.supportAntiAlias && video.curCap.aaSamples>0)
    glEnable(GL_MULTISAMPLE_ARB);

  /*
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glColorMaterial(GL_FRONT_AND_BACK, GL_SPECULAR);
  */

  glAlphaFunc(GL_GEQUAL, 0.8f);
  glDepthFunc(GL_LEQUAL);
  glMaterialf(GL_FRONT, GL_SHININESS, 18.0f);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_ALPHA_TEST);
  // --==--
  
  if (rt)
  {
    glPushAttrib(GL_VIEWPORT_BIT);
    video.ResizeGLScene(rt->nWidth, rt->nHeight);
  }

  // Sets the "clear" colour.  Without this you get the "ghosting" effecting 
  // as the buffer doesn't get set/cleared.
  if (video.useMasking)
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  else
    glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  /*
  if (rt) {
    video.ResizeGLScene(rt->nWidth, rt->nHeight);
  } else {
    video.ResizeGLScene(video.xRes, video.yRes);
  }
  */
  
  // If masking isn't enabled
  //if (!video.useMasking)
  {
    // Draw the background image if any
    if(drawBackground)
      RenderBackground();
        
    // render the skybox, if any
    if (drawSky && skyModel && sky->model())
      RenderSkybox();
  }

  // setup projection (use perspective camera)
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glm::mat4 projection = glm::perspective(video.fov, (float)rt->nWidth / (float)rt->nHeight, 0.1f, 1280 * 5.f);
  glMultMatrixf((glm::value_ptr(projection)));

  // setup camera
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glm::mat4 view = camera.getViewMatrix();
  glMultMatrixf((glm::value_ptr(view)));
    
  // Render the grid if wanted and masking isn't enabled
  if (drawGrid && !video.useMasking)
    RenderGrid();
  
  /*
  // Use model lighting?
  if (model && (lightType==LT_MODEL_ONLY)) {
    glm::vec4 la;

    if (model->lights.size() > 0) {
      la = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
      la = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Set the Model Ambience lighting.
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, la);

  } else if (lightType==LT_DIRECTIONAL) {
    for (size_t i=0; i<MAX_LIGHTS; i++) {
      if (g_modelViewer->lightControl->lights[i].enabled && !g_modelViewer->lightControl->lights[i].relative) {
        lightID = GL_LIGHT0 + i;

        glLightfv(lightID, GL_POSITION, g_modelViewer->lightControl->lights[i].pos);
      }
    }
    
  // Ambient lighting is just a single colour applied to all rendered vertices.
  } else if (lightType==LIGHT_AMBIENT) {
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, g_modelViewer->lightControl->lights[0].diffuse);  // use diffuse, as thats our main 'colour setter'
  }
  

  */
  // *************************
  // setup the view/projection
  if (model_) {
    if (useCamera && model_->hasCamera) {
      WoWModel * m = const_cast<WoWModel *>(model_);
      m->cam[0].setup();
    } else {
      // TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
      glTranslatef(model_->pos_.x, model_->pos_.y, -model_->pos_.z);
      glRotatef(model_->rot_.x, 1.0f, 0.0f, 0.0f);
      glRotatef(model_->rot_.y, 0.0f, 1.0f, 0.0f);
      glRotatef(model_->rot_.z, 0.0f, 0.0f, 1.0f);
      // --==--
    }
  }
  // ==========================
    
  /*
  // ************* Relative Lighting *******************
  // More lighting code, this is to setup the g_modelViewer->lightControl->lights that are 'relative' to the model.
  if (model && (lightType==LT_DIRECTIONAL)) { // Else, for all our models, we use the new "lighting control", IF we're not using model only lighting    
    // loop through the g_modelViewer->lightControl->lights of our lighting system checking to see if they are turned on
    // and if so to apply their settings.
    for (size_t i=0; i<MAX_LIGHTS; i++) {
      if (g_modelViewer->lightControl->lights[i].enabled && g_modelViewer->lightControl->lights[i].relative) {
        lightID = GL_LIGHT0 + i;

        glLightfv(lightID, GL_POSITION, g_modelViewer->lightControl->lights[i].pos);

        // Draw our 'light cone' to represent the light.
        if (drawLightDir)
          RenderLight(&g_modelViewer->lightControl->lights[i]);
      }
    }
  }
  // ==============================================
  */
    

  // render our main model
  if (model_)
    RenderObjects();

  if (rt)
    glPopAttrib();
}




void ModelCanvas::OnTimer(wxTimerEvent& event)
{
  if (init) {
    CheckMovement();
    tick();
    Refresh();
  }
}

void ModelCanvas::tick()
{
  size_t ddt = 0;

  // Time stuff
  //time = float();
  ddt = (timeGetTime() - lastTime);// * animSpeed;
  lastTime = timeGetTime();
  // --

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

void ModelCanvas::LoadBackground(wxString filename)
{
  if (!wxFile::Exists(filename))
    return;

  bgImagePath = filename;

  // Get the file extension and load the file
  wxString tmp = filename.AfterLast(wxT('.'));
  tmp.MakeLower();

  GLuint texFormat = GL_TEXTURE_2D;

  if (tmp == wxT("avi"))
  {
#ifdef _WINDOWS
    cAvi.SetFileName(filename.c_str());
    cAvi.InitEngineForRead();
#endif

    // Setup the OpenGL Texture stuff
    glGenTextures(1, &uiBGTexture);
    glBindTexture(texFormat, uiBGTexture);
    
    glTexParameteri(texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // Linear Filtering
    glTexParameteri(texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // Linear Filtering

#ifndef _WINDOWS
    cAvi.GetFrame();
#endif
    drawBackground = true;
    drawAVIBackground = true;
  }
  else
  {
    QImage texture;

    if(!texture.load(QString::fromWCharArray(filename.c_str())))
    {
      LOG_ERROR << "Failed to load texture" << QString::fromWCharArray(filename.c_str());
      LOG_INFO << "Supported formats:" << QImageReader::supportedImageFormats();
    }

    texture = texture.mirrored();
    texture = texture.convertToFormat(QImage::Format_RGBA8888);

    // Setup the OpenGL Texture stuff
    glGenTextures(1, &uiBGTexture);
    glBindTexture(texFormat, uiBGTexture);
    
    glTexParameteri(texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // Linear Filtering
    glTexParameteri(texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // Linear Filtering
    
    glTexImage2D(texFormat, 0, GL_RGBA, texture.width(), texture.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.bits());
    drawBackground = true;
  }
}

// Check for keyboard input
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

// Our screenshot function which supports both PBO and FBO aswell as traditional older cards, eventually.
void ModelCanvas::Screenshot(const wxString fn, int x, int y)
{
  glPixelStorei(GL_PACK_ALIGNMENT, 1);

  delete rt;
  rt = 0;

  wxFileName temp(fn, wxPATH_NATIVE);
  
  int screenSize[4];
  glGetIntegerv(GL_VIEWPORT, screenSize);

  // Setup out buffers for offscreen rendering
  if (video.supportPBO || video.supportFBO)
  {
    rt = new (std::nothrow) RenderTexture();

    if(!rt)
    {
      LOG_ERROR << "Unable to initialise render texture to make screenshot";
      return;
    }

    rt->Init(x, y, video.supportFBO);
    screenSize[2] = rt->nWidth;

    screenSize[3] = rt->nHeight;

    rt->BeginRender();

    if (model_)
      RenderToBuffer();

    rt->BindTexture();
  }
  else
  {
    glGetIntegerv(GL_VIEWPORT, screenSize);
    glReadBuffer(GL_BACK);
    if(model_)
      RenderToBuffer();
  }

  LOG_INFO << "Saving screenshot in : " << QString::fromWCharArray(fn.c_str());

  if(temp.GetExt() == wxT("tga")) // QT does not support tga writing
  {
    // Make the BYTE array, factor of 3 because it's RBG.
    BYTE* pixels = new BYTE[ 4 * screenSize[2] * screenSize[3]];

    glReadPixels(0, 0, screenSize[2], screenSize[3], GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);

    CxImage newImage;
    // deactivate alpha layr for now, need to rewrite RenderTexture class
    //newImage.AlphaCreate();  // Create the alpha layer
    newImage.IncreaseBpp(32);  // set image to 32bit
    newImage.CreateFromArray(pixels, screenSize[2], screenSize[3], 32, (screenSize[2]*4), false);
    newImage.Save(fn.fn_str(), CXIMAGE_FORMAT_TGA);
    newImage.Destroy();

    // free the memory
    wxDELETEA(pixels);
  }
  else // other formats, let Qt do the job
  {
    QImage image(screenSize[2],  screenSize[3], QImage::Format_RGB32);
    glReadPixels(0, 0, screenSize[2], screenSize[3], GL_BGRA_EXT, GL_UNSIGNED_BYTE, image.bits());
    image.mirrored().save(QString::fromWCharArray(fn.c_str()));
  }
  
  if (rt)
  {
    rt->ReleaseTexture();
    rt->EndRender();
    rt->Shutdown();
    delete rt;
    rt = 0;
  }

  // Set back to normal
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

// Save the scene state,  currently this is just position/rotation/field of view
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

// Load the scene state, as above
void ModelCanvas::LoadSceneState(int id)
{
  if (!model_)
    return;

  // bounds check
  WoWModel * m = const_cast<WoWModel *>(model_);
  if (id > -1 && id < 4) {
    video.fov =  sceneState[id].fov ;
    m->pos_ = sceneState[id].pos;
    m->rot_ = sceneState[id].rot;

    int screenSize[4];
    glGetIntegerv(GL_VIEWPORT, (GLint*)screenSize);        // get the width/height of the canvas
    video.ResizeGLScene(screenSize[2], screenSize[3]);
  }
}

void ModelCanvas::displayDebugInfos() const
{
  static wxString appTitle = GLOBALSETTINGS.appTitle();
  static int frameCount = 0;

  static DWORD previousTime = 0;
  const auto currentTime = timeGetTime();
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

int ModelCanvas::getWidth() const
{
  return GetSize().x;
}

int ModelCanvas::getHeight() const
{
  return GetSize().y;
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

