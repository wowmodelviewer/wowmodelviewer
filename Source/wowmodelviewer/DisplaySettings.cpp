// archived: unreachable since the OpenGL viewport was archived (the Settings > Display page is no longer added).
/*
 * DisplaySettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "DisplaySettings.h"

#include "globalvars.h"
#include "logger/Logger.h"
#include "modelcanvas.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(DisplaySettings, wxWindow)

BEGIN_EVENT_TABLE(DisplaySettings, wxWindow)
  EVT_BUTTON(ID_DISPLAY_SETTINGS_APPLY, DisplaySettings::OnButton)
  EVT_CHECKBOX(CHECK_ENVMAPPING, DisplaySettings::OnCheck)
END_EVENT_TABLE()

DisplaySettings::DisplaySettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("DisplaySettings")) == false) {
    LOG_ERROR << "DisplaySettings";
    return;
  }

  wxFlexGridSizer *top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);
  top->SetFlexibleDirection(wxBOTH);

  top->Add(new wxStaticText(this, wxID_ANY, _("OpenGL Display Mode:"), wxDefaultPosition, wxDefaultSize, 0), 1, wxEXPAND|wxALL, 10);
  top->Add(oglMode = new wxComboBox(this, wxID_ANY, _("Default"), wxDefaultPosition, wxSize(360, 25), 0, 0, wxCB_READONLY), 1, wxEXPAND, 10);

  top->Add(new wxStaticText(this, wxID_ANY, _("Field of View:"), wxDefaultPosition, wxDefaultSize, 0), 1, wxEXPAND|wxALL, 10);
  top->Add(txtFov = new wxTextCtrl(this, wxID_ANY, wxT("45.000000"), wxDefaultPosition, wxSize(100, 20)), 1, 0, 10);

  wxFlexGridSizer *gs = new wxFlexGridSizer(3, 4, 4);

  #define ADD_CONTROLS(index, id, caption) \
  gs->Add(chkBox[index] = new wxCheckBox(this, id, caption, wxDefaultPosition, wxDefaultSize, 0), wxSizerFlags(0).Align(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL));

  ADD_CONTROLS(CHECK_COMPRESSEDTEX, wxID_ANY, _("Compressed Textures"))
  ADD_CONTROLS(CHECK_MULTITEX, wxID_ANY, _("Multi-Textures"))
  ADD_CONTROLS(CHECK_VBO, wxID_ANY, _("Vertex Buffer"))
  ADD_CONTROLS(CHECK_FBO, wxID_ANY, _("Frame Buffer"))
  ADD_CONTROLS(CHECK_PBO, wxID_ANY, _("Pixel Buffer"))
  ADD_CONTROLS(CHECK_DRAWRANGEELEMENTS, wxID_ANY, _("Draw Range Elements"))
  ADD_CONTROLS(CHECK_ENVMAPPING, ID_CHECK_ENVMAPPING, _("Environmental Mapping"))
  ADD_CONTROLS(CHECK_NPOT, wxID_ANY, _("Non-Power of two"))
  ADD_CONTROLS(CHECK_PIXELSHADERS, wxID_ANY, _("Pixel Shaders"))
  ADD_CONTROLS(CHECK_VERTEXSHADERS, wxID_ANY, _("Vertex Shaders"))
  ADD_CONTROLS(CHECK_GLSLSHADERS, wxID_ANY, _("GLSL Shaders"))
  #undef ADD_CONTROLS

  top->Add(gs,wxSizerFlags().Proportion(1).Expand().Border(wxALL, 10));

  top->Add(new wxButton(this, ID_DISPLAY_SETTINGS_APPLY, _("Apply Settings"), wxDefaultPosition, wxDefaultSize, 0), wxSizerFlags()/*.Expand()*/.Border(wxALL, 10).Align(wxALIGN_LEFT|wxALIGN_BOTTOM));

  top->SetMinSize(350, 350);
  //top->SetMaxSize(400, 400);
  top->SetSizeHints(this);
  SetSizer(top);
  SetAutoLayout(true);
  Layout();
}

void DisplaySettings::Update()
{
  // The renderer in use (the viewport's Vulkan device): there is one mode, and every feature these boxes name is
  // part of the renderer, so they read as supported.
  oglMode->Clear();
  const wmv::vk::Viewport * renderer = g_canvas ? g_canvas->renderer() : nullptr;
  oglMode->Append(renderer ? wxString::FromUTF8(renderer->description().c_str())
                           : (g_canvas && !g_canvas->rendererError().IsEmpty() ? g_canvas->rendererError()
                                                                              : wxString(_("Vulkan (not started yet)"))));
  oglMode->SetSelection(0);

  txtFov->SetValue(wxString::Format(wxT("%f"), video.fov));

  chkBox[CHECK_ENVMAPPING]->SetValue(video.useEnvMapping);
  for (int box : { CHECK_DRAWRANGEELEMENTS, CHECK_COMPRESSEDTEX, CHECK_MULTITEX, CHECK_VBO, CHECK_FBO, CHECK_PBO,
                   CHECK_NPOT, CHECK_PIXELSHADERS, CHECK_VERTEXSHADERS, CHECK_GLSLSHADERS })
  {
    chkBox[box]->Disable();
    chkBox[box]->SetValue(renderer != nullptr);
  }
}

void DisplaySettings::OnButton(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id == ID_DISPLAY_SETTINGS_APPLY) {

    double fov;
    txtFov->GetValue().ToDouble(&fov);
    if ((fov > 0) && (fov < 270.0))
      video.fov = (float) fov;

    g_modelViewer->SaveSession();
    g_modelViewer->interfaceManager.GetPane(this->GetParent()).Show(false);
    g_modelViewer->LayoutNow();
  }
}

void DisplaySettings::OnCheck(wxCommandEvent &event)
{
  if (event.GetId() == ID_CHECK_ENVMAPPING)
    video.useEnvMapping = event.IsChecked();
}
