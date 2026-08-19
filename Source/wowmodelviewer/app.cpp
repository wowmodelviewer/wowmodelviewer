#include "app.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>

#include <windows.h>

#include "ClientChoiceDialog.h"
#include "ExporterPlugin.h"
#include "Game.h"
#include "GameFolder.h" // core::GameConfig
#include "GlobalSettings.h"
#include "globalvars.h"
#include "LogStackWalker.h"
#include "PluginManager.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "WoWModel.h"

#include "logger/Logger.h"
#include "logger/LogOutputConsole.h"
#include "logger/LogOutputFile.h"

#include <QCoreApplication>
#include <QSettings>
#include <QImage>

#include "TextureManager.h"


/*  THIS IS OUR MAIN "START UP" FILE.
App.cpp creates our wxApp class object.
the wxApp initiates our program (takes over the role of main())
When our wxApp loads,  it creates our ModelViewer class object,
which is a wxWindow.  From there ModelViewer object then creates
our menu bar, character control, view control, filetree control,
animation control, and the canvas control (opengl).  Once those
controls are created it then loads saved variables from the config.ini
file.  Then it proceeds  to create and open the MPQ archives,  creating
a file list of the contents from all files within all of the opened mpq archives.

I hope this gives some insight into the "program flow".
*/
/*
#ifdef _DEBUG
#define new DEBUG_CLIENTBLOCK
#endif
*/

// tell wxwidgets which class is our app
// IMPLEMENT_APP(WowModelViewApp)

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
  fn.Printf(wxT("localisation%c%s.mo"), SLASH, locales[0].c_str());

  if (interfaceID >= 0)
    fn.Printf(wxT("localisation%c%s.mo"), SLASH, locales[interfaceID].c_str());

  if (wxFileExists(fn))
  {
    locale.Init(langIds[interfaceID]);

    wxLocale::AddCatalogLookupPathPrefix(wxT("localisation"));
    //wxLocale::AddCatalogLookupPathPrefix(wxT(".."));

    //locale.AddCatalog(wxT("wowmodelview")); // Initialize the catalogs we'll be using
    locale.AddCatalog(locales[interfaceID]);
  }
#endif
}

void WowModelViewApp::OnAssertFailure(const wxChar *file, int line, const wxChar *func, const wxChar *cond, const wxChar *msg)
{
  // wxWidgets 3.x leaves asserts enabled in release builds and shows a modal dialog by default,
  // which blocks a headless run and is the wrong UX for end users. Record the assert and carry on.
  LOG_ERROR << "wxAssert:" << QString::fromWCharArray(cond ? cond : L"")
            << "|" << QString::fromWCharArray(msg ? msg : L"")
            << "@" << QString::fromWCharArray(file ? file : L"") << ":" << line
            << QString::fromWCharArray(func ? func : L"");
}

// ---- Headless FBX export helpers (used by the out-of-process export child) -----------------
// Write the status sidecar the parent process reads on child exit: "OK" or "ERROR\t<reason>".
// The parent only reads it after the child terminates, so a single write is safe.
static void writeFbxStatus(const QString & outPath, const std::string & content)
{
  std::ofstream f((outPath + ".status").toStdString().c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
  if (f)
    f << content;
}

// Run the FBX export on the already-loaded model with the parent's options/clips, emit progress
// to stdout, and write the status sidecar. Shared by every headless asset branch (-mo/.chr/-npc).
static void doHeadlessFbxExport(ModelViewer * frame, const QString & outPath,
                                bool mesh, bool skel, bool skin, bool anim, const QString & clipsCsv,
                                bool component = false)
{
  WoWModel * m = (frame && frame->canvas) ? const_cast<WoWModel *>(frame->canvas->model()) : NULL;
  ExporterPlugin * plugin = NULL;
  for (PluginManager::iterator pit = PLUGINMANAGER.begin(); pit != PLUGINMANAGER.end(); ++pit)
  {
    ExporterPlugin * p = dynamic_cast<ExporterPlugin *>(*pit);
    if (p && p->menuLabel() == std::wstring(L"FBX...")) { plugin = p; break; }
  }
  if (!m || !plugin)
  {
    LOG_ERROR << "[fbxexport] model or FBX exporter plugin unavailable";
    writeFbxStatus(outPath, "ERROR\tModel or FBX exporter plugin unavailable");
    return;
  }

  plugin->setExportOptions(mesh, skel, skin, anim);
  plugin->setComponentRawExport(component);

  std::vector<int> clips;
  if (anim && !clipsCsv.isEmpty())
  {
    const QStringList parts = clipsCsv.split(',', QString::SkipEmptyParts);
    for (const QString & s : parts)
    {
      bool okc = false;
      const int v = s.trimmed().toInt(&okc);
      if (okc) clips.push_back(v);
    }
  }
  plugin->setAnimationsToExport(clips);

  std::printf("WMVEXPORT-PROGRESS: STAGE START\n"); std::fflush(stdout);
  const bool ok = plugin->exportModel(m, outPath.toStdWString());
  if (ok)
  {
    std::printf("WMVEXPORT-PROGRESS: STAGE DONE\n"); std::fflush(stdout);
    writeFbxStatus(outPath, "OK");
    LOG_INFO << "[fbxexport] SUCCESS:" << qPrintable(outPath);
  }
  else
  {
    const QString err = QString::fromStdWString(plugin->lastError());
    writeFbxStatus(outPath, std::string("ERROR\t") + err.toUtf8().constData());
    LOG_ERROR << "[fbxexport] FAILED:" << qPrintable(err);
  }
}

// Forensic-only: load an arbitrary texture by FileDataID and save it standalone, bypassing ALL
// item/equip/combiner logic -- lets you look at exactly what a "type 1" replaceable texture (or
// any other raw asset) actually contains, independent of whether the normal resolution pipeline
// binds it correctly. -dumptex <fileDataID> <out.png>
static void doHeadlessDumpTexture(int fileDataId, const QString & outPath)
{
  GameFile * f = GAMEDIRECTORY.getFile((uint)fileDataId);
  if (!f)
  {
    std::printf("WMVDUMPTEX: ERROR file %d not found\n", fileDataId); std::fflush(stdout);
    return;
  }

  const GLuint glid = TEXTUREMANAGER.add(f);
  if (glid == 0)
  {
    std::printf("WMVDUMPTEX: ERROR texture %d failed to load\n", fileDataId); std::fflush(stdout);
    return;
  }

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, glid);
  GLint width = 0, height = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  if (width <= 0 || height <= 0)
  {
    std::printf("WMVDUMPTEX: ERROR texture %d has 0x0 dimensions (glid %u)\n", fileDataId, glid);
    std::fflush(stdout);
    glBindTexture(GL_TEXTURE_2D, 0);
    return;
  }

  unsigned char * pixels = new unsigned char[(size_t)width * (size_t)height * 4];
  glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);

  QImage img(pixels, width, height, QImage::Format_ARGB32);
  const bool saved = img.save(outPath);
  delete[] pixels;

  std::printf("WMVDUMPTEX: file %d -> %s %s (%dx%d)\n", fileDataId, qPrintable(outPath),
              saved ? "OK" : "SAVE-FAILED", width, height);
  std::fflush(stdout);
}

// Headless smoke-test for the Image Sequence Exporter pipeline (off-screen, no GUI/timer): loads
// the model, scrubs the current clip, and writes a handful of PNG + one EXR frame via the same
// ModelCanvas::CaptureSequenceFrame() the GUI uses. Validates capture/alpha/EXR/naming without the
// event loop. NOTE: composited character textures don't bake off-screen, so frames may be
// untextured -- this checks geometry/alpha/format/naming, not final colour. -imgseq <folder>
static void doHeadlessImageSeq(ModelViewer * frame, const QString & folder)
{
  WoWModel * m = (frame && frame->canvas) ? const_cast<WoWModel *>(frame->canvas->model()) : NULL;
  if (!m || !m->animManager)
  {
    std::printf("WMVIMGSEQ: ERROR no model/anim\n"); std::fflush(stdout);
    return;
  }
  const size_t ai = m->animManager->GetAnim();
  const unsigned len = (ai < m->anims.size() && m->anims[ai].length > 0) ? m->anims[ai].length : 1000;
  const int n = 6;                  // sample 6 frames across the clip
  m->animManager->Pause(true);
  for (int i = 0; i < n; i++)
  {
    const unsigned t = (unsigned)((double)i / (n - 1) * len);
    m->animManager->SetFrame(t);
    const int fmt = (i == 0) ? 2 : 0; // first frame EXR, rest PNG
    const QString path = folder + (folder.endsWith("/") || folder.endsWith("\\") ? "" : "/")
                       + QString("imgseqtest_%1.%2").arg(i, 4, 10, QChar('0')).arg(fmt == 2 ? "exr" : "png");
    const bool ok = frame->canvas->CaptureSequenceFrame(wxString(path.toStdWString()), 640, 360, fmt, true /* transparent */);
    std::printf("WMVIMGSEQ: frame %d t=%u -> %s %s\n", i, t, qPrintable(path), ok ? "ok" : "FAILED");
    std::fflush(stdout);
  }
  std::printf("WMVIMGSEQ: DONE\n"); std::fflush(stdout);
}

bool WowModelViewApp::OnInit()
{
  bool displayConsole = false;

  // init next-gen stuff
  GLOBALSETTINGS.bShowParticle = true;
  GLOBALSETTINGS.bZeroParticle = true;

  QCoreApplication::addLibraryPath(QLatin1String("./plugins"));
  frame = NULL;

  // Detect a non-interactive run (background FBX export child / CLI harness) as early as
  // possible -- BEFORE the splash screen below -- so a headless run never flashes it on screen.
  // wxSplashScreen shows itself, centred, the instant it's constructed; the later "park the main
  // frame off-screen" logic only ever moved the FRAME, so a headless FBX export (which relaunches
  // this same exe as a background child -- see ExportJobManager) still flashed the splash for its
  // full timeout on every export. Reused below for that frame-parking too, so there is one scan.
  bool earlyHeadless = false;
  for (int ai = 1; ai < argc; ai++)
  {
    QString a = QString::fromWCharArray(argv[ai]);
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" ||
        a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" ||
        a == "-mpq" || a.endsWith(".chr"))
    {
      earlyHeadless = true;
      break;
    }
  }

  wxSplashScreen* splash = NULL;
  {
    wxLogNull logNo;

    wxImage::AddHandler(new wxPNGHandler);
    wxImage::AddHandler(new wxXPMHandler);

    if (!earlyHeadless)
    {
      // Single Midnight splash (both SPLASH and SPLASH2 point to it); no faction RNG.
      bool randomSplash2 = false;

      wxString splashname = L"SPLASH";
      if (randomSplash2 == true)
      {
        srand(time(NULL));
        int randomchoice = rand() % 10;    // Random number between 0-9
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
      // (removed a blind Sleep(1000) here -- the splash has its own 2s timeout and stays
      //  visible while real init runs, so the sleep was ~1s of dead time on every launch.)
    }
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
  LOG_INFO << "Starting:" << QString::fromStdWString(GLOBALSETTINGS.appName().c_str())
    << QString::fromStdWString(GLOBALSETTINGS.appVersion().c_str())
    << QString::fromStdWString(GLOBALSETTINGS.buildName().c_str());


  // Now create our main frame.
  frame = new ModelViewer();

  if (!frame) {
    //this->Close();
    if (splash)
      splash->Show(false);
    return false;
  }

  SetTopWindow(frame);

  // Park a non-interactive run (background FBX export child / CLI harness) off-screen before the
  // window is shown, so it never flashes in front of the user. It stays "shown" (a valid GL
  // drawable, needed for FBX texture read-back) -- just positioned beyond the desktop.
  // (earlyHeadless was computed above, before the splash screen, which it also gates.)
  if (earlyHeadless)
    frame->Move(-32000, -32000);

  /*
  There is a problem with drawing on surfaces that have previously not been showed.
  The error was 'GLXBadDrawable'.
  */
  frame->Show(true);

  // Set the window + taskbar icon. The classic icon API (wxICON/LoadIcon/LoadImage)
  // cannot load the embedded .ico on this build, so the window icon came up blank.
  // Instead build the icon from the ICON3 PNG resource (which loads via wx's own PNG
  // handler), at the exact big/small sizes, and apply it with WM_SETICON. The wxIcons
  // are static so the HICONs they own stay valid for the window's lifetime.
#if defined (_WINDOWS)
  {
    static wxIcon s_iconBig, s_iconSmall;
    wxBitmap * bmp = createBitmapFromResource(L"ICON3");
    if (bmp && bmp->IsOk())
    {
      const wxImage img = bmp->ConvertToImage();
      s_iconBig.CopyFromBitmap(wxBitmap(img.Scale(::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), wxIMAGE_QUALITY_HIGH)));
      s_iconSmall.CopyFromBitmap(wxBitmap(img.Scale(::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), wxIMAGE_QUALITY_HIGH)));
      HWND hwnd = (HWND) frame->GetHandle();
      if (s_iconBig.IsOk())
      {
        ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM) s_iconBig.GetHICON());
        frame->SetIcon(s_iconBig); // title bar + wx-internal
      }
      if (s_iconSmall.IsOk())
        ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) s_iconSmall.GetHICON());
    }
    else
      LOG_ERROR << "Failed to load ICON3 resource -- application icon not set";
  }
#endif
  // --

  // Point our global vars at the correct memory location
  g_canvas = frame->canvas;
  g_animControl = frame->animControl;
  g_charControl = frame->charControl;
  g_fileControl = frame->fileControl;

#ifndef  _LINUX // buggy
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
  QString snapModelPath; // -mo: defer load+screenshot until after LoadWoW
  QString snapArmoryUrl; // -armory <url>: headless import + screenshot (test harness)
  QString snapNpcArg;    // -npc <id|id:displayId>: headless NPC load + screenshot (test harness)
  QString fbxExportPath; // -fbxexport <out.fbx>: headless FBX export of the -mo model (test harness)
  QString imgSeqFolder;  // -imgseq <folder>: headless image-sequence capture smoke test
  QString fbxInspectPath; // -fbxinspect <in.fbx>: read-only forensic dump of an existing FBX (no game data)
  QString animDumpName;   // -animdump <animName>: source-vs-exported per-bone pose diff for the -mo model
  QString snapCharPath;   // <file.chr>: defer LoadChar until AFTER LoadWoW (export/screenshot)
  QString mpqDataFolder;  // -mpq <DataFolder> [locale]: load a legacy MPQ client instead of CASC
  QString mpqLocale;      // optional locale for -mpq (auto-detected when empty)
  int dumpTexFileDataId = 0; QString dumpTexOutPath; // -dumptex <fileDataID> <out.png>: forensic-only
  // Export content selection + clip list for the headless FBX export (the parent process passes
  // these so the child reproduces the user's exact options). Defaults: full content, no explicit
  // clips (the exporter falls back to none/first-N only if -fbxanim and no -fbxclips).
  int optMesh = 1, optSkel = 1, optSkin = 1, optAnim = 1;
  int optComponent = 0;   // -fbxcomponent : opt-in raw/node-based item-component export (UV2 + raw units + sidecar v2)
  int itemSkinFileId = 0; // -itemskin <fileDataID> : re-bind an item/weapon's on-screen skin after -mo load
  QString fbxClipsArg;    // -fbxclips i,j,k : ModelAnimation.Index values to export
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

        // Defer load + screenshot until AFTER LoadWoW() below -- the game data
        // must be loaded before a model can be resolved/composed.
        snapModelPath = fn;
      }
    }
    else if (cmd == "-mpq") {
      // Headless legacy-MPQ load: "-mpq <DataFolder> [locale] -mo <path\model.m2>" opens a
      // Vanilla/TBC/WotLK MPQ install (instead of modern CASC) and loads the -mo model BY NAME
      // from the archive chain. Locale is optional (auto-detected) and, if given, is the token
      // right after the folder that does not start with '-'.
      if (i + 1 < argc) {
        i++;
        mpqDataFolder = QString::fromWCharArray(argv[i]);
        if (i + 1 < argc) {
          const QString nxt = QString::fromWCharArray(argv[i + 1]);
          if (!nxt.startsWith('-')) { i++; mpqLocale = nxt; }
        }
      }
    }
    else if (cmd == "-dumptex") {
      // Forensic-only: "-dumptex <fileDataID> <out.png>" loads game data, saves that texture
      // standalone (no item/equip/combiner context), and exits. See doHeadlessDumpTexture.
      if (i + 2 < argc) {
        dumpTexFileDataId = QString::fromWCharArray(argv[i + 1]).toInt();
        dumpTexOutPath = QString::fromWCharArray(argv[i + 2]);
        i += 2;
      }
    }
    else if (cmd == "-imgseq") {
      // Headless image-sequence capture smoke test: "<asset> -imgseq <folder>" writes a few
      // PNG + EXR frames via ModelCanvas::CaptureSequenceFrame and exits.
      if (i + 1 < argc) {
        i++;
        imgSeqFolder = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-armory") {
      // Headless armory import for testing: load the character from the URL and
      // screenshot it after LoadWoW(). Mirrors -mo. importChar sets hasTransmogGear
      // false so no modal dialog blocks the run.
      if (i + 1 < argc) {
        i++;
        snapArmoryUrl = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-npc") {
      // Headless NPC load for testing: "-npc <id>" loads an NPC already present in the data,
      // or "-npc <id>:<displayId>" first registers a (possibly newer/PTR) NPC by display id.
      if (i + 1 < argc) {
        i++;
        snapNpcArg = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-fbxexport") {
      // Headless FBX export for testing: "-mo <model.m2> -fbxexport <out.fbx>" loads the model
      // then exports it (mesh + skeleton + skinning + up to 5 clips) through the FBX plugin.
      // Pair with the WMV_FBX_SELFTEST environment variable to log a PASS/FAIL re-import check.
      if (i + 1 < argc) {
        i++;
        fbxExportPath = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-animdump") {
      // "-mo <model.m2> -animdump <animName>": pose the source skeleton vs the exported animation
      // for one clip at frame 0/mid/final and log per-bone world-position divergence. Test harness.
      if (i + 1 < argc) {
        i++;
        animDumpName = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-fbxinspect") {
      // Read-only forensic dump of an existing FBX: "-fbxinspect <in.fbx>" re-imports the file
      // and logs per-mesh verts/weights/clusters/bones/parent/bind-pose. Needs NO game data.
      if (i + 1 < argc) {
        i++;
        fbxInspectPath = QString::fromWCharArray(argv[i]);
      }
    }
    else if (cmd == "-fbxmesh")  { if (i + 1 < argc) { i++; optMesh = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxskel")  { if (i + 1 < argc) { i++; optSkel = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxskin")  { if (i + 1 < argc) { i++; optSkin = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxanim")  { if (i + 1 < argc) { i++; optAnim = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxclips") { if (i + 1 < argc) { i++; fbxClipsArg = QString::fromWCharArray(argv[i]); } }
    else if (cmd == "-fbxcomponent") { optComponent = 1; }
    else if (cmd == "-itemskin") {
      // "-mo <item.m2> -itemskin <fileDataID>": after loading the model, re-bind this texture to
      // the item skin slot so the export matches the appearance the GUI had on screen (the out-of-
      // process export child would otherwise reload the raw model with its default skin).
      if (i + 1 < argc) { i++; itemSkinFileId = QString::fromWCharArray(argv[i]).toInt(); }
    }
    else if (cmd == "-build")    {
      // Pin the child to the EXACT build the parent is viewing (the .chr carries no build, and
      // LoadWoW's auto-pick could load a different one, e.g. retail vs a pinned PTR).
      if (i + 1 < argc) { i++; qputenv("WMV_FORCE_BUILD", QString::fromWCharArray(argv[i]).toUtf8()); }
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
        // Defer until after LoadWoW (game data must be loaded before a character composes).
        snapCharPath = cmd;
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

  // -------
  // Load previously saved layout
  frame->LoadLayout();

  LOG_INFO << "WoW Model Viewer successfully loaded!";

  // A model/char/db argument means a non-interactive (CLI) load -- auto-load the game without
  // blocking on the launcher dialog. Otherwise show the Client Choice launcher at startup.
  bool headlessLoad = false;
  for (int i = 1; i < argc; i++)
  {
    QString a = QString::fromWCharArray(argv[i]);
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" || a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" || a == "-mpq" || a.endsWith(".chr"))
    {
      headlessLoad = true;
      break;
    }
  }

  if (headlessLoad)
  {
    frame->batchMode = true; // non-interactive run: suppress modal dialogs that would block it

    // Read-only forensic dump of an existing FBX -- reads the file only, so handle it before
    // LoadWoW (no game data needed) and exit. Plugins are already loaded (ModelViewer ctor).
    if (!fbxInspectPath.isEmpty())
    {
      bool handled = false;
      for (PluginManager::iterator pit = PLUGINMANAGER.begin(); pit != PLUGINMANAGER.end(); ++pit)
      {
        ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*pit);
        if (plugin && plugin->menuLabel() == std::wstring(L"FBX..."))
        {
          plugin->dumpForensics(fbxInspectPath.toStdWString());
          handled = true;
          break;
        }
      }
      if (!handled)
        LOG_ERROR << "[fbxinspect] FBX exporter plugin not available";
      return false; // read-only inspect done -> exit
    }

    if (!mpqDataFolder.isEmpty())
      frame->LoadWoWFromMpq(mpqDataFolder, mpqLocale); // legacy MPQ client (Vanilla/TBC/WotLK)
    else
      frame->LoadWoW(); // auto-pick config + profile, no prompt

    if (!dumpTexOutPath.isEmpty())
    {
      doHeadlessDumpTexture(dumpTexFileDataId, dumpTexOutPath);
      return false; // forensic dump done -> exit
    }

    if (!snapModelPath.isEmpty())
    {
      frame->LoadModel(GAMEDIRECTORY.getFile(snapModelPath));
      LOG_INFO << "[itemskin] after -mo load, SetSkin captured skin fileDataID =" << frame->m_exportItemSkinFileId;

      // Re-bind the item/weapon skin the GUI had on screen (see -itemskin). The raw -mo load above
      // installs the model's DEFAULT skin; overwrite the TEXTURE_OBJECT_SKIN slot so the export
      // uses the exact texture the user was viewing.
      if (itemSkinFileId > 0)
      {
        WoWModel * m = const_cast<WoWModel *>(frame->canvas->model());
        GameFile * skin = GAMEDIRECTORY.getFile((uint)itemSkinFileId);
        if (m && skin)
        {
          m->updateTextureList(skin, TEXTURE_OBJECT_SKIN);
          LOG_INFO << "[itemskin] re-bound skin fileDataID" << itemSkinFileId << "to TEXTURE_OBJECT_SKIN";
        }
        else
          LOG_WARNING << "[itemskin] could not re-bind skin fileDataID" << itemSkinFileId;
      }

      // Headless source-vs-exported animation pose diff: load model, run the FBX plugin's
      // dumpSourcePose for one clip, exit. Forensic only (no file written).
      if (!animDumpName.isEmpty())
      {
        WoWModel * m = const_cast<WoWModel *>(frame->canvas->model());
        for (PluginManager::iterator pit = PLUGINMANAGER.begin(); pit != PLUGINMANAGER.end(); ++pit)
        {
          ExporterPlugin * plugin = dynamic_cast<ExporterPlugin *>(*pit);
          if (plugin && plugin->menuLabel() == std::wstring(L"FBX..."))
          {
            plugin->dumpSourcePose(m, animDumpName.toStdWString());
            break;
          }
        }
        return false;
      }

      // Headless FBX export of the loaded model (out-of-process export child runs this path).
      if (!fbxExportPath.isEmpty())
      {
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0);
        return false; // headless export done -> exit
      }
      if (!imgSeqFolder.isEmpty())
      {
        doHeadlessImageSeq(frame, imgSeqFolder);
        return false;
      }

      QString out = "ss_" + QString(snapModelPath).replace('\\', '_').replace('/', '_') + ".png";
      frame->canvas->Screenshot(out.toStdWString());
      return false; // headless capture done -> exit
    }

    // Character (.chr) headless branch -- compose the saved character, then export or screenshot.
    if (!snapCharPath.isEmpty())
    {
      frame->LoadChar(snapCharPath);
      if (!fbxExportPath.isEmpty())
      {
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0);
        return false;
      }
      if (!imgSeqFolder.isEmpty())
      {
        doHeadlessImageSeq(frame, imgSeqFolder);
        return false;
      }
      QString out = "ss_chr_" + QString(snapCharPath).section('/', -1).section('\\', -1) + ".png";
      frame->canvas->Screenshot(out.toStdWString());
      return false;
    }
    if (!snapArmoryUrl.isEmpty())
    {
      frame->ImportArmoury(wxString::FromUTF8(snapArmoryUrl.toUtf8().constData()));
      QString out = "ss_armory_" + QString(snapArmoryUrl).section('/', -1).replace('?', '_') + ".png";
      frame->canvas->Screenshot(out.toStdWString());
      return false; // headless capture done -> exit
    }
    if (!snapNpcArg.isEmpty())
    {
      const int npcId = snapNpcArg.section(':', 0, 0).toInt();
      const int dispId = snapNpcArg.contains(':') ? snapNpcArg.section(':', 1, 1).toInt() : 0;
      frame->LoadNPCByDisplay(npcId, dispId);
      if (!fbxExportPath.isEmpty())
      {
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0);
        return false;
      }
      QString out = "ss_npc_" + QString(snapNpcArg).replace(':', '_') + ".png";
      frame->canvas->Screenshot(out.toStdWString());
      return false; // headless capture done -> exit
    }
  }
  else
  {
    // Startup client picker. Loop so a failed legacy-MPQ load returns here instead of starting
    // with no client; Cancel/close still exits without loading, exactly as before.
    for (;;)
    {
      ClientChoiceDialog clientDlg(frame);
      if (clientDlg.ShowModal() != wxID_OK)
      {
        LOG_INFO << "Client Choice dialog dismissed without loading a client.";
        break;
      }

      if (clientDlg.isLegacyMpq())
      {
        // Legacy (pre-CASC) MoPaQ client -- the shared helper shows the folder picker and loads it
        // (auto-detects Data subfolder + locale), exactly like File -> Load Legacy MPQ Client...
        // <=0 means cancelled or no archives (the helper shows its own error) -> back to the picker.
        if (frame->PromptAndLoadLegacyMpqClient() <= 0)
          continue;
      }
      else
      {
        gamePath = clientDlg.dataPath();
        core::GameConfig chosen = clientDlg.selectedConfig();
        frame->LoadWoW(&chosen, clientDlg.selectedProfile(), true /* show loading progress */);
      }
      break; // a client loaded
    }
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

  // Optional override for the embedded Unity renderer player exe. Empty (the default) ->
  // resolved at use-time as tools\unity-renderer\UnityRenderer.exe next to the WMV
  // executable, so a moved install keeps finding its bundled player.
  unityRendererPath = config.value("Tools/UnityRendererPath", "").toString().toStdWString();

  // Optional override for the armory importer's proxy URL (the proxy holds the
  // Blizzard credentials server-side). Pushed into the core singleton so the Qt
  // importer plugin can read it; empty -> the plugin uses its built-in default.
  GLOBALSETTINGS.setArmoryProxyURL(config.value("Armory/ProxyURL", "").toString().toStdString());

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

  config.setValue("Tools/UnityRendererPath", QString::fromWCharArray(unityRendererPath.c_str()));

  config.setValue("Armory/ProxyURL", QString::fromStdString(GLOBALSETTINGS.armoryProxyURL()));
  config.sync();
}


