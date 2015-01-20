#ifndef ANIMCONTROL_H
#define ANIMCONTROL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

//#include "model.h"
//#include "wmo.h"
#include "modelcanvas.h"
#include "database.h"

extern float animSpeed;

// AnimationData.dbc
#define	ANIM_STAND	0

struct TextureGroup {
	static const size_t num = 3;
	size_t count;
	int base;
	wxString tex[num];
	TextureGroup()
	{
		for (size_t i=0; i<num; i++) {
			tex[i] = wxT("");
		}
	}

	// default copy constr
	TextureGroup(const TextureGroup &grp)
	{
		for (size_t i=0; i<num; i++) {
			tex[i] = grp.tex[i];
		}
		base = grp.base;
		count = grp.count;
	}

	bool operator<(const TextureGroup &grp) const
	{
		for (size_t i=0; i<num; i++) {
			if (tex[i]<grp.tex[i]) return true;
			if (tex[i]>grp.tex[i]) return false;
		}
		return false;
	}
};

typedef std::set<TextureGroup> TextureSet;

class AnimControl: public wxWindow
{
	DECLARE_CLASS(AnimControl)
    DECLARE_EVENT_TABLE()

	wxComboBox *animCList, *animCList2, *animCList3, *wmoList, *loopList;
	wxStaticText *wmoLabel,*speedLabel, *speedMouthLabel, *frameLabel;
	wxSlider *speedSlider, *speedMouthSlider, *frameSlider;
	wxButton *btnAdd;
	wxCheckBox *lockAnims, *nextAnims;
	wxTextCtrl *lockText;

	wxButton *btnPlay, *btnPause, *btnStop, *btnClear, *btnPrev, *btnNext;
	wxCheckBox *oldStyle;

	bool UpdateCreatureModel(WoWModel *m);
	bool UpdateItemModel(WoWModel *m);
	bool FillSkinSelector(TextureSet &skins);

public:
	AnimControl(wxWindow* parent, wxWindowID id);
	~AnimControl();

	wxComboBox *skinList;

	void UpdateModel(WoWModel *m);
	void UpdateWMO(WMO *w, int group);

	void OnButton(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnAnim(wxCommandEvent &event);
	void OnSkin(wxCommandEvent &event);
	void OnItemSet(wxCommandEvent &event);
	void OnSliderUpdate(wxCommandEvent &event);
	void OnLoop(wxCommandEvent &event); 

	int AddSkin(TextureGroup grp);
	void SetSkin(int num);
	void SetAnimSpeed(float speed);
	void SetAnimFrame(size_t frame);

	bool randomSkins;
	bool defaultDoodads;

	wxString oldname;

	int selectedAnim;
	int selectedAnim2;
	int selectedAnim3;
	bool bOldStyle;
	bool bLockAnims;
	bool bNextAnims;
};





#endif

