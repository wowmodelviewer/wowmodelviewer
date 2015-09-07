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
      UnderwearType = 4
    };

    EyeGlowTypes eyeGlowType;

    bool showUnderwear, showEars, showHair, showFacialHair, showFeet;

    bool isNPC;

    RaceInfos infos;

    int geosets[NUM_GEOSETS];

    // save + load
    void save(QXmlStreamWriter &);
    void load(QXmlStreamReader &);


    void reset(WoWModel *);

    void print();

    int getNbValuesForSection(SectionType type);
    std::vector<std::string> getTextureNameForSection(SectionType);


    // accessors to customization
    unsigned int skinColor() { return m_skinColor; }
    unsigned int skinColorMax() { return m_skinColorMax; }
    void setSkinColor(unsigned int);

    unsigned int faceType() { return m_faceType; }
    unsigned int faceTypeMax() { return m_faceTypeMax; }
    void setFaceType(unsigned int);

    unsigned int hairColor() { return m_hairColor; }
    unsigned int hairColorMax() { return m_hairColorMax; }
    void setHairColor(unsigned int);

    unsigned int hairStyle() { return m_hairStyle; }
    unsigned int hairStyleMax() { return m_hairStyleMax; }
    void setHairStyle(unsigned int);

    unsigned int facialHair() { return m_facialHair; }
    unsigned int facialHairMax() { return m_facialHairMax; }
    void setFacialHair(unsigned int);

  private:
    unsigned int m_skinColor, m_skinColorMax;
    unsigned int m_faceType, m_faceTypeMax;
    unsigned int m_hairColor, m_hairColorMax;
    unsigned int m_hairStyle, m_hairStyleMax;
    unsigned int m_facialHair, m_facialHairMax;

    WoWModel * m_model;

    void updateMaxValues();



};



#endif /* _CHARDETAILS_H_ */
