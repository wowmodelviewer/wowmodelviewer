#include "charcontrol.h"
#include <wx/combobox.h>
#include <wx/spinbutt.h>
#include <wx/txtstrm.h>
#include "Attachment.h"
#include "Game.h"
#include "globalvars.h"
#include "RaceInfos.h"
#include "itemselection.h"
#include "modelviewer.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWModel.h"
#include "wow_enums.h"
#include "logger/Logger.h"

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

	if (Create(parent, id, wxDefaultPosition, wxSize(100, 700), 0, wxT("CharControl")) == false)
	{
		LOG_ERROR << "Failed to create a window frame for the Character Control!";
		return;
	}

	auto* top = new wxFlexGridSizer(1);

	cdFrame = new CharDetailsFrame(this);
	top->Add(cdFrame, wxSizerFlags(1).Align(wxALIGN_CENTER));

	for (ssize_t i = 0; i < NUM_CHAR_SLOTS; i++)
	{
		buttons[i] = nullptr;
		labels[i] = nullptr;
	}

	top->Add(new wxStaticText(this, -1, _("Equipment"), wxDefaultPosition, wxSize(200, 20), wxALIGN_CENTRE),
	         wxSizerFlags().Border(wxTOP, 5));
	auto* gs2 = new wxFlexGridSizer(3, 5, 5);
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

	top->Add(gs2, wxSizerFlags(1).Align(wxALIGN_CENTER));

	// Create our tabard customisation spin buttons
	auto* gs3 = new wxGridSizer(3);
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

	top->Add(new wxStaticText(this, -1, _("Tabard details")), wxSizerFlags(1).Align(wxALIGN_CENTRE).Border(wxALL, 1));
	top->Add(gs3, wxSizerFlags(1).Align(wxALIGN_CENTER));
	top->Add(new wxButton(this, ID_MOUNT, _("Mount / dismount")),
	         wxSizerFlags(1).Align(wxALIGN_CENTRE).Border(wxTOP, 10));

	//p->SetSizer(top);

	top->SetSizeHints(this);
	Show(true);
	//SetAutoLayout(true);
	SetSizer(top);
	Layout();
	FitInside(); // ask the sizer about the needed size
	SetScrollRate(5, 5);

	choosingSlot = 0;
	itemDialog = nullptr;
	model = nullptr;
	charAtt = nullptr;
}

CharControl::~CharControl() = default;

bool CharControl::Init()
{
	if (!model)
		return true;

	model->td.showCustom = false;
	model->bSheathe = false;

	// Set default eyeglow
	model->cd.eyeGlowType = EGT_DEFAULT;

	return true;
}

//void CharControl::UpdateModel(Model *m)
void CharControl::UpdateModel(Attachment* a)
{
	if (!a)
		return;

	charAtt = a;
	model = dynamic_cast<WoWModel*>(charAtt->model());

	Init();

	const auto& infos = model->infos;
	if (infos.raceID != -1) // fails if it is a creature
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

		cdFrame->setModel(model);
		// The following isn't actually needed,
		// pretty sure all this gets taken care of by TextureManager and CharTexture

		//model->cd.reset();
		model->td.showCustom = false;

		g_modelViewer->charMenu->Check(ID_SHOW_FEET, false);

		model->td.setIcon(randint(0, model->td.GetMaxIcon()));
		model->td.setIconColor(randint(0, model->td.GetMaxIconColor(model->td.getIcon())));
		model->td.setBorder(randint(0, model->td.GetMaxBorder()));
		int maxColor = model->td.GetMaxBorderColor(model->td.getBorder());
		model->td.setBorderColor(randint(0, maxColor));
		model->td.setBackground(randint(0, model->td.GetMaxBackground()));

		tabardSpins[SPIN_TABARD_ICON]->SetValue(model->td.getIcon());
		tabardSpins[SPIN_TABARD_ICONCOLOR]->SetValue(model->td.getIconColor());
		tabardSpins[SPIN_TABARD_BORDER]->SetValue(model->td.getBorder());
		tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.getBorderColor());
		tabardSpins[SPIN_TABARD_BACKGROUND]->SetValue(model->td.getBackground());

		tabardSpins[SPIN_TABARD_ICON]->SetRange(0, model->td.GetMaxIcon());
		tabardSpins[SPIN_TABARD_ICONCOLOR]->SetRange(0, model->td.GetMaxIconColor(model->td.getIcon()));
		tabardSpins[SPIN_TABARD_BORDER]->SetRange(0, model->td.GetMaxBorder());
		tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
		tabardSpins[SPIN_TABARD_BACKGROUND]->SetRange(0, model->td.GetMaxBackground());

		//for (size_t i=0; i<NUM_SPIN_BTNS; i++)
		//  spins[i]->Refresh(false);
		for (size_t i = 0; i < NUM_TABARD_BTNS; i++)
		{
			tabardSpins[i]->Refresh(false);
			spinTbLabels[i]->SetLabel(
				wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));
		}
		//for (size_t i=0; i<NUM_SPIN_BTNS; i++)
		//    spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));
		if (useRandomLooks)
			model->cd.randomise();
		else
			model->cd.reset();
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

	for (size_t i = 0; i < NUM_CHAR_SLOTS; i++)
	{
		WoWItem* item = model->getItem((CharSlots)i);
		if (item)
		{
			if (buttons[i])
				buttons[i]->Enable(true);
		}
		else
		{
			if (buttons[i])
				buttons[i]->Enable(false);
		}

		if (labels[i])
		{
			labels[i]->SetLabel(_("---- None ----"));
			labels[i]->SetForegroundColour(*wxBLACK);
		}
		if (levelboxes[i])
		{
			levelboxes[i]->Enable(false);
		}
	}
	RefreshModel();
}

void CharControl::OnCheck(wxCommandEvent& event)
{
	int ID = event.GetId();
	if (ID == ID_SHOW_UNDERWEAR)
		model->cd.showUnderwear = event.IsChecked();
	else if (ID == ID_SHOW_HAIR)
		model->cd.showHair = event.IsChecked();
	else if (ID == ID_SHOW_FACIALHAIR)
		model->cd.showFacialHair = event.IsChecked();
	else if (ID == ID_SHOW_EARS)
		model->cd.showEars = event.IsChecked();
	else if (ID == ID_SHEATHE)
		model->bSheathe = event.IsChecked();
	else if (ID == ID_SHOW_FEET)
		model->cd.showFeet = event.IsChecked();
	else if (ID == ID_CHAREYEGLOW_NONE)
		model->cd.eyeGlowType = EGT_NONE;
	else if (ID == ID_CHAREYEGLOW_DEFAULT)
		model->cd.eyeGlowType = EGT_DEFAULT;
	else if (ID == ID_CHAREYEGLOW_DEATHKNIGHT)
		model->cd.eyeGlowType = EGT_DEATHKNIGHT;
	else if (ID == ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS)
		model->cd.autoHideGeosetsForHeadItems = event.IsChecked();

	//  Update controls associated
	RefreshEquipment();
	g_modelViewer->UpdateControls();
	// ----
}

bool slotHasModel(size_t i)
{
	return (i == CS_HEAD || i == CS_SHOULDER || i == CS_HAND_LEFT || i == CS_HAND_RIGHT || i == CS_QUIVER);
}

void CharControl::RefreshEquipment()
{
	for (ssize_t i = 0; i < NUM_CHAR_SLOTS; i++)
	{
		if (labels[i])
		{
			WoWItem* item = model->getItem((CharSlots)i);
			if (item)
			{
				labels[i]->SetLabel(item->name().toStdWString());
				labels[i]->SetForegroundColour(ItemQualityColour(item->quality()));

				// refresh level combo box
				levelboxes[i]->Clear();
				if (item->nbLevels() > 1)
				{
					levelboxes[i]->Enable(true);
					for (unsigned int level = 0; level < item->nbLevels(); level++)
						levelboxes[i]->Append(wxString::Format(wxT("%i"), level));
				}
				else
				{
					levelboxes[i]->Enable(false);
				}
			}

			if (item && (i == CS_TABARD) && (model->td.showCustom == true))
			{
				tabardSpins[SPIN_TABARD_ICON]->SetValue(model->td.getIcon());
				tabardSpins[SPIN_TABARD_ICONCOLOR]->SetValue(model->td.getIconColor());
				tabardSpins[SPIN_TABARD_BORDER]->SetValue(model->td.getBorder());
				tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.getBorderColor());
				tabardSpins[SPIN_TABARD_BACKGROUND]->SetValue(model->td.getBackground());

				for (size_t I = 0; I < NUM_TABARD_BTNS; I++)
				{
					tabardSpins[I]->Refresh(false);
					spinTbLabels[I]->SetLabel(
						wxString::Format(wxT("%i / %i"), tabardSpins[I]->GetValue(), tabardSpins[I]->GetMax()));
				}
			}
		}
	}
}

void CharControl::OnButton(wxCommandEvent& event)
{
	// This stores are equipment directory path in session
	static wxString dir = cfgPath.BeforeLast(SLASH);

	//if (dir.Last() != '\\')
	//  dir.Append('\\');
	switch (event.GetId())
	{
	case ID_CLEAR_EQUIPMENT:
		{
			for (ssize_t i = 0; i < NUM_CHAR_SLOTS; i++)
			{
				WoWItem* item = model->getItem((CharSlots)i);
				if (item)
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
	default:
		{
			for (ssize_t i = 0; i < NUM_CHAR_SLOTS; i++)
			{
				if (buttons[i] && (wxButton*)event.GetEventObject() == buttons[i])
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
	for (ssize_t i = 0; i < NUM_CHAR_SLOTS; i++)
	{
		if (levelboxes[i] && (wxComboBox*)event.GetEventObject() == levelboxes[i])
		{
			WoWItem* item = model->getItem((CharSlots)i);
			if (item)
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
	if (!model)
		return;

	model->refresh();

	// Eye Glow Geosets are ID 1701, 1702, etc.
	size_t egt = model->cd.eyeGlowType;
	int egtId = CG_EYEGLOW * 100 + egt + 1; // CG_EYEGLOW = 17

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
	if (itemDialog)
	{
		itemDialog->Show(FALSE);
		itemDialog->Destroy();
		wxDELETE(itemDialog);
	}
}

void CharControl::selectItem(ssize_t type, ssize_t slot, const wxChar* caption)
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
	std::set<std::pair<int, int>> subclassesFound;

	//std::cout << "item db size = " << items.items.size() << std::endl;

	std::map<std::pair<int, int>, int> subclasslookup;

	sqlResult itemClasses = GAMEDATABASE.sqlQuery(
		"SELECT ClassID, SubClassID, DisplayName_Lang, VerboseName_Lang FROM ItemSubClass");

	if (itemClasses.valid && !itemClasses.empty())
	{
		for (auto& value : itemClasses.values)
		{
			// first set verbose name
			wxString name = value[3].toStdWString();
			// if empty, fall back to normal one
			if (name.IsEmpty())
				name = value[2].toStdWString();

			catnames.Add(name);
			subclasslookup[std::pair<int, int>(value[0].toInt(), value[1].toInt())] = (
				int)catnames.size() - 1;
		}
	}

	for (auto& item : items.items)
	{
		if (type == UPDATE_SINGLE_ITEM)
		{
			if (item.type == IT_SHOULDER || item.type == IT_SHIELD ||
				item.type == IT_BOW || item.type == IT_2HANDED || item.type == IT_LEFTHANDED ||
				item.type == IT_RIGHTHANDED || item.type == IT_OFFHAND || item.type == IT_GUN ||
				item.type == IT_DAGGER)
			{
				choices.Add(getItemName(item).toStdWString());
				numbers.push_back(item.id);
				quality.push_back(item.quality);

				subclassesFound.insert(std::pair<int, int>(item.itemclass, item.subclass));
				cats.push_back(subclasslookup[std::pair<int, int>(item.itemclass, item.subclass)]);
			}
		}
		else if (correctType((ssize_t)item.type, slot))
		{
			choices.Add(getItemName(item).toStdWString());
			numbers.push_back(item.id);
			quality.push_back(item.quality);

			if (item.itemclass > 0)
			{
				subclassesFound.insert(std::pair<int, int>(item.itemclass, item.subclass));
			}
			cats.push_back(subclasslookup[std::pair<int, int>(item.itemclass, item.subclass)]);
		}
	}

	if (subclassesFound.size() > 1)
		itemDialog = new CategoryChoiceDialog(this, type, g_modelViewer, wxT("Choose an item"), caption, choices, cats,
		                                      catnames, &quality, false);
	else
		itemDialog = new FilteredChoiceDialog(this, type, g_modelViewer, wxT("Choose an item"), caption, choices,
		                                      &quality);

	wxSize s = itemDialog->GetSize();
	const int w = 250;
	if (s.GetWidth() > w)
	{
		itemDialog->SetSizeHints(w, -1, -1, -1, -1, -1);
		itemDialog->SetSize(w, -1);
	}

	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4, 64));
	itemDialog->Show();
	choosingSlot = slot;
}

void CharControl::selectSet()
{
	ClearItemDialog();

	std::vector<NumStringPair> Items;

	// Adds "none" to select
	NumStringPair n;
	n.id = -1;
	n.name = wxT("---- None ----");
	Items.push_back(n);

	sqlResult itemSet = GAMEDATABASE.sqlQuery("SELECT ID, Name_Lang FROM ItemSet");

	if (itemSet.valid && !itemSet.empty())
	{
		for (auto& value : itemSet.values)
		{
			NumStringPair p;
			p.id = value[0].toInt();
			p.name = value[1].toStdWString();
			Items.push_back(p);
		}
	}

	std::sort(Items.begin(), Items.end());
	numbers.clear();
	choices.Clear();
	for (auto& Item : Items)
	{
		choices.Add(Item.name);
		numbers.push_back(Item.id);
	}

	itemDialog = new FilteredChoiceDialog(this, UPDATE_SET, g_modelViewer, wxT("Choose an item set"), wxT("Item sets"),
	                                      choices, nullptr);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4, 64));
	itemDialog->Show();
}

void CharControl::selectStart()
{
	const auto& infos = model->infos;
	if (infos.raceID == -1)
		return;

	ClearItemDialog();
	numbers.clear();
	choices.Clear();

	LOG_INFO << "race =" << infos.raceID << "sex = " << infos.sexID;

	QString query = QString("SELECT ChrClasses.Filename, CSO.ID "
		"FROM CharStartOutfit AS CSO LEFT JOIN ChrClasses on CSO.classID = ChrClasses.ID "
		"WHERE CSO.raceID=%1 AND CSO.sexID=%2").arg(infos.raceID).arg(infos.sexID);

	sqlResult startOutfit = GAMEDATABASE.sqlQuery(query);

	if (startOutfit.valid && !startOutfit.empty())
	{
		for (auto& value : startOutfit.values)
		{
			choices.Add(value[0].toStdWString());
			numbers.push_back(value[1].toInt());
		}
	}

	itemDialog = new ChoiceDialog(this, UPDATE_START, g_modelViewer, wxT("Choose a class"), wxT("Classes"), choices);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4, 64));
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
		"SELECT MountXDisplay.CreatureDisplayInfoID, Mount.Name_Lang FROM Mount LEFT JOIN MountXDisplay ON Mount.ID = MountXDisplay.MountID");
	if (mountQuery.valid && !mountQuery.empty())
	{
		for (auto& value : mountQuery.values)
		{
			NumStringPair p;
			p.id = value[0].toInt();
			p.name = value[1].toStdWString();
			mounts.push_back(p);
		}
	}
	std::sort(mounts.begin(), mounts.end());
	for (auto& mount : mounts)
	{
		choices.Add(mount.name);
		numbers.push_back(mount.id);
		cats.push_back(0);
	}

	// All models from Creature/
	if (creaturemodels.empty())
	{
		std::vector<GameFile*> files;
		GAMEDIRECTORY.getFilesForFolder(files, QString("creature/"), QString("m2"));
		if (files.size())
		{
			std::vector<GameFile*>::iterator it;
			for (it = files.begin(); it != files.end(); ++it)
			{
				QString fn = (*it)->fullname();
				creaturemodels.push_back(wxString(fn.toStdWString()));
			}
			creaturemodels.Sort();
		}
	}
	for (size_t i = 0; i < creaturemodels.size(); i++)
	{
		choices.Add(creaturemodels[i].substr(9, string::npos)); // remove "creature/" bit for readability
		numbers.push_back(i);
		cats.push_back(1);
	}

	itemDialog = new CategoryChoiceDialog(this, UPDATE_MOUNT, g_modelViewer, wxT("Choose a mount"),
	                                      wxT("Mounts"), choices, cats, catnames, nullptr, true);
	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4, 64));
	itemDialog->Check(1, false);
	itemDialog->DoFilter();
	itemDialog->Show();
	const int w = 250;
	itemDialog->SetSizeHints(w, -1, -1, -1, -1, -1);
	itemDialog->SetSize(w, -1);
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

	sqlResult npccats = GAMEDATABASE.sqlQuery("SELECT ID,Name_Lang FROM CreatureType");

	if (npccats.valid && !npccats.empty())
	{
		for (auto& value : npccats.values)
		{
			catnames.Add(value[1].toStdWString());
			typeLookup[value[0].toInt()] = (int)catnames.size() - 1;
		}
	}

	std::vector<int> typesFound;

	for (auto& npc : npcs)
	{
		if (npc.model > 0)
		{
			QString NPCName = npc.name;

			if (displayItemAndNPCId != 0)
				NPCName += QString(" [%1]").arg(npc.id);

			choices.Add(NPCName.toStdWString());
			numbers.push_back(npc.id);
			quality.push_back(0);

			if (npc.type >= 0)
			{
				cats.push_back(typeLookup[npc.type]);
				typesFound.push_back(npc.type);
			}
			else
			{
				cats.push_back(0);
			}
		}
	}

	if (typesFound.size() > 1)
		itemDialog = new CategoryChoiceDialog(this, (int)type, g_modelViewer, _("Select an NPC"), _("NPC Models"),
		                                      choices, cats, catnames, &quality, false, true);
	else
		itemDialog = new FilteredChoiceDialog(this, (int)type, g_modelViewer, _("Select an NPC"), _("NPC Models"),
		                                      choices, &quality, false);

	itemDialog->SetSelection(0);

	wxSize s = itemDialog->GetSize();
	const int w = 250;
	if (s.GetWidth() > w)
	{
		itemDialog->SetSizeHints(w, -1, -1, -1, -1, -1);
		itemDialog->SetSize(w, -1);
	}

	itemDialog->Move(itemDialog->GetParent()->GetPosition() + wxPoint(4, 64));
	itemDialog->Show();
}

void CharControl::OnUpdateItem(int type, int id)
{
	switch (type)
	{
	case UPDATE_ITEM:
		{
			WoWItem* item = model->getItem((CharSlots)choosingSlot);
			if (item)
			{
				item->setId(numbers[id]);

				labels[choosingSlot]->SetLabel(item->name().toStdWString());
				labels[choosingSlot]->SetForegroundColour(ItemQualityColour(item->quality()));

				// refresh level combo box
				levelboxes[choosingSlot]->Clear();
				if (item->nbLevels() > 1)
				{
					levelboxes[choosingSlot]->Enable(true);
					for (unsigned int i = 0; i < item->nbLevels(); i++)
						levelboxes[choosingSlot]->Append(wxString::Format(wxT("%i"), i));
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
				QString query = QString("SELECT itemID1, itemID2, itemID3, itemID4, itemID5, "
					"itemID6, itemID7,  itemID8 FROM ItemSet WHERE ID = %1").arg(id);

				sqlResult itemSet = GAMEDATABASE.sqlQuery(query);

				if (itemSet.valid && !itemSet.empty())
				{
					// reset previously equipped items

					for (auto it : *model)
						it->setId(0);

					for (unsigned i = 0; i < 8; i++)
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

			if (startOutfit.valid && !startOutfit.empty())
			{
				// reset previously equipped items
				for (auto it : *model)
					it->setId(0);

				for (unsigned i = 0; i < 24; i++)
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
			GameFile* modelFile;
			WoWModel* m;
			int morphID = 0;

			if (!model)
				return;
			if (g_canvas->root->model())
			{
				g_canvas->root->setModel(nullptr);
				g_canvas->setModel(nullptr);
			}
			if (numbers[id] < 0) // The user selected "None". Remove existing mount.
			{
				// clearing the mount
				g_canvas->setModel(model);
				if (charAtt)
				{
					model->scale_ = dynamic_cast<WoWModel*>(g_canvas->root->model())->scale_;
					charAtt->id = 0;
				}
				g_animControl->UpdateModel(model);
				break;
			}

			// Sheathe weapons:
			model->bSheathe = true;
			RefreshEquipment();
			RefreshModel();
			g_modelViewer->charMenu->Check(ID_SHEATHE, true);

			if (cats[id] == 0) // create proper mount from model ID
			{
				morphID = numbers[id];
				// Only dealing with Creature/ models (for now), so don't need to worry about CreatureDisplayInfoExtra
				QString query = QString(
					"SELECT CreatureModelData.FileDataID, CreatureDisplayInfo.TextureVariationFileDataID1, "
					"CreatureDisplayInfo.TextureVariationFileDataID2, CreatureDisplayInfo.TextureVariationFileDataID3 FROM CreatureDisplayInfo "
					"LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
					"WHERE CreatureDisplayInfo.ID = %1;").arg(morphID);

				sqlResult mountQuery = GAMEDATABASE.sqlQuery(query);
				if (!mountQuery.valid || mountQuery.empty())
					break;
				modelFile = GAMEDIRECTORY.getFile(mountQuery.values[0][0].toInt());
			}
			else if (cats[id] == 1) // create mount from any old creature model file name
			{
				modelFile = GAMEDIRECTORY.getFile(QString::fromWCharArray(creaturemodels[numbers[id]].c_str()));
				// that's it. No special textures or anything.
			}
			else
				break; // shouldn't happen
			m = new WoWModel(modelFile, false);
			m->isMount = true;
			g_canvas->root->setModel(m);
			g_canvas->setModel(m, true);
			g_animControl->UpdateModel(m);

			// for official mounts with display IDs:
			if (morphID > 0)
				g_animControl->SetSkinByDisplayID(morphID);

			model->bSheathe = true;
			RefreshEquipment();

			// Alfred 2009.7.23 use animLookups to speed up
			if (model->animLookups.size() >= ANIMATION_MOUNT &&
				model->animLookups[ANIMATION_MOUNT] >= 0)
			{
				model->animManager->Stop();
				model->currentAnim = model->animLookups[ANIMATION_MOUNT];
				model->animManager->SetAnim(0, (short)model->currentAnim, 0);
			}

			if (charAtt)
			{
				charAtt->parent = g_canvas->root;
				// Need to set this - but from what
				// Model data doesn't contain sizes for different race/gender
				// Character data doesn't contain sizes for different mounts
				// possibly some formula that from both models that needs to be calculated.
				// For "Taxi" mounts scale should be 1.0f I think, for now I'll ignore them
				// I really have no idea!  
				if (creaturemodels[id - 1].Mid(9, 9).IsSameAs(wxT("Kodobeast"), false))
					dynamic_cast<WoWModel*>(charAtt->model())->scale_ = 2.25f;
				else
					dynamic_cast<WoWModel*>(charAtt->model())->scale_ = 1.0f;
			}
			model->rot_ = model->pos_ = glm::vec3(0.0f, 0.0f, 0.0f);
			m->rot_.x = 0.0f; // mounted characters look better from the side
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

void CharControl::OnTabardSpin(wxSpinEvent& event)
{
	if (!g_canvas || !g_canvas->model() || g_canvas->model()->modelType == MT_NPC)
	{
		LOG_ERROR << "Model Not Present, or can't use a tabard.";
		return;
	}

	switch (event.GetId())
	{
	case ID_TABARD_ICON:
		LOG_INFO << "Tabard Notice: Icon Change.";
		model->td.setIcon(event.GetPosition());
		break;
	case ID_TABARD_ICONCOLOR:
		LOG_INFO << "Tabard Notice: Icon Color Change.";
		model->td.setIconColor(event.GetPosition());
		break;
	case ID_TABARD_BORDER:
		{
			LOG_INFO << "Tabard Notice: Border Change.";
			model->td.setBorder(event.GetPosition());
			int maxColor = model->td.GetMaxBorderColor(model->td.getBorder());
			if (maxColor < model->td.getBorderColor())
			{
				model->td.setBorderColor(0);
				tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetValue(model->td.getBorderColor());
			}
			tabardSpins[SPIN_TABARD_BORDERCOLOR]->SetRange(0, maxColor);
		}
		break;
	case ID_TABARD_BORDERCOLOR:
		LOG_INFO << "Tabard Notice: Border Color Change.";
		model->td.setBorderColor(event.GetPosition());
		break;
	case ID_TABARD_BACKGROUND:
		LOG_INFO << "Tabard Notice: Background Color Change.";
		model->td.setBackground(event.GetPosition());
		break;
	}

	for (size_t i = 0; i < NUM_TABARD_BTNS; i++)
		spinTbLabels[i]->SetLabel(
			wxString::Format(wxT("%i / %i"), tabardSpins[i]->GetValue(), tabardSpins[i]->GetMax()));

	LOG_INFO << "Current tabard config :"
		<< "Icon" << model->td.getIcon()
		<< "IconColor" << model->td.getIconColor()
		<< "Border" << model->td.getBorder()
		<< "BorderColor" << model->td.getBorderColor()
		<< "Background" << model->td.getBackground();

	WoWItem* tabardModel = model->getItem(CS_TABARD);
	if (tabardModel)
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

void CharControl::onEvent(Event*)
{
	RefreshModel();
}

void CharControl::tryToEquipItem(int id)
{
	if (id == 0)
		return;

	ItemRecord itemr = items.getById(id);
	if (itemr.name != items.items[0].name)
	{
		int itemSlot = itemr.slot();
		if (itemSlot != -1)
		{
			WoWItem* item = model->getItem((CharSlots)itemSlot);
			if (item)
			{
				item->setId(id);
				labels[itemSlot]->SetLabel(item->name().toStdWString());
				labels[itemSlot]->SetForegroundColour(ItemQualityColour(item->quality()));

				// refresh level combo box
				levelboxes[itemSlot]->Clear();
				if (item->nbLevels() > 1)
				{
					levelboxes[itemSlot]->Enable(true);
					for (unsigned int i = 0; i < item->nbLevels(); i++)
						levelboxes[itemSlot]->Append(wxString::Format(wxT("%i"), i));
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

QString CharControl::getItemName(ItemRecord& item)
{
	QString result = item.name;

	if (displayItemAndNPCId != 0)
	{
		result += QString(" [%1]").arg(item.id);
	}

	return result;
}
