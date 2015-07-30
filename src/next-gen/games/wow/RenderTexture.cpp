
#include "RenderTexture.h"

#include "logger/Logger.h"

bool CHECK_FRAMEBUFFER_STATUS()
{
	GLenum status = 0;
	status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

	switch(status) {
		case GL_FRAMEBUFFER_COMPLETE_EXT:
			LOG_INFO << "OGL: Framebuffer created.";
			return true;
			break; 
		case GL_FRAMEBUFFER_UNSUPPORTED_EXT: 
			LOG_ERROR << "GL_FRAMEBUFFER_UNSUPPORTED_EXT";
			/* you gotta choose different formats */ \
			//assert(0); 
			break; 
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT: 
			LOG_ERROR << "INCOMPLETE_ATTACHMENT";
			break; 
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT: 
			LOG_ERROR << "FRAMEBUFFER_MISSING_ATTACHMENT";
			break; 
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT: 
			LOG_ERROR << "FRAMEBUFFER_DIMENSIONS";
			break; 
/*		case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT: 
			LOG_ERROR << "OGL Error: INCOMPLETE_DUPLICATE_ATTACHMENT";
			break; */
		case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT: 
			LOG_ERROR << "OGL Error: INCOMPLETE_FORMATS";
			break; 
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT: 
			LOG_ERROR << "OGL Error: INCOMPLETE_DRAW_BUFFER";
			break; 
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT: 
			LOG_ERROR << "OGL Error: INCOMPLETE_READ_BUFFER";
			break; 
		case GL_FRAMEBUFFER_BINDING_EXT: 
			LOG_ERROR << "OGL Error: BINDING_EXT";
			break; 
/*		case GL_FRAMEBUFFER_STATUS_ERROR_EXT: 
			LOG_ERROR << "OGL Error: STATUS_ERROR";
			break; */
		default: 
			LOG_ERROR << "Unknown" << status;
			/* programming error; will fail on all hardware \*/ 
			//assert(0); 
			//continue;
	};

	return false;
}

void RenderTexture::InitGL()
{
	video.InitGL();

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
}

void RenderTexture::Init(int width, int height, bool fboMode)
{
	GLenum err;
	canvas_hDC = wglGetCurrentDC();
	canvas_hRC = wglGetCurrentContext();

	m_texID = 0;
	m_FBO = fboMode;

	if (width == 0 || height == 0)
	{
		int screenSize[4];
		glGetIntegerv(GL_VIEWPORT, screenSize);

		nWidth = screenSize[2];
		nHeight = screenSize[3];
	}
	else
	{
		nWidth = width;
		nHeight = height;
	}

	// Find the (easiest) format to use
	if (video.supportNPOT && video.supportTexRects && GL_TEXTURE_RECTANGLE_ARB)
	{
		m_texFormat = GL_TEXTURE_RECTANGLE_ARB;
	}
	else
	{
		// If non-power-of-two textures aren't supported, force them to be power-of-two of the max texture size.
		m_texFormat = GL_TEXTURE_2D;

		GLint texSize; 
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texSize); 

		bool end=false;
		int curMax=0;

		// Make sure its square and a power-of-two
		for (size_t i=5; !end; i++)
		{
			int iSize = powf(2,(float)i);
			if (iSize < texSize)
				curMax = iSize;
			else
				end = true;
		}

		// Divide by 2 to make sure they're no where near their max texture size
		nWidth = nHeight = (curMax/2);
	}

	// Hmm... why was this here?
	//m_texFormat = GL_TEXTURE_2D;


	if (m_FBO)
	{ // Frame Buffer OBject mode (newer, better, only supported by newer cards and drivers though)
		
		// Reported crash in this section of the code by a user
		// add exception handling to try and capture it for future versions.
		try
		{
			LOG_INFO << "GFX Info: Initialising FrameBufferObject.";

			// Generate our buffers and texture
			glGenFramebuffers(1, &m_frameBuffer );
			glGenRenderbuffers(1, &m_depthRenderBuffer );
			glGenTextures(1, &m_texID);

			// --
			// Bind frame buffer and texture
			glBindFramebuffer(GL_FRAMEBUFFER_EXT, m_frameBuffer);
			glBindTexture(m_texFormat, m_texID);

			// This is our dynamic texture, which will be loaded with new pixel data
			// after we're finshed rendering to the p-buffer.
			glTexImage2D(m_texFormat, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
			glTexParameteri(m_texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameteri(m_texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR );

			// And attach it to the FBO so we can render to it
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, m_texFormat, m_texID, 0);

			// attach a depth buffer to the Frame buffer
			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, m_depthRenderBuffer );
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, nWidth, nHeight);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, m_depthRenderBuffer);

			CHECK_FRAMEBUFFER_STATUS();

			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now
		}
		catch (...)
		{
			LOG_ERROR << "FrameBuffer Error:" << __FILE__ << ": line #" << __LINE__ << ":" << __FUNCTION__;
		}

	}
	else
	{ // Pixel Buffer Mode
		LOG_INFO << "Attempting to create a PixelBuffer.";

		//-------------------------------------------------------------------------
		// Create a p-buffer for off-screen rendering.
		//-------------------------------------------------------------------------
		
		// Define the minimum pixel format requirements we will need for our 
		// p-buffer. A p-buffer is just like a frame buffer, it can have a depth 
		// buffer associated with it and it can be double buffered.
		int pf_attr[] =
		{
			WGL_SUPPORT_OPENGL_ARB, TRUE,       // P-buffer will be used with OpenGL
			WGL_DRAW_TO_PBUFFER_ARB, TRUE,      // Enable render to p-buffer
			WGL_BIND_TO_TEXTURE_RGBA_ARB, TRUE, // P-buffer will be used as a texture
			WGL_RED_BITS_ARB, 8,                // At least 8 bits for RED channel
			WGL_GREEN_BITS_ARB, 8,              // At least 8 bits for GREEN channel
			WGL_BLUE_BITS_ARB, 8,               // At least 8 bits for BLUE channel
			WGL_ALPHA_BITS_ARB, 8,              // At least 8 bits for ALPHA channel
			WGL_DEPTH_BITS_ARB, 16,             // At least 16 bits for depth buffer
			WGL_DOUBLE_BUFFER_ARB, FALSE,       // We don't require double buffering
			0                                   // Zero terminates the list
		};

		unsigned int iCount = 0;
		int iPixelFormat = 0;
		// g_hdc = wxglcanvas hdc

		wglChoosePixelFormatARB(canvas_hDC, (const int*)pf_attr, NULL, 1, &iPixelFormat, &iCount);

		if (iCount == 0)
		{
			LOG_ERROR << "wglChoosePixelFormatARB() Failed! PixelBuffer could not find an acceptable pixel format!" << glGetError();
			return;
		}

		// Set some p-buffer attributes so that we can use this p-buffer as a
		// 2D RGBA texture target.
		int pb_attr[] =
		{
			WGL_TEXTURE_FORMAT_ARB, WGL_TEXTURE_RGBA_ARB, // Our p-buffer will have a texture format of RGBA
			WGL_TEXTURE_TARGET_ARB, WGL_TEXTURE_2D_ARB,   // Of texture target will be GL_TEXTURE_2D
			0                                             // Zero terminates the list
		};

		// Create the p-buffer...
		m_hPBuffer = wglCreatePbufferARB(canvas_hDC, iPixelFormat, nWidth, nHeight, pb_attr );

		// Error check
		err = glGetError();
		if (!m_hPBuffer)
		{
			LOG_ERROR << "Could not create the PixelBuffer." << err;
			return;
		}
		else if (err==GL_NO_ERROR)
		{
			LOG_INFO << "Successfully created the PixelBuffer.";
		}
		else if (err==GL_INVALID_ENUM)
		{
			LOG_ERROR << "Invalid Enum during PixelBuffer creation.";
		}
		else if (err==GL_INVALID_VALUE)
		{
			LOG_ERROR << "Invalid Value during PixelBuffer creation.";
		}
		else if (err==GL_INVALID_OPERATION)
		{
			LOG_ERROR << "Invalid Operation during PixelBuffer creation.";
		}
		else if (err==GL_OUT_OF_MEMORY)
		{
			LOG_ERROR << "Critical error!  Out-of-Memory during PixelBuffer creation. PixelBuffer could not be created.";
			return;
		}
		else
		{
			LOG_ERROR << "PixelBuffer created, but an unknown error occured :" << err;
		}

		m_hDC = wglGetPbufferDCARB( m_hPBuffer );
		m_hRC = wglCreateContext( m_hDC );

		int h=0, w=0;
		wglQueryPbufferARB( m_hPBuffer, WGL_PBUFFER_HEIGHT_ARB, &h );
		wglQueryPbufferARB( m_hPBuffer, WGL_PBUFFER_WIDTH_ARB, &w );

		if (h!=nHeight || w!=nWidth)
		{
			LOG_ERROR << "The width and height of the created PixelBuffer don't match the requirements. Image likely to come out distorted.";
			nHeight = h;
			nWidth = w;
		}

		if (!wglShareLists(canvas_hRC, m_hRC))
		{
			err = glGetError();
			LOG_ERROR << "Call to wglShareLists() failed for our PixelBuffer.";
		}

		// We were successful in creating a p-buffer. We can now make its context 
		// current and set it up just like we would a regular context 
		// attached to a window.
		if (!wglMakeCurrent(m_hDC, m_hRC))
		{
			err = glGetError();
			LOG_ERROR << "wglMakeCurrent() Failed! Could not make the PBuffer's context current!" << err;
		}

		// Setup OpenGL RenderState
		InitGL();

		// This is our dynamic texture, which will be loaded with new pixel data
		// after we're finshed rendering to the p-buffer.
		glGenTextures(1, &m_texID);
		glBindTexture(m_texFormat, m_texID);
		glTexImage2D(m_texFormat, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL ); //GL_FLOAT
		glTexParameteri(m_texFormat, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri(m_texFormat, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		//glTexParameteri(texFormat, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		//glTexParameteri(texFormat, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

		// Now set the current context back to original.
		if (!wglMakeCurrent(canvas_hDC, canvas_hRC))
		{
			err = glGetError();
			LOG_ERROR << "wglMakeCurrent() Failed! Could not return the context back to the wxGLCanvas!" << err;
		}
	}
}

void RenderTexture::BindTexture()
{
	glBindTexture(m_texFormat, m_texID);

	if (!m_FBO) {
		if(!wglBindTexImageARB(m_hPBuffer, WGL_FRONT_LEFT_ARB )) {
			LOG_ERROR << "Could not bind PixelBuffer to render texture!";
		}
	}
}


void RenderTexture::ReleaseTexture()
{
	if (!m_FBO) { // If its a pixelbuffer, release the texture
		if(!wglReleaseTexImageARB(m_hPBuffer, WGL_FRONT_LEFT_ARB)) {
			LOG_ERROR << "Could not release Texture from the PixelBuffer!";
		}
	}

	glBindTexture(m_texFormat, 0);
}

void RenderTexture::BeginRender()
{
	if (m_FBO) {
		glBindTexture(m_texFormat, 0);
		// Bind the frame-buffer object
		glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_frameBuffer );
		//glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, m_texFormat, m_texID, 0);

	} else { // Pixel Buffer
		int flag = 0;
		wglQueryPbufferARB(m_hPBuffer, WGL_PBUFFER_LOST_ARB, &flag );

		if (flag) {
			LOG_ERROR << "The PixelBuffer was lost!";
			return;
		}

		if (!wglMakeCurrent(m_hDC, m_hRC))
			LOG_ERROR << "Could not make the PixelBuffer's context current.";
	}
}

void RenderTexture::EndRender()
{
	if (m_FBO) {
		// Unbind the frame-buffer and render-buffer objects.
		//glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, m_texFormat, 0, 0);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);


	} else { //PBuffer
		if (!wglMakeCurrent(canvas_hDC, canvas_hRC))
			LOG_ERROR << "Could not return the context back to the primary window.";
	}
}

void RenderTexture::Shutdown()
{
	if (m_texID) {
		glDeleteTextures( 1, &m_texID );
		m_texID = 0;
	}

	if (m_FBO) {
		glDeleteFramebuffersEXT(1, &m_frameBuffer);
		glDeleteRenderbuffersEXT(1, &m_depthRenderBuffer);
	} else {
		wglMakeCurrent(m_hDC, m_hRC);

		// Don't forget to clean up after our pixelbuffer...
		
		wglReleasePbufferDCARB(m_hPBuffer, m_hDC);
		wglDestroyPbufferARB(m_hPBuffer);

		if(m_hRC) {
			wglDeleteContext(m_hRC);
			m_hRC = NULL;
		}

		wglMakeCurrent(canvas_hDC, canvas_hRC);
	}
}
