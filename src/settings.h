//--
#ifndef SETTINGS_H
#define SETTINGS_H

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/notebook.h>

// Custom headers
#include "util.h"
#include "video.h"
#include "modelcanvas.h"

enum
{
  // Settings
  ID_SETTINGS_FRAME,
  ID_SETTINGS_PAGE1,
  ID_SETTINGS_PAGE2,
  ID_SETTINGS_PAGE3,
  ID_SETTINGS_TABS,
  // Settings Page1
  ID_SETTINGS_RANDOMSKIN,
  ID_SETTINGS_SHOWPARTICLE,
  ID_SETTINGS_ZEROPARTICLE,
  ID_SETTINGS_PAGE1_APPLY,
  ID_SETTINGS_PAGE2_APPLY,
  ID_SETTINGS_INIT_POSE_ONLY_EXPORT,
  ID_SETTINGS_PAGE3_APPLY
};

enum {
  CHECK_RANDOMSKIN,
  CHECK_SHOWPARTICLE,
  CHECK_ZEROPARTICLE,

  NUM_SETTINGS1_CHECK
};

enum {
  CHECK_COMPRESSEDTEX,
  CHECK_MULTITEX,
  CHECK_VBO,
  CHECK_FBO,
  CHECK_PBO,
  CHECK_DRAWRANGEELEMENTS,
  CHECK_ENVMAPPING,
  CHECK_NPOT,
  CHECK_PIXELSHADERS,
  CHECK_VERTEXSHADERS,
  CHECK_GLSLSHADERS,

  NUM_SETTINGS2_CHECK
};

enum {
  CHECK_INIT_POSE_ONLY_EXPORT,
  NUM_SETTINGS3_CHECK
};


class Settings_Page1: public wxWindow
{
	DECLARE_CLASS(Settings_Page1)
    DECLARE_EVENT_TABLE()

	wxCheckBox *chkbox[NUM_SETTINGS1_CHECK];
	wxTextCtrl *gamePathCtrl;

public:

	Settings_Page1(wxWindow* parent, wxWindowID id);
	~Settings_Page1(){};

	void Update();

	void OnButton(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);
};

class Settings_Page2: public wxWindow
{
	DECLARE_CLASS(Settings_Page2)
    DECLARE_EVENT_TABLE()

	wxComboBox *oglMode;
	wxCheckBox *chkBox[NUM_SETTINGS2_CHECK];
	wxTextCtrl *txtFov;

public:

	Settings_Page2(wxWindow* parent, wxWindowID id);
	~Settings_Page2() {};

	void Update();
	void OnButton(wxCommandEvent &event);
};

class Settings_Page3: public wxWindow
{
  DECLARE_CLASS(Settings_Page3)
    DECLARE_EVENT_TABLE()

  wxCheckBox *chkbox[NUM_SETTINGS3_CHECK];

public:

  Settings_Page3(wxWindow* parent, wxWindowID id);
  ~Settings_Page3() {};

  void Update();

  void OnButton(wxCommandEvent &event);
  void OnCheck(wxCommandEvent &event);
};


class SettingsControl: public wxWindow
{
	DECLARE_CLASS(SettingsControl)
    DECLARE_EVENT_TABLE()

	wxNotebook *notebook;
	Settings_Page1 *page1;
	Settings_Page2 *page2;
	Settings_Page3 *page3;
public:

	SettingsControl(wxWindow* parent, wxWindowID id);
	~SettingsControl();
	
	void Open();
	void Close();
};

#endif
// --
