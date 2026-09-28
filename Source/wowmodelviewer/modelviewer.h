
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

#include <memory>

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
struct TextureEntry;
class CharInfos;

namespace core { class GameConfig; }

namespace WMVLog
{
  class Logger;
}

class UiMenuBarTitles;

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
  bool PrepareFbxAsset(wxString & args, wxString & label, wxString & tempCharPath);

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
  // commits it with exactly one wxAuiManager::Update(). See SetViewerMode.
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

  // The command bar along the top: open, fullscreen, the current model and the three panel toggles.
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
  enum class PlayerContent { Nothing, Model, MapObject, Unknown };
  PlayerContent m_playerContent = PlayerContent::Nothing;   // what the player was last known to show
  int m_mapObjectAwaited = 0;   // the load serial of a world model built after another kind, until reported
  int m_modelAwaited = 0;       // the FileDataID of a model built after a world model, until the player has it
  int m_modelAwaitedFailed = 0; // that model, when the player finished without it (it still shows the world model)
  wxStopWatch m_modelAwaitedClock;   // since the wait began (monotonic)
  wxTimer m_playerContentPoll;
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
  bool LoadWoW(const core::GameConfig * chosenConfig = 0, bool showProgress = false);

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
    int modelFiles = 0, textureFiles = 0, buildingFiles = 0, races = 0, npcCount = 0, itemCount = 0;
    int filesNotInstalled = 0;        // in the build but not on this computer (a partly downloaded install)
    int tablesNotInstalled = 0, tablesNotRead = 0; // the schema check (WoWDatabase::schemaCheck)
    QString schema;                   // the schema the database was read with, and how it was checked
    QStringList unavailable;          // what is not available, and why
  };
  ClientCapabilities m_clientCaps;
  const ClientCapabilities & clientCapabilities() const { return m_clientCaps; }
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
