#pragma once

#include <string>
#include <vector>
#include <functional>

class GameFile;
class WoWModel;

namespace MountsPanel
{

struct MountEntry
{
    int         displayID;
    std::string name;
};

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
