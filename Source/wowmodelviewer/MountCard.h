/*
 * MountCard.h
 *
 * The Mount card: the first thing on the Model panel's Appearance page for a playable character. It
 * says which mount the character rides, and puts one click between the user and choosing, changing or
 * leaving one. "Choose Mount" / "Change Mount" opens a search field over the list of player mounts in a
 * small window floating just below the button, so the page under it neither moves nor grows: typing
 * filters the list at once, Up and Down move the highlight, and Enter or a click on a row mounts it
 * straight away -- there is no OK -- and closes the list; Escape or a click anywhere else closes it with
 * nothing changed. "Dismount" takes the character off.
 *
 * It owns no mount state. What it shows is read from the host every time it syncs
 * (ModelViewer::riderMount, CharControl::ridingMountName), and what it does is the mount choice the
 * Character > Mount / Dismount dialog makes (CharControl::selectMountChoice, dismount), so the dialog,
 * the card and the viewport always agree.
 */

#ifndef MOUNTCARD_H
#define MOUNTCARD_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <vector>

class CharControl;
class MountPickerList;
class MountPickerPopup;
class WoWModel;
class wxSearchCtrl;

class MountCard : public wxPanel
{
public:
  MountCard(wxWindow * parent, CharControl * owner);

  // Show what the host holds now: nothing unless characterPanel is set and the character is a playable
  // one; otherwise the unmounted state, or the mounted state with the mount's name. An open list is closed
  // when the character or its mount changed under it. Cheap when nothing changed.
  void Sync(bool characterPanel);

  // The text of a row the list shows (it asks only for the rows on screen).
  wxString RowText(long row) const;

private:
  friend class MountPickerPopup;

  void OpenPicker();
  void ClosePicker(bool focusButton);
  bool PickerClickedOutside(const wxWindow * target);
  void PickerDismissed();
  void ApplyFilter();
  void ShowHighlight();
  void MoveHighlight(int step);
  void ChooseRow(long row);
  void OnPickerKey(wxKeyEvent & event);
  bool SearchHasFocus() const;
  void OnPaint(wxPaintEvent & event);
  void OnPickerPaint(wxPaintEvent & event);
  void Relayout();

  CharControl * m_owner;

  wxStaticText * m_title = nullptr;
  wxStaticText * m_hint = nullptr;      // unmounted: what the card is for
  wxStaticText * m_name = nullptr;      // mounted: the mount's name
  wxButton * m_choose = nullptr;        // "Choose Mount", or "Change Mount" while mounted
  wxButton * m_dismount = nullptr;

  // The picker's own window and what it holds.
  MountPickerPopup * m_popup = nullptr;
  wxPanel * m_pickerPanel = nullptr;
  wxSearchCtrl * m_search = nullptr;
  MountPickerList * m_list = nullptr;
  wxStaticText * m_noMatch = nullptr;

  // What the last Sync showed.
  const WoWModel * m_rider = nullptr;
  bool m_mounted = false;
  unsigned int m_serial = 0;

  // The open list. Its rows are built when it opens: the CharControl::mountChoices index each stands for,
  // the name and the name in lower case. m_visible holds the positions of the rows the search lets through,
  // m_highlight the highlighted one of those (-1: none).
  bool m_open = false;
  std::vector<size_t> m_rowChoice;
  std::vector<wxString> m_rowName;
  std::vector<wxString> m_rowLower;
  std::vector<size_t> m_visible;
  int m_highlight = -1;
};

#endif // MOUNTCARD_H
