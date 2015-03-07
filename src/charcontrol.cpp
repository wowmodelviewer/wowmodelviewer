#include "../../../charcontrol.h"

#include "globalvars.h"
#include "itemselection.h"
#include "modelviewer.h"
#include "util.h"

#include "logger/Logger.h"

#include "metaclasses/Iterator.h"

#include "CASCFolder.h"
#include "GameDatabase.h"
#include "RaceInfos.h"

#include <wx/txtstrm.h>
#include "next-gen/games/wow/Attachment.h"

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

	cdFrame = new CharDetailsFrame(this);
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
	model->cd.showEars = true;
	model->cd.showHair = true;
	model->cd.showFacialHair = true;
	model->cd.showUnderwear = true;

  model->cd.attach(this);

  cdFrame->setModel(model->cd);

	// The following isn't actually needed, 
	// pretty sure all this gets taken care of by TextureManager and CharTexture
	charTex = 0;
	if (charTex==0) 
		glGenTextures(1, &charTex);

	//model->cd.reset();
	td.showCustom = false;

	// hide most geosets
	for (size_t i=0; i<model->geosets.size(); i++) {
		model->showGeosets[i] = (model->geosets[i].id==0);
	}

	RaceInfos infos;
	RaceInfos::getCurrent(std::string(model->wxname.mb_str()), infos);

	model->cd.race = infos.raceid;
	model->cd.gender = infos.sexid;

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
		model->cd.showUnderwear = event.IsChecked();
	else if (ID==ID_SHOW_HAIR) 
		model->cd.showHair = event.IsChecked();
	else if (ID==ID_SHOW_FACIALHAIR) 
		model->cd.showFacialHair = event.IsChecked();
	else if (ID==ID_SHOW_EARS) 
		model->cd.showEars = event.IsChecked();
	else if (ID==ID_SHEATHE) 
		bSheathe = event.IsChecked();
	else if (ID==ID_SHOW_FEET) 
		model->cd.showFeet = event.IsChecked();
	else if (ID==ID_CHAREYEGLOW_NONE)
		model->cd.eyeGlowType = 0;
	else if (ID==ID_CHAREYEGLOW_DEFAULT)
		model->cd.eyeGlowType = 1;
	else if (ID==ID_CHAREYEGLOW_DEATHKNIGHT)
		model->cd.eyeGlowType = 2;

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
	  if (labels[i])
	  {
	    WoWItem * item = model->getItem((CharSlots)i);
	    if(item)
	    {
	      labels[i]->SetLabel(item->name());
	      labels[i]->SetForegroundColour(ItemQualityColour(item->quality()));
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
	switch(event.GetId())
	{
	case ID_SAVE_EQUIPMENT:
	{
	  wxFileDialog dialog(this, wxT("Save equipment"), dir, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
	  if (dialog.ShowModal()==wxID_OK)
	  {
	    wxString s(dialog.GetPath());
	    model->cd.save(s, &td);

	    // Save directory path
	    dir = dialog.GetDirectory();
	  }
	  break;
	}
	case ID_LOAD_EQUIPMENT:
	{
/*
	  wxFileDialog dialog(this, wxT("Load equipment"), dir, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_OPEN|wxFD_FILE_MUST_EXIST, wxDefaultPosition);
	  if (dialog.ShowModal()==wxID_OK) {
	    wxString s(dialog.GetPath());
	    if (model->cd.load(s, &td)) {
	      spins[SPIN_SKIN_COLOR]->SetValue((int)model->cd.skinColor);
	      spins[SPIN_FACE_TYPE]->SetValue((int)model->cd.faceType);
	      spins[SPIN_HAIR_COLOR]->SetValue((int)model->cd.hairColor);
	      spins[SPIN_HAIR_STYLE]->SetValue((int)model->cd.hairStyle);
	      spins[SPIN_FACIAL_HAIR]->SetValue((int)model->cd.facialHair);
	      for (size_t i=0; i<NUM_SPIN_BTNS; i++)
	        spins[i]->Refresh(false);
	    }
	    RefreshEquipment();

	    // Save directory path
	    dir = dialog.GetDirectory();

	  }
	*/
	  break;
	}
	case ID_CLEAR_EQUIPMENT:
	{
	  for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
	  {
	    WoWItem * item = model->getItem((CharSlots)i);
	    if(item)
	      item->setId(0);
	  }
	  RefreshEquipment();
	  break;
	}
	case ID_LOAD_SET:
	{
	  selectSet();
	  break;
	}
	case ID_LOAD_START:
	{
	  selectStart();
	  break;
	}
	case ID_MOUNT:
	{
	  selectMount();
	  break;
	}
	case ID_CHAR_RANDOMISE:
	{
	  cdFrame->randomiseChar();
	  break;
	}
	default:
	{
	  for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
	  {
	    if (buttons[i] && (wxButton*)event.GetEventObject()==buttons[i])
	    {
	      selectItem(UPDATE_ITEM, i, buttons[i]->GetLabel().GetData());
	      break;
	    }
	  }
	  break;
	}
	}

	RefreshModel();
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
		model->cd.geosets[i] = 1;
	model->cd.geosets[CG_GEOSET100] = model->cd.geosets[CG_GEOSET200] = model->cd.geosets[CG_GEOSET300] = 0;

	// show ears, if toggled
	if (model->cd.showEars)
		model->cd.geosets[CG_EARS] = 2;

	RaceInfos infos;
	if(!RaceInfos::getCurrent(std::string(model->wxname.mb_str()), infos))
	  return;

	model->tex.reset(infos.textureLayoutID);

	std::vector<std::string> textures = model->cd.getTextureNameForSection(CharDetails::SkinType);

	if(textures.size() > 0)
	  model->tex.addLayer(textures[0].c_str(), CR_BASE, 0);

	if(textures.size() > 1)
	{
	  wxString furTexName = textures[1].c_str();
	  furTex = texturemanager.add(furTexName);
	  model->UpdateTextureList(furTexName, TEXTURE_FUR);
	}

	// Display underwear on the model?
	if (model->cd.showUnderwear)
	{
	  textures = model->cd.getTextureNameForSection(CharDetails::UnderwearType);
	  if(textures.size() > 0)
	    model->tex.addLayer(textures[0].c_str(), CR_PELVIS_UPPER, 1); // pants

	  if(textures.size() > 1)
	    model->tex.addLayer(textures[1].c_str(), CR_TORSO_UPPER, 1); // top
	}

	// face
	textures = model->cd.getTextureNameForSection(CharDetails::FaceType);
	if(textures.size() > 0)
	  model->tex.addLayer(textures[0].c_str(), CR_FACE_LOWER, 1);

	if(textures.size() > 1)
	  model->tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 1);

	// facial hair
	textures = model->cd.getTextureNameForSection(CharDetails::FacialHairType);
	if(textures.size() > 0)
	  model->tex.addLayer(textures[0].c_str(), CR_FACE_LOWER, 2);

	if(textures.size() > 1)
	  model->tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 2);

  // select hairstyle geoset(s)
	QString query = QString("SELECT GeoSetID,ShowScalp FROM CharHairGeoSets WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
	                  .arg(infos.raceid)
	                  .arg(infos.sexid)
	                  .arg(model->cd.hairStyle());

	sqlResult hairStyle = GAMEDATABASE.sqlQuery(query.toStdString());

	if(hairStyle.valid && !hairStyle.values.empty())
	{
	  showScalp = (bool)atoi(hairStyle.values[0][1].c_str());
	  unsigned int geosetId = atoi(hairStyle.values[0][0].c_str());
	  for (size_t j=0; j<model->geosets.size(); j++) {
	    if (model->geosets[j].id == geosetId)
	      model->showGeosets[j] = model->cd.showHair;
	    else if (model->geosets[j].id >= 1 && model->geosets[j].id < 100)
	      model->showGeosets[j] = false;
	  }
	}
	else
	{
	  LOG_ERROR << "Unable to collect hair style" << model->cd.hairStyle() << "for model" << model->wxname.c_str();
	}

  // Hair texture
	textures = model->cd.getTextureNameForSection(CharDetails::HairType);
  if(textures.size() > 0)
  {
    hairTex = texturemanager.add(textures[0].c_str());
    model->UpdateTextureList(textures[0].c_str(), TEXTURE_HAIR);

    if(infos.isHD)
    {
      if(!showScalp && textures.size() > 1)
        model->tex.addLayer(textures[1].c_str(), CR_FACE_UPPER, 3);
    }
    else
    {
      if(!showScalp)
      {
        if(textures.size() > 1)
          model->tex.addLayer(textures[1].c_str(), CR_FACE_LOWER, 3);

        if(textures.size() > 2)
          model->tex.addLayer(textures[2].c_str(), CR_FACE_UPPER, 3);
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
                          .arg(model->cd.facialHair());

  sqlResult facialHairStyle = GAMEDATABASE.sqlQuery(query.toStdString());

  if(facialHairStyle.valid && !facialHairStyle.values.empty() && model->cd.showFacialHair)
  {
    LOG_INFO << "Facial GeoSets : " << atoi(facialHairStyle.values[0][0].c_str())
        << " " << atoi(facialHairStyle.values[0][1].c_str())
        << " " << atoi(facialHairStyle.values[0][2].c_str())
        << " " << atoi(facialHairStyle.values[0][3].c_str())
        << " " << atoi(facialHairStyle.values[0][4].c_str());

    model->cd.geosets[CG_GEOSET100] = atoi(facialHairStyle.values[0][0].c_str());
    model->cd.geosets[CG_GEOSET200] = atoi(facialHairStyle.values[0][2].c_str());
    model->cd.geosets[CG_GEOSET300] = atoi(facialHairStyle.values[0][1].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair style" << model->cd.facialHair() << "for model" << model->wxname.c_str();
  }

	//refresh equipment
  Iterator<WoWItem> itemsIt(model);
  for(itemsIt.begin(); !itemsIt.ended(); itemsIt++)
    (*itemsIt)->refresh();

	LOG_INFO << "Current Equipement :"
	         << "Head" << model->getItem(CS_HEAD)->id()
	         << "Shoulder" << model->getItem(CS_SHOULDER)->id()
	         << "Shirt" << model->getItem(CS_SHIRT)->id()
	         << "Chest" << model->getItem(CS_CHEST)->id()
	         << "Belt" << model->getItem(CS_BELT)->id()
	         << "Legs" << model->getItem(CS_PANTS)->id()
	         << "Boots" << model->getItem(CS_BOOTS)->id()
	         << "Bracers" << model->getItem(CS_BRACERS)->id()
	         << "Gloves" << model->getItem(CS_GLOVES)->id()
	         << "Cape" << model->getItem(CS_CAPE)->id()
	         << "Right Hand" << model->getItem(CS_HAND_RIGHT)->id()
	         << "Left Hand" << model->getItem(CS_HAND_LEFT)->id()
	         << "Quiver" << model->getItem(CS_QUIVER)->id()
	         << "Tabard" << model->getItem(CS_TABARD)->id();

	// reset geosets
	for (size_t j=0; j<model->geosets.size(); j++) {
		int id = model->geosets[j].id;

		for (size_t i=1; i<NUM_GEOSETS; i++) {
			int a = (int)i*100, b = ((int)i+1) * 100;
			if (id>a && id<b) 
				model->showGeosets[j] = (id == (a + model->cd.geosets[i]));

		}
	}

	// gloves - this is so gloves have preference over shirt sleeves.
	if(model->cd.geosets[CG_GLOVES] > 1)
	  model->cd.geosets[CG_WRISTBANDS] = 0;

	WoWItem * headItem = model->getItem(CS_HEAD);

	if( headItem != 0 && headItem->id() != 0)
	{
	  QString query = QString("SELECT HideGeoset1, HideGeoset2, HideGeoset3, HideGeoset4, HideGeoset5,"
	      "HideGeoset6,HideGeoset7 FROM HelmetGeosetVisData WHERE ID = (SELECT %1 FROM ItemDisplayInfo "
	      "WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %2)))")
	      .arg((infos.sexid == 0)?"HelmetGeosetVis1":"HelmetGeosetVis2")
	      .arg(headItem->id());

	  sqlResult helmetInfos = GAMEDATABASE.sqlQuery(query.toStdString());

	  if(helmetInfos.valid && !helmetInfos.values.empty())
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
	    if(atoi(helmetInfos.values[0][1].c_str()) != 0 && infos.customization[0] != "FEATURES")
	    {
	      for (size_t i=0; i<model->geosets.size(); i++)
	      {
	        int id = model->geosets[i].id;
	        if(id > 100 && id < 200)
	          model->showGeosets[i] = false;
	      }
	    }

	    // facial 2
	    if(atoi(helmetInfos.values[0][2].c_str()) != 0  && infos.customization[1] != "FEATURES")
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
	model->tex.compose(charTex);
	
	// set replacable textures
	model->replaceTextures[TEXTURE_BODY] = charTex;
	model->replaceTextures[TEXTURE_CAPE] = capeTex;
	model->replaceTextures[TEXTURE_HAIR] = hairTex;
	model->replaceTextures[TEXTURE_FUR] = furTex;
	model->replaceTextures[TEXTURE_GAMEOBJECT1] = gobTex;


	/*
	// Eye Glows
	for(size_t i=0; i<model->passes.size(); i++) {
		ModelRenderPass &p = model->passes[i];
		wxString texName = model->TextureList[p.tex].AfterLast('\\').Lower();

		if (texName.Find(wxT("eyeglow")) == wxNOT_FOUND)
			continue;

		// Regular Eye Glow
		if ((texName.Find(wxT("eyeglow")) != wxNOT_FOUND)&&(texName.Find(wxT("deathknight")) == wxNOT_FOUND)){
			if (model->cd.eyeGlowType == EGT_NONE){					// If No EyeGlow
				model->showGeosets[p.geoset] = false;
			}else if (model->cd.eyeGlowType == EGT_DEATHKNIGHT){		// If DK EyeGlow
				model->showGeosets[p.geoset] = false;
			}else{												// Default EyeGlow, AKA model->cd.eyeGlowType == EGT_DEFAULT
				model->showGeosets[p.geoset] = true;
			}
		}
		// DeathKnight Eye Glow
		if (texName.Find(wxT("deathknight")) != wxNOT_FOUND){
			if (model->cd.eyeGlowType == EGT_NONE){					// If No EyeGlow
				model->showGeosets[p.geoset] = false;
			}else if (model->cd.eyeGlowType == EGT_DATHKNIGHT){		// If DK EyeGlow
				model->showGeosets[p.geoset] = true;
			}else{												// Default EyeGlow, AKA model->cd.eyeGlowType == EGT_DEFAULT
				model->showGeosets[p.geoset] = false;
			}
		}
	}
	// Update Eye Glow Menu
	size_t egt = model->cd.eyeGlowType;
	if (egt == EGT_NONE)
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
	else if (egt == EGT_DEATHKNIGHT)
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
	else
		g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
		*/
}

void CharControl::ClearItemDialog()
{
	if (itemDialog) {
		itemDialog->Show(FALSE);
		itemDialog->Destroy();
		wxDELETE(itemDialog);
	}
}

void CharControl::selectItem(ssize_t type, ssize_t slot, const wxChar *caption)
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
		if ((it->getByte(StartOutfitDB::Race) == model->cd.race) && (it->getByte(StartOutfitDB::Gender) == model->cd.gender)) {
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
	{
	  WoWItem * item = model->getItem((CharSlots)choosingSlot);
	  if(item)
	  {
	    item->setId(numbers[id]);
		  labels[choosingSlot]->SetLabel(item->name());
		  labels[choosingSlot]->SetForegroundColour(ItemQualityColour(item->quality()));
	  }
		break;
	}
	case UPDATE_SET:
		id = numbers[id];

		if (id) {
		  // @TODO : to repair
		  /*
			for (size_t i=0; i<NUM_CHAR_SLOTS; i++) {
				//if (i!=CS_HAND_LEFT && i!=CS_HAND_RIGHT) 
				model->cd.equipment[i] = 0;
			}
			*/


			model->cd.loadSet(setsdb, items, id);
			RefreshEquipment();
			RefreshModel();
		}
		break;

	case UPDATE_START:
		id = numbers[id];

		if (id) {
		  // @TODO : to repair
			//for (size_t i=0; i<NUM_CHAR_SLOTS; i++) model->cd.equipment[i] = 0;
			model->cd.loadStart(startdb, items, id);
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
		//model->cd.equipment[choosingSlot] = numbers[id];
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

	WoWItem * tabardModel = model->getItem(CS_TABARD);
	if(tabardModel)
	  tabardModel->load();

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

