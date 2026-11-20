#include "modelviewer.h"
#include <algorithm>
#include "ClientChoiceDialog.h"   // File > Load World of Warcraft opens it
#include "ClientInstallations.h"

#include "AnimationExportChoiceDialog.h"
#include "ArmoryImportDialog.h"
#include "itemselection.h"   // ChoiceDialog: the item chooser a viewer switch puts away
#include "TextureView.h"
#include "AnimManager.h"

#include <chrono>
#include <functional>

#include <wx/aboutdlg.h>
#include <wx/aui/auibar.h>
#include <wx/generic/aboutdlgg.h>
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
#include "ClientLoadProgress.h"
#include "LoadTimeline.h"
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
#include "ClientState.h"
#include "AppPaths.h"
#include "CascLib.h" // the error codes a CASC open reports
#include "ViewportHost.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "LightCatalog.h"
#include "LiquidCatalog.h"
#include "WorldMapCatalog.h"

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
#include <QTimer>
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
EVT_UPDATE_UI(ID_UI_MAPS, ModelViewer::OnUpdateCommandUI)
EVT_MENU(ID_UI_MAPS, ModelViewer::OnCommandBar)
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

// A/B RUNS IN ONE SESSION, and the gap filmed (docs/world-viewer/08-streaming.md 8.9). Read once.
//   WMV_SWITCH_TURNS=1      the command bar's switch in one turn, as before the split
//   WMV_SWITCH_LAYOUT=a     a split switch laid out in its first turn even with the player on screen (the F experiment)
//   WMV_SWITCH_LAYOUT=grow  ... only when the viewport keeps its top-left corner and does not shrink (layoutMovesViewport)
//   WMV_SWITCH_LAYOUT=b     always laid out with the repaint
//   WMV_SWITCH_HOLD_MS=n    each turn of a split switch armed n ms after the last (the gap long enough to see and film)
//   WMV_SWITCH_SIZE=wait    the repaint turn waits for the player's resize, as before (the look of T5/T6 compared)
static wxString viewerSwitchEnv(const wxChar * name)
{
  wxString value;
  wxGetEnv(name, &value);
  return value.Lower();
}
static bool viewerSwitchOneTurn() { static const bool v = viewerSwitchEnv(wxT("WMV_SWITCH_TURNS")) == wxT("1"); return v; }
static bool viewerSwitchSizeWaits() { static const bool v = viewerSwitchEnv(wxT("WMV_SWITCH_SIZE")) == wxT("wait"); return v; }
static const wxString & viewerSwitchLayoutRule() { static const wxString v = viewerSwitchEnv(wxT("WMV_SWITCH_LAYOUT")); return v; }
static long viewerSwitchHoldMs()
{
  static const long v = [] { long ms = 0; return viewerSwitchEnv(wxT("WMV_SWITCH_HOLD_MS")).ToLong(&ms) && ms > 0 ? ms : 0L; }();
  return v;
}

// THE VIEWER SWITCH'S GAP as the rest of the program meets it: a filter every event passes first (wxEvtHandler::
// ProcessEvent), installed with the frame for its whole life -- never removed during an event: RemoveFilter cuts the walk
// short (MountCard.cpp) -- and inert unless a switch is on its way.
class ModelViewer::ViewerSwitchFilter : public wxEventFilter
{
public:
  explicit ViewerSwitchFilter(ModelViewer * frame) : m_frame(frame) {}
  int FilterEvent(wxEvent & event) override
  {
    ModelViewer::ViewerSwitch * s = m_frame->m_switch.get();
    if (!s)
      return Event_Skip;
    const wxEventType type = event.GetEventType();
    // HELD UNTIL THE THAW: the frame's own drawing. Its paint clears and draws the panel manager's parts over its frozen
    // windows (a frozen window reads hidden to Windows: it is not clipped out), and the manager draws straight to the
    // screen from Update, a resize and a hover (a client DC; wxEVT_AUI_RENDER is offered to the frame first). A paint left
    // without a wxPaintDC is validated by Windows, its pixels untouched; the thaw invalidates the frame again. And the
    // erase Windows makes of a window of the frame as it is shown, frozen (a panel given back).
    if (s->drawHeld)
    {
      wxObject * object = event.GetEventObject();
      if (type == wxEVT_PAINT && object == m_frame)
        return Event_Processed;
      if (type == wxEVT_AUI_RENDER &&
          static_cast<wxAuiManagerEvent &>(event).GetManager() == &m_frame->interfaceManager)
        return Event_Processed;
      if (type == wxEVT_ERASE_BACKGROUND)
      {
        wxWindow * window = wxDynamicCast(object, wxWindow);
        return window && window != m_frame && wxGetTopLevelParent(window) == m_frame ? Event_Processed : Event_Skip;
      }
    }
    // WHAT THE USER DOES BETWEEN THE TURNS finds the switch done: finished first (its repaint), and the event goes on to
    // what is on screen then. (Not inside a turn of it: that turn ends as planned.)
    if (s->phase == ModelViewer::ViewerSwitch::Phase::Waiting)
      if (const char * why = userAction(event))
        m_frame->FinishViewerSwitch(why);   // (s is gone after this)
    return Event_Skip;
  }

private:
  const char * userAction(const wxEvent & event) const
  {
    const wxEventType t = event.GetEventType();
    if (t == wxEVT_LEFT_DOWN || t == wxEVT_RIGHT_DOWN || t == wxEVT_MIDDLE_DOWN || t == wxEVT_AUX1_DOWN ||
        t == wxEVT_AUX2_DOWN || t == wxEVT_LEFT_DCLICK || t == wxEVT_RIGHT_DCLICK || t == wxEVT_MIDDLE_DCLICK ||
        t == wxEVT_MOUSEWHEEL || t == wxEVT_CONTEXT_MENU)
      return "a press";
    if (t == wxEVT_CHAR_HOOK || t == wxEVT_KEY_DOWN)
      return "a key";
    if (t == wxEVT_MENU || t == wxEVT_MENU_OPEN)
      return "a command";
    if (t == wxEVT_CLOSE_WINDOW)
      return "a window closing";
    if ((t == wxEVT_SIZE || t == wxEVT_DPI_CHANGED) && event.GetEventObject() == m_frame)
      return "the window resized";
    return nullptr;
  }
  ModelViewer * m_frame;
};

ModelViewer::ModelViewer()
#ifdef _LINUX
// Transparency in interfaceManager crashes with Linux compositing
: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_VENETIAN_BLINDS_HINT)
#else
// wxWidgets 3.2's default flags. 3.3's default adds live resizing: a sash drag would resize the viewport's swapchain at
// every mouse move. (Without live resizing wxAuiManager::Update locks the frame, which on MSW freezes only the frame's
// windows -- wxTopLevelWindowMSW::DoFreeze does nothing -- and it draws its parts straight to the screen through a client
// DC: a viewer switch holds that drawing back itself, ViewerSwitchFilter.) (It also drops the docking hint's fade-in,
// which these keep.)
: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_TRANSPARENT_HINT | wxAUI_MGR_HINT_FADE)
#endif
{
  m_themeRecheck.SetOwner(this, ID_THEME_RECHECK_TIMER);
  // THE VIEWER SWITCH IN TURNS: its turns' timer (and the turns after a switch), the idle check that arms it, and the gap's
  // filter.
  m_switchTimer.Bind(wxEVT_TIMER, &ModelViewer::OnViewerSwitchTimer, this);
  Bind(wxEVT_IDLE, &ModelViewer::OnViewerSwitchIdle, this);
  m_switchFilter.reset(new ViewerSwitchFilter(this));
  wxEvtHandler::AddFilter(m_switchFilter.get());
  PLUGINMANAGER.init(AppPaths::pluginDir().toStdString());
  // Until a client loads, the map catalogue says there is none (mapCatalog() is never null); so do the liquid and light
  // catalogues.
  m_mapCatalog = wow::WorldMapCatalog::noClient("no client is loaded");
  m_liquidCatalog = wow::LiquidCatalog::noClient("no client is loaded");
  m_lightCatalog = wow::LightCatalog::noClient("no client is loaded");
  // our main class objects
  animControl = nullptr;
  canvas = NULL;
  charControl = NULL;
  enchants = NULL;
  lightControl = NULL;
  modelControl = NULL;
  modelInspector = NULL;
  settingsControl = NULL;
  viewportHost = NULL;
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
#ifdef __WXMSW__
    SetIcon(wxICON(IDI_ICON1));
#elif !defined(__WXOSX__)
    // (macOS takes the bundle's icon.)
    SetIcon(wxIcon(QString(AppPaths::resourceDir() + "/WMW-Midnight-26-256x256.png").toStdWString(), wxBITMAP_TYPE_PNG));
#endif
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

  g_modelViewer = this;
  // The Maps viewer's panel made now, hidden: made at the first entry into Maps, it took 27-32 ms of that switch's turn.
  fileControl->PrepareMaps();
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
  // No window of its own: a client load shows its progress on the chooser's loading page (m_loadProgress), which
  // learns here whether the database is built (the first time for a version of the game) or comes from the cache.
  ClientLoadProgress * const progress = m_loadProgress;
  bool building = false;
  GAMEDATABASE.setBuildProgressCallback([progress, building](float fraction) mutable {
    if (!progress)
      return;
    if (!building)
    {
      building = true;
      progress->stage(_("Building the game database..."), fraction);
      progress->detail(_("First time with this version of the game: this is done once, later loads reuse it."));
    }
    else
      progress->progress(fraction);
  });

  // Loading a client again fills these from its own database instead of adding to the lists the
  // previous load made. (The item list keeps the "None" entry its constructor puts first.)
  npcs.clear();
  items = ItemDatabase();

  core::LoadTimeline::instance().begin("game database");
  const bool databaseOk = GAMEDATABASE.initFromXML("database.xml");
  GAMEDATABASE.setBuildProgressCallback(std::function<void(float)>());
  core::LoadTimeline::instance().end("game database", databaseOk ? QString() : QString("failed"));
  if (progress)
    progress->stage(_("Preparing characters..."));
  if (!databaseOk)
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
  core::LoadTimeline::instance().begin("texture regions");
  CharTexture::initRegions();
  core::LoadTimeline::instance().end("texture regions");
  
  // init Race informations
  core::LoadTimeline::instance().begin("races");
  RaceInfos::init();
  core::LoadTimeline::instance().end("races", QString("%1 races").arg(RaceInfos::count()));
  
  LOG_INFO << "Initializing Databases...";
  SetStatusText(wxT("Initializing Databases..."));
  initDB = true;

  {
    core::LoadTimeline::Stage stage("npc list");
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
    core::LoadTimeline::Stage stage("item list");
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
  art->SetToolIcon(ID_UI_MAPS, UiIcon::Maps);
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

  // The viewer selector: four radio tools, the active one pressed (OnUpdateCommandUI keeps them on the
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
  commandBar->AddTool(ID_UI_MAPS, _("Maps"), wxNullBitmap,
                      _("The map viewer: the client's maps in Browse, a map's terrain tiles in the grid under them, "
                        "the tile picked in the viewport"), wxITEM_RADIO);
  commandBar->AddSeparator();
  // (No Reset camera: it acted on the archived OpenGL viewport; the Unity viewport frames each model itself. No
  // Fullscreen either: the viewer opens maximised, and View > Fullscreen (F11) is there for the borderless mode.)
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
  CreateViewport();
  interfaceManager.AddPane(viewportHost, viewportPaneInfo());

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
  FinishViewerSwitch("a layout reset");
  // Every panel goes back to where InitDocking put it. The Unity viewport stays the centre pane; the
  // archived canvas is not a pane and is not touched. In a viewer that puts panels away (Textures,
  // Buildings), the default layout is what they are given back to, and they are put away again until it goes.
  m_workspace = 0;
  m_workspaceRecord.clear();
  m_workspaceGivenBack.clear();
  m_workspaceLayout.Clear();
  interfaceManager.DetachPane(commandBar);
  interfaceManager.DetachPane(fileControl);
  interfaceManager.DetachPane(viewportHost);
  interfaceManager.DetachPane(animControl);
  interfaceManager.DetachPane(modelInspector);
  interfaceManager.DetachPane(modelControl);
  interfaceManager.DetachPane(settingsControl);

  interfaceManager.AddPane(commandBar, buildCommandBarPaneInfo(this));
  interfaceManager.AddPane(viewportHost, viewportPaneInfo());

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
  FinishViewerSwitch("a layout loaded");
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
      // Its corner on that display too: a position saved past the edge (a maximised window's -8,-8, which was
      // saved before SaveLayout kept the restore position) would leave the top of the title bar off the screen.
      const wxRect area = wxDisplay(d).GetClientArea();
      posx = std::max(posx, area.x);
      posy = std::max(posy, area.y);
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
      EnsureViewportCentre();
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

  // Where the window goes when it is restored. A maximised (or fullscreen) window's own corner lies past the screen's
  // edge (-8,-8 for a maximised one), which a restored window would take as its place: its restore position instead,
  // which Windows gives in workspace coordinates (moved to the screen's the way wx does for a minimised window).
  wxPoint pos = GetPosition();
  if (IsMaximized() || IsFullScreen())
  {
#ifdef __WXMSW__
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (::GetWindowPlacement((HWND)GetHWND(), &placement))
    {
      const wxDisplay display(this);
      const wxPoint offset = display.GetClientArea().GetPosition() - display.GetGeometry().GetPosition();
      pos = wxPoint(placement.rcNormalPosition.left + offset.x, placement.rcNormalPosition.top + offset.y);
    }
#endif
  }
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

  // Update the animations / skins: the control's choices are what the canvas draws.
  animControl->UpdateModel(m);

  // The Model panel, the command bar, the status bar and the viewport (the model, or a notice when
  // the viewport cannot draw it) follow the new model -- a character rebuilt on its other model generation
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
  // male or female its display names. The display's sex stays the body sex its items are chosen for (itemSexID).
  const int displaySex = sex;
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
  m->setBodySex(displaySex);

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
#ifdef __WXMSW__
  return ::IsWindowEnabled((HWND)GetHWND()) && !(loop && loop->IsYielding());
#else
  return IsEnabled() && !(loop && loop->IsYielding());
#endif
}

void ModelViewer::Relaunch()
{
  const wxString exe = wxStandardPaths::Get().GetExecutablePath();

  // Development build: a "_relaunch.bat" sits a few folders above the exe (next to _run.bat). It
  // waits for THIS instance to exit so its DLLs unlock, then redeploys the freshly-built DLLs and
  // launches a new instance -- so a rebuild is picked up and a new exe never runs against stale
  // DLLs. Only inside a CMake build tree (a CMakeCache.txt on the way up): an installed copy relaunches
  // the exe directly (DLLs are colocated, no redeploy needed) and never runs a script it happens to
  // find in a folder above it.
  wxString relaunchBat;
  wxFileName probe(exe);
  bool inBuildTree = false;
  for (int up = 0; up < 6 && probe.GetDirCount() > 0; ++up)
  {
    probe.RemoveLastDir();
    inBuildTree = inBuildTree || wxFileName(probe.GetPath(), wxT("CMakeCache.txt")).FileExists();
    const wxFileName cand(probe.GetPath(), wxT("_relaunch.bat"));
    if (inBuildTree && cand.FileExists()) { relaunchBat = cand.GetFullPath(); break; }
  }

  if (!relaunchBat.IsEmpty() && wxPlatformInfo::Get().GetOperatingSystemId() & wxOS_WINDOWS)
    wxExecute(wxString::Format(wxT("cmd /c \"\"%s\" %lu\""), relaunchBat, wxGetProcessId()), wxEXEC_ASYNC);
  else
  {
    // The new instance waits for this one to have exited (-waitpid, WowModelViewApp::OnInit) before it
    // reads Config.ini or opens the log: this one saves its layout and settings as it closes.
    wxString command = wxString::Format(wxT("\"%s\" -waitpid %lu"), exe, wxGetProcessId());
    for (int i = 1; i < wxTheApp->argc; i++)
      if (wxTheApp->argv[i] == wxT("-console"))
        command += wxT(" -console");
#ifdef __WXMSW__
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
#else
    if (wxExecute(command, wxEXEC_ASYNC) <= 0)
    {
      LOG_ERROR << "Restart: could not start" << QString::fromWCharArray(command.wc_str());
      wxMessageBox(_("The viewer could not be started again, so it stays open."), _("Restart"), wxOK | wxICON_ERROR, this);
      return;
    }
#endif
  }

  // Tear down like File > Exit; OnClose saves the session as usual before the process exits.
  video.render = false;
  canvas->Disable();
  Close(false);
}

// This is called when the window is closing
void ModelViewer::OnClose(wxCloseEvent &event)
{
  FinishViewerSwitch("closing");   // (repainted whole; SaveLayout then reads pane info the windows follow)
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
  // THE VIEWER SWITCH: none left on its way (OnClose finishes one; a frame deleted without being closed -- the harness's
  // end, a fatal exception -- drops it without its repaint), no later turn left, and the filter out of the program's list
  // (not inside its walk: the frame is deleted by idle-time deletion or the application's clean-up).
  m_switchTimer.Stop();
  AbortViewerSwitch();
  m_later.clear();
  if (m_switchFilter)
    wxEvtHandler::RemoveFilter(m_switchFilter.get());
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

  // wxAUI stuff (teardown, always run)
  interfaceManager.UnInit();

  if (!batchMode)
    SaveSession();

  // The texture view decodes in the background: stop it before the client and canvas go (the window
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

  if (viewportHost) {
    viewportHost->Destroy();
    viewportHost = NULL;
  }

  if (enchants) {
    enchants->Destroy();
    enchants = NULL;
  }
  // The windows parked (parkHidden) that are left go with the frame's base class, after its members.
  *m_parkAlive = false;
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
  LayoutNow();
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
    case ViewerMode::Maps:      return PaneAnimation | PaneModel | PaneAttachments;   // the tile grid is in Browse
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
  commitDocksNow();
}

void ModelViewer::commitDocksNow()
{
  // wxAUI caps only a dock it makes anew, and only in this layout: the cap goes back right after.
  double capX = 0.3, capY = 0.3;
  interfaceManager.GetDockSizeConstraint(&capX, &capY);
  interfaceManager.SetDockSizeConstraint(1.0, 1.0);
  interfaceManager.Update();
  interfaceManager.SetDockSizeConstraint(capX, capY);
}

// An unbatched layout made now: a viewer switch on its way is finished first -- laid out under it, the panel manager would
// lay the panels out while the old workspace is held on screen, and its repaint is the switch's.
void ModelViewer::LayoutNow()
{
  FinishViewerSwitch("a layout");
  interfaceManager.Update();
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
wxAuiPaneInfo ModelViewer::viewportPaneInfo() const
{
  return wxAuiPaneInfo().
         Name(wxT("viewport")).Caption(wxT("Viewport")).
         CenterPane().Show(true);
}

void ModelViewer::CreateViewport()
{
  if (viewportHost)
    return;
  viewportHost = new ViewportHost(this, ID_UNITY_FRAME);
  viewportHost->setBackdrop(m_viewportBackground);

  // The scene's view, in the host panel (it shows it whenever no notice or content covers it).
  if (canvas)
  {
    canvas->Reparent(viewportHost);
    viewportHost->setCanvas(canvas);
  }

  // A texture picked in Browse is shown in the viewport's place.
  textureView = new TextureView(viewportHost, this);
  viewportHost->setContent(textureView);
  textureView->onLookupResolved = [this](int fileDataId, const QString & indexName) {
    if (fileControl)
      fileControl->TextureLookupResolved(fileDataId, indexName);
  };
}

bool ModelViewer::EnsureViewportCentre()
{
  if (!viewportHost)
    return false;
  if (isViewportCentre())
    return false;
  interfaceManager.DetachPane(viewportHost);
  interfaceManager.AddPane(viewportHost, viewportPaneInfo());
  return true;
}

void ModelViewer::OnBackgroundColor(wxCommandEvent & WXUNUSED(event))
{
  if (!m_backgroundDialog)
  {
    m_backgroundDialog = new BackgroundColorDialog(this);
    if (viewportHost)
      m_backgroundDialog->placeBeside(viewportHost->GetScreenRect());
  }
  m_backgroundDialog->Show();
  m_backgroundDialog->Raise();
  m_backgroundDialog->opened();
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
  if (!shown.IsOk() || !viewportHost)
    return false;
  const bool changed = !viewportHost->backdrop().IsOk() || !ViewportBackground::sameColour(shown, viewportHost->backdrop());
  viewportHost->setBackdrop(shown);
  if (canvas)
    canvas->vecBGColor = glm::vec3(shown.Red() / 255.0f, shown.Green() / 255.0f, shown.Blue() / 255.0f);
  return changed;
}

// The half of the viewer-first startup that touches NO player and NO IPC: take the screen. Runs
// BEFORE the client is loaded, which is what makes the first thing on screen the whole viewer
// instead of a small window behind a dialog.
void ModelViewer::ApplyViewerStartupLayout()
{
  if (batchMode || !canvas)
    return;

  // The panels come up as they were left: Browse, Model and Animation keep the shown state the
  // saved layout restored (LoadLayout), all three on a first run. The empty viewport says what to
  // do next itself (the viewport's notice), so hiding the panels that do it is no longer the
  // way to make an empty application look tidy.

  // Take the screen, as a window: maximised, with its title bar, so it can be minimised, restored
  // and moved like any other (View > Fullscreen still gives the borderless mode).
  Maximize(true);

  // What the viewport shows before anything is loaded: the empty viewer's prompt.
  UpdateViewportState();
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

// Options > Settings, on its Export page: the texture export folder (a Quick export that cannot save asks
// for another one here).
void ModelViewer::ShowExportSettings()
{
  interfaceManager.GetPane(settingsControl).Show(true);
  settingsControl->Open();
  settingsControl->ShowExportPage();
  LayoutNow();
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
    textureView->exportRequested(event.ShiftDown() ? TextureExport::Format::Original : TextureExport::Format::Png);
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
bool ModelViewer::canDrawCurrentModel(ViewportNotice * notice) const
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
    if (!w->ok)
    {
      const wxString name = fileName(wxString(const_cast<WMO *>(w)->itemName().toStdWString()));
      out.title = _("World model cannot be read");
      out.detail = wxString::Format(_("%s could not be read as a world model (WMO) root, so there is nothing "
                                      "to show."), name);
      return false;
    }
    return true;
  }
  if (!canvas->model())
    return false;   // nothing loaded: the empty viewer, not a notice about content
  const WoWModel * m = canvas->model();
  if (!m->gamefile)
  {
    out.title = _("Model cannot be shown");
    out.detail = wxString::Format(_("%s has no game file to read."), wxString(const_cast<WoWModel *>(m)->name().toStdWString()));
    return false;
  }
  return true;
}

// THE WHOLE VIEWPORT DECISION, most fundamental first: a platform with no player, texture services
// that never started (nothing can be prepared for the player then), a player that is missing or has
// stopped, content the player cannot draw yet, and finally nothing loaded (the empty viewer's prompt).
// THE WHOLE VIEWPORT DECISION, most fundamental first: a renderer that could not start, the Maps viewer (not
// available yet), content that cannot be drawn, and finally nothing loaded (the empty viewer's prompt).
bool ModelViewer::viewportNotice(ViewportNotice & notice) const
{
  notice = ViewportNotice();
  if (canvas && !canvas->rendererError().IsEmpty())
  {
    notice.title = _("The viewport could not start");
    notice.detail = canvas->rendererError() + wxT("\n") +
                    _("WoW Model Viewer draws with Vulkan (MoltenVK on macOS). Update your graphics driver, then restart the viewer.");
    return true;
  }
  if (isMapsMode())
  {
    notice.title = _("Maps are not available yet");
    notice.detail = _("The world viewer is being rebuilt for the native viewport and returns in a later version. "
                      "Models, textures and buildings are available.");
    notice.actionLabel = _("Browse models");
    notice.actionId = ID_UI_MODELS;
    return true;
  }
  // The viewer's own kind of content only: a model in Models, a world model in Buildings. What the canvas holds
  // of the other kind stays loaded but is not shown -- the mode's empty viewer is.
  const bool buildings = m_viewerMode == ViewerMode::Buildings;
  if (canvasHoldsModesContent())
  {
    ViewportNotice content;
    if (canDrawCurrentModel(&content))
      return false;
    if (!content.title.IsEmpty())
    {
      notice = content;
      return true;
    }
  }
  // Nothing of the mode's kind loaded: the empty viewer's prompt, which depends on whether a client is loaded yet.
  const bool client = isWoWLoaded;
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

WoWModel * ModelViewer::equipmentOwner() const
{
  // A character: the canvas model when it is the character, the rider while it rides a mount.
  if (canvasShowsCharacter())
    return const_cast<WoWModel *>(canvas->model());
  if (canvasShowsMountedCharacter())
    return riderModel();
  if (isWMO || !canvas || !canvas->model() || !charControl)
    return nullptr;
  WoWModel * model = const_cast<WoWModel *>(canvas->model());
  // SetHandsOnly supplies equipment controls for exclusive NPCs. Require the live
  // canvas model and its own node, never a stale character or a rider's parent.
  if (model->charModelDetails.isChar || charControl->model != model ||
      !model->attachment || charControl->charAtt != model->attachment || !model->gamefile)
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

// What the viewport shows is decided here and nowhere else: the canvas, a notice in its place, or the texture
// view. Content that cannot be drawn stays loaded (the panels, exports and Info keep working on it).
//
// Cheap, and safe from the canvas tick: it launches nothing and opens no dialog, and setting the
// notice already on screen again repaints nothing.
void ModelViewer::UpdateViewportState()
{
  // Recorded for the tick, which compares against it to notice a mount or a dismount.
  // Recorded before any early return: a load decides here, and the tick
  // that follows it must not decide it again.
  m_lastShowsCharacter = canvasShowsCharacter();
  m_lastShowsMountedCharacter = canvasShowsMountedCharacter();
  if (!viewportHost)
    return;

  // A texture picked in Browse: the texture view covers the viewport.
  if (isTextureMode() && textureView)
  {
    const bool changed = !viewportHost->isShowingContent() || viewportHost->content() != textureView;
    viewportHost->setContent(textureView);
    viewportHost->showContent(true);
    viewportHost->clearNotice();
    // The model's panels go once the texture covers the viewport.
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
  bool changed = viewportHost->isShowingContent();
  if (changed)
    LOG_INFO << "[viewport] the texture view is put away";
  if (viewportNotice(notice))
  {
    const bool newNotice = !viewportHost->hasNotice() || viewportHost->noticeTitle() != notice.title;
    if (newNotice)
      LOG_INFO << "[viewport] notice:" << QString::fromWCharArray(notice.title.c_str()) << "--"
               << QString::fromWCharArray(notice.detail.c_str());
    changed = changed || newNotice;
    viewportHost->setNotice(notice.title, notice.detail, notice.actionLabel, notice.actionId);
  }
  else
  {
    if (viewportHost->hasNotice() || viewportHost->isShowingContent())
    {
      LOG_INFO << "[viewport] showing the model";
      changed = true;
    }
    viewportHost->clearNotice();
  }
  // The mode's panels come back (or go) first, while the texture view still covers the viewport, so the
  // canvas is uncovered at its final size; then it is put away, after the notice is decided.
  applyWorkspace(workspacePanes(m_viewerMode));
  if (m_layoutBatch > 0)
    m_uncoverPending = true;   // put away after the batch's layout (commitLayoutBatchNow)
  else
    viewportHost->showContent(false);
  // The Geosets tab says whether the viewport shows the model.
  if (changed && modelInspector)
    modelInspector->ViewportNoticeChanged();
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
// The chooser ("Choose World of Warcraft") says which installation, and the client opens in it: a click on a card
// turns it into its loading page, where LoadWoW reports what it is doing and, in plain words, why it cannot. A client
// that could not be opened leaves the page on that, with Back to the cards, so another can be picked; the client
// loaded before (if any) is still loaded meanwhile.
void ModelViewer::PromptAndLoadClient()
{
  FinishViewerSwitch("the client prompt");
  if (m_clientLoading) // the loading window yields to the event loop: no second load inside the first
    return;
  for (;;)
  {
    ClientChoiceDialog clientDlg(this);
    // A card's client opens in the chooser itself, on its loading page: one window from the click to the loaded
    // client. One that could not be opened says why there, and Back returns to the cards.
    clientDlg.setLoader([this](const InstalledClient & client, ClientLoadProgress * progress) {
      const wxString previousPath = gamePath;
      gamePath = wxString(client.dataPath().toStdWString());
      const core::GameConfig chosen = client.config();
      if (LoadWoW(&chosen, progress))
        return true;
      gamePath = previousPath; // not opened: the client loaded before (if any) is still the one in use
      return false;
    });
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

    return; // the chooser closes on a client that opened
  }
}

// Quietly refresh the CASC file list so files added by new client patches resolve by name
// without the user maintaining anything. The source is fixed (no setting, nothing user-visible):
// on EVERY launch it asks the server whether the community listfile changed (cheap conditional
// request) and only re-downloads the ~147 MB list when it actually did. Any problem (offline,
// server error, short payload) leaves the existing list in place, so a failed refresh can never
// break startup. Streams to a temp file to keep memory flat, then swaps it in atomically. Reuses
// the generic "Loading file list..." step so nothing about a network fetch surfaces in the UI.
// A request that stops answering is given up after this long without data -- Qt 5.15's default transfer timeout
// (QNetworkRequest::DefaultTransferTimeoutConstant); this Qt has none of its own -- and the load goes on with what is
// on disk instead of waiting for ever.
static const int NETWORK_STALL_MS = 30000;

// Runs the event loop until the reply has finished: aborted when no data has arrived for NETWORK_STALL_MS (the
// caller sees an error and keeps what it has).
static void waitForReply(QNetworkReply * reply)
{
  QEventLoop loop;
  QTimer stall;
  stall.setSingleShot(true);
  QObject::connect(&stall, &QTimer::timeout, reply, [reply]() {
    LOG_WARNING << "Download stalled (no data for" << NETWORK_STALL_MS / 1000 << "s):" << reply->url().toString()
                << "- giving up";
    reply->abort();
  });
  QObject::connect(reply, &QNetworkReply::downloadProgress, &stall, [&stall]() { stall.start(NETWORK_STALL_MS); });
  QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
  stall.start(NETWORK_STALL_MS);
  if (!reply->isFinished())
    loop.exec();
}

static bool refreshCommunityListfile(const QString & localPath, ClientLoadProgress * progress)
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
    return false; // can't stage a download here; keep what's on disk

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

  if (progress)
  {
    ClientLoadProgress * pd = progress;
    bool downloading = false;
    QObject::connect(reply, &QNetworkReply::downloadProgress, [pd, downloading](qint64 received, qint64 total) mutable {
      // Keeps the loading page moving during the request. Only a real download (a changed list: ~150 MB) is shown
      // as one, with how much of it arrived; an unchanged list answers in a few bytes and shows nothing new.
      if (total > 1024 * 1024)
      {
        if (!downloading)
          pd->stage(_("Downloading the latest file list..."), (double)received / (double)total);
        else
          pd->progress((double)received / (double)total);
        downloading = true;
      }
      else
        pd->progress(-1.0);
    });
  }
  waitForReply(reply);

  const bool ok = (reply->error() == QNetworkReply::NoError);
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray newEtag = reply->rawHeader("ETag");
  out.close();

  // Unchanged since last launch -> nothing downloaded; keep the existing list.
  if (ok && status == 304)
  {
    LOG_INFO << "File list unchanged (304 Not Modified); keeping existing list.";
    core::LoadTimeline::instance().cache("listfile download", "HIT", "304 not modified");
    QFile::remove(tmpPath);
    return true;
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
      core::LoadTimeline::instance().cache("listfile download", "MISS", QString("downloaded %1 bytes").arg(size));
      if (!newEtag.isEmpty())
      {
        QFile ef(etagPath);
        if (ef.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
          ef.write(newEtag);
          ef.close();
        }
      }
      return true;
    }
    LOG_ERROR << "File list refresh: could not replace" << localPath;
  }
  else
  {
    LOG_INFO << "File list refresh skipped; keeping existing list.";
    core::LoadTimeline::instance().cache("listfile download", "SKIPPED", "kept the existing list");
  }
  QFile::remove(tmpPath); // discard the partial/unused temp file
  return false; // no answer, or a list that could not be put in place: asked again next load
}

// Refresh the TACT encryption keys on EVERY launch. CASC needs these to decrypt encrypted content
// -- both whole encrypted files and the encrypted DB2 sections that hold brand-new / unreleased
// records (recent raid bosses etc.). New keys get published frequently while a patch is on the PTR,
// and the list is small (~1 MB), so we always pull the latest rather than caching it for a week
// like the (147 MB) file list. The community list at wowdev/TACTKeys is whitespace-separated
// "<keyname> <key>"; we convert it to the "<keyname>;<key>" form CASCFolder::addExtraEncryptionKeys()
// reads. Must run BEFORE setConfig() opens the storage (that is when the keys are handed to CASC).
// Any failure (e.g. offline, or GitHub down) keeps the existing on-disk keys untouched.
static bool refreshTactKeys(const QString & localPath, ClientLoadProgress * progress)
{
  // The loading window keeps saying which client is being opened: the keys are a detail of opening it.
  Q_UNUSED(progress);

  QNetworkAccessManager manager;
  QNetworkRequest request(QUrl("https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt"));
  request.setRawHeader("User-Agent", "WoWModelViewer");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply * reply = manager.get(request);
  waitForReply(reply);

  const bool ok = (reply->error() == QNetworkReply::NoError);
  const QByteArray body = reply->readAll();
  reply->deleteLater();

  // A real list is hundreds of KB of "<16 hex> <32 hex>" lines; anything tiny is an error page.
  if (!ok || body.size() < (100 * 1024))
  {
    LOG_INFO << "TACT key refresh skipped; keeping existing keys.";
    core::LoadTimeline::instance().cache("tact keys download", "SKIPPED", "kept the existing keys");
    return false;
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
    return false;
  }

  const QString tmpPath = localPath + ".new";
  QFile out(tmpPath);
  if (!out.open(QIODevice::WriteOnly))
    return false;
  out.write(outData);
  out.close();

  if (QFile::exists(localPath))
    QFile::remove(localPath);
  if (QFile::rename(tmpPath, localPath))
  {
    LOG_INFO << "TACT keys refreshed (" << count << "keys).";
    core::LoadTimeline::instance().cache("tact keys download", "MISS", QString("downloaded %1 keys").arg(count));
    return true;
  }
  QFile::remove(tmpPath);
  return false;
}

namespace
{
  // The loading page of the load under way (ModelViewer::m_loadProgress), for the steps that report from outside
  // LoadWoW (InitDatabase); cleared when the load returns, whichever way.
  struct LoadProgressScope
  {
    LoadProgressScope(ClientLoadProgress *& slot, ClientLoadProgress * progress) : m_slot(slot) { m_slot = progress; }
    ~LoadProgressScope() { m_slot = nullptr; }
    ClientLoadProgress *& m_slot;
  };

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
  ClientState::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
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
    UpdateViewportState();
    TexturesClientLoaded();
    if (isMapsMode() && fileControl)
      fileControl->Init(this);
  });

  SetStatusText(wxString(GAMEDIRECTORY.version().toStdWString()), 1);
  SetStatusText(wxT("Legacy MPQ"), 2);
  // A legacy client has no Map.db2 this viewer reads: its catalogue says so (Unsupported) rather than being absent; its
  // liquid and light catalogues likewise (Unavailable).
  m_mapCatalog = wow::WorldMapCatalog::build();
  m_liquidCatalog = wow::LiquidCatalog::build();
  m_lightCatalog = wow::LightCatalog::build();
  m_lightCatalog->buildWorldLight();
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
// (CascLib's error codes: the Windows ones on Windows, errno values elsewhere, where some coincide -- so an if chain.)
static wxString clientOpenProblem(int error)
{
  if (error == (int)ERROR_FILE_NOT_FOUND || error == (int)ERROR_PATH_NOT_FOUND)
    return _("Its game data is not on this computer. The Battle.net app may not have downloaded it yet: start "
             "the game once from Battle.net, or let its installation finish, and try again.");
  if (error == (int)ERROR_FILE_CORRUPT)
    return _("Its game data could not be read: it is incomplete or damaged. Battle.net's Scan and Repair "
             "can restore it.");
  if (error == (int)ERROR_INVALID_PARAMETER)
    return _("It has no language this viewer can open.");
  return _("The installation could not be opened.");
}

// The client-derived state a newly opened client replaces: what is on screen, and every cache filled from the previous
// client's files or database. (The file tree, database, races, NPC and item lists are refilled by the load itself.)
void ModelViewer::ResetClientState()
{
  // The same FileDataID is another file in the next client: caches keyed by it start over.
  ClientState::noteClientReplaced();
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
  // The previous client's map catalogue: dropped, never handed to the next client (a holder keeps it alive, but it
  // then no longer belongs to the loaded client and refuses to read its storage). Until the load builds the next one,
  // the viewer's says a client is being loaded.
  m_mapCatalog = wow::WorldMapCatalog::noClient("a client is being loaded");
  m_liquidCatalog = wow::LiquidCatalog::noClient("a client is being loaded");
  m_lightCatalog = wow::LightCatalog::noClient("a client is being loaded");
}

bool ModelViewer::LoadWoW(const core::GameConfig * chosenConfig, ClientLoadProgress * progress)
{
  FinishViewerSwitch("a client load");
  // One load at a time: the loading window yields to the event loop, where File > Load World of Warcraft (or the
  // empty viewer's button) could otherwise start a second one inside this one.
  if (m_clientLoading)
    return false;
  ClientLoadingFlag loadingFlag(m_clientLoading);

  // Every stage of the load is timed (LoadTimeline). A load started from the chooser's card was started there.
  core::LoadTimeline & timeline = core::LoadTimeline::instance();
  timeline.startIfIdle(chosenConfig ? chosenConfig->product + " " + chosenConfig->version : QString("client load"));
  timeline.mark("load starts");
  timeline.begin("installation");

  wxStopWatch total, step;
  ClientState::ClientLoadGuard unityAssetGuard; // refuse Unity asset requests while the folder is rebuilt
  if (gamePath.IsEmpty() || !wxDirExists(gamePath))
  {
    // An install found on this computer (Battle.net's list, the usual folders, Wine and Proton prefixes) before
    // the folder chooser.
    const QStringList roots = ClientInstallations::candidateRoots();
    if (!roots.isEmpty())
      gamePath = QDir::toNativeSeparators(roots.first() + "/Data/").toStdWString();
    else
      getGamePath();
  }
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
      timeline.finish("failed: no installation");
      LOG_ERROR << "No World of Warcraft installation found in" << dataPath;
      if (progress)
        progress->failed(_("World of Warcraft could not be opened."),
                         _("No World of Warcraft installation was found in that folder."), gamePath);
      else if (!batchMode)
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
  timeline.end("installation", (reuse ? QString("same installation, ") : QString("new folder, ")) + requested.describe());
  timeline.begin("loading window");

  // THE LOADING PAGE (the chooser's: ClientLoadProgress), told a few stages in plain words. It pumps the event loop
  // as it repaints; everything but its window is disabled meanwhile: no menu, shortcut, Browse row or panel can act
  // on a client that is being replaced (the storage is swapped and the file index rebuilt in place on the reuse path).
  std::unique_ptr<wxWindowDisabler> disabler;
  LoadProgressScope progressScope(m_loadProgress, progress);
  if (progress)
  {
    wxWindow * surface = dynamic_cast<wxWindow *>(progress);
    disabler.reset(new wxWindowDisabler(surface ? wxGetTopLevelParent(surface) : nullptr));
    progress->stage(_("Opening game data..."));
  }
  auto closeProgress = [&disabler]() { disabler.reset(); };

  timeline.end("loading window");
  // The TACT keys and the community listfile are asked for once per session, and again after an hour: a client
  // opened minutes after another gains nothing from asking again, and each request holds the load for a round trip.
  static QElapsedTimer s_networkAsked;
  const bool askNetwork = !s_networkAsked.isValid() || s_networkAsked.elapsed() > 60 * 60 * 1000;
  const QString askedAgo = s_networkAsked.isValid()
                             ? QString("asked %1 s ago in this session").arg(s_networkAsked.elapsed() / 1000)
                             : QString();
  // Refresh the TACT keys before opening the storage -- setConfig() hands them to CASC, so a newer key list lets it
  // decrypt encrypted db2 sections + files for recently-added content.
  timeline.begin("tact keys");
  bool keysAnswered = false;
  if (askNetwork)
    keysAnswered = refreshTactKeys("extraEncryptionKeys.csv", progress);
  else
    timeline.cache("tact keys download", "SKIPPED", askedAgo);
  timeline.end("tact keys");

  // A first start has no file list yet (the installer does not carry one): it is fetched now, before anything of the
  // client loaded before is let go of, so a start without a connection says so instead of opening a client with
  // nothing in Browse. (The path is the one the refresh below uses: beside the exe, the working directory.)
  bool listAnsweredEarly = false;
  if (!QFile::exists(QStringLiteral("listfile.csv")))
  {
    timeline.begin("listfile first download");
    listAnsweredEarly = refreshCommunityListfile(QStringLiteral("listfile.csv"), progress);
    timeline.end("listfile first download");
    if (!QFile::exists(QStringLiteral("listfile.csv")))
    {
      timeline.finish("failed: no file list");
      closeProgress();
      LOG_ERROR << "[clientload] no file list, and it could not be downloaded";
      const wxString title = _("The file list could not be downloaded.");
      const wxString message =
        _("WoW Model Viewer needs the community file list (about 150 MB) to find the game's files. It is downloaded "
          "the first time a client is opened. Check the internet connection and open the client again.");
      if (progress)
        progress->failed(title, message, wxString());
      else if (!batchMode)
        wxMessageBox(message, title, wxOK | wxICON_ERROR, this);
      // The client loaded before (if any) is untouched and still usable.
      return false;
    }
    // The download put up its own label: back to what comes next.
    if (progress)
      progress->stage(_("Opening game data..."));
  }

  if (progress)
    folder->setLoadProgressCallback([progress](core::GameFolder::LoadPhase, float fraction) {
      progress->progress(fraction);
    });
  step.Start();
  timeline.begin("storage");
  const bool opened = folder->setConfig(config);
  timeline.end("storage", opened ? QString() : QString("failed"));
  folder->setLoadProgressCallback(core::GameFolder::LoadReport());
  const long openMs = step.Time();
  if (!opened)
  {
    const int err = folder->lastError();
    timeline.finish("failed: the storage could not be opened");
    closeProgress();
    LOG_ERROR << "[clientload] could not open" << requested.describe() << "error" << err << "after" << openMs << "ms";
    const wxString details =
      wxString::Format(_("Product: %s\nVersion: %s\nInstallation: %s\nStorage: CASC, error %d"),
                       wxString(config.product.toStdWString()), wxString(config.version.toStdWString()), gamePath, err);
    if (progress)
      progress->failed(wxString::Format(_("%s could not be opened."), friendly), clientOpenProblem(err), details);
    else if (!batchMode)
    {
      wxRichMessageDialog dlg(this, wxString::Format(_("%s could not be opened."), friendly),
                              _("World of Warcraft"), wxOK | wxICON_ERROR);
      dlg.SetExtendedMessage(clientOpenProblem(err));
      dlg.ShowDetailedText(details);
      dlg.ShowModal();
    }
    // The client loaded before (if any) is untouched and still usable.
    return false;
  }

  // Opened: from here on this client replaces the previous one.
  timeline.begin("teardown");
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
  timeline.end("teardown");

  timeline.begin("schema");
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
  timeline.end("schema", schemaHow);

  step.Start();
  timeline.begin("listfile check");
  if (askNetwork)
  {
    // (Not asked twice: a first start has just downloaded it.)
    const bool listAnswered =
      listAnsweredEarly ||
      refreshCommunityListfile(core::Game::instance().configFolder() + "../../../listfile.csv", progress);
    // Only real answers spare the next hour's loads from asking: offline, or after a failed request, every load asks
    // again, as it always did.
    if (keysAnswered && listAnswered)
      s_networkAsked.start();
  }
  else
    timeline.cache("listfile download", "SKIPPED", askedAgo);
  timeline.end("listfile check");
  if (progress)
  {
    // The listfile is read once per session ("Reading the file list..."); this client's files are indexed every
    // load ("Reading game files..."). Both know how far they are.
    core::GameFolder::LoadPhase shown = core::GameFolder::LoadPhase::OpeningStorage;
    GAMEDIRECTORY.setLoadProgressCallback([progress, shown](core::GameFolder::LoadPhase phase, float fraction) mutable {
      if (phase != shown)
      {
        shown = phase;
        progress->stage(phase == core::GameFolder::LoadPhase::ReadingListfile ? _("Reading the file list...")
                                                                              : _("Reading game files..."),
                        fraction);
      }
      else
        progress->progress(fraction);
    });
  }
  timeline.begin("file index");
  GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
  timeline.end("file index");
  GAMEDIRECTORY.setLoadProgressCallback(core::GameFolder::LoadReport());
  const long listfileMs = step.Time();

  if (!customDirectoryPath.IsEmpty())
  {
    core::LoadTimeline::Stage customStage("custom files");
    core::Game::instance().addCustomFiles(QString::fromWCharArray(customDirectoryPath.c_str()), customFilesConflictPolicy);
  }

  if (progress)
    progress->stage(_("Loading game database..."));
  step.Start();
  timeline.begin("database");
  InitDatabase();
  timeline.end("database");
  const long databaseMs = step.Time();

  if (progress)
    progress->stage(_("Preparing Browse..."));
  SetStatusText(wxT("Initializing File Control..."));
  step.Start();
  timeline.begin("browse");
  fileControl->Init(this);
  timeline.end("browse");
  const long browseMs = step.Time();
  step.Start();
  timeline.begin("characters");
  if (charControl->Init() == false)
    SetStatusText(wxT("Error Initializing the Character Controls."));
  timeline.end("characters");
  const long charactersMs = step.Time();
  fileControl->Enable();
  // Browse and the character controls now point at this client's files only (the canvas was cleared before the
  // file list was read): the files the reload detached can go -- while the viewer is idle, after the load, a share at
  // a time (freeing ~1.8 million files takes ~0.5 s that the user would otherwise wait for).
  FreeDetachedWhenIdle();
  // The client's maps: one query of Map.db2 and a WDT reference per map, no file opened (whether each WDT can be read
  // is inspected per map, on request).
  timeline.begin("map catalogue");
  m_mapCatalog = wow::WorldMapCatalog::build();
  timeline.end("map catalogue", QString("%1 maps, %2").arg(m_mapCatalog->maps().size())
                                  .arg(wow::WorldMapCatalog::statusName(m_mapCatalog->status())));
  // The client's liquid types and their frames (docs/world-viewer/09-liquids.md 1.6): a few queries of its liquid tables
  // and, where the client has no LiquidTypeXTexture rows, its storage root's names -- no file opened. Built once here, the
  // same catalogue goes with every load of this client to a player that draws liquids.
  timeline.begin("liquid catalogue");
  m_liquidCatalog = wow::LiquidCatalog::build();
  timeline.end("liquid catalogue", QString("%1 types, %2 frames, %3")
                                     .arg(m_liquidCatalog->types().size()).arg(m_liquidCatalog->frameCount())
                                     .arg(wow::LiquidCatalog::statusName(m_liquidCatalog->status())));
  // The client's lights, for the zone light's water colours (docs/world-viewer/09-liquids.md): its five light tables read
  // whole and LightParams.db2's copy table. Built once here; a map's lights object is made on the map's first load.
  timeline.begin("light catalogue");
  m_lightCatalog = wow::LightCatalog::build();
  timeline.end("light catalogue", wow::LightCatalog::statusName(m_lightCatalog->status()));
  // The outdoor light (docs/world-viewer/10-light-sky-fog.md 6.2): every named column of the light tables read whole and
  // one fragment per LightParams day made, a stage of its own (budget 250 ms); a map's object is put together from them on
  // the map's first load.
  timeline.begin("outdoor light");
  m_lightCatalog->buildWorldLight();
  timeline.end("outdoor light", m_lightCatalog->worldLightReady()
                                  ? QString("%1 days, %2 keys, %3 ms").arg(m_lightCatalog->worldCounts().days)
                                      .arg(m_lightCatalog->worldCounts().keys).arg(m_lightCatalog->worldLightBuildMilliseconds(), 0, 'f', 1)
                                  : m_lightCatalog->worldLightDetail());

  if (progress)
    progress->stage(_("Almost ready..."));
  timeline.begin("capabilities");
  ComputeClientCapabilities();
  timeline.end("capabilities");
  timeline.begin("remember client");
  RememberLoadedClient(config);
  timeline.end("remember client");

  LOG_INFO << "[clientload] timing: open" << openMs << "ms, file list" << listfileMs << "ms, database" << databaseMs
           << "ms, Browse" << browseMs << "ms, characters" << charactersMs << "ms, total" << total.Time() << "ms";
  // The empty viewport points at Browse now rather than at loading a client -- once the load has
  // returned, since the client does not count as active while it is still inside it.
  CallAfter([this]() {
    core::LoadTimeline::Stage stage("final ui (deferred)");
    UpdateViewportState();
    TexturesClientLoaded();
    // The Maps viewer's list of the new catalogue (built after Browse was listed; listed only once the client counts
    // as active, which it does not inside the load). Another viewer on, the maps are listed at the next entry into Maps,
    // in a turn of its own (section 8.9); listed here, ahead, they added that listing to this round.
    if (isMapsMode() && fileControl)
      fileControl->Init(this);
    DisplayedContentChanged();
  });
  SetStatusText(wxString::Format(_("%s loaded."), friendly));
  timeline.begin("close loading window");
  closeProgress();
  timeline.end("close loading window");
  timeline.mark("load returned");
  return true;
}

// THE FILES A LOAD DETACHED (the previous client's that this one does not list) are freed while the viewer is idle, a
// share per idle event, so the user never waits for it. Never during a load: until a load has rebuilt Browse, rows of
// the client before still point at its files (and a load frees whatever is left first, WoWFolder::initFromListfile).
void ModelViewer::FreeDetachedWhenIdle()
{
  wow::WoWFolder * live = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  if (m_freeingDetached || !live || live->detachedFileCount() == 0)
    return;
  m_freeingDetached = true;
  LOG_INFO << "WoWFolder - Freeing" << (unsigned int)live->detachedFileCount()
           << "files the last reload detached, while the viewer is idle";
  Bind(wxEVT_IDLE, &ModelViewer::OnIdleFreeDetached, this);
}

void ModelViewer::OnIdleFreeDetached(wxIdleEvent & event)
{
  event.Skip();
  if (m_clientLoading || ClientState::isClientLoading())
    return; // a load is under way (a yield inside it): wait for it to finish
  // NOT IN A VIEWER SWITCH'S ROUNDS: while one is on its way, and at the idle of a round a switch's turn or a turn after it
  // ran in (m_turnSerial). A share asked for more is run again at once by the event loop, and a switch turn due meanwhile
  // would follow it in the same busy stretch. The next idle (the next poll's) goes on.
  if (m_switch || m_turnSerial != m_freeDetachedTurnSeen)
  {
    m_freeDetachedTurnSeen = m_turnSerial;
    return;
  }
  wow::WoWFolder * live = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  wxStopWatch clock;
  const size_t left = live ? live->freeDetachedFiles(100000) : 0; // ~25 ms a share
  if (left > 0)
  {
    event.RequestMore();
    return;
  }
  Unbind(wxEVT_IDLE, &ModelViewer::OnIdleFreeDetached, this);
  m_freeingDetached = false;
  LOG_INFO << "WoWFolder - Freed the files the last reload detached (the last share in" << clock.Time() << "ms)";
}

// The first idle after a load's interface refresh: the viewer answers the user from here on, which is what the
// load's timeline measures as "interactive".
void ModelViewer::NoteInteractiveWhenIdle()
{
  if (m_interactivePending || !core::LoadTimeline::instance().running())
    return;
  m_interactivePending = true;
  Bind(wxEVT_IDLE, &ModelViewer::OnIdleAfterLoad, this);
}

void ModelViewer::OnIdleAfterLoad(wxIdleEvent & event)
{
  event.Skip();
  Unbind(wxEVT_IDLE, &ModelViewer::OnIdleAfterLoad, this);
  m_interactivePending = false;
  if (m_clientLoading || ClientState::isClientLoading())
    return; // a load is still under way (a yield inside it): the refresh after it notes the moment
  core::LoadTimeline::instance().mark("interactive");
  core::LoadTimeline::instance().finish("interactive");
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
  // The file index keeps these counts as it changes (GameFolder::indexedTypeCounts): no walk over its paths.
  const core::GameFolder::TypeCounts & types = GAMEDIRECTORY.indexedTypeCounts();
  caps.modelFiles = (int)types.models;
  caps.textureFiles = (int)types.textures;
  caps.buildingFiles = (int)types.buildings;
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
  caps.maps = m_mapCatalog && m_mapCatalog->status() == wow::WorldMapCatalog::Status::Ready &&
              m_mapCatalog->belongsToActiveClient();
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
      // A hand item this client gives no appearance (display 0) holds no model, as in the viewport.
      if (!item->isEquipped() ||
          ((entry.first == CS_HAND_LEFT || entry.first == CS_HAND_RIGHT) && item->displayId() > 0 && item->models().empty()))
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

bool ModelViewer::PrepareFbxAsset(wxString & args, wxString & label, wxString & tempCharPath, bool customizations)
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
    if (customizations)
    {
      // Export-only state: ordinary .chr saves and Current appearance keep their
      // existing behavior. The worker must see the state captured at confirmation.
      QFile snapshot(QString::fromStdWString(tempCharPath.ToStdWstring()));
      QDomDocument doc;
      const auto failed = [&]() {
        snapshot.close();
        wxRemoveFile(tempCharPath);
        tempCharPath.clear();
        return false;
      };
      if (!snapshot.open(QIODevice::ReadOnly) || !doc.setContent(&snapshot)) return failed();
      snapshot.close();
      auto state = doc.createElement("fbxCustomizationState");
      state.setAttribute("sheathed", m->bSheathe ? 1 : 0);
      state.setAttribute("autoHideHead", m->cd.autoHideGeosetsForHeadItems ? 1 : 0);
      for (auto * item : *m)
        for (const auto & model : item->models())
        {
          auto part = doc.createElement("attachment");
          part.setAttribute("slot", (int)item->slot());
          part.setAttribute("position", (int)model.first);
          part.setAttribute("visible", model.second->showModel ? 1 : 0);
          state.appendChild(part);
        }
      doc.documentElement().appendChild(state);
      const auto bytes = doc.toByteArray();
      if (!snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          snapshot.write(bytes) != bytes.size() || !snapshot.flush()) return failed();
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
          // On a model that serves both sexes (the Dracthyr's dragon form), the body sex its items are chosen for
          // (WoWModel::itemSexID); a file saved without one is male, as such a model loads anywhere else.
          bool bodySexRead = false;
          const int bodySex = reader.attributes().value("bodySex").toString().toInt(&bodySexRead);
          LoadModel(GAMEDIRECTORY.getFile(modelname), raceSaved ? race : -1, raceSaved ? sex : -1);
          WoWModel * m = const_cast<WoWModel *>(canvas->model());
          if (m)
            m->setBodySex(bodySexRead ? bodySex : GENDER_MALE);
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

  // Only export snapshots carry this node. Restore it after all items have been
  // loaded and before refresh resolves attachment points and helmet hiding.
  if (!equipmentOnly)
  {
    file.seek(0);
    QDomDocument snapshot;
    if (snapshot.setContent(&file))
    {
      const auto state = snapshot.documentElement().firstChildElement("fbxCustomizationState");
      auto * model = const_cast<WoWModel *>(canvas->model());
      if (!state.isNull() && model)
      {
        model->bSheathe = state.attribute("sheathed", "0").toInt() != 0;
        model->cd.autoHideGeosetsForHeadItems = state.attribute("autoHideHead", "1").toInt() != 0;
        for (auto part = state.firstChildElement("attachment"); !part.isNull(); part = part.nextSiblingElement("attachment"))
          if (auto * item = model->getItem((CharSlots)part.attribute("slot").toInt()))
            for (const auto & attached : item->models())
              if ((int)attached.first == part.attribute("position").toInt())
                attached.second->showModel = part.attribute("visible", "1").toInt() != 0;
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

void ModelViewer::OnAbout(wxCommandEvent & WXUNUSED(event))
{
  wxAboutDialogInfo info;
  // The heading: "WoW Model Viewer 1.1".
  info.SetName(GLOBALSETTINGS.productName());
  wxString version = GLOBALSETTINGS.appVersion();
  if (GLOBALSETTINGS.isBeta())
    version += wxT(" BETA VERSION");
  info.SetVersion(version);

  info.SetDescription(L"WoW Model Viewer is a 3D model viewer for World of Warcraft.\n"
                      L"It uses the game\u2019s data files to display creatures, characters,\n"
                      L"spell effects, objects, and other models.\n"
                      L"\n"
                      L"Credits to: Linghuye, nSzAbolcs, Sailesh, Terran and Cryect\n"
                      L"for their direct and indirect contributions.");
  info.SetWebSite(wxT("https://github.com/wowmodelviewer/wowmodelviewer"));
  info.SetLicence(wxT("WoW Model Viewer is free software, released under the GNU General Public License,\n"
                      "version 3 or (at your option) any later version. It comes with ABSOLUTELY NO WARRANTY.\n"
                      "See LICENSE.txt and THIRD-PARTY-NOTICES.txt in the program folder.\n"
                      "\n"
                      "It uses Qt 5.13.2, Copyright (C) 2019 The Qt Company Ltd., under the GNU Lesser General\n"
                      "Public License, version 3 (its text is in THIRD-PARTY-NOTICES.txt)."));

  info.AddDeveloper(wxT("Ufo_Z"));
  info.AddDeveloper(wxT("Darjk"));
  info.AddDeveloper(wxT("Chuanhsing"));
  info.AddDeveloper(wxT("Kjasi (A.K.A. Sephiroth3D)"));
  info.AddDeveloper(wxT("Tob.Franke"));
  info.AddDeveloper(wxT("Jeromnimo"));
  info.AddDeveloper(wxT("Wain"));
  info.AddDeveloper(wxT("Rasmuslnd"));

  info.SetCopyright(L"World of Warcraft\u00AE is a registered trademark of Blizzard Entertainment\u00AE.\n"
                    L"All game assets and content are \u00A92004\u20132026 Blizzard Entertainment\u00AE.\n"
                    L"All rights reserved.");

  std::unique_ptr<wxBitmap> bitmap(createBitmapFromResource(L"ABOUTICON", wxBITMAP_TYPE_XPM, 128, 128));
  if (bitmap && bitmap->IsOk())
  {
    wxIcon icon;
    icon.CopyFromBitmap(*bitmap);
    info.SetIcon(icon);
  }

  // The dialog wxAboutBox would show, over this window: "About WoW Model Viewer".
  wxGenericAboutDialog dialog(info, this);
  dialog.SetTitle(wxString::Format(_("About %s"), wxString(GLOBALSETTINGS.appName())));
  dialog.ShowModal();
}

bool ModelViewer::isViewportCentre()
{
  if (!viewportHost)
    return false;
  // The host is always managed (InitDocking), so this lookup never lands on wx's shared null pane.
  wxAuiPaneInfo & up = interfaceManager.GetPane(viewportHost);
  return up.IsOk() && up.IsShown() && up.dock_direction == wxAUI_DOCK_CENTER;
}

bool ModelViewer::isViewportShowingModel()
{
  if (!viewportHost || !canvas)
    return false;
  return !viewportHost->hasNotice() && !viewportHost->isShowingContent() && canDrawCurrentModel();
}

bool ModelViewer::viewportHasNotice() const
{
  return !viewportHost || viewportHost->hasNotice() || viewportHost->isShowingContent();
}

// The size of the viewport, which is the Unity viewport's host panel (the archived canvas is hidden and
// has no meaningful size). Called by the host whenever it is resized.
void ModelViewer::UpdateCanvasStatus()
{
  if (!viewportHost || !GetStatusBar())
    return;
  const wxSize size = viewportHost->GetClientSize();
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

// The command bar's name of what the viewer mode shows (and its tooltip): the texture, the world model, the model, or
// the map and its tile on screen.
void ModelViewer::UpdateCommandLabel()
{
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
    if (!isMapsMode())
    {
      name.Replace(wxT("/"), wxT("\\"));
      name = name.AfterLast('\\');
    }
    const wxString empty = isTextureMode() ? _("No texture selected") : isBuildingsMode() ? _("No building selected")
                         : isMapsMode() ? _("No map selected") : _("No model loaded");
    commandModelLabel->SetLabel(name.IsEmpty() ? empty : name);
    UiStyle::setRole(commandModelLabel, name.IsEmpty() ? UiStyle::Role::SecondaryText : UiStyle::Role::Text);
    commandModelLabel->SetToolTip(tip.IsEmpty() ? path : tip);
    commandModelLabel->Refresh();
  }
}

void ModelViewer::DisplayedContentChanged()
{
  if (!canvas)
    return;

  // The viewport first: the model, the empty viewer, or a notice for what it cannot draw yet.
  UpdateViewportState();
  // The colour for what is loaded (viewportBackgroundShown): the panel's backdrop and the command line of a player
  // started later follow it, with or without a player. The player itself is sent it only with a load it gets
  // (SendLoadToUnity): a load that failed, or a world model that cannot be read, leaves it drawing what it had.
  if (viewportHost)
    viewportHost->setBackdrop(viewportBackgroundShown());

  // The Animation panel drives the model: greyed in the other viewers (put away there, too).
  if (animControl)
    animControl->Enable(isModelsMode());

  UpdateCommandLabel();
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
// For the command bar's click the change is made in turns of their own; see BeginViewerSwitch.
void ModelViewer::beginLayoutBatch()
{
  m_layoutBatch++;
}

void ModelViewer::endLayoutBatch()
{
  if (m_layoutBatch <= 0 || --m_layoutBatch > 0)
    return;
  commitLayoutBatchNow();
}

// The batch's layout made now: one wxAuiManager::Update (the dock cap lifted when panels came back), the panels given back
// their own best size, then the texture view or the overhead map put away after it (the player uncovered at its final
// size). A viewer switch's first turn makes it with the batch still open -- what is asked for meanwhile is laid out in its
// last turn. Inside a switch (its drawing held back) the floating panels keep their windows as they are until its repaint,
// and a docked panel given back is moved off the frame before wxAUI shows it.
void ModelViewer::commitLayoutBatchNow()
{
  if (m_layoutPending)
  {
    m_layoutPending = false;
    if (m_switch && m_switch->drawHeld)
    {
      holdFloatingPanes();
      moveGivenBackPanesOff();
    }
    if (m_layoutUncapped)
    {
      m_layoutUncapped = false;
      commitDocksNow();
    }
    else
      interfaceManager.Update();
  }
  if (m_finishWorkspacePending)
  {
    m_finishWorkspacePending = false;
    finishWorkspace();
  }
  if (m_uncoverPending)
  {
    m_uncoverPending = false;
    if (viewportHost)
      viewportHost->showContent(false);
  }
}

// A floating panel's window (Attachments) shown or hidden by a switch's layout would change at once, over the old workspace
// (wxAuiManager::Update: Show on its frame): its pane keeps the window's state through the layout, and what is due is
// applied after the repaint (FinishViewerSwitch). (The layout makes the frame of a floating panel that has none yet, hidden.)
void ModelViewer::holdFloatingPanes()
{
  ViewerSwitch & s = *m_switch;
  wxAuiPaneInfoArray & panes = interfaceManager.GetAllPanes();
  for (size_t i = 0; i < panes.GetCount(); i++)
  {
    wxAuiPaneInfo & p = panes[i];
    if (!p.window || !p.IsFloating())
      continue;
    const bool shownNow = p.frame && p.frame->IsShown();
    if (p.IsShown() == shownNow)
      continue;
    bool recorded = false;
    for (std::pair<wxString, bool> & due : s.floatingDue)
      if (due.first == p.name)
      {
        due.second = p.IsShown();
        recorded = true;
      }
    if (!recorded)
      s.floatingDue.emplace_back(p.name, p.IsShown());
    p.Show(shownNow);
  }
}

// Their panes back to what is due (their windows unchanged): before the thaw, so their toggles paint with the rest.
void ModelViewer::restoreFloatingPanes()
{
  for (const std::pair<wxString, bool> & due : m_switch->floatingDue)
  {
    wxAuiPaneInfo & p = interfaceManager.GetPane(due.first);
    if (p.IsOk())
      p.Show(due.second);
  }
}

// wxAUI shows a docked panel given back before it places it, and Windows erases a window as it is shown -- where the panel
// was when it went, over the old workspace. Moved off the frame first, that erase draws nothing on screen (the filter
// swallows it besides); the layout then places it, frozen.
void ModelViewer::moveGivenBackPanesOff()
{
  wxAuiPaneInfoArray & panes = interfaceManager.GetAllPanes();
  for (size_t i = 0; i < panes.GetCount(); i++)
  {
    wxAuiPaneInfo & p = panes[i];
    if (p.window && p.IsDocked() && p.IsShown() && !p.window->IsShown())
      p.window->Move(-32000, -32000);
  }
}

void ModelViewer::paintTree(wxWindow * window)
{
  if (!window->IsShown())
    return;
  window->Update();
  for (wxWindow * child : window->GetChildren())
    if (!child->IsTopLevel())
      paintTree(child);
}

void ModelViewer::PaintNow()
{
  paintTree(this);
}

// HIDDEN WINDOWS STAY FROZEN. wx's freeze of a window freezes every window in it, and a viewer-mode switch freezes and
// thaws every window of the frame: in the panels put away too, whose controls each had the redraw turned off and on and
// were refreshed -- 6-27 ms of a switch's turn for the Model, animation and light panels, hidden
// (docs/world-viewer/08-streaming.md, section 8.8). So a window of the frame a switch leaves hidden keeps a freeze of
// its own until it is shown, and the next switches' freezes and thaws only count it (nothing in it is drawn while it is
// hidden either way). Its show thaws it: its wxEVT_SHOW, which a switch's own shows send inside the switch's freeze, so
// the window still changes with everything else; and the idle check, for a show that comes another way.
// Hidden windows inside shown ones too -- the texture view in the viewport's place, the overhead map, the Maps panel
// under Browse's list: a freeze goes on into a hidden window's own windows, and each switch turned the redraw of their
// 40 controls off and on (about a third of the switch's 190 WM_SETREDRAWs, section 8.8). A hidden window without
// windows of its own is not parked: there is nothing in it to freeze, and a tree listed while hidden (Browse's map tree)
// must not read as frozen.
void ModelViewer::parkHidden()
{
  std::function<void(wxWindow *)> visit = [&](wxWindow * parent) {
    for (wxWindow * child : parent->GetChildren())
    {
      if (child->IsTopLevel())
        continue;
      const bool parked = m_parked.count(child) > 0;
      if (!child->IsShown())
      {
        if (!parked && !child->GetChildren().empty())
        {
          if (m_parkWatched.insert(child).second)
          {
            // (Not once the frame's own members are gone: a window the frame's base class destroys.)
            const std::shared_ptr<bool> alive = m_parkAlive;
            child->Bind(wxEVT_SHOW, [this, child, alive](wxShowEvent & e) {
              e.Skip();
              if (*alive && e.IsShown())
                unpark(child);
            });
            child->Bind(wxEVT_DESTROY, [this, child, alive](wxWindowDestroyEvent & e) {
              e.Skip();
              if (*alive && e.GetEventObject() == child)
              {
                m_parked.erase(child);
                m_parkWatched.erase(child);
              }
            });
          }
          child->Freeze();
          m_parked.insert(child);
        }
        // (Nothing in a hidden window is on screen.)
        continue;
      }
      if (parked)
        unpark(child);
      visit(child);
    }
  };
  visit(this);
  if (!m_parked.empty() && !m_parkIdleBound)
  {
    m_parkIdleBound = true;
    Bind(wxEVT_IDLE, &ModelViewer::OnParkedIdle, this);
  }
}

void ModelViewer::unpark(wxWindow * window)
{
  if (m_parked.erase(window))
    window->Thaw();
}

void ModelViewer::OnParkedIdle(wxIdleEvent & event)
{
  event.Skip();
  std::vector<wxWindow *> shown;
  for (wxWindow * w : m_parked)
    if (w->IsShown())
      shown.push_back(w);
  for (wxWindow * w : shown)
    unpark(w);
}

void ModelViewer::SetViewerMode(ViewerMode mode, bool openBrowse, bool paintNow)
{
  // In one call: every caller goes on to work that needs the switch made (a load sizes the player and lays out at once; a
  // texture is shown in the view entered). A switch the command bar left on its way is finished first -- before the
  // same-mode return, which would otherwise leave it half made.
  FinishViewerSwitch("a viewer-mode change or a load");
  if (BeginViewerSwitch(mode, openBrowse, paintNow, false))
    FinishViewerSwitch("its one turn");
}

// THE VIEWER SWITCH (ONE CHANGE ON SCREEN, the comment above beginLayoutBatch), for the command bar's click in turns of
// their own (split), so no GUI round is long:
//   turn A (the click's round, here): every change, behind the frame's freeze, the player held, the frame's own drawing held
//     back (ViewerSwitchFilter); laid out now when the player is parked, and a parked player its release puts back sized now
//     by a thread;
//   the steps (turns of their own, only when due): Browse listed, its rows measured, the overhead map's waiting pictures
//     drawn -- still frozen;
//   turn B (FinishViewerSwitch): the layout when it waited (the player on screen at the click), the thaw, the player let go,
//     the repaint; the tool windows and the caption after it.
// Each next turn is a one-shot timer armed at the first idle after the turn before (OnViewerSwitchIdle): the thread has run
// out of work and waits between them -- in the event loop and the harness's pump alike (a CallAfter, a posted message or
// more idle would run in the same busy stretch). What the user does in between finishes the switch first. Every other
// caller makes it in one call (SetViewerMode: split false).
bool ModelViewer::BeginViewerSwitch(ViewerMode mode, bool openBrowse, bool paintNow, bool split)
{
  if (m_switch)
  {
    // (Every way in finishes a waiting switch first: this is one asked for inside a switch's own turn.)
    LOG_ERROR << "[viewport] a viewer switch asked for inside a viewer switch's turn: refused";
    return false;
  }
  if (mode == m_viewerMode)
    return false;
  wxStopWatch turn;
  m_turnSerial++;
  m_switch.reset(new ViewerSwitch());
  // The turns after the last switch share its timer (runOnLaterTurn): a tick of theirs already on its way is not this
  // switch's next turn -- that one is armed at the first idle after turn A (OnViewerSwitchIdle); theirs again once it is done.
  m_switchTimer.Stop();
  ViewerSwitch & s = *m_switch;
  s.previous = m_viewerMode;
  s.mode = mode;
  s.paintNow = paintNow;
  s.split = split;
  s.loop = wxEventLoopBase::GetActive();
  const bool textures = mode == ViewerMode::Textures;
  m_viewerMode = mode;
  LOG_INFO << "[viewport] viewer mode:"
           << (mode == ViewerMode::Textures ? "Textures" : mode == ViewerMode::Buildings ? "Buildings"
               : mode == ViewerMode::Maps ? "Maps" : "Models")
           << (split ? "(in turns)" : "");
  // THE FREEZE: the frame frozen -- wx freezes its windows, hidden ones too, and one made meanwhile as it is added
  // (wxWindowBase::AddChild); on MSW the top-level window's own freeze does nothing (wxTopLevelWindowMSW::DoFreeze), so the
  // frame stays visible and hit-testable, as ApplyTheme relies on -- and its own drawing held back until the thaw.
  Freeze();
  s.frozen = true;
  s.drawHeld = true;
  s.titleHeld = true;
  beginLayoutBatch();
  // The selector: a switch made by a load from a menu does not come through the toolbar.
  if (commandBar)
    commandBar->ToggleTool(textures ? ID_UI_TEXTURES : mode == ViewerMode::Buildings ? ID_UI_BUILDINGS
                           : mode == ViewerMode::Maps ? ID_UI_MAPS : ID_UI_MODELS,
                           true);
  if (textureView)
  {
    if (textures)
      textureView->entered(split);   // a decode on its way waits for resumeDecode, a turn after the switch
    else if (s.previous == ViewerMode::Textures)
      textureView->left();
  }
  // TOOL WINDOWS of the viewer left -- a chooser a model command left open (View NPC, View Item, an equipment slot, an item
  // set, a start outfit, a mount; picked from in another viewer it would load into the Models viewer), and the Models
  // viewport's background window: greyed to input now and put away after the repaint, with the rest of the old workspace.
  // (Disabling a top-level window greys nothing in it: wxWindowBase::NotifyWindowOnEnableChange.) Models gives the background
  // window back if it was open, after the repaint (without taking the keyboard from the viewer).
  if (s.previous == ViewerMode::Models && charControl && charControl->itemDialog)
  {
    charControl->itemDialog->Disable();
    s.itemDialogAway = true;
  }
  if (m_backgroundDialog)
  {
    if (s.previous == ViewerMode::Models)
    {
      m_backgroundDialogPutAway = m_backgroundDialog->IsShown();
      if (m_backgroundDialogPutAway)
      {
        m_backgroundDialog->Disable();
        s.backgroundAway = true;
      }
    }
    else if (mode == ViewerMode::Models && m_backgroundDialogPutAway)
    {
      m_backgroundDialogPutAway = false;
      s.backgroundBack = true;
    }
  }
  // (The viewport's clear colour follows what the player draws, not the viewer: a switch sends none.)
  // The centre, the panels and the menus (UpdateInterface -> DisplayedContentChanged -> UpdateUnityViewportState puts the
  // panels away or gives them back), and Browse opened when asked.
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
  // BROWSE follows: its mode, tree, search and controls now; its rows listed now -- or, split, on a turn of their own, as
  // is the native tree's measure of rows changed while frozen, and the overhead map's pictures that came while it was away.
  if (fileControl && fileControl->FollowViewerMode(mode, !split))
    s.steps.push_back(ViewerSwitch::Step::Listing);
  else if (split && fileControl && fileControl->RowsToSettle() > 0)
    s.steps.push_back(ViewerSwitch::Step::SettleRows);
  // THE LAYOUT now -- its drawing held back (WMV_SWITCH_LAYOUT=b puts it with the repaint instead).
  const wxString & rule = viewerSwitchLayoutRule();
  s.layoutInB = split && rule == wxT("b");
  if (!s.layoutInB)
  {
    commitLayoutBatchNow();   // the batch stays open: what is asked for meanwhile is laid out in turn B
    parkHidden();
  }
  // The panel toggles answered now, not one idle later, so they change with everything else; and the menus, which wx
  // answers only when one opens, while a shortcut is checked against its menu item.
  if (commandBar)
    commandBar->UpdateWindowUI(wxUPDATE_UI_FROMIDLE);
  DoMenuUpdates();
  // THE KEYBOARD OFF A WINDOW THE SWITCH PUTS AWAY, now (split): a key pressed before the repaint finishes the switch first,
  // but the system has already given it to the window with the keyboard.
  if (split && fileControl && fileControl->txtContent)
    if (wxWindow * focus = wxWindow::FindFocus())
      if (focus != fileControl->txtContent && !willBeShownOnScreen(focus) && willBeShownOnScreen(fileControl->txtContent))
        fileControl->txtContent->SetFocus();
  viewerSwitchTurnDone(turn.TimeInMicro().ToDouble() / 1000.0);
  return true;
}

void ModelViewer::viewerSwitchTurnDone(double ms)
{
  ViewerSwitch & s = *m_switch;
  s.turnMs.push_back(ms);
  s.phase = ViewerSwitch::Phase::Waiting;
  s.timerArmed = false;
  s.sinceTurn.Start();
}

double ModelViewer::viewerSwitchStepEstimateMs(ViewerSwitch::Step step) const
{
  switch (step)
  {
    case ViewerSwitch::Step::Listing:
      return 1e9;   // 11 ms to 750 ms, unknown ahead: a turn of its own
    case ViewerSwitch::Step::SettleRows:
      return fileControl ? fileControl->RowsToSettle() * 0.02 : 0.0;   // ~20 us a row (BrowseTree, filecontrol.cpp)
    case ViewerSwitch::Step::Mosaic:
      return 0.0;
  }
  return 1e9;
}

void ModelViewer::runViewerSwitchStep(ViewerSwitch::Step step)
{
  ViewerSwitch & s = *m_switch;
  switch (step)
  {
    case ViewerSwitch::Step::Listing:
      if (fileControl)
      {
        fileControl->ListForViewerMode();
        if (fileControl->RowsToSettle() > 0 &&
            std::find(s.steps.begin(), s.steps.end(), ViewerSwitch::Step::SettleRows) == s.steps.end())
          s.steps.push_front(ViewerSwitch::Step::SettleRows);
      }
      break;
    case ViewerSwitch::Step::SettleRows:
      if (fileControl)
        fileControl->SettleRows();
      break;
    case ViewerSwitch::Step::Mosaic:
      break;
  }
}

// The steps due, as many as fit one turn (all: every one now -- the switch finished before their turns came). The first of
// a turn always runs; the listing never follows another in a turn.
void ModelViewer::runViewerSwitchSteps(bool all)
{
  ViewerSwitch & s = *m_switch;
  wxStopWatch turn;
  bool ran = false;
  while (!s.steps.empty())
  {
    const ViewerSwitch::Step step = s.steps.front();
    if (!all && ran && turn.TimeInMicro().ToDouble() / 1000.0 + viewerSwitchStepEstimateMs(step) > ViewerSwitchTurnBudgetMs)
      return;
    s.steps.pop_front();
    runViewerSwitchStep(step);
    ran = true;
  }
}

// What became due after turn A planned the steps (a client load's deferred interface refresh listing Browse, rows changed
// while frozen): run before the repaint, which keeps its turn to itself.
void ModelViewer::queueLateViewerSwitchSteps()
{
  ViewerSwitch & s = *m_switch;
  if (!s.split || !fileControl)
    return;
  const auto queued = [&](ViewerSwitch::Step step) { return std::find(s.steps.begin(), s.steps.end(), step) != s.steps.end(); };
  if (fileControl->listingDue() && !queued(ViewerSwitch::Step::Listing))
    s.steps.push_front(ViewerSwitch::Step::Listing);
  else if (!fileControl->listingDue() && fileControl->RowsToSettle() > 0 && !queued(ViewerSwitch::Step::SettleRows))
    s.steps.push_front(ViewerSwitch::Step::SettleRows);
}

void ModelViewer::FinishViewerSwitch(const char * why)
{
  if (!m_switch || m_switch->phase != ViewerSwitch::Phase::Waiting)
    return;   // none; or one of its own turns is running (A, a step, B): that turn ends as planned
  // NOTHING IS DESTROYED HERE: this runs inside the event filter and wx's keyboard hook, with the event that finished the
  // switch still to be handled (the item chooser goes on a later turn).
  ViewerSwitch & s = *m_switch;
  s.phase = ViewerSwitch::Phase::Finishing;
  m_switchTimer.Stop();
  m_turnSerial++;
  wxStopWatch turn;
  const bool textures = s.mode == ViewerMode::Textures;
  // Finished before the steps' turns came (input, a load, another switch, closing): their work now, in this turn.
  queueLateViewerSwitchSteps();
  runViewerSwitchSteps(true);
  // THE LAYOUT when it waited for this turn, and what was asked for since turn A: the batch closes -- one
  // wxAuiManager::Update at most, its drawing still held back, the floating panels held as they are (commitLayoutBatchNow).
  endLayoutBatch();
  // What this switch leaves hidden stays frozen past the thaw below.
  parkHidden();
  // The floating panels' panes back to what is due (their windows change after the repaint), and the toggles that show it.
  if (!s.floatingDue.empty())
  {
    restoreFloatingPanes();
    if (commandBar)
      commandBar->UpdateWindowUI(wxUPDATE_UI_FROMIDLE);
    DoMenuUpdates();
  }
  // THE FREEZE OVER: the frame's windows thawed (each invalidated), the frame's own drawing back and its area invalidated
  // (what it was asked to paint since turn A was validated unpainted).
  if (s.frozen)
  {
    s.frozen = false;
    Thaw();
  }
  s.drawHeld = false;
#ifdef _WINDOWS
  ::RedrawWindow((HWND)GetHWND(), nullptr, nullptr, RDW_INVALIDATE | RDW_NOCHILDREN);
#else
  Refresh(false);
#endif
  // THE KEYBOARD before the repaint, so what a focus change repaints is painted with the rest -- not while a modal window has
  // the program (a split switch's turn can come inside its loop; the keyboard is its).
  const bool takesInput = !s.split || frameTakesInputNow();
  if (takesInput)
  {
    // A window that had the keyboard and was put away while frozen keeps it: the keyboard to Browse's search box, or the
    // frame.
    if (wxWindow * focus = wxWindow::FindFocus())
      if (!focus->IsShownOnScreen())
      {
        if (fileControl && fileControl->txtContent && fileControl->txtContent->IsShownOnScreen())
          fileControl->txtContent->SetFocus();
        else
          SetFocus();
      }
    if (s.focusBrowse)
      focusBrowseFor(s.mode);   // the command bar's: the keyboard to Browse
  }
  if (s.paintNow)
    PaintNow();
  // AFTER THE REPAINT, what is outside the frame's client area: the caption, the floating panels' windows, the tool windows.
  s.titleHeld = false;
  UpdateTitle();
  bool unmade = false;
  for (const std::pair<wxString, bool> & due : s.floatingDue)
  {
    wxAuiPaneInfo & p = interfaceManager.GetPane(due.first);
    if (!p.IsOk())
      continue;
    if (p.frame)
      p.frame->Show(due.second);   // what wxAuiManager::Update does for it (framemanager.cpp 3252)
    else if (due.second)
      unmade = true;
  }
  if (unmade)
    interfaceManager.Update();   // (not expected: the switch's layout makes every floating panel's frame, 3194-3212)
  if (s.backgroundAway && m_backgroundDialog)
  {
    m_backgroundDialog->Hide();
    m_backgroundDialog->Enable();
  }
  if (s.backgroundBack && m_backgroundDialog && isModelsMode())
  {
    m_backgroundDialog->Sync();
    m_backgroundDialog->ShowWithoutActivating();
  }
  if (s.itemDialogAway && charControl && charControl->itemDialog)
  {
    wxWindow * chooser = charControl->itemDialog;
    chooser->Hide();
    if (s.split)
    {
      const wxWeakRef<wxWindow> gone(chooser);
      runOnLaterTurn([this, gone]() {
        if (gone && charControl && static_cast<wxWindow *>(charControl->itemDialog) == gone.get())
          charControl->ClearItemDialog();
        return false;
      });
    }
    else
      charControl->ClearItemDialog();
  }
  // THE TURNS AFTER THE SWITCH: what would lengthen its rounds.
  if (s.split)
  {
    if (textureView && textures)
      runOnLaterTurn([this]() {
        if (textureView)
          textureView->resumeDecode();
        return false;
      });
  }
  s.turnMs.push_back(turn.TimeInMicro().ToDouble() / 1000.0);
  m_lastSwitch = ViewerSwitchReport();
  m_lastSwitch.to = s.mode == ViewerMode::Textures ? "Textures" : s.mode == ViewerMode::Buildings ? "Buildings"
                  : s.mode == ViewerMode::Maps ? "Maps" : "Models";
  m_lastSwitch.finishedBy = why;
  m_lastSwitch.turns = (int)s.turnMs.size();
  m_lastSwitch.turnMs = s.turnMs;
  m_lastSwitch.clickToRepaintMs = s.sinceClick.Time();
  m_lastSwitch.split = s.split;
  m_lastSwitch.layoutInB = s.layoutInB;
  if (s.split)
  {
    QString list;
    for (double ms : s.turnMs)
      list += QString(list.isEmpty() ? "%1" : " %1").arg(ms, 0, 'f', 1);
    LOG_INFO << "[viewport] viewer switch done (" << why << "):" << m_lastSwitch.turns << "turn(s), ms:" << list << ";"
             << m_lastSwitch.clickToRepaintMs << "ms from the click to the repaint; laid out in turn" << (s.layoutInB ? "B" : "A");
  }
  m_switch.reset();
}

// A switch dropped without its repaint: the frame being deleted (~ModelViewer; OnClose finishes one). Its freeze, batch and
// floating panels' state let go of (the layout saved is the user's); the player's hold only counted down -- its release
// would move, size and paint the player during the teardown.
void ModelViewer::AbortViewerSwitch()
{
  if (!m_switch)
    return;
  ViewerSwitch & s = *m_switch;
  s.phase = ViewerSwitch::Phase::Finishing;
  m_switchTimer.Stop();
  m_layoutBatch = 0;   // (the switch's batch is the only one: SetViewerMode was its only user)
  m_layoutPending = m_layoutUncapped = m_finishWorkspacePending = m_uncoverPending = false;
  restoreFloatingPanes();
  if (s.frozen)
  {
    s.frozen = false;
    Thaw();
  }
  s.drawHeld = false;
  if (s.backgroundAway && m_backgroundDialog)
    m_backgroundDialog->Enable();
  if (s.itemDialogAway && charControl && charControl->itemDialog)
    charControl->itemDialog->Enable();
  LOG_INFO << "[viewport] a viewer switch dropped without its repaint: the window is going";
  m_switch.reset();
}

void ModelViewer::OnViewerSwitchIdle(wxIdleEvent & event)
{
  event.Skip();
  if (IsBeingDeleted() || wxTheApp->IsScheduledForDestruction(this))
    return;
  if (m_switch)
  {
    ViewerSwitch & s = *m_switch;
    if (s.phase != ViewerSwitch::Phase::Waiting || !s.split)
      return;
    // Armed now, at the first idle after a turn -- the thread has run out of work, its paints included, and waits next -- so
    // the next turn is a round of its own (Windows makes the delay at least 10 ms).
    if (!s.timerArmed)
    {
      s.timerArmed = true;
      m_switchTimer.StartOnce(viewerSwitchHoldMs() > 0 ? (int)viewerSwitchHoldMs() : 1);
    }
    else if (!m_switchTimer.IsRunning())
      m_switchTimer.StartOnce(1);   // its tick lost (a yield that drops one-shot timers' messages)
    else if (s.sinceTurn.Time() > ViewerSwitchGapLimitMs + viewerSwitchHoldMs())
      FinishViewerSwitch("its turn never came");
    return;
  }
  if (!m_later.empty() && !m_switchTimer.IsRunning())
    m_switchTimer.StartOnce(1);
}

void ModelViewer::OnViewerSwitchTimer(wxTimerEvent & WXUNUSED(event))
{
  if (IsBeingDeleted() || wxTheApp->IsScheduledForDestruction(this))
    return;
  m_turnSerial++;
  if (m_switch)
  {
    ViewerSwitch & s = *m_switch;
    if (s.phase != ViewerSwitch::Phase::Waiting)
      return;
    s.timerArmed = false;
    // Inside another event loop (a modal window, a native one's tick): the rest at once, not as a turn behind it.
    if (wxEventLoopBase::GetActive() != s.loop)
    {
      FinishViewerSwitch("another event loop is running");
      return;
    }
    queueLateViewerSwitchSteps();
    if (!s.steps.empty())
    {
      wxStopWatch turn;
      s.phase = ViewerSwitch::Phase::Stepping;
      runViewerSwitchSteps(false);
      viewerSwitchTurnDone(turn.TimeInMicro().ToDouble() / 1000.0);
      return;
    }
    FinishViewerSwitch("its turn");
    return;
  }
  // THE TURNS AFTER A SWITCH (runOnLaterTurn): work until LaterTurnShareMs; the rest on the next turn.
  wxStopWatch turn;
  while (!m_later.empty())
  {
    std::function<bool()> work = std::move(m_later.front());
    m_later.pop_front();
    if (work())
    {
      m_later.push_front(std::move(work));
      break;
    }
    if (turn.Time() >= LaterTurnShareMs)
      break;
  }
}

void ModelViewer::runOnLaterTurn(std::function<bool()> work)
{
  m_later.push_back(std::move(work));
  wxWakeUpIdle();   // (armed at the next idle: OnViewerSwitchIdle)
}

const char * ModelViewer::viewerSwitchPhaseName() const
{
  if (!m_switch)
    return "none";
  switch (m_switch->phase)
  {
    case ViewerSwitch::Phase::Building: return "building";
    case ViewerSwitch::Phase::Waiting: return "waiting";
    case ViewerSwitch::Phase::Stepping: return "stepping";
    case ViewerSwitch::Phase::Finishing: return "finishing";
  }
  return "?";
}

bool ModelViewer::frameTakesInputNow() const
{
#ifdef _WINDOWS
  return ::IsWindowEnabled((HWND)GetHWND()) != FALSE;   // a modal window, wx's or Windows' own, disables it natively
#else
  return IsEnabled();
#endif
}

// A panel the switch changes (Browse, Animation, Model), floated: a top-level window of its own, which the frame's freeze
// skips (wxWindowBase::Freeze) and whose drawing ViewerSwitchFilter does not hold -- what turn A changes in it would be on
// screen a turn before the rest. The command bar's switch is made in one turn then.
bool ModelViewer::floatedPanelChanges()
{
  for (wxWindow * w : { static_cast<wxWindow *>(fileControl), static_cast<wxWindow *>(animControl),
                        static_cast<wxWindow *>(modelInspector) })
  {
    if (!w)
      continue;
    const wxAuiPaneInfo & p = interfaceManager.GetPane(w);
    if (p.IsOk() && p.IsFloating() && p.frame && p.frame->IsShown())
      return true;
  }
  return false;
}

// OnCommandBar's keyboard to Browse: its tree when a row is picked there (the model loaded, the texture shown), to go on
// from it; otherwise its search box.
void ModelViewer::focusBrowseFor(ViewerMode mode)
{
  if (m_viewerMode != mode || !fileControl || !interfaceManager.GetPane(fileControl).IsShown())
    return;
  if (fileControl->fileTree->GetSelection().IsOk())
    fileControl->fileTree->SetFocus();
  else if (fileControl->txtContent)
    fileControl->txtContent->SetFocus();
}

// Whether the layout due moves the viewport's top-left corner or makes it smaller: a docked panel shown (it takes room from
// the centre), or one docked left or at the top hidden. (WMV_SWITCH_LAYOUT=grow only: the F experiment.)
bool ModelViewer::layoutMovesViewport()
{
  wxAuiPaneInfoArray & panes = interfaceManager.GetAllPanes();
  for (size_t i = 0; i < panes.GetCount(); i++)
  {
    const wxAuiPaneInfo & p = panes[i];
    if (!p.window || p.IsFloating() || p.dock_direction == wxAUI_DOCK_CENTER)
      continue;
    const bool showing = p.IsShown() && !p.window->IsShown();
    const bool hiding = !p.IsShown() && p.window->IsShown();
    if (showing || (hiding && (p.dock_direction == wxAUI_DOCK_LEFT || p.dock_direction == wxAUI_DOCK_TOP)))
      return true;
  }
  return false;
}

// Whether `window` is on screen once the layout due is made: its panel's shown state (the layout follows it), and shown
// itself up to it.
bool ModelViewer::willBeShownOnScreen(wxWindow * window)
{
  for (wxWindow * w = window; w && w != this; w = w->GetParent())
  {
    if (w->IsTopLevel())
      return w->IsShown();
    if (w->GetParent() == this)
    {
      const wxAuiPaneInfo & pane = interfaceManager.GetPane(w);
      return pane.IsOk() ? pane.IsShown() : w->IsShown();
    }
    if (!w->IsShown())
      return false;
  }
  return true;
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
    case ID_UI_MAPS:
    {
      // A client load yields to the event loop (its progress dialog), so this can be clicked in the middle
      // of one; switching Browse's lists then, or starting another load, is not safe. The selector goes
      // back to the mode at the next idle update.
      if (ClientState::isClientLoading())
        return;
      // From the id only, never the tool's checked state: a click on the active radio tool, a double-click
      // and the empty viewer's posted command all arrive unchecked.
      const ViewerMode mode = event.GetId() == ID_UI_TEXTURES    ? ViewerMode::Textures
                            : event.GetId() == ID_UI_BUILDINGS   ? ViewerMode::Buildings
                            : event.GetId() == ID_UI_MAPS        ? ViewerMode::Maps
                                                                 : ViewerMode::Models;
      // A switch the last click left on its way is finished first (a click before its repaint).
      FinishViewerSwitch("another viewer-mode click");
      // Browse (closed) opens in the same layout as the switch, when there is a client to list in it.
      const bool openBrowse = ClientState::hasActiveClient() && !interfaceManager.GetPane(fileControl).IsShown();
      // WITH A CLIENT, IN TURNS OF ITS OWN (BeginViewerSwitch): made behind the freeze now, the screen changed on a later
      // turn, where the keyboard then goes to Browse. Not without an event loop to run its turns, nor while a modal window
      // has the program (its loop would run them), nor while a panel it changes floats (floatedPanelChanges), nor for the
      // one-turn comparison runs (WMV_SWITCH_TURNS=1). Browse, opened in the switch, needs no layout here.
      if (ClientState::hasActiveClient() && !viewerSwitchOneTurn() && wxEventLoopBase::GetActive() &&
          frameTakesInputNow() && !floatedPanelChanges() && BeginViewerSwitch(mode, openBrowse, true, true))
      {
        m_switch->focusBrowse = true;
        break;
      }
      // Without a client -- made whole now, before the prompt (its modal loop would run the turns behind it) -- or the mode
      // already this one: in one call, as before.
      SetViewerMode(mode, openBrowse, true);
      if (!ClientState::hasActiveClient())
      {
        PromptAndLoadClient();
        UpdateViewportState();
        if (!ClientState::hasActiveClient())
          return;
      }
      wxAuiPaneInfo & browse = interfaceManager.GetPane(fileControl);
      if (!browse.IsShown())
      {
        browse.Show(true);
        interfaceManager.Update();
      }
      // The keyboard to Browse (focusBrowseFor).
      focusBrowseFor(mode);
      break;
    }
    case ID_UI_SCREENSHOT:
      SaveScreenshot();
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

void ModelViewer::SaveScreenshot()
{
  wxFileDialog dialog(this, _("Save screenshot"), wxEmptyString, DefaultScreenshotName(), _("PNG Image (*.png)|*.png"),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dialog.ShowModal() != wxID_OK)
    return;
  wxString why;
  RequestScreenshot(dialog.GetPath(), why);
}

bool ModelViewer::RequestScreenshot(const wxString & requested, wxString & why)
{
  why.clear();
  wxFileName file(requested);
  if (!file.GetExt().IsSameAs(wxT("png"), false))
    file.SetFullName(file.GetFullName() + wxT(".png"));
  if (!canvas)
    why = _("there is no viewport");
  else if (!isViewportShowingModel())
    why = _("nothing is shown in the viewport");
  else if (!file.IsAbsolute() || file.GetName().IsEmpty())
    why = _("not a full file name: ") + requested;
  else if (!file.DirExists())
    why = _("the folder does not exist: ") + file.GetPath();

  // A transparent PNG at the screenshot size: the same matte as an image sequence's frames.
  if (why.IsEmpty() && !canvas->CaptureSequenceFrame(file.GetFullPath(), SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT, 0, true))
    why = _("the frame could not be drawn or written");

  if (!why.IsEmpty())
  {
    LOG_ERROR << "Screenshot not taken:" << QString::fromWCharArray(why.wc_str());
    if (GetStatusBar())
      SetStatusText(_("Screenshot failed: ") + why, 0);
    return false;
  }
  LOG_INFO << "Screenshot saved:" << QString::fromWCharArray(file.GetFullPath().wc_str());
  if (GetStatusBar())
    SetStatusText(_("Screenshot saved: ") + file.GetFullPath(), 0);
  return true;
}

void ModelViewer::OnUpdateCommandUI(wxUpdateUIEvent & event)
{
  // A command that acts on a model: available in the Models viewer only; the Model panel in Models and
  // Buildings. None of these has a condition of its own to keep (Export Model checks for a model when
  // chosen), so this is their whole state; a command given one later combines it with this.
  if (needsModelViewer(event.GetId()))
    event.Enable(isModelsMode() && capabilityRefusal(event.GetId()).isEmpty());
  else if (needsViewport(event.GetId()))
    event.Enable(!isTextureMode() && !isMapsMode());
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
      // Something to capture.
      event.Enable(isViewportShowingModel());
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
    case ID_UI_MAPS:
      event.Check(m_viewerMode == ViewerMode::Maps);
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
    case ID_UI_MAPS:
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
    case ID_UI_MAPS:
      return caps.maps ? QString()
                       : QString("%1 has no map table this viewer reads (%2).")
                           .arg(client, m_mapCatalog ? m_mapCatalog->statusDetail().isEmpty()
                                                         ? wow::WorldMapCatalog::statusName(m_mapCatalog->status())
                                                         : m_mapCatalog->statusDetail()
                                                     : QString("no catalogue"));
  }
  return QString();
}

bool ModelViewer::needsViewport(int id) const
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
      ((!isModelsMode() && needsModelViewer(event.GetId())) ||
       ((isTextureMode() || isMapsMode()) && needsViewport(event.GetId()))))
  {
    LOG_INFO << "[viewport] a command refused in the"
             << (isTextureMode() ? "Textures" : isBuildingsMode() ? "Buildings" : isMapsMode() ? "Maps" : "Models") << "viewer, id"
             << event.GetId();
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
  // A viewer switch on its way: the caption changes after its repaint (FinishViewerSwitch).
  if (m_switch && m_switch->titleHeld)
    return;
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
#ifdef __WXMSW__
  ::RedrawWindow((HWND)GetHWND(), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
#else
  Refresh();
#endif
}

void ModelViewer::OnThemeChangedOutside()
{
  FinishViewerSwitch("a theme change");   // (ApplyTheme may recreate the status bar)
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
#ifdef __WXMSW__
  UseNativeStatusBar(!generic);
#endif
  CreateStatusBar(fields, generic ? (wxSTB_DEFAULT_STYLE & ~wxSTB_SHOW_TIPS) : wxSTB_DEFAULT_STYLE);
#ifdef __WXMSW__
  UseNativeStatusBar(true);
#endif
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

#ifdef __WXMSW__
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
#endif

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
  // The character model for an imported race and sex, or null when this build has none. A race with one model for both
  // sexes has it under GENDER_ANY (ChrModel.Sex 3: the Dracthyr's dragon form, races 52 and 70), and sex is then
  // GENDER_ANY too -- the key the race is read under when the model is loaded (WoWModel::setRaceSex).
  GameFile * armoryRaceModel(const CharInfos & info, int & sex)
  {
    sex = (info.gender == "Male") ? GENDER_MALE : GENDER_FEMALE;
    int raceModelFileID = RaceInfos::getFileIDForRaceSex(info.raceId, sex);
    if (raceModelFileID <= 0)
    {
      const int anySexFileID = RaceInfos::getFileIDForRaceSex(info.raceId, GENDER_ANY);
      if (anySexFileID > 0)
      {
        sex = GENDER_ANY;
        raceModelFileID = anySexFileID;
      }
    }
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

    // A model that serves both sexes (the Dracthyr's dragon form) wears items made per body sex: the character's own,
    // told before anything is equipped (WoWModel::itemSexID). Other models' item sex is their race's.
    g_charControl->model->setBodySex((result->gender == "Male") ? GENDER_MALE : GENDER_FEMALE);

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
  else
    state.enabled = true;
  return state;
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
  if (ClientState::isClientLoading())
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

  std::wstring exporterLabel = fileMenu->GetLabel(event.GetId()).ToStdWstring();

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
      bool optCustomizations = false, optEquipment = false;
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

        std::wstring customizationReason;
        const bool supportsCustomization = plugin->canExportCustomizations(m, customizationReason);
        AnimationExportChoiceDialog animChoiceDlg(this, L"", wxT("FBX Export Options"), values, animationIds,
          supportsCustomization, wxString(customizationReason));
        if (animChoiceDlg.ShowModal() == wxID_CANCEL)
          return;

        optMesh = animChoiceDlg.exportMesh();
        optSkel = animChoiceDlg.exportSkeleton();
        optSkin = animChoiceDlg.exportSkinning();
        optAnim = animChoiceDlg.exportAnimations();
        optCustomizations = animChoiceDlg.exportAllCustomizations();
        optEquipment = animChoiceDlg.includeEquipment();

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
        if (PrepareFbxAsset(assetArgs, assetLabel, tempCharPath, optCustomizations))
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
          req.allCustomizations = optCustomizations;
          req.includeEquipment = optEquipment;
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
      plugin->setCustomizationExport(optCustomizations, optEquipment, m_loadedBuild.toStdString(), m_loadedProduct.toStdString());
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

}
