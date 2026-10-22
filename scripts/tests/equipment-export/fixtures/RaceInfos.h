#pragma once
#include <vector>
#include <string>
struct RaceInfos
{
  int raceID = 4, sexID = 0;
  int modelFileID = -1;
  bool isNPC = false;
  struct RaceMenuEntry { int raceID; std::string name; bool isNPC; int maleFileID, femaleFileID; };
  static std::vector<RaceMenuEntry> getRaceMenu()
  {
    return {{84,"Earthen",false,84,85},{10,"Blood Elf",false,10,11},{4,"Night Elf",false,4,5},
      {1,"Human",false,1,2},{2,"Orc",false,2,3},{3,"Dwarf",false,3,4},{5,"Undead",false,5,6},
      {6,"Tauren",false,6,7},{7,"Gnome",false,7,8},{8,"Troll",false,8,9},{11,"Draenei",false,11,12},
      {9,"Goblin",false,9,10},{22,"Worgen",false,22,23},{24,"Pandaren",false,24,25},
      {27,"Nightborne",false,27,28},{28,"Highmountain Tauren",false,28,29},{29,"Void Elf",false,29,30},
      {30,"Lightforged Draenei",false,30,31},{34,"Dark Iron Dwarf",false,34,35},
      {52,"Dracthyr",false,52,-1},{999,"NPC",true,999,1000}};
  }
  static bool getRaceInfosForRaceSex(int race, int sex, RaceInfos & out)
  {
    for (const auto & entry : getRaceMenu()) if (entry.raceID == race)
    {
      out.raceID = race; out.sexID = sex; out.isNPC = entry.isNPC;
      out.modelFileID = sex ? entry.femaleFileID : entry.maleFileID;
      return out.modelFileID > 0;
    }
    return false;
  }
};
