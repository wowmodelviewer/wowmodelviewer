#include "EquipmentExportDialog.h"
#include "WoWModel.h"
#include <wx/scrolwin.h>
#include <wx/popupwin.h>
#include <QJsonArray>
#include <fstream>
#include <stdexcept>
#include <iostream>

class TestApp : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(TestApp);
static void require(bool yes, const char * message) { if (!yes) throw std::runtime_error(message); }
template<class T> static T * control(wxWindow & dialog, const char * label)
{
  auto * result = dynamic_cast<T *>(wxWindow::FindWindowByLabel(label, &dialog));
  require(result != nullptr, label); return result;
}
static void check(wxCheckBox * check, bool value)
{
  check->SetValue(value);
  wxCommandEvent event(wxEVT_CHECKBOX, check->GetId()); event.SetEventObject(check);
  check->GetEventHandler()->ProcessEvent(event);
}
int main(int argc, char ** argv)
{
  if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  int status = 0;
  try
  {
    WoWItem helm{CS_HEAD,100}, chest{CS_CHEST,101}, weapon{CS_HAND_LEFT,102};
    for (int mode = 0; mode < 4; ++mode)
    {
      WoWModel model;
      if (mode & 1) { model.push_back(&helm); model.push_back(&chest); }
      if (mode & 2) model.push_back(&weapon);
      EquipmentExportDialog dialog(nullptr, &model, "Battlelord's Plate");
      dialog.Layout();
      require(!dialog.IsShown(), "dialog must remain hidden in tests");
      auto * armor = control<wxCheckBox>(dialog, "Export armor");
      auto * weapons = control<wxCheckBox>(dialog, "Export weapons (one folder per weapon)");
      auto * asSet = control<wxRadioButton>(dialog, "As a set");
      auto * perItem = control<wxRadioButton>(dialog, "One folder per item");
      auto * exportButton = dialog.FindWindow(wxID_OK);
      require(armor->IsEnabled() == bool(mode & 1), "armor availability");
      require(weapons->IsEnabled() == bool(mode & 2), "weapon availability");
      require(exportButton->IsEnabled() == (mode != 0), "default export availability");
      auto request = dialog.request();
      require(request["slots"].toArray().size() == model.size(), "all equipped items selected");
      require(request["bodies"].toArray().size() == 1, "current body selected");
      require(request["asSet"].toBool() && request["setName"].toString() == "Battlelord's Plate", "editable set suggestion");
      wxScrolledWindow * races = nullptr;
      wxTextCtrl * name = nullptr;
      std::vector<wxCheckListBox *> lists;
      for (auto * child : dialog.GetChildren())
      {
        if (auto * c = dynamic_cast<wxScrolledWindow *>(child)) races = c;
        if (auto * c = dynamic_cast<wxTextCtrl *>(child)) name = c;
        if (auto * c = dynamic_cast<wxCheckListBox *>(child)) lists.push_back(c);
      }
      require(races && name && lists.size() == 2, "two item lists and race scroller");
      require(races->GetVirtualSize().y > races->GetClientSize().y, "race list scrolls vertically");
      std::vector<wxString> labels;
      for (auto * child : races->GetChildren())
        if (auto * label = dynamic_cast<wxStaticText *>(child)) labels.push_back(label->GetLabel());
      require(labels.size() == 23 && labels[3] == "Human" && labels[10] == "Troll" && labels[11] == "Blood Elf" && labels.back() == "Earthen", "expansion order, no NPCs or expansion headers");
      for (auto * child : races->GetChildren())
      {
        const auto rect = child->GetRect();
        require(rect.GetRight() < races->GetClientSize().x, "race columns fit without horizontal clipping");
      }
      if (mode == 3)
      {
        std::vector<std::pair<wxWindow *, wxRect>> originalRects;
        for (auto * child : dialog.GetChildren())
          if (!child->IsTopLevel()) originalRects.push_back({child, child->GetRect()});
        for (const auto & invalid : {"Bad<name", "Bad>name", "Bad:name", "Bad\"name", "Bad/name", "Bad\\name", "Bad|name", "Bad?name", "Bad*name", "CON", "nul.txt", "COM1", "LPT9", "End.", "End ", ".", ".."})
        {
          name->SetValue(invalid);
          require(!exportButton->IsEnabled(), "invalid Windows name blocks export");
          for (const auto & entry : originalRects)
            require(entry.first->GetRect() == entry.second, "validation must not move or resize dialog controls");
        }
        name->SetValue(wxString('x', 81)); require(!exportButton->IsEnabled(), "too-long name blocked without truncation");
        name->SetValue(wxString::FromUTF8("Armadura del campe\xc3\xb3n")); require(exportButton->IsEnabled(), "accents allowed");
        name->SetValue("Oathclaw_Wargarb"); require(exportButton->IsEnabled(), "underscores allowed");
        name->SetValue("Oathclaw Wargarb"); require(exportButton->IsEnabled(), "spaces allowed");
        require(dialog.request()["setName"].toString() == "Oathclaw Wargarb", "name preserved");
        name->SetValue("Bad:name");
        require(control<wxStaticText>(dialog, "Set name contains invalid characters: < > : \" / \\ | ? *") != nullptr, "full invalid character list");
        wxPopupWindow * notice = nullptr;
        for (auto * child : dialog.GetChildren())
          if (auto * popup = dynamic_cast<wxPopupWindow *>(child)) notice = popup;
        require(notice && !notice->IsShown(), "hidden dialog cannot expose a floating notice");
        for (auto * target : std::vector<wxWindow *>{name, notice, notice->GetChildren().GetFirst()->GetData()})
        {
          wxMouseEvent click(wxEVT_LEFT_DOWN);
          click.SetEventObject(target);
          target->GetEventHandler()->ProcessEvent(click);
          require(!notice->IsShown() && !exportButton->IsEnabled(), "dismissal must not allow an invalid export");
        }
        for (const auto & entry : originalRects)
          require(entry.first->GetRect() == entry.second, "dismissal leaves layout unchanged");
        name->SetValue("  ");
        for (unsigned i = 0; i < lists[0]->GetCount(); ++i) lists[0]->Check(i, false);
        wxCommandEvent items(wxEVT_CHECKLISTBOX, lists[0]->GetId()); items.SetEventObject(lists[0]); lists[0]->GetEventHandler()->ProcessEvent(items);
        require(exportButton->IsEnabled(), "weapons alone need no set name when armor items are unchecked");
        for (unsigned i = 0; i < lists[0]->GetCount(); ++i) lists[0]->Check(i, true);
        name->SetValue("Battlelord's Plate");
        check(armor, false); require(dialog.request()["slots"].toArray() == QJsonArray{CS_HAND_LEFT}, "weapons alone");
        require(exportButton->IsEnabled() && !asSet->IsEnabled() && !name->IsEnabled(), "weapons alone enabled");
        check(weapons, false); require(!exportButton->IsEnabled(), "nothing selected disabled");
        check(armor, true); require(exportButton->IsEnabled() && dialog.request()["slots"].toArray().size() == 2, "armor alone");
        name->SetValue("  "); require(!exportButton->IsEnabled(), "blank set name disabled");
        perItem->SetValue(true);
        wxCommandEvent radio(wxEVT_RADIOBUTTON, perItem->GetId()); radio.SetEventObject(perItem); perItem->GetEventHandler()->ProcessEvent(radio);
        require(exportButton->IsEnabled() && !name->IsEnabled() && !dialog.request()["asSet"].toBool(), "individual pieces need no name");
        for (auto * child : races->GetChildren()) if (auto * c = dynamic_cast<wxCheckBox *>(child)) check(c, false);
        require(!exportButton->IsEnabled(), "zero bodies disabled");
        dialog.SetClientSize(dialog.GetClientSize() + wxSize(200, 100)); dialog.Layout();
        require(!dialog.IsShown(), "resizing must not open the dialog");
      }
    }
  }
  catch (const std::exception & e) { status = 1; std::cerr << e.what() << '\n'; }
  wxTheApp->OnExit(); wxEntryCleanup();
  if (argc > 1) std::ofstream(argv[1]) << (status ? "FAIL" : "PASS");
  return status;
}
