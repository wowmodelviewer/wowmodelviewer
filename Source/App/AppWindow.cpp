#include "AppWindow.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "stb_image.h"

// ---- Static error callback ------------------------------------------------

void AppWindow::errorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ---- Lifecycle ------------------------------------------------------------

AppWindow::~AppWindow()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

bool AppWindow::init(int width, int height, const char* title)
{
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window)
    {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    // Load OpenGL function pointers via glad
    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialise OpenGL loader (glad)\n");
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
        return false;
    }

    return true;
}

// ---- Icon -----------------------------------------------------------------

void AppWindow::setIcon(const char* fallbackPath)
{
    int iw = 0, ih = 0, ic = 0;
    unsigned char* px = nullptr;

#ifdef _WIN32
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto iconPath = std::filesystem::path(exePath).parent_path() / "wmv_16.png";
        px = stbi_load(iconPath.string().c_str(), &iw, &ih, &ic, 4);
    }
#endif

    if (!px && fallbackPath)
        px = stbi_load(fallbackPath, &iw, &ih, &ic, 4);

    if (px)
    {
        GLFWimage img{ iw, ih, px };
        glfwSetWindowIcon(m_window, 1, &img);
        stbi_image_free(px);
    }
}

// ---- DPI ------------------------------------------------------------------

float AppWindow::queryDpiScale() const
{
    float xscale = 1.0f, yscale = 1.0f;
    if (m_window)
        glfwGetWindowContentScale(m_window, &xscale, &yscale);
    return (xscale > yscale) ? xscale : yscale;
}

// ---- Per-frame helpers ----------------------------------------------------

void AppWindow::pollEvents()
{
    glfwPollEvents();
}

void AppWindow::swapBuffers()
{
    glfwSwapBuffers(m_window);
}

bool AppWindow::shouldClose() const
{
    return m_window && glfwWindowShouldClose(m_window);
}

void AppWindow::requestClose()
{
    if (m_window)
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void AppWindow::framebufferSize(int& w, int& h) const
{
    if (m_window)
        glfwGetFramebufferSize(m_window, &w, &h);
    else
    {
        w = 0;
        h = 0;
    }
}
