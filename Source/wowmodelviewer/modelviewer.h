
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

#include "glm/glm.hpp"

#include <QString>

class SettingsControl;
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
  ImageControl *imageControl;
  //SoundControl *soundControl;
  SettingsControl *settingsControl;
  // Embedded Unity viewport pane -- the new renderer foundation (the OpenGL canvas is the
  // legacy/fallback viewport during the migration). Currently optional: created lazily on
  // first View > Unity Renderer use or by the -unityipctest self-test (nullptr until then).
  // See UnityRendererHost.h and docs/unity-renderer/README.md.
  UnityRendererHost *unityRendererHost;

  CAnimationExporter *animExporter;

  FileControl *fileControl;

  //wxWidget objects
  wxMenuBar *menuBar;
  wxMenu *fileMenu, *exportMenu, *camMenu, *charMenu, *charGlowMenu, *viewMenu, *optMenu, *lightMenu;
  wxColourData bgDialogData;

  // wxAUI - new docking lib (now part of wxWidgets 2.8.0)
  wxAuiManager interfaceManager;

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
  // save + load character *.CHR files
  void LoadChar(QString fn, bool equipmentOnly = false);
  void SaveChar(QString fn, bool equipmentOnly = false);

  void LoadModel(GameFile * f);
  void LoadItem(unsigned int displayID);
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
  // Runtime command to the embedded Unity player: "this is the active model" (path +
  // FileDataID of the model on the canvas). No-op when no player is connected. Called after
  // every model load and when the player announces unityReady.
  void SendCurrentModelToUnity();

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

