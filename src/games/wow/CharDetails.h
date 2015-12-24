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
    enum SectionType{
      SkinType = 0,
      FaceType = 1,
      FacialHairType = 2,
      HairType = 3,
      UnderwearType = 4,
      SkinTypeHD = 5,
      FaceTypeHD = 6,
      FacialHairTypeHD = 7,
      HairTypeHD = 8,
      UnderwearTypeHD = 9
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

    int getNbValuesForSection(SectionType type);
    std::vector<std::string> getTextureNameForSection(SectionType);


    // accessors to customization
    unsigned int skinColor() { return m_skinColor; }
    unsigned int skinColorMax() { return m_skinColorMax; }
    void setSkinColor(unsigned int);

    unsigned int faceType() { return m_faceType; }
    void setFaceType(unsigned int);

    unsigned int hairColor() { return m_hairColor; }
    void setHairColor(unsigned int);

    unsigned int hairStyle() { return m_hairStyle; }
    unsigned int hairStyleMax() { return m_hairStyleMax; }
    void setHairStyle(unsigned int);

    unsigned int facialHair() { return m_facialHair; }
    unsigned int facialHairMax() { return m_facialHairMax; }
    void setFacialHair(unsigned int);
    std::vector<int>validHairColors() { return m_validHairColors; }
    std::vector<int>validFaceTypes() { return m_validFaceTypes; }

  private:
    unsigned int m_skinColor, m_skinColorMax;
    unsigned int m_faceType;
    unsigned int m_hairColor;
    unsigned int m_hairStyle, m_hairStyleMax;
    unsigned int m_facialHair, m_facialHairMax;
    std::vector<int> m_validHairColors;
    std::vector<int> m_validFaceTypes;

    WoWModel * m_model;

    void updateMaxValues();
    void updateValidValues();


};



#endif /* _CHARDETAILS_H_ */
