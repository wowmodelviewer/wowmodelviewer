#pragma once

#include <filesystem>
#include <string>

// Extract all files from a ZIP archive into the given directory.
// Only regular stored and deflated entries are supported (covers dbd.zip).
// Returns true on success.
bool extractZip(const std::string& zipData, const std::filesystem::path& destDir);
