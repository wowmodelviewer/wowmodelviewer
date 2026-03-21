// ---- Custom Title Bar (Windows implementation) ----------------------------
// Subclasses the Win32 window to remove the native title bar and handles
// WM_NCCALCSIZE / WM_NCHITTEST so the window can still be dragged, resized
// and snapped.  The visible title bar is drawn entirely with Dear ImGui.
//
// On non-Windows platforms every function is a thin wrapper around the
// standard ImGui main-menu bar.

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#pragma comment(lib, "dwmapi.lib")
#endif

#include "CustomTitleBar.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------
namespace
{

#ifdef _WIN32
    WNDPROC g_origWndProc = nullptr;
    HWND    g_hwnd        = nullptr;

    // The pixel-rect of the window-control buttons (min/max/close) in
    // client coordinates.  Anything inside this rect returns HTCLIENT so
    // that ImGui handles the click instead of Windows treating it as a
    // caption drag.
    RECT g_controlButtonsRect = {};

    // The pixel-rect of the menu items.  Also returns HTCLIENT.
    RECT g_menuRect = {};
#endif

    // The height of the custom title bar in logical (unscaled) pixels.
    // Updated every frame in begin().
    float g_titleBarHeight = 0.0f;

    // Separate ImFont for the Segoe MDL2 Assets caption-button icons,
    // loaded at a larger pixel size than the UI text font.
    ImFont* g_iconFont    = nullptr;
    bool    g_hasIconFont = false;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Win32 custom frame
// ---------------------------------------------------------------------------
#ifdef _WIN32

static LRESULT CALLBACK customWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // ---- Remove the default non-client frame ----
    case WM_NCCALCSIZE:
    {
        if (wParam == TRUE)
        {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

            // When maximised, the OS inflates the window by the frame thickness
            // so edges extend beyond the monitor.  Compensate so the content
            // stays within the work area.
            if (IsZoomed(hwnd))
            {
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfo(mon, &mi);
                params->rgrc[0] = mi.rcWork;
            }
            return 0;  // returning 0 removes the default title bar
        }
        break;
    }

    // ---- Hit-testing: resize borders + caption drag ----
    case WM_NCHITTEST:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetWindowRect(hwnd, &rc);

        // Resize borders are only active when the window is not maximised.
        if (!IsZoomed(hwnd))
        {
            // Resize-border thickness (use the real system metric)
            const int border = GetSystemMetrics(SM_CXSIZEFRAME) +
                               GetSystemMetrics(SM_CXPADDEDBORDER);

            // --- Resize borders (corners first, then edges) ---
            const bool onLeft   = x < rc.left   + border;
            const bool onRight  = x > rc.right  - border;
            const bool onTop    = y < rc.top    + border;
            const bool onBottom = y > rc.bottom - border;

            if (onTop && onLeft)     return HTTOPLEFT;
            if (onTop && onRight)    return HTTOPRIGHT;
            if (onBottom && onLeft)  return HTBOTTOMLEFT;
            if (onBottom && onRight) return HTBOTTOMRIGHT;
            if (onLeft)              return HTLEFT;
            if (onRight)             return HTRIGHT;
            if (onTop)               return HTTOP;
            if (onBottom)            return HTBOTTOM;
        }

        // --- Title bar area ---
        // Convert to client coordinates for comparison with ImGui rects.
        // GLFW reports physical pixels via GetClientRect/ClientToScreen,
        // and ImGui uses those directly, so no DPI scaling is needed.
        POINT pt = { x, y };
        ScreenToClient(hwnd, &pt);

        const int titleH = static_cast<int>(g_titleBarHeight);

        if (pt.y < titleH)
        {
            // If the cursor is over the window-control buttons or the menu,
            // let ImGui handle the interaction.
            if (PtInRect(&g_controlButtonsRect, pt) || PtInRect(&g_menuRect, pt))
                return HTCLIENT;

            // Everything else in the title-bar strip is draggable caption.
            return HTCAPTION;
        }

        return HTCLIENT;
    }

    // Preserve the thin shadow / border around the window even without a
    // visible non-client frame.
    case WM_ACTIVATE:
    {
        MARGINS m = { 0, 0, 0, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &m);
        break;
    }

    default:
        break;
    }

    return CallWindowProc(g_origWndProc, hwnd, msg, wParam, lParam);
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void CustomTitleBar::init(GLFWwindow* window)
{
#ifdef _WIN32
    if (!window)
        return;

    g_hwnd = glfwGetWin32Window(window);
    if (!g_hwnd)
        return;

    // Subclass the window
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(customWndProc)));

    // Extend the client area so we get the DWM drop-shadow
    MARGINS m = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &m);

    // Force the frame to be recalculated so the title bar disappears
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                 SWP_NOZORDER | SWP_NOOWNERZORDER);
#else
    (void)window;
#endif
}

bool CustomTitleBar::begin([[maybe_unused]] GLFWwindow* window)
{
#ifdef _WIN32
    // We draw a full-width window at the top that acts as the title bar.
    // Use ImGuiWindowFlags_MenuBar so we can embed menus inside it.
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Height: menu-bar frame padding * 2 + font size + a little extra for
    // the window-control buttons so they feel comfortable.
    const float frameH = ImGui::GetFrameHeight();
    g_titleBarHeight = frameH + ImGui::GetStyle().FramePadding.y * 2.0f;

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, g_titleBarHeight));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_MenuBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##CustomTitleBar", nullptr, flags);
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(3);

    // Begin the embedded menu bar
    if (ImGui::BeginMenuBar())
    {
        // Record the menu starting position so we can track its extent.
        // GLFW/ImGui coordinates are already in the same physical-pixel
        // space as ScreenToClient, so no DPI scaling is needed.
        const float menuStartX = ImGui::GetCursorScreenPos().x - vp->Pos.x;
        g_menuRect.left   = static_cast<LONG>(menuStartX);
        g_menuRect.top    = 0;
        g_menuRect.bottom = static_cast<LONG>(g_titleBarHeight);
        return true;
    }

    ImGui::End();
    return false;
#else
    return ImGui::BeginMainMenuBar();
#endif
}

void CustomTitleBar::end([[maybe_unused]] GLFWwindow* window, const char* statusText)
{
#ifdef _WIN32
    // Finish recording how wide the menus are (in client coords)
    {
        const float menuEndX = ImGui::GetCursorScreenPos().x - ImGui::GetMainViewport()->Pos.x;
        g_menuRect.right = static_cast<LONG>(menuEndX);
    }

    // ---- Right-aligned section: status text + window controls ----
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Button dimensions
    const float btnW = ImGui::GetFrameHeight() * 1.6f;
    const float btnH = g_titleBarHeight;
    const float controlsWidth = btnW * 3.0f; // min + max + close

    // Status text
    if (statusText && statusText[0])
    {
        const float textW = ImGui::CalcTextSize(statusText).x;
        const float availX = vp->WorkSize.x - controlsWidth - 20.0f;
        if (availX > textW + 20.0f)
        {
            ImGui::SameLine(availX - textW);
            ImGui::TextDisabled("%s", statusText);
        }
    }

    // Position the control buttons at the far right
    const float startX = vp->WorkSize.x - controlsWidth;
    ImGui::SameLine(startX);

    // Record the control-button rect for hit-testing (in client coords).
    // Use the actual ImGui cursor position so the rect matches exactly.
    {
        const float btnLeft = ImGui::GetCursorScreenPos().x - vp->Pos.x;
        g_controlButtonsRect.left   = static_cast<LONG>(btnLeft);
        g_controlButtonsRect.top    = 0;
        g_controlButtonsRect.right  = static_cast<LONG>(btnLeft + controlsWidth);
        g_controlButtonsRect.bottom = static_cast<LONG>(g_titleBarHeight);
    }

    // Minimise / Maximise-Restore / Close  — flat, frameless buttons
    // Override FrameBorderSize so the active theme's border doesn't leak in.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.25f));

    // Switch to the dedicated icon font (larger than the UI text font)
    // so the caption-button glyphs match native Windows / UE5 proportions.
    if (g_iconFont)
        ImGui::PushFont(g_iconFont);

    // Segoe MDL2 Assets glyph labels (UTF-8 encoded):
    //   U+E921 = ChromeMinimize   U+E922 = ChromeMaximize
    //   U+E923 = ChromeRestore    U+E8BB = ChromeClose
    // Fallback to simple ASCII if the icon font wasn't loaded.
    const char* lblMin   = g_hasIconFont ? "\xEE\xA4\xA1##min"   : " - ##min";
    const char* lblMax   = g_hasIconFont ? "\xEE\xA4\xA2##max"   : " [] ##max";
    const char* lblRest  = g_hasIconFont ? "\xEE\xA4\xA3##max"   : " = ##max";
    const char* lblClose = g_hasIconFont ? "\xEE\xA2\xBB##close" : " X ##close";

    // Minimise
    if (ImGui::Button(lblMin, ImVec2(btnW, btnH)))
    {
        if (g_hwnd) ShowWindow(g_hwnd, SW_MINIMIZE);
    }

    // Maximise / Restore
    ImGui::SameLine();
    const bool isMaximised = g_hwnd && IsZoomed(g_hwnd);
    if (ImGui::Button(isMaximised ? lblRest : lblMax, ImVec2(btnW, btnH)))
    {
        if (g_hwnd) ShowWindow(g_hwnd, isMaximised ? SW_RESTORE : SW_MAXIMIZE);
    }

    // Close — red hover
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.75f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button(lblClose, ImVec2(btnW, btnH)))
    {
        if (window) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    ImGui::PopStyleColor(2); // close-specific colours

    if (g_iconFont)
        ImGui::PopFont();

    ImGui::PopStyleColor(3); // button base colours
    ImGui::PopStyleVar(3);   // FrameRounding, FrameBorderSize, ItemSpacing

    ImGui::EndMenuBar();
    ImGui::End();

#else
    // Non-Windows fallback: standard main menu bar
    if (statusText && statusText[0])
    {
        float textWidth = ImGui::CalcTextSize(statusText).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10.0f);
        ImGui::TextDisabled("%s", statusText);
    }
    ImGui::EndMainMenuBar();
#endif
}

float CustomTitleBar::height() noexcept
{
    return g_titleBarHeight;
}

void CustomTitleBar::mergeIconFont(float pixelSize)
{
#ifdef _WIN32
    // Segoe MDL2 Assets ships with Windows 10/11 and contains the standard
    // Chrome caption-button icons used by Explorer, VS, etc.
    const char* fontPath = "C:\\Windows\\Fonts\\segmdl2.ttf";
    if (!std::filesystem::exists(fontPath))
    {
        g_iconFont    = nullptr;
        g_hasIconFont = false;
        return;
    }

    // Only rasterise the four glyphs we actually use.
    static const ImWchar ranges[] = { 0xE8BB, 0xE8BB, 0xE921, 0xE923, 0 };

    ImFontConfig cfg;
    cfg.PixelSnapH = true;
    // Loaded as a separate font so PushFont/PopFont controls the size
    // independently of the UI text.  0.55x gives proportions matching
    // the native Windows / UE5 caption buttons.
    const float iconSize = pixelSize * 0.55f;
    // Segoe MDL2's ascender is tall relative to the glyph's visual centre,
    // so nudge the icons up to sit in the middle of the button.
    cfg.GlyphOffset.y = -std::round(iconSize * 0.4f);

    g_iconFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        fontPath, iconSize, &cfg, ranges);
    g_hasIconFont = (g_iconFont != nullptr);
#else
    (void)pixelSize;
    g_iconFont    = nullptr;
    g_hasIconFont = false;
#endif
}
