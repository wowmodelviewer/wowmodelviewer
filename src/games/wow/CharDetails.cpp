/*
* CharDetails.cpp
*
*  Created on: 26 oct. 2013
*
*/

#include "CharDetails.h"

#include <iostream>

#include "animated.h" // randint
#include "CharDetailsEvent.h"
#include "Game.h"
#include "WoWModel.h"
#include "logger/Logger.h"

#include <QFile>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

CharDetails::CharDetails():
eyeGlowType(EGT_NONE), showUnderwear(true), showEars(true), showHair(true),
showFacialHair(true), showFeet(true), autoHideGeosetsForHeadItems(true), isNPC(true), m_model(0)
{

}

void CharDetails::save(QXmlStreamWriter & stream)
{
  stream.writeStartElement("CharDetails");

  stream.writeStartElement("skinColor");
  stream.writeAttribute("value", QString::number(m_currentCustomization[SKIN_COLOR]));
  stream.writeEndElement();

  stream.writeStartElement("faceType");
  stream.writeAttribute("value", QString::number(m_currentCustomization[FACE]));
  stream.writeEndElement();

  stream.writeStartElement("hairColor");
  stream.writeAttribute("value", QString::number(m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR]));
  stream.writeEndElement();

  stream.writeStartElement("hairStyle");
  stream.writeAttribute("value", QString::number(m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]));
  stream.writeEndElement();

  stream.writeStartElement("facialHair");
  stream.writeAttribute("value", QString::number(m_currentCustomization[ADDITIONAL_FACIAL_CUSTOMIZATION]));
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
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
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
      if (reader.name() == "skinColor")
      {
        set(SKIN_COLOR, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "faceType")
      {
        set(FACE, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "hairColor")
      {
        set(FACIAL_CUSTOMIZATION_COLOR, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "hairStyle")
      {
        set(FACIAL_CUSTOMIZATION_STYLE, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "facialHair")
      {
        set(ADDITIONAL_FACIAL_CUSTOMIZATION, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "eyeGlowType")
      {
        eyeGlowType = (EyeGlowTypes)reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if (reader.name() == "showUnderwear")
      {
        showUnderwear = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if (reader.name() == "showEars")
      {
        showEars = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if (reader.name() == "showHair")
      {
        showHair = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if (reader.name() == "showFacialHair")
      {
        showFacialHair = reader.attributes().value("value").toString().toUInt();
        nbValuesRead++;
      }

      if (reader.name() == "showFeet")
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

  fillCustomizationMap();
}

std::vector<int> CharDetails::getTextureForSection(SectionType section)
{
  std::vector<int> result;

  RaceInfos infos;
  if (!RaceInfos::getCurrent(m_model, infos))
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

  if (infos.isHD) // HD layout
    type += 5;

  QString query = QString("SELECT TFD1.TextureID, TFD2.TextureID, TFD3.TextureID FROM CharSections "
                          "LEFT JOIN TextureFileData AS TFD1 ON TextureName1 = TFD1.ID "
                          "LEFT JOIN TextureFileData AS TFD2 ON TextureName2 = TFD2.ID "
                          "LEFT JOIN TextureFileData AS TFD3 ON TextureName3 = TFD3.ID ");
  switch (section)
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

  if (query != "")
  {
    sqlResult vals = GAMEDATABASE.sqlQuery(query);
    if (vals.valid && !vals.values.empty())
    {
      for (size_t i = 0; i < vals.values[0].size(); i++)
        if (!vals.values[0][i].isEmpty())
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
  m_multiCustomizationMap.clear();

  // common part for all characters
  // skin
  CustomizationParam skin;
  skin.name = "Skin";

  QString query = QString("SELECT ColorIndex FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
    .arg(infos.raceid)
    .arg(infos.sexid)
    .arg(SkinType + sectionOffset);

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
  // face possible customization depends on current skin color. We fill m_multiCustomizationMap first
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

    m_multiCustomizationMap[FACE].insert({ *it, face });
  }

  m_customizationParamsMap.insert({ FACE, m_multiCustomizationMap[FACE][m_currentCustomization[SKIN_COLOR]] });

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


  // facial color customization depends on current facial style. We fill m_multiCustomizationMap first
  for (auto it = facialCustomizationStyle.possibleValues.begin(), itEnd = facialCustomizationStyle.possibleValues.end(); it != itEnd; ++it)
  {
    query = QString("SELECT DISTINCT ColorIndex FROM CharSections WHERE RaceID = %1 AND SexID = %2 "
                    "AND SectionType = %3 AND VariationIndex = %4")
                    .arg(infos.raceid)
                    .arg(infos.sexid)
                    .arg(HairType + sectionOffset)
                    .arg(*it);

    sqlResult colors = GAMEDATABASE.sqlQuery(query);

    CustomizationParam facialColor;
    facialColor.name = QString(facialCustomizationBaseName + " Color").toStdString();

    if (colors.valid && !colors.values.empty())
    {
      for (uint i = 0; i < colors.values.size(); i++)
        facialColor.possibleValues.push_back(colors.values[i][0].toInt());
    }
    else
    {
      LOG_ERROR << "No facial color available for facial customization style " << *it << "for model" << m_model->name();
    }

    m_multiCustomizationMap[FACIAL_CUSTOMIZATION_COLOR].insert({ *it, facialColor });
  }

  m_customizationParamsMap.insert({ FACIAL_CUSTOMIZATION_COLOR, m_multiCustomizationMap[FACIAL_CUSTOMIZATION_COLOR][m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]] });

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

  // Tatoos
  // skin
  CustomizationParam tatoos;
  tatoos.name = "Tatoo";

  query = QString("SELECT ColorIndex FROM CharSections WHERE RaceID=%1 AND SexID=%2 AND SectionType=%3")
    .arg(infos.raceid)
    .arg(infos.sexid)
    .arg(TatooType);

  vals = GAMEDATABASE.sqlQuery(query);

  if (vals.valid && (vals.values.size() > 1))
  {
    // harcoded for now (dh tatoos are 36 sequential values in CharSections table...)
    // tatoo style = 0 to 6 (0 = no tatoo)

    tatoos.possibleValues = { 0, 1, 2, 3, 4, 5, 6 };
    m_customizationParamsMap.insert({ DH_TATTOO_STYLE, tatoos });

    for (auto it = tatoos.possibleValues.begin(), itEnd = tatoos.possibleValues.end(); it != itEnd; ++it)
    {
      // tatoo color = 0 to 5 for each tatoo style
      CustomizationParam tatooColor;
      tatooColor.name = "Tatoo Color";
      tatooColor.possibleValues = { 0, 1, 2, 3, 4, 5 };

      m_multiCustomizationMap[DH_TATTOO_COLOR].insert({ *it, tatooColor });
    }

    m_customizationParamsMap.insert({ DH_TATTOO_COLOR, m_multiCustomizationMap[DH_TATTOO_COLOR][m_currentCustomization[DH_TATTOO_STYLE]] });
  }

  // horns => check geoset group #24
  std::unordered_set<int> hornsGeoset;
  for (auto it : m_model->geosets)
  {
    if ((it->id / 100) == 24)
      hornsGeoset.insert(it->id);
  }

  if (hornsGeoset.size() > 0)
  {
    CustomizationParam horns;
    horns.name = "Horns";

    for (uint i = 0; i <= hornsGeoset.size(); i++)
      horns.possibleValues.push_back(i);

    m_customizationParamsMap.insert({ DH_HORN_STYLE, horns });
  }

  // blindfolds => check geoset group #25
  std::unordered_set<int> blindfoldGeoset;
  for (auto it : m_model->geosets)
  {
    if ((it->id / 100) == 25)
      blindfoldGeoset.insert(it->id);
  }

  if (blindfoldGeoset.size() > 0)
  {
    CustomizationParam blindfold;
    blindfold.name = "Blindfolds";

    for (uint i = 0; i <= blindfoldGeoset.size(); i++)
      blindfold.possibleValues.push_back(i);

    m_customizationParamsMap.insert({ DH_BLINDFOLDS, blindfold });
  }
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
      {
        m_customizationParamsMap[FACE] = m_multiCustomizationMap[FACE][m_currentCustomization[SKIN_COLOR]];

        // reset current face if not available for this skin color
        std::vector<int> & vec = m_customizationParamsMap[FACE].possibleValues;
        if (std::find(vec.begin(), vec.end(), m_currentCustomization[FACE]) == vec.end())
          m_currentCustomization[FACE] = vec[0];

        event.setType((Event::EventType)CharDetailsEvent::SKIN_COLOR_CHANGED);
        break;
      }
      case FACE:
        event.setType((Event::EventType)CharDetailsEvent::FACE_CHANGED);
        break;
      case FACIAL_CUSTOMIZATION_STYLE:
      {
        m_customizationParamsMap[FACIAL_CUSTOMIZATION_COLOR] = m_multiCustomizationMap[FACIAL_CUSTOMIZATION_COLOR][m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]];

        // reset current hair color if not available for this hair style
        std::vector<int> & vec = m_customizationParamsMap[FACIAL_CUSTOMIZATION_COLOR].possibleValues;
        if (std::find(vec.begin(), vec.end(), m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR]) == vec.end())
          m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR] = vec[0];

        event.setType((Event::EventType)CharDetailsEvent::FACIAL_CUSTOMIZATION_STYLE_CHANGED);
        break;
      }
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

void CharDetails::setRandomValue(CustomizationType type)
{
  uint maxVal = m_customizationParamsMap[type].possibleValues.size() - 1;
  set(type, randint(0, maxVal));
}

std::vector<CharDetails::CustomizationType> CharDetails::getCustomizationOptions() const
{
  std::vector<CustomizationType> result;

  RaceInfos infos;
  if (!RaceInfos::getCurrent(m_model, infos))
    return result;


  LOG_INFO << __FUNCTION__ << infos.raceid << infos.sexid;

  result.push_back(SKIN_COLOR);
  result.push_back(FACE);
  result.push_back(FACIAL_CUSTOMIZATION_STYLE);
  // pandaren male hair color can't be defined
  if (!((infos.raceid == 24) && (infos.sexid == 0)))
    result.push_back(FACIAL_CUSTOMIZATION_COLOR);
  result.push_back(ADDITIONAL_FACIAL_CUSTOMIZATION);

  auto it = m_customizationParamsMap.find(DH_TATTOO_STYLE);
  if (it != m_customizationParamsMap.end())
    result.push_back(DH_TATTOO_STYLE);

  it = m_customizationParamsMap.find(DH_TATTOO_COLOR);
  if (it != m_customizationParamsMap.end())
    result.push_back(DH_TATTOO_COLOR);

  it = m_customizationParamsMap.find(DH_HORN_STYLE);
  if (it != m_customizationParamsMap.end())
    result.push_back(DH_HORN_STYLE);

  it = m_customizationParamsMap.find(DH_BLINDFOLDS);
  if (it != m_customizationParamsMap.end())
    result.push_back(DH_BLINDFOLDS);

  return result;
}
