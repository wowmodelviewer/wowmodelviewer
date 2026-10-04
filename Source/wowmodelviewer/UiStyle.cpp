/*
 * UiStyle.cpp
 *
 * The palette, the type roles and the shared helpers (see UiStyle.h).
 *
 * THE PALETTES. Two fixed sets of the same named roles, light and dark. Neutral greys a few steps apart
 * (the dock behind the panes, the panes, the fields and cards on them), one accent for what is selected
 * or primary, and the text greys. The values are fixed rather than derived from the Windows system
 * colours: Windows keeps those at the classic light values for a desktop app whatever the user's
 * theme, and colours made by lightening or darkening them only work on a light base.
 *
 * THE DARK PALETTE is the same roles a step apart in the other direction: neutral, slightly cool greys
 * (the dock darkest, the panes above it, the fields above those), light text, and the accent a little
 * lighter so it reads on dark. Not an inversion: the viewport keeps the Unity player's own dark in both
 * themes, and the dark panes sit just above it with the darker dock and 1 px borders between them.
 *
 * WHICH ONE, AND WHAT WINDOWS DRAWS. The preference (System, Light, Dark; View > Appearance) and, for
 * System, Windows' app mode (WinTheme::systemAppsMode); Windows' high-contrast mode overrides both
 * (below). Decided once per run, before the first window (WowModelViewApp::OnInit): a dark run turns on
 * wxWidgets' dark mode (wxApp::MSWEnableDarkMode), which darkens most of what Windows draws: the native
 * controls, the menu bar, Settings and the dialogs in the colours newDarkModeSettings() maps from the
 * palette; the title bars, the menus' items, message boxes and file dialogs in Windows' own dark (wx
 * draws them with Windows' dark themes and DWM). It cannot be turned on or off while the viewer runs, so
 * a change that asks for the other of light and dark is offered as a restart (restartWanted,
 * ModelViewer::OfferThemeRestart); a light run follows high contrast at once. Three native
 * parts wxWidgets leaves light or off-palette are drawn here in the dark run: the slider's channel
 * (UiSlider), the status bar (wx's own instead of Windows' black one; ModelViewer::CreateThemedStatusBar)
 * and the menu bar's titles, which Windows draws dark grey and black in the viewer's fullscreen
 * (UiMenuBarTitles; all three in UiControls.h). Where wxWidgets has no dark mode (Windows 10 before
 * 1903, build 18362), the run stays light.
 *
 * HIGH CONTRAST. While Windows' high-contrast mode is on, the roles are taken from the system colours
 * instead (window, window text, highlight, grey text), whatever the theme preference, so what the viewer
 * draws itself follows the user's contrast theme as the native controls do: at once in a light run, after
 * a restart in a dark one (wxWidgets' dark mode ignores high contrast).
 */

#include "UiStyle.h"

#include <wx/msw/darkmode.h>

#include <cmath>
#include <map>

#include <wx/dialog.h>
#include <wx/popupwin.h>

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
    // This run's light or dark, once fixed (fixSessionTheme): -1 not yet, 0 light, 1 dark.
    int sessionDark = -1;
    bool sessionLocked = false;   // light or dark cannot follow the preference (see fixSessionTheme)

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
    Palette fresh;
    if (sessionDark == 1)
    {
      // A dark run stays dark (wxWidgets' dark mode cannot be turned off): what else was asked for
      // waits for the restart (restartWanted).
      fresh = darkPalette();
      darkInUse = true;
    }
    else if (sessionDark == 0)
    {
      // A light run stays light, or follows high contrast (wxWidgets' dark mode is off: the system
      // colours are Windows' own).
      fresh = contrast ? highContrastPalette() : lightPalette();
      darkInUse = false;
    }
    else
    {
      fresh = paletteFor(chosenTheme, systemDark, contrast);
      darkInUse = isDark(chosenTheme, systemDark, contrast);
    }
    const bool changed = !samePalette(fresh, p);
    p = fresh;
    return changed;
  }

  bool darkWanted()
  {
    return isDark(chosenTheme, systemAppsDark(), highContrastOn());
  }

  void fixSessionTheme(bool dark, bool locked)
  {
    sessionLocked = locked;
    sessionDark = dark ? 1 : 0;
    refreshPalette();
  }

  bool restartWanted()
  {
    if (sessionDark < 0 || sessionLocked)
      return false;   // not fixed yet, or a restart would give the same
    // Dark wanted and not this run's, or this run dark and light (or high contrast) wanted.
    return darkWanted() != (sessionDark == 1);
  }

  namespace
  {
    // wxWidgets' dark mode in the palette's colours: the native controls, the dialogs and the menu bar take
    // the same greys as the shell instead of wxWidgets' own (Explorer's) dark greys. (Popup menus and title
    // bars stay Windows' own dark: wx draws them with Windows' dark themes and DWM.)
    class PaletteDarkModeSettings : public wxDarkModeSettings
    {
    public:
      wxColour GetColour(wxSystemColour index) override
      {
        const Palette p = darkPalette();
        switch (index)
        {
          case wxSYS_COLOUR_INFOBK:            // tooltips
            return p.controlBackground;
          case wxSYS_COLOUR_WINDOW:            // in the dark mode, also every panel's and dialog's own
          case wxSYS_COLOUR_LISTBOX:           // (the shell's fields and lists have the Field role)
          case wxSYS_COLOUR_BTNFACE:
          case wxSYS_COLOUR_APPWORKSPACE:
          case wxSYS_COLOUR_ACTIVECAPTION:
          case wxSYS_COLOUR_INACTIVECAPTION:
          case wxSYS_COLOUR_MENU:
            return p.panelBackground;
          case wxSYS_COLOUR_BTNSHADOW:         // a notebook's tabs that are not selected
            return p.appBackground;
          case wxSYS_COLOUR_MENUBAR:           // a notebook's tab borders
            return p.border;
          case wxSYS_COLOUR_HOTLIGHT:          // a notebook's tab under the mouse, the docking drop hint
            return p.accent;
          case wxSYS_COLOUR_WINDOWTEXT:
          case wxSYS_COLOUR_BTNTEXT:
          case wxSYS_COLOUR_LISTBOXTEXT:
          case wxSYS_COLOUR_MENUTEXT:
          case wxSYS_COLOUR_INFOTEXT:
          case wxSYS_COLOUR_CAPTIONTEXT:
            return p.text;
          case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
            return p.textSecondary;
          case wxSYS_COLOUR_GRAYTEXT:
            return p.textDisabled;
          case wxSYS_COLOUR_HIGHLIGHT:         // selected text
            return p.accent;
          case wxSYS_COLOUR_HIGHLIGHTTEXT:
          case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
            return p.textOnAccent;
          case wxSYS_COLOUR_LISTBOXHIGHLIGHT:  // a list's selected row
            return p.pressed;
          case wxSYS_COLOUR_GRIDLINES:
          case wxSYS_COLOUR_ACTIVEBORDER:
          case wxSYS_COLOUR_INACTIVEBORDER:
          case wxSYS_COLOUR_WINDOWFRAME:
            return p.border;
          case wxSYS_COLOUR_3DLIGHT:
            return p.separator;
          case wxSYS_COLOUR_3DDKSHADOW:
          case wxSYS_COLOUR_DESKTOP:
            return p.appBackground;
          case wxSYS_COLOUR_MENUHILIGHT:
            return p.hover;
          case wxSYS_COLOUR_BTNHIGHLIGHT:
            return p.borderStrong;
          default:
            return wxDarkModeSettings::GetColour(index);
        }
      }
      wxColour GetMenuColour(wxMenuColour which) override
      {
        const Palette p = darkPalette();
        switch (which)
        {
          case wxMenuColour::StandardFg: return p.text;
          case wxMenuColour::StandardBg: return p.panelBackground;
          case wxMenuColour::DisabledFg: return p.textDisabled;
          case wxMenuColour::HotBg: return p.hover;
        }
        return wxDarkModeSettings::GetMenuColour(which);
      }
      wxPen GetBorderPen() override
      {
        return wxPen(darkPalette().border);
      }
    };
  }

  wxDarkModeSettings * newDarkModeSettings()
  {
    return new PaletteDarkModeSettings;
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

    void applyRole(wxWindow * w, Role role)
    {
      const Palette & p = palette();
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
    // First every window without a role, from its own colour -- not the dialogs: their colours are
    // Windows' own (high contrast reaches them without us), and a field of theirs can happen to have a
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
