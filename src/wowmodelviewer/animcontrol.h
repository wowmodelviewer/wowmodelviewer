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

class TextureGroup
{
  public:
    static const size_t num = 3;
    size_t count;
    int base;
    GameFile * tex[num];
    // For particle colour replacements:
    int particleColInd; // ID for ParticleColor.dbc
    int PCRIndex;  // index into PCRList - list of particle color replacement values

    TextureGroup() : base(0), count(0)
    {
      for (size_t i=0; i<num; i++)
      {
        tex[i] = 0;
      }
      particleColInd = 0;
      PCRIndex = -1;
    }

    // default copy constr
    TextureGroup(const TextureGroup &grp)
    {
      for (size_t i=0; i<num; i++)
      {
        tex[i] = grp.tex[i];
      }
      base = grp.base;
      count = grp.count;
      particleColInd = grp.particleColInd;
      PCRIndex = grp.PCRIndex;
    }

    bool operator<(const TextureGroup &grp) const
    {
      for (size_t i=0; i<num; i++)
      {
        if (tex[i]<grp.tex[i]) return true;
        if (tex[i]>grp.tex[i]) return false;
      }
      if (particleColInd < grp.particleColInd)
        return true;
      return false;
    }

    bool operator==(const TextureGroup &grp) const
    {
      for (size_t i=0; i<num; i++)
      {
        if (tex[i] != grp.tex[i])
          return false;
      }
      if (particleColInd != grp.particleColInd)
        return false;
      return true;
    }

    bool operator!=(const TextureGroup &grp) const
    {
      return !((*this) == grp);
    }

};

typedef std::set<TextureGroup> TextureSet;
typedef std::vector<Vec4D> particleColorSet; // Holds 3 particle colours: Start, Mid and End (of particle life), for cases where 
                                             // particle colours are overridden by values from ParticleColor.dbc,
typedef std::vector<particleColorSet> particleColorReplacements; // Holds 3 colour sets. The particle will get its replacement
                                                                 // colour set from 0, 1 or 2, depending on whether its
                                                                 // ParticleColorIndex is set to 11, 12 or 13

class AnimControl: public wxWindow
{
  DECLARE_CLASS(AnimControl)
  DECLARE_EVENT_TABLE()

  wxComboBox *animCList, *animCList2, *animCList3, *wmoList, *loopList;
  wxStaticText *wmoLabel,*speedLabel, *speedMouthLabel, *frameLabel;
  wxStaticText *BLPSkinsLabel, *BLPSkinLabel1, *BLPSkinLabel2, *BLPSkinLabel3;
  wxSlider *speedSlider, *speedMouthSlider, *frameSlider;
  wxButton *btnAdd;
  wxCheckBox *lockAnims, *nextAnims;
  wxTextCtrl *lockText;

  wxButton *btnPlay, *btnPause, *btnStop, *btnClear, *btnPrev, *btnNext;
  wxCheckBox *oldStyle;

  bool UpdateCreatureModel(WoWModel *m);
  bool UpdateItemModel(WoWModel *m);
  bool FillSkinSelector(TextureSet &skins);
  bool FillBLPSkinSelector(TextureSet &skins);

public:
  AnimControl(wxWindow* parent, wxWindowID id);
  ~AnimControl();

  wxComboBox *skinList, *BLPSkinList1, *BLPSkinList2, *BLPSkinList3;

  void UpdateModel(WoWModel *m);
  void UpdateWMO(WMO *w, int group);

  void OnButton(wxCommandEvent &event);
  void OnCheck(wxCommandEvent &event);
  void OnAnim(wxCommandEvent &event);
  void OnSkin(wxCommandEvent &event);
  void OnBLPSkin(wxCommandEvent &event);
  void OnItemSet(wxCommandEvent &event);
  void OnSliderUpdate(wxCommandEvent &event);
  void OnLoop(wxCommandEvent &event); 
  Vec4D AnimControl::fromARGB(int color);
  void SetSkinByDisplayID(int cdi);
  int AddSkin(TextureGroup grp);
  void SetSkin(int num);
  void SetSingleSkin(int num, int texnum);
  void SetAnimSpeed(float speed);
  void SetAnimFrame(size_t frame);

  bool randomSkins;
  bool defaultDoodads; 
  std::string oldname;
  std::map<int, TextureGroup> CDIToTexGp;
  std::vector<particleColorReplacements> PCRList; 
  int selectedAnim;
  int selectedAnim2;
  int selectedAnim3;
  bool bOldStyle;
  bool bLockAnims;
  bool bNextAnims;
};

#endif

