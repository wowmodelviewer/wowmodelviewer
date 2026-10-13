#include "app.h"

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
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>
#include <wx/msw/darkmode.h>
#include <wx/sysopt.h>

#include <windows.h>
#include <psapi.h>

#include "ExporterPlugin.h"
#include "Game.h"
#include "GameFolder.h" // core::GameConfig
#include "GlobalSettings.h"
#include "ViewportBackground.h"
#include "globalvars.h"
#include "LogStackWalker.h"
#include "PluginManager.h"
#include "UiStyle.h"
#include "UserSkins.h"
#include "util.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "WorldMapCatalog.h"
#include "CascLib.h"
#include <QCryptographicHash>
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
#include <QSet>
#include <QSettings>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "TextureManager.h"
#include "ModelIdLookup.h"
#include "UnityAssetAccess.h"
#include "UnityCharacterScene.h"
#include "UnityIpcServer.h"
#include "UnityRendererHost.h"

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
                                bool component = false, const QString & equipmentPath = QString())
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
  const auto skip = [](const QString & what, const QString & why) {
    LOG_INFO << QString("[customization-test] SKIP %1 -- %2").arg(what, why);
  };
  checkNpcAppearance(frame, check, skip);

  // ---- 10. The geoset groups an NPC's shirt, chest and legs share --------------------------------------
  checkEquipmentGeosets(frame, check, skip);

  // ---- 11. A character's model of the other generation (Classic | HD) ----------------------------------
  checkModelVariants(frame, check, skip);

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

// THE SCREENSHOT STEP of the lifecycle sequence, "screenshot:<path>" (protocol 6): what the command bar's Screenshot
// does once its Save As dialog has returned a path -- ModelViewer::RequestUnityScreenshot, which shows no dialog --
// then, with the canvas ticking, the player's screenshotSaved for that request. Passes when the answer is ok, the file
// it names has the size it reports, and the file is what was asked for: a PNG signature, an IHDR of
// SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT, 8 bits per channel, colour type 6 (RGBA), not interlaced; and the status bar
// request is settled. what carries the player's timings.
static bool doIpcTestScreenshotStep(ModelViewer * frame, UnityIpcServer * ipc, const QString & target, QString & what,
                                    QString & why)
{
  if (target.isEmpty())
  {
    why = "screenshot: takes the full path of the PNG to write";
    return false;
  }
  std::vector<UnityIpcServer::ScreenshotResult> results;
  auto previous = ipc->onScreenshotSaved;
  ipc->onScreenshotSaved = [&results, previous](const UnityIpcServer::ScreenshotResult & r) {
    results.push_back(r);
    if (previous)
      previous(r);
  };
  wxString whyNot;
  const int request = frame->RequestUnityScreenshot(wxString(target.toStdWString()), whyNot);
  const QString path = QString::fromWCharArray(frame->m_screenshotPath.wc_str());
  UnityIpcServer::ScreenshotResult answer;
  bool answered = false;
  wxStopWatch w;
  if (request != 0)
  {
    wxGUIEventLoop loop;
    wxEventLoopActivator activate(&loop);
    while (!answered && w.Time() < 120000)
    {
      ipc->poll();
      wxTheApp->Yield(true);
      wxMilliSleep(10);
      for (const UnityIpcServer::ScreenshotResult & r : results)
        if (r.request == request)
        {
          answer = r;
          answered = true;
        }
    }
  }
  ipc->onScreenshotSaved = previous;

  QStringList bad;
  if (request == 0)
    bad << "not requested: " + QString::fromWCharArray(whyNot.wc_str());
  else if (!answered)
    bad << QString("no screenshotSaved for request %1 within 120 s").arg(request);
  else
  {
    what = QString("request %1 answered in %2 ms: ").arg(request).arg(w.Time()) + answer.describe();
    if (!answer.ok)
      bad << "the player did not save it: " + answer.error;
    else
    {
      QFile png(path);
      if (!png.open(QIODevice::ReadOnly))
        bad << "cannot open " + path;
      else
      {
        const QByteArray head = png.read(33);
        const auto be32 = [&head](int at) {
          return (quint32)(uchar)head[at] << 24 | (quint32)(uchar)head[at + 1] << 16 | (quint32)(uchar)head[at + 2] << 8 |
                 (quint32)(uchar)head[at + 3];
        };
        if (png.size() != answer.bytes)
          bad << QString("the file has %1 bytes, the player reported %2").arg(png.size()).arg(answer.bytes);
        if (head.size() < 33 || !head.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)) || head.mid(12, 4) != "IHDR")
          bad << "not a PNG";
        else
        {
          const quint32 width = be32(16), height = be32(20);
          if (width != (quint32)ModelViewer::SCREENSHOT_WIDTH || height != (quint32)ModelViewer::SCREENSHOT_HEIGHT)
            bad << QString("the PNG is %1 x %2").arg(width).arg(height);
          if ((uchar)head[24] != 8 || (uchar)head[25] != 6)
            bad << QString("bit depth %1, colour type %2 (expected 8 and 6, RGBA)").arg((uchar)head[24]).arg((uchar)head[25]);
          if ((uchar)head[28] != 0)
            bad << "the PNG is interlaced";
          what += QString("; file %1 x %2, %3 bits, colour type %4, interlace %5").arg(width).arg(height)
                    .arg((uchar)head[24]).arg((uchar)head[25]).arg((uchar)head[28]);
        }
      }
    }
    if (frame->m_screenshotRequest != 0)
      bad << "the host still waits for the screenshot";
  }
  why = bad.join("; ");
  return bad.isEmpty();
}

// THE BACKGROUND STEP of the lifecycle sequence, "background:<#RRGGBB|default>" (protocol 7): what picking a colour in
// View > Swap Background Color does (ModelViewer::setViewportBackground; not kept in Config.ini, so a test leaves the
// file as it was), then the player's runtimeState, which must hold exactly that colour. Exactly one viewportBackground
// is sent for a colour that is not on show, and none for the one that is.
static bool doIpcTestBackgroundStep(ModelViewer * frame, UnityIpcServer * ipc, const QString & target,
                                    const std::vector<UnityIpcServer::RuntimeState> & states, QString & what, QString & why)
{
  wxColour colour;
  if (target.compare("default", Qt::CaseInsensitive) == 0)
    colour = ViewportBackground::defaultColour();
  else if (!ViewportBackground::parseHex(wxString(target.toStdWString()), colour))
  {
    why = "background: takes #RRGGBB or default";
    return false;
  }
  if (!ipc->playerPaintsBackground())
  {
    why = QString("the player (protocol %1) does not take a background").arg(ipc->playerProtocolVersion());
    return false;
  }
  const bool onShow = ViewportBackground::sameColour(colour, frame->viewportBackground());
  const int sentBefore = ipc->stats().backgroundPushes;
  frame->setViewportBackground(colour, false);
  const int sent = ipc->stats().backgroundPushes - sentBefore;
  UnityIpcServer::RuntimeState held;
  bool answered = false;
  const bool holds = waitRuntimeState(ipc, states, [&colour](const UnityIpcServer::RuntimeState & x) {
    return x.backgroundR == colour.Red() && x.backgroundG == colour.Green() && x.backgroundB == colour.Blue();
  }, 10000, held, answered);
  const QString hex = QString::fromWCharArray(ViewportBackground::formatHex(colour).wc_str());
  what = QString("%1 (%2 on show), %3 viewportBackground sent; the player holds %4,%5,%6")
           .arg(hex).arg(onShow ? "already" : "not").arg(sent).arg(held.backgroundR).arg(held.backgroundG).arg(held.backgroundB);
  QStringList bad;
  if (!answered)
    bad << "no runtimeState answer";
  else if (!holds)
    bad << "the player does not hold " + hex;
  if (sent != (onShow ? 0 : 1))
    bad << QString("%1 viewportBackground sent, expected %2").arg(sent).arg(onShow ? 0 : 1);
  why = bad.join("; ");
  return bad.isEmpty();
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
  if (kind == "screenshot")
    return doIpcTestScreenshotStep(frame, ipc, target, what, why);
  if (kind == "background")
    return doIpcTestBackgroundStep(frame, ipc, target, states, what, why);
  if (kind == "npc-equip")
  {
    WoWModel * model = cc ? cc->model : nullptr;
    bool slotOk = false, itemOk = false;
    const int slot = target.section('=', 0, 0).toInt(&slotOk);
    const int item = target.section('=', 1).toInt(&itemOk);
    if (!model || model->charModelDetails.isChar || !ipc->playerAttachesNpcEquipment() ||
        !slotOk || (slot != CS_HAND_RIGHT && slot != CS_HAND_LEFT) || !itemOk || item < 0)
    {
      why = "npc-equip requires an exclusive NPC and <hand slot>=<item id>";
      return false;
    }
    const int revision = frame->m_sceneRevision;
    cc->choosingSlot = slot;
    cc->numbers.assign(1, item);
    cc->cats.assign(1, 0);
    cc->OnUpdateItem(UPDATE_ITEM, 0);
    UnityIpcServer::SceneAck ack;
    bool seen = false;
    const bool applied = waitSceneAnswer(frame, ipc, acks, revision, QString(), QStringLiteral("none"),
                                         120000, ack, seen);
    int expected = 0;
    for (WoWItem * worn : *model)
      if (worn) expected += (int)worn->models().size();
    UnityIpcServer::RuntimeState state;
    bool answered = false;
    const bool held = waitRuntimeState(ipc, states, [&](const UnityIpcServer::RuntimeState & x) {
      return x.modelFileDataID == (int)model->gamefile->fileDataId() && x.liveModels == 1 + expected &&
             x.liveMounts == 0 && x.bodyRebinds == 0 && !x.loading;
    }, 30000, state, answered);
    const bool ok = applied && ack.missing.isEmpty() && ack.attachments == expected && held &&
                    model->getItem((CharSlots)slot)->id() == item && !model->charModelDetails.isChar;
    what = QString("NPC slot %1 item %2: attachments %3/%4; ").arg(slot).arg(item).arg(ack.attachments).arg(expected)
             + state.describe();
    if (!ok) why = "equipment scene/runtime mismatch: " + ack.reason + " " + ack.missing.join(", ");
    return ok;
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
  else if (kind == "manim" || kind == "ranim" || kind == "anim")
  {
    // THE ANIMATION PANEL, on the mount (manim) or on the character (ranim). When it is on the other model it is moved
    // first, as View > Attachments moves it (AnimControl::UpdateModel), then the clip is picked as a user picks it.
    const bool onMount = kind == "manim";
    WoWModel * model = onMount ? frame->riderMount() : rider;
    WoWModel * other = onMount ? rider : frame->riderMount();
    if (!model || (kind != "anim" && !other))
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
    const int otherSequence = other ? hostSequence(other) : -1;
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
    const bool usesRole = other != nullptr;
    const int pushes = usesRole ? ipc->stats().rolePushes : ipc->stats().animPushes;
    frame->animControl->pickAnimationLikeUser(clip);
    pumpTicking(ipc, 200);
    now = riddenHost(frame);
    if (hostSequence(model) != sequence)
      bad << QString("the host's %1 plays %2").arg(onMount ? "mount" : "character").arg(hostSequence(model));
    if (other && hostSequence(other) != otherSequence)
      bad << QString("the host's %1 went from sequence %2 to %3").arg(onMount ? "character" : "mount")
               .arg(otherSequence).arg(hostSequence(other));
    if ((usesRole ? ipc->stats().rolePushes : ipc->stats().animPushes) <= pushes)
      bad << "no animation push on the expected channel";
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
//   anim:<animId>         same character animation selection without requiring a mount; #<n> selects a sequence.
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
//   wait:<ms>             pumps with the canvas ticking (lets a WMV_VIEWPORT_SHOT capture land before the test ends);
//   screenshot:<path>     the command bar's Screenshot without its Save As dialog (doIpcTestScreenshotStep, protocol 6);
//   background:<colour>   the Models viewport's background, #RRGGBB or default, as View > Swap Background Color
//                         sets it but not kept (doIpcTestBackgroundStep, protocol 7).
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
                                      kind == "ranim" || kind == "anim" || kind == "equip" || kind == "custom" || kind == "sheath" ||
                                      kind == "reconnect" || kind == "wait" || kind == "screenshot" ||
                                      kind == "background" || kind == "npc-equip");
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
                "sheath, reconnect, wait:, screenshot: or background:)"
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
        // The notice decision that follows a report: the viewport is uncovered once the player has answered the
        // uncover fence (ModelViewer::m_uncoverFence), so up to 5 s.
        wxStopWatch fence;
        do
          pumpIpc(ipc, 10);
        while (frame->unityRendererHost->hasNotice() && frame->m_uncoverFence != 0 && fence.Time() < 5000);
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
      // The notice decision a report may cause: the viewport is uncovered once the player has answered the uncover
      // fence (ModelViewer::m_uncoverFence: two runtimeState questions in turn), so up to 5 s.
      wxStopWatch fence;
      do
        pumpIpc(ipc, 20);
      while (frame->unityRendererHost->hasNotice() && frame->m_uncoverFence != 0 && fence.Time() < 5000);
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
             << "(" << ipc->stats().imageBytes << "pixel bytes); last" << ipc->stats().lastScene << "| ack"
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

  // THE BACKGROUND (protocol 7): the player was sent the colour for what is loaded (viewportBackgroundShown) when it
  // announced itself and with its loads, and holds exactly that colour. An older player keeps its own default and is
  // not asked.
  bool backgroundOk = true;
  if (ipc->playerPaintsBackground())
  {
    std::vector<UnityIpcServer::RuntimeState> backgroundStates;
    auto previousRuntime = ipc->onRuntimeState;
    ipc->onRuntimeState = [&backgroundStates, previousRuntime](const UnityIpcServer::RuntimeState & x) {
      backgroundStates.push_back(x);
      if (previousRuntime)
        previousRuntime(x);
    };
    // The colour for what is loaded (a world model keeps the viewport's default; viewportBackgroundShown).
    const wxColour want = frame->viewportBackgroundShown();
    UnityIpcServer::RuntimeState held;
    bool answered = false;
    backgroundOk = ipc->stats().backgroundPushes >= 1 &&
                   waitRuntimeState(ipc, backgroundStates, [&want](const UnityIpcServer::RuntimeState & x) {
                     return x.backgroundR == want.Red() && x.backgroundG == want.Green() && x.backgroundB == want.Blue();
                   }, 10000, held, answered);
    ipc->onRuntimeState = previousRuntime;
    LOG_INFO << "[unityipc-test] background:" << QString::fromWCharArray(ViewportBackground::formatHex(want).wc_str())
             << "sent" << ipc->stats().backgroundPushes << "time(s); the player holds" << held.backgroundR
             << held.backgroundG << held.backgroundB << (backgroundOk ? "(OK)" : "(FAIL)");
  }
  else
    LOG_INFO << "[unityipc-test] background: not checked, the player speaks protocol" << ipc->playerProtocolVersion();

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
                    wmoOk && sequenceOk && viewportOk && neverPainted && backgroundOk;
  LOG_INFO << "[unityipc-test] RESULT:" << (pass ? "PASS" : "FAIL");

  // Close the player now (what app shutdown does) and confirm the child process is gone.
  ipc->onMapObjectLoaded = previousMapObject;
  frame->unityRendererHost->shutdown();
  LOG_INFO << "[unityipc-test] player shut down; still running=" << (frame->unityRendererHost->isRunning() ? 1 : 0);
}

// -terraintile <map id> <tile x> <tile y> [<out.json>] (with -product / -build to pin the client): THE TERRAIN TILE TEST
// (protocol 12 and 13; ModelViewer::RequestTerrainTile, UnityIpcServer.h TERRAIN and TERRAIN TEXTURES). After the client is
// loaded, and before any player is started -- so they run with any player, or none -- the in-process checks: the
// getMapAsset decision on made-up requests (ModelViewer::decideMapAssetRequest: every refusal and staleness, the texture
// roles' order, once-only, id, read and byte caps, a reported load, and the read limit of those that go on to the read),
// the per-load accounting driven through it (ModelViewer::noteMapAssetRead / noteMapAssetResult), the kind check on made-up
// bytes (ModelViewer::mapAssetKindProblem), the map asset queue on a server of its own (order, the poll budget, a full
// queue refused unread, dropped with the connection, bounded poll records), the host's read tracker (a reset for a
// replaced client empties it at once, a read under another epoch empties it first, its capacity, no false first read past
// it) and record ring (more records than it keeps), what the host reads of a report's lists on made-up reports (texture
// entries up to TERRAIN_REPORT_TEXTURES_MAX, retired ids and chunk-state counters up to TERRAIN_REPORT_LIST_MAX, the rest
// counted) and how the material check judges a texture list, real reads through the host's own handler
// (ModelViewer::ServeMapAsset) under a made-up load -- a root asked for as a tex0 and a tex0 as a texture fail their kinds,
// the tex0 and a texture are served with the content key as md5, a texture over what is left of the load's budget is not
// read, a spent budget and a reported load read nothing --, the map's WDT inspected with a 1000-byte limit (Unreadable, the
// detail naming its size and the limit, no bytes) and with the map-file limit (read whole), the WDT a load would name as
// this client's family resolves it (ModelViewer::ResolveTerrainTile, with its refusals), and the host's bookkeeping of a
// failed load while an earlier tile is alive (ModelViewer::OnTerrainTileLoaded). Then the player is started
// (StartUnityRenderer(true)); one older than protocol 12 ends the run "FAIL player too old". A protocol-12 player draws
// geometry alone: every textured check is skipped naming why, and each of its built loads must have no material and only
// its two files served. Otherwise the player's object counts are taken with no terrain (every count reported, the
// terrain's own all 0), the tile is requested and its terrainTileLoaded awaited (60 s) -- the Mesh count must rise with
// it -- and WMV_TERRAINTEST_SEQUENCE runs,
// comma-separated:
//   tile:<map>:<x>:<y>        another tile, built;
//   repeat:<n>                the last tile n more times, each built, and the player's Mesh and Material counts after
//                             them those after its first build (nothing piles up); a run of 50 or more is also sampled
//                             every 25 loads;
//   wait:<ms>                 pumps with the canvas ticking (lets a WMV_VIEWPORT_SHOT capture land);
//   clear                     clearTerrain: a tile on show is taken away unanswered;
//   clear:<map>:<x>:<y>       that tile requested and cleared before anything is polled: the load, still building, is
//                             answered "cleared", once; after either form no terrain tile, view, load, terrain object,
//                             texture or alpha array, and the player's Mesh and Material counts those of the reference
//                             with no terrain (taken before the first tile; after a model or switch step, the counts it
//                             left, once its terrain objects were 0) -- the check names which;
//   expectfail:<map>:<x>:<y>:<text>   refused by the host, or answered "failed", with <text> in the reason;
//   notlocal:<map>:<x>:<y>:<root>     when the root ADT is still listed but not downloaded (inspected first; otherwise
//                             SKIP "precondition changed"), the load fails with host state "not installed locally";
//   notlocalpart:<map>:<x>:<y>:<id>   when that tex0 or texture of the tile is listed but not downloaded (a precondition
//                             as above), the tile is built, the host answers the file "not installed locally", and the
//                             material says so: "unavailable" for the tex0, the host's answer quoted for a texture;
//   expectmaterial:<state>[:<map>:<x>:<y>]  that tile (or the last) built, its material <state> (textured, partial,
//                             unavailable, unsupported);
//   expectserved:<role>:<state>[:<text>]    the last load's requests of <role> answered <state>, <text> in the detail (a
//                             stand-in player's deliberate requests: a root asked for as a tex0, a full queue);
//   switch:<product>          the last tile requested, then that client loaded before anything is polled: no
//                             getMapAsset may be answered while it loads (else "test invalid"), the WDT request is
//                             then answered "stale" (its epoch, and the next), the load answered "cleared", no tile left;
//   staleepoch                the last tile, the client epoch moved on before polling: failed, host state "stale";
//   staleload                 the last tile, the load serial raised without sending: failed, host state "stale";
//   supersede:<m>:<x>:<y>|<m>:<x>:<y>  two loads back to back: the first "superseded", the second built;
//   model:<path>              a model selected as Browse does after the terrain: no terrain tile, view or terrain
//                             object left, the model on screen;
//   model:<path>|<m>:<x>:<y>  the same with that tile requested first and the model selected before anything is polled:
//                             the load, still building, is answered "superseded", once;
//   modelthentile:<path>|<m>:<x>:<y>  a model other than the one on screen selected, and (once the player says it is
//                             still building it -- else "test invalid") the tile requested: the tile built, and a
//                             report window later the player builds nothing, shows the tile in the terrain view and still
//                             has the model of before (the dropped one never adopted); the host puts the canvas model's
//                             "could not be built" notice up while the tile builds, none while it is shown, and again
//                             after a later clear or failed load that leaves no tile;
//   failrepeat:<n>[:<m>:<x>:<y>]  n loads of a tile that fails (default the Azeroth tile 0_0, MAIN flag clear): each fails
//                             and is answered once, and each fails on its own answer, so after each the player's
//                             terrainRetiredTotal is still the one sampled before the first (none retired) and no
//                             terrain load is building (terrainLoading 0).
// THE LATE ANSWERS, with the server's test hooks (UnityIpcServer::testHoldNextMapAsset): the player is sampled, a load's
// getMapAsset of <role> (wdt, rootAdt; tex0Adt or texture for a protocol-13 player, skipped for an older one) is held
// unanswered and the load ends with it held. The player must have retired what the reports of the loads ended since name
// (protocol 13: requestsRetired with the ids, the held one among them; protocol 12: exactly the held one, retired last):
// terrainRetiredTotal that much above the sample, terrainRetiredRequests that much fuller up to the ring's capacity (128
// for a protocol-13 player, 32 for a protocol-12 one), terrainLastRetiredRequest one of those. The request is released,
// the host answers it through its usual decision, and the player must drop as late every answer to a retired request --
// mapAnswersLate and mapAnswersDropped that much higher than before the load, mapAnswersUnowned unchanged -- with no
// second report for the load, nothing on screen changed, and every request it sent accounted for (the host received as
// many as the player applied, retired and holds pending):
//   timeout:<m>:<x>:<y>:<role>       the player's own timeout ends the load, "failed" with "timed out" (it states its
//                             terrainTimeoutMs; a short one comes from WMV_DEBUG -wmvTerrainTimeoutMs=<ms>); released as the
//                             report arrives, before the host takes the load as ended, the file goes back "available",
//                             after its load ended; then the host answers the load's requests stale, "was reported failed";
//   latesupersede:<m>:<x>:<y>:<role> the next load of the tile, not held, answers it "superseded" and is built; released, "stale";
//   lateclear:<m>:<x>:<y>:<role>     clearTerrain answers it "cleared"; released, "stale";
//   lateswitch:<product>:<m>:<x>:<y>[:<role>]  its WDT (or role) held, that client is loaded: "cleared", and the host's read
//                             tracker holds no file at once (emptied with the client it was of); released, "stale" under the
//                             epoch before and the one after; then the tile under the new client is built from first
//                             reads, and the tracker holds those files alone (WDT, root, and a protocol-13 load's tex0
//                             and textures). The player's timeout must be longer than loading that client (Retail took
//                             6.9-7.9 s): a shorter one fails the held load, "timed out", before the host can answer it
//                             "cleared" -- a fault of the run's settings, not of either side;
//   reconnect[:<m>:<x>:<y>[:<role>]]  the player closed and started again (with that tile's load in flight, its WDT -- or
//                             role -- request held: dropped with the connection, never answered, and the load answered by
//                             no player; nothing left waiting): the host holds no terrain request and keeps its read
//                             tracker, the new player nothing pending, retired or dropped, and a tile (that one, or the
//                             last) is then built, its first reads only the files not served before the restart.
// terrainPendingRequests is checked 0 wherever no load is in flight, as an invariant only: the player counts the requests
// of the load in flight, so it reads 0 once a load has ended whether or not that load's requests were retired. A player
// that does not report terrainPendingRequests, terrainRetiredRequests, terrainRetiredTotal, terrainLastRetiredRequest,
// mapAnswersRouted, mapAnswersDropped, mapAnswersLate, mapAnswersUnowned or terrainTimeoutMs (or, at protocol 13,
// terrainTextures, terrainTextureBytes and terrainAlphaArrays) fails the checks that need it, "player does not report
// <field>".
// Every built load: the report names the tile, the WDT and root it parsed are those served (FileDataID and MD5), one
// terrain tile is alive and the terrain's Meshes, Materials and GameObjects are counted; a protocol-13 player's material
// agrees with what the host served (the tex0 by FileDataID, bytes and MD5, or "unavailable" quoting the host; each texture
// served with its MD5 and bytes, or the host's answer quoted verbatim, in every entry of its id; each entry a "diffuse" or
// "height" texture, at most 128 of them asked for, at most TERRAIN_REPORT_TEXTURES_MAX in all; the host's limits kept; the
// stage 04 chunk states summing to the chunks; nothing retired by a built load) and its textures and alpha arrays are
// counted. Every load sent is answered exactly once (one in flight when its player was restarted, never), every report
// names a load the run sent (one that does not is counted apart and fails), and no terrain load is building at the end.
// The test keeps what it needs bounded -- the most recent reports and records, counts per load sent, the distinct files
// served, timing samples up to a cap -- and writes every full report, record and load to <out.json>.records.jsonl as it
// comes, read back into <out.json> at the end. At the end the server's records are the most recent MAP_ASSET_RECORDS_KEPT with the rest
// counted, its read tracker holds exactly the files served under its own epoch -- the last a file was read in, or a later
// one when a client was loaded after that read --, nothing is held or waiting, and its poll records are bounded; the
// timing of the files served is summarised by role (handlerMs, readMs, queuedMs; first reads apart; files over 4 MiB
// apart) with the GUI time of each poll that served map files. Every sample of the player is logged and written with this
// process's private bytes and working set and that bookkeeping beside it. Lines "[terrain-test] PASS|FAIL|SKIP <check> --
// <detail>" go to the log, then "[terrain-test] RESULT: PASS|FAIL"; every report, every getMapAsset served and every
// check go to <out.json>, which Tools/world-audit/check_terrain_run.py checks against the pinned values.
namespace
{
  struct TerrainTestLog
  {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    QJsonArray checks;

    void check(const QString & what, bool ok, const QString & detail)
    {
      ok ? passed++ : failed++;
      const QString line = QString("[terrain-test] %1 ").arg(ok ? "PASS" : "FAIL") + what +
                           (detail.isEmpty() ? QString() : " -- " + detail);
      if (ok)
        LOG_INFO << line;
      else
        LOG_ERROR << line;
      QJsonObject c;
      c["check"] = what;
      c["result"] = ok ? "PASS" : "FAIL";
      c["detail"] = detail;
      checks.append(c);
    }

    void skip(const QString & what, const QString & detail)
    {
      skipped++;
      LOG_WARNING << "[terrain-test] SKIP " + what + " -- " + detail;
      QJsonObject c;
      c["check"] = what;
      c["result"] = "SKIP";
      c["detail"] = detail;
      checks.append(c);
    }
  };

  QJsonObject servedJson(const UnityIpcServer::MapAssetServed & s)
  {
    QJsonObject o;
    o["requestId"] = s.requestId;
    o["load"] = s.load;
    o["assetEpoch"] = s.assetEpoch;
    o["epochNow"] = s.epochNow;
    o["role"] = s.role;
    o["fileDataID"] = s.fileDataID;
    o["bytes"] = (double)s.bytes;
    o["state"] = s.state;
    o["detail"] = s.detail;
    o["md5"] = s.md5;
    o["readMs"] = s.readMs;
    o["handlerMs"] = s.handlerMs;
    o["queuedMs"] = s.queuedMs;
    o["bytesRead"] = (double)s.bytesRead;
    o["first"] = s.first;
    o["firstTracked"] = s.firstTracked;
    return o;
  }

  QJsonObject runtimeJson(const QString & label, const UnityIpcServer::RuntimeState & s)
  {
    QJsonObject o;
    o["label"] = label;
    o["query"] = s.query;
    o["liveTerrainTiles"] = s.liveTerrainTiles;
    o["terrainLoad"] = s.terrainLoad;
    o["terrainLoading"] = s.terrainLoading;
    o["terrainView"] = s.terrainView;
    o["cameraMask"] = s.hasCameraMask ? QJsonValue((double)s.cameraMask) : QJsonValue();
    o["meshObjects"] = s.meshObjects;
    o["materialObjects"] = s.materialObjects;
    o["terrainMeshes"] = s.terrainMeshes;
    o["terrainMaterials"] = s.terrainMaterials;
    o["terrainGameObjects"] = s.terrainGameObjects;
    o["liveModels"] = s.liveModels;
    o["liveMapObjects"] = s.liveMapObjects;
    o["modelFileDataID"] = s.modelFileDataID;
    o["mapObjectFileDataID"] = s.mapObjectFileDataID;
    o["loading"] = s.loading;
    o["assetEpoch"] = s.assetEpoch;
    o["terrainPendingRequests"] = s.terrainPendingRequests;
    o["terrainRetiredRequests"] = s.terrainRetiredRequests;
    o["mapAnswersRouted"] = s.mapAnswersRouted;
    o["mapAnswersDropped"] = s.mapAnswersDropped;
    o["terrainTimeoutMs"] = s.terrainTimeoutMs;
    o["terrainRetiredTotal"] = s.terrainRetiredTotal;
    o["terrainLastRetiredRequest"] = s.hasTerrainLastRetiredRequest ? QJsonValue(s.terrainLastRetiredRequest) : QJsonValue();
    o["mapAnswersLate"] = s.mapAnswersLate;
    o["mapAnswersUnowned"] = s.mapAnswersUnowned;
    o["terrainTextures"] = s.terrainTextures;
    o["terrainTextureBytes"] = (double)s.terrainTextureBytes;
    o["terrainAlphaArrays"] = s.terrainAlphaArrays;
    return o;
  }

  // This process's private bytes and working set (-1 when Windows does not say).
  void processMemory(long long & privateBytes, long long & workingSet)
  {
    privateBytes = workingSet = -1;
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    {
      privateBytes = (long long)pmc.PrivateUsage;
      workingSet = (long long)pmc.WorkingSetSize;
    }
  }

  // Median and 95th percentile (nearest rank) of a list, for the timing summary.
  void percentiles(std::vector<double> v, double & median, double & p95, double & max)
  {
    median = p95 = max = -1.0;
    if (v.empty())
      return;
    std::sort(v.begin(), v.end());
    median = v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2.0;
    p95 = v[std::min(v.size() - 1, (size_t)std::ceil(0.95 * v.size()) - 1)];
    max = v.back();
  }
}

// The getMapAsset decision on made-up requests: every way a request is refused or stale, which reason wins when
// several hold, and the requests that go on to the read. Nothing is read and no player is involved.
static void checkMapAssetDecisions(TerrainTestLog & tally)
{
  ModelViewer::TerrainRequest current;
  current.load = 5;
  current.epoch = 7;
  current.mapId = 0;
  current.tileX = 32;
  current.tileY = 50;
  current.wdtFileDataID = 775971;
  current.catalogueGeneration = 1;
  ModelViewer::TerrainRequest served = current;   // the load's WDT answered "available"
  served.wdtServed = true;
  ModelViewer::TerrainRequest rooted = served;    // ... and its root ADT asked for
  rooted.rootFileDataID = 778037;
  // TERRAIN TEXTURES (protocol 13): a load sent with textures, its WDT not served yet, then served, then its tex0 too.
  ModelViewer::TerrainRequest textures = current;
  textures.maxTextureBytes = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES;
  textures.maxTextureBytesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD;
  textures.maxTexturesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD;
  ModelViewer::TerrainRequest texWdt = textures;
  texWdt.wdtServed = true;
  texWdt.reads = 1;
  ModelViewer::TerrainRequest texTex0 = texWdt;   // root and tex0 read, the tex0 answered "available"
  texTex0.rootFileDataID = 778037;
  texTex0.tex0Asked = true;
  texTex0.tex0FileDataID = 778040;
  texTex0.tex0Served = true;
  texTex0.reads = 3;
  ModelViewer::TerrainRequest tex0Failed = texTex0; // the tex0 asked for, but it failed (its kind, say): not served
  tex0Failed.tex0Served = false;
  ModelViewer::TerrainRequest texOne = texTex0;    // one texture read
  texOne.textureIds = {187115};
  texOne.textureBytesRead = 88580;
  texOne.reads = 4;
  ModelViewer::TerrainRequest tex127 = texTex0, tex128 = texTex0;   // 127 and 128 texture ids asked for
  for (int i = 0; i < 128; i++)
  {
    if (i < 127)
      tex127.textureIds.push_back(2000000 + i);
    tex128.textureIds.push_back(2000000 + i);
  }
  tex127.reads = 3 + 127;
  tex128.reads = 3 + 128;
  ModelViewer::TerrainRequest texReads = texTex0;  // few ids, but 3 + 128 reads already (the WDT and root asked again)
  texReads.textureIds = {187115, 187127};
  texReads.reads = 3 + UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD;
  ModelViewer::TerrainRequest rootedReads = rooted; // a load without textures that read 3 files
  rootedReads.reads = 3;
  ModelViewer::TerrainRequest spent = texOne;       // 128 MiB of textures read
  spent.textureBytesRead = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD;
  ModelViewer::TerrainRequest nearlySpent = texOne; // 1000 bytes of it left
  nearlySpent.textureBytesRead = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD - 1000;
  ModelViewer::TerrainRequest ended = texTex0;      // the player reported the load
  ended.ended = true;
  ended.endedStatus = "failed";
  ModelViewer::TerrainRequest plainTex0 = rooted;   // a load without textures whose tex0 somehow counts as served
  plainTex0.tex0Served = true;
  const int epochNow = 7;
  // load -1000 leaves "load" out of the request.
  const auto request = [](int load, int epoch, const QString & role, int fileDataID) {
    QJsonObject r;
    r["type"] = "getMapAsset";
    r["requestId"] = "u1";
    if (load != -1000)
      r["load"] = load;
    r["assetEpoch"] = epoch;
    r["role"] = role;
    r["fileDataID"] = fileDataID;
    return r;
  };
  struct Case
  {
    const char * name;
    QJsonObject request;
    const ModelViewer::TerrainRequest * current;
    bool belongs;
    bool loading;
    const char * state;
    QString detail;   // a part the decision's detail must contain
    qint64 maxBytes = -1;   // the read's limit a request that goes on to the read must have (-1: not checked)
  };
  const qint64 mapLimit = UnityIpcServer::MAX_MAP_ASSET_BYTES, textureLimit = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES;
  const Case cases[] = {
    {"the current load's WDT goes on to the read, at most the map-file limit", request(5, 7, "wdt", 775971), &current, true,
     false, "", "", mapLimit},
    {"role \"x\" is refused", request(5, 7, "x", 775971), &current, true, false, "refused",
     "unknown role \"x\" (wdt, rootAdt, tex0Adt or texture)"},
    {"another WDT is refused", request(5, 7, "wdt", 775972), &current, true, false, "refused",
     "WDT is FileDataID 775971, not 775972"},
    {"FileDataID 0 is refused", request(5, 7, "wdt", 0), &current, true, false, "refused", "FileDataID 0 names no file"},
    {"FileDataID -1 is refused", request(5, 7, "wdt", -1), &current, true, false, "refused", "FileDataID -1 names no file"},
    {"a request without a load is refused", request(-1000, 7, "wdt", 775971), &current, true, false, "refused",
     "names no terrain load"},
    {"load 0 is refused", request(0, 7, "wdt", 775971), &current, true, false, "refused", "names no terrain load"},
    {"the load before the current one is stale", request(4, 7, "wdt", 775971), &current, true, false, "stale",
     "request load 4, current 5"},
    {"the client epoch before the current one is stale", request(5, 6, "wdt", 775971), &current, true, false, "stale",
     "request epoch 6, current 7"},
    {"a map catalogue that is not the loaded client's is stale", request(5, 7, "wdt", 775971), &current, false, false,
     "stale", "map catalogue"},
    {"no current terrain load is stale", request(5, 7, "wdt", 775971), nullptr, true, false, "stale",
     "no terrain load is current"},
    {"every staleness that holds is named", request(4, 6, "wdt", 775971), &current, false, false, "stale",
     "request load 4, current 5; request epoch 6, current 7; the map catalogue"},
    {"staleness is decided before the role", request(4, 7, "x", 775971), &current, true, false, "stale",
     "request load 4, current 5"},
    {"a client being loaded is refused before anything else", request(4, 6, "x", 0), &current, false, true, "refused",
     "a client is being loaded"},
    {"a root ADT before the load's WDT was served is refused", request(5, 7, "rootAdt", 778037), &current, true, false,
     "refused", "WDT has not been served"},
    {"a root ADT after the WDT goes on to the read", request(5, 7, "rootAdt", 778037), &served, true, false, "", ""},
    {"the same root ADT again goes on to the read", request(5, 7, "rootAdt", 778037), &rooted, true, false, "", ""},
    {"a second root ADT for the load is refused", request(5, 7, "rootAdt", 778038), &rooted, true, false, "refused",
     "one root ADT per load, not 778038"},
    {"the WDT again after its root goes on to the read", request(5, 7, "wdt", 775971), &rooted, true, false, "", ""},
    // TERRAIN TEXTURES (protocol 13).
    {"a tex0 ADT before the load's WDT was served is refused", request(5, 7, "tex0Adt", 778040), &textures, true, false,
     "refused", "load 5's WDT has not been served, so no tex0 ADT is"},
    {"a tex0 ADT after the WDT goes on to the read, at most the map-file limit", request(5, 7, "tex0Adt", 778040), &texWdt,
     true, false, "", "", mapLimit},
    {"a second tex0 ADT request (the same id) is refused", request(5, 7, "tex0Adt", 778040), &texTex0, true, false,
     "refused", "has asked for its tex0 ADT (778040) already: one tex0 ADT per load"},
    {"a second tex0 ADT request (another id) is refused", request(5, 7, "tex0Adt", 778041), &texTex0, true, false,
     "refused", "(this request: 778041)"},
    {"a tex0 ADT again after the first one failed is refused (asked once, whatever came of it)",
     request(5, 7, "tex0Adt", 778040), &tex0Failed, true, false, "refused", "one tex0 ADT per load"},
    {"a texture before the load's tex0 was served is refused", request(5, 7, "texture", 187115), &texWdt, true, false,
     "refused", "load 5's tex0 ADT has not been served, so no texture is"},
    {"a texture after a tex0 that failed is refused", request(5, 7, "texture", 187115), &tex0Failed, true, false,
     "refused", "tex0 ADT has not been served"},
    {"a texture after the tex0 goes on to the read, at most the texture limit", request(5, 7, "texture", 187115), &texTex0,
     true, false, "", "", textureLimit},
    {"a texture asked for before is refused", request(5, 7, "texture", 187115), &texOne, true, false, "refused",
     "load 5 has asked for texture 187115 before: each texture once per load"},
    {"another texture goes on to the read", request(5, 7, "texture", 187127), &texOne, true, false, "", "", textureLimit},
    {"the 128th texture id goes on to the read", request(5, 7, "texture", 187115), &tex127, true, false, "", "", textureLimit},
    {"the 129th texture id is refused", request(5, 7, "texture", 187115), &tex128, true, false, "refused",
     "load 5 has asked for 128 textures: at most 128 per load, not 187115"},
    {"a read past 3 + 128 for the load is refused", request(5, 7, "texture", 187138), &texReads, true, false, "refused",
     "load 5 has read 131 files: at most 131 per load (3 + 128 textures)"},
    {"the WDT again past the read cap is refused too (every role counts)", request(5, 7, "wdt", 775971), &texReads, true,
     false, "refused", "has read 131 files"},
    {"a load without textures reads at most 3 files", request(5, 7, "rootAdt", 778037), &rootedReads, true, false,
     "refused", "load 5 has read 3 files: at most 3 per load (3 + 0 textures)"},
    {"a texture once 128 MiB of textures were read is refused (budget)", request(5, 7, "texture", 187127), &spent, true,
     false, "refused", "load 5 has read 134217728 bytes of textures: at most 134217728 per load"},
    {"a texture near the byte cap is read with what is left of it", request(5, 7, "texture", 187127), &nearlySpent, true,
     false, "", "", 1000},
    {"a tex0 ADT for a load sent without textures is refused", request(5, 7, "tex0Adt", 778040), &rooted, true, false,
     "refused", "load 5 was sent without terrain textures (the player does not draw them), so no tex0 ADT is served"},
    {"a texture for a load sent without textures is refused", request(5, 7, "texture", 187115), &plainTex0, true, false,
     "refused", "so no texture is served"},
    {"a request of a load the player has reported is stale", request(5, 7, "texture", 187115), &ended, true, false,
     "stale", "load 5 was reported failed"},
    {"the reported load's staleness is decided before the role", request(5, 7, "x", 0), &ended, true, false, "stale",
     "load 5 was reported failed"},
    {"an earlier load is stale as before, not \"reported\"", request(4, 7, "texture", 187115), &ended, true, false, "stale",
     "request load 4, current 5"},
  };
  for (const Case & c : cases)
  {
    const ModelViewer::MapAssetDecision d =
      ModelViewer::decideMapAssetRequest(c.request, c.current, epochNow, c.belongs, c.loading);
    const bool ok = d.state == QLatin1String(c.state) && d.detail.contains(c.detail) &&
                    (c.maxBytes < 0 || (d.state.isEmpty() && d.maxBytes == c.maxBytes));
    tally.check(QString("decision: ") + c.name, ok,
                "state \"" + d.state + "\" detail \"" + d.detail + QString("\" maxBytes %1; expected \"").arg(d.maxBytes) +
                  c.state + "\" with \"" + c.detail + "\"" +
                  (c.maxBytes < 0 ? QString() : QString(", maxBytes %1").arg(c.maxBytes)));
  }
}

// The per-load accounting (ModelViewer::noteMapAssetRead / noteMapAssetResult) driven through the decision as the host
// serves requests, on made-up loads: the order WDT, root and tex0, textures; a tex0 that fails its kind leaves no texture
// servable and is not asked again; 128 texture ids and no more; texture bytes up to 128 MiB, each read limited to what is
// left, then refused; reads of every role capped at 3 + 128. Nothing is read: the reads' outcomes are made up.
static void checkMapAssetAccounting(TerrainTestLog & tally)
{
  const auto request = [](const QString & role, int fileDataID) {
    QJsonObject r;
    r["type"] = "getMapAsset";
    r["requestId"] = "map1";
    r["load"] = 9;
    r["assetEpoch"] = 3;
    r["role"] = role;
    r["fileDataID"] = fileDataID;
    return r;
  };
  ModelViewer::TerrainRequest base;
  base.load = 9;
  base.epoch = 3;
  base.wdtFileDataID = 775971;
  base.maxTextureBytes = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES;
  base.maxTextureBytesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD;
  base.maxTexturesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD;
  // One request served: decided, accounted before the read, the read's made-up outcome accounted after it. Returns the
  // decision's state ("" when it went on to the read).
  const auto serve = [&request](ModelViewer::TerrainRequest & t, const QString & role, int fileDataID, qint64 bytesRead,
                                bool available, ModelViewer::MapAssetDecision * out = nullptr) {
    const QJsonObject r = request(role, fileDataID);
    const ModelViewer::MapAssetDecision d = ModelViewer::decideMapAssetRequest(r, &t, 3, true, false);
    if (d.state.isEmpty())
    {
      ModelViewer::noteMapAssetRead(t, r);
      // A read never exceeds its limit: a larger file is not read at all (TooLarge).
      const bool fits = bytesRead <= d.maxBytes;
      ModelViewer::noteMapAssetResult(t, role, fits ? bytesRead : 0, available && fits);
    }
    if (out)
      *out = d;
    return d.state;
  };
  {
    // The order, and a tex0 that fails its kind.
    ModelViewer::TerrainRequest t = base;
    const QString textureFirst = serve(t, "texture", 187115, 88580, true);
    const QString tex0First = serve(t, "tex0Adt", 778040, 1286682, true);
    const QString wdt = serve(t, "wdt", 775971, 294988, true);
    const QString root = serve(t, "rootAdt", 778037, 329922, true);
    const QString tex0Bad = serve(t, "tex0Adt", 778037, 329922, false);   // a root read as a tex0: its kind fails
    const QString textureAfterBad = serve(t, "texture", 187115, 88580, true);
    const QString tex0Again = serve(t, "tex0Adt", 778040, 1286682, true);
    tally.check("accounting: before the WDT neither a texture nor the tex0; after a tex0 that failed its kind, no texture "
                "and no second tex0",
                textureFirst == "refused" && tex0First == "refused" && wdt.isEmpty() && root.isEmpty() && tex0Bad.isEmpty() &&
                  textureAfterBad == "refused" && tex0Again == "refused" && t.reads == 3 && t.tex0Asked && !t.tex0Served &&
                  t.textureIds.empty() && t.textureBytesRead == 0,
                QString("texture first \"%1\", tex0 first \"%2\", WDT \"%3\", root \"%4\", tex0 (a root) \"%5\", texture after "
                        "it \"%6\", tex0 again \"%7\"; %8 reads, tex0 asked %9")
                  .arg(textureFirst).arg(tex0First).arg(wdt).arg(root).arg(tex0Bad).arg(textureAfterBad).arg(tex0Again)
                  .arg(t.reads).arg(t.tex0Asked ? "yes" : "no") +
                  QString(", served %1, %2 texture id(s), %3 texture bytes").arg(t.tex0Served ? "yes" : "no")
                    .arg(t.textureIds.size()).arg(t.textureBytesRead));
  }
  {
    // 128 ids of 64 KiB, then the 129th: refused, nothing read.
    ModelViewer::TerrainRequest t = base;
    serve(t, "wdt", 775971, 294988, true);
    serve(t, "tex0Adt", 778040, 1286682, true);
    int read = 0;
    for (int i = 0; i < 128; i++)
      read += serve(t, "texture", 3000000 + i, 65536, true).isEmpty() ? 1 : 0;
    const QString again = serve(t, "texture", 3000000, 65536, true);
    const QString over = serve(t, "texture", 3000128, 65536, true);
    tally.check("accounting: 128 distinct texture ids are read, one asked again and the 129th are refused",
                read == 128 && again == "refused" && over == "refused" && (int)t.textureIds.size() == 128 &&
                  t.textureBytesRead == 128LL * 65536 && t.reads == 2 + 128,
                QString("%1 read; again \"%2\", 129th \"%3\"; %4 id(s), %5 bytes, %6 reads").arg(read).arg(again).arg(over)
                  .arg(t.textureIds.size()).arg(t.textureBytesRead).arg(t.reads));
  }
  {
    // Textures of 5,593,604 bytes (the largest real ones) until 128 MiB are read: each read limited to what is left; the
    // one that would overrun it is not read (TooLarge, nothing counted), then once the bytes are spent no texture is read.
    ModelViewer::TerrainRequest t = base;
    serve(t, "wdt", 775971, 294988, true);
    serve(t, "tex0Adt", 778040, 1286682, true);
    const qint64 large = 5593604;
    int read = 0, id = 4000000;
    ModelViewer::MapAssetDecision d;
    while (serve(t, "texture", id++, large, true, &d).isEmpty() && d.maxBytes >= large)
      read++;
    const qint64 left = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD - t.textureBytesRead;
    const bool overrun = d.state.isEmpty() && d.maxBytes == left && left < large;   // read with what is left: TooLarge
    // A small one still fits what is left; then the bytes are spent exactly and the next is refused unread.
    const QString fits = serve(t, "texture", id++, left, true, &d);
    const QString spentNext = serve(t, "texture", id++, 1, true);
    tally.check("accounting: texture bytes up to 128 MiB, each read limited to what is left, then refused",
                read == (int)(UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD / large) && overrun && fits.isEmpty() &&
                  d.maxBytes == left && spentNext == "refused" &&
                  t.textureBytesRead == UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD,
                QString("%1 read whole (%2 expected); then a read limited to %3 bytes; one of %4 bytes \"%5\" (limit %6); the "
                        "next \"%7\"; %8 bytes read")
                  .arg(read).arg(UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD / large).arg(left).arg(left).arg(fits)
                  .arg(d.maxBytes).arg(spentNext).arg(t.textureBytesRead));
  }
  {
    // Repeated WDT reads count too: the load's reads stop at 3 + 128 whatever the roles.
    ModelViewer::TerrainRequest t = base;
    int read = 0;
    for (int i = 0; i < 200; i++)
      read += serve(t, "wdt", 775971, 294988, true).isEmpty() ? 1 : 0;
    tally.check("accounting: a load's reads of any role stop at 3 + 128", read == 131 && t.reads == 131,
                QString("%1 WDT reads of 200 asked, %2 counted").arg(read).arg(t.reads));
  }
}

// The kind check after a read (ModelViewer::mapAssetKindProblem) on made-up bytes: a tex0 head (MVER, MAMP), a root head
// (MVER, MHDR), a BLP2 head, short and odd files; and the roles it does not apply to.
static void checkMapAssetKinds(TerrainTestLog & tally)
{
  const QByteArray tex0("REVM\x04\x00\x00\x00\x12\x00\x00\x00PMAM\x04\x00\x00\x00\x00\x00\x00\x00", 24);
  const QByteArray root("REVM\x04\x00\x00\x00\x12\x00\x00\x00RDHM\x40\x00\x00\x00", 20);
  const QByteArray blp("BLP2\x01\x00\x00\x00\x02\x08\x07\x01", 12);
  const QByteArray longMver("REVM\xff\x00\x00\x00\x12\x00\x00\x00PMAM", 16);
  struct Case
  {
    const char * name;
    const char * role;
    QByteArray bytes;
    QString expected;   // "" for no problem, else a part of the detail
  };
  const Case cases[] = {
    {"a tex0 head passes as a tex0 ADT", "tex0Adt", tex0, ""},
    {"a root head is not a tex0 ADT (its second chunk named)", "tex0Adt", root,
     "not a tex0 ADT: second chunk RDHM (hex 52 44 48 4d), not MAMP (PMAM)"},
    {"a BLP is not a tex0 ADT", "tex0Adt", blp, "not a tex0 ADT: first chunk BLP2 (hex 42 4c 50 32), not MVER (REVM)"},
    {"an MVER whose size runs past the file is not a tex0 ADT", "tex0Adt", longMver, "leaves no second chunk in 16 bytes"},
    {"three bytes are not a tex0 ADT", "tex0Adt", QByteArray("REV"), "first chunk missing (3 bytes)"},
    {"a BLP2 head passes as a texture", "texture", blp, ""},
    {"a tex0 is not a BLP2 texture", "texture", tex0, "not a BLP2 texture: it starts with REVM (hex 52 45 56 4d)"},
    {"unprintable bytes are shown as ? with their hex", "texture", QByteArray("\x00\x01\x02\x03\x04", 5),
     "it starts with ???? (hex 00 01 02 03)"},
    {"two bytes are not a BLP2 texture", "texture", QByteArray("BL"), "it starts with 2 bytes only"},
    {"no kind is checked for a WDT", "wdt", blp, ""},
    {"no kind is checked for a root ADT", "rootAdt", tex0, ""},
  };
  for (const Case & c : cases)
  {
    const QString problem = ModelViewer::mapAssetKindProblem(c.role, c.bytes);
    const bool ok = c.expected.isEmpty() ? problem.isEmpty() : problem.contains(c.expected);
    tally.check(QString("kind: ") + c.name, ok, "\"" + problem + "\"; expected " +
                (c.expected.isEmpty() ? QString("none") : "\"" + c.expected + "\""));
  }
}

// THE MAP ASSET QUEUE (UnityIpcServer) on a server of its own with no player: a made-up viewer answers each request with
// the bytes it names as read. First come first served; a poll serves until 2 MiB were read or a file over 1 MiB was;
// requests that read nothing cost nothing; past 16 waiting a request is refused at once, never handed to the viewer;
// queuedMs and the poll records; the queue dropped unanswered as when a connection ends; the poll records bounded.
static void checkMapAssetQueue(TerrainTestLog & tally)
{
  UnityIpcServer q;
  QStringList order;
  q.onGetMapAsset = [&order](const QJsonObject & r) {
    order << r.value("requestId").toString();
    UnityIpcServer::MapAssetAnswer a;
    a.state = r.value("fakeState").toString("not in client storage");
    a.detail = "made up by the terrain test";
    a.bytesRead = (long long)r.value("fakeBytes").toDouble(0);
    return a;
  };
  std::vector<UnityIpcServer::MapAssetServed> records;
  q.onMapAssetServed = [&records](const UnityIpcServer::MapAssetServed & s) { records.push_back(s); };
  int serial = 0;
  const auto receive = [&q, &serial](long long bytes) {
    QJsonObject r;
    r["type"] = "getMapAsset";
    r["requestId"] = QString("q%1").arg(++serial);
    r["load"] = 1;
    r["assetEpoch"] = 1;
    r["role"] = "texture";
    r["fileDataID"] = serial;
    r["fakeBytes"] = (double)bytes;
    q.testReceiveMapAsset(r);
  };
  const qint64 KiB = 1024, MiB = 1024 * 1024;
  {
    order.clear();
    records.clear();
    receive(100 * KiB);
    receive(100 * KiB);
    receive(100 * KiB);
    const size_t waiting = q.mapAssetsWaiting();
    const int served = q.testServeMapAssetQueue();
    bool queuedTimes = records.size() == 3;
    for (const UnityIpcServer::MapAssetServed & s : records)
      queuedTimes = queuedTimes && s.queuedMs >= 0.0 && s.bytesRead == 100 * KiB;
    const UnityIpcServer::MapAssetPoll * poll = q.mapAssetPolls().empty() ? nullptr : &q.mapAssetPolls().back();
    tally.check("queue: requests wait until served, then are answered in order, each with its queuedMs, one poll record",
                waiting == 3 && served == 3 && order.join(",") == "q1,q2,q3" && queuedTimes && poll && poll->served == 3 &&
                  poll->bytesRead == 300 * KiB && !poll->stoppedForBudget && poll->waitingAfter == 0 && poll->serveMs >= 0.0,
                QString("%1 waiting, %2 served in the order %3; the poll: %4 served, %5 bytes, %6 ms, %7 waiting after")
                  .arg(waiting).arg(served).arg(order.join(",")).arg(poll ? poll->served : -1).arg(poll ? poll->bytesRead : -1)
                  .arg(poll ? poll->serveMs : -1.0).arg(poll ? poll->waitingAfter : -1));
  }
  {
    // 600 KiB each: the fourth takes the poll past 2 MiB, the fifth waits.
    order.clear();
    for (int i = 0; i < 5; i++)
      receive(600 * KiB);
    const int first = q.testServeMapAssetQueue();
    const UnityIpcServer::MapAssetPoll a = q.mapAssetPolls().back();
    const int second = q.testServeMapAssetQueue();
    tally.check("queue: a poll stops serving once 2 MiB were read in it; the rest is served by the next",
                first == 4 && a.stoppedForBudget && a.waitingAfter == 1 && a.bytesRead == 2400 * KiB && second == 1 &&
                  order.join(",") == "q4,q5,q6,q7,q8",
                QString("first poll %1 (%2 bytes, stopped %3, %4 waiting), second %5; order %6").arg(first).arg(a.bytesRead)
                  .arg(a.stoppedForBudget ? "yes" : "no").arg(a.waitingAfter).arg(second).arg(order.join(",")));
  }
  {
    // A file over 1 MiB ends its poll's serving, wherever it comes.
    order.clear();
    receive(5593604);
    receive(10 * KiB);
    receive(10 * KiB);
    receive(5593604);
    const int a = q.testServeMapAssetQueue();
    const int b = q.testServeMapAssetQueue();
    const UnityIpcServer::MapAssetPoll pb = q.mapAssetPolls().back();
    const int c = q.testServeMapAssetQueue();
    tally.check("queue: a file over 1 MiB is the last its poll reads (the size is known only once it is read)",
                a == 1 && b == 3 && pb.largeFiles == 1 && pb.bytesRead == 20 * KiB + 5593604 && c == 0 &&
                  order.join(",") == "q9,q10,q11,q12",
                QString("polls served %1, %2 (%3 large, %4 bytes), %5; order %6").arg(a).arg(b).arg(pb.largeFiles)
                  .arg(pb.bytesRead).arg(c).arg(order.join(",")));
  }
  {
    // Past 16 waiting a request is refused at once, nothing decided or read; the 16 that read nothing go in one poll.
    order.clear();
    records.clear();
    const long long refusedBefore = q.mapAssetsRefusedQueueFull();
    for (int i = 0; i < 18; i++)
      receive(0);
    const size_t waiting = q.mapAssetsWaiting();
    const size_t answeredAtOnce = records.size();
    const bool refusedRight = answeredAtOnce == 2 && records[0].state == "refused" &&
                              records[0].detail.contains("too many map requests waiting (16 wait, at most 16)") &&
                              records[1].requestId == QString("q%1").arg(serial) && order.isEmpty();
    const int served = q.testServeMapAssetQueue();
    tally.check("queue: past 16 waiting a request is refused at once without the viewer deciding or reading; requests "
                "that read nothing are served in one poll",
                waiting == 16 && refusedRight && q.mapAssetsRefusedQueueFull() == refusedBefore + 2 && served == 16 &&
                  q.mapAssetQueueDepthMax() == 16,
                QString("%1 waiting, %2 answered on arrival (the first: %3 \"%4\"), the viewer asked %5 time(s) then; %6 "
                        "served in one poll; deepest %7")
                  .arg(waiting).arg(answeredAtOnce).arg(answeredAtOnce ? records[0].state : QString("-"))
                  .arg(answeredAtOnce ? records[0].detail : QString("-")).arg(order.size() - served).arg(served)
                  .arg(q.mapAssetQueueDepthMax()));
  }
  {
    // Dropped as when the connection ends: never answered, never handed to the viewer.
    order.clear();
    records.clear();
    receive(1000);
    receive(1000);
    receive(1000);
    const long long droppedBefore = q.mapAssetsQueueDropped();
    q.testDropMapAssetQueue();
    const int after = q.testServeMapAssetQueue();
    tally.check("queue: dropped with its connection, its requests never answered nor read",
                q.mapAssetsWaiting() == 0 && q.mapAssetsQueueDropped() == droppedBefore + 3 && after == 0 && order.isEmpty() &&
                  records.empty(),
                QString("%1 waiting, %2 dropped, %3 served after, the viewer asked %4 time(s), %5 record(s)")
                  .arg(q.mapAssetsWaiting()).arg(q.mapAssetsQueueDropped() - droppedBefore).arg(after).arg(order.size())
                  .arg(records.size()));
  }
  {
    // The poll records are bounded.
    const long long before = q.mapAssetPollsTotal();
    const int polls = UnityIpcServer::MAP_ASSET_POLL_TIMINGS_KEPT + 4;
    for (int i = 0; i < polls; i++)
    {
      receive(0);
      q.testServeMapAssetQueue();
    }
    tally.check(QString("queue: the most recent %1 poll records are kept, every poll counted")
                  .arg(UnityIpcServer::MAP_ASSET_POLL_TIMINGS_KEPT),
                (int)q.mapAssetPolls().size() == UnityIpcServer::MAP_ASSET_POLL_TIMINGS_KEPT &&
                  q.mapAssetPollsTotal() == before + polls,
                QString("%1 kept, %2 counted").arg(q.mapAssetPolls().size()).arg(q.mapAssetPollsTotal() - before));
  }
}

// Real reads through the host's own handler (ModelViewer::ServeMapAsset) with a made-up current load of the loaded client
// (no player is involved, nothing is sent): a root ADT asked for as a tex0 ADT and a tex0 as a texture fail their kinds
// ("unreadable", the bytes found named), the tex0 itself and a texture are served with the content key as their MD5 (the
// test hashes the bytes once to show it), a texture read with only 1000 bytes of the load's budget left is not read
// (the limit applied named), one with none left is refused unread, and a reported load is stale. The pinned files are
// Azeroth 32_50's (root 778037, tex0 778040, texture 187115, in Retail, Classic Era and Classic Beta): a client that cannot
// read one skips the checks that need it. The host's terrain request is put back afterwards.
static void checkMapAssetReads(ModelViewer * frame, TerrainTestLog & tally)
{
  const std::shared_ptr<wow::WorldMapCatalog> catalog = frame->mapCatalog();
  if (!catalog || catalog->status() != wow::WorldMapCatalog::Status::Ready)
  {
    tally.skip("reads: the host's own handler on real files", "the map catalogue is not ready");
    return;
  }
  const int rootId = 778037, tex0Id = 778040, textureId = 187115;
  const auto availableNow = [&catalog](int id) {
    wow::WdtReference r;
    r.kind = wow::WdtReference::Kind::FileDataId;
    r.fileDataId = id;
    QByteArray bytes;
    const wow::WdtInspection i = catalog->inspectFile(r, &bytes, 64 * 1024 * 1024);
    return i.state == wow::WdtAvailability::Available ? (qint64)bytes.size() : (qint64)-1;
  };
  const qint64 rootSize = availableNow(rootId), tex0Size = availableNow(tex0Id), textureSize = availableNow(textureId);
  const ModelViewer::TerrainRequest saved = frame->m_terrainRequest;
  ModelViewer::TerrainRequest base;
  base.load = 999999;   // no load the player is ever sent (none is sent before these checks)
  base.epoch = UnityAssetAccess::clientEpoch();
  base.mapId = 0;
  base.tileX = 32;
  base.tileY = 50;
  base.wdtFileDataID = 775971;
  base.wdtServed = true;
  base.reads = 1;
  base.catalogueGeneration = catalog->client().storageGeneration;
  base.maxTextureBytes = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES;
  base.maxTextureBytesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD;
  base.maxTexturesPerLoad = UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD;
  const auto ask = [&base](const QString & role, int id) {
    QJsonObject r;
    r["type"] = "getMapAsset";
    r["requestId"] = "map0";
    r["load"] = base.load;
    r["assetEpoch"] = base.epoch;
    r["role"] = role;
    r["fileDataID"] = id;
    return r;
  };
  const auto md5Of = [](const QByteArray & b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Md5).toHex());
  };
  const auto describe = [](const UnityIpcServer::MapAssetAnswer & a) {
    return QString("\"%1\" \"%2\", %3 bytes served, %4 read, md5 %5").arg(a.state).arg(a.detail).arg(a.bytes.size())
             .arg(a.bytesRead).arg(a.md5.isEmpty() ? QString("-") : a.md5);
  };
  if (rootSize <= 0)
    tally.skip("reads: a root ADT asked for as a tex0 ADT fails its kind", QString("root %1 cannot be read here").arg(rootId));
  else
  {
    frame->m_terrainRequest = base;
    const UnityIpcServer::MapAssetAnswer a = frame->ServeMapAsset(ask("tex0Adt", rootId));
    const ModelViewer::TerrainRequest & t = frame->m_terrainRequest;
    tally.check(QString("reads: root %1 asked for as a tex0 ADT is \"unreadable\", its second chunk named; read and counted, "
                        "the tex0 asked but not served").arg(rootId),
                a.state == "unreadable" && a.detail.contains("not a tex0 ADT: second chunk RDHM (hex 52 44 48 4d)") &&
                  a.bytes.isEmpty() && a.md5.isEmpty() && a.bytesRead == rootSize && t.tex0Asked && !t.tex0Served &&
                  t.reads == 2,
                describe(a) + QString("; reads %1, tex0 asked %2 served %3").arg(t.reads).arg(t.tex0Asked ? "yes" : "no")
                  .arg(t.tex0Served ? "yes" : "no"));
  }
  if (tex0Size <= 0)
    tally.skip("reads: the tex0 ADT is served, and asked for as a texture fails its kind",
               QString("tex0 %1 cannot be read here").arg(tex0Id));
  else
  {
    frame->m_terrainRequest = base;
    const UnityIpcServer::MapAssetAnswer a = frame->ServeMapAsset(ask("tex0Adt", tex0Id));
    const bool served = frame->m_terrainRequest.tex0Served;
    tally.check(QString("reads: tex0 %1 is served whole, its md5 the content key (the MD5 of its bytes)").arg(tex0Id),
                a.state == "available" && a.bytes.size() == tex0Size && a.md5.size() == 32 && a.md5 == md5Of(a.bytes) &&
                  a.bytesRead == tex0Size && served,
                describe(a) + QString("; the bytes' MD5 %1; tex0 served %2").arg(md5Of(a.bytes)).arg(served ? "yes" : "no"));
    const UnityIpcServer::MapAssetAnswer b = frame->ServeMapAsset(ask("texture", tex0Id));
    const ModelViewer::TerrainRequest & t = frame->m_terrainRequest;
    tally.check(QString("reads: tex0 %1 asked for as a texture is \"unreadable\", not a BLP2; its bytes count against the "
                        "load's texture budget").arg(tex0Id),
                b.state == "unreadable" && b.detail.contains("not a BLP2 texture: it starts with REVM (hex 52 45 56 4d)") &&
                  b.bytes.isEmpty() && b.bytesRead == tex0Size && t.textureBytesRead == tex0Size &&
                  t.textureIds.size() == 1,
                describe(b) + QString("; texture bytes read %1, %2 id(s)").arg(t.textureBytesRead).arg(t.textureIds.size()));
  }
  if (textureSize <= 0)
    tally.skip("reads: a texture is served, and over what is left of the budget is not read",
               QString("texture %1 cannot be read here").arg(textureId));
  else
  {
    ModelViewer::TerrainRequest withTex0 = base;
    withTex0.tex0Asked = true;
    withTex0.tex0FileDataID = tex0Id;
    withTex0.tex0Served = true;
    withTex0.reads = 3;
    frame->m_terrainRequest = withTex0;
    const UnityIpcServer::MapAssetAnswer a = frame->ServeMapAsset(ask("texture", textureId));
    tally.check(QString("reads: texture %1 is served whole, a BLP2, its md5 the content key").arg(textureId),
                a.state == "available" && a.bytes.size() == textureSize && a.bytes.startsWith("BLP2") &&
                  a.md5 == md5Of(a.bytes) && frame->m_terrainRequest.textureBytesRead == textureSize,
                describe(a));
    ModelViewer::TerrainRequest nearly = withTex0;
    nearly.textureBytesRead = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD - 1000;
    frame->m_terrainRequest = nearly;
    const UnityIpcServer::MapAssetAnswer b = frame->ServeMapAsset(ask("texture", textureId));
    tally.check(QString("reads: with 1000 bytes of the load's texture budget left, texture %1 is not read: \"unreadable\", "
                        "the limit applied named").arg(textureId),
                b.state == "unreadable" &&
                  b.detail.contains(QString("is larger than this read allows (%1 bytes, the limit is 1000)").arg(textureSize)) &&
                  b.bytes.isEmpty() && b.bytesRead == 0 && b.readMs >= 0.0 &&
                  frame->m_terrainRequest.textureBytesRead == nearly.textureBytesRead,
                describe(b));
    ModelViewer::TerrainRequest spent = withTex0;
    spent.textureBytesRead = UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD;
    frame->m_terrainRequest = spent;
    const UnityIpcServer::MapAssetAnswer c = frame->ServeMapAsset(ask("texture", textureId));
    tally.check("reads: with the load's texture budget spent, a texture is refused unread",
                c.state == "refused" && c.detail.contains("bytes of textures: at most") && c.readMs < 0.0 && c.bytesRead == 0,
                describe(c) + QString(", readMs %1").arg(c.readMs));
    ModelViewer::TerrainRequest reported = withTex0;
    reported.ended = true;
    reported.endedStatus = "built";
    frame->m_terrainRequest = reported;
    const UnityIpcServer::MapAssetAnswer d = frame->ServeMapAsset(ask("texture", textureId));
    tally.check("reads: a texture of a load the player has reported is stale, unread",
                d.state == "stale" && d.detail.contains(QString("load %1 was reported built").arg(base.load)) && d.readMs < 0.0,
                describe(d));
  }
  frame->m_terrainRequest = saved;
}

// What the host takes as shown after a failed load (ModelViewer::OnTerrainTileLoaded), on made-up reports: a tile still
// alive is on show -- it may be one the host never heard was built (adopted, then answered "superseded" by the failed
// load) --, a failure that leaves no tile tells nothing, and a report about another load changes nothing. No player is
// involved; the host's terrain request and player content are put back afterwards.
static void checkFailedLoadBookkeeping(ModelViewer * frame, TerrainTestLog & tally)
{
  const ModelViewer::TerrainRequest savedRequest = frame->m_terrainRequest;
  const ModelViewer::PlayerContent savedContent = frame->m_playerContent;
  ModelViewer::TerrainRequest current;
  current.load = 1000000;
  struct Case
  {
    const char * name;
    int load;
    int liveTerrainTiles;
    ModelViewer::PlayerContent expected;
  };
  const Case cases[] = {
    {"a failed load while an earlier tile is alive leaves the host taking that tile as shown", current.load, 1,
     ModelViewer::PlayerContent::Terrain},
    {"a failed load that leaves no tile leaves what is shown unknown", current.load, 0, ModelViewer::PlayerContent::Unknown},
    {"a failed report about another load changes nothing", current.load - 1, 1, ModelViewer::PlayerContent::Unknown},
  };
  for (const Case & c : cases)
  {
    frame->m_terrainRequest = current;
    frame->m_playerContent = ModelViewer::PlayerContent::Unknown;
    UnityIpcServer::TerrainTileReport report;
    report.load = c.load;
    report.status = "failed";
    report.reason = "terrain test: a made-up failure";
    report.liveTerrainTiles = c.liveTerrainTiles;
    frame->OnTerrainTileLoaded(report);
    tally.check(QString("host: ") + c.name, frame->m_playerContent == c.expected,
                QString("load %1 (current %2), liveTerrainTiles %3: the host takes %4 as shown")
                  .arg(c.load).arg(current.load).arg(c.liveTerrainTiles)
                  .arg(frame->m_playerContent == ModelViewer::PlayerContent::Terrain ? "the terrain tile" : "nothing known"));
  }
  frame->m_terrainRequest = savedRequest;
  frame->m_playerContent = savedContent;
  frame->UpdateUnityViewportState();
}

// The host's read tracker (MapAssetReadTracker, MapAssetServed::first) on made-up reads: a file's first read and the next,
// a reset (the client replaced) emptying it at once, a read under another client epoch emptying it first (the backstop:
// first reads never mix clients), and, at its real capacity, a new id untracked -- never a first read it may not be --
// and counted. No storage or player is involved.
static void checkMapAssetReadTracker(TerrainTestLog & tally)
{
  typedef MapAssetReadTracker::Read Read;
  const auto name = [](Read r) { return r == Read::First ? QString("First") : r == Read::Again ? QString("Again") : QString("Untracked"); };
  {
    MapAssetReadTracker t(UnityIpcServer::MAP_ASSET_READS_TRACKED);
    const Read a = t.note(5, 775971), b = t.note(5, 775971);
    tally.check("read tracker: a file's first read is First, the next Again", a == Read::First && b == Read::Again &&
                t.size() == 1 && t.epoch() == 5, name(a) + ", " + name(b) + QString(", %1 id(s) of epoch %2").arg(t.size()).arg(t.epoch()));
    const Read c = t.note(6, 775971);
    const size_t afterSwitch = t.size();
    const Read d = t.note(6, 778037), e = t.note(5, 775971);
    tally.check("read tracker: a read under another client epoch empties it first (no first read mixes epochs)",
                c == Read::First && afterSwitch == 1 && d == Read::First && e == Read::First && t.size() == 1 && t.epoch() == 5,
                QString("epoch 6: %1 (%2 id(s) then), another id %3; epoch 5 again: %4, %5 id(s) of epoch %6")
                  .arg(name(c)).arg(afterSwitch).arg(name(d)).arg(name(e)).arg(t.size()).arg(t.epoch()));
  }
  {
    MapAssetReadTracker t(UnityIpcServer::MAP_ASSET_READS_TRACKED);
    t.note(5, 775971);
    t.note(5, 778037);
    const size_t before = t.size();
    t.reset(6);
    const size_t afterReset = t.size();
    const int epochAfterReset = t.epoch();
    const Read a = t.note(6, 775971), b = t.note(6, 775971);
    tally.check("read tracker: a reset for a replaced client empties it at once, under the new epoch; that epoch's reads "
                "are then First, then Again",
                before == 2 && afterReset == 0 && epochAfterReset == 6 && a == Read::First && b == Read::Again &&
                  t.size() == 1 && t.epoch() == 6,
                QString("%1 id(s) of epoch 5, after reset(6) %2 id(s) of epoch %3; id 775971 under epoch 6: %4, %5; %6 id(s) "
                        "of epoch %7")
                  .arg(before).arg(afterReset).arg(epochAfterReset).arg(name(a)).arg(name(b)).arg(t.size()).arg(t.epoch()));
  }
  {
    MapAssetReadTracker t(UnityIpcServer::MAP_ASSET_READS_TRACKED);
    const int capacity = UnityIpcServer::MAP_ASSET_READS_TRACKED;
    int firsts = 0;
    for (int id = 1; id <= capacity; id++)
      firsts += t.note(1, id) == Read::First ? 1 : 0;
    const Read over = t.note(1, capacity + 1), overAgain = t.note(1, capacity + 1), kept = t.note(1, 1);
    tally.check(QString("read tracker: at its capacity (%1 ids) a new id is Untracked, again and again, and counted; a "
                        "tracked id stays Again").arg(capacity),
                firsts == capacity && over == Read::Untracked && overAgain == Read::Untracked && kept == Read::Again &&
                  t.size() == (size_t)capacity && t.untracked() == 2,
                QString("%1 First of %2; id %3: %4 then %5; id 1: %6; %7 id(s) kept, %8 untracked read(s)")
                  .arg(firsts).arg(capacity).arg(capacity + 1).arg(name(over)).arg(name(overAgain)).arg(name(kept))
                  .arg(t.size()).arg(t.untracked()));
    const Read fresh = t.note(2, capacity + 1);
    tally.check("read tracker: a new client epoch frees its capacity", fresh == Read::First && t.size() == 1,
                QString("id %1 under epoch 2: %2, %3 id(s) kept").arg(capacity + 1).arg(name(fresh)).arg(t.size()));
  }
}

// The host's getMapAsset records (UnityIpcServer::recordMapAssetServed) on made-up answers, in a server of their own:
// more than MAP_ASSET_RECORDS_KEPT go in, the most recent that many are kept in order, every one is counted and handed
// to onMapAssetServed.
static void checkMapAssetRecordRing(TerrainTestLog & tally)
{
  UnityIpcServer ring;
  const int kept = UnityIpcServer::MAP_ASSET_RECORDS_KEPT;
  const int fed = kept + 44;
  int handed = 0;
  QString lastHanded;
  ring.onMapAssetServed = [&handed, &lastHanded](const UnityIpcServer::MapAssetServed & s) {
    handed++;
    lastHanded = s.requestId;
  };
  for (int i = 1; i <= fed; i++)
  {
    UnityIpcServer::MapAssetServed s;
    s.requestId = QString("r%1").arg(i);
    s.load = i;
    s.role = i % 2 ? "wdt" : "rootAdt";
    s.state = "available";
    ring.recordMapAssetServed(s);
  }
  const std::deque<UnityIpcServer::MapAssetServed> & r = ring.mapAssetsServed();
  const QString oldest = r.empty() ? QString("-") : r.front().requestId, newest = r.empty() ? QString("-") : r.back().requestId;
  tally.check(QString("record ring: %1 answers keep the most recent %2, every one counted and handed on").arg(fed).arg(kept),
              (int)r.size() == kept && ring.mapAssetsServedTotal() == fed && ring.mapAssetRecordsDropped() == fed - kept &&
                oldest == QString("r%1").arg(fed - kept + 1) && newest == QString("r%1").arg(fed) && handed == fed &&
                lastHanded == newest,
              QString("%1 kept (%2 .. %3), total %4, dropped %5, %6 handed on (the last %7)")
                .arg(r.size()).arg(oldest).arg(newest).arg(ring.mapAssetsServedTotal()).arg(ring.mapAssetRecordsDropped())
                .arg(handed).arg(lastHanded));
}

// THE TEXTURE LIST of a protocol-13 report's material, as far as it can be judged without what the host served
// (UnityIpcServer.h TERRAIN TEXTURES): every entry a "diffuse" or a "height" texture, at most
// MAX_TERRAIN_TEXTURES_PER_LOAD of them in a state of a request (asked for, answered, or failed by the player after its
// answer) -- the rest "not requested", or the "no texture id" entry --, and at most TERRAIN_REPORT_TEXTURES_MAX entries
// sent, every one read. Empty when the list holds.
static QStringList terrainTextureListProblems(const UnityIpcServer::TerrainTileReport & r)
{
  QStringList bad;
  int requested = 0, otherRoles = 0;
  QStringList rolesNamed;   // the first few entries of another role, the player's text appended
  for (const UnityIpcServer::TerrainTileReport::Texture & t : r.textures)
  {
    if (t.state != "not requested" && t.state != "no texture id")
      requested++;
    if (t.role != "diffuse" && t.role != "height")
    {
      otherRoles++;
      if (rolesNamed.size() < 4)
        rolesNamed << QString::number(t.fileDataID) + (t.role.isEmpty() ? QString(" (none)") : " \"" + t.role + "\"");
    }
  }
  if (r.texturesSent > UnityIpcServer::TERRAIN_REPORT_TEXTURES_MAX || r.texturesSent > (int)r.textures.size())
    bad << QString("%1 texture entries sent, %2 read (at most %3)").arg(r.texturesSent).arg(r.textures.size())
             .arg(UnityIpcServer::TERRAIN_REPORT_TEXTURES_MAX);
  if (requested > UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD)
    bad << QString("%1 texture entries asked for or answered (at most %2; the rest \"not requested\" or \"no texture "
                   "id\")").arg(requested).arg(UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD);
  if (otherRoles > 0)
    bad << QString("%1 texture entr%2 with a role other than \"diffuse\" or \"height\": ").arg(otherRoles)
             .arg(otherRoles == 1 ? "y" : "ies") + rolesNamed.join(", ") +
             (otherRoles > rolesNamed.size() ? ", ..." : "");
  return bad;
}

// What the host reads of a report's lists (UnityIpcServer::handleTerrainTileLoaded), on made-up terrainTileLoaded
// messages in a server of their own, and how the material check judges a texture list (terrainTextureListProblems):
// up to TERRAIN_REPORT_TEXTURES_MAX texture entries are kept and accepted -- 200 (128 asked for, 71 "not requested", the
// "no texture id" entry), and every diffuse and height texture 256 chunks of 8 layers can name --; one more is counted,
// not kept, and fails, as do 129 entries asked for and an entry of another role or none. The retired ids and the
// chunk-state counters are kept up to TERRAIN_REPORT_LIST_MAX. No player is involved.
static void checkTerrainReportLists(TerrainTestLog & tally)
{
  UnityIpcServer server;
  UnityIpcServer::TerrainTileReport got;
  int handed = 0;
  server.onTerrainTileLoaded = [&got, &handed](const UnityIpcServer::TerrainTileReport & r) {
    got = r;
    handed++;
  };
  const int asked = UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD, most = UnityIpcServer::TERRAIN_REPORT_TEXTURES_MAX;
  const int layers = 256 * 8;   // a tile's chunks x the layers of a drawn chunk: the entries of one role at most
  const auto entry = [](int id, const QString & role, const QString & state) {
    QJsonObject t;
    t["fileDataID"] = id;
    if (!role.isEmpty())
      t["role"] = role;
    t["state"] = state;
    t["detail"] = state == "not requested" ? QString("this load may ask for at most 128 textures (maxTexturesPerLoad)")
                                           : QString();
    return t;
  };
  // n entries of a role from FileDataID `from`, the first `answered` of them "available", the rest "not requested".
  const auto append = [&entry](QJsonArray & a, int n, const QString & role, int from, int answered) {
    for (int i = 0; i < n; i++)
      a.append(entry(from + i, role, i < answered ? "available" : "not requested"));
  };
  // A built report of a made-up load, its material listing these textures, with these retired ids and counters.
  const auto feed = [&](const QJsonArray & textures, const QJsonArray & retired, const QJsonObject & counters) {
    QJsonObject material;
    material["state"] = "partial";
    material["chunkStates"] = counters;
    material["textures"] = textures;
    QJsonObject m;
    m["type"] = "terrainTileLoaded";
    m["load"] = 1;
    m["status"] = "built";
    m["requestsRetired"] = retired.size();
    m["retiredRequestIds"] = retired;
    m["material"] = material;
    got = UnityIpcServer::TerrainTileReport();
    const int before = handed;
    server.testReceiveTerrainTileLoaded(m);
    return handed == before + 1;
  };
  const auto judged = [&got]() {
    const QStringList p = terrainTextureListProblems(got);
    return QString("%1 sent, %2 kept; ").arg(got.texturesSent).arg(got.textures.size()) +
           (p.isEmpty() ? QString("accepted") : "judged: " + p.join("; "));
  };
  {
    QJsonArray a;
    append(a, 199, "diffuse", 9100000, asked);
    a.append(entry(0, "diffuse", "no texture id"));
    const bool fed = feed(a, QJsonArray(), QJsonObject());
    tally.check(QString("report lists: 200 texture entries (%1 asked for, 71 \"not requested\", the \"no texture id\" "
                        "entry) are all kept, in order, and accepted").arg(asked),
                fed && got.texturesSent == 200 && got.textures.size() == 200 && got.textures.front().fileDataID == 9100000 &&
                  got.textures.back().fileDataID == 0 && got.textures.back().state == "no texture id" &&
                  got.textures.back().role == "diffuse" && terrainTextureListProblems(got).isEmpty(),
                judged());
  }
  QJsonArray full;
  append(full, layers - 1, "diffuse", 9100000, asked);
  full.append(entry(0, "diffuse", "no texture id"));
  append(full, layers, "height", 9200000, 0);
  {
    const bool fed = feed(full, QJsonArray(), QJsonObject());
    tally.check(QString("report lists: %1 texture entries (%2 diffuse with the \"no texture id\" entry, %2 height; %3 asked "
                        "for) are all kept and accepted").arg(most).arg(layers).arg(asked),
                fed && most == 2 * layers && got.texturesSent == most && (int)got.textures.size() == most &&
                  got.textures.back().fileDataID == 9200000 + layers - 1 && got.textures.back().role == "height" &&
                  terrainTextureListProblems(got).isEmpty(),
                judged());
  }
  {
    QJsonArray over = full;
    over.append(entry(9300000, "height", "not requested"));
    const bool fed = feed(over, QJsonArray(), QJsonObject());
    const QStringList p = terrainTextureListProblems(got);
    tally.check(QString("report lists: %1 texture entries are counted, %2 kept, and the list fails").arg(most + 1).arg(most),
                fed && got.texturesSent == most + 1 && (int)got.textures.size() == most && p.size() == 1 &&
                  p.front().contains(QString("%1 texture entries sent, %2 read").arg(most + 1).arg(most)),
                judged());
  }
  {
    QJsonArray a;
    append(a, asked + 1, "diffuse", 9100000, asked + 1);
    QJsonObject other = a.at(1).toObject();
    other["role"] = "specular";
    a.replace(1, other);
    QJsonObject none = a.at(2).toObject();
    none.remove("role");
    a.replace(2, none);
    const bool fed = feed(a, QJsonArray(), QJsonObject());
    const QStringList p = terrainTextureListProblems(got);
    tally.check(QString("report lists: %1 entries asked for fail, and so do an entry of another role and one of none")
                  .arg(asked + 1),
                fed && (int)got.textures.size() == asked + 1 && got.textures[1].role == "specular" &&
                  got.textures[2].role.isEmpty() && p.size() == 2 &&
                  p.at(0).startsWith(QString("%1 texture entries asked for or answered").arg(asked + 1)) &&
                  p.at(1).startsWith("2 texture entries with a role other than"),
                judged());
  }
  {
    const int n = UnityIpcServer::TERRAIN_REPORT_LIST_MAX + 72;
    QJsonArray retired;
    QJsonObject counters;
    for (int i = 0; i < n; i++)
    {
      retired.append(QString("u%1").arg(i + 1));
      counters[QString("c%1").arg(i, 3, 10, QChar('0'))] = i;
    }
    const int keep = UnityIpcServer::TERRAIN_REPORT_LIST_MAX;
    const bool fed = feed(QJsonArray(), retired, counters);
    tally.check(QString("report lists: %1 retired ids and %1 chunk-state counters are counted, the first %2 of each kept")
                  .arg(n).arg(keep),
                fed && got.requestsRetired == n && got.retiredRequestIds.size() == keep &&
                  got.retiredRequestIds.back() == QString("u%1").arg(keep) && (int)got.chunkStates.size() == keep &&
                  got.chunkStates.front().first == "c000" && got.texturesSent == 0 && got.textures.empty(),
                QString("requestsRetired %1, %2 ids kept (the last %3), %4 counters kept (the first %5), %6 texture entries")
                  .arg(got.requestsRetired).arg(got.retiredRequestIds.size())
                  .arg(got.retiredRequestIds.isEmpty() ? QString("-") : got.retiredRequestIds.back())
                  .arg(got.chunkStates.size()).arg(got.chunkStates.empty() ? QString("-") : got.chunkStates.front().first)
                  .arg(got.texturesSent));
  }
}

static int doHeadlessTerrainTileTest(ModelViewer * frame, int mapId, int tileX, int tileY, const QString & outPath)
{
  TerrainTestLog tally;
  QJsonObject out;
  const QString sequenceSpec = qEnvironmentVariable("WMV_TERRAINTEST_SEQUENCE").trimmed();
  out["map"] = mapId;
  out["tileX"] = tileX;
  out["tileY"] = tileY;
  out["sequence"] = sequenceSpec;
  const std::shared_ptr<wow::WorldMapCatalog> startCatalog = frame->mapCatalog();
  {
    QJsonObject client;
    client["product"] = startCatalog ? startCatalog->client().product : QString();
    client["build"] = startCatalog ? startCatalog->client().build : QString();
    out["client"] = client;
  }
  LOG_INFO << "[terrain-test] map" << mapId << "tile" << QString("%1_%2").arg(tileX).arg(tileY) << "client"
           << (startCatalog ? startCatalog->client().product + " " + startCatalog->client().build : QString("none"))
           << "sequence:" << (sequenceSpec.isEmpty() ? QString("none") : sequenceSpec);

  // WHAT THE RUN KEEPS is bounded, since the memory it samples is its own process's: the most recent REPORTS_KEPT reports
  // (their messages dropped; the server reads at most TERRAIN_REPORT_TEXTURES_MAX texture entries into each), the most
  // recent SERVED_KEPT getMapAsset records, per load serial up to the highest one sent two bytes (how often it was
  // answered, how first) -- a report naming a load never sent is counted apart, and fails a check --, the distinct files
  // served per client epoch, and at most TIMING_SAMPLES_KEPT times per timing group (the rest counted). Every full
  // report, record and load goes to <out.json>.records.jsonl as it arrives, and is read back into <out.json> once the
  // last sample is taken.
  // (A load has at most 3 + 128 reads and a few refusals: the records of the last loads fit SERVED_KEPT.)
  const size_t REPORTS_KEPT = 64, SERVED_KEPT = 512, STATES_KEPT = 64;
  const size_t TIMING_SAMPLES_KEPT = 65536;
  std::deque<UnityIpcServer::TerrainTileReport> reports;   // the most recent, message cleared
  long long reportsTotal = 0;
  std::vector<unsigned char> answerCount;                   // by load serial: reports received, up to 255
  std::vector<char> answerFirst;                            // ... the first one's status: b(uilt) f s c, ? other
  long long reportsForUnsentLoads = 0;                      // reports naming a load this run never sent
  QStringList unsentLoadsNamed;                             // ... the first few of those loads, for the check's detail
  long long retiredReported = 0;                            // requestsRetired summed over every report
  std::vector<UnityIpcServer::RuntimeState> states;         // the most recent STATES_KEPT answers (waitRuntimeState)
  QJsonArray samplesJson;
  std::vector<int> loadsSent;
  std::vector<int> loadsOrphaned;   // in flight when the player was restarted: no player may answer them
  // The getMapAsset records answered in this run, numbered in order (the server keeps only its own most recent).
  struct ServedEntry
  {
    long long seq;
    UnityIpcServer::MapAssetServed s;
  };
  std::deque<ServedEntry> servedRecent;
  long long servedTotal = 0;
  std::set<std::pair<int, int>> filesServed;   // (client epoch, FileDataID) of every file answered "available"
  int lastAvailableEpoch = 0;
  long long servedBeforeRestarts = 0;   // the server's answer count of each player before a restart (start() zeroes it)
  std::vector<long long> privateSeries; // the host's private bytes at every sample
  // Timing samples, by group (role, and first read apart).
  struct Samples
  {
    std::vector<double> v;
    long long n = 0;
  };
  std::map<QString, Samples> timing;
  const auto addTiming = [&timing, TIMING_SAMPLES_KEPT](const QString & group, double value) {
    if (value < 0.0)
      return;
    Samples & s = timing[group];
    s.n++;
    if (s.v.size() < TIMING_SAMPLES_KEPT)
      s.v.push_back(value);
  };
  long long pollsStopped = 0, pollBytesMax = 0;
  int pollWaitingMax = 0;
  // The records file (none without <out.json>).
  QFile recordsFile(outPath.isEmpty() ? QString() : outPath + ".records.jsonl");
  const bool recording = !outPath.isEmpty() && recordsFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
  const auto record = [&recordsFile, recording](const char * kind, QJsonObject o) {
    if (!recording)
      return;
    o["recordKind"] = QString::fromLatin1(kind);
    recordsFile.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    recordsFile.write("\n");
  };
  // The timeout step's genuine late answer: the held requests are released when the player's report of this load arrives,
  // before the host takes the load as ended (TERRAIN TEXTURES), so the file goes back "available" to a load already over.
  int releaseOnReportOf = 0, releasedOnReport = -1;
  UnityIpcServer * ipc = nullptr;
  std::function<void(const UnityIpcServer::TerrainTileReport &)> previousTerrain;
  std::function<void(const UnityIpcServer::RuntimeState &)> previousRuntime;
  std::function<void(const UnityIpcServer::MapAssetServed &)> previousServed;
  std::function<void(const UnityIpcServer::MapAssetPoll &)> previousPoll;

  // THE IN-PROCESS CHECKS, before any player: the decision, the host's bookkeeping of a failed load, its read tracker and
  // record ring, and the size limit of the catalogue's read. (That a refused file is never read is shown where the reads
  // can be counted: scripts/tests/casc-whole-read, case 9.)
  checkMapAssetDecisions(tally);
  checkMapAssetAccounting(tally);
  checkMapAssetKinds(tally);
  checkMapAssetQueue(tally);
  checkFailedLoadBookkeeping(frame, tally);
  checkMapAssetReadTracker(tally);
  checkMapAssetRecordRing(tally);
  checkTerrainReportLists(tally);
  checkMapAssetReads(frame, tally);
  {
    const QString tooLargeCheck = "inspectFile with a 1000-byte limit refuses the WDT, size and limit named, no bytes handed back";
    const wow::MapEntry * entry = startCatalog ? startCatalog->find(mapId) : nullptr;
    if (!entry || !startCatalog->inspect(mapId) || entry->inspection.state != wow::WdtAvailability::Available)
      tally.check("the map's WDT can be read", false,
                !entry ? QString("map %1 is not in the catalogue").arg(mapId)
                       : wow::WorldMapCatalog::availabilityName(entry->inspection.state) + ": " + entry->inspection.detail +
                           startCatalog->lastRefusal());
    else if (entry->inspection.bytes <= 1000)
      tally.skip(tooLargeCheck, QString("the WDT is %1 bytes").arg(entry->inspection.bytes));
    else
    {
      const qint64 size = entry->inspection.bytes;
      QByteArray bytes("left from before");
      const wow::WdtInspection tooLarge = startCatalog->inspectFile(entry->wdt, &bytes, 1000);
      const QString expected = QString("is larger than this read allows (%1 bytes, the limit is 1000)").arg(size);
      tally.check(tooLargeCheck, tooLarge.state == wow::WdtAvailability::Unreadable &&
                  tooLarge.detail.contains(expected) && bytes.isEmpty() && tooLarge.bytes < 0,
                wow::WorldMapCatalog::availabilityName(tooLarge.state) + ": " + tooLarge.detail +
                  QString(" (%1 bytes handed back)").arg(bytes.size()));
      const wow::WdtInspection whole = startCatalog->inspectFile(entry->wdt, &bytes, UnityIpcServer::MAX_MAP_ASSET_BYTES);
      tally.check("inspectFile at the map-file limit reads the WDT whole", whole.state == wow::WdtAvailability::Available &&
                  bytes.size() == size, wow::WorldMapCatalog::availabilityName(whole.state) +
                  QString(", %1 of %2 bytes").arg(bytes.size()).arg(size));
      const wow::WdtInspection unchanged = startCatalog->inspectFile(entry->wdt, &bytes);
      tally.check("inspectFile at its default limit reads the WDT whole", unchanged.state == wow::WdtAvailability::Available &&
                  bytes.size() == size, wow::WorldMapCatalog::availabilityName(unchanged.state) +
                  QString(", %1 of %2 bytes").arg(bytes.size()).arg(size));
    }
  }

  // The tile and WDT a load would name (ModelViewer::ResolveTerrainTile, the checks RequestTerrainTile makes after the
  // player's), for this client's family: Map.db2's FileDataID, or the storage root's FileDataID for the directory path.
  {
    UnityIpcServer::TerrainTileCommand command;
    QString why;
    const wow::MapEntry * entry = startCatalog ? startCatalog->find(mapId) : nullptr;
    const bool resolved = frame->ResolveTerrainTile(mapId, tileX, tileY, command, why);
    QStringList bad;
    if (!resolved)
      bad << why;
    else if (!entry)
      bad << "the map is not in the catalogue";
    else if (entry->wdt.kind == wow::WdtReference::Kind::FileDataId)
    {
      if (command.wdtResolvedBy != "fileDataID" || command.wdtFileDataID != entry->wdt.fileDataId || !command.wdtPath.isEmpty())
        bad << "not Map.db2's FileDataID";
    }
    else if (command.wdtResolvedBy != "path" || command.wdtFileDataID <= 0 ||
             command.wdtFileDataID != entry->inspection.storageFileDataId || command.wdtPath != entry->wdt.path)
      bad << "not the storage root's FileDataID for the path";
    tally.check("the load names the map's WDT as this client resolves it", bad.isEmpty(),
                QString("WDT %1 resolved by %2").arg(command.wdtFileDataID).arg(command.wdtResolvedBy) +
                  (command.wdtPath.isEmpty() ? QString() : " " + command.wdtPath) +
                  (startCatalog ? QString(" (catalogue rule: %1)").arg(startCatalog->wdtRule() == wow::WorldMapCatalog::WdtRule::ByFileDataId
                                                                         ? "by FileDataID" : "by directory path")
                                : QString()) +
                  (bad.isEmpty() ? QString() : "; " + bad.join("; ")));
    tally.check("a tile outside the grid is refused", !frame->ResolveTerrainTile(mapId, 64, tileY, command, why) &&
                  why.contains("outside"), why);
    tally.check("a map not in Map.db2 is refused", !frame->ResolveTerrainTile(-12345, tileX, tileY, command, why) &&
                  why.contains("not in this client's Map.db2"), why);
    const wow::MapEntry * noWdt = nullptr;
    if (startCatalog)
      for (const wow::MapEntry & e : startCatalog->maps())
        if (!noWdt && e.wdt.kind == wow::WdtReference::Kind::Unresolved)
          noWdt = &e;
    if (noWdt)
      tally.check(QString("map %1, which has no WDT, is refused").arg(noWdt->id),
                  !frame->ResolveTerrainTile(noWdt->id, tileX, tileY, command, why) && why.contains("has no WDT"), why);
    else
      tally.skip("a map without a WDT is refused", "every map of this client has a WDT reference");
  }

  // Everything from the player on; the results are written and the player shut down however it ends.
  [&]() {
    LOG_INFO << "[terrain-test] starting the player:" << QString::fromWCharArray(UnityRendererHost::resolveUnityExePath().c_str());
    if (!frame->unityRendererHost || !frame->StartUnityRenderer(/* selfTest */ true))
    {
      tally.check("the player starts", false,
                QString::fromWCharArray(frame->unityRendererHost ? frame->unityRendererHost->playerProblem().c_str() : L"no viewport"));
      return;
    }
    ipc = frame->unityRendererHost->ipc();
    if (!ipc || !ipc->isListening())
    {
      tally.check("the IPC server listens", false, QString());
      ipc = nullptr;
      return;
    }
    previousTerrain = ipc->onTerrainTileLoaded;
    previousRuntime = ipc->onRuntimeState;
    ipc->onTerrainTileLoaded = [&, previousTerrain](const UnityIpcServer::TerrainTileReport & r) {
      if (releaseOnReportOf > 0 && r.load == releaseOnReportOf)
      {
        releaseOnReportOf = 0;
        releasedOnReport = ipc->testReleaseHeldMapAssets();
      }
      reportsTotal++;
      retiredReported += std::max(0, r.requestsRetired);
      // The per-load tables go up to the highest serial sent, never further: serials are the host's, raised for every load
      // sent, and request() records a load before any pump can deliver its report. A report naming a load never sent (a
      // made-up or misrouted one) is counted apart.
      const int highestSent = loadsSent.empty() ? 0 : loadsSent.back();
      if (r.load > 0 && r.load <= highestSent && std::binary_search(loadsSent.begin(), loadsSent.end(), r.load))
      {
        if ((size_t)highestSent >= answerCount.size())
        {
          answerCount.resize((size_t)highestSent + 1, 0);
          answerFirst.resize((size_t)highestSent + 1, 0);
        }
        if (answerCount[(size_t)r.load] == 0)
          answerFirst[(size_t)r.load] = r.status == "built" ? 'b' : r.status == "failed" ? 'f' : r.status == "superseded" ? 's'
                                        : r.status == "cleared" ? 'c' : '?';
        if (answerCount[(size_t)r.load] < 255)
          answerCount[(size_t)r.load]++;
      }
      else
      {
        reportsForUnsentLoads++;
        if (unsentLoadsNamed.size() < 8)
          unsentLoadsNamed << QString::number(r.load);
        LOG_ERROR << "[terrain-test] a terrainTileLoaded names load" << r.load << "(status" << r.status
                  << "), which this run never sent (the highest sent:" << highestSent << ")";
      }
      record("report", r.message);
      UnityIpcServer::TerrainTileReport kept = r;
      kept.message = QJsonObject();
      reports.push_back(kept);
      while (reports.size() > REPORTS_KEPT)
        reports.pop_front();
      if (previousTerrain)
        previousTerrain(r);
    };
    ipc->onRuntimeState = [&states, previousRuntime, STATES_KEPT](const UnityIpcServer::RuntimeState & s) {
      states.push_back(s);
      if (states.size() > 2 * STATES_KEPT)
        states.erase(states.begin(), states.end() - STATES_KEPT);
      if (previousRuntime)
        previousRuntime(s);
    };
    previousServed = ipc->onMapAssetServed;
    ipc->onMapAssetServed = [&, previousServed](const UnityIpcServer::MapAssetServed & s) {
      record("served", servedJson(s));
      servedRecent.push_back({servedTotal++, s});
      while (servedRecent.size() > SERVED_KEPT)
        servedRecent.pop_front();
      if (s.state == "available")
      {
        filesServed.insert(std::make_pair(s.epochNow, s.fileDataID));
        lastAvailableEpoch = s.epochNow;
        // Timing by role, the first read of each file apart (MapAssetServed::first); the largest files (over 4 MiB, the
        // 5,593,604-byte textures) also on their own.
        const QString group = s.role + (s.first ? " first" : "");
        addTiming(group + " handlerMs", s.handlerMs);
        addTiming(group + " readMs", s.readMs);
        addTiming(group + " queuedMs", s.queuedMs);
        if (s.bytes > 4 * 1024 * 1024)
        {
          addTiming("over 4 MiB handlerMs", s.handlerMs);
          addTiming("over 4 MiB readMs", s.readMs);
          addTiming("over 4 MiB queuedMs", s.queuedMs);
        }
      }
      else
        addTiming("no file queuedMs", s.queuedMs);
      if (previousServed)
        previousServed(s);
    };
    previousPoll = ipc->onMapAssetPoll;
    ipc->onMapAssetPoll = [&, previousPoll](const UnityIpcServer::MapAssetPoll & p) {
      addTiming("poll serveMs", p.serveMs);
      pollsStopped += p.stoppedForBudget ? 1 : 0;
      pollBytesMax = std::max(pollBytesMax, p.bytesRead);
      pollWaitingMax = std::max(pollWaitingMax, p.waitingAfter);
      if (previousPoll)
        previousPoll(p);
    };
    wxStopWatch ready;
    while (!ipc->isUnityReady() && ready.Time() < 60000 && frame->unityRendererHost->isRunning())
      pumpIpc(ipc, 1);
    tally.check("the player announces itself", ipc->isUnityReady(),
              QString("protocol %1 after %2 ms").arg(ipc->playerProtocolVersion()).arg(ready.Time()));
    out["playerProtocol"] = ipc->playerProtocolVersion();
    if (!ipc->isUnityReady())
      return;
    if (!ipc->playerDrawsTerrain())
    {
      tally.check("player too old", false, QString("the player speaks protocol %1; drawing terrain needs protocol 12")
                                           .arg(ipc->playerProtocolVersion()));
      return;
    }
    // A protocol-12 player draws geometry alone: it is offered no textures, asks for none and reports no material, and
    // every textured check is skipped with that reason. Its retired ring holds 32 requests, a protocol-13 player's 128
    // (WmvTerrainSession.RetiredCapacity).
    const bool texturedPlayer = ipc->playerDrawsTerrainTextures();
    const QString noTexturesWhy = QString("the player speaks protocol %1: it draws no terrain textures (protocol 13)")
                                    .arg(ipc->playerProtocolVersion());
    LOG_INFO << "[terrain-test]" << (texturedPlayer ? QString("the player draws terrain textures") : noTexturesWhy);

    // Every sample of the player goes to <out.json> with the host's own memory and map-file bookkeeping beside it: its
    // private bytes and working set, the records kept and counted, the files tracked, the requests held.
    const auto appendSample = [&](const QString & label, const UnityIpcServer::RuntimeState & s) {
      QJsonObject o = runtimeJson(label, s);
      long long privateBytes = -1, workingSet = -1;
      processMemory(privateBytes, workingSet);
      privateSeries.push_back(privateBytes);
      const MapAssetReadTracker & reads = ipc->mapAssetReads();
      o["hostPrivateBytes"] = (double)privateBytes;
      o["hostWorkingSetBytes"] = (double)workingSet;
      o["mapAssetRecordsKept"] = (int)ipc->mapAssetsServed().size();
      o["mapAssetsServedTotal"] = (double)ipc->mapAssetsServedTotal();
      o["mapAssetRecordsDropped"] = (double)ipc->mapAssetRecordsDropped();
      o["mapAssetReadsTracked"] = (int)reads.size();
      o["mapAssetReadsEpoch"] = reads.epoch();
      o["mapAssetReadsUntracked"] = (double)reads.untracked();
      o["mapAssetsHeld"] = (int)ipc->mapAssetsHeld().size();
      o["mapAssetsHeldDropped"] = (double)ipc->mapAssetsHeldDropped();
      o["mapAssetsWaiting"] = (int)ipc->mapAssetsWaiting();
      o["mapAssetsQueued"] = (double)ipc->mapAssetsQueued();
      o["mapAssetsRefusedQueueFull"] = (double)ipc->mapAssetsRefusedQueueFull();
      o["mapAssetsQueueDropped"] = (double)ipc->mapAssetsQueueDropped();
      o["mapAssetPollsKept"] = (int)ipc->mapAssetPolls().size();
      o["mapAssetRequestsReceived"] = ipc->stats().mapAssetRequests;
      o["servedCopy"] = (double)servedTotal;
      o["servedKept"] = (int)servedRecent.size();
      o["reportsKept"] = (int)reports.size();
      o["statesKept"] = (int)states.size();
      samplesJson.append(o);
      // The label is appended, never passed through arg().
      LOG_INFO << "[terrain-test] memory " + label +
                    QString(": host private %1 bytes, working set %2 bytes; map records kept %3, served %4 since the player "
                            "started, %5 no longer kept; files tracked %6 (epoch %7, %8 untracked); held %9")
                      .arg(privateBytes).arg(workingSet).arg(ipc->mapAssetsServed().size()).arg(ipc->mapAssetsServedTotal())
                      .arg(ipc->mapAssetRecordsDropped()).arg(reads.size()).arg(reads.epoch()).arg(reads.untracked())
                      .arg(ipc->mapAssetsHeld().size()) +
                    QString(" (%1 dropped unanswered); waiting %2, poll records %3; the test keeps %4 of %5 records, %6 "
                            "reports, %7 states")
                      .arg(ipc->mapAssetsHeldDropped()).arg(ipc->mapAssetsWaiting()).arg(ipc->mapAssetPolls().size())
                      .arg(servedRecent.size()).arg(servedTotal).arg(reports.size()).arg(states.size());
    };
    // What the player holds, two questions in turn: the second is answered in a frame after the one the first was.
    const auto sample = [&](const QString & label, UnityIpcServer::RuntimeState & s) {
      bool answered = false;
      const bool got = waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState &) { return true; }, 5000, s, answered) &&
                       waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState &) { return true; }, 5000, s, answered);
      appendSample(label, s);
      return got;
    };
    // One question only, for the steps that ask after every load.
    const auto sampleOnce = [&](const QString & label, UnityIpcServer::RuntimeState & s) {
      bool answered = false;
      waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState &) { return true; }, 5000, s, answered);
      appendSample(label, s);
      return answered;
    };
    const auto reportsFor = [&answerCount](int load) {
      return load > 0 && (size_t)load < answerCount.size() ? (int)answerCount[(size_t)load] : 0;
    };
    // The report of a load: the first one received (an answer counted twice fails the "answered once" checks anyway).
    const auto waitReport = [&](int load, long limitMs, UnityIpcServer::TerrainTileReport & outReport) {
      wxStopWatch w;
      while (true)
      {
        for (const UnityIpcServer::TerrainTileReport & r : reports)
          if (r.load == load)
          {
            outReport = r;
            return true;
          }
        if (reportsFor(load) > 0)
          return false;   // it came, but so long ago that it is no longer kept: a step waits for its own loads only
        if (w.Time() >= limitMs || !ipc->isUnityReady())
          return false;
        pumpIpc(ipc, 1);
      }
    };
    const auto lastServed = [&servedRecent](int load, const QString & role) -> const UnityIpcServer::MapAssetServed * {
      for (auto it = servedRecent.rbegin(); it != servedRecent.rend(); ++it)
        if (it->s.load == load && it->s.role == role)
          return &it->s;
      return nullptr;
    };
    // Every record kept of a load, in order (a load has at most 3 + 128 reads and a few refusals: far fewer than are kept).
    const auto servedFor = [&servedRecent](int load) {
      std::vector<UnityIpcServer::MapAssetServed> out;
      for (const ServedEntry & e : servedRecent)
        if (e.s.load == load)
          out.push_back(e.s);
      return out;
    };
    const auto request = [&](int m, int x, int y, const QString & step, QString & why) {
      const int load = frame->RequestTerrainTile(m, x, y, why);
      if (load > 0)
      {
        loadsSent.push_back(load);
        QJsonObject l;
        l["load"] = load;
        l["mapId"] = m;
        l["tileX"] = x;
        l["tileY"] = y;
        l["step"] = step;
        l["assetEpoch"] = UnityAssetAccess::clientEpoch();
        l["product"] = frame->mapCatalog() ? frame->mapCatalog()->client().product : QString();
        l["build"] = frame->mapCatalog() ? frame->mapCatalog()->client().build : QString();
        l["textures"] = frame->m_terrainRequest.maxTextureBytes > 0;
        record("load", l);
      }
      return load;
    };
    // THE MATERIAL of a built load (TERRAIN TEXTURES). A protocol-12 player: no material, and nothing but the WDT and the
    // root served for the load. A protocol-13 player: the material reported, its state one of the four, no request retired
    // by a load that was built, what the host served for the load within its limits (one tex0 read, each texture id read
    // once, at most 128 of them and 128 MiB), the tex0 the report names the one served (FileDataID, bytes, md5) -- or the
    // material "unavailable", its reason quoting the host's state, when none was --, every texture the host answered named
    // in the report (a diffuse or a height texture: the host serves both as role "texture"), each entry of its id: a file
    // served with its md5 and bytes (or a state of the player's own: md5 mismatch, decode failed), any other answer quoted
    // verbatim, state and detail; the list itself sound (terrainTextureListProblems: roles, at most 128 entries asked for,
    // at most TERRAIN_REPORT_TEXTURES_MAX in all); a "textured" material with no texture missing; the chunk-state counters
    // (the stage 04 states, in precedence order) summing to the chunks.
    const QStringList chunkStateNames = {"malformed", "alphaUnsupported", "layerCountUnsupported", "layerEffectUnresolved",
                                         "heightTextureMissing", "noLayers", "layerEffectStatic", "missingTexture",
                                         "textured"};
    const QStringList materialStates = {"textured", "partial", "unavailable", "unsupported"};
    const QStringList playerTextureStates = {"no texture id", "not requested", "md5 mismatch", "decode failed",
                                             "transport failure"};
    QString lastMaterialState;   // the material of the last load checked (protocol 13)
    const auto checkMaterial = [&](const QString & label, int load, const UnityIpcServer::TerrainTileReport & r) {
      lastMaterialState = r.hasMaterial ? r.materialState : QString();
      const std::vector<UnityIpcServer::MapAssetServed> served = servedFor(load);
      int tex0Reads = 0, textureRecords = 0;
      long long textureBytesRead = 0;
      std::map<int, int> textureReads;   // id -> requests that went on to the read
      const UnityIpcServer::MapAssetServed * tex0Available = nullptr;
      const UnityIpcServer::MapAssetServed * tex0Any = nullptr;
      const auto wentToRead = [](const UnityIpcServer::MapAssetServed & s) { return s.state != "refused" && s.state != "stale"; };
      for (const UnityIpcServer::MapAssetServed & s : served)
        if (s.role == "tex0Adt")
        {
          tex0Any = &s;
          tex0Reads += wentToRead(s) ? 1 : 0;
          if (s.state == "available")
            tex0Available = &s;
        }
        else if (s.role == "texture")
        {
          textureRecords++;
          textureBytesRead += s.bytesRead;
          if (wentToRead(s))
            textureReads[s.fileDataID]++;
        }
      if (!texturedPlayer)
      {
        tally.check(label + QString(": load %1 has no material and was served its two files alone (a protocol-12 player)").arg(load),
                    !r.hasMaterial && r.requestsRetired < 0 && tex0Any == nullptr && textureRecords == 0,
                    QString("material %1, requestsRetired %2, %3 tex0 and %4 texture answer(s)").arg(r.hasMaterial ? "sent" : "none")
                      .arg(r.requestsRetired).arg(tex0Any ? 1 : 0).arg(textureRecords));
        return;
      }
      QStringList bad;
      if (!r.hasMaterial)
        bad << "no material in the report";
      else if (!materialStates.contains(r.materialState))
        bad << "material state \"" + r.materialState + "\"";
      if (r.requestsRetired != 0)
        bad << QString("requestsRetired %1 (a built load has nothing out)").arg(r.requestsRetired);
      if (tex0Reads > 1)
        bad << QString("%1 tex0 reads").arg(tex0Reads);
      int repeats = 0;
      for (const auto & t : textureReads)
        repeats += t.second > 1 ? 1 : 0;
      if (repeats || (int)textureReads.size() > UnityIpcServer::MAX_TERRAIN_TEXTURES_PER_LOAD ||
          textureBytesRead > UnityIpcServer::MAX_TERRAIN_TEXTURE_BYTES_PER_LOAD)
        bad << QString("host limits broken: %1 id(s) read more than once, %2 ids read, %3 texture bytes read").arg(repeats)
                 .arg(textureReads.size()).arg(textureBytesRead);
      if (r.hasMaterial)
      {
        if (tex0Available)
        {
          if (r.tex0FileDataID != tex0Available->fileDataID || r.tex0Bytes != tex0Available->bytes ||
              r.tex0Md5.compare(tex0Available->md5, Qt::CaseInsensitive) != 0)
            bad << QString("tex0 served %1 (%2 bytes, md5 %3), reported %4 (%5 bytes, md5 %6)").arg(tex0Available->fileDataID)
                     .arg(tex0Available->bytes).arg(tex0Available->md5).arg(r.tex0FileDataID).arg(r.tex0Bytes).arg(r.tex0Md5);
          if (r.materialState == "unavailable")
            bad << "material \"unavailable\" although the tex0 was served";
        }
        else
        {
          if (r.materialState != "unavailable")
            bad << "material \"" + r.materialState + "\" although no tex0 was served";
          if (tex0Any && !r.materialReason.contains(tex0Any->state))
            bad << "the reason \"" + r.materialReason + "\" does not quote the host's tex0 answer \"" + tex0Any->state + "\"";
        }
        // Every texture the host answered for the load, in the report as the host answered it -- each entry of its id, a
        // diffuse and a height texture being one file to the host (both role "texture"). An id asked for more than once (a
        // stand-in player's deliberate repeat, refused) is judged by its answer that went on to the read, else its last.
        // (Texts are filled in one pass, so a player's text is never read for a placeholder.)
        const bool complete = r.texturesSent == (int)r.textures.size();
        int matched = 0, failures = 0;
        std::map<int, const UnityIpcServer::MapAssetServed *> effective;
        for (const UnityIpcServer::MapAssetServed & s : served)
          if (s.role == "texture" && (!effective.count(s.fileDataID) || wentToRead(s) || !wentToRead(*effective[s.fileDataID])))
            effective[s.fileDataID] = &s;
        for (const auto & e : effective)
        {
          const UnityIpcServer::MapAssetServed & s = *e.second;
          const QString id = QString::number(s.fileDataID);
          int entries = 0;
          for (const UnityIpcServer::TerrainTileReport::Texture & t : r.textures)
          {
            if (t.fileDataID != s.fileDataID)
              continue;
            entries++;
            if (s.state == "available" && t.state == "available")
            {
              if (t.md5.compare(s.md5, Qt::CaseInsensitive) != 0 || t.bytes != s.bytes)
                bad << QString("texture %1 (%2) served %3 bytes md5 %4, reported %5 bytes md5 %6")
                         .arg(id, t.role, QString::number(s.bytes), s.md5, QString::number(t.bytes), t.md5);
              else
                matched++;
            }
            else if (s.state == "available" && playerTextureStates.contains(t.state))
              failures++;   // served, but the player's own failure (decode, md5): its state, not the host's
            else if (t.state != s.state || t.detail != s.detail)
              bad << QString("texture %1 (%2): host \"%3\" \"%4\", reported \"%5\" \"%6\"")
                       .arg(id, t.role, s.state, s.detail, t.state, t.detail);
            else
              failures++;
          }
          if (!entries && complete)
            bad << QString("texture %1 (host: %2) is not in the report").arg(id, s.state);
        }
        for (const UnityIpcServer::TerrainTileReport::Texture & t : r.textures)
          if (r.materialState == "textured" && t.state != "available")
            bad << QString("material \"textured\" with texture %1 (%2) \"%3\"")
                     .arg(QString::number(t.fileDataID), t.role, t.state);
        bad << terrainTextureListProblems(r);
        // The section 1 counters: each chunk in exactly one state.
        int sum = 0;
        QStringList missing;
        for (const QString & name : chunkStateNames)
        {
          const int n = r.chunkState(name);
          if (n < 0)
            missing << name;
          else
            sum += n;
        }
        if (!missing.isEmpty())
          bad << "chunk states not reported: " + missing.join(", ");
        else if (r.chunks >= 0 && sum != r.chunks && r.materialState != "unavailable")
          bad << QString("the chunk states sum to %1, the tile has %2 chunks").arg(sum).arg(r.chunks);
        if (r.materialState == "textured" && r.chunkState("missingTexture") > 0)
          bad << QString("material \"textured\" with %1 missingTexture chunk(s)").arg(r.chunkState("missingTexture"));
        tally.check(label + QString(": load %1 material \"%2\" agrees with what the host served").arg(load).arg(r.materialState),
                    bad.isEmpty(),
                    bad.isEmpty() ? QString("tex0 %1 (%2 bytes); %3 texture(s) reported, %4 served and matched, %5 failed as "
                                            "answered; %6 texture bytes read; reason \"%7\"")
                                      .arg(r.tex0FileDataID).arg(r.tex0Bytes).arg(r.texturesSent).arg(matched).arg(failures)
                                      .arg(textureBytesRead).arg(r.materialReason)
                                  : bad.join("; "));
        return;
      }
      tally.check(label + QString(": load %1 reports its material").arg(load), false, bad.join("; "));
    };
    // A built load: its report names the tile, the files it parsed are those served, one tile is alive.
    const auto checkBuilt = [&](const QString & label, int load, int m, int x, int y) {
      UnityIpcServer::TerrainTileReport r;
      if (!waitReport(load, 60000, r))
      {
        tally.check(label + QString(": load %1 built").arg(load), false, "no terrainTileLoaded within 60 s");
        return false;
      }
      QStringList bad;
      if (r.status != "built")
        bad << "status " + r.status + " (" + r.reason + (r.hostState.isEmpty() ? QString() : "; host " + r.hostState + ": " + r.hostDetail) + ")";
      if (r.mapId != m || r.tileX != x || r.tileY != y)
        bad << QString("the report names map %1 tile %2_%3").arg(r.mapId).arg(r.tileX).arg(r.tileY);
      const UnityIpcServer::MapAssetServed * wdt = lastServed(load, "wdt");
      const UnityIpcServer::MapAssetServed * root = lastServed(load, "rootAdt");
      if (!wdt || wdt->state != "available")
        bad << "no WDT was served for the load";
      else if (wdt->fileDataID != r.wdtFileDataID || wdt->md5.compare(r.wdtMd5, Qt::CaseInsensitive) != 0)
        bad << QString("WDT served %1 md5 %2, parsed %3 md5 %4").arg(wdt->fileDataID).arg(wdt->md5).arg(r.wdtFileDataID).arg(r.wdtMd5);
      if (!root || root->state != "available")
        bad << "no root ADT was served for the load";
      else if (root->fileDataID != r.rootFileDataID || root->md5.compare(r.rootMd5, Qt::CaseInsensitive) != 0)
        bad << QString("root served %1 md5 %2, parsed %3 md5 %4").arg(root->fileDataID).arg(root->md5).arg(r.rootFileDataID).arg(r.rootMd5);
      if (r.liveTerrainTiles != 1)
        bad << QString("liveTerrainTiles %1, expected 1").arg(r.liveTerrainTiles);
      tally.check(label + QString(": load %1 map %2 tile %3_%4 built").arg(load).arg(m).arg(x).arg(y), bad.isEmpty(),
                bad.isEmpty() ? QString("root %1, %2/%3 chunks, %4 vertices, %5 triangles, %6 hole cells, totalMs %7")
                                  .arg(r.rootFileDataID).arg(r.chunksBuilt).arg(r.chunks).arg(r.vertices).arg(r.triangles)
                                  .arg(r.holeCells).arg(r.totalMs, 0, 'f', 1)
                              : bad.join("; "));
      checkMaterial(label, load, r);
      return bad.isEmpty();
    };
    const auto expectFailed = [&](const QString & label, int load, const QString & hostState, const QString & text) {
      UnityIpcServer::TerrainTileReport r;
      if (!waitReport(load, 60000, r))
      {
        tally.check(label, false, QString("no terrainTileLoaded for load %1 within 60 s").arg(load));
        return r;
      }
      const QString all = r.reason + " | " + r.hostState + " | " + r.hostDetail;
      tally.check(label, r.status == "failed" && (hostState.isEmpty() || r.hostState == hostState) && all.contains(text),
                QString("load %1 status %2: ").arg(load).arg(r.status) + all);
      return r;
    };
    // The player's object counts: every one reported (an absent one reads -1 and must not pass for a count), and what the
    // terrain itself holds -- nothing with no tile, the tile's own Meshes, Materials and GameObjects while it is shown.
    const auto counted = [](const UnityIpcServer::RuntimeState & s) {
      return s.meshObjects >= 0 && s.materialObjects >= 0 && s.liveTerrainTiles >= 0 && s.terrainMeshes >= 0 &&
             s.terrainMaterials >= 0 && s.terrainGameObjects >= 0;
    };
    const auto noTerrainObjects = [&counted](const UnityIpcServer::RuntimeState & s) {
      return counted(s) && s.terrainMeshes == 0 && s.terrainMaterials == 0 && s.terrainGameObjects == 0;
    };
    const auto tileObjects = [&counted](const UnityIpcServer::RuntimeState & s) {
      return counted(s) && s.terrainMeshes > 0 && s.terrainMaterials > 0 && s.terrainGameObjects > 0;
    };
    // The view the player was asked for: -wmvTerrainView=<name> in WMV_DEBUG, which the player inherits and reads the same
    // way (WmvTerrainDebug.Parse: an exact name, the last one given wins, an unknown one ignored). The stage 03 views and
    // the materials view upload no texture or alpha array whatever the material; the textured views, and no view given
    // (a textured or partial material is then drawn textured), upload the tile's textures.
    const QString askedView = [] {
      static const QStringList views = { "shaded", "chunks", "wire", "shadedwire", "normals", "textured", "texturedlit",
                                         "texturedplainweights", "materials" };
      QString v;
      for (const QString & a : qEnvironmentVariable("WMV_DEBUG").split(QRegularExpression("[ \t,;]"), QString::SkipEmptyParts))
        if (a.startsWith("-wmvTerrainView=") && views.contains(a.mid(16)))
          v = a.mid(16);
      return v;
    }();
    const bool viewUploadsTextures = askedView.isEmpty() || askedView.startsWith("textured");
    // A tile on show, a report window after its build: one tile alive, and its objects counted. A protocol-13 player
    // also counts the tile's textures and alpha arrays: some with a textured material, none with an unavailable one, and
    // none in a view that draws no textures.
    const auto sampleShown = [&](const QString & label, UnityIpcServer::RuntimeState & s) {
      const bool got = sample(label, s);
      tally.check(label + ": the player counts the tile's own Meshes, Materials and GameObjects",
                  got && s.liveTerrainTiles == 1 && tileObjects(s), got ? s.describe() : QString("no answer"));
      if (got && texturedPlayer)
      {
        const bool reported = s.terrainTextures >= 0 && s.terrainAlphaArrays >= 0 && s.terrainTextureBytes >= 0;
        const bool none = s.terrainTextures == 0 && s.terrainAlphaArrays == 0 && s.terrainTextureBytes == 0;
        const bool ok = reported && (!viewUploadsTextures ? none
                                     : lastMaterialState == "textured" ? s.terrainTextures > 0 && s.terrainAlphaArrays > 0 &&
                                                                           s.terrainTextureBytes > 0
                                     : lastMaterialState == "unavailable" ? s.terrainTextures == 0 && s.terrainAlphaArrays == 0
                                                                          : true);
        tally.check(label + ": the player counts the tile's textures and alpha arrays (material \"" + lastMaterialState +
                      "\", view " + (askedView.isEmpty() ? QString("as the material") : askedView) + ")",
                    ok, !reported ? QString("player does not report terrainTextures, terrainTextureBytes or terrainAlphaArrays")
                                  : QString("terrainTextures %1, terrainTextureBytes %2, terrainAlphaArrays %3")
                                      .arg(s.terrainTextures).arg(s.terrainTextureBytes).arg(s.terrainAlphaArrays));
      }
      return got;
    };
    // No terrain texture or alpha array left (a protocol-13 player; nothing to say about an older one).
    const auto noTerrainTextures = [&](const UnityIpcServer::RuntimeState & s) {
      return !texturedPlayer || (s.terrainTextures == 0 && s.terrainAlphaArrays == 0 && s.terrainTextureBytes == 0);
    };
    // The viewport notice the host puts up (none: "").
    const auto noticeTitle = [frame]() {
      ModelViewer::ViewportNotice notice;
      return frame->unityViewportNotice(notice) ? QString::fromStdWString(notice.title.ToStdWstring()) : QString();
    };
    const QString cannotBuild = QString::fromStdWString(wxString(_("Model could not be built")).ToStdWstring());

    // THE COUNTS WITH NO TERRAIN. The baseline, taken before the first tile, is kept as it is; a clear compares the
    // player's Mesh and Material counts with the latest reference: the baseline, or the counts a model or switch step
    // left (the model's own meshes count), taken only once that step's terrain objects were 0.
    UnityIpcServer::RuntimeState baseline;
    const bool baselineAnswered = sample("baseline", baseline);
    tally.check("the player holds no terrain before the first tile, and reports every object count",
                baselineAnswered && baseline.liveTerrainTiles == 0 && baseline.terrainView == 0 && noTerrainObjects(baseline),
                baselineAnswered ? baseline.describe() : QString("no answer"));
    UnityIpcServer::RuntimeState countsReference = baseline;
    QString countsReferenceLabel = "taken before the first tile";
    const auto adoptReference = [&](const QString & label, const UnityIpcServer::RuntimeState & s) {
      const bool ok = noTerrainObjects(s);
      tally.check(label + ": no terrain Mesh, Material or GameObject left, every count reported (the reference for a "
                          "later clear)", ok, s.describe());
      if (ok)
      {
        countsReference = s;
        countsReferenceLabel = "taken after " + label;
      }
    };

    // THE TILE asked for on the command line.
    struct Tile { int map, x, y; };
    Tile last = {mapId, tileX, tileY};
    UnityIpcServer::RuntimeState afterTile = baseline;
    {
      QString why;
      const int load = request(mapId, tileX, tileY, "main", why);
      tally.check("the tile is requested", load > 0, load > 0 ? QString("load %1").arg(load) : why);
      if (load > 0 && checkBuilt("main tile", load, mapId, tileX, tileY) && sampleShown("after the main tile", afterTile))
        tally.check("the player's Mesh count rises with the tile (the counter measures)",
                    counted(baseline) && afterTile.meshObjects > baseline.meshObjects,
                    QString("meshObjects %1 with the tile, %2 before it").arg(afterTile.meshObjects).arg(baseline.meshObjects));
    }
    // A model the modelthentile step dropped for its tile, while the canvas still holds it: no tile shown, its notice is up.
    int droppedModel = 0;
    const auto checkDroppedNotice = [&](const QString & label) {
      if (droppedModel == 0)
        return;
      const QString title = noticeTitle();
      tally.check(label + QString(": with no tile shown, the notice for the dropped model %1 is up again").arg(droppedModel),
                  title == cannotBuild, "notice \"" + title + "\"");
    };

    // THE LATE ANSWERS (the server's test hooks: a getMapAsset held unanswered, released once its load has ended). What the
    // player keeps of its requests is checked from the fields it reports; one it does not send reads -1 (a check comparing
    // two samples passes the lower of them), and the check then fails naming it rather than passing on a number that was
    // never sent. A protocol-13 player has several requests out at once: a load that ends retires every one still out and
    // names them in its report (requestsRetired, retiredRequestIds), so the checks follow those; a protocol-12 player has
    // one out, and retires exactly the held one.
    const int retiredCapacity = texturedPlayer ? 128 : 32;   // the player's retired ring (WmvTerrainSession.RetiredCapacity)
    const auto checkField = [&](const QString & label, const char * field, int value, bool ok, const QString & what,
                                const QString & detail) {
      if (value < 0)
        tally.check(label + ": " + what, false, QString("player does not report %1").arg(field));
      else
        tally.check(label + ": " + what, ok, detail);
    };
    // A sample before a held load: what its retirements and its late answers are measured from (with the reports' count of
    // retirements at that moment). Returns whether the player answered.
    long long windowRetiredStart = 0;
    const auto sampleBefore = [&](const QString & label, UnityIpcServer::RuntimeState & before) {
      const bool answered = sampleOnce(label + " (before the held load)", before);
      windowRetiredStart = retiredReported;
      if (!answered)
        tally.check(label + ": the player answers before the held load", false, "no answer");
      return answered;
    };
    // A load whose request of role is held: the hook armed, the load sent, its request awaited until the server holds it.
    // Returns the load (0 when it was not sent); heldId stays empty when nothing was held.
    const auto requestHeld = [&](const QString & label, const QString & role, const Tile & at, const QString & step,
                                 QString & heldId) {
      heldId.clear();
      QString why;
      ipc->testHoldNextMapAsset(role);
      const int load = request(at.map, at.x, at.y, step, why);
      if (load <= 0)
      {
        ipc->testCancelMapAssetHold();
        tally.check(label + ": requested", false, why);
        return 0;
      }
      wxStopWatch w;
      while (ipc->mapAssetsHeld().empty() && w.Time() < 20000 && ipc->isUnityReady())
        pumpIpc(ipc, 1);
      const QJsonObject held = ipc->mapAssetsHeld().empty() ? QJsonObject() : ipc->mapAssetsHeld().back();
      const bool ok = !held.isEmpty() && held.value("load").toInt(0) == load && held.value("role").toString() == role;
      if (ok)
        heldId = held.value("requestId").toString();
      else
      {
        ipc->testCancelMapAssetHold();
        ipc->testReleaseHeldMapAssets();
      }
      tally.check(label + QString(": load %1's %2 request is held unanswered").arg(load).arg(role), ok,
                  held.isEmpty() ? QString("nothing held within 20 s")
                                 : QString("request %1, load %2, role %3, held after %4 ms").arg(held.value("requestId").toString())
                                     .arg(held.value("load").toInt(0)).arg(held.value("role").toString()).arg(w.Time()));
      return load;
    };
    // Once the load has ended with its request heldId still held: the player retired, since before (the sample taken
    // before the load), exactly the requests the reports of the loads ended since say they retired (protocol 13:
    // requestsRetired, their ids named; protocol 12: the held one alone) -- its total of retirements that much higher, its
    // ring that much fuller up to its capacity --, the held request among them (protocol 12: the one retired last), and
    // the one retired last one of the load's. Pending 0 is an invariant only (no load is in flight). retiredIds receives
    // the ids the load retired.
    const auto checkEnded = [&](const QString & label, const UnityIpcServer::RuntimeState & before, const QString & heldId,
                                int heldLoad, UnityIpcServer::RuntimeState & ended, QStringList & retiredIds) {
      retiredIds.clear();
      if (!sample(label + " (the load ended)", ended))
      {
        tally.check(label + ": the player answers once the load ended", false, "no answer");
        return false;
      }
      UnityIpcServer::TerrainTileReport r;
      for (const UnityIpcServer::TerrainTileReport & x : reports)
        if (x.load == heldLoad && r.load == 0)
          r = x;
      const long long expected = texturedPlayer ? retiredReported - windowRetiredStart : 1;
      if (texturedPlayer)
      {
        retiredIds = r.retiredRequestIds;
        tally.check(label + QString(": load %1's report names the requests it retired, the held %2 among them").arg(heldLoad)
                              .arg(heldId),
                    r.load == heldLoad && r.requestsRetired >= 1 && r.requestsRetired == r.retiredRequestIds.size() &&
                      r.retiredRequestIds.contains(heldId),
                    r.load != heldLoad ? QString("no report of the load kept")
                                       : QString("requestsRetired %1, ids [%2]").arg(r.requestsRetired)
                                           .arg(r.retiredRequestIds.join(",")));
      }
      else
        retiredIds << heldId;
      checkField(label, "terrainRetiredTotal", std::min(before.terrainRetiredTotal, ended.terrainRetiredTotal),
                 ended.terrainRetiredTotal == before.terrainRetiredTotal + expected,
                 QString("the loads ended retire the requests their reports name (terrainRetiredTotal %1 higher than "
                         "before the load)").arg(expected),
                 QString("terrainRetiredTotal %1 before the load, %2 now; the reports since name %3")
                   .arg(before.terrainRetiredTotal).arg(ended.terrainRetiredTotal).arg(expected));
      const long long ringExpected = std::min<long long>(before.terrainRetiredRequests + expected, retiredCapacity);
      checkField(label, "terrainRetiredRequests", std::min(before.terrainRetiredRequests, ended.terrainRetiredRequests),
                 ended.terrainRetiredRequests == ringExpected,
                 QString("the retired ring holds %1 more, up to %2 (terrainRetiredRequests %3)").arg(expected)
                   .arg(retiredCapacity).arg(ringExpected),
                 QString("terrainRetiredRequests %1 before the load, %2 now").arg(before.terrainRetiredRequests)
                   .arg(ended.terrainRetiredRequests));
      const QString lastWhat = texturedPlayer ? QString("one the load retired") : "the held one, " + heldId;
      if (!ended.hasTerrainLastRetiredRequest)
        tally.check(label + ": the request retired last is " + lastWhat, false, "player does not report terrainLastRetiredRequest");
      else
        tally.check(label + ": the request retired last is " + lastWhat,
                    texturedPlayer ? retiredIds.contains(ended.terrainLastRetiredRequest)
                                   : ended.terrainLastRetiredRequest == heldId,
                    "terrainLastRetiredRequest \"" + ended.terrainLastRetiredRequest + "\"");
      checkField(label, "terrainPendingRequests", ended.terrainPendingRequests, ended.terrainPendingRequests == 0,
                 "invariant: no request pending with no load in flight",
                 QString("terrainPendingRequests %1").arg(ended.terrainPendingRequests));
      return true;
    };
    // The requests the player sent all accounted for, with nothing in flight: the host received as many as the player
    // applied (answers routed to it and not dropped), retired and has pending.
    const auto checkConservation = [&](const QString & label, const UnityIpcServer::RuntimeState & s) {
      const long long sent = ipc->stats().mapAssetRequests;
      const long long applied = (long long)s.mapAnswersRouted - s.mapAnswersDropped;
      const int lowest = std::min(std::min(s.mapAnswersRouted, s.mapAnswersDropped),
                                  std::min(s.terrainRetiredTotal, s.terrainPendingRequests));
      checkField(label, "mapAnswersRouted, mapAnswersDropped, terrainRetiredTotal or terrainPendingRequests", lowest,
                 ipc->mapAssetsWaiting() == 0 && ipc->mapAssetsHeld().empty() &&
                   sent == applied + s.terrainRetiredTotal + s.terrainPendingRequests,
                 "conservation: the requests the host received from the player = applied + retired + pending",
                 QString("%1 received; %2 routed - %3 dropped = %4 applied, %5 retired, %6 pending; %7 waiting, %8 held")
                   .arg(sent).arg(s.mapAnswersRouted).arg(s.mapAnswersDropped).arg(applied).arg(s.terrainRetiredTotal)
                   .arg(s.terrainPendingRequests).arg(ipc->mapAssetsWaiting()).arg(ipc->mapAssetsHeld().size()));
    };
    // The held request released: the host answers it as any (expectState: "available" when nothing changed since -- the
    // file, long after its load ended -- "stale" otherwise); alreadyReleased >= 0: it was released when the load's report
    // arrived (the timeout step), that many. The player drops as late every answer to a request the load retired --
    // mapAnswersLate and mapAnswersDropped higher than before the load by the answers the host sent to retiredIds of the
    // load, mapAnswersUnowned the same --, with no second report for the load, nothing on screen changed, and every request
    // accounted for (checkConservation). after is the sample.
    const auto releaseLate = [&](const QString & label, int load, const QString & heldId, const QStringList & retiredIds,
                                 const UnityIpcServer::RuntimeState & before, const UnityIpcServer::RuntimeState & ended,
                                 const QString & expectState, int alreadyReleased, UnityIpcServer::RuntimeState & after) {
      const int released = alreadyReleased >= 0 ? alreadyReleased : ipc->testReleaseHeldMapAssets();
      pumpIpc(ipc, 50);
      const UnityIpcServer::MapAssetServed * answer = nullptr;
      int lateExpected = 0;
      for (const ServedEntry & e : servedRecent)
        if (e.s.load == load && retiredIds.contains(e.s.requestId))
        {
          lateExpected++;
          if (e.s.requestId == heldId)
            answer = &e.s;
        }
      tally.check(label + QString(": the held request %1 is answered on release, \"%2\"").arg(heldId).arg(expectState),
                  released == 1 && answer && answer->state == expectState,
                  QString("%1 released; ").arg(released) + (answer ? answer->describe() : QString("no answer recorded")));
      if (!sample(label + " (after the late answer)", after))
      {
        tally.check(label + ": the player answers after the late answer", false, "no answer");
        return;
      }
      checkField(label, "mapAnswersDropped", std::min(before.mapAnswersDropped, after.mapAnswersDropped),
                 after.mapAnswersDropped == before.mapAnswersDropped + lateExpected,
                 QString("the player drops the %1 answer(s) to the retired requests (mapAnswersDropped %1 higher than "
                         "before the load)").arg(lateExpected),
                 QString("mapAnswersDropped %1 before the load, %2 when it had ended, %3 now").arg(before.mapAnswersDropped)
                   .arg(ended.mapAnswersDropped).arg(after.mapAnswersDropped));
      checkField(label, "mapAnswersLate", std::min(before.mapAnswersLate, after.mapAnswersLate),
                 after.mapAnswersLate == before.mapAnswersLate + lateExpected,
                 QString("... as late, their requests found among the retired (mapAnswersLate %1 higher)").arg(lateExpected),
                 QString("mapAnswersLate %1 before the load, %2 when it had ended, %3 now").arg(before.mapAnswersLate)
                   .arg(ended.mapAnswersLate).arg(after.mapAnswersLate));
      checkField(label, "mapAnswersUnowned", std::min(before.mapAnswersUnowned, after.mapAnswersUnowned),
                 after.mapAnswersUnowned == before.mapAnswersUnowned,
                 "... not as unowned (mapAnswersUnowned unchanged)",
                 QString("mapAnswersUnowned %1 before the load, %2 now").arg(before.mapAnswersUnowned).arg(after.mapAnswersUnowned));
      checkField(label, "terrainPendingRequests", after.terrainPendingRequests, after.terrainPendingRequests == 0,
                 "invariant: no request pending with no load in flight",
                 QString("terrainPendingRequests %1").arg(after.terrainPendingRequests));
      checkField(label, "terrainRetiredRequests", after.terrainRetiredRequests,
                 after.terrainRetiredRequests <= retiredCapacity,
                 QString("invariant: the retired ring holds at most %1").arg(retiredCapacity),
                 QString("terrainRetiredRequests %1").arg(after.terrainRetiredRequests));
      checkConservation(label, after);
      tally.check(label + QString(": the late answer brings no second report for load %1").arg(load), reportsFor(load) == 1,
                  QString("%1 report(s)").arg(reportsFor(load)));
      tally.check(label + ": nothing on screen changes with it -- the same model, world model and terrain, nothing loading",
                  after.modelFileDataID == ended.modelFileDataID && after.mapObjectFileDataID == ended.mapObjectFileDataID &&
                    after.liveModels == ended.liveModels && after.liveMapObjects == ended.liveMapObjects &&
                    after.liveTerrainTiles == ended.liveTerrainTiles && after.terrainLoad == ended.terrainLoad &&
                    !after.loading && after.terrainLoading == 0,
                  QString("model %1 -> %2, world model %3 -> %4, live models %5 -> %6, terrain tiles %7 -> %8, terrain load %9")
                    .arg(ended.modelFileDataID).arg(after.modelFileDataID).arg(ended.mapObjectFileDataID).arg(after.mapObjectFileDataID)
                    .arg(ended.liveModels).arg(after.liveModels).arg(ended.liveTerrainTiles).arg(after.liveTerrainTiles)
                    .arg(ended.terrainLoad) +
                    QString(" -> %1, loading %2, terrain loading %3").arg(after.terrainLoad).arg(after.loading ? "true" : "false")
                      .arg(after.terrainLoading));
    };
    // The roles a late step may hold: the WDT and the root for any player, the tex0 and a texture for one that draws
    // terrain textures (an older one never asks for them: skipped, the reason named).
    const auto lateRoleUsable = [&](const QString & label, const QString & role) {
      if ((role == "tex0Adt" || role == "texture") && !texturedPlayer)
      {
        tally.skip(label, noTexturesWhy + ", so it never asks for a " + role);
        return false;
      }
      return true;
    };

    // THE SEQUENCE.
    const QStringList steps = sequenceSpec.split(',', QString::SkipEmptyParts);
    const auto parseTile = [](const QString & text, Tile & t) {
      const QStringList p = text.split(':');
      bool a = false, b = false, c = false;
      if (p.size() < 3)
        return false;
      t.map = p[0].trimmed().toInt(&a);
      t.x = p[1].trimmed().toInt(&b);
      t.y = p[2].trimmed().toInt(&c);
      return a && b && c;
    };
    for (int s = 0; s < steps.size(); s++)
    {
      const QString step = steps[s].trimmed();
      const QString kind = step.section(':', 0, 0).trimmed().toLower();
      const QString arg = step.section(':', 1).trimmed();
      const QString label = QString("step %1 %2").arg(s + 1).arg(step);
      LOG_INFO << "[terrain-test]" << label;
      Tile t = {0, 0, 0};
      QString why;
      if (kind == "tile")
      {
        if (!parseTile(arg, t))
        {
          tally.check(label, false, "expected tile:<map>:<x>:<y>");
          continue;
        }
        const int load = request(t.map, t.x, t.y, step, why);
        if (load <= 0)
          tally.check(label + ": requested", false, why);
        else if (checkBuilt(label, load, t.map, t.x, t.y))
          sampleShown(label, afterTile);
        last = t;
      }
      else if (kind == "repeat")
      {
        const int n = arg.toInt();
        bool all = n > 0;
        for (int i = 0; i < n; i++)
        {
          const int load = request(last.map, last.x, last.y, step, why);
          all = load > 0 && checkBuilt(label + QString(" #%1").arg(i + 1), load, last.map, last.x, last.y) && all;
          if (load <= 0)
            tally.check(label + QString(" #%1: requested").arg(i + 1), false, why);
          // A long run is sampled every 25 loads along the way, for the memory of both processes.
          if (n >= 50 && (i + 1) % 25 == 0)
          {
            UnityIpcServer::RuntimeState along;
            sampleOnce(label + QString(" #%1").arg(i + 1), along);
          }
        }
        UnityIpcServer::RuntimeState now;
        const bool got = sampleShown(label, now);
        tally.check(label + ": the player's Mesh and Material counts are those after the tile's first build",
                  all && got && now.liveTerrainTiles == 1 && counted(now) && counted(afterTile) &&
                    now.meshObjects == afterTile.meshObjects && now.materialObjects == afterTile.materialObjects,
                  QString("meshObjects %1 (after the first build %2), materialObjects %3 (%4), liveTerrainTiles %5")
                    .arg(now.meshObjects).arg(afterTile.meshObjects).arg(now.materialObjects).arg(afterTile.materialObjects)
                    .arg(now.liveTerrainTiles));
      }
      else if (kind == "wait")
        pumpTicking(ipc, arg.toLong());
      else if (kind == "clear")
      {
        // clear:<map>:<x>:<y>: that tile is requested and cleared before anything is polled -- the player cannot build
        // it before the host answers its getMapAsset -- so the clear meets a load still building.
        int inFlight = 0;
        if (!arg.isEmpty())
        {
          if (!parseTile(arg, t))
          {
            tally.check(label, false, "expected clear or clear:<map>:<x>:<y>");
            continue;
          }
          inFlight = request(t.map, t.x, t.y, step, why);
          if (inFlight <= 0)
          {
            tally.check(label + ": requested", false, why);
            continue;
          }
        }
        const long long before = reportsTotal;
        const bool sent = frame->ClearTerrain("terrain test: clear");
        if (inFlight)
        {
          UnityIpcServer::TerrainTileReport r;
          const bool got = waitReport(inFlight, 60000, r);
          pumpIpc(ipc, 100);
          tally.check(label + QString(": the load building (%1) is answered \"cleared\", once").arg(inFlight),
                    sent && got && r.status == "cleared" && reportsFor(inFlight) == 1,
                    got ? QString("%1 answer(s), the first: ").arg(reportsFor(inFlight)) + r.status + " " + r.reason
                        : QString("no answer"));
        }
        else
        {
          pumpIpc(ipc, 100);
          tally.check(label + ": a tile on show is taken away unanswered", sent && reportsTotal == before,
                    QString("%1 report(s) after the clear").arg(reportsTotal - before));
        }
        UnityIpcServer::RuntimeState now;
        const bool got = sample(label, now);
        tally.check(label + ": no terrain tile, view, load, terrain object" + (texturedPlayer ? ", texture or alpha array" : "") +
                            " left, and the player's Mesh and Material counts those " + countsReferenceLabel,
                    got && now.liveTerrainTiles == 0 && now.terrainView == 0 && now.terrainLoading == 0 &&
                      noTerrainObjects(now) && noTerrainTextures(now) && counted(countsReference) &&
                      now.meshObjects == countsReference.meshObjects &&
                      now.materialObjects == countsReference.materialObjects,
                    got ? QString("meshObjects %1 materialObjects %2, those %3: %4 and %5; ")
                            .arg(now.meshObjects).arg(now.materialObjects).arg(countsReferenceLabel)
                            .arg(countsReference.meshObjects).arg(countsReference.materialObjects) + now.describe()
                        : QString("no answer"));
        checkDroppedNotice(label);
      }
      else if (kind == "expectfail")
      {
        if (!parseTile(arg, t) || arg.section(':', 3).isEmpty())
        {
          tally.check(label, false, "expected expectfail:<map>:<x>:<y>:<reason text>");
          continue;
        }
        const QString text = arg.section(':', 3);
        const int load = request(t.map, t.x, t.y, step, why);
        if (load <= 0)
          tally.check(label + ": refused by the host", why.contains(text), why);
        else if (expectFailed(label + ": failed", load, QString(), text).liveTerrainTiles == 0)
          checkDroppedNotice(label);
      }
      else if (kind == "notlocal")
      {
        bool numeric = false;
        const int root = arg.section(':', 3).toInt(&numeric);
        if (!parseTile(arg, t) || !numeric)
        {
          tally.check(label, false, "expected notlocal:<map>:<x>:<y>:<root FileDataID>");
          continue;
        }
        // The install state is Battle.net's, which can change in the background: checked first, a precondition.
        wow::WdtReference reference;
        reference.kind = wow::WdtReference::Kind::FileDataId;
        reference.fileDataId = root;
        const wow::WdtInspection now = frame->mapCatalog()->inspectFile(reference);
        if (now.state != wow::WdtAvailability::NotInstalledLocally)
        {
          LOG_INFO << "[clientcaps] files not installed" << frame->clientCapabilities().filesNotInstalled
                   << "| root" << root << wow::WorldMapCatalog::availabilityName(now.state) << now.detail;
          tally.skip(label, "precondition changed (now " + wow::WorldMapCatalog::availabilityName(now.state) + ")");
          continue;
        }
        const int load = request(t.map, t.x, t.y, step, why);
        if (load <= 0)
          tally.check(label + ": requested", false, why);
        else
          expectFailed(label + ": failed, the root not installed locally", load, "not installed locally", QString());
      }
      else if (kind == "switch")
      {
        const std::vector<core::GameConfig> configs = GAMEDIRECTORY.configsFound();
        const core::GameConfig * config = nullptr;
        for (const core::GameConfig & c : configs)
          if (c.product == arg)
            config = &c;
        if (!config)
        {
          tally.check(label, false, "no such product in the installation");
          continue;
        }
        const core::GameConfig chosen = *config;
        const int epoch = UnityAssetAccess::clientEpoch();
        const int load = request(last.map, last.x, last.y, step, why);
        if (load <= 0)
        {
          tally.check(label + ": requested", false, why);
          continue;
        }
        // The client is loaded before anything is polled: the player's WDT request waits for it.
        const long long servedBefore = servedTotal;
        const bool loaded = frame->LoadWoW(&chosen);
        droppedModel = 0;   // the canvas was emptied with the client
        const long long servedDuring = servedTotal - servedBefore;
        tally.check(label + ": the client loads", loaded, chosen.product + " " + chosen.version);
        if (servedDuring != 0)
        {
          tally.check(label + ": test invalid", false,
                    QString("%1 getMapAsset answered before the load returned").arg(servedDuring));
          continue;
        }
        UnityIpcServer::TerrainTileReport r;
        const bool got = waitReport(load, 60000, r);
        tally.check(label + QString(": load %1 is answered \"cleared\"").arg(load), got && r.status == "cleared",
                  got ? r.status + " " + r.reason : QString("no answer within 60 s"));
        wxStopWatch w;
        while (!lastServed(load, "wdt") && w.Time() < 10000)
          pumpIpc(ipc, 1);
        const UnityIpcServer::MapAssetServed * wdt = lastServed(load, "wdt");
        tally.check(label + ": the load's WDT request is answered stale, under the epoch before and the one after",
                  wdt && wdt->state == "stale" && wdt->assetEpoch == epoch && wdt->epochNow == epoch + 1,
                  wdt ? wdt->describe() : QString("no WDT request for the load was answered"));
        UnityIpcServer::RuntimeState now;
        const bool answered = sample(label, now);
        tally.check(label + ": no terrain tile is left", answered && now.liveTerrainTiles == 0 && now.terrainView == 0,
                    answered ? now.describe() : QString("no answer"));
        adoptReference(label, now);   // the next client's own state is the reference from here on
      }
      else if (kind == "staleepoch" || kind == "staleload")
      {
        const int load = request(last.map, last.x, last.y, step, why);
        if (load <= 0)
        {
          tally.check(label + ": requested", false, why);
          continue;
        }
        // The epoch moves on with no client replaced, so the host's read tracker is not reset: the next read under the new
        // epoch empties it (its backstop).
        if (kind == "staleepoch")
          UnityAssetAccess::noteClientReplaced();
        else
          frame->TerrainTestRaiseLoadWithoutSending();
        expectFailed(label + ": failed, the host answering stale", load, "stale", QString());
      }
      else if (kind == "supersede")
      {
        Tile a = t, b = t;
        if (!parseTile(arg.section('|', 0, 0), a) || !parseTile(arg.section('|', 1), b))
        {
          tally.check(label, false, "expected supersede:<map>:<x>:<y>|<map>:<x>:<y>");
          continue;
        }
        QString whyB;
        const int first = request(a.map, a.x, a.y, step, why);
        const int second = request(b.map, b.x, b.y, step, whyB);
        if (first <= 0 || second <= 0)
        {
          tally.check(label + ": both requested", false, why + " " + whyB);
          continue;
        }
        UnityIpcServer::TerrainTileReport r;
        const bool got = waitReport(first, 60000, r);
        tally.check(label + QString(": the first load (%1) is answered \"superseded\"").arg(first), got && r.status == "superseded",
                  got ? r.status + " " + r.reason : QString("no answer within 60 s"));
        if (checkBuilt(label + ": the second", second, b.map, b.x, b.y))
          sampleShown(label, afterTile);
        last = b;
      }
      else if (kind == "model" || kind == "modelthentile")
      {
        const QString path = arg.section('|', 0, 0).trimmed();
        const QString tileSpec = arg.section('|', 1).trimmed();
        GameFile * file = path.isEmpty() ? nullptr : resolveGameFileArg(path);
        if (!file || ((kind == "modelthentile" || !tileSpec.isEmpty()) && !parseTile(tileSpec, t)))
        {
          tally.check(label, false, !file ? QString("no such model file")
                                          : kind == "model" ? QString("expected model:<path> or model:<path>|<map>:<x>:<y>")
                                                            : QString("expected modelthentile:<path>|<map>:<x>:<y>"));
          continue;
        }
        const int fileDataID = (int)file->fileDataId();
        if (kind == "model")
        {
          // model:<path>|<map>:<x>:<y>: that tile is requested and the model selected before anything is polled -- the
          // player cannot build the tile before the host answers its getMapAsset -- so the model load meets a terrain
          // load still building.
          int inFlight = 0;
          if (!tileSpec.isEmpty())
          {
            inFlight = request(t.map, t.x, t.y, step, why);
            if (inFlight <= 0)
            {
              tally.check(label + ": requested", false, why);
              continue;
            }
          }
          const long long servedBefore = servedTotal;
          frame->fileControl->SelectModelFile(file);
          droppedModel = 0;   // the canvas holds another model
          const long long servedDuring = servedTotal - servedBefore;
          if (inFlight && servedDuring != 0)
          {
            tally.check(label + ": test invalid", false,
                        QString("%1 getMapAsset answered before the model was selected").arg(servedDuring));
            continue;
          }
          tally.check(label + ": the host holds no terrain request", frame->m_terrainRequest.load == 0 &&
                      frame->m_playerContent != ModelViewer::PlayerContent::Terrain,
                    QString("terrain request load %1").arg(frame->m_terrainRequest.load));
          UnityIpcServer::TerrainTileReport r;
          const bool gotReport = inFlight && waitReport(inFlight, 60000, r);
          UnityIpcServer::RuntimeState now;
          bool answered = false;
          const bool holds = waitRuntimeState(ipc, states, [fileDataID](const UnityIpcServer::RuntimeState & x) {
            return x.modelFileDataID == fileDataID && !x.loading && x.liveTerrainTiles == 0 && x.terrainView == 0;
          }, 60000, now, answered);
          appendSample(label, now);
          tally.check(label + ": the model on screen, no terrain tile or view left", holds, now.describe());
          if (inFlight)
            tally.check(label + QString(": the terrain load building (%1) is answered \"superseded\", once").arg(inFlight),
                        gotReport && r.status == "superseded" && reportsFor(inFlight) == 1,
                        gotReport ? QString("%1 answer(s), the first: ").arg(reportsFor(inFlight)) + r.status + " " + r.reason
                                  : QString("no answer"));
          // The player's counts with no terrain now include the model: the reference for a later clear.
          adoptReference(label, now);
        }
        else
        {
          // The model on screen first: the step's model must be another, or a dropped build could not be told from one
          // adopted.
          UnityIpcServer::RuntimeState before;
          const bool told = sample(label + " (before)", before) && before.modelFileDataID >= 0;
          const bool another = told && before.modelFileDataID != fileDataID;
          tally.check(label + (another ? ": the model on screen is another one" : ": test invalid"), another,
                      !told ? QString("the player did not say which model it shows")
                            : another ? QString("model on screen %1, the step's %2").arg(before.modelFileDataID).arg(fileDataID)
                                      : QString("the model on screen is already %1: the step needs another").arg(fileDataID));
          if (!another)
            continue;
          frame->fileControl->SelectModelFile(file);
          const int awaited = frame->m_modelAwaited;
          // The precondition: the player is still building the model when the tile goes out (one question; the tile
          // follows its answer with nothing else polled).
          UnityIpcServer::RuntimeState building;
          bool answered = false;
          waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState &) { return true; }, 5000, building, answered);
          appendSample(label + " (model sent)", building);
          const bool stillBuilding = answered && building.loading;
          tally.check(label + (stillBuilding ? ": the player is still building the model when the tile is sent"
                                             : ": test invalid"), stillBuilding,
                      (stillBuilding ? QString() : QString("the model was built before the tile was sent; ")) +
                        (answered ? building.describe() : QString("no answer")));
          if (!stillBuilding)
            continue;
          const int load = request(t.map, t.x, t.y, step, why);
          if (load <= 0)
          {
            tally.check(label + ": requested", false, why);
            continue;
          }
          last = t;
          // The host: the build the player drops is recorded, so its notice stays up while the tile builds.
          droppedModel = 0;
          if (awaited != fileDataID)
            tally.skip(label + ": the notice for the dropped model",
                       QString("the host was not waiting for model %1 (waiting for %2)").arg(fileDataID).arg(awaited));
          else
          {
            droppedModel = fileDataID;
            const QString title = noticeTitle();
            tally.check(label + ": while the tile builds, the host keeps the dropped model's \"could not be built\" notice up",
                        frame->m_modelAwaitedFailed == fileDataID && title == cannotBuild,
                        QString("model recorded as dropped %1; notice \"").arg(frame->m_modelAwaitedFailed) + title + "\"");
          }
          if (!checkBuilt(label, load, t.map, t.x, t.y))
            continue;
          sampleShown(label, afterTile);
          const QString shownTitle = noticeTitle();
          tally.check(label + ": with the tile shown, the host puts no notice up", shownTitle.isEmpty(),
                      "notice \"" + shownTitle + "\"");
          // A report window later the dropped model is still not adopted, and nothing is building.
          pumpIpc(ipc, 200);
          UnityIpcServer::RuntimeState now;
          const bool got = sample(label + " (model load)", now);
          tally.check(label + ": the dropped model never adopted: nothing building, the tile in the terrain view, the model of "
                              "before on screen",
                      got && !now.loading && now.liveTerrainTiles == 1 && now.terrainView == 1 &&
                        now.modelFileDataID == before.modelFileDataID,
                      got ? QString("model of before %1; ").arg(before.modelFileDataID) + now.describe() : QString("no answer"));
        }
      }
      else if (kind == "timeout" || kind == "latesupersede" || kind == "lateclear")
      {
        const QString role = arg.section(':', 3).trimmed();
        if (!parseTile(arg, t) || (role != "wdt" && role != "rootAdt" && role != "tex0Adt" && role != "texture"))
        {
          tally.check(label, false, QString("expected %1:<map>:<x>:<y>:<wdt|rootAdt|tex0Adt|texture>").arg(kind));
          continue;
        }
        if (!lateRoleUsable(label, role))
          continue;
        // The player before the held load: what its retirements and late answers are measured from (and its timeout).
        UnityIpcServer::RuntimeState before;
        if (!sampleBefore(label, before))
          continue;
        long timeoutMs = 30000;   // the player's own, when it does not say (a player before terrainTimeoutMs)
        if (kind == "timeout")
        {
          checkField(label, "terrainTimeoutMs", before.terrainTimeoutMs,
                     before.terrainTimeoutMs >= 1000 && before.terrainTimeoutMs <= 120000,
                     "the player states its terrain timeout (1000 to 120000 ms)",
                     QString("terrainTimeoutMs %1").arg(before.terrainTimeoutMs));
          if (before.terrainTimeoutMs > 0)
            timeoutMs = before.terrainTimeoutMs;
        }
        QString heldId;
        const int load = requestHeld(label, role, t, step, heldId);
        if (load <= 0 || heldId.isEmpty())
          continue;
        UnityIpcServer::TerrainTileReport r;
        bool got = false;
        int alreadyReleased = -1;
        if (kind == "timeout")
        {
          // Nothing answers: the player's own timeout ends the load. The held request is released as its report arrives,
          // before the host takes the load as ended: the file goes back "available", to a load already over.
          releaseOnReportOf = load;
          releasedOnReport = -1;
          got = waitReport(load, timeoutMs + 15000, r);
          releaseOnReportOf = 0;
          alreadyReleased = std::max(0, releasedOnReport);
          tally.check(label + QString(": load %1 fails, timed out").arg(load),
                      got && r.status == "failed" && r.reason.contains("timed out"),
                      got ? r.status + " " + r.reason : QString("no answer within %1 ms").arg(timeoutMs + 15000));
        }
        else if (kind == "latesupersede")
        {
          // The next load, not held, replaces it and is built; the held request is answered after that.
          const int second = request(t.map, t.x, t.y, step, why);
          if (second <= 0)
          {
            tally.check(label + ": the second load is requested", false, why);
            ipc->testReleaseHeldMapAssets();
            continue;
          }
          got = waitReport(load, 60000, r);
          tally.check(label + QString(": the held load (%1) is answered \"superseded\"").arg(load),
                      got && r.status == "superseded", got ? r.status + " " + r.reason : QString("no answer within 60 s"));
          if (checkBuilt(label + ": the second", second, t.map, t.x, t.y))
            sampleShown(label, afterTile);
          last = t;
        }
        else
        {
          const bool sent = frame->ClearTerrain("terrain test: clear while a map request is held");
          got = waitReport(load, 60000, r);
          tally.check(label + QString(": the held load (%1) is answered \"cleared\"").arg(load),
                      sent && got && r.status == "cleared", got ? r.status + " " + r.reason : QString("no answer within 60 s"));
        }
        UnityIpcServer::RuntimeState ended, after;
        QStringList retiredIds;
        if (!checkEnded(label, before, heldId, load, ended, retiredIds))
        {
          ipc->testReleaseHeldMapAssets();
          continue;
        }
        if (kind == "lateclear")
          tally.check(label + ": no terrain tile, view, load" + (texturedPlayer ? ", texture or alpha array" : "") + " left",
                      ended.liveTerrainTiles == 0 && ended.terrainView == 0 && ended.terrainLoading == 0 &&
                        noTerrainTextures(ended), ended.describe());
        // After a timeout nothing had changed for the host when the request was released, so the file goes back: a
        // genuine late answer. Every other release meets a load the host has moved on from.
        releaseLate(label, load, heldId, retiredIds, before, ended, kind == "timeout" ? "available" : "stale", alreadyReleased,
                    after);
        // Once the host knows the load ended, what the load still asks for is stale unread: asked here of the host's own
        // handler, as the player would ask it.
        if (kind == "timeout")
        {
          QJsonObject again;
          again["type"] = "getMapAsset";
          again["requestId"] = "terrain-test";
          again["load"] = load;
          again["assetEpoch"] = UnityAssetAccess::clientEpoch();
          again["role"] = role;
          again["fileDataID"] = lastServed(load, role) ? lastServed(load, role)->fileDataID : 1;
          const UnityIpcServer::MapAssetAnswer a = frame->ServeMapAsset(again);
          tally.check(label + QString(": once the player reported load %1, the host answers what it asks for stale, unread")
                                .arg(load),
                      a.state == "stale" && a.detail.contains(QString("load %1 was reported failed").arg(load)) && a.readMs < 0.0,
                      QString("\"%1\" \"%2\" readMs %3").arg(a.state).arg(a.detail).arg(a.readMs));
        }
        if (kind == "lateclear")
          checkDroppedNotice(label);
      }
      else if (kind == "lateswitch")
      {
        // lateswitch:<product>:<map>:<x>:<y>[:<role>] (the WDT held when no role is named).
        const QString product = arg.section(':', 0, 0).trimmed();
        const QString role = arg.section(':', 4, 4).trimmed().isEmpty() ? QString("wdt") : arg.section(':', 4, 4).trimmed();
        if (!parseTile(arg.section(':', 1), t) || (role != "wdt" && role != "rootAdt" && role != "tex0Adt" && role != "texture"))
        {
          tally.check(label, false, "expected lateswitch:<product>:<map>:<x>:<y>[:<wdt|rootAdt|tex0Adt|texture>]");
          continue;
        }
        if (!lateRoleUsable(label, role))
          continue;
        const std::vector<core::GameConfig> configs = GAMEDIRECTORY.configsFound();
        const core::GameConfig * config = nullptr;
        for (const core::GameConfig & c : configs)
          if (c.product == product)
            config = &c;
        if (!config)
        {
          tally.check(label, false, "no such product in the installation");
          continue;
        }
        const core::GameConfig chosen = *config;
        const int epoch = UnityAssetAccess::clientEpoch();
        UnityIpcServer::RuntimeState before;
        if (!sampleBefore(label, before))
          continue;
        QString heldId;
        const int load = requestHeld(label, role, t, step, heldId);
        if (load <= 0 || heldId.isEmpty())
          continue;
        // The client is replaced with the load's request held: it can only be answered after.
        const long long servedBefore = servedTotal;
        const size_t readsBeforeSwitch = ipc->mapAssetReads().size();
        const int readsEpochBeforeSwitch = ipc->mapAssetReads().epoch();
        const bool loaded = frame->LoadWoW(&chosen);
        droppedModel = 0;   // the canvas was emptied with the client
        const long long servedDuring = servedTotal - servedBefore;
        tally.check(label + ": the client loads", loaded, chosen.product + " " + chosen.version);
        if (servedDuring != 0)
        {
          tally.check(label + ": test invalid", false, QString("%1 getMapAsset answered before the load returned").arg(servedDuring));
          ipc->testReleaseHeldMapAssets();
          continue;
        }
        // The host forgot the files read from the client before as it was replaced, not at the next read.
        {
          const MapAssetReadTracker & reads = ipc->mapAssetReads();
          tally.check(label + ": right after the switch the host's read tracker holds no file, under the new client's epoch",
                      reads.size() == 0 && reads.epoch() == UnityAssetAccess::clientEpoch() && reads.epoch() != epoch,
                      QString("%1 file(s) of epoch %2 (the client's %3); before the switch %4 file(s) of epoch %5")
                        .arg(reads.size()).arg(reads.epoch()).arg(UnityAssetAccess::clientEpoch()).arg(readsBeforeSwitch)
                        .arg(readsEpochBeforeSwitch));
        }
        UnityIpcServer::TerrainTileReport r;
        const bool got = waitReport(load, 60000, r);
        tally.check(label + QString(": the held load (%1) is answered \"cleared\"").arg(load), got && r.status == "cleared",
                    got ? r.status + " " + r.reason : QString("no answer within 60 s"));
        UnityIpcServer::RuntimeState ended, after;
        QStringList retiredIds;
        if (!checkEnded(label, before, heldId, load, ended, retiredIds))
        {
          ipc->testReleaseHeldMapAssets();
          continue;
        }
        tally.check(label + ": no terrain tile is left", ended.liveTerrainTiles == 0 && ended.terrainView == 0 &&
                      noTerrainTextures(ended), ended.describe());
        releaseLate(label, load, heldId, retiredIds, before, ended, "stale", -1, after);
        const UnityIpcServer::MapAssetServed * held = lastServed(load, role);
        tally.check(label + QString(": the released %1 request is answered stale, under the epoch before the switch and the one "
                                    "after").arg(role),
                    held && held->requestId == heldId && held->state == "stale" && held->assetEpoch == epoch &&
                      held->epochNow == UnityAssetAccess::clientEpoch() && held->epochNow != epoch,
                    held ? held->describe() : QString("no answer for the load's held request"));
        adoptReference(label, after);   // the next client's own state is the reference from here on
        // The new client's tile: its files are first reads of the new epoch, and the read tracker holds them alone.
        const int fresh = request(t.map, t.x, t.y, step, why);
        if (fresh <= 0)
        {
          tally.check(label + ": the new client's tile is requested", false, why);
          continue;
        }
        if (checkBuilt(label + ": the new client's tile", fresh, t.map, t.x, t.y))
        {
          sampleShown(label, afterTile);
          std::set<int> files;
          QStringList notFirst;
          for (const UnityIpcServer::MapAssetServed & s : servedFor(fresh))
            if (s.state == "available")
            {
              files.insert(s.fileDataID);
              if (!s.first || !s.firstTracked)
                notFirst << QString("%1 %2").arg(s.role).arg(s.fileDataID);
            }
          const MapAssetReadTracker & reads = ipc->mapAssetReads();
          int held = 0;
          for (int id : files)
            held += reads.holds(id) ? 1 : 0;
          tally.check(label + ": the new client's files (WDT, root" + (texturedPlayer ? ", tex0, textures" : "") +
                                ") are first reads, and the read tracker holds them alone",
                      !files.empty() && notFirst.isEmpty() && reads.epoch() == UnityAssetAccess::clientEpoch() &&
                        reads.size() == files.size() && held == (int)files.size(),
                      QString("%1 file(s) served, not first: [%2]; the tracker holds %3 file(s) of epoch %4 (the client's %5, "
                              "the old %6), %7 of the load's")
                        .arg(files.size()).arg(notFirst.join(", ")).arg(reads.size()).arg(reads.epoch())
                        .arg(UnityAssetAccess::clientEpoch()).arg(epoch).arg(held));
        }
        last = t;
      }
      else if (kind == "reconnect")
      {
        // reconnect, or reconnect:<map>:<x>:<y>[:<role>] with that load's request of role (the WDT unless named) held.
        const bool withLoad = !arg.isEmpty();
        const QString role = arg.section(':', 3, 3).trimmed().isEmpty() ? QString("wdt") : arg.section(':', 3, 3).trimmed();
        if (withLoad && (!parseTile(arg, t) || (role != "wdt" && role != "rootAdt" && role != "tex0Adt" && role != "texture")))
        {
          tally.check(label, false, "expected reconnect or reconnect:<map>:<x>:<y>[:<wdt|rootAdt|tex0Adt|texture>]");
          continue;
        }
        if (withLoad && !lateRoleUsable(label, role))
          continue;
        int heldLoad = 0;
        QString heldId;
        if (withLoad)
        {
          heldLoad = requestHeld(label, role, t, step, heldId);
          if (heldLoad <= 0 || heldId.isEmpty())
            continue;
        }
        // The player closed and started again, as a restart of the viewport does.
        const long long droppedBefore = ipc->mapAssetsHeldDropped();
        const std::set<std::pair<int, int>> filesBeforeRestart = filesServed;
        const size_t readsBefore = ipc->mapAssetReads().size();
        const int readsEpochBefore = ipc->mapAssetReads().epoch();
        servedBeforeRestarts += ipc->mapAssetsServedTotal();
        frame->unityRendererHost->shutdown();
        tally.check(label + ": the player is closed", !frame->unityRendererHost->isRunning(), QString());
        if (withLoad)
        {
          loadsOrphaned.push_back(heldLoad);
          tally.check(label + QString(": the held request %1 is dropped unanswered with its connection, and nothing waits")
                                .arg(heldId),
                      ipc->mapAssetsHeld().empty() && !ipc->testMapAssetHoldArmed() &&
                        ipc->mapAssetsHeldDropped() == droppedBefore + 1 && ipc->mapAssetsWaiting() == 0,
                      QString("%1 held, %2 dropped unanswered (%3 before), %4 waiting").arg(ipc->mapAssetsHeld().size())
                        .arg(ipc->mapAssetsHeldDropped()).arg(droppedBefore).arg(ipc->mapAssetsWaiting()));
        }
        if (!frame->StartUnityRenderer(/* selfTest */ true))
        {
          tally.check(label + ": the player starts again", false,
                      QString::fromWCharArray(frame->unityRendererHost->playerProblem().c_str()));
          return;
        }
        wxStopWatch again;
        while (!ipc->isUnityReady() && again.Time() < 60000 && frame->unityRendererHost->isRunning())
          pumpIpc(ipc, 1);
        tally.check(label + ": the new player announces itself", ipc->isUnityReady() && ipc->playerDrawsTerrain() &&
                      ipc->playerDrawsTerrainTextures() == texturedPlayer,
                    QString("protocol %1 after %2 ms").arg(ipc->playerProtocolVersion()).arg(again.Time()));
        if (!ipc->isUnityReady() || !ipc->playerDrawsTerrain())
          return;
        tally.check(label + ": the host holds no terrain request", frame->m_terrainRequest.load == 0,
                    QString("terrain request load %1").arg(frame->m_terrainRequest.load));
        tally.check(label + ": the host's read tracker outlives the player (the storage is the same)",
                    ipc->mapAssetReads().size() == readsBefore && ipc->mapAssetReads().epoch() == readsEpochBefore,
                    QString("%1 file(s) of epoch %2, before the restart %3 of epoch %4").arg(ipc->mapAssetReads().size())
                      .arg(ipc->mapAssetReads().epoch()).arg(readsBefore).arg(readsEpochBefore));
        // The new player is given the canvas's model at once: settled when nothing is loading.
        UnityIpcServer::RuntimeState fresh;
        bool answered = false;
        const bool settled = waitRuntimeState(ipc, states, [](const UnityIpcServer::RuntimeState & x) {
          return !x.loading && x.terrainLoading == 0;
        }, 60000, fresh, answered);
        appendSample(label + " (the new player)", fresh);
        tally.check(label + ": the new player holds no terrain, and nothing is loading",
                    settled && fresh.liveTerrainTiles == 0 && fresh.terrainView == 0 && noTerrainTextures(fresh),
                    fresh.describe());
        checkField(label, "terrainPendingRequests", fresh.terrainPendingRequests, fresh.terrainPendingRequests == 0,
                   "the new player has no terrain request pending", QString("terrainPendingRequests %1").arg(fresh.terrainPendingRequests));
        checkField(label, "terrainRetiredRequests", fresh.terrainRetiredRequests, fresh.terrainRetiredRequests == 0,
                   "... none retired", QString("terrainRetiredRequests %1").arg(fresh.terrainRetiredRequests));
        checkField(label, "mapAnswersDropped", fresh.mapAnswersDropped, fresh.mapAnswersDropped == 0,
                   "... and no map answer reached it to drop", QString("mapAnswersDropped %1").arg(fresh.mapAnswersDropped));
        adoptReference(label, fresh);   // the new player's own counts are the reference from here on
        const Tile next = withLoad ? t : last;
        const int load = request(next.map, next.x, next.y, step, why);
        if (load <= 0)
          tally.check(label + ": a tile is requested", false, why);
        else if (checkBuilt(label + ": a tile then", load, next.map, next.x, next.y))
        {
          sampleShown(label, afterTile);
          // A file this client's storage had served before the restart is no first read now; one it had not, is.
          QStringList bad;
          int files = 0;
          for (const UnityIpcServer::MapAssetServed & s : servedFor(load))
          {
            if (s.state != "available")
              continue;
            files++;
            const bool before = filesBeforeRestart.count(std::make_pair(s.epochNow, s.fileDataID)) != 0;
            if (s.first == before || !s.firstTracked)
              bad << QString("%1 %2 first %3, served before the restart %4").arg(s.role).arg(s.fileDataID)
                       .arg(s.first ? "yes" : "no").arg(before ? "yes" : "no");
          }
          tally.check(label + ": the tile's first reads are those the storage had not served before the restart",
                      files > 0 && bad.isEmpty(), bad.isEmpty() ? QString("as expected, %1 file(s)").arg(files) : bad.join("; "));
        }
        last = next;
        if (withLoad)
        {
          int answers = 0;
          for (const ServedEntry & e : servedRecent)
            answers += e.s.load == heldLoad ? 1 : 0;
          // Only the files answered before the hold count: the held one, and anything the load asked after it.
          int heldAnswers = 0;
          for (const ServedEntry & e : servedRecent)
            heldAnswers += e.s.load == heldLoad && e.s.requestId == heldId ? 1 : 0;
          tally.check(label + QString(": load %1's held request %2 was never answered").arg(heldLoad).arg(heldId),
                      heldAnswers == 0, QString("%1 answer(s) to it; %2 answer(s) for the load in all").arg(heldAnswers).arg(answers));
        }
      }
      else if (kind == "expectmaterial")
      {
        // expectmaterial:<state>[:<map>:<x>:<y>]: that tile (or the last) built, its material in that state, and agreeing
        // with what the host served (checkMaterial).
        const QString state = arg.section(':', 0, 0).trimmed();
        Tile at = last;
        if (!materialStates.contains(state) || (!arg.section(':', 1).isEmpty() && !parseTile(arg.section(':', 1), at)))
        {
          tally.check(label, false, "expected expectmaterial:<textured|partial|unavailable|unsupported>[:<map>:<x>:<y>]");
          continue;
        }
        if (!texturedPlayer)
        {
          tally.skip(label, noTexturesWhy);
          continue;
        }
        const int load = request(at.map, at.x, at.y, step, why);
        if (load <= 0)
        {
          tally.check(label + ": requested", false, why);
          continue;
        }
        UnityIpcServer::TerrainTileReport r;
        if (checkBuilt(label, load, at.map, at.x, at.y))
          sampleShown(label, afterTile);
        waitReport(load, 1000, r);
        tally.check(label + QString(": load %1's material is \"%2\"").arg(load).arg(state), r.hasMaterial && r.materialState == state,
                    QString("material \"%1\", reason \"%2\"").arg(r.materialState).arg(r.materialReason));
        last = at;
      }
      else if (kind == "expectserved")
      {
        // expectserved:<role>:<state>[:<text>]: the last load sent had a request of role answered with that state, its
        // detail containing text (every answer of the role is listed).
        const QString role = arg.section(':', 0, 0).trimmed();
        const QString state = arg.section(':', 1, 1).trimmed();
        const QString text = arg.section(':', 2);
        if (role.isEmpty() || state.isEmpty())
        {
          tally.check(label, false, "expected expectserved:<role>:<state>[:<detail text>]");
          continue;
        }
        if ((role == "tex0Adt" || role == "texture") && !texturedPlayer)
        {
          tally.skip(label, noTexturesWhy);
          continue;
        }
        const int load = loadsSent.empty() ? 0 : loadsSent.back();
        pumpIpc(ipc, 20);
        int n = 0, matching = 0;
        QStringList seen;
        for (const UnityIpcServer::MapAssetServed & s : servedFor(load))
          if (s.role == role)
          {
            n++;
            matching += s.state == state && s.detail.contains(text) ? 1 : 0;
            if (seen.size() < 24)
              seen << QString("%1 %2: \"%3\" \"%4\"").arg(s.requestId).arg(s.fileDataID).arg(s.state).arg(s.detail);
          }
        tally.check(label + QString(": a %1 request of load %2 is answered \"%3\"").arg(role).arg(load).arg(state) +
                      (text.isEmpty() ? QString() : " with \"" + text + "\""),
                    matching > 0, QString("%1 of %2 answer(s) match; ").arg(matching).arg(n) + seen.join("; "));
      }
      else if (kind == "notlocalpart")
      {
        // notlocalpart:<map>:<x>:<y>:<FileDataID>: a tex0 or texture of the tile that is listed but not downloaded (checked
        // first, a precondition): the tile is built, the host answers that file "not installed locally", and the material
        // says so -- "unavailable" for the tex0, the texture's own state quoted for a texture.
        bool numeric = false;
        const int id = arg.section(':', 3).toInt(&numeric);
        if (!parseTile(arg, t) || !numeric)
        {
          tally.check(label, false, "expected notlocalpart:<map>:<x>:<y>:<tex0 or texture FileDataID>");
          continue;
        }
        if (!texturedPlayer)
        {
          tally.skip(label, noTexturesWhy);
          continue;
        }
        wow::WdtReference reference;
        reference.kind = wow::WdtReference::Kind::FileDataId;
        reference.fileDataId = id;
        const wow::WdtInspection now = frame->mapCatalog()->inspectFile(reference);
        if (now.state != wow::WdtAvailability::NotInstalledLocally)
        {
          tally.skip(label, "precondition changed (now " + wow::WorldMapCatalog::availabilityName(now.state) + ")");
          continue;
        }
        const int load = request(t.map, t.x, t.y, step, why);
        if (load <= 0)
        {
          tally.check(label + ": requested", false, why);
          continue;
        }
        if (checkBuilt(label, load, t.map, t.x, t.y))
          sampleShown(label, afterTile);
        UnityIpcServer::TerrainTileReport r;
        waitReport(load, 1000, r);
        const UnityIpcServer::MapAssetServed * asked = nullptr;
        for (const ServedEntry & e : servedRecent)
          if (e.s.load == load && e.s.fileDataID == id && (e.s.role == "tex0Adt" || e.s.role == "texture"))
            asked = &e.s;
        const UnityIpcServer::TerrainTileReport::Texture * entry = nullptr;
        for (const UnityIpcServer::TerrainTileReport::Texture & x : r.textures)
          if (x.fileDataID == id)
            entry = &x;
        const bool tex0 = asked && asked->role == "tex0Adt";
        tally.check(label + QString(": %1 is answered \"not installed locally\" and the material says so").arg(id),
                    asked && asked->fileDataID == id && asked->state == "not installed locally" &&
                      (tex0 ? r.materialState == "unavailable" && r.materialReason.contains("not installed locally")
                            : entry && entry->state == asked->state && entry->detail == asked->detail),
                    !asked ? QString("the player never asked for %1").arg(id)
                           : QString("asked as %1, answered \"%2\"; material \"%3\" (\"%4\")").arg(asked->role).arg(asked->state)
                               .arg(r.materialState).arg(r.materialReason) +
                               (entry ? QString("; its entry \"%1\" \"%2\"").arg(entry->state).arg(entry->detail) : QString()));
        last = t;
      }
      else if (kind == "failrepeat")
      {
        // failrepeat:<n> (the Azeroth tile 0_0, whose MAIN flag is clear) or failrepeat:<n>:<map>:<x>:<y>.
        bool numeric = false;
        const int n = arg.section(':', 0, 0).toInt(&numeric);
        Tile f = {0, 0, 0};
        if (!numeric || n <= 0 || (!arg.section(':', 1).isEmpty() && !parseTile(arg.section(':', 1), f)))
        {
          tally.check(label, false, "expected failrepeat:<n> or failrepeat:<n>:<map>:<x>:<y> (a tile whose load fails)");
          continue;
        }
        // Each load fails on its own answer, so none ends with a request out: the player's total of retirements stays the
        // one sampled before the first, and no load is left building after any of them.
        UnityIpcServer::RuntimeState before;
        if (!sampleOnce(label + " (before)", before))
        {
          tally.check(label + ": the player answers before the loads", false, "no answer");
          continue;
        }
        int sent = 0, failed = 0, once = 0, pendingZero = 0, retiredSame = 0, notLoading = 0, answers = 0;
        int pendingField = -1, totalField = -1, loadingField = -1;   // the lowest reported (-1: one not reported)
        QString firstBad, firstRetired, firstLoading;
        for (int i = 0; i < n; i++)
        {
          const int load = request(f.map, f.x, f.y, step, why);
          if (load <= 0)
          {
            if (firstBad.isEmpty())
              firstBad = QString("#%1 not requested: ").arg(i + 1) + why;
            continue;
          }
          sent++;
          UnityIpcServer::TerrainTileReport r;
          if (waitReport(load, 60000, r) && r.status == "failed")
            failed++;
          else if (firstBad.isEmpty())
            firstBad = QString("#%1 load %2: %3 %4").arg(i + 1).arg(load).arg(r.status).arg(r.reason);
          UnityIpcServer::RuntimeState now;
          if (sampleOnce(label + QString(" #%1").arg(i + 1), now))
          {
            answers++;
            pendingField = answers == 1 ? now.terrainPendingRequests : std::min(pendingField, now.terrainPendingRequests);
            totalField = answers == 1 ? now.terrainRetiredTotal : std::min(totalField, now.terrainRetiredTotal);
            loadingField = answers == 1 ? now.terrainLoading : std::min(loadingField, now.terrainLoading);
            pendingZero += now.terrainPendingRequests == 0 ? 1 : 0;
            if (now.terrainRetiredTotal == before.terrainRetiredTotal)
              retiredSame++;
            else if (firstRetired.isEmpty())
              firstRetired = QString("; the first otherwise: #%1 load %2, terrainRetiredTotal %3 (ring %4)").arg(i + 1).arg(load)
                               .arg(now.terrainRetiredTotal).arg(now.terrainRetiredRequests);
            if (now.terrainLoading == 0)
              notLoading++;
            else if (firstLoading.isEmpty())
              firstLoading = QString("; the first otherwise: #%1 load %2, terrainLoading %3").arg(i + 1).arg(load).arg(now.terrainLoading);
          }
          once += reportsFor(load) == 1 ? 1 : 0;
        }
        tally.check(label + QString(": all %1 loads fail, each answered once").arg(n), sent == n && failed == n && once == n,
                    QString("%1 sent, %2 failed, %3 answered once").arg(sent).arg(failed).arg(once) +
                      (firstBad.isEmpty() ? QString() : "; the first otherwise: " + firstBad));
        checkField(label, "terrainRetiredTotal", std::min(before.terrainRetiredTotal, totalField), retiredSame == n,
                   "each fails on its own answer, retiring no request (terrainRetiredTotal as before the first, after each)",
                   QString("%1 of %2 load(s) followed by terrainRetiredTotal %3, the one before the first (its ring then %4)")
                     .arg(retiredSame).arg(n).arg(before.terrainRetiredTotal).arg(before.terrainRetiredRequests) + firstRetired);
        checkField(label, "terrainLoading", loadingField, notLoading == n, "no terrain load building after any of them",
                   QString("%1 of %2 load(s) followed by terrainLoading 0").arg(notLoading).arg(n) + firstLoading);
        checkField(label, "terrainPendingRequests", pendingField, pendingZero == n,
                   "invariant: no request pending with no load in flight, after each",
                   QString("%1 of %2 load(s) followed by an answer with none pending").arg(pendingZero).arg(n));
      }
      else
        tally.check(label, false, "unknown step (tile, repeat, wait, clear, expectfail, notlocal, switch, staleepoch, staleload, "
                                "supersede, model, modelthentile, timeout, latesupersede, lateclear, lateswitch, reconnect, "
                                "failrepeat, expectmaterial, expectserved, notlocalpart)");
    }

    // EVERY LOAD ANSWERED EXACTLY ONCE -- but one in flight when its player was restarted, which no player may answer --
    // and nothing building at the end.
    pumpIpc(ipc, 100);
    for (int load : loadsSent)
    {
      const int answers = reportsFor(load);
      const char first = load > 0 && (size_t)load < answerFirst.size() ? answerFirst[(size_t)load] : 0;
      const QString seen = answers == 0 ? QString("no answer")
                                        : QString("%1 answer(s), the first %2").arg(answers)
                                            .arg(first == 'b' ? "built" : first == 'f' ? "failed" : first == 's' ? "superseded"
                                                 : first == 'c' ? "cleared" : "of another status");
      if (std::find(loadsOrphaned.begin(), loadsOrphaned.end(), load) != loadsOrphaned.end())
        tally.check(QString("load %1, in flight when its player was restarted, is never answered").arg(load), answers == 0, seen);
      else
        tally.check(QString("load %1 is answered exactly once").arg(load), answers == 1, seen);
    }
    tally.check("every terrainTileLoaded names a load this run sent", reportsForUnsentLoads == 0,
                QString("%1 report(s) in all, %2 naming a load never sent%3; %4 load(s) sent, the highest %5")
                  .arg(reportsTotal).arg(reportsForUnsentLoads)
                  .arg(unsentLoadsNamed.isEmpty() ? QString() : " (" + unsentLoadsNamed.join(", ") + ")")
                  .arg(loadsSent.size()).arg(loadsSent.empty() ? 0 : loadsSent.back()));
    UnityIpcServer::RuntimeState end;
    sample("end", end);
    tally.check("no terrain load is building at the end", end.terrainLoading == 0 && !end.loading, end.describe());
    checkConservation("end", end);

    // THE HOST'S BOOKKEEPING, bounded: the server keeps the most recent records and counts every one (the test counted
    // them all as they came), its read tracker holds exactly the files served under its own epoch -- the last a file was
    // read in, or a later one when a client was loaded after that read (emptied then) --, nothing is held or waits, and
    // the poll records are the most recent ones.
    const long long total = servedBeforeRestarts + ipc->mapAssetsServedTotal();
    tally.check("the test counted every getMapAsset answered", servedTotal == total,
                QString("%1 counted (%2 kept), %3 answered (%4 before the last player start)").arg(servedTotal)
                  .arg(servedRecent.size()).arg(total).arg(servedBeforeRestarts));
    const long long keptNow = (long long)ipc->mapAssetsServed().size();
    const long long keptAtMost = (long long)UnityIpcServer::MAP_ASSET_RECORDS_KEPT;
    tally.check(QString("the server keeps the most recent %1 getMapAsset records at most, and counts the rest").arg(keptAtMost),
                keptNow == std::min(ipc->mapAssetsServedTotal(), keptAtMost) &&
                  keptNow + ipc->mapAssetRecordsDropped() == ipc->mapAssetsServedTotal() &&
                  (ipc->mapAssetsServed().empty() ||
                   (!servedRecent.empty() && ipc->mapAssetsServed().back().requestId == servedRecent.back().s.requestId)),
                QString("%1 kept, %2 no longer kept, %3 since the player started").arg(keptNow)
                  .arg(ipc->mapAssetRecordsDropped()).arg(ipc->mapAssetsServedTotal()));
    {
      const MapAssetReadTracker & reads = ipc->mapAssetReads();
      std::set<int> files;   // served under the tracker's own epoch
      for (const std::pair<int, int> & f : filesServed)
        if (f.first == reads.epoch())
          files.insert(f.second);
      int held = 0;
      for (int id : files)
        held += reads.holds(id) ? 1 : 0;
      tally.check("the read tracker holds exactly the files served under its epoch, the last a file was read in or a later "
                  "one (a client loaded since)",
                  reads.size() <= reads.capacity() && reads.epoch() >= lastAvailableEpoch &&
                    reads.epoch() <= UnityAssetAccess::clientEpoch() && reads.size() == files.size() &&
                    held == (int)files.size(),
                  QString("%1 file(s) of epoch %2; %3 distinct file(s) served under it, %4 of them held; the last read under "
                          "epoch %5; the client's epoch %6")
                    .arg(reads.size()).arg(reads.epoch()).arg(files.size()).arg(held).arg(lastAvailableEpoch)
                    .arg(UnityAssetAccess::clientEpoch()));
    }
    tally.check("no getMapAsset is held or waiting at the end, and no hold is armed", ipc->mapAssetsHeld().empty() &&
                  !ipc->testMapAssetHoldArmed() && ipc->mapAssetsWaiting() == 0,
                QString("%1 held, %2 dropped unanswered in the run; %3 waiting, %4 dropped with a connection; %5 refused for a "
                        "full queue, at most %6 waited at once")
                  .arg(ipc->mapAssetsHeld().size()).arg(ipc->mapAssetsHeldDropped()).arg(ipc->mapAssetsWaiting())
                  .arg(ipc->mapAssetsQueueDropped()).arg(ipc->mapAssetsRefusedQueueFull()).arg(ipc->mapAssetQueueDepthMax()));
    tally.check(QString("the server keeps the most recent %1 poll records at most").arg(UnityIpcServer::MAP_ASSET_POLL_TIMINGS_KEPT),
                (long long)ipc->mapAssetPolls().size() ==
                  std::min<long long>(ipc->mapAssetPollsTotal(), UnityIpcServer::MAP_ASSET_POLL_TIMINGS_KEPT),
                QString("%1 kept, %2 polls served map requests since the player started").arg(ipc->mapAssetPolls().size())
                  .arg(ipc->mapAssetPollsTotal()));
  }();

  // How long the GUI thread spent on the map files served, by role (validation, read and queueing the answer, handlerMs;
  // the storage read alone, readMs; the wait in the queue before serving began, queuedMs), the first read of each FileDataID
  // in this process (MapAssetServed::first) apart: a root ADT's, tex0's or texture's is the cold-read candidate; a WDT's is
  // not cold (the load's own request read that WDT whole just before). Files over 4 MiB -- the largest terrain textures --
  // are also summarised alone, and the GUI time each poll spent serving (THE MAP ASSET QUEUE) against the floor measured
  // for one 5,593,604-byte texture read and checked against its content key: 19.7 to 29.9 ms (warm, outside the viewer).
  QJsonObject timingJson;
  if (ipc)
  {
    const auto ms = [](double v) { return v < 0.0 ? QString("none") : QString::number(v, 'f', 3); };
    for (const auto & group : timing)
    {
      double median, p95, max;
      percentiles(group.second.v, median, p95, max);
      QJsonObject g;
      g["count"] = (double)group.second.n;
      g["kept"] = (int)group.second.v.size();
      g["median"] = median;
      g["p95"] = p95;
      g["max"] = max;
      timingJson[group.first] = g;
      LOG_INFO << "[terrain-test] timing " + group.first +
                    QString(": %1 value(s) (%2 kept), median %3 p95 %4 max %5").arg(group.second.n).arg(group.second.v.size())
                      .arg(ms(median)).arg(ms(p95)).arg(ms(max));
    }
    QJsonObject polls;
    polls["stoppedForBudget"] = (double)pollsStopped;
    polls["bytesReadMax"] = (double)pollBytesMax;
    polls["waitingAfterMax"] = pollWaitingMax;
    polls["queueDepthMax"] = ipc->mapAssetQueueDepthMax();
    polls["refusedQueueFull"] = (double)ipc->mapAssetsRefusedQueueFull();
    polls["floorMs"] = "19.7-29.9 (one 5,593,604-byte texture read and checked against its content key, warm)";
    timingJson["polls"] = polls;
    LOG_INFO << QString("[terrain-test] polls that served map files: %1 stopped with requests waiting (the 2 MiB budget or a "
                        "file over 1 MiB), the most bytes read in one %2, the most left waiting %3, the deepest queue %4, %5 "
                        "refused for a full queue; the floor for one 5,593,604-byte texture read and checked: 19.7-29.9 ms")
                  .arg(pollsStopped).arg(pollBytesMax).arg(pollWaitingMax).arg(ipc->mapAssetQueueDepthMax())
                  .arg(ipc->mapAssetsRefusedQueueFull());
  }
  out["timing"] = timingJson;

  // The full reports, records and loads, back from the records file into <out.json> (Tools/world-audit/
  // check_terrain_run.py reads them there); the file is removed once <out.json> is written.
  QJsonArray reportsJson, servedJsonArray, loadsJson;
  if (recording)
  {
    recordsFile.close();
    if (recordsFile.open(QIODevice::ReadOnly))
    {
      while (!recordsFile.atEnd())
      {
        const QByteArray line = recordsFile.readLine().trimmed();
        if (line.isEmpty())
          continue;
        QJsonObject o = QJsonDocument::fromJson(line).object();
        const QString kind = o.value("recordKind").toString();
        o.remove("recordKind");
        if (kind == "report")
          reportsJson.append(o);
        else if (kind == "served")
          servedJsonArray.append(o);
        else if (kind == "load")
          loadsJson.append(o);
      }
      recordsFile.close();
    }
  }
  out["loads"] = loadsJson;
  out["loadsOrphaned"] = [&loadsOrphaned]() {
    QJsonArray a;
    for (int load : loadsOrphaned)
      a.append(load);
    return a;
  }();
  out["reports"] = reportsJson;
  out["reportsForUnsentLoads"] = (double)reportsForUnsentLoads;
  out["served"] = servedJsonArray;
  out["samples"] = samplesJson;
  out["checks"] = tally.checks;
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
  {
    // The player the run used, by its game code (the assembly the player's own build writes).
    const QFileInfo exe(QString::fromWCharArray(UnityRendererHost::resolveUnityExePath().c_str()));
    QFile assembly(exe.absolutePath() + "/" + exe.completeBaseName() + "_Data/Managed/Assembly-CSharp.dll");
    QJsonObject player;
    player["exe"] = exe.absoluteFilePath();
    player["assemblyMd5"] = assembly.open(QIODevice::ReadOnly)
                              ? QString::fromLatin1(QCryptographicHash::hash(assembly.readAll(), QCryptographicHash::Md5).toHex())
                              : QString();
    out["player"] = player;
  }
  if (ipc)
  {
    // The host's map-file bookkeeping at the end, and its private bytes over the run's samples.
    QJsonObject records;
    records["kept"] = (int)ipc->mapAssetsServed().size();
    records["keptAtMost"] = UnityIpcServer::MAP_ASSET_RECORDS_KEPT;
    records["total"] = (double)ipc->mapAssetsServedTotal();
    records["dropped"] = (double)ipc->mapAssetRecordsDropped();
    records["totalBeforeRestarts"] = (double)servedBeforeRestarts;
    records["copy"] = (double)servedTotal;
    records["copyKept"] = (int)servedRecent.size();
    out["mapAssetRecords"] = records;
    QJsonObject queue;
    queue["waiting"] = (int)ipc->mapAssetsWaiting();
    queue["queued"] = (double)ipc->mapAssetsQueued();
    queue["refusedQueueFull"] = (double)ipc->mapAssetsRefusedQueueFull();
    queue["droppedWithConnection"] = (double)ipc->mapAssetsQueueDropped();
    queue["depthMax"] = ipc->mapAssetQueueDepthMax();
    queue["pollsKept"] = (int)ipc->mapAssetPolls().size();
    queue["pollsTotal"] = (double)ipc->mapAssetPollsTotal();
    out["mapAssetQueue"] = queue;
    QJsonObject reads;
    reads["tracked"] = (int)ipc->mapAssetReads().size();
    reads["capacity"] = (int)ipc->mapAssetReads().capacity();
    reads["epoch"] = ipc->mapAssetReads().epoch();
    reads["untracked"] = (double)ipc->mapAssetReads().untracked();
    out["mapAssetReads"] = reads;
    QJsonObject held;
    held["held"] = (int)ipc->mapAssetsHeld().size();
    held["droppedUnanswered"] = (double)ipc->mapAssetsHeldDropped();
    out["mapAssetsHeld"] = held;
  }
  {
    QJsonObject memory;
    long long first = -1, last = -1, max = -1;
    for (long long v : privateSeries)
    {
      if (v < 0)
        continue;
      if (first < 0)
        first = v;
      last = v;
      max = std::max(max, v);
    }
    memory["samples"] = (int)privateSeries.size();
    memory["firstPrivateBytes"] = (double)first;
    memory["lastPrivateBytes"] = (double)last;
    memory["maxPrivateBytes"] = (double)max;
    out["hostMemory"] = memory;
  }
  if (!outPath.isEmpty())
  {
    out["passed"] = tally.passed;
    out["failed"] = tally.failed;
    out["skipped"] = tally.skipped;
    QFile f(outPath);
    const bool written = f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                         f.write(QJsonDocument(out).toJson(QJsonDocument::Indented)) > 0;
    f.close();
    tally.check("the results are written", written, outPath);
    // Its records are in <out.json> now.
    if (written && recording)
      QFile::remove(recordsFile.fileName());
  }
  const bool pass = tally.failed == 0;
  LOG_INFO << QString("[terrain-test] RESULT: %1 (%2 passed, %3 failed, %4 skipped)").arg(pass ? "PASS" : "FAIL")
                .arg(tally.passed).arg(tally.failed).arg(tally.skipped);
  std::printf("WMV: terrain test %s (%d passed, %d failed, %d skipped)\n", pass ? "PASS" : "FAIL", tally.passed, tally.failed,
              tally.skipped);
  std::fflush(stdout);

  if (ipc)
  {
    ipc->onTerrainTileLoaded = previousTerrain;
    ipc->onRuntimeState = previousRuntime;
    ipc->onMapAssetServed = previousServed;
    ipc->onMapAssetPoll = previousPoll;
    ipc->testCancelMapAssetHold();
  }
  if (frame->unityRendererHost && frame->unityRendererHost->isRunning())
  {
    frame->unityRendererHost->shutdown();
    LOG_INFO << "[terrain-test] player shut down; still running=" << (frame->unityRendererHost->isRunning() ? 1 : 0);
  }
  return tally.failed;
}

// A batch run that loads something and has no other job ends here. It used to write an ss_*.png
// screenshot of the OpenGL viewport; that viewport is archived and the Unity viewport has no capture
// yet, so the run says so once instead of writing a picture of a renderer nobody sees.
// Milliseconds since this process was created (its creation time, as Windows keeps it).
static long long processUptimeMs()
{
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return -1;
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  const auto ticks = [](const FILETIME & t) { return ((long long)t.dwHighDateTime << 32) | t.dwLowDateTime; };
  return (ticks(now) - ticks(created)) / 10000; // 100 ns units
}

static void logNoHeadlessScreenshot()
{
  LOG_INFO << "Headless run done: screenshots are not available in the Unity-only viewer, so no ss_*.png was written.";
}

bool WowModelViewApp::OnInit()
{
  // A restart (ModelViewer::Relaunch): wait for the instance that started this one to have exited, so it
  // has saved its layout and settings and let go of the log before this one reads and opens them.
  for (int ai = 1; ai + 1 < argc; ai++)
    if (wxString(argv[ai]) == wxT("-waitpid"))
    {
      unsigned long pid = 0;
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
      break;
    }

  // Held while the viewer runs, so its installer and uninstaller ask to close it first (AppMutex in
  // installer/WoWModelViewer.iss). Never closed: Windows lets go of it when the process ends.
  ::CreateMutexW(nullptr, FALSE, L"WoWModelViewer-B7E9F3A2-1C4D-4E8A-9F6B-2A3C5D7E9F10");

  // What the viewer reads and writes beside itself (plugins\, games\, dbd\, the listfile and keys, dbcache\, the log)
  // is found relative to the working directory: the folder of the exe, however it was started (a shortcut with
  // another "Start in", a console in another folder). A file named on the command line is still found from the folder
  // it was started in (launchPath).
  const wxString launchDir = wxGetCwd();
  wxSetWorkingDirectory(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
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
        a == "-mapcatalogtest" || a == "-terraintile" || a.endsWith(".chr"))
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
    const wxString settingsFile = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(wxPATH_GET_VOLUME) +
                                  SLASH + wxT("userSettings") + SLASH + wxT("Config.ini");
    QSettings config(QString::fromWCharArray(settingsFile.wc_str()), QSettings::IniFormat);
    UiStyle::setThemePreference(UiStyle::themeFromSetting(config.value("Settings/Appearance", 0).toInt()));
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
  QString fbxEquipmentPath; // -fbxequipment <snapshot.chr>: restore equipment after -mo/-npc
  QString mpqDataFolder;  // -mpq <DataFolder> [locale]: load a legacy MPQ client instead of CASC
  QString mpqLocale;      // optional locale for -mpq (auto-detected when empty)
  int dumpTexFileDataId = 0; QString dumpTexOutPath; // -dumptex <fileDataID> <out.png>: forensic-only
  bool matResTest = false;                           // -matrestest: replaceable-material checks
  bool customizationTest = false;                    // -customizationtest: character customization checks
  QString mapCatalogTestOut;                         // -mapcatalogtest <out.json>: the world map catalogue test
  QStringList mapCatalogClients, mapCatalogProbes;   // -mapcatalogclients p,p,... / -mapcatalogprobe id|path,...
  QString mapCatalogWdtFolder; QStringList mapCatalogWdtMaps; // -mapcatalogwdt <folder> <map id,...>
  bool terrainTest = false;                          // -terraintile <map id> <x> <y> [<out.json>]: the terrain tile test
  int terrainMap = -1, terrainX = -1, terrainY = -1; QString terrainOut;
  QString m2InspectList, m2InspectOut;               // -m2inspect <list.txt> [out.csv]: forensic-only
  // Export content selection + clip list for the headless FBX export (the parent process passes
  // these so the child reproduces the user's exact options). Defaults: full content, no explicit
  // clips (the exporter falls back to none/first-N only if -fbxanim and no -fbxclips).
  int optMesh = 1, optSkel = 1, optSkin = 1, optAnim = 1;
  int optComponent = 0;   // -fbxcomponent : opt-in raw/node-based item-component export (UV2 + raw units + sidecar v2)
  int itemSkinFileId = 0; // -itemskin <fileDataID> : re-bind an item/weapon's on-screen skin after -mo load
  bool unityIpcTest = false; // -unityipctest : with -mo, -item or -wmo, run the embedded Unity renderer IPC self-test (see doHeadlessUnityIpcTest)
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
    else if (cmd == "-terraintile") {
      // One terrain tile drawn by the player, and the protocol around it; see doHeadlessTerrainTileTest.
      if (i + 3 < argc)
      {
        terrainTest = true;
        terrainMap = QString::fromWCharArray(argv[++i]).toInt();
        terrainX = QString::fromWCharArray(argv[++i]).toInt();
        terrainY = QString::fromWCharArray(argv[++i]).toInt();
        if (i + 1 < argc && !QString::fromWCharArray(argv[i + 1]).startsWith('-'))
          terrainOut = launchPath(argv[++i]);
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
    if (a == "-m" || a == "-mo" || a == "-armory" || a == "-npc" || a == "-fbxexport" || a == "-animdump" || a == "-fbxinspect" || a == "-dbfromfile" || a == "-dumptex" || a == "-m2inspect" || a == "-matrestest" || a == "-customizationtest" || a == "-mpq" || a == "-item" || a == "-wmo" || a == "-mapcatalogtest" || a == "-terraintile" || a.endsWith(".chr"))
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

    if (terrainTest)
    {
      doHeadlessTerrainTileTest(frame, terrainMap, terrainX, terrainY, terrainOut);
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
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath);
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
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath);
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
        doHeadlessFbxExport(frame, fbxExportPath, optMesh != 0, optSkel != 0, optSkin != 0, optAnim != 0, fbxClipsArg, optComponent != 0, fbxEquipmentPath);
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
    frame->WarmStartUnityViewport();
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

  config.setValue("Locale/LanguageID", langID);
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
