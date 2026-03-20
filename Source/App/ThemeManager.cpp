#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include "ThemeManager.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace ThemeManager
{

// ---- Module state ---------------------------------------------------------
static int s_currentTheme = static_cast<int>(UE4);

static const char* s_themeNames[] = {
    "Default (ImGui Dark)",
    "Unreal Engine 4",
    "Unreal Engine 5",
    "Visual Studio 2026",
    "Light",
    "Warcraft"
};

// ---- Title-bar colour (Windows DWM) ---------------------------------------
static void updateTitleBarColor(Theme theme, [[maybe_unused]] GLFWwindow* window)
{
#ifdef _WIN32
    if (!window)
        return;

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd)
        return;

    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_VAL = 20;
    constexpr DWORD DWMWA_CAPTION_COLOR_VAL = 35;

    BOOL useDarkMode = (theme != Light) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VAL, &useDarkMode, sizeof(useDarkMode));

    COLORREF captionColor = RGB(36, 36, 36);
    switch (theme)
    {
    case Default:    captionColor = RGB(50, 50, 55);    break;
    case UE4:        captionColor = RGB(25, 25, 25);    break;
    case UE5:        captionColor = RGB(30, 32, 36);    break;
    case VS2026:     captionColor = RGB(30, 30, 30);    break;
    case Light:      captionColor = RGB(239, 239, 239); break;
    case Warcraft:   captionColor = RGB(18, 20, 28);    break;
    default: break;
    }
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_VAL, &captionColor, sizeof(captionColor));
#else
    (void)theme;
    (void)window;
#endif
}

// ---- Theme application ----------------------------------------------------
void apply(Theme theme, GLFWwindow* window)
{
    ImGuiStyle& style = ImGui::GetStyle();

    auto setCommonStyle = [&](float rounding, float frameBorder) {
        style.WindowRounding    = rounding;
        style.ChildRounding     = rounding;
        style.FrameRounding     = rounding;
        style.PopupRounding     = rounding;
        style.ScrollbarRounding = rounding;
        style.GrabRounding      = rounding;
        style.TabRounding       = rounding;
        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = frameBorder;
        style.PopupBorderSize   = 1.0f;
        style.FramePadding      = ImVec2(5.0f, 3.0f);
        style.ItemSpacing       = ImVec2(5.0f, 4.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
        style.IndentSpacing     = 16.0f;
        style.ScrollbarSize     = 13.0f;
        style.GrabMinSize       = 8.0f;
        style.SeparatorTextBorderSize = 2.0f;
    };

    ImVec4* c = style.Colors;

    switch (theme)
    {
    case Default:
    {
        ImGui::StyleColorsDark();
        setCommonStyle(4.0f, 0.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;
        break;
    }
    case UE4:
    {
        ImGui::StyleColorsDark();
        setCommonStyle(1.0f, 1.0f);
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;

        c[ImGuiCol_WindowBg]             = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);
        c[ImGuiCol_ChildBg]              = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);
        c[ImGuiCol_PopupBg]              = ImVec4(0.118f, 0.118f, 0.118f, 0.98f);
        c[ImGuiCol_MenuBarBg]            = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_Border]               = ImVec4(0.082f, 0.082f, 0.082f, 1.00f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TitleBg]              = ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
        c[ImGuiCol_TitleBgActive]        = ImVec4(0.137f, 0.137f, 0.137f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.098f, 0.098f, 0.098f, 0.75f);
        c[ImGuiCol_Text]                 = ImVec4(0.784f, 0.784f, 0.784f, 1.00f);
        c[ImGuiCol_TextDisabled]         = ImVec4(0.392f, 0.392f, 0.392f, 1.00f);
        c[ImGuiCol_TextSelectedBg]       = ImVec4(0.051f, 0.431f, 0.992f, 0.40f);
        c[ImGuiCol_FrameBg]              = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.137f, 0.137f, 0.137f, 1.00f);
        c[ImGuiCol_FrameBgActive]        = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_Button]               = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_ButtonHovered]        = ImVec4(0.275f, 0.275f, 0.275f, 1.00f);
        c[ImGuiCol_ButtonActive]         = ImVec4(0.816f, 0.533f, 0.000f, 1.00f);
        c[ImGuiCol_Header]               = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(0.051f, 0.431f, 0.992f, 0.50f);
        c[ImGuiCol_HeaderActive]         = ImVec4(0.051f, 0.431f, 0.992f, 0.70f);
        c[ImGuiCol_Tab]                  = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_TabHovered]           = ImVec4(0.235f, 0.235f, 0.235f, 1.00f);
        c[ImGuiCol_TabSelected]          = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_TabDimmed]            = ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0.102f, 0.102f, 0.102f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.275f, 0.275f, 0.275f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.353f, 0.353f, 0.353f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.431f, 0.431f, 0.431f, 1.00f);
        c[ImGuiCol_SliderGrab]           = ImVec4(0.353f, 0.353f, 0.353f, 1.00f);
        c[ImGuiCol_SliderGrabActive]     = ImVec4(0.816f, 0.533f, 0.000f, 1.00f);
        c[ImGuiCol_CheckMark]            = ImVec4(0.816f, 0.533f, 0.000f, 1.00f);
        c[ImGuiCol_Separator]            = ImVec4(0.082f, 0.082f, 0.082f, 1.00f);
        c[ImGuiCol_SeparatorHovered]     = ImVec4(0.816f, 0.533f, 0.000f, 0.78f);
        c[ImGuiCol_SeparatorActive]      = ImVec4(0.816f, 0.533f, 0.000f, 1.00f);
        c[ImGuiCol_ResizeGrip]           = ImVec4(0.275f, 0.275f, 0.275f, 0.25f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.816f, 0.533f, 0.000f, 0.67f);
        c[ImGuiCol_ResizeGripActive]     = ImVec4(0.816f, 0.533f, 0.000f, 0.95f);
        c[ImGuiCol_DockingPreview]       = ImVec4(0.816f, 0.533f, 0.000f, 0.70f);
        c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
        c[ImGuiCol_NavCursor]            = ImVec4(0.816f, 0.533f, 0.000f, 1.00f);
        c[ImGuiCol_NavWindowingHighlight]= ImVec4(0.816f, 0.533f, 0.000f, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.118f, 0.118f, 0.118f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
        c[ImGuiCol_TableHeaderBg]        = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_TableBorderStrong]    = ImVec4(0.082f, 0.082f, 0.082f, 1.00f);
        c[ImGuiCol_TableBorderLight]     = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
        break;
    }
    case UE5:
    {
        ImGui::StyleColorsDark();
        setCommonStyle(2.0f, 1.0f);
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;

        c[ImGuiCol_WindowBg]             = ImVec4(0.110f, 0.114f, 0.125f, 1.00f);
        c[ImGuiCol_ChildBg]              = ImVec4(0.110f, 0.114f, 0.125f, 1.00f);
        c[ImGuiCol_PopupBg]              = ImVec4(0.094f, 0.098f, 0.110f, 0.98f);
        c[ImGuiCol_MenuBarBg]            = ImVec4(0.133f, 0.137f, 0.153f, 1.00f);
        c[ImGuiCol_Border]               = ImVec4(0.059f, 0.063f, 0.075f, 1.00f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TitleBg]              = ImVec4(0.071f, 0.075f, 0.086f, 1.00f);
        c[ImGuiCol_TitleBgActive]        = ImVec4(0.094f, 0.098f, 0.114f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.071f, 0.075f, 0.086f, 0.75f);
        c[ImGuiCol_Text]                 = ImVec4(0.824f, 0.835f, 0.859f, 1.00f);
        c[ImGuiCol_TextDisabled]         = ImVec4(0.400f, 0.412f, 0.435f, 1.00f);
        c[ImGuiCol_TextSelectedBg]       = ImVec4(0.125f, 0.455f, 0.651f, 0.50f);
        c[ImGuiCol_FrameBg]              = ImVec4(0.075f, 0.078f, 0.090f, 1.00f);
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.110f, 0.114f, 0.130f, 1.00f);
        c[ImGuiCol_FrameBgActive]        = ImVec4(0.145f, 0.149f, 0.169f, 1.00f);
        c[ImGuiCol_Button]               = ImVec4(0.157f, 0.161f, 0.180f, 1.00f);
        c[ImGuiCol_ButtonHovered]        = ImVec4(0.212f, 0.220f, 0.243f, 1.00f);
        c[ImGuiCol_ButtonActive]         = ImVec4(0.125f, 0.455f, 0.651f, 1.00f);
        c[ImGuiCol_Header]               = ImVec4(0.145f, 0.149f, 0.169f, 1.00f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(0.125f, 0.455f, 0.651f, 0.50f);
        c[ImGuiCol_HeaderActive]         = ImVec4(0.125f, 0.455f, 0.651f, 0.70f);
        c[ImGuiCol_Tab]                  = ImVec4(0.086f, 0.090f, 0.102f, 1.00f);
        c[ImGuiCol_TabHovered]           = ImVec4(0.176f, 0.184f, 0.208f, 1.00f);
        c[ImGuiCol_TabSelected]          = ImVec4(0.133f, 0.137f, 0.157f, 1.00f);
        c[ImGuiCol_TabDimmed]            = ImVec4(0.071f, 0.075f, 0.086f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.098f, 0.102f, 0.118f, 1.00f);
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0.075f, 0.078f, 0.090f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.220f, 0.227f, 0.251f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.290f, 0.298f, 0.325f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.361f, 0.369f, 0.400f, 1.00f);
        c[ImGuiCol_SliderGrab]           = ImVec4(0.290f, 0.298f, 0.325f, 1.00f);
        c[ImGuiCol_SliderGrabActive]     = ImVec4(0.125f, 0.455f, 0.651f, 1.00f);
        c[ImGuiCol_CheckMark]            = ImVec4(0.125f, 0.455f, 0.651f, 1.00f);
        c[ImGuiCol_Separator]            = ImVec4(0.059f, 0.063f, 0.075f, 1.00f);
        c[ImGuiCol_SeparatorHovered]     = ImVec4(0.125f, 0.455f, 0.651f, 0.78f);
        c[ImGuiCol_SeparatorActive]      = ImVec4(0.125f, 0.455f, 0.651f, 1.00f);
        c[ImGuiCol_ResizeGrip]           = ImVec4(0.220f, 0.227f, 0.251f, 0.25f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.125f, 0.455f, 0.651f, 0.67f);
        c[ImGuiCol_ResizeGripActive]     = ImVec4(0.125f, 0.455f, 0.651f, 0.95f);
        c[ImGuiCol_DockingPreview]       = ImVec4(0.125f, 0.455f, 0.651f, 0.70f);
        c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.071f, 0.075f, 0.086f, 1.00f);
        c[ImGuiCol_NavCursor]            = ImVec4(0.125f, 0.455f, 0.651f, 1.00f);
        c[ImGuiCol_NavWindowingHighlight]= ImVec4(0.125f, 0.455f, 0.651f, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.094f, 0.098f, 0.110f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
        c[ImGuiCol_TableHeaderBg]        = ImVec4(0.133f, 0.137f, 0.157f, 1.00f);
        c[ImGuiCol_TableBorderStrong]    = ImVec4(0.059f, 0.063f, 0.075f, 1.00f);
        c[ImGuiCol_TableBorderLight]     = ImVec4(0.094f, 0.098f, 0.114f, 1.00f);
        c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
        break;
    }
    case VS2026:
    {
        ImGui::StyleColorsDark();
        setCommonStyle(0.0f, 1.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;

        c[ImGuiCol_WindowBg]             = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_ChildBg]              = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_PopupBg]              = ImVec4(0.118f, 0.118f, 0.118f, 0.98f);
        c[ImGuiCol_MenuBarBg]            = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_Border]               = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TitleBg]              = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_TitleBgActive]        = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.173f, 0.173f, 0.173f, 0.75f);
        c[ImGuiCol_Text]                 = ImVec4(0.863f, 0.863f, 0.863f, 1.00f);
        c[ImGuiCol_TextDisabled]         = ImVec4(0.400f, 0.400f, 0.400f, 1.00f);
        c[ImGuiCol_TextSelectedBg]       = ImVec4(0.000f, 0.478f, 0.800f, 0.45f);
        c[ImGuiCol_FrameBg]              = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.239f, 0.239f, 0.239f, 1.00f);
        c[ImGuiCol_FrameBgActive]        = ImVec4(0.278f, 0.278f, 0.278f, 1.00f);
        c[ImGuiCol_Button]               = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_ButtonHovered]        = ImVec4(0.239f, 0.239f, 0.239f, 1.00f);
        c[ImGuiCol_ButtonActive]         = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_Header]               = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(0.000f, 0.478f, 0.800f, 0.50f);
        c[ImGuiCol_HeaderActive]         = ImVec4(0.000f, 0.478f, 0.800f, 0.70f);
        c[ImGuiCol_Tab]                  = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_TabHovered]           = ImVec4(0.000f, 0.478f, 0.800f, 0.60f);
        c[ImGuiCol_TabSelected]          = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_TabDimmed]            = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0.149f, 0.149f, 0.149f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.310f, 0.310f, 0.310f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.400f, 0.400f, 0.400f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.502f, 0.502f, 0.502f, 1.00f);
        c[ImGuiCol_SliderGrab]           = ImVec4(0.310f, 0.310f, 0.310f, 1.00f);
        c[ImGuiCol_SliderGrabActive]     = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_CheckMark]            = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_Separator]            = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.478f, 0.800f, 0.78f);
        c[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_ResizeGrip]           = ImVec4(0.310f, 0.310f, 0.310f, 0.25f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.000f, 0.478f, 0.800f, 0.67f);
        c[ImGuiCol_ResizeGripActive]     = ImVec4(0.000f, 0.478f, 0.800f, 0.95f);
        c[ImGuiCol_DockingPreview]       = ImVec4(0.000f, 0.478f, 0.800f, 0.70f);
        c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
        c[ImGuiCol_NavCursor]            = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_NavWindowingHighlight]= ImVec4(0.000f, 0.478f, 0.800f, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.118f, 0.118f, 0.118f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
        c[ImGuiCol_TableHeaderBg]        = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_TableBorderStrong]    = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_TableBorderLight]     = ImVec4(0.149f, 0.149f, 0.149f, 1.00f);
        c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
        break;
    }
    case Light:
    {
        ImGui::StyleColorsLight();
        setCommonStyle(3.0f, 1.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;

        c[ImGuiCol_WindowBg]             = ImVec4(0.937f, 0.937f, 0.937f, 1.00f);
        c[ImGuiCol_ChildBg]              = ImVec4(0.961f, 0.961f, 0.961f, 1.00f);
        c[ImGuiCol_PopupBg]              = ImVec4(0.976f, 0.976f, 0.976f, 0.98f);
        c[ImGuiCol_MenuBarBg]            = ImVec4(0.898f, 0.898f, 0.898f, 1.00f);
        c[ImGuiCol_Border]               = ImVec4(0.757f, 0.757f, 0.757f, 1.00f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TitleBg]              = ImVec4(0.898f, 0.898f, 0.898f, 1.00f);
        c[ImGuiCol_TitleBgActive]        = ImVec4(0.835f, 0.835f, 0.835f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.898f, 0.898f, 0.898f, 0.75f);
        c[ImGuiCol_Text]                 = ImVec4(0.133f, 0.133f, 0.133f, 1.00f);
        c[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.502f, 0.502f, 1.00f);
        c[ImGuiCol_TextSelectedBg]       = ImVec4(0.184f, 0.525f, 0.945f, 0.35f);
        c[ImGuiCol_FrameBg]              = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.906f, 0.933f, 0.976f, 1.00f);
        c[ImGuiCol_FrameBgActive]        = ImVec4(0.835f, 0.882f, 0.953f, 1.00f);
        c[ImGuiCol_Button]               = ImVec4(0.859f, 0.859f, 0.859f, 1.00f);
        c[ImGuiCol_ButtonHovered]        = ImVec4(0.184f, 0.525f, 0.945f, 0.60f);
        c[ImGuiCol_ButtonActive]         = ImVec4(0.184f, 0.525f, 0.945f, 1.00f);
        c[ImGuiCol_Header]               = ImVec4(0.898f, 0.898f, 0.898f, 1.00f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(0.184f, 0.525f, 0.945f, 0.40f);
        c[ImGuiCol_HeaderActive]         = ImVec4(0.184f, 0.525f, 0.945f, 0.60f);
        c[ImGuiCol_Tab]                  = ImVec4(0.898f, 0.898f, 0.898f, 1.00f);
        c[ImGuiCol_TabHovered]           = ImVec4(0.184f, 0.525f, 0.945f, 0.40f);
        c[ImGuiCol_TabSelected]          = ImVec4(0.961f, 0.961f, 0.961f, 1.00f);
        c[ImGuiCol_TabDimmed]            = ImVec4(0.922f, 0.922f, 0.922f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.937f, 0.937f, 0.937f, 1.00f);
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0.937f, 0.937f, 0.937f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.690f, 0.690f, 0.690f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.565f, 0.565f, 0.565f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.439f, 0.439f, 0.439f, 1.00f);
        c[ImGuiCol_SliderGrab]           = ImVec4(0.184f, 0.525f, 0.945f, 0.60f);
        c[ImGuiCol_SliderGrabActive]     = ImVec4(0.184f, 0.525f, 0.945f, 1.00f);
        c[ImGuiCol_CheckMark]            = ImVec4(0.184f, 0.525f, 0.945f, 1.00f);
        c[ImGuiCol_Separator]            = ImVec4(0.757f, 0.757f, 0.757f, 1.00f);
        c[ImGuiCol_SeparatorHovered]     = ImVec4(0.184f, 0.525f, 0.945f, 0.78f);
        c[ImGuiCol_SeparatorActive]      = ImVec4(0.184f, 0.525f, 0.945f, 1.00f);
        c[ImGuiCol_ResizeGrip]           = ImVec4(0.690f, 0.690f, 0.690f, 0.25f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.184f, 0.525f, 0.945f, 0.67f);
        c[ImGuiCol_ResizeGripActive]     = ImVec4(0.184f, 0.525f, 0.945f, 0.95f);
        c[ImGuiCol_DockingPreview]       = ImVec4(0.184f, 0.525f, 0.945f, 0.70f);
        c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.937f, 0.937f, 0.937f, 1.00f);
        c[ImGuiCol_NavCursor]            = ImVec4(0.184f, 0.525f, 0.945f, 1.00f);
        c[ImGuiCol_NavWindowingHighlight]= ImVec4(0.184f, 0.525f, 0.945f, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.800f, 0.800f, 0.800f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.200f, 0.200f, 0.200f, 0.35f);
        c[ImGuiCol_TableHeaderBg]        = ImVec4(0.882f, 0.882f, 0.882f, 1.00f);
        c[ImGuiCol_TableBorderStrong]    = ImVec4(0.757f, 0.757f, 0.757f, 1.00f);
        c[ImGuiCol_TableBorderLight]     = ImVec4(0.835f, 0.835f, 0.835f, 1.00f);
        c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.03f);
        break;
    }
    case Warcraft:
    {
        ImGui::StyleColorsDark();
        setCommonStyle(3.0f, 1.0f);
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;

        const ImVec4 gold       = ImVec4(0.773f, 0.643f, 0.306f, 1.00f);
        const ImVec4 goldHover  = ImVec4(0.847f, 0.718f, 0.384f, 1.00f);
        const ImVec4 goldDim    = ImVec4(0.533f, 0.443f, 0.212f, 1.00f);

        c[ImGuiCol_WindowBg]             = ImVec4(0.102f, 0.114f, 0.149f, 1.00f);
        c[ImGuiCol_ChildBg]              = ImVec4(0.102f, 0.114f, 0.149f, 1.00f);
        c[ImGuiCol_PopupBg]              = ImVec4(0.082f, 0.090f, 0.118f, 0.98f);
        c[ImGuiCol_MenuBarBg]            = ImVec4(0.133f, 0.145f, 0.180f, 1.00f);
        c[ImGuiCol_Border]               = ImVec4(0.286f, 0.239f, 0.133f, 0.60f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TitleBg]              = ImVec4(0.071f, 0.078f, 0.102f, 1.00f);
        c[ImGuiCol_TitleBgActive]        = ImVec4(0.184f, 0.153f, 0.082f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.071f, 0.078f, 0.102f, 0.75f);
        c[ImGuiCol_Text]                 = ImVec4(0.882f, 0.843f, 0.749f, 1.00f);
        c[ImGuiCol_TextDisabled]         = ImVec4(0.498f, 0.459f, 0.380f, 1.00f);
        c[ImGuiCol_TextSelectedBg]       = ImVec4(0.773f, 0.643f, 0.306f, 0.35f);
        c[ImGuiCol_FrameBg]              = ImVec4(0.078f, 0.090f, 0.125f, 1.00f);
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.118f, 0.130f, 0.169f, 1.00f);
        c[ImGuiCol_FrameBgActive]        = ImVec4(0.157f, 0.169f, 0.212f, 1.00f);
        c[ImGuiCol_Button]               = ImVec4(0.157f, 0.169f, 0.216f, 1.00f);
        c[ImGuiCol_ButtonHovered]        = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.70f);
        c[ImGuiCol_ButtonActive]         = ImVec4(gold.x, gold.y, gold.z, 1.00f);
        c[ImGuiCol_Header]               = ImVec4(0.141f, 0.153f, 0.196f, 1.00f);
        c[ImGuiCol_HeaderHovered]        = ImVec4(gold.x, gold.y, gold.z, 0.35f);
        c[ImGuiCol_HeaderActive]         = ImVec4(gold.x, gold.y, gold.z, 0.55f);
        c[ImGuiCol_Tab]                  = ImVec4(0.086f, 0.094f, 0.125f, 1.00f);
        c[ImGuiCol_TabHovered]           = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.50f);
        c[ImGuiCol_TabSelected]          = ImVec4(0.133f, 0.145f, 0.184f, 1.00f);
        c[ImGuiCol_TabDimmed]            = ImVec4(0.071f, 0.078f, 0.102f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.102f, 0.110f, 0.145f, 1.00f);
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0.078f, 0.086f, 0.114f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]        = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.60f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(gold.x, gold.y, gold.z, 0.70f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(goldHover.x, goldHover.y, goldHover.z, 0.90f);
        c[ImGuiCol_SliderGrab]           = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.70f);
        c[ImGuiCol_SliderGrabActive]     = ImVec4(gold.x, gold.y, gold.z, 1.00f);
        c[ImGuiCol_CheckMark]            = ImVec4(gold.x, gold.y, gold.z, 1.00f);
        c[ImGuiCol_Separator]            = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.40f);
        c[ImGuiCol_SeparatorHovered]     = ImVec4(gold.x, gold.y, gold.z, 0.78f);
        c[ImGuiCol_SeparatorActive]      = ImVec4(gold.x, gold.y, gold.z, 1.00f);
        c[ImGuiCol_ResizeGrip]           = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.25f);
        c[ImGuiCol_ResizeGripHovered]    = ImVec4(gold.x, gold.y, gold.z, 0.67f);
        c[ImGuiCol_ResizeGripActive]     = ImVec4(gold.x, gold.y, gold.z, 0.95f);
        c[ImGuiCol_DockingPreview]       = ImVec4(gold.x, gold.y, gold.z, 0.70f);
        c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.071f, 0.078f, 0.102f, 1.00f);
        c[ImGuiCol_NavCursor]            = ImVec4(gold.x, gold.y, gold.z, 1.00f);
        c[ImGuiCol_NavWindowingHighlight]= ImVec4(gold.x, gold.y, gold.z, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.082f, 0.090f, 0.118f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.60f);
        c[ImGuiCol_TableHeaderBg]        = ImVec4(0.133f, 0.145f, 0.184f, 1.00f);
        c[ImGuiCol_TableBorderStrong]    = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.40f);
        c[ImGuiCol_TableBorderLight]     = ImVec4(goldDim.x, goldDim.y, goldDim.z, 0.20f);
        c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.773f, 0.643f, 0.306f, 0.03f);
        break;
    }
    default:
        ImGui::StyleColorsDark();
        break;
    }

    s_currentTheme = static_cast<int>(theme);
    updateTitleBarColor(theme, window);
}

// ---- Accessors ------------------------------------------------------------
Theme currentTheme() noexcept
{
    return static_cast<Theme>(s_currentTheme);
}

int& currentThemeRef() noexcept
{
    return s_currentTheme;
}

const char* themeName(int index) noexcept
{
    if (index < 0 || index >= static_cast<int>(Count))
        return "Unknown";
    return s_themeNames[index];
}

const char* const* themeNames() noexcept
{
    return s_themeNames;
}

} // namespace ThemeManager
