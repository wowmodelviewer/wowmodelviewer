#include "modelcanvas.h"

#include "animcontrol.h"
#include "Attachment.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "shaders.h"
#include "video.h"

#include "CxImage/ximage.h"

static const float defaultMatrix[] = {1.000000,0.000000,0.000000,0.000000,0.000000,1.000000,0.000000,0.000000,0.000000,0.000000,1.000000,0.000000,0.000000,0.000000,0.000000,1.000000};

//float animSpeed = 1.0f;
static const float piover180 = 0.0174532925f;
static const float rad2deg = 57.295779513f;

#ifdef	_WINDOWS
IMPLEMENT_CLASS(ModelCanvas, wxWindow)
BEGIN_EVENT_TABLE(ModelCanvas, wxWindow)
#else
IMPLEMENT_CLASS(ModelCanvas, wxGLCanvas)
BEGIN_EVENT_TABLE(ModelCanvas, wxGLCanvas)
#endif
	EVT_SIZE(ModelCanvas::OnSize)
	EVT_PAINT(ModelCanvas::OnPaint)
	EVT_ERASE_BACKGROUND(ModelCanvas::OnEraseBackground)

    EVT_TIMER(ID_TIMER, ModelCanvas::OnTimer)
    EVT_MOUSE_EVENTS(ModelCanvas::OnMouse)
	EVT_KEY_DOWN(ModelCanvas::OnKey)
END_EVENT_TABLE()


#if _MSC_VER // The following time related functions COULD be 64bit incompatible.
	// for timeGetTime:
	#pragma comment(lib,"Winmm.lib")
#endif

#ifndef _WINDOWS // for linux
	#include <sys/time.h>

	//typedef int DWORD;
	int timeGetTime()
	{
		static int start=0;
		static struct timeval t;
		gettimeofday(&t, NULL);
		if (start==0){
			start = t.tv_sec;
		}

		return (int)((t.tv_sec-start)*1000 + t.tv_usec/1000);
	}
#endif

#ifndef _WINDOWS
namespace {
	int attrib[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, 0 };
}
#endif

ModelCanvas::ModelCanvas(wxWindow *parent, VideoCaps *caps)
#ifndef _WINDOWS
: wxGLCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER|wxCLIP_CHILDREN|wxFULL_REPAINT_ON_RESIZE, wxT("ModelCanvas"), attrib, wxNullPalette)
#endif
{
	wxLogMessage(wxT("Creating OpenGL Canvas..."));

    init = false;
	initShaders = false;

	// Init time related stuff
	srand(timeGetTime());
	time = 0;
	lastTime = timeGetTime();

	// Set all our pointers to null
	model =	0;			// Main model.
	skyModel = 0;		// SkyBox Model
	wmo = 0;			// world map object model
	adt = 0;			// ADT
	animControl = 0;
	gifExporter = 0;
#ifdef _WINDOWS
	rt = 0;				// RenderToTexture class
/*
	for(int i=0; i<2; i++) {
		rtt = 0;
	}
*/
#endif
	curAtt = 0;			// Current Attachment
	root = 0;
	sky = 0;
	modelsize = 0;
	fogTex = 0;

	/*
	blurShader = NULL;
	deathShader = NULL;
	desaturateShader = NULL;
	glowShader = NULL;
	boxShader = NULL;
	*/

	lightType = LIGHT_DYNAMIC;

	// Setup our default colour values.
	vecBGColor = Vec3D((float)(71.0/255),(float)(95.0/255),(float)(121.0/255)); 

	drawLightDir = false;
	drawBackground = false;
	drawAVIBackground = false;
	drawSky = false;
	drawGrid = false;
	bMouseLight = false;
	useCamera = false;
	
	
	//wxNO_BORDER|wxCLIP_CHILDREN|wxFULL_REPAINT_ON_RESIZE
#ifdef _WINDOWS
	if(!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER|wxCLIP_CHILDREN|wxFULL_REPAINT_ON_RESIZE, wxT("ModelCanvas"))) {
		wxLogMessage(wxT("Critcal Error: Unable to create a window to handle our OpenGL rendering.\n\tWon't be able to continue."));
		parent->Close();
		return;
	} else 
#endif
	{
#ifndef	_LINUX // buggy
		SetBackgroundStyle(wxBG_STYLE_CUSTOM);
#endif
		Show(true);

		// Initiate the timer that handles our animation and setting the canvas to redraw
		timer.SetOwner(this, ID_TIMER);
		timer.Start(TIME_STEP);

		// Initiate our default OpenGL settings
		wxLogMessage(wxT("Initiating OpenGL..."));
#ifdef _WINDOWS
		wxDisplay *disp = new wxDisplay(0);
		int bpp = disp->GetCurrentMode().bpp;
		video.SetHandle((HWND)this->GetHandle(), bpp);
#else
		video.render = true;
#endif
	}
	
	root = new Attachment(NULL, NULL, -1, -1);
	sky = new Attachment(NULL, NULL, -1, -1);
}

ModelCanvas::~ModelCanvas()
{
	// Release our avi engine
#if defined(_WINDOWS) && !defined(_MINGW)
	cAvi.ReleaseEngine();
#endif

	// Clear remaining textures.
	texturemanager.clear();

	// Uninitialise shaders
	UninitShaders();

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

	if (init) 
		InitView();
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
	//SetupProjection();
	video.ResizeGLScene(w, h);
	video.xRes = w;
	video.yRes = h;
}

void ModelCanvas::InitShaders()
{

	if (video.supportFragProg) {
		/*
		// TODO: Rewrite these shaders from WoW into GLSL
		deathShader.InitShaders("","shaders/FFXDeath.fp");
		deathShader.Disable();
		desaturateShader.InitShaders("","shaders/Desaturate.fp");
		desaturateShader.Disable();
		*/
	}

	fxBlur = false;
	fxGlow = false;
	fxFog = false;

	/*
	// GLSL Shaders
	if (video.supportGLSL && video.supportFBO) {
		// Render-to-texture initialisation
		for(int i=0; i<2; i++) {
			rtt[i] = new RenderTexture();
			rtt[i]->Init((HWND)this->GetHandle(), 512, 512, video.supportFBO);
		}

		perpixelShader.InitShaders("shaders/perpixellighting.vp", "shaders/perpixellighting.fp");
		//perpixelShader.InitShaders("Shaders/phongLighting.vp", "Shaders/phongLighting.fp");
		perpixelShader.Disable();

		toonShader.InitShaders("shaders/ToonShader.vp", "shaders/toonShader.fp");
		toonShader.Disable();

		blurShader.InitShaders("Shaders/Blur.vp", "Shaders/Blur.fp");
		GLint radius_x = blurShader.GetVariable("radius_x");
		GLint radius_y = blurShader.GetVariable("radius_y");
		if(radius_x>=0 && radius_y>=0) {
			blurShader.SetFloat(radius_x, 1.5f / rtt[0]->nWidth); 
			blurShader.SetFloat(radius_y, 1.5f / rtt[0]->nHeight);
		}
		blurShader.Disable();
		
		glowShader.InitShaders("Shaders/Glow.vp", "Shaders/Glow.fp");
		glowShader.Disable();

		multitexShader.InitShaders("Shaders/MultiTexturing.vp", "Shaders/MultiTexturing.fp");
		multitexShader.Disable();
		CreateTexture("fog.bmp", fogTex);
	}
	*/

	initShaders = true;
}

void ModelCanvas::UninitShaders()
{
	if (!initShaders)
		return;

	/*
	wxDELETE(blurShader);
	wxDELETE(deathShader);
	wxDELETE(desaturateShader);
	wxDELETE(glowShader);
	wxDELETE(boxShader);
	*/
	
	/*
	if (video.supportFragProg) {
		blurShader.Disable();
		blurShader.Release();
	}
	*/

	/*
	if (video.supportGLSL) {
		perpixelShader.Disable();
		perpixelShader.Release();

		toonShader.Disable();
		toonShader.Release();

		blurShader.Disable();
		blurShader.Release();

		glowShader.Disable();
		glowShader.Release();

		// Render-to-texture memory clearance
		if (rtt[0]) {
			rtt[0]->Shutdown();
			wxDELETE(rtt[0]);
		}

		if (rtt[1]) {
			rtt[1]->Shutdown();
			wxDELETE(rtt[1]);
		}
	}
	*/
}

#if 0
Attachment* ModelCanvas::AddModel(const char *fn)
{
	Attachment *att = root->addChild(fn, (int)root->children.size(), -1);
	
	//curAtt->pos = vPos;
	//curAtt->rot = vRot;

	curAtt = att;

	ResetView();

	return att;
}
#endif

Attachment* ModelCanvas::LoadModel(wxString fn)
{
	clearAttachments();
	root->model = NULL;

	wxDELETE(wmo);

	model = new Model(fn, true);
	if (!model->ok) {
		wxDELETE(model);
		model = NULL;
		return NULL;
	}
	root->model = model;

	curAtt = root;

	ResetView();

	return root;
}

Attachment* ModelCanvas::LoadCharModel(wxString fn)
{
	clearAttachments();
	root->model = NULL;

	wxDELETE(wmo);

	// Create new one
	model = new Model(fn, true);
	if (!model->ok) {
		wxDELETE(model);
		model = NULL;
		return NULL;
	}
	
	ResetView();

	Attachment *att = root->addChild(model, 0, -1);
	curAtt = att;
	return att;
}

void ModelCanvas::LoadADT(wxString fn)
{
	OldinitShaders();

	root->model = NULL;
	wxDELETE (adt);

	if (!adt) {
		adt = new MapTile(fn);
		if (adt->ok) {
			Vec3D vc = adt->topnode.vmax;
			if (vc.y < 0) vc.y = 0;
			adt->viewpos.y = vc.y + 50.0f;
			adt->viewpos.x = adt->xbase;
			adt->viewpos.z = adt->zbase;
			root->model = adt;
		} else
			wxDELETE(adt);
	}
}

void ModelCanvas::LoadWMO(wxString fn)
{
	if (!wmo) {
		wmo = new WMO(fn);
		root->model = wmo;
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
	if (!model && !wmo && !adt)
		return;

	if (event.Button(wxMOUSE_BTN_ANY) == true)
		SetFocus();

	int px = event.GetX();
	int py = event.GetY();
	int pz = event.GetWheelRotation();

	// mul = multiplier in which to multiply everything to achieve a sense of control over the amount to move stuff by
	float mul = 1.0f;
	if (event.m_shiftDown)
		mul /= 10;
	if (event.m_controlDown)
		mul *= 10;
	if (event.m_altDown)
		mul *= 50;

	if (wmo) {

		//if (model->animManager)
		//	mul *= model->animManager->GetSpeed(); //animSpeed;

		if (event.ButtonDown()) {
			mx = px;
			my = py;

		} else if (event.Dragging()) {
			int dx = mx - px;
			int dy = my - py;
			mx = px;
			my = py;

			if (event.LeftIsDown() && event.RightIsDown()) {
				wmo->viewpos.y -= dy*mul;
			} else if (event.LeftIsDown()) {
				wmo->viewrot.x -= dx*mul/5;
				wmo->viewrot.y -= dy*mul/5;
			} else if (event.RightIsDown()) {
				wmo->viewrot.x -= dx*mul/5;
				float f = cos(wmo->viewrot.y * piover180);
				float sf = sin(wmo->viewrot.x * piover180);
				float cf = cos(wmo->viewrot.x * piover180);
				wmo->viewpos.x -= sf * mul * dy * f;
				wmo->viewpos.z += cf * mul * dy * f;
				wmo->viewpos.y += sin(wmo->viewrot.y * piover180) * mul * dy;
			} else if (event.MiddleIsDown()) {
				//?
			}

		} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
			//?
		}

	} else if (model) {
		if (model->animManager)
			mul *= model->animManager->GetSpeed(); //animSpeed;

		if (event.ButtonDown()) {
			mx = px;
			my = py;

			if (bMouseLight) // going to use vRot to hold our temp light position (technically, our g_modelViewer->lightControl->lights rotation).
				vRot0 = Vec3D(g_modelViewer->lightControl->GetCurrentPos().x,g_modelViewer->lightControl->GetCurrentPos().y,g_modelViewer->lightControl->GetCurrentPos().z);
			else
				vRot0 = model->rot;

			vPos0 = model->pos;

		} else if (event.Dragging()) {
			int dx = mx - px;
			int dy = my - py;

			if (event.LeftIsDown()) {
				if (bMouseLight)
					return;

				model->rot.x = vRot0.x - (dy / 2.0f); // * mul);
				model->rot.y = vRot0.y - (dx / 2.0f); // * mul);

				//viewControl->Refresh();

			} else if (event.RightIsDown()) {
				mul /= 100.0f;

				if (bMouseLight) {
					Vec4D temp = g_modelViewer->lightControl->GetCurrentPos();
					temp.y = vRot0.y + dy*mul;
					temp.x = vRot0.x - dx*mul;
					g_modelViewer->lightControl->SetPos(temp);
					g_modelViewer->lightControl->Update();
				} else {
					model->pos.x = vPos0.x - dx*mul;
					model->pos.y = vPos0.y + dy*mul;

					//viewControl->Refresh();
				}

			} else if (event.MiddleIsDown()) {
				if (!event.m_altDown) {
					mul = (mul / 20.0f) * dy;

					if (bMouseLight) {
						Vec4D temp = g_modelViewer->lightControl->GetCurrentPos();
						temp.z = vRot0.z - mul;
						g_modelViewer->lightControl->SetPos(temp); 
						g_modelViewer->lightControl->Update();
					} else {
						Zoom(mul, false);
						my = py;
					}

				} else {
					mul = (mul / 1200.0f) * dy;
					Zoom(mul, true);
					my = py;
				}
			}

		} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
			if (pz != 0) {
				mul = (mul / 120.0f) * pz;
				if (!wxGetKeyState(WXK_ALT)) {
					if (bMouseLight) {
						Vec4D temp = g_modelViewer->lightControl->GetCurrentPos();
						temp.z -= mul / 10.0f;
						g_modelViewer->lightControl->SetPos(temp); 
						g_modelViewer->lightControl->Update();
					} else {
						Zoom(mul, false);
					}
				} else {
					mul /= 50.0f;
					Zoom(mul, true);
				}
			}
		}
	} else if (adt) {
		// Copied from WMO controls.

		if (event.ButtonDown()) {
			mx = px;
			my = py;

		} else if (event.Dragging()) {
			int dx = mx - px;
			int dy = my - py;
			mx = px;
			my = py;

			if (event.LeftIsDown() && event.RightIsDown()) {
				adt->viewpos.y -= dy*mul;
			} else if (event.LeftIsDown()) {
				adt->viewrot.x -= dx*mul/5;
				adt->viewrot.y -= dy*mul/5;
			} else if (event.RightIsDown()) {
				adt->viewrot.x -= dx*mul/5;
				float f = cos(adt->viewrot.y * piover180);
				float sf = sin(adt->viewrot.x * piover180);
				float cf = cos(adt->viewrot.x * piover180);
				adt->viewpos.x -= sf * mul * dy * f;
				adt->viewpos.z += cf * mul * dy * f;
				adt->viewpos.y += sin(adt->viewrot.y * piover180) * mul * dy;
			} else if (event.MiddleIsDown()) {
				//?
			}

		} else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
			//?
		}
	}


	//if (event.GetEventType() == wxEVT_ENTER_WINDOW)
	//	SetFocus();
}

void ModelCanvas::InitGL()
{
	// Initiate our default OpenGL settings
	SetCurrent();
	video.InitGL();

	// If no g_modelViewer->lightControl object, exit for now
	if (!g_modelViewer || !g_modelViewer->lightControl)
		return;

	// Setup lighting
	g_modelViewer->lightControl->Init();
	g_modelViewer->lightControl->UpdateGL();

	init = true;

	// load up our shaders
	InitShaders();

	// init the default view
	InitView();
}

void ModelCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	// Set this window handler as the reference to draw to.
	wxPaintDC dc(this);

	if (!init)
		InitGL();

	if (video.render) {
		if (wmo)
			RenderWMO();
		else if (model)
			RenderModel();
		else if (adt)
			RenderADT();
		else
			Render();
	}
}

inline void ModelCanvas::CreateTexture(wxString filename, GLuint texture)
{
	BYTE *buffer = NULL;
	CxImage *image = NULL;

	if (!wxFile::Exists(filename))
		return;

	bgImagePath = filename;

	// Get the file extension and load the file
	wxString tmp = filename.AfterLast(wxT('.')).Lower();

	if (tmp == wxT("bmp"))
		image = new CxImage(filename.mb_str(), CXIMAGE_FORMAT_BMP);
	else if (tmp == wxT("tga"))
		image = new CxImage(filename.mb_str(), CXIMAGE_FORMAT_TGA);
	else if (tmp == wxT("jpg"))
		image = new CxImage(filename.mb_str(), CXIMAGE_FORMAT_JPG);
	else if (tmp == wxT("png"))
		image = new CxImage(filename.mb_str(), CXIMAGE_FORMAT_PNG);
	else 
		return;

	if (image == NULL)
		return;
	

	long size = image->GetWidth() * image->GetHeight() * 4;
	image->Encode2RGBA(buffer, size);

	//GLint format = GL_RGBA;
	GLuint texFormat = 0;
	//texFormat = GL_TEXTURE_RECTANGLE_ARB;
	texFormat = GL_TEXTURE_2D;

	// Setup the OpenGL Texture stuff
	glGenTextures(1, &texture);
	glBindTexture(texFormat, texture);
	
	// Build Mipmaps (builds different versions of the picture for distances - looks better)
	//gluBuild2DMipmaps(GL_TEXTURE_2D, 3, pBitmap->sizeX, pBitmap->sizeY, GL_RGB, GL_UNSIGNED_BYTE, pBitmap->data);

	// Lastly, we need to tell OpenGL the quality of our texture map.  GL_LINEAR_MIPMAP_LINEAR
	// is the smoothest.  GL_LINEAR_MIPMAP_NEAREST is faster than GL_LINEAR_MIPMAP_LINEAR, 
	// but looks blotchy and pixilated.  Good for slower computers though.  
	//glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

	glTexParameteri(texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// Linear Filtering
	glTexParameteri(texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// Linear Filtering
	
	glTexImage2D(texFormat, 0, GL_RGBA8, image->GetWidth(), image->GetHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

	wxDELETE(image);
	wxDELETE(buffer);
}

inline void ModelCanvas::GenerateShadowMap()
{
	/*
	GLfloat lightToSceneDistance, nearPlane, fieldOfView;
	GLfloat lightModelview[16], lightProjection[16];

	// Save the depth precision for where it's useful
	lightToSceneDistance = sqrt(LIGHT_POS[0] * LIGHT_POS[0] + 
	LIGHT_POS[1] * LIGHT_POS[1] + 
	LIGHT_POS[2] * LIGHT_POS[2]);
	nearPlane = lightToSceneDistance - 150.0f;
	if (nearPlane < 50.0f)
	nearPlane = 50.0f;
	// Keep the scene filling the depth texture
	fieldOfView = 17000.0f / lightToSceneDistance;

	glViewport(0, 0, 512, 512);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-12.0, 12.0, -20.0, 5.0, 13.0, 150.0);
	glGetFloatv(GL_PROJECTION_MATRIX, lightProjection);
	// Switch to light's point of view
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(LIGHT_POS[0], LIGHT_POS[1], LIGHT_POS[2], 
	0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	glGetFloatv(GL_MODELVIEW_MATRIX, lightModelview);

	// Clear the window with current clearing color
	glClear(GL_DEPTH_BUFFER_BIT);

	// All we care about here is resulting depth values
	glShadeModel(GL_FLAT);
	glDisable(GL_LIGHTING);
	glDisable(GL_COLOR_MATERIAL);
	glDisable(GL_NORMALIZE);
	glColorMask(0, 0, 0, 0);

	// Overcome imprecision
	glEnable(GL_POLYGON_OFFSET_FILL);

	// Draw objects in the scene
	RenderObjects(false);

	// Copy depth values into depth texture
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 
	0, 0, 512, 512, 0);

	// Restore normal drawing state
	glShadeModel(GL_SMOOTH);
	glEnable(GL_LIGHTING);
	glEnable(GL_COLOR_MATERIAL);
	glEnable(GL_NORMALIZE);
	glColorMask(1, 1, 1, 1);
	glDisable(GL_POLYGON_OFFSET_FILL);

	// Set up texture matrix for shadow map projection
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glTranslatef(0.5f, 0.5f, 0.5f);
	glScalef(0.5f, 0.5f, 0.5f);
	glMultMatrixf(lightProjection);
	glMultMatrixf(lightModelview);
	-------------------------------------------------------
	*/
	/*
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glViewport( 0, 0, 512, 512 );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glFrustum(-0.25, 0.25, -0.5, 0.1, 0.3, 150.0);

	// set up the camera to be at the light's position
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	gluLookAt(LIGHT_POS[0], LIGHT_POS[1], LIGHT_POS[2], 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

	// set up the texture matrix
	GLfloat modelview[16], projection[16];
	glGetFloatv(GL_PROJECTION_MATRIX, projection);
	glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glTranslatef(0.5f, 0.5f, 0.5f);
	glScalef(0.5f, 0.5f, 0.5f);
	glMultMatrixf(projection);
	glMultMatrixf(modelview);
	glMatrixMode(GL_MODELVIEW);

	glDisable(GL_LIGHTING);
	glEnable(GL_POLYGON_OFFSET_FILL);
	RenderObjects(true);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_shadowMap);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 0, 0, 512, 512, 0);
	glActiveTexture(GL_TEXTURE0);

	SetupProjection(m_windowWidth, m_windowHeight);
	glClear( GL_DEPTH_BUFFER_BIT );
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	*/
}

inline void ModelCanvas::RenderGrid() 
{
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

			glNormal3f(0, 1, 0);

			glVertex3f(j,  0, i);
			glVertex3f(j,  0, i+1);
			glVertex3f(j+1,0, i+1);
			glVertex3f(j+1,0, i);
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
	GLUquadricObj *quadratic = gluNewQuadric();		// Storage For Our Quadratic Object & // Create A Pointer To The Quadric Object
	gluQuadricNormals(quadratic, GLU_SMOOTH);		// Create Smooth Normals

	glPushMatrix();

	//glEnable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	
	//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, l->diffuse);
	glColor4f(l->diffuse.x, l->diffuse.y, l->diffuse.z, 0.5f);

	glTranslatef(l->pos.x, l->pos.y, l->pos.z);

	// rotate the objects to point in the right direction
	//Vec3D rot(l->pos.x, l->pos.y, l->pos.z);
	//float theta = rot.thetaXZ(l->target);
	//glRotatef(theta * rad2deg, 0.0f, 1.0f, 0.0f);
	
	gluSphere(quadratic, 0.15, 8, 8);

	if (l->type == LIGHT_DIRECTIONAL) { // Directional light
		glBegin(GL_LINES);
		glVertex3f(0, 0, 0);
		glVertex3f(l->target.x, l->target.y, l->target.z);
		glEnd();

	} else if (l->type == LIGHT_POSITIONAL) {	// Positional Light
		
	} else {	// Spot light
		
	}

	glEnable(GL_LIGHTING);
	glPopMatrix();
}

inline void ModelCanvas::RenderSkybox()
{
	// ************** SKYBOX *************
	glPushMatrix();		// Save the current modelview matrix
	glLoadIdentity();	// Reset it
	
	float fScale = 64.0f / skyModel->rad;
	
	glTranslatef(0.0f, 0.0f, -5.0f);	// Position the sky box
	glScalef(fScale, fScale, fScale);	// Scale it so it looks appropriate
	sky->draw(this);					// Render the skybox

	glPopMatrix();						// load the old modelview matrix that we saved previously
	// ====================================
}

inline void ModelCanvas::RenderObjects()
{
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

		if (video.supportGLSL) {
			/*
			// Per pixel lighting, experimental
			perpixelShader.Enable();
			int texture_location = perpixelShader.GetVariable("base_texture");
			perpixelShader.SetInt(texture_location, 0);
			// */			

			// Toon (cel) Shading, experimental effect
			//toonShader.Enable();
			//int texture_location = toonShader.GetVariable("base_texture");
			//toonShader.SetInt(texture_location, 0);

			// Glow shader
			//glowShader.Enable();

			// Blur shader
			//blurShader.Enable();
		}
	}
	// ===============================================
	
	//model->animcalc = false;
		
	//glEnable(GL_NORMALIZE);
	root->draw(this);
	//glDisable(GL_NORMALIZE);

	if (video.supportGLSL) {
		//perpixelShader.Disable();
		//toonShader.Disable();
		//glowShader.Disable();
		//blurShader.Disable();
	}
	
	if (!video.useMasking) {
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
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	//glOrtho(0, video.xRes, 0, video.yRes, -1.0, 1.0);
	gluOrtho2D(0, 1, 0, 1);

	glEnable(GL_TEXTURE_2D);					// Enable 2D Texture Mapping
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	
	glBindTexture(GL_TEXTURE_2D, uiBGTexture);

#if defined(_WINDOWS) && !defined(_MINGW)
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

		/*
		glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
		glTexCoord2f(1.0f, 0.0f); glVertex2i(video.xRes, 0);
		glTexCoord2f(1.0f, 1.0f); glVertex2i(video.xRes, video.yRes);
		glTexCoord2f(0.0f, 1.0f); glVertex2i(0,  video.yRes);
		*/
	glEnd();

	// ModelView
	glPopMatrix();
	// Projection
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	
	// Set back to modelview for rendering
	glMatrixMode(GL_MODELVIEW);
}

void ModelCanvas::Render()
{
	// Sets the "clear" colour.  Without this you get the "ghosting" effecting 
	// as the buffer doesn't get set/cleared.
	if (video.useMasking)
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	else
		glClearColor(vecBGColor.x, vecBGColor.y, vecBGColor.z, 0.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	// (re)set the view
	InitView();

	// If masking isn't enabled
	if (!video.useMasking) {
		// Draw the background image if any
		if(drawBackground)
			RenderBackground();

		// render the skybox, if any
		if (drawSky && skyModel && sky->model)
			RenderSkybox();
	}

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
	InitView();

	// If masking isn't enabled
	if (!video.useMasking) {
		// Draw the background image if any
		if(drawBackground)
			RenderBackground();
				
		// render the skybox, if any
		if (drawSky && skyModel && sky->model)
			RenderSkybox();
	}

	camera.Setup();

	// This is redundant and no longer needed.
	// all lighting stuff needs to be reorganised
	// ************* Absolute Lighting *******************
	// All our lighting related rendering code
	// Use model lighting?
	if (model && (lightType==LIGHT_MODEL_ONLY)) {
		Vec4D la;

		if (model->header.nLights > 0) {
			la = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		} else {
			la = Vec4D(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Set the Model Ambience lighting.
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, la);

	// Dynamic
	} else if (lightType == LIGHT_DYNAMIC) {
		for (size_t i=0; i<MAX_LIGHTS; i++) {
			if (g_modelViewer->lightControl->lights[i].enabled && !g_modelViewer->lightControl->lights[i].relative) {
				glLightfv(GL_LIGHT0 + (GLenum)i, GL_POSITION, g_modelViewer->lightControl->lights[i].pos);

				// Draw our 'light cone' to represent the light.
				if (drawLightDir)
					RenderLight(&g_modelViewer->lightControl->lights[i]);
			}
		}
		
	// Ambient lighting is just a single colour applied to all rendered vertices.
	} else if (lightType==LIGHT_AMBIENT) {
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, g_modelViewer->lightControl->lights[0].diffuse);	// use diffuse, as thats our main 'colour setter'
	}
	// ==============================================

	// This is also redundant
	// The camera class should be taking over this crap
	// *************************
	// setup the view/projection
	if (model) {
		if (useCamera && model->hasCamera) {
			model->cam[0].setup();
		} else {
			// TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
			glTranslatef(model->pos.x, model->pos.y, -model->pos.z);
			glRotatef(model->rot.x, 1.0f, 0.0f, 0.0f);
			glRotatef(model->rot.y, 0.0f, 1.0f, 0.0f);
			glRotatef(model->rot.z, 0.0f, 0.0f, 1.0f);
			// --==--
		}
	}
	// ==========================
			
	// As above for lighting
	// ************* Relative Lighting *******************
	// More lighting code, this is to setup the g_modelViewer->lightControl->lights that are 'relative' to the model.
	if (model && (lightType==LIGHT_DYNAMIC)) { // Else, for all our models, we use the new "lighting control", IF we're not using model only lighting		
		// loop through the g_modelViewer->lightControl->lights of our lighting system checking to see if they are turned on
		// and if so to apply their settings.
		for (size_t i=0; i<MAX_LIGHTS; i++) {
			if (g_modelViewer->lightControl->lights[i].enabled && g_modelViewer->lightControl->lights[i].relative) {
				glLightfv(GL_LIGHT0 + (GLenum)i, GL_POSITION, g_modelViewer->lightControl->lights[i].pos);
				
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
	if (model) {
		//if (video.supportFragProg)
		//	deathShader.Enable();
		if (video.supportGLSL) {
			// Per pixel lighting, experimental
			//perpixelShader.Enable();
			//int texture_location = perpixelShader.GetVariable("base_texture");
			//perpixelShader.SetInt(texture_location, 0);
		}

		glEnable(GL_NORMALIZE);
		RenderObjects();
		glDisable(GL_NORMALIZE);

		//if (video.supportFragProg)
		//	deathShader.Disable();

		// Blur/Glowing effects
		if (video.supportGLSL) {
			//perpixelShader.Disable();
			//RenderToTexture();
		}
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
	
	glMatrixMode(GL_PROJECTION);						// Select The Projection Matrix
	glPushMatrix();
	glLoadIdentity();									// Reset The Projection Matrix

	// Calculate The Aspect Ratio Of The Window
	gluPerspective(video.fov, (float)rtt[0]->nWidth/(float)rtt[0]->nHeight, 0.1f, 128.0f*5);

	glMatrixMode(GL_MODELVIEW);							// Select The Modelview Matrix
	glPushMatrix();
	glLoadIdentity();									// Reset The Modelview Matrix

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

	// ==========================================================
	// BLURRING SCREEN SPACE TEXTURE
	if (fxBlur) {
		blurShader.Enable();
		rtt[0]->BindTexture();
		
		// How many passes do we want?   The more passes the more bluring.
		for(int i=0; i<5; i++, buff_index=!buff_index) {
			rtt[!buff_index]->BeginRender();
			rtt[buff_index]->BindTexture(); // binding buffer to a texture
			
			glBegin(GL_QUADS);
			glTexCoord2d(0,	0);	glVertex3d(0, 0, -1); 
			glTexCoord2d(1, 0);	glVertex3d(1, 0, -1);
			glTexCoord2d(1, 1);	glVertex3d(1, 1, -1);
			glTexCoord2d(0, 1);	glVertex3d(0, 1, -1);
			glEnd();

			rtt[buff_index]->ReleaseTexture();
			rtt[!buff_index]->EndRender();
		}

		blurShader.Disable();
	}
	// =============================================================

	
	// =============================================================
	// RENDERING SCREEN SPACE TEXTURE TO FRAMEBUFFER
	if (fxGlow) {
		glowShader.Enable();
		rtt[buff_index]->BindTexture();

		// additive blending setup
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		
		glBegin(GL_QUADS);
		glTexCoord2d(0,	0);	glVertex3d(0, 0, -1); 
		glTexCoord2d(1, 0);	glVertex3d(1, 0, -1);
		glTexCoord2d(1, 1);	glVertex3d(1, 1, -1);
		glTexCoord2d(0, 1);	glVertex3d(0, 1, -1);
		glEnd();

		glDisable(GL_BLEND);
		glowShader.Disable();
		rtt[buff_index]->ReleaseTexture();	// release pbuffer texture for further rendering
	}
	
	// =============================================================
	// RENDERING FOG-LIKE EFFECTS USING MULTI TEXTURING
	if (fxFog) {
		multitexShader.Enable();

		// Activate the first texture ID and bind the background texture to it
		glActiveTextureARB(GL_TEXTURE0_ARB);
		//glBindTexture(GL_TEXTURE_2D,  g_Texture[0]);
		//rtt[0]->BindTexture();
		glEnable(GL_TEXTURE_2D);

		// Activate the second texture ID and bind the fog texture to it
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D,  fogTex);
		glEnable(GL_TEXTURE_2D);

		// Here pass in our texture unit 0 (GL_TEXTURE0_ARB) for "texture1" in the shader.
		multitexShader.SetInt(multitexShader.GetVariable("texture1"), 0);

		// Here pass in our texture unit 1 (GL_TEXTURE1_ARB) for "texture2" in the shader.
		multitexShader.SetInt(multitexShader.GetVariable("texture2"), 1);

		// Like our "time" variable in the first shader tutorial, we pass in a continually
		// increasing float to create the animated wrapping effect of the fog.
		static float wrap = 0.0f;
		multitexShader.SetFloat(multitexShader.GetVariable("wrap"), wrap);
		wrap += 0.002f;
		
		// Display a multitextured quad texture to the screen
		glBegin(GL_QUADS);
			// Display the top left vertice with each texture's texture coordinates
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 1.0f);
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 1.0f);
			glVertex2f(0, 1);

			// Display the bottom left vertice with each texture's coordinates
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 0.0f);
			glVertex2f(0, 0);

			// Display the bottom right vertice with each texture's coordinates
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 0.0f);
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 0.0f);
			glVertex2f(1, 0);

			// Display the top right vertice with each texture's coordinates
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 1.0f);
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 1.0f);
			glVertex2f(1, 1);
		glEnd();

		multitexShader.Disable();
	}

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

	//SetupProjection(modelsize);
	InitView();

	// Lighting
	Vec4D la;
	// From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
	for (size_t i=0; i<4; i++) {
		GLuint light = GL_LIGHT0 + (GLuint)i;
		glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
		glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
		glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
		glDisable(light);
	}
	la = Vec4D(0.35f, 0.35f, 0.35f, 1.0f);

	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, la);
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
	camera.Setup();


	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	root->draw(this);
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

	//SetupProjection(modelsize);
	InitView();

	// Lighting
	Vec4D la;
	// From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
	for (size_t i=0; i<4; i++) {
		GLuint light = GL_LIGHT0 + (GLuint)i;
		glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
		glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
		glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
		glDisable(light);
	}
	la = Vec4D(0.35f, 0.35f, 0.35f, 1.0f);

	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, la);
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
	camera.Setup();


	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	root->draw(this);
	//root->drawParticles(true);

	//glFlush();
	//glFinish();
	SwapBuffers();

	// cleanup
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


#ifdef _WINDOWS
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
	Vec4D la;
	// From what I can tell, WoW OpenGL only uses 4 g_modelViewer->lightControl->lights
	for (size_t i=0; i<4; i++) {
		GLuint light = GL_LIGHT0 + (GLuint)i;
		glLightf(light, GL_CONSTANT_ATTENUATION, 0.0f);
		glLightf(light, GL_LINEAR_ATTENUATION, 0.7f);
		glLightf(light, GL_QUADRATIC_ATTENUATION, 0.03f);
		glDisable(light);
	}
	la = Vec4D(0.35f, 0.35f, 0.35f, 1.0f);

	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, la);
	glColor3f(1.0f, 1.0f, 1.0f);
	// --==--

	// View
	// TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
	if (model) {
		glTranslatef(model->pos.x, model->pos.y, -model->pos.z);
		glRotatef(model->rot.x, 1.0f, 0.0f, 0.0f);
		glRotatef(model->rot.y, 0.0f, 1.0f, 0.0f);
		glRotatef(model->rot.z, 0.0f, 0.0f, 1.0f);
	}
	// --==--

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	root->draw(this);
	//root->drawParticles(true);
}
#endif

void ModelCanvas::RenderToBuffer()
{
	if (!init || !video.supportFBO) {
		InitGL();
		g_modelViewer->lightControl->UpdateGL();
	}
	
	// --==--
	// Reset the render state
	glLoadMatrixf(defaultMatrix);

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
	
#ifdef _WINDOWS
	if (rt) {
		glPushAttrib(GL_VIEWPORT_BIT);
		glViewport(0, 0, rt->nWidth, rt->nHeight);
		video.ResizeGLScene(rt->nWidth, rt->nHeight);
	}
#endif	

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
	if (!video.useMasking) {
		// Draw the background image if any
		if(drawBackground)
			RenderBackground();
				
		// render the skybox, if any
		if (drawSky && skyModel && sky->model)
			RenderSkybox();
	}

	camera.Setup();
		
	// Render the grid if wanted and masking isn't enabled
	if (drawGrid && !video.useMasking)
		RenderGrid();
	
	/*
	// Use model lighting?
	if (model && (lightType==LT_MODEL_ONLY)) {
		Vec4D la;

		if (model->header.nLights > 0) {
			la = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		} else {
			la = Vec4D(1.0f, 1.0f, 1.0f, 1.0f);
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
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, g_modelViewer->lightControl->lights[0].diffuse);	// use diffuse, as thats our main 'colour setter'
	}
	

	*/
	// *************************
	// setup the view/projection
	if (model) {
		if (useCamera && model->hasCamera) {
			model->cam[0].setup();
		} else {
			// TODO: Possibly move this into the Model/Attachment/Displayable::draw() routine?
			glTranslatef(model->pos.x, model->pos.y, -model->pos.z);
			glRotatef(model->rot.x, 1.0f, 0.0f, 0.0f);
			glRotatef(model->rot.y, 0.0f, 1.0f, 0.0f);
			glRotatef(model->rot.z, 0.0f, 0.0f, 1.0f);
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
	if (model)
		RenderObjects();

#ifdef _WINDOWS
	if (rt)
		glPopAttrib();
#endif
}




void ModelCanvas::OnTimer(wxTimerEvent& event)
{
	if (video.render && init) {
		CheckMovement();
		tick();
		Refresh(false);
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

	if (model) {
		if (model->animManager && !wmo) {
			if (model->animManager->IsPaused())
				ddt = 0;
			
			if (!model->animManager->IsParticlePaused())
				ddt = model->animManager->GetTimeDiff();
		}
		
		root->tick(ddt);
	}

	if (drawSky && sky && skyModel) {
		sky->tick(ddt);
	}

}

/*
void ModelCanvas::TogglePause()
{
	if (!bPaused) {
		// pause
		bPaused = true;
		pauseTime = timeGetTime();

	} else {
		// unpause
		DWORD t = timeGetTime();
		deltaTime += t - pauseTime;
		if (time==0) deltaTime = t;
		bPaused = false;
	}
}
*/

void ModelCanvas::ResetView()
{
	model->rot = Vec3D(0,-90.0f,0);
	model->pos = Vec3D(0, 0, 5.0f);

	bool isSkyBox = (model->name.substr(0,3)==wxT("Env"));
	if (!isSkyBox) {
		if (model->name.find(wxT("SkyBox"))<model->name.length())
			isSkyBox = true;
	}

	if (isSkyBox) {
		// for skyboxes, don't zoom out ;)
		model->pos.y = model->pos.z = 0.0f;
	} else {
		model->pos.z = model->rad * 1.6f;
		if (model->pos.z < 3.0f) model->pos.z = 3.0f;
		if (model->pos.z > 64.0f) model->pos.z = 64.0f;
		
		//ofsy = (model->anims[model->currentAnim].boxA.y + model->anims[model->currentAnim].boxB.y) * 0.5f;
		model->pos.y = -model->rad * 0.5f;
		if (model->pos.y > 50) model->pos.y = 50;
		if (model->pos.y < -50) model->pos.y = -50;
	}

	modelsize = model->rad * 2.0f;
	
	if (model->name.substr(0,4)==wxT("Item")) 
		model->rot.y = 0; // items look better facing right by default
}

void ModelCanvas::ResetViewWMO(int id)
{
	if (!wmo || id>=(int)wmo->nGroups) 
		return;

	wmo->viewrot = Vec3D(-90.0f, 0.0f, 0.0f);
	//model->rot = Vec3D(0.0f, -90.0f, 0.0f);
	//model->pos = Vec3D(0.0f, 0.0f, 5.0f);
	Vec3D mid;

	if (id==-1) {
		//model->pos.z = (wmo->v2-wmo->v1).length();
		mid = (wmo->v1+wmo->v2)*0.3f;
	} else {
		// zoom/center on current WMO group
		WMOGroup &g = wmo->groups[id];
		//model->pos.z = (g.v2-g.v1).length();
		mid = (g.v1+g.v2)*0.5f;
	}

	//modelsize = model->pos.z;

	//if (model->pos.z < 3.0f) model->pos.z = 3.0f;
	//if (model->pos.z > 500.0f) model->pos.z = 500.0f;

	//model->pos.y = (model->pos.y / 2);
}

void ModelCanvas::LoadBackground(wxString filename)
{
	BYTE *buffer = NULL;
	CxImage *image = NULL;

	if (!wxFile::Exists(filename))
		return;

	bgImagePath = filename;

	// Get the file extension and load the file
	wxString tmp = filename.AfterLast(wxT('.'));
	tmp.MakeLower();

	//GLuint texFormat = GL_TEXTURE_RECTANGLE_ARB;
	GLuint texFormat = GL_TEXTURE_2D;

	if (tmp == wxT("avi")) {
#ifdef _WINDOWS
#ifndef _MINGW
		cAvi.SetFileName(filename.c_str());
		cAvi.InitEngineForRead();
#endif
		// Setup the OpenGL Texture stuff
		glGenTextures(1, &uiBGTexture);
		glBindTexture(texFormat, uiBGTexture);
		
		glTexParameteri(texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// Linear Filtering
		glTexParameteri(texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// Linear Filtering
#ifndef _MINGW
		cAvi.GetFrame();
#endif
		drawBackground = true;
		drawAVIBackground = true;
#endif
	} else {
		unsigned int format;
		if (tmp == wxT("bmp"))
			format = CXIMAGE_FORMAT_BMP;
		else if (tmp == wxT("tga"))
			format = CXIMAGE_FORMAT_TGA;
		else if (tmp == wxT("jpg"))
			format = CXIMAGE_FORMAT_JPG;
		else if (tmp == wxT("png"))
			format = CXIMAGE_FORMAT_PNG;
		else 
			return;
			
		image = new CxImage(filename.mb_str(), format);
		if (image == NULL)
			return;

		long size = image->GetWidth() * image->GetHeight() * 4;
		image->Encode2RGBA(buffer, size);

		// Setup the OpenGL Texture stuff
		glGenTextures(1, &uiBGTexture);
		glBindTexture(texFormat, uiBGTexture);
		
		glTexParameteri(texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// Linear Filtering
		glTexParameteri(texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// Linear Filtering
		
		glTexImage2D(texFormat, 0, GL_RGBA8, image->GetWidth(), image->GetHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
		drawBackground = true;
	}

	wxDELETE(image);
	wxDELETE(buffer);
}

void ModelCanvas::Zoom(float f, bool rel)
{
	if (!model)
		return;
	if (rel) {
		float cosx = cos(model->rot.x * piover180);
		model->pos.x += cos(model->rot.y * piover180) * cosx * f;
		model->pos.y += sin(model->rot.x * piover180) * sin(model->rot.y * piover180) * f;
		model->pos.z += sin(model->rot.y * piover180) * cosx * f;
	} else {
		model->pos.z -= f;
	}

	//viewControl->Refresh();
}

// Check for keyboard input
void ModelCanvas::OnKey(wxKeyEvent &event)
{
	// error checking
	if(!model) 
		return;
	
	// --
	int keycode = event.GetKeyCode(); 

	// if(bInGameKeys) {
		
	// } else {
		// animation speed toggles
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
		
		// --	
	// }
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

	
	if (wxGetKeyState(WXK_NUMPAD8))	// Move forward
		camera.MoveForward(-0.1f);
	if (wxGetKeyState(WXK_NUMPAD2))	// Move Backwards
		camera.MoveForward(0.1f);
	if (wxGetKeyState(WXK_NUMPAD7))	// Rotate left
		camera.RotateY(1.0f);
	if (wxGetKeyState(WXK_NUMPAD9))	// Rotate right
		camera.RotateY(-1.0f);
	if (wxGetKeyState(WXK_NUMPAD5))	// Reset Camera
		camera.Reset();
	if (wxGetKeyState(WXK_NUMPAD4))	// Straff Left
		camera.Strafe(-0.05f);
	if (wxGetKeyState(WXK_NUMPAD6))	// Straff Right
		camera.Strafe(0.05f);

	// M2 Model only stuff below here
	if (!model || !model->animManager)
		return;

	float speed = 1.0f;

	// Time stuff
	if (model)
		speed = ((timeGetTime() - lastTime) * model->animManager->GetSpeed()) / 7.0f;
	else
		speed = (timeGetTime() - lastTime);

	//lastTime = timeGetTime();

	// Turning
	if (wxGetKeyState(WXK_LEFT)) {
		model->rot.y += speed;

		if (model->rot.y > 360) model->rot.y -= 360;
		if (model->rot.y < 0) model->rot.y += 360;
		
	} else if (wxGetKeyState(WXK_RIGHT)) {
		model->rot.y -= speed;

		if (model->rot.y > 360) model->rot.y -= 360;
		if (model->rot.y < 0) model->rot.y += 360;
	}
	// --

	// Moving forward/backward
	//float speed = 0.0f;
	if (model->animated)
		speed *= (model->anims[model->currentAnim].moveSpeed / 160.0f);
	//else
	//	speed *= 0.05f;

	if (wxGetKeyState(WXK_UP))
		Zoom(speed, true);
	else if (wxGetKeyState(WXK_DOWN))
		Zoom(-speed, true);
	// --
}

// Our screenshot function which supports both PBO and FBO aswell as traditional older cards, eventually.
void ModelCanvas::Screenshot(const wxString fn, int x, int y)
{
	CxImage *newImage = NULL;
#ifdef _WINDOWS
	wxDELETE(rt);
#endif

	if (!init)
		InitGL();

	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	wxFileName temp(fn, wxPATH_NATIVE);
	
	unsigned char *pixels = NULL;
	int screenSize[4];

#ifdef _WINDOWS
	// Setup out buffers for offscreen rendering
	if (video.supportPBO || video.supportFBO) {
		rt = new RenderTexture();

		if (!rt)
			return;

		rt->Init((HWND)this->GetHandle(), x, y, video.supportFBO);
		
		screenSize[2] = rt->nWidth;
		screenSize[3] = rt->nHeight;
		rt->BeginRender();

		if (wmo)
			RenderWMOToBuffer();
		else if (model)
			RenderToBuffer();

		rt->BindTexture();

	} else {		
		glGetIntegerv(GL_VIEWPORT, screenSize);
		glReadBuffer(GL_BACK);

		if (wmo)
			RenderWMOToBuffer();
		else if (model)
			RenderToBuffer();
	}
#endif

	// (width*height*bytesPerPixel) - 32bit, 4 bytes
	pixels = new unsigned char[screenSize[2]*screenSize[3]*4];

	// read in the pixel data
	glReadPixels(0, 0, screenSize[2], screenSize[3], GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
	// read in the texture data
	//glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
	
#ifdef _WINDOWS
	if (rt) {
		rt->ReleaseTexture();
		rt->EndRender();
		rt->Shutdown();
		wxDELETE(rt);
	}
#endif

	newImage = new CxImage(0);
	newImage->AlphaCreate();	// Create the alpha layer
	newImage->IncreaseBpp(32);	// set image to 32bit 
	newImage->CreateFromArray(pixels, screenSize[2], screenSize[3], 32, (screenSize[2]*4), false);
	
	bool succ = true;

	// Save
	if (temp.GetExt() == wxT("tga")) {
		newImage->Save(fn.fn_str(), CXIMAGE_FORMAT_TGA);
	} else if (temp.GetExt() == wxT("png")) {
		newImage->Save(fn.fn_str(), CXIMAGE_FORMAT_PNG);
	//} else if (temp.GetExt() == "jp2") {
	//	newImage->Save(fn.fn_str(), CXIMAGE_FORMAT_JP2);
	} else if (temp.GetExt() == wxT("jpg")) {
		newImage->SetJpegQuality(100);
		newImage->SetJpegScale(100);
		newImage->Save(fn.fn_str(), CXIMAGE_FORMAT_JPG);
	} else if (temp.GetExt() == wxT("bmp")) // Save Bitmap format
		newImage->Save(fn.fn_str(), CXIMAGE_FORMAT_BMP);
	else
		succ = false;

	if (succ)
		wxLogMessage(wxT("Screenshot saved to: %s"), fn.c_str());

	newImage->Destroy();
	wxDELETE(newImage);

	// free the memory
	wxDELETEA(pixels);

	// Set back to normal
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

// Save the scene state,  currently this is just position/rotation/field of view
void ModelCanvas::SaveSceneState(int id)
{
	if (!model)
		return;

	// bounds check
	if (id > -1 && id < 4) {
		sceneState[id].pos = model->pos;
		sceneState[id].rot = model->rot;
		sceneState[id].fov = video.fov;
	}
}

// Load the scene state, as above
void ModelCanvas::LoadSceneState(int id)
{
	if (!model)
		return;

	// bounds check
	if (id > -1 && id < 4) {
		video.fov =  sceneState[id].fov ;
		model->pos = sceneState[id].pos;
		model->rot = sceneState[id].rot;

		int screenSize[4];
		glGetIntegerv(GL_VIEWPORT, (GLint*)screenSize);				// get the width/height of the canvas
		video.ResizeGLScene(screenSize[2], screenSize[3]);
	}
}

void ModelCanvas::SetCurrent()
{
#ifdef _WINDOWS
	video.SetCurrent();
#else
	wxGLCanvas::SetCurrent();
#endif
}

void ModelCanvas::SwapBuffers()
{
#ifdef _WINDOWS
	video.SwapBuffers();
#else
	wxGLCanvas::SwapBuffers();
#endif
}



