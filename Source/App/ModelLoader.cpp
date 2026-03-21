#include "ModelLoader.h"
#include "AppState.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Logger.h"
#include "Game.h"
#include "WoWModel.h"
#include "Attachment.h"
#include "WoWItem.h"
#include "CharDetails.h"
#include "RaceInfos.h"
#include "TextureManager.h"
#include "database.h"
#include "DB2Table.h"
#include "string_utils.h"
#include "video.h"

namespace ModelLoader
{

// ---- Utility --------------------------------------------------------------

std::string wstringToString(const std::wstring& ws)
{
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s.push_back(static_cast<char>(c & 0x7F));
    return s;
}

WoWModel* getLoadedModel(AppState& app)
{
    if (!app.scene.root) return nullptr;
    auto* att = app.scene.root->children.empty() ? nullptr : app.scene.root->children[0];
    return att ? dynamic_cast<WoWModel*>(att->model()) : nullptr;
}

void resetCameraToModel(OrbitCamera& camera, const WoWModel* model)
{
    if (!model)
    {
        camera.reset();
        return;
    }

    float zMin = 0.0f;
    float zMax = 0.0f;
    for (const auto& v : model->origVertices)
    {
        if (v.pos.z < zMin) zMin = v.pos.z;
        if (v.pos.z > zMax) zMax = v.pos.z;
    }
    camera.resetFromBounds(zMin, zMax, video.fov);
}

void applySkin(WoWModel* model, int skinIndex, AppState& app)
{
    if (!model || skinIndex < 0 || skinIndex >= static_cast<int>(app.anim.skinEntries.size()))
        return;

    const auto& skin = app.anim.skinEntries[skinIndex];
    model->setCreatureGeosetData(skin.creatureGeosetData);
    for (size_t i = 0; i < skin.count; ++i)
    {
        if (skin.tex[i])
            model->updateTextureList(skin.tex[i], skin.base + static_cast<int>(i));
    }
    app.anim.selectedSkin = skinIndex;
}

// ---- Animation control init -----------------------------------------------

void initAnimationControl(WoWModel* model, AppState& app)
{
    app.anim.animEntries.clear();
    app.anim.skinEntries.clear();
    app.anim.selectedAnimCombo = 0;
    app.anim.selectedSkin = -1;
    app.anim.blpSkin[0] = app.anim.blpSkin[1] = app.anim.blpSkin[2] = -1;
    app.anim.animSpeed = 1.0f;
    app.anim.selectedSecondaryAnim = -1;
    app.anim.selectedMouthAnim = -1;
    app.anim.mouthSpeed = 1.0f;
    app.anim.lockAnims = true;
    app.anim.loopCount = 0;

    if (!model || !model->animated || model->anims.empty())
        return;

    auto animsMap = model->getAnimsMap();
    int standIndex = -1;

    for (size_t i = 0; i < model->anims.size(); ++i)
    {
        AnimEntry e;
        auto it = animsMap.find(model->anims[i].animID);
        if (it != animsMap.end())
            e.label = wstringToString(it->second) + " [" + std::to_string(i) + "]";
        else
            e.label = "Anim " + std::to_string(model->anims[i].animID) + " [" + std::to_string(i) + "]";
        e.animIndex = static_cast<int>(i);
        app.anim.animEntries.push_back(e);

        if (model->anims[i].animID == 0 && standIndex < 0)
            standIndex = static_cast<int>(app.anim.animEntries.size()) - 1;
    }

    if (standIndex >= 0)
        app.anim.selectedAnimCombo = standIndex;

    int useAnim = (standIndex >= 0) ? app.anim.animEntries[standIndex].animIndex : 0;
    model->currentAnim = useAnim;
    model->animManager->SetAnim(0, useAnim, 0);
    model->animManager->SetSpeed(1.0f);
    model->animManager->Play();

    // Build skin list for creatures / items
    const std::string fn = model->itemName();
    bool isCreature = (fn.size() >= 8 && (fn.substr(0, 8) == "creature" || fn.substr(0, 8) == "Creature"));
    bool isItem = (fn.size() >= 4 && (fn.substr(0, 4) == "item" || fn.substr(0, 4) == "Item"));

    if (isCreature)
    {
        const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
        const auto* cmdTable = WOWDB.getTable("CreatureModelData");
        const auto* geoTable = WOWDB.getTable("CreatureDisplayInfoGeosetData");
        const int targetFDID = model->gamefile->fileDataId();

        if (cdiTable && cmdTable && geoTable)
        for (const auto& cdiRow : *cdiTable)
        {
            auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
            if (!cmdRow || static_cast<int>(cmdRow.getUInt("FileDataID")) != targetFDID)
                continue;

            SkinEntry se;
            size_t cnt = 0;
            uint32_t texFDIDs[3] = {
                cdiRow.getUInt("TextureVariationFileDataID1"),
                cdiRow.getUInt("TextureVariationFileDataID2"),
                cdiRow.getUInt("TextureVariationFileDataID3")
            };
            for (size_t s = 0; s < 3; ++s)
            {
                if (texFDIDs[s] != 0)
                {
                    se.tex[s] = GAMEDIRECTORY.getFile(texFDIDs[s]);
                    if (se.tex[s]) ++cnt;
                }
            }
            if (cnt == 0) continue;
            se.base = TEXTURE_GAMEOBJECT1;
            se.count = cnt;

            uint32_t cdi = cdiRow.recordID();
            for (const auto& geoRow : *geoTable)
            {
                if (geoRow.getUInt("CreatureDisplayInfoID") != cdi)
                    continue;
                int geoType = 100 * (static_cast<int>(geoRow.getUInt("GeosetIndex")) + 1);
                int geoId   = static_cast<int>(geoRow.getUInt("GeosetValue"));
                if (geoId > 0) se.creatureGeosetData.insert(geoType + geoId);
            }

            se.label = "Skin " + std::to_string(app.anim.skinEntries.size());
            app.anim.skinEntries.push_back(se);
        }
    }
    else if (isItem)
    {
        const auto* idiTable = WOWDB.getTable("ItemDisplayInfo");
        const auto* texFDTable = WOWDB.getTable("TextureFileData");
        const auto* modFDTable = WOWDB.getTable("ModelFileData");
        const int targetFDID = model->gamefile->fileDataId();

        std::set<uint32_t> matchingModelResIDs;
        if (idiTable && texFDTable && modFDTable)
        {
        for (const auto& mfdRow : *modFDTable)
        {
            if (static_cast<int>(mfdRow.getUInt("FileDataID")) == targetFDID)
                matchingModelResIDs.insert(mfdRow.getUInt("ModelResourcesID"));
        }

        for (const auto& idiRow : *idiTable)
        {
            if (matchingModelResIDs.find(idiRow.getUInt("ModelResourcesID1")) == matchingModelResIDs.end())
                continue;

            uint32_t matResID = idiRow.getUInt("ModelMaterialResourcesID1");
            for (const auto& tfdRow : *texFDTable)
            {
                if (tfdRow.getUInt("MaterialResourcesID") != matResID)
                    continue;
                uint32_t fdid = tfdRow.getUInt("FileDataID");
                if (fdid == 0) continue;
                SkinEntry se;
                se.tex[0] = GAMEDIRECTORY.getFile(fdid);
                if (!se.tex[0]) continue;
                se.base = TEXTURE_OBJECT_SKIN;
                se.count = 1;
                se.label = "Skin " + std::to_string(app.anim.skinEntries.size());
                app.anim.skinEntries.push_back(se);
            }
        }
        }
    }

    if (!app.anim.skinEntries.empty())
        applySkin(model, 0, app);
}

// ---- Character control init -----------------------------------------------

void initCharacterControl(WoWModel* model, AppState& app)
{
    app.character.customizationOptions.clear();
    if (!model) return;

    auto& cd = model->cd;
    cd.showUnderwear = true;
    cd.showEars = true;
    cd.showHair = true;
    cd.showFacialHair = true;
    cd.showFeet = false;
    cd.autoHideGeosetsForHeadItems = true;
    cd.eyeGlowType = EGT_DEFAULT;
    model->bSheathe = false;
    cd.reset(model);

    const auto& infos = model->infos;
    if (infos.raceID == -1 || infos.ChrModelID.empty())
        return;

    const auto* optTable = WOWDB.getTable("ChrCustomizationOption");
    const auto* choiceTable = WOWDB.getTable("ChrCustomizationChoice");
    if (!optTable || !choiceTable) return;
    const uint32_t targetModelID = static_cast<uint32_t>(infos.ChrModelID[0]);

    struct OptionEntry { uint32_t id; uint32_t orderIndex; };
    std::vector<OptionEntry> matchingOptions;
    for (const auto& row : *optTable)
    {
        if (row.getUInt("ChrModelID") == targetModelID && row.getUInt("ChrCustomizationID") != 0)
            matchingOptions.push_back({ row.recordID(), row.getUInt("OrderIndex") });
    }
    std::sort(matchingOptions.begin(), matchingOptions.end(),
        [](const OptionEntry& a, const OptionEntry& b) { return a.orderIndex < b.orderIndex; });

    for (const auto& optEntry : matchingOptions)
    {
        unsigned int optionID = optEntry.id;

        CustomizationOption opt;
        opt.optionID = optionID;

        auto optRow = optTable->getRow(optionID);
        if (optRow)
        {
            std::string n = optRow.getString("Name_Lang");
            opt.name = n.empty() ? ("Option " + std::to_string(optionID)) : n;
        }
        else
        {
            opt.name = "Option " + std::to_string(optionID);
        }

        std::vector<unsigned int> choiceIDs = cd.getCustomizationChoices(optionID);
        if (choiceIDs.empty())
            continue;

        std::map<unsigned int, std::string> idToName;
        for (unsigned int cid : choiceIDs)
        {
            auto cRow = choiceTable->getRow(cid);
            if (cRow)
            {
                std::string cname = cRow.getString("Name_Lang");
                idToName[cid] = cname.empty() ? ("Choice " + std::to_string(cid)) : cname;
            }
        }
        for (unsigned int cid : choiceIDs)
        {
            opt.choiceIDs.push_back(cid);
            auto it = idToName.find(cid);
            opt.choiceNames.push_back(it != idToName.end() ? it->second : ("Choice " + std::to_string(cid)));
        }

        unsigned int cur = cd.get(optionID);
        opt.selectedIndex = 0;
        for (size_t c = 0; c < opt.choiceIDs.size(); ++c)
        {
            if (opt.choiceIDs[c] == cur)
            {
                opt.selectedIndex = static_cast<int>(c);
                break;
            }
        }

        app.character.customizationOptions.push_back(std::move(opt));
    }
}

// ---- Model control init ---------------------------------------------------

void initModelControl(WoWModel* model, AppState& app)
{
    app.browsers.geosetGroups.clear();
    app.browsers.pcrState = {};

    if (!model)
        return;

    std::map<size_t, size_t> meshToGroupIdx;
    for (size_t i = 0; i < model->geosets.size(); ++i)
    {
        size_t mesh = model->geosets[i]->id / 100;
        if (meshToGroupIdx.find(mesh) == meshToGroupIdx.end())
        {
            GeosetGroupEntry group;
            group.meshId = mesh;
            std::string groupName = WoWModel::getCGGroupName(static_cast<CharGeosets>(mesh));
            group.name = groupName.empty() ? std::to_string(mesh) : groupName;
            meshToGroupIdx[mesh] = app.browsers.geosetGroups.size();
            app.browsers.geosetGroups.push_back(std::move(group));
        }

        GeosetEntry ge;
        ge.index = i;
        ge.id = model->geosets[i]->id;
        ge.label = std::format("{} [{}, {}, {}]", i, mesh,
                               model->geosets[i]->id % 100, model->geosets[i]->id);
        app.browsers.geosetGroups[meshToGroupIdx[mesh]].geosets.push_back(ge);
    }

    for (uint pcid : model->replacableParticleColorIDs)
    {
        if (pcid == 11) app.browsers.pcrState.hasSet[0] = true;
        else if (pcid == 12) app.browsers.pcrState.hasSet[1] = true;
        else if (pcid == 13) app.browsers.pcrState.hasSet[2] = true;
    }
}

// ---- Equipment helpers ----------------------------------------------------

static bool correctType(int type, int slot)
{
    if (type == IT_ALL)
        return true;
    switch (slot)
    {
    case CS_HEAD:       return (type == IT_HEAD);
    case CS_SHOULDER:   return (type == IT_SHOULDER);
    case CS_SHIRT:      return (type == IT_SHIRT);
    case CS_CHEST:      return (type == IT_CHEST || type == IT_ROBE);
    case CS_BELT:       return (type == IT_BELT);
    case CS_PANTS:      return (type == IT_PANTS);
    case CS_BOOTS:      return (type == IT_BOOTS);
    case CS_BRACERS:    return (type == IT_BRACERS);
    case CS_GLOVES:     return (type == IT_GLOVES);
    case CS_HAND_RIGHT: return (type == IT_RIGHTHANDED || type == IT_GUN || type == IT_THROWN ||
                                type == IT_2HANDED || type == IT_DAGGER);
    case CS_HAND_LEFT:  return (type == IT_LEFTHANDED || type == IT_BOW || type == IT_SHIELD ||
                                type == IT_2HANDED || type == IT_DAGGER || type == IT_OFFHAND);
    case CS_CAPE:       return (type == IT_CAPE);
    case CS_TABARD:     return (type == IT_TABARD);
    case CS_QUIVER:     return (type == IT_QUIVER);
    default: return false;
    }
}

void rebuildEquipFilteredItems(AppState& app)
{
    app.character.equipFilteredItems.clear();
    if (app.character.equipSlotToEdit < 0)
        return;

    std::string search = core::toLower(app.character.equipSearchBuf);
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0)
            continue;
        if (!correctType(item.type, app.character.equipSlotToEdit))
            continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        app.character.equipFilteredItems.push_back(i);
    }
}

void tryToEquipItem(WoWModel* model, int id, AppState& app)
{
    if (id == 0 || !model)
        return;

    ItemRecord itemr = items.getById(id);
    if (itemr.name == items.items[0].name)
    {
        LOG_ERROR << "Cannot retrieve item from database (id " << id << ")";
        return;
    }

    int itemSlot = itemr.slot();
    if (itemSlot == -1)
    {
        LOG_ERROR << "Cannot determine slot for object " << itemr.name;
        return;
    }

    WoWItem* item = model->getItem(static_cast<CharSlots>(itemSlot));
    if (item)
    {
        item->setId(id);
        app.character.equipSlotLevels[itemSlot] = 0;
    }
}

// ---- Item Sets ------------------------------------------------------------

void buildItemSets(AppState& app)
{
    if (app.browsers.itemSetsBuilt)
        return;

    app.browsers.itemSets.clear();

    const auto* itemSetTable = WOWDB.getTable("ItemSet");
    if (!itemSetTable) return;
    for (const auto& row : *itemSetTable)
    {
        ItemSetEntry e;
        e.id = static_cast<int>(row.recordID());
        e.name = row.getString("Name_Lang");
        if (!e.name.empty())
            app.browsers.itemSets.push_back(e);
    }

    std::sort(app.browsers.itemSets.begin(), app.browsers.itemSets.end(),
        [](const ItemSetEntry& a, const ItemSetEntry& b) { return a.name < b.name; });

    app.browsers.itemSetsBuilt = true;
    app.browsers.itemSetFilterDirty = true;
    LOG_INFO << "Item sets loaded: " << app.browsers.itemSets.size();
}

void rebuildItemSetFilter(AppState& app)
{
    app.browsers.itemSetFiltered.clear();

    std::string search = core::toLower(std::string(app.browsers.itemSetSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < app.browsers.itemSets.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(app.browsers.itemSets[i].name, search))
            continue;
        app.browsers.itemSetFiltered.push_back(i);
    }

    app.browsers.itemSetFilterDirty = false;
}

void applyItemSet(WoWModel* model, int setId, AppState& app)
{
    if (!model || setId <= 0)
        return;

    const auto* itemSetTable = WOWDB.getTable("ItemSet");
    if (!itemSetTable) return;
    auto setRow = itemSetTable->getRow(static_cast<uint32_t>(setId));
    if (!setRow)
    {
        LOG_ERROR << "Item set query failed for ID " << setId;
        return;
    }

    for (const auto it : *model)
        it->setId(0);
    std::memset(app.character.equipSlotLevels, 0, sizeof(app.character.equipSlotLevels));

    for (unsigned i = 0; i < 8; ++i)
    {
        std::string fieldName = "ItemID" + std::to_string(i + 1);
        uint32_t itemID = setRow.getUInt(fieldName);
        if (itemID == 0)
            continue;
        try
        {
            tryToEquipItem(model, static_cast<int>(itemID), app);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Failed to equip item set entry " << i
                      << " (id=" << itemID << "): " << e.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied item set ID " << setId;
}

// ---- Start Outfits --------------------------------------------------------

void buildStartOutfits(WoWModel* model, AppState& app)
{
    app.browsers.startOutfits.clear();
    app.browsers.startOutfitsBuilt = false;
    app.browsers.startOutfitFilterDirty = true;

    if (!model) return;

    const auto& infos = model->infos;
    if (infos.raceID == -1)
        return;

    const auto* csoTable = WOWDB.getTable("CharStartOutfit");
    const auto* chrClassTable = WOWDB.getTable("ChrClasses");
    if (!csoTable || !chrClassTable) return;
    const uint32_t targetRace = static_cast<uint32_t>(infos.raceID);
    const uint32_t targetSex = static_cast<uint32_t>(infos.sexID);

    for (const auto& row : *csoTable)
    {
        if (row.getUInt("raceID") != targetRace || row.getUInt("sexID") != targetSex)
            continue;

        auto classRow = chrClassTable->getRow(row.getUInt("classID"));
        std::string className = classRow ? classRow.getString("Filename") : "";

        StartOutfitEntry e;
        e.name = className;
        e.id = static_cast<int>(row.recordID());
        if (!e.name.empty() && e.id > 0)
            app.browsers.startOutfits.push_back(e);
    }

    std::sort(app.browsers.startOutfits.begin(), app.browsers.startOutfits.end(),
        [](const StartOutfitEntry& a, const StartOutfitEntry& b) { return a.name < b.name; });

    app.browsers.startOutfitsBuilt = true;
    app.browsers.startOutfitFilterDirty = true;
    LOG_INFO << "Start outfits loaded: " << app.browsers.startOutfits.size();
}

void rebuildStartOutfitFilter(AppState& app)
{
    app.browsers.startOutfitFiltered.clear();

    std::string search = core::toLower(std::string(app.browsers.startOutfitSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < app.browsers.startOutfits.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(app.browsers.startOutfits[i].name, search))
            continue;
        app.browsers.startOutfitFiltered.push_back(i);
    }

    app.browsers.startOutfitFilterDirty = false;
}

void applyStartOutfit(WoWModel* model, int outfitId, AppState& app)
{
    if (!model || outfitId <= 0)
        return;

    const auto* csoTable = WOWDB.getTable("CharStartOutfit");
    if (!csoTable) return;
    auto csoRow = csoTable->getRow(static_cast<uint32_t>(outfitId));
    if (!csoRow)
    {
        LOG_ERROR << "Start outfit query failed for ID " << outfitId;
        return;
    }

    for (const auto it : *model)
        it->setId(0);
    std::memset(app.character.equipSlotLevels, 0, sizeof(app.character.equipSlotLevels));

    for (unsigned i = 0; i < 24; ++i)
    {
        std::string fieldName = "iitem" + std::to_string(i + 1);
        uint32_t itemID = csoRow.getUInt(fieldName);
        if (itemID == 0)
            continue;
        try
        {
            tryToEquipItem(model, static_cast<int>(itemID), app);
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR << "Failed to equip start outfit entry " << i
                      << " (id=" << itemID << "): " << ex.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied start outfit ID " << outfitId;
}

// ---- Clear / Load model ---------------------------------------------------

void clearModel(AppState& app)
{
    if (app.scene.root)
    {
        app.scene.root->delChildren();
        app.scene.root->setModel(nullptr);
    }

    TEXTUREMANAGER.clear();
    app.scene.isModel = false;
    app.scene.isChar = false;

    app.scene.selModel = nullptr;
    app.anim.animEntries.clear();
    app.anim.skinEntries.clear();
    app.character.customizationOptions.clear();
    app.browsers.geosetGroups.clear();
    app.browsers.pcrState = {};
    app.anim.selectedAnimCombo = 0;
    app.anim.selectedSkin = -1;
    app.anim.animSpeed = 1.0f;
    app.anim.autoAnimate = true;
    app.character.equipSlotToEdit = -1;
    app.character.equipFilteredItems.clear();
    app.character.equipSearchBuf.clear();
    std::memset(app.character.equipSlotLevels, 0, sizeof(app.character.equipSlotLevels));
    app.exporting.exportAnimChecked.clear();
    app.exporting.exportStatus.clear();
    app.scene.isMounted = false;
    app.browsers.startOutfits.clear();
    app.browsers.startOutfitsBuilt = false;
    app.browsers.startOutfitSearchBuf.clear();
    app.browsers.startOutfitFiltered.clear();
    app.browsers.startOutfitFilterDirty = true;
}

void loadModel(GameFile* file, AppState& app)
{
    if (!file || !app.scene.root)
        return;

    LOG_INFO << "Loading model: " << file->fullname();

    clearModel(app);

    auto* model = new WoWModel(file, true);
    if (!model->ok)
    {
        LOG_ERROR << "Model is not OK: " << file->fullname();
        delete model;
        return;
    }

    app.scene.root->addChild(model, 0, -1);

    const std::string fn = file->fullname();
    app.scene.isChar = (core::startsWithIgnoreCase(fn, "char") ||
                core::startsWithIgnoreCase(fn, "alternate/char") ||
                core::startsWithIgnoreCase(fn, "alternate\\char"));

    if (app.scene.isChar)
    {
        model->addChild(new WoWItem(CS_SHIRT));
        model->addChild(new WoWItem(CS_HEAD));
        model->addChild(new WoWItem(CS_SHOULDER));
        model->addChild(new WoWItem(CS_PANTS));
        model->addChild(new WoWItem(CS_BOOTS));
        model->addChild(new WoWItem(CS_CHEST));
        model->addChild(new WoWItem(CS_TABARD));
        model->addChild(new WoWItem(CS_BELT));
        model->addChild(new WoWItem(CS_BRACERS));
        model->addChild(new WoWItem(CS_GLOVES));
        model->addChild(new WoWItem(CS_HAND_RIGHT));
        model->addChild(new WoWItem(CS_HAND_LEFT));
        model->addChild(new WoWItem(CS_CAPE));
        model->addChild(new WoWItem(CS_QUIVER));
        model->modelType = MT_CHAR;
        model->charModelDetails.isChar = true;
    }
    else
    {
        model->addChild(new WoWItem(CS_HAND_RIGHT));
        model->addChild(new WoWItem(CS_HAND_LEFT));
        model->modelType = MT_NORMAL;
    }

    app.scene.isModel = true;

    app.scene.selModel = model;
    initAnimationControl(model, app);
    initModelControl(model, app);
    if (app.scene.isChar)
        initCharacterControl(model, app);

    resetCameraToModel(app.scene.camera, model);

    LOG_INFO << "Model loaded: " << model->name();
}

// ---- NPC Browser ----------------------------------------------------------

void rebuildNpcFilter(AppState& app)
{
    app.browsers.npcFiltered.clear();

    std::string search = core::toLower(std::string(app.browsers.npcSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& npc = npcs[i];
        if (npc.model == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(npc.name, search))
            continue;
        app.browsers.npcFiltered.push_back(i);
    }

    app.browsers.npcFilterDirty = false;
}

void loadNPC(unsigned int creatureID, AppState& app)
{
    const auto* creatureTable = WOWDB.getTable("Creature");
    if (!creatureTable) return;
    auto creatureRow = creatureTable->getRow(creatureID);
    if (!creatureRow)
    {
        LOG_ERROR << "NPC query failed for ID " << creatureID;
        return;
    }

    uint32_t displayInfoID = creatureRow.getUInt("DisplayID1");
    const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
    if (!cdiTable) return;
    auto cdiRow = cdiTable->getRow(displayInfoID);
    if (!cdiRow)
    {
        LOG_ERROR << "NPC query failed for ID " << creatureID << " (no CreatureDisplayInfo for DisplayID1=" << displayInfoID << ")";
        return;
    }

    const auto* cmdTable = WOWDB.getTable("CreatureModelData");
    auto cmdRow = cmdTable ? cmdTable->getRow(cdiRow.getUInt("ModelID")) : DB2Row();
    uint32_t fileDataID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
    uint32_t extraId = cdiRow.getUInt("ExtendedDisplayInfoID");

    if (extraId == 0)
    {
        GameFile* file = GAMEDIRECTORY.getFile(fileDataID);
        if (!file) return;
        loadModel(file, app);

        WoWModel* m = getLoadedModel(app);
        if (m)
        {
            uint32_t texFDIDs[3] = {
                cdiRow.getUInt("TextureVariationFileDataID1"),
                cdiRow.getUInt("TextureVariationFileDataID2"),
                cdiRow.getUInt("TextureVariationFileDataID3")
            };
            for (size_t i = 0; i < app.anim.skinEntries.size(); ++i)
            {
                bool match = true;
                for (size_t t = 0; t < 3 && match; ++t)
                {
                    if (app.anim.skinEntries[i].tex[t])
                    {
                        int fdid = app.anim.skinEntries[i].tex[t]->fileDataId();
                        if (texFDIDs[t] != 0)
                            match = (fdid == static_cast<int>(texFDIDs[t]));
                        else
                            match = false;
                    }
                }
                if (match)
                {
                    applySkin(m, static_cast<int>(i), app);
                    break;
                }
            }
        }
    }
    else
    {
        GameFile* file = GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(static_cast<int>(fileDataID)));
        if (!file) return;
        loadModel(file, app);

        WoWModel* m = getLoadedModel(app);
        if (!m) return;

        const auto* cdieTable = WOWDB.getTable("CreatureDisplayInfoExtra");
        auto cdieRow = cdieTable ? cdieTable->getRow(extraId) : DB2Row();
        if (cdieRow)
        {
            m->cd.set(CharDetails::SKIN_COLOR, static_cast<int>(cdieRow.getUInt("Skin")));
            m->cd.set(CharDetails::FACE, static_cast<int>(cdieRow.getUInt("Face")));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, static_cast<int>(cdieRow.getUInt("HairStyle")));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, static_cast<int>(cdieRow.getUInt("HairColor")));
            m->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, static_cast<int>(cdieRow.getUInt("FacialHair")));
        }

        const auto* npcSlotTable = WOWDB.getTable("NpcModelItemSlotDisplayInfo");
        static const std::map<int, CharSlots> ItemTypeToInternal = {
            {0, CS_HEAD}, {1, CS_SHOULDER}, {2, CS_SHIRT}, {3, CS_CHEST}, {4, CS_BELT}, {5, CS_PANTS},
            {6, CS_BOOTS}, {7, CS_BRACERS}, {8, CS_GLOVES}, {9, CS_TABARD}, {10, CS_CAPE}
        };
        if (npcSlotTable)
        for (const auto& npcRow : *npcSlotTable)
        {
            if (npcRow.getUInt("NpcModelID") != extraId)
                continue;
            auto it = ItemTypeToInternal.find(static_cast<int>(npcRow.getUInt("ItemSlot")));
            if (it != ItemTypeToInternal.end())
            {
                WoWItem* item = m->getItem(it->second);
                if (item)
                    item->setDisplayId(static_cast<int>(npcRow.getUInt("ItemDisplayInfoID")));
            }
        }

        m->cd.isNPC = true;
        m->refresh();
    }
}

// ---- Item Browser ---------------------------------------------------------

void rebuildItemBrowseFilter(AppState& app)
{
    app.browsers.itemBrowseFiltered.clear();

    std::string search = core::toLower(std::string(app.browsers.itemBrowseSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        app.browsers.itemBrowseFiltered.push_back(i);
    }

    app.browsers.itemBrowseFilterDirty = false;
}

void loadItemModel(unsigned int itemId, AppState& app)
{
    try
    {
        const auto* imaTable = WOWDB.getTable("ItemModifiedAppearance");
        const auto* iaTable = WOWDB.getTable("ItemAppearance");
        const auto* idiTable = WOWDB.getTable("ItemDisplayInfo");
        const auto* modFDTable = WOWDB.getTable("ModelFileData");
        const auto* texFDTable = WOWDB.getTable("TextureFileData");
        if (!imaTable || !iaTable || !idiTable || !modFDTable || !texFDTable) return;

        uint32_t itemAppearanceID = 0;
        for (const auto& row : *imaTable)
        {
            if (row.getUInt("ItemID") == itemId)
            {
                itemAppearanceID = row.getUInt("ItemAppearanceID");
                break;
            }
        }
        if (itemAppearanceID == 0) return;

        auto iaRow = iaTable->getRow(itemAppearanceID);
        if (!iaRow) return;

        uint32_t displayInfoID = iaRow.getUInt("ItemDisplayInfoID");
        auto idiRow = idiTable->getRow(displayInfoID);
        if (!idiRow) return;

        uint32_t modelResID = idiRow.getUInt("ModelResourcesID1");
        uint32_t modelFDID = 0;
        for (const auto& mfdRow : *modFDTable)
        {
            if (mfdRow.getUInt("ModelResourcesID") == modelResID)
            {
                modelFDID = mfdRow.getUInt("FileDataID");
                break;
            }
        }
        if (modelFDID == 0) return;

        GameFile* file = GAMEDIRECTORY.getFile(modelFDID);
        if (!file) return;

        loadModel(file, app);

        WoWModel* m = getLoadedModel(app);
        if (m)
        {
            uint32_t matResID = idiRow.getUInt("ModelMaterialResourcesID1");
            for (const auto& tfdRow : *texFDTable)
            {
                if (tfdRow.getUInt("MaterialResourcesID") == matResID)
                {
                    uint32_t texFDID = tfdRow.getUInt("FileDataID");
                    if (texFDID != 0)
                    {
                        GameFile* texFile = GAMEDIRECTORY.getFile(texFDID);
                        if (texFile)
                            m->updateTextureList(texFile, TEXTURE_OBJECT_SKIN);
                    }
                    break;
                }
            }
        }
    }
    catch (...)
    {
        LOG_ERROR << "Exception loading item model for ID " << itemId;
    }
}

// ---- Mounts ---------------------------------------------------------------

void buildMountList(AppState& app)
{
    if (app.browsers.mountListBuilt || !app.loading.isWoWLoaded || !app.loading.initDB)
        return;

    app.browsers.mountList.clear();
    app.browsers.creatureModels.clear();
    app.browsers.creatureModelNames.clear();

    const auto* mountTable = WOWDB.getTable("Mount");
    const auto* mxdTable = WOWDB.getTable("MountXDisplay");
    if (mountTable && mxdTable)
    for (const auto& mxdRow : *mxdTable)
    {
        uint32_t mountID = mxdRow.getUInt("MountID");
        auto mountRow = mountTable->getRow(mountID);
        MountEntry me;
        me.displayID = static_cast<int>(mxdRow.getUInt("CreatureDisplayInfoID"));
        me.name = mountRow ? mountRow.getString("Name_Lang") : "";
        app.browsers.mountList.push_back(me);
    }
    std::sort(app.browsers.mountList.begin(), app.browsers.mountList.end(),
        [](const MountEntry& a, const MountEntry& b) { return a.name < b.name; });
    LOG_INFO << "Mount list: " << app.browsers.mountList.size() << " player mounts.";

    std::vector<GameFile*> files;
    GAMEDIRECTORY.getFilesForFolder(files, std::string("creature/"), std::string("m2"));
    for (auto* gf : files)
    {
        app.browsers.creatureModels.push_back(gf);
        std::string n = gf->fullname();
        if (n.size() > 9)
            n = n.substr(9);
        app.browsers.creatureModelNames.push_back(n);
    }
    if (!app.browsers.creatureModels.empty())
    {
        std::vector<size_t> indices(app.browsers.creatureModels.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return app.browsers.creatureModelNames[a] < app.browsers.creatureModelNames[b]; });
        std::vector<GameFile*> sortedFiles(app.browsers.creatureModels.size());
        std::vector<std::string> sortedNames(app.browsers.creatureModelNames.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            sortedFiles[i] = app.browsers.creatureModels[indices[i]];
            sortedNames[i] = app.browsers.creatureModelNames[indices[i]];
        }
        app.browsers.creatureModels = std::move(sortedFiles);
        app.browsers.creatureModelNames = std::move(sortedNames);
    }
    LOG_INFO << "Creature models: " << app.browsers.creatureModels.size() << " files.";

    app.browsers.mountListBuilt = true;
    app.browsers.mountFilterDirty = true;
}

void rebuildMountFilter(AppState& app)
{
    app.browsers.mountFiltered.clear();

    std::string search = core::toLower(std::string(app.browsers.mountSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    if (app.browsers.mountTab == 0)
    {
        for (size_t i = 0; i < app.browsers.mountList.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(app.browsers.mountList[i].name, search))
                continue;
            app.browsers.mountFiltered.push_back(i);
        }
    }
    else
    {
        for (size_t i = 0; i < app.browsers.creatureModelNames.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(app.browsers.creatureModelNames[i], search))
                continue;
            app.browsers.mountFiltered.push_back(i);
        }
    }

    app.browsers.mountFilterDirty = false;
}

void mountCharacter(int displayID, GameFile* creatureFile, AppState& app)
{
    WoWModel* charModel = getLoadedModel(app);
    if (!charModel || !app.scene.isChar || !app.scene.root)
        return;

    GameFile* modelFile = nullptr;
    int morphID = 0;

    if (displayID > 0)
    {
        morphID = displayID;
        const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
        const auto* cmdTable = WOWDB.getTable("CreatureModelData");
        if (!cdiTable || !cmdTable) return;
        auto cdiRow = cdiTable->getRow(static_cast<uint32_t>(displayID));
        if (!cdiRow)
        {
            LOG_ERROR << "Mount display query failed for displayID " << displayID;
            return;
        }
        auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
        uint32_t mountFDID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
        if (mountFDID == 0)
        {
            LOG_ERROR << "Mount display query failed for displayID " << displayID;
            return;
        }
        modelFile = GAMEDIRECTORY.getFile(mountFDID);
    }
    else if (creatureFile)
    {
        modelFile = creatureFile;
    }

    if (!modelFile)
        return;

    Attachment* charAtt = app.scene.root->children.empty() ? nullptr : app.scene.root->children[0];
    if (!charAtt)
        return;

    auto* mountModel = new WoWModel(modelFile, false);
    if (!mountModel->ok)
    {
        LOG_ERROR << "Mount model failed to load.";
        delete mountModel;
        return;
    }
    mountModel->isMount = true;

    app.scene.root->setModel(mountModel);
    charAtt->id = 0;

    if (morphID > 0)
    {
        const auto* cdiTexTable = WOWDB.getTable("CreatureDisplayInfo");
        if (!cdiTexTable) return;
        auto cdiTexRow = cdiTexTable->getRow(static_cast<uint32_t>(morphID));
        if (cdiTexRow)
        {
            static const char* texFields[] = {
                "TextureVariationFileDataID1",
                "TextureVariationFileDataID2",
                "TextureVariationFileDataID3"
            };
            for (size_t t = 0; t < 3; ++t)
            {
                uint32_t texFDID = cdiTexRow.getUInt(texFields[t]);
                if (texFDID != 0)
                {
                    GameFile* texFile = GAMEDIRECTORY.getFile(texFDID);
                    if (texFile)
                        mountModel->updateTextureList(texFile, TEXTURE_GAMEOBJECT1 + static_cast<int>(t));
                }
            }
        }
    }

    charModel->bSheathe = true;

    if (charModel->animLookups.size() > ANIMATION_MOUNT &&
        charModel->animLookups[ANIMATION_MOUNT] >= 0)
    {
        charModel->animManager->Stop();
        charModel->currentAnim = charModel->animLookups[ANIMATION_MOUNT];
        charModel->animManager->SetAnim(0, static_cast<short>(charModel->currentAnim), 0);
        charModel->animManager->Play();
    }

    charModel->rot_ = charModel->pos_ = glm::vec3(0.0f);
    charModel->scale_ = 1.0f;
    mountModel->rot_.x = 0.0f;

    app.scene.isMounted = true;
    app.scene.selModel = mountModel;

    initAnimationControl(mountModel, app);
    initModelControl(mountModel, app);

    resetCameraToModel(app.scene.camera, mountModel);
    LOG_INFO << "Character mounted on: " << modelFile->fullname();
}

void dismountCharacter(AppState& app)
{
    if (!app.scene.isMounted || !app.scene.root || !app.scene.isChar)
        return;

    WoWModel* charModel = nullptr;
    Attachment* charAtt = app.scene.root->children.empty() ? nullptr : app.scene.root->children[0];
    if (charAtt)
        charModel = dynamic_cast<WoWModel*>(charAtt->model());

    app.scene.root->setModel(nullptr);
    app.scene.isMounted = false;

    if (charAtt)
        charAtt->id = 0;

    if (charModel)
    {
        charModel->bSheathe = false;
        charModel->scale_ = 1.0f;
        charModel->rot_ = charModel->pos_ = glm::vec3(0.0f);

        app.scene.selModel = charModel;
        initAnimationControl(charModel, app);
        initModelControl(charModel, app);
        resetCameraToModel(app.scene.camera, charModel);
    }

    LOG_INFO << "Character dismounted.";
}

} // namespace ModelLoader
