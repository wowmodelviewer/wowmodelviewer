
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
#include "imagecontrol.h"
#include "AnimExporter.h"
#include "effects.h"
#include "ColorPickerDialog.h"
#include "filecontrol.h"
#include "UnityIpcServer.h"

#include "glm/glm.hpp"

#include <QString>

class SettingsControl;
class ModelInspector;
class wxAuiToolBar;
class ExportJobManager;
class ImageSequenceExporter;
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
  ImageControl *imageControl;
  //SoundControl *soundControl;
  SettingsControl *settingsControl;
  // Embedded Unity viewport pane -- the new renderer foundation (the OpenGL canvas is the
  // legacy/fallback viewport during the migration). Currently optional: created lazily on
  // first View > Unity Renderer use or by the -unityipctest self-test (nullptr until then).
  // See UnityRendererHost.h and docs/unity-renderer/README.md.
  UnityRendererHost *unityRendererHost;
  // timeGetTime() of the last playback-state push, for the heartbeat in
  // SendAnimationStateToUnity. 0 until the first push.
  unsigned long m_lastAnimStatePush;

  CAnimationExporter *animExporter;

  FileControl *fileControl;

  //wxWidget objects
  wxMenuBar *menuBar;
  wxMenu *fileMenu, *exportMenu, *camMenu, *charMenu, *charGlowMenu, *viewMenu, *optMenu, *lightMenu;
  wxColourData bgDialogData;

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
  ImageSequenceExporter * m_imgSeqExporter = nullptr;

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
  // panes it does not describe. See LoadLayout.
  static const int LAYOUT_VERSION = 2;
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
  void SetCanvasSize(uint32 sizex, uint32 sizey);

  // menu commands
  void OnToggleDock(wxCommandEvent &event);
  void OnToggleCommand(wxCommandEvent &event);
  void OnSetColor(wxCommandEvent &event);
  void OnEffects(wxCommandEvent &event);
  void OnLightMenu(wxCommandEvent &event);
  void OnCamMenu(wxCommandEvent &event);

  // Wrapper function for character stuff (forwards events to charcontrol)
  void OnSetEquipment(wxCommandEvent &event);
  void OnCharToggle(wxCommandEvent &event);
  void OnImportNPCFromURL(wxCommandEvent &event);  // direct "Import NPC from URL" menu entry

  // View > Unity Renderer: show the embedded Unity viewport -- the new renderer foundation,
  // optional at this migration stage (lazy pane + player launch). See UnityRendererHost.h.
  void OnUnityRenderer(wxCommandEvent &event);
  // Create the Unity pane on first use, show it and launch the player (the body of the menu
  // handler, also used by the headless -unityipctest run). selfTest asks a diagnostic-capable
  // player to also exercise the protocol's error paths -- never set for a normal menu launch.
  // Returns false if the player could not be started (already reported unless batchMode).
  bool ShowUnityRenderer(bool selfTest = false);

  // WHICH VIEWPORT IS THE MAIN ONE for what is currently loaded.
  //
  // The Unity renderer takes the centre for the models it supports; everything else stays on the
  // OpenGL canvas. Called after every model load, and when the View toggle changes, so the two
  // can never disagree about who owns the middle of the window.
  void UpdatePrimaryViewport();
  // Lay the panes out again ONLY if a pane's shown state changed. See the definition for why
  // an unconditional interfaceManager.Update() is a whole-window blink on Windows.
  bool CommitLayoutIfChanged();

  // Open and start the Unity viewport at APP LAUNCH, before any model exists, so that picking
  // the first creature does not also pay for starting a game engine. No-op in batch mode, when
  // the user has turned the primary viewport off, or when no player build is installed.
  void WarmStartUnityViewport();

  // The parts of the viewer-first startup that involve NO player and NO IPC: hide the panes and
  // take the screen. Safe to call before a client is loaded, which is the point -- see the note
  // on WarmStartUnityViewport for why the player itself must wait.
  void ApplyViewerStartupLayout();

  // Show the Client Choice dialog and load whatever the user picks. This is the ONLY thing that
  // loads a client now -- File > "Load World of Warcraft" calls it, and nothing calls it at
  // startup. Loops so a failed legacy-MPQ pick returns to the dialog rather than giving up.
  void PromptAndLoadClient();

  // Put the OpenGL canvas back in the centre and the Unity pane back to a side pane.
  void UncoverOpenGLViewport();
  bool unityAsidePaneShown();

  // Whether the Unity viewport is the one on screen (the centre pane, with the canvas hidden).
  bool isUnityViewportOnScreen();
  // Whether a Unity viewport is showing the loaded model anywhere -- the centre, or the side pane
  // when it is not the main viewport -- with the player connected.
  bool isUnityViewportShowingModel();

  // Something new is on screen (a model, character, WMO or map tile): the Model panel, the
  // command bar's model name, the status bar facts and the empty viewport follow it. The one
  // place every load path reports to.
  void DisplayedContentChanged();

  // The command bar along the top: open, reset camera, screenshot, fullscreen, the current model
  // and the three panel toggles.
  void InitCommandBar();
  void OnCommandBar(wxCommandEvent & event);
  void OnUpdateCommandUI(wxUpdateUIEvent & event);
  void OnKeyboardShortcuts(wxCommandEvent & event);
  void UpdateStatusFacts();
  // The empty viewport's prompt, which depends on whether a client is loaded yet.
  void UpdateEmptyState();

  // Can the Unity viewport show what is currently loaded? Creature M2s, for now: no characters
  // (no equipment pipeline yet) and nothing that is not an M2.
  bool unityCanShowCurrentModel() const;

  void OnUnityPrimaryViewport(wxCommandEvent & event);
  void OnToggleFullScreen(wxCommandEvent & event);
  void OnCharHook(wxKeyEvent & event);

  // Borderless fullscreen that keeps the menu bar, so the mode can always be left.
  void EnterViewerFullScreen(bool full);
  // Runtime command to the embedded Unity player: "this is the active model" (path +
  // FileDataID of the model on the canvas). No-op when no player is connected. Called after
  // every model load and when the player announces unityReady.
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
  // every frame -- it rate-limits itself and no-ops when the Unity pane was never opened.
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
  // Whether the canvas showed a character the viewport can dress when the routing was last decided
  // (UpdatePrimaryViewport records it): mounting and dismounting change it without a load, and the
  // viewport routing has to follow from the tick.
  bool m_lastShowsCharacter = false;
  // The character model the Unity player could not build: the canvas keeps it until a load of another
  // model, a player (re)start, or a later load of the same model that the player does dress.
  int m_unityCharacterFailed = 0;
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
  void OnSave(wxCommandEvent &event);
  void OnBackground(wxCommandEvent &event);
  void OnLanguage(wxCommandEvent &event);
  void OnAbout(wxCommandEvent &event);
  void OnCanvasSize(wxCommandEvent &event);
  void OnTest(wxCommandEvent &event);
  void OnExport(wxCommandEvent &event);
  void OnExportOther(wxCommandEvent &event);
  void OnExportImageSequence(wxCommandEvent &event);
  
  void UpdateControls();
   
  void ImportArmoury(wxString strURL);
  void ModelInfo();

  glm::vec3 DoSetColor(const glm::vec3 &defColor);

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

