
#define _VIDEO_CPP_
#include "video.h"
#undef _VIDEO_CPP_

#include "logger/Logger.h"

#include "Game.h"
#include "GameFile.h"
#include "Texture.h"

#include <QImage>

// gl
#include "OpenGLHeaders.h"

#define WIP_DH_SUPPORT 1

#ifdef _LINUX // Linux
	void (*wglGetProcAddress(const char *function_name))(void)
	{
		return glXGetProcAddress((GLubyte*)function_name);
	}
#endif


#ifdef _WINDOWS
// Create an OpenGL pixel format descriptor
PIXELFORMATDESCRIPTOR pfd =						// pfd Tells Windows How We Want Things To Be
{
	sizeof (PIXELFORMATDESCRIPTOR),				// Size Of This Pixel Format Descriptor
		1,										// Version Number
		PFD_DRAW_TO_WINDOW |					// Format Must Support Window
		PFD_SUPPORT_OPENGL |					// Format Must Support OpenGL
		PFD_DOUBLEBUFFER,						// Must Support Double Buffering
		PFD_TYPE_RGBA,							// Request An RGBA Format
		16,										// Select Our Color Depth (same as desktop)
		0, 0, 0, 0, 0, 0,						// Color Bits Ignored
		1,										// Alpha Buffer
		0,										// Shift Bit Ignored
		0,										// No Accumulation Buffer
		0, 0, 0, 0,								// Accumulation Bits Ignored
		16,										// 16Bit Z-Buffer (Depth Buffer)  
		0,										// No Stencil Buffer
		0,										// No Auxiliary Buffer
		PFD_MAIN_PLANE,							// Main Drawing Layer
		0,										// Reserved
		0, 0, 0									// Layer Masks Ignored
};
#endif

_VIDEO_API_ VideoSettings video;
_VIDEO_API_ TextureManager texturemanager;

VideoSettings::VideoSettings()
{
#ifdef _WINDOWS
	hWnd = NULL;
	hRC = NULL;
	hDC = NULL;
#endif

	pixelFormat = 0;
	xRes = 0;
	yRes = 0;

	fov = 45.0f;

	init = false;
	render = false;

	//useAntiAlias = true;
	useEnvMapping = true;
}

VideoSettings::~VideoSettings()
{
#ifdef _WINDOWS
	// Clear the rendering context
	wglMakeCurrent(NULL, NULL); 

	if (hRC) {
		wglDeleteContext(hRC);
		hRC = NULL;
	}

	if (hDC) {
		ReleaseDC(hWnd,hDC);
		hDC = NULL;
	}
#endif
}

bool VideoSettings::Init()
{
  if(init)
    return true;

  glewExperimental = GL_TRUE;
	int glewErr = glewInit();

	if (glewErr != GLEW_OK)
	{
	  // problem: glewInit failed, something is seriously wrong
	  LOG_ERROR << "GLEW failed to initialize:" << glewGetErrorString(glewErr);
	  return false;
	}
	else
	{
	  LOG_INFO << "GLEW successfully initiated.";
	}

	// Now get some specifics on the card
	// First up, the details
	if(glewIsSupported("GL_EXT_texture_filter_anisotropic"))
		glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint*)&AnisofilterLevel);
	else
		AnisofilterLevel = 0;

	glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, (GLint*)&numTextureUnits);
	vendor = ((char *)glGetString(GL_VENDOR));
	version = ((char *)glGetString(GL_VERSION));
	renderer = ((char *)glGetString(GL_RENDERER));

	double num = atof((char *)glGetString(GL_VERSION));
	supportOGL20 = (num >= 2.0);
	if (supportOGL20)
		supportNPOT = true;
	else
		supportNPOT = glewIsSupported("GL_ARB_texture_non_power_of_two") == GL_TRUE ? true : false;

	supportFragProg = glewIsSupported("GL_ARB_fragment_program") == GL_TRUE ? true : false;
	supportVertexProg = glewIsSupported("GL_ARB_vertex_program") == GL_TRUE ? true : false;
	supportGLSL = glewIsSupported("GL_ARB_shading_language_100") == GL_TRUE ? true : false;
	supportShaders = (supportFragProg && supportVertexProg);
	supportMultiTex = glewIsSupported("GL_ARB_multitexture") == GL_TRUE ? true : false;
	supportDrawRangeElements = glewIsSupported("GL_EXT_draw_range_elements") == GL_TRUE ? true : false;
#if WIP_DH_SUPPORT > 0
  supportVBO = false;
#else
  supportVBO = glewIsSupported("GL_ARB_vertex_buffer_object") == GL_TRUE ? true : false;
#endif
	supportCompression = glewIsSupported("GL_ARB_texture_compression GL_ARB_texture_cube_map GL_EXT_texture_compression_s3tc") == GL_TRUE ? true : false;
	supportPointSprites = glewIsSupported("GL_ARB_point_sprite GL_ARB_point_parameters") == GL_TRUE ? true : false;
#ifdef _WINDOWS
	supportPBO = wglewIsSupported("WGL_ARB_pbuffer WGL_ARB_render_texture") == GL_TRUE ? true : false;
	supportAntiAlias = wglewIsSupported("WGL_ARB_multisample") == GL_TRUE ? true : false;
	supportWGLPixelFormat = wglewIsSupported("WGL_ARB_pixel_format") == GL_TRUE ? true : false;
#endif
	supportFBO = glewIsSupported("GL_EXT_framebuffer_object") == GL_TRUE ? true : false;

	// deactivate FBO for Intel cards, glReadPixels is known to be buggy for those cards...
	if(strcmp(vendor, "Intel") == 0)
		supportFBO = false;

	supportTexRects = glewIsSupported("GL_ARB_texture_rectangle") == GL_TRUE ? true : false;

	// Now output and log the info
	LOG_INFO << "Video Renderer:" << renderer;
	LOG_INFO << "Video Vendor:" << vendor;
	LOG_INFO << "Driver Version:" << version;

	
	if (renderer == "GDI Generic")
	{
	  LOG_INFO << "Warning: Running in software mode, this is not enough. Please try updating your video drivers.";
	  // bloody oath - wtb a graphics card
	  hasHardware = false;
	}
	else
	{
	  hasHardware = true;
	}

	LOG_INFO << "Support wglPixelFormat:" << (supportWGLPixelFormat ? "true" : "false");
	LOG_INFO << "Support Texture Compression:" << (supportCompression ? "true" : "false");
	LOG_INFO << "Support Draw Range Elements:" << (supportDrawRangeElements ? "true" : "false");
	LOG_INFO << "Support Vertex Buffer Objects:" << (supportVBO ? "true" : "false");
	LOG_INFO << "Support Point Sprites:" << (supportPointSprites ? "true" : "false");
	LOG_INFO << "Support Pixel Shaders:"  << (supportFragProg ? "true" : "false");
	LOG_INFO << "Support Vertex Shaders:" << (supportVertexProg ? "true" : "false");
	LOG_INFO << "Support GLSL:" << (supportGLSL ? "true" : "false");
	LOG_INFO << "Support Anti-Aliasing:" << (supportAntiAlias ? "true" : "false");
	LOG_INFO << "Support Pixel Buffer Objects:" << (supportPBO ? "true" : "false");
	LOG_INFO << "Support Frame Buffer Objects:" << (supportFBO ? "true" : "false");
	LOG_INFO << "Support Non-Power-of-Two:" << (supportNPOT ? "true" : "false");
	LOG_INFO << "Support Rectangle Textures:" << (supportTexRects ? "true" : "false");
	LOG_INFO << "Support OpenGL 2.0:" << (supportOGL20 ? "true" : "false");

	// Max texture sizes
	GLint texSize; 
	// Rectangle
	if (glewIsSupported("GL_ARB_texture_rectangle")) {
		glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB, &texSize); 
		LOG_INFO << "Max Rectangle Texture Size Supported:" << texSize;
	}
	// Square
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texSize); 
	LOG_INFO << "Max Texture Size Supported:" << texSize;
	

	
	// Debug:
	//supportPointSprites = false; // Always set to false,  for now.
	//supportVBO = false;
	//supportFBO = false;
	//supportNPOT = false;
	//supportPBO = false;
	//supportCompression = false;
	//supportTexRects = false;

	init = true;

	return true;
}

void VideoSettings::InitGL()
{
	if (!Init())
		return;

	GLenum err = 0;
	err = glGetError();
	if (err)
		LOG_ERROR << __FUNCTION__ << __FILE__ << err << "An error occured on line" << __LINE__;

	if (supportDrawRangeElements && supportVBO) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	} else {
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	
	if(supportAntiAlias && curCap.aaSamples>0)
		glEnable(GL_MULTISAMPLE_ARB);

	// Colour and materials
	glEnable(GL_COLOR_MATERIAL);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT);
	glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
	glColorMaterial(GL_FRONT_AND_BACK, GL_EMISSION);
	glColorMaterial(GL_FRONT_AND_BACK, GL_SPECULAR);
	
	// For environmental mapped meshes
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);


	// TODO: Implement a scene graph, or scene manager to optimise OpenGL?
	// Default texture settings.
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
	/*
	glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE, 1.000000);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE, 1.000000);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA, GL_REPLACE);
	*/
	
	/*
	// Alpha blending texture settings.
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
	glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE, 1.000000);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE, 1.000000);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA, GL_MODULATE);
	*/

	glAlphaFunc(GL_GEQUAL, 0.9f);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_SWAP_BYTES, false);
	glPixelStorei(GL_PACK_LSB_FIRST, false);

	glShadeModel(GL_SMOOTH);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	err = glGetError();
	if (err)
	  LOG_ERROR << __FUNCTION__ << __FILE__ << err << "An error occured on line" << __LINE__;
}


void VideoSettings::EnumDisplayModes()
{
#ifdef _WINDOWS
	if (!hasHardware)
		return;

	// Error check
	if (!hDC) {
		LOG_ERROR << "Unable to enumerate display modes.  Invalid HDC.";
		return;
	}

	// This is where we do the grunt work of checking the caps
	// find out how many pixel formats we have
	size_t num_pfd = 0;
	std::vector<int> iAttributes;
	std::vector<int> results;

	iAttributes.push_back(WGL_NUMBER_PIXEL_FORMATS_ARB);
	results.resize(2);	// 2 elements
	wglGetPixelFormatAttribivARB(hDC, 0, 0, 1, &iAttributes[0], &results[0]);	// get the number of contexts we can create
	num_pfd = results[0];
	iAttributes.clear();
	results.clear();

	// error check
	if (num_pfd == 0) {
		LOG_ERROR << "Could not find any display modes.  Video Card might not support the function.";
		hasHardware = false;
		return;
	}

	iAttributes.push_back(WGL_DRAW_TO_WINDOW_ARB);
	iAttributes.push_back(WGL_STENCIL_BITS_ARB);
	iAttributes.push_back(WGL_ACCELERATION_ARB);
	iAttributes.push_back(WGL_DEPTH_BITS_ARB);
	iAttributes.push_back(WGL_SUPPORT_OPENGL_ARB);
	iAttributes.push_back(WGL_DOUBLE_BUFFER_ARB);
	iAttributes.push_back(WGL_PIXEL_TYPE_ARB);
	iAttributes.push_back(WGL_COLOR_BITS_ARB);
	iAttributes.push_back(WGL_ALPHA_BITS_ARB);
	iAttributes.push_back(WGL_ACCUM_BITS_ARB);

	if(glewIsSupported("GL_ARB_multisample")) {
		iAttributes.push_back(WGL_SAMPLE_BUFFERS_ARB);
		iAttributes.push_back(WGL_SAMPLES_ARB);
	}

	results.resize(iAttributes.size());

	// Reset our list of card capabilities.
	capsList.clear();

	for (size_t i=0; i<num_pfd; i++) {
		if(!wglGetPixelFormatAttribivARB(hDC, (int)i+1,0, (UINT)iAttributes.size(), &iAttributes[0], &results[0]))
			return;
		
		// once we make it here we can look at the pixel data to make sure this is a context we want to use
		// results[0] is WGL_DRAW_TO_WINDOW,  since we only work in windowed mode,  no need to return results for fullscreen mode
		// results[4] is WGL_SUPPORT_OPENGL,  obvious if the mode doesn't support opengl then its useless to us.
		// results[3] is bitdepth,  if it has a 0 bitdepth then its useless to us
		if(results[0] == GL_TRUE && results[4] == GL_TRUE && results[3]>0) {
			// what we have here is a contect which is drawable to a window, is in rgba format and is accelerated in some way
			// so now we pull the infomation, fill a structure and shove it into our map
			VideoCaps caps;

			if(glewIsSupported("GL_ARB_multisample")) {
				caps.sampleBuffer = results[10];
				caps.aaSamples = results[11];
			} else {
				caps.sampleBuffer = false;
				caps.aaSamples = 0;
			}

			caps.accum = results[9];
			caps.alpha = results[8];
			caps.colour = results[7];
			//caps.pixelType = results[6]; /* WGL_TYPE_RGBA_ARB*/
			caps.doubleBuffer = results[5];
			caps.zBuffer = results[3];
			caps.hwAcc = results[2]; /*WGL_FULL_ACCELERATION_ARB / WGL_GENERIC_ACCELERATION_ARB / WGL_NO_ACCELERATION_ARB;*/
			caps.stencil = results[1];
			capsList.push_back(caps);	// insert into the map
		}
	}
#endif
}

// This function basically just uses any available display mode
bool VideoSettings::GetAvailableMode()
{
#ifdef _WINDOWS
	render = false;

	if (!hWnd)
		return false;

	// Clear the rendering context
	wglMakeCurrent(NULL, NULL); 

	if (hRC) {
		wglDeleteContext(hRC);
		hRC = NULL;
	}

	if (hDC) {
		ReleaseDC(hWnd,hDC);
		hDC = NULL;
	}

	hDC = GetDC(hWnd);						// Grab A Device Context For This Window
	if (hDC == 0) {							// Did We Get A Device Context?
		// Failed
		//throw std::runtime_error("Failed To Get Device Context");
		LOG_ERROR << "Failed To Get Device Context.";
		return false;
	}

	pixelFormat = ChoosePixelFormat(hDC, &pfd);				// Find A Compatible Pixel Format
	if (pixelFormat == 0) {									// Did We Find A Compatible Format?
		// Failed
		//throw std::runtime_error("Failed To Acuire PixelFormat");
		LOG_ERROR << "Failed To Accquire PixelFormat.";
		return false;
	}

	if (SetPixelFormat(hDC, pixelFormat, &pfd) == false) {		// Try To Set The Pixel Format
		// Failed
		//throw std::runtime_error("Failed To SetPixelFormat");
		LOG_ERROR << "Failed To SetPixelFormat.";
		return false;
	}

	hRC = wglCreateContext(hDC);						// Try To Get A Rendering Context
	if (hRC == 0) {								// Did We Get A Rendering Context?
		// Failed
		//throw std::runtime_error("Failed To Get OpenGL Context");
		LOG_ERROR << "Failed To Get OpenGL Context.";
		return false;
	}

	SetCurrent();
	render = true;
#endif
	return true;
}

bool VideoSettings::GetCompatibleWinMode(VideoCaps caps)
{
#ifdef _WINDOWS
	if (!hasHardware)
		return false;

	// Ask the WGL for the clostest match to this pixel format
	if (!hDC) {
		LOG_ERROR << "Attempted to Get a Compatible Window Mode Without a Valid Window";
		//throw std::runtime_error("VideoSettings::GetCompatibleWinMode() : Attempted to Get a Compatible Window Mode Without a Valid Window");
		return false;
	}

	std::vector<int> iAttributes;
	std::vector<int> results;
	unsigned int numformats;
	float fAtrributes[] = { 0,0};

	iAttributes.push_back(WGL_DRAW_TO_WINDOW_ARB);
	iAttributes.push_back(GL_TRUE);
	iAttributes.push_back(WGL_ACCELERATION_ARB);
	iAttributes.push_back(caps.hwAcc);
	iAttributes.push_back(WGL_COLOR_BITS_ARB);
	iAttributes.push_back(caps.colour);		// min color required
	iAttributes.push_back(WGL_ALPHA_BITS_ARB);
	iAttributes.push_back(caps.alpha);		// min alpha bits
	iAttributes.push_back(WGL_DEPTH_BITS_ARB);
	iAttributes.push_back(caps.zBuffer);
	iAttributes.push_back(WGL_STENCIL_BITS_ARB);
	iAttributes.push_back(caps.stencil);
	if(caps.aaSamples != 0) {
		iAttributes.push_back(WGL_SAMPLE_BUFFERS_ARB);
		iAttributes.push_back(caps.sampleBuffer);
		iAttributes.push_back(WGL_SAMPLES_ARB);
		iAttributes.push_back(caps.aaSamples);
	}
	iAttributes.push_back(WGL_DOUBLE_BUFFER_ARB);
	iAttributes.push_back(caps.doubleBuffer);
	if(caps.accum != 0) {
		iAttributes.push_back(WGL_ACCUM_BITS_ARB);
		iAttributes.push_back(caps.accum);
	}
	iAttributes.push_back(0);
	iAttributes.push_back(0);
	
	int status = wglChoosePixelFormatARB(hDC, &iAttributes[0], fAtrributes, 1, &pixelFormat, &numformats);	// find a matching format
	if (status == GL_TRUE && numformats) {
		// we have a matching format, extract the details
		iAttributes.clear();
		iAttributes.push_back(WGL_COLOR_BITS_ARB);
		iAttributes.push_back(WGL_ALPHA_BITS_ARB);
		iAttributes.push_back(WGL_DEPTH_BITS_ARB);
		iAttributes.push_back(WGL_STENCIL_BITS_ARB);
		iAttributes.push_back(WGL_SAMPLE_BUFFERS_ARB);
		iAttributes.push_back(WGL_SAMPLES_ARB);
		iAttributes.push_back(WGL_DOUBLE_BUFFER_ARB);
		iAttributes.push_back(WGL_ACCUM_BITS_ARB);
		iAttributes.push_back(WGL_ACCELERATION_ARB);
		results.resize(iAttributes.size());

		if(!wglGetPixelFormatAttribivARB(hDC, pixelFormat, PFD_MAIN_PLANE, (UINT)iAttributes.size(), &iAttributes[0], &results[0])) {
			//int i = GetLastError();
			return false;
		}

		curCap.aaSamples = results[5];
		curCap.accum = results[7];
		curCap.alpha = results[1];
		curCap.colour = results[0];
		curCap.doubleBuffer = results[6];
		curCap.sampleBuffer = results[4];
		curCap.stencil = results[3];
		curCap.zBuffer = results[2];
		curCap.hwAcc = results[8];

		for (size_t i=0; i<capsList.size(); i++) {
			if (capsList[i].colour == curCap.colour && 
				capsList[i].zBuffer == curCap.zBuffer && 
				capsList[i].alpha == curCap.alpha && 
				capsList[i].hwAcc == curCap.hwAcc && 
				capsList[i].doubleBuffer==curCap.doubleBuffer &&
				capsList[i].sampleBuffer==curCap.sampleBuffer &&
				capsList[i].aaSamples==curCap.aaSamples) {
				capIndex = (int)i;
				break;
			}
		}

		return true;
	}
#endif
	return false;
}

#ifdef _WINDOWS
void VideoSettings::SetHandle(HWND hwnd, int bpp=16)
{
	hWnd = hwnd;
	desktopBPP = bpp;

	//pfd.cColorBits = desktopBPP;

	if (!secondPass)
		GetAvailableMode();

	if (!init && !secondPass) {
		Init();
		if (supportWGLPixelFormat) {
			EnumDisplayModes();
			GetCompatibleWinMode(curCap);
		}
	}
	
	if (hasHardware && supportWGLPixelFormat)
		SetMode();
}
#endif

void VideoSettings::Release()
{
#ifdef _WINDOWS
	// Clear the rendering context
	wglMakeCurrent(NULL, NULL); 

	if (hRC) {
		wglDeleteContext(hRC);
		hRC = NULL;
	}

	if (hDC) {
		ReleaseDC(hWnd,hDC);
		hDC = NULL;
	}
#endif
}

void VideoSettings::SetMode()
{
#ifdef _WINDOWS
	if (!hWnd)
		return;

	// Clear the rendering context
	wglMakeCurrent(NULL, NULL); 

	if (hRC) {
		wglDeleteContext(hRC);
		hRC = NULL;
	}

	if (hDC) {
		ReleaseDC(hWnd,hDC);
		hDC = NULL;
	}

	hDC = GetDC(hWnd);
	if (!hDC) {						// Did We Get A Device Context?
		LOG_ERROR << "Can't Create A GL Device Context.";
		return;
	}
	
	GLboolean status;
	unsigned int numFormats;
	float fAttributes[] = { 0,0};
	std::vector<int> AttributeList;

	AttributeList.push_back(WGL_DRAW_TO_WINDOW_ARB);
	AttributeList.push_back(GL_TRUE);
	AttributeList.push_back(WGL_SUPPORT_OPENGL_ARB);
	AttributeList.push_back(GL_TRUE);
	AttributeList.push_back(WGL_ACCELERATION_ARB);
	AttributeList.push_back(curCap.hwAcc);
	AttributeList.push_back(WGL_COLOR_BITS_ARB);
	AttributeList.push_back(curCap.colour);
	AttributeList.push_back(WGL_ALPHA_BITS_ARB);
	AttributeList.push_back(curCap.alpha);
	AttributeList.push_back(WGL_DEPTH_BITS_ARB);
	AttributeList.push_back(curCap.zBuffer);
	//AttributeList.push_back(WGL_STENCIL_BITS_ARB);
	//AttributeList.push_back(curCap.stencil);
	AttributeList.push_back(WGL_DOUBLE_BUFFER_ARB);
	AttributeList.push_back(curCap.doubleBuffer);

	// 8229 being software mode
	if (curCap.hwAcc!=8229  && curCap.accum!=0) {
		AttributeList.push_back(WGL_ACCUM_BITS_ARB);
		AttributeList.push_back(curCap.accum);
	}

	if (curCap.aaSamples != 0 && supportAntiAlias) {
		AttributeList.push_back(WGL_SAMPLE_BUFFERS_ARB);
		AttributeList.push_back(GL_TRUE);
		AttributeList.push_back(WGL_SAMPLES_ARB);
		AttributeList.push_back(curCap.aaSamples);
	}
	AttributeList.push_back(0);
	AttributeList.push_back(0);
    

	status = wglChoosePixelFormatARB(hDC, &AttributeList[0], fAttributes, 1, &pixelFormat, &numFormats);
	if (status == GL_TRUE && numFormats) {
		if (SetPixelFormat(hDC, pixelFormat, &pfd) == FALSE) {
			//LOG_ERROR << "Failed To Set Requested Pixel Format.";
			secondPass = true;
			return;
		}

		hRC = wglCreateContext(hDC);
		if (!hRC) {
			LOG_ERROR << "Error: Failed To Create OpenGL Context.";
			return;
		}
		
		render = true;
		SetCurrent();
	} else {
		// Error
		// Something went wrong,  fall back to the basic opengl setup
		GetAvailableMode();
	}
#endif
}

void VideoSettings::ResizeGLScene(int width, int height)		// Resize And Initialize The GL Window
{
	if (height==0)										// Prevent A Divide By Zero By
		height=1;										// Making Height Equal One

	glViewport(0,0,width,height);						// Reset The Current Viewport
	//glDepthRange(0.0f,1.0f);

	glMatrixMode(GL_PROJECTION);						// Select The Projection Matrix
	glLoadIdentity();									// Reset The Projection Matrix

	// Calculate The Aspect Ratio Of The Window
	gluPerspective(fov, (float)width/(float)height, 0.1f, 1280*5);

	glMatrixMode(GL_MODELVIEW);							// Select The Modelview Matrix
	glLoadIdentity();									// Reset The Modelview Matrix
}


void VideoSettings::SwapBuffers()
{
#ifdef _WINDOWS
	::SwapBuffers(hDC);
#endif
}

void VideoSettings::SetCurrent()
{
#ifdef _WINDOWS
	if(!wglMakeCurrent(hDC, hRC))
	{					// Try To Activate The Rendering Context
	  render = false;
	}
	else
	{
	  render = true;
	}
#endif
}

struct Color {
	unsigned char r, g, b;
};


void decompressDXTC(GLint format, int w, int h, size_t size, unsigned char *src, unsigned char *dest)
{	
	// DXT1 Textures, currently being handles by our routine below
	if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
		DDSDecompressDXT1(src, w, h, dest);
		return;
	}
	
	// DXT3 Textures
	if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
		DDSDecompressDXT3(src, w, h, dest);
		return;
	}

	// DXT5 Textures
	if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)	{
		//DXT5UnpackAlphaValues(src, w, h, dest);
		DDSDecompressDXT5(src, w, h, dest);
		return;
	}

	/*
	// sort of copied from linghuye
	int bsx = (w<4) ? w : 4;
	int bsy = (h<4) ? h : 4;

	for(int y=0; y<h; y += bsy) {
		for(int x=0; x<w; x += bsx) {
			//unsigned long alpha = 0;
			//unsigned int a0 = 0, a1 = 0;

			unsigned int c0 = *(unsigned short*)(src + 0);
			unsigned int c1 = *(unsigned short*)(src + 2);
			src += 4;

			Color color[4];
			color[0].b = (unsigned char) ((c0 >> 11) & 0x1f) << 3;
			color[0].g = (unsigned char) ((c0 >>  5) & 0x3f) << 2;
			color[0].r = (unsigned char) ((c0      ) & 0x1f) << 3;
			color[1].b = (unsigned char) ((c1 >> 11) & 0x1f) << 3;
			color[1].g = (unsigned char) ((c1 >>  5) & 0x3f) << 2;
			color[1].r = (unsigned char) ((c1      ) & 0x1f) << 3;

			if(c0 > c1 || format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
				color[2].r = (color[0].r * 2 + color[1].r) / 3;
				color[2].g = (color[0].g * 2 + color[1].g) / 3;
				color[2].b = (color[0].b * 2 + color[1].b) / 3;
				color[3].r = (color[0].r + color[1].r * 2) / 3;
				color[3].g = (color[0].g + color[1].g * 2) / 3;
				color[3].b = (color[0].b + color[1].b * 2) / 3;
			} else {
				color[2].r = (color[0].r + color[1].r) / 2;
				color[2].g = (color[0].g + color[1].g) / 2;
				color[2].b = (color[0].b + color[1].b) / 2;
				color[3].r = 0;
				color[3].g = 0;
				color[3].b = 0;
			}

			for (ssize_t j=0; j<bsy; j++) {
				unsigned int index = *src++;
				unsigned char* dd = dest + (w*(y+j)+x)*4;
				for (size_t i=0; i<bsx; i++) {
					*dd++ = color[index & 0x03].b;
					*dd++ = color[index & 0x03].g;
					*dd++ = color[index & 0x03].r;
					//if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)	{
						*dd++ = ((index & 0x03) == 3 && c0 <= c1) ? 0 : 255;
					//}
					index >>= 2;
				}
			}
		}
	}
	*/
}
