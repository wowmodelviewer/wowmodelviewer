/*
 * AdtParser.cpp  (port of Wow/World/AdtParser.cs; rules and failure policy in AdtParser.h)
 */

#include "AdtParser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <utility>

#include "LiquidFormat.h"
#include "WdtParser.h"
#include "WorldText.h"

namespace wmv
{
namespace world
{
using text::FloatR;
using text::Hex;
using text::Num;

namespace
{
constexpr int ChunksPerTile = AdtParser::ChunksPerTile;
constexpr int McnkHeaderSize = AdtParser::McnkHeaderSize;
constexpr int LiquidHeaderSize = AdtParser::LiquidHeaderSize;
constexpr int LiquidInstanceSize = AdtParser::LiquidInstanceSize;
constexpr int LiquidAttributesSize = AdtParser::LiquidAttributesSize;
constexpr float LiquidMaxAbsHeight = AdtParser::LiquidMaxAbsHeight;

const char* const RepeatedFirstUsed = "appears more than once (never in the audited files); the first copy is used";

// ===================================================================================================== helpers

std::vector<WorldChunk> WalkFile(const std::vector<uint8_t>& data, const std::string& what)
{
  return WorldChunkWalker::Walk(data, 0, static_cast<int>(data.size()), what, "file");
}

uint32_t ReadVersion(const std::vector<uint8_t>& data, const std::vector<WorldChunk>& top, const std::string& what,
                     std::vector<WorldIssue>& issues)
{
  if (top.empty() || top[0].Tag != "MVER")
    throw WorldParseException(what, "MVER", 0, "the file does not start with an MVER chunk");
  WorldSpan s(data, top[0], what);
  if (s.Size() < 4)
    throw WorldParseException(what, "MVER", top[0].DataOffset, "shorter than 4 bytes");
  uint32_t v = s.U32(0);
  if (v != 18)
    issues.emplace_back(WorldIssueKind::Unexpected, what, "MVER", top[0].DataOffset,
                        "version " + Num(v) + " (every audited ADT part has 18)");
  return v;
}

std::vector<WorldChunk> RequireMcnk(const std::vector<WorldChunk>& top, const std::string& what)
{
  std::vector<WorldChunk> list;
  for (const WorldChunk& c : top)
    if (c.Tag == "MCNK")
      list.push_back(c);
  if (static_cast<int>(list.size()) != ChunksPerTile)
    throw WorldParseException(what, "MCNK", !list.empty() ? list[0].HeaderOffset : 0,
                              Num(static_cast<long long>(list.size())) + " MCNK chunk(s); a tile has 256");
  return list;
}

WorldUnsupportedChunk Unsupported(const WorldChunk& c, const std::string& reason)
{
  WorldUnsupportedChunk u;
  u.Tag = WorldChunkWalker::Printable(c.Tag);
  u.DataOffset = c.DataOffset;
  u.Size = c.Size;
  u.Reason = reason;
  return u;
}

bool Finite(float f) { return std::isfinite(f); }

// ===================================================================================================== root

AdtRootChunk ParseRootChunk(const std::vector<uint8_t>& data, const WorldChunk& c, int index, const std::string& what,
                            std::vector<WorldIssue>& issues)
{
  AdtRootChunk ch;
  ch.Index = index;
  ch.Status = WorldDataStatus::Decoded;
  const std::string label = "MCNK " + Num(index);
  if (c.Size < McnkHeaderSize)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label, c.DataOffset,
                        Num(c.Size) + " byte(s), smaller than its 128-byte header");
    ch.Status = WorldDataStatus::Malformed;
    return ch;
  }
  WorldSpan h(data, c.DataOffset, McnkHeaderSize, what, label);
  ch.RawHeader = h.Bytes(0, McnkHeaderSize);
  ch.Flags = h.U32(0);
  ch.IndexX = h.U32(4);
  ch.IndexY = h.U32(8);
  ch.AreaId = h.U32(52);
  try
  {
    float x = h.F32(104);
    float y = h.F32(108);
    float z = h.F32(112);
    ch.StoredPosition = WorldFloat3(x, y, z);
  }
  catch (const WorldParseException& e)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label, e.Offset, "stored position: " + e.Reason);
    ch.Status = WorldDataStatus::Malformed;
  }
  if (ch.IndexX != static_cast<uint32_t>(index % 16) || ch.IndexY != static_cast<uint32_t>(index / 16))
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label, static_cast<long long>(c.DataOffset) + 4,
                        "header index (" + Num(ch.IndexX) + ", " + Num(ch.IndexY) + ") is not the chunk's position (" +
                          Num(index % 16) + ", " + Num(index / 16) + "); every audited chunk has them equal");
    ch.Status = WorldDataStatus::Malformed;
  }
  if ((ch.Flags & 0x10000) != 0)
  {
    ch.Holes.Kind = HoleMaskKind::HighResolution;
    ch.Holes.HighResolutionBits = h.U64(20);
    ch.Holes.LowResolutionRaw = h.U16(60);
    if (ch.Holes.LowResolutionRaw != 0)
      issues.emplace_back(WorldIssueKind::Unexpected, what, label, static_cast<long long>(c.DataOffset) + 60,
                          "the 16-bit hole field is non-zero although flag 0x10000 is set (always 0 in the audit)");
  }
  else
  {
    ch.Holes.Kind = HoleMaskKind::LowResolutionUnsupported;
    ch.Holes.LowResolutionRaw = h.U16(60);
    issues.emplace_back(WorldIssueKind::Unexpected, what, label, c.DataOffset,
                        "flag 0x10000 clear (set on every audited chunk): bytes 20..27 are not read as holes");
    if (ch.Holes.LowResolutionRaw != 0)
      issues.emplace_back(WorldIssueKind::Unsupported, what, label, static_cast<long long>(c.DataOffset) + 60,
                          "low-resolution hole mask: its bit layout is not established; not decoded");
  }

  std::vector<WorldChunk> sub;
  try
  {
    sub = WorldChunkWalker::Walk(data, c.DataOffset + McnkHeaderSize, c.DataOffset + c.Size, what, label);
  }
  catch (const WorldParseException& e)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, e.Chunk, e.Offset, e.Reason);
    ch.Status = WorldDataStatus::Malformed;
    return ch;
  }

  bool haveHeights = false;
  for (const WorldChunk& s : sub)
  {
    const std::string slabel = label + " " + WorldChunkWalker::Printable(s.Tag);
    if (s.Tag == "MCVT")
    {
      if (haveHeights || s.Size != AdtParser::HeightsBytes)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset,
                            haveHeights ? std::string("appears more than once")
                                        : Num(s.Size) + " byte(s), expected 580 (145 floats)");
        ch.Status = WorldDataStatus::Malformed;
        continue;
      }
      try
      {
        WorldSpan hs(data, s, what);
        std::vector<float> heights(TerrainVertexLayout::VertexCount);
        for (int k = 0; k < TerrainVertexLayout::VertexCount; k++)
          heights[static_cast<size_t>(k)] = hs.F32(4 * k);
        ch.Heights = std::move(heights);
        haveHeights = true;
      }
      catch (const WorldParseException& e)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, e.Offset, e.Reason);
        ch.Status = WorldDataStatus::Malformed;
      }
    }
    else if (s.Tag == "MCNR")
    {
      if (!ch.NormalsRaw.empty() || s.Size < AdtParser::NormalTripletBytes)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset,
                            !ch.NormalsRaw.empty() ? std::string("appears more than once")
                                                   : Num(s.Size) + " byte(s), fewer than 435 (145 triplets)");
        ch.Status = WorldDataStatus::Malformed;
        continue;
      }
      WorldSpan ns(data, s, what);
      std::vector<uint8_t> raw = ns.Bytes(0, AdtParser::NormalTripletBytes);
      ch.NormalsRaw.assign(raw.size(), 0);
      std::memcpy(ch.NormalsRaw.data(), raw.data(), raw.size());
      ch.NormalsTrailingRaw = ns.Bytes(AdtParser::NormalTripletBytes, s.Size - AdtParser::NormalTripletBytes);
    }
    else if (s.Tag == "MCCV")
    {
      if (ch.HasVertexColors() || s.Size != AdtParser::VertexColorBytes)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset,
                            ch.HasVertexColors() ? std::string("appears more than once")
                                                 : Num(s.Size) + " byte(s), expected 580 (145 x 4)");
        ch.Status = WorldDataStatus::Malformed;
        continue;
      }
      ch.VertexColorsRaw = WorldSpan(data, s, what).Bytes(0, s.Size);
    }
    else if (s.Tag == "MCSE")
    {
      if (ch.McseSize >= 0)
      {
        ch.Unsupported.push_back(Unsupported(s, "repeated MCSE"));
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset, RepeatedFirstUsed);
        continue;
      }
      ch.McseSize = s.Size;
      if (s.Size != 0)
      {
        ch.Unsupported.push_back(Unsupported(s, "non-empty MCSE: layout not established"));
        issues.emplace_back(WorldIssueKind::Unexpected, what, slabel, s.DataOffset,
                            Num(s.Size) + " byte(s) of MCSE (0 in every audited chunk); kept undecoded");
      }
    }
    else if (s.Tag == "MCBB" || s.Tag == "MPTX")
      ch.Unsupported.push_back(Unsupported(s, "layout not established (audit U18)"));
    else
    {
      ch.Unsupported.push_back(Unsupported(s, "not seen in audited root MCNKs"));
      issues.emplace_back(WorldIssueKind::Unsupported, what, slabel, s.DataOffset,
                          "sub-chunk not seen in audited root MCNKs; kept undecoded");
    }
  }
  if (!haveHeights && ch.Status == WorldDataStatus::Decoded)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label, c.DataOffset, "no MCVT (every audited root MCNK has one)");
    ch.Status = WorldDataStatus::Malformed;
  }
  if (ch.NormalsRaw.empty())
    issues.emplace_back(WorldIssueKind::Unexpected, what, label, c.DataOffset, "no usable MCNR (every audited root MCNK has one)");
  if (((ch.Flags & 0x40) != 0) != ch.HasVertexColors())
    issues.emplace_back(WorldIssueKind::Unexpected, what, label, c.DataOffset,
                        std::string("flag 0x40 is ") + ((ch.Flags & 0x40) != 0 ? "set" : "clear") + " but MCCV is " +
                          (ch.HasVertexColors() ? "present" : "absent") + " (they agree in every audited chunk)");
  return ch;
}

// ---- MH2O data region (docs/world-viewer/09-liquids.md 1.2, PROVEN_BYTES unless noted). In chunk order, each
// chunk's 16-byte attributes, then per layer its exists bitmap and its vertex data, packed with no gaps and no
// alignment; every structure ends where the next one starts, the last at the end of MH2O. So a vertex block's size
// is the gap to the next structure, and its bytes per vertex (1, 5, 8 or 9 -- all four distinct) decide its
// format. Every structure is checked against the header table, the instance tables, its neighbours, the packing's
// chunk order and its instance's own format (FindMisplaced: an offset out of place is its owner's, never a
// neighbour's re-sizing) and the end of the chunk before it is read; a bad one is reported for its chunk or
// instance and costs only that structure (attributes) or that instance's geometry, never the terrain.

using Range = std::pair<long long, long long>; // [start, end)

/// Where the MH2O structures lie: the sorted starts (the end of the chunk included), the instance tables, the
/// header table and the chunk size; the starts out of place (FindMisplaced) and the vertex blocks whose end their
/// own rule gives.
struct LiquidLayout
{
  std::vector<long long> Starts;
  std::vector<Range> Tables; // sorted, non-overlapping [start, end)
  int HeaderTable = 0;
  int Size = 0;
  std::map<long long, std::string> misplaced;
  std::map<long long, long long> ProvenEnds;

  /// The start of a structure out of place: why, and off the layout (it ends no other structure).
  void Misplace(long long start, const std::string& why)
  {
    if (misplaced.count(start) != 0)
      return;
    misplaced[start] = why;
    auto it = std::lower_bound(Starts.begin(), Starts.end(), start);
    if (it != Starts.end() && *it == start)
      Starts.erase(it);
  }

  bool IsMisplaced(long long start) const { return misplaced.count(start) != 0; }

  /// Why the start is out of place, or empty.
  std::string MisplacedWhy(long long start) const
  {
    auto it = misplaced.find(start);
    return it != misplaced.end() ? it->second : std::string();
  }

  /// Where the structure at `start` ends: the next start, or sooner the end its own rule gives.
  long long End(long long start) const
  {
    long long end = Next(start);
    auto it = ProvenEnds.find(start);
    return it != ProvenEnds.end() && it->second < end ? it->second : end;
  }

  /// The first start after `offset` (at most the end of the chunk).
  long long Next(long long offset) const
  {
    auto it = std::upper_bound(Starts.begin(), Starts.end(), offset);
    return it != Starts.end() ? *it : Size;
  }

  bool Shared(long long offset) const
  {
    auto it = std::lower_bound(Starts.begin(), Starts.end(), offset);
    if (it == Starts.end() || *it != offset)
      return false;
    return it + 1 != Starts.end() && *(it + 1) == offset;
  }

  /// Why a structure of `bytes` at `offset` cannot be read, or empty: it must lie after the header table, outside
  /// every instance table, inside the chunk, and end before the next structure starts.
  std::string Problem(long long offset, long long bytes, const std::string& name) const
  {
    if (offset < HeaderTable)
      return name + " at +0x" + Hex(offset) + " is inside the 3072-byte header table";
    // the last table starting at or before offset
    auto it = std::upper_bound(Tables.begin(), Tables.end(), offset,
                               [](long long o, const Range& r) { return o < r.first; });
    if (it != Tables.begin() && offset < (it - 1)->second)
      return name + " at +0x" + Hex(offset) + " is inside the instance table [+0x" + Hex((it - 1)->first) + ", +0x" +
             Hex((it - 1)->second) + ")";
    if (offset + bytes > Size)
      return name + " (" + Num(bytes) + " byte(s) at +0x" + Hex(offset) + ") runs past the end of the " + Num(Size) +
             "-byte chunk";
    long long next = Next(offset);
    if (offset + bytes > next)
      return name + " (" + Num(bytes) + " byte(s) at +0x" + Hex(offset) + ") overlaps the structure at +0x" + Hex(next);
    return std::string();
  }
};

/// One named structure of the data region, in the order the packing puts it (FindMisplaced).
struct LiquidStructure
{
  long long Start = 0;
  std::string Name;                    // "attribute block", "exists bitmap", VertexData
  const LiquidInstance* Instance = nullptr; // null: the chunk's attribute block
};

// The packing (09-liquids.md 1.2, PROVEN_BYTES in every client file) decides whose a conflicting offset is, so a bad
// offset never re-sizes a well-formed neighbour (a structure's size is the gap to the next start):
//   order    the starts increase in chunk order -- per chunk its attribute block, then per layer its bitmap and its
//            vertex data. A start that does not is out of place: the structures read so far keep their starts,
//            unless the last one kept is the one out of place (it lies past this start and the next structure's,
//            both after the one kept before it: it jumped ahead) and goes instead.
//   extent   a vertex block whose instance's own rule gives its format (the second word 0, 1 or 2, or the ocean
//            type: LiquidFormat::Decide without a catalogue; 6,334,582 of 6,334,582 instances agree with their size)
//            is n times that format's bytes; a start inside that extent is out of place.
// An out-of-place start is its owner's problem (that chunk's attributes, or that instance's geometry) and leaves the
// layout; a vertex block with an own extent whose successor in that order is out of place keeps that extent (the
// successor's real bytes follow it). What remains is measured as before.
const char* const VertexData = "vertex data";

std::string OwnRule(const LiquidInstance& li, const LiquidFormatDecision& own)
{
  return own.Source == LiquidFormatSource::Word ? "second word " + Num(li.ObjectOrFormat)
                                                : "liquid type " + Num(li.LiquidType) + " (Ocean)";
}

void FindMisplaced(const AdtLiquids& liq, LiquidLayout& layout)
{
  std::vector<LiquidStructure> order;
  for (int n = 0; n < ChunksPerTile; n++)
  {
    const LiquidChunkHeader& hd = liq.Headers[static_cast<size_t>(n)];
    if (hd.LayerCount == 0)
      continue;
    if (hd.AttributesOffset != 0)
      order.push_back(LiquidStructure{hd.AttributesOffset, "attribute block", nullptr});
    for (const LiquidInstance& li : liq.Instances[static_cast<size_t>(n)])
    {
      if (li.ExistsOffset != 0)
        order.push_back(LiquidStructure{li.ExistsOffset, "exists bitmap", &li});
      if (li.VertexOffset != 0)
        order.push_back(LiquidStructure{li.VertexOffset, VertexData, &li});
    }
  }
  const int count = static_cast<int>(order.size());

  // Order. (A start shared by two structures is left to the shared-offset rule: both are malformed.)
  std::vector<int> kept;
  std::vector<int> outOfOrder;
  for (int k = 0; k < count; k++)
  {
    long long start = order[static_cast<size_t>(k)].Start;
    if (layout.Shared(start))
      continue;
    int m = static_cast<int>(kept.size());
    long long last = m > 0 ? order[static_cast<size_t>(kept[static_cast<size_t>(m - 1)])].Start : -1;
    if (start > last)
    {
      kept.push_back(k);
      continue;
    }
    long long before = m > 1 ? order[static_cast<size_t>(kept[static_cast<size_t>(m - 2)])].Start : -1;
    long long next = k + 1 < count ? order[static_cast<size_t>(k + 1)].Start : layout.Size;
    if (start > before && next > start && next < last)
    {
      outOfOrder.push_back(kept[static_cast<size_t>(m - 1)]);
      kept[static_cast<size_t>(m - 1)] = k;
    }
    else
      outOfOrder.push_back(k);
  }
  for (int k : outOfOrder)
  {
    const LiquidStructure& o = order[static_cast<size_t>(k)];
    layout.Misplace(o.Start, o.Name + " at +0x" + Hex(o.Start) +
                               " is out of the chunk order the data region is packed in "
                               "(it does not lie between the structures before and after it)");
  }

  // Extent.
  std::map<long long, int> byStart;
  for (int k : kept)
    if (!layout.IsMisplaced(order[static_cast<size_t>(k)].Start))
      byStart[order[static_cast<size_t>(k)].Start] = k;
  for (int k : kept)
  {
    const LiquidStructure& v = order[static_cast<size_t>(k)];
    if (v.Name != VertexData || layout.IsMisplaced(v.Start))
      continue;
    const LiquidInstance& li = *v.Instance;
    if (li.Width < 1 || li.Height < 1 || li.X + li.Width > 8 || li.Y + li.Height > 8 || !layout.Problem(v.Start, 1, v.Name).empty())
      continue;
    LiquidFormatDecision own = LiquidFormat::Decide(li, nullptr);
    if (own.Source != LiquidFormatSource::Word && own.Source != LiquidFormatSource::OceanType)
      continue;
    long long end = v.Start + static_cast<long long>(li.VertexCount()) * LiquidFormat::BytesPerVertex(own.Format);
    for (long long next = layout.Next(v.Start); next < end; next = layout.Next(v.Start))
    {
      auto found = byStart.find(next);
      if (found == byStart.end())
        break; // a table or the chunk's end: the usual checks report it
      int inside = found->second;
      byStart.erase(found);
      layout.Misplace(next, order[static_cast<size_t>(inside)].Name + " at +0x" + Hex(next) + " lies inside the " +
                              Num(end - v.Start) + " byte(s) of the vertex data at +0x" + Hex(v.Start) + " of chunk " +
                              Num(li.Chunk) + " layer " + Num(li.Layer) + " (format " + Num(own.Format) + " by its own " +
                              OwnRule(li, own) + ")");
    }
    if (k + 1 < count && layout.IsMisplaced(order[static_cast<size_t>(k + 1)].Start))
      layout.ProvenEnds[v.Start] = end;
  }
}

/// Whether an existing quad of the instance touches vertex (i, j).
bool VertexUsed(const LiquidInstance& li, int i, int j)
{
  return li.QuadExists(i - 1, j - 1) || li.QuadExists(i - 1, j) || li.QuadExists(i, j - 1) || li.QuadExists(i, j);
}

/// Decodes one instance's bitmap and vertex data; returns why it is malformed, or empty.
std::string DecodeLiquidInstance(const WorldSpan& s, LiquidInstance& li, const LiquidLayout& layout, long long record,
                                 const std::string& what, std::vector<WorldIssue>& issues)
{
  int w = li.Width, h = li.Height;
  if (w < 1 || h < 1 || li.X + w > 8 || li.Y + h > 8)
    return "cells x " + Num(li.X) + " y " + Num(li.Y) + " width " + Num(w) + " height " + Num(h) +
           " are not inside the chunk's 8 x 8 cells";
  if (!Finite(li.MinHeight) || !Finite(li.MaxHeight) || std::fabs(li.MinHeight) > LiquidMaxAbsHeight ||
      std::fabs(li.MaxHeight) > LiquidMaxAbsHeight)
    return "min/max height " + FloatR(li.MinHeight) + " / " + FloatR(li.MaxHeight) + " is not a usable height";
  const std::string label = "MH2O chunk " + Num(li.Chunk) + " layer " + Num(li.Layer);
  if (li.MinHeight > li.MaxHeight)
    issues.emplace_back(WorldIssueKind::Unexpected, what, label, record,
                        "min height " + FloatR(li.MinHeight) + " is above max height " + FloatR(li.MaxHeight) +
                          " (min <= max in every client file)");

  // Exists bitmap: ceil(w h / 8) bytes, quad (r, c) = bit r w + c, least significant first.
  int quads = w * h;
  li.Exists.assign(static_cast<size_t>(quads), false);
  if (li.ExistsOffset == 0)
  {
    for (int q = 0; q < quads; q++)
      li.Exists[static_cast<size_t>(q)] = true;
  }
  else
  {
    int bytes = (quads + 7) / 8;
    std::string why = layout.Problem(li.ExistsOffset, bytes, "exists bitmap");
    if (!why.empty())
      return why;
    if (layout.Shared(li.ExistsOffset))
      return "exists bitmap at +0x" + Hex(li.ExistsOffset) + " shares its offset with another structure";
    if (!(why = layout.MisplacedWhy(li.ExistsOffset)).empty())
      return why;
    li.HasExistsBitmap = true;
    bool any = false;
    for (int q = 0; q < bytes * 8; q++)
    {
      bool bit = ((s.U8(static_cast<int>(li.ExistsOffset) + (q >> 3)) >> (q & 7)) & 1) != 0;
      if (q < quads)
      {
        li.Exists[static_cast<size_t>(q)] = bit;
        any |= bit;
      }
      else if (bit)
        li.ExistsSpareBits = true;
    }
    if (!any)
      issues.emplace_back(WorldIssueKind::Unexpected, what, label, s.AbsoluteOffset(static_cast<int>(li.ExistsOffset)),
                          "the exists bitmap marks no quad (never in a client file)");
  }

  if (li.VertexOffset == 0)
    return std::string();
  int n = li.VertexCount();
  long long start = li.VertexOffset;
  std::string where = layout.Problem(start, 1, "vertex data");
  if (!where.empty())
    return where;
  if (layout.Shared(start))
    return "vertex data at +0x" + Hex(start) + " shares its offset with another structure: its size cannot be measured";
  if (!(where = layout.MisplacedWhy(start)).empty())
    return where;
  long long gap = layout.End(start) - start;
  li.VertexBlockBytes = static_cast<int>(gap);
  int format = gap % n == 0 ? LiquidFormat::FormatOfBytesPerVertex(static_cast<int>(gap / n)) : -1;
  if (format < 0)
    return "vertex data of " + Num(gap) + " byte(s) at +0x" + Hex(start) + " is not 1, 5, 8 or 9 bytes for each of its " +
           Num(n) + " vertices";
  // The instance's own rule (its word, or the ocean type) gives the format too: a block of another size is
  // malformed itself (a start inside its extent was taken out above, FindMisplaced), never decoded by the size.
  LiquidFormatDecision own = LiquidFormat::Decide(li, nullptr);
  if ((own.Source == LiquidFormatSource::Word || own.Source == LiquidFormatSource::OceanType) && own.Format != format)
    return "vertex data of " + Num(gap) + " byte(s) at +0x" + Hex(start) + " is format " + Num(format) +
           " by its size, but format " + Num(own.Format) + " by its own " + OwnRule(li, own);
  li.FormatBySize = format;

  int o = static_cast<int>(start);
  if (LiquidFormat::HasHeights(format))
  {
    li.Heights.resize(static_cast<size_t>(n));
    for (int k = 0; k < n; k++)
      li.Heights[static_cast<size_t>(k)] = s.F32Raw(o + 4 * k);
    o += 4 * n;
  }
  if (LiquidFormat::HasUv(format))
  {
    li.U.resize(static_cast<size_t>(n));
    li.V.resize(static_cast<size_t>(n));
    for (int k = 0; k < n; k++)
    {
      li.U[static_cast<size_t>(k)] = s.U16(o + 4 * k);
      li.V[static_cast<size_t>(k)] = s.U16(o + 4 * k + 2);
    }
    o += 4 * n;
  }
  if (LiquidFormat::HasDepth(format))
    li.Depths = s.Bytes(o, n);

  // Heights matter only where an existing quad uses them (the others are leftovers): there they must be usable,
  // and they lie inside [min, max] in every client file.
  if (!li.Heights.empty())
  {
    int outside = 0;
    for (int i = 0; i <= h; i++)
      for (int j = 0; j <= w; j++)
      {
        if (!VertexUsed(li, i, j))
          continue;
        float z = li.Heights[static_cast<size_t>(i * (w + 1) + j)];
        if (!Finite(z) || std::fabs(z) > LiquidMaxAbsHeight)
          return "vertex (" + Num(i) + ", " + Num(j) + ") has height " + FloatR(z);
        if (z < li.MinHeight || z > li.MaxHeight)
          outside++;
      }
    if (outside > 0)
      issues.emplace_back(WorldIssueKind::Unexpected, what, label, s.AbsoluteOffset(static_cast<int>(start)),
                          Num(outside) + " used vertex height(s) outside the instance's [min, max] (inside in every client file)");
  }
  return std::string();
}

void DecodeLiquidData(const WorldSpan& s, AdtLiquids& liq, const std::vector<Range>& tables, const std::string& what,
                      std::vector<WorldIssue>& issues)
{
  const int headerTable = ChunksPerTile * LiquidHeaderSize;
  // Every start the header table and the instances name, plus the end of the chunk: a structure ends at the next
  // larger start. A start named twice is shared, which no client file does.
  LiquidLayout layout;
  layout.Starts.push_back(s.Size());
  for (int n = 0; n < ChunksPerTile; n++)
  {
    const LiquidChunkHeader& hd = liq.Headers[static_cast<size_t>(n)];
    if (hd.LayerCount == 0)
      continue;
    layout.Starts.push_back(hd.InstancesOffset);
    if (hd.AttributesOffset != 0)
      layout.Starts.push_back(hd.AttributesOffset);
    for (const LiquidInstance& li : liq.Instances[static_cast<size_t>(n)])
    {
      if (li.ExistsOffset != 0)
        layout.Starts.push_back(li.ExistsOffset);
      if (li.VertexOffset != 0)
        layout.Starts.push_back(li.VertexOffset);
    }
  }
  std::sort(layout.Starts.begin(), layout.Starts.end());
  layout.Tables = tables;
  layout.HeaderTable = headerTable;
  layout.Size = s.Size();
  FindMisplaced(liq, layout);

  for (int n = 0; n < ChunksPerTile; n++)
  {
    const LiquidChunkHeader hd = liq.Headers[static_cast<size_t>(n)];
    if (hd.LayerCount == 0)
      continue;
    const std::string label = "MH2O chunk " + Num(n);
    if (hd.AttributesOffset != 0)
    {
      std::string why = layout.Problem(hd.AttributesOffset, LiquidAttributesSize, "attribute block");
      if (why.empty())
        why = layout.MisplacedWhy(hd.AttributesOffset);
      if (!why.empty())
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, label, s.AbsoluteOffset(LiquidHeaderSize * n + 8),
                            why + "; the chunk's attributes are not used");
        liq.MalformedAttributes++;
      }
      else
      {
        int o = static_cast<int>(hd.AttributesOffset);
        LiquidAttributes a;
        a.Present = true;
        a.Fishable = s.U64(o);
        a.Deep = s.U64(o + 8);
        liq.Attributes[static_cast<size_t>(n)] = a;
        if ((a.Deep & ~a.Fishable) != 0)
          issues.emplace_back(WorldIssueKind::Unexpected, what, label, s.AbsoluteOffset(o + 8),
                              "the second attribute mask is not a subset of the first (it is in every client file)");
      }
    }
    for (LiquidInstance& li : liq.Instances[static_cast<size_t>(n)])
    {
      long long record = s.AbsoluteOffset(static_cast<int>(hd.InstancesOffset) + LiquidInstanceSize * li.Layer);
      std::string why = DecodeLiquidInstance(s, li, layout, record, what, issues);
      if (why.empty())
        continue;
      li.Status = WorldDataStatus::Malformed;
      li.Problem = why;
      li.FormatBySize = -1;
      li.Exists.clear();
      li.Heights.clear();
      li.Depths.clear();
      li.U.clear();
      li.V.clear();
      liq.MalformedInstances++;
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " layer " + Num(li.Layer), record,
                          why + "; the instance has no geometry");
    }
  }
}

AdtLiquids ParseLiquids(const std::vector<uint8_t>& data, const WorldChunk& c, const std::string& what,
                        std::vector<WorldIssue>& issues)
{
  AdtLiquids liq;
  liq.DataOffset = c.DataOffset;
  liq.Size = c.Size;
  liq.Status = WorldDataStatus::PartiallyDecoded;
  WorldSpan s(data, c, what);
  if (s.Size() < ChunksPerTile * LiquidHeaderSize)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, "MH2O", c.DataOffset,
                        Num(s.Size()) + " byte(s), fewer than the 256 x 12-byte header table");
    liq.Status = WorldDataStatus::Malformed;
    return liq;
  }
  liq.Headers.resize(ChunksPerTile);
  liq.Instances.resize(ChunksPerTile);
  // Validate every range before allocating anything: in all audited tiles the instance tables lie after the
  // 3,072-byte header table and never overlap, so the instances can never outnumber the bytes that hold them (an
  // overlapping table could otherwise multiply them).
  const int table = ChunksPerTile * LiquidHeaderSize;
  std::vector<Range> ranges;
  for (int n = 0; n < ChunksPerTile; n++)
  {
    LiquidChunkHeader hd;
    hd.InstancesOffset = s.U32(LiquidHeaderSize * n);
    hd.LayerCount = s.U32(LiquidHeaderSize * n + 4);
    hd.AttributesOffset = s.U32(LiquidHeaderSize * n + 8);
    liq.Headers[static_cast<size_t>(n)] = hd;
    if (hd.LayerCount == 0)
      continue;
    long long end = static_cast<long long>(hd.InstancesOffset) + static_cast<long long>(hd.LayerCount) * LiquidInstanceSize;
    if (hd.InstancesOffset < static_cast<uint32_t>(table) || end > s.Size())
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, "MH2O", s.AbsoluteOffset(LiquidHeaderSize * n),
                          "chunk " + Num(n) + ": " + Num(hd.LayerCount) + " instance(s) at +0x" + Hex(hd.InstancesOffset) +
                            " are not inside the " + Num(s.Size()) + "-byte chunk after its header table");
      liq.Status = WorldDataStatus::Malformed;
    }
    else
      ranges.emplace_back(static_cast<long long>(hd.InstancesOffset), end);
  }
  std::stable_sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.first < b.first; });
  for (size_t i = 1; i < ranges.size(); i++)
    if (ranges[i].first < ranges[i - 1].second)
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, "MH2O", s.AbsoluteOffset(static_cast<int>(ranges[i].first)),
                          "instance tables overlap (+0x" + Hex(ranges[i].first) + " starts inside [+0x" +
                            Hex(ranges[i - 1].first) + ", +0x" + Hex(ranges[i - 1].second) +
                            ")); no audited tile has overlapping tables");
      liq.Status = WorldDataStatus::Malformed;
      break;
    }
  liq.Attributes.resize(ChunksPerTile);
  for (int n = 0; n < ChunksPerTile; n++)
  {
    const LiquidChunkHeader& hd = liq.Headers[static_cast<size_t>(n)];
    if (liq.Status == WorldDataStatus::Malformed || hd.LayerCount == 0)
      continue; // the chunk's instance list stays empty
    std::vector<LiquidInstance>& inst = liq.Instances[static_cast<size_t>(n)];
    inst.resize(hd.LayerCount);
    for (int k = 0; k < static_cast<int>(inst.size()); k++)
    {
      int o = static_cast<int>(hd.InstancesOffset) + LiquidInstanceSize * k;
      LiquidInstance& li = inst[static_cast<size_t>(k)];
      li.LiquidType = s.U16(o);
      li.ObjectOrFormat = s.U16(o + 2);
      li.MinHeight = s.F32Raw(o + 4);
      li.MaxHeight = s.F32Raw(o + 8);
      li.X = s.U8(o + 12);
      li.Y = s.U8(o + 13);
      li.Width = s.U8(o + 14);
      li.Height = s.U8(o + 15);
      li.ExistsOffset = s.U32(o + 16);
      li.VertexOffset = s.U32(o + 20);
      li.Chunk = n;
      li.Layer = k;
      li.Status = WorldDataStatus::Decoded;
    }
  }
  if (liq.Status != WorldDataStatus::Malformed)
    DecodeLiquidData(s, liq, ranges, what, issues);
  return liq;
}

// ===================================================================================================== tex0

/// An optional u32 FileDataID table (MDID, MHID): the first copy; a size that is not a whole number of u32 is
/// Malformed and gives an empty table (layers then cannot resolve).
std::vector<uint32_t> OptionalU32s(const std::vector<uint8_t>& data, const std::vector<WorldChunk>& top, const std::string& tag,
                                   const std::string& what, std::vector<WorldIssue>& issues)
{
  WorldChunk c;
  if (!WorldChunkWalker::FindFirst(top, tag, what, issues, c))
    return {};
  if (c.Size % 4 != 0)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, tag, c.DataOffset,
                        Num(c.Size) + " byte(s) is not a whole number of u32 FileDataIDs; the table is not used");
    return {};
  }
  return WorldSpan(data, c, what).U32Array();
}

AdtTexChunk ParseTexChunk(const std::vector<uint8_t>& data, const WorldChunk& c, int index, int textureCount,
                          const std::string& what, std::vector<WorldIssue>& issues)
{
  AdtTexChunk ch;
  ch.Index = index;
  ch.Status = WorldDataStatus::Decoded;
  const std::string label = "tex0 MCNK " + Num(index);
  std::vector<WorldChunk> sub;
  try
  {
    sub = WorldChunkWalker::Walk(data, c.DataOffset, c.DataOffset + c.Size, what, label);
  }
  catch (const WorldParseException& e)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, e.Chunk, e.Offset, e.Reason);
    ch.Status = WorldDataStatus::Malformed;
    return ch;
  }
  WorldChunk mcal, mcly;
  bool haveMcal = false, haveMcly = false;
  for (const WorldChunk& s : sub)
  {
    const std::string slabel = label + " " + WorldChunkWalker::Printable(s.Tag);
    if (s.Tag == "MCLY")
    {
      if (haveMcly || s.Size % 16 != 0 || s.Size / 16 > AdtParser::MaxLayers)
      {
        std::string why = haveMcly ? std::string("appears more than once")
                          : s.Size % 16 != 0
                            ? Num(s.Size) + " byte(s) is not a whole number of 16-byte layers"
                            : Num(s.Size / 16) + " layers (at most 8 in the audited chunks; more than " +
                                Num(AdtParser::MaxLayers) + " are rejected)";
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset, why);
        ch.Status = WorldDataStatus::Malformed;
        continue;
      }
      haveMcly = true;
      mcly = s;
      WorldSpan ls(data, s, what);
      std::vector<TerrainLayer> layers(static_cast<size_t>(s.Size / 16));
      for (int k = 0; k < static_cast<int>(layers.size()); k++)
      {
        TerrainLayer& l = layers[static_cast<size_t>(k)];
        l.TextureIndex = ls.U32(16 * k);
        l.Flags = ls.U32(16 * k + 4);
        l.AlphaOffset = ls.U32(16 * k + 8);
        l.Word12Raw = ls.U32(16 * k + 12);
      }
      ch.Layers = std::move(layers);
    }
    else if (s.Tag == "MCAL")
    {
      if (haveMcal)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset, "appears more than once");
        ch.Status = WorldDataStatus::Malformed;
        continue;
      }
      mcal = s;
      haveMcal = true;
      ch.AlphaSize = s.Size;
    }
    else if (s.Tag == "MCSH")
    {
      if (ch.ShadowRaw)
      {
        issues.emplace_back(WorldIssueKind::Malformed, what, slabel, s.DataOffset, "appears more than once");
        continue;
      }
      ch.ShadowRaw = WorldSpan(data, s, what).Bytes(0, s.Size);
      if (s.Size != 512)
        issues.emplace_back(WorldIssueKind::Unexpected, what, slabel, s.DataOffset,
                            Num(s.Size) + " byte(s) (512 in every audited chunk)");
    }
    else
    {
      ch.Unsupported.push_back(Unsupported(s, "not seen in audited tex0 MCNKs"));
      issues.emplace_back(WorldIssueKind::Unsupported, what, slabel, s.DataOffset,
                          "sub-chunk not seen in audited tex0 MCNKs; kept undecoded");
    }
  }
  for (int k = 0; k < static_cast<int>(ch.Layers.size()); k++)
    if (ch.Layers[static_cast<size_t>(k)].TextureIndex >= static_cast<uint32_t>(textureCount))
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " MCLY", static_cast<long long>(mcly.DataOffset) + 16 * k,
                          "layer " + Num(k) + " names texture " + Num(ch.Layers[static_cast<size_t>(k)].TextureIndex) +
                            ", but MDID has " + Num(textureCount));
      ch.Status = WorldDataStatus::Malformed;
    }
  if (ch.Status == WorldDataStatus::Decoded)
  {
    std::vector<std::optional<AlphaMap>> maps;
    if (AdtParser::DecodeAlpha(data, haveMcal ? std::optional<WorldChunk>(mcal) : std::nullopt, ch.Layers, what, label,
                               issues, maps, haveMcly ? mcly.DataOffset : c.DataOffset, c.DataOffset))
      ch.Alpha = std::move(maps);
    else
      ch.Status = WorldDataStatus::Malformed;
  }
  return ch;
}

// ===================================================================================================== obj0 / obj1

/// One issue per chunk when records lack the FileDataID flag: every audited record has it, and without a name
/// table such a name cannot be resolved.
void ReportUnflagged(int count, const std::function<bool(int)>& unflagged, const std::string& tag, const std::string& flag,
                     long long dataOffset, int recordSize, bool nameTables, const std::string& what,
                     std::vector<WorldIssue>& issues)
{
  int n = 0, first = -1;
  for (int i = 0; i < count; i++)
    if (unflagged(i))
    {
      n++;
      if (first < 0)
        first = i;
    }
  if (n > 0)
    issues.emplace_back(WorldIssueKind::Unexpected, what, tag, dataOffset + static_cast<long long>(recordSize) * first,
                        Num(n) + " record(s) without the FileDataID flag " + flag + " (first: record " + Num(first) +
                          "); every audited record has it" +
                          (nameTables ? "; the file's name table is not decoded"
                                      : ", and the file has no name table to resolve them"));
}

AdtObj0Chunk ParseObj0Chunk(const std::vector<uint8_t>& data, const WorldChunk& c, int index, const std::string& what,
                            std::vector<WorldIssue>& issues)
{
  AdtObj0Chunk ch;
  ch.Index = index;
  const std::string label = "obj0 MCNK " + Num(index);
  std::vector<WorldChunk> sub;
  try
  {
    sub = WorldChunkWalker::Walk(data, c.DataOffset, c.DataOffset + c.Size, what, label);
  }
  catch (const WorldParseException& e)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, e.Chunk, e.Offset, e.Reason);
    return ch;
  }
  for (const WorldChunk& s : sub)
  {
    try
    {
      if (s.Tag == "MCRD" && !ch.McrdRaw)
        ch.McrdRaw = WorldSpan(data, s, what).U32Array();
      else if (s.Tag == "MCRW" && !ch.McrwRaw)
        ch.McrwRaw = WorldSpan(data, s, what).U32Array();
      else
      {
        ch.Unsupported.push_back(Unsupported(s, "not seen (or repeated) in audited obj0 MCNKs"));
        issues.emplace_back(WorldIssueKind::Unsupported, what, label + " " + WorldChunkWalker::Printable(s.Tag), s.DataOffset,
                            "sub-chunk not seen (or repeated) in audited obj0 MCNKs; kept undecoded");
      }
    }
    catch (const WorldParseException& e)
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " " + e.Chunk, e.Offset, e.Reason);
    }
  }
  return ch;
}

// ===================================================================================================== tile

template <typename T, typename F>
AdtPartResult<T> Part(const std::vector<uint32_t>* maid, WdtTileFileRole role, const std::vector<uint8_t>* bytes, F parse)
{
  AdtPartResult<T> r;
  if (bytes == nullptr)
  {
    const size_t i = static_cast<size_t>(role);
    r.State = maid != nullptr && maid->size() > i && (*maid)[i] == 0 ? AdtPartState::NotNamed : AdtPartState::NotSupplied;
    return r;
  }
  if (bytes->empty())
  {
    // A listed file that is not on this computer reads as empty (audit 3.3): not a malformed file.
    r.State = AdtPartState::NotSupplied;
    r.Error = "0 bytes supplied (an empty read: a listed file that is not on this computer opens empty, audit 3.3)";
    return r;
  }
  try
  {
    r.Value = parse();
    r.State = AdtPartState::Parsed;
  }
  catch (const WowParseException& e)
  {
    r.State = AdtPartState::Malformed;
    r.Error = e.what();
  }
  return r;
}
} // namespace

// ===================================================================================================== root

AdtRoot AdtParser::ParseRoot(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? "adt root " + Num(tileX) + "_" + Num(tileY) : whatIn;
  std::vector<WorldChunk> top = WalkFile(data, what);
  AdtRoot root;
  root.What = what;
  root.TileX = tileX;
  root.TileY = tileY;
  root.Chunks = top;
  root.Version = ReadVersion(data, top, what, root.Issues);
  std::vector<WorldChunk> mcnk = RequireMcnk(top, what);
  root.Mcnk.reserve(ChunksPerTile);
  for (int i = 0; i < ChunksPerTile; i++)
    root.Mcnk.push_back(ParseRootChunk(data, mcnk[static_cast<size_t>(i)], i, what, root.Issues));

  for (const WorldChunk& c : top)
  {
    if (c.Tag == "MVER" || c.Tag == "MCNK")
      continue;
    if (c.Tag == "MH2O")
    {
      if (root.Liquids)
      {
        root.Issues.emplace_back(WorldIssueKind::Malformed, what, "MH2O", c.HeaderOffset, RepeatedFirstUsed);
        continue;
      }
      root.Liquids = ParseLiquids(data, c, what, root.Issues);
    }
    else if (c.Tag == "MHDR")
      root.Unsupported.push_back(Unsupported(c, "header offsets not audited; chunks are found by walking"));
    else if (c.Tag == "MFBO" || c.Tag == "MBMH" || c.Tag == "MBBB" || c.Tag == "MBMI" || c.Tag == "MBNV")
      root.Unsupported.push_back(Unsupported(c, "layout not established (audit U18)"));
    else
    {
      root.Unsupported.push_back(Unsupported(c, "not seen in the audited root files"));
      root.Issues.emplace_back(WorldIssueKind::Unsupported, what, WorldChunkWalker::Printable(c.Tag), c.DataOffset,
                               "chunk not seen in the audited root files; kept undecoded");
    }
  }
  return root;
}

// ===================================================================================================== tex0

AdtTex AdtParser::ParseTex(const std::vector<uint8_t>& data, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? std::string("adt tex0") : whatIn;
  std::vector<WorldChunk> top = WalkFile(data, what);
  AdtTex tex;
  tex.What = what;
  tex.Chunks = top;
  tex.Version = ReadVersion(data, top, what, tex.Issues);
  std::vector<WorldChunk> mcnk = RequireMcnk(top, what);
  tex.DiffuseTextureIds = OptionalU32s(data, top, "MDID", what, tex.Issues);
  tex.HeightTextureIds = OptionalU32s(data, top, "MHID", what, tex.Issues);
  WorldChunk c;
  if (WorldChunkWalker::FindFirst(top, "MAMP", what, tex.Issues, c))
    tex.MampRaw = WorldSpan(data, c, what).Bytes(0, c.Size);
  if (WorldChunkWalker::FindFirst(top, "MTXP", what, tex.Issues, c))
    tex.MtxpRaw = WorldSpan(data, c, what).Bytes(0, c.Size);
  for (const WorldChunk& k : top)
  {
    if (k.Tag == "MVER" || k.Tag == "MCNK" || k.Tag == "MDID" || k.Tag == "MHID" || k.Tag == "MAMP" || k.Tag == "MTXP")
      continue;
    if (k.Tag == "MTEX")
    {
      tex.Unsupported.push_back(Unsupported(k, "texture name table: absent from every audited client (FileDataIDs are in MDID)"));
      tex.Issues.emplace_back(WorldIssueKind::Unsupported, what, "MTEX", k.DataOffset,
                              "name-based texture table; MCLY indices are only resolved through MDID");
    }
    else
    {
      tex.Unsupported.push_back(Unsupported(k, "not seen in the audited tex0 files"));
      tex.Issues.emplace_back(WorldIssueKind::Unsupported, what, WorldChunkWalker::Printable(k.Tag), k.DataOffset,
                              "chunk not seen in the audited tex0 files; kept undecoded");
    }
  }
  tex.Mcnk.reserve(ChunksPerTile);
  for (int i = 0; i < ChunksPerTile; i++)
    tex.Mcnk.push_back(ParseTexChunk(data, mcnk[static_cast<size_t>(i)], i, static_cast<int>(tex.DiffuseTextureIds.size()),
                                     what, tex.Issues));
  return tex;
}

bool AdtParser::DecodeAlpha(const std::vector<uint8_t>& data, const std::optional<WorldChunk>& mcal,
                            const std::vector<TerrainLayer>& layers, const std::string& what, const std::string& label,
                            std::vector<WorldIssue>& issues, std::vector<std::optional<AlphaMap>>& maps, long long mclyOffset,
                            long long mcnkOffset)
{
  maps.assign(layers.size(), std::nullopt);
  const long long len = static_cast<long long>(data.size());
  if (mcal)
  {
    const WorldChunk& mc = *mcal;
    if (mc.DataOffset < 0 || mc.Size < 0 || mc.DataOffset > len - mc.Size)
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " MCAL", mc.DataOffset, "MCAL is outside the buffer");
      return false;
    }
  }
  const int count = static_cast<int>(layers.size());
  for (int k = 0; k < count; k++)
  {
    bool has = (layers[static_cast<size_t>(k)].Flags & 0x100) != 0;
    if (has != (k >= 1))
    {
      issues.emplace_back(WorldIssueKind::Unexpected, what, label + " MCLY", mclyOffset + 16 * k + 4,
                          "layer " + Num(k) + " has flag 0x100 " + (has ? "set" : "clear") +
                            "; in every audited chunk exactly the layers after the first have it. Alpha not decoded");
      return false;
    }
  }
  if (count <= 1)
  {
    if (mcal && mcal->Size > 0)
      issues.emplace_back(WorldIssueKind::Unexpected, what, label + " MCAL", mcal->DataOffset,
                          Num(mcal->Size) + " byte(s) of MCAL but no layer after the first");
    return true;
  }
  if (!mcal)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label, mcnkOffset, Num(count) + " layers but no MCAL");
    return false;
  }
  const WorldChunk& m = *mcal;
  long long running = 0;
  bool four = false, eight = false;
  for (int k = 1; k < count; k++)
  {
    uint32_t off = layers[static_cast<size_t>(k)].AlphaOffset;
    if (off != running)
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " MCAL",
                          m.DataOffset + static_cast<long long>(std::min(off, static_cast<uint32_t>(m.Size))),
                          "layer " + Num(k) + "'s alpha starts at +" + Num(off) + ", expected +" + Num(running) +
                            " (maps are back to back from 0; layer " + Num(k - 1) + " ended there)");
      return false;
    }
    std::optional<AlphaMap> map;
    std::string error;
    if ((layers[static_cast<size_t>(k)].Flags & 0x200) != 0)
      map = DecodeRle(data, m.DataOffset + static_cast<int>(off), m.DataOffset + m.Size, error);
    else
    {
      long long next = k + 1 < count ? static_cast<long long>(layers[static_cast<size_t>(k + 1)].AlphaOffset)
                                     : static_cast<long long>(m.Size);
      map = DecodeRaw(data, m.DataOffset, m.Size, off, next, error);
    }
    if (!map)
    {
      issues.emplace_back(WorldIssueKind::Malformed, what, label + " MCAL", static_cast<long long>(m.DataOffset) + off,
                          "layer " + Num(k) + ": " + error);
      return false;
    }
    running += map->StoredBytes;
    if (map->BitDepth() == 4)
      four = true;
    else
      eight = true;
    maps[static_cast<size_t>(k)] = std::move(map);
  }
  if (running != m.Size)
  {
    issues.emplace_back(WorldIssueKind::Malformed, what, label + " MCAL", m.DataOffset + std::min(running, static_cast<long long>(m.Size)),
                        "the alpha maps use " + Num(running) + " of MCAL's " + Num(m.Size) + " byte(s)");
    return false;
  }
  if (four && eight)
    issues.emplace_back(WorldIssueKind::Unexpected, what, label + " MCAL", m.DataOffset,
                        "4-bit and 8-bit (or compressed) maps in one chunk: no audited chunk mixes them; each map is "
                        "decoded by its own spacing");
  return true;
}

std::optional<AlphaMap> AdtParser::DecodeRle(const std::vector<uint8_t>& data, int start, int end, std::string& error)
{
  error.clear();
  if (start < 0 || static_cast<long long>(end) > static_cast<long long>(data.size()) || start > end)
  {
    error = "compressed alpha range is outside the buffer";
    return std::nullopt;
  }
  std::vector<uint8_t> v(AlphaTexels);
  int filled = 0, p = start;
  while (filled < AlphaTexels)
  {
    if (p >= end)
    {
      error = "compressed alpha ends after " + Num(filled) + " of 4096 values";
      return std::nullopt;
    }
    int n = data[static_cast<size_t>(p++)];
    int count = n & 0x7F;
    if (count > AlphaTexels - filled)
    {
      error = "a run of " + Num(count) + " at value " + Num(filled) + " crosses the end of the 4096-value map";
      return std::nullopt;
    }
    if ((n & 0x80) != 0)
    {
      if (p >= end)
      {
        error = "a fill run has no value byte";
        return std::nullopt;
      }
      uint8_t b = data[static_cast<size_t>(p++)];
      for (int k = 0; k < count; k++)
        v[static_cast<size_t>(filled++)] = b;
    }
    else
    {
      if (count > end - p)
      {
        error = "a copy run of " + Num(count) + " byte(s) passes the end of MCAL";
        return std::nullopt;
      }
      if (count > 0)
        std::memcpy(v.data() + filled, data.data() + p, static_cast<size_t>(count));
      p += count;
      filled += count;
    }
  }
  AlphaMap map;
  map.Storage = AlphaStorage::Compressed;
  map.Samples = std::move(v);
  map.StoredBytes = p - start;
  return map;
}

std::optional<AlphaMap> AdtParser::DecodeRaw(const std::vector<uint8_t>& data, int mcalStart, int mcalSize, long long off,
                                             long long next, std::string& error)
{
  error.clear();
  const long long len = static_cast<long long>(data.size());
  if (mcalStart < 0 || mcalSize < 0 || mcalStart > len - mcalSize)
  {
    error = "MCAL at " + Num(mcalStart) + " of " + Num(mcalSize) + " byte(s) is outside the " + Num(len) + "-byte buffer";
    return std::nullopt;
  }
  if (off < 0 || next < off || next > mcalSize)
  {
    error = "uncompressed alpha [" + Num(off) + ", " + Num(next) + ") is outside the " + Num(mcalSize) + "-byte MCAL";
    return std::nullopt;
  }
  long long size = next - off;
  std::vector<uint8_t> v(AlphaTexels);
  const uint8_t* p = data.data() + mcalStart + off;
  AlphaMap map;
  if (size == AlphaTexels)
  {
    std::memcpy(v.data(), p, AlphaTexels);
    map.Storage = AlphaStorage::Uncompressed8;
    map.Samples = std::move(v);
    map.StoredBytes = AlphaTexels;
    return map;
  }
  if (size == AlphaTexels / 2)
  {
    for (int i = 0; i < AlphaTexels / 2; i++)
    {
      uint8_t b = p[i];
      v[static_cast<size_t>(2 * i)] = static_cast<uint8_t>(b & 0x0F);
      v[static_cast<size_t>(2 * i + 1)] = static_cast<uint8_t>(b >> 4);
    }
    map.Storage = AlphaStorage::Uncompressed4;
    map.Samples = std::move(v);
    map.StoredBytes = AlphaTexels / 2;
    return map;
  }
  error = "uncompressed alpha of " + Num(size) + " byte(s): only 4096 (8-bit) and 2048 (4-bit) are established";
  return std::nullopt;
}

// ===================================================================================================== obj0 / obj1 / lod

AdtObj0 AdtParser::ParseObj0(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? "adt obj0 " + Num(tileX) + "_" + Num(tileY) : whatIn;
  std::vector<WorldChunk> top = WalkFile(data, what);
  AdtObj0 obj;
  obj.What = what;
  obj.Chunks = top;
  obj.Version = ReadVersion(data, top, what, obj.Issues);
  WorldChunk mddf, modf;
  bool haveMddf = WorldChunkWalker::FindSingle(top, "MDDF", what, mddf);
  if (haveMddf)
    obj.Doodads = PlacementRecords::ReadDoodads(WorldSpan(data, mddf, what), tileX, tileY, "obj0 MDDF");
  bool haveModf = WorldChunkWalker::FindSingle(top, "MODF", what, modf);
  if (haveModf)
    obj.Wmos = PlacementRecords::ReadWmos(WorldSpan(data, modf, what), tileX, tileY, "obj0 MODF");
  bool nameTables = WorldChunkWalker::Count(top, "MMID") + WorldChunkWalker::Count(top, "MWID") > 0;
  ReportUnflagged(static_cast<int>(obj.Doodads.size()),
                  [&obj](int i) { return obj.Doodads[static_cast<size_t>(i)].NameKind() == PlacementNameKind::NameTableEntry; },
                  "MDDF", "0x40", haveMddf ? mddf.DataOffset : 0, PlacementRecords::DoodadRecordSize, nameTables, what,
                  obj.Issues);
  ReportUnflagged(static_cast<int>(obj.Wmos.size()),
                  [&obj](int i) { return obj.Wmos[static_cast<size_t>(i)].NameKind() == PlacementNameKind::NameTableEntry; },
                  "MODF", "0x8", haveModf ? modf.DataOffset : 0, PlacementRecords::WmoRecordSize, nameTables, what,
                  obj.Issues);
  for (const WorldChunk& k : top)
  {
    if (k.Tag == "MVER" || k.Tag == "MDDF" || k.Tag == "MODF")
      continue;
    if (k.Tag == "MCNK")
      obj.Mcnk.push_back(ParseObj0Chunk(data, k, static_cast<int>(obj.Mcnk.size()), what, obj.Issues));
    else if (k.Tag == "MWDR" || k.Tag == "MWDS" || k.Tag == "MLMB")
      obj.Unsupported.push_back(Unsupported(k, "layout not established (audit U18)"));
    else if (k.Tag == "MMDX" || k.Tag == "MMID" || k.Tag == "MWMO" || k.Tag == "MWID")
    {
      obj.Unsupported.push_back(Unsupported(k, "name table: absent from every audited client"));
      obj.Issues.emplace_back(WorldIssueKind::Unsupported, what, k.Tag, k.DataOffset,
                              "name table; placements without a FileDataID flag cannot be resolved here");
    }
    else
    {
      obj.Unsupported.push_back(Unsupported(k, "not seen in the audited obj0 files"));
      obj.Issues.emplace_back(WorldIssueKind::Unsupported, what, WorldChunkWalker::Printable(k.Tag), k.DataOffset,
                              "chunk not seen in the audited obj0 files; kept undecoded");
    }
  }
  if (static_cast<int>(obj.Mcnk.size()) != ChunksPerTile)
  {
    int first = -1;
    for (const WorldChunk& k : top)
      if (k.Tag == "MCNK")
      {
        first = k.HeaderOffset;
        break;
      }
    obj.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MCNK", first >= 0 ? first : static_cast<long long>(data.size()),
                            Num(static_cast<long long>(obj.Mcnk.size())) + " MCNK(s), 256 in every audited obj0");
  }
  return obj;
}

AdtObj1 AdtParser::ParseObj1(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? "adt obj1 " + Num(tileX) + "_" + Num(tileY) : whatIn;
  std::vector<WorldChunk> top = WalkFile(data, what);
  AdtObj1 obj;
  obj.What = what;
  obj.Chunks = top;
  obj.Version = ReadVersion(data, top, what, obj.Issues);
  WorldChunk c;
  bool haveMldd = WorldChunkWalker::FindSingle(top, "MLDD", what, c);
  if (haveMldd)
    obj.Doodads = PlacementRecords::ReadDoodads(WorldSpan(data, c, what), tileX, tileY, "obj1 MLDD");
  ReportUnflagged(static_cast<int>(obj.Doodads.size()),
                  [&obj](int i) { return obj.Doodads[static_cast<size_t>(i)].NameKind() == PlacementNameKind::NameTableEntry; },
                  "MLDD", "0x40", haveMldd ? c.DataOffset : 0, PlacementRecords::DoodadRecordSize, false, what, obj.Issues);
  if (WorldChunkWalker::FindFirst(top, "MLDX", what, obj.Issues, c))
  {
    WorldSpan s(data, c, what);
    if (static_cast<long long>(s.Size()) !=
        static_cast<long long>(PlacementRecords::DoodadBoxRecordSize) * static_cast<long long>(obj.Doodads.size()))
      obj.Issues.emplace_back(WorldIssueKind::Malformed, what, "MLDX", c.DataOffset,
                              Num(s.Size()) + " byte(s), expected 28 per MLDD record (" +
                                Num(static_cast<long long>(obj.Doodads.size())) + ")");
    else
    {
      try
      {
        obj.DoodadBoxes = PlacementRecords::ReadDoodadBoxes(s);
      }
      catch (const WorldParseException& e)
      {
        obj.Issues.emplace_back(WorldIssueKind::Malformed, what, "MLDX", e.Offset, e.Reason + "; the boxes are not used");
      }
    }
  }
  for (const WorldChunk& k : top)
  {
    if (k.Tag == "MVER" || k.Tag == "MLDD" || k.Tag == "MLDX")
      continue;
    if (k.Tag == "MLFD" || k.Tag == "MLDL" || k.Tag == "MDLI" || k.Tag == "MDLD" || k.Tag == "MLDB" || k.Tag == "MLMD" ||
        k.Tag == "MLMX" || k.Tag == "MLMB" || k.Tag == "MWDR" || k.Tag == "MWDS")
      obj.Unsupported.push_back(Unsupported(k, "layout not established (audit U17/U18)"));
    else
    {
      obj.Unsupported.push_back(Unsupported(k, "not seen in the audited obj1 files"));
      obj.Issues.emplace_back(WorldIssueKind::Unsupported, what, WorldChunkWalker::Printable(k.Tag), k.DataOffset,
                              "chunk not seen in the audited obj1 files; kept undecoded");
    }
  }
  return obj;
}

AdtLod AdtParser::ParseLod(const std::vector<uint8_t>& data, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? std::string("adt lod") : whatIn;
  std::vector<WorldChunk> top = WalkFile(data, what);
  AdtLod lod;
  lod.What = what;
  lod.Chunks = top;
  ReadVersion(data, top, what, lod.Issues);
  lod.Issues.emplace_back(WorldIssueKind::Unsupported, what, "lod", 0,
                          "_lod.adt chunk layouts are not established (audit U17); chunk inventory only");
  return lod;
}

// ===================================================================================================== tile

AdtTile AdtParser::ParseTile(int tileX, int tileY, const std::vector<uint32_t>* maidWords, const std::vector<uint8_t>* root,
                             const std::vector<uint8_t>* tex0, const std::vector<uint8_t>* obj0,
                             const std::vector<uint8_t>* obj1, const std::vector<uint8_t>* lod)
{
  AdtTile t;
  t.TileX = tileX;
  t.TileY = tileY;
  const std::string tile = Num(tileX) + "_" + Num(tileY);
  t.Root = Part<AdtRoot>(maidWords, WdtTileFileRole::RootAdt, root,
                         [&] { return ParseRoot(*root, tileX, tileY, "adt root " + tile); });
  t.Tex0 = Part<AdtTex>(maidWords, WdtTileFileRole::Tex0Adt, tex0, [&] { return ParseTex(*tex0, "adt tex0 " + tile); });
  t.Obj0 = Part<AdtObj0>(maidWords, WdtTileFileRole::Obj0Adt, obj0,
                         [&] { return ParseObj0(*obj0, tileX, tileY, "adt obj0 " + tile); });
  t.Obj1 = Part<AdtObj1>(maidWords, WdtTileFileRole::Obj1Adt, obj1,
                         [&] { return ParseObj1(*obj1, tileX, tileY, "adt obj1 " + tile); });
  t.Lod = Part<AdtLod>(maidWords, WdtTileFileRole::LodAdt, lod, [&] { return ParseLod(*lod, "adt lod " + tile); });
  return t;
}
} // namespace world
} // namespace wmv
