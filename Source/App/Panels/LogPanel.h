#pragma once

#include <string>
#include <vector>

/// @brief ImGui panel that displays the application log.
namespace LogPanel
{

/// @brief Per-frame context for the log panel.
struct DrawContext
{
    std::vector<std::string>* logLines       = nullptr;
    bool*                     logAutoScroll  = nullptr;
    bool*                     logNeedsReload = nullptr;
};

void draw(DrawContext& ctx);

} // namespace LogPanel
