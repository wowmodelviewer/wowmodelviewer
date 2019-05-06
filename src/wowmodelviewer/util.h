
#ifndef UTIL_H
#define UTIL_H

#ifdef _WINDOWS
#include <windows.h>
#endif

//#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

// Standard C++ headers

// Our other utility headers

#include <QString>

#include <wx/bitmap.h>
#include <wx/string.h>

extern wxString gamePath;
extern wxString cfgPath;
extern wxString bgImagePath;
extern wxString armoryPath;
extern wxString customDirectoryPath;
extern int customFilesConflictPolicy;
extern int displayItemAndNPCId;

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
	#define SLASH '\\'
#else
	#define SLASH '/'
#endif

float frand();

template <class T>
bool from_string(T& t, const std::string& s, std::ios_base& (*f)(std::ios_base&))
{
	std::istringstream iss(s);
	return !(iss >> f >> t).fail();
}

float round(float input, int limit);

double getCurrentTime();

wxString getGamePath(bool noSet = false);


#if defined _WINDOWS
wxBitmap* createBitmapFromResource(const wxString& t_name, wxBitmapType type = wxBITMAP_TYPE_PNG, int width = 0, int height = 0);
bool loadDataFromResource(char*& t_data, DWORD& t_dataSize, const wxString& t_name);
#endif

wxBitmap* getBitmapFromMemory(const char* t_data, const DWORD t_size, wxBitmapType type, int width, int height);

bool correctType(ssize_t type, ssize_t slot);

#endif

