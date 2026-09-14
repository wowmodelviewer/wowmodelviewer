#include "modelviewer.h"
#include "ClientChoiceDialog.h"   // File > Load World of Warcraft opens it

#include "AnimationExportChoiceDialog.h"
#include "AnimManager.h"

#include <wx/aboutdlg.h>
#include <wx/aui/auibar.h>
#include <wx/numformatter.h>
#include <wx/srchctrl.h>
#include <wx/busyinfo.h>
#include <wx/colordlg.h>
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
#include "ImageSequenceExporter.h"
#include "ImageSequenceDialog.h"
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
#include "MemoryUtils.h"
#include "ModelInspector.h"
#include "ModelRenderPass.h"
#include "NPCImporterDialog.h"
#include "PluginManager.h"
#include "RaceInfos.h"
#include "SettingsControl.h"
#include "UiStyle.h"
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
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>

#include <fstream>



// default colour values
const static float def_ambience[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
const static float def_diffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
const static float def_emission[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
const static float def_specular[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

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
    { wxACCEL_CTRL, (int)'b', ID_SHOW_BOUNDS, L"Ctrl+B", L"Show or hide the bounding box", L"OpenGL viewport" },
    { wxACCEL_CTRL, (int)'X', ID_FILE_EXIT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F12, ID_FILE_SCREENSHOT, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'e', ID_SHOW_EARS, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'h', ID_SHOW_HAIR, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'f', ID_SHOW_FACIALHAIR, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'z', ID_SHEATHE, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'l', ID_BACKGROUND, nullptr, nullptr, nullptr },
    // Ctrl +/- are bound, but their handler is commented out (OnToggleCommand): not listed.
    { wxACCEL_CTRL, (int)'+', ID_ZOOM_IN, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, (int)'-', ID_ZOOM_OUT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F9, ID_CLEAR_EQUIPMENT, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F10, ID_CHAR_RANDOMISE, nullptr, nullptr, nullptr },
    // F11 is Fullscreen (View menu). It was also bound here to the OpenGL debug toggle, which made
    // the two fight over the key; that toggle is a View menu item now.

    // Temporary saves
    { wxACCEL_NORMAL, WXK_F1, ID_SAVE_TEMP1, L"F1 \u2013 F4", L"Save the camera view to slot 1-4", L"OpenGL viewport" },
    { wxACCEL_NORMAL, WXK_F2, ID_SAVE_TEMP2, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F3, ID_SAVE_TEMP3, nullptr, nullptr, nullptr },
    { wxACCEL_NORMAL, WXK_F4, ID_SAVE_TEMP4, nullptr, nullptr, nullptr },

    // Temp loads
    { wxACCEL_CTRL, WXK_F1, ID_LOAD_TEMP1, L"Ctrl+F1 \u2013 F4", L"Restore the camera view from slot 1-4", L"OpenGL viewport" },
    { wxACCEL_CTRL, WXK_F2, ID_LOAD_TEMP2, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, WXK_F3, ID_LOAD_TEMP3, nullptr, nullptr, nullptr },
    { wxACCEL_CTRL, WXK_F4, ID_LOAD_TEMP4, nullptr, nullptr, nullptr },

    { wxACCEL_CTRL | wxACCEL_SHIFT, (int)'R', ID_RESTART, nullptr, nullptr, nullptr },
  };
}

// Class event handler/importer
IMPLEMENT_CLASS(ModelViewer, wxFrame)

BEGIN_EVENT_TABLE(ModelViewer, wxFrame)
EVT_CLOSE(ModelViewer::OnClose)
//EVT_SIZE(ModelViewer::OnSize)

// File menu
EVT_MENU(ID_LOAD_WOW, ModelViewer::OnGameToggle)
EVT_MENU(ID_LOAD_MPQ, ModelViewer::OnLoadLegacyMpq)
EVT_MENU(ID_FILE_VIEWLOG, ModelViewer::OnViewLog)
EVT_MENU(ID_VIEW_NPC, ModelViewer::OnCharToggle)
EVT_MENU(ID_VIEW_ITEM, ModelViewer::OnCharToggle)
EVT_MENU(ID_FILE_SCREENSHOT, ModelViewer::OnSave)
EVT_MENU(ID_FILE_EXPORTIMGSEQ, ModelViewer::OnExportImageSequence)
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
EVT_MENU(ID_SHOW_LIGHT, ModelViewer::OnToggleDock)
EVT_MENU(ID_SHOW_MODEL, ModelViewer::OnToggleDock)
EVT_MENU(ID_VIEW_UNITY_RENDERER, ModelViewer::OnUnityRenderer)
EVT_MENU(ID_VIEW_UNITY_PRIMARY, ModelViewer::OnUnityPrimaryViewport)
EVT_MENU(ID_VIEW_FULLSCREEN, ModelViewer::OnToggleFullScreen)
EVT_CHAR_HOOK(ModelViewer::OnCharHook)

// Command bar (and the panel toggles it shares with the View menu)
EVT_MENU(ID_UI_OPEN_MODEL, ModelViewer::OnCommandBar)
EVT_MENU(ID_UI_RESET_CAMERA, ModelViewer::OnCommandBar)
EVT_MENU(ID_UI_SCREENSHOT, ModelViewer::OnCommandBar)
EVT_UPDATE_UI(ID_SHOW_FILE_LIST, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_SHOW_CHAR, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_SHOW_ANIM, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_UI_RESET_CAMERA, ModelViewer::OnUpdateCommandUI)
EVT_UPDATE_UI(ID_UI_SCREENSHOT, ModelViewer::OnUpdateCommandUI)
// --
//EVT_MENU(ID_SHOW_WIREFRAME, ModelViewer::OnToggleCommand)
//EVT_MENU(ID_SHOW_BONES, ModelViewer::OnToggleCommand)
EVT_MENU(ID_SHOW_BOUNDS, ModelViewer::OnToggleCommand)
//EVT_MENU(ID_SHOW_PARTICLES, ModelViewer::OnToggleCommand)

EVT_MENU(ID_BACKGROUND, ModelViewer::OnBackground)
EVT_MENU(ID_BG_COLOR, ModelViewer::OnSetColor)

EVT_MENU(ID_USE_CAMERA, ModelViewer::OnToggleCommand)

// Cam
EVT_MENU(ID_CAM_FRONT, ModelViewer::OnCamMenu)
EVT_MENU(ID_CAM_SIDE, ModelViewer::OnCamMenu)
EVT_MENU(ID_CAM_BACK, ModelViewer::OnCamMenu)
EVT_MENU(ID_CAM_ISO, ModelViewer::OnCamMenu)
EVT_MENU(ID_CAM_RESET, ModelViewer::OnCamMenu)

EVT_MENU(ID_CANVASS120, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASS512, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASS1024, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASF480, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASF600, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASF768, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASF864, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASF1200, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASW480, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASW720, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASW1080, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASM768, ModelViewer::OnCanvasSize)
EVT_MENU(ID_CANVASM1200, ModelViewer::OnCanvasSize)

// hidden hotkeys for zooming
EVT_MENU(ID_ZOOM_IN, ModelViewer::OnToggleCommand)
EVT_MENU(ID_ZOOM_OUT, ModelViewer::OnToggleCommand)
EVT_MENU(ID_OPENGL_DEBUG, ModelViewer::OnToggleCommand)

// Light Menu
EVT_MENU(ID_LT_SAVE, ModelViewer::OnLightMenu)
EVT_MENU(ID_LT_LOAD, ModelViewer::OnLightMenu)
//EVT_MENU(ID_LT_COLOR, ModelViewer::OnSetColor)
EVT_MENU(ID_LT_TRUE, ModelViewer::OnLightMenu)
EVT_MENU(ID_LT_AMBIENT, ModelViewer::OnLightMenu)
EVT_MENU(ID_LT_DIRECTIONAL, ModelViewer::OnLightMenu)
EVT_MENU(ID_LT_MODEL, ModelViewer::OnLightMenu)
EVT_MENU(ID_LT_DIRECTION, ModelViewer::OnLightMenu)

// Effects
EVT_MENU(ID_ENCHANTS, ModelViewer::OnEffects)

// Options
EVT_MENU(ID_SAVE_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_IMPORT_CHAR, ModelViewer::OnToggleCommand)
EVT_MENU(ID_IMPORT_NPC, ModelViewer::OnImportNPCFromURL)

EVT_MENU(ID_DEFAULT_DOODADS, ModelViewer::OnToggleCommand)
EVT_MENU(ID_USE_ANTIALIAS, ModelViewer::OnToggleCommand)
EVT_MENU(ID_USE_HWACC, ModelViewer::OnToggleCommand)
EVT_MENU(ID_USE_ENVMAP, ModelViewer::OnToggleCommand)
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

// Hidden menu items
// Temporary saves
EVT_MENU(ID_SAVE_TEMP1, ModelViewer::OnToggleCommand)
EVT_MENU(ID_SAVE_TEMP2, ModelViewer::OnToggleCommand)
EVT_MENU(ID_SAVE_TEMP3, ModelViewer::OnToggleCommand)
EVT_MENU(ID_SAVE_TEMP4, ModelViewer::OnToggleCommand)

// Temp loads
EVT_MENU(ID_LOAD_TEMP1, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_TEMP2, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_TEMP3, ModelViewer::OnToggleCommand)
EVT_MENU(ID_LOAD_TEMP4, ModelViewer::OnToggleCommand)

// Export
EVT_MENU(ID_EXPORT_MODEL, ModelViewer::OnExport)

// refesh status bar timer
EVT_TIMER(ID_STATUS_REFRESH_TIMER, ModelViewer::OnStatusBarRefreshTimer)

END_EVENT_TABLE()

ModelViewer::ModelViewer()
#ifdef _LINUX
// Transparency in interfaceManager crashes with Linux compositing
: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_VENETIAN_BLINDS_HINT)
#endif
{
  PLUGINMANAGER.init("./plugins");
  // our main class objects
  animControl = nullptr;
  canvas = NULL;
  charControl = NULL;
  enchants = NULL;
  lightControl = NULL;
  modelControl = NULL;
  modelInspector = NULL;
  imageControl = NULL;
  settingsControl = NULL;
  unityRendererHost = NULL;
  m_lastAnimStatePush = 0;
  animExporter = NULL;
  fileControl = NULL;

  //wxWidget objects
  menuBar = NULL;
  charMenu = NULL;
  charGlowMenu = NULL;
  viewMenu = NULL;
  optMenu = NULL;
  lightMenu = NULL;
  exportMenu = NULL;
  fileMenu = NULL;
  camMenu = NULL;

  isWoWLoaded = false;
  isModel = false;
  isWMO = false;
  isChar = false;
  isADT = false;
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
    CreateStatusBar(5);
    int widths[] = { -1, 100, 50, 125, 125 };
    SetStatusWidths(5, widths);
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
  fileMenu->Append(ID_FILE_SCREENSHOT, _("Save Screenshot\tF12"));
  fileMenu->Append(ID_FILE_EXPORTIMGSEQ, _("Export Image Sequence..."));
  fileMenu->AppendSeparator();

  // --== Continue regular menu ==--

  // export menu
  wxMenu *ExportMenu = new wxMenu;
  ExportMenu->Append(ID_FILE_MODEL_INFO, wxT("Export ModelInfo.xml"));

  PluginManager::iterator it = PLUGINMANAGER.begin();
  int subMenuId = 10000;
  for (; it != PLUGINMANAGER.end(); ++it, subMenuId++)
  {
    ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*it);

    if (plugin)
    {
      ExportMenu->Append(subMenuId, plugin->menuLabel());
      Connect(subMenuId,
              wxEVT_COMMAND_MENU_SELECTED,
              wxCommandEventHandler(ModelViewer::OnExport));
    }
  }
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
  viewMenu->Append(ID_SHOW_MODEL, _("Render options (OpenGL viewport)..."));
  viewMenu->AppendSeparator();
  viewMenu->Append(ID_VIEW_UNITY_RENDERER, _("Unity Renderer"));
  viewMenu->AppendCheckItem(ID_VIEW_UNITY_PRIMARY, _("Unity as main viewport"));
  viewMenu->Check(ID_VIEW_UNITY_PRIMARY, unityPrimaryViewport);
  viewMenu->Append(ID_VIEW_FULLSCREEN, _("Fullscreen\tF11"));
  if (canvas) {
    viewMenu->Append(ID_BG_COLOR, _("Background Color..."));
    viewMenu->AppendCheckItem(ID_BACKGROUND, _("Load Background\tCTRL+L"));
    viewMenu->Check(ID_BACKGROUND, canvas->drawBackground);


    viewMenu->AppendSeparator();
  }

  try {

    // Camera Menu
    wxMenu *CamMenu = new wxMenu;
    CamMenu->AppendCheckItem(ID_USE_CAMERA, _("Use model camera"));
    CamMenu->AppendSeparator();
    CamMenu->Append(ID_CAM_FRONT, _("Front"));
    CamMenu->Append(ID_CAM_BACK, _("Back"));
    CamMenu->Append(ID_CAM_SIDE, _("Side"));
    CamMenu->Append(ID_CAM_ISO, _("Perspective"));
    CamMenu->Append(ID_CAM_RESET, _("Reset to default"));

    viewMenu->Append(ID_CAMERA, _("Camera"), CamMenu);
    viewMenu->AppendSeparator();

    wxMenu *setSize = new wxMenu;
    setSize->Append(ID_CANVASS120, wxT("(1:1) 120 x 120"), _("Square (1:1)"));
    setSize->Append(ID_CANVASS512, wxT("(1:1) 512 x 512"), _("Square (1:1)"));
    setSize->Append(ID_CANVASS1024, wxT("(1:1) 1024 x 1024"), _("Square (1:1)"));
    setSize->Append(ID_CANVASF480, wxT("(4:3) 640 x 480"), _("Fullscreen (4:3)"));
    setSize->Append(ID_CANVASF600, wxT("(4:3) 800 x 600"), _("Fullscreen (4:3)"));
    setSize->Append(ID_CANVASF768, wxT("(4:3) 1024 x 768"), _("Fullscreen (4:3)"));
    setSize->Append(ID_CANVASF864, wxT("(4:3) 1152 x 864"), _("Fullscreen (4:3)"));
    setSize->Append(ID_CANVASF1200, wxT("(4:3) 1600 x 1200"), _("Fullscreen (4:3)"));
    setSize->Append(ID_CANVASW480, wxT("(16:9) 864 x 480"), _("Widescreen (16:9)"));
    setSize->Append(ID_CANVASW720, wxT("(16:9) 1280 x 720"), _("Widescreen (16:9)"));
    setSize->Append(ID_CANVASW1080, wxT("(16:9) 1920 x 1080"), _("Widescreen (16:9)"));
    setSize->Append(ID_CANVASM768, wxT("(5:3) 1280 x 768"), _("Misc (5:3)"));
    setSize->Append(ID_CANVASM1200, wxT("(8:5) 1920 x 1200"), _("Misc (8:5)"));

    viewMenu->Append(ID_CANVASSIZE, wxT("Set Canvas Size"), setSize);
    // Was only reachable through an F11 accelerator that collided with Fullscreen (F11).
    viewMenu->Append(ID_OPENGL_DEBUG, _("OpenGL debug info in title bar"));

    //lightMenu->Append(ID_LT_COLOR, wxT("Lighting Color..."));

    lightMenu = new wxMenu;
    lightMenu->Append(ID_LT_SAVE, _("Save Lighting"));
    lightMenu->Append(ID_LT_LOAD, _("Load Lighting"));
    lightMenu->AppendSeparator();
    lightMenu->AppendCheckItem(ID_LT_DIRECTION, _("Render Light Objects"));
    lightMenu->AppendSeparator();
    lightMenu->AppendCheckItem(ID_LT_TRUE, _("Use true lighting"));
    lightMenu->Check(ID_LT_TRUE, false);
    lightMenu->AppendRadioItem(ID_LT_DIRECTIONAL, _("Use dynamic light"));
    lightMenu->Check(ID_LT_DIRECTIONAL, true);
    lightMenu->AppendRadioItem(ID_LT_AMBIENT, _("Use ambient light"));
    lightMenu->AppendRadioItem(ID_LT_MODEL, _("Model lights only"));

    charMenu = new wxMenu;
    charMenu->Append(ID_LOAD_CHAR, _("Load Character\tF8"));
    charMenu->Append(ID_IMPORT_CHAR, _("Import Armory Character"));
    charMenu->Append(ID_IMPORT_NPC, _("Import NPC from URL..."));
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
    optMenu->AppendCheckItem(ID_DEFAULT_DOODADS, _("Always show default doodads in WMOs"));
    optMenu->Check(ID_DEFAULT_DOODADS, true);
    optMenu->AppendSeparator();
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

  // The Model panel first: the skin, doodad and character controls are created on its pages.
  modelInspector = new ModelInspector(this, wxID_ANY);
  animControl = new AnimControl(this, ID_ANIM_FRAME, modelInspector->skinParent(),
                                modelInspector->overridesParent(), modelInspector->doodadParent());
  charControl = new CharControl(modelInspector->characterParent(), ID_CHAR_FRAME);
  modelInspector->AttachAppearance(animControl, charControl);
  lightControl = new LightControl(this, ID_LIGHT_FRAME);
  lightControl->Show(false);   // kept only to drive the default scene light; never shown as UI
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

  animExporter = new CAnimationExporter(this, wxID_ANY, wxT("Animation Exporter"), wxDefaultPosition, wxSize(350, 220), wxCAPTION | wxSTAY_ON_TOP | wxFRAME_NO_TASKBAR);
}

void ModelViewer::InitDatabase()
{
  LOG_INFO << "Initializing Databases...";
  SetStatusText(wxT("Initializing Databases..."));
  wxBusyCursor busyCursor;
  wxWindowDisabler disableAll;
  wxBusyInfo info(_T("Please wait during game database analysis..."), this);

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
      initDB = false;
      LOG_ERROR << "Error during NPC detection from database.";
      return;
    }

  }
  
  {
    sqlResult item = GAMEDATABASE.sqlQuery("SELECT Item.ID, ItemSparse.Display_Lang, Item.InventoryType, Item.ClassID, Item.SubclassID, Item.SheathType FROM Item LEFT JOIN ItemSparse ON Item.ID = ItemSparse.ID WHERE Item.InventoryType !=0 AND ItemSparse.Display_Lang != \"\"");

    if (item.valid && !item.empty())
    {
      LOG_INFO << "Found" << item.values.size() << "items";
      for (int i = 0, imax = item.values.size(); i < imax; i++)
      {
        ItemRecord rec(item.values[i]);
        items.items.push_back(rec);
      }
    }
    else
    {
      initDB = false;
      LOG_ERROR << "Error during Item detection from database.";
      return;
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
  const int height = frame->FromDIP(34);
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

// The OpenGL-only render flags (alpha, scale, bones, wireframe, bounds...) and the attachment the
// animation controls drive: the floating window View > "Render options" opens.
static wxAuiPaneInfo buildRenderOptionsPaneInfo()
{
  return wxAuiPaneInfo().
         Name(wxT("Models")).Caption(wxT("Render Options (OpenGL viewport)")).
         FloatingSize(wxSize(180, 300)).Float().TopDockable(false).LeftDockable(false).
         RightDockable(false).BottomDockable(false).Show(false).
         DestroyOnClose(false);
}

void ModelViewer::InitCommandBar()
{
  // Text commands, few of them: the viewport is the thing to look at. The three panel buttons are
  // check tools, pressed while their panel is shown (OnUpdateCommandUI keeps them in step with the
  // panels however those were opened or closed).
  commandBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_PLAIN_BACKGROUND |
                                wxAUI_TB_NO_AUTORESIZE);
  commandBar->SetToolBorderPadding(FromDIP(6));
  commandBar->SetMargins(FromDIP(wxSize(UiStyle::S, 2)));

  commandBar->AddTool(ID_UI_OPEN_MODEL, _("Open model"), wxNullBitmap,
                      _("Find a model in Browse (loads a World of Warcraft client first if none is loaded)"));
  commandBar->AddSeparator();
  commandBar->AddTool(ID_UI_RESET_CAMERA, _("Reset camera"), wxNullBitmap, _("Reset the camera to frame the model"));
  commandBar->AddTool(ID_UI_SCREENSHOT, _("Screenshot"), wxNullBitmap, _("Save a screenshot (F12)"));
  commandBar->AddTool(ID_VIEW_FULLSCREEN, _("Fullscreen"), wxNullBitmap, _("Fullscreen (F11; Esc leaves)"));
  commandBar->AddSeparator();

  commandModelLabel = new wxStaticText(commandBar, ID_UI_MODEL_LABEL, _("No model loaded"), wxDefaultPosition,
                                       FromDIP(wxSize(320, -1)), wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
  commandModelLabel->SetForegroundColour(UiStyle::secondaryText());
  commandBar->AddControl(commandModelLabel);

  commandBar->AddStretchSpacer();
  commandBar->AddTool(ID_SHOW_FILE_LIST, _("Browse"), wxNullBitmap, _("Show or hide the Browse panel"), wxITEM_CHECK);
  commandBar->AddTool(ID_SHOW_CHAR, _("Model"), wxNullBitmap, _("Show or hide the Model panel"), wxITEM_CHECK);
  commandBar->AddTool(ID_SHOW_ANIM, _("Animation"), wxNullBitmap, _("Show or hide the Animation panel"), wxITEM_CHECK);
  commandBar->Realize();
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

  // OpenGL Canvas
  interfaceManager.AddPane(canvas, wxAuiPaneInfo().
                           Name(wxT("canvas")).Caption(wxT("OpenGL Canvas")).
                           CenterPane());

  interfaceManager.AddPane(fileControl, buildBrowsePaneInfo(this));
  interfaceManager.AddPane(animControl, buildAnimationPaneInfo(this));
  interfaceManager.AddPane(modelInspector, buildInspectorPaneInfo(this));

  // Lighting control panel removed (lighting feature pulled). lightControl still exists and
  // provides the default scene light, but it is no longer shown as a dockable pane.

  // model control
  interfaceManager.AddPane(modelControl, buildRenderOptionsPaneInfo());

  // settings frame
  interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
                           Name(wxT("Settings")).Caption(wxT("Settings")).
                           FloatingSize(wxSize(400, 550)).Float().TopDockable(false).LeftDockable(false).
                           RightDockable(false).BottomDockable(false).Fixed().Show(false));

  // tell the manager to "commit" all the changes just made
  //interfaceManager.Update();
}

// forward decl (defined next to OnUnityRenderer): shared Unity pane settings
static wxAuiPaneInfo buildUnityRendererPaneInfo();

void ModelViewer::ResetLayout()
{
  // The Unity viewport keeps the role it has: a reset rearranges the panels, it does not move the
  // model from one renderer to the other.
  bool unityCentre = false;
  if (unityRendererHost)
  {
    wxAuiPaneInfo & up = interfaceManager.GetPane(unityRendererHost);
    unityCentre = up.IsOk() && up.IsShown() && up.dock_direction == wxAUI_DOCK_CENTER;
  }

  interfaceManager.DetachPane(commandBar);
  interfaceManager.DetachPane(fileControl);
  if (unityRendererHost)
    interfaceManager.DetachPane(unityRendererHost);
  interfaceManager.DetachPane(animControl);
  interfaceManager.DetachPane(modelInspector);
  interfaceManager.DetachPane(lightControl);
  interfaceManager.DetachPane(modelControl);
  interfaceManager.DetachPane(settingsControl);
  interfaceManager.DetachPane(canvas);

  interfaceManager.AddPane(commandBar, buildCommandBarPaneInfo(this));

  // OpenGL Canvas
  interfaceManager.AddPane(canvas, wxAuiPaneInfo().
                           Name(wxT("canvas")).Caption(wxT("OpenGL Canvas")).
                           CenterPane().Show(!unityCentre));

  interfaceManager.AddPane(fileControl, buildBrowsePaneInfo(this).Show(true));
  interfaceManager.AddPane(animControl, buildAnimationPaneInfo(this).Show(true));
  interfaceManager.AddPane(modelInspector, buildInspectorPaneInfo(this).Show(true));

  // (Lighting pane removed; lightControl still provides the default scene light.)

  interfaceManager.AddPane(modelControl, buildRenderOptionsPaneInfo());

  interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
                           Name(wxT("Settings")).Caption(wxT("Settings")).
                           FloatingSize(wxSize(400, 550)).Float().TopDockable(false).LeftDockable(false).
                           RightDockable(false).BottomDockable(false).Show(false));

  // Unity viewport pane (only exists once View > Unity Renderer has been used)
  if (unityRendererHost)
  {
    if (unityCentre)
      interfaceManager.AddPane(unityRendererHost, wxAuiPaneInfo().
                               Name(wxT("unityRenderer")).Caption(wxT("Unity Renderer")).
                               CenterPane().Show(true));
    else
      interfaceManager.AddPane(unityRendererHost,
                               buildUnityRendererPaneInfo().Show(unityAsidePaneShown()));
  }

  // tell the manager to "commit" all the changes just made
  interfaceManager.Update();
}


void ModelViewer::LoadSession()
{
  LOG_INFO << "Loading Session settings from:" << QString::fromWCharArray(cfgPath.c_str());

  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  // Application Config Settings
  useRandomLooks = config.value("Session/RandomLooks", true).toBool();
  GLOBALSETTINGS.bShowParticle = config.value("Session/ShowParticle", true).toBool();
  GLOBALSETTINGS.bZeroParticle = config.value("Session/ZeroParticle", true).toBool();
  GLOBALSETTINGS.bInitPoseOnlyExport = config.value("Session/InitPoseOnlyExport", false).toBool();

  // Last legacy-MPQ folder picked via File > Load Legacy MPQ Client... (defaults the dir picker).
  m_lastMpqFolder = config.value("Session/LastMpqFolder", "").toString();

  // Background and Custom Colours
  wxString colStr;
  wxColour bgCol;
  colStr = config.value("Session/bgCol", "#475F79").toString().toStdWString(); // #475F79 = (71, 95, 121)
  if (!bgCol.Set(colStr))
    bgCol = wxColour(71, 95, 121);
  bgDialogData.SetColour(bgCol);
  for (int i = 0; i < 16; i++)
  {
    wxColour custCol;
    colStr = config.value(QString("Session/bgCustCol%1").arg(i), wxEmptyString).toString().toStdWString();
    if ((colStr != wxEmptyString) && custCol.Set(colStr))
      bgDialogData.SetCustomColour(i, custCol);
  }
  // Other session settings
  if (canvas)
  {
    // Set canvas background Colour
    canvas->vecBGColor.x = bgCol.Red() / 255.0f;
    canvas->vecBGColor.y = bgCol.Green() / 255.0f;
    canvas->vecBGColor.z = bgCol.Blue() / 255.0f;

    // boolean vars
    canvas->drawBackground = config.value("Session/DBackground", false).toBool();
    bgImagePath = config.value("Session/BackgroundImage", false).toString().toStdWString();

    if (!bgImagePath.IsEmpty())
      canvas->LoadBackground(bgImagePath);
  }
}

void ModelViewer::SaveSession()
{
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  config.setValue("Graphics/FSAA", video.curCap.aaSamples);
  config.setValue("Graphics/AccumulationBuffer", video.curCap.accum);
  config.setValue("Graphics/AlphaBits", video.curCap.alpha);
  config.setValue("Graphics/ColourBits", video.curCap.colour);
  config.setValue("Graphics/DoubleBuffer", video.curCap.doubleBuffer);
  config.setValue("Graphics/HWAcceleration", video.curCap.hwAcc);
  config.setValue("Graphics/SampleBuffer", video.curCap.sampleBuffer);
  config.setValue("Graphics/StencilBuffer", video.curCap.stencil);
  config.setValue("Graphics/ZBuffer", video.curCap.zBuffer);
  config.setValue("Graphics/UseEnvMapping", video.useEnvMapping);
  config.setValue("Graphics/Fov", (double)video.fov);

  config.setValue("Session/RandomLooks", useRandomLooks);
  config.setValue("Session/ShowParticle", GLOBALSETTINGS.bShowParticle);
  config.setValue("Session/ZeroParticle", GLOBALSETTINGS.bZeroParticle);
  config.setValue("Session/InitPoseOnlyExport", GLOBALSETTINGS.bInitPoseOnlyExport);
  config.setValue("Session/LastMpqFolder", m_lastMpqFolder);

  // Armory importer proxy URL override (entered in General Settings).
  config.setValue("Armory/ProxyURL", QString::fromStdString(GLOBALSETTINGS.armoryProxyURL()));

  // Background and Custom Colours
  wxColour bgCol;
  bgCol = bgDialogData.GetColour();
  config.setValue("Session/bgCol", QString::fromWCharArray(bgCol.GetAsString(wxC2S_HTML_SYNTAX).c_str()));
  for (int i = 0; i < 16; i++)
  {
    bgCol = bgDialogData.GetCustomColour(i);
    if (!bgCol.IsOk())  // skip undefined custom colours
      continue;
    config.setValue(QString("Session/bgCustCol%1").arg(i), QString::fromWCharArray(bgCol.GetAsString(wxC2S_HTML_SYNTAX).c_str()));
  }


  if (canvas)
  {
    // The canvas's own client size. (It used to add the character panel's width back, because
    // that panel came and went with character models; the Model panel does not.)
    int canvx = 0, canvy = 0;
    canvas->GetClientSize(&canvx, &canvy);

    config.setValue("Session/CanvasWidth", canvx);
    config.setValue("Session/CanvasHeight", canvy);

    config.setValue("Session/DBackground", canvas->drawBackground);

    if (canvas->drawBackground)
      config.setValue("Session/BackgroundImage", QString::fromWCharArray(bgImagePath.c_str()));
    else
      config.setValue("Session/BackgroundImage", "");

    // model file
    if (canvas->model())
      config.setValue("Session/Model", canvas->model()->name());
  }
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
      LOG_ERROR << "Could not load the layout.";
    }
    else
    {
      // No need to display these windows on startup
      interfaceManager.GetPane(modelControl).Show(false);
      interfaceManager.GetPane(settingsControl).Show(false);

      // The command bar is not something a saved layout can take away. Browse, Model and
      // Animation keep whatever shown state the layout saved: that is how the panels a user
      // closed stay closed next time.
      interfaceManager.GetPane(wxT("commandBar")).Show(true);
#ifndef  _LINUX // buggy
      interfaceManager.Update();
#endif
      LOG_INFO << "GUI Layout loaded from previous session.";
    }
  }

  // Restore saved canvas size:
  if (canvas)
  {
    int canvx = config.value("Session/CanvasWidth", 800).toInt();
    int canvy = config.value("Session/CanvasHeight", 600).toInt();
    SetCanvasSize(canvx, canvy);
  }
}

void ModelViewer::SaveLayout()
{
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  config.setValue("Session/Layout", QString::fromWCharArray(interfaceManager.SavePerspective().c_str()));
  config.setValue("Session/LayoutVersion", LAYOUT_VERSION);

  wxPoint pos = GetPosition();
  config.setValue("Session/PositionX", pos.x);
  config.setValue("Session/PositionY", pos.y);

  LOG_INFO << "GUI Layout was saved.";
}


void ModelViewer::LoadModel(GameFile * file)
{
  if (!canvas || !file)
    return;

  LOG_INFO << "Loading model:" << file->fullname();

  if (canvas->model() && canvas->model()->gamefile && (canvas->model()->gamefile->fullname() == file->fullname())) // don't reload same model
    return;

  isModel = true;

  // A direct model load is not an NPC; clear any NPC export descriptor. (LoadNPC calls us and
  // then re-sets it afterwards, so the NPC case is unaffected.) Same for the item skin: a raw
  // model load has no item skin, and LoadItem calls us then re-sets it afterwards.
  m_exportNpcId = -1;
  m_exportNpcDisplayId = 0;
  m_exportItemSkinFileId = 0;

  // check if this is a character model
  isChar = (file->fullname().startsWith("char", Qt::CaseInsensitive) || file->fullname().startsWith("alternate\\char", Qt::CaseInsensitive));
  Attachment *modelAtt = NULL;

  if (isChar)
  {
    modelAtt = canvas->LoadModel(file);
    // error check
    if (!modelAtt)
    {
      LOG_ERROR << "Failed to load the model" << file->fullname();
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
  }
  else
  {
    modelAtt = canvas->LoadModel(file); //  change it from LoadModel, don't sure it's right or not.

    // error check
    if (!modelAtt)
    {
      LOG_ERROR << "Failed to load the model" << file->fullname();
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

  viewMenu->Enable(ID_USE_CAMERA, canvas->model()->hasCamera);
  if (canvas->useCamera && !canvas->model()->hasCamera)
  {
    canvas->useCamera = false;
    viewMenu->Check(ID_USE_CAMERA, false);
  }

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
  // fell back to "geoset 0 only": parts the OpenGL viewport shows were missing in the Unity
  // one. Pushing here covers both cases; a model whose skin was already pushed simply gets the
  // same answer twice, which the player treats as the state it already has.
  SendCurrentSkinToUnity();

  // The centre of the window, if the Unity viewport can show this model. A no-op when the pane
  // is already where it should be: see UpdatePrimaryViewport. FIRST: when the routing does
  // change, its one relayout also commits the character panel shown or hidden above, and the
  // commit below then finds nothing to do -- one relayout per switch, not two.
  UpdatePrimaryViewport();

  // The Model panel, the command bar and the status bar follow the new model. After the
  // routing, so they know which viewport it is on.
  DisplayedContentChanged();

  // Lay out ONLY if a pane's shown state actually changed and the routing did not. An
  // unconditional Update() here erased and repainted the whole window, player included, on every
  // load: see CommitLayoutIfChanged.
  CommitLayoutIfChanged();
}

// Load an NPC model
void ModelViewer::LoadNPC(unsigned int modelid)
{
  // Described to the Unity viewport once, dressed, when this returns: see SceneHold.
  SceneHold sceneHold(this);

  canvas->clearAttachments();
  canvas->setModel(NULL);

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
    int extraId = r.values[0][4].toInt();
    // if npc is a simple one (no extra info CreatureDisplayInfoExtra)
    if (extraId == 0)
    {
      LoadModel(GAMEDIRECTORY.getFile(r.values[0][0].toInt()));
      WoWModel * m = const_cast<WoWModel *>(canvas->model());
      if (!m)
      {
        bailNPCUnavailable();
        return;
      }
      m->modelType = MT_NORMAL;
      animControl->SetSkinByDisplayID(r.values[0][5].toInt());
    }
    else
    {
      LoadModel(GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(r.values[0][0].toInt())));
      if (!canvas->model())
      {
        bailNPCUnavailable();
        return;
      }

      query = QString("SELECT Skin, Face, HairStyle, HairColor, FacialHair FROM CreatureDisplayInfoExtra WHERE ID = %1").arg(extraId);

      r = GAMEDATABASE.sqlQuery(query);

      if (r.valid && !r.empty())
      {
        g_charControl->model->cd.set(CharDetails::SKIN_COLOR, r.values[0][0].toInt());
        g_charControl->model->cd.set(CharDetails::FACE, r.values[0][1].toInt());
        g_charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, r.values[0][2].toInt());
        g_charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, r.values[0][3].toInt());
        g_charControl->model->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, r.values[0][4].toInt());
      }

      query = QString("SELECT ItemDisplayInfoID, ItemSlot FROM NpcModelItemSlotDisplayInfo WHERE NpcModelID = %1").arg(extraId);

      r = GAMEDATABASE.sqlQuery(query);

      if (r.valid && !r.empty())
      {
        static map<int, CharSlots> ItemTypeToInternal = { { 0, CS_HEAD }, { 1, CS_SHOULDER }, { 2, CS_SHIRT }, { 3, CS_CHEST }, { 4, CS_BELT }, { 5, CS_PANTS },
        { 6, CS_BOOTS }, { 7, CS_BRACERS }, { 8, CS_GLOVES }, { 9, CS_TABARD }, { 10, CS_CAPE } };
        for (uint i = 0; i < r.values.size(); i++)
        {
          WoWItem * item = g_charControl->model->getItem(ItemTypeToInternal[r.values[i][1].toInt()]);
          if (item)
            item->setDisplayId(r.values[i][0].toInt());
        }
      }

      g_charControl->model->cd.isNPC = true;
      g_charControl->RefreshModel();
      g_charControl->RefreshEquipment();
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
  canvas->clearAttachments();
  canvas->setModel(NULL);

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
      if (itemInfos.values[0][0] != "" && itemInfos.values[0][1] != "")
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
  if (wxMessageBox(_("Restart WoW Model Viewer now?\n\nThe current scene is reloaded fresh; your saved settings are kept."),
                   _("Restart"), wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION, this) != wxYES)
    return;

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
    wxExecute(wxString::Format(wxT("\"%s\""), exe), wxEXEC_ASYNC);

  // Tear down like File > Exit; OnClose saves the session as usual before the process exits.
  video.render = false;
  canvas->Disable();
  Close(false);
}

// This is called when the window is closing
void ModelViewer::OnClose(wxCloseEvent &event)
{
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

  video.render = false;

  // Tear down the export job manager (stops its poll timer; detaches any running children).
  if (m_exportJobManager) {
    delete m_exportJobManager;
    m_exportJobManager = nullptr;
  }
  if (m_imgSeqExporter) {
    delete m_imgSeqExporter;
    m_imgSeqExporter = nullptr;
  }

  // If we have a canvas (which we always should)
  // Stop rendering, give more power back to the CPU to close this sucker down!
  //if (canvas)
  //  canvas->timer.Stop();

  // Persist the GUI layout/session only for real interactive runs. A headless/CLI run
  // (FBX export child, screenshot/test harness) parks its window off-screen at (-32000,-32000)
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

  if (animExporter) {
    animExporter->Destroy();
    wxDELETE(animExporter);
  }

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

// Menu button press events
void ModelViewer::OnToggleDock(wxCommandEvent &event)
{
  int id = event.GetId();

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
  }
  else if (id == ID_SHOW_LIGHT) {
    interfaceManager.GetPane(lightControl).Show(true);
  }
  else if (id == ID_SHOW_MODEL) {
    interfaceManager.GetPane(modelControl).Show(true);
    modelControl->Update();
  }
  else if (id == ID_SHOW_SETTINGS) {
    interfaceManager.GetPane(settingsControl).Show(true);
    settingsControl->Open();
  }
  interfaceManager.Update();
}

// The Unity viewport's docking setup, shared by the lazy creation below and ResetLayout so
// both register the pane identically (an initially-hidden, dockable/floatable side pane).
// For now the OpenGL canvas stays the one CenterPane; as the Unity renderer becomes the
// primary viewport this pane is expected to take over that position, with the OpenGL
// canvas retained as the legacy/fallback viewport.
static wxAuiPaneInfo buildUnityRendererPaneInfo()
{
  return wxAuiPaneInfo().
         Name(wxT("unityRenderer")).Caption(wxT("Unity Renderer")).
         BestSize(wxSize(640, 480)).FloatingSize(wxSize(800, 600)).
         Right().Layer(2).Show(false).DestroyOnClose(false);
}

// View > Unity Renderer: the embedded Unity viewport -- the new renderer foundation for WMV
// (the OpenGL canvas is the legacy/fallback viewport during the migration; see
// docs/unity-renderer/README.md). At this stage it is still OPTIONAL: the host panel and its
// pane are created lazily on FIRST use (same pattern as the Screenshot pane) so normal
// startup does not construct them (headless runs only do so for the -unityipctest self-test),
// and the player itself is an external
// exe launched by UnityRendererHost -- nothing in the WMV build depends on Unity being
// installed.
void ModelViewer::OnUnityRenderer(wxCommandEvent &event)
{
  ShowUnityRenderer();
}

bool ModelViewer::ShowUnityRenderer(bool selfTest)
{
  bool justAdded = false;
  if (!unityRendererHost)
  {
    justAdded = true;
    unityRendererHost = new UnityRendererHost(this, ID_UNITY_FRAME);
    interfaceManager.AddPane(unityRendererHost, buildUnityRendererPaneInfo());
    // Runtime IPC: as soon as the player announces itself, tell it what is on the canvas.
    unityRendererHost->ipc()->onUnityReady = [this]() {
      if (unityRendererHost)
        unityRendererHost->setPlayerReady(true);
      // A (re)started player knows nothing of the states sent to the one before it; the model push
      // below carries the current state, and its build answers for it.
      if (modelInspector)
        modelInspector->UnityPlayerRestarted();
      m_sceneAwaitingRevision = 0;
      // ... nor could the one before it build a character this one never tried: a player rebuilt or
      // restarted after a failed build is given the character again.
      m_unityCharacterFailed = 0;
      SendCurrentModelToUnity();
      // What the player announced may change where the model belongs: a character goes back to the
      // canvas when the player is an older build that cannot dress it.
      UpdatePrimaryViewport();
    };
    unityRendererHost->ipc()->onCharacterSceneApplied = [this](const UnityIpcServer::SceneAck & ack) {
      OnCharacterSceneApplied(ack);
    };
    // ... and what it did with a geoset state, so the Geosets checkboxes follow the renderer.
    unityRendererHost->ipc()->onGeosetsApplied = [this](const UnityIpcServer::GeosetAck & ack) {
      if (modelInspector)
        modelInspector->OnUnityGeosetsApplied(ack);
    };
  }

  // Show the pane first so the panel is realized at its docked size, then embed the player
  // into it (the player parents itself to the panel's HWND). This runs on every load, so the
  // layout is committed only when the pane was not already shown: see CommitLayoutIfChanged.
  wxAuiPaneInfo & unityPane = interfaceManager.GetPane(unityRendererHost);
  const bool wasShown = unityPane.IsShown();
  unityPane.Show(true);
  // Only the change made HERE is committed: the pane appearing, or -- just added -- never laid
  // out at all (AddPane puts it in the manager's list, not in a dock, and its window was
  // created shown, so a shown-state test would see nothing to do and leave a bare 640 x 480
  // panel at the frame's origin). A caller about to re-dock the pane commits everything else
  // -- the character panel LoadModel showed or hid -- in that one relayout; committing it here
  // first made every routing switch relayout twice.
  if (justAdded || !wasShown)
    interfaceManager.Update();

  if (!unityRendererHost->isRunning() && !unityRendererHost->launch(!batchMode, selfTest))
  {
    // Launch failed (missing/broken player build): the host already told the user; hide the
    // empty pane again and carry on -- the rest of the app is unaffected.
    interfaceManager.GetPane(unityRendererHost).Show(false);
    interfaceManager.Update();
    return false;
  }
  return true;
}

// Start the Unity viewport before it is needed.
//
// It used to be created and launched by the first model load that wanted it, which meant the user
// picked a creature and then watched a game engine boot -- process start, engine init and the
// player's own splash -- with the model appearing only afterwards. None of that has anything to do
// with the model, so none of it belongs in front of one. Done here it happens while the user is
// still looking at an empty app, and by the time a creature is picked the player is already
// connected and waiting.
//
// Deliberately quiet: a missing player build is not worth a dialog at startup (the viewport is
// still optional), so this checks for the exe itself rather than letting launch() complain. The
// menu item still reports properly when the user asks for it explicitly.
// The half of the viewer-first startup that touches NO player and NO IPC: hide the panes and take
// the screen. Runs BEFORE the client is loaded, which is what makes the first thing on screen a
// clean fullscreen viewer instead of a small window behind a dialog.
void ModelViewer::ApplyViewerStartupLayout()
{
  if (batchMode || !unityPrimaryViewport || !canvas)
    return;

  // The panels come up as they were left: Browse, Model and Animation keep the shown state the
  // saved layout restored (LoadLayout), all three on a first run. The empty viewport says what to
  // do next itself (UnityRendererHost's empty state), so hiding the panels that do it is no
  // longer the way to make an empty application look tidy.

  // Take the screen. A viewer that opens in a small window in the corner is not one.
  EnterViewerFullScreen(true);
}

// Start the player, at launch, before any client is loaded.
//
// Nothing it does needs game data: it talks to WMV over the local IPC channel and is told what to
// show, so with nothing loaded it simply sits connected and idle on a dark viewport. That leaves
// its one-second start-up overlapping the time the user spends deciding what to open, rather than
// the moment they open it.
//
// Loading a client afterwards, with the player already running, is the normal case and was
// verified as such -- unityReady lands in the middle of CASC and database initialisation and the
// load completes untroubled. (The launch crash this branch had along the way was a double client
// load, and it reproduced with no player running at all.)
void ModelViewer::WarmStartUnityViewport()
{
  if (batchMode || !unityPrimaryViewport)
    return;
  if (!wxFileName::FileExists(UnityRendererHost::resolveUnityExePath()))
  {
    LOG_INFO << "Unity viewport not warm-started: no player build installed. The OpenGL canvas "
                "remains the main viewport.";
    return;
  }

  if (!ShowUnityRenderer())
    return;                       // already reported; the OpenGL canvas keeps the centre

  // Nothing is loaded yet: the viewport shows what to do first instead of an empty dark panel.
  unityRendererHost->setEmptyState(true);
  UpdateEmptyState();

  // Take the centre, so the viewport the user is going to use is the one they can see.
  interfaceManager.DetachPane(unityRendererHost);
  interfaceManager.AddPane(unityRendererHost, wxAuiPaneInfo().
                           Name(wxT("unityRenderer")).Caption(wxT("Unity Renderer")).
                           CenterPane().Show(true));
  interfaceManager.GetPane(canvas).Show(false);
  interfaceManager.Update();
}

// Borderless fullscreen, keeping the MENU BAR.
//
// Dropping the caption removes the window's own close and restore buttons, so without the menu
// there would be no visible way back out -- and no way to reach View > "Unity as main viewport"
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
  event.Skip();
}

// Every M2 the canvas shows, playable characters included: the character's resolved appearance,
// merged armour and attached items reach the Unity viewport as a characterScene (protocol 3). What
// stays on the OpenGL canvas:
//   - a WMO, which is not an M2 at all;
//   - a character riding a mount -- the canvas model is then the mount, with the character hung
//     from one of its attachments, and the Unity viewport has no mount rig;
//   - a character when the connected player is an older build that cannot dress one.
// A player that is not connected yet is assumed to be the current build: this decides the layout
// before the player exists, and a player that then turns out older sends the model back here when it
// announces itself (onUnityReady re-routes).
bool ModelViewer::unityCanShowCurrentModel() const
{
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return false;
  if (canvas->wmo)
    return false;
  if (!isChar)
    return true;
  if (!canvasShowsCharacter())
    return false;
  if (m_unityCharacterFailed != 0 && m_unityCharacterFailed == (int)canvas->model()->gamefile->fileDataId())
    return false;
  const bool playerKnown = unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isUnityReady();
  return !playerKnown || unityRendererHost->ipc()->playerDressesCharacters();
}

// The scene and the player's asset requests both address files by FileDataID, so a character with
// none (a legacy MPQ client) is not one the viewport can dress.
bool ModelViewer::canvasShowsCharacter() const
{
  return isChar && canvas && canvas->model() && canvas->model()->charModelDetails.isChar &&
         canvas->model()->gamefile && canvas->model()->gamefile->fileDataId() > 0;
}

bool ModelViewer::unityPlayerDressesCharacters() const
{
  return unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->playerDressesCharacters();
}

// Hand the centre of the window to whichever viewport should own it.
//
// This is a ROUTING change, not a renderer swap: the OpenGL canvas still loads the model, still
// owns the animation clock, and is still what every Send*ToUnity call reads from. It keeps
// running when it is not the visible one -- its timer is independent of whether the pane is
// shown, and it has already initialised by the time any model is loaded -- which matters,
// because the playback state the Unity viewport mirrors is driven from ModelCanvas::tick.
//
// So the OpenGL viewport is never torn down, only uncovered: View > "Unity as main viewport"
// hands the centre straight back to it for comparison, and unsupported models never leave it.
// THE WHOLE WINDOW BLINKED ON EVERY MODEL LOAD, and this is why. On Windows, wxAuiManager::Update()
// wraps its relayout in a wxWindowUpdateLocker on the frame (wx 3.2.10, framemanager.cpp: "only
// under MSW and only when not using live resizing" -- which this manager does not use). The lock
// is Freeze/Thaw, and wxWindowMSW::DoThaw is SendSetRedraw(true) followed by Refresh(), which is
// RedrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE): every window in the frame is
// invalidated and ERASED, the embedded player's child window included, and everything repaints.
// The load path called Update() two or three times per model -- after the animation control,
// from FileControl::UpdateInterface, from ShowUnityRenderer -- with nothing to lay out: the same
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
    interfaceManager.Update();
  return changed;
}

void ModelViewer::UpdatePrimaryViewport()
{
  // The routing is decided here for what the canvas shows now, so this is what the tick compares
  // against to notice a mount or a dismount (SendCharacterSceneToUnity). Recorded before any early
  // return: a load routes itself, and the tick that follows it must not route it again.
  m_lastShowsCharacter = canvasShowsCharacter();
  if (!canvas)
    return;

  // A non-interactive run keeps the viewport it was given. -mo screenshots and the OpenGL
  // regression render through the canvas and must not have a player process appear underneath
  // them; -unityipctest opens the Unity viewport explicitly and is unaffected by this.
  if (batchMode)
    return;

  const bool wantUnity = unityPrimaryViewport && unityCanShowCurrentModel();

  if (wantUnity)
  {
    // Launching can fail (no player build, or a broken one). ShowUnityRenderer has already told
    // the user why; the centre simply stays where it is rather than going blank.
    //
    // A player that is already running does not need ShowUnityRenderer at all -- it is re-docked
    // just below -- and skipping it spares the relayout that would first show its pane at the
    // side (the pane is hidden while an OpenGL-only model is up: see UncoverOpenGLViewport).
    const bool running = unityRendererHost && unityRendererHost->isRunning();
    if (!running && !ShowUnityRenderer())
    {
      LOG_INFO << "Unity viewport unavailable -- the OpenGL canvas remains the main viewport.";
      UncoverOpenGLViewport();
      return;
    }

    // ALREADY THERE: do nothing. This runs on every model load, and detaching and re-adding a
    // pane that is already the centre pane makes the AUI manager relayout everything, which
    // resizes the embedded player window -- the swapchain is recreated and the viewport blinks
    // on every switch. The layout only has to change when the ROUTING changes.
    wxAuiPaneInfo & unityPane = interfaceManager.GetPane(unityRendererHost);
    const bool alreadyCentre = unityPane.IsOk() && unityPane.IsShown() &&
                               unityPane.dock_direction == wxAUI_DOCK_CENTER &&
                               !interfaceManager.GetPane(canvas).IsShown();
    if (alreadyCentre)
      return;

    interfaceManager.DetachPane(unityRendererHost);
    interfaceManager.AddPane(unityRendererHost, wxAuiPaneInfo().
                             Name(wxT("unityRenderer")).Caption(wxT("Unity Renderer")).
                             CenterPane().Show(true));
    // The canvas is hidden, NOT stopped: see the note above.
    interfaceManager.GetPane(canvas).Show(false);
    interfaceManager.Update();
    return;
  }

  UncoverOpenGLViewport();
}

// Put the OpenGL canvas back in the centre, and the Unity pane back to being a side pane.
void ModelViewer::UncoverOpenGLViewport()
{
  // ALREADY THERE: do nothing. This runs on every load the Unity viewport cannot show (a
  // character model, or Unity not the main viewport), and detaching and re-adding the Unity pane
  // as a side pane, then relaying out, blinked the whole window each time exactly as the centre
  // re-dock used to: see CommitLayoutIfChanged for what an Update() costs. The layout only has
  // to change when the ROUTING changes.
  {
    const bool canvasShown = interfaceManager.GetPane(canvas).IsShown();
    bool unityAside = true;
    if (unityRendererHost)
    {
      wxAuiPaneInfo & up = interfaceManager.GetPane(unityRendererHost);
      unityAside = up.IsOk() && up.dock_direction != wxAUI_DOCK_CENTER &&
                   up.IsShown() == unityAsidePaneShown();
    }
    if (canvasShown && unityAside)
      return;
  }

  interfaceManager.GetPane(canvas).Show(true);
  if (unityRendererHost)
  {
    const bool showAside = unityAsidePaneShown();
    interfaceManager.DetachPane(unityRendererHost);
    interfaceManager.AddPane(unityRendererHost, buildUnityRendererPaneInfo().Show(showAside));
  }
  interfaceManager.Update();
}

// Whether the Unity pane, once it is not the centre, is shown at the side.
//
// When Unity is the main viewport, a model it cannot show (a character, a WMO) moves the canvas to
// the centre -- and the side pane used to stay up showing the PREVIOUS model, frozen, taking a
// third of a small window. It is hidden instead; the player keeps running and takes the centre
// back on the next model it can show. With the main-viewport option off, the side pane is the
// optional comparison view it always was, shown while the player runs.
bool ModelViewer::unityAsidePaneShown()
{
  return unityRendererHost && unityRendererHost->isRunning() && !unityPrimaryViewport;
}

void ModelViewer::OnUnityPrimaryViewport(wxCommandEvent & event)
{
  unityPrimaryViewport = event.IsChecked();
  UpdatePrimaryViewport();
}

// Just the load. LoadModel sends this before the animation control initialises, so that the
// selection and state the control then pushes are about a model the player already expects.
void ModelViewer::SendLoadToUnity()
{
  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return;
  if (!canvas || !canvas->model() || !canvas->model()->gamefile)
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  GameFile * gf = m->gamefile;
  const bool character = canvasShowsCharacter() && unityRendererHost->ipc()->playerDressesCharacters();
  const int fileDataID = gf->fileDataId() > 0 ? (int)gf->fileDataId() : 0;
  // Answers about any earlier load are told apart from answers about this one by this number.
  const int load = ++m_unityLoadSerial;
  unityRendererHost->ipc()->sendLoadWoWModel(gf->fullname(), fileDataID, QStringLiteral("active"), character, load);
  m_unityLoadedFileDataID = fileDataID;
  m_unityLoadedCharacter = character;
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
  // (Not for a character: its scene carries every texture and geoset, and follows on the next tick.)
  if (unityRendererHost && unityRendererHost->ipc() && unityRendererHost->ipc()->isConnected() &&
      canvas && canvas->model() && canvas->model()->gamefile && !canvasShowsCharacter())
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
  // host's own refresh keeps current; a skin push would only repeat part of it.
  if (canvasShowsCharacter())
    return;
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  unityRendererHost->ipc()->sendModelSkin((int)m->gamefile->fileDataId());
}

void ModelViewer::SendCharacterSceneToUnity(bool force)
{
  // Mounting puts the mount on the canvas and dismounting takes it off again, with no load: the
  // viewport routing follows here, where every tick passes. Only a change the routing has not already
  // followed counts -- UpdatePrimaryViewport records what it routed -- so a load is not routed twice.
  const bool showsCharacter = canvasShowsCharacter();
  if (!force && showsCharacter != m_lastShowsCharacter)
  {
    m_lastShowsCharacter = showsCharacter;
    // From here a player is re-docked, never launched: a launch that fails shows a message box, and a
    // modal loop opened inside the canvas timer keeps running the tick beneath it. Handing the centre
    // back to the canvas (mounting) launches nothing and always follows.
    const bool running = unityRendererHost && unityRendererHost->isRunning();
    if (isChar && (running || !showsCharacter))
    {
      // Dismounting. The player may not have THIS character loaded -- one that connected while the
      // character was mounted was sent the mount -- and it refuses a scene for a model it is not
      // building, so the character's load goes out again before the viewport is handed back.
      if (showsCharacter && unityPlayerReady())
      {
        const bool character = unityRendererHost->ipc()->playerDressesCharacters();
        if (m_unityLoadedFileDataID != (int)canvas->model()->gamefile->fileDataId() ||
            m_unityLoadedCharacter != character)
          SendCurrentModelToUnity();
      }
      UpdatePrimaryViewport();
    }
  }

  if (!unityRendererHost || !unityRendererHost->ipc() || !unityRendererHost->ipc()->playerDressesCharacters())
    return;
  if (!showsCharacter || m_sceneHold > 0)
    return;
  // The player reported it could not build this character from the load on display, and the canvas has
  // it: another scene would only be refused again, with the composited body image sent for nothing. A
  // later load of the same body model is another attempt, whose build waits for its scene -- held back,
  // that load would never finish, never answer, and the character would stay on the canvas for good.
  if (m_unityCharacterFailed != 0 && m_unityCharacterFailed == (int)canvas->model()->gamefile->fileDataId() &&
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

  WoWModel * m = const_cast<WoWModel *>(canvas->model());
  const quint64 signature = UnityCharacterScene::signature(m);
  if (!force && signature == m_lastSceneSignature)
    return;
  m_lastSceneSignature = signature;

  QElapsedTimer clock;
  clock.start();
  UnityIpcServer * ipc = unityRendererHost->ipc();
  UnityCharacterScene::Summary summary;
  const QJsonObject scene = UnityCharacterScene::build(
    m, [ipc](const QString & kind, const QImage & image) { return ipc->shareCharacterImage(kind, image); },
    summary);
  const int revision = ++m_sceneRevision;
  if (ipc->sendCharacterScene((int)m->gamefile->fileDataId(), revision, scene))
  {
    m_sceneAwaitingRevision = revision;
    m_sceneSentAt = now;
  }
  if (m_sceneAwaitingRevision == revision)
    LOG_INFO << "[unity-character] scene revision" << revision << "for" << m->gamefile->fullname() << ":"
             << summary.bodyTextures << "body texture(s)," << summary.images << "composited image reference(s),"
             << summary.merged << "merged," << summary.attachments << "attached; built and queued in"
             << clock.elapsed() << "ms";
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
  if (!canvas || !canvas->model() || !canvas->model()->gamefile || !unityCanShowCurrentModel())
    return 0;
  // A character's geosets -- its own, its merged parts' and its items' -- reach the player in its
  // scene, which the signature sends on the next tick. One channel: a modelGeosets for the body
  // would race the scene that also carries the body's flags.
  if (canvasShowsCharacter())
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
  // character that builds fine kept on the canvas. Load 0 is a scene the player could tie to no load at
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

  const bool current = canvasShowsCharacter() && (int)canvas->model()->gamefile->fileDataId() == ack.fileDataID;
  if (current && ack.status == "applied" && m_unityCharacterFailed == ack.fileDataID)
  {
    // A later load of the model whose build failed (another NPC on the same body, say) was dressed, so
    // the viewport takes the character back.
    m_unityCharacterFailed = 0;
    UpdatePrimaryViewport();
  }
  if (current && ack.status == "rejected")
  {
    if (ack.reason.startsWith("load failed"))
    {
      // The player could not build this character at all and is still showing whatever it showed
      // before. The canvas takes it back until something else is loaded.
      LOG_ERROR << "[unity-character] the Unity viewport could not build" << canvas->model()->gamefile->fullname()
                << "(" << ack.reason << ") -- showing it on the OpenGL canvas";
      m_unityCharacterFailed = ack.fileDataID;
      m_unityCharacterFailedLoad = ack.load;
      UpdatePrimaryViewport();
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
  if (!m->animManager || m->anims.empty())
    return;

  const int index = (int)m->animManager->GetAnim();
  if (index < 0 || index >= (int)m->anims.size())
    return;
  unityRendererHost->ipc()->sendModelAnimation((int)m->gamefile->fileDataId(), index,
                                               m->anims[index].animID, (int)m->anims[index].length,
                                               true);
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
  const int timeMs = (int)m->animManager->GetFrame();

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


    case ID_SHOW_BOUNDS:
      if (canvas->model())
      {
        WoWModel * m = const_cast<WoWModel *>(canvas->model());
        m->showBounds = !m->showBounds;
      }
      break;


    case ID_USE_CAMERA:
      canvas->useCamera = event.IsChecked();
      break;

    case ID_DEFAULT_DOODADS:
      // if we have a model...
      if (canvas->wmo) {
        canvas->wmo->includeDefaultDoodads = event.IsChecked();
        canvas->wmo->updateModels();
      }
      animControl->defaultDoodads = event.IsChecked();
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
      wxTextEntryDialog dialog(this, wxT("Please paste in the URL to the character you wish to import."), wxT("Please enter text"), armoryPath, wxOK | wxCANCEL | wxCENTRE, wxDefaultPosition);
      if (dialog.ShowModal() == wxID_OK){
        armoryPath = dialog.GetValue();
        ImportArmoury(armoryPath);
      }
    }
    break;

    /*
  case ID_ZOOM_IN:
  canvas->Zoom(0.5f, false);
  break;

  case ID_ZOOM_OUT:
  canvas->Zoom(-0.5f, false);
  break;
  */
    case ID_OPENGL_DEBUG:
      canvas->toggleOpenGLDebug();
      break;   // (fell through into "save view to slot 1")

    case ID_SAVE_TEMP1:
      canvas->SaveSceneState(1);
      break;
    case ID_SAVE_TEMP2:
      canvas->SaveSceneState(2);
      break;
    case ID_SAVE_TEMP3:
      canvas->SaveSceneState(3);
      break;
    case ID_SAVE_TEMP4:
      canvas->SaveSceneState(4);
      break;
    case ID_LOAD_TEMP1:
      canvas->LoadSceneState(1);
      break;
    case ID_LOAD_TEMP2:
      canvas->LoadSceneState(2);
      break;
    case ID_LOAD_TEMP3:
      canvas->LoadSceneState(3);
      break;
    case ID_LOAD_TEMP4:
      canvas->LoadSceneState(4);
      break;
  }
}

void ModelViewer::OnLightMenu(wxCommandEvent &event)
{
  int id = event.GetId();

  switch (id) {
    case ID_LT_SAVE:
    {
      wxFileDialog dialog(this, wxT("Save Lighting"), wxEmptyString, wxEmptyString, wxT("Scene Lighting (*.lit)|*.lit"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      if (dialog.ShowModal() == wxID_OK) {
        wxString fn = dialog.GetPath();

        // FIXME: ofstream is not compatible with multibyte path name
        std::ofstream f(fn.fn_str(), ios_base::out | ios_base::trunc);

        f << lightMenu->IsChecked(ID_LT_DIRECTION) << " " << lightMenu->IsChecked(ID_LT_TRUE) << " " << lightMenu->IsChecked(ID_LT_DIRECTIONAL) << " " << lightMenu->IsChecked(ID_LT_AMBIENT) << " " << lightMenu->IsChecked(ID_LT_MODEL) << endl;
        for (size_t i = 0; i < MAX_LIGHTS; i++) {
          f << lightControl->lights[i].ambience.x << " " << lightControl->lights[i].ambience.y << " " << lightControl->lights[i].ambience.z << " " << lightControl->lights[i].arc << " " << lightControl->lights[i].constant_int << " " << lightControl->lights[i].diffuse.x << " " << lightControl->lights[i].diffuse.y << " " << lightControl->lights[i].diffuse.z << " " << lightControl->lights[i].enabled << " " << lightControl->lights[i].linear_int << " " << lightControl->lights[i].pos.x << " " << lightControl->lights[i].pos.y << " " << lightControl->lights[i].pos.z << " " << lightControl->lights[i].quadradic_int << " " << lightControl->lights[i].relative << " " << lightControl->lights[i].specular.x << " " << lightControl->lights[i].specular.y << " " << lightControl->lights[i].specular.z << " " << lightControl->lights[i].target.x << " " << lightControl->lights[i].target.y << " " << lightControl->lights[i].target.z << " " << lightControl->lights[i].type << endl;
        }
        f.close();
      }

      return;

    }
    case ID_LT_LOAD:
    {
      wxFileDialog dialog(this, wxT("Load Lighting"), wxEmptyString, wxEmptyString, wxT("Scene Lighting (*.lit)|*.lit"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

      if (dialog.ShowModal() == wxID_OK) {
        wxString fn = dialog.GetFilename();
        // FIXME: ifstream is not compitable with multibyte path name
        ifstream f(fn.fn_str());

        bool lightObj, lightTrue, lightDir, lightAmb, lightModel;

        //lightMenu->IsChecked(ID_LT_AMBIENT)
        f >> lightObj >> lightTrue >> lightDir >> lightAmb >> lightModel;

        lightMenu->Check(ID_LT_DIRECTION, lightObj);
        lightMenu->Check(ID_LT_TRUE, lightTrue);
        lightMenu->Check(ID_LT_DIRECTIONAL, lightDir);
        lightMenu->Check(ID_LT_AMBIENT, lightAmb);
        lightMenu->Check(ID_LT_MODEL, lightModel);

        for (size_t i = 0; i < MAX_LIGHTS; i++) {
          f >> lightControl->lights[i].ambience.x >> lightControl->lights[i].ambience.y >> lightControl->lights[i].ambience.z >> lightControl->lights[i].arc >> lightControl->lights[i].constant_int >> lightControl->lights[i].diffuse.x >> lightControl->lights[i].diffuse.y >> lightControl->lights[i].diffuse.z >> lightControl->lights[i].enabled >> lightControl->lights[i].linear_int >> lightControl->lights[i].pos.x >> lightControl->lights[i].pos.y >> lightControl->lights[i].pos.z >> lightControl->lights[i].quadradic_int >> lightControl->lights[i].relative >> lightControl->lights[i].specular.x >> lightControl->lights[i].specular.y >> lightControl->lights[i].specular.z >> lightControl->lights[i].target.x >> lightControl->lights[i].target.y >> lightControl->lights[i].target.z >> lightControl->lights[i].type;
        }
        f.close();

        if (lightObj)
          canvas->drawLightDir = true;

        if (lightDir) {
          canvas->lightType = LIGHT_DYNAMIC; //LT_DIRECTIONAL;

          /*
          if (lightTrue) {
          if (event.IsChecked()){
          // Need to reset all our colour, lighting, material back to 'default'
          //GLfloat b[] = {0.5f, 0.4f, 0.4f, 1.0f};
          //glColor4fv(b);
          glDisable(GL_COLOR_MATERIAL);

          glMaterialfv(GL_FRONT, GL_EMISSION, def_emission);

          glMaterialfv(GL_FRONT, GL_AMBIENT, def_ambience);
          //glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);

          glMaterialfv(GL_FRONT, GL_DIFFUSE, def_diffuse);
          glMaterialfv(GL_FRONT, GL_SPECULAR, def_specular);
          } else {
          glEnable(GL_COLOR_MATERIAL);
          }
          }
          */
        }
        else if (lightAmb) {
          //glEnable(GL_COLOR_MATERIAL);
          canvas->lightType = LIGHT_AMBIENT;
        }
        else if (lightModel) {
          canvas->lightType = LIGHT_MODEL_ONLY;
        }

        lightControl->UpdateGL();
        lightControl->Update();
      }

      return;
    }
    /* case ID_USE_LIGHTS:
      canvas->useLights = event.IsChecked();
      return;
      */
    case ID_LT_DIRECTION:
      canvas->drawLightDir = event.IsChecked();
      return;
    case ID_LT_TRUE:
      if (event.IsChecked()){
        // Need to reset all our colour, lighting, material back to 'default'
        //GLfloat b[] = {0.5f, 0.4f, 0.4f, 1.0f};
        //glColor4fv(b);
        glDisable(GL_COLOR_MATERIAL);

        glMaterialfv(GL_FRONT, GL_EMISSION, def_emission);
        glMaterialfv(GL_FRONT, GL_AMBIENT, def_ambience);
        //glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);

        glMaterialfv(GL_FRONT, GL_DIFFUSE, def_diffuse);
        glMaterialfv(GL_FRONT, GL_SPECULAR, def_specular);
      }
      else {
        glEnable(GL_COLOR_MATERIAL);
        //glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(glm::vec4(0.4f,0.4f,0.4f,1.0f)));
      }

      lightControl->Update();

      return;

      // Ambient lighting
    case ID_LT_AMBIENT:
      //glEnable(GL_COLOR_MATERIAL);
      canvas->lightType = LIGHT_AMBIENT;
      return;

      // Dynamic lighting
    case ID_LT_DIRECTIONAL:
      //glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);
      canvas->lightType = LIGHT_DYNAMIC;
      return;

      // Model's ambient lighting
    case ID_LT_MODEL:
      canvas->lightType = LIGHT_MODEL_ONLY;
      return;
  }
}

void ModelViewer::OnCamMenu(wxCommandEvent &event)
{
  canvas->OnCamMenu(event);
}

// Menu button press events
void ModelViewer::OnSetColor(wxCommandEvent &event)
{
  int id = event.GetId();
  if (id == ID_BG_COLOR) {
    canvas->vecBGColor = DoSetColor(canvas->vecBGColor);
    canvas->drawBackground = false;
    //} else if (id==ID_LT_COLOR) {
    //  canvas->ltColor = DoSetColor(canvas->ltColor);
  }
}

// Menu button press events
void ModelViewer::OnEffects(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id == ID_ENCHANTS)
    enchants->Display();
}

glm::vec3 ModelViewer::DoSetColor(const glm::vec3 &defColor)
{
  wxColour dcol(roundf(defColor.x*255.0f), roundf(defColor.y*255.0f), roundf(defColor.z*255.0f));

  // Modern Photoshop-style picker (replaces the outdated native Win32 wxColourDialog).
  ColorPickerDialog dialog(this, dcol);
  if (dialog.ShowModal() == wxID_OK)
  {
    wxColour col = dialog.GetColour();
    bgDialogData.SetColour(col); // keep the session "bgCol" persistence in sync
    return glm::vec3(col.Red() / 255.0f, col.Green() / 255.0f, col.Blue() / 255.0f);
  }
  return defColor;
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
// The load goes through the dialog's own commitDetectedSelection(), which is what settles the
// data path: reading dataPath() without it returns an empty string, and CASC given an empty game
// folder takes the application down with it. Nothing here second-guesses whether the load worked
// afterwards -- LoadWoW reports its own failures, and the flag that looks like a success signal
// (isWoWLoaded) is never actually assigned.
void ModelViewer::PromptAndLoadClient()
{
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

    gamePath = clientDlg.dataPath();
    core::GameConfig chosen = clientDlg.selectedConfig();
    LoadWoW(&chosen, clientDlg.selectedProfile(), true /* show loading progress */);
    return;
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
  if (progress)
    progress->step(_("Updating encryption keys..."), 6);

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

int ModelViewer::LoadWoWFromMpq(const QString & dataFolder, const QString & locale)
{
  UnityAssetAccess::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
  fileControl->Disable();

  // Always install a FRESH folder for the legacy client -- never reuse an already-loaded Retail
  // (or previous MPQ) folder. Reusing the Retail folder would mix CASC + MPQ entries in one tree
  // and leave its CASC storage pointing at the wrong path. Game::init replaces the previous folder.
  core::Game::instance().init(new wow::WoWFolder(dataFolder), new wow::WoWDatabase());

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
  CallAfter([this]() { UpdateEmptyState(); });

  SetStatusText(wxString(GAMEDIRECTORY.version().toStdWString()), 1);
  SetStatusText(wxT("Legacy MPQ"), 2);
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
  PromptAndLoadLegacyMpqClient();
}

void ModelViewer::LoadWoW(const core::GameConfig * chosenConfig, const QString & profileOverride, bool showProgress)
{
  UnityAssetAccess::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
  fileControl->Disable();
  if (gamePath.IsEmpty() || !wxDirExists(gamePath)) {
    getGamePath();
  }

  // Create a fresh CASC folder on first load. Also recreate it when switching back from a legacy
  // MPQ client -- otherwise the current folder is the MPQ folder (wrong path, MPQ provider), and
  // reusing it would fail. Retail->Retail reuse is unchanged (initDone && storage==CASC -> skip).
  if (!core::Game::instance().initDone()
      || GAMEDIRECTORY.clientProfile().storage == core::StorageType::MPQ)
    core::Game::instance().init(new wow::WoWFolder(QString::fromWCharArray(gamePath.c_str())), new wow::WoWDatabase());

  core::GameConfig config;

  // The startup Client Choice launcher resolves the product/locale itself and hands it in;
  // when it isn't used (headless/CLI loads), fall back to detecting + auto/prompt-picking.
  if (chosenConfig)
  {
    config = *chosenConfig;
    LOG_INFO << "Client Choice selected config:" << config.locale << config.product << config.version;
  }
  else
  {
  // init game config
  std::vector<core::GameConfig> configsFound = GAMEDIRECTORY.configsFound();

  if (configsFound.empty())
  {
    wxString message = wxString::Format(wxT("Fatal Error: Could not find any locale from your World of Warcraft folder"));
    wxMessageDialog *dial = new wxMessageDialog(NULL, message, wxT("World of Warcraft No locale found"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }

  config = configsFound[0];

  // Diagnostic override (env-only, nothing in the UI): WMV_FORCE_BUILD=<version> pins the load to
  // a specific build present in the install -- e.g. a PTR build that the retail-preferring
  // auto-pick would otherwise skip. Used to exercise a new client's table layouts.
  const QString forceBuild = qEnvironmentVariable("WMV_FORCE_BUILD");
  bool configForced = false;
  if (!forceBuild.isEmpty())
  {
    for (size_t i = 0; i < configsFound.size(); i++)
      if (configsFound[i].version == forceBuild)
      {
        config = configsFound[i];
        configForced = true;
        LOG_INFO << "WMV_FORCE_BUILD selected config:" << config.locale << config.product << config.version;
        break;
      }
    if (!configForced)
      LOG_WARNING << "WMV_FORCE_BUILD" << forceBuild << "not found among detected configs";
  }

  unsigned int nbConfigs = configsFound.size();

  if (!configForced && nbConfigs > 1)
  {
    // Decide whether we actually need to ask the user. If every config is for the
    // same locale (e.g. .build.info lists several builds of one install, like
    // 12.0.5 and 12.0.1 enUS), there is no real choice to make -- auto-pick the
    // newest build (preferring the retail "wow" product) and skip the prompt. This
    // avoids a locale dialog on every (now automatic) startup, and lets the
    // headless snapshot CLI load without blocking on a modal dialog.
    bool singleLocale = true;
    for (size_t i = 1; i < nbConfigs; i++)
      if (configsFound[i].locale != configsFound[0].locale)
      {
        singleLocale = false;
        break;
      }

    if (singleLocale)
    {
      // numeric, component-wise "is a newer than b" on dotted versions ("12.0.5.67823")
      auto isNewer = [](const QString & a, const QString & b) {
        const QStringList va = a.split('.');
        const QStringList vb = b.split('.');
        const int n = (va.size() > vb.size()) ? va.size() : vb.size();
        for (int i = 0; i < n; i++)
        {
          const long long na = (i < va.size()) ? va[i].toLongLong() : 0;
          const long long nb = (i < vb.size()) ? vb[i].toLongLong() : 0;
          if (na != nb)
            return na > nb;
        }
        return false;
      };

      size_t best = 0;
      for (size_t i = 1; i < nbConfigs; i++)
      {
        const bool bestIsRetail = (configsFound[best].product == "wow");
        const bool iIsRetail = (configsFound[i].product == "wow");
        if (iIsRetail != bestIsRetail)
        {
          if (iIsRetail)
            best = i;                       // prefer the retail "wow" product
        }
        else if (isNewer(configsFound[i].version, configsFound[best].version))
        {
          best = i;                         // otherwise prefer the newest build
        }
      }
      config = configsFound[best];
      LOG_INFO << "Auto-selected WoW config:" << config.locale << config.product << config.version;
    }
    else
    {
      wxString * availableConfigs = new wxString[nbConfigs];
      for (size_t i = 0; i < nbConfigs; i++)
      {
        QString label = configsFound[i].locale + " - " + configsFound[i].product;
        if (configsFound[i].version != "")
          label = label + " (" + configsFound[i].version + ")";
        availableConfigs[i] = wxString(label.toStdWString().c_str());
      }

      long id = wxGetSingleChoiceIndex(_("Please select a locale:"), _("Locale"), nbConfigs, availableConfigs);
      delete[] availableConfigs;
      if (id != -1)
        config = configsFound[id];
      else
        return;
    }
  }
  } // end else: auto-detect / prompt for the config

  // Startup progress window (Client Choice -> Load). Shown across the heavy, synchronous load
  // steps below; left null (and thus a no-op) for headless/CLI loads.
  LoadingDialog * progress = 0;
  if (showProgress)
  {
    progress = new LoadingDialog(this);
    progress->Show();
    progress->step(_("Opening game data..."), 10);
    LoadingDialog * pd = progress;
    GAMEDIRECTORY.setLoadProgressCallback([pd](float frac) {
      pd->step(_("Opening game data..."), 10 + (int)(frac * 34.0f)); // advance 10 -> 44 during file enumeration
    });
  }

  // Refresh the TACT keys before opening the storage -- setConfig() hands them to CASC, so a
  // newer key list lets it decrypt encrypted db2 sections + files for recently-added content.
  // The keys file is read from the working directory (see CASCFolder::addExtraEncryptionKeys).
  refreshTactKeys("extraEncryptionKeys.csv", progress);

  if (!GAMEDIRECTORY.setConfig(config))
  {
    GAMEDIRECTORY.setLoadProgressCallback(std::function<void(float)>());
    if (progress) progress->Destroy();
    wxString message = wxString::Format(wxT("Fatal Error: Could not load your World of Warcraft Data folder (error %d)."), GAMEDIRECTORY.lastError());
    wxMessageDialog *dial = new wxMessageDialog(NULL, message, wxT("World of Warcraft Not Found"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }
  GAMEDIRECTORY.setLoadProgressCallback(std::function<void(float)>()); // done enumerating

  // Remember which build we settled on so out-of-process FBX exports can pin their child to
  // the same game data (-build), instead of letting the child's auto-pick choose a different one.
  m_loadedBuild = config.version;

  LOG_INFO << "Major version:" << GAMEDIRECTORY.majorVersion();
  // check if we are loading a 9.x version of WoW
  if(~GAMEDIRECTORY.majorVersion() >= 9)
  {
    wxString message = wxString::Format(wxT("This version of WoW Model Viewer is intended to be used with WoW Shadowlands(9.x.x) or above only\n"
                                            "For older WoW versions support, please refer to this page to pick the right WoW Model Viewer version:\n"
                                            "https://download.wowmodelviewer.net"));
    if (progress) progress->Destroy();
    wxMessageDialog *dial = new wxMessageDialog(NULL, message, wxT("Wrong World of Warcraft version"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }

  // init game version
  SetStatusText(wxString(GAMEDIRECTORY.version().toStdWString()), 1);

  langName = GAMEDIRECTORY.locale().toStdWString();

  SetStatusText(wxString(GAMEDIRECTORY.locale().toStdWString()), 2);

  // Pick the data profile (schema directory). The Client Choice launcher can override it;
  // otherwise derive "games/wow/<major>.<minor>/" from the detected client version.
  QString baseConfigFolder;
  if (!profileOverride.isEmpty())
  {
    baseConfigFolder = "games/wow/" + profileOverride + "/";
  }
  else
  {
    QStringList ver = GAMEDIRECTORY.version().split('.');
    baseConfigFolder = "games/wow/" + ver[0] + "." + ver[1] + "/";

    // A client newer than the shipped schema (e.g. a PTR like 12.1 when only the 12.0 profile
    // ships) has no exact games/wow/<major>.<minor>/ folder, which would leave the database empty.
    // Fall back to the newest available profile for the same major version. The per-file layout
    // matching in WoWDatabase::refreshStructures then corrects any columns that moved in the newer
    // build, so a new patch works without shipping a dedicated profile folder for it.
    if (!QDir(baseConfigFolder).exists())
    {
      const QStringList profiles =
        QDir("games/wow").entryList(QStringList() << (ver[0] + ".*"), QDir::Dirs | QDir::NoDotAndDotDot);
      int bestMinor = -1;
      QString best;
      for (const QString & p : profiles)
      {
        const QStringList pp = p.split('.');
        bool ok = false;
        const int minor = (pp.size() >= 2) ? pp[1].toInt(&ok) : 0;
        if (ok && minor > bestMinor)
        {
          bestMinor = minor;
          best = p;
        }
      }
      if (!best.isEmpty())
      {
        LOG_INFO << "No data profile for build" << GAMEDIRECTORY.version()
                 << "- falling back to newest available profile" << best;
        baseConfigFolder = "games/wow/" + best + "/";
      }
      else
      {
        LOG_WARNING << "No data profile found for major version" << ver[0]
                    << "(expected games/wow/" << (ver[0] + ".x") << ") - database will be empty";
      }
    }
  }

  LOG_INFO << "Using following folder to read game info" << baseConfigFolder;
  core::Game::instance().setConfigFolder(baseConfigFolder);

  if (progress) progress->step(_("Loading file list..."), 45);
  // Hidden weekly refresh of the file list before it is parsed (no setting, falls back to the
  // on-disk copy on any failure). Advances the gauge 45 -> 60 while downloading.
  refreshCommunityListfile(core::Game::instance().configFolder() + "../../../listfile.csv", progress);
  if (progress)
  {
    LoadingDialog * pd = progress;
    GAMEDIRECTORY.setLoadProgressCallback([pd](float frac) {
      pd->step(_("Loading file list..."), 60 + (int)(frac * 12.0f)); // advance 60 -> 72 during the parse
    });
  }
  GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
  GAMEDIRECTORY.setLoadProgressCallback(std::function<void(float)>()); // clear

  if (!customDirectoryPath.IsEmpty())
    core::Game::instance().addCustomFiles(QString::fromWCharArray(customDirectoryPath.c_str()), customFilesConflictPolicy);

  // init database
  if (progress) progress->step(_("Opening database..."), 80);
  InitDatabase();
 
  /*
  // Error check
  if (!initDB)
  {
  wxMessageBox(wxT("Some DBC files could not be loaded.  These files are vital to being able to render models correctly.\nFile list has been disabled until you are able to correct this problem."), wxT("DBC Error"));
  fileControl->Disable();
  SetStatusText(wxT("Some DBC files could not be loaded."));
  }
  else
  {
  isWoWLoaded = true;
  SetStatusText(wxT("Initializing WoW Done."));
  fileMenu->Enable(ID_LOAD_WOW, false);
  }
  */
  //wxMessageBox(wxT("Database loading is not yet supported. Available functionalities are quite restricted in this alpha release."), wxT("No database support yet"));


  if (progress) progress->step(_("Building file list..."), 92);
  SetStatusText(wxT("Initializing File Control..."));
  fileControl->Init(this);

  if (charControl->Init() == false)
  {
    SetStatusText(wxT("Error Initializing the Character Controls."));
  };
  fileControl->Enable();
  // The empty viewport points at Browse now rather than at loading a client -- once the load has
  // returned, since the client does not count as active while it is still inside it.
  CallAfter([this]() { UpdateEmptyState(); });
  SetStatusText(wxT("File Control Initialized."));

  if (progress)
  {
    progress->step(_("Ready"), 100);
    progress->Destroy();
  }
}

void ModelViewer::OnCharToggle(wxCommandEvent &event)
{
  int ID = event.GetId();
  if (ID == ID_VIEW_NPC)
    charControl->selectNPC(UPDATE_NPC);
  if (ID == ID_VIEW_ITEM)
    charControl->selectItem(UPDATE_SINGLE_ITEM, -1);
  else if (isChar)
    charControl->OnCheck(event);
}

// Direct "Import NPC from URL" entry: open the Wowhead NPC import dialog and load the model
// straight away, so the user no longer has to go View -> View NPC -> Import URL -> Display.
void ModelViewer::OnImportNPCFromURL(wxCommandEvent &event)
{
  NPCimporterDialog * dlg = new NPCimporterDialog();
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

void ModelViewer::OnSave(wxCommandEvent &event)
{
  static wxFileName dir = cfgPath;

  if (!canvas || (!canvas->model() && !canvas->wmo))
    return;

  if (event.GetId() == ID_FILE_SCREENSHOT) {
    wxString tmp = wxT("screenshot_");
    tmp << ssCounter;
    wxFileDialog dialog(this, wxT("Save screenshot"), dir.GetPath(wxPATH_GET_VOLUME), tmp, wxT("Bitmap Images (*.bmp)|*.bmp|TGA Images (*.tga)|*.tga|JPEG Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetFilterIndex(imgFormat);

    if (dialog.ShowModal() == wxID_OK) {
      imgFormat = dialog.GetFilterIndex();
      tmp = dialog.GetPath();
      dialog.Show(false);
      canvas->Screenshot(tmp);
      dir.SetPath(tmp);
      ssCounter++;
    }

    //canvas->InitView();

  }
  else if (event.GetId() == ID_FILE_EXPORTGIF) {
    if (canvas->wmo)
      return;

    if (!canvas->model())
      return;

    if (!video.supportFBO && !video.supportPBO) {
      wxMessageBox(wxT("This function is currently disabled for video cards that don't\nsupport the FrameBufferObject or PixelBufferObject OpenGL extensions"), wxT("Error"));
      return;
    }

    wxFileDialog dialog(this, wxT("Save Animation"), dir.GetPath(wxPATH_GET_VOLUME), wxT("filename"), wxT("Animation"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, wxDefaultPosition);

    if (dialog.ShowModal() == wxID_OK) {
      // Save the folder location for next time
      dir.SetPath(dialog.GetPath());

      // Show our exporter window      
      animExporter->Init(dialog.GetPath());
      animExporter->Show(true);
    }

  }
  else if (event.GetId() == ID_FILE_EXPORTAVI) {
    if (canvas->wmo && !canvas->model())
      return;

    if (!video.supportFBO && !video.supportPBO) {
      wxMessageBox(wxT("This function is currently disabled for video cards that don't\nsupport the FrameBufferObject or PixelBufferObject OpenGL extensions"), wxT("Error"));
      return;
    }

    wxFileDialog dialog(this, wxT("Save AVI"), dir.GetPath(wxPATH_GET_VOLUME), wxT("animation.avi"), wxT("animation (*.avi)|*.avi"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, wxDefaultPosition);

    if (dialog.ShowModal() == wxID_OK) {
      animExporter->CreateAvi(dialog.GetPath());
    }

  }
  else if (event.GetId() == ID_FILE_SCREENSHOTCONFIG) {
    if (!imageControl) {
      imageControl = new ImageControl(this, ID_IMAGE_FRAME, canvas);

      interfaceManager.AddPane(imageControl, wxAuiPaneInfo().
                               Name(wxT("Screenshot")).Caption(wxT("Screenshot")).
                               FloatingSize(wxSize(295, 145)).Float().Fixed().
                               Dockable(false)); //.FloatingPosition(GetStartPosition())
    }

    imageControl->OnShow(&interfaceManager);
  }
}

void ModelViewer::OnBackground(wxCommandEvent &event)
{
  static wxFileName dir = cfgPath;

  int id = event.GetId();

  if (id == ID_BACKGROUND) {
    if (event.IsChecked()) {
      wxFileDialog dialog(this, wxT("Load Background"), dir.GetPath(wxPATH_GET_VOLUME), wxEmptyString, wxT("All (*.bmp;*.jpg;*.png;*.avi)|*.bmp;*.jpg;*.png;*.avi|Bitmap Images (*.bmp)|*.bmp|Jpeg Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png|AVI Video file(*.avi)|*.avi"));
      if (dialog.ShowModal() == wxID_OK) {
        canvas->LoadBackground(dialog.GetPath());
        dir.SetPath(dialog.GetPath());
        viewMenu->Check(ID_BACKGROUND, canvas->drawBackground);
      }
      else {
        viewMenu->Check(ID_BACKGROUND, false);
      }
    }
    else {
      canvas->drawBackground = false;
    }
  }
}

void ModelViewer::SaveChar(QString fn, bool equipmentOnly /*= false*/)
{
  QFile file(fn);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << fn;
    return;
  }

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();
  stream.writeStartElement("SavedCharacter");
  stream.writeAttribute("version", "2.0");
  // save model itself
  WoWModel * m = const_cast<WoWModel *>(canvas->model());
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

  file.close();
}

void ModelViewer::LoadChar(QString fn, bool equipmentOnly /* = false */)
{
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
    m_exportItemSkinFileId = 0;
  }

  if (!equipmentOnly)
  {
    // Clear the existing model
    if (isWMO)
    {
      //canvas->clearAttachments();
      wxDELETE(canvas->wmo);
      canvas->wmo = NULL;
    }
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
          LoadModel(GAMEDIRECTORY.getFile(modelname));
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
  // Rebuild the Model Control attachment list so a loaded character's helm/shoulders/weapon models
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

void ModelViewer::OnCanvasSize(wxCommandEvent &event)
{
  switch (event.GetId())
  {
    case ID_CANVASS120:  SetCanvasSize(120, 120);
      break;
    case ID_CANVASS512:  SetCanvasSize(512, 512);
      break;
    case ID_CANVASS1024:  SetCanvasSize(1024, 1024);
      break;
    case ID_CANVASF480:  SetCanvasSize(640, 480);
      break;
    case ID_CANVASF600:  SetCanvasSize(800, 600);
      break;
    case ID_CANVASF768:  SetCanvasSize(1024, 768);
      break;
    case ID_CANVASF864:  SetCanvasSize(1152, 864);
      break;
    case ID_CANVASF1200:  SetCanvasSize(1600, 1200);
      break;
    case ID_CANVASW480:  SetCanvasSize(864, 480);
      break;
    case ID_CANVASW720:  SetCanvasSize(1280, 720);
      break;
    case ID_CANVASW1080:  SetCanvasSize(1920, 1080);
      break;
    case ID_CANVASM768:  SetCanvasSize(1280, 768);
      break;
    case ID_CANVASM1200:  SetCanvasSize(1900, 1200);
      break;
  }
}

void ModelViewer::SetCanvasSize(uint32 sizex, uint32 sizey)
{
  if (canvas && sizex && sizey)
  {
    canvas->SetMinSize(wxSize(sizex, sizey));
    // Fit() needs to be called twice to ensure it resizes properly for small sizes.
    // (At 120x120 the menu will wrap and impinge on the canvas, so need to call Fit() again!)
    // It's clunky, but it's the only way I can think of to do it - Wain
    Fit();
    Fit();
  }
}

bool ModelViewer::isUnityViewportOnScreen()
{
  if (!unityRendererHost || !canvas)
    return false;
  wxAuiPaneInfo & up = interfaceManager.GetPane(unityRendererHost);
  return up.IsOk() && up.IsShown() && up.dock_direction == wxAUI_DOCK_CENTER &&
         !interfaceManager.GetPane(canvas).IsShown();
}

bool ModelViewer::isUnityViewportShowingModel()
{
  if (!unityRendererHost || !canvas || !unityRendererHost->ipc() || !unityRendererHost->ipc()->isConnected())
    return false;
  wxAuiPaneInfo & up = interfaceManager.GetPane(unityRendererHost);
  return up.IsOk() && up.IsShown() && unityCanShowCurrentModel();
}

// The size of the viewport the user is looking at, whichever renderer that is. Called by the
// canvas and the Unity host whenever either is resized.
void ModelViewer::UpdateCanvasStatus()
{
  if (!canvas || !GetStatusBar())
    return;
  const wxWindow * viewport = isUnityViewportOnScreen() ? (wxWindow *)unityRendererHost : (wxWindow *)canvas;
  const wxSize size = viewport->GetClientSize();
  SetStatusText(wxString::Format(wxT("Viewport %i \u00D7 %i"), size.x, size.y), 3);
}

// Facts about what is loaded, for the status bar: the model's name is already in the title bar
// and the command bar, so this says what it is made of instead. Counts that do not apply are
// left out rather than shown as zero.
void ModelViewer::UpdateStatusFacts()
{
  if (!GetStatusBar() || !canvas)
    return;

  wxArrayString parts;
  auto count = [&parts](size_t n, const wxString & one, const wxString & many) {
    if (n > 0)
      parts.Add(wxNumberFormatter::ToString((long)n, wxNumberFormatter::Style_WithThousandsSep) +
                wxT(" ") + (n == 1 ? one : many));
  };

  if (isWMO && canvas->wmo)
  {
    count(canvas->wmo->nGroups, _("group"), _("groups"));
    count(canvas->wmo->doodadsets.size(), _("doodad set"), _("doodad sets"));
  }
  else if (canvas->model())
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

void ModelViewer::UpdateEmptyState()
{
  if (!unityRendererHost)
    return;
  if (UnityAssetAccess::hasActiveClient())
    unityRendererHost->setEmptyStateText(_("No model loaded"),
                                         _("Choose a model in Browse, or search for one by name."),
                                         _("Browse models"));
  else
    unityRendererHost->setEmptyStateText(_("No model loaded"),
                                         _("Load a World of Warcraft client to browse its models."),
                                         _("Load World of Warcraft..."));
}

void ModelViewer::DisplayedContentChanged()
{
  if (!canvas)
    return;

  const bool loaded = canvas->model() || canvas->wmo || canvas->adt;
  if (unityRendererHost)
  {
    if (loaded)
      unityRendererHost->setEmptyState(false);
    else
      UpdateEmptyState();
  }

  if (commandModelLabel)
  {
    wxString path;
    if (isWMO && canvas->wmo)
      path = canvas->wmo->itemName().toStdWString();
    else if (isADT && canvas->adt)
      path = canvas->adt->name;
    else if (canvas->model())
      path = canvas->model()->gamefile ? canvas->model()->gamefile->fullname().toStdWString()
                                       : const_cast<WoWModel *>(canvas->model())->name().toStdWString();
    wxString name = path;
    name.Replace(wxT("/"), wxT("\\"));
    name = name.AfterLast('\\');
    commandModelLabel->SetLabel(name.IsEmpty() ? wxString(_("No model loaded")) : name);
    commandModelLabel->SetForegroundColour(name.IsEmpty() ? UiStyle::secondaryText()
                                                          : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    commandModelLabel->SetToolTip(path);
    commandModelLabel->Refresh();
  }

  if (modelInspector)
    modelInspector->ContentChanged();

  UpdateStatusFacts();
  UpdateCanvasStatus();
}

// Open model: without a client there is nothing to browse, so this is "load a client"; with one,
// it is "go to Browse", with the search box ready for typing.
void ModelViewer::OnCommandBar(wxCommandEvent & event)
{
  switch (event.GetId())
  {
    case ID_UI_OPEN_MODEL:
    {
      // A client load yields to the event loop (its progress dialog), so this can be clicked in
      // the middle of one; starting another there is the double load that brings the app down.
      if (UnityAssetAccess::isClientLoading())
        return;
      if (!UnityAssetAccess::hasActiveClient())
      {
        PromptAndLoadClient();
        UpdateEmptyState();
        if (!UnityAssetAccess::hasActiveClient())
          return;
      }
      wxAuiPaneInfo & browse = interfaceManager.GetPane(fileControl);
      if (!browse.IsShown())
      {
        browse.Show(true);
        interfaceManager.Update();
      }
      if (fileControl->txtContent)
        fileControl->txtContent->SetFocus();
      break;
    }

    // The same commands as View > Camera > "Reset to default" and File > "Save Screenshot".
    // Both act on the OpenGL canvas, so the buttons are only enabled while that is the viewport
    // on screen (OnUpdateCommandUI).
    case ID_UI_RESET_CAMERA:
    {
      wxCommandEvent reset(wxEVT_MENU, ID_CAM_RESET);
      OnCamMenu(reset);
      break;
    }
    case ID_UI_SCREENSHOT:
    {
      wxCommandEvent shot(wxEVT_MENU, ID_FILE_SCREENSHOT);
      OnSave(shot);
      break;
    }
  }
}

void ModelViewer::OnUpdateCommandUI(wxUpdateUIEvent & event)
{
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

    // Neither command reaches the Unity viewport (it frames models itself and has no screenshot
    // path), so neither pretends to while it is the one on screen.
    case ID_UI_RESET_CAMERA:
    {
      const bool openGL = canvas && !isUnityViewportOnScreen();
      event.Enable(openGL && canvas->model());
      if (commandBar)
        commandBar->SetToolShortHelp(ID_UI_RESET_CAMERA, openGL
          ? _("Reset the camera to frame the model")
          : _("Reset camera works in the OpenGL viewport; the Unity viewport frames each model itself"));
      break;
    }
    case ID_UI_SCREENSHOT:
    {
      const bool openGL = canvas && !isUnityViewportOnScreen();
      event.Enable(openGL && (canvas->model() || canvas->wmo));
      if (commandBar)
        commandBar->SetToolShortHelp(ID_UI_SCREENSHOT, openGL
          ? _("Save a screenshot (F12)")
          : _("Screenshots are taken from the OpenGL viewport, which is not the one on screen"));
      break;
    }
  }
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
  add(unity, _("Left drag"), _("Orbit around the model"));
  add(unity, _("Right drag"), _("Pan"));
  add(unity, _("Mouse wheel"), _("Zoom"));

  const wxString gl = _("OpenGL viewport");
  add(gl, _("Left drag"), _("Orbit around the model"));
  add(gl, _("Right drag"), _("Pan"));
  add(gl, _("Mouse wheel, middle drag"), _("Zoom"));
  add(gl, _("Shift + drag or wheel"), _("Finer movement"));
  add(gl, _("Numpad 4 / 6"), _("Rotate left / right"));
  add(gl, _("Numpad 8 / 2"), _("Rotate back / front"));
  add(gl, _("Numpad 7 / 9"), _("Raise / lower the view"));
  add(gl, _("Numpad 1 / 3"), _("Pan left / right"));
  add(gl, _("Numpad 5"), _("Reset the camera"));
  add(gl, _("1 \u2013 9, 0"), _("Animation speed 0.1\u00D7 \u2013 0.9\u00D7, 0 = normal"));

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
  if (m->modelType == MT_CHAR)
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

void ModelViewer::ImportArmoury(wxString strURL)
{
  // Described to the Unity viewport once, dressed, when this returns: see SceneHold.
  SceneHold sceneHold(this);

  CharInfos * result = NULL;

  QString url = strURL.utf8_str();
  LOG_INFO << "Importing character from the Armory:" << url;

  for (PluginManager::iterator it = PLUGINMANAGER.begin();
       it != PLUGINMANAGER.end();
       ++it)
  {
    const auto * plugin = dynamic_cast<ImporterPlugin *>(*it);
    if (plugin && plugin->acceptURL(url))
    {
      result = plugin->importChar(url);
    }
  }

  if (result)
  {
    if (!result->valid)
    {
      const wxString msg = result->errorMessage.empty()
        ? wxString(wxT("Improperly Formatted URL.\nMake sure the link points to a character page (e.g. https://worldofwarcraft.blizzard.com/en-gb/character/eu/realm/name)."))
        : wxString::FromUTF8(result->errorMessage.c_str());
      wxMessageBox(msg, wxT("Armory Import Failed"));
      delete result;
      return;
    }

    const auto sex = (result->gender == "Male") ? 0 : 1;

    LoadModel(GAMEDIRECTORY.getFile(RaceInfos::getFileIDForRaceSex(result->raceId, sex)));

    if (!g_canvas->model())
      return;

    if (result->hasTransmogGear == true)
    {
      LOG_INFO << "Transmogrified Gear was found. Switching items...";
      wxMessageBox(wxT("We found Transmogrified gear on your character. The items your character is wearing will be exchanged for the items they look like."), wxT("Transmog Notice"));
    }

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
    // Rebuild the Model Control attachment list so the imported helm/shoulders/weapon models are
    // selectable immediately (previously the list stayed empty until an item was re-equipped).
    if (canvas && canvas->root)
      modelControl->RefreshModel(canvas->root);

    delete result;
  }
  else
  {
    LOG_ERROR << "There were errors gathering the Armory page.";
    wxMessageBox(wxT("There was an error when gathering the Armory data.\nPlease try again later."), wxT("Armory Error"));

  }
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

        for (size_t I = 0; I < canvas->model()->anims.size(); I++)
        {
          wxString animName = animsMap[canvas->model()->anims[I].animID];
          animName << L" [";
          animName << I;
          animName << L"]";
          values.Add(animName);
          selection.Add(I);
        }

        AnimationExportChoiceDialog animChoiceDlg(this, L"", wxT("FBX Export Options"), values);
        animChoiceDlg.SetSelections(selection);
        if (animChoiceDlg.ShowModal() == wxID_CANCEL)
          return;

        optMesh = animChoiceDlg.exportMesh();
        optSkel = animChoiceDlg.exportSkeleton();
        optSkin = animChoiceDlg.exportSkinning();
        optAnim = animChoiceDlg.exportAnimations();

        // Clip selection only matters when animations are being exported.
        if (optAnim)
        {
          selection = animChoiceDlg.GetSelections();
          animsToExport.reserve(selection.GetCount());
          for (unsigned int I = 0; I < selection.GetCount(); I++)
            animsToExport.push_back(canvas->model()->anims[selection[I]].Index);
        }
      }

      // ---------- Out-of-process FBX export ----------
      if (isFbx && m_exportJobManager)
      {
        WoWModel * m = const_cast<WoWModel *>(canvas->model());

        // Build the descriptor a fresh process needs to reload exactly this asset.
        wxString assetArgs, assetLabel, tempCharPath;
        if (isChar)
        {
          // Serialise the live customisation + equipment to a temp .chr the child reloads.
          wxString base = wxFileName::CreateTempFileName(wxT("wmvexport"));
          if (!base.IsEmpty() && wxFileName::FileExists(base))
            wxRemoveFile(base);
          tempCharPath = base + wxT(".chr");
          SaveChar(QString::fromStdWString(tempCharPath.ToStdWstring()));
          assetArgs  = wxT("\"") + tempCharPath + wxT("\"");
          assetLabel = wxT("character");
        }
        else if (m_exportNpcId > 0)
        {
          assetArgs  = wxString::Format(wxT("-npc %d:%d"), m_exportNpcId, m_exportNpcDisplayId);
          assetLabel = wxString::Format(wxT("NPC %d"), m_exportNpcId);
        }
        else if (m && !m->modelname.empty())
        {
          wxString gp = wxString::FromUTF8(m->modelname.c_str());
          gp.Replace(wxT("\\"), wxT("/"));
          assetArgs  = wxT("-mo \"") + gp + wxT("\"");
          // Carry the on-screen item skin so the child re-binds it instead of the default one.
          if (m_exportItemSkinFileId > 0)
            assetArgs << wxString::Format(wxT(" -itemskin %d"), m_exportItemSkinFileId);
          assetLabel = gp;
        }

        if (!assetArgs.IsEmpty())
        {
          ExportJobManager::Request req;
          req.assetArgs    = assetArgs;
          req.assetLabel   = assetLabel;
          req.outPath      = outPath;
          req.build        = wxString(m_loadedBuild.toStdWString().c_str());
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

          m_exportJobManager->startExport(req);
          return; // async: the manager owns progress, completion, and cleanup from here
        }

        // Couldn't build a re-loadable descriptor (e.g. an unnamed model) -> fall through to
        // the in-process export below so the user still gets their file.
        if (!tempCharPath.IsEmpty() && wxFileName::FileExists(tempCharPath))
          wxRemoveFile(tempCharPath);
        LOG_WARNING << "[export] no asset descriptor for out-of-process FBX; exporting in-process.";
      }

      // ---------- In-process export (non-FBX, or FBX fallback) ----------
      plugin->setExportOptions(optMesh, optSkel, optSkin, optAnim);
      plugin->setAnimationsToExport(animsToExport);

      WoWModel * m = const_cast<WoWModel *>(canvas->model());
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

void ModelViewer::OnExportImageSequence(wxCommandEvent & WXUNUSED(event))
{
  if (!canvas || !canvas->model())
  {
    wxMessageBox(wxT("Load a model before exporting an image sequence."),
                 wxT("Export Image Sequence"), wxOK | wxICON_ERROR, this);
    return;
  }

  if (!m_imgSeqExporter)
    m_imgSeqExporter = new ImageSequenceExporter(this);

  if (m_imgSeqExporter->isRunning())
  {
    wxMessageBox(wxT("An image-sequence export is already running."),
                 wxT("Export Image Sequence"), wxOK | wxICON_INFORMATION, this);
    return;
  }

  ImageSequenceDialog dlg(this);
  if (dlg.ShowModal() != wxID_OK)
    return;

  ImageSequenceExporter::Settings s;
  if (dlg.getSettings(s))
    m_imgSeqExporter->start(s); // non-blocking: renders one frame per event-loop tick
}

void ModelViewer::OnStatusBarRefreshTimer(wxTimerEvent& event)
{
  SetStatusText(wxString::Format(wxT("Memory: %i Mo"), core::getMemoryUsed()), 4);
}

