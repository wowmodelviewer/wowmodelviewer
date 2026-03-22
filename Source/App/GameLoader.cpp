#include "GameLoader.h"
#include "AppState.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "Logger.h"
#include "Game.h"
#include "WoWFolder.h"
#include "WoWDatabase.h"
#include "HttpClient.h"
#include "CharTexture.h"
#include "RaceInfos.h"
#include "FileBrowserPanel.h"
#include "string_utils.h"

namespace GameLoader
{

// ---- Path helper ----------------------------------------------------------

std::filesystem::path getApplicationDirPath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// ---- Thread-safe load status helpers --------------------------------------

void setLoadStatus(const std::string& s, AppState& app)
{
    std::lock_guard<std::mutex> lock(app.loading.loadStatusMutex);
    app.loading.loadStatus = s;
}

std::string getLoadStatus(AppState& app)
{
    std::lock_guard<std::mutex> lock(app.loading.loadStatusMutex);
    return app.loading.loadStatus;
}

// ---- Support-file download ------------------------------------------------

static bool downloadFile(const std::string& url, const std::filesystem::path& dest,
                          const std::string& label, AppState& app,
                          bool replaceSeparators = false)
{
    LOG_INFO << "Downloading " << label << "...";
    setLoadStatus("Downloading " + label + "...", app);

    const auto resp = HttpClient::Get(url);
    if (!resp.success)
    {
        LOG_ERROR << "Failed to download " << label << ": " << resp.error;
        setLoadStatus("Failed to download " + label + ": " + resp.error, app);
        return false;
    }

    std::string content = resp.body;
    if (replaceSeparators)
        std::replace(content.begin(), content.end(), ' ', ';');

    std::ofstream file(dest, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR << "Failed to write " << label << " to " << dest.string();
        setLoadStatus("Failed to write " + label, app);
        return false;
    }

    file.write(content.data(), content.size());
    file.close();
    LOG_INFO << label << " saved to " << dest.string();
    return true;
}

static bool checkAndDownloadSupportFiles(AppState& app)
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
                          listfilePath, "listfile.csv", app))
            return false;
    }

    if (keysMissing)
    {
        if (!downloadFile("https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt",
                          keysPath, "extraEncryptionKeys.csv", app, true))
            return false;
    }

    // Ensure the dbdefs cache directory exists (DBDs are downloaded on demand)
    const fs::path dbdDir = appDir / "games" / "wow" / "dbdefs";
    fs::create_directories(dbdDir, ec);

    return true;
}

// ---- InitDatabase ---------------------------------------------------------

static void initDatabase(AppState& app)
{
    LOG_INFO << "Initializing Databases...";
    setLoadStatus("Initializing database...", app);

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
        LOG_INFO << "Database cache miss — will rebuild from DB2 files.";
        fs::remove(cachePath, ec);
        fs::remove(versionPath, ec);
    }

    if (enableDbCache)
    {
        GAMEDATABASE.setCachePath(cachePath.string());
        GAMEDATABASE.setFastMode();
    }

    const fs::path dbdDir = appDir / "games" / "wow" / "dbdefs";

    GAMEDATABASE.setDbdBaseUrl(
        "https://raw.githubusercontent.com/wowdev/WoWDBDefs/refs/heads/master/definitions/%s.dbd");

    GAMEDATABASE.setManifestUrl(
        "https://raw.githubusercontent.com/wowdev/WoWDBDefs/refs/heads/master/manifest.json");
    GAMEDATABASE.downloadAndParseManifest();

    LOG_INFO << "Attempting on-demand DBD-based database init from" << dbdDir.string();
    if (!GAMEDATABASE.initFromDBD(dbdDir.string(), currentVersion))
    {
        app.loading.initDB = false;
        LOG_ERROR << "Database initialization failed!";
        setLoadStatus("Database initialization failed!", app);
        fs::remove(cachePath, ec);
        fs::remove(versionPath, ec);
        return;
    }

    if (enableDbCache && !cacheValid)
    {
        std::ofstream vf(versionPath, std::ios::trunc);
        vf << currentVersion;
        LOG_INFO << "Database cache written for version " << currentVersion;
    }

    LOG_INFO << "Database initialization succeeded.";
    app.loading.loadProgress = 0.60f;

    LOG_INFO << "initDatabase: CharTexture::initRegions...";
    CharTexture::initRegions();
    app.loading.loadProgress = 0.65f;

    LOG_INFO << "initDatabase: RaceInfos::init...";
    RaceInfos::init();
    app.loading.loadProgress = 0.70f;

    app.loading.initDB = true;

    LOG_INFO << "initDatabase: skipping Creature table (disabled).";
    app.loading.loadProgress = 0.80f;

    LOG_INFO << "initDatabase: skipping Item/ItemSparse loading (disabled).";
    app.loading.loadProgress = 0.90f;
    LOG_INFO << "Finished initiating database files.";
}

// ---- loadWoW (CASC + listfile + database) ---------------------------------

static void loadWoW(const core::GameConfig& config, AppState& app)
{
    app.loading.loadProgress = 0.0f;
    setLoadStatus("Opening CASC storage...", app);

    if (!GAMEDIRECTORY.setConfig(config))
    {
        LOG_ERROR << "Could not load WoW Data folder (error "
                  << GAMEDIRECTORY.lastError() << ").";
        setLoadStatus("Failed to open CASC storage (error "
                       + std::to_string(GAMEDIRECTORY.lastError()) + ").", app);
        app.loading.loadThreadDone = true;
        return;
    }

    LOG_INFO << "Major version: " << GAMEDIRECTORY.majorVersion();
    app.loading.loadProgress = 0.05f;

    const std::string baseConfigFolder = "games/wow/";
    LOG_INFO << "Using config folder: " << baseConfigFolder;
    core::Game::instance().setConfigFolder(baseConfigFolder);

    setLoadStatus("Loading file list...", app);
    app.loading.loadProgress = 0.10f;
    GAMEDIRECTORY.setProgressCallback([&app](int current, int total) {
        if (total > 0)
            app.loading.loadProgress = 0.10f + 0.40f * static_cast<float>(current) / static_cast<float>(total);
    });
    GAMEDIRECTORY.initFromListfile("../../listfile.csv");
    GAMEDIRECTORY.setProgressCallback(nullptr);
    app.loading.loadProgress = 0.50f;

    setLoadStatus("Initializing database...", app);
    app.loading.loadProgress = 0.55f;
    initDatabase(app);

    if (!app.loading.initDB)
    {
        app.loading.loadThreadDone = true;
        return;
    }

    app.loading.loadProgress = 1.0f;
    setLoadStatus("World of Warcraft loaded successfully.", app);
    app.loading.loadThreadSuccess = true;
    app.loading.loadThreadDone = true;
}

// ---- Public API -----------------------------------------------------------

void loadWoWThreadFunc(core::GameConfig config, AppState& app)
{
    setLoadStatus("Checking support files...", app);
    if (!checkAndDownloadSupportFiles(app))
    {
        app.loading.loadThreadDone = true;
        return;
    }
    loadWoW(config, app);
}

void launchLoadThread(const core::GameConfig& config, AppState& app)
{
    app.loading.loadProgress = 0.0f;
    app.loading.loadThreadDone = false;
    app.loading.loadThreadSuccess = false;
    app.loading.loadInProgress = true;

    if (app.loading.loadThread.joinable())
        app.loading.loadThread.join();

    app.loading.loadThread = std::thread(loadWoWThreadFunc, config, std::ref(app));
}

void pollAsyncLoad(AppState& app)
{
    if (!app.loading.loadInProgress || !app.loading.loadThreadDone.load())
        return;

    if (app.loading.loadThread.joinable())
        app.loading.loadThread.join();

    app.loading.loadInProgress = false;

    if (app.loading.loadThreadSuccess.load())
    {
        app.loading.isWoWLoaded = true;
        FileBrowserPanel::markDirty();
        LOG_INFO << "World of Warcraft loaded successfully. Version: "
                 << GAMEDIRECTORY.version() << " Locale: " << GAMEDIRECTORY.locale();
        app.settings.save();
    }
}

void beginLoadWoW(AppState& app)
{
    if (app.loading.isWoWLoaded || app.loading.loadInProgress)
        return;

    app.settings.gamePath = app.loading.pathBuf;

    app.loading.loadInProgress = true;
    app.loading.loadProgress = 0.0f;
    setLoadStatus("Validating game path...", app);

    namespace fs = std::filesystem;
    std::string path = app.settings.gamePath;
    if (path.empty() || !fs::is_directory(path))
    {
        setLoadStatus("Please set a valid WoW Data folder path in Options > Settings.", app);
        app.loading.loadInProgress = false;
        return;
    }

    if (path.back() != '/' && path.back() != '\\')
        path += '\\';

    {
        std::string lower = core::toLower(path);
        if (lower.find("data\\") == std::string::npos && lower.find("data/") == std::string::npos)
            path += "Data\\";
    }
    app.settings.gamePath = path;

    if (!core::Game::instance().initDone())
        core::Game::instance().init(std::make_unique<wow::WoWFolder>(app.settings.gamePath), std::make_unique<wow::WoWDatabase>());

    app.loading.pendingConfigs = GAMEDIRECTORY.configsFound();

    if (app.loading.pendingConfigs.empty())
    {
        LOG_ERROR << "No locale found in WoW folder.";
        setLoadStatus("No locale found in the WoW folder.", app);
        app.loading.loadInProgress = false;
        return;
    }

    if (app.loading.pendingConfigs.size() == 1)
    {
        launchLoadThread(app.loading.pendingConfigs[0], app);
    }
    else
    {
        app.loading.selectedConfig = 0;
        app.loading.showConfigPopup = true;
        app.loading.loadInProgress = false;
    }
}

} // namespace GameLoader
