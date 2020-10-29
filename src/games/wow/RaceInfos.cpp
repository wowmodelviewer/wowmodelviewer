#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#define DEBUG_RACEINFOS 1

std::map<int, RaceInfos> RaceInfos::RACES;

// This mapping links *_sdr character models to their HD equivalents, so they can get race info for display.
// *_sdr models are actually now obsolete and the database that used to provide this info is now empty,
// but while the models are still appearing in the model tree we may as well keep them working, so they
// don't look broken:
std::map<int, int> RaceInfos::SDReplacementModel = // {SDFileID , HDFileID}
  {{1838568, 119369},  {1838570, 119376},  {1838201, 307453},  {1838592, 307454},  {1853956, 535052},
   {1853610, 589715},  {1838560, 878772},  {1838566, 900914},  {1838578, 917116},  {1838574, 921844},
   {1838564, 940356},  {1838580, 949470},  {1838562, 950080},  {1838584, 959310},  {1838586, 968705},
   {1838576, 974343},  {1839008, 986648},  {1838582, 997378},  {1838572, 1000764}, {1839253, 1005887},
   {1838385, 1011653}, {1838588, 1018060}, {1822372, 1022598}, {1838590, 1022938}, {1853408, 1100087},
   {1839709, 1100258}, {1825438, 1593999}, {1839042, 1620605}, {1858265, 1630218}, {1859379, 1630402},
   {1900779, 1630447}, {1894572, 1662187}, {1859345, 1733758}, {1858367, 1734034}, {1858099, 1810676},
   {1857801, 1814471}, {1892825, 1890763}, {1892543, 1890765}, {1968838, 1968587}, {1842700, 1000764}};
           
bool RaceInfos::getCurrent(WoWModel * model, RaceInfos & result)
{
  if (!model)
  {
    LOG_ERROR << __FUNCTION__ << "model is null";
    return false;
  }
  auto fdid = model->gamefile->fileDataId();
  if (SDReplacementModel.count(fdid)) // if it's an old *_sdr model, use the file ID of its HD counterpart for race info
    fdid = SDReplacementModel[fdid];
  const auto raceInfosIt = RACES.find(fdid);
  if(raceInfosIt != RACES.end())
  {
    result = raceInfosIt->second;
    return true;
  }

  LOG_ERROR << "Unable to retrieve race infos for model" << model->gamefile->fullname() << model->gamefile->fileDataId();
 
  return false;
}

void RaceInfos::init()
{
  if (GAMEDIRECTORY.majorVersion() < 9)
  {
    auto races =
      GAMEDATABASE.sqlQuery("SELECT CMDM.FileID as malemodel, lower(ClientPrefix), CharComponentTexLayoutID, "
        "CMDF.FileID AS femalemodel, lower(ClientPrefix), CharComponentTexLayoutID, "
        "CMDMHD.FileID as malemodelHD, lower(ClientPrefix), CharComponentTexLayoutHiResID, "
        "CMDFHD.FileID AS femalemodelHD, lower(ClientPrefix), CharComponentTexLayoutHiResID, "
        "ChrRaces.ID, BaseRaceID, Flags, MaleModelFallbackRaceID, MaleModelFallbackSex, "
        "FemaleModelFallbackRaceID, FemaleModelFallbackSex, MaleTextureFallbackRaceID, MaleTextureFallbackSex, "
        "FemaleTextureFallbackRaceID, FemaleTextureFallbackSex FROM ChrRaces "
        "LEFT JOIN CreatureDisplayInfo CDIM ON CDIM.ID = MaleDisplayID LEFT JOIN CreatureModelData CMDM ON CDIM.ModelID = CMDM.ID "
        "LEFT JOIN CreatureDisplayInfo CDIF ON CDIF.ID = FemaleDisplayID LEFT JOIN CreatureModelData CMDF ON CDIF.ModelID = CMDF.ID "
        "LEFT JOIN CreatureDisplayInfo CDIMHD ON CDIMHD.ID = HighResMaleDisplayId LEFT JOIN CreatureModelData CMDMHD ON CDIMHD.ModelID = CMDMHD.ID "
        "LEFT JOIN CreatureDisplayInfo CDIFHD ON CDIFHD.ID = HighResFemaleDisplayId LEFT JOIN CreatureModelData CMDFHD ON CDIFHD.ModelID = CMDFHD.ID");

    if (!races.valid || races.empty())
    {
      LOG_ERROR << "Unable to collect race information from game database";
      return;
    }

    for (auto& value : races.values)
    {
      for (auto r = 0; r < 12; r += 3)
      {
        if (value[r] != "")
        {
          RaceInfos infos;
          infos.prefix = value[r + 1].toStdString();
          infos.textureLayoutID = value[r + 2].toInt();
          infos.raceID = value[12].toInt();
          infos.sexID = (r == 0 || r == 6) ? GENDER_MALE : GENDER_FEMALE;
          infos.barefeet = (value[14].toInt() & 0x2);
          // Get fallback display race ID (this is mostly for allied races and others that rely on
          // item display info from other race models):
          if (infos.sexID == GENDER_MALE)
          {
            infos.modelFallbackRaceID = value[15].toInt();
            infos.modelFallbackSexID = value[16].toInt();
            infos.textureFallbackRaceID = value[19].toInt();
            infos.textureFallbackSexID = value[20].toInt();
          }
          else
          {
            infos.modelFallbackRaceID = value[17].toInt();
            infos.modelFallbackSexID = value[18].toInt();
            infos.textureFallbackRaceID = value[21].toInt();
            infos.textureFallbackSexID = value[22].toInt();
          }

          auto modelfileid = value[r].toInt();

          if ((r == 6) || (r == 9)) // if we are dealing with a HD model
            infos.isHD = true;
          else
            infos.isHD = false;

          if (RACES.find(modelfileid) == RACES.end())
            RACES[modelfileid] = infos;

          // cheap workaround to include the upright male orc model, because I
          // can't find anything in the database that links it to the orc races:
          if (infos.raceID == RACE_ORC && infos.sexID == GENDER_MALE && infos.isHD)
          {
            RACES[1968587] = infos;
          }
        }
      }
    }
  }
  else // we are dealing with a 9.x database
  {
    auto races =
      GAMEDATABASE.sqlQuery("SELECT ChrRaces.ClientPrefix, ChrRaces.ID, ChrRaces.Flags, ChrModel.Sex, CreatureModelData.FileID, ChrModel.CharComponentTextureLayoutID, "
                            "ChrRaces.MaleModelFallbackRaceID, ChrRaces.MaleModelFallbackSex, ChrRaces.MaleTextureFallbackRaceID, ChrRaces.MaleTextureFallbackSex, "
                            "ChrRaces.FemaleModelFallbackRaceID, ChrRaces.FemaleModelFallbackSex, ChrRaces.FemaleTextureFallbackRaceID, ChrRaces.FemaleTextureFallbackSex, "
                            "ChrRaceXChrModel.ChrModelID "
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

      infos.isHD = GAMEDIRECTORY.getFile(modelfileid)->fullname().contains("_hd") ? true : false;

      if (RACES.find(modelfileid) == RACES.end())
      {
        RACES[modelfileid] = infos;
      }
      else // if a race is already inserted, capture any additional ChrModelID
      {
        auto id = race[14].toInt();
        if (std::find(RACES[modelfileid].ChrModelID.begin(), RACES[modelfileid].ChrModelID.end(), id) == RACES[modelfileid].ChrModelID.end())
          RACES[modelfileid].ChrModelID.push_back(id);
      }
    }
  }

#if DEBUG_RACEINFOS > 0
  for (const auto & r : RACES)
  {
    LOG_INFO << "---------------------------";
    LOG_INFO << "modelfileid ->" << r.first;
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

  const auto it = RACES.find(fileid);
  if (it != RACES.end() && !it->second.isHD)
  {
    const auto raceID = it->second.raceID;
    const auto sexID = it->second.sexID;

    for (auto &r : RACES)
    {
      if (r.second.raceID == raceID && r.second.sexID == sexID && r.second.isHD)
      {
        result = r.first;
        break;
      }
    }
  }

  return result;
}
