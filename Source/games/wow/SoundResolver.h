/*
 * SoundResolver.h
 *
 * Voice Lines (V1) creature-sound resolution. Given a creature model's FileDataID, walks
 * CreatureModelData/CreatureDisplayInfo -> CreatureSoundData -> per-category SoundKit -> SoundKitEntry
 * to produce a flat, playable list of VoiceLineEntry (one per audio file). Pure DB/CASC lookups (no
 * audio, no UI), shared by the -vlprobe CLI and the Voice Lines dialog.
 */
#ifndef _SOUNDRESOLVER_H_
#define _SOUNDRESOLVER_H_

#include <vector>

#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _SOUNDRESOLVER_API_ __declspec(dllexport)
#    else
#        define _SOUNDRESOLVER_API_ __declspec(dllimport)
#    endif
#else
#    define _SOUNDRESOLVER_API_
#endif

// One playable creature-sound line: a single audio file belonging to a category's SoundKit.
struct VoiceLineEntry
{
  QString category;       // "Aggro", "Attack", "Wound", "Death", "Fidget", "Stand", "Loop", "Alert", ...
  QString label;          // UI label, e.g. "Aggro 1"  (+ SoundKit name if the client ships one)
  int     soundKitId = 0;
  int     fileDataId = 0;
  int     variation  = 1; // 1-based index within its category (Attack 1, Attack 2, ...)
  QString soundKitName;   // friendly name if available (usually blank in retail)
};

class _SOUNDRESOLVER_API_ SoundResolver
{
public:
  // Resolve the creature sound set for a model's FileDataID (CreatureModelData.FileDataID). Uses the
  // model-default CreatureSoundData.SoundID, falling back to the first per-display override. Returns one
  // VoiceLineEntry per resolved audio file, deduplicated by (category, SoundKitID). An empty vector means
  // no creature sounds (not a creature model, or no sound set). outSoundDataId / outError are optional.
  static std::vector<VoiceLineEntry> resolveCreatureSoundsForModel(int modelFileDataId,
                                                                   int * outSoundDataId = nullptr,
                                                                   QString * outError = nullptr);
};

#endif /* _SOUNDRESOLVER_H_ */
