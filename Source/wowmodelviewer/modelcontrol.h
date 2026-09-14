#ifndef MODELCONTROL_H
#define MODELCONTROL_H

#include <wx/wxprec.h>
#include <wx/clrpicker.h>
#include <wx/treectrl.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "WoWModel.h"
#include "modelcanvas.h"
#include "animcontrol.h"

// View > "Attachments": pick the model or attachment the Animation panel drives, and set Render and Scale
// for an item attached to a character (the Unity viewport receives both in the character scene). See
// modelcontrol.cpp for what was removed with the OpenGL viewport.

class ModelControl: public wxWindow
{
  DECLARE_CLASS(ModelControl)
  DECLARE_EVENT_TABLE()

  wxComboBox *modelname;
  // wxComboBox *cbLod;
  wxSlider *scale;
  wxCheckBox *render;
  wxTextCtrl *txtsize;
  wxStaticText *hint;

  // Whether Render and Scale on the current selection reach the viewport: it is an item attached
  // directly to a character.
  bool selectionReachesViewport() const;

  // List of models in the scene.
  //std::vector<Model*> models;
  std::vector<Attachment*> attachments;
  bool init;
  
public:
  WoWModel *model;  // Currently 'active' model.
  Attachment *att; // Currently 'active' attachment.
  AnimControl *animControl;
  ModelControl(wxWindow* parent, wxWindowID id);
  ~ModelControl();

  void UpdateModel(Attachment *a);
  void Update();
  void UpdateGeosetSelection();
  void RefreshModel(Attachment *root);
  void OnCheck(wxCommandEvent &event);
  void OnCombo(wxCommandEvent &event);
  void OnSlider(wxScrollEvent &event);
  void OnEnter(wxCommandEvent &event);
};

class ScrWindow : public wxFrame
{
  wxScrolledWindow *sw;
  wxStaticBitmap *sb;
public:
  ScrWindow(const wxString& title);
  ~ScrWindow();
};

#endif

