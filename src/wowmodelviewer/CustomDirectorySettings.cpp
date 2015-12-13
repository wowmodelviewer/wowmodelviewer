/*
 * CustomDirectorySettings.cpp
 *
 *  Created on: 11 dec 2015
 *      Author: Jeromnimo
 */

#include "CustomDirectorySettings.h"

#include "logger/Logger.h"
#include "globalvars.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(CustomDirectorySettings, wxWindow)

BEGIN_EVENT_TABLE(CustomDirectorySettings, wxWindow)
  EVT_BUTTON(ID_CUSTOM_DIRECTORY_SETTINGS_APPLY, CustomDirectorySettings::OnButton)
END_EVENT_TABLE()

CustomDirectorySettings::CustomDirectorySettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("CustomDirectorySettings")) == false) {
    LOG_ERROR << "CustomDirectorySettings";
    return;
  }

  new wxStaticText(this, wxID_ANY, _("Folder to explore to add additional files"),  wxPoint(5,90), wxDefaultSize, 0);
  customDirectoryPathCtrl =  new wxTextCtrl(this, wxID_ANY, "", wxPoint(5,115), wxSize(300,-1), 0);
  new wxButton(this, ID_CUSTOM_DIRECTORY_SETTINGS_APPLY, _("Apply"), wxPoint(315,110), wxDefaultSize, 0);
}


void CustomDirectorySettings::Update()
{
  customDirectoryPathCtrl->SetValue(customDirectoryPath);
}

void CustomDirectorySettings::OnButton(wxCommandEvent &event)
{
  if ( event.GetId() == ID_CUSTOM_DIRECTORY_SETTINGS_APPLY)
  {
    if(customDirectoryPath !=  customDirectoryPathCtrl->GetValue())
    {
      customDirectoryPath = customDirectoryPathCtrl->GetValue();
      wxMessageBox(wxT("Additional path changed.\nYou need to restart WoW Model Viewer to take it into account"), wxT("Settings Changed"), wxICON_INFORMATION);
      g_modelViewer->SaveSession();
    }
  }

}

