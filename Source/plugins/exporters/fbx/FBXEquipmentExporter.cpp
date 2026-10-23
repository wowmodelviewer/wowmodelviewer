#include "FBXExporter.h"
#include "../../../games/wow/EquipmentFolderName.h"
#include "WoWModel.h"
#include "WoWItem.h"
#include "Attachment.h"
#include "ModelRenderPass.h"
#include "Game.h"
#include "GameFolder.h"
#include "GameFile.h"
#include "CharTexture.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <memory>
#include <set>
#include <algorithm>
#include <cstdio>

namespace
{
QString folderName(QString name)
{
  name.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
  name = name.trimmed().left(80);
  while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
  if (name.isEmpty() || name == "." || name == "..") name = "Equipment";
  if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) name.prepend('_');
  return name;
}
bool writeJson(const QString & path, const QJsonObject & object)
{
  QSaveFile file(path);
  const auto bytes = QJsonDocument(object).toJson();
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool isWeapon(int slot) { return slot == CS_HAND_RIGHT || slot == CS_HAND_LEFT; }
const char * slotName(int slot)
{
  static const char * names[] = {"Head", "Shoulders", "Feet", "Waist", "Shirt", "Legs", "Chest", "Wrists", "Hands", "MainHand", "OffHand", "Back", "Tabard", "Quiver"};
  return slot >= 0 && slot < NUM_CHAR_SLOTS ? names[slot] : "Item";
}

// The ordinary exporter preserves the model's full vertex array, including unused
// vertices. Equipment packages must not carry the hidden character surface with them.
// Only call this on the private copy, after posing and before the final FBX export.
bool compactEquipmentGeometry(WoWModel * model)
{
  std::vector<int> remap(model->origVertices.size(), -1);
  std::vector<ModelVertex> vertices;
  for (auto * pass : model->passes)
  {
    if (!pass->init(true)) continue;
    pass->deinit();
    const auto * geoset = model->geosets[pass->geoIndex];
    if ((size_t)geoset->istart + geoset->icount > model->indices.size()) return false;
    for (size_t i = geoset->istart; i < (size_t)geoset->istart + geoset->icount; ++i)
    {
      const size_t old = model->indices[i];
      if (old >= remap.size()) return false;
      if (remap[old] < 0) { remap[old] = (int)vertices.size(); vertices.push_back(model->origVertices[old]); }
    }
  }
  for (auto & index : model->indices) index = index < remap.size() && remap[index] >= 0 ? remap[index] : 0;
  model->origVertices.swap(vertices);
  return true;
}
}

bool FBXExporter::shareEquipmentTextures()
{
  std::map<QString, QString> relativeNames;
  std::map<std::wstring, GLuint> sharedTextures;
  const QDir sceneFolder(QFileInfo(QString::fromStdWString(m_filename)).absolutePath());
  for (const auto & texture : m_texturesToExport)
  {
    const QString original = QString::fromStdWString(texture.first);
    QFile source(original);
    if (!source.open(QIODevice::ReadOnly) || source.size() == 0)
    { m_lastError = L"An equipment texture could not be read for packaging."; return false; }
    const QByteArray bytes = source.readAll();
    source.close();
    const QString shared = QDir(m_equipmentTextureRoot).filePath(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + ".png");
    if (!QFileInfo::exists(shared))
    {
      QSaveFile output(shared);
      if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
      { m_lastError = L"An equipment texture could not be saved in the shared folder."; return false; }
    }
    relativeNames[QFileInfo(original).fileName()] = sceneFolder.relativeFilePath(shared);
    sharedTextures[shared.toStdWString()] = texture.second;
  }
  const auto relocate = [&](std::string & file) {
    const auto found = relativeNames.find(QString::fromStdString(file));
    if (found != relativeNames.end()) file = found->second.toStdString();
  };
  for (auto & material : m_materialMeta)
  {
    relocate(material.textureFile);
    for (auto & unit : material.units) relocate(unit.file);
  }
  for (int i = 0; i < m_p_scene->GetTextureCount(); ++i)
  {
    auto * candidate = m_p_scene->GetTexture(i);
    if (QString::fromUtf8(candidate->GetClassId().GetName()) != "FbxFileTexture") continue;
    auto * texture = static_cast<FbxFileTexture *>(candidate);
    const auto found = relativeNames.find(QFileInfo(QString::fromUtf8(texture->GetFileName())).fileName());
    if (found == relativeNames.end()) continue;
    texture->SetRelativeFileName(found->second.toUtf8().constData());
    texture->SetFileName(sceneFolder.absoluteFilePath(found->second).toUtf8().constData());
  }
  for (const auto & texture : m_texturesToExport) QFile::remove(QString::fromStdWString(texture.first));
  m_texturesToExport.swap(sharedTextures);
  return true;
}

bool FBXExporter::exportEquipmentPackage(WoWModel * source, const std::wstring & target)
{
  m_lastError.clear();
  const auto fail = [&](const QString & error) { m_lastError = error.toStdWString(); return false; };
  const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(m_equipmentRequest));
  const auto request = document.object();
  if (!source || !source->charModelDetails.isChar || source->infos.isNPC || request["version"].toInt() != 1)
    return fail("Equipment export requires a playable character and a valid request.");
  std::vector<WoWItem *> items;
  std::set<int> selectedSlots;
  for (const auto & value : request["slots"].toArray())
  {
    const int slot = value.toInt(-1);
    if (slot < 0 || slot >= NUM_CHAR_SLOTS || !selectedSlots.insert(slot).second) return fail("Invalid or repeated equipment slot.");
    auto * item = source->getItem((CharSlots)slot);
    if (!item || !item->isEquipped()) return fail("A selected slot is no longer equipped.");
    items.push_back(item);
  }
  struct Body : RaceInfos { int option = 0, choice = 0; QString suffix; };
  std::vector<Body> bodies;
  std::set<std::pair<int, int>> bodyKeys;
  for (const auto & value : request["bodies"].toArray())
  {
    const auto body = value.toObject();
    Body info;
    const int race = body["race"].toInt(-1), sex = body["sex"].toInt(-1);
    if (sex < 0 || sex > 1 || !bodyKeys.emplace(race, sex).second ||
        !RaceInfos::getRaceInfosForRaceSex(race, sex, info) || info.isNPC || info.modelFileID <= 0)
      return fail("An unavailable body was selected.");
    // The base race/sex remains at its original path. Orc male additionally
    // resolves the authored conditional body, using its own model and item load.
    if (race == 2 && sex == 0 && !info.ChrModelID.empty())
    {
      const auto rows = GAMEDATABASE.sqlQuery(QString("SELECT C.ID, C.ChrCustomizationOptionID, E.RelatedChrCustomizationChoiceID, M.FileDataID FROM ChrCustomizationElement E JOIN ChrCustomizationChoice C ON C.ID=E.ChrCustomizationChoiceID JOIN ChrCustomizationOption O ON O.ID=C.ChrCustomizationOptionID JOIN ChrCustomizationCondModel B ON B.ID=E.ChrCustomizationCondModelID JOIN CreatureModelData M ON M.ID=B.CreatureModelDataID WHERE O.ChrModelID=%1 AND E.ChrCustomizationCondModelID<>0").arg(info.ChrModelID[0]));
      if (!rows.valid) return fail("Cannot resolve equipment posture models.");
      if (!rows.values.empty())
      {
        if (rows.values.size() != 1 || rows.values[0][2].toInt() != 0 || rows.values[0][3].toInt() <= 0)
          return fail("Unsupported equipment posture conditions.");
        const auto & row = rows.values[0];
        info.option = row[1].toInt();
        const auto choices = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID=%1 AND ID<>%2").arg(info.option).arg(row[0]));
        if (!choices.valid || choices.values.size() != 1) return fail("Cannot resolve the base equipment posture.");
        info.choice = choices.values[0][0].toInt();
        bodies.push_back(info);
        info.choice = row[0].toInt(); info.modelFileID = row[3].toInt();
        info.suffix = QString("/postures/choice-%1").arg(info.choice);
      }
    }
    bodies.push_back(info);
  }
  if (items.empty() || bodies.empty()) return fail("Select at least one item and one body.");
  const bool asSet = request["asSet"].toBool();
  std::vector<WoWItem *> armor;
  for (auto * item : items) if (!isWeapon(item->slot())) armor.push_back(item);
  const QString setFolder = request["setName"].toString();
  if (asSet && !armor.empty())
  {
    const QString error = equipmentSetNameError(setFolder);
    if (!error.isEmpty()) return fail(error);
  }
  // Older headless callers may still request the original single-package layout.
  const bool direct = request.contains("destination");
  const QFileInfo destination(direct ? request["destination"].toString() : QFileInfo(QString::fromStdWString(target)).absolutePath());
  if (!destination.isDir() || (direct && request["destination"].toString().isEmpty()))
    return fail("The destination folder does not exist.");
  const QString packagePath = QString::fromStdWString(target) + ".equipment";
  if (!direct && QFileInfo::exists(packagePath)) return fail("The equipment package already exists. Choose another name.");
  QTemporaryDir staging(destination.absoluteFilePath() + "/.wmv-equipment-XXXXXX");
  if (!staging.isValid()) return fail("Cannot create the equipment folder.");
  const QDir root(staging.path());
  std::map<int, QString> itemFolders;
  QStringList outputFolders;
  std::set<QString> usedNames;
  if (asSet && !armor.empty()) { outputFolders.append(setFolder); usedNames.insert(setFolder.toCaseFolded()); }
  for (auto * item : items)
  {
    const bool topLevel = isWeapon(item->slot()) || !asSet;
    QString name = direct && topLevel ? folderName(item->name()) :
      QString("%1_%2_%3_%4").arg(slotName(item->slot())).arg(folderName(item->name())).arg(item->id()).arg(item->displayId());
    if (topLevel)
    {
      // Preserve readable names; disambiguate only duplicate selections.
      if (usedNames.count(name.toCaseFolded())) name += QString("_%1_%2").arg(item->id()).arg(slotName(item->slot()));
      if (!usedNames.insert(name.toCaseFolded()).second) return fail("Selected equipment folder names conflict.");
      outputFolders.append(name);
    }
    itemFolders[item->slot()] = name;
  }
  if (direct)
  {
    for (const auto & name : outputFolders)
      if (QFileInfo::exists(QDir(destination.absoluteFilePath()).filePath(name)))
        return fail(QString("The destination already contains '%1'. Choose another folder or set name.").arg(name));
  }
  else if (usedNames.count("textures") || usedNames.count("catalog.json") || usedNames.count("readme.txt"))
    return fail("This name is reserved for package resources.");
  QJsonArray entries, setEntries, setVariants;
  int variantIndex = 0;
  const int variantTotal = (int)(bodies.size() * (items.size() + (asSet && !armor.empty() ? 1 : 0)));

  // Resolve each selection with the same item loader, fallback tables and merging as the viewer.
  // This never changes the character that the user is looking at.
  const auto variant = [&](const std::vector<WoWItem *> & selected, const Body & info,
                           const QString & relative, bool geometry, QJsonObject & result) -> bool {
    QString label = QString("%1 - %2, %3").arg(geometry ? selected.front()->name() : request["setName"].toString())
      .arg(QString::fromStdString(info.nameLang)).arg(info.sexID ? "Female" : "Male");
    label.replace('\n', ' '); label.replace('\r', ' ');
    std::printf("WMVEXPORT-PROGRESS: EQUIPMENT %d %d %s\n", ++variantIndex, variantTotal, label.toUtf8().constData());
    std::printf("WMVEXPORT-PROGRESS: STAGE RESOLVING\n"); std::fflush(stdout);
    auto * file = GAMEDIRECTORY.getFile((uint)info.modelFileID);
    if (!file) return fail(QString("Missing body model %1 for %2 (%3).").arg(info.modelFileID).arg(QString::fromStdString(info.nameLang)).arg(info.sexID));
    std::unique_ptr<WoWModel> model(new WoWModel(file, true));
    if (!model->ok || !model->setRaceSex(info.raceID, info.sexID)) return fail("Cannot load the selected body.");
    model->charModelDetails.isChar = true;
    model->modelType = MT_CHAR;
    Attachment attachment(nullptr, model.get(), 0, -1);
    for (int slot = 0; slot < NUM_CHAR_SLOTS; ++slot) model->addChild(new WoWItem((CharSlots)slot));
    model->bSheathe = source->bSheathe;
    model->td = source->td;
    model->cd.autoHideGeosetsForHeadItems = source->cd.autoHideGeosetsForHeadItems;
    if (info.option)
    {
      auto selection = model->cd.captureAppearance().selection;
      selection[(uint)info.option] = (uint)info.choice;
      model->cd.applyAppearance(selection, false, true);
    }
    model->refresh();
    const auto baseGroups = model->cd.geosets;
    std::set<uint> declaredGroups;
    QJsonArray itemRecords;
    for (auto * original : selected)
    {
      auto * item = model->getItem(original->slot());
      item->restore(original->id(), original->displayId(), original->level());
      if (!item->isEquipped() || !item->showsAnything())
        return fail(QString("%1 has no resolved resources for race %2, sex %3.").arg(original->name()).arg(info.raceID).arg(info.sexID));
      for (const auto & group : item->geosetGroups()) declaredGroups.insert((uint)group.first);
      itemRecords.append(QJsonObject{{"slot", (int)item->slot()}, {"itemId", item->id()}, {"displayId", item->displayId()}, {"level", item->level()}, {"name", original->name()}});
    }
    model->refresh();
    QJsonArray overrides, hidden, layers, suppressed;
    for (const auto & group : model->cd.geosets)
      if (declaredGroups.count(group.first) || !baseGroups.count(group.first) || baseGroups.at(group.first) != group.second)
        overrides.append(QJsonObject{{"group", (int)group.first}, {"variant", (int)group.second}});
    for (int group : model->equipmentHiddenGroups()) hidden.append(group);
    if (model->getItem(CS_CHEST)->isEquipped() || model->getItem(CS_SHIRT)->isEquipped()) suppressed.append((int)CR_TORSO_UPPER);
    if (model->getItem(CS_PANTS)->isEquipped()) suppressed.append((int)CR_LEG_UPPER);
    const QString textureFolder = direct ? relative.section('/', 0, 0) + "/textures" : "textures";
    if (!root.mkpath(textureFolder)) return fail("Cannot create the texture folder.");
    const QDir folder(root.filePath(relative));
    if (!root.mkpath(relative)) return fail("Cannot create a body variant folder.");
    std::set<int> textureIds;
    for (const auto & layer : model->tex.components())
    {
      if (!layer.file) return fail("An equipment texture is missing.");
      const int id = layer.file->fileDataId();
      const bool customization = std::any_of(model->cd.textures.begin(), model->cd.textures.end(), [&](const CharDetails::TextureCustomization & t) {
        return t.type == 1 && (int)t.fileId == id && t.region == layer.region && (int)t.layer == layer.layer && (int)t.blendMode == layer.blendMode;
      });
      if (customization) continue;
      layers.append(QJsonObject{{"fileId", id}, {"region", layer.region}, {"layer", layer.layer}, {"blendMode", layer.blendMode}});
      textureIds.insert(id);
    }
    LayoutSize size;
    std::map<int, CharRegionCoords> regions;
    if (!CharTexture::layout(info.textureLayoutID, size, regions)) return fail("The body texture layout is unavailable.");
    QJsonArray regionArray;
    for (const auto & r : regions) regionArray.append(QJsonObject{{"id", r.first}, {"x", r.second.xpos}, {"y", r.second.ypos}, {"width", r.second.width}, {"height", r.second.height}});
    result = QJsonObject{{"race", info.raceID}, {"sex", info.sexID}, {"modelFileId", info.modelFileID},
      {"items", itemRecords}, {"groupOverrides", overrides}, {"hideGroups", hidden}, {"bodyLayers", layers}, {"suppressCustomizationRegions", suppressed},
      {"textureLayout", QJsonObject{{"width", size.width}, {"height", size.height}, {"regions", regionArray}, {"origin", "top-left"}}}};
    result["exporter"] = "wmv-fbx-posture-2";
    result["build"] = QString::fromStdString(m_exportBuild);
    result["product"] = QString::fromStdString(m_exportProduct);
    if (info.option) { result["option"] = info.option; result["choice"] = info.choice; }
    QJsonArray textures;
    for (int id : textureIds)
    {
      const QString shared = root.filePath(textureFolder + QString("/%1.png").arg(id));
      const QString path = folder.relativeFilePath(shared);
      if (!QFileInfo::exists(shared))
      {
        const auto image = CharTexture::sourceImage(GAMEDIRECTORY.getFile((uint)id));
        if (image.isNull() || !image.save(shared, "PNG")) return fail(QString("Cannot export equipment texture %1.").arg(id));
      }
      textures.append(QJsonObject{{"fileId", id}, {"file", path}});
    }
    result["textures"] = textures;
    if (geometry)
    {
      model->rebuildExportGeometry();
      for (size_t i = 0; i < model->geosets.size(); ++i)
      {
        bool keep = false;
        if (i < model->ownGeosetCount())
        {
          const int id = model->geosets[i]->id;
          // Only the clothing variants requested by equipment, never the body, hair or face.
          // Tabards and cloaks are equipment-only surfaces even when their authored
          // variant is 1. Other *01 variants belong to the base character.
          const bool equipmentOnly = id / 100 == CG_TABARD || id / 100 == CG_CLOAK;
          keep = id >= 400 && declaredGroups.count(id / 100) && (id % 100 > 1 || (equipmentOnly && id % 100 == 1));
        }
        else
          for (const auto & part : model->mergedParts())
            if (i >= part.geosetStart && i < part.geosetStart + part.geosetCount)
              for (auto * item : *model) if (item->isEquipped() && item->mergedModel() == part.model) keep = true;
        model->geosets[i]->display = model->geosets[i]->display && keep;
      }
      for (auto * item : *model)
        for (const auto & part : item->models())
        {
          if (part.second->mirrored_) return fail("This equipment uses mirrored geometry which is not supported by equipment packages yet.");
          int id = -1;
          for (const auto * child : attachment.children) if (child->model() == part.second && child->slot == item->slot()) id = child->id;
          if (id < 0 || id >= (int)WoWModel::ATT_MAX) return fail("An equipment attachment could not be resolved.");
          const int lookup = model->attLookup[id];
          if (lookup < 0 || lookup >= (int)model->atts.size() || model->atts[lookup].bone >= model->bones.size()) return fail("An equipment attachment has no valid bone.");
        }
      for (const auto & part : model->mergedParts())
      {
        bool equipment = false;
        for (auto * item : *model) equipment = equipment || (item->isEquipped() && item->mergedModel() == part.model);
        if (!equipment) continue;
        for (const auto & vertex : part.model->sourceVertices())
          for (int w = 0; w < 4; ++w)
            if (vertex.weights[w])
            {
              const size_t from = vertex.bones[w];
              if (from >= part.boneMap.size() || from >= part.model->bones.size()) return fail("Equipment geometry has an invalid source bone.");
              const int to = part.boneMap[from];
              if (to < 0 || to > 255 || to >= (int)model->bones.size()) return fail("Equipment geometry cannot be mapped to this body's skeleton.");
              if (part.model->bones[from].boneDef.unknown != model->bones[to].boneDef.unknown ||
                  glm::length(part.model->bones[from].pivot - model->bones[to].pivot) > 0.0002f)
                return fail("Equipment geometry has an incompatible bone mapping for this body.");
            }
      }
      FBXExporter scene;
      scene.setExportOptions(true, true, true, false);
      scene.setComponentRawExport(true);
      scene.m_packageScene = true;
      scene.m_equipmentTextureRoot = root.filePath(textureFolder);
      model->updatePose();
      if (!compactEquipmentGeometry(model.get())) return fail("Equipment geometry contains invalid vertex indices.");
      for (auto * item : *model)
        for (const auto & part : item->models())
          if (!compactEquipmentGeometry(part.second)) return fail("An attached item contains invalid vertex indices.");
      if (!scene.exportModel(model.get(), folder.filePath("equipment.fbx").toStdWString())) { m_lastError = scene.lastError(); return false; }
      result["fbx"] = "equipment.fbx";
      result["materialCatalog"] = "equipment.fbx.wmvmat.json";
      result["geometry"] = scene.m_packageGeometry;
      QJsonArray bones;
      for (const auto & bone : scene.m_boneNodes) bones.append(QJsonObject{{"index", bone.first}, {"node", QString::fromUtf8(bone.second->GetName())}, {"parent", model->bones[bone.first].boneDef.parent}});
      result["bones"] = bones;
      for (const auto & material : scene.m_materialMeta)
        for (const auto & unit : material.units)
          if (unit.uvSource != 2 && unit.file.empty()) return fail("An equipment material has an unresolved texture unit.");
      for (const auto & texture : scene.m_texturesToExport)
        if (QFileInfo(QString::fromStdWString(texture.first)).size() <= 0) return fail("An equipment material texture could not be saved.");
      if (!QFileInfo::exists(folder.filePath("equipment.fbx.wmvmat.json"))) return fail("The material catalog could not be saved.");
    }
    return writeJson(folder.filePath("catalog.json"), result) || fail("Cannot write the variant catalog.");
  };

  const auto appendVariant = [](QJsonArray & variants, const Body & body, const QString & path) {
    QJsonObject entry{{"race", body.raceID}, {"sex", body.sexID}, {"modelFileId", body.modelFileID}, {"catalog", path}};
    if (!body.option) { variants.append(entry); return; }
    QJsonObject posture{{"option", body.option}, {"choice", body.choice}, {"modelFileId", body.modelFileID}, {"catalog", path}};
    for (int i = 0; i < variants.size(); ++i)
    {
      auto existing = variants[i].toObject();
      if (existing["race"].toInt() != body.raceID || existing["sex"].toInt() != body.sexID) continue;
      auto postures = existing["bodyVariants"].toArray(); postures.append(posture);
      existing["bodyVariants"] = postures; variants[i] = existing; return;
    }
    entry["bodyVariants"] = QJsonArray{posture}; variants.append(entry);
  };
  for (auto * item : items)
  {
    const bool weapon = isWeapon(item->slot());
    const QString name = itemFolders.at(item->slot());
    const QString relative = asSet && !weapon ? setFolder + "/" + name : name;
    QJsonArray variants;
    for (const auto & body : bodies)
    {
      const QString bodyFolder = QString("race-%1/%2").arg(body.raceID).arg(body.sexID ? "female" : "male") + body.suffix;
      QJsonObject resolved;
      if (!variant({item}, body, relative + "/" + bodyFolder, true, resolved)) return false;
      appendVariant(variants, body, bodyFolder + "/catalog.json");
    }
    if (!writeJson(root.filePath(relative + "/catalog.json"), QJsonObject{{"exporter", "wmv-fbx-posture-2"}, {"version", 1}, {"type", weapon ? "weapon" : "armor"}, {"slot", (int)item->slot()}, {"name", item->name()}, {"build", QString::fromStdString(m_exportBuild)}, {"product", QString::fromStdString(m_exportProduct)}, {"rules", "wmv-player-layers-v1; resolved-equipment-groups-v1"}, {"variants", variants}})) return fail("Cannot write the item catalog.");
    entries.append(QJsonObject{{"slot", (int)item->slot()}, {"catalog", relative + "/catalog.json"}});
    if (!weapon) setEntries.append(QJsonObject{{"slot", (int)item->slot()}, {"catalog", name + "/catalog.json"}});
  }
  if (asSet && !armor.empty())
  {
    for (const auto & body : bodies)
    {
      const QString relative = QString("rules/race-%1/%2").arg(body.raceID).arg(body.sexID ? "female" : "male") + body.suffix;
      QJsonObject resolved;
      if (!variant(armor, body, setFolder + "/" + relative, false, resolved)) return false;
      appendVariant(setVariants, body, relative + "/catalog.json");
    }
    if (!writeJson(root.filePath(setFolder + "/catalog.json"), QJsonObject{{"exporter", "wmv-fbx-posture-2"}, {"version", 1}, {"type", "armor-set"}, {"name", request["setName"]}, {"build", QString::fromStdString(m_exportBuild)}, {"product", QString::fromStdString(m_exportProduct)}, {"rules", "wmv-player-layers-v1; resolved-equipment-groups-v1"}, {"pieces", setEntries}, {"variants", setVariants}})) return fail("Cannot write the set catalog.");
  }
  const QJsonObject catalog{{"version", 1}, {"type", "wmv-equipment"}, {"build", QString::fromStdString(m_exportBuild)}, {"product", QString::fromStdString(m_exportProduct)},
    {"items", entries}, {"set", asSet && !armor.empty() ? setFolder + "/catalog.json" : QString()},
    {"rules", "wmv-player-layers-v1; resolved-equipment-groups-v1"}};
  if (!direct && !writeJson(root.filePath("catalog.json"), catalog)) return fail("Cannot write the equipment catalog.");
  QFile readme(root.filePath("README.txt"));
  const QByteArray instructions = "WMV equipment package v1\nEach piece has its own folder. An armor set groups those folders and supplies combined rules. Weapons remain independent.\nRead catalog.json; paths are relative to the catalog that contains them. Each selected race/sex has its own resolved variant. When bodyVariants is present, select its modelFileId matching the active character body, for both pieces and combined rules; never relabel or reuse another posture. Catalog paths are relative to the containing index.\nFBX contains equipment and supplemental clothing geosets on the body's reference skeleton, without animations or the character surface. Retarget to the matching body's joints by bone index/name.\nGeometry is addressed by FBX node and material/submesh. Owner base means supplemental body clothing geometry; equipment:<slot> means attached or merged equipment.\nApply groupOverrides after character customization, then hideGroups. Add bodyLayers by ascending layer; suppress the listed customization regions. For a complete set use its combined rules, not sequential per-piece overrides.\nTextures use top-left atlas regions; blend 4 multiply, 6 overlay, otherwise source-over. Recompose skin for texture types 1/8; retain other FBX material bindings. Read the material sidecar for blend modes, UVs and shaders.\nA compatible consumer is required; this does not install an importer into another project. Unsupported/missing resources fail the complete export.\n";
  if (!direct)
  {
    if (!readme.open(QIODevice::WriteOnly) || readme.write(instructions) != instructions.size()) return fail("Cannot write equipment instructions.");
    readme.close();
  }
  if (direct)
  {
    QStringList published;
    const QDir output(destination.absoluteFilePath());
    for (const auto & name : outputFolders)
    {
      QSaveFile itemReadme(root.filePath(name + "/README.txt"));
      if (!itemReadme.open(QIODevice::WriteOnly) || itemReadme.write(instructions) != instructions.size() || !itemReadme.commit())
        return fail("Cannot write equipment instructions.");
    }
    for (const auto & name : outputFolders)
    {
      if (!QDir().rename(root.filePath(name), output.filePath(name)))
      {
        bool restored = true;
        for (const auto & previous : published)
          restored = QDir().rename(output.filePath(previous), root.filePath(previous)) && restored;
        return fail(restored ? "Cannot publish equipment folders. No output was published." :
          "Cannot publish all equipment folders. Some completed folders remain in the destination.");
      }
      published.append(name);
    }
    return true;
  }
  if (!QDir().rename(staging.path(), packagePath)) return fail("Cannot publish the equipment package.");
  staging.setAutoRemove(false);
  return true;
}
