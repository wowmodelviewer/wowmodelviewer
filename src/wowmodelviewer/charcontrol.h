#ifndef CHARCONTROL_H
#define CHARCONTROL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// stl
#include <string>
#include <vector>

// our headers
#include "CharDetails.h"
#include "CharDetailsFrame.h"
#include "CharTexture.h"
#include "database.h"
#include "enums.h"
#include "WoWModel.h"
#include "modelcanvas.h"
#include "TabardDetails.h"

#include "metaclasses/Observer.h"

// forward class declarations
class ChoiceDialog;
class ModelViewer;

bool slotHasModel(size_t i);

class CharControl: public wxWindow, public Observer
{
	DECLARE_CLASS(CharControl)
    DECLARE_EVENT_TABLE()

	wxSpinButton *tabardSpins[NUM_TABARD_BTNS];
	wxButton *buttons[NUM_CHAR_SLOTS];
	wxComboBox *levelboxes[NUM_CHAR_SLOTS];
	wxStaticText *labels[NUM_CHAR_SLOTS];
	wxStaticText *spinTbLabels[NUM_TABARD_BTNS];
	CharDetailsFrame * cdFrame;

	void onEvent(Event *);
	void tryToEquipItem(int id);

public:
	// Item selection stuff
	ChoiceDialog *itemDialog;
	ssize_t choosingSlot;
	std::vector<int> numbers, cats;
	wxArrayString choices, catnames;

	CharControl(wxWindow* parent, wxWindowID id);
	~CharControl();

	bool Init();
	void UpdateModel(Attachment *a);
	
	void RefreshModel();
	void RefreshEquipment();
	inline void RandomiseChar();

	void OnTabardSpin(wxSpinEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnButton(wxCommandEvent &event);
	void OnItemLevelChange(wxCommandEvent& event);

	void OnUpdateItem(int type, int id);

	Attachment *charAtt;
	WoWModel *model;

	wxString customSkin;

	void ClearItemDialog();

	void selectItem(ssize_t type, ssize_t slot, const wxChar *caption=wxT("Item"));
	void selectSet();
	void selectStart();
	void selectMount();
	void selectNPC(ssize_t type);

	const wxString selectCharModel();

	void UpdateTextureList(wxString texName, int special);
};


#endif

