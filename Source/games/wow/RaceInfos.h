#pragma once

#include <map>
#include <string>
#include <vector>

#define _RACEINFOS_API_

class WoWModel;

class _RACEINFOS_API_ RaceInfos
{
public:
	int raceID = -1; // -1 means invalid race (default value)
	int sexID = 0; // 0 male / 1 female
	int textureLayoutID = 0;
	bool isHD = false;
	bool barefeet = false;
	std::string prefix;
	int modelFallbackRaceID = -1;
	int modelFallbackSexID = -1;
	int textureFallbackRaceID = -1;
	int textureFallbackSexID = -1;
	std::vector<int> ChrModelID;

	static void init();
	static int getHDModelForFileID(int);
	static bool getRaceInfosForFileID(int, RaceInfos&);
	static int getFileIDForRaceSex(const int& race, const int& sex);
	static const std::map<int, RaceInfos>& getAllRaces() { return RACES; }

private:
	static std::map<int, RaceInfos> RACES;
};
