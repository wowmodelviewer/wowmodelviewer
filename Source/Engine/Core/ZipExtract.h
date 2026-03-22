#pragma once

#include <filesystem>
#include <string>

/// @brief Extract all files from a ZIP archive into the given directory.
///
/// Only regular stored and deflated entries are supported (covers dbd.zip).
/// @param zipData  Raw ZIP file contents in memory.
/// @param destDir  Destination directory (created if needed).
/// @return true on success.
bool extractZip(const std::string& zipData, const std::filesystem::path& destDir);
