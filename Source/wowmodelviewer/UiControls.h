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
 *   UiColourSwatch a colour to pick (View > Swap Background Color): the same native button, painted as the colour.
 *   UiColourPicker a colour chosen by eye (the same window): a saturation x brightness square and a hue strip.
 *   UiSearchFrame  the search field: a native search control without its border, framed here at a
 *                  comfortable height with a rounded outline that turns to the accent while the field
 *                  has the keyboard. Its text, hint, clear button and events are the native control's.
 *   UiTabBar       a flat tab strip over a wxSimplebook: the selected tab in the text colour with an
 *                  accent line under it, the others quiet. Click a tab, or give the strip the keyboard
 *                  and use Left / Right (Home / End); the book's page events are unchanged.
 */

#ifndef UICONTROLS_H
#define UICONTROLS_H

#include <wx/timer.h>
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

// A colour to pick. Like UiButton it IS a native push button, only painted here: Space, Enter, Tab, focus,
// wxEVT_BUTTON, wxEVT_CONTEXT_MENU and tooltips stay the system's, and a screen reader says its label (the colour's
// name), which is never drawn. Painted as the colour in a rounded frame with the outline the controls have (the strong
// one under the mouse), so a colour equal to the panel still has an edge. Selected: an accent ring with a gap of the
// surface between it and the colour, and a check mark in black or white, whichever reads on the colour -- so the mark
// does not depend on the ring's hue. Keyboard focus: a ring in the text colour inside the gap. Pressed: the colour
// drawn a pixel smaller. Disabled: the colour faded towards what the swatch stands on, the outline quiet.
class UiColourSwatch : public wxButton
{
public:
  // sizeDip: the whole swatch, ring and gap included (they are always reserved, so selecting one moves nothing).
  UiColourSwatch(wxWindow * parent, wxWindowID id, const wxColour & colour, const wxSize & sizeDip,
                 const wxString & accessibleName);

  void SetColour(const wxColour & colour);
  const wxColour & GetColour() const { return m_colour; }
  void SetSelected(bool selected);
  bool IsSelected() const { return m_selected; }

  bool MSWOnDraw(WXDRAWITEMSTRUCT * item) wxOVERRIDE;

protected:
  wxSize DoGetBestSize() const wxOVERRIDE;

private:
  void paint(wxDC & dc, const wxSize & size, bool pressed, bool hot, bool focusRing, bool disabled);

  wxColour m_colour;
  wxSize m_sizeDip;
  bool m_selected = false;
  bool m_hot = false;
};

// A colour chosen by eye, in place of the system's colour dialog: a square of saturation (left to right) by brightness
// (bottom to top) for one hue, and a strip of hues beside it -- HSV over sRGB bytes, as colour pickers are. Click or
// drag in either (a quick second press too); with the keyboard (Tab reaches each part, a focus ring shows which) the
// arrow keys move the focused part a step, ten with Shift, and Home / End go to the strip's ends. Drawn from the
// palette (outline, focus ring) and the colours themselves; the markers are a white ring in a dark one, so they show
// on any colour. A screen reader hears each part as a slider with its value, and is told when it changes.
//
// While the colour moves (a drag, a key held) the picker sends UI_EVT_COLOUR_CHANGING, at most once per new set of
// bytes; when it settles (the mouse let go, the key let up, the focus gone, settle()) UI_EVT_COLOUR_CHANGED. SetColour
// from outside sends nothing and is ignored during a drag; a grey, black or white keeps the hue the picker had (they
// have none of their own), so the strip does not jump to red.
//
// The colours are drawn Bleed DIP inside the picker's outer edges (room for a marker at an edge, and the focus ring):
// laid out that much wider than a column on each side, its colours line up with the column.
class UiColourPickerPart;

class UiColourPicker : public wxPanel
{
public:
  static const int Bleed = 7;   // DIP
  // The strip's width and the gap before it (DIP): the square is the rest.
  static const int StripWidth = 23;
  static const int StripGap = 4;

  // fieldSizeDip: the square's window (its colours Bleed inside it on the outer sides); the strip is as tall.
  UiColourPicker(wxWindow * parent, wxWindowID id, const wxSize & fieldSizeDip, const wxString & accessibleName);

  void SetColour(const wxColour & colour);
  wxColour GetColour() const;
  double GetHue() const { return m_h; }          // 0..360
  double GetSaturation() const { return m_s; }   // 0..1
  double GetBrightness() const { return m_v; }   // 0..1
  bool IsDragging() const;
  // A drag or a held key ends here and now, with UI_EVT_COLOUR_CHANGED (the window it is in is closing or hiding).
  void settle();
  // ... or without any event (the window it is in is being destroyed).
  void abandon();
  // The two parts: the square and the strip.
  wxWindow * Field() const;
  wxWindow * Strip() const;

private:
  friend class UiColourPickerPart;
  // A part moved the colour; settled: the mouse or key was let go.
  void partChanged(bool settled);

  UiColourPickerPart * m_field = nullptr;
  UiColourPickerPart * m_strip = nullptr;
  double m_h = 0.0, m_s = 0.0, m_v = 0.0;
  wxColour m_sent;   // the bytes of the last UI_EVT_COLOUR_CHANGING
};

wxDECLARE_EVENT(UI_EVT_COLOUR_CHANGING, wxCommandEvent);
wxDECLARE_EVENT(UI_EVT_COLOUR_CHANGED, wxCommandEvent);

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

// A PROGRESS BAR in the palette's colours (the native gauge is a white channel in either theme): a quiet solid
// track and an accent fill of the same rounded shape, cut to the track. Determinate (SetValue, 0..1) while the amount
// of work is known; otherwise indeterminate (SetIndeterminate): a short accent segment that slides along the track and
// starts over, so the bar never shows a number nobody measured. The slide is timed by the clock, not by the timer's
// ticks, so a bar repainted late (a load that pumps its events now and then) shows where the segment is now.
class UiProgressBar : public wxWindow
{
public:
  explicit UiProgressBar(wxWindow * parent, wxWindowID id = wxID_ANY);
  ~UiProgressBar();

  void SetValue(double fraction);
  void SetIndeterminate();
  bool IsIndeterminate() const { return m_indeterminate; }
  double GetValue() const { return m_value; }

  bool AcceptsFocus() const wxOVERRIDE { return false; }
  // A hidden bar stops its timer (no wxEVT_SHOW handler: that one also fires while the window is destroyed).
  bool Show(bool show = true) wxOVERRIDE;

protected:
  wxSize DoGetBestSize() const wxOVERRIDE;

private:
  void OnPaint(wxPaintEvent & event);
  void syncTimer();

  double m_value = 0.0;
  bool m_indeterminate = true;
  wxTimer m_timer;
  wxLongLong m_slideStart;
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
