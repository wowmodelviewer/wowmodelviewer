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
#include "RaceInfos.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "Game.h"
#include "CharDetails.h"
#include "DB2Table.h"
#include "Logger.h"
#include "string_utils.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{

// ---- Character Browser race data ------------------------------------------
struct CharBrowserRace
{
    int         raceID = 0;
    std::string name;
    bool hasMale     = false;
    bool hasFemale   = false;
    bool hasMaleHD   = false;
    bool hasFemaleHD = false;
};

std::vector<CharBrowserRace> s_races;
std::vector<size_t>          s_filtered;
bool  s_built        = false;
bool  s_filterDirty  = true;
char  s_searchBuf[256] = {};

// ---- Character Viewer state -----------------------------------------------
int   s_selectedRaceIdx = -1;
int   s_gender          = 0;       // 0 = Male, 1 = Female
bool  s_preferHD        = true;
float s_leftWidth       = 220.0f;
float s_rightWidth      = 220.0f;

// ---- Helpers --------------------------------------------------------------
void buildRaceList()
{
    s_races.clear();

    const auto* chrRaces = WOWDB.getTable("ChrRaces");

    std::map<int, CharBrowserRace> raceMap;
    for (const auto& [fileID, info] : RaceInfos::getAllRaces())
    {
        auto& entry = raceMap[info.raceID];
        entry.raceID = info.raceID;

        if (entry.name.empty())
        {
            if (chrRaces)
            {
                auto row = chrRaces->getRow(static_cast<uint32_t>(info.raceID));
                if (row)
                    entry.name = row.getString("Name_Lang");
            }
            if (entry.name.empty())
                entry.name = info.prefix.empty()
                    ? ("Race " + std::to_string(info.raceID))
                    : info.prefix;
        }

        if (info.sexID == GENDER_MALE)
        {
            if (info.isHD) entry.hasMaleHD = true;
            else           entry.hasMale   = true;
        }
        else
        {
            if (info.isHD) entry.hasFemaleHD = true;
            else           entry.hasFemale   = true;
        }
    }

    for (auto& [id, race] : raceMap)
        s_races.push_back(std::move(race));

    std::sort(s_races.begin(), s_races.end(),
        [](const CharBrowserRace& a, const CharBrowserRace& b)
        { return a.name < b.name; });

    s_filterDirty = true;
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

void loadRace(int raceID, int gender, bool preferHD,
              const CharacterViewerPanel::DrawContext& ctx)
{
    int fileID     = -1;
    int fallbackID = -1;

    for (const auto& [fid, info] : RaceInfos::getAllRaces())
    {
        if (info.raceID == raceID && info.sexID == gender)
        {
            if (preferHD && info.isHD)
            {
                fileID = fid;
                break;
            }
            else if (!preferHD && !info.isHD)
            {
                fileID = fid;
                break;
            }
            if (fallbackID == -1)
                fallbackID = fid;
        }
    }

    if (fileID == -1)
        fileID = fallbackID;

    if (fileID > 0)
    {
        GameFile* file = GAMEDIRECTORY.getFile(fileID);
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
                if (ImGui::Selectable(race.name.c_str(), selected))
                {
                    s_selectedRaceIdx = i;
                    loadRace(race.raceID, s_gender, s_preferHD, ctx);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Gender radio
        {
            int prevGender = s_gender;
            ImGui::RadioButton("Male##cv",   &s_gender, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Female##cv", &s_gender, 1);
            if (s_gender != prevGender && s_selectedRaceIdx >= 0)
                loadRace(s_races[s_selectedRaceIdx].raceID,
                         s_gender, s_preferHD, ctx);
        }

        // HD toggle
        {
            bool prevHD = s_preferHD;
            ImGui::Checkbox("HD Model##cv", &s_preferHD);
            if (s_preferHD != prevHD && s_selectedRaceIdx >= 0)
                loadRace(s_races[s_selectedRaceIdx].raceID,
                         s_gender, s_preferHD, ctx);
        }

        // ---- Customization Options ----
        if (isChar && ctx.customizationOptions && !ctx.customizationOptions->empty())
        {
            ImGui::SeparatorText("Customization");
            for (auto& opt : *ctx.customizationOptions)
            {
                if (opt.choiceNames.empty()) continue;
                const char* preview =
                    (opt.selectedIndex >= 0 &&
                     opt.selectedIndex < static_cast<int>(opt.choiceNames.size()))
                    ? opt.choiceNames[opt.selectedIndex].c_str()
                    : "<none>";
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(opt.name.c_str(), preview))
                {
                    for (int c = 0; c < static_cast<int>(opt.choiceNames.size()); ++c)
                    {
                        bool sel = (c == opt.selectedIndex);
                        if (ImGui::Selectable(opt.choiceNames[c].c_str(), sel))
                        {
                            opt.selectedIndex = c;
                            model->cd.set(opt.optionID, opt.choiceIDs[c]);
                            model->refresh();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }

        // ---- Display Toggles ----
        if (isChar)
        {
            ImGui::SeparatorText("Display");
            auto& cd = model->cd;
            bool changed = false;
            changed |= ImGui::Checkbox("Underwear##cv",    &cd.showUnderwear);
            changed |= ImGui::Checkbox("Hair##cv",         &cd.showHair);
            changed |= ImGui::Checkbox("Facial Hair##cv",  &cd.showFacialHair);
            changed |= ImGui::Checkbox("Ears##cv",         &cd.showEars);
            changed |= ImGui::Checkbox("Feet##cv",         &cd.showFeet);

            int eyeGlow = static_cast<int>(cd.eyeGlowType);
            ImGui::Text("Eye Glow:");
            if (ImGui::RadioButton("None##cveg",    &eyeGlow, EGT_NONE))        changed = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Default##cveg", &eyeGlow, EGT_DEFAULT))     changed = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("DK##cveg",      &eyeGlow, EGT_DEATHKNIGHT)) changed = true;
            cd.eyeGlowType = static_cast<EyeGlowTypes>(eyeGlow);

            if (changed) model->refresh();
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
