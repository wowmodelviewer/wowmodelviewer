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
#include "CustomTitleBar.h"
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
#include "SettingsPanel.h"
#include "AppDialogs.h"

// Game loading (headers used transitively by helper modules)

#include "stb_image.h"

// Exporters (OBJ / FBX)
#include "OBJExporter.h"
#include "FBXExporter.h"

// Importers (Armory / Wowhead URL import)
#include "ArmoryImporter.h"
#include "WowheadImporter.h"


#include <format>
#include <memory>
#include <set>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Consolidated application state and helper modules
#include "AppState.h"
#include "GameLoader.h"
#include "ModelLoader.h"
#include "PresetManager.h"
#include "InputManager.h"
#include "ViewportController.h"

static AppState app;
static InputManager inputManager;

// ---- Thin wrappers forwarding to helper modules --------------------------
static std::filesystem::path getApplicationDirPath() { return GameLoader::getApplicationDirPath(); }
static WoWModel* getLoadedModel() { return ModelLoader::getLoadedModel(app); }
static std::string getLoadStatus() { return GameLoader::getLoadStatus(app); }
static void pollAsyncLoad() { GameLoader::pollAsyncLoad(app); }
static void beginLoadWoW() { GameLoader::beginLoadWoW(app); }
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
static void initAnimationControl(WoWModel* m) { ModelLoader::initAnimationControl(m, app); }
static void initCharacterControl(WoWModel* m) { ModelLoader::initCharacterControl(m, app); }
static void initModelControl(WoWModel* m) { ModelLoader::initModelControl(m, app); }
static void tryToEquipItem(WoWModel* m, int id) { ModelLoader::tryToEquipItem(m, id, app); }

// ---- Handle viewport input (delegates to InputManager + ViewportController)
static void handleViewportInput()
{
    ViewportController::apply(inputManager.state(), app.scene.camera);
    if (inputManager.state().resetCamera)
        resetCameraToModel(app.scene.camera, getLoadedModel());
}

// ---- Animation tick -------------------------------------------------------
static void tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - app.scene.lastTick).count();
    app.scene.lastTick = now;

    // Clamp to avoid huge jumps after breakpoints, window moves, or long pauses
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    // FPS tracking
    app.scene.fpsAccum += dt;
    app.scene.fpsFrameCount++;
    if (app.scene.fpsAccum >= 0.5f)
    {
        app.scene.fps = static_cast<float>(app.scene.fpsFrameCount) / app.scene.fpsAccum;
        app.scene.fpsFrameCount = 0;
        app.scene.fpsAccum = 0.0f;
    }

    app.scene.animTime += dt;

    if (app.scene.root)
        app.scene.root->tick(dt * 1000.0f);
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
    strncpy_s(app.loading.pathBuf, app.settings.gamePath.c_str(), sizeof(app.loading.pathBuf) - 1);

    // Instantiate exporters (OBJ / FBX)
    app.exporting.exporters.push_back(std::make_unique<OBJExporter>());
    app.exporting.exporters.push_back(std::make_unique<FBXExporter>());

    // Instantiate importers (Armory / Wowhead)
    app.exporting.importers.push_back(std::make_unique<ArmoryImporter>());
    app.exporting.importers.push_back(std::make_unique<WowheadImporter>());
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

    // ---- Custom title bar (embed menus in the window frame) ----
    CustomTitleBar::init(window);

    // ---- Engine init ----
    initEngine();
    initGL();

    // Create root attachment (scene graph root � no model yet)
    app.scene.root = std::make_unique<Attachment>(nullptr, nullptr, -1, -1);

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
    app.ui.dpiScale = (xscale > yscale) ? xscale : yscale;
    if (app.ui.dpiScale > 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(app.ui.dpiScale);
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
                app.ui.availableFonts.push_back({stemName, absPath});
            }
        }
        // Sort alphabetically by display name
        std::sort(app.ui.availableFonts.begin(), app.ui.availableFonts.end(),
            [](const FontEntry& a, const FontEntry& b) { return a.name < b.name; });

        // Default font: prefer "Roboto-Regular" (UE5 default), fall back to "arialn"
        if (app.settings.currentFont <= 0)
        {
            const char* preferred[] = { "roboto-regular", "arialn" };
            for (const char* target : preferred)
            {
                bool found = false;
                for (int i = 0; i < static_cast<int>(app.ui.availableFonts.size()); ++i)
                {
                    std::string lower = app.ui.availableFonts[i].name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (lower == target)
                    {
                        app.settings.currentFont = i;
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
        }
    }

    // ---- Build initial font atlas ----
    {
        const float pixelSize = app.settings.fontSize * app.ui.dpiScale;
        bool loaded = false;
        if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.ui.availableFonts.size()))
        {
            const auto& fe = app.ui.availableFonts[app.settings.currentFont];
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
        io.FontGlobalScale = 1.0f; // size is already baked into the rasterised glyphs
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = false;
    bool firstFrame = true;
    bool resetLayout = false;
    app.scene.lastTick = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // ---- Rebuild font atlas if font/size changed ----
        if (app.ui.fontsDirty)
        {
            app.ui.fontsDirty = false;
            ImGuiIO& fio = ImGui::GetIO();
            fio.Fonts->Clear();
            const float pixelSize = app.settings.fontSize * app.ui.dpiScale;
            bool loaded = false;
            if (app.settings.currentFont >= 0 && app.settings.currentFont < static_cast<int>(app.ui.availableFonts.size()))
            {
                const auto& fe = app.ui.availableFonts[app.settings.currentFont];
                if (std::filesystem::exists(fe.path))
                {
                    fio.Fonts->AddFontFromFileTTF(fe.path.c_str(), pixelSize);
                    loaded = true;
                }
            }
            if (!loaded)
                fio.Fonts->AddFontDefault();
            CustomTitleBar::mergeIconFont(pixelSize);
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

        // ---- Resolve input bindings for this frame ----
        inputManager.update();

        // ===== Custom Title Bar (menus embedded in window frame) =====
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
        if (CustomTitleBar::begin(window))
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Load WoW", nullptr, false, !app.loading.isWoWLoaded && !app.loading.loadInProgress))
                    beginLoadWoW();
                ImGui::Separator();
                if (ImGui::MenuItem("Import from URL...", nullptr, false, app.loading.isWoWLoaded && app.loading.initDB))
                {
                    app.ui.showImportDialog = true;
                    app.ui.importPopupJustOpened = true;
                    app.exporting.importStatus.clear();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close Model", nullptr, false, getLoadedModel() != nullptr))
                    clearModel();
                ImGui::Separator();
                if (ImGui::MenuItem("Export...", nullptr, false, getLoadedModel() != nullptr))
                {
                    app.ui.showExport = true;
                    ImGui::SetWindowFocus("Export");
                }
                if (ImGui::MenuItem("Screenshot...", "Ctrl+S"))
                {
                    app.ui.showScreenshot = true;
                    ImGui::SetWindowFocus("Screenshot");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4"))
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Reset Camera", "Numpad 5", false, getLoadedModel() != nullptr))
                    resetCameraToModel(app.scene.camera, getLoadedModel());
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                    resetLayout = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("3D Viewport", nullptr, &app.ui.showViewport);
                ImGui::MenuItem("Character Viewer", nullptr, &app.ui.showCharViewer);
                ImGui::MenuItem("File Browser", nullptr, &app.ui.showFileBrowser);
                ImGui::MenuItem("Animation", nullptr, &app.ui.showAnimation);
                ImGui::MenuItem("Viewport Options", nullptr, &app.ui.showViewportOpts);
                ImGui::MenuItem("Mounts", nullptr, &app.ui.showMounts);
                ImGui::MenuItem("Item Sets", nullptr, &app.ui.showItemSets);
                ImGui::MenuItem("NPC Browser", nullptr, &app.ui.showNpcBrowser);
                ImGui::MenuItem("Item Browser", nullptr, &app.ui.showItemBrowser);
                ImGui::MenuItem("Export", nullptr, &app.ui.showExport);
                ImGui::MenuItem("Screenshot", nullptr, &app.ui.showScreenshot);
                ImGui::MenuItem("Presets", nullptr, &app.ui.showPresets);
                ImGui::MenuItem("Log", nullptr, &app.ui.showLog);
                ImGui::MenuItem("Settings", nullptr, &app.ui.showSettings);
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Options"))
            {
                if (ImGui::MenuItem("Language / Locale..."))
                    app.ui.showLanguageDialog = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Settings..."))
                    app.ui.showSettings = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About..."))
                    app.ui.showAboutDialog = true;
                ImGui::EndMenu();
            }

            // ---- Status bar + window controls (right-aligned in title bar) ----
            std::string statusText;
            {
                WoWModel* sm = getLoadedModel();
                if (sm)
                {
                    int curFrame = 0, totalFrames = 0;
                    if (sm->animManager)
                    {
                        curFrame = static_cast<int>(sm->animManager->GetFrame());
                        totalFrames = static_cast<int>(sm->animManager->GetFrameCount());
                    }
                    statusText = std::format("FPS: {:.0f} | {} | V:{} B:{} T:{} | Frame: {}/{}",
                        app.scene.fps, sm->name(),
                        sm->header.nVertices, sm->header.nBones, sm->header.nTextures,
                        curFrame, totalFrames);
                }
                else
                {
                    statusText = std::format("FPS: {:.0f}", app.scene.fps);
                }
            }
            CustomTitleBar::end(window, statusText.c_str());
        }
        ImGui::PopStyleVar(2);

        // ===== Dockspace (below custom title bar) =====
        {
            const float titleBarH = CustomTitleBar::height();
            const ImGuiViewport* mainVp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(mainVp->WorkPos.x, mainVp->WorkPos.y + titleBarH));
            ImGui::SetNextWindowSize(ImVec2(mainVp->WorkSize.x, mainVp->WorkSize.y - titleBarH));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("##DockHost", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(2);
            ImGuiID dockspace_id = ImGui::DockSpace(ImGui::GetID("MainDockspace"));
            ImGui::End();

            // ---- Build default docking layout on first frame (only if no saved layout) ----
            if (firstFrame || resetLayout)
            {
                firstFrame = false;
                if (resetLayout || !std::filesystem::exists(AppSettings::imguiIniPath))
                {
                    resetLayout = false;
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
        } // dockspace scope

        // ===== Character Viewer (standalone tab like wow.export) =====
        if (app.ui.showCharViewer)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::Begin("Character Viewer", &app.ui.showCharViewer))
        {
            CharacterViewerPanel::DrawContext cvCtx;
            cvCtx.isWoWLoaded          = app.loading.isWoWLoaded;
            cvCtx.isDBReady            = app.loading.initDB;
            cvCtx.isChar               = app.scene.isChar;
            cvCtx.customizationOptions = &app.character.customizationOptions;
            cvCtx.animEntries          = &app.anim.animEntries;
            cvCtx.selectedAnimCombo    = &app.anim.selectedAnimCombo;
            cvCtx.fbo                  = &app.scene.fbo;
            cvCtx.camera               = &app.scene.camera;
            cvCtx.root                 = app.scene.root.get();
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
        if (app.ui.showViewport)
        {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport", &app.ui.showViewport))
        {
            // Determine available size for the viewport image
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);

            if (vpW > 0 && vpH > 0)
            {
                // Render scene to offscreen FBO
                SceneRenderer::renderToFBO(app.scene.fbo, vpW, vpH, app.scene.camera, app.scene.root.get(), video.fov, app.settings.bgColor, app.settings.drawGrid);

                // Display FBO colour texture (UV-flipped: OpenGL is bottom-up)
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(app.scene.fbo.colorTex)),
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
        if (app.ui.showFileBrowser)
        {
            auto statusStr = getLoadStatus();
            FileBrowserPanel::LoadState ls;
            ls.isLoaded   = app.loading.isWoWLoaded;
            ls.inProgress = app.loading.loadInProgress;
            ls.progress   = app.loading.loadProgress;
            ls.statusText = statusStr.c_str();

            if (GameFile* picked = FileBrowserPanel::draw(app.ui.showFileBrowser, ls))
                loadModel(picked);
        }

        // ===== Animation Control =====
        if (app.ui.showAnimation)
        {
        if (ImGui::Begin("Animation", &app.ui.showAnimation))
        {
            AnimationPanel::DrawContext animCtx;
            animCtx.getLoadedModel       = getLoadedModel;
            animCtx.animEntries          = &app.anim.animEntries;
            animCtx.selectedAnimCombo    = &app.anim.selectedAnimCombo;
            animCtx.animSpeed            = &app.anim.animSpeed;
            animCtx.loopCount            = &app.anim.loopCount;
            animCtx.lockAnims            = &app.anim.lockAnims;
            animCtx.selectedSecondaryAnim = &app.anim.selectedSecondaryAnim;
            animCtx.selectedMouthAnim    = &app.anim.selectedMouthAnim;
            animCtx.mouthSpeed           = &app.anim.mouthSpeed;
            animCtx.skinEntries          = &app.anim.skinEntries;
            animCtx.selectedSkin         = &app.anim.selectedSkin;
            animCtx.blpSkin[0] = app.anim.blpSkin[0];
            animCtx.blpSkin[1] = app.anim.blpSkin[1];
            animCtx.blpSkin[2] = app.anim.blpSkin[2];
            animCtx.applySkin            = [](WoWModel* m, int idx) { applySkin(m, idx); };

            AnimationPanel::draw(animCtx);

            // Copy back blpSkin state (modified by per-slot BLP selector)
            app.anim.blpSkin[0] = animCtx.blpSkin[0];
            app.anim.blpSkin[1] = animCtx.blpSkin[1];
            app.anim.blpSkin[2] = animCtx.blpSkin[2];
        }
        ImGui::End();
        }

        // ===== Viewport Options (combined Background / Lighting / Model Control) =====
        if (app.ui.showViewportOpts)
        {
        if (ImGui::Begin("Viewport Options", &app.ui.showViewportOpts))
        {
            ViewportOptionsPanel::DrawContext vpCtx;
            vpCtx.drawGrid       = &app.settings.drawGrid;
            vpCtx.bgColor        = &app.settings.bgColor;
            vpCtx.camera         = &app.scene.camera;
            vpCtx.getLoadedModel = getLoadedModel;
            vpCtx.geosetGroups   = &app.browsers.geosetGroups;
            vpCtx.pcrState       = &app.browsers.pcrState;
            vpCtx.selectedSkin   = &app.anim.selectedSkin;
            vpCtx.applySkin      = [](WoWModel* m, int idx) { applySkin(m, idx); };
            vpCtx.resetCamera    = [] { resetCameraToModel(app.scene.camera, getLoadedModel()); };

            ViewportOptionsPanel::draw(vpCtx);
        }
        ImGui::End();
        }
        if (app.ui.showMounts)
        {
        if (ImGui::Begin("Mounts", &app.ui.showMounts))
        {
            MountsPanel::DrawContext mCtx;
            mCtx.isChar              = app.scene.isChar;
            mCtx.isMounted           = app.scene.isMounted;
            mCtx.mountList           = &app.browsers.mountList;
            mCtx.creatureModelNames  = &app.browsers.creatureModelNames;
            mCtx.creatureModels      = &app.browsers.creatureModels;
            mCtx.mountFiltered       = &app.browsers.mountFiltered;
            mCtx.mountFilterDirty    = &app.browsers.mountFilterDirty;
            mCtx.mountTab            = &app.browsers.mountTab;
            mCtx.mountSearchBuf      = app.browsers.mountSearchBuf;
            mCtx.mountSearchBufSize  = sizeof(app.browsers.mountSearchBuf);
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
        if (app.ui.showItemSets)
        {
        if (ImGui::Begin("Item Sets", &app.ui.showItemSets))
        {
            ItemSetsPanel::DrawContext isCtx;
            isCtx.isChar                  = app.scene.isChar;
            isCtx.itemSets                = &app.browsers.itemSets;
            isCtx.itemSetsBuilt           = &app.browsers.itemSetsBuilt;
            isCtx.itemSetSearchBuf        = app.browsers.itemSetSearchBuf;
            isCtx.itemSetSearchBufSize    = sizeof(app.browsers.itemSetSearchBuf);
            isCtx.itemSetFiltered         = &app.browsers.itemSetFiltered;
            isCtx.itemSetFilterDirty      = &app.browsers.itemSetFilterDirty;
            isCtx.startOutfits            = &app.browsers.startOutfits;
            isCtx.startOutfitsBuilt       = &app.browsers.startOutfitsBuilt;
            isCtx.startOutfitSearchBuf    = app.browsers.startOutfitSearchBuf;
            isCtx.startOutfitSearchBufSize = sizeof(app.browsers.startOutfitSearchBuf);
            isCtx.startOutfitFiltered     = &app.browsers.startOutfitFiltered;
            isCtx.startOutfitFilterDirty  = &app.browsers.startOutfitFilterDirty;
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
        if (app.ui.showNpcBrowser)
        {
        if (ImGui::Begin("NPC Browser", &app.ui.showNpcBrowser))
        {
            NpcBrowserPanel::DrawContext npcCtx;
            npcCtx.isWoWLoaded      = app.loading.isWoWLoaded;
            npcCtx.isDBReady        = app.loading.initDB;
            npcCtx.npcs             = &npcs;
            npcCtx.npcFiltered      = &app.browsers.npcFiltered;
            npcCtx.npcFilterDirty   = &app.browsers.npcFilterDirty;
            npcCtx.npcSearchBuf     = app.browsers.npcSearchBuf;
            npcCtx.npcSearchBufSize = sizeof(app.browsers.npcSearchBuf);
            npcCtx.rebuildNpcFilter = [] { rebuildNpcFilter(); };
            npcCtx.loadNPC          = [](unsigned int id) { loadNPC(id); };

            NpcBrowserPanel::draw(npcCtx);
        }
        ImGui::End();
        }

        // ===== Item Browser panel =====
        if (app.ui.showItemBrowser)
        {
        if (ImGui::Begin("Item Browser", &app.ui.showItemBrowser))
        {
            ItemBrowserPanel::DrawContext ibCtx;
            ibCtx.isWoWLoaded            = app.loading.isWoWLoaded;
            ibCtx.isDBReady              = app.loading.initDB;
            ibCtx.items                  = &items;
            ibCtx.itemBrowseFiltered     = &app.browsers.itemBrowseFiltered;
            ibCtx.itemBrowseFilterDirty  = &app.browsers.itemBrowseFilterDirty;
            ibCtx.itemBrowseSearchBuf    = app.browsers.itemBrowseSearchBuf;
            ibCtx.itemBrowseSearchBufSize = sizeof(app.browsers.itemBrowseSearchBuf);
            ibCtx.rebuildItemBrowseFilter = [] { rebuildItemBrowseFilter(); };
            ibCtx.loadItemModel          = [](unsigned int id) { loadItemModel(id); };

            ItemBrowserPanel::draw(ibCtx);
        }
        ImGui::End();
        }

        // ===== Export panel =====
        if (app.ui.showExport)
        {
        if (ImGui::Begin("Export", &app.ui.showExport))
        {
            ExportPanel::DrawContext exCtx;
            exCtx.getLoadedModel    = getLoadedModel;
            exCtx.exporters         = &app.exporting.exporters;
            exCtx.selectedExporter  = &app.exporting.selectedExporter;
            exCtx.animEntries       = &app.anim.animEntries;
            exCtx.exportAnimChecked = &app.exporting.exportAnimChecked;
            exCtx.selectedAnimCombo = &app.anim.selectedAnimCombo;
            exCtx.exportPath        = app.exporting.exportPath;
            exCtx.exportPathSize    = sizeof(app.exporting.exportPath);
            exCtx.exportStatus      = &app.exporting.exportStatus;

            ExportPanel::draw(exCtx);
        }
        ImGui::End();
        }

        // ===== Screenshot panel =====
        if (app.ui.showScreenshot)
        {
        if (ImGui::Begin("Screenshot", &app.ui.showScreenshot))
        {
            ScreenshotPanel::DrawContext ssCtx;
            ssCtx.screenshotPath     = app.exporting.screenshotPath;
            ssCtx.screenshotPathSize = sizeof(app.exporting.screenshotPath);
            ssCtx.screenshotStatus   = &app.exporting.screenshotStatus;
            ssCtx.useCanvasOverride  = &app.exporting.useCanvasOverride;
            ssCtx.canvasWidth        = &app.exporting.canvasWidth;
            ssCtx.canvasHeight       = &app.exporting.canvasHeight;
            ssCtx.fbo                = &app.scene.fbo;
            ssCtx.camera             = &app.scene.camera;
            ssCtx.root               = app.scene.root.get();
            ssCtx.fov                = video.fov;
            ssCtx.bgColor            = app.settings.bgColor;
            ssCtx.drawGrid           = app.settings.drawGrid;

            ScreenshotPanel::draw(ssCtx);
        }
        ImGui::End();
        }

        // ===== Character Preset panel =====
        if (app.ui.showPresets)
        {
        if (ImGui::Begin("Presets", &app.ui.showPresets))
        {
            PresetsPanel::DrawContext preCtx;
            preCtx.presetPath     = app.exporting.presetPath;
            preCtx.presetPathSize = sizeof(app.exporting.presetPath);
            preCtx.presetStatus   = &app.exporting.presetStatus;
            preCtx.isChar         = app.scene.isChar;
            preCtx.hasModel       = getLoadedModel() != nullptr;
            preCtx.savePreset     = [](const char* p) { saveCharacterPreset(p); };
            preCtx.loadPreset     = [](const char* p) { loadCharacterPreset(p); };

            PresetsPanel::draw(preCtx);
        }
        ImGui::End();
        }

        // ===== Log viewer panel =====
        if (app.ui.showLog)
        {
        if (ImGui::Begin("Log", &app.ui.showLog))
        {
            LogPanel::DrawContext logCtx;
            logCtx.logLines       = &app.ui.logLines;
            logCtx.logAutoScroll  = &app.ui.logAutoScroll;
            logCtx.logNeedsReload = &app.ui.logNeedsReload;

            LogPanel::draw(logCtx);
        }
        ImGui::End();
        }

        // ===== Settings panel (floating popup) =====
        if (app.ui.showSettings)
        {
        ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Settings", &app.ui.showSettings, ImGuiWindowFlags_NoDocking))
        {
            SettingsPanel::DrawContext settingsCtx;
            settingsCtx.pathBuf                = app.loading.pathBuf;
            settingsCtx.pathBufSize            = sizeof(app.loading.pathBuf);
            settingsCtx.isWoWLoaded            = app.loading.isWoWLoaded;
            settingsCtx.loadInProgress         = app.loading.loadInProgress;
            settingsCtx.loadProgress           = &app.loading.loadProgress;
            settingsCtx.showFolderPicker       = &app.ui.showFolderPicker;
            settingsCtx.folderPickerCurrent    = &app.ui.folderPickerCurrent;
            settingsCtx.folderPickerEntries    = &app.ui.folderPickerEntries;
            settingsCtx.folderPickerNeedsRefresh = &app.ui.folderPickerNeedsRefresh;
            settingsCtx.settings               = &app.settings;
            settingsCtx.availableFonts         = &app.ui.availableFonts;
            settingsCtx.fontsDirty             = &app.ui.fontsDirty;
            settingsCtx.window                 = app.window;
            settingsCtx.showDemoWindow         = &show_demo_window;
            settingsCtx.camera                 = &app.scene.camera;
            settingsCtx.getLoadStatus          = [&]() { return getLoadStatus(); };

            SettingsPanel::draw(settingsCtx);
        }
        ImGui::End();
        }

        // ===== Modal dialogs =====
        AppDialogs::drawImportDialog(app);
        AppDialogs::drawConfigPopup(app);

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        AppDialogs::drawAboutDialog(app);
        AppDialogs::drawLanguageDialog(app);

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
    if (app.loading.loadThread.joinable())
        app.loading.loadThread.join();

    FileBrowserPanel::shutdown();

    app.scene.root.reset();

    app.scene.fbo.destroy();

    SceneRenderer::shutdown();

    app.exporting.exporters.clear();
    app.exporting.importers.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG_INFO << "WoW Model Viewer shutdown complete.";
    return 0;
}
