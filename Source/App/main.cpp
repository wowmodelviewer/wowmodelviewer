// ============================================================================
// WoW Model Viewer � ImGui / GLFW entry point
//
// Initialises engine systems (GlobalSettings, Logger, video), creates an
// offscreen FBO for the 3-D viewport, renders the scene into that FBO, and
// displays it as an ImGui::Image() inside a dockable "3D Viewport" panel.
// OrbitCamera input is wired to ImGui's mouse/keyboard state.
//
// Game loading (CascLib + GameDatabase) � reads Config.ini,
// opens the WoW game folder via an ImGui path dialog, initialises the
// CASC storage, loads the listfile, and builds the in-memory database.
//
// File Browser � filterable tree view of CASC files with configurable
// extension filter and text search. Clicking a .m2 file loads it.
//
// Model loading � creates WoWModel from GameFile, sets up character
// equipment slots, and resets the camera to frame the model.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

// Engine (no wxWidgets dependencies)
#include "GlobalSettings.h"
#include "Logger.h"
#include "LogOutputFile.h"
#include "LogOutputConsole.h"
#include "video.h"
#include "Attachment.h"
#include "WoWModel.h"
#include "OrbitCamera.h"
#include "ViewportFBO.h"
#include "ThemeManager.h"
#include "AppSettings.h"
#include "SceneRenderer.h"
#include "FileBrowserPanel.h"
#include "CharacterViewerPanel.h"
#include "AnimationPanel.h"
#include "ViewportOptionsPanel.h"
#include "ExportPanel.h"
#include "ScreenshotPanel.h"
#include "LogPanel.h"
#include "PresetsPanel.h"
#include "MountsPanel.h"
#include "ItemSetsPanel.h"
#include "NpcBrowserPanel.h"
#include "ItemBrowserPanel.h"

// Game loading
#include "Game.h"
#include "WoWFolder.h"
#include "WoWDatabase.h"
#include "database.h"
#include "string_utils.h"

#include "stb_image.h"

// Exporters (OBJ / FBX)
#include "OBJExporter.h"
#include "FBXExporter.h"

// Importers (Armory / Wowhead URL import)
#include "ArmoryImporter.h"
#include "WowheadImporter.h"


#include <format>
#include <map>
#include <set>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Consolidated application state and helper modules
#include "AppState.h"
#include "GameLoader.h"
#include "ModelLoader.h"
#include "PresetManager.h"
#include "URLImportHandler.h"

static AppState app;

// ---- Thin wrappers forwarding to helper modules --------------------------
static std::filesystem::path getApplicationDirPath() { return GameLoader::getApplicationDirPath(); }
static WoWModel* getLoadedModel() { return ModelLoader::getLoadedModel(app); }
static void setLoadStatus(const std::string& s) { GameLoader::setLoadStatus(s, app); }
static std::string getLoadStatus() { return GameLoader::getLoadStatus(app); }
static void pollAsyncLoad() { GameLoader::pollAsyncLoad(app); }
static void beginLoadWoW() { GameLoader::beginLoadWoW(app); }
static void launchLoadThread(const core::GameConfig& c) { GameLoader::launchLoadThread(c, app); }
static void applySkin(WoWModel* m, int idx) { ModelLoader::applySkin(m, idx, app); }
static void resetCameraToModel(OrbitCamera& cam, const WoWModel* m) { ModelLoader::resetCameraToModel(cam, m); }
static void loadModel(GameFile* f) { ModelLoader::loadModel(f, app); }
static void clearModel() { ModelLoader::clearModel(app); }
static void rebuildEquipFilteredItems() { ModelLoader::rebuildEquipFilteredItems(app); }
static void buildItemSets() { ModelLoader::buildItemSets(app); }
static void rebuildItemSetFilter() { ModelLoader::rebuildItemSetFilter(app); }
static void applyItemSet(WoWModel* m, int id) { ModelLoader::applyItemSet(m, id, app); }
static void buildStartOutfits(WoWModel* m) { ModelLoader::buildStartOutfits(m, app); }
static void rebuildStartOutfitFilter() { ModelLoader::rebuildStartOutfitFilter(app); }
static void applyStartOutfit(WoWModel* m, int id) { ModelLoader::applyStartOutfit(m, id, app); }
static void rebuildNpcFilter() { ModelLoader::rebuildNpcFilter(app); }
static void loadNPC(unsigned int id) { ModelLoader::loadNPC(id, app); }
static void rebuildItemBrowseFilter() { ModelLoader::rebuildItemBrowseFilter(app); }
static void loadItemModel(unsigned int id) { ModelLoader::loadItemModel(id, app); }
static void buildMountList() { ModelLoader::buildMountList(app); }
static void rebuildMountFilter() { ModelLoader::rebuildMountFilter(app); }
static void mountCharacter(int d, GameFile* f) { ModelLoader::mountCharacter(d, f, app); }
static void dismountCharacter() { ModelLoader::dismountCharacter(app); }
static void saveCharacterPreset(const char* p) { PresetManager::save(p, app); }
static void loadCharacterPreset(const char* p) { PresetManager::load(p, app); }
static void doURLImport() { URLImportHandler::doImport(app); }
static void initAnimationControl(WoWModel* m) { ModelLoader::initAnimationControl(m, app); }
static void initCharacterControl(WoWModel* m) { ModelLoader::initCharacterControl(m, app); }
static void initModelControl(WoWModel* m) { ModelLoader::initModelControl(m, app); }
static void tryToEquipItem(WoWModel* m, int id) { ModelLoader::tryToEquipItem(m, id, app); }

// ---- Folder Picker helpers ------------------------------------------------
static void folderPickerRefresh()
{
    app.folderPickerEntries.clear();
    namespace fs = std::filesystem;
    std::error_code ec;

    if (app.folderPickerCurrent.empty())
    {
        // List drive roots on Windows
#ifdef _WIN32
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (drives & (1u << i))
            {
                std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
                app.folderPickerEntries.push_back(fs::path(root));
            }
        }
#else
        app.folderPickerEntries.push_back(fs::path("/"));
#endif
    }
    else
    {
        for (auto& entry : fs::directory_iterator(app.folderPickerCurrent, fs::directory_options::skip_permission_denied, ec))
        {
            if (entry.is_directory(ec))
                app.folderPickerEntries.push_back(entry.path());
        }
        std::sort(app.folderPickerEntries.begin(), app.folderPickerEntries.end(),
            [](const fs::path& a, const fs::path& b)
            {
                return core::toLower(a.filename().string()) < core::toLower(b.filename().string());
            });
    }

    app.folderPickerNeedsRefresh = false;
}

static void openFolderPicker()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Start from the current path buffer if it's a valid directory
    fs::path startDir(app.pathBuf);
    if (fs::is_directory(startDir, ec))
        app.folderPickerCurrent = startDir;
    else if (startDir.has_parent_path() && fs::is_directory(startDir.parent_path(), ec))
        app.folderPickerCurrent = startDir.parent_path();
    else
        app.folderPickerCurrent.clear(); // show drive roots

    app.folderPickerNeedsRefresh = true;
    app.showFolderPicker = true;
}

// ---- Handle viewport input ------------------------------------------------
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
        app.camera.setRadius(app.camera.radius() + zoom);
    }

    // Left drag ? orbit (yaw / pitch)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        app.camera.setYawAndPitch(app.camera.yaw() + (-dx), app.camera.pitch() + (-dy));
    }

    // Right drag ? pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x * MOUSE_SENSITIVITY * mul * 0.025f;
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul * 0.025f;
        const auto  look  = app.camera.lookAt();
        const auto  right = app.camera.right();
        app.camera.setLookAt(glm::vec3(look.x + right.x * -dx,
                                      look.y + right.y * -dx,
                                      look.z + dy));
    }

    // Middle drag ? zoom (alternative)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const float dy = io.MouseDelta.y * MOUSE_SENSITIVITY * mul;
        app.camera.setRadius(app.camera.radius() + dy / 10.0f);
    }

    // Numpad camera controls
    if (ImGui::IsKeyDown(ImGuiKey_Keypad4))
        app.camera.setYaw(app.camera.yaw() + 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad6))
        app.camera.setYaw(app.camera.yaw() - 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad8))
        app.camera.setPitch(app.camera.pitch() + 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Keypad2))
        app.camera.setPitch(app.camera.pitch() - 1.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad5))
        resetCameraToModel(app.camera, getLoadedModel());
    if (ImGui::IsKeyDown(ImGuiKey_Keypad7))
    {
        auto la = app.camera.lookAt();
        app.camera.setLookAt(glm::vec3(la.x, la.y, la.z + 0.2f));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad9))
    {
        auto la = app.camera.lookAt();
        app.camera.setLookAt(glm::vec3(la.x, la.y, la.z - 0.2f));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad1))
    {
        auto la = app.camera.lookAt();
        auto r = app.camera.right();
        app.camera.setLookAt(glm::vec3(la.x + r.x * -0.2f, la.y + r.y * -0.2f, la.z));
    }
    if (ImGui::IsKeyDown(ImGuiKey_Keypad3))
    {
        auto la = app.camera.lookAt();
        auto r = app.camera.right();
        app.camera.setLookAt(glm::vec3(la.x + r.x * 0.2f, la.y + r.y * 0.2f, la.z));
    }
}

// ---- Animation tick -------------------------------------------------------
static void tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - app.lastTick).count();
    app.lastTick = now;

    // Clamp to avoid huge jumps after breakpoints, window moves, or long pauses
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    // FPS tracking
    app.fpsAccum += dt;
    app.fpsFrameCount++;
    if (app.fpsAccum >= 0.5f)
    {
        app.fps = static_cast<float>(app.fpsFrameCount) / app.fpsAccum;
        app.fpsFrameCount = 0;
        app.fpsAccum = 0.0f;
    }

    app.animTime += dt;

    if (app.root)
        app.root->tick(dt * 1000.0f);
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

    app.settings.load();

    // Apply initial console visibility
#ifdef _WIN32
    if (HWND hConsole = GetConsoleWindow())
        ShowWindow(hConsole, app.settings.showConsole ? SW_SHOW : SW_HIDE);
#endif

    // Pre-fill the path input buffer from saved settings
    strncpy_s(app.pathBuf, app.settings.gamePath.c_str(), sizeof(app.pathBuf) - 1);

    // Instantiate exporters (OBJ / FBX)
    app.exporters.push_back(new OBJExporter());
    app.exporters.push_back(new FBXExporter());

    // Instantiate importers (Armory / Wowhead)
    app.importers.push_back(new ArmoryImporter());
    app.importers.push_back(new WowheadImporter());
}

static void initGL()
{
    video.render = true;
    // video.Init() calls gladLoaderLoadGL() internally � safe after GLFW context
    video.InitGL();

    SceneRenderer::initResources();

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
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "WoW Model Viewer", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    app.window = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

// Set window icon from wmv_16.png.
{
    // Resolve the icon relative to the executable so it works in installed
    // builds (NSIS) as well as development builds (compile-time path).
    int iw = 0, ih = 0, ic = 0;
    unsigned char* px = nullptr;
#ifdef _WIN32
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path iconPath = std::filesystem::path(exePath).parent_path() / "wmv_16.png";
        px = stbi_load(iconPath.string().c_str(), &iw, &ih, &ic, 4);
    }
#endif
    if (!px)
        px = stbi_load(WMV_ICON_PATH, &iw, &ih, &ic, 4);
    if (px)
    {
        GLFWimage img{ iw, ih, px };
        glfwSetWindowIcon(window, 1, &img);
        stbi_image_free(px);
    }
}

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

    // Create root attachment (scene graph root � no model yet)
    app.root = new Attachment(nullptr, nullptr, -1, -1);

    // ---- Dear ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = AppSettings::imguiIniPath;

    ThemeManager::apply(ThemeManager::currentTheme(), app.window);

    // ---- DPI-aware scaling ----
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    app.dpiScale = (xscale > yscale) ? xscale : yscale;
    if (app.dpiScale > 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(app.dpiScale);
    }

    // ---- Font discovery ----
    {
        namespace fs = std::filesystem;
        // Look for fonts next to the executable (installed build) and in the
        // compile-time source tree (development build).
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
        // Always include a "fonts" dir relative to cwd as fallback.
        searchDirs.push_back(fs::current_path() / "fonts");

        std::set<std::string> seen; // avoid duplicates (keyed on lowercase stem name)
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
                app.availableFonts.push_back({stemName, absPath});
            }
        }
        // Sort alphabetically by display name
        std::sort(app.availableFonts.begin(), app.availableFonts.end(),
            [](const FontEntry& a, const FontEntry& b) { return a.name < b.name; });

        // Default font: prefer "arialn" by name if available and no saved preference
        if (app.settings.currentFont <= 0)
        {
            for (int i = 0; i < static_cast<int>(app.availableFonts.size()); ++i)
            {
                std::string lower = app.availableFonts[i].name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "arialn")
                {
                    app.settings.currentFont = i;
                    break;
                }
            }
        }
    }

    // ---- Build initial font atlas ----
    {
        const float pixelSize = app.settings.fontSize * app.dpiScale;
        bool loaded = false;
        if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
        {
            const auto& fe = app.availableFonts[app.settings.currentFont];
            if (std::filesystem::exists(fe.path))
            {
                io.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
                loaded = true;
                LOG_INFO << "Loaded font:" << fe.name << "at" << pixelSize << "px";
            }
        }
        if (!loaded)
            io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.0f; // size is already baked into the rasterised glyphs
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = false;
    bool firstFrame = true;
    app.lastTick = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // ---- Rebuild font atlas if font/size changed ----
        if (app.fontsDirty)
        {
            app.fontsDirty = false;
            ImGuiIO& fio = ImGui::GetIO();
            fio.Fonts->Clear();
            const float pixelSize = app.settings.fontSize * app.dpiScale;
            bool loaded = false;
            if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
            {
                const auto& fe = app.availableFonts[app.settings.currentFont];
                if (std::filesystem::exists(fe.path))
                {
                    fio.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
                    loaded = true;
                }
            }
            if (!loaded)
                fio.Fonts->AddFontDefault();
            fio.FontGlobalScale = 1.0f;
            fio.Fonts->Build();
        }

        // Check if background loading thread has finished
        pollAsyncLoad();

        // Animation tick
        tickScene();

        // ---- ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // ---- Build default docking layout on first frame (only if no saved layout) ----
        if (firstFrame)
        {
            firstFrame = false;
            if (!std::filesystem::exists(AppSettings::imguiIniPath))
            {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(static_cast<float>(fw), static_cast<float>(fh)));

            ImGuiID dock_left, dock_center;
            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.15f, &dock_left, &dock_center);

            ImGuiID dock_right;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.20f, &dock_right, &dock_center);

            ImGuiID dock_bottom;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.25f, &dock_bottom, &dock_center);

            ImGui::DockBuilderDockWindow("File Browser", dock_left);
            ImGui::DockBuilderDockWindow("NPC Browser", dock_left);
            ImGui::DockBuilderDockWindow("Item Browser", dock_left);
            ImGui::DockBuilderDockWindow("3D Viewport", dock_center);
            ImGui::DockBuilderDockWindow("Character Viewer", dock_center);
            ImGui::DockBuilderDockWindow("Animation", dock_bottom);
            ImGui::DockBuilderDockWindow("Screenshot", dock_bottom);
            ImGui::DockBuilderDockWindow("Export", dock_bottom);
            ImGui::DockBuilderDockWindow("Presets", dock_bottom);
            ImGui::DockBuilderDockWindow("Viewport Options", dock_right);
            ImGui::DockBuilderDockWindow("Mounts", dock_right);
            ImGui::DockBuilderDockWindow("Item Sets", dock_right);
            ImGui::DockBuilderDockWindow("Log", dock_bottom);
            ImGui::DockBuilderFinish(dockspace_id);
            }
        }

        // ===== Main Menu Bar =====
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Load WoW", nullptr, false, !app.isWoWLoaded && !app.loadInProgress))
                    beginLoadWoW();
                ImGui::Separator();
                if (ImGui::MenuItem("Import from URL...", nullptr, false, app.isWoWLoaded && app.initDB))
                {
                    app.showImportDialog = true;
                    app.importPopupJustOpened = true;
                    app.importStatus.clear();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Screenshot...", "Ctrl+S"))
                {
                    // Focus the Screenshot panel (user picks path there)
                    ImGui::SetWindowFocus("Screenshot");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4"))
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("3D Viewport", nullptr, &app.showViewport);
                ImGui::MenuItem("Character Viewer", nullptr, &app.showCharViewer);
                ImGui::MenuItem("File Browser", nullptr, &app.showFileBrowser);
                ImGui::MenuItem("Animation", nullptr, &app.showAnimation);
                ImGui::MenuItem("Viewport Options", nullptr, &app.showViewportOpts);
                ImGui::MenuItem("Mounts", nullptr, &app.showMounts);
                ImGui::MenuItem("Item Sets", nullptr, &app.showItemSets);
                ImGui::MenuItem("NPC Browser", nullptr, &app.showNpcBrowser);
                ImGui::MenuItem("Item Browser", nullptr, &app.showItemBrowser);
                ImGui::MenuItem("Export", nullptr, &app.showExport);
                ImGui::MenuItem("Screenshot", nullptr, &app.showScreenshot);
                ImGui::MenuItem("Presets", nullptr, &app.showPresets);
                ImGui::MenuItem("Log", nullptr, &app.showLog);
                ImGui::MenuItem("Settings", nullptr, &app.showSettings);
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Options"))
            {
                if (ImGui::MenuItem("Language / Locale..."))
                    app.showLanguageDialog = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Settings..."))
                    app.showSettings = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About..."))
                    app.showAboutDialog = true;
                ImGui::EndMenu();
            }

            // ---- Status bar (right-aligned in menu bar) ----
            {
                WoWModel* sm = getLoadedModel();
                std::string statusText;
                if (sm)
                {
                    int curFrame = 0, totalFrames = 0;
                    if (sm->animManager)
                    {
                        curFrame = static_cast<int>(sm->animManager->GetFrame());
                        totalFrames = static_cast<int>(sm->animManager->GetFrameCount());
                    }
                    statusText = std::format("FPS: {:.0f} | {} | V:{} B:{} T:{} | Frame: {}/{}",
                        app.fps, sm->name(),
                        sm->header.nVertices, sm->header.nBones, sm->header.nTextures,
                        curFrame, totalFrames);
                }
                else
                {
                    statusText = std::format("FPS: {:.0f}", app.fps);
                }
                float textWidth = ImGui::CalcTextSize(statusText.c_str()).x;
                ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 10.0f);
                ImGui::TextDisabled("%s", statusText.c_str());
            }

            ImGui::EndMainMenuBar();
        }

        // ===== Character Viewer (standalone tab like wow.export) =====
        if (app.showCharViewer)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::Begin("Character Viewer", &app.showCharViewer))
        {
            CharacterViewerPanel::DrawContext cvCtx;
            cvCtx.isWoWLoaded          = app.isWoWLoaded;
            cvCtx.isDBReady            = app.initDB;
            cvCtx.isChar               = app.isChar;
            cvCtx.customizationOptions = &app.customizationOptions;
            cvCtx.animEntries          = &app.animEntries;
            cvCtx.selectedAnimCombo    = &app.selectedAnimCombo;
            cvCtx.fbo                  = &app.fbo;
            cvCtx.camera               = &app.camera;
            cvCtx.root                 = app.root;
            cvCtx.fov                  = video.fov;
            cvCtx.bgColor              = app.settings.bgColor;
            cvCtx.drawGrid             = app.settings.drawGrid;
            cvCtx.getLoadedModel       = getLoadedModel;
            cvCtx.loadModel            = [](GameFile* f) { loadModel(f); };
            cvCtx.handleViewportInput  = handleViewportInput;

            CharacterViewerPanel::draw(cvCtx);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        }

        // ===== 3D Viewport panel =====
        if (app.showViewport)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport", &app.showViewport))
        {
            // Determine available size for the viewport image
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);

            if (vpW > 0 && vpH > 0)
            {
                // Render scene to offscreen FBO
                SceneRenderer::renderToFBO(app.fbo, vpW, vpH, app.camera, app.root, video.fov, app.settings.bgColor, app.settings.drawGrid);

                // Display FBO colour texture (UV-flipped: OpenGL is bottom-up)
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(app.fbo.colorTex)),
                             panelSize,
                             ImVec2(0, 1), ImVec2(1, 0));

                // Handle orbit camera input when viewport is hovered
                if (ImGui::IsItemHovered())
                    handleViewportInput();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        }

        // ===== File Browser panel =====
        if (app.showFileBrowser)
        {
            auto statusStr = getLoadStatus();
            FileBrowserPanel::LoadState ls;
            ls.isLoaded   = app.isWoWLoaded;
            ls.inProgress = app.loadInProgress;
            ls.progress   = app.loadProgress;
            ls.statusText = statusStr.c_str();

            if (GameFile* picked = FileBrowserPanel::draw(app.showFileBrowser, ls))
                loadModel(picked);
        }

        // ===== Animation Control =====
        if (app.showAnimation)
        {
        if (ImGui::Begin("Animation", &app.showAnimation))
        {
            AnimationPanel::DrawContext animCtx;
            animCtx.getLoadedModel       = getLoadedModel;
            animCtx.animEntries          = &app.animEntries;
            animCtx.selectedAnimCombo    = &app.selectedAnimCombo;
            animCtx.animSpeed            = &app.animSpeed;
            animCtx.loopCount            = &app.loopCount;
            animCtx.lockAnims            = &app.lockAnims;
            animCtx.selectedSecondaryAnim = &app.selectedSecondaryAnim;
            animCtx.selectedMouthAnim    = &app.selectedMouthAnim;
            animCtx.mouthSpeed           = &app.mouthSpeed;
            animCtx.skinEntries          = &app.skinEntries;
            animCtx.selectedSkin         = &app.selectedSkin;
            animCtx.blpSkin[0] = app.blpSkin[0];
            animCtx.blpSkin[1] = app.blpSkin[1];
            animCtx.blpSkin[2] = app.blpSkin[2];
            animCtx.applySkin            = [](WoWModel* m, int idx) { applySkin(m, idx); };

            AnimationPanel::draw(animCtx);

            // Copy back blpSkin state (modified by per-slot BLP selector)
            app.blpSkin[0] = animCtx.blpSkin[0];
            app.blpSkin[1] = animCtx.blpSkin[1];
            app.blpSkin[2] = animCtx.blpSkin[2];
        }
        ImGui::End();
        }

        // ===== Viewport Options (combined Background / Lighting / Model Control) =====
        if (app.showViewportOpts)
        {
        if (ImGui::Begin("Viewport Options", &app.showViewportOpts))
        {
            ViewportOptionsPanel::DrawContext vpCtx;
            vpCtx.drawGrid       = &app.settings.drawGrid;
            vpCtx.bgColor        = &app.settings.bgColor;
            vpCtx.camera         = &app.camera;
            vpCtx.getLoadedModel = getLoadedModel;
            vpCtx.geosetGroups   = &app.geosetGroups;
            vpCtx.pcrState       = &app.pcrState;
            vpCtx.selectedSkin   = &app.selectedSkin;
            vpCtx.applySkin      = [](WoWModel* m, int idx) { applySkin(m, idx); };
            vpCtx.resetCamera    = [] { resetCameraToModel(app.camera, getLoadedModel()); };

            ViewportOptionsPanel::draw(vpCtx);
        }
        ImGui::End();
        }
        if (app.showMounts)
        {
        if (ImGui::Begin("Mounts", &app.showMounts))
        {
            MountsPanel::DrawContext mCtx;
            mCtx.isChar              = app.isChar;
            mCtx.isMounted           = app.isMounted;
            mCtx.mountList           = &app.mountList;
            mCtx.creatureModelNames  = &app.creatureModelNames;
            mCtx.creatureModels      = &app.creatureModels;
            mCtx.mountFiltered       = &app.mountFiltered;
            mCtx.mountFilterDirty    = &app.mountFilterDirty;
            mCtx.mountTab            = &app.mountTab;
            mCtx.mountSearchBuf      = app.mountSearchBuf;
            mCtx.mountSearchBufSize  = sizeof(app.mountSearchBuf);
            mCtx.getLoadedModel      = getLoadedModel;
            mCtx.buildMountList      = [] { buildMountList(); };
            mCtx.rebuildMountFilter  = [] { rebuildMountFilter(); };
            mCtx.mountCharacter      = [](int d, GameFile* f) { mountCharacter(d, f); };
            mCtx.dismountCharacter   = [] { dismountCharacter(); };

            MountsPanel::draw(mCtx);
        }
        ImGui::End();
        }

        // ===== Item Sets panel (standalone tab) =====
        if (app.showItemSets)
        {
        if (ImGui::Begin("Item Sets", &app.showItemSets))
        {
            ItemSetsPanel::DrawContext isCtx;
            isCtx.isChar                  = app.isChar;
            isCtx.itemSets                = &app.itemSets;
            isCtx.itemSetsBuilt           = &app.itemSetsBuilt;
            isCtx.itemSetSearchBuf        = app.itemSetSearchBuf;
            isCtx.itemSetSearchBufSize    = sizeof(app.itemSetSearchBuf);
            isCtx.itemSetFiltered         = &app.itemSetFiltered;
            isCtx.itemSetFilterDirty      = &app.itemSetFilterDirty;
            isCtx.startOutfits            = &app.startOutfits;
            isCtx.startOutfitsBuilt       = &app.startOutfitsBuilt;
            isCtx.startOutfitSearchBuf    = app.startOutfitSearchBuf;
            isCtx.startOutfitSearchBufSize = sizeof(app.startOutfitSearchBuf);
            isCtx.startOutfitFiltered     = &app.startOutfitFiltered;
            isCtx.startOutfitFilterDirty  = &app.startOutfitFilterDirty;
            isCtx.getLoadedModel          = getLoadedModel;
            isCtx.buildItemSets           = [] { buildItemSets(); };
            isCtx.rebuildItemSetFilter    = [] { rebuildItemSetFilter(); };
            isCtx.applyItemSet            = [](WoWModel* m, int id) { applyItemSet(m, id); };
            isCtx.buildStartOutfits       = [](WoWModel* m) { buildStartOutfits(m); };
            isCtx.rebuildStartOutfitFilter = [] { rebuildStartOutfitFilter(); };
            isCtx.applyStartOutfit        = [](WoWModel* m, int id) { applyStartOutfit(m, id); };

            ItemSetsPanel::draw(isCtx);
        }
        ImGui::End();
        }

        // ===== NPC Browser panel =====
        if (app.showNpcBrowser)
        {
        if (ImGui::Begin("NPC Browser", &app.showNpcBrowser))
        {
            NpcBrowserPanel::DrawContext npcCtx;
            npcCtx.isWoWLoaded      = app.isWoWLoaded;
            npcCtx.isDBReady        = app.initDB;
            npcCtx.npcs             = &npcs;
            npcCtx.npcFiltered      = &app.npcFiltered;
            npcCtx.npcFilterDirty   = &app.npcFilterDirty;
            npcCtx.npcSearchBuf     = app.npcSearchBuf;
            npcCtx.npcSearchBufSize = sizeof(app.npcSearchBuf);
            npcCtx.rebuildNpcFilter = [] { rebuildNpcFilter(); };
            npcCtx.loadNPC          = [](unsigned int id) { loadNPC(id); };

            NpcBrowserPanel::draw(npcCtx);
        }
        ImGui::End();
        }

        // ===== Item Browser panel =====
        if (app.showItemBrowser)
        {
        if (ImGui::Begin("Item Browser", &app.showItemBrowser))
        {
            ItemBrowserPanel::DrawContext ibCtx;
            ibCtx.isWoWLoaded            = app.isWoWLoaded;
            ibCtx.isDBReady              = app.initDB;
            ibCtx.items                  = &items;
            ibCtx.itemBrowseFiltered     = &app.itemBrowseFiltered;
            ibCtx.itemBrowseFilterDirty  = &app.itemBrowseFilterDirty;
            ibCtx.itemBrowseSearchBuf    = app.itemBrowseSearchBuf;
            ibCtx.itemBrowseSearchBufSize = sizeof(app.itemBrowseSearchBuf);
            ibCtx.rebuildItemBrowseFilter = [] { rebuildItemBrowseFilter(); };
            ibCtx.loadItemModel          = [](unsigned int id) { loadItemModel(id); };

            ItemBrowserPanel::draw(ibCtx);
        }
        ImGui::End();
        }

        // ===== Export panel =====
        if (app.showExport)
        {
        if (ImGui::Begin("Export", &app.showExport))
        {
            ExportPanel::DrawContext exCtx;
            exCtx.getLoadedModel    = getLoadedModel;
            exCtx.exporters         = &app.exporters;
            exCtx.selectedExporter  = &app.selectedExporter;
            exCtx.animEntries       = &app.animEntries;
            exCtx.exportAnimChecked = &app.exportAnimChecked;
            exCtx.selectedAnimCombo = &app.selectedAnimCombo;
            exCtx.exportPath        = app.exportPath;
            exCtx.exportPathSize    = sizeof(app.exportPath);
            exCtx.exportStatus      = &app.exportStatus;

            ExportPanel::draw(exCtx);
        }
        ImGui::End();
        }

        // ===== Screenshot panel =====
        if (app.showScreenshot)
        {
        if (ImGui::Begin("Screenshot", &app.showScreenshot))
        {
            ScreenshotPanel::DrawContext ssCtx;
            ssCtx.screenshotPath     = app.screenshotPath;
            ssCtx.screenshotPathSize = sizeof(app.screenshotPath);
            ssCtx.screenshotStatus   = &app.screenshotStatus;
            ssCtx.useCanvasOverride  = &app.useCanvasOverride;
            ssCtx.canvasWidth        = &app.canvasWidth;
            ssCtx.canvasHeight       = &app.canvasHeight;
            ssCtx.fbo                = &app.fbo;
            ssCtx.camera             = &app.camera;
            ssCtx.root               = app.root;
            ssCtx.fov                = video.fov;
            ssCtx.bgColor            = app.settings.bgColor;
            ssCtx.drawGrid           = app.settings.drawGrid;

            ScreenshotPanel::draw(ssCtx);
        }
        ImGui::End();
        }

        // ===== Character Preset panel =====
        if (app.showPresets)
        {
        if (ImGui::Begin("Presets", &app.showPresets))
        {
            PresetsPanel::DrawContext preCtx;
            preCtx.presetPath     = app.presetPath;
            preCtx.presetPathSize = sizeof(app.presetPath);
            preCtx.presetStatus   = &app.presetStatus;
            preCtx.isChar         = app.isChar;
            preCtx.hasModel       = getLoadedModel() != nullptr;
            preCtx.savePreset     = [](const char* p) { saveCharacterPreset(p); };
            preCtx.loadPreset     = [](const char* p) { loadCharacterPreset(p); };

            PresetsPanel::draw(preCtx);
        }
        ImGui::End();
        }

        // ===== Log viewer panel =====
        if (app.showLog)
        {
        if (ImGui::Begin("Log", &app.showLog))
        {
            LogPanel::DrawContext logCtx;
            logCtx.logLines       = &app.logLines;
            logCtx.logAutoScroll  = &app.logAutoScroll;
            logCtx.logNeedsReload = &app.logNeedsReload;

            LogPanel::draw(logCtx);
        }
        ImGui::End();
        }

        // ===== Settings panel (floating popup) =====
        if (app.showSettings)
        {
        ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Settings", &app.showSettings, ImGuiWindowFlags_NoDocking))
        {
            // ---- Game loading section ----
            ImGui::SeparatorText("World of Warcraft");

            ImGui::Text("Game Data Path:");
            float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputText("##gamepath", app.pathBuf, sizeof(app.pathBuf));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
                openFolderPicker();

            // ---- Folder Picker popup ----
            if (app.showFolderPicker)
                ImGui::OpenPopup("Select Folder##FolderPicker");

            if (ImGui::BeginPopupModal("Select Folder##FolderPicker", &app.showFolderPicker,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                // Current path display
                std::string curPathStr = app.folderPickerCurrent.empty()
                    ? "My Computer" : app.folderPickerCurrent.string();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", curPathStr.c_str());
                ImGui::Separator();

                if (app.folderPickerNeedsRefresh)
                    folderPickerRefresh();

                // Up / back button
                {
                    bool canGoUp = !app.folderPickerCurrent.empty() && app.folderPickerCurrent.has_parent_path()
                        && app.folderPickerCurrent.parent_path() != app.folderPickerCurrent;
                    bool canGoRoot = !app.folderPickerCurrent.empty();
                    if (!canGoUp && !canGoRoot) ImGui::BeginDisabled();
                    if (ImGui::Button("Up"))
                    {
                        if (canGoUp)
                            app.folderPickerCurrent = app.folderPickerCurrent.parent_path();
                        else
                            app.folderPickerCurrent.clear(); // back to drive roots
                        app.folderPickerNeedsRefresh = true;
                    }
                    if (!canGoUp && !canGoRoot) ImGui::EndDisabled();
                }

                ImGui::SameLine();
                ImGui::Text("%d folders", static_cast<int>(app.folderPickerEntries.size()));

                // Folder list
                ImGui::BeginChild("##FolderList", ImVec2(500, 400), ImGuiChildFlags_Borders);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(app.folderPickerEntries.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& p = app.folderPickerEntries[i];
                        std::string displayName = app.folderPickerCurrent.empty()
                            ? p.string() : p.filename().string();
                        ImGui::PushID(i);
                        if (ImGui::Selectable(displayName.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                app.folderPickerCurrent = p;
                                app.folderPickerNeedsRefresh = true;
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                ImGui::Separator();
                if (ImGui::Button("Select This Folder", ImVec2(200, 0)))
                {
                    if (!app.folderPickerCurrent.empty())
                    {
                        strncpy_s(app.pathBuf, app.folderPickerCurrent.string().c_str(), sizeof(app.pathBuf) - 1);
                        app.showFolderPicker = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    app.showFolderPicker = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (app.isWoWLoaded)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded: %s (%s)",
                                   GAMEDIRECTORY.version().c_str(),
                                   GAMEDIRECTORY.locale().c_str());
            }
            else if (app.loadInProgress)
            {
                ImGui::ProgressBar(app.loadProgress);
                auto status = getLoadStatus();
                ImGui::TextWrapped("%s", status.c_str());
            }
            else
            {
                auto status = getLoadStatus();
                if (!status.empty())
                    ImGui::TextWrapped("%s", status.c_str());
                else
                    ImGui::TextDisabled("Use File > Load WoW to load game data.");
            }

            ImGui::Checkbox("Enable Database Cache", &app.settings.enableDbCache);
            ImGui::TextDisabled("Speeds up loading by caching the database. Takes effect on next load.");

            // ---- General section ----
            ImGui::SeparatorText("General");
            if (ImGui::Checkbox("Show Console Window", &app.settings.showConsole))
            {
#ifdef _WIN32
                if (HWND hConsole = GetConsoleWindow())
                    ShowWindow(hConsole, app.settings.showConsole ? SW_SHOW : SW_HIDE);
#endif
            }
            ImGui::TextDisabled("Shows/hides the debug console. Useful for diagnostics.");

            // ---- Theme selector ----
            ImGui::SeparatorText("Appearance");
            ImGui::Text("Theme:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Theme", &ThemeManager::currentThemeRef(), ThemeManager::themeNames(), ThemeManager::themeCount()))
                ThemeManager::apply(ThemeManager::currentTheme(), app.window);

            // ---- Font selector ----
            ImGui::Text("Font:");
            ImGui::SetNextItemWidth(-1);
            if (!app.availableFonts.empty())
            {
                if (ImGui::BeginCombo("##Font",
                    (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.availableFonts.size()))
                        ? app.availableFonts[app.settings.currentFont].name.c_str() : "Default"))
                {
                    for (int i = 0; i < static_cast<int>(app.availableFonts.size()); ++i)
                    {
                        ImGui::PushID(i);
                        const bool selected = (i == app.settings.currentFont);
                        if (ImGui::Selectable(app.availableFonts[i].name.c_str(), selected))
                        {
                            app.settings.currentFont = i;
                            app.fontsDirty = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextDisabled("No .ttf/.otf files found in fonts/ directory.");
            }

            ImGui::Text("Font Size:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##FontSize", &app.settings.fontSize, 10.0f, 40.0f, "%.0f px"))
                app.fontsDirty = true;
            ImGui::TextDisabled("Drop .ttf or .otf files into the fonts/ folder to add more.");

            ImGui::Separator();
            ImGui::Checkbox("ImGui Demo Window", &show_demo_window);
            ImGui::Separator();
            ImGui::Text("Camera  yaw=%.1f  pitch=%.1f  radius=%.2f",
                        app.camera.yaw(), app.camera.pitch(), app.camera.radius());
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));

            // ---- Save ----
            ImGui::SeparatorText("Save");
            if (ImGui::Button("Save Settings", ImVec2(-1, 0)))
            {
                app.settings.gamePath = app.pathBuf;
                app.settings.save();
            }
            ImGui::TextDisabled("Saves preferences and UI layout.");
        }
        ImGui::End();
        }

        // ===== URL Import dialog =====
        if (app.showImportDialog)
            ImGui::OpenPopup("Import from URL##ImportModal");

        if (ImGui::BeginPopupModal("Import from URL##ImportModal", &app.showImportDialog,
            ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Paste an Armory, Battle.net, or Wowhead URL:");
            ImGui::Spacing();

            if (app.importPopupJustOpened)
            {
                ImGui::SetKeyboardFocusHere();
                app.importPopupJustOpened = false;
            }

            ImGui::SetNextItemWidth(500);
            ImGui::InputText("##importUrl", app.importUrlBuf, sizeof(app.importUrlBuf));

            ImGui::Spacing();
            if (ImGui::Button("Import", ImVec2(120, 0)))
                doURLImport();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                app.showImportDialog = false;
                ImGui::CloseCurrentPopup();
            }

            if (!app.importStatus.empty())
            {
                ImGui::Spacing();
                bool isError = app.importStatus.find("failed") != std::string::npos ||
                               app.importStatus.find("No ") != std::string::npos ||
                               app.importStatus.find("not") != std::string::npos ||
                               app.importStatus.find("Please") != std::string::npos;
                if (isError)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", app.importStatus.c_str());
                else
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", app.importStatus.c_str());
            }

            ImGui::EndPopup();
        }

        // ===== Config selection modal (multiple WoW installs) =====
        if (app.showConfigPopup)
            ImGui::OpenPopup("Select WoW Config");

        if (ImGui::BeginPopupModal("Select WoW Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Multiple configurations found. Please select one:");
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(app.pendingConfigs.size()); ++i)
            {
                std::string label = app.pendingConfigs[i].locale + " - " + app.pendingConfigs[i].product;
                if (!app.pendingConfigs[i].version.empty())
                    label += " (" + app.pendingConfigs[i].version + ")";
                ImGui::RadioButton(label.c_str(), &app.selectedConfig, i);
            }

            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                app.showConfigPopup = false;
                ImGui::CloseCurrentPopup();
                launchLoadThread(app.pendingConfigs[app.selectedConfig]);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                app.showConfigPopup = false;
                setLoadStatus("Load cancelled.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // ===== About Dialog =====
        if (app.showAboutDialog)
            ImGui::OpenPopup("About WoW Model Viewer");

        if (ImGui::BeginPopupModal("About WoW Model Viewer", &app.showAboutDialog,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            // Convert wstring title to narrow UTF-8 string for ImGui
            std::wstring wTitle = GLOBALSETTINGS.appTitle();
            std::string title;
            if (!wTitle.empty())
            {
                int n = WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(), static_cast<int>(wTitle.size()), nullptr, 0, nullptr, nullptr);
                title.resize(n);
                WideCharToMultiByte(CP_UTF8, 0, wTitle.c_str(), static_cast<int>(wTitle.size()), title.data(), n, nullptr, nullptr);
            }
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", title.c_str());

            ImGui::Separator();
            ImGui::Text("A 3D model viewer for World of Warcraft game assets.");
            ImGui::Spacing();
            ImGui::Text("Built with GLFW, OpenGL, and Dear ImGui.");
            ImGui::Text("Uses CASCLib for game data access.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "https://wowmodelviewer.net");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "https://github.com/wowmodelviewer/wowmodelviewer");
            ImGui::Spacing();
            ImGui::Text("GL_RENDERER: %s", glGetString(GL_RENDERER));
            ImGui::Text("GL_VERSION:  %s", glGetString(GL_VERSION));
            ImGui::Spacing();

            if (ImGui::Button("Close", ImVec2(120, 0)))
            {
                app.showAboutDialog = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ===== Language / Locale Dialog =====
        if (app.showLanguageDialog)
            ImGui::OpenPopup("Language / Locale");

        if (ImGui::BeginPopupModal("Language / Locale", &app.showLanguageDialog,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            if (!app.isWoWLoaded)
            {
                ImGui::TextWrapped("Game data is not loaded. Load WoW first, then change the locale here.");
                ImGui::Spacing();
                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    app.showLanguageDialog = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                ImGui::Text("Current locale: %s", GAMEDIRECTORY.locale().c_str());
                ImGui::Separator();
                ImGui::Text("Select a different locale to reload game data:");
                ImGui::Spacing();

                auto configs = GAMEDIRECTORY.configsFound();
                for (int i = 0; i < static_cast<int>(configs.size()); ++i)
                {
                    std::string label = configs[i].locale + " - " + configs[i].product;
                    bool isCurrent = (configs[i].locale == GAMEDIRECTORY.locale());
                    if (isCurrent)
                        ImGui::BeginDisabled();

                    if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
                    {
                        app.showLanguageDialog = false;
                        ImGui::CloseCurrentPopup();
                        // Trigger a reload with the selected config
                        app.isWoWLoaded = false;
                        app.initDB = false;
                        app.loadInProgress = true;
                        app.loadProgress = 0.0f;
                        setLoadStatus("Reloading with locale: " + configs[i].locale + "...");
                        launchLoadThread(configs[i]);
                    }

                    if (isCurrent)
                        ImGui::EndDisabled();
                }

                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    app.showLanguageDialog = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

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
    if (app.loadThread.joinable())
        app.loadThread.join();

    FileBrowserPanel::shutdown();

    if (app.root)
    {
        app.root->delChildren();
        delete app.root;
        app.root = nullptr;
    }

    app.fbo.destroy();

    SceneRenderer::shutdown();

    for (auto* e : app.exporters)
        delete e;
    app.exporters.clear();

    for (auto* imp : app.importers)
        delete imp;
    app.importers.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer shutdown complete.";
    return 0;
}
