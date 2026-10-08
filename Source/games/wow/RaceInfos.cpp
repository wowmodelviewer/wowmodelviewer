#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include <algorithm>

#define DEBUG_RACEINFOS 1

std::map<std::pair<int, int>, RaceInfos> RaceInfos::RACES;
std::map<int, RaceInfos> RaceInfos::RACES_BY_FILEID;

namespace
{
  // Collect a ChrModelID on an entry that already exists, without repeating one.
  void addChrModelID(RaceInfos & infos, int chrModelID)
  {
    if (std::find(infos.ChrModelID.begin(), infos.ChrModelID.end(), chrModelID) == infos.ChrModelID.end())
      infos.ChrModelID.push_back(chrModelID);
  }
}

void RaceInfos::init()
{
  // Loading a client again describes the races of that client only.
  RACES.clear();
  RACES_BY_FILEID.clear();

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