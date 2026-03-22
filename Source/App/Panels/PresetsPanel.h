#pragma once

#include <string>
#include <functional>

/// @brief ImGui panel for saving and loading character presets.
namespace PresetsPanel
{

/// @brief Per-frame context for the presets panel.
struct DrawContext
{
    std::string* presetPath     = nullptr;
    std::string* presetStatus   = nullptr;
    bool         isChar         = false;
    bool         hasModel       = false;

    std::function<void(const char*)> savePreset;
    std::function<void(const char*)> loadPreset;
};

void draw(DrawContext& ctx);

} // namespace PresetsPanel
