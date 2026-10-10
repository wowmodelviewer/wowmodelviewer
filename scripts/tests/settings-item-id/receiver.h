#pragma once
#include <wx/wx.h>
#include <QString>
#include <set>
#include <iostream>
#include "database.h"
#include "wow_enums.h"
#include "itemselection.h"
#define LOG_INFO std::cerr
#define LOG_ERROR std::cerr
enum { UPDATE_NPC, UPDATE_SINGLE_ITEM, UPDATE_ITEM };
struct sqlResult {
  bool valid = true;
  std::vector<std::vector<QString>> values;
  bool empty() const { return values.empty(); }
};
struct Database { sqlResult sqlQuery(const QString& sql); };
extern Database GAMEDATABASE;
extern bool displayItemAndNPCId;
extern wxString gamePath, customDirectoryPath;
extern wxFrame* g_modelViewer;
class CharControl {
public:
  ChoiceDialog* itemDialog = nullptr;
  ssize_t choosingSlot = 0;
  std::vector<int> numbers, cats;
  wxArrayString choices, catnames;
  int calls = 0, selectedId = -1;
  void ClearItemDialog() { delete itemDialog; itemDialog = nullptr; }
  void selectItem(ssize_t type, ssize_t slot, const wxChar* caption);
  QString getItemName(ItemRecord& item);
  void OnUpdateItem(int, int index) { ++calls; selectedId = numbers.at(index); }
  ~CharControl() { ClearItemDialog(); }
};
