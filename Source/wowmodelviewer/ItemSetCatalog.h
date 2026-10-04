#pragma once
#include "ItemSetCategory.h"
#include <QString>
#include <functional>
#include <vector>

namespace ItemSets {
struct Appearance {
  int sourceId = 0, id = 0, modifier = 0, displayId = 0;
  bool preferred = false;
};
struct Item {
  int id = 0;
  QString name;
  std::vector<Appearance> appearances;
};
struct Slot { int id = -1; std::vector<Item> items; };
struct Set {
  int id = 0;
  bool transmog = false;
  QString name;
  ItemSetCategory::Type category = ItemSetCategory::Unknown;
  std::vector<Slot> equipmentSlots;
  bool incomplete = false;
};
struct Equipped { int slot, itemId, appearanceId; };
using Rows = std::vector<std::vector<QString>>;
using Query = std::function<Rows(const QString&)>;
// Bulk queries only, independent of the UI and the displayable-item cache.
std::vector<Set> read(const Query& query);
int slotForInventory(int inventoryType);
}
