#pragma once
enum { UPDATE_NPC = 100, UPDATE_SINGLE_ITEM, UPDATE_SET };
#ifdef SET_INTEGRATION
#include "integration.h"
#else
class CharControl
{
public:
  int calls = 0, selected = -1;
  void OnUpdateItem(int, int index) { ++calls; selected = index; }
};
#endif
