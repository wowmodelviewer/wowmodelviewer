
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
#include "charcontrol.h"
#include "lightcontrol.h"
#include "modelcontrol.h"
#include "effects.h"
#include "filecontrol.h"
#include "UnityIpcServer.h"

#include "glm/glm.hpp"

#include <QString>

class SettingsControl;
class ModelInspector;
class wxAuiToolBar;
class ExportJobManager;
class UnityRendererHost;

namespace core { class GameConfig; }

namespace WMVLog
{
  class Logger;
}

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
  bool isADT;
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
  int m_exportNpcId = -1;
  int m_exportNpcDisplayId = 0;
  int m_exportItemSkinFileId = 0;

  ExportJobManager * m_exportJobManager = nullptr;

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
  void SaveChar(QString fn, bool equipmentOnly = false);

  void LoadModel(GameFile * f);
  void LoadItem(unsigned int displayID);
  // The component-geoset state an item's own model should be shown with. See the definition.
  void applyItemComponentGeosets(unsigned int itemId);
  void LoadNPC(unsigned int modelid);
  // Register an NPC in the in-memory DB (if not already present) and load it. Shared by the
  // "Import NPC from URL" dialog flow and the -npc headless test harness.
  void LoadNPCByDisplay(int npcId, int displayId, int type = 0, const QString & name = QString("npc"));

  // Window GUI event related functions
  //void OnIdle();
  void OnClose(wxCloseEvent &event);
  void OnSize(wxSizeEvent &event);
  void OnExit(wxCommandEvent &event);
  void OnRestart(wxCommandEvent &event); // File > Restart: relaunch the app in one click
  void UpdateCanvasStatus();

  // menu commands
  void OnToggleDock(wxCommandEvent &event);
  void OnToggleCommand(wxCommandEvent &event);
  void OnEffects(wxCommandEvent &event);

  // Wrapper function for character stuff (forwards events to charcontrol)
  void OnSetEquipment(wxCommandEvent &event);
  void OnCharToggle(wxCommandEvent &event);
  void OnImportNPCFromURL(wxCommandEvent &event);  // direct "Import NPC from URL" menu entry

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

  // Whether the Unity viewport is the shown centre pane (it always should be; the self-test checks).
  bool isUnityViewportCentre();
  // Whether the Unity viewport is showing the loaded model: the player is connected and what is
  // loaded is something it can draw (no notice in front of it).
  bool isUnityViewportShowingModel();
  // Whether the Unity viewport has a notice in front of the player (content it cannot draw yet, a
  // player that is not running, nothing loaded), as last decided by UpdateUnityViewportState. True
  // when there is no viewport at all.
  bool unityViewportHasNotice() const;

  // Something new is on screen (a model, character, WMO or map tile): the Model panel, the
  // command bar's model name, the status bar facts and the viewport (model or notice) follow it. The one
  // place every load path reports to.
  void DisplayedContentChanged();

  // The command bar along the top: open, fullscreen, the current model and the three panel toggles.
  void InitCommandBar();
  void OnCommandBar(wxCommandEvent & event);
  void OnUpdateCommandUI(wxUpdateUIEvent & event);
  void OnKeyboardShortcuts(wxCommandEvent & event);
  void UpdateStatusFacts();

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
  // The name of the image last picked in Browse, for the viewport's notice; empty otherwise. The image
  // itself is not loaded anywhere, so nothing else records that an image, not a model, is what the
  // user picked.
  wxString m_browseImageName;

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
  // Dismounting hands the viewport back without a load, and a player that connected while the mount was
  // up was sent the mount.
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
  struct SceneHold
  {
    explicit SceneHold(ModelViewer * v) : viewer(v) { viewer->m_sceneHold++; }
    ~SceneHold() { if (--viewer->m_sceneHold == 0) viewer->SendCharacterSceneToUnity(true); }
    ModelViewer * viewer;
  };

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
   
  void ImportArmoury(wxString strURL);
  void ModelInfo();

  void OnGameToggle(wxCommandEvent &event);
  void OnViewLog(wxCommandEvent &event);
  // chosenConfig/profileOverride come from the startup Client Choice launcher; both null/empty
  // means "auto" (detect configs + derive the profile from the version), the headless default.
  // showProgress displays the "Loading Client" progress dialog during the (synchronous) load.
  void LoadWoW(const core::GameConfig * chosenConfig = 0, const QString & profileOverride = QString(),
               bool showProgress = false);

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

};

#endif

