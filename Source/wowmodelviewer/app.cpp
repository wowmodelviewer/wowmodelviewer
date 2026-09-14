#include "app.h"

#include <cctype>
#include <cstddef>
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
#include "animcontrol.h"
#include "AnimManager.h"
#include "Attachment.h"
#include "filecontrol.h"
#include "wmo.h"
#include "WMOGroup.h"
#include "WoWModel.h"

#include "logger/Logger.h"
#include "logger/LogOutputConsole.h"
#include "logger/LogOutputFile.h"

#include <QElapsedTimer>

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QImage>

#include "TextureManager.h"
#include "UnityAssetAccess.h"
#include "UnityIpcServer.h"
#include "UnityRendererHost.h"

#include <wx/evtloop.h>
#include <wx/stopwatch.h>


/*  THIS IS OUR MAIN "START UP" FILE.
App.cpp creates our wxApp class object.
the wxApp initiates our program (takes over the role of main())
When our wxApp loads,  it creates our ModelViewer class object,
which is a wxWindow.  From there ModelViewer object then creates
our menu bar, character control, view control, filetree control,
animation control, and the hidden canvas control (the archived OpenGL
viewport, kept for its GL context, the model and the animation clock).  Once those
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

  // The pose the exporter reads is computed, not left over from a drawn frame; see UpdateExportPose.
  frame->UpdateExportPose();

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
// -m2inspect <list.txt> [out.csv]: one model per line, as an internal path or "fdid:<id>".
//
// For each, read the file's raw bytes and print the counts in its M2 header -- geometry, bones,
// animations, colour and transparency tracks, texture transforms, materials, attachments,
// events, lights, cameras, ribbon emitters, particle emitters -- and which sibling chunks it
// carries. It reads the HEADER ONLY and never constructs a WoWModel, so it costs a file open per
// entry and cannot be tripped up by a model the full loader chokes on; sweeping thousands of
// files is a minute's work.
//
// Why it exists: "which model should I test this feature against" is otherwise answered by
// guessing at names. This answers it from the files -- ask for every model whose header says it
// has particle emitters, or texture transforms, or more than one skin profile, and the list is
// evidence rather than a hunch. Forensic and read-only; nothing is written but the report.
// -matrestest: regression checks for RETAIL replaceable-material selection, the
// ItemDisplayInfoModelMatRes -> TextureFileData -> replaceTextures[type] chain.
//
// Why a headless switch and not a unit test: this repository has no C++ test framework and no
// DB2 fixture layer -- every table is read from the installed client. So these are checks against
// the live data, in the same idiom as -m2inspect. They need a client; they assert nothing about
// pixels. Each line is PASS or FAIL and the process exit code is the failure count.
//
// The benchmark values come from build 12.1.0.69587. A future client may legitimately re-author
// them: a failure here means "the data moved, go look", not necessarily "the code broke".
static int doHeadlessMatResTest()
{
  int failures = 0;
  auto check = [&failures](const char * what, bool ok, const QString & detail)
  {
    if (!ok) failures++;
    std::printf("WMVMATRES: %s %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.isEmpty() ? "" : " -- ", qPrintable(detail));
    std::fflush(stdout);
  };

  // 1. the table decoded at all
  sqlResult all = GAMEDATABASE.sqlQuery("SELECT COUNT(*) FROM ItemDisplayInfoModelMatRes");
  const int rows = (all.valid && !all.empty()) ? all.values[0][0].toInt() : 0;
  check("schema: ItemDisplayInfoModelMatRes decodes", rows > 100000,
        QString("%1 rows").arg(rows));

  // 2. TextureType only ever carries M2 replaceable texture types
  sqlResult types = GAMEDATABASE.sqlQuery(
      "SELECT DISTINCT TextureType FROM ItemDisplayInfoModelMatRes ORDER BY TextureType");
  QStringList seen;
  bool typesSane = types.valid && !types.empty();
  for (size_t i = 0; typesSane && i < types.values.size(); i++)
  {
    const int t = types.values[i][0].toInt();
    seen << QString::number(t);
    if (t < 1 || t > 30) typesSane = false;
  }
  check("schema: TextureType values are M2 texture types", typesSane, seen.join(","));

  // 3. ModelIndex is a two-value model selector
  sqlResult mi = GAMEDATABASE.sqlQuery(
      "SELECT MIN(ModelIndex), MAX(ModelIndex) FROM ItemDisplayInfoModelMatRes");
  const bool miOk = mi.valid && !mi.empty() &&
                    mi.values[0][0].toInt() == 0 && mi.values[0][1].toInt() == 1;
  check("schema: ModelIndex is 0..1", miOk,
        mi.valid && !mi.empty() ? QString("%1..%2").arg(mi.values[0][0]).arg(mi.values[0][1]) : "no rows");

  // 4. the benchmark's rows, exactly
  sqlResult bench = GAMEDATABASE.sqlQuery(
      "SELECT ModelIndex, TextureType, MaterialResourcesID FROM ItemDisplayInfoModelMatRes "
      "WHERE ItemDisplayInfoID = 671486 ORDER BY ModelIndex, TextureType");
  QStringList got;
  for (size_t i = 0; bench.valid && i < bench.values.size(); i++)
    got << QString("(%1,%2,%3)").arg(bench.values[i][0]).arg(bench.values[i][1]).arg(bench.values[i][2]);
  check("lookup: ItemDisplayInfo 671486 rows",
        got.join(" ") == "(0,2,797639) (0,3,799300) (1,2,797639) (1,3,799300)", got.join(" "));

  // 5. MaterialResourcesID resolves through TextureFileData
  sqlResult res = GAMEDATABASE.sqlQuery(
      "SELECT FileDataID FROM TextureFileData WHERE MaterialResourcesID = 799300");
  const int fdid = (res.valid && !res.empty()) ? res.values[0][0].toInt() : 0;
  check("lookup: MaterialResourcesID 799300 -> FileDataID 5274037", fdid == 5274037,
        QString::number(fdid));

  // 6. model-index filtering actually filters
  sqlResult idx0 = GAMEDATABASE.sqlQuery(
      "SELECT COUNT(*) FROM ItemDisplayInfoModelMatRes WHERE ItemDisplayInfoID=671486 AND ModelIndex=0");
  sqlResult idx1 = GAMEDATABASE.sqlQuery(
      "SELECT COUNT(*) FROM ItemDisplayInfoModelMatRes WHERE ItemDisplayInfoID=671486 AND ModelIndex=1");
  check("lookup: model-index filtering", idx0.valid && idx1.valid &&
        idx0.values[0][0].toInt() == 2 && idx1.values[0][0].toInt() == 2,
        QString("idx0=%1 idx1=%2").arg(idx0.values[0][0]).arg(idx1.values[0][0]));

  // 7. a display with no rows returns nothing (absence stays absence)
  sqlResult none = GAMEDATABASE.sqlQuery(
      "SELECT COUNT(*) FROM ItemDisplayInfoModelMatRes WHERE ItemDisplayInfoID = 0");
  check("lookup: missing display yields no rows",
        none.valid && !none.empty() && none.values[0][0].toInt() == 0,
        none.valid && !none.empty() ? none.values[0][0] : "query failed");

  // 8. one display, several material types (the weapon benchmark)
  sqlResult multi = GAMEDATABASE.sqlQuery(
      "SELECT TextureType FROM ItemDisplayInfoModelMatRes WHERE ItemDisplayInfoID = 732409 "
      "AND ModelIndex = 0 ORDER BY TextureType");
  QStringList mt;
  for (size_t i = 0; multi.valid && i < multi.values.size(); i++)
    mt << multi.values[i][0];
  check("lookup: one display carries several texture types", mt.join(",") == "2,3,4,24",
        mt.join(","));

  // 9. colour variants: every appearance of the benchmark model pairs type 2 with its own type 3
  sqlResult variants = GAMEDATABASE.sqlQuery(
      "SELECT m.ItemDisplayInfoID, COUNT(DISTINCT m.TextureType) FROM ItemDisplayInfoModelMatRes m "
      "JOIN ItemDisplayInfo idi ON idi.ID = m.ItemDisplayInfoID "
      "JOIN ModelFileData mfd ON idi.ModelResourcesID1 = mfd.ModelResourcesID "
      "WHERE mfd.FileDataID = 5225313 AND m.ModelIndex = 0 GROUP BY m.ItemDisplayInfoID");
  int variantsOk = 0, variantsSeen = 0;
  for (size_t i = 0; variants.valid && i < variants.values.size(); i++)
  {
    variantsSeen++;
    if (variants.values[i][1].toInt() == 2) variantsOk++;
  }
  check("variants: every appearance of the benchmark model names both type 2 and type 3",
        variantsSeen > 0 && variantsOk == variantsSeen,
        QString("%1 of %2 appearances").arg(variantsOk).arg(variantsSeen));

  std::printf("WMVMATRES: %d failure(s)\n", failures);
  std::fflush(stdout);
  return failures;
}

static void doHeadlessM2Inspect(const QString & listPath, const QString & outPath)
{
  QFile in(listPath);
  if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    std::printf("WMVM2: ERROR cannot read %s\n", qPrintable(listPath)); std::fflush(stdout);
    return;
  }
  QFile out(outPath);
  const bool toFile = !outPath.isEmpty() && out.open(QIODevice::WriteOnly | QIODevice::Text);
  const char * head = "name,fileDataID,version,globalFlags,vertices,bones,anims,globalSeqs,colors,"
                      "transparency,texTransforms,textures,materials,texLookup,attachments,events,"
                      "lights,cameras,ribbons,particles,skinProfiles,chunks";
  if (toFile) { out.write(head); out.write("\n"); }
  std::printf("WMVM2: %s\n", head);

  // Everything up to and including ofsParticleEmitters must be present to read; the two fields
  // after it exist only when GlobalModelFlags & 8, so the struct's full size is not required.
  const size_t needed = offsetof(ModelHeader, nTextureCombinerCombos);
  int total = 0, ok = 0;
  while (!in.atEnd())
  {
    const QString line = QString::fromUtf8(in.readLine()).trimmed();
    if (line.isEmpty() || line.startsWith('#')) continue;
    total++;
    UnityAssetAccess::Result r = line.startsWith("fdid:", Qt::CaseInsensitive)
        ? UnityAssetAccess::readByFileDataID(line.mid(5).toInt())
        : UnityAssetAccess::readByPath(line);
    if (!r.ok)
    {
      std::printf("WMVM2: %s,,,,,,,,,,,,,,,,,,,,,ERROR %s\n", qPrintable(line), qPrintable(r.error));
      continue;
    }
    const char * data = r.data.constData();
    const int size = r.data.size();

    // Chunked files (MD21 plus siblings) or a bare MD20 payload.
    int payload = 0, payloadSize = size;
    QString chunks;
    if (size >= 8 && qstrncmp(data, "MD20", 4) != 0)
    {
      int off = 0;
      while (off + 8 <= size)
      {
        char tag[5] = { data[off], data[off + 1], data[off + 2], data[off + 3], 0 };
        quint32 sz = 0; memcpy(&sz, data + off + 4, 4);
        if ((qint64)off + 8 + (qint64)sz > (qint64)size) break;
        chunks += (chunks.isEmpty() ? "" : " ") + QString::fromLatin1(tag);
        if (qstrcmp(tag, "MD21") == 0) { payload = off + 8; payloadSize = (int)sz; }
        off += 8 + (int)sz;
      }
    }
    if (payloadSize < (int)needed)
    {
      std::printf("WMVM2: %s,%d,,,,,,,,,,,,,,,,,,,,ERROR header truncated (%d bytes)\n",
                  qPrintable(line), r.fileDataID, payloadSize);
      continue;
    }
    ModelHeader h;
    memset(&h, 0, sizeof(h));
    memcpy(&h, data + payload, needed);
    const unsigned version = (unsigned)h.version[0] | ((unsigned)h.version[1] << 8)
                           | ((unsigned)h.version[2] << 16) | ((unsigned)h.version[3] << 24);
    const QString row = QString("%1,%2,%3,0x%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19,%20,%21,%22")
        .arg(line).arg(r.fileDataID).arg(version).arg(h.GlobalModelFlags, 0, 16)
        .arg(h.nVertices).arg(h.nBones).arg(h.nAnimations).arg(h.nGlobalSequences)
        .arg(h.nColors).arg(h.nTransparency).arg(h.nTexAnims).arg(h.nTextures)
        .arg(h.nTexFlags).arg(h.nTexLookup).arg(h.nAttachments).arg(h.nEvents)
        .arg(h.nLights).arg(h.nCameras).arg(h.nRibbonEmitters).arg(h.nParticleEmitters)
        .arg(h.nViews).arg(chunks);
    if (toFile) { out.write(row.toUtf8()); out.write("\n"); }
    std::printf("WMVM2: %s\n", qPrintable(row));
    ok++;
  }
  if (toFile) out.close();
  std::printf("WMVM2: %d of %d entries read\n", ok, total);
  std::fflush(stdout);
}

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

// A game file named on the command line: all digits is a FileDataID, anything else a listfile path.
static GameFile * resolveGameFileArg(const QString & arg)
{
  const QString a = arg.trimmed();
  bool numeric = false;
  const int id = a.toInt(&numeric);
  return numeric ? GAMEDIRECTORY.getFile(id) : GAMEDIRECTORY.getFile(a);
}

// Pump the IPC server and the wx queue for n * 10 ms (no event loop runs inside OnInit).
static void pumpIpc(UnityIpcServer * ipc, int n)
{
  for (int i = 0; i < n; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
}

// The player's report on the world-model load with this serial, from the reports gathered so far, pumping
// until it arrives or limitMs passes. "superseded" does not end the wait for a later-serial report, and
// a report for another serial is never taken for this one.
static bool waitMapObjectReport(UnityIpcServer * ipc, const std::vector<UnityIpcServer::MapObjectReport> & reports,
                                int load, long limitMs, UnityIpcServer::MapObjectReport & out)
{
  wxStopWatch w;
  while (true)
  {
    for (const UnityIpcServer::MapObjectReport & r : reports)
      if (r.load == load)
      {
        out = r;
        return true;
      }
    if (w.Time() >= limitMs)
      return false;
    pumpIpc(ipc, 1);
  }
}

// Ask the player what it holds (runtimeState, protocol 4) until an answer satisfies holds or limitMs passes.
// out is the last answer, answered whether any came at all. The player answers in message order after
// whatever the step already waited for, so a switch is normally settled on the first answer; the retry
// covers a character adopted a frame after its scene answer. A runtime left behind stays and still fails.
static bool waitRuntimeState(UnityIpcServer * ipc, const std::vector<UnityIpcServer::RuntimeState> & states,
                             const std::function<bool(const UnityIpcServer::RuntimeState &)> & holds, long limitMs,
                             UnityIpcServer::RuntimeState & out, bool & answered)
{
  answered = false;
  wxStopWatch total;
  while (true)
  {
    const int query = ipc->requestRuntimeState();
    if (query == 0)
      return false;
    wxStopWatch w;
    bool got = false;
    while (!got && w.Time() < 5000)
    {
      pumpIpc(ipc, 1);
      for (const UnityIpcServer::RuntimeState & s : states)
        if (s.query == query)
        {
          out = s;
          got = true;
        }
    }
    answered = answered || got;
    if (got && holds(out))
      return true;
    if (total.Time() >= limitMs)
      return false;
    pumpIpc(ipc, 20);
  }
}

// THE WORLD-MODEL ASSERTIONS shared by the -wmo main check and every wmo step of the lifecycle sequence:
// the report is about this root and this load, it was built, it built the header's group count with no
// group file missing, and afterwards the player holds exactly one runtime -- this world model -- and no
// model. why collects what failed. The host side is checked too: the WMO is what the canvas root and the
// doodad-set list point at, and no model is left on the canvas.
static bool checkMapObjectReport(ModelViewer * frame, const UnityIpcServer::MapObjectReport & r, int load, QString & why)
{
  const WMO * w = frame->canvas ? frame->canvas->wmo : nullptr;
  QStringList bad;
  if (!w)
    bad << "no WMO on the canvas";
  if (r.load != load)
    bad << QString("report is for load %1, expected %2").arg(r.load).arg(load);
  if (r.status != "built")
    bad << QString("status %1 (%2)").arg(r.status, r.reason);
  if (w && r.fileDataID != (int)w->fileDataID)
    bad << QString("fileDataID %1, expected %2").arg(r.fileDataID).arg(w->fileDataID);
  if (w && r.groups != (int)w->nGroups)
    bad << QString("groups %1, MOHD says %2").arg(r.groups).arg(w->nGroups);
  if (r.groupFilesMissing != 0)
    bad << QString("groupFilesMissing %1").arg(r.groupFilesMissing);
  if (r.liveMapObjects != 1)
    bad << QString("liveMapObjects %1, expected 1").arg(r.liveMapObjects);
  if (r.liveModels != 0)
    bad << QString("liveModels %1, expected 0").arg(r.liveModels);
  if (w && g_selWMO != w)
    bad << "g_selWMO is not the canvas WMO";
  if (w && (!frame->canvas->root || frame->canvas->root->model() != (Displayable *)const_cast<WMO *>(w)))
    bad << "canvas root is not attached to the canvas WMO";
  if (frame->canvas && frame->canvas->model())
    bad << "a model is still on the canvas";
  why = bad.join("; ");
  return bad.isEmpty();
}

// WMV_IPCTEST_SEQUENCE="m2:creature/bear/bear.m2;wmo:115058;m2:...;wmo:...;wmo:<another>": after the
// main checks, select each entry exactly as Browse does (FileControl::SelectModelFile / SelectWMOFile) and
// check after each step that the player ended up showing exactly that, with nothing left over:
//   wmo step -- the mapObjectLoaded for the step's load serial passes checkMapObjectReport (built, the
//     right root and group count, no missing group file, liveMapObjects 1 and liveModels 0), the
//     viewport shows it (no notice), and the player's runtimeState answer then still names this root as
//     the world model on screen, no model, and 1 / 0 live runtimes;
//   m2 step -- the host holds the model and no WMO (canvas->wmo, g_selWMO and the root all cleared), the
//     player confirms the model is built and current where it can (a character by its scene answer for the
//     load, any other model with geosets by answering a geoset state for its FileDataID "applied"), and
//     then -- for every model -- its runtimeState answer names this model as the one on screen, no world
//     model, liveMapObjects 0, and liveModels 1 (a character: at least 1, its parts count too). That answer
//     is the only evidence for this step: the next wmo step cannot stand in for it, because adopting a world
//     model disposes any model AND any world model still alive before the counts are taken, so a WMO kept
//     alive under this model would pass there.
//   no step may produce a "failed" world-model report.
// Entries are "m2:" or "wmo:" followed by a listfile path or a FileDataID. "m2!:" / "wmo!:" is a QUICK step:
// selected and left at once, the way a user steps through the Browse tree, so the next load replaces one
// still in flight. It asserts nothing itself; the next waited step then also requires an answer for every
// quick world-model load ("superseded", or "built" if it won the race -- never "failed" or none), and its
// own checks prove the replaced load left nothing behind. So a quick step must be followed by a waited
// one: a sequence ending on a quick step is rejected. Returns whether all steps passed.
static bool doIpcTestLifecycleSequence(ModelViewer * frame, UnityIpcServer * ipc, const QString & spec,
                                       const std::vector<UnityIpcServer::MapObjectReport> & reports)
{
  const QStringList steps = spec.split(';', QString::SkipEmptyParts);
  LOG_INFO << "[unityipc-test] lifecycle sequence:" << steps.size() << "step(s):" << spec;

  std::vector<UnityIpcServer::SceneAck> sceneAcks;
  std::vector<UnityIpcServer::GeosetAck> geoAcks;
  std::vector<UnityIpcServer::RuntimeState> runtimeStates;
  auto previousScene = ipc->onCharacterSceneApplied;
  auto previousGeo = ipc->onGeosetsApplied;
  auto previousRuntime = ipc->onRuntimeState;
  ipc->onRuntimeState = [&runtimeStates, previousRuntime](const UnityIpcServer::RuntimeState & s) {
    runtimeStates.push_back(s);
    if (previousRuntime)
      previousRuntime(s);
  };
  ipc->onCharacterSceneApplied = [&sceneAcks, previousScene](const UnityIpcServer::SceneAck & a) {
    sceneAcks.push_back(a);
    if (previousScene)
      previousScene(a);
  };
  ipc->onGeosetsApplied = [&geoAcks, previousGeo](const UnityIpcServer::GeosetAck & a) {
    geoAcks.push_back(a);
    if (previousGeo)
      previousGeo(a);
  };

  int failed = 0;
  // A quick step asserts nothing and relies on the waited step after it (see above); as the last step it
  // would never be checked at all.
  if (!steps.isEmpty() && steps.last().trimmed().section(':', 0, 0).trimmed().endsWith('!'))
  {
    LOG_ERROR << "[unityipc-test]   the sequence ends on a quick step, which nothing would check: end it on a waited step";
    failed++;
  }
  std::vector<int> quickWmoLoads;   // quick world-model loads not yet answered for
  for (int s = 0; s < steps.size(); s++)
  {
    const QString step = steps[s].trimmed();
    const int colon = step.indexOf(':');
    const QString rawKind = colon > 0 ? step.left(colon).trimmed().toLower() : QString();
    const bool quick = rawKind.endsWith('!');
    const QString kind = quick ? rawKind.left(rawKind.size() - 1) : rawKind;
    const QString target = colon > 0 ? step.mid(colon + 1).trimmed() : QString();
    GameFile * file = target.isEmpty() ? nullptr : resolveGameFileArg(target);
    const size_t reportsBefore = reports.size();
    const int serialBefore = frame->m_unityLoadSerial;
    QElapsedTimer clock;
    clock.start();
    QString why;
    bool ok = true;

    if ((kind != "m2" && kind != "wmo") || !file)
    {
      ok = false;
      why = (kind != "m2" && kind != "wmo") ? "unknown step kind (use m2:, wmo:, m2!: or wmo!:)" : "file not found";
    }
    else if (quick)
    {
      if (kind == "wmo")
        frame->fileControl->SelectWMOFile(file);
      else
        frame->fileControl->SelectModelFile(file);
      pumpIpc(ipc, 5);
      if (kind == "wmo" && frame->m_unityLoadSerial != serialBefore)
        quickWmoLoads.push_back(frame->m_unityLoadSerial);
      LOG_INFO << "[unityipc-test]   step" << (s + 1) << "quick: selected and not waited for";
    }
    else if (kind == "wmo")
    {
      frame->fileControl->SelectWMOFile(file);
      const WMO * w = frame->canvas->wmo;
      const int load = frame->m_unityLoadSerial;
      UnityIpcServer::MapObjectReport r;
      if (!ipc->playerDrawsMapObjects())
      {
        ok = false;
        why = QString("the player (protocol %1) cannot load world models").arg(ipc->playerProtocolVersion());
      }
      else if (!w || load == serialBefore)
      {
        ok = false;
        why = !w ? "no WMO on the canvas after the selection" : "no world-model load was sent";
      }
      else if (!waitMapObjectReport(ipc, reports, load, 180000, r))
      {
        ok = false;
        why = QString("no mapObjectLoaded for load %1 within 180 s").arg(load);
      }
      else
      {
        LOG_INFO << "[unityipc-test]   step" << (s + 1) << "mapObjectLoaded:" << r.describe();
        ok = checkMapObjectReport(frame, r, load, why);
        // The player's own account after the report: this root on screen, no model, one world model.
        const int rootId = (int)w->fileDataID;
        UnityIpcServer::RuntimeState st;
        bool answered = false;
        const bool held = waitRuntimeState(ipc, runtimeStates, [rootId](const UnityIpcServer::RuntimeState & x) {
          return x.liveMapObjects == 1 && x.liveModels == 0 && x.mapObjectFileDataID == rootId && x.modelFileDataID == 0;
        }, 30000, st, answered);
        if (!held)
        {
          ok = false;
          why += QString(why.isEmpty() ? "" : "; ") +
                 (answered ? "the player holds " + st.describe() +
                               QString(", expected world model %1, no model, liveMapObjects 1, liveModels 0").arg(rootId)
                           : QString("the player never answered runtimeState"));
        }
        else
          LOG_INFO << "[unityipc-test]   step" << (s + 1) << "runtime state:" << st.describe();
        pumpIpc(ipc, 10);   // the notice decision that follows a report
        ModelViewer::ViewportNotice notice;
        if (!frame->unityCanDrawCurrentModel(&notice) || frame->unityRendererHost->hasNotice())
        {
          ok = false;
          why += QString(why.isEmpty() ? "" : "; ") + "viewport shows a notice: " +
                 QString::fromWCharArray(frame->unityRendererHost->noticeTitle().c_str());
        }
      }
    }
    else
    {
      frame->fileControl->SelectModelFile(file);
      const WoWModel * m = frame->canvas->model();
      const int fdid = file->fileDataId();
      QStringList bad;
      if (!m || !m->gamefile || m->gamefile->fileDataId() != fdid)
        bad << "the canvas does not hold the model";
      if (frame->canvas->wmo || frame->isWMO)
        bad << "a WMO is still loaded on the host";
      if (g_selWMO)
        bad << "g_selWMO still set";
      if (frame->canvas->root && frame->canvas->root->model())
        bad << "the canvas root still holds a model object";

      QString confirmed = "unconfirmed";
      if (bad.isEmpty() && frame->canvasShowsCharacter() && ipc->playerDressesCharacters())
      {
        const int load = frame->m_unityLoadSerial;
        wxStopWatch w;
        bool got = false;
        while (!got && w.Time() < 60000)
        {
          pumpIpc(ipc, 1);
          for (const UnityIpcServer::SceneAck & a : sceneAcks)
            got = got || (a.load == load && a.status == "applied");
        }
        if (got)
          confirmed = QString("character scene applied for load %1").arg(load);
        else
          bad << QString("no applied character scene for load %1 within 60 s").arg(load);
      }
      else if (bad.isEmpty() && ipc->playerSwitchesSubmeshes() && m->geosets.size() > 0)
      {
        wxStopWatch w;
        bool got = false;
        QString last;
        while (!got && w.Time() < 60000)
        {
          const int revision = frame->SendCurrentGeosetsToUnity();
          if (revision == 0)
            break;
          wxStopWatch answer;
          bool settled = false;
          while (!settled && answer.Time() < 10000)
          {
            pumpIpc(ipc, 1);
            for (const UnityIpcServer::GeosetAck & a : geoAcks)
              if (a.revision == revision && a.status != "pending")
              {
                settled = true;
                got = a.status == "applied" && a.fileDataID == fdid;
                last = QString("rev %1 %2 %3").arg(revision).arg(a.status, a.reason);
              }
          }
          if (!got)
            pumpIpc(ipc, 30);
        }
        if (got)
          confirmed = "geoset state applied for fileDataID " + QString::number(fdid) + " (" + last + ")";
        else
          bad << "the player never answered a geoset state for the model as built (" + last + ")";
      }
      else if (bad.isEmpty())
      {
        // Nothing the player answers for this model: wait for its fetching to go quiet instead.
        int requests = ipc->stats().requests;
        wxStopWatch quiet, total;
        while (quiet.Time() < 2000 && total.Time() < 60000)
        {
          pumpIpc(ipc, 5);
          if (ipc->stats().requests != requests)
          {
            requests = ipc->stats().requests;
            quiet.Start();
          }
        }
      }

      // WHAT THE PLAYER HOLDS NOW: this model and no world model. The answers above carry no counts, and
      // for a model with neither geosets nor a character scene this is the only confirmation there is.
      // A player older than protocol 4 cannot answer, and cannot have drawn a world model either.
      if (bad.isEmpty() && ipc->playerDrawsMapObjects())
      {
        const bool character = frame->canvasShowsCharacter();
        UnityIpcServer::RuntimeState st;
        bool answered = false;
        const bool held = waitRuntimeState(ipc, runtimeStates, [fdid, character](const UnityIpcServer::RuntimeState & x) {
          return x.liveMapObjects == 0 && x.mapObjectFileDataID == 0 && x.modelFileDataID == fdid &&
                 (character ? x.liveModels >= 1 : x.liveModels == 1);
        }, 30000, st, answered);
        if (!answered)
          bad << "the player never answered runtimeState";
        else if (!held)
          bad << "the player holds " + st.describe() +
                   QString(", expected model %1, no world model, liveMapObjects 0, liveModels %2")
                     .arg(fdid).arg(character ? ">= 1" : "1");
        else
          confirmed = (confirmed == "unconfirmed" ? QString() : confirmed + "; ") + "runtime state " + st.describe();
      }
      ok = bad.isEmpty();
      why = bad.join("; ");
      LOG_INFO << "[unityipc-test]   step" << (s + 1) << "model:" << confirmed;
    }

    // Every quick world-model load before a waited step must have been answered by now, and not "failed".
    if (!quick && file && (kind == "m2" || kind == "wmo"))
    {
      for (int quickLoad : quickWmoLoads)
      {
        UnityIpcServer::MapObjectReport q;
        if (!waitMapObjectReport(ipc, reports, quickLoad, 5000, q))
        {
          ok = false;
          why += QString(why.isEmpty() ? "" : "; ") + QString("no answer for the replaced load %1").arg(quickLoad);
        }
        else
          LOG_INFO << "[unityipc-test]   step" << (s + 1) << "replaced quick load" << quickLoad << "answered:"
                   << q.status << q.reason;
      }
      quickWmoLoads.clear();
    }

    // A failed world-model build anywhere in the step fails it, whichever load it names.
    for (size_t i = reportsBefore; i < reports.size(); i++)
      if (reports[i].status == "failed")
      {
        ok = false;
        why += QString(why.isEmpty() ? "" : "; ") + "failed report: " + reports[i].describe();
      }

    if (!ok)
      failed++;
    // Free text (the step, the reasons) is concatenated, never passed through arg().
    const QString line = QString("step %1/%2 ").arg(s + 1).arg(steps.size()) + step +
                         QString(" -> %1 in %2 ms; host wmo=%3 g_selWMO=%4 model=%5 load serial %6 -> %7")
                           .arg(ok ? "OK" : "FAIL").arg(clock.elapsed())
                           .arg(frame->canvas->wmo ? 1 : 0).arg(g_selWMO ? 1 : 0).arg(frame->canvas->model() ? 1 : 0)
                           .arg(serialBefore).arg(frame->m_unityLoadSerial) +
                         (why.isEmpty() ? QString() : " -- " + why);
    if (ok)
      LOG_INFO << "[unityipc-test]  " << line;
    else
      LOG_ERROR << "[unityipc-test]  " << line;
    pumpIpc(ipc, 20);
  }

  ipc->onCharacterSceneApplied = previousScene;
  ipc->onGeosetsApplied = previousGeo;
  ipc->onRuntimeState = previousRuntime;
  const bool pass = failed == 0 && !steps.isEmpty();
  LOG_INFO << "[unityipc-test] lifecycle sequence:" << steps.size() << "step(s)," << failed << "failed"
           << (pass ? "(OK)" : "(FAIL)");
  return pass;
}

// -mo <model> -unityipctest: end-to-end self-test of the embedded Unity renderer's runtime
// asset access, using whatever player build is installed (the Unity-free TestStub or a real
// Unity build). Launches the player into the Unity viewport (already the centre pane) exactly as
// the app's own start-up does (ModelViewer::StartUnityRenderer), then pumps until the player has
// connected, announced unityReady, received loadWoWModel for the loaded model, requested it and got
// an assetResponse (or a timeout). The player is launched with -wmvSelfTest, so a diagnostic-capable
// player (the TestStub) also probes the error paths -- a missing asset and an unknown message type --
// which a normal launch never does. The missing-asset and by-FileDataID paths are additionally
// exercised in-process, and the VIEWPORT check confirms the Unity viewport is the centre pane, that
// no viewport toggle exists, that the archived canvas is hidden, unmanaged and never painted, that
// its clock still ticks, and that the loaded model is shown rather than a notice. Everything is
// logged with the [unityipc-test] prefix. The asset exchange itself touches no files on disk
// (runtime access, not an export). The frame is parked off-screen in this mode, so nothing shows up
// on the desktop.
//
// With -wmo <root path or FileDataID> instead of -mo, the world model is selected exactly as Browse does
// (FileControl::SelectWMOFile) and the WORLD-MODEL check waits for the player's mapObjectLoaded for that
// load and asserts it (checkMapObjectReport), logging every field. WMV_IPCTEST_SEQUENCE adds the lifecycle
// sequence (doIpcTestLifecycleSequence) after all other checks, in either mode.
static void doHeadlessUnityIpcTest(ModelViewer * frame)
{
  LOG_INFO << "[unityipc-test] starting -- player:" << QString::fromWCharArray(UnityRendererHost::resolveUnityExePath().c_str());
  if (!frame->unityRendererHost || !frame->StartUnityRenderer(/* selfTest */ true))
  {
    LOG_ERROR << "[unityipc-test] RESULT: FAIL (player could not be launched:"
              << QString::fromWCharArray(frame->unityRendererHost ? frame->unityRendererHost->playerProblem().c_str()
                                                                    : L"no viewport") << ")";
    return;
  }
  UnityIpcServer * ipc = frame->unityRendererHost->ipc();
  if (!ipc || !ipc->isListening())
  {
    LOG_ERROR << "[unityipc-test] RESULT: FAIL (IPC server not listening)";
    return;
  }
  // Every world-model report of the run, in arrival order (the app's own handler still runs).
  std::vector<UnityIpcServer::MapObjectReport> mapReports;
  const auto previousMapObject = ipc->onMapObjectLoaded;
  ipc->onMapObjectLoaded = [&mapReports, previousMapObject](const UnityIpcServer::MapObjectReport & r) {
    mapReports.push_back(r);
    if (previousMapObject)
      previousMapObject(r);
  };

  // Pump: no event loop runs inside OnInit, so drive the server + the wx message queue by hand.
  const long timeoutMs = 20000;
  wxStopWatch sw;
  while (sw.Time() < timeoutMs)
  {
    ipc->poll();
    wxTheApp->Yield(true);
    wxMilliSleep(10);
    const UnityIpcServer::Stats & st = ipc->stats();
    if (st.responsesOk + st.responsesError >= 1)
      break;
    // A world model the player was not sent (an older player, an unreadable root) fetches nothing; the
    // load decision is made synchronously on unityReady, so there is nothing more to wait for.
    if (ipc->isUnityReady() && frame->isWMO && frame->canvas && frame->canvas->wmo && st.mapObjectLoads == 0)
      break;
    if (!frame->unityRendererHost->isRunning())
    {
      LOG_ERROR << "[unityipc-test] player exited before completing the exchange";
      break;
    }
  }
  // let the response drain and the player log it before we look at the counters
  for (int i = 0; i < 50; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

  // A WORLD MODEL (-wmo): selected exactly as Browse does before this test started, so the player was
  // sent the root when it announced itself. Wait for its answer to that load and check it: built, the
  // root's own group count (MOHD) with no group file missing, the right root, and the player holding this
  // one runtime and no model. Every field of the report is logged. Checked before the viewport, whose
  // decision a failed build changes.
  bool wmoOk = true;
  const bool wmoMode = frame->isWMO && frame->canvas && frame->canvas->wmo;
  if (wmoMode)
  {
    WMO * w = frame->canvas->wmo;
    const int load = frame->m_unityLoadSerial;
    LOG_INFO << "[unityipc-test] world-model check:" << w->itemName() << "root FileDataID" << w->fileDataID
             << "host metadata: ok=" << (w->ok ? 1 : 0) << "metadataOnly=" << (w->metadataOnly ? 1 : 0)
             << "groups=" << w->nGroups << "materials=" << w->nTextures << "doodadSets=" << (int)w->doodadsets.size()
             << "doodads=" << (int)w->modelis.size() << "lights=" << (int)w->lights.size()
             << "GFID=" << (int)w->groupFileDataIDs.size() << "| player protocol" << ipc->playerProtocolVersion()
             << "wmo loads sent" << ipc->stats().mapObjectLoads << "load serial" << load;
    // The metadata-only promise: no group file opened, so no group holds geometry or a display list.
    bool groupsEmpty = true;
    for (size_t g = 0; w->groups && g < w->nGroups; g++)
      groupsEmpty = groupsEmpty && w->groups[g].nVertices == 0 && !w->groups[g].ok;
    UnityIpcServer::MapObjectReport report;
    QString why;
    if (!ipc->playerDrawsMapObjects())
      why = QString("the player (protocol %1) cannot load world models").arg(ipc->playerProtocolVersion());
    else if (ipc->stats().mapObjectLoads < 1 || load <= 0)
      why = "no world-model load was sent";
    else if (!waitMapObjectReport(ipc, mapReports, load, 180000, report))
      why = QString("no mapObjectLoaded for load %1 within 180 s").arg(load);
    else
    {
      LOG_INFO << "[unityipc-test]   mapObjectLoaded:" << report.describe();
      checkMapObjectReport(frame, report, load, why);
      pumpIpc(ipc, 20);   // the notice decision a report may cause
    }
    if (!groupsEmpty)
      why += QString(why.isEmpty() ? "" : "; ") + "the host built group geometry (metadata-only expected)";
    for (const UnityIpcServer::MapObjectReport & r : mapReports)
      if (r.status == "failed")
        why += QString(why.isEmpty() ? "" : "; ") + "failed report for load " + QString::number(r.load);
    wmoOk = why.isEmpty();
    if (wmoOk)
      LOG_INFO << "[unityipc-test] world-model check: (OK)";
    else
      LOG_ERROR << "[unityipc-test] world-model check: (FAIL)" << why;
  }

  const UnityIpcServer::Stats & st = ipc->stats();
  bool geosetLiveOk = true;   // set by the live geoset check below, when the model has submeshes to switch
  LOG_INFO << "[unityipc-test] connections=" << st.connections << "unityReady=" << (ipc->isUnityReady() ? 1 : 0)
           << "requests=" << st.requests << "ok=" << st.responsesOk << "errors=" << st.responsesError
           << "bytesServed=" << (qlonglong)st.bytesServed << "provider=" << st.lastProvider
           << "lastRequest=" << st.lastRequest << "lastError=" << st.lastError
           << "elapsedMs=" << (long)sw.Time();

  // THE VIEWPORT. The Unity viewport is the only viewport: it must be the shown centre pane, no toggle
  // may exist to hand the centre to anything else, and the archived OpenGL canvas must be neither a
  // pane nor a shown window -- and must never have been painted. The canvas is still the model owner
  // and the clock the renderer mirrors, so its clock must keep ticking with nothing drawn (the state
  // pushes that clock drives must keep arriving too). And for the loaded model, the viewport decision
  // must be "the model", not a notice. A frozen clock here would look exactly like a frozen viewport.
  bool viewportOk = true;
  {
    ModelViewer::ViewportNotice notice;
    const bool drawable = frame->unityCanDrawCurrentModel(&notice);
    const bool noticeUp = frame->unityRendererHost->hasNotice();
    const bool centre = frame->isUnityViewportCentre();

    bool canvasPane = false;
    wxAuiPaneInfoArray & panes = frame->interfaceManager.GetAllPanes();
    for (size_t i = 0; i < panes.GetCount(); i++)
      canvasPane = canvasPane || panes.Item(i).window == frame->canvas || panes.Item(i).name == wxT("canvas");
    const bool canvasShown = frame->canvas && frame->canvas->IsShown();

    // A toggle is a menu item; look for the old one by its label, and for any item that would name a
    // main-viewport choice.
    bool toggle = false;
    if (frame->menuBar)
      for (size_t m = 0; m < frame->menuBar->GetMenuCount(); m++)
      {
        const wxMenuItemList & items = frame->menuBar->GetMenu(m)->GetMenuItems();
        for (wxMenuItemList::compatibility_iterator it = items.GetFirst(); it; it = it->GetNext())
        {
          const wxString label = it->GetData()->GetItemLabelText().Lower();
          toggle = toggle || label.Contains(wxT("main viewport"));
        }
      }

    const wxSize hostSize = frame->unityRendererHost->GetClientSize();
    LOG_INFO << "[unityipc-test] viewport check: unityCentre=" << (centre ? 1 : 0)
             << "hostSize=" << hostSize.x << "x" << hostSize.y
             << "viewportToggle=" << (toggle ? 1 : 0)
             << "canvasPane=" << (canvasPane ? 1 : 0) << "canvasShown=" << (canvasShown ? 1 : 0)
             << "canvasInit=" << ((frame->canvas && frame->canvas->init) ? 1 : 0)
             << "videoRender=" << (video.render ? 1 : 0)
             << "decision=" << (drawable ? "model" : "notice")
             << "notice=" << QString::fromWCharArray((drawable ? frame->unityRendererHost->noticeTitle()
                                                               : notice.title).c_str())
             << "noticeUp=" << (noticeUp ? 1 : 0);

    // The clock: read the loaded model's frame across a stretch of pumping, playing.
    const WoWModel * cm = frame->canvas ? frame->canvas->model() : NULL;
    bool clockOk = true;
    if (cm && cm->animManager && !cm->anims.empty())
    {
      const bool paused = cm->animManager->IsPaused();
      const size_t before = cm->animManager->GetFrame();
      const int statesBefore = ipc->stats().statePushes;
      const unsigned long ticksBefore = ModelCanvas::s_clockTicks;
      {
        // This runs inside OnInit, before the application's event loop exists, and wxYield without an
        // active loop does not dispatch the timer's messages: the clock cannot move however long the
        // pump runs (0 ticks over 300 ms when tried; the reason this measurement used to read "not
        // ticking in a headless run"). A loop activated for the measurement dispatches like the running
        // app does -- timers, and any paint the hidden canvas might receive, which the paint check at
        // the end of the run would then catch.
        wxGUIEventLoop measureLoop;
        wxEventLoopActivator activate(&measureLoop);
        for (int i = 0; i < 120; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
      }
      const size_t after = cm->animManager->GetFrame();
      const unsigned long ticks = ModelCanvas::s_clockTicks - ticksBefore;
      clockOk = paused || after != before;
      LOG_INFO << "[unityipc-test]   canvas clock (hidden, never painted):" << (qulonglong)ticks << "timer ticks, frame"
               << (int)before << "->" << (int)after
               << (paused ? "(paused, not measured)" : (after != before ? "(ticking)" : "(NOT TICKING)"))
               << "| statePushes +" << (ipc->stats().statePushes - statesBefore)
               << "timerRunning=" << ((frame->canvas && frame->canvas->timer.IsRunning()) ? 1 : 0);
    }
    else
      LOG_INFO << "[unityipc-test]   canvas clock: the model has no animation to measure it with";

    // The hidden canvas must still hold a working GL context: every texture the player is sent and the
    // character's composited images are decoded through it.
    const bool glOk = frame->canvas && frame->canvas->init && video.render;
    viewportOk = centre && !toggle && !canvasPane && !canvasShown && drawable && !noticeUp && clockOk && glOk;
    LOG_INFO << "[unityipc-test] viewport check:" << (viewportOk ? "(OK)" : "(FAIL)");
  }

  // Direct checks (no player involved): a clean error for a missing asset, and the by-FileDataID path.
  {
    const UnityAssetAccess::Result miss = UnityAssetAccess::readByPath("creature/chicken/does_not_exist.m2");
    LOG_INFO << "[unityipc-test] missing-asset check: ok=" << (miss.ok ? 1 : 0) << "error=" << miss.error
             << "provider=" << miss.provider;
    if (frame->canvas && frame->canvas->model() && frame->canvas->model()->gamefile)
    {
      const int fdid = frame->canvas->model()->gamefile->fileDataId();
      const UnityAssetAccess::Result byId = UnityAssetAccess::readByFileDataID(fdid > 0 ? fdid : 0);
      LOG_INFO << "[unityipc-test] by-FileDataID check: fileDataID=" << fdid << "ok=" << (byId.ok ? 1 : 0)
               << "bytes=" << byId.data.size() << "path=" << byId.path << "error=" << byId.error;

      // Replaceable-texture metadata: modern M2s do not name creature skins, so the renderer
      // asks WMV to resolve them from the client database (getModelTextures over IPC).
      std::vector<UnityAssetAccess::ModelTexture> modelTextures;
      QString texError;
      const bool texOk = UnityAssetAccess::resolveModelTextures(fdid, modelTextures, texError);
      LOG_INFO << "[unityipc-test] model-textures check: ok=" << (texOk ? 1 : 0)
               << "count=" << (int)modelTextures.size() << "error=" << texError;
      for (size_t ti = 0; ti < modelTextures.size(); ti++)
      {
        const UnityAssetAccess::Result tex = UnityAssetAccess::readByFileDataID(modelTextures[ti].fileDataID);
        LOG_INFO << "[unityipc-test]   texture[" << (int)modelTextures[ti].index << "] type="
                 << modelTextures[ti].type << "fileDataID=" << modelTextures[ti].fileDataID
                 << "ok=" << (tex.ok ? 1 : 0) << "bytes=" << tex.data.size() << "path=" << tex.path
                 << "source=" << UnityAssetAccess::sourceName(modelTextures[ti].source);
      }

      // Selected-skin sync: a creature normally has several skins and the viewport shows ONE of
      // them. Walk the app's own selector and confirm that what it selects is what gets resolved
      // for the renderer -- and that each change is pushed to the player.
      AnimControl * ac = frame->animControl;
      const int skins = ac ? ac->skinCount() : 0;
      LOG_INFO << "[unityipc-test] skin-sync check: the selector offers" << skins << "skin(s)";
      for (int s = 0; s < skins; s++)
      {
        ac->SetSkin(s);
        std::vector<UnityAssetAccess::ModelTexture> sel;
        QString selError;
        const bool selOk = UnityAssetAccess::resolveModelTextures(fdid, sel, selError);
        const int selFdid = (selOk && !sel.empty()) ? sel[0].fileDataID : 0;

        // Two variants of a creature can differ by GEOMETRY rather than texture, so report the
        // geoset set alongside the texture -- that is what the renderer has to mirror.
        std::vector<int> geosets;
        UnityAssetAccess::selectedModelGeosets(fdid, geosets);
        QString geoStr;
        for (size_t gi = 0; gi < geosets.size(); gi++)
          geoStr += (gi ? "," : "") + QString::number(geosets[gi]);
        if (geoStr.isEmpty())
          geoStr = "none";

        LOG_INFO << "[unityipc-test]   skin[" << s << "]"
                 << QString::fromWCharArray(ac->skinName(s).c_str()) << "-> fileDataID="
                 << selFdid << "type=" << ((selOk && !sel.empty()) ? sel[0].type : 0)
                 << "source=" << ((selOk && !sel.empty())
                                  ? UnityAssetAccess::sourceName(sel[0].source) : "none")
                 << "path=" << (selFdid > 0 ? UnityAssetAccess::readByFileDataID(selFdid).path : QString())
                 << "geosets=" << geoStr;
        // let the push reach the player before selecting the next one
        for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
      }
      // Re-select what is already selected. WMV pushes regardless -- it cannot know what the
      // player is holding -- so this checks the other half of the contract: the player must
      // recognise the texture it already has and fetch nothing.
      if (skins > 0)
      {
        ac->SetSkin(skins - 1);
        for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
        LOG_INFO << "[unityipc-test]   re-selected skin[" << (skins - 1)
                 << "] -- the player should report it unchanged and fetch nothing";
      }
      // Display-id lookup: NPC and Armory import select a skin by CreatureDisplayInfo id
      // (SetSkinByDisplayID), not by list position, so the map that translates one into the
      // other has to be keyed on real display ids. Report what it holds, then round-trip one
      // id through it and check the skin that comes out is the one that id names.
      if (ac)
      {
        std::vector<std::pair<int, int> > displayMap;
        ac->displayIdSkinIndices(displayMap);
        LOG_INFO << "[unityipc-test] display-map check:" << (int)displayMap.size()
                 << "display id(s) known to the skin selector";
        for (size_t di = 0; di < displayMap.size() && di < 5; di++)
          LOG_INFO << "[unityipc-test]   display" << displayMap[di].first << "-> skin["
                   << displayMap[di].second << "]"
                   << QString::fromWCharArray(ac->skinName(displayMap[di].second).c_str());

        if (!displayMap.empty())
        {
          const int probeId = displayMap[0].first;
          ac->SetSkinByDisplayID(probeId);
          std::vector<UnityAssetAccess::ModelTexture> after;
          QString afterError;
          const bool afterOk = UnityAssetAccess::resolveModelTextures(fdid, after, afterError);
          const int afterFdid = (afterOk && !after.empty()) ? after[0].fileDataID : 0;
          LOG_INFO << "[unityipc-test]   SetSkinByDisplayID(" << probeId << ") -> skin["
                   << displayMap[0].second << "] texture" << afterFdid
                   << (afterFdid > 0 ? UnityAssetAccess::readByFileDataID(afterFdid).path : QString());
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
        }
      }

      LOG_INFO << "[unityipc-test] skin-sync: skinPushes=" << ipc->stats().skinPushes
               << "lastSkin=" << ipc->stats().lastSkin;

      const WoWModel * mdl2 = frame->canvas ? frame->canvas->model() : NULL;
      // Animation sync: the viewport plays ONE of the model's animations and the renderer has to
      // play the same one. Walk a spread of the selector rather than all of it -- a boss has
      // hundreds of entries and each push has to reach the player before the next -- and report
      // what each selection resolved to, so a run shows the whole chain from the selector to the
      // sequence index that left the app.
      const int animCount = ac ? ac->animationCount() : 0;
      LOG_INFO << "[unityipc-test] animation-sync check: the selector offers" << animCount << "animation(s)";
      if (ac && animCount > 0)
      {
        const int wanted = 6;
        const int step = (animCount > wanted) ? (animCount / wanted) : 1;
        for (int a = 0; a < animCount; a += step)
        {
          // The selector's label carries the animation-table index in brackets; that index, not
          // the list position, is what SelectAnimation and the renderer both work in.
          const wxString label = ac->animationName(a);
          const int open = label.Find('[') + 1;
          const int close = label.Find(']');
          const int index = (open > 0 && close > open) ? wxAtoi(label.Mid(open, close - open)) : a;
          ac->SelectAnimation(index, 0);
          const WoWModel * mdl = frame->canvas ? frame->canvas->model() : NULL;
          const int playing = (mdl && mdl->animManager) ? (int)mdl->animManager->GetAnim() : -1;
          LOG_INFO << "[unityipc-test]   animation[" << a << "]"
                   << QString::fromWCharArray(label.c_str())
                   << "-> sequence" << index << "playing" << playing
                   << "animID" << ((mdl && playing >= 0 && playing < (int)mdl->anims.size())
                                   ? mdl->anims[playing].animID : -1)
                   << "length" << ((mdl && playing >= 0 && playing < (int)mdl->anims.size())
                                   ? (int)mdl->anims[playing].length : -1);
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
        }
        LOG_INFO << "[unityipc-test] animation-sync: animPushes=" << ipc->stats().animPushes
                 << "lastAnimation=" << ipc->stats().lastAnimation;

        // EXTERNAL ANIMATIONS. A sequence without flag 0x20 keeps its keyframes in a .anim file
        // rather than in the .m2; the host's model code reads those files (WoWModel::readAnimsFromFile)
        // and plays such a sequence normally, so the embedded renderer has to as well. These are
        // the sequences that used to fall back to the idle in the Unity viewport while playing fine in
        // the old OpenGL one -- Agronn's SitGroundDown among them -- so the self-test drives them
        // explicitly rather than hoping the sampled walk lands on one.
        if (mdl2 && ac)
        {
          int externalDriven = 0;
          for (size_t s = 0; s < mdl2->anims.size() && externalDriven < 8; s++)
          {
            if (mdl2->anims[s].flags & 0x20)
              continue;                       // its keys are in the .m2
            if (mdl2->anims[s].length == 0)
              continue;                       // nothing to play either way
            LOG_INFO << "[unityipc-test]   external anim: sequence" << (int)s
                     << "animID" << mdl2->anims[s].animID
                     << "subAnimID" << mdl2->anims[s].subAnimID
                     << "flags" << QString("0x%1").arg(mdl2->anims[s].flags, 0, 16)
                     << "length" << mdl2->anims[s].length;
            ac->SelectAnimation((int)s, 0);
            // A .anim fetch is a round trip, so give it longer than an in-file switch needs.
            for (int i = 0; i < 60; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
            externalDriven++;
          }
          LOG_INFO << "[unityipc-test] external-anim check: drove" << externalDriven
                   << "sequence(s) whose keyframes are in .anim files";

          // THE DROPDOWN PATH. AnimControl::OnAnim is what the user actually operates, and it is
          // NOT SelectAnimation: it stops the model, selects, and plays again. Only the selection
          // pushed anything, so the renderer was told "this animation, not running" and nothing
          // afterwards -- it sat still until the next heartbeat. Driving the handler itself is the
          // only way a headless run can see that, which is why it is done here rather than by
          // calling SelectAnimation like the checks above.
          if (ac->animationCount() > 0)
          {
            const int pickIndex = (ac->animationCount() > 1) ? 1 : 0;
            const wxString label = ac->animationName(pickIndex);
            ac->pickAnimationLikeUser(pickIndex);
            for (int i = 0; i < 40; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

            const bool saysPlaying = ipc->stats().lastState.startsWith("playing");
            LOG_INFO << "[unityipc-test] dropdown check: picked"
                     << QString::fromWCharArray(label.c_str())
                     << "-> app paused=" << (mdl2->animManager->IsPaused() ? 1 : 0)
                     << "lastState=" << ipc->stats().lastState
                     << (saysPlaying
                         ? "(OK: the state after a dropdown change says the animation is running)"
                         : "(BAD: the renderer was left holding until the next heartbeat)");
            if (!saysPlaying)
              LOG_ERROR << "[unityipc-test] dropdown change left the renderer paused";
          }

          // SELECTION MUST CARRY ITS STATE. Changing animation has to tell the renderer what the
          // app is doing with it in the same breath -- if the state only turned up on the next
          // heartbeat, the viewport would sit there for up to a second before the new animation
          // started, which is the difference between "instant" and "laggy" to anyone using it.
          // The counters make that checkable: one selection must produce one of each push.
          {
            AnimManager * am = mdl2->animManager;      // same manager, this block's own handle
            const int animsBefore = ipc->stats().animPushes;
            int probe = -1;
            for (size_t s = 0; s < mdl2->anims.size(); s++)
              if (mdl2->anims[s].length > 0) { probe = (int)s; break; }

            if (probe >= 0)
            {
              // ...while PLAYING
              am->Play();
              ac->PushAnimationState();
              const int stateAfterPlay = ipc->stats().statePushes;
              ac->SelectAnimation(probe, 0);
              LOG_INFO << "[unityipc-test]   selection while PLAYING -> animPushes +"
                       << (ipc->stats().animPushes - animsBefore) << "statePushes +"
                       << (ipc->stats().statePushes - stateAfterPlay)
                       << "(both must be >= 1 -- the state cannot wait for the heartbeat)"
                       << "lastState=" << ipc->stats().lastState;
              for (int i = 0; i < 40; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

              // ...and while PAUSED. The renderer must hold, not run: a switch while paused shows
              // the new animation's first frame and stays there.
              am->Pause(true);
              ac->PushAnimationState();
              const int stateAfterPause = ipc->stats().statePushes;
              const int animAfterPause = ipc->stats().animPushes;
              int other = -1;
              for (size_t s = 0; s < mdl2->anims.size(); s++)
                if (mdl2->anims[s].length > 0 && (int)s != probe) { other = (int)s; break; }
              if (other >= 0)
              {
                ac->SelectAnimation(other, 0);
                LOG_INFO << "[unityipc-test]   selection while PAUSED -> animPushes +"
                         << (ipc->stats().animPushes - animAfterPause) << "statePushes +"
                         << (ipc->stats().statePushes - stateAfterPause)
                         << "paused=" << (am->IsPaused() ? 1 : 0)
                         << "lastState=" << ipc->stats().lastState;
                for (int i = 0; i < 40; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
              }
              am->Play();
              ac->PushAnimationState();
              for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
            }
          }

          // REVISIT. Going back to an animation already played is what a user does constantly --
          // comparing two animations means switching between them repeatedly -- so it has to be
          // the cheapest thing the renderer does: read once, kept, and the second visit should
          // neither fetch nor read nor allocate. The player logs which path it took, so the run
          // shows whether the cache actually caught it.
          //
          // Prefer a sequence whose keyframes are EXTERNAL. That exercises both caches at once:
          // the .anim bytes must not be fetched twice, and the tracks read out of them must not
          // be read twice. An in-file sequence only exercises the second.
          if (!mdl2->anims.empty())
          {
            int first = -1, second = -1;
            for (size_t s = 0; s < mdl2->anims.size(); s++)   // an external one, if there is one
            {
              if (mdl2->anims[s].length == 0 || (mdl2->anims[s].flags & 0x20))
                continue;
              first = (int)s;
              break;
            }
            for (size_t s = 0; s < mdl2->anims.size(); s++)
            {
              if (mdl2->anims[s].length == 0 || (int)s == first)
                continue;
              if (first < 0) { first = (int)s; continue; }
              second = (int)s;
              break;
            }
            if (first >= 0 && second >= 0)
            {
              LOG_INFO << "[unityipc-test] revisit check: sequence" << first
                       << (mdl2->anims[first].flags & 0x20 ? "(in-file)" : "(external .anim)")
                       << "-> " << second << "-> " << first
                       << "-- the last one must come from the cache, with no second fetch";
              for (int pass = 0; pass < 3; pass++)
              {
                ac->SelectAnimation(pass == 1 ? second : first, 0);
                for (int i = 0; i < 60; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
              }
            }
          }
        }

        // Playback state: the same animation, driven through the transport controls. Each step
        // reports what the app now holds, so a run shows the whole chain from the control to the
        // state that left the app -- and the player's own log shows what it did with it.
        AnimManager * am = mdl2 ? mdl2->animManager : NULL;
        // The scrub below indexes anims by the manager's current animation, so both have to be
        // real: a model can reach here with a populated selector but nothing playable.
        if (am && (am->GetAnim() >= mdl2->anims.size()))
          am = NULL;
        LOG_INFO << "[unityipc-test] playback-state check:";
        if (am)
        {
          // pause
          am->Pause(true);
          ac->PushAnimationState();
          LOG_INFO << "[unityipc-test]   pause     -> playing=" << (am->IsPaused() ? 0 : 1)
                   << "timeMs=" << (int)am->GetFrame() << "speed=" << am->GetSpeed();
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

          // scrub while paused
          ac->SetAnimFrame(mdl2->anims[am->GetAnim()].length / 3);
          LOG_INFO << "[unityipc-test]   scrub     -> playing=" << (am->IsPaused() ? 0 : 1)
                   << "timeMs=" << (int)am->GetFrame();
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

          // speed
          ac->SetAnimSpeed(2.0f);
          LOG_INFO << "[unityipc-test]   speed 2x  -> speed=" << am->GetSpeed();
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

          // A skin change WHILE PAUSED. Selecting a skin re-pushes the texture set and the
          // geoset set, and the two subsystems have to stay out of each other's way: the model
          // must not resume because its skin changed, and must not lose the frame it is held on.
          if (skins > 1)
          {
            const size_t heldFrame = am->GetFrame();
            ac->SetSkin(0);
            for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
            std::vector<int> pausedGeosets;
            UnityAssetAccess::selectedModelGeosets(fdid, pausedGeosets);
            QString pausedGeoStr;
            for (size_t gi = 0; gi < pausedGeosets.size(); gi++)
              pausedGeoStr += (gi ? "," : "") + QString::number(pausedGeosets[gi]);
            if (pausedGeoStr.isEmpty())
              pausedGeoStr = "none";
            LOG_INFO << "[unityipc-test]   skin while paused -> skin[0] geosets=" << pausedGeoStr
                     << "| playback still playing=" << (am->IsPaused() ? 0 : 1)
                     << "timeMs=" << (int)am->GetFrame()
                     << (am->GetFrame() == heldFrame ? "(frame held)" : "(FRAME MOVED)");

            ac->SetSkin(skins - 1);
            for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
            LOG_INFO << "[unityipc-test]   skin while paused -> skin[" << (skins - 1)
                     << "] | playback still playing=" << (am->IsPaused() ? 0 : 1)
                     << "timeMs=" << (int)am->GetFrame()
                     << (am->GetFrame() == heldFrame ? "(frame held)" : "(FRAME MOVED)");
          }

          // resume
          am->Play();
          ac->PushAnimationState();
          LOG_INFO << "[unityipc-test]   resume    -> playing=" << (am->IsPaused() ? 0 : 1)
                   << "timeMs=" << (int)am->GetFrame() << "speed=" << am->GetSpeed();
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

          // ...and the same change while it RUNS, which is the other half: a skin push must not
          // stop or restart the animation either.
          if (skins > 1)
          {
            ac->SetSkin(0);
            for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }
            LOG_INFO << "[unityipc-test]   skin while animated -> skin[0] | playback still playing="
                     << (am->IsPaused() ? 0 : 1) << "timeMs=" << (int)am->GetFrame();
          }

          // back to something ordinary so the run does not end in a doubled-speed state
          ac->SetAnimSpeed(1.0f);
          for (int i = 0; i < 20; i++) { ipc->poll(); wxTheApp->Yield(true); wxMilliSleep(10); }

          LOG_INFO << "[unityipc-test] playback-state: statePushes=" << ipc->stats().statePushes
                   << "lastState=" << ipc->stats().lastState;
        }
      }

      // LIVE GEOSET SWITCHING. A Geosets checkbox sends the displayed model's whole per-submesh
      // state (modelGeosets) and the player answers with what it now draws. Switched here the way
      // the checkboxes do -- a submesh with geoset id 0 first, which the id list alone could never
      // hide -- in the order A off, B off, A on, then back to the original state; every answer must
      // be "applied" and must match the host's own flags exactly.
      {
        WoWModel * gm = const_cast<WoWModel *>(frame->canvas->model());
        const size_t owned = gm ? std::min(gm->ownGeosetCount(), gm->geosets.size()) : 0;
        if (owned < 2 || !frame->unityCanDrawCurrentModel() || !ipc->playerSwitchesSubmeshes() ||
            frame->canvasShowsCharacter())
        {
          // A character's geosets travel in its scene (the character check below); a model the Unity
          // viewport cannot draw has a notice in front of it and no state to switch.
          LOG_INFO << "[unityipc-test] geoset-live check: skipped (" << (int)owned
                   << "submesh(es), unity can draw=" << (frame->unityCanDrawCurrentModel() ? 1 : 0)
                   << ", character=" << (frame->canvasShowsCharacter() ? 1 : 0)
                   << ", player protocol" << ipc->playerProtocolVersion() << ")";
        }
        else
        {
          std::vector<UnityIpcServer::GeosetAck> acks;
          auto previous = ipc->onGeosetsApplied;
          ipc->onGeosetsApplied = [&acks, previous](const UnityIpcServer::GeosetAck & a) {
            acks.push_back(a);
            if (previous)
              previous(a);
          };

          size_t idxA = 0;
          for (size_t i = 0; i < owned; i++)
            if (gm->geosets[i]->id == 0) { idxA = i; break; }
          const size_t idxB = (idxA == owned - 1) ? 0 : owned - 1;
          std::vector<bool> original;
          for (size_t i = 0; i < owned; i++)
            original.push_back(gm->geosets[i]->display);

          int geoPass = 0, geoFail = 0;
          long long firstAnim = -1, lastAnim = -1;
          auto step = [&](const char * what, size_t index, bool value, bool restoreAll) {
            if (restoreAll)
              for (size_t i = 0; i < owned; i++)
                gm->showGeoset((uint)i, original[i]);
            else
              gm->showGeoset((uint)index, value);
            const int revision = frame->SendCurrentGeosetsToUnity();
            const UnityIpcServer::GeosetAck * got = nullptr;
            wxStopWatch wait;
            while (revision > 0 && !got && wait.Time() < 5000)
            {
              ipc->poll();
              wxTheApp->Yield(true);
              wxMilliSleep(10);
              for (const UnityIpcServer::GeosetAck & a : acks)
                if (a.revision == revision && a.status != "pending")
                  got = &a;
            }
            bool match = got && got->status == "applied" && got->hasVisible && got->visible.size() == owned;
            QString bits;
            for (size_t i = 0; match && i < owned; i++)
              match = (got->visible[i] == gm->geosets[i]->display);
            for (size_t i = 0; got && i < got->visible.size(); i++)
              bits += got->visible[i] ? "1" : "0";
            if (got && got->status == "applied")
            {
              if (firstAnim < 0) firstAnim = got->animTimeMs;
              lastAnim = got->animTimeMs;
            }
            (match ? geoPass : geoFail)++;
            LOG_INFO << "[unityipc-test]   geoset" << what << "submesh" << (int)index << "id"
                     << (int)gm->geosets[index]->id << "revision" << revision
                     << "-> status=" << (got ? got->status : QString("NO ANSWER"))
                     << "drawn=" << bits << "triangles=" << (got ? got->triangles : 0)
                     << "animTimeMs=" << (got ? (qlonglong)got->animTimeMs : -1)
                     << (match ? "(matches the host flags)" : "(MISMATCH)");
          };

          step("A off", idxA, false, false);
          step("B off", idxB, false, false);
          step("A on ", idxA, true, false);
          step("restore", idxA, true, true);
          ipc->onGeosetsApplied = previous;

          geosetLiveOk = (geoFail == 0 && geoPass == 4);
          LOG_INFO << "[unityipc-test] geoset-live check:" << geoPass << "applied and matching," << geoFail
                   << "failed; animation clock" << (qlonglong)firstAnim << "->" << (qlonglong)lastAnim << "ms"
                   << (geosetLiveOk ? "(OK)" : "(FAIL)");
        }
      }
    }
  }

  // A CHARACTER. Its body load waits for the host's characterScene; the player dresses it and answers
  // characterSceneApplied. Checked: the dressed character was applied, then two live changes -- a body
  // geoset switched off and back on, and a new random appearance (a new composited body image) --
  // each answered "applied" with the time it took.
  bool characterOk = true;
  if (frame->canvasShowsCharacter() && ipc->playerDressesCharacters())
  {
    auto waitApplied = [&](int before, long limitMs) {
      wxStopWatch w;
      while (w.Time() < limitMs)
      {
        ipc->poll();
        wxTheApp->Yield(true);
        wxMilliSleep(10);
        if (ipc->stats().sceneApplied > before && frame->m_sceneAwaitingRevision == 0)
          return (long)w.Time();
      }
      return -1L;
    };
    const long first = waitApplied(0, 60000);
    LOG_INFO << "[unityipc-test] character: first scene" << (first >= 0 ? "applied" : "NOT applied") << "after"
             << first << "ms; scenes sent" << ipc->stats().scenePushes << "images" << ipc->stats().imagePushes
             << "(" << ipc->stats().imageBytes << "base64 bytes); last" << ipc->stats().lastScene << "| ack"
             << ipc->stats().lastSceneAck;
    // The body the player dresses is the host's composite, made in the hidden canvas's GL context: no
    // characterImage sent means the composite was not produced.
    characterOk = first >= 0 && ipc->stats().imagePushes >= 1;

    WoWModel * cm = const_cast<WoWModel *>(frame->canvas->model());
    if (characterOk && cm)
    {
      size_t index = cm->geosets.size();
      for (size_t i = 0; i < std::min(cm->ownGeosetCount(), cm->geosets.size()); i++)
        if (cm->geosets[i]->display && cm->geosets[i]->id != 0) { index = i; break; }
      if (index < cm->geosets.size())
      {
        int before = ipc->stats().sceneApplied;
        cm->showGeoset((uint)index, false);
        frame->SendCharacterSceneToUnity(true);
        const long off = waitApplied(before, 20000);
        before = ipc->stats().sceneApplied;
        cm->showGeoset((uint)index, true);
        frame->SendCharacterSceneToUnity(true);
        const long on = waitApplied(before, 20000);
        LOG_INFO << "[unityipc-test] character: body geoset" << (int)cm->geosets[index]->id << "off applied in" << off
                 << "ms, back on in" << on << "ms";
        characterOk = characterOk && off >= 0 && on >= 0;
      }
      int before = ipc->stats().sceneApplied;
      const int imagesBefore = ipc->stats().imagePushes;
      QElapsedTimer refreshClock;
      refreshClock.start();
      cm->cd.randomise();
      const qint64 refreshMs = refreshClock.elapsed();
      frame->SendCharacterSceneToUnity(true);
      const long random = waitApplied(before, 30000);
      LOG_INFO << "[unityipc-test] character: random appearance -- host refresh" << refreshMs << "ms, applied in"
               << random << "ms," << (ipc->stats().imagePushes - imagesBefore) << "new image(s); ack"
               << ipc->stats().lastSceneAck;
      characterOk = characterOk && random >= 0;
    }
    LOG_INFO << "[unityipc-test] character check:" << (characterOk ? "(OK)" : "(FAIL)");
  }

  // THE LIFECYCLE SEQUENCE, opt-in (see doIpcTestLifecycleSequence): after everything above.
  bool sequenceOk = true;
  const QString sequenceSpec = qEnvironmentVariable("WMV_IPCTEST_SEQUENCE").trimmed();
  if (!sequenceSpec.isEmpty())
    sequenceOk = doIpcTestLifecycleSequence(frame, ipc, sequenceSpec, mapReports);
  else
    LOG_INFO << "[unityipc-test] lifecycle sequence: not requested (set WMV_IPCTEST_SEQUENCE)";
  LOG_INFO << "[unityipc-test] world-model reports in the run:" << ipc->stats().mapObjectReports << "("
           << ipc->stats().mapObjectBuilt << "built," << ipc->stats().mapObjectFailed << "failed,"
           << ipc->stats().mapObjectSuperseded << "superseded) of" << ipc->stats().mapObjectLoads << "load(s) sent";

  // A model with a skin selector must have pushed at least one skin; one without simply has
  // nothing to sync, so the condition only bites when there was something to send.
  const bool skinsOk = (frame->animControl == NULL) || (frame->animControl->skinCount() == 0) ||
                       (st.skinPushes >= 1);
  // A model with an animation selector must have pushed at least one animation; one without has
  // nothing to sync, so the condition only bites when there was something to send.
  const bool animsOk = (frame->animControl == NULL) || (frame->animControl->animationCount() == 0) ||
                       (ipc->stats().animPushes >= 1);
  // Same shape for the playback state: a model with animations must have reported how it is
  // playing them at least once.
  const bool stateOk = (frame->animControl == NULL) || (frame->animControl->animationCount() == 0) ||
                       (ipc->stats().statePushes >= 1);
  // The archived canvas must not have received a single paint in the whole run (ModelCanvas::Render
  // also logs an error the first time it is entered).
  const bool neverPainted = !ModelCanvas::s_renderEntered;
  LOG_INFO << "[unityipc-test] archived canvas paint handler entered during the run=" << (neverPainted ? 0 : 1)
           << "| clock ticks in total=" << (qulonglong)ModelCanvas::s_clockTicks
           << (neverPainted ? "(OK)" : "(FAIL)");
  const bool pass = st.connections >= 1 && ipc->isUnityReady() && st.requests >= 1 &&
                    st.responsesOk >= 1 && skinsOk && animsOk && stateOk && geosetLiveOk && characterOk &&
                    wmoOk && sequenceOk && viewportOk && neverPainted;
  LOG_INFO << "[unityipc-test] RESULT:" << (pass ? "PASS" : "FAIL");

  // Close the player now (what app shutdown does) and confirm the child process is gone.
  ipc->onMapObjectLoaded = previousMapObject;
  frame->unityRendererHost->shutdown();
  LOG_INFO << "[unityipc-test] player shut down; still running=" << (frame->unityRendererHost->isRunning() ? 1 : 0);
}

// A batch run that loads something and has no other job ends here. It used to write an ss_*.png
// screenshot of the OpenGL viewport; that viewport is archived and the Unity viewport has no capture
// yet, so the run says so once instead of writing a picture of a renderer nobody sees.
static void logNoHeadlessScreenshot()
{
  LOG_INFO << "Headless run done: screenshots are not available in the Unity-only viewer, so no ss_*.png was written.";
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
        a == "-m2inspect" || a == "-matrestest" || a == "-mpq" || a == "-item" || a == "-wmo" || a.endsWith(".chr"))
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
  // window is shown, so it never flashes in front of the user. The frame itself stays "shown", just
  // positioned beyond the desktop; the archived OpenGL canvas inside it is never shown in any run
  // (its GL context does not need it to be -- see ModelCanvas's constructor).
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

  // The archived canvas: initialise its GL state and lights WITHOUT showing it. It is never shown --
  // not here, not in a headless run -- and its clock (OnTimer) starts ticking once init is set.
  if (frame->canvas) {
    if (!frame->canvas->init)
      frame->canvas->InitGL();

    if (frame->lightControl)
      frame->lightControl->UpdateGL();
  }
  // --

  // TODO: Improve this feature and expand on it.
  // Command arguments
  QString cmd;
  QString snapModelPath; // -mo: defer the load until after LoadWoW
  int snapItemId = 0;    // -item: defer an item/display-context load until after LoadWoW
  QString snapWmoArg;    // -wmo <root path|FileDataID>: defer a Browse-style WMO selection until after LoadWoW
  QString snapArmoryUrl; // -armory <url>: headless import (test harness)
  QString snapNpcArg;    // -npc <id|id:displayId>: headless NPC load (test harness)
  QString fbxExportPath; // -fbxexport <out.fbx>: headless FBX export of the -mo model (test harness)
  QString fbxInspectPath; // -fbxinspect <in.fbx>: read-only forensic dump of an existing FBX (no game data)
  QString animDumpName;   // -animdump <animName>: source-vs-exported per-bone pose diff for the -mo model
  QString snapCharPath;   // <file.chr>: defer LoadChar until AFTER LoadWoW (export)
  QString mpqDataFolder;  // -mpq <DataFolder> [locale]: load a legacy MPQ client instead of CASC
  QString mpqLocale;      // optional locale for -mpq (auto-detected when empty)
  int dumpTexFileDataId = 0; QString dumpTexOutPath; // -dumptex <fileDataID> <out.png>: forensic-only
  bool matResTest = false;                           // -matrestest: replaceable-material checks
  QString m2InspectList, m2InspectOut;               // -m2inspect <list.txt> [out.csv]: forensic-only
  // Export content selection + clip list for the headless FBX export (the parent process passes
  // these so the child reproduces the user's exact options). Defaults: full content, no explicit
  // clips (the exporter falls back to none/first-N only if -fbxanim and no -fbxclips).
  int optMesh = 1, optSkel = 1, optSkin = 1, optAnim = 1;
  int optComponent = 0;   // -fbxcomponent : opt-in raw/node-based item-component export (UV2 + raw units + sidecar v2)
  int itemSkinFileId = 0; // -itemskin <fileDataID> : re-bind an item/weapon's on-screen skin after -mo load
  bool unityIpcTest = false; // -unityipctest : with -mo, -item or -wmo, run the embedded Unity renderer IPC self-test (see doHeadlessUnityIpcTest)
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

        // Defer the load until AFTER LoadWoW() below -- the game data
        // must be loaded before a model can be resolved/composed.
        snapModelPath = fn;
      }
    }
    else if (cmd == "-item") {
      // Headless item load: "-item <itemID>" shows the item through the same ModelViewer::LoadItem
      // the item-selection dialog calls, so the item/display-context path -- which resolves the
      // item's display, its component model and its component geoset state -- can be captured and
      // regressed. Composes with -unityipctest exactly as -mo does.
      if (i + 1 < argc) { i++; snapItemId = QString::fromWCharArray(argv[i]).toInt(); }
    }
    else if (cmd == "-wmo") {
      // Headless world-model selection: "-wmo <root listfile path or FileDataID>" selects the WMO through
      // FileControl::SelectWMOFile, the same code a pick under Browse's WMO filter runs. Composes with
      // -unityipctest (the world-model check); see doHeadlessUnityIpcTest.
      if (i + 1 < argc) { i++; snapWmoArg = QString::fromWCharArray(argv[i]); }
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
    else if (cmd == "-matrestest") {
      // Regression checks for retail replaceable-material selection; see doHeadlessMatResTest.
      matResTest = true;
    }
    else if (cmd == "-m2inspect") {
      // Forensic-only: "-m2inspect <list.txt> [out.csv]" reads each listed model's M2 header,
      // reports what visual systems it carries, and exits. See doHeadlessM2Inspect.
      if (i + 1 < argc) {
        i++;
        m2InspectList = QString::fromWCharArray(argv[i]);
        if (i + 1 < argc) {
          const QString nxt = QString::fromWCharArray(argv[i + 1]);
          if (!nxt.startsWith('-')) { i++; m2InspectOut = nxt; }
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
    else if (cmd == "-armory") {
      // Headless armory import for testing: load the character from the URL after
      // LoadWoW(). Mirrors -mo. importChar sets hasTransmogGear
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
    else if (cmd == "-unityipctest") { unityIpcTest = true; }
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
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" || a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" || a == "-m2inspect" || a == "-matrestest" || a == "-mpq" || a == "-item" || a == "-wmo" || a.endsWith(".chr"))
    {
      headlessLoad = true;
      break;
    }
  }

  if (headlessLoad)
  {
    frame->batchMode = true; // non-interactive run: suppress modal dialogs that would block it

    // The command bar and the Model panel did not exist when the batch runs were settled; keep them
    // out of a non-interactive run so its viewport keeps the size those runs have always had.
    frame->interfaceManager.GetPane(wxT("commandBar")).Show(false);
    frame->interfaceManager.GetPane(wxT("modelInspector")).Show(false);
    frame->interfaceManager.Update();

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

    if (matResTest)
    {
      doHeadlessMatResTest();
      return false; // checks done -> exit
    }

    if (!m2InspectList.isEmpty())
    {
      doHeadlessM2Inspect(m2InspectList, m2InspectOut);
      return false; // forensic sweep done -> exit
    }

    if (!dumpTexOutPath.isEmpty())
    {
      doHeadlessDumpTexture(dumpTexFileDataId, dumpTexOutPath);
      return false; // forensic dump done -> exit
    }

    if (!snapWmoArg.isEmpty())
    {
      GameFile * wmoFile = resolveGameFileArg(snapWmoArg);
      if (!wmoFile)
        LOG_ERROR << "[wmo] no game file for" << snapWmoArg;
      else if (!frame->fileControl)
        LOG_ERROR << "[wmo] no Browse control to select" << snapWmoArg << "with";
      else
        frame->fileControl->SelectWMOFile(wmoFile);
      if (unityIpcTest)
        doHeadlessUnityIpcTest(frame);
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }

    if (snapItemId > 0)
    {
      // The item path decides the display, the component model, its skin and its component
      // geoset state. Everything after this is the same tail -mo uses.
      frame->LoadItem((unsigned int)snapItemId);
      if (unityIpcTest)
        doHeadlessUnityIpcTest(frame);
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
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

      // Embedded Unity renderer runtime asset access self-test (needs the loaded model above).
      if (unityIpcTest)
        doHeadlessUnityIpcTest(frame);

      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }

    // Character (.chr) headless branch -- compose the saved character, then export.
    if (!snapCharPath.isEmpty())
    {
      frame->LoadChar(snapCharPath);
      if (!fbxExportPath.isEmpty())
      {
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0);
        return false;
      }
      logNoHeadlessScreenshot();
      return false;
    }
    if (!snapArmoryUrl.isEmpty())
    {
      frame->ImportArmoury(wxString::FromUTF8(snapArmoryUrl.toUtf8().constData()));
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
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
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }
  }
  else
  {
    // THE APPLICATION COMES UP AND WAITS. It opens as an empty fullscreen viewer -- no client
    // read, no dialog asked, no question put to the user before they have even seen the program.
    // Loading a client is something they do when they want to, through
    // File > "Load World of Warcraft", which is the only thing that loads one now.
    //
    // Nothing here needs game data: the layout is layout, and the renderer talks to WMV rather
    // than to the client, so it can sit connected and idle until there is something to show.
    frame->ApplyViewerStartupLayout();
    frame->WarmStartUnityViewport();
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

  // The GL pixel format the archived canvas's context asks for. It was a user setting (Settings > Display,
  // saved as Graphics/*) while the OpenGL viewport existed; now the context only decodes textures for
  // the Unity viewport, so it always asks for the defaults a fresh install used, and old Graphics/* values
  // are ignored -- a bad saved mode can no longer break texture decoding with no page left to fix it on.
  // (SetHandle still adjusts this to a mode the display supports.)
  video.curCap.aaSamples = 0;
  video.curCap.accum = 0;
  video.curCap.alpha = 0;
  video.curCap.colour = 24;
  video.curCap.doubleBuffer = 1;
#ifdef _WINDOWS
  video.curCap.hwAcc = WGL_FULL_ACCELERATION_ARB;
#endif
  video.curCap.sampleBuffer = 0;
  video.curCap.stencil = 0;
  video.curCap.zBuffer = 16;

  // Application locale info
  langID = config.value("Locale/LanguageID", 1).toInt();
  langName = config.value("Locale/LanguageName", "").toString().toStdWString();

  // Application settings
  gamePath = config.value("Settings/Path", "").toString().toStdWString();
  armoryPath = config.value("Settings/ArmoryPath", "").toString().toStdWString();
  customDirectoryPath = config.value("Settings/CustomDirPath", "").toString().toStdWString();
  customFilesConflictPolicy = config.value("Settings/CustomFilesConflictPolicy", 0).toInt();
  displayItemAndNPCId = config.value("Settings/displayItemAndNPCId", 0).toInt();
  // Settings/SSCounter and Settings/DefaultFormat (the screenshot file counter and format) are no longer
  // read or written: Save Screenshot went with the OpenGL viewport.

  // Optional override for the embedded Unity renderer player exe. Empty (the default) ->
  // resolved at use-time as tools\unity-renderer\UnityRenderer.exe next to the WMV
  // executable, so a moved install keeps finding its bundled player.
  unityRendererPath = config.value("Tools/UnityRendererPath", "").toString().toStdWString();

  // Tools/UnityPrimaryViewport is no longer read: the Unity viewport is the only viewport, so an
  // old value left in a Config.ini has nothing left to choose between.

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

  config.setValue("Tools/UnityRendererPath", QString::fromWCharArray(unityRendererPath.c_str()));

  config.setValue("Armory/ProxyURL", QString::fromStdString(GLOBALSETTINGS.armoryProxyURL()));
  config.sync();
}


