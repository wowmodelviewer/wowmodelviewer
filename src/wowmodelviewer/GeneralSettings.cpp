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
#include "util.h"
#include <wx/stattext.h>
#include <wx/statline.h>

IMPLEMENT_CLASS(GeneralSettings, wxWindow)

BEGIN_EVENT_TABLE(GeneralSettings, wxWindow)
  EVT_CHECKBOX(ID_SETTINGS_RANDOMSKIN, GeneralSettings::OnCheck)
  EVT_CHECKBOX(ID_SETTINGS_SHOWPARTICLE, GeneralSettings::OnCheck)
  EVT_CHECKBOX(ID_SETTINGS_ZEROPARTICLE, GeneralSettings::OnCheck)
  EVT_CHECKBOX(ID_SETTINGS_DISPLAYIDINLIST, GeneralSettings::OnCheck)
  EVT_BUTTON(ID_GENERAL_SETTINGS_APPLY, GeneralSettings::OnButton)
  EVT_BUTTON(ID_FIND_GAME_FOLDER, GeneralSettings::OnButton)
  EVT_BUTTON(ID_FIND_CUSTOM_FOLDER, GeneralSettings::OnButton)
  EVT_BUTTON(ID_ERASE_CUSTOM_FOLDER, GeneralSettings::OnButton)
END_EVENT_TABLE()

GeneralSettings::GeneralSettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxPoint(0,0), wxSize(400,550), 0, wxT("GeneralSettings")) == false)
  {
    LOG_ERROR << "GeneralSettings";
    return;
  }
  wxFlexGridSizer *top = new wxFlexGridSizer(1);

  wxGridSizer *sizer = new wxGridSizer(2, 2, 5, 5);

  chkbox[CHECK_SHOWPARTICLE] = new wxCheckBox(this, ID_SETTINGS_SHOWPARTICLE, _("Show Particle"), wxDefaultPosition, wxDefaultSize, 0);
  chkbox[CHECK_ZEROPARTICLE] = new wxCheckBox(this, ID_SETTINGS_ZEROPARTICLE, _("Zero Particle"), wxDefaultPosition, wxDefaultSize, 0);
  chkbox[CHECK_RANDOMSKIN] = new wxCheckBox(this, ID_SETTINGS_RANDOMSKIN, _("Random Skins"), wxDefaultPosition, wxDefaultSize, 0);
  chkbox[CHECK_DISPLAYIDINLIST] = new wxCheckBox(this, ID_SETTINGS_DISPLAYIDINLIST, _("Display Items/NPCs' IDs in lists"), wxDefaultPosition, wxDefaultSize, 0);

  sizer->Add(chkbox[CHECK_SHOWPARTICLE], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(chkbox[CHECK_ZEROPARTICLE], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(chkbox[CHECK_RANDOMSKIN], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(chkbox[CHECK_DISPLAYIDINLIST], 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);

  wxString policies[2] = {_("Use game files"), _("Use custom files")};

  keepPolicyRadioBox = new wxRadioBox(this, -1, _("Conflict policy"), wxDefaultPosition, wxDefaultSize, 2, policies, 2);
  gamePathDisplay =  new wxTextCtrl(this, wxID_ANY, gamePath, wxDefaultPosition, wxSize(300,-1), wxTE_READONLY);
  wxString customMsg = customDirectoryPath;
  if (customMsg.IsEmpty())
    customMsg = wxString("Select a folder...");
  customDirectoryPathDisplay =  new wxTextCtrl(this, wxID_ANY, customMsg, wxDefaultPosition, wxSize(300,-1), wxTE_READONLY);
  top->AddSpacer(2);
  top->Add(new wxStaticText(this, wxID_ANY, _("Game Folder"),  wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->Add(gamePathDisplay, 0, wxALL, 5);
  top->Add(new wxButton(this, ID_FIND_GAME_FOLDER, _("Change Game Folder"), wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->AddSpacer(2);
  top->Add(new wxStaticLine(this, wxID_ANY), 1, wxEXPAND);
  top->AddSpacer(2);
  top->Add(new wxStaticText(this, wxID_ANY, _("Custom / Imported Files"),  wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->Add(customDirectoryPathDisplay, 0, wxALL, 5);
  wxFlexGridSizer * gbox = new wxFlexGridSizer(2, 5, 5);
  gbox->Add(new wxButton(this, ID_FIND_CUSTOM_FOLDER, _("Select Custom Folder"), wxDefaultPosition, wxDefaultSize, 0));
  gbox->Add(new wxButton(this, ID_ERASE_CUSTOM_FOLDER, _("No Custom Folder"), wxDefaultPosition, wxDefaultSize, 0));
  top->Add(gbox, 0, wxALL, 5);
  wxStaticText *customFileMsg = new wxStaticText(this, wxID_ANY,
                                                 _("Custom files should be placed in a folder hierarchy that mirrors "
                                                   "the game database (ie <Custom Folder>/Creature/Dragon/Dragon.m2)"),
                                                 wxDefaultPosition, wxDefaultSize, 0);
  customFileMsg->Wrap(350);

  top->Add(customFileMsg, 0, wxALL, 5);
  top->AddSpacer(2);
  top->Add(keepPolicyRadioBox, 0, wxALL, 5);
  top->AddSpacer(2);
  top->Add(new wxStaticLine(this, wxID_ANY), 1, wxEXPAND);
  top->AddSpacer(2);
  top->Add(new wxStaticText(this, wxID_ANY, _("Other"),  wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->Add(sizer, 0, wxEXPAND | wxALL, 5);
  top->AddSpacer(2);
  top->Add(new wxButton(this, ID_GENERAL_SETTINGS_APPLY, _("Apply"), wxDefaultPosition, wxDefaultSize, 0), 0, wxALL, 5);
  top->SetMinSize(350, 550);
  SetSizer(top);
  SetAutoLayout(true);
  Layout();
}


void GeneralSettings::OnCheck(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id == ID_SETTINGS_RANDOMSKIN) {
    useRandomLooks = event.IsChecked();
  } else if (id == ID_SETTINGS_SHOWPARTICLE) {
    GLOBALSETTINGS.bShowParticle = event.IsChecked();
  } else if (id == ID_SETTINGS_ZEROPARTICLE) {
    GLOBALSETTINGS.bZeroParticle = event.IsChecked();
  } else if (id == ID_SETTINGS_DISPLAYIDINLIST) {
    displayItemAndNPCId = event.IsChecked();
  }
}

void GeneralSettings::Update()
{
  chkbox[CHECK_RANDOMSKIN]->SetValue(useRandomLooks);
  chkbox[CHECK_SHOWPARTICLE]->SetValue(GLOBALSETTINGS.bShowParticle);
  chkbox[CHECK_ZEROPARTICLE]->SetValue(GLOBALSETTINGS.bZeroParticle);
  chkbox[CHECK_DISPLAYIDINLIST]->SetValue(displayItemAndNPCId);
  gamePathDisplay->SetValue(gamePath);
  if (customDirectoryPath.IsEmpty())
    customDirectoryPathDisplay->SetValue("Select a folder...");
  else
    customDirectoryPathDisplay->SetValue(customDirectoryPath);
  keepPolicyRadioBox->SetSelection(customFilesConflictPolicy);
  newGamePath = wxEmptyString;
  newCustomFolder = wxEmptyString;
}

void GeneralSettings::OnButton(wxCommandEvent &event)
{
  int id = event.GetId();

  if ( id == ID_GENERAL_SETTINGS_APPLY)
  {
    bool settingsChanged = false;
    if(!newGamePath.IsEmpty() && gamePath !=  newGamePath)
    {
      gamePath = newGamePath;
      settingsChanged = true;
    }
    if (newCustomFolder == wxString("ERASE"))
    {
      if (customDirectoryPath != wxEmptyString)
        settingsChanged = true;
      customDirectoryPath = wxEmptyString;
    }
    else if(!newCustomFolder.IsEmpty() && customDirectoryPath !=  newCustomFolder)
    {
      customDirectoryPath = newCustomFolder;
      settingsChanged = true;
    }
    if(customFilesConflictPolicy != keepPolicyRadioBox->GetSelection())
    {
      customFilesConflictPolicy = keepPolicyRadioBox->GetSelection();
      settingsChanged = true;
    }

    if(settingsChanged)
    {
      wxMessageBox(wxT("Settings changed.\nYou need to restart WoW Model Viewer to take them into account"),
                   wxT("Settings Changed"), wxICON_INFORMATION);
      g_modelViewer->SaveSession();
      settingsChanged = false;
    }
  }

  else if (id == ID_FIND_GAME_FOLDER)
  {
    wxString newPath = getGamePath(true);
    if (newPath.IsEmpty()) // user probably hit cancel
      return;
    gamePathDisplay->SetValue(newPath);
    newGamePath = newPath;
  }

  else if (id == ID_FIND_CUSTOM_FOLDER)
  {
    wxDirDialog *customDirPicker = new wxDirDialog(this, _("Select the folder containing your custom files."), 
                                                   customDirectoryPath, 0);
    int i = customDirPicker->ShowModal();
    if (i != wxID_OK)
      return;
    newCustomFolder = customDirPicker->GetPath();
    customDirectoryPathDisplay->SetValue(newCustomFolder);
  }

  else if (id == ID_ERASE_CUSTOM_FOLDER)
  {
    newCustomFolder = wxString("ERASE");
    customDirectoryPathDisplay->SetValue(wxString("Select a folder.."));
  }
}


