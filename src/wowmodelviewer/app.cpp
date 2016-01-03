#include "app.h"

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>

#include <windows.h>

#include "Game.h"
#include "GameDatabase.h"
#include "GlobalSettings.h"
#include "LogStackWalker.h"
#include "PluginManager.h"
#include "resource1.h"
#include "UserSkins.h"
#include "util.h"

#include "logger/Logger.h"
#include "logger/LogOutputConsole.h"
#include "logger/LogOutputFile.h"

#include <QCoreApplication>
#include <QSettings>

/*	THIS IS OUR MAIN "START UP" FILE.
	App.cpp creates our wxApp class object.
	the wxApp initiates our program (takes over the role of main())
	When our wxApp loads,  it creates our ModelViewer class object,  
	which is a wxWindow.  From there ModelViewer object then creates
	our menu bar, character control, view control, filetree control, 
	animation control, and the canvas control (opengl).  Once those 
	controls are created it then loads saved variables from the config.ini
	file.  Then it proceeds	to create and open the MPQ archives,  creating
	a file list of the contents from all files within all of the opened mpq archives.

	I hope this gives some insight into the "program flow".
*/
/*
#ifdef _DEBUG
	#define new DEBUG_CLIENTBLOCK
#endif
*/

// tell wxwidgets which class is our app
IMPLEMENT_APP(WowModelViewApp)

void dumpStackInLogs()
{
  LOG_ERROR << "---- WALK FROM EXCEPTION -----";
  LogStackWalker sw;
  sw.WalkFromException();
  LOG_ERROR << "---- WALK FROM CURRENT CONTEXT -----";
  sw.Walk();
}

void WowModelViewApp::setInterfaceLocale()
{
	if (interfaceID <= 0)
		return;
#ifdef _WINDOWS
	// This chunk of code is all related to locale translation (if a translation is available).
	// Only use locale for non-english?
	wxString fn;
	fn.Printf(wxT("mo%c%s.mo"), SLASH, locales[0].c_str());

	if (interfaceID >= 0)
		fn.Printf(wxT("mo%c%s.mo"), SLASH, locales[interfaceID].c_str());

		if (wxFileExists(fn))
	{
		locale.Init(langIds[interfaceID], wxLOCALE_CONV_ENCODING);
		
		wxLocale::AddCatalogLookupPathPrefix(wxT("mo"));
		//wxLocale::AddCatalogLookupPathPrefix(wxT(".."));

		//locale.AddCatalog(wxT("wowmodelview")); // Initialize the catalogs we'll be using
		locale.AddCatalog(locales[interfaceID]);
	}
#endif
}

bool WowModelViewApp::OnInit()
{
  // init next-gen stuff
  GLOBALSETTINGS.bShowParticle = true;
  GLOBALSETTINGS.bZeroParticle = true;

#if defined(_WINDOWS) && defined(KEEP_CONSOLE)
  AllocConsole() ;
  AttachConsole( GetCurrentProcessId() ) ;
  freopen( "CON", "w", stdout ) ;
#endif

  QCoreApplication::addLibraryPath(QLatin1String("./plugins"));

	frame = NULL;
	wxSplashScreen* splash = NULL;

	wxImage::AddHandler( new wxPNGHandler);
	wxImage::AddHandler( new wxXPMHandler);

	wxBitmap * bitmap = createBitmapFromResource("SPLASH");
	if(!bitmap)
		wxMessageBox(_("Failed to load Splash Screen.\nPress OK to continue loading WMV."), _("Failure"));
	else
		splash = new wxSplashScreen(*bitmap,
				wxSPLASH_CENTRE_ON_SCREEN|wxSPLASH_TIMEOUT,
				2000, NULL, -1, wxDefaultPosition, wxDefaultSize,
				wxBORDER_NONE);
	wxYield();
	Sleep(1000); // let's our beautiful spash beeing displayed a few second :)
	
	
	// Error & Logging settings
	wxHandleFatalExceptions(true);


	wxStandardPaths sp;
	wxString execPath = sp.GetExecutablePath();
	wxFileName fname(execPath);
	wxString userPath = fname.GetPath(wxPATH_GET_VOLUME)+SLASH+wxT("userSettings");
	wxFileName::Mkdir(userPath, 0777, wxPATH_MKDIR_FULL);

	// Application Info
	SetVendorName(wxT("WoWModelViewer"));
	SetAppName(wxT("WoWModelViewer"));

	// set the config file path.
	cfgPath = userPath+SLASH+wxT("Config.ini");
	LoadSettings();

	setInterfaceLocale();

	LOGGER.addChild(new WMVLog::LogOutputConsole());
	LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log.txt"));

	// Just a little header to start off the log file.
	LOG_INFO << "Starting:" << GLOBALSETTINGS.appName().c_str()
			  		<< GLOBALSETTINGS.appVersion().c_str()
						<< GLOBALSETTINGS.buildName().c_str();


	// Now create our main frame.
	frame = new ModelViewer();

	if (!frame) {
		//this->Close();
		if (splash)
			splash->Show(false);
		return false;
	}

	SetTopWindow(frame);
	/*
	There is a problem with drawing on surfaces that have previously not been showed.
	The error was 'GLXBadDrawable'.
	*/
	frame->Show(true);

	// Set the icon, different source location for the icon under Linux & Mac
	wxIcon icon;
#if defined (_WINDOWS)
	if (icon.LoadFile(wxT("mainicon"),wxBITMAP_TYPE_ICO_RESOURCE) == false)
		wxMessageBox(wxT("Failed to load Icon"),wxT("Failure"));
#elif defined (_LINUX)
	// This probably needs to be fixed...
	//if (icon.LoadFile(wxT("../bin_support/icon/wmv_xpm")) == false)
	//	wxMessageBox(wxT("Failed to load Icon"),wxT("Failure"));
#elif defined (_MAC)
	// Dunno what to do about Macs...
	//if (icon.LoadFile(wxT("../bin_support/icon/wmv.icns")) == false)
	//	wxMessageBox(wxT("Failed to load Icon"),wxT("Failure"));
#endif
	frame->SetIcon(icon);
	// --

	// Point our global vars at the correct memory location
	g_canvas = frame->canvas;
	g_animControl = frame->animControl;
	g_charControl = frame->charControl;
	g_fileControl = frame->fileControl;

#ifndef	_LINUX // buggy
	frame->interfaceManager.Update();
#endif

	if (frame->canvas) {
		frame->canvas->Show(true);
		
		if (!frame->canvas->init)
			frame->canvas->InitGL();

		if (frame->lightControl)
			frame->lightControl->UpdateGL();
	}
	// --

	// TODO: Improve this feature and expand on it.
	// Command arguments
	wxString cmd;
	for (int i=0; i<argc; i++) {
		cmd = argv[i];

		if (cmd == wxT("-m")) {
			if (i+1 < argc) {
				i++;
				wxString fn(argv[i]);

				// Error check
				if (fn.Last() != '2') // Its not an M2 file, exit
					break;

				// Load the model
				frame->LoadModel(GAMEDIRECTORY.getFile(fn.c_str()));
			}
		} else if (cmd == wxT("-mo")) {
			if (i+1 < argc) {
				i++;
				wxString fn(argv[i]);

				if (fn.Last() != '2') // Its not an M2 file, exit
					break;

				// If its a character model, give it some skin.
				// Load the model
				frame->LoadModel(GAMEDIRECTORY.getFile(fn.c_str()));

				// Output the screenshot
				fn = wxT("ss_")+fn.AfterLast('\\').BeforeLast('.')+wxT(".png");
				frame->canvas->Screenshot(fn);
			}
		} else if (cmd == wxT("-dbfromfile")) {
			GAMEDATABASE.setFastMode();
		}
		else {
			wxString tmp = cmd.AfterLast('.');
			if (!tmp.IsNull() && !tmp.IsEmpty() && tmp.IsSameAs(wxT("chr"), false))
				frame->LoadChar(cmd.c_str());
		}
	}
	// -------
	// Load previously saved layout
	frame->LoadLayout();

	LOG_INFO << "WoW Model Viewer successfully loaded!";

	// check for last version
	if(wxExecute("UpdateManager.exe --no-ui",wxEXEC_SYNC) < 0)
	  if(wxMessageBox(_("A new version is available, do you want to open Update Manager now ?"), _("Update Software"), wxYES_NO) == wxYES) {
	    wxExecute("UpdateManager.exe",wxEXEC_SYNC);
	  }


	// Classic Mode?
	if (wxMessageBox(_("Would you like to load World of Warcraft right now?"), _("Load World of Warcraft"), wxYES_NO) == wxYES) {
		frame->LoadWoW();
	}

	return true;
}

void WowModelViewApp::OnFatalException()
{
  LOG_ERROR << __FUNCTION__;
  dumpStackInLogs();

  if (frame != NULL) {
    frame->Destroy();
    frame = NULL;
  }
}

int WowModelViewApp::OnExit()
{
	SaveSettings();

	CleanUp();

	//_CrtMemDumpAllObjectsSince( NULL );

	return 0;
}

/*
void WowModelViewApp::HandleEvent(wxEvtHandler *handler, wxEventFunction func, wxEvent& event) const 
{ 
    try 
    {        
        HandleEvent(handler, func, event); 
	} 
	catch(...) 
	{ 
		wxMessageBox(wxT("An error occured while handling an application event."), wxT("Execption in event handling"), wxOK | wxICON_ERROR); 
		throw; 
	} 
}
*/

void WowModelViewApp::OnUnhandledException() 
{ 
  LOG_ERROR << __FUNCTION__;
  dumpStackInLogs();
  wxMessageBox(wxT("An unhandled exception was caught, the program will now terminate."), wxT("Unhandled Exception"), wxOK | wxICON_ERROR);
}

void WowModelViewApp::LoadSettings()
{
	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	// graphic settings
	video.curCap.aaSamples = config.value("Graphics/FSAA", 0).toInt();
	video.curCap.accum = config.value("Graphics/AccumulationBuffer", 0).toInt();
	video.curCap.alpha = config.value("Graphics/AlphaBits", 0).toInt();
	video.curCap.colour = config.value("Graphics/ColourBits", 24).toInt();
	video.curCap.doubleBuffer = config.value("Graphics/DoubleBuffer", 1).toInt();
#ifdef _WINDOWS
	video.curCap.hwAcc = config.value("Graphics/HWAcceleration", WGL_FULL_ACCELERATION_ARB).toInt();
#endif
	video.curCap.sampleBuffer = config.value("Graphics/SampleBuffer", 0).toInt();
	video.curCap.stencil = config.value("Graphics/StencilBuffer", 0).toInt();
	video.curCap.zBuffer = config.value("Graphics/ZBuffer", 16).toInt();

	// Application locale info
	langID = config.value("Locale/LanguageID", 1).toInt();
	langName = config.value("Locale/LanguageName", "").toString().toStdString().c_str();

	// Application settings
	gamePath = config.value("Settings/Path", "").toString().toStdString().c_str();
	armoryPath = config.value("Settings/ArmoryPath", "").toString().toStdString().c_str();
	customDirectoryPath = config.value("Settings/CustomDirPath", "").toString().toStdString().c_str();
	customFilesConflictPolicy = config.value("Settings/CustomFilesConflictPolicy", 0).toInt();
	displayItemAndNPCId = config.value("Settings/displayItemAndNPCId", 0).toInt();
	ssCounter = config.value("Settings/SSCounter", 100).toInt();
	imgFormat = config.value("Settings/DefaultFormat", 1).toInt();
}

void WowModelViewApp::SaveSettings()
{
	// Application Config Settings
	QSettings config(cfgPath.c_str(), QSettings::IniFormat);

	config.setValue("Locale/LanguageID", langID);
	config.setValue("Locale/LanguageName", langName.c_str());
	
	config.setValue("Settings/Path", gamePath.c_str());
	config.setValue("Settings/ArmoryPath", armoryPath.c_str());
	config.setValue("Settings/CustomDirPath", customDirectoryPath.c_str());
	config.setValue("Settings/CustomFilesConflictPolicy", customFilesConflictPolicy);
	config.setValue("Settings/displayItemAndNPCId", displayItemAndNPCId);
	config.setValue("Settings/SSCounter", ssCounter);
	config.setValue("Settings/DefaultFormat", imgFormat);
}


