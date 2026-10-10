#include "receiver.h"
#include "SettingsControl.h"
#include "GeneralSettings.h"
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/aui/aui.h>
#include <wx/aui/floatpane.h>
#include <sqlite3.h>
#include <stdexcept>
#include <algorithm>
Database GAMEDATABASE;
bool displayItemAndNPCId = false;
wxString gamePath, customDirectoryPath;
wxFrame* g_modelViewer = nullptr;
sqlite3* db = nullptr;
int checks = 0;
void require(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
sqlResult Database::sqlQuery(const QString& sql) {
  sqlResult result;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
  while (sqlite3_step(statement) == SQLITE_ROW) {
    std::vector<QString> row;
    for (int i=0; i<sqlite3_column_count(statement); ++i) {
      auto text = sqlite3_column_text(statement, i);
      row.push_back(text ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString());
    }
    result.values.push_back(row);
  }
  sqlite3_finalize(statement);
  return result;
}
#include "selection.inc"
sqlResult catalog() {
#include "catalog.inc"
}
void addSettings(wxAuiManager& interfaceManager, SettingsControl* settingsControl) {
#include "pane.inc"
}
class App : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(App);
void layout(SettingsControl* settings) {
  settings->SendSizeEvent();
  auto notebook = static_cast<wxNotebook*>(settings->FindWindow(ID_SETTINGS_TABS));
  notebook->SendSizeEvent();
  auto page = static_cast<GeneralSettings*>(notebook->GetPage(0));
  page->SendSizeEvent();
}
int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  if (argc < 2 || !wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  int result = 0;
  try {
    require(sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "read-only database");
    g_modelViewer = new wxFrame(nullptr, wxID_ANY, "Hidden tests");
    auto settings = new SettingsControl(g_modelViewer, ID_SETTINGS_FRAME);
    auto initial = settings->InitialFloatingSize();
    settings->SetSize(initial); layout(settings);
    auto page = static_cast<GeneralSettings*>(settings->FindWindow(ID_GENERAL_SETTINGS));
    auto apply = page->FindWindow(ID_GENERAL_SETTINGS_APPLY);
    wxWindow* ids = nullptr;
    for (auto child : page->GetChildren())
      if (child->GetId()==ID_SETTINGS_DISPLAYIDINLIST && wxDynamicCast(child, wxCheckBox)) ids=child;
    require(ids!=nullptr,"ID checkbox exists");
    std::cout << "Initial settings " << initial.x << "x" << initial.y << "; content " << page->GetVirtualSize().y << "; client " << page->GetClientSize().y << '\n';
    std::cout << "Apply y=" << apply->GetPosition().y << " h=" << apply->GetSize().y << "; IDs y=" << ids->GetPosition().y << " h=" << ids->GetSize().y << "; natural=" << page->GetSizer()->GetMinSize().y << '\n';
    require(apply->GetRect().GetBottom() < page->GetClientSize().y, "initial Apply visible");
    require(ids->GetRect().GetBottom() < page->GetClientSize().y, "initial IDs visible");
    int oldWidth = page->GetClientSize().x;
    settings->SetSize(initial + wxSize(220,100)); layout(settings);
    require(page->GetClientSize().x >= oldWidth+200, "notebook/page expands");
    for (auto child : page->GetChildren())
      if (wxDynamicCast(child, wxTextCtrl)) require(child->GetSize().x > oldWidth, "text fields expand");
    settings->SetSize(wxSize(initial.x, 350)); layout(settings);
    require(page->GetVirtualSize().y > page->GetClientSize().y, "small panel scroll range");
    require(page->HasScrollbar(wxVERTICAL), "vertical scrollbar present");
    page->Scroll(0, 10000);
    require(page->GetViewStart().y > 0, "scroll reaches bottom");
    require(apply->GetRect().GetBottom() < page->GetClientSize().y, "Apply reachable after scroll");
    settings->SetSize(initial); layout(settings); page->Scroll(0,0);
    require(!page->HasScrollbar(wxVERTICAL), "scrollbar removed when space returns");
    {
      wxAuiManager manager(g_modelViewer);
      addSettings(manager, settings);
      auto pane=manager.GetPane(settings);
      require(pane.IsResizable(),"production AUI pane is resizable");
      require(pane.floating_size==initial,"production initial floating size");
      auto floating=new wxAuiFloatingFrame(g_modelViewer,&manager,pane);
      floating->SetPaneWindow(pane);
      require((floating->GetWindowStyle() & wxRESIZE_BORDER)!=0,"native resize border");
      require(!floating->IsShown(),"test frame stays hidden");
      floating->SetSize(initial); floating->Layout(); floating->GetAuiManager().Update(); layout(settings);
      require(apply->GetRect().GetBottom()<page->GetClientSize().y,"Apply fits including native frame and notebook chrome");
      manager.DetachPane(settings);
      manager.UnInit();
      delete floating;
    }
    auto rows = catalog();
    std::cout << "Catalog loaded " << rows.values.size() << '\n';
    std::set<int> unique;
    bool jaina=false;
    for (const auto& row : rows.values) {
      ItemRecord item(row); require(unique.insert(item.id).second, "no duplicate item rows");
      if(item.id==153575) { jaina=true; require(item.name.isEmpty(), "Jaina has no ItemSparse name"); require(item.type==IT_2HANDED, "Jaina inventory type"); }
      items.items.push_back(item);
    }
    require(jaina, "Jaina admitted into catalog");
    auto link = GAMEDATABASE.sqlQuery("SELECT A.ItemDisplayInfoID FROM ItemModifiedAppearance M JOIN ItemAppearance A ON A.ID=M.ItemAppearanceID WHERE M.ItemID=153575");
    require(!link.empty() && link.values[0][0].toInt()==185141, "Jaina DisplayID");
    CharControl cc;
    for(bool show : {false,true}) {
      displayItemAndNPCId=show;
      for (int slot : {int(CS_HAND_LEFT),int(CS_HAND_RIGHT)}) {
        std::cout << "Picker IDs=" << show << " slot=" << slot << '\n';
        cc.selectItem(UPDATE_ITEM, slot, L"Weapons");
        std::cout << "Picker built rows=" << cc.choices.size() << '\n';
        auto dialog = static_cast<FilteredChoiceDialog*>(cc.itemDialog);
        auto search = static_cast<wxTextCtrl*>(dialog->FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT));
        require(search!=nullptr,"search control exists");
        search->SetValue("153575");
        std::cout << "Filtered rows=" << dialog->m_listctrl->GetItemCount() << '\n';
        require(dialog->m_listctrl->GetItemCount()==1, "ID match independent of display preference");
        require(dialog->m_listctrl->GetItemText(0)==(show ? "Unnamed item [153575]" : "Unnamed item"), "unnamed label and ID visibility");
        dialog->m_listctrl->Select(0);
        require(cc.selectedId==153575, "filtered selection delivers ItemID");
        auto categories = static_cast<wxCheckListBox*>(dialog->FindWindow(CategoryChoiceDialog::ID_CAT_LIST));
        require(categories!=nullptr, "weapon categories");
        for(unsigned i=0;i<categories->GetCount();++i) categories->Check(i,false);
        dialog->DoFilter(); require(dialog->m_listctrl->GetItemCount()==0,"category excludes ID match");
        for(unsigned i=0;i<categories->GetCount();++i) categories->Check(i,true);
        search->SetValue("jaina");
        require(dialog->m_listctrl->GetItemCount()>0,"name search retained");
        search->SetValue("*jAiNa*"); require(dialog->m_listctrl->GetItemCount()>0,"case-insensitive wildcard search");
        search->SetValue("no-such-item-abcdef"); require(dialog->m_listctrl->GetItemCount()==0,"no results");
      }
      cc.selectItem(UPDATE_ITEM,CS_HEAD,L"Head");
      auto dialog=static_cast<FilteredChoiceDialog*>(cc.itemDialog);
      static_cast<wxTextCtrl*>(dialog->FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT))->SetValue("153575");
      require(dialog->m_listctrl->GetItemCount()==0,"slot excludes weapon");
    }
    // Exercise the no-category picker too, retaining its existing None row.
    cc.ClearItemDialog();
    ItemRecord staff=items.getById(153575);
    items.items.resize(1); items.items.push_back(staff);
    for(bool show : {false,true}) {
      displayItemAndNPCId=show;
      cc.selectItem(UPDATE_ITEM,CS_HAND_RIGHT,L"Only staves");
      auto dialog=static_cast<FilteredChoiceDialog*>(cc.itemDialog);
      require(dialog->FindWindow(CategoryChoiceDialog::ID_CAT_LIST)==nullptr,"single category fallback");
      static_cast<wxTextCtrl*>(dialog->FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT))->SetValue("153575");
      require(dialog->m_listctrl->GetItemCount()==2,"fallback ID search keeps None");
      dialog->m_listctrl->Select(1); require(cc.selectedId==153575,"fallback delivers correct ItemID");
    }
    cc.ClearItemDialog();
    // Missing ItemSparse rows, empty/whitespace names, bad appearance links,
    // non-equipment, and duplicate modifiers must all have explicit coverage.
    sqlite3* realDb=db;
    require(sqlite3_open(":memory:",&db)==SQLITE_OK,"synthetic DB");
    const char* fixture=
      "CREATE TABLE Item(ID,InventoryType,ClassID,SubclassID,SheatheType);"
      "CREATE TABLE ItemSparse(ID,Display_Lang);"
      "CREATE TABLE ItemModifiedAppearance(ItemID,ItemAppearanceID);"
      "CREATE TABLE ItemAppearance(ID,ItemDisplayInfoID);"
      "CREATE TABLE ItemDisplayInfo(ID);"
      "INSERT INTO Item VALUES(1,17,2,10,0),(2,17,2,10,0),(3,17,2,10,0),(4,17,2,10,0),"
      "(5,17,2,10,0),(6,0,2,10,0),(7,17,2,10,0),(8,17,2,10,0);"
      "INSERT INTO ItemSparse VALUES(1,'Named without appearance'),(3,''),(4,'   '),(5,''),(6,''),(7,''),(8,'');"
      "INSERT INTO ItemModifiedAppearance VALUES(2,20),(2,20),(3,20),(4,20),(5,99),(6,20),(7,21),(8,22);"
      "INSERT INTO ItemAppearance VALUES(20,30),(21,0),(22,999);"
      "INSERT INTO ItemDisplayInfo VALUES(30),(0);";
    require(sqlite3_exec(db,fixture,nullptr,nullptr,nullptr)==SQLITE_OK,"create catalog fixture");
    auto synthetic=catalog();
    std::set<int> accepted;
    for(const auto& row:synthetic.values) accepted.insert(row[0].toInt());
    require(synthetic.values.size()==4 && accepted==std::set<int>({1,2,3,4}),"unnamed validity, named compatibility and deduplication");
    sqlite3_close(db); db=realDb;
    std::cout << "PASS " << checks << " checks; catalog " << rows.values.size() << " rows\n";
  } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result=1; }
  delete g_modelViewer;
  if(db) sqlite3_close(db);
  wxTheApp->OnExit(); wxEntryCleanup();
  return result;
}
