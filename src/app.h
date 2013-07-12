#ifndef APP_H
#define APP_H

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#ifndef _MINGW

#if _MSC_VER>=1400
	// This gives us Win XP style common controls in MSVC 8.0.
	#if defined _M_X64
		#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#elif defined _M_IA64
		#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#elif defined _M_IX86
		#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#else
		#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#endif
#endif

// Link to our Windows libs.
#ifdef _WINDOWS
	#pragma message("     Adding library: opengl32.lib" ) 
	#pragma comment( lib, "opengl32.lib" )	// OpenGL API
	#pragma message("     Adding library: glu32.lib" ) 
	#pragma comment( lib, "glu32.lib" ) // OpenGL Utilities
	#define GLEW_BUILD

	// Build-specific Libs
	#ifdef _DEBUG
		#if _MSC_VER==1600		// If VC100 (VS2010)
			#ifdef _WIN64
				#pragma message("     Adding library: glew64d_VC100.lib" ) 
				#pragma comment( lib, "glew64d_VC100.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64d_VC100.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64d_VC100.lib" )
			#else
				#pragma message("     Adding library: glew32d_VC100.lib" ) 
				#pragma comment( lib, "glew32d_VC100.lib" )
				#pragma message("     Adding library: fbxsdk_20113d_VC100.lib" ) 
				#pragma comment( lib, "fbxsdk_20113d_VC100.lib" )
			#endif
		#elif _MSC_VER==1500	// If VC90 (VS2008)
			#ifdef _WIN64
				#pragma message("     Adding library: glew64d_VC90.lib" ) 
				#pragma comment( lib, "glew64d_VC90.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64d_VC90.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64d_VC90.lib" )
			#else
				#pragma message("     Adding library: glew32d_VC90.lib" ) 
				#pragma comment( lib, "glew32d_VC90.lib" )
				#pragma message("     Adding library: fbxsdk_20113d_VC90.lib" ) 
				#pragma comment( lib, "fbxsdk_20113d_VC90.lib" )
			#endif
		#else					// Otherwise
			#ifdef _WIN64
				#pragma message("     Adding library: glew64d.lib" ) 
				#pragma comment( lib, "glew64d.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64d.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64d.lib" )
			#else
				#pragma message("     Adding library: glew32d.lib" ) 
				#pragma comment( lib, "glew32d.lib" )
				#pragma message("     Adding library: fbxsdk_20113d.lib" ) 
				#pragma comment( lib, "fbxsdk_20113d.lib" )
			#endif
		#endif
	#else	// If Release
		#if _MSC_VER==1600		// If VC100 (VS2010)
			#ifdef _WIN64
				#pragma message("     Adding library: glew64_VC100.lib" ) 
				#pragma comment( lib, "glew64_VC100.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64_VC100.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64_VC100.lib" )
			#else
				#pragma message("     Adding library: glew32_VC100.lib" ) 
				#pragma comment( lib, "glew32_VC100.lib" )
				#pragma message("     Adding library: fbxsdk_20113_VC100.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_VC100.lib" )
			#endif
		#elif _MSC_VER==1500	// If VC90 (VS2008)
			#ifdef _WIN64
				#pragma message("     Adding library: glew64_VC90.lib" ) 
				#pragma comment( lib, "glew64_VC90.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64_VC90.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64_VC90.lib" )
			#else
				#pragma message("     Adding library: glew32_VC90.lib" ) 
				#pragma comment( lib, "glew32_VC90.lib" )
				#pragma message("     Adding library: fbxsdk_20113_VC90.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_VC90.lib" )
			#endif
		#else					// Otherwise
			#ifdef _WIN64
				#pragma message("     Adding library: glew64.lib" ) 
				#pragma comment( lib, "glew64.lib" )
				#pragma message("     Adding library: fbxsdk_20113_amd64.lib" ) 
				#pragma comment( lib, "fbxsdk_20113_amd64.lib" )
			#else
				#pragma message("     Adding library: glew32.lib" ) 
				#pragma comment( lib, "glew32.lib" )
				#pragma message("     Adding library: fbxsdk_20113.lib" ) 
				#pragma comment( lib, "fbxsdk_20113.lib" )
			#endif
		#endif
	#endif	

	#pragma message("     Adding library: uxtheme.lib" ) 
	#pragma comment( lib, "uxtheme.lib" ) // WinXP Theme Engine
	#pragma message("     Adding library: comctl32.lib" ) 
	#pragma comment( lib, "comctl32.lib" ) // Common Controls 32bit
	#pragma message("     Adding library: rpcrt4.lib" ) 
	#pragma comment( lib, "rpcrt4.lib" )
#endif

// More Libs
#ifdef _DEBUG
	#ifdef _WINDOWS
		#pragma comment( lib, "wxmsw28d_core.lib" )	// wxCore Debug Lib
		#pragma comment( lib, "wxmsw28d_adv.lib" )
		#pragma comment( lib, "wxmsw28d_qa.lib" )
		#pragma comment( lib, "wxmsw28d_aui.lib" )
		
		// Winsock 2
		#pragma comment( lib, "ws2_32.lib ") // This lib is required by wxbase28_net lib
	#elif _MAC
		#pragma comment( lib, "wxmac28d_core.lib" )
		#pragma comment( lib, "wxmac28d_adv.lib" )
		#pragma comment( lib, "wxmac28d_gl.lib" )
		#pragma comment( lib, "wxmac28d_qa.lib" )
		#pragma comment( lib, "wxmac28d_aui.lib" )
	#endif

	#pragma comment( lib, "wxzlibd.lib" )
	#pragma comment( lib, "wxregexd.lib" )
	#pragma comment( lib, "wxbase28d.lib" )
	#pragma comment( lib, "wxbase28d_net.lib" )
	#pragma comment( lib, "wxexpatd.lib" )
	#pragma comment( lib, "wxbase28d_xml.lib" )

	// cxImage
	#ifdef _WINDOWS
		#if _MSC_VER==1600		// If VC100 (VS2010)
			#ifdef _WIN64
				#pragma message("     Adding library: cximagecrt64d_VC100.lib" ) 
				#pragma comment( lib, "cximagecrt64d_VC100.lib" )
			#else
				#pragma message("     Adding library: cximagecrt32d_VC100.lib" ) 
				#pragma comment( lib, "cximagecrt32d_VC100.lib" )
			#endif
		#elif _MSC_VER==1500	// If VC90 (VS2008)
			#ifdef _WIN64
				#pragma message("     Adding library: cximagecrt64d_VC90.lib" ) 
				#pragma comment( lib, "cximagecrt64d_VC90.lib" )
			#else
				#pragma message("     Adding library: cximagecrt32d_VC90.lib" ) 
				#pragma comment( lib, "cximagecrt32d_VC90.lib" )
			#endif
		#else					// Otherwise
			#pragma message("     Adding library: cximagecrtd.lib" ) 
			#pragma comment( lib, "cximagecrtd.lib" )
		#endif
	#else
		#pragma message("     Adding library: cximagecrtd.lib" ) 
		#pragma comment( lib, "cximagecrtd.lib" )
	#endif
#else	// Release
	#define NDEBUG			// Disables Asserts in release
	#define VC_EXTRALEAN	// Exclude rarely-used stuff from Windows headers
	#define WIN32_LEAN_AND_MEAN

	#ifdef _WINDOWS
		#pragma comment( lib, "wxmsw28_core.lib" )
		#pragma comment( lib, "wxmsw28_adv.lib" )
		#pragma comment( lib, "wxmsw28_qa.lib" )
		#pragma comment( lib, "wxmsw28_aui.lib" )
	
		#pragma comment( lib, "ws2_32.lib ") // This lib is required by wxbase28_net lib
	#elif _MAC
		#pragma comment( lib, "wxmac28_core.lib" )
		#pragma comment( lib, "wxmac28_adv.lib" )
		#pragma comment( lib, "wxmac28_gl.lib" )
		#pragma comment( lib, "wxmac28_qa.lib" )
		#pragma comment( lib, "wxmac28_aui.lib" )
	#endif

	#pragma comment( lib, "wxzlib.lib" )
	#pragma comment( lib, "wxregex.lib" )
	#pragma comment( lib, "wxbase28.lib" )
	#pragma comment( lib, "wxbase28_net.lib" )
	#pragma comment( lib, "wxexpat.lib" )
	#pragma comment( lib, "wxbase28_xml.lib" )

	// cxImage
	#if defined(_WINDOWS)
		#if _MSC_VER==1600		// If VC100 (VS2010)
			#ifdef _WIN64
				#pragma message("     Adding library: cximagecrt64_VC100.lib" ) 
				#pragma comment( lib, "cximagecrt64_VC100.lib" )
			#else
				#pragma message("     Adding library: cximagecrt32_VC100.lib" ) 
				#pragma comment( lib, "cximagecrt32_VC100.lib" )
			#endif
		#elif _MSC_VER==1500	// If VC90 (VS2008)
			#ifdef _WIN64
				#pragma message("     Adding library: cximagecrt64_VC90.lib" ) 
				#pragma comment( lib, "cximagecrt64_VC90.lib" )
			#else
				#pragma message("     Adding library: cximagecrt32_VC90.lib" ) 
				#pragma comment( lib, "cximagecrt32_VC90.lib" )
			#endif
		#else					// Otherwise
			#pragma message("     Adding library: cximagecrt.lib" ) 
			#pragma comment( lib, "cximagecrt.lib" )
		#endif
	#else
		#pragma message("     Adding library: cximagecrt.lib" ) 
		#pragma comment( lib, "cximagecrt.lib" )
	#endif
#endif // _DEBUG

#endif // _MINGW


#ifndef _WINDOWS
	#include "../bin_support/Icons/wmv.xpm"
#endif

// headers
#include <wx/app.h>
//#include <wx/debugrpt.h>
#include <wx/log.h>
#include <wx/tokenzr.h>
#include <wx/dir.h>
#include <wx/aui/aui.h>

#include "util.h"
#include "globalvars.h"
#include "modelviewer.h"

// vars
static wxString langNames[] =
{
	wxT("English"),
	wxT("Korean"),
	wxT("French"),
	wxT("German"),
	wxT("Simplified Chinese"),
	wxT("Traditional Chinese"),
	wxT("Spanish (EU)"),
	wxT("Spanish (Latin American)"),
	wxT("Russian"),
};

static const wxLanguage langIds[] =
{
	wxLANGUAGE_ENGLISH,
	wxLANGUAGE_KOREAN,
	wxLANGUAGE_FRENCH,
	wxLANGUAGE_GERMAN,
	wxLANGUAGE_CHINESE_SIMPLIFIED,
	wxLANGUAGE_CHINESE_TRADITIONAL,
	wxLANGUAGE_SPANISH,
	wxLANGUAGE_SPANISH,
	wxLANGUAGE_RUSSIAN,
};


 

class WowModelViewApp : public wxApp
{
public:
    virtual bool OnInit();
	virtual int OnExit();
	virtual void OnUnhandledException();
	virtual void OnFatalException();
	void setInterfaceLocale();

	//virtual bool OnExceptionInMainLoop();
	//virtual void HandleEvent(wxEvtHandler *handler, wxEventFunction func, wxEvent& event) const ; 

	bool LoadSettings();
	void SaveSettings();

	ModelViewer *frame;
	
	wxLocale locale;
	FILE *LogFile;
	
};

void searchMPQs(bool firstTime);

#endif

