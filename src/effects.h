#ifndef EFFECTS_H
#define EFFECTS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "charcontrol.h"
#include "modelcanvas.h"

#include <string>

extern wxArrayString spelleffects;

void SelectCreatureItem(ssize_t slot, ssize_t current, CharControl *cc, wxWindow *parent);

struct NumStringPair {
	int id;
	wxString name;

	bool operator< (const NumStringPair &p) const {
		return name < p.name;
	}
};

struct EnchantsRec {
	wxString name;
	std::string models[5];
	bool operator< (const EnchantsRec &p) const {
		return name < p.name;
	}
};


class EnchantsDialog : public wxDialog {
    DECLARE_EVENT_TABLE()

	void InitObjects();
	void InitEnchants();

	CharControl *charControl;

	wxRadioBox *slot;
	wxListBox *effectsListbox;
	wxStaticText *text1;
	wxButton *btnOK, *btnCancel;

	bool EnchantsInitiated;
	bool Initiated;

	wxArrayString choices;
	std::map<int, EnchantsRec> enchants;
	
public:
    EnchantsDialog(wxWindow *parent, CharControl *cc);
	~EnchantsDialog() 
	{ 
		enchants.clear();
	}
	
	void OnClick(wxCommandEvent &event);
	void Display();

	int RHandEnchant;
	int LHandEnchant;
};


#endif
