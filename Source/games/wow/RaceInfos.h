#ifndef _RACEINFOS_H_
#define _RACEINFOS_H_

#include <map>
#include <string>
#include <utility>
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
    std::string clientFileString; // lowercased ChrRaces.ClientFileString, e.g. "bloodelf"
    std::string nameLang;         // ChrRaces.Name_lang display name, e.g. "Blood Elf"
    bool isNPC = false;           // ChrRaces.Flags & 1 (except races 23/75) -> NPC-only race
    int modelFallbackRaceID;
    int modelFallbackSexID;
    int textureFallbackRaceID;
    int textureFallbackSexID;
    int modelFileID = -1; // CreatureModelData FileDataID of this race+sex's character model
    std::vector<int> ChrModelID;

    // One row per race for the UI race browser (Playable vs NPC), built from the
    // ChrRaces data; maps each race to its male/female model FileDataID.
    struct RaceMenuEntry
    {
      int raceID = -1;
      std::string name;       // display name (Name_lang, falls back to clientFileString)
      bool isNPC = false;
      int maleFileID = -1;    // model FileDataID, -1 if none
      int femaleFileID = -1;
    };
    static std::vector<RaceMenuEntry> getRaceMenu(); // sorted by display name

    static void init();
    static int getHDModelForFileID(int);
    static bool getRaceInfosForFileID(int, RaceInfos &);
    // Resolve by race directory name (lowercased ClientFileString) + sex, used as a
    // fallback when a character model file isn't the canonical race model in the map.
    static bool getRaceInfosForName(const std::string & raceName, int sex, RaceInfos &);
    static int getFileIDForRaceSex(const int & race, const int & sex);
    // The row for one race and sex, including the races that share a model file with another
    // race and so cannot be found by that file id.
    static bool getRaceInfosForRaceSex(int race, int sex, RaceInfos & out);
    // How many (race, sex) pairs the loaded client resolved to a model.
    static size_t count() { return RACES.size(); }

  private:
    // Every race and sex, one entry each, preferring the HD model where a race has more than
    // one. Keyed by (raceID, sexID) rather than by model FileDataID: races that share their
    // model file with another race -- Mag'har Orc with Orc, the two faction Pandaren with
    // Pandaren -- collided under a file-id key and were dropped, which left them unloadable.
    static std::map<std::pair<int, int>, RaceInfos> RACES;

    // The same rows keyed by model FileDataID, first row for a file winning and later races
    // on that file only adding their ChrModelIDs. This is what answers "which race is this
    // model I just loaded", where a shared file can only mean one race.
    static std::map<int, RaceInfos> RACES_BY_FILEID;
};




#endif /* _RACEINFOS_H_ */
