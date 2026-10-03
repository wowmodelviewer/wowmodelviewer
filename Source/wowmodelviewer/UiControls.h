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
#include <wx/bitmap.h>
#include <wx/bmpcbox.h>
#include <wx/bookctrl.h>
#include <wx/combobox.h>
#include <wx/odcombo.h>
#include <wx/renderer.h>
#include <wx/slider.h>
#include <wx/srchctrl.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>

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

// NATIVE CONTROLS IN THE DARK THEME (see UiStyle::themeNativeWindow for the rest). Four native controls
// paint a part with Windows' light colours whatever their theme; these draw that part from the palette in
// the dark theme and leave the light one as Windows draws it.
//   UiComboBox        a combo box whose field, disabled, Windows fills with the light button face
//   UiTextCtrl        a one-line text field, likewise when disabled
//   UiBitmapComboBox  wxBitmapComboBox, which wx owner-draws in the light window colours
//   UiSlider          a slider whose channel Windows draws light (there is no dark trackbar theme)
class UiComboBox : public wxComboBox
{
public:
  using wxComboBox::wxComboBox;
  WXHBRUSH MSWControlColor(WXHDC pDC, WXHWND hWnd) wxOVERRIDE;
};

class UiTextCtrl : public wxTextCtrl
{
public:
  using wxTextCtrl::wxTextCtrl;
  WXHBRUSH MSWControlColor(WXHDC pDC, WXHWND hWnd) wxOVERRIDE;
};

class UiBitmapComboBox : public wxBitmapComboBox
{
public:
  using wxBitmapComboBox::wxBitmapComboBox;

protected:
  bool MSWOnDraw(WXDRAWITEMSTRUCT * item) wxOVERRIDE;
};

class UiSlider : public wxSlider
{
public:
  using wxSlider::wxSlider;
  bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM * result) wxOVERRIDE;
};

// wx's own drawing of native-looking items (the Geosets tree list's rows) in the dark theme: wx asks
// Windows for the light Explorer list's selection and gives a selected row the system's black text.
// Installed while the dark palette is in use (wxRendererNative::Set), removed with the light one.
void UiSetDarkRenderer(bool dark);

// The status bar of the dark theme is wx's own (Windows' is light in every theme); this gives it what
// Windows' own gives and wx's lacks on Windows: a status bar for screen readers, the fields' text as its
// parts, and the full text of a cut-off field as a tooltip. (Its size grip wx draws on GTK only.)
void UiEquipGenericStatusBar(wxStatusBar * bar);

#endif // UICONTROLS_H
