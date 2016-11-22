#include "RaceInfos.h"

#include "GameDatabase.h"
#include "Game.h"
#include "WoWModel.h"

#include "logger/Logger.h"



std::map< int, RaceInfos> RaceInfos::RACES;

bool RaceInfos::getCurrent(WoWModel * model, RaceInfos & result)
{
  if (!model)
  {
    LOG_ERROR << __FUNCTION__ << "model is null";
    return false;
  }

  auto raceInfosIt = RACES.find(model->gamefile->fileDataId());
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
                          "ChrRaces.ID, FacialHairCustomization1, FacialHairCustomization2, HairCustomization FROM ChrRaces "
                          "LEFT JOIN CreatureDisplayInfo CDIM ON CDIM.ID = MaleDisplayID LEFT JOIN CreatureModelData CMDM ON CDIM.ModelID = CMDM.ID "
                          "LEFT JOIN CreatureDisplayInfo CDIF ON CDIF.ID = FemaleDisplayID LEFT JOIN CreatureModelData CMDF ON CDIF.ModelID = CMDF.ID "
                          "LEFT JOIN CreatureDisplayInfo CDIMHD ON CDIMHD.ID = HighResMaleDisplayId LEFT JOIN CreatureModelData CMDMHD ON CDIMHD.ModelID = CMDMHD.ID "
                          "LEFT JOIN CreatureDisplayInfo CDIFHD ON CDIFHD.ID = HighResFemaleDisplayId LEFT JOIN CreatureModelData CMDFHD ON CDIFHD.ModelID = CMDFHD.ID");

  if(!races.valid || races.empty())
  {
    LOG_ERROR << "Unable to collect race information from game database";
    return;
  }

  for(int i=0, imax = races.values.size() ; i < imax ; i++)
  {
    for(int r = 0; r <12 ; r+=3)
    {
      if(races.values[i][r] != "")
      {
        RaceInfos infos;
        infos.prefix = races.values[i][r+1].toStdString();
        infos.textureLayoutID = races.values[i][r+2].toInt();
        infos.raceid = races.values[i][12].toInt();
        infos.sexid = (r == 0 || r == 6)?0:1;
        infos.customization[0] = races.values[i][13].toStdString();
        infos.customization[1] = races.values[i][14].toStdString();
        infos.customization[2] = races.values[i][15].toStdString();
        int modelfileid = races.values[i][r].toInt();
        
        if (infos.textureLayoutID == 2 && infos.raceid != 24)
          infos.isHD = true;
        else
          infos.isHD = false;

        if (RACES.find(modelfileid) == RACES.end())
          RACES[modelfileid] = infos;

      }
    }
  }

}
