#ifndef OPENGLHEADERS_H
#define OPENGLHEADERS_H

// glew
#ifdef _WINDOWS
	#include "GL\glew.h"
	#include "GL\wglew.h"
#elif _MAC
	#include <GL/glew.h>
#else // _LINUX
	#include <GL/glew.h>
	#include <GL/glxew.h>
#endif

#ifdef _WINDOWS
#include <windows.h>
#endif

// opengl
#ifdef _MAC
	#include <OpenGL/gl.h>
	#include <OpenGL/glu.h>
#else // _WINDOWS _LINUX
	#include <GL/gl.h>
	#include <GL/glu.h>
#endif


#define GL_BUFFER_OFFSET(i) ((char *)(0) + (i))

#endif

