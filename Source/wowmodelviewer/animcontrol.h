#ifndef ANIMCONTROL_H
#define ANIMCONTROL_H

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
    GameFile * tex[num];
    // tex gp is derived from CreatureDisplayInfo, not just a random skin in the folder:
    bool definedTexture;
    // For particle colour replacements:
    int particleColInd; // ID for ParticleColor.dbc
    int PCRIndex;  // index into PCRList - list of particle color replacement values
    std::set<GeosetNum> creatureGeosetData;  // Defines which geosets are switched on for a particular display ID of a model

    TextureGroup() : count(0), base(0)
    {
      for (size_t i=0; i<num; i++)
      {
        tex[i] = 0;
      }
      particleColInd = 0;
      PCRIndex = -1;
      creatureGeosetData.clear();
      definedTexture = false;
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
      creatureGeosetData = grp.creatureGeosetData;
      definedTexture = grp.definedTexture;
    }

    bool operator<(const TextureGroup &grp) const
    {
      if (!definedTexture && grp.definedTexture)
        return false;
      if (definedTexture && !grp.definedTexture)
        return true;
      QString texname1 = tex[0]->fullname();
      QString texname2 = grp.tex[0]->fullname();
      texname1 = texname1.mid(texname1.lastIndexOf("/"));
      texname2 = texname2.mid(texname2.lastIndexOf("/"));
      if(texname1 != texname2)
        return texname1 < texname2;
      for (size_t i=0; i<num; i++)
      {
        if (tex[i]<grp.tex[i]) return true;
        if (tex[i]>grp.tex[i]) return false;
      }
      if (particleColInd < grp.particleColInd)
        return true;
      if (creatureGeosetData < grp.creatureGeosetData)
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
      if (creatureGeosetData != grp.creatureGeosetData)
        return false;
      return true;
    }

    bool operator!=(const TextureGroup &grp) const
    {
      return !((*this) == grp);
    }

};

typedef std::set<TextureGroup> TextureSet;
typedef std::vector<glm::vec4> particleColorSet; // Holds 3 particle colours: Start, Mid and End (of particle life), for cases where 
                                             // particle colours are overridden by values from ParticleColor.dbc,
typedef std::vector<particleColorSet> particleColorReplacements; // Holds 3 colour sets. The particle will get its replacement
                                                                 // colour set from 0, 1 or 2, depending on whether its
                                                                 // ParticleColorIndex is set to 11, 12 or 13

class AnimControl: public wxWindow
{
  DECLARE_CLASS(AnimControl)
  DECLARE_EVENT_TABLE()

  wxComboBox *animCList, *animCList2, *animCList3, *wmoList, *loopList;
  wxButton *showBLPList;
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
  bool FillBLPSkinSelector(TextureSet &skins, bool item = false);
  void UpdateFrameSlider(int maxRange, int tickFreq);

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
  glm::vec4 fromARGB(int color);
  void SetSkinByDisplayID(int cdi);

  // The skin the selector is showing right now, as (texture type, texture FileDataID) pairs --
  // the textures the viewport is ACTUALLY drawing. The database can only say what a model's
  // DEFAULT skin is; which of its variations is on screen is a UI fact, and this is where it
  // lives. Texture type is the WoW TEXTURE_* slot the texture feeds (TEXTURE_GAMEOBJECT1 = 11
  // for the first creature skin), i.e. TextureGroup::base + i, matching what SetSkin hands to
  // WoWModel::updateTextureList. False (and out empty) when no skin list is active or nothing
  // is selected -- e.g. a character model, or a creature with no skins at all.
  bool selectedSkinTextures(std::vector<std::pair<int, int> > & out);

  // The geosets the selected display switches on, as the same numbers the model's submeshes are
  // identified by (group * 100 + variant). A creature display can differ from another by geometry
  // rather than texture -- one horse mane instead of another -- and this is what says which.
  // Empty is meaningful, not "unknown": it means the display switches none on, so only the
  // submeshes with id 0 are drawn. See WoWModel::setCreatureGeosetData.
  bool selectedSkinGeosets(std::vector<int> & out);

  // Entries in the skin selector, and one entry's label -- diagnostics only (the -unityipctest
  // run walks the list to prove every selection reaches the embedded renderer).
  int skinCount();
  wxString skinName(int index);


  // THE funnel for animation selection. Everything that changes which animation plays goes
  // through here -- the default picked on model load, the dropdown, and the loop control -- so
  // the embedded Unity viewport can be told from one place, exactly as SetSkin does for skins.
  // index is a position in the model's animation table (WoWModel::anims), which is what the
  // dropdown's "[n]" suffix carries and what the animation manager takes.
  void SelectAnimation(int index, int loops);

  // Entries in the animation selector and one entry's label -- diagnostics only, mirroring
  // skinCount/skinName so the -unityipctest run can walk animations the same way.
  int animationCount();
  wxString animationName(int index);

  // Every creature display id this model's skins were built from, paired with the selector entry
  // it resolves to (-1 when it resolves to none). Matched exactly the way SetSkinByDisplayID
  // matches, so it reports what that call would actually do. Diagnostics only.
  void displayIdSkinIndices(std::vector<std::pair<int, int> > & out);
  int AddSkin(TextureGroup grp);
  void SetSkin(int num);
  void ActivateBLPSkinList();
  void SyncBLPSkinList();
  void SetSingleSkin(int num, int texnum);
  void SetAnimSpeed(float speed);
  void SetAnimFrame(size_t frame);
  QString GetModelFolder(WoWModel *m);

  bool defaultDoodads; 
  std::string oldname;
  QString modelFolder;
  bool modelFolderChanged, BLPListFilled;
  std::map<int, TextureGroup> CDIToTexGp;

  // Textures picked one slot at a time from the "all skins in folder" lists, as
  // texture type -> FileDataID. These sit ON TOP of whatever the main skin selector chose (and
  // SetSingleSkin's own caller clears that selection), so the displayed skin is the selector's
  // answer with these applied over it. Cleared when the model changes or a whole skin is picked.
  std::map<int, int> singleSkinOverrides;
  std::vector<particleColorReplacements> PCRList; 
  int selectedAnim;
  int selectedAnim2;
  int selectedAnim3;
  bool bOldStyle;
  bool bLockAnims;
  bool bNextAnims;
  TextureSet BLPskins;
};

#endif

