/*
 * CharDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharDetails.h"

#include <iostream>
#include "CharDetailsEvent.h"
#include "GameDatabase.h"
#include "TabardDetails.h"
#include "WoWModel.h"
#include "logger/Logger.h"

void CharDetails::save(std::string fn, TabardDetails *td)
{
	// TODO: save/load as xml?
	// wx/xml/xml.h says the api will change, do not use etc etc.
	std::ofstream f;
	f.open(fn, std::ofstream::out | std::ofstream::app);
	if(f.is_open())
	{
		f << (int)race << " " << (int)gender << endl;
		f << (int)m_skinColor << " " << (int)m_faceType << " " << (int)m_hairColor << " " << (int)m_hairStyle << " " << (int)m_facialHair << endl;
	// @TODO : to repair
	/* for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f << equipment[i] << endl;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if ((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) {
		f << td->Background << wxT(" ") << td->Border << wxT(" ") << td->BorderColor << wxT(" ") << td->Icon << wxT(" ") << td->IconColor << endl;
	}
	*/
	  f.close();
	}
}

bool CharDetails::load(std::string fn, TabardDetails *td)
{
	unsigned int r, g;
	bool same = false;

	// for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
			// equipment[i] = 0;

	std::ifstream f(fn.c_str());

	if(f.is_open())
	{
	  f >> r >> g;

	  if (r==race && g==gender) {
#if defined _WINDOWS
		f >> m_skinColor >> m_faceType >> m_hairColor >> m_hairStyle >> m_facialHair;
#endif
		same = true;
	  } else {
		int dummy;
		for (size_t i=0; i<6; i++) f >> dummy;
	  }

	  // @TODO : to repair
	  /*
		for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f >> tmp;

		if (tmp > 0)
			equipment[i] = tmp;
		}

		// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
		if (((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) && !input.Eof()) {
		f >> td->Background >> td->Border >> td->BorderColor >> td->Icon >> td->IconColor;
		td->showCustom = true;
		}
	   */
	  f.close();
	}
	return same;
}

void CharDetails::reset(WoWModel * model)
{
  m_model = model;

  m_skinColor = 0;
  m_faceType = 0;
  m_hairColor = 0;
  m_hairStyle = 0;
  m_facialHair = 0;

  showUnderwear = true;
  showHair = true;
  showFacialHair = true;
  showEars = true;
  showFeet = false;

  isNPC = false;

  updateMaxValues();
}

void CharDetails::setSkinColor(size_t val)
{
  if(val != m_skinColor)
  {
    m_skinColor = val;
    updateMaxValues();
    CharDetailsEvent event(this, CharDetailsEvent::SKINCOLOR_CHANGED);
    notify(event);
  }
}

void CharDetails::setFaceType(size_t val)
{
  if(val != m_faceType)
  {
    m_faceType = val;
    updateMaxValues();
    CharDetailsEvent event(this, CharDetailsEvent::FACETYPE_CHANGED);
    notify(event);
  }
}

void CharDetails::setHairColor(size_t val)
{
  if(val != m_hairColor)
  {
    m_hairColor = val;
    updateMaxValues();
    CharDetailsEvent event(this, CharDetailsEvent::HAIRCOLOR_CHANGED);
    notify(event);
  }
}

void CharDetails::setHairStyle(size_t val)
{
  if(val != m_hairStyle)
  {
    m_hairStyle = val;
    updateMaxValues();
    CharDetailsEvent event(this, CharDetailsEvent::HAIRSTYLE_CHANGED);
    notify(event);
  }
}

void CharDetails::setFacialHair(size_t val)
{
  if(val != m_facialHair)
  {
    m_facialHair = val;
    updateMaxValues();
    CharDetailsEvent event(this, CharDetailsEvent::FACIALHAIR_CHANGED);
    notify(event);
  }
}

void CharDetails::updateMaxValues()
{
  if(!m_model)
    return;

  m_faceTypeMax = getNbValuesForSection(FaceType);
  m_skinColorMax = getNbValuesForSection(SkinType);
  m_hairColorMax = getNbValuesForSection(HairType);

  RaceInfos infos;
  RaceInfos::getCurrent(m_model->name().toStdString(), infos);

  QString query = QString("SELECT MAX(VariationIndex) FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
                        .arg(race)
                        .arg(gender)
                        .arg(infos.isHD?8:3);

  sqlResult hairStyles = GAMEDATABASE.sqlQuery(query);

  if(hairStyles.valid && !hairStyles.values.empty())
  {
    m_hairStyleMax = hairStyles.values[0][0].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of hair styles for model" << m_model->name();
    m_hairStyleMax = 0;
  }


  query = QString("SELECT MAX(VariationID) FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2")
                            .arg(race)
                            .arg(gender);

  sqlResult facialHairStyles = GAMEDATABASE.sqlQuery(query);
  if(facialHairStyles.valid && !facialHairStyles.values.empty())
  {
    m_facialHairMax = facialHairStyles.values[0][0].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair styles for model" << m_model->name();
    m_facialHairMax = 0;
  }

  if (m_faceTypeMax == 0) m_faceTypeMax = 1;
  if (m_skinColorMax == 0) m_skinColorMax = 1;
  if (m_hairColorMax == 0) m_hairColorMax = 1;
  if (m_hairStyleMax == 0) m_hairStyleMax = 1;
  if (m_facialHairMax == 0) m_facialHairMax = 1;
}

std::vector<std::string> CharDetails::getTextureNameForSection(SectionType section)
{
  std::vector<std::string> result;

  RaceInfos infos;
  if(!RaceInfos::getCurrent(m_model->name().toStdString(), infos))
    return result;

/*
  std::cout << __FUNCTION__ << std::endl;
  std::cout << "----------------------------------------------" << std::endl;
  std::cout << "infos.raceid = " << infos.raceid << std::endl;
  std::cout << "infos.sexid = " << infos.sexid << std::endl;
  std::cout << "infos.textureLayoutID = " << infos.textureLayoutID << std::endl;
  std::cout << "infos.isHD = " << infos.isHD << std::endl;
  std::cout << "cd.skinColor() = " << skinColor() << std::endl;
  std::cout << "section = " << section << std::endl;

  std::cout << "----------------------------------------------" << std::endl;
*/

  size_t type = section;

  if(infos.isHD) // HD layout
    type+=5;

  QString query;
  switch(section)
  {
    case SkinType:
    case UnderwearType:
      query = QString("SELECT TextureName1, TextureName2, TextureName3 FROM CharSections WHERE \
              (RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND SectionType=%4)")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(skinColor())
              .arg(type);
      break;
    case FaceType:
      query = QString("SELECT TextureName1, TextureName2, TextureName3 FROM CharSections WHERE \
              (RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND VariationIndex=%4 AND SectionType=%5)")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(skinColor())
              .arg(faceType())
              .arg(type);
      break;
    case HairType:
        query = QString("SELECT TextureName1, TextureName2, TextureName3 FROM CharSections \
              WHERE (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4 AND SectionType=%5)")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(hairStyle()?hairStyle():1)
              .arg(hairColor())
              .arg(type);
      break;
    case FacialHairType:
      query = QString("SELECT TextureName1, TextureName2, TextureName3 FROM CharSections WHERE \
                  (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4 AND SectionType=%5)")
                  .arg(infos.raceid)
                  .arg(infos.sexid)
                  .arg(facialHair())
                  .arg(hairColor())
                  .arg(type);
      break;
    default:
      query = "";
  }

  if(query != "")
  {
    sqlResult vals = GAMEDATABASE.sqlQuery(query);
    if(vals.valid && !vals.values.empty())
    {
      for(size_t i = 0; i < vals.values[0].size() ; i++)
        if(!vals.values[0][i].isEmpty())
          result.push_back(vals.values[0][i].toStdString());
    }
    else
    {
      LOG_ERROR << "Unable to collect infos for model";
      LOG_ERROR << query << vals.valid << vals.values.size();
    }
  }

  return result;
}

int CharDetails::getNbValuesForSection(SectionType section)
{
  int result = 0;

  RaceInfos infos;
  if(!RaceInfos::getCurrent(m_model->name().toStdString(), infos))
    return result;

  size_t type = section;

  if(infos.isHD)
    type+=5;

  QString query;
  switch(section)
  {
    case SkinType:
      query = QString("SELECT COUNT(*)-1 FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(type);
      break;
    case FaceType:
      query = QString("SELECT COUNT(*)-1 FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND SectionType=%4")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(skinColor())
              .arg(type);
      break;
    case HairType:
      query = QString("SELECT COUNT(*)-1 FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND SectionType=%4")
              .arg(infos.raceid)
              .arg(infos.sexid)
              .arg(hairStyle())
              .arg(type);
      break;
    default:
      query = "";
  }

  sqlResult vals = GAMEDATABASE.sqlQuery(query);

  if(vals.valid && !vals.values.empty())
  {
    result = vals.values[0][0].toInt();

  }
  else
  {
    LOG_ERROR << "Unable to collect number of customization for model" << m_model->name();
  }

  return result;
}
