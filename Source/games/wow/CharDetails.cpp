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

#include <set>

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

  // Apply a default choice to EVERY customization option, like the WoW client
  // does when first creating a character. We must not skip options flagged 0x20:
  // those include things like female-undead "Jaw Features", and skipping them
  // leaves the jaw geoset unselected -> a visibly missing jaw. (the game client only
  // auto-defaults non-0x20 options, which is exactly why it shows the same bug
  // unless a saved appearance is imported.)
  // Batch the default-apply: set() normally calls the expensive model_->refresh() each
  // time, so applying defaults to all ~45 options used to refresh ~45 times (a multi-second
  // freeze on every character open). With batchUpdate_ each set() only records the choice;
  // we refresh ONCE at the end (mirrors randomise()).
  batchUpdate_ = true;
  for (const auto &c : choicesPerOptionMap_)
  {
    // pick the first choice whose requirement is satisfied for this character so
    // class-gated choices (Demon-Hunter horns/blindfold/tattoos on a non-DH, etc.)
    // are not applied by default.
    for (const uint choiceID : c.second)
    {
      if (isChoiceAvailable(choiceID))
      {
        set(c.first, choiceID);
        break;
      }
    }
  }
  batchUpdate_ = false;

  if (model_)
    model_->refresh(); // single refresh for the whole default set (was ~45)
}

void CharDetails::randomise()
{
  // Pick a random AVAILABLE choice for every customization option. isChoiceAvailable
  // keeps the result valid for this character (e.g. a non-Demon-Hunter never
  // randomises into DH-only horns/blindfold).
  //
  // Batch the apply: set() normally calls the expensive model_->refresh() (texture
  // re-composite + skinned-model reload + geoset merge) every time, so randomising all
  // ~45 Dracthyr options used to refresh ~45 times and took several seconds. With
  // batchUpdate_ each set() only records the choice; we refresh ONCE at the end.
  if (!model_)
    return;

  // Snapshot the options first: set() can modify choicesPerOptionMap_ (child options),
  // so iterate a stable copy rather than the live map.
  const std::vector<std::pair<uint, std::vector<uint> > > opts(choicesPerOptionMap_.begin(), choicesPerOptionMap_.end());

  batchUpdate_ = true;
  for (const auto & c : opts)
  {
    std::vector<uint> avail;
    for (const uint choiceID : c.second)
      if (isChoiceAvailable(choiceID))
        avail.push_back(choiceID);

    if (!avail.empty())
      set(c.first, avail[randint(0, static_cast<int>(avail.size()) - 1)]);
  }
  batchUpdate_ = false;

  model_->refresh(); // single refresh for the whole randomised set
}

void CharDetails::setDemonHunterMode(bool val)
{
  if (val == isDemonHunter_)
    return;
  isDemonHunter_ = val;
  if (!model_)
    return;

  // Toggling DH mode only changes which choices isChoiceAvailable() permits, but
  // availability alone applies nothing: the DH horns/blindfold collection models,
  // geosets and textures only enter customizationElementsPerOption_ when set()
  // runs for the option. So re-resolve every option and APPLY it via set() --
  // keep the user's current choice when it is still available, otherwise fall
  // back to the first available one. set() also repopulates the merged skinned
  // models, so DH looks attach (on) and detach (off) correctly.
  for (const auto & c : choicesPerOptionMap_)
  {
    uint chosen = 0;
    const uint cur = get(c.first);
    if (cur != 0 && isChoiceAvailable(cur))
      chosen = cur;
    else
      for (const uint choiceID : c.second)
        if (isChoiceAvailable(choiceID)) { chosen = choiceID; break; }

    if (chosen != 0)
      set(c.first, chosen);                            // repopulates elements + refreshes
    else
      customizationElementsPerOption_.erase(c.first);  // no available choice: drop stale DH elements
  }
}

void CharDetails::fillCustomizationMap()
{
  if (!model_)
    return;

  // clear any previous value found
  choicesPerOptionMap_.clear();
  optionFlags_.clear();
  choiceGeosetElements_.clear();
  choiceReq_.clear();

  const auto infos = model_->infos;
  if (infos.raceID == -1)
    return;

  auto options = GAMEDATABASE.sqlQuery(QString("SELECT ID, Flags FROM ChrCustomizationOption WHERE ChrModelID = %1 AND ChrCustomizationID != 0 ORDER BY OrderIndex").arg(infos.ChrModelID[0]));

  // Some ChrModels have EVERY option with ChrCustomizationID == 0 (e.g. the
  // Dracthyr visage male, ChrModelID 127, plus ~22 newer forms). For those the
  // filter above returns nothing and the character renders with no customization
  // -> a blank body. Fall back to the unfiltered option set ONLY in that case, so
  // models that do have ChrCustomizationID-tagged options keep their exact prior
  // behaviour (no regression) while the otherwise-blank models become customisable.
  if (!options.valid || options.values.empty())
    options = GAMEDATABASE.sqlQuery(QString("SELECT ID, Flags FROM ChrCustomizationOption WHERE ChrModelID = %1 ORDER BY OrderIndex").arg(infos.ChrModelID[0]));

  if (options.valid)
    for (auto& option : options.values)
    {
      const auto id = option[0].toUInt();
      choicesPerOptionMap_[id] = {};
      optionFlags_[id] = option[1].toUInt();
    }
  
  LINKED_OPTIONS_MAP_.clear();
  initLinkedOptionsMap();

  // Load per-choice requirement fields so we can hide choices that don't apply to
  // this character: class/race gating (e.g. Demon-Hunter-only horns/blindfold on a
  // regular character) and "borrowed"/collectible appearances the account must unlock
  // (achievement/quest/transmog item). See isChoiceAvailable() for how each is used.
  auto reqs = GAMEDATABASE.sqlQuery(
    "SELECT ChrCustomizationChoice.ID, ChrCustomizationReq.ClassMask, ChrCustomizationReq.RaceMask, "
    "ChrCustomizationReq.ReqAchievementID, ChrCustomizationReq.ReqQuestID, ChrCustomizationReq.ReqItemModifiedAppearanceID "
    "FROM ChrCustomizationChoice "
    "JOIN ChrCustomizationReq ON ChrCustomizationReq.ID = ChrCustomizationChoice.ChrCustomizationReqID "
    "WHERE ChrCustomizationChoice.ChrCustomizationReqID != 0");
  if (reqs.valid)
    for (auto & r : reqs.values)
    {
      ChoiceReq cr;
      cr.classMask = r[1].toLongLong();
      cr.raceMask = r[2].toLongLong();
      cr.unlockGated = (r[3].toLongLong() != 0) || (r[4].toLongLong() != 0) || (r[5].toLongLong() != 0);
      choiceReq_[r[0].toUInt()] = cr;
    }

  for (auto &option : choicesPerOptionMap_)
    fillCustomizationMapForOption(option.first);
}

bool CharDetails::isChoiceAvailable(uint chrCustomizationChoiceID) const
{
  const auto it = choiceReq_.find(chrCustomizationChoiceID);
  if (it == choiceReq_.end())
    return true; // no requirement -> available to everyone

  const long long raceMask = it->second.raceMask;
  const int rid = (model_ && model_->infos.raceID > 0) ? model_->infos.raceID : 0;

  // 1. Unlock-gated appearances. A ChrCustomizationReq with an achievement, quest or
  // transmog-item requirement is a collectible/"borrowed" look the account has to earn;
  // the in-game appearance editor hides them by default and WMV cannot evaluate the
  // condition, so drop them. This replaces an older guess (RaceMask == 0xFFFFFFFF) that
  // mis-classified the *normal* "all classic races" mask as junk -- which silently
  // wiped out every hair/ear/jewellery/skin choice on Blood Elf and left it bald with
  // empty customization dropdowns. The real gate is these unlock fields.
  if (it->second.unlockGated)
    return false;

  // 2. Race gating. RaceMask is a 64-bit race bitmask (race N => bit N-1); 0 = no race
  // restriction. The low 32 bits set (0xFFFFFFFF) is the "all classic races" baseline.
  // Two cases must be told apart:
  //   * low == 0xFFFFFFFF and NO high bits  -> a genuine all-core-races appearance
  //     (real hair, eyes, etc.) -> keep for any classic race.
  //   * low == 0xFFFFFFFF and high bits set -> the entry actually targets the high
  //     (allied/Evoker) races and a classic race is only incidentally in the baseline:
  //     e.g. the Evoker "Primalist" eye colours and Dracthyr "Slit/Star/Glow" eye
  //     styles. A classic race (id <= 32, its bit lives in the always-set low word)
  //     should NOT show these, so drop them. A high race (id > 32) IS genuinely the
  //     target, so it keeps them -- this is what stops the Dracthyr dragon (whose own
  //     bit 52/70 is not set, but which is covered by the broad baseline) from losing
  //     all of its customization.
  if (raceMask != 0 && rid > 0)
  {
    const unsigned long long m = static_cast<unsigned long long>(raceMask);
    const bool broadClassic = (m & 0xFFFFFFFFull) == 0xFFFFFFFFull;
    const bool hasHighBits = (m >> 32) != 0;
    if (broadClassic)
    {
      if (hasHighBits && rid <= 32)
        return false; // borrowed allied/Evoker appearance, not for this classic race
      // else: applies broadly to this race -> keep
    }
    else if (rid <= 64) // narrow, genuinely race-specific mask (avoid 1<<69 for race 70)
    {
      const unsigned long long raceBit = 1ull << (rid - 1);
      if ((m & raceBit) == 0)
        return false;
    }
  }

  // 3. ChrCustomizationReq.ClassMask is an int32 class bitmask (class N => bit N-1).
  // 0 = no class restriction; "all classes" comes through as the unsigned int32
  // sentinel 0xFFFFFFFF (NOT -1, since it's read unsigned). We only model Demon
  // Hunter specially: hide a choice only when it is DEMON-HUNTER-EXCLUSIVE -- the
  // DH bit (0x800, class 12) is the ONLY player-class bit set -- and we are not in
  // Demon Hunter mode. Masking to the 13 player-class bits makes the "all classes"
  // value (0x1FFF / 0xFFFFFFFF) read as non-exclusive, so normal choices (hair,
  // eyes, skin, the "None" options) are never filtered out.
  const long long classBits = it->second.classMask & 0x1FFFll; // player classes 1..13
  const bool dhExclusive = (classBits != 0) && ((classBits & ~0x800ll) == 0);
  if (dhExclusive && !isDemonHunter_)
    return false;

  return true;
}

void CharDetails::fillCustomizationMapForOption(uint chrCustomizationOption)
{
  const auto parentOptions = getParentOptions(chrCustomizationOption);
 
  auto &vals = choicesPerOptionMap_.at(chrCustomizationOption);
  const auto curvals = vals;
  vals.clear();

  // 1. fill direct values
  // ORDER BY ID as a tiebreaker after OrderIndex: several choices can share an
  // OrderIndex, and without a stable secondary sort SQLite returns them in an
  // arbitrary order, so reset()'s "first available choice" default would vary
  // between loads (a character's default skin/markings changing each time).
  //
  // Drop the game's "Transmog" placeholder choice. Many options (Skin/Hair/Eye
  // Color, Face, Fur Color, ...) carry one extra choice -- named "Transmog" in
  // English -- that is not a real appearance: it is the slot meaning "this part is
  // driven by the equipped transmog set". A model viewer has no transmog context,
  // so it is a dead entry in every affected dropdown (e.g. Blood Elf Skin Color #25,
  // Hair Color #13, Eye Color #20). Every such choice, and ONLY these, points at the
  // shared requirement row ChrCustomizationReqID 10 (verified across the whole table:
  // every choice on that requirement is the placeholder, no real choice uses it), so
  // we exclude by that requirement id rather than the localized name -- locale
  // independent, and the choice never appears, is never a default, is never randomised.
  const int TRANSMOG_PLACEHOLDER_REQ = 10;
  auto choices = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1 AND ChrCustomizationReqID <> %2 ORDER BY OrderIndex, ID").arg(chrCustomizationOption).arg(TRANSMOG_PLACEHOLDER_REQ));
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




bool CharDetails::hasOption(uint chrCustomizationOptionID) const
{
  return choicesPerOptionMap_.find(chrCustomizationOptionID) != choicesPerOptionMap_.end();
}

void CharDetails::set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID) // wow version >= 9.x
{
  const auto infos = model_->infos;
  if (infos.raceID == -1)
    return;

  currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;
  customizationElementsPerOption_.erase(chrCustomizationOptionID);

  const auto parentOptions = getParentOptions(chrCustomizationOptionID);
  const auto childOption = getChildOption(chrCustomizationOptionID);

  if (!batchUpdate_) // skip the verbose per-option logging during a batch (randomise)
  {
    LOG_INFO << __FUNCTION__ << chrCustomizationOptionID << chrCustomizationChoiceID;
    LOG_INFO << "Parent options for" << chrCustomizationOptionID;
    for (const auto &opt : parentOptions)
      LOG_INFO << "\t" << opt;
    LOG_INFO << "Child option for" << chrCustomizationOptionID;
    LOG_INFO << "\t" << childOption;
  }

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

  // If this choice adds a skinned model whose texture is gated by another option
  // (e.g. a DH blindfold needs a DH eye-glow colour), make sure that option holds a
  // compatible value, otherwise the model merges untextured and renders white.
  autoSelectTextureGating(chrCustomizationChoiceID);

  CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
  event.setCustomizationOptionId(chrCustomizationOptionID);
  notify(event);

  // During a batch (randomise) skip the heavy refresh; the caller refreshes once.
  if (!batchUpdate_)
    model_->refresh();
 // TEXTUREMANAGER.dump();
}

void CharDetails::autoSelectTextureGating(uint chrCustomizationChoiceID)
{
  if (autoSelectInProgress_ || !model_)
    return;

  // Only choices that add a skinned model can end up merged-but-untextured this way.
  auto sm = GAMEDATABASE.sqlQuery(QString(
    "SELECT 1 FROM ChrCustomizationElement WHERE ChrCustomizationChoiceID = %1 "
    "AND ChrCustomizationSkinnedModelID != 0 LIMIT 1").arg(chrCustomizationChoiceID));
  if (!sm.valid || sm.values.empty())
    return;

  // The choice's direct-bind (non-skin) material elements, with the related choice that
  // supplies each. A SKIN-typed material composes into the body skin, so the model is
  // textured regardless and needs no gating; only non-skin (direct-bind) materials can
  // leave it white.
  auto mats = GAMEDATABASE.sqlQuery(QString(
    "SELECT ChrCustomizationElement.RelatedChrCustomizationChoiceID, ChrModelTextureLayer.TextureType "
    "FROM ChrCustomizationElement "
    "JOIN ChrCustomizationMaterial ON ChrCustomizationElement.ChrCustomizationMaterialID = ChrCustomizationMaterial.ID "
    "LEFT JOIN ChrModelTextureLayer ON ChrCustomizationMaterial.ChrModelTextureTargetID = ChrModelTextureLayer.ChrModelTextureTargetID1 "
    "AND ChrModelTextureLayer.CharComponentTextureLayoutsID = %1 "
    "WHERE ChrCustomizationElement.ChrCustomizationChoiceID = %2 "
    "AND ChrCustomizationElement.ChrCustomizationMaterialID != 0").arg(model_->infos.textureLayoutID).arg(chrCustomizationChoiceID));
  if (!mats.valid)
    return;

  // gating option -> compatible related choices that would supply a texture
  std::map<uint, std::vector<uint> > gates;
  bool hasUngated = false;
  for (auto & m : mats.values)
  {
    const uint related = m[0].toUInt();
    const int type = m[1].toInt();
    if (type == 1) // SKIN texture type: composed into the body skin, never leaves white
      continue;
    if (related == 0)
    {
      hasUngated = true; // a texture that always applies -> no gating needed
      continue;
    }
    auto opt = GAMEDATABASE.sqlQuery(QString(
      "SELECT ChrCustomizationOptionID FROM ChrCustomizationChoice WHERE ID = %1").arg(related));
    if (opt.valid && !opt.values.empty())
      gates[opt.values[0][0].toUInt()].push_back(related);
  }

  if (hasUngated || gates.empty())
    return;

  autoSelectInProgress_ = true;
  for (auto & g : gates)
  {
    const uint optID = g.first;
    if (choicesPerOptionMap_.count(optID) == 0)
      continue; // gating option not present on this model
    const uint cur = get(optID);
    if (std::find(g.second.begin(), g.second.end(), cur) != g.second.end())
      continue; // already a compatible value, nothing to do
    LOG_INFO << "autoSelectTextureGating: choice" << chrCustomizationChoiceID
             << "needs option" << optID << "-> switching it to compatible choice" << g.second.front();
    set(optID, g.second.front());
  }
  autoSelectInProgress_ = false;
}

std::vector<uint> CharDetails::getCustomizationChoices(const uint chrCustomizationOptionID)
{
  if (choicesPerOptionMap_.count(chrCustomizationOptionID) == 0)
    fillCustomizationMap();

  if (choicesPerOptionMap_.count(chrCustomizationOptionID) == 0)
    return {};

  // Only expose choices whose requirements are met for this character, so the
  // dropdown hides Demon-Hunter-only choices on a non-DH (matches Wowhead).
  std::vector<uint> available;
  for (const uint choiceID : choicesPerOptionMap_.at(chrCustomizationOptionID))
    if (isChoiceAvailable(choiceID))
      available.push_back(choiceID);

  return available;
}

uint CharDetails::get(uint chrCustomizationOptionID) const
{
  // Not every option is necessarily assigned a current choice (e.g. non-default
  // options skipped during reset()). Return 0 rather than throwing -- the UI builds
  // a control for every option and queries get() for each, so an unset option must
  // not crash (std::map::at() would throw -> uncaught -> terminate).
  const auto it = currentCustomization_.find(chrCustomizationOptionID);
  return (it != currentCustomization_.end()) ? it->second : 0;
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

        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT GeosetType, GeosetID FROM ChrCustomizationGeoset WHERE ID = %1").arg(elt[0].toUInt()));

        if (vals.valid)
        {
          for (auto geo : vals.values)
            customizationElementsPerOption_[chrCustomizationOption].geosets.emplace_back(geo[0].toUInt(),geo[1].toUInt());
        }
      }
      else if (elt[1].toUInt() != 0) // added model customization
      {
        LOG_INFO << "ChrCustomizationSkinnedModelID based customization for" << elt[6] << "/" << elt[1];
        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT CollectionsFileDataID, GeosetType, GeosetID FROM ChrCustomizationSkinnedModel WHERE ID = %1").arg(elt[1].toUInt()));

        if (vals.valid)
          customizationElementsPerOption_[chrCustomizationOption].models.emplace_back(vals.values[0][0].toInt(), std::make_pair(vals.values[0][1].toInt(), vals.values[0][2].toInt()));
      }
      else if (elt[2].toUInt() != 0) // texture customization
      {
        LOG_INFO << "ChrCustomizationMaterialID based customization for" << elt[6] << "/" << elt[2];
        auto vals = GAMEDATABASE.sqlQuery(QString("SELECT ChrModelTextureLayer.Layer, ChrModelTextureLayer.TextureSectionTypeBitMask, ChrModelTextureLayer.TextureType, ChrModelTextureLayer.BlendMode, FileDataID FROM ChrCustomizationMaterial "
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
          t.blendMode = vals.values[0][3].toUInt();
          t.fileId = vals.values[0][4].toUInt();

          LOG_INFO << "texture ->" << "layer" << t.layer << "region" << t.region << "type" << t.type << "blendMode" << t.blendMode << "fileId" << t.fileId;

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

  // NOTE: default geoset visibility is decided per-geoset in WoWModel::refresh()
  // using the face-geoset rule (show id 0 / *01 / 32xx face; hide 17xx eye-glow /
  // 35xx earrings). This map only records explicit toggles and customization
  // choices, which override those defaults via setGeosetGroupDisplay().

  // Ear shape is a per-race customization option (geoset group CG_EARS = 7) resolved
  // selectively by applyCustomizationGeosets(). The cd.geosets map is applied AFTER
  // customization (so explicit UI toggles win), so pinning CG_EARS to a fixed variant here
  // clobbered the chosen ear geoset -- e.g. Haranir's "Ears" option (Small/Droopy/Medium/Large,
  // geosets 702-705) had no effect because this forced group 7 back to variant 2 every refresh.
  // Only force the ears OFF when they must be hidden; when shown, leave group 7 to the active
  // Ears choice (or the default *01 rule for races that have no Ears option).
  if (!showEars)
    geosets[CG_EARS] = 0;

  // The facial-feature groups (1/2/3) carry facial hair, which is now a
  // customization option resolved by applyCustomizationGeosets(). Only force
  // them off when the user has explicitly disabled facial hair; otherwise leave
  // them untouched so the customization result is not clobbered by the
  // group-narrowing loop in WoWModel::refresh().
  if (!showFacialHair)
    geosets[CG_FACE_1] = geosets[CG_FACE_2] = geosets[CG_FACE_3] = 0;

  // NOTE: customization-choice geosets are applied selectively in
  // applyCustomizationGeosets() -- it toggles only the specific choice geosets
  // (show the active choice's geoset, hide the other choices' geosets). The old
  // group-narrowing here (setGeosetGroupDisplay) wrongly hid body geosets such
  // as the bare arms when they shared a group with a customization option.

  if (model_)
  {
    // only show underwear bottoms if the character isn't wearing pants or chest 
    if (showUnderwear && model_->getItemId(CS_PANTS) < 1 && !model_->isWearingARobe())
    {
      // demon hunters and female pandaren use the TABARD2 geoset for part of their underwear:
      if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
        geosets[CG_DH_LOINCLOTH] = 1;
    }
    else  // hide underwear
    {
      // demon hunters and female pandaren - need to hide the TABARD2 geoset when no underwear:
      if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
        geosets[CG_DH_LOINCLOTH] = 0;
    }
  }

}

const std::vector<std::pair<int, uint> > & CharDetails::getChoiceGeosetElements(uint chrCustomizationChoiceID)
{
  const auto it = choiceGeosetElements_.find(chrCustomizationChoiceID);
  if (it != choiceGeosetElements_.end())
    return it->second;

  // ALL geoset elements of the choice, each with its RelatedChrCustomizationChoiceID.
  // A single choice can reference several geosets, each gated by a related choice:
  // e.g. a Dracthyr drake's "Arm Spikes"/"Body Size" choice maps to a DIFFERENT
  // group variant depending on the active Body Size / related option (geoset 4201 vs
  // 4211). The related gate is what makes those variants mutually exclusive; ignoring
  // it (and just taking the last row) left two variants of the same group visible at
  // once -> z-fighting / spikes poking through. model geoset id = GeosetType*100 + GeosetID.
  std::vector<std::pair<int, uint> > elems;
  auto r = GAMEDATABASE.sqlQuery(QString(
    "SELECT ChrCustomizationGeoset.GeosetType, ChrCustomizationGeoset.GeosetID, ChrCustomizationElement.RelatedChrCustomizationChoiceID "
    "FROM ChrCustomizationElement "
    "JOIN ChrCustomizationGeoset ON ChrCustomizationElement.ChrCustomizationGeosetID = ChrCustomizationGeoset.ID "
    "WHERE ChrCustomizationElement.ChrCustomizationChoiceID = %1 "
    "AND ChrCustomizationElement.ChrCustomizationGeosetID != 0 "
    "ORDER BY ChrCustomizationElement.ID").arg(chrCustomizationChoiceID));

  if (r.valid)
    for (const auto & row : r.values)
      elems.emplace_back(row[0].toInt() * 100 + row[1].toInt(), row[2].toUInt());

  return choiceGeosetElements_.emplace(chrCustomizationChoiceID, std::move(elems)).first->second;
}

void CharDetails::applyCustomizationGeosets()
{
  if (!model_)
    return;

  // Apply customization-choice geosets. Each customization OPTION is mutually
  // exclusive within the set of geosets ITS choices control: the active choice's
  // applicable geosets are shown and every other geoset that any of the option's
  // choices could touch is hidden. This both shows the right variant AND suppresses
  // the group's default-on *01 geoset when a different/None variant is selected.
  //
  // An element only APPLIES when its RelatedChrCustomizationChoiceID is 0
  // (unconditional) or is itself an active choice -- that gate is what makes the
  // Dracthyr drake's related-dependent variants (geoset 4201 vs 4211, 4301 vs 4302)
  // mutually exclusive instead of overlapping (z-fighting).
  //
  // We collect the show-set and the controlled-set across ALL active options FIRST,
  // then apply once, so a geoset shared by two options (e.g. the undead jaw geoset
  // 202 used by several styles) is shown if ANY active choice wants it -- "active
  // wins" -- and is never clobbered back off by a sibling option.
  std::set<uint> activeChoices;
  for (const auto & a : currentCustomization_)
    activeChoices.insert(a.second);

  std::set<int> controlled;  // geosets any active option controls
  std::set<int> show;        // geosets the active choices want shown
  for (const auto & active : currentCustomization_) // option id -> active choice id
  {
    const auto choicesIt = choicesPerOptionMap_.find(active.first);
    if (choicesIt == choicesPerOptionMap_.end())
      continue;

    for (const uint choiceID : choicesIt->second)
    {
      const bool isActive = (choiceID == active.second);
      for (const auto & ge : getChoiceGeosetElements(choiceID))
      {
        controlled.insert(ge.first);
        const bool gateOk = (ge.second == 0) || (activeChoices.count(ge.second) != 0);
        if (isActive && gateOk)
          show.insert(ge.first);
      }
    }
  }

  for (const int id : controlled)
    model_->setGeosetDisplayById(id, show.count(id) != 0);
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
  // Files that were merged on the PREVIOUS refresh. Only skinned-customization models
  // are tracked here (equipment merges are owned separately by WoWItem and must not be
  // touched), so this is the exact set we are responsible for diffing against.
  std::set<uint> prevFiles;
  for (const auto m : models_)
    prevFiles.insert(m.first);

  // Several elements can share ONE collection-model file (e.g. the DH horn +
  // blindfold pack 7760202), each selecting a different geoset GROUP within it.
  // Merge each file ONCE and apply ALL of its selected groups, otherwise a later
  // element's hideAllGeosets() wipes an earlier element's geoset (so the horns
  // vanish the moment a blindfold is also active, and vice-versa).
  std::map<uint, std::vector<std::pair<uint, uint> > > groupsByFile; // fileID -> [(GeosetType, GeosetID)]
  models_.clear();
  for (const auto& elt : customizationElementsPerOption_)
    for (const auto m : elt.second.models)
    {
      groupsByFile[m.first].push_back(m.second);
      models_.emplace_back(m.first, m.second);
    }

  // Diff the desired set against what is already merged, and DEFER the geometry rebuild.
  // Previously this unmerged every skinned model and re-merged them all on every refresh,
  // re-reading each M2 from CASC and triggering a full refreshMerging() per merge/unmerge
  // -- so one customization change (or a single Randomise) did N+M full geometry rebuilds
  // plus N CASC reloads, freezing the UI for seconds. WoWModel::refresh() already ends with
  // one refreshMerging() that re-bakes all merged models, so here we only touch what
  // actually changed and pass noRefresh=true; the final pass does the single rebuild.
  for (uint fid : prevFiles)
    if (groupsByFile.find(fid) == groupsByFile.end())
      model_->unmergeModel(fid, true);            // no longer needed -> drop (cached, not freed)

  for (const auto& gf : groupsByFile)
    if (prevFiles.find(gf.first) == prevFiles.end())
      model_->mergeModel(gf.first, 1, true);      // newly needed -> merge (reuses cache, no CASC read)

  // (Re)apply the selected variant for EVERY needed file: the chosen geoset can change
  // even when the file itself persists across refreshes (e.g. a different horn style on
  // the same horn pack). This only toggles display flags on the merged model; the final
  // refreshMerging() copies them into the base (restoreRawGeosets preserves them by index).
  //
  // Hide everything, then show only the selected variant of each group.
  // setGeosetGroupDisplay's strict "id > GeosetType*100" test means a GeosetID of
  // 0 selects NOTHING -- correct, because a GeosetID-0 choice is the "None"
  // variant (e.g. Blindfold = None -> geoset 2500) which must stay hidden. Real
  // attachments (DH horns 2401, etc.) use GeosetID >= 1 and show normally.
  for (const auto& gf : groupsByFile)
  {
    auto * model = model_->getMergedModel(gf.first);
    if (!model)
      continue;
    model->hideAllGeosets();
    for (const auto& g : gf.second)
      model->setGeosetGroupDisplay((CharGeosets)g.first, g.second);
  }
}
