#include "AppDialogs.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>

#include "imgui.h"

#include "AppState.h"
#include "GameLoader.h"
#include "URLImportHandler.h"

// Engine headers for version / locale queries
#include "Game.h"
#include "WoWFolder.h"
#include "GlobalSettings.h"

#include <string>

// ---- URL Import dialog ----------------------------------------------------

void AppDialogs::drawImportDialog(AppState& app)
{
    if (app.showImportDialog)
        ImGui::OpenPopup("Import from URL##ImportModal");

    if (ImGui::BeginPopupModal("Import from URL##ImportModal", &app.showImportDialog,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Paste an Armory, Battle.net, or Wowhead URL:");
        ImGui::Spacing();

        if (app.importPopupJustOpened)
        {
            ImGui::SetKeyboardFocusHere();
            app.importPopupJustOpened = false;
        }

        ImGui::SetNextItemWidth(500);
        ImGui::InputText("##importUrl", app.importUrlBuf, sizeof(app.importUrlBuf));

        ImGui::Spacing();
        if (ImGui::Button("Import", ImVec2(120, 0)))
            URLImportHandler::doImport(app);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            app.showImportDialog = false;
            ImGui::CloseCurrentPopup();
        }

        if (!app.importStatus.empty())
        {
            ImGui::Spacing();
            bool isError = app.importStatus.find("failed") != std::string::npos ||
                           app.importStatus.find("No ") != std::string::npos ||
                           app.importStatus.find("not") != std::string::npos ||
                           app.importStatus.find("Please") != std::string::npos;
            if (isError)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", app.importStatus.c_str());
            else
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", app.importStatus.c_str());
        }

        ImGui::EndPopup();
    }
}

// ---- Config selection modal -----------------------------------------------

void AppDialogs::drawConfigPopup(AppState& app)
{
    if (app.showConfigPopup)
        ImGui::OpenPopup("Select WoW Config");

    if (ImGui::BeginPopupModal("Select WoW Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Multiple configurations found. Please select one:");
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(app.pendingConfigs.size()); ++i)
        {
            std::string label = app.pendingConfigs[i].locale + " - " + app.pendingConfigs[i].product;
            if (!app.pendingConfigs[i].version.empty())
                label += " (" + app.pendingConfigs[i].version + ")";
            ImGui::RadioButton(label.c_str(), &app.selectedConfig, i);
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0)))
        {
            app.showConfigPopup = false;
            ImGui::CloseCurrentPopup();
            GameLoader::launchLoadThread(app.pendingConfigs[app.selectedConfig], app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            app.showConfigPopup = false;
            GameLoader::setLoadStatus("Load cancelled.", app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---- About dialog ---------------------------------------------------------

void AppDialogs::drawAboutDialog(AppState& app)
{
    if (app.showAboutDialog)
        ImGui::OpenPopup("About WoW Model Viewer");

    if (ImGui::BeginPopupModal("About WoW Model Viewer", &app.showAboutDialog,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        // Convert wstring title to narrow UTF-8 string for ImGui
        std::wstring wTitle = GLOBALSETTINGS.appTitle();
        std::string title;
        if (!wTitle.empty())
        {
#ifdef _WIN32
            int n = WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(),
                        static_cast<int>(wTitle.size()), nullptr, 0, nullptr, nullptr);
            title.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(),
                        static_cast<int>(wTitle.size()), title.data(), n, nullptr, nullptr);
#else
            title.assign(wTitle.begin(), wTitle.end());
#endif
        }
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", title.c_str());

        ImGui::Separator();
        ImGui::Text("A 3D model viewer for World of Warcraft game assets.");
        ImGui::Spacing();
        ImGui::Text("Built with GLFW, OpenGL, and Dear ImGui.");
        ImGui::Text("Uses CASCLib for game data access.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "https://wowmodelviewer.net");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "https://github.com/wowmodelviewer/wowmodelviewer");
        ImGui::Spacing();
        ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));
        ImGui::Text("GL_VERSION:  %s", glGetString(GL_VERSION));
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            app.showAboutDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---- Language / Locale dialog ---------------------------------------------

void AppDialogs::drawLanguageDialog(AppState& app)
{
    if (app.showLanguageDialog)
        ImGui::OpenPopup("Language / Locale");

    if (ImGui::BeginPopupModal("Language / Locale", &app.showLanguageDialog,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        if (!app.isWoWLoaded)
        {
            ImGui::TextWrapped("Game data is not loaded. Load WoW first, then change the locale here.");
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                app.showLanguageDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::Text("Current locale: %s", GAMEDIRECTORY.locale().c_str());
            ImGui::Separator();
            ImGui::Text("Select a different locale to reload game data:");
            ImGui::Spacing();

            auto configs = GAMEDIRECTORY.configsFound();
            for (int i = 0; i < static_cast<int>(configs.size()); ++i)
            {
                std::string label = configs[i].locale + " - " + configs[i].product;
                bool isCurrent = (configs[i].locale == GAMEDIRECTORY.locale());
                if (isCurrent)
                    ImGui::BeginDisabled();

                if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
                {
                    app.showLanguageDialog = false;
                    ImGui::CloseCurrentPopup();
                    app.isWoWLoaded = false;
                    app.initDB = false;
                    app.loadInProgress = true;
                    app.loadProgress = 0.0f;
                    GameLoader::setLoadStatus("Reloading with locale: " + configs[i].locale + "...", app);
                    GameLoader::launchLoadThread(configs[i], app);
                }

                if (isCurrent)
                    ImGui::EndDisabled();
            }

            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                app.showLanguageDialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}
