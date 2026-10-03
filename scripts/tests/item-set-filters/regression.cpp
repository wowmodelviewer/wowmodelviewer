#include "receiver.h"
#include "itemselection.h"
#include "ItemSetCategory.h"
#include <wx/listctrl.h>
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void require(bool ok, const char* message)
{
  ++checks;
  if (!ok) throw std::runtime_error(message);
}

class Dialog : public CategoryChoiceDialog
{
public:
  using CategoryChoiceDialog::CategoryChoiceDialog;
  void search(const wxString& text) { m_pattern->SetValue(text); }
  void only(int cat)
  {
    wxCommandEvent event(wxEVT_LISTBOX_DCLICK, ID_CAT_LIST);
    event.SetInt(cat);
    OnCheckDoubleClick(event);
  }
  void all(bool value)
  {
    for (int i = 0; i < numcats; ++i) Check(i, value);
    DoFilter();
  }
  int count() { return m_listctrl->GetItemCount(); }
  void choose(int row) { m_listctrl->Select(row); }
};

class App : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(App);

int main(int argc, char** argv)
{
  if (!wxEntryStart(argc, argv)) return 2;
  wxTheApp->CallOnInit();
  int result = 0;
  try
  {
    using namespace ItemSetCategory;
    Classifier empty;
    empty.add(0, -1, -1, -1);
    require(empty.category() == ItemSetCategory::Unknown, "empty set");
    for (int material = 1; material <= 4; ++material)
    {
      for (int slot : {IT_HEAD, IT_SHOULDER, IT_CHEST, IT_ROBE, IT_BELT, IT_PANTS, IT_BOOTS, IT_BRACERS, IT_GLOVES})
      {
        Classifier c;
        c.add(1, 4, material, slot);
        c.add(2, 4, material % 4 + 1, IT_CAPE);
        c.add(3, 4, 1, IT_NECK);
        c.add(4, 2, 4, IT_2HANDED);
        c.add(5, 4, 6, IT_SHIELD);
        require(c.category() == material - 1, "armor with accessories");
        c.add(6, -1, -1, -1);
        require(c.category() == ItemSetCategory::Unknown, "partial armor must be unknown");
        c.add(7, 4, material % 4 + 1, IT_GLOVES);
        require(c.category() == Mixed, "confirmed mixed despite missing piece");
      }
    }
    for (int slot : {IT_CAPE, IT_NECK, IT_RINGS, IT_ACCESSORY, IT_SHIRT, IT_TABARD, IT_SHIELD, IT_OFFHAND})
    {
      Classifier c;
      c.add(1, 4, 1, slot);
      require(c.category() == Other, "accessory-only set");
      c.add(2, -1, -1, -1);
      require(c.category() == ItemSetCategory::Unknown, "incomplete accessories");
    }
    wxArrayString names, categoryNames;
    names.Add("---- None ----");
    for (const auto* name : {"Cloth", "Leather", "Mail", "Plate", "Mixed", "Other", "Unknown"})
      categoryNames.Add(name);
    std::vector<int> cats{ItemSetCategory::Unknown};
    std::vector<int> ids{-1};
    for (int i = 0; i < 7; ++i)
    {
      names.Add("Alpha " + categoryNames[i]); cats.push_back(i); ids.push_back(100 + i * 13);
      names.Add("Beta " + categoryNames[i]); cats.push_back(i); ids.push_back(900 + i * 7);
    }
    CharControl receiver;
    {
      Dialog d(&receiver, UPDATE_SET, nullptr, "Choose", "Sets", names, cats, categoryNames, nullptr);
      require(d.count() == 15, "initially all categories");
      for (int i = 0; i < 7; ++i)
      {
        d.only(i);
        require(d.count() == 3, "individual category plus None");
        d.search("beta");
        require(d.count() == 2, "category AND case-insensitive search");
        const int before = receiver.calls;
        d.choose(1);
        require(receiver.calls == before + 1, "user click dispatches once");
        require(ids[receiver.selected] == 900 + i * 7, "filtered row maps to correct ID");
        d.search("Beta ");
        require(d.GetSelection() == i * 2 + 2, "visible selection preserved");
        require(receiver.calls == before + 1, "filter must not equip");
        d.search("no-such-set");
        require(d.count() == 1 && d.GetSelection() == -1, "zero sets leaves only None");
        d.search("");
        require(d.GetSelection() == -1, "restoring filter does not select a replacement");
      }
      const int before = receiver.calls;
      d.only(Cloth); d.Check(Plate); d.DoFilter();
      require(d.count() == 5, "multiple categories are ORed");
      d.search("Alpha"); require(d.count() == 3, "combined categories plus name");
      d.all(false); require(d.count() == 1, "all unchecked");
      d.search(""); d.all(true); require(d.count() == 15, "restore all");
      require(receiver.calls == before, "checkboxes never equip");
    }
    // Same production widget configuration as individual items, no keep-first row.
    {
      wxArrayString itemNames; itemNames.Add("Cloth hat"); itemNames.Add("Plate hat");
      std::vector<int> itemCats{Cloth, Plate};
      Dialog d(&receiver, 0, nullptr, "Choose", "Items", itemNames, itemCats, categoryNames, nullptr, false);
      require(d.count() == 2, "individual items initially visible");
      d.only(1); require(d.count() == 1, "sparse category mapping and double-click");
      d.choose(0); require(receiver.selected == 1, "individual filtered item index");
      const int before = receiver.calls;
      d.search("hat"); require(d.GetSelection() == 1, "individual selection retained");
      d.search("nothing"); require(d.count() == 0 && d.GetSelection() == -1, "truly empty list safe");
      d.all(true); d.search(""); require(d.count() == 2, "individual recovery");
      require(receiver.calls == before, "individual filtering never equips");
    }
    std::cout << checks << " checks passed; hidden production dialogs, no game or network.\n";
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
  wxTheApp->OnExit(); wxEntryCleanup();
  return result;
}
