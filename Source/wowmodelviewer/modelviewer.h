
#ifndef MODELVIEWER_H
#define MODELVIEWER_H

// wx
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#if defined(__WIN32__) && !defined(__WIN__)
#endif


//wxAUI
#include <wx/aui/aui.h>
#include <wx/evtloop.h>
#include <wx/eventfilter.h>

// Our files
#include "modelcanvas.h"
#include "animcontrol.h"
#include "ViewerMode.h"
#include "charcontrol.h"
#include "lightcontrol.h"
#include "modelcontrol.h"
#include "effects.h"
#include "filecontrol.h"
#include "UnityIpcServer.h"

#include "glm/glm.hpp"

#include <functional>
#include <memory>
#include <set>
#include <deque>
#include <vector>

#include <QString>
#include <QStringList>
#include <QVariantMap>

class SettingsControl;
class ModelInspector;
class wxAuiToolBar;
class ExportJobManager;
class UnityRendererHost;
class TextureView;
class BackgroundColorDialog;
namespace ModelIdLookup { struct Resolved; }
namespace wow { class WorldMapCatalog; class LiquidCatalog; class LightCatalog; }
struct TextureEntry;
class CharInfos;

namespace core { class GameConfig; }

namespace WMVLog
{
  class Logger;
}

class UiMenuBarTitles;

class ClientLoadProgress;
class MapsViewer;
class MapOverview;

class ModelViewer: public wxFrame
{    
    DECLARE_CLASS(ModelViewer)
    DECLARE_EVENT_TABLE()

    void OnStatusBarRefreshTimer(wxTimerEvent& event);
    wxTimer timer;

public:
  // Constructor + Deconstructor
  ModelViewer();
  virtual ~ModelViewer();

  // our class objects
  AnimControl *animControl;
  ModelCanvas *canvas;
  CharControl *charControl;
  EnchantsDialog *enchants;
  LightControl *lightControl;
  ModelControl *modelControl;
  // The "Model" panel: Appearance / Geosets / Info for whatever is loaded. See ModelInspector.h.
  ModelInspector *modelInspector;
  //SoundControl *soundControl;
  SettingsControl *settingsControl;
  // The Unity viewport: the application's only viewport, docked as the centre pane at construction
  // (InitDocking) and never removed. The player it embeds is started by WarmStartUnityViewport (or
  // by the -unityipctest self-test); what the panel shows instead of the player -- the empty viewer,
  // a notice for content it cannot draw yet, a stopped player -- is decided by
  // UpdateUnityViewportState. The canvas above is archived: hidden, never painted, kept for its GL
  // context, the loaded model and the animation clock. See UnityRendererHost.h and
  // docs/unity-renderer/README.md.
  UnityRendererHost *unityRendererHost;
  // timeGetTime() of the last playback-state push, for the heartbeat in
  // SendAnimationStateToUnity. 0 until the first push.
  unsigned long m_lastAnimStatePush;

  FileControl *fileControl;

  //wxWidget objects
  wxMenuBar *menuBar;
  wxMenu *fileMenu, *exportMenu, *charMenu, *charGlowMenu, *viewMenu, *optMenu;

  // wxAUI - new docking lib (now part of wxWidgets 2.8.0)
  wxAuiManager interfaceManager;
  wxAuiToolBar * commandBar = nullptr;
  wxStaticText * commandModelLabel = nullptr;

  // Boolean flags
  bool isWoWLoaded;
  bool isModel;
  bool isChar;
  bool isWMO;
  bool initDB;
  // Set for non-interactive CLI / headless test runs (-mo/-armory/-npc). Suppresses modal
  // dialogs that would otherwise block a headless run.
  bool batchMode = false;

  // FBX export runs out-of-process (see ExportJobManager). These remember enough about the
  // currently displayed model for a fresh child process to reload exactly the same asset:
  //   - m_loadedBuild: the build version LoadWoW settled on, passed as -build so the child
  //     loads the SAME game data (e.g. a pinned PTR build, not the auto-picked retail one).
  //   - m_exportNpcId/m_exportNpcDisplayId: set when an NPC is shown so the child can
  //     reconstruct it via -npc; cleared (-1) when a plain model or character is loaded.
  //   - m_exportItemSkinFileId: the skin (TEXTURE_OBJECT_SKIN) texture FileDataID applied when an
  //     item/weapon is shown via LoadItem, so the child can re-bind it via -itemskin instead of
  //     re-loading the raw model with its DEFAULT skin; 0 when no item skin is active.
  // Characters are reconstructed by serialising the live customisation to a temp .chr.
  QString m_loadedBuild;
  QString m_loadedProduct;   // the product LoadWoW loaded (-product for the export child)
  QString m_clientSchema;    // the games/wow/<dir> the database was read with ("" = none)
  bool m_clientLoading = false;
  ClientLoadProgress * m_loadProgress = nullptr; // the load under way's loading page (LoadWoW sets it)
  // The load's timeline notes "interactive" at the first idle after the interface follows the loaded client.
  void NoteInteractiveWhenIdle();
  void OnIdleAfterLoad(wxIdleEvent & event);
  bool m_interactivePending = false;
  // The previous client's files a load detached, freed while the viewer is idle after it.
  void FreeDetachedWhenIdle();
  void OnIdleFreeDetached(wxIdleEvent & event);
  bool m_freeingDetached = false;
  // A newly opened client replaces what is on screen and every cache filled from the previous one.
  void ResetClientState();
  void ComputeClientCapabilities();
  // Config.ini: the installation and product loaded last (the chooser marks it "Last used").
  void RememberLoadedClient(const core::GameConfig & config);
  int m_exportNpcId = -1;
  int m_exportNpcDisplayId = 0;
  int m_exportItemSkinFileId = 0;

  ExportJobManager * m_exportJobManager = nullptr;
  // A texture picked in Browse (Textures mode), shown in the viewport's place: the Unity host's
  // content window (UnityRendererHost::showContent). Created with the viewport.
  TextureView * textureView = nullptr;
  // The Maps viewer's overhead map, in the viewport's place until a world is entered (MapOverview.h).
  MapOverview * mapOverview = nullptr;

  // Initialising related functions
  void InitMenu();
  void InitObjects();
  void InitDocking();
  void InitDatabase();

  // Save and load various settings between sessions
  void LoadSession();
  void SaveSession();
  // Save and load the GUI layout
  void LoadLayout();
  void SaveLayout();
  void ResetLayout();
  // Bumped when the panel arrangement changes, so an older saved perspective is not applied to
  // panes it does not describe. See LoadLayout. 3: the OpenGL canvas is no longer a pane and the
  // Unity viewport is always the centre pane, so a perspective naming a "canvas" pane is discarded.
  static const int LAYOUT_VERSION = 3;
  // save + load character *.CHR files
  void LoadChar(QString fn, bool equipmentOnly = false);
  bool SaveChar(QString fn, bool equipmentOnly = false);
  // FBX child-process state; equipment restoration must not customize an exclusive NPC's body.
  bool LoadFbxEquipment(QString fn);
  bool PrepareFbxAsset(wxString & args, wxString & label, wxString & tempCharPath, bool customizations = false);

  // raceID/sexID name the race the model should be read as, for the races that share a model
  // file with another race (Mag'har Orc on the Orc model); -1 leaves the model to resolve it.
  void LoadModel(GameFile * f, int raceID = -1, int sexID = -1);
  void LoadItem(unsigned int displayID);
  // The component-geoset state an item's own model should be shown with. See the definition.
  void applyItemComponentGeosets(unsigned int itemId);
  void LoadNPC(unsigned int modelid);
  // Frees the character riding the mount on the canvas, if one is: the canvas frees only its own model, the mount.
  // Called where a model is replaced or cleared, before the canvas model goes.
  void ReleaseRider();
  // Register an NPC in the in-memory DB (if not already present) and load it. Shared by the
  // Load NPC / Model dialog's link flow and the -npc headless test harness.
  void LoadNPCByDisplay(int npcId, int displayId, int type = 0, const QString & name = QString("npc"));
  // A creature display's model and appearance, for an NPC (LoadNPC) and a Creature Display ID; see the definition. Only
  // on a cleared canvas (both callers clear it first): LoadModel keeps a model already showing the same file.
  bool ShowCreatureDisplay(int fileDataId, int extraId, int displayId);
  // Character > Load NPC / Model..., by ID: shows what ModelIdLookup::resolve found. False, with why, when the model on
  // the canvas afterwards is not that file.
  bool LoadModelById(const ModelIdLookup::Resolved & resolved, wxString & why);

  // Window GUI event related functions
  //void OnIdle();
  void OnClose(wxCloseEvent &event);
  void OnSize(wxSizeEvent &event);
  void OnExit(wxCommandEvent &event);
  void OnRestart(wxCommandEvent &event); // File > Restart: relaunch the app in one click
  void Relaunch();                       // close this instance and start a new one
  bool exportRunning() const;            // an export's child process is still at work
  bool canCloseNow() const;              // not inside a modal dialog's loop or a yield
  wxString m_startDirectory = wxGetCwd(); // the working directory the viewer was started in
  void UpdateCanvasStatus();

  // menu commands
  void OnToggleDock(wxCommandEvent &event);
  void OnChildFocus(wxChildFocusEvent & event);
  void OnAppearance(wxCommandEvent & event);
  void OnSysColourChanged(wxSysColourChangedEvent & event);
  void OnThemeRecheck(wxTimerEvent & event);
  WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;
protected:
  // A command of another viewer (needsModelViewer, needsUnityViewport) is refused before any handler sees it.
  bool TryBefore(wxEvent & event) wxOVERRIDE;
public:
  // The palette resolved again (in a light run: high contrast on or off) and, when it changed, applied to
  // every window. Light and dark themselves are fixed for the run (UiStyle.h).
  void ApplyTheme();
  // Light and dark are fixed for a run: a change that asks for the other one is offered as a restart.
  void OfferThemeRestart(bool chosen);
  void OnThemeChangedOutside();
  int m_themeRestartOffered = -1;   // the appearance (0 light, 1 dark) a restart was last offered for
  bool m_themeOfferChosen = false;  // a restart offer for the user's own choice, put off (canCloseNow)
  // The status bar of the run: Windows' own in a light run and in high contrast (as always), wx's own in
  // a dark run, where it can take the palette (Windows' own dark status bar is black).
  void CreateThemedStatusBar();

  // THE LAYOUT BATCH (a viewer-mode switch). While one is open, CommitLayoutIfChanged and
  // commitDocksAtTheirSize only note that the layout has to be committed; closing the outermost batch
  // commits it with exactly one wxAuiManager::Update(). See BeginViewerSwitch. The command bar's switch
  // keeps it open from its first turn to its repaint.
  void beginLayoutBatch();
  void endLayoutBatch();
  int m_layoutBatch = 0;
  bool m_layoutPending = false;          // an Update is due when the batch closes
  bool m_layoutUncapped = false;         // ... with the dock size cap lifted (docks made again)
  bool m_finishWorkspacePending = false;   // applyWorkspace's second half, after that Update
  bool m_uncoverPending = false;          // the texture view put away once the layout is committed
  void finishWorkspace();
  // Every shown window of the frame painted now, top to bottom (not the player's: another process).
  void PaintNow();
  // A window and its own windows painted now, top to bottom (PaintNow is the frame's).
  static void paintTree(wxWindow * window);

  // THE VIEWER SWITCH IN TURNS (BeginViewerSwitch / FinishViewerSwitch; see the comment above BeginViewerSwitch).
  struct ViewerSwitch
  {
    enum class Phase { Building, Waiting, Stepping, Finishing };   // turn A; between turns; a step's turn; turn B
    enum class Step { Listing, SettleRows, Mosaic };
    Phase phase = Phase::Building;
    ViewerMode previous = ViewerMode::Models;
    ViewerMode mode = ViewerMode::Models;
    bool paintNow = false;           // repainted at its end (the click); a load paints after its own work
    bool split = false;              // in turns of their own (the command bar's click)
    bool focusBrowse = false;        // OnCommandBar's keyboard to Browse, with the repaint
    bool frozen = false;             // the frame frozen (Freeze): thawed once, in turn B (or the abort)
    bool drawHeld = false;           // the frame's own drawing held back (ViewerSwitchFilter) until the thaw
    bool titleHeld = false;          // the caption changes after the repaint (UpdateTitle)
    bool held = false;               // the player held: released once in turn B (or counted down by the abort)
    bool playerWasOnScreen = false;
    bool layoutInB = false;          // the layout waits for turn B (the player on screen at the click)
    bool presized = false;           // a parked player sized ahead for its put-back (waited for, at most ViewerSwitchSizeWaitMs)
    bool timerArmed = false;         // the next turn's timer set (OnViewerSwitchIdle)
    bool itemDialogAway = false;     // the item chooser disabled now, put away after the repaint
    bool backgroundAway = false;     // the background window disabled now, hidden after the repaint
    bool backgroundBack = false;     // the background window shown after the repaint
    std::vector<std::pair<wxString, bool>> floatingDue;   // floating panels' windows shown (true) or hidden after the repaint
    QString clearTerrainReason;      // the Maps viewer left, the player on screen at the click: told with the repaint
    std::deque<Step> steps;          // the turns between turn A and turn B
    wxEventLoopBase * loop = nullptr;   // the loop the click came in (another one running: finished at once)
    wxStopWatch sinceClick, sinceTurn, presizeClock;
    std::vector<double> turnMs;
  };
  struct ViewerSwitchReport
  {
    QString to, finishedBy;
    int turns = 0;
    std::vector<double> turnMs;
    long clickToRepaintMs = 0;
    bool split = false, layoutInB = false;
  };
  std::unique_ptr<ViewerSwitch> m_switch;
  ViewerSwitchReport m_lastSwitch;
  wxTimer m_switchTimer;                         // a switch's next turn; the turns after it (m_later) when none is on its way
  std::deque<std::function<bool()>> m_later;     // runOnLaterTurn
  unsigned m_turnSerial = 0;                     // raised by every switch turn and later turn (OnIdleFreeDetached)
  unsigned m_freeDetachedTurnSeen = 0;
  class ViewerSwitchFilter;
  std::unique_ptr<ViewerSwitchFilter> m_switchFilter;
  static const int ViewerSwitchTurnBudgetMs = 25;    // the steps packed in one turn while under this
  static const long ViewerSwitchSizeWaitMs = 250;    // turn B waits for a presize at most this long after turn A
  static const long ViewerSwitchGapLimitMs = 500;    // the idle check finishes a switch whose next turn never came
  static const long LaterTurnShareMs = 10;           // a later turn's work stops once over this
  bool BeginViewerSwitch(ViewerMode mode, bool openBrowse, bool paintNow, bool split);
  void FinishViewerSwitch(const char * why);
  void AbortViewerSwitch();
  void viewerSwitchTurnDone(double ms);
  void queueLateViewerSwitchSteps();
  void runViewerSwitchSteps(bool all);
  void runViewerSwitchStep(ViewerSwitch::Step step);
  double viewerSwitchStepEstimateMs(ViewerSwitch::Step step) const;
  void OnViewerSwitchIdle(wxIdleEvent & event);
  void OnViewerSwitchTimer(wxTimerEvent & event);
  void commitLayoutBatchNow();
  void holdFloatingPanes();
  void restoreFloatingPanes();
  void moveGivenBackPanesOff();
  bool layoutMovesViewport();
  bool willBeShownOnScreen(wxWindow * window);
  void focusBrowseFor(ViewerMode mode);
  bool frameTakesInputNow() const;
  bool floatedPanelChanges();
  // Work on a turn of its own, once the GUI thread has gone idle, after any viewer switch on its way; true from `work`:
  // more of it on the next turn.
  void runOnLaterTurn(std::function<bool()> work);
  // The harness.
  bool viewerSwitchPending() const { return m_switch != nullptr; }
  bool viewerSwitchDrawHeld() const { return m_switch && m_switch->drawHeld; }
  const char * viewerSwitchPhaseName() const;
  const ViewerSwitchReport & lastViewerSwitch() const { return m_lastSwitch; }
  bool laterWorkPending() const { return !m_later.empty(); }
  void OnActivateFrame(wxActivateEvent & event);
  void UpdateActivePaneCaption(bool frameActive);
  void OnPaneClose(wxAuiManagerEvent &event);
  void OnToggleCommand(wxCommandEvent &event);
  void OnEffects(wxCommandEvent &event);

  // Wrapper function for character stuff (forwards events to charcontrol)
  void OnSetEquipment(wxCommandEvent &event);
  void OnCharToggle(wxCommandEvent &event);
  void OnImportNPCFromURL(wxCommandEvent &event);  // Character > Load NPC / Model... (a Wowhead link, or an ID)

  // Create the Unity viewport's host panel and wire its IPC callbacks. Called once, by InitDocking,
  // which docks it as the centre pane before any player exists.
  void CreateUnityViewport();
  // The centre pane's settings, shared by InitDocking, ResetLayout and LoadLayout so all three dock
  // the viewport identically.
  wxAuiPaneInfo unityViewportPaneInfo() const;
  // Make sure the Unity viewport is the shown centre pane (a saved perspective or a reset must never
  // leave the middle of the window to anything else). Returns true if the layout had to change.
  bool EnsureUnityViewportCentre();

  // View > "Restart Unity Renderer", and the button on a stopped-player notice.
  void OnRestartUnityRenderer(wxCommandEvent &event);
  // View > Swap Background Color: the Models viewport's background window, made the first time and shown again after.
  void OnBackgroundColor(wxCommandEvent & event);
  // Close the player (if any) and start it again; the viewport's state follows.
  void RestartUnityRenderer();
  // Launch the player into the viewport unless it is already running (also used by the headless
  // -unityipctest run). selfTest asks a diagnostic-capable player to also exercise the protocol's
  // error paths -- never set for a normal launch. Returns false if the player could not be started;
  // the reason is in unityRendererHost->playerProblem() and the viewport's notice, never a dialog.
  bool StartUnityRenderer(bool selfTest = false);

  // WHAT THE UNITY VIEWPORT SHOWS for what is currently loaded: the model (the player's window), or a
  // notice painted by the host panel -- the empty viewer's prompt, content it cannot draw yet, a
  // missing or stopped player. Never a different viewport. Called on every path that changes what
  // is loaded (model, NPC, item and character loads and their failures, Browse selections, clearing),
  // when a mount is put on or taken off (from the canvas tick), when the player announces itself or
  // reports a character it could not build, and when a stopped player is noticed.
  void UpdateUnityViewportState();
  // Lay the panes out again ONLY if a pane's shown state changed. See the definition for why
  // an unconditional interfaceManager.Update() is a whole-window blink on Windows.
  bool CommitLayoutIfChanged();
  // An unbatched layout made now (a command's, a settings page's): a viewer switch on its way finished first.
  void LayoutNow();

  // Start the Unity viewport's player at APP LAUNCH, before any model exists, so that picking the
  // first creature does not also pay for starting a game engine. No-op in batch mode. A missing
  // player build is reported by the viewport's notice.
  void WarmStartUnityViewport();

  // The part of the viewer-first startup that involves NO player and NO IPC: take the screen (the
  // panels keep the shown state the saved layout restored). Safe to call before a client is loaded, which is the point -- see the note
  // on WarmStartUnityViewport for why the player itself must wait.
  void ApplyViewerStartupLayout();

  // Show the Client Choice dialog and load whatever the user picks. This is the ONLY thing that
  // loads a client now -- File > "Load World of Warcraft" calls it, and nothing calls it at
  // startup. Loops so a failed legacy-MPQ pick returns to the dialog rather than giving up.
  void PromptAndLoadClient();
  // THE VIEWER MODE (ViewerMode.h): Models (the Unity viewport and the model panels), Textures (the texture
  // viewer in the viewport's place, the model panels put away) or Buildings (the Unity viewport with a world
  // model, WMO, the panels that act on a model put away). The command bar's Models | Textures | Buildings
  // selector, what Browse lists (FileControl::FollowViewerMode) and the centre all follow it, and SetViewerMode
  // is the one way to change it: the selector, a model loaded from a menu or a file (Models), a texture picked
  // (Textures), a world model picked or loaded (Buildings: FileControl::SelectWMOFile).
  // The texture selected stays selected behind the other modes, and what the canvas holds stays behind
  // Textures. The canvas holds one model OR one world model, never both, so Models and Buildings each show
  // only their own kind: a world model on the canvas in Models, or a model in Buildings, is not shown -- the
  // mode's empty viewer is -- and is shown again, without loading anything, when its mode comes back.
  using ViewerMode = ::ViewerMode;
  void SetViewerMode(ViewerMode mode, bool openBrowse = false, bool paintNow = false);
  ViewerMode viewerMode() const { return m_viewerMode; }
  bool isTextureMode() const { return m_viewerMode == ViewerMode::Textures; }
  bool isModelsMode() const { return m_viewerMode == ViewerMode::Models; }
  bool isBuildingsMode() const { return m_viewerMode == ViewerMode::Buildings; }
  bool isMapsMode() const { return m_viewerMode == ViewerMode::Maps; }
  // What the canvas holds, by kind: a world model (with its flag: a pointer a load left behind does not
  // count), or a model.
  bool canvasHasWorldModel() const;
  bool canvasHasModel() const;
  // Whether the canvas holds what the viewer mode shows: a model in Models, a world model in Buildings.
  bool canvasHoldsModesContent() const;
  // The commands that act on a model -- the Animation and Attachments panels, View NPC, View Item, Load
  // Character, the Armory and NPC imports, every export of the model, and Swap Background Color (the
  // Models viewport's) -- are the Models viewer's: greyed in Textures and Buildings (OnUpdateCommandUI,
  // answered again at every switch) and refused however they arrive (TryBefore).
  bool needsModelViewer(int id) const;
  // The Model panel, which shows a world model's information too, is the Models and Buildings viewers':
  // greyed and refused in Textures only.
  bool needsUnityViewport(int id) const;
  // A command the loaded client cannot do (ClientCapabilities): why, in plain words; empty when it can (or when no
  // client is loaded -- the viewer selector then offers to load one). Greyed and refused like the above.
  QString capabilityRefusal(int id) const;
  // The commands capabilityRefusal answers for (their menu items and tools follow it both ways).
  bool isCapabilityGated(int id) const;
  // Select a texture picked in Browse (Textures mode): the texture view reads and shows it.
  void ShowTexture(const TextureEntry & entry, bool lookup);
  // The texture view's selection changed, or its facts arrived: the command bar's label, the status bar
  // and the Model panel follow (not the whole interface: nothing else depends on it).
  void TextureSelectionChanged();
  // Options > Settings on its Export page (the texture export folder).
  void ShowExportSettings();

  // Whether the Unity viewport is the shown centre pane (it always should be; the self-test checks).
  bool isUnityViewportCentre();
  // Whether the Unity viewport is showing the loaded model: the player is connected and what is
  // loaded is something it can draw (no notice in front of it).
  bool isUnityViewportShowingModel();
  // Whether the Unity viewport has a notice in front of the player (content it cannot draw yet, a
  // player that is not running, nothing loaded) or a texture, as last decided by
  // UpdateUnityViewportState. True when there is no viewport at all.
  bool unityViewportHasNotice() const;

  // Something new is on screen (a model, character or WMO): the Model panel, the
  // command bar's model name, the status bar facts and the viewport (model or notice) follow it. The one
  // place every load path reports to.
  void DisplayedContentChanged();
  void UpdateCommandLabel();

  // The command bar along the top: the viewer selector, the screenshot, the current model and the three panel toggles.
  void InitCommandBar();
  void OnCommandBar(wxCommandEvent & event);
  void OnUpdateCommandUI(wxUpdateUIEvent & event);
  void OnKeyboardShortcuts(wxCommandEvent & event);
  void UpdateStatusFacts();
  // Help text in the status bar's first field, as wx gives it for menu items and toolbar buttons -- only
  // where there is some: see the definition.
  void DoGiveHelp(const wxString & text, bool show) wxOVERRIDE;

  // THE MODELS VIEWPORT'S BACKGROUND (View > Swap Background Color; ViewportBackground.h): the colour the Unity player clears
  // the Models viewport to, as the sRGB bytes it displays as. Loaded with the session; behind a model the player's frame
  // paints it while the player starts, a player started later is given it on its command line, and one that announces
  // itself is sent it (viewportBackgroundShown).
  const wxColour & viewportBackground() const { return m_viewportBackground; }
  // Show colour now. Sent to the player only when it differs from the colour on show (and the player speaks protocol
  // 7); kept in Config.ini when persist and it differs from what is kept there. The background window follows.
  void setViewportBackground(const wxColour & colour, bool persist);
  // The colour the player clears to, which follows what it is given to draw, never the viewer mode: a world model
  // (the Buildings viewer's) on the viewport's default, anything else on the Models viewport's background. The player
  // has one clear colour for everything it draws, and keeps drawing what it holds while it is covered, so a colour
  // that changed with the viewer would show on the content of the other viewer -- the Models colour behind a
  // building for a frame or more when Buildings uncovers it. Sent to the player on connect, when the Models colour
  // changes (setViewportBackground; while a world model is on the canvas only if what the player was last sent differs
  // -- it can, when that world model never reached the player, and what it keeps covered is then re-coloured, the next
  // load fenced as any re-colouring load is), and with a load the player
  // gets, before it (SendLoadToUnity) -- not because the canvas changed without the player getting it (a load that
  // failed, a world model that cannot be read), which would re-colour what the player keeps -- and a load that
  // changes it is not shown before it is built, as a load of the other kind is not.
  wxColour viewportBackgroundShown() const;
  // The host panel and the player given that colour, when it is not the one they have. True when the player was sent
  // a new colour now (what it already draws is then drawn in it from its next frame).
  bool pushViewportBackground();
  wxColour m_viewportBackgroundSent;   // what the player was last sent (invalid: nothing sent to this player)

  // THE VIEWPORT SCREENSHOT. The command bar's Screenshot: a Save As dialog (PNG, overwrite confirmed, named after
  // the model and the time), then RequestUnityScreenshot. Cancelling does nothing.
  void SaveUnityScreenshot();
  // Ask the Unity player for a SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT PNG of what the viewport shows, with a transparent
  // background, written to path (a full path; ".png" is added when it does not end in it). The outcome reaches the
  // status bar when the player answers (OnUnityScreenshotSaved), or when it has not after SCREENSHOT_TIMEOUT_MS.
  // Returns the request's number, or 0 with why when nothing was asked (and the status bar says why as well). No
  // dialog or message box is ever shown here, so a headless test can call it directly.
  int RequestUnityScreenshot(const wxString & path, wxString & why);
  // The name Save As starts from: the model's (the character's, while it rides), sanitised, and the local time,
  // "<model>_<yyyy-MM-dd_HHmmss>.png".
  wxString DefaultScreenshotName() const;
  void OnUnityScreenshotSaved(const UnityIpcServer::ScreenshotResult & result);
  static const int SCREENSHOT_WIDTH = 3840;
  static const int SCREENSHOT_HEIGHT = 2160;
  static const unsigned long SCREENSHOT_TIMEOUT_MS = 60000;
  int m_screenshotRequest = 0;             // the capture on its way (its request number), 0 for none
  wxString m_screenshotPath;
  unsigned long m_screenshotSentAt = 0;

  // What the Unity viewport paints instead of the player: a title, a detail line and an optional
  // button (the menu command it posts, 0 for none).
  struct ViewportNotice
  {
    wxString title;
    wxString detail;
    wxString actionLabel;
    int actionId = 0;
  };
  // Can the Unity player draw what is currently loaded? False for nothing loaded and for content it
  // cannot draw yet; notice (when given) then says what it is and why. This also gates the geoset
  // pushes, so a state is never sent about content the player is not building.
  bool unityCanDrawCurrentModel(ViewportNotice * notice = nullptr) const;
  // The whole decision UpdateUnityViewportState applies: true with the notice to paint, or false when
  // the player's window should be on screen.
  bool unityViewportNotice(ViewportNotice & notice) const;
  ViewerMode m_viewerMode = ViewerMode::Models;
  bool m_helpInStatus = false;   // DoGiveHelp has help text in the status bar, to take away
  wxTimer m_themeRecheck;          // the theme checked once more after a burst of Windows' colour messages
  std::unique_ptr<UiMenuBarTitles> m_menuBarTitles;  // the menu bar's titles in the dark run (UiControls.h)
  int m_exportMenuFirst = 0;     // the exporters' ids in File > Export Model: [first, end) (needsModelViewer)
  int m_exportMenuEnd = 0;
  bool m_frameActive = true;     // the window is the active one (its last activate event): a pane's caption
                                 // reads as active only then (UpdateActivePaneCaption)
  // THE WORKSPACE OF A VIEWER MODE. The panels a mode cannot use are put away while it is on screen, so its
  // content gets the room: in Textures the Animation and Model panels and the Attachments window, in Buildings
  // the Animation panel and the Attachments window (the Model panel shows a world model's information), in
  // Models none (workspacePanes). Each panel put away is recorded and given back as it was -- shown, at the
  // size it had -- when a mode that uses it comes back; a switch between two modes puts away or gives back
  // only the difference (decided with the viewport, in UpdateUnityViewportState, and laid out at once).
  // Browse stays. The panels put away cannot be shown meanwhile (their toggles are another viewer's:
  // needsModelViewer, needsUnityViewport), so they come back as they were. The layout saved on exit is the
  // user's: SaveLayout writes the panels given back. 'commit' false: the caller lays out.
  enum WorkspacePane { PaneAnimation = 1, PaneModel = 2, PaneAttachments = 4 };
  static int workspacePanes(ViewerMode mode);
  static const wxChar * workspacePaneName(int pane);
  void applyWorkspace(int panes, bool commit = true);
  // Drop a panel from the record (the user shows or closes it -- the panels put away cannot be shown);
  // the record is handed back when asked.
  struct WorkspacePaneRecord;
  bool forgetWorkspacePane(const wxString & name, WorkspacePaneRecord * taken = nullptr);
  // Lay the panes out with docks made again at the size their panels' best size says, larger than the
  // 30% of the window a new dock is otherwise capped at (a sash may have made them larger than that).
  void commitDocksAtTheirSize();
  void commitDocksNow();
  // The layout as the user has it: SavePerspective, with the panels a mode put away given back.
  wxString userPerspective();
  struct WorkspacePaneRecord
  {
    wxString name;
    wxSize bestSize;    // the pane's own best size, put back once it is shown again
    wxSize shownSize;   // its window's size when it went (a dock made again is sized from it)
  };
  int m_workspace = 0;                                // the panels put away now (WorkspacePane bits)
  std::vector<WorkspacePaneRecord> m_workspaceRecord;  // those of them that were shown
  std::vector<WorkspacePaneRecord> m_workspaceGivenBack;   // given back by the last switch: their own best size
                                                           // goes back once the dock has its size (finishWorkspace)
  wxString m_workspaceLayout;   // SavePerspective when the first went: the sizes of the docks they took
  // A client load is starting or has ended: the texture view and Browse's texture tree follow
  // (Textures mode stays: its selection and cache go with the old client).
  void TexturesClientLoadStarting();
  void TexturesClientLoaded();

  void OnToggleFullScreen(wxCommandEvent & event);
  void OnCharHook(wxKeyEvent & event);

  // Borderless fullscreen that keeps the menu bar, so the mode can always be left.
  void EnterViewerFullScreen(bool full);
  // Runtime command to the embedded Unity player: "this is the active model" (path +
  // FileDataID of the model on the canvas), or for a WMO the root's path + FileDataID with kind "wmo"
  // (only to a player that is ready and speaks protocol 4). No-op when no player is connected. Called
  // after every model load, after a WMO selection (FileControl::SelectWMOFile) and when the player
  // announces unityReady. Raises the load serial for whatever it sends.
  void SendLoadToUnity();
  void SendCurrentModelToUnity();
  void SendCurrentSkinToUnity();
  void SendCurrentAnimationToUnity();

  // The displayed model's per-submesh geoset state changed (a Geosets checkbox, or a load path that
  // set flags after the skin push went out): send the whole state to the Unity player. Returns the
  // revision sent -- the player's modelGeosetsApplied answer names it -- or 0 when nothing was sent
  // (no player connected and ready, or nothing on the canvas the Unity viewport can show).
  int SendCurrentGeosetsToUnity();
  int m_geosetRevision = 0;
  // A player is connected and has announced itself; ...and speaks protocol 2, so it switches
  // submeshes live and answers every state it is sent.
  bool unityPlayerReady() const;
  bool unityPlayerSwitchesSubmeshes() const;
  bool unityPlayerDressesCharacters() const;

  // The playback state of that animation: playing/paused, speed, and where in the sequence the
  // app is. force pushes unconditionally (a control was used); without it this is the heartbeat,
  // which pushes only while something is playing and only every so often. Safe and cheap to call
  // every frame -- it rate-limits itself and no-ops while no player is connected.
  void SendAnimationStateToUnity(bool force = false);

  // The resolved state of the character on the canvas (UnityCharacterScene), whenever it differs
  // from what the player was last sent: a customization, equipment, a render toggle, a geoset
  // checkbox. Cheap to call every canvas tick -- it rate-limits itself and compares a fingerprint
  // before building anything. force sends regardless of the fingerprint.
  void SendCharacterSceneToUnity(bool force = false);
  // The canvas shows a playable character the Unity viewport can dress (not a mount carrying one).
  bool canvasShowsCharacter() const;
  // THE RIDER: in a character context, the character model -- CharControl::model, the model of its attachment
  // node CharControl::charAtt -- whether or not it rides a mount; null in any other context. While a mount is
  // up the canvas model is the mount, so this, not canvas->model(), is the character.
  WoWModel * riderModel() const;
  // The mount the rider rides: the WoWModel on the rider node's parent (the canvas root, where the mount choice
  // puts it), or null when that holds none.
  WoWModel * riderMount() const;
  // The canvas shows a playable character riding a mount: a character context, a rider that is a character
  // model with a FileDataID, and a mount on its node's parent. The Unity viewport draws it when the player
  // rides mounts (protocol 5); an older player gets the mounted-character notice.
  bool canvasShowsMountedCharacter() const;
  bool unityPlayerRidesMounts() const;
  // The character the Unity player is told about -- loaded, dressed and answered for: the canvas model when
  // it is the character, the rider while it rides a mount and the player rides mounts, otherwise null.
  WoWModel * unityCharacter() const;
  // A racial character/rider or a live ordinary NPC with hands-only equipment controls.
  WoWModel * unityEquipmentOwner() const;
  // Whether the canvas showed a mounted character when the viewport state was last decided (with
  // m_lastShowsCharacter below).
  bool m_lastShowsMountedCharacter = false;
  // The mount last described in the log ([unity-mount]), so each new mount is described once.
  const WoWModel * m_loggedMount = nullptr;
  unsigned int m_loggedMountSerial = 0;
  int m_sceneRevision = 0;
  quint64 m_lastSceneSignature = 0;
  unsigned long m_lastSceneCheck = 0;
  static const unsigned long SCENE_CHECK_MS = 50;
  // FLOW CONTROL: one scene at a time. A scene can carry an 8 MB composited image, and cycling
  // through customization choices would otherwise queue one per 50 ms faster than the player can
  // decode them; the next scene goes out when the player has answered this one, and it describes the
  // state at THAT moment. A player silent for SCENE_ACK_TIMEOUT_MS is no longer waited for: the timeout
  // is logged once and the current state is sent once more.
  int m_sceneAwaitingRevision = 0;
  unsigned long m_sceneSentAt = 0;
  static const unsigned long SCENE_ACK_TIMEOUT_MS = 8000;
  // A character loaded and dressed in several steps (a .chr, an Armory import, an NPC) is described to
  // the player once, when it is complete, rather than after each step: see SceneHold.
  int m_sceneHold = 0;
  // Whether the canvas showed a character the viewport can dress when the viewport state was last
  // decided (UpdateUnityViewportState records it): mounting and dismounting change it without a load,
  // and the viewport has to follow from the tick.
  bool m_lastShowsCharacter = false;
  // The character model the Unity player could not build: the viewport shows a notice for it until a
  // load of another model, a player (re)start, or a later load of the same model that the player does
  // dress. m_unityCharacterFailReason is the player's reason, for that notice.
  int m_unityCharacterFailed = 0;
  QString m_unityCharacterFailReason;
  // The load serial that build belonged to. No scene is sent while it is still the load on display: the
  // player has dropped that load. A later load of the same model is a new attempt, and its body build
  // waits for a scene, so that one is sent.
  int m_unityCharacterFailedLoad = 0;
  // THE LOAD SERIAL: raised for every loadWoWModel sent (SendLoadToUnity), which carries it, and named
  // by every characterSceneApplied. An answer naming another load is about a model the player has
  // since been told to replace -- often the same body model, so the same fileDataID -- and says nothing
  // about the one on display.
  int m_unityLoadSerial = 0;
  // What the last loadWoWModel sent named: the model's fileDataID and whether it went as a character.
  // Mounting and dismounting hand the viewport over without a load, and a player that connected while the
  // mount was up was sent the rider if it seats characters on mounts (protocol 5), the mount if it does not.
  int m_unityLoadedFileDataID = 0;
  bool m_unityLoadedCharacter = false;
  void OnCharacterSceneApplied(const UnityIpcServer::SceneAck & ack);
  // The world model (root FileDataID) the Unity player reported it could not build, the load serial that
  // build belonged to and the player's reason. The viewport shows a notice for it while that load is the
  // one on display -- the player keeps showing whatever it had before, which must not pass for the WMO --
  // until another load or a player (re)start.
  int m_unityWmoFailed = 0;
  int m_unityWmoFailedLoad = 0;
  QString m_unityWmoFailReason;
  // Every mapObjectLoaded: logged, and a failure of the WMO on display becomes the notice above.
  void OnMapObjectLoaded(const UnityIpcServer::MapObjectReport & report);
  // WHAT THE PLAYER SHOWS WHILE A LOAD BUILDS. A load replaces the player's content only once it is built, so
  // until then it shows what it had: across Models and Buildings, the other viewer's kind -- a model in the
  // Buildings viewer, a world model in the Models viewer -- which must never pass for this mode's content. So
  // while a world model builds after something that is not one, or a model after a world model, the viewport
  // shows a notice ("Loading ..."); the player's mapObjectLoaded report ends a world model's wait, its
  // runtimeState (asked for every POLL_MS while a model is awaited) a model's. A model the player could not
  // build after a world model gets a notice, as a world model does.
  // Unknown: a load of the other kind is in flight or was dropped -- the player may show either until it answers.
  // Terrain: the player shows a terrain tile (THE TERRAIN TILE below), in front of whatever the canvas holds: no notice
  // is put over it, and a load of either kind is awaited as after the other kind.
  enum class PlayerContent { Nothing, Model, MapObject, Unknown, Terrain };
  PlayerContent m_playerContent = PlayerContent::Nothing;   // what the player was last known to show
  int m_mapObjectAwaited = 0;   // the load serial of a world model built after another kind, until reported
  int m_modelAwaited = 0;       // the FileDataID of a model built after a world model, until the player has it
  int m_modelAwaitedFailed = 0; // that model, when the player finished without it (it still shows the world model) or
                                // dropped it for a terrain tile
  wxStopWatch m_modelAwaitedClock;   // since the wait began (monotonic)
  wxTimer m_playerContentPoll;
  // THE KEYBOARD TAKEN FROM THE PLAYER after a viewer-mode switch, on a turn of its own (handKeyboardSoon): the move
  // waits on the player's thread for one of its frames (17-30 ms), and the switch's turn is long enough without it.
  wxTimer m_keyboardHandoff;
  std::function<void()> m_keyboardHandoffDo;
  wxStopWatch m_keyboardHandoffClock;               // since handKeyboardSoon
  static const long KeyboardHandoffWaitMs = 250;    // a handoff waits for the player's thread to finish sizing at most this
  void handKeyboardSoon(std::function<void()> handoff);
  // HIDDEN WINDOWS STAY FROZEN (parkHidden): the frame's windows a viewer-mode switch leaves hidden (and hidden ones with
  // windows of their own inside shown ones), each with a freeze of its own until it is shown again.
  std::set<wxWindow *> m_parked;
  std::set<wxWindow *> m_parkWatched;
  bool m_parkIdleBound = false;
  std::shared_ptr<bool> m_parkAlive = std::make_shared<bool>(true);   // the handlers' guard: false once the frame goes
  void parkHidden();
  void unpark(wxWindow * window);
  void OnParkedIdle(wxIdleEvent & event);
  wxTimer m_streamUiTimer;      // STREAMING: a streamState's streaming lines and rings, after STREAM_UI_MS (coalesced)
  static const int STREAM_UI_MS = 150;
  static const int POLL_MS = 100;
  static const long MODEL_AWAIT_TIMEOUT_MS = 60000;
  void OnPlayerContentPoll(wxTimerEvent & event);
  void OnPlayerRuntimeState(const UnityIpcServer::RuntimeState & state);
  int m_playerContentQuery = 0;   // the runtimeState question the poll is waiting on
  // THE UNCOVER FENCE. The player answers a load (mapObjectLoaded, a runtimeState that has the model) from the frame
  // that adopts it, before that frame is presented: uncovered on that answer, the viewport could show the frame before
  // it -- the previous model or building -- for one frame of the player's. So the "Loading ..." notice stays until the
  // player answers a question asked then, which it does in a later frame, after the one that draws the new content.
  int m_uncoverFence = 0;
  // Two questions in turn: the player renders on a render thread of its own, so the answer to the first can come
  // from a frame whose rendering still waits for the adopting frame's Present; the second's frame began rendering
  // after it. Asked again when unanswered for FENCE_RETRY_MS, given up after MODEL_AWAIT_TIMEOUT_MS.
  int m_uncoverFenceRounds = 0;
  wxStopWatch m_uncoverFenceClock, m_uncoverFenceAsked;
  static const long FENCE_RETRY_MS = 500;
  bool startUncoverFence();
  void endUncoverFence();

  // THE TERRAIN TILE (protocol 12; UnityIpcServer.h, TERRAIN). One ADT tile of a map of the loaded client, drawn by the
  // player from the map's WDT and the tile's root ADT, which it asks for (getMapAsset) and parses itself. The host
  // names the WDT from its map catalogue and serves the two files through it, for the current terrain load only.
  // Protocol 13 (TERRAIN TEXTURES) adds the tile's tex0 ADT and its textures, accounted per load here: the request is
  // replaced with the load, so nothing of it outlives the load.
  struct TerrainRequest
  {
    int load = 0;                       // the load serial sent (0: no terrain request)
    int epoch = 0;                      // UnityAssetAccess::clientEpoch() when sent
    int mapId = -1;
    int tileX = -1;
    int tileY = -1;
    int wdtFileDataID = 0;              // the one WDT this load may fetch
    bool wdtServed = false;             // that WDT was answered "available"
    int rootFileDataID = 0;             // the first root ADT this load asked for (0: none yet): the only one it may fetch
    quint64 catalogueGeneration = 0;    // the storage the map catalogue was built from (MapCatalogClient)
    // The limits the load was sent with: maxAssetBytes for the WDT, the root and the tex0; the texture limits only to a
    // player that draws terrain textures (0: the load offers no textures, and no tex0 or texture is served for it).
    qint64 maxAssetBytes = UnityIpcServer::MAX_MAP_ASSET_BYTES;
    qint64 maxTextureBytes = 0;
    qint64 maxTextureBytesPerLoad = 0;
    int maxTexturesPerLoad = 0;
    // What the load has had (accounted when a request goes on to the read, noteMapAssetRead, and after it,
    // noteMapAssetResult).
    int reads = 0;                      // requests that went on to the storage read, every role: the terrain roles' at
                                        // most 3 + maxTexturesPerLoad, the world roles' (worldReads), the doodad
                                        // roles' (doodadReads) and the liquid role's (liquidReads) apart
    int worldReads = 0;                 // ... of them the world roles' (obj0Adt, wmoRoot, wmoGroup, wmoTexture): at most
                                        // 1 + maxWmoModelsPerLoad + maxWmoGroupsPerLoad + maxWmoTexturesPerLoad
    int doodadReads = 0;                // ... of them the doodad roles' (m2Model, m2Skin, m2Texture): at most
                                        // 2 * maxM2ModelsPerLoad + maxM2TexturesPerLoad
    bool tex0Asked = false;             // a tex0 ADT went on to the read: the only one this load may have
    int tex0FileDataID = 0;             // ... its FileDataID
    bool tex0Served = false;            // ... and it was answered "available" (its kind checked)
    std::vector<int> textureIds;        // the texture ids that went on to the read, each once: at most maxTexturesPerLoad
    qint64 textureBytesRead = 0;        // the bytes read for them, kind failures included: at most maxTextureBytesPerLoad
    qint64 textureBytesReserved = 0;    // the limits of the texture reads in flight (THE MAP ASSET WORKER): what they may
                                        // still read, counted against maxTextureBytesPerLoad until their results come
    bool ended = false;                 // the player reported the load (terrainTileLoaded): nothing more is read for it
    QString endedStatus;                // ... the status it reported
    // WORLD MODELS (protocol 16, docs/world-viewer/06-world-models-in-world.md): a world load (loadWorld) -- the tile and
    // the world models its obj0 ADT places, or (globalWmo) a WMO-only map's one world model and no tile. The budgets it was
    // sent with (0: none -- a loadTerrainTile, which is served no world-model file), and what it has had.
    bool world = false;
    bool globalWmo = false;
    int globalWmoFileDataID = 0;        // the WDT's world model: the one root a WMO-only map's load may fetch (0: not known)
    qint64 maxWmoFileBytes = 0;
    qint64 maxWmoBytesPerLoad = 0;
    qint64 maxWmoTextureBytes = 0;
    qint64 maxWmoTextureBytesPerLoad = 0;
    int maxWmoTexturesPerLoad = 0;
    int maxWmoModelsPerLoad = 0;
    int maxWmoGroupsPerLoad = 0;
    bool obj0Asked = false;             // the tile's obj0 ADT went on to the read: the only one this load may have
    int obj0FileDataID = 0;
    bool obj0Served = false;            // ... answered "available" (its kind checked)
    std::vector<int> wmoRootIds;        // the world-model roots that went on to the read, each once
    int wmoRootsServed = 0;             // ... answered "available" (their kind checked)
    std::vector<int> wmoGroupIds;       // the group files that went on to the read, each once
    std::vector<int> wmoTextureIds;     // the world-model textures that went on to the read, each once
    qint64 wmoBytesRead = 0;            // root and group bytes read, kind failures included: at most maxWmoBytesPerLoad
    qint64 wmoBytesReserved = 0;        // the limits of the root and group reads in flight
    qint64 wmoTextureBytesRead = 0;     // world-model texture bytes read: at most maxWmoTextureBytesPerLoad
    qint64 wmoTextureBytesReserved = 0; // the limits of those reads in flight
    // DOODADS (protocol 17, docs/world-viewer/07-doodads.md): a world load to a player that places doodads -- the M2
    // models the obj0 ADT's MDDF and the world models' doodad sets name, their skins and their textures. The budgets it
    // was sent with (0: none -- a load to an older player, or not a world load, which is served no doodad file: the load
    // "carries doodad budgets" when maxM2ModelsPerLoad > 0), and what it has had.
    qint64 maxM2FileBytes = 0;
    qint64 maxM2BytesPerLoad = 0;
    qint64 maxM2TextureBytes = 0;
    qint64 maxM2TextureBytesPerLoad = 0;
    int maxM2ModelsPerLoad = 0;
    int maxM2TexturesPerLoad = 0;
    int maxDoodadInstancesPerLoad = 0;  // the player's budget alone, kept for the log: the host never sees a placement
    std::vector<int> m2ModelIds;        // the doodad models that went on to the read, each once
    int m2ModelsServed = 0;             // ... answered "available" (their kind checked)
    std::vector<int> m2SkinIds;         // the doodad skins that went on to the read, each once
    std::vector<int> m2TextureIds;      // the doodad textures that went on to the read, each once
    qint64 m2BytesRead = 0;             // model and skin bytes read, kind failures included: at most maxM2BytesPerLoad
    qint64 m2BytesReserved = 0;         // the limits of the model and skin reads in flight
    qint64 m2TextureBytesRead = 0;      // doodad texture bytes read: at most maxM2TextureBytesPerLoad
    qint64 m2TextureBytesReserved = 0;  // the limits of those reads in flight
    // STREAMING (protocol 18, docs/world-viewer/08-streaming.md 1.7): what any load of the load's streaming session was
    // served -- an obj0, a world-model root, a doodad model -- so that a file the session's tiles share can be asked for
    // again under another tile's load once the tile that first asked for it closed: its groups and textures after a root
    // the session was served, its skins and textures after a model. Kept the same in the entry and every open tile load
    // (noteStreamSessionServed); 0 for every load outside a session, whose own order rules are unchanged.
    bool sessionObj0Served = false;
    int sessionWmoRootsServed = 0;
    int sessionM2ModelsServed = 0;
    // LIQUIDS (protocol 19, docs/world-viewer/09-liquids.md 1.6): a load to a player that draws liquids -- the animated
    // frames and the bumps of the liquid catalogue's types, asked for by FileDataID (getMapAsset "liquidTexture"). The
    // budgets it was sent with (0: none -- a load to an older player, which is served no liquid texture: the load "carries
    // a liquids offer" when maxLiquidTexturesPerLoad > 0), the frames and bumps of the catalogue it was sent with
    // (LiquidCatalog::textureIds: sorted; the only files the role serves), and what it has had. A streaming session's tile
    // loads take the entry's (OnStreamTile).
    int maxLiquidTexturesPerLoad = 0;
    qint64 maxLiquidTextureBytes = 0;
    qint64 maxLiquidTextureBytesPerLoad = 0;
    std::shared_ptr<const std::vector<int>> liquidFrames;
    int liquidReads = 0;                // requests of the liquid role that went on to the read: at most maxLiquidTexturesPerLoad
    std::vector<int> liquidTextureIds;  // the liquid textures that went on to the read, each once
    qint64 liquidTextureBytesRead = 0;  // their bytes read, kind failures included: at most maxLiquidTextureBytesPerLoad
    qint64 liquidTextureBytesReserved = 0; // the limits of the liquid texture reads in flight
  };
  // What getMapAsset decided before anything is read: state "" goes on to the read (at most maxBytes), "refused" or
  // "stale" is the answer.
  struct MapAssetDecision
  {
    QString state;
    QString detail;
    qint64 maxBytes = -1;               // the read's limit, when it goes on to the read
  };
  // The decision, from nothing but its arguments (a test calls it with made-up requests), in this order: a client being
  // loaded is refused; a request without a load (> 0) is refused; one with no current terrain request, another load, a
  // load the player has reported, another client epoch, or under a map catalogue that is not the loaded client's any
  // more is stale (the detail names every reason that holds); a role other than "wdt", "rootAdt", "tex0Adt" or
  // "texture", a fileDataID <= 0 is refused; then by role: a WDT other than the load's; a root ADT before the load's WDT
  // was served, or a second root ADT id for the load; a tex0 ADT or a texture for a load that offers no textures, a tex0
  // ADT before the WDT was served or after one was asked for; a texture before the tex0 was served, one asked for
  // before, or past maxTexturesPerLoad ids; and for every role a read past 3 + maxTexturesPerLoad for the load, and for a
  // texture once maxTextureBytesPerLoad bytes were read -- all refused. The read's limit: maxAssetBytes, for a texture the
  // smaller of maxTextureBytes and what is left of maxTextureBytesPerLoad.
  // WORLD MODELS (protocol 16): "obj0Adt", "wmoRoot", "wmoGroup" and "wmoTexture" are served to a world load only (a
  // loadTerrainTile's is refused), and a WMO-only map's load has no tile (its "rootAdt", "tex0Adt", "texture" and
  // "obj0Adt" are refused). The obj0 ADT once, after the WDT; a root after the obj0 was served (a WMO-only map's: after
  // the WDT, and only the WDT's world model when it is known), each once, at most maxWmoModelsPerLoad; a group file or a
  // texture after a root was served, each once, at most maxWmoGroupsPerLoad and maxWmoTexturesPerLoad; root and group
  // bytes within maxWmoBytesPerLoad, texture bytes within maxWmoTextureBytesPerLoad (the reads in flight reserved). The
  // reads a world load may make grow by 1 + those three counts. The read's limit: maxAssetBytes for the obj0, the smaller
  // of maxWmoFileBytes (maxWmoTextureBytes) and what is left of the load's bytes for the others.
  // DOODADS (protocol 17): "m2Model", "m2Skin" and "m2Texture" are served to a load that carries doodad budgets only (a
  // load to an older player, or a loadTerrainTile, is refused every one). A model after the load's obj0 or one of its
  // world-model roots was served; a skin or a texture after a model was served; each id once, at most maxM2ModelsPerLoad
  // models, as many skins, and maxM2TexturesPerLoad textures; model and skin bytes within maxM2BytesPerLoad, texture
  // bytes within maxM2TextureBytesPerLoad (the reads in flight reserved). The doodad roles' reads are a third family,
  // capped at 2 * maxM2ModelsPerLoad + maxM2TexturesPerLoad apart from the terrain's and the world models' (the terrain
  // roles counted as reads less both). The read's limit: the smaller of maxM2FileBytes (maxM2TextureBytes) and what is
  // left of the load's bytes.
  // LIQUIDS (protocol 19): "liquidTexture" is served to a load that carries a liquids offer only ("liquids not offered"
  // otherwise), once the load's WDT was served (a streaming session's tile loads count it served), and only an id among
  // the frames of the liquid catalogue the load was sent with ("not a liquid texture" otherwise); each id once, at most
  // maxLiquidTexturesPerLoad ids, the bytes within maxLiquidTextureBytesPerLoad (the reads in flight reserved). Its reads
  // are a fourth family, capped at maxLiquidTexturesPerLoad apart from the others (the terrain roles counted as reads less
  // all three). The read's limit: the smaller of maxLiquidTextureBytes and what is left of the load's liquid bytes.
  static MapAssetDecision decideMapAssetRequest(const QJsonObject & request, const TerrainRequest * current, int epochNow,
                                                bool catalogueBelongs, bool clientLoading);
  // The accounting of a request the decision let through, before its read (the read counted, the tex0 or texture id
  // taken), and after it (bytesRead: what the read read, kind failures included; available: the answer, its kind checked).
  static void noteMapAssetRead(TerrainRequest & current, const QJsonObject & request);
  static void noteMapAssetResult(TerrainRequest & current, const QString & role, qint64 bytesRead, bool available);
  // A read decideMapAssetRequest cut to what was left of its load's budget for the role's family, which found the file
  // larger than that (the inspection's TooLarge) but within the role's per-file limit: the load's refusal, not the file's
  // failure -- another load with room left can read it. The answer's detail then; "" otherwise (a file over the per-file
  // limit, which no load can read; a read not cut; a file not too large; a role whose read is never cut: wdt, rootAdt,
  // tex0Adt, obj0Adt; a load without that family's limits).
  static QString mapAssetBudgetRefusal(const TerrainRequest & load, const QString & role, qint64 fileSize, qint64 readLimit);
  // The kind of a file read for role (TERRAIN TEXTURES: "tex0Adt" MVER then MAMP, "texture" BLP2; WORLD MODELS: "obj0Adt"
  // MVER then MDDF, "wmoRoot" MVER then MOHD, "wmoGroup" MVER then MOGP, "wmoTexture" BLP2; DOODADS: "m2Model" MD21 then,
  // at 8, MD20, "m2Skin" SKIN, "m2Texture" BLP2; LIQUIDS: "liquidTexture" BLP2; nothing for the other roles): "" when it is
  // that kind, else what was found, the answer's detail.
  static QString mapAssetKindProblem(const QString & role, const QByteArray & bytes);
  // Show one terrain tile: refused, 0 with why, unless a client is loaded (and none is loading), the player draws terrain,
  // the map catalogue is ready and the loaded client's, the tile is in the 64 x 64 grid, the map has a WDT and that WDT
  // can be read now (inspected through the storage, for every client family). Otherwise the load serial sent. The model
  // or world model the player was building is dropped by it (the player's model session stays, hidden), so the waits
  // for it end here. wdtInspected (the Maps viewer, docs/world-viewer/05-maps-viewer-ui.md): the map's WDT inspection
  // the catalogue holds was made by the map asset worker for this client (WorldMapCatalog::noteInspection), and is used
  // as it is when it says Available -- the GUI thread reads nothing; otherwise the WDT is inspected here as before.
  // camera (protocol 15, the Maps viewer's Enter world): where the player puts its free camera when the tile is shown.
  int RequestTerrainTile(int mapId, int tileX, int tileY, QString & why, bool wdtInspected = false,
                         const UnityIpcServer::WorldCamera * camera = nullptr);
  // THE WORLD (protocol 16): a world load, sent as loadWorld with the world-model budgets -- the tile and the world models
  // its obj0 ADT places, or (globalWmo) a WMO-only map's one world model and no tile (tileX and tileY ignored;
  // globalWmoFileDataID the WDT's world model, the one root the load may fetch, 0 when not known). Refused as
  // RequestTerrainTile is, and without a protocol-16 player. The rest as RequestTerrainTile. To a player that places
  // doodads (protocol 17) the load offers them too, with the doodad budgets (UnityIpcServer::MAX_M2_*); an older one's
  // load carries none, and is served no doodad file.
  // STREAMING (protocol 18): streaming asks a player that streams to keep the tiles around its camera loaded (a tile map's
  // world load only; docs/world-viewer/08-streaming.md).
  int RequestWorld(int mapId, int tileX, int tileY, bool globalWmo, int globalWmoFileDataID, QString & why,
                   bool wdtInspected = false, const UnityIpcServer::WorldCamera * camera = nullptr, bool streaming = false);
  // The tile and WDT a load would name, without the player (RequestTerrainTile's checks after the player's): false with
  // why, or the command filled but for its load and epoch. Reads the map's WDT through the storage (inspect), unless
  // wdtInspected and the catalogue's inspection of it is Available.
  // noTile (a WMO-only map's world load): no tile is named (tileX and tileY -1, not checked).
  bool ResolveTerrainTile(int mapId, int tileX, int tileY, UnityIpcServer::TerrainTileCommand & command, QString & why,
                          bool wdtInspected = false, bool noTile = false);
  // Take the terrain away (clearTerrain): false when there is no player that draws terrain.
  bool ClearTerrain(const QString & reason);
  // THE MAP ASSET WORKER (docs/world-viewer/04b-asset-serving.md), the viewer's side, all on the GUI thread.
  // DispatchMapAsset (UnityIpcServer::onDispatchMapAsset, when the queue serves the request): the decision above; a
  // request let through is accounted (noteMapAssetRead; a texture's limit reserved) and handed to the worker as a read
  // of the loaded client's storage -- the worker granted its own reference to that storage first, when it holds none
  // for this client epoch and storage. CompleteMapAsset (onCompleteMapAsset, when its result is taken back): checked
  // again (MapAssetStaleReason: a read whose client, load or catalogue has gone is answered "stale", its bytes never
  // sent); otherwise the read's own answer -- the inspection's state and detail, the kind of the bytes checked, the
  // answer's md5 the content key the read checked them against -- and its accounting (noteMapAssetResult, the
  // reservation released). MapAssetStaleReason (onMapAssetStale, every poll for every read in flight, and at
  // completion): "" while the request is still the current load's, of the loaded client, else every reason that holds.
  UnityIpcServer::MapAssetDispatch DispatchMapAsset(const QJsonObject & request);
  UnityIpcServer::MapAssetAnswer CompleteMapAsset(const QJsonObject & request, MapAssetWorker::Result & result);
  QString MapAssetStaleReason(const QJsonObject & request) const;
  // A client is being loaded: the reads in flight wait (UnityIpcServer::onMapAssetWait).
  bool MapAssetWait() const;
  // TEST: one request decided, read by the worker and answered as above, waiting here for its result (the
  // -terraintile in-process checks; no player involved, nothing sent).
  UnityIpcServer::MapAssetAnswer ServeMapAssetAndWait(const QJsonObject & request);
  // The worker holds a reference to the loaded client's storage for this client epoch and storage generation, granted now
  // when it did not (false: the folder holds no such storage open).
  bool EnsureMapAssetLease(int epochNow, quint64 generation);
  // TEST: one read through the worker of any file of the loaded client, as a dispatched read is made (the -terraintile
  // in-process checks: too large, encrypted, corrupt, cancelled before its read began, decided under another storage).
  MapAssetWorker::Result TestReadThroughWorker(int fileDataID, qint64 maxBytes, bool corrupt, bool cancelledBefore,
                                               bool otherStorage);
  // TEST: a read of fileDataID given to the worker and held there with its file open (MapAssetWorker::Job::testHold),
  // not waited for: the -terraintile "readshutdown" step leaves it so for the viewer's shutdown. Its ticket, 0 when no
  // read could be given.
  quint64 TestHoldReadForShutdown(int fileDataID);
  // TEST HOOK (the -terraintile "readshutdownserved" step, docs/world-viewer/04b-asset-serving.md 18.1): when set, the
  // destructor calls it at its first statement, immediately before the map asset worker is stopped, and immediately
  // before and after the player is shut down.
  enum class ShutdownPoint
  {
    Begin,
    BeforeWorkerStop,
    BeforePlayerShutdown,
    AfterPlayerShutdown
  };
  static std::function<void(ModelViewer &, ShutdownPoint)> testShutdownProbe;
  // The worker (shared with the IPC server), and the lease it was last granted: client epoch and storage generation.
  std::shared_ptr<MapAssetWorker> m_mapAssetWorker;
  int m_mapAssetLeaseEpoch = 0;
  quint64 m_mapAssetLeaseGeneration = 0;
  void OnTerrainTileLoaded(const UnityIpcServer::TerrainTileReport & report);
  // worldShown (protocol 16): the current world load's scene is on screen (its world models may still be loading).
  void OnWorldShown(const UnityIpcServer::WorldShown & shown);
  // RequestTerrainTile and RequestWorld: one load sent (world: as loadWorld).
  int RequestLoad(int mapId, int tileX, int tileY, bool world, bool globalWmo, int globalWmoFileDataID, QString & why,
                  bool wdtInspected, const UnityIpcServer::WorldCamera * camera, bool streaming = false);
  // The headless terrain test's "staleload" step: a newer load the player never gets (the serial raised, the request
  // moved to it, nothing sent).
  void TerrainTestRaiseLoadWithoutSending();
  // The map catalogue is ready, the loaded client's, and the one the current terrain request was sent under.
  bool terrainCatalogueBelongs() const;
  int m_terrainLoadSerial = 0;          // raised for every loadTerrainTile sent, never otherwise (but the test above)
  TerrainRequest m_terrainRequest;      // the latest load sent; cleared by clearTerrain, a model load, a player restart
  // STREAMING (protocol 18, docs/world-viewer/08-streaming.md 1.7): a streaming session's tile loads beside its entry load
  // (m_terrainRequest), each a TerrainRequest of its own -- its own load id (from m_terrainLoadSerial), budgets and order,
  // the entry's map, epoch and catalogue, its WDT counted served (the session's) -- opened and closed by the player
  // (streamTile, OnStreamTile); a request of a tile load that is not open is stale. All of them end with the session
  // (ResetTerrainRequests: a new load, a clear, a model load, a client change, a player restart).
  std::map<int, TerrainRequest> m_streamLoads;
  bool m_streamSession = false;
  long long m_streamTilesOpened = 0, m_streamTilesClosed = 0, m_streamTilesRefused = 0;
  UnityIpcServer::StreamState m_streamState;   // the player's last streamState of the session
  // The request of a load: the entry's, or an open tile load's (nullptr: neither).
  TerrainRequest * terrainRequestFor(int load);
  const TerrainRequest * terrainRequestFor(int load) const;
  // Every terrain request goes: the entry's and the session's tile loads (the session ends).
  void ResetTerrainRequests();
  // streamTile: the tile loads the player opens (each checked as a tile of the session's map the client has) and closes;
  // the answer (streamTileAnswer) names each with its load id or why it was refused.
  QJsonObject OnStreamTile(const QJsonObject & msg);
  void OnStreamState(const UnityIpcServer::StreamState & state);
  // A file served for one of the session's loads: what it makes allowed session-wide (TerrainRequest::sessionObj0Served
  // ...), noted in the entry and every open tile load.
  void noteStreamSessionServed(const TerrainRequest & own, const QString & role, bool available);
  // The title bar names what the viewer mode shows (Models, Buildings); Textures keeps it.
  void UpdateTitle();
  struct SceneHold
  {
    explicit SceneHold(ModelViewer * v) : viewer(v) { viewer->m_sceneHold++; }
    ~SceneHold() { if (--viewer->m_sceneHold == 0) viewer->SendCharacterSceneToUnity(true); }
    ModelViewer * viewer;
  };

  // The character's appearance on each model of its variant pair, for as long as it is on screen: memory only (no
  // setting, no file), cleared by any load that is not a variant switch and by loading another client.
  struct CharacterVariantSession
  {
    int pairID = 0;
    int raceID = -1;
    int sexID = -1;
    std::map<int, CharDetails::Appearance> byChrModel;
    wxString note;
    void clear() { *this = CharacterVariantSession(); }
  };
  CharacterVariantSession m_variantSession;
  // > 0 while SwitchCharacterVariant rebuilds the character: LoadModel keeps the session, and the Unity player is
  // asked to keep its view.
  int m_variantSwitching = 0;
  struct VariantSwitchScope
  {
    explicit VariantSwitchScope(ModelViewer * v) : viewer(v) { viewer->m_variantSwitching++; }
    ~VariantSwitchScope() { viewer->m_variantSwitching--; }
    ModelViewer * viewer;
  };
  // The character on screen is a creature display (View NPC, Load NPC / Model by Creature Display ID), whatever it
  // looks like: it gets no Model selector.
  bool m_shownAsCreatureDisplay = false;
  // The load whose character's other model generation the player was asked to prefetch (protocol 9), so it is asked
  // once per load: an answer comes for every scene. 0 for none.
  int m_assetHintLoad = 0;
  // Ask the player to prefetch the other model generation of the character on screen, when it has one to switch to.
  void HintVariantPartner();

  // How often the heartbeat above may push while an animation runs. One a second is far below
  // anything a viewer would notice and far above what clock drift needs.
  static const unsigned long ANIM_STATE_HEARTBEAT_MS = 1000;

  void OnMount(wxCommandEvent &event);
  void OnLanguage(wxCommandEvent &event);
  void OnAbout(wxCommandEvent &event);
  void OnTest(wxCommandEvent &event);
  void OnExport(wxCommandEvent &event);
  void OnExportOther(wxCommandEvent &event);
  // Compute the pose of everything in the scene for the current animation frame, without drawing, so
  // an export reads the pose the Animation panel shows. Called before every export (the in-process ones
  // and the headless FBX export child); see the definition.
  void UpdateExportPose();
  
  void UpdateControls();
   
  // An Armory character import in two steps, so the import dialog can show each outcome in
  // place. FetchArmoryCharacter asks the importer plugin for the character behind a link and
  // checks that this build has a model for its race; nothing on screen changes, and a failure
  // comes back as a message (with no character). summary, when given, receives what the
  // importer read beyond CharInfos (server-spelled name, realm, race and class names), or
  // stays empty when the plugin cannot say. ApplyArmoryCharacter loads the race's model and
  // dresses it. ImportArmoury runs both for -armory, and only logs a failure: there is
  // nobody to click a message box away in a headless run.
  CharInfos * FetchArmoryCharacter(const wxString & strURL, wxString & error, QVariantMap * summary = nullptr);
  bool ApplyArmoryCharacter(CharInfos & info, wxString & error);

  // CHARACTER MODEL VARIANTS: a race's model of the other generation (Classic or High Definition), where the loaded
  // client ships one (RaceInfos::VariantPair; Classic Beta). What the character panel's Model selector shows for the
  // character on screen: shown only for a player character of a client with pairs; enabled when its other model can
  // be switched to now, else the reason.
  struct CharacterVariantState
  {
    bool shown = false;
    bool enabled = false;
    CharacterModelVariant current = CharacterModelVariant::Unknown;
    CharacterModelVariant other = CharacterModelVariant::Unknown;
    wxString reason;
    wxString note; // what the first switch to this model could not carry over, until the character changes
  };
  CharacterVariantState characterVariantState() const;
  // Rebuilds the character on screen on its model of the target generation: the same race, sex, equipment, sheathe,
  // animation (by its animation ID, else Stand) and view, presented once. The appearance is remembered per model for
  // as long as this character is on screen -- a model visited before comes back exactly as it was left; the first
  // visit takes over what the client's tables pair, and the target model's defaults for the rest. False and why when
  // refused; the character is then as it was.
  bool SwitchCharacterVariant(CharacterModelVariant target, wxString & why);
  // True while SwitchCharacterVariant rebuilds the character: the character panel builds its rows once, dressed.
  bool variantSwitching() const { return m_variantSwitching > 0; }
  // The Character menu's checks follow a character's toggles (a load sets them to a new character's).
  void SyncCharacterMenuChecks(const WoWModel * m);
  bool ImportArmoury(wxString strURL);
  // A region's realm list as the importer plugin's proxy serves it (see ArmoryImporter::realmList);
  // { ok, unsupported, message, realms }, or ok false when no Armory importer is loaded.
  QVariantMap ArmoryRealmList(const QString & region);
  void ModelInfo();

  void OnGameToggle(wxCommandEvent &event);
  void OnViewLog(wxCommandEvent &event);
  // Load a Battle.net (CASC) client: chosenConfig from the client chooser; null means auto (the -build/-product
  // asked for, otherwise the newest Retail), the headless default. showProgress shows the loading window. The schema
  // is resolved from the opened client itself, never chosen. Atomic: a client that cannot be opened leaves the one
  // already loaded (if any) as it was, and the user is told why in plain words. True when the client loaded.
  // progress: the loading page to report to (the chooser's); none for a load without a window (headless).
  bool LoadWoW(const core::GameConfig * chosenConfig = 0, ClientLoadProgress * progress = nullptr);

  // WHAT THE LOADED CLIENT CAN DO, established from what actually loaded (files indexed, tables read, races
  // resolved) rather than from its product or version. Commands that need something the client lacks are greyed
  // and refused with the reason.
  struct ClientCapabilities
  {
    bool loaded = false;
    bool models = false;              // M2 files indexed
    bool characters = false;          // playable races resolved to their models (RaceInfos)
    bool modernCustomization = false; // ChrCustomizationOption/Choice rows read
    bool npcDisplayInfo = false;      // Creature + CreatureDisplayInfo rows read (View NPC, Load NPC / Model by ID)
    bool items = false;               // the item list (View Item, equipment)
    bool armory = false;              // a Retail-family client with characters (Armory imports Retail characters)
    bool textures = false;            // BLP files indexed
    bool buildings = false;           // WMO files indexed
    bool maps = false;                // the map catalogue is ready (Map.db2 read: docs/world-viewer/02-map-catalog.md)
    int modelFiles = 0, textureFiles = 0, buildingFiles = 0, races = 0, npcCount = 0, itemCount = 0;
    int filesNotInstalled = 0;        // in the build but not on this computer (a partly downloaded install)
    int tablesNotInstalled = 0, tablesNotRead = 0; // the schema check (WoWDatabase::schemaCheck)
    QString schema;                   // the schema the database was read with, and how it was checked
    QStringList unavailable;          // what is not available, and why
  };
  ClientCapabilities m_clientCaps;
  const ClientCapabilities & clientCapabilities() const { return m_clientCaps; }
  // The loaded client's world map catalogue (Map.db2's maps and their WDT references, docs/world-viewer/02-map-catalog.md):
  // built as the client loads, dropped when another client loads (ResetClientState). Never null: before a client is
  // loaded and while one loads it is a catalogue of no client (status NoClient, with the reason). Shared: a holder that
  // outlives the client finds it no longer belongs to the loaded one.
  std::shared_ptr<wow::WorldMapCatalog> mapCatalog() const { return m_mapCatalog; }
  std::shared_ptr<wow::WorldMapCatalog> m_mapCatalog;
  // The loaded client's liquid catalogue (its liquid tables' types and their frames, docs/world-viewer/09-liquids.md 1.6):
  // built as the client loads, beside the map catalogue, and dropped with it. Never null: before a client is loaded and
  // while one loads it is a catalogue of no client. Every load to a player that draws liquids carries its toJson().
  std::shared_ptr<wow::LiquidCatalog> liquidCatalog() const { return m_liquidCatalog; }
  std::shared_ptr<wow::LiquidCatalog> m_liquidCatalog;
  // The loaded client's light catalogue (its light tables' zone polygons, lights and water colours at 12:00,
  // docs/world-viewer/09-liquids.md): built and dropped with the liquid catalogue. Never null: before a client is loaded
  // and while one loads it is a catalogue of no client. Every load to a player that draws liquids carries its mapJson() of
  // the load's map in the liquids offer ("lights").
  std::shared_ptr<wow::LightCatalog> lightCatalog() const { return m_lightCatalog; }
  std::shared_ptr<wow::LightCatalog> m_lightCatalog;
  // THE MAPS VIEWER (docs/world-viewer/05-maps-viewer-ui.md): the fourth viewer mode's state, made with the viewer.
  std::unique_ptr<MapsViewer> m_maps;
  MapsViewer * maps() const { return m_maps.get(); }
  // The loaded client in a line, for the status bar and Advanced details: "Classic Era · Vanilla · 1.15.9".
  QString loadedClientSummary() const;

  // Load a legacy (pre-CASC) client from its MPQ archives instead of CASC. Model loading by path
  // only -- no DBC/database, customization or equipment. locale may be empty to auto-detect.
  // Returns the number of archives opened (0 = no MPQ client found). Leaves the Retail CASC
  // LoadWoW path untouched.
  int LoadWoWFromMpq(const QString & dataFolder, const QString & locale = QString());

  // Prompt for a legacy (pre-CASC) MoPaQ install and load it: folder picker -> LoadWoWFromMpq ->
  // persist + user feedback. Shared by the File menu and the startup Client Choice so both behave
  // identically. Returns archive count on success, 0 on failure (error shown), -1 if cancelled.
  int PromptAndLoadLegacyMpqClient();

  // File > Load Legacy MPQ Client... : thin wrapper over PromptAndLoadLegacyMpqClient().
  void OnLoadLegacyMpq(wxCommandEvent & event);

  // Last legacy-MPQ folder the user picked (persisted in the session config).
  QString m_lastMpqFolder;

  // The Models viewport's background (viewportBackground), and what Config.ini holds for it.
  wxColour m_viewportBackground;
  wxColour m_viewportBackgroundKept;
  // Its window (View > Swap Background Color), made when first asked for; hidden, not destroyed, when closed.
  BackgroundColorDialog * m_backgroundDialog = nullptr;
  // The window was open when another viewer put it away: Models gives it back.
  bool m_backgroundDialogPutAway = false;

};

#endif
