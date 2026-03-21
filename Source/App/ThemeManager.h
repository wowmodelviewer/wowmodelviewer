#pragma once

struct GLFWwindow;

namespace ThemeManager
{

enum Theme
{
    Dark = 0,
    Light,
    UE5,
    Count
};

/// Apply the given theme to the current ImGui context and update the
/// native title-bar colour on Windows.  Pass the GLFW window so the
/// DWM caption colour can be set (may be nullptr if unavailable).
void apply(Theme theme, GLFWwindow* window);

/// Currently active theme index.
[[nodiscard]] Theme  currentTheme() noexcept;

/// Writable reference so ImGui::Combo can bind directly.
[[nodiscard]] int&   currentThemeRef() noexcept;

/// Human-readable name for a theme.
[[nodiscard]] const char* themeName(int index) noexcept;

/// Pointer to the contiguous name array (for ImGui::Combo items).
[[nodiscard]] const char* const* themeNames() noexcept;

/// Number of available themes.
[[nodiscard]] constexpr int themeCount() noexcept { return static_cast<int>(Count); }

} // namespace ThemeManager
