#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#define DEBUG_RACEINFOS 0

std::map<int, RaceInfos> RaceInfos::RACES;

bool RaceInfos::getCurrent(WoWModel * model, RaceInfos & result)
{
  if (!model)
  {
    LOG_ERROR << __FUNCTION__ << "model is null";
    return false;
  }

  const auto raceInfosIt = RACES.find(model->gamefile->fileDataId());
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
                          "ChrRaces.ID, BaseRaceID, Flags, MaleModelFallbackRaceID, FemaleModelFallbackRaceID, MaleTextureFallbackRaceID, FemaleTextureFallbackRaceID FROM ChrRaces "
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
        infos.raceid = value[12].toInt();
        infos.sexid = (r == 0 || r == 6)?0:1;
        infos.barefeet = (value[14].toInt() & 0x2);
        // Get fallback display race ID (this is mostly for allied races and others that rely on
        // item display info from other race models):
        infos.MaleModelFallbackRaceID = value[15].toInt();
        infos.FemaleModelFallbackRaceID = value[16].toInt();
        infos.MaleTextureFallbackRaceID = value[17].toInt();
        infos.FemaleTextureFallbackRaceID = value[18].toInt();

 /*      
        // workaround - manually associate display race id with related race - info not in db ?
        switch (infos.raceid)
        {
          case 25: // pandaren 2
          case 26: // pandaren 3
            infos.displayRaceid = 24; // pandaren 1
            break;
          case 28: // High Roc Tauren
            infos.displayRaceid = 6; // Tauren
            break;
          case 29: // Void Elf
            infos.displayRaceid = 10; // Blood elf
            break;
          case 30: // Sancteforge Draenei
            infos.displayRaceid = 11; // Draenei
            break;
          default:
            break;
        }
*/

        int modelfileid = value[r].toInt();
        
        if ((r == 6) || (r == 9)) // if we are dealing with a HD model
          infos.isHD = true;
        else
          infos.isHD = false;

        if (RACES.find(modelfileid) == RACES.end())
          RACES[modelfileid] = infos;

#if DEBUG_RACEINFOS > 0
        LOG_INFO << "---------------------------";
        LOG_INFO << "modelfileid ->" << modelfileid;
        LOG_INFO << "infos.prefix =" << infos.prefix.c_str();
        LOG_INFO << "infos.textureLayoutID =" << infos.textureLayoutID;
        LOG_INFO << "infos.raceid =" << infos.raceid;
        LOG_INFO << "infos.sexid =" << infos.sexid;
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
    const auto raceid = it->second.raceid;
    const auto sexid = it->second.sexid;

    for (auto &r : RACES)
    {
      if (r.second.raceid == raceid && r.second.sexid == sexid && r.second.isHD)
      {
        result = r.first;
        break;
      }
    }
  }

  return result;
}
