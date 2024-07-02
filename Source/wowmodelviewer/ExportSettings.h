#pragma once

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

enum
{
	ID_SETTINGS_INIT_POSE_ONLY_EXPORT,
	ID_EXPORT_SETTINGS_APPLY
};

enum
{
	CHECK_INIT_POSE_ONLY_EXPORT,
	NUM_SETTINGS3_CHECK
};

class ExportSettings : public wxWindow
{
	DECLARE_CLASS(ExportSettings)
	DECLARE_EVENT_TABLE()

	wxCheckBox* chkbox[NUM_SETTINGS3_CHECK];

public:
	ExportSettings(wxWindow* parent, wxWindowID id);
	~ExportSettings() = default;

	void Update();

	void OnButton(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);
};
