/*
 * UiStyle.cpp
 *
 * The palette, the type roles and the shared helpers (see UiStyle.h).
 *
 * THE PALETTES. Two fixed sets of the same named roles, light and dark. Neutral greys a few steps apart
 * (the dock behind the panes, the panes, the fields and cards on them), one accent for what is selected
 * or primary, and the text greys. The values are fixed rather than derived from the Windows system
 * colours: on Windows 10 and 11 those stay the classic light values whatever the user's theme
 * (wxWidgets 3.2 has no dark mode), and colours made by lightening or darkening them only work on a
 * light base.
 *
 * THE DARK PALETTE is the same roles a step apart in the other direction: neutral, slightly cool greys
 * (the dock darkest, the panes above it, the fields above those), light text, and the accent a little
 * lighter so it reads on dark. Not an inversion: the viewport keeps the Unity player's own dark in both
 * themes, and the dark panes sit just above it with the darker dock and 1 px borders between them.
 *
 * WHICH ONE. The preference (System, Light, Dark; View > Appearance) and, for System, Windows' app mode
 * (WinTheme::systemAppsMode); Windows' high-contrast mode overrides both (below). Resolved again
 * (refreshPalette) at start-up, when the preference changes and when Windows announces a colour change;
 * the frame then re-applies it to every window (ModelViewer::ApplyTheme: retheme, the pane art,
 * themeNativeWindow), without a restart.
 *
 * WHAT WINDOWS DRAWS, in the dark theme: Windows' own dark visual styles, the ones File Explorer uses,
 * through the documented SetWindowTheme with the theme class names Windows 10 1809 and later ship
 * (DarkMode_Explorer, DarkMode_CFD, DarkMode_ItemsView: the names are not documented; on older Windows
 * they do not exist and the control simply keeps its light style), and the title bar through the DWM
 * dark-mode attribute. With the palette's colours where the control takes colours, simple borders where
 * the themed border stays light (themeNativeWindow), a few controls drawing one light part themselves and
 * wx's own status bar instead of Windows' (UiControls.h, ModelViewer::CreateThemedStatusBar). What stays
 * light, because Windows offers no supported dark mode for it and wxWidgets 3.2 none either: the menu bar
 * and its menus (wxWidgets 3.3 darkens them through undocumented uxtheme entry points), message boxes and
 * the Open / Save dialogs, and the grey placeholder text of an empty search field (drawn by the edit
 * control). The viewer's own dialogs and Settings keep Windows' light look whole (paletteOf, keepLight):
 * their native tabs and group boxes have no dark style, and a whole light window reads better than a
 * half-dark one.
 *
 * HIGH CONTRAST. While Windows' high-contrast mode is on, the roles are taken from the system colours
 * instead (window, window text, highlight, grey text), whatever the theme preference, so what the viewer
 * draws itself follows the user's contrast theme as the native controls do.
 */

#include "UiStyle.h"

#include <cmath>
#include <functional>
#include <map>
#include <set>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/collheaderctrl.h>
#include <wx/aui/floatpane.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/headerctrl.h>
#include <wx/popupwin.h>
#include <wx/listctrl.h>
#include <wx/msw/wrapwin.h>
#include <wx/spinbutt.h>
#include <wx/srchctrl.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "UiArt.h"
#include "WinTheme.h"

namespace UiStyle
{
  namespace
  {
    // The roles from the system colours of the high-contrast theme in use.
    Palette highContrastPalette()
    {
      const auto sys = [](wxSystemColour c) { return wxSystemSettings::GetColour(c); };
      Palette p;
      p.appBackground = sys(wxSYS_COLOUR_BTNFACE);
      p.panelBackground = sys(wxSYS_COLOUR_WINDOW);
      p.controlBackground = sys(wxSYS_COLOUR_WINDOW);
      p.hover = sys(wxSYS_COLOUR_WINDOW);
      p.pressed = sys(wxSYS_COLOUR_BTNFACE);
      p.checked = sys(wxSYS_COLOUR_WINDOW);
      p.border = sys(wxSYS_COLOUR_WINDOWTEXT);
      p.borderStrong = sys(wxSYS_COLOUR_HIGHLIGHT);
      p.separator = sys(wxSYS_COLOUR_GRAYTEXT);
      p.text = sys(wxSYS_COLOUR_WINDOWTEXT);
      p.textSecondary = sys(wxSYS_COLOUR_WINDOWTEXT);
      p.textDisabled = sys(wxSYS_COLOUR_GRAYTEXT);
      p.accent = sys(wxSYS_COLOUR_HIGHLIGHT);
      p.accentHover = sys(wxSYS_COLOUR_HIGHLIGHT);
      p.accentPressed = sys(wxSYS_COLOUR_HIGHLIGHT);
      p.textOnAccent = sys(wxSYS_COLOUR_HIGHLIGHTTEXT);
      p.warning = sys(wxSYS_COLOUR_WINDOWTEXT);
      // The viewport keeps the Unity player's own dark, whatever the theme.
      p.viewport = wxColour(35, 31, 32);
      p.viewportText = wxColour(226, 222, 218);
      p.viewportTextSecondary = wxColour(160, 154, 150);
      return p;
    }

    Palette lightPalette()
    {
      Palette p;
      p.appBackground = wxColour(236, 236, 236);
      p.panelBackground = wxColour(249, 249, 249);
      p.controlBackground = wxColour(255, 255, 255);
      p.hover = wxColour(234, 234, 234);
      p.pressed = wxColour(222, 222, 222);
      p.checked = wxColour(228, 228, 228);
      p.border = wxColour(214, 214, 214);
      p.borderStrong = wxColour(184, 184, 184);
      p.separator = wxColour(226, 226, 226);
      p.text = wxColour(30, 30, 30);
      p.textSecondary = wxColour(98, 98, 98);
      p.textDisabled = wxColour(164, 164, 164);
      p.accent = wxColour(15, 108, 189);
      p.accentHover = wxColour(17, 94, 163);
      p.accentPressed = wxColour(15, 84, 140);
      p.textOnAccent = wxColour(255, 255, 255);
      p.warning = wxColour(170, 90, 0);
      // The viewport's own dark: the Unity player's background, so the handover to its picture is not
      // a flash.
      p.viewport = wxColour(35, 31, 32);
      p.viewportText = wxColour(226, 222, 218);
      p.viewportTextSecondary = wxColour(160, 154, 150);
      return p;
    }

    Palette darkPalette()
    {
      Palette p;
      p.appBackground = wxColour(22, 22, 24);
      p.panelBackground = wxColour(36, 36, 39);
      p.controlBackground = wxColour(46, 46, 50);
      p.hover = wxColour(54, 54, 59);
      p.pressed = wxColour(66, 66, 72);
      p.checked = wxColour(58, 58, 63);
      p.border = wxColour(70, 70, 76);
      p.borderStrong = wxColour(120, 120, 128);   // 3.5:1 on the panel: a toggle that is on reads as on
      p.separator = wxColour(52, 52, 57);
      p.text = wxColour(230, 230, 232);
      p.textSecondary = wxColour(164, 164, 170);
      p.textDisabled = wxColour(104, 104, 110);
      // White on the accent at least 4.5:1 (4.6), on hover and pressed more (darker, as in the light
      // palette); the accent as a line or ring on the panel 3.3:1.
      p.accent = wxColour(38, 118, 204);
      p.accentHover = wxColour(34, 108, 186);
      p.accentPressed = wxColour(30, 96, 166);
      p.textOnAccent = wxColour(255, 255, 255);
      p.warning = wxColour(232, 166, 80);
      // The viewport is the Unity player's, the same in every theme.
      p.viewport = wxColour(35, 31, 32);
      p.viewportText = wxColour(226, 222, 218);
      p.viewportTextSecondary = wxColour(160, 154, 150);
      return p;
    }

    bool samePalette(const Palette & a, const Palette & b)
    {
      const wxColour Palette::*roles[] = {
        &Palette::appBackground, &Palette::panelBackground, &Palette::controlBackground, &Palette::hover,
        &Palette::pressed, &Palette::checked, &Palette::border, &Palette::borderStrong, &Palette::separator,
        &Palette::text, &Palette::textSecondary, &Palette::textDisabled, &Palette::accent, &Palette::accentHover,
        &Palette::accentPressed, &Palette::textOnAccent, &Palette::warning, &Palette::viewport,
        &Palette::viewportText, &Palette::viewportTextSecondary };
      for (const auto role : roles)
        if (a.*role != b.*role)
          return false;
      return true;
    }

    Theme chosenTheme = Theme::System;
    bool darkInUse = false;

    Palette & current()
    {
      static Palette p = [] {
        const bool systemDark = systemAppsDark(), contrast = highContrastOn();
        darkInUse = isDark(chosenTheme, systemDark, contrast);
        return paletteFor(chosenTheme, systemDark, contrast);
      }();
      return p;
    }
  }

  Theme themeFromSetting(int value)
  {
    switch (value)
    {
      case 1: return Theme::Light;
      case 2: return Theme::Dark;
      default: return Theme::System;
    }
  }

  int themeToSetting(Theme theme)
  {
    switch (theme)
    {
      case Theme::Light: return 1;
      case Theme::Dark: return 2;
      default: return 0;
    }
  }

  Theme themePreference()
  {
    return chosenTheme;
  }

  void setThemePreference(Theme theme)
  {
    chosenTheme = theme;
  }

  bool systemAppsDark()
  {
    return WinTheme::systemAppsMode() == WinTheme::AppsMode::Dark;   // Unknown counts as light
  }

  bool highContrastOn()
  {
    return WinTheme::highContrastOn();
  }

  bool isDark(Theme preference, bool systemDark, bool highContrast)
  {
    if (highContrast)
      return false;
    return preference == Theme::Dark || (preference == Theme::System && systemDark);
  }

  Palette paletteFor(Theme preference, bool systemDark, bool highContrast)
  {
    if (highContrast)
      return highContrastPalette();
    return isDark(preference, systemDark, highContrast) ? darkPalette() : lightPalette();
  }

  const Palette & palette()
  {
    return current();
  }

  bool darkActive()
  {
    current();
    return darkInUse;
  }

  bool refreshPalette()
  {
    Palette & p = current();
    const bool systemDark = systemAppsDark(), contrast = highContrastOn();
    const Palette fresh = paletteFor(chosenTheme, systemDark, contrast);
    darkInUse = isDark(chosenTheme, systemDark, contrast);
    const bool changed = !samePalette(fresh, p);
    p = fresh;
    return changed;
  }

  wxFont font(Type role)
  {
    wxFont f = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    switch (role)
    {
      case Type::Normal:
      case Type::Secondary:
        break;
      case Type::Section:
      case Type::Strong:
        f.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        break;
      case Type::Title:
        f.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        f.SetFractionalPointSize(f.GetFractionalPointSize() + 3);
        break;
    }
    return f;
  }

  // ---- roles

  namespace
  {
    std::map<wxWindow *, Role> & roles()
    {
      static std::map<wxWindow *, Role> map;
      return map;
    }

    // The windows kept light (keepLight).
    std::set<wxWindow *> & lightWindows()
    {
      static std::set<wxWindow *> set;
      return set;
    }

    void applyRole(wxWindow * w, Role role)
    {
      const Palette & p = paletteOf(w);
      switch (role)
      {
        case Role::Panel:
          w->SetBackgroundColour(p.panelBackground);
          w->SetForegroundColour(p.text);
          break;
        case Role::Card:
          w->SetOwnBackgroundColour(p.controlBackground);
          w->SetForegroundColour(p.text);
          break;
        case Role::Field:
          w->SetBackgroundColour(p.controlBackground);
          w->SetForegroundColour(p.text);
          break;
        case Role::Separator:
          w->SetBackgroundColour(p.separator);
          break;
        case Role::Text:
          w->SetForegroundColour(p.text);
          break;
        case Role::SecondaryText:
          w->SetForegroundColour(p.textSecondary);
          break;
        case Role::WarningText:
          w->SetForegroundColour(p.warning);
          break;
        case Role::Viewport:
          w->SetBackgroundColour(p.viewport);
          break;
      }
    }

    // A colour of the previous palette, as the same role's colour in the new one.
    bool mapColour(const wxColour & colour, const Palette & from, const Palette & to,
                   std::initializer_list<wxColour Palette::*> order, wxColour & out)
    {
      for (const auto role : order)
        if (colour == from.*role)
        {
          out = to.*role;
          return true;
        }
      return false;
    }

    void rethemeWindow(wxWindow * w, const Palette & from, const Palette & to)
    {
      // A window kept light keeps its colours, whatever they happen to equal.
      if (lightWindows().count(w))
        return;
      // A window with a role takes its role's colours afterwards (retheme); the others from their own.
      if (roles().find(w) == roles().end())
      {
        wxColour c;
        if (w->UseBgCol() && mapColour(w->GetBackgroundColour(), from, to,
                                       { &Palette::panelBackground, &Palette::controlBackground, &Palette::appBackground,
                                         &Palette::separator, &Palette::viewport }, c))
        {
          if (w->InheritsBackgroundColour())
            w->SetBackgroundColour(c);
          else
            w->SetOwnBackgroundColour(c);
        }
        if (w->UseForegroundColour() && mapColour(w->GetForegroundColour(), from, to,
                                                  { &Palette::text, &Palette::textSecondary, &Palette::warning,
                                                    &Palette::textDisabled }, c))
        {
          if (w->InheritsForegroundColour())
            w->SetForegroundColour(c);
          else
            w->SetOwnForegroundColour(c);
        }
      }
      // Its children, and a popup it owns (the Mount picker), which is not among the top-level windows.
      for (wxWindow * child : w->GetChildren())
        if (!child->IsTopLevel() || dynamic_cast<wxPopupWindow *>(child))
          rethemeWindow(child, from, to);
    }
  }

  void setRole(wxWindow * window, Role role)
  {
    if (!window)
      return;
    if (roles().find(window) == roles().end())
      window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent & e) {
        if (e.GetEventObject() == window)
          roles().erase(window);
        e.Skip();
      });
    roles()[window] = role;
    applyRole(window, role);
  }

  void retheme(const Palette & previous)
  {
    const Palette & now = palette();
    // First every window without a role, from its own colour -- not the dialogs: the shell does not style
    // them (they keep Windows' light look, see keepLight), and a field of theirs can happen to have a
    // palette colour (white).
    for (wxWindow * top : wxTopLevelWindows)
      if (!dynamic_cast<wxDialog *>(top))
      {
        rethemeWindow(top, previous, now);
        top->Refresh();
      }
    // Then every window with a role (a popup's too). Last, because wx passes a window's colours on to its
    // parts (a data view's rows, a search field's edit): a part matched by its colour above could match
    // the wrong role where two roles share a colour (high contrast), and its role's window corrects it.
    for (const auto & entry : roles())
    {
      applyRole(entry.first, entry.second);
      entry.first->Refresh();
    }
  }

  // ---- native windows

  namespace
  {
    // Windows whose themed border (light in every Windows theme) was made a simple one for the dark theme,
    // with the style and extended style they had (wx gives a native control's themed border a client
    // edge, its own windows none; changing the border style at run time leaves that edge, drawn as a
    // classic sunken one).
    struct Styles
    {
      long style;
      LONG_PTR ex;
    };
    std::map<wxWindow *, Styles> & simpleBorders()
    {
      static std::map<wxWindow *, Styles> map;
      return map;
    }

    void setExStyle(wxWindow * w, LONG_PTR ex)
    {
      const HWND h = (HWND)w->GetHWND();
      ::SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
      ::SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void darkBorder(wxWindow * w, bool dark)
    {
      const long style = w->GetWindowStyleFlag();
      auto & map = simpleBorders();
      const auto found = map.find(w);
      // The border it has, its class's default included (a search field or a data view asks for none and
      // gets the themed one).
      if (dark && found == map.end() && w->GetBorder() == wxBORDER_THEME)
      {
        const LONG_PTR ex = ::GetWindowLongPtrW((HWND)w->GetHWND(), GWL_EXSTYLE);
        map[w] = Styles{ style, ex };
        w->Bind(wxEVT_DESTROY, [w](wxWindowDestroyEvent & e) {
          if (e.GetEventObject() == w)
            simpleBorders().erase(w);
          e.Skip();
        });
        w->SetWindowStyleFlag((style & ~wxBORDER_MASK) | wxBORDER_SIMPLE);
        setExStyle(w, ex & ~(LONG_PTR)WS_EX_CLIENTEDGE);
      }
      else if (!dark && found != map.end())
      {
        const Styles was = found->second;
        map.erase(found);
        w->SetWindowStyleFlag((style & ~wxBORDER_MASK) | (was.style & wxBORDER_MASK));
        setExStyle(w, was.ex);
      }
    }

    // A window kept light, a window inside one, or the floating frame one is shown in.
    bool keptLight(wxWindow * w)
    {
      for (wxWindow * p = w; p; p = p->GetParent())
      {
        if (lightWindows().count(p))
          return true;
        if (p->IsTopLevel())
          break;
      }
      if (dynamic_cast<wxAuiFloatingFrame *>(w))
        for (wxWindow * light : lightWindows())
          if (wxGetTopLevelParent(light) == w)
            return true;
      return false;
    }

    // A window the shell leaves to Windows' light look: inside a dialog, or kept light.
    bool outsideShell(const wxWindow * window)
    {
      wxWindow * w = const_cast<wxWindow *>(window);
      return keptLight(w) || dynamic_cast<wxDialog *>(wxGetTopLevelParent(w)) != nullptr;
    }

    unsigned long colourRef(const wxColour & c)
    {
      return (unsigned long)c.Red() | ((unsigned long)c.Green() << 8) | ((unsigned long)c.Blue() << 16);
    }
  }

  void themeNativeWindow(wxWindow * w)
  {
    void * hwnd = w ? w->GetHWND() : nullptr;
    if (!hwnd)
      return;
    const bool dark = darkActive() && !outsideShell(w);
    const Palette & p = palette();
    const wchar_t * explorer = dark ? L"DarkMode_Explorer" : nullptr;
    if (w->IsTopLevel())
    {
      // The title bar dark, its caption the command bar's colour (Windows 11); Windows' own otherwise. (A
      // popup has none.)
      if (dynamic_cast<wxTopLevelWindow *>(w))
        WinTheme::setDarkTitleBar(hwnd, dark, dark ? colourRef(p.panelBackground) : 0xFFFFFFFF);
      return;
    }
    // C++ casts: wx's own type information gives wxComboBox wxControl as its base, not wxChoice.
    if (wxChoice * combo = dynamic_cast<wxChoice *>(w))   // also wxComboBox and wxBitmapComboBox
    {
      // The field and the list in the palette's colours, the face, border and arrow in the dark combo
      // style; in the light theme, Windows' own.
      combo->SetBackgroundColour(dark ? p.controlBackground : wxNullColour);
      combo->SetForegroundColour(dark ? p.text : wxNullColour);
      WinTheme::setTheme(hwnd, dark ? L"DarkMode_CFD" : nullptr);
      WinTheme::setComboListTheme(hwnd, explorer);
      return;
    }
    if (wxListCtrl * list = dynamic_cast<wxListCtrl *>(w))
    {
      // Its colours are the Field role's. Dark scroll bars and selection, a dark header with the text
      // colour (the header's own text would stay black), a simple border.
      WinTheme::setTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer");
      WinTheme::setListHeaderTheme(hwnd, dark ? L"DarkMode_ItemsView" : nullptr);
      list->SetHeaderAttr(dark ? wxItemAttr(p.text, wxNullColour, wxNullFont) : wxItemAttr());
      darkBorder(w, dark);
      return;
    }
    if (dynamic_cast<wxTreeCtrl *>(w))
    {
      WinTheme::setTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer");
      WinTheme::setTreeTooltipTheme(hwnd, explorer);
      return;
    }
    if (wxDataViewCtrl * view = dynamic_cast<wxDataViewCtrl *>(w))
    {
      // The tree list (Geosets): check boxes, expanders and scroll bars in the dark Explorer style (the
      // rows' window draws the expanders), its header as a list's, a simple border; its rows' colours are
      // the Field role's (and the dark renderer's). In the light theme the Explorer style wx gives it.
      WinTheme::setTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer");
      if (wxWindow * rows = view->GetMainWindow())
        WinTheme::setTheme(rows->GetHWND(), dark ? L"DarkMode_Explorer" : L"Explorer");
      if (wxHeaderCtrl * header = view->GenericGetHeader())
      {
        void * native = WinTheme::findChild(header->GetHWND(), L"SysHeader32");
        WinTheme::setTheme(native ? native : header->GetHWND(), dark ? L"DarkMode_ItemsView" : nullptr);
      }
      view->SetHeaderAttr(dark ? wxItemAttr(p.text, wxNullColour, wxNullFont) : wxItemAttr());
      darkBorder(w, dark);
      return;
    }
    if (dynamic_cast<wxTextCtrl *>(w) && !dynamic_cast<wxSearchCtrl *>(w->GetParent()))
    {
      WinTheme::setTheme(hwnd, dark ? L"DarkMode_CFD" : nullptr);
      return;
    }
    if (dynamic_cast<wxSearchCtrl *>(w))
    {
      darkBorder(w, dark);   // the Mount picker's search field (the others have none: UiSearchFrame frames them)
      return;
    }
    if (dynamic_cast<wxCheckBox *>(w) || dynamic_cast<wxSpinButton *>(w) || dynamic_cast<wxCollapsibleHeaderCtrl *>(w) ||
        w->HasFlag(wxVSCROLL) || w->HasFlag(wxHSCROLL))
      WinTheme::setTheme(hwnd, explorer);   // the check box's box, the spin arrows, the expander, scroll bars
  }

  const Palette & paletteOf(const wxWindow * window)
  {
    if (!window || !outsideShell(window))
      return palette();
    static Palette light;
    light = paletteFor(Theme::Light, false, highContrastOn());
    return light;
  }

  void keepLight(wxWindow * window)
  {
    if (!window || !lightWindows().insert(window).second)
      return;
    window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent & e) {
      if (e.GetEventObject() == window)
        lightWindows().erase(window);
      e.Skip();
    });
    // Its windows were themed as they were made, before this: Windows' light look for them now.
    std::function<void(wxWindow *)> walk = [&](wxWindow * w) {
      themeNativeWindow(w);
      for (wxWindow * child : w->GetChildren())
        if (!child->IsTopLevel())
          walk(child);
    };
    walk(window);
  }

  wxColour readableOn(const wxColour & colour, const wxColour & background, double ratio)
  {
    const auto luminance = [](const wxColour & c) {
      const auto channel = [](unsigned char v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * channel(c.Red()) + 0.7152 * channel(c.Green()) + 0.0722 * channel(c.Blue());
    };
    const double back = luminance(background);
    const bool lighten = back < 0.5;
    wxColour c = colour;
    for (int step = 1; step <= 20; step++)
    {
      const double l = luminance(c);
      const double contrast = lighten ? (l + 0.05) / (back + 0.05) : (back + 0.05) / (l + 0.05);
      if (contrast >= ratio)
        break;
      // A twentieth of the way further towards white (on a dark background) or black.
      const double t = step / 20.0;
      const int target = lighten ? 255 : 0;
      c = wxColour((unsigned char)(colour.Red() + (target - colour.Red()) * t),
                   (unsigned char)(colour.Green() + (target - colour.Green()) * t),
                   (unsigned char)(colour.Blue() + (target - colour.Blue()) * t));
    }
    return c;
  }

  wxStaticText * secondaryLabel(wxWindow * parent, const wxString & text, wxWindowID id)
  {
    wxStaticText * label = new wxStaticText(parent, id, text);
    setRole(label, Role::SecondaryText);
    return label;
  }

  void applyPanel(wxWindow * panel)
  {
    setRole(panel, Role::Panel);
  }

  void applyPanelWithChildren(wxWindow * panel)
  {
    if (!panel)
      return;
    setRole(panel, Role::Panel);
    for (wxWindow * child : panel->GetChildren())
      setRole(child, Role::Panel);
  }

  wxWindow * separator(wxWindow * parent, wxOrientation orientation)
  {
    // A window one pixel thick in the separator colour: wxStaticLine is a 2 px etched pair on Windows.
    wxWindow * line = new wxWindow(parent, wxID_ANY, wxDefaultPosition,
                                   orientation == wxHORIZONTAL ? wxSize(-1, 1) : wxSize(1, -1), wxBORDER_NONE);
    setRole(line, Role::Separator);
    line->SetMinSize(orientation == wxHORIZONTAL ? wxSize(-1, 1) : wxSize(1, -1));
    line->Enable(false);   // never takes the mouse or the keyboard
    return line;
  }

  wxSizer * sectionHeader(wxWindow * parent, const wxString & title, wxStaticText ** labelOut)
  {
    wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText * label = new wxStaticText(parent, wxID_ANY, title);
    label->SetFont(font(Type::Section));
    setRole(label, Role::Text);
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(separator(parent), 1, wxALIGN_CENTER_VERTICAL | wxLEFT, dip(parent, S));
    if (labelOut)
      *labelOut = label;
    return row;
  }

  void applyDockArt(wxAuiManager & manager, const wxWindow * frame)
  {
    manager.SetArtProvider(new UiDockArt(frame));
  }
}
