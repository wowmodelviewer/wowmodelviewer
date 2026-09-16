/*
 * MountCard.cpp
 */

#include "MountCard.h"

#include <wx/dcclient.h>
#include <wx/eventfilter.h>
#include <wx/listctrl.h>
#include <wx/popupwin.h>
#include <wx/srchctrl.h>
#include <wx/stopwatch.h>

#include "charcontrol.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "UiStyle.h"
#include "WoWModel.h"

#include "logger/Logger.h"

// The list of mounts. Virtual, because there are well over a thousand player mounts and the search
// filters them on every keystroke: the control only ever asks for the rows on screen.
class MountPickerList : public wxListCtrl
{
public:
  MountPickerList(wxWindow * parent, MountCard * card)
    : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_THEME),
      m_card(card)
  {
    AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, FromDIP(200));
    // After the layout has settled: during a resize the list passes through sizes too small to fit.
    Bind(wxEVT_SIZE, [this](wxSizeEvent & e) {
      e.Skip();
      CallAfter([this]() { FitColumn(); });
    });
  }

  // The one column runs up to where the vertical scrollbar is, or would be. The client width already leaves
  // out a scrollbar that shows -- the list has one while it holds more rows than fit -- so the room for one
  // is kept only while it does not show: a search that brings it up never brings a horizontal one with it.
  void FitColumn()
  {
    const bool scrollbar = GetItemCount() > GetCountPerPage();
    const int w = GetClientSize().x - (scrollbar ? 0 : wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this)) -
                  FromDIP(2);
    if (w > FromDIP(40) && w != GetColumnWidth(0))
      SetColumnWidth(0, w);
  }

protected:
  wxString OnGetItemText(long item, long WXUNUSED(column)) const override
  {
    return m_card->RowText(item);
  }

private:
  MountCard * m_card;
};

// The picker's window: it floats just below Choose Mount / Change Mount, so opening it leaves the page as it
// is. wxPU_CONTAINS_CONTROLS makes it a popup that can hold the keyboard focus (on MSW the default kind is a
// child of the desktop that never takes it). wx closes it when it loses the activation; while it is open it
// also watches for a mouse button pressed on any other window of the program, which closes it too, as not
// every such press moves the activation.
class MountPickerPopup : public wxPopupTransientWindow, public wxEventFilter
{
public:
  explicit MountPickerPopup(MountCard * card)
    : wxPopupTransientWindow(card, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS),
      m_card(card)
  {
  }

  ~MountPickerPopup() override
  {
    if (m_watching)
      wxEvtHandler::RemoveFilter(this);
  }

  void WatchClicks()
  {
    if (!m_watching)
      wxEvtHandler::AddFilter(this);
    m_watching = true;
  }

  void StopWatchingClicks()
  {
    if (!m_watching)
      return;
    // Not from inside the filter chain, which is still being walked.
    if (m_filtering)
    {
      CallAfter([this]() {
        if (!IsShown())
          StopWatchingClicks();
      });
      return;
    }
    wxEvtHandler::RemoveFilter(this);
    m_watching = false;
  }

  int FilterEvent(wxEvent & event) override
  {
    const wxEventType type = event.GetEventType();
    if (type != wxEVT_LEFT_DOWN && type != wxEVT_RIGHT_DOWN && type != wxEVT_MIDDLE_DOWN && type != wxEVT_AUX1_DOWN &&
        type != wxEVT_AUX2_DOWN)
      return Event_Skip;
    wxWindow * target = wxDynamicCast(event.GetEventObject(), wxWindow);
    if (!IsShown() || (target && IsDescendant(target)))
      return Event_Skip;
    m_filtering = true;
    const bool eaten = m_card->PickerClickedOutside(target);
    m_filtering = false;
    return eaten ? Event_Processed : Event_Skip;
  }

protected:
  void OnDismiss() override { m_card->PickerDismissed(); }

private:
  MountCard * m_card;
  bool m_watching = false;
  bool m_filtering = false;
};

MountCard::MountCard(wxWindow * parent, CharControl * owner)
  : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE,
            wxT("mountCard")),
    m_owner(owner)
{
  const int xs = FromDIP(UiStyle::XS);
  const int sp = FromDIP(UiStyle::S);
  const int md = FromDIP(UiStyle::M);

  // The card's own colour, not passed on: the labels on it are drawn on it.
  SetOwnBackgroundColour(UiStyle::cardBackground());
  Bind(wxEVT_PAINT, &MountCard::OnPaint, this);

  m_title = new wxStaticText(this, wxID_ANY, _("Mount"));
  wxFont titleFont = m_title->GetFont().Bold();
  titleFont.SetPointSize(titleFont.GetPointSize() + 1);
  m_title->SetFont(titleFont);

  m_hint = UiStyle::secondaryLabel(this, _("Add a mount to this character"));
  m_name = new wxStaticText(this, wxID_ANY, _("Mount"), wxDefaultPosition, wxDefaultSize,
                            wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
  m_name->SetFont(m_name->GetFont().Bold());
  // A long name is cut short with an ellipsis rather than widening the panel; the tooltip has all of it.
  m_name->SetMinSize(wxSize(FromDIP(40), m_name->GetBestSize().y));

  m_choose = new wxButton(this, wxID_ANY, _("Choose Mount"));
  m_choose->SetMinSize(wxSize(-1, FromDIP(UiStyle::ControlHeight)));
  m_choose->SetToolTip(_("Put this character on a mount"));
  m_choose->SetFont(m_choose->GetFont().Bold());   // the call to action while unmounted (see Sync)
  m_dismount = new wxButton(this, wxID_ANY, _("Dismount"));
  m_dismount->SetMinSize(wxSize(-1, FromDIP(UiStyle::ControlHeight)));
  m_dismount->SetToolTip(_("Take the character off its mount"));

  // The picker: a search field over the list, or "No mounts found" in its place, on a panel in the card's
  // colours inside its own window. The search field and the list keep their own colours.
  m_popup = new MountPickerPopup(this);
  m_pickerPanel = new wxPanel(m_popup, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE);
  m_pickerPanel->SetOwnBackgroundColour(UiStyle::cardBackground());
  m_pickerPanel->Bind(wxEVT_PAINT, &MountCard::OnPickerPaint, this);
  m_search = new wxSearchCtrl(m_pickerPanel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_PROCESS_ENTER);
  m_search->ShowCancelButton(true);
  m_search->SetDescriptiveText(_("Search mounts"));
  m_list = new MountPickerList(m_pickerPanel, this);
  m_list->SetMinSize(wxSize(FromDIP(60), FromDIP(60)));
  m_noMatch = UiStyle::secondaryLabel(m_pickerPanel, _("No mounts found"));

  wxBoxSizer * picker = new wxBoxSizer(wxVERTICAL);
  picker->Add(m_search, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  picker->Add(m_list, 1, wxEXPAND | wxALL, sp);
  picker->Add(m_noMatch, 0, wxEXPAND | wxALL, sp);
  m_pickerPanel->SetSizer(picker);
  wxBoxSizer * popupSizer = new wxBoxSizer(wxVERTICAL);
  popupSizer->Add(m_pickerPanel, 1, wxEXPAND);
  m_popup->SetSizer(popupSizer);

  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->Add(m_choose, 0, wxALIGN_CENTER_VERTICAL);
  buttons->Add(m_dismount, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, xs);

  wxBoxSizer * content = new wxBoxSizer(wxVERTICAL);
  content->Add(m_title, 0, wxEXPAND | wxBOTTOM, xs);
  content->Add(m_hint, 0, wxEXPAND | wxBOTTOM, sp);
  content->Add(m_name, 0, wxEXPAND | wxBOTTOM, sp);
  content->Add(buttons, 0, wxEXPAND);

  wxBoxSizer * padding = new wxBoxSizer(wxVERTICAL);
  padding->Add(content, 1, wxEXPAND | wxALL, md);
  SetSizer(padding);

  // Choose Mount / Change Mount opens the list, and closes it again when it is open.
  m_choose->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_open)
      ClosePicker(true);
    else
      OpenPicker();
  });
  m_dismount->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    ClosePicker(false);
    m_owner->dismount();
    // The button just clicked is gone with the mount.
    if (m_choose->IsShownOnScreen())
      m_choose->SetFocus();
  });
  m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { ApplyFilter(); });
  // The search field shows no focus of its own beyond the text caret; the picker rings it while it has the
  // focus (OnPickerPaint). Its text control's focus events reach the search control itself.
  m_search->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent & e) {
    e.Skip();
    m_pickerPanel->Refresh();
  });
  m_search->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) {
    e.Skip();
    m_pickerPanel->Refresh();
  });
  // The keys work wherever the focus is in the open list: the search field or, tabbed into, the list.
  // wxEVT_CHAR_HOOK gets here before the main window's own handler sees the key (Escape leaves fullscreen
  // there); wxEVT_KEY_DOWN takes a key that arrives without one.
  for (wxWindow * w : { static_cast<wxWindow *>(m_search), static_cast<wxWindow *>(m_list) })
  {
    w->Bind(wxEVT_CHAR_HOOK, &MountCard::OnPickerKey, this);
    w->Bind(wxEVT_KEY_DOWN, &MountCard::OnPickerKey, this);
  }
  // A click on a row mounts it. Taken on the button going down, before the list moves its selection: moving
  // the highlight never mounts anything.
  m_list->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent & e) {
    int flags = 0;
    const long row = m_list->HitTest(e.GetPosition(), flags);
    if (row >= 0 && (flags & wxLIST_HITTEST_ONITEM))
      ChooseRow(row);
    else
      e.Skip();
  });
  // Enter that the list itself takes, on its selection -- which is the highlight.
  m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent & e) { ChooseRow(e.GetIndex()); });

  m_name->Show(false);
  m_dismount->Show(false);
  m_noMatch->Show(false);
  Show(false);   // until Sync finds a playable character
}

void MountCard::Sync(bool characterPanel)
{
  const WoWModel * rider = g_modelViewer ? g_modelViewer->riderModel() : nullptr;
  const bool playable = characterPanel && rider && rider->infos.raceID != -1;
  const bool mounted = playable && g_modelViewer->riderMount() != nullptr;
  const unsigned int serial = m_owner->mountSerial;

  // The list was opened for another character or another mount: Change Mount opens it again for this one.
  if (m_open && (!playable || rider != m_rider || mounted != m_mounted || serial != m_serial))
    ClosePicker(false);

  bool changed = false;
  if (IsShown() != playable)
  {
    Show(playable);
    changed = true;
  }
  const wxString name = mounted ? m_owner->ridingMountName() : wxString();
  if (mounted != m_mounted || m_name->GetLabelText() != name)
  {
    m_hint->Show(!mounted);
    m_name->SetLabelText(name);
    m_name->SetToolTip(name);
    m_name->Show(mounted);
    m_dismount->Show(mounted);
    m_choose->SetLabel(mounted ? _("Change Mount") : _("Choose Mount"));
    m_choose->SetToolTip(mounted ? _("Put the character on a different mount") : _("Put this character on a mount"));
    // Unmounted, Choose Mount is what the card is for and reads bold; on a mount the name stands out, and
    // Change Mount reads like Dismount beside it.
    m_choose->SetFont(mounted ? m_dismount->GetFont() : m_dismount->GetFont().Bold());
    changed = true;
  }
  m_rider = rider;
  m_mounted = mounted;
  m_serial = serial;
  if (changed)
    Relayout();
}

wxString MountCard::RowText(long row) const
{
  if (row < 0 || row >= (long)m_visible.size())
    return wxEmptyString;
  return m_rowName[m_visible[row]];
}

void MountCard::OpenPicker()
{
  if (m_open || !IsShownOnScreen())
    return;
  wxStopWatch clock;

  // Every player mount the mount choice lists that can be put on: a few entries have no display, and no name.
  const std::vector<CharControl::MountChoice> & choices = m_owner->mountChoices();
  const long listMs = clock.Time();
  m_rowChoice.clear();
  m_rowName.clear();
  m_rowLower.clear();
  for (size_t i = 0; i < choices.size(); i++)
    if (choices[i].displayId > 0 && !choices[i].name.IsEmpty())
    {
      m_rowChoice.push_back(i);
      m_rowName.push_back(choices[i].name);
      m_rowLower.push_back(choices[i].name.Lower());
    }

  // Change Mount starts on the mount the character rides.
  const int riding = m_owner->ridingMountChoice();
  m_visible.clear();
  m_highlight = -1;
  for (size_t i = 0; i < m_rowChoice.size(); i++)
  {
    if ((int)m_rowChoice[i] == riding)
      m_highlight = (int)i;
    m_visible.push_back(i);
  }

  // As wide as the card, with room for about a dozen rows; just below the card, or above the button when the
  // screen has no room below.
  const int sp = FromDIP(UiStyle::S);
  const wxRect card = GetScreenRect();
  const wxRect button = m_choose->GetScreenRect();
  m_list->Show(true);
  m_noMatch->Show(false);
  m_popup->SetSize(card.width, sp + m_search->GetBestSize().y + sp + FromDIP(240) + sp);
  m_popup->Layout();
  m_popup->Position(wxPoint(card.x, button.y), wxSize(0, card.GetBottom() + 1 - button.y + FromDIP(UiStyle::XS)));

  m_open = true;
  m_search->ChangeValue(wxEmptyString);
  ApplyFilter();
  m_popup->WatchClicks();
  m_popup->Popup(m_search);
  ShowHighlight();   // scrolled into view now that the list is on screen
  LOG_INFO << "[mount-card] mount list opened:" << (int)m_rowChoice.size() << "mounts in" << clock.Time()
           << "ms (" << listMs << "ms reading the mount choice)";
}

void MountCard::ClosePicker(bool focusButton)
{
  if (!m_open)
    return;
  m_open = false;
  m_popup->StopWatchingClicks();
  m_popup->Dismiss();
  if (focusButton && m_choose->IsShownOnScreen())
    m_choose->SetFocus();
}

// A mouse button pressed on a window outside the open picker: it closes with nothing chosen. A press on Choose
// Mount / Change Mount itself is taken here, or its click would open the picker again.
bool MountCard::PickerClickedOutside(const wxWindow * target)
{
  if (!m_open)
    return false;
  const bool onButton = target == m_choose;
  ClosePicker(onButton);
  return onButton;
}

// wx closed the picker itself: it lost the activation to another window or program.
void MountCard::PickerDismissed()
{
  if (!m_open)
    return;
  m_open = false;
  m_popup->StopWatchingClicks();
}

void MountCard::ApplyFilter()
{
  if (!m_open)
    return;
  wxString needle = m_search->GetValue();
  needle.Trim(true).Trim(false);
  needle.MakeLower();

  // The highlight stays on its mount while the search still lists it.
  const long kept = (m_highlight >= 0 && m_highlight < (int)m_visible.size()) ? (long)m_visible[m_highlight] : -1;
  m_visible.clear();
  m_highlight = -1;
  for (size_t i = 0; i < m_rowLower.size(); i++)
  {
    if (!needle.IsEmpty() && !m_rowLower[i].Contains(needle))
      continue;
    if ((long)i == kept)
      m_highlight = (int)m_visible.size();
    m_visible.push_back(i);
  }

  m_list->SetItemCount((long)m_visible.size());
  m_list->Refresh();
  // A row count that brings the vertical scrollbar up or takes it away changes the client width without a size
  // event.
  CallAfter([this]() { m_list->FitColumn(); });
  ShowHighlight();
  // Nothing highlighted: the results start at the top, wherever the list was scrolled before. Only the view moves.
  if (m_highlight < 0 && !m_visible.empty())
    m_list->EnsureVisible(0);

  // No match: said in place of the list, and the search stays as it is to be corrected.
  const bool none = m_visible.empty();
  if (m_list->IsShown() == none)
  {
    m_list->Show(!none);
    m_noMatch->Show(none);
    m_pickerPanel->Layout();
  }
}

// The list's own selection is the highlight. It is set here only: the list never changes it itself.
void MountCard::ShowHighlight()
{
  for (long s = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); s >= 0;
       s = m_list->GetNextItem(s, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED))
    if (s != m_highlight)
      m_list->SetItemState(s, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  if (m_highlight >= 0)
  {
    m_list->SetItemState(m_highlight, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                         wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    m_list->EnsureVisible(m_highlight);
  }
}

void MountCard::MoveHighlight(int step)
{
  if (m_visible.empty())
    return;
  if (m_highlight < 0)
  {
    if (step < 0)
      return;   // Up with nothing highlighted has nowhere to go
    m_highlight = 0;
  }
  else
  {
    // It stops at the first and the last row.
    m_highlight += step;
    if (m_highlight < 0)
      m_highlight = 0;
    if (m_highlight >= (int)m_visible.size())
      m_highlight = (int)m_visible.size() - 1;
  }
  ShowHighlight();
}

void MountCard::ChooseRow(long row)
{
  if (!m_open || row < 0 || row >= (long)m_visible.size())
    return;
  const size_t choice = m_rowChoice[m_visible[row]];
  ClosePicker(true);
  // The mount it already rides: there is nothing to put on.
  if ((int)choice == m_owner->ridingMountChoice())
    return;
  m_owner->selectMountChoice(choice);
}

void MountCard::OnPickerKey(wxKeyEvent & event)
{
  if (!m_open || event.HasAnyModifiers())
  {
    event.Skip();
    return;
  }
  switch (event.GetKeyCode())
  {
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN:
      MoveHighlight(1);
      break;
    case WXK_UP:
    case WXK_NUMPAD_UP:
      MoveHighlight(-1);
      break;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
      // The highlighted mount; with none highlighted, the only one the search left. Never any other.
      if (m_highlight >= 0)
        ChooseRow(m_highlight);
      else if (m_visible.size() == 1)
        ChooseRow(0);
      break;
    case WXK_ESCAPE:
      ClosePicker(true);
      break;
    default:
      event.Skip();
      break;
  }
}

bool MountCard::SearchHasFocus() const
{
  for (const wxWindow * w = wxWindow::FindFocus(); w; w = w->GetParent())
    if (w == m_search)
      return true;
  return false;
}

void MountCard::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxPaintDC dc(this);
  dc.SetPen(wxPen(UiStyle::cardBorder()));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawRectangle(GetClientRect());
}

void MountCard::OnPickerPaint(wxPaintEvent & WXUNUSED(event))
{
  wxPaintDC dc(m_pickerPanel);
  dc.SetPen(wxPen(UiStyle::cardBorder()));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawRectangle(m_pickerPanel->GetClientRect());

  // The focus ring: the theme's selection colour just outside the search field, while it has the focus.
  if (m_open && SearchHasFocus())
  {
    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
    const wxRect field = m_search->GetRect();
    for (int i = 1; i <= FromDIP(2); i++)
      dc.DrawRectangle(wxRect(field).Inflate(i));
  }
}

// Labels and buttons shown or hidden change the card's height, which the page's scrolled layout has to hear
// about.
void MountCard::Relayout()
{
  InvalidateBestSize();
  Layout();
  m_owner->Layout();
  m_owner->FitInside();
  Refresh();
}
