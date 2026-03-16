#include "modelviewer.h"
#include "AnimationExportChoiceDialog.h"
#include <wx/aboutdlg.h>
#include <wx/busyinfo.h>
#include <wx/colordlg.h>
#include <wx/colour.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include "Attachment.h"
#include "app.h"
#include "Bone.h"
#include "CASCFile.h"
#include "ExporterPlugin.h"
#include "Game.h"
#include "GlobalSettings.h"
#include "globalvars.h"
#include "MemoryUtils.h"
#include "ModelRenderPass.h"
#include "ArmoryImporter.h"
#include "FBXExporter.h"
#include "OBJExporter.h"
#include "WowheadImporter.h"
#include "RaceInfos.h"
#include "SettingsControl.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "logger/Logger.h"
#include <QSettings>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <filesystem>
#include <fstream>
#include <thread>

static void startQtEventLoop()
{
	if (QCoreApplication::instance() != nullptr)
		return;

	static std::thread qtThread([]() {
		int argc = 1;
		char* argv[] = {const_cast<char*>("wmv.app"), nullptr};
		QCoreApplication app(argc, argv);
		app.exec();
	});
	qtThread.detach();
}

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
	EVT_MENU(ID_FILE_UPDATE_LISTFILE, ModelViewer::OnUpdateListfile)
	EVT_MENU(ID_FILE_UPDATE_ENCRYPTION_KEYS, ModelViewer::OnUpdateEncryptionKeys)
	EVT_MENU(ID_VIEW_NPC, ModelViewer::OnCharToggle)
	EVT_MENU(ID_VIEW_ITEM, ModelViewer::OnCharToggle)
	EVT_MENU(ID_FILE_MODEL_INFO, ModelViewer::OnExportOther)
	EVT_MENU(ID_FILE_RESETLAYOUT, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_FILE_EXIT, ModelViewer::OnExit)
	// view menu
	EVT_MENU(ID_SHOW_FILE_LIST, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_ANIM, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_CHAR, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_LIGHT, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_MODEL, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_MODELBANK, ModelViewer::OnToggleDock)
	EVT_MENU(ID_SHOW_MASK, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_WIREFRAME, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_BONES, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SHOW_BOUNDS, ModelViewer::OnToggleCommand)
	//EVT_MENU(ID_SHOW_PARTICLES, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_BG_COLOR, ModelViewer::OnSetColor)
	EVT_MENU(ID_SHOW_GRID, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_USE_CAMERA, ModelViewer::OnToggleCommand)
	// Cam
	EVT_MENU(ID_CAM_FRONT, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_SIDE, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_BACK, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_ISO, ModelViewer::OnCamMenu)
	EVT_MENU(ID_CAM_RESET, ModelViewer::OnCamMenu)
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
	EVT_MENU(ID_OPENGL_DEBUG, ModelViewer::OnToggleCommand)
	// Light Menu
	EVT_MENU(ID_LT_SAVE, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_LOAD, ModelViewer::OnLightMenu)
	//EVT_MENU(ID_LT_COLOR, ModelViewer::OnSetColor)
	EVT_MENU(ID_LT_TRUE, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_AMBIENT, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_DIRECTIONAL, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_MODEL, ModelViewer::OnLightMenu)
	EVT_MENU(ID_LT_DIRECTION, ModelViewer::OnLightMenu)
	// Options
	EVT_MENU(ID_DEFAULT_DOODADS, ModelViewer::OnToggleCommand)
	EVT_MENU(ID_SHOW_SETTINGS, ModelViewer::OnToggleDock)
	// char controls:
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
	// About menu
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
	// refesh status bar timer
	EVT_TIMER(ID_STATUS_REFRESH_TIMER, ModelViewer::OnStatusBarRefreshTimer)
END_EVENT_TABLE()

ModelViewer::ModelViewer()
{
	startQtEventLoop();
	m_exporters.push_back(new OBJExporter());
	m_exporters.push_back(new FBXExporter());
	m_importers.push_back(new ArmoryImporter());
	m_importers.push_back(new WowheadImporter());
	// our main class objects
	animControl = nullptr;
	canvas = nullptr;
	charControl = nullptr;
	lightControl = nullptr;
	modelControl = nullptr;
	settingsControl = nullptr;
	modelbankControl = nullptr;
	fileControl = nullptr;

	//wxWidget objects
	menuBar = nullptr;
	charMenu = nullptr;
	charGlowMenu = nullptr;
	viewMenu = nullptr;
	optMenu = nullptr;
	lightMenu = nullptr;
	fileMenu = nullptr;

	isWoWLoaded = false;
	isModel = false;
	isChar = false;
	initDB = false;

	//wxCAPTION|wxRESIZE_BORDER|wxSYSTEM_MENU
	// create our main frame
	if (Create(nullptr, wxID_ANY, wxString(GLOBALSETTINGS.appTitle()), wxDefaultPosition, wxSize(1024, 768),
	           wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN, wxT("ModelViewerFrame")))
	{
		wxTopLevelWindowMSW::SetIcon(wxICON(IDI_ICON1));
		wxWindow::SetExtraStyle(wxWS_EX_VALIDATE_RECURSIVELY);
wxWindowBase::SetBackgroundStyle(wxBG_STYLE_CUSTOM);

		InitObjects(); // create our canvas, anim control, character control, etc

		// Show our window
		wxTopLevelWindowMSW::Show(false);
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
		wxWindow::Refresh();
		wxWindow::Update();

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
		wxFrameBase::SetStatusText(wxT("Setting OpenGL render state..."));
		video.InitGL();

		wxFrameBase::SetStatusText(wxEmptyString);

		timer.SetOwner(this, ID_STATUS_REFRESH_TIMER);
		timer.Start(2000);
	}
	else
	{
		LOG_FATAL << "Unable to create the main window for the application.";
		Close(true);
	}
}

void ModelViewer::InitMenu()
{
	LOG_INFO << "Initializing File Menu...";

	if (GetStatusBar() == nullptr)
	{
		CreateStatusBar(5);
		const int widths[] = {-1, 100, 50, 125, 125};
		SetStatusWidths(5, widths);
		SetStatusText(wxT("Initializing File Menu..."));
	}

	// MENU
	fileMenu = new wxMenu;
	fileMenu->Append(ID_LOAD_WOW, _("Load World of Warcraft"));
	if (isWoWLoaded == true)
		fileMenu->Enable(ID_LOAD_WOW, false);
	fileMenu->Append(ID_FILE_VIEWLOG, _("View Log"));
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_UPDATE_LISTFILE, _("Update Listfile"));
	fileMenu->Append(ID_FILE_UPDATE_ENCRYPTION_KEYS, _("Update Encryption Keys"));
	fileMenu->AppendSeparator();

	// export menu
	wxMenu* ExportMenu = new wxMenu;
	ExportMenu->Append(ID_FILE_MODEL_INFO, wxT("Export ModelInfo.xml"));

	int subMenuId = 10000;
	for (const auto* exporter : m_exporters)
	{
		ExportMenu->Append(subMenuId, exporter->menuLabel());
		Connect(subMenuId,
				wxEVT_COMMAND_MENU_SELECTED,
				wxCommandEventHandler(ModelViewer::OnExport));
		subMenuId++;
	}
	fileMenu->Append(ID_EXPORT_MODEL, wxT("Export Model"), ExportMenu);

	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_RESETLAYOUT, _("Reset Layout"));
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_EXIT, _("E&xit\tCTRL+X"));

	viewMenu = new wxMenu;
	viewMenu->Append(ID_VIEW_NPC, _("View NPC"));
	viewMenu->Append(ID_VIEW_ITEM, _("View Item"));
	viewMenu->AppendSeparator();
	viewMenu->Append(ID_SHOW_FILE_LIST, _("Show file list"));
	viewMenu->Append(ID_SHOW_ANIM, _("Show animation control"));
	viewMenu->Append(ID_SHOW_CHAR, _("Show character control"));
	viewMenu->Append(ID_SHOW_LIGHT, _("Show light control"));
	viewMenu->Append(ID_SHOW_MODEL, _("Show model control"));
	viewMenu->Append(ID_SHOW_MODELBANK, _("Show model bank"));
	viewMenu->AppendSeparator();
	if (canvas)
	{
		viewMenu->Append(ID_BG_COLOR, _("Background Color..."));
		viewMenu->AppendCheckItem(ID_SHOW_GRID, _("Show Grid"));
		viewMenu->Check(ID_SHOW_GRID, canvas->drawGrid);

		viewMenu->AppendCheckItem(ID_SHOW_MASK, _("Show Mask"));
		viewMenu->Check(ID_SHOW_MASK, false);

		viewMenu->AppendSeparator();
	}

	try
	{
		// Camera Menu
		wxMenu* CamMenu = new wxMenu;
		CamMenu->AppendCheckItem(ID_USE_CAMERA, _("Use model camera"));
		CamMenu->AppendSeparator();
		CamMenu->Append(ID_CAM_FRONT, _("Front"));
		CamMenu->Append(ID_CAM_BACK, _("Back"));
		CamMenu->Append(ID_CAM_SIDE, _("Side"));
		CamMenu->Append(ID_CAM_ISO, _("Perspective"));
		CamMenu->Append(ID_CAM_RESET, _("Reset to default"));

		viewMenu->Append(ID_CAMERA, _("Camera"), CamMenu);
		viewMenu->AppendSeparator();

		wxMenu* setSize = new wxMenu;
		setSize->Append(ID_CANVASS120, wxT("(1:1) 120 x 120"), _("Square (1:1)"));
		setSize->Append(ID_CANVASS512, wxT("(1:1) 512 x 512"), _("Square (1:1)"));
		setSize->Append(ID_CANVASS1024, wxT("(1:1) 1024 x 1024"), _("Square (1:1)"));
		setSize->Append(ID_CANVASF480, wxT("(4:3) 640 x 480"), _("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF600, wxT("(4:3) 800 x 600"), _("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF768, wxT("(4:3) 1024 x 768"), _("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF864, wxT("(4:3) 1152 x 864"), _("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASF1200, wxT("(4:3) 1600 x 1200"), _("Fullscreen (4:3)"));
		setSize->Append(ID_CANVASW480, wxT("(16:9) 864 x 480"), _("Widescreen (16:9)"));
		setSize->Append(ID_CANVASW720, wxT("(16:9) 1280 x 720"), _("Widescreen (16:9)"));
		setSize->Append(ID_CANVASW1080, wxT("(16:9) 1920 x 1080"), _("Widescreen (16:9)"));
		setSize->Append(ID_CANVASM768, wxT("(5:3) 1280 x 768"), _("Misc (5:3)"));
		setSize->Append(ID_CANVASM1200, wxT("(8:5) 1920 x 1200"), _("Misc (8:5)"));

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

		charGlowMenu = new wxMenu;
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_NONE, _("None"));
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEFAULT, _("Default"));
		charGlowMenu->AppendRadioItem(ID_CHAREYEGLOW_DEATHKNIGHT, _("Death Knight"));
		if (charControl->model)
		{
			const size_t egt = charControl->model->cd.eyeGlowType;
			if (egt == EGT_NONE)
				charGlowMenu->Check(ID_CHAREYEGLOW_NONE, true);
			else if (egt == EGT_DEATHKNIGHT)
				charGlowMenu->Check(ID_CHAREYEGLOW_DEATHKNIGHT, true);
			else
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
		charMenu->AppendCheckItem(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, _("Auto Hide Geosets for head items"));
		charMenu->Check(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);
		charMenu->AppendCheckItem(ID_SHEATHE, _("Sheathe Weapons\tCTRL+Z"));
		charMenu->Check(ID_SHEATHE, false);

		charMenu->AppendSeparator();
		charMenu->Append(ID_LOAD_SET, _("Load Item Set"));
		charMenu->Append(ID_LOAD_START, _("Load Start Outfit"));
		charMenu->AppendSeparator();
		charMenu->Append(ID_MOUNT_CHARACTER, _("Mount / Dismount"));

		// Start out Disabled.
		charMenu->Enable(ID_SHOW_UNDERWEAR, false);
		charMenu->Enable(ID_SHOW_EARS, false);
		charMenu->Enable(ID_SHOW_HAIR, false);
		charMenu->Enable(ID_SHOW_FACIALHAIR, false);
		charMenu->Enable(ID_SHOW_FEET, false);
		charMenu->Enable(ID_SHEATHE, false);
		charMenu->Enable(ID_CHAREYEGLOW, false);
		charMenu->Enable(ID_LOAD_SET, false);
		charMenu->Enable(ID_LOAD_START, false);
		charMenu->Enable(ID_MOUNT_CHARACTER, false);
		charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);

		// Options menu
		optMenu = new wxMenu;
		optMenu->AppendCheckItem(ID_DEFAULT_DOODADS, _("Always show default doodads in WMOs"));
		optMenu->Check(ID_DEFAULT_DOODADS, true);
		optMenu->AppendSeparator();
		optMenu->Append(ID_SHOW_SETTINGS, _("Settings..."));

		wxMenu* aboutMenu = new wxMenu;
		aboutMenu->Append(ID_LANGUAGE, _("Language"));
		aboutMenu->Append(ID_HELP, _("Help"));
		aboutMenu->Enable(ID_HELP, false);
		aboutMenu->Append(ID_ABOUT, _("About"));

		menuBar = new wxMenuBar();
		menuBar->Append(fileMenu, _("&File"));
		menuBar->Append(viewMenu, _("&View"));
		menuBar->Append(charMenu, _("&Character"));
		menuBar->Append(lightMenu, _("&Lighting"));
		menuBar->Append(optMenu, _("&Options"));
		menuBar->Append(aboutMenu, _("&About"));
		SetMenuBar(menuBar);
	}
	catch (...)
	{
		LOG_ERROR << "Exception occurred during menu initialization in ModelViewer::InitMenu()";
	};

	// Disable our "Character" menu, only accessible when a character model is being displayed
	// menuBar->EnableTop(2, false);

	// Hotkeys / shortcuts
	wxAcceleratorEntry entries[17];
	int keys = 0;
	entries[keys++].Set(wxACCEL_CTRL, (int)'b', ID_SHOW_BOUNDS);
	entries[keys++].Set(wxACCEL_CTRL, (int)'X', ID_FILE_EXIT);
	entries[keys++].Set(wxACCEL_CTRL, (int)'e', ID_SHOW_EARS);
	entries[keys++].Set(wxACCEL_CTRL, (int)'h', ID_SHOW_HAIR);
	entries[keys++].Set(wxACCEL_CTRL, (int)'f', ID_SHOW_FACIALHAIR);
	entries[keys++].Set(wxACCEL_CTRL, (int)'z', ID_SHEATHE);
	entries[keys++].Set(wxACCEL_CTRL, (int)'+', ID_ZOOM_IN);
	entries[keys++].Set(wxACCEL_CTRL, (int)'-', ID_ZOOM_OUT);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F11, ID_OPENGL_DEBUG);

	// Temporary saves
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F1, ID_SAVE_TEMP1);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F2, ID_SAVE_TEMP2);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F3, ID_SAVE_TEMP3);
	entries[keys++].Set(wxACCEL_NORMAL, WXK_F4, ID_SAVE_TEMP4);

	// Temp loads
	entries[keys++].Set(wxACCEL_CTRL, WXK_F1, ID_LOAD_TEMP1);
	entries[keys++].Set(wxACCEL_CTRL, WXK_F2, ID_LOAD_TEMP2);
	entries[keys++].Set(wxACCEL_CTRL, WXK_F3, ID_LOAD_TEMP3);
	entries[keys++].Set(wxACCEL_CTRL, WXK_F4, ID_LOAD_TEMP4);

	const wxAcceleratorTable accel(keys, entries);
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

	g_modelViewer = this;
	g_animControl = animControl;
	g_charControl = charControl;
	g_canvas = canvas;

	modelControl->animControl = animControl;
}

void ModelViewer::InitDatabase(std::function<void(int, int)> progressCallback)
{
	LOG_INFO << "Initializing Databases...";
	SetStatusText(wxT("Initializing Databases..."));

	if (progressCallback) progressCallback(0, 100);

	if (!GAMEDATABASE.initFromXML("database.xml"))
	{
		initDB = false;
		LOG_ERROR << "Initializing failed!";
		SetStatusText(wxT("Initializing failed!"));
		return;
	}
	else
	{
		LOG_INFO << "Initializing succeeded.";
	}

	if (progressCallback) progressCallback(15, 100);

	// init texture regions
	CharTexture::initRegions();

	if (progressCallback) progressCallback(20, 100);

	// init Race informations
	RaceInfos::init();

	if (progressCallback) progressCallback(25, 100);

	LOG_INFO << "Initializing Databases...";
	SetStatusText(wxT("Initializing Databases..."));
	initDB = true;

	{
		sqlResult npc = GAMEDATABASE.sqlQuery("SELECT ID, DisplayID1, CreatureType, Name_Lang From Creature;");

		if (npc.valid && !npc.empty())
		{
			LOG_INFO << "Found" << npc.values.size() << "NPCs";
			int count = 0;
			const int total = static_cast<int>(npc.values.size());
			for (const auto& value : npc.values)
			{
				NPCRecord rec(value);
				if (rec.model != 0)
					npcs.push_back(rec);
				if (progressCallback && ++count % 500 == 0)
					progressCallback(25 + count * 30 / total, 100);
			}
		}
		else
		{
			initDB = false;
			LOG_ERROR << "Error during NPC detection from database.";
			return;
		}
	}

	if (progressCallback) progressCallback(55, 100);

	{
		sqlResult item = GAMEDATABASE.sqlQuery(
			"SELECT Item.ID, ItemSparse.Display_Lang, Item.InventoryType, Item.ClassID, Item.SubclassID, Item.SheathType FROM Item LEFT JOIN ItemSparse ON Item.ID = ItemSparse.ID WHERE Item.InventoryType !=0 AND ItemSparse.Display_Lang != \"\"");

		if (item.valid && !item.empty())
		{
			LOG_INFO << "Found" << item.values.size() << "items";
			int count = 0;
			const int total = static_cast<int>(item.values.size());
			for (const auto& value : item.values)
			{
				ItemRecord rec(value);
				items.items.push_back(rec);
				if (progressCallback && ++count % 500 == 0)
					progressCallback(55 + count * 40 / total, 100);
			}
		}
		else
		{
			initDB = false;
			LOG_ERROR << "Error during Item detection from database.";
			return;
		}
	}

	if (progressCallback) progressCallback(100, 100);
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
	                                      BestSize(wxSize(170, 700)).Left().Layer(2));

	// Animation frame
	interfaceManager.AddPane(animControl, wxAuiPaneInfo().
	                                      Name(wxT("animControl")).Caption(wxT("Animation")).
	                                      Bottom().Layer(1));

	// Character frame
	interfaceManager.AddPane(charControl, wxAuiPaneInfo().
	                                      Name(wxT("charControl")).Caption(wxT("Character")).
	                                      BestSize(wxSize(170, 700)).Right().Layer(2).Show(isChar));

	// Lighting control
	interfaceManager.AddPane(lightControl, wxAuiPaneInfo().
	                                       Name(wxT("Lighting")).Caption(wxT("Lighting")).
	                                       FloatingSize(wxSize(170, 430)).Float().Fixed().Show(false).
	                                       DestroyOnClose(false)); //.FloatingPosition(GetStartPosition())

	// model control
	interfaceManager.AddPane(modelControl, wxAuiPaneInfo().
	                                       Name(wxT("Models")).Caption(wxT("Models")).
	                                       FloatingSize(wxSize(160, 460)).TopDockable(false).BottomDockable(false).
	                                       Float().Show(false).
	                                       DestroyOnClose(false));

	// model bank control
	interfaceManager.AddPane(modelbankControl, wxAuiPaneInfo().
	                                           Name(wxT("ModelBank")).Caption(wxT("ModelBank")).
	                                           FloatingSize(wxSize(300, 320)).Float().Fixed().Show(false).
	                                           DestroyOnClose(false));

	// settings frame
	interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
	                                          Name(wxT("Settings")).Caption(wxT("Settings")).
	                                          FloatingSize(wxSize(400, 550)).Float().TopDockable(false).LeftDockable(
		                                          false).
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
	                                      BestSize(wxSize(170, 700)).Left().Layer(2));

	// Animation frame
	interfaceManager.AddPane(animControl, wxAuiPaneInfo().
	                                      Name(wxT("animControl")).Caption(wxT("Animation")).
	                                      Bottom().Layer(1));

	// Character frame
	interfaceManager.AddPane(charControl, wxAuiPaneInfo().
	                                      Name(wxT("charControl")).Caption(wxT("Character")).
	                                      BestSize(wxSize(170, 700)).Right().Layer(2).Show(isChar));

	interfaceManager.AddPane(lightControl, wxAuiPaneInfo().
	                                       Name(wxT("Lighting")).Caption(wxT("Lighting")).
	                                       FloatingSize(wxSize(170, 430)).Float().Fixed().Show(false).
	                                       DestroyOnClose(false)); //.FloatingPosition(GetStartPosition())

	interfaceManager.AddPane(modelControl, wxAuiPaneInfo().
	                                       Name(wxT("Models")).Caption(wxT("Models")).
	                                       FloatingSize(wxSize(160, 460)).Float().TopDockable(false).LeftDockable(false)
	                                       .
	                                       RightDockable(false).TopDockable(false).BottomDockable(false).Show(false).
	                                       DestroyOnClose(false));

	interfaceManager.AddPane(settingsControl, wxAuiPaneInfo().
	                                          Name(wxT("Settings")).Caption(wxT("Settings")).
	                                          FloatingSize(wxSize(400, 550)).Float().TopDockable(false).LeftDockable(
		                                          false).
	                                          RightDockable(false).BottomDockable(false).Show(false));

	// tell the manager to "commit" all the changes just made
	interfaceManager.Update();
}

void ModelViewer::LoadSession()
{
	LOG_INFO << "Loading Session settings from:" << QString::fromWCharArray(cfgPath.c_str());

	const QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

	// Application Config Settings
	useRandomLooks = config.value("Session/RandomLooks", true).toBool();
	GLOBALSETTINGS.bShowParticle = config.value("Session/ShowParticle", true).toBool();
	GLOBALSETTINGS.bZeroParticle = config.value("Session/ZeroParticle", true).toBool();
	GLOBALSETTINGS.bInitPoseOnlyExport = config.value("Session/InitPoseOnlyExport", false).toBool();

	// Background and Custom Colours
	wxColour bgCol;
	wxString colStr = config.value("Session/bgCol", "#475F79").toString().toStdWString(); // #475F79 = (71, 95, 121)
	if (!bgCol.Set(colStr))
		bgCol = wxColour(71, 95, 121);
	bgDialogData.SetColour(bgCol);
	for (int i = 0; i < 16; i++)
	{
		wxColour custCol;
		colStr = config.value(QString("Session/bgCustCol%1").arg(i), QString()).toString().toStdWString();
		if ((colStr != wxEmptyString) && custCol.Set(colStr))
			bgDialogData.SetCustomColour(i, custCol);
	}
	// Other session settings
	if (canvas)
	{
		// Set canvas background Colour
		canvas->vecBGColor.x = bgCol.Red() / 255.0f;
		canvas->vecBGColor.y = bgCol.Green() / 255.0f;
		canvas->vecBGColor.z = bgCol.Blue() / 255.0f;

			}
		}

void ModelViewer::SaveSession()
{
	QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

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
	wxColour bgCol = bgDialogData.GetColour();
	config.setValue("Session/bgCol", QString::fromWCharArray(bgCol.GetAsString(wxC2S_HTML_SYNTAX).c_str()));
	for (int i = 0; i < 16; i++)
	{
		bgCol = bgDialogData.GetCustomColour(i);
		if (!bgCol.IsOk()) // skip undefined custom colours
			continue;
		config.setValue(QString("Session/bgCustCol%1").arg(i),
		                QString::fromWCharArray(bgCol.GetAsString(wxC2S_HTML_SYNTAX).c_str()));
	}

	if (canvas)
	{
		int canvx = 0, canvy = 0;
		canvas->GetClientSize(&canvx, &canvy);
		if (charControl->IsShown() == true)
		{
			const wxAuiPaneInfo info = interfaceManager.GetPane(wxT("charControl"));
			if (info.IsFloating() == false)
			{
				if (info.IsDocked() == true && (info.dock_direction == wxAUI_DOCK_RIGHT || info.dock_direction ==
					wxAUI_DOCK_LEFT))
				{
					int x = 0;
					charControl->GetClientSize(&x, nullptr);
					canvx += x + 6; // 6 seems to cover margins and borders...
				}
				else if (info.IsDocked() == true && (info.dock_direction == wxAUI_DOCK_TOP || info.dock_direction ==
					wxAUI_DOCK_BOTTOM))
				{
					int y = 0;
					charControl->GetClientSize(nullptr, &y);
					canvy += y + 23; // 23 covers the margins, borders, and title bar...
				}
			}
		}

		config.setValue("Session/CanvasWidth", canvx);
		config.setValue("Session/CanvasHeight", canvy);

		// model file
		if (canvas->model())
			config.setValue("Session/Model", QString::fromStdString(canvas->model()->name()));
	}
}

void ModelViewer::LoadLayout()
{
	const QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

	const int posx = config.value("Session/PositionX", "").toInt();
	const int posy = config.value("Session/PositionY", "").toInt();

	SetPosition(wxPoint(posx, posy));

	// Increment this whenever the default layout changes or a layout reset
	// is needed (e.g. after the x86 → x64 migration).
	const int currentLayoutVersion = 2;
	const int savedLayoutVersion = config.value("Session/LayoutVersion", 0).toInt();

	const wxString layout = config.value("Session/Layout", "").toString().toStdWString();

	// if the layout data exists and is from a compatible version, load it.
	if (savedLayoutVersion == currentLayoutVersion
		&& !layout.IsNull() // something goes wrong
		&& !layout.IsEmpty() // empty value
		&& !layout.EndsWith(L"canvas")) // old saving badly read by Qt, ignore
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
interfaceManager.Update();
LOG_INFO << "GUI Layout loaded from previous session.";
		}
	}
	else if (!layout.IsEmpty() && savedLayoutVersion != currentLayoutVersion)
	{
		LOG_INFO << "Discarding saved layout (version " << savedLayoutVersion << " != " << currentLayoutVersion << "). Using default layout.";
	}

	// Restore saved canvas size:
	if (canvas)
	{
		const int canvx = config.value("Session/CanvasWidth", 800).toInt();
		const int canvy = config.value("Session/CanvasHeight", 600).toInt();
		SetCanvasSize(canvx, canvy);
	}
}

void ModelViewer::SaveLayout()
{
	QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

	config.setValue("Session/Layout", QString::fromWCharArray(interfaceManager.SavePerspective().c_str()));
	config.setValue("Session/LayoutVersion", 2);

	const wxPoint pos = GetPosition();
	config.setValue("Session/PositionX", pos.x);
	config.setValue("Session/PositionY", pos.y);

	LOG_INFO << "GUI Layout was saved.";
}

void ModelViewer::LoadModel(GameFile* file)
{
	if (!canvas || !file)
		return;

	LOG_INFO << "Loading model:" << file->fullname();

	if (canvas->model() && canvas->model()->gamefile && (canvas->model()->gamefile->fullname() == file->fullname()))
		// don't reload same model
		return;

	isModel = true;

	// check if this is a character model
	isChar = (file->fullname().startsWith("char", Qt::CaseInsensitive) || file->fullname().startsWith(
		"alternate\\char", Qt::CaseInsensitive));
	Attachment* modelAtt;

	if (isChar)
	{
		modelAtt = canvas->LoadModel(file);
		// error check
		if (!modelAtt)
		{
			LOG_ERROR << "Failed to load the model" << file->fullname();
			return;
		}

		// add children to manage items equipped
		WoWModel* m = const_cast<WoWModel*>(canvas->model());
		m->addChild(new WoWItem(CS_SHIRT));
		m->addChild(new WoWItem(CS_HEAD));
		m->addChild(new WoWItem(CS_SHOULDER));
		m->addChild(new WoWItem(CS_PANTS));
		m->addChild(new WoWItem(CS_BOOTS));
		m->addChild(new WoWItem(CS_CHEST));
		m->addChild(new WoWItem(CS_TABARD));
		m->addChild(new WoWItem(CS_BELT));
		m->addChild(new WoWItem(CS_BRACERS));
		m->addChild(new WoWItem(CS_GLOVES));
		m->addChild(new WoWItem(CS_HAND_RIGHT));
		m->addChild(new WoWItem(CS_HAND_LEFT));
		m->addChild(new WoWItem(CS_CAPE));
		m->addChild(new WoWItem(CS_QUIVER));
		m->modelType = MT_CHAR;
	}
	else
	{
		modelAtt = canvas->LoadModel(file); //  change it from LoadModel, don't sure it's right or not.

		// error check
		if (!modelAtt)
		{
			LOG_ERROR << "Failed to load the model" << file->fullname();
			return;
		}
		// creature model, keep left/right hand only as equipment
		WoWModel* m = const_cast<WoWModel*>(canvas->model());
		m->addChild(new WoWItem(CS_HAND_RIGHT));
		m->addChild(new WoWItem(CS_HAND_LEFT));
		m->modelType = MT_NORMAL;
	}

	// Error check,  make sure the model was actually loaded and set to canvas->model
	if (!canvas->model())
	{
		LOG_ERROR << "[ModelViewer::LoadModel()]  Model* Canvas::model is null!";
		return;
	}

	SetStatusText(wxString::FromUTF8(canvas->model()->name()));
	WoWModel* m = const_cast<WoWModel*>(canvas->model());
	m->charModelDetails.isChar = isChar;

	viewMenu->Enable(ID_USE_CAMERA, canvas->model()->hasCamera);
	if (canvas->useCamera && !canvas->model()->hasCamera)
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

		charMenu->Enable(ID_SHOW_UNDERWEAR, true);
		charMenu->Enable(ID_SHOW_EARS, true);
		charMenu->Enable(ID_SHOW_HAIR, true);
		charMenu->Enable(ID_SHOW_FACIALHAIR, true);
		charMenu->Enable(ID_SHOW_FEET, true);
		charMenu->Enable(ID_SHEATHE, true);
		charMenu->Enable(ID_CHAREYEGLOW, true);
		charMenu->Enable(ID_LOAD_SET, true);
		charMenu->Enable(ID_LOAD_START, true);
		charMenu->Enable(ID_MOUNT_CHARACTER, true);
		charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);

		charControl->UpdateModel(modelAtt);
	}
	else
	{
		charControl->UpdateModel(modelAtt);

		charMenu->Enable(ID_SHOW_UNDERWEAR, false);
		charMenu->Enable(ID_SHOW_EARS, false);
		charMenu->Enable(ID_SHOW_HAIR, false);
		charMenu->Enable(ID_SHOW_FACIALHAIR, false);
		charMenu->Enable(ID_SHOW_FEET, false);
		charMenu->Enable(ID_SHEATHE, false);
		charMenu->Enable(ID_CHAREYEGLOW, false);
		charMenu->Enable(ID_LOAD_SET, false);
		charMenu->Enable(ID_LOAD_START, false);
		charMenu->Enable(ID_MOUNT_CHARACTER, false);
		charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
	}

	// Update the model control
	modelControl->UpdateModel(modelAtt);
	modelControl->RefreshModel(canvas->root);

	// Update the animations / skins
	animControl->UpdateModel(m);
	interfaceManager.Update();
}

// Load an NPC model
void ModelViewer::LoadNPC(unsigned int modelid)
{
	canvas->clearAttachments();
	canvas->setModel(nullptr);

	isModel = true;
	isChar = false;


	QString query = QString("SELECT CreatureModelData.FileDataID, CreatureDisplayInfo.TextureVariationFileDataID1, "
		"CreatureDisplayInfo.TextureVariationFileDataID2, CreatureDisplayInfo.TextureVariationFileDataID3, "
		"CreatureDisplayInfo.ExtendedDisplayInfoID, CreatureDisplayInfo.ID FROM Creature "
		"LEFT JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
		"LEFT JOIN CreatureModelData ON CreatureDisplayInfo.modelID = CreatureModelData.ID "
		"WHERE Creature.ID = %1;").arg(modelid);

	sqlResult r = GAMEDATABASE.sqlQuery(query.toStdString());

	if (r.valid && !r.empty())
	{
		const int extraId = std::stoi(r.values[0][4]);
		// if npc is a simple one (no extra info CreatureDisplayInfoExtra)
		if (extraId == 0)
		{
			LoadModel(GAMEDIRECTORY.getFile(std::stoi(r.values[0][0])));
			WoWModel* m = const_cast<WoWModel*>(canvas->model());
			m->modelType = MT_NORMAL;
			animControl->SetSkinByDisplayID(std::stoi(r.values[0][5]));
		}
		else
		{
			LoadModel(GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(std::stoi(r.values[0][0]))));

			query = QString(
					"SELECT Skin, Face, HairStyle, HairColor, FacialHair FROM CreatureDisplayInfoExtra WHERE ID = %1").
					arg(extraId);

			r = GAMEDATABASE.sqlQuery(query.toStdString());

			if (r.valid && !r.empty())
			{
				g_charControl->model->cd.set(CharDetails::SKIN_COLOR, std::stoi(r.values[0][0]));
				g_charControl->model->cd.set(CharDetails::FACE, std::stoi(r.values[0][1]));
				g_charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_STYLE, std::stoi(r.values[0][2]));
				g_charControl->model->cd.set(CharDetails::FACIAL_CUSTOMIZATION_COLOR, std::stoi(r.values[0][3]));
				g_charControl->model->cd.set(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION, std::stoi(r.values[0][4]));
			}

			query = QString("SELECT ItemDisplayInfoID, ItemSlot FROM NpcModelItemSlotDisplayInfo WHERE NpcModelID = %1")
				.arg(extraId);

			r = GAMEDATABASE.sqlQuery(query.toStdString());

			if (r.valid && !r.empty())
			{
				static map<int, CharSlots> ItemTypeToInternal = {
					{0, CS_HEAD}, {1, CS_SHOULDER}, {2, CS_SHIRT}, {3, CS_CHEST}, {4, CS_BELT}, {5, CS_PANTS},
					{6, CS_BOOTS}, {7, CS_BRACERS}, {8, CS_GLOVES}, {9, CS_TABARD}, {10, CS_CAPE}
				};
				for (auto& value : r.values)
				{
					WoWItem* item = g_charControl->model->getItem(ItemTypeToInternal[std::stoi(value[1])]);
					if (item)
						item->setDisplayId(std::stoi(value[0]));
				}
			}

			g_charControl->model->cd.isNPC = true;
			g_charControl->RefreshModel();
			g_charControl->RefreshEquipment();
		}
	}

	fileControl->UpdateInterface();

	// wxAUI
	// hide charControl if current model is not a Character one.
	interfaceManager.GetPane(charControl).Show(isChar);

	interfaceManager.Update();
}

void ModelViewer::LoadItem(unsigned int id)
{
	canvas->clearAttachments();
	canvas->setModel(nullptr);

	isModel = true;
	isChar = false;

	try
	{
		const QString query = QString(
			"SELECT ModelFileData.FileDataID, TextureFileData.FileDataID, ItemDisplayInfo.ID FROM ItemDisplayInfo "
			"LEFT JOIN ModelFileData ON ItemDisplayInfo.ModelResourcesID1 = ModelFileData.ModelResourcesID "
			"LEFT JOIN TextureFileData ON ItemDisplayInfo.ModelMaterialResourcesID1 = TextureFileData.MaterialResourcesID "
			"WHERE ItemDisplayInfo.ID = (SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ItemAppearance.ID = "
			"(SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1))").arg(id);

		sqlResult itemInfos = GAMEDATABASE.sqlQuery(query.toStdString());
		// LOG_INFO << query;

		if (itemInfos.valid && !itemInfos.empty())
		{
			if (itemInfos.values[0][0] != "" && itemInfos.values[0][1] != "")
			{
				LoadModel(GAMEDIRECTORY.getFile(std::stoi(itemInfos.values[0][0])));
				TextureGroup grp;
				grp.base = TEXTURE_OBJECT_SKIN;
				grp.count = 1;
				grp.tex[0] = GAMEDIRECTORY.getFile(std::stoi(itemInfos.values[0][1]));
				if (grp.tex[0])
					animControl->SetSkinByDisplayID(std::stoi(itemInfos.values[0][2]));
			}
		}

		charMenu->Enable(ID_SHOW_UNDERWEAR, false);
		charMenu->Enable(ID_SHOW_EARS, false);
		charMenu->Enable(ID_SHOW_HAIR, false);
		charMenu->Enable(ID_SHOW_FACIALHAIR, false);
		charMenu->Enable(ID_SHOW_FEET, false);
		charMenu->Enable(ID_SHEATHE, false);
		charMenu->Enable(ID_CHAREYEGLOW, false);
		charMenu->Enable(ID_LOAD_SET, false);
		charMenu->Enable(ID_LOAD_START, false);
		charMenu->Enable(ID_MOUNT_CHARACTER, false);
		charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
	}
	catch (...)
	{
		LOG_ERROR << "Exception occurred during menu initialization in ModelViewer::InitMenu()";
	}

	// wxAUI
	interfaceManager.GetPane(charControl).Show(isChar);
	interfaceManager.Update();
}

// This is called when the user goes to File->Exit
void ModelViewer::OnExit(wxCommandEvent& event)
{
	if (event.GetId() == ID_FILE_EXIT)
	{
		video.render = false;
		//canvas->timer.Stop();
		canvas->Disable();
		Close(false);
	}
}

// This is called when the window is closing
void ModelViewer::OnClose(wxCloseEvent& event)
{
	Destroy();
}

ModelViewer::~ModelViewer()
{
	LOG_INFO << "Shutting down the program...";

	video.render = false;

	// If we have a canvas (which we always should)
	// Stop rendering, give more power back to the CPU to close this sucker down!
	//if (canvas)
	//  canvas->timer.Stop();

	// Save current layout
	SaveLayout();

	// wxAUI stuff
	interfaceManager.UnInit();

	// Save our session and layout info
	SaveSession();

	if (canvas)
	{
		canvas->Disable();
		canvas->Destroy();
		canvas = nullptr;
	}

	if (fileControl)
	{
		fileControl->Destroy();
		fileControl = nullptr;
	}

	if (animControl)
	{
		animControl->Destroy();
		animControl = nullptr;
	}

	if (charControl)
	{
		charControl->Destroy();
		charControl = nullptr;
	}

	if (lightControl)
	{
		lightControl->Destroy();
		lightControl = nullptr;
	}

	if (settingsControl)
	{
		settingsControl->Destroy();
		settingsControl = nullptr;
	}

	if (modelControl)
	{
		modelControl->Destroy();
		modelControl = nullptr;
	}

	for (auto* e : m_exporters)
		delete e;
	m_exporters.clear();

	for (auto* i : m_importers)
		delete i;
	m_importers.clear();

	}

// Menu button press events
void ModelViewer::OnToggleDock(wxCommandEvent& event)
{
	const int id = event.GetId();

	// wxAUI Stuff
	if (id == ID_SHOW_FILE_LIST)
	{
		interfaceManager.GetPane(fileControl).Show(true);
	}
	else if (id == ID_SHOW_ANIM)
	{
		interfaceManager.GetPane(animControl).Show(true);
	}
	else if (id == ID_SHOW_CHAR)
	{
		interfaceManager.GetPane(charControl).Show(true);
	}
	else if (id == ID_SHOW_LIGHT)
	{
		interfaceManager.GetPane(lightControl).Show(true);
	}
	else if (id == ID_SHOW_MODEL)
	{
		interfaceManager.GetPane(modelControl).Show(true);
		modelControl->Update();
	}
	else if (id == ID_SHOW_SETTINGS)
	{
		interfaceManager.GetPane(settingsControl).Show(true);
		settingsControl->Open();
	}
	else if (id == ID_SHOW_MODELBANK)
	{
		interfaceManager.GetPane(modelbankControl).Show(true);
	}
	interfaceManager.Update();
}

// Menu button press events
void ModelViewer::OnToggleCommand(wxCommandEvent& event)
{
	const int id = event.GetId();

	//switch 
	switch (id)
	{
	case ID_FILE_RESETLAYOUT:
		ResetLayout();
		break;

	case ID_SHOW_MASK:
		video.useMasking = !video.useMasking;
		break;

	case ID_SHOW_BOUNDS:
		if (canvas->model())
		{
			WoWModel* m = const_cast<WoWModel*>(canvas->model());
			m->showBounds = !m->showBounds;
		}
		break;

	case ID_SHOW_GRID:
		canvas->drawGrid = event.IsChecked();
		break;

	case ID_USE_CAMERA:
		canvas->useCamera = event.IsChecked();
		break;

	case ID_DEFAULT_DOODADS:
		animControl->defaultDoodads = event.IsChecked();
		break;

	/*
  case ID_ZOOM_IN:
  canvas->Zoom(0.5f, false);
  break;

  case ID_ZOOM_OUT:
  canvas->Zoom(-0.5f, false);
  break;
  */
	case ID_OPENGL_DEBUG:
		canvas->toggleOpenGLDebug();
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
	default: ;
	}
}

void ModelViewer::OnLightMenu(wxCommandEvent& event)
{
	const int id = event.GetId();

	switch (id)
	{
	case ID_LT_SAVE:
		{
			wxFileDialog dialog(this, wxT("Save Lighting"), wxEmptyString, wxEmptyString,
			                    wxT("Scene Lighting (*.lit)|*.lit"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dialog.ShowModal() == wxID_OK)
			{
				const wxString fn = dialog.GetPath();

				// FIXME: ofstream is not compatible with multibyte path name
				std::ofstream f(fn.fn_str(), ios_base::out | ios_base::trunc);

				f << lightMenu->IsChecked(ID_LT_DIRECTION) << " " << lightMenu->IsChecked(ID_LT_TRUE) << " " <<
					lightMenu->IsChecked(ID_LT_DIRECTIONAL) << " " << lightMenu->IsChecked(ID_LT_AMBIENT) << " " <<
					lightMenu->IsChecked(ID_LT_MODEL) << endl;
				for (size_t i = 0; i < MAX_LIGHTS; i++)
				{
					f << lightControl->lights[i].ambience.x << " " << lightControl->lights[i].ambience.y << " " <<
						lightControl->lights[i].ambience.z << " " << lightControl->lights[i].arc << " " << lightControl
						->lights[i].constant_int << " " << lightControl->lights[i].diffuse.x << " " << lightControl->
						lights[i].diffuse.y << " " << lightControl->lights[i].diffuse.z << " " << lightControl->lights[
							i].enabled << " " << lightControl->lights[i].linear_int << " " << lightControl->lights[i].
						pos.x << " " << lightControl->lights[i].pos.y << " " << lightControl->lights[i].pos.z << " " <<
						lightControl->lights[i].quadradic_int << " " << lightControl->lights[i].relative << " " <<
						lightControl->lights[i].specular.x << " " << lightControl->lights[i].specular.y << " " <<
						lightControl->lights[i].specular.z << " " << lightControl->lights[i].target.x << " " <<
						lightControl->lights[i].target.y << " " << lightControl->lights[i].target.z << " " <<
						lightControl->lights[i].type << endl;
				}
				f.close();
			}

			return;
		}
	case ID_LT_LOAD:
		{
			wxFileDialog dialog(this, wxT("Load Lighting"), wxEmptyString, wxEmptyString,
			                    wxT("Scene Lighting (*.lit)|*.lit"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

			if (dialog.ShowModal() == wxID_OK)
			{
				const wxString fn = dialog.GetFilename();
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

				for (size_t i = 0; i < MAX_LIGHTS; i++)
				{
					f >> lightControl->lights[i].ambience.x >> lightControl->lights[i].ambience.y >> lightControl->
						lights[i].ambience.z >> lightControl->lights[i].arc >> lightControl->lights[i].constant_int >>
						lightControl->lights[i].diffuse.x >> lightControl->lights[i].diffuse.y >> lightControl->lights[
							i].diffuse.z >> lightControl->lights[i].enabled >> lightControl->lights[i].linear_int >>
						lightControl->lights[i].pos.x >> lightControl->lights[i].pos.y >> lightControl->lights[i].pos.z
						>> lightControl->lights[i].quadradic_int >> lightControl->lights[i].relative >> lightControl->
						lights[i].specular.x >> lightControl->lights[i].specular.y >> lightControl->lights[i].specular.z
						>> lightControl->lights[i].target.x >> lightControl->lights[i].target.y >> lightControl->lights[
							i].target.z >> lightControl->lights[i].type;
				}
				f.close();

				if (lightObj)
					canvas->drawLightDir = true;

				if (lightDir)
				{
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
				}
				else if (lightAmb)
				{
					//glEnable(GL_COLOR_MATERIAL);
					canvas->lightType = LIGHT_AMBIENT;
				}
				else if (lightModel)
				{
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
		if (event.IsChecked())
		{
			// Need to reset all our colour, lighting, material back to 'default'
			//GLfloat b[] = {0.5f, 0.4f, 0.4f, 1.0f};
			//glColor4fv(b);
			glDisable(GL_COLOR_MATERIAL);

			glMaterialfv(GL_FRONT, GL_EMISSION, def_emission);
			glMaterialfv(GL_FRONT, GL_AMBIENT, def_ambience);
			//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, def_ambience);

			glMaterialfv(GL_FRONT, GL_DIFFUSE, def_diffuse);
			glMaterialfv(GL_FRONT, GL_SPECULAR, def_specular);
		}
		else
		{
			glEnable(GL_COLOR_MATERIAL);
			//glLightModelfv(GL_LIGHT_MODEL_AMBIENT, glm::value_ptr(glm::vec4(0.4f,0.4f,0.4f,1.0f)));
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
	default: ;
	}
}

void ModelViewer::OnCamMenu(wxCommandEvent& event)
{
	canvas->OnCamMenu(event);
}

// Menu button press events
void ModelViewer::OnSetColor(wxCommandEvent& event)
{
	const int id = event.GetId();
	if (id == ID_BG_COLOR)
	{
		canvas->vecBGColor = DoSetColor(canvas->vecBGColor);
	}
}

glm::vec3 ModelViewer::DoSetColor(const glm::vec3& defColor)
{
	const wxColour dcol(roundf(defColor.x * 255.0f), roundf(defColor.y * 255.0f), roundf(defColor.z * 255.0f));
	bgDialogData.SetChooseFull(true);
	bgDialogData.SetColour(dcol);

	wxColourDialog dialog(this, &bgDialogData);

	if (dialog.ShowModal() == wxID_OK)
	{
		bgDialogData = dialog.GetColourData();
		const wxColour col = bgDialogData.GetColour();
		return glm::vec3(col.Red() / 255.0f, col.Green() / 255.0f, col.Blue() / 255.0f);
	}
	return defColor;
}

void ModelViewer::OnSetEquipment(wxCommandEvent& event)
{
	if (isChar)
		charControl->OnButton(event);

	UpdateControls();
}

void ModelViewer::OnViewLog(wxCommandEvent& event)
{
	const int ID = event.GetId();
	if (ID == ID_FILE_VIEWLOG)
	{
		wxString logPath = cfgPath.BeforeLast(SLASH) + SLASH + wxT("log.txt");
wxExecute(wxT("notepad.exe ") + logPath);
	}
}

void ModelViewer::OnGameToggle(wxCommandEvent& event)
{
	const int ID = event.GetId();
	if (ID == ID_LOAD_WOW)
		LoadWoW();
}

bool ModelViewer::DownloadListfile()
{
	LOG_INFO << "Downloading latest listfile...";
	SetStatusText(wxT("Downloading listfile..."));

	wxProgressDialog progressDlg(wxT("Downloading Listfile"),
		wxT("Connecting..."), 100, this,
		wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);

	const QUrl url("https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv");
	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", "WoWModelViewer");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* response = manager.get(request);

	QObject::connect(response, &QNetworkReply::downloadProgress,
		[&progressDlg](qint64 received, qint64 total) {
			if (total > 0)
			{
				const int percent = static_cast<int>(received * 100 / total);
				progressDlg.Update(percent,
					wxString::Format("Downloaded %lld / %lld KB", received / 1024, total / 1024));
			}
			else
			{
				progressDlg.Pulse(
					wxString::Format("Downloaded %lld KB", received / 1024));
			}
		});

	QEventLoop eventLoop;
	QObject::connect(response, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
	QObject::connect(response, &QNetworkReply::errorOccurred, &eventLoop, &QEventLoop::quit);
	eventLoop.exec();

	if (response->error() != QNetworkReply::NoError)
	{
		LOG_ERROR << "Failed to download listfile:" << response->errorString();
		wxMessageBox(wxString::Format("Failed to download listfile:\n%s",
			response->errorString().toStdWString()), wxT("Download Error"), wxOK | wxICON_ERROR);
		response->deleteLater();
		SetStatusText(wxT("Listfile update failed."));
		return false;
	}

	const QString destPath = QCoreApplication::applicationDirPath() + "/listfile.csv";
	std::ofstream file(destPath.toStdWString(), std::ios::binary);
	if (!file.is_open())
	{
		LOG_ERROR << "Failed to write listfile to" << destPath;
		wxMessageBox(wxT("Failed to write listfile.csv to disk."), wxT("File Error"), wxOK | wxICON_ERROR);
		response->deleteLater();
		SetStatusText(wxT("Listfile update failed."));
		return false;
	}

	const QByteArray data = response->readAll();
	file.write(data.constData(), data.size());
	file.close();
	response->deleteLater();

	LOG_INFO << "Listfile updated successfully at" << destPath;
	SetStatusText(wxT("Listfile updated successfully."));
	return true;
}

void ModelViewer::OnUpdateListfile(wxCommandEvent& /*event*/)
{
	if (DownloadListfile())
	{
		if (isWoWLoaded)
			wxMessageBox(wxT("Listfile updated successfully.\nRestart the application to use the new listfile."),
				wxT("Update Complete"), wxOK | wxICON_INFORMATION);
		else
			wxMessageBox(wxT("Listfile updated successfully."),
				wxT("Update Complete"), wxOK | wxICON_INFORMATION);
	}
}

bool ModelViewer::DownloadEncryptionKeys()
{
	LOG_INFO << "Downloading latest encryption keys...";
	SetStatusText(wxT("Downloading encryption keys..."));

	wxProgressDialog progressDlg(wxT("Downloading Encryption Keys"),
		wxT("Connecting..."), 100, this,
		wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);

	const QUrl url("https://raw.githubusercontent.com/wowdev/TACTKeys/master/WoW.txt");
	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", "WoWModelViewer");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* response = manager.get(request);

	QObject::connect(response, &QNetworkReply::downloadProgress,
		[&progressDlg](qint64 received, qint64 total) {
			if (total > 0)
			{
				const int percent = static_cast<int>(received * 100 / total);
				progressDlg.Update(percent,
					wxString::Format("Downloaded %lld / %lld bytes", received, total));
			}
			else
			{
				progressDlg.Pulse(
					wxString::Format("Downloaded %lld bytes", received));
			}
		});

	QEventLoop eventLoop;
	QObject::connect(response, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
	QObject::connect(response, &QNetworkReply::errorOccurred, &eventLoop, &QEventLoop::quit);
	eventLoop.exec();

	if (response->error() != QNetworkReply::NoError)
	{
		LOG_ERROR << "Failed to download encryption keys:" << response->errorString();
		wxMessageBox(wxString::Format("Failed to download encryption keys:\n%s",
			response->errorString().toStdWString()), wxT("Download Error"), wxOK | wxICON_ERROR);
		response->deleteLater();
		SetStatusText(wxT("Encryption keys update failed."));
		return false;
	}

	// The source file uses spaces as separators; the app expects semicolons.
	QString content = QString::fromUtf8(response->readAll());
	response->deleteLater();
	content.replace(' ', ';');

	const QString destPath = QCoreApplication::applicationDirPath() + "/extraEncryptionKeys.csv";
	std::ofstream file(destPath.toStdWString());
	if (!file.is_open())
	{
		LOG_ERROR << "Failed to write encryption keys to" << destPath;
		wxMessageBox(wxT("Failed to write extraEncryptionKeys.csv to disk."), wxT("File Error"), wxOK | wxICON_ERROR);
		SetStatusText(wxT("Encryption keys update failed."));
		return false;
	}

	const QByteArray utf8Data = content.toUtf8();
	file.write(utf8Data.constData(), utf8Data.size());
	file.close();

	LOG_INFO << "Encryption keys updated successfully at" << destPath;
	SetStatusText(wxT("Encryption keys updated successfully."));
	return true;
}

void ModelViewer::OnUpdateEncryptionKeys(wxCommandEvent& /*event*/)
{
	if (DownloadEncryptionKeys())
	{
		if (isWoWLoaded)
			wxMessageBox(wxT("Encryption keys updated successfully.\nRestart the application to use the new keys."),
				wxT("Update Complete"), wxOK | wxICON_INFORMATION);
		else
			wxMessageBox(wxT("Encryption keys updated successfully."),
				wxT("Update Complete"), wxOK | wxICON_INFORMATION);
	}
}

bool ModelViewer::CheckAndUpdateSupportFiles()
{
	namespace fs = std::filesystem;

	const QString appDir = QCoreApplication::applicationDirPath();
	const fs::path listfilePath = fs::path(appDir.toStdWString()) / "listfile.csv";
	const fs::path keysPath = fs::path(appDir.toStdWString()) / "extraEncryptionKeys.csv";

	std::error_code ec;
	const bool listfileExists = fs::exists(listfilePath, ec);
	const bool keysExists = fs::exists(keysPath, ec);
	const auto listfileSize = listfileExists ? fs::file_size(listfilePath, ec) : 0;
	const auto keysSize = keysExists ? fs::file_size(keysPath, ec) : 0;

	const bool listfileMissing = !listfileExists || listfileSize == 0;
	const bool keysMissing = !keysExists || keysSize == 0;

	// Check freshness (older than 7 days)
	constexpr int maxAgeDays = 7;
	using namespace std::chrono;
	const auto now = file_clock::now();

	auto daysSinceModified = [&](const fs::path& p) -> long long {
		const auto lwt = fs::last_write_time(p, ec);
		if (ec)
			return 0;
		return duration_cast<hours>(now - lwt).count() / 24;
	};

	const long long listfileDays = !listfileMissing ? daysSinceModified(listfilePath) : 0;
	const long long keysDays = !keysMissing ? daysSinceModified(keysPath) : 0;
	const bool listfileOutdated = !listfileMissing && listfileDays > maxAgeDays;
	const bool keysOutdated = !keysMissing && keysDays > maxAgeDays;

	if (listfileMissing || keysMissing)
	{
		wxString message = wxT("The following required files are missing:\n\n");
		if (listfileMissing)
			message += wxT("  - listfile.csv\n");
		if (keysMissing)
			message += wxT("  - extraEncryptionKeys.csv\n");
		message += wxT("\nWould you like to download them now?");

		if (wxMessageBox(message, wxT("Missing Files"), wxYES_NO | wxICON_WARNING) == wxYES)
		{
			if (listfileMissing)
				DownloadListfile();
			if (keysMissing)
				DownloadEncryptionKeys();
		}
		else if (listfileMissing)
		{
			wxMessageBox(wxT("The listfile is required for WoW Model Viewer to function correctly.\n"
				"You can download it later from the File menu."),
				wxT("Warning"), wxOK | wxICON_WARNING);
			return false;
		}
	}

	if (listfileOutdated || keysOutdated)
	{
		wxString message = wxT("The following files may be out of date:\n\n");
		if (listfileOutdated)
			message += wxString::Format(wxT("  - listfile.csv (last updated %lld days ago)\n"),
				listfileDays);
		if (keysOutdated)
			message += wxString::Format(wxT("  - extraEncryptionKeys.csv (last updated %lld days ago)\n"),
				keysDays);
		message += wxT("\nWould you like to update them now?");

		if (wxMessageBox(message, wxT("Files May Be Outdated"), wxYES_NO | wxICON_QUESTION) == wxYES)
		{
			if (listfileOutdated)
				DownloadListfile();
			if (keysOutdated)
				DownloadEncryptionKeys();
		}
	}

	return true;
}

void ModelViewer::LoadWoW()
{
	if (!CheckAndUpdateSupportFiles())
		return;

	fileControl->Disable();
	if (gamePath.IsEmpty() || !wxDirExists(gamePath))
	{
		getGamePath();
	}

	if (!core::Game::instance().initDone())
		core::Game::instance().init(new wow::WoWFolder(QString::fromWCharArray(gamePath.c_str())),
		                            new wow::WoWDatabase());

	// init game config
	const std::vector<core::GameConfig> configsFound = GAMEDIRECTORY.configsFound();

	if (configsFound.empty())
	{
		const wxString message = wxString::Format(
			wxT("Fatal Error: Could not find any locale from your World of Warcraft folder"));
		wxMessageDialog dial(nullptr, message, wxT("World of Warcraft No locale found"),
		                                            wxOK | wxICON_ERROR);
		dial.ShowModal();
		return;
	}

	core::GameConfig config = configsFound[0];

	const unsigned int nbConfigs = configsFound.size();

	if (nbConfigs > 1)
	{
		wxString* availableConfigs = new wxString[nbConfigs];
		for (size_t i = 0; i < nbConfigs; i++)
		{
			QString label = configsFound[i].locale + " - " + configsFound[i].product;
			if (configsFound[i].version != "")
				label = label + " (" + configsFound[i].version + ")";
			availableConfigs[i] = wxString(label.toStdWString().c_str());
		}

		const long id = wxGetSingleChoiceIndex(_("Please select a locale:"), _("Locale"), nbConfigs, availableConfigs);
		if (id != -1)
			config = configsFound[id];
		else
			return;
	}

	wxProgressDialog loadProgress(wxT("Loading World of Warcraft"),
		wxT("Opening CASC storage..."), 100, this,
		wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);

	// Helper to map a sub-operation's (current, total) to an absolute progress range
	auto mapProgress = [&loadProgress](int rangeStart, int rangeEnd, const wxString& msg) {
		return [&loadProgress, rangeStart, rangeEnd, msg](int current, int total) {
			if (total > 0)
			{
				const int progress = rangeStart + static_cast<int>(static_cast<int64_t>(current) * (rangeEnd - rangeStart) / total);
				loadProgress.Update(progress, wxString::Format(wxT("%s (%d%%)"), msg, progress));
			}
		};
	};

	if (!GAMEDIRECTORY.setConfig(config))
	{
		const wxString message = wxString::Format(
			wxT("Fatal Error: Could not load your World of Warcraft Data folder (error %d)."),
			GAMEDIRECTORY.lastError());
		wxMessageDialog dial(nullptr, message, wxT("World of Warcraft Not Found"),
													wxOK | wxICON_ERROR);
		dial.ShowModal();
		return;
	}

	LOG_INFO << "Major version:" << GAMEDIRECTORY.majorVersion();
	// check if we are loading a 9.x version of WoW
	if (~GAMEDIRECTORY.majorVersion() >= 9)
	{
		const wxString message = wxString::Format(wxT(
			"This version of WoW Model Viewer is intended to be used with WoW Shadowlands(9.x.x) or above only\n"
			"For older WoW versions support, please refer to this page to pick the right WoW Model Viewer version:\n"
			"https://download.wowmodelviewer.net"));
		wxMessageDialog dial(nullptr, message, wxT("Wrong World of Warcraft version"),
													wxOK | wxICON_ERROR);
		dial.ShowModal();
		return;
	}

	// init game version
	SetStatusText(wxString(GAMEDIRECTORY.version().toStdWString()), 1);

	langName = GAMEDIRECTORY.locale().toStdWString();

	SetStatusText(wxString(GAMEDIRECTORY.locale().toStdWString()), 2);

	// init file list
	QStringList ver = GAMEDIRECTORY.version().split('.');

	const QString baseConfigFolder = "games/wow/" + ver[0] + "." + ver[1] + "/";

	LOG_INFO << "Using following folder to read game info" << baseConfigFolder;
	core::Game::instance().setConfigFolder(baseConfigFolder.toStdString());

	// Range 5-50: Loading file list (heaviest operation)
	loadProgress.Update(5, wxT("Loading file list... (5%)"));
	GAMEDIRECTORY.setProgressCallback(mapProgress(5, 50, wxT("Loading file list...")));
	GAMEDIRECTORY.initFromListfile("../../../listfile.csv");
	GAMEDIRECTORY.setProgressCallback(nullptr);

	// Range 50-55: Loading custom files
	loadProgress.Update(50, wxT("Loading custom files... (50%)"));
	if (!customDirectoryPath.IsEmpty())
		core::Game::instance().addCustomFiles(QString::fromWCharArray(customDirectoryPath.c_str()).toStdString(),
											  customFilesConflictPolicy);

	// Range 55-75: Initializing database
	loadProgress.Update(55, wxT("Initializing database... (55%)"));
	InitDatabase(mapProgress(55, 75, wxT("Initializing database...")));

	// Range 75-80: Filtering files
	loadProgress.Update(75, wxT("Filtering files... (75%)"));
	GAMEDIRECTORY.setProgressCallback(mapProgress(75, 80, wxT("Filtering files...")));

	// Range 80-97: Building file tree
	SetStatusText(wxT("Initializing File Control..."));
	fileControl->Init(this, mapProgress(80, 97, wxT("Building file tree...")));
	GAMEDIRECTORY.setProgressCallback(nullptr);

	// Range 97-100: Character controls
	loadProgress.Update(97, wxT("Initializing character controls... (97%)"));
	if (charControl->Init() == false)
	{
		SetStatusText(wxT("Error Initializing the Character Controls."));
	};
	fileControl->Enable();

	loadProgress.Update(100, wxT("Done. (100%)"));
	SetStatusText(wxT("World of Warcraft loaded successfully."));
}

void ModelViewer::OnCharToggle(wxCommandEvent& event)
{
	const int ID = event.GetId();
	if (ID == ID_VIEW_NPC)
		charControl->selectNPC(UPDATE_NPC);
	if (ID == ID_VIEW_ITEM)
		charControl->selectItem(UPDATE_SINGLE_ITEM, -1);
	else if (isChar)
		charControl->OnCheck(event);
}

void ModelViewer::OnMount(wxCommandEvent& event)
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

void ModelViewer::OnLanguage(wxCommandEvent& event)
{
	if (event.GetId() == ID_LANGUAGE)
	{
		// the arrays should be in sync
		wxCOMPILE_TIME_ASSERT(WXSIZEOF(langNames) == WXSIZEOF(langIds), LangArraysMismatch);

		const long lng = wxGetSingleChoiceIndex(_("Please select a language:"), _("Language"), std::size(langNames),
		                                        langNames);

		if (lng != -1 && lng != interfaceID)
		{
			interfaceID = lng;
			wxMessageBox(
				wxT("You will need to reload WoW Model Viewer for changes to take effect."), wxT("Language Changed"),
				wxOK | wxICON_INFORMATION);
		}
	}
}

void ModelViewer::OnAbout(wxCommandEvent& event)
{
	wxAboutDialogInfo info;
	info.SetName(GLOBALSETTINGS.appName());
	wxString l_version = L"\n" + GLOBALSETTINGS.appVersion() + L" (" + GLOBALSETTINGS.buildName() + L")\n";

	if (GLOBALSETTINGS.isBeta())
		l_version += L"BETA VERSION";

	info.SetVersion(l_version);

	info.AddDeveloper(wxT("Ufo_Z"));
	info.AddDeveloper(wxT("Darjk"));
	info.AddDeveloper(wxT("Chuanhsing"));
	info.AddDeveloper(wxT("Kjasi (A.K.A. Sephiroth3D)"));
	info.AddDeveloper(wxT("Tob.Franke"));
	info.AddDeveloper(wxT("Jeromnimo"));
	info.AddDeveloper(wxT("Wain"));
	info.AddTranslator(wxT("MadSquirrel (French)"));
	info.AddTranslator(wxT("Tigurius (Deutsch)"));
	info.AddTranslator(wxT("Kurax (Chinese)"));

	info.SetWebSite(wxT("https://wowmodelviewer.net"));
	info.SetCopyright(
		wxString(wxT("World of Warcraft(R) is a Registered trademark of\n\
                 Blizzard Entertainment(R). All game assets and content\n\
                 is (C)2004-2016 Blizzard Entertainment(R). All rights reserved.")));

	info.SetLicence(wxT("WoW Model Viewer is released under the GNU General Public License v3, Non-Commercial Use."));

	info.SetDescription(wxT(
		"WoW Model Viewer is a 3D model viewer for World of Warcraft.\nIt uses the data files included with the game to display\nthe 3D models from the game: creatures, characters, spell\neffects, objects and so forth.\n\nCredits To: Linghuye,  nSzAbolcs,  Sailesh, Terran and Cryect\nfor their contributions either directly or indirectly."));

	const wxBitmap* bitmap = createBitmapFromResource(L"ABOUTICON", wxBITMAP_TYPE_XPM, 128, 128);
	wxIcon icon;
	icon.CopyFromBitmap(*bitmap);

info.SetIcon(icon);

wxAboutBox(info);
}

void ModelViewer::OnCanvasSize(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case ID_CANVASS120: SetCanvasSize(120, 120);
		break;
	case ID_CANVASS512: SetCanvasSize(512, 512);
		break;
	case ID_CANVASS1024: SetCanvasSize(1024, 1024);
		break;
	case ID_CANVASF480: SetCanvasSize(640, 480);
		break;
	case ID_CANVASF600: SetCanvasSize(800, 600);
		break;
	case ID_CANVASF768: SetCanvasSize(1024, 768);
		break;
	case ID_CANVASF864: SetCanvasSize(1152, 864);
		break;
	case ID_CANVASF1200: SetCanvasSize(1600, 1200);
		break;
	case ID_CANVASW480: SetCanvasSize(864, 480);
		break;
	case ID_CANVASW720: SetCanvasSize(1280, 720);
		break;
	case ID_CANVASW1080: SetCanvasSize(1920, 1080);
		break;
	case ID_CANVASM768: SetCanvasSize(1280, 768);
		break;
	case ID_CANVASM1200: SetCanvasSize(1900, 1200);
		break;
	default: ;
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
	if (!canvas->model())
		return;
	const WoWModel* m = const_cast<WoWModel*>(canvas->model());
	const wxString fn = wxT("ModelInfo.xml");
	// FIXME: ofstream is not compatible with multibyte path name
	std::ofstream xml(fn.fn_str(), ios_base::out | ios_base::trunc);

	if (!xml.is_open())
	{
		LOG_ERROR << "Unable to open file '" << QString::fromWCharArray(fn.c_str()) << "'. Could not export model.";
		return;
	}

	xml << *m;

	xml.close();
}

// Other things to export...
void ModelViewer::OnExportOther(wxCommandEvent& event)
{
	const int id = event.GetId();
	if (id == ID_FILE_MODEL_INFO)
	{
		ModelInfo();
	}
}

void ModelViewer::UpdateControls()
{
	if (!canvas || !canvas->model() || !canvas->root)
		return;

	WoWModel* m = const_cast<WoWModel*>(canvas->model());
	if (m->modelType == MT_CHAR)
		charControl->RefreshModel();
	else
	{
		//refresh equipment
		for (const auto it : *m)
			it->refresh();
	}
	modelControl->RefreshModel(canvas->root);
}

void ModelViewer::OnExport(wxCommandEvent& event)
{
	if (!g_charControl->model)
	{
		wxMessageBox(
			wxT("You must prepare your model before trying to export it."), wxT("Export Error"), wxOK | wxICON_ERROR);
		return;
	}

	const std::wstring exporterLabel{fileMenu->GetLabel(event.GetId())};

	for (auto* exporter : m_exporters)
	{
		if (exporter->menuLabel() == exporterLabel)
		{
			wxFileDialog saveFileDialog(this, exporter->fileSaveTitle(), L"", L"",
										exporter->fileSaveFilter(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

			if (saveFileDialog.ShowModal() == wxID_CANCEL)
				return;

			// START OF HACK
			// @TODO : remove Hack
			// ugly hack waiting for application to be full Qt, and being able to have qt pop up in plugins...
			// today, creating wxDialog in Qt plugins simply crashes, and no qt app in executed to raised a Qt pop up...

			// if exporter supports animations, we have to chose which one to export
			if (exporter->canExportAnimation())
			{
				WoWModel* m = const_cast<WoWModel*>(canvas->model());
				std::map<int, std::wstring> animsMap = m->getAnimsMap();
				wxArrayString values;
				wxArrayInt selection;
				std::vector<int> ids;
				ids.resize(animsMap.size());

				for (size_t I = 0; I < canvas->model()->anims.size(); I++)
				{
					wxString animName = animsMap[canvas->model()->anims[I].animID];
					animName << L" [";
					animName << I;
					animName << L"]";
					values.Add(animName);
					selection.Add(I);
				}

				AnimationExportChoiceDialog animChoiceDlg(this, L"", wxT("Animation Choice"), values);
				animChoiceDlg.SetSelections(selection);
				if (animChoiceDlg.ShowModal() == wxID_CANCEL)
					return;

				selection = animChoiceDlg.GetSelections();
				vector<int> animsToExport;
				animsToExport.reserve(selection.GetCount());
				for (unsigned int I = 0; I < selection.GetCount(); I++)
					animsToExport.push_back(canvas->model()->anims[selection[I]].Index);

				exporter->setAnimationsToExport(animsToExport);
			}

			// END OF HACK
			WoWModel* m = const_cast<WoWModel*>(canvas->model());
			if (!exporter->exportModel(m, std::wstring(saveFileDialog.GetPath().c_str())))
			{
				wxMessageBox(wxT("An error occurred during export."), wxT("Export Error"), wxOK | wxICON_ERROR);
			}
			else
			{
				wxMessageBox(wxT("Export successfully done."), wxT("Export done"), wxOK | wxICON_INFORMATION);
			}

			break;
		}
	}
}

void ModelViewer::OnStatusBarRefreshTimer(wxTimerEvent& event)
{
	SetStatusText(wxString::Format(wxT("Memory: %i Mo"), core::getMemoryUsed()), 4);
}
