#include "EquipmentExportDialog.h"
#include "../games/wow/EquipmentFolderName.h"
#include "WoWModel.h"
#include "WoWItem.h"
#include "RaceInfos.h"
#include <wx/scrolwin.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <QJsonArray>
#include <algorithm>

EquipmentExportDialog::EquipmentExportDialog(wxWindow * parent, WoWModel * model, const QString & suggestedName)
  : wxDialog(parent, wxID_ANY, _("Export equipment"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
  const int gap = FromDIP(10);
  auto * root = new wxBoxSizer(wxVERTICAL);
  auto * columns = new wxBoxSizer(wxHORIZONTAL);
  auto * left = new wxBoxSizer(wxVERTICAL);
  armor_ = new wxCheckBox(this, wxID_ANY, _("Export armor"));
  left->Add(armor_, 0, wxBOTTOM, gap);
  armorItems_ = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(320, 190)), 0, nullptr, wxLB_HSCROLL);
  weaponItems_ = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(320, 65)), 0, nullptr, wxLB_HSCROLL);
  for (auto * item : *model)
    if (item->isEquipped())
    {
      const bool weapon = item->slot() == CS_HAND_RIGHT || item->slot() == CS_HAND_LEFT;
      auto * list = weapon ? weaponItems_ : armorItems_;
      (weapon ? weaponSlots_ : armorSlots_).push_back((int)item->slot());
      const wxString label = wxString(item->name().toStdWString()) + wxString::Format(" [%d / %d]", item->id(), item->displayId());
      list->Check(list->Append(label));
    }
  left->Add(armorItems_, 1, wxEXPAND | wxBOTTOM, gap);
  set_ = new wxRadioButton(this, wxID_ANY, _("As a set"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  individual_ = new wxRadioButton(this, wxID_ANY, _("One folder per item"));
  left->Add(set_, 0, wxBOTTOM, gap / 2);
  name_ = new wxTextCtrl(this, wxID_ANY, suggestedName.isEmpty() ? _("Armor set") : wxString(suggestedName.toStdWString()));

  left->Add(name_, 0, wxEXPAND | wxBOTTOM, gap);
  nameNotice_ = new wxPopupWindow(this, wxBORDER_SIMPLE);
  nameNotice_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
  nameError_ = new wxStaticText(nameNotice_, wxID_ANY, wxEmptyString);
  nameError_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
  auto * noticeSizer = new wxBoxSizer(wxVERTICAL);
  noticeSizer->Add(nameError_, 0, wxALL, gap);
  nameNotice_->SetSizer(noticeSizer);
  const auto dismiss = [this](wxMouseEvent & event) { dismissNameNotice(); event.Skip(); };
  name_->Bind(wxEVT_LEFT_DOWN, dismiss);
  nameNotice_->Bind(wxEVT_LEFT_DOWN, dismiss);
  nameError_->Bind(wxEVT_LEFT_DOWN, dismiss);
  left->Add(individual_, 0, wxBOTTOM, gap * 2);
  weapons_ = new wxCheckBox(this, wxID_ANY, _("Export weapons (one folder per weapon)"));
  left->Add(weapons_, 0, wxBOTTOM, gap);
  left->Add(weaponItems_, 0, wxEXPAND);
  armor_->SetValue(!armorSlots_.empty());
  armor_->Enable(!armorSlots_.empty());
  weapons_->SetValue(!weaponSlots_.empty());
  weapons_->Enable(!weaponSlots_.empty());
  columns->Add(left, 1, wxRIGHT | wxEXPAND, gap * 2);

  auto * right = new wxBoxSizer(wxVERTICAL);
  auto * bodiesTitle = new wxStaticText(this, wxID_ANY, _("Bodies"));
  bodiesTitle->SetFont(bodiesTitle->GetFont().Bold());
  right->Add(bodiesTitle, 0, wxBOTTOM, gap);
  auto * scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(240, 420)));
  scroll->SetScrollRate(0, FromDIP(18));
  auto * table = new wxFlexGridSizer(3, gap, gap);
  table->AddGrowableCol(0);
  for (const auto & title : {_("Race"), _("Male"), _("Female")})
    table->Add(new wxStaticText(scroll, wxID_ANY, title));
  auto races = RaceInfos::getRaceMenu();
  const std::vector<int> order = {1,2,3,4,5,6,7,8,10,11,9,22,24,25,26,27,28,29,30,34,36,31,32,35,37,52,70,84,85};
  const auto rank = [&](int id) { auto p = std::find(order.begin(), order.end(), id); return p == order.end() ? 1000 + id : (int)(p - order.begin()); };
  std::stable_sort(races.begin(), races.end(), [&](const RaceInfos::RaceMenuEntry & a, const RaceInfos::RaceMenuEntry & b) { return rank(a.raceID) < rank(b.raceID); });
  for (const auto & race : races)
  {
    if (race.isNPC) continue;
    table->Add(new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(race.name.c_str())), 0, wxALIGN_CENTER_VERTICAL);
    for (int sex = 0; sex < 2; ++sex)
    {
      auto * check = new wxCheckBox(scroll, wxID_ANY, wxEmptyString);
      RaceInfos body;
      check->Enable(RaceInfos::getRaceInfosForRaceSex(race.raceID, sex, body) && body.modelFileID > 0 && !body.isNPC);
      check->SetValue(check->IsEnabled() && race.raceID == model->infos.raceID && sex == model->infos.sexID);
      table->Add(check, 0, wxALIGN_CENTER);
      bodies_.push_back({race.raceID, sex, check});
      check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { update(); });
    }
  }
  scroll->SetSizer(table);
  right->Add(scroll, 1, wxEXPAND);
  columns->Add(right, 0, wxEXPAND);
  root->Add(columns, 1, wxEXPAND | wxALL, gap * 2);
  auto * buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
  root->Add(buttons, 0, wxEXPAND | wxALL, gap);
  FindWindow(wxID_OK)->SetLabel(_("Export"));
  SetSizerAndFit(root);
  SetMinSize(GetSize());
  CentreOnParent();
  Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { update(); });
  Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { update(); });
  Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent &) { update(); });
  name_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update(); });
  Bind(wxEVT_SHOW, [this](wxShowEvent & event) {
    if (event.IsShown()) CallAfter([this]() { update(); });
    else nameNotice_->Hide();
    event.Skip();
  });
  Bind(wxEVT_ACTIVATE, [this](wxActivateEvent & event) {
    if (!event.GetActive()) nameNotice_->Hide();
    event.Skip();
  });
  Bind(wxEVT_MOVE, [this](wxMoveEvent & event) { nameNotice_->Hide(); event.Skip(); });
  Bind(wxEVT_SIZE, [this](wxSizeEvent & event) { nameNotice_->Hide(); event.Skip(); });
  update();
}

void EquipmentExportDialog::dismissNameNotice()
{
  dismissedName_ = name_->GetValue();
  nameNoticeDismissed_ = true;
  nameNotice_->Hide();
}

void EquipmentExportDialog::update()
{
  armorItems_->Enable(armor_->GetValue());
  set_->Enable(armor_->GetValue());
  individual_->Enable(armor_->GetValue());
  name_->Enable(armor_->GetValue() && set_->GetValue());
  weaponItems_->Enable(weapons_->GetValue());
  const auto r = request();
  const auto selected = r["slots"].toArray();
  const bool hasArmor = std::any_of(selected.begin(), selected.end(), [](const QJsonValue & slot) {
    return slot.toInt() != CS_HAND_RIGHT && slot.toInt() != CS_HAND_LEFT;
  });
  const QString error = hasArmor && set_->GetValue() ? equipmentSetNameError(r["setName"].toString()) : QString();
  nameError_->SetLabel(wxString(error.toStdWString()));
  nameError_->Wrap(FromDIP(320));
  nameNotice_->GetSizer()->Fit(nameNotice_);
  if (error.isEmpty() || dismissedName_ != name_->GetValue()) nameNoticeDismissed_ = false;
  if (!error.isEmpty() && !nameNoticeDismissed_ && IsShownOnScreen())
  {
    nameNotice_->Position(name_->ClientToScreen(wxPoint(0, 0)), name_->GetSize());
    nameNotice_->Show();
  }
  else nameNotice_->Hide();
  const bool validName = error.isEmpty();
  FindWindow(wxID_OK)->Enable(!r["slots"].toArray().isEmpty() && !r["bodies"].toArray().isEmpty() &&
    (!hasArmor || !set_->GetValue() || validName));
}

QJsonObject EquipmentExportDialog::request() const
{
  QJsonArray selectedSlots, bodies;
  if (armor_->GetValue())
    for (size_t i = 0; i < armorSlots_.size(); ++i) if (armorItems_->IsChecked(i)) selectedSlots.append(armorSlots_[i]);
  if (weapons_->GetValue())
    for (size_t i = 0; i < weaponSlots_.size(); ++i) if (weaponItems_->IsChecked(i)) selectedSlots.append(weaponSlots_[i]);
  for (const auto & b : bodies_) if (b.check->IsEnabled() && b.check->GetValue()) bodies.append(QJsonObject{{"race", b.race}, {"sex", b.sex}});
  return QJsonObject{{"version", 1}, {"slots", selectedSlots}, {"bodies", bodies}, {"asSet", set_->GetValue()},
    {"setName", QString::fromStdWString(name_->GetValue().ToStdWstring())}};
}
