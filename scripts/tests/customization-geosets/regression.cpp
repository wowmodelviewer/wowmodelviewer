// Customization geosets over real client records: the production CharDetails::applyCustomizationGeosets() (with the
// default rule of WoWModel::refresh() before it) on the models and ChrCustomization records of fixtures.h, read from
// the installed clients' own files. See README.md.
#include "receiver.h"
#include "fixtures.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
int passed = 0, failed = 0;

void check(bool ok, const std::string & what, const std::string & detail = std::string())
{
  ok ? passed++ : failed++;
  std::printf("[customization-geosets-test] %s %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : " -- ",
              detail.c_str());
}

std::string join(const std::vector<int> & ids)
{
  std::string s;
  for (int id : ids)
    s += (s.empty() ? "" : " ") + std::to_string(id);
  return s.empty() ? "(none)" : s;
}

const Fixture & fixture(const std::string & client, unsigned chrModel)
{
  for (const Fixture & f : fixtures())
    if (client == f.client && f.chrModel == chrModel)
      return f;
  std::printf("[customization-geosets-test] no fixture %s ChrModel %u\n", client.c_str(), chrModel);
  std::exit(2);
}

std::string name(const Fixture & fx)
{
  return std::string(fx.client) + " " + fx.build + " " + fx.label + " (ChrModel " + std::to_string(fx.chrModel) + ")";
}

typedef std::map<uint, uint> Selection; // option -> choice

// The first choice of every option, in client order: a selection every model has.
Selection firstChoices(const Fixture & fx)
{
  Selection s;
  for (const FixtureOption & o : fx.options)
    if (!o.choices.empty())
      s[o.id] = o.choices.front();
  return s;
}

// One character: the fixture's model and records, and the production code over them.
struct Character
{
  const Fixture & fx;
  std::vector<std::unique_ptr<ModelGeosetHD> > owned;
  WoWModel model;
  CharDetails cd;

  explicit Character(const Fixture & f) : fx(f)
  {
    for (int id : fx.geosets)
    {
      owned.emplace_back(new ModelGeosetHD());
      owned.back()->id = id;
      model.rawGeosets.push_back(owned.back().get());
    }
    model.geosets = model.rawGeosets;
    cd.model_ = &model;
    for (const FixtureOption & o : fx.options)
      cd.choicesPerOptionMap_[o.id] = o.choices;
    for (const FixtureElement & e : fx.elements)
      cd.elements[e.choice].push_back(CharDetails::GeosetElement{ e.geoset, e.type, e.related });
    cd.currentCustomization_ = firstChoices(fx);
  }

  // The geoset steps of WoWModel::refresh() up to the customization: the default rule, then the choices.
  std::vector<int> refresh()
  {
    model.applyDefaultGeosets();
    cd.applyCustomizationGeosets();
    return visible();
  }

  std::vector<int> visible() const
  {
    std::vector<int> v;
    for (const ModelGeosetHD * g : model.geosets)
      if (g->display)
        v.push_back(g->id);
    std::sort(v.begin(), v.end());
    return v;
  }
};

// The picture a selection must give, worked out from the records alone, not from the code under test. A geoset
// element sets the value of its geoset group, its GeosetType (the model draws one variant per group): every variant an active
// choice sets is shown (an element applies when its related choice is 0 or active), and in a group an active
// choice sets nothing else is. A variant some choice of the model names, in a group no active choice sets, is
// hidden. Everything else keeps the default rule: geoset 0, ids ending in 01 and 32xx shown; 17xx, 35xx and the
// tabard group 12xx hidden.
bool defaultShown(int id)
{
  const std::string s = std::to_string(id);
  const bool isDefault = id == 0 || (s.size() >= 2 && s.compare(s.size() - 2, 2, "01") == 0) || s.compare(0, 2, "32") == 0;
  const bool hiddenDefault = s.compare(0, 2, "17") == 0 || s.compare(0, 2, "35") == 0;
  return isDefault && !hiddenDefault && id / 100 != 12;
}

std::vector<int> expected(const Fixture & fx, const Selection & selection)
{
  std::set<uint> active;
  for (const auto & s : selection)
    active.insert(s.second);
  std::set<int> named, shown, setGroups;
  for (const FixtureElement & e : fx.elements)
  {
    named.insert(e.geoset);
    if (active.count(e.choice) && (e.related == 0 || active.count(e.related)))
    {
      shown.insert(e.geoset);
      setGroups.insert(e.type);
    }
  }
  std::vector<int> v;
  for (int id : fx.geosets)
  {
    bool on;
    if (id == 0)
      on = true;
    else if (setGroups.count(id / 100))
      on = shown.count(id) != 0;
    else if (named.count(id))
      on = false;
    else
      on = defaultShown(id);
    if (on)
      v.push_back(id);
  }
  std::sort(v.begin(), v.end());
  return v;
}

std::vector<int> groups(const std::vector<int> & ids, std::initializer_list<int> wanted)
{
  std::vector<int> v;
  for (int id : ids)
    if (std::find(wanted.begin(), wanted.end(), id / 100) != wanted.end() && id != 0)
      v.push_back(id);
  return v;
}

std::string choiceName(const Fixture & fx, uint option, uint choice)
{
  for (const FixtureOption & o : fx.options)
    if (o.id == option)
    {
      for (size_t i = 0; i < o.choices.size(); i++)
        if (o.choices[i] == choice)
          return std::string(o.name) + " " + std::to_string(choice) + (o.choiceNames[i][0] ? std::string(" ") + o.choiceNames[i] : "");
      return std::string(o.name) + " " + std::to_string(choice) + " (not a choice of the option)";
    }
  return "option " + std::to_string(option) + " (not on the model)";
}

// The jaw: groups 1 and 2 after one jaw choice, from a default selection. The expected ids are the client's records
// as read (fixtures.h), written out here so a regenerated fixture that lost them fails too.
struct JawCase
{
  const char * client;
  unsigned chrModel, option, choice;
  std::vector<int> jaw;
};

void checkJaws()
{
  const JawCase cases[] = {
    // Retail HD Undead male, Jaw Features: group 1 is the flesh jaw (101 is a copy of the Intact jaw 102 that no
    // choice names), group 2 the bone jaws; a bone jaw sets group 1 to none (100) and a flesh jaw group 2 to none (200).
    { "Retail", 9, 62, 967, { 102 } },      // Intact
    { "Retail", 9, 62, 968, { 102 } },      // Rot-Kissed
    { "Retail", 9, 62, 971, { 104 } },      // Drooler
    { "Retail", 9, 62, 983, { 104 } },      // Slobber
    { "Retail", 9, 62, 969, { 103 } },      // Slackjawed
    { "Retail", 9, 62, 975, { 202, 202 } }, // Bonejawed: a bone jaw of two submeshes
    { "Retail", 9, 62, 976, { 204 } },      // Jawsome
    { "Retail", 9, 62, 977, { 203 } },      // Toothy
    { "Retail", 9, 62, 978, { 205 } },      // Unhinged
    { "Retail", 9, 62, 979, { 102 } },      // Cheeky
    { "Retail", 9, 62, 980, { 103 } },      // Loose
    // Retail HD Undead female: one jaw group (2) and no default variant in it -- unaffected before and after.
    { "Retail", 10, 67, 1037, { 202 } },
    { "Retail", 10, 67, 1041, { 203 } },
    { "Retail", 10, 67, 1042, { 204 } },
    // MoP Classic SD Undead male, Features (jaw and face in one option): 101 is a copy of 102 again.
    { "MoP Classic", 9, 62, 17601, { 102 } }, // Intact
    { "MoP Classic", 9, 62, 17604, { 103 } }, // Strapped Slackjaw
    { "MoP Classic", 9, 62, 17605, { 104 } }, // Strapped Drooler
    { "MoP Classic", 9, 62, 17617, { 104 } }, // Putrid Drooler
    // Classic Era SD Undead male: its model has no 101 -- unaffected before and after.
    { "Classic Era", 9, 62, 17601, { 102 } },
    { "Classic Era", 9, 62, 17604, { 103 } },
    { "Classic Era", 9, 62, 17605, { 104 } },
    // Classic Beta: the HD male (the Retail model and choices) and its Classic variant (ChrModel 265: the SD model
    // with 101-106; its Jaw Features name 102-104 only).
    { "Classic Beta", 9, 62, 969, { 103 } },
    { "Classic Beta", 9, 62, 975, { 202, 202 } },
    { "Classic Beta", 265, 9480, 78852, { 102 } }, // Intact
    { "Classic Beta", 265, 9480, 78853, { 103 } }, // Slackjawed
    { "Classic Beta", 265, 9480, 78854, { 104 } }, // Drooler
  };
  for (const JawCase & c : cases)
  {
    const Fixture & fx = fixture(c.client, c.chrModel);
    Character ch(fx);
    ch.cd.currentCustomization_[c.option] = c.choice;
    const std::vector<int> jaw = groups(ch.refresh(), { 1, 2 });
    std::vector<int> want = c.jaw;
    std::sort(want.begin(), want.end());
    check(jaw == want, name(fx) + ": " + choiceName(fx, c.option, c.choice) + " shows jaw geosets " + join(want),
          "groups 1 and 2 show " + join(jaw));
  }
}

// Every choice of every option of every model, from the first-choice selection: the production picture equals the
// one worked out from the records.
void checkEveryChoice()
{
  for (const Fixture & fx : fixtures())
  {
    int choices = 0, bad = 0;
    std::string first;
    for (const FixtureOption & o : fx.options)
      for (uint c : o.choices)
      {
        Character ch(fx);
        ch.cd.currentCustomization_[o.id] = c;
        const std::vector<int> got = ch.refresh(), want = expected(fx, ch.cd.currentCustomization_);
        choices++;
        if (got != want && bad++ == 0)
          first = choiceName(fx, o.id, c) + ": shows " + join(got) + ", records give " + join(want);
      }
    check(bad == 0, name(fx) + ": all " + std::to_string(choices) + " choices give the records' picture", first);
  }
}

// Specific facts of other races' records, each a geoset the old per-id rule left on beside the chosen variant.
void checkOtherRaces()
{
  struct Case
  {
    const char * client;
    unsigned chrModel;
    std::vector<std::pair<uint, uint> > set; // option, choice
    int hidden;                              // a geoset that must be hidden
    std::vector<int> group;                  // and the group's visible ids
    const char * why;
  };
  const Case cases[] = {
    { "Retail", 1, { { 8789, 56651 } }, 701, { 702 }, "Human male Ears Round: ear 702 alone, the base ear 701 (all of whose vertices 702 repeats) off" },
    { "Retail", 1, { { 525, 5056 } }, 3201, { 3202 }, "Human male Face Shape Narrow: face 3202 alone" },
    { "Retail", 20, { { 583, 6719 } }, 701, { 702 }, "Blood Elf female Ears Long: 702 alone" },
    { "Retail", 22, { { 137, 2011 } }, 2401, { 2402 }, "Draenei female Horns Sweeping: 2402 alone" },
    { "Retail", 69, { { 338, 3335 } }, 4101, { 4102 }, "Vulpera male Snout Long: 4102 alone (4101 is a copy of Short, 4105)" },
    { "Retail", 71, { { 882, 9867 } }, 201, {}, "Mag'har Orc male Sideburns None (200): no sideburns at all" },
    { "Retail", 71, { { 882, 9870 } }, 201, { 211 }, "Mag'har Orc male Sideburns Long: 211 alone" },
    { "Retail", 9, { { 534, 6304 } }, 3301, {}, "Undead male Eye Color Sockets (3300): no eyes" },
    { "Retail", 9, { { 534, 5330 }, { 6346, 45119 } }, 5101, { 5103 }, "Undead male Eye Color with Eyesight Right: 5103 alone" },
    { "Retail", 9, { { 534, 5330 }, { 6346, 45121 } }, 5102, { 5101 }, "Undead male Eye Color with Eyesight Neither: 5101 alone" },
    { "Retail", 46, { { 220, 2487 } }, 32, { 2 }, "Gilnean female Hair Style 2: hair 2 alone, not also hair 32" },
    { "Classic Beta", 2, { { 16, 132 } }, 32, {}, "Classic Beta Human female Hair Style Straight: no stray hair 32" },
    { "MoP Classic", 22, { { 137, 18113 } }, 101, {}, "MoP Classic Draenei female Horn Style Sweeping: base horns 101 off" },
  };
  for (const Case & c : cases)
  {
    const Fixture & fx = fixture(c.client, c.chrModel);
    Character ch(fx);
    std::string what = name(fx) + ": ";
    for (const auto & s : c.set)
    {
      ch.cd.currentCustomization_[s.first] = s.second;
      what += choiceName(fx, s.first, s.second) + "; ";
    }
    const std::vector<int> got = ch.refresh();
    const bool hiddenOk = std::find(got.begin(), got.end(), c.hidden) == got.end();
    std::vector<int> group;
    const int g = c.hidden / 100;
    for (int id : got)
      if (id != 0 && id / 100 == g)
        group.push_back(id);
    const bool groupOk = c.group.empty() ? true : group == c.group;
    check(hiddenOk && groupOk && got == expected(fx, ch.cd.currentCustomization_), what + c.why,
          "group " + std::to_string(g) + " shows " + join(group));
  }
}

// A sequence of selections on one character: after every step the picture equals a fresh character's with the same
// selection, and the records' picture. Returns the final picture.
std::vector<int> runSequence(const Fixture & fx, Character & ch, const std::vector<std::pair<uint, uint> > & steps,
                             const std::string & label, int & bad, std::string & first)
{
  std::vector<int> got = ch.refresh();
  for (const auto & s : steps)
  {
    ch.cd.currentCustomization_[s.first] = s.second;
    got = ch.refresh();
    Character fresh(fx);
    fresh.cd.currentCustomization_ = ch.cd.currentCustomization_;
    const std::vector<int> clean = fresh.refresh(), want = expected(fx, ch.cd.currentCustomization_);
    if ((got != clean || got != want) && bad++ == 0)
      first = label + " after " + choiceName(fx, s.first, s.second) + ": shows " + join(got) + ", a fresh character " + join(clean) +
              ", the records " + join(want);
  }
  return got;
}

void checkTransitions()
{
  const Fixture & male = fixture("Retail", 9), & female = fixture("Retail", 10);
  const uint JAW = 62, FACE = 563;
  Character ch(male);
  const std::vector<int> start = ch.refresh();
  int bad = 0;
  std::string first;
  struct Step { const char * what; uint option, choice; std::vector<int> jaw; };
  const Step steps[] = {
    { "2 Jaw A (Slackjawed)", JAW, 969, { 103 } },
    { "3 Jaw B (Bonejawed)", JAW, 975, { 202, 202 } },
    { "4 Jaw C (Drooler)", JAW, 971, { 104 } },
    { "5 back to A (Slackjawed)", JAW, 969, { 103 } },
    { "6 another facial option (Face Features Strapped)", FACE, 6288, { 103 } },
    { "7 the jaw again (Jawsome)", JAW, 976, { 204 } },
  };
  std::vector<int> afterA;
  for (const Step & s : steps)
  {
    runSequence(male, ch, { { s.option, s.choice } }, s.what, bad, first);
    const std::vector<int> v = ch.visible();
    std::vector<int> want = s.jaw;
    std::sort(want.begin(), want.end());
    check(groups(v, { 1, 2 }) == want, std::string("transition ") + s.what + ": jaw " + join(want), "shows " + join(groups(v, { 1, 2 })));
    if (s.choice == 969 && afterA.empty())
      afterA = v;
    else if (s.choice == 969)
      check(v == afterA, "transition 5: returning to Jaw A gives the picture Jaw A gave", join(v) + " vs " + join(afterA));
  }
  const std::vector<int> face = groups(ch.visible(), { 3 });
  check(face == std::vector<int>{ 302 }, "transition 7: the other facial option (Face Features Strapped, 302) stays", "group 3 shows " + join(face));
  // 8 back to the start selection (the first choices; -customizationtest runs CharDetails::reset itself)
  ch.cd.currentCustomization_ = firstChoices(male);
  check(ch.refresh() == start, "transition 8: the start selection again gives the start picture back");
  // 9 another character and back: the female, then the male again with the selection the male had
  Character other(female);
  other.cd.currentCustomization_[67] = 1041;
  check(groups(other.refresh(), { 1, 2 }) == std::vector<int>{ 203 }, "transition 9: the female in between shows her own jaw (Bonejawed, 203)");
  Character back(male);
  back.cd.currentCustomization_ = ch.cd.currentCustomization_;
  check(back.refresh() == start, "transition 9: back on the male, the start picture");
  // 10 the Classic Beta HD male (ChrModel 9) and its Classic model (265), each from its own records (-customizationtest
  // runs ModelViewer::SwitchCharacterVariant itself)
  const Fixture & hd = fixture("Classic Beta", 9), & classic = fixture("Classic Beta", 265);
  Character a(hd), b(classic), c(hd);
  a.cd.currentCustomization_[62] = 969;
  b.cd.currentCustomization_[9480] = 78853;
  c.cd.currentCustomization_[62] = 969;
  const std::vector<int> ja = groups(a.refresh(), { 1, 2 }), jb = groups(b.refresh(), { 1, 2 }), jc = groups(c.refresh(), { 1, 2 });
  check(ja == std::vector<int>{ 103 } && jb == std::vector<int>{ 103 } && jc == ja,
        "transition 10: Slackjawed on the HD model, its Classic model and the HD model again: 103 on each", join(ja) + " / " + join(jb) + " / " + join(jc));
  check(bad == 0, "transitions 2-7: every step equals a fresh character with that selection and the records' picture", first);

  // The final picture does not depend on the order: every order of four jaws, then Loose, gives Loose's picture.
  std::vector<uint> jaws = { 969, 975, 971, 976 };
  std::sort(jaws.begin(), jaws.end());
  std::vector<int> finalPicture;
  int orders = 0, differ = 0, orderBad = 0;
  std::string orderFirst;
  do
  {
    Character o(male);
    std::vector<std::pair<uint, uint> > seq;
    for (uint j : jaws)
      seq.push_back({ JAW, j });
    seq.push_back({ JAW, 980 });
    const std::vector<int> v = runSequence(male, o, seq, "order " + std::to_string(orders + 1), orderBad, orderFirst);
    if (finalPicture.empty())
      finalPicture = v;
    else if (v != finalPicture)
      differ++;
    orders++;
  } while (std::next_permutation(jaws.begin(), jaws.end()));
  check(differ == 0 && orderBad == 0 && groups(finalPicture, { 1, 2 }) == std::vector<int>{ 103 },
        "all " + std::to_string(orders) + " orders of four jaws, then Loose: one final picture, jaw 103, every step as a fresh character's",
        orderFirst);
}

// An element whose GeosetID is -1 (Retail and Classic Beta ChrCustomizationGeoset 12028: GeosetType 14, GeosetID -1,
// the Dracthyr drake's Breechcloth None) is none for its own group, 14, and leaves group 13 alone. A model of
// geosets 0, 1301 (default-on legs), 1401 and 1402, with one option: None (14, -1) and a variant (1402).
void checkNegativeGeosetId()
{
  Fixture fx{ "synthetic", "-", 0, "GeosetID -1", 0, { 0, 1301, 1401, 1402 },
              { { 1, "Breechcloth", { 10, 11 }, { "None", "Regal" } } },
              { { 10, 1399, 14, 0 }, { 11, 1402, 14, 0 } } };
  Character none(fx);
  const std::vector<int> a = none.refresh();
  Character regal(fx);
  regal.cd.currentCustomization_[1] = 11;
  const std::vector<int> b = regal.refresh();
  check(a == std::vector<int>{ 0, 1301 } && b == std::vector<int>{ 0, 1301, 1402 },
        "GeosetID -1 (type 14): None hides group 14's variants and keeps group 13's 1301; Regal shows 1402", join(a) + " / " + join(b));
}

// Rapid changes: on every model, 2000 changes of a random option to a random choice (a fixed-seed generator), each
// compared with a fresh character and with the records; every 250 changes a reset to the start selection.
void checkRandomSequences()
{
  for (const Fixture & fx : fixtures())
  {
    unsigned long long seed = 0x9E3779B97F4A7C15ull ^ fx.chrModel;
    auto next = [&seed]() {
      seed = seed * 6364136223846793005ull + 1442695040888963407ull;
      return (unsigned)(seed >> 33);
    };
    Character ch(fx);
    const std::vector<int> start = ch.refresh();
    int bad = 0, resets = 0, resetBad = 0;
    std::string first;
    for (int i = 1; i <= 2000; i++)
    {
      const FixtureOption & o = fx.options[next() % fx.options.size()];
      if (o.choices.empty())
        continue;
      runSequence(fx, ch, { { o.id, o.choices[next() % o.choices.size()] } }, "change " + std::to_string(i), bad, first);
      if (i % 250 == 0)
      {
        ch.cd.currentCustomization_ = firstChoices(fx);
        resets++;
        if (ch.refresh() != start)
          resetBad++;
      }
    }
    check(bad == 0 && resetBad == 0, name(fx) + ": 2000 random changes and " + std::to_string(resets) +
          " resets, every picture as a fresh character's and the records'", first);
  }
}
} // namespace

int main()
{
  checkJaws();
  checkEveryChoice();
  checkOtherRaces();
  checkNegativeGeosetId();
  checkTransitions();
  checkRandomSequences();
  std::printf("[customization-geosets-test] RESULT: %s (%d passed, %d failed)\n", failed ? "FAIL" : "PASS", passed, failed);
  return failed ? 1 : 0;
}
