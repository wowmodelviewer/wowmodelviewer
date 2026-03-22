// ============================================================================
// CharacterViewerPanel  –  standalone Character Viewer tab (wow.export-style)
//
// Three-column layout: Race/Customization (left), 3D Viewport with animation
// bar (center), Equipment placeholder (right).
// ============================================================================
#include "CharacterViewerPanel.h"

#include <cassert>

#include "imgui.h"
#include "imgui_internal.h"

#include "WoWModel.h"
#include "OrbitCamera.h"
#include "ViewportFBO.h"
#include "AppSettings.h"
#include "Renderer.h"
#include "Attachment.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "Game.h"
#include "CharDetails.h"
#include "DB2Table.h"
#include "Logger.h"
#include "string_utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

// ---- Character Browser race data ------------------------------------------
struct CharBrowserRace
{
    int         raceID = 0;
    std::string name;
};

// ---- Body type data (matches wow.export ChrRaceXChrModel flow) ------------
struct BodyType
{
    int         chrModelID = 0;
    int         fileDataID = 0;
    std::string label;
};

std::vector<CharBrowserRace> s_races;
std::vector<BodyType>        s_bodyTypes;
std::vector<size_t>          s_filtered;
bool  s_built        = false;
bool  s_filterDirty  = true;
char  s_searchBuf[256] = {};

// ---- Character Viewer state -----------------------------------------------
int   s_selectedRaceIdx  = -1;
int   s_selectedBodyType = -1;
float s_leftWidth        = 280.0f;
float s_rightWidth       = 220.0f;

// ---- Helpers --------------------------------------------------------------

/// Build race list from ChrRaceXChrModel + ChrRaces (matches wow.export).
/// Only includes races that have at least one model in ChrRaceXChrModel.
void buildRaceList()
{
    s_races.clear();

    const auto* chrRaces = WOWDB.getTable("ChrRaces");
    const auto* xTable   = WOWDB.getTable("ChrRaceXChrModel");
    if (!chrRaces || !xTable) return;

    // Collect race IDs that have at least one model
    std::set<int> raceIdsWithModels;
    for (const auto& row : *xTable)
        raceIdsWithModels.insert(static_cast<int>(row.getUInt("ChrRacesID")));

    std::map<int, CharBrowserRace> raceMap;
    for (int rid : raceIdsWithModels)
    {
        auto row = chrRaces->getRow(static_cast<uint32_t>(rid));
        if (!row)
            continue;

        CharBrowserRace race;
        race.raceID = rid;
        race.name = row.getString("Name_Lang");
        if (race.name.empty())
            race.name = "Race " + std::to_string(rid);

        raceMap[rid] = std::move(race);
    }

    for (auto& [id, race] : raceMap)
        s_races.push_back(std::move(race));

    std::sort(s_races.begin(), s_races.end(),
        [](const CharBrowserRace& a, const CharBrowserRace& b)
        { return a.name < b.name; });

    s_filterDirty = true;
}

/// Build body type list for a given race from ChrRaceXChrModel → ChrModel →
/// CreatureDisplayInfo → CreatureModelData → FileDataID (matches wow.export).
void buildBodyTypes(int raceID)
{
    s_bodyTypes.clear();
    s_selectedBodyType = -1;

    const auto* xTable   = WOWDB.getTable("ChrRaceXChrModel");
    const auto* chrModel = WOWDB.getTable("ChrModel");
    const auto* cdi      = WOWDB.getTable("CreatureDisplayInfo");
    const auto* cmd      = WOWDB.getTable("CreatureModelData");
    if (!xTable || !chrModel || !cdi || !cmd) return;

    struct ModelEntry { int sex; int chrModelID; };
    std::vector<ModelEntry> models;

    for (const auto& row : *xTable)
    {
        if (static_cast<int>(row.getUInt("ChrRacesID")) == raceID)
        {
            const int modelID = static_cast<int>(row.getUInt("ChrModelID"));
            DB2Row modelRow = chrModel->getRow(static_cast<uint32_t>(modelID));
            if (!modelRow) continue;
            const int sex = static_cast<int>(modelRow.getUInt("Sex"));
            models.push_back({ sex, modelID });
        }
    }

    std::sort(models.begin(), models.end(),
        [](const ModelEntry& a, const ModelEntry& b) { return a.sex < b.sex; });

    for (const auto& m : models)
    {
        DB2Row modelRow = chrModel->getRow(static_cast<uint32_t>(m.chrModelID));
        if (!modelRow) continue;

        DB2Row displayRow = cdi->getRow(modelRow.getUInt("DisplayID"));
        if (!displayRow) continue;

        DB2Row modelDataRow = cmd->getRow(displayRow.getUInt("ModelID"));
        if (!modelDataRow) continue;

        BodyType bt;
        bt.chrModelID = m.chrModelID;
        bt.fileDataID = static_cast<int>(modelDataRow.getUInt("FileDataID"));
        bt.label = (m.sex == 0) ? "Male" : (m.sex == 1) ? "Female" : "Body " + std::to_string(m.sex + 1);
        s_bodyTypes.push_back(std::move(bt));
    }
}

void rebuildFilter()
{
    s_filtered.clear();

    std::string search = core::toLower(std::string(s_searchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < s_races.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(s_races[i].name, search))
            continue;
        s_filtered.push_back(i);
    }

    s_filterDirty = false;
}

void loadBodyType(int fileDataID, const CharacterViewerPanel::DrawContext& ctx)
{
    if (fileDataID > 0)
    {
        GameFile* file = GAMEDIRECTORY.getFile(fileDataID);
        if (file && ctx.loadModel)
            ctx.loadModel(file);
    }
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================
namespace CharacterViewerPanel
{

void draw(const DrawContext& ctx)
{
    assert(ctx.customizationOptions && "DrawContext::customizationOptions must not be null");
    assert(ctx.animEntries && "DrawContext::animEntries must not be null");
    assert(ctx.selectedAnimCombo && "DrawContext::selectedAnimCombo must not be null");
    assert(ctx.fbo && "DrawContext::fbo must not be null");
    assert(ctx.camera && "DrawContext::camera must not be null");

    if (!ctx.isWoWLoaded || !ctx.isDBReady)
    {
        ImGui::TextDisabled("Game not loaded.");
        return;
    }

    // Lazy-init race list
    if (!s_built)
    {
        buildRaceList();
        s_built = true;
    }

    WoWModel* model = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    const bool isChar = model && ctx.isChar;

    const ImVec2 avail   = ImGui::GetContentRegionAvail();
    const float  spacing = ImGui::GetStyle().ItemSpacing.x;
    const float  centerW = avail.x - s_leftWidth - s_rightWidth - spacing * 2.0f;

    // =================================================================
    // LEFT COLUMN – Race selector + Customization
    // =================================================================
    ImGui::BeginChild("##cvLeft", ImVec2(s_leftWidth, -1), ImGuiChildFlags_Borders);
    {
        // ---- Race Selector ----
        ImGui::SeparatorText("Race");

        const char* racePreview =
            (s_selectedRaceIdx >= 0 &&
             s_selectedRaceIdx < static_cast<int>(s_races.size()))
            ? s_races[s_selectedRaceIdx].name.c_str()
            : "<select race>";

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##cvRace", racePreview))
        {
            for (int i = 0; i < static_cast<int>(s_races.size()); ++i)
            {
                const auto& race = s_races[i];
                bool selected = (i == s_selectedRaceIdx);
                std::string label = race.name + "##race" + std::to_string(race.raceID);
                if (ImGui::Selectable(label.c_str(), selected) && !selected)
                {
                    s_selectedRaceIdx = i;
                    buildBodyTypes(race.raceID);
                    if (!s_bodyTypes.empty())
                    {
                        s_selectedBodyType = 0;
                        loadBodyType(s_bodyTypes[0].fileDataID, ctx);
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Body type selector (populated from ChrRaceXChrModel)
        if (!s_bodyTypes.empty())
        {
            const char* bodyPreview =
                (s_selectedBodyType >= 0 &&
                 s_selectedBodyType < static_cast<int>(s_bodyTypes.size()))
                ? s_bodyTypes[s_selectedBodyType].label.c_str()
                : "<select body>";

            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##cvBodyType", bodyPreview))
            {
                for (int i = 0; i < static_cast<int>(s_bodyTypes.size()); ++i)
                {
                    bool selected = (i == s_selectedBodyType);
                    std::string bodyLabel = s_bodyTypes[i].label + "##body" + std::to_string(s_bodyTypes[i].chrModelID);
                    if (ImGui::Selectable(bodyLabel.c_str(), selected) && !selected)
                    {
                        s_selectedBodyType = i;
                        loadBodyType(s_bodyTypes[i].fileDataID, ctx);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // ---- Customization Options (grouped by category) ----
        if (isChar && ctx.customizationOptions && !ctx.customizationOptions->empty())
        {
            // Try to load ChrCustomizationCategory table for section names
            const auto* catTable = WOWDB.getTable("ChrCustomizationCategory");

            unsigned int lastCatID = UINT_MAX;
            for (auto& opt : *ctx.customizationOptions)
            {
                if (opt.choiceNames.empty()) continue;

                // Show section header when category changes
                if (opt.categoryID != lastCatID)
                {
                    lastCatID = opt.categoryID;
                    std::string catName;
                    if (catTable)
                    {
                        auto catRow = catTable->getRow(opt.categoryID);
                        if (catRow)
                            catName = catRow.getString("CategoryName_Lang");
                    }
                    if (catName.empty())
                        catName = "Customization";
                    ImGui::SeparatorText(catName.c_str());
                }

                const char* preview =
                    (opt.selectedIndex >= 0 &&
                     opt.selectedIndex < static_cast<int>(opt.choiceNames.size()))
                    ? opt.choiceNames[opt.selectedIndex].c_str()
                    : "<none>";
                ImGui::Text("%s:", opt.name.c_str());
                ImGui::SetNextItemWidth(-1);
                std::string comboID = "##cvOpt" + std::to_string(opt.optionID);
                if (ImGui::BeginCombo(comboID.c_str(), preview))
                {
                    for (int c = 0; c < static_cast<int>(opt.choiceNames.size()); ++c)
                    {
                        bool sel = (c == opt.selectedIndex);
                        std::string choiceLabel = opt.choiceNames[c] + "##ch" + std::to_string(opt.choiceIDs[c]);
                        if (ImGui::Selectable(choiceLabel.c_str(), sel))
                        {
                            if (c != opt.selectedIndex)
                            {
                                opt.selectedIndex = c;
                                model->cd.set(opt.optionID, opt.choiceIDs[c]);
                                model->refresh();
                            }
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // =================================================================
    // CENTER COLUMN – Animation controls + 3D Viewport
    // =================================================================
    ImGui::BeginChild("##cvCenter",
                      ImVec2(centerW > 100.0f ? centerW : 100.0f, -1),
                      ImGuiChildFlags_None);
    {
        // ---- Animation Controls Bar ----
        if (isChar && model->animated &&
            ctx.animEntries && !ctx.animEntries->empty() && ctx.selectedAnimCombo)
        {
            auto& animEntries      = *ctx.animEntries;
            int&  selectedAnimCombo = *ctx.selectedAnimCombo;

            const char* animPreview =
                (selectedAnimCombo >= 0 &&
                 selectedAnimCombo < static_cast<int>(animEntries.size()))
                ? animEntries[selectedAnimCombo].label.c_str()
                : "<none>";

            ImGui::SetNextItemWidth(180);
            if (ImGui::BeginCombo("##cvAnim", animPreview))
            {
                for (int i = 0; i < static_cast<int>(animEntries.size()); ++i)
                {
                    bool selected = (i == selectedAnimCombo);
                    if (ImGui::Selectable(animEntries[i].label.c_str(), selected))
                    {
                        selectedAnimCombo = i;
                        int idx = animEntries[i].animIndex;
                        model->currentAnim = idx;
                        model->animManager->SetAnim(0, idx, 0);
                        model->animManager->Play();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("\xE2\x97\x80##cv"))
                model->animManager->PrevFrame();
            ImGui::SameLine();
            if (model->animManager->IsPaused())
            {
                if (ImGui::Button("\xE2\x96\xB6##cv"))
                    model->animManager->Play();
            }
            else
            {
                if (ImGui::Button("\xE2\x8F\xB8##cv"))
                    model->animManager->Pause();
            }
            ImGui::SameLine();
            if (ImGui::Button("\xE2\x96\xB8##cv"))
                model->animManager->NextFrame();

            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            int frameCount = static_cast<int>(model->animManager->GetFrameCount());
            int curFrame   = static_cast<int>(model->animManager->GetFrame());
            if (frameCount > 0)
            {
                if (ImGui::SliderInt("##cvFrame", &curFrame, 0, frameCount))
                    model->animManager->SetFrame(static_cast<size_t>(curFrame));
            }

            ImGui::SameLine();
            ImGui::Text("%d", curFrame);
        }

        // ---- 3D Viewport ----
        ImVec2 vpSize = ImGui::GetContentRegionAvail();
        int vpW = static_cast<int>(vpSize.x);
        int vpH = static_cast<int>(vpSize.y);

        if (vpW > 0 && vpH > 0 && ctx.fbo && ctx.camera && ctx.root && ctx.renderer)
        {
            ctx.renderer->renderScene(*ctx.fbo, vpW, vpH, *ctx.camera,
                                       ctx.fov, ctx.bgColor, ctx.drawGrid,
                                       [root = ctx.root]() {
                                           glEnable(GL_LIGHTING);
                                           glEnable(GL_TEXTURE_2D);
                                           glEnable(GL_DEPTH_TEST);
                                           glDepthFunc(GL_LEQUAL);
                                           root->draw();
                                           glEnable(GL_TEXTURE_2D);
                                           glDisable(GL_LIGHTING);
                                           glDepthMask(GL_FALSE);
                                           glEnable(GL_BLEND);
                                           root->drawParticles();
                                           glDisable(GL_BLEND);
                                           glDepthMask(GL_TRUE);
                                       });
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<uintptr_t>(ctx.fbo->colorTex)),
                vpSize, ImVec2(0, 1), ImVec2(1, 0));

            if (ImGui::IsItemHovered() && ctx.handleViewportInput)
                ctx.handleViewportInput();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // =================================================================
    // RIGHT COLUMN – Equipment slots (placeholder)
    // =================================================================
    ImGui::BeginChild("##cvRight", ImVec2(s_rightWidth, -1), ImGuiChildFlags_Borders);
    {
        ImGui::SeparatorText("Equipment");

        static const char* slotLabels[] = {
            "Head", "Neck", "Shoulder", "Back", "Chest", "Shirt",
            "Tabard", "Wrist", "Hands", "Waist", "Legs", "Feet",
            "Main-hand", "Off-hand"
        };

        for (int i = 0; i < 14; ++i)
        {
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(i);
            ImGui::BeginGroup();
            ImGui::Text("%s:", slotLabels[i]);
            ImGui::SameLine(w - ImGui::CalcTextSize("Empty").x);
            ImGui::TextDisabled("Empty");
            ImGui::EndGroup();
            if (i < 13) ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::Button("Clear All Equipment", ImVec2(-1, 0)))
        {
            // TODO: clear all equipment when item loading is re-enabled
        }

        // ---- Export Section ----
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SeparatorText("Export");

        if (isChar)
        {
            if (ImGui::Button("Export glTF", ImVec2(-1, 0)))
            {
                // TODO: quick-export from Character Viewer
                ImGui::SetWindowFocus("Export");
            }
        }
        else
        {
            ImGui::TextDisabled("Load a character first.");
        }
    }
    ImGui::EndChild();
}

} // namespace CharacterViewerPanel
