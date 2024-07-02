#pragma once

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

class wxNotebook;
class DisplaySettings;
class ExportSettings;
class GeneralSettings;

enum
{
	// Settings
	ID_SETTINGS_FRAME,
	ID_GENERAL_SETTINGS,
	ID_DISPLAY_SETTINGS,
	ID_EXPORT_SETTINGS,
	ID_SETTINGS_TABS
};

class SettingsControl : public wxWindow
{
	DECLARE_CLASS(SettingsControl)
	DECLARE_EVENT_TABLE()

	wxNotebook* notebook;
	GeneralSettings* page1;
	DisplaySettings* page2;
	ExportSettings* page3;

public:
	SettingsControl(wxWindow* parent, wxWindowID id);
	~SettingsControl();

	void Open();
	void Close();
};
