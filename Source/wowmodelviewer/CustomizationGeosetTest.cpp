#include "CustomizationGeosetTest.h"

// -customizationtest, every client: the geosets customization choices show, on the loaded client's own characters and
// records. A ChrCustomizationGeoset element sets the VALUE of its geoset group (GeosetType; a GeosetID of 0 is "none")
// and the model draws one variant per group, so after any selection, in every group the selection's elements set
// (an element applies when its related choice is 0 or part of the selection), the model shows exactly the variants
// set there, with all their submeshes, and nothing else. The elements are read from the client's tables here, not
// through CharDetails. Checked:
//   - every choice the Appearance panel offers, option by option, on both sexes of every playable race;
//   - the Undead male's jaw for each Jaw Features (Features) choice, against the jaw the client's records give (the
//     model's own default jaw, geoset 101, never shows beside a chosen one);
//   - a sequence of jaw changes with another facial option between them, a reset, another character and back, a
//     fresh character given the same selection, a saved character loading back and, on Classic Beta, the Classic | HD
//     switch and back.
// The expected jaws were read from the clients' DB2 files and models (Retail 12.1.5.70391, MoP Classic 5.5.4.70268,
// Classic Era 1.15.9.70003, Classic Beta 1.60.1.70374) by a reader independent of the viewer. A later client may
// re-author them: a failure then means "the data moved, go look", not necessarily "the code broke".

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <vector>

#include <QDir>
#include <QFile>
#include <QStringList>

#include <wx/app.h>

#include "CharDetails.h"
#include "Game.h"
#include "RaceInfos.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"
#include "WoWItem.h"
#include "WoWModel.h"
#include "modelviewer.h"

namespace
{
typedef std::function<void(const QString &, bool, const QString &)> Check;

void settle()
{
  for (int i = 0; i < 20; i++)
    wxTheApp->Yield(true);
}

QString join(const std::vector<int> & ids)
{
  QStringList parts;
  for (const int id : ids)
    parts << QString::number(id);
  return parts.isEmpty() ? QString("(none)") : parts.join(QLatin1Char(' '));
}

// A character as the Characters list loads it, with nothing equipped (the viewer keeps equipment over a load).
WoWModel * loadCharacter(ModelViewer * frame, int race, int sex)
{
  const int fileID = RaceInfos::getFileIDForRaceSex(race, sex);
  GameFile * file = fileID > 0 ? GAMEDIRECTORY.getFile(fileID) : nullptr;
  if (!file)
    return nullptr;
  frame->LoadModel(file, race, sex);
  settle();
  WoWModel * m = frame->riderModel();
  if (!m || m->infos.raceID != race || m->infos.ChrModelID.empty())
    return nullptr;
  bool dressed = false;
  for (int s = 0; s < NUM_CHAR_SLOTS; s++)
    if (WoWItem * item = m->getItem((CharSlots)s))
      if (item->id() > 0 || item->displayId() > 0)
      {
        item->setId(0);
        dressed = true;
      }
  if (dressed)
    m->refresh();
  return m;
}

// The model's own geosets that show, ascending (an id with two submeshes twice).
std::vector<int> visibleGeosets(const WoWModel * m)
{
  std::vector<int> v;
  for (size_t i = 0; i < m->ownGeosetCount() && i < m->geosets.size(); i++)
    if (m->geosets[i]->display)
      v.push_back(m->geosets[i]->id);
  std::sort(v.begin(), v.end());
  return v;
}

std::vector<int> inGroups(const std::vector<int> & ids, std::initializer_list<int> groups)
{
  std::vector<int> v;
  for (const int id : ids)
    if (id != 0 && std::find(groups.begin(), groups.end(), id / 100) != groups.end())
      v.push_back(id);
  return v;
}

// In every geoset group the model's selection sets, the variants that show must be exactly the ones it sets there.
// Returns the first group that differs, or nothing.
QString groupsDiffer(const WoWModel * m)
{
  const std::map<uint, uint> selection = m->cd.captureAppearance().selection;
  std::set<uint> active;
  QStringList choices;
  for (const auto & s : selection)
  {
    active.insert(s.second);
    choices << QString::number(s.second);
  }
  std::set<int> shown, groups;
  if (!choices.isEmpty())
  {
    sqlResult r = GAMEDATABASE.sqlQuery(QString(
      "SELECT ChrCustomizationGeoset.GeosetType, ChrCustomizationGeoset.GeosetID, ChrCustomizationElement.RelatedChrCustomizationChoiceID "
      "FROM ChrCustomizationElement JOIN ChrCustomizationGeoset ON ChrCustomizationGeoset.ID = ChrCustomizationElement.ChrCustomizationGeosetID "
      "WHERE ChrCustomizationElement.ChrCustomizationChoiceID IN (%1)").arg(choices.join(QLatin1Char(','))));
    for (size_t i = 0; r.valid && i < r.values.size(); i++)
    {
      const uint related = r.values[i][2].toUInt();
      if (related != 0 && !active.count(related))
        continue;
      shown.insert(r.values[i][0].toInt() * 100 + r.values[i][1].toInt());
      groups.insert(r.values[i][0].toInt());
    }
  }
  for (const int g : groups)
  {
    std::vector<int> showing, set;
    for (size_t i = 0; i < m->ownGeosetCount() && i < m->geosets.size(); i++)
    {
      const int id = m->geosets[i]->id;
      if (id == 0 || id / 100 != g)
        continue;
      if (m->geosets[i]->display)
        showing.push_back(id);
      if (shown.count(id))
        set.push_back(id);
    }
    std::sort(showing.begin(), showing.end());
    std::sort(set.begin(), set.end());
    if (showing != set)
      return QString("group %1 shows %2, the selection sets %3").arg(g).arg(join(showing)).arg(join(set));
  }
  return QString();
}

// The options whose choices set geosets, or gate an element that does (Eyesight on Eye Color): the ones a sweep goes
// through. A colour with no geoset element changes nothing here.
std::set<uint> geosetOptions(uint chrModel)
{
  std::set<uint> options;
  sqlResult r = GAMEDATABASE.sqlQuery(QString(
    "SELECT DISTINCT ChrCustomizationChoice.ChrCustomizationOptionID FROM ChrCustomizationChoice "
    "JOIN ChrCustomizationOption ON ChrCustomizationOption.ID = ChrCustomizationChoice.ChrCustomizationOptionID "
    "WHERE ChrCustomizationOption.ChrModelID = %1 AND (ChrCustomizationChoice.ID IN (SELECT ChrCustomizationChoiceID "
    "FROM ChrCustomizationElement WHERE ChrCustomizationGeosetID != 0) OR ChrCustomizationChoice.ID IN "
    "(SELECT RelatedChrCustomizationChoiceID FROM ChrCustomizationElement WHERE ChrCustomizationGeosetID != 0))").arg(chrModel));
  for (size_t i = 0; r.valid && i < r.values.size(); i++)
    options.insert(r.values[i][0].toUInt());
  return options;
}

// The geoset groups an option's choices set.
std::set<int> optionGroups(uint option)
{
  std::set<int> groups;
  sqlResult r = GAMEDATABASE.sqlQuery(QString(
    "SELECT DISTINCT ChrCustomizationGeoset.GeosetType FROM ChrCustomizationChoice "
    "JOIN ChrCustomizationElement ON ChrCustomizationElement.ChrCustomizationChoiceID = ChrCustomizationChoice.ID "
    "JOIN ChrCustomizationGeoset ON ChrCustomizationGeoset.ID = ChrCustomizationElement.ChrCustomizationGeosetID "
    "WHERE ChrCustomizationChoice.ChrCustomizationOptionID = %1").arg(option));
  for (size_t i = 0; r.valid && i < r.values.size(); i++)
    groups.insert(r.values[i][0].toInt());
  return groups;
}

// Every choice the panel offers of every geoset option, option by option, then the option's choice from before again.
void sweep(ModelViewer * frame, const QString & who, int race, int sex, const Check & check)
{
  WoWModel * m = loadCharacter(frame, race, sex);
  if (!m)
  {
    check(who + ": loads", false, QString("race %1 sex %2").arg(race).arg(sex));
    return;
  }
  const std::set<uint> options = geosetOptions(m->infos.ChrModelID[0]);
  int selections = 0, bad = 0;
  QString first;
  const auto verify = [&](const QString & after) {
    selections++;
    const QString d = groupsDiffer(m);
    if (!d.isEmpty() && bad++ == 0)
      first = after + ": " + d;
  };
  verify("the defaults");
  for (const uint option : m->cd.getCustomizationOptions())
  {
    if (!options.count(option))
      continue;
    const uint before = m->cd.get(option);
    for (const uint choice : m->cd.getCustomizationChoices(option))
    {
      m->cd.set(option, choice);
      verify(QString("option %1 choice %2").arg(option).arg(choice));
    }
    m->cd.set(option, before);
    verify(QString("option %1 back to %2").arg(option).arg(before));
  }
  check(QString("%1 (ChrModel %2): %3 selections, each showing exactly the variants it sets in every geoset group it sets")
          .arg(who).arg(m->infos.ChrModelID[0]).arg(selections), bad == 0, first);
}

struct Jaw
{
  uint choice;
  const char * name;
  std::vector<int> jaw; // geoset groups 1 and 2: the flesh jaw (1xx) and the bone jaws (2xx); a two-submesh jaw twice
};

struct JawOption
{
  const char * product;
  uint chrModel, option;
  std::vector<Jaw> jaws;
};

// The Undead male's jaw choices and the jaw each shows, from the client's records (see the file's header).
const std::vector<JawOption> & undeadMaleJaws()
{
  static const std::vector<JawOption> all = {
    { "wow", 9, 62, { { 967, "Intact", { 102 } }, { 968, "Rot-Kissed", { 102 } }, { 971, "Drooler", { 104 } },
                      { 983, "Slobber", { 104 } }, { 969, "Slackjawed", { 103 } }, { 975, "Bonejawed", { 202, 202 } },
                      { 976, "Jawsome", { 204 } }, { 977, "Toothy", { 203 } }, { 978, "Unhinged", { 205 } },
                      { 979, "Cheeky", { 102 } }, { 980, "Loose", { 103 } } } },
    { "wow_classic_beta", 9, 62, { { 967, "Intact", { 102 } }, { 968, "Rot-Kissed", { 102 } }, { 971, "Drooler", { 104 } },
                                   { 969, "Slackjawed", { 103 } }, { 975, "Bonejawed", { 202, 202 } }, { 976, "Jawsome", { 204 } },
                                   { 977, "Toothy", { 203 } }, { 978, "Toothy Slackjaw", { 205 } }, { 979, "Cheeky", { 102 } },
                                   { 980, "Cheeky Slackjaw", { 103 } } } },
    { "wow_classic_beta", 265, 9480, { { 78852, "Intact", { 102 } }, { 78853, "Slackjawed", { 103 } }, { 78854, "Drooler", { 104 } },
                                       { 78875, "Rot-Kissed", { 102 } }, { 78876, "Bonejawed", { 102 } }, { 78877, "Jawsome", { 103 } },
                                       { 78878, "Toothy", { 102 } }, { 78879, "Toothy Slackjaw", { 103 } }, { 78880, "Cheeky", { 102 } },
                                       { 78881, "Cheeky Slackjaw", { 103 } } } },
    { "wow_classic", 9, 62, { { 17601, "Intact", { 102 } }, { 17602, "Rot-Kissed", { 102 } }, { 17603, "Strapped", { 102 } },
                              { 17604, "Strapped Slackjaw", { 103 } }, { 17605, "Strapped Drooler", { 104 } }, { 17606, "Rotting", { 102 } },
                              { 17607, "Rotting Slackjaw", { 103 } }, { 17608, "Rotting Drooler", { 104 } }, { 17609, "Bonejawed", { 102 } },
                              { 17610, "Jawsome", { 103 } }, { 17611, "Toothy", { 102 } }, { 17612, "Toothy Slackjaw", { 103 } },
                              { 17613, "Cheeky", { 102 } }, { 17614, "Cheeky Slackjaw", { 103 } }, { 17615, "Putrid", { 102 } },
                              { 17616, "Putrid Slackjaw", { 103 } }, { 17617, "Putrid Drooler", { 104 } } } },
    { "wow_classic_era", 9, 62, { { 17601, "", { 102 } }, { 17602, "", { 102 } }, { 17603, "", { 102 } }, { 17604, "", { 103 } },
                                  { 17605, "", { 104 } }, { 17606, "", { 102 } }, { 17607, "", { 103 } }, { 17608, "", { 104 } },
                                  { 17609, "", { 102 } }, { 17610, "", { 103 } }, { 17611, "", { 102 } }, { 17612, "", { 103 } },
                                  { 17613, "", { 102 } }, { 17614, "", { 103 } }, { 17615, "", { 102 } }, { 17616, "", { 103 } },
                                  { 17617, "", { 104 } } } },
  };
  return all;
}

const JawOption * jawOption(const QString & product, uint chrModel)
{
  for (const JawOption & o : undeadMaleJaws())
    if (product == o.product && chrModel == o.chrModel)
      return &o;
  return nullptr;
}

std::vector<int> sorted(std::vector<int> v)
{
  std::sort(v.begin(), v.end());
  return v;
}

// Each jaw choice from the defaults: the jaw the records give, and the full picture as the records give it.
void checkJaws(WoWModel * m, const JawOption & o, const QString & who, const Check & check)
{
  const std::vector<uint> offered = m->cd.getCustomizationChoices(o.option);
  const uint start = m->cd.get(o.option);
  for (const Jaw & j : o.jaws)
  {
    const QString what = QString("%1: %2 option %3 choice %4 %5 shows jaw %6").arg(who).arg(m->gamefile ? m->gamefile->fileDataId() : 0)
                           .arg(o.option).arg(j.choice).arg(QString::fromLatin1(j.name)).arg(join(j.jaw));
    if (std::find(offered.begin(), offered.end(), j.choice) == offered.end())
    {
      check(what, false, "the choice is not offered");
      continue;
    }
    m->cd.set(o.option, j.choice);
    const std::vector<int> jaw = inGroups(visibleGeosets(m), { 1, 2 });
    const QString d = groupsDiffer(m);
    check(what, jaw == sorted(j.jaw) && d.isEmpty(), "groups 1 and 2 show " + join(jaw) + (d.isEmpty() ? QString() : "; " + d));
  }
  m->cd.set(o.option, start);
}

const Jaw * findJaw(const JawOption & o, uint choice)
{
  for (const Jaw & j : o.jaws)
    if (j.choice == choice)
      return &j;
  return nullptr;
}
} // namespace

void checkCustomizationGeosets(ModelViewer * frame, const Check & check, const std::function<void(const QString &, const QString &)> & skip)
{
  const QString product = GAMEDIRECTORY.clientProfile().product;
  const QString client = GAMEDIRECTORY.clientProfile().product + " " + GAMEDIRECTORY.clientProfile().versionString;

  // ---- the Undead male's jaw, choice by choice ----
  WoWModel * m = loadCharacter(frame, 5, 0);
  const JawOption * jaws = m ? jawOption(product, m->infos.ChrModelID[0]) : nullptr;
  if (!m)
    check("customization geosets: the Undead male loads (race 5, sex 0)", false, client);
  else if (!jaws)
    skip("customization geosets: the Undead male's jaw", QString("no expected jaws for %1 ChrModel %2").arg(product).arg(m->infos.ChrModelID[0]));
  else
  {
    const QString who = QString("customization geosets: Undead male, %1, ChrModel %2, model").arg(client).arg(m->infos.ChrModelID[0]);
    checkJaws(m, *jaws, who, check);

    // ---- transitions: three jaws, the first again, another facial option, a fourth jaw, reset, another character ----
    const std::vector<int> start = visibleGeosets(m);
    std::vector<const Jaw *> distinct; // jaws with different pictures, in the records' order
    for (const Jaw & j : jaws->jaws)
    {
      bool seen = false;
      for (const Jaw * d : distinct)
        seen = seen || d->jaw == j.jaw;
      if (!seen)
        distinct.push_back(&j);
    }
    // Another facial option: the first offered option whose choices set the face features (group 3), else the hair
    // (group 0), with a choice away from its current one.
    uint otherOption = 0, otherChoice = 0;
    for (const int group : { 3, 0 })
      for (const uint option : m->cd.getCustomizationOptions())
      {
        if (otherOption || option == jaws->option || !optionGroups(option).count(group))
          continue;
        for (const uint c : m->cd.getCustomizationChoices(option))
          if (c != m->cd.get(option))
          {
            otherOption = option;
            otherChoice = c;
            break;
          }
      }
    if (distinct.size() < 3 || !otherOption)
      skip("customization geosets: Undead male jaw transitions", QString("%1 distinct jaws, another option %2").arg(distinct.size()).arg(otherOption));
    else
    {
      int bad = 0;
      QString first;
      std::vector<int> pictureA;
      const auto step = [&](const QString & what, uint option, uint choice, const std::vector<int> & jaw) {
        m->cd.set(option, choice);
        const std::vector<int> got = inGroups(visibleGeosets(m), { 1, 2 });
        const QString d = groupsDiffer(m);
        if ((got != sorted(jaw) || !d.isEmpty()) && bad++ == 0)
          first = what + ": jaw " + join(got) + " (records " + join(sorted(jaw)) + ")" + (d.isEmpty() ? QString() : "; " + d);
      };
      // A, B and C are three different jaws (C the default one); the jaw after the other option a fourth where the
      // records have one.
      const Jaw & a = *distinct[1], & b = *distinct[2], & c = *distinct[0], & d = distinct.size() > 3 ? *distinct[3] : b;
      step("jaw A", jaws->option, a.choice, a.jaw);
      pictureA = visibleGeosets(m);
      step("jaw B", jaws->option, b.choice, b.jaw);
      step("jaw C", jaws->option, c.choice, c.jaw);
      step("jaw A again", jaws->option, a.choice, a.jaw);
      const bool backToA = visibleGeosets(m) == pictureA;
      step("another facial option", otherOption, otherChoice, a.jaw);
      step("a jaw again", jaws->option, d.choice, d.jaw);
      check(QString("%1: jaw transitions A %2, B %3, C %4, A, option %5 choice %6, jaw %7 -- each the records' jaw and groups")
              .arg(who).arg(a.choice).arg(b.choice).arg(c.choice).arg(otherOption).arg(otherChoice).arg(d.choice), bad == 0, first);
      check(who + ": returning to jaw A gives jaw A's whole picture back", backToA, QString());

      // a fresh character given the same selection shows the same
      const std::map<uint, uint> selection = m->cd.captureAppearance().selection;
      const std::vector<int> picture = visibleGeosets(m);
      WoWModel * fresh = loadCharacter(frame, 5, 0);
      if (fresh)
        fresh->cd.applyAppearance(selection, false, true);
      check(who + ": a freshly loaded Undead male given the same selection shows the same geosets",
            fresh && visibleGeosets(fresh) == picture, fresh ? join(visibleGeosets(fresh)) + " vs " + join(picture) : QString("no model"));

      // saved and loaded back
      m = fresh;
      if (m)
      {
        const QString chr = QDir::temp().filePath("wmv_customizationtest_geosets.chr");
        frame->SaveChar(chr);
        frame->LoadChar(chr);
        settle();
        m = frame->riderModel();
        QFile::remove(chr);
        check(who + ": a saved character loads back with the same geosets", m && visibleGeosets(m) == picture,
              m ? join(visibleGeosets(m)) + " vs " + join(picture) : QString("no model"));
      }

      // reset, another character, back
      if (m)
      {
        m->cd.reset();
        check(who + ": reset gives the default geosets back", visibleGeosets(m) == start, join(visibleGeosets(m)) + " vs " + join(start));
      }
      WoWModel * female = loadCharacter(frame, 5, 1);
      const QString femaleDiff = female ? groupsDiffer(female) : QString("no model");
      m = loadCharacter(frame, 5, 0);
      check(who + ": the Undead female in between, then the male again: the default geosets",
            female && femaleDiff.isEmpty() && m && visibleGeosets(m) == start,
            femaleDiff + (m ? " male " + join(visibleGeosets(m)) : QString(" no male")));
    }

    // ---- Classic Beta: the Classic | HD switch and back ----
    if (m && product == "wow_classic_beta")
    {
      const JawOption * hd = jawOption(product, 9), * classic = jawOption(product, 265);
      const Jaw * slack = hd ? findJaw(*hd, 969) : nullptr, * classicSlack = classic ? findJaw(*classic, 78853) : nullptr;
      wxString why;
      m->cd.set(62, 969);
      const std::vector<int> before = visibleGeosets(m);
      const bool toClassic = frame->SwitchCharacterVariant(CharacterModelVariant::Classic, why);
      settle();
      WoWModel * c = frame->riderModel();
      const std::vector<int> classicJaw = c ? inGroups(visibleGeosets(c), { 1, 2 }) : std::vector<int>();
      const QString classicDiff = c ? groupsDiffer(c) : QString("no model");
      check(QString("customization geosets: %1 Undead male Slackjawed, switched to Classic (ChrModel 265): jaw %2 (choice 78853)")
              .arg(client).arg(classicSlack ? join(classicSlack->jaw) : QString("?")),
            toClassic && c && c->cd.get(9480) == 78853 && classicSlack && classicJaw == sorted(classicSlack->jaw) && classicDiff.isEmpty(),
            QString("ok %1 (%2), choice %3, jaw %4 %5").arg(toClassic).arg(QString::fromWCharArray(why.wc_str())).arg(c ? c->cd.get(9480) : 0)
              .arg(join(classicJaw)).arg(classicDiff));
      const bool toHD = frame->SwitchCharacterVariant(CharacterModelVariant::HD, why);
      settle();
      WoWModel * h = frame->riderModel();
      check(QString("customization geosets: %1 Undead male, switched back to HD: Slackjawed, jaw %2, the geosets before the switch")
              .arg(client).arg(slack ? join(slack->jaw) : QString("?")),
            toHD && h && h->cd.get(62) == 969 && visibleGeosets(h) == before,
            QString("ok %1, choice %2, %3 vs %4").arg(toHD).arg(h ? h->cd.get(62) : 0).arg(h ? join(visibleGeosets(h)) : QString()).arg(join(before)));
      // the Classic model's own jaws, choice by choice
      if (frame->SwitchCharacterVariant(CharacterModelVariant::Classic, why))
      {
        settle();
        if (WoWModel * cm = frame->riderModel())
          checkJaws(cm, *classic, QString("customization geosets: Undead male, %1, ChrModel 265, model").arg(client), check);
      }
      else
        check("customization geosets: Classic Beta Undead male switches to Classic for its jaws", false, QString::fromWCharArray(why.wc_str()));
    }
  }

  // ---- every playable race, both sexes: every geoset choice the panel offers ----
  // (a model several races share -- Pandaren, Dracthyr, Earthen, Haranir -- once)
  sqlResult races = GAMEDATABASE.sqlQuery("SELECT ID, Name_Lang FROM ChrRaces WHERE PlayableRaceBit >= 0 ORDER BY ID");
  int characters = 0;
  std::set<int> files;
  for (size_t i = 0; races.valid && i < races.values.size(); i++)
    for (int sex = 0; sex < 2; sex++)
    {
      const int race = races.values[i][0].toInt();
      const int file = RaceInfos::getFileIDForRaceSex(race, sex);
      if (file <= 0 || !files.insert(file).second)
        continue;
      characters++;
      sweep(frame, QString("customization geosets: %1 %2 %3").arg(client).arg(races.values[i][1]).arg(sex == 0 ? "male" : "female"),
            race, sex, check);
    }
  check(QString("customization geosets: %1 playable characters swept").arg(characters), characters > 0, client);
}
