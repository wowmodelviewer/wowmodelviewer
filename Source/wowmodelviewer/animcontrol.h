#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

//#include "model.h"
//#include "wmo.h"
#include "modelcanvas.h"

extern float animSpeed;

// AnimationData.dbc
#define ANIM_STAND  0

typedef int GeosetNum;

class TextureGroup
{
public:
	static const size_t num = 3;
	size_t count;
	int base;
	GameFile* tex[num];
	// tex gp is derived from CreatureDisplayInfo, not just a random skin in the folder:
	bool definedTexture;
	// For particle colour replacements:
	int particleColInd; // ID for ParticleColor.dbc
	int PCRIndex; // index into PCRList - list of particle color replacement values
	std::set<GeosetNum> creatureGeosetData;
	// Defines which geosets are switched on for a particular display ID of a model

	TextureGroup() : count(0), base(0)
	{
		for (size_t i = 0; i < num; i++)
		{
			tex[i] = 0;
		}
		particleColInd = 0;
		PCRIndex = -1;
		creatureGeosetData.clear();
		definedTexture = false;
	}

	// default copy constr
	TextureGroup(const TextureGroup& grp)
	{
		for (size_t i = 0; i < num; i++)
		{
			tex[i] = grp.tex[i];
		}
		base = grp.base;
		count = grp.count;
		particleColInd = grp.particleColInd;
		PCRIndex = grp.PCRIndex;
		creatureGeosetData = grp.creatureGeosetData;
		definedTexture = grp.definedTexture;
	}

	bool operator<(const TextureGroup& grp) const
	{
		if (!definedTexture && grp.definedTexture)
			return false;
		if (definedTexture && !grp.definedTexture)
			return true;
		QString texname1 = tex[0]->fullname();
		QString texname2 = grp.tex[0]->fullname();
		texname1 = texname1.mid(texname1.lastIndexOf("/"));
		texname2 = texname2.mid(texname2.lastIndexOf("/"));
		if (texname1 != texname2)
			return texname1 < texname2;
		for (size_t i = 0; i < num; i++)
		{
			if (tex[i] < grp.tex[i]) return true;
			if (tex[i] > grp.tex[i]) return false;
		}
		if (particleColInd < grp.particleColInd)
			return true;
		if (creatureGeosetData < grp.creatureGeosetData)
			return true;
		return false;
	}

	bool operator==(const TextureGroup& grp) const
	{
		for (size_t i = 0; i < num; i++)
		{
			if (tex[i] != grp.tex[i])
				return false;
		}
		if (particleColInd != grp.particleColInd)
			return false;
		if (creatureGeosetData != grp.creatureGeosetData)
			return false;
		return true;
	}

	bool operator!=(const TextureGroup& grp) const
	{
		return !((*this) == grp);
	}
};

typedef std::set<TextureGroup> TextureSet;
typedef std::vector<glm::vec4> particleColorSet;
// Holds 3 particle colours: Start, Mid and End (of particle life), for cases where 
// particle colours are overridden by values from ParticleColor.dbc,
typedef std::vector<particleColorSet> particleColorReplacements;
// Holds 3 colour sets. The particle will get its replacement
// colour set from 0, 1 or 2, depending on whether its
// ParticleColorIndex is set to 11, 12 or 13

class AnimControl : public wxWindow
{
	DECLARE_CLASS(AnimControl)
	DECLARE_EVENT_TABLE()

	wxComboBox *animCList, *animCList2, *animCList3, *wmoList, *loopList;
	wxButton* showBLPList;
	wxStaticText *wmoLabel, *speedLabel, *speedMouthLabel, *frameLabel;
	wxStaticText *BLPSkinsLabel, *BLPSkinLabel1, *BLPSkinLabel2, *BLPSkinLabel3;
	wxSlider *speedSlider, *speedMouthSlider, *frameSlider;
	wxButton* btnAdd;
	wxCheckBox *lockAnims, *nextAnims;
	wxTextCtrl* lockText;

	wxButton *btnPlay, *btnPause, *btnStop, *btnClear, *btnPrev, *btnNext;
	wxCheckBox* oldStyle;

	bool UpdateCreatureModel(WoWModel* m);
	bool UpdateItemModel(WoWModel* m);
	bool FillSkinSelector(TextureSet& skins);
	bool FillBLPSkinSelector(TextureSet& skins, bool item = false);
	void UpdateFrameSlider(int maxRange, int tickFreq);

public:
	AnimControl(wxWindow* parent, wxWindowID id);
	~AnimControl();

	wxComboBox *skinList, *BLPSkinList1, *BLPSkinList2, *BLPSkinList3;

	void UpdateModel(WoWModel* m);
	void UpdateWMO(WMO* w, int group);

	void OnButton(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);
	void OnAnim(wxCommandEvent& event);
	void OnSkin(wxCommandEvent& event);
	void OnBLPSkin(wxCommandEvent& event);
	void OnItemSet(wxCommandEvent& event);
	void OnSliderUpdate(wxCommandEvent& event);
	void OnLoop(wxCommandEvent& event);
	glm::vec4 fromARGB(int color);
	void SetSkinByDisplayID(int cdi);
	int AddSkin(TextureGroup grp);
	void SetSkin(int num);
	void ActivateBLPSkinList();
	void SyncBLPSkinList();
	void SetSingleSkin(int num, int texnum);
	void SetAnimSpeed(float speed);
	void SetAnimFrame(size_t frame);
	QString GetModelFolder(WoWModel* m);

	bool defaultDoodads;
	std::string oldname;
	QString modelFolder;
	bool modelFolderChanged, BLPListFilled;
	std::map<int, TextureGroup> CDIToTexGp;
	std::vector<particleColorReplacements> PCRList;
	int selectedAnim;
	int selectedAnim2;
	int selectedAnim3;
	bool bOldStyle;
	bool bLockAnims;
	bool bNextAnims;
	TextureSet BLPskins;
};
