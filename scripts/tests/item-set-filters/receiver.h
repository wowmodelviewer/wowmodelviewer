#pragma once
enum { UPDATE_NPC = 100, UPDATE_SINGLE_ITEM, UPDATE_SET };
class CharControl
{
public:
  int calls = 0, selected = -1;
  void OnUpdateItem(int, int index) { ++calls; selected = index; }
};
