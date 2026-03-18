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
static glm::vec3    g_bgColor(71.0f / 255.0f, 95.0f / 255.0f, 121.0f / 255.0f);

// Timing for animation tick
static float        g_animTime   = 0.0f;
static std::chrono::steady_clock::time_point g_lastTick;

// ---- Game loading state (Phase 2) -----------------------------------------
static std::string  g_gamePath;              // WoW Data folder path
static std::string  g_cfgPath;               // userSettings/Config.ini
static bool         g_isWoWLoaded  = false;
static bool         g_initDB       = false;
static std::string  g_loadStatus;            // status text shown in File Browser
static float        g_loadProgress = 0.0f;   // 0..1 progress bar fraction
static bool         g_loadInProgress = false;

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
static void loadSettings()
{
    g_cfgPath = "userSettings/Config.ini";
    const core::IniFile config(g_cfgPath);

    g_gamePath = config.getString("Settings/Path");

    LOG_INFO << "Settings loaded. Game path:" << g_gamePath;
}

static void saveSettings()
{
    core::IniFile config(g_cfgPath);
    config.setValue("Settings/Path", g_gamePath);
    config.sync();
    LOG_INFO << "Settings saved.";
}

// ---- Support-file download (listfile.csv, extraEncryptionKeys.csv) --------
static bool downloadFile(const std::string& url, const std::filesystem::path& dest,
                          const std::string& label, bool replaceSeparators = false)
{
    LOG_INFO << "Downloading " << label << "...";
    g_loadStatus = "Downloading " + label + "...";

    const auto resp = HttpClient::Get(url);
    if (!resp.success)
    {
        LOG_ERROR << "Failed to download " << label << ": " << resp.error;
        g_loadStatus = "Failed to download " + label + ": " + resp.error;
        return false;
    }

    std::string content = resp.body;
    if (replaceSeparators)
        std::replace(content.begin(), content.end(), ' ', ';');

    std::ofstream file(dest, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR << "Failed to write " << label << " to " << dest.string();
        g_loadStatus = "Failed to write " + label;
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
    g_loadStatus = "Initializing database...";

    if (!GAMEDATABASE.initFromXML("database.xml"))
    {
        g_initDB = false;
        LOG_ERROR << "Database initialization failed!";
        g_loadStatus = "Database initialization failed!";
        return;
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
// Called once the user has set g_gamePath (and optionally chosen a config).
// This runs synchronously (blocks the frame), which is acceptable for now.
static void loadWoW(const core::GameConfig& config)
{
    g_loadInProgress = true;
    g_loadProgress = 0.0f;
    g_loadStatus = "Opening CASC storage...";

    if (!GAMEDIRECTORY.setConfig(config))
    {
        LOG_ERROR << "Could not load WoW Data folder (error "
                  << GAMEDIRECTORY.lastError() << ").";
        g_loadStatus = "Failed to open CASC storage (error "
                       + std::to_string(GAMEDIRECTORY.lastError()) + ").";
        g_loadInProgress = false;
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
    g_loadStatus = "Loading file list...";
    g_loadProgress = 0.10f;
    GAMEDIRECTORY.setProgressCallback([](int current, int total) {
        if (total > 0)
            g_loadProgress = 0.10f + 0.40f * static_cast<float>(current) / static_cast<float>(total);
    });
    GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
    GAMEDIRECTORY.setProgressCallback(nullptr);
    g_loadProgress = 0.50f;

    // Init database
    g_loadStatus = "Initializing database...";
    g_loadProgress = 0.55f;
    initDatabase();

    if (!g_initDB)
    {
        g_loadInProgress = false;
        return;
    }

    g_loadProgress = 1.0f;
    g_loadStatus = "World of Warcraft loaded successfully.";
    g_isWoWLoaded = true;
    g_loadInProgress = false;
    g_fileTreeDirty = true; // trigger initial file tree build

    LOG_INFO << "World of Warcraft loaded successfully. Version: "
             << GAMEDIRECTORY.version() << " Locale: " << GAMEDIRECTORY.locale();

    saveSettings();
}

// Called when the user clicks "Load WoW" — kicks off the full init sequence.
static void beginLoadWoW()
{
    if (g_isWoWLoaded || g_loadInProgress)
        return;

    g_loadInProgress = true;
    g_loadProgress = 0.0f;
    g_loadStatus = "Checking support files...";

    if (!checkAndDownloadSupportFiles())
    {
        g_loadInProgress = false;
        return;
    }

    // Validate game path
    namespace fs = std::filesystem;
    std::string path = g_gamePath;
    if (path.empty() || !fs::is_directory(path))
    {
        g_loadStatus = "Please set a valid WoW Data folder path above.";
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
        g_loadStatus = "No locale found in the WoW folder.";
        g_loadInProgress = false;
        return;
    }

    if (g_pendingConfigs.size() == 1)
    {
        // Only one config — load immediately
        loadWoW(g_pendingConfigs[0]);
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
                        se.tex[s] = GAMEDIRECTORY.getFile(std::stoi(r.values[i][s]));
                        if (se.tex[s]) ++cnt;
                    }
                }
                if (cnt == 0) continue;
                se.base = TEXTURE_GAMEOBJECT1;
                se.count = cnt;

                int cdi = std::stoi(r.values[i][3]);
                std::string q2 = std::format(
                    "SELECT GeosetIndex, GeosetValue FROM CreatureDisplayInfoGeosetData "
                    "WHERE CreatureDisplayInfoID = {}", cdi);
                sqlResult r2 = GAMEDATABASE.sqlQuery(q2);
                if (r2.valid && !r2.empty())
                {
                    for (size_t j = 0; j < r2.values.size(); ++j)
                    {
                        int geoType = 100 * (std::stoi(r2.values[j][0]) + 1);
                        int geoId   = std::stoi(r2.values[j][1]);
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
                se.tex[0] = GAMEDIRECTORY.getFile(std::stoi(r.values[i][0]));
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

// ---- Default lighting (replaces LightControl / wxWindow) ------------------
static void setupDefaultLighting()
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // A simple directional light from upper-right-front
    GLfloat pos[]     = { -1.0f, 1.0f, -1.0f, 0.0f };
    GLfloat diffuse[] = {  1.0f, 1.0f,  1.0f, 1.0f };
    GLfloat ambient[] = {  0.35f, 0.35f, 0.35f, 1.0f };
    GLfloat specular[]= {  0.0f, 0.0f,  0.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    GLfloat modelAmb[] = { 0.35f, 0.35f, 0.35f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, modelAmb);
}

// ---- Grid (ported from ModelCanvas::RenderGrid) ---------------------------
static void renderGrid()
{
    int count = 0;
    const GLfloat white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const GLfloat black[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    glBegin(GL_QUADS);
    for (int i = -20; i <= 20; ++i)
    {
        for (int j = -20; j <= 20; ++j)
        {
            if ((count % 2) == 0)
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
                glColor3f(1.0f, 1.0f, 1.0f);
            }
            else
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
                glColor3f(0.2f, 0.2f, 0.2f);
            }

            glNormal3f(0, 0, 1);
            glVertex3f(static_cast<float>(j),     static_cast<float>(i),     0.0f);
            glVertex3f(static_cast<float>(j),     static_cast<float>(i + 1), 0.0f);
            glVertex3f(static_cast<float>(j + 1), static_cast<float>(i + 1), 0.0f);
            glVertex3f(static_cast<float>(j + 1), static_cast<float>(i),     0.0f);
            count++;
        }
    }
    glEnd();

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
}

static void initGL()
{
    video.render = true;
    // video.Init() calls gladLoaderLoadGL() internally — safe after GLFW context
    video.InitGL();

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

        // Animation tick
        tickScene();

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // ---- Build default docking layout on first frame ----
        if (firstFrame)
        {
            firstFrame = false;
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
            ImGui::DockBuilderDockWindow("Settings", dock_left);
            ImGui::DockBuilderDockWindow("3D Viewport", dock_center);
            ImGui::DockBuilderDockWindow("Animation", dock_bottom);
            ImGui::DockBuilderDockWindow("Model Control", dock_bottom);
            ImGui::DockBuilderDockWindow("Character", dock_right);
            ImGui::DockBuilderFinish(dockspace_id);
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
                    ImGui::TextWrapped("%s", g_loadStatus.c_str());
                }
                else if (!g_loadStatus.empty())
                {
                    ImGui::TextWrapped("%s", g_loadStatus.c_str());
                }
                else
                {
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

                // ---- Equipment summary ----
                ImGui::SeparatorText("Equipment");
                static const char* slotNames[] = {
                    "Head", "Shoulder", "Boots", "Belt", "Shirt", "Pants",
                    "Chest", "Bracers", "Gloves", "Right Hand", "Left Hand",
                    "Cape", "Tabard", "Quiver"
                };
                for (int s = 0; s < NUM_CHAR_SLOTS; ++s)
                {
                    WoWItem* item = cModel->getItem(static_cast<CharSlots>(s));
                    if (item && item->id() > 0)
                        ImGui::Text("%s: %s (%d)", slotNames[s], item->name().c_str(), item->id());
                    else
                        ImGui::TextDisabled("%s: empty", slotNames[s]);
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
                    ImGui::TextWrapped("%s", g_loadStatus.c_str());
                }
                else if (!g_loadStatus.empty())
                {
                    ImGui::TextWrapped("%s", g_loadStatus.c_str());
                }
            }

            // ---- Viewport section ----
            ImGui::SeparatorText("Viewport");
            ImGui::Checkbox("Draw Grid", &g_drawGrid);
            ImGui::ColorEdit3("Background", &g_bgColor.x);
            if (ImGui::Button("Reset Camera"))
                g_camera.reset();
            ImGui::Separator();
            ImGui::Checkbox("ImGui Demo Window", &show_demo_window);
            ImGui::Separator();
            ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                        g_camera.yaw(), g_camera.pitch(), g_camera.radius());
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));
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
                loadWoW(g_pendingConfigs[g_selectedConfig]);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                g_showConfigPopup = false;
                g_loadStatus = "Load cancelled.";
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
    freeNodePool();
    g_fileTreeRoot = nullptr;

    if (g_root)
    {
        g_root->delChildren();
        delete g_root;
        g_root = nullptr;
    }

    g_fbo.destroy();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer (ImGui) shutdown complete.";
    return 0;
}
