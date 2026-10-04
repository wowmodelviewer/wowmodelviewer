#include "ItemSetChoiceDialog.h"
#include <sqlite3.h>
#include <wx/listctrl.h>
#include <wx/app.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <set>

class TestApp : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(TestApp);
static int checks = 0;
void require(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
int main(int argc, char** argv)
{
  const std::string path = argc > 1 ? argv[1] : "";
  const bool smoke = argc > 2 && std::string(argv[2]) == "--smoke";
  sqlite3* db = nullptr;
  if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  int result = 0;
  try {
    require(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "open DB");
    int queries = 0;
    auto query = [&](const QString& sql) {
      ++queries;
      ItemSets::Rows rows;
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
      int step;
      while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::vector<QString> row;
        for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
          const auto* text = sqlite3_column_text(stmt, i);
          row.push_back(text ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString());
        }
        rows.push_back(row);
      }
      sqlite3_finalize(stmt);
      if (step != SQLITE_DONE) throw std::runtime_error("query failed");
      return rows;
    };
    auto start = std::chrono::steady_clock::now();
    auto sets = ItemSets::read(query);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    require(sets.size() > 1000, "catalog unexpectedly empty");
    const int openingQueries = queries;
    require(openingQueries <= 4, "bounded bulk queries");
    int legacy = 0, transmog = 0, alternatives = 0, variants = 0;
    wxArrayString names, catnames;
    std::vector<int> cats;
    for (const char* name : {"Cloth","Leather","Mail","Plate","Mixed","Other","Unknown"}) catnames.Add(name);
    for (const auto& set : sets) {
      names.Add(set.name.toStdWString()); cats.push_back(set.category);
      if (set.id) (set.transmog ? transmog : legacy)++;
      for (const auto& slot : set.equipmentSlots) {
        if (slot.items.size()>1) ++alternatives;
        for (const auto& item : slot.items) {
          if (item.appearances.size()>1) ++variants;
          std::set<int> appearances;
          for (const auto& app : item.appearances)
            require(appearances.insert(app.id).second, "duplicate appearance within item");
        }
      }
    }
    std::cout << "Catalog: " << legacy << " legacy, " << transmog << " transmog; " << elapsed << " ms" << std::endl;
    int calls = 0;
    bool replace = false;
    std::vector<ItemSets::Equipped> equipped;
    CategoryChoiceDialog original(nullptr, 0, nullptr, "Choose an item set", "Item sets", names, cats, catnames, nullptr);
    original.Layout();
    const wxSize originalClient = original.GetClientSize();
    const int originalListWidth = originalClient.x - 2 * original.FromDIP(10);
    ItemSetChoiceDialog d(nullptr, names, cats, catnames, sets,
      [&](const std::vector<ItemSets::Equipped>& p, bool r) { ++calls; equipped=p; replace=r; });
    require(d.GetClientSize().x == originalClient.x * 2, "popup must double original client width");
    require(d.GetClientSize().y == originalClient.y, "popup must preserve original height");
    require(d.m_listctrl->GetSize().x == originalListWidth, "original set list width must not change");
    std::cout << "Layout: original " << originalClient.x << "x" << originalClient.y
              << ", expanded " << d.GetClientSize().x << "x" << d.GetClientSize().y << std::endl;
    require(d.FindWindow(wxID_OK)->GetRect().GetRight() > d.GetClientSize().x - d.FromDIP(25), "OK must align with the right edge");
    const wxSize compactList=d.m_listctrl->GetSize();
    d.SetClientSize(wxSize(originalClient.x*2+200, originalClient.y+120)); d.Layout();
    require(d.m_listctrl->GetSize().x==compactList.x+100, "left column must receive half the added width");
    require(d.m_listctrl->GetSize().y>=compactList.y+110, "item grid must grow vertically");
    d.SetClientSize(wxSize(originalClient.x*2, originalClient.y)); d.Layout();
    for(const char* name : {"set-categories-none", "set-categories-all"}) {
      auto* button=wxWindow::FindWindowByName(name,&d);
      require(button!=nullptr, "category select buttons missing");
      const int before=calls;
      wxCommandEvent click(wxEVT_BUTTON,button->GetId()); click.SetEventObject(button);
      button->GetEventHandler()->ProcessEvent(click);
      require(calls==before,"bulk category selection must not equip");
      require(d.m_listctrl->GetItemCount()==(std::string(name)=="set-categories-none" ? 1 : static_cast<long>(sets.size())), "bulk categories filter rows");
    }
    auto* search = static_cast<wxTextCtrl*>(d.FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT));
    auto* categories = static_cast<wxCheckListBox*>(d.FindWindow(CategoryChoiceDialog::ID_CAT_LIST));
    int chosen = 0, changedItems = 0, changedAppearances = 0;
    for (unsigned cat=0; cat<categories->GetCount(); ++cat) {
      int before = calls;
      wxCommandEvent isolate(wxEVT_LISTBOX_DCLICK, CategoryChoiceDialog::ID_CAT_LIST); isolate.SetInt(cat);
      d.OnCheckDoubleClick(isolate);
      require(calls==before, "category filter equipped a set");
      for (long row=1; row<d.m_listctrl->GetItemCount(); ++row) {
        if (smoke && row > 3) break;
        before = calls;
        d.m_listctrl->Select(row);
        const int index=d.GetSelection();
        require(index>0, "filtered index missing");
        const auto& set=sets[index];
        if (set.equipmentSlots.empty()) { require(calls==before, "unresolved set cleared equipment"); continue; }
        require(calls==before+1 && replace, "set selection callback");
        require(equipped.size()==set.equipmentSlots.size(), "all rendered slots equipped");
        for (size_t i=0;i<set.equipmentSlots.size();++i) {
          const auto& slot=set.equipmentSlots[i]; const auto& item=slot.items.front();
          require(equipped[i].slot==slot.id && equipped[i].itemId==item.id && equipped[i].appearanceId==item.appearances.front().id,"wrong default piece/appearance");
        }
        require(d.GetClientSize().x == originalClient.x * 2 && d.m_listctrl->GetSize().x == originalListWidth,
          "long set or item names must not widen either column");
        ++chosen;
        // Exercise both selectors for every set with alternatives.
        for (const auto& slot:set.equipmentSlots) {
          auto* box=static_cast<wxChoice*>(wxWindow::FindWindowByName(wxString::Format("set-item-%d",slot.id),&d));
          auto* app=static_cast<wxChoice*>(wxWindow::FindWindowByName(wxString::Format("set-appearance-%d",slot.id),&d));
          require(box && app,"slot controls missing");
          require(box->IsKindOf(wxCLASSINFO(wxChoice)), "item alternatives must be closed dropdowns");
          size_t itemIndex=0;
          if (slot.items.size()>1) {
            itemIndex=slot.items.size()-1;
            box->SetSelection(static_cast<int>(itemIndex));
            wxCommandEvent change(wxEVT_CHOICE,box->GetId()); change.SetEventObject(box); box->GetEventHandler()->ProcessEvent(change);
            require(!replace && equipped.size()==1 && equipped[0].itemId==slot.items[itemIndex].id,"item option updates only chosen slot");
            ++changedItems;
          }
          const auto& item=slot.items[itemIndex];
          if (item.appearances.size()>1) {
            app->SetSelection(static_cast<int>(item.appearances.size()-1));
            wxCommandEvent change(wxEVT_CHOICE,app->GetId()); change.SetEventObject(app); app->GetEventHandler()->ProcessEvent(change);
            require(!replace && equipped.size()==1 && equipped[0].appearanceId==item.appearances.back().id,"appearance option maps exact appearance");
            ++changedAppearances;
          }
        }
        if (chosen % 173 == 0) {
          before=calls; d.DoFilter();
          require(d.GetSelection()==index && calls==before,"filter lost visible selection or equipped");
        }
      }
    }
    const int before=calls;
    search->SetValue("no-match-xyz-123"); require(d.GetSelection()==-1 && d.m_listctrl->GetItemCount()==1,"zero results");
    search->SetValue("");
    for(unsigned i=0;i<categories->GetCount();++i) d.Check(i,true);
    d.DoFilter();
    require(d.m_listctrl->GetItemCount()==static_cast<long>(sets.size()) && calls==before,"recovery equips or loses rows");
    require(queries==openingQueries,"UI performed SQL queries");
    if(transmog) {
      bool hateful=false, sabellian=false; int cosmic=0, astral=0, retainedDonors=0;
      for(const auto& set:sets) {
        if(set.transmog && set.name.contains("Hateful") && set.name.contains("Chain")) {
          for(const auto& slot:set.equipmentSlots) for(const auto& item:slot.items) if(item.id==41215) hateful=true;
          require(set.category==ItemSetCategory::Mail,"Hateful Chain classification");
        }
        if(set.transmog && set.id==2671) for(const auto& slot:set.equipmentSlots) if(slot.id==CS_CHEST) sabellian=slot.items.size()>=2;
        if(set.transmog && (set.id==2352 || set.id==2353)) ++cosmic;
        if(set.transmog && (set.id==4102 || set.id==4103 || set.id==4109 || set.id==4115 || set.id==4116 || set.id==4122)) ++astral;
        if(set.transmog && (set.id==3216 || set.id==3218 || set.id==3220 || set.id==1731 || set.id==1735)) {
          ++retainedDonors; require(set.equipmentSlots.size()>=8,"complete Verdant/Dread donor lost pieces");
        }
        require(!set.transmog || set.id<3608 || set.id>3622,"Draconic fragments should be omitted despite the Verdant donor name");
        require(!set.transmog || (set.id!=1706 && set.id!=1708 && set.id!=1709 && set.id!=1726 && set.id!=1728 && set.id!=1729),"Dread Vestment fragments should be omitted despite the Plate donor name");
        require(!set.transmog || set.id<4069 || set.id>4083,"Astral fragments should be omitted");
        require(!set.transmog || set.id<2420 || set.id>2424,"Cosmic fragments should be omitted");
      }
      require(retainedDonors==5,"all complete Verdant and Dread donors must remain selectable");
      require(hateful,"missing Hateful shoulders"); require(sabellian,"missing Sabellian alternatives"); require(cosmic==2,"missing complete Cosmic variants"); require(astral==6,"must retain all six complete Astral variants");
    }
    std::cout<<checks<<" checks; "<<legacy<<" ItemSets, "<<transmog<<" transmogs; "<<chosen<<" filtered selections; "<<changedItems<<" item changes; "<<changedAppearances<<" appearance changes; open "<<elapsed<<" ms, "<<openingQueries<<" queries.\n";
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
  sqlite3_close(db); wxTheApp->OnExit(); wxEntryCleanup(); return result;
}
