#include "ItemSetCatalog.h"
#include <iostream>
#include <stdexcept>
void require(bool v, const char* text) { if(!v) throw std::runtime_error(text); }
int main() {
  ItemSets::Rows legacy={{"7","Same name","10","4","3","1","Mail helm","100","900","159","0","0","1900"}};
  ItemSets::Rows transmog={
    {"7","Same name","10","4","3","1","Mail helm","101","901","160","1","0","1901"},
    {"7","Same name","10","4","3","1","Mail helm","102","900","159","0","0","1900"},
    {"7","Same name","10","4","3","1","Mail helm","103","901","161","0","0","1901"},
    {"7","Same name","11","4","3","1","Other helm","104","901","160","0","0","1901"},
    {"7","Same name","12","4","1","16","Cloak","105","903","0","1","0","1903"},
    {"8","Incomplete","-1","-1","-1","-1","","","","","1","",""},
    {"9","Mixed","13","4","1","5","Cloth chest","106","904","0","1","0","1904"},
    {"9","Mixed","14","4","4","1","Plate helm","107","905","0","1","0","1905"},
    {"9","Mixed","-1","-1","-1","-1","","","","","1","",""},
    {"10","Other","15","4","5","1","Cosmetic helm","108","906","0","1","0","1906"}
  };
  int calls=0;
  auto sets=ItemSets::read([&](const QString& sql) {
    ++calls;
    if(sql.startsWith("PRAGMA")) return ItemSets::Rows{{"0","TransmogSetGroupID"},{"1","ClassMask"}};
    if(sql.contains("sqlite_master")) return ItemSets::Rows{{"TransmogSet"},{"TransmogSetItem"}};
    if(sql.contains("FROM TransmogSet S")) return transmog;
    require(sql.contains("ItemID17"),"must inspect all legacy pieces"); return legacy;
  });
  require(calls==4 && sets.size()==6,"source identities and bounded queries");
  for(const auto& set:sets) {
    if(set.id==7) {
      require(set.category==ItemSetCategory::Mail,"cloak must not change material");
      if(set.transmog) {
        require(set.equipmentSlots.size()==2,"head and cloak");
        const auto& head=set.equipmentSlots.front();
        require(head.items.size()==2,"different ItemIDs sharing a look are retained");
        require(head.items[0].id==10 && head.items[0].appearances.size()==2,"appearance deduplication");
        require(head.items[0].appearances[0].id==901 && head.items[0].appearances[0].sourceId==101,"preferred source wins deterministically");
      } else require(set.equipmentSlots.front().items.front().appearances.size()==1,"no cross-source alternatives");
    }
    if(set.id==8) require(set.incomplete && set.category==ItemSetCategory::Unknown && set.equipmentSlots.empty(),"missing source unknown");
    if(set.id==9) require(set.incomplete && set.category==ItemSetCategory::Mixed,"known mixed survives missing source");
    if(set.id==10) require(set.category==ItemSetCategory::Other,"cosmetic other");
  }
  ItemSets::Rows related;
  auto add = [&](int setId, int group, int mask, int source, int inventory, const char* name = "Related") {
    related.push_back({QString::number(setId),name,QString::number(source),"4","3",QString::number(inventory),"Piece",
      QString::number(source),QString::number(source),"0","1","0",QString::number(source),QString::number(group),QString::number(mask)});
  };
  const int inventory[] = {1,3,5,7,8};
  for(int i=0;i<5;++i) {
    add(100,5,4,1000+i,inventory[i]); add(200,5,4,2000+i,inventory[i]);
    add(201,6,4,3000+i,inventory[i]); add(304,0,4,1000+i,inventory[i]);
  }
  add(300,0,4,1000,1); add(300,0,4,2000,1); // Covered by variants in the SAME group.
  add(301,0,4,9999,1); // Unique source must remain.
  add(302,0,64,1000,1); // Different class relationship must remain.
  add(303,0,4,1000,1); add(303,0,4,-1,-1); // Missing data must remain.
  add(305,7,4,1000,1); // Grouped record must remain.
  add(306,0,4,1000,1,"Different name");
  add(307,0,4,1000,1); add(307,0,4,3000,1); // Cannot combine unrelated groups.
  auto filtered=ItemSets::read([&](const QString& sql) {
    if(sql.startsWith("PRAGMA")) return ItemSets::Rows{{"0","TransmogSetGroupID"},{"1","ClassMask"}};
    if(sql.contains("sqlite_master")) return ItemSets::Rows{{"TransmogSet"},{"TransmogSetItem"}};
    if(sql.contains("FROM TransmogSet S")) return related;
    return ItemSets::Rows{};
  });
  for(const auto& set:filtered) require(set.id!=300 && set.id!=306,"covered reward fragments must be omitted even with different names");
  for(int id : {100,200,201,301,302,303,304,305,307}) {
    bool found=false; for(const auto& set:filtered) found |= set.id==id;
    require(found,"unique, incomplete, differently related or full sets must remain");
  }
  require(filtered.size()==10,"only proven redundant records are removed");
  for(auto& row:related) { row[13]="0"; row[14]="0"; }
  auto oldSchema=ItemSets::read([&](const QString& sql) {
    if(sql.startsWith("PRAGMA")) return ItemSets::Rows{{"0","ID"},{"1","Name_Lang"}};
    if(sql.contains("sqlite_master")) return ItemSets::Rows{{"TransmogSet"},{"TransmogSetItem"}};
    if(sql.contains("FROM TransmogSet S")) {
      require(sql.contains(", 0, 0 FROM TransmogSet"),"old schemas need a column-safe fallback");
      return related;
    }
    return ItemSets::Rows{};
  });
  require(oldSchema.size()==12,"missing group metadata must preserve all fragments");
  std::cout<<"Redundant fragment fixtures passed.\n";
  std::cout<<"Catalog fixtures passed: source namespaces, all 17 columns, default preference, deduplication, accessories, missing sources, Mixed and Other.\n";
}
