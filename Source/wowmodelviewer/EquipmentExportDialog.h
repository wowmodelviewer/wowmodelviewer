#pragma once
#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/radiobut.h>
#include <QJsonObject>
#include <vector>
class WoWModel;
class wxPopupWindow;

class EquipmentExportDialog : public wxDialog
{
public:
  EquipmentExportDialog(wxWindow * parent, WoWModel * model, const QString & suggestedName = QString());
  QJsonObject request() const;
private:
  struct Body { int race, sex; wxCheckBox * check; };
  wxCheckBox * armor_, * weapons_;
  wxCheckListBox * armorItems_, * weaponItems_;
  wxRadioButton * set_, * individual_;
  wxTextCtrl * name_;
  wxStaticText * nameError_;
  wxPopupWindow * nameNotice_;
  wxString dismissedName_;
  bool nameNoticeDismissed_ = false;
  void dismissNameNotice();
  std::vector<int> armorSlots_, weaponSlots_;
  std::vector<Body> bodies_;
  void update();
};
