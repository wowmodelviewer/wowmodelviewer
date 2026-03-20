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
    char*                         npcSearchBuf   = nullptr;
    int                           npcSearchBufSize = 0;

    std::function<void()>              rebuildNpcFilter;
    std::function<void(unsigned int)>  loadNPC;
};

void draw(DrawContext& ctx);

} // namespace NpcBrowserPanel
