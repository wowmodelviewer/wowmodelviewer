#ifndef CHARCONTROL_H
#define CHARCONTROL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// wx
#include <wx/spinbutt.h>

// stl
#include <string>
#include <vector>

// our headers
#include "CharDetails.h"
#include "CharTexture.h"
#include "database.h"
#include "enums.h"
#include "WoWModel.h"
#include "modelcanvas.h"
#include "TabardDetails.h"


// forward class declarations
class ChoiceDialog;
class ModelViewer;

bool slotHasModel(size_t i);

class RaceInfos
{
  public:
    int raceid;
    int sexid; // 0 male / 1 female
    int textureLayoutID;
    std::string prefix;
};


class CharControl: public wxWindow
{
	DECLARE_CLASS(CharControl)
    DECLARE_EVENT_TABLE()

	wxSpinButton *spins[NUM_SPIN_BTNS];
	wxStaticText *spinLabels[NUM_SPIN_BTNS];
	wxSpinButton *tabardSpins[NUM_TABARD_BTNS];
	wxButton *buttons[NUM_CHAR_SLOTS];
	wxStaticText *labels[NUM_CHAR_SLOTS];
	wxStaticText *spinTbLabels[NUM_TABARD_BTNS];

	void AddEquipment(ssize_t slot, ssize_t itemnum, ssize_t layer, CharTexture &tex, bool lookup = true);
	void UpdateTextureList(wxString texName, int special);

	static std::map< std::string, RaceInfos> RACES;
	bool getRaceInfosForCurrentModel(RaceInfos &);

	std::vector<std::string> getTextureNameForSection(CharSectionsDB::SectionType);
	int getNbValuesForSection(CharSectionsDB::SectionType type);

	void refreshModelSpins();

public:
	// Item selection stuff
	ChoiceDialog *itemDialog;
	ssize_t choosingSlot;
	std::vector<int> numbers, cats;
	wxArrayString choices, catnames;

	CharControl(wxWindow* parent, wxWindowID id);
	~CharControl();

	bool Init();
	//void UpdateModel(Model *m);
	void UpdateModel(Attachment *a);
	void UpdateNPCModel(Attachment *a, size_t id);
	
	void RefreshModel();
	void RefreshNPCModel();
	void RefreshItem(ssize_t slot);
	void RefreshCreatureItem(ssize_t slot);
	void RefreshEquipment();
	inline void RandomiseChar();

	TextureID charTex, hairTex, furTex, capeTex, gobTex;

	bool bSheathe;

	void OnSpin(wxSpinEvent &event);
	void OnTabardSpin(wxSpinEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnButton(wxCommandEvent &event);

	void OnUpdateItem(int type, int id);

	CharDetails cd;
	TabardDetails td;

	Attachment *charAtt;
	WoWModel *model;

	wxString makeItemTexture(int region, const wxString name);
	wxString customSkin;

	void ClearItemDialog();

	void selectItem(ssize_t type, ssize_t slot, ssize_t current, const wxChar *caption=wxT("Item"));
	void selectSet();
	void selectStart();
	void selectMount();
	void selectNPC(ssize_t type);

	const wxString selectCharModel();

	static void initRaces();

};


#endif

