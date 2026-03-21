#pragma once

// ---- ImGuiLayer (GUI subsystem lifecycle) ----------------------------------
// Manages the Dear ImGui context, GLFW/OpenGL3 backends, DPI scaling,
// font discovery, and font-atlas building/rebuilding.
//
// Following the Gregory "Engine Support" pattern this class encapsulates
// the entire ImGui lifecycle with explicit init / shutdown phases.

#include <vector>
#include <string>

struct GLFWwindow;
struct FontEntry;
class AppWindow;

class ImGuiLayer
{
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() = default;

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    /// Create the ImGui context, configure flags, apply DPI scaling,
    /// and initialise the GLFW + OpenGL3 backends.
    bool init(GLFWwindow* window, float dpiScale);

    /// Scan font directories for .ttf / .otf files and populate @p fonts.
    /// If @p selectedFont is <= 0 a sensible default is chosen.
    void discoverFonts(std::vector<FontEntry>& fonts, int& selectedFont);

    /// Build (or rebuild) the font atlas from the currently selected font.
    void buildFontAtlas(const std::vector<FontEntry>& fonts,
                        int selectedFont,
                        float fontSize,
                        float dpiScale);

    /// If the fontsDirty flag is set, rebuild the atlas and clear the flag.
    void rebuildFontAtlasIfDirty(bool& fontsDirty,
                                 const std::vector<FontEntry>& fonts,
                                 int selectedFont,
                                 float fontSize,
                                 float dpiScale);

    /// Start a new ImGui frame (backend NewFrame + ImGui::NewFrame).
    void beginFrame();

    /// Finalise the ImGui frame: Render(), clear the default framebuffer,
    /// and draw the ImGui render data.
    void endFrame(const AppWindow& window);

    /// Shut down ImGui backends and destroy the context.
    void shutdown();
};
