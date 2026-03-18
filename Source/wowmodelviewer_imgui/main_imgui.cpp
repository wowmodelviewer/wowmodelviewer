// ============================================================================
// WoW Model Viewer — ImGui / GLFW entry point
//
// Replaces the wxWidgets WinMain ? wxEntry flow from main.cpp / app.cpp.
// Initialises engine systems (GlobalSettings, Logger, video), creates an
// offscreen FBO for the 3-D viewport, renders the scene into that FBO, and
// displays it as an ImGui::Image() inside a dockable "3D Viewport" panel.
// OrbitCamera input is wired to ImGui's mouse/keyboard state.
//
// Game loading (CascLib + GameDatabase) — reads Config.ini,
// opens the WoW game folder via an ImGui path dialog, initialises the
// CASC storage, loads the listfile, and builds the in-memory database.
//
// File Browser — filterable tree view of CASC files with configurable
// extension filter and text search. Clicking a .m2 file loads it.
//
// Model loading — creates WoWModel from GameFile, sets up character
// equipment slots, and resets the camera to frame the model.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

// Engine (no wxWidgets dependencies)
#include "GlobalSettings.h"
#include "logger/Logger.h"
#include "logger/LogOutputFile.h"
#include "logger/LogOutputConsole.h"
#include "video.h"
#include "globalvars.h"
#include "Attachment.h"
#include "WoWModel.h"
#include "OrbitCamera.h"

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
#include "WoWItem.h"
#include "CharDetails.h"

#include "stb_image_write.h"

// Exporters (OBJ / FBX)
#include "OBJExporter.h"
#include "FBXExporter.h"

#include <format>
#include <deque>
#include <map>
#include <set>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---- FBO wrapper ----------------------------------------------------------
struct ViewportFBO
{
    GLuint fbo       = 0;
    GLuint colorTex  = 0;
    GLuint depthRbo  = 0;
    int    width     = 0;
    int    height    = 0;

    void create(int w, int h)
    {
        width  = w;
        height = h;
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTex);
        glGenRenderbuffers(1, &depthRbo);

        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resize(int w, int h)
    {
        if (w == width && h == height)
            return;
        destroy();
        if (w > 0 && h > 0)
            create(w, h);
    }

    void bind()   const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    void unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0);   }

    void destroy()
    {
        if (fbo)       { glDeleteFramebuffers(1, &fbo);       fbo       = 0; }
        if (colorTex)  { glDeleteTextures(1, &colorTex);      colorTex  = 0; }
        if (depthRbo)  { glDeleteRenderbuffers(1, &depthRbo); depthRbo  = 0; }
        width = height = 0;
    }
};

// ---- Globals --------------------------------------------------------------
static OrbitCamera  g_camera;
static Attachment*  g_root       = nullptr;
static ViewportFBO  g_fbo;
static bool         g_drawGrid   = true;
static glm::vec3    g_bgColor(0.22f, 0.22f, 0.22f);
static GLuint       g_checkerTex = 0;
static bool         g_drawCheckerBg = true;

// Timing for animation tick
static float        g_animTime   = 0.0f;
static std::chrono::steady_clock::time_point g_lastTick;

// ---- Game loading state (Phase 2) -----------------------------------------
static std::string  g_gamePath;              // WoW Data folder path
static std::string  g_cfgPath;               // userSettings/Config.ini
static bool         g_isWoWLoaded  = false;
static bool         g_initDB       = false;
static bool         g_enableDbCache = false;
static std::string  g_loadStatus;            // status text shown in File Browser
static std::atomic<float> g_loadProgress{0.0f}; // 0..1 progress bar fraction (atomic for thread safety)
static bool         g_loadInProgress = false;

// Async loading state
static std::thread        g_loadThread;
static std::mutex         g_loadStatusMutex;
static std::atomic<bool>  g_loadThreadDone{false};
static std::atomic<bool>  g_loadThreadSuccess{false};

// ImGui folder-path input buffer
static char         g_pathBuf[1024] = {};

// Config selection state (ImGui modal replaces wxGetSingleChoiceIndex)
static bool                            g_showConfigPopup = false;
static std::vector<core::GameConfig>   g_pendingConfigs;
static int                             g_selectedConfig  = 0;

// ---- File Browser state ---------------------------------------------------
struct FileBrowserNode
{
    std::string                              name;
    GameFile*                                file = nullptr; // non-null for leaf nodes
    std::map<std::string, FileBrowserNode*>  children;
};

// Arena allocator: all tree nodes live in a deque and are freed in bulk.
// std::deque never moves existing elements on push_back, so FileBrowserNode*
// pointers stored in children maps remain valid as the pool grows.
static std::deque<FileBrowserNode>  g_nodePool;

static FileBrowserNode* allocNode()
{
    g_nodePool.emplace_back();
    return &g_nodePool.back();
}

static void freeNodePool()
{
    g_nodePool.clear();
}

static const char* g_filterLabels[] = {
    "Models (*.m2)",
    "WMOs (*.wmo)",
    "ADTs (*.adt)",
    "Sounds (*.wav)",
    "OGGs (*.ogg)",
    "MP3s (*.mp3)",
    "Images (*.blp)",
    "Shaders (*.bls)",
    "DBCs (*.dbc)",
    "DB2s (*.db2)",
    "LUAs (*.lua)",
    "XMLs (*.xml)",
    "SKINs (*.skin)"
};

static const char* g_filterExtensions[] = {
    "m2", "wmo", "adt", "wav", "ogg", "mp3",
    "blp", "bls", "dbc", "db2", "lua", "xml", "skin"
};

static constexpr int          g_filterCount = sizeof(g_filterLabels) / sizeof(g_filterLabels[0]);
static int                    g_filterMode  = 0; // index into g_filterLabels
static char                   g_searchBuf[256] = {};
static FileBrowserNode*       g_fileTreeRoot = nullptr;
static bool                   g_fileTreeDirty = true; // needs rebuild
static int                    g_fileTreeFileCount = 0;
static bool                   g_isModel = false;
static bool                   g_isChar  = false;

// ---- Animation control state (Phase 4) ------------------------------------
struct AnimEntry
{
    std::string label;
    int animIndex;   // index into model->anims[]
};

static std::vector<AnimEntry>  g_animEntries;
static int                     g_selectedAnimCombo = 0;
static float                   g_animSpeed = 1.0f;
static bool                    g_autoAnimate = true;

struct SkinEntry
{
    std::string label;
    GameFile* tex[3] = {nullptr, nullptr, nullptr};
    int base = 0;
    size_t count = 0;
    std::set<int> creatureGeosetData;
};

static std::vector<SkinEntry>  g_skinEntries;
static int                     g_selectedSkin = -1;

// ---- Character control state (Phase 4) ------------------------------------
struct CustomizationOption
{
    unsigned int optionID;
    std::string name;
    std::vector<unsigned int> choiceIDs;
    std::vector<std::string> choiceNames;
    int selectedIndex = 0;
};

static std::vector<CustomizationOption> g_customizationOptions;

// ---- Equipment popup state ------------------------------------------------
static char                   g_equipSearchBuf[256] = {};
static int                    g_equipSlotToEdit = -1;
static bool                   g_equipPopupJustOpened = false;
static std::vector<size_t>    g_equipFilteredItems; // indices into items.items
static int                    g_equipSlotLevels[NUM_CHAR_SLOTS] = {};

// ---- Item Set / Start Outfit state ----------------------------------------
struct ItemSetEntry
{
    int id;
    std::string name;
};

static std::vector<ItemSetEntry>  g_itemSets;
static bool                       g_itemSetsBuilt = false;
static char                       g_itemSetSearchBuf[256] = {};
static std::vector<size_t>        g_itemSetFiltered; // indices into g_itemSets
static bool                       g_itemSetFilterDirty = true;

struct StartOutfitEntry
{
    int id;           // CharStartOutfit.ID
    std::string name; // class name
};

static std::vector<StartOutfitEntry> g_startOutfits;
static bool                          g_startOutfitsBuilt = false;
static char                          g_startOutfitSearchBuf[256] = {};
static std::vector<size_t>           g_startOutfitFiltered; // indices into g_startOutfits
static bool                          g_startOutfitFilterDirty = true;

// ---- Light Control state --------------------------------------------------
struct LightSettings
{
    float direction[4] = { -1.0f, 1.0f, -1.0f, 0.0f }; // xyz + w=0 directional
    float diffuse[3]   = {  1.0f, 1.0f,  1.0f };
    float ambient[3]   = {  0.35f, 0.35f, 0.35f };
    float specular[3]  = {  0.0f, 0.0f,  0.0f };
    float intensity    = 1.0f;
    bool  enabled      = true;
};

static LightSettings g_light;

// ---- Model Control state --------------------------------------------------
struct GeosetEntry
{
    size_t index;     // index into model->geosets[]
    uint32_t id;      // geoset id
    std::string label;
};

struct GeosetGroupEntry
{
    std::string name;
    size_t meshId;
    std::vector<GeosetEntry> geosets;
};

static std::vector<GeosetGroupEntry> g_geosetGroups;

struct ParticleColorState
{
    bool enabled = false;
    float colors[3][3][3] = {}; // [set 0..2 for IDs 11,12,13][phase 0..2 = start/mid/end][r/g/b]
    bool hasSet[3] = {};        // which color IDs (11,12,13) are present on model
};

static ParticleColorState g_pcrState;

// ---- Screenshot state -----------------------------------------------------
static char g_screenshotPath[512] = "screenshot.png";
static std::string g_screenshotStatus;

// ---- Save/Load Character Preset state -------------------------------------
static char g_presetPath[512] = "userSettings/preset.ini";
static std::string g_presetStatus;

// ---- NPC Browser state ----------------------------------------------------
static char g_npcSearchBuf[256] = {};
static std::vector<size_t> g_npcFiltered; // indices into npcs
static bool g_npcFilterDirty = true;

// ---- Item Browser state ---------------------------------------------------
static char g_itemBrowseSearchBuf[256] = {};
static std::vector<size_t> g_itemBrowseFiltered; // indices into items.items
static bool g_itemBrowseFilterDirty = true;

// ---- Export state ---------------------------------------------------------
static std::vector<ExporterPlugin*> g_exporters;
static int                          g_selectedExporter = 0;
static char                         g_exportPath[512] = "export";
static std::string                  g_exportStatus;
static std::vector<char>            g_exportAnimChecked; // per-anim checkbox state (char to avoid vector<bool> proxy)

// ---- Mount state ----------------------------------------------------------
struct MountEntry
{
    int         displayID;  // CreatureDisplayInfoID (>0 for DB mounts, -1 for "None")
    std::string name;
};

static std::vector<MountEntry>   g_mountList;        // built from MountXDisplay DB
static std::vector<GameFile*>    g_creatureModels;   // all creature/*.m2 files
static std::vector<std::string>  g_creatureModelNames;
static bool                      g_mountListBuilt = false;
static bool                      g_isMounted = false;
static char                      g_mountSearchBuf[256] = {};
static int                       g_mountTab = 0;      // 0 = Player Mounts, 1 = Creature Models
static std::vector<size_t>       g_mountFiltered;     // filtered indices into g_mountList or g_creatureModels
static bool                      g_mountFilterDirty = true;

// ---- Helpers --------------------------------------------------------------
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

// ---- Config.ini reading/writing -------------------------------------------
static const char* g_imguiIniPath = "userSettings/imgui_layout.ini";

static void loadSettings()
{
    g_cfgPath = "userSettings/Config.ini";
    const core::IniFile config(g_cfgPath);

    g_gamePath = config.getString("Settings/Path");
    g_enableDbCache = config.getBool("Settings/EnableDbCache", false);
    g_drawGrid = config.getBool("Viewport/DrawGrid", true);
    g_bgColor.x = static_cast<float>(config.getDouble("Viewport/BgR", 71.0 / 255.0));
    g_bgColor.y = static_cast<float>(config.getDouble("Viewport/BgG", 95.0 / 255.0));
    g_bgColor.z = static_cast<float>(config.getDouble("Viewport/BgB", 121.0 / 255.0));

    LOG_INFO << "Settings loaded. Game path:" << g_gamePath;
}

static void saveSettings()
{
    core::IniFile config(g_cfgPath);
    config.setValue("Settings/Path", g_gamePath);
    config.setValue("Settings/EnableDbCache", g_enableDbCache);
    config.setValue("Viewport/DrawGrid", g_drawGrid);
    config.setValue("Viewport/BgR", static_cast<double>(g_bgColor.x));
    config.setValue("Viewport/BgG", static_cast<double>(g_bgColor.y));
    config.setValue("Viewport/BgB", static_cast<double>(g_bgColor.z));
    config.sync();

    ImGui::SaveIniSettingsToDisk(g_imguiIniPath);
    LOG_INFO << "Settings and UI layout saved.";
}

// ---- Thread-safe load status helpers --------------------------------------
static void setLoadStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(g_loadStatusMutex);
    g_loadStatus = s;
}

static std::string getLoadStatus()
{
    std::lock_guard<std::mutex> lock(g_loadStatusMutex);
    return g_loadStatus;
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

    const bool enableDbCache = g_enableDbCache;

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
        LOG_INFO << "Database cache miss — will rebuild from DB2 files.";
        fs::remove(cachePath, ec);
        fs::remove(versionPath, ec);
    }

    if (enableDbCache)
    {
        GAMEDATABASE.setCachePath(cachePath.string());
        GAMEDATABASE.setFastMode();
    }

    if (!GAMEDATABASE.initFromXML("database.xml"))
    {
        g_initDB = false;
        LOG_ERROR << "Database initialization failed!";
        setLoadStatus("Database initialization failed!");
        // Clean up partial cache so next launch starts fresh
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
    g_loadProgress = 0.60f;

    CharTexture::initRegions();
    g_loadProgress = 0.65f;

    RaceInfos::init();
    g_loadProgress = 0.70f;

    g_initDB = true;

    {
        sqlResult npc = GAMEDATABASE.sqlQuery(
            "SELECT ID, DisplayID1, CreatureType, Name_Lang From Creature;");

        if (npc.valid && !npc.empty())
        {
            LOG_INFO << "Found " << npc.values.size() << " NPCs";
            for (const auto& value : npc.values)
            {
                NPCRecord rec(value);
                if (rec.model != 0)
                    npcs.push_back(rec);
            }
        }
        else
        {
            g_initDB = false;
            LOG_ERROR << "Error during NPC detection from database.";
            return;
        }
    }

    g_loadProgress = 0.80f;

    {
        sqlResult item = GAMEDATABASE.sqlQuery(
            "SELECT Item.ID, ItemSparse.Display_Lang, Item.InventoryType, "
            "Item.ClassID, Item.SubclassID, Item.SheathType "
            "FROM Item LEFT JOIN ItemSparse ON Item.ID = ItemSparse.ID "
            "WHERE Item.InventoryType !=0 AND ItemSparse.Display_Lang != \"\"");

        if (item.valid && !item.empty())
        {
            LOG_INFO << "Found " << item.values.size() << " items";
            for (const auto& value : item.values)
            {
                ItemRecord rec(value);
                items.items.push_back(rec);
            }
        }
        else
        {
            g_initDB = false;
            LOG_ERROR << "Error during Item detection from database.";
            return;
        }
    }

    g_loadProgress = 0.90f;
    LOG_INFO << "Finished initiating database files.";
}

// ---- loadWoW (ported from ModelViewer::LoadWoW) ---------------------------
// Called on the background thread to perform heavy CASC / listfile / DB work.
static void loadWoW(const core::GameConfig& config)
{
    g_loadProgress = 0.0f;
    setLoadStatus("Opening CASC storage...");

    if (!GAMEDIRECTORY.setConfig(config))
    {
        LOG_ERROR << "Could not load WoW Data folder (error "
                  << GAMEDIRECTORY.lastError() << ").";
        setLoadStatus("Failed to open CASC storage (error "
                       + std::to_string(GAMEDIRECTORY.lastError()) + ").");
        g_loadThreadDone = true;
        return;
    }

    LOG_INFO << "Major version: " << GAMEDIRECTORY.majorVersion();
    g_loadProgress = 0.05f;

    // Set the config folder used for database.xml, listfile paths, etc.
    auto ver = core::split(GAMEDIRECTORY.version(), '.');
    const std::string baseConfigFolder = "games/wow/" + ver[0] + "." + ver[1] + "/";
    LOG_INFO << "Using config folder: " << baseConfigFolder;
    core::Game::instance().setConfigFolder(baseConfigFolder);

    // Load file list from listfile.csv
    setLoadStatus("Loading file list...");
    g_loadProgress = 0.10f;
    GAMEDIRECTORY.setProgressCallback([](int current, int total) {
        if (total > 0)
            g_loadProgress = 0.10f + 0.40f * static_cast<float>(current) / static_cast<float>(total);
    });
    GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
    GAMEDIRECTORY.setProgressCallback(nullptr);
    g_loadProgress = 0.50f;

    // Init database
    setLoadStatus("Initializing database...");
    g_loadProgress = 0.55f;
    initDatabase();

    if (!g_initDB)
    {
        g_loadThreadDone = true;
        return;
    }

    g_loadProgress = 1.0f;
    setLoadStatus("World of Warcraft loaded successfully.");
    g_loadThreadSuccess = true;
    g_loadThreadDone = true;
}

// ---- Async thread launcher / poller ---------------------------------------
static void loadWoWThreadFunc(core::GameConfig config)
{
    setLoadStatus("Checking support files...");
    if (!checkAndDownloadSupportFiles())
    {
        g_loadThreadDone = true;
        return;
    }
    loadWoW(config);
}

static void launchLoadThread(const core::GameConfig& config)
{
    g_loadProgress = 0.0f;
    g_loadThreadDone = false;
    g_loadThreadSuccess = false;
    g_loadInProgress = true;

    if (g_loadThread.joinable())
        g_loadThread.join();

    g_loadThread = std::thread(loadWoWThreadFunc, config);
}

static void pollAsyncLoad()
{
    if (!g_loadInProgress || !g_loadThreadDone.load())
        return;

    if (g_loadThread.joinable())
        g_loadThread.join();

    g_loadInProgress = false;

    if (g_loadThreadSuccess.load())
    {
        g_isWoWLoaded = true;
        g_fileTreeDirty = true;
        LOG_INFO << "World of Warcraft loaded successfully. Version: "
                 << GAMEDIRECTORY.version() << " Locale: " << GAMEDIRECTORY.locale();
        saveSettings();
    }
}

// Called when the user clicks "Load WoW" — validates the path and launches
// the background loading thread (downloads, CASC, listfile, database).
static void beginLoadWoW()
{
    if (g_isWoWLoaded || g_loadInProgress)
        return;

    g_loadInProgress = true;
    g_loadProgress = 0.0f;
    setLoadStatus("Validating game path...");

    // Validate game path
    namespace fs = std::filesystem;
    std::string path = g_gamePath;
    if (path.empty() || !fs::is_directory(path))
    {
        setLoadStatus("Please set a valid WoW Data folder path above.");
        g_loadInProgress = false;
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
    g_gamePath = path;

    // Init Game if needed
    if (!core::Game::instance().initDone())
        core::Game::instance().init(new wow::WoWFolder(g_gamePath), new wow::WoWDatabase());

    // Check available configs
    g_pendingConfigs = GAMEDIRECTORY.configsFound();

    if (g_pendingConfigs.empty())
    {
        LOG_ERROR << "No locale found in WoW folder.";
        setLoadStatus("No locale found in the WoW folder.");
        g_loadInProgress = false;
        return;
    }

    if (g_pendingConfigs.size() == 1)
    {
        // Only one config — launch background loading thread
        launchLoadThread(g_pendingConfigs[0]);
    }
    else
    {
        // Multiple configs — show selection popup
        g_selectedConfig = 0;
        g_showConfigPopup = true;
        g_loadInProgress = false; // will resume after user picks
    }
}

// ---- Build file browser tree from CASC files ------------------------------
static void rebuildFileTree()
{
    if (!g_isWoWLoaded)
        return;

    freeNodePool();
    g_fileTreeRoot = allocNode();
    g_fileTreeRoot->name = "Root";

    // Prepare filter strings (case-insensitive via pre-lowered comparison)
    std::string search = core::toLower(std::string(g_searchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    const std::string ext = std::string(".") + g_filterExtensions[g_filterMode];

    // Direct iteration — no regex, just suffix + substring check
    g_fileTreeFileCount = 0;
    for (auto* gf : GAMEDIRECTORY)
    {
        const auto& fname = gf->fullname();   // already lowercase
        if (!core::endsWithIgnoreCase(fname, ext))
            continue;
        if (!search.empty() && !core::containsIgnoreCase(fname, search))
            continue;

        ++g_fileTreeFileCount;

        // Build display name
        std::string displayName = std::format("{} [{}]", fname, gf->fileDataId());
        std::transform(displayName.begin(), displayName.end(), displayName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::replace(displayName.begin(), displayName.end(), '/', '\\');
        if (!displayName.empty())
            displayName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(displayName[0])));

        auto parts = core::split(displayName, '\\');
        FileBrowserNode* cur = g_fileTreeRoot;

        // Build intermediate directory nodes
        for (int i = 0; i < static_cast<int>(parts.size()) - 1; ++i)
        {
            auto it = cur->children.find(parts[i]);
            if (it == cur->children.end())
            {
                auto* child = allocNode();
                child->name = parts[i];
                cur->children[parts[i]] = child;
                cur = child;
            }
            else
            {
                cur = it->second;
            }
        }

        // Add leaf node
        auto* leaf = allocNode();
        leaf->name = parts.back();
        leaf->file = gf;
        cur->children[parts.back()] = leaf;
    }

    g_fileTreeDirty = false;
    LOG_INFO << "File tree rebuilt: " << g_fileTreeFileCount << " files matching filter.";
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
    if (!model || skinIndex < 0 || skinIndex >= static_cast<int>(g_skinEntries.size()))
        return;

    const auto& skin = g_skinEntries[skinIndex];
    model->setCreatureGeosetData(skin.creatureGeosetData);
    for (size_t i = 0; i < skin.count; ++i)
    {
        if (skin.tex[i])
            model->updateTextureList(skin.tex[i], skin.base + static_cast<int>(i));
    }
    g_selectedSkin = skinIndex;
}

static WoWModel* getLoadedModel()
{
    if (!g_root) return nullptr;
    auto* att = g_root->children.empty() ? nullptr : g_root->children[0];
    return att ? dynamic_cast<WoWModel*>(att->model()) : nullptr;
}

static void initAnimationControl(WoWModel* model)
{
    g_animEntries.clear();
    g_skinEntries.clear();
    g_selectedAnimCombo = 0;
    g_selectedSkin = -1;
    g_animSpeed = 1.0f;

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
        g_animEntries.push_back(e);

        if (model->anims[i].animID == 0 && standIndex < 0) // ANIM_STAND == 0
            standIndex = static_cast<int>(g_animEntries.size()) - 1;
    }

    if (standIndex >= 0)
        g_selectedAnimCombo = standIndex;

    int useAnim = (standIndex >= 0) ? g_animEntries[standIndex].animIndex : 0;
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
        std::string query = std::format(
            "SELECT TextureVariationFileDataID1, TextureVariationFileDataID2, TextureVariationFileDataID3, "
            "CreatureDisplayInfo.ID FROM CreatureDisplayInfo "
            "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
            "WHERE CreatureModelData.FileDataID = {}", model->gamefile->fileDataId());

        sqlResult r = GAMEDATABASE.sqlQuery(query);
        if (r.valid && !r.empty())
        {
            for (size_t i = 0; i < r.values.size(); ++i)
            {
                SkinEntry se;
                size_t cnt = 0;
                for (size_t s = 0; s < 3; ++s)
                {
                    if (!r.values[i][s].empty() && r.values[i][s] != "0")
                    {
                        se.tex[s] = GAMEDIRECTORY.getFile(core::safeStoi(r.values[i][s]));
                        if (se.tex[s]) ++cnt;
                    }
                }
                if (cnt == 0) continue;
                se.base = TEXTURE_GAMEOBJECT1;
                se.count = cnt;

                int cdi = core::safeStoi(r.values[i][3]);
                std::string q2 = std::format(
                    "SELECT GeosetIndex, GeosetValue FROM CreatureDisplayInfoGeosetData "
                    "WHERE CreatureDisplayInfoID = {}", cdi);
                sqlResult r2 = GAMEDATABASE.sqlQuery(q2);
                if (r2.valid && !r2.empty())
                {
                    for (size_t j = 0; j < r2.values.size(); ++j)
                    {
                        int geoType = 100 * (core::safeStoi(r2.values[j][0]) + 1);
                        int geoId   = core::safeStoi(r2.values[j][1]);
                        if (geoId > 0) se.creatureGeosetData.insert(geoType + geoId);
                    }
                }

                se.label = "Skin " + std::to_string(g_skinEntries.size());
                g_skinEntries.push_back(se);
            }
        }
    }
    else if (isItem)
    {
        std::string query = std::format(
            "SELECT TextureFileData.FileDataID FROM ItemDisplayInfo "
            "LEFT JOIN TextureFileData ON ModelMaterialResourcesID1 = TextureFileData.MaterialResourcesID "
            "LEFT JOIN ModelFileData ON ItemDisplayInfo.ModelResourcesID1 = ModelFileData.ModelResourcesID "
            "WHERE ModelFileData.FileDataID = {}", model->gamefile->fileDataId());

        sqlResult r = GAMEDATABASE.sqlQuery(query);
        if (r.valid && !r.empty())
        {
            for (size_t i = 0; i < r.values.size(); ++i)
            {
                if (r.values[i][0].empty() || r.values[i][0] == "0") continue;
                SkinEntry se;
                se.tex[0] = GAMEDIRECTORY.getFile(core::safeStoi(r.values[i][0]));
                if (!se.tex[0]) continue;
                se.base = TEXTURE_OBJECT_SKIN;
                se.count = 1;
                se.label = "Skin " + std::to_string(g_skinEntries.size());
                g_skinEntries.push_back(se);
            }
        }
    }

    if (!g_skinEntries.empty())
        applySkin(model, 0);
}

static void initCharacterControl(WoWModel* model)
{
    g_customizationOptions.clear();
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

    std::string query = std::format(
        "SELECT ID FROM ChrCustomizationOption WHERE ChrModelID = {} "
        "AND ChrCustomizationID != 0 ORDER BY OrderIndex",
        infos.ChrModelID[0]);

    sqlResult r = GAMEDATABASE.sqlQuery(query);
    if (!r.valid || r.empty())
        return;

    for (size_t i = 0; i < r.values.size(); ++i)
    {
        unsigned int optionID = static_cast<unsigned int>(std::stoul(r.values[i][0]));

        CustomizationOption opt;
        opt.optionID = optionID;

        // Get option name
        std::string nameQ = std::format(
            "SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = {}", optionID);
        sqlResult nameR = GAMEDATABASE.sqlQuery(nameQ);
        if (nameR.valid && !nameR.empty() && !nameR.values[0][0].empty())
            opt.name = nameR.values[0][0];
        else
            opt.name = "Option " + std::to_string(optionID);

        // Get available choices
        std::vector<unsigned int> choiceIDs = cd.getCustomizationChoices(optionID);
        if (choiceIDs.empty())
            continue;

        // Build IN clause for choice names
        std::string inClause;
        for (size_t c = 0; c < choiceIDs.size(); ++c)
        {
            if (c > 0) inClause += ",";
            inClause += std::to_string(choiceIDs[c]);
        }

        std::string choiceQ = std::format(
            "SELECT ID, Name_Lang FROM ChrCustomizationChoice WHERE ID IN ({}) ORDER BY OrderIndex",
            inClause);
        sqlResult choiceR = GAMEDATABASE.sqlQuery(choiceQ);

        if (choiceR.valid && !choiceR.empty())
        {
            // Build ordered lists from DB results
            std::map<unsigned int, std::string> idToName;
            for (size_t j = 0; j < choiceR.values.size(); ++j)
            {
                unsigned int cid = static_cast<unsigned int>(std::stoul(choiceR.values[j][0]));
                std::string cname = choiceR.values[j][1].empty()
                    ? ("Choice " + std::to_string(j))
                    : choiceR.values[j][1];
                idToName[cid] = cname;
            }
            for (unsigned int cid : choiceIDs)
            {
                opt.choiceIDs.push_back(cid);
                auto it = idToName.find(cid);
                opt.choiceNames.push_back(it != idToName.end() ? it->second : ("Choice " + std::to_string(cid)));
            }
        }
        else
        {
            for (unsigned int cid : choiceIDs)
            {
                opt.choiceIDs.push_back(cid);
                opt.choiceNames.push_back("Choice " + std::to_string(cid));
            }
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

        g_customizationOptions.push_back(std::move(opt));
    }
}

// ---- Model Control helpers ------------------------------------------------
static void applyParticleColors(WoWModel* model)
{
    if (!model) return;

    model->particleColorReplacements.clear();
    for (int s = 0; s < 3; ++s)
    {
        std::vector<glm::vec4> pcs;
        for (int p = 0; p < 3; ++p)
        {
            pcs.push_back(glm::vec4(
                g_pcrState.colors[s][p][0],
                g_pcrState.colors[s][p][1],
                g_pcrState.colors[s][p][2],
                1.0f));
        }
        model->particleColorReplacements.push_back(pcs);
    }
    model->replaceParticleColors = true;
}

static void initModelControl(WoWModel* model)
{
    g_geosetGroups.clear();
    g_pcrState = {};

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
            meshToGroupIdx[mesh] = g_geosetGroups.size();
            g_geosetGroups.push_back(std::move(group));
        }

        GeosetEntry ge;
        ge.index = i;
        ge.id = model->geosets[i]->id;
        ge.label = std::format("{} [{}, {}, {}]", i, mesh,
                               model->geosets[i]->id % 100, model->geosets[i]->id);
        g_geosetGroups[meshToGroupIdx[mesh]].geosets.push_back(ge);
    }

    // Detect available particle color replacement IDs
    for (uint pcid : model->replacableParticleColorIDs)
    {
        if (pcid == 11) g_pcrState.hasSet[0] = true;
        else if (pcid == 12) g_pcrState.hasSet[1] = true;
        else if (pcid == 13) g_pcrState.hasSet[2] = true;
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
    g_equipFilteredItems.clear();
    if (g_equipSlotToEdit < 0)
        return;

    std::string search = core::toLower(std::string(g_equipSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0)
            continue;
        if (!correctType(item.type, g_equipSlotToEdit))
            continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        g_equipFilteredItems.push_back(i);
    }
}

// ---- Item Set helpers -----------------------------------------------------
static void buildItemSets()
{
    if (g_itemSetsBuilt)
        return;

    g_itemSets.clear();

    sqlResult r = GAMEDATABASE.sqlQuery("SELECT ID, Name_Lang FROM ItemSet");
    if (r.valid && !r.empty())
    {
        for (const auto& value : r.values)
        {
            ItemSetEntry e;
            e.id = core::safeStoi(value[0]);
            e.name = value[1];
            if (!e.name.empty())
                g_itemSets.push_back(e);
        }
    }

    std::sort(g_itemSets.begin(), g_itemSets.end(),
        [](const ItemSetEntry& a, const ItemSetEntry& b) { return a.name < b.name; });

    g_itemSetsBuilt = true;
    g_itemSetFilterDirty = true;
    LOG_INFO << "Item sets loaded: " << g_itemSets.size();
}

static void rebuildItemSetFilter()
{
    g_itemSetFiltered.clear();

    std::string search = core::toLower(std::string(g_itemSetSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < g_itemSets.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(g_itemSets[i].name, search))
            continue;
        g_itemSetFiltered.push_back(i);
    }

    g_itemSetFilterDirty = false;
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
        g_equipSlotLevels[itemSlot] = 0;
    }
}

// ---- Start Outfit helpers -------------------------------------------------
static void buildStartOutfits(WoWModel* model)
{
    g_startOutfits.clear();
    g_startOutfitsBuilt = false;
    g_startOutfitFilterDirty = true;

    if (!model) return;

    const auto& infos = model->infos;
    if (infos.raceID == -1)
        return;

    const std::string query = std::format(
        "SELECT ChrClasses.Filename, CSO.ID "
        "FROM CharStartOutfit AS CSO LEFT JOIN ChrClasses ON CSO.classID = ChrClasses.ID "
        "WHERE CSO.raceID={} AND CSO.sexID={}", infos.raceID, infos.sexID);

    sqlResult r = GAMEDATABASE.sqlQuery(query);
    if (r.valid && !r.empty())
    {
        for (const auto& value : r.values)
        {
            StartOutfitEntry e;
            e.name = value[0];
            e.id = core::safeStoi(value[1]);
            if (!e.name.empty() && e.id > 0)
                g_startOutfits.push_back(e);
        }
    }

    std::sort(g_startOutfits.begin(), g_startOutfits.end(),
        [](const StartOutfitEntry& a, const StartOutfitEntry& b) { return a.name < b.name; });

    g_startOutfitsBuilt = true;
    g_startOutfitFilterDirty = true;
    LOG_INFO << "Start outfits loaded: " << g_startOutfits.size();
}

static void rebuildStartOutfitFilter()
{
    g_startOutfitFiltered.clear();

    std::string search = core::toLower(std::string(g_startOutfitSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < g_startOutfits.size(); ++i)
    {
        if (!search.empty() && !core::containsIgnoreCase(g_startOutfits[i].name, search))
            continue;
        g_startOutfitFiltered.push_back(i);
    }

    g_startOutfitFilterDirty = false;
}

static void applyStartOutfit(WoWModel* model, int outfitId)
{
    if (!model || outfitId <= 0)
        return;

    const std::string query = std::format(
        "SELECT CSO.iitem1, CSO.iitem2, CSO.iitem3, CSO.iitem4, CSO.iitem5,"
        "CSO.iitem6, CSO.iitem7, CSO.iitem8, CSO.iitem9, CSO.iitem10, CSO.iitem11,"
        "CSO.iitem12, CSO.iitem13, CSO.iitem14, CSO.iitem15, CSO.iitem16, CSO.iitem17, CSO.iitem18,"
        "CSO.iitem19, CSO.iitem20, CSO.iitem21, CSO.iitem22, CSO.iitem23, CSO.iitem24 "
        "FROM CharStartOutfit AS CSO WHERE CSO.ID={}", outfitId);

    sqlResult r = GAMEDATABASE.sqlQuery(query);
    if (!r.valid || r.empty())
    {
        LOG_ERROR << "Start outfit query failed for ID " << outfitId;
        return;
    }

    // Reset all equipped items
    for (const auto it : *model)
        it->setId(0);
    std::memset(g_equipSlotLevels, 0, sizeof(g_equipSlotLevels));

    const size_t cols = r.values[0].size();
    for (unsigned i = 0; i < 24 && i < cols; ++i)
    {
        const auto& val = r.values[0][i];
        if (val.empty() || val == "0")
            continue;
        try
        {
            tryToEquipItem(model, core::safeStoi(val));
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR << "Failed to equip start outfit entry " << i
                      << " (val=\"" << val << "\"): " << ex.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied start outfit ID " << outfitId;
}

static void applyItemSet(WoWModel* model, int setId)
{
    if (!model || setId <= 0)
        return;

    const std::string query = std::format(
        "SELECT ItemID1, ItemID2, ItemID3, ItemID4, ItemID5, "
        "ItemID6, ItemID7, ItemID8 FROM ItemSet WHERE ID = {}", setId);

    sqlResult r = GAMEDATABASE.sqlQuery(query);
    if (!r.valid || r.empty())
    {
        LOG_ERROR << "Item set query failed for ID " << setId;
        return;
    }

    // Reset all equipped items
    for (const auto it : *model)
        it->setId(0);
    std::memset(g_equipSlotLevels, 0, sizeof(g_equipSlotLevels));

    // Equip each item from the set.  WoWItem::setId() may throw
    // std::invalid_argument when downstream DB queries return empty strings
    // for items that lack appearance data — catch per-item to keep going.
    const size_t cols = r.values[0].size();
    for (unsigned i = 0; i < 8 && i < cols; ++i)
    {
        const auto& val = r.values[0][i];
        if (val.empty() || val == "0")
            continue;
        try
        {
            tryToEquipItem(model, core::safeStoi(val));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Failed to equip item set entry " << i
                      << " (val=\"" << val << "\"): " << e.what();
        }
    }

    model->refresh();
    LOG_INFO << "Applied item set ID " << setId;
}

// ---- Clear current model --------------------------------------------------
static void clearModel()
{
    if (g_root)
    {
        g_root->delChildren();
        g_root->setModel(nullptr);
    }

    TEXTUREMANAGER.clear();
    g_isModel = false;
    g_isChar = false;

    g_selModel = nullptr;
    g_animEntries.clear();
    g_skinEntries.clear();
    g_customizationOptions.clear();
    g_geosetGroups.clear();
    g_pcrState = {};
    g_selectedAnimCombo = 0;
    g_selectedSkin = -1;
    g_animSpeed = 1.0f;
    g_autoAnimate = true;
    g_equipSlotToEdit = -1;
    g_equipFilteredItems.clear();
    g_equipSearchBuf[0] = '\0';
    std::memset(g_equipSlotLevels, 0, sizeof(g_equipSlotLevels));
    g_exportAnimChecked.clear();
    g_exportStatus.clear();
    g_isMounted = false;
    g_startOutfits.clear();
    g_startOutfitsBuilt = false;
    g_startOutfitSearchBuf[0] = '\0';
    g_startOutfitFiltered.clear();
    g_startOutfitFilterDirty = true;
}

// ---- Load a model from GameFile (ported from ModelViewer::LoadModel) -------
static void loadModel(GameFile* file)
{
    if (!file || !g_root)
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

    g_root->addChild(model, 0, -1);

    // Determine if this is a character model
    const std::string fn = file->fullname();
    g_isChar = (core::startsWithIgnoreCase(fn, "char") ||
                core::startsWithIgnoreCase(fn, "alternate/char") ||
                core::startsWithIgnoreCase(fn, "alternate\\char"));

    if (g_isChar)
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

    g_isModel = true;

    g_selModel = model;
    initAnimationControl(model);
    initModelControl(model);
    if (g_isChar)
        initCharacterControl(model);

    // Reset camera to frame the model
    g_camera.reset(model);

    LOG_INFO << "Model loaded: " << model->name();
}

// ---- Screenshot (capture FBO to PNG) --------------------------------------
static void captureScreenshot(const char* path)
{
    if (g_fbo.width <= 0 || g_fbo.height <= 0 || !g_fbo.fbo)
    {
        g_screenshotStatus = "No viewport to capture.";
        return;
    }

    const int w = g_fbo.width;
    const int h = g_fbo.height;
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Flip vertically (OpenGL is bottom-up, PNG is top-down)
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < h / 2; ++y)
    {
        unsigned char* top = pixels.data() + y * rowBytes;
        unsigned char* bot = pixels.data() + (h - 1 - y) * rowBytes;
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, row.data(), rowBytes);
    }

    if (stbi_write_png(path, w, h, 4, pixels.data(), static_cast<int>(rowBytes)))
    {
        g_screenshotStatus = std::string("Saved: ") + path;
        LOG_INFO << "Screenshot saved to " << path;
    }
    else
    {
        g_screenshotStatus = std::string("Failed to write: ") + path;
        LOG_ERROR << "Screenshot failed: " << path;
    }
}

// ---- Save/Load Character Preset -------------------------------------------
static void saveCharacterPreset(const char* path)
{
    WoWModel* model = getLoadedModel();
    if (!model || !g_isChar)
    {
        g_presetStatus = "No character model loaded.";
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
    for (const auto& opt : g_customizationOptions)
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
        ini.setValue(key + "_Level", g_equipSlotLevels[s]);
    }

    ini.sync();
    g_presetStatus = std::string("Preset saved: ") + path;
    LOG_INFO << "Character preset saved to " << path;
}

static void loadCharacterPreset(const char* path)
{
    WoWModel* model = getLoadedModel();
    if (!model || !g_isChar)
    {
        g_presetStatus = "No character model loaded.";
        return;
    }

    if (!std::filesystem::exists(path))
    {
        g_presetStatus = std::string("File not found: ") + path;
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
        for (auto& opt : g_customizationOptions)
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
        g_equipSlotLevels[s] = level;
    }

    model->refresh();
    g_presetStatus = std::string("Preset loaded: ") + path;
    LOG_INFO << "Character preset loaded from " << path;
}

// ---- NPC Browser helpers --------------------------------------------------
static void rebuildNpcFilter()
{
    g_npcFiltered.clear();

    std::string search = core::toLower(std::string(g_npcSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& npc = npcs[i];
        if (npc.model == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(npc.name, search))
            continue;
        g_npcFiltered.push_back(i);
    }

    g_npcFilterDirty = false;
}

static void loadNPC(unsigned int creatureID)
{
    std::string query = std::format(
        "SELECT CreatureModelData.FileDataID, CreatureDisplayInfo.TextureVariationFileDataID1, "
        "CreatureDisplayInfo.TextureVariationFileDataID2, CreatureDisplayInfo.TextureVariationFileDataID3, "
        "CreatureDisplayInfo.ExtendedDisplayInfoID, CreatureDisplayInfo.ID FROM Creature "
        "LEFT JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
        "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
        "WHERE Creature.ID = {};", creatureID);

    sqlResult r = GAMEDATABASE.sqlQuery(query);
    if (!r.valid || r.empty())
    {
        LOG_ERROR << "NPC query failed for ID " << creatureID;
        return;
    }

    const int extraId = core::safeStoi(r.values[0][4]);
    if (extraId == 0)
    {
        // Simple NPC — load model directly
        GameFile* file = GAMEDIRECTORY.getFile(core::safeStoi(r.values[0][0]));
        if (!file) return;
        loadModel(file);

        // Apply skin by display ID
        WoWModel* m = getLoadedModel();
        if (m)
        {
            int displayID = core::safeStoi(r.values[0][5]);
            // Find matching skin entry for this displayID
            std::string skinQuery = std::format(
                "SELECT TextureVariationFileDataID1, TextureVariationFileDataID2, TextureVariationFileDataID3 "
                "FROM CreatureDisplayInfo WHERE ID = {}", displayID);
            sqlResult sr = GAMEDATABASE.sqlQuery(skinQuery);
            if (sr.valid && !sr.empty())
            {
                for (size_t i = 0; i < g_skinEntries.size(); ++i)
                {
                    bool match = true;
                    for (size_t t = 0; t < 3 && match; ++t)
                    {
                        if (g_skinEntries[i].tex[t])
                        {
                            int fdid = g_skinEntries[i].tex[t]->fileDataId();
                            if (!sr.values[0][t].empty() && sr.values[0][t] != "0")
                                match = (fdid == core::safeStoi(sr.values[0][t]));
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
    }
    else
    {
        // Character-type NPC
        int fileDataId = core::safeStoi(r.values[0][0]);
        GameFile* file = GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(fileDataId));
        if (!file) return;
        loadModel(file);

        WoWModel* m = getLoadedModel();
        if (!m) return;

        // Apply customization from CreatureDisplayInfoExtra
        query = std::format(
            "SELECT Skin, Face, HairStyle, HairColor, FacialHair FROM CreatureDisplayInfoExtra WHERE ID = {}",
            extraId);
        r = GAMEDATABASE.sqlQuery(query);
        if (r.valid && !r.empty())
        {
            m->cd.set(CharDetails::SKIN_COLOR, core::safeStoi(r.values[0][0]));
            m->cd.set(CharDetails::FACE, core::safeStoi(r.values[0][1]));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, core::safeStoi(r.values[0][2]));
            m->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, core::safeStoi(r.values[0][3]));
            m->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, core::safeStoi(r.values[0][4]));
        }

        // Apply equipment from NpcModelItemSlotDisplayInfo
        query = std::format(
            "SELECT ItemDisplayInfoID, ItemSlot FROM NpcModelItemSlotDisplayInfo WHERE NpcModelID = {}",
            extraId);
        r = GAMEDATABASE.sqlQuery(query);
        if (r.valid && !r.empty())
        {
            static const std::map<int, CharSlots> ItemTypeToInternal = {
                {0, CS_HEAD}, {1, CS_SHOULDER}, {2, CS_SHIRT}, {3, CS_CHEST}, {4, CS_BELT}, {5, CS_PANTS},
                {6, CS_BOOTS}, {7, CS_BRACERS}, {8, CS_GLOVES}, {9, CS_TABARD}, {10, CS_CAPE}
            };
            for (const auto& value : r.values)
            {
                auto it = ItemTypeToInternal.find(core::safeStoi(value[1]));
                if (it != ItemTypeToInternal.end())
                {
                    WoWItem* item = m->getItem(it->second);
                    if (item)
                        item->setDisplayId(core::safeStoi(value[0]));
                }
            }
        }

        m->cd.isNPC = true;
        m->refresh();
    }
}

// ---- Item Browser helpers -------------------------------------------------
static void rebuildItemBrowseFilter()
{
    g_itemBrowseFiltered.clear();

    std::string search = core::toLower(std::string(g_itemBrowseSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    for (size_t i = 0; i < items.items.size(); ++i)
    {
        const auto& item = items.items[i];
        if (item.id == 0) continue;
        if (!search.empty() && !core::containsIgnoreCase(item.name, search))
            continue;
        g_itemBrowseFiltered.push_back(i);
    }

    g_itemBrowseFilterDirty = false;
}

static void loadItemModel(unsigned int itemId)
{
    try
    {
        const std::string query = std::format(
            "SELECT ModelFileData.FileDataID, TextureFileData.FileDataID, ItemDisplayInfo.ID FROM ItemDisplayInfo "
            "LEFT JOIN ModelFileData ON ItemDisplayInfo.ModelResourcesID1 = ModelFileData.ModelResourcesID "
            "LEFT JOIN TextureFileData ON ItemDisplayInfo.ModelMaterialResourcesID1 = TextureFileData.MaterialResourcesID "
            "WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ItemAppearance.ID = "
            "(SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = {}))", itemId);

        sqlResult r = GAMEDATABASE.sqlQuery(query);
        if (!r.valid || r.empty())
        {
            LOG_ERROR << "Item model query failed for ID " << itemId;
            return;
        }

        if (r.values[0][0].empty() || r.values[0][0] == "0")
            return;

        GameFile* file = GAMEDIRECTORY.getFile(core::safeStoi(r.values[0][0]));
        if (!file) return;

        loadModel(file);

        // Apply texture if available
        WoWModel* m = getLoadedModel();
        if (m && !r.values[0][1].empty() && r.values[0][1] != "0")
        {
            GameFile* texFile = GAMEDIRECTORY.getFile(core::safeStoi(r.values[0][1]));
            if (texFile)
                m->updateTextureList(texFile, TEXTURE_OBJECT_SKIN);
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
    if (g_mountListBuilt || !g_isWoWLoaded || !g_initDB)
        return;

    g_mountList.clear();
    g_creatureModels.clear();
    g_creatureModelNames.clear();

    // Player mounts from MountXDisplay DB
    sqlResult r = GAMEDATABASE.sqlQuery(
        "SELECT MountXDisplay.CreatureDisplayInfoID, Mount.Name_Lang "
        "FROM Mount LEFT JOIN MountXDisplay ON Mount.ID = MountXDisplay.MountID");
    if (r.valid && !r.empty())
    {
        for (auto& value : r.values)
        {
            MountEntry me;
            me.displayID = core::safeStoi(value[0]);
            me.name = value[1];
            g_mountList.push_back(me);
        }
        std::sort(g_mountList.begin(), g_mountList.end(),
            [](const MountEntry& a, const MountEntry& b) { return a.name < b.name; });
    }
    LOG_INFO << "Mount list: " << g_mountList.size() << " player mounts.";

    // All creature/*.m2 files
    std::vector<GameFile*> files;
    GAMEDIRECTORY.getFilesForFolder(files, std::string("creature/"), std::string("m2"));
    for (auto* gf : files)
    {
        g_creatureModels.push_back(gf);
        // Remove "creature/" prefix for readability
        std::string n = gf->fullname();
        if (n.size() > 9)
            n = n.substr(9);
        g_creatureModelNames.push_back(n);
    }
    // Sort alphabetically (keeping parallel arrays in sync)
    if (!g_creatureModels.empty())
    {
        std::vector<size_t> indices(g_creatureModels.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return g_creatureModelNames[a] < g_creatureModelNames[b]; });
        std::vector<GameFile*> sortedFiles(g_creatureModels.size());
        std::vector<std::string> sortedNames(g_creatureModelNames.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            sortedFiles[i] = g_creatureModels[indices[i]];
            sortedNames[i] = g_creatureModelNames[indices[i]];
        }
        g_creatureModels = std::move(sortedFiles);
        g_creatureModelNames = std::move(sortedNames);
    }
    LOG_INFO << "Creature models: " << g_creatureModels.size() << " files.";

    g_mountListBuilt = true;
    g_mountFilterDirty = true;
}

static void rebuildMountFilter()
{
    g_mountFiltered.clear();

    std::string search = core::toLower(std::string(g_mountSearchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    if (g_mountTab == 0)
    {
        for (size_t i = 0; i < g_mountList.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(g_mountList[i].name, search))
                continue;
            g_mountFiltered.push_back(i);
        }
    }
    else
    {
        for (size_t i = 0; i < g_creatureModelNames.size(); ++i)
        {
            if (!search.empty() && !core::containsIgnoreCase(g_creatureModelNames[i], search))
                continue;
            g_mountFiltered.push_back(i);
        }
    }

    g_mountFilterDirty = false;
}

static void mountCharacter(int displayID, GameFile* creatureFile)
{
    WoWModel* charModel = getLoadedModel();
    if (!charModel || !g_isChar || !g_root)
        return;

    // Get or resolve the mount model file
    GameFile* modelFile = nullptr;
    int morphID = 0;

    if (displayID > 0)
    {
        // Player mount — lookup model file from CreatureDisplayInfo
        morphID = displayID;
        std::string query = std::format(
            "SELECT CreatureModelData.FileDataID FROM CreatureDisplayInfo "
            "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
            "WHERE CreatureDisplayInfo.ID = {};", displayID);
        sqlResult r = GAMEDATABASE.sqlQuery(query);
        if (!r.valid || r.empty() || r.values[0][0].empty() || r.values[0][0] == "0")
        {
            LOG_ERROR << "Mount display query failed for displayID " << displayID;
            return;
        }
        modelFile = GAMEDIRECTORY.getFile(core::safeStoi(r.values[0][0]));
    }
    else if (creatureFile)
    {
        modelFile = creatureFile;
    }

    if (!modelFile)
        return;

    // Get the character's attachment
    Attachment* charAtt = g_root->children.empty() ? nullptr : g_root->children[0];
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
    g_root->setModel(mountModel);
    charAtt->id = 0; // attachment slot 0 = mount point

    // Apply mount skin/texture if it's a DB mount
    if (morphID > 0)
    {
        std::string texQuery = std::format(
            "SELECT TextureVariationFileDataID1, TextureVariationFileDataID2, TextureVariationFileDataID3 "
            "FROM CreatureDisplayInfo WHERE ID = {}", morphID);
        sqlResult tr = GAMEDATABASE.sqlQuery(texQuery);
        if (tr.valid && !tr.empty())
        {
            for (size_t t = 0; t < 3; ++t)
            {
                if (!tr.values[0][t].empty() && tr.values[0][t] != "0")
                {
                    GameFile* texFile = GAMEDIRECTORY.getFile(core::safeStoi(tr.values[0][t]));
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

    g_isMounted = true;
    g_selModel = mountModel;

    // Update animation control for the mount
    initAnimationControl(mountModel);
    initModelControl(mountModel);

    g_camera.reset(mountModel);
    LOG_INFO << "Character mounted on: " << modelFile->fullname();
}

static void dismountCharacter()
{
    if (!g_isMounted || !g_root || !g_isChar)
        return;

    WoWModel* charModel = nullptr;
    Attachment* charAtt = g_root->children.empty() ? nullptr : g_root->children[0];
    if (charAtt)
        charModel = dynamic_cast<WoWModel*>(charAtt->model());

    // Remove mount model from root
    g_root->setModel(nullptr);
    g_isMounted = false;

    if (charAtt)
        charAtt->id = 0;

    if (charModel)
    {
        charModel->bSheathe = false;
        charModel->scale_ = 1.0f;
        charModel->rot_ = charModel->pos_ = glm::vec3(0.0f);

        g_selModel = charModel;
        initAnimationControl(charModel);
        initModelControl(charModel);
        g_camera.reset(charModel);
    }

    LOG_INFO << "Character dismounted.";
}

// ---- Export helper --------------------------------------------------------
static std::wstring stringToWstring(const std::string& s)
{
    std::wstring ws;
    ws.reserve(s.size());
    for (char c : s)
        ws.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return ws;
}

static void doExport()
{
    WoWModel* model = getLoadedModel();
    if (!model)
    {
        g_exportStatus = "No model loaded.";
        return;
    }

    if (g_selectedExporter < 0 || g_selectedExporter >= static_cast<int>(g_exporters.size()))
    {
        g_exportStatus = "Invalid exporter selection.";
        return;
    }

    ExporterPlugin* exporter = g_exporters[g_selectedExporter];

    // Build file path with appropriate extension
    std::string pathStr{g_exportPath};
    // Extract extension from the exporter filter (e.g. "*.fbx" -> ".fbx")
    std::wstring filter = exporter->fileSaveFilter();
    std::string ext;
    {
        std::string f = wstringToString(filter);
        auto pos = f.find("*.");
        if (pos != std::string::npos)
        {
            auto end = f.find_first_of(";|)", pos);
            ext = f.substr(pos + 1, (end == std::string::npos) ? std::string::npos : end - pos - 1);
        }
    }

    // Append extension if the user hasn't already
    if (!ext.empty() && !core::endsWithIgnoreCase(pathStr, ext))
        pathStr += ext;

    // If exporter supports animation, gather selected anim indices
    if (exporter->canExportAnimation())
    {
        std::vector<int> animsToExport;
        for (size_t i = 0; i < g_exportAnimChecked.size() && i < model->anims.size(); ++i)
        {
            if (g_exportAnimChecked[i])
                animsToExport.push_back(model->anims[i].Index);
        }
        exporter->setAnimationsToExport(animsToExport);
    }

    std::wstring wpath = stringToWstring(pathStr);
    LOG_INFO << "Exporting model to: " << pathStr;

    if (exporter->exportModel(model, wpath))
    {
        g_exportStatus = "Export successful: " + pathStr;
        LOG_INFO << "Export complete: " << pathStr;
    }
    else
    {
        g_exportStatus = "Export failed: " + pathStr;
        LOG_ERROR << "Export failed: " << pathStr;
    }
}

// ---- Default lighting (replaces LightControl / wxWindow) ------------------
static void setupDefaultLighting()
{
    if (!g_light.enabled)
    {
        glDisable(GL_LIGHTING);
        return;
    }

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    GLfloat pos[] = { g_light.direction[0], g_light.direction[1],
                      g_light.direction[2], g_light.direction[3] };

    float i = g_light.intensity;
    GLfloat diffuse[]  = { g_light.diffuse[0]  * i, g_light.diffuse[1]  * i,
                           g_light.diffuse[2]  * i, 1.0f };
    GLfloat ambient[]  = { g_light.ambient[0], g_light.ambient[1],
                           g_light.ambient[2], 1.0f };
    GLfloat specular[] = { g_light.specular[0] * i, g_light.specular[1] * i,
                           g_light.specular[2] * i, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    GLfloat modelAmb[] = { g_light.ambient[0], g_light.ambient[1],
                           g_light.ambient[2], 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, modelAmb);
}

// ---- Checkerboard background ----------------------------------------------
static void createCheckerboardTexture()
{
    // 2x2 checkerboard — two shades of dark gray
    const unsigned char pixels[2 * 2 * 4] = {
        56, 56, 56, 255,   46, 46, 46, 255,
        46, 46, 46, 255,   56, 56, 56, 255,
    };
    glGenTextures(1, &g_checkerTex);
    glBindTexture(GL_TEXTURE_2D, g_checkerTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void renderCheckerboardBackground(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, g_checkerTex);
    glColor3f(1.0f, 1.0f, 1.0f);

    const float tileSize = 16.0f;
    float u = static_cast<float>(w) / (tileSize * 2.0f);
    float v = static_cast<float>(h) / (tileSize * 2.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(u, 0); glVertex2f(static_cast<float>(w), 0);
    glTexCoord2f(u, v); glVertex2f(static_cast<float>(w), static_cast<float>(h));
    glTexCoord2f(0, v); glVertex2f(0, static_cast<float>(h));
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ---- Grid (wireframe with blue center axes) -------------------------------
static void renderGrid()
{
    const float gridSize = 40.0f;
    const float step     = 1.0f;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    // Minor grid lines — thin gray
    glLineWidth(1.0f);
    glColor4f(0.55f, 0.55f, 0.55f, 0.6f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_LINES);
    for (float i = -gridSize; i <= gridSize; i += step)
    {
        if (i == 0.0f) continue; // center axes drawn separately
        glVertex3f(-gridSize, i, 0.0f);
        glVertex3f( gridSize, i, 0.0f);
        glVertex3f(i, -gridSize, 0.0f);
        glVertex3f(i,  gridSize, 0.0f);
    }
    glEnd();

    // Center axis lines — blue
    glLineWidth(2.0f);
    glColor3f(0.2f, 0.5f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-gridSize, 0.0f, 0.0f);
    glVertex3f( gridSize, 0.0f, 0.0f);
    glVertex3f(0.0f, -gridSize, 0.0f);
    glVertex3f(0.0f,  gridSize, 0.0f);
    glEnd();

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

// ---- RenderObjects (ported from ModelCanvas::RenderObjects) ---------------
static void renderObjects()
{
    if (!g_root)
        return;

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    g_root->draw();

    // Particles: rendered after opaque geometry with blending
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    g_root->drawParticles();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// ---- Render scene to FBO --------------------------------------------------
static void renderSceneToFBO(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    g_fbo.resize(w, h);
    g_fbo.bind();

    glViewport(0, 0, w, h);
    glClearColor(g_bgColor.x, g_bgColor.y, g_bgColor.z, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    // Checkerboard background (screen-space, drawn before 3D scene)
    if (g_drawCheckerBg && g_checkerTex)
        renderCheckerboardBackground(w, h);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glm::mat4 proj = glm::perspective(video.fov,
                                       static_cast<float>(w) / static_cast<float>(h),
                                       0.1f, 1280.0f * 5.0f);
    glMultMatrixf(glm::value_ptr(proj));

    // View
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glm::mat4 view = g_camera.getViewMatrix();
    glMultMatrixf(glm::value_ptr(view));

    // Lighting
    setupDefaultLighting();

    // Grid
    if (g_drawGrid)
        renderGrid();

    // Model
    glEnable(GL_NORMALIZE);
    renderObjects();
    glDisable(GL_NORMALIZE);

    g_fbo.unbind();
}

// ---- Handle viewport input (ported from ModelCanvas::OnMouse) -------------
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
        g_camera.setRadius(g_camera.radius() + zoom);
    }

    // Left drag ? orbit (yaw / pitch)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        g_camera.setYawAndPitch(g_camera.yaw() + (-dx), g_camera.pitch() + (-dy));
    }

    // Right drag ? pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul * 0.025f;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul * 0.025f;
        const auto  look  = g_camera.lookAt();
        const auto  right = g_camera.right();
        g_camera.setLookAt(glm::vec3(look.x + right.x * -dx,
                                      look.y + right.y * -dx,
                                      look.z + dy));
    }

    // Middle drag ? zoom (alternative)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        g_camera.setRadius(g_camera.radius() + dy / 10.0f);
    }
}

// ---- Animation tick -------------------------------------------------------
static void tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTick).count();
    g_lastTick = now;

    // Clamp to avoid huge jumps
    if (dt > 0.1f) dt = 0.1f;

    g_animTime += dt;

    if (g_root)
        g_root->tick(dt);
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

    loadSettings();
    // Pre-fill the path input buffer from saved settings
    strncpy_s(g_pathBuf, g_gamePath.c_str(), sizeof(g_pathBuf) - 1);

    // Instantiate exporters (OBJ / FBX)
    g_exporters.push_back(new OBJExporter());
    g_exporters.push_back(new FBXExporter());
}

static void initGL()
{
    video.render = true;
    // video.Init() calls gladLoaderLoadGL() internally — safe after GLFW context
    video.InitGL();

    createCheckerboardTexture();

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

    GLFWwindow* window = glfwCreateWindow(1600, 900, "WoW Model Viewer (ImGui)", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

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

    // Create root attachment (scene graph root — no model yet)
    g_root = new Attachment(nullptr, nullptr, -1, -1);

    // ---- Dear ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = g_imguiIniPath;

    ImGui::StyleColorsDark();

    // ---- DPI-aware font scaling ----
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    const float dpiScale = (xscale > yscale) ? xscale : yscale;
    if (dpiScale > 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(dpiScale);
    }
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = dpiScale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = false;
    bool firstFrame = true;
    g_lastTick = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

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
            if (!std::filesystem::exists(g_imguiIniPath))
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
            ImGui::DockBuilderDockWindow("Settings", dock_left);
            ImGui::DockBuilderDockWindow("3D Viewport", dock_center);
            ImGui::DockBuilderDockWindow("Animation", dock_bottom);
            ImGui::DockBuilderDockWindow("Model Control", dock_bottom);
            ImGui::DockBuilderDockWindow("Screenshot", dock_bottom);
            ImGui::DockBuilderDockWindow("Export", dock_bottom);
            ImGui::DockBuilderDockWindow("Presets", dock_bottom);
            ImGui::DockBuilderDockWindow("Character", dock_right);
            ImGui::DockBuilderDockWindow("Lighting", dock_right);
            ImGui::DockBuilderFinish(dockspace_id);
            }
        }

        // ===== 3D Viewport panel =====
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport"))
        {
            // Determine available size for the viewport image
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);

            if (vpW > 0 && vpH > 0)
            {
                // Render scene to offscreen FBO
                renderSceneToFBO(vpW, vpH);

                // Display FBO colour texture (UV-flipped: OpenGL is bottom-up)
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(g_fbo.colorTex)),
                             panelSize,
                             ImVec2(0, 1), ImVec2(1, 0));

                // Handle orbit camera input when viewport is hovered
                if (ImGui::IsItemHovered())
                    handleViewportInput();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // ===== File Browser panel =====
        if (ImGui::Begin("File Browser"))
        {
            if (!g_isWoWLoaded)
            {
                if (g_loadInProgress)
                {
                    ImGui::Text("Loading...");
                    ImGui::ProgressBar(g_loadProgress, ImVec2(-1, 0));
                    auto status = getLoadStatus();
                    ImGui::TextWrapped("%s", status.c_str());
                }
                else
                {
                    auto status = getLoadStatus();
                    if (!status.empty())
                        ImGui::TextWrapped("%s", status.c_str());
                    else
                        ImGui::TextWrapped("Game not loaded. Use Settings panel to set the WoW path and click Load WoW.");
                }
            }
            else
            {
                // ---- Filter options ----
                ImGui::Text("Filter:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::Combo("##filter", &g_filterMode, g_filterLabels, g_filterCount))
                    g_fileTreeDirty = true;

                ImGui::Text("Search:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##search", g_searchBuf, sizeof(g_searchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    g_fileTreeDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply", ImVec2(-1, 0)))
                    g_fileTreeDirty = true;

                // Rebuild tree when filter changes
                if (g_fileTreeDirty)
                    rebuildFileTree();

                ImGui::Separator();
                ImGui::Text("Files: %d", g_fileTreeFileCount);
                ImGui::Separator();

                // ---- File tree ----
                ImGui::BeginChild("FileTree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
                if (g_fileTreeRoot)
                {
                    // Recursive lambda to draw the tree
                    std::function<void(FileBrowserNode*)> drawNode = [&](FileBrowserNode* node)
                    {
                        for (auto& [name, child] : node->children)
                        {
                            if (child->file)
                            {
                                // Leaf node (file)
                                ImGuiTreeNodeFlags leafFlags =
                                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                    ImGuiTreeNodeFlags_SpanAvailWidth;
                                ImGui::TreeNodeEx(child->name.c_str(), leafFlags);
                                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                                {
                                    loadModel(child->file);
                                }
                            }
                            else
                            {
                                // Directory node
                                bool open = ImGui::TreeNodeEx(child->name.c_str(),
                                    ImGuiTreeNodeFlags_SpanAvailWidth);
                                if (open)
                                {
                                    drawNode(child);
                                    ImGui::TreePop();
                                }
                            }
                        }
                    };
                    drawNode(g_fileTreeRoot);
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();

        // ===== Animation Control =====
        if (ImGui::Begin("Animation"))
        {
            WoWModel* aModel = getLoadedModel();
            if (aModel && !g_animEntries.empty())
            {
                // ---- Animation selector ----
                ImGui::SeparatorText("Animation");
                const char* previewAnim = (g_selectedAnimCombo >= 0 && g_selectedAnimCombo < static_cast<int>(g_animEntries.size()))
                    ? g_animEntries[g_selectedAnimCombo].label.c_str() : "<none>";
                if (ImGui::BeginCombo("##AnimCombo", previewAnim))
                {
                    for (int i = 0; i < static_cast<int>(g_animEntries.size()); ++i)
                    {
                        bool selected = (i == g_selectedAnimCombo);
                        if (ImGui::Selectable(g_animEntries[i].label.c_str(), selected))
                        {
                            g_selectedAnimCombo = i;
                            int idx = g_animEntries[i].animIndex;
                            aModel->currentAnim = idx;
                            aModel->animManager->SetAnim(0, idx, 0);
                            aModel->animManager->Play();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // ---- Playback controls ----
                if (ImGui::Button("Play"))
                    aModel->animManager->Play();
                ImGui::SameLine();
                if (ImGui::Button("Pause"))
                    aModel->animManager->Pause();
                ImGui::SameLine();
                if (ImGui::Button("Stop"))
                    aModel->animManager->Stop();
                ImGui::SameLine();
                if (ImGui::Button("<<"))
                    aModel->animManager->PrevFrame();
                ImGui::SameLine();
                if (ImGui::Button(">>"))
                    aModel->animManager->NextFrame();

                if (aModel->animManager->IsPaused())
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Paused");

                // ---- Speed ----
                if (ImGui::SliderFloat("Speed", &g_animSpeed, 0.0f, 4.0f, "%.2f"))
                    aModel->animManager->SetSpeed(g_animSpeed);

                // ---- Frame scrubber ----
                int frameCount = static_cast<int>(aModel->animManager->GetFrameCount());
                int curFrame = static_cast<int>(aModel->animManager->GetFrame());
                if (frameCount > 0)
                {
                    if (ImGui::SliderInt("Frame", &curFrame, 0, frameCount))
                        aModel->animManager->SetFrame(static_cast<size_t>(curFrame));
                }
                else
                {
                    ImGui::Text("Frame: %d", curFrame);
                }

                // ---- Skin selector ----
                if (!g_skinEntries.empty())
                {
                    ImGui::SeparatorText("Skin / Texture");
                    const char* previewSkin = (g_selectedSkin >= 0 && g_selectedSkin < static_cast<int>(g_skinEntries.size()))
                        ? g_skinEntries[g_selectedSkin].label.c_str() : "<none>";
                    if (ImGui::BeginCombo("##SkinCombo", previewSkin))
                    {
                        for (int i = 0; i < static_cast<int>(g_skinEntries.size()); ++i)
                        {
                            bool selected = (i == g_selectedSkin);
                            if (ImGui::Selectable(g_skinEntries[i].label.c_str(), selected))
                                applySkin(aModel, i);
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No model loaded.");
            }
        }
        ImGui::End();

        // ===== Model Control =====
        if (ImGui::Begin("Model Control"))
        {
            WoWModel* mModel = getLoadedModel();
            if (mModel)
            {
                // ---- Rendering toggles ----
                ImGui::SeparatorText("Display Toggles");
                ImGui::Checkbox("Render", &mModel->showModel);
                ImGui::Checkbox("Wireframe", &mModel->showWireframe);
                ImGui::Checkbox("Texture", &mModel->showTexture);
                ImGui::Checkbox("Bones", &mModel->showBones);
                ImGui::Checkbox("Bounds", &mModel->showBounds);
                ImGui::Checkbox("Particles", &mModel->showParticles);

                // ---- Alpha ----
                ImGui::SeparatorText("Opacity & Scale");
                int alphaPercent = static_cast<int>(mModel->alpha_ * 100.0f);
                if (ImGui::SliderInt("Alpha", &alphaPercent, 0, 100))
                    mModel->alpha_ = alphaPercent / 100.0f;

                // ---- Scale ----
                ImGui::SliderFloat("Scale", &mModel->scale_, 0.1f, 3.0f, "%.2f");

                // ---- Position / Rotation ----
                ImGui::SeparatorText("Position & Rotation");
                ImGui::DragFloat3("Position", &mModel->pos_.x, 0.1f);
                ImGui::DragFloat3("Rotation", &mModel->rot_.x, 1.0f);

                // ---- Geosets ----
                if (!g_geosetGroups.empty())
                {
                    ImGui::SeparatorText("Geosets");
                    ImGui::TextDisabled("Click to toggle on/off");
                    for (auto& group : g_geosetGroups)
                    {
                        if (ImGui::TreeNode(group.name.c_str()))
                        {
                            for (auto& ge : group.geosets)
                            {
                                bool displayed = mModel->isGeosetDisplayed(static_cast<uint>(ge.index));
                                ImVec4 color = displayed
                                    ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                    : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                                ImGui::PushStyleColor(ImGuiCol_Text, color);
                                ImGuiTreeNodeFlags flags =
                                    ImGuiTreeNodeFlags_Leaf |
                                    ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                    ImGuiTreeNodeFlags_SpanAvailWidth;
                                ImGui::TreeNodeEx(ge.label.c_str(), flags);
                                if (ImGui::IsItemClicked())
                                    mModel->showGeoset(static_cast<uint>(ge.index), !displayed);
                                ImGui::PopStyleColor();
                            }
                            ImGui::TreePop();
                        }
                    }
                }

                // ---- Particle Color Replacement ----
                bool anyPCR = g_pcrState.hasSet[0] || g_pcrState.hasSet[1] || g_pcrState.hasSet[2];
                ImGui::SeparatorText("Particle Colors");
                if (anyPCR)
                {
                    if (ImGui::Checkbox("Replace Particle Colors", &g_pcrState.enabled))
                    {
                        if (g_pcrState.enabled)
                        {
                            applyParticleColors(mModel);
                        }
                        else
                        {
                            mModel->replaceParticleColors = false;
                            if (g_selectedSkin >= 0)
                                applySkin(mModel, g_selectedSkin);
                        }
                    }

                    static const char* setNames[] = {"ID 11", "ID 12", "ID 13"};
                    static const char* phaseNames[] = {"Start", "Mid", "End"};
                    for (int s = 0; s < 3; ++s)
                    {
                        if (!g_pcrState.hasSet[s]) continue;
                        ImGui::Text("%s", setNames[s]);
                        for (int p = 0; p < 3; ++p)
                        {
                            std::string label = std::format("{} {}##pcr{}{}",
                                setNames[s], phaseNames[p], s, p);
                            if (ImGui::ColorEdit3(label.c_str(), g_pcrState.colors[s][p]))
                            {
                                if (g_pcrState.enabled)
                                    applyParticleColors(mModel);
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Not available on this model.");
                }
            }
            else
            {
                ImGui::TextDisabled("No model loaded.");
            }
        }
        ImGui::End();

        // ===== Character Control =====
        if (ImGui::Begin("Character"))
        {
            WoWModel* cModel = getLoadedModel();
            if (cModel && g_isChar)
            {
                auto& cd = cModel->cd;

                // ---- Display options ----
                ImGui::SeparatorText("Display");
                bool changed = false;
                changed |= ImGui::Checkbox("Show Underwear", &cd.showUnderwear);
                changed |= ImGui::Checkbox("Show Hair", &cd.showHair);
                changed |= ImGui::Checkbox("Show Facial Hair", &cd.showFacialHair);
                changed |= ImGui::Checkbox("Show Ears", &cd.showEars);
                changed |= ImGui::Checkbox("Show Feet", &cd.showFeet);
                changed |= ImGui::Checkbox("Auto-hide Geosets for Head Items", &cd.autoHideGeosetsForHeadItems);
                changed |= ImGui::Checkbox("Sheathe Weapons", &cModel->bSheathe);

                // ---- Eye glow ----
                ImGui::SeparatorText("Eye Glow");
                int eyeGlow = static_cast<int>(cd.eyeGlowType);
                if (ImGui::RadioButton("None", &eyeGlow, EGT_NONE)) changed = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Default", &eyeGlow, EGT_DEFAULT)) changed = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Death Knight", &eyeGlow, EGT_DEATHKNIGHT)) changed = true;
                cd.eyeGlowType = static_cast<EyeGlowTypes>(eyeGlow);

                if (changed)
                    cModel->refresh();

                // ---- Customization options ----
                if (!g_customizationOptions.empty())
                {
                    ImGui::SeparatorText("Customization");
                    for (auto& opt : g_customizationOptions)
                    {
                        if (opt.choiceNames.empty()) continue;
                        const char* preview = (opt.selectedIndex >= 0 && opt.selectedIndex < static_cast<int>(opt.choiceNames.size()))
                            ? opt.choiceNames[opt.selectedIndex].c_str() : "<none>";
                        if (ImGui::BeginCombo(opt.name.c_str(), preview))
                        {
                            for (int c = 0; c < static_cast<int>(opt.choiceNames.size()); ++c)
                            {
                                bool sel = (c == opt.selectedIndex);
                                if (ImGui::Selectable(opt.choiceNames[c].c_str(), sel))
                                {
                                    opt.selectedIndex = c;
                                    cd.set(opt.optionID, opt.choiceIDs[c]);
                                    cModel->refresh();
                                }
                                if (sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }

                // ---- Equipment ----
                ImGui::SeparatorText("Equipment");
                static const char* slotNames[] = {
                    "Head", "Shoulder", "Boots", "Belt", "Shirt", "Pants",
                    "Chest", "Bracers", "Gloves", "Right Hand", "Left Hand",
                    "Cape", "Tabard", "Quiver"
                };

                bool openEquipPopup = false;
                for (int s = 0; s < NUM_CHAR_SLOTS; ++s)
                {
                    WoWItem* witem = cModel->getItem(static_cast<CharSlots>(s));
                    ImGui::PushID(s);

                    if (ImGui::Button(slotNames[s], ImVec2(90, 0)))
                    {
                        g_equipSlotToEdit = s;
                        g_equipSearchBuf[0] = '\0';
                        g_equipPopupJustOpened = true;
                        rebuildEquipFilteredItems();
                        openEquipPopup = true;
                    }

                    ImGui::SameLine();

                    if (witem && witem->id() > 0)
                    {
                        ImVec4 qcol = getQualityColor(witem->quality());
                        ImGui::TextColored(qcol, "%s (%d)", witem->name().c_str(), witem->id());

                        if (witem->nbLevels() > 1)
                        {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(60);
                            int maxLvl = static_cast<int>(witem->nbLevels()) - 1;
                            if (ImGui::SliderInt("##Lvl", &g_equipSlotLevels[s], 0, maxLvl))
                            {
                                witem->setLevel(g_equipSlotLevels[s]);
                                cModel->refresh();
                            }
                        }

                        ImGui::SameLine();
                        if (ImGui::SmallButton("X"))
                        {
                            witem->setId(0);
                            g_equipSlotLevels[s] = 0;
                            cModel->refresh();
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("empty");
                    }

                    ImGui::PopID();
                }

                if (openEquipPopup)
                    ImGui::OpenPopup("Select Item##EquipModal");

                if (ImGui::BeginPopupModal("Select Item##EquipModal", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize))
                {
                    if (g_equipSlotToEdit >= 0 && g_equipSlotToEdit < NUM_CHAR_SLOTS)
                    {
                        ImGui::Text("Equip to: %s", slotNames[g_equipSlotToEdit]);
                        ImGui::Separator();

                        if (g_equipPopupJustOpened)
                        {
                            ImGui::SetKeyboardFocusHere();
                            g_equipPopupJustOpened = false;
                        }

                        ImGui::SetNextItemWidth(400);
                        if (ImGui::InputText("##EquipSearch", g_equipSearchBuf,
                            sizeof(g_equipSearchBuf)))
                            rebuildEquipFilteredItems();

                        ImGui::Text("%d items found",
                            static_cast<int>(g_equipFilteredItems.size()));
                        ImGui::Separator();

                        ImGui::BeginChild("##EquipItemList", ImVec2(420, 350),
                            ImGuiChildFlags_Borders);
                        ImGuiListClipper clipper;
                        clipper.Begin(
                            static_cast<int>(g_equipFilteredItems.size()));
                        while (clipper.Step())
                        {
                            for (int i = clipper.DisplayStart;
                                 i < clipper.DisplayEnd; ++i)
                            {
                                const auto& rec =
                                    items.items[g_equipFilteredItems[i]];
                                ImGui::PushID(i);
                                ImVec4 qcol = getQualityColor(rec.quality);
                                ImGui::PushStyleColor(ImGuiCol_Text, qcol);
                                std::string label = std::format(
                                    "{} ({})", rec.name, rec.id);
                                if (ImGui::Selectable(label.c_str()))
                                {
                                    WoWItem* targetItem = cModel->getItem(
                                        static_cast<CharSlots>(
                                            g_equipSlotToEdit));
                                    if (targetItem)
                                    {
                                        targetItem->setId(rec.id);
                                        g_equipSlotLevels[
                                            g_equipSlotToEdit] = 0;
                                        cModel->refresh();
                                    }
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::PopStyleColor();
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();

                        ImGui::Separator();
                        if (ImGui::Button("Cancel", ImVec2(120, 0)))
                            ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // ---- Item Sets ----
                ImGui::SeparatorText("Item Sets");

                buildItemSets(); // lazy init on first frame

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##itemSetSearch", g_itemSetSearchBuf, sizeof(g_itemSetSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    g_itemSetFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##itemset", ImVec2(-1, 0)))
                    g_itemSetFilterDirty = true;

                if (g_itemSetFilterDirty)
                    rebuildItemSetFilter();

                ImGui::Text("%d sets", static_cast<int>(g_itemSetFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##ItemSetList", ImVec2(0, 200), ImGuiChildFlags_Borders);
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(g_itemSetFiltered.size()));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        {
                            const auto& setEntry = g_itemSets[g_itemSetFiltered[i]];
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

                if (!g_startOutfitsBuilt)
                    buildStartOutfits(cModel);

                if (g_startOutfits.empty())
                {
                    ImGui::TextDisabled("No start outfits available for this race/sex.");
                }
                else
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                    if (ImGui::InputText("##startOutfitSearch", g_startOutfitSearchBuf, sizeof(g_startOutfitSearchBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                        g_startOutfitFilterDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Apply##startoutfit", ImVec2(-1, 0)))
                        g_startOutfitFilterDirty = true;

                    if (g_startOutfitFilterDirty)
                        rebuildStartOutfitFilter();

                    ImGui::Text("%d classes", static_cast<int>(g_startOutfitFiltered.size()));
                    ImGui::Separator();

                    ImGui::BeginChild("##StartOutfitList", ImVec2(0, 150), ImGuiChildFlags_Borders);
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(g_startOutfitFiltered.size()));
                        while (clipper.Step())
                        {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                            {
                                const auto& entry = g_startOutfits[g_startOutfitFiltered[i]];
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

                // ---- Mount ----
                ImGui::SeparatorText("Mount");

                buildMountList(); // lazy init on first frame

                if (g_isMounted)
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mounted");
                    if (ImGui::Button("Dismount", ImVec2(-1, 0)))
                        dismountCharacter();
                }
                else
                {
                    if (ImGui::BeginTabBar("##MountTabs"))
                    {
                        int prevTab = g_mountTab;

                        if (ImGui::BeginTabItem("Player Mounts"))
                        {
                            g_mountTab = 0;
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Creature Models"))
                        {
                            g_mountTab = 1;
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();

                        if (g_mountTab != prevTab)
                        {
                            g_mountFilterDirty = true;
                            g_mountSearchBuf[0] = '\0';
                        }
                    }

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                    if (ImGui::InputText("##mountSearch", g_mountSearchBuf, sizeof(g_mountSearchBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                        g_mountFilterDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Apply##mount", ImVec2(-1, 0)))
                        g_mountFilterDirty = true;

                    if (g_mountFilterDirty)
                        rebuildMountFilter();

                    ImGui::Text("%d entries", static_cast<int>(g_mountFiltered.size()));
                    ImGui::Separator();

                    ImGui::BeginChild("##MountList", ImVec2(0, 200), ImGuiChildFlags_Borders);
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(g_mountFiltered.size()));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        {
                            size_t idx = g_mountFiltered[i];
                            ImGui::PushID(static_cast<int>(idx));

                            if (g_mountTab == 0)
                            {
                                const auto& me = g_mountList[idx];
                                std::string label = std::format("{} (DisplayID:{})", me.name, me.displayID);
                                if (ImGui::Selectable(label.c_str()))
                                    mountCharacter(me.displayID, nullptr);
                            }
                            else
                            {
                                if (ImGui::Selectable(g_creatureModelNames[idx].c_str()))
                                    mountCharacter(-1, g_creatureModels[idx]);
                            }

                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
            }
            else if (g_isModel)
            {
                ImGui::TextDisabled("Not a character model.");
            }
            else
            {
                ImGui::TextDisabled("No model loaded.");
            }
        }
        ImGui::End();

        // ===== Lighting panel =====
        if (ImGui::Begin("Lighting"))
        {
            ImGui::Checkbox("Enable Lighting", &g_light.enabled);

            if (!g_light.enabled)
                ImGui::BeginDisabled();

            ImGui::SeparatorText("Direction");
            ImGui::DragFloat3("Dir XYZ", g_light.direction, 0.01f, -5.0f, 5.0f, "%.2f");

            ImGui::SeparatorText("Colors");
            ImGui::ColorEdit3("Diffuse",  g_light.diffuse,  ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Ambient",  g_light.ambient,  ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Specular", g_light.specular, ImGuiColorEditFlags_Float);

            ImGui::SeparatorText("Intensity");
            ImGui::SliderFloat("##Intensity", &g_light.intensity, 0.0f, 3.0f, "%.2f");

            if (!g_light.enabled)
                ImGui::EndDisabled();

            ImGui::SeparatorText("Presets");
            if (ImGui::Button("Default", ImVec2(-1, 0)))
            {
                g_light = LightSettings{};
            }
            if (ImGui::Button("Bright Daylight", ImVec2(-1, 0)))
            {
                g_light.direction[0] = -0.5f; g_light.direction[1] = 1.0f;
                g_light.direction[2] = -0.3f; g_light.direction[3] = 0.0f;
                g_light.diffuse[0] = 1.0f; g_light.diffuse[1] = 0.98f; g_light.diffuse[2] = 0.92f;
                g_light.ambient[0] = 0.45f; g_light.ambient[1] = 0.45f; g_light.ambient[2] = 0.50f;
                g_light.specular[0] = 0.3f; g_light.specular[1] = 0.3f; g_light.specular[2] = 0.3f;
                g_light.intensity = 1.2f;
                g_light.enabled = true;
            }
            if (ImGui::Button("Warm Sunset", ImVec2(-1, 0)))
            {
                g_light.direction[0] = -1.0f; g_light.direction[1] = 0.3f;
                g_light.direction[2] = -0.5f; g_light.direction[3] = 0.0f;
                g_light.diffuse[0] = 1.0f; g_light.diffuse[1] = 0.65f; g_light.diffuse[2] = 0.3f;
                g_light.ambient[0] = 0.25f; g_light.ambient[1] = 0.2f; g_light.ambient[2] = 0.25f;
                g_light.specular[0] = 0.1f; g_light.specular[1] = 0.05f; g_light.specular[2] = 0.0f;
                g_light.intensity = 1.0f;
                g_light.enabled = true;
            }
            if (ImGui::Button("Cool Moonlight", ImVec2(-1, 0)))
            {
                g_light.direction[0] = 0.3f; g_light.direction[1] = 1.0f;
                g_light.direction[2] = -0.7f; g_light.direction[3] = 0.0f;
                g_light.diffuse[0] = 0.6f; g_light.diffuse[1] = 0.65f; g_light.diffuse[2] = 0.8f;
                g_light.ambient[0] = 0.15f; g_light.ambient[1] = 0.15f; g_light.ambient[2] = 0.2f;
                g_light.specular[0] = 0.0f; g_light.specular[1] = 0.0f; g_light.specular[2] = 0.0f;
                g_light.intensity = 0.7f;
                g_light.enabled = true;
            }
            if (ImGui::Button("Flat (No Shading)", ImVec2(-1, 0)))
            {
                g_light.direction[0] = 0.0f; g_light.direction[1] = 0.0f;
                g_light.direction[2] = -1.0f; g_light.direction[3] = 0.0f;
                g_light.diffuse[0] = 1.0f; g_light.diffuse[1] = 1.0f; g_light.diffuse[2] = 1.0f;
                g_light.ambient[0] = 1.0f; g_light.ambient[1] = 1.0f; g_light.ambient[2] = 1.0f;
                g_light.specular[0] = 0.0f; g_light.specular[1] = 0.0f; g_light.specular[2] = 0.0f;
                g_light.intensity = 0.5f;
                g_light.enabled = true;
            }
        }
        ImGui::End();

        // ===== NPC Browser panel =====
        if (ImGui::Begin("NPC Browser"))
        {
            if (!g_isWoWLoaded || !g_initDB)
            {
                ImGui::TextDisabled("Game not loaded.");
            }
            else
            {
                ImGui::Text("Search:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##npcSearch", g_npcSearchBuf, sizeof(g_npcSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    g_npcFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##npc", ImVec2(-1, 0)))
                    g_npcFilterDirty = true;

                if (g_npcFilterDirty)
                    rebuildNpcFilter();

                ImGui::Text("%d NPCs", static_cast<int>(g_npcFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##NpcList", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(g_npcFiltered.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& npc = npcs[g_npcFiltered[i]];
                        ImGui::PushID(static_cast<int>(g_npcFiltered[i]));
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

        // ===== Item Browser panel =====
        if (ImGui::Begin("Item Browser"))
        {
            if (!g_isWoWLoaded || !g_initDB)
            {
                ImGui::TextDisabled("Game not loaded.");
            }
            else
            {
                ImGui::Text("Search:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::InputText("##itemBrowseSearch", g_itemBrowseSearchBuf, sizeof(g_itemBrowseSearchBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    g_itemBrowseFilterDirty = true;
                ImGui::SameLine();
                if (ImGui::Button("Apply##itembrowse", ImVec2(-1, 0)))
                    g_itemBrowseFilterDirty = true;

                if (g_itemBrowseFilterDirty)
                    rebuildItemBrowseFilter();

                ImGui::Text("%d items", static_cast<int>(g_itemBrowseFiltered.size()));
                ImGui::Separator();

                ImGui::BeginChild("##ItemBrowseList", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(g_itemBrowseFiltered.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& item = items.items[g_itemBrowseFiltered[i]];
                        ImGui::PushID(static_cast<int>(g_itemBrowseFiltered[i]));
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

        // ===== Export panel =====
        if (ImGui::Begin("Export"))
        {
            WoWModel* eModel = getLoadedModel();
            if (eModel)
            {
                // ---- Format selector ----
                ImGui::SeparatorText("Format");
                if (!g_exporters.empty())
                {
                    for (int i = 0; i < static_cast<int>(g_exporters.size()); ++i)
                    {
                        std::string label = wstringToString(g_exporters[i]->menuLabel());
                        ImGui::RadioButton(label.c_str(), &g_selectedExporter, i);
                        if (i < static_cast<int>(g_exporters.size()) - 1)
                            ImGui::SameLine();
                    }
                }

                // ---- Output path ----
                ImGui::SeparatorText("Output");
                ImGui::Text("File Path:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##exportPath", g_exportPath, sizeof(g_exportPath));

                // ---- Animation selection (only for exporters that support it) ----
                bool canAnim = (g_selectedExporter >= 0 &&
                                g_selectedExporter < static_cast<int>(g_exporters.size()) &&
                                g_exporters[g_selectedExporter]->canExportAnimation());

                if (canAnim && !g_animEntries.empty())
                {
                    ImGui::SeparatorText("Animations");

                    // Ensure checkbox vector is sized to match
                    if (g_exportAnimChecked.size() != g_animEntries.size())
                    {
                        g_exportAnimChecked.assign(g_animEntries.size(), 1);
                    }

                    if (ImGui::Button("Select All"))
                        std::fill(g_exportAnimChecked.begin(), g_exportAnimChecked.end(), static_cast<char>(1));
                    ImGui::SameLine();
                    if (ImGui::Button("Select None"))
                        std::fill(g_exportAnimChecked.begin(), g_exportAnimChecked.end(), static_cast<char>(0));

                    int checkedCount = 0;
                    for (char b : g_exportAnimChecked) if (b) ++checkedCount;
                    ImGui::Text("%d / %d selected", checkedCount, static_cast<int>(g_animEntries.size()));

                    ImGui::BeginChild("##AnimExportList", ImVec2(0, 200), ImGuiChildFlags_Borders);
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(g_animEntries.size()));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        {
                            ImGui::PushID(i);
                            bool checked = g_exportAnimChecked[i] != 0;
                            if (ImGui::Checkbox(g_animEntries[i].label.c_str(), &checked))
                                g_exportAnimChecked[i] = checked ? 1 : 0;
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
                else if (canAnim)
                {
                    ImGui::SeparatorText("Animations");
                    ImGui::TextDisabled("No animations on current model.");
                }

                // ---- Export button ----
                ImGui::Separator();
                if (ImGui::Button("Export", ImVec2(-1, 0)))
                    doExport();

                // ---- Status ----
                if (!g_exportStatus.empty())
                {
                    bool isError = g_exportStatus.find("failed") != std::string::npos ||
                                   g_exportStatus.find("No ") != std::string::npos ||
                                   g_exportStatus.find("Invalid") != std::string::npos;
                    if (isError)
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_exportStatus.c_str());
                    else
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g_exportStatus.c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("No model loaded.");
            }
        }
        ImGui::End();

        // ===== Screenshot panel =====
        if (ImGui::Begin("Screenshot"))
        {
            ImGui::SeparatorText("Capture Viewport");
            ImGui::Text("Output File:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##screenshotPath", g_screenshotPath, sizeof(g_screenshotPath));

            if (ImGui::Button("Save Screenshot", ImVec2(-1, 0)))
                captureScreenshot(g_screenshotPath);

            if (!g_screenshotStatus.empty())
            {
                bool isError = g_screenshotStatus.find("Failed") != std::string::npos ||
                               g_screenshotStatus.find("No ") != std::string::npos;
                if (isError)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_screenshotStatus.c_str());
                else
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g_screenshotStatus.c_str());
            }

            ImGui::SeparatorText("Quick Sizes");
            if (ImGui::Button("1x (viewport)", ImVec2(-1, 0)))
            {
                captureScreenshot(g_screenshotPath);
            }
            ImGui::TextDisabled("Captures at current viewport resolution (%dx%d).",
                                g_fbo.width, g_fbo.height);
        }
        ImGui::End();

        // ===== Character Preset panel =====
        if (ImGui::Begin("Presets"))
        {
            ImGui::SeparatorText("Character Preset");
            ImGui::Text("File:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##presetPath", g_presetPath, sizeof(g_presetPath));

            {
                bool canSave = g_isChar && getLoadedModel() != nullptr;
                if (!canSave) ImGui::BeginDisabled();

                if (ImGui::Button("Save Preset", ImVec2(-1, 0)))
                    saveCharacterPreset(g_presetPath);

                if (!canSave) ImGui::EndDisabled();
            }

            {
                bool canLoad = g_isChar && getLoadedModel() != nullptr;
                if (!canLoad) ImGui::BeginDisabled();

                if (ImGui::Button("Load Preset", ImVec2(-1, 0)))
                    loadCharacterPreset(g_presetPath);

                if (!canLoad) ImGui::EndDisabled();
            }

            if (!g_presetStatus.empty())
            {
                bool isError = g_presetStatus.find("not found") != std::string::npos ||
                               g_presetStatus.find("No ") != std::string::npos;
                if (isError)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_presetStatus.c_str());
                else
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", g_presetStatus.c_str());
            }

            if (!g_isChar)
                ImGui::TextDisabled("Load a character model first.");
        }
        ImGui::End();

        // ===== Settings panel =====
        if (ImGui::Begin("Settings"))
        {
            // ---- Game loading section ----
            ImGui::SeparatorText("World of Warcraft");

            ImGui::Text("Game Data Path:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##gamepath", g_pathBuf, sizeof(g_pathBuf));

            if (g_isWoWLoaded)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded: %s (%s)",
                                   GAMEDIRECTORY.version().c_str(),
                                   GAMEDIRECTORY.locale().c_str());
            }
            else
            {
                bool canLoad = !g_loadInProgress;
                if (!canLoad)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Load WoW", ImVec2(-1, 0)))
                {
                    g_gamePath = g_pathBuf;
                    beginLoadWoW();
                }

                if (!canLoad)
                    ImGui::EndDisabled();

                if (g_loadInProgress)
                {
                    ImGui::ProgressBar(g_loadProgress);
                    auto status = getLoadStatus();
                    ImGui::TextWrapped("%s", status.c_str());
                }
                else
                {
                    auto status = getLoadStatus();
                    if (!status.empty())
                        ImGui::TextWrapped("%s", status.c_str());
                }
            }

            ImGui::Checkbox("Enable Database Cache", &g_enableDbCache);
            ImGui::TextDisabled("Speeds up loading by caching the database. Takes effect on next load.");

            // ---- Viewport section ----
            ImGui::SeparatorText("Viewport");
            ImGui::Checkbox("Draw Grid", &g_drawGrid);
                    ImGui::Checkbox("Checkerboard Background", &g_drawCheckerBg);
                    ImGui::ColorEdit3("Background", &g_bgColor.x);
            if (ImGui::Button("Reset Camera"))
                g_camera.reset();
            ImGui::Separator();
            ImGui::Checkbox("ImGui Demo Window", &show_demo_window);
            ImGui::Separator();
            ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                        g_camera.yaw(), g_camera.pitch(), g_camera.radius());
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));

            // ---- Save ----
            ImGui::SeparatorText("Save");
            if (ImGui::Button("Save Settings", ImVec2(-1, 0)))
            {
                g_gamePath = g_pathBuf;
                saveSettings();
            }
            ImGui::TextDisabled("Saves preferences and UI layout.");
        }
        ImGui::End();

        // ===== Config selection modal (multiple WoW installs) =====
        if (g_showConfigPopup)
            ImGui::OpenPopup("Select WoW Config");

        if (ImGui::BeginPopupModal("Select WoW Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Multiple configurations found. Please select one:");
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(g_pendingConfigs.size()); ++i)
            {
                std::string label = g_pendingConfigs[i].locale + " - " + g_pendingConfigs[i].product;
                if (!g_pendingConfigs[i].version.empty())
                    label += " (" + g_pendingConfigs[i].version + ")";
                ImGui::RadioButton(label.c_str(), &g_selectedConfig, i);
            }

            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                g_showConfigPopup = false;
                ImGui::CloseCurrentPopup();
                launchLoadThread(g_pendingConfigs[g_selectedConfig]);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                g_showConfigPopup = false;
                setLoadStatus("Load cancelled.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

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
    if (g_loadThread.joinable())
        g_loadThread.join();

    freeNodePool();
    g_fileTreeRoot = nullptr;

    if (g_root)
    {
        g_root->delChildren();
        delete g_root;
        g_root = nullptr;
    }

    g_fbo.destroy();

    if (g_checkerTex) { glDeleteTextures(1, &g_checkerTex); g_checkerTex = 0; }

    for (auto* e : g_exporters)
        delete e;
    g_exporters.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer (ImGui) shutdown complete.";
    return 0;
}
