/*
* CharDetails.cpp
*
*  Created on: 26 oct. 2013
*
*/

#include "CharDetails.h"

#include "animated.h" // randint
#include "CharDetailsEvent.h"
#include "Game.h"
#include "WoWModel.h"
#include "logger/Logger.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamReader>

CharDetails::CharDetails():
eyeGlowType(EGT_NONE), showUnderwear(true), showEars(true), showHair(true),
showFacialHair(true), showFeet(true), autoHideGeosetsForHeadItems(true), 
isNPC(true), m_model(nullptr), m_isDemonHunter(false), m_dhModel("")
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

  stream.writeStartElement("isDemonHunter");
  stream.writeAttribute("value", QString::number(m_isDemonHunter));
  stream.writeEndElement();

  stream.writeStartElement("DHTattooStyle");
  stream.writeAttribute("value", QString::number(m_currentCustomization[DH_TATTOO_STYLE]));
  stream.writeEndElement();

  stream.writeStartElement("DHTattooColor");
  stream.writeAttribute("value", QString::number(m_currentCustomization[DH_TATTOO_COLOR]));
  stream.writeEndElement();

  stream.writeStartElement("DHHornStyle");
  stream.writeAttribute("value", QString::number(m_currentCustomization[DH_HORN_STYLE]));
  stream.writeEndElement();

  stream.writeStartElement("DHBlindFolds");
  stream.writeAttribute("value", QString::number(m_currentCustomization[DH_BLINDFOLDS]));
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
  while (!reader.atEnd() && nbValuesRead != 16)
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

      if (reader.name() == "isDemonHunter")
      {
        LOG_INFO << __FILE__ << __LINE__ << "reading demonHunter mode value";
        setDemonHunterMode(reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "DHTattooStyle")
      {
        set(DH_TATTOO_STYLE, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "DHTattooColor")
      {
        set(DH_TATTOO_COLOR, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "DHHornStyle")
      {
        set(DH_HORN_STYLE, reader.attributes().value("value").toString().toUInt());
        nbValuesRead++;
      }

      if (reader.name() == "DHBlindFolds")
      {
        set(DH_BLINDFOLDS, reader.attributes().value("value").toString().toUInt());
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

  m_isDemonHunter = false;

  fillCustomizationMap();
}

std::vector<int> CharDetails::getTextureForSection(BaseSectionType baseSection)
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
  LOG_INFO << "baseSection = " << baseSection;
  LOG_INFO << "----------------------------------------------";
  */

  QString query = QString("SELECT TFD1.TextureID, TFD2.TextureID, TFD3.TextureID FROM CharSections "
                          "LEFT JOIN TextureFileData AS TFD1 ON TextureName1 = TFD1.ID "
                          "LEFT JOIN TextureFileData AS TFD2 ON TextureName2 = TFD2.ID "
                          "LEFT JOIN TextureFileData AS TFD3 ON TextureName3 = TFD3.ID "
                          "LEFT JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                          "WHERE CharBaseSection.VariationEnum = %1 AND CharBaseSection.LayoutResType = %2 ")
                          .arg(baseSection)
                          .arg(infos.isHD ? 1 : 0);
  switch (baseSection)
  {
    case SkinBaseType:
    case UnderwearBaseType:
      query += QString(" AND (RaceID=%1 AND SexID=%2 AND ColorIndex=%3)")
        .arg(infos.raceid)
        .arg(infos.sexid)
        .arg(m_currentCustomization[SKIN_COLOR]);
      break;
    case FaceBaseType:
      query += QString(" AND (RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND VariationIndex=%4)")
        .arg(infos.raceid)
        .arg(infos.sexid)
        .arg(m_currentCustomization[SKIN_COLOR])
        .arg(m_currentCustomization[FACE]);
      break;
    case HairBaseType:
      query += QString(" AND (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4)")
        .arg(infos.raceid)
        .arg(infos.sexid)
        .arg((m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE] == 0) ? 1 : m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]) // quick fix for bald characters... VariationIndex = 0 returns no result
        .arg(m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR]);
      break;
    case FacialHairBaseType:
      query += QString(" AND (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex=%4)")
        .arg(infos.raceid)
        .arg(infos.sexid)
        .arg(m_currentCustomization[ADDITIONAL_FACIAL_CUSTOMIZATION])
        .arg(m_currentCustomization[FACIAL_CUSTOMIZATION_COLOR]);
      break;
    case Custom1BaseType:
      if (infos.raceid == RACE_NIGHTELF || infos.raceid == RACE_BLOODELF) // for Night Elves and Blood Elves - Demon Hunter Tattoos are handled strangely:
      {
        query += QString(" AND (RaceID=%1 AND SexID=%2 AND VariationIndex=%3)")
          .arg(infos.raceid)
          .arg(infos.sexid)
          .arg((m_customizationParamsMap[DH_TATTOO_STYLE].possibleValues.size() - 1) * m_currentCustomization[DH_TATTOO_COLOR] + m_currentCustomization[DH_TATTOO_STYLE]);
      }
      else
      {
        query += QString(" AND (RaceID=%1 AND SexID=%2 AND VariationIndex=%3 AND ColorIndex = %4)")
          .arg(infos.raceid)
          .arg(infos.sexid)
          .arg(m_currentCustomization[DH_TATTOO_STYLE])
          .arg(m_currentCustomization[DH_TATTOO_COLOR]);
      }
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

  // clear any previous value found
  m_customizationParamsMap.clear();
  m_multiCustomizationMap.clear();

  // common part for all characters
  // skin
  CustomizationParam skin;
  skin.name = getCustomizationName(SkinBaseType, infos.raceid, infos.sexid);

  QString query = QString("SELECT ColorIndex FROM CharSections "
                          "LEFT JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                          "WHERE RaceID=%1 AND SexID=%2 AND CharBaseSection.VariationEnum = %3 AND CharBaseSection.LayoutResType = %4" )
                         .arg(infos.raceid)
                         .arg(infos.sexid)
                         .arg(SkinBaseType)
                         .arg(infos.isHD ? 1 : 0);

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
  QString faceName = getCustomizationName(FaceBaseType, infos.raceid, infos.sexid);
  for (auto it = skin.possibleValues.begin(), itEnd = skin.possibleValues.end(); it != itEnd; ++it)
  {
    query = QString("SELECT DISTINCT VariationIndex FROM CharSections "
                    "LEFT JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum " 
                    "WHERE RaceID=%1 AND SexID=%2 AND ColorIndex=%3 AND CharBaseSection.VariationEnum = %4 AND CharBaseSection.LayoutResType = %5")
                    .arg(infos.raceid)
                    .arg(infos.sexid)
                    .arg(*it)
                    .arg(FaceBaseType)
                    .arg(infos.isHD ? 1 : 0);

    sqlResult faces = GAMEDATABASE.sqlQuery(query);

    CustomizationParam face;
    face.name = faceName;

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

  // Hair style customization (horn style for some races)
  
  query = QString("SELECT DISTINCT VariationIndex FROM CharSections "
                  "LEFT JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                  "WHERE RaceID = %1 AND SexID = %2 AND CharBaseSection.VariationEnum = %3 AND CharBaseSection.LayoutResType = %4")
                  .arg(infos.raceid)
                  .arg(infos.sexid)
                  .arg(HairBaseType)
                  .arg(infos.isHD ? 1 : 0);

  sqlResult styles = GAMEDATABASE.sqlQuery(query);

  QString hairName = getCustomizationName(HairBaseType, infos.raceid, infos.sexid);
  QString hairColName = getCustomizationName(HairBaseType, infos.raceid, infos.sexid, true);
  
  CustomizationParam hairCustomizationStyle;
  hairCustomizationStyle.name = hairName;

  if (styles.valid && !styles.values.empty())
  {
    for (uint i = 0; i < styles.values.size(); i++)
      hairCustomizationStyle.possibleValues.push_back(styles.values[i][0].toInt());
  }
  else
  {
    LOG_ERROR << "Unable to facial style parameters for model" << m_model->name();
  }

  m_customizationParamsMap.insert({ FACIAL_CUSTOMIZATION_STYLE, hairCustomizationStyle });

  for (auto it = hairCustomizationStyle.possibleValues.begin(), itEnd = hairCustomizationStyle.possibleValues.end(); it != itEnd; ++it)
  {
    query = QString("SELECT DISTINCT ColorIndex FROM CharSections "
                    "LEFT JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                    "WHERE RaceID = %1 AND SexID = %2 AND VariationIndex = %3 AND CharBaseSection.VariationEnum = %4 AND CharBaseSection.LayoutResType = %5")
                    .arg(infos.raceid)
                    .arg(infos.sexid)
                    .arg(*it)
                    .arg(HairBaseType)
                    .arg(infos.isHD ? 1 : 0);

    sqlResult colors = GAMEDATABASE.sqlQuery(query);

    CustomizationParam hairColor;
    hairColor.name = hairColName;

    if (colors.valid && !colors.values.empty())
    {
      for (uint i = 0; i < colors.values.size(); i++)
        hairColor.possibleValues.push_back(colors.values[i][0].toInt());
    }
    else
    {
      LOG_ERROR << "No hair color available for hair customization style " << *it << "for model" << m_model->name();
    }

    m_multiCustomizationMap[FACIAL_CUSTOMIZATION_COLOR].insert({ *it, hairColor });
  }

  m_customizationParamsMap.insert({ FACIAL_CUSTOMIZATION_COLOR, m_multiCustomizationMap[FACIAL_CUSTOMIZATION_COLOR][m_currentCustomization[FACIAL_CUSTOMIZATION_STYLE]] });


  // Additional facial customization - facial hair, earrings, horns, tusks, depending on model:
  
  query = QString("SELECT DISTINCT VariationID FROM CharacterFacialHairStyles WHERE RaceID = %1 AND SexID = %2")
    .arg(infos.raceid)
    .arg(infos.sexid);

  sqlResult additional = GAMEDATABASE.sqlQuery(query);

  CustomizationParam additionalCustomization;
  additionalCustomization.name = getCustomizationName(FacialHairBaseType, infos.raceid, infos.sexid);

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


  // Custom1 Section (tattoos and some other things, depending on race)
  
  CustomizationParam custom1;
  custom1.name = getCustomizationName(Custom1BaseType, infos.raceid, infos.sexid);
  QString custom1ColName = getCustomizationName(Custom1BaseType, infos.raceid, infos.sexid, true);
        
  // Night Elf & Blood Elf (Demon Hunters) only:
  // For some reason tattoo colour indices aren't used in CharDetails.db2.
  // Instead the variation index goes from 0-36, even though there are actually 6
  // colours of 6 tattoo types (plus 0 = no tattoo). So we have to hardcode this instead:
  if (infos.raceid == RACE_NIGHTELF || infos.raceid == RACE_BLOODELF)
  {
    query = QString("SELECT ColorIndex FROM CharSections "
                    "INNER JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                    "WHERE RaceID=%1 AND SexID=%2 "
                    "AND CharBaseSection.VariationEnum=%3 AND CharBaseSection.LayoutResType=%4")
      .arg(infos.raceid)
      .arg(infos.sexid)
      .arg(Custom1BaseType)
      .arg(infos.isHD ? 1 : 0);
 
    vals = GAMEDATABASE.sqlQuery(query);

    if (vals.valid && (vals.values.size() > 1))
    {
      custom1.possibleValues = { 0, 1, 2, 3, 4, 5, 6 };
      m_customizationParamsMap.insert({ DH_TATTOO_STYLE, custom1 });
 
      for (auto it = custom1.possibleValues.begin(), itEnd = custom1.possibleValues.end(); it != itEnd; ++it)
      {
        // tattoo color = 0 to 5 for each tattoo style
        CustomizationParam custom1Color;
        custom1Color.name = custom1ColName;
        custom1Color.possibleValues = { 0, 1, 2, 3, 4, 5 };
 
        m_multiCustomizationMap[DH_TATTOO_COLOR].insert({ *it, custom1Color });
      }
      m_customizationParamsMap.insert({ DH_TATTOO_COLOR, m_multiCustomizationMap[DH_TATTOO_COLOR][m_currentCustomization[DH_TATTOO_STYLE]] });
    }
  }
  // Other races with tattoos don't seem to have tattoo colours at all (yet?), but we'll
  // add the check in case it's used in the future. Also note that not all races use the
  // Custom1 section for tattoos - a fix to support other customization types needs to be
  // added next.
  else
  {
    query = QString("SELECT DISTINCT VariationIndex FROM CharSections "
                    "INNER JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                    "WHERE RaceID = %1 AND SexID = %2 "
                    "AND CharBaseSection.VariationEnum=%3 AND CharBaseSection.LayoutResType=%4")
      .arg(infos.raceid)
      .arg(infos.sexid)
      .arg(Custom1BaseType)
      .arg(infos.isHD ? 1 : 0);
  
    sqlResult styles = GAMEDATABASE.sqlQuery(query);
  
    if (styles.valid && !styles.values.empty())
    {
      for (uint i = 0; i < styles.values.size(); i++)
        custom1.possibleValues.push_back(styles.values[i][0].toInt());
    }
    else
    {
      LOG_ERROR << "Unable to get custom1 style parameters for model" << m_model->name();
    }
  
    m_customizationParamsMap.insert({ DH_TATTOO_STYLE, custom1 });
  
  
    // Tattoo color customization depends on current tattoo style. We fill m_multiCustomizationMap first
    for (auto it = custom1.possibleValues.begin(), itEnd = custom1.possibleValues.end(); it != itEnd; ++it)
    {
      query = QString("SELECT DISTINCT ColorIndex FROM CharSections "
                      "INNER JOIN CharBaseSection ON CharSections.SectionType = CharBaseSection.ResolutionVariationEnum "
                      "WHERE RaceID = %1 AND SexID = %2 AND VariationIndex = %3 "
                      "AND CharBaseSection.VariationEnum=%4 AND CharBaseSection.LayoutResType=%5")
                      .arg(infos.raceid)
                      .arg(infos.sexid)
                      .arg(*it)
                      .arg(Custom1BaseType)
                      .arg(infos.isHD ? 1 : 0);
  
      sqlResult colors = GAMEDATABASE.sqlQuery(query);
  
      CustomizationParam custom1Color;

      custom1Color.name = getCustomizationName(Custom1BaseType, infos.raceid, infos.sexid, true);
  
      if (colors.valid && (colors.values.size() > 1))
      {
        for (uint i = 0; i < colors.values.size(); i++)
          custom1Color.possibleValues.push_back(colors.values[i][0].toInt());
      }
      else
      {
        LOG_ERROR << "No tattoo color available for tattoo customization style " << *it << "for model" << m_model->name();
      }
      m_multiCustomizationMap[DH_TATTOO_COLOR].insert({ *it, custom1Color });
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

  if (m_customizationParamsMap.find(DH_TATTOO_STYLE) != m_customizationParamsMap.end() &&
      m_customizationParamsMap.at(DH_TATTOO_STYLE).possibleValues.size() > 1)
    result.push_back(DH_TATTOO_STYLE);

  if (m_customizationParamsMap.find(DH_TATTOO_COLOR) != m_customizationParamsMap.end() &&
      m_customizationParamsMap.at(DH_TATTOO_COLOR).possibleValues.size() > 1)
    result.push_back(DH_TATTOO_COLOR);

  if (m_customizationParamsMap.find(DH_HORN_STYLE) != m_customizationParamsMap.end() &&
      m_customizationParamsMap.at(DH_HORN_STYLE).possibleValues.size() > 1)
    result.push_back(DH_HORN_STYLE);

  if (m_customizationParamsMap.find(DH_BLINDFOLDS) != m_customizationParamsMap.end() &&
      m_customizationParamsMap.at(DH_BLINDFOLDS).possibleValues.size() > 1)
    result.push_back(DH_BLINDFOLDS);

  return result;
}

QString CharDetails::getCustomizationName(BaseSectionType section, uint raceID, uint sexID, bool secondCustomization)
{
  // Names for most customization types are stored in ChrCustomization.db2, by race, sex and section.
  // Some are listed for "either sex" (sex = 3) or "any race" (race = 0), so we need to sort the query
  // so entries for specific race/sex combos have priority over generic ones. UiCustomizationType is
  // only relevant when there are two entries for the same section: usually the lower value is the
  // style and the higher one is for colour variation.
  QString query = QString("SELECT Name, RaceId, Sex, UiCustomizationType FROM ChrCustomization "
                  "WHERE BaseSection = %1 "
                  "AND (RaceId = %2 OR RaceId = 0) "
                  "AND (Sex = %3 OR Sex = 3)"
                  "ORDER BY RaceId DESC, Sex, UiCustomizationType")
                  .arg(section)
                  .arg(raceID)
                  .arg(sexID);
  
  sqlResult styles = GAMEDATABASE.sqlQuery(query);

  if (styles.valid && !styles.values.empty())
  {
    if (secondCustomization)
    {
      // If the race and sex values in the second entry are the same as the first, return the name from that:
      if (styles.values.size() > 1 && styles.values[0][1] == styles.values[1][1] && styles.values[0][2] == styles.values[1][2])
        return QString(styles.values[1][0]);
      // Otherwise it's likely we constructed the secondary customization category ourselves, e.g. WMV allows config. of
      // Vulpera marking colour even though the game client doesn't. In these few cases we need to make up a category name by
      // guesswork (or hardcoding them):
      QString custName1 = QString(styles.values[0][0]);
      if (custName1.endsWith(" Style", Qt::CaseInsensitive))
        return custName1.replace(" Style", " Color");
      return custName1.append(" Color");
    }
    return QString(styles.values[0][0]);
  }
  else
    LOG_ERROR << "Unable to get customization name for race:" << raceID << "sex:" << sexID << "section:" << section;
  return QString("");
}
  
void CharDetails::setDemonHunterMode(bool val)
{
  if (val != m_isDemonHunter)
  {
    m_isDemonHunter = val;
    if (m_isDemonHunter)
    {
      if (m_model->name().contains("bloodelfmale_hd"))
        m_dhModel = "item\\objectcomponents\\collections\\demonhuntergeosets_be_m.m2";
      else if (m_model->name().contains("bloodelffemale_hd"))
        m_dhModel = "item\\objectcomponents\\collections\\demonhuntergeosets_be_f.m2";
      else if (m_model->name().contains("nightelfmale_hd"))
        m_dhModel = "item\\objectcomponents\\collections\\demonhuntergeosets_ni_m.m2";
      else if (m_model->name().contains("nightelffemale_hd"))
        m_dhModel = "item\\objectcomponents\\collections\\demonhuntergeosets_ni_f.m2";

      m_model->mergeModel(m_dhModel);
    }
    else
    {
      m_model->unmergeModel(m_dhModel);
    }

    fillCustomizationMap();
    CharDetailsEvent event(this, CharDetailsEvent::DH_MODE_CHANGED);
    notify(event);
  }
}
