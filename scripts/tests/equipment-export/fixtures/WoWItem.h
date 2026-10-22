#pragma once
#include <QString>
enum { CS_HEAD=0, CS_CHEST=6, CS_HAND_RIGHT=9, CS_HAND_LEFT=10 };
struct WoWItem
{
  int slot_, item_;
  bool isEquipped() const { return item_ > 0; }
  int slot() const { return slot_; }
  int id() const { return item_; }
  int displayId() const { return item_ + 100; }
  QString name() const { return QString("Item %1").arg(item_); }
};
