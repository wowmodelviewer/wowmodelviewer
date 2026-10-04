#include <QString>
#include "ItemSetCatalog.h"
#include <map>
#include <vector>
#include <iostream>
#include <stdexcept>
struct Result {
  bool valid = true;
  std::vector<std::vector<QString>> values;
  bool empty() const { return values.empty(); }
};
struct Database {
  Result next;
  QString last;
  Result sqlQuery(const QString& sql) { last=sql; return next; }
} GAMEDATABASE;
class WoWItem {
public:
  std::map<int,int> levelDisplayMap_;
  int level_=0, displayId_=0, id_=42, loads=0;
  bool setAppearanceId(int appearanceId);
  void load() { ++loads; }
  void setId(int id) { id_=id; level_=0; displayId_=0; }
};
#include "appearance-method.inc"
struct Model : public std::vector<WoWItem*> {
  WoWItem* getItem(CharSlots slot) { return static_cast<size_t>(slot)<size() ? (*this)[slot] : nullptr; }
};
struct Viewer { int updates=0; void UpdateControls() { ++updates; } } viewer;
auto* g_modelViewer=&viewer;
#define LOG_WARNING std::cerr
class CharControl {
public:
  Model* model=nullptr;
  int equipmentRefreshes=0, modelRefreshes=0;
  void RefreshEquipment() { ++equipmentRefreshes; }
  void RefreshModel() { ++modelRefreshes; }
  void applySetPieces(const std::vector<ItemSets::Equipped>& pieces, bool replaceAll);
};
#include "equipment-method.inc"
void require(bool value) { if(!value) throw std::runtime_error("Appearance application failed"); }
int main() {
  WoWItem item; item.levelDisplayMap_={{0,101},{1,102}};
  GAMEDATABASE.next.values={{"501"}};
  require(item.setAppearanceId(101)); require(item.displayId_==501 && item.level_==0 && item.id_==42 && item.loads==1);
  require(item.setAppearanceId(101)); require(item.loads==1);
  GAMEDATABASE.next.values={{"502"}};
  require(item.setAppearanceId(102)); require(item.displayId_==502 && item.level_==1 && item.loads==2);
  // A modifier changed the display while leaving the legacy level at 1.
  item.displayId_=999;
  require(item.setAppearanceId(102)); require(item.displayId_==502 && item.loads==3);
  require(!item.setAppearanceId(9999)); require(item.displayId_==502 && item.id_==42);
  GAMEDATABASE.next.values.clear();
  require(!item.setAppearanceId(101)); require(item.displayId_==502);
  GAMEDATABASE.next.valid=false;
  require(!item.setAppearanceId(101)); require(item.loads==3);
  GAMEDATABASE.next.valid=true; GAMEDATABASE.next.values={{"502"}};
  WoWItem first, second; first.levelDisplayMap_={{0,101},{1,102}}; second.levelDisplayMap_=first.levelDisplayMap_;
  Model model; model.push_back(&first); model.push_back(&second);
  CharControl control; control.model=&model;
  control.applySetPieces({{0,123,102}},true);
  require(first.id_==123 && first.displayId_==502 && second.id_==0 && viewer.updates==1);
  second.id_=456;
  control.applySetPieces({{0,124,102}},false);
  require(first.id_==124 && second.id_==456 && viewer.updates==2);
  control.applySetPieces({},true);
  require(first.id_==0 && second.id_==0 && viewer.updates==3);
  require(control.equipmentRefreshes==3 && control.modelRefreshes==3);
  control.model=nullptr; control.applySetPieces({},true); require(viewer.updates==3);
  std::cout<<"4 equipment application cases and 7 appearance application cases passed (production method, asset load mocked).\n";
}
