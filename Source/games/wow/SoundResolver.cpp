/*
 * SoundResolver.cpp -- Voice Lines (V1) creature-sound resolution. See SoundResolver.h.
 */
#include "SoundResolver.h"

#include "Game.h"         // GAMEDATABASE, GAMEDIRECTORY
#include "GameDatabase.h" // sqlResult
#include "GameFolder.h"   // getFilesForFolder
#include "GameFile.h"

#include <QStringList>

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

  // Reverse-resolve a SoundKitID for an audio file (folder-scanned files carry no kit id of their own).
  // Returns 0 when the file is not referenced by any SoundKitEntry.
  int soundKitForFile(int fileDataId)
  {
    sqlResult r = GAMEDATABASE.sqlQuery(
        QString("SELECT SoundKitID FROM SoundKitEntry WHERE FileDataID = %1 LIMIT 1").arg(fileDataId));
    if (r.valid && !r.empty())
      return r.values[0][0].toInt();
    return 0;
  }

  // Shared folder scanner for the V2 (vo_ only) and V3 (all .ogg) sources. Derives the "<token>" from a
  // "creature/<token>/..." model path, lists sound/creature/<token>/<prefix>*.ogg from the listfile-backed
  // file tree (so every match is openable), sorts by path, and builds one VoiceLineEntry per file with the
  // SoundKitID reverse-resolved. prefix is "vo_" for V2 or "" for V3. Empty vector = no matching folder.
  std::vector<VoiceLineEntry> scanCreatureFolder(const QString & modelPath, const QString & creatureName,
                                                 const char * prefix, const QString & source,
                                                 const QString & category, const QString & labelWord)
  {
    std::vector<VoiceLineEntry> out;

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

    const QString folder = "sound/creature/" + token + "/" + QString::fromLatin1(prefix);
    std::vector<GameFile *> files;
    GAMEDIRECTORY.getFilesForFolder(files, folder, ".ogg");
    if (files.empty())
      return out;

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
      e.source = source;
      e.category = category;
      e.fileDataId = f->fileDataId();
      e.filePath = f->fullname();
      e.soundKitId = soundKitForFile(e.fileDataId);
      e.variation = ++n;
      e.label = (creatureName.isEmpty() ? (labelWord + " ")
                                        : (creatureName + " " + labelWord + " ")) + QString::number(e.variation);
      out.push_back(e);
    }
    return out;
  }

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
      // Resolve a filename for the row when the listfile knows one. getFile() synthesizes a
      // "File########.unk" placeholder for files with NO listfile name; treat that as unnamed
      // (leave filePath empty) so the UI can show "(unnamed)" + the FileDataID rather than a stub.
      // Real names -- friendly ("sound/creature/vashnik/...") or generic/numeric
      // ("sound/creature/7133443/...") -- are kept as-is.
      GameFile * gf = GAMEDIRECTORY.getFile((uint)fdid);
      if (gf)
      {
        const QString fn = gf->fullname();
        if (!fn.isEmpty() && !fn.endsWith(".unk", Qt::CaseInsensitive))
          e.filePath = fn;
      }
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
  // V2: only vo_*.ogg. SoundKitID is reverse-resolved per file inside the shared scanner.
  return scanCreatureFolder(modelPath, creatureName, "vo_", "CreatureVoiceFolder", "Voice Line", "VO");
}

std::vector<VoiceLineEntry> SoundResolver::resolveEncounterDialogue(int modelFileDataId,
                                                                    QString * outEncounterName)
{
  std::vector<VoiceLineEntry> out;
  if (outEncounterName)
    outEncounterName->clear();

  // model FileDataID -> CreatureModelData -> every CreatureDisplayInfo using that model.
  sqlResult d = GAMEDATABASE.sqlQuery(QString(
      "SELECT CreatureDisplayInfo.ID FROM CreatureModelData "
      "JOIN CreatureDisplayInfo ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
      "WHERE CreatureModelData.FileDataID = %1").arg(modelFileDataId));
  if (!d.valid || d.empty())
    return out;
  QString dispList;
  for (size_t i = 0; i < d.values.size(); ++i)
  { if (!dispList.isEmpty()) dispList += ","; dispList += d.values[i][0]; }

  // display -> JournalEncounterCreature -> the encounter this model belongs to.
  sqlResult je = GAMEDATABASE.sqlQuery(QString(
      "SELECT DISTINCT JournalEncounter.Name_lang FROM JournalEncounterCreature "
      "JOIN JournalEncounter ON JournalEncounter.ID = JournalEncounterCreature.JournalEncounterID "
      "WHERE JournalEncounterCreature.CreatureDisplayInfoID IN (%1)").arg(dispList));
  if (!je.valid || je.empty())
    return out;
  const QString encName = je.values[0][0].trimmed();
  if (encName.isEmpty())
    return out;
  if (outEncounterName)
    *outEncounterName = encName;

  // Encounter name -> candidate folder tokens. Words too short or too generic are never a VO
  // folder on their own; keeping them produces obvious false hits ("king" -> king_anduin_wrynn).
  static const char * STOP[] = { "the", "and", "of", "a", "an", "in", "on", "at", "to", "for",
                                 "heads", "one", "king", "chrome" };
  QStringList tokens;
  QString slug, cur;
  const QString low = encName.toLower();
  for (int i = 0; i < low.size(); ++i)
  {
    const QChar ch = low[i];
    if (ch.isLetterOrNumber()) { cur += ch; slug += ch; }
    else
    {
      if (!cur.isEmpty()) { tokens << cur; cur.clear(); }
      if (ch == '-') slug += ch;                                   // keep "one-armed_bandit"
      else if (!slug.isEmpty() && !slug.endsWith('_')) slug += '_';
    }
  }
  if (!cur.isEmpty()) tokens << cur;
  while (slug.endsWith('_') || slug.endsWith('-')) slug.chop(1);
  // Drop leading stop-words from the slug so "the_one-armed_bandit" -> "one-armed_bandit".
  bool trimmed = true;
  while (trimmed)
  {
    trimmed = false;
    for (size_t s = 0; s < sizeof(STOP) / sizeof(STOP[0]); ++s)
    {
      const QString pre = QString(STOP[s]) + "_";
      if (slug.startsWith(pre)) { slug = slug.mid(pre.length()); trimmed = true; break; }
    }
  }

  QStringList cands;
  for (int i = 0; i < tokens.size(); ++i)
  {
    const QString & t = tokens[i];
    if (t.length() < 3) continue;
    bool stop = false;
    for (size_t s = 0; s < sizeof(STOP) / sizeof(STOP[0]) && !stop; ++s)
      if (t == STOP[s]) stop = true;
    if (!stop && !cands.contains(t)) cands << t;
  }
  if (!slug.isEmpty() && !cands.contains(slug)) cands << slug;

  // For each candidate, list sound/creature/<cand>* and keep only files whose FOLDER is exactly
  // the token or starts with "<token>_". The loose prefix alone would drag in rik->rikkal/riko.
  std::map<QString, std::vector<GameFile *>> byFolder;
  for (int i = 0; i < cands.size(); ++i)
  {
    const QString & t = cands[i];
    std::vector<GameFile *> files;
    GAMEDIRECTORY.getFilesForFolder(files, "sound/creature/" + t, ".ogg");
    for (size_t k = 0; k < files.size(); ++k)
    {
      GameFile * f = files[k];
      if (!f) continue;
      QString p = f->fullname(); p.replace('\\', '/');
      const int s = p.indexOf("sound/creature/", 0, Qt::CaseInsensitive);
      if (s < 0) continue;
      const int st = s + 15, sl = p.indexOf('/', st);
      if (sl <= st) continue;
      const QString folder = p.mid(st, sl - st).toLower();
      if (folder == t || folder.startsWith(t + "_"))
        byFolder[folder].push_back(f);
    }
  }
  if (byFolder.empty())
    return out;

  for (std::map<QString, std::vector<GameFile *>>::iterator it = byFolder.begin(); it != byFolder.end(); ++it)
  {
    std::vector<GameFile *> & files = it->second;
    std::sort(files.begin(), files.end(), [](GameFile * a, GameFile * b) {
      return a->fullname().compare(b->fullname(), Qt::CaseInsensitive) < 0;
    });
    int n = 0;
    for (size_t k = 0; k < files.size(); ++k)
    {
      VoiceLineEntry e;
      e.source = "EncounterDialogue";
      e.category = it->first;                 // the folder ("mug"/"zee") -> filter per speaker
      e.fileDataId = files[k]->fileDataId();
      e.filePath = files[k]->fullname();
      e.soundKitId = soundKitForFile(e.fileDataId);
      e.variation = ++n;
      e.label = it->first + " " + QString::number(e.variation);
      out.push_back(e);
    }
  }
  return out;
}

std::vector<VoiceLineEntry> SoundResolver::resolveCreatureAudioFolder(const QString & modelPath,
                                                                     const QString & creatureName)
{
  // V3: all *.ogg in the folder (superset of the vo_ files), catching non-vo_-named creature sounds.
  return scanCreatureFolder(modelPath, creatureName, "", "CreatureAudioFolder", "Folder Audio", "Audio");
}
