#include "app.h"

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>

#include <windows.h>

#include "Game.h"
#include "GlobalSettings.h"
#include "LogStackWalker.h"
#include "PluginManager.h"
#include "resource1.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"

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
    locale.Init(langIds[interfaceID]);

    wxLocale::AddCatalogLookupPathPrefix(wxT("mo"));
    //wxLocale::AddCatalogLookupPathPrefix(wxT(".."));

    //locale.AddCatalog(wxT("wowmodelview")); // Initialize the catalogs we'll be using
    locale.AddCatalog(locales[interfaceID]);
  }
#endif
}

bool WowModelViewApp::OnInit()
{
	bool displayConsole = false;

	// init next-gen stuff
	GLOBALSETTINGS.bShowParticle = true;
	GLOBALSETTINGS.bZeroParticle = true;

	QCoreApplication::addLibraryPath(QLatin1String("./plugins"));
	frame = NULL;
	wxSplashScreen* splash = NULL;
	{
		wxLogNull logNo;

		wxImage::AddHandler(new wxPNGHandler);
		wxImage::AddHandler(new wxXPMHandler);

		// Enable Randomly choosing between SPLASH and SPLASH2
		bool randomSplash2 = true;

		wxString splashname = L"SPLASH";
		if (randomSplash2 == true)
		{
			srand(time(NULL));
			int randomchoice = rand() % 10;		// Random number between 0-9
			if (randomchoice >= 5)
			{
				splashname = L"SPLASH2";
			}
		}

		wxBitmap * bitmap = createBitmapFromResource(splashname);
		if (!bitmap)
			wxMessageBox(_("Failed to load Splash Screen.\nPress OK to continue loading WMV."), _("Failure"));
		else
			splash = new wxSplashScreen(*bitmap,
				wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_TIMEOUT,
				2000, NULL, -1, wxDefaultPosition, wxDefaultSize,
				wxBORDER_NONE);
		wxYield();
		Sleep(1000); // let's our beautiful spash beeing displayed a few second :)
	}


  // Error & Logging settings
  wxHandleFatalExceptions(true);


  wxString execPath = wxStandardPaths::Get().GetExecutablePath();
  wxFileName fname(execPath);
  wxString userPath = fname.GetPath(wxPATH_GET_VOLUME) + SLASH + wxT("userSettings");
  wxFileName::Mkdir(userPath, 0777, wxPATH_MKDIR_FULL);

  // Application Info
  SetVendorName(wxT("WoWModelViewer"));
  SetAppName(wxT("WoWModelViewer"));

  // set the config file path.
  cfgPath = userPath + SLASH + wxT("Config.ini");
  LoadSettings();

  setInterfaceLocale();
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
  if (icon.LoadFile(wxT("mainicon"), wxBITMAP_TYPE_ICO_RESOURCE) == false)
    wxMessageBox(wxT("Failed to load Icon"), wxT("Failure"));
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
  QString cmd;
  for (int i = 0; i<argc; i++) {
    cmd = QString::fromWCharArray(argv[i]);

    if (cmd == "-m") {
      if (i + 1 < argc) {
        i++;
        QString fn = QString::fromWCharArray(argv[i]);

        // Error check
        if (!fn.endsWith("2")) // Its not an M2 file, exit
          break;

        // Load the model
        frame->LoadModel(GAMEDIRECTORY.getFile(fn));
      }
    }
    else if (cmd == "-mo") {
      if (i + 1 < argc) {                                                       
        i++;
        QString fn = QString::fromWCharArray(argv[i]);

        if (!fn.endsWith("2")) // Its not an M2 file, exit
          break;

        // If its a character model, give it some skin.
        // Load the model
        frame->LoadModel(GAMEDIRECTORY.getFile(fn));

        // Output the screenshot
        fn = "ss_" + fn.replace('\\', '_') + ".png";
        frame->canvas->Screenshot(fn.toStdWString());
      }
    }
    else if (cmd == "-dbfromfile") {
      LOG_INFO << "Read database from file";
      core::Game::instance().init(new wow::WoWFolder(QString::fromWCharArray(gamePath.c_str())), new wow::WoWDatabase());
      GAMEDATABASE.setFastMode();
    }
    else if (cmd == "-console") {
      LOG_INFO << "Displaying console requested";
      displayConsole = true;
    }
    else if (cmd.endsWith(".chr")) {
        frame->LoadChar(cmd);
    }
  }

#if defined(_WINDOWS) 
  if (displayConsole) {
    if (AllocConsole()) {
      freopen("CONOUT$", "w", stdout);
      freopen("CONOUT$", "w", stderr);
      SetConsoleTitle(L"WoWModelViewer Debug Console");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);

      HWND console = GetConsoleWindow();
      RECT r;
      GetWindowRect(console, &r);
      MoveWindow(console, r.left, r.top, 800, 600, TRUE);

      std::wcout.clear();
      std::cout.clear();
      std::wcerr.clear();
      std::cerr.clear();

      LOGGER.addChild(new WMVLog::LogOutputConsole());
    }
  }
#endif

  if (useNewCamera)
    frame->canvas->activateNewCamera();

  // -------
  // Load previously saved layout
  frame->LoadLayout();

  LOG_INFO << "WoW Model Viewer successfully loaded!";

  // check for last version
  if (wxExecute(L"UpdateManager.exe --no-ui", wxEXEC_SYNC) < 0)
    if (wxMessageBox(_("A new version is available, do you want to open Update Manager now ?"), _("Update Software"), wxYES_NO) == wxYES) {
      wxExecute(L"UpdateManager.exe", wxEXEC_SYNC);
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
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

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
  langName = config.value("Locale/LanguageName", "").toString().toStdWString();

  // Application settings
  gamePath = config.value("Settings/Path", "").toString().toStdWString();
  armoryPath = config.value("Settings/ArmoryPath", "").toString().toStdWString();
  customDirectoryPath = config.value("Settings/CustomDirPath", "").toString().toStdWString();
  customFilesConflictPolicy = config.value("Settings/CustomFilesConflictPolicy", 0).toInt();
  displayItemAndNPCId = config.value("Settings/displayItemAndNPCId", 0).toInt();
  ssCounter = config.value("Settings/SSCounter", 100).toInt();
  imgFormat = config.value("Settings/DefaultFormat", 1).toInt();

  useNewCamera = config.value("Unofficial/UseNewCamera", false).toBool();
  if (config.value("Unofficial/UseDoNotTrailInfo", false).toBool() == true)
    ParticleSystem::useDoNotTrailInfo();
}

void WowModelViewApp::SaveSettings()
{
  // Application Config Settings
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);

  config.setValue("Locale/LanguageID", langID);
  config.setValue("Locale/LanguageName", QString::fromWCharArray(langName.c_str()));

  config.setValue("Settings/Path", QString::fromWCharArray(gamePath.c_str()));
  config.setValue("Settings/ArmoryPath", QString::fromWCharArray(armoryPath.c_str()));
  config.setValue("Settings/CustomDirPath", QString::fromWCharArray(customDirectoryPath.c_str()));
  config.setValue("Settings/CustomFilesConflictPolicy", customFilesConflictPolicy);
  config.setValue("Settings/displayItemAndNPCId", displayItemAndNPCId);
  config.setValue("Settings/SSCounter", ssCounter);
  config.setValue("Settings/DefaultFormat", imgFormat);
  config.sync();
}


