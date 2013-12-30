#include "arrows.h"

#include "Attachment.h"
#include "enums.h"
#include "modelexport.h"
#include "logger/Logger.h"

IMPLEMENT_CLASS(ArrowControl, wxWindow)

BEGIN_EVENT_TABLE(ArrowControl, wxWindow)
	EVT_CLOSE(ArrowControl::OnClose)

	EVT_BUTTON(ID_ARROW_ATTACH, ArrowControl::OnButton)
	EVT_BUTTON(ID_ARROW_CLEAR, ArrowControl::OnButton)

	EVT_COMBOBOX(ID_ARROW_MODEL, ArrowControl::OnCombo)
	EVT_COMBOBOX(ID_ARROW_JOINT, ArrowControl::OnCombo)

	EVT_SLIDER(ID_ARROW_SCALE, ArrowControl::OnSlider)
	EVT_SLIDER(ID_ARROW_ROTATION, ArrowControl::OnSlider)
	EVT_SLIDER(ID_ARROW_POSITION, ArrowControl::OnSlider)
END_EVENT_TABLE()


ArrowControl::ArrowControl(wxWindow* parent, wxWindowID id, const wxPoint& pos, Attachment *att)
{
	atts.clear();
	curAtt = NULL;
	charAtt = att;

	if(Create(parent, id, pos, wxSize(140, 300), 0, wxT("ArrowControlFrame")) == false) {
		wxMessageBox(wxT("Failed to create a window for our ArrowControl!"), wxT("ERROR"));
		wxLogMessage(wxT("ERROR - Failed to create a window for our ArrowControl!"));
		return;
	}

	// modelexport.h Attach_Names
	wxArrayString locs;
	for(size_t i=0; i<WXSIZEOF(Attach_Names); i++) {
		locs.Add(wxString::Format(_("%d "), i) + Attach_Names[i]);
	}

	joint = new wxComboBox(this, ID_ARROW_JOINT, locs[0], wxPoint(5,5), wxSize(130,20), locs, wxCB_READONLY);
	const wxString models[] = {wxT("arrowacidflight_01.m2"), wxT("arrowfireflight_01.m2"), wxT("arrowflight_01.m2"), wxT("arrowiceflight_01.m2"), wxT("arrowmagicflight_01.m2")};
	model = new wxComboBox(this, ID_ARROW_MODEL, models[0], wxPoint(5,30), wxSize(130,20), WXSIZEOF(models), models, wxCB_READONLY);
	//tex = new wxComboBox(this, ID_ARROW_TEXTURE,wxEmptyString, wxPoint(55,5), wxSize(100,20), 0, NULL, wxCB_READONLY);
	
	attach = new wxButton(this, ID_ARROW_ATTACH, wxT("Attach"), wxPoint(10,55), wxSize(55,20));
	clear = new wxButton(this, ID_ARROW_CLEAR, wxT("Clear All"), wxPoint(80,55), wxSize(55,20));

	rot = new wxSlider(this, ID_ARROW_ROTATION, 18,0,36, wxPoint(5, 85), wxSize(130,38), wxSL_HORIZONTAL|wxSL_LABELS );
	scale = new wxSlider(this, ID_ARROW_SCALE, 5,0,20, wxPoint(5, 125), wxSize(130,38), wxSL_HORIZONTAL|wxSL_LABELS );
	position = new wxSlider(this, ID_ARROW_POSITION, 0,-50,50, wxPoint(5, 165), wxSize(130,38), wxSL_HORIZONTAL|wxSL_LABELS );
}

ArrowControl::~ArrowControl()
{
	for (size_t i=0; i<atts.size(); i++) {
		curAtt = atts[i];
		wxDELETE(atts[i]->model);
		wxDELETE(curAtt);
	}
	atts.clear();
}

void ArrowControl::OnButton(wxCommandEvent &event)
{
	int id = event.GetId();

	if(id == ID_ARROW_ATTACH) {
		wxString mp = wxT("Item\\ObjectComponents\\Ammo\\Arrow_A_01Brown.blp");
		
		curAtt = charAtt->addChild(wxT("Item\\Objectcomponents\\ammo\\") + model->GetStringSelection(), joint->GetSelection(), -1);
		atts.push_back(curAtt);
		
		GLuint tex = texturemanager.add(mp);
		Model *m = static_cast<Model*>(curAtt->model);
		m->replaceTextures[TEXTURE_CAPE] = tex;

		curAtt->scale = 0.5f;
		curAtt->rot = 180.0f;

		rot->SetValue(18);
		scale->SetValue(5);
		position->SetValue(0);

	} else if (id == ID_ARROW_CLEAR) {
		curAtt = NULL;
		for (size_t i=0; i<atts.size(); i++) {
			wxDELETE(atts[i]->model);
		}
		atts.clear();
	}

}

void ArrowControl::OnCombo(wxCommandEvent &)
{


}

void ArrowControl::OnSlider(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id == ID_ARROW_ROTATION) {
		curAtt->rot = event.GetInt() * 10.0f;
	} else if ( id == ID_ARROW_SCALE) {
		curAtt->scale = event.GetInt() / 10.0f;
	} else if ( id == ID_ARROW_POSITION) {
		curAtt->pos.x = event.GetInt() / 10.0f;
	}
}

void ArrowControl::OnClose(wxCloseEvent &)
{
	Show(false);
}

