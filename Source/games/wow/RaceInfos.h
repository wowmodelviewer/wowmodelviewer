#pragma once

#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _RACEINFOS_API_ __declspec(dllexport)
#    else
#        define _RACEINFOS_API_ __declspec(dllimport)
#    endif
#else
#    define _RACEINFOS_API_
#endif

class WoWModel;

class _RACEINFOS_API_ RaceInfos
{
public:
	int raceID = -1; // -1 means invalid race (default value)
	int sexID; // 0 male / 1 female
	int textureLayoutID;
	bool isHD;
	bool barefeet;
	std::string prefix;
	int modelFallbackRaceID;
	int modelFallbackSexID;
	int textureFallbackRaceID;
	int textureFallbackSexID;
	std::vector<int> ChrModelID;

	static void init();
	static int getHDModelForFileID(int);
	static bool getRaceInfosForFileID(int, RaceInfos&);
	static int getFileIDForRaceSex(const int& race, const int& sex);

private:
	static std::map<int, RaceInfos> RACES;
};
