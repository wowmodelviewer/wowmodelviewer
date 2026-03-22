#pragma once

// ---- Consolidated application state header --------------------------------
// Decomposed into sub-structs following the subsystem pattern from
// Game Engine Architecture (Gregory): each group owns a cohesive slice
// of the application's mutable state.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "AppSettings.h"
#include "OrbitCamera.h"
#include "Renderer.h"
#include "ViewportFBO.h"
#include "wow_enums.h"

// Panel type imports (lightweight value types only)
#include "AnimationPanel.h"
#include "CharacterViewerPanel.h"
#include "ViewportOptionsPanel.h"
#include "ItemSetsPanel.h"
#include "MountsPanel.h"

// Full definitions required for std::unique_ptr members
#include "Attachment.h"
#include "ExporterPlugin.h"
#include "ImporterPlugin.h"

// Forward declarations for pointer / reference members
class GameFile;
class WoWModel;

// Full definition required for std::vector<core::GameConfig>
#include "GameFolder.h"

// ---- Supporting types -----------------------------------------------------

/// @brief Font entry for the UI font selector.
struct FontEntry
{
    std::string name;  ///< Display name of the font.
    std::string path;  ///< Absolute path to the .ttf file.
};

// ---- Type aliases for brevity ---------------------------------------------

using SkinEntry          = AnimationPanel::SkinEntry;
using AnimEntry          = AnimationPanel::AnimEntry;
using CustomizationOption = CharacterViewerPanel::CustomizationOption;
using GeosetEntry        = ViewportOptionsPanel::GeosetEntry;
using GeosetGroupEntry   = ViewportOptionsPanel::GeosetGroupEntry;
using ParticleColorState = ViewportOptionsPanel::ParticleColorState;
using ItemSetEntry       = ItemSetsPanel::ItemSetEntry;
using StartOutfitEntry   = ItemSetsPanel::StartOutfitEntry;
using MountEntry         = MountsPanel::MountEntry;

// ---- Sub-struct: Scene (3D scene graph, camera, timing, model flags) ------

/// @brief Mutable state for the 3D scene: renderer, camera, model tree, and timing.
struct SceneState
{
    Renderer renderer;
    OrbitCamera camera;
    std::unique_ptr<Attachment> root;
    WoWModel* selModel = nullptr;
    ViewportFBO fbo;

    float animTime = 0.0f;
    std::chrono::steady_clock::time_point lastTick;
    float fps = 0.0f;
    int fpsFrameCount = 0;
    float fpsAccum = 0.0f;

    bool isModel = false;
    bool isChar = false;
    bool isMounted = false;
};

// ---- Sub-struct: Loading (async game-data loading thread) -----------------

/// @brief Mutable state for the async game-data loading thread.
struct LoadingState
{
    bool isWoWLoaded = false;
    std::atomic<bool> initDB{false};
    std::string loadStatus;
    std::atomic<float> loadProgress{0.0f};
    bool loadInProgress = false;
    std::thread loadThread;
    std::mutex loadStatusMutex;
    std::atomic<bool> loadThreadDone{false};
    std::atomic<bool> loadThreadSuccess{false};
    std::string pathBuf;
    bool showConfigPopup = false;
    std::vector<core::GameConfig> pendingConfigs;
    int selectedConfig = 0;
};

// ---- Sub-struct: UI (visibility toggles, fonts, log, folder picker) -------

/// @brief Mutable state for the UI layer: dialogs, fonts, log viewer, folder picker.
struct UIState
{
    // Dialog visibility
    bool showAboutDialog = false;
    bool showLanguageDialog = false;

    // Panel open/close state (persisted via ImGui settings handler).
    // Panels not in the map default to open.
    std::unordered_map<std::string, bool> panelOpen;

    /// Returns a reference to the open flag for a named panel.
    /// Missing entries are inserted as true (open by default).
    bool& panel(const char* name)
    {
        auto [it, _] = panelOpen.try_emplace(name, true);
        return it->second;
    }

    // Transient dialog visibility (not persisted)
    bool showImportDialog = false;
    bool showFolderPicker = false;

    // Import dialog UI state
    bool importPopupJustOpened = false;

    // Font system
    std::vector<FontEntry> availableFonts;
    bool fontsDirty = false;
    float dpiScale = 1.0f;

    // Folder picker
    std::filesystem::path folderPickerCurrent;
    std::vector<std::filesystem::path> folderPickerEntries;
    bool folderPickerNeedsRefresh = true;

    // Log viewer
    std::vector<std::string> logLines;
    bool logAutoScroll = true;
    bool logNeedsReload = true;
};

// ---- Sub-struct: Animation (playback, skins) ------------------------------

/// @brief Mutable state for animation playback and skin selection.
struct AnimationState
{
    std::vector<AnimEntry> animEntries;
    int selectedAnimCombo = 0;
    float animSpeed = 1.0f;
    bool autoAnimate = true;
    int selectedSecondaryAnim = -1;
    int selectedMouthAnim = -1;
    float mouthSpeed = 1.0f;
    bool lockAnims = true;
    int loopCount = 0;
    std::vector<SkinEntry> skinEntries;
    int selectedSkin = -1;
    int blpSkin[3] = {-1, -1, -1};
};

// ---- Sub-struct: Character (customization + equipment) --------------------

/// @brief Mutable state for character customisation and equipment editing.
struct CharacterState
{
    std::vector<CustomizationOption> customizationOptions;
    std::string equipSearchBuf;
    int equipSlotToEdit = -1;
    bool equipPopupJustOpened = false;
    std::vector<size_t> equipFilteredItems;
    int equipSlotLevels[NUM_CHAR_SLOTS] = {};
};

// ---- Sub-struct: Browsers (NPC, Item, Mount, ItemSet, StartOutfit) --------

/// @brief Mutable state for the NPC, Item, Mount, ItemSet, and StartOutfit browsers.
struct BrowserState
{
    // Item Sets
    std::vector<ItemSetEntry> itemSets;
    bool itemSetsBuilt = false;
    std::string itemSetSearchBuf;
    std::vector<size_t> itemSetFiltered;
    bool itemSetFilterDirty = true;

    // Start Outfits
    std::vector<StartOutfitEntry> startOutfits;
    bool startOutfitsBuilt = false;
    std::string startOutfitSearchBuf;
    std::vector<size_t> startOutfitFiltered;
    bool startOutfitFilterDirty = true;

    // NPC Browser
    std::string npcSearchBuf;
    std::vector<size_t> npcFiltered;
    bool npcFilterDirty = true;

    // Item Browser
    std::string itemBrowseSearchBuf;
    std::vector<size_t> itemBrowseFiltered;
    bool itemBrowseFilterDirty = true;

    // Mounts
    std::vector<MountEntry> mountList;
    std::vector<GameFile*> creatureModels;
    std::vector<std::string> creatureModelNames;
    bool mountListBuilt = false;
    std::string mountSearchBuf;
    int mountTab = 0;
    std::vector<size_t> mountFiltered;
    bool mountFilterDirty = true;

    // Model control (geosets, particle color)
    std::vector<GeosetGroupEntry> geosetGroups;
    ParticleColorState pcrState;
};

// ---- Sub-struct: Export (export, import, screenshot, presets) --------------

/// @brief Mutable state for export, import, screenshot, and preset operations.
struct ExportState
{
    std::vector<std::unique_ptr<ExporterPlugin>> exporters;
    int selectedExporter = 0;
    std::string exportPath = "export";
    std::string exportStatus;
    std::vector<char> exportAnimChecked;

    // Screenshot
    std::string screenshotPath = "screenshot.png";
    std::string screenshotStatus;
    bool useCanvasOverride = false;
    int canvasWidth = 1920;
    int canvasHeight = 1080;

    // Presets
    std::string presetPath = "userSettings/preset.ini";
    std::string presetStatus;

    // URL Import
    std::vector<std::unique_ptr<ImporterPlugin>> importers;
    std::string importUrlBuf;
    std::string importStatus;
};

// ---- Consolidated application state ---------------------------------------

/// @brief Top-level aggregate of all mutable application state.
///
/// Sub-structs follow the Gregory "subsystem state" pattern: each group
/// owns a cohesive slice of the application's data.
struct AppState
{
    SceneState     scene;
    LoadingState   loading;
    UIState        ui;
    AnimationState anim;
    CharacterState character;
    BrowserState   browsers;
    ExportState    exporting;
    AppSettings    settings;
};
