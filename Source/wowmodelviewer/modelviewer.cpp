#include "modelviewer.h"
#include <algorithm>
#include "ClientChoiceDialog.h"   // File > Load World of Warcraft opens it
#include "ClientInstallations.h"

#include "AnimationExportChoiceDialog.h"
#include "ArmoryImportDialog.h"
#include "TextureView.h"
#include "AnimManager.h"

#include <chrono>
#include <functional>

#include <wx/aboutdlg.h>
#include <wx/aui/auibar.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>
#include <wx/wupdlock.h>
#include <wx/evtloop.h>
#include <wx/numformatter.h>
#include <wx/srchctrl.h>
#include <wx/busyinfo.h>
#include <wx/dirdlg.h>
#include <wx/colour.h>
#include <wx/filedlg.h>
#include <wx/filename.h>

#include "Attachment.h"
#include "app.h"
#include "Bone.h"
#include "CASCFile.h"
#include "CASCFolder.h"
#include "CharInfos.h"
#include "ExporterPlugin.h"
#include "ExportJobManager.h"
#include "Game.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h> // wxExecute / wxGetProcessId for File > Restart
#include <wx/display.h>
#include "GlobalSettings.h"
#include "globalvars.h"
#include "ImporterPlugin.h"
#include "KeyboardShortcutsDialog.h"
#include "LoadingDialog.h"
#include "CharTexture.h"
#include "WotlkDbc.h"
#include <wx/richmsgdlg.h>
#include <QDir>
#include <QSettings>
#include <memory>
#include "MemoryUtils.h"
#include "ModelInspector.h"
#include "ModelRenderPass.h"
#include "NPCImporterDialog.h"
#include "PluginManager.h"
#include "RaceInfos.h"
#include "SettingsControl.h"
#include "BackgroundColorDialog.h"
#include "ModelIdLookup.h"
#include "ViewportBackground.h"
#include "UiArt.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "WinTheme.h"
#include "UnityAssetAccess.h"
#include "UnityCharacterScene.h"
#include "UnityIpcServer.h"
#include "UnityRendererHost.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"

#include "logger/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>
#include <QXmlStreamWriter>
#include <QDomDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>

#include <fstream>
#include <memory>



namespace
{
  // The frame's keyboard accelerators, in one table: InitMenu installs it, and Help > Keyboard
  // Shortcuts lists the entries no menu item already shows (description set). Menu shortcuts that
  // are also written in a menu label are listed from the menu bar itself.
  struct AppAccelerator
  {
    int flags;
    int key;
    int id;
    const wchar_t * keys;         // as shown to the user; null when a menu label already shows it
    const wchar_t * description;
    const wchar_t * where;
  };

  const AppAccelerator kAppAccelerators[] =
  {
    { wxACCEL_NORMAL, WXK_F5, ID_SAVE_EQUIPMENT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F6, ID_LOAD_EQUIPMENT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F7, ID_SAVE_CHAR, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F8, ID_LOAD_CHAR, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'X', ID_FILE_EXIT, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'e', ID_SHOW_EARS, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'h', ID_SHOW_HAIR, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'f', ID_SHOW_FACIALHAIR, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'z', ID_SHEATHE, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F9, ID_CLEAR_EQUIPMENT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F10, ID_CHAR_RANDOMISE, nullptr, nullptr, nullptr },
    // F11 is Fullscreen (View menu).
    //
    // The keys that only drove the OpenGL viewport are gone with it: F12 (screenshot), Ctrl+L
    // (background image), Ctrl+B (bounding box), Ctrl +/- (zoom, whose handler was already disabled)
    // and F1-F4 / Ctrl+F1-F4 (saved views). The Unity viewport has none of these yet.

    { wxACCEL_CTRL | wxACCEL_SHIFT, (int)'R', ID_RESTART, nullptr, nullptr, nullptr },
  };
}

// Class event handler/importer
IMPLEMENT_CLASS(ModelViewer, wxFrame)

BEGIN_EVENT_TABLE(ModelViewer, wxFrame)
  EVT_CHILD_FOCUS(ModelViewer::OnChildFocus)
  EVT_ACTIVATE(ModelViewer::OnActivateFrame)
  EVT_MENU_RANGE(ID_VIEW_APPEARANCE_SYSTEM, ID_VIEW_APPEARANCE_DARK, ModelViewer::OnAppearance)
  EVT_UPDATE_UI_RANGE(ID_VIEW_APPEARANCE_SYSTEM, ID_VIEW_APPEARANCE_DARK, ModelViewer::OnUpdateCommandUI)
  EVT_SYS_COLOUR_CHANGED(ModelViewer::OnSysColourChanged)
  EVT_TIMER(ID_THEME_RECHECK_TIMER, ModelViewer::OnThemeRecheck)
EVT_CLOSE(ModelViewer::OnClose)
//EVT_SIZE(ModelViewer::OnSize)

// File menu
EVT_MENU(ID_LOAD_WOW, ModelViewer::OnGameToggle)
EVT_MENU(ID_LOAD_MPQ, ModelViewer::OnLoadLegacyMpq)
EVT_UPDATE_UI(ID_LOAD_WOW, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_LOAD_MPQ, ModelViewer::OnUpdateCommandUI)
EVT_MENU(ID_FILE_VIEWLOG, ModelViewer::OnViewLog)
EVT_MENU(ID_VIEW_NPC, ModelViewer::OnCharToggle)
EVT_MENU(ID_VIEW_ITEM, ModelViewer::OnCharToggle)
// --
EVT_MENU(ID_FILE_MODEL_INFO, ModelViewer::OnExportOther)
//--
EVT_MENU(ID_FILE_RESETLAYOUT, ModelViewer::OnToggleCommand)
// --
EVT_MENU(ID_FILE_EXIT, ModelViewer::OnExit)
EVT_MENU(ID_RESTART, ModelViewer::OnRestart)

// view menu
EVT_MENU(ID_SHOW_FILE_LIST, ModelViewer::OnToggleDock)
EVT_MENU(ID_SHOW_ANIM, ModelViewer::OnToggleDock)
EVT_MENU(ID_SHOW_CHAR, ModelViewer::OnToggleDock)
EVT_MENU(ID_SHOW_MODEL, ModelViewer::OnToggleDock)
EVT_MENU(ID_VIEW_UNITY_RESTART, ModelViewer::OnRestartUnityRenderer)
EVT_MENU(ID_VIEW_BACKGROUND_COLOR, ModelViewer::OnBackgroundColor)
EVT_UPDATE_UI(ID_VIEW_BACKGROUND_COLOR, ModelViewer::OnUpdateCommandUI)
EVT_MENU(ID_VIEW_FULLSCREEN, ModelViewer::OnToggleFullScreen)
EVT_CHAR_HOOK(ModelViewer::OnCharHook)

// Command bar (and the panel toggles it shares with the View menu)
EVT_MENU(ID_UI_MODELS, ModelViewer::OnCommandBar)
EVT_UPDATE_UI(ID_UI_MODELS, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_UI_TEXTURES, ModelViewer::OnUpdateCommandUI)
EVT_MENU(ID_UI_TEXTURES, ModelViewer::OnCommandBar)
EVT_UPDATE_UI(ID_UI_BUILDINGS, ModelViewer::OnUpdateCommandUI)
EVT_MENU(ID_UI_BUILDINGS, ModelViewer::OnCommandBar)
EVT_AUI_PANE_CLOSE(ModelViewer::OnPaneClose)
EVT_MENU(ID_UI_SCREENSHOT, ModelViewer::OnCommandBar)
EVT_UPDATE_UI(ID_UI_SCREENSHOT, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_SHOW_FILE_LIST, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_SHOW_CHAR, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_SHOW_ANIM, ModelViewer::OnUpdateCommandUI)
// The other commands that act on a model: greyed in the Textures and Buildings viewers (needsModelViewer).
EVT_UPDATE_UI(ID_SHOW_MODEL, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_VIEW_NPC, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_VIEW_ITEM, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_LOAD_CHAR, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_IMPORT_CHAR, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_IMPORT_NPC, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_EXPORT_MODEL, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_FILE_MODEL_INFO, ModelViewer::OnUpdateCommandUI)
// (The OpenGL viewport's commands -- background, camera, canvas size, bounds, debug info, saved views,
// lighting -- were removed with it; their ModelCanvas implementations are archived, unreferenced.)

// Effects
EVT_MENU(ID_ENCHANTS, ModelViewer::OnEffects)

// Options
EVT_MENU(ID_SAVE_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_IMPORT_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_IMPORT_NPC, ModelViewer::OnImportNPCFromURL)

EVT_MENU(ID_SHOW_SETTINGS, ModelViewer::OnToggleDock)

// char controls:
EVT_MENU(ID_SAVE_EQUIPMENT, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_EQUIPMENT, ModelViewer::OnToggleCommand)
EVT_MENU(ID_CLEAR_EQUIPMENT, ModelViewer::OnSetEquipment)

EVT_MENU(ID_LOAD_SET, ModelViewer::OnSetEquipment)
EVT_MENU(ID_LOAD_START, ModelViewer::OnSetEquipment)

EVT_MENU(ID_SHOW_UNDERWEAR, ModelViewer::OnCharToggle)
EVT_MENU(ID_SHOW_EARS, ModelViewer::OnCharToggle)
EVT_MENU(ID_SHOW_HAIR, ModelViewer::OnCharToggle)
EVT_MENU(ID_SHOW_FACIALHAIR, ModelViewer::OnCharToggle)
EVT_MENU(ID_SHOW_FEET, ModelViewer::OnCharToggle)
EVT_MENU(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, ModelViewer::OnCharToggle)
EVT_MENU(ID_SHEATHE, ModelViewer::OnCharToggle)
EVT_MENU(ID_CHAREYEGLOW_NONE, ModelViewer::OnCharToggle)
EVT_MENU(ID_CHAREYEGLOW_DEFAULT, ModelViewer::OnCharToggle)
EVT_MENU(ID_CHAREYEGLOW_DEATHKNIGHT, ModelViewer::OnCharToggle)

EVT_MENU(ID_MOUNT_CHARACTER, ModelViewer::OnMount)
EVT_MENU(ID_CHAR_RANDOMISE, ModelViewer::OnSetEquipment)

// About menu
EVT_MENU(ID_LANGUAGE, ModelViewer::OnLanguage)
EVT_MENU(ID_HELP, ModelViewer::OnAbout)
EVT_MENU(ID_ABOUT, ModelViewer::OnAbout)
EVT_MENU(ID_KEYBOARD_SHORTCUTS, ModelViewer::OnKeyboardShortcuts)

// Export
EVT_MENU(ID_EXPORT_MODEL, ModelViewer::OnExport)

// refesh status bar timer
EVT_TIMER(ID_STATUS_REFRESH_TIMER, ModelViewer::OnStatusBarRefreshTimer)

END_EVENT_TABLE()

ModelViewer::ModelViewer()
#ifdef _LINUX
// Transparency in interfaceManager crashes with Linux compositing
: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_VENETIAN_BLINDS_HINT)
#else
// wxWidgets 3.2's default flags. 3.3's default adds live resizing: a sash drag would resize the Unity
// player's window at every mouse move, and wxAuiManager::Update would lay out an unfrozen frame, which
// the one-step viewer-mode switch (SetViewerMode) relies on it not doing. (It also drops the docking
// hint's fade-in, which these keep.)
: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_TRANSPARENT_HINT | wxAUI_MGR_HINT_FADE)
#endif
{
  m_themeRecheck.SetOwner(this, ID_THEME_RECHECK_TIMER);
  m_playerContentPoll.Bind(wxEVT_TIMER, &ModelViewer::OnPlayerContentPoll, this);
  PLUGINMANAGER.init("./plugins");
  // our main class objects
  animControl = nullptr;
  canvas = NULL;
  charControl = NULL;
  enchants = NULL;
  lightControl = NULL;
  modelControl = NULL;
  modelInspector = NULL;
  settingsControl = NULL;
  unityRendererHost = NULL;
  m_lastAnimStatePush = 0;
  fileControl = NULL;

  //wxWidget objects
  menuBar = NULL;
  charMenu = NULL;
  charGlowMenu = NULL;
  viewMenu = NULL;
  optMenu = NULL;
  exportMenu = NULL;
  fileMenu = NULL;

  isWoWLoaded = false;
  isModel = false;
  isWMO = false;
  isChar = false;
  initDB = false;

  //wxCAPTION|wxRESIZE_BORDER|wxSYSTEM_MENU
  // create our main frame
  if (Create(NULL, wxID_ANY, wxString(GLOBALSETTINGS.appTitle()), wxDefaultPosition, wxSize(1024, 768), wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN, wxT("ModelViewerFrame"))) 
  {
    SetIcon(wxICON(IDI_ICON1));
    SetExtraStyle(wxWS_EX_VALIDATE_RECURSIVELY);
#ifndef  _LINUX // buggy
    SetBackgroundStyle(wxBG_STYLE_CUSTOM);
#endif

    InitObjects();  // create our canvas, anim control, character control, etc

    // FBX exports run in a separate process so the FBX SDK can never freeze or crash WMV.
    m_exportJobManager = new ExportJobManager(this);

    // Show our window
    Show(false);
    // Display the window
    Centre();

    // ------
    // Initialise our main window.
    // Load session settings
    LoadSession();

    // create our menu objects
    InitMenu();

    // GUI and Canvas Stuff
    InitDocking();

    // Ensure that the docking windows are properly positioned (otherwise it starts with a mess of overlapping windows)
    interfaceManager.Update();

    // Are these really needed?
    Refresh();
    Update();

    /*
    // Set our display mode
    //if (video.GetCompatibleWinMode(video.curCap)) {
    video.SetMode();
    if (!video.render) // Something bad must have happened - find a new working display mode
    video.GetAvailableMode();
    } else {
    LOG_ERROR << "Failed to find a compatible graphics mode.  Finding first available display mode...";
    video.GetAvailableMode(); // Get first available display mode that supports the current desktop colour bitdepth
    }
    */

    LOG_INFO << "Setting OpenGL render state...";
    SetStatusText(wxT("Setting OpenGL render state..."));
    video.InitGL();

    SetStatusText(wxEmptyString);

    timer.SetOwner(this, ID_STATUS_REFRESH_TIMER);
    timer.Start(2000);
  }
  else 
  {
    LOG_FATAL << "Unable to create the main window for the application.";
    Close(true);
  }
}

void ModelViewer::InitMenu()
{
  LOG_INFO << "Initializing File Menu...";

  if (GetStatusBar() == NULL){
    CreateThemedStatusBar();
    SetStatusText(wxT("Initializing File Menu..."));
  }

  // MENU
  fileMenu = new wxMenu;
  fileMenu->Append(ID_LOAD_WOW, _("Load World of Warcraft"));
  fileMenu->Append(ID_LOAD_MPQ, _("Load Legacy MPQ Client..."));
  if (isWoWLoaded == true)
    fileMenu->Enable(ID_LOAD_WOW, false);
  fileMenu->Append(ID_FILE_VIEWLOG, _("View Log"));
  fileMenu->AppendSeparator();
  // No Save Screenshot or Export Image Sequence: both captured the OpenGL viewport, which is archived,
  // and the Unity viewport has no capture of its own yet.

  // --== Continue regular menu ==--

  // export menu
  wxMenu *ExportMenu = new wxMenu;
  ExportMenu->Append(ID_FILE_MODEL_INFO, wxT("Export ModelInfo.xml"));

  PluginManager::iterator it = PLUGINMANAGER.begin();
  int subMenuId = 10000;
  m_exportMenuFirst = subMenuId;
  for (; it != PLUGINMANAGER.end(); ++it, subMenuId++)
  {
    ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*it);

    if (plugin)
    {
      ExportMenu->Append(subMenuId, plugin->menuLabel());
      Connect(subMenuId,
              wxEVT_COMMAND_MENU_SELECTED,
              wxCommandEventHandler(ModelViewer::OnExport));
      Connect(subMenuId,
              wxEVT_UPDATE_UI,
              wxUpdateUIEventHandler(ModelViewer::OnUpdateCommandUI));
    }
  }
  m_exportMenuEnd = subMenuId;
  fileMenu->Append(ID_EXPORT_MODEL, wxT("Export Model"), ExportMenu);






  fileMenu->AppendSeparator();
  fileMenu->Append(ID_FILE_RESETLAYOUT, _("Reset Layout"));
  fileMenu->AppendSeparator();
  fileMenu->Append(ID_RESTART, _("Restart\tCTRL+SHIFT+R"));
  fileMenu->Append(ID_FILE_EXIT, _("E&xit\tCTRL+X"));

  viewMenu = new wxMenu;
  viewMenu->Append(ID_VIEW_NPC, _("View NPC"));
  viewMenu->Append(ID_VIEW_ITEM, _("View Item"));
  viewMenu->AppendSeparator();
  // The three panels are toggles, checked while shown (the command bar has the same three).
  viewMenu->AppendCheckItem(ID_SHOW_FILE_LIST, _("Browse panel"));
  viewMenu->AppendCheckItem(ID_SHOW_CHAR, _("Model panel"));
  viewMenu->AppendCheckItem(ID_SHOW_ANIM, _("Animation panel"));
  viewMenu->Append(ID_SHOW_MODEL, _("Attachments..."));
  viewMenu->AppendSeparator();
  // The Unity viewport is always the viewport; this is the discoverable way to bring its player back
  // (the notice of a stopped player has the same command on a button).
  viewMenu->Append(ID_VIEW_UNITY_RESTART, _("Restart Unity Renderer"));
  viewMenu->Append(ID_VIEW_FULLSCREEN, _("Fullscreen\tF11"));
  // The colour behind the model in the Models viewer (BackgroundColorDialog): a window of its own, so it can stay open
  // beside the viewport while colours are tried.
  viewMenu->Append(ID_VIEW_BACKGROUND_COLOR, _("Swap Background Color..."),
                   _("Choose the color behind the model in the Models viewer"));
  viewMenu->AppendSeparator();
  // The colours of the viewer's own window (the viewport draws the same in every theme).
  wxMenu * appearanceMenu = new wxMenu;
  appearanceMenu->AppendRadioItem(ID_VIEW_APPEARANCE_SYSTEM, _("System"), _("Light or dark as Windows' app mode is"));
  appearanceMenu->AppendRadioItem(ID_VIEW_APPEARANCE_LIGHT, _("Light"), _("Always the light theme"));
  appearanceMenu->AppendRadioItem(ID_VIEW_APPEARANCE_DARK, _("Dark"), _("Always the dark theme"));
  viewMenu->AppendSubMenu(appearanceMenu, _("Appearance"));
  // The OpenGL viewport's own View items -- Background Color, Load Background, the Camera submenu,
  // Set Canvas Size and OpenGL debug info -- went with that viewport: none of them reaches the Unity
  // viewport, which frames each model itself. (Swap Background Color, above, is the Unity viewport's.) The Lighting menu that was built here (and never put on
  // the menu bar) is gone too.

  try {

    charMenu = new wxMenu;
    charMenu->Append(ID_LOAD_CHAR, _("Load Character\tF8"));
    charMenu->Append(ID_IMPORT_CHAR, _("Import Armory Character"));
    // One entry point for an NPC from a Wowhead link and a model by ID (NPCimporterDialog).
    charMenu->Append(ID_IMPORT_NPC, _("Load NPC / Model..."), _("Load an NPC from a Wowhead link, or a model by its ID"));
    charMenu->Append(ID_SAVE_CHAR, _("Save Character\tF7"));
    charMenu->AppendSeparator();

    charGlowMenu = new wxMenu;
    charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_NONE, _("None"));
    charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEFAULT, _("Default"));
    charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEATHKNIGHT, _("Death Knight"));
    if (charControl->model)
    {
      if (charControl->model->cd.eyeGlowType){
        size_t egt = charControl->model->cd.eyeGlowType;
        if (egt == EGT_NONE)
          charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
        else if (egt == EGT_DEATHKNIGHT)
          charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
        else
          charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
      }
      else{
        charControl->model->cd.eyeGlowType = EGT_DEFAULT;
        charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
      }
    }
    charMenu->Append(ID_CHAREYEGLOW, _("Eye Glow"), charGlowMenu);

    charMenu->AppendCheckItem(ID_SHOW_UNDERWEAR, _("Show Underwear"));
    charMenu->Check(ID_SHOW_UNDERWEAR, true);
    charMenu->AppendCheckItem(ID_SHOW_EARS, _("Show Ears\tCTRL+E"));
    charMenu->Check(ID_SHOW_EARS, true);
    charMenu->AppendCheckItem(ID_SHOW_HAIR, _("Show Hair\tCTRL+H"));
    charMenu->Check(ID_SHOW_HAIR, true);
    charMenu->AppendCheckItem(ID_SHOW_FACIALHAIR, _("Show Facial Hair\tCTRL+F"));
    charMenu->Check(ID_SHOW_FACIALHAIR, true);
    charMenu->AppendCheckItem(ID_SHOW_FEET, _("Show Feet"));
    charMenu->Check(ID_SHOW_FEET, false);
    charMenu->AppendCheckItem(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, _("Auto Hide Geosets for head items"));
    charMenu->Check(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);
    charMenu->AppendCheckItem(ID_SHEATHE, _("Sheathe Weapons\tCTRL+Z"));
    charMenu->Check(ID_SHEATHE, false);

    charMenu->AppendSeparator();
    charMenu->Append(ID_SAVE_EQUIPMENT, _("Save Equipment\tF5"));
    charMenu->Append(ID_LOAD_EQUIPMENT, _("Load Equipment\tF6"));
    charMenu->Append(ID_CLEAR_EQUIPMENT, _("Clear Equipment\tF9"));
    charMenu->AppendSeparator();
    charMenu->Append(ID_LOAD_SET, _("Load Item Set"));
    charMenu->Append(ID_LOAD_START, _("Load Start Outfit"));
    charMenu->AppendSeparator();
    charMenu->Append(ID_MOUNT_CHARACTER, _("Mount / Dismount"));
    charMenu->Append(ID_CHAR_RANDOMISE, _("Randomise Character\tF10"));

    // Start out Disabled.
    charMenu->Enable(ID_SAVE_CHAR, false);
    charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    charMenu->Enable(ID_SHOW_EARS, false);
    charMenu->Enable(ID_SHOW_HAIR, false);
    charMenu->Enable(ID_SHOW_FACIALHAIR, false);
    charMenu->Enable(ID_SHOW_FEET, false);
    charMenu->Enable(ID_SHEATHE, false);
    charMenu->Enable(ID_CHAREYEGLOW, false);
    charMenu->Enable(ID_SAVE_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_EQUIPMENT, false);
    charMenu->Enable(ID_CLEAR_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_SET, false);
    charMenu->Enable(ID_LOAD_START, false);
    charMenu->Enable(ID_MOUNT_CHARACTER, false);
    charMenu->Enable(ID_CHAR_RANDOMISE, false);
    charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);


    // Options menu
    optMenu = new wxMenu;
    // ("Always show default doodads in WMOs" is gone: only the archived OpenGL viewport drew WMOs, and
    // the Unity viewport shows a notice for one. Default doodads stay included, as the item defaulted.)
    optMenu->Append(ID_SHOW_SETTINGS, _("Settings..."));


    wxMenu *aboutMenu = new wxMenu;
    aboutMenu->Append(ID_KEYBOARD_SHORTCUTS, _("Keyboard Shortcuts..."));
    aboutMenu->AppendSeparator();
    aboutMenu->Append(ID_LANGUAGE, _("Language"));
    aboutMenu->Append(ID_ABOUT, _("About"));

    menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, _("&File"));
    menuBar->Append(viewMenu, _("&View"));
    menuBar->Append(charMenu, _("&Character"));
    menuBar->Append(optMenu, _("&Options"));
    menuBar->Append(aboutMenu, _("&Help"));
    SetMenuBar(menuBar);
    m_menuBarTitles.reset(new UiMenuBarTitles);
    m_menuBarTitles->attach(this);
  }
  catch (...) {};

  // Disable our "Character" menu, only accessible when a character model is being displayed
  // menuBar->EnableTop(2, false);

  // Hotkeys / shortcuts (the table is kAppAccelerators, near the top of this file)
  std::vector<wxAcceleratorEntry> entries;
  for (const AppAccelerator & a : kAppAccelerators)
    entries.push_back(wxAcceleratorEntry(a.flags, a.key, a.id));

  wxAcceleratorTable accel((int)entries.size(), entries.data());
  this->SetAcceleratorTable(accel);
}

void ModelViewer::InitObjects()
{
  LOG_INFO << "Initializing Objects...";

  fileControl = new FileControl(this, ID_FILELIST_FRAME);
  // Known from the start (not only once a client's files are listed): the viewer mode goes through it.
  fileControl->modelviewer = this;

  // The Model panel first: the skin, doodad and character controls are created on its pages.
  modelInspector = new ModelInspector(this, wxID_ANY);
  animControl = new AnimControl(this, ID_ANIM_FRAME, modelInspector->skinParent(),
                                modelInspector->overridesParent(), modelInspector->doodadParent());
  charControl = new CharControl(modelInspector->characterParent(), ID_CHAR_FRAME);
  modelInspector->AttachAppearance(animControl, charControl);
  // Never shown. It only still exists because ModelCanvas::InitGL will not set init -- and the canvas
  // clock the Unity viewport mirrors will not tick -- without it.
  lightControl = new LightControl(this, ID_LIGHT_FRAME);
  lightControl->Show(false);
  modelControl = new ModelControl(this, ID_MODEL_FRAME);
  settingsControl = new SettingsControl(this, ID_SETTINGS_FRAME);
  settingsControl->Show(false);

  canvas = new ModelCanvas(this);

  if (video.secondPass) {
    canvas->Destroy();
    video.Release();
    canvas = new ModelCanvas(this);
  }

  g_modelViewer = this;
  g_animControl = animControl;
  g_charControl = charControl;
  g_canvas = canvas;

  modelControl->animControl = animControl;

  enchants = new EnchantsDialog(this, charControl);
}

void ModelViewer::InitDatabase()
{
  LOG_INFO << "Initializing Databases...";
  SetStatusText(wxT("Initializing Databases..."));
  wxBusyCursor busyCursor;
  wxWindowDisabler disableAll;
  wxBusyInfo info(_T("Please wait during game database analysis..."), this);

  // Loading a client again fills these from its own database instead of adding to the lists the
  // previous load made. (The item list keeps the "None" entry its constructor puts first.)
  npcs.clear();
  items = ItemDatabase();

  if (!GAMEDATABASE.initFromXML("database.xml"))
  {
    initDB = false;
    LOG_ERROR << "Initializing failed!";
    SetStatusText(wxT("Initializing failed!"));
    return;
  }
  else
  {
    LOG_INFO << "Initializing succeeded.";
  }

  // init texture regions
  CharTexture::initRegions();
  
  // init Race informations
  RaceInfos::init();
  
  LOG_INFO << "Initializing Databases...";
  SetStatusText(wxT("Initializing Databases..."));
  initDB = true;

  {
    sqlResult npc = GAMEDATABASE.sqlQuery("SELECT ID, DisplayID1, CreatureType, Name_Lang From Creature;");

    if (npc.valid && !npc.empty())
    {
      LOG_INFO << "Found" << npc.values.size() << "NPCs";
      for (int i = 0, imax = npc.values.size(); i < imax; i++)
      {
        NPCRecord rec(npc.values[i]);
        if (rec.model != 0)
          npcs.push_back(rec);
      }
    }
    else
    {
      // Not fatal: this client has no NPC data the viewer could read (see clientCapabilities); the items below
      // are independent of it.
      LOG_ERROR << "Error during NPC detection from database.";
    }

  }
  
  {
    // Keep the existing named catalog, and admit unnamed equipment only when an
    // appearance resolves to a real display record. IN avoids duplicate items
    // when several modifiers share the same appearance.
    sqlResult item = GAMEDATABASE.sqlQuery(
      "SELECT Item.ID, ItemSparse.Display_Lang, Item.InventoryType, Item.ClassID, Item.SubclassID, Item.SheatheType "
      "FROM Item LEFT JOIN ItemSparse ON Item.ID = ItemSparse.ID WHERE Item.InventoryType != 0 AND "
      "(TRIM(COALESCE(ItemSparse.Display_Lang, '')) != '' OR Item.ID IN "
      "(SELECT M.ItemID FROM ItemModifiedAppearance M JOIN ItemAppearance A ON A.ID = M.ItemAppearanceID "
      "JOIN ItemDisplayInfo D ON D.ID = A.ItemDisplayInfoID WHERE A.ItemDisplayInfoID > 0))");

    if (item.valid && !item.empty())
    {
      LOG_INFO << "Found" << item.values.size() << "items";
      for (int i = 0, imax = item.values.size(); i < imax; i++)
      {
        ItemRecord rec(item.values[i]);
        if (rec.name.trimmed().isEmpty())
          rec.name = QString::fromStdWString(_("Unnamed item").ToStdWstring());
        items.items.push_back(rec);
      }
    }
    else
    {
      LOG_ERROR << "Error during Item detection from database.";
    }
  }

  LOG_INFO << "Finished initiating database files.";
  SetStatusText(wxT("Finished initiating database files."));;
}

// The panel arrangement, in one place so InitDocking and ResetLayout cannot disagree:
//
//   +--------------------------------------------------------------+
//   | command bar                                                  |
//   +---------+--------------------------------------+-------------+
//   | Browse  |              viewport                |   Model     |
//   |         |                                      | Appearance  |
//   |         +--------------------------------------+ Geosets     |
//   |         |  Animation                           | Info        |
//   +---------+--------------------------------------+-------------+
//
// Browse and Model are the outer layer so they run the full height; Animation sits under the
// viewport between them. Every panel can be closed (its command bar button and View menu item
// bring it back), resized at its sash, or floated.
static wxAuiPaneInfo buildCommandBarPaneInfo(const wxWindow * frame)
{
  const int height = frame->FromDIP(UiStyle::ToolbarHeight);
  return wxAuiPaneInfo().
         Name(wxT("commandBar")).Caption(wxT("Command bar")).
         Top().Layer(10).Row(0).Position(0).
         CaptionVisible(false).PaneBorder(false).Gripper(false).CloseButton(false).
         Floatable(false).Movable(false).DockFixed(true).
         MinSize(wxSize(-1, height)).BestSize(wxSize(-1, height)).MaxSize(wxSize(-1, height));
}

static wxAuiPaneInfo buildBrowsePaneInfo(const wxWindow * frame)
{
  return wxAuiPaneInfo().
         Name(wxT("fileControl")).Caption(wxT("Browse")).
         BestSize(frame->FromDIP(wxSize(230, 700))).MinSize(frame->FromDIP(wxSize(180, 200))).
         FloatingSize(frame->FromDIP(wxSize(300, 700))).
         Left().Layer(2);
}

static wxAuiPaneInfo buildAnimationPaneInfo(const wxWindow * frame)
{
  return wxAuiPaneInfo().
         Name(wxT("animControl")).Caption(wxT("Animation")).
         BestSize(frame->FromDIP(wxSize(700, 160))).MinSize(frame->FromDIP(wxSize(360, 128))).
         FloatingSize(frame->FromDIP(wxSize(900, 220))).
         Bottom().Layer(1);
}

static wxAuiPaneInfo buildInspectorPaneInfo(const wxWindow * frame)
{
  return wxAuiPaneInfo().
         Name(wxT("modelInspector")).Caption(wxT("Model")).
         BestSize(frame->FromDIP(wxSize(270, 700))).MinSize(frame->FromDIP(wxSize(240, 200))).
         FloatingSize(frame->FromDIP(wxSize(320, 700))).
         Right().Layer(2);
}

// The model and attachment the animation controls drive, and Render / Scale for the items attached to a
// character (they travel to the Unity viewport in the character scene): the floating window
// View > "Attachments" opens. The pane keeps its old name so a saved perspective still finds it.
static wxAuiPaneInfo buildRenderOptionsPaneInfo()
{
  return wxAuiPaneInfo().
         Name(wxT("Models")).Caption(wxT("Attachments")).
         FloatingSize(wxSize(180, 300)).Float().TopDockable(false).LeftDockable(false).
         RightDockable(false).BottomDockable(false).Show(false).
         DestroyOnClose(false);
}

void ModelViewer::InitCommandBar()
{
  // Few commands, by importance (drawn by UiToolBarArt): the viewer selector first, as one segmented
  // control with the active mode in the accent; then the utility commands as quiet buttons; the name of
  // what is shown; and at the right the three panel buttons, check tools pressed while their panel is
  // shown (OnUpdateCommandUI keeps them in step with the panels however those were opened or closed).
  commandBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_PLAIN_BACKGROUND |
                                wxAUI_TB_NO_AUTORESIZE);
  UiToolBarArt * art = new UiToolBarArt(true);
  art->SetToolIcon(ID_UI_MODELS, UiIcon::Models);
  art->SetToolIcon(ID_UI_TEXTURES, UiIcon::Textures);
  art->SetToolIcon(ID_UI_BUILDINGS, UiIcon::Buildings);
  art->SetToolIcon(ID_VIEW_FULLSCREEN, UiIcon::Fullscreen);
  art->SetToolIcon(ID_UI_SCREENSHOT, UiIcon::Screenshot);
  art->SetToolIcon(ID_SHOW_FILE_LIST, UiIcon::Browse);
  art->SetToolIcon(ID_SHOW_CHAR, UiIcon::ModelPanel);
  art->SetToolIcon(ID_SHOW_ANIM, UiIcon::Animation);
  commandBar->SetArtProvider(art);
  commandBar->SetBackgroundColour(UiStyle::palette().panelBackground);
  // The tools touch (the segments join); the gaps between groups are explicit.
  commandBar->SetToolBorderPadding(0);
  commandBar->SetToolPacking(0);
  commandBar->SetToolSeparation(17);   // DIPs: wxWidgets 3.3 scales it itself
  const int margin = (FromDIP(UiStyle::ToolbarHeight) - FromDIP(UiStyle::ToolHeight)) / 2;
  commandBar->SetMargins(FromDIP(UiStyle::M), FromDIP(UiStyle::S), margin, margin);

  // The viewer selector: three radio tools, the active one pressed (OnUpdateCommandUI keeps them on the
  // viewer mode). Nothing may stand between them: that would split them into separate groups.
  commandBar->AddLabel(wxID_ANY, _("Viewer"));
  commandBar->AddTool(ID_UI_MODELS, _("Models"), wxNullBitmap,
                      _("The model viewer: the viewport and the model panels, Browse listing models (asks for a World "
                        "of Warcraft client first if none is loaded)"), wxITEM_RADIO);
  commandBar->AddTool(ID_UI_TEXTURES, _("Textures"), wxNullBitmap,
                      _("The texture viewer: the game client's textures (BLP) in Browse, the selected one in the "
                        "viewport's place"), wxITEM_RADIO);
  commandBar->AddTool(ID_UI_BUILDINGS, _("Buildings"), wxNullBitmap,
                      _("The building viewer: World Model Objects (WMO) -- buildings, dungeons, cities -- in Browse, "
                        "the selected one in the viewport"), wxITEM_RADIO);
  commandBar->AddSeparator();
  // (No Reset camera: it acted on the archived OpenGL viewport; the Unity viewport frames each model itself.)
  commandBar->AddTool(ID_VIEW_FULLSCREEN, _("Fullscreen"), wxNullBitmap, _("Fullscreen (F11; Esc leaves)"));
  commandBar->AddSpacer(FromDIP(2));
  // The Unity viewport's own capture (SaveUnityScreenshot).
  commandBar->AddTool(ID_UI_SCREENSHOT, _("Screenshot"), wxNullBitmap,
                      _("Save the viewport as a 3840 x 2160 PNG with a transparent background"));
  commandBar->AddSeparator();

  // What the viewer shows: the document's name, the bar's one piece of content.
  commandModelLabel = new wxStaticText(commandBar, ID_UI_MODEL_LABEL, _("No model loaded"), wxDefaultPosition,
                                       FromDIP(wxSize(320, -1)), wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
  commandModelLabel->SetFont(UiStyle::font(UiStyle::Type::Strong));
  UiStyle::setRole(commandModelLabel, UiStyle::Role::SecondaryText);
  commandBar->AddControl(commandModelLabel);

  commandBar->AddStretchSpacer();
  commandBar->AddTool(ID_SHOW_FILE_LIST, _("Browse"), wxNullBitmap, _("Show or hide the Browse panel"), wxITEM_CHECK);
  commandBar->AddSpacer(FromDIP(2));
  commandBar->AddTool(ID_SHOW_CHAR, _("Model"), wxNullBitmap, _("Show or hide the Model panel"), wxITEM_CHECK);
  commandBar->AddSpacer(FromDIP(2));
  commandBar->AddTool(ID_SHOW_ANIM, _("Animation"), wxNullBitmap, _("Show or hide the Animation panel"), wxITEM_CHECK);
  commandBar->Realize();
  // No radio tool is checked by itself.
  commandBar->ToggleTool(ID_UI_MODELS, true);
}

void ModelViewer::InitDocking()
{
  LOG_INFO << "Initializing GUI Docking.";

  // wxAUI stuff
  //interfaceManager.SetFrame(this);
  interfaceManager.SetManagedWindow(this);
  UiStyle::applyDockArt(interfaceManager, this);

  InitCommandBar();
  interfaceManager.AddPane(commandBar, buildCommandBarPaneInfo(this));

  // THE VIEWPORT: the Unity host panel, the centre pane from the start, before any player exists.
  //
  // The OpenGL canvas is deliberately NOT a pane. It is archived: a hidden window that is never
  // shown and never painted, kept for the GL context every texture decode needs, the loaded model
  // and the animation clock (see ModelCanvas). Keeping it out of the manager altogether means no
  // saved perspective, reset or pane toggle can ever put it on screen -- and nothing may call
  // GetPane(canvas): for a window the manager does not know, wx hands back one shared, writable
  // "null" pane, and a Show() on that would change the answer to every later unknown lookup.
  CreateUnityViewport();
  interfaceManager.AddPane(unityRendererHost, unityViewportPaneInfo());

  interfaceManager.AddPane(fileControl, buildBrowsePaneInfo(this));
  interfaceManager.AddPane(animControl, buildAnimationPaneInfo(this));
  interfaceManager.AddPane(modelInspector, buildInspectorPaneInfo(this));

  // (No lighting pane: lightControl is never shown; see InitObjects.)

  // model control (View > Attachments)
  interfaceManager.AddPane(modelControl, buildRenderOptionsPaneInfo());

  // settings frame
  interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
                           Name(wxT("Settings")).Caption(wxT("Settings")).
                           FloatingSize(settingsControl->InitialFloatingSize()).Float().TopDockable(false).LeftDockable(false).
                           RightDockable(false).BottomDockable(false).Resizable().Show(false));

  // tell the manager to "commit" all the changes just made
  //interfaceManager.Update();
}

void ModelViewer::ResetLayout()
{
  // Every panel goes back to where InitDocking put it. The Unity viewport stays the centre pane; the
  // archived canvas is not a pane and is not touched. In a viewer that puts panels away (Textures,
  // Buildings), the default layout is what they are given back to, and they are put away again until it goes.
  m_workspace = 0;
  m_workspaceRecord.clear();
  m_workspaceGivenBack.clear();
  m_workspaceLayout.Clear();
  interfaceManager.DetachPane(commandBar);
  interfaceManager.DetachPane(fileControl);
  interfaceManager.DetachPane(unityRendererHost);
  interfaceManager.DetachPane(animControl);
  interfaceManager.DetachPane(modelInspector);
  interfaceManager.DetachPane(modelControl);
  interfaceManager.DetachPane(settingsControl);

  interfaceManager.AddPane(commandBar, buildCommandBarPaneInfo(this));
  interfaceManager.AddPane(unityRendererHost, unityViewportPaneInfo());

  interfaceManager.AddPane(fileControl, buildBrowsePaneInfo(this).Show(true));
  interfaceManager.AddPane(animControl, buildAnimationPaneInfo(this).Show(true));
  interfaceManager.AddPane(modelInspector, buildInspectorPaneInfo(this).Show(true));

  interfaceManager.AddPane(modelControl, buildRenderOptionsPaneInfo());

  interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
                           Name(wxT("Settings")).Caption(wxT("Settings")).
                           FloatingSize(settingsControl->InitialFloatingSize()).Float().TopDockable(false).LeftDockable(false).
                           RightDockable(false).BottomDockable(false).Resizable().Show(false));

  applyWorkspace(workspacePanes(m_viewerMode), false);

  // tell the manager to "commit" all the changes just made
  interfaceManager.Update();
}


void ModelViewer::LoadSession()
{
  LOG_INFO << "Loading Session settings from:" << QString::fromWCharArray(cfgPath.c_str());

  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  // Application Config Settings
  useRandomLooks = config.value("Session/RandomLooks", true).toBool();
  GLOBALSETTINGS.bInitPoseOnlyExport = config.value("Session/InitPoseOnlyExport", false).toBool();

  // Last legacy-MPQ folder picked via File > Load Legacy MPQ Client... (defaults the dir picker).
  m_lastMpqFolder = config.value("Session/LastMpqFolder", "").toString();

  // The Models viewport's background (ModelViewport/BackgroundColor), before the viewport exists.
  m_viewportBackground = ViewportBackground::loadColour();
  m_viewportBackgroundKept = m_viewportBackground;

  // The archived OpenGL viewport's session keys are no longer read: Session/ShowParticle and
  // Session/ZeroParticle (particle options that only changed its drawing), Session/bgCol and
  // Session/bgCustCol0-15 (background colour), Session/DBackground and Session/BackgroundImage (a
  // background image, which used to be decoded into GL at every start for nobody to see). Values left in
  // an existing Config.ini are ignored.
}

void ModelViewer::SaveSession()
{
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  // Nothing of the archived OpenGL viewport is written any more: not its display mode, field of view or
  // environment mapping (Graphics/*, which the Settings > Display page edited), its particle options,
  // background colour and image, or canvas size (Session/CanvasWidth/CanvasHeight).

  config.setValue("Session/RandomLooks", useRandomLooks);
  config.setValue("Session/InitPoseOnlyExport", GLOBALSETTINGS.bInitPoseOnlyExport);
  config.setValue("Session/LastMpqFolder", m_lastMpqFolder);

  // Armory importer proxy URL override (entered in General Settings).
  config.setValue("Armory/ProxyURL", QString::fromStdString(GLOBALSETTINGS.armoryProxyURL()));

  // model file
  if (canvas && canvas->model())
    config.setValue("Session/Model", canvas->model()->name());
}

void ModelViewer::LoadLayout()
{
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  int posx = config.value("Session/PositionX", "").toInt();
  int posy = config.value("Session/PositionY", "").toInt();

  // Sanity-check the saved position against the currently-connected monitors before applying it.
  // A bad value strands the window where the user can't reach it -- e.g. (-32000,-32000) left by a
  // non-interactive run, or a coordinate on a monitor that has since been disconnected. If the
  // window's title bar wouldn't land on any live display, fall back to a safe on-screen spot.
  bool onScreen = false;
  for (unsigned int d = 0; d < wxDisplay::GetCount(); d++)
  {
    const wxRect g = wxDisplay(d).GetGeometry();
    if (g.Contains(posx + 20, posy + 10)) // a few px into the window must be visible
    {
      onScreen = true;
      break;
    }
  }
  if (!onScreen)
  {
    const wxRect primary = wxDisplay(0u).GetClientArea();
    posx = primary.x + 40;
    posy = primary.y + 40;
    LOG_INFO << "Saved window position was off-screen; recentering to" << posx << posy;
  }

  SetPosition(wxPoint(posx, posy));

  wxString layout = config.value("Session/Layout", "").toString().toStdWString();
  const int layoutVersion = config.value("Session/LayoutVersion", 0).toInt();

  // if the layout data exists,  load it. A layout saved by the previous panel arrangement (no
  // version) names panes that no longer exist and lacks the ones that do, so it is ignored and
  // the default arrangement used instead.
  if (layoutVersion < LAYOUT_VERSION && !layout.IsEmpty())
  {
    LOG_INFO << "Saved GUI layout is from an earlier panel arrangement; using the default layout.";
  }
  else if (!layout.IsNull() // something goes wrong
      && !layout.IsEmpty() // empty value
      && !layout.EndsWith(L"canvas")) // old saving badly read by Qt, ignore
  {
    if (!interfaceManager.LoadPerspective(layout, false))
    {
      // wxWidgets 3.3 can give up halfway, with the panes already hidden: the default layout then.
      LOG_ERROR << "Could not load the layout.";
      ResetLayout();
    }
    else
    {
      // No need to display these windows on startup. A perspective stores captions too, and one saved
      // before the Attachments window was renamed would bring back its old OpenGL caption.
      interfaceManager.GetPane(modelControl).Show(false).Caption(wxT("Attachments"));
      auto &settingsPane = interfaceManager.GetPane(settingsControl);
      // Migrate the old fixed-size pane even when restored from a saved layout.
      if (!settingsPane.IsResizable())
        settingsPane.FloatingSize(settingsControl->InitialFloatingSize());
      settingsPane.Resizable().Show(false);

      // The command bar is not something a saved layout can take away. Browse, Model and
      // Animation keep whatever shown state the layout saved: that is how the panels a user
      // closed stay closed next time.
      interfaceManager.GetPane(wxT("commandBar")).Show(true);
      // It keeps today's height, too: a layout saved before the bar grew stores the old one.
      {
        const wxAuiPaneInfo bar = buildCommandBarPaneInfo(this);
        interfaceManager.GetPane(wxT("commandBar")).MinSize(bar.min_size).BestSize(bar.best_size).MaxSize(bar.max_size);
      }
      // Nor is the viewport: whatever the perspective says, the Unity viewport is the shown centre.
      EnsureUnityViewportCentre();
#ifndef  _LINUX // buggy
      interfaceManager.Update();
#endif
      LOG_INFO << "GUI Layout loaded from previous session.";
    }
  }

  // The saved canvas size (Session/CanvasWidth/CanvasHeight) is no longer restored. It sized the frame
  // around the OpenGL canvas with Fit(), and Fit() only measures windows that are shown -- with the
  // canvas archived and hidden, it shrank the frame around the panels and left the Unity viewport a
  // strip a few dozen pixels high.
}

void ModelViewer::SaveLayout()
{
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  // The user's layout, with the panels a texture on screen has put away given back.
  config.setValue("Session/Layout", QString::fromWCharArray(userPerspective().c_str()));
  config.setValue("Session/LayoutVersion", LAYOUT_VERSION);

  wxPoint pos = GetPosition();
  config.setValue("Session/PositionX", pos.x);
  config.setValue("Session/PositionY", pos.y);

  LOG_INFO << "GUI Layout was saved.";
}


void ModelViewer::LoadModel(GameFile * file, int raceID, int sexID)
{
  if (!canvas || !file)
    return;

  // A model loaded -- from Browse, or from a menu while textures were on screen -- is the model viewer's.
  SetViewerMode(ViewerMode::Models);

  LOG_INFO << "Loading model:" << file->fullname();

  // don't reload same model -- unless it is an NPC: its stored appearance, class context and equipment are the NPC's,
  // and a character asked for on the same file (an Armory import, a saved character) starts afresh
  if (canvas->model() && canvas->model()->gamefile && (canvas->model()->gamefile->fullname() == file->fullname()) &&
      !canvas->model()->cd.isNPC)
  {
    // One model file can carry more than one race (a Mag'har Orc wears the Orc model), so the
    // same file may still be a different character: switch the race on the loaded model rather
    // than reloading it, and rebuild the character panel around its customization options.
    WoWModel * loaded = const_cast<WoWModel *>(canvas->model());
    if (raceID >= 0 && loaded->setRaceSex(raceID, sexID) && charControl && charControl->charAtt)
    {
      // A character started afresh: what it looked like on its other model is no longer it.
      if (m_variantSwitching == 0)
        m_variantSession.clear();
      charControl->UpdateModel(charControl->charAtt);
    }
    return;
  }

  // Any load but a model variant switch is another character (or none): its appearance on the other model of a
  // pair is no longer remembered, and it is no creature display until ShowCreatureDisplay says so.
  if (m_variantSwitching == 0)
    m_variantSession.clear();
  m_shownAsCreatureDisplay = false;

  isModel = true;
  // A model replaces whatever WMO was shown (canvas->LoadModel below drops it). Only
  // FileControl::ClearCanvas used to reset this, and the menu loads -- an NPC, an item, a character file,
  // an import -- do not go through it.
  isWMO = false;

  // A direct model load is not an NPC; clear any NPC export descriptor. (LoadNPC calls us and
  // then re-sets it afterwards, so the NPC case is unaffected.) Same for the item skin: a raw
  // model load has no item skin, and LoadItem calls us then re-sets it afterwards.
  m_exportNpcId = -1;
  m_exportNpcDisplayId = 0;
  m_exportItemSkinFileId = 0;

  // check if this is a character model: one this client's character tables name (a race's ChrModel shows it, see
  // RaceInfos), or one in the character folders (a file those tables do not name, such as a legacy model
  // WoWModel::initRaceInfos resolves by its path). The folder alone is not enough: a character model the listfile has
  // no real name for is listed under a generated one -- Classic Beta's Skyborne models are
  // models/creature/unk_exp00_<id>/<id>.m2 -- and was loaded as a creature: no race, no customization, no composed
  // skin, a white model.
  RaceInfos chrModelRace;
  isChar = RaceInfos::getRaceInfosForFileID(file->fileDataId(), chrModelRace) ||
           RaceInfos::getRaceInfosForAlternateFileID(file->fileDataId(), chrModelRace) ||
           file->fullname().startsWith("char", Qt::CaseInsensitive) || file->fullname().startsWith("alternate\\char", Qt::CaseInsensitive);
  Attachment *modelAtt = NULL;

  if (isChar)
  {
    ReleaseRider();   // the canvas below frees only its own model: a mount, if the character was riding one
    modelAtt = canvas->LoadModel(file);
    // THE MODEL THAT WAS ON THE CANVAS IS FREED BY THAT CALL (ModelCanvas::LoadModel clears the attachments,
    // which deletes charAtt, and then deletes the model itself), so the character control's two pointers into
    // it are stale from here until UpdateModel below re-points them. Everything that reads them in between --
    // the viewport state a failed load goes on to refresh (DisplayedContentChanged) -- would read freed memory.
    charControl->charAtt = nullptr;
    charControl->model = nullptr;
    // The Attachments window may have pointed at one of the old model's items (a helm, a weapon): those are freed
    // with it now, so it forgets them until UpdateModel below re-points it -- a failed load never gets that far.
    if (modelControl)
      modelControl->Forget();
    // error check
    if (!modelAtt)
    {
      LOG_ERROR << "Failed to load the model" << file->fullname();
      // The previous model is gone (canvas->LoadModel dropped it), so everything that shows what is
      // loaded -- the viewport included -- has to hear about it, or it keeps describing that model.
      DisplayedContentChanged();
      return;
    }

    // add children to manage items equipped
    WoWModel * m = const_cast<WoWModel *>(canvas->model());
    m->addChild(new WoWItem(CS_SHIRT));
    m->addChild(new WoWItem(CS_HEAD));
    m->addChild(new WoWItem(CS_SHOULDER));
    m->addChild(new WoWItem(CS_PANTS));
    m->addChild(new WoWItem(CS_BOOTS));
    m->addChild(new WoWItem(CS_CHEST));
    m->addChild(new WoWItem(CS_TABARD));
    m->addChild(new WoWItem(CS_BELT));
    m->addChild(new WoWItem(CS_BRACERS));
    m->addChild(new WoWItem(CS_GLOVES));
    m->addChild(new WoWItem(CS_HAND_RIGHT));
    m->addChild(new WoWItem(CS_HAND_LEFT));
    m->addChild(new WoWItem(CS_CAPE));
    m->addChild(new WoWItem(CS_QUIVER));
    m->modelType = MT_CHAR;

    // A race that shares this model file with another one is read as that other race (the first
    // race on the file), and would then offer only that race's customization options. Put the
    // race that was asked for back, before the character panel is built from it below.
    if (raceID >= 0)
      m->setRaceSex(raceID, sexID);
  }
  else
  {
    ReleaseRider();
    modelAtt = canvas->LoadModel(file); //  change it from LoadModel, don't sure it's right or not.
    charControl->charAtt = nullptr;   // freed with the model they pointed at: see the character branch above
    charControl->model = nullptr;
    if (modelControl)
      modelControl->Forget();

    // error check
    if (!modelAtt)
    {
      LOG_ERROR << "Failed to load the model" << file->fullname();
      DisplayedContentChanged();   // see the character branch above
      return;
    }
    // creature model, keep left/right hand only as equipment
    WoWModel * m = const_cast<WoWModel *>(canvas->model());
    m->addChild(new WoWItem(CS_HAND_RIGHT));
    m->addChild(new WoWItem(CS_HAND_LEFT));
    m->modelType = MT_NORMAL;
  }

  // Error check,  make sure the model was actually loaded and set to canvas->model
  if (!canvas->model())
  {
    LOG_ERROR << "[ModelViewer::LoadModel()]  Model* Canvas::model is null!";
    DisplayedContentChanged();
    return;
  }

  // The loaded model's name goes in the title bar and the command bar; the status bar gets its
  // facts (vertices, triangles...) from DisplayedContentChanged below.
  // Show the loaded model in the title bar -- the status bar at the bottom is easy to miss,
  // so this makes "what am I looking at" obvious at a glance.
  SetTitle(wxString(GLOBALSETTINGS.appTitle()) + wxT("  -  ") +
           wxString(canvas->model()->name().toStdWString()));
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  m->charModelDetails.isChar = isChar;

  if (isChar)
  {
    charMenu->Check(ID_SHOW_UNDERWEAR, true);
    charMenu->Check(ID_SHOW_EARS, true);
    charMenu->Check(ID_SHOW_HAIR, true);
    charMenu->Check(ID_SHOW_FACIALHAIR, true);
    charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);

    charMenu->Enable(ID_SAVE_CHAR, true);
    charMenu->Enable(ID_SHOW_UNDERWEAR, true);
    charMenu->Enable(ID_SHOW_EARS, true);
    charMenu->Enable(ID_SHOW_HAIR, true);
    charMenu->Enable(ID_SHOW_FACIALHAIR, true);
    charMenu->Enable(ID_SHOW_FEET, true);
    charMenu->Enable(ID_SHEATHE, true);
    charMenu->Enable(ID_CHAREYEGLOW, true);
    charMenu->Enable(ID_SAVE_EQUIPMENT, true);
    charMenu->Enable(ID_LOAD_EQUIPMENT, true);
    charMenu->Enable(ID_CLEAR_EQUIPMENT, true);
    charMenu->Enable(ID_LOAD_SET, true);
    charMenu->Enable(ID_LOAD_START, true);
    charMenu->Enable(ID_MOUNT_CHARACTER, true);
    charMenu->Enable(ID_CHAR_RANDOMISE, true);
    charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);

    charControl->UpdateModel(modelAtt);
  }
  else
  {
    charControl->UpdateModel(modelAtt);

    charMenu->Enable(ID_SAVE_CHAR, false);
    charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    charMenu->Enable(ID_SHOW_EARS, false);
    charMenu->Enable(ID_SHOW_HAIR, false);
    charMenu->Enable(ID_SHOW_FACIALHAIR, false);
    charMenu->Enable(ID_SHOW_FEET, false);
    charMenu->Enable(ID_SHEATHE, false);
    charMenu->Enable(ID_CHAREYEGLOW, false);
    charMenu->Enable(ID_SAVE_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_EQUIPMENT, false);
    charMenu->Enable(ID_CLEAR_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_SET, false);
    charMenu->Enable(ID_LOAD_START, false);
    charMenu->Enable(ID_MOUNT_CHARACTER, false);
    charMenu->Enable(ID_CHAR_RANDOMISE, false);
    charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
  }

  // Update the model control
  modelControl->UpdateModel(modelAtt);
  modelControl->RefreshModel(canvas->root);

  // Embedded Unity renderer (if open): the load itself goes out FIRST. AnimControl::UpdateModel
  // below pushes the skin, the default animation and its playback state for the new model, and
  // the player judges every push by the model it names -- so the player has to know which model
  // is coming before those arrive, or it drops them as being about a different model and starts
  // the new animation unsynchronised. Sending the load first is also what a player that is not
  // open yet gets on connect (onUnityReady -> SendCurrentModelToUnity), so the two paths agree.
  SendLoadToUnity();
  // ... and, for a character, what it looks like: refresh() has already run (charControl above), so
  // the scene the body load waits for goes out with it. An import that dresses the character further
  // holds this until it is done (SceneHold); later changes follow from the canvas tick.
  SendCharacterSceneToUnity(true);

  // Update the animations / skins. This is where the skin and animation pushes happen, once,
  // from the choices the control makes; nothing re-sends them afterwards.
  animControl->UpdateModel(m);

  // WHAT THE MODEL DISPLAYS, always. AnimControl::SetSkin pushes the skin -- which carries the
  // displayed geosets and the particle colour, not just textures -- but only for a model that
  // HAS a skin group in the database. A creature with none (felreavergolem, the cinematic
  // models) pushed nothing, so the player never learned which submeshes the app is drawing and
  // fell back to "geoset 0 only": parts the host displays were missing in the Unity
  // viewport. Pushing here covers both cases; a model whose skin was already pushed simply gets the
  // same answer twice, which the player treats as the state it already has.
  SendCurrentSkinToUnity();

  // The Model panel, the command bar, the status bar and the viewport (the model, or a notice when
  // the Unity viewport cannot draw it yet) follow the new model -- a character rebuilt on its other model generation
  // once it is dressed, at the end of SwitchCharacterVariant, not here as well.
  if (m_variantSwitching == 0)
    DisplayedContentChanged();

  // Lay out ONLY if a pane's shown state actually changed. An unconditional Update() here erased and
  // repainted the whole window, player included, on every load: see CommitLayoutIfChanged.
  CommitLayoutIfChanged();
}

// A mounted character is not the canvas model -- the mount is, and the character hangs on the root's attachment --
// so replacing or clearing the canvas freed the mount and left the rider, with all its equipment, to leak (about
// 144 MB for a robed NPC, on every load made while mounted). It is freed here, with its attachment already gone.
// Only while a mount is up: otherwise the character IS the canvas model, and charControl->model may point at a
// model already freed.
void ModelViewer::ReleaseRider()
{
  const WoWModel * mount = canvas ? canvas->model() : nullptr;
  WoWModel * rider = charControl ? charControl->model : nullptr;
  Attachment * node = charControl ? charControl->charAtt : nullptr;
  if (!mount || !mount->isMount || !rider || rider == mount || !node || !canvas->root)
    return;
  // The rider is alive only while its node still hangs on the root and holds it -- compared as pointers, nothing read
  // through them (after a failed load charControl->model can be a model the canvas freed).
  const std::vector<Attachment *> & nodes = canvas->root->children;
  if (std::find(nodes.begin(), nodes.end(), node) == nodes.end() || node->model() != rider)
    return;
  canvas->clearAttachments();
  canvas->root->setModel(nullptr); // the mount goes next, and one that did not load would not detach itself
  charControl->charAtt = nullptr;
  charControl->model = nullptr;
  if (modelControl)
    modelControl->Forget();
  if (g_selModel == rider) // the Animation panel was on the rider (View > Attachments)
  {
    if (animControl)
      animControl->Forget();
    g_selModel = nullptr;
  }
  delete rider;
}

// Load an NPC model
void ModelViewer::LoadNPC(unsigned int modelid)
{
  // A model from a menu: the model viewer's, even when it turns out to have no model.
  SetViewerMode(ViewerMode::Models);
  // Described to the Unity viewport once, dressed, when this returns: see SceneHold.
  SceneHold sceneHold(this);

  ReleaseRider();
  canvas->clearAttachments();
  canvas->setModel(NULL);
  charControl->charAtt = nullptr;   // freed with the model, as in LoadModel: nothing may read them until re-pointed
  charControl->model = nullptr;
  if (modelControl)
    modelControl->Forget();   // it may point at an item of the model just freed: see LoadModel
  canvas->ClearWMO();   // a world model left on the canvas goes with the flag (a failed load must not keep it)

  isModel = true;
  isChar = false;
  isWMO = false;


  QString query = QString("SELECT CreatureModelData.FileDataID, CreatureDisplayInfo.TextureVariationFileDataID1, "
                          "CreatureDisplayInfo.TextureVariationFileDataID2, CreatureDisplayInfo.TextureVariationFileDataID3, "
                          "CreatureDisplayInfo.ExtendedDisplayInfoID, CreatureDisplayInfo.ID FROM Creature "
                          "LEFT JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
                          "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
                          "WHERE Creature.ID = %1;").arg(modelid);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  // The NPC's display id may not resolve to a model in the currently loaded data -- e.g. an NPC
  // from a newer patch than this build's data: the Wowhead page hands back a CreatureDisplayInfo
  // id that doesn't exist locally, so the LEFT JOINs above yield a null FileDataID, getFile(0)
  // returns null and LoadModel fails. The model pointer is then null; bail out cleanly here
  // instead of dereferencing it (which crashed the whole application).
  auto bailNPCUnavailable = [&]()
  {
    LOG_ERROR << "LoadNPC: NPC" << modelid << "has no loadable model in the currently loaded game "
                 "data (its display id is likely from a newer / PTR build); not displaying.";
    if (!batchMode)
      wxMessageBox(wxT("This NPC could not be displayed.\n\n"
                       "Its model isn't in the game data WoW Model Viewer currently has loaded. "
                       "This usually means the NPC is from a newer patch (for example a PTR build) "
                       "than the data you have loaded."),
                   wxT("NPC unavailable"), wxOK | wxICON_INFORMATION);
    fileControl->UpdateInterface();
    DisplayedContentChanged();
    CommitLayoutIfChanged();
  };

  if (r.valid && !r.empty())
  {
    if (!ShowCreatureDisplay(r.values[0][0].toInt(), r.values[0][4].toInt(), r.values[0][5].toInt()))
    {
      bailNPCUnavailable();
      return;
    }
  }

  // Reaching here means the NPC composed successfully (the failure paths above return early).
  // Remember it so an out-of-process FBX export can rebuild this exact NPC via -npc.
  m_exportNpcId = (int)modelid;
  m_exportNpcDisplayId = 0; // child re-resolves DisplayID1; overridden by LoadNPCByDisplay

  fileControl->UpdateInterface();
  DisplayedContentChanged();

  CommitLayoutIfChanged();
}

// THE APPEARANCE OF A CREATURE DISPLAY, shared by an NPC (LoadNPC: View NPC, an NPC link) and Load NPC / Model by
// Creature Display ID (LoadModelById). fileDataId is the display's CreatureModelData.FileDataID, extraId its
// ExtendedDisplayInfoID, displayId its CreatureDisplayInfo.ID. A simple display is that model with the display's skin
// -- texture variations, geosets, particle colours (AnimControl::SetSkinByDisplayID); a display with extended info is
// a humanoid NPC, shown on its race's HD character model as the NPC it is (see below).
// False when the model did not load; nothing is applied then.
bool ModelViewer::ShowCreatureDisplay(int fileDataId, int extraId, int displayId)
{
  // if npc is a simple one (no extra info CreatureDisplayInfoExtra)
  if (extraId == 0)
  {
    LoadModel(GAMEDIRECTORY.getFile(fileDataId));
    WoWModel * m = const_cast<WoWModel *>(canvas->model());
    if (!m)
      return false;
    m_shownAsCreatureDisplay = true;
    m->modelType = MT_NORMAL;
    animControl->SetSkinByDisplayID(displayId);
    return true;
  }

  // A HUMANOID NPC, from its extended display (CreatureDisplayInfoExtra):
  //  - the race and sex it is shown as. One model file can carry several races -- MoP Classic's Human files list
  //    Gilnean first, Classic Beta's Skyborne files two races -- and the race decides which options the model has
  //    and which race-specific item parts it wears. A race that does not wear the file is not applied (the file's
  //    first race stays, and the stored choices of the other race's model are skipped);
  //  - its own stored appearance (CreatureDisplayInfoOption), NPC-only choices included, applied internally: the
  //    player's lists still offer only player choices (CharDetails::applyStoredAppearance);
  //  - its class context (DisplayClassID: a Demon Hunter wears the class's own choices and item parts);
  //  - its equipment (NpcModelItemSlotDisplayInfo).
  // A client whose extended display table is not installed shows the file's first race with its stored choices.
  int race = -1, sex = -1, classID = 0;
  sqlResult extra = GAMEDATABASE.sqlQuery(QString("SELECT DisplayRaceID, DisplaySexID, DisplayClassID FROM CreatureDisplayInfoExtra "
                                                   "WHERE ID = %1").arg(extraId));
  if (extra.valid && !extra.empty())
  {
    race = extra.values[0][0].toInt();
    sex = extra.values[0][1].toInt();
    classID = extra.values[0][2].toInt();
  }
  // A race's model of the other generation (Classic Beta's classic models) wears its own options: an NPC whose stored
  // choices are the pair's playable model's is shown on that model, as that model's own NPC would be.
  const int shownFileId = RaceInfos::getCreatureDisplayFileID(fileDataId, extraId);
  if (shownFileId != RaceInfos::getHDModelForFileID(fileDataId))
    LOG_INFO << "Creature display" << displayId << "names the other-generation model" << RaceInfos::getHDModelForFileID(fileDataId)
             << "but stores choices of the model" << shownFileId << "-- shown on that model";
  GameFile * modelFile = GAMEDIRECTORY.getFile(shownFileId);
  // The race on its model in the model's own sex: a dragon form's model (a Dracthyr's, a drake's) has its own, not the
  // male or female its display names.
  RaceInfos wearer;
  if (race > 0 && modelFile && RaceInfos::getRaceInfosForRaceAndFile(race, modelFile->fileDataId(), wearer))
    sex = wearer.sexID;
  LoadModel(modelFile, race > 0 ? race : -1, sex);
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  if (!m)
    return false;
  if (!m->charModelDetails.isChar)
  {
    // A few displays carry extended info on a model no race uses (a treasure chest): it is shown as that model.
    LOG_WARNING << "Creature display" << displayId << "has extended display" << extraId << "on a model that is not a character -- "
                   "shown as the model";
    return true;
  }
  if (race > 0 && (m->infos.raceID != race || m->infos.sexID != sex))
    LOG_WARNING << "Creature display" << displayId << ": race" << race << "sex" << sex << "of extended display" << extraId
                << "does not wear" << m->gamefile->fullname() << "-- shown as race" << m->infos.raceID;

  // The stored appearance first, in the NPC's class context: the equipment's parts are chosen by it as they load.
  std::vector<std::pair<uint, uint> > stored;
  sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT ChrCustomizationOptionID, ChrCustomizationChoiceID FROM CreatureDisplayInfoOption "
                                              "WHERE CreatureDisplayInfoExtraID = %1 ORDER BY ID").arg(extraId));
  for (size_t i = 0; r.valid && i < r.values.size(); i++)
    stored.emplace_back(r.values[i][0].toUInt(), r.values[i][1].toUInt());
  m->cd.applyStoredAppearance(stored, classID == CLASS_DEMONHUNTER && m->cd.clientHasDemonHunters());

  // NpcModelItemSlotDisplayInfo.ItemSlot, as the data's items show it (their inventory types and the texture sections
  // they paint): 0 head, 1 shoulder, 2 shirt, 3 chest, 4 waist, 5 legs, 6 feet, 7 wrist, 8 hands, 9 tabard, 10 back.
  // A slot outside these is no equipment slot (Retail's 11 always names an empty display): it is skipped, where it
  // used to land on the head and take the helmet off.
  static const std::map<int, CharSlots> npcItemSlots = { { 0, CS_HEAD }, { 1, CS_SHOULDER }, { 2, CS_SHIRT }, { 3, CS_CHEST },
    { 4, CS_BELT }, { 5, CS_PANTS }, { 6, CS_BOOTS }, { 7, CS_BRACERS }, { 8, CS_GLOVES }, { 9, CS_TABARD }, { 10, CS_CAPE } };
  r = GAMEDATABASE.sqlQuery(QString("SELECT ItemDisplayInfoID, ItemSlot FROM NpcModelItemSlotDisplayInfo "
                                    "WHERE NpcModelID = %1 ORDER BY ID").arg(extraId));
  for (size_t i = 0; r.valid && i < r.values.size(); i++)
  {
    const auto slot = npcItemSlots.find(r.values[i][1].toInt());
    if (slot == npcItemSlots.end())
      continue;
    if (WoWItem * item = m->getItem(slot->second))
      item->setDisplayId(r.values[i][0].toInt());
  }

  m->cd.isNPC = true;
  m_shownAsCreatureDisplay = true;
  g_charControl->RefreshModel();
  g_charControl->RefreshEquipment();
  return true;
}

bool ModelViewer::LoadModelById(const ModelIdLookup::Resolved & resolved, wxString & why)
{
  why.clear();
  GameFile * file = GAMEDIRECTORY.getFile(resolved.loadFileDataId);
  if (!file)
  {
    why = wxString::Format(_("FileDataID %d was not found in the loaded client."), resolved.loadFileDataId);
    return false;
  }
  LOG_INFO << "Load NPC / Model by ID:" << resolved.describe();
  {
    // On a cleared canvas, as LoadNPC starts: the file is loaded afresh even when it is the one on show, so an ID
    // always shows what it names -- not the skin or equipment the same file was last given.
    SetViewerMode(ViewerMode::Models);
    SceneHold sceneHold(this);
    ReleaseRider();
    canvas->clearAttachments();
    canvas->setModel(NULL);
    charControl->charAtt = nullptr;   // as LoadNPC
    charControl->model = nullptr;
    if (modelControl)
      modelControl->Forget();   // as LoadNPC
    canvas->ClearWMO();   // as LoadNPC
    isModel = true;
    isChar = false;
    isWMO = false;
    if (resolved.kind == ModelIdLookup::Kind::CreatureDisplay)
      ShowCreatureDisplay(resolved.fileDataId, resolved.extendedDisplayId, resolved.displayId);   // no Creature row read or written
    else
      LoadModel(file);   // the model file alone, by the loader Browse ends in (Browse finds files by name; this one may have none)
    fileControl->UpdateInterface();
    DisplayedContentChanged();
    CommitLayoutIfChanged();
  }
  // What is on the canvas now has to be the file the ID resolved to.
  WoWModel * shown = const_cast<WoWModel *>(canvas->model());
  if (!shown || !shown->gamefile || shown->gamefile->fileDataId() != resolved.loadFileDataId)
  {
    LOG_ERROR << "Load NPC / Model by ID: the model did not load:" << resolved.describe();
    why = wxString::Format(_("FileDataID %d could not be loaded as a model; File > View Log shows what the loader "
                             "reported."), resolved.loadFileDataId);
    return false;
  }
  return true;
}

void ModelViewer::LoadNPCByDisplay(int npcId, int displayId, int type, const QString & name)
{
  if (npcId <= 0)
    return;

  // If this NPC isn't already in the loaded WoW database, add it from the imported data so
  // LoadNPC() can resolve its display/model (mirrors the old NPC-browser import path).
  sqlResult existing = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM Creature WHERE ID = %1").arg(npcId));
  if ((!existing.valid || existing.empty()) && displayId > 0)
    GAMEDATABASE.sqlQuery(QString("INSERT INTO Creature(ID,CreatureType,DisplayID1,Name_Lang) VALUES (%1,%2,%3,\"%4\")")
                            .arg(npcId).arg(type).arg(displayId).arg(name));

  LoadNPC(npcId);

  // Carry the display id (LoadNPC only knows the creature id) so a child process can
  // re-register an imported NPC that isn't in its own database.
  if (m_exportNpcId == (int)npcId)
    m_exportNpcDisplayId = displayId;
}

// The component-geoset state an item's own model should be shown with.
//
// Opening an item here loads its component M2 as a plain model, and a plain model comes up in the
// parse-time default: submesh id 0 only (WoWModel's "hdgeo->display = (hdgeo->id == 0)"). That
// default is right for a raw M2 opened on its own. It is NOT what the item looks like, and the
// application already knows better -- WoWItem decides a component's geosets when the item is
// equipped, and for the slots below it decides "all of them" (WoWItem::updateItemModel, "for (uint
// i = 0; i < m->geosets.size(); i++) m->showGeoset(i, true);"). Drakestalker's Trophy Pauldrons are
// the worked example: their upper flame plumes are submesh 2, geoset 2602, and the default hides
// them here while the equipped item shows them.
//
// WHICH SLOTS. ModelResourcesID1 -- the model this function loads -- is the ATTACHED component for
// head, shoulder, belt (the buckle) and both hands; those take WoWItem's attachment path and its
// all-on rule, and that is what is reproduced. For boots, trousers, shirt, chest, gloves and cape
// ModelResourcesID1 is instead the MERGED component: WoWItem hides everything and re-opens one
// variant per geoset group from ItemDisplayInfo.AttachmentGeosetGroup, against a model merged into
// the character. That selection has no meaning for a component shown on its own out of that
// context, so those slots are deliberately left at the default rather than guessed at.
void ModelViewer::applyItemComponentGeosets(unsigned int itemId)
{
  WoWModel * m = const_cast<WoWModel *>(canvas ? canvas->model() : 0);
  if (!m)
    return;

  sqlResult r = GAMEDATABASE.sqlQuery(
      QString("SELECT InventoryType FROM Item WHERE ID = %1").arg(itemId));
  if (!r.valid || r.empty())
    return;
  const int invType = r.values[0][0].toInt();

  // The slot this inventory type belongs to, through the application's own mapping rather than a
  // second table of numbers kept in step by hand.
  int slot = -1;
  for (int s = 0; s < NUM_CHAR_SLOTS; s++)
  {
    if (correctType(invType, s)) { slot = s; break; }
  }

  const bool attachedComponent = (slot == CS_HEAD || slot == CS_SHOULDER || slot == CS_BELT ||
                                  slot == CS_HAND_LEFT || slot == CS_HAND_RIGHT);
  if (!attachedComponent)
  {
    LOG_INFO << "[itemgeoset] item" << itemId << "inventory type" << invType
             << "-- its first component is merged into the character, not attached; left at the"
             << "default geoset state";
    return;
  }

  for (uint i = 0; i < m->geosets.size(); i++)
    m->showGeoset(i, true);
  LOG_INFO << "[itemgeoset] item" << itemId << "slot" << slot << ": showed all"
           << (int)m->geosets.size() << "component geoset(s), as the equipped attachment path does";
}

void ModelViewer::LoadItem(unsigned int id)
{
  SetViewerMode(ViewerMode::Models);
  ReleaseRider();
  canvas->clearAttachments();
  canvas->setModel(NULL);
  charControl->charAtt = nullptr;   // as LoadNPC
  charControl->model = nullptr;
  if (modelControl)
    modelControl->Forget();   // as LoadNPC
  canvas->ClearWMO();   // as LoadNPC

  isModel = true;
  isChar = false;
  isWMO = false;

  try
  {
    QString query = QString("SELECT ModelFileData.FileDataID, TextureFileData.FileDataID, ItemDisplayInfo.ID FROM ItemDisplayInfo "
                            "LEFT JOIN ModelFileData ON ItemDisplayInfo.ModelResourcesID1 = ModelFileData.ModelResourcesID "
                            "LEFT JOIN TextureFileData ON ItemDisplayInfo.ModelMaterialResourcesID1 = TextureFileData.MaterialResourcesID "
                            "WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ItemAppearance.ID = "
                            "(SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1))").arg(id);

    sqlResult itemInfos = GAMEDATABASE.sqlQuery(query);
    // LOG_INFO << query;

    if (itemInfos.valid && !itemInfos.empty())
    {
      // Some weapons (e.g. Jaina's staff) use textures embedded in the M2's
      // material definitions and have no replacement skin in ItemDisplayInfo.
      // Their model is still valid; apply an external skin only if one exists.
      if (itemInfos.values[0][0].toUInt() != 0)
      {
        LoadModel(GAMEDIRECTORY.getFile(itemInfos.values[0][0].toInt()));
        applyItemComponentGeosets(id);
        TextureGroup grp;
        grp.base = TEXTURE_OBJECT_SKIN;
        grp.count = 1;
        grp.tex[0] = GAMEDIRECTORY.getFile(itemInfos.values[0][1].toInt());
        if (grp.tex[0])
        {
          animControl->SetSkinByDisplayID(itemInfos.values[0][2].toInt());
          // Remember the applied skin texture so an out-of-process FBX export can re-bind it
          // in the child (a raw -mo reload would otherwise fall back to the model's DEFAULT skin,
          // exporting a different texture than the one on screen).
          m_exportItemSkinFileId = itemInfos.values[0][1].toInt();
        }
      }
    }

    charMenu->Enable(ID_SAVE_CHAR, false);
    charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    charMenu->Enable(ID_SHOW_EARS, false);
    charMenu->Enable(ID_SHOW_HAIR, false);
    charMenu->Enable(ID_SHOW_FACIALHAIR, false);
    charMenu->Enable(ID_SHOW_FEET, false);
    charMenu->Enable(ID_SHEATHE, false);
    charMenu->Enable(ID_CHAREYEGLOW, false);
    charMenu->Enable(ID_SAVE_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_EQUIPMENT, false);
    charMenu->Enable(ID_CLEAR_EQUIPMENT, false);
    charMenu->Enable(ID_LOAD_SET, false);
    charMenu->Enable(ID_LOAD_START, false);
    charMenu->Enable(ID_MOUNT_CHARACTER, false);
    charMenu->Enable(ID_CHAR_RANDOMISE, false);
    charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
  }
  catch (...) {}

  // applyItemComponentGeosets set this component's flags AFTER LoadModel's skin push went out, and
  // a skin push follows only when the display resolves a skin (SetSkinByDisplayID). Send the state
  // the host decided, so the Unity viewport draws the item the way the Geosets tab lists it.
  SendCurrentGeosetsToUnity();

  DisplayedContentChanged();
  CommitLayoutIfChanged();
}

// This is called when the user goes to File->Exit
void ModelViewer::OnExit(wxCommandEvent &event)
{
  if (event.GetId() == ID_FILE_EXIT) {
    video.render = false;
    //canvas->timer.Stop();
    canvas->Disable();
    Close(false);
  }
}

// File > Restart: quit and reopen the application in one click (no manual exit + relaunch).
void ModelViewer::OnRestart(wxCommandEvent & WXUNUSED(event))
{
  if (exportRunning() || !canCloseNow())
  {
    wxMessageBox(exportRunning() ? _("An export is still running: restart once it has finished.")
                                 : _("Restart once the viewer has finished what it is doing."),
                 _("Restart"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  if (wxMessageBox(_("Restart WoW Model Viewer now?\n\nThe current scene is reloaded fresh; your saved settings are kept."),
                   _("Restart"), wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION, this) != wxYES)
    return;
  Relaunch();
}

bool ModelViewer::exportRunning() const
{
  return m_exportJobManager && m_exportJobManager->hasActiveJobs();
}

bool ModelViewer::canCloseNow() const
{
  // Not from a modal dialog's loop (it disables the window; closing would delete the window under the
  // dialog) nor from inside a yield (code further up the stack still uses the window).
  const wxEventLoopBase * loop = wxEventLoopBase::GetActive();
  return ::IsWindowEnabled((HWND)GetHWND()) && !(loop && loop->IsYielding());
}

void ModelViewer::Relaunch()
{
  const wxString exe = wxStandardPaths::Get().GetExecutablePath();

  // Development build: a "_relaunch.bat" sits a few folders above the exe (next to _run.bat). It
  // waits for THIS instance to exit so its DLLs unlock, then redeploys the freshly-built DLLs and
  // launches a new instance -- so a rebuild is picked up and a new exe never runs against stale
  // DLLs. Installed builds don't ship that script, so we relaunch the exe directly (DLLs are
  // colocated, no redeploy needed).
  wxString relaunchBat;
  wxFileName probe(exe);
  for (int up = 0; up < 6 && probe.GetDirCount() > 0; ++up)
  {
    probe.RemoveLastDir();
    const wxFileName cand(probe.GetPath(), wxT("_relaunch.bat"));
    if (cand.FileExists()) { relaunchBat = cand.GetFullPath(); break; }
  }

  if (!relaunchBat.IsEmpty())
    wxExecute(wxString::Format(wxT("cmd /c \"\"%s\" %lu\""), relaunchBat, wxGetProcessId()), wxEXEC_ASYNC);
  else
  {
    // The new instance waits for this one to have exited (-waitpid, WowModelViewApp::OnInit) before it
    // reads Config.ini or opens the log: this one saves its layout and settings as it closes.
    wxString command = wxString::Format(wxT("\"%s\" -waitpid %lu"), exe, wxGetProcessId());
    for (int i = 1; i < wxTheApp->argc; i++)
      if (wxTheApp->argv[i] == wxT("-console"))
        command += wxT(" -console");
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    std::wstring line = command.ToStdWstring();
    if (!::CreateProcessW(nullptr, &line[0], nullptr, nullptr, FALSE, 0, nullptr, m_startDirectory.wc_str(), &startup, &process))
    {
      // This instance stays: closing it would leave nothing running.
      const DWORD error = ::GetLastError();
      LOG_ERROR << "Restart: could not start" << QString::fromWCharArray(command.wc_str()) << "error" << (unsigned)error;
      wxMessageBox(wxString::Format(_("The viewer could not be started again (Windows error %lu), so it stays open."), (unsigned long)error),
                   _("Restart"), wxOK | wxICON_ERROR, this);
      return;
    }
    ::AllowSetForegroundWindow(process.dwProcessId);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
  }

  // Tear down like File > Exit; OnClose saves the session as usual before the process exits.
  video.render = false;
  canvas->Disable();
  Close(false);
}

// This is called when the window is closing
void ModelViewer::OnClose(wxCloseEvent &event)
{
  // The background window goes with the frame, first: left open it would stay on screen, unresponsive, while the
  // frame tears down (the player's shutdown waits seconds), and be destroyed after the frame state it reads.
  if (m_backgroundDialog)
  {
    m_backgroundDialog->Destroy();
    m_backgroundDialog = nullptr;
  }
  Destroy();
}

// Called when the window is resized, minimised, etc
void ModelViewer::OnSize(wxSizeEvent &event)
{
  /* // wxIFM stuff
  if(!interfaceManager)
  event.Skip();
  else
  interfaceManager->Update(IFM_DEFAULT_RECT);
  */

  // wxAUI
  //interfaceManager.Update(); // causes an error?
}

ModelViewer::~ModelViewer()
{
  LOG_INFO << "Shutting down the program...";

  // Not closed through OnClose: the background window still goes before anything it reads.
  if (m_backgroundDialog)
  {
    delete m_backgroundDialog;
    m_backgroundDialog = nullptr;
  }

  // The menu bar's titles back to Windows' own before the window goes (wxFrame's own procedure would
  // take their data for its menu items).
  m_menuBarTitles.reset();

  video.render = false;

  // Tear down the export job manager (stops its poll timer; detaches any running children).
  if (m_exportJobManager) {
    delete m_exportJobManager;
    m_exportJobManager = nullptr;
  }
  // If we have a canvas (which we always should)
  // Stop rendering, give more power back to the CPU to close this sucker down!
  //if (canvas)
  //  canvas->timer.Stop();

  // Persist the GUI layout/session only for real interactive runs. A headless/CLI run
  // (FBX export child, test harness) parks its window off-screen at (-32000,-32000)
  // and never touches the UI, so saving here would overwrite the shared Config.ini with that
  // off-screen position -- which then strands the next interactive launch's window off-screen.
  if (!batchMode)
    SaveLayout();

  // Shut the embedded Unity player down (if one was ever opened) BEFORE the AUI teardown
  // destroys its host panel, so the child process never outlives -- or paints into -- a
  // dead window.
  if (unityRendererHost)
    unityRendererHost->shutdown();

  // wxAUI stuff (teardown, always run)
  interfaceManager.UnInit();

  if (!batchMode)
    SaveSession();

  // The texture view decodes through the canvas's GL context: stop it before that goes (the window
  // itself goes with the viewport's host, after the canvas).
  if (textureView)
    textureView->shutdown();

  if (canvas) {
    canvas->Disable();
    canvas->Destroy();
    canvas = NULL;
  }

  if (fileControl) {
    fileControl->Destroy();
    fileControl = NULL;
  }

  if (animControl) {
    animControl->Destroy();
    animControl = NULL;
  }

  if (charControl) {
    charControl->Destroy();
    charControl = NULL;
  }

  if (lightControl) {
    lightControl->Destroy();
    lightControl = NULL;
  }

  if (settingsControl) {
    settingsControl->Destroy();
    settingsControl = NULL;
  }

  if (modelControl) {
    modelControl->Destroy();
    modelControl = NULL;
  }

  // After the animation and character controls: some of their controls live on its pages.
  if (modelInspector) {
    modelInspector->Destroy();
    modelInspector = NULL;
  }

  if (unityRendererHost) {
    unityRendererHost->Destroy();
    unityRendererHost = NULL;
  }

  if (enchants) {
    enchants->Destroy();
    enchants = NULL;
  }
}

// The docked pane whose window holds the keyboard reads as active (UiDockArt draws its caption in the
// text colour with an accent line); while the window itself is not the active one, none does. Only the
// two captions involved are repainted: a whole-frame refresh would repaint the Unity viewport's window too.
void ModelViewer::OnChildFocus(wxChildFocusEvent & event)
{
  event.Skip();
  UpdateActivePaneCaption(m_frameActive);
}

void ModelViewer::OnActivateFrame(wxActivateEvent & event)
{
  event.Skip();
  m_frameActive = event.GetActive();
  UpdateActivePaneCaption(m_frameActive);
}

void ModelViewer::UpdateActivePaneCaption(bool frameActive)
{
  UiDockArt * art = dynamic_cast<UiDockArt *>(interfaceManager.GetArtProvider());
  if (!art)
    return;
  const wxAuiPaneInfoArray & panes = interfaceManager.GetAllPanes();
  wxString active;
  for (wxWindow * w = frameActive ? wxWindow::FindFocus() : nullptr; w && active.IsEmpty() && w != this; w = w->GetParent())
    for (size_t i = 0; i < panes.GetCount(); i++)
      if (panes[i].window == w && panes[i].HasCaption() && panes[i].IsShown() && panes[i].IsDocked())
      {
        active = panes[i].name;
        break;
      }
  const wxString before = art->activePane();
  if (!art->SetActivePane(active))
    return;
  // A pane's rect is its window's; the caption sits on top of it, inside the pane's border.
  const int caption = art->GetMetricForWindow(wxAUI_DOCKART_CAPTION_SIZE, this);
  const int border = art->GetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE);
  for (const wxString & name : { before, active })
  {
    if (name.IsEmpty())
      continue;
    const wxAuiPaneInfo & pane = interfaceManager.GetPane(name);
    if (pane.IsOk() && pane.IsShown() && pane.IsDocked())
      RefreshRect(wxRect(pane.rect.x - border, pane.rect.y - caption - border, pane.rect.width + 2 * border,
                         caption + border + 1), false);
  }
}

// Menu button press events
void ModelViewer::OnToggleDock(wxCommandEvent &event)
{
  int id = event.GetId();

  // A panel a viewer put away, shown (or hidden) by the user meanwhile: theirs again, left as they put it
  // when the viewer goes. Shown again, it comes back at the size it had. (The panels put away are the ones
  // whose toggles that viewer refuses -- needsModelViewer, needsUnityViewport -- so this is kept for safety,
  // not reached today.)
  const wxChar * putAway = id == ID_SHOW_ANIM ? wxT("animControl") : id == ID_SHOW_CHAR ? wxT("modelInspector")
                         : id == ID_SHOW_MODEL ? wxT("Models") : nullptr;
  WorkspacePaneRecord given;
  const bool wasPutAway = putAway && forgetWorkspacePane(putAway, &given);

  // wxAUI Stuff. Browse, Model and Animation toggle: the View menu items and the command bar
  // buttons are checked while their panel is shown (OnUpdateCommandUI).
  if (id == ID_SHOW_FILE_LIST) {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(fileControl);
    pane.Show(!pane.IsShown());
  }
  else if (id == ID_SHOW_ANIM) {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(animControl);
    pane.Show(!pane.IsShown());
  }
  else if (id == ID_SHOW_CHAR) {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(modelInspector);
    pane.Show(!pane.IsShown());
    // Opened in Textures mode -- not possible while its toggle is the Models viewer's (needsModelViewer) --
    // its Info would name the texture picked while it was away (TextureSelectionChanged rebuilds it only
    // while it is open).
    if (pane.IsShown() && isTextureMode())
      modelInspector->ContentChanged();
  }
  else if (id == ID_SHOW_MODEL) {
    interfaceManager.GetPane(modelControl).Show(true);
    modelControl->Update();
  }
  else if (id == ID_SHOW_SETTINGS) {
    interfaceManager.GetPane(settingsControl).Show(true);
    settingsControl->Open();
  }
  if (wasPutAway)
  {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(given.name);
    if (pane.IsOk() && pane.IsShown() && pane.IsDocked() && given.shownSize != wxDefaultSize)
      pane.BestSize(given.shownSize);
    commitDocksAtTheirSize();
    if (pane.IsOk())
      pane.BestSize(given.bestSize);
    return;
  }
  interfaceManager.Update();
}

// A panel closed with its own close button while a viewer has panels put away stays closed afterwards.
void ModelViewer::OnPaneClose(wxAuiManagerEvent & event)
{
  if (event.GetPane())
    forgetWorkspacePane(event.GetPane()->name);
  event.Skip();
}

int ModelViewer::workspacePanes(ViewerMode mode)
{
  // Textures: the Animation and Model panels (the Model panel's texture rows repeat what the texture view
  // shows) and the Attachments window. Buildings: the Animation panel and the Attachments window, which act on
  // a model (the Model panel shows a world model's information and doodad sets). Models: none. Never Browse or
  // Settings.
  switch (mode)
  {
    case ViewerMode::Textures:  return PaneAnimation | PaneModel | PaneAttachments;
    case ViewerMode::Buildings: return PaneAnimation | PaneAttachments;
    case ViewerMode::Models:    break;
  }
  return 0;
}

const wxChar * ModelViewer::workspacePaneName(int pane)
{
  switch (pane)
  {
    case PaneAnimation:   return wxT("animControl");
    case PaneModel:       return wxT("modelInspector");
    case PaneAttachments: return wxT("Models");
  }
  return nullptr;
}

void ModelViewer::applyWorkspace(int panes, bool commit)
{
  if (panes == m_workspace)
    return;
  // The first panel put away from the user's own layout: the sizes of the docks they take with them. A panel put
  // away later (Buildings, then Textures) takes its dock's size as it is now -- the user may have changed it
  // meanwhile -- and the docks gone before keep the sizes they had (userPerspective).
  if (m_workspace == 0 && m_workspaceRecord.empty())
    m_workspaceLayout = interfaceManager.SavePerspective();
  else if (!m_workspaceLayout.IsEmpty() && (panes & ~m_workspace) != 0)
  {
    wxString merged;
    for (const wxString & part : wxSplit(interfaceManager.SavePerspective(), '|', '\0'))
      if (part.StartsWith(wxT("dock_size(")))
        merged += part + wxT("|");
    for (const wxString & part : wxSplit(m_workspaceLayout, '|', '\0'))
      if (part.StartsWith(wxT("dock_size(")) && !merged.Contains(part.BeforeFirst('=') + wxT("=")))
        merged += part + wxT("|");
    m_workspaceLayout = merged;
  }
  // Given back: the panels put away now that the viewer coming uses. Its dock went with it, and one made
  // again is sized from the pane's best size (plus its border and caption): the window's size when it went
  // brings it back exactly as wide or tall.
  bool givenBack = false;
  for (int pane = PaneAnimation; pane <= PaneAttachments; pane <<= 1)
  {
    if (!(m_workspace & pane) || (panes & pane))
      continue;
    WorkspacePaneRecord p;
    if (!forgetWorkspacePane(workspacePaneName(pane), &p))
      continue;   // it was not shown when it went (or the user closed it meanwhile)
    wxAuiPaneInfo & info = interfaceManager.GetPane(p.name);
    if (!info.IsOk() || info.IsShown())
      continue;
    if (info.IsDocked() && p.shownSize != wxDefaultSize)
      info.BestSize(p.shownSize);
    info.Show(true);
    m_workspaceGivenBack.push_back(p);
    givenBack = true;
  }
  // Put away: the panels the viewer coming does not use, those shown recorded.
  int putAway = 0;
  for (int pane = PaneAnimation; pane <= PaneAttachments; pane <<= 1)
  {
    if ((m_workspace & pane) || !(panes & pane))
      continue;
    wxAuiPaneInfo & info = interfaceManager.GetPane(workspacePaneName(pane));
    if (!info.IsOk() || !info.IsShown())
      continue;
    WorkspacePaneRecord p;
    p.name = workspacePaneName(pane);
    p.bestSize = info.best_size;
    // (A window not laid out yet -- a panel ResetLayout has just added -- comes back at its best size.)
    p.shownSize = info.window && info.window->IsShown() ? info.window->GetSize() : wxDefaultSize;
    m_workspaceRecord.push_back(p);
    info.Show(false);
    putAway++;
  }
  m_workspace = panes;
  LOG_INFO << "[viewport] workspace:" << putAway << "panels put away," << (int)m_workspaceGivenBack.size()
           << "given back," << (int)m_workspaceRecord.size() << "kept away";
  if (givenBack)
  {
    commitDocksAtTheirSize();
    if (m_layoutBatch > 0)
      m_finishWorkspacePending = true;   // after the batch's one Update
    else
      finishWorkspace();
  }
  else if (commit && putAway > 0)
    CommitLayoutIfChanged();
}

void ModelViewer::finishWorkspace()
{
  // The docks have their size now; each pane's own best size is what it was, so nothing else changes.
  for (const WorkspacePaneRecord & p : m_workspaceGivenBack)
  {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(p.name);
    if (pane.IsOk())
      pane.BestSize(p.bestSize);
  }
  m_workspaceGivenBack.clear();
  if (m_workspaceRecord.empty())
    m_workspaceLayout.Clear();
}

bool ModelViewer::forgetWorkspacePane(const wxString & name, WorkspacePaneRecord * taken)
{
  for (size_t i = 0; i < m_workspaceRecord.size(); i++)
    if (m_workspaceRecord[i].name == name)
    {
      if (taken)
        *taken = m_workspaceRecord[i];
      m_workspaceRecord.erase(m_workspaceRecord.begin() + i);
      return true;
    }
  return false;
}

void ModelViewer::commitDocksAtTheirSize()
{
  if (m_layoutBatch > 0)
  {
    m_layoutPending = true;
    m_layoutUncapped = true;
    return;
  }
  // wxAUI caps only a dock it makes anew, and only in this layout: the cap goes back right after.
  double capX = 0.3, capY = 0.3;
  interfaceManager.GetDockSizeConstraint(&capX, &capY);
  interfaceManager.SetDockSizeConstraint(1.0, 1.0);
  interfaceManager.Update();
  interfaceManager.SetDockSizeConstraint(capX, capY);
}

wxString ModelViewer::userPerspective()
{
  if (m_workspaceRecord.empty())
    return interfaceManager.SavePerspective();
  // The panels a viewer put away, set shown for the saving only (nothing is laid out, so nothing on screen
  // changes), and the sizes of the docks they took, which went with them.
  // (The panes, not copies of their names: wxWidgets 3.3 exports std::vector<wxString> from its DLL,
  // whose prebuilt binaries lack members a newer MSVC library calls.)
  std::vector<const WorkspacePaneRecord *> hidden;
  for (const WorkspacePaneRecord & p : m_workspaceRecord)
  {
    wxAuiPaneInfo & pane = interfaceManager.GetPane(p.name);
    if (!pane.IsOk() || pane.IsShown())
      continue;
    pane.Show(true);
    hidden.push_back(&p);
  }
  wxString perspective = interfaceManager.SavePerspective();
  for (const WorkspacePaneRecord * p : hidden)
    interfaceManager.GetPane(p->name).Show(false);
  for (const wxString & part : wxSplit(m_workspaceLayout, '|', '\0'))
  {
    if (!part.StartsWith(wxT("dock_size(")))
      continue;
    if (!perspective.Contains(part.BeforeFirst('=') + wxT("=")))
      perspective += part + wxT("|");
  }
  return perspective;
}

// The Unity viewport's docking setup, shared by InitDocking, ResetLayout and LoadLayout. A centre
// pane has no caption and no close button, so the user cannot close it; nothing else docks there.
wxAuiPaneInfo ModelViewer::unityViewportPaneInfo() const
{
  return wxAuiPaneInfo().
         Name(wxT("unityRenderer")).Caption(wxT("Viewport")).
         CenterPane().Show(true);
}

void ModelViewer::CreateUnityViewport()
{
  if (unityRendererHost)
    return;
  unityRendererHost = new UnityRendererHost(this, ID_UNITY_FRAME);
  // Under the player while it starts, and on its command line, so the first frame is already this colour.
  unityRendererHost->setBackdrop(m_viewportBackground);

  // A texture picked in Browse is shown in the viewport's place.
  textureView = new TextureView(unityRendererHost, this);
  unityRendererHost->setContent(textureView);
  textureView->onLookupResolved = [this](int fileDataId, const QString & indexName) {
    if (fileControl)
      fileControl->TextureLookupResolved(fileDataId, indexName);
  };

  // Runtime IPC: as soon as the player announces itself, tell it what is on the canvas.
  unityRendererHost->ipc()->onUnityReady = [this]() {
    // A (re)started player knows nothing of the states sent to the one before it; the model push
    // below carries the current state, and its build answers for it.
    if (modelInspector)
      modelInspector->UnityPlayerRestarted();
    m_sceneAwaitingRevision = 0;
    // ... nor could the one before it build a character this one never tried: a player rebuilt or
    // restarted after a failed build is given the character again.
    m_unityCharacterFailed = 0;
    m_unityCharacterFailReason.clear();
    // ... nor a world model (it is loaded again below, and the new player answers for it).
    m_unityWmoFailed = 0;
    m_unityWmoFailReason.clear();
    // A new player shows nothing yet, and no answer from the one before it is awaited any more. Nor does it hold any
    // asset file: the character's other model is hinted again once it is on screen.
    m_playerContent = PlayerContent::Nothing;
    m_assetHintLoad = 0;
    m_mapObjectAwaited = 0;
    m_uncoverFence = 0;
    m_modelAwaited = 0;
    m_modelAwaitedFailed = 0;
    m_playerContentPoll.Stop();
    // The background before the model: a player started before the colour last changed has an older one from its
    // command line, and a restarted one may have been started by another run's settings.
    m_viewportBackgroundSent = wxColour();
    pushViewportBackground();
    // Ready after the colour: its window is shown then, so its first frames have it (setPlayerReady).
    if (unityRendererHost)
      unityRendererHost->setPlayerReady(true);
    SendCurrentModelToUnity();
    // What the player announced may change what the viewport can show: a character gets a notice
    // when the player is an older build that cannot dress it. With nothing loaded, the empty
    // viewer's prompt simply stays.
    UpdateUnityViewportState();
    // The character panel's Model selector depends on the player too (characterVariantState).
    if (charControl)
      charControl->SyncModelVariant();
  };
  unityRendererHost->ipc()->onCharacterSceneApplied = [this](const UnityIpcServer::SceneAck & ack) {
    OnCharacterSceneApplied(ack);
  };
  unityRendererHost->ipc()->onMapObjectLoaded = [this](const UnityIpcServer::MapObjectReport & report) {
    OnMapObjectLoaded(report);
  };
  // ... and what it shows, while a model built after a world model is awaited (OnPlayerContentPoll).
  unityRendererHost->ipc()->onRuntimeState = [this](const UnityIpcServer::RuntimeState & state) {
    OnPlayerRuntimeState(state);
  };
  // ... and what it did with a geoset state, so the Geosets checkboxes follow the renderer.
  unityRendererHost->ipc()->onGeosetsApplied = [this](const UnityIpcServer::GeosetAck & ack) {
    if (modelInspector)
      modelInspector->OnUnityGeosetsApplied(ack);
  };
  // ... and what became of a screenshot, for the status bar.
  unityRendererHost->ipc()->onScreenshotSaved = [this](const UnityIpcServer::ScreenshotResult & result) {
    OnUnityScreenshotSaved(result);
  };
}

bool ModelViewer::EnsureUnityViewportCentre()
{
  if (!unityRendererHost)
    return false;
  if (isUnityViewportCentre())
    return false;
  interfaceManager.DetachPane(unityRendererHost);
  interfaceManager.AddPane(unityRendererHost, unityViewportPaneInfo());
  return true;
}

void ModelViewer::OnBackgroundColor(wxCommandEvent & WXUNUSED(event))
{
  if (!m_backgroundDialog)
  {
    m_backgroundDialog = new BackgroundColorDialog(this);
    if (unityRendererHost)
      m_backgroundDialog->placeBeside(unityRendererHost->GetScreenRect());
  }
  m_backgroundDialog->Show();
  m_backgroundDialog->Raise();
  m_backgroundDialog->opened();
}

void ModelViewer::OnRestartUnityRenderer(wxCommandEvent & WXUNUSED(event))
{
  RestartUnityRenderer();
}

void ModelViewer::RestartUnityRenderer()
{
  if (!unityRendererHost)
    return;
  LOG_INFO << "Restarting the Unity renderer.";
  // A player that is still running (frozen, disconnected, or simply asked to restart) is closed first:
  // launch() is a no-op while a process is alive.
  unityRendererHost->shutdown();
  StartUnityRenderer();
  // The notice follows at once: the restart's own outcome (a missing build is reported again), or the
  // content state. The player, once connected, is sent the current model by onUnityReady.
  UpdateUnityViewportState();
  if (charControl)
    charControl->SyncModelVariant();
}

void ModelViewer::setViewportBackground(const wxColour & colour, bool persist)
{
  if (!colour.IsOk())
    return;
  const wxColour opaque(colour.Red(), colour.Green(), colour.Blue());
  if (!ViewportBackground::sameColour(opaque, m_viewportBackground))
  {
    m_viewportBackground = opaque;
    LOG_INFO << "[viewport] background" << QString::fromWCharArray(ViewportBackground::formatHex(opaque).c_str());
    // On show at once behind a model; a world model keeps the viewport's default (viewportBackgroundShown).
    pushViewportBackground();
    if (m_backgroundDialog)
      m_backgroundDialog->Sync();
  }
  if (persist && !ViewportBackground::sameColour(opaque, m_viewportBackgroundKept))
  {
    ViewportBackground::saveColour(opaque);
    m_viewportBackgroundKept = opaque;
  }
}

wxColour ModelViewer::viewportBackgroundShown() const
{
  return canvasHasWorldModel() ? ViewportBackground::defaultColour() : m_viewportBackground;
}

bool ModelViewer::pushViewportBackground()
{
  const wxColour shown = viewportBackgroundShown();
  if (!shown.IsOk() || !unityRendererHost)
    return false;
  // Under the player and on the command line of one started later, whatever is sent.
  unityRendererHost->setBackdrop(shown);
  // A player not ready yet gets it on connect (onUnityReady); one older than protocol 7 keeps its own default.
  if (m_viewportBackgroundSent.IsOk() && ViewportBackground::sameColour(shown, m_viewportBackgroundSent))
    return false;
  if (!unityRendererHost->ipc() || !unityRendererHost->ipc()->isUnityReady())
    return false;
  if (!unityRendererHost->ipc()->sendViewportBackground(shown.Red(), shown.Green(), shown.Blue()))
    return false;
  m_viewportBackgroundSent = shown;
  return true;
}

bool ModelViewer::StartUnityRenderer(bool selfTest)
{
  if (!unityRendererHost)
    return false;
  if (unityRendererHost->isRunning())
    return true;
  // The host panel is already laid out as the centre pane, so the player parents itself to a window
  // of the right size from its first frame.
  return unityRendererHost->launch(selfTest);
}

// The half of the viewer-first startup that touches NO player and NO IPC: take the screen. Runs
// BEFORE the client is loaded, which is what makes the first thing on screen a clean fullscreen
// viewer instead of a small window behind a dialog.
void ModelViewer::ApplyViewerStartupLayout()
{
  if (batchMode || !canvas)
    return;

  // The panels come up as they were left: Browse, Model and Animation keep the shown state the
  // saved layout restored (LoadLayout), all three on a first run. The empty viewport says what to
  // do next itself (the Unity viewport's notice), so hiding the panels that do it is no longer the
  // way to make an empty application look tidy.

  // Take the screen. A viewer that opens in a small window in the corner is not one.
  EnterViewerFullScreen(true);
}

// Start the player, at launch, before any client is loaded.
//
// It used to be created and launched by the first model load that wanted it, which meant the user
// picked a creature and then watched a game engine boot -- process start, engine init and the
// player's own splash -- with the model appearing only afterwards. None of that has anything to do
// with the model, so none of it belongs in front of one. Done here it happens while the user is
// still looking at an empty app, and by the time a creature is picked the player is already
// connected and waiting.
//
// Nothing it does needs game data: it talks to WMV over the local IPC channel and is told what to
// show, so with nothing loaded it simply sits connected and idle behind the empty viewer's prompt.
//
// Loading a client afterwards, with the player already running, is the normal case and was
// verified as such -- unityReady lands in the middle of CASC and database initialisation and the
// load completes untroubled. (The launch crash this branch had along the way was a double client
// load, and it reproduced with no player running at all.)
//
// A player build that is missing or will not start is not a dialog: the viewport's notice says why
// and offers a restart.
void ModelViewer::WarmStartUnityViewport()
{
  if (batchMode)
    return;
  if (!StartUnityRenderer())
    LOG_INFO << "Unity viewport not started:" << QString::fromWCharArray(unityRendererHost ?
                                                   unityRendererHost->playerProblem().c_str() : L"no host");
  UpdateUnityViewportState();
}

// Borderless fullscreen, keeping the MENU BAR.
//
// Dropping the caption removes the window's own close and restore buttons, so without the menu
// there would be no visible way back out -- and no way to reach View > "Restart Unity Renderer"
// either. Keeping it costs one strip of chrome and means the mode can always be left: F11 or Esc
// from anywhere, or View > Fullscreen.
void ModelViewer::EnterViewerFullScreen(bool full)
{
  if (IsFullScreen() == full)
    return;
  ShowFullScreen(full, wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
}

void ModelViewer::OnToggleFullScreen(wxCommandEvent & WXUNUSED(event))
{
  EnterViewerFullScreen(!IsFullScreen());
}

// Esc leaves fullscreen. Only that -- it is a way out, not a shortcut for anything else, and it
// does nothing at all when the window is not fullscreen.
void ModelViewer::OnCharHook(wxKeyEvent & event)
{
  if (event.GetKeyCode() == WXK_ESCAPE && IsFullScreen())
  {
    EnterViewerFullScreen(false);
    return;
  }
  // A texture in the viewport is exported from wherever the keyboard is, Browse included (the texture
  // view takes the same keys itself when it has the keyboard).
  if (isTextureMode() && textureView && event.ControlDown() && !event.AltDown() && event.GetKeyCode() == 'S')
  {
    if (event.ShiftDown())
      textureView->exportBlpInteractive();
    else
      textureView->exportPngInteractive();
    return;
  }
  event.Skip();
}

// WHAT THE UNITY PLAYER CAN DRAW. Every M2 addressed by FileDataID, playable characters included:
// the character's resolved appearance, merged armour and attached items reach it as a characterScene
// (protocol 3). A world model (WMO) root addressed by FileDataID, as static group geometry with a
// provisional material and no doodads, liquids or lights yet (protocol 4). A character riding a mount:
// the canvas model is then the mount, with the character hung from one of its attachments, and the player
// is still loaded with the character and seats it on the mount its scene describes (protocol 5). What it
// cannot draw yet, each with the notice the viewport shows instead:
//   - an image picked in Browse;
//   - a WMO whose root the host could not read, one with no FileDataID (a legacy client), one the player
//     reported it could not build (while that load is on display), and any WMO when the connected
//     player is an older build that cannot draw world models;
//   - a character riding a mount when the connected player is an older build that cannot seat one, or
//     when the rider is not a character model with a FileDataID;
//   - a model with no FileDataID (a legacy MPQ client): the player addresses every asset by one;
//   - a character the player reported it could not build, until another load or a player restart;
//   - a character when the connected player is an older build that cannot dress one.
// A player that is not connected yet is assumed to be the current build: this decides before the
// player exists, and a player that then turns out older gets the notice when it announces itself
// (onUnityReady). Nothing loaded is not drawable either, but has no notice of its own here: the
// empty viewer's prompt is the caller's (unityViewportNotice).
bool ModelViewer::unityCanDrawCurrentModel(ViewportNotice * notice) const
{
  ViewportNotice ignored;
  ViewportNotice & out = notice ? *notice : ignored;
  out = ViewportNotice();
  const auto fileName = [](const wxString & path) {
    wxString name = path;
    name.Replace(wxT("/"), wxT("\\"));
    return name.AfterLast('\\');
  };

  if (!canvas)
    return false;
  // The flags as well as the pointers, as everything else that says what is loaded tests them
  // (DisplayedContentChanged, UpdateStatusFacts, the Model panel): a pointer left behind by a load that
  // did not clear it must never put a notice in front of the model that replaced it.
  if (isWMO && canvas->wmo)
  {
    const WMO * w = canvas->wmo;
    const wxString name = fileName(wxString(const_cast<WMO *>(w)->itemName().toStdWString()));
    // The host reads the same root the player would fetch: one it cannot open or that is not a root is
    // not worth a load the player can only fail.
    if (!w->ok)
    {
      out.title = _("World model cannot be read");
      out.detail = wxString::Format(_("%s could not be read as a world model (WMO) root, so there is nothing "
                                      "to show."), name);
      return false;
    }
    if (w->fileDataID == 0)
    {
      out.title = _("Legacy client world model");
      out.detail = wxString::Format(_("%s has no FileDataID (it comes from a legacy client). The Unity viewport "
                                      "can only show world models that have one."), name);
      return false;
    }
    if (m_unityWmoFailed != 0 && m_unityWmoFailed == (int)w->fileDataID && m_unityWmoFailedLoad == m_unityLoadSerial)
    {
      out.title = _("World model could not be built");
      out.detail = m_unityWmoFailReason.isEmpty()
        ? wxString::Format(_("The Unity viewport could not build %s."), name)
        : wxString::Format(_("The Unity viewport could not build %s (%s)."), name,
                           wxString(m_unityWmoFailReason.toStdWString()));
      return false;
    }
    // As for characters: a player not known yet is assumed current, and one that announces an older
    // protocol gets this notice when it does (onUnityReady decides again).
    const bool playerKnown = unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady();
    if (playerKnown && !unityRendererHost->ipc()->playerDrawsMapObjects())
    {
      out.title = _("Unity renderer out of date");
      out.detail = wxString::Format(_("The Unity renderer build in use cannot show world models. Rebuild the player "
                                      "from Tools\\UnityRendererProject to show %s."), name);
      return false;
    }
    return true;
  }
  if (!canvas->model())
    return false;   // nothing loaded: the empty viewer, not a notice about content
  const WoWModel * m = canvas->model();
  wxString name = m->gamefile ? fileName(wxString(m->gamefile->fullname().toStdWString()))
                              : wxString(const_cast<WoWModel *>(m)->name().toStdWString());
  if (!m->gamefile)
  {
    out.title = _("Model cannot be shown");
    out.detail = wxString::Format(_("%s has no game file the Unity viewport can load."), name);
    return false;
  }
  if (isChar && !m->charModelDetails.isChar)
  {
    // A MOUNTED CHARACTER. As for characters and world models, a player not known yet is assumed current, and
    // one that announces an older protocol gets this notice when it does (onUnityReady decides again).
    const bool playerKnown = unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady();
    if (!canvasShowsMountedCharacter() || (playerKnown && !unityRendererHost->ipc()->playerRidesMounts()))
    {
      out.title = _("Mounted character");
      out.detail = _("The character is riding a mount. The Unity viewport cannot show mounted characters yet; "
                     "dismount to see the character again.");
      return false;
    }
    // From here on the question is about the rider, which is what the player is loaded with and dresses; its
    // mount travels in the rider's scene.
    m = riderModel();
    name = fileName(wxString(m->gamefile->fullname().toStdWString()));
  }
  if (m->gamefile->fileDataId() <= 0)
  {
    out.title = _("Legacy client model");
    out.detail = wxString::Format(_("%s has no FileDataID (it comes from a legacy client). The Unity viewport "
                                    "can only show models that have one."), name);
    return false;
  }
  if (!isChar)
    return true;
  // From here m is the character: a model, its game file, isChar, a character model -- the canvas model, or
  // the rider of a mount when the player is not known to be too old to seat it -- and a FileDataID were all
  // established above.
  if (m_unityCharacterFailed != 0 && m_unityCharacterFailed == (int)m->gamefile->fileDataId())
  {
    out.title = _("Character could not be built");
    out.detail = m_unityCharacterFailReason.isEmpty()
      ? wxString::Format(_("The Unity viewport could not build %s."), name)
      : wxString::Format(_("The Unity viewport could not build %s (%s)."), name,
                         wxString(m_unityCharacterFailReason.toStdWString()));
    return false;
  }
  const bool playerKnown = unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady();
  if (playerKnown && !unityRendererHost->ipc()->playerDressesCharacters())
  {
    out.title = _("Unity renderer out of date");
    out.detail = wxString::Format(_("The Unity renderer build in use cannot dress characters. Rebuild the player "
                                    "from Tools\\UnityRendererProject to show %s."), name);
    return false;
  }
  return true;
}

// THE WHOLE VIEWPORT DECISION, most fundamental first: a platform with no player, texture services
// that never started (nothing can be prepared for the player then), a player that is missing or has
// stopped, content the player cannot draw yet, and finally nothing loaded (the empty viewer's prompt).
bool ModelViewer::unityViewportNotice(ViewportNotice & notice) const
{
  notice = ViewportNotice();
#ifndef _WINDOWS
  notice.title = _("Unity viewport unavailable");
  notice.detail = _("The Unity viewport is not available on this platform.");
  return true;
#else
  if (!canvas || !video.render || !canvas->init)
  {
    notice.title = _("Textures cannot be decoded");
    notice.detail = _("The OpenGL services this viewer decodes textures with did not start, so nothing can be "
                      "prepared for the Unity viewport. Check the graphics driver, then restart the application.");
    return true;
  }
  if (unityRendererHost && !unityRendererHost->playerProblem().IsEmpty())
  {
    notice.title = _("The Unity renderer is not running");
    notice.detail = unityRendererHost->playerProblem();
    notice.actionLabel = _("Restart Unity renderer");
    notice.actionId = ID_VIEW_UNITY_RESTART;
    return true;
  }
  // The viewer's own kind of content only: a model in Models, a world model in Buildings. What the canvas holds
  // of the other kind stays loaded but is not shown -- the mode's empty viewer is.
  const bool buildings = m_viewerMode == ViewerMode::Buildings;
  if (canvasHoldsModesContent())
  {
    ViewportNotice content;
    if (unityCanDrawCurrentModel(&content))
    {
      // Drawable -- unless the player is still building it after showing the other viewer's kind, which must
      // not pass for it meanwhile (PlayerContent).
      const WoWModel * shownModel = buildings ? nullptr : canvas->model();
      const QString name = buildings ? canvas->wmo->itemName()
                         : shownModel ? (shownModel->gamefile ? shownModel->gamefile->fullname() : shownModel->name())
                                      : QString();
      const wxString shortName = wxString(name.toStdWString()).AfterLast('/').AfterLast('\\');
      if (buildings && m_mapObjectAwaited != 0 && m_mapObjectAwaited == m_unityLoadSerial)
      {
        notice.title = _("Loading building");
        notice.detail = wxString::Format(_("The Unity viewport is building %s."), shortName);
        return true;
      }
      if (!buildings && m_modelAwaited != 0)
      {
        notice.title = _("Loading model");
        notice.detail = wxString::Format(_("The Unity viewport is building %s."), shortName);
        return true;
      }
      if (!buildings && m_modelAwaitedFailed != 0 && shownModel && shownModel->gamefile &&
          (int)shownModel->gamefile->fileDataId() == m_modelAwaitedFailed)
      {
        notice.title = _("Model could not be built");
        notice.detail = wxString::Format(_("The Unity viewport could not build %s."), shortName);
        return true;
      }
      return false;
    }
    if (!content.title.IsEmpty())
    {
      notice = content;
      return true;
    }
  }
  // Nothing of the mode's kind loaded: the empty viewer's prompt, which depends on whether a client is loaded yet.
  const bool client = UnityAssetAccess::hasActiveClient();
  if (buildings)
  {
    notice.title = _("No building selected");
    notice.detail = client ? _("Select a building in Browse, or search for one by name, path or FileDataID.")
                           : _("Load a World of Warcraft client to browse its buildings.");
    notice.actionLabel = client ? _("Browse buildings") : _("Load World of Warcraft...");
    notice.actionId = ID_UI_BUILDINGS;
    return true;
  }
  notice.title = _("No model loaded");
  if (client)
  {
    notice.detail = _("Choose a model in Browse, or search for one by name.");
    notice.actionLabel = _("Browse models");
  }
  else
  {
    notice.detail = _("Load a World of Warcraft client to browse its models.");
    notice.actionLabel = _("Load World of Warcraft...");
  }
  notice.actionId = ID_UI_MODELS;
  return true;
#endif
}

// The scene and the player's asset requests both address files by FileDataID, so a character with
// none (a legacy MPQ client) is not one the viewport can dress.
bool ModelViewer::canvasShowsCharacter() const
{
  return isChar && canvas && canvas->model() && canvas->model()->charModelDetails.isChar &&
         canvas->model()->gamefile && canvas->model()->gamefile->fileDataId() > 0;
}

// CharControl::UpdateModel takes the model and its node together (charcontrol.cpp), and mounting changes
// neither: the mount choice puts the mount on the canvas root, the node's parent. The node is compared with
// the model's own pointer back to its node before anything reads through it: clearing the canvas deletes the
// node and leaves charAtt dangling until the next load, but the model's pointer back to it is cleared with it
// (Attachment::~Attachment), so the two no longer agree. A LOAD frees the model itself as well, which no
// comparison here could survive, so ModelViewer::LoadModel nulls both the moment the canvas may have freed
// them.
WoWModel * ModelViewer::riderModel() const
{
  if (!isChar || !charControl || !charControl->model || !charControl->charAtt)
    return nullptr;
  if (charControl->model->attachment != charControl->charAtt)
    return nullptr;
  return charControl->model;
}

WoWModel * ModelViewer::riderMount() const
{
  WoWModel * rider = riderModel();
  if (!rider || !charControl->charAtt->parent)
    return nullptr;
  WoWModel * mount = dynamic_cast<WoWModel *>(charControl->charAtt->parent->model());
  return mount != rider ? mount : nullptr;
}

bool ModelViewer::canvasShowsMountedCharacter() const
{
  const WoWModel * rider = riderModel();
  return rider && rider->charModelDetails.isChar && rider->gamefile && rider->gamefile->fileDataId() > 0 &&
         riderMount() != nullptr;
}

bool ModelViewer::unityPlayerRidesMounts() const
{
  return unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->playerRidesMounts();
}

WoWModel * ModelViewer::unityCharacter() const
{
  if (canvasShowsCharacter())
    return const_cast<WoWModel *>(canvas->model());
  if (canvasShowsMountedCharacter() && unityPlayerRidesMounts())
    return riderModel();
  return nullptr;
}

bool ModelViewer::unityPlayerDressesCharacters() const
{
  return unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->playerDressesCharacters();
}

WoWModel * ModelViewer::unityEquipmentOwner() const
{
  if (WoWModel * character = unityCharacter())
    return character;
  if (!unityRendererHost || !unityRendererHost->ipc() ||
      !unityRendererHost->ipc()->playerAttachesNpcEquipment() || isWMO ||
      !canvas || !canvas->model() || !charControl)
    return nullptr;
  WoWModel * model = const_cast<WoWModel *>(canvas->model());
  // SetHandsOnly supplies equipment controls for exclusive NPCs. Require the live
  // canvas model and its own node, never a stale character or a rider's parent.
  if (model->charModelDetails.isChar || charControl->model != model ||
      !model->attachment || charControl->charAtt != model->attachment ||
      !model->gamefile || model->gamefile->fileDataId() <= 0)
    return nullptr;
  return model;
}

// THE WHOLE WINDOW BLINKED ON EVERY MODEL LOAD, and this is why. On Windows, wxAuiManager::Update()
// wraps its relayout in a wxWindowUpdateLocker on the frame (wx 3.3.3 as 3.2.10, framemanager.cpp: "only
// under MSW and only when not using live resizing" -- which this manager does not use). The lock
// is Freeze/Thaw, and wxWindowMSW::DoThaw is SendSetRedraw(true) followed by Refresh(), which is
// RedrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE): every window in the frame is
// invalidated and ERASED, the embedded player's child window included, and everything repaints.
// The load path called Update() two or three times per model -- after the animation control,
// from FileControl::UpdateInterface, from the viewport routing -- with nothing to lay out: the same
// panes, in the same places, at the same sizes.
//
// So the layout is committed only when something a load can change has changed: whether a pane
// is shown (the character panel comes and goes with character models; a floating pane may have
// been floated and not yet given its frame). Everything else that moves a pane -- a drag, a
// detach-and-add, the perspective loader -- calls Update() itself, where the change is made.
bool ModelViewer::CommitLayoutIfChanged()
{
  wxAuiPaneInfoArray & panes = interfaceManager.GetAllPanes();
  bool changed = false;
  for (size_t i = 0; i < panes.GetCount() && !changed; i++)
  {
    wxAuiPaneInfo & p = panes.Item(i);
    if (!p.window)
      continue;
    if (p.IsFloating())
      changed = !p.frame || (p.frame->IsShown() != p.IsShown());
    else
      changed = (p.window->IsShown() != p.IsShown());
  }
  if (changed)
  {
    if (m_layoutBatch > 0)
      m_layoutPending = true;
    else
      interfaceManager.Update();
  }
  return changed;
}

// What the viewport shows is decided here and nowhere else. It is not a routing: the Unity viewport
// is the only viewport, and the archived OpenGL canvas behind it still loads the model, owns the
// animation clock and is what every Send*ToUnity call reads from -- it is simply never shown. Content
// the player cannot draw yet stays loaded (the panels, exports and Info keep working on it) and the
// viewport paints a notice in its place instead -- the player is only taken off screen (its frame parked), not
// closed -- so the next drawable model is on screen as soon as it is built.
//
// Cheap, and safe from the canvas tick: it launches nothing and opens no dialog, and setting the
// notice already on screen again repaints nothing.
void ModelViewer::UpdateUnityViewportState()
{
  // Recorded for the tick, which compares against it to notice a mount or a dismount
  // (SendCharacterSceneToUnity). Recorded before any early return: a load decides here, and the tick
  // that follows it must not decide it again.
  m_lastShowsCharacter = canvasShowsCharacter();
  m_lastShowsMountedCharacter = canvasShowsMountedCharacter();
  if (!unityRendererHost)
    return;

  // A texture picked in Browse: the texture view covers the viewport. It needs no player, so nothing
  // about the player -- missing, stopped, still starting -- is said over it.
  if (isTextureMode() && textureView)
  {
    const bool changed = !unityRendererHost->isShowingContent();
    unityRendererHost->showContent(true);
    unityRendererHost->clearNotice();
    // The model's panels go once the texture covers the viewport, so the player is never seen
    // resized to the larger area.
    applyWorkspace(workspacePanes(ViewerMode::Textures));
    if (changed)
    {
      LOG_INFO << "[viewport] showing a texture";
      if (modelInspector)
        modelInspector->ViewportNoticeChanged();
    }
    return;
  }

  ViewportNotice notice;
  bool changed = unityRendererHost->isShowingContent();
  if (changed)
    LOG_INFO << "[viewport] the texture view is put away";
  if (unityViewportNotice(notice))
  {
    const bool newNotice = !unityRendererHost->hasNotice() || unityRendererHost->noticeTitle() != notice.title;
    if (newNotice)
      LOG_INFO << "[viewport] notice:" << QString::fromWCharArray(notice.title.c_str()) << "--"
               << QString::fromWCharArray(notice.detail.c_str());
    changed = changed || newNotice;
    unityRendererHost->setNotice(notice.title, notice.detail, notice.actionLabel, notice.actionId);
  }
  else
  {
    if (unityRendererHost->hasNotice() || unityRendererHost->isShowingContent())
    {
      LOG_INFO << "[viewport] showing the model";
      changed = true;
    }
    unityRendererHost->clearNotice();
  }
  // The mode's panels come back (or go) first, while the texture view still covers the viewport, so the
  // player is uncovered at its final size; then it is put away, after the notice is decided, so the
  // player's window is not shown for a moment between.
  applyWorkspace(workspacePanes(m_viewerMode));
  if (m_layoutBatch > 0)
    m_uncoverPending = true;   // SetViewerMode puts it away after its one layout
  else
    unityRendererHost->showContent(false);
  // The Geosets tab says whether the viewport shows the model; a stopped or restarted player changes
  // that without a load, so the tab is told here rather than only on content changes.
  if (changed && modelInspector)
    modelInspector->ViewportNoticeChanged();
}

// Just the load. LoadModel sends this before the animation control initialises, so that the
// selection and state the control then pushes are about a model the player already expects.
void ModelViewer::SendLoadToUnity()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return;
  if (isWMO && canvas && canvas->wmo)
  {
    // A WORLD MODEL: its root, by FileDataID, and nothing after it -- no skin, animation or geoset push
    // exists for a WMO. Only to a player that has said it draws them; a player not ready yet is sent
    // this by onUnityReady, and an older one gets the out-of-date notice instead.
    WMO * w = canvas->wmo;
    UnityIpcServer * ipc = unityRendererHost->ipc();
    if (!ipc->playerDrawsMapObjects() || !w->ok || w->fileDataID == 0)
    {
      LOG_INFO << "[unity-wmo] not sending" << w->itemName() << "to the player:"
               << (!ipc->isUnityReady() ? "the player has not announced itself yet"
                   : !ipc->playerDrawsMapObjects() ? "the player is older than protocol 4"
                   : !w->ok ? "the root could not be read" : "the root has no FileDataID");
      return;
    }
    // Its colour first (viewportBackgroundShown): the player draws it on that from its first frame. And, parked, the
    // viewport's size: it frames what it builds by its own aspect.
    const bool recoloured = pushViewportBackground();
    unityRendererHost->sizePlayerForLoad();
    const int load = ++m_unityLoadSerial;
    ipc->sendLoadWoWModel(w->itemName(), (int)w->fileDataID, QStringLiteral("active"), false, load,
                          QStringLiteral("wmo"));
    m_unityLoadedFileDataID = (int)w->fileDataID;
    m_unityLoadedCharacter = false;
    // Until it is built the player shows what it had: awaited, unless that is known to be a world model too
    // (PlayerContent); what the player shows is then uncertain until this load is answered.
    // (Nor while what it shows has just been re-coloured: that is not on screen before the new one is built.)
    // (Nor while an earlier answer's fence is pending: what it shows is not known to be on screen yet.)
    const bool sameKind = m_playerContent == PlayerContent::MapObject && m_modelAwaited == 0 && !recoloured &&
                          m_uncoverFence == 0;
    m_mapObjectAwaited = sameKind ? 0 : load;
    m_uncoverFence = 0;
    if (!sameKind)
      m_playerContent = PlayerContent::Unknown;
    m_modelAwaited = 0;
    m_modelAwaitedFailed = 0;
    m_playerContentPoll.Stop();
    // Whatever a character scene was waiting for belongs to a load the player now drops.
    m_lastSceneSignature = 0;
    m_sceneAwaitingRevision = 0;
    m_unityCharacterFailed = 0;
    return;
  }
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return;
  // A CHARACTER RIDING A MOUNT is loaded as the character -- the rider, not the canvas model -- when the player
  // seats characters on mounts (protocol 5): the mount travels in the rider's scene. An older player is sent the
  // canvas model, the mount, as before, behind the mounted-character notice.
  const bool riding = canvasShowsMountedCharacter() && unityRendererHost->ipc()->playerRidesMounts();
  WoWModel * m = riding ? riderModel() : const_cast<WoWModel *>(canvas->model());
  GameFile * gf = m->gamefile;
  const bool character = (riding || canvasShowsCharacter()) && unityRendererHost->ipc()->playerDressesCharacters();
  const int fileDataID = gf->fileDataId() > 0 ? (int)gf->fileDataId() : 0;
  // Its colour first (viewportBackgroundShown): the player draws it on that from its first frame. And, parked, the
  // viewport's size: it frames what it builds by its own aspect.
  const bool recoloured = pushViewportBackground();
  unityRendererHost->sizePlayerForLoad();
  // Answers about any earlier load are told apart from answers about this one by this number.
  const int load = ++m_unityLoadSerial;
  // A file opened by its FileDataID alone goes to the player by FileDataID alone: its made-up name ("FileXXXXXXXX.unk",
  // for a file the listfile does not name, or one the client's file enumeration missed) is no path it could ask for.
  QString path = gf->fullname();
  if (fileDataID > 0 &&
      static_cast<wow::WoWFolder &>(GAMEDIRECTORY).fileName(fileDataID).compare(path, Qt::CaseInsensitive) != 0)
    path.clear();
  // A character rebuilt on its other model generation keeps the view the user had (SwitchCharacterVariant).
  const bool keepView = character && m_variantSwitching > 0 && unityRendererHost->ipc()->playerKeepsView();
  unityRendererHost->ipc()->sendLoadWoWModel(path, fileDataID, QStringLiteral("active"), character, load,
                                             QStringLiteral("m2"), keepView);
  m_unityLoadedFileDataID = fileDataID;
  m_unityLoadedCharacter = character;
  // A model built after a world model: awaited, the player asked what it shows until it has the model
  // (PlayerContent). After anything else, the model replaces what was there as models always have.
  m_mapObjectAwaited = 0;
  m_modelAwaitedFailed = 0;
  // (And a model whose colour has just changed under what the player shows, or sent while an earlier answer's fence
  // is pending: neither is on screen before it is built.)
  const bool mayShowOther = m_playerContent == PlayerContent::MapObject || m_playerContent == PlayerContent::Unknown ||
                            recoloured || m_uncoverFence != 0;
  m_uncoverFence = 0;
  if (mayShowOther && fileDataID > 0 && unityRendererHost->ipc()->playerDrawsMapObjects())
  {
    m_playerContent = PlayerContent::Unknown;
    m_modelAwaited = fileDataID;
    m_modelAwaitedClock.Start();
    m_playerContentQuery = 0;
    m_playerContentPoll.Start(POLL_MS);
  }
  else
  {
    m_modelAwaited = 0;
    m_playerContentPoll.Stop();
    m_playerContent = PlayerContent::Model;
  }
  // A new load is dressed from scratch: the next tick sends this character's scene even when its
  // fingerprint happens to equal the previous one (the same character loaded again), and it does not
  // wait for an answer about the previous model's scene -- the player drops that one for this load.
  m_lastSceneSignature = 0;
  m_sceneAwaitingRevision = 0;
  if (!character || (int)gf->fileDataId() != m_unityCharacterFailed)
    m_unityCharacterFailed = 0;
}

// The load AND what is showing: for a player that connects while a model is already up
// (onUnityReady), which missed the pushes LoadModel's own path would have made.
void ModelViewer::SendCurrentModelToUnity()
{
  SendLoadToUnity();
  SendCharacterSceneToUnity(true);
  // ... its skin -- the display's textures, geosets and particle colour, as the animation
  // control pushes them on a load -- so a player that connects with a model already up shows
  // the geosets the file-list path would give it. After the load, which is what the player's
  // guard for the model being loaded requires.
  // (Not for a character: its scene carries every texture and geoset, and follows on the next tick. Nor for the
  // mount a character rides, to a player that seats it: the rider's scene carries the mount's too.)
  if (unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isConnected() &&
      canvas && canvas->model() && canvas->model()->gamefile && !canvasShowsCharacter() &&
      !(canvasShowsMountedCharacter() && unityPlayerRidesMounts()))
    unityRendererHost->ipc()->sendModelSkin((int)canvas->model()->gamefile->fileDataId());
  // ... and which animation it is showing, so the player starts on the app's selection instead of
  // picking its own idle and being corrected a moment later.
  SendCurrentAnimationToUnity();
}

// The displayed skin changed (dropdown, or the default picked on model load). The Unity viewport
// renders the same model from the same data, so it has to follow the same selection -- otherwise
// it shows whatever the database happens to call the model's default while the canvas shows what
// the user picked. Texture only: the mesh it already built is unchanged.
void ModelViewer::SendCurrentSkinToUnity()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return;
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return;
  // A character's textures and geosets travel in its scene (SendCharacterSceneToUnity), which the
  // host's own refresh keeps current; a skin push would only repeat part of it. So do those of the mount a
  // character rides, to a player that seats it (the scene's "mount"): one channel for its display state.
  if (canvasShowsCharacter() || (canvasShowsMountedCharacter() && unityPlayerRidesMounts()))
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  unityRendererHost->ipc()->sendModelSkin((int)m->gamefile->fileDataId());
}

namespace
{
  // The mount the rider rides, as a character scene describes it: the model on the rider node's parent, the
  // node's attachment id, and CharControl's serial and display id for it. model is null when none is ridden.
  UnityCharacterScene::Mount riddenMount(const ModelViewer * viewer)
  {
    UnityCharacterScene::Mount mount;
    mount.model = viewer->riderMount();
    if (mount.model)
    {
      mount.attachmentId = viewer->charControl->charAtt->id;
      mount.serial = viewer->charControl->mountSerial;
      mount.displayId = viewer->charControl->mountDisplayId;
    }
    return mount;
  }

  // One line about a scene's "mount" (UnityCharacterScene::buildMount), for the [unity-mount] log. The animation
  // ids of the two sequences are looked up for the reader; the scene itself carries only the indices.
  QString describeMount(const QJsonObject & o, const WoWModel * mount, const WoWModel * rider)
  {
    const auto animId = [](const WoWModel * m, int index) {
      return (m && index >= 0 && index < (int)m->anims.size()) ? QString::number(m->anims[index].animID)
                                                                 : QString("-");
    };
    const QJsonArray p = o.value("position").toArray();
    const int sequence = o.value("sequenceIndex").toInt(-1);
    const int riderSequence = o.value("riderSequenceIndex").toInt(-1);
    // Concatenated rather than passed through arg(): a path is free text.
    return o.value("key").toString() + " " + o.value("path").toString() +
           QString(" fileDataID=%1 displayID=%2 attachmentId=%3 bone=%4 position=(%5, %6, %7) riderScale=%8")
             .arg(o.value("fileDataID").toInt()).arg(o.value("displayID").toInt()).arg(o.value("attachmentId").toInt())
             .arg(o.value("bone").toInt()).arg(p.at(0).toDouble(), 0, 'g', 6).arg(p.at(1).toDouble(), 0, 'g', 6)
             .arg(p.at(2).toDouble(), 0, 'g', 6).arg(o.value("riderScale").toDouble(), 0, 'g', 6) +
           QString(" textures=%1 submeshes=%2 particleColorSets=%3 sequenceIndex=%4 (animID %5) "
                   "riderSequenceIndex=%6 (animID %7)")
             .arg(o.value("textures").toArray().size()).arg(o.value("submeshCount").toInt())
             .arg(o.contains("particleColorSets") ? "yes" : "no").arg(sequence).arg(animId(mount, sequence))
             .arg(riderSequence).arg(animId(rider, riderSequence));
  }
}

void ModelViewer::SendCharacterSceneToUnity(bool force)
{
  // Mounting puts the mount on the canvas and dismounting takes it off again, with no load: the
  // viewport follows here, where every tick passes. Only a change the viewport has not already followed
  // counts -- UpdateUnityViewportState records what it decided -- so a load is not decided twice. Swapping
  // one mount for another changes neither: the scene below describes the new mount.
  const bool showsCharacter = canvasShowsCharacter();
  const bool showsMounted = canvasShowsMountedCharacter();
  if (!force && (showsCharacter != m_lastShowsCharacter || showsMounted != m_lastShowsMountedCharacter))
  {
    m_lastShowsCharacter = showsCharacter;
    m_lastShowsMountedCharacter = showsMounted;
    if (isChar)
    {
      // The character is in front of the player again: dismounted, or mounted with a player that seats it on
      // the mount (protocol 5) and so keeps the character it was loaded with. The player may not have THIS
      // character loaded -- one that connected while the character rode a mount it could not seat was sent
      // the mount -- and it refuses a scene for a model it is not building, so the character's load goes out
      // again before the notice is taken down. A player that has it loaded is not sent it again: mounting,
      // dismounting and swapping load nothing on the host either.
      const WoWModel * character = unityCharacter();
      if (character && unityPlayerReady())
      {
        const bool dresses = unityRendererHost->ipc()->playerDressesCharacters();
        if (m_unityLoadedFileDataID != (int)character->gamefile->fileDataId() || m_unityLoadedCharacter != dresses)
          SendCurrentModelToUnity();
      }
      // Mounting puts the mounted-character notice up for a player that cannot seat the character; dismounting
      // takes it down. Nothing here launches or opens a dialog, so it is safe inside the canvas timer.
      UpdateUnityViewportState();
    }
  }

  // THE MOUNT IN THE LOG: each mount model the character rides is described once, as a scene would carry it,
  // whatever the player can do with it.
  if (showsMounted)
  {
    const UnityCharacterScene::Mount mount = riddenMount(this);
    if (mount.model != m_loggedMount || mount.serial != m_loggedMountSerial)
    {
      m_loggedMount = mount.model;
      m_loggedMountSerial = mount.serial;
      const WoWModel * rider = riderModel();
      LOG_INFO << "[unity-mount] the character" << rider->gamefile->fullname() << "rides"
               << describeMount(UnityCharacterScene::buildMount(riderModel(), mount), mount.model, rider)
               << (unityPlayerRidesMounts() ? "-- described in the character's scene"
                   : unityPlayerReady() ? "-- the player cannot seat it (older than protocol 5): mounted-character notice"
                                        : "-- no player ready yet");
    }
  }
  else if (m_loggedMount)
  {
    LOG_INFO << "[unity-mount] no mount is ridden any more (was" << QString("M%1").arg(m_loggedMountSerial).toLatin1().constData()
             << ")";
    m_loggedMount = nullptr;
  }

  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->playerDressesCharacters())
    return;
  // The character the player is loaded with: the canvas model, or the rider of a mount the player seats.
  WoWModel * m = unityEquipmentOwner();
  if (!m || m_sceneHold > 0)
    return;
  // The player reported it could not build this character from the load on display, and the canvas has
  // it: another scene would only be refused again, with the composited body image sent for nothing. A
  // later load of the same body model is another attempt, whose build waits for its scene -- held back,
  // that load would never finish, never answer, and the character would stay on the canvas for good.
  if (m_unityCharacterFailed != 0 && m_unityCharacterFailed == (int)m->gamefile->fileDataId() &&
      m_unityCharacterFailedLoad == m_unityLoadSerial)
    return;
  const unsigned long now = timeGetTime();
  if (!force)
  {
    if (m_lastSceneCheck != 0 && (now - m_lastSceneCheck) < SCENE_CHECK_MS)
      return;
    m_lastSceneCheck = now;
  }
  if (m_sceneAwaitingRevision != 0 && (now - m_sceneSentAt) < SCENE_ACK_TIMEOUT_MS)
  {
    // The previous scene is still being applied. Whatever changes meanwhile goes out when it is
    // answered (OnCharacterSceneApplied); a forced send is remembered by clearing the fingerprint.
    if (force)
      m_lastSceneSignature = 0;
    return;
  }
  if (m_sceneAwaitingRevision != 0)
  {
    // No answer in time. The wait ends HERE, once: left in place, every tick after this one would find
    // the same expired wait and log it again, twenty times a second, while nothing changed. The current
    // state goes out once more, and the next wait is timed from now.
    LOG_ERROR << "[unity-character] no answer to scene revision" << m_sceneAwaitingRevision << "after"
              << (now - m_sceneSentAt) << "ms -- sending the current state";
    m_sceneAwaitingRevision = 0;
    m_sceneSentAt = now;
    m_lastSceneSignature = 0;
  }

  // The rider's mount goes in its scene: m is the rider, not the canvas model, only while it rides one the player
  // seats (unityCharacter).
  const UnityCharacterScene::Mount mount = riddenMount(this);
  const UnityCharacterScene::Mount * ridden = (m != canvas->model() && mount.model) ? &mount : nullptr;
  const quint64 signature = UnityCharacterScene::signature(m, ridden);
  if (!force && signature == m_lastSceneSignature)
    return;
  m_lastSceneSignature = signature;

  QElapsedTimer clock;
  clock.start();
  UnityIpcServer * ipc = unityRendererHost->ipc();
  UnityCharacterScene::Summary summary;
  QJsonObject scene = UnityCharacterScene::build(
    m, [ipc](const QString & kind, const QImage & image) { return ipc->shareCharacterImage(kind, image); },
    summary, ridden, !m->charModelDetails.isChar);
  scene["load"] = m_unityLoadSerial;
  const int revision = ++m_sceneRevision;
  if (ipc->sendCharacterScene((int)m->gamefile->fileDataId(), revision, scene))
  {
    m_sceneAwaitingRevision = revision;
    m_sceneSentAt = now;
  }
  if (m_sceneAwaitingRevision == revision)
  {
    LOG_INFO << "[unity-character] scene revision" << revision << "for" << m->gamefile->fullname() << ":"
             << summary.bodyTextures << "body texture(s)," << summary.images << "composited image reference(s),"
             << summary.merged << "merged," << summary.attachments << "attached; built and queued in"
             << clock.elapsed() << "ms";
    if (summary.mount)
      LOG_INFO << "[unity-mount] scene revision" << revision << "load" << m_unityLoadSerial << "mount"
               << describeMount(scene.value("mount").toObject(), mount.model, m);
  }
}

bool ModelViewer::unityPlayerReady() const
{
  return unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isConnected() &&
         unityRendererHost->ipc()->isUnityReady();
}

bool ModelViewer::unityPlayerSwitchesSubmeshes() const
{
  return unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->playerSwitchesSubmeshes();
}

int ModelViewer::SendCurrentGeosetsToUnity()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return 0;
  if (!canvas || !canvas->model() || !canvas->model()->gamefile || !unityCanDrawCurrentModel())
    return 0;
  // A character's geosets -- its own, its merged parts' and its items' -- reach the player in its
  // scene, which the signature sends on the next tick. One channel: a modelGeosets for the body
  // would race the scene that also carries the body's flags. The same holds for the mount a character
  // rides: the canvas model is then the mount, and its flags travel in the rider's scene.
  if (canvasShowsCharacter() || canvasShowsMountedCharacter())
    return 0;
  const int revision = ++m_geosetRevision;
  return unityRendererHost->ipc()->sendModelGeosets((int)canvas->model()->gamefile->fileDataId(), revision)
           ? revision : 0;
}

void ModelViewer::OnCharacterSceneApplied(const UnityIpcServer::SceneAck & ack)
{
  if (ack.status == "pending")
    return;   // the body is still building; the final answer follows
  // Revisions are numbered across loads, so the wait for one ends with its answer whichever load that
  // answer names.
  const bool endsWait = ack.revision != 0 && ack.revision == m_sceneAwaitingRevision;
  if (endsWait)
    m_sceneAwaitingRevision = 0;

  // AN ANSWER ABOUT AN EARLIER LOAD SAYS NOTHING ABOUT THIS ONE. The player answers for the scene a new
  // load made it drop, and a failed build reports after the next load may already have gone out -- as
  // often as not of the same body model (NPCs of one race and sex share it), so the fileDataID alone
  // took either for news about the character on display: a false notice in the Geosets tab, or a
  // character that builds fine kept behind a notice. Load 0 is a scene the player could tie to no load at
  // all (the serial sent is never 0). "superseded" is a scene dropped for a newer scene or a new load,
  // which is answered in its own right: no failure either.
  if (ack.load == 0 || ack.load != m_unityLoadSerial || ack.status == "superseded")
  {
    if (endsWait)
    {
      m_lastSceneCheck = 0;
      SendCharacterSceneToUnity(false);
    }
    return;
  }

  // The character the player is loaded with: the canvas model, or the rider of a mount the player seats.
  const WoWModel * character = unityEquipmentOwner();
  const bool current = character && (int)character->gamefile->fileDataId() == ack.fileDataID;
  // On screen now: its other model generation is fetched by the player while nothing waits for it, so a switch to it
  // finds its files there. Decided here, not at the load: an NPC is known as one only once its load returned.
  if (current && ack.status == "applied" && m_assetHintLoad != m_unityLoadSerial)
    HintVariantPartner();
  // THE MOUNT (protocol 5) is answered in the same ack but is not the character: a mount the player could not
  // build leaves the character built and on screen, so it is logged here and changes no notice.
  if (current && ack.mountStatus == "failed")
    LOG_ERROR << "[unity-mount] the Unity viewport could not build mount" << ack.mountKey << "of scene revision"
              << ack.revision << "(" << ack.mountReason << ") -- the character is shown without it";
  else if (current && (!ack.mountKey.isEmpty() || (!ack.mountStatus.isEmpty() && ack.mountStatus != "none")))
    LOG_INFO << "[unity-mount] scene revision" << ack.revision << "load" << ack.load << "mount"
             << (ack.mountKey.isEmpty() ? QString("-") : ack.mountKey) << ack.mountStatus;
  if (current && ack.status == "applied" && m_unityCharacterFailed == ack.fileDataID)
  {
    // A later load of the model whose build failed (another NPC on the same body, say) was dressed, so
    // the notice comes down and the viewport shows the character.
    m_unityCharacterFailed = 0;
    m_unityCharacterFailReason.clear();
    UpdateUnityViewportState();
  }
  if (current && ack.status == "rejected")
  {
    if (ack.reason.startsWith("load failed"))
    {
      // The player could not build this character at all and is still holding whatever it showed
      // before. The viewport says so, in front of it, until something else is loaded.
      LOG_ERROR << "[unity-character] the Unity viewport could not build" << character->gamefile->fullname()
                << "(" << ack.reason << ") -- showing a notice instead";
      m_unityCharacterFailed = ack.fileDataID;
      m_unityCharacterFailedLoad = ack.load;
      m_unityCharacterFailReason = ack.reason;
      UpdateUnityViewportState();
      return;
    }
    // A scene the player refused as a whole (a submesh list that does not fit, say): the Geosets tab
    // says so rather than implying the checkboxes are on screen.
    if (modelInspector)
    {
      UnityIpcServer::GeosetAck geo;
      geo.fileDataID = ack.fileDataID;
      geo.revision = 0;
      geo.status = "rejected";
      geo.reason = ack.reason;
      modelInspector->OnUnityGeosetsApplied(geo);
    }
  }
  if (current && !ack.missing.isEmpty())
    LOG_ERROR << "[unity-character] scene revision" << ack.revision << "applied without" << ack.missing.join(", ");

  // Whatever changed while this scene was being applied goes out now.
  m_lastSceneCheck = 0;
  SendCharacterSceneToUnity(false);
}

void ModelViewer::OnMapObjectLoaded(const UnityIpcServer::MapObjectReport & report)
{
  // A world model built for the latest load is what the player shows now. (One built for an earlier load says
  // nothing certain: a later load may already have replaced it, or be about to -- PlayerContent stays Unknown.)
  if (report.status == "built" && report.load != 0 && report.load == m_unityLoadSerial)
    m_playerContent = PlayerContent::MapObject;
  // The wait for a world model ends with any answer about its load -- once a failure is recorded below, so the
  // viewport goes from "Loading building" straight to the failure's notice, never uncovering what the player had.
  bool update = false;
  if (m_mapObjectAwaited != 0 && report.load == m_mapObjectAwaited && m_uncoverFence == 0)
  {
    // Built: shown once the player has answered the fence (THE UNCOVER FENCE), in later frames.
    if (!(report.status == "built" && startUncoverFence()))
    {
      m_mapObjectAwaited = 0;
      update = true;
    }
  }
  // Only an answer about the load on display says anything about the WMO on display: a "superseded"
  // report, or any report naming an earlier serial, is about a load the player was told to drop.
  const bool current = isWMO && canvas && canvas->wmo && report.load != 0 && report.load == m_unityLoadSerial &&
                       (int)canvas->wmo->fileDataID == report.fileDataID;
  if (!current)
  {
    if (update)
      UpdateUnityViewportState();
    return;
  }
  if (report.status == "built")
  {
    LOG_INFO << "[unity-wmo] the Unity viewport built" << canvas->wmo->itemName() << ":" << report.groups << "group(s),"
             << report.submeshes << "submesh(es)," << report.materials << "material(s) ("
             << report.unresolvedMaterials << "unresolved)," << report.texturesDecoded << "/" << report.texturesReferenced
             << "texture(s) in" << report.totalMs << "ms; doodads, liquids and lights are not drawn yet";
    if (report.groups >= 0 && report.groups != (int)canvas->wmo->nGroups)
      LOG_ERROR << "[unity-wmo] the player built" << report.groups << "group(s) but the root's header names"
                << canvas->wmo->nGroups;
  }
  else if (report.status == "failed")
  {
    // The player is still showing whatever it had before, which must not pass for this WMO.
    LOG_ERROR << "[unity-wmo] the Unity viewport could not build" << canvas->wmo->itemName() << "(" << report.reason
              << ") -- showing a notice instead";
    m_unityWmoFailed = report.fileDataID;
    m_unityWmoFailedLoad = report.load;
    m_unityWmoFailReason = report.reason;
    update = true;
  }
  if (update)
    UpdateUnityViewportState();
}

// The animation on display changed (the dropdown, or the default picked on model load). Same
// reasoning as the skin: the Unity viewport draws the same model from the same data, so it has to
// play what the canvas is playing rather than the idle it would choose for itself.
//
// The animation manager is asked what is PLAYING rather than the control being asked what is
// selected, so a default chosen during model load and a dropdown choice both report the same way.
void ModelViewer::SendCurrentAnimationToUnity()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return;
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  // A CHARACTER RIDING A MOUNT the player seats (protocol 5): two models animate, and the Animation panel acts on
  // one of them -- g_selModel, the mount after the mount choice, the rider once View > Attachments picked it --
  // so the push is about that model and says which of the two it is. Its FileDataID cannot say: a mount model
  // can also be a playable race's body. Any other pick there (an item model) is nothing the player animates on
  // its own, and is not pushed.
  QString role;
  if (canvasShowsMountedCharacter() && unityPlayerRidesMounts())
  {
    if (g_selModel && g_selModel == riderMount())
      role = QStringLiteral("mount");
    else if (g_selModel && g_selModel == riderModel())
      role = QStringLiteral("rider");
    else
      return;
    m = g_selModel;
    if (!m->gamefile)
      return;
  }
  if (!m->animManager || m->anims.empty())
    return;

  const int index = (int)m->animManager->GetAnim();
  if (index < 0 || index >= (int)m->anims.size())
    return;
  unityRendererHost->ipc()->sendModelAnimation((int)m->gamefile->fileDataId(), index,
                                               m->anims[index].animID, (int)m->anims[index].length,
                                               true, role, role.isEmpty() ? 0 : m_unityLoadSerial);
  // A new selection restarts at frame 0 and keeps whatever play/pause and speed were in force;
  // send that alongside so the renderer does not briefly run an animation the app has paused.
  SendAnimationStateToUnity(true);
}

// Whether the animation is running, how fast, and where it is.
//
// This one has NO single funnel, unlike the skin and the animation choice. Play, pause, stop,
// clear, the two step buttons, the speed slider and the frame slider all change it, and the time
// advances every frame with no control involved at all. So it is pushed two ways: forced from each
// of those controls, and on a slow heartbeat from the canvas tick while something is playing.
//
// The heartbeat exists because two renderers timing themselves independently drift apart; it is
// the channel that corrects that. It is deliberately slow, and the PLAYER decides whether a given
// timestamp is worth snapping to -- only it knows where its own clock is, and snapping on every
// message would trade drift for jitter.
void ModelViewer::SendAnimationStateToUnity(bool force)
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return;
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  if (!m->animManager || m->anims.empty())
    return;

  const int index = (int)m->animManager->GetAnim();
  if (index < 0 || index >= (int)m->anims.size())
    return;

  const bool playing = !m->animManager->IsPaused();
  const float speed = m->animManager->GetSpeed();
  // The position now, not at the last canvas tick: a state forced in the middle of a UI-thread wait (a choice made
  // while the mount's model loads) would otherwise send the viewport back by the wait (AnimManager::GetFrameNow).
  const int timeMs = (int)m->animManager->GetFrameNow(playing);

  if (!force)
  {
    // Heartbeat. A paused model cannot drift, so it needs none; and the interval is long enough
    // that this is a rounding error next to the asset traffic the same channel already carries.
    if (!playing)
      return;
    const unsigned long now = timeGetTime();
    if (m_lastAnimStatePush != 0 && (now - m_lastAnimStatePush) < ANIM_STATE_HEARTBEAT_MS)
      return;
    m_lastAnimStatePush = now;
  }
  else
  {
    m_lastAnimStatePush = timeGetTime();
  }

  // A CHARACTER RIDING A MOUNT the player seats (protocol 5): the fields above stay the canvas model's, the
  // mount's, and the rider's clock rides along, sampled in this same call so the player can apply both at once.
  // Both advance by the same tick but on their own clocks (ModelCanvas::tick -> Attachment::tick). The rider's
  // "playing" is the MOUNT's: the tick stops the time of the whole tree when the canvas model is paused and never
  // reads the rider's own flag -- which the mount choice leaves set by stopping the rider (AnimManager::Stop)
  // while its time keeps advancing (AnimManager::Tick does not read it).
  if (canvasShowsMountedCharacter() && unityPlayerRidesMounts())
  {
    WoWModel * rider = riderModel();
    const int riderIndex = (rider->animManager && !rider->anims.empty()) ? (int)rider->animManager->GetAnim() : -1;
    if (riderIndex >= 0 && riderIndex < (int)rider->anims.size())
    {
      UnityIpcServer::RiderState state;
      state.sequenceIndex = riderIndex;
      state.playing = playing;
      state.timeMs = (int)rider->animManager->GetFrameNow(playing);
      state.speed = rider->animManager->GetSpeed();
      state.loop = true;
      unityRendererHost->ipc()->sendModelAnimationState((int)m->gamefile->fileDataId(), index,
                                                        playing, timeMs, speed, true,
                                                        /* explicitState */ force, &state, m_unityLoadSerial);
      return;
    }
  }

  unityRendererHost->ipc()->sendModelAnimationState((int)m->gamefile->fileDataId(), index,
                                                    playing, timeMs, speed, true,
                                                    /* explicitState */ force);
}

// Menu button press events
void ModelViewer::OnToggleCommand(wxCommandEvent &event)
{
  int id = event.GetId();

  //switch 
  switch (id) {
    case ID_FILE_RESETLAYOUT:
      ResetLayout();
      break;

    case ID_SAVE_CHAR:
    {
      wxFileDialog saveDialog(this, wxT("Save character"), wxEmptyString, wxEmptyString, wxT("Character files (*.chr)|*.chr"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      if (saveDialog.ShowModal() == wxID_OK)
        SaveChar(QString::fromWCharArray(saveDialog.GetPath().c_str()));
    }
    break;
    case ID_LOAD_CHAR:
    {
      wxFileDialog loadDialog(this, wxT("Load character"), wxEmptyString, wxEmptyString, wxT("Character files (*.chr)|*.chr"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      if (loadDialog.ShowModal() == wxID_OK)
      {
        LOG_INFO << "Loading character from a save file:" << QString::fromWCharArray(loadDialog.GetPath().c_str());
        if (charControl->model) // if a model is already present, unload equipment
        {
          for (size_t i = 0; i < NUM_CHAR_SLOTS; i++)
          {
            WoWItem * item = charControl->model->getItem((CharSlots)i);
            if (item)
              item->setId(0);
          }
        }
        LoadChar(QString::fromWCharArray(loadDialog.GetPath().c_str()));
      }
    }
    fileControl->UpdateInterface();
    break;

    case ID_SAVE_EQUIPMENT:
    {
      wxFileDialog dialog(this, wxT("Save equipment"), wxEmptyString, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
      if (dialog.ShowModal() == wxID_OK)
        SaveChar(QString::fromWCharArray(dialog.GetPath().c_str()), true);
      break;
    }

    case ID_LOAD_EQUIPMENT:
    {
      wxFileDialog loadDialog(this, wxT("Load equipment"), wxEmptyString, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      if (loadDialog.ShowModal() == wxID_OK)
      {
        LOG_INFO << "Loading equipment from a save file:" << QString::fromWCharArray(loadDialog.GetPath().c_str());
        if (charControl->model) // if a model is already present, unload equipment
        {
          for (size_t i = 0; i < NUM_CHAR_SLOTS; i++)
          {
            WoWItem * item = charControl->model->getItem((CharSlots)i);
            if (item)
              item->setId(0);
          }
        }
        LoadChar(QString::fromWCharArray(loadDialog.GetPath().c_str()), true);
      }
      break;
    }

    case ID_IMPORT_CHAR:
    {
      ArmoryImportDialog dialog(this);
      dialog.ShowModal();
    }
    break;
  }
}

// Menu button press events
void ModelViewer::OnEffects(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id == ID_ENCHANTS)
    enchants->Display();
}

void ModelViewer::OnSetEquipment(wxCommandEvent &event)
{
  if (isChar)
    charControl->OnButton(event);

  UpdateControls();
}

void ModelViewer::OnViewLog(wxCommandEvent &event)
{
  int ID = event.GetId();
  if (ID == ID_FILE_VIEWLOG) {
    wxString logPath = cfgPath.BeforeLast(SLASH) + SLASH + wxT("log.txt");
#ifdef  _WINDOWS
    wxExecute(wxT("notepad.exe ") + logPath);
#elif  _MAC
    wxExecute(wxT("/Applications/TextEdit.app/Contents/MacOS/TextEdit ")+logPath);
#endif
  }
}

void ModelViewer::OnGameToggle(wxCommandEvent &event)
{
  int ID = event.GetId();
  if (ID == ID_LOAD_WOW)
    PromptAndLoadClient();
}

// Choosing and loading a client, on request.
//
// This used to run itself at startup, which is why it lived in app.cpp; it does not any more --
// the application opens on an empty viewer and waits to be told which client to read. So the
// picker belongs where the user asks for it, on the File menu, and this is the only path that
// loads one.
//
// The chooser ("Choose World of Warcraft") only says which installation; LoadWoW opens it, resolves its schema
// and tells the user, in plain words, when it cannot. A client that could not be opened brings the chooser back,
// so another can be picked; the client loaded before (if any) is still loaded meanwhile.
void ModelViewer::PromptAndLoadClient()
{
  if (m_clientLoading) // the loading window yields to the event loop: no second load inside the first
    return;
  for (;;)
  {
    ClientChoiceDialog clientDlg(this);
    if (clientDlg.ShowModal() != wxID_OK)
    {
      LOG_INFO << "Client Choice dialog dismissed without loading a client.";
      return;
    }

    if (clientDlg.isLegacyMpq())
    {
      // Legacy (pre-CASC) MoPaQ client -- the shared helper shows the folder picker and loads it,
      // exactly like File -> Load Legacy MPQ Client... <=0 means cancelled or no archives (the
      // helper shows its own error) -> back to the picker.
      if (PromptAndLoadLegacyMpqClient() <= 0)
        continue;
      return;
    }

    const wxString previousPath = gamePath;
    gamePath = clientDlg.dataPath();
    core::GameConfig chosen = clientDlg.selectedConfig();
    if (LoadWoW(&chosen, true /* show loading progress */))
      return;
    gamePath = previousPath; // not opened: the client loaded before (if any) is still the one in use
    continue;
  }
}

// Quietly refresh the CASC file list so files added by new client patches resolve by name
// without the user maintaining anything. The source is fixed (no setting, nothing user-visible):
// on EVERY launch it asks the server whether the community listfile changed (cheap conditional
// request) and only re-downloads the ~147 MB list when it actually did. Any problem (offline,
// server error, short payload) leaves the existing list in place, so a failed refresh can never
// break startup. Streams to a temp file to keep memory flat, then swaps it in atomically. Reuses
// the generic "Loading file list..." step so nothing about a network fetch surfaces in the UI.
static void refreshCommunityListfile(const QString & localPath, LoadingDialog * progress)
{
  // Check on EVERY launch, but avoid re-pulling the ~147 MB list when it hasn't changed: send the
  // ETag saved from last time as If-None-Match and let the server answer "304 Not Modified" for an
  // unchanged list (GitHub's release CDN honours this through the redirect). Only a real change
  // (200) downloads. The saved ETag is trusted only while the list it described is still on disk.
  const QString etagPath = localPath + ".etag";
  QByteArray savedEtag;
  if (QFile::exists(localPath))
  {
    QFile ef(etagPath);
    if (ef.open(QIODevice::ReadOnly))
      savedEtag = ef.readAll().trimmed();
  }

  const QString tmpPath = localPath + ".new";
  QFile out(tmpPath);
  if (!out.open(QIODevice::WriteOnly))
    return; // can't stage a download here; keep what's on disk

  QNetworkAccessManager manager;
  QNetworkRequest request(QUrl("https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv"));
  request.setRawHeader("User-Agent", "WoWModelViewer");
  if (!savedEtag.isEmpty())
    request.setRawHeader("If-None-Match", savedEtag);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply * reply = manager.get(request);

  // Write each chunk straight to disk as it arrives instead of buffering the whole list.
  QObject::connect(reply, &QNetworkReply::readyRead, [&out, reply]() {
    out.write(reply->readAll());
  });

  QEventLoop loop;
  QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
  if (progress)
  {
    LoadingDialog * pd = progress;
    QObject::connect(reply, &QNetworkReply::downloadProgress, [pd](qint64 received, qint64 total) {
      // Pumps the wx loading dialog (step() yields) during the otherwise-blocking download and
      // inches the gauge 45 -> 60.
      const float frac = (total > 0) ? (float)received / (float)total : 0.0f;
      pd->step(_("Loading file list..."), 45 + (int)(frac * 15.0f));
    });
  }
  loop.exec();

  const bool ok = (reply->error() == QNetworkReply::NoError);
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray newEtag = reply->rawHeader("ETag");
  out.close();

  // Unchanged since last launch -> nothing downloaded; keep the existing list.
  if (ok && status == 304)
  {
    LOG_INFO << "File list unchanged (304 Not Modified); keeping existing list.";
    QFile::remove(tmpPath);
    return;
  }

  const qint64 size = QFileInfo(tmpPath).size();

  // A real listfile is tens of MB of "<id>;<path>" lines; anything much smaller is an error page
  // or a truncated transfer, so keep the existing list rather than overwrite it with junk.
  if (ok && size > (50 * 1024 * 1024))
  {
    if (QFile::exists(localPath))
      QFile::remove(localPath);
    if (QFile::rename(tmpPath, localPath))
    {
      LOG_INFO << "File list refreshed (" << size << "bytes).";
      if (!newEtag.isEmpty())
      {
        QFile ef(etagPath);
        if (ef.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
          ef.write(newEtag);
          ef.close();
        }
      }
      return;
    }
    LOG_ERROR << "File list refresh: could not replace" << localPath;
  }
  else
  {
    LOG_INFO << "File list refresh skipped; keeping existing list.";
  }
  QFile::remove(tmpPath); // discard the partial/unused temp file
}

// Refresh the TACT encryption keys on EVERY launch. CASC needs these to decrypt encrypted content
// -- both whole encrypted files and the encrypted DB2 sections that hold brand-new / unreleased
// records (recent raid bosses etc.). New keys get published frequently while a patch is on the PTR,
// and the list is small (~1 MB), so we always pull the latest rather than caching it for a week
// like the (147 MB) file list. The community list at wowdev/TACTKeys is whitespace-separated
// "<keyname> <key>"; we convert it to the "<keyname>;<key>" form CASCFolder::addExtraEncryptionKeys()
// reads. Must run BEFORE setConfig() opens the storage (that is when the keys are handed to CASC).
// Any failure (e.g. offline, or GitHub down) keeps the existing on-disk keys untouched.
static void refreshTactKeys(const QString & localPath, LoadingDialog * progress)
{
  // The loading window keeps saying which client is being opened: the keys are a detail of opening it.
  Q_UNUSED(progress);

  QNetworkAccessManager manager;
  QNetworkRequest request(QUrl("https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt"));
  request.setRawHeader("User-Agent", "WoWModelViewer");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply * reply = manager.get(request);

  QEventLoop loop;
  QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
  loop.exec();

  const bool ok = (reply->error() == QNetworkReply::NoError);
  const QByteArray body = reply->readAll();
  reply->deleteLater();

  // A real list is hundreds of KB of "<16 hex> <32 hex>" lines; anything tiny is an error page.
  if (!ok || body.size() < (100 * 1024))
  {
    LOG_INFO << "TACT key refresh skipped; keeping existing keys.";
    return;
  }

  // Convert to the "<keyname>;<key>" CSV, validating the hex lengths.
  QByteArray outData;
  int count = 0;
  for (const QByteArray & raw : body.split('\n'))
  {
    const QByteArray line = raw.simplified();
    if (line.isEmpty() || line.startsWith('#'))
      continue;
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 2)
      continue;
    const QByteArray name = parts[0].toUpper();
    const QByteArray key = parts[1].toUpper();
    if (name.size() != 16 || key.size() != 32)
      continue;
    outData += name + ';' + key + '\n';
    count++;
  }

  if (count < 1000) // a real list has many thousands of keys; refuse a suspicious result
  {
    LOG_INFO << "TACT key refresh produced too few keys (" << count << "); keeping existing.";
    return;
  }

  const QString tmpPath = localPath + ".new";
  QFile out(tmpPath);
  if (!out.open(QIODevice::WriteOnly))
    return;
  out.write(outData);
  out.close();

  if (QFile::exists(localPath))
    QFile::remove(localPath);
  if (QFile::rename(tmpPath, localPath))
    LOG_INFO << "TACT keys refreshed (" << count << "keys).";
  else
    QFile::remove(tmpPath);
}

namespace
{
  // Set while a client is being opened: the loading window yields to the event loop, and nothing may start a
  // second load (or a legacy one) inside it.
  struct ClientLoadingFlag
  {
    bool & flag;
    explicit ClientLoadingFlag(bool & f) : flag(f) { flag = true; }
    ~ClientLoadingFlag() { flag = false; }
  };
}

int ModelViewer::LoadWoWFromMpq(const QString & dataFolder, const QString & locale)
{
  if (m_clientLoading)
    return 0;
  // A folder without MPQ archives changes nothing: the client loaded before stays loaded.
  if (wow::WoWFolder::countMpqArchives(dataFolder, locale) <= 0)
  {
    LOG_ERROR << "[mpq] No MPQ archives found under" << dataFolder << "-- legacy client not loaded; the loaded client is kept.";
    return 0;
  }
  ClientLoadingFlag loadingFlag(m_clientLoading);
  UnityAssetAccess::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
  fileControl->Disable();
  TexturesClientLoadStarting();

  // Always install a FRESH folder for the legacy client -- never reuse an already-loaded Retail
  // (or previous MPQ) folder. Reusing the Retail folder would mix CASC + MPQ entries in one tree
  // and leave its CASC storage pointing at the wrong path. As for any change of client (LoadWoW),
  // the previous client's storage and database are let go of, and what is on screen and cached
  // from it is cleared.
  if (core::Game::instance().initDone())
    if (wow::WoWFolder * old = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY))
      old->closeStorage();
  ResetClientState();
  wow::WoWFolder * folder = new wow::WoWFolder(dataFolder);
  core::Game::instance().replace(folder, new wow::WoWDatabase());
  folder->init();
  m_loadedProduct = "mpq";
  m_clientSchema.clear();

  // Open the legacy MPQ archive chain, build the MPQ client profile and populate the file tree.
  // GAMEDIRECTORY is a WoWFolder in this mode.
  const int archives = static_cast<wow::WoWFolder &>(GAMEDIRECTORY).initMpq(dataFolder, locale, "3.3.5.12340");
  if (archives <= 0)
  {
    LOG_ERROR << "[mpq] No MPQ archives found under" << dataFolder << "-- legacy client not loaded.";
    return 0;
  }

  // No DBC/database yet: point the schema folder at the (absent) legacy profile so the database
  // stays empty -- a model's textures come from its own embedded texture list. No CASC listfile
  // and no version gate; files are served by name (WoWFolder::getFile -> MpqFile). Character
  // customization / equipment / DBC are later milestones.
  core::Game::instance().setConfigFolder("games/wow/3.3.5/");

  // Enable the file browser over the freshly-populated MPQ file tree so models can be picked as
  // usual. (charControl needs the DBC/database, which MPQ mode does not load yet, so it is left
  // untouched here.)
  fileControl->Init(this);
  fileControl->Enable();
  // The empty viewport points at Browse now rather than at loading a client -- once the load has
  // returned, since the client does not count as active while it is still inside it.
  CallAfter([this]() {
    UpdateUnityViewportState();
    TexturesClientLoaded();
  });

  SetStatusText(wxString(GAMEDIRECTORY.version().toStdWString()), 1);
  SetStatusText(wxT("Legacy MPQ"), 2);
  ComputeClientCapabilities();
  LOG_INFO << "[mpq] legacy client ready: storage=MPQ, provider ready, archives=" << archives
           << " -- load models from the file browser (no DBC/customization/equipment yet).";
  return archives;
}

int ModelViewer::PromptAndLoadLegacyMpqClient()
{
  const wxString defaultDir = m_lastMpqFolder.isEmpty()
                            ? wxString()
                            : wxString(m_lastMpqFolder.toStdWString());

  // ALWAYS prompt for the folder first -- never assume a path (the startup dialog's field holds
  // the Retail/CASC folder, which has no MPQ archives).
  wxDirDialog dlg(this,
    _("Select WoW install folder or Data folder (containing common.MPQ, expansion.MPQ, ...)"),
    defaultDir, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK)
    return -1; // cancelled -> no error; caller stays where it was

  const wxString path = dlg.GetPath();
  const QString qpath = QString::fromWCharArray(path.c_str());

  int archives = 0;
  {
    wxBusyCursor busy; // enumerating the archives + file list can take a moment
    SetStatusText(_("Loading legacy MPQ client..."));
    archives = LoadWoWFromMpq(qpath, QString()); // empty locale -> auto-detect
  }

  if (archives <= 0)
  {
    wxMessageBox(
      wxString::Format(_("No MPQ archives were found in:\n\n%s\n\nPick the WoW install folder or its "
                         "Data folder (the one containing common.MPQ, patch.MPQ, ...)."), path),
      _("No legacy MPQ client found"), wxOK | wxICON_ERROR, this);
    SetStatusText(_("No MPQ archives found."));
    return 0; // invalid folder -> error already shown
  }

  // Persist the chosen folder for next launch (also written in SaveSession).
  m_lastMpqFolder = qpath;
  {
    QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
    config.setValue("Session/LastMpqFolder", m_lastMpqFolder);
  }

  // Clear user feedback: client type / era-build / locale / archive count.
  const core::ClientProfile & prof = GAMEDIRECTORY.clientProfile();
  const QString loc = GAMEDIRECTORY.locale();
  const wxString msg = wxString::Format(
    _("Active client:   Legacy MPQ\n"
      "Era / build:     %s (build %d)\n"
      "Locale:          %s\n"
      "Archives loaded: %d\n\n"
      "Folder:\n%s\n\n"
      "Browse the file list and load a model as usual."),
    wxString(prof.eraName().toStdWString()),
    prof.build,
    wxString((loc.isEmpty() ? QString("(unknown)") : loc).toStdWString()),
    archives,
    path);
  wxMessageBox(msg, _("Legacy MPQ client loaded"), wxOK | wxICON_INFORMATION, this);
  SetStatusText(wxString::Format(_("Legacy MPQ client loaded (%d archives)"), archives));
  return archives;
}

void ModelViewer::OnLoadLegacyMpq(wxCommandEvent & WXUNUSED(event))
{
  if (m_clientLoading)
    return;
  PromptAndLoadLegacyMpqClient();
}

// What a CASC open error means for the person who asked for it.
static wxString clientOpenProblem(int error)
{
  switch (error)
  {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return _("Its game data is not on this computer. The Battle.net app may not have downloaded it yet: start "
               "the game once from Battle.net, or let its installation finish, and try again.");
    case ERROR_FILE_CORRUPT:
      return _("Its game data could not be read: it is incomplete or damaged. Battle.net's Scan and Repair "
               "can restore it.");
    case ERROR_INVALID_PARAMETER:
      return _("It has no language this viewer can open.");
    default:
      return _("The installation could not be opened.");
  }
}

// The client-derived state a newly opened client replaces: what is on screen, and every cache filled from the previous
// client's files or database. (The file tree, database, races, NPC and item lists are refilled by the load itself.)
void ModelViewer::ResetClientState()
{
  // The same FileDataID is another file in the next client: the Unity player's asset cache starts over (protocol 9).
  UnityAssetAccess::noteClientReplaced();
  // ChrModel IDs are one client's.
  m_variantSession.clear();
  m_shownAsCreatureDisplay = false;
  // What is on screen: the model or building, its textures, the choosers that list the old client's records.
  if (fileControl)
    fileControl->ClearCanvas();
  if (charControl)
  {
    charControl->ClearItemDialog();
    charControl->ClientChanged();
  }
  // Decoded images and texture layouts of the previous client.
  CharTexture::clearClientCaches();
  // A legacy MPQ client's DBC tables (read only for an MPQ client).
  wow::WotlkDbc::instance().reset();
  // The previous client's races, NPCs and items: a CASC load reads them again (InitDatabase), a legacy MPQ load
  // reads no database, so it must not go on offering the previous client's.
  RaceInfos::clear();
  npcs.clear();
  items = ItemDatabase();
  m_clientCaps = ClientCapabilities();
}

bool ModelViewer::LoadWoW(const core::GameConfig * chosenConfig, bool showProgress)
{
  // One load at a time: the loading window yields to the event loop, where File > Load World of Warcraft (or the
  // empty viewer's button) could otherwise start a second one inside this one.
  if (m_clientLoading)
    return false;
  ClientLoadingFlag loadingFlag(m_clientLoading);

  wxStopWatch total, step;
  UnityAssetAccess::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
  if (gamePath.IsEmpty() || !wxDirExists(gamePath))
    getGamePath();
  const QString dataPath = QString::fromWCharArray(gamePath.c_str());

  // THE FOLDER. The loaded one when it is this same installation's CASC folder (opening another of its products
  // reuses its file objects); otherwise a new one -- first load, another installation, or after a legacy MPQ
  // client. Nothing replaces the loaded client until the new one has opened.
  auto normalized = [](QString p) {
    p = QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
    while (p.endsWith('/'))
      p.chop(1);
    return p;
  };
  const bool reuse = core::Game::instance().initDone() &&
                     GAMEDIRECTORY.clientProfile().storage == core::StorageType::CASC &&
                     normalized(GAMEDIRECTORY.path()) == normalized(dataPath);
  std::unique_ptr<wow::WoWFolder> fresh;
  core::GameFolder * folder = nullptr;
  if (reuse)
    folder = &GAMEDIRECTORY;
  else
  {
    fresh.reset(new wow::WoWFolder(dataPath));
    fresh->init(); // reads the installation's .build.info
    folder = fresh.get();
  }

  core::GameConfig config;
  if (chosenConfig)
  {
    config = *chosenConfig;
    LOG_INFO << "Client Choice selected config:" << config.locale << config.product << config.version;
  }
  else
  {
    // Headless / CLI: the build (and product) asked for with -build / -product, or the newest Retail.
    std::vector<core::GameConfig> configsFound = folder->configsFound();
    if (configsFound.empty())
    {
      LOG_ERROR << "No World of Warcraft installation found in" << dataPath;
      if (!batchMode)
        wxMessageBox(_("No World of Warcraft installation was found in that folder."), _("World of Warcraft"),
                     wxOK | wxICON_ERROR, this);
      return false;
    }
    const QString forceBuild = qEnvironmentVariable("WMV_FORCE_BUILD");
    const QString forceProduct = qEnvironmentVariable("WMV_FORCE_PRODUCT");
    int picked = -1;
    if (!forceBuild.isEmpty() || !forceProduct.isEmpty())
    {
      for (size_t i = 0; i < configsFound.size() && picked < 0; i++)
        if ((forceBuild.isEmpty() || configsFound[i].version == forceBuild) &&
            (forceProduct.isEmpty() || configsFound[i].product == forceProduct))
          picked = (int)i;
      if (picked < 0)
        LOG_WARNING << "WMV_FORCE_BUILD/PRODUCT" << forceBuild << forceProduct << "not found among detected configs";
      else
        LOG_INFO << "WMV_FORCE_BUILD/PRODUCT selected config:" << configsFound[picked].locale
                 << configsFound[picked].product << configsFound[picked].version;
    }
    if (picked < 0)
    {
      // Prefer Retail ("wow"), then the newest version.
      auto isNewer = [](const QString & a, const QString & b) {
        const QStringList va = a.split('.'), vb = b.split('.');
        for (int i = 0; i < qMax(va.size(), vb.size()); i++)
        {
          const long long na = va.value(i).toLongLong(), nb = vb.value(i).toLongLong();
          if (na != nb)
            return na > nb;
        }
        return false;
      };
      picked = 0;
      for (size_t i = 1; i < configsFound.size(); i++)
      {
        const bool bestRetail = configsFound[picked].product == "wow", iRetail = configsFound[i].product == "wow";
        if (iRetail != bestRetail ? iRetail : isNewer(configsFound[i].version, configsFound[picked].version))
          picked = (int)i;
      }
      LOG_INFO << "Auto-selected WoW config:" << configsFound[picked].locale << configsFound[picked].product
               << configsFound[picked].version;
    }
    config = configsFound[picked];
  }

  const core::ClientProfile requested = core::ClientProfile::fromGameConfig(config);
  const wxString friendly = wxString(requested.friendlyName().toStdWString());

  // Loading window: plain words, the steps the user can relate to.
  LoadingDialog * progress = 0;
  std::unique_ptr<wxWindowDisabler> disabler;
  if (showProgress)
  {
    progress = new LoadingDialog(this);
    progress->SetTitle(_("World of Warcraft"));
    progress->Show();
    // The loading window yields to the event loop at every step. Everything else is disabled meanwhile: no menu,
    // shortcut, Browse row or panel can act on a client that is being replaced (the storage is swapped and the
    // file index rebuilt in place on the reuse path).
    disabler.reset(new wxWindowDisabler(progress));
    progress->step(wxString::Format(_("Opening %s..."), friendly), 5);
  }
  auto closeProgress = [&progress, &disabler]() {
    disabler.reset();
    if (progress)
      progress->Destroy();
    progress = 0;
  };

  // Refresh the TACT keys before opening the storage -- setConfig() hands them to CASC, so a newer key list lets it
  // decrypt encrypted db2 sections + files for recently-added content.
  refreshTactKeys("extraEncryptionKeys.csv", progress);

  if (progress)
  {
    LoadingDialog * pd = progress;
    const wxString text = wxString::Format(_("Opening %s..."), friendly);
    folder->setLoadProgressCallback([pd, text](float frac) { pd->step(text, 10 + (int)(frac * 34.0f)); });
  }
  step.Start();
  const bool opened = folder->setConfig(config);
  folder->setLoadProgressCallback(std::function<void(float)>());
  const long openMs = step.Time();
  if (!opened)
  {
    const int err = folder->lastError();
    closeProgress();
    LOG_ERROR << "[clientload] could not open" << requested.describe() << "error" << err << "after" << openMs << "ms";
    if (!batchMode)
    {
      wxRichMessageDialog dlg(this, wxString::Format(_("%s could not be opened."), friendly),
                              _("World of Warcraft"), wxOK | wxICON_ERROR);
      dlg.SetExtendedMessage(clientOpenProblem(err));
      dlg.ShowDetailedText(wxString::Format(_("Product: %s\nVersion: %s\nInstallation: %s\nStorage: CASC, error %d"),
                                            wxString(config.product.toStdWString()),
                                            wxString(config.version.toStdWString()), gamePath, err));
      dlg.ShowModal();
    }
    // The client loaded before (if any) is untouched and still usable.
    return false;
  }

  // Opened: from here on this client replaces the previous one.
  if (fresh)
  {
    // The previous folder's storage is let go of; its file objects stay alive (the canvas, Browse and textures may
    // still point at them until they are rebuilt below), as they always have. Its database (and its connection to
    // the on-disk cache, which would keep the cache from being rebuilt) goes with it.
    if (core::Game::instance().initDone())
      if (wow::WoWFolder * old = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY))
        old->closeStorage();
    core::Game::instance().replace(fresh.release(), new wow::WoWDatabase());
  }
  fileControl->Disable();
  TexturesClientLoadStarting();
  ResetClientState();

  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  m_loadedBuild = config.version;
  m_loadedProduct = config.product;
  LOG_INFO << "[clientload] opened" << profile.describe() << "in" << openMs << "ms";

  // The loaded client in plain words ("Classic Era - Vanilla - 1.15.9"), its build beside it.
  const wxString clientLine =
    wxString(loadedClientSummary().toStdWString()) + wxString::Format(wxT(" (build %d)"), profile.build);
  SetStatusText(clientLine, 1);
  // The field was sized for a bare version number; it now fits the line it shows.
  if (wxStatusBar * bar = GetStatusBar())
  {
    std::vector<int> widths(bar->GetFieldsCount());
    for (size_t i = 0; i < widths.size(); i++)
      widths[i] = bar->GetStatusWidth((int)i);
    if (widths.size() > 1)
    {
      widths[1] = bar->GetTextExtent(clientLine).x + FromDIP(16);
      SetStatusWidths((int)widths.size(), widths.data());
    }
  }
  langName = GAMEDIRECTORY.locale().toStdWString();
  SetStatusText(wxString(GAMEDIRECTORY.locale().toStdWString()), 2);

  // THE SCHEMA, resolved from the opened client (never from a choice in the chooser).
  QString schemaHow;
  const QString schema = ClientInstallations::resolveSchema(profile, &schemaHow);
  m_clientSchema = schema;
  LOG_INFO << "[clientload] schema:" << schemaHow;
  core::Game::instance().setConfigFolder(schema.isEmpty() ? QString("games/wow/none/") : "games/wow/" + schema + "/");

  if (progress) progress->step(_("Reading the file list..."), 45);
  step.Start();
  refreshCommunityListfile(core::Game::instance().configFolder() + "../../../listfile.csv", progress);
  if (progress)
  {
    LoadingDialog * pd = progress;
    GAMEDIRECTORY.setLoadProgressCallback([pd](float frac) {
      pd->step(_("Reading the file list..."), 60 + (int)(frac * 12.0f)); // 60 -> 72 during the parse
    });
  }
  GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
  GAMEDIRECTORY.setLoadProgressCallback(std::function<void(float)>());
  const long listfileMs = step.Time();

  if (!customDirectoryPath.IsEmpty())
    core::Game::instance().addCustomFiles(QString::fromWCharArray(customDirectoryPath.c_str()), customFilesConflictPolicy);

  if (progress) progress->step(_("Reading game data..."), 75);
  step.Start();
  InitDatabase();
  const long databaseMs = step.Time();

  if (progress) progress->step(_("Building Browse..."), 92);
  SetStatusText(wxT("Initializing File Control..."));
  step.Start();
  fileControl->Init(this);
  const long browseMs = step.Time();
  step.Start();
  if (charControl->Init() == false)
    SetStatusText(wxT("Error Initializing the Character Controls."));
  const long charactersMs = step.Time();
  fileControl->Enable();
  // Browse and the character controls now point at this client's files only (the canvas was cleared before the
  // file list was read): the files the reload detached can go.
  if (wow::WoWFolder * live = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY))
    live->freeDetachedFiles();

  ComputeClientCapabilities();
  RememberLoadedClient(config);

  LOG_INFO << "[clientload] timing: open" << openMs << "ms, file list" << listfileMs << "ms, database" << databaseMs
           << "ms, Browse" << browseMs << "ms, characters" << charactersMs << "ms, total" << total.Time() << "ms";
  // The empty viewport points at Browse now rather than at loading a client -- once the load has
  // returned, since the client does not count as active while it is still inside it.
  CallAfter([this]() {
    UpdateUnityViewportState();
    TexturesClientLoaded();
    DisplayedContentChanged();
  });
  SetStatusText(wxString::Format(_("%s loaded."), friendly));
  if (progress)
    progress->step(_("Ready"), 100);
  closeProgress();
  return true;
}

// CAPABILITIES from what loaded (see ClientCapabilities).
void ModelViewer::ComputeClientCapabilities()
{
  ClientCapabilities caps;
  caps.loaded = core::Game::instance().initDone();
  if (!caps.loaded)
  {
    m_clientCaps = caps;
    return;
  }
  wxStopWatch clock;
  for (const auto & entry : GAMEDIRECTORY.filesByPath())
  {
    const QString & name = entry.first;
    if (name.endsWith(".m2", Qt::CaseInsensitive))
      caps.modelFiles++;
    else if (name.endsWith(".blp", Qt::CaseInsensitive))
      caps.textureFiles++;
    else if (name.endsWith(".wmo", Qt::CaseInsensitive))
      caps.buildingFiles++;
  }
  // A legacy MPQ client is opened for its models only: no database is read for it (LoadWoWFromMpq).
  const bool database = GAMEDIRECTORY.clientProfile().storage != core::StorageType::MPQ;
  auto rows = [database](const char * table) {
    if (!database)
      return 0;
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1").arg(table));
    return (r.valid && !r.empty()) ? r.values[0][0].toInt() : 0;
  };
  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  if (wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY))
    caps.filesNotInstalled = (int)folder->remoteViewerFileCount();
  QStringList notInstalledTables;
  if (wow::WoWDatabase * db = dynamic_cast<wow::WoWDatabase *>(&GAMEDATABASE))
  {
    caps.tablesNotInstalled = db->schemaCheck().notInstalled;
    caps.tablesNotRead = db->schemaCheck().notRead;
    for (const QString & note : db->schemaCheck().notes)
      if (note.endsWith(": not installed"))
        notInstalledTables << note.section(':', 0, 0);
  }
  auto missing = [&notInstalledTables](const char * table) { return notInstalledTables.contains(QString(table)); };
  caps.models = caps.modelFiles > 0;
  caps.textures = caps.textureFiles > 0;
  caps.buildings = caps.buildingFiles > 0;
  caps.races = database ? (int)RaceInfos::count() : 0;
  caps.characters = caps.races > 0;
  caps.modernCustomization = caps.characters && rows("ChrCustomizationOption") > 0 && rows("ChrCustomizationChoice") > 0;
  caps.npcCount = database ? (int)npcs.size() : 0;
  caps.npcDisplayInfo = caps.npcCount > 0 && rows("CreatureDisplayInfo") > 0;
  caps.itemCount = database ? (int)items.items.size() - 1 : 0; // "None" is always first
  caps.items = caps.itemCount > 0;
  caps.armory = caps.characters && profile.isRetailFamily();
  caps.schema = m_clientSchema.isEmpty() ? QString("none") : m_clientSchema;

  if (!caps.models)
    caps.unavailable << "Models: no model files are indexed";
  if (caps.filesNotInstalled > 0)
    caps.unavailable << QString("%1 models, textures or other viewer files of this build are not on this computer "
                                "(it is not fully downloaded)")
                          .arg(caps.filesNotInstalled);
  if (!database)
    caps.unavailable << "Characters, NPCs and items: a legacy MPQ client is opened for its models only";
  else if (!caps.characters)
    caps.unavailable << (missing("ChrRaces") || missing("ChrModel") || missing("ChrRaceXChrModel")
                           ? QString("Characters: this client's race tables are not installed on this computer")
                           : QString("Characters: no playable race could be resolved from this client's game data"));
  else if (!caps.modernCustomization)
    caps.unavailable << "Character customization: this client's customization tables could not be read";
  else if (rows("ChrClasses") == 0)
    caps.unavailable << (missing("ChrClasses")
                           ? QString("Character customization: this client's ChrClasses is not installed on this computer, so choices limited by class are not offered")
                           : QString("Character customization: this client's ChrClasses could not be read, so choices limited by class are not offered"));
  if (database && !caps.npcDisplayInfo)
    caps.unavailable << "NPCs: this client's creature tables could not be read";
  if (database && !caps.items)
    caps.unavailable << "Items: this client's item tables could not be read";
  if (!caps.armory)
    caps.unavailable << (profile.isRetailFamily() ? "Armory: needs characters" : "Armory: imports Retail characters only");
  if (!caps.textures)
    caps.unavailable << "Textures: no texture files are indexed";
  if (!caps.buildings)
    caps.unavailable << "Buildings: no world model files are indexed";

  LOG_INFO << "[clientcaps]" << profile.friendlyName() << profile.versionString << "| models" << caps.modelFiles
           << "| textures" << caps.textureFiles << "| buildings" << caps.buildingFiles << "| races" << caps.races
           << "| customization" << (caps.modernCustomization ? "yes" : "no") << "| npcs" << caps.npcCount
           << "| items" << caps.itemCount << "| armory" << (caps.armory ? "yes" : "no") << "| schema" << caps.schema
           << "| files not installed" << caps.filesNotInstalled << "| tables not installed" << caps.tablesNotInstalled
           << "| tables not read" << caps.tablesNotRead
           << "|" << clock.Time() << "ms";
  for (const QString & u : caps.unavailable)
    LOG_INFO << "[clientcaps] unavailable:" << u;
  m_clientCaps = caps;
}

void ModelViewer::RememberLoadedClient(const core::GameConfig & config)
{
  QSettings settings(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
  settings.setValue("Client/LastPath", QString::fromWCharArray(gamePath.c_str()));
  settings.setValue("Client/LastProduct", config.product);
}

QString ModelViewer::loadedClientSummary() const
{
  if (!core::Game::instance().initDone())
    return QString();
  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  return profile.friendlyName() + " " + QChar(0x00B7) + " " + profile.versionLabel();
}

void ModelViewer::OnCharToggle(wxCommandEvent &event)
{
  int ID = event.GetId();
  // One command each: View NPC used to fall through to the character toggles below, which changed nothing but
  // refreshed the whole character and sent its scene again every time the NPC list was opened.
  if (ID == ID_VIEW_NPC)
    charControl->selectNPC(UPDATE_NPC);
  else if (ID == ID_VIEW_ITEM)
    charControl->selectItem(UPDATE_SINGLE_ITEM, -1);
  else if (isChar)
    charControl->OnCheck(event);
}

// Character > Load NPC / Model...: an NPC from a Wowhead link (loaded here, as before: LoadNPCByDisplay), or a model by
// an ID of a type the user picks (loaded by the dialog itself, which stays open to say why when it cannot).
void ModelViewer::OnImportNPCFromURL(wxCommandEvent &event)
{
  NPCimporterDialog * dlg = new NPCimporterDialog(this, NPCimporterDialog::Use::Load);
  if (dlg->ShowModal() == wxID_OK)
  {
    const int npcId = dlg->getImportedId();
    if (npcId != -1)
    {
      const QString line = dlg->getNPCLine();         // "id,displayId,type,name"
      const int displayId = line.section(',', 1, 1).toInt();
      const int type = line.section(',', 2, 2).toInt();
      const QString name = line.section(',', 3);      // remainder, tolerates commas in the name
      LoadNPCByDisplay(npcId, displayId, type, name);
    }
  }
  dlg->Destroy();
}

void ModelViewer::OnMount(wxCommandEvent &event)
{
  /*
  const unsigned int mountSlot = 0;

  // check if it's mountable
  if (!canvas->viewingModel) return;
  Model *root = (Model*)canvas->root->model;
  if (!root) return;
  if (root->name.substr(0,8)!="Creature") return;
  bool mountable = (root->header.nAttachLookup > mountSlot) && (root->attLookup[mountSlot]!=-1);
  if (!mountable) return;

  wxString fn = charControl->selectCharModel();
  if (fn.length()==0) return;

  canvas->root->delChildren();
  Attachment *att = canvas->root->addChild(fn.c_str(), mountSlot, -1);

  wxHostInfo hi;
  hi = layoutManager->GetDockHost(wxDEFAULT_RIGHT_HOST);
  if (!charControlDockWindow->IsDocked()) {
  layoutManager->DockWindow(charControlDockWindow, hi);
  charControlDockWindow->Show(TRUE);
  }
  charMenu->Check(ID_SHOW_UNDERWEAR, true);
  charMenu->Check(ID_SHOW_EARS, true);
  charMenu->Check(ID_SHOW_HAIR, true);
  charMenu->Check(ID_SHOW_FACIALHAIR, true);

  Model *m = (Model*)att->model;
  charControl->UpdateModel(att);

  menuBar->EnableTop(2, true);
  isChar = true;

  // find a Mount animation (id = 91, let's hope this doesn't change)
  for (size_t i=0; i<m->header.nAnimations; i++) {
  if (m->anims[i].animID == 91) {
  m->currentAnim = (int)i;
  break;
  }
  }
  */

  charControl->selectMount();
}

bool ModelViewer::SaveChar(QString fn, bool equipmentOnly /*= false*/)
{
  WoWModel * m = canvas ? const_cast<WoWModel *>(canvas->model()) : nullptr;
  if (!m)
    return false;
  QFile file(fn);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << fn;
    return false;
  }

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();
  stream.writeStartElement("SavedCharacter");
  stream.writeAttribute("version", "2.0");
  // save model itself
  if (equipmentOnly)
    stream.writeAttribute("equipmentModel", QString::fromStdString(m->modelname));
  if (!equipmentOnly)
    m->save(stream);

  // then save equipment
  stream.writeStartElement("equipment");

  for (WoWModel::iterator it = m->begin();
       it != m->end();
       ++it)
       (*it)->save(stream);

  stream.writeEndElement(); // equipment

  stream.writeEndElement(); // SavedCharacter
  stream.writeEndDocument();

  const bool ok = !stream.hasError() && file.flush();
  file.close();
  return ok;
}

bool ModelViewer::LoadFbxEquipment(QString fn)
{
  WoWModel * m = canvas ? const_cast<WoWModel *>(canvas->model()) : nullptr;
  QFile file(fn);
  QDomDocument doc;
  if (!m || m->charModelDetails.isChar || !file.open(QIODevice::ReadOnly) || !doc.setContent(&file))
    return false;
  const QDomElement root = doc.documentElement();
  if (root.tagName() != "SavedCharacter" || root.attribute("version") != "2.0" ||
      root.attribute("equipmentModel") != QString::fromStdString(m->modelname))
    return false;

  // Validate the entire snapshot before applying it. Empty slots are authoritative too:
  // a fresh -npc load may supply default weapons that the user has since removed.
  struct SavedItem { int id; int display; };
  std::map<int, SavedItem> saved;
  const QDomElement equipment = root.firstChildElement("equipment");
  for (QDomElement e = equipment.firstChildElement("item"); !e.isNull(); e = e.nextSiblingElement("item"))
  {
    bool slotOk = false, idOk = false, displayOk = false, levelOk = false;
    const int slot = e.firstChildElement("slot").attribute("value").toInt(&slotOk);
    const int id = e.firstChildElement("id").attribute("value").toInt(&idOk);
    const int display = e.firstChildElement("displayId").attribute("value").toInt(&displayOk);
    const int level = e.firstChildElement("level").attribute("value").toInt(&levelOk);
    if (!slotOk || !idOk || !displayOk || !levelOk || slot < 0 || slot >= NUM_CHAR_SLOTS ||
        id < -1 || display < -1 || level < 0 || saved.count(slot) || !m->getItem((CharSlots)slot))
      return false;
    saved[slot] = { id, display };
  }
  size_t expectedSlots = 0;
  for (auto it = m->begin(); it != m->end(); ++it)
    ++expectedSlots;
  if (saved.size() != expectedSlots)
    return false;
  file.close();

  for (const auto & entry : saved)
  {
    WoWItem * item = m->getItem((CharSlots)entry.first);
    item->setId(0);
    const bool equipped = entry.second.id > 0 || (entry.second.id == -1 && entry.second.display > 0);
    if (equipped)
    {
      // Reuse .chr's exact display/variant restoration, independently of racial customization.
      if (entry.second.id == -1)
      {
        // setId(0) unloads geometry but retains the old display ID. Force a new display load
        // even when a default NPC weapon already had the display we are restoring.
        item->setDisplayId(-1);
        item->setDisplayId(entry.second.display);
      }
      item->load(fn);
      item->refresh();
      if (!item->isEquipped() ||
          ((entry.first == CS_HAND_LEFT || entry.first == CS_HAND_RIGHT) && item->models().empty()))
      {
        LOG_ERROR << "[fbxequipment] could not restore slot" << entry.first;
        return false;
      }
    }
    LOG_INFO << "[fbxequipment] slot" << entry.first << "id" << entry.second.id
             << "display" << entry.second.display << "models" << (int)item->models().size();
  }
  // Do not call LoadChar/RefreshModel: they also apply body/geoset customization.
  return true;
}

bool ModelViewer::PrepareFbxAsset(wxString & args, wxString & label, wxString & tempCharPath)
{
  args.clear();
  label.clear();
  tempCharPath.clear();
  WoWModel * m = canvas ? const_cast<WoWModel *>(canvas->model()) : nullptr;
  if (!m || m->modelname.empty())
    return false;

  if (!isChar)
  {
    if (m_exportNpcId > 0)
    {
      args = wxString::Format(wxT("-npc %d:%d"), m_exportNpcId, m_exportNpcDisplayId);
      label = wxString::Format(wxT("NPC %d"), m_exportNpcId);
    }
    else
    {
      label = wxString::FromUTF8(m->modelname.c_str());
      label.Replace(wxT("\\"), wxT("/"));
      args = wxT("-mo \"") + label + wxT("\"");
      if (m_exportItemSkinFileId > 0)
        args << wxString::Format(wxT(" -itemskin %d"), m_exportItemSkinFileId);
    }
  }

  // Non-racial models retain their original load route, plus an equipment-only snapshot.
  // Never route an exclusive NPC through the racial character customization loader.
  const bool equipmentOwner = charControl && charControl->model == m && m->attachment &&
                              charControl->charAtt == m->attachment;
  if (isChar || equipmentOwner)
  {
    const wxString base = wxFileName::CreateTempFileName(wxT("wmvexport"));
    if (base.IsEmpty())
      return false;
    wxRemoveFile(base);
    tempCharPath = base + wxT(".chr");
    if (!SaveChar(QString::fromStdWString(tempCharPath.ToStdWstring()), !isChar))
    {
      if (wxFileName::FileExists(tempCharPath)) wxRemoveFile(tempCharPath);
      tempCharPath.clear();
      return false;
    }
    if (isChar)
    {
      args = wxT("\"") + tempCharPath + wxT("\"");
      label = wxT("character");
    }
    else
      args << wxT(" -fbxequipment \"") << tempCharPath << wxT("\"");
  }
  return !args.IsEmpty();
}

void ModelViewer::LoadChar(QString fn, bool equipmentOnly /* = false */)
{
  SetViewerMode(ViewerMode::Models);
  // Described to the Unity viewport once, dressed, when this returns: see SceneHold.
  SceneHold sceneHold(this);

  QFile file(fn);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << fn;
    return;
  }

  // A loaded character is exported via SaveChar, not the NPC path -- drop any NPC descriptor.
  if (!equipmentOnly)
  {
    m_exportNpcId = -1;
    m_exportNpcDisplayId = 0;
    // And it is another character, also when its file is the one on screen (LoadModel's same-file path, which a file
    // without race and sex takes without starting anything afresh): what the last one looked like on its other model
    // is not this one's, and it is no creature display.
    if (m_variantSwitching == 0)
      m_variantSession.clear();
    m_shownAsCreatureDisplay = false;
    m_exportItemSkinFileId = 0;
  }

  if (!equipmentOnly)
  {
    // Clear the existing model. ClearWMO detaches canvas->root and clears g_selWMO before the delete;
    // a plain delete here left both dangling.
    if (isWMO)
      canvas->ClearWMO();
  }

  bool loadCharDetails = true;

  QXmlStreamReader reader;
  reader.setDevice(&file);
  while (!reader.atEnd())
  {
    reader.readNext();
    if (reader.hasError())
    {
      file.close();
      file.open(QIODevice::ReadOnly | QIODevice::Text);
      LOG_INFO << "Loading character from legacy file";
      QTextStream in(&file);

      std::vector<QString> values;
      while (!in.atEnd())
      {
        QString line = in.readLine();
        if (!line.isEmpty())
          values.push_back(line);
      }

      unsigned int lineIndex = 0;

      // modelname (if available)
      if (values[lineIndex].contains("m2", Qt::CaseInsensitive))
      {
        LOG_INFO << "modelname" << values[lineIndex];

        // Load the model
        LoadModel(GAMEDIRECTORY.getFile(values[lineIndex]));
        WoWModel * m = const_cast<WoWModel *>(canvas->model());
        m->modelType = MT_CHAR;
        lineIndex++;
      }

      // race and gender, we don't care
      lineIndex++;

      // character customization
      QStringList multiVal = values[lineIndex].split(" ");
      if (multiVal.size() >= 5)
      {
        LOG_INFO << "skin color" << multiVal[0];
        charControl->model->cd.set(CharDetails::SKIN_COLOR, multiVal[0].toInt());

        LOG_INFO << "face type" << multiVal[1];
        charControl->model->cd.set(CharDetails::FACE, multiVal[1].toInt());

        LOG_INFO << "hair color" << multiVal[2];
        charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, multiVal[2].toInt());

        LOG_INFO << "hair style" << multiVal[3];
        charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, multiVal[3].toInt());

        LOG_INFO << "facial hair" << multiVal[4];
        charControl->model->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, multiVal[4].toInt());
      }

      // eye glow (if present)
      if (multiVal.size() >= 7)
      {
        LOG_INFO << "reading eyeglow from file:" << multiVal[6].toInt();
        charControl->model->cd.eyeGlowType = (EyeGlowTypes)multiVal[6].toInt();
      }
      else
      {
        // Otherwise, default to this value
        LOG_INFO << "setting eye glow to default value";
        charControl->model->cd.eyeGlowType = EGT_DEFAULT;
      }

      lineIndex++;

      CharSlots legacySlots[15] = { CS_HEAD, NUM_CHAR_SLOTS, CS_SHOULDER, CS_BOOTS, CS_BELT, CS_SHIRT, CS_PANTS, CS_CHEST, CS_BRACERS, CS_GLOVES, CS_HAND_RIGHT,
        CS_HAND_LEFT, CS_CAPE, CS_TABARD, NUM_CHAR_SLOTS };

      for (unsigned int i = 0; i < 15 && lineIndex < values.size(); i++, lineIndex++)
      {
        LOG_INFO << "item" << i << "=>" << values[lineIndex].toInt();
        WoWItem * item = charControl->model->getItem(legacySlots[i]);
        if (item)
          item->setId(values[lineIndex].toInt());
      }

      // read tabard customization (if needed)
      if (lineIndex < values.size())
      {
        WoWItem * tabard = charControl->model->getItem(CS_TABARD);
        // 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
        if (tabard && (tabard->id() == 5976 || tabard->id() == 69209 || tabard->id() == 69210))
        {
          LOG_INFO << "read custom tabard values" << values[lineIndex];
          multiVal = values[lineIndex].split(" ");
          if (multiVal.size() >= 5)
          {
            charControl->model->td.setBackground(multiVal[0].toInt());
            charControl->model->td.setBorder(multiVal[1].toInt());
            charControl->model->td.setBorderColor(multiVal[2].toInt());
            charControl->model->td.setIcon(multiVal[3].toInt());
            charControl->model->td.setIconColor(multiVal[4].toInt());
            charControl->model->td.showCustom = true;
          }
        }
      }
    }

    if (reader.isStartElement())
    {
      if (reader.name() == "SavedCharacter") // check file version
      {
        QString version = reader.attributes().value("version").toString();
        if (version == "1.0" && !equipmentOnly)
        {
          loadCharDetails = false;
          wxMessageBox(wxT("You are loading a character file customized before Shadowlands. Character customization won't be applied."), wxT("Character customization"), wxOK | wxICON_INFORMATION);
        }
      }

      if (reader.name() == "model" && !equipmentOnly)
      {
        reader.readNext();
        while (reader.isStartElement() == false)
          reader.readNext();

        if (reader.name() == "file")
        {
          QString modelname = reader.attributes().value("name").toString();
          // The race and sex the character was saved as, when the file names them: one model file can carry several
          // races (MoP Classic's Human files list Gilnean first, Classic Beta's Skyborne files two races), and the
          // saved choices are options of the saved race's model. A file saved before it named them loads as it did.
          bool raceRead = false, sexRead = false;
          const int race = reader.attributes().value("race").toString().toInt(&raceRead);
          const int sex = reader.attributes().value("sex").toString().toInt(&sexRead);
          const bool raceSaved = raceRead && sexRead && race > 0;
          LoadModel(GAMEDIRECTORY.getFile(modelname), raceSaved ? race : -1, raceSaved ? sex : -1);
          WoWModel * m = const_cast<WoWModel *>(canvas->model());
          if(loadCharDetails)
            m->load(fn);
        }
        else
        {
          LOG_ERROR << "Failed to find character filename in file";
          return;
        }
      }

      if (reader.name() == "equipment")
      {
        WoWModel * m = const_cast<WoWModel *>(canvas->model());
        for (WoWModel::iterator it = m->begin();
             it != m->end();
             ++it)
             (*it)->load(fn);
      }
    }
  }

  charControl->RefreshModel();
  charControl->RefreshEquipment();
  // Rebuild the attachment list (View > Attachments) so a loaded character's helm/shoulders/weapon models
  // are selectable immediately (previously the list stayed empty until an item was re-equipped).
  if (canvas && canvas->root)
    modelControl->RefreshModel(canvas->root);

  charMenu->Enable(ID_SAVE_CHAR, true);
  charMenu->Enable(ID_SHOW_UNDERWEAR, true);
  charMenu->Enable(ID_SHOW_EARS, true);
  charMenu->Enable(ID_SHOW_HAIR, true);
  charMenu->Enable(ID_SHOW_FACIALHAIR, true);
  charMenu->Enable(ID_SHOW_FEET, true);
  charMenu->Enable(ID_SHEATHE, true);
  charMenu->Enable(ID_CHAREYEGLOW, true);
  charMenu->Enable(ID_SAVE_EQUIPMENT, true);
  charMenu->Enable(ID_LOAD_EQUIPMENT, true);
  charMenu->Enable(ID_CLEAR_EQUIPMENT, true);
  charMenu->Enable(ID_LOAD_SET, true);
  charMenu->Enable(ID_LOAD_START, true);
  charMenu->Enable(ID_MOUNT_CHARACTER, true);
  charMenu->Enable(ID_CHAR_RANDOMISE, true);
  charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);

  // Update interface docking components
  interfaceManager.Update();
}

void ModelViewer::OnLanguage(wxCommandEvent &event)
{
  if (event.GetId() == ID_LANGUAGE) {
    // the arrays should be in sync
    wxCOMPILE_TIME_ASSERT(WXSIZEOF(langNames) == WXSIZEOF(langIds), LangArraysMismatch);

    long lng = wxGetSingleChoiceIndex(_("Please select a language:"), _("Language"), WXSIZEOF(langNames), langNames);

    if (lng != -1 && lng != interfaceID) {
      interfaceID = lng;
      wxMessageBox(wxT("You will need to reload WoW Model Viewer for changes to take effect."), wxT("Language Changed"), wxOK | wxICON_INFORMATION);
    }
  }
}

void ModelViewer::OnAbout(wxCommandEvent &event)
{
  wxAboutDialogInfo info;
  info.SetName(GLOBALSETTINGS.appName());
  wxString l_version = L"\n" + GLOBALSETTINGS.appVersion() + L" (" + GLOBALSETTINGS.buildName() + L")\n";

  if (GLOBALSETTINGS.isBeta())
    l_version += L"BETA VERSION";

  info.SetVersion(l_version);

  info.AddDeveloper(wxT("Ufo_Z"));
  info.AddDeveloper(wxT("Darjk"));
  info.AddDeveloper(wxT("Chuanhsing"));
  info.AddDeveloper(wxT("Kjasi (A.K.A. Sephiroth3D)"));
  info.AddDeveloper(wxT("Tob.Franke"));
  info.AddDeveloper(wxT("Jeromnimo"));
  info.AddDeveloper(wxT("Wain"));
  info.AddTranslator(wxT("MadSquirrel (French)"));
  info.AddTranslator(wxT("Tigurius (Deutsch)"));
  info.AddTranslator(wxT("Kurax (Chinese)"));

  info.SetWebSite(wxT("https://wowmodelviewer.net"));
  info.SetCopyright(
    wxString(wxT("World of Warcraft(R) is a Registered trademark of\n\
                 Blizzard Entertainment(R). All game assets and content\n\
                 is (C)2004-2016 Blizzard Entertainment(R). All rights reserved.")));

  info.SetLicence(wxT("WoW Model Viewer is released under the GNU General Public License v3, Non-Commercial Use."));

  info.SetDescription(wxT("WoW Model Viewer is a 3D model viewer for World of Warcraft.\nIt uses the data files included with the game to display\nthe 3D models from the game: creatures, characters, spell\neffects, objects and so forth.\n\nCredits To: Linghuye,  nSzAbolcs,  Sailesh, Terran and Cryect\nfor their contributions either directly or indirectly."));

  wxBitmap * bitmap = createBitmapFromResource(L"ABOUTICON", wxBITMAP_TYPE_XPM, 128, 128);
  wxIcon icon;
  icon.CopyFromBitmap(*bitmap);

#if defined (_LINUX)
  //icon.LoadFile(wxT("../bin_support/icon/wmv_xpm"));
#elif defined (_MAC)
  //icon.LoadFile(wxT("../bin_support/icon/wmv.icns"));
#endif

  info.SetIcon(icon);

  // FIXME: Doesn't link on OSX
  wxAboutBox(info);
}

bool ModelViewer::isUnityViewportCentre()
{
  if (!unityRendererHost)
    return false;
  // The host is always managed (InitDocking), so this lookup never lands on wx's shared null pane.
  wxAuiPaneInfo & up = interfaceManager.GetPane(unityRendererHost);
  return up.IsOk() && up.IsShown() && up.dock_direction == wxAUI_DOCK_CENTER;
}

bool ModelViewer::isUnityViewportShowingModel()
{
  if (!unityRendererHost || !canvas || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return false;
  return !unityRendererHost->hasNotice() && !unityRendererHost->isShowingContent() && unityCanDrawCurrentModel();
}

bool ModelViewer::unityViewportHasNotice() const
{
  return !unityRendererHost || unityRendererHost->hasNotice() || unityRendererHost->isShowingContent();
}

// The size of the viewport, which is the Unity viewport's host panel (the archived canvas is hidden and
// has no meaningful size). Called by the host whenever it is resized.
void ModelViewer::UpdateCanvasStatus()
{
  if (!unityRendererHost || !GetStatusBar())
    return;
  const wxSize size = unityRendererHost->GetClientSize();
  SetStatusText(wxString::Format(wxT("Viewport %i \u00D7 %i"), size.x, size.y), 3);
}

// Facts about what is loaded, for the status bar: the model's name is already in the title bar
// and the command bar, so this says what it is made of instead. Counts that do not apply are
// left out rather than shown as zero.
void ModelViewer::UpdateStatusFacts()
{
  if (!GetStatusBar() || !canvas)
    return;

  // Textures mode: the selected texture's size, format and alpha (nothing before it is decoded).
  if (isTextureMode() && textureView)
  {
    SetStatusText(textureView->statusFacts(), 0);
    return;
  }

  wxArrayString parts;
  auto count = [&parts](size_t n, const wxString & one, const wxString & many) {
    if (n > 0)
      parts.Add(wxNumberFormatter::ToString((long)n, wxNumberFormatter::Style_WithThousandsSep) +
                wxT(" ") + (n == 1 ? one : many));
  };

  // What the viewer mode shows only (a world model on the canvas in Models, or a model in Buildings, is not on screen).
  if (isBuildingsMode() && canvasHasWorldModel())
  {
    count(canvas->wmo->nGroups, _("group"), _("groups"));
    count(canvas->wmo->modelis.size(), _("doodad"), _("doodads"));
    count(canvas->wmo->doodadsets.size(), _("doodad set"), _("doodad sets"));
  }
  else if (isModelsMode() && canvasHasModel())
  {
    const WoWModel * m = canvas->model();
    count(m->origVertices.size(), _("vertex"), _("vertices"));
    count(m->indices.size() / 3, _("triangle"), _("triangles"));
    count(m->geosets.size(), _("submesh"), _("submeshes"));
    count(m->anims.size(), _("animation"), _("animations"));
  }

  wxString text;
  for (size_t i = 0; i < parts.size(); i++)
    text += (i ? wxT("  \u00B7  ") : wxT("")) + parts[i];
  SetStatusText(text, 0);
}

// The status bar's first field says what is on screen (UpdateStatusFacts). wx blanks it while the mouse is
// over a menu item or toolbar button that has no help text, and on leaving puts back what the field said
// when the mouse arrived: after a click that changed what is on screen, the facts of what was there before
// (the model's counts over the Textures viewer, which has none to show until a texture is picked). Help
// text, where there is some, is still shown and taken away (an item without any that follows it clears it,
// as wx does); otherwise an item without help leaves the field alone. No menu item or tool has help today.
void ModelViewer::DoGiveHelp(const wxString & text, bool show)
{
  if (!m_helpInStatus && (!show || text.IsEmpty()))
    return;
  m_helpInStatus = show;
  wxFrame::DoGiveHelp(text, show);
}

void ModelViewer::DisplayedContentChanged()
{
  if (!canvas)
    return;

  // The viewport first: the model, the empty viewer, or a notice for what it cannot draw yet.
  UpdateUnityViewportState();
  // The colour for what is loaded (viewportBackgroundShown): the panel's backdrop and the command line of a player
  // started later follow it, with or without a player. The player itself is sent it only with a load it gets
  // (SendLoadToUnity): a load that failed, or a world model that cannot be read, leaves it drawing what it had.
  if (unityRendererHost)
    unityRendererHost->setBackdrop(viewportBackgroundShown());

  // The Animation panel drives the model: greyed in the other viewers (put away there, too).
  if (animControl)
    animControl->Enable(isModelsMode());

  if (commandModelLabel)
  {
    // What the viewer mode shows: the texture, the world model (Buildings) or the model (Models).
    wxString path, tip;
    if (isTextureMode() && textureView)
      path = textureView->displayPath().IsEmpty() ? textureView->displayName() : textureView->displayPath();
    else if (isBuildingsMode() && canvasHasWorldModel())
    {
      path = canvas->wmo->itemName().toStdWString();
      if (canvas->wmo->fileDataID > 0)
        tip = path + wxString::Format(wxT("  [%u]"), (unsigned)canvas->wmo->fileDataID);
    }
    else if (isModelsMode() && canvasHasModel())
      path = canvas->model()->gamefile ? canvas->model()->gamefile->fullname().toStdWString()
                                       : const_cast<WoWModel *>(canvas->model())->name().toStdWString();
    wxString name = path;
    name.Replace(wxT("/"), wxT("\\"));
    name = name.AfterLast('\\');
    const wxString empty = isTextureMode() ? _("No texture selected") : isBuildingsMode() ? _("No building selected")
                                                                                          : _("No model loaded");
    commandModelLabel->SetLabel(name.IsEmpty() ? empty : name);
    UiStyle::setRole(commandModelLabel, name.IsEmpty() ? UiStyle::Role::SecondaryText : UiStyle::Role::Text);
    commandModelLabel->SetToolTip(tip.IsEmpty() ? path : tip);
    commandModelLabel->Refresh();
  }
  UpdateTitle();

  if (modelInspector)
    modelInspector->ContentChanged();
  if (fileControl)
    fileControl->PickedRowFollowsLoad();

  UpdateStatusFacts();
  UpdateCanvasStatus();
}

// ONE CHANGE ON SCREEN. A switch changes the whole workspace -- the selector, the centre (texture view
// or the player), the panels put away or given back, Browse's list -- and the screen goes from the old
// workspace straight to the new one, with nothing in between: no panel going before another, no centre
// growing into the space they leave, no captions drawn over the old contents. So every change is made
// with the frame's windows frozen and laid out once (the layout batch: one wxAuiManager::Update, with the dock cap
// lifted when panels come back) and everything is painted at once (PaintNow). The player -- another process,
// which the freeze does not stop -- changes with that repaint (the hold): its frame moved off screen at the end
// of the freeze (a notice) or once the texture view is painted, sized and put back as the repaint starts.
void ModelViewer::beginLayoutBatch()
{
  m_layoutBatch++;
}

void ModelViewer::endLayoutBatch()
{
  if (m_layoutBatch <= 0 || --m_layoutBatch > 0)
    return;
  if (m_layoutPending)
  {
    m_layoutPending = false;
    if (m_layoutUncapped)
    {
      m_layoutUncapped = false;
      commitDocksAtTheirSize();
    }
    else
      interfaceManager.Update();
  }
  if (m_finishWorkspacePending)
  {
    m_finishWorkspacePending = false;
    finishWorkspace();
  }
}

void ModelViewer::PaintNow()
{
  std::function<void(wxWindow *)> paint = [&](wxWindow * w) {
    if (!w->IsShown())
      return;
    w->Update();
    for (wxWindow * child : w->GetChildren())
      if (!child->IsTopLevel())
        paint(child);
  };
  paint(this);
}

void ModelViewer::SetViewerMode(ViewerMode mode, bool openBrowse, bool paintNow)
{
  if (mode == m_viewerMode)
    return;
  const ViewerMode previous = m_viewerMode;
  m_viewerMode = mode;
  LOG_INFO << "[viewport] viewer mode:"
           << (mode == ViewerMode::Textures ? "Textures" : mode == ViewerMode::Buildings ? "Buildings" : "Models");
  const bool textures = mode == ViewerMode::Textures;
  struct PlayerHold
  {
    UnityRendererHost * host;
    explicit PlayerHold(UnityRendererHost * h) : host(h) { if (host) host->holdPlayer(); }
    ~PlayerHold() { release(); }
    void release() { if (host) host->releasePlayer(); host = nullptr; }
  } hold(unityRendererHost);
  bool parkedNow = false;
  {
    // Every window of the frame frozen, hidden ones too (a panel shown during the switch stays frozen) --
    // but not the frame itself: a frozen top-level window is invisible to Windows' hit-testing, and a
    // click during a long switch (the first Textures listing) would go to the window behind it.
    std::vector<std::unique_ptr<wxWindowUpdateLocker>> freeze;
    for (wxWindow * child : GetChildren())
      if (!child->IsTopLevel())
        freeze.emplace_back(new wxWindowUpdateLocker(child));
    beginLayoutBatch();
    // The selector: a switch made by a load from a menu does not come through the toolbar.
    if (commandBar)
      commandBar->ToggleTool(textures ? ID_UI_TEXTURES : mode == ViewerMode::Buildings ? ID_UI_BUILDINGS : ID_UI_MODELS,
                             true);
    if (textureView)
    {
      if (textures)
        textureView->entered();
      else if (previous == ViewerMode::Textures)
        textureView->left();
    }
    // A chooser a model command left open (View NPC, View Item, an equipment slot, an item set, a start
    // outfit, a mount): picked from in another viewer, it would load into the Models viewer or change the
    // model behind it. It is not opened again in Models.
    if (previous == ViewerMode::Models && charControl)
      charControl->ClearItemDialog();
    // The Models viewport's background window is put away like the model panels: what it sets is not on show in
    // the other viewers, where its command is greyed. Models gives it back if it was open (without taking the
    // keyboard from the viewer).
    if (m_backgroundDialog)
    {
      if (previous == ViewerMode::Models)
      {
        m_backgroundDialogPutAway = m_backgroundDialog->IsShown();
        if (m_backgroundDialogPutAway)
          m_backgroundDialog->Hide();
      }
      else if (mode == ViewerMode::Models && m_backgroundDialogPutAway)
      {
        m_backgroundDialogPutAway = false;
        m_backgroundDialog->Sync();
        m_backgroundDialog->ShowWithoutActivating();
      }
    }
    // (The viewport's clear colour follows what the player draws, not the viewer: a switch sends none.)
    // The centre, the panels and the menus (UpdateInterface -> DisplayedContentChanged ->
    // UpdateUnityViewportState puts the panels away or gives them back), Browse opened when asked, and
    // Browse's list, which the first entry into Textures after a client load lists from a catalogue
    // built there.
    if (fileControl)
      fileControl->UpdateInterface();
    else
      DisplayedContentChanged();
    if (openBrowse && fileControl)
    {
      wxAuiPaneInfo & browse = interfaceManager.GetPane(fileControl);
      if (!browse.IsShown())
      {
        browse.Show(true);
        m_layoutPending = true;
      }
    }
    if (fileControl)
      fileControl->FollowViewerMode(mode);
    endLayoutBatch();
    if (m_uncoverPending)
    {
      m_uncoverPending = false;
      unityRendererHost->showContent(false);
    }
    // The panel toggles answered now, not one idle later, so they change with everything else; and the
    // menus, which wx answers only when one opens, while a shortcut is checked against its menu item.
    if (commandBar)
      commandBar->UpdateWindowUI(wxUPDATE_UI_FROMIDLE);
    DoMenuUpdates();
    // A notice now covers the player (the viewer has nothing of its own to show): its frame goes off screen here, at
    // the end of the freeze -- the old workspace stayed whole until now. (Not for the texture view: it is painted
    // before the player goes, after PaintNow.) The viewport is let go of with the rest: let go of first, the player
    // was seen on screen in the new workspace for a few milliseconds before it went.
    if (!textures && unityRendererHost && unityRendererHost->hasNotice())
      parkedNow = unityRendererHost->coverPlayerNow();
  }
  // Where it was, plain dark at once, before anything else (a load that made this switch goes on before the repaint):
  // not what was under the player's window. The notice follows with the repaint.
  if (parkedNow)
    unityRendererHost->paintPlainNow();
  // Going to Models or Buildings the player is let go as the repaint starts: put back (its frame) and sized as the
  // panels paint -- or, behind a notice, it went at the end of the freeze. Going to Textures it is taken off screen
  // after the repaint, so the texture view -- raised above it, but no cover while it is on screen -- is already
  // painted when it goes.
  // A window that had the keyboard and was put away while frozen keeps it (a hidden window's hide moves
  // the focus away only when Windows sees it hidden, and frozen it already reads so): the keyboard to
  // Browse's search box, or the frame. Not the player's window (seen here as its frame): it keeps the keyboard,
  // and taking it away would wait on the player's thread.
  // (Asked of the player first, which FindFocus would ask WM_GETDLGCODE and wait for.)
  if (wxWindow * focus = unityRendererHost && unityRendererHost->playerHasKeyboard() ? nullptr : wxWindow::FindFocus())
    if (!focus->IsShownOnScreen() && !(unityRendererHost && focus == unityRendererHost->playerFrame()))
    {
      if (fileControl && fileControl->txtContent && fileControl->txtContent->IsShownOnScreen())
        fileControl->txtContent->SetFocus();
      else
        SetFocus();
    }
  if (!textures)
    hold.release();
  if (paintNow)
    PaintNow();
}

void ModelViewer::ShowTexture(const TextureEntry & entry, bool lookup)
{
  if (!textureView)
    return;
  SetViewerMode(ViewerMode::Textures);
  textureView->show(entry, lookup);
}

void ModelViewer::TextureSelectionChanged()
{
  if (!isTextureMode())
    return;
  if (commandModelLabel && textureView)
  {
    const wxString path = textureView->displayPath().IsEmpty() ? textureView->displayName() : textureView->displayPath();
    wxString name = path;
    name.Replace(wxT("/"), wxT("\\"));
    name = name.AfterLast('\\');
    commandModelLabel->SetLabel(name.IsEmpty() ? wxString(_("No texture selected")) : name);
    UiStyle::setRole(commandModelLabel, name.IsEmpty() ? UiStyle::Role::SecondaryText : UiStyle::Role::Text);
    commandModelLabel->SetToolTip(path);
    commandModelLabel->Refresh();
  }
  UpdateStatusFacts();
  // The Model panel's Info names the texture, were the panel open in Textures mode (it cannot be: its
  // toggle is the Models viewer's, needsModelViewer; not on every pick otherwise, which would rebuild its
  // other pages for nothing).
  if (modelInspector && interfaceManager.GetPane(modelInspector).IsShown())
    modelInspector->ContentChanged();
}

void ModelViewer::TexturesClientLoadStarting()
{
  // The old client's texture and cache go with it; Textures mode stays (the view says the client is
  // loading meanwhile).
  if (textureView)
    textureView->clientLoadStarting();
  if (fileControl)
    fileControl->TexturesClientLoadStarting();
  TextureSelectionChanged();
}

void ModelViewer::TexturesClientLoaded()
{
  // (Browse's watch tells the texture view once the load has really ended.)
  if (fileControl)
    fileControl->TexturesClientLoaded();
}

// The viewer selector, Models | Textures | Buildings (and the empty viewer's button, which is the mode's own): the
// mode at once; then, with no client, the client prompt (also when the mode was already this one: that is how the
// empty viewer's "Load World of Warcraft..." works); then Browse, shown, with the keyboard.
void ModelViewer::OnCommandBar(wxCommandEvent & event)
{
  switch (event.GetId())
  {
    case ID_UI_MODELS:
    case ID_UI_TEXTURES:
    case ID_UI_BUILDINGS:
    {
      // A client load yields to the event loop (its progress dialog), so this can be clicked in the middle
      // of one; switching Browse's lists then, or starting another load, is not safe. The selector goes
      // back to the mode at the next idle update.
      if (UnityAssetAccess::isClientLoading())
        return;
      // From the id only, never the tool's checked state: a click on the active radio tool, a double-click
      // and the empty viewer's posted command all arrive unchecked.
      const ViewerMode mode = event.GetId() == ID_UI_TEXTURES    ? ViewerMode::Textures
                            : event.GetId() == ID_UI_BUILDINGS   ? ViewerMode::Buildings
                                                                 : ViewerMode::Models;
      // Browse (closed) opens in the same layout as the switch, when there is a client to list in it.
      const bool openBrowse = UnityAssetAccess::hasActiveClient() && !interfaceManager.GetPane(fileControl).IsShown();
      SetViewerMode(mode, openBrowse, true);
      if (!UnityAssetAccess::hasActiveClient())
      {
        PromptAndLoadClient();
        UpdateUnityViewportState();
        if (!UnityAssetAccess::hasActiveClient())
          return;
      }
      wxAuiPaneInfo & browse = interfaceManager.GetPane(fileControl);
      if (!browse.IsShown())
      {
        browse.Show(true);
        interfaceManager.Update();
      }
      // The keyboard to Browse: its tree when a row is picked there (the model loaded, the texture shown),
      // to go on from it; otherwise its search box.
      if (fileControl->fileTree->GetSelection().IsOk())
        fileControl->fileTree->SetFocus();
      else if (fileControl->txtContent)
        fileControl->txtContent->SetFocus();
      break;
    }
    case ID_UI_SCREENSHOT:
      SaveUnityScreenshot();
      break;
  }
}

wxString ModelViewer::DefaultScreenshotName() const
{
  wxString path;
  const WoWModel * rider = riderModel();
  const WoWModel * m = rider ? rider : (canvas ? canvas->model() : nullptr);
  // Named after what the viewer mode shows (Screenshot captures only that: unityViewportNotice).
  if (isBuildingsMode() && canvasHasWorldModel())
    path = wxString(canvas->wmo->itemName().toStdWString());
  else if (!isBuildingsMode() && canvasHasModel() && m)
    path = m->gamefile ? wxString(m->gamefile->fullname().toStdWString())
                       : wxString(const_cast<WoWModel *>(m)->name().toStdWString());
  path.Replace(wxT("/"), wxT("\\"));
  wxString name = path.AfterLast('\\');
  const int dot = name.Find('.', true);
  if (dot > 0)
    name = name.Left(dot);
  // Nothing a Windows file name cannot hold: the reserved characters and the control characters.
  wxString safe;
  for (wxUniChar c : name)
    safe += (c.GetValue() < 32 || wxString(wxT("<>:\"/\\|?*")).Find(c) != wxNOT_FOUND) ? wxUniChar('_') : c;
  safe.Trim(true).Trim(false);
  while (!safe.IsEmpty() && safe.Last() == '.')
    safe.RemoveLast();
  if (safe.IsEmpty())
    safe = wxT("screenshot");
  return safe + wxT("_") + wxDateTime::Now().Format(wxT("%Y-%m-%d_%H%M%S")) + wxT(".png");
}

void ModelViewer::SaveUnityScreenshot()
{
  if (m_screenshotRequest != 0)
    return;
  wxFileDialog dialog(this, _("Save screenshot"), wxEmptyString, DefaultScreenshotName(), _("PNG Image (*.png)|*.png"),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dialog.ShowModal() != wxID_OK)
    return;
  wxString why;
  RequestUnityScreenshot(dialog.GetPath(), why);
}

int ModelViewer::RequestUnityScreenshot(const wxString & requested, wxString & why)
{
  why.clear();
  wxFileName file(requested);
  if (!file.GetExt().IsSameAs(wxT("png"), false))
    file.SetFullName(file.GetFullName() + wxT(".png"));
  if (m_screenshotRequest != 0)
    why = _("a screenshot is already being saved");
  else if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isUnityReady())
    why = _("the Unity viewport is not running");
  else if (!unityRendererHost->ipc()->playerTakesScreenshots())
    why = _("the Unity viewport player is too old to take screenshots; rebuild it");
  else if (!isUnityViewportShowingModel())
    why = _("nothing is shown in the viewport");
  else if (!file.IsAbsolute() || file.GetName().IsEmpty())
    why = _("not a full file name: ") + requested;
  else if (!file.DirExists())
    why = _("the folder does not exist: ") + file.GetPath();
  if (!why.IsEmpty())
  {
    LOG_ERROR << "Screenshot not taken:" << QString::fromWCharArray(why.wc_str());
    if (GetStatusBar())
      SetStatusText(_("Screenshot failed: ") + why, 0);
    return 0;
  }
  const wxString path = file.GetFullPath();
  const int request = unityRendererHost->ipc()->requestScreenshot(QString::fromWCharArray(path.wc_str()), SCREENSHOT_WIDTH,
                                                                  SCREENSHOT_HEIGHT);
  if (request == 0)
  {
    why = _("the Unity viewport could not be asked");
    if (GetStatusBar())
      SetStatusText(_("Screenshot failed: ") + why, 0);
    return 0;
  }
  m_screenshotRequest = request;
  m_screenshotPath = path;
  m_screenshotSentAt = timeGetTime();
  if (GetStatusBar())
    SetStatusText(_("Saving screenshot..."), 0);
  return request;
}

void ModelViewer::OnUnityScreenshotSaved(const UnityIpcServer::ScreenshotResult & result)
{
  if (result.request != m_screenshotRequest || m_screenshotRequest == 0)
    return;   // an answer nobody is waiting for any more (logged by the IPC server)
  const wxString name = wxFileName(m_screenshotPath).GetFullName();
  m_screenshotRequest = 0;
  if (!GetStatusBar())
    return;
  if (result.ok)
    SetStatusText(_("Screenshot saved: ") + name, 0);
  else
    SetStatusText(_("Screenshot failed: ") + wxString(result.error.toStdWString()), 0);
}

void ModelViewer::OnUpdateCommandUI(wxUpdateUIEvent & event)
{
  // A command that acts on a model: available in the Models viewer only; the Model panel in Models and
  // Buildings. None of these has a condition of its own to keep (Export Model checks for a model when
  // chosen), so this is their whole state; a command given one later combines it with this.
  if (needsModelViewer(event.GetId()))
    event.Enable(isModelsMode() && capabilityRefusal(event.GetId()).isEmpty());
  else if (needsUnityViewport(event.GetId()))
    event.Enable(!isTextureMode());
  else if (isCapabilityGated(event.GetId()))
    event.Enable(capabilityRefusal(event.GetId()).isEmpty()); // both ways: a refusal ends (a load finishes)
  switch (event.GetId())
  {
    case ID_SHOW_FILE_LIST:
      event.Check(fileControl && interfaceManager.GetPane(fileControl).IsShown());
      break;
    case ID_SHOW_CHAR:
      event.Check(modelInspector && interfaceManager.GetPane(modelInspector).IsShown());
      break;
    case ID_SHOW_ANIM:
      event.Check(animControl && interfaceManager.GetPane(animControl).IsShown());
      break;
    case ID_UI_SCREENSHOT:
      // Something to capture, and no capture on its way (an older player is told why on the click).
      event.Enable(isUnityViewportShowingModel() && m_screenshotRequest == 0);
      break;
    // The viewer selector: all answered every time, so a switch made elsewhere (a menu load, a world model
    // loaded) or refused (during a client load) shows right.
    case ID_UI_MODELS:
      event.Check(m_viewerMode == ViewerMode::Models);
      break;
    case ID_UI_TEXTURES:
      event.Check(m_viewerMode == ViewerMode::Textures);
      break;
    case ID_UI_BUILDINGS:
      event.Check(m_viewerMode == ViewerMode::Buildings);
      break;
    case ID_VIEW_APPEARANCE_SYSTEM:
      event.Check(UiStyle::themePreference() == UiStyle::Theme::System);
      break;
    case ID_VIEW_APPEARANCE_LIGHT:
      event.Check(UiStyle::themePreference() == UiStyle::Theme::Light);
      break;
    case ID_VIEW_APPEARANCE_DARK:
      event.Check(UiStyle::themePreference() == UiStyle::Theme::Dark);
      break;
  }
}

bool ModelViewer::needsModelViewer(int id) const
{
  switch (id)
  {
    case ID_SHOW_ANIM:          // the panels the other viewers put away (not the Model panel: needsUnityViewport)
    case ID_SHOW_MODEL:
    case ID_VIEW_NPC:           // the loads, which would switch back to Models
    case ID_VIEW_ITEM:
    case ID_LOAD_CHAR:
    case ID_IMPORT_CHAR:
    case ID_IMPORT_NPC:
    case ID_EXPORT_MODEL:       // exporting the model: the submenu, ModelInfo.xml and each exporter
    case ID_FILE_MODEL_INFO:
    case ID_VIEW_BACKGROUND_COLOR:   // the Models viewport's background, not on show in the other viewers
      return true;
  }
  return id >= m_exportMenuFirst && id < m_exportMenuEnd;
}

bool ModelViewer::isCapabilityGated(int id) const
{
  switch (id)
  {
    case ID_LOAD_WOW:
    case ID_LOAD_MPQ:
    case ID_LOAD_CHAR:
    case ID_IMPORT_CHAR:
    case ID_VIEW_NPC:
    case ID_VIEW_ITEM:
    case ID_UI_TEXTURES:
    case ID_UI_BUILDINGS:
      return true;
    default:
      return false;
  }
}

QString ModelViewer::capabilityRefusal(int id) const
{
  if (m_clientLoading && (id == ID_LOAD_WOW || id == ID_LOAD_MPQ))
    return QString("A World of Warcraft client is being opened.");
  const ClientCapabilities & caps = m_clientCaps;
  if (!caps.loaded)
    return QString();
  const QString client = loadedClientSummary();
  switch (id)
  {
    case ID_LOAD_CHAR:
      return caps.characters ? QString() : QString("%1 has no characters this viewer can load.").arg(client);
    case ID_IMPORT_CHAR:
      return caps.armory ? QString()
                         : QString("The Armory importer loads Retail characters; %1 is loaded.").arg(client);
    case ID_VIEW_NPC:
      return caps.npcDisplayInfo ? QString() : QString("%1 has no NPC data this viewer can read.").arg(client);
    case ID_VIEW_ITEM:
      return caps.items ? QString() : QString("%1 has no item data this viewer can read.").arg(client);
    case ID_UI_TEXTURES:
      return caps.textures ? QString() : QString("%1 has no textures on this computer.").arg(client);
    case ID_UI_BUILDINGS:
      return caps.buildings ? QString() : QString("%1 has no world models on this computer.").arg(client);
  }
  return QString();
}

bool ModelViewer::needsUnityViewport(int id) const
{
  // The Model panel: a model's appearance and information, or a world model's.
  return id == ID_SHOW_CHAR;
}

// The one place a command of another viewer is refused, however it arrives: a menu, a shortcut, the
// command bar or an event posted to the frame. Its menu item and tool are greyed there as well, and the
// menus are answered at every switch, so a shortcut is normally dropped by wx before it gets here.
// Nothing switches back to Models.
bool ModelViewer::TryBefore(wxEvent & event)
{
  if (event.GetEventType() == wxEVT_MENU)
  {
    const QString refusal = capabilityRefusal(event.GetId());
    if (!refusal.isEmpty())
    {
      LOG_INFO << "[clientcaps] command refused, id" << event.GetId() << "-" << refusal;
      SetStatusText(wxString(refusal.toStdWString()), 0);
      return true;
    }
  }
  if (event.GetEventType() == wxEVT_MENU &&
      ((!isModelsMode() && needsModelViewer(event.GetId())) || (isTextureMode() && needsUnityViewport(event.GetId()))))
  {
    LOG_INFO << "[viewport] a command refused in the"
             << (isTextureMode() ? "Textures" : isBuildingsMode() ? "Buildings" : "Models") << "viewer, id" << event.GetId();
    return true;
  }
  return wxFrame::TryBefore(event);
}

bool ModelViewer::canvasHasWorldModel() const
{
  return canvas && isWMO && canvas->wmo;
}

bool ModelViewer::canvasHasModel() const
{
  return canvas && !isWMO && canvas->model();
}

bool ModelViewer::canvasHoldsModesContent() const
{
  if (m_viewerMode == ViewerMode::Buildings)
    return canvasHasWorldModel();
  if (m_viewerMode == ViewerMode::Models)
    return canvasHasModel();
  return false;
}

void ModelViewer::UpdateTitle()
{
  if (isTextureMode())
    return;   // the Textures viewer leaves the title as it was
  wxString name;
  if (isBuildingsMode() && canvasHasWorldModel())
    name = wxString(canvas->wmo->itemName().toStdWString());
  else if (isModelsMode() && canvasHasModel())
    name = wxString(const_cast<WoWModel *>(canvas->model())->name().toStdWString());
  const wxString title = name.IsEmpty() ? wxString(GLOBALSETTINGS.appTitle())
                                        : wxString(GLOBALSETTINGS.appTitle()) + wxT("  -  ") + name;
  if (GetTitle() != title)
    SetTitle(title);
}

// THE UNCOVER FENCE begun (two questions in turn, the poll timer for its retries and limit); false when the player
// cannot be asked, and there is nothing to wait for.
bool ModelViewer::startUncoverFence()
{
  const int question = unityRendererHost && unityRendererHost->ipc() ? unityRendererHost->ipc()->requestRuntimeState() : 0;
  if (question == 0)
    return false;
  m_uncoverFence = question;
  m_uncoverFenceRounds = 2;
  m_uncoverFenceClock.Start();
  m_uncoverFenceAsked.Start();
  m_playerContentPoll.Start(POLL_MS);   // the retries and the limit
  return true;
}

// The fence answered (or given up): the waits it ended, and the viewport uncovered.
void ModelViewer::endUncoverFence()
{
  m_uncoverFence = 0;
  m_uncoverFenceRounds = 0;
  m_mapObjectAwaited = 0;
  m_modelAwaited = 0;
  m_playerContentPoll.Stop();
  UpdateUnityViewportState();
}

// A model built after a world model: the player is asked what it shows until it has the model, or has finished
// without it, or MODEL_AWAIT_TIMEOUT_MS have gone by (PlayerContent). The uncover fence's retries and limit too.
void ModelViewer::OnPlayerContentPoll(wxTimerEvent & WXUNUSED(event))
{
  // THE UNCOVER FENCE pending: asked again when unanswered for a while, given up after the limit.
  if (m_uncoverFence != 0)
  {
    // A player gone meanwhile: the wait stays (the health check's notice, or the next player's onUnityReady, ends it).
    if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isUnityReady())
    {
      m_playerContentPoll.Stop();
      return;
    }
    if (m_uncoverFenceClock.Time() > MODEL_AWAIT_TIMEOUT_MS)
    {
      LOG_ERROR << "[viewport] the player did not answer the uncover fence; showing the viewport";
      endUncoverFence();
      return;
    }
    if (m_uncoverFenceAsked.Time() > FENCE_RETRY_MS)
    {
      const int question = unityRendererHost->ipc()->requestRuntimeState();
      if (question != 0)
      {
        m_uncoverFence = question;
        m_uncoverFenceAsked.Start();
      }
    }
    return;
  }
  if (m_modelAwaited == 0 || !unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isUnityReady())
  {
    m_playerContentPoll.Stop();
    return;
  }
  if (m_modelAwaitedClock.Time() > MODEL_AWAIT_TIMEOUT_MS)
  {
    LOG_ERROR << "[viewport] the player did not report model" << m_modelAwaited << "in"
              << (int)(MODEL_AWAIT_TIMEOUT_MS / 1000) << "s; showing the viewport";
    m_modelAwaited = 0;
    m_uncoverFence = 0;
    m_playerContent = PlayerContent::Unknown;
    m_playerContentPoll.Stop();
    UpdateUnityViewportState();
    return;
  }
  m_playerContentQuery = unityRendererHost->ipc()->requestRuntimeState();
}

void ModelViewer::OnPlayerRuntimeState(const UnityIpcServer::RuntimeState & state)
{
  // THE UNCOVER FENCE answered: the next question, or -- the second answered -- the frame that draws the new content
  // has been presented.
  if (m_uncoverFence != 0 && state.query == m_uncoverFence)
  {
    if (--m_uncoverFenceRounds > 0)
    {
      const int question = unityRendererHost && unityRendererHost->ipc() ? unityRendererHost->ipc()->requestRuntimeState() : 0;
      if (question != 0)
      {
        m_uncoverFence = question;
        m_uncoverFenceAsked.Start();
        return;
      }
    }
    endUncoverFence();
    return;
  }
  if (m_modelAwaited == 0 || m_uncoverFence != 0 || state.query == 0 || state.query != m_playerContentQuery || state.loading)
    return;
  if (state.modelFileDataID == m_modelAwaited)
  {
    m_playerContent = PlayerContent::Model;
    m_playerContentPoll.Stop();
    // Shown once the player has answered the fence (THE UNCOVER FENCE), in later frames.
    if (startUncoverFence())
      return;
  }
  else
  {
    m_playerContent = state.mapObjectFileDataID > 0 ? PlayerContent::MapObject
                    : state.modelFileDataID > 0     ? PlayerContent::Model
                                                    : PlayerContent::Nothing;
    // Finished without it: the player still shows what it had (the world model), which must not pass for it.
    LOG_ERROR << "[viewport] the player finished without model" << m_modelAwaited << "(it shows"
              << state.modelFileDataID << "/ world model" << state.mapObjectFileDataID << ")";
    m_modelAwaitedFailed = m_modelAwaited;
  }
  m_modelAwaited = 0;
  m_playerContentPoll.Stop();
  UpdateUnityViewportState();
}

// THE THEME. The palette in use (UiStyle) follows the Appearance preference, Windows' app mode (for
// System) and high contrast (which overrides both). Light or dark is fixed for a run -- wxWidgets' dark
// mode, which darkens everything Windows draws, is decided before the first window -- so a change that
// asks for the other one is offered as a restart (OfferThemeRestart); a light run follows high contrast
// at once (ApplyTheme: every window's colours, the pane chrome, the toolbars, then one repaint). The
// viewport is not touched: it draws the same in every theme.
void ModelViewer::OnAppearance(wxCommandEvent & event)
{
  const UiStyle::Theme theme = event.GetId() == ID_VIEW_APPEARANCE_LIGHT  ? UiStyle::Theme::Light
                             : event.GetId() == ID_VIEW_APPEARANCE_DARK   ? UiStyle::Theme::Dark
                                                                          : UiStyle::Theme::System;
  if (theme == UiStyle::themePreference())
    return;
  UiStyle::setThemePreference(theme);
  // Kept at once (and again at exit), so a restart straight after reads it.
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
  config.setValue("Settings/Appearance", UiStyle::themeToSetting(theme));
  config.sync();
  ApplyTheme();
  if (UiStyle::restartWanted())
    OfferThemeRestart(true);
  else
    m_themeRestartOffered = -1;   // back to this run's: a later change is offered again
}

// Light and dark are fixed for a run (wxWidgets' dark mode is decided before the first window): a
// change that asks for the other one is offered as a restart. Asked about every time the user chooses
// an appearance; for Windows' own changes (System following the app mode, high contrast in a dark run)
// once per change, not at every message of the burst Windows sends.
void ModelViewer::OfferThemeRestart(bool chosen)
{
  if (batchMode)
    return;   // a headless run (an export's child process, a command-line load) never asks
  if (!canCloseNow())
  {
    // Asked again once the dialog or the load in progress is done (OnThemeRecheck), as the user's own
    // choice if it was one.
    m_themeOfferChosen = m_themeOfferChosen || chosen;
    m_themeRecheck.StartOnce(1000);
    return;
  }
  chosen = chosen || m_themeOfferChosen;
  m_themeOfferChosen = false;
  const bool wantDark = UiStyle::darkWanted();
  if (!chosen && m_themeRestartOffered == (wantDark ? 1 : 0))
    return;
  m_themeRestartOffered = wantDark ? 1 : 0;
  const wxString what = wantDark ? _("the dark appearance")
                      : UiStyle::highContrastOn() ? _("Windows' high-contrast colours") : _("the light appearance");
  if (exportRunning())
  {
    wxMessageBox(wxString::Format(_("The viewer changes to %s when it next starts (an export is still running)."), what),
                 _("Appearance"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  // Yes by default only when the user just chose it; a change of Windows' never restarts on Enter.
  if (wxMessageBox(wxString::Format(_("The viewer changes to %s when it restarts. The model or building on screen is closed; "
                                      "your settings are kept.\n\nRestart now?"), what),
                   _("Appearance"), wxYES_NO | (chosen ? wxYES_DEFAULT : wxNO_DEFAULT) | wxICON_QUESTION, this) == wxYES)
    Relaunch();
  else
    SetStatusText(wxString::Format(_("The viewer changes to %s at its next start (or File > Restart)."), what), 0);
}

void ModelViewer::ApplyTheme()
{
  const UiStyle::Palette previous = UiStyle::palette();
  if (!UiStyle::refreshPalette())
    return;
  Freeze();
  UiStyle::retheme(previous);
  if (UiDockArt * art = dynamic_cast<UiDockArt *>(interfaceManager.GetArtProvider()))
    art->applyPalette();
  // The toolbars (the command bar, the texture view's) keep their background in a bitmap, which wx
  // only makes again for a resize or Windows' own colour change: the same event makes it again now.
  std::function<void(wxWindow *)> toolbars = [&](wxWindow * w) {
    if (wxAuiToolBar * bar = dynamic_cast<wxAuiToolBar *>(w))
    {
      wxSysColourChangedEvent changed;
      changed.SetEventObject(bar);
      bar->GetEventHandler()->ProcessEvent(changed);
    }
    for (wxWindow * child : w->GetChildren())
      if (!child->IsTopLevel())
        toolbars(child);
  };
  toolbars(this);
  // Colours the shell does not take from a role: the item names' quality colours.
  if (isChar && charControl && charControl->model)
    charControl->RefreshEquipment();
  Thaw();
  ::RedrawWindow((HWND)GetHWND(), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void ModelViewer::OnThemeChangedOutside()
{
  // Windows' app mode or high contrast changed: a light run follows high contrast at once (ApplyTheme);
  // what would need the other of light and dark is offered as a restart, once.
  ApplyTheme();
  if (UiStyle::restartWanted())
    OfferThemeRestart(false);
  else
    m_themeRestartOffered = -1;   // back to this run's: a later change is offered again
}

void ModelViewer::CreateThemedStatusBar()
{
  const bool generic = UiStyle::darkActive();
  const int fields = 5;
  // wx shows a cut-off field's full text itself only on its native bar (and refuses a tooltip set by
  // hand while it would): UiEquipGenericStatusBar does it for its own.
  UseNativeStatusBar(!generic);
  CreateStatusBar(fields, generic ? (wxSTB_DEFAULT_STYLE & ~wxSTB_SHOW_TIPS) : wxSTB_DEFAULT_STYLE);
  UseNativeStatusBar(true);
  wxStatusBar * bar = GetStatusBar();
  int widths[fields] = { -1, 100, 50, 125, 125 };
  SetStatusWidths(fields, widths);
  // Flat fields: text, not a row of sunken boxes.
  int styles[fields] = { wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT };
  bar->SetStatusStyles(fields, styles);
  if (generic)
  {
    UiStyle::setRole(bar, UiStyle::Role::Panel);
    UiEquipGenericStatusBar(bar);
  }
}

void ModelViewer::OnSysColourChanged(wxSysColourChangedEvent & event)
{
  // Windows' colours changed (high contrast on or off): once wx has passed the change on, the palette is
  // resolved again.
  event.Skip();
  CallAfter([this] { OnThemeChangedOutside(); });
}

void ModelViewer::OnThemeRecheck(wxTimerEvent & WXUNUSED(event))
{
  OnThemeChangedOutside();
}

WXLRESULT ModelViewer::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
  // Windows' app mode or high contrast changed. wxWidgets passes the app mode's message on as a
  // wxEVT_SYS_COLOUR_CHANGED (OnSysColourChanged); checked once more a moment later for both: Windows sends
  // these in bursts, and the values they announce are not always readable at the first one.
  if (WinTheme::isColourSettingChange(message, wParam, lParam))
    m_themeRecheck.StartOnce(500);
  WXLRESULT result = 0;
  if (m_menuBarTitles && m_menuBarTitles->handle(message, wParam, lParam, &result))
    return result;
  return wxFrame::MSWWindowProc(message, wParam, lParam);
}

void ModelViewer::OnKeyboardShortcuts(wxCommandEvent & WXUNUSED(event))
{
  std::vector<ShortcutInfo> extra;
  auto add = [&extra](const wxString & section, const wxString & keys, const wxString & action) {
    ShortcutInfo info;
    info.section = section;
    info.keys = keys;
    info.action = action;
    extra.push_back(info);
  };

  for (const AppAccelerator & a : kAppAccelerators)
    if (a.keys && a.description)
      add(a.where, a.keys, a.description);

  add(_("Window"), _("Esc"), _("Leave fullscreen"));

  const wxString unity = _("Unity viewport");
  add(unity, _("Left drag"), _("Orbit around the model or building"));
  add(unity, _("Right drag"), _("Pan"));
  add(unity, _("Mouse wheel"), _("Zoom"));

  const wxString textures = _("Textures (the Textures viewer)");
  add(textures, _("Ctrl+S"), _("Export the texture shown as PNG"));
  add(textures, _("Ctrl+Shift+S"), _("Export the original BLP file"));
  add(textures, _("A / Shift+A"), _("Alpha on and off / alpha only (in the texture)"));
  // (The OpenGL viewport's section -- its mouse camera, numpad camera keys and 0-9 speed keys -- is gone
  // with that viewport.)

  KeyboardShortcutsDialog dialog(this, GetMenuBar(), extra);
  dialog.ShowModal();
}

void ModelViewer::ModelInfo()
{
  if (!canvas->model())
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  wxString fn = wxT("ModelInfo.xml");
  // FIXME: ofstream is not compatible with multibyte path name
  std::ofstream xml(fn.fn_str(), ios_base::out | ios_base::trunc);

  if (!xml.is_open()) {
    LOG_ERROR << "Unable to open file '" << QString::fromWCharArray(fn.c_str()) << "'. Could not export model.";
    return;
  }

  xml << *m;
 
  xml.close();
}


// Other things to export...
void ModelViewer::OnExportOther(wxCommandEvent &event)
{
  int id = event.GetId();
  if (id == ID_FILE_MODEL_INFO) {
    ModelInfo();
  }
}

void ModelViewer::UpdateControls()
{
  if (!canvas || !canvas->model() || !canvas->root)
    return;

  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  // A character riding a mount is refreshed as the character it is. The canvas model is then the mount, whose
  // item list is empty, while the character controls act on the rider (CharControl::model): an equipment slot
  // pick or an item level change loads the item into the rider and relies on this refresh to put it on, which
  // refreshing the mount's items never did -- the change only appeared at the rider's next refresh.
  if (m->modelType == MT_CHAR || (riderModel() && riderMount() == m))
    charControl->RefreshModel();
  else
  {
    //refresh equipment
    for (WoWModel::iterator it = m->begin();
         it != m->end();
         ++it)
         (*it)->refresh();
  }
  modelControl->RefreshModel(canvas->root);
}

namespace
{
  // The character model for an imported race and sex, or null when this build has none.
  GameFile * armoryRaceModel(const CharInfos & info, int & sex)
  {
    sex = (info.gender == "Male") ? 0 : 1;
    const int raceModelFileID = RaceInfos::getFileIDForRaceSex(info.raceId, sex);
    return (raceModelFileID > 0) ? GAMEDIRECTORY.getFile(raceModelFileID) : nullptr;
  }

  // The Armory importer among the loaded plugins: the first one that takes a character link.
  ImporterPlugin * armoryImporter(const QString & url)
  {
    for (PluginManager::iterator it = PLUGINMANAGER.begin(); it != PLUGINMANAGER.end(); ++it)
    {
      auto * plugin = dynamic_cast<ImporterPlugin *>(*it);
      if (plugin && plugin->acceptURL(url))
        return plugin;
    }
    return nullptr;
  }
}

CharInfos * ModelViewer::FetchArmoryCharacter(const wxString & strURL, wxString & error, QVariantMap * summary)
{
  error.clear();
  if (summary)
    summary->clear();

  const QString url = QString::fromUtf8(strURL.utf8_str());
  LOG_INFO << "Importing character from the Armory:" << url;

  // Only the first plugin that takes the link is asked: a second one would overwrite (and
  // leak) the first one's answer.
  ImporterPlugin * importer = armoryImporter(url);
  CharInfos * result = importer ? importer->importChar(url) : nullptr;
  if (!result)
  {
    LOG_ERROR << "Armory import: no importer took the link.";
    error = _("That is not an Armory character link.\n\n"
              "Paste the address of a character's Armory page: it has /character/ in it.");
    return nullptr;
  }

  if (!result->valid)
  {
    error = result->errorMessage.empty()
      ? wxString(_("Could not read the character link.\n\nPaste the address of a character's Armory page."))
      : wxString::FromUTF8(result->errorMessage.c_str());
    delete result;
    return nullptr;
  }

  // The race has to resolve to a character model before anything is dressed. For a race
  // this build has no model for (getFileIDForRaceSex returns -1), LoadModel() would be handed
  // nothing and the character's customizations and equipment would land on whatever model
  // happened to be on screen. Checked here, before anything on screen changes.
  int sex = 0;
  if (!armoryRaceModel(*result, sex))
  {
    LOG_ERROR << "Armory import: no character model for race" << result->raceId << "sex" << sex
              << "- nothing was imported.";
    error = wxString::Format(_("This build has no character model for race %d (%s), so the character could not be imported.\n\n"
                               "Nothing on screen was changed."),
                             result->raceId, (sex == 0) ? _("male") : _("female"));
    delete result;
    return nullptr;
  }

  // What the importer read beyond CharInfos, through Qt's meta-object system so neither the
  // plugin interface nor CharInfos changes shape. An importer without it leaves this empty.
  if (summary && !QMetaObject::invokeMethod(importer, "lastCharacter", Qt::DirectConnection,
                                            Q_RETURN_ARG(QVariantMap, *summary)))
    summary->clear();

  return result;
}

QVariantMap ModelViewer::ArmoryRealmList(const QString & region)
{
  QVariantMap answer;
  answer["ok"] = false;
  ImporterPlugin * importer = armoryImporter("https://worldofwarcraft.blizzard.com/");
  if (!importer || !QMetaObject::invokeMethod(importer, "realmList", Qt::DirectConnection,
                                              Q_RETURN_ARG(QVariantMap, answer), Q_ARG(QString, region)))
  {
    answer.clear();
    answer["ok"] = false;
    answer["unsupported"] = true;
    answer["message"] = QString("the Armory importer has no realm list");
  }
  return answer;
}

bool ModelViewer::ImportArmoury(wxString strURL)
{
  wxString error;
  std::unique_ptr<CharInfos> info(FetchArmoryCharacter(strURL, error));
  if (info && ApplyArmoryCharacter(*info, error))
    return true;

  LOG_ERROR << "Armory import failed:" << QString::fromWCharArray(error.wc_str());
  return false;
}

bool ModelViewer::ApplyArmoryCharacter(CharInfos & info, wxString & error)
{
  SetViewerMode(ViewerMode::Models);
  CharInfos * result = &info;
  error.clear();

  // Described to the Unity viewport once, dressed, when this returns: see SceneHold. Held for
  // the dressing only -- the network wait is over by now.
  SceneHold sceneHold(this);

  int sex = 0;
  GameFile * raceModel = armoryRaceModel(info, sex);
  if (!raceModel)
  {
    error = _("This build has no character model for the character's race, so nothing was imported.");
    return false;
  }

  {
    // Load the model as this character's race: a race that shares its model file with another
    // one (Mag'har Orc on the Orc model) is otherwise read as that other race, whose
    // customization options are not the imported character's -- every choice below would be
    // skipped, leaving a default character in the right gear.
    LoadModel(raceModel, result->raceId, sex);

    if (!g_canvas->model() || !g_charControl->model)
    {
      LOG_ERROR << "Armory import: the character model" << raceModel->fullname()
                << "did not load - nothing was imported.";
      error = _("The character's model could not be loaded, so nothing was imported.");
      return false;
    }

    if (g_charControl->model->infos.raceID != result->raceId)
      LOG_INFO << "Armory import: the model is race" << g_charControl->model->infos.raceID
               << "and the character is race" << result->raceId
               << "- customizations that belong to the character's own race will be skipped.";

    if (result->hasTransmogGear == true)
      LOG_INFO << "Transmogrified Gear was found. Switching items...";

    // The appearance API returns EVERY customization on the account's character record,
    // which today includes the character's dragonriding-drake mounts (Worn Wylderdrake,
    // Renewed Proto-Drake, Cliffside Wylderdrake, ...). Those options belong to the drake
    // ChrModels, not the humanoid -- and a drake "Skin Color" resolves to a companion-drake/
    // serpent/proto-dragon scale texture that targets the universal base-skin layer, so
    // applying it paints the drake's (often dark) scales over the character's body. Keep only
    // the options that belong to THIS model so foreign customizations can't leak in.
    std::vector<std::pair<unsigned int, unsigned int> > ownCustomizations;
    for (const auto& customization : result->customizations)
      if (g_charControl->model->cd.hasOption(customization.first))
        ownCustomizations.push_back(customization);
    LOG_INFO << "Armory import: applying" << (int)ownCustomizations.size() << "of"
             << (int)result->customizations.size() << "customizations ("
             << (int)(result->customizations.size() - ownCustomizations.size())
             << "belong to other models, e.g. dragonriding drakes -- skipped).";

    // Apply the imported customizations. Skin/hair COLOUR options are parent/child-linked
    // (Face->SkinColor, HairStyle->HairColor) and their textures are related-gated, so resolving
    // a colour depends on its linked partner's current value. The appearance API returns the
    // choices in an arbitrary order, and a single in-order pass can leave a stale, default-
    // resolved colour texture in the parent option's bucket (geometry survives because it comes
    // from direct, non-related elements -- which is exactly why geometry was right but colour
    // wrong). Apply all choices, then re-resolve them in a second pass so every colour is
    // resolved against the final value of its partner (same idea as reset()/setDemonHunterMode).
    for (const auto& customization : ownCustomizations)
      g_charControl->model->cd.set(customization.first, customization.second);
    for (const auto& customization : ownCustomizations)
      g_charControl->model->cd.set(customization.first, customization.second);

    g_charControl->model->cd.eyeGlowType = static_cast<EyeGlowTypes>(result->eyeGlowType);

    if (result->customTabard)
    {
      g_charControl->model->td.showCustom = true;
      g_charControl->model->td.setIconId(result->tabardIcon);
      g_charControl->model->td.setIconColor(result->iconColor);
      g_charControl->model->td.setBorderId(result->tabardBorder);
      g_charControl->model->td.setBorderColor(result->borderColor);
      g_charControl->model->td.setBackgroundId(result->background);
      g_charControl->model->td.setTabardId(result->equipment[CS_TABARD]);
    }

    for (unsigned int i = 0; i < NUM_CHAR_SLOTS; i++)
    {
      WoWItem * item = g_charControl->model->getItem((CharSlots)i);
      if (item)
      {
        item->setId(result->equipment[i]);
        item->setModifierId(result->itemModifierIds[i]);
      }
    }

    g_charControl->RefreshModel();
    g_charControl->RefreshEquipment();
    // Rebuild the attachment list (View > Attachments) so the imported helm/shoulders/weapon models are
    // selectable immediately (previously the list stayed empty until an item was re-equipped).
    if (canvas && canvas->root)
      modelControl->RefreshModel(canvas->root);
  }
  return true;
}

namespace
{
  wxString generationName(CharacterModelVariant v)
  {
    return v == CharacterModelVariant::HD ? _("High Definition") : _("Classic");
  }

  wxString characterName(const RaceInfos & infos)
  {
    return wxString::Format(infos.sexID == GENDER_FEMALE ? _("%s female") : _("%s male"), wxString(infos.nameLang.c_str(), wxConvUTF8));
  }

  QString optionNames(const std::vector<unsigned int> & options)
  {
    QStringList names;
    for (const unsigned int o : options)
    {
      sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = %1").arg(o));
      names << ((r.valid && !r.values.empty() && !r.values[0][0].isEmpty()) ? r.values[0][0] : QString::number(o));
    }
    return names.join(", ");
  }
}

ModelViewer::CharacterVariantState ModelViewer::characterVariantState() const
{
  CharacterVariantState state;
  if (RaceInfos::variantPairCount() == 0)
    return state; // this client ships one generation of each character model
  WoWModel * m = riderModel();
  if (!m || !m->charModelDetails.isChar || m->modelType != MT_CHAR || m->infos.raceID == -1 || m->infos.ChrModelID.empty())
    return state;
  if (m->cd.isNPC || m_shownAsCreatureDisplay)
    return state; // an NPC is shown as the display it is
  state.shown = true;
  state.current = m->modelGeneration();
  state.other = state.current == CharacterModelVariant::HD ? CharacterModelVariant::Classic : CharacterModelVariant::HD;
  if (m_variantSession.pairID != 0 && m_variantSession.raceID == m->infos.raceID && m_variantSession.sexID == m->infos.sexID)
    state.note = m_variantSession.note;

  RaceInfos::VariantPair pair;
  if (!RaceInfos::getVariantPair(m->infos.ChrModelID[0], pair))
  {
    state.reason = wxString::Format(_("The %s has only a %s model in this client."), characterName(m->infos), generationName(state.current));
    return state;
  }
  const int fileID = m->gamefile ? (int)m->gamefile->fileDataId() : 0;
  if (fileID != pair.primaryFileID && fileID != pair.alternateFileID)
  {
    state.reason = _("This file is not one of this client's character models.");
    return state;
  }
  RaceInfos partner;
  GameFile * partnerFile = RaceInfos::getVariantPartner(m->infos, partner) ? GAMEDIRECTORY.getFile(partner.modelFileID) : nullptr;
  wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  if (!partnerFile)
    state.reason = wxString::Format(_("The %s model of the %s is not in this client."), generationName(state.other), characterName(m->infos));
  else if (folder && folder->isRemoteFileId(partner.modelFileID))
    state.reason = wxString::Format(_("The %s model of the %s is not on this computer yet."), generationName(state.other), characterName(m->infos));
  else if (riderMount())
    state.reason = _("Dismount to change the model.");
  else if (unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady() &&
           !unityRendererHost->ipc()->playerKeepsView())
    state.reason = _("The 3D viewport cannot keep its view while the model changes; update it to switch.");
  else
    state.enabled = true;
  return state;
}

void ModelViewer::HintVariantPartner()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->playerCachesAssets())
    return;
  // What the Model selector offers: a player character (no NPC), not mounted, whose other model is in the client and
  // on this computer.
  const CharacterVariantState state = characterVariantState();
  WoWModel * m = riderModel();
  RaceInfos partner;
  if (!state.shown || !state.enabled || !m || !RaceInfos::getVariantPartner(m->infos, partner) || partner.modelFileID <= 0)
    return;
  if (unityRendererHost->ipc()->sendPrefetchAssets({ partner.modelFileID }))
    m_assetHintLoad = m_unityLoadSerial;
}

void ModelViewer::SyncCharacterMenuChecks(const WoWModel * m)
{
  if (!m || !charMenu)
    return;
  charMenu->Check(ID_SHOW_UNDERWEAR, m->cd.showUnderwear);
  charMenu->Check(ID_SHOW_EARS, m->cd.showEars);
  charMenu->Check(ID_SHOW_HAIR, m->cd.showHair);
  charMenu->Check(ID_SHOW_FACIALHAIR, m->cd.showFacialHair);
  charMenu->Check(ID_SHOW_FEET, m->cd.showFeet);
  charMenu->Check(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, m->cd.autoHideGeosetsForHeadItems);
  charMenu->Check(ID_SHEATHE, m->bSheathe);
}

bool ModelViewer::SwitchCharacterVariant(CharacterModelVariant target, wxString & why)
{
  why.clear();
  const CharacterVariantState state = characterVariantState();
  if (!state.shown)
  {
    why = _("This character has no other model to switch to.");
    return false;
  }
  if (target == state.current)
    return true; // and never through LoadModel: its same-file path starts the character afresh
  if (!state.enabled || target != state.other)
  {
    why = state.reason.IsEmpty() ? _("That model is not available for this character.") : state.reason;
    return false;
  }
  if (UnityAssetAccess::isClientLoading())
  {
    why = _("A client is loading; switch the model once it has loaded.");
    return false;
  }

  WoWModel * m = riderModel();
  RaceInfos partner;
  RaceInfos::VariantPair pair;
  if (!RaceInfos::getVariantPartner(m->infos, partner) || partner.ChrModelID.empty() || !RaceInfos::getVariantPair(m->infos.ChrModelID[0], pair))
  {
    why = _("That model is not available for this character.");
    return false;
  }
  // Loading frees the character first: the other model has to be there, readable and an M2 before anything goes.
  // The check reads it whole and leaves it open, so the load below takes that buffer instead of reading it again.
  wxString fileWhy;
  GameFile * partnerRead = nullptr;
  if (!ModelIdLookup::checkModelFile(partner.modelFileID, fileWhy, &partnerRead))
  {
    why = wxString::Format(_("The %s model (FileDataID %d) %s."), generationName(target), partner.modelFileID, fileWhy);
    return false;
  }

  // WHAT CARRIES OVER is the character, not its model: race and sex, appearance, class context, toggles, tabard,
  // sheathe, equipment (and which item models are shown), the animation by its animation ID with its time.
  struct CarriedItem
  {
    int slot = 0, id = -1, displayId = -1, level = 0;
    std::vector<std::pair<int, bool> > shownModels; // POSITION_SLOTS -> showModel
  };
  const int raceID = m->infos.raceID, sexID = m->infos.sexID;
  GameFile * originalFile = m->gamefile;
  const CharDetails::Appearance before = m->cd.captureAppearance();
  const bool demonHunter = m->cd.isDemonHunter();
  const EyeGlowTypes eyeGlow = m->cd.eyeGlowType;
  const bool showUnderwear = m->cd.showUnderwear, showEars = m->cd.showEars, showHair = m->cd.showHair,
             showFacialHair = m->cd.showFacialHair, showFeet = m->cd.showFeet, autoHide = m->cd.autoHideGeosetsForHeadItems;
  const TabardDetails tabard = m->td;
  const bool sheathe = m->bSheathe;
  std::vector<CarriedItem> items;
  for (int slot = 0; slot < NUM_CHAR_SLOTS; slot++)
    if (WoWItem * item = m->getItem((CharSlots)slot))
      if (item->id() > 0 || (item->id() == -1 && item->displayId() > 0))
      {
        CarriedItem carried;
        carried.slot = slot;
        carried.id = item->id();
        carried.displayId = item->displayId();
        carried.level = item->level();
        for (const auto & pm : item->models())
          if (pm.second)
            carried.shownModels.emplace_back((int)pm.first, pm.second->showModel);
        items.push_back(carried);
      }
  int animID = -1, subAnimID = 0;
  size_t frame = 0;
  float speed = 1.0f;
  bool paused = false;
  if (m->animManager && !m->anims.empty())
  {
    const size_t index = m->animManager->GetAnim();
    if (index < m->anims.size())
    {
      animID = m->anims[index].animID;
      subAnimID = m->anims[index].subAnimID;
    }
    paused = m->animManager->IsPaused();
    frame = m->animManager->GetFrameNow(!paused);
    speed = m->animManager->GetSpeed();
  }
  const auto capturedAt = std::chrono::steady_clock::now();

  // This character's memory of its pair's models.
  if (m_variantSession.pairID != pair.id || m_variantSession.raceID != raceID || m_variantSession.sexID != sexID)
  {
    m_variantSession.clear();
    m_variantSession.pairID = pair.id;
    m_variantSession.raceID = raceID;
    m_variantSession.sexID = sexID;
  }
  m_variantSession.byChrModel[before.chrModelID] = before;
  const auto remembered = m_variantSession.byChrModel.find(partner.ChrModelID[0]);
  const bool firstVisit = remembered == m_variantSession.byChrModel.end();
  const RaceInfos::Translation translation =
    firstVisit ? RaceInfos::translateSelection(pair, before.selection, pair.isPrimary(before.chrModelID)) : RaceInfos::Translation();
  const std::map<unsigned int, unsigned int> targetSelection = firstVisit ? translation.selection : remembered->second.selection;

  // Dresses the character just loaded, all before the scene goes out.
  auto dress = [&](const std::map<unsigned int, unsigned int> & selection, bool exact) {
    WoWModel * n = riderModel();
    if (!n)
      return;
    n->td = tabard;
    n->bSheathe = sheathe;
    n->cd.eyeGlowType = eyeGlow;
    n->cd.showUnderwear = showUnderwear;
    n->cd.showEars = showEars;
    n->cd.showHair = showHair;
    n->cd.showFacialHair = showFacialHair;
    n->cd.showFeet = showFeet;
    n->cd.autoHideGeosetsForHeadItems = autoHide;
    // The appearance first: an item's parts are chosen in the character's class context as it loads.
    n->cd.applyAppearance(selection, demonHunter, exact);
    for (const CarriedItem & carried : items)
      if (WoWItem * item = n->getItem((CharSlots)carried.slot))
      {
        item->restore(carried.id, carried.displayId, carried.level);
        const auto models = item->models();
        for (const auto & shown : carried.shownModels)
        {
          const auto pm = models.find((POSITION_SLOTS)shown.first);
          if (pm != models.end() && pm->second)
            pm->second->showModel = shown.second;
        }
      }
    charControl->RefreshModel();
    charControl->RefreshEquipment();
    charControl->SyncTabardSpins();
    charControl->BuildDeferredRows();   // the appearance rows, once, for the dressed character
    if (canvas && canvas->root)
      modelControl->RefreshModel(canvas->root);
    SyncCharacterMenuChecks(n);
    if (animID >= 0)
    {
      size_t now = frame;
      if (!paused)
        now += (size_t)(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - capturedAt).count() * speed);
      animControl->RestoreAnimation(animID, subAnimID, now, speed, paused);
    }
  };

  bool switched = false;
  {
    // The panels change once, and the Unity player is told once, about the dressed character.
    wxWindowUpdateLocker lockCharacter(charControl), lockAnimation(animControl), lockInfo(modelInspector);
    SceneHold hold(this);
    VariantSwitchScope scope(this);
    // The model's items are released with it, as Load Character does (a WoWItem frees nothing of its own).
    for (int slot = 0; slot < NUM_CHAR_SLOTS; slot++)
      if (WoWItem * item = m->getItem((CharSlots)slot))
        item->setId(0);
    LoadModel(GAMEDIRECTORY.getFile(partner.modelFileID), raceID, sexID);
    // The model's load reads and closes the file the check left open; one it never reached is closed here.
    if (partnerRead && partnerRead->isCurrentlyOpen())
      partnerRead->close();
    WoWModel * n = riderModel();
    switched = n && n->gamefile && (int)n->gamefile->fileDataId() == partner.modelFileID && !n->infos.ChrModelID.empty() &&
               n->infos.ChrModelID[0] == partner.ChrModelID[0] && n->infos.raceID == raceID && n->infos.sexID == sexID &&
               n->modelGeneration() == target;
    if (switched)
      dress(targetSelection, !firstVisit);
    else
    {
      // Whatever went wrong, the character is put back as it was.
      LOG_ERROR << "[variant] ChrModel" << partner.ChrModelID[0] << "(FileDataID" << partner.modelFileID << ") did not load as a"
                << generationName(target).ToStdString().c_str() << "character -- the character is put back on its model";
      LoadModel(originalFile, raceID, sexID);
      dress(before.selection, true);
      why = wxString::Format(_("The %s model could not be built; the character was kept as it was."), generationName(target));
    }
  }

  if (switched)
  {
    WoWModel * n = riderModel();
    if (firstVisit)
    {
      std::vector<unsigned int> replaced;
      for (const auto & oc : translation.selection)
        if (n->cd.get(oc.first) != oc.second)
          replaced.push_back(oc.first);
      LOG_INFO << "[variant]" << characterName(n->infos).ToStdString().c_str() << "ChrModel" << before.chrModelID << "->"
               << n->infos.ChrModelID[0] << "(first visit):" << translation.carried.size() << "of" << before.selection.size()
               << "option(s) carried | no counterpart:" << optionNames(translation.noCounterpart) << "| choice not paired:"
               << optionNames(translation.notCovered) << "| replaced by the player's rules:" << optionNames(replaced)
               << "| options of the new model set to their default:" << (n->cd.captureAppearance().selection.size() - translation.carried.size() + replaced.size());
      std::vector<unsigned int> lost = translation.noCounterpart;
      lost.insert(lost.end(), translation.notCovered.begin(), translation.notCovered.end());
      m_variantSession.note = lost.empty() ? wxString()
        : wxString::Format(_("Carried over %d of %d appearance options. Not on the %s model: %s."),
                           (int)translation.carried.size(), (int)before.selection.size(), generationName(target),
                           wxString(optionNames(lost).toStdWString()));
    }
    else
    {
      LOG_INFO << "[variant]" << characterName(n->infos).ToStdString().c_str() << "ChrModel" << before.chrModelID << "->"
               << n->infos.ChrModelID[0] << ": its appearance on this model restored exactly (" << targetSelection.size() << "option(s))";
      m_variantSession.note.clear();
    }
  }
  DisplayedContentChanged();
  if (charControl)
  {
    charControl->BuildDeferredRows();   // a switch that ended before dressing anything
    charControl->SyncModelVariant();
  }
  return switched;
}

void ModelViewer::OnExport(wxCommandEvent &event)
{
  if (!g_charControl->model)
  {
    wxMessageBox(wxT("You must prepare your model before trying to export it."), wxT("Export Error"), wxOK | wxICON_ERROR);
    return;
  }

  std::wstring exporterLabel = fileMenu->GetLabel(event.GetId());

  PluginManager::iterator it = PLUGINMANAGER.begin();
  for (; it != PLUGINMANAGER.end(); ++it)
  {
    ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*it);

    if (plugin && plugin->menuLabel() == exporterLabel)
    {
      wxFileDialog saveFileDialog(this, plugin->fileSaveTitle(), L"", L"",
                                  plugin->fileSaveFilter(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

      if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;

      const wxString outPath = saveFileDialog.GetPath();

      // The FBX exporter runs the heavy, non-thread-safe FBX SDK; if it freezes or crashes it
      // must not take WMV down with it. So FBX exports are dispatched to a separate process
      // (see ExportJobManager) while the UI stays live. Other exporters (OBJ, ...) are quick
      // and stay in-process below.
      const bool isFbx = (plugin->menuLabel() == std::wstring(L"FBX..."));

      // Gather the content + clip selection. Only animation-capable exporters (FBX) prompt.
      // Component/raw export (UV2 + raw per-unit textures + node-based sidecar for the Blender
      // add-on) is ALWAYS on -- it is the only FBX export mode now, no longer a dialog option.
      bool optMesh = true, optSkel = true, optSkin = true, optAnim = true, optComponent = true;
      std::vector<int> animsToExport;
      if (plugin->canExportAnimation())
      {
        WoWModel * m = const_cast<WoWModel *>(canvas->model());
        std::map<int, std::wstring> animsMap = m->getAnimsMap();
        wxArrayString values;
        wxArrayInt selection;
        wxArrayInt animationIds;

        for (size_t I = 0; I < canvas->model()->anims.size(); I++)
        {
          const int animationId = m->anims[I].animID;
          wxString animName = animsMap[animationId];
          if (animName.IsEmpty())
            animName = wxString::Format(_("Animation %d"), animationId);
          values.Add(animName);
          animationIds.Add(animationId);
        }

        AnimationExportChoiceDialog animChoiceDlg(this, L"", wxT("FBX Export Options"), values, animationIds);
        if (animChoiceDlg.ShowModal() == wxID_CANCEL)
          return;

        optMesh = animChoiceDlg.exportMesh();
        optSkel = animChoiceDlg.exportSkeleton();
        optSkin = animChoiceDlg.exportSkinning();
        optAnim = animChoiceDlg.exportAnimations();

        // Clip selection only matters when animations are being exported.
        if (optAnim)
        {
          selection = animChoiceDlg.GetAnimationSelections();
          animsToExport.reserve(selection.GetCount());
          for (unsigned int I = 0; I < selection.GetCount(); I++)
            // The exporter indexes the model's animation array, not its track Index field.
            animsToExport.push_back(selection[I]);
        }
      }

      // ---------- Out-of-process FBX export ----------
      if (isFbx && m_exportJobManager)
      {
        // Build the descriptor a fresh process needs to reload exactly this asset.
        wxString assetArgs, assetLabel, tempCharPath;
        if (PrepareFbxAsset(assetArgs, assetLabel, tempCharPath))
        {
          ExportJobManager::Request req;
          req.assetArgs    = assetArgs;
          req.assetLabel   = assetLabel;
          req.outPath      = outPath;
          req.build        = wxString(m_loadedBuild.toStdWString().c_str());
          req.product      = wxString(m_loadedProduct.toStdWString().c_str());
          req.mesh         = optMesh;
          req.skeleton     = optSkel;
          req.skinning     = optSkin;
          req.animation    = optAnim;
          req.component    = optComponent;
          req.tempCharPath = tempCharPath;
          wxString csv;
          for (size_t I = 0; I < animsToExport.size(); I++)
          {
            if (I) csv << wxT(",");
            csv << animsToExport[I];
          }
          req.clipsCsv = csv;

          if (!m_exportJobManager->startExport(req) && !tempCharPath.IsEmpty())
            wxRemoveFile(tempCharPath);
          return; // async: the manager owns progress, completion, and cleanup from here
        }

        wxMessageBox(wxT("Could not save the model and equipment for export."),
                     wxT("Export Error"), wxOK | wxICON_ERROR, this);
        return;
      }

      // ---------- In-process export (non-FBX, or FBX fallback) ----------
      plugin->setExportOptions(optMesh, optSkel, optSkin, optAnim);
      plugin->setAnimationsToExport(animsToExport);

      WoWModel * m = const_cast<WoWModel *>(canvas->model());
      // The pose the Animation panel is on, computed now: see UpdateExportPose.
      UpdateExportPose();
      if (!plugin->exportModel(m, std::wstring(outPath.c_str())))
      {
        // Surface the exporter's specific reason (missing skeleton, unwritable path, ...) so the
        // user knows what to change, falling back to a generic message if none was set.
        std::wstring err = plugin->lastError();
        wxString msg = err.empty() ? wxString(wxT("An error occurred during export."))
                                   : wxString(err.c_str());
        wxMessageBox(msg, wxT("Export Error"), wxOK | wxICON_ERROR);
      }
      else
      {
        wxMessageBox(wxT("Export successfully done."), wxT("Export done"), wxOK | wxICON_INFORMATION);
      }

      break;
    }
  }
}

// Pose every model in the scene -- the root and everything attached to it, parents before children --
// at the animation clock's current frame.
static void updateAttachmentPose(Attachment * att)
{
  if (!att)
    return;
  if (WoWModel * m = dynamic_cast<WoWModel *>(att->model()))
    m->updatePose();
  for (Attachment * child : att->children)
    updateAttachmentPose(child);
}

// The exporters read the pose: OBJ writes the skinned vertices (unless Settings > Export "Init pose only
// export" is set) and places equipped items with the bone matrices, and both OBJ and FBX pick render
// passes and texture scrolling by the current animation. That pose used to be whatever the OpenGL viewport
// had last drawn -- and nothing draws since it was archived, so without this an export would silently
// fall back to the bind pose and animation 0. It is computed here instead, for the animation and frame the
// Animation panel shows, just before an export reads it.
void ModelViewer::UpdateExportPose()
{
  if (canvas && canvas->root)
    updateAttachmentPose(canvas->root);
}

void ModelViewer::OnStatusBarRefreshTimer(wxTimerEvent& event)
{
  SetStatusText(wxString::Format(wxT("Memory: %i Mo"), core::getMemoryUsed()), 4);

  // A player that crashed, was closed from outside or dropped its connection is noticed here, on this
  // existing two-second timer, rather than at the next model load: the viewport shows the restart
  // notice instead of a frozen or empty rectangle.
  if (unityRendererHost && unityRendererHost->checkPlayerHealth())
  {
    UpdateUnityViewportState();
    if (charControl)
      charControl->SyncModelVariant();
  }

  // A screenshot the player never answered: it went away, or it is stuck.
  if (m_screenshotRequest != 0)
  {
    const bool connected = unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady();
    if (!connected || timeGetTime() - m_screenshotSentAt >= SCREENSHOT_TIMEOUT_MS)
    {
      LOG_ERROR << "Screenshot request" << m_screenshotRequest << "was not answered"
                << (connected ? "in time" : "before the Unity viewport went away");
      m_screenshotRequest = 0;
      if (GetStatusBar())
        SetStatusText(connected ? _("Screenshot failed: the Unity viewport did not answer")
                                : _("Screenshot failed: the Unity viewport is not running"), 0);
    }
  }
}
