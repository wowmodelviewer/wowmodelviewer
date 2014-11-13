#include "modelviewer.h"

#include "app.h"
#include "Attachment.h"
#include "Bone.h"
#include "globalvars.h"
#include "ModelColor.h"
#include "ModelEvent.h"
#include "ModelTransparency.h"
#include "mpq.h"
#include "TextureAnim.h"
#include "UserSkins.h"
#include "util.h"

#include "core/CharInfos.h"
#include "core/GlobalSettings.h"
#include "core/ImporterPlugin.h"
#include "core/PluginManager.h"
#include "metaclasses/Iterator.h"

#include <wx/app.h>
#include <wx/regex.h>
#include <wx/tokenzr.h>
#include <wx/txtstrm.h>
#include <wx/utils.h>

#include "CASCFile.h"
#include "CASCFolder.h"
#include "GameDatabase.h"

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
	EVT_MENU(ID_FILE_DISCOVERY_ITEM, ModelViewer::OnExportOther)
	EVT_MENU(ID_FILE_DISCOVERY_NPC, ModelViewer::OnExportOther)
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
	EVT_MENU(ID_SPELLS, ModelViewer::OnEffects)
	EVT_MENU(ID_EQCREATURE_R, ModelViewer::OnEffects)
	EVT_MENU(ID_EQCREATURE_L, ModelViewer::OnEffects)
	EVT_MENU(ID_SHADER_DEATH, ModelViewer::OnEffects)

	// Options
	EVT_MENU(ID_SAVE_CHAR, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_LOAD_CHAR, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_IMPORT_CHAR, ModelViewer::OnToggleCommand)

	EVT_MENU(ID_USE_NPCSKINS, ModelViewer::OnCharToggle)
	EVT_MENU(ID_DEFAULT_DOODADS, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_ANTIALIAS, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_HWACC, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_ENVMAP, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SHOW_SETTINGS, ModelViewer::OnToggleDock)

	// char controls:
	EVT_MENU(ID_SAVE_EQUIPMENT, ModelViewer::OnSetEquipment)
	EVT_MENU(ID_LOAD_EQUIPMENT, ModelViewer::OnSetEquipment)
	EVT_MENU(ID_CLEAR_EQUIPMENT, ModelViewer::OnSetEquipment)

	EVT_MENU(ID_LOAD_SET, ModelViewer::OnSetEquipment)
	EVT_MENU(ID_LOAD_START, ModelViewer::OnSetEquipment)
	EVT_MENU(ID_LOAD_NPC_START, ModelViewer::OnSetEquipment)

	EVT_MENU(ID_SHOW_UNDERWEAR, ModelViewer::OnCharToggle)
	EVT_MENU(ID_SHOW_EARS, ModelViewer::OnCharToggle)
	EVT_MENU(ID_SHOW_HAIR, ModelViewer::OnCharToggle)
	EVT_MENU(ID_SHOW_FACIALHAIR, ModelViewer::OnCharToggle)
	EVT_MENU(ID_SHOW_FEET, ModelViewer::OnCharToggle)
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

END_EVENT_TABLE()

ModelViewer::ModelViewer()
#ifdef _LINUX
	// Transparency in interfaceManager crashes with Linux compositing
	: interfaceManager(0, wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_VENETIAN_BLINDS_HINT)
#endif
{
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
			if (!video.render) // Something bad must of happened - find a new working display mode
				video.GetAvailableMode();
		} else {
			wxLogMessage(wxT("Error: Failed to find a compatible graphics mode.  Finding first available display mode..."));
			video.GetAvailableMode(); // Get first available display mode that supports the current desktop colour bitdepth
		}
		*/
		
		wxLogMessage(wxT("Setting OpenGL render state..."));
		SetStatusText(wxT("Setting OpenGL render state..."));
		video.InitGL();

		SetStatusText(wxEmptyString);
	} else {
		wxLogMessage(wxT("Critical Error: Unable to create the main window for the application."));
		Close(true);
	}
}

void ModelViewer::InitMenu()
{
	wxLogMessage(wxT("Initializing File Menu..."));

	if (GetStatusBar() == NULL){
		CreateStatusBar(3);
		int widths[] = {-1, 100, 50};
		SetStatusWidths(3, widths);
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
	fileMenu->Append(ID_FILE_EXPORTGIF, _("GIF/Sequence Export"));
	fileMenu->Append(ID_FILE_EXPORTAVI, _("Export AVI"));
	fileMenu->AppendSeparator();

	// --== Continue regular menu ==--
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_DISCOVERY_ITEM, _("Discovery Item"));
	fileMenu->Append(ID_FILE_DISCOVERY_NPC, _("Discovery NPC"));
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_MODEL_INFO, wxT("Export ModelInfo.xml"));

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
		setSize->AppendRadioItem(ID_CANVASS120, wxT("(1:1) 120 x 120"),_("Square (1:1)"));
		setSize->AppendRadioItem(ID_CANVASS512, wxT("(1:1) 512 x 512"),_("Square (1:1)"));
		setSize->AppendRadioItem(ID_CANVASS1024, wxT("(1:1) 1024 x 1024"),_("Square (1:1)"));
		setSize->AppendRadioItem(ID_CANVASF480, wxT("(4:3) 640 x 480"),_("Fullscreen (4:3)"));
		setSize->AppendRadioItem(ID_CANVASF600, wxT("(4:3) 800 x 600"),_("Fullscreen (4:3)"));
		setSize->AppendRadioItem(ID_CANVASF768, wxT("(4:3) 1024 x 768"),_("Fullscreen (4:3)"));
		setSize->AppendRadioItem(ID_CANVASF864, wxT("(4:3) 1152 x 864"),_("Fullscreen (4:3)"));
		setSize->AppendRadioItem(ID_CANVASF1200, wxT("(4:3) 1600 x 1200"),_("Fullscreen (4:3)"));
		setSize->AppendRadioItem(ID_CANVASW480, wxT("(16:9) 864 x 480"),_("Widescreen (16:9)"));
		setSize->AppendRadioItem(ID_CANVASW720, wxT("(16:9) 1280 x 720"),_("Widescreen (16:9)"));
		setSize->AppendRadioItem(ID_CANVASW1080, wxT("(16:9) 1920 x 1080"),_("Widescreen (16:9)"));
		setSize->AppendRadioItem(ID_CANVASM768, wxT("(5:3) 1280 x 768"),_("Misc (5:3)"));
		setSize->AppendRadioItem(ID_CANVASM1200, wxT("(8:5) 1920 x 1200"),_("Misc (8:5)"));

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
		if (charControl->cd.eyeGlowType){
			size_t egt = charControl->cd.eyeGlowType;
			if (egt == EGT_NONE)
				charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
			else if (egt == EGT_DEATHKNIGHT)
				charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
			else
				charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
		}else{
			charControl->cd.eyeGlowType = EGT_DEFAULT;
			charGlowMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
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
		charMenu->AppendCheckItem(ID_SHEATHE, _("Sheathe Weapons\tCTRL+Z"));
		charMenu->Check(ID_SHEATHE, false);

		charMenu->AppendSeparator();
		charMenu->Append(ID_SAVE_EQUIPMENT, _("Save Equipment\tF5"));
		charMenu->Append(ID_LOAD_EQUIPMENT, _("Load Equipment\tF6"));
		charMenu->Append(ID_CLEAR_EQUIPMENT, _("Clear Equipment\tF9"));
		charMenu->AppendSeparator();
		charMenu->Append(ID_LOAD_SET, _("Load Item Set"));
		charMenu->Append(ID_LOAD_START, _("Load Start Outfit"));
		charMenu->Append(ID_LOAD_NPC_START, _("Load NPC Outfit"));
		charMenu->AppendSeparator();
		charMenu->Append(ID_MOUNT_CHARACTER, _("Mount a character..."));
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
		charMenu->Enable(ID_LOAD_NPC_START, false);
		charMenu->Enable(ID_MOUNT_CHARACTER, false);
		charMenu->Enable(ID_CHAR_RANDOMISE, false);

		wxMenu *effectsMenu = new wxMenu;
		effectsMenu->Append(ID_ENCHANTS, _("Apply Enchants"));
		effectsMenu->Append(ID_EQCREATURE_R, _("Creature Right-Hand"));
		effectsMenu->Append(ID_EQCREATURE_L, _("Creature Left-Hand"));
		if (gameVersion < VERSION_WOTLK) {
			effectsMenu->Append(ID_SPELLS, _("Spell Effects"));
			effectsMenu->Enable(ID_SPELLS, false);
			effectsMenu->Append(ID_SHADER_DEATH, _("Death Effect"));
			effectsMenu->Enable(ID_SHADER_DEATH, false);
		}

		// Options menu
		optMenu = new wxMenu;
		if (gameVersion < VERSION_WOTLK) {
			optMenu->AppendCheckItem(ID_USE_NPCSKINS, _("Use npc character skins"));
			optMenu->Check(ID_USE_NPCSKINS, false);
		}
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
	wxLogMessage(wxT("Initializing Objects..."));

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

  if(!GAMEDATABASE.initFromXML("wow6.xml"))
    LOG_ERROR << "Initializing failed !";
  else
    LOG_INFO << "Initializing succeed.";

	wxLogMessage(wxT("Initializing Databases..."));
	SetStatusText(wxT("Initializing Databases..."));
	initDB = true;

	if (!itemdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Item DB."));
	}

	if (!itemsparsedb.open()) {
		wxLogMessage(wxT("Error: Could not open the Item Sparse DB."));
	}

	SetStatusText(wxT("Initializing items.csv Databases..."));
	wxString filename = langName+SLASH+wxT("items.csv");
	wxLogMessage(wxT("Trying to open %s file (for updating locale files)"),filename.c_str());
	if (!wxFile::Exists(filename))
		DownloadLocaleFiles();
	if (!wxFile::Exists(filename))
		filename = locales[0]+SLASH+wxT("items.csv");
	if (wxFile::Exists(filename)) {
		items.open(filename);
	} else {
		wxLogMessage(wxT("Error: Could not find items.csv to load an item list from."));
	}

	if (!skyboxdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the SkyBox DB."));
	}

	if (!spellitemenchantmentdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Spell Item Enchanement DB."));
	}

	if (!itemvisualsdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Item Visuals DB."));
	}

	if (!animdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Animation DB."));
	}

	if (!modeldb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Creatures DB."));
	}

	if (!skindb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the CreatureDisplayInfo DB."));
	}

	if(!hairdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Hair Geoset DB."));
	}

	if(!chardb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Character DB."));
	}

	if(!racedb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Char Races DB."));
	}

	if(!classdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Char Classes DB."));
	}

	if(!facialhairdb.open()) {
		initDB = false;
		wxLogMessage(wxT("Error: Could not open the Char Facial Hair DB."));
	}

	if(!visualdb.open())
		wxLogMessage(wxT("Error: Could not open the ItemVisuals DB."));

	if(!effectdb.open())
		wxLogMessage(wxT("Error: Could not open the ItemVisualEffects DB."));

	if(!subclassdb.open())
		wxLogMessage(wxT("Error: Could not open the Item Subclasses DB."));

	if(!startdb.open())
		wxLogMessage(wxT("Error: Could not open the Start Outfit Sets DB."));
	//if(!helmetdb.open()) return false;

	if(!npcdb.open())
		wxLogMessage(wxT("Error: Could not open the Start Outfit NPC DB."));

	if(!npctypedb.open())
		wxLogMessage(wxT("Error: Could not open the Creature Type DB."));

	if(!camcinemadb.open())
		wxLogMessage(wxT("Error: Could not open the Cinema Camera DB."));

	if(!itemdisplaydb.open())
	{
		wxLogMessage(wxT("Error: Could not open the ItemDisplayInfo DB."));
	}
	else
	{
		items.cleanup(itemdisplaydb);
	}

	if(!setsdb.open())
	{
		wxLogMessage(wxT("Error: Could not open the Item Sets DB."));
	}
	else
	{
		setsdb.cleanup(items);
	}

	SetStatusText(wxT("Initializing Databases..."));

	sqlResult npc = GAMEDATABASE.sqlQuery("SELECT ID, DisplayID, CreatureTypeID, Name From Creature;");

	if(npc.valid)
	{
	  for(int i=0, imax=npc.values.size() ; i < imax ; i++)
	  {
	    NPCRecord rec(npc.values[i]);
	    npcs.npcs.push_back(rec);
	  }
	}

	if(spelleffectsdb.open())
		GetSpellEffects();
	else
		wxLogMessage(wxT("Error: Could not open the SpellVisualEffects DB."));

	wxLogMessage(wxT("Finished initiating database files."));
	SetStatusText(wxT("Finished initiating database files."));
}

void ModelViewer::InitDocking()
{
	wxLogMessage(wxT("Initializing GUI Docking."));
	
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
	wxLogMessage(wxT("Loading Session settings from: %s\n"), cfgPath.c_str());

	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(wxT("Global"),wxEmptyString, cfgPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);

	// Other session settings
	if (canvas) {
		pConfig->SetPath(wxT("/Session"));
		double c;
		// Background Colour
		pConfig->Read(wxT("bgR"), &c, 71.0/255);
		canvas->vecBGColor.x = c;
		pConfig->Read(wxT("bgG"), &c, 95.0/255);
		canvas->vecBGColor.y = c;
		pConfig->Read(wxT("bgB"), &c, 121.0/255);
		canvas->vecBGColor.z = c;
		
		// boolean vars
		pConfig->Read(wxT("RandomLooks"), &useRandomLooks, true);
		pConfig->Read(wxT("HideHelmet"), &bHideHelmet, false);
		pConfig->Read(wxT("ShowParticle"), &bShowParticle, true);
		pConfig->Read(wxT("ZeroParticle"), &bZeroParticle, true);
		pConfig->Read(wxT("Alternate"), &bAlternate, false);
		pConfig->Read(wxT("DBackground"), &canvas->drawBackground, false);
		pConfig->Read(wxT("BackgroundImage"), &bgImagePath, wxEmptyString);
		if (!bgImagePath.IsEmpty()) {
			canvas->LoadBackground(bgImagePath);
			//viewMenu->Check(ID_BACKGROUND, canvas->drawBackground);
		}
		

		// model file
		/*wxString modelfn;
		pConfig->Read(wxT("Model"), &modelfn);
		if (modelfn) {
			LoadModel(modelfn);
		}*/
	}

	wxDELETE(pConfig);
}

void ModelViewer::SaveSession()
{
	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(wxT("Global"), wxEmptyString, cfgPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);


	// Graphic / Video display settings
	pConfig->SetPath(wxT("/Graphics"));
	pConfig->Write(wxT("FSAA"), video.curCap.aaSamples);
	pConfig->Write(wxT("AccumulationBuffer"), video.curCap.accum);
	pConfig->Write(wxT("AlphaBits"), video.curCap.alpha);
	pConfig->Write(wxT("ColourBits"), video.curCap.colour);
	pConfig->Write(wxT("DoubleBuffer"), video.curCap.doubleBuffer);
	pConfig->Write(wxT("HWAcceleration"), video.curCap.hwAcc);
	pConfig->Write(wxT("SampleBuffer"), video.curCap.sampleBuffer);
	pConfig->Write(wxT("StencilBuffer"), video.curCap.stencil);
	pConfig->Write(wxT("ZBuffer"), video.curCap.zBuffer);

	pConfig->SetPath(wxT("/Session"));
	// Attempt at saving colour values as 3 byte hex - loss of accuracy from float
	//wxString temp(Vec3DToString(canvas->vecBGColor));

	if (canvas) {
		pConfig->Write(wxT("bgR"), (double)canvas->vecBGColor.x);
		pConfig->Write(wxT("bgG"), (double)canvas->vecBGColor.y);
		pConfig->Write(wxT("bgB"), (double)canvas->vecBGColor.z);
		
		// boolean vars
		pConfig->Write(wxT("RandomLooks"), useRandomLooks);
		pConfig->Write(wxT("HideHelmet"), bHideHelmet);
		pConfig->Write(wxT("ShowParticle"), bShowParticle);
		pConfig->Write(wxT("ZeroParticle"), bZeroParticle);
		pConfig->Write(wxT("Alternate"), bAlternate);

		pConfig->Write(wxT("DBackground"), canvas->drawBackground);
		if (canvas->drawBackground)
			pConfig->Write(wxT("BackgroundImage"), bgImagePath);
		else
			pConfig->Write(wxT("BackgroundImage"), wxEmptyString);

		// model file
		if (canvas->model)
			pConfig->Write(wxT("Model"), wxString(canvas->model->name.c_str(), wxConvUTF8));
	}

	// character details
	// equipment

	wxDELETE(pConfig);
}

void ModelViewer::LoadLayout()
{
	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(wxT("Global"), wxEmptyString, cfgPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);

	wxString layout;

	// Get layout data
	pConfig->SetPath(wxT("/Session"));
	pConfig->Read(wxT("Layout"), &layout);

	// if the layout data exists,  load it.
	if (!layout.IsNull() && !layout.IsEmpty()) {
		if (!interfaceManager.LoadPerspective(layout, false)){
			wxLogMessage(wxT("Error: Could not load the layout."));
		}
		else {
			// No need to display these windows on startup
			interfaceManager.GetPane(modelControl).Show(false);
			interfaceManager.GetPane(settingsControl).Show(false);

			// If character panel is showing,  hide it
			interfaceManager.GetPane(charControl).Show(isChar);
#ifndef	_LINUX // buggy
			interfaceManager.Update();
#endif
			wxLogMessage(wxT("Info: GUI Layout loaded from previous session."));
		}
	}

	wxDELETE(pConfig);
}

void ModelViewer::SaveLayout()
{
	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(wxT("Global"), wxEmptyString, cfgPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);

	pConfig->SetPath(wxT("/Session"));
	
	// Save GUI layout data
	wxString layout = interfaceManager.SavePerspective();
	pConfig->Write(wxT("Layout"), layout);

	wxLogMessage(wxT("Info: GUI Layout was saved."));

	wxDELETE(pConfig);
}


void ModelViewer::LoadModel(const wxString fn)
{
	if (!canvas || fn.IsEmpty())
		return;

	isModel = true;

	// check if this is a character model
	isChar = (fn.Lower().StartsWith(wxT("char")) || fn.Lower().StartsWith(wxT("alternate\\char")));

	Attachment *modelAtt = NULL;

	if (isChar) {
		modelAtt = canvas->LoadCharModel(fn);

		// error check
		if (!modelAtt) {
			wxLogMessage(wxT("Error: Failed to load the model - %s"), fn.c_str());
			return;
		}

		canvas->model->modelType = MT_CHAR;

	} else {
		modelAtt = canvas->LoadCharModel(fn); //  change it from LoadModel, don't sure it's right or not.

		// error check
		if (!modelAtt) {
			wxLogMessage(wxT("Error: Failed to load the model - %s"), fn.c_str());
			return;
		}

		canvas->model->modelType = MT_NORMAL;
	}

	// Error check,  make sure the model was actually loaded and set to canvas->model
	if (!canvas->model) {
		wxLogMessage(wxT("Error: [ModelViewer::LoadModel()]  Model* Canvas::model is null!"));
		return;
	}

	canvas->model->charModelDetails.isChar = isChar;
	
	viewMenu->Enable(ID_USE_CAMERA, canvas->model->hasCamera);
	if (canvas->useCamera && !canvas->model->hasCamera) {
		canvas->useCamera = false;
		viewMenu->Check(ID_USE_CAMERA, false);
	}
	
	// wxAUI
	interfaceManager.GetPane(charControl).Show(isChar);
	if (isChar) {
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
		charMenu->Enable(ID_LOAD_NPC_START, true);
		charMenu->Enable(ID_MOUNT_CHARACTER, true);
		charMenu->Enable(ID_CHAR_RANDOMISE, true);

		charControl->UpdateModel(modelAtt);
	} else {
		charControl->charAtt = modelAtt;
		charControl->model = (WoWModel*)modelAtt->model;

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
		charMenu->Enable(ID_LOAD_NPC_START, false);
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

	try {

	  stringstream ss;
	  ss << modelid;
	  string model = ss.str();

	  std::string query = "SELECT FileData.path, FileData.name, CreatureDisplayInfo.TextureVariation "
	      "FROM Creature "
	      "LEFT JOIN CreatureDisplayInfo ON Creature.DisplayID = CreatureDisplayInfo.ID "
	      "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
	      "LEFT JOIN FileData ON CreatureModelData.FileDataID = FileData.ID WHERE Creature.ID = " + model + ";" ;

	  std::cout << __FILE__ << " " << __FUNCTION__ << " " << query << std::endl;
	  sqlResult r = GAMEDATABASE.sqlQuery(query.c_str());

	  if(r.valid)
	  {
	    std::string modelname = r.values[0][0] + r.values[0][1];
	    wxString name(modelname.c_str());
	    LoadModel(name);
	    canvas->model->modelType = MT_NORMAL;

	    TextureGroup grp;
	    grp.tex[0] = wxString(r.values[0][2]);
	    grp.base = TEXTURE_GAMEOBJECT1;
	    grp.count = 1;
	    if (grp.tex[0].length() > 0)
	      animControl->AddSkin(grp);

	  }


	  /*
		CreatureSkinDB::Record modelRec = skindb.getBySkinID(modelid);
		int displayID = modelRec.getUInt(CreatureSkinDB::ExtraInfoID);
		// if the creature ID ISN'T a "NPC",  then load the creature model and skin it.
		if (displayID == 0) {
			
			unsigned int modelID = modelRec.getUInt(CreatureSkinDB::ModelID);
			CreatureModelDB::Record creatureModelRec = modeldb.getByID(modelID);
			
			wxString name(creatureModelRec.getString(CreatureModelDB::Filename));
			name = name.BeforeLast('.');
			name.Append(wxT(".m2"));

			LoadModel(name);
			canvas->model->modelType = MT_NORMAL;

			TextureGroup grp;
			int count = 0;
			for (size_t i=0; i<TextureGroup::num; i++) {
				wxString skin(modelRec.getString(CreatureSkinDB::Skin1 + i));
				
				grp.tex[i] = skin;
				if (skin.length() > 0)
					count++;
			}
			grp.base = TEXTURE_GAMEOBJECT1;
			grp.count = count;
			if (grp.tex[0].length() > 0) 
				animControl->AddSkin(grp);

		} else {
			isChar = true;
			NPCDB::Record rec = npcdb.getByNPCID(displayID);
			CharRacesDB::Record rec2 = racedb.getById(rec.getUInt(NPCDB::RaceID));
			
			wxString retval = rec.getString(NPCDB::Gender);
			wxString strModel = wxT("Character\\");

			if (gameVersion == 30100) {
				if (!retval.IsEmpty()) {
					strModel.append(rec2.getString(CharRacesDB::NameV310));
					strModel.append(wxT("\\Female\\"));
					strModel.append(rec2.getString(CharRacesDB::NameV310));
					strModel.append(wxT("Female.m2"));
				} else {
					strModel.append(rec2.getString(CharRacesDB::NameV310));
					strModel.append(wxT("\\Male\\"));
					strModel.append(rec2.getString(CharRacesDB::NameV310));
					strModel.append(wxT("Male.m2"));
				}
			} else {
				if (!retval.IsEmpty()) {
					strModel.append(rec2.getString(CharRacesDB::Name));
					strModel.append(wxT("\\Female\\"));
					strModel.append(rec2.getString(CharRacesDB::Name));
					strModel.append(wxT("Female.m2"));
				} else {
					strModel.append(rec2.getString(CharRacesDB::Name));
					strModel.append(wxT("\\Male\\"));
					strModel.append(rec2.getString(CharRacesDB::Name));
					strModel.append(wxT("Male.m2"));
				}
			}
			
			Attachment *modelAtt;
			modelAtt = canvas->LoadCharModel(strModel);
			canvas->model->modelType = MT_NPC;

			wxString fn(wxT("Textures\\Bakednpctextures\\"));
			fn.Append(rec.getString(NPCDB::Filename));
			charControl->UpdateNPCModel(modelAtt, displayID);
			charControl->customSkin = fn;

			charControl->RefreshModel();
			charControl->RefreshEquipment();

			menuBar->EnableTop(2, true);
			charMenu->Check(ID_SHOW_UNDERWEAR, true);
			charMenu->Check(ID_SHOW_EARS, true);
			charMenu->Check(ID_SHOW_HAIR, true);
			charMenu->Check(ID_SHOW_FACIALHAIR, true);
			charMenu->Check(ID_CHAREYEGLOW_NONE, false);
			charMenu->Check(ID_CHAREYEGLOW_DEFAULT, true);
			charMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, false);

			// ---

			animControl->UpdateModel(canvas->model);
			canvas->ResetView();
		}*/
	} catch (...) {}

	fileControl->UpdateInterface();

	// wxAUI
	interfaceManager.GetPane(charControl).Show(false);
	interfaceManager.Update();
}

void ModelViewer::LoadItem(unsigned int displayID)
{
	canvas->clearAttachments();
	//if (!isChar) // may memory leak
	//	wxDELETE(canvas->model);
	canvas->model = NULL;
	
	isModel = true;
	isChar = false;
	isWMO = false;

	try {
		ItemDisplayDB::Record modelRec = itemdisplaydb.getById(displayID);
		wxString name = modelRec.getString(ItemDisplayDB::Model);
		name = name.BeforeLast('.');
		name.Append(wxT(".M2"));
		wxLogMessage(wxT("LoadItem %d %s"), displayID, name.c_str());

		wxString fns[] = {
			wxT("Item\\ObjectComponents\\Ammo\\"),
			wxT("Item\\ObjectComponents\\Backpack\\"),
			wxT("Item\\ObjectComponents\\Battlestandards\\"),
			wxT("Item\\ObjectComponents\\Head\\"),
			wxT("Item\\ObjectComponents\\Quiver\\"),
			wxT("Item\\ObjectComponents\\Scroll\\"),
			wxT("Item\\ObjectComponents\\Shield\\"),
			wxT("Item\\ObjectComponents\\Shoulder\\"),
			wxT("Item\\ObjectComponents\\Waist\\"),
			wxT("Item\\ObjectComponents\\Weapon\\")
		};
		wxString fn;
		bool loaded = false;
		for(int i=0; i<10; i++) {
			fn = fns[i]+name;
			if (MPQFile::exists(fn)) {
				loaded = true;
				LoadModel(fn);
				break;
			}
		}

		if(!loaded){ // try head item specific stuff
			// sigh, head items have more crap to sort out
			wxString name = modelRec.getString(ItemDisplayDB::Model);
			name = name.substr(0, name.length()-4); // delete .mdx
			name.append(wxT("_"));

			// no model loaded, so try blood elf female
			name.append(wxT("bef.m2"));

			// finally try to load it again
			fn = fns[3]+name;
			if (MPQFile::exists(fn)) {
				LoadModel(fn);
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
		charMenu->Enable(ID_LOAD_NPC_START, false);
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
	wxLogMessage(wxT("Shuting down the program...\n"));

	video.render = false;

	// If we have a canvas (which we always should)
	// Stop rendering, give more power back to the CPU to close this sucker down!
	//if (canvas)
	//	canvas->timer.Stop();

	// Save current layout
	SaveLayout();

	// wxAUI stuff
	interfaceManager.UnInit();

	// Clear the MPQ archives.
	for (std::vector<MPQArchive*>::iterator it = archives.begin(); it != archives.end(); ++it) {
        (*it)->close();
		//archives.erase(it);
	}
	archives.clear();

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


/*
 * Try to init mpq archives to use them in the viewer.
 * See ArchiveInitStatus enum for error description
 */

ModelViewer::ArchiveInitStatus ModelViewer::InitMPQArchives()
{
	/*
	Debug code to test reading problems easily
	static bool first = true;
	if(first)
	{
	//	first = false;
		return ARCHIVEINITSTATUS_TOCREADING_ERROR;
	}
    */
	wxString path;

	// first clear current archive list to remove unexisting files
	archives.clear();
	for (size_t i=0; i<mpqArchives.GetCount(); i++) {
		if (wxFileName::FileExists(mpqArchives[i])) {
			archives.push_back(new MPQArchive(mpqArchives[i]));
		}
	}

	// Checks and logs the "TOC" version of the interface files that were loaded
	MPQFile f(wxT("Interface\\FrameXML\\FrameXML.TOC"));
	if (f.isEof()) {
		f.close();
		wxLogMessage(wxT("Unable to gather TOC data."));
		SetStatusText(wxT("Unable to gather TOC data."));
		return ARCHIVEINITSTATUS_WOWRUNNING_ERROR;
	}
	
	// reading and dumping first 100 characters 
	unsigned char tocfile[100];
	f.read(tocfile,99);
	wxString tocFileString((char *)tocfile, wxConvUTF8);
	wxLogMessage(wxT("TOC file size : %d"), f.getSize());
	wxLogMessage(wxT("Content read from file to get TOC number :"));
	wxLogMessage(tocFileString);
	f.seek(0);
	
	// Seek the TOC number by searching for the Interface Offset
	int tocstart = tocFileString.Find(wxT("## Interface: ")) + strlen("## Interface: ");
	f.seek(tocstart);
	unsigned char toc[6];
	memset(toc,'\0', 6);
	f.read(toc, 5);		// Read the 5-digit TOC
	f.close();

	// Check for Corrupted MPQ
	if (!isdigit(toc[0]) || !isdigit(toc[1]) || !isdigit(toc[2]) || !isdigit(toc[3]) || !isdigit(toc[4])) {
		wxLogMessage(wxT("MPQ files appear to be corrupted. TOC Read: \"%s\""), (char*)toc);
		return ARCHIVEINITSTATUS_TOCREADING_ERROR;
	}else{
		wxLogMessage(wxT("MPQ files do not appear to be corrupted."));
	}

	// Read Version Number
	SetStatusText(wxString((char *)toc, wxConvUTF8), 1);
	wxLogMessage(wxT("Loaded Content TOC: v%c.%c%c.%c%c"), toc[0], toc[1], toc[2], toc[3], toc[4]);
	if (wxString((char *)toc, wxConvUTF8) > wxT("99999")) {		// The 99999 should be updated if the TOC ever gets that high.
		return ARCHIVEINITSTATUS_TOCREADING_ERROR;
	}

	wxString info = wxT("WoW Model Viewer is designed to work with the latest version of World of Warcraft.\nYour version is supported, but support will be removed in the near future.\nYou may experience diminished capacity while working with WoW Model Viewer.\nPlease update your World of Warcraft client soon.");
	
	// Convert our TOC into an integer
	wxString stoc((char*)toc, wxConvUTF8);
	long itoc;
	stoc.ToLong(&itoc);

	// If we support more than 1 TOC version, place the others here.
	if ((itoc >= VERSION_WOTLK) && (itoc <=39999)) {			// WotLK TOCs
		wxLogMessage(wxT("Compatible Wrath of the Lich King Version Found."));
		//wxMessageBox(info, wxT("Compatible Wrath of the Lich King Version Found."),wxOK);
		gameVersion = itoc;
	} else if ((itoc >= VERSION_CATACLYSM) && (itoc < 49999)) {		// Cataclysm TOCs
		wxLogMessage(wxT("Compatible Cataclysm Version Found."));
		gameVersion = itoc;
		langOffset = 0;
	} else if ((itoc >= VERSION_MOP) && (itoc < 59999)) {		// Mists of Pandaria TOCs
		wxLogMessage(wxT("Compatible Mists of Pandaria Version Found."));
		gameVersion = itoc;
		langOffset = 0;
	} else { // else if not a supported edition...
		return ARCHIVEINITSTATUS_WOWVERSION_ERROR;
	}

	return ARCHIVEINITSTATUS_NOERROR;
}

wxString ModelViewer::Init()
{
	// Initiate other stuff
	wxLogMessage(wxT("Initializing Archives..."));
	SetStatusText(wxT("Initializing Archives..."));

	// more detail logging, this is so when someone has a problem and they send their log info
	wxLogMessage(wxT("Game Data Path: %s"), gamePath.c_str());
	wxLogMessage(wxT("Use Local Files: %s\n"), useLocalFiles ? wxT("true") : wxT("false"));
	
	isChar = false;
	isModel = false;

	// Load the games MPQ files into memory
	bool initArchivesSuccess = false;
	wxString initArchivesText;
	switch(InitMPQArchives())
	{
	case ARCHIVEINITSTATUS_TOCREADING_ERROR:
	{
		// TOC reading error : we propose to reset MPQ list and try to reload WoW again
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("There was a problem reading the TOC number.\nDo you want to try to reset your MPQ list ?"),
								wxT("Question"), wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
		size_t result = dial->ShowModal();
		if (result == wxID_YES){
			wxLogMessage(wxT("Trying to reset MPQ list to load WoW again."));
			// so let's go, reset
			settingsControl->ResetMPQ();
			// then load again
			if(InitMPQArchives() != ARCHIVEINITSTATUS_NOERROR) {
				initArchivesText = wxT("Your MPQ files appear to be unreadable \n(Unable to read TOC number). \nPlease re-download and re-install your WoW client.");
			}
			else {
				initArchivesSuccess = true;
			}
		}
		break;
	}
	case ARCHIVEINITSTATUS_WOWRUNNING_ERROR:
		initArchivesText = wxT("Could not read data from MPQ files.\nPlease make sure World of Warcraft is not running.");
		break;
	case ARCHIVEINITSTATUS_WOWVERSION_ERROR:
		initArchivesText = wxT("WoW Model Viewer does not support your version of World of Warcraft.\nPlease update your World of Warcraft client to be able to use it.");
		break;
	case ARCHIVEINITSTATUS_NOERROR:
	default:
		// by default, we consider there is no error, be carfeull if you put new value inside
		// ArchiveInitStatus enum
		initArchivesSuccess = true;
		break;
	}

	if(!initArchivesSuccess) {
		SetStatusText(initArchivesText.c_str());
		wxLogMessage(wxT("InitMPQArchives result: %s"), initArchivesText.c_str());
		return initArchivesText;
	}

	SetStatusText(wxT("Initializing File Control..."));
	fileControl->Init(this);

	if (charControl->Init() == false){
		return wxT("Error Initializing the Character Controls.");
	};

	return wxEmptyString;
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
	} else if (id==ID_SHOW_CHAR && isChar) {
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

	/*
	case ID_USE_ANTIALIAS:
		useAntiAlias = event.IsChecked();
		break;


	case ID_USE_HWACC:
		if (event.IsChecked() == true)
			disableHWAcc = false;
		else
			disableHWAcc = true;
		break;
	*/

	case ID_USE_ENVMAP:
		video.useEnvMapping = event.IsChecked();
		break;

	case ID_SHOW_MASK:
		video.useMasking = !video.useMasking;

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
			if (saveDialog.ShowModal()==wxID_OK) {
				SaveChar(saveDialog.GetPath());
			}
		}
		break;
	case ID_LOAD_CHAR:
		{
			wxFileDialog loadDialog(this, wxT("Load character"), wxEmptyString, wxEmptyString, wxT("Character files (*.chr)|*.chr"), wxFD_OPEN|wxFD_FILE_MUST_EXIST);
			if (loadDialog.ShowModal()==wxID_OK) {
				wxLogMessage(wxT("\nLoading character from a save file: %s\n"), loadDialog.GetPath().c_str());
				for (size_t i=0; i<NUM_CHAR_SLOTS; i++)
					charControl->cd.equipment[i] = 0;
				
				LoadChar(loadDialog.GetPath());
			}
		}
		fileControl->UpdateInterface();
		break;

	case ID_IMPORT_CHAR:
		{
			wxTextEntryDialog dialog(this, wxT("Please paste in the URL to the character you wish to import."), wxT("Please enter text"), armoryPath, wxOK | wxCANCEL | wxCENTRE, wxDefaultPosition);
			if (dialog.ShowModal() == wxID_OK){
				armoryPath = dialog.GetValue();
				wxLogMessage(wxT("\nImporting character from the Armory: %s\n"), armoryPath.c_str());
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
#ifndef _MINGW
				ofstream f(fn.fn_str(), ios_base::out|ios_base::trunc);
#else
				ofstream f(fn.char_str(), ios_base::out|ios_base::trunc);
#endif
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
#ifndef _MINGW
				ifstream f(fn.fn_str());
#else
				ifstream f(fn.char_str());
#endif
				
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

	if (id == ID_ENCHANTS) {
		// Currently, only support enchanting character weapons
		if (isChar)
			enchants->Display();

	} else if (id == ID_SPELLS) {
		wxSingleChoiceDialog spellsDialog(this, wxT("Choose"), wxT("Select a Spell Effect"), spelleffects);

		if (spellsDialog.ShowModal() == wxID_OK) {
			// TODO: Finish adding support for spells.
		}

	} else if (id == ID_EQCREATURE_R) { // R for righthand
		WoWModel *m = static_cast<WoWModel*>(canvas->root->model);
		if (!m)
			m = static_cast<WoWModel*>(canvas->model);

		// make sure m is a valid pointer to a model
		if (m) {
			// This is an error check to make sure the creature can be equipped.
			for(size_t k=0; k<m->ATT_MAX; k++){
				if (m->attLookup[k] == ATT_RIGHT_PALM) {
					SelectCreatureItem(CS_HAND_RIGHT, 0, charControl, canvas);
					break;
				}
			}
		}

	} else if (id == ID_EQCREATURE_L) { // L for lefthand
		WoWModel *m = static_cast<WoWModel*>(canvas->root->model);
		if (!m)
			m = static_cast<WoWModel*>(canvas->model);

		// make sure m is a valid pointer to a model
		if (m) {
			// This is an error check to make sure the creature can be equipped.
			for(size_t k=0; k<m->ATT_MAX; k++){
				if (m->attLookup[k] == ATT_LEFT_PALM) {
					SelectCreatureItem(CS_HAND_LEFT, 0, charControl, canvas);
					break;
				}
			}
		}
	} else if (id==ID_SHADER_DEATH) {
		//Shader test("Shaders\\Pixel\\FFXDeath.bls");
		//test.Load();
	}
}

Vec3D ModelViewer::DoSetColor(const Vec3D &defColor)
{
	wxColourData data;
	wxColour dcol((unsigned char)(defColor.x*255.0f), (unsigned char)(defColor.y*255.0f), (unsigned char)(defColor.z*255.0f));
	data.SetChooseFull(true);
	data.SetColour(dcol);
	   
	wxColourDialog dialog(this, &data);

	if (dialog.ShowModal() == wxID_OK) {
		wxColour col = dialog.GetColourData().GetColour();
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
		mpqArchives.Clear();
	}

	CASCFOLDER.init(gamePath.c_str());

  CASCFile TOCFile("Interface\\FrameXML\\FrameXML.TOC");

  if(!TOCFile.open())
  {
    LOG_ERROR << "Opening TOC file in folder " << CASCFOLDER.folder().c_str() << " failed.";
  }
  else
  {
    DWORD dwBytesRead;
    char buffer[100];
    dwBytesRead = TOCFile.read(buffer,100);

    if(dwBytesRead !=0)
    {
      std::string tocString = buffer;
      // Seek the TOC number by searching for the Interface Offset
      int tocstart = tocString.find("## Interface: ") + strlen("## Interface: ");
      tocString = tocString.substr(tocstart, tocstart+5);
      tocString.resize(5);
      TOCFile.close();
      SetStatusText(wxString(tocString), 1);

      mpqArchives.Add(wxString("fakeMPQFile"));
      langName = CASCFOLDER.locale();
      gameVersion = atoi(tocString.c_str());
    }
    else
    {
      LOG_ERROR << "FAIL to read TOC content from file.";
    }
  }


  if(!CASCFOLDER.hStorage)
  {
    wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("Fatal Error: Could not load your World of Warcraft Data folder."), wxT("World of Warcraft Not Found"), wxOK | wxICON_ERROR);
    dial->ShowModal();
    return;
  }

	// auto search for MPQ Archives
	//if (mpqArchives.GetCount() == 0) {
	//	searchMPQs(true);
	//}


	/*

	// if we can't search any mpqs
	if (mpqArchives.GetCount() == 0) {
		wxLogMessage(wxT("World of Warcraft Data Directory Not Found. Returned GamePath: %s"), gamePath.c_str());
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("Fatal Error: Could not find your World of Warcraft Data folder."), wxT("World of Warcraft Not Found"), wxOK | wxICON_ERROR);
		dial->ShowModal();
		return;
	}

    if (langID == -1 && WXSIZEOF(langNames) > 0) {
        // the arrays should be in sync
        wxCOMPILE_TIME_ASSERT(WXSIZEOF(langNames) == WXSIZEOF(langIds), LangArraysMismatch);
        langID = wxGetSingleChoiceIndex(wxT("Please select a language:"), wxT("Language"), WXSIZEOF(langNames), langNames);
	}

	if (langID == -1) {
		wxLogMessage(wxT("World of Warcraft Data Directory Not Found. Returned GamePath: %s"), gamePath.c_str());
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("Fatal Error: Could not find your World of Warcraft Data folder."), wxT("World of Warcraft Not Found"), wxOK | wxICON_ERROR);
		dial->ShowModal();
		return;
	}

	// initial interfaceID as langID
	if (interfaceID == 0 && langID >= 0) {
		interfaceID = langID;
	}

	// initial langOffset
	if (langOffset == -1) {
		// gameVersion VERSION_CATACLYSM remove all other language strings
		if (gameVersion >= VERSION_CATACLYSM)
			langOffset = 0;
		else
			langOffset = langID;
	}
*/
  SetStatusText(wxString(CASCFOLDER.locale()), 2);

  InitDatabase();

    // Error check
    if (!initDB) {
      wxMessageBox(wxT("Some DBC files could not be loaded.  These files are vital to being able to render models correctly.\nPlease make sure you are loading the 'Locale-xxxx.MPQ' file.\nFile list has been disabled until you are able to correct this problem."), wxT("DBC Error"));
      //fileControl->Disable();
      SetStatusText(wxT("Some DBC files could not be loaded."));
    } else {
      isWoWLoaded = true;
      SetStatusText(wxT("Initial WoW Done."));
      fileMenu->Enable(ID_LOAD_WOW, false);
    }

    SetStatusText(wxT("Initializing File Control..."));
    fileControl->Init(this);

    if (charControl->Init() == false){
       SetStatusText(wxT("Error Initializing the Character Controls."));
    };

	// load our World of Warcraft mpq archives
	/*wxString initvar = Init();
	if (initvar != wxEmptyString){
		wxString info = wxT("Fatal Error: ") + initvar;
		wxLogMessage(info);
		wxMessageDialog *dial = new wxMessageDialog(NULL, info, wxT("Fatal Error"), wxOK | wxICON_ERROR);
		dial->ShowModal();
		//Close(true);
		return;
	}

	// Load user skins 
	gUserSkins.LoadFile(cfgPath.BeforeLast(SLASH) + SLASH + wxT("Skins.txt"));
	if (!gUserSkins.Loaded())
		wxLogMessage(wxT("Warning: Failed to load user skins"));

	// Initial Databases like dbc, db2, csvs

	*/
}

void ModelViewer::OnCharToggle(wxCommandEvent &event)
{
	int ID = event.GetId();
	if (ID == ID_VIEW_NPC)
		charControl->selectNPC(UPDATE_NPC);
	if (ID == ID_VIEW_ITEM)
		charControl->selectItem(UPDATE_SINGLE_ITEM, -1, -1);
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
			wxFileDialog dialog(this, wxT("Load Background"), dir.GetPath(wxPATH_GET_VOLUME), wxEmptyString, wxT("Bitmap Images (*.bmp)|*.bmp|TGA Images (*.tga)|*.tga|Jpeg Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png|AVI Video file(*.avi)|*.avi"));
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

			for (LightSkyBoxDB::Iterator it=skyboxdb.begin();  it!=skyboxdb.end(); ++it) {
				wxString str(it->getString(LightSkyBoxDB::Name));
				str = str.BeforeLast('.');
				str.Append(wxT(".m2"));

				if (skyboxes.Index(str, false) == wxNOT_FOUND)
					skyboxes.Add(str);
			}
			skyboxes.Add(wxT("World\\Outland\\PassiveDoodads\\SkyBox\\OutlandSkyBox.m2"));
			skyboxes.Sort();


			wxSingleChoiceDialog skyDialog(this, wxT("Choose"), wxT("Select a Sky Box"), skyboxes);
			if (skyDialog.ShowModal() == wxID_OK && skyDialog.GetStringSelection() != wxEmptyString) {
				canvas->skyModel = new WoWModel(wxString(skyDialog.GetStringSelection()), false);
				canvas->sky->model = canvas->skyModel;
			}
		}
		
		canvas->drawSky = event.IsChecked();
	}
}

void ModelViewer::SaveChar(wxString fn)
{
	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream f(fn.fn_str(), ios_base::out|ios_base::trunc);
#else
	ofstream f(fn.char_str(), ios_base::out|ios_base::trunc);
#endif
	f << canvas->model->name << endl;
	f << charControl->cd.race << " " << charControl->cd.gender << endl;
	f << charControl->cd.skinColor << " " << charControl->cd.faceType << " " << charControl->cd.hairColor << " " << charControl->cd.hairStyle << " " << charControl->cd.facialHair << " " << charControl->cd.facialColor  << " " << charControl->cd.eyeGlowType << endl;
	for (size_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f << charControl->cd.equipment[i] << endl;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if ((charControl->cd.equipment[CS_TABARD] == 5976) || (charControl->cd.equipment[CS_TABARD] == 69209) || (charControl->cd.equipment[CS_TABARD] == 69210)) {
		f << charControl->td.Background << " " << charControl->td.Border << " " << charControl->td.BorderColor << " " << charControl->td.Icon << " " << charControl->td.IconColor << endl;
	}

	f << endl;
	f.close();
}

void ModelViewer::LoadChar(wxString fn)
{
	std::string modelname;
	// FIXME: ifstream is not compitable with multibyte path name
#ifndef _MINGW
	ifstream f(fn.fn_str());
#else
	ifstream f(fn.char_str());
#endif
	
	f >> modelname; // model name

	// Clear the existing model
	if (isWMO) {
		//canvas->clearAttachments();
		wxDELETE(canvas->wmo);
		canvas->wmo = NULL;
	} else if (isModel) {
		canvas->clearAttachments();
		//if (!isChar) // may memory leak
		//	wxDELETE(canvas->model);
		canvas->model = NULL;
	}

	// Load the model
	LoadModel(wxString(modelname.c_str(), wxConvUTF8));
	canvas->model->modelType = MT_CHAR;

	f >> charControl->cd.race >> charControl->cd.gender; // race and gender
	f >> charControl->cd.skinColor >> charControl->cd.faceType >> charControl->cd.hairColor >> charControl->cd.hairStyle >> charControl->cd.facialHair >> charControl->cd.facialColor;
	
	// If Eyeglow is in char file...
	if (f.peek() != wxT('\n')){
		f >> charControl->cd.eyeGlowType;
	}else{
		// Otherwise, default to this value
		charControl->cd.eyeGlowType = EGT_DEFAULT;
	}

	while (!f.eof()) {
		for (size_t i=0; i<NUM_CHAR_SLOTS; i++) {
			f >> charControl->cd.equipment[i];
		}
		break;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if (((charControl->cd.equipment[CS_TABARD] == 5976) || (charControl->cd.equipment[CS_TABARD] == 69209) || (charControl->cd.equipment[CS_TABARD] == 69210)) && !f.eof()) {
		f >> charControl->td.Background >> charControl->td.Border >> charControl->td.BorderColor >> charControl->td.Icon >> charControl->td.IconColor;
		charControl->td.showCustom = true;
	}

	f.close();

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
	charMenu->Enable(ID_LOAD_NPC_START, true);
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
/*
text = new wxStaticText(this, wxID_ANY, wxT("Developers:		UfoZ, Darjk\n\n\
Pioneers:		UfoZ,  Linghuye,  nSzAbolcs\n\n\
Translators:	Culhag (French), Die_Backe (Deutsch)\n\n\
Developed Using:\n\
wxWidgets(2.6.3), wxAUI(0.9.2), OpenGL, zlib,\n\
CxImage(5.99c), MPQLib, DDSLib, GLEW(1.3.3)\n\n\
Build Info:\nVersion 0.5 compiled using Visual C++ 7.1 (2003) for\n\
Windows 98\\ME\\2000\\XP on 17th December 2006\n\n\
*/

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

void ModelViewer::DownloadLocaleFiles()
{
	wxString trunk = wxT("http://wowmodelviewer.net/update/");
	wxString lang = langName;
	if (lang == wxT("enGB"))
		lang = wxT("enUS");

	wxString msg = wxString::Format(_("Would you like to download %s locale files?"), lang.c_str());
	if (wxMessageBox(msg, _("Update Locale Files"), wxYES_NO) == wxYES) {
		wxString csvs[] = {wxT("items.csv"), wxT("npcs.csv")};
		if (!wxDirExists(lang))
			wxMkdir(lang);
		for(size_t i=0; i<WXSIZEOF(csvs); i++) {
			wxString items_csv = trunk + lang + wxT("/") + csvs[i];
			wxLogMessage(wxT("Trying to get %s ... "),items_csv.c_str());
			wxURL items_url(items_csv);
			if(items_url.GetError() == wxURL_NOERR) {
				wxInputStream *stream = items_url.GetInputStream();
				if (stream) {
					wxLogMessage(wxT("SUCCESS"));
					wxFFileOutputStream f(lang + SLASH + csvs[i], wxT("w+b"));
					wxString data;
					char buffer[1024];
					while(!stream->Eof()) {
						stream->Read(buffer, sizeof(buffer));
						f.Write(buffer, stream->LastRead());
					}
					//delete stream;
					f.Close();
				} else {
				    wxLogMessage(wxT("ERROR"));
				}
			} else {
			    wxLogMessage(wxT("wxURL failed: ")+items_csv);
			}
		}
	}
}

void ModelViewer::OnCheckForUpdate(wxCommandEvent &event)
{
  wxExecute("UpdateManager.exe",wxEXEC_SYNC);
  /*
	wxString trunk = wxT("http://wowmodelviewer.net/update/");
	DownloadLocaleFiles();

	wxURL url(trunk + wxT("latest.txt"));
	if(url.GetError() == wxURL_NOERR) {
		wxInputStream *stream = url.GetInputStream();
		if(!stream)
			return;
		// here, just for example, I read 1024 bytes. You should read what you need...
		char buffer[1024];
		stream->Read(&buffer, sizeof(buffer));
		
		// Sort the data
		wxString data(buffer, wxConvUTF8);
		wxString version = data.BeforeFirst('\n');
		wxString downloadURL = data.AfterFirst('\n');
		int Compare = (int)version.find(wxString(GLOBALSETTINGS.appVersion()));

		wxLogMessage("Update Data:\nCurrent Version: \"%s\"\nRecorded Version \"%s\"\nURL Download: %s\nComparison Result: %i",GLOBALSETTINGS.appVersion().c_str(), version.c_str(), downloadURL.c_str(), Compare);

		if (Compare == 0) {
			wxMessageBox(_("You have the most up-to-date version."), _("Update Check"));
		} else {
			wxString msg = wxString::Format(wxT("The most recent version is : %s\nWould you like to go to the download page?"), version.c_str());
			if (wxMessageBox(msg, _("Update Check"), wxYES_NO, this) == wxYES)
				wxLaunchDefaultBrowser(wxString(downloadURL.ToUTF8(), wxConvUTF8));
		}

		// Create a string from the data that was received... (?)
		//wxString webversion;
		//wxMessageBox(wxString::Format("%s",buffer));

		delete stream;
	}else{
		wxMessageBox(wxT("Error retrieving update information.\nPlease try again later."), wxT("Update Error"));
	}
	*/
}

void ModelViewer::OnCanvasSize(wxCommandEvent &event)
{
	int id = event.GetId();
	uint32 sizex = 0;
	uint32 sizey = 0;
	
	if (id == ID_CANVASS120) {
		sizex = 120;
		sizey = 120;
	} else if (id == ID_CANVASS512) {
		sizex = 512;
		sizey = 512;
	} else if (id == ID_CANVASS1024) {
		sizex = 1024;
		sizey = 1024;
	} else if (id == ID_CANVASF480) {
		sizex = 640;
		sizey = 480;
	} else if (id == ID_CANVASF600) {
		sizex = 800;
		sizey = 600;
	} else if (id == ID_CANVASF768) {
		sizex = 1024;
		sizey = 768;
	} else if (id == ID_CANVASF864) {
		sizex = 1152;
		sizey = 864;
	} else if (id == ID_CANVASF1200) {
		sizex = 1600;
		sizey = 1200;
	} else if (id == ID_CANVASW480) {
		sizex = 864;
		sizey = 480;
	} else if (id == ID_CANVASW720) {
		sizex = 1280;
		sizey = 720;
	} else if (id == ID_CANVASW1080) {
		sizex = 1920;
		sizey = 1080;
	} else if (id == ID_CANVASM768) {
		sizex = 1280;
		sizey = 768;
	} else if (id == ID_CANVASM1200) {
		sizex = 1900;
		sizey = 1200;
	}

	if (sizex > 0 && sizey > 0) {
		int curx=0, cury=0;
		int difx=0, dify=0;

		canvas->GetClientSize(&curx, &cury);
		difx = sizex - curx;
		dify = sizey - cury;
		// if the window is already large enough,  just shrink the canvas
		//if (difx <= 0 && dify <= 0 && sizex < 800 && sizey < 600) {
		//	canvas->SetClientSize(sizex, sizey);
		//} else {
			GetClientSize(&curx, &cury);
			SetSize((curx + difx), (cury + dify));
		//}
	}
}

void ModelViewer::ModelInfo()
{
	if (!canvas->model)
		return;
	WoWModel *m = canvas->model;
	wxString fn = wxT("ModelInfo.xml");
	// FIXME: ofstream is not compitable with multibyte path name
#ifndef _MINGW
	ofstream xml(fn.fn_str(), ios_base::out | ios_base::trunc);
#else
	ofstream xml(fn.char_str(), ios_base::out | ios_base::trunc);
#endif

	if (!xml.is_open()) {
		wxLogMessage(wxT("Error: Unable to open file '%s'. Could not export model."), fn.c_str());
		return;
	}

	MPQFile f(m->modelname);
	if (f.isEof() || (f.getSize() < sizeof(ModelHeader))) {
		wxLogMessage(wxT("Error: Unable to load model: [%s]"), m->modelname.c_str());
		// delete this; //?
		xml.close();
		f.close();
		return;
	}

	MPQFile g(m->lodname);
	if (g.isEof() || (g.getSize() < sizeof(ModelView))) {
		wxLogMessage(wxT("Error: Unable to load Lod: [%s]"), m->lodname.c_str());
		// delete this; //?
		xml.close();
		f.close();
		g.close();
		return;
	}
	ModelView *view = (ModelView*)(g.getBuffer());

	xml << "<m2>" << endl;
	xml << "  <info>" << endl;
	xml << "    <modelname>" <<m->modelname.c_str()  << "</modelname>" << endl;
	xml << "  </info>" << endl;
	xml << "  <header>" << endl;
//	xml << "    <id>" << m->header.id << "</id>" << endl;
	xml << "    <nameLength>" << m->header.nameLength << "</nameLength>" << endl;
	xml << "    <nameOfs>" << m->header.nameOfs << "</nameOfs>" << endl;
	xml << "    <name>" << f.getBuffer()+m->header.nameOfs << "</name>" << endl;
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
			// subAnimID
			wxString strName;
			try {
				AnimDB::Record rec = animdb.getByAnimID(m->anims[i].animID);
				strName = rec.getString(AnimDB::Name);
			} catch (AnimDB::NotFound) {
				strName = wxT("???");
			}
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

	xml << "  <BoneLookups size=\"" << m->header.nBoneLookup << "\">" << endl;
	uint16 *boneLookup = (uint16 *)(f.getBuffer() + m->header.ofsBoneLookup);
	for(size_t i=0; i<m->header.nBoneLookup; i++) {
		xml << "    <BoneLookup id=\"" << i << "\">" << boneLookup[i] << "</BoneLookup>" << endl;
	}
	xml << "  </BoneLookups>" << endl;

	xml << "  <KeyBoneLookups size=\"" << m->header.nKeyBoneLookup << "\">" << endl;
	for(size_t i=0; i<m->header.nKeyBoneLookup; i++)
		xml << "    <KeyBoneLookup id=\"" << i << "\">" << m->keyBoneLookup[i] << "</KeyBoneLookup>" << endl;
	xml << "  </KeyBoneLookups>" << endl;

	xml << "  </SkeletonAndAnimation>" << endl;

	xml << "  <GeometryAndRendering>" << endl;

	xml << "  <Vertices size=\"" << m->header.nVertices << "\">" << endl;
	ModelVertex *verts = (ModelVertex*)(f.getBuffer() + m->header.ofsVertices);
	for(uint32 i=0; i<m->header.nVertices; i++) {
		xml << "    <Vertice id=\"" << i << "\">" << endl;
		xml << "      <pos>" << verts[i].pos << "</pos>" << endl; // TODO
		xml << "    </Vertice>" << endl;
	}
	xml << "  </Vertices>" << endl; // TODO
	xml << "  <Views>" << endl;

	xml << "  <Indices size=\"" << view->nIndex << "\">" << endl;
	xml << "  </Indices>" << endl; // TODO
	xml << "  <Triangles size=\""<< view->nTris << "\">" << endl;
	xml << "  </Triangles>" << endl; // TODO
	xml << "  <Properties size=\"" << view->nProps << "\">" << endl;
	xml << "  </Properties>" << endl; // TODO
	xml << "  <Subs size=\"" << view->nSub << "\">" << endl;
	xml << "  </Subs>" << endl; // TODO

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
			xml << "      <texName>" << m->TextureList[p.tex] << "</texName>" << endl;
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

	ModelTexUnit *tex = (ModelTexUnit*)(g.getBuffer() + view->ofsTex);
	xml << "	<TexUnits size=\"" << view->nTex << "\">" << endl;
	for (size_t i=0; i<view->nTex; i++) {
		xml << "	  <TexUnit id=\"" << i << "\">" << endl;
		xml << "      <flags>" << tex[i].flags << "</flags>" << endl;
		xml << "      <shading>" << tex[i].shading << "</shading>" << endl;
		xml << "      <op>" << tex[i].op << "</op>" << endl;
		xml << "      <op2>" << tex[i].op2 << "</op2>" << endl;
		xml << "      <colorIndex>" << tex[i].colorIndex << "</colorIndex>" << endl;
		xml << "      <flagsIndex>" << tex[i].flagsIndex << "</flagsIndex>" << endl;
		xml << "      <texunit>" << tex[i].texunit << "</texunit>" << endl;
		xml << "      <mode>" << tex[i].mode << "</mode>" << endl;
		xml << "      <textureid>" << tex[i].textureid << "</textureid>" << endl;
		xml << "      <texunit2>" << tex[i].texunit2 << "</texunit2>" << endl;
		xml << "      <transid>" << tex[i].transid << "</transid>" << endl;
		xml << "      <texanimid>" << tex[i].texanimid << "</texanimid>" << endl;
		xml << "	  </TexUnit>" << endl;
	}
	xml << "	</TexUnits>" << endl;

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

	ModelTextureDef *texdef = (ModelTextureDef*)(f.getBuffer() + m->header.ofsTextures);
	xml << "	<Textures size=\"" << m->header.nTextures << "\">" << endl;
	for(size_t i=0; i<m->header.nTextures; i++) {
		xml << "	  <Texture id=\"" << i << "\">" << endl;
		xml << "      <type>" << texdef[i].type << "</type>" << endl;
		xml << "      <flags>" << texdef[i].flags << "</flags>" << endl;
		//xml << "      <nameLen>" << texdef[i].nameLen << "</nameLen>" << endl;
		//xml << "      <nameOfs>" << texdef[i].nameOfs << "</nameOfs>" << endl;
		if (texdef[i].type == TEXTURE_FILENAME)
			xml << "		<name>" << f.getBuffer()+texdef[i].nameOfs  << "</name>" << endl;
		xml << "	  </Texture>" << endl;
	}
	xml << "	</Textures>" << endl;

	xml << "  <TexLookups size=\"" << m->header.nTexLookup << "\">" << endl;
	uint16 *texLookup = (uint16 *)(f.getBuffer() + m->header.ofsTexLookup);
	for(size_t i=0; i<m->header.nTexLookup; i++) {
		xml << "    <TexLookup id=\"" << i << "\">" << texLookup[i] << "</TexLookup>" << endl;
	}
	xml << "  </TexLookups>" << endl;

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
	int16 *attachLookup = (int16 *)(f.getBuffer() + m->header.ofsAttachLookup);
	for(size_t i=0; i<m->header.nAttachLookup; i++) {
		xml << "    <AttachLookup id=\"" << i << "\">" << attachLookup[i] << "</AttachLookup>" << endl;
	}
	xml << "  </AttachLookups>" << endl;

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
		xml << "    <TextureList id=\"" << i << "\">" << m->TextureList[i] << "</TextureList>" << endl;
	}
	xml << "  </TextureLists>" << endl;

	xml << "</m2>" << endl;
	xml.close();
	f.close();
	g.close();
}

void DiscoveryNPC()
{
	if (skindb.size() == 0)
		return;
	wxString name, ret;
	// 1. from creaturedisplayinfo.dbc
	for (CreatureSkinDB::Iterator it = skindb.begin(); it != skindb.end(); ++it) {
		int npcid = it->getUInt(CreatureSkinDB::ExtraInfoID);
		int id = it->getUInt(CreatureSkinDB::SkinID);
		if (npcid == 0)
			continue;
		if (!npcs.avaiable(id)) {
			name.Printf(wxT("Skin%d"), id);
			ret = npcs.addDiscoveryId(id, name);
		}
	}
	// 2. from creaturedisplayinfoextra.dbc
	wxLogMessage(wxT("Discovery npc done."));
	g_modelViewer->SetStatusText(wxT("Discovery npc done."));

	g_modelViewer->fileMenu->Enable(ID_FILE_DISCOVERY_NPC, false);
}

void DiscoveryItem()
{
	if (setsdb.size() == 0)
		return;
	wxString name, ret;
	items.cleanupDiscovery();
	wxFFileOutputStream fs (wxT("discoveryitems.csv"));

	if (!fs.IsOk()) {
		wxLogMessage(wxT("Error: Unable to open file 'discoveryitems.csv'. Could not discovery item."));
		return;
	}

    wxTextOutputStream f (fs);

	// 1. from itemsets.dbc
	for (ItemSetDB::Iterator it = setsdb.begin(); it != setsdb.end(); ++it) {
		for(size_t i=0; i<ItemSetDB::NumItems; i++) {
			int id;
			if (gameVersion >= VERSION_CATACLYSM)
				id = it->getUInt(ItemSetDB::ItemIDBaseV400+i);
			else
				id = it->getUInt(ItemSetDB::ItemIDBase+i);
			if (id == 0)
				continue;
			if (!items.avaiable(id)) {
				if (langID == 0)
					name = it->getString(ItemSetDB::Name);
				else
					name.Printf(wxT("Set%d"), it->getUInt(ItemSetDB::SetID));
				ret = items.addDiscoveryId(id, name);
				if (!ret.IsEmpty())
					f << ret << endl;
			}
		}
	}
	// 2. from item.dbc
	for (ItemDB::Iterator it = itemdb.begin(); it != itemdb.end(); ++it) {
		int id = it->getUInt(ItemDB::ID);
		if (!items.avaiable(id)) {
			name.Printf(wxT("Item%d"), id);
			ret = items.addDiscoveryId(id, name);
			if (!ret.IsEmpty())
				f << ret << endl;
		}
	}

	// 4. from model dir
	// 5. from blp dir
	wxLogMessage(wxT("Discovery item done."));
	g_modelViewer->SetStatusText(wxT("Discovery item done."));
	fs.Close();
	items.cleanup(itemdisplaydb);
	g_modelViewer->fileMenu->Enable(ID_FILE_DISCOVERY_ITEM, false);
}

// Other things to export...
void ModelViewer::OnExportOther(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id == ID_FILE_MODEL_INFO) {
		ModelInfo();
	} else if (id == ID_FILE_DISCOVERY_ITEM) {
		DiscoveryItem();
		fileMenu->Enable(ID_FILE_DISCOVERY_ITEM, false);
	} else if (id == ID_FILE_DISCOVERY_NPC) {
		DiscoveryNPC();
		fileMenu->Enable(ID_FILE_DISCOVERY_NPC, false);
	}
}

void ModelViewer::UpdateControls()
{
	if (!canvas || !canvas->model || !canvas->root)
		return;

	if (canvas->model->modelType == MT_NPC)
		charControl->RefreshNPCModel();
	else if (canvas->model->modelType == MT_CHAR)
		charControl->RefreshModel();
	
	modelControl->RefreshModel(canvas->root);
}

void ModelViewer::ImportArmoury(wxString strURL)
{
  CharInfos * result = NULL;

  std::string url = strURL.ToAscii();
  Iterator<ImporterPlugin> pluginIt(PLUGINMANAGER);
  for(pluginIt.begin(); !pluginIt.ended() ; pluginIt++)
  {
    ImporterPlugin * plugin = *pluginIt;
    if(plugin->acceptURL(url))
    {
      result = plugin->importChar(url);
    }
  }

	if(result)
	{
	  CharRacesDB::Record racer = racedb.getById(result->raceId);
	  if (gameVersion == 30100)
	    result->race = racer.getString(CharRacesDB::NameV310);
	  else
	    result->race = racer.getString(CharRacesDB::Name);
	  //wxLogMessage(wxT("RaceID: %i, Race: %s\n          GenderID: %i, Gender: %s"),raceID,race,genderID,gender);

		// Load the model
		wxString strModel = wxT("Character\\") + result->race + MPQ_SLASH + result->gender + MPQ_SLASH + result->race + result->gender + wxT(".m2");
		LoadModel(strModel);
		if (!g_canvas->model)
			return;

		if (result->hasTransmogGear == true)
		{
			wxLogMessage(wxT("Transmogrified Gear was found. Switching items..."));
			wxMessageBox(wxT("We found Transmogrified gear on your character. The items your character is wearing will be exchanged for the items they look like."),wxT("Transmog Notice"));
		}

		// Update the model
		g_charControl->cd = result->cd;
		g_charControl->RefreshModel();
		g_charControl->RefreshEquipment();

		delete result;
	}
	else
	{
		wxLogMessage(wxT("There were errors gathering the Armory page."));
		wxMessageBox(wxT("There was an error when gathering the Armory data.\nPlease try again later."),wxT("Armory Error"));

	}
}
