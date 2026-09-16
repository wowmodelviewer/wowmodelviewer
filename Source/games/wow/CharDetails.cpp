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
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <set>

namespace
{
  // ChrCustomizationReq.ClassMask names class N with bit N - 1. The client's ChrClasses run from 1
  // (Warrior) to 15 (Traveler) in 12.1.0, and the masks in the data use exactly bits 0-14.
  constexpr unsigned int CLASS_ID_LAST = 15;
  constexpr unsigned int classBit(unsigned int classID) { return 1u << (classID - 1); }
  constexpr unsigned int CLASS_BITS_ALL = (1u << CLASS_ID_LAST) - 1;

  // The class context of a character viewed without a class. The viewer has one class switch, the
  // Demon Hunter checkbox; otherwise the character is taken as an ordinary class: one whose looks
  // the data does not single out. Death Knight and Demon Hunter are the classes it does (Death
  // Knight eye glow and skin colours, Demon Hunter tattoos, horns and blindfolds), and the data's own
  // "everyone but them" masks show it: Req 146 0x7FDF (all but Death Knight), Req 144 0x77DF (all
  // but Death Knight and Demon Hunter).
  constexpr unsigned int CLASS_BITS_ORDINARY = CLASS_BITS_ALL & ~classBit(CLASS_DEATHKNIGHT) & ~classBit(CLASS_DEMONHUNTER);

  QString idList(const std::set<uint> & ids)
  {
    QStringList parts;
    for (const uint id : ids)
      parts << QString::number(id);
    return parts.join(QLatin1Char(','));
  }
}

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

  // Record every saved choice first and resolve them together at the end: judging each one on
  // arrival would test it against prerequisite choices that come later in the file.
  const SelectionState before = captureSelectionState();
  batchUpdate_ = true;

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

  batchUpdate_ = false;
  if (!model_ || model_->infos.raceID == -1)
    return;

  // A saved choice that is not valid with the loaded choices as a whole (e.g. a skin colour saved
  // with a skin type it does not belong to) gives way to the first valid one.
  resolveSelection(nullptr);
  const std::map<uint, uint> loaded = currentCustomization_;
  for (const auto & c : loaded)
    autoSelectTextureGating(c.second);
  applySelection(before, 0);
}

void CharDetails::reset(WoWModel * model)
{
  if ((model != nullptr) & (model != model_))
  {
    model_ = model;
    fillCustomizationMap();
  }

  const SelectionState before = captureSelectionState();
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

  if (!model_)
    return;

  // Apply a default choice to EVERY customization option the character can use, like the WoW
  // client does when first creating a character. We must not skip options flagged 0x20:
  // those include things like female-undead "Jaw Features", and skipping them
  // leaves the jaw geoset unselected -> a visibly missing jaw. (the game client only
  // auto-defaults non-0x20 options, which is exactly why it shows the same bug
  // unless a saved appearance is imported.)
  // Each option takes its first valid choice in client order, in resolution order, so an option
  // whose requirement names another option's choices (Skin Color after Skin Type, Eyesight after
  // Eye Color) is defaulted against that option's default. The whole set is applied with ONE
  // refresh (applying defaults option by option used to refresh ~45 times on a Dracthyr).
  resolveSelection(nullptr);
  const std::map<uint, uint> defaults = currentCustomization_;
  for (const auto & c : defaults)
    autoSelectTextureGating(c.second);
  applySelection(before, 0);
}

void CharDetails::randomise()
{
  // Pick a random VALID choice for every customization option, so the result is valid for this
  // character (e.g. a non-Demon-Hunter never randomises into DH-only horns/blindfold). Options are
  // drawn in resolution order: an option whose requirement names another option's choices is drawn
  // against the choice already drawn there (a Skin Color of the drawn Skin Type).
  //
  // The whole set is applied with ONE refresh: randomising all ~45 Dracthyr options one refresh
  // at a time used to take several seconds.
  if (!model_)
    return;

  const SelectionState before = captureSelectionState();
  for (const uint optionID : optionResolveOrder_)
  {
    const std::vector<uint> valid = validChoices(optionID, currentCustomization_);
    if (valid.empty())
      currentCustomization_.erase(optionID);
    else
      currentCustomization_[optionID] = valid[randint(0, static_cast<int>(valid.size()) - 1)];
  }
  const std::map<uint, uint> drawn = currentCustomization_;
  for (const auto & c : drawn)
    autoSelectTextureGating(c.second);
  applySelection(before, 0);
}

void CharDetails::setDemonHunterMode(bool val)
{
  if (val == isDemonHunter_)
    return;
  if (!model_ || batchUpdate_) // load() resolves the whole selection once it has read everything
  {
    isDemonHunter_ = val;
    return;
  }

  // The class context decides which choices are valid (see classMaskAllows), and the elements the
  // character wears follow its choices: re-validate every option -- keeping the current choice when
  // it is still valid, otherwise the first valid one -- and apply the result once, which also
  // attaches (on) or detaches (off) the DH horns/blindfold collection models.
  const SelectionState before = captureSelectionState();
  isDemonHunter_ = val;
  resolveSelection(nullptr);
  const std::map<uint, uint> resolved = currentCustomization_;
  for (const auto & c : resolved)
    autoSelectTextureGating(c.second);
  applySelection(before, 0);
}

void CharDetails::fillCustomizationMap()
{
  if (!model_)
    return;

  // clear any previous value found
  choicesPerOptionMap_.clear();
  optionFlags_.clear();
  optionClientOrder_.clear();
  optionResolveOrder_.clear();
  dependentOptions_.clear();
  optionRequirement_.clear();
  choiceRequirement_.clear();
  choiceOption_.clear();
  requirements_.clear();
  unresolvedRequirementsLogged_.clear();
  playableRaceBit_ = -1;
  choiceGeosetElements_.clear();
  choiceElementRows_.clear();
  resolvedElements_.clear();
  customizationElementsPerOption_.clear();

  const auto infos = model_->infos;
  if (infos.raceID == -1)
    return;

  // Load ALL of this model's customization options, regardless of ChrCustomizationOption's
  // ChrCustomizationID. An earlier version filtered to ChrCustomizationID != 0 and only fell
  // back to the unfiltered set when that returned NOTHING. That silently dropped every
  // ChrCustomizationID == 0 option on models that ALSO have a few tagged ones -- a mixed case
  // the all-empty fallback never caught -- and those options are legitimate. The casualties
  // were severe: the Dracthyr VISAGE female (ChrModelID 128) kept only Skin Color + Eyesight
  // and lost Face / Hair / Horns / Eye Color / Scales / Eyebrows / ...; every dragonriding
  // drake lost its entire armour wardrobe; and allied races (Vulpera, Mechagnome, Mag'har,
  // Dark Iron, Kul Tiran, ...) silently lost Eyesight + Eye Style. The visage MALE and every
  // all-zero form already rendered correctly from the unfiltered set via the old fallback, so
  // ChrCustomizationID == 0 is clearly valid -- there is no reason to filter at all.
  // Whether the character can use an option is its Requirement's call (see evaluateRequirement),
  // not this query's.
  auto options = GAMEDATABASE.sqlQuery(QString("SELECT ID, Flags, Requirement FROM ChrCustomizationOption WHERE ChrModelID = %1 ORDER BY OrderIndex, ID").arg(infos.ChrModelID[0]));

  if (options.valid)
    for (auto& option : options.values)
    {
      const auto id = option[0].toUInt();
      choicesPerOptionMap_[id] = {};
      optionFlags_[id] = option[1].toUInt();
      optionRequirement_[id] = option[2].toUInt();
      optionClientOrder_.push_back(id);
    }

  // Record which geoset GROUPS (GeosetType) any customization option for this model
  // drives. Used to distinguish fixed geometry the default-visibility rule must show
  // from geometry a customization choice owns -- specifically the ears group (CG_EARS=7),
  // which is fixed ear geometry on most playable races but is repurposed as a
  // customization option on others (Tail/Throat/Effects on dragonriding drakes,
  // "Modification" on Mechagnome, "Ears" on races with an ear-shape option). See
  // isGeosetGroupCustomized() and the ear-visibility net in WoWModel::refresh().
  customizationControlledGroups_.clear();
  {
    auto grps = GAMEDATABASE.sqlQuery(QString(
      "SELECT DISTINCT ChrCustomizationGeoset.GeosetType "
      "FROM ChrCustomizationOption "
      "JOIN ChrCustomizationChoice ON ChrCustomizationChoice.ChrCustomizationOptionID = ChrCustomizationOption.ID "
      "JOIN ChrCustomizationElement ON ChrCustomizationElement.ChrCustomizationChoiceID = ChrCustomizationChoice.ID "
      "JOIN ChrCustomizationGeoset ON ChrCustomizationGeoset.ID = ChrCustomizationElement.ChrCustomizationGeosetID "
      "WHERE ChrCustomizationOption.ChrModelID = %1").arg(infos.ChrModelID[0]));
    if (grps.valid)
      for (const auto & g : grps.values)
        customizationControlledGroups_.insert(g[0].toInt());
  }

  // Every choice of these options, in client order. ORDER BY ID as a tiebreaker after OrderIndex:
  // several choices can share an OrderIndex, and without a stable secondary sort SQLite returns
  // them in an arbitrary order, so the "first valid choice" default would vary between loads.
  // No choice is dropped here: whether one is offered is its requirement's call. That includes
  // the "Transmog" placeholder every Skin/Hair/Eye Color option carries (the slot meaning "driven
  // by the equipped transmog set"): its requirement (Req 10, ReqType 4) is not a player one, which
  // is what excludes it now, rather than its ID.
  auto choices = GAMEDATABASE.sqlQuery(QString(
    "SELECT ChrCustomizationChoice.ID, ChrCustomizationChoice.ChrCustomizationOptionID, ChrCustomizationChoice.ChrCustomizationReqID "
    "FROM ChrCustomizationChoice "
    "JOIN ChrCustomizationOption ON ChrCustomizationOption.ID = ChrCustomizationChoice.ChrCustomizationOptionID "
    "WHERE ChrCustomizationOption.ChrModelID = %1 "
    "ORDER BY ChrCustomizationChoice.OrderIndex, ChrCustomizationChoice.ID").arg(infos.ChrModelID[0]));
  if (choices.valid)
    for (const auto & c : choices.values)
    {
      const uint choiceID = c[0].toUInt();
      const uint optionID = c[1].toUInt();
      const auto listIt = choicesPerOptionMap_.find(optionID);
      if (listIt == choicesPerOptionMap_.end())
        continue;
      listIt->second.push_back(choiceID);
      choiceOption_[choiceID] = optionID;
      choiceRequirement_[choiceID] = c[2].toUInt();
    }

  // The requirement rows these options and choices use, with their prerequisite choices.
  std::set<uint> requirementIDs;
  for (const auto & r : optionRequirement_)
    if (r.second != 0)
      requirementIDs.insert(r.second);
  for (const auto & r : choiceRequirement_)
    if (r.second != 0)
      requirementIDs.insert(r.second);
  if (!requirementIDs.empty())
  {
    auto reqs = GAMEDATABASE.sqlQuery(QString(
      "SELECT ID, ReqType, ClassMask, RegionGroupMask, ReqAchievementID, ReqQuestID, OverrideArchive, "
      "ReqItemModifiedAppearanceID, RaceMasks1, RaceMasks2 FROM ChrCustomizationReq WHERE ID IN (%1)").arg(idList(requirementIDs)));
    if (reqs.valid)
      for (const auto & r : reqs.values)
      {
        Requirement req;
        req.id = r[0].toUInt();
        req.reqType = r[1].toInt();
        req.classMask = r[2].toInt();
        req.regionGroupMask = r[3].toInt();
        req.unlockGated = (r[4].toLongLong() != 0) || (r[5].toLongLong() != 0) || (r[7].toLongLong() != 0);
        req.overrideArchive = r[6].toInt();
        req.raceMask = ((r[9].toULongLong() & 0xFFFFFFFFull) << 32) | (r[8].toULongLong() & 0xFFFFFFFFull);
        requirements_[req.id] = req;
      }

    auto prerequisites = GAMEDATABASE.sqlQuery(QString(
      "SELECT ChrCustomizationReqID, ChrCustomizationChoiceID FROM ChrCustomizationReqChoice "
      "WHERE ChrCustomizationReqID IN (%1) ORDER BY ID").arg(idList(requirementIDs)));
    if (prerequisites.valid)
      for (const auto & p : prerequisites.values)
      {
        const auto reqIt = requirements_.find(p[0].toUInt());
        if (reqIt != requirements_.end())
          reqIt->second.prerequisiteChoices.push_back(p[1].toUInt());
      }
  }

  auto race = GAMEDATABASE.sqlQuery(QString("SELECT PlayableRaceBit FROM ChrRaces WHERE ID = %1").arg(infos.raceID));
  if (race.valid && !race.values.empty())
    playableRaceBit_ = race.values[0][0].toInt();

  // Dependencies: an option depends on another when its own requirement, or one of its choices',
  // names choices of that other option (Skin Color on Skin Type, Face Features on Jaw Features,
  // Eyesight on Eye Color).
  std::map<uint, std::set<uint> > prerequisiteOptions; // option -> options it depends on
  for (const uint optionID : optionClientOrder_)
  {
    std::vector<uint> reqIDs;
    reqIDs.push_back(optionRequirement_.at(optionID));
    for (const uint choiceID : choicesPerOptionMap_.at(optionID))
      reqIDs.push_back(choiceRequirement_.at(choiceID));
    for (const uint reqID : reqIDs)
    {
      const auto reqIt = requirements_.find(reqID);
      if (reqIt == requirements_.end())
        continue;
      for (const uint prerequisite : reqIt->second.prerequisiteChoices)
      {
        const auto optIt = choiceOption_.find(prerequisite);
        if (optIt != choiceOption_.end() && optIt->second != optionID)
        {
          prerequisiteOptions[optionID].insert(optIt->second);
          dependentOptions_[optIt->second].insert(optionID);
        }
      }
    }
  }

  // Resolution order: client order, except that an option comes after the options it depends on.
  std::set<uint> placed;
  std::vector<uint> pending = optionClientOrder_;
  while (!pending.empty())
  {
    auto next = std::find_if(pending.begin(), pending.end(), [&](uint optionID) {
      const auto it = prerequisiteOptions.find(optionID);
      if (it != prerequisiteOptions.end())
        for (const uint p : it->second)
          if (placed.count(p) == 0)
            return false;
      return true;
    });
    if (next == pending.end())
    {
      LOG_WARNING << __FUNCTION__ << "customization option requirements depend on each other in a cycle; resolving"
                  << (int)pending.size() << "options in client order";
      optionResolveOrder_.insert(optionResolveOrder_.end(), pending.begin(), pending.end());
      break;
    }
    optionResolveOrder_.push_back(*next);
    placed.insert(*next);
    pending.erase(next);
  }

  LOG_INFO << __FUNCTION__ << "ChrModel" << infos.ChrModelID[0] << "race" << infos.raceID << "PlayableRaceBit" << playableRaceBit_
           << ":" << (int)optionClientOrder_.size() << "options," << (int)choiceOption_.size() << "choices,"
           << (int)requirements_.size() << "requirements," << (int)dependentOptions_.size() << "options other options depend on";
}

// ReqType is a bit field. Bit 0 marks a requirement a player can select; without it the row serves
// NPC displays only. Evidence (12.1.0 data): ReqType 1 (bit 0 only) gates 94 choices and not one
// of them is used by a creature display; ReqType 2 (bit 1 only) gates 1297 choices that include
// the eye-colour templates creature displays use, and it is the Requirement of the per-model
// "Eye Style" options (e.g. option 8530 on the Undead male), which the appearance reference for the
// Undead male does not list, while it lists every choice of Req 141 and 55 (ReqType 3): Jaw
// Features 11, Skin Type 3 with Bony. ReqType 4 is only Req 10, the "Transmog" placeholder choice.
// The Dracthyr's own Eye Style (option 1584) carries ReqType 3 choices and stays. (The column note
// in bin_support/dbd reads bit 0 as "class required"; that reading would offer the Eye Style of Req
// 12 and never the Eyesight option of Req 4103 -- ReqType 3 with ClassMask 0 -- so it does not fit.)
bool CharDetails::isPlayerRequirement(int reqType)
{
  return (reqType & 1) != 0;
}

// RaceMasks is indexed by ChrRaces.PlayableRaceBit, not by the race ID: the faction masks of Req
// 4603 (0x55155555B1354C4D) and 4509 (0xAA2AAAAA4E0AB3B2) are exactly the alliance and horde
// races under PlayableRaceBit (Dark Iron Dwarf 34 is bit 11, Mag'har Orc 36 bit 13) and nonsense
// under ID - 1, and the druid-form masks 0x180000 name bits 19 and 20, the two Haranir rows. The
// all-races mask sets every bit, so it passes by the same test; a race without a playable bit
// (-1, e.g. Fel Orc) is inside a mask only when every bit is set.
bool CharDetails::raceMaskAllows(unsigned long long raceMask, int playableRaceBit)
{
  if (playableRaceBit >= 0 && playableRaceBit < 64)
    return ((raceMask >> playableRaceBit) & 1ull) != 0;
  return raceMask == ~0ull;
}

// ClassMask names class N with bit N - 1 (the Undead Eye Color masks split cleanly: Req 146 0x7FDF
// + Req 142 0x20 = 0x7FFF, Death Knight being class 6). 0 restricts nothing: Req 4103, the
// Requirement of the Eyesight option, has ClassMask 0 on a player requirement and the option is
// offered. The viewer's class context is the Demon Hunter checkbox or, without it, the ordinary
// classes (see CLASS_BITS_ORDINARY): a Demon Hunter passes a mask that names class 12; an ordinary
// character passes only a mask that names every ordinary class, so a choice limited to some classes
// (Death Knight only, Demon Hunter only, a druid form) is not offered as if every class had it.
bool CharDetails::classMaskAllows(int classMask, bool demonHunter)
{
  if (classMask == 0)
    return true;
  const unsigned int mask = static_cast<unsigned int>(classMask);
  if (demonHunter)
    return (mask & classBit(CLASS_DEMONHUNTER)) != 0;
  return (mask & CLASS_BITS_ORDINARY) == CLASS_BITS_ORDINARY;
}

const CharDetails::Requirement * CharDetails::requirement(uint requirementID) const
{
  const auto it = requirements_.find(requirementID);
  return (it != requirements_.end()) ? &it->second : nullptr;
}

void CharDetails::logUnresolvedRequirement(uint requirementID, const QString & why) const
{
  if (unresolvedRequirementsLogged_.insert(requirementID).second)
    LOG_WARNING << "Customization requirement" << requirementID << "unresolved:" << why;
}

CharDetails::RequirementResult CharDetails::evaluateRequirement(uint requirementID, const std::map<uint, uint> & selection) const
{
  if (requirementID == 0)
    return REQUIREMENT_MET;

  const auto it = requirements_.find(requirementID);
  if (it == requirements_.end())
  {
    logUnresolvedRequirement(requirementID, "no ChrCustomizationReq row was loaded for it, so it is not evaluated");
    return REQUIREMENT_MET;
  }
  const Requirement & req = it->second;

  if (!isPlayerRequirement(req.reqType))
    return REQUIREMENT_NOT_PLAYER;

  if (!raceMaskAllows(req.raceMask, playableRaceBit_))
    return REQUIREMENT_RACE;

  if (!classMaskAllows(req.classMask, isDemonHunter_))
    return REQUIREMENT_CLASS;

  // Achievement, quest and item-appearance gates: a collectible look the account has to earn. The
  // in-game appearance editor hides those by default and the viewer cannot evaluate the condition,
  // so they stay hidden. OverrideArchive and RegionGroupMask are loaded but not evaluated: no rule
  // for them is established. OverrideArchive is 0 only on Req 55, 61 and 66 -- the Undead jaws, the
  // Bony skin type and its skin colours, all of which the appearance reference for the Undead lists --
  // so reading 0 as a restriction would take the base Undead look away.
  if (req.unlockGated)
    return REQUIREMENT_UNLOCK;

  // Prerequisite choices (ChrCustomizationReqChoice). When they all belong to one option the list
  // is "any of": one option holds one choice at a time, so the Undead Face Features "Rotting" pair
  // (Req 58 and 59) partition the 11 jaws between them. A list spanning several options has no
  // established combination; it is logged and not evaluated (the requirement's other gates still
  // apply), which is what the viewer did before it read the table.
  if (!req.prerequisiteChoices.empty())
  {
    uint prerequisiteOption = 0;
    bool resolvable = true;
    for (const uint choiceID : req.prerequisiteChoices)
    {
      const auto optIt = choiceOption_.find(choiceID);
      if (optIt == choiceOption_.end())
      {
        logUnresolvedRequirement(requirementID, QString("prerequisite choice %1 is not a choice of this model; the list is not evaluated").arg(choiceID));
        resolvable = false;
        break;
      }
      if (prerequisiteOption != 0 && optIt->second != prerequisiteOption)
      {
        logUnresolvedRequirement(requirementID, QString("its prerequisite choices span options %1 and %2; how such a list combines is not established, so it is not evaluated")
                                                  .arg(prerequisiteOption).arg(optIt->second));
        resolvable = false;
        break;
      }
      prerequisiteOption = optIt->second;
    }

    if (resolvable)
    {
      const auto current = selection.find(prerequisiteOption);
      if (current == selection.end() ||
          std::find(req.prerequisiteChoices.begin(), req.prerequisiteChoices.end(), current->second) == req.prerequisiteChoices.end())
        return REQUIREMENT_PREREQUISITE;
    }
  }

  return REQUIREMENT_MET;
}

std::vector<uint> CharDetails::validChoices(uint chrCustomizationOptionID, const std::map<uint, uint> & selection) const
{
  std::vector<uint> valid;
  const auto listIt = choicesPerOptionMap_.find(chrCustomizationOptionID);
  if (listIt == choicesPerOptionMap_.end())
    return valid;

  const auto optionReq = optionRequirement_.find(chrCustomizationOptionID);
  if (optionReq != optionRequirement_.end() && evaluateRequirement(optionReq->second, selection) != REQUIREMENT_MET)
    return valid;

  for (const uint choiceID : listIt->second)
  {
    const auto choiceReq = choiceRequirement_.find(choiceID);
    if (evaluateRequirement(choiceReq != choiceRequirement_.end() ? choiceReq->second : 0, selection) == REQUIREMENT_MET)
      valid.push_back(choiceID);
  }
  return valid;
}

std::set<uint> CharDetails::dependentOptions(uint chrCustomizationOptionID) const
{
  std::set<uint> result;
  std::vector<uint> pending(1, chrCustomizationOptionID);
  while (!pending.empty())
  {
    const uint optionID = pending.back();
    pending.pop_back();
    const auto it = dependentOptions_.find(optionID);
    if (it == dependentOptions_.end())
      continue;
    for (const uint dependent : it->second)
      if (dependent != chrCustomizationOptionID && result.insert(dependent).second)
        pending.push_back(dependent);
  }
  return result;
}

void CharDetails::resolveSelection(const std::set<uint> * scope)
{
  for (const uint optionID : optionResolveOrder_)
  {
    if (scope && scope->count(optionID) == 0)
      continue;

    const std::vector<uint> valid = validChoices(optionID, currentCustomization_);
    const auto current = currentCustomization_.find(optionID);
    if (valid.empty())
    {
      if (current != currentCustomization_.end())
      {
        LOG_INFO << __FUNCTION__ << "option" << optionID << "has no valid choice now; choice" << current->second << "dropped";
        currentCustomization_.erase(current);
      }
      continue;
    }

    if (current != currentCustomization_.end() && std::find(valid.begin(), valid.end(), current->second) != valid.end())
      continue;

    if (current != currentCustomization_.end())
      LOG_INFO << __FUNCTION__ << "option" << optionID << "choice" << current->second << "is no longer valid; first valid choice" << valid.front() << "taken";
    currentCustomization_[optionID] = valid.front();
  }
}

CharDetails::SelectionState CharDetails::captureSelectionState() const
{
  SelectionState state;
  state.selection = currentCustomization_;
  for (const uint optionID : optionClientOrder_)
    state.choices[optionID] = validChoices(optionID, currentCustomization_);
  return state;
}

void CharDetails::applySelection(const SelectionState & before, uint changedOption)
{
  rebuildCustomizationElements();

  const SelectionState after = captureSelectionState();

  // Tell observers inside a batch: CharControl refreshes the model on every CHOICE_LIST_CHANGED
  // outside one, and this change gets exactly one refresh, below.
  const bool wasBatching = batchUpdate_;
  batchUpdate_ = true;

  bool optionListChanged = false;
  for (const uint optionID : optionClientOrder_)
  {
    const auto b = before.choices.find(optionID);
    const bool availableBefore = (b != before.choices.end()) && !b->second.empty();
    if (availableBefore != !after.choices.at(optionID).empty())
      optionListChanged = true;
  }
  if (optionListChanged)
  {
    CharDetailsEvent event(this, CharDetailsEvent::OPTION_LIST_CHANGED);
    notify(event);
  }

  for (const uint optionID : optionClientOrder_)
  {
    const auto choicesBefore = before.choices.find(optionID);
    const auto selectedBefore = before.selection.find(optionID);
    const auto selectedAfter = after.selection.find(optionID);
    const uint choiceBefore = (selectedBefore != before.selection.end()) ? selectedBefore->second : 0;
    const uint choiceAfter = (selectedAfter != after.selection.end()) ? selectedAfter->second : 0;
    if (optionID == changedOption || choiceBefore != choiceAfter || choicesBefore == before.choices.end() ||
        choicesBefore->second != after.choices.at(optionID))
    {
      CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
      event.setCustomizationOptionId(optionID);
      notify(event);
    }
  }

  batchUpdate_ = wasBatching;

  if (!batchUpdate_ && model_)
    model_->refresh();
}

bool CharDetails::hasOption(uint chrCustomizationOptionID) const
{
  return choicesPerOptionMap_.find(chrCustomizationOptionID) != choicesPerOptionMap_.end();
}

void CharDetails::set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID) // wow version >= 9.x
{
  if (!model_ || model_->infos.raceID == -1)
    return;

  // Only a choice of this option on this model is recorded: no entry is created for an option or a
  // choice the model does not have (an old saved character's "8530 value 0", a pre-9.x index).
  const auto listIt = choicesPerOptionMap_.find(chrCustomizationOptionID);
  if (listIt == choicesPerOptionMap_.end() ||
      std::find(listIt->second.begin(), listIt->second.end(), chrCustomizationChoiceID) == listIt->second.end())
  {
    LOG_WARNING << __FUNCTION__ << "choice" << chrCustomizationChoiceID << "is not a choice of option" << chrCustomizationOptionID
                << "on this model -- ignored";
    return;
  }

  if (batchUpdate_) // load(): resolved and applied together once everything is read
  {
    currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;
    return;
  }

  const auto optionReq = optionRequirement_.find(chrCustomizationOptionID);
  if (optionReq != optionRequirement_.end() && evaluateRequirement(optionReq->second, currentCustomization_) != REQUIREMENT_MET)
  {
    LOG_WARNING << __FUNCTION__ << "option" << chrCustomizationOptionID << "is not available with the current choices -- choice"
                << chrCustomizationChoiceID << "ignored";
    return;
  }

  if (get(chrCustomizationOptionID) == chrCustomizationChoiceID)
    return;

  LOG_INFO << __FUNCTION__ << chrCustomizationOptionID << chrCustomizationChoiceID;
  if (!isChoiceAvailable(chrCustomizationChoiceID))
    LOG_WARNING << __FUNCTION__ << "choice" << chrCustomizationChoiceID << "is not valid with the current choices; set as requested";

  // Record the choice as requested, then re-validate only the options whose requirements depend on
  // this one (Skin Color on Skin Type, Face Features on Jaw Features, Eyesight on Eye Color): each
  // keeps its current choice while it stays valid and otherwise takes its first valid choice. What
  // the character wears is then rebuilt from the resulting choices and refreshed once.
  const SelectionState before = captureSelectionState();
  currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;
  const std::set<uint> dependents = dependentOptions(chrCustomizationOptionID);
  resolveSelection(&dependents);

  // If this choice adds a skinned model whose texture is gated by another option (e.g. a DH
  // blindfold needs a DH eye-glow colour), make sure that option holds a compatible value,
  // otherwise the model merges untextured and renders white.
  autoSelectTextureGating(chrCustomizationChoiceID);

  applySelection(before, chrCustomizationOptionID);
}

void CharDetails::autoSelectTextureGating(uint chrCustomizationChoiceID)
{
  if (!model_)
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

  for (auto & g : gates)
  {
    const uint optID = g.first;
    if (choicesPerOptionMap_.count(optID) == 0)
      continue; // gating option not present on this model
    const uint cur = get(optID);
    if (std::find(g.second.begin(), g.second.end(), cur) != g.second.end())
      continue; // already a compatible value, nothing to do
    // Prefer a compatible choice that is valid for this character.
    uint pick = g.second.front();
    const std::vector<uint> valid = validChoices(optID, currentCustomization_);
    for (const uint c : g.second)
      if (std::find(valid.begin(), valid.end(), c) != valid.end())
      {
        pick = c;
        break;
      }
    LOG_INFO << "autoSelectTextureGating: choice" << chrCustomizationChoiceID
             << "needs option" << optID << "-> switching it to compatible choice" << pick;
    currentCustomization_[optID] = pick;
    const std::set<uint> dependents = dependentOptions(optID);
    resolveSelection(&dependents);
  }
}

std::vector<uint> CharDetails::getCustomizationChoices(const uint chrCustomizationOptionID) const
{
  // Only the choices valid for this character with its current choices -- race, class context,
  // player requirement, unlock gates and prerequisite choices (see evaluateRequirement) -- so the
  // dropdown hides e.g. Demon-Hunter-only choices on a non-DH and a Skin Color of another Skin Type.
  return validChoices(chrCustomizationOptionID, currentCustomization_);
}

std::vector<uint> CharDetails::getCustomizationOptions() const
{
  std::vector<uint> options;
  for (const uint optionID : optionClientOrder_)
    if (!validChoices(optionID, currentCustomization_).empty())
      options.push_back(optionID);
  return options;
}

bool CharDetails::isOptionAvailable(uint chrCustomizationOptionID) const
{
  return !validChoices(chrCustomizationOptionID, currentCustomization_).empty();
}

bool CharDetails::isChoiceAvailable(uint chrCustomizationChoiceID) const
{
  const auto optIt = choiceOption_.find(chrCustomizationChoiceID);
  if (optIt == choiceOption_.end())
    return false;
  const std::vector<uint> valid = validChoices(optIt->second, currentCustomization_);
  return std::find(valid.begin(), valid.end(), chrCustomizationChoiceID) != valid.end();
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

const std::vector<CharDetails::ChoiceElementRow> & CharDetails::getChoiceElementRows(uint chrCustomizationChoiceID)
{
  const auto it = choiceElementRows_.find(chrCustomizationChoiceID);
  if (it != choiceElementRows_.end())
    return it->second;

  std::vector<ChoiceElementRow> rows;
  auto elements = GAMEDATABASE.sqlQuery(QString("SELECT ID, RelatedChrCustomizationChoiceID, ChrCustomizationGeosetID, ChrCustomizationSkinnedModelID, "
                                                "ChrCustomizationMaterialID, ChrCustomizationBoneSetID, ChrCustomizationCondModelID, ChrCustomizationDisplayInfoID "
                                                "FROM ChrCustomizationElement WHERE ChrCustomizationChoiceID = %1 ORDER BY ID").arg(chrCustomizationChoiceID));
  if (elements.valid)
    for (const auto & e : elements.values)
    {
      ChoiceElementRow row;
      row.id = e[0].toUInt();
      row.related = e[1].toUInt();
      row.geosetID = e[2].toUInt();
      row.skinnedModelID = e[3].toUInt();
      row.materialID = e[4].toUInt();
      row.boneSetID = e[5].toUInt();
      row.condModelID = e[6].toUInt();
      row.displayInfoID = e[7].toUInt();
      rows.push_back(row);
    }

  return choiceElementRows_.emplace(chrCustomizationChoiceID, std::move(rows)).first->second;
}

const CharDetails::CustomizationElements & CharDetails::resolveElement(const ChoiceElementRow & row)
{
  const auto it = resolvedElements_.find(row.id);
  if (it != resolvedElements_.end())
    return it->second;

  // Only the first non-zero column is honoured; no element row of the client data sets more than one.
  CustomizationElements effect;
  if (row.geosetID != 0) // geoset customization
  {
    auto vals = GAMEDATABASE.sqlQuery(QString("SELECT GeosetType, GeosetID FROM ChrCustomizationGeoset WHERE ID = %1").arg(row.geosetID));
    if (vals.valid)
      for (const auto & geo : vals.values)
        effect.geosets.emplace_back(geo[0].toUInt(), geo[1].toUInt());
  }
  else if (row.skinnedModelID != 0) // added model customization
  {
    auto vals = GAMEDATABASE.sqlQuery(QString("SELECT CollectionsFileDataID, GeosetType, GeosetID FROM ChrCustomizationSkinnedModel WHERE ID = %1").arg(row.skinnedModelID));
    if (vals.valid && !vals.values.empty())
      effect.models.emplace_back(vals.values[0][0].toInt(), std::make_pair(vals.values[0][1].toInt(), vals.values[0][2].toInt()));
  }
  else if (row.materialID != 0) // texture customization
  {
    auto vals = GAMEDATABASE.sqlQuery(QString("SELECT ChrModelTextureLayer.Layer, ChrModelTextureLayer.TextureSectionTypeBitMask, ChrModelTextureLayer.TextureType, ChrModelTextureLayer.BlendMode, FileDataID FROM ChrCustomizationMaterial "
      "LEFT JOIN TextureFileData ON ChrCustomizationMaterial.MaterialResourcesID = TextureFileData.MaterialResourcesID "
      "LEFT JOIN ChrModelTextureLayer ON ChrCustomizationMaterial.ChrModelTextureTargetID = ChrModelTextureLayer.ChrModelTextureTargetID1 "
      "AND ChrModelTextureLayer.CharComponentTextureLayoutsID = %1 "
      "WHERE ChrCustomizationMaterial.ID = %2").arg(model_->infos.textureLayoutID).arg(row.materialID));

    if (vals.valid && !vals.values.empty())
    {
      TextureCustomization t{};
      t.layer = vals.values[0][0].toUInt();
      t.region = bitMaskToSectionType(vals.values[0][1].toInt());
      t.type = vals.values[0][2].toUInt();
      t.blendMode = vals.values[0][3].toUInt();
      t.fileId = vals.values[0][4].toUInt();

      LOG_INFO << "ChrCustomizationElement" << row.id << "texture ->" << "layer" << t.layer << "region" << t.region << "type" << t.type
               << "blendMode" << t.blendMode << "fileId" << t.fileId;

      effect.textures.push_back(t);
    }
  }
  else if (row.boneSetID != 0) // boneset customization ??
  {
    LOG_ERROR << "Not yet implemented ! boneset based customization for" << row.id << "/" << row.boneSetID;
  }
  else if (row.condModelID != 0) // cond model customization ??
  {
    LOG_ERROR << "Not yet implemented ! Cond model based customization for" << row.id << "/" << row.condModelID;
  }
  else if (row.displayInfoID != 0) // display info customization ??
  {
    LOG_ERROR << "Not yet implemented ! Display info based customization for" << row.id << "/" << row.displayInfoID;
  }

  return resolvedElements_.emplace(row.id, std::move(effect)).first->second;
}

void CharDetails::rebuildCustomizationElements()
{
  // Rebuilt from scratch after every change, from the current choices only: each option holds the
  // elements of ITS current choice, and an element gated by a related choice
  // (RelatedChrCustomizationChoiceID: a Face texture per Skin Color, a scalp texture per Hair Color,
  // an eye geoset per Eyesight) applies while that choice is current. Nothing of an earlier choice
  // survives -- the old per-option bookkeeping filed gated elements under the other option and kept
  // them across changes -- and an option without a current choice contributes nothing.
  std::set<uint> activeChoices;
  for (const auto & c : currentCustomization_)
    activeChoices.insert(c.second);

  customizationElementsPerOption_.clear();
  if (!model_)
    return;

  for (const auto & c : currentCustomization_)
  {
    CustomizationElements & applied = customizationElementsPerOption_[c.first];
    for (const ChoiceElementRow & row : getChoiceElementRows(c.second))
    {
      if (row.related != 0 && activeChoices.count(row.related) == 0)
        continue;
      const CustomizationElements & effect = resolveElement(row);
      applied.geosets.insert(applied.geosets.end(), effect.geosets.begin(), effect.geosets.end());
      applied.textures.insert(applied.textures.end(), effect.textures.begin(), effect.textures.end());
      applied.models.insert(applied.models.end(), effect.models.begin(), effect.models.end());
    }
  }
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

  // Hair is geoset group 0 (CG_SKIN_OR_HAIR); the individual hairstyles are ids 1..99, resolved as
  // a customization choice by applyCustomizationGeosets(). The Show Hair toggle previously did
  // nothing -- showHair was never read anywhere. Forcing the group to variant 0 hides every
  // hairstyle; setGeosetGroupDisplay excludes id 0 (the base scalp/body), so this is a clean "bald"
  // rather than a hole in the head. Only when Show Hair is off -- otherwise leave the chosen style.
  if (!showHair)
    geosets[CG_SKIN_OR_HAIR] = 0;

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
  //
  // Groups in which an active customization choice already shows a BASE geoset element. A
  // skinned part in such a group COEXISTS with that base geometry (Dracthyr drake body armor
  // = an always-on base armor geoset PLUS a body-size-gated skinned detail; Earthen hair =
  // a base scalp geoset PLUS a skinned hair model), so its base group must NOT be hidden or
  // that geometry vanishes. Only when the skinned part is the SOLE geometry of its group (no
  // base geoset element -- Mechagnome arm/leg, DH horns) do we hide the body's default below.
  std::set<uint> groupsWithBaseGeoset;
  for (const auto& elt : customizationElementsPerOption_)
    for (const auto& geo : elt.second.geosets)
      groupsWithBaseGeoset.insert(geo.first);

  for (const auto& gf : groupsByFile)
  {
    auto * model = model_->getMergedModel(gf.first);
    if (!model)
      continue;
    model->hideAllGeosets();
    for (const auto& g : gf.second)
    {
      model->setGeosetGroupDisplay((CharGeosets)g.first, g.second);
      // The merged (skinned) part PROVIDES geoset group g.first. When the base body has no
      // customization geoset of its OWN in that group, HIDE the base's whole group (cd.geosets
      // [group] = 0, the "variant 0 / none" hide idiom) so the part REPLACES the bare body
      // instead of z-fighting it. Hiding the WHOLE group -- not switching it to the part's
      // variant -- is required because the part's variant can EQUAL the base default: the
      // Mechagnome leg upgrade is geoset 3001, the SAME id as the body's default leg, so
      // selecting that variant left the body's 3001 drawn under the mech leg and they
      // z-fought (the male flicker). The groupsWithBaseGeoset guard skips groups where a
      // customization geoset co-exists with the part (Dracthyr drake armor, Earthen hair),
      // which must stay visible.
      if (groupsWithBaseGeoset.count(g.first) == 0)
        geosets[g.first] = 0;
    }
  }
}
