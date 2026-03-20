#pragma once

#include <string>
#include <glm/glm.hpp>

/// Persistent application settings backed by userSettings/Config.ini.
struct AppSettings
{
    // ---- Paths ----
    std::string gamePath;                       // WoW Data folder

    // ---- Game loading ----
    bool enableDbCache = false;
    bool showConsole   = false;

    // ---- Viewport ----
    bool      drawGrid = true;
    glm::vec3 bgColor{0.22f, 0.22f, 0.22f};

    // ---- Font ----
    int   currentFont = 0;
    float fontSize    = 18.0f;

    // ---- Fixed paths ----
    static constexpr const char* configPath   = "userSettings/Config.ini";
    static constexpr const char* imguiIniPath = "userSettings/imgui_layout.ini";

    /// Load from Config.ini (also restores the current ThemeManager theme).
    void load();

    /// Persist to Config.ini and save the ImGui layout to disk.
    void save() const;
};
