/*
 * WinTheme.h
 *
 * What Windows says about the colours apps should use, for the viewer's theme (UiStyle.h): whether its
 * app mode is dark, whether high contrast is on, and whether a message announces a change of either.
 * (Drawing dark is wxWidgets' dark mode's job.) Kept apart from wxWidgets (WinTheme.cpp includes no wx
 * header): the WinRT headers it needs clash with wx's.
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
}

#endif // WINTHEME_H
