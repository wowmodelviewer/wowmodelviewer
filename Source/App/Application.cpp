#include "Application.h"

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

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

#include "GlobalSettings.h"
#include "Logger.h"
#include "LogOutputFile.h"
#include "LogOutputConsole.h"
#include "video.h"
#include "Attachment.h"
#include "WoWModel.h"
#include "SceneRenderer.h"
#include "CustomTitleBar.h"
#include "ViewportController.h"

// Panel headers
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

// Exporters / Importers
#include "OBJExporter.h"
#include "FBXExporter.h"
#include "ArmoryImporter.h"
#include "WowheadImporter.h"

// Helper modules (namespace-based, take AppState&)
#include "GameLoader.h"
#include "ModelLoader.h"
#include "PresetManager.h"

// ============================================================================
// DrawContext factory helpers — wire AppState fields into per-panel contexts.
// Each function assembles a single panel's DrawContext from AppState so that
// drawPanels() stays concise and each panel remains decoupled from AppState.
// ============================================================================

namespace {

CharacterViewerPanel::DrawContext buildCharViewerCtx(
    AppState& st, std::function<void()> handleInput)
{
    CharacterViewerPanel::DrawContext ctx;
    ctx.isWoWLoaded          = st.loading.isWoWLoaded;
    ctx.isDBReady            = st.loading.initDB;
    ctx.isChar               = st.scene.isChar;
    ctx.customizationOptions = &st.character.customizationOptions;
    ctx.animEntries          = &st.anim.animEntries;
    ctx.selectedAnimCombo    = &st.anim.selectedAnimCombo;
    ctx.fbo                  = &st.scene.fbo;
    ctx.camera               = &st.scene.camera;
    ctx.root                 = st.scene.root.get();
    ctx.fov                  = video.fov;
    ctx.bgColor              = st.settings.bgColor;
    ctx.drawGrid             = st.settings.drawGrid;
    ctx.getLoadedModel       = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.loadModel            = [&st](GameFile* f) { ModelLoader::loadModel(f, st); };
    ctx.handleViewportInput  = std::move(handleInput);
    return ctx;
}

AnimationPanel::DrawContext buildAnimCtx(AppState& st)
{
    AnimationPanel::DrawContext ctx;
    ctx.getLoadedModel        = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.animEntries           = &st.anim.animEntries;
    ctx.selectedAnimCombo     = &st.anim.selectedAnimCombo;
    ctx.animSpeed             = &st.anim.animSpeed;
    ctx.loopCount             = &st.anim.loopCount;
    ctx.lockAnims             = &st.anim.lockAnims;
    ctx.selectedSecondaryAnim = &st.anim.selectedSecondaryAnim;
    ctx.selectedMouthAnim     = &st.anim.selectedMouthAnim;
    ctx.mouthSpeed            = &st.anim.mouthSpeed;
    ctx.skinEntries           = &st.anim.skinEntries;
    ctx.selectedSkin          = &st.anim.selectedSkin;
    ctx.blpSkin[0]            = st.anim.blpSkin[0];
    ctx.blpSkin[1]            = st.anim.blpSkin[1];
    ctx.blpSkin[2]            = st.anim.blpSkin[2];
    ctx.applySkin             = [&st](WoWModel* m, int idx) { ModelLoader::applySkin(m, idx, st); };
    return ctx;
}

ViewportOptionsPanel::DrawContext buildViewportOptsCtx(AppState& st)
{
    ViewportOptionsPanel::DrawContext ctx;
    ctx.drawGrid       = &st.settings.drawGrid;
    ctx.bgColor        = &st.settings.bgColor;
    ctx.camera         = &st.scene.camera;
    ctx.getLoadedModel = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.geosetGroups   = &st.browsers.geosetGroups;
    ctx.pcrState       = &st.browsers.pcrState;
    ctx.selectedSkin   = &st.anim.selectedSkin;
    ctx.applySkin      = [&st](WoWModel* m, int idx) { ModelLoader::applySkin(m, idx, st); };
    ctx.resetCamera    = [&st]() {
        ModelLoader::resetCameraToModel(st.scene.camera, ModelLoader::getLoadedModel(st));
    };
    return ctx;
}

MountsPanel::DrawContext buildMountsCtx(AppState& st)
{
    MountsPanel::DrawContext ctx;
    ctx.isChar             = st.scene.isChar;
    ctx.isMounted          = st.scene.isMounted;
    ctx.mountList          = &st.browsers.mountList;
    ctx.creatureModelNames = &st.browsers.creatureModelNames;
    ctx.creatureModels     = &st.browsers.creatureModels;
    ctx.mountFiltered      = &st.browsers.mountFiltered;
    ctx.mountFilterDirty   = &st.browsers.mountFilterDirty;
    ctx.mountTab           = &st.browsers.mountTab;
    ctx.mountSearchBuf     = st.browsers.mountSearchBuf;
    ctx.mountSearchBufSize = sizeof(st.browsers.mountSearchBuf);
    ctx.getLoadedModel     = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.buildMountList     = [&st]() { ModelLoader::buildMountList(st); };
    ctx.rebuildMountFilter = [&st]() { ModelLoader::rebuildMountFilter(st); };
    ctx.mountCharacter     = [&st](int d, GameFile* f) { ModelLoader::mountCharacter(d, f, st); };
    ctx.dismountCharacter  = [&st]() { ModelLoader::dismountCharacter(st); };
    return ctx;
}

ItemSetsPanel::DrawContext buildItemSetsCtx(AppState& st)
{
    ItemSetsPanel::DrawContext ctx;
    ctx.isChar                   = st.scene.isChar;
    ctx.itemSets                 = &st.browsers.itemSets;
    ctx.itemSetsBuilt            = &st.browsers.itemSetsBuilt;
    ctx.itemSetSearchBuf         = st.browsers.itemSetSearchBuf;
    ctx.itemSetSearchBufSize     = sizeof(st.browsers.itemSetSearchBuf);
    ctx.itemSetFiltered          = &st.browsers.itemSetFiltered;
    ctx.itemSetFilterDirty       = &st.browsers.itemSetFilterDirty;
    ctx.startOutfits             = &st.browsers.startOutfits;
    ctx.startOutfitsBuilt        = &st.browsers.startOutfitsBuilt;
    ctx.startOutfitSearchBuf     = st.browsers.startOutfitSearchBuf;
    ctx.startOutfitSearchBufSize = sizeof(st.browsers.startOutfitSearchBuf);
    ctx.startOutfitFiltered      = &st.browsers.startOutfitFiltered;
    ctx.startOutfitFilterDirty   = &st.browsers.startOutfitFilterDirty;
    ctx.getLoadedModel           = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.buildItemSets            = [&st]() { ModelLoader::buildItemSets(st); };
    ctx.rebuildItemSetFilter     = [&st]() { ModelLoader::rebuildItemSetFilter(st); };
    ctx.applyItemSet             = [&st](WoWModel* m, int id) { ModelLoader::applyItemSet(m, id, st); };
    ctx.buildStartOutfits        = [&st](WoWModel* m) { ModelLoader::buildStartOutfits(m, st); };
    ctx.rebuildStartOutfitFilter = [&st]() { ModelLoader::rebuildStartOutfitFilter(st); };
    ctx.applyStartOutfit         = [&st](WoWModel* m, int id) { ModelLoader::applyStartOutfit(m, id, st); };
    return ctx;
}

NpcBrowserPanel::DrawContext buildNpcBrowserCtx(AppState& st)
{
    NpcBrowserPanel::DrawContext ctx;
    ctx.isWoWLoaded      = st.loading.isWoWLoaded;
    ctx.isDBReady        = st.loading.initDB;
    ctx.npcs             = &npcs;
    ctx.npcFiltered      = &st.browsers.npcFiltered;
    ctx.npcFilterDirty   = &st.browsers.npcFilterDirty;
    ctx.npcSearchBuf     = st.browsers.npcSearchBuf;
    ctx.npcSearchBufSize = sizeof(st.browsers.npcSearchBuf);
    ctx.rebuildNpcFilter = [&st]() { ModelLoader::rebuildNpcFilter(st); };
    ctx.loadNPC          = [&st](unsigned int id) { ModelLoader::loadNPC(id, st); };
    return ctx;
}

ItemBrowserPanel::DrawContext buildItemBrowserCtx(AppState& st)
{
    ItemBrowserPanel::DrawContext ctx;
    ctx.isWoWLoaded             = st.loading.isWoWLoaded;
    ctx.isDBReady               = st.loading.initDB;
    ctx.items                   = &items;
    ctx.itemBrowseFiltered      = &st.browsers.itemBrowseFiltered;
    ctx.itemBrowseFilterDirty   = &st.browsers.itemBrowseFilterDirty;
    ctx.itemBrowseSearchBuf     = st.browsers.itemBrowseSearchBuf;
    ctx.itemBrowseSearchBufSize = sizeof(st.browsers.itemBrowseSearchBuf);
    ctx.rebuildItemBrowseFilter = [&st]() { ModelLoader::rebuildItemBrowseFilter(st); };
    ctx.loadItemModel           = [&st](unsigned int id) { ModelLoader::loadItemModel(id, st); };
    return ctx;
}

ExportPanel::DrawContext buildExportCtx(AppState& st)
{
    ExportPanel::DrawContext ctx;
    ctx.getLoadedModel    = [&st]() { return ModelLoader::getLoadedModel(st); };
    ctx.exporters         = &st.exporting.exporters;
    ctx.selectedExporter  = &st.exporting.selectedExporter;
    ctx.animEntries       = &st.anim.animEntries;
    ctx.exportAnimChecked = &st.exporting.exportAnimChecked;
    ctx.selectedAnimCombo = &st.anim.selectedAnimCombo;
    ctx.exportPath        = st.exporting.exportPath;
    ctx.exportPathSize    = sizeof(st.exporting.exportPath);
    ctx.exportStatus      = &st.exporting.exportStatus;
    return ctx;
}

ScreenshotPanel::DrawContext buildScreenshotCtx(AppState& st)
{
    ScreenshotPanel::DrawContext ctx;
    ctx.screenshotPath     = st.exporting.screenshotPath;
    ctx.screenshotPathSize = sizeof(st.exporting.screenshotPath);
    ctx.screenshotStatus   = &st.exporting.screenshotStatus;
    ctx.useCanvasOverride  = &st.exporting.useCanvasOverride;
    ctx.canvasWidth        = &st.exporting.canvasWidth;
    ctx.canvasHeight       = &st.exporting.canvasHeight;
    ctx.fbo                = &st.scene.fbo;
    ctx.camera             = &st.scene.camera;
    ctx.root               = st.scene.root.get();
    ctx.fov                = video.fov;
    ctx.bgColor            = st.settings.bgColor;
    ctx.drawGrid           = st.settings.drawGrid;
    return ctx;
}

PresetsPanel::DrawContext buildPresetsCtx(AppState& st)
{
    PresetsPanel::DrawContext ctx;
    ctx.presetPath     = st.exporting.presetPath;
    ctx.presetPathSize = sizeof(st.exporting.presetPath);
    ctx.presetStatus   = &st.exporting.presetStatus;
    ctx.isChar         = st.scene.isChar;
    ctx.hasModel       = ModelLoader::getLoadedModel(st) != nullptr;
    ctx.savePreset     = [&st](const char* p) { PresetManager::save(p, st); };
    ctx.loadPreset     = [&st](const char* p) { PresetManager::load(p, st); };
    return ctx;
}

LogPanel::DrawContext buildLogCtx(AppState& st)
{
    LogPanel::DrawContext ctx;
    ctx.logLines       = &st.ui.logLines;
    ctx.logAutoScroll  = &st.ui.logAutoScroll;
    ctx.logNeedsReload = &st.ui.logNeedsReload;
    return ctx;
}

SettingsPanel::DrawContext buildSettingsCtx(AppState& st, bool* showDemoWindow)
{
    SettingsPanel::DrawContext ctx;
    ctx.pathBuf                  = st.loading.pathBuf;
    ctx.pathBufSize              = sizeof(st.loading.pathBuf);
    ctx.isWoWLoaded              = st.loading.isWoWLoaded;
    ctx.loadInProgress           = st.loading.loadInProgress;
    ctx.loadProgress             = &st.loading.loadProgress;
    ctx.showFolderPicker         = &st.ui.showFolderPicker;
    ctx.folderPickerCurrent      = &st.ui.folderPickerCurrent;
    ctx.folderPickerEntries      = &st.ui.folderPickerEntries;
    ctx.folderPickerNeedsRefresh = &st.ui.folderPickerNeedsRefresh;
    ctx.settings                 = &st.settings;
    ctx.availableFonts           = &st.ui.availableFonts;
    ctx.fontsDirty               = &st.ui.fontsDirty;
    ctx.window                   = st.window;
    ctx.showDemoWindow           = showDemoWindow;
    ctx.camera                   = &st.scene.camera;
    ctx.getLoadStatus            = [&st]() { return GameLoader::getLoadStatus(st); };
    return ctx;
}

} // anonymous namespace

// ============================================================================
// run() — single entry point called from main()
// ============================================================================

Application::~Application() = default;

int Application::run()
{
    if (!init())
        return 1;

    mainLoop();
    shutdown();
    return 0;
}

// ============================================================================
// Lifecycle phases
// ============================================================================

bool Application::init()
{
    // ---- Platform window (GLFW + glad) ----
    if (!m_window.init(1600, 900, "WoW Model Viewer"))
        return false;

    m_window.setIcon(WMV_ICON_PATH);
    m_state.window = m_window.handle();

    // ---- Custom title bar (embed menus in the window frame) ----
    CustomTitleBar::init(m_window.handle());

    // ---- Engine init ----
    initEngine();
    initGL();

    // Create root attachment (scene graph root — no model yet)
    m_state.scene.root = std::make_unique<Attachment>(nullptr, nullptr, -1, -1);

    // ---- Dear ImGui ----
    m_state.ui.dpiScale = m_window.queryDpiScale();
    m_imguiLayer.init(m_window.handle(), m_state.ui.dpiScale);
    m_imguiLayer.discoverFonts(m_state.ui.availableFonts, m_state.settings.currentFont);
    m_imguiLayer.buildFontAtlas(m_state.ui.availableFonts, m_state.settings.currentFont,
                                m_state.settings.fontSize, m_state.ui.dpiScale);

    m_state.scene.lastTick = std::chrono::steady_clock::now();
    m_inputManager.loadDefaults();

    return true;
}

void Application::mainLoop()
{
    while (!m_window.shouldClose())
    {
        m_window.pollEvents();

        // ---- Rebuild font atlas if font/size changed ----
        m_imguiLayer.rebuildFontAtlasIfDirty(m_state.ui.fontsDirty, m_state.ui.availableFonts,
                                             m_state.settings.currentFont, m_state.settings.fontSize,
                                             m_state.ui.dpiScale);

        // Check if background loading thread has finished
        GameLoader::pollAsyncLoad(m_state);

        // Animation tick
        tickScene();

        // ---- ImGui frame ----
        m_imguiLayer.beginFrame();

        // ---- Resolve input bindings for this frame ----
        m_inputManager.update();

        // ---- Draw UI ----
        drawTitleBarAndMenus();
        drawDockspace();
        drawPanels();
        drawDialogs();

        // ---- Finalise frame ----
        m_imguiLayer.endFrame(m_window);
        m_window.swapBuffers();
    }
}

void Application::shutdown()
{
    if (m_state.loading.loadThread.joinable())
        m_state.loading.loadThread.join();

    FileBrowserPanel::shutdown();

    m_state.scene.root.reset();
    m_state.scene.fbo.destroy();

    SceneRenderer::shutdown();

    m_state.exporting.exporters.clear();
    m_state.exporting.importers.clear();

    m_imguiLayer.shutdown();

    // AppWindow destructor handles glfwDestroyWindow + glfwTerminate

    LOG_INFO << "WoW Model Viewer shutdown complete.";
}

// ============================================================================
// Engine / GL initialisation
// ============================================================================

void Application::initEngine()
{
    GLOBALSETTINGS.bShowParticle = true;
    GLOBALSETTINGS.bZeroParticle = true;

#ifdef _WIN32
    CreateDirectoryA("userSettings", nullptr);
#endif

    LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log_imgui.txt"));
    LOGGER.addChild(new WMVLog::LogOutputConsole());

    LOG_INFO << "==============================================";
    LOG_INFO << "Starting:" << GLOBALSETTINGS.appName()
             << GLOBALSETTINGS.appVersion()
             << GLOBALSETTINGS.buildName();
    LOG_INFO << "==============================================";

    m_state.settings.load();

#ifdef _WIN32
    if (HWND hConsole = GetConsoleWindow())
        ShowWindow(hConsole, m_state.settings.showConsole ? SW_SHOW : SW_HIDE);
#endif

    strncpy_s(m_state.loading.pathBuf, m_state.settings.gamePath.c_str(),
              sizeof(m_state.loading.pathBuf) - 1);

    m_state.exporting.exporters.push_back(std::make_unique<OBJExporter>());
    m_state.exporting.exporters.push_back(std::make_unique<FBXExporter>());

    m_state.exporting.importers.push_back(std::make_unique<ArmoryImporter>());
    m_state.exporting.importers.push_back(std::make_unique<WowheadImporter>());
}

void Application::initGL()
{
    video.render = true;
    video.InitGL();
    SceneRenderer::initResources();
    LOG_INFO << "OpenGL initialisation complete.";
}

// ============================================================================
// Per-frame helpers
// ============================================================================

void Application::tickScene()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - m_state.scene.lastTick).count();
    m_state.scene.lastTick = now;

    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    m_state.scene.fpsAccum += dt;
    m_state.scene.fpsFrameCount++;
    if (m_state.scene.fpsAccum >= 0.5f)
    {
        m_state.scene.fps = static_cast<float>(m_state.scene.fpsFrameCount) / m_state.scene.fpsAccum;
        m_state.scene.fpsFrameCount = 0;
        m_state.scene.fpsAccum = 0.0f;
    }

    m_state.scene.animTime += dt;

    if (m_state.scene.root)
        m_state.scene.root->tick(dt * 1000.0f);
}

void Application::handleViewportInput()
{
    ViewportController::apply(m_inputManager.state(), m_state.scene.camera);
    if (m_inputManager.state().resetCamera)
        ModelLoader::resetCameraToModel(m_state.scene.camera,
                                        ModelLoader::getLoadedModel(m_state));
}

// ============================================================================
// UI drawing
// ============================================================================

void Application::drawTitleBarAndMenus()
{
    GLFWwindow* window = m_window.handle();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
    if (CustomTitleBar::begin(window))
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load WoW", nullptr, false,
                                !m_state.loading.isWoWLoaded && !m_state.loading.loadInProgress))
                GameLoader::beginLoadWoW(m_state);
            ImGui::Separator();
            if (ImGui::MenuItem("Import from URL...", nullptr, false,
                                m_state.loading.isWoWLoaded && m_state.loading.initDB))
            {
                m_state.ui.showImportDialog = true;
                m_state.ui.importPopupJustOpened = true;
                m_state.exporting.importStatus.clear();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Model", nullptr, false,
                                ModelLoader::getLoadedModel(m_state) != nullptr))
                ModelLoader::clearModel(m_state);
            ImGui::Separator();
            if (ImGui::MenuItem("Export...", nullptr, false,
                                ModelLoader::getLoadedModel(m_state) != nullptr))
            {
                m_state.ui.showExport = true;
                ImGui::SetWindowFocus("Export");
            }
            if (ImGui::MenuItem("Screenshot...", "Ctrl+S"))
            {
                m_state.ui.showScreenshot = true;
                ImGui::SetWindowFocus("Screenshot");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                m_window.requestClose();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Reset Camera", "Numpad 5", false,
                                ModelLoader::getLoadedModel(m_state) != nullptr))
                ModelLoader::resetCameraToModel(m_state.scene.camera,
                                                ModelLoader::getLoadedModel(m_state));
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
                m_resetLayout = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("3D Viewport",      nullptr, &m_state.ui.showViewport);
            ImGui::MenuItem("Character Viewer",  nullptr, &m_state.ui.showCharViewer);
            ImGui::MenuItem("File Browser",      nullptr, &m_state.ui.showFileBrowser);
            ImGui::MenuItem("Animation",         nullptr, &m_state.ui.showAnimation);
            ImGui::MenuItem("Viewport Options",  nullptr, &m_state.ui.showViewportOpts);
            ImGui::MenuItem("Mounts",            nullptr, &m_state.ui.showMounts);
            ImGui::MenuItem("Item Sets",         nullptr, &m_state.ui.showItemSets);
            ImGui::MenuItem("NPC Browser",       nullptr, &m_state.ui.showNpcBrowser);
            ImGui::MenuItem("Item Browser",      nullptr, &m_state.ui.showItemBrowser);
            ImGui::MenuItem("Export",            nullptr, &m_state.ui.showExport);
            ImGui::MenuItem("Screenshot",        nullptr, &m_state.ui.showScreenshot);
            ImGui::MenuItem("Presets",           nullptr, &m_state.ui.showPresets);
            ImGui::MenuItem("Log",               nullptr, &m_state.ui.showLog);
            ImGui::MenuItem("Settings",          nullptr, &m_state.ui.showSettings);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo",        nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Options"))
        {
            if (ImGui::MenuItem("Language / Locale..."))
                m_state.ui.showLanguageDialog = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Settings..."))
                m_state.ui.showSettings = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About..."))
                m_state.ui.showAboutDialog = true;
            ImGui::EndMenu();
        }

        // ---- Status bar + window controls ----
        std::string statusText;
        {
            WoWModel* sm = ModelLoader::getLoadedModel(m_state);
            if (sm)
            {
                int curFrame = 0, totalFrames = 0;
                if (sm->animManager)
                {
                    curFrame   = static_cast<int>(sm->animManager->GetFrame());
                    totalFrames = static_cast<int>(sm->animManager->GetFrameCount());
                }
                statusText = std::format("FPS: {:.0f} | {} | V:{} B:{} T:{} | Frame: {}/{}",
                    m_state.scene.fps, sm->name(),
                    sm->header.nVertices, sm->header.nBones, sm->header.nTextures,
                    curFrame, totalFrames);
            }
            else
            {
                statusText = std::format("FPS: {:.0f}", m_state.scene.fps);
            }
        }
        CustomTitleBar::end(window, statusText.c_str());
    }
    ImGui::PopStyleVar(2);
}

void Application::drawDockspace()
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

    if (m_firstFrame || m_resetLayout)
    {
        m_firstFrame = false;
        if (m_resetLayout || !std::filesystem::exists(AppSettings::imguiIniPath))
        {
            m_resetLayout = false;
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

            int fw, fh;
            m_window.framebufferSize(fw, fh);
            ImGui::DockBuilderSetNodeSize(dockspace_id,
                ImVec2(static_cast<float>(fw), static_cast<float>(fh)));

            ImGuiID dock_left, dock_center;
            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.15f, &dock_left, &dock_center);

            ImGuiID dock_right;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.20f, &dock_right, &dock_center);

            ImGuiID dock_bottom;
            ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.25f, &dock_bottom, &dock_center);

            ImGui::DockBuilderDockWindow("File Browser",      dock_left);
            ImGui::DockBuilderDockWindow("NPC Browser",       dock_left);
            ImGui::DockBuilderDockWindow("Item Browser",      dock_left);
            ImGui::DockBuilderDockWindow("3D Viewport",       dock_center);
            ImGui::DockBuilderDockWindow("Character Viewer",  dock_center);
            ImGui::DockBuilderDockWindow("Animation",         dock_bottom);
            ImGui::DockBuilderDockWindow("Screenshot",        dock_bottom);
            ImGui::DockBuilderDockWindow("Export",            dock_bottom);
            ImGui::DockBuilderDockWindow("Presets",           dock_bottom);
            ImGui::DockBuilderDockWindow("Viewport Options",  dock_right);
            ImGui::DockBuilderDockWindow("Mounts",            dock_right);
            ImGui::DockBuilderDockWindow("Item Sets",         dock_right);
            ImGui::DockBuilderDockWindow("Log",               dock_bottom);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }
}

void Application::drawPanels()
{
    auto& st = m_state;

    // ===== Character Viewer =====
    if (st.ui.showCharViewer)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::Begin("Character Viewer", &st.ui.showCharViewer))
        {
            auto ctx = buildCharViewerCtx(st, [this]() { handleViewportInput(); });
            CharacterViewerPanel::draw(ctx);
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ===== 3D Viewport =====
    if (st.ui.showViewport)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("3D Viewport", &st.ui.showViewport))
        {
            ImVec2 panelSize = ImGui::GetContentRegionAvail();
            int vpW = static_cast<int>(panelSize.x);
            int vpH = static_cast<int>(panelSize.y);
            if (vpW > 0 && vpH > 0)
            {
                SceneRenderer::renderToFBO(st.scene.fbo, vpW, vpH, st.scene.camera,
                                           st.scene.root.get(), video.fov,
                                           st.settings.bgColor, st.settings.drawGrid);
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(st.scene.fbo.colorTex)),
                             panelSize, ImVec2(0, 1), ImVec2(1, 0));
                if (ImGui::IsItemHovered())
                    handleViewportInput();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ===== File Browser =====
    if (st.ui.showFileBrowser)
    {
        auto statusStr = GameLoader::getLoadStatus(st);
        FileBrowserPanel::LoadState ls;
        ls.isLoaded   = st.loading.isWoWLoaded;
        ls.inProgress = st.loading.loadInProgress;
        ls.progress   = st.loading.loadProgress;
        ls.statusText = statusStr.c_str();
        if (GameFile* picked = FileBrowserPanel::draw(st.ui.showFileBrowser, ls))
            ModelLoader::loadModel(picked, st);
    }

    // ===== Animation Control =====
    if (st.ui.showAnimation)
    {
        if (ImGui::Begin("Animation", &st.ui.showAnimation))
        {
            auto ctx = buildAnimCtx(st);
            AnimationPanel::draw(ctx);
            st.anim.blpSkin[0] = ctx.blpSkin[0];
            st.anim.blpSkin[1] = ctx.blpSkin[1];
            st.anim.blpSkin[2] = ctx.blpSkin[2];
        }
        ImGui::End();
    }

    // ===== Viewport Options =====
    if (st.ui.showViewportOpts)
    {
        if (ImGui::Begin("Viewport Options", &st.ui.showViewportOpts))
        {
            auto ctx = buildViewportOptsCtx(st);
            ViewportOptionsPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Mounts =====
    if (st.ui.showMounts)
    {
        if (ImGui::Begin("Mounts", &st.ui.showMounts))
        {
            auto ctx = buildMountsCtx(st);
            MountsPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Item Sets =====
    if (st.ui.showItemSets)
    {
        if (ImGui::Begin("Item Sets", &st.ui.showItemSets))
        {
            auto ctx = buildItemSetsCtx(st);
            ItemSetsPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== NPC Browser =====
    if (st.ui.showNpcBrowser)
    {
        if (ImGui::Begin("NPC Browser", &st.ui.showNpcBrowser))
        {
            auto ctx = buildNpcBrowserCtx(st);
            NpcBrowserPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Item Browser =====
    if (st.ui.showItemBrowser)
    {
        if (ImGui::Begin("Item Browser", &st.ui.showItemBrowser))
        {
            auto ctx = buildItemBrowserCtx(st);
            ItemBrowserPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Export =====
    if (st.ui.showExport)
    {
        if (ImGui::Begin("Export", &st.ui.showExport))
        {
            auto ctx = buildExportCtx(st);
            ExportPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Screenshot =====
    if (st.ui.showScreenshot)
    {
        if (ImGui::Begin("Screenshot", &st.ui.showScreenshot))
        {
            auto ctx = buildScreenshotCtx(st);
            ScreenshotPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Presets =====
    if (st.ui.showPresets)
    {
        if (ImGui::Begin("Presets", &st.ui.showPresets))
        {
            auto ctx = buildPresetsCtx(st);
            PresetsPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Log =====
    if (st.ui.showLog)
    {
        if (ImGui::Begin("Log", &st.ui.showLog))
        {
            auto ctx = buildLogCtx(st);
            LogPanel::draw(ctx);
        }
        ImGui::End();
    }

    // ===== Settings =====
    if (st.ui.showSettings)
    {
        ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Settings", &st.ui.showSettings, ImGuiWindowFlags_NoDocking))
        {
            auto ctx = buildSettingsCtx(st, &m_showDemoWindow);
            SettingsPanel::draw(ctx);
        }
        ImGui::End();
    }
}

void Application::drawDialogs()
{
    AppDialogs::drawImportDialog(m_state);
    AppDialogs::drawConfigPopup(m_state);

    if (m_showDemoWindow)
        ImGui::ShowDemoWindow(&m_showDemoWindow);

    AppDialogs::drawAboutDialog(m_state);
    AppDialogs::drawLanguageDialog(m_state);
}
