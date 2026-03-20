#pragma once

#include <string>
#include <vector>
#include <functional>

class WoWModel;

namespace ItemSetsPanel
{

struct ItemSetEntry
{
    int         id;
    std::string name;
};

struct StartOutfitEntry
{
    int         id;
    std::string name;
};

struct DrawContext
{
    bool isChar = false;

    // Item Sets state
    std::vector<ItemSetEntry>*    itemSets          = nullptr;
    bool*                         itemSetsBuilt     = nullptr;
    char*                         itemSetSearchBuf  = nullptr;
    int                           itemSetSearchBufSize = 0;
    std::vector<size_t>*          itemSetFiltered   = nullptr;
    bool*                         itemSetFilterDirty = nullptr;

    // Start Outfits state
    std::vector<StartOutfitEntry>* startOutfits          = nullptr;
    bool*                          startOutfitsBuilt     = nullptr;
    char*                          startOutfitSearchBuf  = nullptr;
    int                            startOutfitSearchBufSize = 0;
    std::vector<size_t>*           startOutfitFiltered   = nullptr;
    bool*                          startOutfitFilterDirty = nullptr;

    // Callbacks
    std::function<WoWModel*()>           getLoadedModel;
    std::function<void()>                buildItemSets;
    std::function<void()>                rebuildItemSetFilter;
    std::function<void(WoWModel*, int)>  applyItemSet;
    std::function<void(WoWModel*)>       buildStartOutfits;
    std::function<void()>                rebuildStartOutfitFilter;
    std::function<void(WoWModel*, int)>  applyStartOutfit;
};

void draw(DrawContext& ctx);

} // namespace ItemSetsPanel
