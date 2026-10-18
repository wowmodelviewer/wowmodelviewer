#include "AnimationExportChoiceDialog.h"
#include <fstream>
#include <stdexcept>

class TestApp : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(TestApp);
static void require(bool value) { if (!value) throw std::runtime_error("customization dialog check failed"); }
template<class T> static T * control(wxWindow & dialog, const char * label)
{
  auto * result = dynamic_cast<T *>(wxWindow::FindWindowByLabel(label, &dialog));
  require(result != nullptr);
  return result;
}
static void select(wxRadioButton * radio)
{
  radio->SetValue(true);
  wxCommandEvent event(wxEVT_RADIOBUTTON, radio->GetId());
  event.SetEventObject(radio);
  radio->GetEventHandler()->ProcessEvent(event);
}
int main(int argc, char ** argv)
{
  if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  int status = 0;
  try
  {
    for (bool hasClips : {false, true})
      for (bool supported : {false, true})
      {
        wxArrayString names; wxArrayInt ids;
        if (hasClips) { names.Add("Stand"); ids.Add(0); names.Add("Walk"); ids.Add(4); }
        AnimationExportChoiceDialog dialog(nullptr, "", "Export", names, ids, supported, "Unsupported fixture");
        require(!dialog.IsShown()); // Never opens/focuses the user's desktop.
        auto * current = control<wxRadioButton>(dialog, "Current appearance");
        auto * all = control<wxRadioButton>(dialog, "All customization options");
        auto * equipment = control<wxCheckBox>(dialog, "Include equipment");
        auto * mesh = control<wxCheckBox>(dialog, "Export Mesh");
        auto * skin = control<wxCheckBox>(dialog, "Export Skinning");
        auto * rig = control<wxCheckBox>(dialog, "Export Skeleton");
        auto * anim = control<wxCheckBox>(dialog, "Export Animations");
        require(current->GetValue() && !all->GetValue() && !dialog.exportAllCustomizations());
        require(current->IsEnabled() == supported && all->IsEnabled() == supported);
        require(equipment->GetValue() && !equipment->IsEnabled());
        require(dialog.GetAnimationSelections().size() == names.size());
        if (!supported)
        {
          auto * reason = control<wxStaticText>(dialog, "Unsupported fixture");
          require(reason->IsEnabled() && reason->GetName() == "Customization availability reason");
          continue;
        }
        mesh->SetValue(false); skin->SetValue(false); anim->SetValue(false);
        select(all);
        require(dialog.exportAllCustomizations() && !equipment->GetValue() && equipment->IsEnabled());
        require(mesh->GetValue() && skin->GetValue() && rig->GetValue());
        require(!mesh->IsEnabled() && !skin->IsEnabled() && !rig->IsEnabled());
        require(!dialog.exportAnimations() && anim->IsEnabled());
        equipment->SetValue(true); select(current);
        require(!mesh->GetValue() && !skin->GetValue() && rig->GetValue());
        require(mesh->IsEnabled() && skin->IsEnabled() && rig->IsEnabled());
        require(equipment->GetValue() && !equipment->IsEnabled());
        select(all); require(equipment->GetValue());
        equipment->SetValue(false); select(current); select(all);
        require(!equipment->GetValue() && !dialog.exportAnimations());
        require(dialog.GetAnimationSelections().size() == names.size());
      }
  }
  catch (...) { status = 1; }
  wxTheApp->OnExit(); wxEntryCleanup();
  if (argc > 1) std::ofstream(argv[1]) << (status == 0 ? "PASS" : "FAIL");
  return status;
}
