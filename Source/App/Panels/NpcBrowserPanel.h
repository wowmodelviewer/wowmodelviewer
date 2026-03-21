#pragma once

#include <string>
#include <vector>
#include <functional>

struct NPCRecord;

namespace NpcBrowserPanel
{

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
