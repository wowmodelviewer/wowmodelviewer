#pragma once

// ---- Consolidated application state header --------------------------------
// Extracted from main.cpp so that helper modules (GameLoader, ModelLoader,
// PresetManager, URLImportHandler) can accept AppState& without circular deps.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AppSettings.h"
#include "OrbitCamera.h"
#include "ViewportFBO.h"
#include "wow_enums.h"

// Panel type imports (lightweight value types only)
#include "AnimationPanel.h"
#include "CharacterViewerPanel.h"
#include "ViewportOptionsPanel.h"
#include "ItemSetsPanel.h"
#include "MountsPanel.h"

// Forward declarations for pointer / reference members
class Attachment;
class ExporterPlugin;
class GameFile;
class ImporterPlugin;
class WoWModel;
struct GLFWwindow;

// Full definition required for std::vector<core::GameConfig>
#include "GameFolder.h"

// ---- Supporting types -----------------------------------------------------

struct FontEntry
{
    std::string name;
    std::string path;  // absolute path to .ttf
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

// ---- Consolidated application state ---------------------------------------

struct AppState
{
    // Scene core
    OrbitCamera camera;
    std::unique_ptr<Attachment> root;
    WoWModel* selModel = nullptr;
    ViewportFBO fbo;
    AppSettings settings;
    GLFWwindow* window = nullptr;

    // Timing / FPS
    float animTime = 0.0f;
    std::chrono::steady_clock::time_point lastTick;
    float fps = 0.0f;
    int fpsFrameCount = 0;
    float fpsAccum = 0.0f;

    // Game loading
    bool isWoWLoaded = false;
    bool initDB = false;
    std::string loadStatus;
    std::atomic<float> loadProgress{0.0f};
    bool loadInProgress = false;
    std::thread loadThread;
    std::mutex loadStatusMutex;
    std::atomic<bool> loadThreadDone{false};
    std::atomic<bool> loadThreadSuccess{false};
    char pathBuf[1024] = {};
    bool showConfigPopup = false;
    std::vector<core::GameConfig> pendingConfigs;
    int selectedConfig = 0;

    // Dialog visibility
    bool showAboutDialog = false;
    bool showLanguageDialog = false;

    // Panel visibility (View menu toggles)
    bool showCharViewer = true;
    bool showViewport = true;
    bool showFileBrowser = true;
    bool showAnimation = true;
    bool showViewportOpts = true;
    bool showNpcBrowser = true;
    bool showItemBrowser = true;
    bool showExport = true;
    bool showScreenshot = true;
    bool showPresets = true;
    bool showLog = true;
    bool showSettings = false;
    bool showMounts = false;
    bool showItemSets = false;

    // Font system
    std::vector<FontEntry> availableFonts;
    bool fontsDirty = false;
    float dpiScale = 1.0f;

    // URL Import
    std::vector<std::unique_ptr<ImporterPlugin>> importers;
    bool showImportDialog = false;
    char importUrlBuf[1024] = {};
    std::string importStatus;
    bool importPopupJustOpened = false;

    // Folder Picker (ImGui-based)
    bool showFolderPicker = false;
    std::filesystem::path folderPickerCurrent;
    std::vector<std::filesystem::path> folderPickerEntries;
    bool folderPickerNeedsRefresh = true;

    // Model flags
    bool isModel = false;
    bool isChar = false;

    // Animation control
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

    // Character customization
    std::vector<CustomizationOption> customizationOptions;

    // Equipment popup
    char equipSearchBuf[256] = {};
    int equipSlotToEdit = -1;
    bool equipPopupJustOpened = false;
    std::vector<size_t> equipFilteredItems;
    int equipSlotLevels[NUM_CHAR_SLOTS] = {};

    // Item Sets
    std::vector<ItemSetEntry> itemSets;
    bool itemSetsBuilt = false;
    char itemSetSearchBuf[256] = {};
    std::vector<size_t> itemSetFiltered;
    bool itemSetFilterDirty = true;

    // Start Outfits
    std::vector<StartOutfitEntry> startOutfits;
    bool startOutfitsBuilt = false;
    char startOutfitSearchBuf[256] = {};
    std::vector<size_t> startOutfitFiltered;
    bool startOutfitFilterDirty = true;

    // Model control
    std::vector<GeosetGroupEntry> geosetGroups;
    ParticleColorState pcrState;

    // Screenshot
    char screenshotPath[512] = "screenshot.png";
    std::string screenshotStatus;
    bool useCanvasOverride = false;
    int canvasWidth = 1920;
    int canvasHeight = 1080;

    // Presets
    char presetPath[512] = "userSettings/preset.ini";
    std::string presetStatus;

    // NPC Browser
    char npcSearchBuf[256] = {};
    std::vector<size_t> npcFiltered;
    bool npcFilterDirty = true;

    // Item Browser
    char itemBrowseSearchBuf[256] = {};
    std::vector<size_t> itemBrowseFiltered;
    bool itemBrowseFilterDirty = true;

    // Export
    std::vector<std::unique_ptr<ExporterPlugin>> exporters;
    int selectedExporter = 0;
    char exportPath[512] = "export";
    std::string exportStatus;
    std::vector<char> exportAnimChecked;

    // Mounts
    std::vector<MountEntry> mountList;
    std::vector<GameFile*> creatureModels;
    std::vector<std::string> creatureModelNames;
    bool mountListBuilt = false;
    bool isMounted = false;
    char mountSearchBuf[256] = {};
    int mountTab = 0;
    std::vector<size_t> mountFiltered;
    bool mountFilterDirty = true;

    // Log viewer
    std::vector<std::string> logLines;
    bool logAutoScroll = true;
    bool logNeedsReload = true;
};
