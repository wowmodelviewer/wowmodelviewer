/*
 * LiquidFormat.cpp  (partial port of Wow/World/LiquidFormat.cs: the vertex formats and their rule)
 */

#include "LiquidFormat.h"

namespace wmv
{
namespace world
{
int LiquidFormat::BytesPerVertex(int format)
{
  switch (format)
  {
    case HeightDepth: return 5;
    case HeightUv: return 8;
    case DepthOnly: return 1;
    case HeightUvDepth: return 9;
    default: return 0;
  }
}

int LiquidFormat::FormatOfBytesPerVertex(int bytes)
{
  switch (bytes)
  {
    case 5: return HeightDepth;
    case 8: return HeightUv;
    case 1: return DepthOnly;
    case 9: return HeightUvDepth;
    default: return None;
  }
}

LiquidFormatDecision LiquidFormat::Decide(const LiquidInstance& instance, const ILiquidCatalog* catalog)
{
  LiquidFormatDecision d;
  d.Format = None;
  d.Source = LiquidFormatSource::None;
  int lvf = -1;
  if (instance.ObjectOrFormat <= 2)
  {
    d.Format = instance.ObjectOrFormat;
    d.Source = LiquidFormatSource::Word;
  }
  else if (instance.LiquidType == OceanType)
  {
    d.Format = DepthOnly;
    d.Source = LiquidFormatSource::OceanType;
  }
  else if (catalog != nullptr && catalog->TryGetLvf(instance.LiquidType, lvf) && lvf >= 0 && lvf <= 3)
  {
    d.Format = lvf;
    d.Source = LiquidFormatSource::Catalog;
  }
  else if (instance.FormatBySize >= 0)
  {
    d.Format = instance.FormatBySize;
    d.Source = LiquidFormatSource::Size;
  }
  d.Disagrees = d.Source != LiquidFormatSource::Size && instance.FormatBySize >= 0 && d.Format != instance.FormatBySize;
  return d;
}
} // namespace world
} // namespace wmv
