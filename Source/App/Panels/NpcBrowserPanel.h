#pragma once

#include <string>
#include <vector>
#include <functional>

struct NPCRecord;

/// @brief ImGui panel for browsing and loading NPC models.
namespace NpcBrowserPanel
{

/// @brief Per-frame context for the NPC browser panel.
struct DrawContext
{
    bool isWoWLoaded = false;
    bool isDBReady   = false;

    const std::vector<NPCRecord>* npcs          = nullptr;
    std::vector<size_t>*          npcFiltered    = nullptr;
    bool*                         npcFilterDirty = nullptr;
    std::string*                  npcSearchBuf   = nullptr;

    std::function<void()>              rebuildNpcFilter;
    std::function<void(unsigned int)>  loadNPC;
};

void draw(DrawContext& ctx);

} // namespace NpcBrowserPanel
