/*
 * ExportSettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "ExportSettings.h"

#include "logger/Logger.h"
#include "globalvars.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(ExportSettings, wxWindow)

BEGIN_EVENT_TABLE(ExportSettings, wxWindow)
  EVT_CHECKBOX(ID_SETTINGS_INIT_POSE_ONLY_EXPORT, ExportSettings::OnCheck)
  EVT_BUTTON(ID_EXPORT_SETTINGS_APPLY, ExportSettings::OnButton)
END_EVENT_TABLE()

ExportSettings::ExportSettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("ExportSettings")) == false) {
    LOG_ERROR << "ExportSettings";
    return;
  }

  chkbox[CHECK_INIT_POSE_ONLY_EXPORT] = new wxCheckBox(this, ID_SETTINGS_INIT_POSE_ONLY_EXPORT, _("Initial Pose Only Export"), wxPoint(5,50), wxDefaultSize, 0);
  new wxButton(this, ID_EXPORT_SETTINGS_APPLY, _("Apply"), wxPoint(315,110), wxDefaultSize, 0);
}

void ExportSettings::Update()
{
  chkbox[CHECK_INIT_POSE_ONLY_EXPORT]->SetValue(GLOBALSETTINGS.bInitPoseOnlyExport);
}

void ExportSettings::OnCheck(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id==ID_SETTINGS_INIT_POSE_ONLY_EXPORT) {
    GLOBALSETTINGS.bInitPoseOnlyExport = event.IsChecked();
  }
}

void ExportSettings::OnButton(wxCommandEvent &event)
{
  if (event.GetId() == ID_EXPORT_SETTINGS_APPLY) {
    g_modelViewer->SaveSession();
  }
}

