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
  // Makes the choice current for its option, re-validates the options whose requirements name
  // that option's choices (keeping each current choice that stays valid, otherwise taking the first
  // valid one in client order), rebuilds the applied elements from the resulting choices and
  // refreshes the model once. A choice that is not one of the option's choices on this model, or an
  // option whose own requirement fails with the current choices, is ignored.
  void set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID);
  // The choices of an option that are valid with the current choices, in client order (OrderIndex,
  // ID); empty when the option itself is not available.
  std::vector<uint> getCustomizationChoices(const uint chrCustomizationOptionID) const;
  // The options this character can use with the current choices, in client order (OrderIndex, ID):
  // an option is listed when its own requirement holds and at least one of its choices is valid.
  std::vector<uint> getCustomizationOptions() const;
  bool isOptionAvailable(uint chrCustomizationOptionID) const;
  bool isChoiceAvailable(uint chrCustomizationChoiceID) const;

  // True if chrCustomizationOptionID is one of THIS model's customization options
  // (i.e. it belongs to the model's ChrModelID). Used to reject foreign options --
  // the armory appearance API returns the character's dragonriding-drake options too,
  // and applying a drake "Skin Color" to a humanoid paints the drake's scale texture
  // over the body. Valid only after the model is set (reset()/fillCustomizationMap()).
  bool hasOption(uint chrCustomizationOptionID) const;
 
  void setDemonHunterMode(bool val);
  bool isDemonHunter() const { return isDemonHunter_; }

  // True while a change is being recorded or announced before its single model refresh: while
  // load() records the saved choices, and while set()/reset()/randomise()/setDemonHunterMode()
  // send their CHOICE_LIST_CHANGED / OPTION_LIST_CHANGED events. Listeners (e.g. CharControl)
  // must NOT trigger a full model refresh per event during a batch -- it refreshes once at the end.
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

  // ---- Requirements (ChrCustomizationReq) -----------------------------------------------------
  // One evaluator decides for options (ChrCustomizationOption.Requirement) and choices
  // (ChrCustomizationChoice.ChrCustomizationReqID) alike; see evaluateRequirement().

  // A requirement row as this character's options and choices use it.
  struct Requirement
  {
    uint id = 0;
    int reqType = 0;
    int classMask = 0;
    int regionGroupMask = 0;          // loaded and reported, not evaluated
    int overrideArchive = 0;          // loaded and reported, not evaluated
    unsigned long long raceMask = 0;  // RaceMasks[1] << 32 | RaceMasks[0]
    bool unlockGated = false;         // ReqAchievementID, ReqQuestID or ReqItemModifiedAppearanceID set
    std::vector<uint> prerequisiteChoices; // ChrCustomizationReqChoice rows, in table order
  };

  enum RequirementResult
  {
    REQUIREMENT_MET = 0,
    REQUIREMENT_NOT_PLAYER,   // ReqType bit 0 clear
    REQUIREMENT_RACE,         // the race's PlayableRaceBit is not set in RaceMasks
    REQUIREMENT_CLASS,        // ClassMask does not admit the class context
    REQUIREMENT_UNLOCK,       // an achievement, quest or item appearance unlocks it
    REQUIREMENT_PREREQUISITE  // none of its prerequisite choices is current
  };

  // The rules, one function each (see their definitions for the evidence behind them).
  static bool isPlayerRequirement(int reqType);
  static bool raceMaskAllows(unsigned long long raceMask, int playableRaceBit);
  static bool classMaskAllows(int classMask, bool demonHunter);

  // A requirement of this character's options or choices, evaluated against a selection
  // (ChrCustomizationOption::ID -> ChrCustomizationChoice::ID). 0 is no requirement and is met.
  RequirementResult evaluateRequirement(uint requirementID, const std::map<uint, uint> & selection) const;
  // The loaded row, or nullptr when no option or choice of this character uses the requirement.
  const Requirement * requirement(uint requirementID) const;
  // ChrRaces.PlayableRaceBit of the character's race (-1: none).
  int playableRaceBit() const { return playableRaceBit_; }

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

  // One ChrCustomizationElement row of a choice. Its effect is resolved (for this model's texture
  // layout) the first time it is applied.
  struct ChoiceElementRow
  {
    uint id = 0;
    uint related = 0; // RelatedChrCustomizationChoiceID: applies only while that choice is current (0 = always)
    uint geosetID = 0, skinnedModelID = 0, materialID = 0, boneSetID = 0, condModelID = 0, displayInfoID = 0;
  };
  std::map<uint, std::vector<ChoiceElementRow> > choiceElementRows_; // ChrCustomizationChoice::ID -> its rows, by element ID
  std::map<uint, CustomizationElements> resolvedElements_;          // ChrCustomizationElement::ID -> its effect
  const std::vector<ChoiceElementRow> & getChoiceElementRows(uint chrCustomizationChoiceID);
  const CustomizationElements & resolveElement(const ChoiceElementRow & row);

  static int bitMaskToSectionType(int mask);

  // The choices of an option valid with a selection, in client order; empty when the option's own
  // requirement fails.
  std::vector<uint> validChoices(uint chrCustomizationOptionID, const std::map<uint, uint> & selection) const;
  // The options whose requirements name choices of this option, directly or through another such
  // option.
  std::set<uint> dependentOptions(uint chrCustomizationOptionID) const;
  // Re-validates the options in scope (all when null) in resolution order: no valid choice -> the
  // option has no current choice; a current choice still valid -> kept; otherwise the first valid
  // choice in client order. Records choices only; nothing is applied or refreshed.
  void resolveSelection(const std::set<uint> * scope);
  // Replaces every option's applied elements with the elements of the current choices, related
  // gates judged against the current choices.
  void rebuildCustomizationElements();

  struct SelectionState
  {
    std::map<uint, uint> selection;
    std::map<uint, std::vector<uint> > choices; // option -> valid choices
  };
  SelectionState captureSelectionState() const;
  // After the choices changed: rebuild the elements, tell observers which options changed (and
  // whether the set of available options did), then refresh the model once.
  void applySelection(const SelectionState & before, uint changedOption);
  void logUnresolvedRequirement(uint requirementID, const QString & why) const;

  void refreshGeosets();
  void refreshTextures();
  void refreshSkinnedModels();

  std::map<uint, std::vector<uint> > choicesPerOptionMap_; // map < ChrCustomizationOption::ID, vector <ChrCustomizationChoice::ID> > (client order)
  std::map<uint, uint> optionFlags_; // map < ChrCustomizationOption::ID, ChrCustomizationOption::Flags >
  std::vector<uint> optionClientOrder_;   // this model's options by OrderIndex, ID
  std::vector<uint> optionResolveOrder_;  // the same, each option after the options its requirements name
  std::map<uint, std::set<uint> > dependentOptions_; // option -> options whose requirements name its choices
  std::map<uint, uint> optionRequirement_;  // ChrCustomizationOption::ID -> ChrCustomizationReq::ID (0: none)
  std::map<uint, uint> choiceRequirement_;  // ChrCustomizationChoice::ID -> ChrCustomizationReq::ID
  std::map<uint, uint> choiceOption_;       // ChrCustomizationChoice::ID -> ChrCustomizationOption::ID
  std::map<uint, Requirement> requirements_;
  int playableRaceBit_ = -1;
  mutable std::set<uint> unresolvedRequirementsLogged_;

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

  // When a choice adds a skinned model whose texture comes from a direct-bind material
  // gated by another option (e.g. the DH blindfold texture is gated by the DH eye-glow
  // colour), switch that gating option to a compatible value so the model isn't merged
  // untextured (white). Mirrors the in-game texture-gating behaviour. Records the choice
  // (and re-validates the options depending on it); the caller applies.
  void autoSelectTextureGating(uint chrCustomizationChoiceID);

  // True while load() records the saved choices: set() only records them, and load() resolves
  // and applies the whole selection once at the end.
  bool batchUpdate_ = false;
  std::map<uint, CustomizationElements> customizationElementsPerOption_; // the elements applied for each option's current choice
  std::vector<std::pair<uint, std::pair<uint, uint> > > models_; // vector < pair < GameFileId, pair <GeosetType, GeosetID> > >
};



#endif /* _CHARDETAILS_H_ */
