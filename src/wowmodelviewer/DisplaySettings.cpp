/*
 * DisplaySettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "DisplaySettings.h"

#include "globalvars.h"
#include "logger/Logger.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(DisplaySettings, wxWindow)

BEGIN_EVENT_TABLE(DisplaySettings, wxWindow)
  EVT_BUTTON(ID_DISPLAY_SETTINGS_APPLY, DisplaySettings::OnButton)
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
  ADD_CONTROLS(CHECK_ENVMAPPING, wxID_ANY, _("Environmental Mapping"))
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
  oglMode->Clear();

  for (size_t i=0; i<video.capsList.size(); i++) {
    wxString mode = wxString::Format(wxT("Colour:%i Depth:%i Alpha:%i "), video.capsList[i].colour, video.capsList[i].zBuffer, video.capsList[i].alpha);

    if (video.capsList[i].sampleBuffer)
      mode.Append(wxString::Format(wxT("FSAA:%i "), video.capsList[i].aaSamples));

    if (video.capsList[i].doubleBuffer)
      mode.Append(wxT("DoubleBuffer "));

#ifdef _WINDOWS
    if (video.capsList[i].hwAcc == WGL_FULL_ACCELERATION_ARB)
      mode.Append(wxT("Hardware mode"));
    else if (video.capsList[i].hwAcc == WGL_GENERIC_ACCELERATION_ARB)
      mode.Append(wxT("Emulation mode"));
    else //WGL_NO_ACCELERATION_ARB
      mode.Append(wxT("Software mode"));
#endif

    oglMode->Append(mode);
  }

  oglMode->SetSelection(video.capIndex);

  txtFov->SetValue(wxString::Format(wxT("%f"), video.fov));

  // Toggle all the video options
  if (video.supportCompression)
    chkBox[CHECK_COMPRESSEDTEX]->SetValue(video.useCompression);
  else
    chkBox[CHECK_COMPRESSEDTEX]->Disable();

  if (video.supportMultiTex) {
    chkBox[CHECK_MULTITEX]->SetValue(true);
    chkBox[CHECK_MULTITEX]->Disable();
  } else
    chkBox[CHECK_MULTITEX]->Disable();

  if (video.supportVBO)
    chkBox[CHECK_VBO]->SetValue(video.useVBO);
  else
    chkBox[CHECK_VBO]->Disable();

  if (video.supportFBO)
    chkBox[CHECK_FBO]->SetValue(video.useFBO);
  else
    chkBox[CHECK_FBO]->Disable();

  if (video.supportPBO)
    chkBox[CHECK_PBO]->SetValue(video.usePBO);
  else
    chkBox[CHECK_PBO]->Disable();

  if (video.supportDrawRangeElements) {
    chkBox[CHECK_DRAWRANGEELEMENTS]->SetValue(true);
    chkBox[CHECK_DRAWRANGEELEMENTS]->Disable();
  } else
    chkBox[CHECK_DRAWRANGEELEMENTS]->Disable();

  chkBox[CHECK_ENVMAPPING]->SetValue(video.useEnvMapping);

  if (video.supportNPOT) {
    chkBox[CHECK_NPOT]->SetValue(true);
    chkBox[CHECK_NPOT]->Disable();
  } else
    chkBox[CHECK_NPOT]->Disable();

  if (video.supportFragProg)
    chkBox[CHECK_PIXELSHADERS]->SetValue(true);
  else
    chkBox[CHECK_PIXELSHADERS]->Disable();

  if (video.supportVertexProg)
    chkBox[CHECK_VERTEXSHADERS]->SetValue(true);
  else
    chkBox[CHECK_VERTEXSHADERS]->Disable();

  if (video.supportGLSL)
    chkBox[CHECK_GLSLSHADERS]->SetValue(true);
  else
    chkBox[CHECK_GLSLSHADERS]->Disable();
}

void DisplaySettings::OnButton(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id == ID_DISPLAY_SETTINGS_APPLY) {
    if ((oglMode->GetSelection() != video.capIndex) && video.GetCompatibleWinMode(video.capsList[oglMode->GetSelection()])) {
      LOG_INFO << "Graphics display mode changed.  Requires restart to take effect.";
      wxMessageBox(wxT("Graphics display settings changed.\nWoW Model Viewer requires restarting to take effect."), wxT("Settings Changed"), wxICON_INFORMATION);
    }

    double fov;
    txtFov->GetValue().ToDouble(&fov);
    if ((fov > 0) && (fov < 270.0))
      video.fov = (float) fov;

    g_modelViewer->SaveSession();
    g_modelViewer->interfaceManager.GetPane(this->GetParent()).Show(false);
    g_modelViewer->interfaceManager.Update();
  }
}
