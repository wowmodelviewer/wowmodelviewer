#include "ItemSetChoiceDialog.h"
#include <wx/listctrl.h>
#include <wx/artprov.h>
#include <wx/bmpbuttn.h>
#include <wx/scrolwin.h>
#include <algorithm>

ItemSetChoiceDialog::ItemSetChoiceDialog(wxWindow* parent, const wxArrayString& names,
  const std::vector<int>& categories, const wxArrayString& categoryNames,
  const std::vector<ItemSets::Set>& sets, Apply apply)
  : CategoryChoiceDialog(nullptr, 0, parent, _("Choose an item set"), _("Item sets"),
      names, categories, categoryNames, nullptr), m_sets(sets), m_apply(std::move(apply))
{
  // Keep the original picker column at its original client width and height.
  const wxSize originalClient = GetClientSize();
  SetWindowStyleFlag(GetWindowStyleFlag() | wxRESIZE_BORDER);
  wxSizer* left = GetSizer();
  SetSizer(nullptr, false);
  const int gap = FromDIP(10);
  // Move the existing separator and standard OK button out of the left column.
  wxSizer* footer = left->GetItem(left->GetItemCount() - 1)->GetSizer();
  left->Detach(footer);
  // Let the existing search row fill the column; only this set picker changes.
  auto* filterRow = m_pattern->GetContainingSizer();
  auto* filterLabel = filterRow->GetItem(static_cast<size_t>(0))->GetWindow();
  filterRow->Detach(filterLabel);
  filterLabel->Destroy();
  m_pattern->SetHint(_("Filter by name"));
  m_pattern->SetName(_("Filter by name"));
  auto* filterArea = left->GetItem(2)->GetSizer();
  filterArea->GetItem(filterRow)->SetFlag(wxEXPAND);
  auto* oldClear = FindWindow(ID_FILTER_CLEAR);
  const int buttonHeight = FindWindow(wxID_OK)->GetBestSize().y;
  oldClear->SetId(wxID_ANY);
  auto* clear = new wxBitmapButton(this, ID_FILTER_CLEAR,
    wxArtProvider::GetBitmap(wxART_CLOSE, wxART_BUTTON, FromDIP(wxSize(12, 12))),
    wxDefaultPosition, wxSize(buttonHeight, buttonHeight));
  filterRow->Replace(oldClear, clear);
  oldClear->Destroy();
  clear->SetToolTip(_("Clear search"));
  clear->SetName(_("Clear search"));
  clear->SetMinSize(wxSize(buttonHeight, buttonHeight));
  clear->SetMaxSize(wxSize(buttonHeight, buttonHeight));
  clear->SetSize(wxSize(buttonHeight, buttonHeight));
  filterRow->GetItem(m_pattern)->SetFlag(wxALIGN_CENTER_VERTICAL);
  filterRow->GetItem(clear)->SetFlag(wxALIGN_CENTER_VERTICAL);
  auto* categoryActions = new wxGridSizer(1, 2, 0, 0);
  for (const bool checked : {true, false}) {
    auto* button = new wxButton(this, wxID_ANY, checked ? _("Select all") : _("Select none"),
      wxDefaultPosition, wxDefaultSize);
    button->SetName(checked ? "set-categories-all" : "set-categories-none");
    categoryActions->Add(button, 1, wxEXPAND);
    button->Bind(wxEVT_BUTTON, [this, checked](wxCommandEvent&) {
      for (unsigned i = 0; i < m_catlist->GetCount(); ++i) Check(i, checked);
      DoFilter();
    });
  }
  // Base order: category help, categories, search, item heading, list.
  left->GetItem(m_catlist)->SetFlag(wxEXPAND | wxTOP | wxLEFT | wxRIGHT);
  left->Insert(2, categoryActions, 0, wxEXPAND | wxLEFT | wxRIGHT, gap);
  left->Insert(3, FromDIP(0), FromDIP(6));
  auto* heading = left->GetItem(5);
  heading->SetFlag(wxEXPAND | wxLEFT | wxRIGHT | wxTOP);
  heading->SetBorder(gap);
  left->Detach(m_listctrl);
  auto* listRow = new wxBoxSizer(wxHORIZONTAL);
  listRow->AddSpacer(gap);
  listRow->Add(m_listctrl, 1, wxEXPAND);
  listRow->AddSpacer(gap);
  left->Add(listRow, 1, wxEXPAND | wxTOP, gap);
  m_listctrl->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
    fitListColumn();
    e.Skip();
  });
  auto* columns = new wxGridSizer(1, 2, 0, 0);
  columns->Add(left, 1, wxEXPAND)->SetMinSize(originalClient.x, -1);
  auto* panel = new wxPanel(this);
  panel->SetMinSize(wxSize(originalClient.x, -1));
  auto* right = new wxBoxSizer(wxVERTICAL);
  const int padding = FromDIP(10);
  m_detailWidth = originalClient.x - 2 * padding;
  m_title = new wxStaticText(panel, wxID_ANY, _("Set pieces"), wxDefaultPosition,
    wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
  m_title->SetMinSize(wxSize(0, -1));
  m_title->SetFont(m_title->GetFont().Bold());
  right->Add(m_title, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
  m_notice = new wxStaticText(panel, wxID_ANY, _("Select a set to see its pieces and alternatives."));
  m_notice->Wrap(m_detailWidth);
  right->Add(m_notice, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
  m_details = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
  m_details->SetMinSize(wxSize(0, 0));
  m_details->SetScrollRate(0, FromDIP(12));
  auto* grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
  grid->AddGrowableCol(1);
  const int order[] = {CS_HEAD, CS_SHOULDER, CS_SHIRT, CS_CHEST, CS_BELT, CS_PANTS,
    CS_BOOTS, CS_BRACERS, CS_GLOVES, CS_CAPE, CS_HAND_RIGHT, CS_HAND_LEFT, CS_TABARD};
  const char* labels[] = {"Head", "Shoulder", "Shirt", "Chest", "Belt", "Legs", "Boots",
    "Bracers", "Gloves", "Cape", "Right hand", "Left hand", "Tabard"};
  for (size_t i = 0; i < sizeof(order)/sizeof(order[0]); ++i) {
    const int slot = order[i];
    grid->Add(new wxStaticText(m_details, wxID_ANY, wxGetTranslation(labels[i])), 0, wxTOP, FromDIP(4));
    auto* cell = new wxBoxSizer(wxVERTICAL);
    m_labels[slot] = new wxStaticText(m_details, wxID_ANY, _("--- None ---"), wxDefaultPosition,
      wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    m_labels[slot]->SetMinSize(wxSize(0, -1));
    cell->Add(m_labels[slot], 0, wxEXPAND);
    m_items[slot] = new wxChoice(m_details, wxID_ANY);
    m_items[slot]->SetMinSize(wxSize(0, -1));
    m_items[slot]->SetName(wxString::Format("set-item-%d", slot));
    cell->Add(m_items[slot], 0, wxEXPAND);
    m_items[slot]->Bind(wxEVT_CHOICE, [this, slot](wxCommandEvent&) { changeItem(slot); });
    m_appearances[slot] = new wxChoice(m_details, wxID_ANY);
    m_appearances[slot]->SetMinSize(wxSize(0, -1));
    m_appearances[slot]->SetName(wxString::Format("set-appearance-%d", slot));
    cell->Add(m_appearances[slot], 0, wxEXPAND | wxTOP, FromDIP(4));
    m_appearances[slot]->Bind(wxEVT_CHOICE, [this, slot](wxCommandEvent&) { changeAppearance(slot); });
    grid->Add(cell, 1, wxEXPAND);
  }
  m_details->SetSizer(grid);
  right->Add(m_details, 1, wxEXPAND);
  auto* inset = new wxBoxSizer(wxVERTICAL);
  inset->Add(right, 1, wxEXPAND | wxALL, padding);
  panel->SetSizer(inset);
  columns->Add(panel, 1, wxEXPAND);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(columns, 1, wxEXPAND);
  root->Add(footer, 0, wxEXPAND | wxALL, gap);
  SetSizer(root);
  clearDetails();
  const wxSize expandedClient(originalClient.x * 2, originalClient.y);
  SetMinSize(ClientToWindowSize(expandedClient));
  SetClientSize(expandedClient);
  Layout();
  // This handler consumes explicit row selections; filtering uses the inherited guard.
  m_listctrl->Bind(wxEVT_LIST_ITEM_SELECTED, &ItemSetChoiceDialog::chooseSet, this);
  m_pattern->SetFocus();
}

const ItemSets::Slot* ItemSetChoiceDialog::findSlot(int slot) const
{
  if (m_active < 0 || static_cast<size_t>(m_active) >= m_sets.size()) return nullptr;
  for (const auto& value : m_sets[m_active].equipmentSlots) if (value.id == slot) return &value;
  return nullptr;
}
void ItemSetChoiceDialog::clearDetails()
{
  for (int slot = 0; slot < NUM_CHAR_SLOTS; ++slot) if (m_labels[slot]) {
    m_labels[slot]->SetLabel(_("--- None ---")); m_labels[slot]->Show();
    m_items[slot]->Hide(); m_items[slot]->Clear(); m_appearances[slot]->Hide(); m_appearances[slot]->Clear();
  }
  m_details->FitInside();
}
void ItemSetChoiceDialog::updateSlot(int slot)
{
  const auto* values = findSlot(slot);
  if (!values || !m_labels[slot]) return;
  const auto& selected = m_selected[slot];
  const auto& item = values->items[selected.item];
  m_labels[slot]->SetLabel(item.name.toStdWString());
  m_labels[slot]->SetToolTip(item.name.toStdWString());

  m_labels[slot]->Show(values->items.size() == 1);
  m_items[slot]->Show(values->items.size() > 1);
  m_appearances[slot]->Clear();
  for (size_t i = 0; i < item.appearances.size(); ++i)
    m_appearances[slot]->Append(wxString::Format(_("Appearance %u"), static_cast<unsigned>(i + 1)));
  m_appearances[slot]->SetSelection(static_cast<int>(selected.appearance));
  m_appearances[slot]->Show(item.appearances.size() > 1);
}
ItemSets::Equipped ItemSetChoiceDialog::equipment(int slot) const
{
  const auto* values = findSlot(slot);
  const auto& selected = m_selected[slot];
  const auto& item = values->items[selected.item];
  return {slot, item.id, item.appearances[selected.appearance].id};
}
void ItemSetChoiceDialog::chooseSet(wxListEvent&)
{
  if (m_syncingSelection) return;
  m_selection = m_listctrl->GetFirstSelected();
  const int index = GetSelection();
  if (index < 0 || static_cast<size_t>(index) >= m_sets.size()) return;
  m_details->Freeze();
  m_active = index;
  m_selected = {};
  clearDetails();
  const auto& set = m_sets[index];
  m_title->SetLabel(set.name.toStdWString()); m_title->SetToolTip(set.name.toStdWString());
  m_notice->SetLabel(set.incomplete ? _("Some pieces could not be resolved from the game data.") :
    (set.equipmentSlots.empty() && set.id ? _("This set has no pieces in supported equipment slots.") : _("Choose an item or appearance where alternatives are available.")));
  m_notice->Wrap(-1); // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
  m_notice->Wrap(m_detailWidth);
  std::vector<ItemSets::Equipped> pieces;
  for (const auto& slot : set.equipmentSlots) {
    for (const auto& item : slot.items)
      m_items[slot.id]->Append(wxString(item.name.toStdWString()) + wxString::Format(" [%d]", item.id));
    m_items[slot.id]->SetSelection(0);
    updateSlot(slot.id);
    pieces.push_back(equipment(slot.id));
  }
  m_details->Layout(); m_details->FitInside(); m_details->Thaw();
  m_details->Scroll(0, 0); Layout();
  if (!pieces.empty() || !set.id) m_apply(pieces, true);
}
void ItemSetChoiceDialog::changeItem(int slot)
{
  const auto* values = findSlot(slot);
  const int index = m_items[slot]->GetSelection();
  if (!values || index < 0 || static_cast<size_t>(index) >= values->items.size()) return;
  m_selected[slot] = {static_cast<size_t>(index), 0};
  updateSlot(slot);
  m_details->Layout(); m_details->FitInside();
  m_apply({equipment(slot)}, false);
}
void ItemSetChoiceDialog::changeAppearance(int slot)
{
  const auto* values = findSlot(slot);
  const int index = m_appearances[slot]->GetSelection();
  if (!values || index < 0 || static_cast<size_t>(index) >= values->items[m_selected[slot].item].appearances.size()) return;
  m_selected[slot].appearance = static_cast<size_t>(index);
  m_apply({equipment(slot)}, false);
}
void ItemSetChoiceDialog::fitListColumn()
{
  const int width = std::max(1, m_listctrl->GetClientSize().x);
  if (m_listctrl->GetColumnWidth(0) != width)
    m_listctrl->SetColumnWidth(0, width);
}

void ItemSetChoiceDialog::DoFilter()
{
  CategoryChoiceDialog::DoFilter();
  fitListColumn();
  if (GetSelection() == wxNOT_FOUND) {
    m_active = -1; clearDetails();
    m_title->SetLabel(_("Set pieces"));
    m_notice->SetLabel(_("Select a set to see its pieces and alternatives."));
    m_notice->Wrap(-1);
    m_notice->Wrap(m_detailWidth); Layout();
  }
}
