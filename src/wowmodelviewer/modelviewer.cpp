#include "modelviewer.h"

#include "AnimationExportChoiceDialog.h"

#include <wx/aboutdlg.h>
#include <wx/busyinfo.h>
#include <wx/colordlg.h>
#include <wx/colour.h>
#include <wx/filedlg.h>
#include <wx/filename.h>

#include "CharInfos.h"
#include "ExporterPlugin.h"
#include "GlobalSettings.h"
#include "ImporterPlugin.h"
#include "PluginManager.h"
#include "Attachment.h"
#include "Bone.h"
#include "CASCFile.h"
#include "CASCFolder.h"
#include "globalvars.h"
#include "GameDatabase.h"
#include "Game.h"
#include "ModelColor.h"
#include "ModelEvent.h"
#include "ModelTransparency.h"
#include "RaceInfos.h"
#include "TextureAnim.h"
#include "logger/Logger.h"
#include "app.h"
#include "AnimationExportChoiceDialog.h"
#include "SettingsControl.h"
#include "util.h"
#include "UserSkins.h"

#include <QFile>
#include <QSettings>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

// default colour values
const static float def_ambience[4] = {1.0f, 1.0f, 1.0f, 1.0f};
const static float def_diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
const static float def_emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
const static float def_specular[4] = {1.0f, 1.0f, 1.0f, 1.0f};

// Class event handler/importer
IMPLEMENT_CLASS(ModelViewer, wxFrame)

BEGIN_EVENT_TABLE(ModelViewer, wxFrame)
	EVT_CLOSE(ModelViewer::OnClose)
	//EVT_SIZE(ModelViewer::OnSize)

	// File menu
	EVT_MENU(ID_LOAD_WOW, ModelViewer::OnGameToggle)
	EVT_MENU(ID_FILE_VIEWLOG, ModelViewer::OnViewLog)
	EVT_MENU(ID_VIEW_NPC, ModelViewer::OnCharToggle)
	EVT_MENU(ID_VIEW_ITEM, ModelViewer::OnCharToggle)
	EVT_MENU(ID_FILE_SCREENSHOT, ModelViewer::OnSave)
	EVT_MENU(ID_FILE_SCREENSHOTCONFIG, ModelViewer::OnSave)
	EVT_MENU(ID_FILE_EXPORTGIF, ModelViewer::OnSave)
	EVT_MENU(ID_FILE_EXPORTAVI, ModelViewer::OnSave)
	// --
	EVT_MENU(ID_FILE_MODEL_INFO, ModelViewer::OnExportOther)
	//--
	EVT_MENU(ID_FILE_RESETLAYOUT, ModelViewer::OnToggleCommand)
	// --
	EVT_MENU(ID_FILE_EXIT, ModelViewer::OnExit)

	// view menu
	EVT_MENU(ID_SHOW_FILE_LIST, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_ANIM, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_CHAR, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_LIGHT, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_MODEL, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_MODELBANK, ModelViewer::OnToggleDock)	
	// --
	EVT_MENU(ID_SHOW_MASK, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_WIREFRAME, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_BONES, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SHOW_BOUNDS, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_PARTICLES, ModelViewer::OnToggleCommand)

	EVT_MENU(ID_BACKGROUND, ModelViewer::OnBackground)
	EVT_MENU(ID_BG_COLOR, ModelViewer::OnSetColor)
	EVT_MENU(ID_SKYBOX, ModelViewer::OnBackground)
	EVT_MENU(ID_SHOW_GRID, ModelViewer::OnToggleCommand)

	EVT_MENU(ID_USE_CAMERA, ModelViewer::OnToggleCommand)

	// Cam
	EVT_MENU(ID_CAM_FRONT, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_SIDE, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_BACK, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_ISO, ModelViewer::OnCamMenu)

	EVT_MENU(ID_CANVASS120, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASS512, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASS1024, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASF480, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASF600, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASF768, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASF864, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASF1200, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASW480, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASW720, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASW1080, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASM768, ModelViewer::OnCanvasSize)
	EVT_MENU(ID_CANVASM1200, ModelViewer::OnCanvasSize)

	// hidden hotkeys for zooming
	EVT_MENU(ID_ZOOM_IN, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_ZOOM_OUT, ModelViewer::OnToggleCommand)

	// Light Menu
	EVT_MENU(ID_LT_SAVE, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_LOAD, ModelViewer::OnLightMenu)
	//EVT_MENU(ID_LT_COLOR, ModelViewer::OnSetColor)
	EVT_MENU(ID_LT_TRUE, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_AMBIENT, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_DIRECTIONAL, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_MODEL, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_DIRECTION, ModelViewer::OnLightMenu)
	
	// Effects
	EVT_MENU(ID_ENCHANTS, ModelViewer::OnEffects)

	// Options
	EVT_MENU(ID_SAVE_CHAR, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_LOAD_CHAR, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_IMPORT_CHAR, ModelViewer::OnToggleCommand)

	EVT_MENU(ID_DEFAULT_DOODADS, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_ANTIALIAS, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_HWACC, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_ENVMAP, ModelViewer::OnToggleCommand)
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
	EVT_MENU(ID_CHECKFORUPDATE, ModelViewer::OnCheckForUpdate)
	EVT_MENU(ID_LANGUAGE, ModelViewer::OnLanguage)
	EVT_MENU(ID_HELP, ModelViewer::OnAbout)
	EVT_MENU(ID_ABOUT, ModelViewer::OnAbout)

	// Hidden menu items
	// Temporary saves
	EVT_MENU(ID_SAVE_TEMP1, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SAVE_TEMP2, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SAVE_TEMP3, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SAVE_TEMP4, ModelViewer::OnToggleCommand)

	// Temp loads
	EVT_MENU(ID_LOAD_TEMP1, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_LOAD_TEMP2, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_LOAD_TEMP3, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_LOAD_TEMP4, ModelViewer::OnToggleCommand)

	// Export
	EVT_MENU(ID_EXPORT_MODEL, ModelViewer::OnExport)

END_EVENT_TABLE()

ModelViewer::ModelViewer()
#ifdef _LINUX
	// Transparency in interfaceManager crashes with Linux compositing
	: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_VENETIAN_BLINDS_HINT)
#endif
{
  PLUGINMANAGER.init("./plugins");
	// our main class objects
	animControl = NULL;
	canvas = NULL;
	charControl = NULL;
	enchants = NULL;
	lightControl = NULL;
	modelControl = NULL;
	imageControl = NULL;
	settingsControl = NULL;
	modelbankControl = NULL;
	animExporter = NULL;
	fileControl = NULL;

	//wxWidget objects
	menuBar = NULL;
	charMenu = NULL;
	charGlowMenu = NULL;
	viewMenu = NULL;
	optMenu = NULL;
	lightMenu = NULL;
	exportMenu = NULL;
	fileMenu = NULL;
	camMenu = NULL;

	isWoWLoaded = false;
	isModel = false;
	isWMO = false;
	isChar = false;
	isADT = false;
	initDB = false;

	//wxCAPTION|wxRESIZE_BORDER|wxSYSTEM_MENU
	// create our main frame
	if (Create(NULL, wxID_ANY, wxString(GLOBALSETTINGS.appTitle()), wxDefaultPosition, wxSize(1024, 768), wxDEFAULT_FRAME_STYLE|wxCLIP_CHILDREN, wxT("ModelViewerFrame"))) {
	  SetIcon(wxICON(IDI_ICON1));
		SetExtraStyle(wxWS_EX_VALIDATE_RECURSIVELY);
#ifndef	_LINUX // buggy
		SetBackgroundStyle(wxBG_STYLE_CUSTOM);
#endif

		InitObjects();  // create our canvas, anim control, character control, etc

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
		
		LOG_INFO << "Setting OpenGL render state...";
		SetStatusText(wxT("Setting OpenGL render state..."));
		video.InitGL();

		SetStatusText(wxEmptyString);
	} else {
		LOG_FATAL << "Unable to create the main window for the application.";
		Close(true);
	}
}

void ModelViewer::InitMenu()
{
	LOG_INFO << "Initializing File Menu...";

	if (GetStatusBar() == NULL){
	  CreateStatusBar(4);
	  int widths[] = {-1, 100, 50, 125};
	  SetStatusWidths(4, widths);
		SetStatusText(wxT("Initializing File Menu..."));
	}

	// MENU
	fileMenu = new wxMenu;
	fileMenu->Append(ID_LOAD_WOW, _("Load World of Warcraft"));
	if (isWoWLoaded == true)
		fileMenu->Enable(ID_LOAD_WOW,false);
	fileMenu->Append(ID_FILE_VIEWLOG, _("View Log"));
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_SCREENSHOT, _("Save Screenshot\tF12"));
	fileMenu->Append(ID_FILE_SCREENSHOTCONFIG, _("Save Sized Screenshot\tCTRL+S"));

	// deactivate sized screenshot (needs a backbuffer rendering)
	//fileMenu->Enable(ID_FILE_SCREENSHOTCONFIG,false);

	fileMenu->Append(ID_FILE_EXPORTGIF, _("GIF/Sequence Export"));
	fileMenu->Append(ID_FILE_EXPORTAVI, _("Export AVI"));
	fileMenu->AppendSeparator();

	// --== Continue regular menu ==--

	// export menu
	wxMenu *exportMenu = new wxMenu;
	exportMenu->Append(ID_FILE_MODEL_INFO, wxT("Export ModelInfo.xml"));

	PluginManager::iterator it = PLUGINMANAGER.begin();
	int subMenuId = 10000;
	for(; it != PLUGINMANAGER.end() ; ++it, subMenuId++)
	{
	  ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*it);

	  if(plugin)
	  {
	    exportMenu->Append(subMenuId,plugin->menuLabel());
	    Connect( subMenuId,
	      wxEVT_COMMAND_MENU_SELECTED,
	      wxCommandEventHandler(ModelViewer::OnExport) );
	  }
	}
	fileMenu->Append(ID_EXPORT_MODEL, wxT("Export Model"), exportMenu);






	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_RESETLAYOUT, _("Reset Layout"));
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_EXIT, _("E&xit\tCTRL+X"));

	viewMenu = new wxMenu;
	viewMenu->Append(ID_VIEW_NPC, _("View NPC"));
	viewMenu->Append(ID_VIEW_ITEM, _("View Item"));
	viewMenu->AppendSeparator();
	viewMenu->Append(ID_SHOW_FILE_LIST, _("Show file list"));
	viewMenu->Append(ID_SHOW_ANIM, _("Show animaton control"));
	viewMenu->Append(ID_SHOW_CHAR, _("Show character control"));
	viewMenu->Append(ID_SHOW_LIGHT, _("Show light control"));
	viewMenu->Append(ID_SHOW_MODEL, _("Show model control"));
	viewMenu->Append(ID_SHOW_MODELBANK, _("Show model bank"));
	viewMenu->AppendSeparator();
	if (canvas) {
		viewMenu->Append(ID_BG_COLOR, _("Background Color..."));
		viewMenu->AppendCheckItem(ID_BACKGROUND, _("Load Background\tCTRL+L"));
		viewMenu->Check(ID_BACKGROUND, canvas->drawBackground);
		viewMenu->AppendCheckItem(ID_SKYBOX, _("Skybox"));
		viewMenu->Check(ID_SKYBOX, canvas->drawSky);
		viewMenu->AppendCheckItem(ID_SHOW_GRID, _("Show Grid"));
		viewMenu->Check(ID_SHOW_GRID, canvas->drawGrid);

		viewMenu->AppendCheckItem(ID_SHOW_MASK, _("Show Mask"));
		viewMenu->Check(ID_SHOW_MASK, false);

		viewMenu->AppendSeparator();
	}

	try {
		
		// Camera Menu
		wxMenu *camMenu = new wxMenu;
		camMenu->AppendCheckItem(ID_USE_CAMERA, _("Use model camera"));
		camMenu->AppendSeparator();
		camMenu->Append(ID_CAM_FRONT, _("Front"));
		camMenu->Append(ID_CAM_BACK, _("Back"));
		camMenu->Append(ID_CAM_SIDE, _("Side"));
		camMenu->Append(ID_CAM_ISO, _("Perspective"));

		viewMenu->Append(ID_CAMERA, _("Camera"), camMenu);
		viewMenu->AppendSeparator();

		wxMenu *setSize = new wxMenu;
		setSize->Append(ID_CANVASS120, wxT("(1:1) 120 x 120"),_("Square (1:1)"));
		setSize->Append(ID_CANVASS512, wxT("(1:1) 512 x 512"),_("Square (1:1)"));
		setSize->Append(ID_CANVASS1024, wxT("(1:1) 1024 x 1024"),_("Square (1:1)"));
		setSize->Append(ID_CANVASF480, wxT("(4:3) 640 x 480"),_("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF600, wxT("(4:3) 800 x 600"),_("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF768, wxT("(4:3) 1024 x 768"),_("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF864, wxT("(4:3) 1152 x 864"),_("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF1200, wxT("(4:3) 1600 x 1200"),_("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASW480, wxT("(16:9) 864 x 480"),_("Widescreen (16:9)"));
		setSize->Append(ID_CANVASW720, wxT("(16:9) 1280 x 720"),_("Widescreen (16:9)"));
		setSize->Append(ID_CANVASW1080, wxT("(16:9) 1920 x 1080"),_("Widescreen (16:9)"));
		setSize->Append(ID_CANVASM768, wxT("(5:3) 1280 x 768"),_("Misc (5:3)"));
		setSize->Append(ID_CANVASM1200, wxT("(8:5) 1920 x 1200"),_("Misc (8:5)"));

		viewMenu->Append(ID_CANVASSIZE, wxT("Set Canvas Size"), setSize);
		
		//lightMenu->Append(ID_LT_COLOR, wxT("Lighting Color..."));

		lightMenu = new wxMenu;
		lightMenu->Append(ID_LT_SAVE, _("Save Lighting"));
		lightMenu->Append(ID_LT_LOAD, _("Load Lighting"));
		lightMenu->AppendSeparator();
		lightMenu->AppendCheckItem(ID_LT_DIRECTION, _("Render Light Objects"));
		lightMenu->AppendSeparator();
		lightMenu->AppendCheckItem(ID_LT_TRUE, _("Use true lighting"));
		lightMenu->Check(ID_LT_TRUE, false);
		lightMenu->AppendRadioItem(ID_LT_DIRECTIONAL, _("Use dynamic light"));
		lightMenu->Check(ID_LT_DIRECTIONAL, true);
		lightMenu->AppendRadioItem(ID_LT_AMBIENT, _("Use ambient light"));
		lightMenu->AppendRadioItem(ID_LT_MODEL, _("Model lights only"));

		charMenu = new wxMenu;
		charMenu->Append(ID_LOAD_CHAR, _("Load Character\tF8"));
		charMenu->Append(ID_IMPORT_CHAR, _("Import Armory Character"));
		charMenu->Append(ID_SAVE_CHAR, _("Save Character\tF7"));
		charMenu->AppendSeparator();

		charGlowMenu = new wxMenu;
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_NONE, _("None"));
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEFAULT, _("Default"));
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEATHKNIGHT, _("Death Knight"));
		if(charControl->model)
		{
		  if (charControl->model->cd.eyeGlowType){
		    size_t egt = charControl->model->cd.eyeGlowType;
		    if (egt == EGT_NONE)
		      charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
		    else if (egt == EGT_DEATHKNIGHT)
		      charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
		    else
		      charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
		  }else{
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

		wxMenu *effectsMenu = new wxMenu;
		effectsMenu->Append(ID_ENCHANTS, _("Apply Enchants"));

		// Options menu
		optMenu = new wxMenu;
		optMenu->AppendCheckItem(ID_DEFAULT_DOODADS, _("Always show default doodads in WMOs"));
		optMenu->Check(ID_DEFAULT_DOODADS, true);
		optMenu->AppendSeparator();
		optMenu->Append(ID_SHOW_SETTINGS, _("Settings..."));


		wxMenu *aboutMenu = new wxMenu;
		aboutMenu->Append(ID_LANGUAGE, _("Language"));
		aboutMenu->Append(ID_HELP, _("Help"));
		aboutMenu->Enable(ID_HELP, false);
		aboutMenu->Append(ID_ABOUT, _("About"));
		aboutMenu->AppendSeparator();
		aboutMenu->Append(ID_CHECKFORUPDATE, _("Check for Update"));

		menuBar = new wxMenuBar();
		menuBar->Append(fileMenu, _("&File"));
		menuBar->Append(viewMenu, _("&View"));
		menuBar->Append(charMenu, _("&Character"));
		menuBar->Append(lightMenu, _("&Lighting"));
		menuBar->Append(optMenu, _("&Options"));
		menuBar->Append(effectsMenu, _("&Effects"));
		menuBar->Append(aboutMenu, _("&About"));
		SetMenuBar(menuBar);
	} catch(...) {};

	// Disable our "Character" menu, only accessible when a character model is being displayed
	// menuBar->EnableTop(2, false);
	
	// Hotkeys / shortcuts
	wxAcceleratorEntry entries[25];
	int keys = 0;
	entries[keys++].Set(wxACCEL_NORMAL,  WXK_F5,     ID_SAVE_EQUIPMENT);
	entries[keys++].Set(wxACCEL_NORMAL,  WXK_F6,     ID_LOAD_EQUIPMENT);
	entries[keys++].Set(wxACCEL_NORMAL,  WXK_F7,     ID_SAVE_CHAR);
	entries[keys++].Set(wxACCEL_NORMAL,	WXK_F8,     ID_LOAD_CHAR);
	entries[keys++].Set(wxACCEL_CTRL,	(int)'b',	ID_SHOW_BOUNDS);
	entries[keys++].Set(wxACCEL_CTRL,	(int)'X',	ID_FILE_EXIT);
	entries[keys++].Set(wxACCEL_NORMAL,	WXK_F12,	ID_FILE_SCREENSHOT);
	entries[keys++].Set(wxACCEL_CTRL,	(int)'e',	ID_SHOW_EARS);
	entries[keys++].Set(wxACCEL_CTRL,	(int)'h',	ID_SHOW_HAIR);
	entries[keys++].Set(wxACCEL_CTRL, (int)'f',	ID_SHOW_FACIALHAIR);
	entries[keys++].Set(wxACCEL_CTRL, (int)'z',	ID_SHEATHE);
	entries[keys++].Set(wxACCEL_CTRL, (int)'l',	ID_BACKGROUND);
	entries[keys++].Set(wxACCEL_CTRL, (int)'+',		ID_ZOOM_IN);
	entries[keys++].Set(wxACCEL_CTRL, (int)'-',		ID_ZOOM_OUT);
	entries[keys++].Set(wxACCEL_CTRL, (int)'s',		ID_FILE_SCREENSHOTCONFIG);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F9,		ID_CLEAR_EQUIPMENT);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F10,	ID_CHAR_RANDOMISE);

	// Temporary saves
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F1,		ID_SAVE_TEMP1);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F2,		ID_SAVE_TEMP2);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F3,		ID_SAVE_TEMP3);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F4,		ID_SAVE_TEMP4);

	// Temp loads
	entries[keys++].Set(wxACCEL_CTRL,	WXK_F1,		ID_LOAD_TEMP1);
	entries[keys++].Set(wxACCEL_CTRL,	WXK_F2,		ID_LOAD_TEMP2);
	entries[keys++].Set(wxACCEL_CTRL,	WXK_F3,		ID_LOAD_TEMP3);
	entries[keys++].Set(wxACCEL_CTRL,	WXK_F4,		ID_LOAD_TEMP4);

	wxAcceleratorTable accel(keys, entries);
	this->SetAcceleratorTable(accel);
}

void ModelViewer::InitObjects()
{
	LOG_INFO << "Initializing Objects...";

	fileControl = new FileControl(this, ID_FILELIST_FRAME);

	animControl = new AnimControl(this, ID_ANIM_FRAME);
	charControl = new CharControl(this, ID_CHAR_FRAME);
	lightControl = new LightControl(this, ID_LIGHT_FRAME);
	modelControl = new ModelControl(this, ID_MODEL_FRAME);
	settingsControl = new SettingsControl(this, ID_SETTINGS_FRAME);
	settingsControl->Show(false);
	modelbankControl = new ModelBankControl(this, ID_MODELBANK_FRAME);

	canvas = new ModelCanvas(this);

	if (video.secondPass) {
		canvas->Destroy();
		video.Release();
		canvas = new ModelCanvas(this);
	}

	g_modelViewer = this;
	g_animControl = animControl;
	g_charControl = charControl;
	g_canvas = canvas;

	modelControl->animControl = animControl;
	
	enchants = new EnchantsDialog(this, charControl);

	animExporter = new CAnimationExporter(this, wxID_ANY, wxT("Animation Exporter"), wxDefaultPosition, wxSize(350, 220), wxCAPTION|wxSTAY_ON_TOP|wxFRAME_NO_TASKBAR);
}

void ModelViewer::InitDatabase()
{
  LOG_INFO << "Initializing Databases...";
  SetStatusText(wxT("Initializing Databases..."));
  wxBusyCursor busyCursor;
  wxWindowDisabler disableAll;
  wxBusyInfo info(_T("Please wait during game database analysis..."), this);

  if(!GAMEDATABASE.initFromXML("wow6.xml"))
  {
    initDB = false;
    LOG_ERROR << "Initializing failed !";
    return;
  }
  else
  {
    LOG_INFO << "Initializing succeeded.";
  }

	LOG_INFO << "Initializing Databases...";
	SetStatusText(wxT("Initializing Databases..."));
	initDB = true;

	{
	  sqlResult npc = GAMEDATABASE.sqlQuery("SELECT ID, DisplayID, CreatureTypeID, Name From Creature;");

	  if(npc.valid && !npc.empty())
	  {
	    LOG_INFO << "Found" << npc.values.size() << "NPCs";
	    for(int i=0, imax=npc.values.size() ; i < imax ; i++)
	    {
	      NPCRecord rec(npc.values[i]);
	      if(rec.model != 0)
	        npcs.push_back(rec);
	    }
	  }
	  else
	  {
	    initDB = false;
	    LOG_ERROR << "Error during NPC detection from database.";
	    return;
	  }
	}

	{
	  sqlResult item = GAMEDATABASE.sqlQuery("SELECT Item.ID, ItemSparse.Name, Item.Type, Item.Class, Item.SubClass, Item.Sheath, ItemSparse.Quality FROM Item LEFT JOIN ItemSparse ON Item.ID = ItemSparse.ID WHERE Item.Type !=0 AND ItemSparse.Name != \"\"");

	  if(item.valid && !item.empty())
	  {
	    LOG_INFO << "Found" << item.values.size() << "items";
	    for(int i=0, imax=item.values.size() ; i < imax ; i++)
	    {
	      ItemRecord rec(item.values[i]);
	      items.items.push_back(rec);
	    }
	  }
	  else
	  {
	    initDB = false;
	    LOG_ERROR << "Error during Item detection from database.";
	    return;
	  }
	}

	// init texture regions
	CharTexture::initRegions();

	// init Race informations
	RaceInfos::init();

  LOG_INFO << "Finished initiating database files.";
  SetStatusText(wxT("Finished initiating database files."));;
}

void ModelViewer::InitDocking()
{
	LOG_INFO << "Initializing GUI Docking.";
	
	// wxAUI stuff
	//interfaceManager.SetFrame(this); 
	interfaceManager.SetManagedWindow(this);

	// OpenGL Canvas
	interfaceManager.AddPane(canvas, wxAuiPaneInfo().
				Name(wxT("canvas")).Caption(wxT("OpenGL Canvas")).
				CenterPane());
	
	// Tree list control
	interfaceManager.AddPane(fileControl, wxAuiPaneInfo().
				Name(wxT("fileControl")).Caption(wxT("File List")).
				BestSize(wxSize(170,700)).Left().Layer(2));

	// Animation frame
    interfaceManager.AddPane(animControl, wxAuiPaneInfo().
				Name(wxT("animControl")).Caption(wxT("Animation")).
				Bottom().Layer(1));

	// Character frame
	interfaceManager.AddPane(charControl, wxAuiPaneInfo().
                Name(wxT("charControl")).Caption(wxT("Character")).
                BestSize(wxSize(170,700)).Right().Layer(2).Show(isChar));

	// Lighting control
	interfaceManager.AddPane(lightControl, wxAuiPaneInfo().
		Name(wxT("Lighting")).Caption(wxT("Lighting")).
		FloatingSize(wxSize(170,430)).Float().Fixed().Show(false).
		DestroyOnClose(false)); //.FloatingPosition(GetStartPosition())

	// model control
	interfaceManager.AddPane(modelControl, wxAuiPaneInfo().
		Name(wxT("Models")).Caption(wxT("Models")).
		FloatingSize(wxSize(160,460)).Float().Show(false).
		DestroyOnClose(false));

	// model bank control
	interfaceManager.AddPane(modelbankControl, wxAuiPaneInfo().
		Name(wxT("ModelBank")).Caption(wxT("ModelBank")).
		FloatingSize(wxSize(300,320)).Float().Fixed().Show(false).
		DestroyOnClose(false));

	// settings frame
	interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
		Name(wxT("Settings")).Caption(wxT("Settings")).
		FloatingSize(wxSize(400,440)).Float().TopDockable(false).LeftDockable(false).
		RightDockable(false).BottomDockable(false).Fixed().Show(false));

    // tell the manager to "commit" all the changes just made
    //interfaceManager.Update();
}

void ModelViewer::ResetLayout()
{
	interfaceManager.DetachPane(fileControl);
	interfaceManager.DetachPane(animControl);
	interfaceManager.DetachPane(charControl);
	interfaceManager.DetachPane(lightControl);
	interfaceManager.DetachPane(modelControl);
	interfaceManager.DetachPane(settingsControl);
	interfaceManager.DetachPane(canvas);
	
	// OpenGL Canvas
	interfaceManager.AddPane(canvas, wxAuiPaneInfo().
				Name(wxT("canvas")).Caption(wxT("OpenGL Canvas")).
				CenterPane());
	
	// Tree list control
	interfaceManager.AddPane(fileControl, wxAuiPaneInfo().
				Name(wxT("fileControl")).Caption(wxT("File List")).
				BestSize(wxSize(170,700)).Left().Layer(2));

	// Animation frame
    interfaceManager.AddPane(animControl, wxAuiPaneInfo().
				Name(wxT("animControl")).Caption(wxT("Animation")).
				Bottom().Layer(1));

	// Character frame
	interfaceManager.AddPane(charControl, wxAuiPaneInfo().
                Name(wxT("charControl")).Caption(wxT("Character")).
                BestSize(wxSize(170,700)).Right().Layer(2).Show(isChar));

	interfaceManager.AddPane(lightControl, wxAuiPaneInfo().
		Name(wxT("Lighting")).Caption(wxT("Lighting")).
		FloatingSize(wxSize(170,430)).Float().Fixed().Show(false).
		DestroyOnClose(false)); //.FloatingPosition(GetStartPosition())

	interfaceManager.AddPane(modelControl, wxAuiPaneInfo().
		Name(wxT("Models")).Caption(wxT("Models")).
		FloatingSize(wxSize(160,460)).Float().Show(false).
		DestroyOnClose(false));

	interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
		Name(wxT("Settings")).Caption(wxT("Settings")).
		FloatingSize(wxSize(400,440)).Float().TopDockable(false).LeftDockable(false).
		RightDockable(false).BottomDockable(false).Show(false));

    // tell the manager to "commit" all the changes just made
    interfaceManager.Update();
}


void ModelViewer::LoadSession()
{
	LOG_INFO << "Loading Session settings from:" << cfgPath.c_str();

	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	// Application Config Settings
	useRandomLooks = config.value("Session/RandomLooks", true).toBool();
	GLOBALSETTINGS.bShowParticle = config.value("Session/ShowParticle", true).toBool();
	GLOBALSETTINGS.bZeroParticle = config.value("Session/ZeroParticle", true).toBool();
	GLOBALSETTINGS.bInitPoseOnlyExport = config.value("Session/InitPoseOnlyExport", false).toBool();

	// Background and Custom Colours
	wxString colStr;
	wxColour bgCol;
	colStr = config.value("Session/bgCol",  "#475F79").toString().toStdString(); // #475F79 = (71, 95, 121)
	if (!bgCol.Set(colStr))
 		bgCol = wxColour(71, 95, 121);
	bgDialogData.SetColour(bgCol);
	for (int i = 0; i < 16; i++)
	{
		wxColour custCol;
		colStr = config.value(QString("Session/bgCustCol%1").arg(i), wxEmptyString).toString().toStdString();
		if ((colStr != wxEmptyString) && custCol.Set(colStr))
			bgDialogData.SetCustomColour(i, custCol);
	}
	// Other session settings
	if (canvas)
	{
		// Set canvas background Colour
		canvas->vecBGColor.x = bgCol.Red()/255.0f;
		canvas->vecBGColor.y = bgCol.Green()/255.0f;
		canvas->vecBGColor.z = bgCol.Blue()/255.0f;

		// boolean vars
		canvas->drawBackground = config.value("Session/DBackground",  false).toBool();
		bgImagePath = config.value("Session/BackgroundImage",  false).toString().toStdString();

		if (!bgImagePath.IsEmpty())
			canvas->LoadBackground(bgImagePath);
	}
}

void ModelViewer::SaveSession()
{
	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	config.setValue("Graphics/FSAA", video.curCap.aaSamples);
	config.setValue("Graphics/AccumulationBuffer", video.curCap.accum);
	config.setValue("Graphics/AlphaBits", video.curCap.alpha);
	config.setValue("Graphics/ColourBits", video.curCap.colour);
	config.setValue("Graphics/DoubleBuffer", video.curCap.doubleBuffer);
	config.setValue("Graphics/HWAcceleration", video.curCap.hwAcc);
	config.setValue("Graphics/SampleBuffer", video.curCap.sampleBuffer);
	config.setValue("Graphics/StencilBuffer", video.curCap.stencil);
	config.setValue("Graphics/ZBuffer", video.curCap.zBuffer);
	config.setValue("Graphics/UseEnvMapping", video.useEnvMapping);
	config.setValue("Graphics/Fov", (double)video.fov);

	config.setValue("Session/RandomLooks", useRandomLooks);
	config.setValue("Session/ShowParticle", GLOBALSETTINGS.bShowParticle);
	config.setValue("Session/ZeroParticle", GLOBALSETTINGS.bZeroParticle);
	config.setValue("Session/InitPoseOnlyExport", GLOBALSETTINGS.bInitPoseOnlyExport);

	// Background and Custom Colours
	wxColour bgCol;
	bgCol = bgDialogData.GetColour();
	config.setValue("Session/bgCol", bgCol.GetAsString(wxC2S_HTML_SYNTAX).mb_str());
	for (int i = 0; i < 16; i++)
	{
		bgCol = bgDialogData.GetCustomColour(i);
		if (!bgCol.IsOk())  // skip undefined custom colours
			continue;
		config.setValue(QString("Session/bgCustCol%1").arg(i), bgCol.GetAsString(wxC2S_HTML_SYNTAX).mb_str());
	}


	if (canvas)
	{
		int canvx = 0, canvy = 0;
		canvas->GetClientSize(&canvx, &canvy);
		config.setValue("Session/CanvasWidth", canvx);
		config.setValue("Session/CanvasHeight", canvy);

		config.setValue("Session/DBackground", canvas->drawBackground);

		if (canvas->drawBackground)
			config.setValue("Session/BackgroundImage", bgImagePath.c_str());
		else
			config.setValue("Session/BackgroundImage", "");

		// model file
		if (canvas->model)
			config.setValue("Session/Model", canvas->model->name());
	}
}

void ModelViewer::LoadLayout()
{
	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	wxString layout = config.value("Session/Layout", "").toString().toStdString();

	// if the layout data exists,  load it.
	if (!layout.IsNull() // something goes wrong
			&& !layout.IsEmpty() // empty value
			&& !layout.EndsWith("canvas")) // old saving badly read by Qt, ignore
	{
		if (!interfaceManager.LoadPerspective(layout, false))
		{
			LOG_ERROR << "Could not load the layout.";
		}
		else
		{
			// No need to display these windows on startup
			interfaceManager.GetPane(modelControl).Show(false);
			interfaceManager.GetPane(settingsControl).Show(false);

			// If character panel is showing,  hide it
			interfaceManager.GetPane(charControl).Show(isChar);
#ifndef	_LINUX // buggy
			interfaceManager.Update();
#endif
			LOG_INFO << "GUI Layout loaded from previous session.";
		}
	}

	// Restore saved canvas size:
	if (canvas)
	{
		int canvx = config.value("Session/CanvasWidth", 800).toInt();
		int canvy = config.value("Session/CanvasHeight", 600).toInt();
		SetCanvasSize(canvx, canvy);
	}
}

void ModelViewer::SaveLayout()
{
	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	config.setValue("Session/Layout", interfaceManager.SavePerspective().c_str());
	LOG_INFO << "GUI Layout was saved.";
}


void ModelViewer::LoadModel(GameFile * file)
{
	if (!canvas || !file)
		return;

	isModel = true;

	// check if this is a character model
	isChar = (file->fullname().startsWith("char", Qt::CaseInsensitive) || file->fullname().startsWith("alternate\\char", Qt::CaseInsensitive));
	Attachment *modelAtt = NULL;

	if (isChar)
	{
		modelAtt = canvas->LoadCharModel(file);
		// error check
		if (!modelAtt)
		{
			LOG_ERROR << "Failed to load the model" << file->fullname();
			return;
		}

		// add children to manage items equipped
		canvas->model->addChild(new WoWItem(CS_SHIRT));
		canvas->model->addChild(new WoWItem(CS_HEAD));
		canvas->model->addChild(new WoWItem(CS_SHOULDER));
		canvas->model->addChild(new WoWItem(CS_PANTS));
		canvas->model->addChild(new WoWItem(CS_BOOTS));
		canvas->model->addChild(new WoWItem(CS_CHEST));
		canvas->model->addChild(new WoWItem(CS_TABARD));
		canvas->model->addChild(new WoWItem(CS_BELT));
		canvas->model->addChild(new WoWItem(CS_BRACERS));
		canvas->model->addChild(new WoWItem(CS_GLOVES));
		canvas->model->addChild(new WoWItem(CS_HAND_RIGHT));
		canvas->model->addChild(new WoWItem(CS_HAND_LEFT));
		canvas->model->addChild(new WoWItem(CS_CAPE));
		canvas->model->addChild(new WoWItem(CS_QUIVER));
		canvas->model->modelType = MT_CHAR;
		canvas->model->cd.reset(canvas->model);
	}
	else
	{
		modelAtt = canvas->LoadCharModel(file); //  change it from LoadModel, don't sure it's right or not.

		// error check
		if (!modelAtt)
		{
			LOG_ERROR << "Failed to load the model" << file->fullname();
			return;
		}
		// creature model, keep left/right hand only as equipment
		canvas->model->addChild(new WoWItem(CS_HAND_RIGHT));
		canvas->model->addChild(new WoWItem(CS_HAND_LEFT));
		canvas->model->modelType = MT_NORMAL;
	}

	// Error check,  make sure the model was actually loaded and set to canvas->model
	if (!canvas->model)
	{
		LOG_ERROR << "[ModelViewer::LoadModel()]  Model* Canvas::model is null!";
		return;
	}

	SetStatusText(canvas->model->name().toStdString());
	canvas->model->charModelDetails.isChar = isChar;
	
	viewMenu->Enable(ID_USE_CAMERA, canvas->model->hasCamera);
	if (canvas->useCamera && !canvas->model->hasCamera)
	{
		canvas->useCamera = false;
		viewMenu->Check(ID_USE_CAMERA, false);
	}
	
	// wxAUI
	interfaceManager.GetPane(charControl).Show(isChar);
	interfaceManager.GetPane(charControl).Show(isChar);
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
	}

	// Update the model control
	modelControl->UpdateModel(modelAtt);
	modelControl->RefreshModel(canvas->root);

	// Update the animations / skins
	animControl->UpdateModel(canvas->model);
	interfaceManager.Update();
}

// Load an NPC model
void ModelViewer::LoadNPC(unsigned int modelid)
{
	canvas->clearAttachments();
	//if (!isChar) // may memory leak
	//	wxDELETE(canvas->model);
	canvas->model = NULL;
	
	isModel = true;
	isChar = false;
	isWMO = false;

	QString query = QString("SELECT FileData.path, FileData.name, CreatureDisplayInfo.Texture1, "
	    "CreatureDisplayInfo.Texture2, CreatureDisplayInfo.Texture3, CreatureDisplayInfo.ExtendedDisplayInfoID FROM Creature "
	    "LEFT JOIN CreatureDisplayInfo ON Creature.DisplayID = CreatureDisplayInfo.ID "
	    "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
	    "LEFT JOIN FileData ON CreatureModelData.FileDataID = FileData.ID WHERE Creature.ID = %1;").arg(modelid);

	sqlResult r = GAMEDATABASE.sqlQuery(query);

	if(r.valid && !r.empty())
	{
	  // if npc is a simple one (no extra info CreatureDisplayInfoExtra)
	  if(r.values[0][5].toInt() == 0)
	  {
	    std::string modelname = r.values[0][0].toStdString() + r.values[0][1].toStdString();
	    wxString name(modelname.c_str());
	    LoadModel(GAMEDIRECTORY.getFile(name.c_str()));
	    canvas->model->modelType = MT_NORMAL;

	    TextureGroup grp;
	    int count = 0;
	    for(int i=0; i < 3; i++)
	    {
	      grp.tex[i] = GAMEDIRECTORY.getFile(r.values[0][0] + r.values[0][i+2] + ".blp");
	      if(grp.tex[0])
	        count++;
	    }
	    grp.base = TEXTURE_GAMEOBJECT1;
	    grp.count = count;
	    if (grp.tex[0])
	      animControl->AddSkin(grp);
	  }
	  else
	  {
	    QString modelname = r.values[0][0] + r.values[0][1];
	    QString modelnamehd = modelname;
	    modelnamehd.replace(".m2","_hd.m2");

	    GameFile * model = GAMEDIRECTORY.getFile(modelnamehd);
	    if(!model)
	      model = GAMEDIRECTORY.getFile(modelname);

	    LoadModel(model);

	    query = QString("SELECT * FROM CreatureDisplayInfoExtra WHERE ID = %1").arg(r.values[0][5]);

	    r = GAMEDATABASE.sqlQuery(query);

	    if(r.valid && !r.empty())
	    {
	      g_charControl->model->cd.setSkinColor(r.values[0][3].toInt());
	      g_charControl->model->cd.setFaceType(r.values[0][4].toInt());
	      g_charControl->model->cd.setHairColor(r.values[0][6].toInt());
	      g_charControl->model->cd.setHairStyle(r.values[0][5].toInt());
	      g_charControl->model->cd.setFacialHair(r.values[0][7].toInt());

	      WoWItem * item = g_charControl->model->getItem(CS_HEAD);
	      if(item)
	        item->setDisplayId(r.values[0][8].toInt());

	      item = g_charControl->model->getItem(CS_SHOULDER);
	      if(item)
	        item->setDisplayId(r.values[0][9].toInt());

	      item = g_charControl->model->getItem(CS_SHIRT);
	      if(item)
	        item->setDisplayId(r.values[0][10].toInt());

	      item = g_charControl->model->getItem(CS_CHEST);
	      if(item)
	        item->setDisplayId(r.values[0][11].toInt());

	      item = g_charControl->model->getItem(CS_BELT);
	      if(item)
	        item->setDisplayId(r.values[0][12].toInt());

	      item = g_charControl->model->getItem(CS_PANTS);
	      if(item)
	        item->setDisplayId(r.values[0][13].toInt());

	      item = g_charControl->model->getItem(CS_BOOTS);
	      if(item)
	        item->setDisplayId(r.values[0][14].toInt());

	      item = g_charControl->model->getItem(CS_BRACERS);
	      if(item)
	        item->setDisplayId(r.values[0][15].toInt());

	      item = g_charControl->model->getItem(CS_GLOVES);
	      if(item)
	        item->setDisplayId(r.values[0][16].toInt());

	      item = g_charControl->model->getItem(CS_TABARD);
	      if(item)
	        item->setDisplayId(r.values[0][17].toInt());

	      item = g_charControl->model->getItem(CS_CAPE);
	      if(item)
	        item->setDisplayId(r.values[0][18].toInt());



	      g_charControl->model->cd.isNPC = true;

	      g_charControl->RefreshModel();
	      g_charControl->RefreshEquipment();
	    }

	  }
	}

	fileControl->UpdateInterface();

	// wxAUI
	// hide charControl if current model is not a Character one.
	if(!g_charControl->model->charModelDetails.isChar)
	  interfaceManager.GetPane(charControl).Show(false);

	interfaceManager.Update();
}

void ModelViewer::LoadItem(unsigned int id)
{
	canvas->clearAttachments();
	//if (!isChar) // may memory leak
	//	wxDELETE(canvas->model);
	canvas->model = NULL;
	
	isModel = true;
	isChar = false;
	isWMO = false;

	try {
	  QString query = QString("SELECT Model1, Path, Name FROM ItemDisplayInfo"
	      " LEFT JOIN TextureFileData ON ItemDisplayInfo.TextureItemID1 = TextureFileData.TextureItemID"
	      " LEFT JOIN FileData ON TextureFileData.FileDataID = FileData.ID"
	      " WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ItemAppearance.ID ="
	      " (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1))").arg(id);

	  sqlResult itemInfos = GAMEDATABASE.sqlQuery(query);
	  // LOG_INFO << query;

	  if(itemInfos.valid && !itemInfos.empty())
	  {
	    QString model1 = itemInfos.values[0][0];
	    std::string texture1 = itemInfos.values[0][1].toStdString();

	    model1 = GAMEDIRECTORY.getFullPathForFile(model1);
	    if(model1 == "") // try with .m2 at the end
	    {
	      model1 = itemInfos.values[0][0];
	      model1.replace(".mdx", ".m2", Qt::CaseInsensitive);
	      model1 = GAMEDIRECTORY.getFullPathForFile(model1);
	    }

	    texture1 = itemInfos.values[0][1].toStdString() + itemInfos.values[0][2].toStdString();

	    if(model1 != "" && texture1 != "")
	    {
	      LoadModel(GAMEDIRECTORY.getFile(model1));
	      canvas->model->TextureList.push_back(texture1.c_str());
	      TextureGroup grp;
	      grp.base = TEXTURE_ITEM;
	      grp.count = 1;

	      grp.tex[0] = GAMEDIRECTORY.getFile(texture1.c_str());
	      if (grp.tex[0])
	        animControl->AddSkin(grp);
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

	} catch (...) {}

	// wxAUI
	interfaceManager.GetPane(charControl).Show(isChar);
	interfaceManager.Update();
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

// This is called when the window is closing
void ModelViewer::OnClose(wxCloseEvent &event)
{
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
	LOG_INFO << "Shutting down the program...";

	video.render = false;

	// If we have a canvas (which we always should)
	// Stop rendering, give more power back to the CPU to close this sucker down!
	//if (canvas)
	//	canvas->timer.Stop();

	// Save current layout
	SaveLayout();

	// wxAUI stuff
	interfaceManager.UnInit();

	// Save our session and layout info
	SaveSession();

	if (animExporter) {
		animExporter->Destroy();
		wxDELETE(animExporter);
	}

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

	if (enchants) {
		enchants->Destroy();
		enchants = NULL;
	}
}

// Menu button press events
void ModelViewer::OnToggleDock(wxCommandEvent &event)
{
	int id = event.GetId();

	// wxAUI Stuff
	if (id==ID_SHOW_FILE_LIST) {
		interfaceManager.GetPane(fileControl).Show(true);
	} else if (id==ID_SHOW_ANIM) {
		interfaceManager.GetPane(animControl).Show(true);
	} else if (id==ID_SHOW_CHAR) {
		interfaceManager.GetPane(charControl).Show(true);
	} else if (id==ID_SHOW_LIGHT) {
		interfaceManager.GetPane(lightControl).Show(true);
	} else if (id==ID_SHOW_MODEL) {
		interfaceManager.GetPane(modelControl).Show(true);
	} else if (id==ID_SHOW_SETTINGS) {
		interfaceManager.GetPane(settingsControl).Show(true);
		settingsControl->Open();
	} else if (id==ID_SHOW_MODELBANK) {
		interfaceManager.GetPane(modelbankControl).Show(true);
	}
	interfaceManager.Update();
}

// Menu button press events
void ModelViewer::OnToggleCommand(wxCommandEvent &event)
{
	int id = event.GetId();

	//switch 
	switch(id) {
	case ID_FILE_RESETLAYOUT:
		ResetLayout();
		break;

	case ID_SHOW_MASK:
		video.useMasking = !video.useMasking;
		break;

	case ID_SHOW_BOUNDS:
		if (canvas->model)
			canvas->model->showBounds = !canvas->model->showBounds;
		break;

	case ID_SHOW_GRID:
		canvas->drawGrid = event.IsChecked();
		break;

	case ID_USE_CAMERA:
		canvas->useCamera = event.IsChecked();
		break;

	case ID_DEFAULT_DOODADS:
		// if we have a model...
		if (canvas->wmo) {
			canvas->wmo->includeDefaultDoodads = event.IsChecked();
			canvas->wmo->updateModels();
		}
		animControl->defaultDoodads = event.IsChecked();
		break;

	case ID_SAVE_CHAR:
		{
			wxFileDialog saveDialog(this, wxT("Save character"), wxEmptyString, wxEmptyString, wxT("Character files (*.chr)|*.chr"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
			if (saveDialog.ShowModal()==wxID_OK)
				SaveChar(saveDialog.GetPath());
		}
		break;
	case ID_LOAD_CHAR:
		{
			wxFileDialog loadDialog(this, wxT("Load character"), wxEmptyString, wxEmptyString, wxT("Character files (*.chr)|*.chr"), wxFD_OPEN|wxFD_FILE_MUST_EXIST);
			if (loadDialog.ShowModal()==wxID_OK)
			{
				LOG_INFO << "Loading character from a save file:" << loadDialog.GetPath().c_str();
				if(charControl->model) // if a model is already present, unload equipment
				{
				  for (size_t i=0; i<NUM_CHAR_SLOTS; i++)
				  {
				    WoWItem * item = charControl->model->getItem((CharSlots)i);
				    if(item)
				      item->setId(0);
				  }
				}
				LoadChar(loadDialog.GetPath());
			}
		}
		fileControl->UpdateInterface();
		break;

	case ID_SAVE_EQUIPMENT:
	  {
	    wxFileDialog dialog(this, wxT("Save equipment"), wxEmptyString, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
	    if (dialog.ShowModal()==wxID_OK)
	      SaveChar(dialog.GetPath().c_str(), true);
	    break;
	  }

	case ID_LOAD_EQUIPMENT:
	  {
	    wxFileDialog loadDialog(this, wxT("Load equipment"), wxEmptyString, wxEmptyString, wxT("Equipment files (*.eq)|*.eq"), wxFD_OPEN|wxFD_FILE_MUST_EXIST);
	    if (loadDialog.ShowModal()==wxID_OK)
	    {
	      LOG_INFO << "Loading equipment from a save file:" << loadDialog.GetPath().c_str();
	      if(charControl->model) // if a model is already present, unload equipment
	      {
	        for (size_t i=0; i<NUM_CHAR_SLOTS; i++)
	        {
	          WoWItem * item = charControl->model->getItem((CharSlots)i);
	          if(item)
	            item->setId(0);
	        }
	      }
	      LoadChar(loadDialog.GetPath(), true);
	    }
	    break;
	  }

	case ID_IMPORT_CHAR:
		{
			wxTextEntryDialog dialog(this, wxT("Please paste in the URL to the character you wish to import."), wxT("Please enter text"), armoryPath, wxOK | wxCANCEL | wxCENTRE, wxDefaultPosition);
			if (dialog.ShowModal() == wxID_OK){
				armoryPath = dialog.GetValue();
				LOG_INFO << "Importing character from the Armory:" << armoryPath.c_str();
				ImportArmoury(armoryPath);
			}
		}
		break;

	case ID_ZOOM_IN:
		canvas->Zoom(0.5f, false);
		break;

	case ID_ZOOM_OUT:
		canvas->Zoom(-0.5f, false);
		break;

	case ID_SAVE_TEMP1:
		canvas->SaveSceneState(1);
		break;
	case ID_SAVE_TEMP2:
		canvas->SaveSceneState(2);
		break;
	case ID_SAVE_TEMP3:
		canvas->SaveSceneState(3);
		break;
	case ID_SAVE_TEMP4:
		canvas->SaveSceneState(4);
		break;
	case ID_LOAD_TEMP1:
		canvas->LoadSceneState(1);
		break;
	case ID_LOAD_TEMP2:
		canvas->LoadSceneState(2);
		break;
	case ID_LOAD_TEMP3:
		canvas->LoadSceneState(3);
		break;
	case ID_LOAD_TEMP4:
		canvas->LoadSceneState(4);
		break;
	}
}

void ModelViewer::OnLightMenu(wxCommandEvent &event)
{
	int id = event.GetId();

	switch (id) {
		case ID_LT_SAVE:
		{
			wxFileDialog dialog(this, wxT("Save Lighting"), wxEmptyString, wxEmptyString, wxT("Scene Lighting (*.lit)|*.lit"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
			if (dialog.ShowModal()==wxID_OK) {
				wxString fn = dialog.GetPath();

				// FIXME: ofstream is not compitable with multibyte path name
				ofstream f(fn.fn_str(), ios_base::out|ios_base::trunc);

				f << lightMenu->IsChecked(ID_LT_DIRECTION) << " " << lightMenu->IsChecked(ID_LT_TRUE) << " " << lightMenu->IsChecked(ID_LT_DIRECTIONAL) << " " << lightMenu->IsChecked(ID_LT_AMBIENT) << " " << lightMenu->IsChecked(ID_LT_MODEL) << endl;
				for (size_t i=0; i<MAX_LIGHTS; i++) {
					f << lightControl->lights[i].ambience.x << " " << lightControl->lights[i].ambience.y << " " << lightControl->lights[i].ambience.z << " " << lightControl->lights[i].arc << " " << lightControl->lights[i].constant_int << " " << lightControl->lights[i].diffuse.x << " " << lightControl->lights[i].diffuse.y << " " << lightControl->lights[i].diffuse.z << " " << lightControl->lights[i].enabled << " " << lightControl->lights[i].linear_int << " " << lightControl->lights[i].pos.x << " " << lightControl->lights[i].pos.y << " " << lightControl->lights[i].pos.z << " " << lightControl->lights[i].quadradic_int << " " << lightControl->lights[i].relative << " " << lightControl->lights[i].specular.x << " " << lightControl->lights[i].specular.y << " " << lightControl->lights[i].specular.z << " " << lightControl->lights[i].target.x << " " << lightControl->lights[i].target.y << " " << lightControl->lights[i].target.z << " " << lightControl->lights[i].type << endl;
				}
				f.close();
			}

			return;

		} 
		case ID_LT_LOAD: 
		{
			wxFileDialog dialog(this, wxT("Load Lighting"), wxEmptyString, wxEmptyString, wxT("Scene Lighting (*.lit)|*.lit"), wxFD_OPEN|wxFD_FILE_MUST_EXIST);
			
			if (dialog.ShowModal()==wxID_OK) {
				wxString fn = dialog.GetFilename();
				// FIXME: ifstream is not compitable with multibyte path name
				ifstream f(fn.fn_str());
				
				bool lightObj, lightTrue, lightDir, lightAmb, lightModel;

				//lightMenu->IsChecked(ID_LT_AMBIENT)
				f >> lightObj >> lightTrue >> lightDir >> lightAmb >> lightModel;

				lightMenu->Check(ID_LT_DIRECTION, lightObj);
				lightMenu->Check(ID_LT_TRUE, lightTrue);
				lightMenu->Check(ID_LT_DIRECTIONAL, lightDir);
				lightMenu->Check(ID_LT_AMBIENT, lightAmb);
				lightMenu->Check(ID_LT_MODEL, lightModel);

				for (size_t i=0; i<MAX_LIGHTS; i++) {
					f >> lightControl->lights[i].ambience.x >> lightControl->lights[i].ambience.y >> lightControl->lights[i].ambience.z >> lightControl->lights[i].arc >> lightControl->lights[i].constant_int >> lightControl->lights[i].diffuse.x >> lightControl->lights[i].diffuse.y >> lightControl->lights[i].diffuse.z >> lightControl->lights[i].enabled >> lightControl->lights[i].linear_int >> lightControl->lights[i].pos.x >> lightControl->lights[i].pos.y >> lightControl->lights[i].pos.z >> lightControl->lights[i].quadradic_int >> lightControl->lights[i].relative >> lightControl->lights[i].specular.x >> lightControl->lights[i].specular.y >> lightControl->lights[i].specular.z >> lightControl->lights[i].target.x >> lightControl->lights[i].target.y >> lightControl->lights[i].target.z >> lightControl->lights[i].type;
				}
				f.close();

				if (lightObj) 
					canvas->drawLightDir = true;

				if (lightDir) {
					canvas->lightType = LIGHT_DYNAMIC; //LT_DIRECTIONAL;
					
					/*
					if (lightTrue) {
						if (event.IsChecked()){
							// Need to reset all our colour, lighting, material back to 'default'
							//GLfloat b[] = {0.5f, 0.4f, 0.4f, 1.0f};
							//glColor4fv(b);
							glDisable(GL_COLOR_MATERIAL);

							glMaterialfv(GL_FRONT, GL_EMISSION, def_emission);
							
							glMaterialfv(GL_FRONT, GL_AMBIENT, def_ambience);
							//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);
							
							glMaterialfv(GL_FRONT, GL_DIFFUSE, def_diffuse);
							glMaterialfv(GL_FRONT, GL_SPECULAR, def_specular);
						} else {
							glEnable(GL_COLOR_MATERIAL);
						}
					}
					*/
				} else if (lightAmb) {
					//glEnable(GL_COLOR_MATERIAL);
					canvas->lightType = LIGHT_AMBIENT;
				} else if (lightModel) {
					canvas->lightType = LIGHT_MODEL_ONLY;
				}

				lightControl->UpdateGL();
				lightControl->Update();
			}
			
			return;
		}
		/* case ID_USE_LIGHTS:
			canvas->useLights = event.IsChecked();
			return;
		*/
		case ID_LT_DIRECTION:
			canvas->drawLightDir = event.IsChecked();
			return;
		case ID_LT_TRUE:
			if (event.IsChecked()){
				// Need to reset all our colour, lighting, material back to 'default'
				//GLfloat b[] = {0.5f, 0.4f, 0.4f, 1.0f};
				//glColor4fv(b);
				glDisable(GL_COLOR_MATERIAL);

				glMaterialfv(GL_FRONT, GL_EMISSION, def_emission);
				glMaterialfv(GL_FRONT, GL_AMBIENT, def_ambience);
				//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);
				
				glMaterialfv(GL_FRONT, GL_DIFFUSE, def_diffuse);
				glMaterialfv(GL_FRONT, GL_SPECULAR, def_specular);				
			} else {
				glEnable(GL_COLOR_MATERIAL);
				//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, Vec4D(0.4f,0.4f,0.4f,1.0f));
			}
			
			lightControl->Update();

			return;

		// Ambient lighting
		case ID_LT_AMBIENT:
			//glEnable(GL_COLOR_MATERIAL);
			canvas->lightType = LIGHT_AMBIENT;
			return;

		// Dynamic lighting
		case ID_LT_DIRECTIONAL:
			//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);
			canvas->lightType = LIGHT_DYNAMIC;
			return;

		// Model's ambient lighting
		case ID_LT_MODEL:
			canvas->lightType = LIGHT_MODEL_ONLY;
			return;
	}
}

void ModelViewer::OnCamMenu(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id==ID_CAM_FRONT)
		canvas->model->rot.y = -90.0f;
	else if (id==ID_CAM_BACK)
		canvas->model->rot.y = 90.0f;
	else if (id==ID_CAM_SIDE)
		canvas->model->rot.y = 0.0f;
	else if (id==ID_CAM_ISO) {
		canvas->model->rot.y = -40.0f;
		canvas->model->rot.x = 20.0f;
	}

	//viewControl->Update();	
}

// Menu button press events
void ModelViewer::OnSetColor(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id==ID_BG_COLOR) {
		canvas->vecBGColor = DoSetColor(canvas->vecBGColor);
		canvas->drawBackground = false;
	//} else if (id==ID_LT_COLOR) {
	//	canvas->ltColor = DoSetColor(canvas->ltColor);
	}
}

// Menu button press events
void ModelViewer::OnEffects(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id == ID_ENCHANTS)
	  enchants->Display();
}

Vec3D ModelViewer::DoSetColor(const Vec3D &defColor)
{
	wxColour dcol(roundf(defColor.x*255.0f), roundf(defColor.y*255.0f), roundf(defColor.z*255.0f));
	bgDialogData.SetChooseFull(true);
	bgDialogData.SetColour(dcol);
 	   
	wxColourDialog dialog(this, &bgDialogData);

	if (dialog.ShowModal() == wxID_OK)
	{
		bgDialogData = dialog.GetColourData();
		wxColour col = bgDialogData.GetColour();
		return Vec3D(col.Red()/255.0f, col.Green()/255.0f, col.Blue()/255.0f);
	}
	return defColor;
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
		wxString logPath = cfgPath.BeforeLast(SLASH)+SLASH+wxT("log.txt");
#ifdef	_WINDOWS
		wxExecute(wxT("notepad.exe ")+logPath);
#elif	_MAC
		wxExecute(wxT("/Applications/TextEdit.app/Contents/MacOS/TextEdit ")+logPath);
#endif
	}
}

void ModelViewer::OnGameToggle(wxCommandEvent &event)
{
	int ID = event.GetId();
	if (ID == ID_LOAD_WOW)
		LoadWoW();
}

void ModelViewer::LoadWoW()
{
  if (gamePath.IsEmpty() || !wxDirExists(gamePath)) {
    getGamePath();
  }

  Game::instance().init(QString(gamePath.c_str()));

  // init game version
  SetStatusText(wxString(GAMEDIRECTORY.version().toStdString()), 1);

  // init game locale
  std::vector<std::string> localesFound = GAMEDIRECTORY.localesFound();

  if(localesFound.empty())
  {
    wxString message =  wxString::Format(wxT("Fatal Error: Could not find any locale from your World of Warcraft folder"));
    wxMessageDialog *dial = new wxMessageDialog(NULL, message, wxT("World of Warcraft No locale found"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }

  std::string locale = localesFound[0];

  unsigned int nbLocales = localesFound.size();

  if(nbLocales > 1)
  {
    wxString * availableLocales = new wxString[nbLocales];
    for(size_t i=0;i<nbLocales;i++)
      availableLocales[i] = wxString(localesFound[i].c_str());

    long id = wxGetSingleChoiceIndex(_("Please select a locale:"), _("Locale"), nbLocales, availableLocales);
    if (id != -1)
      locale = localesFound[id];
  }

  if(!GAMEDIRECTORY.setLocale(locale))
  {
    wxString message =  wxString::Format(wxT("Fatal Error: Could not load your World of Warcraft Data folder (error %d)."), GAMEDIRECTORY.lastError());
    wxMessageDialog *dial = new wxMessageDialog(NULL, message, wxT("World of Warcraft Not Found"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }


  langName = GAMEDIRECTORY.locale();

  SetStatusText(wxString(GAMEDIRECTORY.locale()), 2);

  InitDatabase();

  // Error check
  if (!initDB)
  {
    wxMessageBox(wxT("Some DBC files could not be loaded.  These files are vital to being able to render models correctly.\nFile list has been disabled until you are able to correct this problem."), wxT("DBC Error"));
    fileControl->Disable();
    SetStatusText(wxT("Some DBC files could not be loaded."));
  }
  else
  {
    isWoWLoaded = true;
    SetStatusText(wxT("Initializing WoW Done."));
    fileMenu->Enable(ID_LOAD_WOW, false);
  }

  SetStatusText(wxT("Initializing File Control..."));
  fileControl->Init(this);

  if (charControl->Init() == false)
  {
    SetStatusText(wxT("Error Initializing the Character Controls."));
  };
}

void ModelViewer::OnCharToggle(wxCommandEvent &event)
{
	int ID = event.GetId();
	if (ID == ID_VIEW_NPC)
		charControl->selectNPC(UPDATE_NPC);
	if (ID == ID_VIEW_ITEM)
		charControl->selectItem(UPDATE_SINGLE_ITEM, -1);
	else if (isChar)
		charControl->OnCheck(event);
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

void ModelViewer::OnSave(wxCommandEvent &event)
{
	static wxFileName dir = cfgPath;
		
	if (!canvas || (!canvas->model && !canvas->wmo))
		return;

	if (event.GetId() == ID_FILE_SCREENSHOT) {
		wxString tmp = wxT("screenshot_");
		tmp << ssCounter;
		wxFileDialog dialog(this, wxT("Save screenshot"), dir.GetPath(wxPATH_GET_VOLUME), tmp, wxT("Bitmap Images (*.bmp)|*.bmp|TGA Images (*.tga)|*.tga|JPEG Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
		dialog.SetFilterIndex(imgFormat);

		if (dialog.ShowModal()==wxID_OK) {
			imgFormat = dialog.GetFilterIndex();
			tmp = dialog.GetPath();
			dialog.Show(false);
			canvas->Screenshot(tmp);
			dir.SetPath(tmp);
			ssCounter++;
		}

		//canvas->InitView();

	} else if (event.GetId() == ID_FILE_EXPORTGIF) {
		if (canvas->wmo)
			return;

		if (!canvas->model)
			return;

		if (!video.supportFBO && !video.supportPBO) {
			wxMessageBox(wxT("This function is currently disabled for video cards that don't\nsupport the FrameBufferObject or PixelBufferObject OpenGL extensions"), wxT("Error"));
			return;
		}
		
		wxFileDialog dialog(this, wxT("Save Animation"), dir.GetPath(wxPATH_GET_VOLUME), wxT("filename"), wxT("Animation"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
		
		if (dialog.ShowModal()==wxID_OK) {
			// Save the folder location for next time
			dir.SetPath(dialog.GetPath());

			// Show our exporter window			
			animExporter->Init(dialog.GetPath());
			animExporter->Show(true);
		}

	} else if (event.GetId() == ID_FILE_EXPORTAVI) {
		if (canvas->wmo && !canvas->model)
			return;

		if (!video.supportFBO && !video.supportPBO) {
			wxMessageBox(wxT("This function is currently disabled for video cards that don't\nsupport the FrameBufferObject or PixelBufferObject OpenGL extensions"), wxT("Error"));
			return;
		}
		
		wxFileDialog dialog(this, wxT("Save AVI"), dir.GetPath(wxPATH_GET_VOLUME), wxT("animation.avi"), wxT("animation (*.avi)|*.avi"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT, wxDefaultPosition);
		
		if (dialog.ShowModal()==wxID_OK) {
			animExporter->CreateAvi(dialog.GetPath());
		}

	} else if (event.GetId() == ID_FILE_SCREENSHOTCONFIG) {
		if (!imageControl) {
			imageControl = new ImageControl(this, ID_IMAGE_FRAME, canvas);

			interfaceManager.AddPane(imageControl, wxAuiPaneInfo().
				Name(wxT("Screenshot")).Caption(wxT("Screenshot")).
				FloatingSize(wxSize(295,145)).Float().Fixed().
				Dockable(false)); //.FloatingPosition(GetStartPosition())
		}

		imageControl->OnShow(&interfaceManager);
	}
}

void ModelViewer::OnBackground(wxCommandEvent &event)
{
	static wxFileName dir = cfgPath;
	
	int id = event.GetId();

	if (id == ID_BACKGROUND) {
		if (event.IsChecked()) {
		  wxFileDialog dialog(this, wxT("Load Background"), dir.GetPath(wxPATH_GET_VOLUME), wxEmptyString, wxT("All (*.bmp;*.jpg;*.png;*.avi)|*.bmp;*.jpg;*.png;*.avi|Bitmap Images (*.bmp)|*.bmp|Jpeg Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png|AVI Video file(*.avi)|*.avi"));
			if (dialog.ShowModal() == wxID_OK) {
				canvas->LoadBackground(dialog.GetPath());
				dir.SetPath(dialog.GetPath());
				viewMenu->Check(ID_BACKGROUND, canvas->drawBackground);
			} else {
				viewMenu->Check(ID_BACKGROUND, false);
			}
		} else {
			canvas->drawBackground = false;
		}
	} else if (id == ID_SKYBOX) {
		if (canvas->skyModel) {
			wxDELETE(canvas->skyModel);
			canvas->sky->delChildren();
			
		} else {
			// List of skybox models, LightSkybox.dbc
			wxArrayString skyboxes;

			sqlResult skyboxesInfos = GAMEDATABASE.sqlQuery("SELECT DISTINCT name FROM LightSkybox");

			if(skyboxesInfos.valid && !skyboxesInfos.values.empty())
			{
				for(unsigned int i=0, imax = skyboxesInfos.values.size() ; i <imax ; i++)
				{
					skyboxes.Add(skyboxesInfos.values[i][0].replace(".mdx",".m2").toStdString());
				}
			}

			skyboxes.Add(wxT("World\\Outland\\PassiveDoodads\\SkyBox\\OutlandSkyBox.m2"));
			skyboxes.Sort();


			wxSingleChoiceDialog skyDialog(this, wxT("Choose"), wxT("Select a Sky Box"), skyboxes);
			if (skyDialog.ShowModal() == wxID_OK && skyDialog.GetStringSelection() != wxEmptyString) {
				canvas->skyModel = new WoWModel(GAMEDIRECTORY.getFile(skyDialog.GetStringSelection().c_str()), false);
				canvas->sky->setModel(canvas->skyModel);
			}
		}
		
		canvas->drawSky = event.IsChecked();
	}
}

void ModelViewer::SaveChar(wxString fn, bool equipmentOnly /*= false*/)
{
  QFile file(fn.c_str());
  if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << fn.c_str();
    return;
  }

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();
  stream.writeStartElement("SavedCharacter");
  stream.writeAttribute("version", "1.0");
  // save model itself
  if(!equipmentOnly)
    canvas->model->save(stream);

  // then save equipment
  stream.writeStartElement("equipment");

  for(WoWModel::iterator it = canvas->model->begin();
      it != canvas->model->end();
      ++it)
    (*it)->save(stream);

  stream.writeEndElement(); // equipment

  stream.writeEndElement(); // SavedCharacter
  stream.writeEndDocument();

  file.close();
}

void ModelViewer::LoadChar(wxString fn, bool equipmentOnly /* = false */)
{
  QFile file(fn.c_str());
  if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << fn.c_str();
    return;
  }

  if(!equipmentOnly)
  {
    // Clear the existing model
    if (isWMO)
    {
      //canvas->clearAttachments();
      wxDELETE(canvas->wmo);
      canvas->wmo = NULL;
    }
    else if (isModel)
    {
      canvas->clearAttachments();
      delete canvas->model;
      canvas->model = NULL;
    }
  }

  QXmlStreamReader reader;
  reader.setDevice(&file);
  while (!reader.atEnd())
  {
    reader.readNext();
    if(reader.hasError())
    {
      file.close();
      file.open(QIODevice::ReadOnly | QIODevice::Text);
      LOG_INFO << "Loading character from legacy file";
      QTextStream in(&file);

      std::vector<QString> values;
      while (!in.atEnd())
      {
        QString line = in.readLine();
        if(!line.isEmpty())
          values.push_back(line);
      }

      unsigned int lineIndex = 0;

      // modelname (if available)
      if(values[lineIndex].contains("m2",Qt::CaseInsensitive))
      {
        LOG_INFO << "modelname" << values[lineIndex];

        // Load the model
        LoadModel(GAMEDIRECTORY.getFile(values[lineIndex]));
        canvas->model->modelType = MT_CHAR;
        lineIndex++;
      }

      // race and gender, we don't care
      lineIndex++;

      // character customization
      QStringList multiVal = values[lineIndex].split(" ");
      if(multiVal.size() >= 5)
      {
        LOG_INFO << "skin color" << multiVal[0];
        charControl->model->cd.setSkinColor(multiVal[0].toInt());

        LOG_INFO << "face type" << multiVal[0];
        charControl->model->cd.setSkinColor(multiVal[1].toInt());

        LOG_INFO << "hair color" << multiVal[0];
        charControl->model->cd.setSkinColor(multiVal[2].toInt());

        LOG_INFO << "hair style" << multiVal[0];
        charControl->model->cd.setSkinColor(multiVal[3].toInt());

        LOG_INFO << "facial hair" << multiVal[0];
        charControl->model->cd.setSkinColor(multiVal[4].toInt());
      }

      // eye glow (if present)
      if(multiVal.size() >= 7)
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

      CharSlots legacySlots[15] = {CS_HEAD, NUM_CHAR_SLOTS, CS_SHOULDER, CS_BOOTS, CS_BELT, CS_SHIRT, CS_PANTS, CS_CHEST, CS_BRACERS, CS_GLOVES, CS_HAND_RIGHT,
          CS_HAND_LEFT, CS_CAPE, CS_TABARD, NUM_CHAR_SLOTS};

      for (unsigned int i=0; i<15 && lineIndex < values.size(); i++, lineIndex++)
      {
        LOG_INFO << "item" << i << "=>" << values[lineIndex].toInt();
        WoWItem * item = charControl->model->getItem(legacySlots[i]);
        if(item)
          item->setId(values[lineIndex].toInt());
      }

      // read tabard customization (if needed)
      if(lineIndex < values.size())
      {
        WoWItem * tabard = charControl->model->getItem(CS_TABARD);
        // 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
        if(tabard && (tabard->id() == 5976 || tabard->id() == 69209 || tabard->id() == 69210))
        {
          LOG_INFO << "read custom tabard values" << values[lineIndex];
          multiVal = values[lineIndex].split(" ");
          if(multiVal.size() >= 5)
          {
            charControl->model->td.Background = multiVal[0].toInt();
            charControl->model->td.Border = multiVal[1].toInt();
            charControl->model->td.BorderColor = multiVal[2].toInt();
            charControl->model->td.Icon = multiVal[3].toInt();
            charControl->model->td.IconColor = multiVal[4].toInt();
            charControl->model->td.showCustom = true;
          }
        }
      }
    }

    if (reader.isStartElement())
    {
      if (reader.name() == "model" && !equipmentOnly)
      {
        reader.readNext();
        while(reader.isStartElement()==false)
          reader.readNext();

        if(reader.name() == "file")
        {
          QString modelname = reader.attributes().value("name").toString();
          LoadModel(GAMEDIRECTORY.getFile(modelname));
          canvas->model->load(reader);
        }
        else
        {
          LOG_ERROR << "Failed to find character filename in file";
          return;
        }
      }

      if(reader.name() == "equipment")
      {
        for(WoWModel::iterator it = canvas->model->begin();
            it != canvas->model->end();
            ++it)
          (*it)->load(reader);
      }
    }
  }

	charControl->RefreshModel();
	charControl->RefreshEquipment();

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

void ModelViewer::OnAbout(wxCommandEvent &event)
{
	wxAboutDialogInfo info;
	info.SetName(GLOBALSETTINGS.appName());
	wxString l_version = L"\n" + GLOBALSETTINGS.appVersion() + L" (" + GLOBALSETTINGS.buildName() + L")\n";
	
	if(GLOBALSETTINGS.isBeta())
	  l_version += "BETA VERSION";
	
	info.SetVersion(l_version);

	info.AddDeveloper(wxT("Ufo_Z"));
	info.AddDeveloper(wxT("Darjk"));
	info.AddDeveloper(wxT("Chuanhsing"));
	info.AddDeveloper(wxT("Kjasi (A.K.A. Sephiroth3D)"));
	info.AddDeveloper(wxT("Tob.Franke"));
	info.AddDeveloper(wxT("Jeromnimo"));
	info.AddTranslator(wxT("MadSquirrel (French)"));
	info.AddTranslator(wxT("Tigurius (Deutsch)"));
	info.AddTranslator(wxT("Kurax (Chinese)"));

	info.SetWebSite(wxT("http://wowmodelviewer.net/forum/"));
    info.SetCopyright(
wxString(wxT("World of Warcraft(R) is a Registered trademark of\n\
Blizzard Entertainment(R). All game assets and content\n\
is (C)2004-2011 Blizzard Entertainment(R). All rights reserved.")));

	info.SetLicence(wxT("WoW Model Viewer is released under the GNU General Public License v3, Non-Commercial Use."));

	info.SetDescription(wxT("WoW Model Viewer is a 3D model viewer for World of Warcraft.\nIt uses the data files included with the game to display\nthe 3D models from the game: creatures, characters, spell\neffects, objects and so forth.\n\nCredits To: Linghuye,  nSzAbolcs,  Sailesh, Terran and Cryect\nfor their contributions either directly or indirectly."));

	wxBitmap * bitmap = createBitmapFromResource("ABOUTICON",wxBITMAP_TYPE_XPM,128,128);
	wxIcon icon;
	icon.CopyFromBitmap(*bitmap);

#if defined (_LINUX)
	//icon.LoadFile(wxT("../bin_support/icon/wmv_xpm"));
#elif defined (_MAC)
	//icon.LoadFile(wxT("../bin_support/icon/wmv.icns"));
#endif

	info.SetIcon(icon);

	// FIXME: Doesn't link on OSX
	wxAboutBox(info);
}

void ModelViewer::OnCheckForUpdate(wxCommandEvent &event)
{
  wxExecute("UpdateManager.exe",wxEXEC_SYNC);
}

void ModelViewer::OnCanvasSize(wxCommandEvent &event)
{
	switch (event.GetId())
	{
		case ID_CANVASS120  :	SetCanvasSize(120, 120);
					break;
		case ID_CANVASS512  :	SetCanvasSize(512, 512);
					break;
		case ID_CANVASS1024 :	SetCanvasSize(1024, 1024); 
					break; 
		case ID_CANVASF480  :	SetCanvasSize(640, 480);
					break;
		case ID_CANVASF600  :	SetCanvasSize(800, 600);
					break;
		case ID_CANVASF768  :	SetCanvasSize(1024, 768);
					break;
		case ID_CANVASF864  :	SetCanvasSize(1152, 864);
					break;
		case ID_CANVASF1200 :	SetCanvasSize(1600, 1200); 
					break;
		case ID_CANVASW480  :	SetCanvasSize(864, 480);
					break;
		case ID_CANVASW720  :	SetCanvasSize(1280, 720);
					break;
		case ID_CANVASW1080 :	SetCanvasSize(1920, 1080);
					break;
		case ID_CANVASM768  :	SetCanvasSize(1280, 768);
					break;
		case ID_CANVASM1200 :	SetCanvasSize(1900, 1200);
					break;
	}
}

void ModelViewer::SetCanvasSize(uint32 sizex, uint32 sizey)
{
	if (canvas && sizex && sizey)
	{
		canvas->SetMinSize(wxSize(sizex, sizey));
		// Fit() needs to be called twice to ensure it resizes properly for small sizes.
		// (At 120x120 the menu will wrap and impinge on the canvas, so need to call Fit() again!)
		// It's clunky, but it's the only way I can think of to do it - Wain
		Fit();
		Fit();
	}
}

void ModelViewer::UpdateCanvasStatus()
{
  // called by ModelCanvas::OnSize() to display updated canvas dimensions on the status bar
  int canvx = 0, canvy = 0;
  canvas->GetClientSize(&canvx, &canvy);
  SetStatusText(wxString::Format(wxT("Canvas: %i x %i"), canvx, canvy), 3);
}

void ModelViewer::ModelInfo()
{
	if (!canvas->model)
		return;
	WoWModel *m = canvas->model;
	wxString fn = wxT("ModelInfo.xml");
	// FIXME: ofstream is not compatible with multibyte path name
	ofstream xml(fn.fn_str(), ios_base::out | ios_base::trunc);

	if (!xml.is_open()) {
		LOG_ERROR << "Unable to open file '" << fn.c_str() << "'. Could not export model.";
		return;
	}

	xml << "<m2>" << endl;
	xml << "  <info>" << endl;
	xml << "    <modelname>" <<m->modelname.c_str()  << "</modelname>" << endl;
	xml << "  </info>" << endl;
	xml << "  <header>" << endl;
//	xml << "    <id>" << m->header.id << "</id>" << endl;
	xml << "    <nameLength>" << m->header.nameLength << "</nameLength>" << endl;
	xml << "    <nameOfs>" << m->header.nameOfs << "</nameOfs>" << endl;
//	xml << "    <name>" << f.getBuffer()+m->header.nameOfs << "</name>" << endl; // @TODO
	xml << "    <GlobalModelFlags>" << m->header.GlobalModelFlags << "</GlobalModelFlags>" << endl;
	xml << "    <nGlobalSequences>" << m->header.nGlobalSequences << "</nGlobalSequences>" << endl;
	xml << "    <ofsGlobalSequences>" << m->header.ofsGlobalSequences << "</ofsGlobalSequences>" << endl;
	xml << "    <nAnimations>" << m->header.nAnimations << "</nAnimations>" << endl;
	xml << "    <ofsAnimations>" << m->header.ofsAnimations << "</ofsAnimations>" << endl;
	xml << "    <nAnimationLookup>" << m->header.nAnimationLookup << "</nAnimationLookup>" << endl;
	xml << "    <ofsAnimationLookup>" << m->header.ofsAnimationLookup << "</ofsAnimationLookup>" << endl;
	xml << "    <nBones>" << m->header.nBones << "</nBones>" << endl;
	xml << "    <ofsBones>" << m->header.ofsBones << "</ofsBones>" << endl;
	xml << "    <nKeyBoneLookup>" << m->header.nKeyBoneLookup << "</nKeyBoneLookup>" << endl;
	xml << "    <ofsKeyBoneLookup>" << m->header.ofsKeyBoneLookup << "</ofsKeyBoneLookup>" << endl;
	xml << "    <nVertices>" << m->header.nVertices << "</nVertices>" << endl;
	xml << "    <ofsVertices>" << m->header.ofsVertices << "</ofsVertices>" << endl;
	xml << "    <nViews>" << m->header.nViews << "</nViews>" << endl;
	xml << "    <lodname>" <<m->lodname.c_str()  << "</lodname>" << endl;
	xml << "    <nColors>" << m->header.nColors << "</nColors>" << endl;
	xml << "    <ofsColors>" << m->header.ofsColors << "</ofsColors>" << endl;
	xml << "    <nTextures>" << m->header.nTextures << "</nTextures>" << endl;
	xml << "    <ofsTextures>" << m->header.ofsTextures << "</ofsTextures>" << endl;
	xml << "    <nTransparency>" << m->header.nTransparency << "</nTransparency>" << endl;
	xml << "    <ofsTransparency>" << m->header.ofsTransparency << "</ofsTransparency>" << endl;
	xml << "    <nTexAnims>" << m->header.nTexAnims << "</nTexAnims>" << endl;
	xml << "    <ofsTexAnims>" << m->header.ofsTexAnims << "</ofsTexAnims>" << endl;
	xml << "    <nTexReplace>" << m->header.nTexReplace << "</nTexReplace>" << endl;
	xml << "    <ofsTexReplace>" << m->header.ofsTexReplace << "</ofsTexReplace>" << endl;
	xml << "    <nTexFlags>" << m->header.nTexFlags << "</nTexFlags>" << endl;
	xml << "    <ofsTexFlags>" << m->header.ofsTexFlags << "</ofsTexFlags>" << endl;
	xml << "    <nBoneLookup>" << m->header.nBoneLookup << "</nBoneLookup>" << endl;
	xml << "    <ofsBoneLookup>" << m->header.ofsBoneLookup << "</ofsBoneLookup>" << endl;
	xml << "    <nTexLookup>" << m->header.nTexLookup << "</nTexLookup>" << endl;
	xml << "    <ofsTexLookup>" << m->header.ofsTexLookup << "</ofsTexLookup>" << endl;
	xml << "    <nTexUnitLookup>" << m->header.nTexUnitLookup << "</nTexUnitLookup>" << endl;
	xml << "    <ofsTexUnitLookup>" << m->header.ofsTexUnitLookup << "</ofsTexUnitLookup>" << endl;
	xml << "    <nTransparencyLookup>" << m->header.nTransparencyLookup << "</nTransparencyLookup>" << endl;
	xml << "    <ofsTransparencyLookup>" << m->header.ofsTransparencyLookup << "</ofsTransparencyLookup>" << endl;
	xml << "    <nTexAnimLookup>" << m->header.nTexAnimLookup << "</nTexAnimLookup>" << endl;
	xml << "    <ofsTexAnimLookup>" << m->header.ofsTexAnimLookup << "</ofsTexAnimLookup>" << endl;
    xml << "    <collisionSphere>" << endl;
	xml << "      <min>" << m->header.collisionSphere.min << "</min>" <<  endl;
	xml << "      <max>" << m->header.collisionSphere.max << "</max>" <<  endl;
	xml << "      <radius>" << m->header.collisionSphere.radius << "</radius>" << endl;
	xml << "    </collisionSphere>" << endl;
    xml << "    <boundSphere>" << endl;
	xml << "      <min>" << m->header.boundSphere.min << "</min>" <<  endl;
	xml << "      <max>" << m->header.boundSphere.max << "</max>" <<  endl;
	xml << "      <radius>" << m->header.boundSphere.radius << "</radius>" << endl;
	xml << "    </boundSphere>" << endl;
	xml << "    <nBoundingTriangles>" << m->header.nBoundingTriangles << "</nBoundingTriangles>" << endl;
	xml << "    <ofsBoundingTriangles>" << m->header.ofsBoundingTriangles << "</ofsBoundingTriangles>" << endl;
	xml << "    <nBoundingVertices>" << m->header.nBoundingVertices << "</nBoundingVertices>" << endl;
	xml << "    <ofsBoundingVertices>" << m->header.ofsBoundingVertices << "</ofsBoundingVertices>" << endl;
	xml << "    <nBoundingNormals>" << m->header.nBoundingNormals << "</nBoundingNormals>" << endl;
	xml << "    <ofsBoundingNormals>" << m->header.ofsBoundingNormals << "</ofsBoundingNormals>" << endl;
	xml << "    <nAttachments>" << m->header.nAttachments << "</nAttachments>" << endl;
	xml << "    <ofsAttachments>" << m->header.ofsAttachments << "</ofsAttachments>" << endl;
	xml << "    <nAttachLookup>" << m->header.nAttachLookup << "</nAttachLookup>" << endl;
	xml << "    <ofsAttachLookup>" << m->header.ofsAttachLookup << "</ofsAttachLookup>" << endl;
	xml << "    <nEvents>" << m->header.nEvents << "</nEvents>" << endl;
	xml << "    <ofsEvents>" << m->header.ofsEvents << "</ofsEvents>" << endl;
	xml << "    <nLights>" << m->header.nLights << "</nLights>" << endl;
	xml << "    <ofsLights>" << m->header.ofsLights << "</ofsLights>" << endl;
	xml << "    <nCameras>" << m->header.nCameras << "</nCameras>" << endl;
	xml << "    <ofsCameras>" << m->header.ofsCameras << "</ofsCameras>" << endl;
	xml << "    <nCameraLookup>" << m->header.nCameraLookup << "</nCameraLookup>" << endl;
	xml << "    <ofsCameraLookup>" << m->header.ofsCameraLookup << "</ofsCameraLookup>" << endl;
	xml << "    <nRibbonEmitters>" << m->header.nRibbonEmitters << "</nRibbonEmitters>" << endl;
	xml << "    <ofsRibbonEmitters>" << m->header.ofsRibbonEmitters << "</ofsRibbonEmitters>" << endl;
	xml << "    <nParticleEmitters>" << m->header.nParticleEmitters << "</nParticleEmitters>" << endl;
	xml << "    <ofsParticleEmitters>" << m->header.ofsParticleEmitters << "</ofsParticleEmitters>" << endl;
	xml << "  </header>" << endl;

	xml << "  <SkeletonAndAnimation>" << endl;

	xml << "  <GlobalSequences size=\"" << m->header.nGlobalSequences << "\">" << endl;
	for(size_t i=0; i<m->header.nGlobalSequences; i++)
		xml << "<Sequence>" << m->globalSequences[i] << "</Sequence>" << endl;
	xml << "  </GlobalSequences>" << endl;

	xml << "  <Animations size=\"" << m->header.nAnimations << "\">" << endl;
	if (m->anims) {
		for(size_t i=0; i<m->header.nAnimations; i++) {
			xml << "    <Animation id=\"" << i << "\">" << endl;
			xml << "      <animID>"<< m->anims[i].animID << "</animID>" << endl;
			wxString strName;
			QString query = QString("SELECT Name FROM AnimationData WHERE ID = %1").arg(m->anims[i].animID);
			sqlResult anim = GAMEDATABASE.sqlQuery(query);
			if(anim.valid && !anim.empty())
			  strName = anim.values[0][0].toStdString().c_str();
			else
			  strName = wxT("???");
			xml << "      <animName>"<< strName.c_str() << "</animName>" << endl;
			xml << "      <length>"<< m->anims[i].timeEnd << "</length>" << endl;
			xml << "      <moveSpeed>"<< m->anims[i].moveSpeed << "</moveSpeed>" << endl;
			xml << "      <flags>"<< m->anims[i].flags << "</flags>" << endl;
			xml << "      <probability>"<< m->anims[i].probability << "</probability>" << endl;
			xml << "      <d1>"<< m->anims[i].d1 << "</d1>" << endl;
			xml << "      <d2>"<< m->anims[i].d2 << "</d2>" << endl;
			xml << "      <playSpeed>"<< m->anims[i].playSpeed << "</playSpeed>" << endl;
			xml << "      <boxA>"<< m->anims[i].boundSphere.min << "</boxA>" << endl;
			xml << "      <boxB>"<< m->anims[i].boundSphere.max << "</boxB>" << endl;
			xml << "      <rad>"<< m->anims[i].boundSphere.radius << "</rad>" << endl;
			xml << "      <NextAnimation>"<< m->anims[i].NextAnimation << "</NextAnimation>" << endl;
			xml << "      <Index>"<< m->anims[i].Index << "</Index>" << endl;
			xml << "    </Animation>" << endl;
		}
	}
	xml << "  </Animations>" << endl;

	xml << "  <AnimationLookups size=\"" << m->header.nAnimationLookup << "\">" << endl;
	if (m->animLookups) {
		for(size_t i=0; i<m->header.nAnimationLookup; i++)
			xml << "    <AnimationLookup id=\"" << i << "\">" << m->animLookups[i] << "</AnimationLookup>" << endl;
	}
	xml << "  </AnimationLookups>" << endl;

	xml << "  <Bones size=\"" << m->header.nBones << "\">" << endl;
	if (m->bones) {
		for(size_t i=0; i<m->header.nBones; i++) {
			xml << "    <Bone id=\"" << i << "\">" << endl;
			xml << "      <keyboneid>"<< m->bones[i].boneDef.keyboneid << "</keyboneid>" << endl;
			xml << "      <billboard>"<< m->bones[i].billboard << "</billboard>" << endl;
			xml << "      <parent>"<< m->bones[i].boneDef.parent << "</parent>" << endl;
			xml << "      <geoid>"<< m->bones[i].boneDef.geoid << "</geoid>" << endl;
			xml << "      <unknown>"<< m->bones[i].boneDef.unknown << "</unknown>" << endl;
#if 1 // too huge
			// AB translation
			xml << "      <trans>" << endl;
			xml << m->bones[i].trans;
			xml << "      </trans>" << endl;
			// AB rotation
			xml << "      <rot>" << endl;
			xml << m->bones[i].rot;
			xml << "      </rot>" << endl;
			// AB scaling
			xml << "      <scale>" << endl;
			xml << m->bones[i].scale;
			xml << "      </scale>" << endl;
#endif
			xml << "      <pivot>"<< m->bones[i].boneDef.pivot << "</pivot>" << endl;
			xml << "    </Bone>" << endl;
		}
	}
	xml << "  </Bones>" << endl;

//	xml << "  <BoneLookups size=\"" << m->header.nBoneLookup << "\">" << endl;
//	uint16 *boneLookup = (uint16 *)(f.getBuffer() + m->header.ofsBoneLookup);
//	for(size_t i=0; i<m->header.nBoneLookup; i++) {
//		xml << "    <BoneLookup id=\"" << i << "\">" << boneLookup[i] << "</BoneLookup>" << endl;
//	}
//	xml << "  </BoneLookups>" << endl;

	xml << "  <KeyBoneLookups size=\"" << m->header.nKeyBoneLookup << "\">" << endl;
	for(size_t i=0; i<m->header.nKeyBoneLookup; i++)
		xml << "    <KeyBoneLookup id=\"" << i << "\">" << m->keyBoneLookup[i] << "</KeyBoneLookup>" << endl;
	xml << "  </KeyBoneLookups>" << endl;

	xml << "  </SkeletonAndAnimation>" << endl;

	xml << "  <GeometryAndRendering>" << endl;

//	xml << "  <Vertices size=\"" << m->header.nVertices << "\">" << endl;
//	ModelVertex *verts = (ModelVertex*)(f.getBuffer() + m->header.ofsVertices);
//	for(uint32 i=0; i<m->header.nVertices; i++) {
//		xml << "    <Vertice id=\"" << i << "\">" << endl;
//		xml << "      <pos>" << verts[i].pos << "</pos>" << endl; // TODO
//		xml << "    </Vertice>" << endl;
//	}
	xml << "  </Vertices>" << endl; // TODO
	xml << "  <Views>" << endl;

//	xml << "  <Indices size=\"" << view->nIndex << "\">" << endl;
//	xml << "  </Indices>" << endl; // TODO
//	xml << "  <Triangles size=\""<< view->nTris << "\">" << endl;
//	xml << "  </Triangles>" << endl; // TODO
//	xml << "  <Properties size=\"" << view->nProps << "\">" << endl;
//	xml << "  </Properties>" << endl; // TODO
//	xml << "  <Subs size=\"" << view->nSub << "\">" << endl;
//	xml << "  </Subs>" << endl; // TODO

	xml << "	<RenderPasses size=\"" << m->passes.size() << "\">" << endl;
	for (size_t i=0; i<m->passes.size(); i++) {
		xml << "	  <RenderPass id=\"" << i << "\">" << endl;
		ModelRenderPass &p = m->passes[i];
		xml << "      <indexStart>" << p.indexStart << "</indexStart>" << endl;
		xml << "      <indexCount>" << p.indexCount << "</indexCount>" << endl;
		xml << "      <vertexStart>" << p.vertexStart << "</vertexStart>" << endl;
		xml << "      <vertexEnd>" << p.vertexEnd << "</vertexEnd>" << endl;
		xml << "      <tex>" << p.tex << "</tex>" << endl;
		if (p.tex >= 0)
			xml << "      <texName>" << m->TextureList[p.tex].toStdString() << "</texName>" << endl;
		xml << "      <useTex2>" << p.useTex2 << "</useTex2>" << endl;
		xml << "      <useEnvMap>" << p.useEnvMap << "</useEnvMap>" << endl;
		xml << "      <cull>" << p.cull << "</cull>" << endl;
		xml << "      <trans>" << p.trans << "</trans>" << endl;
		xml << "      <unlit>" << p.unlit << "</unlit>" << endl;
		xml << "      <noZWrite>" << p.noZWrite << "</noZWrite>" << endl;
		xml << "      <billboard>" << p.billboard << "</billboard>" << endl;
		xml << "      <p>" << p.p << "</p>" << endl;
		xml << "      <texanim>" << p.texanim << "</texanim>" << endl;
		xml << "      <color>" << p.color << "</color>" << endl;
		xml << "      <opacity>" << p.opacity << "</opacity>" << endl;
		xml << "      <blendmode>" << p.blendmode << "</blendmode>" << endl;
		xml << "      <geoset>" << p.geoset << "</geoset>" << endl;
		xml << "      <swrap>" << p.swrap << "</swrap>" << endl;
		xml << "      <twrap>" << p.twrap << "</twrap>" << endl;
		xml << "      <ocol>" << p.ocol << "</ocol>" << endl;
		xml << "      <ecol>" << p.ecol << "</ecol>" << endl;
		xml << "	  </RenderPass>" << endl;
	}
	xml << "	</RenderPasses>" << endl;

	xml << "	<Geosets size=\"" << m->geosets.size() << "\">" << endl;
	for (size_t i=0; i<m->geosets.size(); i++) {
		xml << "	  <Geoset id=\"" << i << "\">" << endl;
		xml << "      <id>" << m->geosets[i].id << "</id>" << endl;
		xml << "      <vstart>" << m->geosets[i].vstart << "</vstart>" << endl;
		xml << "      <vcount>" << m->geosets[i].vcount << "</vcount>" << endl;
		xml << "      <istart>" << m->geosets[i].istart << "</istart>" << endl;
		xml << "      <icount>" << m->geosets[i].icount << "</icount>" << endl;
		xml << "      <nSkinnedBones>" << m->geosets[i].nSkinnedBones << "</nSkinnedBones>" << endl;
		xml << "      <StartBones>" << m->geosets[i].StartBones << "</StartBones>" << endl;
		xml << "      <rootBone>" << m->geosets[i].rootBone << "</rootBone>" << endl;
		xml << "      <nBones>" << m->geosets[i].nBones << "</nBones>" << endl;
		xml << "      <BoundingBox>" << m->geosets[i].BoundingBox[0] << "</BoundingBox>" << endl;
		xml << "      <BoundingBox>" << m->geosets[i].BoundingBox[1] << "</BoundingBox>" << endl;
		xml << "      <radius>" << m->geosets[i].radius << "</radius>" << endl;
		xml << "	  </Geoset>" << endl;
	}
	xml << "	</Geosets>" << endl;

//	ModelTexUnit *tex = (ModelTexUnit*)(g.getBuffer() + view->ofsTex);
//	xml << "	<TexUnits size=\"" << view->nTex << "\">" << endl;
//	for (size_t i=0; i<view->nTex; i++) {
//		xml << "	  <TexUnit id=\"" << i << "\">" << endl;
//		xml << "      <flags>" << tex[i].flags << "</flags>" << endl;
//		xml << "      <shading>" << tex[i].shading << "</shading>" << endl;
//		xml << "      <op>" << tex[i].op << "</op>" << endl;
//		xml << "      <op2>" << tex[i].op2 << "</op2>" << endl;
//		xml << "      <colorIndex>" << tex[i].colorIndex << "</colorIndex>" << endl;
//		xml << "      <flagsIndex>" << tex[i].flagsIndex << "</flagsIndex>" << endl;
//		xml << "      <texunit>" << tex[i].texunit << "</texunit>" << endl;
//		xml << "      <mode>" << tex[i].mode << "</mode>" << endl;
//		xml << "      <textureid>" << tex[i].textureid << "</textureid>" << endl;
//		xml << "      <texunit2>" << tex[i].texunit2 << "</texunit2>" << endl;
//		xml << "      <transid>" << tex[i].transid << "</transid>" << endl;
//		xml << "      <texanimid>" << tex[i].texanimid << "</texanimid>" << endl;
//		xml << "	  </TexUnit>" << endl;
//	}
//	xml << "	</TexUnits>" << endl;

	xml << "  </Views>" << endl;

	xml << "  <RenderFlags></RenderFlags>" << endl;

	xml << "	<Colors size=\"" << m->header.nColors << "\">" << endl;
	for(size_t i=0; i<m->header.nColors; i++) {
		xml << "    <Color id=\"" << i << "\">" << endl;
		// AB color
		xml << "    <color>" << endl;
		xml << m->colors[i].color;
		xml << "    </color>" << endl;
		// AB opacity
		xml << "    <opacity>" << endl;
		xml << m->colors[i].opacity;
		xml << "    </opacity>" << endl;
		xml << "    </Color>" << endl;
	}
	xml << "	</Colors>" << endl;

	xml << "	<Transparency size=\"" << m->header.nTransparency << "\">" << endl;
	if (m->transparency) {
		for(size_t i=0; i<m->header.nTransparency; i++) {
			xml << "    <Tran id=\"" << i << "\">" << endl;
			// AB trans
			xml << "    <trans>" << endl;
			xml << m->transparency[i].trans;
			xml << "    </trans>" << endl;
			xml << "    </Tran>" << endl;
		}
	}
	xml << "	</Transparency>" << endl;

	xml << "  <TransparencyLookup></TransparencyLookup>" << endl;

//	ModelTextureDef *texdef = (ModelTextureDef*)(f.getBuffer() + m->header.ofsTextures);
//	xml << "	<Textures size=\"" << m->header.nTextures << "\">" << endl;
//	for(size_t i=0; i<m->header.nTextures; i++) {
//		xml << "	  <Texture id=\"" << i << "\">" << endl;
//		xml << "      <type>" << texdef[i].type << "</type>" << endl;
//		xml << "      <flags>" << texdef[i].flags << "</flags>" << endl;
//		//xml << "      <nameLen>" << texdef[i].nameLen << "</nameLen>" << endl;
//		//xml << "      <nameOfs>" << texdef[i].nameOfs << "</nameOfs>" << endl;
//		if (texdef[i].type == TEXTURE_FILENAME)
//			xml << "		<name>" << f.getBuffer()+texdef[i].nameOfs  << "</name>" << endl;
//		xml << "	  </Texture>" << endl;
//	}
//	xml << "	</Textures>" << endl;

//	xml << "  <TexLookups size=\"" << m->header.nTexLookup << "\">" << endl;
//	uint16 *texLookup = (uint16 *)(f.getBuffer() + m->header.ofsTexLookup);
//	for(size_t i=0; i<m->header.nTexLookup; i++) {
//		xml << "    <TexLookup id=\"" << i << "\">" << texLookup[i] << "</TexLookup>" << endl;
//	}
//	xml << "  </TexLookups>" << endl;

	xml << "	<ReplacableTextureLookup></ReplacableTextureLookup>" << endl;

	xml << "  </GeometryAndRendering>" << endl;

	xml << "  <Effects>" << endl;

	xml << "	<TexAnims size=\"" << m->header.nTexAnims << "\">" << endl;
	if (m->texAnims) {
		for(size_t i=0; i<m->header.nTexAnims; i++) {
			xml << "	  <TexAnim id=\"" << i << "\">" << endl;
			// AB trans
			xml << "    <trans>" << endl;
			xml << m->texAnims[i].trans;
			xml << "    </trans>" << endl;
			// AB rot
			xml << "    <rot>" << endl;
			xml << m->texAnims[i].rot;
			xml << "    </rot>" << endl;
			// AB scale
			xml << "    <scale>" << endl;
			xml << m->texAnims[i].scale;
			xml << "    </scale>" << endl;
			xml << "	  </TexAnim>" << endl;
		}
	}
	xml << "	</TexAnims>" << endl;

	xml << "	<RibbonEmitters></RibbonEmitters>" << endl; // TODO

	xml << "	<Particles size=\"" << m->header.nParticleEmitters << "\">" << endl;
	if (m->particleSystems) {
		for (size_t i=0; i<m->header.nParticleEmitters; i++) {
			xml << "	  <Particle id=\"" << i << "\">" << endl;
			xml << m->particleSystems[i];
			xml << "	  </Particle>" << endl;
		}
	}
	xml << "	</Particles>" << endl;

	xml << "  </Effects>" << endl;

	xml << "	<Miscellaneous>" << endl;

	xml << "	<BoundingVolumes></BoundingVolumes>" << endl;
	xml << "	<Lights></Lights>" << endl;
	xml << "	<Cameras></Cameras>" << endl;

	xml << "	<Attachments size=\"" << m->header.nAttachments << "\">" << endl;
	for (size_t i=0; i<m->header.nAttachments; i++) {
		xml << "	  <Attachment id=\"" << i << "\">" << endl;
		xml << "      <id>" << m->atts[i].id << "</id>" << endl;
		xml << "      <bone>" << m->atts[i].bone << "</bone>" << endl;
		xml << "      <pos>" << m->atts[i].pos << "</pos>" << endl;
		xml << "	  </Attachment>" << endl;
	}
	xml << "	</Attachments>" << endl;

	xml << "  <AttachLookups size=\"" << m->header.nAttachLookup << "\">" << endl;
//	int16 *attachLookup = (int16 *)(f.getBuffer() + m->header.ofsAttachLookup);
//	for(size_t i=0; i<m->header.nAttachLookup; i++) {
//		xml << "    <AttachLookup id=\"" << i << "\">" << attachLookup[i] << "</AttachLookup>" << endl;
//	}
//	xml << "  </AttachLookups>" << endl;

	xml << "	<Events size=\"" << m->header.nEvents << "\">" << endl;
	if (m->events) {
		for (size_t i=0; i<m->header.nEvents; i++) {
			xml << "	  <Event id=\"" << i << "\">" << endl;
			xml << m->events[i];
			xml << "	  </Event>" << endl;
		}
	}
	xml << "	</Events>" << endl;

	xml << "	</Miscellaneous>" << endl;

//	xml << "    <>" << m->header. << "</>" << endl;
	xml << "  <TextureLists>" << endl;
	for(size_t i=0; i<m->TextureList.size(); i++) {
		xml << "    <TextureList id=\"" << i << "\">" << m->TextureList[i].toStdString() << "</TextureList>" << endl;
	}
	xml << "  </TextureLists>" << endl;

	xml << "</m2>" << endl;
	xml.close();
//	f.close();
//	g.close();
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
	if (!canvas || !canvas->model || !canvas->root)
		return;

	if (canvas->model->modelType == MT_CHAR)
	  charControl->RefreshModel();
	else
	{
	  //refresh equipment
	  for(WoWModel::iterator it = canvas->model->begin();
	      it != canvas->model->end();
	      ++it)
	    (*it)->refresh();
	}
	modelControl->RefreshModel(canvas->root);
}

void ModelViewer::ImportArmoury(wxString strURL)
{
  CharInfos * result = NULL;

  std::string url = strURL.ToAscii();

  for(PluginManager::iterator it = PLUGINMANAGER.begin();
      it != PLUGINMANAGER.end();
      ++it)
  {
    ImporterPlugin * plugin = dynamic_cast<ImporterPlugin *>(*it);
    if(plugin && plugin->acceptURL(url))
    {
      result = plugin->importChar(url);
    }
  }

	if(result)
	{
	  if(!result->valid)
	  {
	    wxMessageBox(wxT("Improperly Formatted URL.\nMake sure your link ends in /simple or /advanced."),wxT("Bad Armory Link"));
	    return;
	  }
	  // retrieve race name from DB
	  QString query = QString("SELECT ClientFileString FROM ChrRaces WHERE ID = %1").arg(result->raceId);
	  sqlResult r = GAMEDATABASE.sqlQuery(query);

	  QString race = r.values[0][0];

		// Load the model
		QString strModel = "Character\\" + race + MPQ_SLASH + result->gender.c_str() + MPQ_SLASH + race + result->gender.c_str() + ".m2";
		QString strModelHD = "Character\\" + race + MPQ_SLASH + result->gender.c_str() + MPQ_SLASH + race + result->gender.c_str() + "_hd.m2";

		// try with hd model first
		GameFile * file = GAMEDIRECTORY.getFile(strModelHD);
		if(!file)
		  file = GAMEDIRECTORY.getFile(strModel);

		LoadModel(file);

		if (!g_canvas->model)
			return;

		if (result->hasTransmogGear == true)
		{
			LOG_INFO << "Transmogrified Gear was found. Switching items...";
			wxMessageBox(wxT("We found Transmogrified gear on your character. The items your character is wearing will be exchanged for the items they look like."),wxT("Transmog Notice"));
		}

		// Update the model
		g_charControl->model->cd.setSkinColor(result->skinColor);
		g_charControl->model->cd.setFaceType(result->faceType);
		g_charControl->model->cd.setHairColor(result->hairColor);
		g_charControl->model->cd.setHairStyle(result->hairStyle);
		g_charControl->model->cd.setFacialHair(result->facialHair);
		g_charControl->model->cd.eyeGlowType = static_cast<EyeGlowTypes>(result->eyeGlowType);

		if(result->customTabard)
		{
		  g_charControl->model->td.showCustom = true;
		  g_charControl->model->td.Icon = result->tabardIcon;
		  g_charControl->model->td.IconColor = result->IconColor;
		  g_charControl->model->td.Border = result->tabardBorder;
		  g_charControl->model->td.BorderColor = result->BorderColor;
		  g_charControl->model->td.Background = result->Background;
		}

		for(unsigned int i=0 ; i < NUM_CHAR_SLOTS ; i++)
		{
		  WoWItem * item = g_charControl->model->getItem((CharSlots)i);
		  if(item)
		    item->setId(result->equipment[i]);
		}

		g_charControl->RefreshModel();
		g_charControl->RefreshEquipment();

		delete result;
	}
	else
	{
		LOG_ERROR << "There were errors gathering the Armory page.";
		wxMessageBox(wxT("There was an error when gathering the Armory data.\nPlease try again later."),wxT("Armory Error"));

	}
}

void ModelViewer::OnExport(wxCommandEvent &event)
{
  if(!g_charControl->model)
  {
    wxMessageBox(wxT("You must prepare your model before trying to export it."),wxT("Export Error"), wxOK | wxICON_ERROR);
    return;
  }

  std::string exporterLabel = fileMenu->GetLabel(event.GetId()).mb_str();

  PluginManager::iterator it = PLUGINMANAGER.begin();
  for(; it != PLUGINMANAGER.end() ; ++it)
  {
    ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*it);

    if(plugin && plugin->menuLabel() == exporterLabel)
    {
      wxFileDialog saveFileDialog(this, plugin->fileSaveTitle(), "", "",
          plugin->fileSaveFilter(), wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

      if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;

      // START OF HACK
      // @TODO : remove Hack
      // ugly hack waiting for application to be full Qt, and being able to have qt pop up in plugins...
      // today, creating wxDialog in Qt plugins simply crashes, and no qt app in executed to raised a Qt pop up...

      // if exporter supports animations, we have to chose which one to export
      if(plugin->canExportAnimation())
      {
        std::map<int, std::string> animsMap = canvas->model->getAnimsMap();
        wxArrayString values;
        wxArrayInt selection;
        std::vector<int> ids;
        ids.resize(animsMap.size());
        unsigned int i = 0;

        for (size_t i=0; i<canvas->model->header.nAnimations; i++)
        {
          wxString animName = animsMap[canvas->model->anims[i].animID];
          animName << " [";
          animName << i;
          animName << "]";
          values.Add(animName);
          selection.Add(i);
        }

        AnimationExportChoiceDialog animChoiceDlg(this, "", wxT("Animation Choice"),values);
        animChoiceDlg.SetSelections(selection);
        if(animChoiceDlg.ShowModal() == wxID_CANCEL)
          return;

        selection = animChoiceDlg.GetSelections();
        vector<int> animsToExport;
        animsToExport.reserve(selection.GetCount());
        for(unsigned int i = 0 ; i < selection.GetCount() ; i++)
          animsToExport.push_back(canvas->model->anims[selection[i]].animID);

        plugin->setAnimationsToExport(animsToExport);

      }

      // END OF HACK

      if(!plugin->exportModel(canvas->model, saveFileDialog.GetPath().mb_str()))
      {
        wxMessageBox(wxT("An error occurred during export."),wxT("Export Error"), wxOK | wxICON_ERROR);
      }
      else
      {
        wxMessageBox(wxT("Export successfully done."),wxT("Export done"), wxOK | wxICON_INFORMATION);
      }

      break;
    }
  }
}

