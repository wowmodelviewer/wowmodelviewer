#pragma once

#include <string>
#include <vector>
#include <functional>

class GameFile;
class WoWModel;

/// @brief ImGui panel for mounting and dismounting characters.
namespace MountsPanel
{

/// @brief A mount entry from the CreatureDisplayInfo DB2 table.
struct MountEntry
{
    int         displayID;  ///< Creature display ID.
    std::string name;       ///< Display name.
};

/// @brief Per-frame context for the mounts panel.
struct DrawContext
{
    bool isChar    = false;
    bool isMounted = false;

    std::vector<MountEntry>*   mountList          = nullptr;
    std::vector<std::string>*  creatureModelNames = nullptr;
    std::vector<GameFile*>*    creatureModels     = nullptr;
    std::vector<size_t>*       mountFiltered      = nullptr;
    bool*                      mountFilterDirty   = nullptr;
    int*                       mountTab           = nullptr;
    std::string*               mountSearchBuf     = nullptr;

    std::function<WoWModel*()>                getLoadedModel;
    std::function<void()>                     buildMountList;
    std::function<void()>                     rebuildMountFilter;
    std::function<void(int, GameFile*)>       mountCharacter;
    std::function<void()>                     dismountCharacter;
};

void draw(DrawContext& ctx);

} // namespace MountsPanel
