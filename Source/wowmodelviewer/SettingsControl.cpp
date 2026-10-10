
#include "SettingsControl.h"

#include <wx/notebook.h>
#include <wx/display.h>

#include "logger/Logger.h"
#include "ExportSettings.h"
#include "GeneralSettings.h"

IMPLEMENT_CLASS(SettingsControl, wxWindow)

BEGIN_EVENT_TABLE(SettingsControl, wxWindow)
  
END_EVENT_TABLE()

SettingsControl::SettingsControl(wxWindow* parent, wxWindowID id)
{
  LOG_INFO << "Creating Settings Control...";
  
  if (Create(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("SettingsControlFrame")) == false) {
    LOG_ERROR << "Failed to create the window for our SettingsControl!";
    return;
  }

  //
  notebook = new wxNotebook(this, ID_SETTINGS_TABS, wxDefaultPosition, wxDefaultSize, wxNB_TOP|wxNB_FIXEDWIDTH|wxNB_NOPAGETHEME);
  
  page1 = new GeneralSettings(notebook, ID_GENERAL_SETTINGS);
  page3 = new ExportSettings(notebook, ID_EXPORT_SETTINGS);

  // No Display page: its OpenGL display mode, field of view, GL capability and environment-mapping
  // options only ever configured the OpenGL viewport, which is archived (DisplaySettings is unreferenced).
  notebook->AddPage(page1, _("General"), false, -1);
  notebook->AddPage(page3, _("Export"), false);
  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(notebook, 1, wxEXPAND);
  SetSizer(sizer);
  // Keep labels and the folder buttons readable; height can shrink and scroll.
  SetMinSize(wxSize(notebook->CalcSizeFromPage(page1->GetSizer()->GetMinSize()).x, FromDIP(180)));
}

wxSize SettingsControl::InitialFloatingSize() const
{
  // Allow for notebook tabs and the AUI floating frame, using the actual content
  // height rather than a fixed height that becomes stale as options are added.
  wxSize size = notebook->CalcSizeFromPage(page1->GetSizer()->GetMinSize());
  size.IncBy(FromDIP(32), FromDIP(80));
  size.SetWidth(wxMax(size.GetWidth(), FromDIP(440)));
  const int display = wxDisplay::GetFromWindow(GetParent());
  const wxSize available = wxDisplay(display == wxNOT_FOUND ? 0u : static_cast<unsigned>(display)).GetClientArea().GetSize();
  size.SetWidth(wxMin(size.GetWidth(), available.GetWidth() - FromDIP(24)));
  size.SetHeight(wxMin(size.GetHeight(), available.GetHeight() - FromDIP(24)));
  return size;
}



SettingsControl::~SettingsControl()
{
}


void SettingsControl::Open()
{
  Show(true);

  page1->Update();
  page3->Update();
}

void SettingsControl::Close()
{
  
}

// --
