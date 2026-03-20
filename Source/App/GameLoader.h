#pragma once

// ---- Game-loading helpers -------------------------------------------------
// Extracted from main.cpp — handles CASC storage, listfile, database init,
// support-file downloads, and the background loading thread.

#include <filesystem>
#include <string>

struct AppState;
namespace core { class GameConfig; }

namespace GameLoader
{

/// Return the directory containing the running executable.
std::filesystem::path getApplicationDirPath();

/// Thread-safe load-status setters / getters (lock app.loadStatusMutex).
void setLoadStatus(const std::string& s, AppState& app);
std::string getLoadStatus(AppState& app);

/// Entry point executed on the background thread.
/// Downloads support files, opens CASC, loads the listfile and database.
void loadWoWThreadFunc(core::GameConfig config, AppState& app);

/// Spawn the background loading thread for the given config.
void launchLoadThread(const core::GameConfig& config, AppState& app);

/// Poll whether the background thread has finished; joins it and updates
/// app.isWoWLoaded on success.  Call once per frame from the main loop.
void pollAsyncLoad(AppState& app);

/// Validate the game path, detect configs, and either launch the load
/// thread or show the config-selection popup.  Main "Load WoW" entry.
void beginLoadWoW(AppState& app);

} // namespace GameLoader
