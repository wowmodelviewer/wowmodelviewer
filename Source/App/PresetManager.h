#pragma once

// ---- Character preset save / load -----------------------------------------
// Extracted from main.cpp.

struct AppState;

namespace PresetManager
{

/// Save the current character's customisations, display options, and
/// equipment to an INI file.
void save(const char* path, AppState& app);

/// Load a previously saved character preset from an INI file.
void load(const char* path, AppState& app);

} // namespace PresetManager
