// Copied from Settings.cpp
#include "modelexportoptions.h"

#include "enums.h"
#include "exporters.h"
#include "filecontrol.h"
#include "globalvars.h"
#include "model.h"
#include "modelviewer.h"

#include "logger/Logger.h"


// All IDs & Vars should follow the naming structure similar to "ExportOptionswxT(3D Format)wxT(Option name)"

IMPLEMENT_CLASS(ModelExportOptions_Control, wxWindow)
IMPLEMENT_CLASS(ModelExportOptions_General, wxWindow)
IMPLEMENT_CLASS(ModelExportOptions_Lightwave, wxWindow)
IMPLEMENT_CLASS(ModelExportOptions_X3D, wxWindow)
IMPLEMENT_CLASS(ModelExportOptions_M3, wxWindow)

BEGIN_EVENT_TABLE(ModelExportOptions_General, wxWindow)
	EVT_COMBOBOX(ID_EXPORTOPTIONS_PERFERED_EXPORTER,ModelExportOptions_General::OnComboBox)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_PRESERVE_DIR, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_USE_WMV_POSROT, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_SCALE_TO_REALWORLD, ModelExportOptions_General::OnCheck)
	/*
	EVT_BUTTON(ID_SETTINGS_UP, ModelExportOptions_General::OnButton)
	EVT_BUTTON(ID_SETTINGS_DOWN, ModelExportOptions_General::OnButton)
	EVT_BUTTON(ID_SETTINGS_ADD, ModelExportOptions_General::OnButton)
	EVT_BUTTON(ID_SETTINGS_REMOVE, ModelExportOptions_General::OnButton)
	EVT_BUTTON(ID_SETTINGS_CLEAR, ModelExportOptions_General::OnButton)
	EVT_CHECKBOX(ID_SETTINGS_RANDOMSKIN, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_HIDEHELMET, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_SHOWPARTICLE, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_ZEROPARTICLE, ModelExportOptions_General::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_LOCALFILES, ModelExportOptions_General::OnCheck)
	*/
END_EVENT_TABLE()


BEGIN_EVENT_TABLE(ModelExportOptions_Lightwave, wxWindow)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_PRESERVE_DIR, ModelExportOptions_Lightwave::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_ALWAYSWRITESCENEFILE, ModelExportOptions_Lightwave::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_EXPORTLIGHTS, ModelExportOptions_Lightwave::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_EXPORTDOODADS, ModelExportOptions_Lightwave::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_EXPORTCAMERAS, ModelExportOptions_Lightwave::OnCheck)
	EVT_CHECKBOX(ID_EXPORTOPTIONS_LW_EXPORTBONES, ModelExportOptions_Lightwave::OnCheck)
	EVT_COMBOBOX(ID_EXPORTOPTIONS_LW_DOODADSAS, ModelExportOptions_Lightwave::OnComboBox)
	
	// EVT_BUTTON(ID_SETTINGS_APPLY, ModelExportOptions_Lightwave::OnButton)
END_EVENT_TABLE()


BEGIN_EVENT_TABLE(ModelExportOptions_Control, wxWindow)
	//EVT_CLOSE(ModelExportOptions_Control::OnClose)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(ModelExportOptions_X3D, wxWindow)
    EVT_CHECKBOX(ID_EXPORTOPTIONS_X3D_EXPORT_ANIMATION, ModelExportOptions_X3D::OnCheck)
    EVT_CHECKBOX(ID_EXPORTOPTIONS_X3D_CENTER_MODEL, ModelExportOptions_X3D::OnCheck)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(ModelExportOptions_M3, wxWindow)
	EVT_BUTTON(ID_EXPORTOPTIONS_M3_APPLY, ModelExportOptions_M3::OnButton)
	EVT_BUTTON(ID_EXPORTOPTIONS_M3_RESET, ModelExportOptions_M3::OnButton)
	EVT_BUTTON(ID_EXPORTOPTIONS_M3_RENAME, ModelExportOptions_M3::OnButton)
	EVT_LISTBOX(ID_EXPORTOPTIONS_M3_ANIMS, ModelExportOptions_M3::OnChoice)
END_EVENT_TABLE()


// --== Shared Options & Setup ==--

ModelExportOptions_General::ModelExportOptions_General(wxWindow* parent, wxWindowID id)
{
	if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("ModelExportOptions_General")) == false) {
		wxLogMessage(wxT("GUI Error: ModelExportOptions_General"));
		return;
	}
	wxFlexGridSizer *top = new wxFlexGridSizer(1);

	text = new wxStaticText(this, wxID_ANY, wxT("Perferred Exporter:"), wxPoint(5,9), wxDefaultSize, 0);
	top->Add(ddextype = new wxComboBox(this, ID_EXPORTOPTIONS_PERFERED_EXPORTER, wxT("Perferred Exporter"), wxPoint(115,5), wxDefaultSize, 0, 0, wxCB_READONLY), 1, wxEXPAND, 10);
	chkbox[MEO_CHECK_PRESERVE_DIR] = new wxCheckBox(this, ID_EXPORTOPTIONS_PRESERVE_DIR, wxT("Preserve Directory Structure"), wxPoint(5,30), wxDefaultSize, 0);
	chkbox[MEO_CHECK_SCALE_TO_REALWORLD] = new wxCheckBox(this, ID_EXPORTOPTIONS_SCALE_TO_REALWORLD, wxT("Scale to Real World dimensions"), wxPoint(5,50), wxDefaultSize, 0);
	chkbox[MEO_CHECK_USE_WMV_POSROT] = new wxCheckBox(this, ID_EXPORTOPTIONS_USE_WMV_POSROT, wxT("Use Position and Rotation from WMV (M2 Only)"), wxPoint(5,70), wxDefaultSize, 0);
	
}


void ModelExportOptions_General::OnButton(wxCommandEvent &event)
{
	//int id = event.GetId();
}

void ModelExportOptions_General::OnCheck(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id==ID_EXPORTOPTIONS_PRESERVE_DIR){
		modelExport_PreserveDir = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_USE_WMV_POSROT){
		modelExport_UseWMVPosRot = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_SCALE_TO_REALWORLD){
		modelExport_ScaleToRealWorld = event.IsChecked();
	}
}

void ModelExportOptions_General::OnComboBox(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id==ID_EXPORTOPTIONS_PERFERED_EXPORTER){
		Perfered_Exporter = (ddextype->GetCurrentSelection()-1);

		g_modelViewer->InitMenu();
		g_fileControl->UpdateInterface();
	}
}

void ModelExportOptions_General::Update()
{
	ddextype->Clear();
	ddextype->Append(wxString(wxT("None")));
	for (size_t x=0;x<ExporterTypeCount;x++){
		ddextype->Append(Exporter_Types[x].Name);
	}
	ddextype->SetSelection(Perfered_Exporter+1);

	//Perfered_Exporter
	chkbox[MEO_CHECK_PRESERVE_DIR]->SetValue(modelExport_PreserveDir);
	chkbox[MEO_CHECK_USE_WMV_POSROT]->SetValue(modelExport_UseWMVPosRot);
	chkbox[MEO_CHECK_SCALE_TO_REALWORLD]->SetValue(modelExport_ScaleToRealWorld);
}

ModelExportOptions_Control::ModelExportOptions_Control(wxWindow* parent, wxWindowID id)
{
	wxLogMessage(wxT("Creating Model Export Options Control..."));
	
	if (Create(parent, id, wxDefaultPosition, wxSize(405,440), wxDEFAULT_FRAME_STYLE, wxT("ModelExportOptions_ControlFrame")) == false) {
		wxLogMessage(wxT("GUI Error: Failed to create the window for our ModelExportOptions_Control!"));
		return;
	}

	notebook = new wxNotebook(this, ID_EXPORTOPTIONS_TABS, wxPoint(0,0), wxSize(400,420), wxNB_TOP|wxNB_FIXEDWIDTH|wxNB_NOPAGETHEME);
	
	page1 = new ModelExportOptions_General(notebook, ID_EXPORTOPTIONS_PAGE_GENERAL);
	page2 = new ModelExportOptions_Lightwave(notebook, ID_EXPORTOPTIONS_PAGE_LIGHTWAVE);
    page3 = new ModelExportOptions_X3D(notebook, ID_EXPORTOPTIONS_PAGE_X3D);
	page4 = new ModelExportOptions_M3(notebook, ID_EXPORTOPTIONS_PAGE_M3);

	notebook->AddPage(page1, wxT("General"), false, -1);
	notebook->AddPage(page2, wxT("Lightwave"), false);
    notebook->AddPage(page3, wxT("X3D and XHTML"), false);
	notebook->AddPage(page4, wxT("M3"), false);
}

ModelExportOptions_Control::~ModelExportOptions_Control()
{
	//page1->Destroy();
	//page2->Destroy();
    //page3->Destroy();
	//page4->Destroy();
	//notebook->Destroy();
}

void ModelExportOptions_Control::Open()
{
	Show(true);

	page1->Update();
	page2->Update();
    page3->Update();
	page4->Update();
}

void ModelExportOptions_Control::OnClose(wxCloseEvent &event)
{

}



// --== Individual Options ==--


// -= Lightwave Options =-

ModelExportOptions_Lightwave::ModelExportOptions_Lightwave(wxWindow* parent, wxWindowID id)
{
	if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("ModelExportOptions_Lightwave")) == false) {
		wxLogMessage(wxT("GUI Error: ModelExportOptions_Lightwave"));
		return;
	}
	wxFlexGridSizer *top = new wxFlexGridSizer(1);

	chkbox[MEO_CHECK_PRESERVE_LWDIR] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_PRESERVE_DIR, wxT("Build Content Directories"), wxPoint(5,5), wxDefaultSize, 0);
	chkbox[MEO_CHECK_LW_ALWAYSWRITESCENEFILE] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_ALWAYSWRITESCENEFILE, wxT("Always Write Scene File"), wxPoint(180,5), wxDefaultSize, 0);

	chkbox[MEO_CHECK_LW_EXPORTDOODADS] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_EXPORTDOODADS, wxT("Export Doodads"), wxPoint(5,35), wxDefaultSize, 0);
	top->Add(ddextype = new wxComboBox(this, ID_EXPORTOPTIONS_LW_DOODADSAS, wxT("Doodads As"), wxPoint(120,32), wxSize(220, 25), 0, 0, wxCB_READONLY), 1, wxEXPAND, 10);
	chkbox[MEO_CHECK_LW_EXPORTLIGHTS] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_EXPORTLIGHTS, wxT("Export Lights"), wxPoint(5,55), wxDefaultSize, 0);
	chkbox[MEO_CHECK_LW_EXPORTCAMERAS] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_EXPORTCAMERAS, wxT("Export Cameras"), wxPoint(5,75), wxDefaultSize, 0);
	chkbox[MEO_CHECK_LW_EXPORTBONES] = new wxCheckBox(this, ID_EXPORTOPTIONS_LW_EXPORTBONES, wxT("Export Bones"), wxPoint(5,95), wxDefaultSize, 0);
}

void ModelExportOptions_Lightwave::Update()
{
	chkbox[MEO_CHECK_PRESERVE_LWDIR]->SetValue(modelExport_LW_PreserveDir);
	chkbox[MEO_CHECK_LW_ALWAYSWRITESCENEFILE]->SetValue(modelExport_LW_AlwaysWriteSceneFile);
	chkbox[MEO_CHECK_LW_EXPORTLIGHTS]->SetValue(modelExport_LW_ExportLights);
	chkbox[MEO_CHECK_LW_EXPORTDOODADS]->SetValue(modelExport_LW_ExportDoodads);
	chkbox[MEO_CHECK_LW_EXPORTCAMERAS]->SetValue(modelExport_LW_ExportCameras);
	chkbox[MEO_CHECK_LW_EXPORTBONES]->SetValue(modelExport_LW_ExportBones);

	ddextype->Clear();

	ddextype->Append(wxString(wxT("All Doodads as Nulls")));
	ddextype->Append(wxString(wxT("All Doodads as Scene Objects")));
#ifdef _DEBUG
	ddextype->Append(wxString(wxT("Each Doodad Set as a Seperate Layer")));
#endif
	// Uncomment as we're able to do it!
	//ddextype->Append(wxString("All Doodads as a Single Layer"));
	//ddextype->Append(wxString("Doodads as a Single Layer, Per Group"));
	ddextype->SetSelection(modelExport_LW_DoodadsAs);
	ddextype->Enable(modelExport_LW_ExportDoodads);

}

void ModelExportOptions_Lightwave::OnButton(wxCommandEvent &event)
{
	//int id = event.GetId();
	
}

void ModelExportOptions_Lightwave::OnCheck(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id==ID_EXPORTOPTIONS_LW_PRESERVE_DIR){
		modelExport_LW_PreserveDir = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_LW_ALWAYSWRITESCENEFILE){
		modelExport_LW_AlwaysWriteSceneFile = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_LW_EXPORTLIGHTS){
		modelExport_LW_ExportLights = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_LW_EXPORTDOODADS){
		modelExport_LW_ExportDoodads = event.IsChecked();
		ddextype->Enable(event.IsChecked());
	}else if (id==ID_EXPORTOPTIONS_LW_EXPORTCAMERAS){
		modelExport_LW_ExportCameras = event.IsChecked();
	}else if (id==ID_EXPORTOPTIONS_LW_EXPORTBONES){
		modelExport_LW_ExportBones = event.IsChecked();
	}
}

void ModelExportOptions_Lightwave::OnComboBox(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id==ID_EXPORTOPTIONS_LW_DOODADSAS){
		modelExport_LW_DoodadsAs = ddextype->GetCurrentSelection();
	}
}



// -= X3D Options =-

ModelExportOptions_X3D::ModelExportOptions_X3D(wxWindow* parent, wxWindowID id)
{
    if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("ModelExportOptions_X3D")) == false) {
        wxLogMessage(wxT("GUI Error: ModelExportOptions_X3D"));
        return;
    }
    top = new wxFlexGridSizer(1);

    chkbox[MEO_CHECK_EXPORT_ANIMATION] = new wxCheckBox(this, ID_EXPORTOPTIONS_X3D_EXPORT_ANIMATION, wxT("Export keyframe animation"), wxPoint(5,5), wxDefaultSize, 0);
    chkbox[MEO_CHECK_CENTER_MODEL] = new wxCheckBox(this, ID_EXPORTOPTIONS_X3D_CENTER_MODEL, wxT("Add Transform to center model"), wxPoint(160,5), wxDefaultSize, 0);
    
    // disabled for now
    chkbox[MEO_CHECK_EXPORT_ANIMATION]->Enable(false);
}

void ModelExportOptions_X3D::Update()
{
    chkbox[MEO_CHECK_EXPORT_ANIMATION]->SetValue(modelExport_X3D_ExportAnimation);
    chkbox[MEO_CHECK_CENTER_MODEL]->SetValue(modelExport_X3D_CenterModel);
}

void ModelExportOptions_X3D::OnCheck(wxCommandEvent &event)
{
    int id = event.GetId();

    if (id==ID_EXPORTOPTIONS_X3D_EXPORT_ANIMATION){
        modelExport_X3D_ExportAnimation = event.IsChecked();
    }else if (id==ID_EXPORTOPTIONS_X3D_CENTER_MODEL){
        modelExport_X3D_CenterModel = event.IsChecked();
    }
}

// -= M3 Options =-

ModelExportOptions_M3::ModelExportOptions_M3(wxWindow* parent, wxWindowID id)
{
    if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("ModelExportOptions_M3")) == false) {
        wxLogMessage(wxT("GUI Error: ModelExportOptions_M3"));
        return;
    }

	stBoundScale = new wxStaticText(this, wxID_ANY, wxT("Bound Scale"), wxPoint(5, 8));
	tcBoundScale = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(100, 5));
	stSphereScale = new wxStaticText(this, wxID_ANY, wxT("Sphere Scale"), wxPoint(5, 33));
	tcSphereScale = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(100, 30));
	stTexturePath = new wxStaticText(this, wxID_ANY, wxT("Texture Path"), wxPoint(5, 58));
	tcTexturePath = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(100, 55), wxSize(250, 25));
	clbAnimations = new wxCheckListBox(this, ID_EXPORTOPTIONS_M3_ANIMS, wxPoint(5, 85), wxSize(250,165), 0, NULL, 0, wxDefaultValidator, wxT("Animations"));
	stRename = new wxStaticText(this, wxID_ANY, wxT("Rename"), wxPoint(5, 255));
	tcRename = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(100, 255), wxSize(250, 25));
	bApply = new wxButton(this, ID_EXPORTOPTIONS_M3_APPLY, wxT("Apply"), wxPoint(5, 285));
	bReset = new wxButton(this, ID_EXPORTOPTIONS_M3_RESET, wxT("Reset"), wxPoint(95, 285));
	bRename = new wxButton(this, ID_EXPORTOPTIONS_M3_RENAME, wxT("Rename"), wxPoint(185, 285));
}

void ModelExportOptions_M3::Update()
{
	tcBoundScale->SetValue(wxString::Format(wxT("%0.2f"), modelExport_M3_BoundScale));
	tcSphereScale->SetValue(wxString::Format(wxT("%0.2f"), modelExport_M3_SphereScale));
	tcTexturePath->SetValue(modelExport_M3_TexturePath);
	clbAnimations->Clear();
	asAnims.Clear();
	if (g_selModel && g_selModel->animated && g_selModel->anims) {
		Model *m = g_selModel;
		
		for(uint32 i=0; i<m->header.nAnimations; i++) {
			wxString strName;
			try {
				AnimDB::Record rec = animdb.getByAnimID(m->anims[i].animID);
				strName = rec.getString(AnimDB::Name);
			} catch (AnimDB::NotFound) {
				strName = wxT("???");
			}
			asAnims.push_back(strName);

			strName += wxString::Format(wxT(" [%i]"), i);
			clbAnimations->Append(strName);

			// set default actions
			if (modelExport_M3_Anims.size() == 0) { 
				if (m->anims[i].animID == 0 || m->anims[i].animID == 1 || m->anims[i].animID == 5) {
					clbAnimations->Check(clbAnimations->GetCount()-1);
				}
			}
		}
		for(uint32 i=0; i<modelExport_M3_Anims.size(); i++) {
			uint32 j = modelExport_M3_Anims[i];
			if (j >= clbAnimations->GetCount()) // error check
				continue;
			clbAnimations->Check(j);
			asAnims[j] = modelExport_M3_AnimNames[i];
			clbAnimations->SetString(j, clbAnimations->GetString(j)+wxT("  :")+asAnims[j]);
		}
	}
}

void ModelExportOptions_M3::OnChoice(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id == ID_EXPORTOPTIONS_M3_ANIMS) {
		int i = clbAnimations->GetSelection();
		tcRename->SetValue(asAnims[i]);
	}
}

void ModelExportOptions_M3::OnButton(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id == ID_EXPORTOPTIONS_M3_APPLY) {
		modelExport_M3_BoundScale = wxAtof(tcBoundScale->GetValue());
		modelExport_M3_SphereScale = wxAtof(tcSphereScale->GetValue());
		modelExport_M3_TexturePath = tcTexturePath->GetValue();
		modelExport_M3_Anims.clear();
		modelExport_M3_AnimNames.clear();
		for(uint32 i=0; i<clbAnimations->GetCount(); i++) {
			if (!clbAnimations->IsChecked(i))
				continue;
			modelExport_M3_Anims.push_back(i);
			modelExport_M3_AnimNames.push_back(asAnims[i]);
		}
	} else if (id == ID_EXPORTOPTIONS_M3_RESET) {
		//modelExport_M3_BoundScale = 0.5f;
		//modelExport_M3_SphereScale = 0.5f;
		//modelExport_M3_TexturePath = wxT("");
		modelExport_M3_Anims.clear();
		modelExport_M3_AnimNames.clear();
		Update();
	} else if (id == ID_EXPORTOPTIONS_M3_RENAME) {
		int i = clbAnimations->GetSelection();
		if (i > -1) {
			asAnims[i] = tcRename->GetValue();
			wxString strName = clbAnimations->GetString(i).BeforeFirst(']');
			strName.Append(wxT("]"));
			clbAnimations->SetString(i, strName+wxT("  :")+asAnims[i]);
		}
	}
}
