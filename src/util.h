#ifndef UTIL_H
#define UTIL_H

// STL headers
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#ifdef _WINDOWS
#include <windows.h>
#endif

#include <algorithm>
//#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <set>
#include <sstream>
#include <vector>
#include <cstddef>

// Standard C++ headers
#include <stdio.h>

// wx
#include <wx/bitmap.h>
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/string.h>

// Our other utility headers
#include "vec3d.h"
#include "quaternion.h"



using namespace std;

#if defined(_WINDOWS)
	#define snprintf _snprintf
	typedef unsigned char uint8;
	typedef char int8;
	typedef unsigned __int16 uint16;
	typedef __int16 int16;
	typedef unsigned __int32 uint32;
	typedef __int32 int32;
#else
	#include <stdint.h>
	typedef uint8_t uint8;
	typedef int8_t int8;
	typedef uint16_t uint16;
	typedef int16_t int16;
	typedef uint32_t uint32;
	typedef int32_t int32;
#endif

extern wxString gamePath;
extern wxString cfgPath;
extern wxString bgImagePath;
extern wxString armoryPath;

extern bool useRandomLooks;

class UserSkins;
extern UserSkins& gUserSkins;

extern long langID;
extern wxString langName;
extern long langOffset;
extern long interfaceID;
extern int ssCounter;
extern int imgFormat;
extern long versionID;

extern wxString locales[];

// Slashes for Pathing
#ifdef _WINDOWS
	#define SLASH wxT('\\')
#else
	#define SLASH wxT('/')
#endif
#define	MPQ_SLASH   wxT('\\')

wxString fixMPQPath(wxString path);
float frand();


template <class T>
bool from_string(T& t, const string& s, ios_base& (*f)(ios_base&))
{
  istringstream iss(s);
  return !(iss >> f >> t).fail();
}

wxString CSConv(wxString str);
void fixname(wxString &name);
void fixnamen(char *name, size_t len);
wxString Vec3DToString(Vec3D vec);
int wxStringToInt(const wxString& str);
float round(float input, int limit);
void MakeDirs(wxString PathBase, wxString ExtPaths);
unsigned short _SwapTwoBytes (unsigned short w);

void getGamePath();

// Byte Swapping
#if defined _WINDOWS || defined _MSWIN
	#define MSB2			_SwapTwoBytes
	#define MSB4			_SwapFourBytes
	#define LSB2(w)			(w)
	#define LSB4(w)			(w)
#else
	#define MSB2(w)			(w)
	#define MSB4			static_cast
	#define LSB2			_SwapTwoBytes
	#define LSB4			_SwapFourBytes 
#endif

template <typename T>
inline T _SwapFourBytes (T w)
{
	T a;
	unsigned char *src = (unsigned char*)&w;
	unsigned char *dst = (unsigned char*)&a;

	dst[0] = src[3];
	dst[1] = src[2];
	dst[2] = src[1];
	dst[3] = src[0];

	return a;
}

#if defined _WINDOWS
wxBitmap* createBitmapFromResource(const wxString& t_name, long type = wxBITMAP_TYPE_PNG, int width = 0, int height = 0);
bool loadDataFromResource(char*& t_data, DWORD& t_dataSize, const wxString& t_name);
#endif

wxBitmap* getBitmapFromMemory(const char* t_data, const DWORD t_size, long type, int width, int height);

bool correctType(ssize_t type, ssize_t slot);

#endif

