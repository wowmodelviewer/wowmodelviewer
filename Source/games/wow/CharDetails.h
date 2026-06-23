/*
* CharDetails.h
*
*  Created on: 26 oct. 2013
*
*/

#ifndef _CHARDETAILS_H_
#define _CHARDETAILS_H_

#include <set>

#include "CharTexture.h"
#include "database.h"
#include "RaceInfos.h"
#include "wow_enums.h"

#include "metaclasses/Observable.h"

class sqlResult;
class WoWModel;
class QXmlStreamWriter;
class QXmlStreamReader;


#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _CHARDETAILS_API_ __declspec(dllexport)
#    else
#        define _CHARDETAILS_API_ __declspec(dllimport)
#    endif
#else
#    define _CHARDETAILS_API_
#endif

class _CHARDETAILS_API_ CharDetails : public Observable
{
public:
  CharDetails();

  // Types
  enum BaseSectionType
  {
    SkinBaseType = 0,
    FaceBaseType = 1,
    FacialHairBaseType = 2,
    HairBaseType = 3,
    UnderwearBaseType = 4,
    Custom1BaseType = 5,
    Custom2BaseType = 6,
    Custom3BaseType = 7
  };
  
  // Types
  enum SectionType
  {
    SkinType = 0,
    FaceType = 1,
    FacialHairType = 2,
    HairType = 3,
    UnderwearType = 4,
    SkinTypeHD = 5,
    FaceTypeHD = 6,
    FacialHairTypeHD = 7,
    HairTypeHD = 8,
    UnderwearTypeHD = 9,
    Custom1Type = 11,
    Custom1TypeHD = 12,
    Custom2Type = 13,
    Custom2TypeHD = 14,
    Custom3Type = 15,
    Custom3TypeHD = 16
  };
  
  enum CustomizationType
  {
    SKIN_COLOR = 0,
    FACE = 1,
    FACIAL_CUSTOMIZATION_STYLE = 2,
    FACIAL_CUSTOMIZATION_COLOR = 3,
    ADDITIONAL_FACIAL_CUSTOMIZATION = 4,
    CUSTOM1_STYLE = 5,
    CUSTOM1_COLOR = 8,
    CUSTOM2_STYLE = 6,
    CUSTOM3_STYLE = 7
  };

  // Flags from CharSections.db2 that tell us what types of character (regular, death knight, demon hunter, etc.) the section is used for:
  enum SectionFlags
  {
    SF_CHARACTER_CREATE  = 0x1,   // available on the character create screen of the client. Includes DK and DH skins
    SF_BARBERSHOP        = 0x2,
    SF_DEATH_KNIGHT      = 0x4,
    SF_NPC_SPECIAL       = 0x8,   // a random bunch of things. Some Twilight skins, mummies, wooden dolls, etc.
    SF_REGULAR           = 0x10,  // only used on regular appearances, sometimes also Demon Hunters
    SF_DEMON_HUNTER      = 0x20,
    SF_DEMON_HUNTER_FACE = 0x40,  // unsure why these have a different flag to other Demon Hunter skins
    SF_DEMON_HUNTER_BFX  = 0x80,  // just for a couple of Demon Hunter blindfolds. Unsure why
    SF_SILHOUETTE        = 0x100, // black / shadow, used for some in-game displays
    SF_VOID_ELF_SPECIAL  = 0x200  // just the Void Elf saturated purple skin for Entropic Embrace
  };

  class CustomizationParam
  {
  public:
    QString name;
    std::vector<int> possibleValues;
    std::vector<int> flags;
  };

  class TextureCustomization
  {
  public:
    uint layer;
    int region;
    uint type;
    uint fileId;
    uint blendMode;
  };

  EyeGlowTypes eyeGlowType;

  bool showUnderwear, showEars, showHair, showFacialHair, showFeet, autoHideGeosetsForHeadItems;

  bool isNPC;

  std::map <uint, uint> geosets; // map <geoset type, geosetid>
  std::vector<TextureCustomization> textures;

  // save + load
  void save(QXmlStreamWriter &);
  void load(QString &);

  void reset(WoWModel * m = nullptr);
  void randomise();

  // accessors to customization
  uint get(uint chrCustomizationOptionID) const;
  void set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID);
  std::vector<uint> getCustomizationChoices(const uint chrCustomizationOptionID);

  // True if chrCustomizationOptionID is one of THIS model's customization options
  // (i.e. it belongs to the model's ChrModelID). Used to reject foreign options --
  // the armory appearance API returns the character's dragonriding-drake options too,
  // and applying a drake "Skin Color" to a humanoid paints the drake's scale texture
  // over the body. Valid only after the model is set (reset()/fillCustomizationMap()).
  bool hasOption(uint chrCustomizationOptionID) const;
 
  void setDemonHunterMode(bool val);
  bool isDemonHunter() const { return isDemonHunter_; }

  // True while reset()/randomise() are applying a choice to every option in one
  // pass. Listeners (e.g. CharControl) must NOT trigger a full model refresh per
  // CHOICE_LIST_CHANGED during a batch -- the batch refreshes once at the end.
  bool isBatching() const { return batchUpdate_; }

  // True if a customization option for this model drives the given geoset GROUP
  // (GeosetType). E.g. false for the ears group (CG_EARS = 7) on Gnome -- their ears
  // are fixed geometry -- but true on Mechagnome (a "Modification" option), a drake
  // (group 7 repurposed as Tail/Throat/Effects), or any race with an "Ears" option.
  // The ear-visibility net in WoWModel::refresh() uses this to act ONLY on fixed ears.
  bool isGeosetGroupCustomized(int geosetGroup) const { return customizationControlledGroups_.count(geosetGroup) != 0; }

  void refresh();

  // Selective application of customization-choice geosets to the model:
  // for each active option, show the active choice's geoset(s) and hide the
  // other choices' geoset(s); geosets not referenced by a choice keep their
  // default visibility. Call after the default geoset rule, before equipment.
  void applyCustomizationGeosets();

private:

  void fillCustomizationMap();
  void setRandomValue(CustomizationType type);

  WoWModel * model_;
  bool isDemonHunter_;

  std::map<uint, uint> currentCustomization_; // map <ChrCustomizationOption::ID, ChrCustomizationChoice::ID>

  class CustomizationElements
  {
  public:
    std::vector<std::pair<uint, uint> > geosets; // std::vector<std::pair<GeosetType, GeosetID>>
    std::vector<TextureCustomization> textures; 
    std::vector<std::pair<uint, std::pair<uint, uint> > > models; // std::vector<std::pair<GameFileId, std::pair<GeosetType, GeosetID>>>
    void clear()
    {
      geosets.clear();
      textures.clear();
      models.clear();
    }
  };

  void fillCustomizationMapForOption(uint chrCustomizationOption);

  bool applyChrCustomizationElements(uint chrCustomizationOption, sqlResult &);
  static int bitMaskToSectionType(int mask);
  std::vector<int> getParentOptions(uint chrCustomizationOption);
  int getChildOption(uint chrCustomizationOption);

  void initLinkedOptionsMap();

  void refreshGeosets();
  void refreshTextures();
  void refreshSkinnedModels();

  std::map<uint, std::vector<uint> > choicesPerOptionMap_; // map < ChrCustomizationOption::ID, vector <ChrCustomizationChoice::ID> >
  std::map<uint, uint> optionFlags_; // map < ChrCustomizationOption::ID, ChrCustomizationOption::Flags >

  // Geoset GROUPS (GeosetType) that any customization option for this model drives.
  // Computed once in fillCustomizationMap(); lets fixed geometry be told from
  // customization-owned geometry. See isGeosetGroupCustomized().
  std::set<int> customizationControlledGroups_;

  // ChrCustomizationChoice::ID -> its geoset elements: each is
  // { geosetId (GeosetType*100 + GeosetID), RelatedChrCustomizationChoiceID }.
  // A choice can carry several geoset elements, each gated by a related choice
  // (0 = unconditional). Resolved + cached from ChrCustomizationElement.
  std::map<uint, std::vector<std::pair<int, uint> > > choiceGeosetElements_;
  const std::vector<std::pair<int, uint> > & getChoiceGeosetElements(uint chrCustomizationChoiceID);

  // ChrCustomizationChoice::ID -> its ChrCustomizationReq fields. Used to hide choices
  // that don't apply to this character: class/race-gated (e.g. Demon-Hunter-only
  // horns/blindfold on a non-DH) and "borrowed"/collectible appearances the account
  // would have to unlock (achievement/quest/transmog-item). A choice absent from the
  // map has no requirement and is always available. See isChoiceAvailable().
  struct ChoiceReq
  {
    long long raceMask = 0;
    long long classMask = 0;
    bool unlockGated = false; // ReqAchievementID / ReqQuestID / ReqItemModifiedAppearanceID set
  };
  std::map<uint, ChoiceReq> choiceReq_;
  bool isChoiceAvailable(uint chrCustomizationChoiceID) const;

  // When a choice adds a skinned model whose texture comes from a direct-bind material
  // gated by another option (e.g. the DH blindfold texture is gated by the DH eye-glow
  // colour), switch that gating option to a compatible value so the model isn't merged
  // untextured (white). Mirrors the in-game texture-gating behaviour.
  void autoSelectTextureGating(uint chrCustomizationChoiceID);
  bool autoSelectInProgress_ = false;

  // When true, set() applies the choice but skips the expensive model_->refresh()
  // (texture re-composite + skinned-model reload + geoset merge). randomise() uses
  // this to apply all ~45 options and refresh ONCE at the end instead of per option.
  bool batchUpdate_ = false;
  std::map<uint, CustomizationElements> customizationElementsPerOption_; // keep track of current elements applied for a given option
  std::vector<std::pair<uint, std::pair<uint, uint> > > models_; // vector < pair < GameFileId, pair <GeosetType, GeosetID> > >

  static std::multimap<uint, int> LINKED_OPTIONS_MAP_; // multimap < child ChrCustomizationOption::ID, parent ChrCustomizationOption::ID>
                                                       // (ie, <markings color, markings> or <tattoo color, tattoo>)
                                                       // some child options are multi parent (ie Tauren facial Markings & body markings are sharing same color)
};



#endif /* _CHARDETAILS_H_ */
