#ifndef CHARCONTROL_H
#define CHARCONTROL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

// stl
#include <vector>

// our headers
#include "CharDetails.h"
#include "CharDetailsFrame.h"
#include "database.h"
#include "enums.h"
#include "WoWModel.h"

#include "metaclasses/Observer.h"

// forward class declarations
class ChoiceDialog;
class ModelViewer;

bool slotHasModel(size_t i);

class CharControl : public wxScrolledWindow, public Observer
{
  DECLARE_CLASS(CharControl)
  DECLARE_EVENT_TABLE()

  wxSpinButton *tabardSpins[NUM_TABARD_BTNS];
  wxButton *buttons[NUM_CHAR_SLOTS];
  wxComboBox *levelboxes[NUM_CHAR_SLOTS];
  wxButton *clearButtons[NUM_CHAR_SLOTS]; // small "X" per slot: one-click remove of that item
  wxStaticText *labels[NUM_CHAR_SLOTS];
  wxStaticText *spinTbLabels[NUM_TABARD_BTNS];
  CharDetailsFrame * cdFrame;

  void tryToEquipItem(int id);

  public:
  // Item selection stuff
  ChoiceDialog *itemDialog;
  ssize_t choosingSlot;
  std::vector<int> numbers, cats;
  wxArrayString choices, catnames;

  CharControl(wxWindow* parent, wxWindowID id);
  ~CharControl();

  bool Init();
  void UpdateModel(Attachment *a);

  void RefreshModel();
  void RefreshEquipment();
  inline void RandomiseChar();

  void OnTabardSpin(wxSpinEvent &event);
  void OnCheck(wxCommandEvent &event);
  void OnButton(wxCommandEvent &event);
  void OnClearSlot(wxCommandEvent &event); // "X" next to a slot: remove just that equipped item
  void OnItemLevelChange(wxCommandEvent& event);

  void OnUpdateItem(int type, int id);

  Attachment *charAtt;
  WoWModel *model;

  // The mount the character rides, for the Unity viewport's scene (ModelViewer::SendCharacterSceneToUnity).
  // mountSerial is raised for every mount model the mount choice installs, never on a dismount: it is the
  // mount's identity there, which the model's address cannot be -- the previous mount is freed before its
  // replacement is allocated, so the same address can come back for a different mount. mountDisplayId is the
  // CreatureDisplayInfo id that mount was chosen by: 0 for a creature file, and while no mount is up.
  unsigned int mountSerial = 0;
  int mountDisplayId = 0;

  wxString customSkin;

  void ClearItemDialog();

  // Only the two hand slots, for a creature or item on the Model panel. See the definition.
  void SetHandsOnly(bool handsOnly);

  private:
  bool m_handsOnly = false;
  wxSizer * m_tabardHeader = nullptr;
  wxSizer * m_tabardGrid = nullptr;
  wxSizer * m_mountHeader = nullptr;
  wxWindow * m_mountButton = nullptr;

  public:

  void selectItem(ssize_t type, ssize_t slot, const wxChar *caption = wxT("Item"));
  void selectSet();
  void selectStart();
  void selectMount();
  // The mount dialog's rows, as selectMount lists them: numbers/cats/choices, row 0 "None" (-1), then every player
  // mount by name (its CreatureDisplayInfo id, category 0), then every creature model file (an index into the file
  // list, category 1). OnUpdateItem(UPDATE_MOUNT, row) takes a row of these; the -unityipctest mount steps pick one
  // the way the dialog does.
  void fillMountChoices();
  void selectNPC(ssize_t type);

  const wxString selectCharModel();
  static QString getItemName(ItemRecord &);

  void onEvent(Event *) override;
};


#endif

