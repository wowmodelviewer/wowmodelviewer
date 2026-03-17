// ============================================================================
// WoW Model Viewer — ImGui / GLFW entry point (Phase 1 migration)
//
// Replaces the wxWidgets WinMain ? wxEntry flow from main.cpp / app.cpp.
// Initialises engine systems (GlobalSettings, Logger, video), creates an
// offscreen FBO for the 3-D viewport, renders the scene into that FBO, and
// displays it as an ImGui::Image() inside a dockable "3D Viewport" panel.
// OrbitCamera input is wired to ImGui's mouse/keyboard state.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>
#include <chrono>

// Engine (no wxWidgets dependencies)
#include "GlobalSettings.h"
#include "logger/Logger.h"
#include "logger/LogOutputFile.h"
#include "logger/LogOutputConsole.h"
#include "video.h"
#include "globalvars.h"
#include "Attachment.h"
#include "WoWModel.h"
#include "OrbitCamera.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---- FBO wrapper ----------------------------------------------------------
struct ViewportFBO
{
    GLuint fbo       = 0;
    GLuint colorTex  = 0;
    GLuint depthRbo  = 0;
    int    width     = 0;
    int    height    = 0;

    void create(int w, int h)
    {
        width  = w;
        height = h;
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTex);
        glGenRenderbuffers(1, &depthRbo);

        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resize(int w, int h)
    {
        if (w == width && h == height)
            return;
        destroy();
        if (w > 0 && h > 0)
            create(w, h);
    }

    void bind()   const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    void unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0);   }

    void destroy()
    {
        if (fbo)       { glDeleteFramebuffers(1, &fbo);       fbo       = 0; }
        if (colorTex)  { glDeleteTextures(1, &colorTex);      colorTex  = 0; }
        if (depthRbo)  { glDeleteRenderbuffers(1, &depthRbo); depthRbo  = 0; }
        width = height = 0;
    }
};

// ---- Globals --------------------------------------------------------------
static OrbitCamera  g_camera;
static Attachment*  g_root       = nullptr;
static ViewportFBO  g_fbo;
static bool         g_drawGrid   = true;
static glm::vec3    g_bgColor(71.0f / 255.0f, 95.0f / 255.0f, 121.0f / 255.0f);

// Timing for animation tick
static float        g_animTime   = 0.0f;
static std::chrono::steady_clock::time_point g_lastTick;

// ---- Default lighting (replaces LightControl / wxWindow) ------------------
static void setupDefaultLighting()
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // A simple directional light from upper-right-front
    GLfloat pos[]     = { -1.0f, 1.0f, -1.0f, 0.0f };
    GLfloat diffuse[] = {  1.0f, 1.0f,  1.0f, 1.0f };
    GLfloat ambient[] = {  0.35f, 0.35f, 0.35f, 1.0f };
    GLfloat specular[]= {  0.0f, 0.0f,  0.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    GLfloat modelAmb[] = { 0.35f, 0.35f, 0.35f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, modelAmb);
}

// ---- Grid (ported from ModelCanvas::RenderGrid) ---------------------------
static void renderGrid()
{
    int count = 0;
    const GLfloat white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const GLfloat black[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    glBegin(GL_QUADS);
    for (int i = -20; i <= 20; ++i)
    {
        for (int j = -20; j <= 20; ++j)
        {
            if ((count % 2) == 0)
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
                glColor3f(1.0f, 1.0f, 1.0f);
            }
            else
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
                glColor3f(0.2f, 0.2f, 0.2f);
            }

            glNormal3f(0, 0, 1);
            glVertex3f(static_cast<float>(j),     static_cast<float>(i),     0.0f);
            glVertex3f(static_cast<float>(j),     static_cast<float>(i + 1), 0.0f);
            glVertex3f(static_cast<float>(j + 1), static_cast<float>(i + 1), 0.0f);
            glVertex3f(static_cast<float>(j + 1), static_cast<float>(i),     0.0f);
            count++;
        }
    }
    glEnd();

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

// ---- RenderObjects (ported from ModelCanvas::RenderObjects) ---------------
static void renderObjects()
{
    if (!g_root)
        return;

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    g_root->draw();

    // Particles: rendered after opaque geometry with blending
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    g_root->drawParticles();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// ---- Render scene to FBO --------------------------------------------------
static void renderSceneToFBO(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    g_fbo.resize(w, h);
    g_fbo.bind();

    glViewport(0, 0, w, h);
    glClearColor(g_bgColor.x, g_bgColor.y, g_bgColor.z, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    // Projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glm::mat4 proj = glm::perspective(video.fov,
                                       static_cast<float>(w) / static_cast<float>(h),
                                       0.1f, 1280.0f * 5.0f);
    glMultMatrixf(glm::value_ptr(proj));

    // View
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glm::mat4 view = g_camera.getViewMatrix();
    glMultMatrixf(glm::value_ptr(view));

    // Lighting
    setupDefaultLighting();

    // Grid
    if (g_drawGrid)
        renderGrid();

    // Model
    glEnable(GL_NORMALIZE);
    renderObjects();
    glDisable(GL_NORMALIZE);

    g_fbo.unbind();
}

// ---- Handle viewport input (ported from ModelCanvas::OnMouse) -------------
static void handleViewportInput()
{
    const ImGuiIO& io = ImGui::GetIO();

    float mul = 1.0f;
    if (io.KeyShift)
        mul /= 10.0f;

    const float MOUSE_SENSITIVITY = 0.25f;

    // Mouse wheel ? zoom
    if (io.MouseWheel != 0.0f)
    {
        const float zoom = -io.MouseWheel * 0.5f * mul;
        g_camera.setRadius(g_camera.radius() + zoom);
    }

    // Left drag ? orbit (yaw / pitch)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        g_camera.setYawAndPitch(g_camera.yaw() + (-dx), g_camera.pitch() + (-dy));
    }

    // Right drag ? pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul * 0.025f;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul * 0.025f;
        const auto  look  = g_camera.lookAt();
        const auto  right = g_camera.right();
        g_camera.setLookAt(glm::vec3(look.x + right.x * -dx,
                                      look.y + right.y * -dx,
                                      look.z + dy));
    }

    // Middle drag ? zoom (alternative)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        g_camera.setRadius(g_camera.radius() + dy / 10.0f);
    }
}

// ---- Animation tick -------------------------------------------------------
static void tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTick).count();
    g_lastTick = now;

    // Clamp to avoid huge jumps
    if (dt > 0.1f) dt = 0.1f;

    g_animTime += dt;

    if (g_root)
        g_root->tick(dt);
}

// ---- Engine initialization ------------------------------------------------
static void initEngine()
{
    // GlobalSettings singleton
    GLOBALSETTINGS.bShowParticle = true;
    GLOBALSETTINGS.bZeroParticle = true;

    // Create userSettings directory for logs / config
#ifdef _WIN32
    CreateDirectoryA("userSettings", nullptr);
#endif

    // Logger
    LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log_imgui.txt"));
    LOGGER.addChild(new WMVLog::LogOutputConsole());

    LOG_INFO << "==============================================";
    LOG_INFO << "Starting:" << GLOBALSETTINGS.appName()
             << GLOBALSETTINGS.appVersion()
             << GLOBALSETTINGS.buildName();
    LOG_INFO << "==============================================";
}

static void initGL()
{
    video.render = true;
    // video.Init() calls gladLoaderLoadGL() internally — safe after GLFW context
    video.InitGL();

    LOG_INFO << "OpenGL initialisation complete.";
}

// ---- GLFW error callback --------------------------------------------------
static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ---- Entry point ----------------------------------------------------------
int main(int /*argc*/, char* /*argv*/[])
{
    // ---- GLFW + window ----
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "WoW Model Viewer (ImGui)", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---- glad ----
    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialise OpenGL loader (glad)\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // ---- Engine init ----
    initEngine();
    initGL();

    // Create root attachment (scene graph root — no model yet)
    g_root = new Attachment(nullptr, nullptr, -1, -1);

    // ---- Dear ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = false;
    g_lastTick = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Animation tick
        tickScene();

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // ===== 3D Viewport panel =====
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport"))
        {
            // Determine available size for the viewport image
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);

            if (vpW > 0 && vpH > 0)
            {
                // Render scene to offscreen FBO
                renderSceneToFBO(vpW, vpH);

                // Display FBO colour texture (UV-flipped: OpenGL is bottom-up)
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(g_fbo.colorTex)),
                             panelSize,
                             ImVec2(0, 1), ImVec2(1, 0));

                // Handle orbit camera input when viewport is hovered
                if (ImGui::IsItemHovered())
                    handleViewportInput();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // ===== File Browser placeholder =====
        if (ImGui::Begin("File Browser"))
        {
            ImGui::Text("FileControl tree will go here.");
            ImGui::Text("(Game loading not yet ported — Phase 2)");
        }
        ImGui::End();

        // ===== Animation placeholder =====
        if (ImGui::Begin("Animation"))
        {
            ImGui::Text("AnimControl will go here.");
        }
        ImGui::End();

        // ===== Settings panel =====
        if (ImGui::Begin("Settings"))
        {
            ImGui::Checkbox("Draw Grid", &g_drawGrid);
            ImGui::ColorEdit3("Background", &g_bgColor.x);
            if (ImGui::Button("Reset Camera"))
                g_camera.reset();
            ImGui::Separator();
            ImGui::Checkbox("ImGui Demo Window", &show_demo_window);
            ImGui::Separator();
            ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                        g_camera.yaw(), g_camera.pitch(), g_camera.radius());
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));
        }
        ImGui::End();

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // ---- Render ImGui over the default framebuffer ----
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ---- Cleanup ----
    if (g_root)
    {
        g_root->delChildren();
        delete g_root;
        g_root = nullptr;
    }

    g_fbo.destroy();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer (ImGui) shutdown complete.";
    return 0;
}
