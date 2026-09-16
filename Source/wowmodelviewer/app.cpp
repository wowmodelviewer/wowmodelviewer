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
#include "UnityCharacterScene.h"
#include "UnityIpcServer.h"
#include "UnityRendererHost.h"

#include <wx/evtloop.h>
#include <wx/stopwatch.h>

#include <QBuffer>
#include <QDir>
#include <QXmlStreamWriter>
#include "CharDetailsCustomizationChoice.h"
#include "CharDetailsFrame.h"
#include "dbfile.h"
#include "RaceInfos.h"


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

// -customizationtest: regression checks for modern character customization, in the idiom of
// -matrestest: checks against the installed client's data, one [customization-test] PASS or FAIL line
// each, then "RESULT: PASS|FAIL (<passed> passed, <failed> failed)" (the return value is the failure
// count). It covers what the loader stores for ChrCustomizationReq (the race-mask census of the whole
// table and representative rows, among them rows the old loader stored with a garbage high half or a
// wrong pallet entry), the loader's value path read directly, ChrCustomizationOption.Requirement,
// ChrRaces.PlayableRaceBit and ChrCustomizationReqChoice, the requirement rules on their own, and then
// loaded characters as CharDetails and the Appearance panel see them: an Undead male (option and choice
// lists, lists that follow Skin Type, Jaw Features and Eye Color, the textures applied after repeated
// changes, one model refresh per change, a saved invalid combination), a Night Elf male's Demon Hunter
// context, the Dracthyr's own Eye Style and a Dark Iron Dwarf's race bit.
//
// Every expected value was read from the 12.1.0.69814 client's DB2 files by a reader independent of this
// loader. A later client may legitimately re-author them: a failure then means "the data moved, go look",
// not necessarily "the code broke".
static int doHeadlessCustomizationTest(ModelViewer * frame)
{
  int passed = 0, failed = 0;
  const auto check = [&passed, &failed](const QString & what, bool ok, const QString & detail) {
    ok ? passed++ : failed++;
    const QString line = QString("[customization-test] %1 %2%3").arg(ok ? "PASS" : "FAIL").arg(what)
                           .arg(detail.isEmpty() ? QString() : QString(" -- ") + detail);
    if (ok)
      LOG_INFO << line;
    else
      LOG_ERROR << line;
  };
  const auto ids = [](const std::vector<uint> & v) {
    QStringList parts;
    for (const uint id : v)
      parts << QString::number(id);
    return parts.join(QLatin1Char(','));
  };
  const auto queryIDs = [](const QString & sql) {
    std::vector<uint> v;
    sqlResult r = GAMEDATABASE.sqlQuery(sql);
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
      v.push_back(r.values[i][0].toUInt());
    return v;
  };

  // ---- 1. ChrCustomizationReq as stored ------------------------------------------------------------
  // The (RaceMasks[0], RaceMasks[1]) pairs of all 1117 rows (694 records and 423 copies): every row's
  // pallet entry, whole.
  struct RacePair { uint low; uint high; int rows; };
  static const RacePair censusExpected[] = {
    { 0xFFFFFFFFU, 0xFFFFFFFFU, 881 }, { 0xC83800A8U, 0x00000000U,  61 }, { 0x00180000U, 0x00000000U,  39 },
    { 0x80000000U, 0xFFFFFFFFU,  19 }, { 0x40000000U, 0x00000000U,  17 }, { 0x00000080U, 0x00000000U,  15 },
    { 0x00200000U, 0x00000000U,  15 }, { 0x00000008U, 0x00000000U,  14 }, { 0x00000020U, 0x00000000U,  14 },
    { 0x08000000U, 0x00000000U,  12 }, { 0x00000000U, 0x00000000U,   7 }, { 0x00180008U, 0x00000000U,   4 },
    { 0x40000080U, 0x00000000U,   4 }, { 0x08000020U, 0x00000000U,   3 }, { 0x80200000U, 0xFFFFFFFFU,   3 },
    { 0x00000400U, 0x00000000U,   2 }, { 0x000000A0U, 0x00000000U,   1 }, { 0x00200008U, 0x00000000U,   1 },
    { 0x082000A8U, 0x00000000U,   1 }, { 0x4E0AB3B2U, 0xAA2AAAAAU,   1 }, { 0x80000000U, 0x00000000U,   1 },
    { 0x88000020U, 0xFFFFFFFFU,   1 }, { 0xB1354C4DU, 0x55155555U,   1 },
  };
  {
    std::map<std::pair<quint64, quint64>, int> expected, got;
    for (const RacePair & p : censusExpected)
      expected[std::make_pair((quint64)p.low, (quint64)p.high)] = p.rows;
    sqlResult r = GAMEDATABASE.sqlQuery("SELECT RaceMasks1, RaceMasks2, COUNT(*) FROM ChrCustomizationReq GROUP BY RaceMasks1, RaceMasks2");
    int rows = 0;
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
    {
      got[std::make_pair(r.values[i][0].toULongLong(), r.values[i][1].toULongLong())] = r.values[i][2].toInt();
      rows += r.values[i][2].toInt();
    }
    QString diff;
    for (const auto & e : expected)
      if (got.count(e.first) == 0 || got[e.first] != e.second)
        diff += QString(" [%1,%2] expected %3 got %4").arg(e.first.first, 8, 16, QLatin1Char('0')).arg(e.first.second, 8, 16, QLatin1Char('0'))
                  .arg(e.second).arg(got.count(e.first) ? got[e.first] : 0);
    for (const auto & g : got)
      if (expected.count(g.first) == 0)
        diff += QString(" unexpected [%1,%2] x%3").arg(g.first.first, 16, 16, QLatin1Char('0')).arg(g.first.second, 16, 16, QLatin1Char('0')).arg(g.second);
    check("database: ChrCustomizationReq RaceMasks census of the whole table (23 pallet pairs, 1117 rows)",
          r.valid && diff.isEmpty() && rows == 1117, QString("%1 rows, %2 pairs%3").arg(rows).arg(got.size()).arg(diff));
  }

  struct ReqRow { uint id; int reqType; int classMask; int regionGroupMask; int overrideArchive; uint raceLow; uint raceHigh; int achievement; int quest; int item; };
  static const ReqRow reqExpected[] = {
    //  id  type  class  region  OA  RaceMasks[0]  RaceMasks[1]  achievement quest item
    {   10, 4,      0,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // the "Transmog" placeholder
    {   12, 2,      0,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // NPC Eye Style; old loader 0x29F0D4A0FFFFFFFF
    {   35, 3,     -1,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // old loader 0x29F0D4A0FFFFFFFF
    {   53, 3,     32,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // old loader 0x29F0D4A0FFFFFFFF
    {   55, 3,     -1,     0,  0, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // Undead jaws and Bony; old loader garbage
    {   61, 3,     -1,     0,  0, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // Bony skin colours
    {  141, 3,     -1,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // old loader 0x00000000FFFFFFFF
    {  142, 3,     32,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // Death Knight only
    {  143, 3,   2048,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // Demon Hunter only
    {  144, 3,  30687,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // not DK, not DH
    {  146, 3,  32735,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // not DK
    { 4103, 3,      0,     0, -1, 0xFFFFFFFFU, 0xFFFFFFFFU,     0,     0, 0 }, // a copy row (of 322): the Eyesight option
    { 4120, 3,     -1,     0, -1, 0x00000008U, 0x00000000U,     0,     0, 0 }, // pallet entry 2; old loader 0x400
    { 4121, 3,     -1,     0, -1, 0x80000000U, 0xFFFFFFFFU,     0,     0, 0 }, // old loader 0
    { 4244, 3,   1024,     0, -1, 0xC83800A8U, 0x00000000U, 15228,     0, 0 }, // old loader 0
    { 4509, 3,     -1,     0, -1, 0x4E0AB3B2U, 0xAA2AAAAAU,     0, 82194, 0 }, // the horde races
    { 4576, 3,     -1,     0, -1, 0x00000000U, 0x00000000U,     0, 88814, 0 },
    { 4603, 3,     -1,     0, -1, 0xB1354C4DU, 0x55155555U,     0, 82192, 0 }, // the alliance races; old loader 0x80200000
    { 4604, 3,     -1, -2048, -1, 0x00000000U, 0x00000000U,     0,     0, 0 },
  };
  for (const ReqRow & e : reqExpected)
  {
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT ReqType, ClassMask, RegionGroupMask, OverrideArchive, RaceMasks1, RaceMasks2, "
                                                "ReqAchievementID, ReqQuestID, ReqItemModifiedAppearanceID FROM ChrCustomizationReq WHERE ID = %1").arg(e.id));
    const bool found = r.valid && !r.values.empty();
    const std::vector<QString> v = found ? r.values[0] : std::vector<QString>(9);
    const bool ok = found && v[0].toInt() == e.reqType && v[1].toInt() == e.classMask && v[2].toInt() == e.regionGroupMask &&
                    v[3].toInt() == e.overrideArchive && v[4].toULongLong() == e.raceLow && v[5].toULongLong() == e.raceHigh &&
                    v[6].toInt() == e.achievement && v[7].toInt() == e.quest && v[8].toInt() == e.item;
    check(QString("database: ChrCustomizationReq %1 equals the client row").arg(e.id), ok,
          QString("stored ReqType %1 ClassMask %2 RegionGroupMask %3 OverrideArchive %4 RaceMasks [%5, %6] achievement %7 quest %8 item %9")
            .arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]).arg(v[4].toULongLong(), 8, 16, QLatin1Char('0')).arg(v[5].toULongLong(), 8, 16, QLatin1Char('0'))
            .arg(v[6]).arg(v[7]).arg(v[8]));
  }

  // ---- 2. The loader's value path, read directly ---------------------------------------------------
  // ChrCustomizationReq.db2 read through the loader with RaceMasks declared four ways: as its two uint32,
  // as two int64 and two uint64 (each element must come back as that uint32 widened -- the old int64 path
  // copied eight bytes out of a four-byte value), and as ONE uint32 (element 0 through the file's own
  // pallet stride, which the old arraySize-1 declaration broke for every pallet entry past the first).
  {
    core::TableStructure * tbl = GAMEDATABASE.createTableStructure();
    tbl->name = QStringLiteral("ChrCustomizationReq");
    tbl->file = tbl->name;
    const auto addField = [tbl](const char * name, const char * type, int pos, unsigned int arraySize, bool key) {
      core::FieldStructure * f = GAMEDATABASE.createFieldStructure();
      f->name = QString::fromLatin1(name);
      f->type = QString::fromLatin1(type);
      f->arraySize = arraySize;
      f->isKey = key;
      if (wow::FieldStructure * wf = dynamic_cast<wow::FieldStructure *>(f))
        wf->pos = pos;
      tbl->fields.push_back(f);
    };
    addField("ID", "int32", -1, 1, true);            // [0]
    addField("RaceMasks", "uint32", 8, 2, false);    // [1] [2]
    addField("RaceMasks", "int64", 8, 2, false);     // [3] [4]
    addField("RaceMasks", "uint64", 8, 2, false);    // [5] [6]
    addField("RaceMask", "uint32", 8, 1, false);     // [7]
    addField("ClassMask", "int32", 2, 1, false);     // [8]
    addField("OverrideArchive", "int32", 6, 1, false); // [9]

    std::map<uint, std::vector<QString> > stored;
    sqlResult db = GAMEDATABASE.sqlQuery("SELECT ID, RaceMasks1, RaceMasks2, ClassMask, OverrideArchive FROM ChrCustomizationReq");
    for (size_t i = 0; db.valid && i < db.values.size(); i++)
      stored[db.values[i][0].toUInt()] = db.values[i];

    DBFile * file = tbl->createDBFile();
    const bool opened = file && file->open();
    int rows = 0, widenMismatch = 0, strideMismatch = 0, storedMismatch = 0;
    QString firstMismatch, row4120;
    for (size_t i = 0; opened && i < file->getRecordCount(); i++)
    {
      const std::vector<std::string> s = file->get((unsigned int)i, tbl);
      if (s.size() != 10)
      {
        widenMismatch++;
        continue;
      }
      rows++;
      const auto q = [&s](size_t k) { return QString::fromStdString(s[k]); };
      const quint64 low = q(1).toULongLong(), high = q(2).toULongLong();
      const bool widened = q(3).toLongLong() == (qint64)(qint32)(quint32)low && q(4).toLongLong() == (qint64)(qint32)(quint32)high &&
                           q(5).toULongLong() == low && q(6).toULongLong() == high;
      if (!widened)
      {
        widenMismatch++;
        if (firstMismatch.isEmpty())
          firstMismatch = QString("ID %1: uint32 [%2,%3] int64 [%4,%5] uint64 [%6,%7]").arg(q(0)).arg(q(1)).arg(q(2)).arg(q(3)).arg(q(4)).arg(q(5)).arg(q(6));
      }
      if (q(7).toULongLong() != low)
        strideMismatch++;
      if (q(0).toUInt() == 4120)
        row4120 = QString("ID 4120 read as one uint32 = 0x%1").arg(q(7).toULongLong(), 0, 16);
      const auto st = stored.find(q(0).toUInt());
      if (st == stored.end() || st->second[1].toULongLong() != low || st->second[2].toULongLong() != high ||
          st->second[3] != q(8) || st->second[4] != q(9))
        storedMismatch++;
    }
    delete file;
    delete tbl;
    check("loader: RaceMasks read as int64/uint64 is each uint32 element widened, never a four-byte value copied as eight",
          opened && rows == 1117 && widenMismatch == 0, QString("%1 rows read, %2 mismatching%3").arg(rows).arg(widenMismatch)
            .arg(firstMismatch.isEmpty() ? QString() : "; first " + firstMismatch));
    check("loader: a one-element RaceMasks read uses the file's pallet stride (element 0 of the right entry)",
          opened && rows == 1117 && strideMismatch == 0 && row4120 == "ID 4120 read as one uint32 = 0x8",
          QString("%1 mismatching; %2").arg(strideMismatch).arg(row4120));
    check("loader: the database holds exactly what a direct read gives (RaceMasks, sign-extended ClassMask and OverrideArchive)",
          opened && rows == 1117 && storedMismatch == 0 && stored.size() == 1117, QString("%1 of %2 rows differ").arg(storedMismatch).arg(stored.size()));
  }

  // ---- 3. Option requirements, race bits, prerequisite choices --------------------------------------
  {
    // ChrClasses keeps its id inline in 8 bits: read whole, it came back with three bytes of heap on top.
    sqlResult r = GAMEDATABASE.sqlQuery("SELECT ID, Filename FROM ChrClasses ORDER BY ID");
    QStringList got;
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
      got << r.values[i][0] + ":" + r.values[i][1];
    check("loader: short inline ids read exactly (ChrClasses 1-15)",
          got.join(" ") == "1:WARRIOR 2:PALADIN 3:HUNTER 4:ROGUE 5:PRIEST 6:DEATHKNIGHT 7:SHAMAN 8:MAGE 9:WARLOCK 10:MONK 11:DRUID "
                           "12:DEMONHUNTER 13:EVOKER 14:Adventurer 15:TRAVELER", got.join(" "));
  }
  {
    sqlResult r = GAMEDATABASE.sqlQuery("SELECT ID, Requirement FROM ChrCustomizationOption WHERE ID IN (59, 62, 567, 1584, 6346, 8530) ORDER BY ID");
    QStringList got;
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
      got << r.values[i][0] + ":" + r.values[i][1];
    check("database: ChrCustomizationOption.Requirement", got.join(" ") == "59:0 62:0 567:0 1584:0 6346:4103 8530:12", got.join(" "));
  }
  {
    sqlResult r = GAMEDATABASE.sqlQuery("SELECT ID, PlayableRaceBit FROM ChrRaces WHERE ID IN (1, 2, 5, 12, 22, 34, 35, 36, 37, 52, 70, 86, 91) ORDER BY ID");
    QStringList got;
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
      got << r.values[i][0] + ":" + r.values[i][1];
    check("database: ChrRaces.PlayableRaceBit (not ID - 1 past race 32)",
          got.join(" ") == "1:0 2:1 5:4 12:-1 22:21 34:11 35:12 36:13 37:14 52:16 70:15 86:20 91:19", got.join(" "));
  }
  {
    sqlResult count = GAMEDATABASE.sqlQuery("SELECT COUNT(*), COUNT(DISTINCT ChrCustomizationReqID) FROM ChrCustomizationReqChoice");
    const bool countOk = count.valid && !count.values.empty() && count.values[0][0].toInt() == 2050 && count.values[0][1].toInt() == 342;
    check("database: ChrCustomizationReqChoice 2050 rows over 342 requirements", countOk,
          countOk ? QString() : (count.valid && !count.values.empty() ? count.values[0][0] + " rows, " + count.values[0][1] + " requirements" : QString("query failed")));
    const std::pair<uint, const char *> lists[] = {
      { 58, "967,968,975,977,979" }, { 59, "969,971,976,978,980,983" }, { 61, "6527" }, { 62, "6528" }, { 63, "6529" },
      { 4103, "5330,5331,5332,5333,5334,5335,5344" },
    };
    for (const auto & l : lists)
    {
      const QString got = ids(queryIDs(QString("SELECT ChrCustomizationChoiceID FROM ChrCustomizationReqChoice WHERE ChrCustomizationReqID = %1 "
                                               "ORDER BY ChrCustomizationChoiceID").arg(l.first)));
      check(QString("database: ChrCustomizationReqChoice of Req %1").arg(l.first), got == QLatin1String(l.second), got);
    }
  }

  // ---- 4. The rules on their own, fed stored values ------------------------------------------------
  {
    std::map<uint, std::vector<QString> > req;
    sqlResult r = GAMEDATABASE.sqlQuery("SELECT ID, ReqType, ClassMask, RaceMasks1, RaceMasks2 FROM ChrCustomizationReq "
                                        "WHERE ID IN (10, 12, 55, 141, 142, 143, 144, 146, 4103, 4509, 4576, 4603)");
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
      req[r.values[i][0].toUInt()] = r.values[i];
    const auto reqType = [&req](uint id) { return req.count(id) ? req[id][1].toInt() : -99; };
    const auto classMask = [&req](uint id) { return req.count(id) ? req[id][2].toInt() : -99; };
    const auto raceMask = [&req](uint id) {
      return req.count(id) ? ((req[id][4].toULongLong() & 0xFFFFFFFFull) << 32) | (req[id][3].toULongLong() & 0xFFFFFFFFull) : 0ull;
    };
    const auto raceBit = [](int raceID) {
      sqlResult b = GAMEDATABASE.sqlQuery(QString("SELECT PlayableRaceBit FROM ChrRaces WHERE ID = %1").arg(raceID));
      return (b.valid && !b.values.empty()) ? b.values[0][0].toInt() : -99;
    };

    check("rule: ReqType bit 0 is the player requirement (Req 141 and 55 yes; Req 12 NPC-only and Req 10 no)",
          CharDetails::isPlayerRequirement(reqType(141)) && CharDetails::isPlayerRequirement(reqType(55)) &&
            !CharDetails::isPlayerRequirement(reqType(12)) && !CharDetails::isPlayerRequirement(reqType(10)),
          QString("ReqType 141=%1 55=%2 12=%3 10=%4").arg(reqType(141)).arg(reqType(55)).arg(reqType(12)).arg(reqType(10)));

    // Alliance (Req 4603) and horde (Req 4509) masks: Dark Iron Dwarf 34 and Vulpera 35 are where the
    // playable bit and ID - 1 disagree; Human 1, Orc 2, Mag'har 36 and Mechagnome 37 agree either way.
    const int races[] = { 1, 2, 34, 35, 36, 37 };
    const bool allianceExpected[] = { true, false, true, false, false, true };
    QString raceDetail;
    bool racesOk = true;
    for (size_t i = 0; i < 6; i++)
    {
      const int bit = raceBit(races[i]);
      const bool alliance = CharDetails::raceMaskAllows(raceMask(4603), bit);
      const bool horde = CharDetails::raceMaskAllows(raceMask(4509), bit);
      const bool allianceByID = CharDetails::raceMaskAllows(raceMask(4603), races[i] - 1);
      racesOk = racesOk && alliance == allianceExpected[i] && horde == !allianceExpected[i];
      raceDetail += QString(" race %1 bit %2: alliance %3 horde %4 (ID-1 would say alliance %5)").arg(races[i]).arg(bit)
                      .arg(alliance ? 1 : 0).arg(horde ? 1 : 0).arg(allianceByID ? 1 : 0);
    }
    check("rule: RaceMasks tested at ChrRaces.PlayableRaceBit (faction masks, allied races whose bit is not ID - 1)", racesOk, raceDetail.trimmed());

    bool allRaces = raceMask(141) == ~0ull;
    for (int bit = 0; bit < 64; bit++)
      allRaces = allRaces && CharDetails::raceMaskAllows(raceMask(141), bit);
    allRaces = allRaces && CharDetails::raceMaskAllows(raceMask(141), -1);
    check("rule: the all-races mask passes every bit and a race without one", allRaces, QString("Req 141 mask 0x%1").arg(raceMask(141), 16, 16, QLatin1Char('0')));
    check("rule: an empty race mask passes nobody (Req 4576)",
          raceMask(4576) == 0 && !CharDetails::raceMaskAllows(raceMask(4576), 4) && !CharDetails::raceMaskAllows(raceMask(4576), -1), QString());

    const struct { uint id; bool ordinary; bool demonHunter; } classes[] = {
      { 141, true, true }, { 4103, true, true }, { 146, true, true }, { 144, true, false }, { 142, false, false }, { 143, false, true },
    };
    QString classDetail;
    bool classesOk = true;
    for (const auto & c : classes)
    {
      const bool ordinary = CharDetails::classMaskAllows(classMask(c.id), false);
      const bool demonHunter = CharDetails::classMaskAllows(classMask(c.id), true);
      classesOk = classesOk && ordinary == c.ordinary && demonHunter == c.demonHunter;
      classDetail += QString(" Req %1 ClassMask %2: ordinary %3 DH %4").arg(c.id).arg(classMask(c.id)).arg(ordinary ? 1 : 0).arg(demonHunter ? 1 : 0);
    }
    check("rule: ClassMask bit classID-1 against the class context (ordinary = every class but DK and DH; DH checkbox)", classesOk, classDetail.trimmed());
  }

  // ---- 5. Undead male ---------------------------------------------------------------------------------
  const auto loadCharacter = [frame](int raceID, int sex) -> WoWModel * {
    const int fileID = RaceInfos::getFileIDForRaceSex(raceID, sex);
    GameFile * file = fileID > 0 ? GAMEDIRECTORY.getFile(fileID) : nullptr;
    if (!file)
      return nullptr;
    frame->LoadModel(file);
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
    WoWModel * m = frame->canvas ? const_cast<WoWModel *>(frame->canvas->model()) : nullptr;
    return (m && m->infos.raceID == raceID) ? m : nullptr;
  };
  // The Appearance panel's customization rows, in order: label and dropdown item count.
  const auto panelRows = [frame]() {
    std::vector<std::pair<QString, int> > rows;
    CharDetailsFrame * details = nullptr;
    std::vector<wxWindow *> pending(1, frame->charControl);
    while (!pending.empty() && !details && pending.back())
    {
      wxWindow * w = pending.back();
      pending.pop_back();
      for (wxWindowList::compatibility_iterator node = w->GetChildren().GetFirst(); node; node = node->GetNext())
      {
        if ((details = wxDynamicCast(node->GetData(), CharDetailsFrame)) != nullptr)
          break;
        pending.push_back(node->GetData());
      }
    }
    if (!details)
      return rows;
    for (wxWindowList::compatibility_iterator node = details->GetChildren().GetFirst(); node; node = node->GetNext())
    {
      CharDetailsCustomizationChoice * row = wxDynamicCast(node->GetData(), CharDetailsCustomizationChoice);
      if (!row)
        continue;
      QString label;
      int items = -1;
      for (wxWindowList::compatibility_iterator c = row->GetChildren().GetFirst(); c; c = c->GetNext())
      {
        if (wxStaticText * text = wxDynamicCast(c->GetData(), wxStaticText))
          label = QString::fromStdWString(text->GetLabel().ToStdWstring());
        if (wxBitmapComboBox * combo = wxDynamicCast(c->GetData(), wxBitmapComboBox))
          items = (int)combo->GetCount();
      }
      rows.emplace_back(label, items);
    }
    return rows;
  };
  const auto pumpEvents = []() {
    for (int i = 0; i < 20; i++)
    {
      wxTheApp->Yield(true);
      wxTheApp->ProcessPendingEvents(); // the panel rebuilds its rows after the event that changed them
    }
  };
  const auto optionName = [](uint optionID) {
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = %1").arg(optionID));
    return (r.valid && !r.values.empty()) ? r.values[0][0] : QString();
  };
  const auto rowsDescribe = [](const std::vector<std::pair<QString, int> > & rows) {
    QStringList parts;
    for (const auto & r : rows)
      parts << QString("%1(%2)").arg(r.first).arg(r.second);
    return parts.join(QLatin1Char(' '));
  };
  // The rows the panel should show: one per available option, as many items as valid choices.
  const auto panelMatches = [&](CharDetails & cd) {
    const std::vector<std::pair<QString, int> > rows = panelRows();
    const std::vector<uint> options = cd.getCustomizationOptions();
    bool same = rows.size() == options.size();
    for (size_t i = 0; same && i < rows.size(); i++)
      same = rows[i].first == optionName(options[i]) && rows[i].second == (int)cd.getCustomizationChoices(options[i]).size();
    return same;
  };
  const auto textureFiles = [](const CharDetails & cd, uint type, uint layer) {
    std::vector<uint> files;
    for (const CharDetails::TextureCustomization & t : cd.textures)
      if (t.type == type && t.layer == layer)
        files.push_back(t.fileId);
    return files;
  };

  if (WoWModel * m = loadCharacter(5, 0))
  {
    CharDetails & cd = m->cd;
    check("undead male: identity", m->infos.ChrModelID.size() == 1 && m->infos.ChrModelID[0] == 9 && cd.playableRaceBit() == 4,
          QString("ChrModel %1, PlayableRaceBit %2").arg(m->infos.ChrModelID.empty() ? -1 : m->infos.ChrModelID[0]).arg(cd.playableRaceBit()));

    const QString options = ids(cd.getCustomizationOptions());
    check("undead male: options Face, Skin Type, Skin Color, Hair Style, Hair Color, Jaw Features, Face Features, Eye Color, Eyesight",
          options == "59,567,58,60,61,62,563,534,6346", options);
    check("undead male: Eye Style 8530 excluded by its own requirement (Req 12, not a player requirement)",
          !cd.isOptionAvailable(8530) && cd.getCustomizationChoices(8530).empty() &&
            cd.evaluateRequirement(12, std::map<uint, uint>()) == CharDetails::REQUIREMENT_NOT_PLAYER, QString());

    const QString jaw = ids(cd.getCustomizationChoices(62));
    check("undead male: Jaw Features 11 choices in client order", jaw == "967,968,971,983,969,975,976,977,978,979,980", jaw);
    const QString skinType = ids(cd.getCustomizationChoices(567));
    check("undead male: Skin Type Bony, Mottled, Fresh", skinType == "6527,6528,6529", skinType);
    const QString eyeColor = ids(cd.getCustomizationChoices(534));
    check("undead male: Eye Color 7 (Death Knight 5344 excluded by class, 21648-21661 and Primalist by ReqType, Transmog 15786 by ReqType)",
          eyeColor == "5330,5331,5332,5333,5334,5335,6304", eyeColor);
    const QString eyesight = ids(cd.getCustomizationChoices(6346));
    check("undead male: Eyesight Both, Right, Left, Neither", eyesight == "45118,45119,45120,45121", eyesight);
    check("undead male: Face 11, Hair Style 21, Hair Color 17 (Transmog 966 excluded)",
          cd.getCustomizationChoices(59).size() == 11 && cd.getCustomizationChoices(60).size() == 21 && cd.getCustomizationChoices(61).size() == 17,
          QString("%1 %2 %3").arg(cd.getCustomizationChoices(59).size()).arg(cd.getCustomizationChoices(60).size()).arg(cd.getCustomizationChoices(61).size()));
    check("undead male: defaults are the first valid choices in resolution order",
          cd.get(567) == 6527 && cd.get(58) == 913 && cd.get(62) == 967 && cd.get(563) == 6287 && cd.get(534) == 5330 && cd.get(6346) == 45118 && cd.get(8530) == 0,
          QString("567:%1 58:%2 62:%3 563:%4 534:%5 6346:%6 8530:%7").arg(cd.get(567)).arg(cd.get(58)).arg(cd.get(62)).arg(cd.get(563))
            .arg(cd.get(534)).arg(cd.get(6346)).arg(cd.get(8530)));
    pumpEvents();
    check("undead male: the Appearance panel has one row per available option, each listing the valid choices",
          panelMatches(cd), rowsDescribe(panelRows()));

    unsigned int version = m->stateVersion();
    m->refresh();
    const unsigned int perRefresh = m->stateVersion() - version;
    // One refresh per change; re-selecting the current choice changes nothing and refreshes nothing.
    int asExpected = 0, changes = 0, unchanged = 0;
    const auto change = [&](uint option, uint choice) {
      const bool same = cd.get(option) == choice;
      const unsigned int before = m->stateVersion();
      cd.set(option, choice);
      changes++;
      unchanged += same ? 1 : 0;
      if (m->stateVersion() - before == (same ? 0 : perRefresh))
        asExpected++;
    };

    const QString bony = "913,914,915,916,917,918,6420,6421,6422,6423,6424,50245,50246,50247,50248,50249";
    const QString mottled = "6436,6437,6438,6439,6440,6441,6442,6443,6444,6445,6446,50543,50544,50545,50546,50547";
    const QString fresh = "6425,6426,6427,6428,6429,6430,6431,6432,6433,6434,6435,50538,50539,50540,50541,50542";
    change(567, 6528);
    const QString colours1 = ids(cd.getCustomizationChoices(58));
    const uint colour1 = cd.get(58);
    change(567, 6529);
    const QString colours2 = ids(cd.getCustomizationChoices(58));
    const uint colour2 = cd.get(58);
    change(567, 6527);
    const QString colours3 = ids(cd.getCustomizationChoices(58));
    const uint colour3 = cd.get(58);
    check("undead male: Skin Color follows Skin Type (16 Mottled, 16 Fresh, 16 Bony; an invalid colour gives way to the first valid)",
          colours1 == mottled && colour1 == 6436 && colours2 == fresh && colour2 == 6425 && colours3 == bony && colour3 == 913,
          QString("Mottled [%1] -> %2; Fresh [%3] -> %4; Bony [%5] -> %6").arg(colours1).arg(colour1).arg(colours2).arg(colour2).arg(colours3).arg(colour3));

    change(62, 967);
    const QString ffIntact = ids(cd.getCustomizationChoices(563));
    change(563, 6289);
    change(62, 971);
    const QString ffDrooler = ids(cd.getCustomizationChoices(563));
    const uint ffAfterDrooler = cd.get(563);
    pumpEvents();
    const std::vector<std::pair<QString, int> > rowsDrooler = panelRows();
    change(563, 6292);
    change(62, 975);
    const QString ffBonejawed = ids(cd.getCustomizationChoices(563));
    const uint ffAfterBonejawed = cd.get(563);
    check("undead male: Face Features offers the Rotting (6289 or 6292) that the current Jaw allows, and drops the other when the Jaw changes",
          ffIntact == "6287,6288,6289,6293" && ffDrooler == "6287,6288,6292,6293" && ffAfterDrooler == 6287 &&
            ffBonejawed == "6287,6288,6289,6293" && ffAfterBonejawed == 6287,
          QString("Intact [%1]; Drooler [%2] current %3; Bonejawed [%4] current %5").arg(ffIntact).arg(ffDrooler).arg(ffAfterDrooler)
            .arg(ffBonejawed).arg(ffAfterBonejawed));
    {
      int rottingItems = -1;
      for (const auto & r : rowsDrooler)
        if (r.first == optionName(563))
          rottingItems = r.second;
      check("undead male: the Face Features dropdown is rebuilt for the new Jaw (4 items)", rottingItems == 4, rowsDescribe(rowsDrooler));
    }

    change(534, 6304);
    const bool eyesightGone = !cd.isOptionAvailable(6346) && cd.get(6346) == 0 && ids(cd.getCustomizationOptions()) == "59,567,58,60,61,62,563,534";
    pumpEvents();
    const std::vector<std::pair<QString, int> > rowsSockets = panelRows();
    const bool panelSockets = panelMatches(cd);
    check("undead male: Eye Color Sockets makes Eyesight unavailable (Req 4103), its row goes and no eye layer stays",
          eyesightGone && panelSockets && rowsSockets.size() == 8 && textureFiles(cd, 19, 9).empty() && textureFiles(cd, 19, 10).empty(),
          QString("options [%1]; rows %2").arg(ids(cd.getCustomizationOptions())).arg(rowsDescribe(rowsSockets)));
    change(534, 5331);
    pumpEvents();
    check("undead male: Eyesight comes back with its first valid choice when the Eye Color allows it again",
          cd.isOptionAvailable(6346) && cd.get(6346) == 45118 && panelMatches(cd) && panelRows().size() == 9,
          QString("Eyesight %1; rows %2").arg(cd.get(6346)).arg(rowsDescribe(panelRows())));

    // Textures after repeated changes: exactly the current choices' layers, related gates judged now.
    change(59, 921);
    const QString face921 = ids(textureFiles(cd, 1, 4));
    change(59, 922);
    const QString face922 = ids(textureFiles(cd, 1, 4));
    change(59, 921);
    const QString face921again = ids(textureFiles(cd, 1, 4));
    check("undead male: one Face texture after Face 921 -> 922 -> 921 (Skin Color 913: files 959222, 959228, 959222)",
          face921 == "959222" && face922 == "959228" && face921again == "959222", QString("%1 | %2 | %3").arg(face921).arg(face922).arg(face921again));

    change(61, 956);
    change(60, 941);
    const QString scalp941 = ids(textureFiles(cd, 1, 6));
    change(60, 940);
    const QString scalpBald = ids(textureFiles(cd, 1, 6));
    check("undead male: the Hair Style scalp texture goes with the style (941 with Hair Color 956: 3457610; Bald: none)",
          scalp941 == "3457610" && scalpBald.isEmpty(), QString("941 [%1] Bald [%2]").arg(scalp941).arg(scalpBald));

    change(534, 5330);
    change(534, 5332);
    const QString iris = ids(textureFiles(cd, 19, 9));
    change(6346, 45119);
    const QString irisRight = ids(textureFiles(cd, 19, 9)) + "/" + ids(textureFiles(cd, 19, 10));
    change(6346, 45118);
    const QString irisBoth = ids(textureFiles(cd, 19, 9)) + "/" + ids(textureFiles(cd, 19, 10));
    check("undead male: one iris layer after Eye Color 5330 -> 5332 (3484669); Eyesight Right adds its layer (4705409) and Both removes it",
          iris == "3484669" && irisRight == "3484669/4705409" && irisBoth == "3484669/", QString("%1 | %2 | %3").arg(iris).arg(irisRight).arg(irisBoth));

    check("undead male: every change refreshed the model exactly once (a re-selected current choice not at all)",
          asExpected == changes && perRefresh > 0 && changes > unchanged,
          QString("%1 of %2 selections as expected (%3 re-selected the current choice); one refresh advances the state by %4")
            .arg(asExpected).arg(changes).arg(unchanged).arg(perRefresh));

    {
      QBuffer buffer;
      buffer.open(QIODevice::WriteOnly);
      QXmlStreamWriter writer(&buffer);
      writer.writeStartDocument();
      cd.save(writer);
      writer.writeEndDocument();
      const QString xml = QString::fromUtf8(buffer.data());
      check("undead male: a saved character names only available options (no Eye Style 8530 entry)",
          xml.count("<customization ") == 9 && !xml.contains("id=\"8530\""), QString("%1 customization entries").arg(xml.count("<customization ")));
    }

    {
      // A saved character with the combination the old viewer produced (Mottled with a Bony colour,
      // Eye Style 0) and a choice of an option its Eye Color makes unavailable.
      const QString path = QDir::temp().filePath("wmv_customization_test.chr");
      QFile chr(path);
      if (chr.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        chr.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SavedCharacter version=\"2.0\"><model><CharDetails>"
                  "<customization id=\"58\" value=\"913\"/><customization id=\"534\" value=\"6304\"/>"
                  "<customization id=\"567\" value=\"6528\"/><customization id=\"6346\" value=\"45119\"/>"
                  "<customization id=\"8530\" value=\"0\"/><isDemonHunter value=\"0\"/>"
                  "</CharDetails></model></SavedCharacter>\n");
        chr.close();
      }
      QString loadPath = path;
      const unsigned int before = m->stateVersion();
      cd.load(loadPath);
      const unsigned int loadRefreshes = (m->stateVersion() - before) / (perRefresh ? perRefresh : 1);
      QFile::remove(path);
      check("undead male: loading Mottled + Bony colour 913 + Sockets + Eyesight Right resolves to a valid set with one refresh",
            cd.get(567) == 6528 && cd.get(58) == 6436 && cd.get(534) == 6304 && cd.get(6346) == 0 && cd.get(8530) == 0 && loadRefreshes == 1,
            QString("567:%1 58:%2 534:%3 6346:%4 8530:%5, %6 refresh(es)").arg(cd.get(567)).arg(cd.get(58)).arg(cd.get(534)).arg(cd.get(6346))
              .arg(cd.get(8530)).arg(loadRefreshes));
    }
  }
  else
    check("undead male: model loaded (race 5, sex 0)", false, QString());

  // ---- 6. Night Elf male: the Demon Hunter context ---------------------------------------------------
  if (WoWModel * m = loadCharacter(4, 0))
  {
    CharDetails & cd = m->cd;
    // 709: Skin Color, Req 143 (Demon Hunter only). 45111: Eyesight Right, Req 144 (not DK, not DH).
    // Eye Color 682: 7603 (Req 648, not DK/DH) is valid only without the DH context, and the current
    // choice must be re-validated when the context changes.
    const bool ordinary = !cd.isChoiceAvailable(709) && cd.isChoiceAvailable(45111) && cd.get(682) == 7603;
    cd.setDemonHunterMode(true);
    pumpEvents();
    const uint eyeDH = cd.get(682);
    const bool demonHunter = cd.isChoiceAvailable(709) && !cd.isChoiceAvailable(45111) && eyeDH != 7603 && cd.isChoiceAvailable(eyeDH) && panelMatches(cd);
    cd.setDemonHunterMode(false);
    pumpEvents();
    const uint eyeBack = cd.get(682);
    const bool back = !cd.isChoiceAvailable(709) && cd.isChoiceAvailable(45111) && cd.isChoiceAvailable(eyeBack) && panelMatches(cd);
    check("night elf male: the Demon Hunter checkbox context admits DH-only choices and drops choices that exclude DH, both ways",
          ordinary && demonHunter && back, QString("ordinary %1, DH %2 (Eye Color %3), back %4 (Eye Color %5)").arg(ordinary ? 1 : 0)
            .arg(demonHunter ? 1 : 0).arg(eyeDH).arg(back ? 1 : 0).arg(eyeBack));
  }
  else
    check("night elf male: model loaded (race 4, sex 0)", false, QString());

  // ---- 7. Dracthyr: its own Eye Style stays ----------------------------------------------------------
  // Races 52 and 70 share the dragon model (ChrModel 89, Sex 3); the race map keeps whichever row came first.
  WoWModel * dracthyr = loadCharacter(52, 3);
  if (!dracthyr)
    dracthyr = loadCharacter(70, 3);
  if (WoWModel * m = dracthyr)
  {
    CharDetails & cd = m->cd;
    const QString eyeStyle = ids(cd.getCustomizationChoices(1584));
    check("dracthyr: its own Eye Style (option 1584, Requirement 0, choices of Req 141) is offered",
          cd.isOptionAvailable(1584) && eyeStyle == "19386,19387,19388", QString("[%1], PlayableRaceBit %2").arg(eyeStyle).arg(cd.playableRaceBit()));
  }
  else
    check("dracthyr: model loaded (race 52 or 70, sex 3)", false, QString());

  // ---- 8. Dark Iron Dwarf: an allied race whose bit is not ID - 1 ------------------------------------
  if (WoWModel * m = loadCharacter(34, 0))
  {
    CharDetails & cd = m->cd;
    check("dark iron dwarf: PlayableRaceBit 11 (not 33), options offered, no Eye Style",
          cd.playableRaceBit() == 11 && cd.getCustomizationOptions().size() >= 5 && !cd.isOptionAvailable(8560),
          QString("PlayableRaceBit %1, options [%2]").arg(cd.playableRaceBit()).arg(ids(cd.getCustomizationOptions())));
  }
  else
    check("dark iron dwarf: model loaded (race 34, sex 0)", false, QString());

  LOG_INFO << QString("[customization-test] RESULT: %1 (%2 passed, %3 failed)").arg(failed == 0 ? "PASS" : "FAIL").arg(passed).arg(failed);
  return failed;
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

// Pump the IPC server and the wx queue for ms milliseconds inside an activated event loop, so the canvas timer ticks
// as it does in the running application. A mount choice, a dismount and a change to what a mounted character wears
// load nothing, so no load sends their scene: the canvas tick does (ModelViewer::SendCharacterSceneToUnity), and
// inside OnInit a bare wxYield dispatches no timer message (see the canvas clock of the viewport check).
static void pumpTicking(UnityIpcServer * ipc, long ms)
{
  wxGUIEventLoop loop;
  wxEventLoopActivator activate(&loop);
  wxStopWatch w;
  while (w.Time() < ms)
  {
    ipc->poll();
    wxTheApp->Yield(true);
    wxMilliSleep(10);
  }
}

// The player's answer to the newest scene sent after revision `after` for the character's current load, with the canvas
// ticking while it waits: it must be applied, name the mount key and mount status expected, and no newer scene may be
// out waiting for its own answer. out is that newest answer, seen whether there was one (both for the reason when the
// wait fails). Returns false after limitMs.
static bool waitSceneAnswer(ModelViewer * frame, UnityIpcServer * ipc, const std::vector<UnityIpcServer::SceneAck> & acks,
                            int after, const QString & mountKey, const QString & mountStatus, long limitMs,
                            UnityIpcServer::SceneAck & out, bool & seen)
{
  wxGUIEventLoop loop;
  wxEventLoopActivator activate(&loop);
  wxStopWatch w;
  seen = false;
  while (w.Time() < limitMs)
  {
    ipc->poll();
    wxTheApp->Yield(true);
    wxMilliSleep(10);
    for (size_t i = acks.size(); i-- > 0;)
    {
      const UnityIpcServer::SceneAck & a = acks[i];
      if (a.revision <= after || a.load != frame->m_unityLoadSerial)
        continue;
      out = a;
      seen = true;
      if (a.status == "applied" && a.mountKey == mountKey && a.mountStatus == mountStatus &&
          frame->m_sceneAwaitingRevision == 0)
        return true;
      break;
    }
  }
  return false;
}

// The sequence a host model plays, as a character scene names it; -1 without one.
static int hostSequence(const WoWModel * m)
{
  if (!m || !m->animManager || m->anims.empty())
    return -1;
  const int index = (int)m->animManager->GetAnim();
  return (index >= 0 && index < (int)m->anims.size()) ? index : -1;
}

// The first sequence of a host model with this animation id, or -1.
static int firstSequenceWithAnimId(const WoWModel * m, int animId)
{
  for (size_t i = 0; m && i < m->anims.size(); i++)
    if ((int)m->anims[i].animID == animId)
      return (int)i;
  return -1;
}

// WHAT THE HOST SHOWS after a mounted-character step, as the player must hold it: the character model (the rider) and
// what it plays; the mount its node hangs from, described exactly as the character's scene describes it -- the key, the
// file, and the bone the MOUNT's attachment lookup gives the character's attachment (UnityCharacterScene::buildMount) --
// with what the mount plays; and whether the mount model declares particle and ribbon emitters. All zero / -1 / empty
// for what is not there.
struct RiddenHost
{
  int character = 0;
  int characterSequence = -1;
  int mount = 0;
  QString key;
  int bone = -1;
  int mountSequence = -1;
  bool mountParticles = false;
  bool mountRibbons = false;
};

static RiddenHost riddenHost(ModelViewer * frame)
{
  RiddenHost h;
  WoWModel * character = frame->riderModel();
  if (!character || !character->gamefile)
    return h;
  h.character = (int)character->gamefile->fileDataId();
  h.characterSequence = hostSequence(character);
  WoWModel * mount = frame->riderMount();
  if (!mount)
    return h;
  UnityCharacterScene::Mount described;
  described.model = mount;
  described.attachmentId = frame->charControl->charAtt->id;
  described.serial = frame->charControl->mountSerial;
  described.displayId = frame->charControl->mountDisplayId;
  const QJsonObject o = UnityCharacterScene::buildMount(character, described);
  h.mount = o.value("fileDataID").toInt();
  h.key = o.value("key").toString();
  h.bone = o.value("bone").toInt(-1);
  h.mountSequence = o.value("sequenceIndex").toInt(-1);
  h.mountParticles = mount->header.nParticleEmitters > 0;
  h.mountRibbons = mount->header.nRibbonEmitters > 0;
  return h;
}

// Every way a runtimeState answer differs from what the host shows; empty when the player holds exactly that: the
// character on screen with no load in flight; the host's mount under it by key and file, with exactly one mount runtime
// alive (none without a mount); the character seated where the host's resolved attachment puts it -- at the mount's
// origin when the mount has no such attachment, otherwise under the named bone, or at the attachment's position on the
// root of a mount built without bone transforms; both animators on the host's sequences; the mount's emitters those of
// this mount (none drawn for a mount that declares none, some for one that does) and, while the player's clock runs,
// particle emitters with live particles (a clock pinned by -wmvAnimTime has only spawned up to its instant).
static QStringList riddenMismatches(const UnityIpcServer::RuntimeState & s, const RiddenHost & h)
{
  QStringList bad;
  if (s.modelFileDataID != h.character)
    bad << QString("model on screen %1, the host's character is %2").arg(s.modelFileDataID).arg(h.character);
  if (s.loading)
    bad << "a load is still in flight";
  if (s.mountFileDataID != h.mount)
    bad << QString("mount %1, the host's is %2").arg(s.mountFileDataID).arg(h.mount);
  if (s.mountKey != h.key)
    bad << "mount key \"" + s.mountKey + "\", the host's is \"" + h.key + "\"";
  if (s.liveMounts != (h.mount ? 1 : 0))
    bad << QString("%1 mount runtime(s) alive, expected %2").arg(s.liveMounts).arg(h.mount ? 1 : 0);
  if (!h.mount && s.mountSeat != -1)
    bad << QString("seated (%1) with no mount").arg(s.mountSeat);
  if (h.mount && h.bone < 0 && (s.mountSeat != 0 || s.mountSeatBone != -1))
    bad << QString("seat %1 bone %2: the mount has no such attachment, so the character belongs at its origin (seat 0)")
             .arg(s.mountSeat).arg(s.mountSeatBone);
  if (h.mount && h.bone >= 0 && !(s.mountSeat == 1 && s.mountSeatBone == h.bone) && s.mountSeat != 2)
    bad << QString("seat %1 bone %2: the host's attachment is on bone %3").arg(s.mountSeat).arg(s.mountSeatBone).arg(h.bone);
  if (s.modelSequence != h.characterSequence)
    bad << QString("the character plays sequence %1, the host's %2").arg(s.modelSequence).arg(h.characterSequence);
  if (s.mountSequence != h.mountSequence)
    bad << QString("the mount plays sequence %1, the host's %2").arg(s.mountSequence).arg(h.mountSequence);
  if (s.mountEmitters < 0 || s.mountRibbons < 0 || s.mountParticles < 0)
    bad << "no emitter counts in the answer";
  else
  {
    if (h.mountParticles != (s.mountEmitters > 0))
      bad << QString("%1 particle emitter(s) drawn on a mount that declares %2").arg(s.mountEmitters)
               .arg(h.mountParticles ? "some" : "none");
    if (h.mountRibbons != (s.mountRibbons > 0))
      bad << QString("%1 ribbon emitter(s) drawn on a mount that declares %2").arg(s.mountRibbons)
               .arg(h.mountRibbons ? "some" : "none");
    if (s.mountEmitters > 0 && s.mountParticles == 0 && !qEnvironmentVariable("WMV_DEBUG").contains("-wmvAnimTime"))
      bad << QString("the mount's %1 particle emitter(s) have no live particle").arg(s.mountEmitters);
  }
  return bad;
}

// THE MOUNTED-CHARACTER STEPS of the lifecycle sequence (see doIpcTestLifecycleSequence), protocol 5. Each drives the
// code a user's action runs, then requires the player's answer to the scene it caused (when it causes one) and its
// runtimeState account afterwards to match the host (riddenMismatches), plus what the step itself must or must not
// change against the account before it. what describes the step's outcome for the log; why collects what failed.
static bool doIpcTestMountStep(ModelViewer * frame, UnityIpcServer * ipc, const QString & kind, const QString & target,
                               const std::vector<UnityIpcServer::SceneAck> & acks,
                               const std::vector<UnityIpcServer::RuntimeState> & states, QString & what, QString & why)
{
  QStringList bad;
  CharControl * cc = frame->charControl;
  if (kind == "wait")
  {
    bool number = false;
    const int ms = target.toInt(&number);
    if (!number || ms < 0)
    {
      why = "wait: takes a number of milliseconds";
      return false;
    }
    pumpTicking(ipc, ms);
    what = QString("waited %1 ms").arg(ms);
    return true;
  }
  if (!ipc->playerRidesMounts())
  {
    why = QString("the player (protocol %1) cannot seat characters on mounts").arg(ipc->playerProtocolVersion());
    return false;
  }
  const int revision = frame->m_sceneRevision;
  const int load = frame->m_unityLoadSerial;
  const long answerLimitMs = 120000;
  // The player's account after the step, asked again while it differs: a sequence whose keys are in a .anim, or a
  // mount's first particles, can land a little after the scene's answer.
  const auto settle = [&](const std::function<QStringList(const UnityIpcServer::RuntimeState &)> & differs,
                          UnityIpcServer::RuntimeState & after) {
    bool answered = false;
    waitRuntimeState(ipc, states, [&differs](const UnityIpcServer::RuntimeState & x) { return differs(x).isEmpty(); },
                     30000, after, answered);
    if (!answered)
      bad << "the player never answered runtimeState";
    else
      bad << differs(after);
  };
  // The player's answer to the scene the step caused. Its text is concatenated, never passed through arg().
  const auto answer = [&](const QString & key, const QString & status) {
    UnityIpcServer::SceneAck ack;
    bool seen = false;
    wxStopWatch w;
    const bool matched = waitSceneAnswer(frame, ipc, acks, revision, key, status, answerLimitMs, ack, seen);
    const QString text = seen ? QString("rev %1 in %2 ms: ").arg(ack.revision).arg(w.Time()) + ack.status + ", mount \"" +
                                  ack.mountKey + "\" " + ack.mountStatus +
                                  (ack.mountReason.isEmpty() ? QString() : " (" + ack.mountReason + ")")
                              : QString("no answer");
    if (!matched)
      bad << (seen ? "the scene was answered " + text + ", expected applied with mount \"" + key + "\" " + status
                   : QString("no scene answered for load %1 within %2 s").arg(frame->m_unityLoadSerial)
                       .arg(answerLimitMs / 1000));
    return text;
  };

  if (kind == "chr")
  {
    // LOAD CHARACTER, as the menu does: the character's own scene answers for it, riding nothing.
    if (!QFile::exists(target))
    {
      why = "no such character file: " + target;
      return false;
    }
    frame->LoadChar(target);
    if (!frame->canvasShowsCharacter())
      bad << "the host shows no character after loading it";
    const QString answered = answer(QString(), QStringLiteral("none"));
    UnityIpcServer::RuntimeState after;
    const RiddenHost now = riddenHost(frame);
    settle([&now](const UnityIpcServer::RuntimeState & x) { return riddenMismatches(x, now); }, after);
    what = QString("character %1 loaded (load %2 -> %3): ").arg(now.character).arg(load).arg(frame->m_unityLoadSerial) +
           answered + "; the player holds " + after.describe();
    why = bad.join("; ");
    return bad.isEmpty();
  }

  WoWModel * rider = frame->riderModel();
  if (!rider || !rider->charModelDetails.isChar || !rider->gamefile)
  {
    why = "no character is loaded";
    return false;
  }
  // The player's account before the step, which the one after it is compared with.
  UnityIpcServer::RuntimeState before;
  bool answeredBefore = false;
  waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState &) { return true; }, 5000, before, answeredBefore);
  if (!answeredBefore)
  {
    why = "the player never answered runtimeState before the step";
    return false;
  }
  const RiddenHost was = riddenHost(frame);
  const int images = ipc->stats().imagePushes;
  const int skins = ipc->stats().skinPushes;
  const int geosets = ipc->stats().geosetPushes;
  const int parts = before.liveModels - before.liveMounts;     // the character's body and what it wears
  // ONE CHANNEL for what a ridden mount displays (user 25): its skin, geosets and particle colour travel in the
  // character's scene, so no step that acts on a riding character sends an ordinary modelSkin or modelGeosets --
  // the Animation panel sits on the mount throughout, and a second channel for the same state is what this
  // forbids. Checked after the step has settled, so a push that arrives late is caught too.
  const auto oneChannel = [&]() {
    if (ipc->stats().skinPushes != skins || ipc->stats().geosetPushes != geosets)
      bad << QString("%1 modelSkin and %2 modelGeosets push(es) during the step: what the mount displays belongs in "
                     "the character's scene alone").arg(ipc->stats().skinPushes - skins)
               .arg(ipc->stats().geosetPushes - geosets);
  };
  // What must not change when the character itself does not: its parts, its body's textures and composited images.
  const auto sameCharacter = [&](const UnityIpcServer::RuntimeState & x) {
    QStringList d;
    if (x.liveModels - x.liveMounts != parts)
      d << QString("%1 character runtime(s), %2 before").arg(x.liveModels - x.liveMounts).arg(parts);
    if (x.bodyRebinds != before.bodyRebinds)
      d << QString("the body's textures were bound again (%1 -> %2)").arg(before.bodyRebinds).arg(x.bodyRebinds);
    if (ipc->stats().imagePushes != images)
      d << QString("%1 composited image(s) sent").arg(ipc->stats().imagePushes - images);
    return d;
  };
  const auto noticeUp = [&]() {
    if (frame->unityRendererHost->hasNotice())
      bad << "the viewport shows a notice: " + QString::fromWCharArray(frame->unityRendererHost->noticeTitle().c_str());
  };
  UnityIpcServer::RuntimeState after;
  RiddenHost now;

  if (kind == "mount")
  {
    // THE MOUNT DIALOG'S ROW for this CreatureDisplayInfo id, chosen as the dialog chooses it.
    bool number = false;
    const int display = target.toInt(&number);
    cc->fillMountChoices();
    int row = -1;
    for (size_t i = 0; number && i < cc->numbers.size() && i < cc->cats.size() && row < 0; i++)
      if (cc->cats[i] == 0 && cc->numbers[i] == display)
        row = (int)i;
    if (row < 0)
    {
      why = "no player mount row for display " + target + " in the mount list";
      return false;
    }
    const unsigned serial = cc->mountSerial;
    cc->OnUpdateItem(UPDATE_MOUNT, row);
    now = riddenHost(frame);
    if (!frame->canvasShowsMountedCharacter())
      bad << "the host shows no mounted character after the choice";
    if (cc->mountSerial != serial + 1)
      bad << "the host's mount serial was not raised";
    if (cc->mountDisplayId != display)
      bad << QString("the host keeps display %1").arg(cc->mountDisplayId);
    if (frame->m_unityLoadSerial != load)
      bad << "a load was sent";
    if (now.key == was.key)
      bad << "the mount key did not change: " + now.key;
    if (!g_selModel || g_selModel != frame->riderMount())
      bad << "the Animation panel is not on the mount";
    const QString answered = answer(now.key, QStringLiteral("applied"));
    noticeUp();
    settle([&](const UnityIpcServer::RuntimeState & x) {
      QStringList d = riddenMismatches(x, now) + sameCharacter(x);
      if (x.mountsBuilt != before.mountsBuilt + 1)
        d << QString("%1 mount(s) built, expected one more than %2").arg(x.mountsBuilt).arg(before.mountsBuilt);
      // The view is fitted to the mount and the character together, once (users 26, 47: no camera flicker, no reset loop).
      if (x.viewFramings != before.viewFramings + 1)
        d << QString("the view was fitted %1 time(s), expected one more than %2").arg(x.viewFramings)
                 .arg(before.viewFramings);
      return d;
    }, after);
    const WoWModel * ridden = frame->riderMount();
    what = QString("row %1 display %2 -> ").arg(row).arg(display) + now.key + " " +
           (ridden && ridden->gamefile ? ridden->gamefile->fullname() : QString("-")) +
           QString(" fileDataID %1 bone %2, the character on sequence %3, the mount on %4 (was \"")
             .arg(now.mount).arg(now.bone).arg(now.characterSequence).arg(now.mountSequence) + was.key + "\"): " + answered;
  }
  else if (kind == "dismount")
  {
    // THE MOUNT DIALOG'S "---- None ----" ROW (row 0).
    cc->fillMountChoices();
    cc->OnUpdateItem(UPDATE_MOUNT, 0);
    now = riddenHost(frame);
    if (frame->canvasShowsMountedCharacter() || !frame->canvasShowsCharacter())
      bad << "the host still shows a mount, or no character";
    if (frame->canvas->root->model() != nullptr || cc->mountDisplayId != 0)
      bad << "the host kept a mount on the canvas root, or its display";
    if (frame->m_unityLoadSerial != load)
      bad << "a load was sent";
    // With no mount up the choice changes nothing, and no scene follows.
    const QString answered = was.mount ? answer(QString(), QStringLiteral("none")) : QString("nothing was ridden");
    noticeUp();
    settle([&](const UnityIpcServer::RuntimeState & x) {
      QStringList d = riddenMismatches(x, now) + sameCharacter(x);
      if (x.mountsBuilt != before.mountsBuilt)
        d << QString("%1 mount(s) built, %2 before").arg(x.mountsBuilt).arg(before.mountsBuilt);
      // Fitted to the character alone, once -- and not at all when there was nothing to come off.
      const int framings = before.viewFramings + (was.mount ? 1 : 0);
      if (x.viewFramings != framings)
        d << QString("the view was fitted %1 time(s), expected %2").arg(x.viewFramings).arg(framings);
      return d;
    }, after);
    what = "off \"" + was.key + QString("\", the character on sequence %1: ").arg(now.characterSequence) + answered;
  }
  else if (kind == "manim" || kind == "ranim")
  {
    // THE ANIMATION PANEL, on the mount (manim) or on the character (ranim). When it is on the other model it is moved
    // first, as View > Attachments moves it (AnimControl::UpdateModel), then the clip is picked as a user picks it.
    const bool onMount = kind == "manim";
    WoWModel * model = onMount ? frame->riderMount() : rider;
    WoWModel * other = onMount ? rider : frame->riderMount();
    if (!model || !other)
    {
      why = "the character rides no mount";
      return false;
    }
    bool number = false;
    const int sequence = target.startsWith('#') ? target.mid(1).toInt(&number)
                                                : firstSequenceWithAnimId(model, target.toInt(&number));
    if (!number || sequence < 0 || sequence >= (int)model->anims.size())
    {
      why = QString("the %1 has no sequence for %2").arg(onMount ? "mount" : "character", target);
      return false;
    }
    if (g_selModel != model)
    {
      frame->animControl->UpdateModel(model);
      pumpTicking(ipc, 300);
    }
    const int otherSequence = hostSequence(other);
    const QString suffix = QString("[%1]").arg(sequence);
    int clip = -1;
    for (int i = 0; i < frame->animControl->animationCount() && clip < 0; i++)
      if (QString::fromWCharArray(frame->animControl->animationName(i).c_str()).endsWith(suffix))
        clip = i;
    if (clip < 0)
    {
      why = "the Animation panel lists no clip for sequence " + QString::number(sequence);
      return false;
    }
    const int roles = ipc->stats().rolePushes;
    frame->animControl->pickAnimationLikeUser(clip);
    pumpTicking(ipc, 200);
    now = riddenHost(frame);
    if (hostSequence(model) != sequence)
      bad << QString("the host's %1 plays %2").arg(onMount ? "mount" : "character").arg(hostSequence(model));
    if (hostSequence(other) != otherSequence)
      bad << QString("the host's %1 went from sequence %2 to %3").arg(onMount ? "character" : "mount")
               .arg(otherSequence).arg(hostSequence(other));
    if (ipc->stats().rolePushes <= roles)
      bad << "no animation push with a role";
    settle([&](const UnityIpcServer::RuntimeState & x) {
      QStringList d = riddenMismatches(x, now) + sameCharacter(x);
      if (x.mountsBuilt != before.mountsBuilt)
        d << QString("%1 mount(s) built, %2 before").arg(x.mountsBuilt).arg(before.mountsBuilt);
      if (x.viewFramings != before.viewFramings)
        d << QString("the view was fitted again (%1 -> %2): a clip change moves nothing on screen")
                 .arg(before.viewFramings).arg(x.viewFramings);
      return d;
    }, after);
    what = QString("%1 sequence %2 (animID %3, %4 ms) picked, the %5 stays on sequence %6")
             .arg(onMount ? "mount" : "character").arg(sequence).arg(model->anims[sequence].animID)
             .arg(model->anims[sequence].length).arg(onMount ? "character" : "mount").arg(otherSequence);
  }
  else if (kind == "equip" || kind == "custom" || kind == "sheath")
  {
    // WHAT THE CHARACTER WEARS OR LOOKS LIKE, changed while it rides (or not): an equipment slot pick as the item dialog
    // makes it (OnUpdateItem(UPDATE_ITEM) for the slot being chosen), a choice in the Appearance panel (CharDetails::set),
    // or Character > Sheathe weapons (ModelViewer::OnCharToggle). The character's scene carries it; the mount stays --
    // the same key, nothing built -- and the character's parts, body textures and images may change with it.
    QString change;
    if (kind == "equip")
    {
      bool slotNumber = false, itemNumber = false;
      const int slot = target.section('=', 0, 0).toInt(&slotNumber);
      const int item = target.section('=', 1).toInt(&itemNumber);
      WoWItem * worn = slotNumber && slot >= 0 && slot < NUM_CHAR_SLOTS ? rider->getItem((CharSlots)slot) : nullptr;
      if (!worn || !itemNumber)
      {
        why = "equip: takes <slot>=<item id> (0 takes the item off)";
        return false;
      }
      const int wore = worn->id();
      cc->choosingSlot = slot;
      cc->numbers.assign(1, item);
      cc->cats.assign(1, 0);
      cc->OnUpdateItem(UPDATE_ITEM, 0);
      if (worn->id() != item)
        bad << QString("the host's slot %1 holds item %2").arg(slot).arg(worn->id());
      change = QString("slot %1: item %2 -> %3").arg(slot).arg(wore).arg(item);
    }
    else if (kind == "custom")
    {
      bool optionNumber = false, choiceNumber = true;
      const uint option = target.section('=', 0, 0).toUInt(&optionNumber);
      const std::vector<uint> choices = optionNumber ? rider->cd.getCustomizationChoices(option) : std::vector<uint>();
      const uint current = rider->cd.get(option);
      uint choice = target.contains('=') ? target.section('=', 1).toUInt(&choiceNumber) : 0;
      if (!target.contains('=') && !choices.empty())
      {
        // The option's next choice in its list, after the current one.
        size_t at = 0;
        while (at < choices.size() && choices[at] != current)
          at++;
        choice = choices[at < choices.size() ? (at + 1) % choices.size() : 0];
      }
      if (!optionNumber || !choiceNumber || choices.empty() || choice == current)
      {
        why = "custom: takes <option id>[=<choice id>] of an option of the character with another choice";
        return false;
      }
      rider->cd.set(option, choice);
      if (rider->cd.get(option) != choice)
        bad << QString("the host's option %1 holds choice %2").arg(option).arg(rider->cd.get(option));
      change = QString("option %1: choice %2 -> %3").arg(option).arg(current).arg(choice);
    }
    else
    {
      const bool sheathe = !rider->bSheathe;
      frame->charMenu->Check(ID_SHEATHE, sheathe);
      wxCommandEvent toggle(wxEVT_MENU, ID_SHEATHE);
      toggle.SetInt(sheathe ? 1 : 0);
      frame->OnCharToggle(toggle);
      if (rider->bSheathe != sheathe)
        bad << "the host's sheathe flag did not follow";
      change = sheathe ? QString("weapons sheathed") : QString("weapons in hand");
    }
    now = riddenHost(frame);
    if (now.key != was.key)
      bad << "the mount key changed: \"" + was.key + "\" -> \"" + now.key + "\"";
    if (frame->m_unityLoadSerial != load)
      bad << "a load was sent";
    const QString answered = answer(now.key, now.mount ? QStringLiteral("applied") : QStringLiteral("none"));
    noticeUp();
    const bool sheathing = kind == "sheath";
    settle([&](const UnityIpcServer::RuntimeState & x) {
      QStringList d = riddenMismatches(x, now);
      if (x.mountsBuilt != before.mountsBuilt)
        d << QString("%1 mount(s) built, %2 before: the mount was built again").arg(x.mountsBuilt).arg(before.mountsBuilt);
      // An appearance change must not move the camera (user 26: no camera flicker).
      if (x.viewFramings != before.viewFramings)
        d << QString("the view was fitted again (%1 -> %2): what the character wears does not re-aim the camera")
                 .arg(before.viewFramings).arg(x.viewFramings);
      // Sheathing moves what is in the hands and changes neither what is worn nor the body.
      if (sheathing)
        d << sameCharacter(x);
      return d;
    }, after);
    what = change + ": " + answered +
           QString("; character runtimes %1 -> %2, body texture binds %3 -> %4, composited images sent %5")
             .arg(parts).arg(after.liveModels - after.liveMounts).arg(before.bodyRebinds).arg(after.bodyRebinds)
             .arg(ipc->stats().imagePushes - images);
  }
  else if (kind == "reconnect")
  {
    // VIEW > RESTART UNITY RENDERER: a new player connects and is sent what the host shows -- the character's load, then
    // its scene with the mount it rides. It builds both for that load (a job's mount) and holds exactly what the one
    // before it held. The new player starts its log afresh, so the log so far is copied beside it first.
    static int reconnects = 0;
    const QString logDir = QString::fromWCharArray(wxFileName(wxStandardPaths::Get().GetExecutablePath())
                                                     .GetPath(wxPATH_GET_VOLUME).c_str()) + "/userSettings/";
    const QString kept = logDir + QString("unityRenderer.before-reconnect-%1.log").arg(++reconnects);
    QFile::remove(kept);
    const bool copied = QFile::copy(logDir + "unityRenderer.log", kept);
    frame->RestartUnityRenderer();
    {
      wxGUIEventLoop loop;
      wxEventLoopActivator activate(&loop);
      wxStopWatch w;
      while (w.Time() < 90000 && !(ipc->isUnityReady() && frame->m_unityLoadSerial > load))
      {
        ipc->poll();
        wxTheApp->Yield(true);
        wxMilliSleep(10);
      }
    }
    now = riddenHost(frame);
    if (!ipc->playerRidesMounts() || frame->m_unityLoadSerial <= load)
      bad << QString("the restarted player (protocol %1) was sent no load").arg(ipc->playerProtocolVersion());
    if (frame->m_unityLoadedFileDataID != was.character || !frame->m_unityLoadedCharacter)
      bad << QString("the load named %1 %2, not the character %3").arg(frame->m_unityLoadedFileDataID)
               .arg(frame->m_unityLoadedCharacter ? "(character)" : "(model)").arg(was.character);
    if (now.key != was.key)
      bad << "the mount key changed: \"" + was.key + "\" -> \"" + now.key + "\"";
    const QString answered = answer(now.key, now.mount ? QStringLiteral("applied") : QStringLiteral("none"));
    noticeUp();
    settle([&](const UnityIpcServer::RuntimeState & x) {
      QStringList d = riddenMismatches(x, now);
      if (x.liveModels != before.liveModels)
        d << QString("%1 model runtime(s), the player before held %2").arg(x.liveModels).arg(before.liveModels);
      if (x.mountsBuilt != (now.mount ? 1 : 0))
        d << QString("%1 mount(s) built by the new player, expected %2").arg(x.mountsBuilt).arg(now.mount ? 1 : 0);
      // The new player fits the view once for the model it puts on screen, and once more when the mount goes under it.
      if (x.viewFramings != (now.mount ? 2 : 1))
        d << QString("the new player fitted the view %1 time(s), expected %2").arg(x.viewFramings).arg(now.mount ? 2 : 1);
      return d;
    }, after);
    what = QString("load %1 -> %2 names %3 as a character; ").arg(load).arg(frame->m_unityLoadSerial)
             .arg(frame->m_unityLoadedFileDataID) + answered +
           (copied ? "; the log before it kept as " + kept : QString("; the log before it could not be copied"));
  }
  else
  {
    why = "unknown step kind";
    return false;
  }
  oneChannel();
  what += "; the player holds " + after.describe();
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
//     alive under this model would pass there. The answer must name no mount either, and no mount runtime may
//     be alive (protocol 5).
//   no step may produce a "failed" world-model report.
// Entries are "m2:" or "wmo:" followed by a listfile path or a FileDataID. "m2!:" / "wmo!:" is a QUICK step:
// selected and left at once, the way a user steps through the Browse tree, so the next load replaces one
// still in flight. It asserts nothing itself; the next waited step then also requires an answer for every
// quick world-model load ("superseded", or "built" if it won the race -- never "failed" or none), and its
// own checks prove the replaced load left nothing behind. So a quick step must be followed by a waited
// one: a sequence ending on a quick step is rejected. Returns whether all steps passed.
//
// MOUNTED CHARACTERS (protocol 5; doIpcTestMountStep). These steps act on the character on the canvas, as its
// menus, panels and dialogs do, and then require the player's answer to the scene the step caused (when it causes
// one) and its runtimeState account to match what the host shows (riddenMismatches): the character on screen, the
// mount by key and file with exactly one mount runtime alive, the seat the host's resolved attachment gives, both
// models on the host's sequences, the mount's emitters its own, and no notice. No step may send an ordinary
// modelSkin or modelGeosets either: what a ridden mount displays travels in the character's scene and nowhere else.
//   chr:<file.chr>        Load Character (ModelViewer::LoadChar); the character's scene answered, riding nothing;
//   mount:<displayId>     the mount dialog's row for that CreatureDisplayInfo id, chosen as the dialog chooses it
//                         (CharControl::fillMountChoices, OnUpdateItem(UPDATE_MOUNT, row)): the host rides it with a
//                         new serial and sends no load; the scene answered with the new key applied; one mount more
//                         built, none left over, the character's parts, body texture binds and images unchanged, and
//                         the view fitted exactly once to the mount and the character together;
//   dismount              the dialog's "---- None ----" row: the scene answered with mount "none", no mount runtime
//                         alive, no mount built, the character unchanged, and the view fitted once to the character
//                         alone (not at all when there was nothing to come off);
//   manim:<animId>        the mount's first sequence with that animation id picked in the Animation panel (moved to
//   ranim:<animId>        the mount first, as View > Attachments moves it, when it is on the character); ranim the
//                         same for the character. "#<n>" names sequence n instead. The other model's sequence stays
//                         and the view is not fitted again;
//   equip:<slot>=<item>   an equipment slot pick (OnUpdateItem(UPDATE_ITEM), 0 takes the item off);
//   custom:<option>[=<choice>]  an Appearance choice (CharDetails::set), by default the option's next choice;
//   sheath                Character > Sheathe weapons toggled (ModelViewer::OnCharToggle); these three must reach
//                         the player in a scene that keeps the mount's key, with no mount built and the view not
//                         fitted again; sheathing also leaves the character's parts, body texture binds and images
//                         as they were;
//   reconnect             View > Restart Unity Renderer: the new player's load names the character as a character,
//                         its scene answers with the same mount key, and it holds what the player before it held,
//                         having built exactly the one mount and fitted the view twice -- once for the model it put
//                         on screen, once for the mount that went under it (the log before the restart is copied
//                         beside the new one's as unityRenderer.before-reconnect-<n>.log);
//   wait:<ms>             pumps with the canvas ticking (lets a WMV_VIEWPORT_SHOT capture land before the test ends).
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
    // "dismount", "sheath" and "reconnect" take nothing after them.
    const QString rawKind = (colon > 0 ? step.left(colon) : step).trimmed().toLower();
    const bool quick = rawKind.endsWith('!');
    const QString kind = quick ? rawKind.left(rawKind.size() - 1) : rawKind;
    const QString target = colon > 0 ? step.mid(colon + 1).trimmed() : QString();
    const bool mountKind = !quick && (kind == "chr" || kind == "mount" || kind == "dismount" || kind == "manim" ||
                                      kind == "ranim" || kind == "equip" || kind == "custom" || kind == "sheath" ||
                                      kind == "reconnect" || kind == "wait");
    GameFile * file = (kind == "m2" || kind == "wmo") && !target.isEmpty() ? resolveGameFileArg(target) : nullptr;
    const size_t reportsBefore = reports.size();
    const int serialBefore = frame->m_unityLoadSerial;
    QElapsedTimer clock;
    clock.start();
    QString why;
    bool ok = true;

    if (mountKind)
    {
      QString what;
      ok = doIpcTestMountStep(frame, ipc, kind, target, sceneAcks, runtimeStates, what, why);
      LOG_INFO << "[unityipc-test]   step" << (s + 1) << kind.toLatin1().constData() << "--" << what;
    }
    else if ((kind != "m2" && kind != "wmo") || !file)
    {
      ok = false;
      why = (kind != "m2" && kind != "wmo")
              ? "unknown step kind (use m2:, wmo:, m2!:, wmo!:, chr:, mount:, dismount, manim:, ranim:, equip:, custom:, "
                "sheath, reconnect or wait:)"
              : "file not found";
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
        // No mount either: a model or character loaded after a mounted character rides nothing (a player older
        // than protocol 5 reports no mount fields, -1).
        const bool held = waitRuntimeState(ipc, runtimeStates, [fdid, character](const UnityIpcServer::RuntimeState & x) {
          return x.liveMapObjects == 0 && x.mapObjectFileDataID == 0 && x.modelFileDataID == fdid &&
                 (character ? x.liveModels >= 1 : x.liveModels == 1) && x.mountFileDataID <= 0 && x.liveMounts <= 0;
        }, 30000, st, answered);
        if (!answered)
          bad << "the player never answered runtimeState";
        else if (!held)
          bad << "the player holds " + st.describe() +
                   QString(", expected model %1, no world model, liveMapObjects 0, liveModels %2, no mount")
                     .arg(fdid).arg(character ? ">= 1" : "1");
        else
          confirmed = (confirmed == "unconfirmed" ? QString() : confirmed + "; ") + "runtime state " + st.describe();
      }
      ok = bad.isEmpty();
      why = bad.join("; ");
      LOG_INFO << "[unityipc-test]   step" << (s + 1) << "model:" << confirmed;
    }

    // Every quick world-model load before a waited step must have been answered by now, and not "failed".
    if (mountKind || (!quick && file && (kind == "m2" || kind == "wmo")))
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

      // DISMOUNT. Choosing "None" in the mount list must hand the canvas back to the character without
      // freeing it: first with no mount up (the canvas model IS the character), then with one up, set up
      // the way the mount choice sets it up (the mount on the root and on the canvas, the character kept
      // on its attachment). Checked: canvas and root afterwards, the scale the character takes back from
      // the mount, and the player dressing the character again.
      CharControl * cc = frame->charControl;
      const std::vector<int> savedNumbers = cc->numbers, savedCats = cc->cats;
      cc->numbers.assign(1, -1);   // the list's "None" entry
      cc->cats.assign(1, 0);
      cc->OnUpdateItem(UPDATE_MOUNT, 0);
      const bool unmountedOk = frame->canvas->model() == cm && frame->canvas->root->model() == nullptr &&
                               cc->model == cm && !cm->geosets.empty();
      LOG_INFO << "[unityipc-test] character: \"None\" with no mount up -> canvas keeps the character"
               << (unmountedOk ? "(OK)" : "(FAIL)");

      bool mountedOk = false;
      GameFile * mountFile = GAMEDIRECTORY.getFile(QString("creature/bear/bear.m2"));
      WoWModel * mount = mountFile ? new WoWModel(mountFile, false) : nullptr;
      if (mount && mount->ok)
      {
        const float savedScale = cm->scale_;
        const float mountScale = 1.25f;
        const int revisionBefore = frame->m_sceneRevision;
        mount->isMount = true;
        mount->scale_ = mountScale;
        frame->canvas->root->setModel(mount);
        frame->canvas->setModel(mount, true);
        // Every mount model the mount choice puts up gets a new serial, which the character's scene names it by
        // (CharControl::mountSerial); this one too.
        cc->mountSerial++;

        // MOUNTED. The canvas tick decides the viewport, and it runs only inside an activated event loop (see the canvas
        // clock above). A player that seats characters on mounts (protocol 5) keeps the viewport on the character and is
        // sent the character's scene with the bear in it: it must answer it with the bear applied, show no notice, and
        // hold the character with the bear under it. An older player must get the mounted-character notice instead.
        std::vector<UnityIpcServer::SceneAck> acks;
        std::vector<UnityIpcServer::RuntimeState> states;
        const auto previousScene = ipc->onCharacterSceneApplied;
        const auto previousRuntime = ipc->onRuntimeState;
        ipc->onCharacterSceneApplied = [&acks, previousScene](const UnityIpcServer::SceneAck & a) {
          acks.push_back(a);
          if (previousScene)
            previousScene(a);
        };
        ipc->onRuntimeState = [&states, previousRuntime](const UnityIpcServer::RuntimeState & s) {
          states.push_back(s);
          if (previousRuntime)
            previousRuntime(s);
        };
        const bool seats = ipc->playerRidesMounts();
        const int characterId = (int)cm->gamefile->fileDataId();
        const int mountId = (int)mountFile->fileDataId();
        const QString key = QString("M%1").arg(cc->mountSerial);
        bool shownMounted = false;
        QString mountedText;
        if (seats)
        {
          UnityIpcServer::SceneAck ack;
          bool seen = false;
          const bool applied = waitSceneAnswer(frame, ipc, acks, revisionBefore, key, QStringLiteral("applied"), 60000, ack, seen);
          UnityIpcServer::RuntimeState held;
          bool answered = false;
          const bool holds = waitRuntimeState(ipc, states, [&](const UnityIpcServer::RuntimeState & x) {
            return x.modelFileDataID == characterId && x.mountFileDataID == mountId && x.mountKey == key &&
                   x.liveMounts == 1 && !x.loading;
          }, 30000, held, answered);
          const bool noticeUp = frame->unityRendererHost->hasNotice() || !frame->unityCanDrawCurrentModel();
          shownMounted = applied && holds && !noticeUp;
          mountedText = (seen ? "answered " + ack.status + " with mount \"" + ack.mountKey + "\" " + ack.mountStatus
                              : QString("no scene answered")) +
                        (noticeUp ? QString(", a notice is up") : QString(", no notice")) + "; the player holds " +
                        held.describe();
        }
        else
        {
          pumpTicking(ipc, 1000);
          ModelViewer::ViewportNotice notice;
          shownMounted = !frame->unityCanDrawCurrentModel(&notice) && frame->unityRendererHost->hasNotice();
          mountedText = "notice: " + QString::fromWCharArray(notice.title.c_str());
        }
        LOG_INFO << "[unityipc-test] character: a bear put up by hand as" << key.toLatin1().constData() << "->"
                 << (seats ? "seated in the Unity viewport:" : "an older player:") << mountedText
                 << (shownMounted ? "(OK)" : "(FAIL)");

        const int beforeDismount = ipc->stats().sceneApplied;
        cc->OnUpdateItem(UPDATE_MOUNT, 0);   // frees the mount
        const bool handedBack = frame->canvas->model() == cm && frame->canvas->root->model() == nullptr;
        const bool scaleBack = cc->charAtt == nullptr || cm->scale_ == mountScale;
        cm->scale_ = savedScale;
        frame->SendCharacterSceneToUnity(true);
        const long redressed = waitApplied(beforeDismount, 60000);
        // Off again: to a player that seats mounts the scene answers with no mount, and the bear is gone from the player.
        bool offOk = true;
        if (seats)
        {
          UnityIpcServer::RuntimeState held;
          bool answered = false;
          offOk = ipc->stats().lastMountAck.startsWith("- none") &&
                  waitRuntimeState(ipc, states, [&](const UnityIpcServer::RuntimeState & x) {
                    return x.modelFileDataID == characterId && x.mountFileDataID == 0 && x.liveMounts == 0 && !x.loading;
                  }, 30000, held, answered);
          LOG_INFO << "[unityipc-test] character: the bear taken off -> last mount answer" << ipc->stats().lastMountAck
                   << "; the player holds" << held.describe() << (offOk ? "(OK)" : "(FAIL)");
        }
        ipc->onCharacterSceneApplied = previousScene;
        ipc->onRuntimeState = previousRuntime;
        LOG_INFO << "[unityipc-test] character: \"None\" with a mount up -> canvas back to the character="
                 << handedBack << "scale taken back=" << scaleBack << "| scene applied again after" << redressed << "ms";
        mountedOk = handedBack && scaleBack && redressed >= 0 && shownMounted && offOk;
      }
      else
      {
        delete mount;
        LOG_ERROR << "[unityipc-test] character: dismount with a mount up NOT checked (creature/bear/bear.m2 did not load)";
      }
      cc->numbers = savedNumbers;
      cc->cats = savedCats;
      characterOk = characterOk && unmountedOk && mountedOk;
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
  // nothing to sync, so the condition only bites when there was something to send. A run that ends on a
  // character riding a mount has the Animation panel on the mount's skins, and to a player that seats it the
  // mount's skin travels only in the character's scene (no modelSkin is sent for it): a scene answered with a
  // mount applied is that push.
  const bool skinsOk = (frame->animControl == NULL) || (frame->animControl->skinCount() == 0) ||
                       (st.skinPushes >= 1) ||
                       (frame->canvasShowsMountedCharacter() && ipc->playerRidesMounts() && ipc->stats().mountApplied >= 1);
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
        a == "-m2inspect" || a == "-matrestest" || a == "-customizationtest" || a == "-mpq" || a == "-item" || a == "-wmo" ||
        a.endsWith(".chr"))
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
  bool customizationTest = false;                    // -customizationtest: character customization checks
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
    else if (cmd == "-customizationtest") {
      // Regression checks for character customization requirements; see doHeadlessCustomizationTest.
      customizationTest = true;
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
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" || a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" || a == "-m2inspect" || a == "-matrestest" || a == "-customizationtest" || a == "-mpq" || a == "-item" || a == "-wmo" || a.endsWith(".chr"))
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

    if (customizationTest)
    {
      doHeadlessCustomizationTest(frame);
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


