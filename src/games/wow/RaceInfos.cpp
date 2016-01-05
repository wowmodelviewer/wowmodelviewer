#include "RaceInfos.h"

#include "GameDatabase.h"
#include <algorithm> // std::stransform
#include "Game.h"
#include "logger/Logger.h"

std::map< std::string, RaceInfos> RaceInfos::RACES;

bool RaceInfos::getCurrent(std::string modelName, RaceInfos & result)
{
  // find informations from curent model
  size_t lastSlashPos = modelName.find_last_of("\\");
  if(lastSlashPos != std::string::npos)
  {
    modelName = modelName.substr(lastSlashPos+1, modelName.length());
  }
  std::transform(modelName.begin(), modelName.end(), modelName.begin(), ::tolower);

  QString model = QString::fromStdString(modelName);
  if(!model.contains(".m2", Qt::CaseInsensitive))
	modelName += ".m2";

  std::map< std::string, RaceInfos>::iterator raceInfosIt = RACES.find(modelName);
  if(raceInfosIt != RACES.end())
  {
    result = raceInfosIt->second;
    return true;
  }

  LOG_ERROR << "Unable to retrieve race infos for model" << modelName.c_str();
  return false;
}

void RaceInfos::init()
{
  sqlResult races = GAMEDATABASE.sqlQuery(" \
  SELECT FDM.name as malemodel, ClientPrefix, CharComponentTexLayoutID, \
  FDF.name AS femalemodel, ClientPrefix, CharComponentTexLayoutID, \
  FDMHD.name as malemodelHD, ClientPrefix, CharComponentTexLayoutHiResID, \
  FDFHD.name AS femalemodelHD, ClientPrefix, CharComponentTexLayoutHiResID, \
  ChrRaces.ID, FacialHairCustomization1, FacialHairCustomization2, HairCustomization FROM ChrRaces \
  LEFT JOIN CreatureDisplayInfo CDIM ON CDIM.ID = MaleDisplayID LEFT JOIN CreatureModelData CMDM ON CDIM.ModelID = CMDM.ID LEFT JOIN FileData FDM ON CMDM.FileDataID = FDM.ID \
  LEFT JOIN CreatureDisplayInfo CDIF ON CDIF.ID = FemaleDisplayID LEFT JOIN CreatureModelData CMDF ON CDIF.ModelID = CMDF.ID LEFT JOIN FileData FDF ON CMDF.FileDataID = FDF.ID \
  LEFT JOIN CreatureDisplayInfo CDIMHD ON CDIMHD.ID = HighResMaleDisplayId LEFT JOIN CreatureModelData CMDMHD ON CDIMHD.ModelID = CMDMHD.ID LEFT JOIN FileData FDMHD ON CMDMHD.FileDataID = FDMHD.ID \
  LEFT JOIN CreatureDisplayInfo CDIFHD ON CDIFHD.ID = HighResFemaleDisplayId LEFT JOIN CreatureModelData CMDFHD ON CDIFHD.ModelID = CMDFHD.ID LEFT JOIN FileData FDFHD ON CMDFHD.FileDataID = FDFHD.ID");

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
        std::string modelname = races.values[i][r].toStdString();
        std::transform(modelname.begin(), modelname.end(), modelname.begin(), ::tolower);

        if(modelname.find("_hd") != std::string::npos)
          infos.isHD = true;
        else
          infos.isHD = false;

        if(RACES.find(modelname) == RACES.end())
          RACES[modelname] = infos;

      }
    }
  }

}
