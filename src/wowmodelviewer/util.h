#ifndef UTIL_H
#define UTIL_H

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

// Our other utility headers
#include "vec3d.h"
#include "quaternion.h"

#include <QString>

#include <wx/bitmap.h>
#include <wx/string.h>

using namespace std;

extern wxString gamePath;
extern wxString cfgPath;
extern wxString bgImagePath;
extern wxString armoryPath;
extern wxString customDirectoryPath;

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
wxString CSConv(QString str);
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

