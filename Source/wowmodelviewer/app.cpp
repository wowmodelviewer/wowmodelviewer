#include "app.h"
#include <memory>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <set>

#include <wx/app.h>
#include <wx/checklst.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/listctrl.h>
#include <wx/mstream.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>
#ifdef __WXMSW__
#include <wx/msw/darkmode.h>
#endif
#include <wx/sysopt.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <chrono>
#include <signal.h>
#include <unistd.h>
#endif

#include "CustomizationGeosetTest.h"
#include "ExporterPlugin.h"
#include "Game.h"
#include "GameFolder.h" // core::GameConfig
#include "GlobalSettings.h"
#include "ViewportBackground.h"
#include "globalvars.h"
#include "LogStackWalker.h"
#include "PluginManager.h"
#include "AppPaths.h"
#include "modelcanvas.h"
#include "ViewportHost.h"
#include "UiStyle.h"
#include "UiControls.h"
#include "TextureDecode.h"
#include <wx/checkbox.h>
#include <wx/choice.h>
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "WorldMapCatalog.h"
#include "LiquidCatalog.h"
#include "LightCatalog.h"
#include "CascLib.h"
#include <QCryptographicHash>
#include "animcontrol.h"
#include "AnimManager.h"
#include "Attachment.h"
#include "filecontrol.h"
#include "itemselection.h"
#include "wmo.h"
#include "WMOGroup.h"
#include "WoWModel.h"

#include "logger/Logger.h"
#include "logger/LogOutputConsole.h"
#include "logger/LogOutputFile.h"

#include <QElapsedTimer>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "TextureManager.h"
#include "ModelIdLookup.h"
#include "ClientState.h"

#include <wx/evtloop.h>
#include <wx/stopwatch.h>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
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
                                bool component = false, const QString & equipmentPath = QString(),
                                bool allCustomizations = false, bool includeEquipment = false)
{
  if (!equipmentPath.isEmpty() && !frame->LoadFbxEquipment(equipmentPath))
  {
    LOG_ERROR << "[fbxequipment] invalid or unrestorable snapshot:" << equipmentPath;
    writeFbxStatus(outPath, "ERROR\tCould not restore equipment snapshot");
    return;
  }
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

  // Diagnostic producer: exercise the GUI descriptor builder, then let the regression harness
  // launch the resulting command in a fresh process and remove its temporary snapshot.
  const QString descriptorPath = QString::fromLocal8Bit(qgetenv("WMV_FBX_DESCRIBE"));
  if (!descriptorPath.isEmpty())
  {
    wxString args, label, snapshot;
    const bool prepared = frame->PrepareFbxAsset(args, label, snapshot);
    QJsonObject descriptor;
    descriptor["assetArgs"] = QString::fromStdWString(args.ToStdWstring());
    descriptor["snapshot"] = QString::fromStdWString(snapshot.ToStdWstring());
    descriptor["build"] = frame->m_loadedBuild;
    descriptor["product"] = frame->m_loadedProduct;
    QFile output(descriptorPath);
    const QByteArray json = QJsonDocument(descriptor).toJson();
    const bool written = prepared && output.open(QIODevice::WriteOnly) &&
                         output.write(json) == json.size() && output.flush();
    if (!written && !snapshot.IsEmpty()) wxRemoveFile(snapshot);
    writeFbxStatus(outPath, written ? "OK" : "ERROR\tCould not prepare export descriptor");
    return;
  }

  plugin->setExportOptions(mesh, skel, skin, anim);
  plugin->setComponentRawExport(component);
  plugin->setCustomizationExport(allCustomizations, includeEquipment, frame->m_loadedBuild.toStdString(), frame->m_loadedProduct.toStdString());

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

// -customizationtest, every client: a humanoid NPC wears its own stored appearance (CreatureDisplayInfoExtra,
// CreatureDisplayInfoOption) without the player's lists ever offering what only NPCs wear. The cases are found in the
// loaded client's own tables, in Creature order -- no NPC is named:
//   - the first humanoid NPC whose race wears its display's model: race, sex, every stored choice of the model current
//     and kept as stored, every other option given a valid choice (an option the NPC stores nothing for, often Ears,
//     must not be left without one), and a pick of the user's taking that option back to the player's rules;
//   - the first that stores an NPC-only choice of a player option: current, and not among the player's choices;
//   - the first that stores a choice of an option no player is offered (Eye Style): current, and not offered;
//   - the first whose race wears its model but is not the first race on the file (MoP Classic's Human NPCs on the
//     files Gilnean is listed on first, Classic Beta's Windshaper Skyborne): that race;
//   - the first Demon Hunter NPC (its display's class is 12, in a client that has the class): in that class context,
//     and a player character loaded on the same model file afterwards (as an Armory import is) is not;
//   - a saved NPC loads back as it was saved, a player character afterwards carries nothing of the NPC, and a saved
//     player character loads back as its race, with nothing marked stored.
template <class Check, class Skip>
static void checkNpcAppearance(ModelViewer * frame, const Check & check, const Skip & skip)
{
  const auto canvasModel = [frame]() { return frame->canvas ? const_cast<WoWModel *>(frame->canvas->model()) : nullptr; };
  const auto settle = []() {
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
  };
  const auto rows = [](const QString & table) {
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1").arg(table));
    return (r.valid && !r.values.empty()) ? r.values[0][0].toInt() : 0;
  };
  const auto notInstalled = [](const QString & table) {
    wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
    return folder && folder->isRemoteFile(QString("dbfilesclient/%1.db2").arg(table.toLower()));
  };
  for (const QString table : { QString("CreatureDisplayInfoExtra"), QString("CreatureDisplayInfoOption") })
    if (rows(table) == 0)
    {
      if (notInstalled(table))
        skip("NPC appearance", QString("the client's %1 is not installed on this computer").arg(table));
      else
        check(QString("NPC appearance: %1 was read").arg(table), false, "the table holds no row");
      return;
    }

  struct Stored { uint option; uint choice; bool npcOnlyChoice; bool npcOnlyOption; };
  // A case is an NPC (a Creature row, loaded with View NPC) or, in a client whose Creature table lists no humanoid NPC
  // (Classic Era's lists one creature), a creature display with extended info (loaded with Load NPC / Model by
  // Creature Display ID): creature is then the display's ID.
  struct Case { uint creature = 0; bool byDisplay = false; QString who; uint extra = 0; int race = -1; int sex = -1; int classID = 0;
                std::vector<Stored> stored; };
  const auto storedOf = [](uint extra) {
    std::vector<Stored> stored;
    sqlResult r = GAMEDATABASE.sqlQuery(QString(
      "SELECT o.ChrCustomizationOptionID, o.ChrCustomizationChoiceID, IFNULL(cq.ReqType, -1), IFNULL(oq.ReqType, -1) "
      "FROM CreatureDisplayInfoOption o "
      "LEFT JOIN ChrCustomizationChoice c ON c.ID = o.ChrCustomizationChoiceID "
      "LEFT JOIN ChrCustomizationReq cq ON cq.ID = c.ChrCustomizationReqID "
      "LEFT JOIN ChrCustomizationOption op ON op.ID = o.ChrCustomizationOptionID "
      "LEFT JOIN ChrCustomizationReq oq ON oq.ID = op.Requirement "
      "WHERE o.CreatureDisplayInfoExtraID = %1 ORDER BY o.ID").arg(extra));
    std::set<uint> seen;
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
    {
      const uint option = r.values[i][0].toUInt();
      if (!seen.insert(option).second)
        continue; // an option stored twice keeps its first row
      const int choiceType = r.values[i][2].toInt(), optionType = r.values[i][3].toInt();
      const bool optionNpcOnly = optionType >= 0 && !CharDetails::isPlayerRequirement(optionType);
      stored.push_back(Stored{ option, r.values[i][1].toUInt(),
                               !optionNpcOnly && choiceType >= 0 && !CharDetails::isPlayerRequirement(choiceType), optionNpcOnly });
    }
    return stored;
  };

  // Find the cases.
  Case plain, npcChoice, npcOption, sharedFile, demonHunter;
  const bool clientHasDH = (CharDetails::clientClassMask() & (1u << (CLASS_DEMONHUNTER - 1))) != 0;
  const auto findCases = [&](const sqlResult & npcs, bool byDisplay) {
  for (size_t i = 0; npcs.valid && i < npcs.values.size(); i++)
  {
    if (plain.creature && npcChoice.creature && npcOption.creature && sharedFile.creature && (demonHunter.creature || !clientHasDH))
      break;
    Case c;
    c.creature = npcs.values[i][0].toUInt();
    c.byDisplay = byDisplay;
    c.who = QString(byDisplay ? "creature display %1" : "NPC %1").arg(c.creature);
    c.extra = npcs.values[i][2].toUInt();
    c.race = npcs.values[i][3].toInt();
    c.sex = npcs.values[i][4].toInt();
    c.classID = npcs.values[i][5].toInt();
    const int file = RaceInfos::getHDModelForFileID(npcs.values[i][1].toInt());
    RaceInfos wearer, first;
    if (!GAMEDIRECTORY.getFile(file) || !RaceInfos::getRaceInfosForRaceSex(c.race, c.sex, wearer) || wearer.modelFileID != file ||
        !RaceInfos::getRaceInfosForFileID(file, first))
      continue; // the display's race does not wear its model, or the model is not installed
    c.stored = storedOf(c.extra);
    if (c.stored.empty())
      continue;
    if (!plain.creature)
      plain = c;
    if (!npcChoice.creature && std::any_of(c.stored.begin(), c.stored.end(), [](const Stored & s) { return s.npcOnlyChoice; }))
      npcChoice = c;
    if (!npcOption.creature && std::any_of(c.stored.begin(), c.stored.end(), [](const Stored & s) { return s.npcOnlyOption; }))
      npcOption = c;
    if (!sharedFile.creature && first.raceID != c.race)
      sharedFile = c;
    if (!demonHunter.creature && clientHasDH && c.classID == CLASS_DEMONHUNTER)
      demonHunter = c;
  }
  };
  const QString joins = "JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
                        "JOIN CreatureDisplayInfoExtra ON CreatureDisplayInfo.ExtendedDisplayInfoID = CreatureDisplayInfoExtra.ID ";
  const QString columns = "CreatureModelData.FileDataID, CreatureDisplayInfoExtra.ID, CreatureDisplayInfoExtra.DisplayRaceID, "
                          "CreatureDisplayInfoExtra.DisplaySexID, CreatureDisplayInfoExtra.DisplayClassID ";
  findCases(GAMEDATABASE.sqlQuery("SELECT Creature.ID, " + columns + "FROM Creature "
                                  "JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID " + joins + "ORDER BY Creature.ID"),
            false);
  if (!plain.creature)
  {
    LOG_INFO << QString("[customization-test] no humanoid NPC in this client's Creature table (%1 row(s)): the cases are its creature "
                        "displays with extended info").arg(rows("Creature"));
    findCases(GAMEDATABASE.sqlQuery("SELECT CreatureDisplayInfo.ID, " + columns + "FROM CreatureDisplayInfo " + joins +
                                    "ORDER BY CreatureDisplayInfo.ID"), true);
  }
  if (!plain.creature)
  {
    check("NPC appearance: a humanoid NPC or creature display whose race wears its model, with stored choices", false, "none found");
    return;
  }

  // Loads the NPC; the model when it loaded as that race's character NPC, else null (and a failed check).
  const auto loadNpc = [&](const Case & c, const QString & what) -> WoWModel * {
    if (c.byDisplay)
    {
      ModelIdLookup::Resolved resolved;
      wxString why;
      if (ModelIdLookup::resolve(ModelIdLookup::Kind::CreatureDisplay, (int)c.creature, resolved, why))
        frame->LoadModelById(resolved, why);
    }
    else
      frame->LoadNPC(c.creature);
    settle();
    WoWModel * m = canvasModel();
    const bool ok = m && m->charModelDetails.isChar && m->cd.isNPC && m->infos.raceID == c.race && m->infos.sexID == c.sex;
    check(QString("NPC appearance, %1: %2 (extended display %3) loads as a character NPC of race %4 sex %5")
            .arg(what).arg(c.who).arg(c.extra).arg(c.race).arg(c.sex), ok,
          m ? QString("isChar %1, NPC %2, race %3 sex %4").arg(m->charModelDetails.isChar ? 1 : 0).arg(m->cd.isNPC ? 1 : 0)
                .arg(m->infos.raceID).arg(m->infos.sexID) : QString("no model"));
    return ok ? m : nullptr;
  };
  const auto storedState = [](WoWModel * m, const Case & c, int * applied, QStringList * wrong) {
    for (const Stored & s : c.stored)
    {
      if (!m->cd.hasOption(s.option))
        continue; // another model's option (an alternate form)
      (*applied)++;
      if (m->cd.get(s.option) != s.choice || !m->cd.isStoredChoice(s.option))
        *wrong << QString("%1=%2 (current %3, stored %4)").arg(s.option).arg(s.choice).arg(m->cd.get(s.option))
                    .arg(m->cd.isStoredChoice(s.option) ? 1 : 0);
    }
  };

  // ---- The first humanoid NPC ----
  if (WoWModel * m = loadNpc(plain, "first humanoid NPC"))
  {
    int applied = 0;
    QStringList wrong;
    storedState(m, plain, &applied, &wrong);
    check(QString("NPC appearance: %1 wears every stored choice of its model (%2), kept as stored").arg(plain.who).arg(applied),
          applied > 0 && wrong.isEmpty(), wrong.join(", "));
    QStringList bare;
    for (const uint option : m->cd.getCustomizationOptions())
    {
      const std::vector<uint> offered = m->cd.getCustomizationChoices(option);
      if (!m->cd.isStoredChoice(option) && std::find(offered.begin(), offered.end(), m->cd.get(option)) == offered.end())
        bare << QString("%1 (current %2)").arg(option).arg(m->cd.get(option));
    }
    check(QString("NPC appearance: %1's options it stores nothing for hold a valid choice").arg(plain.who),
          bare.isEmpty(), bare.join(", "));

    // A user's pick of a stored option's other valid choice takes it back to the player's rules; the rest stay stored.
    uint picked = 0, pick = 0;
    for (const Stored & s : plain.stored)
    {
      if (!m->cd.isStoredChoice(s.option))
        continue;
      for (const uint c : m->cd.getCustomizationChoices(s.option))
        if (c != s.choice)
        {
          picked = s.option;
          pick = c;
          break;
        }
      if (picked)
        break;
    }
    if (picked)
    {
      m->cd.set(picked, pick);
      settle();
      size_t stillStored = 0;
      for (const Stored & s : plain.stored)
        if (s.option != picked && m->cd.isStoredChoice(s.option))
          stillStored++;
      check(QString("NPC appearance: a pick (option %1 -> %2) takes the option back to the player's rules, the others stay stored")
              .arg(picked).arg(pick),
            m->cd.get(picked) == pick && !m->cd.isStoredChoice(picked) && stillStored > 0,
            QString("current %1, stored %2, other stored options %3").arg(m->cd.get(picked)).arg(m->cd.isStoredChoice(picked) ? 1 : 0)
              .arg(stillStored));
    }
  }

  // ---- NPC-only choices and options are worn, never offered ----
  if (!npcChoice.creature)
    skip("NPC appearance: an NPC-only choice of a player option", "no humanoid NPC of this client stores one");
  else if (WoWModel * m = loadNpc(npcChoice, "NPC-only choice"))
  {
    QStringList wrong;
    int n = 0;
    for (const Stored & s : npcChoice.stored)
      if (s.npcOnlyChoice && m->cd.hasOption(s.option))
      {
        n++;
        const std::vector<uint> offered = m->cd.getCustomizationChoices(s.option);
        if (m->cd.get(s.option) != s.choice || std::find(offered.begin(), offered.end(), s.choice) != offered.end())
          wrong << QString("%1=%2 (current %3)").arg(s.option).arg(s.choice).arg(m->cd.get(s.option));
      }
    check(QString("NPC appearance: %1 wears its %2 NPC-only choice(s), and the player's lists do not offer them").arg(npcChoice.who).arg(n),
          n > 0 && wrong.isEmpty(), wrong.join(", "));
  }
  if (!npcOption.creature)
    skip("NPC appearance: an option no player is offered", "no humanoid NPC of this client stores one");
  else if (WoWModel * m = loadNpc(npcOption, "option no player is offered"))
  {
    QStringList wrong;
    int n = 0;
    const std::vector<uint> offered = m->cd.getCustomizationOptions();
    for (const Stored & s : npcOption.stored)
      if (s.npcOnlyOption && m->cd.hasOption(s.option))
      {
        n++;
        if (m->cd.get(s.option) != s.choice || std::find(offered.begin(), offered.end(), s.option) != offered.end())
          wrong << QString("%1=%2 (current %3)").arg(s.option).arg(s.choice).arg(m->cd.get(s.option));
      }
    check(QString("NPC appearance: %1 wears its choice of %2 option(s) no player is offered, and the options are not offered")
            .arg(npcOption.who).arg(n),
          n > 0 && wrong.isEmpty(), wrong.join(", "));
  }

  // ---- A race that is not the first race on its file ----
  if (!sharedFile.creature)
    skip("NPC appearance: a race that is not the first race on its model file", "no humanoid NPC of this client is one");
  else if (WoWModel * m = loadNpc(sharedFile, "a race not first on its model file"))
  {
    int applied = 0;
    QStringList wrong;
    storedState(m, sharedFile, &applied, &wrong);
    check(QString("NPC appearance: %1 (race %2) wears its stored choices (%3)").arg(sharedFile.who).arg(sharedFile.race).arg(applied),
          applied > 0 && wrong.isEmpty(), wrong.join(", "));
  }

  // ---- A Demon Hunter NPC, and a player character on its model file afterwards ----
  if (!demonHunter.creature)
    skip("NPC appearance: a Demon Hunter NPC", clientHasDH ? "no humanoid NPC of this client is one" : "this client has no Demon Hunters");
  else if (WoWModel * m = loadNpc(demonHunter, "Demon Hunter"))
  {
    check(QString("NPC appearance: %1 (class 12) is in the Demon Hunter context").arg(demonHunter.who), m->cd.isDemonHunter(),
          QString("Demon Hunter %1").arg(m->cd.isDemonHunter() ? 1 : 0));
    // The user unticks Demon Hunter: the character is the player's now -- nothing stays stored, every choice is one
    // the player is offered.
    m->cd.setDemonHunterMode(false);
    settle();
    QStringList kept;
    for (const Stored & s : demonHunter.stored)
      if (m->cd.isStoredChoice(s.option))
        kept << QString("%1 stored").arg(s.option);
    for (const uint option : m->cd.getCustomizationOptions())
    {
      const std::vector<uint> offered = m->cd.getCustomizationChoices(option);
      if (std::find(offered.begin(), offered.end(), m->cd.get(option)) == offered.end())
        kept << QString("%1=%2 not offered").arg(option).arg(m->cd.get(option));
    }
    check(QString("NPC appearance: unticking Demon Hunter on %1 leaves no stored choice and only choices the player is offered")
            .arg(demonHunter.who), !m->cd.isDemonHunter() && kept.isEmpty(), kept.join(", "));
    // As ApplyArmoryCharacter loads a character of that race and sex: on the file on screen.
    frame->LoadModel(m->gamefile, demonHunter.race, demonHunter.sex);
    settle();
    WoWModel * p = canvasModel();
    bool anyStored = false;
    for (const Stored & s : demonHunter.stored)
      anyStored = anyStored || (p && p->cd.isStoredChoice(s.option));
    QStringList equipped;
    for (int slot = CS_HEAD; p && slot < NUM_CHAR_SLOTS; slot++)
      if (WoWItem * item = p->getItem((CharSlots)slot))
        if (item->displayId() > 0)
          equipped << QString::number(slot);
    check(QString("NPC appearance: a character loaded on %1's model file afterwards is not a Demon Hunter, wears no NPC equipment and no "
                  "stored choice").arg(demonHunter.who),
          p && !p->cd.isDemonHunter() && !p->cd.isNPC && !anyStored && equipped.isEmpty(),
          p ? QString("Demon Hunter %1, NPC %2, stored %3, NPC equipment in slots [%4]").arg(p->cd.isDemonHunter() ? 1 : 0)
                .arg(p->cd.isNPC ? 1 : 0).arg(anyStored ? 1 : 0).arg(equipped.join(','))
            : QString("no model"));
  }

  // ---- Saved and loaded back ----
  const Case & saved = npcChoice.creature ? npcChoice : plain;
  const QString npcFile = QDir::temp().filePath("wmv_customizationtest_npc.chr");
  if (WoWModel * m = loadNpc(saved, "saved"))
  {
    const std::map<uint, uint> before = [m]() {
      std::map<uint, uint> s;
      for (const uint o : m->cd.getCustomizationOptions())
        s[o] = m->cd.get(o);
      return s;
    }();
    std::vector<uint> storedBefore;
    for (const Stored & s : saved.stored)
      if (m->cd.isStoredChoice(s.option))
        storedBefore.push_back(s.option);
    frame->SaveChar(npcFile);
    frame->LoadChar(npcFile);
    settle();
    WoWModel * l = canvasModel();
    QStringList wrong;
    if (l)
    {
      for (const auto & o : before)
        if (l->cd.get(o.first) != o.second)
          wrong << QString("%1: %2 -> %3").arg(o.first).arg(o.second).arg(l->cd.get(o.first));
      for (const uint o : storedBefore)
        if (!l->cd.isStoredChoice(o))
          wrong << QString("%1 no longer stored").arg(o);
    }
    check(QString("NPC appearance: %1 saved and loaded back: race %2 sex %3, the same choices, %4 stored")
            .arg(saved.who).arg(saved.race).arg(saved.sex).arg(storedBefore.size()),
          l && l->infos.raceID == saved.race && l->infos.sexID == saved.sex && l->cd.isNPC && wrong.isEmpty(),
          l ? QString("race %1 sex %2 NPC %3 %4").arg(l->infos.raceID).arg(l->infos.sexID).arg(l->cd.isNPC ? 1 : 0).arg(wrong.join(", "))
            : QString("no model"));
  }
  QFile::remove(npcFile);

  // ---- A player character afterwards, saved and loaded back ----
  const int playerFile = RaceInfos::getFileIDForRaceSex(plain.race, plain.sex);
  GameFile * player = playerFile > 0 ? GAMEDIRECTORY.getFile(playerFile) : nullptr;
  if (player)
  {
    frame->LoadModel(player, plain.race, plain.sex);
    settle();
    WoWModel * m = canvasModel();
    bool anyStored = false;
    for (const Stored & s : plain.stored)
      anyStored = anyStored || (m && m->cd.isStoredChoice(s.option));
    check(QString("NPC appearance: a player character (race %1 sex %2) loaded afterwards carries nothing of the NPC").arg(plain.race).arg(plain.sex),
          m && !m->cd.isNPC && !anyStored, m ? QString("NPC %1, stored %2").arg(m->cd.isNPC ? 1 : 0).arg(anyStored ? 1 : 0) : QString("no model"));
    const QString playerChr = QDir::temp().filePath("wmv_customizationtest_player.chr");
    std::map<uint, uint> before;
    if (m)
      for (const uint o : m->cd.getCustomizationOptions())
        before[o] = m->cd.get(o);
    frame->SaveChar(playerChr);
    QFile f(playerChr);
    const QString text = f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    f.close();
    frame->LoadChar(playerChr);
    settle();
    WoWModel * l = canvasModel();
    QStringList wrong;
    for (const auto & o : before)
      if (!l || l->cd.get(o.first) != o.second)
        wrong << QString("%1: %2 -> %3").arg(o.first).arg(o.second).arg(l ? l->cd.get(o.first) : 0);
    check(QString("NPC appearance: a saved player character names its race, carries no NPC mark, and loads back unchanged"),
          !text.contains("npc=") && !text.contains("stored=") && text.contains(QString("race=\"%1\"").arg(plain.race)) &&
            l && l->infos.raceID == plain.race && !l->cd.isNPC && wrong.isEmpty(),
          l ? QString("race %1 NPC %2 %3").arg(l->infos.raceID).arg(l->cd.isNPC ? 1 : 0).arg(wrong.join(", ")) : QString("no model"));
    QFile::remove(playerChr);
  }
}

// -customizationtest, every client: a geoset group an NPC's shirt, chest and legs all set goes to the item that declares
// it (its GeosetGroup is not 0), and to the outer one where two do -- whatever order the viewer holds the items in. The
// cases are found in the loaded client's own tables, in Creature order (then creature display order, for a case no
// Creature row gives), among NPCs whose race wears their display's model and whose gloves leave the sleeves alone --
// no NPC is named:
//   - legs declaring the robe skirt (the trousers group) under a shirt or chest that leaves it at its default: the legs'
//     skirt, and no underwear bottoms under the legs;
//   - a chest declaring the skirt over legs that leave it at their default, and no gloves: the chest's skirt, the NPC
//     robed, no underwear top under the chest, and the bare hands still drawn;
//   - a shirt declaring the sleeves under a chest that leaves them at their default: the shirt's sleeves;
//   - a shirt and a chest declaring different sleeves: the chest's.
template <class Check, class Skip>
static void checkEquipmentGeosets(ModelViewer * frame, const Check & check, const Skip & skip)
{
  for (const QString table : { QString("CreatureDisplayInfoExtra"), QString("NpcModelItemSlotDisplayInfo"), QString("ItemDisplayInfo") })
  {
    sqlResult n = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1").arg(table));
    if (n.valid && !n.values.empty() && n.values[0][0].toInt() > 0)
      continue;
    wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
    if (folder && folder->isRemoteFile(QString("dbfilesclient/%1.db2").arg(table.toLower())))
      skip("equipment geosets", QString("the client's %1 is not installed on this computer").arg(table));
    else
      check(QString("equipment geosets: %1 was read").arg(table), false, "the table holds no row");
    return;
  }

  // NpcModelItemSlotDisplayInfo.ItemSlot 2 shirt, 3 chest, 5 legs, 8 hands; a slot named twice keeps its last row, as
  // ModelViewer::ShowCreatureDisplay puts them on. GeoSetGroup1 is the sleeves (gloves: the gloves group, which hides
  // the sleeves when declared), GeoSetGroup3 the skirt.
  struct Worn { bool worn = false; int sleeves = 0; int skirt = 0; };
  std::map<uint, std::map<int, Worn> > equipment;
  sqlResult rows = GAMEDATABASE.sqlQuery("SELECT n.NpcModelID, n.ItemSlot, i.ID, IFNULL(i.GeoSetGroup1, 0), IFNULL(i.GeoSetGroup3, 0) "
                                         "FROM NpcModelItemSlotDisplayInfo n LEFT JOIN ItemDisplayInfo i ON i.ID = n.ItemDisplayInfoID "
                                         "WHERE n.ItemSlot IN (2, 3, 5, 8) ORDER BY n.ID");
  for (size_t i = 0; rows.valid && i < rows.values.size(); i++)
    equipment[rows.values[i][0].toUInt()][rows.values[i][1].toInt()] =
      Worn{ !rows.values[i][2].isEmpty() && rows.values[i][2].toInt() != 0, rows.values[i][3].toInt(), rows.values[i][4].toInt() };
  if (equipment.empty())
  {
    skip("equipment geosets", "the client's NPCs wear no shirt, chest, legs or gloves");
    return;
  }

  struct Case { uint creature = 0; bool byDisplay = false; QString who; uint extra = 0; int race = -1; int sex = -1; Worn shirt, chest, legs; };
  Case legsSkirt, chestSkirt, shirtSleeves, bothSleeves;
  const auto findCases = [&](const sqlResult & npcs, bool byDisplay) {
    for (size_t i = 0; npcs.valid && i < npcs.values.size(); i++)
    {
      if (legsSkirt.creature && chestSkirt.creature && shirtSleeves.creature && bothSleeves.creature)
        break;
      const auto e = equipment.find(npcs.values[i][2].toUInt());
      if (e == equipment.end())
        continue;
      Case c;
      c.creature = npcs.values[i][0].toUInt();
      c.byDisplay = byDisplay;
      c.who = QString(byDisplay ? "creature display %1" : "NPC %1").arg(c.creature);
      c.extra = e->first;
      c.race = npcs.values[i][3].toInt();
      c.sex = npcs.values[i][4].toInt();
      c.shirt = e->second[2];
      c.chest = e->second[3];
      c.legs = e->second[5];
      const Worn gloves = e->second[8];
      if (gloves.worn && gloves.sleeves > 0)
        continue; // the gloves hide the sleeves
      const int file = RaceInfos::getHDModelForFileID(npcs.values[i][1].toInt());
      RaceInfos wearer;
      if (!GAMEDIRECTORY.getFile(file) || !RaceInfos::getRaceInfosForRaceSex(c.race, c.sex, wearer) || wearer.modelFileID != file)
        continue; // the display's race does not wear its model, or the model is not installed
      const bool shirtDefaultSkirt = !c.shirt.worn || c.shirt.skirt == 0, chestDefaultSkirt = !c.chest.worn || c.chest.skirt == 0;
      if (!legsSkirt.creature && c.legs.worn && c.legs.skirt != 0 && (c.shirt.worn || c.chest.worn) && shirtDefaultSkirt && chestDefaultSkirt)
        legsSkirt = c;
      if (!chestSkirt.creature && c.chest.worn && c.chest.skirt != 0 && c.legs.worn && c.legs.skirt == 0 && !gloves.worn)
        chestSkirt = c;
      if (!shirtSleeves.creature && c.shirt.worn && c.shirt.sleeves != 0 && c.chest.worn && c.chest.sleeves == 0)
        shirtSleeves = c;
      if (!bothSleeves.creature && c.shirt.worn && c.chest.worn && c.shirt.sleeves != 0 && c.chest.sleeves != 0 && c.shirt.sleeves != c.chest.sleeves)
        bothSleeves = c;
    }
  };
  const QString columns = "CreatureModelData.FileDataID, CreatureDisplayInfoExtra.ID, CreatureDisplayInfoExtra.DisplayRaceID, "
                          "CreatureDisplayInfoExtra.DisplaySexID ";
  const QString joins = "JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
                        "JOIN CreatureDisplayInfoExtra ON CreatureDisplayInfo.ExtendedDisplayInfoID = CreatureDisplayInfoExtra.ID ";
  findCases(GAMEDATABASE.sqlQuery("SELECT Creature.ID, " + columns + "FROM Creature "
                                  "JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID " + joins + "ORDER BY Creature.ID"),
            false);
  if (!legsSkirt.creature || !chestSkirt.creature || !shirtSleeves.creature || !bothSleeves.creature)
    findCases(GAMEDATABASE.sqlQuery("SELECT CreatureDisplayInfo.ID, " + columns + "FROM CreatureDisplayInfo " + joins +
                                    "ORDER BY CreatureDisplayInfo.ID"), true);

  // Loads the NPC; the model when it loaded as that race's character NPC, else null (and a failed check).
  const auto loadNpc = [&](const Case & c, const QString & what) -> WoWModel * {
    if (c.byDisplay)
    {
      ModelIdLookup::Resolved resolved;
      wxString why;
      if (ModelIdLookup::resolve(ModelIdLookup::Kind::CreatureDisplay, (int)c.creature, resolved, why))
        frame->LoadModelById(resolved, why);
    }
    else
      frame->LoadNPC(c.creature);
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
    WoWModel * m = frame->canvas ? const_cast<WoWModel *>(frame->canvas->model()) : nullptr;
    const bool ok = m && m->charModelDetails.isChar && m->cd.isNPC && m->infos.raceID == c.race && m->infos.sexID == c.sex;
    check(QString("equipment geosets, %1: %2 (extended display %3) loads as a character NPC of race %4 sex %5")
            .arg(what).arg(c.who).arg(c.extra).arg(c.race).arg(c.sex), ok,
          m ? QString("isChar %1, NPC %2, race %3 sex %4").arg(m->charModelDetails.isChar ? 1 : 0).arg(m->cd.isNPC ? 1 : 0)
                .arg(m->infos.raceID).arg(m->infos.sexID) : QString("no model"));
    return ok ? m : nullptr;
  };
  const auto group = [](WoWModel * m, CharGeosets g) {
    const auto it = m->cd.geosets.find(g);
    return it == m->cd.geosets.end() ? -1 : (int)it->second;
  };
  const auto drawn = [](WoWModel * m, CharGeosets g) {
    for (size_t i = 0; i < m->ownGeosetCount() && i < m->geosets.size(); i++)
      if (m->geosets[i] && m->geosets[i]->display && m->geosets[i]->id / 100 == (int)g)
        return true;
    return false;
  };
  const auto underwear = [](WoWModel * m, int region) {
    return std::any_of(m->cd.textures.begin(), m->cd.textures.end(), [region](const CharDetails::TextureCustomization & t) { return t.region == region; });
  };

  if (!legsSkirt.creature)
    skip("equipment geosets: legs declaring the robe skirt under a shirt or chest that does not", "no NPC of this client wears one");
  else if (WoWModel * m = loadNpc(legsSkirt, "legs declaring the skirt"))
    check(QString("equipment geosets: %1's legs declare the skirt (%2), its shirt and chest do not: the legs' skirt, no underwear bottoms")
            .arg(legsSkirt.who).arg(1 + legsSkirt.legs.skirt),
          group(m, CG_TROUSERS) == 1 + legsSkirt.legs.skirt && !underwear(m, CR_LEG_UPPER),
          QString("trousers group %1, underwear bottoms %2").arg(group(m, CG_TROUSERS)).arg(underwear(m, CR_LEG_UPPER) ? 1 : 0));

  if (!chestSkirt.creature)
    skip("equipment geosets: a chest declaring the robe skirt over legs that do not, no gloves", "no NPC of this client wears one");
  else if (WoWModel * m = loadNpc(chestSkirt, "chest declaring the skirt"))
    check(QString("equipment geosets: %1's chest declares the skirt (%2), its legs do not, no gloves: the chest's skirt, robed, no "
                  "underwear top, bare hands drawn").arg(chestSkirt.who).arg(1 + chestSkirt.chest.skirt),
          group(m, CG_TROUSERS) == 1 + chestSkirt.chest.skirt && m->isWearingARobe() && !underwear(m, CR_TORSO_UPPER) && drawn(m, CG_GLOVES),
          QString("trousers group %1, robe %2, underwear top %3, hands drawn %4").arg(group(m, CG_TROUSERS)).arg(m->isWearingARobe() ? 1 : 0)
            .arg(underwear(m, CR_TORSO_UPPER) ? 1 : 0).arg(drawn(m, CG_GLOVES) ? 1 : 0));

  if (!shirtSleeves.creature)
    skip("equipment geosets: a shirt declaring the sleeves under a chest that does not", "no NPC of this client wears one");
  else if (WoWModel * m = loadNpc(shirtSleeves, "shirt declaring the sleeves"))
    check(QString("equipment geosets: %1's shirt declares the sleeves (%2), its chest does not: the shirt's sleeves")
            .arg(shirtSleeves.who).arg(1 + shirtSleeves.shirt.sleeves),
          group(m, CG_SLEEVES) == 1 + shirtSleeves.shirt.sleeves, QString("sleeves group %1").arg(group(m, CG_SLEEVES)));

  if (!bothSleeves.creature)
    skip("equipment geosets: a shirt and a chest declaring different sleeves", "no NPC of this client wears them");
  else if (WoWModel * m = loadNpc(bothSleeves, "shirt and chest declaring the sleeves"))
    check(QString("equipment geosets: %1's shirt (%2) and chest (%3) declare the sleeves: the chest's")
            .arg(bothSleeves.who).arg(1 + bothSleeves.shirt.sleeves).arg(1 + bothSleeves.chest.sleeves),
          group(m, CG_SLEEVES) == 1 + bothSleeves.chest.sleeves, QString("sleeves group %1").arg(group(m, CG_SLEEVES)));
}

// -customizationtest, every client: a character's model of the other generation (RaceInfos::VariantPair), where the
// client ships one (Classic Beta). Independent expectations per client, read from its DB2 files by a reader apart from
// this loader: how many pairs, option pairs and choice pairs, and one pair's models; a client without the tables
// offers no switch. Then, for EVERY pair the client has (no ID named): the primary character with its appearance set
// away from the defaults, dressed (the display items of the extended display that wears the most slots), sheathed and
// playing an animation other than Stand, switched to its other model and back:
//   - the other model is the pair's: its file, ChrModel and texture layout, the generation the client's data gives it
//     (Classic Beta: every primary High Definition, every alternate Classic), the same race and sex;
//   - every choice carries through the client's option and choice tables, as read here by SQL; the equipment (a weapon
//     in hand too), sheathe, the animation (by its animation ID), the viewport background and the panes are kept;
//   - back on the primary, its whole appearance is exactly as it was; on the other model again, exactly as it was left
//     there; an edit on one model does not reach the other.
// And: an NPC on a pair's model gets no switch, and one whose display names an alternate model but stores the
// primary's choices is shown on the primary, by View NPC and by Load NPC / Model by ID alike; a character loaded afresh
// after a switch -- by a full load, or by Load Character on the file on screen from a file without race and sex -- has
// no memory of it; a character saved on its other model loads back on it; and one saved on the alternate file with
// the primary's choices (as builds before the pairs saved them) loads with them translated.
template <class Check, class Skip>
static void checkModelVariants(ModelViewer * frame, const Check & check, const Skip & skip)
{
  const auto settle = []() {
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
  };
  const QString product = GAMEDIRECTORY.clientProfile().product;
  struct VariantExpect
  {
    const char * product;
    size_t pairs, optionPairs, choicePairs;
    int primary, alternate, alternateFile, alternateLayout;
    CharacterModelVariant primaryGeneration, alternateGeneration;   // of every pair
  };
  static const VariantExpect expectations[] = {
    { "wow", 0, 0, 0, 0, 0, 0, 0, CharacterModelVariant::Unknown, CharacterModelVariant::Unknown },
    { "wow_classic", 0, 0, 0, 0, 0, 0, 0, CharacterModelVariant::Unknown, CharacterModelVariant::Unknown },
    { "wow_classic_era", 0, 0, 0, 0, 0, 0, 0, CharacterModelVariant::Unknown, CharacterModelVariant::Unknown },
    { "wow_classic_beta", 18, 93, 1136, 1, 257, 119940, 203, CharacterModelVariant::HD, CharacterModelVariant::Classic },
  };
  const VariantExpect * expect = nullptr;
  for (const auto & e : expectations)
    if (product == e.product)
      expect = &e;

  std::vector<RaceInfos::VariantPair> pairs;
  size_t optionPairs = 0, choicePairs = 0;
  sqlResult rows = GAMEDATABASE.sqlQuery("SELECT PrimaryChrModelID FROM ChrModelAltVariant ORDER BY ID");
  for (size_t i = 0; rows.valid && i < rows.values.size(); i++)
  {
    RaceInfos::VariantPair pair;
    if (RaceInfos::getVariantPair(rows.values[i][0].toInt(), pair))
    {
      pairs.push_back(pair);
      optionPairs += pair.optionToAlternate.size();
      choicePairs += pair.choiceToAlternate.size();
    }
  }
  if (!expect)
    skip("model variants: the client's pairs", QString("no expectation for %1").arg(product));
  else
  {
    check(QString("model variants: %1 has %2 pairs, %3 option pairs and %4 choice pairs").arg(product).arg(expect->pairs)
            .arg(expect->optionPairs).arg(expect->choicePairs),
          RaceInfos::variantPairCount() == expect->pairs && optionPairs == expect->optionPairs && choicePairs == expect->choicePairs,
          QString("%1 pairs, %2 option pairs, %3 choice pairs").arg(RaceInfos::variantPairCount()).arg(optionPairs).arg(choicePairs));
    if (expect->pairs > 0)
    {
      RaceInfos::VariantPair pair;
      RaceInfos alternate;
      const bool ok = RaceInfos::getVariantPair(expect->primary, pair) && pair.alternateChrModelID == expect->alternate &&
                      pair.alternateFileID == expect->alternateFile &&
                      RaceInfos::getRaceInfosForAlternateFileID(expect->alternateFile, alternate) &&
                      alternate.textureLayoutID == expect->alternateLayout && alternate.ChrModelID.size() == 1 &&
                      alternate.ChrModelID[0] == expect->alternate && alternate.raceID == pair.raceID && alternate.sexID == pair.sexID;
      check(QString("model variants: ChrModel %1's other model is ChrModel %2, FileDataID %3, texture layout %4")
              .arg(expect->primary).arg(expect->alternate).arg(expect->alternateFile).arg(expect->alternateLayout),
            ok, QString("alternate %1 file %2 layout %3").arg(pair.alternateChrModelID).arg(pair.alternateFileID).arg(alternate.textureLayoutID));
    }
  }

  if (pairs.empty())
  {
    // No switch is offered: a character of the first race on the menu has no Model selector.
    const auto menu = RaceInfos::getRaceMenu();
    for (const auto & e : menu)
    {
      GameFile * file = e.maleFileID > 0 ? GAMEDIRECTORY.getFile(e.maleFileID) : nullptr;
      if (!file)
        continue;
      frame->LoadModel(file, e.raceID, 0);
      settle();
      const ModelViewer::CharacterVariantState state = frame->characterVariantState();
      check(QString("model variants: none in this client, and a character (race %1) gets no Model selector").arg(e.raceID),
            !state.shown, QString("shown %1").arg(state.shown ? 1 : 0));
      break;
    }
    return;
  }

  // The display items of the extended display that wears the most equipment slots, slots as ShowCreatureDisplay
  // maps them; put on every pair's character by display.
  static const std::map<int, CharSlots> npcItemSlots = { { 0, CS_HEAD }, { 1, CS_SHOULDER }, { 2, CS_SHIRT }, { 3, CS_CHEST },
    { 4, CS_BELT }, { 5, CS_PANTS }, { 6, CS_BOOTS }, { 7, CS_BRACERS }, { 8, CS_GLOVES }, { 9, CS_TABARD }, { 10, CS_CAPE } };
  std::vector<std::pair<CharSlots, int> > equipment;
  sqlResult dressed = GAMEDATABASE.sqlQuery("SELECT NpcModelID FROM NpcModelItemSlotDisplayInfo WHERE ItemSlot <= 10 GROUP BY NpcModelID "
                                            "ORDER BY COUNT(DISTINCT ItemSlot) DESC, NpcModelID LIMIT 1");
  if (dressed.valid && !dressed.values.empty())
  {
    sqlResult items = GAMEDATABASE.sqlQuery(QString("SELECT ItemDisplayInfoID, ItemSlot FROM NpcModelItemSlotDisplayInfo WHERE NpcModelID = %1 "
                                                    "ORDER BY ID").arg(dressed.values[0][0]));
    for (size_t i = 0; items.valid && i < items.values.size(); i++)
    {
      const auto slot = npcItemSlots.find(items.values[i][1].toInt());
      if (slot != npcItemSlots.end())
        equipment.emplace_back(slot->second, items.values[i][0].toInt());
    }
  }
  // A one-handed sword in the right hand, by display (Classic Beta reads no Item rows: what a character wears there is
  // put on by display).
  int weaponDisplay = 0;
  sqlResult weapons = GAMEDATABASE.sqlQuery("SELECT ItemDisplayInfo.ID, ModelFileData.FileDataID FROM ItemDisplayInfo JOIN ModelFileData "
                                            "ON ItemDisplayInfo.ModelResourcesID1 = ModelFileData.ModelResourcesID ORDER BY ItemDisplayInfo.ID");
  for (size_t i = 0; weapons.valid && i < weapons.values.size() && weaponDisplay == 0; i++)
  {
    GameFile * f = GAMEDIRECTORY.getFile(weapons.values[i][1].toInt());
    wxString why;
    if (f && f->fullname().toLower().contains("sword_1h") && ModelIdLookup::checkModelFile(weapons.values[i][1].toInt(), why))
      weaponDisplay = weapons.values[i][0].toInt();
  }
  if (weaponDisplay > 0)
    equipment.emplace_back(CS_HAND_RIGHT, weaponDisplay);
  else
    skip("model variants: a weapon in hand", "no one-handed sword display whose model is on this computer");
  const auto weaponShown = [](WoWModel * m) {
    WoWItem * item = m ? m->getItem(CS_HAND_RIGHT) : nullptr;
    return !item || item->displayId() <= 0 || !item->models().empty();
  };
  const auto itemsOf = [](WoWModel * m) {
    QStringList list;
    for (int slot = 0; m && slot < NUM_CHAR_SLOTS; slot++)
      if (WoWItem * item = m->getItem((CharSlots)slot))
        if (item->id() > 0 || item->displayId() > 0)
          list << QString("%1:%2/%3").arg(slot).arg(item->id()).arg(item->displayId());
    return list.join(' ');
  };
  const auto playing = [](WoWModel * m) {
    if (!m || !m->animManager || m->anims.empty() || m->animManager->GetAnim() >= m->anims.size())
      return -1;
    return (int)m->anims[m->animManager->GetAnim()].animID;
  };
  const auto generationName = [](CharacterModelVariant v) { return v == CharacterModelVariant::HD ? QString("High Definition") : QString("Classic"); };

  const wxColour background = frame->viewportBackground();
  int savedLoaded = 0;
  for (const RaceInfos::VariantPair & pair : pairs)
  {
    const QString who = QString("model variants, pair %1 (ChrModel %2 -> %3, race %4 sex %5)").arg(pair.id)
                          .arg(pair.primaryChrModelID).arg(pair.alternateChrModelID).arg(pair.raceID).arg(pair.sexID);
    GameFile * primaryFile = GAMEDIRECTORY.getFile(pair.primaryFileID);
    if (!primaryFile)
    {
      check(who + ": its primary model is in the client", false, QString("FileDataID %1").arg(pair.primaryFileID));
      continue;
    }
    frame->LoadModel(primaryFile, pair.raceID, pair.sexID);
    settle();
    WoWModel * m = frame->riderModel();
    if (!m || m->infos.ChrModelID.empty() || m->infos.ChrModelID[0] != pair.primaryChrModelID)
    {
      check(who + ": loads on its primary ChrModel", false, m ? QString("ChrModel %1").arg(m->infos.ChrModelID.empty() ? 0 : m->infos.ChrModelID[0]) : QString("no model"));
      continue;
    }
    // Away from the defaults: every option its second valid choice, where it has one.
    for (const uint option : m->cd.getCustomizationOptions())
    {
      const std::vector<uint> offered = m->cd.getCustomizationChoices(option);
      if (offered.size() > 1)
        m->cd.set(option, offered[1]);
    }
    for (const auto & e : equipment)
      if (WoWItem * item = m->getItem(e.first))
        item->setDisplayId(e.second);
    g_charControl->RefreshModel();
    g_charControl->RefreshEquipment();
    frame->charMenu->Check(ID_SHEATHE, true);
    wxCommandEvent toggle(wxEVT_MENU, ID_SHEATHE);
    toggle.SetInt(1);
    frame->OnCharToggle(toggle);
    int animID = -1;
    for (const auto & a : m->anims)
      if (a.animID != ANIM_STAND)
      {
        animID = a.animID;
        break;
      }
    if (animID >= 0)
      frame->animControl->RestoreAnimation(animID, 0, 0, 1.0f, false);
    settle();
    const CharDetails::Appearance before = m->cd.captureAppearance();
    const QString itemsBefore = itemsOf(m);
    const wxString panes = frame->interfaceManager.SavePerspective();
    const CharacterModelVariant primaryGeneration = m->modelGeneration();
    const CharacterModelVariant other = primaryGeneration == CharacterModelVariant::HD ? CharacterModelVariant::Classic : CharacterModelVariant::HD;
    if (expect && expect->primaryGeneration != CharacterModelVariant::Unknown)
      check(who + QString(": its primary model is %1").arg(generationName(expect->primaryGeneration)),
            primaryGeneration == expect->primaryGeneration, generationName(primaryGeneration));
    const RaceInfos::Translation translation = RaceInfos::translateSelection(pair, before.selection, true);
    // What the switch has to carry, read from the client's tables here: each choice of the character's through its
    // option row and choice row, or no counterpart (no option row) or not covered (no choice row under it).
    std::map<uint, uint> expected;
    size_t expectedNoCounterpart = 0, expectedNotCovered = 0;
    for (const auto & oc : before.selection)
    {
      sqlResult rowsOf = GAMEDATABASE.sqlQuery(QString("SELECT o.AlternateOptionID, c.AlternateChoiceID FROM ChrModelAltVariantOption o "
                                                       "LEFT JOIN ChrModelAltVariantChoice c ON c.ChrModelAltVariantOptionID = o.ID AND "
                                                       "c.PrimaryChoiceID = %1 WHERE o.ChrModelAltVariantID = %2 AND o.PrimaryOptionID = %3")
                                                 .arg(oc.second).arg(pair.id).arg(oc.first));
      if (!rowsOf.valid || rowsOf.values.empty())
        expectedNoCounterpart++;
      else if (rowsOf.values[0][1].isEmpty() || rowsOf.values[0][1].toUInt() == 0)
        expectedNotCovered++;
      else
        expected[rowsOf.values[0][0].toUInt()] = rowsOf.values[0][1].toUInt();
    }

    wxString why;
    const bool switched = frame->SwitchCharacterVariant(other, why);
    settle();
    WoWModel * n = frame->riderModel();
    RaceInfos alternate;
    RaceInfos::getRaceInfosForAlternateFileID(pair.alternateFileID, alternate);
    const bool identity = switched && n && n->gamefile && (int)n->gamefile->fileDataId() == pair.alternateFileID &&
                          !n->infos.ChrModelID.empty() && n->infos.ChrModelID[0] == pair.alternateChrModelID &&
                          n->infos.textureLayoutID == alternate.textureLayoutID && n->modelGeneration() == other &&
                          n->infos.raceID == pair.raceID && n->infos.sexID == pair.sexID;
    check(who + QString(": switched from %1 to %2 -- the pair's file, ChrModel and texture layout, same race and sex")
                  .arg(generationName(primaryGeneration)).arg(generationName(other)), identity,
          n ? QString("ok %1 why %2 | file %3 ChrModel %4 layout %5 generation %6 race %7 sex %8").arg(switched).arg(QString::fromWCharArray(why.wc_str()))
                .arg(n->gamefile ? n->gamefile->fileDataId() : 0).arg(n->infos.ChrModelID.empty() ? 0 : n->infos.ChrModelID[0])
                .arg(n->infos.textureLayoutID).arg(generationName(n->modelGeneration())).arg(n->infos.raceID).arg(n->infos.sexID)
            : QString("no model"));
    if (!identity)
      continue;
    if (expect && expect->alternateGeneration != CharacterModelVariant::Unknown)
      check(who + QString(": its other model is %1").arg(generationName(expect->alternateGeneration)),
            n->modelGeneration() == expect->alternateGeneration, generationName(n->modelGeneration()));
    QStringList wrong;
    for (const auto & oc : expected)
      if (n->cd.get(oc.first) != oc.second)
        wrong << QString("%1: %2, holds %3").arg(oc.first).arg(oc.second).arg(n->cd.get(oc.first));
    check(who + QString(": %1 of %2 choice(s) carried as the client's tables pair them (%3 option(s) without a counterpart, "
                        "%4 choice(s) not paired)").arg(expected.size()).arg(before.selection.size()).arg(expectedNoCounterpart).arg(expectedNotCovered),
          !expected.empty() && wrong.isEmpty() && translation.selection == expected &&
            translation.noCounterpart.size() == expectedNoCounterpart && translation.notCovered.size() == expectedNotCovered &&
            expected.size() + expectedNoCounterpart + expectedNotCovered == before.selection.size(),
          QString("wrong: %1 | the switch's translation %2 carried, %3 without, %4 not paired")
            .arg(wrong.join(", ")).arg(translation.selection.size()).arg(translation.noCounterpart.size()).arg(translation.notCovered.size()));
    const int animAfter = playing(n);
    bool animationHere = false;
    for (const auto & a : n->anims)
      animationHere = animationHere || (int)a.animID == animID;
    check(who + ": equipment (the weapon's model in hand too), sheathe, animation, viewport background and panes kept",
          itemsOf(n) == itemsBefore && weaponShown(n) && n->bSheathe && (animID < 0 || animAfter == (animationHere ? animID : (int)ANIM_STAND)) &&
            frame->viewportBackground() == background && frame->interfaceManager.SavePerspective() == panes,
          QString("items %1 (before %2) | weapon model %3 | sheathe %4 | animation %5 (before %6) | background %7 | panes %8")
            .arg(itemsOf(n)).arg(itemsBefore).arg(weaponShown(n) ? "shown" : "missing").arg(n->bSheathe ? 1 : 0).arg(animAfter).arg(animID)
            .arg(frame->viewportBackground() == background ? "kept" : "changed").arg(frame->interfaceManager.SavePerspective() == panes ? "kept" : "changed"));
    const std::map<uint, uint> firstVisit = n->cd.captureAppearance().selection;

    frame->SwitchCharacterVariant(primaryGeneration, why);
    settle();
    WoWModel * back = frame->riderModel();
    check(who + ": back on the primary, its whole appearance exactly as it was",
          back && !back->infos.ChrModelID.empty() && back->infos.ChrModelID[0] == pair.primaryChrModelID &&
            back->cd.captureAppearance().selection == before.selection,
          back ? QString("ChrModel %1, %2 of %3 option(s)").arg(back->infos.ChrModelID.empty() ? 0 : back->infos.ChrModelID[0])
                   .arg(back->cd.captureAppearance().selection.size()).arg(before.selection.size()) : QString("no model"));

    frame->SwitchCharacterVariant(other, why);
    settle();
    WoWModel * again = frame->riderModel();
    const bool sameAsLeft = again && again->cd.captureAppearance().selection == firstVisit;
    // An edit on the other model stays there.
    std::map<uint, uint> edited;
    if (again)
      for (const uint option : again->cd.getCustomizationOptions())
      {
        const std::vector<uint> offered = again->cd.getCustomizationChoices(option);
        if (offered.size() > 2)
        {
          again->cd.set(option, offered.back() != again->cd.get(option) ? offered.back() : offered.front());
          break;
        }
      }
    if (again)
      edited = again->cd.captureAppearance().selection;
    frame->SwitchCharacterVariant(primaryGeneration, why);
    settle();
    WoWModel * primaryAgain = frame->riderModel();
    const bool primaryUntouched = primaryAgain && primaryAgain->cd.captureAppearance().selection == before.selection;
    frame->SwitchCharacterVariant(other, why);
    settle();
    WoWModel * otherAgain = frame->riderModel();
    check(who + ": the other model comes back as it was left, an edit there stays there and does not reach the primary",
          sameAsLeft && primaryUntouched && otherAgain && otherAgain->cd.captureAppearance().selection == edited && edited != firstVisit,
          QString("as left %1 | primary untouched %2 | edit kept %3 | edit made %4").arg(sameAsLeft).arg(primaryUntouched)
            .arg(otherAgain && otherAgain->cd.captureAppearance().selection == edited).arg(edited != firstVisit));

    // The first pair only (a .chr round trip per pair adds nothing):
    if (savedLoaded++ == 0 && otherAgain)
    {
      const auto remembered = [frame]() { return frame->m_variantSession.byChrModel.size(); };
      const auto loadedAs = [](WoWModel * l) {
        return l ? QString("file %1 ChrModel %2").arg(l->gamefile ? l->gamefile->fileDataId() : 0).arg(l->infos.ChrModelID.empty() ? 0 : l->infos.ChrModelID[0])
                 : QString("no model");
      };
      const auto rewrite = [](const QString & path, const std::function<QString(QString)> & change) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
          return false;
        const QString text = change(QString::fromUtf8(f.readAll()));
        f.close();
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
          return false;
        f.write(text.toUtf8());
        return true;
      };
      const QString chr = QDir::temp().filePath("wmv_customizationtest_variant.chr");

      // Saved on the other model, it loads back on it -- a full load, from the primary on screen -- with the same
      // choices, and nothing of the switches before it is remembered.
      const size_t heldBefore = remembered();
      const std::map<uint, uint> saved = otherAgain->cd.captureAppearance().selection;
      frame->SaveChar(chr);
      frame->SwitchCharacterVariant(primaryGeneration, why);
      settle();
      frame->LoadChar(chr);
      settle();
      WoWModel * l = frame->riderModel();
      check(who + ": saved on its other model, it loads back on it with the same choices, remembering nothing before it",
            heldBefore == 2 && l && l->gamefile && (int)l->gamefile->fileDataId() == pair.alternateFileID && !l->infos.ChrModelID.empty() &&
              l->infos.ChrModelID[0] == pair.alternateChrModelID && l->cd.captureAppearance().selection == saved && remembered() == 0,
            QString("%1 | %2 model(s) remembered before, %3 after").arg(loadedAs(l)).arg(heldBefore).arg(remembered()));

      // Load Character on the file on screen from a file that names no race and sex (saved before files named them):
      // the same-file load starts the character afresh too. (One switch after a fresh load remembers the model it left.)
      frame->SwitchCharacterVariant(primaryGeneration, why);
      settle();
      const size_t heldSameFile = remembered();
      frame->SaveChar(chr);
      const bool stripped = rewrite(chr, [](QString text) {
        return text.remove(QRegularExpression(QStringLiteral(" (race|sex)=\"[0-9-]*\"")));
      });
      frame->LoadChar(chr);
      settle();
      l = frame->riderModel();
      check(who + ": Load Character of a file without race and sex, on the file on screen, remembers nothing of the switches before it",
            stripped && heldSameFile == 1 && l && l->gamefile && (int)l->gamefile->fileDataId() == pair.primaryFileID && remembered() == 0 &&
              frame->characterVariantState().note.IsEmpty(),
            QString("%1 | %2 model(s) remembered before, %3 after").arg(loadedAs(l)).arg(heldSameFile).arg(remembered()));

      // Saved on the alternate file with the primary's choices -- as builds before the pairs saved a classic model --,
      // it loads on the alternate with the choices translated through the pair's tables.
      const std::map<uint, uint> primarySelection = l ? l->cd.captureAppearance().selection : std::map<uint, uint>();
      frame->SaveChar(chr);
      GameFile * alternateFile = GAMEDIRECTORY.getFile(pair.alternateFileID);
      const bool renamed = alternateFile && rewrite(chr, [primaryFile, alternateFile](QString text) {
        return text.replace(primaryFile->fullname(), alternateFile->fullname(), Qt::CaseInsensitive);
      });
      frame->LoadChar(chr);
      settle();
      l = frame->riderModel();
      const RaceInfos::Translation old = RaceInfos::translateSelection(pair, primarySelection, true);
      QStringList oldWrong;
      for (const auto & oc : old.selection)
        if (!l || l->cd.get(oc.first) != oc.second)
          oldWrong << QString("%1: %2, holds %3").arg(oc.first).arg(oc.second).arg(l ? l->cd.get(oc.first) : 0);
      check(who + QString(": saved on its alternate file with the primary's choices, it loads with %1 translated").arg(old.selection.size()),
            renamed && l && l->gamefile && (int)l->gamefile->fileDataId() == pair.alternateFileID && !old.selection.empty() && oldWrong.isEmpty(),
            QString("%1 | %2").arg(loadedAs(l)).arg(oldWrong.join(", ")));
      QFile::remove(chr);

      // A full load after switches: nothing remembered.
      frame->LoadModel(primaryFile, pair.raceID, pair.sexID);
      settle();
      frame->SwitchCharacterVariant(other, why);
      settle();
      const size_t heldFull = remembered();
      frame->LoadModel(primaryFile, pair.raceID, pair.sexID);
      settle();
      check(who + ": a character loaded afresh has no memory of the switches before it",
            heldFull == 1 && remembered() == 0 && frame->characterVariantState().note.IsEmpty(),
            QString("%1 model(s) remembered before, %2 after").arg(heldFull).arg(remembered()));
    }
  }

  // An NPC whose display is on a pair's model gets no switch.
  QStringList files;
  for (const auto & pair : pairs)
    files << QString::number(pair.primaryFileID) << QString::number(pair.alternateFileID);
  sqlResult npc = GAMEDATABASE.sqlQuery("SELECT Creature.ID FROM Creature JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
                                        "JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
                                        "WHERE CreatureModelData.FileDataID IN (" + files.join(',') + ") ORDER BY Creature.ID LIMIT 1");
  if (!npc.valid || npc.values.empty())
    skip("model variants: an NPC on a pair's model", "no NPC of this client is one");
  else
  {
    frame->LoadNPC(npc.values[0][0].toUInt());
    settle();
    const ModelViewer::CharacterVariantState state = frame->characterVariantState();
    check(QString("model variants: NPC %1, on a pair's model, gets no Model selector").arg(npc.values[0][0]), !state.shown,
          QString("shown %1").arg(state.shown ? 1 : 0));
  }

  // A display that names a pair's alternate model and stores choices of its primary ChrModel is shown on the primary,
  // and Load NPC / Model by ID says it loaded.
  int routedDisplay = 0;
  const RaceInfos::VariantPair * routedPair = nullptr;
  for (const auto & pair : pairs)
  {
    sqlResult routed = GAMEDATABASE.sqlQuery(QString("SELECT CreatureDisplayInfo.ID FROM CreatureDisplayInfo JOIN CreatureModelData "
                                                     "ON CreatureDisplayInfo.ModelID = CreatureModelData.ID JOIN CreatureDisplayInfoOption "
                                                     "ON CreatureDisplayInfoOption.CreatureDisplayInfoExtraID = CreatureDisplayInfo.ExtendedDisplayInfoID "
                                                     "JOIN ChrCustomizationOption ON ChrCustomizationOption.ID = CreatureDisplayInfoOption.ChrCustomizationOptionID "
                                                     "WHERE CreatureModelData.FileDataID = %1 AND ChrCustomizationOption.ChrModelID = %2 "
                                                     "ORDER BY CreatureDisplayInfo.ID LIMIT 1").arg(pair.alternateFileID).arg(pair.primaryChrModelID));
    if (routed.valid && !routed.values.empty())
    {
      routedDisplay = routed.values[0][0].toInt();
      routedPair = &pair;
      break;
    }
  }
  if (!routedPair)
    skip("model variants: a display on an alternate model with the primary's choices", "no display of this client is one");
  else
  {
    ModelIdLookup::Resolved resolved;
    wxString why;
    const bool resolvedOk = ModelIdLookup::resolve(ModelIdLookup::Kind::CreatureDisplay, routedDisplay, resolved, why);
    const bool loaded = resolvedOk && frame->LoadModelById(resolved, why);
    settle();
    WoWModel * shown = frame->riderModel();
    size_t stored = 0;
    if (shown)
      for (const auto & oc : shown->cd.captureAppearance().selection)
        stored += shown->cd.isStoredChoice(oc.first) ? 1 : 0;
    check(QString("model variants: creature display %1 (alternate model %2, ChrModel %3's choices) is shown on the primary model %4, "
                  "and Load NPC / Model by ID says it loaded").arg(routedDisplay).arg(routedPair->alternateFileID)
            .arg(routedPair->primaryChrModelID).arg(routedPair->primaryFileID),
          loaded && resolved.loadFileDataId == routedPair->primaryFileID && shown && shown->gamefile &&
            (int)shown->gamefile->fileDataId() == routedPair->primaryFileID && stored > 0 &&
            !frame->characterVariantState().shown,
          QString("resolved %1 loaded %2 why %3 | %4 stored choice(s)").arg(resolved.loadFileDataId).arg(loaded)
            .arg(QString::fromWCharArray(why.wc_str())).arg(stored));
  }
}

// -customizationtest, every client: the item list the equipment dialogs choose from, and the dialogs on a character.
// The list comes from the client's own Item and ItemSparse, and a client whose item tables are on this computer and
// come back empty fails: Classic Beta's did (their layouts were in no definition), and every equipment dialog listed
// only "None". Independent expectations per client, read from its DB2 files by a reader apart from this loader: the
// list's size and each slot's (for the build named; another build is reported, not judged: its data may have moved),
// and representative items with their name, inventory type, class, sheathe and display. Then, on the client's Human
// male, through the dialogs as a click reaches them: each slot's dialog lists "None" and that slot's items; the filter
// finds an item by its name and by its ID; one category alone lists that category's items; choosing a row equips the
// item (its display resolved, a model item's models loaded), choosing another row replaces it and "None" takes it
// off, the other slots keeping theirs. Classic Beta also dresses its Blood Elf male and female (the helm takes each
// one's own model) and the Human male's Classic model.
template <class Check, class Skip>
static void checkItemCatalogue(ModelViewer * frame, const Check & check, const Skip & skip)
{
  const auto settle = []() {
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
  };
  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  for (const QString table : { QString("Item"), QString("ItemSparse") })
  {
    sqlResult n = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1").arg(table));
    const int rows = (n.valid && !n.values.empty()) ? n.values[0][0].toInt() : 0;
    wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
    if (rows == 0 && folder && folder->isRemoteFile(QString("dbfilesclient/%1.db2").arg(table.toLower())))
    {
      skip("item list", QString("the client's %1 is not installed on this computer").arg(table));
      return;
    }
    check(QString("item list: the client's %1 was read").arg(table), rows > 0, QString("%1 rows").arg(rows));
    if (rows == 0)
      return;
  }

  struct SlotExpect { CharSlots slot; const char * caption; };
  struct ItemExpect { int id; const char * name; int inventoryType, itemClass, subclass, sheathe, display; CharSlots slot; };
  struct CatalogueExpect { const char * product; const char * build; int items; std::vector<int> slotItems; std::vector<ItemExpect> pieces; };
  // slotItems: each slot's items, "None" apart, in the order of equipmentSlots
  static const SlotExpect equipmentSlots[] = {
    { CS_HEAD, "Head" }, { CS_SHOULDER, "Shoulder" }, { CS_SHIRT, "Shirt" }, { CS_CHEST, "Chest" }, { CS_BELT, "Belt" },
    { CS_PANTS, "Legs" }, { CS_BOOTS, "Boots" }, { CS_BRACERS, "Bracers" }, { CS_GLOVES, "Gloves" },
    { CS_HAND_RIGHT, "Right hand" }, { CS_HAND_LEFT, "Left hand" }, { CS_CAPE, "Cape" }, { CS_TABARD, "Tabard" },
    { CS_QUIVER, "Quiver" },
  };
  static const CatalogueExpect expectations[] = {
    { "wow_classic_beta", "1.60.1.70338", 18082,
      { 1311, 1285, 83, 1729, 1345, 1531, 1569, 1238, 1523, 3569, 3575, 731, 53, 155 },
      { { 8345, "Wolfshead Helm", 1, 4, 2, 0, 28987, CS_HEAD }, { 12625, "Dawnbringer Shoulders", 3, 4, 4, 0, 25827, CS_SHOULDER },
        { 2870, "Shining Silver Breastplate", 5, 4, 3, 0, 9695, CS_CHEST }, { 7938, "Truesilver Gauntlets", 10, 4, 4, 0, 13172, CS_GLOVES },
        { 12619, "Enchanted Thorium Leggings", 7, 4, 4, 0, 22882, CS_PANTS }, { 12021, "Shieldplate Sabatons", 8, 4, 4, 0, 17814, CS_BOOTS },
        { 810, "Hammer of the Northern Wind", 21, 2, 4, 3, 19726, CS_HAND_RIGHT }, { 6898, "Orb of Soran'ruk", 23, 4, 0, 3, 21597, CS_HAND_LEFT } } },
    { "wow_classic_era", "1.15.9.70003", 15433,
      { 1053, 1038, 72, 1426, 1043, 1217, 1225, 983, 1183, 2933, 2969, 610, 32, 143 },
      { { 8345, "Wolfshead Helm", 1, 4, 2, 0, 28987, CS_HEAD }, { 12625, "Dawnbringer Shoulders", 3, 4, 4, 0, 25827, CS_SHOULDER },
        { 2870, "Shining Silver Breastplate", 5, 4, 3, 0, 9695, CS_CHEST }, { 7938, "Truesilver Gauntlets", 10, 4, 4, 0, 13172, CS_GLOVES },
        { 12619, "Enchanted Thorium Leggings", 7, 4, 4, 0, 22882, CS_PANTS }, { 12021, "Shieldplate Sabatons", 8, 4, 4, 0, 17814, CS_BOOTS },
        { 810, "Hammer of the Northern Wind", 21, 2, 4, 3, 19726, CS_HAND_RIGHT }, { 6898, "Orb of Soran'ruk", 23, 4, 0, 7, 21597, CS_HAND_LEFT } } },
    { "wow_classic", "5.5.4.70268", 65083,
      { 5319, 5125, 161, 6384, 4474, 5625, 4742, 4221, 5511, 11482, 12325, 2160, 130, 243 },
      { { 8345, "Wolfshead Helm", 1, 4, 2, 0, 18392, CS_HEAD }, { 12625, "Dawnbringer Shoulders", 3, 4, 4, 0, 25827, CS_SHOULDER },
        { 2870, "Shining Silver Breastplate", 5, 4, 3, 0, 9695, CS_CHEST }, { 7938, "Truesilver Gauntlets", 10, 4, 4, 0, 13172, CS_GLOVES },
        { 12619, "Enchanted Thorium Leggings", 7, 4, 4, 0, 22882, CS_PANTS }, { 12021, "Shieldplate Sabatons", 8, 4, 4, 0, 17814, CS_BOOTS },
        { 810, "Hammer of the Northern Wind", 13, 2, 4, 3, 19726, CS_HAND_RIGHT }, { 6898, "Orb of Soran'ruk", 23, 4, 0, 7, 21597, CS_HAND_LEFT } } },
    { "wow", "12.1.0.69933", 137501,
      { 11493, 10572, 932, 12284, 9692, 11296, 10056, 8962, 10923, 25689, 28116, 6170, 1008, 297 },
      { { 8345, "Wolfshead Helm", 1, 4, 2, 0, 18392, CS_HEAD }, { 12625, "Dawnbringer Shoulders", 3, 4, 4, 0, 25827, CS_SHOULDER },
        { 2870, "Shining Silver Breastplate", 5, 4, 3, 0, 9695, CS_CHEST }, { 7938, "Truesilver Gauntlets", 10, 4, 4, 0, 13172, CS_GLOVES },
        { 12619, "Enchanted Thorium Leggings", 7, 4, 4, 0, 13382, CS_PANTS }, { 12021, "Shieldplate Sabatons", 8, 4, 4, 0, 17814, CS_BOOTS },
        { 810, "Hammer of the Northern Wind", 13, 2, 4, 3, 19726, CS_HAND_RIGHT }, { 6898, "Orb of Soran'ruk", 23, 4, 0, 7, 179728, CS_HAND_LEFT } } },
  };
  const CatalogueExpect * e = nullptr;
  for (const CatalogueExpect & c : expectations)
    if (profile.product == QLatin1String(c.product))
      e = &c;
  if (!e)
  {
    skip("item list", QString("no expectation for %1").arg(profile.product));
    return;
  }
  const bool sameBuild = GAMEDIRECTORY.version() == QLatin1String(e->build);

  // ---- the list ----
  std::map<int, int> perSlot;
  std::set<int> ids;
  int typeless = 0, duplicates = 0;
  for (size_t i = 1; i < items.items.size(); i++)
  {
    const ItemRecord & r = items.items[i];
    typeless += r.type == 0 ? 1 : 0;
    duplicates += ids.insert(r.id).second ? 0 : 1;
    for (const SlotExpect & s : equipmentSlots)
      perSlot[s.slot] += correctType((ssize_t)r.type, s.slot) ? 1 : 0;
  }
  const int listed = (int)items.items.size() - 1;
  check("item list: \"None\" first, then items each listed once, every one with an inventory type",
        !items.items.empty() && items.items[0].id == 0 && typeless == 0 && duplicates == 0 && listed > 0,
        QString("%1 items, %2 without an inventory type, %3 listed twice").arg(listed).arg(typeless).arg(duplicates));
  QStringList slotCounts, slotWanted;
  bool slotsSame = true;
  for (size_t i = 0; i < sizeof(equipmentSlots) / sizeof(equipmentSlots[0]); i++)
  {
    slotCounts << QString("%1 %2").arg(equipmentSlots[i].caption).arg(perSlot[equipmentSlots[i].slot]);
    slotWanted << QString("%1 %2").arg(equipmentSlots[i].caption).arg(e->slotItems[i]);
    slotsSame = slotsSame && perSlot[equipmentSlots[i].slot] == e->slotItems[i];
  }
  const QString countWhat = QString("item list: %1 items, and each slot's (build %2)").arg(e->items).arg(e->build);
  const QString countDetail = QString("%1 items; %2 | expected %3").arg(listed).arg(slotCounts.join(", ")).arg(slotWanted.join(", "));
  if (sameBuild)
    check(countWhat, listed == e->items && slotsSame, countDetail);
  else
    skip(countWhat, QString("this is build %1: %2").arg(GAMEDIRECTORY.version(), countDetail));
  for (const ItemExpect & p : e->pieces)
  {
    const ItemRecord & r = items.getById(p.id);
    sqlResult sheathe = GAMEDATABASE.sqlQuery(QString("SELECT SheatheType FROM Item WHERE ID = %1").arg(p.id));
    const int rawSheathe = (sheathe.valid && !sheathe.values.empty()) ? sheathe.values[0][0].toInt() : -1;
    check(QString("item list: %1 \"%2\": inventory type %3, class %4/%5, sheathe %6").arg(p.id).arg(QString::fromLatin1(p.name))
            .arg(p.inventoryType).arg(p.itemClass).arg(p.subclass).arg(p.sheathe),
          r.id == p.id && r.name == QString::fromLatin1(p.name) && r.type == p.inventoryType && r.itemclass == p.itemClass &&
            r.subclass == p.subclass && rawSheathe == p.sheathe,
          QString("%1 \"%2\": inventory type %3, class %4/%5, sheathe %6").arg(r.id).arg(r.name).arg(r.type).arg(r.itemclass)
            .arg(r.subclass).arg(rawSheathe));
  }

  // ---- the dialogs, on a character ----
  CharControl * cc = g_charControl;
  const int humanFileId = RaceInfos::getFileIDForRaceSex(1, 0);
  GameFile * humanFile = humanFileId > 0 ? GAMEDIRECTORY.getFile(humanFileId) : nullptr;
  wow::WoWFolder * storage = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  if (!cc || !humanFile || (storage && storage->isRemoteFileId(humanFileId)))
  {
    if (cc && humanFileId > 0 && storage && storage->isRemoteFileId(humanFileId))
      skip("equipment dialogs", "the Human male's model is not installed on this computer");
    else
      check("equipment dialogs: the Human male has a race row and a model", false,
            QString("FileDataID %1").arg(humanFileId));
    return;
  }
  frame->LoadModel(humanFile, 1, 0);
  settle();
  WoWModel * m = frame->riderModel();
  if (!m || m->infos.raceID != 1)
  {
    check("equipment dialogs: the Human male loads", false, humanFile->fullname());
    return;
  }
  // A slot's dialog, as its button opens it.
  const auto open = [cc, settle](CharSlots slot, const char * caption) {
    cc->selectItem(UPDATE_ITEM, slot, wxString::FromAscii(caption).wc_str());
    settle();
    return dynamic_cast<FilteredChoiceDialog *>(cc->itemDialog);
  };
  const auto filterTo = [settle](FilteredChoiceDialog * dlg, const QString & text) {
    if (wxTextCtrl * box = dynamic_cast<wxTextCtrl *>(dlg->FindWindow(FilteredChoiceDialog::ID_FILTER_TEXT)))
      box->SetValue(text.toStdWString());   // EVT_TEXT, as typing does
    settle();
  };
  // Whether the item's row is among the rows the dialog shows.
  const auto shows = [](FilteredChoiceDialog * dlg, int id, const std::vector<int> & numbers) {
    for (size_t k = 0; k < numbers.size(); k++)
      if (numbers[k] == id)
      {
        dlg->SelectChoiceRow((int)k);
        const bool shown = dlg->m_listctrl->GetFirstSelected() >= 0;
        dlg->SelectChoiceRow(-1);
        return shown;
      }
    return false;
  };
  // Chooses the item's row as a click does: the list's own selection event.
  const auto choose = [settle](FilteredChoiceDialog * dlg, int index) {
    dlg->SelectChoiceRow(index);
    const long row = dlg->m_listctrl->GetFirstSelected();
    if (row < 0)
      return false;
    dlg->m_listctrl->Select(row, false);
    dlg->m_listctrl->Select(row, true);
    settle();
    return true;
  };
  const auto indexOf = [](const std::vector<int> & numbers, int id) {
    for (size_t k = 0; k < numbers.size(); k++)
      if (numbers[k] == id)
        return (int)k;
    return -1;
  };
  const auto worn = [](WoWModel * model) {
    QStringList list;
    for (int slot = 0; model && slot < NUM_CHAR_SLOTS; slot++)
      if (WoWItem * item = model->getItem((CharSlots)slot))
        if (item->isEquipped())
          list << QString("%1:%2/%3").arg(slot).arg(item->id()).arg(item->displayId());
    return list.join(' ');
  };

  for (size_t i = 0; i < sizeof(equipmentSlots) / sizeof(equipmentSlots[0]); i++)
  {
    FilteredChoiceDialog * dlg = open(equipmentSlots[i].slot, equipmentSlots[i].caption);
    const int rows = dlg ? dlg->m_listctrl->GetItemCount() : -1;
    check(QString("equipment dialog %1: lists \"None\" and the slot's items").arg(equipmentSlots[i].caption),
          dlg && rows == 1 + perSlot[equipmentSlots[i].slot] && (int)cc->numbers.size() == rows && rows > 1 && cc->numbers[0] == 0,
          QString("%1 rows for %2 items").arg(rows).arg(perSlot[equipmentSlots[i].slot]));
    cc->ClearItemDialog();
  }

  // Each representative item: found by name and by ID, alone in its category, then worn.
  std::map<CharSlots, int> wearing;
  for (const ItemExpect & p : e->pieces)
  {
    const SlotExpect * s = nullptr;
    for (const SlotExpect & candidate : equipmentSlots)
      if (candidate.slot == p.slot)
        s = &candidate;
    FilteredChoiceDialog * dlg = open(p.slot, s->caption);
    const int index = dlg ? indexOf(cc->numbers, p.id) : -1;
    if (!dlg || index < 0)
    {
      check(QString("equipment dialog %1: lists %2").arg(s->caption).arg(p.id), false, QString());
      cc->ClearItemDialog();
      continue;
    }
    const int all = dlg->m_listctrl->GetItemCount();
    filterTo(dlg, QString::fromLatin1(p.name));
    const int byName = dlg->m_listctrl->GetItemCount();
    const bool foundByName = shows(dlg, p.id, cc->numbers);
    filterTo(dlg, QString::number(p.id));
    const int byId = dlg->m_listctrl->GetItemCount();
    const bool foundById = shows(dlg, p.id, cc->numbers);
    check(QString("equipment dialog %1: the filter finds %2 by its name and by its ID").arg(s->caption).arg(p.id),
          foundByName && foundById && byName < all && byId < all,
          QString("%1 rows unfiltered, %2 by name, %3 by ID").arg(all).arg(byName).arg(byId));
    filterTo(dlg, QString());
    if (CategoryChoiceDialog * cat = dynamic_cast<CategoryChoiceDialog *>(dlg))
    {
      // The category checklist lists each category the dialog's rows fall in once, in the order of CharControl's
      // category names -- "None"'s too: this dialog keeps no row out of the categories (CharControl::selectItem), so
      // "None" is shown only while the category CharControl gave it is checked.
      const int category = cc->cats[index];
      std::set<int> listedCategories;
      int inCategory = 0;
      for (size_t k = 0; k < cc->cats.size(); k++)
      {
        listedCategories.insert(cc->cats[k]);
        inCategory += cc->cats[k] == category ? 1 : 0;
      }
      const int position = (int)std::distance(listedCategories.begin(), listedCategories.find(category));
      // Each representative item's category, read from the clients' ItemSubClass by the independent reader (the same
      // in every client).
      static const std::map<int, const char *> categoryOf = {
        { 8345, "Leather" }, { 12625, "Plate" }, { 7938, "Plate" }, { 12619, "Plate" }, { 12021, "Plate" },
        { 2870, "Mail" }, { 810, "One-Handed Maces" }, { 6898, "Miscellaneous" },
      };
      const auto named = categoryOf.find(p.id);
      wxCheckListBox * boxes = dynamic_cast<wxCheckListBox *>(cat->FindWindow(CategoryChoiceDialog::ID_CAT_LIST));
      const int count = boxes ? (int)boxes->GetCount() : 0;
      for (int c = 0; c < count; c++)
        cat->Check(c, c == position);
      cat->DoFilter();
      settle();
      const int alone = cat->m_listctrl->GetItemCount();
      const bool stillThere = shows(cat, p.id, cc->numbers);
      check(QString("equipment dialog %1: %2's category alone (\"%3\") lists its %4 items").arg(s->caption).arg(p.id)
              .arg(boxes && position < count ? QString::fromStdWString(boxes->GetString(position).ToStdWstring()) : QString("?")).arg(inCategory),
            count == (int)listedCategories.size() && alone == inCategory && stillThere && boxes && position < count &&
              (named == categoryOf.end() || boxes->GetString(position) == wxString::FromAscii(named->second)),
            QString("%1 categories, %2 rows").arg(count).arg(alone));
      for (int c = 0; c < count; c++)
        cat->Check(c, true);
      cat->DoFilter();
      settle();
    }
    const bool chosen = choose(dlg, index);
    m = frame->riderModel();
    WoWItem * item = m ? m->getItem(p.slot) : nullptr;
    const bool modelItem = p.slot == CS_HEAD || p.slot == CS_SHOULDER || p.slot == CS_HAND_RIGHT || p.slot == CS_HAND_LEFT;
    check(QString("equipment dialog %1: choosing %2 wears it, display %3%4").arg(s->caption).arg(p.id).arg(p.display)
            .arg(modelItem ? QString(", its models loaded") : QString()),
          chosen && item && item->id() == p.id && item->displayId() == p.display && (!modelItem || !item->models().empty()),
          item ? QString("item %1, display %2, %3 models; wearing %4").arg(item->id()).arg(item->displayId()).arg(item->models().size()).arg(worn(m))
               : QString("no item slot"));
    wearing[p.slot] = p.id;
    cc->ClearItemDialog();
  }
  // Every piece still worn together.
  m = frame->riderModel();
  bool all = m != nullptr;
  for (const auto & w : wearing)
    all = all && m->getItem(w.first) && m->getItem(w.first)->id() == w.second;
  check("equipment: every chosen item is worn together", all, worn(m));

  // Replace, then take off: the other slots keep theirs. The replacement shows its own look: another item with one
  // appearance (its display read here by SQL) takes the place of the first one's models; an item this client gives
  // no appearance shows none -- not the look of the item it replaced.
  if (!e->pieces.empty())
  {
    const ItemExpect & p = e->pieces[0];
    const SlotExpect * s = nullptr;
    for (const SlotExpect & candidate : equipmentSlots)
      if (candidate.slot == p.slot)
        s = &candidate;
    FilteredChoiceDialog * dlg = open(p.slot, s->caption);
    int other = -1, otherDisplay = 0, plain = -1;
    for (size_t k = 1; dlg && k < cc->numbers.size() && (other < 0 || plain < 0); k++)
    {
      if (cc->numbers[k] == p.id)
        continue;
      sqlResult looks = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*), MAX(COALESCE(P.ItemDisplayInfoID, 0)) FROM ItemModifiedAppearance A "
                                                      "LEFT JOIN ItemAppearance P ON P.ID = A.ItemAppearanceID WHERE A.ItemID = %1").arg(cc->numbers[k]));
      const int appearances = (looks.valid && !looks.values.empty()) ? looks.values[0][0].toInt() : -1;
      const int display = appearances > 0 ? looks.values[0][1].toInt() : 0;
      if (other < 0 && appearances == 1 && display > 0 && display != p.display)
      {
        other = (int)k;
        otherDisplay = display;
      }
      else if (plain < 0 && appearances == 0)
        plain = (int)k;
    }
    const auto othersKept = [&](WoWModel * model) {
      bool kept = model != nullptr;
      for (const auto & w : wearing)
        if (w.first != p.slot)
          kept = kept && model->getItem(w.first) && model->getItem(w.first)->id() == w.second;
      return kept;
    };
    const auto shownModels = [](WoWItem * item) {
      QStringList files;
      for (const auto & entry : item ? item->models() : std::map<POSITION_SLOTS, WoWModel *>())
        if (entry.second && entry.second->gamefile)
          files << QString::number(entry.second->gamefile->fileDataId());
      return files;
    };
    m = frame->riderModel();
    const QStringList before = shownModels(m ? m->getItem(p.slot) : nullptr);
    const int otherId = other > 0 ? cc->numbers[other] : -1;
    const bool replaced = other > 0 && choose(dlg, other);
    m = frame->riderModel();
    WoWItem * item = m ? m->getItem(p.slot) : nullptr;
    const QStringList after = shownModels(item);
    check(QString("equipment dialog %1: choosing another row (%2, display %3) replaces %4 and its look, the other slots keep theirs")
            .arg(s->caption).arg(otherId).arg(otherDisplay).arg(p.id),
          replaced && othersKept(m) && item && item->id() == otherId && item->displayId() == otherDisplay && !after.isEmpty() && after != before,
          QString("models %1 -> %2; wearing %3").arg(before.join(' ')).arg(after.join(' ')).arg(worn(m)));
    if (plain < 0)
      skip(QString("equipment dialog %1: an item with no appearance").arg(s->caption), "the slot lists none in this client");
    else
    {
      const int plainId = cc->numbers[plain];
      const bool chosen = choose(dlg, plain);
      m = frame->riderModel();
      item = m ? m->getItem(p.slot) : nullptr;
      check(QString("equipment dialog %1: choosing an item this client gives no appearance (%2) shows no look, not the one it replaced")
              .arg(s->caption).arg(plainId),
            chosen && othersKept(m) && item && item->id() == plainId && item->displayId() <= 0 && item->models().empty(),
            item ? QString("item %1, display %2, %3 models; wearing %4").arg(item->id()).arg(item->displayId()).arg(item->models().size()).arg(worn(m))
                 : QString("no item slot"));
    }
    const bool none = dlg && choose(dlg, 0);
    m = frame->riderModel();
    check(QString("equipment dialog %1: choosing \"None\" takes the item off, the other slots keep theirs").arg(s->caption),
          none && othersKept(m) && m->getItem(p.slot) && !m->getItem(p.slot)->isEquipped(), worn(m));
    cc->ClearItemDialog();
  }

  // A weapon replaced by a hand item this client gives no appearance: nothing in the hand, and the hand no longer closed
  // around the weapon that is gone.
  {
    FilteredChoiceDialog * dlg = open(CS_HAND_RIGHT, "Right hand");
    int plain = -1;
    for (size_t k = 1; dlg && k < cc->numbers.size() && plain < 0; k++)
    {
      sqlResult looks = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM ItemModifiedAppearance WHERE ItemID = %1").arg(cc->numbers[k]));
      if (looks.valid && !looks.values.empty() && looks.values[0][0].toInt() == 0)
        plain = (int)k;
    }
    m = frame->riderModel();
    WoWItem * hand = m ? m->getItem(CS_HAND_RIGHT) : nullptr;
    const bool weaponBefore = hand && !hand->models().empty() && m->charModelDetails.closeRHand;
    if (plain < 0)
      skip("equipment dialog Right hand: an item with no appearance", "the slot lists none in this client");
    else
    {
      const int plainId = cc->numbers[plain];
      const bool chosen = choose(dlg, plain);
      m = frame->riderModel();
      hand = m ? m->getItem(CS_HAND_RIGHT) : nullptr;
      check(QString("equipment dialog Right hand: a weapon replaced by an item this client gives no appearance (%1) leaves the "
                    "hand empty and open").arg(plainId),
            weaponBefore && chosen && hand && hand->id() == plainId && hand->displayId() <= 0 && hand->models().empty() &&
              !m->charModelDetails.closeRHand,
            hand ? QString("weapon held before %1; item %2, display %3, %4 models, hand closed %5").arg(weaponBefore ? 1 : 0)
                     .arg(hand->id()).arg(hand->displayId()).arg(hand->models().size()).arg(m->charModelDetails.closeRHand ? 1 : 0)
                 : QString("no item slot"));
    }
    cc->ClearItemDialog();
  }

  // ---- Classic Beta: its Blood Elves and the Human male's Classic model wear them too ----
  if (profile.product != QLatin1String("wow_classic_beta"))
    return;
  // The helm's model for each race and sex, from ComponentModelFileData (independent reading): ModelResources 17256.
  struct Wearer { int race; int sex; int helmFile; const char * who; };
  static const Wearer wearers[] = { { 10, 0, 141711, "Blood Elf male" }, { 10, 1, 141710, "Blood Elf female" } };
  const auto helmFileOf = [](WoWModel * model) {
    WoWItem * item = model ? model->getItem(CS_HEAD) : nullptr;
    if (!item)
      return -1;
    for (const auto & entry : item->models())
      if (entry.second && entry.second->gamefile)
        return (int)entry.second->gamefile->fileDataId();
    return -1;
  };
  const auto dress = [&]() {
    for (const ItemExpect & p : e->pieces)
    {
      const SlotExpect * s = nullptr;
      for (const SlotExpect & candidate : equipmentSlots)
        if (candidate.slot == p.slot)
          s = &candidate;
      FilteredChoiceDialog * dlg = open(p.slot, s->caption);
      const int index = dlg ? indexOf(cc->numbers, p.id) : -1;
      if (index > 0)
        choose(dlg, index);
      cc->ClearItemDialog();
    }
  };
  for (const Wearer & w : wearers)
  {
    GameFile * file = GAMEDIRECTORY.getFile(RaceInfos::getFileIDForRaceSex(w.race, w.sex));
    if (!file)
    {
      check(QString("equipment on the %1: its model is in the client").arg(w.who), false, QString());
      continue;
    }
    frame->LoadModel(file, w.race, w.sex);
    settle();
    dress();
    m = frame->riderModel();
    bool allWorn = m && m->infos.raceID == w.race && m->infos.sexID == w.sex;
    for (const ItemExpect & p : e->pieces)
      allWorn = allWorn && m->getItem(p.slot) && m->getItem(p.slot)->id() == p.id && m->getItem(p.slot)->displayId() == p.display;
    check(QString("equipment on the %1 (%2): every piece worn, the helm on its own model %3").arg(w.who).arg(file->fullname()).arg(w.helmFile),
          allWorn && helmFileOf(m) == w.helmFile, QString("helm model %1; wearing %2").arg(helmFileOf(m)).arg(worn(m)));
  }
  frame->LoadModel(humanFile, 1, 0);
  settle();
  dress();
  wxString why;
  const bool switched = frame->SwitchCharacterVariant(CharacterModelVariant::Classic, why);
  settle();
  m = frame->riderModel();
  bool allWorn = switched && m && m->modelGeneration() == CharacterModelVariant::Classic;
  for (const ItemExpect & p : e->pieces)
    allWorn = allWorn && m->getItem(p.slot) && m->getItem(p.slot)->id() == p.id && m->getItem(p.slot)->displayId() == p.display;
  check("equipment on the Human male's Classic model: every piece kept across the switch, the helm on its model 141721",
        allWorn && helmFileOf(m) == 141721,
        QString("%1; helm model %2; wearing %3").arg(m && m->gamefile ? m->gamefile->fullname() : QString("no model")).arg(helmFileOf(m)).arg(worn(m)));
}

// -customizationtest, every client: the Characters list's Playable Races and NPC Races. A race goes under one of the
// two as its ChrRaces row's not-playable flag (bit 0 of Flags) says, except the two form races 23 and 75, which the
// viewer always lists as playable (RaceInfos::init). Independent expectations, read from each client's DB2 files by a
// reader apart from this loader: the Blood Elf's place and models, listed once by name (the Browse tree's nodes are
// names). Retail and MoP Classic make it playable. Classic Beta does not: its ChrRaces row carries the flag, and the
// client's own race and class pairs (CharBaseInfo), starting outfits (CharacterLoadout), racial abilities and
// character-creation icons all leave race 10 out; and its only Blood Elf character models are the High Definition
// ones, with no Classic model to switch to (no ChrModelAltVariant pair; the classic files in its storage are no
// ChrModel's). Classic Era has no Blood Elf. And Classic Beta's whole Playable list: the races its data makes playable,
// 1-8 and the two Skyborne (95, 96) -- checked apart from the forms 23 and 75, which are listed there only by the
// viewer's rule although Classic Beta's data marks them (and their base races) not playable: a known limitation of that
// rule, reported on a line of its own so a change to it is seen; and its Blood Elf male's and female's options as the
// Appearance panel lists them, with the choices a player is offered of each (Retail's High Definition option set: the
// four Demon Hunter options offer only their first choice, and Eye Style is no player's option).
template <class Check, class Skip>
static void checkRaceCategories(ModelViewer * frame, const Check & check, const Skip & skip)
{
  const QString product = GAMEDIRECTORY.clientProfile().product;
  const std::vector<RaceInfos::RaceMenuEntry> menu = RaceInfos::getRaceMenu();
  std::vector<int> playable;
  const RaceInfos::RaceMenuEntry * bloodElf = nullptr;
  QStringList bloodElfNames;   // every entry the tree would show under the Blood Elf's name, with its category
  for (const RaceInfos::RaceMenuEntry & entry : menu)
  {
    if (!entry.isNPC)
      playable.push_back(entry.raceID);
    if (entry.raceID == 10)
      bloodElf = &entry;
  }
  for (const RaceInfos::RaceMenuEntry & entry : menu)
    if (bloodElf && entry.name == bloodElf->name)
      bloodElfNames << QString("%1 (race %2)").arg(entry.isNPC ? "NPC Races" : "Playable Races").arg(entry.raceID);
  std::sort(playable.begin(), playable.end());
  QStringList playableText;
  for (int race : playable)
    playableText << QString::number(race);

  struct AppearanceExpect { const char * name; int valid, total; };
  struct RaceExpect
  {
    const char * product;
    bool present, playable;
    int male, female, maleLayout, femaleLayout;
    std::vector<int> playableRaces;   // by the client's data (the form races 23 and 75 apart); empty: not checked
    std::vector<AppearanceExpect> maleOptions, femaleOptions;   // empty: not checked for this client
  };
  static const RaceExpect expectations[] = {
    { "wow", true, true, 1100087, 1100258, 121, 122, {}, {}, {} },
    { "wow_classic", true, true, 117170, 116921, 1, 1, {}, {}, {} },
    { "wow_classic_era", false, false, -1, -1, 0, 0, {}, {}, {} },
    { "wow_classic_beta", true, false, 1100087, 1100258, 121, 122, { 1, 2, 3, 4, 5, 6, 7, 8, 95, 96 },
      { { "Face", 10, 20 }, { "Skin Color", 14, 31 }, { "Hair Style", 21, 21 }, { "Hair Color", 20, 21 }, { "Facial Hair", 13, 13 },
        { "Ears", 3, 3 }, { "Horns", 1, 7 }, { "Blindfold", 1, 12 }, { "Tattoo", 1, 7 }, { "Tattoo Color", 1, 6 }, { "Eye Color", 11, 34 },
        { "Eyesight", 4, 4 } },
      { { "Face", 10, 20 }, { "Skin Color", 14, 30 }, { "Hair Style", 25, 25 }, { "Hair Color", 20, 22 }, { "Horns", 1, 7 },
        { "Eye Color", 11, 34 }, { "Blindfold", 1, 12 }, { "Ears", 3, 3 }, { "Earrings", 14, 14 }, { "Necklace", 4, 4 }, { "Tattoo", 1, 7 },
        { "Tattoo Color", 1, 7 }, { "Armbands", 4, 4 }, { "Jewelry Color", 2, 2 }, { "Bracelets", 7, 7 }, { "Eyesight", 4, 4 } } },
  };
  const RaceExpect * e = nullptr;
  for (const RaceExpect & c : expectations)
    if (product == QLatin1String(c.product))
      e = &c;
  if (!e)
  {
    skip("race categories: the Blood Elf", QString("no expectation for %1").arg(product));
    return;
  }
  if (!e->present)
  {
    check("race categories: the client has no Blood Elf", bloodElf == nullptr, QString());
    return;
  }
  bool noPair = true, layouts = true;
  QString chrModels;
  for (int sex = 0; sex <= 1; sex++)
  {
    RaceInfos ri;
    RaceInfos::VariantPair pair;
    if (RaceInfos::getRaceInfosForRaceSex(10, sex, ri) && !ri.ChrModelID.empty())
    {
      noPair = noPair && !RaceInfos::getVariantPair(ri.ChrModelID[0], pair);
      layouts = layouts && ri.textureLayoutID == (sex == 1 ? e->femaleLayout : e->maleLayout);
      chrModels += QString(" %1 (layout %2)").arg(ri.ChrModelID[0]).arg(ri.textureLayoutID);
    }
    else
      layouts = false;
  }
  check(QString("race categories: the Blood Elf is listed once, under %1, on FileDataIDs %2 / %3 (texture layouts %4 / %5), with no model "
                "of the other generation").arg(e->playable ? "Playable Races" : "NPC Races").arg(e->male).arg(e->female)
          .arg(e->maleLayout).arg(e->femaleLayout),
        bloodElf && bloodElfNames.size() == 1 && bloodElf->isNPC == !e->playable && bloodElf->maleFileID == e->male &&
          bloodElf->femaleFileID == e->female && noPair && layouts,
        bloodElf ? QString("%1, files %2 / %3, ChrModel%4%5; by its name: %6").arg(bloodElf->isNPC ? "NPC Races" : "Playable Races")
                     .arg(bloodElf->maleFileID).arg(bloodElf->femaleFileID).arg(chrModels).arg(noPair ? QString() : QString(", a pair"))
                     .arg(bloodElfNames.join(", "))
                 : QString("not listed"));
  if (!e->playableRaces.empty())
  {
    std::vector<int> byData, forms;
    for (int race : playable)
      (race == 23 || race == 75 ? forms : byData).push_back(race);
    QStringList wanted;
    for (int race : e->playableRaces)
      wanted << QString::number(race);
    check(QString("race categories: Playable Races of %1 are the races its data makes playable (%2), the form races 23 and 75 "
                  "apart").arg(product).arg(wanted.join(' ')),
          byData == e->playableRaces, playableText.join(' '));
    check(QString("race categories: %1 lists the form races 23 and 75 as playable by the viewer's rule (RaceInfos::init), "
                  "although its data marks them not playable").arg(product),
          forms == std::vector<int>({ 23, 75 }), playableText.join(' '));
  }

  // The Blood Elf's options, as the Appearance panel lists them: "name valid/total".
  for (int sex = 0; sex <= 1; sex++)
  {
    const std::vector<AppearanceExpect> & expected = sex == 1 ? e->femaleOptions : e->maleOptions;
    if (expected.empty())
      continue;
    const QString who = sex == 1 ? QString("female") : QString("male");
    GameFile * file = GAMEDIRECTORY.getFile(sex == 1 ? e->female : e->male);
    if (!file)
    {
      check(QString("Blood Elf %1: its model is in the client").arg(who), false, QString());
      continue;
    }
    frame->LoadModel(file, 10, sex);
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
    WoWModel * m = frame->riderModel();
    if (!m || m->infos.raceID != 10 || m->infos.sexID != sex)
    {
      check(QString("Blood Elf %1: loads as race 10").arg(who), false, file->fullname());
      continue;
    }
    const std::vector<uint> options = m->cd.getCustomizationOptions();
    QStringList got, want;
    bool same = options.size() == expected.size();
    for (size_t i = 0; i < options.size(); i++)
    {
      sqlResult n = GAMEDATABASE.sqlQuery(QString("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = %1").arg(options[i]));
      sqlResult c = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1").arg(options[i]));
      const QString name = (n.valid && !n.values.empty()) ? n.values[0][0] : QString("?");
      const int valid = (int)m->cd.getCustomizationChoices(options[i]).size();
      const int total = (c.valid && !c.values.empty()) ? c.values[0][0].toInt() : -1;
      got << QString("%1 %2/%3").arg(name).arg(valid).arg(total);
      if (i < expected.size())
        same = same && name == QLatin1String(expected[i].name) && valid == expected[i].valid && total == expected[i].total;
    }
    for (const AppearanceExpect & o : expected)
      want << QString("%1 %2/%3").arg(o.name).arg(o.valid).arg(o.total);
    check(QString("Blood Elf %1 (%2): options in client order, valid choices of each").arg(who).arg(file->fullname()), same,
          "got " + got.join(", ") + " | expected " + want.join(", "));
  }
}

// -customizationtest on a Classic client (Classic Era, MoP Classic, Classic Beta): the same idea against the
// client's own data. Every expected value was read from the named build's DB2 files by a reader independent of
// this loader (MoP Classic 5.5.4.70032, Classic Beta 1.60.1.70235 -- unchanged in 1.60.1.70245 --, Classic Era
// 1.15.9.70003, whose ChrClasses was read once installed): a failure on a later build means "the data moved, go look". A check whose data is not installed on this computer (a client Battle.net
// has not finished installing) is reported as skipped, not failed: it is not the code's to pass. A table that is on
// disk and still not read fails.
static int doHeadlessClassicCustomizationTest(ModelViewer * frame)
{
  int passed = 0, failed = 0, skipped = 0;
  const auto check = [&passed, &failed](const QString & what, bool ok, const QString & detail) {
    ok ? passed++ : failed++;
    const QString line = QString("[customization-test] %1 %2%3").arg(ok ? "PASS" : "FAIL").arg(what)
                           .arg(detail.isEmpty() ? QString() : QString(" -- ") + detail);
    if (ok)
      LOG_INFO << line;
    else
      LOG_ERROR << line;
  };
  const auto skip = [&skipped](const QString & what, const QString & why) {
    skipped++;
    LOG_INFO << QString("[customization-test] SKIP %1 -- %2").arg(what, why);
  };
  // Whether a table's file is in the build but not on this computer (Battle.net has not installed it). A table
  // that IS on disk and still came back empty was refused (e.g. a layout no definition knows): that fails.
  const auto notInstalled = [](const char * table) {
    wow::WoWFolder * folder = dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
    return folder && folder->isRemoteFile(QString("dbfilesclient/%1.db2").arg(QString::fromLatin1(table).toLower()));
  };
  const auto resultName = [](CharDetails::RequirementResult r) {
    switch (r)
    {
      case CharDetails::REQUIREMENT_MET: return QString("met");
      case CharDetails::REQUIREMENT_NOT_PLAYER: return QString("not player");
      case CharDetails::REQUIREMENT_RACE: return QString("race");
      case CharDetails::REQUIREMENT_CLASS: return QString("class");
      case CharDetails::REQUIREMENT_UNLOCK: return QString("unlock");
      case CharDetails::REQUIREMENT_PREREQUISITE: return QString("prerequisite");
      case CharDetails::REQUIREMENT_CLASS_UNKNOWN: return QString("class unknown");
    }
    return QString("?");
  };
  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  LOG_INFO << "[customization-test]" << profile.describe();

  struct ModelExpect { int race; int sex; int chrModel; int fileID; int layout; };
  struct OptionExpect { const char * name; int valid; int total; };
  // A requirement and what the rule must answer for it: ordinary character, Demon Hunter, and unknown classes.
  struct ClassExpect { int req; int mask; CharDetails::RequirementResult ordinary, demonHunter, unknown; const char * what; };
  // A FileDataID listed twice in the build's ROOT, and the content key of the version that must be read (empty: none
  // is installed, nothing can be read).
  struct StorageExpect { int fileID; const char * contentKey; const char * what; };
  struct ClientExpect
  {
    const char * product;
    unsigned int classes;          // ChrClasses as a ClassMask
    unsigned int ordinary;         // every class but Death Knight and Demon Hunter
    std::vector<ClassExpect> classRules;
    std::vector<ModelExpect> models;
    std::vector<OptionExpect> humanMale; // the Human male's options, in client order
    std::vector<OptionExpect> humanMaleWithoutClasses; // the same with ChrClasses empty
    std::vector<StorageExpect> storage;
  };
  using R = CharDetails::RequirementResult;
  const R MET = CharDetails::REQUIREMENT_MET, CLASS = CharDetails::REQUIREMENT_CLASS, UNKNOWN = CharDetails::REQUIREMENT_CLASS_UNKNOWN;
  static const ClientExpect expectations[] = {
    { "wow_classic", 0x7FF, 0x7DF,
      { { 141, -1, MET, MET, MET, "every class" }, { 144, 0x7DF, MET, CLASS, UNKNOWN, "all but Death Knight and Demon Hunter" },
        { 146, 0xFDF, MET, MET, UNKNOWN, "all but Death Knight" }, { 142, 0x20, CLASS, CLASS, UNKNOWN, "Death Knight only" },
        { 12, 0, MET, MET, MET, "no class limit (an NPC row)" } },
      { { 1, 0, 1, 119940, 1 }, { 1, 1, 2, 119563, 1 }, { 2, 0, 3, 121287, 1 }, { 2, 1, 4, 121087, 1 }, { 24, 0, 47, 535052, 2 } },
      { { "Skin Color", 10, 16 }, { "Face", 12, 24 }, { "Hair Style", 17, 17 }, { "Hair Color", 10, 14 }, { "Facial Hair", 9, 9 } },
      { { "Skin Color", 10, 16 }, { "Hair Style", 17, 17 }, { "Hair Color", 10, 14 }, { "Facial Hair", 9, 9 } },
      { { 120191, "c5f43d0d7cab4f843b801b15e394009a", "high-resolution version first and not installed, standard version installed: the standard one" },
        { 119463, "9fec72bee8335dad5319d78549da972f", "both versions installed: the first listed (the high-resolution one) stays" },
        { 5446713, "8f3475633d8dd17b4128aab465c13d86", "first installed, the later one not: the first stays" } } },
    { "wow_classic_beta", 0x5DF, 0x5DF,
      { { 141, -1, MET, MET, MET, "every class" }, { 144, 0x37DF, MET, CLASS, UNKNOWN, "all but Death Knight and Demon Hunter" },
        { 146, 0x3FDF, MET, MET, UNKNOWN, "all but Death Knight" }, { 53, 0x20, CLASS, CLASS, UNKNOWN, "Death Knight only" },
        { 143, 0x800, CLASS, MET, UNKNOWN, "Demon Hunter only" }, { 12, 0, MET, MET, MET, "no class limit (an NPC row)" } },
      { { 1, 0, 1, 1011653, 103 }, { 1, 1, 2, 1000764, 104 }, { 2, 0, 3, 917116, 105 }, { 2, 1, 4, 949470, 106 } },
      { { "Skin Color", 13, 19 }, { "Face", 12, 12 }, { "Hair Style", 17, 17 }, { "Hair Color", 10, 14 }, { "Facial Hair", 9, 9 },
        { "Face Shape", 3, 3 }, { "Eyebrows", 12, 12 }, { "Eye Color", 19, 41 }, { "Ears", 1, 1 } },
      { { "Skin Color", 13, 19 }, { "Face", 12, 12 }, { "Hair Style", 17, 17 }, { "Hair Color", 10, 14 }, { "Facial Hair", 9, 9 },
        { "Face Shape", 3, 3 }, { "Eyebrows", 12, 12 }, { "Ears", 1, 1 } },
      { { 121288, "392685753a3e76d2f08a0b95c8610f0e", "high-resolution version first and not installed, standard version installed: the standard one" },
        { 124154, "ff83ec974773b9674f0c6dadb5813309", "both versions installed: the first listed (the high-resolution one) stays" },
        { 5446713, "9ab7ecad2dc460efc3b963c80d850335", "first installed, the later one not: the first stays" },
        { 895912, "", "neither installed: the first stays, and it cannot be read" } } },
    { "wow_classic_era", 0x5DF, 0x5DF,
      { { 141, -1, MET, MET, MET, "every class" }, { 144, 0x7DF, MET, CLASS, UNKNOWN, "all but Death Knight and Demon Hunter" },
        { 146, 0xFDF, MET, MET, UNKNOWN, "all but Death Knight" }, { 12, 0, MET, MET, MET, "no class limit (an NPC row)" } },
      { { 1, 0, 1, 119940, 1 }, { 1, 1, 2, 119563, 1 } },
      { { "Skin Color", 10, 12 }, { "Face", 12, 12 }, { "Hair Style", 12, 12 }, { "Hair Color", 10, 10 }, { "Facial Hair", 9, 9 } },
      { { "Skin Color", 10, 12 }, { "Hair Style", 12, 12 }, { "Hair Color", 10, 10 }, { "Facial Hair", 9, 9 } },
      {} },
  };
  const ClientExpect * e = nullptr;
  for (const ClientExpect & c : expectations)
    if (profile.product == QLatin1String(c.product))
      e = &c;
  if (!e)
  {
    LOG_ERROR << "[customization-test] RESULT: FAIL (no Classic expectations for" << profile.product << ")";
    return 1;
  }

  // ---- 1. The class context comes from the client's own ChrClasses, and from nothing else -----------------
  const unsigned int classes = CharDetails::clientClassMask();
  const auto classMaskOf = [](int reqID, bool * found) {
    sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT ClassMask FROM ChrCustomizationReq WHERE ID = %1").arg(reqID));
    *found = r.valid && !r.values.empty();
    return *found ? r.values[0][0].toInt() : 0;
  };
  if (classes == 0 && notInstalled("ChrClasses"))
    skip("class context: ChrClasses", "the client's ChrClasses is not installed on this computer");
  else if (classes == 0)
    check("class context: ChrClasses was read", false, "the table is on this computer but holds no class: it was not read");
  else
    check("class context: ChrClasses as a mask, and the ordinary classes (all but Death Knight and Demon Hunter)",
          classes == e->classes && CharDetails::ordinaryClassMask(classes) == e->ordinary,
          QString("classes 0x%1, ordinary 0x%2").arg(classes, 0, 16).arg(CharDetails::ordinaryClassMask(classes), 0, 16));
  // The rule against this client's own masks, in each class context: the client's (when its ChrClasses is here),
  // and no class context at all -- what a client whose ChrClasses is not installed gets, never Retail's classes.
  for (const ClassExpect & c : e->classRules)
  {
    bool found = false;
    const int mask = classMaskOf(c.req, &found);
    if (!found)
    {
      check(QString("class rule: Req %1 is in this client's ChrCustomizationReq").arg(c.req), false, QString());
      continue;
    }
    const R unknown = CharDetails::classRequirement(mask, false, 0);
    const R unknownDH = CharDetails::classRequirement(mask, true, 0);
    QString detail = QString("ClassMask 0x%1: no class context -> %2 / %3").arg((unsigned int)mask, 0, 16).arg(resultName(unknown), resultName(unknownDH));
    bool ok = mask == c.mask && unknown == c.unknown && unknownDH == c.unknown;
    if (classes != 0)
    {
      const R ordinary = CharDetails::classRequirement(mask, false, classes);
      const R demonHunter = CharDetails::classRequirement(mask, true, classes);
      detail += QString(", ordinary -> %1, Demon Hunter -> %2").arg(resultName(ordinary), resultName(demonHunter));
      ok = ok && ordinary == c.ordinary && demonHunter == c.demonHunter;
    }
    check(QString("class rule: Req %1 (%2)").arg(c.req).arg(QString::fromLatin1(c.what)), ok, detail);
  }

  // ---- 2. Race and sex -> ChrModel -> the client's own model -----------------------------------------
  for (const ModelExpect & m : e->models)
  {
    RaceInfos ri;
    if (!RaceInfos::getRaceInfosForRaceSex(m.race, m.sex, ri))
    {
      if (notInstalled("ChrRaces") || notInstalled("ChrRaceXChrModel") || notInstalled("ChrModel"))
        skip(QString("model of race %1 sex %2").arg(m.race).arg(m.sex), "no race row: ChrRaces, ChrRaceXChrModel or ChrModel is not installed on this computer");
      else
        check(QString("model: race %1 sex %2 has a race row").arg(m.race).arg(m.sex), false, "its tables are on this computer");
      continue;
    }
    check(QString("model: race %1 sex %2 -> ChrModel %3, FileDataID %4, texture layout %5").arg(m.race).arg(m.sex).arg(m.chrModel).arg(m.fileID).arg(m.layout),
          !ri.ChrModelID.empty() && ri.ChrModelID[0] == m.chrModel && ri.modelFileID == m.fileID && ri.textureLayoutID == m.layout,
          QString("ChrModel %1, file %2, layout %3").arg(ri.ChrModelID.empty() ? -1 : ri.ChrModelID[0]).arg(ri.modelFileID).arg(ri.textureLayoutID));
  }

  // ---- 2b. A race's model is a character, whatever the listfile calls it --------------------------------
  // The Characters list and an NPC reach a race's model by FileDataID, and a model the listfile has no real name for
  // is listed under a generated one outside the character folders (Classic Beta's Skyborne models). Each must load as
  // its race's character -- as a creature it had no race, no customization and no composed skin, and drew white --
  // and a model no race uses must still load as a creature.
  {
    const auto canvasModel = [frame]() { return frame->canvas ? const_cast<WoWModel *>(frame->canvas->model()) : nullptr; };
    const auto settle = []() {
      for (int i = 0; i < 20; i++)
        wxTheApp->Yield(true);
    };
    const auto outsideCharacterFolders = [](GameFile * f) {
      return !f->fullname().startsWith("char", Qt::CaseInsensitive) && !f->fullname().startsWith("alternate\\char", Qt::CaseInsensitive);
    };
    const auto describe = [](WoWModel * m) {
      return m ? QString("modelType %1, isChar %2, race %3 sex %4, NPC %5").arg((int)m->modelType).arg(m->charModelDetails.isChar ? 1 : 0)
                   .arg(m->infos.raceID).arg(m->infos.sexID).arg(m->cd.isNPC ? 1 : 0)
               : QString("no model");
    };
    int raceModels = 0;
    for (const auto & entry : RaceInfos::getRaceMenu())
      for (int sex = 0; sex <= 1; sex++)
      {
        const int fileID = sex == 1 ? entry.femaleFileID : entry.maleFileID;
        GameFile * file = fileID > 0 ? GAMEDIRECTORY.getFile(fileID) : nullptr;
        if (!file || !outsideCharacterFolders(file))
          continue;
        raceModels++;
        frame->LoadModel(file, entry.raceID, sex);
        settle();
        WoWModel * m = canvasModel();
        check(QString("race model outside the character folders: race %1 (%2) sex %3, %4 [%5], loads as that race's character")
                .arg(entry.raceID).arg(QString::fromStdString(entry.name)).arg(sex).arg(file->fullname()).arg(fileID),
              m && m->gamefile == file && m->modelType == MT_CHAR && m->charModelDetails.isChar && m->infos.raceID == entry.raceID &&
                m->infos.sexID == sex,
              describe(m));
      }
    // A humanoid NPC (a display with extended info) on such a model.
    int npcs = 0;
    sqlResult humanoid = GAMEDATABASE.sqlQuery(
        "SELECT Creature.ID, CreatureModelData.FileDataID FROM Creature "
        "JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
        "JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
        "WHERE CreatureDisplayInfo.ExtendedDisplayInfoID != 0 ORDER BY Creature.ID");
    for (size_t i = 0; humanoid.valid && i < humanoid.values.size(); i++)
    {
      RaceInfos ri;
      const int fileID = humanoid.values[i][1].toInt();
      GameFile * file = GAMEDIRECTORY.getFile(RaceInfos::getHDModelForFileID(fileID));
      if (!file || !outsideCharacterFolders(file) || !RaceInfos::getRaceInfosForFileID(file->fileDataId(), ri))
        continue;
      npcs++;
      frame->LoadNPC(humanoid.values[i][0].toUInt());
      settle();
      WoWModel * m = canvasModel();
      check(QString("humanoid NPC %1 on a race model outside the character folders (%2) loads as a character NPC")
              .arg(humanoid.values[i][0]).arg(file->fullname()),
            m && m->gamefile == file && m->modelType == MT_CHAR && m->charModelDetails.isChar && m->cd.isNPC, describe(m));
    }
    LOG_INFO << QString("[customization-test] race models outside the character folders: %1 race/sex model(s), %2 humanoid NPC(s)")
                  .arg(raceModels).arg(npcs);
    // A model no race uses, outside the character folders: a creature, as before.
    sqlResult plain = GAMEDATABASE.sqlQuery(
        "SELECT CreatureModelData.FileDataID FROM Creature "
        "JOIN CreatureDisplayInfo ON Creature.DisplayID1 = CreatureDisplayInfo.ID "
        "JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
        "WHERE CreatureDisplayInfo.ExtendedDisplayInfoID = 0 ORDER BY Creature.ID");
    GameFile * creature = nullptr;
    for (size_t i = 0; plain.valid && i < plain.values.size() && !creature; i++)
    {
      RaceInfos ri;
      GameFile * file = GAMEDIRECTORY.getFile(plain.values[i][0].toInt());
      if (file && outsideCharacterFolders(file) && !RaceInfos::getRaceInfosForFileID(file->fileDataId(), ri))
        creature = file;
    }
    if (!creature)
      skip("a model no race uses loads as a creature", "no such creature model is installed on this computer");
    else
    {
      frame->LoadModel(creature);
      settle();
      WoWModel * m = canvasModel();
      check(QString("a model no race uses (%1) loads as a creature").arg(creature->fullname()),
            m && m->gamefile == creature && m->modelType != MT_CHAR && !m->charModelDetails.isChar, describe(m));
    }
  }

  // ---- 3. The Human male's options and choices ---------------------------------------------------------
  const int humanFile = RaceInfos::getFileIDForRaceSex(1, 0);
  GameFile * humanModel = humanFile > 0 ? GAMEDIRECTORY.getFile(humanFile) : nullptr;
  const auto loadHuman = [frame, humanModel]() -> WoWModel * {
    if (!humanModel)
      return nullptr;
    frame->LoadModel(humanModel, 1, 0);
    for (int i = 0; i < 20; i++)
      wxTheApp->Yield(true);
    WoWModel * m = frame->canvas ? const_cast<WoWModel *>(frame->canvas->model()) : nullptr;
    return (m && m->infos.raceID == 1) ? m : nullptr;
  };
  // The options shown, in order, as "name valid/total".
  const auto describeOptions = [](CharDetails & cd, const std::vector<OptionExpect> & expected, bool * same) {
    const std::vector<uint> options = cd.getCustomizationOptions();
    QStringList got;
    *same = options.size() == expected.size();
    for (size_t i = 0; i < options.size(); i++)
    {
      sqlResult n = GAMEDATABASE.sqlQuery(QString("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = %1").arg(options[i]));
      sqlResult t = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1").arg(options[i]));
      const QString name = (n.valid && !n.values.empty()) ? n.values[0][0] : QString("?");
      const int valid = (int)cd.getCustomizationChoices(options[i]).size();
      const int total = (t.valid && !t.values.empty()) ? t.values[0][0].toInt() : -1;
      got << QString("%1 %2/%3").arg(name).arg(valid).arg(total);
      if (i < expected.size())
        *same = *same && name == QLatin1String(expected[i].name) && valid == expected[i].valid && total == expected[i].total;
    }
    return got.join(", ");
  };
  const auto describeExpected = [](const std::vector<OptionExpect> & expected) {
    QStringList want;
    for (const OptionExpect & o : expected)
      want << QString("%1 %2/%3").arg(o.name).arg(o.valid).arg(o.total);
    return want.join(", ");
  };
  WoWModel * human = loadHuman();
  if (!human && !humanModel)
    skip("Human male", "its model or race row is not installed on this computer");
  else if (!human)
    check("Human male: its model loads as race 1", false, humanModel->fullname());
  else
  {
    CharDetails & cd = human->cd;
    bool same = false;
    const QString got = describeOptions(cd, e->humanMale, &same);
    if (classes == 0 && notInstalled("ChrClasses"))
      skip("Human male: options and valid choices", "the class context needs ChrClasses: " + got);
    else
      check("Human male: options in client order, valid choices of each", same, "got " + got + " | expected " + describeExpected(e->humanMale));

    // The face carries a classic model's eyes; the eye texture slot carries a high-resolution model's.
    if (profile.product == QLatin1String("wow_classic"))
    {
      bool lower = false, upper = false, base = false;
      for (const auto & t : cd.textures)
      {
        base = base || (t.fileId == 120191 && t.type == 1);
        lower = lower || (t.fileId == 119941 && t.type == 1 && t.region == 10);
        upper = upper || (t.fileId == 120061 && t.type == 1 && t.region == 9);
      }
      check("Human male: base skin humanmaleskin00_00 (120191), and the default Face (17172) composes facelower00_00 (119941) into section 10 and faceupper00_00 (120061) into 9",
            cd.get(10) == 17172 && base && lower && upper, QString("Face choice %1").arg(cd.get(10)));
      const QImage & body = human->tex.lastImage();
      check("Human male: the body composite is the layout's 1024x1024", body.width() == 1024 && body.height() == 1024,
            QString("%1x%2").arg(body.width()).arg(body.height()));
      cd.set(13, 17206); // Facial Hair "Bearded": geosets 101, 201, 301
      for (int i = 0; i < 20; i++)
        wxTheApp->Yield(true);
      QStringList shown;
      int beard = 0;
      for (size_t i = 0; i < (std::min)(human->ownGeosetCount(), human->geosets.size()); i++)
        if (human->geosets[i] && human->geosets[i]->display)
        {
          shown << QString::number(human->geosets[i]->id);
          beard += (human->geosets[i]->id == 101 || human->geosets[i]->id == 201 || human->geosets[i]->id == 301) ? 1 : 0;
        }
      check("Human male: Facial Hair Bearded shows geosets 101, 201 and 301", beard == 3, "visible " + shown.join(' '));
    }
    if (profile.product == QLatin1String("wow_classic_beta"))
    {
      const QImage & eyes = human->eyeCompositeImage();
      bool eyeLayer = false;
      for (const auto & t : cd.textures)
        eyeLayer = eyeLayer || t.type == 19;
      check("Human male: an Eye Color is current, its type-19 layer is composed into the 256x128 eye texture",
            cd.get(463) == 4126 && eyeLayer && eyes.width() == 256 && eyes.height() == 128,
            QString("Eye Color %1, eyes %2x%3").arg(cd.get(463)).arg(eyes.width()).arg(eyes.height()));
      check("Human male: no Demon Hunter class context in a client without Demon Hunters", !cd.clientHasDemonHunters(), QString());
    }

    // ---- 4. A client whose ChrClasses is not installed (simulated: its rows removed inside a transaction that is
    // rolled back, so nothing on disk changes). The class-limited choices must be left out as "class unknown" --
    // not judged against Retail's classes 1-15, and not shown as if every class had them.
    if (classes != 0 && GAMEDATABASE.sqlQuery("BEGIN").valid)
    {
      GAMEDATABASE.sqlQuery("DELETE FROM ChrClasses");
      human->cd.reset(human, true); // read the options again, as a load of this client would
      for (int i = 0; i < 20; i++)
        wxTheApp->Yield(true);
      WoWModel * without = human;
      {
        CharDetails & cdw = without->cd;
        const std::map<uint, uint> none;
        bool same2 = false;
        const QString got2 = describeOptions(cdw, e->humanMaleWithoutClasses, &same2);
        check("without ChrClasses: the class context is unknown (no class, no Demon Hunter)",
              CharDetails::clientClassMask() == 0 && !cdw.clientHasDemonHunters(), QString());
        check("without ChrClasses: Req 144 is 'class unknown' -- not 'class' (Retail's 0x77DF) and not met",
              cdw.requirement(144) == nullptr || cdw.evaluateRequirement(144, none) == UNKNOWN,
              cdw.requirement(144) ? resultName(cdw.evaluateRequirement(144, none)) : QString("Req 144 unused by this model"));
        check("without ChrClasses: the options left are those no class limit hides, choices limited only by 'every class' stay",
              same2, "got " + got2 + " | expected " + describeExpected(e->humanMaleWithoutClasses));
      }
      GAMEDATABASE.sqlQuery("ROLLBACK");
      human->cd.reset(human, true);
      for (int i = 0; i < 20; i++)
        wxTheApp->Yield(true);
      WoWModel * again = human;
      bool same3 = false;
      const QString got3 = again ? describeOptions(again->cd, e->humanMale, &same3) : QString("(no model)");
      check("after the rollback: ChrClasses and the Human male's options are back", CharDetails::clientClassMask() == classes && same3, got3);
    }
  }

  // ---- 5. Storage: a FileDataID listed twice in the build's ROOT ---------------------------------------
  // The rule on its own, every combination, then real files: each one's content read through the viewer and
  // its MD5 (the content key) compared with the version that must be selected.
  check("storage rule: kept installed, later installed -> keep", !CascWowRootReplacesKeptEntry(true, true), QString());
  check("storage rule: kept installed, later not installed -> keep", !CascWowRootReplacesKeptEntry(true, false), QString());
  check("storage rule: kept not installed, later installed -> replace", CascWowRootReplacesKeptEntry(false, true), QString());
  check("storage rule: kept not installed, later not installed -> keep (the first, as before)", !CascWowRootReplacesKeptEntry(false, false), QString());
  for (const StorageExpect & s : e->storage)
  {
    GameFile * f = GAMEDIRECTORY.getFile(s.fileID);
    QString detail = f ? f->fullname() : QString("(not listed)");
    QString contentKey;
    if (f && f->open())
    {
      if (f->readComplete() && f->getSize() > 0)
        contentKey = QString::fromLatin1(QCryptographicHash::hash(QByteArray((const char *)f->getBuffer(), (int)f->getSize()), QCryptographicHash::Md5).toHex());
      detail += QString(", %1 bytes").arg(f->getSize());
      f->close();
    }
    detail += ", content key " + (contentKey.isEmpty() ? QString("(nothing read)") : contentKey);
    check(QString("storage: %1 -- %2").arg(s.fileID).arg(QString::fromLatin1(s.what)), contentKey == QLatin1String(s.contentKey), detail);
  }

  // ---- 6. Humanoid NPCs wear their own stored appearance ----------------------------------------------
  checkNpcAppearance(frame, check, skip);

  // ---- 7. The geoset groups an NPC's shirt, chest and legs share ---------------------------------------
  checkEquipmentGeosets(frame, check, skip);

  // ---- 8. A character's model of the other generation (Classic | HD) -----------------------------------
  checkModelVariants(frame, check, skip);

  // ---- 9. The item list and the equipment dialogs ------------------------------------------------------
  checkItemCatalogue(frame, check, skip);

  // ---- 10. Playable Races and NPC Races ---------------------------------------------------------------
  checkRaceCategories(frame, check, skip);

  // ---- 11. The geosets customization choices show ----------------------------------------------------
  checkCustomizationGeosets(frame, check, skip);

  LOG_INFO << QString("[customization-test] RESULT: %1 (%2 passed, %3 failed, %4 skipped)").arg(failed == 0 ? "PASS" : "FAIL").arg(passed).arg(failed).arg(skipped);
  return failed;
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
  if (GAMEDIRECTORY.clientProfile().isClassicFamily())
    return doHeadlessClassicCustomizationTest(frame);
  LOG_INFO << "[customization-test]" << GAMEDIRECTORY.clientProfile().describe();

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
    const unsigned int clientClasses = CharDetails::clientClassMask();
    check("rule: the ordinary classes are 12.1.0's ChrClasses 1-15 but Death Knight and Demon Hunter",
          clientClasses == 0x7FFF && CharDetails::ordinaryClassMask(clientClasses) == 0x77DF,
          QString("classes 0x%1, ordinary 0x%2").arg(clientClasses, 0, 16).arg(CharDetails::ordinaryClassMask(clientClasses), 0, 16));
    QString classDetail;
    bool classesOk = true;
    for (const auto & c : classes)
    {
      const bool ordinary = CharDetails::classRequirement(classMask(c.id), false, clientClasses) == CharDetails::REQUIREMENT_MET;
      const bool demonHunter = CharDetails::classRequirement(classMask(c.id), true, clientClasses) == CharDetails::REQUIREMENT_MET;
      classesOk = classesOk && ordinary == c.ordinary && demonHunter == c.demonHunter;
      classDetail += QString(" Req %1 ClassMask %2: ordinary %3 DH %4").arg(c.id).arg(classMask(c.id)).arg(ordinary ? 1 : 0).arg(demonHunter ? 1 : 0);
    }
    check("rule: ClassMask bit classID-1 against the class context (ordinary = every class but DK and DH; DH checkbox)", classesOk, classDetail.trimmed());
    // Without ChrClasses there is no class context: a class-limited requirement is "class unknown", never judged
    // against an assumed class list; no limit, or every class, is met whatever the classes are.
    const bool unknownOk = CharDetails::classRequirement(classMask(144), false, 0) == CharDetails::REQUIREMENT_CLASS_UNKNOWN &&
                           CharDetails::classRequirement(classMask(143), true, 0) == CharDetails::REQUIREMENT_CLASS_UNKNOWN &&
                           CharDetails::classRequirement(classMask(142), false, 0) == CharDetails::REQUIREMENT_CLASS_UNKNOWN &&
                           CharDetails::classRequirement(classMask(141), false, 0) == CharDetails::REQUIREMENT_MET &&
                           CharDetails::classRequirement(classMask(4103), false, 0) == CharDetails::REQUIREMENT_MET;
    check("rule: no class context (ChrClasses not available): Req 144, 143, 142 unknown; Req 141 (every class) and 4103 (no limit) met",
          unknownOk, QString());
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

  // ---- 9. Humanoid NPCs wear their own stored appearance ----------------------------------------------
  int skipped = 0;
  const auto skip = [&skipped](const QString & what, const QString & why) {
    skipped++;
    LOG_INFO << QString("[customization-test] SKIP %1 -- %2").arg(what, why);
  };
  checkNpcAppearance(frame, check, skip);

  // ---- 10. The geoset groups an NPC's shirt, chest and legs share --------------------------------------
  checkEquipmentGeosets(frame, check, skip);

  // ---- 11. A character's model of the other generation (Classic | HD) ----------------------------------
  checkModelVariants(frame, check, skip);

  // ---- 12. The item list and the equipment dialogs -----------------------------------------------------
  checkItemCatalogue(frame, check, skip);

  // ---- 13. Playable Races and NPC Races --------------------------------------------------------------
  checkRaceCategories(frame, check, skip);

  // ---- 14. The geosets customization choices show ----------------------------------------------------
  checkCustomizationGeosets(frame, check, skip);

  LOG_INFO << QString("[customization-test] RESULT: %1 (%2 passed, %3 failed, %4 skipped)").arg(failed == 0 ? "PASS" : "FAIL").arg(passed)
                  .arg(failed).arg(skipped);
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
    ClientState::Result r = line.startsWith("fdid:", Qt::CaseInsensitive)
        ? ClientState::readByFileDataID(line.mid(5).toInt())
        : ClientState::readByPath(line);
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

// -mapcatalogtest <out.json> [-mapcatalogclients <product,product,...>] [-mapcatalogprobe <id|path,...>]
//                [-mapcatalogwdt <folder> <map id,map id,...>]
// THE WORLD MAP CATALOGUE TEST (docs/world-viewer/02-map-catalog.md), on the real clients of this computer. The clients
// listed are loaded in turn in this one process -- the first by the usual headless pick (the first product listed,
// unless -product names one), each next one as a switch of client -- and for each: the catalogue the load built,
// every map's WDT inspected through the client's storage, the probe files inspected the same way, and the checks
// below. With -mapcatalogwdt, the WDTs of the maps listed are read through each client's catalogue (readWdt) and
// written to <folder>/<product>_<map id>.wdt, with <product>_<map id>.json (the SHA-256 read and the form Map.db2
// states), for the player's WDT parser to take over in a test of the runtime contract (Tools/UnityRendererProject/
// Tests/MapContractCheck.cs, docs/world-viewer/02-map-catalog.md section 14). Lines
// "[mapcatalog-test] PASS|FAIL <what> -- <detail>" go to the log, ending with
// "[mapcatalog-test] RESULT: PASS|FAIL (<passed> passed, <failed> failed)"; everything goes into <out.json>, which
// Tools/world-audit/compare_map_catalog.py compares with the world audit's results.
static int doHeadlessMapCatalogTest(ModelViewer * frame, const QString & outPath, QStringList clients,
                                    const QStringList & probes, const std::shared_ptr<wow::WorldMapCatalog> & beforeLoad,
                                    double firstLoadMs, const QString & wdtFolder, const QStringList & wdtMaps)
{
  int passed = 0, failed = 0;
  const auto check = [&passed, &failed](const QString & what, bool ok, const QString & detail) {
    ok ? passed++ : failed++;
    const QString line = QString("[mapcatalog-test] %1 %2%3").arg(ok ? "PASS" : "FAIL").arg(what)
                           .arg(detail.isEmpty() ? QString() : QString(" -- ") + detail);
    if (ok)
      LOG_INFO << line;
    else
      LOG_ERROR << line;
  };

  QJsonObject out;
  // Asked for before any client was loaded: a catalogue that says so, and refuses to read anything.
  {
    const bool noClient = beforeLoad && beforeLoad->status() == wow::WorldMapCatalog::Status::NoClient;
    check("before any client: the catalogue reports no client", noClient,
          beforeLoad ? wow::WorldMapCatalog::statusName(beforeLoad->status()) : QString("none built"));
    QByteArray bytes;
    QString why;
    wow::WdtReference azeroth;
    azeroth.kind = wow::WdtReference::Kind::FileDataId;
    azeroth.fileDataId = 775971;
    const bool refused = beforeLoad && !beforeLoad->inspect(0) && beforeLoad->inspectAll() == 0 &&
                         !beforeLoad->readWdt(0, bytes, why) && bytes.isEmpty() &&
                         beforeLoad->inspectFile(azeroth).state == wow::WdtAvailability::NotChecked;
    check("before any client: every read is refused", refused, beforeLoad ? beforeLoad->lastRefusal() : QString());
    out["beforeLoad"] = beforeLoad ? beforeLoad->toJson() : QJsonObject();
  }

  if (clients.isEmpty())
    clients << GAMEDIRECTORY.clientProfile().product;
  QJsonArray steps;
  std::vector<std::shared_ptr<wow::WorldMapCatalog>> earlier; // every step's catalogue, kept past every later switch
  QSet<int> wdtMapsFound;
  for (int i = 0; i < clients.size(); i++)
  {
    const QString product = clients[i];
    QJsonObject step;
    step["product"] = product;
    double loadMs = firstLoadMs;
    if (i > 0)
    {
      // A switch of client, as the chooser makes it: the next product's config of the same installation.
      const std::vector<core::GameConfig> configs = GAMEDIRECTORY.configsFound();
      const core::GameConfig * config = nullptr;
      for (const core::GameConfig & c : configs)
        if (c.product == product)
          config = &c;
      if (!config)
      {
        check(QString("%1: the installation has the client").arg(product), false, "no such product in .build.info");
        steps.append(step);
        continue;
      }
      QElapsedTimer t;
      t.start();
      const bool loaded = frame->LoadWoW(config);
      loadMs = t.nsecsElapsed() / 1e6;
      check(QString("%1: loads after %2").arg(product, clients[i - 1]), loaded, QString("%1 ms").arg(loadMs, 0, 'f', 0));
      if (!loaded)
      {
        steps.append(step);
        continue;
      }
    }
    step["loadMs"] = loadMs;

    const std::shared_ptr<wow::WorldMapCatalog> catalog = frame->mapCatalog();
    check(QString("%1: the load built a catalogue").arg(product), catalog != nullptr, QString());
    if (!catalog)
    {
      steps.append(step);
      continue;
    }
    const wow::MapCatalogClient & who = catalog->client();
    check(QString("%1: the catalogue is the loaded client's").arg(product),
          who.product == product && who.product == GAMEDIRECTORY.clientProfile().product && who.build == GAMEDIRECTORY.version() &&
            catalog->belongsToActiveClient(),
          QString("%1 %2, storage %3").arg(who.product, who.build).arg(who.storageGeneration));
    check(QString("%1: the catalogue is ready").arg(product), catalog->status() == wow::WorldMapCatalog::Status::Ready,
          QString("%1%2").arg(wow::WorldMapCatalog::statusName(catalog->status()),
                              catalog->statusDetail().isEmpty() ? QString() : QString(": ") + catalog->statusDetail()));
    check(QString("%1: Map.db2 lists maps").arg(product), !catalog->maps().empty(),
          QString("%1 maps").arg(catalog->maps().size()));
    // The layout the catalogue was read with is the one in the client's own Map.db2 header, read again here.
    const QString fileLayout = wow::WoWDatabase::fileLayoutHash("Map");
    check(QString("%1: the catalogue's Map.db2 layout is the file's").arg(product),
          !fileLayout.isEmpty() && catalog->mapLayout() == fileLayout,
          QString("catalogue %1, file %2").arg(catalog->mapLayout(), fileLayout));

    // As the load left it: nothing read from storage yet (inspection is lazy), only maps without a reference settled.
    int readAtLoad = 0;
    for (const wow::MapEntry & e : catalog->maps())
      if (e.inspection.state != (e.wdt.kind == wow::WdtReference::Kind::Unresolved ? wow::WdtAvailability::Unsupported
                                                                                   : wow::WdtAvailability::NotChecked))
        readAtLoad++;
    check(QString("%1: loading inspected no WDT").arg(product), readAtLoad == 0,
          QString("%1 maps not as the load leaves them").arg(readAtLoad));
    step["retainedBytesAtLoad"] = (double)catalog->approximateBytes();

    // Every map a map refers to (ParentMapID, WorldCopyOfMapID, LocationMapID) is -1 or a map of this catalogue.
    QSet<int> ids;
    for (const wow::MapEntry & e : catalog->maps())
      ids.insert(e.id);
    QStringList dangling;
    for (const wow::MapEntry & e : catalog->maps())
      for (const wow::MapCode * c : {&e.parentMapId, &e.worldCopyOfMapId, &e.locationMapId})
        if (c->present && c->value != -1 && !ids.contains(c->value))
          dangling << QString("%1->%2").arg(e.id).arg(c->value);
    check(QString("%1: every map reference is -1 or a listed map").arg(product), dangling.isEmpty(), dangling.join(", "));

    // One WDT mechanism per client, from its own layout: never a path in a FileDataID client, never a FileDataID in a
    // path client, and every map without a reference says why.
    const bool byId = catalog->wdtRule() == wow::WorldMapCatalog::WdtRule::ByFileDataId;
    int wrongKind = 0, unexplained = 0;
    for (const wow::MapEntry & e : catalog->maps())
    {
      if ((byId && e.wdt.kind == wow::WdtReference::Kind::Path) || (!byId && e.wdt.kind == wow::WdtReference::Kind::FileDataId))
        wrongKind++;
      if (e.wdt.kind == wow::WdtReference::Kind::Unresolved && e.wdt.why.isEmpty())
        unexplained++;
    }
    check(QString("%1: every reference follows the client's rule (%2)").arg(product, byId ? "by FileDataID" : "by directory path"),
          wrongKind == 0 && unexplained == 0,
          QString("%1 of another kind, %2 unresolved without a reason").arg(wrongKind).arg(unexplained));

    // Every earlier step's catalogue, kept past the switches: none belongs to the loaded client, and none reads anything
    // (the same product loaded again included: a new storage is a new client).
    for (size_t k = 0; k < earlier.size(); k++)
    {
      const std::shared_ptr<wow::WorldMapCatalog> & old = earlier[k];
      const QString which = QString("step %1's catalogue (%2)").arg(k + 1).arg(old->client().product);
      check(QString("%1: %2 no longer belongs to the loaded client").arg(product, which), !old->belongsToActiveClient(), QString());
      const QJsonValue before = old->toJson()["maps"];
      const int someId = old->maps().empty() ? 0 : old->maps().front().id;
      const bool refused = !old->inspect(someId) && old->inspectAll() == 0;
      QByteArray staleBytes;
      QString staleWhy;
      const bool readRefused = !old->readWdt(someId, staleBytes, staleWhy) && staleBytes.isEmpty();
      wow::WdtReference anyFile;
      anyFile.kind = wow::WdtReference::Kind::FileDataId;
      anyFile.fileDataId = 1349477; // Map.db2 in every audited client
      const bool fileRefused = old->inspectFile(anyFile).state == wow::WdtAvailability::NotChecked;
      check(QString("%1: %2 refuses to read the storage").arg(product, which),
            refused && readRefused && fileRefused && old->toJson()["maps"] == before, old->lastRefusal());
      check(QString("%1: %2 is not the new catalogue").arg(product, which),
            old.get() != catalog.get() && old->client().storageGeneration != who.storageGeneration, QString());
    }

    // Every map's WDT through the client's storage: whether it is listed, on this computer, and readable. Timed
    // without hashing (the viewer's own use), then again with it (what the comparison needs).
    QElapsedTimer t;
    t.start();
    const size_t inspectedPlain = catalog->inspectAll();
    const double inspectPlainMs = t.nsecsElapsed() / 1e6;
    step["inspectAllNoHashMs"] = inspectPlainMs;
    catalog->setHashing(true);
    t.restart();
    const size_t inspected = catalog->inspectAll();
    const double inspectMs = t.nsecsElapsed() / 1e6;
    check(QString("%1: inspection without hashing covered every map").arg(product), inspectedPlain == catalog->maps().size(),
          QString("%1 maps in %2 ms").arg(inspectedPlain).arg(inspectPlainMs, 0, 'f', 1));
    QJsonObject counts;
    for (const wow::MapEntry & e : catalog->maps())
    {
      const QString s = wow::WorldMapCatalog::availabilityName(e.inspection.state);
      counts[s] = counts.value(s).toInt() + 1;
    }
    const int notChecked = counts.value(wow::WorldMapCatalog::availabilityName(wow::WdtAvailability::NotChecked)).toInt();
    check(QString("%1: every map's WDT was inspected").arg(product), inspected == catalog->maps().size() && notChecked == 0,
          QString("%1 maps in %2 ms: %3").arg(inspected).arg(inspectMs, 0, 'f', 1)
            .arg(QString::fromUtf8(QJsonDocument(counts).toJson(QJsonDocument::Compact))));
    step["inspectAllMs"] = inspectMs;
    step["availability"] = counts;

    // Probe files: the same storage inspection on files named here (a tile part known not to be local, a table with
    // encrypted sections...), by FileDataID or by path.
    QJsonArray probeResults;
    for (const QString & probe : probes)
    {
      wow::WdtReference ref;
      bool numeric = false;
      const int id = probe.toInt(&numeric);
      if (numeric)
      {
        ref.kind = wow::WdtReference::Kind::FileDataId;
        ref.fileDataId = id;
      }
      else
      {
        ref.kind = wow::WdtReference::Kind::Path;
        ref.path = probe;
      }
      const wow::WdtInspection r = catalog->inspectFile(ref);
      QJsonObject p;
      p["probe"] = probe;
      p["state"] = wow::WorldMapCatalog::availabilityName(r.state);
      p["detail"] = r.detail;
      p["storageFileDataId"] = r.storageFileDataId;
      p["bytes"] = (double)r.bytes;
      p["sha256"] = r.sha256;
      probeResults.append(p);
      LOG_INFO << QString("[mapcatalog-test] probe %1 %2: %3%4").arg(product, probe, wow::WorldMapCatalog::availabilityName(r.state),
                                                                   r.detail.isEmpty() ? QString() : QString(" (") + r.detail + ")");
    }
    step["probes"] = probeResults;

    // The WDTs the runtime-contract test parses next, read the way a runtime reads them: through the catalogue.
    if (!wdtFolder.isEmpty())
    {
      QJsonArray written;
      QDir().mkpath(wdtFolder);
      for (const QString & idText : wdtMaps)
      {
        const int mapId = idText.toInt();
        const wow::MapEntry * entry = catalog->find(mapId);
        if (!entry)
          continue; // not a map of this client
        wdtMapsFound.insert(mapId);
        QJsonObject w;
        w["id"] = mapId;
        QByteArray bytes;
        QString why;
        if (catalog->readWdt(mapId, bytes, why))
        {
          const QString file = QString("%1/%2_%3.wdt").arg(wdtFolder, product).arg(mapId);
          QFile f(file);
          const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
          f.close();
          w["file"] = file;
          w["bytes"] = bytes.size();
          w["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
          // What the player-side check compares its parse with: the bytes it was given, and the form Map.db2 states.
          static const char * const forms[] = {"unknown", "tile grid", "WMO only", "no WDT"};
          QJsonObject side;
          side["id"] = mapId;
          side["product"] = product;
          side["sha256"] = w["sha256"];
          side["statedWdtForm"] = forms[(int)entry->statedWdtForm()];
          QFile sf(QString("%1/%2_%3.json").arg(wdtFolder, product).arg(mapId));
          const bool sideOk = sf.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                              sf.write(QJsonDocument(side).toJson(QJsonDocument::Compact)) > 0;
          sf.close();
          check(QString("%1: map %2's WDT read through the catalogue").arg(product).arg(mapId), ok && sideOk,
                QString("%1 bytes").arg(bytes.size()));
        }
        else
        {
          w["why"] = why;
          check(QString("%1: map %2's WDT read through the catalogue").arg(product).arg(mapId), false, why);
        }
        written.append(w);
      }
      step["wdtWritten"] = written;
    }
    step["catalog"] = catalog->toJson();
    steps.append(step);
    earlier.push_back(catalog);
  }
  out["steps"] = steps;
  // The binaries that produced these results, so a comparison can name the build it checked.
  QJsonObject binaries;
  for (const QString & file : {QCoreApplication::applicationFilePath(), QCoreApplication::applicationDirPath() + "/wow.dll",
                               QCoreApplication::applicationDirPath() + "/core.dll"})
  {
    QFile bin(file);
    if (bin.open(QIODevice::ReadOnly))
      binaries[QFileInfo(file).fileName()] =
        QString::fromLatin1(QCryptographicHash::hash(bin.readAll(), QCryptographicHash::Sha256).toHex());
  }
  out["binaries"] = binaries;
  if (!wdtFolder.isEmpty())
    for (const QString & idText : wdtMaps)
      check(QString("map %1 is in one of the clients loaded").arg(idText), wdtMapsFound.contains(idText.toInt()), QString());
  out["passed"] = passed;
  out["failed"] = failed;

  QFile f(outPath);
  const bool written = f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                       f.write(QJsonDocument(out).toJson(QJsonDocument::Indented)) > 0;
  f.close();
  check("the results were written", written, outPath);
  LOG_INFO << QString("[mapcatalog-test] RESULT: %1 (%2 passed, %3 failed)").arg(failed == 0 ? "PASS" : "FAIL").arg(passed).arg(failed);
  std::printf("WMV: mapcatalog test %s (%d passed, %d failed)\n", failed == 0 ? "PASS" : "FAIL", passed, failed);
  std::fflush(stdout);
  return failed;
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

  const TextureID id = TEXTUREMANAGER.add(f);
  int width = 0, height = 0;
  std::vector<unsigned char> pixels;
  if (id == 0 || !core::readTexturePixels(id, width, height, pixels) || width <= 0 || height <= 0)
  {
    std::printf("WMVDUMPTEX: ERROR texture %d failed to load\n", fileDataId); std::fflush(stdout);
    return;
  }

  QImage img(pixels.data(), width, height, QImage::Format_ARGB32); // BGRA bytes
  const bool saved = img.save(outPath);
  TEXTUREMANAGER.del(id);

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

// Milliseconds since this process was created.
static long long processUptimeMs()
{
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return -1;
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  const auto ticks = [](const FILETIME & t) { return ((long long)t.dwHighDateTime << 32) | t.dwLowDateTime; };
  return (ticks(now) - ticks(created)) / 10000; // 100 ns units
#else
  static const auto start = std::chrono::steady_clock::now(); // (first asked at start-up)
  return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
#endif
}

// A batch run that loads something and has no other job ends here: a picture of the viewport -- the -screenshot path
// when one was given, else ss_<model>.png in the home folder -- drawn offscreen at 1920 x 1080, as the OpenGL
// viewport's batch runs once wrote one.
static QString g_headlessShotPath;
static void logNoHeadlessScreenshot()
{
  ModelViewer * frame = g_modelViewer;
  if (!frame || !frame->canvas)
    return;
  QString path = g_headlessShotPath;
  if (path.isEmpty())
  {
    const WoWModel * m = frame->canvas->model();
    const QString name = m ? QFileInfo(const_cast<WoWModel *>(m)->name()).baseName()
                           : frame->canvas->wmo ? QFileInfo(frame->canvas->wmo->itemName()).baseName() : QString("viewport");
    path = QDir::current().absoluteFilePath("ss_" + name + ".png");
  }
  // Ticked once, so the model's pose and particles are those of a drawn frame.
  frame->canvas->tick();
  const bool ok = frame->canvas->Screenshot(wxString(path.toStdWString()), 1920, 1080);
  LOG_INFO << "Headless run done:" << (ok ? "viewport written to" : "the viewport could not be written to") << path;
}

bool WowModelViewApp::OnInit()
{
  // A restart (ModelViewer::Relaunch): wait for the instance that started this one to have exited, so it
  // has saved its layout and settings and let go of the log before this one reads and opens them.
  for (int ai = 1; ai + 1 < argc; ai++)
    if (wxString(argv[ai]) == wxT("-waitpid"))
    {
      unsigned long pid = 0;
#ifdef _WIN32
      if (wxString(argv[ai + 1]).ToULong(&pid) && pid != 0)
        if (HANDLE previous = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid))
        {
          // Only the instance that started this one: a process started after this one has merely been
          // given its number again.
          FILETIME created, mine, unused;
          const bool older = ::GetProcessTimes(previous, &created, &unused, &unused, &unused) &&
                             ::GetProcessTimes(::GetCurrentProcess(), &mine, &unused, &unused, &unused) &&
                             ::CompareFileTime(&created, &mine) < 0;
          if (older)
            ::WaitForSingleObject(previous, 15000);
          ::CloseHandle(previous);
        }
#else
      // The instance that started this one is this one's parent until it exits: wait (15 s at most) for it to go.
      if (wxString(argv[ai + 1]).ToULong(&pid) && pid != 0 && (pid_t)pid == getppid())
        for (int waited = 0; waited < 1500 && ::kill((pid_t)pid, 0) == 0 && getppid() == (pid_t)pid; waited++)
          usleep(10000);
#endif
      break;
    }

  // Held while the viewer runs, so its installer and uninstaller ask to close it first (AppMutex in
  // installer/WoWModelViewer.iss). Never closed: Windows lets go of it when the process ends.
#ifdef _WIN32
  ::CreateMutexW(nullptr, FALSE, L"WoWModelViewer-B7E9F3A2-1C4D-4E8A-9F6B-2A3C5D7E9F10");
#endif

  // What the viewer reads and writes beside itself (plugins\, games\, dbd\, the listfile and keys, dbcache\, the log)
  // is found relative to the working directory: the folder of the exe, however it was started (a shortcut with
  // another "Start in", a console in another folder). A file named on the command line is still found from the folder
  // it was started in (launchPath).
  // On macOS and Linux that folder is the user's home folder for the viewer (AppPaths: the program's own data is linked
  // into it), since an app bundle or a system install is not written to.
  const wxString launchDir = wxGetCwd();
#ifdef _WIN32
  wxSetWorkingDirectory(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
#else
  {
    QString homeProblem;
    if (!AppPaths::prepareHome(homeProblem))
      std::fprintf(stderr, "WMV: the home folder could not be prepared (%s)\n", qPrintable(homeProblem));
  }
#endif
  auto launchPath = [&launchDir](const wxString & arg) {
    wxFileName fn(arg);
    if (fn.IsRelative())
      fn.MakeAbsolute(launchDir);
    return QString::fromWCharArray(fn.GetFullPath().wc_str());
  };

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
        a == "-mapcatalogtest" || a.endsWith(".chr"))
    {
      earlyHeadless = true;
      break;
    }
  }

  // THE THEME, before the first window (the splash): wxWidgets' dark mode is decided once, for the
  // whole run, and only before any window exists. So the appearance is read from Config.ini here
  // (LoadSettings reads the rest later), and the run is fixed light or dark (UiStyle::fixSessionTheme);
  // changing it later asks for a restart.
  {
    // (The working directory is the home folder by now: the exe's on Windows, AppPaths::homeDir elsewhere.)
    const wxString settingsFile = wxGetCwd() + SLASH + wxT("userSettings") + SLASH + wxT("Config.ini");
    QSettings config(QString::fromWCharArray(settingsFile.wc_str()), QSettings::IniFormat);
    UiStyle::setThemePreference(UiStyle::themeFromSetting(config.value("Settings/Appearance", 0).toInt()));
#ifdef __WXMSW__
    // wxWidgets has dark mode from Windows 10 1903 (build 18362); its own msw.dark-mode option (also from
    // the environment) may have turned it on already, before this: then the run is dark whatever is asked.
    const bool possible = wxCheckOsVersion(10, 0, 18362);
    const int option = wxSystemOptions::GetOptionInt(wxT("msw.dark-mode"));
    const bool forced = possible && (option > 1 || (option == 1 && UiStyle::systemAppsDark()));
    bool dark = false;
    if (possible && (forced || UiStyle::darkWanted()))
    {
      wxDarkModeSettings * settings = UiStyle::newDarkModeSettings();
      dark = MSWEnableDarkMode(DarkMode_Always, settings);   // owns the settings when it succeeds
      if (!dark)
        delete settings;
    }
    // Locked (no restart offered): when a restart would give the same, or when wx's own option decides
    // (with option 1 wx follows Windows' app mode itself).
    UiStyle::fixSessionTheme(dark, option != 0 || !possible || (UiStyle::darkWanted() && !dark));
#else
    // macOS and GTK draw the native controls in the system's appearance, which the app can set for itself.
    const bool dark = UiStyle::darkWanted();
#if wxCHECK_VERSION(3, 3, 0)
    SetAppearance(dark ? Appearance::Dark : Appearance::Light);
#endif
    UiStyle::fixSessionTheme(dark, false);
#endif
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


  // userSettings in the home folder (the working directory: the exe's folder on Windows, AppPaths::homeDir elsewhere).
  wxString userPath = wxGetCwd() + SLASH + wxT("userSettings");
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
  LOG_INFO << "[theme] appearance" << UiStyle::themeToSetting(UiStyle::themePreference()) << "(0 System, 1 Light, 2 Dark):"
           << (UiStyle::darkActive() ? "dark run (wxWidgets dark mode)" : UiStyle::darkWanted() ? "light run (no dark mode on this Windows)" : "light run")
           << (UiStyle::highContrastOn() ? ", high contrast" : "");
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

  if (frame->canvas && frame->lightControl)
    frame->lightControl->UpdateGL();
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
  QString fbxEquipmentPath; // -fbxequipment <snapshot.chr>: restore equipment after -mo/-npc
  QString mpqDataFolder;  // -mpq <DataFolder> [locale]: load a legacy MPQ client instead of CASC
  QString mpqLocale;      // optional locale for -mpq (auto-detected when empty)
  int dumpTexFileDataId = 0; QString dumpTexOutPath; // -dumptex <fileDataID> <out.png>: forensic-only
  bool matResTest = false;                           // -matrestest: replaceable-material checks
  bool customizationTest = false;                    // -customizationtest: character customization checks
  QString mapCatalogTestOut;                         // -mapcatalogtest <out.json>: the world map catalogue test
  QStringList mapCatalogClients, mapCatalogProbes;   // -mapcatalogclients p,p,... / -mapcatalogprobe id|path,...
  QString mapCatalogWdtFolder; QStringList mapCatalogWdtMaps; // -mapcatalogwdt <folder> <map id,...>
  QString m2InspectList, m2InspectOut;               // -m2inspect <list.txt> [out.csv]: forensic-only
  // Export content selection + clip list for the headless FBX export (the parent process passes
  // these so the child reproduces the user's exact options). Defaults: full content, no explicit
  // clips (the exporter falls back to none/first-N only if -fbxanim and no -fbxclips).
  int optMesh = 1, optSkel = 1, optSkin = 1, optAnim = 1;
  int optComponent = 0;   // -fbxcomponent : opt-in raw/node-based item-component export (UV2 + raw units + sidecar v2)
  bool optCustomizations = false, optEquipment = false;
  int itemSkinFileId = 0; // -itemskin <fileDataID> : re-bind an item/weapon's on-screen skin after -mo load
  QString fbxClipsArg;    // -fbxclips i,j,k : positions in the model's animation array
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
      // FileControl::SelectWMOFile, the same code picking a building in Browse runs (it switches to the Buildings
      // viewer). Composes with -unityipctest (the world-model check); see doHeadlessUnityIpcTest.
      if (i + 1 < argc) { i++; snapWmoArg = QString::fromWCharArray(argv[i]); }
    }
    else if (cmd == "-mpq") {
      // Headless legacy-MPQ load: "-mpq <DataFolder> [locale] -mo <path\model.m2>" opens a
      // Vanilla/TBC/WotLK MPQ install (instead of modern CASC) and loads the -mo model BY NAME
      // from the archive chain. Locale is optional (auto-detected) and, if given, is the token
      // right after the folder that does not start with '-'.
      if (i + 1 < argc) {
        i++;
        mpqDataFolder = launchPath(argv[i]);
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
    else if (cmd == "-mapcatalogtest") {
      // The world map catalogue on this computer's clients; see doHeadlessMapCatalogTest.
      if (i + 1 < argc) { i++; mapCatalogTestOut = launchPath(argv[i]); }
    }
    else if (cmd == "-mapcatalogclients") {
      // The clients the map catalogue test loads in turn, e.g. "wow,wow_classic,wow" (switches of client).
      if (i + 1 < argc) { i++; mapCatalogClients = QString::fromWCharArray(argv[i]).split(',', QString::SkipEmptyParts); }
    }
    else if (cmd == "-mapcatalogprobe") {
      // Files the map catalogue test inspects through each client's storage, by FileDataID or by path: a list, or
      // "@<file>" with one per line.
      if (i + 1 < argc)
      {
        const QString arg = QString::fromWCharArray(argv[++i]);
        if (arg.startsWith('@'))
        {
          QFile list(launchPath(wxString(arg.mid(1).toStdWString())));
          if (list.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QString & line : QString::fromUtf8(list.readAll()).split('\n'))
              if (!line.trimmed().isEmpty())
                mapCatalogProbes << line.trimmed();
        }
        else
          mapCatalogProbes = arg.split(',', QString::SkipEmptyParts);
      }
    }
    else if (cmd == "-mapcatalogwdt") {
      // Where the map catalogue test writes the WDTs of the maps listed, read through the catalogue (contract test).
      if (i + 2 < argc)
      {
        mapCatalogWdtFolder = launchPath(argv[++i]);
        mapCatalogWdtMaps = QString::fromWCharArray(argv[++i]).split(',', QString::SkipEmptyParts);
      }
    }
    else if (cmd == "-m2inspect") {
      // Forensic-only: "-m2inspect <list.txt> [out.csv]" reads each listed model's M2 header,
      // reports what visual systems it carries, and exits. See doHeadlessM2Inspect.
      if (i + 1 < argc) {
        i++;
        m2InspectList = launchPath(argv[i]);
        if (i + 1 < argc) {
          const QString nxt = QString::fromWCharArray(argv[i + 1]);
          if (!nxt.startsWith('-')) { i++; m2InspectOut = launchPath(argv[i]); }
        }
      }
    }
    else if (cmd == "-dumptex") {
      // Forensic-only: "-dumptex <fileDataID> <out.png>" loads game data, saves that texture
      // standalone (no item/equip/combiner context), and exits. See doHeadlessDumpTexture.
      if (i + 2 < argc) {
        dumpTexFileDataId = QString::fromWCharArray(argv[i + 1]).toInt();
        dumpTexOutPath = launchPath(argv[i + 2]);
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
        fbxExportPath = launchPath(argv[i]);
      }
    }
    else if (cmd == "-fbxequipment") {
      if (i + 1 < argc) { i++; fbxEquipmentPath = launchPath(argv[i]); }
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
        fbxInspectPath = launchPath(argv[i]);
      }
    }
    else if (cmd == "-fbxmesh")  { if (i + 1 < argc) { i++; optMesh = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxskel")  { if (i + 1 < argc) { i++; optSkel = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxskin")  { if (i + 1 < argc) { i++; optSkin = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxanim")  { if (i + 1 < argc) { i++; optAnim = QString::fromWCharArray(argv[i]).toInt(); } }
    else if (cmd == "-fbxclips") { if (i + 1 < argc) { i++; fbxClipsArg = QString::fromWCharArray(argv[i]); } }
    else if (cmd == "-fbxcomponent") { optComponent = 1; }
    else if (cmd == "-fbxcustomizations") { optCustomizations = true; }
    else if (cmd == "-fbxincludeequipment") { if (i + 1 < argc) optEquipment = QString::fromWCharArray(argv[++i]).toInt() != 0; }
    else if (cmd == "-screenshot") {
      // The headless run's picture of the viewport (logNoHeadlessScreenshot): its path.
      if (i + 1 < argc)
        g_headlessShotPath = launchPath(argv[++i]);
    }
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
    else if (cmd == "-product")  {
      // ...and to the same product: two products can list the same version (a PTR and its Retail).
      if (i + 1 < argc) { i++; qputenv("WMV_FORCE_PRODUCT", QString::fromWCharArray(argv[i]).toUtf8()); }
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
        snapCharPath = launchPath(argv[i]);
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
#else
  // A terminal already is the console: the log goes to it too.
  if (displayConsole)
    LOGGER.addChild(new WMVLog::LogOutputConsole());
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
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" || a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" || a == "-m2inspect" || a == "-matrestest" || a == "-customizationtest" || a == "-mpq" || a == "-item" || a == "-wmo" || a == "-mapcatalogtest" || a.endsWith(".chr"))
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

    // The map catalogue test takes the viewer's catalogue before any client is loaded (it must say there is none), and
    // loads the first client it lists unless -product names one.
    std::shared_ptr<wow::WorldMapCatalog> catalogBeforeLoad;
    if (!mapCatalogTestOut.isEmpty())
    {
      catalogBeforeLoad = frame->mapCatalog();
      if (!mapCatalogClients.isEmpty() && !qEnvironmentVariableIsSet("WMV_FORCE_PRODUCT"))
        qputenv("WMV_FORCE_PRODUCT", mapCatalogClients.first().toUtf8());
    }
    QElapsedTimer firstLoad;
    firstLoad.start();
    if (!mpqDataFolder.isEmpty())
      frame->LoadWoWFromMpq(mpqDataFolder, mpqLocale); // legacy MPQ client (Vanilla/TBC/WotLK)
    else
      {
        // auto-pick config, no prompt. A client that cannot be opened ends the run (it was opened for this run).
        if (!frame->LoadWoW())
        {
          LOG_ERROR << "Headless run: the World of Warcraft client could not be opened - nothing to do.";
          std::printf("WMV: client could not be opened\n");
          std::fflush(stdout);
          return false;
        }
      }

    if (!mapCatalogTestOut.isEmpty())
    {
      doHeadlessMapCatalogTest(frame, mapCatalogTestOut, mapCatalogClients, mapCatalogProbes, catalogBeforeLoad,
                               firstLoad.nsecsElapsed() / 1e6, mapCatalogWdtFolder, mapCatalogWdtMaps);
      return false; // checks done -> exit
    }

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
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }

    if (snapItemId > 0)
    {
      // The item path decides the display, the component model, its skin and its component
      // geoset state. Everything after this is the same tail -mo uses.
      frame->LoadItem((unsigned int)snapItemId);
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
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath, optCustomizations, optEquipment);
        return false; // headless export done -> exit
      }


      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }

    // Character (.chr) headless branch -- compose the saved character, then export.
    if (!snapCharPath.isEmpty())
    {
      frame->LoadChar(snapCharPath);
      if (!fbxExportPath.isEmpty())
      {
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath, optCustomizations, optEquipment);
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
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath, optCustomizations, optEquipment);
        return false;
      }
      logNoHeadlessScreenshot();
      return false; // headless run done -> exit
    }
  }
  else
  {
    // THE APPLICATION COMES UP AND WAITS. It opens as an empty, maximised viewer -- no client
    // read, no dialog asked, no question put to the user before they have even seen the program.
    // Loading a client is something they do when they want to, through
    // File > "Load World of Warcraft", which is the only thing that loads one now.
    //
    // Nothing here needs game data: the layout is layout, and the renderer talks to WMV rather
    // than to the client, so it can sit connected and idle until there is something to show.
    frame->ApplyViewerStartupLayout();
    // How long the start took, from the process's creation: the viewer is usable (its window up, the renderer
    // starting beside it) without any game data read.
    LOG_INFO << "[startup] the viewer is up" << processUptimeMs() << "ms after the process started (no client read;"
             << "the renderer is starting)";
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

void WowModelViewApp::CleanUp()
{
  wxApp::CleanUp(); // the windows (and what queries the database) first
  if (core::Game::instance().initDone())
    GAMEDATABASE.close();
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

  // Application locale info
  langID = config.value("Locale/LanguageID", 1).toInt();
  langName = config.value("Locale/LanguageName", "").toString().toStdWString();

  // Application settings
  gamePath = config.value("Settings/Path", "").toString().toStdWString();
  armoryPath = config.value("Settings/ArmoryPath", "").toString().toStdWString();
  customDirectoryPath = config.value("Settings/CustomDirPath", "").toString().toStdWString();
  customFilesConflictPolicy = config.value("Settings/CustomFilesConflictPolicy", 0).toInt();
  displayItemAndNPCId = config.value("Settings/displayItemAndNPCId", 0).toInt();
  // The appearance (View > Appearance; Settings/Appearance: 0 System, 1 Light, 2 Dark) is read before the
  // first window, in WowModelViewApp::OnInit: wxWidgets' dark mode is decided there.
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

  config.setValue("Locale/LanguageID", (qlonglong)langID);
  config.setValue("Locale/LanguageName", QString::fromWCharArray(langName.c_str()));

  config.setValue("Settings/Path", QString::fromWCharArray(gamePath.c_str()));
  config.setValue("Settings/ArmoryPath", QString::fromWCharArray(armoryPath.c_str()));
  config.setValue("Settings/CustomDirPath", QString::fromWCharArray(customDirectoryPath.c_str()));
  config.setValue("Settings/CustomFilesConflictPolicy", customFilesConflictPolicy);
  config.setValue("Settings/displayItemAndNPCId", displayItemAndNPCId);
  config.setValue("Settings/Appearance", UiStyle::themeToSetting(UiStyle::themePreference()));

  config.setValue("Tools/UnityRendererPath", QString::fromWCharArray(unityRendererPath.c_str()));

  config.setValue("Armory/ProxyURL", QString::fromStdString(GLOBALSETTINGS.armoryProxyURL()));
  config.sync();
}
