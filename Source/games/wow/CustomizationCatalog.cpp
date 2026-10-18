#include "CharDetails.h"
#include "WoWModel.h"
#include "Game.h"
#include "GameFile.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "CustomizationBoneFile.h"
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

namespace
{
  // Verified neutral Night Elf female faces, not a general missing-file fallback.
  // Their DB2 references are absent from CASC and the skeleton's BFID list; the
  // corresponding inline FacePose sequences contain no per-sequence transforms.
  // An independent reference renderer clears the previous correction in this case.
  // Keep the selection and explicitly serialize an identity set (see docs).
  bool verifiedNeutralFace(const WoWModel & model, int modelId, uint choice, uint set, int file)
  {
    if (modelId != 921844) return false;
    const int sub = choice == 825 && set == 580 && file == 1053758 ? 1 :
                    choice == 834 && set == 581 && file == 1053760 ? 10 : -1;
    if (sub < 0) return false;
    int sequence = -1;
    for (size_t i = 0; i < model.anims.size(); ++i)
      if (model.anims[i].animID == 808 && model.anims[i].subAnimID == sub)
      {
        if (sequence >= 0 || !(model.anims[i].flags & 0x20) || (model.anims[i].flags & 0x40)) return false;
        sequence = (int)i;
      }
    if (sequence < 0 || sequence >= MAX_ANIMATED || model.bones.empty()) return false;
    for (const auto & bone : model.bones)
    {
      // Global tracks belong to the ordinary animated pose, not to a facial delta.
      // Check every stored per-sequence track, not just its first sampled value.
      if (!bone.trans.data[sequence].empty() || !bone.rot.data[sequence].empty() ||
          !bone.scale.data[sequence].empty()) return false;
    }
    return true;
  }
}

bool CharDetails::customizationCatalog(QJsonObject & catalog, QString & reason) const
{
  catalog = QJsonObject();
  reason.clear();
  const auto fail = [&](const QString & s) { reason = s; return false; };
  if (!model_ || !model_->ok || !model_->charModelDetails.isChar || model_->infos.raceID < 0 ||
      model_->infos.ChrModelID.empty() || !model_->gamefile || model_->bones.empty() || choicesPerOptionMap_.empty())
    return fail("Customization export requires a player character with customization data and a skeleton.");
  if (!storedChoices_.empty())
    return fail("This character uses a stored NPC appearance. Load a player character to export its customization options.");

  // Remove only the choice-dependent part of a requirement to judge the fixed player context.
  // A requirement with multiple prerequisite options has no established semantics in WMV.
  std::map<uint, bool> fixedAllowed;
  QJsonArray requirements;
  for (const auto & entry : requirements_)
  {
    const auto & r = entry.second;
    const bool allowed = isPlayerRequirement(r.reqType) && raceMaskAllows(r.raceMask, playableRaceBit_) &&
      classRequirement(r.classMask, isDemonHunter_, clientClasses_) == REQUIREMENT_MET && !r.unlockGated;
    fixedAllowed[r.id] = allowed;
    if (!allowed) continue;
    uint option = 0;
    QJsonArray prerequisites;
    for (uint c : r.prerequisiteChoices)
    {
      const auto it = choiceOption_.find(c);
      if (it == choiceOption_.end() || (option && option != it->second))
        return fail(QString("Customization requirement %1 has an unresolved prerequisite rule.").arg(r.id));
      option = it->second;
      prerequisites.append((int)c);
    }
    requirements.append(QJsonObject{{"id", (int)r.id}, {"anyChoice", prerequisites},
      {"regionGroupMask", r.regionGroupMask}, {"overrideArchive", r.overrideArchive}});
  }
  const auto fixed = [&](uint id) { return !id || (fixedAllowed.count(id) && fixedAllowed.at(id)); };
  for (const auto & req : optionRequirement_)
    if (req.second && !requirements_.count(req.second)) return fail("Missing customization option requirement.");
  for (const auto & req : choiceRequirement_)
    if (req.second && !requirements_.count(req.second)) return fail("Missing customization choice requirement.");

  // Dependencies form a DAG in supported data. Enumerate dependency possibilities, not complete
  // character combinations: OR within a prerequisite option, AND between option/choice requirements.
  std::map<uint, int> state;
  std::function<bool(uint)> reachable;
  bool cycle = false;
  const auto reachableRequirement = [&](uint id) {
    if (!fixed(id)) return false;
    if (!id || requirements_.at(id).prerequisiteChoices.empty()) return true;
    bool any = false;
    for (uint c : requirements_.at(id).prerequisiteChoices) any = reachable(c) || any;
    return any;
  };
  reachable = [&](uint c) {
    if (state[c] == 1) { cycle = true; return false; }
    if (state[c] >= 2) return state[c] == 3;
    state[c] = 1;
    const auto co = choiceOption_.find(c);
    bool yes = co != choiceOption_.end();
    if (yes)
    {
      const uint orq = optionRequirement_.at(co->second);
      const uint crq = choiceRequirement_.at(c);
      yes = reachableRequirement(orq) && reachableRequirement(crq);
      // If both requirements constrain the SAME option, their OR lists must intersect.
      if (yes && orq && crq)
      {
        const auto & a = requirements_.at(orq).prerequisiteChoices;
        const auto & b = requirements_.at(crq).prerequisiteChoices;
        if (!a.empty() && !b.empty() && choiceOption_.at(a.front()) == choiceOption_.at(b.front()))
        {
          yes = false;
          for (uint x : a) if (std::find(b.begin(), b.end(), x) != b.end() && reachable(x)) yes = true;
        }
      }
    }
    state[c] = yes ? 3 : 2;
    return yes;
  };
  std::set<uint> included;
  QJsonArray exclusions;
  for (const auto & c : choiceOption_)
    if (reachable(c.first)) included.insert(c.first);
    else exclusions.append((int)c.first);
  if (cycle) return fail("Customization requirements contain a dependency cycle.");
  if (included.empty()) return fail("No player customization options are available in this context.");
  for (const auto & c : currentCustomization_)
    if (!included.count(c.second)) return fail("The current appearance is outside the supported player context.");

  const int chr = model_->infos.ChrModelID[0];
  auto optionNames = GAMEDATABASE.sqlQuery(QString("SELECT ID, Name_Lang FROM ChrCustomizationOption WHERE ChrModelID = %1").arg(chr));
  auto choiceNames = GAMEDATABASE.sqlQuery(QString("SELECT C.ID, C.Name_Lang FROM ChrCustomizationChoice C JOIN ChrCustomizationOption O ON O.ID=C.ChrCustomizationOptionID WHERE O.ChrModelID=%1").arg(chr));
  if (!optionNames.valid || !choiceNames.valid) return fail("Cannot read customization names.");
  std::map<uint, QString> onames, cnames;
  for (const auto & r : optionNames.values) onames[r[0].toUInt()] = r[1];
  for (const auto & r : choiceNames.values) cnames[r[0].toUInt()] = r[1];
  QJsonArray options;
  std::map<uint, QJsonObject> boneSets;
  uint boneSetOption = 0;
  // Numeric option order is the effect/composition order used by rebuildCustomizationElements.
  for (const auto & option : choicesPerOptionMap_)
  {
    QJsonArray choices;
    for (uint c : option.second)
    {
      if (!included.count(c)) continue;
      auto rows = GAMEDATABASE.sqlQuery(QString("SELECT ID, RelatedChrCustomizationChoiceID, ChrCustomizationGeosetID, ChrCustomizationSkinnedModelID, ChrCustomizationMaterialID, ChrCustomizationBoneSetID, ChrCustomizationCondModelID, ChrCustomizationDisplayInfoID FROM ChrCustomizationElement WHERE ChrCustomizationChoiceID=%1 ORDER BY ID").arg(c));
      if (!rows.valid) return fail(QString("Cannot read customization choice %1.").arg(c));
      QJsonArray effects;
      uint choiceBoneSet = 0;
      for (const auto & r : rows.values)
      {
        const uint related = r[1].toUInt();
        if (related && !choiceOption_.count(related)) return fail("Customization effect references an unknown choice.");
        if (related && !included.count(related)) continue;
        if (r[6].toUInt() || r[7].toUInt())
          return fail(QString("Choice %1 uses conditional-model or display-info customization, which cannot yet be exported.").arg(c));
        QJsonObject e{{"id", r[0].toInt()}, {"relatedChoice", (int)related}};
        if (const uint id = r[5].toUInt())
        {
          // The client selects a correction SET, not a stack of independently
          // composable facial deltas. Restrict ambiguous multi-option/choice data.
          if ((boneSetOption && boneSetOption != option.first) || (choiceBoneSet && choiceBoneSet != id))
            return fail("This character requires an unresolved combination of customization bone sets.");
          boneSetOption = option.first;
          choiceBoneSet = id;
          if (!boneSets.count(id))
          {
            auto values = GAMEDATABASE.sqlQuery(QString("SELECT BoneFileDataID, ModelFileDataID FROM ChrCustomizationBoneSet WHERE ID=%1").arg(id));
            if (!values.valid || values.values.size() != 1 || values.values[0][0].toInt() <= 0)
              return fail(QString("Missing customization bone set %1.").arg(id));
            const int fileId = values.values[0][0].toInt();
            if (values.values[0][1].toInt() != model_->gamefile->fileDataId())
              return fail(QString("Customization bone set %1 belongs to a different model.").arg(id));
            auto * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
            QByteArray bytes;
            // An independent, checked storage read must not close/seek a folder-owned
            // GameFile that another animation/texture consumer may still be using.
            if (!folder) return fail("Cannot access the game storage for customization bones.");
            const auto read = folder->readStorageFile(fileId, QString(), bytes, nullptr, 16*1024*1024);
            const bool neutral = read == CASCFolder::ReadOutcome::NotListed &&
              verifiedNeutralFace(*model_, model_->gamefile->fileDataId(), c, id, fileId);
            if (read == CASCFolder::ReadOutcome::NotListed && !neutral)
              return fail(QString("All customizations cannot be exported: bone file %1, required by choice %2, is not listed in this game build.").arg(fileId).arg(c));
            if (read == CASCFolder::ReadOutcome::NotLocal)
              return fail(QString("All customizations cannot be exported: bone file %1, required by choice %2, is not installed locally.").arg(fileId).arg(c));
            if (read != CASCFolder::ReadOutcome::Read && !neutral)
              return fail(QString("Cannot read customization bone file %1.").arg(fileId));
            std::vector<customization::BoneCorrection> corrections;
            std::string error;
            if (!neutral && !customization::readBoneFile(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                             model_->bones.size(), corrections, error))
              return fail(QString("Invalid customization bone file %1: %2").arg(fileId).arg(QString::fromStdString(error)));
            QJsonArray transforms;
            for (const auto & correction : corrections)
            {
              const auto & bone = model_->bones[correction.bone];
              if (bone.boneDef.flags & 0x7f)
                return fail("A customization bone uses unsupported inheritance or billboard rules.");
              QJsonArray matrix;
              for (float value : correction.matrix) matrix.append((double)value);
              transforms.append(QJsonObject{{"bone", (int)correction.bone}, {"matrix", matrix}});
            }
            boneSets[id] = QJsonObject{{"id", (int)id}, {"fileId", fileId},
              {"modelFileId", model_->gamefile->fileDataId()}, {"transforms", transforms}};
            if (neutral)
            {
              boneSets[id]["resolution"] = "verified-neutral-face";
              boneSets[id]["facePoseSubAnimation"] = c == 825 ? 1 : 10;
            }
          }
          e["boneSet"] = (int)id;
        }
        if (r[2].toUInt())
        {
          auto values = GAMEDATABASE.sqlQuery(QString("SELECT GeosetType, GeosetID FROM ChrCustomizationGeoset WHERE ID=%1").arg(r[2]));
          if (!values.valid || values.values.empty()) return fail("Missing customization geoset.");
          QJsonArray geosets;
          QJsonArray groups;
          for (const auto & v : values.values)
          {
            // Keep the authored group even for None (0 or -1). Deriving it
            // from group*100-1 would incorrectly address the previous group.
            groups.append(v[0].toInt());
            if (v[1].toInt() > 0) geosets.append(v[0].toInt()*100 + v[1].toInt());
          }
          e["geosets"] = geosets;
          e["geosetGroups"] = groups;
        }
        else if (r[3].toUInt())
        {
          auto values = GAMEDATABASE.sqlQuery(QString("SELECT CollectionsFileDataID, GeosetType, GeosetID FROM ChrCustomizationSkinnedModel WHERE ID=%1").arg(r[3]));
          if (!values.valid || values.values.size() != 1 || values.values[0][0].toInt() <= 0) return fail("Missing customization collection model.");
          const auto & v = values.values[0];
          e["model"] = v[0].toInt(); e["group"] = v[1].toInt(); e["variant"] = v[2].toInt();
        }
        else if (r[4].toUInt())
        {
          auto values = GAMEDATABASE.sqlQuery(QString("SELECT L.Layer, L.TextureSectionTypeBitMask, L.TextureType, L.BlendMode, T.FileDataID FROM ChrCustomizationMaterial M LEFT JOIN TextureFileData T ON M.MaterialResourcesID=T.MaterialResourcesID LEFT JOIN ChrModelTextureLayer L ON M.ChrModelTextureTargetID=L.ChrModelTextureTargetID1 AND L.CharComponentTextureLayoutsID=%1 WHERE M.ID=%2").arg(model_->infos.textureLayoutID).arg(r[4]));
          if (!values.valid || values.values.empty()) return fail("Missing customization texture layer.");
          auto v = values.values[0];
          std::set<int> files;
          for (const auto & other : values.values)
          {
            // Several file variants can share one material resource, but different
            // layout/layer recipes are not interchangeable.
            for (int field = 0; field < 4; ++field)
              if (other[field] != v[field]) return fail("Ambiguous customization texture layout.");
            files.insert(other[4].toInt());
          }
          if (files.size() > 1)
          {
            int best = -1, selectedFile = 0;
            bool ambiguous = false;
            for (int file : files)
            {
              auto variants = GAMEDATABASE.sqlQuery(QString("SELECT RaceID, GenderIndex, ClassID FROM ComponentTextureFileData WHERE ID=%1").arg(file));
              if (!variants.valid) return fail("Cannot resolve customization texture variants.");
              for (const auto & variant : variants.values)
              {
                const int race = variant[0].toInt(), sex = variant[1].toInt(), cls = variant[2].toInt();
                int rank = -1, targetSex = model_->infos.sexID;
                if (race == model_->infos.raceID) rank = 30;
                else if (model_->infos.textureFallbackRaceID > 0 && race == model_->infos.textureFallbackRaceID)
                { rank = 20; targetSex = model_->infos.textureFallbackSexID; }
                else if (race == RACE_ANY) rank = 10;
                if (rank < 0 || (sex != targetSex && sex != GENDER_ANY) ||
                    (cls != CLASS_ANY && !(isDemonHunter_ && cls == CLASS_DEMONHUNTER))) continue;
                rank += (sex == targetSex ? 2 : 0) + (cls == CLASS_DEMONHUNTER ? 1 : 0);
                if (rank > best) { best = rank; selectedFile = file; ambiguous = false; }
                else if (rank == best && selectedFile != file) ambiguous = true;
              }
            }
            if (!selectedFile || ambiguous)
              return fail(QString("Choice %1, material %2 has unresolved texture variants.").arg(c).arg(r[4]));
            v[4] = QString::number(selectedFile);
          }
          for (const auto & cell : v) if (cell.isEmpty()) return fail("Incomplete customization texture layer.");
          const int mask = v[1].toInt();
          if (mask != -1 && mask != 0 && (mask < 0 || (mask & (mask-1)))) return fail("Unsupported multi-region texture layer.");
          const int region = mask == 1 ? 0 : bitMaskToSectionType(mask);
          if (v[4].toInt() <= 0) return fail("Missing customization texture file ID.");
          e["texture"] = QJsonObject{{"fileId", v[4].toInt()}, {"region", region}, {"layer", v[0].toInt()},
            {"type", v[2].toInt()}, {"blendMode", v[3].toInt()}};
        }
        effects.append(e);
      }
      choices.append(QJsonObject{{"id", (int)c}, {"name", cnames[c]}, {"requirement", (int)choiceRequirement_.at(c)}, {"effects", effects}});
    }
    if (!choices.isEmpty()) options.append(QJsonObject{{"id", (int)option.first}, {"name", onames[option.first]},
      {"requirement", (int)optionRequirement_.at(option.first)}, {"choices", choices}});
  }
  QJsonArray selection;
  QJsonArray boneSetValues;
  for (const auto & set : boneSets) boneSetValues.append(set.second);
  for (const auto & c : currentCustomization_) selection.append(QJsonObject{{"option", (int)c.first}, {"choice", (int)c.second}});
  catalog = QJsonObject{{"schema", "wmv.customization"}, {"version", 1}, {"chrModelId", chr},
    {"modelFileId", (int)model_->gamefile->fileDataId()}, {"raceId", model_->infos.raceID}, {"sexId", model_->infos.sexID},
    {"context", QJsonObject{{"demonHunter", isDemonHunter_}, {"scope", "current-player-context"},
      {"excludedChoiceIds", exclusions}, {"unlocks", "viewer-available-only"}}},
    {"options", options}, {"requirements", requirements}, {"defaultSelection", selection}, {"boneSets", boneSetValues}};
  return true;
}
