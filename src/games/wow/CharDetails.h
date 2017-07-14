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
    TatooType = 11
  };

  enum CustomizationType
  {
    SKIN_COLOR = 0,
    FACE = 1,
    FACIAL_CUSTOMIZATION_STYLE = 2,
    FACIAL_CUSTOMIZATION_COLOR = 3,
    ADDITIONAL_FACIAL_CUSTOMIZATION = 4,
    DH_TATTOO_STYLE = 5,
    DH_TATTOO_COLOR = 6,
    DH_HORN_STYLE = 7,
    DH_BLINDFOLDS = 8
  };

  class CustomizationParam
  {
  public:
    std::string name;
    std::vector<int> possibleValues;
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

  std::vector<int> getTextureForSection(SectionType);


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

  std::map<CustomizationType, CustomizationParam> m_customizationParamsMap;
  std::map<CustomizationType, std::map<int, CustomizationParam> > m_multiCustomizationMap;


  std::map<CustomizationType, uint> m_currentCustomization;

  bool m_isDemonHunter;
};



#endif /* _CHARDETAILS_H_ */
