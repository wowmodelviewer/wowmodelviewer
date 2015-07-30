#include "shaders.h"
#include <string>

#include "logger/Logger.h"

bool supportShaders = false;
static bool initedShaders = false;

ShaderPair *terrainShaders[4]={0,0,0,0}, *wmoShader=0, *waterShaders[1]={0};

// TODO
bool isExtensionSupported(wxString s)
{
	bool ret = false;
	ret = glewIsSupported((char *)s.c_str()) == GL_TRUE ? true : false;

	return ret;
}

void OldinitShaders()
{
	if (initedShaders)
		return;
	supportShaders = isExtensionSupported(wxT("ARB_vertex_program")) && isExtensionSupported(wxT("ARB_fragment_program"));
	if (supportShaders) {
		// init extension stuff
#if defined (_WINDOWS)
		//glARB = () wglGetProcAddress("");
		glProgramStringARB = (PFNGLPROGRAMSTRINGARBPROC) wglGetProcAddress("glProgramStringARB");
		glBindProgramARB = (PFNGLBINDPROGRAMARBPROC) wglGetProcAddress("glBindProgramARB");
		glDeleteProgramsARB = (PFNGLDELETEPROGRAMSARBPROC) wglGetProcAddress("glDeleteProgramsARB");
		glGenProgramsARB = (PFNGLGENPROGRAMSARBPROC) wglGetProcAddress("glGenProgramsARB");
		glProgramLocalParameter4fARB = (PFNGLPROGRAMLOCALPARAMETER4FARBPROC) wglGetProcAddress("glProgramLocalParameter4fARB");
#endif
#if defined (_LINUX)
		glProgramStringARB = (PFNGLPROGRAMSTRINGARBPROC) glXGetProcAddress((GLubyte*) "glProgramStringARB");
		glBindProgramARB = (PFNGLBINDPROGRAMARBPROC) glXGetProcAddress((GLubyte*) "glBindProgramARB");
		glDeleteProgramsARB = (PFNGLDELETEPROGRAMSARBPROC) glXGetProcAddress((GLubyte*) "glDeleteProgramsARB");
		glGenProgramsARB = (PFNGLGENPROGRAMSARBPROC) glXGetProcAddress((GLubyte*) "glGenProgramsARB");
		glProgramLocalParameter4fARB = (PFNGLPROGRAMLOCALPARAMETER4FARBPROC) glXGetProcAddress((GLubyte*) "glProgramLocalParameter4fARB");
#endif
#if defined (_MAC)
// Under Mac OS X, if an extension name is listed in the OpenGL extension string at runtime, applications can use the extension entry points without having to obtain entry-point addresses.
#endif
		// init various shaders here
		OldreloadShaders();
	}
	LOG_INFO << "Shaders" << (supportShaders?"enabled":"disabled");
	initedShaders = true;
}

void OldreloadShaders()
{
	for (size_t i=0; i<4; i++) delete terrainShaders[i];
	delete wmoShader;
	delete waterShaders[0];

	terrainShaders[0] = new ShaderPair(0, "shaders/terrain1.fs", true);
	terrainShaders[1] = new ShaderPair(0, "shaders/terrain2.fs", true);
	terrainShaders[2] = new ShaderPair(0, "shaders/terrain3.fs", true);
	terrainShaders[3] = new ShaderPair(0, "shaders/terrain4.fs", true);
	wmoShader = new ShaderPair(0, "shaders/wmospecular.fs", true);
	waterShaders[0] = new ShaderPair(0, "shaders/wateroutdoor.fs", true);
}

Shader::Shader(GLenum target, const char *program, bool fromFile):target(target), id(0)
{
	if (!program || !strlen(program)) {
		ok = true;
		return;
	}

	const char *progtext;
	char *buf =0;
	if (fromFile) {
		FILE *f = fopen(program, "rb");
		if (!f) {
			ok = false;
			return;
		}
		fseek(f, 0, SEEK_END);
		size_t len = ftell(f);
		fseek(f, 0, SEEK_SET);

		buf = new char[len+1];
		progtext = buf;
		fread(buf, len, 1, f);
		buf[len]=0;
		fclose(f);
		//gLog("Len: %d\nShader text:\n[%s]\n",len,progtext);
	} else progtext = program;

	glGenProgramsARB(1, &id);
	glBindProgramARB(target, id);
	glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(progtext), progtext);
	if (glGetError() != 0) {
		int errpos;
		glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);
		LOG_ERROR << "Failed to load shader:" << glGetString(GL_PROGRAM_ERROR_STRING_ARB) << "Error position: " << errpos;
		ok = false;
	} else ok = true;

	if (fromFile) delete[] buf;
}

Shader::~Shader()
{
	if (ok && id) glDeleteProgramsARB(1, &id);
}

void Shader::bind()
{
	glBindProgramARB(target, id);
	glEnable(target);
}

void Shader::unbind()
{
	glDisable(target);
}

ShaderPair::ShaderPair(const char *vprog, const char *fprog, bool fromFile)
{
	if (vprog && strlen(vprog)) {
		vertex = new Shader(GL_VERTEX_PROGRAM_ARB, vprog, fromFile);
		if (!vertex->ok) {
			delete vertex;
			vertex = 0;
		}
	} else vertex = 0;
	if (fprog && strlen(fprog)) {
		fragment = new Shader(GL_FRAGMENT_PROGRAM_ARB, fprog, fromFile);
		if (!fragment->ok) {
			delete fragment;
			fragment = 0;
		}
	} else fragment = 0;
}

void ShaderPair::bind()
{
	if (vertex) {
		vertex->bind();
	} else {
		glDisable(GL_VERTEX_PROGRAM_ARB);
	}
	if (fragment) {
		fragment->bind();
	} else {
		glDisable(GL_FRAGMENT_PROGRAM_ARB);
	}
}

void ShaderPair::unbind()
{
	if (vertex) vertex->unbind();
	if (fragment) fragment->unbind();
}
