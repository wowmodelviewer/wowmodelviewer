/*
 * GeneralSettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "GeneralSettings.h"
#include "globalvars.h"
#include "modelViewer.h"

#include "logger/Logger.h"

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
    wxLogMessage(wxT("GUI Error: GeneralSettings"));
    return;
  }

  chkbox[CHECK_SHOWPARTICLE] = new wxCheckBox(this, ID_SETTINGS_SHOWPARTICLE, _("Show Particle"), wxPoint(5,50), wxDefaultSize, 0);
  chkbox[CHECK_ZEROPARTICLE] = new wxCheckBox(this, ID_SETTINGS_ZEROPARTICLE, _("Zero Particle"), wxPoint(145,50), wxDefaultSize, 0);
  chkbox[CHECK_RANDOMSKIN] = new wxCheckBox(this, ID_SETTINGS_RANDOMSKIN, _("Random Skins"), wxPoint(285,50), wxDefaultSize, 0);

  new wxStaticText(this, wxID_ANY, _("Game path (including final \\Data statement - ie C:\\Games\\WoW\\Data)"),  wxPoint(5,90), wxDefaultSize, 0);
  gamePathCtrl =  new wxTextCtrl(this, wxID_ANY, gamePath, wxPoint(5,115), wxSize(300,-1), 0);
  new wxButton(this, ID_GENERAL_SETTINGS_APPLY, _("Apply"), wxPoint(315,110), wxDefaultSize, 0);
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
}

void GeneralSettings::OnButton(wxCommandEvent &event)
{
  if ( event.GetId() == ID_GENERAL_SETTINGS_APPLY)
  {
    if(gamePath !=  gamePathCtrl->GetValue())
    {
      gamePath = gamePathCtrl->GetValue();
      wxMessageBox(wxT("WoW Game Path changed.\nYou need to restart WoW Model Viewer to take it into account"), wxT("Settings Changed"), wxICON_INFORMATION);
      g_modelViewer->SaveSession();
    }
  }
}

