/*
 * WinTheme.cpp
 *
 * See WinTheme.h. No wxWidgets header here: the WinRT headers clash with wx's (INET_E_* macros).
 *
 * THE APP MODE. Microsoft's documented way for a Win32 app is UISettings.GetColorValue(Foreground): a
 * light foreground means the dark mode ("Support Dark and Light themes in Win32 apps"). It is called
 * through WRL here, which works in C++14 (C++/WinRT would need C++17 coroutines). When it cannot be
 * asked, the AppsUseLightTheme registry value decides -- not formally documented, but what wxWidgets'
 * own wxSystemAppearance::AreAppsDark, Chromium and File Explorer's neighbours read. Neither answering means
 * Unknown, which the viewer treats as light.
 */

#include "WinTheme.h"

#include <cwchar>

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <roapi.h>
#include <uxtheme.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "uxtheme.lib")

namespace WinTheme
{
  namespace
  {
    // An environment variable's value, read on every call so a test can change it while running.
    bool envValue(const wchar_t * name, wchar_t (&buffer)[16])
    {
      const DWORD n = ::GetEnvironmentVariableW(name, buffer, 16);
      return n > 0 && n < 16;
    }

    AppsMode fromUiSettings()
    {
      using Microsoft::WRL::ComPtr;
      using Microsoft::WRL::Wrappers::HStringReference;
      namespace VM = ABI::Windows::UI::ViewManagement;
      ComPtr<IInspectable> inspectable;
      if (FAILED(::RoActivateInstance(HStringReference(RuntimeClass_Windows_UI_ViewManagement_UISettings).Get(),
                                      inspectable.GetAddressOf())))
        return AppsMode::Unknown;
      ComPtr<VM::IUISettings3> settings;
      ABI::Windows::UI::Color foreground = {};
      if (FAILED(inspectable.As(&settings)) || FAILED(settings->GetColorValue(VM::UIColorType_Foreground, &foreground)))
        return AppsMode::Unknown;
      return fromForeground(foreground.R, foreground.G, foreground.B);
    }

    AppsMode fromRegistry()
    {
      DWORD value = 0, size = sizeof(value);
      const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
      return fromAppsUseLightTheme(status == ERROR_SUCCESS, value);
    }
  }

  AppsMode fromAppsUseLightTheme(bool found, unsigned long value)
  {
    if (!found)
      return AppsMode::Unknown;
    return value == 0 ? AppsMode::Dark : AppsMode::Light;
  }

  AppsMode fromForeground(unsigned char r, unsigned char g, unsigned char b)
  {
    // Microsoft's IsColorLight: a light foreground (text) colour is the dark mode.
    return (5 * g + 2 * r + b) > 8 * 128 ? AppsMode::Dark : AppsMode::Light;
  }

  AppsMode systemAppsMode()
  {
    wchar_t value[16];
    if (envValue(L"WMV_UI_SYSTEM_THEME", value))
    {
      if (_wcsicmp(value, L"dark") == 0)
        return AppsMode::Dark;
      if (_wcsicmp(value, L"light") == 0)
        return AppsMode::Light;
    }
    const AppsMode documented = fromUiSettings();
    return documented != AppsMode::Unknown ? documented : fromRegistry();
  }

  bool highContrastOn()
  {
    wchar_t value[16];
    if (envValue(L"WMV_UI_HIGH_CONTRAST", value))
      return value[0] == L'1';
    HIGHCONTRASTW hc = {};
    hc.cbSize = sizeof(hc);
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
  }

  bool isColourSettingChange(unsigned message, std::uintptr_t wParam, std::intptr_t lParam)
  {
    if (message != WM_SETTINGCHANGE)
      return false;
    if (wParam == SPI_SETHIGHCONTRAST)
      return true;
    const wchar_t * what = reinterpret_cast<const wchar_t *>(lParam);
    return what && (std::wcscmp(what, L"ImmersiveColorSet") == 0 || std::wcscmp(what, L"WindowsThemeElement") == 0);
  }

  bool setDarkTitleBar(void * hwnd, bool dark, unsigned long caption)
  {
    if (!hwnd)
      return false;
    const HWND h = static_cast<HWND>(hwnd);
    const BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004 and later, documented for Windows 11); the
    // same attribute was 19 on Windows 10 1809 to 1909.
    bool ok = SUCCEEDED(::DwmSetWindowAttribute(h, 20, &value, sizeof(value))) ||
              SUCCEEDED(::DwmSetWindowAttribute(h, 19, &value, sizeof(value)));
    // 35 = DWMWA_CAPTION_COLOR (Windows 11 22000 and later; refused before, which changes nothing).
    const COLORREF colour = static_cast<COLORREF>(caption);
    ::DwmSetWindowAttribute(h, 35, &colour, sizeof(colour));
    ::SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return ok;
  }

  void setTheme(void * hwnd, const wchar_t * app)
  {
    if (!hwnd)
      return;
    const HWND h = static_cast<HWND>(hwnd);
    ::SetWindowTheme(h, app, nullptr);
    ::SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ::RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE);
  }

  void setComboListTheme(void * combo, const wchar_t * app)
  {
    COMBOBOXINFO info = {};
    info.cbSize = sizeof(info);
    if (combo && ::GetComboBoxInfo(static_cast<HWND>(combo), &info) && info.hwndList)
      setTheme(info.hwndList, app);
  }

  void setListHeaderTheme(void * list, const wchar_t * app)
  {
    if (list)
      if (HWND header = ListView_GetHeader(static_cast<HWND>(list)))
        setTheme(header, app);
  }

  void * findChild(void * parent, const wchar_t * windowClass)
  {
    return parent ? ::FindWindowExW(static_cast<HWND>(parent), nullptr, windowClass, nullptr) : nullptr;
  }

  void setTooltipTheme(const wchar_t * app)
  {
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND h, LPARAM data) -> BOOL {
      wchar_t name[32];
      if (::GetClassNameW(h, name, 32) && std::wcscmp(name, TOOLTIPS_CLASSW) == 0)
        ::SetWindowTheme(h, reinterpret_cast<const wchar_t *>(data), nullptr);
      return TRUE;
    }, reinterpret_cast<LPARAM>(app));
  }

  void setTreeTooltipTheme(void * tree, const wchar_t * app)
  {
    if (tree)
      if (HWND tip = TreeView_GetToolTips(static_cast<HWND>(tree)))
        ::SetWindowTheme(tip, app, nullptr);
  }
}
