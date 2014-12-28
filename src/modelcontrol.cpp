#include "modelcontrol.h"

#include "Attachment.h"

#include "logger/Logger.h"

#include "CxImage/ximage.h"

#include <wx/wx.h>
#include <wx/ffile.h>
#include <wx/textctrl.h>

IMPLEMENT_CLASS(ModelControl, wxWindow)

BEGIN_EVENT_TABLE(ModelControl, wxWindow)
  EVT_TREE_KEY_DOWN(ID_MODEL_GEOSETS, ModelControl::OnList)

	EVT_COMBOBOX(ID_MODEL_NAME, ModelControl::OnCombo)
	EVT_COMBOBOX(ID_MODEL_LOD, ModelControl::OnCombo)

	EVT_CHECKBOX(ID_MODEL_BONES, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_BOUNDS, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_RENDER, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_WIREFRAME, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_PARTICLES, ModelControl::OnCheck)
	EVT_CHECKBOX(ID_MODEL_TEXTURE, ModelControl::OnCheck)

	EVT_COMMAND_SCROLL(ID_MODEL_ALPHA, ModelControl::OnSlider)
	EVT_COMMAND_SCROLL(ID_MODEL_SCALE, ModelControl::OnSlider)
	EVT_TEXT_ENTER(ID_MODEL_SIZE, ModelControl::OnEnter)

	EVT_TEXT_ENTER(ID_MODEL_X, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_Y, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_Z, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_X, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_Y, ModelControl::OnEnter)
	EVT_TEXT_ENTER(ID_MODEL_ROT_Z, ModelControl::OnEnter)
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
 : wxWindow(parent, id, wxDefaultPosition, wxSize(120, 500), 0,  wxT("ModelControlFrame"))
{
	model = NULL;
	att = NULL;

	wxLogMessage(wxT("Creating Model Control..."));

	wxFlexGridSizer *top = new wxFlexGridSizer(1);
	modelname = new wxComboBox(this, ID_MODEL_NAME);//, wxEmptyString, wxPoint(5,5), wxSize(150,16), 0, NULL, wxCB_READONLY);
	top->Add(modelname, 1, wxEXPAND);

	lblLod = new wxStaticText(this, wxID_ANY, wxT("View"));//, wxPoint(5,25), wxDefaultSize);
	cbLod = new wxComboBox(this, ID_MODEL_LOD); //, wxEmptyString, wxPoint(5,40), wxSize(120,16), 0, NULL, wxCB_READONLY, wxDefaultValidator, wxT("LOD")); //|wxCB_SORT); //wxPoint(66,10)
	top->Add(lblLod, 1, wxEXPAND);
	top->Add(cbLod, 1, wxEXPAND);

	lblAlpha = new wxStaticText(this, wxID_ANY, wxT("Alpha"));//, wxPoint(5,65), wxDefaultSize);
	alpha = new wxSlider(this, ID_MODEL_ALPHA, 100, 0, 100);//, wxPoint(45, 65), wxSize(110, 30), wxSL_HORIZONTAL);
	top->Add(lblAlpha, 1, wxEXPAND);
	top->Add(alpha, 1, wxEXPAND);

	lblScale = new wxStaticText(this, wxID_ANY, wxT("Scale"));//, wxPoint(5,90), wxDefaultSize);
	scale = new wxSlider(this, ID_MODEL_SCALE, 100, 10, 300);//, wxPoint(45, 90), wxSize(110, 30), wxSL_HORIZONTAL);
	top->Add(lblScale, 1, wxEXPAND);
	top->Add(scale, 1, wxEXPAND);

	txtsize = new wxTextCtrl(this, ID_MODEL_SIZE, wxT("1.0"));//, wxPoint(30, 115), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	top->Add(txtsize, 1, wxEXPAND);

	wxGridSizer * gbox = new wxGridSizer(2,3);
	bones = new wxCheckBox(this, ID_MODEL_BONES, wxT("Bones"));//, wxPoint(5, 140), wxDefaultSize);
	wireframe = new wxCheckBox(this, ID_MODEL_WIREFRAME, wxT("Wireframe"));//, wxPoint(75, 140), wxDefaultSize);
	gbox->Add(bones);
	gbox->Add(wireframe);
	box = new wxCheckBox(this, ID_MODEL_BOUNDS, wxT("Bounds"));//, wxPoint(5, 160), wxDefaultSize);
	texture = new wxCheckBox(this, ID_MODEL_TEXTURE, wxT("Texture"));//, wxPoint(75, 160), wxDefaultSize);
	gbox->Add(box);
	gbox->Add(texture);
	render = new wxCheckBox(this, ID_MODEL_RENDER, wxT("Render"));//, wxPoint(5, 180), wxDefaultSize);
	particles = new wxCheckBox(this, ID_MODEL_PARTICLES, wxT("Particles"));//, wxPoint(75, 180), wxDefaultSize);
	gbox->Add(render);
	gbox->Add(particles);
	top->Add(gbox, 1, wxEXPAND);

	lblGeosets = new wxStaticText(this, wxID_ANY, wxT("Show Geosets"));//, wxPoint(5,200), wxDefaultSize);
	top->Add(lblGeosets, 1, wxEXPAND);

	//clbGeosets = new wxCheckListBox(this, ID_MODEL_GEOSETS, wxPoint(5, 215), wxSize(150,120), 0, NULL, 0, wxDefaultValidator, wxT("GeosetsList"));
	clbGeosets = new wxTreeCtrl (this, ID_MODEL_GEOSETS, wxDefaultPosition, wxSize(150,220) );//, wxPoint(5, 215), wxSize(150,220));
	top->Add(clbGeosets, 1, wxEXPAND);

	wxBoxSizer * hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("X")));
	txtX = new wxTextCtrl(this, ID_MODEL_X, wxT("0.0"));//, wxPoint(30,445), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(txtX);
	top->Add(hbox, 1, wxEXPAND);
	hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("Y")));
	txtY = new wxTextCtrl(this, ID_MODEL_Y, wxT("0.0"));//, wxPoint(30,465), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(txtY);
	top->Add(hbox, 1, wxEXPAND);
	hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("Z")));
	txtZ = new wxTextCtrl(this, ID_MODEL_Z, wxT("0.0"));//, wxPoint(30,485), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(txtZ);
	top->Add(hbox, 1, wxEXPAND);
	hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("rX")));
	rotX = new wxTextCtrl(this, ID_MODEL_ROT_X, wxT("0.0"));//, wxPoint(30,505), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(rotX);
	top->Add(hbox, 1, wxEXPAND);
	hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("rY")));
	rotY = new wxTextCtrl(this, ID_MODEL_ROT_Y, wxT("0.0"));//, wxPoint(30,525), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(rotY);
	top->Add(hbox, 1, wxEXPAND);
	hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(new wxStaticText(this, wxID_ANY, wxT("rZ")));
	rotZ = new wxTextCtrl(this, ID_MODEL_ROT_Z, wxT("0.0"));//, wxPoint(30,545), wxDefaultSize, wxTE_PROCESS_ENTER, wxDefaultValidator);
	hbox->Add(rotZ);
	top->Add(hbox, 1, wxEXPAND);

	top->SetSizeHints(this);
	Show(true);
	SetAutoLayout(true);
	SetSizer(top);
	Layout();
	//vbox->SetSizeHints(this);

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
	txtsize->Destroy();
}

// Iterates through all the models counting and creating a list
void ModelControl::RefreshModel(Attachment *root)
{
	try {
		attachments.clear();

		WoWModel *m = static_cast<WoWModel*>(root->model);
		if (m) {
		//	wxASSERT(m);
			attachments.push_back(root);
			if (!init)
				UpdateModel(root);
			wxLogMessage(wxT("ModelControl Refresh: Adding Model..."));
		}
		
		for (std::vector<Attachment *>::iterator it=root->children.begin(); it!=root->children.end(); ++it) {
			//m = NULL;
			m = static_cast<WoWModel*>((*it)->model);
			if (m) {
				attachments.push_back((*it));
				if (!init)
					UpdateModel((*it));
				wxLogMessage(wxT("ModelControl Refresh: Adding Attachment Level 1..."));
			}

			for (std::vector<Attachment *>::iterator it2=(*it)->children.begin(); it2!=(*it)->children.end(); ++it2) {
				m = static_cast<WoWModel*>((*it2)->model);
				if (m) {
					//models.push_back(m);
					attachments.push_back((*it2));
					if (!init)
						UpdateModel((*it2));
					wxLogMessage(wxT("ModelControl Refresh: Adding Attachment Level 2..."));
				}

				for (std::vector<Attachment *>::iterator it3=(*it2)->children.begin(); it3!=(*it2)->children.end(); ++it3) {
					m = static_cast<WoWModel*>((*it3)->model);
					if (m) {
						//models.push_back(m);
						attachments.push_back((*it3));
						if (!init)
							UpdateModel((*it3));
						wxLogMessage(wxT("ModelControl Refresh: Adding Attachment Level 3..."));
					}
				}
			}
		}
		
		// update combo box with the list of models?
		wxString tmp;
		modelname->Clear();
		for (std::vector<Attachment*>::iterator it=attachments.begin(); it!=attachments.end(); ++it) {
			m = static_cast<WoWModel*>((*it)->model);
			if (m) {
				tmp = m->name;
				modelname->Append(tmp.AfterLast(MPQ_SLASH));
			}
		}
		wxLogMessage(wxT("ModelControl Refresh: Found %i Models..."),attachments.size());

		if (modelname->GetCount() > 0)
			modelname->SetSelection(0);

	} catch( ... ) {
		wxLogMessage(wxT("Error: Problem occured in ModelControl::RefreshModel(Attachment *)"));
	}

}

void ModelControl::UpdateModel(Attachment *a)
{
	if (!a)
		return;

	init = false;

	WoWModel *m = NULL;
	if (a->model)
		m = static_cast<WoWModel*>(a->model);

	if (m) {
		init = true;
		model = m;
		att = a;

		modelname->SetLabel(m->name);

		Update();
	}
}

void ModelControl::Update()
{
	if (!model)
		return;

	// Loop through all the views.
	cbLod->Clear();
	if (model->header.nViews == 1) {
		cbLod->Append(wxT("1 (Only View)"));
	} else if (model->header.nViews == 2) {
		cbLod->Append(wxT("1 (Worst)"));
		cbLod->Append(wxT("2 (Best)"));
	} else {
		cbLod->Append(wxT("1 (Worst)"));
		for (size_t i=1; i<(model->header.nViews-1); i++) {
			cbLod->Append(wxString::Format(wxT("%i"), i+1));
		}
		cbLod->Append(wxString::Format(wxT("%i (Best)"), model->header.nViews));
	}
	cbLod->SetSelection(0);

	// Loop through all the geosets.
	wxArrayString geosetItems;
	//geosets->Clear();
	// enum CharGeosets
	wxString meshes[NUM_GEOSETS] = {wxT("Hairstyles"), wxT("Facial1"), wxT("Facial2"), wxT("Facial3"), wxT("Braces"),
		wxT("Boots"), wxEmptyString, wxT("Ears"), wxT("Wristbands"),  wxT("Kneepads"),
		 wxT("Pants"), wxT("Pants2"), wxT("Tarbard"), wxT("Trousers"), wxT("Tarbard2"),
		  wxT("Cape"), wxEmptyString, wxT("Eyeglows"), wxT("Belt"), wxT("Tail") };

	std::map <size_t,wxTreeItemId> geosetGroupsMap;
	clbGeosets->SetWindowStyle(wxTR_HIDE_ROOT);
	wxTreeItemId root = clbGeosets->AddRoot("Model Geosets");
	for (size_t i=0; i<model->geosets.size(); i++)
	{
	/*	size_t mesh = model->geosets[i].id / 100;
		if (mesh < WXSIZEOF(meshes) && meshes[mesh] != wxEmptyString)
			geosetItems.Add(wxString::Format(wxT("%i [%s, %i, %i]"), i, meshes[mesh].c_str(), model->geosets[i].id % 100, model->geosets[i].id), 1);
		else
		{
			geosetItems.Add(wxString::Format(wxT("%i [%i, %i, %i]"), i, mesh, (model->geosets[i].id % 100), model->geosets[i].id ), 1);
		}
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
	  wxTreeItemId item = clbGeosets->AppendItem(geosetGroupsMap[mesh], wxString::Format(wxT("%i"), model->geosets[i].id % 100),-1,-1,data);
	  if(model->showGeosets[i] == true)
	    clbGeosets->SetItemBackgroundColour(item, *wxGREEN);
	}

//	for (size_t i=0; i<model->geosets.size(); i++) {
//		clbGeosets->Check((unsigned int)i, model->showGeosets[i]);
//	}

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
	txtsize->SetValue(wxString::Format(wxT("%f"), att->scale));
}

void ModelControl::OnCheck(wxCommandEvent &event)
{
	if (!init || !model)
		return;

	int id = event.GetId();

	if (id == ID_MODEL_BONES) {
		model->showBones = event.IsChecked();
	} else if (id == ID_MODEL_BOUNDS) {
		model->showBounds = event.IsChecked();
	} else if (id == ID_MODEL_RENDER) {
		model->showModel = event.IsChecked();
	} else if (id == ID_MODEL_WIREFRAME) {
		model->showWireframe = event.IsChecked();
	} else if (id == ID_MODEL_PARTICLES) {
		model->showParticles = event.IsChecked();
	} else if (id == ID_MODEL_TEXTURE) {
		model->showTexture = event.IsChecked();
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
//			wxLogMessage(wxT("ERROR - unable to open MPQFile: [%s]"), model->name.c_str());
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
			model = static_cast<WoWModel*>(attachments[CurrentSelection]->model);
			
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
	    clbGeosets->SetItemBackgroundColour(curItem, (model->showGeosets[geosetIndex])?*wxGREEN:*wxWHITE);
	  }
	  else
	  {
	    std::cout << "data is null !!!" << std::endl;
	  }
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
		txtsize->SetValue(wxString::Format(wxT("%f"), att->scale));
	}
}

void ModelControl::OnEnter(wxCommandEvent &event)
{
	if (!init || !model)
		return;

	model->pos.x = wxAtof(txtX->GetValue());
	model->pos.y = wxAtof(txtY->GetValue());
	model->pos.z = wxAtof(txtZ->GetValue());
	model->rot.x = wxAtof(rotX->GetValue());
	model->rot.y = wxAtof(rotY->GetValue());
	model->rot.z = wxAtof(rotZ->GetValue());
	att->scale = wxAtof(txtsize->GetValue());
	if (event.GetId() == ID_MODEL_SIZE){
		scale->SetValue(wxAtoi(txtsize->GetValue())*100);
	}
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
