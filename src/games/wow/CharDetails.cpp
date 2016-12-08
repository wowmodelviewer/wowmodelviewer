/*
 * CharDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharDetails.h"

#include <iostream>
#include "CharDetailsEvent.h"
#include "Game.h"
#include "TabardDetails.h"
#include "WoWModel.h"
#include "logger/Logger.h"

#include <QFile>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

CharDetails::CharDetails() :
eyeGlowType(EGT_NONE), showUnderwear(true), showEars(true), showHair(true),
showFacialHair(true), showFeet(true), autoHideGeosetsForHeadItems(true), isNPC(true), m_model(0),
m_skinColor(0), m_skinColorMax(0), m_faceType(0), m_hairColor(0),
m_hairStyle(0), m_hairStyleMax(0), m_facialHair(0), m_facialHairMax(0)
{

}

void CharDetails::save(QXmlStreamWriter & stream)
{
  stream.writeStartElement("CharDetails");

  stream.writeStartElement("skinColor");
  stream.writeAttribute("value", QString::number(m_skinColor));
  stream.writeEndElement();

  stream.writeStartElement("faceType");
  stream.writeAttribute("value", QString::number(m_faceType));
  stream.writeEndElement();

  stream.writeStartElement("hairColor");
  stream.writeAttribute("value", QString::number(m_hairColor));
  stream.writeEndElement();

  stream.writeStartElement("hairStyle");
  stream.writeAttribute("value", QString::number(m_hairStyle));
  stream.writeEndElement();

  stream.writeStartElement("facialHair");
  stream.writeAttribute("value", QString::number(m_facialHair));
  stream.writeEndElement();

  stream.writeStartElement("eyeGlowType");
  stream.writeAttribute("value", QString::number((int)eyeGlowType));
  stream.writeEndElement();

  stream.writeStartElement("showUnderwear");
  stream.writeAttribute("value", QString::number(showUnderwear));
  stream.writeEndElement();

  stream.writeStartElement("showEars");
  stream.writeAttribute("value", QString::number(showEars));
  stream.writeEndElement();

  stream.writeStartElement("showHair");
  stream.writeAttribute("value", QString::number(showHair));
  stream.writeEndElement();

  stream.writeStartElement("showFacialHair");
  stream.writeAttribute("value", QString::number(showFacialHair));
  stream.writeEndElement();

  stream.writeStartElement("showFeet");
  stream.writeAttribute("value", QString::number(showFeet));
  stream.writeEndElement();

  stream.writeEndElement(); // CharDetails
}

void CharDetails::load(QString & f)
{
  QFile file(f);
  if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << f;
    return;
  }

  QXmlStreamReader reader;
  reader.setDevice(&file);

  int nbValuesRead = 0;
  while (!reader.atEnd() && nbValuesRead != 11)
  {
    if (reader.isStartElement())
    {
      if(reader.name() == "skinColor")
      {
        unsigned int skinColor = reader.attributes().value("value").toString().toUInt();
        setSkinColor(skinColor);
        nbValuesRead++;
      }

      if(reader.name() == "faceType")
      {
        unsigned int faceType = reader.attributes().value("value").toString().toUInt();
        setFaceType(faceType);
        nbValuesRead++;
      }

      if(reader.name() == "hairColor")
      {
        unsigned int hairColor = reader.attributes().value("value").toString().toUInt();
        setHairColor(hairColor);
        nbValuesRead++;
      }

      if(reader.name() == "hairStyle")
      {
        unsigned int hairStyle = reader.attributes().value("value").toString().toUInt();
        setHairStyle(hairStyle);
        nbValuesRead++;
      }

      if(reader.name() == "facialHair")
      {
        unsigned int facialHair = reader.attributes().value("value").toString().toUInt();
        setFacialHair(facialHair);
        nbValuesRead++;
      }

      if(reader.name() == "eyeGlowType")
      {
        eyeGlowType = (EyeGlowTypes)reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if(reader.name() == "showUnderwear")
      {
        showUnderwear = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if(reader.name() == "showEars")
      {
        showEars = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if(reader.name() == "showHair")
      {
        showHair = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if(reader.name() == "showFacialHair")
      {
        showFacialHair = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if(reader.name() == "showFeet")
      {
        showFeet = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }
    }
    reader.readNext();
  }
}

void CharDetails::reset(WoWModel * model)
{
  m_model = model;

  m_skinColor = 0;
  m_faceType = 0;
  m_hairColor = 0;
  m_hairStyle = 0;
  m_facialHair = 0;

  m_currentCustomization.clear();
  m_currentCustomization.insert({ SKIN_COLOR, 0 });
  m_currentCustomization.insert({ FACE, 0 });
  m_currentCustomization.insert({ FACIAL_CUSTOMIZATION_STYLE, 0 });
  m_currentCustomization.insert({ FACIAL_CUSTOMIZATION_COLOR, 0 });
  m_currentCustomization.insert({ ADDITIONAL_FACIAL_CUSTOMIZATION, 0 });
  m_currentCustomization.insert({ DH_TATTOO_STYLE, 0 });
  m_currentCustomization.insert({ DH_TATTOO_COLOR, 0 });
  m_currentCustomization.insert({ DH_HORN_STYLE, 0 });
  m_currentCustomization.insert({ DH_BLINDFOLDS, 0 });

  showUnderwear = true;
  showHair = true;
  showFacialHair = true;
  showEars = true;
  showFeet = false;

  isNPC = false;

  //updateMaxValues();
  //updateValidValues();
  fillCustomizationMap();
}

void CharDetails::setSkinColor(size_t val)
{
  if(val != m_skinColor && val <= m_skinColorMax && val >= 0)
  {
    m_skinColor = val;
    updateMaxValues();
    updateValidValues();
 //   CharDetailsEvent event(this, CharDetailsEvent::SKINCOLOR_CHANGED);
   // notify(event);
  }
}

void CharDetails::setFaceType(size_t val)
{
  if(val != m_faceType && (find(m_validFaceTypes.begin(), m_validFaceTypes.end(), val) != m_validFaceTypes.end()) )
  {
    m_faceType = val;
 //   CharDetailsEvent event(this, CharDetailsEvent::FACETYPE_CHANGED);
  //  notify(event);
  }
}

void CharDetails::setHairColor(size_t val)
{
  if(val != m_hairColor && (find(m_validHairColors.begin(), m_validHairColors.end(), val) != m_validHairColors.end()) )
  {
    m_hairColor = val;
   // CharDetailsEvent event(this, CharDetailsEvent::HAIRCOLOR_CHANGED);
  //  notify(event);
  }
}

void CharDetails::setHairStyle(size_t val)
{
  if(val != m_hairStyle && val <= m_hairStyleMax && val >= 0)
  {
    m_hairStyle = val;
    updateMaxValues();
   // CharDetailsEvent event(this, CharDetailsEvent::HAIRSTYLE_CHANGED);
  //  notify(event);
  }
}

void CharDetails::setFacialHair(size_t val)
{
  if(val != m_facialHair && val <= m_facialHairMax && val >= 0)
  {
    m_facialHair = val;
    updateMaxValues();
  //  CharDetailsEvent event(this, CharDetailsEvent::FACIALHAIR_CHANGED);
 //   notify(event);
  }
}

void CharDetails::updateMaxValues()
{
  if(!m_model)
    return;

  m_skinColorMax = getNbValuesForSection(SkinType);

  RaceInfos infos;
  RaceInfos::getCurrent(m_model, infos);

  QString query = QString("SELECT MAX(VariationID) FROM CharHairGeosets WHERE RaceID=%1 AND SexID=%2")
                        .arg(infos.raceid)
                        .arg(infos.sexid);

  sqlResult hairStyles = GAMEDATABASE.sqlQuery(query);

  if(hairStyles.valid && !hairStyles.values.empty())
  {
    m_hairStyleMax = hairStyles.values[0][0].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of hair styles for model" << m_model->name();
    LOG_ERROR << query;
    m_hairStyleMax = 0;
  }


  query = QString("SELECT MAX(VariationID) FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2")
                            .arg(infos.raceid)
                            .arg(infos.sexid);

  sqlResult facialHairStyles = GAMEDATABASE.sqlQuery(query);
  if(facialHairStyles.valid && !facialHairStyles.values.empty())
  {
    m_facialHairMax = facialHairStyles.values[0][0].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair styles for model" << m_model->name();
    LOG_ERROR << query;
    m_facialHairMax = 0;
  }

  if (m_skinColorMax == 0) m_skinColorMax = 1;
  if (m_hairStyleMax == 0) m_hairStyleMax = 1;
  if (m_facialHairMax == 0) m_facialHairMax = 1;
}

std::vector<int> CharDetails::getTextureForSection(SectionType section)
{
  std::vector<int> result;

  RaceInfos infos;
  if(!RaceInfos::getCurrent(m_model, infos))
    return result;

/*
  LOG_INFO << __FUNCTION__;
  LOG_INFO << "----------------------------------------------";
  LOG_INFO << "infos.raceid = " << infos.raceid;
  LOG_INFO << "infos.sexid = " << infos.sexid;
  LOG_INFO << "infos.textureLayoutID = " << infos.textureLayoutID;
  LOG_INFO << "infos.isHD = " << infos.isHD;
  LOG_INFO << "cd.skinColor() = " << skinColor();
  LOG_INFO << "section = " << section;
  LOG_INFO << "----------------------------------------------";
*/

  size_t type = section;

  if(infos.isHD) // HD layout
    type+=5;

  QString query = QString("SELECT TFD1.TextureID, TFD2.TextureID, TFD3.TextureID FROM CharSections "
                          "LEFT JOIN TextureFileData AS TFD1 ON TextureName1 = TFD1.ID "
                          "LEFT JOIN TextureFileData AS TFD2 ON TextureName2 = TFD2.ID "
                          "LEFT JOIN TextureFileData AS TFD3 ON TextureName3 = TFD3.ID ");
  switch(section)
  {
    case SkinType:
    case UnderwearType:
      query += QString("WHERE (RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND SectionType=%4)")
                       .arg(infos.raceid)
                       .arg(infos.sexid)
                       .arg(m_currentCustomization[SKIN_COLOR])
                       .arg(type);
      break;
    case FaceType:
      query += QString("WHERE (RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND VariationIndex=%4 AND SectionType=%5)")
                       .arg(infos.raceid)
                       .arg(infos.sexid)
                       .arg(m_currentCustomization[SKIN_COLOR])
                       .arg(m_currentCustomization[FACE])
                       .arg(type);
      break;
    case HairType:
      query += QString("WHERE (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4 AND SectionType=%5)")
                       .arg(infos.raceid)
                       .arg(infos.sexid)
                       .arg((m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE] == 0) ? 1 : m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]) // quick fix for bald characters... VariationIndex = 0 returns no result
                       .arg(m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR])
                       .arg(type);
      break;
    case FacialHairType:
      query += QString("WHERE (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4 AND SectionType=%5)")
                       .arg(infos.raceid)
                       .arg(infos.sexid)
                       .arg(m_currentCustomization[ADDITIONAL_FACIAL_CUSTOMIZATION])
                       .arg(m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR])
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
          result.push_back(vals.values[0][i].toInt());
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
  if(!RaceInfos::getCurrent(m_model, infos))
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

void CharDetails::updateValidValues()
{
  if(!m_model)
    return;

  RaceInfos infos;
  RaceInfos::getCurrent(m_model, infos);

  // valid hair colours:
  m_validHairColors.clear();
  QString query = QString("SELECT DISTINCT ColorIndex FROM CharSections WHERE RaceID=%1 "
                          "AND SexID=%2 AND VariationIndex=%3 AND SectionType=%4")
                           .arg(infos.raceid)
                           .arg(infos.sexid)
                           .arg(hairStyle())
                           .arg(infos.isHD? HairTypeHD : HairType);

  sqlResult hairCols = GAMEDATABASE.sqlQuery(query);

  if(hairCols.valid && !hairCols.values.empty())
  {
    for (int i = 0, j = hairCols.values.size(); i < j; i++)
      m_validHairColors.push_back(hairCols.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to collect hair colours for model " << m_model->name();
  }


  // valid face types:
  m_validFaceTypes.clear();
  query = QString("SELECT DISTINCT VariationIndex FROM CharSections WHERE RaceID=%1 "
                  "AND SexID=%2 AND ColorIndex=%3 AND SectionType=%4")
                           .arg(infos.raceid)
                           .arg(infos.sexid)
                           .arg(skinColor())
                           .arg(infos.isHD? FaceTypeHD : FaceType);

  sqlResult faces = GAMEDATABASE.sqlQuery(query);

  if(faces.valid && !faces.values.empty())
  {
    for (int i = 0, j = faces.values.size(); i < j; i++)
      m_validFaceTypes.push_back(faces.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to collect face types for model " << m_model->name();
  }

}

void CharDetails::fillCustomizationMap()
{
  if (!m_model)
    return;

  RaceInfos infos;
  RaceInfos::getCurrent(m_model, infos);

  int sectionOffset = 0;
  if (infos.isHD)
    sectionOffset = 5;


  // clear any previous value found
  m_customizationParamsMap.clear();
  m_facialCustomizationMap.clear();

  // common part for all characters
  // skin
  CustomizationParam skin;
  skin.name = "Skin";
  
  QString query = QString("SELECT ColorIndex FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
                          .arg(infos.raceid)
                          .arg(infos.sexid)
                          .arg(SkinType);

  sqlResult vals = GAMEDATABASE.sqlQuery(query);

  if (vals.valid && !vals.values.empty())
  {
    for (uint i = 0; i < vals.values.size(); i++)
      skin.possibleValues.push_back(vals.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to collect skin parameters for model" << m_model->name();
  }

  m_customizationParamsMap.insert({ SKIN_COLOR, skin });

  // face customization
  // face possible customization depends on current skin color. We fill m_facialCustomizationMap first
  for (auto it = skin.possibleValues.begin(), itEnd = skin.possibleValues.end(); it != itEnd; ++it)
  {
    query = QString("SELECT DISTINCT VariationIndex FROM CharSections WHERE RaceID=%1 "
                    "AND SexID=%2 AND ColorIndex=%3 AND SectionType=%4")
                    .arg(infos.raceid)
                    .arg(infos.sexid)
                    .arg(*it)
                    .arg(FaceType + sectionOffset);

    sqlResult faces = GAMEDATABASE.sqlQuery(query);

    CustomizationParam face;
    face.name = "Face";

    if (faces.valid && !faces.values.empty())
    {
      for (uint i = 0; i < faces.values.size(); i++)
        face.possibleValues.push_back(faces.values[i][0].toInt());
    }
    else
    {
      LOG_ERROR << "No face customization available for skin color" << *it << "for model" << m_model->name();
    }

    m_facialCustomizationMap.insert({ *it, face });
  }

  m_customizationParamsMap.insert({ FACE, m_facialCustomizationMap[m_skinColor] });

  // starting from here, customization may differ based on database values
  // get customization names
  query = QString("SELECT HairCustomization, FacialHairCustomization%1 FROM ChrRaces WHERE ID = %2")
                  .arg(infos.sexid + 1)
                  .arg(infos.raceid);
  
  sqlResult names = GAMEDATABASE.sqlQuery(query);

  QString facialCustomizationBaseName;
  QString additionalCustomizationName;

  if (names.valid && !names.values.empty())
  {
    facialCustomizationBaseName = names.values[0][0];
    facialCustomizationBaseName = facialCustomizationBaseName.at(0).toUpper() + facialCustomizationBaseName.mid(1).toLower();
    if (facialCustomizationBaseName == "Normal") 
      facialCustomizationBaseName = "Hair";

    additionalCustomizationName = names.values[0][1];
    additionalCustomizationName = additionalCustomizationName.at(0).toUpper() + additionalCustomizationName.mid(1).toLower();
    if (additionalCustomizationName == "Normal") 
      additionalCustomizationName = "Facial Hair";
  }
  else
  {
    LOG_ERROR << "Unable to collect customization names for model" << m_model->name() << "(race id" << infos.raceid << "- sex id" << infos.sexid << ")";
  }

  // facial style customization
  query = QString("SELECT DISTINCT VariationIndex FROM CharSections WHERE RaceID = %1 AND SexID = %2 AND SectionType = %3")
                  .arg(infos.raceid)
                  .arg(infos.sexid)
                  .arg(HairType + sectionOffset);

  sqlResult styles = GAMEDATABASE.sqlQuery(query);

  CustomizationParam facialCustomizationStyle;
  facialCustomizationStyle.name = QString(facialCustomizationBaseName + " Style").toStdString();

  if (styles.valid && !styles.values.empty())
  {
    for (uint i = 0; i < styles.values.size(); i++)
      facialCustomizationStyle.possibleValues.push_back(styles.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to facial style parameters for model" << m_model->name();
  }

  m_customizationParamsMap.insert({ FACIAL_CUSTOMIZATION_STYLE, facialCustomizationStyle });


  // facial color customization
  query = QString("SELECT DISTINCT ColorIndex FROM CharSections WHERE RaceID = %1 AND SexID = %2 AND SectionType = %3")
                  .arg(infos.raceid)
                  .arg(infos.sexid)
                  .arg(HairType + sectionOffset);

  sqlResult colors = GAMEDATABASE.sqlQuery(query);

  CustomizationParam facialCustomizationColor;
  facialCustomizationColor.name = QString(facialCustomizationBaseName + " Color").toStdString();

  if (colors.valid && !colors.values.empty())
  {
    for (uint i = 0; i < colors.values.size(); i++)
      facialCustomizationColor.possibleValues.push_back(colors.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to collect facial color parameters for model" << m_model->name();
  }

  m_customizationParamsMap.insert({ FACIAL_CUSTOMIZATION_COLOR, facialCustomizationColor });

  // addtional facial customization
  query = QString("SELECT DISTINCT VariationID FROM CharacterFacialHairStyles WHERE RaceID = %1 AND SexID = %2")
                  .arg(infos.raceid)
                  .arg(infos.sexid);

  sqlResult additional = GAMEDATABASE.sqlQuery(query);

  CustomizationParam additionalCustomization;
  additionalCustomization.name = additionalCustomizationName.toStdString();

  if (additional.valid && !additional.values.empty())
  {
    for (uint i = 0; i < additional.values.size(); i++)
      additionalCustomization.possibleValues.push_back(additional.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to collect additional facial customization parameters for model" << m_model->name();
  }

  m_customizationParamsMap.insert({ ADDITIONAL_FACIAL_CUSTOMIZATION, additionalCustomization });
}

CharDetails::CustomizationParam CharDetails::getParams(CustomizationType type)
{
  auto it = m_customizationParamsMap.find(type);

  if (it != m_customizationParamsMap.end())
    return it->second;

  CustomizationParam dummy;
  return dummy;
}

void CharDetails::set(CustomizationType type, unsigned int val)
{
  if (val != m_currentCustomization.at(type))
  {
    m_currentCustomization[type] = val;
    CharDetailsEvent event(this, CharDetailsEvent::SKIN_COLOR_CHANGED);
    switch (type)
    {
      case SKIN_COLOR:
        event.setType((Event::EventType)CharDetailsEvent::SKIN_COLOR_CHANGED);
        break;
      case FACE:
        event.setType((Event::EventType)CharDetailsEvent::FACE_CHANGED);
        break;
      case FACIAL_CUSTOMIZATION_STYLE:
        event.setType((Event::EventType)CharDetailsEvent::FACIAL_CUSTOMIZATION_STYLE_CHANGED);
        break;
      case FACIAL_CUSTOMIZATION_COLOR:
        event.setType((Event::EventType)CharDetailsEvent::FACIAL_CUSTOMIZATION_STYLE_CHANGED);
        break;
      case ADDITIONAL_FACIAL_CUSTOMIZATION:
        event.setType((Event::EventType)CharDetailsEvent::ADDITIONAL_FACIAL_CUSTOMIZATION_CHANGED);
        break;
      case DH_TATTOO_STYLE:
        event.setType((Event::EventType)CharDetailsEvent::DH_TATTOO_STYLE_CHANGED);
        break;
      case DH_TATTOO_COLOR:
        event.setType((Event::EventType)CharDetailsEvent::DH_TATTOO_COLOR_CHANGED);
        break;
      case DH_HORN_STYLE:
        event.setType((Event::EventType)CharDetailsEvent::DH_HORN_STYLE_CHANGED);
        break;
      case DH_BLINDFOLDS:
        event.setType((Event::EventType)CharDetailsEvent::DH_BLINDFOLDS_CHANGED);
        break;
    }

    notify(event);
  }
}

uint CharDetails::get(CustomizationType type) const
{
  return m_currentCustomization.at(type);
}

