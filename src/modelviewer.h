
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

//#include "CShader.h"

// defines
#define APP_TITLE wxT("World of Warcraft Model Viewer")
#define APP_VERSION wxT("v0.7.0.6")
#define APP_BUILDNAME wxT("Skeer the Bloodseeker")	// Fun thing for developers to play with. Should change with each Main Release. (Not counting DEV WORK editions)
/*
	--==List of Build Name ideas==--	(Feel free to add!)
	Hoppin Jalapeno
	Stealthed Rogue
	Deadly Druid
	Killer Krakken
	Crazy Kaelthas
	Lonely Mastiff
	Cold Kelthuzad
	Jiggly Jaina
	Vashj's Folly
	Epic Win
	Epic Lose
	Lord Kezzak
	Perky Pug

	--== Used Build Names ==--			(So we don't repeat...)
	Bouncing Baracuda
	Wascally Wabbit
	Gnome Punter
	Fickle Felguard
	Demented Deathwing
	Pickled Herring
	Windrunner's Lament
	Lost Lich King
	Great-father Winter
	Chen Stormstout

*/

#ifdef _DEBUG
	#define APP_ISDEBUG wxT(" Debug")
#else
	#define APP_ISDEBUG wxT("")
#endif

// This should only be touched when adding a new OS or platform.
#if defined (_WINDOWS)
	#if defined (_WIN64)
		#define APP_PLATFORM wxT("Windows 64-bit")
	#elif defined (_WIN32)
		#define APP_PLATFORM wxT("Windows 32-bit")
	#else
		#error wxT("Your Windows platform is not defined. Please specify either _WIN64 or _WIN32.")
	#endif
#elif defined (_MAC)
	#if defined (_MAC_INTEL)
		#define APP_PLATFORM wxT("Macintosh Intel")
	#elif defined (_MAC_PPC)
		#define APP_PLATFORM wxT("Macintosh PowerPC")
	#else
		#error wxT("Your Macintosh platform is not defined. Please specify either _MAC_INTEL or _MAC_PPC.")
	#endif
#elif defined (_LINUX)
	#if defined (_LINUX64)
		#define APP_PLATFORM wxT("Linux 64-bit")
	#elif defined (_LINUX32)
		#define APP_PLATFORM wxT("Linux 32-bit")
	#else
		#error wxT("Your Linux platform is not defined. Please specify either _LINUX64 or _LINUX32.")
	#endif
#else
	#ifdef _DEBUG
		#error wxT("You have not specified a valid Operating System for your Debug configuration.")
	#else
		#error wxT("You have not specified a valid Operating System for your Release configuration.")
	#endif
#endif

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

