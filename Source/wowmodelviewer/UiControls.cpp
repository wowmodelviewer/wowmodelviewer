/*
 * UiControls.cpp
 *
 * The shell's own controls (see UiControls.h).
 */

#include "UiControls.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <wx/access.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/msw/wrapwin.h>
#include <oleacc.h>
#include <uxtheme.h>
#include <vssym32.h>

#pragma comment(lib, "uxtheme.lib")

#include "UiStyle.h"

using UiStyle::palette;

namespace
{
  std::unique_ptr<wxGraphicsContext> graphicsFor(wxDC & dc)
  {
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (gc)
      gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
    return gc;
  }

  bool isInside(const wxWindow * w, const wxWindow * container)
  {
    for (; w; w = w->GetParent())
      if (w == container)
        return true;
    return false;
  }

  // Whether Windows shows focus cues in this window now (they appear once the keyboard is used).
  bool focusCuesShown(const wxWindow * w)
  {
    const LRESULT state = ::SendMessage((HWND)w->GetHWND(), WM_QUERYUISTATE, 0, 0);
    return (state & UISF_HIDEFOCUS) == 0;
  }

  // The keyboard was used on a control that takes its own keys: show focus cues from now on, as the dialog
  // navigation does for the keys it handles.
  void showFocusCues(wxWindow * w)
  {
    if (wxWindow * top = wxGetTopLevelParent(w))
      ::SendMessage((HWND)top->GetHWND(), WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
  }

  // A name for a screen reader; the rest is the standard object's.
  class NamedAccessible : public wxAccessible
  {
  public:
    NamedAccessible(wxWindow * window, const wxString & name) : wxAccessible(window), m_name(name) {}
    wxAccStatus GetName(int childId, wxString * name) wxOVERRIDE
    {
      if (childId != wxACC_SELF)
        return wxACC_NOT_IMPLEMENTED;
      *name = m_name;
      return wxACC_OK;
    }

  private:
    wxString m_name;
  };
}

void UiSetAccessibleName(wxWindow * window, const wxString & name)
{
  if (window)
    window->SetAccessible(new NamedAccessible(window, name));
}

// ========================================================================================= button

UiButton::UiButton(wxWindow * parent, wxWindowID id, const wxString & label, Kind kind, UiIcon icon, long style)
  : m_kind(kind), m_icon(icon)
{
  Create(parent, id, label, wxDefaultPosition, wxDefaultSize, style | wxBORDER_NONE);
  // Painted here from now on (MSWOnDraw); everything else stays the native button's.
  MakeOwnerDrawn();
  SetFont(UiStyle::font(kind == Kind::Primary ? UiStyle::Type::Strong : UiStyle::Type::Normal));
  Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent & e) {
    m_hot = true;
    Refresh(false);
    e.Skip();
  });
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent & e) {
    m_hot = false;
    Refresh(false);
    e.Skip();
  });
  SetInitialSize();
}

void UiButton::SetKind(Kind kind)
{
  if (kind == m_kind)
    return;
  m_kind = kind;
  SetFont(UiStyle::font(kind == Kind::Primary ? UiStyle::Type::Strong : UiStyle::Type::Normal));
  InvalidateBestSize();
  Refresh(false);
}

void UiButton::SetIcon(UiIcon icon)
{
  if (icon == m_icon)
    return;
  m_icon = icon;
  InvalidateBestSize();
  Refresh(false);
}

void UiButton::SetCompact(bool compact)
{
  m_compact = compact;
  InvalidateBestSize();
  SetInitialSize();
}

void UiButton::SetIconAfter(bool after)
{
  m_iconAfter = after;
  Refresh(false);
}

void UiButton::SetIconOnly(bool iconOnly)
{
  m_iconOnly = iconOnly;
  InvalidateBestSize();
  SetInitialSize();
}

wxSize UiButton::DoGetBestSize() const
{
  const wxString label = m_iconOnly ? wxString() : GetLabelText();
  // The design's height, or more when Windows' text size makes the label taller.
  const int height = std::max(FromDIP(m_compact ? UiStyle::CompactButtonHeight : UiStyle::ButtonHeight),
                              GetCharHeight() + FromDIP(m_compact ? 6 : 10));
  if (label.IsEmpty())
    return wxSize(height, height);   // an icon alone: square
  int width = GetTextExtent(label).x;
  if (m_icon != UiIcon::None)
    width += FromDIP(UiStyle::IconSize) + FromDIP(6);
  const bool exact = HasFlag(wxBU_EXACTFIT);
  width += 2 * FromDIP(exact ? 8 : m_compact ? 10 : 14);
  if (!exact)
    width = std::max(width, FromDIP(m_compact ? 56 : 72));
  return wxSize(width, height);
}

bool UiButton::MSWOnDraw(WXDRAWITEMSTRUCT * item)
{
  const DRAWITEMSTRUCT * dis = reinterpret_cast<const DRAWITEMSTRUCT *>(item);
  const wxSize size(dis->rcItem.right - dis->rcItem.left, dis->rcItem.bottom - dis->rcItem.top);
  if (size.x <= 0 || size.y <= 0)
    return true;
  const UINT state = dis->itemState;
  wxBitmap buffer(size, 24);
  {
    wxMemoryDC mem(buffer);
    paint(mem, size, (state & ODS_SELECTED) != 0, m_hot, (state & ODS_FOCUS) && !(state & ODS_NOFOCUSRECT),
          (state & ODS_DISABLED) != 0 || !IsEnabled());
    ::BitBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, size.x, size.y, (HDC)mem.GetHDC(), 0, 0, SRCCOPY);
  }
  return true;
}

void UiButton::paint(wxDC & dc, const wxSize & size, bool pressed, bool hot, bool focusRing, bool disabled)
{
  const UiStyle::Palette & p = palette();
  // The corners show what the button stands on.
  const wxColour under = GetParent() ? GetParent()->GetBackgroundColour() : p.panelBackground;
  dc.SetBackground(wxBrush(under));
  dc.Clear();

  wxColour fill, edge, ink = disabled ? p.textDisabled : p.text;
  hot = hot && !disabled;
  pressed = pressed && !disabled;
  switch (m_kind)
  {
    case Kind::Primary:
      fill = disabled ? p.checked : pressed ? p.accentPressed : hot ? p.accentHover : p.accent;
      edge = fill;
      if (!disabled)
        ink = p.textOnAccent;
      break;
    case Kind::Secondary:
      fill = disabled ? under : pressed ? p.pressed : hot ? p.hover : p.controlBackground;
      edge = disabled ? p.separator : hot ? p.borderStrong : p.border;
      break;
    case Kind::Subtle:
      if (pressed)
        fill = p.pressed;
      else if (hot)
        fill = p.hover;
      break;
  }

  const double radius = FromDIP(UiStyle::Radius);
  {
    std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
    if (gc)
    {
      if (fill.IsOk() || edge.IsOk())
      {
        gc->SetBrush(fill.IsOk() ? wxBrush(fill) : *wxTRANSPARENT_BRUSH);
        gc->SetPen(edge.IsOk() ? wxPen(edge) : *wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, radius);
      }
      if (focusRing)
      {
        // Keyboard focus: an accent ring (inside a light one on the accent fill, so it shows on it).
        const double w = std::max(2, FromDIP(2));
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(m_kind == Kind::Primary ? p.textOnAccent : p.accent, w)));
        const double inset = m_kind == Kind::Primary ? w * 1.5 : w / 2.0;
        gc->DrawRoundedRectangle(inset, inset, size.x - 2 * inset, size.y - 2 * inset, std::max(1.0, radius - inset / 2));
      }
    }
  }

  const wxString label = m_iconOnly ? wxString() : GetLabelText();
  dc.SetFont(GetFont());
  const wxSize text = label.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(label);
  const int iconSize = m_icon == UiIcon::None ? 0 : FromDIP(UiStyle::IconSize);
  const int gap = (iconSize && text.x) ? FromDIP(6) : 0;
  // Centred, or from the left edge (wxBU_LEFT: buttons of one width in a column line up their icons).
  int x = (size.x - (iconSize + gap + text.x)) / 2;
  if (HasFlag(wxBU_LEFT))
    x = FromDIP(HasFlag(wxBU_EXACTFIT) ? 8 : m_compact ? 10 : 14);
  const auto drawIcon = [&](int at) {
    const wxBitmap bmp = UiIcons::bitmap(m_icon, ink, this, UiStyle::IconSize);
    if (bmp.IsOk())
      dc.DrawBitmap(bmp, at, (size.y - bmp.GetHeight()) / 2, true);
  };
  if (iconSize && !m_iconAfter)
  {
    drawIcon(x);
    x += iconSize + gap;
  }
  if (text.x)
  {
    dc.SetTextForeground(ink);
    dc.DrawText(label, x, (size.y - text.y) / 2);
    x += text.x + gap;
  }
  if (iconSize && m_iconAfter)
    drawIcon(x);
}

// =================================================================================== search field

UiSearchFrame::UiSearchFrame(wxWindow * parent, wxWindowID searchId, const wxString & hint, long searchStyle)
  : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_search = new wxSearchCtrl(this, searchId, wxEmptyString, wxDefaultPosition, wxDefaultSize, searchStyle | wxBORDER_NONE);
  UiStyle::setRole(m_search, UiStyle::Role::Field);
  if (!hint.IsEmpty())
    m_search->SetDescriptiveText(hint);

  wxBoxSizer * sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(m_search, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(6));
  SetSizer(sizer);
  SetMinSize(wxSize(FromDIP(80), std::max(FromDIP(UiStyle::SearchHeight), m_search->GetBestSize().y + FromDIP(4))));

  Bind(wxEVT_PAINT, &UiSearchFrame::OnPaint, this);
  // A click on the frame's padding gives the field the keyboard.
  Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_search->SetFocus(); });
  // The outline darkens while the mouse is over the field: entering or leaving the frame or anything in
  // it (the mouse can leave the edit straight out of the frame, which then gets no event of its own).
  // It follows the field's keyboard focus too: its edit child's, which is what has it. Losing it to
  // another part of the field (its clear button) is not losing it.
  std::function<void(wxWindow *)> watch = [&](wxWindow * w) {
    w->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent & e) { e.Skip(); updateHot(); });
    w->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent & e) { e.Skip(); CallAfter([this] { updateHot(); }); });
    if (w == this)
      return;
    w->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent & e) { e.Skip(); setFocused(true); });
    w->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) { e.Skip(); setFocused(isInside(e.GetWindow(), m_search)); });
    for (wxWindow * child : w->GetChildren())
      watch(child);
  };
  watch(this);
  watch(m_search);
}

void UiSearchFrame::updateHot()
{
  const bool hot = IsShownOnScreen() && GetScreenRect().Contains(wxGetMousePosition());
  if (hot == m_hot)
    return;
  m_hot = hot;
  Refresh(false);
}

void UiSearchFrame::setFocused(bool focused)
{
  if (focused == m_focused)
    return;
  m_focused = focused;
  Refresh(false);
}

void UiSearchFrame::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxAutoBufferedPaintDC dc(this);
  const UiStyle::Palette & p = palette();
  dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : p.panelBackground));
  dc.Clear();
  std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
  if (!gc)
    return;
  const wxSize size = GetClientSize();
  const double radius = FromDIP(UiStyle::Radius);
  const bool enabled = IsEnabled();
  gc->SetBrush(wxBrush(enabled ? p.controlBackground : p.panelBackground));
  gc->SetPen(wxPen(m_focused ? p.accent : (m_hot && enabled) ? p.borderStrong : p.border));
  gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, radius);
  if (m_focused)
  {
    // The field with the keyboard: an accent line along its bottom edge.
    const double thick = std::max(2, FromDIP(2));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(p.accent));
    gc->Clip(0, size.y - thick, size.x, thick);
    gc->DrawRoundedRectangle(0, 0, size.x, size.y, radius);
    gc->ResetClip();
  }
}

// ======================================================================================== tab bar

// What a screen reader sees of the strip: a page tab list whose children are the tabs, named by their
// pages, the shown one selected (and focused while the strip has the keyboard), as the native tab
// control reported them.
class UiTabBarAccessible : public wxAccessible
{
public:
  explicit UiTabBarAccessible(UiTabBar * bar) : wxAccessible(bar), m_bar(bar) {}

  wxAccStatus GetChildCount(int * count) wxOVERRIDE
  {
    *count = m_bar->m_book ? (int)m_bar->m_book->GetPageCount() : 0;
    return wxACC_OK;
  }
  wxAccStatus GetChild(int childId, wxAccessible ** child) wxOVERRIDE
  {
    *child = childId == wxACC_SELF ? this : nullptr;   // the tabs are simple elements
    return wxACC_OK;
  }
  wxAccStatus GetRole(int childId, wxAccRole * role) wxOVERRIDE
  {
    *role = childId == wxACC_SELF ? wxROLE_SYSTEM_PAGETABLIST : wxROLE_SYSTEM_PAGETAB;
    return wxACC_OK;
  }
  wxAccStatus GetName(int childId, wxString * name) wxOVERRIDE
  {
    if (childId == wxACC_SELF)
    {
      // The strip's own name: its label, when its owner gave it one.
      if (m_bar->GetLabel().IsEmpty())
        return wxACC_NOT_IMPLEMENTED;
      *name = m_bar->GetLabel();
      return wxACC_OK;
    }
    if (!valid(childId))
      return wxACC_FAIL;
    *name = wxStripMenuCodes(m_bar->m_book->GetPageText(childId - 1));
    return wxACC_OK;
  }
  wxAccStatus GetState(int childId, long * state) wxOVERRIDE
  {
    const bool focused = m_bar->HasFocus();
    if (childId == wxACC_SELF)
    {
      *state = wxACC_STATE_SYSTEM_FOCUSABLE | (focused ? wxACC_STATE_SYSTEM_FOCUSED : 0);
      return wxACC_OK;
    }
    if (!valid(childId))
      return wxACC_FAIL;
    const bool selected = childId - 1 == m_bar->m_book->GetSelection();
    *state = wxACC_STATE_SYSTEM_SELECTABLE | wxACC_STATE_SYSTEM_FOCUSABLE |
             (selected ? wxACC_STATE_SYSTEM_SELECTED : 0) | (selected && focused ? wxACC_STATE_SYSTEM_FOCUSED : 0);
    return wxACC_OK;
  }
  wxAccStatus GetLocation(wxRect & rect, int elementId) wxOVERRIDE
  {
    if (elementId == wxACC_SELF)
    {
      rect = m_bar->GetScreenRect();
      return wxACC_OK;
    }
    if (!valid(elementId))
      return wxACC_FAIL;
    wxClientDC dc(m_bar);
    const std::vector<wxRect> tabs = m_bar->layout(dc);
    rect = tabs[elementId - 1];
    rect.SetPosition(m_bar->ClientToScreen(rect.GetPosition()));
    return wxACC_OK;
  }
  wxAccStatus HitTest(const wxPoint & pt, int * childId, wxAccessible ** childObject) wxOVERRIDE
  {
    const int hit = m_bar->hitTest(m_bar->ScreenToClient(pt));
    *childId = hit >= 0 ? hit + 1 : wxACC_SELF;
    *childObject = nullptr;
    return wxACC_OK;
  }
  wxAccStatus GetFocus(int * childId, wxAccessible ** child) wxOVERRIDE
  {
    *child = nullptr;
    if (!m_bar->HasFocus())
    {
      *childId = wxACC_SELF;
      return wxACC_FALSE;
    }
    *childId = m_bar->m_book->GetSelection() + 1;
    return wxACC_OK;
  }
  wxAccStatus GetSelections(wxVariant * selections) wxOVERRIDE
  {
    if (!m_bar->m_book || m_bar->m_book->GetSelection() == wxNOT_FOUND)
      return wxACC_FALSE;
    *selections = (long)(m_bar->m_book->GetSelection() + 1);
    return wxACC_OK;
  }
  wxAccStatus GetDefaultAction(int childId, wxString * action) wxOVERRIDE
  {
    if (childId == wxACC_SELF)
      return wxACC_NOT_IMPLEMENTED;
    *action = _("Switch");
    return wxACC_OK;
  }
  wxAccStatus DoDefaultAction(int childId) wxOVERRIDE
  {
    if (!valid(childId))
      return wxACC_FAIL;
    m_bar->select(childId - 1);
    return wxACC_OK;
  }
  wxAccStatus Select(int childId, wxAccSelectionFlags WXUNUSED(flags)) wxOVERRIDE
  {
    return DoDefaultAction(childId);
  }

private:
  bool valid(int childId) const
  {
    return m_bar->m_book && childId >= 1 && childId <= (int)m_bar->m_book->GetPageCount();
  }

  UiTabBar * m_bar;
};

UiTabBar::UiTabBar(wxWindow * parent, wxBookCtrlBase * book)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE),
    m_book(book)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetFont(UiStyle::font(UiStyle::Type::Normal));
  SetAccessible(new UiTabBarAccessible(this));
  Bind(wxEVT_PAINT, &UiTabBar::OnPaint, this);
  Bind(wxEVT_LEFT_DOWN, &UiTabBar::OnMouse, this);
  Bind(wxEVT_MOTION, &UiTabBar::OnMouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &UiTabBar::OnMouse, this);
  Bind(wxEVT_KEY_DOWN, &UiTabBar::OnKey, this);
  Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent & e) { Refresh(false); e.Skip(); });
  Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) { Refresh(false); e.Skip(); });
  if (m_book)
  {
    m_book->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent & e) { pageChanged(); e.Skip(); });
    // Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PgDn and Ctrl+PgUp change the page from anywhere in the panel, as on
    // a native notebook (the panel passes these "window change" keys to its one book).
    m_book->Bind(wxEVT_NAVIGATION_KEY, [this](wxNavigationKeyEvent & e) {
      const int count = m_book ? (int)m_book->GetPageCount() : 0;
      if (!e.IsWindowChange() || count < 2)
      {
        e.Skip();
        return;
      }
      const int sel = m_book->GetSelection();
      const int target = e.GetDirection() ? (sel + 1) % count : (sel + count - 1) % count;
      if (HasFocus())
        select(target);   // the strip keeps the keyboard
      else
        m_book->SetSelection(target);   // the new page gets it, as a native notebook gives it
    });
  }
  SetInitialSize();
}

WXLRESULT UiTabBar::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
  if (message == WM_GETDLGCODE)
    return DLGC_WANTARROWS;
  return wxWindow::MSWWindowProc(message, wParam, lParam);
}

void UiTabBar::Sync()
{
  InvalidateBestSize();
  Refresh(false);
}

void UiTabBar::pageChanged()
{
  Refresh(false);
  if (!m_book || m_book->GetSelection() == wxNOT_FOUND)
    return;
  const int child = m_book->GetSelection() + 1;
  wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_SELECTION, this, wxOBJID_CLIENT, child);
  if (HasFocus())
    wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_FOCUS, this, wxOBJID_CLIENT, child);
}

std::vector<wxRect> UiTabBar::layout(wxDC & dc, int * padOut) const
{
  std::vector<wxRect> tabs;
  int pad = FromDIP(10);
  if (padOut)
    *padOut = pad;
  if (!m_book)
    return tabs;
  const int height = GetClientSize().y > 0 ? GetClientSize().y : FromDIP(UiStyle::TabHeight);
  dc.SetFont(UiStyle::font(UiStyle::Type::Strong));
  std::vector<int> widths;
  int text = 0;
  for (size_t i = 0; i < m_book->GetPageCount(); i++)
  {
    widths.push_back(dc.GetTextExtent(m_book->GetPageText(i)).x);
    text += widths.back();
  }
  // The first tab's title lines up with the pages' content (UiStyle::M in). Short of room, the padding
  // inside the tabs narrows (to a minimum) so the last tab stays on the strip.
  const int n = (int)widths.size();
  const int room = GetClientSize().x;
  if (room > 0 && n > 0 && FromDIP(UiStyle::M) + text + (2 * n - 1) * pad > room)
    pad = std::max(FromDIP(4), (room - FromDIP(UiStyle::M) - text) / (2 * n - 1));
  if (padOut)
    *padOut = pad;
  int x = FromDIP(UiStyle::M) - pad;
  for (int w : widths)
  {
    tabs.push_back(wxRect(x, 0, w + 2 * pad, height));
    x += w + 2 * pad;
  }
  return tabs;
}

int UiTabBar::hitTest(const wxPoint & at) const
{
  wxClientDC dc(const_cast<UiTabBar *>(this));
  const std::vector<wxRect> tabs = layout(dc);
  for (size_t i = 0; i < tabs.size(); i++)
    if (tabs[i].Contains(at))
      return (int)i;
  return -1;
}

wxSize UiTabBar::DoGetBestSize() const
{
  wxClientDC dc(const_cast<UiTabBar *>(this));
  const std::vector<wxRect> tabs = layout(dc);
  const int width = tabs.empty() ? FromDIP(60) : tabs.back().GetRight() + FromDIP(UiStyle::M);
  // The design's height, or more when Windows' text size makes the titles taller.
  dc.SetFont(UiStyle::font(UiStyle::Type::Strong));
  return wxSize(width, std::max(FromDIP(UiStyle::TabHeight), dc.GetCharHeight() + FromDIP(14)));
}

void UiTabBar::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxAutoBufferedPaintDC dc(this);
  const UiStyle::Palette & p = palette();
  const wxSize size = GetClientSize();
  dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : p.panelBackground));
  dc.Clear();
  // The line the tabs stand on.
  dc.SetPen(wxPen(p.separator));
  dc.DrawLine(0, size.y - 1, size.x, size.y - 1);
  if (!m_book)
    return;

  int pad = 0;
  const std::vector<wxRect> tabs = layout(dc, &pad);
  const int selected = m_book->GetSelection();
  const double radius = FromDIP(UiStyle::Radius);
  for (size_t i = 0; i < tabs.size(); i++)
  {
    const wxRect & r = tabs[i];
    const bool isSelected = (int)i == selected;
    const bool isHot = (int)i == m_hot && !isSelected;
    const wxString text = m_book->GetPageText(i);
    dc.SetFont(UiStyle::font(isSelected ? UiStyle::Type::Strong : UiStyle::Type::Normal));
    const wxSize extent = dc.GetTextExtent(text);
    const wxRect pill(r.x + 1, r.y + FromDIP(3), r.width - 2, r.height - FromDIP(7));
    if (isHot)
    {
      std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
      if (gc)
      {
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(p.hover));
        gc->DrawRoundedRectangle(pill.x, pill.y, pill.width, pill.height, radius);
      }
    }
    dc.SetTextForeground(isSelected || isHot ? p.text : p.textSecondary);
    dc.DrawText(text, r.x + pad, r.y + (r.height - extent.y) / 2 - FromDIP(1));
    if (isSelected)
    {
      // The selected tab: an accent line under its title, on the base line.
      const int thick = std::max(2, FromDIP(2));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(p.accent));
      dc.DrawRectangle(r.x + pad, size.y - thick, extent.x, thick);
      if (HasFocus() && focusCuesShown(this))
      {
        // Keyboard focus on the strip: a ring around the selected tab, as thick as a button's.
        std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
        if (gc)
        {
          const double w = std::max(2, FromDIP(2));
          gc->SetBrush(*wxTRANSPARENT_BRUSH);
          gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(p.accent, w)));
          gc->DrawRoundedRectangle(pill.x + w / 2, pill.y + w / 2, pill.width - w, pill.height - w, radius);
        }
      }
    }
  }
}

void UiTabBar::OnMouse(wxMouseEvent & event)
{
  const int hit = event.Leaving() ? -1 : hitTest(event.GetPosition());
  if (event.LeftDown() && hit >= 0 && m_book && hit != m_book->GetSelection())
    select(hit);
  if (hit != m_hot)
  {
    m_hot = hit;
    Refresh(false);
  }
  event.Skip();
}

void UiTabBar::OnKey(wxKeyEvent & event)
{
  if (!m_book || m_book->GetPageCount() == 0)
  {
    event.Skip();
    return;
  }
  const int count = (int)m_book->GetPageCount();
  int target = m_book->GetSelection();
  switch (event.GetKeyCode())
  {
    case WXK_LEFT:
    case WXK_NUMPAD_LEFT:
      target = (target + count - 1) % count;
      break;
    case WXK_RIGHT:
    case WXK_NUMPAD_RIGHT:
      target = (target + 1) % count;
      break;
    case WXK_HOME:
    case WXK_NUMPAD_HOME:
      target = 0;
      break;
    case WXK_END:
    case WXK_NUMPAD_END:
      target = count - 1;
      break;
    default:
      event.Skip();
      return;
  }
  // The keyboard is in use: the focus ring shows from now on, as with any control.
  showFocusCues(this);
  if (target != m_book->GetSelection())
    select(target);
  else
    Refresh(false);
}

void UiTabBar::select(int page)
{
  // The page events go out as for a click on a native tab. The book gives the new page the keyboard; as
  // with native tabs, the strip keeps it instead, so Left / Right go on through the tabs.
  m_book->SetSelection(page);
  SetFocus();
}

// ======================================================================= dark run: slider, status bar

bool UiSlider::MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM * result)
{
  const NMHDR * header = reinterpret_cast<const NMHDR *>(lParam);
  if (header->code == NM_CUSTOMDRAW && header->hwndFrom == (HWND)GetHWND() && UiStyle::darkActive())
  {
    const NMCUSTOMDRAW * draw = reinterpret_cast<const NMCUSTOMDRAW *>(lParam);
    if (draw->dwDrawStage == CDDS_PREPAINT)
    {
      *result = CDRF_NOTIFYITEMDRAW;
      return true;
    }
    if (draw->dwDrawStage == CDDS_ITEMPREPAINT && draw->dwItemSpec == TBCD_CHANNEL)
    {
      // The channel in the palette's border colour; the thumb stays Windows' (the accent).
      const HBRUSH brush = ::CreateSolidBrush(palette().border.GetPixel());
      ::FillRect(draw->hdc, &draw->rc, brush);
      ::DeleteObject(brush);
      *result = CDRF_SKIPDEFAULT;
      return true;
    }
    *result = CDRF_DODEFAULT;
    return true;
  }
  return wxSlider::MSWOnNotify(idCtrl, lParam, result);
}

namespace
{
  // What Windows' own status bar tells a screen reader: a status bar whose parts are its fields, each
  // named by its text.
  class StatusBarAccessible : public wxAccessible
  {
  public:
    explicit StatusBarAccessible(wxStatusBar * bar) : wxAccessible(bar), m_bar(bar) {}

    wxAccStatus GetChildCount(int * count) wxOVERRIDE
    {
      *count = m_bar->GetFieldsCount();
      return wxACC_OK;
    }
    wxAccStatus GetChild(int childId, wxAccessible ** child) wxOVERRIDE
    {
      *child = childId == wxACC_SELF ? this : nullptr;   // the fields are simple elements
      return wxACC_OK;
    }
    wxAccStatus GetRole(int childId, wxAccRole * role) wxOVERRIDE
    {
      *role = childId == wxACC_SELF ? wxROLE_SYSTEM_STATUSBAR : wxROLE_SYSTEM_STATICTEXT;
      return wxACC_OK;
    }
    wxAccStatus GetName(int childId, wxString * name) wxOVERRIDE
    {
      if (childId == wxACC_SELF)
        return wxACC_NOT_IMPLEMENTED;
      if (childId < 1 || childId > m_bar->GetFieldsCount())
        return wxACC_FAIL;
      *name = m_bar->GetStatusText(childId - 1);
      return wxACC_OK;
    }
    wxAccStatus GetState(int WXUNUSED(childId), long * state) wxOVERRIDE
    {
      *state = wxACC_STATE_SYSTEM_READONLY;
      return wxACC_OK;
    }
    wxAccStatus GetLocation(wxRect & rect, int elementId) wxOVERRIDE
    {
      if (elementId == wxACC_SELF)
      {
        rect = m_bar->GetScreenRect();
        return wxACC_OK;
      }
      wxRect field;
      if (elementId < 1 || !m_bar->GetFieldRect(elementId - 1, field))
        return wxACC_FAIL;
      rect = wxRect(m_bar->ClientToScreen(field.GetPosition()), field.GetSize());
      return wxACC_OK;
    }
    wxAccStatus HitTest(const wxPoint & pt, int * childId, wxAccessible ** childObject) wxOVERRIDE
    {
      *childObject = nullptr;
      *childId = wxACC_SELF;
      const wxPoint at = m_bar->ScreenToClient(pt);
      wxRect field;
      for (int i = 0; i < m_bar->GetFieldsCount(); i++)
        if (m_bar->GetFieldRect(i, field) && field.Contains(at))
          *childId = i + 1;
      return wxACC_OK;
    }

  private:
    wxStatusBar * m_bar;
  };
}

void UiEquipGenericStatusBar(wxStatusBar * bar)
{
  bar->SetAccessible(new StatusBarAccessible(bar));
  bar->Bind(wxEVT_MOTION, [bar](wxMouseEvent & e) {
    e.Skip();
    wxString tip;
    wxRect field;
    for (int i = 0; i < bar->GetFieldsCount(); i++)
      if (bar->GetFieldRect(i, field) && field.Contains(e.GetPosition()) && bar->GetField(i).IsEllipsized())
        tip = bar->GetStatusText(i);
    if (tip != bar->GetToolTipText())
    {
      if (tip.IsEmpty())
        bar->UnsetToolTip();
      else
        bar->SetToolTip(tip);
    }
  });
}

// ==================================================================================== dark run: menu bar

// A title's item data. MSAA reads an owner-drawn item's name from an MSAAMENUINFO at the start of it.
struct UiMenuBarTitles::Title
{
  MSAAMENUINFO msaa;
  std::wstring label;  // with the mnemonic's &
  std::wstring name;   // as read out
  int width;           // of the text: Windows adds the menu font's average character width either side
};

UiMenuBarTitles::UiMenuBarTitles() = default;

UiMenuBarTitles::~UiMenuBarTitles()
{
  detach();
}

void UiMenuBarTitles::attach(wxFrame * frame)
{
  wxMenuBar * bar = frame->GetMenuBar();
  if (!UiStyle::darkActive() || !bar)
    return;
  const HWND hwnd = (HWND)frame->GetHWND();
  const HMENU hmenu = (HMENU)bar->GetHMenu();

  // Each title as wide as Windows makes it: its text in the menu font (and Windows' margins).
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  const HFONT font = ::CreateFontIndirectW(&metrics.lfMenuFont);
  const HDC dc = ::GetDC(hwnd);
  const HGDIOBJ oldFont = ::SelectObject(dc, font);

  std::vector<std::unique_ptr<Title>> titles;
  for (size_t i = 0; i < bar->GetMenuCount(); i++)
  {
    std::unique_ptr<Title> title(new Title);
    title->label = bar->GetMenuLabel(i).ToStdWstring();
    title->name = wxStripMenuCodes(bar->GetMenuLabel(i), wxStrip_Mnemonics).ToStdWstring();
    SIZE text = {};
    ::GetTextExtentPoint32W(dc, title->name.c_str(), (int)title->name.size(), &text);
    title->width = text.cx;
    title->msaa.dwMSAASignature = MSAA_MENU_SIG;
    title->msaa.cchWText = (DWORD)title->name.size();
    title->msaa.pszWText = &title->name[0];
    titles.push_back(std::move(title));
  }
  ::SelectObject(dc, oldFont);
  ::ReleaseDC(hwnd, dc);
  ::DeleteObject(font);

  // handle() answers for the new titles from here on (an item's data is only read once it has been
  // set below); the previous ones live until no item points at them.
  std::vector<std::unique_ptr<Title>> previous;
  previous.swap(m_titles);
  m_titles.swap(titles);
  m_owner = frame;
  m_frame = hwnd;
  m_bar = hmenu;
  for (size_t i = 0; i < m_titles.size(); i++)
  {
    MENUITEMINFOW item = {};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_DATA;
    item.fType = MFT_OWNERDRAW;
    item.dwItemData = (ULONG_PTR)m_titles[i].get();
    ::SetMenuItemInfoW(hmenu, (UINT)i, TRUE, &item);
  }

  // The bar around the titles: Windows' themed bar is wx's (the panel colour already); this is the one
  // Windows draws itself.
  if (!m_background)
    m_background = (WXHBRUSH)::CreateSolidBrush(palette().panelBackground.GetPixel());
  MENUINFO info = {};
  info.cbSize = sizeof(info);
  info.fMask = MIM_BACKGROUND;
  info.hbrBack = (HBRUSH)m_background;
  ::SetMenuInfo(hmenu, &info);
  ::DrawMenuBar(hwnd);
}

void UiMenuBarTitles::detach()
{
  if (m_bar && ::IsMenu((HMENU)m_bar))
  {
    for (size_t i = 0; i < m_titles.size(); i++)
    {
      MENUITEMINFOW item = {};
      item.cbSize = sizeof(item);
      item.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
      item.fType = MFT_STRING;
      item.dwItemData = 0;
      item.dwTypeData = &m_titles[i]->label[0];
      ::SetMenuItemInfoW((HMENU)m_bar, (UINT)i, TRUE, &item);
    }
    MENUINFO info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = nullptr;
    ::SetMenuInfo((HMENU)m_bar, &info);
  }
  m_titles.clear();
  m_bar = nullptr;
  if (m_background)
    ::DeleteObject((HBRUSH)m_background);
  m_background = nullptr;
}

bool UiMenuBarTitles::handle(WXUINT message, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT * result)
{
  if (m_titles.empty())
    return false;
  // The menu font changed (Windows' text size): the titles measured again. Not the message's end.
  if (message == WM_SETTINGCHANGE && wParam == SPI_SETNONCLIENTMETRICS)
  {
    attach(m_owner);
    return false;
  }
  const auto ours = [this](ULONG_PTR data) -> Title * {
    for (const auto & title : m_titles)
      if ((ULONG_PTR)title.get() == data)
        return title.get();
    return nullptr;
  };

  if (message == WM_MEASUREITEM)
  {
    MEASUREITEMSTRUCT * measure = (MEASUREITEMSTRUCT *)lParam;
    const Title * title = measure->CtlType == ODT_MENU ? ours(measure->itemData) : nullptr;
    if (!title)
      return false;
    measure->itemWidth = title->width;
    measure->itemHeight = ::GetSystemMetrics(SM_CYMENU);   // Windows keeps the bar's own height
    *result = TRUE;
    return true;
  }

  if (message == WM_DRAWITEM)
  {
    const DRAWITEMSTRUCT * draw = (const DRAWITEMSTRUCT *)lParam;
    if (draw->CtlType != ODT_MENU || draw->hwndItem != (HWND)m_bar)
      return false;
    // Every title on the bar is drawn here, never by wx (which would take its data for a wxMenuItem).
    *result = TRUE;
    const Title * title = ours(draw->itemData);
    if (!title)
      return true;
    const UiStyle::Palette & p = palette();
    const UINT state = draw->itemState;
    const bool open = (state & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
    const wxColour & text = (state & (ODS_GRAYED | ODS_DISABLED)) ? p.textDisabled
                            : (state & ODS_INACTIVE) ? p.textSecondary : p.text;
    const HBRUSH back = ::CreateSolidBrush((open ? p.hover : p.panelBackground).GetPixel());
    ::FillRect(draw->hDC, &draw->rcItem, back);
    ::DeleteObject(back);
    RECT rect = draw->rcItem;
    const DWORD format = DT_CENTER | DT_SINGLELINE | DT_VCENTER | ((state & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
    // As wx draws the themed bar's titles: the menu theme's font and text rendering, in our colour.
    if (const HTHEME theme = ::OpenThemeData((HWND)m_frame, L"Menu"))
    {
      DTTOPTS options = {};
      options.dwSize = sizeof(options);
      options.dwFlags = DTT_TEXTCOLOR;
      options.crText = text.GetPixel();
      ::DrawThemeTextEx(theme, draw->hDC, MENU_BARITEM, MBI_NORMAL, title->label.c_str(), -1, format, &rect, &options);
      ::CloseThemeData(theme);
    }
    else
    {
      ::SetBkMode(draw->hDC, TRANSPARENT);
      ::SetTextColor(draw->hDC, text.GetPixel());
      ::DrawTextW(draw->hDC, title->label.c_str(), -1, &rect, format);
    }
    return true;
  }
  return false;
}
