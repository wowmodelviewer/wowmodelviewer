#include "FBXExporter.h"
#include "FBXHeaders.h"
#include "WoWModel.h"
#include "WoWItem.h"
#include "Attachment.h"
#include "ModelRenderPass.h"
#include "Game.h"
#include "GameFile.h"
#include "GameFolder.h"
#include "CharTexture.h"
#include "CustomizationBoneTransform.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <memory>
#include <set>
#include <algorithm>
#include <cstdio>

namespace
{
  void progress(const char * stage)
  { std::printf("WMVEXPORT-PROGRESS: STAGE %s\n", stage); std::fflush(stdout); }

  QJsonArray groups(const std::map<uint, uint> & values)
  {
    QJsonArray result;
    for (const auto & g : values) result.append(QJsonObject{{"group", (int)g.first}, {"variant", (int)g.second}});
    return result;
  }

  QString partOwner(WoWModel * model, size_t geoset)
  {
    if (geoset < model->ownGeosetCount()) return "base";
    for (const auto & p : model->mergedParts())
      if (geoset >= p.geosetStart && geoset < p.geosetStart + p.geosetCount)
      {
        if (p.model->mergedModelType == -1)
          for (auto * item : *model)
            if (item->mergedModel() == p.model) return QString("equipment:%1").arg((int)item->slot());
        return QString("customization:%1").arg(p.model->gamefile->fileDataId());
      }
    return QString();
  }

  bool saveJson(const QString & path, const QJsonObject & object)
  {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  }
}

bool FBXExporter::canExportCustomizations(Model * source, std::wstring & reason) const
{
  auto * model = dynamic_cast<WoWModel *>(source);
  QJsonObject catalog;
  QString message;
  const bool yes = model && model->cd.customizationCatalog(catalog, message);
  reason = yes ? L"" : (message.isEmpty() ? L"Customization export is not available for this model." : message.toStdWString());
  return yes;
}

// A render pass becomes one FBX material/submesh. The catalog targets that material on a
// named node, not a vertex or polygon index that a Unity/DCC importer may reorder.
bool FBXExporter::describeCustomizationGeometry()
{
  m_packageGeometry = QJsonArray();
  const auto describe = [&](WoWModel * model, FbxNode * node, const QString & attachedOwner) {
    if (!node || !node->GetMesh()) return false;
    int material = 0;
    for (size_t pi = 0; pi < model->passes.size(); ++pi)
    {
      auto * pass = model->passes[pi];
      if (!pass->init(true)) continue;
      pass->deinit();
      if (material >= node->GetMaterialCount()) return false;
      const size_t gi = pass->geoIndex;
      if (gi >= model->geosets.size()) return false;
      const QString owner = attachedOwner.isEmpty() ? partOwner(model, gi) : attachedOwner;
      if (owner.isEmpty()) return false;
      QJsonArray units;
      const int textureSlots[] = {pass->tex, pass->tex2, pass->tex3, pass->tex4};
      for (int u = 0; u < std::max(1, std::min(4, (int)pass->textureCount)); ++u)
      {
        if (pass->uvSource[u] == 2)
        { units.append(QJsonObject{{"unit", u}, {"textureType", 0}, {"environment", true}}); continue; }
        const int slot = textureSlots[u];
        int type = model->textureTypeForSlot(slot);
        // The viewer rewrites merged skin slots to a bound fallback. The catalog
        // must keep the authored type so another selection can replace that fallback.
        if (slot >= TEXTURE_MAX)
          for (const auto & part : model->mergedParts())
            if (part.mergeIndex == slot / TEXTURE_MAX)
              type = part.model->textureTypeForSlot(slot % TEXTURE_MAX);
        if (type < 0) return false;
        const int baseType = type % TEXTURE_MAX;
        const bool customization = !owner.startsWith("equipment:") || baseType == 1 || baseType == 8;
        units.append(QJsonObject{{"unit", u}, {"textureType", baseType}, {"customizationBinding", customization}});
      }
      m_packageGeometry.append(QJsonObject{{"owner", owner}, {"geoset", (int)model->geosets[gi]->id},
        {"node", QString::fromUtf8(node->GetName())}, {"material", QString::fromUtf8(node->GetMaterial(material)->GetName())},
        {"materialIndex", material}, {"textureUnits", units}, {"defaultVisible", model->showModel}});
      ++material;
    }
    return material == node->GetMaterialCount();
  };
  m_p_meshNode->SetName("WMVCharacter");
  if (!describe(m_p_model, m_p_meshNode, QString()))
  { m_lastError = L"Could not map customization geometry to the exported FBX materials."; return false; }
  for (auto * item : *m_p_model)
    for (const auto & a : item->models())
    {
      auto n = m_attachMeshNodes.find(a.first);
      if (n != m_attachMeshNodes.end()) n->second->SetName(QString("WMVEquipment_%1_%2").arg((int)item->slot()).arg((int)a.first).toUtf8().constData());
      if (n == m_attachMeshNodes.end() || !describe(a.second, n->second, QString("equipment:%1").arg((int)item->slot())))
      { m_lastError = L"Could not map attached equipment to its FBX node."; return false; }
    }
  return true;
}

bool FBXExporter::exportCustomizationPackage(WoWModel * source, const std::wstring & target)
{
  m_lastError.clear();
  const auto fail = [&](const QString & why) { m_lastError = why.toStdWString(); return false; };
  QJsonObject catalog;
  QString reason;
  if (!source || !source->cd.customizationCatalog(catalog, reason)) return fail(reason);
  const QFileInfo destination(QString::fromStdWString(target));
  const QString packagePath = destination.absoluteFilePath() + ".customization";
  // Publish a whole directory once. A killed worker never leaves a successful catalog at the
  // destination, and an existing package is never partly overwritten by a failed new export.
  if (QFileInfo::exists(packagePath)) return fail("The customization package already exists. Choose a new export name.");
  QTemporaryDir staging(destination.absolutePath() + "/.wmv-customization-XXXXXX");
  if (!staging.isValid()) return fail("Cannot create the customization package folder.");
  const QDir folder(staging.path());
  if (!folder.mkdir("textures")) return fail("Cannot create the texture folder.");
  progress("CUSTOMIZATION");

  // Never reconfigure the live character, including the synchronous plugin fallback.
  std::unique_ptr<WoWModel> copy(new WoWModel(source->gamefile, true));
  if (!copy->ok) return fail("Cannot load the character for customization export.");
  copy->charModelDetails = source->charModelDetails;
  copy->modelType = MT_CHAR;
  if (!copy->setRaceSex(source->infos.raceID, source->infos.sexID)) return fail("Cannot restore the character's race context.");
  copy->setBodySex(source->itemSexID());
  Attachment attachment(nullptr, copy.get(), 0, -1);
  for (auto * original : *source) copy->addChild(new WoWItem(original->slot()));
  copy->cd.showUnderwear = source->cd.showUnderwear;
  copy->cd.showEars = source->cd.showEars;
  copy->cd.showHair = source->cd.showHair;
  copy->cd.showFacialHair = source->cd.showFacialHair;
  copy->cd.showFeet = source->cd.showFeet;
  copy->cd.autoHideGeosetsForHeadItems = source->cd.autoHideGeosetsForHeadItems;
  copy->cd.eyeGlowType = source->cd.eyeGlowType;
  copy->cd.applyAppearance(source->cd.captureAppearance().selection, source->cd.isDemonHunter(), true);
  copy->bSheathe = source->bSheathe;
  copy->td = source->td;

  QJsonObject preferences{{"showUnderwear", copy->cd.showUnderwear}, {"showHair", copy->cd.showHair},
    {"showFacialHair", copy->cd.showFacialHair}, {"showEars", copy->cd.showEars}, {"showFeet", copy->cd.showFeet}};
  catalog["preferences"] = preferences;
  catalog["underwearRegions"] = QJsonObject{{"upper", (int)CR_TORSO_UPPER}, {"lower", (int)CR_LEG_UPPER}};
  const auto unarmoredGroups = copy->cd.geosets;
  QJsonArray underwearGroups;
  if (copy->cd.isDemonHunter() || (copy->infos.raceID == RACE_PANDAREN && copy->infos.sexID == GENDER_FEMALE))
    underwearGroups.append(QJsonObject{{"group", (int)CG_DH_LOINCLOTH}, {"variant", copy->cd.showUnderwear ? 1 : 0}});
  catalog["baseUnderwearGroups"] = underwearGroups;
  catalog["build"] = QString::fromStdString(m_exportBuild);
  catalog["product"] = QString::fromStdString(m_exportProduct);
  catalog["fbx"] = destination.fileName();
  catalog["materialCatalog"] = destination.fileName() + ".wmvmat.json";

  LayoutSize size;
  std::map<int, CharRegionCoords> regions;
  if (!CharTexture::layout(copy->infos.textureLayoutID, size, regions)) return fail("Missing character texture layout.");
  QJsonArray regionArray;
  for (const auto & r : regions)
    regionArray.append(QJsonObject{{"id", r.first}, {"x", r.second.xpos}, {"y", r.second.ypos},
      {"width", r.second.width}, {"height", r.second.height}});
  catalog["textureLayout"] = QJsonObject{{"width", size.width}, {"height", size.height}, {"regions", regionArray},
    {"origin", "top-left"}, {"blendModes", "4=multiply;6=overlay;otherwise=source-over;region -1 or 11 at layer 0 replaces the canvas"}};

  std::set<int> textureIds, collectionIds;
  std::set<std::pair<QString, int>> requiredGeometry;
  for (size_t i = 0; i < copy->ownGeosetCount(); ++i)
  {
    const int id = copy->geosets[i]->id, group = id / 100;
    if ((id == 0 || id % 100 == 1 || group == 32) && group != 12 && group != 17 && group != 35)
      requiredGeometry.emplace("base", id);
    if (copy->geosets[i]->display) requiredGeometry.emplace("base", id);
  }
  std::map<int, int> fallbackTextures;
  for (const auto & o : catalog["options"].toArray())
    for (const auto & c : o.toObject()["choices"].toArray())
      for (const auto & value : c.toObject()["effects"].toArray())
      {
        const auto e = value.toObject();
        for (const auto & geo : e["geosets"].toArray())
          if (geo.toInt() % 100 != 0) requiredGeometry.emplace("base", geo.toInt());
        if (e.contains("model"))
        {
          collectionIds.insert(e["model"].toInt());
          if (e["variant"].toInt()) requiredGeometry.emplace(QString("customization:%1").arg(e["model"].toInt()),
            e["group"].toInt()*100 + e["variant"].toInt());
        }
        if (e.contains("texture"))
        {
          const auto t = e["texture"].toObject();
          textureIds.insert(t["fileId"].toInt());
          fallbackTextures.emplace(t["type"].toInt(), t["fileId"].toInt());
          if (t["type"].toInt() == 1 && !regions.count(t["region"].toInt())) return fail("Missing customization texture region.");
        }
      }

  // Capture the optional outfit as a separate layer over a complete base. Its resolved
  // group overrides are independent of skin/face/hair choices; helmet hiding runs last.
  QJsonArray equipment, equipmentLayers, hiddenGroups;
  QJsonObject outfit;
  std::set<uint> equipmentGroups;
  if (m_includeEquipment)
  {
    for (auto * original : *source)
      if (original->isEquipped())
      {
        auto * item = copy->getItem(original->slot());
        if (!item) return fail("Cannot restore an equipped slot.");
        item->restore(original->id(), original->displayId(), original->level());
        if (!item->isEquipped() || !item->showsAnything()) return fail("An equipped item has no exportable resources.");
        const auto originalModels = original->models();
        for (const auto & part : item->models())
        {
          const auto previous = originalModels.find(part.first);
          if (previous != originalModels.end()) part.second->showModel = previous->second->showModel;
        }
        QJsonArray itemGroups;
        for (const auto & g : item->geosetGroups())
        { equipmentGroups.insert((uint)g.first); itemGroups.append(QJsonObject{{"group", (int)g.first}, {"variant", g.second}}); }
        equipment.append(QJsonObject{{"slot", (int)item->slot()}, {"itemId", item->id()},
          {"displayId", item->displayId()}, {"level", item->level()}, {"groups", itemGroups}});
      }
    copy->refresh();
    for (const auto & layer : copy->tex.components())
    {
      if (!layer.file) return fail("An equipment texture is missing.");
      const int fid = layer.file->fileDataId();
      const bool customization = std::any_of(copy->cd.textures.begin(), copy->cd.textures.end(), [&](const CharDetails::TextureCustomization & t) {
        return t.type == 1 && (int)t.fileId == fid && t.region == layer.region && (int)t.layer == layer.layer && (int)t.blendMode == layer.blendMode;
      });
      if (!customization)
      {
        textureIds.insert(fid);
        equipmentLayers.append(QJsonObject{{"fileId", fid}, {"region", layer.region}, {"layer", layer.layer}, {"blendMode", layer.blendMode}});
      }
    }
    for (int g : copy->equipmentHiddenGroups()) hiddenGroups.append(g);
    // Gloves also suppress shirt sleeves; this cross-slot override is part of the final outfit.
    if (copy->cd.geosets.count(CG_GLOVES) && copy->cd.geosets.at(CG_GLOVES) > 1) equipmentGroups.insert(CG_SLEEVES);
    std::map<uint, uint> overrides;
    for (uint g : equipmentGroups) if (copy->cd.geosets.count(g)) overrides[g] = copy->cd.geosets.at(g);
    for (const auto & g : copy->cd.geosets)
      if (!unarmoredGroups.count(g.first) || unarmoredGroups.at(g.first) != g.second) overrides[g.first] = g.second;
    outfit["groupOverrides"] = groups(overrides);
    outfit["hideGroups"] = hiddenGroups;
    outfit["bodyLayers"] = equipmentLayers;
    QJsonArray suppressedRegions;
    if ((copy->getItem(CS_CHEST) && copy->getItem(CS_CHEST)->isEquipped()) || (copy->getItem(CS_SHIRT) && copy->getItem(CS_SHIRT)->isEquipped())) suppressedRegions.append((int)CR_TORSO_UPPER);
    if (copy->getItem(CS_PANTS) && copy->getItem(CS_PANTS)->isEquipped()) suppressedRegions.append((int)CR_LEG_UPPER);
    outfit["suppressCustomizationRegions"] = suppressedRegions;
  }
  outfit["included"] = m_includeEquipment;
  outfit["items"] = equipment;
  catalog["equipment"] = outfit;

  std::set<std::pair<QString, int>> defaultGeometry;
  for (size_t i = 0; i < copy->geosets.size(); ++i)
    if (copy->geosets[i]->display) defaultGeometry.emplace(partOwner(copy.get(), i), copy->geosets[i]->id);
  requiredGeometry.insert(defaultGeometry.begin(), defaultGeometry.end());

  const auto bindFallbacks = [&](WoWModel * model) {
    for (const auto & t : fallbackTextures)
    {
      bool bound = false;
      for (auto * pass : model->passes)
        if (model->textureTypeForSlot(pass->tex) == t.first && model->getGLTexture(pass->tex) != ModelRenderPass::INVALID_TEX)
          bound = true;
      if (t.first != 1 && !bound) model->updateTextureList(GAMEDIRECTORY.getFile((uint)t.second), t.first);
    }
  };
  bindFallbacks(copy.get());

  // Every geometry alternative is merged once, on the character skeleton. No character
  // combination enumeration. refreshMerging uses the same bone mapping as the live viewer.
  for (int fid : collectionIds)
  {
    if (!GAMEDIRECTORY.getFile((uint)fid)) return fail(QString("Missing collection model %1.").arg(fid));
    auto * part = copy->mergeModel((uint)fid, 1, true);
    if (!part || !part->ok) return fail(QString("Cannot load collection model %1.").arg(fid));
    bindFallbacks(part);
  }
  copy->rebuildExportGeometry();
  for (size_t i = 0; i < copy->geosets.size(); ++i)
    copy->geosets[i]->display = requiredGeometry.count({partOwner(copy.get(), i), copy->geosets[i]->id}) != 0;
  // A character can carry an auxiliary geoset-0 surface whose replaceable
  // material is only supplied in another class context. It is not the body
  // surface (type 1), despite sharing its geoset ID. Do not turn that dormant
  // surface into a required missing texture merely because all id-0 pieces
  // satisfy the geometric default. Never suppress named textures, body skin,
  // explicitly selected variants, or surfaces with a binding in this catalog.
  QJsonArray excludedGeometry;
  for (size_t gi = 0; gi < copy->ownGeosetCount(); ++gi)
  {
    if (copy->gamefile->fileDataId() != 921844 || !copy->geosets[gi]->display || copy->geosets[gi]->id != 0) continue;
    bool found = false, dormant = true;
    int unboundType = -1;
    for (auto * pass : copy->passes)
      if (pass->geoIndex == gi)
      {
        found = true;
        const int type = copy->textureTypeForSlot(pass->tex);
        if (pass->textureCount > 1 || type != TEXTURE_UI_SKIN || fallbackTextures.count(type) ||
            copy->getGLTexture(pass->tex) != ModelRenderPass::INVALID_TEX)
          dormant = false;
        unboundType = type;
      }
    if (found && dormant)
    {
      copy->geosets[gi]->display = false;
      excludedGeometry.append(QJsonObject{{"owner", "base"}, {"sourceGeosetIndex", (int)gi},
        {"geoset", 0}, {"textureType", unboundType}, {"reason", "no-material-binding-in-player-context"}});
    }
  }
  catalog["excludedGeometry"] = excludedGeometry;
  for (auto * item : *copy)
    for (const auto & a : item->models())
    {
      if (a.second->mirrored_) return fail("Mirrored equipment is not yet supported by customization packages. Disable Include equipment.");
      int attachmentId = -1;
      for (const auto * child : attachment.children)
        if (child->model() == a.second && child->slot == item->slot()) attachmentId = child->id;
      if (attachmentId < 0 || attachmentId >= (int)WoWModel::ATT_MAX) return fail("An equipped model has no resolved attachment. Disable Include equipment.");
      const int lookup = copy->attLookup[attachmentId];
      if (lookup < 0 || lookup >= (int)copy->atts.size() || copy->atts[lookup].bone >= copy->bones.size())
        return fail("An equipped model has no valid attachment bone. Disable Include equipment.");
    }
  // A merged part with an unmapped weighted bone cannot be exported as a valid skinned part.
  for (const auto & p : copy->mergedParts())
    for (const auto & vertex : p.model->sourceVertices())
      for (int w = 0; w < 4; ++w)
        if (vertex.weights[w])
        {
          const size_t from = vertex.bones[w];
          if (from >= p.boneMap.size() || from >= p.model->bones.size()) return fail("A collection model has an invalid source bone.");
          const int to = p.boneMap[from];
          if (to < 0 || to > 255 || to >= (int)copy->bones.size()) return fail("A collection model has an unmapped bone.");
          const auto & a = p.model->bones[from];
          const auto & b = copy->bones[to];
          if (a.boneDef.unknown != b.boneDef.unknown || glm::length(a.pivot-b.pivot) > 0.0002f)
            return fail("A collection model has a bone whose identity cannot be matched to the character.");
        }

  progress("CUSTOMIZATION_TEXTURES");
  QJsonArray textures;
  for (int fid : textureIds)
  {
    if (fid <= 0) return fail("A customization or equipment texture has no file identity.");
    const auto image = CharTexture::sourceImage(GAMEDIRECTORY.getFile((uint)fid));
    const QString file = QString("textures/%1.png").arg(fid);
    if (image.isNull() || !image.save(folder.filePath(file), "PNG")) return fail(QString("Cannot export texture %1.").arg(fid));
    textures.append(QJsonObject{{"fileId", fid}, {"file", file}, {"width", image.width()}, {"height", image.height()}});
  }
  catalog["textures"] = textures;

  FBXExporter scene;
  scene.setExportOptions(true, true, true, m_exportAnimation);
  scene.setAnimationsToExport(m_animsToExport);
  scene.setComponentRawExport(true);
  scene.m_packageScene = true;
  copy->updatePose();
  if (!scene.exportModel(copy.get(), folder.filePath(destination.fileName()).toStdWString()))
  { m_lastError = scene.lastError(); return false; }
  QJsonArray geometry;
  for (const auto & value : scene.m_packageGeometry)
  {
    auto g = value.toObject();
    if (!g["owner"].toString().startsWith("equipment:") || !g["node"].toString().startsWith("WMVEquipment_"))
      g["defaultVisible"] = defaultGeometry.count({g["owner"].toString(), g["geoset"].toInt()}) != 0;
    geometry.append(g);
  }
  catalog["geometry"] = geometry;
  std::set<std::pair<QString, int>> exportedGeosets;
  for (const auto & value : geometry)
  {
    const auto g = value.toObject();
    exportedGeosets.emplace(g["owner"].toString(), g["geoset"].toInt());
  }
  for (const auto & o : catalog["options"].toArray())
    for (const auto & c : o.toObject()["choices"].toArray())
      for (const auto & value : c.toObject()["effects"].toArray())
      {
        const auto e = value.toObject();
        for (const auto & geo : e["geosets"].toArray())
          if (geo.toInt() % 100 != 0 && !exportedGeosets.count({QString("base"), geo.toInt()}))
            return fail(QString("Choice %1 references a base geoset without exported geometry.").arg(c.toObject()["id"].toInt()));
        if (e.contains("model") && e["variant"].toInt() != 0 &&
            !exportedGeosets.count({QString("customization:%1").arg(e["model"].toInt()), e["group"].toInt()*100 + e["variant"].toInt()}))
          return fail(QString("Choice %1 references collection geometry that could not be exported.").arg(c.toObject()["id"].toInt()));
      }
  QJsonArray bones;
  for (const auto & b : scene.m_boneNodes)
  {
    const auto pivot = copy->bones[b.first].pivot * (float)SCALE_FACTOR;
    bones.append(QJsonObject{{"index", b.first}, {"node", QString::fromUtf8(b.second->GetName())},
      {"parent", copy->bones[b.first].boneDef.parent}, {"bindPivot", QJsonArray{pivot.x, pivot.y, pivot.z}}});
  }
  catalog["bones"] = bones;
  QJsonArray boneSets;
  for (const auto & value : catalog["boneSets"].toArray())
  {
    auto set = value.toObject();
    QJsonArray transforms;
    for (const auto & transformValue : set["transforms"].toArray())
    {
      auto transform = transformValue.toObject();
      const int index = transform["bone"].toInt(-1);
      const auto it = scene.m_boneNodes.find(index);
      const auto sourceMatrix = transform["matrix"].toArray();
      if (it == scene.m_boneNodes.end() || sourceMatrix.size() != 16)
        return fail("A customization bone is not present in the exported skeleton.");
      customization::Matrix matrix;
      for (int i = 0; i < 16; ++i) matrix[i] = (float)sourceMatrix[i].toDouble();
      matrix = customization::changeUnits(matrix, (float)SCALE_FACTOR);
      QJsonArray converted;
      for (float v : matrix) converted.append((double)v);
      transform["matrix"] = converted;
      transform["node"] = QString::fromUtf8(it->second->GetName());
      transforms.append(transform);
    }
    set["transforms"] = transforms;
    boneSets.append(set);
  }
  catalog["boneSets"] = boneSets;
  catalog["boneCorrectionContract"] = QJsonObject{{"version", 1}, {"matrixLayout", "column-major"},
    {"coordinates", "FBX-native-Z-up-right-handed"}, {"unit", "centimeter"},
    {"sourceUnitsToFbx", (double)SCALE_FACTOR}, {"application", "animated-joint-origin-pre-multiply"},
    {"bindPose", "uncorrected"}, {"animationTakes", "uncorrected"}};
  QJsonArray clips;
  for (const auto & name : scene.m_exportedClipNames) clips.append(QString::fromStdString(name));
  catalog["animationTakes"] = clips;
  catalog["rules"] = QJsonObject{{"geometry", "wmv-player-geosets-v1"}, {"textures", "wmv-player-layers-v1"},
    {"equipment", "resolved-current-outfit-v1"}, {"bones", "animated-joint-origin-v1"}, {"instructions", "README.txt"}};

  // The ordinary exporter treats material sidecars as optional. A customization package cannot.
  QFile materialFile(folder.filePath(destination.fileName() + ".wmvmat.json"));
  if (!materialFile.open(QIODevice::ReadOnly)) return fail("Missing material catalog.");
  QJsonParseError parse;
  const auto materials = QJsonDocument::fromJson(materialFile.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !materials.isObject()) return fail("Invalid material catalog.");
  // Every registered texture is mandatory here, including raw per-unit textures.
  for (const auto & texture : scene.m_texturesToExport)
    if (!QFileInfo(QString::fromStdWString(texture.first)).isFile() || QFileInfo(QString::fromStdWString(texture.first)).size() == 0)
      return fail(QString("An FBX material texture could not be written: %1.").arg(QFileInfo(QString::fromStdWString(texture.first)).fileName()));

  const QByteArray readme =
    "WMV customizable character package v1\n"
    "Read catalog.json and the FBX material sidecar; FBX alone is a geometry library, not a ready-made character.\n"
    "Geometry is addressed by FBX node + material (submesh). All alternative geometry is present.\n"
    "Scope: current player context; excludedChoiceIds are intentional exclusions.\n"
    "Version 2 requires conditional-body-models-v1: resolve bodyVariants by the selected option/choice first, then read that catalog and use its own mesh, skeleton, animations and boneSets. Never apply another posture's bone indices to this skeleton.\n"
    "An entry pointing to catalog.json uses this catalog's FBX directly; do not recurse. Keep the full package when moving postures. unavailableAnimations lists requested ID/sub-ID pairs absent in the alternate model.\n"
    "Requirements: zero is unconditional; anyChoice is OR. Option and choice requirements are AND.\n"
    "Effects apply for the selected choice and, if nonzero, relatedChoice. Options/effects are in composition order.\n"
    "Base geometry defaults: id 0, ids ending 01, or 32xx visible; 17xx,35xx,12xx hidden.\n"
    "For each active option, hide all base geosets referenced by its choices, then show the selected active effects; union wins.\n"
    "For every geosetGroups entry of an active effect, also hide all other variants of that base group; preserve body geoset 0.\n"
    "geosetGroups stores the authored groups even for None effects (empty geosets); never infer their group from a negative variant.\n"
    "Collection geometry defaults hidden. Model effects select group*100+variant (variant 0 means none) on customization:<fileId>.\n"
    "Hide the matching base group for active model effects unless an active base-geoset effect supplies that group.\n"
    "ShowHair=false hides group 0 except id 0; ShowFacialHair=false hides 1/2/3; ShowEars=false hides 7.\n"
    "Apply baseUnderwearGroups after the selected customization geometry. underwearRegions identifies upper/lower atlas regions.\n"
    "If ears have no customization owner and are enabled, ensure the lowest nonzero variant in group 7 is visible.\n"
    "Textures: select active effects, stable-sort by layer for types 1 and 19. Type 1 composes the body atlas by textureLayout.\n"
    "Type 19 composes full-image eye layers (first layer replaces, later layers blend). Other types use the last active effect.\n"
    "Blend modes: 4 multiply, 6 overlay, otherwise straight-alpha source-over. Resize smoothly to the target region.\n"
    "Suppress torso-upper/leg-upper customization textures when underwear is disabled; equipment additionally suppresses its listed regions.\n"
    "Bind the composed/type texture to geometry.textureUnits using textureType; retain type 0 or customizationBinding=false textures from the material catalog.\n"
    "Merged skin type 8 without its own active texture falls back to body type 1. Materials/UVs/render states use the material sidecar.\n"
    "Equipment is the captured current outfit, not a universal item database: items identify its pieces; resolved outfit rules describe them worn together.\n"
    "To enable the outfit, apply its groupOverrides after customization, then hideGroups; add bodyLayers and suppress listed customization regions.\n"
    "Enable equipment:<slot> geometry for included pieces according to defaultVisible (attached nodes already follow their FBX bones).\n"
    "To remove the outfit, recompute base visibility and textures without its overrides, suppression or layers; hide equipment geometry.\n"
    "Face bones: resolve boneSet effects for the selected choice AND relatedChoice; different posture choices are alternatives, never stacked.\n"
    "Use the single active boneSet; duplicate references to the same set apply once. No active boneSet means identity, not the previous selection.\n"
    "boneSets.transforms target the named FBX joint. Matrices are column-major, column-vector, FBX-native axes and centimeters.\n"
    "On EACH fresh sampled animation/rest local matrix L, let t=L.translation; apply L'=T(t)*C*T(-t)*L before accumulating parents.\n"
    "Apply once per frame, after animation blending. Never accumulate corrections from the previous frame or alter shared bind poses.\n"
    "Correction translations are already scaled to FBX units. An importer changing axes/units must conjugate C by that basis conversion.\n"
    "Preserve the full affine matrix: rotation plus nonuniform scaling can produce shear; dropping it to an approximate TRS loses fidelity.\n"
    "FacePose clips are raw source animations, not automatically applied customization layers; do not add FacePose to the same face correction.\n"
    "Moving this entire folder keeps all catalog references valid. An importer/runtime must implement these rules.\n";
  QFile instructions(folder.filePath("README.txt"));
  if (!instructions.open(QIODevice::WriteOnly) || instructions.write(readme) != readme.size() || !instructions.flush())
    return fail("Cannot write package instructions.");
  instructions.close();
  if (!catalog["bodyModels"].toArray().isEmpty())
  {
    catalog["version"] = 2;
    catalog["requiredFeatures"] = QJsonArray{"conditional-body-models-v1"};
    catalog["bodyModelFileId"] = (int)source->gamefile->fileDataId();
  }
  if (!m_postureVariant && !catalog["bodyModels"].toArray().isEmpty())
  {
    QJsonArray bodyVariants;
    for (const auto & value : catalog["bodyModels"].toArray())
    {
      auto body = value.toObject();
      const int fileId = body["modelFileId"].toInt();
      if (fileId == source->gamefile->fileDataId())
      {
        body["catalog"] = "catalog.json";
        bodyVariants.append(body);
        continue;
      }
      auto * modelFile = GAMEDIRECTORY.getFile((uint)fileId);
      if (!modelFile) return fail("A posture model is not available in this installation.");
      std::unique_ptr<WoWModel> alternate(new WoWModel(modelFile, true));
      if (!alternate->ok || !alternate->setRaceSex(source->infos.raceID, source->infos.sexID))
        return fail("Cannot load the alternate posture model.");
      alternate->charModelDetails = source->charModelDetails;
      alternate->modelType = MT_CHAR;
      alternate->setBodySex(source->itemSexID());
      Attachment alternateAttachment(nullptr, alternate.get(), 0, -1);
      for (auto * item : *source) alternate->addChild(new WoWItem(item->slot()));
      alternate->cd.showUnderwear = source->cd.showUnderwear;
      alternate->cd.showEars = source->cd.showEars;
      alternate->cd.showHair = source->cd.showHair;
      alternate->cd.showFacialHair = source->cd.showFacialHair;
      alternate->cd.showFeet = source->cd.showFeet;
      alternate->cd.autoHideGeosetsForHeadItems = source->cd.autoHideGeosetsForHeadItems;
      alternate->cd.eyeGlowType = source->cd.eyeGlowType;
      auto selection = source->cd.captureAppearance().selection;
      selection[(uint)body["option"].toInt()] = (uint)body["choice"].toInt();
      alternate->cd.applyAppearance(selection, source->cd.isDemonHunter(), true);
      alternate->bSheathe = source->bSheathe;
      alternate->td = source->td;
      if (m_includeEquipment)
      {
        for (auto * item : *source)
          if (item->isEquipped()) alternate->getItem(item->slot())->restore(item->id(), item->displayId(), item->level());
        alternate->refresh();
        for (auto * item : *source)
        {
          const auto originalParts = item->models();
          for (const auto & part : alternate->getItem(item->slot())->models())
          {
            const auto original = originalParts.find(part.first);
            if (original != originalParts.end()) part.second->showModel = original->second->showModel;
          }
        }
      }
      std::vector<int> clips;
      QJsonArray unavailable;
      // List positions are model-local. Carry the actual animation ID + variant.
      for (int index : m_animsToExport)
      {
        if (index < 0 || index >= (int)source->anims.size()) return fail("Invalid requested animation index.");
        const auto & requested = source->anims[index];
        bool found = false;
        for (size_t i = 0; i < alternate->anims.size(); ++i)
          if (alternate->anims[i].animID == requested.animID && alternate->anims[i].subAnimID == requested.subAnimID)
          {
            if (std::find(clips.begin(), clips.end(), (int)i) == clips.end()) clips.push_back((int)i);
            found = true;
          }
        if (!found) unavailable.append(QJsonObject{{"animationId", (int)requested.animID}, {"subAnimationId", (int)requested.subAnimID}});
      }
      const QString relative = QString("postures/choice-%1").arg(body["choice"].toInt());
      if (!folder.mkpath(relative)) return fail("Cannot create the posture folder.");
      const QString alternateTarget = folder.filePath(relative + "/character.fbx");
      FBXExporter exporter;
      exporter.m_postureVariant = true;
      exporter.setExportOptions(true, true, true, m_exportAnimation);
      exporter.setAnimationsToExport(clips);
      exporter.setCustomizationExport(true, m_includeEquipment, m_exportBuild, m_exportProduct);
      if (!exporter.exportCustomizationPackage(alternate.get(), alternateTarget.toStdWString()))
      { m_lastError = exporter.lastError(); return false; }
      body["catalog"] = relative + "/character.fbx.customization/catalog.json";
      body["unavailableAnimations"] = unavailable;
      bodyVariants.append(body);
    }
    catalog["bodyVariants"] = bodyVariants;
  }
  if (!saveJson(folder.filePath("catalog.json"), catalog)) return fail("Cannot write customization catalog.");
  materialFile.close();
  if (!QDir().rename(staging.path(), packagePath)) return fail("Cannot publish the customization package. Choose another destination.");
  staging.setAutoRemove(false);
  return true;
}
