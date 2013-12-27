
#ifndef MODELVIEWER_H
#define MODELVIEWER_H

// wx
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/treectrl.h>
#include <wx/colordlg.h>
#include <wx/msgdlg.h>
#include <wx/display.h>
#include <wx/aboutdlg.h>
#if defined(__WIN32__) && !defined(__WIN__)
	#include <winsock.h>
#endif
#include <wx/url.h>
#include <wx/xml/xml.h>
#include <wx/wfstream.h>

//wxAUI
#include <wx/aui/aui.h>

// Our files
#include "modelcanvas.h"
#include "animcontrol.h"
#include "charcontrol.h"
#include "lightcontrol.h"
#include "modelcontrol.h"
#include "imagecontrol.h"
#include "util.h"
#include "AnimExporter.h"
#include "effects.h"
#include "arrows.h"
#include "modelexport.h"
#include "settings.h"
#include "modelbankcontrol.h"
#include "filecontrol.h"
#include "modelexportoptions.h"

#include "enums.h"

class ModelViewer: public wxFrame
{    
    DECLARE_CLASS(ModelViewer)
    DECLARE_EVENT_TABLE()

	std::vector<MPQArchive*> archives;

public:
    // MPQ archive init status
    enum ArchiveInitStatus
    {
    	ARCHIVEINITSTATUS_NOERROR = 0, 			// No error
    	ARCHIVEINITSTATUS_WOWRUNNING_ERROR = 1, // Impossible to read datas into MPQ archive (WoW is running)
    	ARCHIVEINITSTATUS_TOCREADING_ERROR = 2, // Problem with reading TOC information
    	ARCHIVEINITSTATUS_WOWVERSION_ERROR = 3  // Unsupported WoW version found
    };

	// Constructor + Deconstructor
	ModelViewer();
	~ModelViewer();

	// our class objects
	AnimControl *animControl;
	ModelCanvas *canvas;
	CharControl *charControl;
	EnchantsDialog *enchants;
	LightControl *lightControl;
	ModelControl *modelControl;
	ArrowControl *arrowControl;
	ImageControl *imageControl;
	//SoundControl *soundControl;
	SettingsControl *settingsControl;
	ModelBankControl *modelbankControl;
	ModelOpened *modelOpened;
	ModelExportOptions_Control *exportOptionsControl;

	CAnimationExporter *animExporter;

	FileControl *fileControl;

	//wxWidget objects
	wxMenuBar *menuBar;
	wxMenu *fileMenu, *exportMenu, *camMenu, *charMenu, *charGlowMenu, *viewMenu, *optMenu, *lightMenu;
	
	// wxAUI - new docking lib (now part of wxWidgets 2.8.0)
	wxAuiManager interfaceManager;

	// Boolean flags
	bool isWoWLoaded;
	bool isModel;
	bool isChar;
	bool isWMO;
	bool isADT;
	bool initDB;

	// Initialising related functions
	void InitMenu();
	void InitObjects();
	wxString Init();
	void InitDocking();
	void InitDatabase();
	ArchiveInitStatus InitMPQArchives();

	// Save and load various settings between sessions
	void LoadSession();
	void SaveSession();
	// Save and load the GUI layout
	void LoadLayout();
	void SaveLayout();
	void ResetLayout();
	// save + load character *.CHR files
	void LoadChar(wxString fn);
	void SaveChar(wxString fn);

	void LoadModel(const wxString fn);
	void LoadItem(unsigned int displayID);
	void LoadNPC(unsigned int modelid);

	// Window GUI event related functions
	//void OnIdle();
	void OnClose(wxCloseEvent &event);
	void OnSize(wxSizeEvent &event);
    void OnExit(wxCommandEvent &event);


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

	void OnMount(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnBackground(wxCommandEvent &event);
	void OnLanguage(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void DownloadLocaleFiles();
	void OnCheckForUpdate(wxCommandEvent &event);
	void OnCanvasSize(wxCommandEvent &event);
	void OnTest(wxCommandEvent &event);
	void OnExport(wxCommandEvent &event);
	void OnExportOther(wxCommandEvent &event);
	
	void UpdateControls();
   
	void ImportArmoury(wxString strURL);
	void ModelInfo();

	Vec3D DoSetColor(const Vec3D &defColor);

	void OnGameToggle(wxCommandEvent &event);
	void OnViewLog(wxCommandEvent &event);
	void LoadWoW();
};

#endif

