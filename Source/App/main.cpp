// ============================================================================
// WoW Model Viewer � ImGui / GLFW entry point
//
// Initialises engine systems (GlobalSettings, Logger, video), creates an
// offscreen FBO for the 3-D viewport, renders the scene into that FBO, and
// displays it as an ImGui::Image() inside a dockable "3D Viewport" panel.
// OrbitCamera input is wired to ImGui's mouse/keyboard state.
//
// Game loading (CascLib + GameDatabase) � reads Config.ini,
// opens the WoW game folder via an ImGui path dialog, initialises the
// CASC storage, loads the listfile, and builds the in-memory database.
//
// File Browser � filterable tree view of CASC files with configurable
// extension filter and text search. Clicking a .m2 file loads it.
//
// Model loading � creates WoWModel from GameFile, sets up character
// equipment slots, and resets the camera to frame the model.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

// Engine (no wxWidgets dependencies)
#include "GlobalSettings.h"
#include "Logger.h"
#include "LogOutputFile.h"
#include "LogOutputConsole.h"
#include "video.h"
#include "Attachment.h"
#include "WoWModel.h"
#include "OrbitCamera.h"
#include "ViewportFBO.h"
#include "ThemeManager.h"
#include "AppSettings.h"
#include "SceneRenderer.h"
#include "FileBrowserPanel.h"
#include "CharacterViewerPanel.h"
#include "AnimationPanel.h"
#include "ViewportOptionsPanel.h"
#include "ExportPanel.h"
#include "ScreenshotPanel.h"

// Game loading
#include "Game.h"
#include "WoWFolder.h"
#include "WoWDatabase.h"
#include "IniFile.h"
#include "HttpClient.h"
#include "CharTexture.h"
#include "RaceInfos.h"
#include "database.h"
#include "string_utils.h"
#include "TextureManager.h"
#include "ZipExtract.h"
#include "WoWItem.h"
#include "CharDetails.h"
#include "DB2Table.h"

#include "stb_image.h"
#include "stb_image_write.h"

// Exporters (OBJ / FBX)
#include "OBJExporter.h"
#include "FBXExporter.h"

// Importers (Armory / Wowhead URL import)
#include "ArmoryImporter.h"
#include "WowheadImporter.h"
#include "CharInfos.h"
#include "NPCInfos.h"

#include <format>
#include <deque>
#include <map>
#include <set>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---- Supporting types -----------------------------------------------------
struct FontEntry {
    std::string name;
    std::string path;  // absolute path to .ttf
};

using SkinEntry = AnimationPanel::SkinEntry;

struct ItemSetEntry
{
    int id;
    std::string name;
};

struct StartOutfitEntry
{
    int id;           // CharStartOutfit.ID
    std::string name; // class name
};

using GeosetEntry      = ViewportOptionsPanel::GeosetEntry;
using GeosetGroupEntry = ViewportOptionsPanel::GeosetGroupEntry;
using ParticleColorState = ViewportOptionsPanel::ParticleColorState;

struct MountEntry
{
    int         displayID;  // CreatureDisplayInfoID (>0 for DB mounts, -1 for "None")
    std::string name;
};

using AnimEntry = AnimationPanel::AnimEntry;
using CustomizationOption = CharacterViewerPanel::CustomizationOption;

// ---- Consolidated application state ---------------------------------------
struct AppState
{
    // Scene core
    OrbitCamera camera;
    Attachment* root = nullptr;
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
    std::vector<ImporterPlugin*> importers;
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
    std::vector<ExporterPlugin*> exporters;
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

static AppState app;

// ---- Helpers --------------------------------------------------------------
static void reloadLogFile()
{
    app.logLines.clear();
    std::ifstream file("userSettings/log_imgui.txt");
    if (!file.is_open())
        return;
    std::string line;
    while (std::getline(file, line))
        app.logLines.push_back(line);
    app.logNeedsReload = false;
}

static std::filesystem::path getApplicationDirPath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// ---- Folder Picker helpers ------------------------------------------------
static void folderPickerRefresh()
{
    app.folderPickerEntries.clear();
    namespace fs = std::filesystem;
    std::error_code ec;

    if (app.folderPickerCurrent.empty())
    {
        // List drive roots on Windows
#ifdef _WIN32
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (drives & (1u << i))
            {
                std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
                app.folderPickerEntries.push_back(fs::path(root));
            }
        }
#else
        app.folderPickerEntries.push_back(fs::path("/"));
#endif
    }
    else
    {
        for (auto& entry : fs::directory_iterator(app.folderPickerCurrent, fs::directory_options::skip_permission_denied, ec))
        {
            if (entry.is_directory(ec))
                app.folderPickerEntries.push_back(entry.path());
        }
        std::sort(app.folderPickerEntries.begin(), app.folderPickerEntries.end(),
            [](const fs::path& a, const fs::path& b)
            {
                return core::toLower(a.filename().string()) < core::toLower(b.filename().string());
            });
    }

    app.folderPickerNeedsRefresh = false;
}

static void openFolderPicker()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Start from the current path buffer if it's a valid directory
    fs::path startDir(app.pathBuf);
    if (fs::is_directory(startDir, ec))
        app.folderPickerCurrent = startDir;
    else if (startDir.has_parent_path() && fs::is_directory(startDir.parent_path(), ec))
        app.folderPickerCurrent = startDir.parent_path();
    else
        app.folderPickerCurrent.clear(); // show drive roots

    app.folderPickerNeedsRefresh = true;
    app.showFolderPicker = true;
}


// ---- Thread-safe load status helpers --------------------------------------
static void setLoadStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(app.loadStatusMutex);
    app.loadStatus = s;
}

static std::string getLoadStatus()
{
    std::lock_guard<std::mutex> lock(app.loadStatusMutex);
    return app.loadStatus;
}

// ---- Support-file download (listfile.csv, extraEncryptionKeys.csv) --------
static bool downloadFile(const std::string& url, const std::filesystem::path& dest,
                          const std::string& label, bool replaceSeparators = false)
{
    LOG_INFO << "Downloading " << label << "...";
    setLoadStatus("Downloading " + label + "...");

    const auto resp = HttpClient::Get(url);
    if (!resp.success)
    {
        LOG_ERROR << "Failed to download " << label << ": " << resp.error;
        setLoadStatus("Failed to download " + label + ": " + resp.error);
        return false;
    }

    std::string content = resp.body;
    if (replaceSeparators)
        std::replace(content.begin(), content.end(), ' ', ';');

    std::ofstream file(dest, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR << "Failed to write " << label << " to " << dest.string();
        setLoadStatus("Failed to write " + label);
        return false;
    }

    file.write(content.data(), content.size());
    file.close();
    LOG_INFO << label << " saved to " << dest.string();
    return true;
}

static bool checkAndDownloadSupportFiles()
{
    namespace fs = std::filesystem;

    const fs::path appDir = getApplicationDirPath();
    const fs::path listfilePath = appDir / "listfile.csv";
    const fs::path keysPath = appDir / "extraEncryptionKeys.csv";

    std::error_code ec;
    const bool listfileMissing = !fs::exists(listfilePath, ec) || fs::file_size(listfilePath, ec) == 0;
    const bool keysMissing = !fs::exists(keysPath, ec) || fs::file_size(keysPath, ec) == 0;

    if (listfileMissing)
    {
        if (!downloadFile("https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv",
                          listfilePath, "listfile.csv"))
            return false;
    }

    if (keysMissing)
    {
        if (!downloadFile("https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt",
                          keysPath, "extraEncryptionKeys.csv", true))
            return false;
    }

    // Ensure the dbdefs cache directory exists (DBDs are downloaded on demand)
    const fs::path dbdDir = appDir / "games" / "wow" / "dbdefs";
    fs::create_directories(dbdDir, ec);

    return true;
}

// ---- InitDatabase (ported from ModelViewer::InitDatabase) -----------------
static void initDatabase()
{
    LOG_INFO << "Initializing Databases...";
    setLoadStatus("Initializing database...");

    // --- Database caching via fast mode ---
    // When a valid cache file exists for the current game version, SQLite is
    // opened from disk and the expensive DB2-to-SQLite conversion is skipped.
    namespace fs = std::filesystem;
    const fs::path appDir      = getApplicationDirPath();
    const fs::path cachePath   = appDir / "wowdb.sqlite";
    const fs::path versionPath = appDir / "wowdb.version";

    const std::string currentVersion = GAMEDIRECTORY.version();
    bool cacheValid = false;

    const bool enableDbCache = app.settings.enableDbCache;

    std::error_code ec;
    if (enableDbCache &&
        fs::exists(cachePath, ec) && fs::file_size(cachePath, ec) > 0 &&
        fs::exists(versionPath, ec))
    {
        std::ifstream vf(versionPath);
        std::string cachedVersion;
        if (std::getline(vf, cachedVersion) && cachedVersion == currentVersion)
        {
            cacheValid = true;
            LOG_INFO << "Database cache is valid for version " << currentVersion;
        }
    }

    if (!cacheValid)
    {
        LOG_INFO << "Database cache miss � will rebuild from DB2 files.";
        fs::remove(cachePath, ec);
        fs::remove(versionPath, ec);
    }

    if (enableDbCache)
    {
        GAMEDATABASE.setCachePath(cachePath.string());
        GAMEDATABASE.setFastMode();
    }

    // DBD-based database initialization � fully on-demand like wow.export.
    // Tables are loaded lazily when first accessed via WOWDB.getTable().
    const fs::path dbdDir = appDir / "games" / "wow" / "dbdefs";

    // Configure on-demand DBD downloading from WoWDBDefs master branch
    GAMEDATABASE.setDbdBaseUrl(
        "https://raw.githubusercontent.com/wowdev/WoWDBDefs/refs/heads/master/definitions/%s.dbd");

    // Download and parse the DBD manifest for table name -> file data ID mapping
    GAMEDATABASE.setManifestUrl(
        "https://raw.githubusercontent.com/wowdev/WoWDBDefs/refs/heads/master/manifest.json");
    GAMEDATABASE.downloadAndParseManifest();

    LOG_INFO << "Attempting on-demand DBD-based database init from" << dbdDir.string();
    if (!GAMEDATABASE.initFromDBD(dbdDir.string(), currentVersion))
    {
        app.initDB = false;
        LOG_ERROR << "Database initialization failed!";
        setLoadStatus("Database initialization failed!");
        fs::remove(cachePath, ec);
        fs::remove(versionPath, ec);
        return;
    }

    // Write version marker so subsequent launches can reuse the cache.
    if (enableDbCache && !cacheValid)
    {
        std::ofstream vf(versionPath, std::ios::trunc);
        vf << currentVersion;
        LOG_INFO << "Database cache written for version " << currentVersion;
    }

    LOG_INFO << "Database initialization succeeded.";
    app.loadProgress = 0.60f;

    LOG_INFO << "initDatabase: CharTexture::initRegions...";
    CharTexture::initRegions();
    app.loadProgress = 0.65f;

    LOG_INFO << "initDatabase: RaceInfos::init...";
    RaceInfos::init();
    app.loadProgress = 0.70f;

    app.initDB = true;

    // TODO: Creature table in 12.0.x no longer has DisplayID � needs
    //       CreatureDisplayInfo join (like wow.export).  Disabled for now.
    LOG_INFO << "initDatabase: skipping Creature table (disabled).";

    app.loadProgress = 0.80f;

    // TODO: Item/ItemSparse loading disabled while focusing on base character
    //       customisation.  Re-enable once WDCReader string resolution is
    //       verified stable for large multi-section DB2 files.
    LOG_INFO << "initDatabase: skipping Item/ItemSparse loading (disabled).";

    app.loadProgress = 0.90f;
    LOG_INFO << "Finished initiating database files.";
}

// ---- loadWoW (ported from ModelViewer::LoadWoW) ---------------------------
// Called on the background thread to perform heavy CASC / listfile / DB work.
static void loadWoW(const core::GameConfig& config)
{
    app.loadProgress = 0.0f;
    setLoadStatus("Opening CASC storage...");

    if (!GAMEDIRECTORY.setConfig(config))
    {
        LOG_ERROR << "Could not load WoW Data folder (error "
                  << GAMEDIRECTORY.lastError() << ").";
        setLoadStatus("Failed to open CASC storage (error "
                       + std::to_string(GAMEDIRECTORY.lastError()) + ").");
        app.loadThreadDone = true;
        return;
    }

    LOG_INFO << "Major version: " << GAMEDIRECTORY.majorVersion();
    app.loadProgress = 0.05f;

    // Set the config folder used for CSV data files, listfile paths, etc.
    const std::string baseConfigFolder = "games/wow/";
    LOG_INFO << "Using config folder: " << baseConfigFolder;
    core::Game::instance().setConfigFolder(baseConfigFolder);

    // Load file list from listfile.csv
    setLoadStatus("Loading file list...");
    app.loadProgress = 0.10f;
    GAMEDIRECTORY.setProgressCallback([](int current, int total) {
        if (total > 0)
            app.loadProgress = 0.10f + 0.40f * static_cast<float>(current) / static_cast<float>(total);
    });
    GAMEDIRECTORY.initFromListfile("../../listfile.csv");
    GAMEDIRECTORY.setProgressCallback(nullptr);
    app.loadProgress = 0.50f;

    // Init database
    setLoadStatus("Initializing database...");
    app.loadProgress = 0.55f;
    initDatabase();

    if (!app.initDB)
    {
        app.loadThreadDone = true;
        return;
    }

    app.loadProgress = 1.0f;
    setLoadStatus("World of Warcraft loaded successfully.");
    app.loadThreadSuccess = true;
    app.loadThreadDone = true;
}

// ---- Async thread launcher / poller ---------------------------------------
static void loadWoWThreadFunc(core::GameConfig config)
{
    setLoadStatus("Checking support files...");
    if (!checkAndDownloadSupportFiles())
    {
        app.loadThreadDone = true;
        return;
    }
    loadWoW(config);
}

static void launchLoadThread(const core::GameConfig& config)
{
    app.loadProgress = 0.0f;
    app.loadThreadDone = false;
    app.loadThreadSuccess = false;
    app.loadInProgress = true;

    if (app.loadThread.joinable())
        app.loadThread.join();

    app.loadThread = std::thread(loadWoWThreadFunc, config);
}

static void pollAsyncLoad()
{
    if (!app.loadInProgress || !app.loadThreadDone.load())
        return;

    if (app.loadThread.joinable())
        app.loadThread.join();

    app.loadInProgress = false;

    if (app.loadThreadSuccess.load())
    {
        app.isWoWLoaded = true;
        FileBrowserPanel::markDirty();
        LOG_INFO << "World of Warcraft loaded successfully. Version: "
                 << GAMEDIRECTORY.version() << " Locale: " << GAMEDIRECTORY.locale();
        app.settings.save();
    }
}

// Called when the user clicks "Load WoW" � validates the path and launches
// the background loading thread (downloads, CASC, listfile, database).
static void beginLoadWoW()
{
    if (app.isWoWLoaded || app.loadInProgress)
        return;

    // Sync game path from the Settings panel input buffer
    app.settings.gamePath = app.pathBuf;

    app.loadInProgress = true;
    app.loadProgress = 0.0f;
    setLoadStatus("Validating game path...");

    // Validate game path
    namespace fs = std::filesystem;
    std::string path = app.settings.gamePath;
    if (path.empty() || !fs::is_directory(path))
    {
        setLoadStatus("Please set a valid WoW Data folder path in Options > Settings.");
        app.loadInProgress = false;
        return;
    }

    // Ensure path ends with separator
    if (path.back() != '/' && path.back() != '\\')
        path += '\\';

    // Ensure path points to the Data folder
    {
        std::string lower = core::toLower(path);
        if (lower.find("data\\") == std::string::npos && lower.find("data/") == std::string::npos)
            path += "Data\\";
    }
    app.settings.gamePath = path;

    // Init Game if needed
    if (!core::Game::instance().initDone())
        core::Game::instance().init(new wow::WoWFolder(app.settings.gamePath), new wow::WoWDatabase());

    // Check available configs
    app.pendingConfigs = GAMEDIRECTORY.configsFound();

    if (app.pendingConfigs.empty())
    {
        LOG_ERROR << "No locale found in WoW folder.";
        setLoadStatus("No locale found in the WoW folder.");
        app.loadInProgress = false;
        return;
    }

    if (app.pendingConfigs.size() == 1)
    {
        // Only one config � launch background loading thread
        launchLoadThread(app.pendingConfigs[0]);
    }
    else
    {
        // Multiple configs � show selection popup
        app.selectedConfig = 0;
        app.showConfigPopup = true;
        app.loadInProgress = false; // will resume after user picks
    }
}

// ---- Animation / skin helpers (Phase 4) -----------------------------------
static std::string wstringToString(const std::wstring& ws)
{
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s.push_back(static_cast<char>(c & 0x7F));
    return s;
}

static void applySkin(WoWModel* model, int skinIndex)
{
    if (!model || skinIndex < 0 || skinIndex >= static_cast<int>(app.skinEntries.size()))
        return;

    const auto& skin = app.skinEntries[skinIndex];
    model->setCreatureGeosetData(skin.creatureGeosetData);
    for (size_t i = 0; i < skin.count; ++i)
    {
        if (skin.tex[i])
            model->updateTextureList(skin.tex[i], skin.base + static_cast<int>(i));
    }
    app.selectedSkin = skinIndex;
}

static WoWModel* getLoadedModel()
{
    if (!app.root) return nullptr;
    auto* att = app.root->children.empty() ? nullptr : app.root->children[0];
    return att ? dynamic_cast<WoWModel*>(att->model()) : nullptr;
}

static void resetCameraToModel(OrbitCamera& camera, const WoWModel* model)
{
    if (!model)
    {
        camera.reset();
        return;
    }

    float zMin = 0.0f;
    float zMax = 0.0f;
    for (const auto& v : model->origVertices)
    {
        if (v.pos.z < zMin) zMin = v.pos.z;
        if (v.pos.z > zMax) zMax = v.pos.z;
    }
    camera.resetFromBounds(zMin, zMax, video.fov);
}

static void initAnimationControl(WoWModel* model)
{
    app.animEntries.clear();
    app.skinEntries.clear();
    app.selectedAnimCombo = 0;
    app.selectedSkin = -1;
    app.blpSkin[0] = app.blpSkin[1] = app.blpSkin[2] = -1;
    app.animSpeed = 1.0f;
    app.selectedSecondaryAnim = -1;
    app.selectedMouthAnim = -1;
    app.mouthSpeed = 1.0f;
    app.lockAnims = true;
    app.loopCount = 0;

    if (!model || !model->animated || model->anims.empty())
        return;

    // Build animation list
    auto animsMap = model->getAnimsMap();
    int standIndex = -1;

    for (size_t i = 0; i < model->anims.size(); ++i)
    {
        AnimEntry e;
        auto it = animsMap.find(model->anims[i].animID);
        if (it != animsMap.end())
            e.label = wstringToString(it->second) + " [" + std::to_string(i) + "]";
        else
            e.label = "Anim " + std::to_string(model->anims[i].animID) + " [" + std::to_string(i) + "]";
        e.animIndex = static_cast<int>(i);
        app.animEntries.push_back(e);

        if (model->anims[i].animID == 0 && standIndex < 0) // ANIM_STAND == 0
            standIndex = static_cast<int>(app.animEntries.size()) - 1;
    }

    if (standIndex >= 0)
        app.selectedAnimCombo = standIndex;

    int useAnim = (standIndex >= 0) ? app.animEntries[standIndex].animIndex : 0;
    model->currentAnim = useAnim;
    model->animManager->SetAnim(0, useAnim, 0);
    model->animManager->SetSpeed(1.0f);
    model->animManager->Play();

    // Build skin list for creatures / items
    const std::string fn = model->itemName();
    bool isCreature = (fn.size() >= 8 && (fn.substr(0, 8) == "creature" || fn.substr(0, 8) == "Creature"));
    bool isItem = (fn.size() >= 4 && (fn.substr(0, 4) == "item" || fn.substr(0, 4) == "Item"));

    if (isCreature)
    {
        // Find CreatureDisplayInfo rows whose CreatureModelData has matching FileDataID
        const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
        const auto* cmdTable = WOWDB.getTable("CreatureModelData");
        const auto* geoTable = WOWDB.getTable("CreatureDisplayInfoGeosetData");
        const int targetFDID = model->gamefile->fileDataId();

        if (cdiTable && cmdTable && geoTable)
        for (const auto& cdiRow : *cdiTable)
        {
            auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
            if (!cmdRow || static_cast<int>(cmdRow.getUInt("FileDataID")) != targetFDID)
                continue;

            SkinEntry se;
            size_t cnt = 0;
            uint32_t texFDIDs[3] = {
                cdiRow.getUInt("TextureVariationFileDataID1"),
                cdiRow.getUInt("TextureVariationFileDataID2"),
                cdiRow.getUInt("TextureVariationFileDataID3")
            };
            for (size_t s = 0; s < 3; ++s)
            {
                if (texFDIDs[s] != 0)
                {
                    se.tex[s] = GAMEDIRECTORY.getFile(texFDIDs[s]);
                    if (se.tex[s]) ++cnt;
                }
            }
            if (cnt == 0) continue;
            se.base = TEXTURE_GAMEOBJECT1;
            se.count = cnt;

            uint32_t cdi = cdiRow.recordID();
            for (const auto& geoRow : *geoTable)
            {
                if (geoRow.getUInt("CreatureDisplayInfoID") != cdi)
                    continue;
                int geoType = 100 * (static_cast<int>(geoRow.getUInt("GeosetIndex")) + 1);
                int geoId   = static_cast<int>(geoRow.getUInt("GeosetValue"));
                if (geoId > 0) se.creatureGeosetData.insert(geoType + geoId);
            }

            se.label = "Skin " + std::to_string(app.skinEntries.size());
            app.skinEntries.push_back(se);
        }
    }
    else if (isItem)
    {
        // Find ItemDisplayInfo rows whose ModelFileData has matching FileDataID
        const auto* idiTable = WOWDB.getTable("ItemDisplayInfo");
        const auto* texFDTable = WOWDB.getTable("TextureFileData");
        const auto* modFDTable = WOWDB.getTable("ModelFileData");
        const int targetFDID = model->gamefile->fileDataId();

        // Build a set of ModelResourcesIDs that match our file
        std::set<uint32_t> matchingModelResIDs;
        if (idiTable && texFDTable && modFDTable)
        {
        for (const auto& mfdRow : *modFDTable)
        {
            if (static_cast<int>(mfdRow.getUInt("FileDataID")) == targetFDID)
                matchingModelResIDs.insert(mfdRow.getUInt("ModelResourcesID"));
        }

        for (const auto& idiRow : *idiTable)
        {
            if (matchingModelResIDs.find(idiRow.getUInt("ModelResourcesID1")) == matchingModelResIDs.end())
                continue;

            uint32_t matResID = idiRow.getUInt("ModelMaterialResourcesID1");
            // Find TextureFileData for this material
            for (const auto& tfdRow : *texFDTable)
            {
                if (tfdRow.getUInt("MaterialResourcesID") != matResID)
                    continue;
                uint32_t fdid = tfdRow.getUInt("FileDataID");
                if (fdid == 0) continue;
                SkinEntry se;
                se.tex[0] = GAMEDIRECTORY.getFile(fdid);
                if (!se.tex[0]) continue;
                se.base = TEXTURE_OBJECT_SKIN;
                se.count = 1;
                se.label = "Skin " + std::to_string(app.skinEntries.size());
                app.skinEntries.push_back(se);
            }
        }
        } // if (idiTable && texFDTable && modFDTable)
    }

    if (!app.skinEntries.empty())
        applySkin(model, 0);
}

static void initCharacterControl(WoWModel* model)
{
    app.customizationOptions.clear();
    if (!model) return;

    auto& cd = model->cd;
    cd.showUnderwear = true;
    cd.showEars = true;
    cd.showHair = true;
    cd.showFacialHair = true;
    cd.showFeet = false;
    cd.autoHideGeosetsForHeadItems = true;
    cd.eyeGlowType = EGT_DEFAULT;
    model->bSheathe = false;
    cd.reset(model);

    const auto& infos = model->infos;
    if (infos.raceID == -1 || infos.ChrModelID.empty())
        return;

    // Gather ChrCustomizationOption rows for this ChrModelID, sorted by OrderIndex
    const auto* optTable = WOWDB.getTable("ChrCustomizationOption");
    const auto* choiceTable = WOWDB.getTable("ChrCustomizationChoice");
    if (!optTable || !choiceTable) return;
    const uint32_t targetModelID = static_cast<uint32_t>(infos.ChrModelID[0]);

    struct OptionEntry { uint32_t id; uint32_t orderIndex; };
    std::vector<OptionEntry> matchingOptions;
    for (const auto& row : *optTable)
    {
        if (row.getUInt("ChrModelID") == targetModelID && row.getUInt("ChrCustomizationID") != 0)
            matchingOptions.push_back({ row.recordID(), row.getUInt("OrderIndex") });
    }
    std::sort(matchingOptions.begin(), matchingOptions.end(),
        [](const OptionEntry& a, const OptionEntry& b) { return a.orderIndex < b.orderIndex; });

    for (const auto& optEntry : matchingOptions)
    {
        unsigned int optionID = optEntry.id;

        CustomizationOption opt;
        opt.optionID = optionID;

        // Get option name
        auto optRow = optTable->getRow(optionID);
        if (optRow)
        {
            std::string n = optRow.getString("Name_Lang");
            opt.name = n.empty() ? ("Option " + std::to_string(optionID)) : n;
        }
        else
        {
            opt.name = "Option " + std::to_string(optionID);
        }

        // Get available choices
        std::vector<unsigned int> choiceIDs = cd.getCustomizationChoices(optionID);
        if (choiceIDs.empty())
            continue;

        // Lookup choice names by ID
        std::map<unsigned int, std::string> idToName;
        for (unsigned int cid : choiceIDs)
        {
            auto cRow = choiceTable->getRow(cid);
            if (cRow)
            {
                std::string cname = cRow.getString("Name_Lang");
                idToName[cid] = cname.empty() ? ("Choice " + std::to_string(cid)) : cname;
            }
        }
        for (unsigned int cid : choiceIDs)
        {
            opt.choiceIDs.push_back(cid);
            auto it = idToName.find(cid);
            opt.choiceNames.push_back(it != idToName.end() ? it->second : ("Choice " + std::to_string(cid)));
        }

        // Determine current selection
        unsigned int cur = cd.get(optionID);
        opt.selectedIndex = 0;
        for (size_t c = 0; c < opt.choiceIDs.size(); ++c)
        {
            if (opt.choiceIDs[c] == cur)
            {
                opt.selectedIndex = static_cast<int>(c);
                break;
            }
        }

        app.customizationOptions.push_back(std::move(opt));
    }
}

static void initModelControl(WoWModel* model)
{
    app.geosetGroups.clear();
    app.pcrState = {};

    if (!model)
        return;

    // Build geoset groups
    std::map<size_t, size_t> meshToGroupIdx;
    for (size_t i = 0; i < model->geosets.size(); ++i)
    {
        size_t mesh = model->geosets[i]->id / 100;
        if (meshToGroupIdx.find(mesh) == meshToGroupIdx.end())
        {
            GeosetGroupEntry group;
            group.meshId = mesh;
            std::string groupName = WoWModel::getCGGroupName(static_cast<CharGeosets>(mesh));
            group.name = groupName.empty() ? std::to_string(mesh) : groupName;
            meshToGroupIdx[mesh] = app.geosetGroups.size();
            app.geosetGroups.push_back(std::move(group));
        }

        GeosetEntry ge;
        ge.index = i;
        ge.id = model->geosets[i]->id;
        ge.label = std::format("{} [{}, {}, {}]", i, mesh,
                               model->geosets[i]->id % 100, model->geosets[i]->id);
        app.geosetGroups[meshToGroupIdx[mesh]].geosets.push_back(ge);
    }

    // Detect available particle color replacement IDs
    for (uint pcid : model->replacableParticleColorIDs)
    {
        if (pcid == 11) app.pcrState.hasSet[0] = true;
        else if (pcid == 12) app.pcrState.hasSet[1] = true;
        else if (pcid == 13) app.pcrState.hasSet[2] = true;
    }
}

// ---- Equipment helpers (ported from charcontrol / util) -------------------
static bool correctType(int type, int slot)
{
    if (type == IT_ALL)
        return true;
    switch (slot)
    {
    case CS_HEAD:       return (type == IT_HEAD);
    case CS_SHOULDER:   return (type == IT_SHOULDER);
    case CS_SHIRT:      return (type == IT_SHIRT);
    case CS_CHEST:      return (type == IT_CHEST || type == IT_ROBE);
    case CS_BELT:       return (type == IT_BELT);
    case CS_PANTS:      return (type == IT_PANTS);
    case CS_BOOTS:      return (type == IT_BOOTS);
    case CS_BRACERS:    return (type == IT_BRACERS);
    case CS_GLOVES:     return (type == IT_GLOVES);
    case CS_HAND_RIGHT: return (type == IT_RIGHTHANDED || type == IT_GUN || type == IT_THROWN ||
                                type == IT_2HANDED || type == IT_DAGGER);
    case CS_HAND_LEFT:  return (type == IT_LEFTHANDED || type == IT_BOW || type == IT_SHIELD ||
                                type == IT_2HANDED || type == IT_DAGGER || type == IT_OFFHAND);
    case CS_CAPE:       return (type == IT_CAPE);
    case CS_TABARD:     return (type == IT_TABARD);
    case CS_QUIVER:     return (type == IT_QUIVER);
    default: return false;
    }
}

static ImVec4 getQualityColor(int quality)
{
    switch (quality)
    {
    case 0:  return ImVec4(0.616f, 0.616f, 0.616f, 1.0f); // Poor (gray)
    case 1:  return ImVec4(1.0f,   1.0f,   1.0f,   1.0f); // Common (white)
    case 2:  return ImVec4(0.118f, 1.0f,   0.0f,   1.0f); // Uncommon (green)
    case 3:  return ImVec4(0.0f,   0.439f, 0.867f, 1.0f); // Rare (blue)
    case 4:  return ImVec4(0.639f, 0.208f, 0.933f, 1.0f); // Epic (purple)
    case 5:  return ImVec4(1.0f,   0.502f, 0.0f,   1.0f); // Legendary (orange)
    case 6:
    case 7:  return ImVec4(0.898f, 0.8f,   0.502f, 1.0f); // Artifact/Heirloom (gold)
    default: return ImVec4(1.0f,   1.0f,   1.0f,   1.0f);
    }
}

static void rebuildEquipFilteredItems()
{
    app.equipFilteredItems.clear();
    if (app.equipSlotToEdit < 0)
        return;

    std::string search = core::toLower(std::string(app.equipSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0)
            continue;
        if (!correctType(item.type, app.equipSlotToEdit))
            continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        app.equipFilteredItems.push_back(i);
    }
}

// ---- Item Set helpers -----------------------------------------------------
static void buildItemSets()
{
    if (app.itemSetsBuilt)
        return;

    app.itemSets.clear();

    const auto* itemSetTable = WOWDB.getTable("ItemSet");
    if (!itemSetTable) return;
    for (const auto& row : *itemSetTable)
    {
        ItemSetEntry e;
        e.id = static_cast<int>(row.recordID());
        e.name = row.getString("Name_Lang");
        if (!e.name.empty())
            app.itemSets.push_back(e);
    }

    std::sort(app.itemSets.begin(), app.itemSets.end(),
        [](const ItemSetEntry& a, const ItemSetEntry& b) { return a.name < b.name; });

    app.itemSetsBuilt = true;
    app.itemSetFilterDirty = true;
    LOG_INFO << "Item sets loaded: " << app.itemSets.size();
}

static void rebuildItemSetFilter()
{
    app.itemSetFiltered.clear();

    std::string search = core::toLower(std::string(app.itemSetSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < app.itemSets.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(app.itemSets[i].name, search))
            continue;
        app.itemSetFiltered.push_back(i);
    }

    app.itemSetFilterDirty = false;
}

static void tryToEquipItem(WoWModel* model, int id)
{
    if (id == 0 || !model)
        return;

    ItemRecord itemr = items.getById(id);
    if (itemr.name == items.items[0].name)
    {
        LOG_ERROR << "Cannot retrieve item from database (id " << id << ")";
        return;
    }

    int itemSlot = itemr.slot();
    if (itemSlot == -1)
    {
        LOG_ERROR << "Cannot determine slot for object " << itemr.name;
        return;
    }

    WoWItem* item = model->getItem(static_cast<CharSlots>(itemSlot));
    if (item)
    {
        item->setId(id);
        app.equipSlotLevels[itemSlot] = 0;
    }
}

// ---- Start Outfit helpers -------------------------------------------------
static void buildStartOutfits(WoWModel* model)
{
    app.startOutfits.clear();
    app.startOutfitsBuilt = false;
    app.startOutfitFilterDirty = true;

    if (!model) return;

    const auto& infos = model->infos;
    if (infos.raceID == -1)
        return;

    const auto* csoTable = WOWDB.getTable("CharStartOutfit");
    const auto* chrClassTable = WOWDB.getTable("ChrClasses");
    if (!csoTable || !chrClassTable) return;
    const uint32_t targetRace = static_cast<uint32_t>(infos.raceID);
    const uint32_t targetSex = static_cast<uint32_t>(infos.sexID);

    for (const auto& row : *csoTable)
    {
        if (row.getUInt("raceID") != targetRace || row.getUInt("sexID") != targetSex)
            continue;

        auto classRow = chrClassTable->getRow(row.getUInt("classID"));
        std::string className = classRow ? classRow.getString("Filename") : "";

        StartOutfitEntry e;
        e.name = className;
        e.id = static_cast<int>(row.recordID());
        if (!e.name.empty() && e.id > 0)
            app.startOutfits.push_back(e);
    }

    std::sort(app.startOutfits.begin(), app.startOutfits.end(),
        [](const StartOutfitEntry& a, const StartOutfitEntry& b) { return a.name < b.name; });

    app.startOutfitsBuilt = true;
    app.startOutfitFilterDirty = true;
    LOG_INFO << "Start outfits loaded: " << app.startOutfits.size();
}

static void rebuildStartOutfitFilter()
{
    app.startOutfitFiltered.clear();

    std::string search = core::toLower(std::string(app.startOutfitSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < app.startOutfits.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(app.startOutfits[i].name, search))
            continue;
        app.startOutfitFiltered.push_back(i);
    }

    app.startOutfitFilterDirty = false;
}

static void applyStartOutfit(WoWModel* model, int outfitId)
{
    if (!model || outfitId <= 0)
        return;

    const auto* csoTable = WOWDB.getTable("CharStartOutfit");
    if (!csoTable) return;
    auto csoRow = csoTable->getRow(static_cast<uint32_t>(outfitId));
    if (!csoRow)
    {
        LOG_ERROR << "Start outfit query failed for ID " << outfitId;
        return;
    }

    // Reset all equipped items
    for (const auto it : *model)
        it->setId(0);
    std::memset(app.equipSlotLevels, 0, sizeof(app.equipSlotLevels));

    for (unsigned i = 0; i < 24; ++i)
    {
        std::string fieldName = "iitem" + std::to_string(i + 1);
        uint32_t itemID = csoRow.getUInt(fieldName);
        if (itemID == 0)
            continue;
        try
        {
            tryToEquipItem(model, static_cast<int>(itemID));
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR << "Failed to equip start outfit entry " << i
                      << " (id=" << itemID << "): " << ex.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied start outfit ID " << outfitId;
}

static void applyItemSet(WoWModel* model, int setId)
{
    if (!model || setId <= 0)
        return;

    const auto* itemSetTable = WOWDB.getTable("ItemSet");
    if (!itemSetTable) return;
    auto setRow = itemSetTable->getRow(static_cast<uint32_t>(setId));
    if (!setRow)
    {
        LOG_ERROR << "Item set query failed for ID " << setId;
        return;
    }

    // Reset all equipped items
    for (const auto it : *model)
        it->setId(0);
    std::memset(app.equipSlotLevels, 0, sizeof(app.equipSlotLevels));

    // Equip each item from the set.  WoWItem::setId() may throw
    // std::invalid_argument when downstream DB queries return empty strings
    // for items that lack appearance data � catch per-item to keep going.
    for (unsigned i = 0; i < 8; ++i)
    {
        std::string fieldName = "ItemID" + std::to_string(i + 1);
        uint32_t itemID = setRow.getUInt(fieldName);
        if (itemID == 0)
            continue;
        try
        {
            tryToEquipItem(model, static_cast<int>(itemID));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Failed to equip item set entry " << i
                      << " (id=" << itemID << "): " << e.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied item set ID " << setId;
}

// ---- Clear current model --------------------------------------------------
static void clearModel()
{
    if (app.root)
    {
        app.root->delChildren();
        app.root->setModel(nullptr);
    }

    TEXTUREMANAGER.clear();
    app.isModel = false;
    app.isChar = false;

    app.selModel = nullptr;
    app.animEntries.clear();
    app.skinEntries.clear();
    app.customizationOptions.clear();
    app.geosetGroups.clear();
    app.pcrState = {};
    app.selectedAnimCombo = 0;
    app.selectedSkin = -1;
    app.animSpeed = 1.0f;
    app.autoAnimate = true;
    app.equipSlotToEdit = -1;
    app.equipFilteredItems.clear();
    app.equipSearchBuf[0] = '\0';
    std::memset(app.equipSlotLevels, 0, sizeof(app.equipSlotLevels));
    app.exportAnimChecked.clear();
    app.exportStatus.clear();
    app.isMounted = false;
    app.startOutfits.clear();
    app.startOutfitsBuilt = false;
    app.startOutfitSearchBuf[0] = '\0';
    app.startOutfitFiltered.clear();
    app.startOutfitFilterDirty = true;
}

// ---- Load a model from GameFile (ported from ModelViewer::LoadModel) -------
static void loadModel(GameFile* file)
{
    if (!file || !app.root)
        return;

    LOG_INFO << "Loading model: " << file->fullname();

    clearModel();

    auto* model = new WoWModel(file, true);
    if (!model->ok)
    {
        LOG_ERROR << "Model is not OK: " << file->fullname();
        delete model;
        return;
    }

    app.root->addChild(model, 0, -1);

    // Determine if this is a character model
    const std::string fn = file->fullname();
    app.isChar = (core::startsWithIgnoreCase(fn, "char") ||
                core::startsWithIgnoreCase(fn, "alternate/char") ||
                core::startsWithIgnoreCase(fn, "alternate\\char"));

    if (app.isChar)
    {
        model->addChild(new WoWItem(CS_SHIRT));
        model->addChild(new WoWItem(CS_HEAD));
        model->addChild(new WoWItem(CS_SHOULDER));
        model->addChild(new WoWItem(CS_PANTS));
        model->addChild(new WoWItem(CS_BOOTS));
        model->addChild(new WoWItem(CS_CHEST));
        model->addChild(new WoWItem(CS_TABARD));
        model->addChild(new WoWItem(CS_BELT));
        model->addChild(new WoWItem(CS_BRACERS));
        model->addChild(new WoWItem(CS_GLOVES));
        model->addChild(new WoWItem(CS_HAND_RIGHT));
        model->addChild(new WoWItem(CS_HAND_LEFT));
        model->addChild(new WoWItem(CS_CAPE));
        model->addChild(new WoWItem(CS_QUIVER));
        model->modelType = MT_CHAR;
        model->charModelDetails.isChar = true;
    }
    else
    {
        model->addChild(new WoWItem(CS_HAND_RIGHT));
        model->addChild(new WoWItem(CS_HAND_LEFT));
        model->modelType = MT_NORMAL;
    }

    app.isModel = true;

    app.selModel = model;
    initAnimationControl(model);
    initModelControl(model);
    if (app.isChar)
        initCharacterControl(model);

    // Reset camera to frame the model
    resetCameraToModel(app.camera, model);

    LOG_INFO << "Model loaded: " << model->name();
}

// ---- Save/Load Character Preset
static void saveCharacterPreset(const char* path)
{
    WoWModel* model = getLoadedModel();
    if (!model || !app.isChar)
    {
        app.presetStatus = "No character model loaded.";
        return;
    }

    std::string pathStr{path};
    core::IniFile ini{pathStr};

    // Display options
    const auto& cd = model->cd;
    ini.setValue("Display/ShowUnderwear", cd.showUnderwear);
    ini.setValue("Display/ShowHair", cd.showHair);
    ini.setValue("Display/ShowFacialHair", cd.showFacialHair);
    ini.setValue("Display/ShowEars", cd.showEars);
    ini.setValue("Display/ShowFeet", cd.showFeet);
    ini.setValue("Display/AutoHideGeosets", cd.autoHideGeosetsForHeadItems);
    ini.setValue("Display/Sheathe", model->bSheathe);
    ini.setValue("Display/EyeGlow", static_cast<int>(cd.eyeGlowType));

    // Customization choices
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

    // Equipment
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

static void loadCharacterPreset(const char* path)
{
    WoWModel* model = getLoadedModel();
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

    // Display options
    auto& cd = model->cd;
    cd.showUnderwear = ini.getBool("Display/ShowUnderwear", true);
    cd.showHair = ini.getBool("Display/ShowHair", true);
    cd.showFacialHair = ini.getBool("Display/ShowFacialHair", true);
    cd.showEars = ini.getBool("Display/ShowEars", true);
    cd.showFeet = ini.getBool("Display/ShowFeet", false);
    cd.autoHideGeosetsForHeadItems = ini.getBool("Display/AutoHideGeosets", true);
    model->bSheathe = ini.getBool("Display/Sheathe", false);
    cd.eyeGlowType = static_cast<EyeGlowTypes>(ini.getInt("Display/EyeGlow", EGT_DEFAULT));

    // Customization choices
    int optCount = ini.getInt("Customization/Count", 0);
    for (int i = 0; i < optCount; ++i)
    {
        std::string key = "Customization/" + std::to_string(i);
        unsigned int optionID = static_cast<unsigned int>(ini.getInt(key + "_OptionID", 0));
        unsigned int choiceID = static_cast<unsigned int>(ini.getInt(key + "_ChoiceID", 0));
        if (optionID == 0) continue;

        cd.set(optionID, choiceID);

        // Update the UI state
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

    // Equipment
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

// ---- NPC Browser helpers --------------------------------------------------
static void rebuildNpcFilter()
{
    app.npcFiltered.clear();

    std::string search = core::toLower(std::string(app.npcSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& npc = npcs[i];
        if (npc.model == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(npc.name, search))
            continue;
        app.npcFiltered.push_back(i);
    }

    app.npcFilterDirty = false;
}

static void loadNPC(unsigned int creatureID)
{
    // Creature ? CreatureDisplayInfo ? CreatureModelData
    const auto* creatureTable = WOWDB.getTable("Creature");
    if (!creatureTable) return;
    auto creatureRow = creatureTable->getRow(creatureID);
    if (!creatureRow)
    {
        LOG_ERROR << "NPC query failed for ID " << creatureID;
        return;
    }

    uint32_t displayInfoID = creatureRow.getUInt("DisplayID1");
    const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
    if (!cdiTable) return;
    auto cdiRow = cdiTable->getRow(displayInfoID);
    if (!cdiRow)
    {
        LOG_ERROR << "NPC query failed for ID " << creatureID << " (no CreatureDisplayInfo for DisplayID1=" << displayInfoID << ")";
        return;
    }

    const auto* cmdTable = WOWDB.getTable("CreatureModelData");
    auto cmdRow = cmdTable ? cmdTable->getRow(cdiRow.getUInt("ModelID")) : DB2Row();
    uint32_t fileDataID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
    uint32_t extraId = cdiRow.getUInt("ExtendedDisplayInfoID");

    if (extraId == 0)
    {
        // Simple NPC � load model directly
        GameFile* file = GAMEDIRECTORY.getFile(fileDataID);
        if (!file) return;
        loadModel(file);

        // Apply skin by display ID
        WoWModel* m = getLoadedModel();
        if (m)
        {
            uint32_t texFDIDs[3] = {
                cdiRow.getUInt("TextureVariationFileDataID1"),
                cdiRow.getUInt("TextureVariationFileDataID2"),
                cdiRow.getUInt("TextureVariationFileDataID3")
            };
            for (size_t i = 0; i < app.skinEntries.size(); ++i)
            {
                bool match = true;
                for (size_t t = 0; t < 3 && match; ++t)
                {
                    if (app.skinEntries[i].tex[t])
                    {
                        int fdid = app.skinEntries[i].tex[t]->fileDataId();
                        if (texFDIDs[t] != 0)
                            match = (fdid == static_cast<int>(texFDIDs[t]));
                        else
                            match = false;
                    }
                }
                if (match)
                {
                    applySkin(m, static_cast<int>(i));
                    break;
                }
            }
        }
    }
    else
    {
        // Character-type NPC
        GameFile* file = GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(static_cast<int>(fileDataID)));
        if (!file) return;
        loadModel(file);

        WoWModel* m = getLoadedModel();
        if (!m) return;

        // Apply customization from CreatureDisplayInfoExtra
        const auto* cdieTable = WOWDB.getTable("CreatureDisplayInfoExtra");
        auto cdieRow = cdieTable ? cdieTable->getRow(extraId) : DB2Row();
        if (cdieRow)
        {
            m->cd.set(CharDetails::SKIN_COLOR, static_cast<int>(cdieRow.getUInt("Skin")));
            m->cd.set(CharDetails::FACE, static_cast<int>(cdieRow.getUInt("Face")));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, static_cast<int>(cdieRow.getUInt("HairStyle")));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, static_cast<int>(cdieRow.getUInt("HairColor")));
            m->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, static_cast<int>(cdieRow.getUInt("FacialHair")));
        }

        // Apply equipment from NpcModelItemSlotDisplayInfo
        const auto* npcSlotTable = WOWDB.getTable("NpcModelItemSlotDisplayInfo");
        static const std::map<int, CharSlots> ItemTypeToInternal = {
            {0, CS_HEAD}, {1, CS_SHOULDER}, {2, CS_SHIRT}, {3, CS_CHEST}, {4, CS_BELT}, {5, CS_PANTS},
            {6, CS_BOOTS}, {7, CS_BRACERS}, {8, CS_GLOVES}, {9, CS_TABARD}, {10, CS_CAPE}
        };
        if (npcSlotTable)
        for (const auto& npcRow : *npcSlotTable)
        {
            if (npcRow.getUInt("NpcModelID") != extraId)
                continue;
            auto it = ItemTypeToInternal.find(static_cast<int>(npcRow.getUInt("ItemSlot")));
            if (it != ItemTypeToInternal.end())
            {
                WoWItem* item = m->getItem(it->second);
                if (item)
                    item->setDisplayId(static_cast<int>(npcRow.getUInt("ItemDisplayInfoID")));
            }
        }

        m->cd.isNPC = true;
        m->refresh();
    }
}

// ---- Item Browser helpers -------------------------------------------------
static void rebuildItemBrowseFilter()
{
    app.itemBrowseFiltered.clear();

    std::string search = core::toLower(std::string(app.itemBrowseSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        app.itemBrowseFiltered.push_back(i);
    }

    app.itemBrowseFilterDirty = false;
}

static void loadItemModel(unsigned int itemId)
{
    try
    {
        // ItemModifiedAppearance ? ItemAppearance ? ItemDisplayInfo ? ModelFileData/TextureFileData
        const auto* imaTable = WOWDB.getTable("ItemModifiedAppearance");
        const auto* iaTable = WOWDB.getTable("ItemAppearance");
        const auto* idiTable = WOWDB.getTable("ItemDisplayInfo");
        const auto* modFDTable = WOWDB.getTable("ModelFileData");
        const auto* texFDTable = WOWDB.getTable("TextureFileData");
        if (!imaTable || !iaTable || !idiTable || !modFDTable || !texFDTable) return;

        // Find ItemModifiedAppearance for this item
        uint32_t itemAppearanceID = 0;
        for (const auto& row : *imaTable)
        {
            if (row.getUInt("ItemID") == itemId)
            {
                itemAppearanceID = row.getUInt("ItemAppearanceID");
                break;
            }
        }
        if (itemAppearanceID == 0) return;

        auto iaRow = iaTable->getRow(itemAppearanceID);
        if (!iaRow) return;

        uint32_t displayInfoID = iaRow.getUInt("ItemDisplayInfoID");
        auto idiRow = idiTable->getRow(displayInfoID);
        if (!idiRow) return;

        // Find model file
        uint32_t modelResID = idiRow.getUInt("ModelResourcesID1");
        uint32_t modelFDID = 0;
        for (const auto& mfdRow : *modFDTable)
        {
            if (mfdRow.getUInt("ModelResourcesID") == modelResID)
            {
                modelFDID = mfdRow.getUInt("FileDataID");
                break;
            }
        }
        if (modelFDID == 0) return;

        GameFile* file = GAMEDIRECTORY.getFile(modelFDID);
        if (!file) return;

        loadModel(file);

        // Apply texture if available
        WoWModel* m = getLoadedModel();
        if (m)
        {
            uint32_t matResID = idiRow.getUInt("ModelMaterialResourcesID1");
            for (const auto& tfdRow : *texFDTable)
            {
                if (tfdRow.getUInt("MaterialResourcesID") == matResID)
                {
                    uint32_t texFDID = tfdRow.getUInt("FileDataID");
                    if (texFDID != 0)
                    {
                        GameFile* texFile = GAMEDIRECTORY.getFile(texFDID);
                        if (texFile)
                            m->updateTextureList(texFile, TEXTURE_OBJECT_SKIN);
                    }
                    break;
                }
            }
        }
    }
    catch (...)
    {
        LOG_ERROR << "Exception loading item model for ID " << itemId;
    }
}

// ---- Mount helpers --------------------------------------------------------
static void buildMountList()
{
    if (app.mountListBuilt || !app.isWoWLoaded || !app.initDB)
        return;

    app.mountList.clear();
    app.creatureModels.clear();
    app.creatureModelNames.clear();

    // Player mounts from MountXDisplay DB
    const auto* mountTable = WOWDB.getTable("Mount");
    const auto* mxdTable = WOWDB.getTable("MountXDisplay");
    if (mountTable && mxdTable)
    for (const auto& mxdRow : *mxdTable)
    {
        uint32_t mountID = mxdRow.getUInt("MountID");
        auto mountRow = mountTable->getRow(mountID);
        MountEntry me;
        me.displayID = static_cast<int>(mxdRow.getUInt("CreatureDisplayInfoID"));
        me.name = mountRow ? mountRow.getString("Name_Lang") : "";
        app.mountList.push_back(me);
    }
    std::sort(app.mountList.begin(), app.mountList.end(),
        [](const MountEntry& a, const MountEntry& b) { return a.name < b.name; });
    LOG_INFO << "Mount list: " << app.mountList.size() << " player mounts.";

    // All creature/*.m2 files
    std::vector<GameFile*> files;
    GAMEDIRECTORY.getFilesForFolder(files, std::string("creature/"), std::string("m2"));
    for (auto* gf : files)
    {
        app.creatureModels.push_back(gf);
        // Remove "creature/" prefix for readability
        std::string n = gf->fullname();
        if (n.size() > 9)
            n = n.substr(9);
        app.creatureModelNames.push_back(n);
    }
    // Sort alphabetically (keeping parallel arrays in sync)
    if (!app.creatureModels.empty())
    {
        std::vector<size_t> indices(app.creatureModels.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return app.creatureModelNames[a] < app.creatureModelNames[b]; });
        std::vector<GameFile*> sortedFiles(app.creatureModels.size());
        std::vector<std::string> sortedNames(app.creatureModelNames.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            sortedFiles[i] = app.creatureModels[indices[i]];
            sortedNames[i] = app.creatureModelNames[indices[i]];
        }
        app.creatureModels = std::move(sortedFiles);
        app.creatureModelNames = std::move(sortedNames);
    }
    LOG_INFO << "Creature models: " << app.creatureModels.size() << " files.";

    app.mountListBuilt = true;
    app.mountFilterDirty = true;
}

static void rebuildMountFilter()
{
    app.mountFiltered.clear();

    std::string search = core::toLower(std::string(app.mountSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    if (app.mountTab == 0)
    {
        for (size_t i = 0; i < app.mountList.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(app.mountList[i].name, search))
                continue;
            app.mountFiltered.push_back(i);
        }
    }
    else
    {
        for (size_t i = 0; i < app.creatureModelNames.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(app.creatureModelNames[i], search))
                continue;
            app.mountFiltered.push_back(i);
        }
    }

    app.mountFilterDirty = false;
}

static void mountCharacter(int displayID, GameFile* creatureFile)
{
    WoWModel* charModel = getLoadedModel();
    if (!charModel || !app.isChar || !app.root)
        return;

    // Get or resolve the mount model file
    GameFile* modelFile = nullptr;
    int morphID = 0;

    if (displayID > 0)
    {
        // Player mount � lookup model file from CreatureDisplayInfo ? CreatureModelData
        morphID = displayID;
        const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
        const auto* cmdTable = WOWDB.getTable("CreatureModelData");
        if (!cdiTable || !cmdTable) return;
        auto cdiRow = cdiTable->getRow(static_cast<uint32_t>(displayID));
        if (!cdiRow)
        {
            LOG_ERROR << "Mount display query failed for displayID " << displayID;
            return;
        }
        auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
        uint32_t mountFDID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
        if (mountFDID == 0)
        {
            LOG_ERROR << "Mount display query failed for displayID " << displayID;
            return;
        }
        modelFile = GAMEDIRECTORY.getFile(mountFDID);
    }
    else if (creatureFile)
    {
        modelFile = creatureFile;
    }

    if (!modelFile)
        return;

    // Get the character's attachment
    Attachment* charAtt = app.root->children.empty() ? nullptr : app.root->children[0];
    if (!charAtt)
        return;

    // Create the mount model
    auto* mountModel = new WoWModel(modelFile, false);
    if (!mountModel->ok)
    {
        LOG_ERROR << "Mount model failed to load.";
        delete mountModel;
        return;
    }
    mountModel->isMount = true;

    // Set mount as root model; character stays as child attachment
    app.root->setModel(mountModel);
    charAtt->id = 0; // attachment slot 0 = mount point

    // Apply mount skin/texture if it's a DB mount
    if (morphID > 0)
    {
        const auto* cdiTexTable = WOWDB.getTable("CreatureDisplayInfo");
        if (!cdiTexTable) return;
        auto cdiTexRow = cdiTexTable->getRow(static_cast<uint32_t>(morphID));
        if (cdiTexRow)
        {
            static const char* texFields[] = {
                "TextureVariationFileDataID1",
                "TextureVariationFileDataID2",
                "TextureVariationFileDataID3"
            };
            for (size_t t = 0; t < 3; ++t)
            {
                uint32_t texFDID = cdiTexRow.getUInt(texFields[t]);
                if (texFDID != 0)
                {
                    GameFile* texFile = GAMEDIRECTORY.getFile(texFDID);
                    if (texFile)
                        mountModel->updateTextureList(texFile, TEXTURE_GAMEOBJECT1 + static_cast<int>(t));
                }
            }
        }
    }

    // Sheathe character weapons
    charModel->bSheathe = true;

    // Switch character to mount animation
    if (charModel->animLookups.size() > ANIMATION_MOUNT &&
        charModel->animLookups[ANIMATION_MOUNT] >= 0)
    {
        charModel->animManager->Stop();
        charModel->currentAnim = charModel->animLookups[ANIMATION_MOUNT];
        charModel->animManager->SetAnim(0, static_cast<short>(charModel->currentAnim), 0);
        charModel->animManager->Play();
    }

    // Reset transforms
    charModel->rot_ = charModel->pos_ = glm::vec3(0.0f);
    charModel->scale_ = 1.0f;
    mountModel->rot_.x = 0.0f;

    app.isMounted = true;
    app.selModel = mountModel;

    // Update animation control for the mount
    initAnimationControl(mountModel);
    initModelControl(mountModel);

    resetCameraToModel(app.camera, mountModel);
    LOG_INFO << "Character mounted on: " << modelFile->fullname();
}

static void dismountCharacter()
{
    if (!app.isMounted || !app.root || !app.isChar)
        return;

    WoWModel* charModel = nullptr;
    Attachment* charAtt = app.root->children.empty() ? nullptr : app.root->children[0];
    if (charAtt)
        charModel = dynamic_cast<WoWModel*>(charAtt->model());

    // Remove mount model from root
    app.root->setModel(nullptr);
    app.isMounted = false;

    if (charAtt)
        charAtt->id = 0;

    if (charModel)
    {
        charModel->bSheathe = false;
        charModel->scale_ = 1.0f;
        charModel->rot_ = charModel->pos_ = glm::vec3(0.0f);

        app.selModel = charModel;
        initAnimationControl(charModel);
        initModelControl(charModel);
        resetCameraToModel(app.camera, charModel);
    }

    LOG_INFO << "Character dismounted.";
}

// ---- Handle viewport input ------------------------------------------------
static void handleViewportInput()
{
    const ImGuiIO& io = ImGui::GetIO();

    float mul = 1.0f;
    if (io.KeyShift)
        mul /= 10.0f;

    const float MOUSE_SENSITIVITY = 0.25f;

    // Mouse wheel ? zoom
    if (io.MouseWheel != 0.0f)
    {
        const float zoom = -io.MouseWheel * 0.5f * mul;
        app.camera.setRadius(app.camera.radius() + zoom);
    }

    // Left drag ? orbit (yaw / pitch)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        app.camera.setYawAndPitch(app.camera.yaw() + (-dx), app.camera.pitch() + (-dy));
    }

    // Right drag ? pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul * 0.025f;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul * 0.025f;
        const auto  look  = app.camera.lookAt();
        const auto  right = app.camera.right();
        app.camera.setLookAt(glm::vec3(look.x + right.x * -dx,
                                      look.y + right.y * -dx,
                                      look.z + dy));
    }

    // Middle drag ? zoom (alternative)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        app.camera.setRadius(app.camera.radius() + dy / 10.0f);
    }

    // Numpad camera controls
    if (ImGui::IsKeyDown(ImGuiKey_Keypad4))
        app.camera.setYaw(app.camera.yaw() + 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad6))
        app.camera.setYaw(app.camera.yaw() - 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad8))
        app.camera.setPitch(app.camera.pitch() + 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad2))
        app.camera.setPitch(app.camera.pitch() - 1.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad5))
        resetCameraToModel(app.camera, getLoadedModel());
    if (ImGui::IsKeyDown(ImGuiKey_Keypad7))
    {
        auto la = app.camera.lookAt();
        app.camera.setLookAt(glm::vec3(la.x, la.y, la.z + 0.2f));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad9))
    {
        auto la = app.camera.lookAt();
        app.camera.setLookAt(glm::vec3(la.x, la.y, la.z - 0.2f));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad1))
    {
        auto la = app.camera.lookAt();
        auto r = app.camera.right();
        app.camera.setLookAt(glm::vec3(la.x + r.x * -0.2f, la.y + r.y * -0.2f, la.z));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad3))
    {
        auto la = app.camera.lookAt();
        auto r = app.camera.right();
        app.camera.setLookAt(glm::vec3(la.x + r.x * 0.2f, la.y + r.y * 0.2f, la.z));
    }
}

// ---- Animation tick -------------------------------------------------------
static void tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - app.lastTick).count();
    app.lastTick = now;

    // Clamp to avoid huge jumps after breakpoints, window moves, or long pauses
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    // FPS tracking
    app.fpsAccum += dt;
    app.fpsFrameCount++;
    if (app.fpsAccum >= 0.5f)
    {
        app.fps = static_cast<float>(app.fpsFrameCount) / app.fpsAccum;
        app.fpsFrameCount = 0;
        app.fpsAccum = 0.0f;
    }

    app.animTime += dt;

    if (app.root)
        app.root->tick(dt * 1000.0f);
}

// ---- URL Import helpers ----------------------------------------------------
static void applyImportedChar(CharInfos* info)
{
    if (!info || !info->valid)
    {
        app.importStatus = "Import returned no valid character data.";
        return;
    }

    // Find the character model by race + gender
    int raceID = static_cast<int>(info->raceId);
    int sexID = (info->gender == "FEMALE" || info->gender == "Female") ? 1 : 0;

    int fileDataID = RaceInfos::getFileIDForRaceSex(raceID, sexID);
    if (fileDataID <= 0)
    {
        app.importStatus = "Could not determine model for race " + std::to_string(raceID);
        return;
    }

    GameFile* file = GAMEDIRECTORY.getFile(fileDataID);
    if (!file)
    {
        app.importStatus = "Model file not found for race " + std::to_string(raceID);
        return;
    }

    loadModel(file);

    WoWModel* model = getLoadedModel();
    if (!model || !app.isChar)
    {
        app.importStatus = "Failed to load character model.";
        return;
    }

    // Apply customizations
    auto& cd = model->cd;
    for (const auto& [optionId, choiceId] : info->customizations)
        cd.set(optionId, choiceId);

    // Apply eye glow
    cd.eyeGlowType = static_cast<EyeGlowTypes>(info->eyeGlowType);

    // Apply equipment
    for (int s = 0; s < NUM_CHAR_SLOTS && s < static_cast<int>(info->equipment.size()); ++s)
    {
        int itemId = info->equipment[s];
        if (itemId <= 0) continue;
        WoWItem* witem = model->getItem(static_cast<CharSlots>(s));
        if (witem)
            witem->setId(itemId);
    }

    model->refresh();

    // Update customization UI state
    initCharacterControl(model);

    app.importStatus = "Character imported successfully.";
    LOG_INFO << "Character imported from URL.";
}

static void applyImportedNPC(NPCInfos* info)
{
    if (!info || info->displayId <= 0)
    {
        app.importStatus = "Import returned no valid NPC data.";
        return;
    }

    // Use DB2Table to resolve CreatureDisplayInfo ? CreatureModelData ? FileDataID
    const auto* cdiTable = WOWDB.getTable("CreatureDisplayInfo");
    const auto* cmdTable = WOWDB.getTable("CreatureModelData");
    if (!cdiTable || !cmdTable)
    {
        app.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }
    auto cdiRow = cdiTable->getRow(static_cast<uint32_t>(info->displayId));
    if (!cdiRow)
    {
        app.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }
    auto cmdRow = cmdTable->getRow(cdiRow.getUInt("ModelID"));
    uint32_t npcFDID = cmdRow ? cmdRow.getUInt("FileDataID") : 0;
    if (npcFDID == 0)
    {
        app.importStatus = "NPC display ID " + std::to_string(info->displayId) + " not found in database.";
        return;
    }

    GameFile* file = GAMEDIRECTORY.getFile(npcFDID);
    if (!file)
    {
        app.importStatus = "NPC model file not found.";
        return;
    }

    loadModel(file);
    app.importStatus = std::string("NPC imported: ") + wstringToString(info->name);
    LOG_INFO << "NPC imported from URL: " << wstringToString(info->name);
}

static void applyImportedItem(ItemRecord* rec)
{
    if (!rec || rec->id <= 0)
    {
        app.importStatus = "Import returned no valid item data.";
        return;
    }

    loadItemModel(static_cast<unsigned int>(rec->id));
    app.importStatus = std::string("Item imported: ") + rec->name;
    LOG_INFO << "Item imported from URL: " << rec->name;
}

static void doURLImport()
{
    std::string url(app.importUrlBuf);
    if (url.empty())
    {
        app.importStatus = "Please enter a URL.";
        return;
    }

    app.importStatus = "Importing...";

    // Find matching importer
    ImporterPlugin* importer = nullptr;
    for (auto* imp : app.importers)
    {
        if (imp->acceptURL(url))
        {
            importer = imp;
            break;
        }
    }

    if (!importer)
    {
        app.importStatus = "No importer recognises this URL. Supported: battle.net, worldofwarcraft.com, wowhead.com";
        return;
    }

    // Try character import first (Armory)
    CharInfos* charInfo = importer->importChar(url);
    if (charInfo && charInfo->valid)
    {
        applyImportedChar(charInfo);
        delete charInfo;
        return;
    }
    delete charInfo;

    // Try NPC import (Wowhead)
    NPCInfos* npcInfo = importer->importNPC(url);
    if (npcInfo && npcInfo->displayId > 0)
    {
        applyImportedNPC(npcInfo);
        delete npcInfo;
        return;
    }
    delete npcInfo;

    // Try item import
    ItemRecord* itemRec = importer->importItem(url);
    if (itemRec && itemRec->id > 0)
    {
        applyImportedItem(itemRec);
        delete itemRec;
        return;
    }
    delete itemRec;

    app.importStatus = "Could not import anything from this URL.";
}

// ---- Engine initialization ------------------------------------------------
static void initEngine()
{
    // GlobalSettings singleton
    GLOBALSETTINGS.bShowParticle = true;
    GLOBALSETTINGS.bZeroParticle = true;

    // Create userSettings directory for logs / config
#ifdef _WIN32
    CreateDirectoryA("userSettings", nullptr);
#endif

    // Logger
    LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log_imgui.txt"));
    LOGGER.addChild(new WMVLog::LogOutputConsole());

    LOG_INFO << "==============================================";
    LOG_INFO << "Starting:" << GLOBALSETTINGS.appName()
             << GLOBALSETTINGS.appVersion()
             << GLOBALSETTINGS.buildName();
    LOG_INFO << "==============================================";

    app.settings.load();

    // Apply initial console visibility
#ifdef _WIN32
    if (HWND hConsole = GetConsoleWindow())
        ShowWindow(hConsole, app.settings.showConsole ? SW_SHOW : SW_HIDE);
#endif

    // Pre-fill the path input buffer from saved settings
    strncpy_s(app.pathBuf, app.settings.gamePath.c_str(), sizeof(app.pathBuf) - 1);

    // Instantiate exporters (OBJ / FBX)
    app.exporters.push_back(new OBJExporter());
    app.exporters.push_back(new FBXExporter());

    // Instantiate importers (Armory / Wowhead)
    app.importers.push_back(new ArmoryImporter());
    app.importers.push_back(new WowheadImporter());
}

static void initGL()
{
    video.render = true;
    // video.Init() calls gladLoaderLoadGL() internally � safe after GLFW context
    video.InitGL();

    SceneRenderer::initResources();

    LOG_INFO << "OpenGL initialisation complete.";
}

// ---- GLFW error callback --------------------------------------------------
static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ---- Entry point ----------------------------------------------------------
int main(int /*argc*/, char* /*argv*/[])
{
    // ---- GLFW + window ----
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "WoW Model Viewer", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    app.window = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

// Set window icon from wmv_16.png.
{
    // Resolve the icon relative to the executable so it works in installed
    // builds (NSIS) as well as development builds (compile-time path).
    int iw = 0, ih = 0, ic = 0;
    unsigned char* px = nullptr;
#ifdef _WIN32
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path iconPath = std::filesystem::path(exePath).parent_path() / "wmv_16.png";
        px = stbi_load(iconPath.string().c_str(), &iw, &ih, &ic, 4);
    }
#endif
    if (!px)
        px = stbi_load(WMV_ICON_PATH, &iw, &ih, &ic, 4);
    if (px)
    {
        GLFWimage img{ iw, ih, px };
        glfwSetWindowIcon(window, 1, &img);
        stbi_image_free(px);
    }
}

// ---- glad ----
    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialise OpenGL loader (glad)\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // ---- Engine init ----
    initEngine();
    initGL();

    // Create root attachment (scene graph root � no model yet)
    app.root = new Attachment(nullptr, nullptr, -1, -1);

    // ---- Dear ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = AppSettings::imguiIniPath;

    ThemeManager::apply(ThemeManager::currentTheme(), app.window);

    // ---- DPI-aware scaling ----
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    app.dpiScale = (xscale > yscale) ? xscale : yscale;
    if (app.dpiScale > 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(app.dpiScale);
    }

    // ---- Font discovery ----
    {
        namespace fs = std::filesystem;
        // Look for fonts next to the executable (installed build) and in the
        // compile-time source tree (development build).
        std::vector<fs::path> searchDirs;
#ifdef _WIN32
        {
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            searchDirs.push_back(fs::path(exePath).parent_path() / "fonts");
        }
#endif
#ifdef WMV_FONTS_PATH
        searchDirs.push_back(fs::path(WMV_FONTS_PATH));
#endif
        // Always include a "fonts" dir relative to cwd as fallback.
        searchDirs.push_back(fs::current_path() / "fonts");

        std::set<std::string> seen; // avoid duplicates (keyed on lowercase stem name)
        for (const auto& dir : searchDirs)
        {
            if (!fs::is_directory(dir))
                continue;
            for (const auto& entry : fs::directory_iterator(dir))
            {
                if (!entry.is_regular_file())
                    continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".ttf" && ext != ".otf")
                    continue;
                std::string stemName = entry.path().stem().string();
                std::string stemLower = stemName;
                std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);
                if (seen.count(stemLower))
                    continue;
                seen.insert(stemLower);
                std::string absPath = fs::canonical(entry.path()).string();
                app.availableFonts.push_back({stemName, absPath});
            }
        }
        // Sort alphabetically by display name
        std::sort(app.availableFonts.begin(), app.availableFonts.end(),
            [](const FontEntry& a, const FontEntry& b) { return a.name < b.name; });

        // Default font: prefer "arialn" by name if available and no saved preference
        if (app.settings.currentFont <= 0)
        {
            for (int i = 0; i < static_cast<int>(app.availableFonts.size()); ++i)
            {
                std::string lower = app.availableFonts[i].name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "arialn")
                {
                    app.settings.currentFont = i;
                    break;
                }
            }
        }
    }

    // ---- Build initial font atlas ----
    {
        const float pixelSize = app.settings.fontSize * app.dpiScale;
        bool loaded = false;
        if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
        {
            const auto& fe = app.availableFonts[app.settings.currentFont];
            if (std::filesystem::exists(fe.path))
            {
                io.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
                loaded = true;
                LOG_INFO << "Loaded font:" << fe.name << "at" << pixelSize << "px";
            }
        }
        if (!loaded)
            io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.0f; // size is already baked into the rasterised glyphs
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = false;
    bool firstFrame = true;
    app.lastTick = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // ---- Rebuild font atlas if font/size changed ----
        if (app.fontsDirty)
        {
            app.fontsDirty = false;
            ImGuiIO& fio = ImGui::GetIO();
            fio.Fonts->Clear();
            const float pixelSize = app.settings.fontSize * app.dpiScale;
            bool loaded = false;
            if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
            {
                const auto& fe = app.availableFonts[app.settings.currentFont];
                if (std::filesystem::exists(fe.path))
                {
                    fio.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
                    loaded = true;
                }
            }
            if (!loaded)
                fio.Fonts->AddFontDefault();
            fio.FontGlobalScale = 1.0f;
            fio.Fonts->Build();
        }

        // Check if background loading thread has finished
        pollAsyncLoad();

        // Animation tick
        tickScene();

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // ---- Build default docking layout on first frame (only if no saved layout) ----
        if (firstFrame)
        {
            firstFrame = false;
            if (!std::filesystem::exists(AppSettings::imguiIniPath))
            {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(static_cast<float>(fw), static_cast<float>(fh)));

            ImGuiID dock_left, dock_center;
            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.15f, &dock_left, &dock_center);

            ImGuiID dock_right;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.20f, &dock_right, &dock_center);

            ImGuiID dock_bottom;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.25f, &dock_bottom, &dock_center);

            ImGui::DockBuilderDockWindow("File Browser", dock_left);
            ImGui::DockBuilderDockWindow("NPC Browser", dock_left);
            ImGui::DockBuilderDockWindow("Item Browser", dock_left);
            ImGui::DockBuilderDockWindow("3D Viewport", dock_center);
            ImGui::DockBuilderDockWindow("Character Viewer", dock_center);
            ImGui::DockBuilderDockWindow("Animation", dock_bottom);
            ImGui::DockBuilderDockWindow("Screenshot", dock_bottom);
            ImGui::DockBuilderDockWindow("Export", dock_bottom);
            ImGui::DockBuilderDockWindow("Presets", dock_bottom);
            ImGui::DockBuilderDockWindow("Viewport Options", dock_right);
            ImGui::DockBuilderDockWindow("Mounts", dock_right);
            ImGui::DockBuilderDockWindow("Item Sets", dock_right);
            ImGui::DockBuilderDockWindow("Log", dock_bottom);
            ImGui::DockBuilderFinish(dockspace_id);
            }
        }

        // ===== Main Menu Bar =====
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Load WoW", nullptr, false, !app.isWoWLoaded && !app.loadInProgress))
                    beginLoadWoW();
                ImGui::Separator();
                if (ImGui::MenuItem("Import from URL...", nullptr, false, app.isWoWLoaded && app.initDB))
                {
                    app.showImportDialog = true;
                    app.importPopupJustOpened = true;
                    app.importStatus.clear();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Screenshot...", "Ctrl+S"))
                {
                    // Focus the Screenshot panel (user picks path there)
                    ImGui::SetWindowFocus("Screenshot");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4"))
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("3D Viewport", nullptr, &app.showViewport);
                ImGui::MenuItem("Character Viewer", nullptr, &app.showCharViewer);
                ImGui::MenuItem("File Browser", nullptr, &app.showFileBrowser);
                ImGui::MenuItem("Animation", nullptr, &app.showAnimation);
                ImGui::MenuItem("Viewport Options", nullptr, &app.showViewportOpts);
                ImGui::MenuItem("Mounts", nullptr, &app.showMounts);
                ImGui::MenuItem("Item Sets", nullptr, &app.showItemSets);
                ImGui::MenuItem("NPC Browser", nullptr, &app.showNpcBrowser);
                ImGui::MenuItem("Item Browser", nullptr, &app.showItemBrowser);
                ImGui::MenuItem("Export", nullptr, &app.showExport);
                ImGui::MenuItem("Screenshot", nullptr, &app.showScreenshot);
                ImGui::MenuItem("Presets", nullptr, &app.showPresets);
                ImGui::MenuItem("Log", nullptr, &app.showLog);
                ImGui::MenuItem("Settings", nullptr, &app.showSettings);
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Options"))
            {
                if (ImGui::MenuItem("Language / Locale..."))
                    app.showLanguageDialog = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Settings..."))
                    app.showSettings = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About..."))
                    app.showAboutDialog = true;
                ImGui::EndMenu();
            }

            // ---- Status bar (right-aligned in menu bar) ----
            {
                WoWModel* sm = getLoadedModel();
                std::string statusText;
                if (sm)
                {
                    int curFrame = 0, totalFrames = 0;
                    if (sm->animManager)
                    {
                        curFrame = static_cast<int>(sm->animManager->GetFrame());
                        totalFrames = static_cast<int>(sm->animManager->GetFrameCount());
                    }
                    statusText = std::format("FPS: {:.0f} | {} | V:{} B:{} T:{} | Frame: {}/{}",
                        app.fps, sm->name(),
                        sm->header.nVertices, sm->header.nBones, sm->header.nTextures,
                        curFrame, totalFrames);
                }
                else
                {
                    statusText = std::format("FPS: {:.0f}", app.fps);
                }
                float textWidth = ImGui::CalcTextSize(statusText.c_str()).x;
                ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10.0f);
                ImGui::TextDisabled("%s", statusText.c_str());
            }

            ImGui::EndMainMenuBar();
        }

        // ===== Character Viewer (standalone tab like wow.export) =====
        if (app.showCharViewer)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::Begin("Character Viewer", &app.showCharViewer))
        {
            CharacterViewerPanel::DrawContext cvCtx;
            cvCtx.isWoWLoaded          = app.isWoWLoaded;
            cvCtx.isDBReady            = app.initDB;
            cvCtx.isChar               = app.isChar;
            cvCtx.customizationOptions = &app.customizationOptions;
            cvCtx.animEntries          = &app.animEntries;
            cvCtx.selectedAnimCombo    = &app.selectedAnimCombo;
            cvCtx.fbo                  = &app.fbo;
            cvCtx.camera               = &app.camera;
            cvCtx.root                 = app.root;
            cvCtx.fov                  = video.fov;
            cvCtx.bgColor              = app.settings.bgColor;
            cvCtx.drawGrid             = app.settings.drawGrid;
            cvCtx.getLoadedModel       = getLoadedModel;
            cvCtx.loadModel            = [](GameFile* f) { loadModel(f); };
            cvCtx.handleViewportInput  = handleViewportInput;

            CharacterViewerPanel::draw(cvCtx);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        }

        // ===== 3D Viewport panel =====
        if (app.showViewport)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport", &app.showViewport))
        {
            // Determine available size for the viewport image
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);

            if (vpW > 0 && vpH > 0)
            {
                // Render scene to offscreen FBO
                SceneRenderer::renderToFBO(app.fbo, vpW, vpH, app.camera, app.root, video.fov, app.settings.bgColor, app.settings.drawGrid);

                // Display FBO colour texture (UV-flipped: OpenGL is bottom-up)
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(app.fbo.colorTex)),
                             panelSize,
                             ImVec2(0, 1), ImVec2(1, 0));

                // Handle orbit camera input when viewport is hovered
                if (ImGui::IsItemHovered())
                    handleViewportInput();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        }

        // ===== File Browser panel =====
        if (app.showFileBrowser)
        {
            auto statusStr = getLoadStatus();
            FileBrowserPanel::LoadState ls;
            ls.isLoaded   = app.isWoWLoaded;
            ls.inProgress = app.loadInProgress;
            ls.progress   = app.loadProgress;
            ls.statusText = statusStr.c_str();

            if (GameFile* picked = FileBrowserPanel::draw(app.showFileBrowser, ls))
                loadModel(picked);
        }

        // ===== Animation Control =====
        if (app.showAnimation)
        {
        if (ImGui::Begin("Animation", &app.showAnimation))
        {
            AnimationPanel::DrawContext animCtx;
            animCtx.getLoadedModel       = getLoadedModel;
            animCtx.animEntries          = &app.animEntries;
            animCtx.selectedAnimCombo    = &app.selectedAnimCombo;
            animCtx.animSpeed            = &app.animSpeed;
            animCtx.loopCount            = &app.loopCount;
            animCtx.lockAnims            = &app.lockAnims;
            animCtx.selectedSecondaryAnim = &app.selectedSecondaryAnim;
            animCtx.selectedMouthAnim    = &app.selectedMouthAnim;
            animCtx.mouthSpeed           = &app.mouthSpeed;
            animCtx.skinEntries          = &app.skinEntries;
            animCtx.selectedSkin         = &app.selectedSkin;
            animCtx.blpSkin[0] = app.blpSkin[0];
            animCtx.blpSkin[1] = app.blpSkin[1];
            animCtx.blpSkin[2] = app.blpSkin[2];
            animCtx.applySkin            = [](WoWModel* m, int idx) { applySkin(m, idx); };

            AnimationPanel::draw(animCtx);

            // Copy back blpSkin state (modified by per-slot BLP selector)
            app.blpSkin[0] = animCtx.blpSkin[0];
            app.blpSkin[1] = animCtx.blpSkin[1];
            app.blpSkin[2] = animCtx.blpSkin[2];
        }
        ImGui::End();
        }

        // ===== Viewport Options (combined Background / Lighting / Model Control) =====
        if (app.showViewportOpts)
        {
        if (ImGui::Begin("Viewport Options", &app.showViewportOpts))
        {
            ViewportOptionsPanel::DrawContext vpCtx;
            vpCtx.drawGrid       = &app.settings.drawGrid;
            vpCtx.bgColor        = &app.settings.bgColor;
            vpCtx.camera         = &app.camera;
            vpCtx.getLoadedModel = getLoadedModel;
            vpCtx.geosetGroups   = &app.geosetGroups;
            vpCtx.pcrState       = &app.pcrState;
            vpCtx.selectedSkin   = &app.selectedSkin;
            vpCtx.applySkin      = [](WoWModel* m, int idx) { applySkin(m, idx); };
            vpCtx.resetCamera    = [] { resetCameraToModel(app.camera, getLoadedModel()); };

            ViewportOptionsPanel::draw(vpCtx);
        }
        ImGui::End();
        }
        if (app.showMounts)
        {
        if (ImGui::Begin("Mounts", &app.showMounts))
        {
            WoWModel* cModel = getLoadedModel();
            if (cModel && app.isChar)
            {
                buildMountList(); // lazy init on first frame

                if (app.isMounted)
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mounted");
                    if (ImGui::Button("Dismount", ImVec2(-1, 0)))
                        dismountCharacter();
                }
                else
                {
                    if (ImGui::BeginTabBar("##MountTabs"))
                    {
                        int prevTab = app.mountTab;

                        if (ImGui::BeginTabItem("Player Mounts"))
                        {
                            app.mountTab = 0;
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Creature Models"))
                        {
                            app.mountTab = 1;
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();

                        if (app.mountTab != prevTab)
                        {
                            app.mountFilterDirty = true;
                            app.mountSearchBuf[0] = '\0';
                        }
                    }

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                    if (ImGui::InputText("##mountSearch", app.mountSearchBuf, sizeof(app.mountSearchBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                        app.mountFilterDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Apply##mount", ImVec2(-1, 0)))
                        app.mountFilterDirty = true;

                    if (app.mountFilterDirty)
                        rebuildMountFilter();

                    ImGui::Text("%d entries", static_cast<int>(app.mountFiltered.size()));
                    ImGui::Separator();

                    ImGui::BeginChild("##MountList", ImVec2(0, 0), ImGuiChildFlags_Borders);
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(app.mountFiltered.size()));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        {
                            size_t idx = app.mountFiltered[i];
                            ImGui::PushID(static_cast<int>(idx));

                            if (app.mountTab == 0)
                            {
                                const auto& me = app.mountList[idx];
                                std::string label = std::format("{} (DisplayID:{})", me.name, me.displayID);
                                if (ImGui::Selectable(label.c_str()))
                                    mountCharacter(me.displayID, nullptr);
                            }
                            else
                            {
                                if (ImGui::Selectable(app.creatureModelNames[idx].c_str()))
                                    mountCharacter(-1, app.creatureModels[idx]);
                            }

                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
            }
            else
            {
                ImGui::TextDisabled("Load a character model first.");
            }
        }
        ImGui::End();
        }

        // ===== Item Sets panel (standalone tab) =====
        if (app.showItemSets)
        {
        if (ImGui::Begin("Item Sets", &app.showItemSets))
        {
            WoWModel* cModel = getLoadedModel();
            if (cModel && app.isChar)
            {
                // ---- Item Sets ----
                buildItemSets(); // lazy init on first frame

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##itemSetSearch", app.itemSetSearchBuf, sizeof(app.itemSetSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    app.itemSetFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##itemset", ImVec2(-1, 0)))
                    app.itemSetFilterDirty = true;

                if (app.itemSetFilterDirty)
                    rebuildItemSetFilter();

                ImGui::Text("%d sets", static_cast<int>(app.itemSetFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##ItemSetList", ImVec2(0, 200), ImGuiChildFlags_Borders);
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(app.itemSetFiltered.size()));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        {
                            const auto& setEntry = app.itemSets[app.itemSetFiltered[i]];
                            ImGui::PushID(setEntry.id);
                            std::string label = std::format("{} (ID:{})", setEntry.name, setEntry.id);
                            if (ImGui::Selectable(label.c_str()))
                                applyItemSet(cModel, setEntry.id);
                            ImGui::PopID();
                        }
                    }
                }
                ImGui::EndChild();

                // ---- Start Outfits ----
                ImGui::SeparatorText("Start Outfits");

                if (!app.startOutfitsBuilt)
                    buildStartOutfits(cModel);

                if (app.startOutfits.empty())
                {
                    ImGui::TextDisabled("No start outfits available for this race/sex.");
                }
                else
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                    if (ImGui::InputText("##startOutfitSearch", app.startOutfitSearchBuf, sizeof(app.startOutfitSearchBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                        app.startOutfitFilterDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Apply##startoutfit", ImVec2(-1, 0)))
                        app.startOutfitFilterDirty = true;

                    if (app.startOutfitFilterDirty)
                        rebuildStartOutfitFilter();

                    ImGui::Text("%d classes", static_cast<int>(app.startOutfitFiltered.size()));
                    ImGui::Separator();

                    ImGui::BeginChild("##StartOutfitList", ImVec2(0, 150), ImGuiChildFlags_Borders);
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(app.startOutfitFiltered.size()));
                        while (clipper.Step())
                        {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                            {
                                const auto& entry = app.startOutfits[app.startOutfitFiltered[i]];
                                ImGui::PushID(entry.id);
                                std::string label = std::format("{} (ID:{})", entry.name, entry.id);
                                if (ImGui::Selectable(label.c_str()))
                                    applyStartOutfit(cModel, entry.id);
                                ImGui::PopID();
                            }
                        }
                    }
                    ImGui::EndChild();
                }
            }
            else
            {
                ImGui::TextDisabled("Load a character model first.");
            }
        }
        ImGui::End();
        }

        // ===== NPC Browser panel =====
        if (app.showNpcBrowser)
        {
        if (ImGui::Begin("NPC Browser", &app.showNpcBrowser))
        {
            if (!app.isWoWLoaded || !app.initDB)
            {
                ImGui::TextDisabled("Game not loaded.");
            }
            else
            {
                ImGui::Text("Search:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##npcSearch", app.npcSearchBuf, sizeof(app.npcSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    app.npcFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##npc", ImVec2(-1, 0)))
                    app.npcFilterDirty = true;

                if (app.npcFilterDirty)
                    rebuildNpcFilter();

                ImGui::Text("%d NPCs", static_cast<int>(app.npcFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##NpcList", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(app.npcFiltered.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& npc = npcs[app.npcFiltered[i]];
                        ImGui::PushID(static_cast<int>(app.npcFiltered[i]));
                        std::string label = std::format("{} (ID:{} Type:{})", npc.name, npc.id, npc.type);
                        if (ImGui::Selectable(label.c_str()))
                            loadNPC(static_cast<unsigned int>(npc.id));
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
        }

        // ===== Item Browser panel =====
        if (app.showItemBrowser)
        {
        if (ImGui::Begin("Item Browser", &app.showItemBrowser))
        {
            if (!app.isWoWLoaded || !app.initDB)
            {
                ImGui::TextDisabled("Game not loaded.");
            }
            else
            {
                ImGui::Text("Search:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##itemBrowseSearch", app.itemBrowseSearchBuf, sizeof(app.itemBrowseSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    app.itemBrowseFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##itembrowse", ImVec2(-1, 0)))
                    app.itemBrowseFilterDirty = true;

                if (app.itemBrowseFilterDirty)
                    rebuildItemBrowseFilter();

                ImGui::Text("%d items", static_cast<int>(app.itemBrowseFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##ItemBrowseList", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(app.itemBrowseFiltered.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& item = items.items[app.itemBrowseFiltered[i]];
                        ImGui::PushID(static_cast<int>(app.itemBrowseFiltered[i]));
                        ImVec4 qcol = getQualityColor(item.quality);
                        ImGui::PushStyleColor(ImGuiCol_Text, qcol);
                        std::string label = std::format("{} ({})", item.name, item.id);
                        if (ImGui::Selectable(label.c_str()))
                            loadItemModel(static_cast<unsigned int>(item.id));
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
        }

        // ===== Export panel =====
        if (app.showExport)
        {
        if (ImGui::Begin("Export", &app.showExport))
        {
            ExportPanel::DrawContext exCtx;
            exCtx.getLoadedModel    = getLoadedModel;
            exCtx.exporters         = &app.exporters;
            exCtx.selectedExporter  = &app.selectedExporter;
            exCtx.animEntries       = &app.animEntries;
            exCtx.exportAnimChecked = &app.exportAnimChecked;
            exCtx.selectedAnimCombo = &app.selectedAnimCombo;
            exCtx.exportPath        = app.exportPath;
            exCtx.exportPathSize    = sizeof(app.exportPath);
            exCtx.exportStatus      = &app.exportStatus;

            ExportPanel::draw(exCtx);
        }
        ImGui::End();
        }

        // ===== Screenshot panel =====
        if (app.showScreenshot)
        {
        if (ImGui::Begin("Screenshot", &app.showScreenshot))
        {
            ScreenshotPanel::DrawContext ssCtx;
            ssCtx.screenshotPath     = app.screenshotPath;
            ssCtx.screenshotPathSize = sizeof(app.screenshotPath);
            ssCtx.screenshotStatus   = &app.screenshotStatus;
            ssCtx.useCanvasOverride  = &app.useCanvasOverride;
            ssCtx.canvasWidth        = &app.canvasWidth;
            ssCtx.canvasHeight       = &app.canvasHeight;
            ssCtx.fbo                = &app.fbo;
            ssCtx.camera             = &app.camera;
            ssCtx.root               = app.root;
            ssCtx.fov                = video.fov;
            ssCtx.bgColor            = app.settings.bgColor;
            ssCtx.drawGrid           = app.settings.drawGrid;

            ScreenshotPanel::draw(ssCtx);
        }
        ImGui::End();
        }

        // ===== Character Preset panel =====
        if (app.showPresets)
        {
        if (ImGui::Begin("Presets", &app.showPresets))
        {
            ImGui::SeparatorText("Character Preset");
            ImGui::Text("File:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##presetPath", app.presetPath, sizeof(app.presetPath));

            {
                bool canSave = app.isChar && getLoadedModel() != nullptr;
                if (!canSave) ImGui::BeginDisabled();

                if (ImGui::Button("Save Preset", ImVec2(-1, 0)))
                    saveCharacterPreset(app.presetPath);

                if (!canSave) ImGui::EndDisabled();
            }

            {
                bool canLoad = app.isChar && getLoadedModel() != nullptr;
                if (!canLoad) ImGui::BeginDisabled();

                if (ImGui::Button("Load Preset", ImVec2(-1, 0)))
                    loadCharacterPreset(app.presetPath);

                if (!canLoad) ImGui::EndDisabled();
            }

            if (!app.presetStatus.empty())
            {
                bool isError = app.presetStatus.find("not found") != std::string::npos ||
                               app.presetStatus.find("No ") != std::string::npos;
                if (isError)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", app.presetStatus.c_str());
                else
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", app.presetStatus.c_str());
            }

            if (!app.isChar)
                ImGui::TextDisabled("Load a character model first.");
        }
        ImGui::End();
        }

        // ===== Log viewer panel =====
        if (app.showLog)
        {
        if (ImGui::Begin("Log", &app.showLog))
        {
            if (app.logNeedsReload)
                reloadLogFile();

            if (ImGui::Button("Reload"))
                reloadLogFile();
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                app.logLines.clear();
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &app.logAutoScroll);
            ImGui::SameLine();
            ImGui::TextDisabled("%d lines", static_cast<int>(app.logLines.size()));

            ImGui::Separator();
            ImGui::BeginChild("##LogScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(app.logLines.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto& line = app.logLines[i];
                    // Colour-code by log level
                    if (line.find("ERROR") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    else if (line.find("WARNING") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopStyleColor();
                }
            }
            if (app.logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();
        }

        // ===== Settings panel (floating popup) =====
        if (app.showSettings)
        {
        ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Settings", &app.showSettings, ImGuiWindowFlags_NoDocking))
        {
            // ---- Game loading section ----
            ImGui::SeparatorText("World of Warcraft");

            ImGui::Text("Game Data Path:");
            float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputText("##gamepath", app.pathBuf, sizeof(app.pathBuf));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
                openFolderPicker();

            // ---- Folder Picker popup ----
            if (app.showFolderPicker)
                ImGui::OpenPopup("Select Folder##FolderPicker");

            if (ImGui::BeginPopupModal("Select Folder##FolderPicker", &app.showFolderPicker,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                // Current path display
                std::string curPathStr = app.folderPickerCurrent.empty()
                    ? "My Computer" : app.folderPickerCurrent.string();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", curPathStr.c_str());
                ImGui::Separator();

                if (app.folderPickerNeedsRefresh)
                    folderPickerRefresh();

                // Up / back button
                {
                    bool canGoUp = !app.folderPickerCurrent.empty() && app.folderPickerCurrent.has_parent_path()
                        && app.folderPickerCurrent.parent_path() != app.folderPickerCurrent;
                    bool canGoRoot = !app.folderPickerCurrent.empty();
                    if (!canGoUp && !canGoRoot) ImGui::BeginDisabled();
                    if (ImGui::Button("Up"))
                    {
                        if (canGoUp)
                            app.folderPickerCurrent = app.folderPickerCurrent.parent_path();
                        else
                            app.folderPickerCurrent.clear(); // back to drive roots
                        app.folderPickerNeedsRefresh = true;
                    }
                    if (!canGoUp && !canGoRoot) ImGui::EndDisabled();
                }

                ImGui::SameLine();
                ImGui::Text("%d folders", static_cast<int>(app.folderPickerEntries.size()));

                // Folder list
                ImGui::BeginChild("##FolderList", ImVec2(500, 400), ImGuiChildFlags_Borders);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(app.folderPickerEntries.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& p = app.folderPickerEntries[i];
                        std::string displayName = app.folderPickerCurrent.empty()
                            ? p.string() : p.filename().string();
                        ImGui::PushID(i);
                        if (ImGui::Selectable(displayName.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                app.folderPickerCurrent = p;
                                app.folderPickerNeedsRefresh = true;
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                ImGui::Separator();
                if (ImGui::Button("Select This Folder", ImVec2(200, 0)))
                {
                    if (!app.folderPickerCurrent.empty())
                    {
                        strncpy_s(app.pathBuf, app.folderPickerCurrent.string().c_str(), sizeof(app.pathBuf) - 1);
                        app.showFolderPicker = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    app.showFolderPicker = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (app.isWoWLoaded)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded: %s (%s)",
                                   GAMEDIRECTORY.version().c_str(),
                                   GAMEDIRECTORY.locale().c_str());
            }
            else if (app.loadInProgress)
            {
                ImGui::ProgressBar(app.loadProgress);
                auto status = getLoadStatus();
                ImGui::TextWrapped("%s", status.c_str());
            }
            else
            {
                auto status = getLoadStatus();
                if (!status.empty())
                    ImGui::TextWrapped("%s", status.c_str());
                else
                    ImGui::TextDisabled("Use File > Load WoW to load game data.");
            }

            ImGui::Checkbox("Enable Database Cache", &app.settings.enableDbCache);
            ImGui::TextDisabled("Speeds up loading by caching the database. Takes effect on next load.");

            // ---- General section ----
            ImGui::SeparatorText("General");
            if (ImGui::Checkbox("Show Console Window", &app.settings.showConsole))
            {
#ifdef _WIN32
                if (HWND hConsole = GetConsoleWindow())
                    ShowWindow(hConsole, app.settings.showConsole ? SW_SHOW : SW_HIDE);
#endif
            }
            ImGui::TextDisabled("Shows/hides the debug console. Useful for diagnostics.");

            // ---- Theme selector ----
            ImGui::SeparatorText("Appearance");
            ImGui::Text("Theme:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Theme", &ThemeManager::currentThemeRef(), ThemeManager::themeNames(), ThemeManager::themeCount()))
                ThemeManager::apply(ThemeManager::currentTheme(), app.window);

            // ---- Font selector ----
            ImGui::Text("Font:");
            ImGui::SetNextItemWidth(-1);
            if (!app.availableFonts.empty())
            {
                if (ImGui::BeginCombo("##Font",
                    (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
                        ? app.availableFonts[app.settings.currentFont].name.c_str() : "Default"))
                {
                    for (int i = 0; i < static_cast<int>(app.availableFonts.size()); ++i)
                    {
                        ImGui::PushID(i);
                        const bool selected = (i == app.settings.currentFont);
                        if (ImGui::Selectable(app.availableFonts[i].name.c_str(), selected))
                        {
                            app.settings.currentFont = i;
                            app.fontsDirty = true;
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
            if (ImGui::SliderFloat("##FontSize", &app.settings.fontSize, 10.0f, 40.0f, "%.0f px"))
                app.fontsDirty = true;
            ImGui::TextDisabled("Drop .ttf or .otf files into the fonts/ folder to add more.");

            ImGui::Separator();
            ImGui::Checkbox("ImGui Demo Window", &show_demo_window);
            ImGui::Separator();
            ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                        app.camera.yaw(), app.camera.pitch(), app.camera.radius());
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));

            // ---- Save ----
            ImGui::SeparatorText("Save");
            if (ImGui::Button("Save Settings", ImVec2(-1, 0)))
            {
                app.settings.gamePath = app.pathBuf;
                app.settings.save();
            }
            ImGui::TextDisabled("Saves preferences and UI layout.");
        }
        ImGui::End();
        }

        // ===== URL Import dialog =====
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
                doURLImport();
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

        // ===== Config selection modal (multiple WoW installs) =====
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
                launchLoadThread(app.pendingConfigs[app.selectedConfig]);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                app.showConfigPopup = false;
                setLoadStatus("Load cancelled.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // ===== About Dialog =====
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
                int n = WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(), static_cast<int>(wTitle.size()), nullptr, 0, nullptr, nullptr);
                title.resize(n);
                WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(), static_cast<int>(wTitle.size()), title.data(), n, nullptr, nullptr);
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

        // ===== Language / Locale Dialog =====
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
                        // Trigger a reload with the selected config
                        app.isWoWLoaded = false;
                        app.initDB = false;
                        app.loadInProgress = true;
                        app.loadProgress = 0.0f;
                        setLoadStatus("Reloading with locale: " + configs[i].locale + "...");
                        launchLoadThread(configs[i]);
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

        // ---- Render ImGui over the default framebuffer ----
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ---- Cleanup ----
    if (app.loadThread.joinable())
        app.loadThread.join();

    FileBrowserPanel::shutdown();

    if (app.root)
    {
        app.root->delChildren();
        delete app.root;
        app.root = nullptr;
    }

    app.fbo.destroy();

    SceneRenderer::shutdown();

    for (auto* e : app.exporters)
        delete e;
    app.exporters.clear();

    for (auto* imp : app.importers)
        delete imp;
    app.importers.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer shutdown complete.";
    return 0;
}
