#pragma once
#include "receiver.h"
#include "itemselection.h"
#include "ItemSetCategory.h"
#include <QString>
#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>

struct sqlResult
{
  bool valid = true;
  std::vector<std::vector<QString>> values;
  bool empty() const { return values.empty(); }
};
class Database
{
public:
  sqlite3* db = nullptr;
  int queries = 0;
  sqlResult sqlQuery(const QString& query);
};
extern Database GAMEDATABASE;
extern wxFrame* g_modelViewer;
struct NumStringPair
{
  int id; wxString name;
  bool operator<(const NumStringPair& other) const { return name < other.name; }
};
struct Piece { int id = 123; void setId(int value) { id = value; } };
using WoWModel = std::vector<Piece*>;
class CharControl
{
public:
  int calls = 0, selected = -1, refreshes = 0;
  std::vector<int> numbers, cats, equipped;
  wxArrayString choices, catnames;
  ChoiceDialog* itemDialog = nullptr;
  WoWModel* model = nullptr;
  void ClearItemDialog() { delete itemDialog; itemDialog = nullptr; }
  void selectSet();
  void OnUpdateItem(int type, int id);
  void tryToEquipItem(int id) { equipped.push_back(id); }
  void RefreshEquipment() { ++refreshes; }
  void RefreshModel() { ++refreshes; }
};
