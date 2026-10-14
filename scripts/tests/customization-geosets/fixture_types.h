#pragma once
#include <vector>

// One character model of one client, as its own files have it: the geoset id of every submesh of the model's
// first .skin (in file order; an id with two submeshes appears twice), its customization options in client
// order (OrderIndex, then ID) with their choices in client order, and every geoset element of those choices
// (in element ID order): the model geoset id it sets (GeosetType * 100 + GeosetID), its GeosetType and its
// RelatedChrCustomizationChoiceID (0 = unconditional).
struct FixtureOption
{
  unsigned id;
  const char * name;
  std::vector<unsigned> choices;
  std::vector<const char *> choiceNames;
};

struct FixtureElement
{
  unsigned choice;
  int geoset;
  int type;
  unsigned related;
};

struct Fixture
{
  const char * client;
  const char * build;
  unsigned chrModel;
  const char * label;
  unsigned fileDataId;
  std::vector<int> geosets;
  std::vector<FixtureOption> options;
  std::vector<FixtureElement> elements;
};
