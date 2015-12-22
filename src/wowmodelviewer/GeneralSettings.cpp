/*
 * GeneralSettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "GeneralSettings.h"

#include "logger/Logger.h"
#include "globalvars.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(GeneralSettings, wxWindow)

BEGIN_EVENT_TABLE(GeneralSettings, wxWindow)
  EVT_CHECKBOX(ID_SETTINGS_RANDOMSKIN, GeneralSettings::OnCheck)
  EVT_CHECKBOX(ID_SETTINGS_SHOWPARTICLE, GeneralSettings::OnCheck)
  EVT_CHECKBOX(ID_SETTINGS_ZEROPARTICLE, GeneralSettings::OnCheck)
  EVT_BUTTON(ID_GENERAL_SETTINGS_APPLY, GeneralSettings::OnButton)
END_EVENT_TABLE()

GeneralSettings::GeneralSettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("GeneralSettings")) == false) {
    LOG_ERROR << "GeneralSettings";
    return;
  }
  wxFlexGridSizer *top = new wxFlexGridSizer(1);

  wxBoxSizer *sizer = new wxBoxSizer( wxHORIZONTAL );

  chkbox[CHECK_SHOWPARTICLE] = new wxCheckBox(this, ID_SETTINGS_SHOWPARTICLE, _("Show Particle"), wxDefaultPosition, wxDefaultSize, 0);
  chkbox[CHECK_ZEROPARTICLE] = new wxCheckBox(this, ID_SETTINGS_ZEROPARTICLE, _("Zero Particle"), wxDefaultPosition, wxDefaultSize, 0);
  chkbox[CHECK_RANDOMSKIN] = new wxCheckBox(this, ID_SETTINGS_RANDOMSKIN, _("Random Skins"), wxDefaultPosition, wxDefaultSize, 0);

  sizer->Add(chkbox[CHECK_SHOWPARTICLE], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(chkbox[CHECK_ZEROPARTICLE], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(chkbox[CHECK_RANDOMSKIN], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);

  wxString policies[2] = {_("Keep game files"), _("Keep custom files")};

  keepPolicyRadioBox = new wxRadioBox(this, -1, _("Conflict policy"), wxDefaultPosition, wxDefaultSize, 2, policies, 2);
  gamePathCtrl =  new wxTextCtrl(this, wxID_ANY, gamePath, wxDefaultPosition, wxSize(300,-1), 0);
  customDirectoryPathCtrl =  new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(300,-1), 0);

  top->Add(sizer, 0, wxEXPAND | wxALL, 10);
  top->Add(new wxStaticText(this, wxID_ANY, _("Game path (including final \\Data statement - ie C:\\Games\\WoW\\Data)"),  wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->Add(gamePathCtrl, 0, wxALL, 5);
  top->Add(new wxStaticText(this, wxID_ANY, _("Folder to explore for additional files"),  wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->Add(customDirectoryPathCtrl, 0, wxALL, 5);
  top->Add(keepPolicyRadioBox, 0, wxALL, 5);
  top->Add(new wxButton(this, ID_GENERAL_SETTINGS_APPLY, _("Apply"), wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->SetMinSize(350, 350);
  SetSizer(top);
  SetAutoLayout(true);
  Layout();
}


void GeneralSettings::OnCheck(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id==ID_SETTINGS_RANDOMSKIN) {
    useRandomLooks = event.IsChecked();
  } else if (id==ID_SETTINGS_SHOWPARTICLE) {
    GLOBALSETTINGS.bShowParticle = event.IsChecked();
  } else if (id==ID_SETTINGS_ZEROPARTICLE) {
    GLOBALSETTINGS.bZeroParticle = event.IsChecked();
  }
}

void GeneralSettings::Update()
{
  chkbox[CHECK_RANDOMSKIN]->SetValue(useRandomLooks);
  chkbox[CHECK_SHOWPARTICLE]->SetValue(GLOBALSETTINGS.bShowParticle);
  chkbox[CHECK_ZEROPARTICLE]->SetValue(GLOBALSETTINGS.bZeroParticle);
  gamePathCtrl->SetValue(gamePath);
  customDirectoryPathCtrl->SetValue(customDirectoryPath);
  keepPolicyRadioBox->SetSelection(customFilesConflictPolicy);
}

void GeneralSettings::OnButton(wxCommandEvent &event)
{
  if ( event.GetId() == ID_GENERAL_SETTINGS_APPLY)
  {
    bool settingsChanged = false;

    if(gamePath != gamePathCtrl->GetValue())
    {
      gamePath = gamePathCtrl->GetValue();
      settingsChanged = true;
    }

    if(customDirectoryPath !=  customDirectoryPathCtrl->GetValue())
    {
      customDirectoryPath = customDirectoryPathCtrl->GetValue();
      settingsChanged = true;
    }

    if(customFilesConflictPolicy != keepPolicyRadioBox->GetSelection())
    {
      customFilesConflictPolicy = keepPolicyRadioBox->GetSelection();
      settingsChanged = true;
    }

    if(settingsChanged)
    {
      wxMessageBox(wxT("Settings changed.\nYou need to restart WoW Model Viewer to take them into account"), wxT("Settings Changed"), wxICON_INFORMATION);
      g_modelViewer->SaveSession();
    }
  }
}

