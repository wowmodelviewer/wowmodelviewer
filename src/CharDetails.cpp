/*
 * CharDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharDetails.h"

#include "TabardDetails.h"
#include "util.h" // correctType

#include "CharDetailsEvent.h"
#include "GameDatabase.h"
#include "globalvars.h"
#include "logger/Logger.h"
#include "modelviewer.h"

#include <wx/wfstream.h>

void CharDetails::save(wxString fn, TabardDetails *td)
{
	// TODO: save/load as xml?
	// wx/xml/xml.h says the api will change, do not use etc etc.
	wxFFileOutputStream output( fn );
    wxTextOutputStream f( output );
	if (!output.IsOk())
		return;
	f << (int)race << wxT(" ") << (int)gender << endl;
	f << (int)m_skinColor << wxT(" ") << (int)m_faceType << wxT(" ") << (int)m_hairColor << wxT(" ") << (int)m_hairStyle << wxT(" ") << (int)m_facialHair << endl;
	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f << equipment[i] << endl;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if ((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) {
		f << td->Background << wxT(" ") << td->Border << wxT(" ") << td->BorderColor << wxT(" ") << td->Icon << wxT(" ") << td->IconColor << endl;
	}
	output.Close();
}

bool CharDetails::load(wxString fn, TabardDetails *td)
{
	unsigned int r, g;
	int tmp;
	bool same = false;

	// for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
			// equipment[i] = 0;

	wxFFileInputStream input( fn );
	if (!input.IsOk())
		return false;
	wxTextInputStream f( input );

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

	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f >> tmp;

		//
		if (tmp > 0)
			equipment[i] = tmp;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if (((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) && !input.Eof()) {
		f >> td->Background >> td->Border >> td->BorderColor >> td->Icon >> td->IconColor;
		td->showCustom = true;
	}

	//input.Close();
	return same;
}

void CharDetails::loadSet(ItemSetDB &sets, ItemDatabase &items, int setid)
{
	try {
		ItemSetDB::Record rec = sets.getById(setid);
		for (size_t i=0; i<ItemSetDB::NumItems; i++) {
			int id = rec.getInt(ItemSetDB::ItemIDBaseV400 + i);

			const ItemRecord &r = items.getById(id);
			if (r.type > 0) {
				// find a slot for it
				for (ssize_t s=0; s<NUM_CHAR_SLOTS; s++) {
					if (correctType((ssize_t)r.type, s)) {
						equipment[s] = id;
						break;
					}
				}
			}
		}
	} catch (ItemSetDB::NotFound &) {}
}

void CharDetails::loadStart(StartOutfitDB &start, ItemDatabase &items, int setid)
{
	try {
		StartOutfitDB::Record rec = start.getById(setid);
		for (size_t i=0; i<StartOutfitDB::NumItems; i++) {
			int id = rec.getInt(StartOutfitDB::ItemIDBase + i);
			if (id==0) continue;
			const ItemRecord &r = items.getById(id);
			if (r.type > 0) {
				// find a slot for it
				for (ssize_t s=0; s<NUM_CHAR_SLOTS; s++) {
					if (correctType((ssize_t)r.type, s)) {
						equipment[s] = id;
						break;
					}
				}
			}
		}
	} catch (ItemSetDB::NotFound &) {}
}

void CharDetails::reset()
{
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

	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		equipment[i] = 0;
	}

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
  m_faceTypeMax = getNbValuesForSection(FaceType);
  m_skinColorMax = getNbValuesForSection(SkinType);
  m_hairColorMax = getNbValuesForSection(HairType);

  RaceInfos infos;
  RaceInfos::getCurrent(std::string(g_modelViewer->charControl->model->wxname.mb_str()), infos);

  QString query = QString("SELECT MAX(VariationIndex) FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
                        .arg(race)
                        .arg(gender)
                        .arg(infos.isHD?8:3);

  sqlResult hairStyles = GAMEDATABASE.sqlQuery(query.toStdString());

  if(hairStyles.valid && !hairStyles.values.empty())
  {
    m_hairStyleMax = atoi(hairStyles.values[0][0].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of hair styles for model" << g_canvas->model->wxname.c_str();
    m_hairStyleMax = 0;
  }


  query = QString("SELECT MAX(VariationID) FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2")
                            .arg(race)
                            .arg(gender);

  sqlResult facialHairStyles = GAMEDATABASE.sqlQuery(query.toStdString());
  if(facialHairStyles.valid && !facialHairStyles.values.empty())
  {
    m_facialHairMax = atoi(facialHairStyles.values[0][0].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair styles for model" << g_canvas->model->wxname.c_str();
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
  if(!RaceInfos::getCurrent(std::string(g_modelViewer->charControl->model->wxname.mb_str()), infos))
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

  //LOG_INFO << query;

  if(query != "")
  {
    sqlResult vals = GAMEDATABASE.sqlQuery(query.toStdString());
    if(vals.valid && !vals.values.empty())
    {
      for(size_t i = 0; i < vals.values[0].size() ; i++)
        if(!vals.values[0][i].empty())
          result.push_back(vals.values[0][i]);
    }
    else
    {
      LOG_ERROR << "Unable to collect infos for model";
      LOG_ERROR << query;
    }
  }

  return result;
}

int CharDetails::getNbValuesForSection(SectionType section)
{
  int result = 0;

  RaceInfos infos;
  if(!RaceInfos::getCurrent(std::string(g_modelViewer->charControl->model->wxname.mb_str()), infos))
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

  sqlResult vals = GAMEDATABASE.sqlQuery(query.toStdString());

  if(vals.valid && !vals.values.empty())
  {
    result = atoi(vals.values[0][0].c_str());

  }
  else
  {
    LOG_ERROR << "Unable to collect number of customization for model" << g_modelViewer->charControl->model->wxname.c_str();
  }

  return result;
}
