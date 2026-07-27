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
    int modelFileID = -1; // CreatureModelData.FileDataID of this race+sex model
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
    // Resolve by ChrRaces ID + sex. Unlike getRaceInfosForFileID() this can tell apart races
    // that SHARE an M2 (Mag'har/Orc, faction Pandaren/neutral Pandaren, Gilnean/Human, ...),
    // because it is keyed by the race, not by the model file.
    static bool getRaceInfosForRaceSex(const int & race, const int & sex, RaceInfos & out);

  private:
    // Keyed by model FileDataID. Several races can share one M2, so this map holds only the
    // FIRST race seen for a given file -- it answers "which race is this model file?", which
    // is inherently ambiguous, and it must stay this way for the by-file lookups.
    static std::map<int, RaceInfos> RACES;
    // Keyed by (raceID, sexID). One entry per race+sex, so races that share an M2 with an
    // earlier race keep their own textureLayoutID and ChrModelID list instead of being folded
    // into that race.
    static std::map<std::pair<int, int>, RaceInfos> RACES_BY_RACE_SEX;
};




#endif /* _RACEINFOS_H_ */
