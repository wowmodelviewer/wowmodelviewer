#pragma once
#include "itemselection.h"
#include "ItemSetCatalog.h"
#include <array>
#include <functional>
class wxScrolledWindow;

class ItemSetChoiceDialog : public CategoryChoiceDialog {
public:
  using Apply = std::function<void(const std::vector<ItemSets::Equipped>&, bool)>;
  ItemSetChoiceDialog(wxWindow* parent, const wxArrayString& names,
    const std::vector<int>& categories, const wxArrayString& categoryNames,
    const std::vector<ItemSets::Set>& sets, Apply apply);
  void DoFilter() override;
private:
  struct Selection { size_t item = 0, appearance = 0; };
  const std::vector<ItemSets::Set>& m_sets;
  Apply m_apply;
  int m_active = -1;
  std::array<Selection, NUM_CHAR_SLOTS> m_selected{};
  wxScrolledWindow* m_details;
  wxStaticText* m_title;
  wxStaticText* m_notice;
  int m_detailWidth = 0;
  std::array<wxStaticText*, NUM_CHAR_SLOTS> m_labels{};
  std::array<wxChoice*, NUM_CHAR_SLOTS> m_items{};
  std::array<wxChoice*, NUM_CHAR_SLOTS> m_appearances{};
  void chooseSet(wxListEvent& event);
  void updateSlot(int slot);
  void changeItem(int slot);
  void changeAppearance(int slot);
  const ItemSets::Slot* findSlot(int slot) const;
  ItemSets::Equipped equipment(int slot) const;
  void clearDetails();
  void fitListColumn();
};
