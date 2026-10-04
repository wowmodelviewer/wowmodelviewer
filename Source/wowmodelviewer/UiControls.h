/*
 * UiControls.h
 *
 * The few controls the shell draws itself, where the native ones cannot take the design (UiStyle.h)
 * and the surface is used all the time:
 *
 *   UiButton       a button by importance -- Primary (the accent: the action the area is for, at most
 *                  one per area), Secondary (outlined), Subtle (no frame until the mouse is over it:
 *                  copy, reset, remove). It IS a native Windows button, only painted here, so the
 *                  keyboard (Space, Enter, Tab), focus, wxEVT_BUTTON, tooltips and accessibility (a
 *                  screen reader sees a push button with its label) stay the system's. The focus ring
 *                  shows for keyboard focus only, as Windows does.
 *   UiSearchFrame  the search field: a native search control without its border, framed here at a
 *                  comfortable height with a rounded outline that turns to the accent while the field
 *                  has the keyboard. Its text, hint, clear button and events are the native control's.
 *   UiTabBar       a flat tab strip over a wxSimplebook: the selected tab in the text colour with an
 *                  accent line under it, the others quiet. Click a tab, or give the strip the keyboard
 *                  and use Left / Right (Home / End); the book's page events are unchanged.
 */

#ifndef UICONTROLS_H
#define UICONTROLS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/bookctrl.h>
#include <wx/slider.h>
#include <wx/srchctrl.h>
#include <wx/statusbr.h>

#include <memory>
#include <vector>

#include "UiIcons.h"

// The name a screen reader says for a window whose own text does not say what it is (a choice whose label
// is drawn by a toolbar, a button whose label is a single word beside its icon). Everything else about the
// window stays what Windows reports for it.
void UiSetAccessibleName(wxWindow * window, const wxString & name);

class UiButton : public wxButton
{
public:
  enum class Kind { Primary, Secondary, Subtle };

  UiButton(wxWindow * parent, wxWindowID id, const wxString & label, Kind kind = Kind::Secondary,
           UiIcon icon = UiIcon::None, long style = 0);

  void SetKind(Kind kind);
  Kind GetKind() const { return m_kind; }
  void SetIcon(UiIcon icon);
  // A button in a dense row: the compact height.
  void SetCompact(bool compact);
  // The icon after the label (a "next" step) instead of before it.
  void SetIconAfter(bool after);
  // The icon alone, square: the label stays the button's name for a screen reader.
  void SetIconOnly(bool iconOnly);

  bool MSWOnDraw(WXDRAWITEMSTRUCT * item) wxOVERRIDE;

protected:
  wxSize DoGetBestSize() const wxOVERRIDE;

private:
  void paint(wxDC & dc, const wxSize & size, bool pressed, bool hot, bool focusRing, bool disabled);

  Kind m_kind;
  UiIcon m_icon;
  bool m_compact = false;
  bool m_iconAfter = false;
  bool m_iconOnly = false;
  bool m_hot = false;
};

class UiSearchFrame : public wxPanel
{
public:
  UiSearchFrame(wxWindow * parent, wxWindowID searchId, const wxString & hint, long searchStyle = wxTE_PROCESS_ENTER);
  wxSearchCtrl * search() const { return m_search; }

private:
  void OnPaint(wxPaintEvent & event);
  void setFocused(bool focused);
  // Whether the mouse is over the field (the frame or anything in it).
  void updateHot();

  wxSearchCtrl * m_search;
  bool m_focused = false;
  bool m_hot = false;
};

class UiTabBar : public wxWindow
{
public:
  UiTabBar(wxWindow * parent, wxBookCtrlBase * book);
  // A page's name changed (the Appearance page's, while mounted): repaint.
  void Sync();

  bool AcceptsFocusFromKeyboard() const wxOVERRIDE { return true; }
  // The arrow keys are the strip's (moving between tabs), not the dialog navigation's; Tab still moves on.
  WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;

protected:
  wxSize DoGetBestSize() const wxOVERRIDE;

private:
  friend class UiTabBarAccessible;

  void OnPaint(wxPaintEvent & event);
  void OnMouse(wxMouseEvent & event);
  void OnKey(wxKeyEvent & event);
  // The tabs' rectangles, left to right, and the padding inside each (narrower when the strip is short
  // of room, so every tab stays on it).
  std::vector<wxRect> layout(wxDC & dc, int * padOut = nullptr) const;
  int hitTest(const wxPoint & at) const;
  void select(int page);
  // The page shown changed: repaint, and tell a screen reader.
  void pageChanged();

  wxBookCtrlBase * m_book;
  int m_hot = -1;
};

// THREE NATIVE PARTS IN THE DARK RUN that wxWidgets' dark mode leaves as they are (see UiStyle.cpp): a
// slider's channel, which Windows draws light (there is no dark trackbar theme), drawn here from the
// palette; the status bar; and the menu bar's titles, below.
class UiSlider : public wxSlider
{
public:
  using wxSlider::wxSlider;
  bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM * result) wxOVERRIDE;
};

// The status bar of the dark run is wx's own, in the panel colour (Windows' own dark status bar is black,
// and ignores a colour); this gives it what Windows' own gives and wx's lacks on Windows: a status bar
// for screen readers, the fields' text as its parts, and the full text of a cut-off field as a tooltip.
// (Its size grip wx draws on GTK only.)
void UiEquipGenericStatusBar(wxStatusBar * bar);

// The menu bar's titles (File, View, ...) of the dark run. wxWidgets darkens the menu bar through Windows'
// themed menu bar, and Windows draws a window's menu bar themed only while the window has a caption: in
// the viewer's fullscreen (no caption, no border) Windows draws the bar itself, dark grey with BLACK titles.
// So in the dark run the titles are drawn here, owner-drawn, in both window states alike: the palette's
// text (its secondary text while another window is the active one), the hover colour under the mouse and
// while the title's menu is open, on the panel colour, which is also the bar's. The menus stay wxWidgets'
// and Windows'. The items keep their text, so Alt with the underlined letter still opens a menu, and a
// screen reader reads each title (MSAA's name for an owner-drawn menu item).
class UiMenuBarTitles
{
public:
  UiMenuBarTitles();
  ~UiMenuBarTitles();
  // Once the frame's menu bar is set (does nothing outside the dark run); again after a title changes.
  void attach(wxFrame * frame);
  // The frame's MSWWindowProc asks this FIRST: wxWidgets would take an owner-drawn item's data for a
  // wxMenuItem of its own. True when the message was a title's (the result in *result).
  bool handle(WXUINT message, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT * result);

private:
  struct Title;
  // The titles back to Windows' own (the frame keeps its menu bar until its window is destroyed).
  void detach();

  std::vector<std::unique_ptr<Title>> m_titles;
  wxFrame * m_owner = nullptr;
  WXHWND m_frame = nullptr;
  WXHMENU m_bar = nullptr;
  WXHBRUSH m_background = nullptr;
};

#endif // UICONTROLS_H
