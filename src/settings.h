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

class Settings_Page1: public wxWindow
{
	DECLARE_CLASS(Settings_Page1)
    DECLARE_EVENT_TABLE()

	wxCheckBox *chkbox[NUM_SETTINGS1_CHECK];
	wxTextCtrl *txtPath;
	wxStaticText *lbl2;
	wxButton *btn1, *btn2, *btn3, *btn4, *btn5;

public:
	wxListBox *mpqList;

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


class SettingsControl: public wxWindow
{
	DECLARE_CLASS(SettingsControl)
    DECLARE_EVENT_TABLE()

	wxNotebook *notebook;
	Settings_Page1 *page1;
	Settings_Page2 *page2;
public:

	SettingsControl(wxWindow* parent, wxWindowID id);
	~SettingsControl();
	
	void Open();
	void Close();
	void ResetMPQ();
};

#endif
// --
