#pragma once

// ---- Settings panel -------------------------------------------------------
// Extracted from main.cpp — renders the Settings window including the
// game-path editor, ImGui folder picker, load status, console toggle,
// theme / font selectors, and the Save button.

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct AppSettings;
struct FontEntry;
struct GLFWwindow;
class  OrbitCamera;

namespace SettingsPanel
{

struct DrawContext
{
    // Game path
    char* pathBuf       = nullptr;
    int   pathBufSize   = 0;
    bool  isWoWLoaded   = false;
    bool  loadInProgress = false;
    std::atomic<float>* loadProgress = nullptr;

    // Folder picker state (owned by AppState)
    bool* showFolderPicker = nullptr;
    std::filesystem::path*              folderPickerCurrent  = nullptr;
    std::vector<std::filesystem::path>* folderPickerEntries  = nullptr;
    bool* folderPickerNeedsRefresh = nullptr;

    // Settings / appearance
    AppSettings*          settings       = nullptr;
    std::vector<FontEntry>* availableFonts = nullptr;
    bool*    fontsDirty     = nullptr;
    GLFWwindow* window     = nullptr;
    bool*    showDemoWindow = nullptr;

    // Debug info
    OrbitCamera* camera = nullptr;

    // Callbacks
    std::function<std::string()> getLoadStatus;
};

void draw(DrawContext& ctx);

} // namespace SettingsPanel
