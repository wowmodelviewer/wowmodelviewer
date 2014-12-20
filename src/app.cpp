#include "app.h"
#include <wx/image.h>
#include <wx/splash.h>
#include <wx/mstream.h>
#include <wx/stdpaths.h>
#include <wx/app.h>

#include <windows.h>
#include "util.h"

#include "UserSkins.h"
#include "resource1.h"

#include "core/GlobalSettings.h"
#include "core/PluginManager.h"
#include "logger/Logger.h"
#include "logger/LogOutputConsole.h"
#include "logger/LogOutputFile.h"

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

//#include "globalvars.h"

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
  LOGGER.addChild(new WMVLog::LogOutputConsole());
  LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/log.txt"));

  // be carefull, LOGGER must be instanciated before plugin, to pass pointer to all plugins !
  PLUGINMANAGER.init("./plugins",&LOGGER);

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
#ifndef _DEBUG
	#if wxUSE_ON_FATAL_EXCEPTION
		wxHandleFatalExceptions(true);
	#endif
#endif

	wxStandardPaths sp;
	wxString execPath = sp.GetExecutablePath();
	wxFileName fname(execPath);
	wxString userPath = fname.GetPath(wxPATH_GET_VOLUME)+SLASH+wxT("userSettings");
	wxFileName::Mkdir(userPath, 0777, wxPATH_MKDIR_FULL);

	// Application Info
	SetVendorName(wxT("WoWModelViewer"));
	SetAppName(wxT("WoWModelViewer"));

	// Just a little header to start off the log file.
	wxLogMessage(wxString(wxT("Starting:\n")));
	wxString l_logMess = std::string(GLOBALSETTINGS.appName() 
	+ " " 
	+ GLOBALSETTINGS.appVersion() 
	+ " "
	+ GLOBALSETTINGS.buildName() 
	+ "\n\n").c_str();
	wxLogMessage(l_logMess);

	// set the config file path.
	cfgPath = userPath+SLASH+wxT("Config.ini");

	
	bool loadfail = LoadSettings();
	if (loadfail == true) {
		if (splash)
			splash->Show(false);
		return false;
	}

	setInterfaceLocale();

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
#if defined (_WINDOWS) && !defined(_MINGW)
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
	g_modelViewer = frame;
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
				frame->LoadModel(fn);
			}
		} else if (cmd == wxT("-mo")) {
			if (i+1 < argc) {
				i++;
				wxString fn(argv[i]);

				if (fn.Last() != '2') // Its not an M2 file, exit
					break;

				// If its a character model, give it some skin.
				// Load the model
				frame->LoadModel(fn);

				// Output the screenshot
				fn = wxT("ss_")+fn.AfterLast('\\').BeforeLast('.')+wxT(".png");
				frame->canvas->Screenshot(fn);
			}
		} else {
			wxString tmp = cmd.AfterLast('.');
			if (!tmp.IsNull() && !tmp.IsEmpty() && tmp.IsSameAs(wxT("chr"), false))
				frame->LoadChar(cmd);
		}
	}
	// -------
	// Load previously saved layout
	frame->LoadLayout();

	wxLogMessage(wxT("WoW Model Viewer successfully loaded!\n----\n"));

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
	//wxApp::SetExitOnFrameDelete(false);

	/*
	wxDebugReport report;
    wxDebugReportPreviewStd preview;

	report.AddAll(wxDebugReport::Context_Exception);

    if (preview.Show(report))
        report.Process();
	*/

	if (frame != NULL) {
		frame->Destroy();
		frame = NULL;
	}
}

int WowModelViewApp::OnExit()
{
	SaveSettings();
	
	//if (frame != NULL)
	//	frame->Destroy();

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
    //wxMessageBox(wxT("An unhandled exception was caught, the program will now terminate."), wxT("Unhandled Exception"), wxOK | wxICON_ERROR); 
	wxLogFatalError(wxT("An unhandled exception error has occured."));
}

bool WowModelViewApp::LoadSettings()
{
	wxString tmp;
	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(GetAppName(), wxEmptyString,
                             cfgPath,
                             wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	
	
	if(pConfig)
	{
		// Graphic / Video display settings
		pConfig->SetPath(wxT("/Graphics"));
		pConfig->Read(wxT("FSAA"), &video.curCap.aaSamples, 0);
		pConfig->Read(wxT("AccumulationBuffer"), &video.curCap.accum, 0);
		pConfig->Read(wxT("AlphaBits"), &video.curCap.alpha, 0);
		pConfig->Read(wxT("ColourBits"), &video.curCap.colour, 24);
		pConfig->Read(wxT("DoubleBuffer"), (bool*)&video.curCap.doubleBuffer, 1);	// True
#ifdef _WINDOWS
		pConfig->Read(wxT("HWAcceleration"), &video.curCap.hwAcc, WGL_FULL_ACCELERATION_ARB);
#endif
		pConfig->Read(wxT("SampleBuffer"), (bool*)&video.curCap.sampleBuffer, 0);	// False
		pConfig->Read(wxT("StencilBuffer"), &video.curCap.stencil, 0);
		pConfig->Read(wxT("ZBuffer"), &video.curCap.zBuffer, 16);

		// Application locale info
		pConfig->SetPath(wxT("/Locale"));
		pConfig->Read(wxT("LanguageID"), &langID, -1);
		pConfig->Read(wxT("LanguageName"), &langName, wxEmptyString);
		//pConfig->Read(wxT("InterfaceID"), &interfaceID, 0);
		
		// Application settings
		pConfig->SetPath(wxT("/Settings"));
		pConfig->Read(wxT("Path"), &gamePath, wxEmptyString);
		pConfig->Read(wxT("ArmoryPath"), &armoryPath, wxEmptyString);
		pConfig->Read(wxT("TOCVersion"), &gameVersion, 0);

		pConfig->Read(wxT("UseLocalFiles"), &useLocalFiles, false);
		pConfig->Read(wxT("SSCounter"), &ssCounter, 100);
		//pConfig->Read(wxT("AntiAlias"), &useAntiAlias, true);
		//pConfig->Read(wxT("DisableHWAcc"), &disableHWAcc, false);
		pConfig->Read(wxT("DefaultFormat"), &imgFormat, 0);

		// Clear our ini file config object
		wxDELETE( pConfig );
	}
	return false;
}

void WowModelViewApp::SaveSettings()
{
	// Application Config Settings
	wxFileConfig *pConfig = new wxFileConfig(wxT("Global"), wxEmptyString, cfgPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	
	pConfig->SetPath(wxT("/Locale"));
	pConfig->Write(wxT("LanguageID"), langID);
	pConfig->Write(wxT("LanguageName"), langName);
	pConfig->Write(wxT("InterfaceID"), interfaceID);

	pConfig->SetPath(wxT("/Settings"));
	pConfig->Write(wxT("Path"), gamePath);
	pConfig->Write(wxT("ArmoryPath"), armoryPath);
	pConfig->Write(wxT("TOCVersion"), gameVersion);
	pConfig->Write(wxT("UseLocalFiles"), useLocalFiles);
	pConfig->Write(wxT("SSCounter"), ssCounter);
	//pConfig->Write(wxT("AntiAlias"), useAntiAlias);
	//pConfig->Write(wxT("DisableHWAcc"), disableHWAcc);
	pConfig->Write(wxT("DefaultFormat"), imgFormat);

	// Clear our ini file config object
	wxDELETE( pConfig );
}


