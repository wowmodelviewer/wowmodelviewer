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
    auto& st = m_state; // shorthand

    // ===== Character Viewer =====
    if (st.ui.showCharViewer)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        if (ImGui::Begin("Character Viewer", &st.ui.showCharViewer))
        {
            CharacterViewerPanel::DrawContext cvCtx;
            cvCtx.isWoWLoaded          = st.loading.isWoWLoaded;
            cvCtx.isDBReady            = st.loading.initDB;
            cvCtx.isChar               = st.scene.isChar;
            cvCtx.customizationOptions = &st.character.customizationOptions;
            cvCtx.animEntries          = &st.anim.animEntries;
            cvCtx.selectedAnimCombo    = &st.anim.selectedAnimCombo;
            cvCtx.fbo                  = &st.scene.fbo;
            cvCtx.camera               = &st.scene.camera;
            cvCtx.root                 = st.scene.root.get();
            cvCtx.fov                  = video.fov;
            cvCtx.bgColor              = st.settings.bgColor;
            cvCtx.drawGrid             = st.settings.drawGrid;
            cvCtx.getLoadedModel       = [&]() { return ModelLoader::getLoadedModel(st); };
            cvCtx.loadModel            = [&](GameFile* f) { ModelLoader::loadModel(f, st); };
            cvCtx.handleViewportInput  = [this]() { handleViewportInput(); };
            CharacterViewerPanel::draw(cvCtx);
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
            AnimationPanel::DrawContext animCtx;
            animCtx.getLoadedModel        = [&]() { return ModelLoader::getLoadedModel(st); };
            animCtx.animEntries           = &st.anim.animEntries;
            animCtx.selectedAnimCombo     = &st.anim.selectedAnimCombo;
            animCtx.animSpeed             = &st.anim.animSpeed;
            animCtx.loopCount             = &st.anim.loopCount;
            animCtx.lockAnims             = &st.anim.lockAnims;
            animCtx.selectedSecondaryAnim = &st.anim.selectedSecondaryAnim;
            animCtx.selectedMouthAnim     = &st.anim.selectedMouthAnim;
            animCtx.mouthSpeed            = &st.anim.mouthSpeed;
            animCtx.skinEntries           = &st.anim.skinEntries;
            animCtx.selectedSkin          = &st.anim.selectedSkin;
            animCtx.blpSkin[0] = st.anim.blpSkin[0];
            animCtx.blpSkin[1] = st.anim.blpSkin[1];
            animCtx.blpSkin[2] = st.anim.blpSkin[2];
            animCtx.applySkin  = [&](WoWModel* m, int idx) { ModelLoader::applySkin(m, idx, st); };
            AnimationPanel::draw(animCtx);
            st.anim.blpSkin[0] = animCtx.blpSkin[0];
            st.anim.blpSkin[1] = animCtx.blpSkin[1];
            st.anim.blpSkin[2] = animCtx.blpSkin[2];
        }
        ImGui::End();
    }

    // ===== Viewport Options =====
    if (st.ui.showViewportOpts)
    {
        if (ImGui::Begin("Viewport Options", &st.ui.showViewportOpts))
        {
            ViewportOptionsPanel::DrawContext vpCtx;
            vpCtx.drawGrid       = &st.settings.drawGrid;
            vpCtx.bgColor        = &st.settings.bgColor;
            vpCtx.camera         = &st.scene.camera;
            vpCtx.getLoadedModel = [&]() { return ModelLoader::getLoadedModel(st); };
            vpCtx.geosetGroups   = &st.browsers.geosetGroups;
            vpCtx.pcrState       = &st.browsers.pcrState;
            vpCtx.selectedSkin   = &st.anim.selectedSkin;
            vpCtx.applySkin      = [&](WoWModel* m, int idx) { ModelLoader::applySkin(m, idx, st); };
            vpCtx.resetCamera    = [&]() {
                ModelLoader::resetCameraToModel(st.scene.camera, ModelLoader::getLoadedModel(st));
            };
            ViewportOptionsPanel::draw(vpCtx);
        }
        ImGui::End();
    }

    // ===== Mounts =====
    if (st.ui.showMounts)
    {
        if (ImGui::Begin("Mounts", &st.ui.showMounts))
        {
            MountsPanel::DrawContext mCtx;
            mCtx.isChar              = st.scene.isChar;
            mCtx.isMounted           = st.scene.isMounted;
            mCtx.mountList           = &st.browsers.mountList;
            mCtx.creatureModelNames  = &st.browsers.creatureModelNames;
            mCtx.creatureModels      = &st.browsers.creatureModels;
            mCtx.mountFiltered       = &st.browsers.mountFiltered;
            mCtx.mountFilterDirty    = &st.browsers.mountFilterDirty;
            mCtx.mountTab            = &st.browsers.mountTab;
            mCtx.mountSearchBuf      = st.browsers.mountSearchBuf;
            mCtx.mountSearchBufSize  = sizeof(st.browsers.mountSearchBuf);
            mCtx.getLoadedModel      = [&]() { return ModelLoader::getLoadedModel(st); };
            mCtx.buildMountList      = [&]() { ModelLoader::buildMountList(st); };
            mCtx.rebuildMountFilter  = [&]() { ModelLoader::rebuildMountFilter(st); };
            mCtx.mountCharacter      = [&](int d, GameFile* f) { ModelLoader::mountCharacter(d, f, st); };
            mCtx.dismountCharacter   = [&]() { ModelLoader::dismountCharacter(st); };
            MountsPanel::draw(mCtx);
        }
        ImGui::End();
    }

    // ===== Item Sets =====
    if (st.ui.showItemSets)
    {
        if (ImGui::Begin("Item Sets", &st.ui.showItemSets))
        {
            ItemSetsPanel::DrawContext isCtx;
            isCtx.isChar                   = st.scene.isChar;
            isCtx.itemSets                 = &st.browsers.itemSets;
            isCtx.itemSetsBuilt            = &st.browsers.itemSetsBuilt;
            isCtx.itemSetSearchBuf         = st.browsers.itemSetSearchBuf;
            isCtx.itemSetSearchBufSize     = sizeof(st.browsers.itemSetSearchBuf);
            isCtx.itemSetFiltered          = &st.browsers.itemSetFiltered;
            isCtx.itemSetFilterDirty       = &st.browsers.itemSetFilterDirty;
            isCtx.startOutfits             = &st.browsers.startOutfits;
            isCtx.startOutfitsBuilt        = &st.browsers.startOutfitsBuilt;
            isCtx.startOutfitSearchBuf     = st.browsers.startOutfitSearchBuf;
            isCtx.startOutfitSearchBufSize = sizeof(st.browsers.startOutfitSearchBuf);
            isCtx.startOutfitFiltered      = &st.browsers.startOutfitFiltered;
            isCtx.startOutfitFilterDirty   = &st.browsers.startOutfitFilterDirty;
            isCtx.getLoadedModel           = [&]() { return ModelLoader::getLoadedModel(st); };
            isCtx.buildItemSets            = [&]() { ModelLoader::buildItemSets(st); };
            isCtx.rebuildItemSetFilter     = [&]() { ModelLoader::rebuildItemSetFilter(st); };
            isCtx.applyItemSet             = [&](WoWModel* m, int id) { ModelLoader::applyItemSet(m, id, st); };
            isCtx.buildStartOutfits        = [&](WoWModel* m) { ModelLoader::buildStartOutfits(m, st); };
            isCtx.rebuildStartOutfitFilter = [&]() { ModelLoader::rebuildStartOutfitFilter(st); };
            isCtx.applyStartOutfit         = [&](WoWModel* m, int id) { ModelLoader::applyStartOutfit(m, id, st); };
            ItemSetsPanel::draw(isCtx);
        }
        ImGui::End();
    }

    // ===== NPC Browser =====
    if (st.ui.showNpcBrowser)
    {
        if (ImGui::Begin("NPC Browser", &st.ui.showNpcBrowser))
        {
            NpcBrowserPanel::DrawContext npcCtx;
            npcCtx.isWoWLoaded      = st.loading.isWoWLoaded;
            npcCtx.isDBReady        = st.loading.initDB;
            npcCtx.npcs             = &npcs;
            npcCtx.npcFiltered      = &st.browsers.npcFiltered;
            npcCtx.npcFilterDirty   = &st.browsers.npcFilterDirty;
            npcCtx.npcSearchBuf     = st.browsers.npcSearchBuf;
            npcCtx.npcSearchBufSize = sizeof(st.browsers.npcSearchBuf);
            npcCtx.rebuildNpcFilter = [&]() { ModelLoader::rebuildNpcFilter(st); };
            npcCtx.loadNPC          = [&](unsigned int id) { ModelLoader::loadNPC(id, st); };
            NpcBrowserPanel::draw(npcCtx);
        }
        ImGui::End();
    }

    // ===== Item Browser =====
    if (st.ui.showItemBrowser)
    {
        if (ImGui::Begin("Item Browser", &st.ui.showItemBrowser))
        {
            ItemBrowserPanel::DrawContext ibCtx;
            ibCtx.isWoWLoaded             = st.loading.isWoWLoaded;
            ibCtx.isDBReady               = st.loading.initDB;
            ibCtx.items                   = &items;
            ibCtx.itemBrowseFiltered      = &st.browsers.itemBrowseFiltered;
            ibCtx.itemBrowseFilterDirty   = &st.browsers.itemBrowseFilterDirty;
            ibCtx.itemBrowseSearchBuf     = st.browsers.itemBrowseSearchBuf;
            ibCtx.itemBrowseSearchBufSize = sizeof(st.browsers.itemBrowseSearchBuf);
            ibCtx.rebuildItemBrowseFilter = [&]() { ModelLoader::rebuildItemBrowseFilter(st); };
            ibCtx.loadItemModel           = [&](unsigned int id) { ModelLoader::loadItemModel(id, st); };
            ItemBrowserPanel::draw(ibCtx);
        }
        ImGui::End();
    }

    // ===== Export =====
    if (st.ui.showExport)
    {
        if (ImGui::Begin("Export", &st.ui.showExport))
        {
            ExportPanel::DrawContext exCtx;
            exCtx.getLoadedModel    = [&]() { return ModelLoader::getLoadedModel(st); };
            exCtx.exporters         = &st.exporting.exporters;
            exCtx.selectedExporter  = &st.exporting.selectedExporter;
            exCtx.animEntries       = &st.anim.animEntries;
            exCtx.exportAnimChecked = &st.exporting.exportAnimChecked;
            exCtx.selectedAnimCombo = &st.anim.selectedAnimCombo;
            exCtx.exportPath        = st.exporting.exportPath;
            exCtx.exportPathSize    = sizeof(st.exporting.exportPath);
            exCtx.exportStatus      = &st.exporting.exportStatus;
            ExportPanel::draw(exCtx);
        }
        ImGui::End();
    }

    // ===== Screenshot =====
    if (st.ui.showScreenshot)
    {
        if (ImGui::Begin("Screenshot", &st.ui.showScreenshot))
        {
            ScreenshotPanel::DrawContext ssCtx;
            ssCtx.screenshotPath     = st.exporting.screenshotPath;
            ssCtx.screenshotPathSize = sizeof(st.exporting.screenshotPath);
            ssCtx.screenshotStatus   = &st.exporting.screenshotStatus;
            ssCtx.useCanvasOverride  = &st.exporting.useCanvasOverride;
            ssCtx.canvasWidth        = &st.exporting.canvasWidth;
            ssCtx.canvasHeight       = &st.exporting.canvasHeight;
            ssCtx.fbo                = &st.scene.fbo;
            ssCtx.camera             = &st.scene.camera;
            ssCtx.root               = st.scene.root.get();
            ssCtx.fov                = video.fov;
            ssCtx.bgColor            = st.settings.bgColor;
            ssCtx.drawGrid           = st.settings.drawGrid;
            ScreenshotPanel::draw(ssCtx);
        }
        ImGui::End();
    }

    // ===== Presets =====
    if (st.ui.showPresets)
    {
        if (ImGui::Begin("Presets", &st.ui.showPresets))
        {
            PresetsPanel::DrawContext preCtx;
            preCtx.presetPath     = st.exporting.presetPath;
            preCtx.presetPathSize = sizeof(st.exporting.presetPath);
            preCtx.presetStatus   = &st.exporting.presetStatus;
            preCtx.isChar         = st.scene.isChar;
            preCtx.hasModel       = ModelLoader::getLoadedModel(st) != nullptr;
            preCtx.savePreset     = [&](const char* p) { PresetManager::save(p, st); };
            preCtx.loadPreset     = [&](const char* p) { PresetManager::load(p, st); };
            PresetsPanel::draw(preCtx);
        }
        ImGui::End();
    }

    // ===== Log =====
    if (st.ui.showLog)
    {
        if (ImGui::Begin("Log", &st.ui.showLog))
        {
            LogPanel::DrawContext logCtx;
            logCtx.logLines       = &st.ui.logLines;
            logCtx.logAutoScroll  = &st.ui.logAutoScroll;
            logCtx.logNeedsReload = &st.ui.logNeedsReload;
            LogPanel::draw(logCtx);
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
            SettingsPanel::DrawContext settingsCtx;
            settingsCtx.pathBuf                  = st.loading.pathBuf;
            settingsCtx.pathBufSize              = sizeof(st.loading.pathBuf);
            settingsCtx.isWoWLoaded              = st.loading.isWoWLoaded;
            settingsCtx.loadInProgress           = st.loading.loadInProgress;
            settingsCtx.loadProgress             = &st.loading.loadProgress;
            settingsCtx.showFolderPicker         = &st.ui.showFolderPicker;
            settingsCtx.folderPickerCurrent      = &st.ui.folderPickerCurrent;
            settingsCtx.folderPickerEntries       = &st.ui.folderPickerEntries;
            settingsCtx.folderPickerNeedsRefresh = &st.ui.folderPickerNeedsRefresh;
            settingsCtx.settings                 = &st.settings;
            settingsCtx.availableFonts           = &st.ui.availableFonts;
            settingsCtx.fontsDirty               = &st.ui.fontsDirty;
            settingsCtx.window                   = st.window;
            settingsCtx.showDemoWindow           = &m_showDemoWindow;
            settingsCtx.camera                   = &st.scene.camera;
            settingsCtx.getLoadStatus            = [&]() { return GameLoader::getLoadStatus(st); };
            SettingsPanel::draw(settingsCtx);
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
