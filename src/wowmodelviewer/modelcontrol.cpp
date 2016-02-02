#include "modelcontrol.h"

#include "ximage.h"

#include <wx/wx.h>
#include <wx/statline.h>
#include <wx/ffile.h>
#include <wx/textctrl.h>

#include "logger/Logger.h"
#include "Attachment.h"
#include "WoWItem.h"


IMPLEMENT_CLASS(ModelControl, wxWindow)

BEGIN_EVENT_TABLE(ModelControl, wxWindow)
  EVT_TREE_ITEM_ACTIVATED(ID_MODEL_GEOSETS, ModelControl::OnList)

	EVT_COMBOBOX(ID_MODEL_NAME, ModelControl::OnCombo)
	EVT_COMBOBOX(ID_MODEL_LOD, ModelControl::OnCombo)

	EVT_CHECKBOX(ID_MODEL_BONES, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_BOUNDS, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_RENDER, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_WIREFRAME, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_PARTICLES, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_TEXTURE, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_PC_REPLACE, ModelControl::OnCheck)

	EVT_COMMAND_SCROLL(ID_MODEL_ALPHA, ModelControl::OnSlider)
	EVT_COMMAND_SCROLL(ID_MODEL_SCALE, ModelControl::OnSlider)
	EVT_TEXT_ENTER(ID_MODEL_SIZE, ModelControl::OnEnter)

	EVT_TEXT_ENTER(ID_MODEL_X, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_Y, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_Z, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_X, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_Y, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_Z, ModelControl::OnEnter)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_START_11, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_MID_11, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_END_11, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_START_12, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_MID_12, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_END_12, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_START_13, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_MID_13, ModelControl::OnColourChange)
	EVT_COLOURPICKER_CHANGED(ID_MODEL_PC_END_13, ModelControl::OnColourChange)
END_EVENT_TABLE()


// ModelName
// LevelOfDetail
// Opacity
// Bones
// Bounding Box
// Render
// Geosets
// Future Additions:
//		- Pos
//		- Rotation
//		- Scale
//		- Attach model

ModelControl::ModelControl(wxWindow* parent, wxWindowID id)
 : wxWindow(parent, id, wxDefaultPosition, wxSize(120, 550), 0,  wxT("ModelControlFrame"))
{
  model = NULL;
  att = NULL;

  LOG_INFO << "Creating Model Control...";

  wxFlexGridSizer *padding = new wxFlexGridSizer(1,1);

  wxFlexGridSizer *top = new wxFlexGridSizer(1,5);
  modelname = new wxComboBox(this, ID_MODEL_NAME);
  top->Add(modelname, 1, wxEXPAND);

  cbLod = new wxComboBox(this, ID_MODEL_LOD);
  top->AddSpacer(5);
  top->Add(new wxStaticText(this, wxID_ANY, wxT("View")), 1, wxEXPAND);
  top->Add(cbLod, 1, wxEXPAND);

  top->AddSpacer(5);
  alpha = new wxSlider(this, ID_MODEL_ALPHA, 100, 0, 100);
  top->Add(new wxStaticText(this, wxID_ANY, wxT("Alpha")), 1, wxEXPAND);
  top->Add(alpha, 1, wxEXPAND);

  wxFlexGridSizer * gbox = new wxFlexGridSizer(2, 5, 5);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("Scale")), 1, wxALIGN_CENTER_VERTICAL);
  txtsize = new wxTextCtrl(this, ID_MODEL_SIZE, wxT("1.00"));
  gbox->Add(txtsize);
  top->Add(gbox, 1, wxEXPAND);
  scale = new wxSlider(this, ID_MODEL_SCALE, 100, 10, 300);
  top->Add(scale, 1, wxEXPAND);

  top->AddSpacer(5);
  gbox = new wxFlexGridSizer(2, 5, 5);
  bones = new wxCheckBox(this, ID_MODEL_BONES, wxT("Bones"));
  wireframe = new wxCheckBox(this, ID_MODEL_WIREFRAME, wxT("Wireframe"));
  gbox->Add(bones);
  gbox->Add(wireframe);
  box = new wxCheckBox(this, ID_MODEL_BOUNDS, wxT("Bounds"));
  texture = new wxCheckBox(this, ID_MODEL_TEXTURE, wxT("Texture"));
  gbox->Add(box);
  gbox->Add(texture);
  render = new wxCheckBox(this, ID_MODEL_RENDER, wxT("Render"));
  particles = new wxCheckBox(this, ID_MODEL_PARTICLES, wxT("Particles"));
  gbox->Add(render);
  gbox->Add(particles);
  top->Add(gbox, 1, wxEXPAND);

  top->AddSpacer(5);
  top->Add(new wxStaticLine(this, wxID_ANY), 1, wxEXPAND);
  top->AddSpacer(5);

  top->Add(new wxStaticText(this, wxID_ANY, wxT("Geosets")), 1, wxEXPAND);
  top->Add(new wxStaticText(this, wxID_ANY, wxT("Double click to toggle on/off")), 1, wxEXPAND);
  clbGeosets = new wxTreeCtrl(this, ID_MODEL_GEOSETS, wxDefaultPosition, wxSize(150,150));
  top->Add(clbGeosets, 1, wxEXPAND);

  top->AddSpacer(5);
  top->Add(new wxStaticLine(this, wxID_ANY), 1, wxEXPAND);
  top->AddSpacer(5);

  // POSITION CONTROLS
  top->Add(new wxStaticText(this, wxID_ANY, wxT("Position")), 1, wxEXPAND);
  gbox = new wxFlexGridSizer(2, 5, 5);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("X")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  txtX = new wxTextCtrl(this, ID_MODEL_X, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(txtX);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("Y")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  txtY = new wxTextCtrl(this, ID_MODEL_Y, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(txtY);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("Z")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  txtZ = new wxTextCtrl(this, ID_MODEL_Z, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(txtZ);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("rX")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  rotX = new wxTextCtrl(this, ID_MODEL_ROT_X, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(rotX);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("rY")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  rotY = new wxTextCtrl(this, ID_MODEL_ROT_Y, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(rotY);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("rZ")), 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  rotZ = new wxTextCtrl(this, ID_MODEL_ROT_Z, wxT("0.0"), wxDefaultPosition, wxSize(75, -1));
  gbox->Add(rotZ);
  top->Add(gbox, 1, wxEXPAND);

  // PARTICLE COLOUR CONTROLS
  top->AddSpacer(5);
  top->Add(new wxStaticLine(this, wxID_ANY), 1, wxEXPAND);
  top->AddSpacer(5);
  particlecolreplace = new wxCheckBox(this, ID_MODEL_PC_REPLACE, wxT("Replace particle colours:"));
  top->Add(particlecolreplace, 1, wxEXPAND);
  PCHint = new wxStaticText(this, wxID_ANY, wxT("(#RRGGBB hexadecimal)"));
  top->Add(PCHint, 1, wxEXPAND);
  NoPC = new wxStaticText(this, wxID_ANY, wxT("* Not enabled on this model *"));
  top->Add(NoPC, 1, wxEXPAND);
  gbox = new wxFlexGridSizer(2, 5, 5);
  PC11SLabel = new wxStaticText(this, wxID_ANY, wxT("ID11 Start"));
  gbox->Add(PC11SLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC11S = new wxColourPickerCtrl(this, ID_MODEL_PC_START_11, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, 10), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC11S);
  PC11MLabel = new wxStaticText(this, wxID_ANY, wxT("Mid"));
  gbox->Add(PC11MLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC11M = new wxColourPickerCtrl(this, ID_MODEL_PC_MID_11, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC11M);
  PC11ELabel = new wxStaticText(this, wxID_ANY, wxT("End"));
  gbox->Add(PC11ELabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC11E = new wxColourPickerCtrl(this, ID_MODEL_PC_END_11, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC11E);
  PC12SLabel = new wxStaticText(this, wxID_ANY, wxT("ID12 Start"));
  gbox->Add(PC12SLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC12S = new wxColourPickerCtrl(this, ID_MODEL_PC_START_12, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC12S);
  PC12MLabel = new wxStaticText(this, wxID_ANY, wxT("Mid"));
  gbox->Add(PC12MLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC12M = new wxColourPickerCtrl(this, ID_MODEL_PC_MID_12, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC12M);
  PC12ELabel = new wxStaticText(this, wxID_ANY, wxT("End"));
  gbox->Add(PC12ELabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC12E = new wxColourPickerCtrl(this, ID_MODEL_PC_END_12, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC12E);
  PC13SLabel = new wxStaticText(this, wxID_ANY, wxT("ID13 Start"));
  gbox->Add(PC13SLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC13S = new wxColourPickerCtrl(this, ID_MODEL_PC_START_13, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC13S);
  PC13MLabel = new wxStaticText(this, wxID_ANY, wxT("Mid"));
  gbox->Add(PC13MLabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC13M = new wxColourPickerCtrl(this, ID_MODEL_PC_MID_13, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC13M);
  PC13ELabel = new wxStaticText(this, wxID_ANY, wxT("End"));
  gbox->Add(PC13ELabel, 1, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT);
  PC13E = new wxColourPickerCtrl(this, ID_MODEL_PC_END_13, wxColour(0,0,0), wxDefaultPosition,
                                  wxSize(55, -1), wxCLRP_USE_TEXTCTRL);
  gbox->Add(PC13E);
  top->Add(gbox, 1);
  top->SetSizeHints(this);
  TogglePCRFields();
  Show(true);
  SetAutoLayout(true);
  padding->Add(top, 1, wxEXPAND|wxLEFT|wxTOP, 10);
  SetSizer(padding);
  Layout();
}

ModelControl::~ModelControl()
{
  modelname->Destroy();
  cbLod->Destroy();
  alpha->Destroy();
  scale->Destroy();
  bones->Destroy();
  box->Destroy();
  render->Destroy();
  wireframe->Destroy();
  texture->Destroy();
  particles->Destroy();
  clbGeosets->Destroy();
  txtX->Destroy();
  txtY->Destroy();
  txtZ->Destroy();
  rotX->Destroy();
  rotY->Destroy();
  rotZ->Destroy();
  PC11S->Destroy();
  PC11M->Destroy();
  PC11E->Destroy();
  PC12S->Destroy();
  PC12M->Destroy();
  PC12E->Destroy();
  PC13S->Destroy();
  PC13M->Destroy();
  PC13E->Destroy();
  NoPC->Destroy();
  PCHint->Destroy();
  PC11SLabel->Destroy();
  PC11MLabel->Destroy();
  PC11ELabel->Destroy();
  PC12SLabel->Destroy();
  PC12MLabel->Destroy();
  PC12ELabel->Destroy();
  PC13SLabel->Destroy();
  PC13MLabel->Destroy();
  PC13ELabel->Destroy();
}

// Iterates through all the models counting and creating a list
void ModelControl::RefreshModel(Attachment *root)
{
	try {
		attachments.clear();

		WoWModel *m = static_cast<WoWModel*>(root->model());
		if (m) {
		//	wxASSERT(m);
			attachments.push_back(root);
			if (!init)
				UpdateModel(root);
			LOG_INFO << "ModelControl Refresh: Adding Model...";
		}
		
		for (std::vector<Attachment *>::iterator it=root->children.begin(); it!=root->children.end(); ++it) {
			//m = NULL;
			m = static_cast<WoWModel*>((*it)->model());
			if (m) {
				attachments.push_back((*it));
				if (!init)
					UpdateModel((*it));
				LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 1...";
			}

			for (std::vector<Attachment *>::iterator it2=(*it)->children.begin(); it2!=(*it)->children.end(); ++it2) {
				m = static_cast<WoWModel*>((*it2)->model());
				if (m) {
					//models.push_back(m);
					attachments.push_back((*it2));
					if (!init)
						UpdateModel((*it2));
					LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 2...";
				}

				for (std::vector<Attachment *>::iterator it3=(*it2)->children.begin(); it3!=(*it2)->children.end(); ++it3) {
					m = static_cast<WoWModel*>((*it3)->model());
					if (m) {
						//models.push_back(m);
						attachments.push_back((*it3));
						if (!init)
							UpdateModel((*it3));
						LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 3...";
					}
				}
			}
		}

		// update combo box with the list of models?
		wxString tmp;
		modelname->Clear();
		for (std::vector<Attachment*>::iterator it=attachments.begin(); it!=attachments.end(); ++it) {
			m = dynamic_cast<WoWModel*>((*it)->model());
			if (m) {
				tmp = m->name().toStdString();
				modelname->Append(tmp.AfterLast(MPQ_SLASH));
			}
		}

		LOG_INFO << "ModelControl Refresh: Found" << attachments.size() << "Models...";

		if (modelname->GetCount() > 0)
			modelname->SetSelection(0);

	} catch( ... ) {
		LOG_ERROR << "Problem occured in ModelControl::RefreshModel(Attachment *)";
	}

}

void ModelControl::UpdateModel(Attachment *a)
{
	if (!a)
		return;

	init = false;

	WoWModel *m = NULL;
	if (a->model())
		m = static_cast<WoWModel*>(a->model());

	if (m) {
		init = true;
		model = m;
		att = a;

		modelname->SetLabel(m->name().toStdString());

		Update();
	}
}

void ModelControl::Update()
{
  if (!model)
    return;

  // Loop through all the views.
  cbLod->Clear();
  if (model->header.nViews == 1)
  {
    cbLod->Append(wxT("1 (Only View)"));
  }
  else if (model->header.nViews == 2)
  {
		cbLod->Append(wxT("1 (Worst)"));
		cbLod->Append(wxT("2 (Best)"));
  }
  else
  {
    cbLod->Append(wxT("1 (Worst)"));
    for (size_t i=1; i<(model->header.nViews-1); i++)
    {
      cbLod->Append(wxString::Format(wxT("%i"), i+1));
    }
    cbLod->Append(wxString::Format(wxT("%i (Best)"), model->header.nViews));
  }
  cbLod->SetSelection(0);

  // Loop through all the geosets.
  wxArrayString geosetItems;
  //geosets->Clear();
  // enum CharGeosets
  wxString meshes[NUM_GEOSETS] = { wxT("Main"), wxT("Facial1"), wxT("Facial2"), wxT("Facial3"), wxT("Braces"),
                                   wxT("Boots"), wxT("Tail"), wxT("Ears"), wxT("Wristbands"), wxT("Kneepads"),
                                   wxT("Pants"), wxT("Pants2"), wxT("Tabard"), wxT("Trousers"), wxT("Tabard2"),
                                   wxT("Cape"), wxT("Feet"), wxT("Eyeglows"), wxT("Belt"), wxT("Tail"), wxT("Feet") };

  std::map <size_t,wxTreeItemId> geosetGroupsMap;
  clbGeosets->DeleteAllItems();
  clbGeosets->SetWindowStyle(wxTR_HIDE_ROOT);
  wxTreeItemId root = clbGeosets->AddRoot("Model Geosets");
  for (size_t i=0; i<model->geosets.size(); i++)
  {
    /*  size_t mesh = model->geosets[i].id / 100;
        if (mesh < WXSIZEOF(meshes) && meshes[mesh] != wxEmptyString)
          geosetItems.Add(wxString::Format(wxT("%i [%s, %i, %i]"), i, meshes[mesh].c_str(), model->geosets[i].id % 100, model->geosets[i].id), 1);
        else
          geosetItems.Add(wxString::Format(wxT("%i [%i, %i, %i]"), i, mesh, (model->geosets[i].id % 100), model->geosets[i].id ), 1);
    */
    size_t mesh = model->geosets[i].id / 100;
    if(geosetGroupsMap.find(mesh) == geosetGroupsMap.end())
    {
      if (mesh < WXSIZEOF(meshes) && meshes[mesh] != wxEmptyString)
        geosetGroupsMap[mesh] = clbGeosets->AppendItem(root, meshes[mesh]);
	 else
	   geosetGroupsMap[mesh] = clbGeosets->AppendItem(root, wxString::Format(wxT("%i"),mesh));
    }
    GeosetTreeItemData * data = new GeosetTreeItemData();
    data->geosetId = i;
    wxTreeItemId item = clbGeosets->AppendItem(geosetGroupsMap[mesh], wxString::Format(wxT("%i [%i, %i, %i]"), i, mesh, (model->geosets[i].id % 100), model->geosets[i].id ),-1,-1,data);
    if (model->showGeosets[i] == true)
       clbGeosets->SetItemBackgroundColour(item, *wxGREEN);
  }

  //for (size_t i=0; i<model->geosets.size(); i++)
  //  clbGeosets->Check((unsigned int)i, model->showGeosets[i]);

  bones->SetValue(model->showBones);
  box->SetValue(model->showBounds);
  render->SetValue(model->showModel);
  wireframe->SetValue(model->showWireframe);
  particles->SetValue(model->showParticles);
  texture->SetValue(model->showTexture);

  alpha->SetValue(int(model->alpha * 100));
  scale->SetValue(att->scale*100);

  txtX->SetValue(wxString::Format(wxT("%f"), model->pos.x));
  txtY->SetValue(wxString::Format(wxT("%f"), model->pos.y));
  txtZ->SetValue(wxString::Format(wxT("%f"), model->pos.z));
  rotX->SetValue(wxString::Format(wxT("%f"), model->rot.x));
  rotY->SetValue(wxString::Format(wxT("%f"), model->rot.y));
  rotZ->SetValue(wxString::Format(wxT("%f"), model->rot.z));
  txtsize->SetValue(wxString::Format(wxT("%.2f"), att->scale));

  if (modelPCRSaves.find(model->modelname) != modelPCRSaves.end())
  {
    pcr = modelPCRSaves[model->modelname];
  }
  else
  {
    pcr.clear();
    particleColorSet cols11, cols12, cols13;
    cols11 = { Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0) };
    cols12 = { Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0) };
    cols13 = { Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0), Vec4D(0.0, 0.0, 0.0, 1.0) };
    pcr = { cols11, cols12, cols13 };
  }
  particlecolreplace->SetValue(false);
  PC11S->SetColour(wxColour(pcr[0][0][0]*255, pcr[0][0][1]*255, pcr[0][0][2]*255));
  PC11M->SetColour(wxColour(pcr[0][1][0]*255, pcr[0][1][1]*255, pcr[0][1][2]*255));
  PC11E->SetColour(wxColour(pcr[0][2][0]*255, pcr[0][2][1]*255, pcr[0][2][2]*255));
  PC12S->SetColour(wxColour(pcr[1][0][0]*255, pcr[1][0][1]*255, pcr[1][0][2]*255));
  PC12M->SetColour(wxColour(pcr[1][1][0]*255, pcr[1][1][1]*255, pcr[1][1][2]*255));
  PC12E->SetColour(wxColour(pcr[1][2][0]*255, pcr[1][2][1]*255, pcr[1][2][2]*255));
  PC13S->SetColour(wxColour(pcr[2][0][0]*255, pcr[2][0][1]*255, pcr[2][0][2]*255));
  PC13M->SetColour(wxColour(pcr[2][1][0]*255, pcr[2][1][1]*255, pcr[2][1][2]*255));
  PC13E->SetColour(wxColour(pcr[2][2][0]*255, pcr[2][2][1]*255, pcr[2][2][2]*255));
  UpdatePCRTexts();
  TogglePCRFields();
}

void ModelControl::TogglePCRFields()
{
  bool show11, show12, show13;

  if (model)
  {
    std::vector<uint> pcrIDs = model->replacableParticleColorIDs;
    show11 = (std::find(pcrIDs.begin(), pcrIDs.end(), 11) != pcrIDs.end());
    show12 = (std::find(pcrIDs.begin(), pcrIDs.end(), 12) != pcrIDs.end());
    show13 = (std::find(pcrIDs.begin(), pcrIDs.end(), 13) != pcrIDs.end());
  }
  else
  {
    show11 = false;
    show12 = false;
    show13 = false;
  }
  PCHint->Show(show11 || show12 || show13);
  NoPC->Show(!(show11 || show12 || show13));
  PC11SLabel->Show(show11);
  PC11MLabel->Show(show11);
  PC11ELabel->Show(show11);
  PC12SLabel->Show(show12);
  PC12MLabel->Show(show12);
  PC12ELabel->Show(show12);
  PC13SLabel->Show(show13);
  PC13MLabel->Show(show13);
  PC13ELabel->Show(show13);
  PC11S->Show(show11);
  PC11M->Show(show11);
  PC11E->Show(show11);
  PC12S->Show(show12);
  PC12M->Show(show12);
  PC12E->Show(show12);
  PC13S->Show(show13);
  PC13M->Show(show13);
  PC13E->Show(show13);
  Layout();
}

void ModelControl::OnCheck(wxCommandEvent &event)
{
  if (!init || !model)
    return;

  int id = event.GetId();
  bool check = event.IsChecked();
  switch (id)
  {
    case ID_MODEL_BONES :
          model->showBones = check;
          break;
    case ID_MODEL_BOUNDS :
          model->showBounds = check;
          break;
    case ID_MODEL_RENDER :
          model->showModel = check;
          break;
    case ID_MODEL_WIREFRAME :
          model->showWireframe = check;
          break;
    case ID_MODEL_PARTICLES :
          model->showParticles = check;
          break;
    case ID_MODEL_TEXTURE :
          model->showTexture = check;
          break;
    case ID_MODEL_PC_REPLACE :
          if (check)
          {
            model->particleColorReplacements = pcr;
            model->replaceParticleColors = true;
          }
          else
            animControl->SetSkin(-1);  // reset to the current skin to use default particles
          break;
  }
}

void ModelControl::OnCombo(wxCommandEvent &event)
{
	if (!init)
		return;

	int id = event.GetId();

	if (id == ID_MODEL_LOD) {
//		int value = event.GetInt();
//
//		MPQFile f(model->name);
//		if (f.isEof() || (f.getSize() < sizeof(ModelHeader))) {
//			LOG_ERROR << "Unable to open MPQFile:" << model->name.c_str();
//			f.close();
//			return;
//		}
//
//		model->showModel = false;
//		model->setLOD(&f, value);
//		model->showModel = true;
//
//		/*
//		for (size_t i=0; i<model->geosets.size(); i++) {
//			int id = model->geosets[i].id;
//			model->showGeosets[i] = (id==0);
//		}
//
//		cc->RefreshModel();
//		*/
//
//		f.close();
	} else if (id == ID_MODEL_NAME) {
		/* Alfred 2009/07/16 fix crash, remember CurrentSelection before UpdateModel() */
		int CurrentSelection = modelname->GetCurrentSelection();
		if (CurrentSelection < (int)attachments.size()) {
			UpdateModel(attachments[CurrentSelection]);
			att = attachments[CurrentSelection];
			model = static_cast<WoWModel*>(attachments[CurrentSelection]->model());
			
			animControl->UpdateModel(model);
			modelname->SetSelection(CurrentSelection);
		}
	}
}

void ModelControl::OnList(wxTreeEvent &event)
{
  if (!init || !model)
    return;

  int id = event.GetId();

  if (id == ID_MODEL_GEOSETS)
  {
    wxTreeItemId curItem = clbGeosets->GetSelection();
    GeosetTreeItemData * data = (GeosetTreeItemData *)clbGeosets->GetItemData(curItem);
    if(data)
    {
      size_t geosetIndex = data->geosetId;
      model->showGeosets[geosetIndex] = !model->showGeosets[geosetIndex];
      clbGeosets->SetItemBackgroundColour(curItem,
                    (model->showGeosets[geosetIndex])?*wxGREEN:*wxWHITE);
    }
    else
      std::cout << "data is null !!!" << std::endl;
    clbGeosets->Layout();
    clbGeosets->Fit();
    Layout();
    Fit();
  }
}

void ModelControl::OnSlider(wxScrollEvent &event)
{
	if (!init || !model)
		return;

	int id = event.GetId();
	if (id == ID_MODEL_ALPHA) {
		model->alpha = event.GetInt() / 100.0f;
	} else if (id == ID_MODEL_SCALE) {
		att->scale = event.GetInt() / 100.0f;
		txtsize->SetValue(wxString::Format(wxT("%.2f"), att->scale));
	}
}

Vec4D ModelControl::fromColWidget(wxColour col)
{
  return Vec4D((float)col.Red()/255.0f, (float)col.Green()/255.0f, (float)col.Blue()/255.0f, 1.0);
}

void ModelControl::OnEnter(wxCommandEvent &event)
{
  if (!init || !model)
    return;

  int eventID = event.GetId();

  if (eventID == ID_MODEL_X || eventID == ID_MODEL_Y || eventID == ID_MODEL_Z ||
      eventID == ID_MODEL_ROT_X || eventID == ID_MODEL_ROT_Y || eventID == ID_MODEL_ROT_Z)
  {
    model->pos.x = wxAtof(txtX->GetValue());
    model->pos.y = wxAtof(txtY->GetValue());
    model->pos.z = wxAtof(txtZ->GetValue());
    model->rot.x = wxAtof(rotX->GetValue());
    model->rot.y = wxAtof(rotY->GetValue());
    model->rot.z = wxAtof(rotZ->GetValue());
  }

  if (eventID == ID_MODEL_SIZE)
  {
    att->scale = wxAtof(txtsize->GetValue());
    scale->SetValue(wxAtoi(txtsize->GetValue())*100);
  }
}


void ModelControl::UpdatePCRTexts()
{
  UpdatePCRText(PC11S);
  UpdatePCRText(PC11M);
  UpdatePCRText(PC11E);
  UpdatePCRText(PC12S);
  UpdatePCRText(PC12M);
  UpdatePCRText(PC12E);
  UpdatePCRText(PC13S);
  UpdatePCRText(PC13M);
  UpdatePCRText(PC13E);
}


void ModelControl::UpdatePCRText(wxColourPickerCtrl *cpc)
{
  wxTextCtrl *txtCtrl;

  if (!cpc)
    return;
  txtCtrl = cpc->GetTextCtrl();
  if (txtCtrl)
    txtCtrl->SetValue(cpc->GetColour().GetAsString(wxC2S_HTML_SYNTAX));
}

void ModelControl::OnColourChange(wxColourPickerEvent &event)
{
  if (!init)
    return;

  Vec4D col = fromColWidget(event.GetColour());
  wxColourPickerCtrl *cpc;

  switch (event.GetId())
  {
    case ID_MODEL_PC_START_11 :
          pcr[0][0] = col;
          cpc = PC11S;
          break;
    case ID_MODEL_PC_MID_11 :
          pcr[0][1] = col;
          cpc = PC11M;
          break;
    case ID_MODEL_PC_END_11 :
          pcr[0][2] = col;
          cpc = PC11E;
          break;
    case ID_MODEL_PC_START_12 :
          pcr[1][0] = col;
          cpc = PC12S;
          break;
    case ID_MODEL_PC_MID_12 :
          pcr[1][1] = col;
          cpc = PC12M;
          break;
    case ID_MODEL_PC_END_12 :
          pcr[1][2] = col;
          cpc = PC12E;
          break;
    case ID_MODEL_PC_START_13 :
          pcr[2][0] = col;
          cpc = PC13S;
          break;
    case ID_MODEL_PC_MID_13 :
          pcr[2][1] = col;
          cpc = PC13M;
          break;
    case ID_MODEL_PC_END_13 :
          pcr[2][2] = col;
          cpc = PC13E;
          break;
  }

  if (cpc)
    UpdatePCRText(cpc);
  if (model)
  {
    if (particlecolreplace->GetValue() == true)
      model->particleColorReplacements = pcr;
    modelPCRSaves[model->modelname] = pcr;
  }
}

bool ModelControl::IsReplacingParticleColors()
{
  return particlecolreplace->GetValue();
}



/**************************************************************************
  * ScrWindow
  *************************************************************************/

ScrWindow::ScrWindow(const wxString& title)
       : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(512, 512))
{
	wxImage::AddHandler(new wxPNGHandler);
	sw = new wxScrolledWindow(this);

	wxBitmap bmp(title, wxBITMAP_TYPE_PNG);
	sb = new wxStaticBitmap(sw, -1, bmp);

	int width = bmp.GetWidth();
	int height = bmp.GetHeight();

	CreateStatusBar();
	wxString sbarText;
	sbarText.Printf(wxT("%ix%i"), width, height);
	SetStatusText(sbarText);

	sw->SetScrollbars(10, 10, width/10, height/10);
//	sw->Scroll(50,10);

	Center();
}

ScrWindow::~ScrWindow()
{
	sb->Destroy();
	sw->Destroy();
}
