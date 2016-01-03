#include "util.h"
#ifdef _WINDOWS
#include <windows.h>
#include <wx/msw/winundef.h>
#endif
#include <wx/bitmap.h>
#include <wx/choicdlg.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/mstream.h>

#include "UserSkins.h"

wxString gamePath;
wxString cfgPath;
wxString bgImagePath;
wxString armoryPath;
wxString customDirectoryPath;
int customFilesConflictPolicy = 0;
int displayItemAndNPCId = 0;

UserSkins userSkins;
UserSkins& gUserSkins = userSkins;

bool useRandomLooks = true;

long langID = -1;
wxString langName;
long langOffset = -1;
long interfaceID = 0;
int ssCounter = 100; // ScreenShot Counter
int imgFormat = 0;

wxString locales[] = {wxT("enUS"), wxT("koKR"), wxT("frFR"), wxT("deDE"), wxT("zhCN"), wxT("zhTW"), wxT("esES"), wxT("esMX"), wxT("ruRU")};

// Slash correction, just in case.
wxString fixMPQPath(wxString path)
{
#ifdef	_WINDOWS
    return path;
#else
    wxString str = path;
    str.Replace(wxString(MPQ_SLASH), wxString(SLASH));
    return str;
#endif
}

// Convert UTF8 string to local string
wxString CSConv(wxString str)
{
  return wxConvLocal.cWC2WX(wxConvUTF8.cMB2WC(str.mb_str())); // from private.h
}

wxString CSConv(QString str)
{
  return wxConvLocal.cWC2WX(wxConvUTF8.cMB2WC(str.toStdString().c_str()));
}

void fixname(wxString &name)
{
	for (size_t i=0; i<name.length(); i++) {
		if (i>0 && name[i]>='A' && name[i]<='Z' && isalpha(name[i-1])) {
			name[i] |= 0x20;
		} else if ((i==0 || !isalpha(name[i-1])) && name[i]>='a' && name[i]<='z') {
			name[i] &= ~0x20;
		}
	}
}
void fixnamen(char *name, size_t len)
{
	for (size_t i=0; i<len; i++) {
		if (i>0 && name[i]>='A' && name[i]<='Z' && isalpha(name[i-1])) {
			name[i] |= 0x20;
		} else if ((i==0 || !isalpha(name[i-1])) && name[i]>='a' && name[i]<='z') {
			name[i] &= ~0x20;
		}
	}
}

// Byteswap for 2 Bytes
unsigned short _SwapTwoBytes (unsigned short w)
{
	unsigned short tmp;
	tmp =  (w & 0x00ff);
	tmp = ((w & 0xff00) >> 0x08) | (tmp << 0x08);
	return tmp;
}

// Round a float, down to the specified decimal
float round(float input, int limit = 2){
	if (limit > 0){
		input *= (10^limit);
	}
	input = int(input+0.5);
	if (limit > 0){
		input /= (10^limit);
	}
	return input;
}

void MakeDirs(wxString PathBase, wxString ExtPaths){
	wxString NewBase = PathBase;
	//LOG_INFO << "MKDIR Paths BasePath:" << PathBase << "Others Paths:" << ExtPaths;
	wxString Paths[128];
	size_t PathNum = 0;
	while (ExtPaths.Find(SLASH)>0){
		Paths[PathNum] = ExtPaths.BeforeFirst(SLASH);
		wxString rep = Paths[PathNum]+SLASH;
		ExtPaths.Replace(rep, wxEmptyString, true);
		//LOG_INFO << "Building Paths:" << Paths[PathNum] << "paths:" << ExtPaths;
		PathNum++;
	}
	Paths[PathNum] = ExtPaths;
	PathNum++;

	for (size_t x=0;x<PathNum;x++){
		NewBase = wxString(NewBase << SLASH << Paths[x]);
		if (wxDirExists(NewBase) == false){
			//LOG_INFO << "Attempting to create the following directory:" << NewBase;
			wxMkdir(NewBase);
		}
	}
}

void getGamePath()
{
#ifdef _WINDOWS
	HKEY key;
	unsigned long t, s;
	long l;
	unsigned char path[1024];
	memset(path, 0, sizeof(path));

	wxArrayString sNames;
	gamePath = wxEmptyString;

	// if it failed, look for World of Warcraft install
	const wxString regpaths[] = {
		// _WIN64
		wxT("SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft"),
		wxT("SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\PTR"),
		wxT("SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\World of Warcraft\\Beta"),
		//_WIN32, but for compatible
		wxT("SOFTWARE\\Blizzard Entertainment\\World of Warcraft"),
		wxT("SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\PTR"),
		wxT("SOFTWARE\\Blizzard Entertainment\\World of Warcraft\\Beta"),
	};

	for (size_t i=0; i<WXSIZEOF(regpaths); i++) {
		l = RegOpenKeyEx((HKEY)HKEY_LOCAL_MACHINE, regpaths[i], 0, KEY_QUERY_VALUE, &key);

		if (l == ERROR_SUCCESS) {
			s = sizeof(path);
			l = RegQueryValueEx(key, wxT("InstallPath"), 0, &t,(LPBYTE)path, &s);
			wxString spath(path);
			if (l == ERROR_SUCCESS && wxDir::Exists(path) && sNames.Index(spath) == wxNOT_FOUND) {
				sNames.Add(spath);
			}
			RegCloseKey(key);
		}
	}

	if (sNames.size() == 1)
		gamePath = sNames[0];
	else if (sNames.size() > 1)
		gamePath = wxGetSingleChoice(wxT("Please select a Path:"), wxT("Path"), sNames);

	// If we found an install then set the game path, otherwise just set to C:\ for now
	if (gamePath == wxEmptyString) {
		gamePath = wxT("C:\\Program Files\\World of Warcraft\\");
		if (!wxFileExists(gamePath+wxT("Wow.exe"))){
			gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
		}
	}
	if (!gamePath.IsEmpty() && gamePath.Last() != SLASH)
		gamePath.Append(SLASH);
	gamePath.Append(wxT("Data\\"));
#elif _MAC // Mac OS X
    gamePath = wxT("/Applications/World of Warcraft/");
	if (!wxFileExists(gamePath+wxT("Data/common.MPQ")) && !wxFileExists(gamePath+wxT("Data/art.MPQ")) ){
        gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
    }
	if (!gamePath.IsEmpty() && gamePath.Last() != SLASH)
		gamePath.Append(SLASH);
	gamePath.Append(wxT("Data/"));
#else // Linux
	gamePath = wxT(".")+SLASH;
	if (!wxFileExists(gamePath+wxT("Wow.exe"))){
		gamePath = wxDirSelector(wxT("Please select your World of Warcraft folder:"), gamePath);
	}
	if (!gamePath.IsEmpty() && gamePath.Last() != SLASH)
		gamePath.Append(SLASH);
	gamePath.Append(wxT("Data/"));
#endif
}

#ifdef _WINDOWS
wxBitmap* createBitmapFromResource(const wxString& t_name,long type /* = wxBITMAP_TYPE_PNG */, int width /* = 0 */, int height /* = 0 */)
{
  wxBitmap*   r_bitmapPtr = 0;
  
  char*       a_data      = 0;
  DWORD       a_dataSize  = 0;
  
  if(loadDataFromResource(a_data, a_dataSize, t_name))
  {
    r_bitmapPtr = getBitmapFromMemory(a_data, a_dataSize,type,width,height);
  }
  
  return r_bitmapPtr;
}


bool loadDataFromResource(char*& t_data, DWORD& t_dataSize, const wxString& t_name)
{
  bool     r_result    = false;
  HGLOBAL  a_resHandle = 0;
  HRSRC    a_resource;
  
  a_resource = FindResource(0, t_name.mb_str(), RT_RCDATA);
  
  if(0 != a_resource)
  {
	a_resHandle = LoadResource(NULL, a_resource);
	if (0 != a_resHandle)
    {
	  t_data = (char*)LockResource(a_resHandle);
      t_dataSize = SizeofResource(NULL, a_resource);
      r_result = true;
    }
  }
  
  return r_result;
}
#endif


wxBitmap* getBitmapFromMemory(const char* t_data, const DWORD t_size, long type, int width, int height)
{
  wxMemoryInputStream a_is(t_data, t_size);
  
  wxImage newImage(wxImage(a_is, type, -1));
  
  if((width != 0) && (height != 0))
	newImage.Rescale(width,height);
  return new wxBitmap(newImage, -1);
}

bool correctType(ssize_t type, ssize_t slot)
{
	if (type == IT_ALL)
		return true;

	switch (slot) {
	case CS_HEAD:		return (type == IT_HEAD);
	case CS_SHOULDER:	return (type == IT_SHOULDER);
	case CS_SHIRT:		return (type == IT_SHIRT);
	case CS_CHEST:		return (type == IT_CHEST || type == IT_ROBE);
	case CS_BELT:		return (type == IT_BELT);
	case CS_PANTS:		return (type == IT_PANTS);
	case CS_BOOTS:		return (type == IT_BOOTS);
	case CS_BRACERS:	return (type == IT_BRACERS);
	case CS_GLOVES:		return (type == IT_GLOVES);

	// Slight correction.  Type 21 = Lefthand weapon, Type 22 = Righthand weapon
	//case CS_HAND_RIGHT:	return (type == IT_1HANDED || type == IT_GUN || type == IT_THROWN || type == IT_2HANDED || type == IT_CLAW || type == IT_DAGGER);
	//case CS_HAND_LEFT:	return (type == IT_1HANDED || type == IT_BOW || type == IT_SHIELD || type == IT_2HANDED || type == IT_CLAW || type == IT_DAGGER || type == IT_OFFHAND);
	case CS_HAND_RIGHT:	return (type == IT_RIGHTHANDED || type == IT_GUN || type == IT_THROWN || type == IT_2HANDED || type == IT_DAGGER);
	case CS_HAND_LEFT:	return (type == IT_LEFTHANDED || type == IT_BOW || type == IT_SHIELD || type == IT_2HANDED || type == IT_DAGGER || type == IT_OFFHAND);
	case CS_CAPE:		return (type == IT_CAPE);
	case CS_TABARD:		return (type == IT_TABARD);
	case CS_QUIVER:		return (type == IT_QUIVER);
	}
	return false;
}
