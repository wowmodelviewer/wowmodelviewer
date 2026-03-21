#pragma once

// ---- Custom Title Bar (Windows) -------------------------------------------
// Removes the native title bar and replaces it with an ImGui-drawn bar
// that embeds the main menu, status text, and window controls (min/max/close),
// similar to Visual Studio and Unreal Engine.
//
// On non-Windows platforms the module is a no-op: init() does nothing and
// begin()/end() fall back to the standard ImGui main menu bar.

struct GLFWwindow;

namespace CustomTitleBar
{

/// Call once after creating the GLFW window to subclass the Win32 WndProc
/// and extend the client area into the title bar.
void init(GLFWwindow* window);

/// Begin the custom title bar.  Returns true if menus should be drawn
/// (always true on Windows; delegates to ImGui::BeginMainMenuBar otherwise).
bool begin(GLFWwindow* window);

/// Draw the window-control buttons (minimise / maximise-restore / close)
/// and the right-aligned status text, then close the title bar.
/// @param statusText  Text shown right-aligned before the window buttons.
void end(GLFWwindow* window, const char* statusText);

/// Title bar height in logical pixels (call after begin()).
float height() noexcept;

/// Merge the Windows Segoe MDL2 Assets caption-button icons into the
/// current ImGui font atlas.  Call this right after adding the main font
/// (with AddFontFromFileTTF / AddFontDefault) and before the atlas is built.
/// @param pixelSize  The pixel size used for the main UI font.
void mergeIconFont(float pixelSize);

} // namespace CustomTitleBar
