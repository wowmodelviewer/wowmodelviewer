// Copied from Settings
#ifndef MODELEXPORTOPTIONS_H
#define MODELEXPORTOPTIONS_H

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/notebook.h>

// Custom headers
#include "util.h"
#include "enums.h"

// Options Functions
class ModelExportOptions_General: public wxWindow
{
	DECLARE_CLASS(ModelExportOptions_General)
    DECLARE_EVENT_TABLE()

	wxCheckBox *chkbox[NUM_MEO1_CHECK];
	wxComboBox *ddextype;
	wxStaticText *text;
	//wxListBox *mpqList;
	//wxTextCtrl *txtPath;

public:

	ModelExportOptions_General(wxWindow* parent, wxWindowID id);
	~ModelExportOptions_General(){};

	void Update();

	void OnButton(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnComboBox(wxCommandEvent &event);
};

class ModelExportOptions_Lightwave: public wxWindow
{
	DECLARE_CLASS(ModelExportOptions_Lightwave)
    DECLARE_EVENT_TABLE()

	wxCheckBox *chkbox[NUM_MEO2_CHECK];
	wxComboBox *ddextype;
	//wxTextCtrl *txtFov;

public:

	ModelExportOptions_Lightwave(wxWindow* parent, wxWindowID id);
	~ModelExportOptions_Lightwave() {};

	void Update();

	void OnButton(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnComboBox(wxCommandEvent &event);
};

class ModelExportOptions_X3D: public wxWindow
{
    DECLARE_CLASS(ModelExportOptions_X3D)
    DECLARE_EVENT_TABLE()

    wxCheckBox *chkbox[NUM_MEO3_CHECK];
    wxFlexGridSizer *top;

public:

    ModelExportOptions_X3D(wxWindow* parent, wxWindowID id);
    ~ModelExportOptions_X3D() {};

    void Update();

    void OnCheck(wxCommandEvent &event);
};

class ModelExportOptions_M3: public wxWindow
{
    DECLARE_CLASS(ModelExportOptions_M3)
    DECLARE_EVENT_TABLE()

    wxStaticText *stBoundScale, *stSphereScale;
	wxStaticText *stTexturePath, *stRename;
	wxButton *bApply, *bReset, *bRename;

public:
	wxTextCtrl *tcBoundScale, *tcSphereScale, *tcTexturePath, *tcRename;
	wxCheckListBox *clbAnimations;
	wxArrayString asAnims;

    ModelExportOptions_M3(wxWindow* parent, wxWindowID id);
	~ModelExportOptions_M3() {};

    void Update();
	void OnButton(wxCommandEvent &event);
	void OnChoice(wxCommandEvent &event);
};

class ModelExportOptions_Control: public wxWindow
{
	DECLARE_CLASS(ModelExportOptions_Control)
    DECLARE_EVENT_TABLE()

	wxNotebook *notebook;
	ModelExportOptions_General *page1;
	ModelExportOptions_Lightwave *page2;
    ModelExportOptions_X3D *page3;
	ModelExportOptions_M3 *page4;
public:

	ModelExportOptions_Control(wxWindow* parent, wxWindowID id);
	~ModelExportOptions_Control();
	
	void Open();
	void OnClose(wxCloseEvent &event);
};

#endif
