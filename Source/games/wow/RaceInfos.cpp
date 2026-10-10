#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include <algorithm>

#define DEBUG_RACEINFOS 1

std::map<std::pair<int, int>, RaceInfos> RaceInfos::RACES;
std::map<int, RaceInfos> RaceInfos::RACES_BY_FILEID;
std::map<int, RaceInfos::VariantPair> RaceInfos::VARIANT_PAIRS;
std::map<int, int> RaceInfos::VARIANT_PAIR_BY_CHRMODEL;
std::map<int, RaceInfos> RaceInfos::ALTERNATES_BY_FILEID;

namespace
{
  // Collect a ChrModelID on an entry that already exists, without repeating one.
  void addChrModelID(RaceInfos & infos, int chrModelID)
  {
    if (std::find(infos.ChrModelID.begin(), infos.ChrModelID.end(), chrModelID) == infos.ChrModelID.end())
      infos.ChrModelID.push_back(chrModelID);
  }
}

void RaceInfos::clear()
{
  RACES.clear();
  RACES_BY_FILEID.clear();
  VARIANT_PAIRS.clear();
  VARIANT_PAIR_BY_CHRMODEL.clear();
  ALTERNATES_BY_FILEID.clear();
}

void RaceInfos::init()
{
  // Loading a client again describes the races of that client only.
  clear();

  auto races =
    GAMEDATABASE.sqlQuery("SELECT ChrRaces.ClientPrefix, ChrRaces.ID, ChrRaces.Flags, ChrModel.Sex, CreatureModelData.FileDataID, ChrModel.CharComponentTextureLayoutID, "
                          "ChrRaces.MaleModelFallbackRaceID, ChrRaces.MaleModelFallbackSex, ChrRaces.MaleTextureFallbackRaceID, ChrRaces.MaleTextureFallbackSex, "
                          "ChrRaces.FemaleModelFallbackRaceID, ChrRaces.FemaleModelFallbackSex, ChrRaces.FemaleTextureFallbackRaceID, ChrRaces.FemaleTextureFallbackSex, "
                          "ChrRaceXChrModel.ChrModelID, ChrRaces.ClientFileString, ChrRaces.Name_lang "
                          "FROM ChrRaceXChrModel "
                          "LEFT JOIN ChrRaces ON ChrRaces.ID = ChrRaceXChrModel.ChrRacesID "
                          "LEFT JOIN ChrModel ON ChrModel.ID = ChrRaceXChrModel.ChrModelID "
                          "LEFT JOIN CreatureDisplayInfo ON CreatureDisplayInfo.ID = ChrModel.DisplayID "
                          "LEFT JOIN CreatureModelData ON CreatureModelData.ID = CreatureDisplayInfo.ModelID ");

  if (!races.valid || races.empty())
  {
    LOG_ERROR << "Unable to collect race information from game database";
    return;
  }

  for (auto& race : races.values)
  {
    RaceInfos infos;
    infos.prefix = race[0].toStdString();
    infos.raceID = race[1].toInt();
    // A model of a race this client has no ChrRaces row for (its race table not installed, as on a partly
    // downloaded Classic Era) is no character the viewer can name, file or dress.
    if (infos.raceID <= 0)
      continue;
    infos.barefeet = (race[2].toInt() & 0x2);
    infos.sexID = race[3].toInt();
    auto modelfileid = race[4].toInt();
    infos.textureLayoutID = race[5].toInt();

    // Get fallback display race ID (this is mostly for allied races and others that rely on
    // item display info from other race models):
    if (infos.sexID == GENDER_MALE)
    {
      infos.modelFallbackRaceID = race[6].toInt();
      infos.modelFallbackSexID = race[7].toInt();
      infos.textureFallbackRaceID = race[8].toInt();
      infos.textureFallbackSexID = race[9].toInt();
    }
    else
    {
      infos.modelFallbackRaceID = race[10].toInt();
      infos.modelFallbackSexID = race[11].toInt();
      infos.textureFallbackRaceID = race[12].toInt();
      infos.textureFallbackSexID = race[13].toInt();
    }

    infos.ChrModelID.push_back(race[14].toInt());

    // lowercased race directory name (matches the character/<race>/<sex>/ path)
    if (race.size() > 15)
      infos.clientFileString = race[15].toLower().toStdString();

    // display name + playable/NPC classification (rule: Flags & 1 means
    // NPC-only, except race 23 (Pandaren) and 75 which are forced playable)
    if (race.size() > 16)
      infos.nameLang = race[16].toStdString();
    infos.isNPC = ((race[2].toInt() & 1) != 0) && infos.raceID != 23 && infos.raceID != 75;

    // modelfileid comes from a CreatureDisplayInfo -> CreatureModelData join. If those tables
    // didn't populate cleanly (e.g. a DB2 layout mismatch on an unexpected client build), the id
    // can be 0/invalid and getFile() returns null -- dereferencing it here crashed the whole app
    // on startup. Skip such races instead.
    GameFile * modelFile = GAMEDIRECTORY.getFile(modelfileid);
    if (!modelFile)
      continue;
    infos.isHD = modelFile->fullname().contains("_hd") ? true : false;
    infos.modelFileID = modelfileid;

    const int chrModelID = race[14].toInt();

    // One entry per race and sex. A race with several models (an HD one beside the old one,
    // or a second form) keeps the HD model and collects the other's ChrModelID.
    const auto key = std::make_pair(infos.raceID, infos.sexID);
    const auto existing = RACES.find(key);
    if (existing == RACES.end())
    {
      RACES[key] = infos;
    }
    else if (infos.isHD && !existing->second.isHD)
    {
      const std::vector<int> collected = existing->second.ChrModelID;
      existing->second = infos;
      for (const auto id : collected)
        addChrModelID(existing->second, id);
    }
    else
    {
      addChrModelID(existing->second, chrModelID);
    }

    // Keyed by model file: the first race on a file wins, and the races that share it only
    // add their ChrModelIDs, so a model loaded by file id still resolves to one race and to
    // every customization set that model can wear.
    const auto byFile = RACES_BY_FILEID.find(modelfileid);
    if (byFile == RACES_BY_FILEID.end())
      RACES_BY_FILEID[modelfileid] = infos;
    else
      addChrModelID(byFile->second, chrModelID);
  }

  initVariants();

#if DEBUG_RACEINFOS > 0
  for (const auto & r : RACES)
  {
    LOG_INFO << "---------------------------";
    LOG_INFO << "modelfileid ->" << r.second.modelFileID;
    LOG_INFO << "infos.prefix =" << r.second.prefix.c_str();
    LOG_INFO << "infos.textureLayoutID =" << r.second.textureLayoutID;
    LOG_INFO << "infos.raceID =" << r.second.raceID;
    LOG_INFO << "infos.sexID =" << r.second.sexID;
    LOG_INFO << "infos.isHD =" << r.second.isHD;
    LOG_INFO << "infos.modelFallbackRaceID =" << r.second.modelFallbackRaceID;
    LOG_INFO << "infos.modelFallbackSexID =" << r.second.modelFallbackSexID;
    LOG_INFO << "infos.textureFallbackRaceID =" << r.second.textureFallbackRaceID;
    LOG_INFO << "infos.textureFallbackSexID =" << r.second.textureFallbackSexID;
    for(const auto & it : r.second.ChrModelID)
      LOG_INFO << "infos.ChrModelID ->" << it;
    LOG_INFO << "---------------------------";
  }
#endif
}

void RaceInfos::initVariants()
{
  sqlResult pairs = GAMEDATABASE.sqlQuery(
    "SELECT ChrModelAltVariant.ID, ChrModelAltVariant.PrimaryChrModelID, ChrModelAltVariant.AlternateChrModelID, ChrModel.ID, ChrModel.Sex, "
    "ChrModel.CharComponentTextureLayoutID, CreatureModelData.FileDataID FROM ChrModelAltVariant "
    "LEFT JOIN ChrModel ON ChrModel.ID = ChrModelAltVariant.AlternateChrModelID "
    "LEFT JOIN CreatureDisplayInfo ON CreatureDisplayInfo.ID = ChrModel.DisplayID "
    "LEFT JOIN CreatureModelData ON CreatureModelData.ID = CreatureDisplayInfo.ModelID ORDER BY ChrModelAltVariant.ID");
  if (!pairs.valid || pairs.values.empty())
  {
    LOG_INFO << "[variants] none in this client";
    return;
  }

  // Every row is checked against what it names before it is used: the columns of these tables were identified from
  // one client's data, and a client of another layout would be read at the same positions.
  int droppedPairs = 0;
  for (const auto & row : pairs.values)
  {
    VariantPair pair;
    pair.id = row[0].toInt();
    pair.primaryChrModelID = row[1].toInt();
    pair.alternateChrModelID = row[2].toInt();
    pair.alternateFileID = row[6].toInt();
    const RaceInfos * primary = nullptr;
    int primaryRaces = 0;
    for (const auto & race : RACES)
      if (!race.second.ChrModelID.empty() && race.second.ChrModelID[0] == pair.primaryChrModelID)
      {
        if (!primary)
          primary = &race.second;
        primaryRaces++;
      }
    QString why;
    if (!primary)
      why = "its primary ChrModel is no race's model";
    else if (primaryRaces > 1)
      why = "its primary ChrModel is the model of more than one race";   // a switch could not keep the race
    else if (row[3].toInt() != pair.alternateChrModelID)
      why = "its alternate ChrModel is not in ChrModel";
    else if (row[4].toInt() != primary->sexID)
      why = "its alternate ChrModel is of another sex";
    else if (pair.alternateFileID <= 0)
      why = "its alternate ChrModel has no model file";
    else if (RACES_BY_FILEID.count(pair.alternateFileID) != 0)
      why = "its alternate model is a playable model";
    else if (ALTERNATES_BY_FILEID.count(pair.alternateFileID) != 0)
      why = "its alternate model is another pair's";
    else if (VARIANT_PAIR_BY_CHRMODEL.count(pair.primaryChrModelID) != 0 || VARIANT_PAIR_BY_CHRMODEL.count(pair.alternateChrModelID) != 0)
      why = "one of its ChrModels is already paired";
    if (!why.isEmpty())
    {
      LOG_WARNING << "[variants] pair" << pair.id << "(" << pair.primaryChrModelID << "->" << pair.alternateChrModelID << ") left out:" << why;
      droppedPairs++;
      continue;
    }
    pair.raceID = primary->raceID;
    pair.sexID = primary->sexID;
    pair.primaryFileID = primary->modelFileID;

    // The alternate's row is the race's, on the alternate's ChrModel, model file and texture layout. A file the client
    // does not list keeps its pair: the character panel then says it is not in this client.
    RaceInfos alternate = *primary;
    alternate.ChrModelID = { pair.alternateChrModelID };
    alternate.modelFileID = pair.alternateFileID;
    alternate.textureLayoutID = row[5].toInt();
    GameFile * file = GAMEDIRECTORY.getFile(pair.alternateFileID);
    alternate.isHD = file && file->fullname().contains("_hd");
    ALTERNATES_BY_FILEID[pair.alternateFileID] = alternate;

    VARIANT_PAIRS[pair.id] = pair;
    VARIANT_PAIR_BY_CHRMODEL[pair.primaryChrModelID] = pair.id;
    VARIANT_PAIR_BY_CHRMODEL[pair.alternateChrModelID] = pair.id;
  }

  // Option pairs: each an option of the pair's primary model and one of its alternate model, one to one.
  struct OptionRow { int pairID; unsigned int primary; unsigned int alternate; };
  std::map<int, OptionRow> optionRows;
  int droppedOptions = 0;
  sqlResult options = GAMEDATABASE.sqlQuery(
    "SELECT o.ID, o.ChrModelAltVariantID, o.PrimaryOptionID, o.AlternateOptionID, po.ChrModelID, ao.ChrModelID "
    "FROM ChrModelAltVariantOption o LEFT JOIN ChrCustomizationOption po ON po.ID = o.PrimaryOptionID "
    "LEFT JOIN ChrCustomizationOption ao ON ao.ID = o.AlternateOptionID ORDER BY o.ID");
  for (size_t i = 0; options.valid && i < options.values.size(); i++)
  {
    const auto & row = options.values[i];
    const auto pair = VARIANT_PAIRS.find(row[1].toInt());
    const unsigned int primary = row[2].toUInt(), alternate = row[3].toUInt();
    if (pair == VARIANT_PAIRS.end() || row[4].toInt() != pair->second.primaryChrModelID || row[5].toInt() != pair->second.alternateChrModelID ||
        pair->second.optionToAlternate.count(primary) != 0 || pair->second.optionToPrimary.count(alternate) != 0)
    {
      droppedOptions++;
      continue;
    }
    pair->second.optionToAlternate[primary] = alternate;
    pair->second.optionToPrimary[alternate] = primary;
    optionRows[row[0].toInt()] = OptionRow{ pair->first, primary, alternate };
  }

  // Choice pairs: each a choice of its option pair's primary option and one of its alternate option, one to one.
  int choicePairs = 0, droppedChoices = 0;
  sqlResult choices = GAMEDATABASE.sqlQuery(
    "SELECT c.ChrModelAltVariantOptionID, c.PrimaryChoiceID, c.AlternateChoiceID, pc.ChrCustomizationOptionID, ac.ChrCustomizationOptionID "
    "FROM ChrModelAltVariantChoice c LEFT JOIN ChrCustomizationChoice pc ON pc.ID = c.PrimaryChoiceID "
    "LEFT JOIN ChrCustomizationChoice ac ON ac.ID = c.AlternateChoiceID ORDER BY c.ID");
  for (size_t i = 0; choices.valid && i < choices.values.size(); i++)
  {
    const auto & row = choices.values[i];
    const auto option = optionRows.find(row[0].toInt());
    const unsigned int primary = row[1].toUInt(), alternate = row[2].toUInt();
    if (option == optionRows.end() || row[3].toUInt() != option->second.primary || row[4].toUInt() != option->second.alternate)
    {
      droppedChoices++;
      continue;
    }
    VariantPair & pair = VARIANT_PAIRS[option->second.pairID];
    if (pair.choiceToAlternate.count(primary) != 0 || pair.choiceToPrimary.count(alternate) != 0)
    {
      droppedChoices++;
      continue;
    }
    pair.choiceToAlternate[primary] = alternate;
    pair.choiceToPrimary[alternate] = primary;
    choicePairs++;
  }
  LOG_INFO << "[variants]" << VARIANT_PAIRS.size() << "pairs," << optionRows.size() << "option pairs," << choicePairs
           << "choice pairs; rows left out:" << droppedPairs << "pairs," << droppedOptions << "options," << droppedChoices << "choices";
}

bool RaceInfos::getVariantPair(int chrModelID, VariantPair & out)
{
  const auto id = VARIANT_PAIR_BY_CHRMODEL.find(chrModelID);
  if (id == VARIANT_PAIR_BY_CHRMODEL.end())
    return false;
  out = VARIANT_PAIRS[id->second];
  return true;
}

bool RaceInfos::getRaceInfosForAlternateFileID(int fileid, RaceInfos & out)
{
  const auto it = ALTERNATES_BY_FILEID.find(fileid);
  if (it == ALTERNATES_BY_FILEID.end())
    return false;
  out = it->second;
  return true;
}

bool RaceInfos::getVariantPartner(const RaceInfos & current, RaceInfos & partner)
{
  VariantPair pair;
  if (current.ChrModelID.empty() || !getVariantPair(current.ChrModelID[0], pair))
    return false;
  if (pair.isPrimary(current.ChrModelID[0]))
    return getRaceInfosForAlternateFileID(pair.alternateFileID, partner);
  return getRaceInfosForRaceSex(pair.raceID, pair.sexID, partner) && !partner.ChrModelID.empty() &&
         partner.ChrModelID[0] == pair.primaryChrModelID;
}

RaceInfos::Translation RaceInfos::translateSelection(const VariantPair & pair, const std::map<unsigned int, unsigned int> & from, bool toAlternate)
{
  const auto & options = toAlternate ? pair.optionToAlternate : pair.optionToPrimary;
  const auto & choices = toAlternate ? pair.choiceToAlternate : pair.choiceToPrimary;
  Translation t;
  for (const auto & oc : from)
  {
    const auto option = options.find(oc.first);
    if (option == options.end())
    {
      t.noCounterpart.push_back(oc.first);
      continue;
    }
    const auto choice = choices.find(oc.second);
    if (choice == choices.end())
    {
      t.notCovered.push_back(oc.first);
      continue;
    }
    t.selection[option->second] = choice->second;
    t.carried.emplace_back(oc.first, option->second);
  }
  return t;
}

int RaceInfos::getCreatureDisplayFileID(int fileDataId, int extraId)
{
  const int shown = getHDModelForFileID(fileDataId);
  RaceInfos alternate;
  VariantPair pair;
  if (extraId <= 0 || !getRaceInfosForAlternateFileID(shown, alternate) || !getVariantPair(alternate.ChrModelID[0], pair) ||
      !GAMEDIRECTORY.getFile(pair.primaryFileID))
    return shown;
  sqlResult owned = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM CreatureDisplayInfoOption JOIN ChrCustomizationOption "
                                                  "ON ChrCustomizationOption.ID = CreatureDisplayInfoOption.ChrCustomizationOptionID "
                                                  "WHERE CreatureDisplayInfoOption.CreatureDisplayInfoExtraID = %1 AND "
                                                  "ChrCustomizationOption.ChrModelID = %2").arg(extraId).arg(pair.primaryChrModelID));
  return (owned.valid && !owned.values.empty() && owned.values[0][0].toInt() > 0) ? pair.primaryFileID : shown;
}

int RaceInfos::getHDModelForFileID(int fileid)
{
  auto result = fileid; // return same file id by default

  const auto it = RACES_BY_FILEID.find(fileid);
  if (it != RACES_BY_FILEID.end() && !it->second.isHD)
  {
    // RACES holds the HD model for a race and sex when there is one.
    const auto hd = RACES.find(std::make_pair(it->second.raceID, it->second.sexID));
    if (hd != RACES.end() && hd->second.isHD)
      result = hd->second.modelFileID;
  }

  return result;
}

bool RaceInfos::getRaceInfosForFileID(int fileid, RaceInfos & infos)
{
  const auto raceInfosIt = RaceInfos::RACES_BY_FILEID.find(fileid);

  if (raceInfosIt != RaceInfos::RACES_BY_FILEID.end())
  {
    infos = raceInfosIt->second;
    return true;
  }

  return false;
}

bool RaceInfos::getRaceInfosForName(const std::string & raceName, int sex, RaceInfos & out)
{
  bool found = false;
  for (const auto & r : RACES)
  {
    if (r.second.sexID == sex && !r.second.clientFileString.empty() && r.second.clientFileString == raceName)
    {
      out = r.second;
      found = true;
      if (r.second.isHD) // prefer the HD model when several match
        break;
    }
  }
  return found;
}

int RaceInfos::getFileIDForRaceSex(const int & race, const int & sex)
{
  const auto it = RACES.find(std::make_pair(race, sex));

  return (it != RACES.end()) ? it->second.modelFileID : -1;
}

bool RaceInfos::getRaceInfosForRaceSex(int race, int sex, RaceInfos & out)
{
  const auto it = RACES.find(std::make_pair(race, sex));
  if (it == RACES.end())
    return false;

  out = it->second;
  return true;
}

bool RaceInfos::getRaceInfosForRaceAndFile(int race, int fileid, RaceInfos & out)
{
  for (const auto & r : RACES)
    if (r.first.first == race && r.second.modelFileID == fileid)
    {
      out = r.second;
      return true;
    }
  return false;
}

std::vector<RaceInfos::RaceMenuEntry> RaceInfos::getRaceMenu()
{
  // collapse the per-(race,sex,model) RACES map into one entry per race, keeping
  // the HD model FileDataID for each sex.
  std::map<int, RaceMenuEntry> byRace;
  for (const auto & kv : RACES)
  {
    const RaceInfos & r = kv.second;
    const int fileID = r.modelFileID;
    if (r.raceID < 0)
      continue;

    RaceMenuEntry & e = byRace[r.raceID];
    e.raceID = r.raceID;
    e.isNPC = r.isNPC;
    if (e.name.empty())
      e.name = !r.nameLang.empty() ? r.nameLang : r.clientFileString;

    if (r.sexID == GENDER_FEMALE)
    {
      if (e.femaleFileID < 0 || r.isHD)
        e.femaleFileID = fileID;
    }
    else // treat anything else as male (covers GENDER_MALE / GENDER_NONE)
    {
      if (e.maleFileID < 0 || r.isHD)
        e.maleFileID = fileID;
    }
  }

  std::vector<RaceMenuEntry> out;
  out.reserve(byRace.size());
  for (auto & kv : byRace)
    out.push_back(kv.second);

  std::sort(out.begin(), out.end(),
            [](const RaceMenuEntry & a, const RaceMenuEntry & b) { return a.name < b.name; });

  return out;
}