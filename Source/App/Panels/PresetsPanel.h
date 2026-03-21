#pragma once

#include <string>
#include <functional>

namespace PresetsPanel
{

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
