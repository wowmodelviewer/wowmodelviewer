#include "charcontrol.h"

#include <wx/combobox.h>
#include <wx/spinbutt.h>
#include <wx/txtstrm.h>

#include "Attachment.h"
#include "globalvars.h"
#include "GameDatabase.h"
#include "RaceInfos.h"
#include "logger/Logger.h"
#include "itemselection.h"
#include "modelviewer.h"
#include "util.h"
#include "WoWModel.h"
#include "wow_enums.h"

CharSlots slotOrder[] = {
	CS_SHIRT,
	CS_HEAD,
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
IMPLEMENT_CLASS(CharControl, wxWindow)

BEGIN_EVENT_TABLE(CharControl, wxWindow)
	EVT_SPIN(ID_TABARD_ICON, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_ICONCOLOR, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BORDER, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BORDERCOLOR, CharControl::OnTabardSpin)
	EVT_SPIN(ID_TABARD_BACKGROUND, CharControl::OnTabardSpin)

	EVT_BUTTON(ID_MOUNT, CharControl::OnButton)

	EVT_BUTTON(ID_EQUIPMENT + CS_HEAD, CharControl::OnButton)
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

	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_HEAD, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_SHOULDER, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_SHIRT, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_CHEST, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_BELT, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_PANTS, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_BOOTS, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_BRACERS, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_GLOVES, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_CAPE, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_HAND_RIGHT, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_HAND_LEFT, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_QUIVER, CharControl::OnItemLevelChange)
	EVT_COMBOBOX(ID_EQUIPMENT + 1000 + CS_TABARD, CharControl::OnItemLevelChange)


END_EVENT_TABLE()

CharControl::CharControl(wxWindow* parent, wxWindowID id)
{
	LOG_INFO << "Creating Char Control...";

	if(Create(parent, id, wxDefaultPosition, wxSize(100,700), 0, wxT("CharControl")) == false) {
		LOG_ERROR << "Failed to create a window frame for the Character Control!";
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
	wxFlexGridSizer *gs2 = new wxFlexGridSizer(3, 5, 5);
	gs2->AddGrowableCol(1);

	#define ADD_CONTROLS(type, caption) \
	{ \
	gs2->Add(buttons[type]=new wxButton(this, ID_EQUIPMENT + type, caption)); \
	gs2->Add(levelboxes[type]=new wxComboBox(this, ID_EQUIPMENT + 1000 + type, caption)); \
	levelboxes[type]->SetMinSize(wxSize(15, -1)); \
	levelboxes[type]->SetMaxSize(wxSize(15, -1)); \
	gs2->Add(labels[type]=new wxStaticText(this, -1, _("---- None ----"))); \
  }
	
	ADD_CONTROLS(CS_HEAD, _("Head"))
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
	top->Add(new wxButton(this, ID_MOUNT, _("Mount / dismount")), wxSizerFlags(1).Align(wxALIGN_CENTRE).Border(wxTOP, 10));

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
	if(!model)
		return true;

	model->charTex = 0;
	model->hairTex = 0;
	model->furTex = 0;
	model->capeTex = 0;
	model->gobTex = 0;

	model->td.showCustom = false;
	model->bSheathe = false;

	// set max values for custom tabard
	model->td.maxBackground = model->td.GetMaxBackground();
	model->td.maxBorder = model->td.GetMaxBorder();
	model->td.maxBorderColor = model->td.GetMaxBorderColor(0);
	model->td.maxIcon = model->td.GetMaxIcon();
	model->td.maxIconColor = model->td.GetMaxIconColor(0);

	// Set default eyeglow
	model->cd.eyeGlowType = EGT_DEFAULT;

	return true;
}

//void CharControl::UpdateModel(Model *m)
void CharControl::UpdateModel(Attachment *a)
{
	if (!a)
		return;

	charAtt = a;
	model = dynamic_cast<WoWModel*>(charAtt->model());

	Init();

	RaceInfos infos;
	if(RaceInfos::getCurrent(model->name().toStdString(), infos)) // fails if it is a creature
	{
	  cdFrame->Enable(true);
	  tabardSpins[SPIN_TABARD_ICON]->Enable(true);
	  tabardSpins[SPIN_TABARD_ICONCOLOR]->Enable(true);
	  tabardSpins[SPIN_TABARD_BORDER]->Enable(true);
	  tabardSpins[SPIN_TABARD_BORDERCOLOR]->Enable(true);
	  tabardSpins[SPIN_TABARD_BACKGROUND]->Enable(true);
	  model->cd.showEars = true;
	  model->cd.showHair = true;
	  model->cd.showFacialHair = true;
	  model->cd.showUnderwear = true;
	  model->cd.attach(this);

	  cdFrame->setModel(model->cd);
	  // The following isn't actually needed,
	  // pretty sure all this gets taken care of by TextureManager and CharTexture
	  model->charTex = 0;
	  if (model->charTex==0)
	    glGenTextures(1, &model->charTex);

	  //model->cd.reset();
	  model->td.showCustom = false;

	  // hide most geosets
	  for (size_t i=0; i<model->geosets.size(); i++) {
	    model->showGeosets[i] = (model->geosets[i].id==0);
	  }

	  g_modelViewer->charMenu->Check(ID_SHOW_FEET, 0);

	  cdFrame->refresh();

	  model->td.Icon = randint(0, model->td.maxIcon);
	  model->td.IconColor = randint(0, model->td.maxIconColor);
	  model->td.Border = randint(0, model->td.maxBorder);
	  int maxColor = model->td.GetMaxBorderColor(model->td.Border);
	  model->td.BorderColor = randint(0, maxColor);
	  model->td.Background = randint(0, model->td.maxBackground);

	  tabardSpins[SPIN_TABARD_ICON]->SetValue(model->td.Icon);
	  tabardSpins[SPIN_TABARD_ICONCOLOR]->SetValue(model->td.IconColor);
	  tabardSpins[SPIN_TABARD_BORDER]->SetValue(model->td.Border);
	  tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.BorderColor);
	  tabardSpins[SPIN_TABARD_BACKGROUND]->SetValue(model->td.Background);

	  tabardSpins[SPIN_TABARD_ICON]->SetRange(0, model->td.maxIcon);
	  tabardSpins[SPIN_TABARD_ICONCOLOR]->SetRange(0, model->td.maxIconColor);
	  tabardSpins[SPIN_TABARD_BORDER]->SetRange(0, model->td.maxBorder);
	  tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
	  tabardSpins[SPIN_TABARD_BACKGROUND]->SetRange(0, model->td.maxBackground);

	  //for (size_t i=0; i<NUM_SPIN_BTNS; i++)
	  //	spins[i]->Refresh(false);
	  for (size_t i=0; i<NUM_TABARD_BTNS; i++) {
	    tabardSpins[i]->Refresh(false);
	    spinTbLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));
	  }
	  //for (size_t i=0; i<NUM_SPIN_BTNS; i++)
	  //		spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));
	  if (useRandomLooks)
	      cdFrame->randomiseChar();
	}
	else // creature
	{
	  cdFrame->Enable(false);
	  tabardSpins[SPIN_TABARD_ICON]->Enable(false);
	  tabardSpins[SPIN_TABARD_ICONCOLOR]->Enable(false);
	  tabardSpins[SPIN_TABARD_BORDER]->Enable(false);
	  tabardSpins[SPIN_TABARD_BORDERCOLOR]->Enable(false);
	  tabardSpins[SPIN_TABARD_BACKGROUND]->Enable(false);
	}

	for (size_t i=0; i<NUM_CHAR_SLOTS; i++)
	{
	  WoWItem * item = model->getItem((CharSlots)i);
	  if(item)
	  {
	    if(buttons[i])
	      buttons[i]->Enable(true);
	  }
	  else
	  {
	    if(buttons[i])
	      buttons[i]->Enable(false);
	  }

		if (labels[i])
		{
			labels[i]->SetLabel(_("---- None ----"));
			labels[i]->SetForegroundColour(*wxBLACK);
		}
		if(levelboxes[i])
		{
		  levelboxes[i]->Enable(false);
		}
	}
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
		model->bSheathe = event.IsChecked();
	else if (ID==ID_SHOW_FEET) 
		model->cd.showFeet = event.IsChecked();
	else if (ID==ID_CHAREYEGLOW_NONE)
		model->cd.eyeGlowType = EGT_NONE;
	else if (ID==ID_CHAREYEGLOW_DEFAULT)
		model->cd.eyeGlowType = EGT_DEFAULT;
	else if (ID==ID_CHAREYEGLOW_DEATHKNIGHT)
		model->cd.eyeGlowType = EGT_DEATHKNIGHT;
	else if(ID==ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS)
	  model->cd.autoHideGeosetsForHeadItems = event.IsChecked();

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
	      labels[i]->SetLabel(CSConv(item->name()));
	      labels[i]->SetForegroundColour(ItemQualityColour(item->quality()));

	      // refresh level combo box
	      levelboxes[i]->Clear();
	      if(item->nbLevels() > 1)
	      {
	        levelboxes[i]->Enable(true);
	        for(unsigned int level = 0 ; level < item->nbLevels() ; level++)
	          levelboxes[i]->Append(wxString::Format(wxT("%i"),level));
	      }
	      else
	      {
	        levelboxes[i]->Enable(false);
	      }
	    }

	    if(item && (i == CS_TABARD) && (model->td.showCustom == true))
	    {
	      tabardSpins[SPIN_TABARD_ICON]->SetValue(model->td.Icon);
	      tabardSpins[SPIN_TABARD_ICONCOLOR]->SetValue(model->td.IconColor);
	      tabardSpins[SPIN_TABARD_BORDER]->SetValue(model->td.Border);
	      tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.BorderColor);
	      tabardSpins[SPIN_TABARD_BACKGROUND]->SetValue(model->td.Background);

	      for (size_t i=0; i<NUM_TABARD_BTNS; i++) {
	        tabardSpins[i]->Refresh(false);
	        spinTbLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));
	      }
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

void CharControl::OnItemLevelChange(wxCommandEvent& event)
{
  for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
  {
    if (levelboxes[i] && (wxComboBox*)event.GetEventObject()==levelboxes[i])
    {
      WoWItem * item = model->getItem((CharSlots)i);
      if(item)
      {
        item->setLevel(levelboxes[i]->GetSelection());
        g_modelViewer->UpdateControls();
      }
      break;
    }
  }
}

void CharControl::RefreshModel()
{
	model->hairTex = 0;
	model->furTex = 0;
	model->gobTex = 0;
	model->capeTex = 0;
	bool showScalp = true;

	// Reset geosets
	for (size_t i=0; i<NUM_GEOSETS; i++) 
		model->cd.geosets[i] = 1;
	model->cd.geosets[CG_GEOSET100] = model->cd.geosets[CG_GEOSET200] = model->cd.geosets[CG_GEOSET300] = 0;

	// show ears, if toggled
	if (model->cd.showEars)
		model->cd.geosets[CG_EARS] = 2;

	RaceInfos infos;
	if(!RaceInfos::getCurrent(model->name().toStdString(), infos))
	  return;

	model->tex.reset(infos.textureLayoutID);

	std::vector<std::string> textures = model->cd.getTextureNameForSection(CharDetails::SkinType);

	if(textures.size() > 0)
	  model->tex.addLayer(textures[0].c_str(), CR_BASE, 0);

	if(textures.size() > 1)
	{
		model->furTex = texturemanager.add(textures[1]);
	  model->UpdateTextureList(textures[1], TEXTURE_FUR);
	}

	// Display underwear on the model?
	if (model->cd.showUnderwear)
	{
	  textures = model->cd.getTextureNameForSection(CharDetails::UnderwearType);
	  if(textures.size() > 0)
	    model->tex.addLayer(textures[0].c_str(), CR_PELVIS_UPPER, 1); // pants

	  if(textures.size() > 1)
	    model->tex.addLayer(textures[1].c_str(), CR_TORSO_UPPER, 1); // top

	  // pandaren female => need to display tabard2 geosets (need to find something better...)
	  for (size_t i=0; i<model->geosets.size(); i++)
	  {
	    if(model->geosets[i].id == 1401)
	      model->showGeosets[i] = true;
	  }
	}
	else
	{
	  // de activate pandaren female tabard2 when no underwear
	  for (size_t i=0; i<model->geosets.size(); i++)
	  {
	    if(model->geosets[i].id == 1401)
	      model->showGeosets[i] = false;
	  }
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

	sqlResult hairStyle = GAMEDATABASE.sqlQuery(query);

	if(hairStyle.valid && !hairStyle.values.empty())
	{
	  showScalp = (bool)hairStyle.values[0][1].toInt();
	  unsigned int geosetId = hairStyle.values[0][0].toInt();
	  for (size_t j=0; j<model->geosets.size(); j++) {
	    if (model->geosets[j].id == geosetId)
	      model->showGeosets[j] = model->cd.showHair;
	    else if (model->geosets[j].id >= 1 && model->geosets[j].id < 100)
	      model->showGeosets[j] = false;
	  }
	}
	else
	{
	  LOG_ERROR << "Unable to collect hair style" << model->cd.hairStyle() << "for model" << model->name();
	}

  // Hair texture
	textures = model->cd.getTextureNameForSection(CharDetails::HairType);
  if(textures.size() > 0)
  {
  	model->hairTex = texturemanager.add(textures[0].c_str());
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
  	model->hairTex = 0;
  }

  // select facial geoset(s)
  query = QString("SELECT GeoSet1,GeoSet2,GeoSet3,GeoSet4,GeoSet5 FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
                          .arg(infos.raceid)
                          .arg(infos.sexid)
                          .arg(model->cd.facialHair());

  sqlResult facialHairStyle = GAMEDATABASE.sqlQuery(query);

  if(facialHairStyle.valid && !facialHairStyle.values.empty() && model->cd.showFacialHair)
  {
    LOG_INFO << "Facial GeoSets : " << facialHairStyle.values[0][0].toInt()
        << " " << facialHairStyle.values[0][1].toInt()
        << " " << facialHairStyle.values[0][2].toInt()
        << " " << facialHairStyle.values[0][3].toInt()
        << " " << facialHairStyle.values[0][4].toInt();

    model->cd.geosets[CG_GEOSET100] = facialHairStyle.values[0][0].toInt();
    model->cd.geosets[CG_GEOSET200] = facialHairStyle.values[0][2].toInt();
    model->cd.geosets[CG_GEOSET300] = facialHairStyle.values[0][1].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair style" << model->cd.facialHair() << "for model" << model->name();
  }

	//refresh equipment

  for(WoWModel::iterator it = model->begin();
      it != model->end();
      ++it)
    (*it)->refresh();

	LOG_INFO << "Current Equipment :"
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
	for (size_t j=0; j<model->geosets.size(); j++)
	{
		int id = model->geosets[j].id;
		for (size_t i=1; i<NUM_GEOSETS; i++)
		{
			int a = (int)i*100, b = ((int)i+1) * 100;
			if (a != 1400 && id>a && id<b) // skip tabard2 group (1400) -> buggy pandaren female tabard
				model->showGeosets[j] = (id == (a + model->cd.geosets[i]));
		}
	}

	// gloves - this is so gloves have preference over shirt sleeves.
	if(model->cd.geosets[CG_GLOVES] > 1)
	  model->cd.geosets[CG_WRISTBANDS] = 0;

	WoWItem * headItem = model->getItem(CS_HEAD);

	if( headItem != 0 && headItem->id() != 0 && model->cd.autoHideGeosetsForHeadItems)
	{
	  QString query = QString("SELECT HideGeoset1, HideGeoset2, HideGeoset3, HideGeoset4, HideGeoset5,"
	      "HideGeoset6,HideGeoset7 FROM HelmetGeosetVisData WHERE ID = (SELECT %1 FROM ItemDisplayInfo "
	      "WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %2)))")
	      .arg((infos.sexid == 0)?"HelmetGeosetVis1":"HelmetGeosetVis2")
	      .arg(headItem->id());

	  sqlResult helmetInfos = GAMEDATABASE.sqlQuery(query);

	  if(helmetInfos.valid && !helmetInfos.values.empty())
	  {
	    // hair styles
	    if(helmetInfos.values[0][0].toInt() != 0)
	    {
	      for (size_t i=0; i<model->geosets.size(); i++)
	      {
	        int id = model->geosets[i].id;
	        if(id > 0 && id < 100)
	          model->showGeosets[i] = false;
	      }
	    }

	    // facial 1
	    if(helmetInfos.values[0][1].toInt() != 0 && infos.customization[0] != "FEATURES")
	    {
	      for (size_t i=0; i<model->geosets.size(); i++)
	      {
	        int id = model->geosets[i].id;
	        if(id > 100 && id < 200)
	          model->showGeosets[i] = false;
	      }
	    }

	    // facial 2
	    if(helmetInfos.values[0][2].toInt() != 0  && infos.customization[1] != "FEATURES")
	    {
	      for (size_t i=0; i<model->geosets.size(); i++)
	      {
	        int id = model->geosets[i].id;
	        if(id > 200 && id < 300)
	          model->showGeosets[i] = false;
	      }
	    }

	    // facial 3
	    if(helmetInfos.values[0][3].toInt() != 0)
	    {
	      for (size_t i=0; i<model->geosets.size(); i++)
	      {
	        int id = model->geosets[i].id;
	        if(id > 300 && id < 400)
	          model->showGeosets[i] = false;
	      }
	    }

	    // ears
	    if(helmetInfos.values[0][4].toInt() != 0)
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
	model->tex.compose(model->charTex);

	// set replacable textures
	model->replaceTextures[TEXTURE_BODY] = model->charTex;
	model->replaceTextures[TEXTURE_CAPE] = model->capeTex;
	model->replaceTextures[TEXTURE_HAIR] = model->hairTex;
	model->replaceTextures[TEXTURE_FUR] = model->furTex;
	model->replaceTextures[TEXTURE_GAMEOBJECT1] = model->gobTex;

  // Eye Glow Geosets are ID 1701, 1702, etc.
  size_t egt = model->cd.eyeGlowType;
  int egtId = CG_EYEGLOW*100 + egt + 1;   // CG_EYEGLOW = 17
  for (size_t i=0; i<model->geosets.size(); i++)
  {
    int id = model->geosets[i].id;
    if ((int)(id/100) == CG_EYEGLOW)  // geosets 1700..1799
      model->showGeosets[i] = (id == egtId);
  }
  // Update Eye Glow Menu
  if (egt == EGT_NONE)
    g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
  else if (egt == EGT_DEATHKNIGHT)
    g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
  else
    g_modelViewer->charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
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
	    wxString name = itemClasses.values[i][3].toStdString().c_str();
	    // if empty, fall back to normal one
	    if(name.IsEmpty())
	      name = itemClasses.values[i][2].toStdString().c_str();

	    catnames.Add(CSConv(name));
	    subclasslookup[std::pair<int,int>(itemClasses.values[i][0].toInt(), itemClasses.values[i][1].toInt())] = (int)catnames.size()-1;
	  }
	}

	for (std::vector<ItemRecord>::iterator it = items.items.begin(); it != items.items.end(); ++it) {
		if (type == UPDATE_SINGLE_ITEM)
		{
		  if (it->type == IT_SHOULDER || it->type == IT_SHIELD ||
		      it->type == IT_BOW || it->type == IT_2HANDED || it->type == IT_LEFTHANDED ||
		      it->type == IT_RIGHTHANDED || it->type == IT_OFFHAND || it->type == IT_GUN ||
		      it->type == IT_DAGGER )
		  {
		    choices.Add(it->name.toStdString());
		    numbers.push_back(it->id);
		    quality.push_back(it->quality);

		    subclassesFound.insert(std::pair<int,int>(it->itemclass,it->subclass));
		    cats.push_back(subclasslookup[std::pair<int,int>(it->itemclass, it->subclass)]);
		  }
		}
		else if (correctType((ssize_t)it->type, slot))
		{
			choices.Add(it->name.toStdString());
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
		itemDialog = new CategoryChoiceDialog(this, type, g_modelViewer, wxT("Choose an item"), caption, choices, cats, catnames, &quality, false);
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
	ClearItemDialog();

	std::vector<NumStringPair> items;

	// Adds "none" to select
	NumStringPair n; 
	n.id = -1; 
	n.name = wxT("---- None ----");
	items.push_back(n);

	sqlResult itemSet = GAMEDATABASE.sqlQuery("SELECT ID, Name FROM ItemSet");

	if(itemSet.valid && !itemSet.empty())
	{
		for(int i=0, imax=itemSet.values.size() ; i < imax ; i++)
		{
			NumStringPair p;
			p.id = itemSet.values[i][0].toInt();
			p.name = itemSet.values[i][1].toStdString();
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
	RaceInfos infos;
	if(!RaceInfos::getCurrent(model->name().toStdString(), infos))
		return;

	ClearItemDialog();
	numbers.clear();
	choices.Clear();

	LOG_INFO << "race =" << infos.raceid << "sex = " << infos.sexid;

	QString query = QString("SELECT ChrClasses.name, CSO.ID "
			"FROM CharStartOutfit AS CSO LEFT JOIN ChrClasses on CSO.classID = ChrClasses.ID "
			"WHERE CSO.raceID=%1 AND CSO.sexID=%2").arg(infos.raceid).arg(infos.sexid);

	sqlResult startOutfit = GAMEDATABASE.sqlQuery(query);

	if(startOutfit.valid && !startOutfit.empty())
	{
		for(int i=0, imax=startOutfit.values.size() ; i < imax ; i++)
		{
			choices.Add(startOutfit.values[i][0].toStdString());
			numbers.push_back(startOutfit.values[i][1].toInt());
		}
	}

	itemDialog = new ChoiceDialog(this, UPDATE_START, g_modelViewer, wxT("Choose a class"), wxT("Classes"), choices);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
	itemDialog->Show();
}

bool filterCreatures(wxString fn)
{
	wxString tmp = fn.Lower();
	return (tmp.StartsWith(wxT("crea")) && tmp.EndsWith(wxT("m2")));
}

void CharControl::selectMount()
{
  ClearItemDialog();
  numbers.clear();
  choices.Clear();
  cats.clear();
  catnames.Clear();
  catnames.Add(wxT("Player mounts"));
  catnames.Add(wxT("All Creature/* models"));
  std::vector<NumStringPair> mounts;

  // the "always show first" flag to CategoryChoiceDialog will ensure this is always shown, regardless of category:
  choices.Add(_("---- None ----"));
  cats.push_back(0);
  numbers.push_back(-1);

  // Proper player mounts:
  sqlResult mountQuery = GAMEDATABASE.sqlQuery(
                         "SELECT Mount.DisplayID, Mount.Name FROM Mount");
  if(mountQuery.valid && !mountQuery.empty())
  {
    for(int i = 0, imax = mountQuery.values.size(); i < imax; i++)
    {
      NumStringPair p;
      p.id = mountQuery.values[i][0].toInt();
      p.name = mountQuery.values[i][1].toStdString();
      mounts.push_back(p);
    }
  }
  std::sort(mounts.begin(), mounts.end());
  for (std::vector<NumStringPair>::iterator it = mounts.begin(); it != mounts.end(); it++)
  {
    choices.Add(it->name);
    numbers.push_back(it->id);
    cats.push_back(0);
  }

  // All models from Creature/
  if (creaturemodels.empty())
  {
    sqlResult creatureQuery = GAMEDATABASE.sqlQuery(
                              "SELECT name, path FROM FileData "
                              "WHERE path LIKE 'creature%' "
                              "AND name LIKE '%.m2' COLLATE NOCASE "
                              "ORDER BY LOWER(path), LOWER(name)");
    if(creatureQuery.valid && !creatureQuery.empty())
    {
      for(int i = 0, imax = creatureQuery.values.size(); i < imax; i++)
      {
        std::string path, name;
        name = creatureQuery.values[i][0].toLower().toStdString();
        path = creatureQuery.values[i][1].toLower().toStdString() + name;
        creaturemodels.push_back(wxString(path));
      }
    }
  }
  for (size_t i = 0; i < creaturemodels.size(); i++) 
  {
    choices.Add(creaturemodels[i].substr(9,string::npos));  // remove "creature/" bit for readability
    numbers.push_back(i);
    cats.push_back(1);
  }

  itemDialog = new CategoryChoiceDialog(this, UPDATE_MOUNT, g_modelViewer, wxT("Choose a mount"),
                                        wxT("Mounts"), choices, cats, catnames, 0, true);
  itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4,64));
  itemDialog->Check(1, false);
  itemDialog->DoFilter();
  itemDialog->Show();
  const int w = 250;
  itemDialog->SetSizeHints(w,-1,-1,-1,-1,-1);
  itemDialog->SetSize(w, -1); 
  this->itemDialog = itemDialog;
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
	    typeLookup[npccats.values[i][0].toInt()] = (int)catnames.size()-1;
	  }
	}

	std::vector<int> typesFound;

	for (std::vector<NPCRecord>::iterator it=npcs.begin();  it!=npcs.end(); ++it)
	{
		if (it->model > 0)
		{
			choices.Add(it->name.toStdString());
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
  switch (type)
  {
	case UPDATE_ITEM:
	{
	  WoWItem * item = model->getItem((CharSlots)choosingSlot);
	  if(item)
	  {
	    item->setId(numbers[id]);

		  labels[choosingSlot]->SetLabel(CSConv(item->name()));
		  labels[choosingSlot]->SetForegroundColour(ItemQualityColour(item->quality()));

		  // refresh level combo box
		  levelboxes[choosingSlot]->Clear();
		  if(item->nbLevels() > 1)
		  {
		    levelboxes[choosingSlot]->Enable(true);
		    for(unsigned int i = 0 ; i < item->nbLevels() ; i++)
		      levelboxes[choosingSlot]->Append(wxString::Format(wxT("%i"),i));
		  }
		  else
		  {
		    levelboxes[choosingSlot]->Enable(false);
		  }
	  }
		break;
	}
	case UPDATE_SET:
	{
		id = numbers[id];

		if (id && model)
		{
		  QString query = QString("SELECT item1, item2, item3, item4, item5, "
		  		"item6, item7,	item8 FROM ItemSet WHERE ID = %1").arg(id);

			sqlResult itemSet = GAMEDATABASE.sqlQuery(query);

			if(itemSet.valid && !itemSet.empty())
			{
				// reset previously equipped items

			  for(WoWModel::iterator it = model->begin();
			      it != model->end();
			      ++it)
			    (*it)->setId(0);

				for(unsigned i = 0 ; i < 8; i++)
					tryToEquipItem(itemSet.values[0][i].toInt());

				RefreshEquipment();
				RefreshModel();
			}
		}
		break;
	}
	case UPDATE_START:
		id = numbers[id];

		if (id && model)
		{
			QString query = QString("SELECT CSO.iitem1, CSO.iitem2, CSO.iitem3, CSO.iitem4, CSO.iitem5,"
					"CSO.iitem6, CSO.iitem6, CSO.iitem7, CSO.iitem8, CSO.iitem9, CSO.iitem10, CSO.iitem11,"
					"CSO.iitem12, CSO.iitem13, CSO.iitem14, CSO.iitem15, CSO.iitem16, CSO.iitem17, CSO.iitem18,"
					"CSO.iitem19, CSO.iitem20, CSO.iitem21, CSO.iitem22, CSO.iitem23, CSO.iitem24 "
					"FROM CharStartOutfit AS CSO WHERE CSO.ID=%1").arg(id);

			sqlResult startOutfit = GAMEDATABASE.sqlQuery(query);

			if(startOutfit.valid && !startOutfit.empty())
			{
				// reset previously equipped items
			  for(WoWModel::iterator it = model->begin();
			      it != model->end();
			      ++it)
			    (*it)->setId(0);

				for(unsigned i = 0 ; i < 24; i++)
				{
					tryToEquipItem(startOutfit.values[0][i].toInt());
				}

				RefreshEquipment();
				RefreshModel();
			}
		}
		break;

    case UPDATE_MOUNT:
    {
      TextureGroup grp;
      std::string modelName;
      WoWModel *m;

      if (!model)
        return;
      if (g_canvas->root->model())
      {
        delete g_canvas->root->model();
        g_canvas->root->setModel(0);
        g_canvas->model = 0;
      }
      if (numbers[id] < 0)  // The user selected "None". Remove existing mount.
      {
        // clearing the mount
        model->charModelDetails.isMounted = false;
        g_canvas->model = model;
        g_canvas->ResetView();
        if (charAtt)
        {
          charAtt->scale = g_canvas->root->scale;
          charAtt->id = 0;
        }
        g_animControl->UpdateModel(model);
        break;
      }

      // Sheathe weapons:
      model->bSheathe = true;
      RefreshEquipment();
      RefreshModel();
      g_modelViewer->charMenu->Check(ID_SHEATHE, 1);

      if (cats[id] == 0) // create proper mount from model ID
      {
        int morphID = numbers[id];
        // Only dealing with Creature/ models (for now), so don't need to worry about CreatureDisplayInfoExtra
        QString query = QString("SELECT FileData.path, FileData.name, CreatureDisplayInfo.Texture1, "
	    "CreatureDisplayInfo.Texture2, CreatureDisplayInfo.Texture3 FROM CreatureDisplayInfo "
	    "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
	    "LEFT JOIN FileData ON CreatureModelData.FileDataID = FileData.ID WHERE CreatureDisplayInfo.ID = %1;").arg(morphID);

        sqlResult mountQuery = GAMEDATABASE.sqlQuery(query);
        if (!mountQuery.valid || mountQuery.empty())
          break;
        std::string path = mountQuery.values[0][0].toStdString();
        modelName = path + mountQuery.values[0][1].toStdString();
        int count = 0;
        for(int i = 0; i < 3; i++)
        {
          std::string tex = "";
          std::string fname = mountQuery.values[0][i+2].toStdString();
          if (!fname.empty())
          {
            tex = path + fname + ".blp";
            count++;
          }
          grp.tex[i] = tex;
        }
        grp.base = TEXTURE_GAMEOBJECT1;
        grp.count = count;
      }
      else if (cats[id] == 1) // create mount from any old creature model file name
      {
        modelName = creaturemodels[numbers[id]].c_str();
        // that's it. No special textures or anything.
      }
      else
        break; // shouldn't happen
      m = new WoWModel(modelName, false);
      m->isMount = true;
      g_canvas->root->setModel(m);
      g_canvas->model = m;
      g_animControl->UpdateModel(m);
      // add specific textures for proper mounts. Must do this after model is updated.
      if (!cats[id] && !grp.tex[0].empty())
        g_animControl->AddSkin(grp);

      model->bSheathe = true;
      RefreshEquipment();

      // Alfred 2009.7.23 use animLookups to speed up
      if (model->header.nAnimationLookup >= ANIMATION_MOUNT &&
          model->animLookups[ANIMATION_MOUNT] >= 0)
      {
        model->animManager->Stop();
        model->currentAnim = model->animLookups[ANIMATION_MOUNT];
        model->animManager->SetAnim(0,(short)model->currentAnim,0);
      }		
      g_canvas->curAtt = g_canvas->root;
      model->charModelDetails.isMounted = true;
      if (charAtt)
      { 
        charAtt->parent = g_canvas->root;
          // Need to set this - but from what
          // Model data doesn't contain sizes for different race/gender
          // Character data doesn't contain sizes for different mounts
          // possibly some formula that from both models that needs to be calculated.
          // For "Taxi" mounts scale should be 1.0f I think, for now I'll ignore them
          // I really have no idea!  
        if (creaturemodels[id-1].Mid(9,9).IsSameAs(wxT("Kodobeast"), false))
          charAtt->scale = 2.25f;
        else
          charAtt->scale = 1.0f;
      }
      g_canvas->ResetView();
      model->rot = model->pos = Vec3D(0.0f, 0.0f, 0.0f);
      g_canvas->model->rot.x = 0.0f; // mounted characters look better from the side
      break;
     }
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
		LOG_ERROR << "Model Not Present, or can't use a tabard.";
		return;
	}

	switch (event.GetId())
	{
	case ID_TABARD_ICON:
		LOG_INFO << "Tabard Notice: Icon Change.";
		model->td.Icon = event.GetPosition();
		break;
	case ID_TABARD_ICONCOLOR:
		LOG_INFO << "Tabard Notice: Icon Color Change.";
		model->td.IconColor = event.GetPosition();
		break;
	case ID_TABARD_BORDER:
	{
		LOG_INFO << "Tabard Notice: Border Change.";
		model->td.Border = event.GetPosition();
		int maxColor = model->td.GetMaxBorderColor(model->td.Border);
		if (maxColor < model->td.BorderColor)
		{
			model->td.BorderColor = 0;
			tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.BorderColor);
		}
		tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
	}
		break;
	case ID_TABARD_BORDERCOLOR:
		LOG_INFO << "Tabard Notice: Border Color Change.";
		model->td.BorderColor = event.GetPosition();
		break;
	case ID_TABARD_BACKGROUND:
		LOG_INFO << "Tabard Notice: Background Color Change.";
		model->td.Background = event.GetPosition();
		break;
	}

	for (size_t i=0; i<NUM_TABARD_BTNS; i++)
	  spinTbLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));

	LOG_INFO << "Current tabard config :"
	           << "Icon" << model->td.Icon
	           << "IconColor" << model->td.IconColor
	           << "Border" << model->td.Border
	           << "BorderColor" << model->td.BorderColor
	           << "Background" << model->td.Background;

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

void CharControl::tryToEquipItem(int id)
{
	if(id == 0)
		return;

	ItemRecord itemr = items.getById(id);
	if(itemr.name != items.items[0].name)
	{
		int itemSlot = itemr.slot();
		if(itemSlot != -1)
		{
			WoWItem * item = model->getItem((CharSlots)itemSlot);
			if(item)
			{
				item->setId(id);
				labels[itemSlot]->SetLabel(CSConv(item->name()));
				labels[itemSlot]->SetForegroundColour(ItemQualityColour(item->quality()));

				// refresh level combo box
				levelboxes[itemSlot]->Clear();
				if(item->nbLevels() > 1)
				{
					levelboxes[itemSlot]->Enable(true);
					for(unsigned int i = 0 ; i < item->nbLevels() ; i++)
						levelboxes[itemSlot]->Append(wxString::Format(wxT("%i"),i));
				}
				else
				{
					levelboxes[itemSlot]->Enable(false);
				}
			}
		}
		else
		{
			LOG_ERROR << "Cannot determine slot for object" << itemr.name;
		}
	}
	else
	{
		LOG_ERROR << "Cannot retrieve item from database (id" << id << ")";
	}
}
