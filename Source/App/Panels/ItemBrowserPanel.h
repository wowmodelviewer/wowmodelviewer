#pragma once

#include <string>
#include <vector>
#include <functional>

struct ItemRecord;
class ItemDatabase;

/// @brief ImGui panel for browsing and loading item models.
namespace ItemBrowserPanel
{

/// @brief Per-frame context for the item browser panel.
struct DrawContext
{
    bool isWoWLoaded = false;
    bool isDBReady   = false;

    const ItemDatabase*           items               = nullptr;
    std::vector<size_t>*          itemBrowseFiltered   = nullptr;
    bool*                         itemBrowseFilterDirty = nullptr;
    std::string*                  itemBrowseSearchBuf  = nullptr;

    std::function<void()>              rebuildItemBrowseFilter;
    std::function<void(unsigned int)>  loadItemModel;
};

void draw(DrawContext& ctx);

} // namespace ItemBrowserPanel
