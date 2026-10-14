#pragma once
// The members the extracted production code uses, and nothing else: CharDetails::applyCustomizationGeosets() reads
// the current selection, the options' choices and their geoset elements and writes into the model;
// WoWModel::setGeosetDisplayById() and showGeoset() and the default rule of WoWModel::refresh() work on the
// model's geosets. getChoiceGeosetElements() serves the fixture's elements the way the production query orders
// them (by element ID), in the production element type.
#include <QString>

#include <bitset>
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

typedef unsigned int uint;

enum CharGeosets
{
  CG_TABARD = 12
};

struct ModelGeosetHD
{
  int id = 0;
  bool display = false;
};

class WoWModel
{
public:
  std::vector<ModelGeosetHD *> rawGeosets;
  std::vector<ModelGeosetHD *> geosets;

  size_t ownGeosetCount() const { return rawGeosets.size(); }
  void showGeoset(uint geosetindex, bool value);
  void setGeosetDisplayById(int geosetId, bool display);
  void applyDefaultGeosets(); // the default rule of WoWModel::refresh()
};

class CharDetails
{
public:
  WoWModel * model_ = nullptr;
  std::map<uint, uint> currentCustomization_;
  std::map<uint, std::vector<uint> > choicesPerOptionMap_;
  struct GeosetElement
  {
    int geoset;
    int group;
    uint related;
  };
  std::map<uint, std::vector<GeosetElement> > elements;

  const std::vector<GeosetElement> & getChoiceGeosetElements(uint chrCustomizationChoiceID)
  {
    return elements[chrCustomizationChoiceID];
  }
  void applyCustomizationGeosets();
};
