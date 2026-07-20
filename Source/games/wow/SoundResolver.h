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

// One playable line. Two sources:
//   "CreatureSound"       -- V1: a CreatureSoundData category audio file (Aggro/Attack/...).
//   "CreatureVoiceFolder" -- V2: a sound/creature/<folder>/vo_*.ogg file matched by the model folder name.
struct VoiceLineEntry
{
  QString source;         // "CreatureSound" (V1) or "CreatureVoiceFolder" (V2)
  QString category;       // V1: "Aggro"/"Attack"/...  V2: "Voice"
  QString label;          // UI label, e.g. "Aggro 1" (V1) or "Aggramar VO 1" (V2)
  int     soundKitId = 0;
  int     fileDataId = 0;
  int     variation  = 1; // 1-based index within its category/source
  QString soundKitName;   // friendly name if available (usually blank in retail)
  QString filePath;       // listfile path when known (V2 vo_* files; used for export filename)
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

  // V2: from a creature model path (e.g. "creature/aggramar/aggramar.m2") derive the folder token and
  // return the sound/creature/<token>/vo_*.ogg files (matched against the listfile-backed file tree).
  // One VoiceLineEntry per file (source "CreatureVoiceFolder"), labelled "<creatureName> VO N" when a
  // name is given, else "VO N". No text/role labels are invented. Empty vector = no voice folder.
  static std::vector<VoiceLineEntry> resolveCreatureVoiceFolder(const QString & modelPath,
                                                                const QString & creatureName = QString());
};

#endif /* _SOUNDRESOLVER_H_ */
