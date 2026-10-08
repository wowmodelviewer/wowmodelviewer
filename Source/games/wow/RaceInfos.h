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

// The two generations of a character model. Told apart by the model itself (WoWModel::modelGeneration), never by its
// file name: a model with a separate eye texture (M2 texture type 19) is High Definition, any other is Classic, whose
// eyes are painted in the face of the body texture.
enum class CharacterModelVariant { Unknown, Classic, HD };

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
    // The row of a race on a model file, in whatever sex that model has: a dragon form's ChrModel has a sex of its own
    // (neither male nor female), so a race worn on it is not found by the sex an NPC's display names.
    static bool getRaceInfosForRaceAndFile(int race, int fileid, RaceInfos & out);
    // How many (race, sex) pairs the loaded client resolved to a model.
    static size_t count() { return RACES.size(); }
    static void clear();

    // A character model's other generation, where the loaded client ships one (Classic Beta 1.60.1): ChrModelAltVariant
    // pairs the playable ChrModel of a race and sex (the primary, named by ChrRaceXChrModel) with an alternate ChrModel
    // of the same race and sex that no ChrRaceXChrModel row names, and its option and choice tables translate one
    // model's appearance into the other's. One pair, as RaceInfos validated it on loading the client.
    struct VariantPair
    {
      int id = 0;
      int raceID = -1;
      int sexID = -1;
      int primaryChrModelID = 0;
      int alternateChrModelID = 0;
      int primaryFileID = -1;
      int alternateFileID = -1;
      std::map<unsigned int, unsigned int> optionToAlternate, optionToPrimary; // ChrModelAltVariantOption, both ways
      std::map<unsigned int, unsigned int> choiceToAlternate, choiceToPrimary; // ChrModelAltVariantChoice, one to one
      bool isPrimary(int chrModelID) const { return chrModelID == primaryChrModelID; }
    };
    // One translation of an appearance (option -> choice) to the other model of a pair.
    struct Translation
    {
      std::map<unsigned int, unsigned int> selection;              // the other model's option -> choice
      std::vector<std::pair<unsigned int, unsigned int> > carried; // this model's option -> the other model's option
      std::vector<unsigned int> noCounterpart;                     // this model's options the other model has no option for
      std::vector<unsigned int> notCovered;                        // options whose current choice has no counterpart
    };
    static size_t variantPairCount() { return VARIANT_PAIRS.size(); }
    // The pair a ChrModel belongs to, as primary or as alternate.
    static bool getVariantPair(int chrModelID, VariantPair & out);
    // The row of an alternate model file: its pair's primary row with the alternate's ChrModel, model file and texture
    // layout (race, sex, fallbacks and names are the race's).
    static bool getRaceInfosForAlternateFileID(int fileid, RaceInfos & out);
    // The other model of a character's pair: the alternate's row for a primary, the primary's for an alternate.
    static bool getVariantPartner(const RaceInfos & current, RaceInfos & partner);
    static Translation translateSelection(const VariantPair & pair, const std::map<unsigned int, unsigned int> & from, bool toAlternate);
    // The model file a creature display with extended display info is shown on: its race's HD model
    // (getHDModelForFileID), or -- for a display that names a pair's alternate model but stores choices of the pair's
    // primary ChrModel -- the primary model, when the client has it. One answer for View NPC and Load NPC / Model by ID.
    static int getCreatureDisplayFileID(int fileDataId, int extraId);

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

    // The variant pairs (by ChrModelAltVariant ID), each pair by its primary and its alternate ChrModel, and the
    // alternates' rows by model file. Kept apart from RACES and RACES_BY_FILEID: the Characters list, Armory, the NPC
    // upgrade to the HD model and every lookup by race stay on the playable models.
    static void initVariants();
    static std::map<int, VariantPair> VARIANT_PAIRS;
    static std::map<int, int> VARIANT_PAIR_BY_CHRMODEL;
    static std::map<int, RaceInfos> ALTERNATES_BY_FILEID;
};




#endif /* _RACEINFOS_H_ */
