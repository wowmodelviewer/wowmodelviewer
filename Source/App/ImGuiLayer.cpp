#include "ImGuiLayer.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Logger.h"
#include "AppSettings.h"
#include "AppState.h"
#include "AppWindow.h"
#include "CustomTitleBar.h"
#include "ThemeManager.h"

// ---- init / shutdown ------------------------------------------------------

bool ImGuiLayer::init(GLFWwindow* window, float dpiScale)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = AppSettings::imguiIniPath;

    ThemeManager::apply(ThemeManager::currentTheme(), window);

    if (dpiScale > 1.0f)
        ImGui::GetStyle().ScaleAllSizes(dpiScale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void ImGuiLayer::shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

// ---- Font discovery -------------------------------------------------------

void ImGuiLayer::discoverFonts(std::vector<FontEntry>& fonts, int& selectedFont)
{
    namespace fs = std::filesystem;

    std::vector<fs::path> searchDirs;
#ifdef _WIN32
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        searchDirs.push_back(fs::path(exePath).parent_path() / "fonts");
    }
#endif
#ifdef WMV_FONTS_PATH
    searchDirs.push_back(fs::path(WMV_FONTS_PATH));
#endif
    searchDirs.push_back(fs::current_path() / "fonts");

    std::set<std::string> seen;
    for (const auto& dir : searchDirs)
    {
        if (!fs::is_directory(dir))
            continue;
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".ttf" && ext != ".otf")
                continue;
            std::string stemName = entry.path().stem().string();
            std::string stemLower = stemName;
            std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);
            if (seen.count(stemLower))
                continue;
            seen.insert(stemLower);
            std::string absPath = fs::canonical(entry.path()).string();
            fonts.push_back({ stemName, absPath });
        }
    }

    std::sort(fonts.begin(), fonts.end(),
        [](const FontEntry& a, const FontEntry& b) { return a.name < b.name; });

    if (selectedFont <= 0)
    {
        const char* preferred[] = { "roboto-regular", "arialn" };
        for (const char* target : preferred)
        {
            bool found = false;
            for (int i = 0; i < static_cast<int>(fonts.size()); ++i)
            {
                std::string lower = fonts[i].name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == target)
                {
                    selectedFont = i;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
    }
}

// ---- Font atlas -----------------------------------------------------------

void ImGuiLayer::buildFontAtlas(const std::vector<FontEntry>& fonts,
                                int selectedFont,
                                float fontSize,
                                float dpiScale)
{
    ImGuiIO& io = ImGui::GetIO();
    const float pixelSize = fontSize * dpiScale;
    bool loaded = false;

    if (selectedFont >= 0 && selectedFont < static_cast<int>(fonts.size()))
    {
        const auto& fe = fonts[selectedFont];
        if (std::filesystem::exists(fe.path))
        {
            io.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
            loaded = true;
            LOG_INFO << "Loaded font:" << fe.name << "at" << pixelSize << "px";
        }
    }
    if (!loaded)
        io.Fonts->AddFontDefault();

    CustomTitleBar::mergeIconFont(pixelSize);
    io.FontGlobalScale = 1.0f;
}

void ImGuiLayer::rebuildFontAtlasIfDirty(bool& fontsDirty,
                                          const std::vector<FontEntry>& fonts,
                                          int selectedFont,
                                          float fontSize,
                                          float dpiScale)
{
    if (!fontsDirty)
        return;

    fontsDirty = false;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float pixelSize = fontSize * dpiScale;
    bool loaded = false;

    if (selectedFont >= 0 && selectedFont < static_cast<int>(fonts.size()))
    {
        const auto& fe = fonts[selectedFont];
        if (std::filesystem::exists(fe.path))
        {
            io.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
            loaded = true;
        }
    }
    if (!loaded)
        io.Fonts->AddFontDefault();

    CustomTitleBar::mergeIconFont(pixelSize);
    io.FontGlobalScale = 1.0f;
    io.Fonts->Build();
}

// ---- Per-frame helpers ----------------------------------------------------

void ImGuiLayer::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame(const AppWindow& window)
{
    ImGui::Render();

    int w, h;
    window.framebufferSize(w, h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
