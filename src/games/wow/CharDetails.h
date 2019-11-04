/*
* CharDetails.h
*
*  Created on: 26 oct. 2013
*
*/

#ifndef _CHARDETAILS_H_
#define _CHARDETAILS_H_

#include "database.h"
#include "RaceInfos.h"
#include "wow_enums.h"
#include "metaclasses/Observable.h"

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

  EyeGlowTypes eyeGlowType;

  bool showUnderwear, showEars, showHair, showFacialHair, showFeet, autoHideGeosetsForHeadItems;

  bool isNPC;

  RaceInfos infos;

  int geosets[NUM_GEOSETS];

  // save + load
  void save(QXmlStreamWriter &);
  void load(QString &);


  void reset(WoWModel *);

  void print();

  std::vector<int> getTextureForSection(BaseSectionType);
  std::vector<int> getRegionForSection(BaseSectionType);

  // accessors to customization
  void set(CustomizationType type, uint val);
  uint get(CustomizationType type) const;

  CustomizationParam getParams(CustomizationType type);

  void setRandomValue(CustomizationType type);

  std::vector<CustomizationType> getCustomizationOptions() const;

  void setDemonHunterMode(bool);
  bool isDemonHunter() const { return m_isDemonHunter; }

private:
  WoWModel * m_model;

  void fillCustomizationMap();
  QString getCustomizationName(BaseSectionType section, uint raceID, uint sexID, bool secondCustomization = false);
  std::map<CustomizationType, CustomizationParam> m_customizationParamsMap;
  std::map<CustomizationType, std::map<int, CustomizationParam> > m_multiCustomizationMap;


  std::map<CustomizationType, uint> m_currentCustomization;

  bool m_isDemonHunter;
  QString m_dhModel;
};



#endif /* _CHARDETAILS_H_ */
