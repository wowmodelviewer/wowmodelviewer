#include "PresetManager.h"
#include "AppState.h"
#include "ModelLoader.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstring>
#include <filesystem>
#include <string>

#include "Logger.h"
#include "IniFile.h"
#include "WoWModel.h"
#include "WoWItem.h"
#include "CharDetails.h"

namespace PresetManager
{

void save(const char* path, AppState& app)
{
    WoWModel* model = ModelLoader::getLoadedModel(app);
    if (!model || !app.isChar)
    {
        app.presetStatus = "No character model loaded.";
        return;
    }

    std::string pathStr{path};
    core::IniFile ini{pathStr};

    const auto& cd = model->cd;
    ini.setValue("Display/ShowUnderwear", cd.showUnderwear);
    ini.setValue("Display/ShowHair", cd.showHair);
    ini.setValue("Display/ShowFacialHair", cd.showFacialHair);
    ini.setValue("Display/ShowEars", cd.showEars);
    ini.setValue("Display/ShowFeet", cd.showFeet);
    ini.setValue("Display/AutoHideGeosets", cd.autoHideGeosetsForHeadItems);
    ini.setValue("Display/Sheathe", model->bSheathe);
    ini.setValue("Display/EyeGlow", static_cast<int>(cd.eyeGlowType));

    int optIdx = 0;
    for (const auto& opt : app.customizationOptions)
    {
        std::string key = "Customization/" + std::to_string(optIdx);
        ini.setValue(key + "_OptionID", static_cast<int>(opt.optionID));
        if (opt.selectedIndex >= 0 && opt.selectedIndex < static_cast<int>(opt.choiceIDs.size()))
            ini.setValue(key + "_ChoiceID", static_cast<int>(opt.choiceIDs[opt.selectedIndex]));
        ++optIdx;
    }
    ini.setValue("Customization/Count", optIdx);

    for (int s = 0; s < NUM_CHAR_SLOTS; ++s)
    {
        WoWItem* witem = model->getItem(static_cast<CharSlots>(s));
        std::string key = "Equipment/" + std::to_string(s);
        ini.setValue(key + "_ID", witem ? static_cast<int>(witem->id()) : 0);
        ini.setValue(key + "_Level", app.equipSlotLevels[s]);
    }

    ini.sync();
    app.presetStatus = std::string("Preset saved: ") + path;
    LOG_INFO << "Character preset saved to " << path;
}

void load(const char* path, AppState& app)
{
    WoWModel* model = ModelLoader::getLoadedModel(app);
    if (!model || !app.isChar)
    {
        app.presetStatus = "No character model loaded.";
        return;
    }

    if (!std::filesystem::exists(path))
    {
        app.presetStatus = std::string("File not found: ") + path;
        return;
    }

    std::string pathStr{path};
    core::IniFile ini{pathStr};

    auto& cd = model->cd;
    cd.showUnderwear = ini.getBool("Display/ShowUnderwear", true);
    cd.showHair = ini.getBool("Display/ShowHair", true);
    cd.showFacialHair = ini.getBool("Display/ShowFacialHair", true);
    cd.showEars = ini.getBool("Display/ShowEars", true);
    cd.showFeet = ini.getBool("Display/ShowFeet", false);
    cd.autoHideGeosetsForHeadItems = ini.getBool("Display/AutoHideGeosets", true);
    model->bSheathe = ini.getBool("Display/Sheathe", false);
    cd.eyeGlowType = static_cast<EyeGlowTypes>(ini.getInt("Display/EyeGlow", EGT_DEFAULT));

    int optCount = ini.getInt("Customization/Count", 0);
    for (int i = 0; i < optCount; ++i)
    {
        std::string key = "Customization/" + std::to_string(i);
        unsigned int optionID = static_cast<unsigned int>(ini.getInt(key + "_OptionID", 0));
        unsigned int choiceID = static_cast<unsigned int>(ini.getInt(key + "_ChoiceID", 0));
        if (optionID == 0) continue;

        cd.set(optionID, choiceID);

        for (auto& opt : app.customizationOptions)
        {
            if (opt.optionID == optionID)
            {
                for (int c = 0; c < static_cast<int>(opt.choiceIDs.size()); ++c)
                {
                    if (opt.choiceIDs[c] == choiceID)
                    {
                        opt.selectedIndex = c;
                        break;
                    }
                }
                break;
            }
        }
    }

    for (int s = 0; s < NUM_CHAR_SLOTS; ++s)
    {
        std::string key = "Equipment/" + std::to_string(s);
        int itemId = ini.getInt(key + "_ID", 0);
        int level = ini.getInt(key + "_Level", 0);

        WoWItem* witem = model->getItem(static_cast<CharSlots>(s));
        if (witem)
        {
            witem->setId(itemId);
            if (level > 0) witem->setLevel(level);
        }
        app.equipSlotLevels[s] = level;
    }

    model->refresh();
    app.presetStatus = std::string("Preset loaded: ") + path;
    LOG_INFO << "Character preset loaded from " << path;
}

} // namespace PresetManager
