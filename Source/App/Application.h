#pragma once

// ---- Application (top-level orchestrator) ----------------------------------
// Owns all subsystems (AppWindow, ImGuiLayer, InputManager) and the
// aggregate AppState.  Provides an explicit init / run / shutdown lifecycle
// following the Gregory "Game Loop" pattern.

#include "AppState.h"
#include "AppWindow.h"
#include "ImGuiLayer.h"
#include "InputManager.h"

class Application
{
public:
    /// Initialise all subsystems and enter the main loop.
    /// @return Process exit code (0 on success).
    int run();

    ~Application();

private:
    // ---- Owned subsystems (explicit lifecycle order) ----
    AppWindow    m_window;
    ImGuiLayer   m_imguiLayer;
    InputManager m_inputManager;
    AppState     m_state;

    // ---- Frame-local UI flags ----
    bool m_showDemoWindow = false;
    bool m_firstFrame     = true;
    bool m_resetLayout    = false;

    // ---- Lifecycle phases ----
    bool init();
    void mainLoop();
    void shutdown();

    // ---- Per-frame helpers ----
    void initEngine();
    void initGL();
    void tickScene();
    void handleViewportInput();

    // ---- UI drawing (called from mainLoop) ----
    void drawTitleBarAndMenus();
    void drawDockspace();
    void drawPanels();
    void drawDialogs();
};
