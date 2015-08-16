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
struct TabardDetails;

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
    // Types
    enum SectionType{
      SkinType = 0,
      FaceType = 1,
      FacialHairType = 2,
      HairType = 3,
      UnderwearType = 4
    };

    size_t eyeGlowType;
    size_t race, gender;

    bool showUnderwear, showEars, showHair, showFacialHair, showFeet;

    bool isNPC;

    RaceInfos infos;

    int geosets[NUM_GEOSETS];

    // save + load equipment
    void save(std::string fn, TabardDetails *td);
    bool load(std::string fn, TabardDetails *td);


    void reset(WoWModel *);

    void print();

    int getNbValuesForSection(SectionType type);
    std::vector<std::string> getTextureNameForSection(SectionType);


    // accessors to customization
    size_t skinColor() { return m_skinColor; }
    size_t skinColorMax() { return m_skinColorMax; }
    void setSkinColor(size_t);

    size_t faceType() { return m_faceType; }
    size_t faceTypeMax() { return m_faceTypeMax; }
    void setFaceType(size_t);

    size_t hairColor() { return m_hairColor; }
    size_t hairColorMax() { return m_hairColorMax; }
    void setHairColor(size_t);

    size_t hairStyle() { return m_hairStyle; }
    size_t hairStyleMax() { return m_hairStyleMax; }
    void setHairStyle(size_t);

    size_t facialHair() { return m_facialHair; }
    size_t facialHairMax() { return m_facialHairMax; }
    void setFacialHair(size_t);

  private:
    size_t m_skinColor, m_skinColorMax;
    size_t m_faceType, m_faceTypeMax;
    size_t m_hairColor, m_hairColorMax;
    size_t m_hairStyle, m_hairStyleMax;
    size_t m_facialHair, m_facialHairMax;

    WoWModel * m_model;

    void updateMaxValues();



};



#endif /* _CHARDETAILS_H_ */
