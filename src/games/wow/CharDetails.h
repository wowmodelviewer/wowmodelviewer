/*
* CharDetails.h
*
*  Created on: 26 oct. 2013
*
*/

#ifndef _CHARDETAILS_H_
#define _CHARDETAILS_H_

#include "CharTexture.h"
#include "database.h"
#include "RaceInfos.h"
#include "wow_enums.h"

#include "metaclasses/Observable.h"

class sqlResult;
class WoWModel;
class QXmlStreamWriter;
class QXmlStreamReader;

class CharDetails : public Observable
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
 
  void setDemonHunterMode(bool val) { isDemonHunter_ = val; }
  bool isDemonHunter() const { return isDemonHunter_; }

  void refresh();

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
  std::map<uint, CustomizationElements> customizationElementsPerOption_; // keep track of current elements applied for a given option
  std::vector<std::pair<uint, std::pair<uint, uint> > > models_; // vector < pair < GameFileId, pair <GeosetType, GeosetID> > >

  static std::multimap<uint, int> LINKED_OPTIONS_MAP_; // multimap < child ChrCustomizationOption::ID, parent ChrCustomizationOption::ID>
                                                       // (ie, <markings color, markings> or <tattoo color, tattoo>)
                                                       // some child options are multi parent (ie Tauren facial Markings & body markings are sharing same color)
};



#endif /* _CHARDETAILS_H_ */
