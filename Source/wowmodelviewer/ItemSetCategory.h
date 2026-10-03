#ifndef WMV_ITEM_SET_CATEGORY_H
#define WMV_ITEM_SET_CATEGORY_H

#include "wow_enums.h"

namespace ItemSetCategory
{
// All supported database.xml profiles expose these 17 ItemSet.ItemID columns.
constexpr int ItemCount = 17;
enum Type { Cloth, Leather, Mail, Plate, Mixed, Other, Unknown };

class Classifier
{
  unsigned mask = 0;
  bool any = false;
  bool incomplete = false;
public:
  void add(int itemId, int itemClass, int subclass, int inventoryType)
  {
    if (itemId == 0)
      return; // Empty set position, not missing data.
    any = true;
    if (itemId < 0 || itemClass < 0 || subclass < 0 || inventoryType < 0)
    {
      incomplete = true;
      return;
    }
    if (itemClass != 4) // Armor
      return;
    switch (inventoryType)
    {
      case IT_HEAD: case IT_SHOULDER: case IT_CHEST: case IT_ROBE:
      case IT_BELT: case IT_PANTS: case IT_BOOTS: case IT_BRACERS: case IT_GLOVES:
        if (subclass >= 1 && subclass <= 4)
          mask |= 1u << (subclass - 1);
        break;
      default: break; // Cloaks, jewelry, shields, shirts etc. do not imply a material.
    }
  }

  Type category() const
  {
    if (mask && (mask & (mask - 1)))
      return Mixed; // Missing pieces cannot undo two known materials.
    if (!any || incomplete)
      return Unknown;
    for (int i = Cloth; i <= Plate; ++i)
      if (mask == (1u << i))
        return static_cast<Type>(i);
    return Other;
  }
};
}
#endif
