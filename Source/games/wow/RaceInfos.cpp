#include "RaceInfos.h"
#include "DB2Table.h"
#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"
#include "logger/Logger.h"

#define DEBUG_RACEINFOS 1

std::map<int, RaceInfos> RaceInfos::RACES;

void RaceInfos::init()
{
	const DB2Table* chrRaceXChrModel = WOWDB.getTable("ChrRaceXChrModel");
	const DB2Table* chrRaces = WOWDB.getTable("ChrRaces");
	const DB2Table* chrModel = WOWDB.getTable("ChrModel");
	const DB2Table* creatureDisplayInfo = WOWDB.getTable("CreatureDisplayInfo");
	const DB2Table* creatureModelData = WOWDB.getTable("CreatureModelData");

	if (!chrRaceXChrModel || !chrRaces || !chrModel || !creatureDisplayInfo || !creatureModelData)
	{
		LOG_ERROR << "Unable to collect race information from game database";
		return;
	}

	for (const auto& xRow : *chrRaceXChrModel)
	{
		const uint32_t chrRacesID = xRow.getUInt("ChrRacesID");
		const uint32_t chrModelID = xRow.getUInt("ChrModelID");

		DB2Row raceRow = chrRaces->getRow(chrRacesID);
		DB2Row modelRow = chrModel->getRow(chrModelID);

		if (!raceRow || !modelRow)
			continue;

		const uint32_t displayID = modelRow.getUInt("DisplayID");
		DB2Row displayRow = creatureDisplayInfo->getRow(displayID);
		if (!displayRow)
			continue;

		const uint32_t modelDataID = displayRow.getUInt("ModelID");
		DB2Row modelDataRow = creatureModelData->getRow(modelDataID);
		if (!modelDataRow)
			continue;

		RaceInfos infos;
		infos.prefix = raceRow.getString("ClientPrefix");
		infos.raceID = static_cast<int>(raceRow.recordID());
		infos.barefeet = (static_cast<int>(raceRow.getUInt("Flags")) & 0x2);
		infos.sexID = static_cast<int>(modelRow.getUInt("Sex"));
		auto modelfileid = static_cast<int>(modelDataRow.getUInt("FileDataID"));
		infos.textureLayoutID = static_cast<int>(modelRow.getUInt("CharComponentTextureLayoutID"));
		if (infos.textureLayoutID <= 0)
		{
			LOG_WARNING << "Unexpected textureLayoutID (" << infos.textureLayoutID << ") for raceID " << infos.raceID << ", sexID " << infos.sexID;
		}

		// Get fallback display race ID (this is mostly for allied races and others that rely on
		// item display info from other race models):
		if (infos.sexID == GENDER_MALE)
		{
			infos.modelFallbackRaceID = raceRow.getInt("MaleModelFallbackRaceID");
			infos.modelFallbackSexID = raceRow.getInt("MaleModelFallbackSex");
			infos.textureFallbackRaceID = raceRow.getInt("MaleTextureFallbackRaceID");
			infos.textureFallbackSexID = raceRow.getInt("MaleTextureFallbackSex");
		}
		else
		{
			infos.modelFallbackRaceID = raceRow.getInt("FemaleModelFallbackRaceID");
			infos.modelFallbackSexID = raceRow.getInt("FemaleModelFallbackSex");
			infos.textureFallbackRaceID = raceRow.getInt("FemaleTextureFallbackRaceID");
			infos.textureFallbackSexID = raceRow.getInt("FemaleTextureFallbackSex");
		}

		infos.ChrModelID.push_back(static_cast<int>(chrModelID));

		infos.isHD = GAMEDIRECTORY.getFile(modelfileid)->fullname().find("_hd") != std::string::npos;

		if (RACES.find(modelfileid) == RACES.end())
		{
			RACES[modelfileid] = infos;
		}
		else // if a race is already inserted, capture any additional ChrModelID
		{
			auto id = static_cast<int>(chrModelID);
			if (std::find(RACES[modelfileid].ChrModelID.begin(), RACES[modelfileid].ChrModelID.end(), id) == RACES[
				modelfileid].ChrModelID.end())
				RACES[modelfileid].ChrModelID.push_back(id);
		}
	}

#if DEBUG_RACEINFOS > 0
	for (const auto& r : RACES)
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
		for (const auto& it : r.second.ChrModelID)
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

		for (const auto& r : RACES)
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

bool RaceInfos::getRaceInfosForFileID(int fileid, RaceInfos& infos)
{
	const auto raceInfosIt = RaceInfos::RACES.find(fileid);

	if (raceInfosIt != RaceInfos::RACES.end())
	{
		infos = raceInfosIt->second;
		return true;
	}

	return false;
}

int RaceInfos::getFileIDForRaceSex(const int& race, const int& sex)
{
	for (const auto& r : RACES)
	{
		if (r.second.raceID == race && r.second.sexID == sex)
			return r.first;
	}

	return -1;
}
