#include "URLImportHandler.h"
#include "AppState.h"
#include "ModelLoader.h"

#include <string>

#include "Logger.h"
#include "ImporterPlugin.h"
#include "CharInfos.h"
#include "NPCInfos.h"
#include "RaceInfos.h"
#include "WoWModel.h"
#include "WoWItem.h"
#include "CharDetails.h"
#include "Game.h"
#include "WoWDatabase.h"
#include "DB2Table.h"
#include "database.h"
#include "wow_enums.h"

namespace
{

void applyImportedChar(CharInfos* info, AppState& app)
{
    if (!info || !info->valid)
    {
        app.exporting.importStatus = "Import returned no valid character data.";
        return;
    }

    // Find the character model by race + gender
    int raceID = static_cast<int>(info->raceId);
    int sexID = (info->gender == "FEMALE" || info->gender == "Female") ? 1 : 0;

    int fileDataID = RaceInfos::getFileIDForRaceSex(raceID, sexID);
    if (fileDataID <= 0)
    {
        app.exporting.importStatus = "Could not determine model for race " + std::to_string(raceID);
        return;
    }

    GameFile* file = GAMEDIRECTORY.getFile(fileDataID);
    if (!file)
    {
        app.exporting.importStatus = "Model file not found for race " + std::to_string(raceID);
        return;
    }

    ModelLoader::loadModel(file, app);

    WoWModel* model = ModelLoader::getLoadedModel(app);
    if (!model || !app.scene.isChar)
    {
        app.exporting.importStatus = "Failed to load character model.";
        return;
    }

    // Apply customizations
    auto& cd = model->cd;
    for (const auto& [optionId, choiceId] : info->customizations)
        cd.set(optionId, choiceId);

    // Apply eye glow
    cd.eyeGlowType = static_cast<EyeGlowTypes>(info->eyeGlowType);

    // Apply equipment
    for (int s = 0; s < NUM_CHAR_SLOTS && s < static_cast<int>(info->equipment.size()); ++s)
    {
        int itemId = info->equipment[s];
        if (itemId <= 0) continue;
        WoWItem* witem = model->getItem(static_cast<CharSlots>(s));
        if (witem)
            witem->setId(itemId);
    }

    model->refresh();

    // Update customization UI state
    ModelLoader::initCharacterControl(model, app);

    app.exporting.importStatus = "Character imported successfully.";
    LOG_INFO << "Character imported from URL.";
}

void applyImportedNPC(NPCInfos* info, AppState& app)
{
    if (!info || info->displayId <= 0)
    {
        app.exporting.importStatus = "Import returned no valid NPC data.";
        return;
    }

    // Use DB2Table to resolve CreatureDisplayInfo -> CreatureModelData -> FileDataID
    const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
    const auto* cmdTable = WOWDB.getTable("CreatureModelData");
    if (!cdiTable || !cmdTable)
    {
        app.exporting.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }
    auto cdiRow = cdiTable->getRow(static_cast<uint32_t>(info->displayId));
    if (!cdiRow)
    {
        app.exporting.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }
    auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
    uint32_t npcFDID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
    if (npcFDID == 0)
    {
        app.exporting.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }

    GameFile* file = GAMEDIRECTORY.getFile(npcFDID);
    if (!file)
    {
        app.exporting.importStatus = "NPC model file not found.";
        return;
    }

    ModelLoader::loadModel(file, app);
    app.exporting.importStatus = std::string("NPC imported: ") + ModelLoader::wstringToString(info->name);
    LOG_INFO << "NPC imported from URL: " << ModelLoader::wstringToString(info->name);
}

void applyImportedItem(ItemRecord* rec, AppState& app)
{
    if (!rec || rec->id <= 0)
    {
        app.exporting.importStatus = "Import returned no valid item data.";
        return;
    }

    ModelLoader::loadItemModel(static_cast<unsigned int>(rec->id), app);
    app.exporting.importStatus = std::string("Item imported: ") + rec->name;
    LOG_INFO << "Item imported from URL: " << rec->name;
}

} // anonymous namespace

namespace URLImportHandler
{

void doImport(AppState& app)
{
    std::string url(app.exporting.importUrlBuf);
    if (url.empty())
    {
        app.exporting.importStatus = "Please enter a URL.";
        return;
    }

    app.exporting.importStatus = "Importing...";

    // Find matching importer
    ImporterPlugin* importer = nullptr;
    for (const auto& imp : app.exporting.importers)
    {
        if (imp->acceptURL(url))
        {
            importer = imp.get();
            break;
        }
    }

    if (!importer)
    {
        app.exporting.importStatus = "No importer recognises this URL. Supported: battle.net, worldofwarcraft.com, wowhead.com";
        return;
    }

    // Try character import first (Armory)
    CharInfos* charInfo = importer->importChar(url);
    if (charInfo && charInfo->valid)
    {
        applyImportedChar(charInfo, app);
        delete charInfo;
        return;
    }
    delete charInfo;

    // Try NPC import (Wowhead)
    NPCInfos* npcInfo = importer->importNPC(url);
    if (npcInfo && npcInfo->displayId > 0)
    {
        applyImportedNPC(npcInfo, app);
        delete npcInfo;
        return;
    }
    delete npcInfo;

    // Try item import
    ItemRecord* itemRec = importer->importItem(url);
    if (itemRec && itemRec->id > 0)
    {
        applyImportedItem(itemRec, app);
        delete itemRec;
        return;
    }
    delete itemRec;

    app.exporting.importStatus = "Could not import anything from this URL.";
}

} // namespace URLImportHandler
