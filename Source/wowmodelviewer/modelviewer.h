
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
#include "modelbankcontrol.h"
#include "filecontrol.h"

#include "glm/glm.hpp"

#include <QString>

class SettingsControl;

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
  ModelBankControl *modelbankControl;

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

  void OnMount(wxCommandEvent &event);
  void OnSave(wxCommandEvent &event);
  void OnBackground(wxCommandEvent &event);
  void OnLanguage(wxCommandEvent &event);
  void OnAbout(wxCommandEvent &event);
  void OnCanvasSize(wxCommandEvent &event);
  void OnTest(wxCommandEvent &event);
  void OnExport(wxCommandEvent &event);
  void OnExportOther(wxCommandEvent &event);
  
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

};

#endif

