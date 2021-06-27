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

std::multimap<uint, int> CharDetails::LINKED_OPTIONS_MAP_ = {
  // hardcoded values (need to figure out how to find this from DB - if possible ?)
  {726, 724}, // veins color linked to veins for BE male
  {730, 728} // veins color linked to veins for BE female
};

CharDetails::CharDetails():
eyeGlowType(EGT_NONE), showUnderwear(true), showEars(true), showHair(true),
showFacialHair(true), showFeet(true), autoHideGeosetsForHeadItems(true), 
isNPC(true), model_(nullptr), isDemonHunter_(false)
{
  refreshGeosets();
}

void CharDetails::save(QXmlStreamWriter & stream)
{
  stream.writeStartElement("CharDetails");

  for (auto & opt : currentCustomization_)
  {
    stream.writeStartElement("customization");
    stream.writeAttribute("id", QString::number(opt.first));
    stream.writeAttribute("value", QString::number(opt.second));
    stream.writeEndElement();
  }

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
  stream.writeAttribute("value", QString::number(isDemonHunter_));
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

  while (!reader.atEnd())
  {
    if (reader.isStartElement())
    {
      if (reader.name() == "customization")
        set(reader.attributes().value("id").toString().toUInt(), reader.attributes().value("value").toString().toUInt());

      if (reader.name() == "eyeGlowType")
        eyeGlowType = (EyeGlowTypes)reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "showUnderwear")
        showUnderwear = reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "showEars")
        showEars = reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "showHair")
        showHair = reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "showFacialHair")
        showFacialHair = reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "showFeet")
        showFeet = reader.attributes().value("value").toString().toUInt();

      if (reader.name() == "isDemonHunter")
      {
        LOG_INFO << __FILE__ << __LINE__ << "reading demonHunter mode value";
        setDemonHunterMode(reader.attributes().value("value").toString().toUInt());
      }
    }
    reader.readNext();
  }
}

void CharDetails::reset(WoWModel * model)
{
  if ((model != nullptr) & (model != model_))
  {
    model_ = model;
    fillCustomizationMap();
  }

  currentCustomization_.clear();

  showUnderwear = true;
  showHair = true;
  showFacialHair = true;
  showEars = true;
  showFeet = false;

  isNPC = false;

  isDemonHunter_ = false;

  refreshGeosets();
  refreshTextures();

  for (const auto &c : choicesPerOptionMap_)
    set(c.first, c.second[0]);
}

void CharDetails::randomise()
{
  // TODO repair randomise
  reset();
  /*
  // Choose random values for the looks! ^_^
  setRandomValue(SKIN_COLOR);
  setRandomValue(FACE);
  setRandomValue(FACIAL_CUSTOMIZATION_STYLE);
  setRandomValue(FACIAL_CUSTOMIZATION_COLOR);
  setRandomValue(ADDITIONAL_FACIAL_CUSTOMIZATION);

  // Don't worry about Custom 1-3 for elves, unless they're Demon Hunters:
  if(isDemonHunter_)
  {
    setRandomValue(CUSTOM1_STYLE);
    setRandomValue(CUSTOM1_COLOR);
    setRandomValue(CUSTOM2_STYLE);
    setRandomValue(CUSTOM3_STYLE);
  }
  */
}

void CharDetails::fillCustomizationMap()
{
  if (!model_)
    return;

  // clear any previous value found
  choicesPerOptionMap_.clear();

  const auto infos = model_->infos;
  if (infos.raceID == -1)
    return;

  auto options = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationOption WHERE ChrModelID = %1 AND ChrCustomizationID != 0 ORDER BY OrderIndex").arg(infos.ChrModelID[0]));

  if (options.valid)
    for (auto& option : options.values)
      choicesPerOptionMap_[option[0].toUInt()] = {};
  
  LINKED_OPTIONS_MAP_.clear();
  initLinkedOptionsMap();

  for (auto &option : choicesPerOptionMap_)
    fillCustomizationMapForOption(option.first);
}

void CharDetails::fillCustomizationMapForOption(uint chrCustomizationOption)
{
  const auto parentOptions = getParentOptions(chrCustomizationOption);
 
  auto &vals = choicesPerOptionMap_.at(chrCustomizationOption);
  const auto curvals = vals;
  vals.clear();

  // 1. fill direct values
  auto choices = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1 ORDER BY OrderIndex").arg(chrCustomizationOption));
  if (choices.valid)
  {
    LOG_INFO << __FUNCTION__ << "DIRECT values" << choices.values.size();
    for (auto v : choices.values)
      vals.push_back(v[0].toUInt());
  }

  // 2. fill with parent values
  /*
  for (auto parentOption : parentOptions)
  {
    choices.valid = false;
    if ((parentOption != -1) && (currentCustomization_.count(parentOption) != 0))
    {
      choices = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationChoice WHERE ID IN (SELECT ChrCustomizationChoiceID FROM ChrCustomizationElement WHERE RelatedChrCustomizationChoiceID = %1) "
        "ORDER BY OrderIndex").arg(currentCustomization_[parentOption]));
    }

    if (choices.valid)
    {
      LOG_INFO << __FUNCTION__ << "INDIRECT values from" << parentOption << currentCustomization_[parentOption] << choices.values.size();
      for (auto v : choices.values)
        vals.push_back(v[0].toUInt());
    }
  }

  // remove potential duplicates
  std::sort(vals.begin(), vals.end());
  const auto last = std::unique(vals.begin(), vals.end());
  vals.erase(last, vals.end());
  */
  if (vals != curvals)
  {
    LOG_INFO << __FUNCTION__ << chrCustomizationOption;
    QString info;
    for (const auto& v : vals)
      info += QString("%1 ").arg(v);
    LOG_INFO << info;

    CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
    event.setCustomizationOptionId(chrCustomizationOption);
    notify(event);
  }
}




void CharDetails::set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID) // wow version >= 9.x
{
  const auto infos = model_->infos;
  if (infos.raceID == -1)
    return;

  currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;
  customizationElementsPerOption_.erase(chrCustomizationOptionID);

  LOG_INFO << __FUNCTION__ << chrCustomizationOptionID << chrCustomizationChoiceID;
  const auto parentOptions = getParentOptions(chrCustomizationOptionID);
  const auto childOption = getChildOption(chrCustomizationOptionID);

  LOG_INFO << "Parent options for" << chrCustomizationOptionID;
  for (const auto &opt : parentOptions)
    LOG_INFO << "\t" << opt;
  LOG_INFO << "Child option for" << chrCustomizationOptionID;
    LOG_INFO << "\t" << childOption;

  auto choiceId = chrCustomizationChoiceID;
  auto relatedChoiceId = 0;

  // 1. First query direct elements (related choice id = 0)
  auto query = QString("SELECT ChrCustomizationGeosetID, ChrCustomizationSkinnedModelID, ChrCustomizationMaterialID, "
                              "ChrCustomizationBoneSetID, ChrCustomizationCondModelID, ChrCustomizationDisplayInfoID, ID FROM ChrCustomizationElement "
                              "WHERE ChrCustomizationChoiceID = %1 AND RelatedChrCustomizationChoiceID = %2").arg(choiceId)
                              .arg(relatedChoiceId);

  auto elements = GAMEDATABASE.sqlQuery(query);
  if (!applyChrCustomizationElements(chrCustomizationOptionID, elements))
  {
    LOG_ERROR << __FUNCTION__ << "No direct customization entry found for chrCustomizationOptionID" << chrCustomizationOptionID << "/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
    LOG_ERROR << query;
  }

  // 2. Query elements coming from parent options
  for(const auto option:parentOptions)
  {
    if(option != -1)
    {
      relatedChoiceId = currentCustomization_[option];

      // query related ChrCustomizationElements
      query = QString("SELECT ChrCustomizationGeosetID, ChrCustomizationSkinnedModelID, ChrCustomizationMaterialID, "
        "ChrCustomizationBoneSetID, ChrCustomizationCondModelID, ChrCustomizationDisplayInfoID, ID FROM ChrCustomizationElement "
        "WHERE ChrCustomizationChoiceID = %1 AND RelatedChrCustomizationChoiceID = %2").arg(choiceId)
        .arg(relatedChoiceId);

      elements = GAMEDATABASE.sqlQuery(query);

      if (!applyChrCustomizationElements(option, elements))
      {
        LOG_ERROR << __FUNCTION__ << "Parent Option" << option << "-> No dependant customization entry found for chrCustomizationOptionID" << chrCustomizationOptionID << "/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
        LOG_ERROR << query;
      }
    }
  }

  // 3. Query elements coming from child option
  if (childOption != -1)
  {
    // we are setting an option which have a dependant option, we need to set child choice with a new related choice (ie, we are setting tattoo, which needs to set tattoo color)
    choiceId = currentCustomization_[childOption];
    relatedChoiceId = chrCustomizationChoiceID;
    //customizationElementsPerOption_.erase(childOption);
    fillCustomizationMapForOption(childOption);
    
    // query related ChrCustomizationElements
    query = QString("SELECT ChrCustomizationGeosetID, ChrCustomizationSkinnedModelID, ChrCustomizationMaterialID, "
      "ChrCustomizationBoneSetID, ChrCustomizationCondModelID, ChrCustomizationDisplayInfoID, ID FROM ChrCustomizationElement "
      "WHERE ChrCustomizationChoiceID = %1 AND RelatedChrCustomizationChoiceID = %2").arg(choiceId)
      .arg(relatedChoiceId);

    elements = GAMEDATABASE.sqlQuery(query);

    if (!applyChrCustomizationElements(chrCustomizationOptionID, elements))
    {
      LOG_ERROR << __FUNCTION__ << "Child option" << childOption << "No dependant customization entry found for chrCustomizationOptionID" << chrCustomizationOptionID << "/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
      LOG_ERROR << query;
    }
  }

  CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
  event.setCustomizationOptionId(chrCustomizationOptionID);
  notify(event);

  model_->refresh();
 // TEXTUREMANAGER.dump();
}

std::vector<uint> CharDetails::getCustomizationChoices(const uint chrCustomizationOptionID)
{
  if (choicesPerOptionMap_.count(chrCustomizationOptionID) == 0)
    fillCustomizationMap();

  return choicesPerOptionMap_.at(chrCustomizationOptionID);

}

uint CharDetails::get(uint chrCustomizationOptionID) const
{
  return currentCustomization_.at(chrCustomizationOptionID);
}

void CharDetails::setRandomValue(CustomizationType type)
{
  /*
  const auto allValues = customizationParamsMap_[type].possibleValues;
  if (allValues.empty())
    return;
  const auto flags = customizationParamsMap_[type].flags;
  std::vector<int> filteredIndices;
  for (uint i = 0; i < allValues.size(); i++)
  {
    const auto flag = flags[i];
    if (isDemonHunter_)
    {
      if ((flag & SF_DEMON_HUNTER) || (flag & SF_DEMON_HUNTER_FACE) || (flag & SF_DEMON_HUNTER_BFX) || (flag & SF_REGULAR) || flag == 0)
      {
        filteredIndices.push_back(i);
      }
    }
    else  // only select regular, mundane skins for the random display
    {
      if ((flag & SF_REGULAR) || flag == SF_BARBERSHOP || flag == SF_CHARACTER_CREATE || flag == 0)
      {
        filteredIndices.push_back(i);
      }
    }
  }
  if (!filteredIndices.empty())
  {
    const auto maxVal = filteredIndices.size() - 1;
    const auto randval = filteredIndices[randint(0, maxVal)];
    set(type, randval);
  }
  else // ok, filtering left us with nothing...
  {
    const auto maxVal = allValues.size() - 1;
    const auto randval = randint(0, maxVal);
    set(type, randval);
  }
  */
}

bool CharDetails::applyChrCustomizationElements(uint chrCustomizationOption, sqlResult & elements)
{
  LOG_INFO << __FUNCTION__ << chrCustomizationOption << elements.values.size();

  if (elements.valid && !elements.values.empty())
  {
    for (auto elt : elements.values) // treat each line
    {
      if (elt[0].toUInt() != 0) // geoset customization
      {
        LOG_INFO << "ChrCustomizationGeosetID based customization for" << elt[6] << "/" << elt[0];

        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT GeosetType, GeoSetID FROM ChrCustomizationGeoset WHERE ID = %1").arg(elt[0].toUInt()));

        if (vals.valid)
        {
          for (auto geo : vals.values)
            customizationElementsPerOption_[chrCustomizationOption].geosets.emplace_back(geo[0].toUInt(),geo[1].toUInt());
        }
      }
      else if (elt[1].toUInt() != 0) // added model customization
      {
        LOG_INFO << "ChrCustomizationSkinnedModelID based customization for" << elt[6] << "/" << elt[1];
        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT CollectionsFileDataID, GeosetType, GeoSetID FROM ChrCustomizationSkinnedModel WHERE ID = %1").arg(elt[1].toUInt()));

        if (vals.valid)
          customizationElementsPerOption_[chrCustomizationOption].models.emplace_back(vals.values[0][0].toInt(), std::make_pair(vals.values[0][1].toInt(), vals.values[0][2].toInt()));
      }
      else if (elt[2].toUInt() != 0) // texture customization
      {
        LOG_INFO << "ChrCustomizationMaterialID based customization for" << elt[6] << "/" << elt[2];
        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT ChrModelTextureLayer.Layer, ChrModelTextureLayer.TextureSectionTypeBitMask, ChrModelTextureLayer.TextureType, TextureID FROM ChrCustomizationMaterial "
          "LEFT JOIN TextureFileData ON ChrCustomizationMaterial.MaterialResourcesID = TextureFileData.MaterialResourcesID "
          "LEFT JOIN ChrModelTextureLayer ON ChrCustomizationMaterial.ChrModelTextureTargetID = ChrModelTextureLayer.ChrModelTextureTargetID1 "
          "AND ChrModelTextureLayer.CharComponentTextureLayoutsID = %1 "
          "WHERE ChrCustomizationMaterial.ID = %2").arg(model_->infos.textureLayoutID).arg(elt[2].toUInt()));

        if (vals.valid)
        {
          TextureCustomization t{};
          t.layer = vals.values[0][0].toUInt();
          t.region = bitMaskToSectionType(vals.values[0][1].toInt());
          t.type = vals.values[0][2].toUInt();
          t.fileId = vals.values[0][3].toUInt();
          customizationElementsPerOption_[chrCustomizationOption].textures.push_back(t);
        }
      }
      else if (elt[3].toUInt() != 0) // boneset customization ??
      {
        LOG_ERROR << "Not yet implemented ! boneset based customization for" << elt[6] << "/" << elt[3];
      }
      else if (elt[4].toUInt() != 0) // cond model customization ??
      {
        LOG_ERROR << "Not yet implemented ! Cond model based customization for" << elt[6] << "/" << elt[4];
      }
      else if (elt[5].toUInt() != 0) // display info customization ??
      {
        LOG_ERROR << "Not yet implemented ! Display info based customization for" << elt[6] << "/" << elt[5];
      }
    }
    return true;
  }
  return false;
}

int CharDetails::bitMaskToSectionType(int mask)
{
  if (mask == -1)
    return -1;

  if (mask == 0)
    return 0;

  auto val = 1;

  while (((mask = mask >> 1) & 0x01) == 0)
    val++;

  return val;
}

std::vector<int> CharDetails::getParentOptions(uint chrCustomizationOption)
{
  initLinkedOptionsMap();

  std::vector<int> result;

  const auto vals = LINKED_OPTIONS_MAP_.equal_range(chrCustomizationOption);

  for (auto it = vals.first; it != vals.second; ++it)
    result.push_back(it->second);

  return result;
}

int CharDetails::getChildOption(uint chrCustomizationOption)
{
  initLinkedOptionsMap();

  for (const auto &c: LINKED_OPTIONS_MAP_)
  {
    if (c.second == static_cast<int>(chrCustomizationOption))
      return static_cast<int>(c.first);
  }

  return -1;
}

void CharDetails::initLinkedOptionsMap()
{
  if (!LINKED_OPTIONS_MAP_.empty()) // already initialized
    return;

  for (const auto& c : choicesPerOptionMap_)
  {
    auto id = c.first;
    const auto query = QString("SELECT DISTINCT ChrCustomizationOptionID FROM ChrCustomizationChoice WHERE ID IN "
      "(SELECT RelatedChrCustomizationChoiceID FROM ChrCustomizationElement WHERE ChrCustomizationChoiceID = "
      "(SELECT ID FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1 AND OrderIndex = 1))").arg(id);

    auto link = GAMEDATABASE.sqlQuery(query);

    if (link.valid && !link.values.empty())
    {
      for(const auto & vals: link.values)
        LINKED_OPTIONS_MAP_.emplace(id, vals[0].toInt());
    }
    else
    {
      LINKED_OPTIONS_MAP_.emplace(id, -1);
    }
  }
}

void CharDetails::refresh()
{
  refreshGeosets();
  refreshTextures();
  refreshSkinnedModels();
}


void CharDetails::refreshGeosets()
{
  geosets.clear();

  for (auto i = 0; i < NUM_GEOSETS; i++)
    geosets[i] = 1;

  if (showEars)
    geosets[CG_EARS] = 2;
  else
    geosets[CG_EARS] = 0;

  geosets[CG_GEOSET100] = geosets[CG_GEOSET200] = geosets[CG_GEOSET300] = 0;

  // apply customization elements
  for (const auto& elt : customizationElementsPerOption_)
  {
    for (auto geo : elt.second.geosets)
    {
      if (geo.first == CG_EARS && !showEars)
        continue;
      geosets[geo.first] = geo.second;
    }
  }

  if (model_)
  {
    // only show underwear bottoms if the character isn't wearing pants or chest 
    if (showUnderwear && model_->getItemId(CS_PANTS) < 1 && !model_->isWearingARobe())
    {
      // demon hunters and female pandaren use the TABARD2 geoset for part of their underwear:
      if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
        geosets[CG_TABARD2] = 1;
    }
    else  // hide underwear
    {
      // demon hunters and female pandaren - need to hide the TABARD2 geoset when no underwear:
      if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
        geosets[CG_TABARD2] = 0;
    }
  }

}

void CharDetails::refreshTextures()
{
  textures.clear();

  // apply customization elements
  for (const auto& elt : customizationElementsPerOption_)
  {
    for (auto t : elt.second.textures)
    {
      if (model_ != nullptr)
      {
        // don't apply underwear tops/bras if show underwear is off or if the character is wearing a shirt or chest
        if (t.region == CR_TORSO_UPPER &&
          (!showUnderwear ||
            model_->getItemId(CS_CHEST) > 1 || model_->getItemId(CS_SHIRT) > 1))
          continue;

        // don't apply underwear bottoms if show underwear is off or if the character is wearing pants
        if (t.region == CR_LEG_UPPER &&
          (!showUnderwear ||
            model_->getItemId(CS_PANTS) > 1))
          continue;
      }

      textures.push_back(t);
    }
  }

}

void CharDetails::refreshSkinnedModels()
{
  // first clean any previous merging
  for (const auto m : models_)
    model_->unmergeModel(m.first);

  models_.clear();

  for (const auto& elt : customizationElementsPerOption_)
  {
    for (const auto m : elt.second.models)
    {
      auto * model = model_->mergeModel(m.first);
      model->setGeosetGroupDisplay((CharGeosets)m.second.first, m.second.second);
      models_.emplace_back(m.first, m.second);
    }
  }
}
