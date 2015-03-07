
#include "itemselection.h"
#include "globalvars.h"
#include "ItemImporterDialog.h"
#include "ModelViewer.h"
#include "NPCimporterDialog.h"

#include <QString>

#include "charcontrol.h"
#include "GameDatabase.h"

// HACK: this is the ID for the single choice dialog listbox in the wx src
// - if it changes this code may break
#define wxID_LISTBOX 3000
#define wxID_LISTCTRL 3001

BEGIN_EVENT_TABLE(ChoiceDialog, wxSingleChoiceDialog)
	EVT_LISTBOX(wxID_LISTBOX, FilteredChoiceDialog::OnClick)
	EVT_LIST_ITEM_SELECTED(wxID_LISTCTRL, ChoiceDialog::OnSelect)
END_EVENT_TABLE()

wxColour ItemQualityColour(int quality)
{
	wxColour c = *wxBLACK;
	switch(quality) {
		case 0:
			c = wxColour(0x9d, 0x9d, 0x9d);
			break;
		case 2:
			c = wxColour(0x1e, 0xff, 0x00);
			break;
		case 3:
			c = wxColour(0x00, 0x70, 0xdd);
			break;
		case 4:
			c = wxColour(0xa3, 0x35, 0xee);
			break;
		case 5:
			c = wxColour(0xff, 0x80, 0x00);
			break;
		case 6:
		case 7:
			c = wxColour(0xe5, 0xcc, 0x80);
			break;
	}
	return c;
}

ChoiceDialog::ChoiceDialog(CharControl *dest, int type,
	                       wxWindow *parent,
                           const wxString& message,
                           const wxString& caption,
                           const wxArrayString& choices)
    : wxSingleChoiceDialog(parent, message, caption, choices, (char**)NULL, wxCHOICEDLG_STYLE & ~wxCANCEL & ~wxCENTER, wxDefaultPosition)
{
	cc = dest;
	this->type = type;

	m_listctrl = NULL;

	// New Item Selection stuff
	// Objective is to change over from a wxListBox to a wxListCtrl
	// which supports different text colours
	wxCArrayString chs(choices);
	m_listctrl = new wxListView(this, wxID_LISTCTRL, wxDefaultPosition, wxSize(200,200), wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER); 
	
	m_listctrl->InsertColumn(0, wxT("Item"), wxLIST_FORMAT_LEFT, 195);
	//m_listctrl->SetColumnWidth(0, wxLIST_AUTOSIZE);

	wxListItem item;
	for (size_t i = 0; i < chs.GetCount(); i++) {
		item.SetText(choices[i]);
		item.SetId((long)i);
/*
		if (item.GetText() == "Death Knight")
			item.SetTextColour(wxColour(196, 30, 59));
		else if (item.GetText() == "Druid")
			item.SetTextColour(wxColour(255, 125, 10));
		else if (item.GetText() == "Hunter")
			item.SetTextColour(wxColour(171, 212, 115));
		else if (item.GetText() == "Mage")
			item.SetTextColour(wxColour(105, 204, 240));
		else if (item.GetText() == "Paladin")
			item.SetTextColour(wxColour(245, 140, 186));
		else if (item.GetText() == "Priest")
			item.SetTextColour(wxColour(255, 255, 255));
		else if (item.GetText() == "Rogue")
			item.SetTextColour(wxColour(255, 245, 105));
		else if (item.GetText() == "Shaman")
			item.SetTextColour(wxColour(36, 89, 255));
		else if (item.GetText() == "Warlock")
			item.SetTextColour(wxColour(148, 130, 201));
		else if (item.GetText() == "Warrior")
			item.SetTextColour(wxColour(199, 156, 110));
		else
			item.SetTextColour(*wxBLACK);
*/
		if ((i%2)==0)
			item.SetBackgroundColour(*wxWHITE);
		else
			item.SetBackgroundColour(wxColour(237,243,254));

		m_listctrl->InsertItem(item);
		//m_listctrl->InsertItem(i, choices[i]);
	}

	wxBoxSizer *frameSizer = (wxBoxSizer*)this->GetSizer();
	if (frameSizer) {
		frameSizer->Detach(m_listbox);
		frameSizer->Insert(1, m_listctrl, wxEXPAND|wxADJUST_MINSIZE|wxLEFT|wxRIGHT);
		frameSizer->SetSizeHints( m_listctrl );
		frameSizer->Fit( m_listctrl );

		m_listbox->Show(false);
	}
}

void ChoiceDialog::OnClick(wxCommandEvent &)
{
    m_selection = m_listbox->GetSelection();
	if (m_selection == -1) // If nothing was selected, exit function.
		return;

    m_stringSelection = m_listbox->GetStringSelection();
	if (cc)	
		cc->OnUpdateItem(type, GetSelection());
}

void ChoiceDialog::OnSelect(wxListEvent &)
{
    // MacOS is buggy with GetFocusedItem(), use GetFirstSelected instead
    m_selection = m_listctrl->GetFirstSelected();
    if (m_selection == -1)
	m_selection = m_listctrl->GetFocusedItem();

	if (m_selection == -1) // If nothing was selected, exit function.
		return;

    m_stringSelection = m_listctrl->GetItemText(m_selection);
	if (cc)	
		cc->OnUpdateItem(type, GetSelection());
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


BEGIN_EVENT_TABLE(FilteredChoiceDialog, ChoiceDialog)
    EVT_TEXT_ENTER(FilteredChoiceDialog::ID_FILTER_TEXT, FilteredChoiceDialog::OnFilter)
    EVT_TEXT(FilteredChoiceDialog::ID_FILTER_TEXT, FilteredChoiceDialog::OnFilter)
    EVT_BUTTON(FilteredChoiceDialog::ID_FILTER_BUTTON, FilteredChoiceDialog::OnFilter)
    EVT_BUTTON(FilteredChoiceDialog::ID_FILTER_CLEAR, FilteredChoiceDialog::OnFilter)
    EVT_BUTTON(FilteredChoiceDialog::ID_IMPORT_NPC_BUTTON, FilteredChoiceDialog::OnImportNPC)
    EVT_BUTTON(FilteredChoiceDialog::ID_IMPORT_ITEM_BUTTON, FilteredChoiceDialog::OnImportItem)
END_EVENT_TABLE()


FilteredChoiceDialog::FilteredChoiceDialog(CharControl *dest, int type, wxWindow *parent,
                            const wxString& message,
                            const wxString& caption,
                            const wxArrayString& choices,
							const std::vector<int> *,
							bool keepfirst)
    :ChoiceDialog(dest, type, parent, message, caption, choices)
{
	keepFirst = keepfirst;
    m_choices = &choices;
    m_indices.resize(m_choices->GetCount());
    for(size_t i=0; i<m_choices->GetCount(); ++i) 
		m_indices[i]=(int)i;
    
    wxSizer *topsizer = GetSizer();
    
    wxBoxSizer *sizer = new wxBoxSizer( wxHORIZONTAL );
    m_pattern = new wxTextCtrl(this, ID_FILTER_TEXT, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER); // fnmatch: "*" re: ""
    
    sizer->Add(new wxStaticText(this, -1, wxT("Filter   ")), 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(m_pattern, 1, 0);
    sizer->Add(new wxButton(this, ID_FILTER_CLEAR, wxT("Clear"), wxDefaultPosition, wxSize(40,-1)), 0, 0);
    
    wxBoxSizer *sizerImport = 0;
    if(type == UPDATE_NPC){
    	sizerImport = new wxBoxSizer( wxHORIZONTAL );
    	sizerImport->Add(new wxButton(this, ID_IMPORT_NPC_BUTTON, wxT("Import from URL"), wxDefaultPosition, wxSize(-1,-1)), 0, 0);
    }
    if(type == UPDATE_SINGLE_ITEM){
    	sizerImport = new wxBoxSizer( wxHORIZONTAL );
    	sizerImport->Add(new wxButton(this, ID_IMPORT_ITEM_BUTTON, wxT("Import from URL"), wxDefaultPosition, wxSize(-1,-1)), 0, 0);
    }
    wxBoxSizer * globalActionsSizer = new wxBoxSizer( wxVERTICAL );
    globalActionsSizer->Add(sizer);

    if(sizerImport){
    	globalActionsSizer->Add(sizerImport);
    }

    topsizer->Prepend(globalActionsSizer, 0, wxEXPAND | wxALL, 10);
    topsizer->SetSizeHints( this );
    topsizer->Fit( this );
    

	m_listctrl->DeleteAllItems();

	wxListItem item; 
	for(unsigned int i=0; i<choices.Count(); i++) {
		//item.m_mask=wxLIST_MASK_TEXT; 
		item.SetId(i); 
		//item.m_col=col; 
		item.SetText(choices[i]);
		/*
		if (quality) {
			item.SetTextColour(ItemQualityColour(quality->at(i)));
		}
		*/
		if ((i%2)==0)
			item.SetBackgroundColour(*wxWHITE);
		else
			item.SetBackgroundColour(wxColour(237,243,254));

		// m_listbox doesn't support wxListItem, try wxListCtrl
		m_listctrl->InsertItem(item);
	}
}

void FilteredChoiceDialog::OnFilter(wxCommandEvent& event){
	if (event.GetId() == ID_FILTER_CLEAR) 
		m_pattern->SetValue(wxEmptyString);
    DoFilter();
}

void FilteredChoiceDialog::OnImportNPC(wxCommandEvent& event){
	NPCimporterDialog *dlg = new NPCimporterDialog();
	if ( dlg->ShowModal() == wxID_OK ) {
		int modelid = dlg->getImportedId();
		if(modelid != -1) {
			int id = 0;
			bool found = false;
			for (std::vector<NPCRecord>::iterator it=npcs.begin();  it!=npcs.end(); ++it, id++) {
				if(it->id == modelid) {
					found = true;
					break;
				}
			}

			if(!found) { // npc is not present in current database
				NPCRecord rec(dlg->getNPCLine());
				if (rec.model > 0) {
					npcs.push_back(rec);
					id = npcs.size()-1;
					QString query = QString("INSERT INTO Creature(ID,CreatureTypeID,DisplayID,Name) VALUES (%1,%2,%3,\"%4\")").arg(modelid).arg(rec.type).arg(rec.model).arg(CSConv(rec.name).mb_str());
					GAMEDATABASE.sqlQuery(query.toStdString());
				}
			}

			if (cc)
				cc->OnUpdateItem(UPDATE_NPC, id );
		}
	}
	dlg->Destroy();
}

void FilteredChoiceDialog::OnImportItem(wxCommandEvent& event){
	ItemImporterDialog *dlg = new ItemImporterDialog();
	if ( dlg->ShowModal() == wxID_OK ){
		ItemRecord rec = dlg->getImportedItem();
		if(rec.id != 0) {
			bool found = false;

			for (std::vector<ItemRecord>::iterator it=items.items.begin();  it!=items.items.end(); ++it) {
				if(it->id == rec.id) {
					found = true;
					break;
				}
			}

			if(!found) { // item is not present in current database
				if (rec.model > 0) {
					items.items.push_back(rec);
				}
			}

			g_modelViewer->LoadItem(rec.model);
			g_modelViewer->UpdateControls();
		}
	}
	dlg->Destroy();
}

void FilteredChoiceDialog::DoFilter()
{
	m_indices.clear();

	m_listctrl->DeleteAllItems();

	wxListItem item; 
	for(int i=0; i<(int)m_choices->GetCount(); ++i)
	{
		if (FilterFunc(i))
		{
			item.SetId(i); 
			item.SetText(m_choices->Item(i));
			m_indices.push_back((int)i);
			if ((i%2)==0)
				item.SetBackgroundColour(*wxWHITE);
			else
				item.SetBackgroundColour(wxColour(237,243,254));

			m_listctrl->InsertItem(item);
		}
	}
}

bool FilteredChoiceDialog::FilterFunc(int index)
{
	if (index==0 && keepFirst) 
		return true;

	return m_choices->Item(index).Lower().Matches("*" + m_pattern->GetValue().Lower() + "*");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(CategoryChoiceDialog, FilteredChoiceDialog)
	EVT_CHECKLISTBOX(CategoryChoiceDialog::ID_CAT_LIST, CategoryChoiceDialog::OnCheck)
	EVT_LISTBOX_DCLICK(CategoryChoiceDialog::ID_CAT_LIST, CategoryChoiceDialog::OnCheckDoubleClick)
END_EVENT_TABLE()

CategoryChoiceDialog::CategoryChoiceDialog(CharControl *dest, int type,
	                    wxWindow *parent,
                        const wxString& message,
                        const wxString& caption,
                        const wxArrayString& choices,
						const std::vector<int> &cats,
						const wxArrayString& catnames,
						const std::vector<int> *quality,
						bool keepfirst,
						bool helpmsg):
	FilteredChoiceDialog(dest, type, parent, message, caption, choices, quality, keepfirst), m_cats(cats)
{
  wxArrayString realcatnames;
  // filter catnames based on cats
  for (size_t i=0; i<catnames.GetCount(); i++)
  {
    for(size_t j=keepfirst?1:0 ; j < m_cats.size() ; j++)
    {
      if(m_cats[j] == (int)i)
      {
        realcatnames.push_back(catnames[i]);
        m_catsConvert[m_cats[j]] = realcatnames.size()-1;
        break;
      }
    }
  }

	m_catlist = new wxCheckListBox(this, ID_CAT_LIST, wxDefaultPosition, wxDefaultSize, realcatnames, wxLB_HSCROLL);
	for (size_t i=0; i<realcatnames.GetCount(); i++)
		m_catlist->Check((unsigned int)i);

	numcats = (int)realcatnames.GetCount();

	wxSizer *topsizer = GetSizer();

	topsizer->Prepend(m_catlist, 0, wxEXPAND | wxALL, 10);

	if (helpmsg)
		topsizer->Prepend(new wxStaticText(this, -1, wxT("Double-click to select only one category")), 0, wxEXPAND | wxTOP | wxLEFT, 10);

	topsizer->SetSizeHints(this);
	topsizer->Fit(this);

	m_listbox->SetFocus();
}

bool CategoryChoiceDialog::FilterFunc(int index)
{
	if (index==0 && keepFirst) 
		return true;

	return m_catlist->IsChecked(m_catsConvert[m_cats[index]]) && FilteredChoiceDialog::FilterFunc(index);
}

void CategoryChoiceDialog::OnCheck(wxCommandEvent &)
{
    DoFilter();
}

void CategoryChoiceDialog::OnCheckDoubleClick(wxCommandEvent &e)
{
	for (int i=0; i<numcats; i++) {
		Check(i, i==e.GetInt());
	}
	DoFilter();
}

void CategoryChoiceDialog::Check(int index, bool state)
{
	m_catlist->Check(index, state);
}
