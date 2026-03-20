#pragma once

#include <string>
#include <vector>

namespace LogPanel
{

struct DrawContext
{
    std::vector<std::string>* logLines       = nullptr;
    bool*                     logAutoScroll  = nullptr;
    bool*                     logNeedsReload = nullptr;
};

void draw(DrawContext& ctx);

} // namespace LogPanel
