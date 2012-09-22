#ifndef UTIL_H
#define UTIL_H

// STL headers
#ifdef min
#undef min
#endif

#ifdef max
#undef max
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
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/string.h>
#include <wx/log.h>

// Our other utility headers
#include "vec3d.h"

using namespace std;

#if defined(_WINDOWS) && !defined(_MINGW)
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

extern int gameVersion;

extern bool useLocalFiles;
extern bool useRandomLooks;
extern bool bHideHelmet;
extern bool bShowParticle;
extern bool bZeroParticle;
extern bool bAlternate;

extern int Perfered_Exporter;
extern bool modelExportInitOnly;
extern bool modelExport_PreserveDir;
extern bool modelExport_UseWMVPosRot;
extern bool modelExport_ScaleToRealWorld;
extern bool modelExport_LW_PreserveDir;
extern bool modelExport_LW_AlwaysWriteSceneFile;
extern bool modelExport_LW_ExportLights;
extern bool modelExport_LW_ExportDoodads;
extern bool modelExport_LW_ExportCameras;
extern bool modelExport_LW_ExportBones;
extern int modelExport_LW_DoodadsAs;
extern bool modelExport_X3D_ExportAnimation;
extern bool modelExport_X3D_CenterModel;
extern float modelExport_M3_BoundScale;
extern float modelExport_M3_SphereScale;
extern wxString modelExport_M3_TexturePath;
extern std::vector<uint32> modelExport_M3_Anims;
extern wxArrayString modelExport_M3_AnimNames;

extern wxArrayString mpqArchives;

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
float randfloat(float lower, float upper);
int randint(int lower, int upper);

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

#endif

