#include "ItemSetCatalog.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace ItemSets {
int slotForInventory(int type)
{
  switch (type) {
    case IT_HEAD: return CS_HEAD;
    case IT_SHOULDER: return CS_SHOULDER;
    case IT_SHIRT: return CS_SHIRT;
    case IT_CHEST: case IT_ROBE: return CS_CHEST;
    case IT_BELT: return CS_BELT;
    case IT_PANTS: return CS_PANTS;
    case IT_BOOTS: return CS_BOOTS;
    case IT_BRACERS: return CS_BRACERS;
    case IT_GLOVES: return CS_GLOVES;
    case IT_DAGGER: case IT_RIGHTHANDED: case IT_GUN: case IT_THROWN:
    case IT_2HANDED: case IT_BOW: return CS_HAND_RIGHT;
    case IT_SHIELD: case IT_LEFTHANDED: case IT_OFFHAND: return CS_HAND_LEFT;
    case IT_CAPE: return CS_CAPE;
    case IT_TABARD: return CS_TABARD;
    default: return -1;
  }
}

std::vector<Set> read(const Query& query)
{
  std::vector<Set> result;
  auto consume = [&](const Rows& rows, bool transmog) {
    struct Coverage { int group = 0, classMask = 0; std::set<int> sources; };
    std::map<int, Coverage> coverage;
    std::map<int, Set> sets;
    std::map<int, ItemSetCategory::Classifier> classifiers;
    for (const auto& r : rows) {
      if (r.size() != 13 && r.size() != 15) continue;
      const int id = r[0].toInt(), itemId = r[2].toInt();
      auto& set = sets[id];
      set.id = id; set.name = r[1]; set.transmog = transmog;
      if (transmog && r.size() == 15) {
        auto& member = coverage[id];
        member.group = r[13].toInt(); member.classMask = r[14].toInt();
        if (r[7].toInt() > 0) member.sources.insert(r[7].toInt());
      }
      classifiers[id].add(itemId, r[3].toInt(), r[4].toInt(), r[5].toInt());
      const int slotId = slotForInventory(r[5].toInt());
      const int appearanceId = r[8].toInt(), displayId = r[12].toInt();
      if (itemId <= 0 || r[3].toInt() < 0 || appearanceId <= 0 || displayId <= 0 || r[6].isEmpty()) {
        if (itemId != 0) set.incomplete = true;
        continue;
      }
      // The equipment loader uses the named-item cache; unnamed sources cannot be loaded safely.
      if (slotId < 0) continue; // Jewelry etc. have no rendered character slot.
      auto slot = std::find_if(set.equipmentSlots.begin(), set.equipmentSlots.end(), [=](const Slot& s) { return s.id == slotId; });
      if (slot == set.equipmentSlots.end()) { set.equipmentSlots.push_back({slotId, {}}); slot = set.equipmentSlots.end()-1; }
      auto item = std::find_if(slot->items.begin(), slot->items.end(), [=](const Item& i) { return i.id == itemId; });
      if (item == slot->items.end()) {
        Item added; added.id = itemId;
        added.name = r[6].isEmpty() ? QString("Item %1").arg(itemId) : r[6];
        slot->items.push_back(added); item = slot->items.end()-1;
      }
      Appearance app;
      app.sourceId = r[7].toInt(); app.id = appearanceId; app.modifier = r[9].toInt();
      app.displayId = displayId;
      // This is a deterministic preference, not an assertion about the native WoW API.
      app.preferred = transmog ? (r[10].toInt() & 1) != 0 : r[11].toInt() == 0;
      auto same = std::find_if(item->appearances.begin(), item->appearances.end(), [=](const Appearance& a) { return a.id == app.id; });
      if (same == item->appearances.end()) item->appearances.push_back(app);
      else if (std::make_pair(!app.preferred, app.sourceId) < std::make_pair(!same->preferred, same->sourceId)) *same = app;
    }
    // Ungrouped reward records can list fragments of several variants. Hide one
    // only when a real catalog group covers every exact source it references.
    // ClassMask is a relationship guard here, never a character/class filter.
    // Reward fragments can have a different localized name (e.g. Draconic
    // versus Verdant). Exact source membership establishes the relationship.
    std::map<int, std::map<int, std::set<int>>> completeGroups;
    for (const auto& pair : sets) {
      const auto& set = pair.second;
      const auto& member = coverage[pair.first];
      if (!transmog || !member.group || set.incomplete) continue;
      std::set<int> body;
      for (const auto& slot : set.equipmentSlots)
        if (slot.id == CS_HEAD || slot.id == CS_SHOULDER || slot.id == CS_CHEST ||
            slot.id == CS_PANTS || slot.id == CS_BOOTS) body.insert(slot.id);
      // Conservative completeness guard: retain fragments when no full outfit
      // with head, shoulders, chest, legs and feet is available in the catalog.
      if (body.size() != 5) continue;
      auto& sources = completeGroups[member.classMask][member.group];
      sources.insert(member.sources.begin(), member.sources.end());
    }
    std::set<int> redundant;
    for (const auto& pair : sets) {
      const auto& set = pair.second;
      const auto& member = coverage[pair.first];
      if (!transmog || member.group || set.incomplete || member.sources.empty()) continue;
      std::set<int> body;
      for (const auto& slot : set.equipmentSlots)
        if (slot.id == CS_HEAD || slot.id == CS_SHOULDER || slot.id == CS_CHEST ||
            slot.id == CS_PANTS || slot.id == CS_BOOTS) body.insert(slot.id);
      if (body.size() == 5) continue; // Never discard another full variant.
      const auto family = completeGroups.find(member.classMask);
      if (family == completeGroups.end()) continue;
      for (const auto& group : family->second)
        if (std::includes(group.second.begin(), group.second.end(), member.sources.begin(), member.sources.end())) {
          redundant.insert(pair.first); break;
        }
    }
    for (auto& pair : sets) {
      auto& set = pair.second;
      if (set.name.trimmed().isEmpty() || redundant.count(pair.first)) continue;
      set.category = classifiers[pair.first].category();
      // Unresolved appearance sources may hide an additional material.
      if (set.incomplete && set.category != ItemSetCategory::Mixed) set.category = ItemSetCategory::Unknown;
      for (auto& slot : set.equipmentSlots) {
        for (auto& item : slot.items)
          std::sort(item.appearances.begin(), item.appearances.end(), [](const Appearance& a, const Appearance& b) {
            return std::make_tuple(!a.preferred, a.modifier, a.sourceId) < std::make_tuple(!b.preferred, b.modifier, b.sourceId);
          });
        std::sort(slot.items.begin(), slot.items.end(), [](const Item& a, const Item& b) {
          return std::make_pair(!a.appearances.front().preferred, a.id) < std::make_pair(!b.appearances.front().preferred, b.id);
        });
      }
      std::sort(set.equipmentSlots.begin(), set.equipmentSlots.end(), [](const Slot& a, const Slot& b) { return a.id < b.id; });
      result.push_back(std::move(set));
    }
  };
  QString members;
  for (int i = 1; i <= ItemSetCategory::ItemCount; ++i) {
    if (i > 1) members += " UNION ALL ";
    members += QString("SELECT ID, Name_Lang, ItemID%1 AS ItemID FROM ItemSet").arg(i);
  }
  const QString fields = "COALESCE(I.ClassID,-1), COALESCE(I.SubclassID,-1), COALESCE(I.InventoryType,-1), "
    "N.Display_Lang, A.ID, A.ItemAppearanceID, A.ItemAppearanceModifierID, ";
  const QString joins = " LEFT JOIN Item I ON I.ID=M.ItemID LEFT JOIN ItemSparse N ON N.ID=M.ItemID "
    "LEFT JOIN ItemModifiedAppearance A ON A.ItemID=M.ItemID LEFT JOIN ItemAppearance P ON P.ID=A.ItemAppearanceID";
  consume(query("SELECT M.ID, M.Name_Lang, M.ItemID, " + fields + "0, A.OrderIndex, P.ItemDisplayInfoID FROM (" + members + ") M" + joins), false);
  const auto tables = query("SELECT name FROM sqlite_master WHERE type='table' AND name IN ('TransmogSet','TransmogSetItem')");
  if (tables.size() == 2) {
    // Graceful fallback for pre-upgrade/offline databases with only the old columns.
    bool hasGroup = false, hasClass = false;
    for (const auto& field : query("PRAGMA table_info(TransmogSet)"))
      if (field.size() > 1) {
        hasGroup |= field[1].compare("TransmogSetGroupID", Qt::CaseInsensitive) == 0;
        hasClass |= field[1].compare("ClassMask", Qt::CaseInsensitive) == 0;
      }
    const QString metadata = hasGroup && hasClass ? ", S.TransmogSetGroupID, S.ClassMask" : ", 0, 0";
    consume(query("SELECT S.ID, S.Name_Lang, COALESCE(A.ItemID,-1), " + fields +
      "T.Flags, A.OrderIndex, P.ItemDisplayInfoID" + metadata + " FROM TransmogSet S "
      "LEFT JOIN TransmogSetItem T ON T.TransmogSetID=S.ID "
      "LEFT JOIN ItemModifiedAppearance A ON A.ID=T.ItemModifiedAppearanceID "
      "LEFT JOIN Item I ON I.ID=A.ItemID LEFT JOIN ItemSparse N ON N.ID=A.ItemID "
      "LEFT JOIN ItemAppearance P ON P.ID=A.ItemAppearanceID"), true);
  }
  std::sort(result.begin(), result.end(), [](const Set& a, const Set& b) {
    const int name = QString::compare(a.name, b.name, Qt::CaseInsensitive);
    return name ? name < 0 : std::make_pair(a.transmog, a.id) < std::make_pair(b.transmog, b.id);
  });
  Set none; none.name = "---- None ----";
  result.insert(result.begin(), none);
  return result;
}
}
