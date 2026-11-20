
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
class ViewportHost;
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
  // THE VIEWPORT: the centre pane, docked at construction (InitDocking) and never removed. It shows the canvas
  // (ModelCanvas: the scene, drawn natively with Vulkan), a notice in its place (the empty viewer, content it cannot
  // draw yet, a renderer that could not start), or another viewer's content (the texture view). What it shows is
  // decided by UpdateViewportState. See ViewportHost.h.
  ViewportHost *viewportHost;

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
  // A texture picked in Browse (Textures mode), shown in the viewport's place: the viewport host's
  // content window (ViewportHost::showContent). Created with the viewport.
  TextureView * textureView = nullptr;

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
  // panes it does not describe. See LoadLayout. 3: the viewport is always the centre pane, so a perspective
  // naming a "canvas" pane is discarded.
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
#ifdef __WXMSW__
  WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;
#endif
protected:
  // A command of another viewer (needsModelViewer, needsViewport) is refused before any handler sees it.
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

  // Create the viewport (its host panel and the canvas in it). Called once, by InitDocking, which docks it as the
  // centre pane.
  void CreateViewport();
  // The centre pane's settings, shared by InitDocking, ResetLayout and LoadLayout so all three dock
  // the viewport identically.
  wxAuiPaneInfo viewportPaneInfo() const;
  // Make sure the viewport is the shown centre pane (a saved perspective or a reset must never
  // leave the middle of the window to anything else). Returns true if the layout had to change.
  bool EnsureViewportCentre();

  // View > Swap Background Color: the Models viewport's background window, made the first time and shown again after.
  void OnBackgroundColor(wxCommandEvent & event);

  // WHAT THE VIEWPORT SHOWS for what is currently loaded: the canvas, or a notice painted by the host panel -- the
  // empty viewer's prompt, content it cannot draw yet, a renderer that could not start. Called on every path that
  // changes what is loaded (model, NPC, item and character loads and their failures, Browse selections, clearing)
  // and when a mount is put on or taken off (from the canvas tick).
  void UpdateViewportState();
  // Lay the panes out again ONLY if a pane's shown state changed. See the definition for why
  // an unconditional interfaceManager.Update() is a whole-window blink on Windows.
  bool CommitLayoutIfChanged();
  // An unbatched layout made now (a command's, a settings page's): a viewer switch on its way finished first.
  void LayoutNow();

  // The viewer-first startup: take the screen (the panels keep the shown state the saved layout restored). Safe to
  // call before a client is loaded.
  void ApplyViewerStartupLayout();

  // Show the Client Choice dialog and load whatever the user picks. This is the ONLY thing that
  // loads a client now -- File > "Load World of Warcraft" calls it, and nothing calls it at
  // startup. Loops so a failed legacy-MPQ pick returns to the dialog rather than giving up.
  void PromptAndLoadClient();
  // THE VIEWER MODE (ViewerMode.h): Models (the viewport and the model panels), Textures (the texture
  // viewer in the viewport's place, the model panels put away) or Buildings (the viewport with a world
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
  bool needsViewport(int id) const;
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

  // Whether the viewport is the shown centre pane (it always should be; the self-test checks).
  bool isViewportCentre();
  // Whether the viewport is showing the loaded model: the canvas is on screen with what is loaded.
  bool isViewportShowingModel();
  // Whether the viewport has a notice in front of the canvas (content it cannot draw yet, a renderer that could not
  // start, nothing loaded) or a texture, as last decided by UpdateViewportState. True when there is no viewport.
  bool viewportHasNotice() const;

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

  // THE MODELS VIEWPORT'S BACKGROUND (View > Swap Background Color; ViewportBackground.h): the colour the viewport
  // clears to behind a model, as the sRGB bytes it displays as. Loaded with the session.
  const wxColour & viewportBackground() const { return m_viewportBackground; }
  // Show colour now; kept in Config.ini when persist and it differs from what is kept there. The background window
  // follows.
  void setViewportBackground(const wxColour & colour, bool persist);
  // The colour the viewport clears to for what the canvas holds: a world model (the Buildings viewer's) on the
  // viewport's default, anything else on the Models viewport's background.
  wxColour viewportBackgroundShown() const;
  // The canvas and the host panel given that colour, when it is not the one they have. True when it changed.
  bool pushViewportBackground();

  // THE VIEWPORT SCREENSHOT. The command bar's Screenshot: a Save As dialog (PNG, overwrite confirmed, named after
  // the model and the time), then RequestScreenshot. Cancelling does nothing.
  void SaveScreenshot();
  // A SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT PNG of what the viewport shows, written to path (a full path; ".png" is
  // added when it does not end in it). Returns true, or false with why (and the status bar says why as well). No
  // dialog or message box is ever shown here, so a headless test can call it directly.
  bool RequestScreenshot(const wxString & path, wxString & why);
  // The name Save As starts from: the model's (the character's, while it rides), sanitised, and the local time,
  // "<model>_<yyyy-MM-dd_HHmmss>.png".
  wxString DefaultScreenshotName() const;
  static const int SCREENSHOT_WIDTH = 3840;
  static const int SCREENSHOT_HEIGHT = 2160;

  // What the viewport paints instead of the canvas: a title, a detail line and an optional
  // button (the menu command it posts, 0 for none).
  struct ViewportNotice
  {
    wxString title;
    wxString detail;
    wxString actionLabel;
    int actionId = 0;
  };
  // Can the viewport draw what is currently loaded? False for nothing loaded and for content it cannot draw yet;
  // notice (when given) then says what it is and why.
  bool canDrawCurrentModel(ViewportNotice * notice = nullptr) const;
  // The whole decision UpdateViewportState applies: true with the notice to paint, or false when the canvas should
  // be on screen.
  bool viewportNotice(ViewportNotice & notice) const;
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
  // only the difference (decided with the viewport, in UpdateViewportState, and laid out at once).
  // Browse stays. The panels put away cannot be shown meanwhile (their toggles are another viewer's:
  // needsModelViewer, needsViewport), so they come back as they were. The layout saved on exit is the
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
  // The canvas shows a playable character (not a mount carrying one).
  bool canvasShowsCharacter() const;
  // THE RIDER: in a character context, the character model -- CharControl::model, the model of its attachment
  // node CharControl::charAtt -- whether or not it rides a mount; null in any other context. While a mount is
  // up the canvas model is the mount, so this, not canvas->model(), is the character.
  WoWModel * riderModel() const;
  // The mount the rider rides: the WoWModel on the rider node's parent (the canvas root, where the mount choice
  // puts it), or null when that holds none.
  WoWModel * riderMount() const;
  // The canvas shows a playable character riding a mount: a character context, a rider that is a character
  // model with a FileDataID, and a mount on its node's parent.
  bool canvasShowsMountedCharacter() const;
  // The model whose equipment the Model panel's hands-only equipment controls act on: a racial character or its
  // rider, or a live ordinary NPC; null otherwise.
  WoWModel * equipmentOwner() const;
  // Whether the canvas showed a character, and a mounted one, when the viewport state was last decided: mounting
  // and dismounting change it without a load, and the viewport has to follow from the tick.
  bool m_lastShowsCharacter = false;
  bool m_lastShowsMountedCharacter = false;
  // HIDDEN WINDOWS STAY FROZEN (parkHidden): the frame's windows a viewer-mode switch leaves hidden (and hidden ones with
  // windows of their own inside shown ones), each with a freeze of its own until it is shown again.
  std::set<wxWindow *> m_parked;
  std::set<wxWindow *> m_parkWatched;
  bool m_parkIdleBound = false;
  std::shared_ptr<bool> m_parkAlive = std::make_shared<bool>(true);   // the handlers' guard: false once the frame goes
  void parkHidden();
  void unpark(wxWindow * window);
  void OnParkedIdle(wxIdleEvent & event);
  // The title bar names what the viewer mode shows (Models, Buildings); Textures keeps it.
  void UpdateTitle();
  // A character loaded and dressed in several steps (a .chr, an Armory import, an NPC): the viewport follows it once,
  // when it is complete.
  struct SceneHold
  {
    explicit SceneHold(ModelViewer * v) : viewer(v) { viewer->m_sceneHold++; }
    ~SceneHold() { if (--viewer->m_sceneHold == 0) viewer->UpdateViewportState(); }
    ModelViewer * viewer;
  };
  int m_sceneHold = 0;

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
  // > 0 while SwitchCharacterVariant rebuilds the character: LoadModel keeps the session and the camera.
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
