#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#define DEBUG_RACEINFOS 0

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
  int fdid = model->gamefile->fileDataId();
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
  sqlResult races = 
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

  if(!races.valid || races.empty())
  {
    LOG_ERROR << "Unable to collect race information from game database";
    return;
  }

  for (auto& value : races.values)
  {
    std::string displayPrefix;

    for(int r = 0; r <12 ; r+=3)
    {
      if(value[r] != "")
      {
        RaceInfos infos;
        infos.prefix = !displayPrefix.empty() ? displayPrefix : value[r + 1].toStdString();
        infos.textureLayoutID = value[r+2].toInt();
        infos.raceID = value[12].toInt();
        infos.sexID = (r == 0 || r == 6)?0:1;
        infos.barefeet = (value[14].toInt() & 0x2);
        // Get fallback display race ID (this is mostly for allied races and others that rely on
        // item display info from other race models):
        infos.MaleModelFallbackRaceID = value[15].toInt();
        infos.MaleModelFallbackSex = value[16].toInt();
        infos.FemaleModelFallbackRaceID = value[17].toInt();
        infos.FemaleModelFallbackSex = value[18].toInt();
        infos.MaleTextureFallbackRaceID = value[19].toInt();
        infos.MaleTextureFallbackSex = value[20].toInt();
        infos.FemaleTextureFallbackRaceID = value[21].toInt();
        infos.FemaleTextureFallbackSex = value[22].toInt();

        int modelfileid = value[r].toInt();
        
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

#if DEBUG_RACEINFOS > 0
        LOG_INFO << "---------------------------";
        LOG_INFO << "modelfileid ->" << modelfileid;
        LOG_INFO << "infos.prefix =" << infos.prefix.c_str();
        LOG_INFO << "infos.textureLayoutID =" << infos.textureLayoutID;
        LOG_INFO << "infos.raceID =" << infos.raceID;
        LOG_INFO << "infos.sexID =" << infos.sexID;
        LOG_INFO << "infos.customization[0] =" << infos.customization[0].c_str();
        LOG_INFO << "infos.customization[1] =" << infos.customization[1].c_str();
        LOG_INFO << "infos.customization[2] =" << infos.customization[2].c_str();
        LOG_INFO << "infos.isHD =" << infos.isHD;
        LOG_INFO << "---------------------------";
#endif
      }
    }
  }

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
