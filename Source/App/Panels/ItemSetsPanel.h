#pragma once

#include <string>
#include <vector>
#include <functional>

class WoWModel;

/// @brief ImGui panel for applying pre-defined item sets and start outfits.
namespace ItemSetsPanel
{

/// @brief An item set from the ItemSet DB2 table.
struct ItemSetEntry
{
    int         id;    ///< ItemSet ID.
    std::string name;  ///< Display name.
};

/// @brief A starter outfit from the CharStartOutfit DB2 table.
struct StartOutfitEntry
{
    int         id;    ///< StartOutfit ID.
    std::string name;  ///< Display name.
};

/// @brief Per-frame context for the item sets panel.
struct DrawContext
{
    bool isChar = false;

    // Item Sets state
    std::vector<ItemSetEntry>*    itemSets          = nullptr;
    bool*                         itemSetsBuilt     = nullptr;
    std::string*                  itemSetSearchBuf  = nullptr;
    std::vector<size_t>*          itemSetFiltered   = nullptr;
    bool*                         itemSetFilterDirty = nullptr;

    // Start Outfits state
    std::vector<StartOutfitEntry>* startOutfits          = nullptr;
    bool*                          startOutfitsBuilt     = nullptr;
    std::string*                   startOutfitSearchBuf  = nullptr;
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
