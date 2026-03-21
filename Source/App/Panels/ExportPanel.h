#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AnimationPanel.h"

class ExporterPlugin;
class WoWModel;

namespace ExportPanel
{
    using AnimEntry = AnimationPanel::AnimEntry;

    struct DrawContext
    {
        std::function<WoWModel*()>       getLoadedModel;
        std::vector<std::unique_ptr<ExporterPlugin>>* exporters = nullptr;
        int*                             selectedExporter   = nullptr;
        std::vector<AnimEntry>*          animEntries        = nullptr;
        std::vector<char>*               exportAnimChecked  = nullptr;
        int*                             selectedAnimCombo  = nullptr;
        char*                            exportPath         = nullptr;
        size_t                           exportPathSize     = 0;
        std::string*                     exportStatus       = nullptr;
    };

    void draw(DrawContext& ctx);

} // namespace ExportPanel
