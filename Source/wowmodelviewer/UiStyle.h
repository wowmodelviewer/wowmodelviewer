/*
 * UiStyle.h
 *
 * THE VIEWER'S DESIGN SYSTEM: the one place its look is decided. Spacing, control sizes, the type
 * roles and the colour palette live here, and every panel, toolbar, button and pane caption takes
 * them from here instead of picking its own pixels and RGB values. The pieces built on it:
 *   - UiArt.h: the pane chrome (captions, sashes, borders) and the toolbars (the command bar, the
 *     texture view's tools), drawn flat through wxAUI's art providers;
 *   - UiControls.h: buttons by importance (primary, secondary, subtle), the search field, the tab
 *     strip, 1 px separators;
 *   - UiIcons.h: the few icons, drawn from one icon family.
 *
 * The direction: flat and restrained, a modern desktop tool rather than a web page. Hierarchy comes
 * from weight, size and secondary colour, not from borders; the viewport is the thing to look at and
 * the panels around it read as quiet tools. Native controls stay native where Windows draws them
 * well (combo boxes, check boxes, sliders, menus, the status bar).
 *
 * Everything is in DIPs and goes through FromDIP. (The process is not DPI-aware today: above 100%
 * scaling Windows stretches the whole window, and FromDIP returns its input. The sizes here are
 * already in the form a DPI-aware build needs.)
 *
 * COLOURS ARE ROLES, NOT VALUES. Code asks for palette().text or palette().separator, never for an
 * RGB, so a dark palette later is a second Palette, not an edit of every control (see UiStyle.cpp).
 */

#ifndef UISTYLE_H
#define UISTYLE_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/aui/aui.h>
#include <wx/statline.h>

class wxDarkModeSettings;

namespace UiStyle
{
  // ---- Spacing scale (DIP). XS between a label and its control, S between rows, M around a
  // panel's content, L between sections.
  const int XS = 4;
  const int S = 8;
  const int M = 12;
  const int L = 16;

  // ---- Control sizes (DIP).
  const int ControlHeight = 24;        // single-line native controls (combo boxes, choices)
  const int ButtonHeight = 28;         // a button
  const int CompactButtonHeight = 24;  // a button in a dense row (equipment slots, transport)
  const int SearchHeight = 28;         // the search field's frame
  const int ToolbarHeight = 40;        // the command bar
  const int ToolHeight = 28;           // a tool on a toolbar
  const int TabHeight = 32;            // the tab strip
  const int TreeRowHeight = 22;        // a row of Browse's tree
  const int IconSize = 16;             // an icon on a button or a tool
  const int Radius = 4;                // the corner of a button, a field, a segment
  // The pane chrome keeps the sizes the docked layout has always had (22 / 5 / 1), so the viewport
  // keeps its size in a saved layout and in headless runs.
  const int CaptionHeight = 22;
  const int SashSize = 5;

  // ---- Colour roles.
  struct Palette
  {
    wxColour appBackground;          // behind the panes: the dock background and the sashes
    wxColour panelBackground;        // a pane's body, the command bar
    wxColour controlBackground;      // a field, a list, a card, a secondary button
    wxColour hover;                  // an item under the mouse (on a panel)
    wxColour pressed;                // an item held down
    wxColour checked;                // a pressed toggle that is not the accent (a pane toggle)
    wxColour border;                 // a control's or a card's outline
    wxColour borderStrong;           // an outline under the mouse
    wxColour separator;              // a hairline between sections, under a toolbar or caption
    wxColour text;
    wxColour textSecondary;          // hints, units, counts, a quiet caption
    wxColour textDisabled;
    wxColour accent;                 // the selected mode, the primary action, focus
    wxColour accentHover;
    wxColour accentPressed;
    wxColour textOnAccent;
    wxColour warning;                // a notice in text (the Geosets page)
    wxColour viewport;               // the centre pane while no picture covers it
    wxColour viewportText;
    wxColour viewportTextSecondary;
  };
  // ---- Themes. The preference (View > Appearance; Config.ini Settings/Appearance): System follows
  // Windows' app mode (Settings > Personalisation > Colours), Light and Dark are fixed. Windows'
  // high-contrast mode overrides all three: the palette is then made of the system colours (in a dark run,
  // from the next start: see fixSessionTheme).
  enum class Theme { System, Light, Dark };
  // As kept in Config.ini (Settings/Appearance): 0 System, 1 Light, 2 Dark (anything else reads as System).
  Theme themeFromSetting(int value);
  int themeToSetting(Theme theme);
  Theme themePreference();
  // Sets the preference. Once the run is fixed (fixSessionTheme) it changes only what restartWanted() says.
  void setThemePreference(Theme theme);
  // Windows' app mode is dark (the documented-in-practice AppsUseLightTheme value; light when unknown).
  bool systemAppsDark();
  // Windows' high-contrast mode is on.
  bool highContrastOn();
  // The palette a preference gives with Windows in a given state (no side effects: for tests too).
  Palette paletteFor(Theme preference, bool systemDark, bool highContrast);
  bool isDark(Theme preference, bool systemDark, bool highContrast);

  // The palette in use.
  const Palette & palette();
  // True in a dark run (wxWidgets' dark mode on).
  bool darkActive();
  // Resolves the palette again; true when it changed. Before the run is fixed, from the preference and
  // Windows' state; after, a dark run keeps the dark palette and a light run follows high contrast.
  bool refreshPalette();

  // ---- This run's theme. wxWidgets' dark mode is decided once, before the first window, and cannot be
  // switched while the viewer runs. darkWanted(): the preference, Windows' app mode and high contrast now
  // ask for the dark theme. fixSessionTheme(dark, locked) fixes, for this run, whether it is light or dark:
  // darkActive() keeps that answer, a light run still follows high contrast, and a change of the
  // preference or of Windows that asks for the other one waits for a restart (restartWanted()) -- unless
  // the run is locked: a restart would give the same (no dark mode on this Windows, or wxWidgets' own
  // msw.dark-mode option forcing it).
  bool darkWanted();
  void fixSessionTheme(bool dark, bool locked);
  bool restartWanted();
  // wxWidgets' dark mode settings in the palette's colours, for wxApp::MSWEnableDarkMode (which owns it).
  wxDarkModeSettings * newDarkModeSettings();

  // ---- Type roles: one family (the system UI font), hierarchy by weight, size and colour.
  enum class Type
  {
    Normal,      // content
    Secondary,   // hints and metadata (same font; palette().textSecondary)
    Section,     // a section title, a pane caption
    Strong,      // an action's label, an emphasised value
    Title        // a notice's title, the name of what is shown
  };
  wxFont font(Type role);

  inline int dip(const wxWindow * w, int v) { return w ? w->FromDIP(v) : v; }

  // ---- Colour roles of windows. A window given a role takes its colours from the palette in use and
  // takes them again when the theme changes (retheme).
  enum class Role
  {
    Panel,           // a pane's body: panel background, text colour (inherited by its labels, check boxes)
    Card,            // a card on a panel: the field colour as its own (not inherited) background
    Field,           // a native field: field background, text colour
    Separator,       // a 1 px line
    Text,            // a label in the text colour
    SecondaryText,   // a hint, a count, a path
    WarningText,     // a notice
    Viewport         // the viewport's own dark
  };
  void setRole(wxWindow * window, Role role);

  // After the palette changed (refreshPalette), every window of the application takes its colours from
  // the new one: those with a role, and any other window whose colour was one of the previous palette's
  // (a label that inherited its panel's text colour when it was made). Then everything is repainted.
  void retheme(const Palette & previous);

  // A colour as close to the given one as reads on a background: mixed towards white (on a dark
  // background) or black until its contrast is at least ratio (WCAG: 4.5 for text). The colour itself when
  // it already reads.
  wxColour readableOn(const wxColour & colour, const wxColour & background, double ratio = 4.5);

  // ---- Helpers the panels share.
  inline wxColour secondaryText() { return palette().textSecondary; }
  wxStaticText * secondaryLabel(wxWindow * parent, const wxString & text, wxWindowID id = wxID_ANY);

  // A panel's body: its background and text colour (inherited by its labels and check boxes).
  void applyPanel(wxWindow * panel);
  // The same for a wxCollapsiblePane: the pane, its header and its content window.
  void applyPanelWithChildren(wxWindow * panel);

  // A card: a few controls that belong together and have to stand out from the rows around them
  // (the Model panel's Mount card).
  inline wxColour cardBackground() { return palette().controlBackground; }
  inline wxColour cardBorder() { return palette().border; }

  // A 1 px line in the separator colour, across (wxHORIZONTAL) or down (wxVERTICAL) its sizer slot.
  wxWindow * separator(wxWindow * parent, wxOrientation orientation = wxHORIZONTAL);

  // A section title: a semibold label followed by a hairline across the remaining width.
  wxSizer * sectionHeader(wxWindow * parent, const wxString & title, wxStaticText ** labelOut = nullptr);

  // The pane chrome for the docking manager (UiDockArt): flat captions, a quiet close button, thin
  // sashes. Replaces the manager's art provider.
  void applyDockArt(wxAuiManager & manager, const wxWindow * frame);
}

#endif // UISTYLE_H
