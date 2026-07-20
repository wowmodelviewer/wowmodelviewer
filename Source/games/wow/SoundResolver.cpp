/*
 * SoundResolver.cpp -- Voice Lines (V1) creature-sound resolution. See SoundResolver.h.
 */
#include "SoundResolver.h"

#include "Game.h"         // GAMEDATABASE, GAMEDIRECTORY
#include "GameDatabase.h" // sqlResult
#include "GameFolder.h"   // getFilesForFolder
#include "GameFile.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace
{
  struct CatCol { const char * col; const char * label; };

  // CreatureSoundData columns that carry a SoundKit id, in display order. Array columns use the DB's
  // suffixed names (SoundFidget1..5, CustomAttack1..4). Only V1 categories are listed.
  const CatCol SCALAR_CATS[] = {
    { "SoundAggroID", "Aggro" },
    { "SoundExertionID", "Attack" }, { "SoundExertionCriticalID", "Attack" },
    { "SoundInjuryID", "Wound" }, { "SoundInjuryCriticalID", "Wound" }, { "SoundInjuryCrushingBlowID", "Wound" },
    { "SoundDeathID", "Death" },
    { "SoundStandID", "Stand" }, { "LoopSoundID", "Loop" },
    { "SoundStunID", "Stun" }, { "SoundAlertID", "Alert" }, { "SoundFootstepID", "Footstep" },
    { "SoundWingFlapID", "WingFlap" }, { "SoundWingGlideID", "WingGlide" },
  };
  const CatCol ARRAY_CATS[] = {
    { "SoundFidget1", "Fidget" }, { "SoundFidget2", "Fidget" }, { "SoundFidget3", "Fidget" },
    { "SoundFidget4", "Fidget" }, { "SoundFidget5", "Fidget" },
    { "CustomAttack1", "Attack" }, { "CustomAttack2", "Attack" },
    { "CustomAttack3", "Attack" }, { "CustomAttack4", "Attack" },
  };

  // Query a group of category columns for one CreatureSoundData row -> (label, kit) pairs for non-zero
  // kits. A separate query per group so a missing array column can't hide the scalar categories.
  std::vector<std::pair<QString, int>> queryGroup(const CatCol * arr, size_t n, int soundSetId)
  {
    std::vector<std::pair<QString, int>> res;
    QString cols;
    for (size_t i = 0; i < n; ++i) { if (i) cols += ", "; cols += arr[i].col; }
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT %1 FROM CreatureSoundData WHERE ID = %2").arg(cols).arg(soundSetId));
    if (r.valid && !r.empty())
      for (size_t i = 0; i < n; ++i)
      {
        int kit = r.values[0][i].toInt();
        if (kit != 0)
          res.push_back(std::make_pair(QString(arr[i].label), kit));
      }
    return res;
  }
}

std::vector<VoiceLineEntry> SoundResolver::resolveCreatureSoundsForModel(int modelFileDataId,
                                                                        int * outSoundDataId,
                                                                        QString * outError)
{
  std::vector<VoiceLineEntry> out;
  if (outSoundDataId) *outSoundDataId = 0;
  if (outError) outError->clear();

  // 1) model FileDataID -> CreatureModelData default sound + any per-display override.
  sqlResult m = GAMEDATABASE.sqlQuery(QString(
      "SELECT CreatureModelData.SoundID, CreatureDisplayInfo.SoundID "
      "FROM CreatureModelData "
      "LEFT JOIN CreatureDisplayInfo ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
      "WHERE CreatureModelData.FileDataID = %1").arg(modelFileDataId));

  if (!m.valid || m.empty())
  {
    if (outError) *outError = "no CreatureModelData for this model FileDataID";
    return out;
  }

  int modelSoundId = m.values[0][0].toInt();
  int firstDisplayOverride = 0;
  for (size_t i = 0; i < m.values.size(); ++i)
  {
    int ds = m.values[i][1].toInt();
    if (ds != 0) { firstDisplayOverride = ds; break; }
  }
  int soundSetId = modelSoundId != 0 ? modelSoundId : firstDisplayOverride;
  if (outSoundDataId) *outSoundDataId = soundSetId;
  if (soundSetId == 0)
  {
    if (outError) *outError = "no SoundID on model or display";
    return out;
  }

  // 2) gather (category, kit) pairs.
  std::vector<std::pair<QString, int>> pairs = queryGroup(SCALAR_CATS, sizeof(SCALAR_CATS) / sizeof(SCALAR_CATS[0]), soundSetId);
  std::vector<std::pair<QString, int>> arrPairs = queryGroup(ARRAY_CATS, sizeof(ARRAY_CATS) / sizeof(ARRAY_CATS[0]), soundSetId);
  pairs.insert(pairs.end(), arrPairs.begin(), arrPairs.end());

  // 3) resolve each (category, kit) -> audio files; dedup repeated kits; number per category.
  std::set<QString> seenCatKit;
  std::map<QString, int> perCatCount;
  for (size_t p = 0; p < pairs.size(); ++p)
  {
    const QString & category = pairs[p].first;
    int kit = pairs[p].second;
    QString ck = category + "#" + QString::number(kit);
    if (seenCatKit.count(ck))
      continue; // e.g. CustomAttack reuses the same kit in all 4 slots
    seenCatKit.insert(ck);

    QString name;
    sqlResult kn = GAMEDATABASE.sqlQuery(QString("SELECT Name FROM SoundKitName WHERE ID = %1").arg(kit));
    if (kn.valid && !kn.empty())
      name = kn.values[0][0].trimmed();

    sqlResult ke = GAMEDATABASE.sqlQuery(QString("SELECT FileDataID FROM SoundKitEntry WHERE SoundKitID = %1").arg(kit));
    if (!ke.valid || ke.empty())
      continue;

    for (size_t r = 0; r < ke.values.size(); ++r)
    {
      int fdid = ke.values[r][0].toInt();
      if (fdid == 0)
        continue;
      VoiceLineEntry e;
      e.source = "CreatureSound";
      e.category = category;
      e.soundKitId = kit;
      e.fileDataId = fdid;
      e.soundKitName = name;
      e.variation = ++perCatCount[category];
      e.label = category + " " + QString::number(e.variation);
      if (!name.isEmpty())
        e.label += " (" + name + ")";
      out.push_back(e);
    }
  }

  return out;
}

std::vector<VoiceLineEntry> SoundResolver::resolveCreatureVoiceFolder(const QString & modelPath,
                                                                     const QString & creatureName)
{
  std::vector<VoiceLineEntry> out;

  // Derive the folder token: "creature/<token>/<file>.m2" -> "<token>".
  QString p = modelPath;
  p.replace('\\', '/');
  QString token;
  int ci = p.indexOf("creature/", 0, Qt::CaseInsensitive);
  if (ci >= 0)
  {
    int start = ci + 9; // past "creature/"
    int slash = p.indexOf('/', start);
    if (slash > start)
      token = p.mid(start, slash - start);
  }
  if (token.isEmpty())
    return out;

  // Match sound/creature/<token>/vo_*.ogg against the listfile-backed CASC file tree. Only files that
  // actually exist in CASC are in the tree, so every match is openable.
  const QString folder = "sound/creature/" + token + "/vo_";
  std::vector<GameFile *> files;
  GAMEDIRECTORY.getFilesForFolder(files, folder, ".ogg");
  if (files.empty())
    return out;

  // Order by path so vo_..._01, _02, ... come out in sequence.
  std::sort(files.begin(), files.end(), [](GameFile * a, GameFile * b) {
    return a->fullname().compare(b->fullname(), Qt::CaseInsensitive) < 0;
  });

  int n = 0;
  for (size_t i = 0; i < files.size(); ++i)
  {
    GameFile * f = files[i];
    if (!f)
      continue;
    VoiceLineEntry e;
    e.source = "CreatureVoiceFolder";
    e.category = "Voice";
    e.fileDataId = f->fileDataId();
    e.filePath = f->fullname();
    e.variation = ++n;
    e.label = (creatureName.isEmpty() ? QString("VO ") : (creatureName + " VO ")) + QString::number(e.variation);
    out.push_back(e);
  }
  return out;
}
