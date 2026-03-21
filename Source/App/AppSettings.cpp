#ifdef _WIN32
#include <windows.h>
#endif

#include "AppSettings.h"
#include "ThemeManager.h"
#include "IniFile.h"
#include "Logger.h"
#include "imgui.h"

void AppSettings::load()
{
    const core::IniFile config(configPath);

    gamePath      = config.getString("Settings/Path");
    enableDbCache = config.getBool("Settings/EnableDbCache", false);
    showConsole   = config.getBool("Settings/ShowConsole", false);
    drawGrid      = config.getBool("Viewport/DrawGrid", true);
    bgColor.x     = static_cast<float>(config.getDouble("Viewport/BgR", 71.0 / 255.0));
    bgColor.y     = static_cast<float>(config.getDouble("Viewport/BgG", 95.0 / 255.0));
    bgColor.z     = static_cast<float>(config.getDouble("Viewport/BgB", 121.0 / 255.0));

    ThemeManager::currentThemeRef() = config.getInt("Settings/Theme",
                                                     static_cast<int>(ThemeManager::UE5));
    if (ThemeManager::currentThemeRef() < 0 ||
        ThemeManager::currentThemeRef() >= ThemeManager::themeCount())
        ThemeManager::currentThemeRef() = static_cast<int>(ThemeManager::UE5);

    currentFont = config.getInt("Settings/Font", 0);
    fontSize    = static_cast<float>(config.getDouble("Settings/FontSize", 18.0));
    if (fontSize < 10.0f) fontSize = 10.0f;
    if (fontSize > 40.0f) fontSize = 40.0f;

    LOG_INFO << "Settings loaded. Game path:" << gamePath;
}

void AppSettings::save() const
{
    core::IniFile config(configPath);
    config.setValue("Settings/Path", gamePath);
    config.setValue("Settings/EnableDbCache", enableDbCache);
    config.setValue("Settings/ShowConsole", showConsole);
    config.setValue("Viewport/DrawGrid", drawGrid);
    config.setValue("Viewport/BgR", static_cast<double>(bgColor.x));
    config.setValue("Viewport/BgG", static_cast<double>(bgColor.y));
    config.setValue("Viewport/BgB", static_cast<double>(bgColor.z));
    config.setValue("Settings/Theme", ThemeManager::currentThemeRef());
    config.setValue("Settings/Font", currentFont);
    config.setValue("Settings/FontSize", static_cast<double>(fontSize));
    config.sync();

    ImGui::SaveIniSettingsToDisk(imguiIniPath);
    LOG_INFO << "Settings and UI layout saved.";
}
