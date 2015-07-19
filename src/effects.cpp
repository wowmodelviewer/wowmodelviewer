#include "effects.h"

#include "database.h"
#include "enums.h"
#include "itemselection.h"
#include "next-gen/games/wow/Attachment.h"

wxArrayString spelleffects;


// 10 for rhand, 11 for lhand)
void SelectCreatureItem(ssize_t slot, ssize_t current, CharControl *cc, wxWindow *parent)
{
  /*
     @TODO : TO REPAIR

	cc->ClearItemDialog();
	cc->numbers.clear();
	cc->choices.Clear();

	// collect all items for this slot, making note of the occurring subclasses
	set<pair<int,int> > subclassesFound;

	int sel=0, ord=0;
	for (std::vector<ItemRecord>::iterator it = items.items.begin();  it != items.items.end();  ++it) {
		if (correctType((size_t)it->type, slot)) {
			cc->choices.Add(it->name);
			cc->numbers.push_back(it->id);
			if (it->id == current)
				sel = ord;

			ord++;

			if (it->itemclass > 0)
				subclassesFound.insert(pair<int,int>(it->itemclass, it->subclass));
		}
	}

	// make category list
	cc->cats.clear();
	cc->catnames.clear();

	map<pair<int,int>, int> subclasslookup;
	for (ItemSubClassDB::Iterator it=subclassdb.begin(); it != subclassdb.end(); ++it) {
		int cl = it->getInt(ItemSubClassDB::ClassIDV400);
		int scl = it->getInt(ItemSubClassDB::SubClassIDV400);
		// only add the subclass if it was found in the itemlist
		if (cl>0 && subclassesFound.find(pair<int,int>(cl, scl)) != subclassesFound.end()) {
			wxString str = CSConv(it->getString(ItemSubClassDB::NameV400 + langOffset));
			int hands = it->getInt(ItemSubClassDB::HandsV400);
			if (hands > 0) {
				str << wxT(" (") << hands << wxT("-handed)");

				//char buf[16];
				//sprintf(buf, " (%d-handed)", hands);
				//str.append(buf);
			}
			cc->catnames.Add(str.c_str());
			subclasslookup[pair<int,int>(cl,scl)] = (int)cc->catnames.size()-1;
		}
	}

	if (subclassesFound.size() > 1) {
		// build category list
		for (size_t i=0; i<cc->numbers.size(); i++) {
			ItemRecord r = items.getById(cc->numbers[i]);
			cc->cats.push_back(subclasslookup[pair<int,int>(r.itemclass, r.subclass)]);
		}

		cc->itemDialog = new CategoryChoiceDialog(cc, UPDATE_CREATURE_ITEM, parent, wxT("Choose an item"), wxT("Select a Weapon"), cc->choices, cc->cats, cc->catnames, 0);
	} else {
		cc->itemDialog = new FilteredChoiceDialog(cc, UPDATE_CREATURE_ITEM, parent, wxT("Choose an item"), wxT("Select a Weapon"), cc->choices, 0);
	}

	cc->itemDialog->SetSelection(sel);

	wxSize s = cc->itemDialog->GetSize();
	const int w = 250;
	if (s.GetWidth() > w) {
		cc->itemDialog->SetSizeHints(w,-1,-1,-1,-1,-1);
		cc->itemDialog->SetSize(w, -1);
	}

	cc->itemDialog->Move(parent->GetPosition() + wxPoint(4,64));
	cc->itemDialog->Show();
	cc->choosingSlot = slot;
	*/
}

// Enchants Dialog

BEGIN_EVENT_TABLE(EnchantsDialog, wxDialog)
	EVT_BUTTON(ID_ENCHANTSOK, EnchantsDialog::OnClick)
	EVT_BUTTON(ID_ENCHANTSCANCEL, EnchantsDialog::OnClick)
END_EVENT_TABLE()

EnchantsDialog::EnchantsDialog(wxWindow *parent, CharControl *cc)
{
	charControl = cc;
	Initiated = false;
	EnchantsInitiated = false;

	LHandEnchant = -1;
	RHandEnchant = -1;

	slot = NULL;
	effectsListbox = NULL;

	Create(parent, -1, wxT("Weapon Enchants"), wxDefaultPosition, wxSize(200,350), wxDEFAULT_DIALOG_STYLE);
	Show(false);
}

void EnchantsDialog::OnClick(wxCommandEvent &event)
{
	if (event.GetId() == ID_ENCHANTSOK) {
		wxString sel(effectsListbox->GetStringSelection());

		if (sel.IsEmpty()) {
			Show(false);
			return;
		}

		if (sel==wxT("NONE") || sel==wxT("None")) {
			if (slot->GetSelection() == 0)
				RHandEnchant = -1;
			else
				LHandEnchant = -1;
			Show(false);
			return;
		}
		/*
		for (std::vector<EnchantsRec>::iterator it=enchants.begin();  it!=enchants.end();  ++it) {
			if (it->name == sel) {
				int s = slot->GetSelection();
				s += 10;

				if (!charControl->model)
					return;

				if (!slotHasModel(s))
					return;
				
				if (slot->GetSelection() == 0)
					RHandEnchant = it->id;
				else
					LHandEnchant = it->id;

				// children:
				for (size_t i=0; i < charControl->charAtt->children.size(); i++) {
					if (charControl->charAtt->children[i]->slot == s) {
						Attachment *att = charControl->charAtt->children[i];
						if (att->children.size() > 0)
							att->delChildren();

						WoWModel *m = static_cast<WoWModel*>(att->model);
						if (!m)
							return;

						for (ssize_t k=0; k<5; k++) {
							if ((it->index[k] > 0) && (m->attLookup[k]>=0)) {
								ItemVisualEffectDB::Record rec = effectdb.getById(it->index[k]);
								att->addChild(rec.getString(ItemVisualEffectDB::Model), k, -1);
							}
						}
						break;
					}
				}

				Show(false);
				return;
			}
		}
		*/
	} else if (event.GetId() == ID_ENCHANTSCANCEL)
		this->Show(false);
}

void EnchantsDialog::InitObjects()
{
	wxString slots[2] = {wxT("Right Hand"), wxT("Left Hand")};

	slot = new wxRadioBox(this, -1, wxT("Apply effects to:"), wxPoint(10,10), wxSize(180, 80), 2, slots, 4, wxRA_SPECIFY_ROWS, wxDefaultValidator, wxT("radioBox"));

	text1 = new wxStaticText(this, -1, wxT("Enchantments:"), wxPoint(10, 110), wxDefaultSize);
	effectsListbox = new wxListBox(this, -1, wxPoint(10,130), wxSize(180,160), choices, wxLB_SINGLE);
	
	btnOK = new wxButton(this, ID_ENCHANTSOK, wxT("OK"), wxPoint(90,295), wxSize(50,22));
	btnCancel = new wxButton(this, ID_ENCHANTSCANCEL, wxT("Cancel"), wxPoint(140,295), wxSize(50,22));

	Initiated = true;
}

void EnchantsDialog::InitEnchants()
{
  /*
  SELECT SpellItemEnchantment.ID, Name, IVE1.path AS enchant1, IVE2.path AS enchant2, IVE3.path AS enchant3, IVE4.path AS enchant4, IVE5.path AS enchant5
FROM SpellItemEnchantment
LEFT JOIN ItemVisuals ON VisualID = ItemVisuals .ID
LEFT JOIN ItemVisualEffects IVE1 ON ItemVisuals .itemVisualEffects1 = IVE1.ID
LEFT JOIN ItemVisualEffects IVE2 ON ItemVisuals .itemVisualEffects1 = IVE2.ID
LEFT JOIN ItemVisualEffects IVE3 ON ItemVisuals .itemVisualEffects3 = IVE3.ID
LEFT JOIN ItemVisualEffects IVE4 ON ItemVisuals .itemVisualEffects4 = IVE4.ID
LEFT JOIN ItemVisualEffects IVE5 ON ItemVisuals .itemVisualEffects5 = IVE5.ID
WHERE VisualID != 0




	EnchantsRec temp;

	// Alfred 2009.07.17 rewrite, use system database
	temp.id = 0;
	temp.index[0] = 0;
	temp.index[1] = 0;
	temp.index[2] = 0;
	temp.index[3] = 0;
	temp.index[4] = 0;
	temp.name = wxT("None");
	enchants.push_back(temp);

	for (SpellItemEnchantmentDB::Iterator it=spellitemenchantmentdb.begin();  it!=spellitemenchantmentdb.end(); ++it) {
		int visualid = it->getInt(SpellItemEnchantmentDB::VisualIDV400);
		if (visualid < 1)
			continue;
		for (ItemVisualsDB::Iterator it2=itemvisualsdb.begin();  it2!=itemvisualsdb.end(); ++it2) {
			if (it2->getInt(ItemVisualsDB::VisualID) == visualid) {
				temp.id = visualid;
				for(size_t i=0; i<5; i++)
					temp.index[i] = it2->getInt(ItemVisualsDB::VisualID+1+i);
				temp.name = CSConv(it->getString(SpellItemEnchantmentDB::Name + langOffset));
				enchants.push_back(temp);
				break;
			}
		}
	}

	choices.Clear();
	for (std::vector<EnchantsRec>::iterator it=enchants.begin();  it!=enchants.end();  ++it)
		choices.Add(it->name);

	EnchantsInitiated = true;
	*/
}

void EnchantsDialog::Display()
{
	if (!EnchantsInitiated)
		InitEnchants();

	if (!Initiated)
		InitObjects();

	if (Initiated) {
		Center();
		Show(true);
	}
}
