#include "integration.h"
#include <wx/listctrl.h>
#include <iostream>
#include <chrono>
Database GAMEDATABASE;
wxFrame* g_modelViewer = nullptr;
sqlResult Database::sqlQuery(const QString& query)
{
  ++queries;
  sqlResult result;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, query.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    std::vector<QString> row;
    for (int i = 0; i < sqlite3_column_count(stmt); ++i)
      row.push_back(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i))));
    result.values.push_back(row);
  }
  sqlite3_finalize(stmt);
  return result;
}
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
class App : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(App);
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  if (sqlite3_open_v2(argv[1], &GAMEDATABASE.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return 3;
  wxEntryStart(argc, argv); wxTheApp->CallOnInit();
  int result = 0;
  try
  {
    g_modelViewer = new wxFrame(nullptr, wxID_ANY, "Hidden test parent");
    CharControl c;
    Piece oldPiece; WoWModel model{&oldPiece}; c.model = &model;
    const auto start = std::chrono::steady_clock::now();
    c.selectSet();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    require(GAMEDATABASE.queries == 1, "opening must use one bulk query");
    require(c.numbers[0] == -1, "None remains first");
    int counts[7] = {};
    for (size_t i = 1; i < c.cats.size(); ++i) ++counts[c.cats[i]];
    auto* d = static_cast<CategoryChoiceDialog*>(c.itemDialog);
    auto* pattern = static_cast<wxTextCtrl*>(d->FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT));
    auto* checklist = static_cast<wxCheckListBox*>(d->FindWindow(CategoryChoiceDialog::ID_CAT_LIST));
    int verified = 0, beyondEight = 0;
    for (unsigned cat = 0; cat < checklist->GetCount(); ++cat)
    {
      wxCommandEvent event(wxEVT_LISTBOX_DCLICK, CategoryChoiceDialog::ID_CAT_LIST); event.SetInt(cat);
      d->OnCheckDoubleClick(event);
      require(GAMEDATABASE.queries == 1 + verified * 2, "filter must not query");
      require(c.calls == verified, "filter must not equip");
      for (long row = 1; row < d->m_listctrl->GetItemCount(); ++row)
      {
        c.equipped.clear(); oldPiece.id = 123;
        d->m_listctrl->Select(row);
        const int id = c.numbers[c.selected];
        auto expected = GAMEDATABASE.sqlQuery(QString("SELECT * FROM ItemSet WHERE ID=%1").arg(id));
        require(c.equipped.size() == 17, "all 17 positions must be offered to equipment loader");
        for (int slot = 0; slot < 17; ++slot)
        {
          require(c.equipped[slot] == expected.values[0][slot + 2].toInt(), "wrong set/piece after filtering");
          if (slot >= 8 && c.equipped[slot]) ++beyondEight;
        }
        require(oldPiece.id == 0, "previous equipment is cleared on selection");
        ++verified;
      }
    }
    require(c.refreshes == verified * 2, "equipment and model refresh for each selected set");
    const auto equipmentBefore = c.equipped;
    pattern->SetValue("impossible-set-name-xyz");
    require(d->m_listctrl->GetItemCount() == 1 && d->GetSelection() == -1, "zero matches");
    pattern->SetValue("");
    require(c.equipped == equipmentBefore && c.calls == verified, "search cannot change equipment");
    require(verified == c.numbers.size() - 1, "every real set checked exactly once");
    std::cout << verified << " real sets: correct IDs, all 17 piece requests; " << beyondEight
              << " nonzero pieces beyond slot 8. Open " << ms << " ms. Categories:";
    for (int count : counts) std::cout << ' ' << count;
    std::cout << '\n';
    c.ClearItemDialog(); delete g_modelViewer;
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
  sqlite3_close(GAMEDATABASE.db); wxTheApp->OnExit(); wxEntryCleanup(); return result;
}
