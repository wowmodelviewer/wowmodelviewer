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
#include "database.h"
#include "enums.h"
#include "model.h"
#include "modelcanvas.h"


// forward class declarations
class ChoiceDialog;
class ModelViewer;

bool slotHasModel(size_t i);
bool correctType(ssize_t type, ssize_t slot);

struct CharRegionCoords {
	int xpos, ypos, xsize, ysize;
};

#define	REGION_FAC_X	2
#define REGION_FAC_Y  2
#define	REGION_PX_WIDTH	(256*REGION_FAC_X)
#define REGION_PX_HEIGHT (256*REGION_FAC_Y)

const CharRegionCoords regions[NUM_REGIONS] =
{
	{0, 0, 256*REGION_FAC_X, 256*REGION_FAC_Y},	// base
	{0, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm upper
	{0, 64*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm lower
	{0, 128*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// hand
	{0, 160*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// face upper
	{0, 192*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// face lower
	{128*REGION_FAC_X, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// torso upper
	{128*REGION_FAC_X, 64*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// torso lower
	{128*REGION_FAC_X, 96*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y}, // pelvis upper
	{128*REGION_FAC_X, 160*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},// pelvis lower
	{128*REGION_FAC_X, 224*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y}	// foot
};

const CharRegionCoords pandaren_regions[NUM_REGIONS] =
{
  {0, 0, 256*REGION_FAC_X*2, 256*REGION_FAC_Y},	// base
  {0, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm upper
  {0, 64*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm lower
  {0, 128*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// hand
  {128*REGION_FAC_X*2, 0, 256*REGION_FAC_X, 256*REGION_FAC_Y},	// face upper
  {0, 192*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// face lower
  {128*REGION_FAC_X, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// torso upper
  {128*REGION_FAC_X, 64*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// torso lower
  {128*REGION_FAC_X, 96*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y}, // pelvis upper
  {128*REGION_FAC_X, 160*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},// pelvis lower
  {128*REGION_FAC_X, 224*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y}	// foot
};

struct CharTextureComponent
{
	wxString name;
	int region;
	int layer;

	bool operator<(const CharTextureComponent& c) const
	{
		return layer < c.layer;
	}
};

struct CharTexture
{
  size_t race;
	std::vector<CharTextureComponent> components;

  CharTexture(size_t _race)
    : race(_race)
  {}

	void addLayer(wxString fn, int region, int layer)
	{
		if (!fn || fn.length()==0)
			return;

		CharTextureComponent ct;
		ct.name = fn;
		ct.region = region;
		ct.layer = layer;
		components.push_back(ct);
	}
	void compose(TextureID texID);
};

struct TabardDetails
{
	int Icon;
	int IconColor;
	int Border;
	int BorderColor;
	int Background;

	int maxIcon;
	int maxIconColor;
	int maxBorder;
	int maxBorderColor;
	int maxBackground;

	bool showCustom;

	wxString GetIconTex(int slot);
	wxString GetBorderTex(int slot);
	wxString GetBackgroundTex(int slot);

	int GetMaxIcon();
	int GetMaxIconColor(int icon);
	int GetMaxBorder();
	int GetMaxBorderColor(int border);
	int GetMaxBackground();
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
	Model *model;

	wxString makeItemTexture(int region, const wxString name);
	wxString customSkin;

	void ClearItemDialog();

	void selectItem(ssize_t type, ssize_t slot, ssize_t current, const wxChar *caption=wxT("Item"));
	void selectSet();
	void selectStart();
	void selectMount();
	void selectNPC(ssize_t type);

	const wxString selectCharModel();
};


#endif

