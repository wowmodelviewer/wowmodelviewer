#include "charcontrol.h"

// our headers
#include "Attachment.h"
#include "globalvars.h"
#include "itemselection.h"
#include "modelviewer.h"
#include "util.h"

#include "logger/Logger.h"

#include "CASCFolder.h"
#include "GameDatabase.h"
#include "RaceInfos.h"

#include <wx/txtstrm.h>

CharSlots slotOrder[] = {
	CS_SHIRT,
	CS_HEAD,
	CS_NECK,
	CS_SHOULDER,
	CS_PANTS,
	CS_BOOTS,
	CS_CHEST,
	CS_TABARD,
	CS_BELT,
	CS_BRACERS,
	CS_GLOVES,
	CS_HAND_RIGHT,
	CS_HAND_LEFT,
	CS_CAPE,
	CS_QUIVER
};

CharSlots slotOrderWithRobe[] = {
	CS_SHIRT,
	CS_HEAD,
	CS_NECK,
	CS_SHOULDER,
	CS_BOOTS,
	CS_PANTS,
	CS_BRACERS,
	CS_CHEST,
	CS_GLOVES,
	CS_TABARD,
	CS_BELT,
	CS_HAND_RIGHT,
	CS_HAND_LEFT,
	CS_CAPE,
	CS_QUIVER
};

wxString regionPaths[NUM_REGIONS] =
{
	wxEmptyString,
	wxT("Item\\TextureComponents\\ArmUpperTexture\\"),
	wxT("Item\\TextureComponents\\ArmLowerTexture\\"),
	wxT("Item\\TextureComponents\\HandTexture\\"),
	wxEmptyString,
	wxEmptyString,
	wxT("Item\\TextureComponents\\TorsoUpperTexture\\"),
	wxT("Item\\TextureComponents\\TorsoLowerTexture\\"),
	wxT("Item\\TextureComponents\\LegUpperTexture\\"),
	wxT("Item\\TextureComponents\\LegLowerTexture\\"),
	wxT("Item\\TextureComponents\\FootTexture\\")
};

static wxArrayString creaturemodels;
static std::vector<bool> ridablelist;

IMPLEMENT_CLASS(CharControl, wxWindow)

BEGIN_EVENT_TABLE(CharControl, wxWindow)
	EVT_SPIN(ID_TABARD_ICON, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_ICONCOLOR, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BORDER, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BORDERCOLOR, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BACKGROUND, CharControl::OnTabardSpin)

	EVT_BUTTON(ID_MOUNT, CharControl::OnButton)

	EVT_BUTTON(ID_EQUIPMENT + CS_HEAD, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_NECK, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_SHOULDER, CharControl::OnButton)
	
	EVT_BUTTON(ID_EQUIPMENT + CS_SHIRT, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_CHEST, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_BELT, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_PANTS, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_BOOTS, CharControl::OnButton)

	EVT_BUTTON(ID_EQUIPMENT + CS_BRACERS, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_GLOVES, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_CAPE, CharControl::OnButton)

	EVT_BUTTON(ID_EQUIPMENT + CS_HAND_RIGHT, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_HAND_LEFT, CharControl::OnButton)

	EVT_BUTTON(ID_EQUIPMENT + CS_QUIVER, CharControl::OnButton)
	EVT_BUTTON(ID_EQUIPMENT + CS_TABARD, CharControl::OnButton)
END_EVENT_TABLE()

CharControl::CharControl(wxWindow* parent, wxWindowID id)
{
	wxLogMessage(wxT("Creating Char Control..."));

	if(Create(parent, id, wxDefaultPosition, wxSize(100,700), 0, wxT("CharControl")) == false) {
		wxLogMessage(wxT("GUI Error: Failed to create a window frame for the Character Control!"));
		return;
	}

	wxFlexGridSizer *top = new wxFlexGridSizer(1);

	cdFrame = new CharDetailsFrame(this,cd);
	top->Add(cdFrame);

	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		buttons[i] = NULL;
		labels[i] = NULL;
	}
	
	top->Add(new wxStaticText(this, -1, _("Equipment"), wxDefaultPosition, wxSize(200,20), wxALIGN_CENTRE), wxSizerFlags().Border(wxTOP, 5));
	wxFlexGridSizer *gs2 = new wxFlexGridSizer(2, 5, 5);
	gs2->AddGrowableCol(1);

	#define ADD_CONTROLS(type, caption) \
	gs2->Add(buttons[type]=new wxButton(this, ID_EQUIPMENT + type, caption), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL)); \
	gs2->Add(labels[type]=new wxStaticText(this, -1, _("---- None ----")), wxSizerFlags().Proportion(1).Expand().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));
	
	ADD_CONTROLS(CS_HEAD, _("Head"))
	//ADD_CONTROLS(CS_NECK, wxT("Neck"))
	ADD_CONTROLS(CS_SHOULDER, _("Shoulder"))

	ADD_CONTROLS(CS_SHIRT, _("Shirt"))
	ADD_CONTROLS(CS_CHEST, _("Chest"))
	ADD_CONTROLS(CS_BELT, _("Belt"))
	ADD_CONTROLS(CS_PANTS, _("Legs"))
	ADD_CONTROLS(CS_BOOTS, _("Boots"))

	ADD_CONTROLS(CS_BRACERS, _("Bracers"))
	ADD_CONTROLS(CS_GLOVES, _("Gloves"))
	ADD_CONTROLS(CS_CAPE, _("Cape"))

	ADD_CONTROLS(CS_HAND_RIGHT, _("Right hand"))
	ADD_CONTROLS(CS_HAND_LEFT, _("Left hand"))

	ADD_CONTROLS(CS_QUIVER, _("Quiver"))
	ADD_CONTROLS(CS_TABARD, _("Tabard"))
	#undef ADD_CONTROLS
	
	top->Add(gs2, wxEXPAND);

	// Create our tabard customisation spin buttons
	wxGridSizer *gs3 = new wxGridSizer(3);
	#define ADD_CONTROLS(type, id, caption) \
	gs3->Add(new wxStaticText(this, wxID_ANY, caption), wxSizerFlags().Align(wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL)); \
	gs3->Add(tabardSpins[type]=new wxSpinButton(this, id, wxDefaultPosition, wxSize(30,16), wxSP_HORIZONTAL|wxSP_WRAP), wxSizerFlags(1).Align(wxALIGN_CENTER|wxALIGN_CENTER_VERTICAL)); \
	gs3->Add(spinTbLabels[type] = new wxStaticText(this, wxID_ANY, wxT("0")), wxSizerFlags(2).Align(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL));

	ADD_CONTROLS(SPIN_TABARD_ICON, ID_TABARD_ICON, _("Icon"))
	ADD_CONTROLS(SPIN_TABARD_ICONCOLOR, ID_TABARD_ICONCOLOR, _("Icon Color"))
	ADD_CONTROLS(SPIN_TABARD_BORDER, ID_TABARD_BORDER, _("Border"))
	ADD_CONTROLS(SPIN_TABARD_BORDERCOLOR, ID_TABARD_BORDERCOLOR, _("Border Color"))
	ADD_CONTROLS(SPIN_TABARD_BACKGROUND, ID_TABARD_BACKGROUND, _("BG Color"))

	#undef ADD_CONTROLS

	top->Add(new wxStaticText(this, -1, _("Tabard details")), wxSizerFlags().Align(wxALIGN_CENTRE).Border(wxALL, 1));
	top->Add(gs3, wxEXPAND);
	top->Add(new wxButton(this, ID_MOUNT, _("Choose mount")), wxSizerFlags().Align(wxALIGN_CENTRE).Border(wxTOP, 5));

	//p->SetSizer(top);
	
	top->SetSizeHints(this);
	Show(true);
	SetAutoLayout(true);
	SetSizer(top);
	Layout();
	
	choosingSlot = 0;
	itemDialog = 0;
	model = 0;
	charAtt = 0;

	cd.attach(this);
}

CharControl::~CharControl()
{
	
}


bool CharControl::Init()
{
	charTex = 0;
	hairTex = 0;
	furTex = 0;
	capeTex = 0;
	gobTex = 0;

	td.showCustom = false;
	bSheathe = false;

	cd.showEars = true;
	cd.showHair = true;
	cd.showFacialHair = true;
	cd.showUnderwear = true;

	// set max values for custom tabard
	td.maxBackground = td.GetMaxBackground();
	td.maxBorder = td.GetMaxBorder();
	td.maxBorderColor = td.GetMaxBorderColor(0);
	td.maxIcon = td.GetMaxIcon();
	td.maxIconColor = td.GetMaxIconColor(0);

	return true;
}

//void CharControl::UpdateModel(Model *m)
void CharControl::UpdateModel(Attachment *a)
{
	if (!a)
		return;

	charAtt = a;
	model = (WoWModel*)charAtt->model;

	// The following isn't actually needed, 
	// pretty sure all this gets taken care of by TextureManager and CharTexture
	charTex = 0;
	if (charTex==0) 
		glGenTextures(1, &charTex);

	cd.reset();
	td.showCustom = false;

	// hide most geosets
	for (size_t i=0; i<model->geosets.size(); i++) {
		model->showGeosets[i] = (model->geosets[i].id==0);
	}

	RaceInfos infos;
	RaceInfos::getCurrent(std::string(model->name.mb_str()), infos);

	cd.race = infos.raceid;
	cd.gender = infos.sexid;

	g_modelViewer->charMenu->Check(ID_SHOW_FEET, 0);


	cdFrame->refresh();

	td.Icon = randint(0, td.maxIcon);
	td.IconColor = randint(0, td.maxIconColor);
	td.Border = randint(0, td.maxBorder);
  int maxColor = td.GetMaxBorderColor(td.Border);
	td.BorderColor = randint(0, maxColor);
	td.Background = randint(0, td.maxBackground);

	tabardSpins[SPIN_TABARD_ICON]->SetValue(td.Icon);
	tabardSpins[SPIN_TABARD_ICONCOLOR]->SetValue(td.IconColor);
	tabardSpins[SPIN_TABARD_BORDER]->SetValue(td.Border);
	tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(td.BorderColor);
	tabardSpins[SPIN_TABARD_BACKGROUND]->SetValue(td.Background);

	tabardSpins[SPIN_TABARD_ICON]->SetRange(0, td.maxIcon);
	tabardSpins[SPIN_TABARD_ICONCOLOR]->SetRange(0, td.maxIconColor);
	tabardSpins[SPIN_TABARD_BORDER]->SetRange(0, td.maxBorder);
	tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
	tabardSpins[SPIN_TABARD_BACKGROUND]->SetRange(0, td.maxBackground);

	//for (size_t i=0; i<NUM_SPIN_BTNS; i++)
	//	spins[i]->Refresh(false);
	for (size_t i=0; i<NUM_TABARD_BTNS; i++) {
		tabardSpins[i]->Refresh(false);
		spinTbLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));
	}
	//for (size_t i=0; i<NUM_SPIN_BTNS; i++)
//		spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));

	for (size_t i=0; i<NUM_CHAR_SLOTS; i++)
	{
		if (labels[i])
		{
			labels[i]->SetLabel(_("---- None ----"));
			labels[i]->SetForegroundColour(*wxBLACK);
		}
	}

	if (useRandomLooks)
		cdFrame->randomiseChar();

	RefreshModel();
}

void CharControl::OnCheck(wxCommandEvent &event)
{
	int ID = event.GetId();
	if (ID==ID_SHOW_UNDERWEAR) 
		cd.showUnderwear = event.IsChecked();
	else if (ID==ID_SHOW_HAIR) 
		cd.showHair = event.IsChecked();
	else if (ID==ID_SHOW_FACIALHAIR) 
		cd.showFacialHair = event.IsChecked();
	else if (ID==ID_SHOW_EARS) 
		cd.showEars = event.IsChecked();
	else if (ID==ID_SHEATHE) 
		bSheathe = event.IsChecked();
	else if (ID==ID_SHOW_FEET) 
		cd.showFeet = event.IsChecked();
	else if (ID==ID_CHAREYEGLOW_NONE)
		cd.eyeGlowType = 0;
	else if (ID==ID_CHAREYEGLOW_DEFAULT)
		cd.eyeGlowType = 1;
	else if (ID==ID_CHAREYEGLOW_DEATHKNIGHT)
		cd.eyeGlowType = 2;

	//  Update controls associated
	RefreshEquipment();
	g_modelViewer->UpdateControls();	
	// ----
}

bool slotHasModel(size_t i)
{
	return (i==CS_HEAD || i==CS_SHOULDER || i==CS_HAND_LEFT || i==CS_HAND_RIGHT || i==CS_QUIVER);
}

void CharControl::RefreshEquipment()
{
	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
	{
	//	if (slotHasModel(i))
	//		RefreshItem(i);
	  if (g_canvas->model->modelType != MT_NPC)
	  {
			if (labels[i])
			{
				labels[i]->SetLabel(items.getById(cd.equipment[i]).name);
				labels[i]->SetForegroundColour(ItemQualityColour(items.getById(cd.equipment[i]).quality));
			}
		}
	}
}

void CharControl::OnButton(wxCommandEvent &event)
{
	// This stores are equipment directory path in session
	static wxString dir = cfgPath.BeforeLast(SLASH);

	//if (dir.Last() != '\\')
	//	dir.Append('\\');
	if (event.GetId()==ID_SAVE_EQUIPMENT) {
		wxFileDialog dialog(this, wxT("Save equipment"), dir, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
		if (dialog.ShowModal()==wxID_OK) {
			wxString s(dialog.GetPath());
			cd.save(s, &td);

			// Save directory path
			dir = dialog.GetDirectory();
		}

	} else if (event.GetId()==ID_LOAD_EQUIPMENT) {
/*
		wxFileDialog dialog(this, wxT("Load equipment"), dir, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_OPEN|wxFD_FILE_MUST_EXIST, wxDefaultPosition);
		if (dialog.ShowModal()==wxID_OK) {
			wxString s(dialog.GetPath());
			if (cd.load(s, &td)) {
				spins[SPIN_SKIN_COLOR]->SetValue((int)cd.skinColor);
				spins[SPIN_FACE_TYPE]->SetValue((int)cd.faceType);
				spins[SPIN_HAIR_COLOR]->SetValue((int)cd.hairColor);
				spins[SPIN_HAIR_STYLE]->SetValue((int)cd.hairStyle);
				spins[SPIN_FACIAL_HAIR]->SetValue((int)cd.facialHair);
				for (size_t i=0; i<NUM_SPIN_BTNS; i++) 
					spins[i]->Refresh(false);
			}
			RefreshEquipment();

			// Save directory path
			dir = dialog.GetDirectory();

		}
*/
	} else if (event.GetId()==ID_CLEAR_EQUIPMENT) {
		for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) 
			cd.equipment[i] = 0;
		RefreshEquipment();
	} else if (event.GetId()==ID_LOAD_SET) {
		selectSet();
	} else if (event.GetId()==ID_LOAD_START) {
		selectStart();
	} else if (event.GetId()==ID_MOUNT) {
		selectMount();
	} else {
		for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
			if (buttons[i] && (wxButton*)event.GetEventObject()==buttons[i]) {
				selectItem(UPDATE_ITEM, i, cd.equipment[i], buttons[i]->GetLabel().GetData());
				break;
			}
		}
	}

	RefreshModel();
}

void CharControl::UpdateTextureList(wxString texName, int special)
{
	for (size_t i=0; i< model->header.nTextures; i++)
	{
		if (model->specialTextures[i] == special)
		{
			wxLogMessage(wxT("Updating %s to %s"),model->TextureList[i].c_str(),texName.c_str());
			model->TextureList[i] = texName;
			break;
		}
	}
}

void CharControl::RefreshModel()
{
	hairTex = 0;
	furTex = 0;
	gobTex = 0;
	capeTex = 0;
	bool showScalp = true;

	// Reset geosets
	for (size_t i=0; i<NUM_GEOSETS; i++) 
		cd.geosets[i] = 1;
	cd.geosets[CG_GEOSET100] = cd.geosets[CG_GEOSET200] = cd.geosets[CG_GEOSET300] = 0;

	// show ears, if toggled
	if (cd.showEars) 
		cd.geosets[CG_EARS] = 2;

	RaceInfos infos;
	if(!RaceInfos::getCurrent(std::string(model->name.mb_str()), infos))
	  return;

	CharTexture tex(infos.textureLayoutID);

	std::vector<std::string> textures = cd.getTextureNameForSection(CharDetails::SkinType);

	tex.addLayer(textures[0].c_str(), CR_BASE, 0);

	wxString furTexName = textures[1].c_str();

	if(!furTexName.IsEmpty())
	{
	  furTex = texturemanager.add(furTexName);
	  UpdateTextureList(furTexName, TEXTURE_FUR);
	}

	// Display underwear on the model?
	if (cd.showUnderwear)
	{
	  textures = cd.getTextureNameForSection(CharDetails::UnderwearType);
	  if(!textures.empty())
	  {
	    if(!textures[0].empty())
	      tex.addLayer(textures[0].c_str(), CR_PELVIS_UPPER, 1); // pants

	    if(!textures[1].empty())
	      tex.addLayer(textures[1].c_str(), CR_TORSO_UPPER, 1); // top
	  }
	}

	// face
	textures = cd.getTextureNameForSection(CharDetails::FaceType);
	if(textures.size() != 0)
	{
	  tex.addLayer(textures[0].c_str(), CR_FACE_LOWER, 1);
	  tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 1);
	}

	// facial hair
	textures = cd.getTextureNameForSection(CharDetails::FacialHairType);
	if(textures.size() != 0)
	{
	  tex.addLayer(textures[0].c_str(), CR_FACE_LOWER, 2);
	  if(textures.size() > 1)
	    tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 2);
	}

  // select hairstyle geoset(s)
	QString query = QString("SELECT GeoSetID,ShowScalp FROM CharHairGeoSets WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
	                  .arg(infos.raceid)
	                  .arg(infos.sexid)
	                  .arg(cd.hairStyle());

	sqlResult hairStyle = GAMEDATABASE.sqlQuery(query.toStdString());

	if(hairStyle.valid && !hairStyle.values.empty())
	{
	  showScalp = (bool)atoi(hairStyle.values[0][1].c_str());
	  unsigned int geosetId = atoi(hairStyle.values[0][0].c_str());
	  for (size_t j=0; j<model->geosets.size(); j++) {
	    if (model->geosets[j].id == geosetId)
	      model->showGeosets[j] = cd.showHair;
	    else if (model->geosets[j].id >= 1 && model->geosets[j].id < 100)
	      model->showGeosets[j] = false;
	  }
	}
	else
	{
	  LOG_ERROR << "Unable to collect hair style" << cd.hairStyle() << "for model" << model->name.c_str();
	}

  // Hair texture
	textures = cd.getTextureNameForSection(CharDetails::HairType);
  if(textures.size() != 0 && textures[0] != "")
  {
    hairTex = texturemanager.add(textures[0].c_str());
    UpdateTextureList(textures[0].c_str(), TEXTURE_HAIR);

    if(infos.isHD)
    {
      if(!showScalp && textures.size() > 1 && !textures[1].empty())
        tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 3);
    }
    else
    {
      if(!showScalp)
      {
        if(textures.size() > 1)
          tex.addLayer(textures[1].c_str(), CR_FACE_LOWER, 3);

        if(textures.size() > 2)
          tex.addLayer(textures[2].c_str(), CR_FACE_UPPER, 3);
      }
    }
  }
  else
  {
    hairTex = 0;
  }

  // select facial geoset(s)
  query = QString("SELECT GeoSet1,GeoSet2,GeoSet3,GeoSet4,GeoSet5 FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
                          .arg(infos.raceid)
                          .arg(infos.sexid)
                          .arg(cd.facialHair());

  sqlResult facialHairStyle = GAMEDATABASE.sqlQuery(query.toStdString());

  if(facialHairStyle.valid && !facialHairStyle.values.empty() && cd.showFacialHair)
  {
    LOG_INFO << "Facial GeoSets : " << atoi(facialHairStyle.values[0][0].c_str())
        << " " << atoi(facialHairStyle.values[0][1].c_str())
        << " " << atoi(facialHairStyle.values[0][2].c_str())
        << " " << atoi(facialHairStyle.values[0][3].c_str())
        << " " << atoi(facialHairStyle.values[0][4].c_str());

    cd.geosets[CG_GEOSET100] = atoi(facialHairStyle.values[0][0].c_str());
    cd.geosets[CG_GEOSET200] = atoi(facialHairStyle.values[0][2].c_str());
    cd.geosets[CG_GEOSET300] = atoi(facialHairStyle.values[0][1].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair style" << cd.facialHair() << "for model" << model->name.c_str();
  }

/*
		// facial feature geosets
		try {
			CharFacialHairDB::Record frec = facialhairdb.getByParams((unsigned int)cd.race, (unsigned int)cd.gender, (unsigned int)cd.facialHair);
				cd.geosets[CG_GEOSET100] = frec.getUInt(CharFacialHairDB::Geoset100V400);
				cd.geosets[CG_GEOSET200] = frec.getUInt(CharFacialHairDB::Geoset200V400);
				cd.geosets[CG_GEOSET300] = frec.getUInt(CharFacialHairDB::Geoset300V400);
		} catch (CharFacialHairDB::NotFound) {
			wxLogMessage(wxT("DBC facial feature geosets Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		}
	}

	// hair
	try {
		rec = chardb.getByParams(cd.race, cd.gender, CharSectionsDB::HairType, cd.hairStyle, cd.hairColor, 0);
		wxString hairTexfn = rec.getString(CharSectionsDB::Tex1);
		if (!hairTexfn.IsEmpty()) 
		{
			hairTex = texturemanager.add(hairTexfn);
			UpdateTextureList(hairTexfn, TEXTURE_HAIR);
		}
		else {
			// oops, looks like we're missing a hair texture. Let's try with hair style #0.
			// (only a problem for orcs with no hair but some beard
			try {
	getByParams(cd.race, cd.gender, CharSectionsDB::HairType, 0, cd.hairColor, 0);
				hairTexfn = rec.getString(CharSectionsDB::Tex1);
				if (!hairTexfn.IsEmpty()) 
				{
					hairTex = texturemanager.add(hairTexfn);
					UpdateTextureList(hairTexfn, TEXTURE_HAIR);
				}
				else 
					hairTex = 0;
			} catch (CharSectionsDB::NotFound) {
				// oh well, give up.
				hairTex = 0; // or chartex?
			}
		}
		if (!bald) {
			tex.addLayer(rec.getString(CharSectionsDB::Tex2), CR_FACE_LOWER, 3);
			tex.addLayer(rec.getString(CharSectionsDB::Tex3), CR_FACE_UPPER, 3);
		}

	} catch (CharSectionsDB::NotFound) {
		wxLogMessage(wxT("DBC hair Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		hairTex = 0;
	}

	// If they have no hair, toggle the 'bald' flag.
	if (!showHair)
		bald = true;
	
	// Hide facial hair if it isn't toggled and they don't have tusks, horns, etc.
	if (!showFacialHair) {		
		try {
			CharRacesDB::Record race = racedb.getById(cd.race);
			wxString tmp = race.getString(CharRacesDB::GeoType1V400);
			if (tmp.Lower() == wxT("normal")) {
				cd.geosets[CG_GEOSET100] = 1;
				cd.geosets[CG_GEOSET200] = 1;
				cd.geosets[CG_GEOSET300] = 1;
			}
		} catch (CharRacesDB::NotFound) {
			wxLogMessage(wxT("Assertion FacialHair Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		}
      }
*/
	/*
	// TODO, Temporary work-around - need to do more research.
	// Check to see if we are wearing a helmet - if so, we need to hide our hair
	if (cd.equipment[CS_HEAD] != 0) {
		try {
			const ItemRecord &item = items.getById(cd.equipment[CS_HEAD]);
			int type = item.type;
			if (type==IT_HEAD) {
				ItemDisplayDB::Record r = itemdisplaydb.getById(item.model);
				
				int geoID;
				if(cd.gender == 0)
					geoID = r.getUInt(ItemDisplayDB::GeosetVisID1);
				else
					geoID = r.getUInt(ItemDisplayDB::GeosetVisID2);

				if (geoID) {
				HelmGeosetDB::Record rec = helmetdb.getById(geoID);
				int Hair = rec.getInt(HelmGeosetDB::Hair);
				//char c2 = rec.getByte(HelmGeosetDB::Facial1Flags);
				//char c3 = rec.getByte(HelmGeosetDB::Facial2Flags);
				//unsigned char c4 = rec.getByte(HelmGeosetDB::Facial3Flags);
				//unsigned char c5 = rec.getByte(HelmGeosetDB::EarsFlags);
				
				// TODO: Work out what exactly these geosets mean and act accordingly.
				// These values point to records in HelmetGeosetVisData.dbc
				// Still not sure if the 2 columns are for male / female or
				// for facial hair / normal hair
				//std::cout << "----------\n" << r.getUInt(ItemDisplayDB::GeosetVisID1) << "\t" << r.getUInt(ItemDisplayDB::GeosetVisID2) << "\n";
				
				//std::cout << (unsigned int)rec.getByte(HelmGeosetDB::Field1) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field2) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field3) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field4) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field5) << "\n";
				//rec = helmetdb.getById(r.getUInt(ItemDisplayDB::GeosetVisID2));
				//std::cout << (unsigned int)rec.getByte(HelmGeosetDB::Field1) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field2) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field3) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field4) << "\t" << (unsigned int)rec.getByte(HelmGeosetDB::Field5) << "\n";
				
				
				//std::cout << (int)c1 << " " << (int)c2 << " " << (int)c3 << " " << (unsigned int)c4 << " " << (unsigned int)c5 << "\n";

				if(Hair != 0)
					showHair = false;
					
				//if(c5 == 1)
				//	showFacialHair = false;

				//if(r.getUInt(ItemDisplayDB::GeosetG) > 265)
				//	showFacialHair = false;
				}
			}
		} catch (...) {}
	}
	*/
/*
	// check if we have a robe on
	bool hadRobe = false;
	if (cd.equipment[CS_CHEST] != 0) {
		try {
			const ItemRecord &item = items.getById(cd.equipment[CS_CHEST]);
			if (item.type==IT_ROBE || item.type==IT_CHEST) {
				ItemDisplayDB::Record r = itemdisplaydb.getById(item.model);
				if (r.getUInt(ItemDisplayDB::RobeGeosetFlags)==1) 
					hadRobe = true;
			}
		} catch (ItemDisplayDB::NotFound) {
			wxLogMessage(wxT("Assertion robe Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		}
	}

	// check if we have a kilt on, just like our robes
	if (cd.equipment[CS_PANTS] != 0) {
		try {
			const ItemRecord &item = items.getById(cd.equipment[CS_PANTS]);
			if (item.type==IT_PANTS) {
				ItemDisplayDB::Record r = itemdisplaydb.getById(item.model);
				if (r.getUInt(ItemDisplayDB::RobeGeosetFlags)==1) 
					hadRobe = true;
			}
		} catch (ItemDisplayDB::NotFound) {
			wxLogMessage(wxT("Assertion kilt Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		}
	}

	// Default
	slotOrderWithRobe[7] = CS_CHEST;
	slotOrderWithRobe[8] = CS_GLOVES;

	// check the order of robe/gloves
	if (cd.equipment[CS_CHEST] && cd.equipment[CS_GLOVES]) {
		try {
			//const ItemRecord &item = items.getById(cd.equipment[CS_CHEST]);
			//if (item.type==IT_ROBE) {
			//	ItemDisplayDB::Record r = itemdisplaydb.getById(item.model);
				//if (r.getUInt(ItemDisplayDB::GeosetA)>0) {
					const ItemRecord &item2 = items.getById(cd.equipment[CS_GLOVES]);
					ItemDisplayDB::Record r2 = itemdisplaydb.getById(item2.model);
					if (r2.getUInt(ItemDisplayDB::GloveGeosetFlags)==0) {
						slotOrderWithRobe[7] = CS_GLOVES;
						slotOrderWithRobe[8] = CS_CHEST;
					}
				//}
			//}
		} catch (ItemDisplayDB::NotFound) {
			wxLogMessage(wxT("Assertion robe/gloves Error: %s : line #%i : %s"), __FILE__, __LINE__, __FUNCTION__);
		}
	}
*/
	// dressup
	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
	{
		CharSlots sn = slotOrder[i];
		if (cd.equipment[sn] != 0)
			AddEquipment(sn, cd.equipment[sn], 10+i, tex, !cd.isNPC);
	}

	LOG_INFO << "Current Equipement :"
	         << "Head" << cd.equipment[CS_HEAD]
	         << "Shoulder" << cd.equipment[CS_SHOULDER]
	         << "Shirt" << cd.equipment[CS_SHIRT]
	         << "Chest" << cd.equipment[CS_CHEST]
	         << "Belt" << cd.equipment[CS_BELT]
	         << "Legs" << cd.equipment[CS_PANTS]
	         << "Boots" << cd.equipment[CS_BOOTS]
	         << "Bracers" << cd.equipment[CS_BRACERS]
	         << "Gloves" << cd.equipment[CS_GLOVES]
	         << "Cape" << cd.equipment[CS_CAPE]
	         << "Right Hand" << cd.equipment[CS_HAND_RIGHT]
	         << "Left Hand" << cd.equipment[CS_HAND_LEFT]
	         << "Quiver" << cd.equipment[CS_QUIVER]
	         << "Tabard" << cd.equipment[CS_TABARD];

	// reset geosets
	for (size_t j=0; j<model->geosets.size(); j++) {
		int id = model->geosets[j].id;

		for (size_t i=1; i<NUM_GEOSETS; i++) {
			int a = (int)i*100, b = ((int)i+1) * 100;
			if (id>a && id<b) 
				model->showGeosets[j] = (id == (a + cd.geosets[i]));

		}
	}

	if(cd.equipment[CS_HEAD] != 0)
	{
	  QString query = QString("SELECT HideGeoset1, HideGeoset2, HideGeoset3, HideGeoset4, HideGeoset5,"
	    "HideGeoset6,HideGeoset7 FROM HelmetGeosetVisData WHERE ID = (SELECT %1 FROM ItemDisplayInfo "
	    "WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %2)))")
	  .arg((infos.sexid == 0)?"HelmetGeosetVis1":"HelmetGeosetVis2")
	  .arg(cd.equipment[CS_HEAD]);

	sqlResult helmetInfos = GAMEDATABASE.sqlQuery(query.toStdString());

	if(helmetInfos.valid)
	{
	  // hair styles
	  if(atoi(helmetInfos.values[0][0].c_str()) != 0)
	  {
	    for (size_t i=0; i<model->geosets.size(); i++)
	    {
	      int id = model->geosets[i].id;
	      if(id > 0 && id < 100)
	        model->showGeosets[i] = false;
	    }
	  }

	  // facial 1
	  if(atoi(helmetInfos.values[0][1].c_str()) != 0)
	  {
	    for (size_t i=0; i<model->geosets.size(); i++)
	    {
	      int id = model->geosets[i].id;
	      if(id > 100 && id < 200)
	        model->showGeosets[i] = false;
	    }
	  }

	  // facial 2
	  if(atoi(helmetInfos.values[0][2].c_str()) != 0)
	  {
	    for (size_t i=0; i<model->geosets.size(); i++)
	    {
	      int id = model->geosets[i].id;
	      if(id > 200 && id < 300)
	        model->showGeosets[i] = false;
	    }
	  }

	  // facial 3
	  if(atoi(helmetInfos.values[0][3].c_str()) != 0)
	  {
	    for (size_t i=0; i<model->geosets.size(); i++)
	    {
	      int id = model->geosets[i].id;
	      if(id > 300 && id < 400)
	        model->showGeosets[i] = false;
	    }
	  }

	  // ears
	  if(atoi(helmetInfos.values[0][4].c_str()) != 0)
	  {
	    for (size_t i=0; i<model->geosets.size(); i++)
	    {
	      int id = model->geosets[i].id;
	      if(id > 700 && id < 800)
	        model->showGeosets[i] = false;
	    }
	  }
	}
}


	// finalize character texture
	tex.compose(charTex);
	
	// set replacable textures
	model->replaceTextures[TEXTURE_BODY] = charTex;
	model->replaceTextures[TEXTURE_CAPE] = capeTex;
	model->replaceTextures[TEXTURE_HAIR] = hairTex;
	model->replaceTextures[TEXTURE_FUR] = furTex;
	model->replaceTextures[TEXTURE_GAMEOBJECT1] = gobTex;

	/*
	for (size_t i=0; i<ATT_MAX; i++) {
		model->atts[i].dr = (model->atts[i].id==cd.hairStyle);
	}
	*/

	// Alfred 2009.07.18 show max value
/*	for (size_t i=0; i<NUM_SPIN_BTNS; i++)
		spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));

	spins[SPIN_SKIN_COLOR]->SetValue((int)cd.skinColor);
	spins[SPIN_FACE_TYPE]->SetValue((int)cd.faceType);
	spins[SPIN_HAIR_COLOR]->SetValue((int)cd.hairColor);
	spins[SPIN_HAIR_STYLE]->SetValue((int)cd.hairStyle);
	spins[SPIN_FACIAL_HAIR]->SetValue((int)cd.facialHair);
*/
	/*
	// Eye Glows
	for(size_t i=0; i<model->passes.size(); i++) {
		ModelRenderPass &p = model->passes[i];
		wxString texName = model->TextureList[p.tex].AfterLast('\\').Lower();

		if (texName.Find(wxT("eyeglow")) == wxNOT_FOUND)
			continue;

		// Regular Eye Glow
		if ((texName.Find(wxT("eyeglow")) != wxNOT_FOUND)&&(texName.Find(wxT("deathknight")) == wxNOT_FOUND)){
			if (cd.eyeGlowType == EGT_NONE){					// If No EyeGlow
				model->showGeosets[p.geoset] = false;
			}else if (cd.eyeGlowType == EGT_DEATHKNIGHT){		// If DK EyeGlow
				model->showGeosets[p.geoset] = false;
			}else{												// Default EyeGlow, AKA cd.eyeGlowType == EGT_DEFAULT
				model->showGeosets[p.geoset] = true;
			}
		}
		// DeathKnight Eye Glow
		if (texName.Find(wxT("deathknight")) != wxNOT_FOUND){
			if (cd.eyeGlowType == EGT_NONE){					// If No EyeGlow
				model->showGeosets[p.geoset] = false;
			}else if (cd.eyeGlowType == EGT_DEATHKNIGHT){		// If DK EyeGlow
				model->showGeosets[p.geoset] = true;
			}else{												// Default EyeGlow, AKA cd.eyeGlowType == EGT_DEFAULT
				model->showGeosets[p.geoset] = false;
			}
		}
	}
	// Update Eye Glow Menu
	size_t egt = cd.eyeGlowType;
	if (egt == EGT_NONE)
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
	else if (egt == EGT_DEATHKNIGHT)
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
	else
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
		*/
}

void CharControl::AddEquipment(CharSlots slot, ssize_t itemnum, ssize_t layer, CharTexture &tex, bool itemid)
{
  RaceInfos infos;
  if(!RaceInfos::getCurrent(std::string(model->name.mb_str()), infos))
    return;

  QString query = QString("SELECT Model1,Model2, \
	    FD10.path AS Model1TexPath, FD10.name AS Model1TexName, \
	    FD11.path AS Model2TexPath, FD11.name AS Model2TexName, \
	    GeosetGroup1, GeosetGroup2, GeosetGroup3, \
	    HelmetGeoSetVis1,HelmetGeoSetVis2, \
	    FD1.path AS UpperArmTexPath, FD1.name AS UpperArmTexName, \
	    FD2.path AS LowerArmTexPath, FD2.name AS LowerArmTexName, \
	    FD3.path AS HandsTexPath, FD3.name AS HandsTexName, \
	    FD4.path AS UpperTorsoTexPath, FD4.name AS UpperTorsoTexName, \
	    FD5.path AS LowerTorsoTexPath, FD5.name AS LowerTorsoTexName, \
	    FD6.path AS UpperLegTexPath, FD6.name AS UpperLegTexName, \
	    FD7.path AS LowerLegTexPath, FD7.name AS LowerLegTexName, \
	    FD8.path AS FootTexPath, FD8.name AS FootTexName, \
	    FD9.path AS AccessoryTexPath, FD9.name AS AccessoryTexName \
	    FROM ItemDisplayInfo \
	    LEFT JOIN TextureFileData TFD1 ON TFD1.TextureItemID = TextureID1 AND TFD1.TextureType != %2 LEFT JOIN FileData FD1 ON TFD1.FileDataID = FD1.ID \
	    LEFT JOIN TextureFileData TFD2 ON TFD2.TextureItemID = TextureID2 AND TFD2.TextureType != %2 LEFT JOIN FileData FD2 ON TFD2.FileDataID = FD2.ID \
	    LEFT JOIN TextureFileData TFD3 ON TFD3.TextureItemID = TextureID3 AND TFD3.TextureType != %2 LEFT JOIN FileData FD3 ON TFD3.FileDataID = FD3.ID \
	    LEFT JOIN TextureFileData TFD4 ON TFD4.TextureItemID = TextureID4 AND TFD4.TextureType != %2 LEFT JOIN FileData FD4 ON TFD4.FileDataID = FD4.ID \
	    LEFT JOIN TextureFileData TFD5 ON TFD5.TextureItemID = TextureID5 AND TFD5.TextureType != %2 LEFT JOIN FileData FD5 ON TFD5.FileDataID = FD5.ID \
	    LEFT JOIN TextureFileData TFD6 ON TFD6.TextureItemID = TextureID6 AND TFD6.TextureType != %2 LEFT JOIN FileData FD6 ON TFD6.FileDataID = FD6.ID \
	    LEFT JOIN TextureFileData TFD7 ON TFD7.TextureItemID = TextureID7 AND TFD7.TextureType != %2 LEFT JOIN FileData FD7 ON TFD7.FileDataID = FD7.ID \
	    LEFT JOIN TextureFileData TFD8 ON TFD8.TextureItemID = TextureID8 AND TFD8.TextureType != %2 LEFT JOIN FileData FD8 ON TFD8.FileDataID = FD8.ID \
	    LEFT JOIN TextureFileData TFD9 ON TFD9.TextureItemID = TextureID9 AND TFD9.TextureType != %2 LEFT JOIN FileData FD9 ON TFD9.FileDataID = FD9.ID \
	    LEFT JOIN TextureFileData TFD10 ON TFD10.TextureItemID = TextureItemID1 AND TFD10.TextureType != %2 LEFT JOIN FileData FD10 ON TFD10.FileDataID = FD10.ID \
	    LEFT JOIN TextureFileData TFD11 ON TFD11.TextureItemID = TextureItemID2 AND TFD11.TextureType != %2 LEFT JOIN FileData FD11 ON TFD11.FileDataID = FD11.ID ")
    .arg((infos.sexid == 0)?1:0);

  if(itemid)
    query += QString("WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1))")
	     .arg(itemnum);
  else
    query += QString("WHERE ItemDisplayInfo.ID = %1")
       .arg(itemnum);

    sqlResult iteminfos = GAMEDATABASE.sqlQuery(query.toStdString());

/*
    if(iteminfos.valid && ! iteminfos.values.empty())
    {
      for(unsigned int i=0; i < iteminfos.values.size() ; i++)
        for(unsigned int j=0; j < iteminfos.values[i].size() ; j++)
          std::cout << i << " " << j << " " << iteminfos.values[i][j] << std::endl;
    }
*/

    switch(slot)
    {
    case CS_HEAD:
    {
      charAtt->delSlot(CS_HEAD);
      Attachment *att = NULL;
      WoWModel *m = NULL;
      GLuint tex;
      std::string model = iteminfos.values[0][0];
      // remove .mdx
      model = model.substr(0, model.length()-4);
      // add race prefix
      model += "_";
      model += infos.prefix;
      // add sex suffix
      model += ((infos.sexid == 0)?"M":"F");
      // add .m2
      model += ".m2";
      model = CASCFOLDER.getFullPathForFile(model);
      att = charAtt->addChild(model, ATT_HELMET, slot);
      if (att)
      {
        m = static_cast<WoWModel*>(att->model);
        if (m->ok)
        {
          std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
          tex = texturemanager.add(texture);
          for (size_t x=0;x<m->TextureList.size();x++)
          {
            if (m->TextureList[x] == wxString(wxT("Special_2")))
            {
              wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
              m->TextureList[x] = texture;
            }
          }
          m->replaceTextures[TEXTURE_CAPE] = tex;
        }
      }

      break;
    }
    case CS_NECK:
      break;
    case CS_SHOULDER:
    {
      charAtt->delSlot(CS_SHOULDER);
      Attachment *att = NULL;
      WoWModel *m = NULL;
      GLuint tex;

      // left shoulder
      std::string model = iteminfos.values[0][0];
      model = model.substr(0, model.length()-4); // remove .mdx
      model += ".m2"; // add .m2
      model = CASCFOLDER.getFullPathForFile(model);
      att = charAtt->addChild(model, ATT_LEFT_SHOULDER, slot);
      if (att)
      {
        m = static_cast<WoWModel*>(att->model);
        if (m->ok)
        {
          std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
          tex = texturemanager.add(texture);
          for (size_t x=0;x<m->TextureList.size();x++)
          {
            if (m->TextureList[x] == wxString(wxT("Special_2")))
            {
              wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
              m->TextureList[x] = texture;
            }
          }
          m->replaceTextures[TEXTURE_CAPE] = tex;
        }
      }

      // right shoulder
      model = iteminfos.values[0][1];
      model = model.substr(0, model.length()-4); // remove .mdx
      model += ".m2"; // add .m2
      model = CASCFOLDER.getFullPathForFile(model);
      att = charAtt->addChild(model, ATT_RIGHT_SHOULDER, slot);
      if (att)
      {
        m = static_cast<WoWModel*>(att->model);
        if (m->ok)
        {
          std::string texture = iteminfos.values[0][4] + iteminfos.values[0][5];
          tex = texturemanager.add(texture);
          for (size_t x=0;x<m->TextureList.size();x++)
          {
            if (m->TextureList[x] == wxString(wxT("Special_2")))
            {
              wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
              m->TextureList[x] = texture;
            }
          }
          m->replaceTextures[TEXTURE_CAPE] = tex;
        }
      }
    }
      break;
    case CS_BOOTS:
    {
      cd.geosets[CG_BOOTS] = 1 + atoi(iteminfos.values[0][6].c_str());

      wxString texture = iteminfos.values[0][23] + iteminfos.values[0][24];
      tex.addLayer(texture, CR_LEG_LOWER, layer);
      if (!cd.showFeet)
      {
        texture = iteminfos.values[0][25] + iteminfos.values[0][26];
        tex.addLayer(texture, CR_FOOT, layer);
      }
    }
      break;
    case CS_BELT:
    {
      wxString texture = iteminfos.values[0][21] + iteminfos.values[0][22];
      tex.addLayer(texture, CR_LEG_UPPER, layer);
    }
      break;
    case CS_PANTS:
    {
      cd.geosets[CG_KNEEPADS] = 1 + atoi(iteminfos.values[0][7].c_str());
      wxString texture = iteminfos.values[0][21] + iteminfos.values[0][22];
      tex.addLayer(texture, CR_LEG_UPPER, layer);
      texture = iteminfos.values[0][23] + iteminfos.values[0][24];
      tex.addLayer(texture, CR_LEG_LOWER, layer);

      if (atoi(iteminfos.values[0][8].c_str())==1)
        cd.geosets[CG_TROUSERS] = 1 + atoi(iteminfos.values[0][8].c_str());

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      cd.geosets[CG_WRISTBANDS] = 1 + atoi(iteminfos.values[0][6].c_str());
      wxString texture = iteminfos.values[0][11] + iteminfos.values[0][12];
      tex.addLayer(texture, CR_ARM_UPPER, layer);
      texture = iteminfos.values[0][13] + iteminfos.values[0][14];
      tex.addLayer(texture, CR_ARM_LOWER, layer);
      texture = iteminfos.values[0][17] + iteminfos.values[0][18];
      tex.addLayer(texture, CR_TORSO_UPPER, layer);
      texture = iteminfos.values[0][19] + iteminfos.values[0][20];
      tex.addLayer(texture, CR_TORSO_LOWER, layer);

      const ItemRecord &item = items.getById(itemnum);

      if (item.type == IT_ROBE || atoi(iteminfos.values[0][8].c_str())==1)
      {
        cd.geosets[CG_TROUSERS] = 1 + atoi(iteminfos.values[0][8].c_str());
        wxString texture = iteminfos.values[0][21] + iteminfos.values[0][22];
        tex.addLayer(texture, CR_LEG_UPPER, layer);
        texture = iteminfos.values[0][23] + iteminfos.values[0][24];
        tex.addLayer(texture, CR_LEG_LOWER, layer);
      }
    }
      break;
    case CS_BRACERS:
    {
      wxString texture = iteminfos.values[0][13] + iteminfos.values[0][14];
      tex.addLayer(texture, CR_ARM_LOWER, layer);
    }
      break;
    case CS_GLOVES:
    {
      cd.geosets[CG_GLOVES] = 1 + atoi(iteminfos.values[0][6].c_str());
      wxString texture = iteminfos.values[0][13] + iteminfos.values[0][14];
      tex.addLayer(texture, CR_ARM_LOWER, layer);
      texture = iteminfos.values[0][15] + iteminfos.values[0][16];
      tex.addLayer(texture, CR_HAND, layer);
    }
      break;
    case CS_HAND_RIGHT:
    {
      charAtt->delSlot(CS_HAND_RIGHT);
      Attachment *att = NULL;
      WoWModel *m = NULL;
      GLuint tex;

      std::string itemModel = iteminfos.values[0][0];
      itemModel = itemModel.substr(0, itemModel.length()-4); // remove .mdx
      itemModel += ".m2"; // add .m2
      itemModel = CASCFOLDER.getFullPathForFile(itemModel);
      int attachement = ATT_RIGHT_PALM;
      const ItemRecord &item = items.getById(itemnum);
      if(bSheathe &&  item.sheath != SHEATHETYPE_NONE)
      {
        // make the weapon cross
        if (item.sheath == ATT_LEFT_BACK_SHEATH)
          attachement = ATT_RIGHT_BACK_SHEATH;
        if (item.sheath == ATT_LEFT_BACK)
          attachement = ATT_RIGHT_BACK;
        if (item.sheath == ATT_LEFT_HIP_SHEATH)
          attachement = ATT_RIGHT_HIP_SHEATH;
      }

      att = charAtt->addChild(itemModel, attachement, slot);
      if (att)
      {
        if(bSheathe)
          model->charModelDetails.closeRHand = false;
        else
          model->charModelDetails.closeRHand = true;

        m = static_cast<WoWModel*>(att->model);
        if (m->ok)
        {
          std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
          tex = texturemanager.add(texture);
          for (size_t x=0;x<m->TextureList.size();x++)
          {
            if (m->TextureList[x] == wxString(wxT("Special_2")))
            {
              wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
              m->TextureList[x] = texture;
            }
          }
          m->replaceTextures[TEXTURE_CAPE] = tex;
        }
      }
    }
      break;
    case CS_HAND_LEFT:
    {
      charAtt->delSlot(CS_HAND_LEFT);
      Attachment *att = NULL;
      WoWModel *m = NULL;
      GLuint tex;

      std::string itemModel = iteminfos.values[0][0];
      itemModel = itemModel.substr(0, itemModel.length()-4); // remove .mdx
      itemModel += ".m2"; // add .m2
      itemModel = CASCFOLDER.getFullPathForFile(itemModel);
      const ItemRecord &item = items.getById(itemnum);
      int attachement = ATT_LEFT_PALM;

      if(item.type == IT_SHIELD)
        attachement = ATT_LEFT_WRIST;

      if(bSheathe &&  item.sheath != SHEATHETYPE_NONE)
        attachement = item.sheath;

      att = charAtt->addChild(itemModel, attachement, slot);
      if (att)
      {
        if(bSheathe || item.type == IT_SHIELD)
          model->charModelDetails.closeLHand = false;
        else
          model->charModelDetails.closeLHand = true;

        m = static_cast<WoWModel*>(att->model);
        if (m->ok)
        {
          std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
          tex = texturemanager.add(texture);
          for (size_t x=0;x<m->TextureList.size();x++)
          {
            if (m->TextureList[x] == wxString(wxT("Special_2")))
            {
              wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
              m->TextureList[x] = texture;
            }
          }
          m->replaceTextures[TEXTURE_CAPE] = tex;
        }
      }
    }
      break;
    case CS_CAPE:
    {
      cd.geosets[CG_CAPE] = 1 + atoi(iteminfos.values[0][6].c_str());
      wxString texture = iteminfos.values[0][2] + iteminfos.values[0][3];
      capeTex = texturemanager.add(texture);
      UpdateTextureList(texture, TEXTURE_CAPE);
    }
      break;
    case CS_TABARD:
    {
      cd.geosets[CG_TARBARD] = 2;
      if(itemnum == 5976) // guild tabard
      {
        LOG_INFO << "Current tabard config :"
                 << "Icon" << td.Icon
                 << "IconColor" << td.IconColor
                 << "Border" << td.Border
                 << "BorderColor" << td.BorderColor
                 << "Background" << td.Background;
        td.showCustom = true;
        tex.addLayer(td.GetBackgroundTex(CR_TORSO_UPPER), CR_TORSO_UPPER, layer);
        tex.addLayer(td.GetBackgroundTex(CR_TORSO_LOWER), CR_TORSO_LOWER, layer);
        tex.addLayer(td.GetIconTex(CR_TORSO_UPPER), CR_TORSO_UPPER, layer);
        tex.addLayer(td.GetIconTex(CR_TORSO_LOWER), CR_TORSO_LOWER, layer);
        tex.addLayer(td.GetBorderTex(CR_TORSO_UPPER), CR_TORSO_UPPER, layer);
        tex.addLayer(td.GetBorderTex(CR_TORSO_LOWER), CR_TORSO_LOWER, layer);
      }
      else
      {
        td.showCustom = false;
        wxString texture = iteminfos.values[0][17] + iteminfos.values[0][18];
        tex.addLayer(texture, CR_TORSO_UPPER, layer);
        texture = iteminfos.values[0][19] + iteminfos.values[0][20];
        tex.addLayer(texture, CR_TORSO_LOWER, layer);
      }
    }
      break;
    case CS_QUIVER:
      break;
    default:
      break;
    }

		// gloves - this is so gloves have preference over shirt sleeves.
		if (cd.geosets[CG_GLOVES] > 1) 
			cd.geosets[CG_WRISTBANDS] = 0;
}

void CharControl::ClearItemDialog()
{
	if (itemDialog) {
		itemDialog->Show(FALSE);
		itemDialog->Destroy();
		wxDELETE(itemDialog);
	}
}

void CharControl::selectItem(ssize_t type, ssize_t slot, ssize_t current, const wxChar *caption)
{
  //std::cout << __FUNCTION__ << " type = " << type << " / slot = " << slot << " / current = " << current << std::endl;
	if (items.items.size() == 0)
		return;
	ClearItemDialog();

	numbers.clear();
	choices.Clear();
	cats.clear();
	catnames.clear();

	std::vector<int> quality;

	// collect all items for this slot, making note of the occurring subclasses
	std::set<std::pair<int,int> > subclassesFound;
	
	//std::cout << "item db size = " << items.items.size() << std::endl;

	std::map<std::pair<int,int>, int> subclasslookup;

	sqlResult itemClasses = GAMEDATABASE.sqlQuery("SELECT ID, SubClassID, Name, VerboseName FROM ItemSubClass");

	if(itemClasses.valid && !itemClasses.empty())
	{
	  for(int i=0, imax=itemClasses.values.size() ; i < imax ; i++)
	  {
	    // first set verbose name
	    wxString name = itemClasses.values[i][3].c_str();
	    // if empty, fall back to normal one
	    if(name.IsEmpty())
	      name = itemClasses.values[i][2].c_str();

	    catnames.Add(CSConv(name));
	    subclasslookup[std::pair<int,int>(atoi(itemClasses.values[i][0].c_str()),atoi(itemClasses.values[i][1].c_str()))] = (int)catnames.size()-1;
	  }
	}

	for (std::vector<ItemRecord>::iterator it = items.items.begin(); it != items.items.end(); ++it) {
		if (type == UPDATE_SINGLE_ITEM)
		{
		  if (it->type == IT_SHOULDER || it->type == IT_SHIELD ||
		      it->type == IT_BOW || it->type == IT_2HANDED || it->type == IT_LEFTHANDED ||
		      it->type == IT_RIGHTHANDED || it->type == IT_OFFHAND || it->type == IT_GUN)
		  {
		    choices.Add(it->name);
		    numbers.push_back(it->id);
		    quality.push_back(it->quality);

		    subclassesFound.insert(std::pair<int,int>(it->itemclass,it->subclass));
		    cats.push_back(subclasslookup[std::pair<int,int>(it->itemclass, it->subclass)]);
		  }
		}
		else if (correctType((ssize_t)it->type, slot))
		{
			choices.Add(it->name);
			numbers.push_back(it->id);
			quality.push_back(it->quality);

			if (it->itemclass > 0)
			{
				subclassesFound.insert(std::pair<int,int>(it->itemclass,it->subclass));
			}
			cats.push_back(subclasslookup[std::pair<int,int>(it->itemclass, it->subclass)]);
		}
	}

	if (subclassesFound.size() > 1)
		itemDialog = new CategoryChoiceDialog(this, type, g_modelViewer, wxT("Choose an item"), caption, choices, cats, catnames, &quality);
	else
	  itemDialog = new FilteredChoiceDialog(this, type, g_modelViewer, wxT("Choose an item"), caption, choices, &quality);

	wxSize s = itemDialog->GetSize();
	const int w = 250;
	if (s.GetWidth() > w) {
		itemDialog->SetSizeHints(w,-1,-1,-1,-1,-1);
		itemDialog->SetSize(w, -1);
	}

	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Show();
	choosingSlot = slot;
}

/*
struct NumStringPair {
	int id;
	string name;

	const bool operator< (const NumStringPair &p) const {
		return name < p.name;
	}
};
*/

void CharControl::selectSet()
{
	if (setsdb.size() == 0)
		return;
	ClearItemDialog();

	std::vector<NumStringPair> items;

	// Adds "none" to select
	NumStringPair n; 
	n.id = -1; 
	n.name = wxT("---- None ----");
	items.push_back(n);

	for (ItemSetDB::Iterator it = setsdb.begin(); it != setsdb.end(); ++it) {
		int id = it->getUInt(ItemSetDB::SetID);
		if (setsdb.available(id)) {
			NumStringPair p;
			p.id = id;
			p.name = CSConv(it->getString(ItemSetDB::Name + langOffset));
			items.push_back(p);
		}
	}

	std::sort(items.begin(), items.end());
	numbers.clear();
	choices.Clear();
	for (std::vector<NumStringPair>::iterator it = items.begin(); it != items.end(); ++it) {
		choices.Add(it->name);
		numbers.push_back(it->id);
	}

	itemDialog = new FilteredChoiceDialog(this, UPDATE_SET, g_modelViewer, wxT("Choose an item set"), wxT("Item sets"), choices, NULL);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Show();
}

void CharControl::selectStart()
{
  /*
   @ TODO : to repair
	if (startdb.size() == 0)
		return;
	ClearItemDialog();

	numbers.clear();
	choices.Clear();

	for (StartOutfitDB::Iterator it = startdb.begin(); it != startdb.end(); ++it) {
		if ((it->getByte(StartOutfitDB::Race) == cd.race) && (it->getByte(StartOutfitDB::Gender) == cd.gender)) {
			try {
				CharClassesDB::Record r = classdb.getById(it->getByte(StartOutfitDB::Class));
				choices.Add(CSConv(r.getString(CharClassesDB::NameV400 + langOffset)));
				numbers.push_back(it->getUInt(StartOutfitDB::StartOutfitID));
			} catch (CharClassesDB::NotFound) {}
		}
	}

	itemDialog = new ChoiceDialog(this, UPDATE_START, g_modelViewer, wxT("Choose a class"), wxT("Classes"), choices);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Show();
	*/
}

bool filterCreatures(wxString fn)
{
	wxString tmp = fn.Lower();
	return (tmp.StartsWith(wxT("crea")) && tmp.EndsWith(wxT("m2")));
}

// TODO: Add an equivilant working version of this function for Linux / Mac OS X
void CharControl::selectMount()
{
  /*
	ClearItemDialog();

	numbers.clear();
	choices.Clear();
	cats.clear();
	catnames.Clear();
	catnames.Add(wxT("Known ridable models"));
	catnames.Add(wxT("Other models"));

	static bool filelistInitialized = false;

	if (!filelistInitialized) {
		std::set<FileTreeItem> filelist;

		wxArrayString knownRidable;

		getFileLists(filelist, filterCreatures);

		wxTextFile file;
		file.Open(wxT("ridable.csv"));
		if (file.IsOpened()) {
			wxString tmp;
			for ( tmp = file.GetFirstLine(); !file.Eof(); tmp = file.GetNextLine() ) {
				tmp.MakeLower();
				if (knownRidable.Index(tmp, false)==wxNOT_FOUND)
					knownRidable.Add(tmp);
			}
		} else {
			wxLogMessage(wxT("Can't Initiate ridable.csv ..."));
		}
		
		for (std::set<FileTreeItem>::iterator it = filelist.begin(); it != filelist.end(); ++it) {
			wxString str((*it).displayName.Lower());
			creaturemodels.push_back(str);
			ridablelist.push_back(knownRidable.Index(str, false)!=wxNOT_FOUND);
		}
		filelistInitialized = true;
	}

	choices.Add(_("---- None ----"));
	cats.push_back(0);
	
	for (size_t i=0; i<creaturemodels.size(); i++) {
		choices.Add(creaturemodels[i].Mid(9));
		cats.push_back(ridablelist[i]?0:1);
	}

	// TODO: obtain a list of "known ridable" models, and filter the list into two categories
	itemDialog = new FilteredChoiceDialog(this, UPDATE_MOUNT, g_modelViewer, wxT("Choose a mount"), wxT("Creatures"), choices, 0);
	CategoryChoiceDialog *itemDialog = new CategoryChoiceDialog(this, UPDATE_MOUNT, g_modelViewer, wxT("Choose a mount"), wxT("Creatures"), choices, cats, catnames, 0);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Check(1, false);
	itemDialog->DoFilter();
	itemDialog->Show();

	const int w = 250;
	itemDialog->SetSizeHints(w,-1,-1,-1,-1,-1);
	itemDialog->SetSize(w, -1); 
	this->itemDialog = itemDialog;
	*/
}

void CharControl::selectNPC(ssize_t type)
{
	if (npcs.size() == 0)
		return;
	ClearItemDialog();

	numbers.clear();
	choices.Clear();
	cats.clear();
	catnames.clear();

	std::vector<int> quality;


	std::map<int, int> typeLookup;

	sqlResult npccats = GAMEDATABASE.sqlQuery("SELECT ID,Name FROM CreatureType");

	if(npccats.valid && !npccats.empty())
	{
	  for(int i=0, imax=npccats.values.size() ; i < imax ; i++)
	  {
	    catnames.Add(CSConv(npccats.values[i][1]));
	    typeLookup[atoi(npccats.values[i][0].c_str())] = (int)catnames.size()-1;
	  }
	}

	std::vector<int> typesFound;

	for (std::vector<NPCRecord>::iterator it=npcs.begin();  it!=npcs.end(); ++it)
	{
		if (it->model > 0)
		{
			choices.Add(it->name);
			numbers.push_back(it->id);
			quality.push_back(0);
			
			if (it->type >= 0)
			{
			  cats.push_back(typeLookup[it->type]);
				typesFound.push_back(it->type);
			}
			else
			{
			  cats.push_back(0);
			}
		}
	}
	
	if (typesFound.size() > 1)
		itemDialog = new CategoryChoiceDialog(this, (int)type, g_modelViewer, _("Select an NPC"), _("NPC Models"), choices, cats, catnames, &quality, false, true);
	else
		itemDialog = new FilteredChoiceDialog(this, (int)type, g_modelViewer, _("Select an NPC"), _("NPC Models"), choices, &quality, false);
	
	itemDialog->SetSelection(0);
	
	wxSize s = itemDialog->GetSize();
	const int w = 250;
	if (s.GetWidth() > w)
	{
		itemDialog->SetSizeHints(w,-1,-1,-1,-1,-1);
		itemDialog->SetSize(w, -1);
	}

	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Show();
}

void CharControl::OnUpdateItem(int type, int id)
{
	switch (type) {
	case UPDATE_ITEM:
		if (choosingSlot == CS_HAND_LEFT)
			model->charModelDetails.closeLHand = false;
		else if (choosingSlot == CS_HAND_RIGHT)
			model->charModelDetails.closeRHand = false;

		// special case : we previously have a model, but not anymore => remove it
		// @TODO : need better management of this...
		if (slotHasModel(choosingSlot) && cd.equipment[choosingSlot] != 0 && numbers[id] == 0)
		  charAtt->delSlot(choosingSlot);

		cd.equipment[choosingSlot] = numbers[id];

		labels[choosingSlot]->SetLabel(items.getById(cd.equipment[choosingSlot]).name);
		labels[choosingSlot]->SetForegroundColour(ItemQualityColour(items.getById(cd.equipment[choosingSlot]).quality));
		break;

	case UPDATE_SET:
		id = numbers[id];

		if (id) {
			for (size_t i=0; i<NUM_CHAR_SLOTS; i++) {
				//if (i!=CS_HAND_LEFT && i!=CS_HAND_RIGHT) 
				cd.equipment[i] = 0;
			}
			cd.loadSet(setsdb, items, id);
			RefreshEquipment();
			RefreshModel();
		}
		break;

	case UPDATE_START:
		id = numbers[id];

		if (id) {
			for (size_t i=0; i<NUM_CHAR_SLOTS; i++) cd.equipment[i] = 0;
			cd.loadStart(startdb, items, id);
			RefreshEquipment();
		}
		break;

	case UPDATE_MOUNT:
		if (model == 0)
			return;

		//canvas->timer.Stop();
		if (g_canvas->root->model) {
			delete g_canvas->root->model;
			g_canvas->root->model = 0;
			g_canvas->model = 0;
		}

		if (id == 0) {
			// clearing the mount
			model->charModelDetails.isMounted = false;
			g_canvas->model = model;
			g_canvas->ResetView();
			if (charAtt) {
				charAtt->scale = g_canvas->root->scale;
				charAtt->id = 0;
			}
			g_animControl->UpdateModel(model);
		} else {
			WoWModel *m = new WoWModel(creaturemodels[id-1], false);
			m->isMount = true;

			// TODO: check if model is ridable
			g_canvas->root->model = m;
			g_canvas->model = m;
			g_animControl->UpdateModel(m);
			
			// find the "mount" animation
			/*
			for (size_t i=0; i<model->header.nAnimations; i++) {
				if (model->anims[i].animID == ANIMATION_MOUNT) {
					model->animManager->Stop();
					model->currentAnim = (int)i;
					model->animManager->Set(0,(short)i,0);
					break;
				}
			}
			*/
			// Alfred 2009.7.23 use animLookups to speednup
			if (model->header.nAnimationLookup >= ANIMATION_MOUNT && model->animLookups[ANIMATION_MOUNT] >= 0) {
					model->animManager->Stop();
					model->currentAnim = model->animLookups[ANIMATION_MOUNT];
					model->animManager->SetAnim(0,(short)model->currentAnim,0);
			}
			
			g_canvas->curAtt = g_canvas->root;
			model->charModelDetails.isMounted = true;

			if (charAtt) {
				charAtt->parent = g_canvas->root;
				//charAtt->id = 42;

				// Need to set this - but from what
				// Model data doesn't contain sizes for different race/gender
				// Character data doesn't contain sizes for different mounts
				// possibly some formula that from both models that needs to be calculated.
				// For "Taxi" mounts scale should be 1.0f I think, for now I'll ignore them
				// I really have no idea!  
				if(creaturemodels[id-1].Mid(9,9).IsSameAs(wxT("Kodobeast"), false))
					charAtt->scale = 2.25f;
				else
					charAtt->scale = 1.0f;
				
				//Model *mChar = static_cast<Model*>(charAtt->model);
				//charAtt->scale = m->rad / mChar->rad;

				// Human Male = 2.0346599
				// NE Male = 2.5721216
				// NE Female = 2.2764397

				// RidingFrostSaber = 2.4360743
				// 1.00000

				//canvas->root->scale = 0.5f;
				//Attachment *att = charAtt->addChild("World\\ArtTest\\Boxtest\\xyz.m2", 24, -1);
				//m-> = att->scale;
				//delete att;
			}

			g_canvas->ResetView();
			model->rot = model->pos = Vec3D(0.0f, 0.0f, 0.0f);
			g_canvas->model->rot.x = 0.0f; // mounted characters look better from the side
		}
		//canvas->timer.Start();
		break;

	case UPDATE_CREATURE_ITEM:
		cd.equipment[choosingSlot] = numbers[id];
		//RefreshCreatureItem(choosingSlot);
		//RefreshItem(choosingSlot);
		return;

	case UPDATE_NPC:
		g_modelViewer->LoadNPC(npcs[id].id);

		break;

	case UPDATE_SINGLE_ITEM:
		g_modelViewer->LoadItem(numbers[id]);
		break;

	}

	//  Update controls associated
	g_modelViewer->UpdateControls();
}

void CharControl::OnTabardSpin(wxSpinEvent &event)
{
	if (!g_canvas || !g_canvas->model || g_canvas->model->modelType == MT_NPC)
	{
		wxLogMessage(wxT("Tabard Error: Model Not Present, or can't use a tabard."));
		return;
	}

	switch (event.GetId())
	{
	case ID_TABARD_ICON:
		wxLogMessage(wxT("Tabard Notice: Icon Change."));
		td.Icon = event.GetPosition();
		break;
	case ID_TABARD_ICONCOLOR:
		wxLogMessage(wxT("Tabard Notice: Icon Color Change."));
		td.IconColor = event.GetPosition();
		break;
	case ID_TABARD_BORDER:
	{
		wxLogMessage(wxT("Tabard Notice: Border Change."));
        td.Border = event.GetPosition();
		int maxColor = td.GetMaxBorderColor(td.Border);
		if (maxColor < td.BorderColor)
		{
			td.BorderColor = 0;
			tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(td.BorderColor);
		}
		tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
	}
		break;
	case ID_TABARD_BORDERCOLOR:
		wxLogMessage(wxT("Tabard Notice: Border Color Change."));
		td.BorderColor = event.GetPosition();
		break;
	case ID_TABARD_BACKGROUND:
		wxLogMessage(wxT("Tabard Notice: Background Color Change."));
		td.Background = event.GetPosition();
		break;
	}

	for (size_t i=0; i<NUM_TABARD_BTNS; i++)
	  spinTbLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));

	LOG_INFO << "Current tabard config :"
	           << "Icon" << td.Icon
	           << "IconColor" << td.IconColor
	           << "Border" << td.Border
	           << "BorderColor" << td.BorderColor
	           << "Background" << td.Background;

	RefreshModel();
}



const wxString CharControl::selectCharModel()
{
/* // Alfred 2009.07.21 called by OnMount, but not complete
	wxArrayString arr;
	std::vector<int> ids;
	for (CharRacesDB::Iterator it = racedb.begin(); it != racedb.end(); ++it) {
		char buf[64];
		sprintf(buf,"%s Male", it->getString(CharRacesDB::FullName+langOffset).mb_str());
		arr.Add(buf);
		sprintf(buf,"%s Female", it->getString(CharRacesDB::FullName+langOffset).mb_str());
		arr.Add(buf);
		ids.push_back(it->getInt(CharRacesDB::RaceID));
	}
	wxSingleChoiceDialog dialog(this, wxT("Choose a character model"), wxT("Races"), arr);
	if (dialog.ShowModal() == wxID_OK) {
		int sel = dialog.GetSelection();
		int raceid = ids[sel >> 1];
		int gender = sel & 1;
		string genderStr = gender ? "Female" : "Male";
		try {
			CharRacesDB::Record r = racedb.getById(raceid);
			wxString path = wxT("Character\\");
			path += r.getString(CharRacesDB::Name).mb_str();
			path += "\\" + genderStr + "\\";
			path += r.getString(CharRacesDB::Name).mb_str();
			path += genderStr + ".m2";
			return path;
		} catch (CharRacesDB::NotFound) {
			return ""; // wtf
		}
	}
	
*/
	return wxT("");
}

void CharControl::onEvent(Event *)
{
  RefreshModel();
}

