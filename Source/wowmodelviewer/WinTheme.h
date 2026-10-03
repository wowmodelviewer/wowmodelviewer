/*
 * WinTheme.h
 *
 * What Windows says about the colours apps should use, for the viewer's theme (UiStyle.h): whether its
 * app mode is dark, whether high contrast is on, whether a message announces a change of either, and the
 * two native theme calls the dark theme uses (the title bar, the Explorer-style controls). Kept apart
 * from wxWidgets (WinTheme.cpp includes no wx header): the WinRT headers it needs clash with wx's.
 */

#ifndef WINTHEME_H
#define WINTHEME_H

#include <cstdint>

namespace WinTheme
{
  enum class AppsMode { Unknown, Light, Dark };

  // The rules, without asking Windows (for tests).
  AppsMode fromAppsUseLightTheme(bool found, unsigned long value);
  AppsMode fromForeground(unsigned char r, unsigned char g, unsigned char b);

  // Windows' app mode (Settings > Personalisation > Colours > "Choose your app mode"): the documented
  // UISettings foreground colour, else the AppsUseLightTheme value, else Unknown. The test harness can
  // replace the answer with WMV_UI_SYSTEM_THEME=dark|light (read on every call).
  AppsMode systemAppsMode();
  // Windows' high-contrast mode (SPI_GETHIGHCONTRAST). WMV_UI_HIGH_CONTRAST=0|1 replaces it for tests.
  bool highContrastOn();

  // A WM_SETTINGCHANGE that can change the app mode or high contrast ("ImmersiveColorSet",
  // "WindowsThemeElement", SPI_SETHIGHCONTRAST).
  bool isColourSettingChange(unsigned message, std::uintptr_t wParam, std::intptr_t lParam);

  // The title bar of a top-level window drawn dark or light (DWMWA_USE_IMMERSIVE_DARK_MODE: documented
  // for Windows 11; the same attribute numbered 19 on Windows 10 1809-1909). False when Windows has no
  // such attribute (the title bar then stays as it is).
  // caption: the caption's colour as 0x00BBGGRR (Windows 11's DWMWA_CAPTION_COLOR; ignored before), or
  // Windows' default colour with defaultCaption.
  bool setDarkTitleBar(void * hwnd, bool dark, unsigned long caption = 0xFFFFFFFF);

  // A window's visual-styles theme (SetWindowTheme): app is the theme class prefix, nullptr for the
  // window's default. The dark ones are Windows' own: "DarkMode_Explorer" (trees, lists, scroll bars,
  // check boxes, spin buttons, tooltips), "DarkMode_CFD" (combo boxes, edit borders), "DarkMode_ItemsView"
  // (list headers). They ship in Windows 10 1809 and later and are what File Explorer uses; Microsoft
  // does not document the names. The window repaints, its scroll bars and border too.
  void setTheme(void * hwnd, const wchar_t * app);
  // The drop-down list of a combo box.
  void setComboListTheme(void * combo, const wchar_t * app);
  // The header of a list view.
  void setListHeaderTheme(void * list, const wchar_t * app);
  // The first child window of a class ("SysHeader32"...), or nullptr.
  void * findChild(void * parent, const wchar_t * windowClass);
  // Every tooltip window of the calling thread, and a tree view's own one.
  void setTooltipTheme(const wchar_t * app);
  void setTreeTooltipTheme(void * tree, const wchar_t * app);
}

#endif // WINTHEME_H
