#include "SettingsPanel.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "AppSettings.h"
#include "OrbitCamera.h"
#include "ThemeManager.h"
#include "string_utils.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

// Forward-declare the font entry type (defined in AppState.h)
#include "AppState.h"

// Game directory access for version / locale display
#include "Game.h"
#include "WoWFolder.h"

namespace
{

// Refresh the folder-picker entry list.
void folderPickerRefresh(SettingsPanel::DrawContext& ctx)
{
    ctx.folderPickerEntries->clear();
    namespace fs = std::filesystem;
    std::error_code ec;

    if (ctx.folderPickerCurrent->empty())
    {
#ifdef _WIN32
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (drives & (1u << i))
            {
                std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
                ctx.folderPickerEntries->push_back(fs::path(root));
            }
        }
#else
        ctx.folderPickerEntries->push_back(fs::path("/"));
#endif
    }
    else
    {
        for (auto& entry : fs::directory_iterator(*ctx.folderPickerCurrent,
                                                   fs::directory_options::skip_permission_denied, ec))
        {
            if (entry.is_directory(ec))
                ctx.folderPickerEntries->push_back(entry.path());
        }
        std::sort(ctx.folderPickerEntries->begin(), ctx.folderPickerEntries->end(),
            [](const fs::path& a, const fs::path& b)
            {
                return core::toLower(a.filename().string()) < core::toLower(b.filename().string());
            });
    }

    *ctx.folderPickerNeedsRefresh = false;
}

// Open the folder picker, seeding from the current path buffer.
void openFolderPicker(SettingsPanel::DrawContext& ctx)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path startDir(*ctx.pathBuf);
    if (fs::is_directory(startDir, ec))
        *ctx.folderPickerCurrent = startDir;
    else if (startDir.has_parent_path() && fs::is_directory(startDir.parent_path(), ec))
        *ctx.folderPickerCurrent = startDir.parent_path();
    else
        ctx.folderPickerCurrent->clear();

    *ctx.folderPickerNeedsRefresh = true;
    *ctx.showFolderPicker = true;
}

} // anonymous namespace

void SettingsPanel::draw(DrawContext& ctx)
{
    // ---- Game loading section ----
    ImGui::SeparatorText("World of Warcraft");

    ImGui::Text("Game Data Path:");
    float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##gamepath", ctx.pathBuf);
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
        openFolderPicker(ctx);

    // ---- Folder Picker popup ----
    if (*ctx.showFolderPicker)
        ImGui::OpenPopup("Select Folder##FolderPicker");

    if (ImGui::BeginPopupModal("Select Folder##FolderPicker", ctx.showFolderPicker,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        std::string curPathStr = ctx.folderPickerCurrent->empty()
            ? "My Computer" : ctx.folderPickerCurrent->string();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", curPathStr.c_str());
        ImGui::Separator();

        if (*ctx.folderPickerNeedsRefresh)
            folderPickerRefresh(ctx);

        // Up / back button
        {
            bool canGoUp = !ctx.folderPickerCurrent->empty()
                && ctx.folderPickerCurrent->has_parent_path()
                && ctx.folderPickerCurrent->parent_path() != *ctx.folderPickerCurrent;
            bool canGoRoot = !ctx.folderPickerCurrent->empty();
            if (!canGoUp && !canGoRoot) ImGui::BeginDisabled();
            if (ImGui::Button("Up"))
            {
                if (canGoUp)
                    *ctx.folderPickerCurrent = ctx.folderPickerCurrent->parent_path();
                else
                    ctx.folderPickerCurrent->clear();
                *ctx.folderPickerNeedsRefresh = true;
            }
            if (!canGoUp && !canGoRoot) ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::Text("%d folders", static_cast<int>(ctx.folderPickerEntries->size()));

        // Folder list
        ImGui::BeginChild("##FolderList", ImVec2(500, 400), ImGuiChildFlags_Borders);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ctx.folderPickerEntries->size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& p = (*ctx.folderPickerEntries)[i];
                std::string displayName = ctx.folderPickerCurrent->empty()
                    ? p.string() : p.filename().string();
                ImGui::PushID(i);
                if (ImGui::Selectable(displayName.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        *ctx.folderPickerCurrent = p;
                        *ctx.folderPickerNeedsRefresh = true;
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Select This Folder", ImVec2(200, 0)))
        {
            if (!ctx.folderPickerCurrent->empty())
            {
                *ctx.pathBuf = ctx.folderPickerCurrent->string();
                *ctx.showFolderPicker = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            *ctx.showFolderPicker = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ctx.isWoWLoaded)
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded: %s (%s)",
                           GAMEDIRECTORY.version().c_str(),
                           GAMEDIRECTORY.locale().c_str());
    }
    else if (ctx.loadInProgress)
    {
        ImGui::ProgressBar(ctx.loadProgress->load());
        auto status = ctx.getLoadStatus();
        ImGui::TextWrapped("%s", status.c_str());
    }
    else
    {
        auto status = ctx.getLoadStatus();
        if (!status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        else
            ImGui::TextDisabled("Use File > Load WoW to load game data.");
    }

    ImGui::Checkbox("Enable Database Cache", &ctx.settings->enableDbCache);
    ImGui::TextDisabled("Speeds up loading by caching the database. Takes effect on next load.");

    // ---- General section ----
    ImGui::SeparatorText("General");
    if (ImGui::Checkbox("Show Console Window", &ctx.settings->showConsole))
    {
#ifdef _WIN32
        if (HWND hConsole = GetConsoleWindow())
            ShowWindow(hConsole, ctx.settings->showConsole ? SW_SHOW : SW_HIDE);
#endif
    }
    ImGui::TextDisabled("Shows/hides the debug console. Useful for diagnostics.");

    // ---- Theme selector ----
    ImGui::SeparatorText("Appearance");
    ImGui::Text("Theme:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Theme", &ThemeManager::currentThemeRef(),
                     ThemeManager::themeNames(), ThemeManager::themeCount()))
        ThemeManager::apply(ThemeManager::currentTheme(), ctx.window);

    // ---- Font selector ----
    ImGui::Text("Font:");
    ImGui::SetNextItemWidth(-1);
    if (!ctx.availableFonts->empty())
    {
        if (ImGui::BeginCombo("##Font",
            (ctx.settings->currentFont >= 0 &&
             ctx.settings->currentFont < static_cast<int>(ctx.availableFonts->size()))
                ? (*ctx.availableFonts)[ctx.settings->currentFont].name.c_str() : "Default"))
        {
            for (int i = 0; i < static_cast<int>(ctx.availableFonts->size()); ++i)
            {
                ImGui::PushID(i);
                const bool selected = (i == ctx.settings->currentFont);
                if (ImGui::Selectable((*ctx.availableFonts)[i].name.c_str(), selected))
                {
                    ctx.settings->currentFont = i;
                    *ctx.fontsDirty = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextDisabled("No .ttf/.otf files found in fonts/ directory.");
    }

    ImGui::Text("Font Size:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##FontSize", &ctx.settings->fontSize, 10.0f, 40.0f, "%.0f px"))
        *ctx.fontsDirty = true;
    ImGui::TextDisabled("Drop .ttf or .otf files into the fonts/ folder to add more.");

    ImGui::Separator();
    ImGui::Checkbox("ImGui Demo Window", ctx.showDemoWindow);
    ImGui::Separator();
    ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                ctx.camera->yaw(), ctx.camera->pitch(), ctx.camera->radius());
    ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));

    // ---- Save ----
    ImGui::SeparatorText("Save");
    if (ImGui::Button("Save Settings", ImVec2(-1, 0)))
    {
        ctx.settings->gamePath = *ctx.pathBuf;
        ctx.settings->save();
    }
    ImGui::TextDisabled("Saves preferences and UI layout.");
}
