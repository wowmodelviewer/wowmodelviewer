#pragma once
#include "RaceInfos.h"
#include "WoWItem.h"
struct WoWModel : std::vector<WoWItem *> { RaceInfos infos; };
