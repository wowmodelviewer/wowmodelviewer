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

#include "enums.h"

// ModelName
// LevelOfDetail
// Opacity
// Bones
// Bounding Box
// Render
// Wireframe
// Geosets
// Future Additions:
//		- Pos
//		- Rotation
//		- Scale
//		- Attach model

class ModelControl: public wxWindow
{
  DECLARE_CLASS(ModelControl)
  DECLARE_EVENT_TABLE()
  class GeosetTreeItemData : public wxTreeItemData
  {
    public:
      size_t geosetId;
  };
	
  wxComboBox *cbLod, *modelname;
  wxSlider *alpha, *scale;
  wxCheckBox *bones, *box, *render, *wireframe, *texture, *particles, *particlecolreplace;
  //wxCheckListBox *clbGeosets;
  wxTreeCtrl *clbGeosets;
  wxTextCtrl *txtX, *txtY, *txtZ;
  wxTextCtrl *rotX, *rotY, *rotZ;
  wxColourPickerCtrl *PC11S, *PC11M, *PC11E, *PC12S, *PC12M, *PC12E, *PC13S, *PC13M, *PC13E;
  wxTextCtrl *txtsize;
  wxStaticText *PC11SLabel, *PC11MLabel, *PC11ELabel, *PC12SLabel, *PC12MLabel, *PC12ELabel;
  wxStaticText *PC13SLabel, *PC13MLabel, *PC13ELabel;
  wxStaticText *NoPC, *PCHint;

  // List of models in the scene.
  //std::vector<Model*> models;
  std::vector<Attachment*> attachments;
  bool init;
	
public:
  WoWModel *model;	// Currently 'active' model.
  Attachment *att; // Currently 'active' attachment.
  AnimControl *animControl;
  particleColorReplacements pcr;
  std::map<std::string, particleColorReplacements> modelPCRSaves;
  ModelControl(wxWindow* parent, wxWindowID id);
  ~ModelControl();

  void UpdateModel(Attachment *a);
  void Update();
  void RefreshModel(Attachment *root);
  void OnCheck(wxCommandEvent &event);
  void OnCombo(wxCommandEvent &event);
  void OnList(wxTreeEvent &event);
  void OnSlider(wxScrollEvent &event);
  void OnEnter(wxCommandEvent &event);
  void OnColourChange(wxColourPickerEvent &event);
  void TogglePCRFields();
  void UpdatePCRText(wxColourPickerCtrl *cpc);
  void UpdatePCRTexts();
  bool IsReplacingParticleColors();
  Vec4D fromColWidget(wxColour col);
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

