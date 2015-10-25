
#include "SettingsControl.h"

#include <wx/notebook.h>

#include "logger/Logger.h"
#include "DisplaySettings.h"
#include "ExportSettings.h"
#include "GeneralSettings.h"

IMPLEMENT_CLASS(SettingsControl, wxWindow)

BEGIN_EVENT_TABLE(SettingsControl, wxWindow)
	
END_EVENT_TABLE()

SettingsControl::SettingsControl(wxWindow* parent, wxWindowID id)
{
	LOG_INFO << "Creating Settings Control...";
	
	if (Create(parent, id, wxDefaultPosition, wxSize(405,440), wxDEFAULT_FRAME_STYLE, wxT("SettingsControlFrame")) == false) {
		LOG_ERROR << "Failed to create the window for our SettingsControl!";
		return;
	}

	//
	notebook = new wxNotebook(this, ID_SETTINGS_TABS, wxPoint(0,0), wxSize(400,420), wxNB_TOP|wxNB_FIXEDWIDTH|wxNB_NOPAGETHEME);
	
	page1 = new GeneralSettings(notebook, ID_GENERAL_SETTINGS);
	page2 = new DisplaySettings(notebook, ID_DISPLAY_SETTINGS);
	page3 = new ExportSettings(notebook, ID_EXPORT_SETTINGS);

	notebook->AddPage(page1, _("General"), false, -1);
	notebook->AddPage(page2, _("Display"), false);
	notebook->AddPage(page3, _("Export"), false);
}




SettingsControl::~SettingsControl()
{
}


void SettingsControl::Open()
{
	Show(true);

	page1->Update();
	page2->Update();
	page3->Update();
}

void SettingsControl::Close()
{
	
}

// --
