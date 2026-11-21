/*
 * WorldPlacements.cpp  (partial port of Wow/World/WorldPlacements.cs: the record readers)
 */

#include "WorldPlacements.h"

#include <cstring>

namespace wmv
{
namespace world
{
namespace
{
uint32_t Bits(float f)
{
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

// Reads in order (C++ leaves argument evaluation order unspecified), so the first non-finite value is the one
// reported, as in C#.
WorldFloat3 F3(const WorldSpan& s, int o)
{
  float a = s.F32(o);
  float b = s.F32(o + 4);
  float c = s.F32(o + 8);
  return WorldFloat3(a, b, c);
}

PlacementSource Source(int tileX, int tileY, const std::string& container, int i)
{
  PlacementSource src;
  src.TileX = tileX;
  src.TileY = tileY;
  src.Container = container;
  src.RecordIndex = i;
  return src;
}
} // namespace

bool SameBits(const WorldFloat3& a, const WorldFloat3& b)
{
  return Bits(a.x) == Bits(b.x) && Bits(a.y) == Bits(b.y) && Bits(a.z) == Bits(b.z);
}

std::vector<DoodadPlacement> PlacementRecords::ReadDoodads(const WorldSpan& s, int tileX, int tileY, const std::string& container)
{
  int n = s.RecordCount(DoodadRecordSize, container);
  std::vector<DoodadPlacement> r(static_cast<size_t>(n));
  for (int i = 0; i < n; i++)
  {
    int o = DoodadRecordSize * i;
    DoodadPlacement& p = r[static_cast<size_t>(i)];
    p.NameId = s.U32(o);
    p.UniqueId = s.U32(o + 4);
    p.Position = F3(s, o + 8);
    p.Rotation = F3(s, o + 20);
    p.ScaleRaw = s.U16(o + 32);
    p.Flags = s.U16(o + 34);
    p.Source = Source(tileX, tileY, container, i);
  }
  return r;
}

std::vector<WmoPlacement> PlacementRecords::ReadWmos(const WorldSpan& s, int tileX, int tileY, const std::string& container)
{
  int n = s.RecordCount(WmoRecordSize, container);
  std::vector<WmoPlacement> r(static_cast<size_t>(n));
  for (int i = 0; i < n; i++)
  {
    int o = WmoRecordSize * i;
    WmoPlacement& p = r[static_cast<size_t>(i)];
    p.NameId = s.U32(o);
    p.UniqueId = s.U32(o + 4);
    p.Position = F3(s, o + 8);
    p.Rotation = F3(s, o + 20);
    p.ExtentsMin = F3(s, o + 32);
    p.ExtentsMax = F3(s, o + 44);
    p.Flags = s.U16(o + 56);
    p.Word58 = s.U16(o + 58);
    p.Word60 = s.U16(o + 60);
    p.ScaleRaw = s.U16(o + 62);
    p.Source = Source(tileX, tileY, container, i);
  }
  return r;
}

std::vector<DoodadWorldBox> PlacementRecords::ReadDoodadBoxes(const WorldSpan& s)
{
  int n = s.RecordCount(DoodadBoxRecordSize, "MLDX");
  std::vector<DoodadWorldBox> r(static_cast<size_t>(n));
  for (int i = 0; i < n; i++)
  {
    int o = DoodadBoxRecordSize * i;
    DoodadWorldBox& b = r[static_cast<size_t>(i)];
    b.Min = F3(s, o);
    b.Max = F3(s, o + 12);
    b.Value6Raw = s.F32Raw(o + 24);
  }
  return r;
}
} // namespace world
} // namespace wmv
