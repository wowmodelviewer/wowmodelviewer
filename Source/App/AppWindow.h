#pragma once

// ---- AppWindow (platform-abstraction layer) --------------------------------
// Owns the GLFW window lifecycle: creation, OpenGL loader (glad), window icon,
// DPI scale, event polling, buffer swap, and destruction.
//
// Follows the Gregory "Engine Support" pattern: a thin wrapper over the
// platform windowing API that the rest of the application programs against.

struct GLFWwindow;

class AppWindow
{
public:
    AppWindow() = default;
    ~AppWindow();

    AppWindow(const AppWindow&)            = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    /// Create the GLFW window and load OpenGL via glad.
    /// @return true on success, false on fatal error.
    bool init(int width, int height, const char* title);

    /// Load and set the window icon from a PNG file.
    void setIcon(const char* fallbackPath);

    /// Query the monitor content-scale (DPI).
    float queryDpiScale() const;

    /// Poll platform events (must be called once per frame).
    void pollEvents();

    /// Swap the front/back framebuffers.
    void swapBuffers();

    /// Returns true when the user (or code) has requested the window to close.
    [[nodiscard]] bool shouldClose() const;

    /// Signal the window to close.
    void requestClose();

    /// Retrieve the framebuffer size in pixels.
    void framebufferSize(int& w, int& h) const;

    /// Raw GLFW handle — needed by ImGui backends and CustomTitleBar.
    [[nodiscard]] GLFWwindow* handle() const noexcept { return m_window; }

private:
    GLFWwindow* m_window = nullptr;

    static void errorCallback(int error, const char* description);
};
