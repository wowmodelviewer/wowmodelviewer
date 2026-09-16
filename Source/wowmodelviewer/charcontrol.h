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
class MountCard;

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
  // The name that mount was chosen by -- its row in the mount choice -- and the mountSerial it belongs to. A mount
  // put up any other way (the -unityipctest self-check does) has no name of its own: see ridingMountName.
  wxString mountName;
  unsigned int mountNameSerial = 0;

  wxString customSkin;

  void ClearItemDialog();

  // Only the two hand slots, for a creature or item on the Model panel. See the definition.
  void SetHandsOnly(bool handsOnly);

  // The Mount card at the top of the panel follows the host's mount state again (see MountCard::Sync).
  void RefreshMountCard();

  // A player mount as the mount choice lists it: its name and the CreatureDisplayInfo id it is mounted by.
  struct MountChoice
  {
    wxString name;
    int displayId;
  };

  private:
  bool m_handsOnly = false;
  wxSizer * m_tabardHeader = nullptr;
  wxSizer * m_tabardGrid = nullptr;
  MountCard * m_mountCard = nullptr;
  bool m_itemDialogIsMount = false;   // itemDialog is the mount dialog (selectMount)
  std::vector<MountChoice> m_mountChoices;
  bool m_mountChoicesRead = false;
  // Puts the mount rows in numbers/cats/choices for OnUpdateItem(UPDATE_MOUNT). See the definition.
  void listMountRows();

  public:

  void selectItem(ssize_t type, ssize_t slot, const wxChar *caption = wxT("Item"));
  void selectSet();
  void selectStart();
  void selectMount();
  // The mount dialog's rows, as selectMount lists them: numbers/cats/choices, row 0 "None" (-1), then every player
  // mount by name (mountChoices, category 0), then every creature model file (an index into the file list,
  // category 1). OnUpdateItem(UPDATE_MOUNT, row) takes a row of these; the -unityipctest mount steps pick one
  // the way the dialog does.
  void fillMountChoices();
  // Every player mount, sorted by name: rows 1..N of fillMountChoices, in the same order. Read from the database
  // once for each loaded model and kept, so the Mount card's picker filters it in memory.
  const std::vector<MountChoice> & mountChoices();
  // THE MOUNT CHOICE for a caller that is not the mount dialog (the Mount card): put the character on player mount
  // mountChoices()[index], swapping any mount it rides, or take it off. Both pass OnUpdateItem(UPDATE_MOUNT) the
  // dialog's own row for the choice, so everything that choice does -- the model, its display skin, the serial,
  // the riding sequence, the scale, the Unity scene -- is exactly what the dialog does. False when there is no
  // character or no such mount.
  bool selectMountChoice(size_t index);
  void dismount();
  // The name of the mount the character rides, as the mount choice listed it (a mount put up without one: its
  // file name); empty when it rides none. Read from the host every time, never remembered by the caller.
  wxString ridingMountName() const;
  // The mountChoices() index of the mount the character rides, -1 when it rides none or none lists it.
  int ridingMountChoice();
  void selectNPC(ssize_t type);

  const wxString selectCharModel();
  static QString getItemName(ItemRecord &);

  void onEvent(Event *) override;
};


#endif

