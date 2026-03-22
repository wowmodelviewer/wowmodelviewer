#pragma once

#include <map>
#include <string>
#include <vector>

#define _RACEINFOS_API_

class WoWModel;

/// @brief Stores per-race/sex metadata loaded from ChrRaces / ChrModel database tables.
class _RACEINFOS_API_ RaceInfos
{
public:
	int raceID = -1;               ///< Race ID (-1 = invalid).
	int sexID = 0;                 ///< 0 = male, 1 = female.
	int textureLayoutID = 0;       ///< Texture layout ID for compositing.
	bool isHD = false;             ///< Whether this is an HD model.
	bool barefeet = false;         ///< Whether the race shows bare feet by default.
	std::string prefix;            ///< Race name prefix used for file lookups.
	int modelFallbackRaceID = -1;  ///< Fallback race for the model.
	int modelFallbackSexID = -1;   ///< Fallback sex for the model.
	int textureFallbackRaceID = -1; ///< Fallback race for textures.
	int textureFallbackSexID = -1;  ///< Fallback sex for textures.
	std::vector<int> ChrModelID;   ///< ChrModel IDs for this race/sex.

	/// @brief Initialise the global race info table from the database.
	static void init();
	/// @brief Get the HD model file ID for a given file ID.
	static int getHDModelForFileID(int);
	/// @brief Populate a RaceInfos struct from a model file ID.
	static bool getRaceInfosForFileID(int, RaceInfos&);
	/// @brief Get the model file ID for a specific race and sex.
	static int getFileIDForRaceSex(const int& race, const int& sex);
	/// @brief Return all loaded race info entries.
	static const std::map<int, RaceInfos>& getAllRaces() { return RACES; }

private:
	static std::map<int, RaceInfos> RACES;
};
