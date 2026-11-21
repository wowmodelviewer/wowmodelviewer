/*
 * worldparse_test.cpp
 *
 * Headless checks of the ported world parsers (wmv::world) against synthetic byte images built the way
 * Tools/UnityRendererProject/Assets/Scripts/Wow/World/AdtSynthetic.cs builds them: MVER, then 256 root MCNKs of a
 * 128-byte header (flags +0, index x/y +4/+8, 64-bit hole mask +20, area +52, 16-bit hole field +60, stored
 * position +104) followed by MCVT (145 floats, k * 0.25), MCNR (448 bytes) and an empty MCSE. Exit code 0 = pass.
 */

// This target is compiled in the wow library's directory, which defines BUILDING_WOW_DLL; it is a consumer of
// the library, so it must see the import side of the export macro.
#ifdef BUILDING_WOW_DLL
#    undef BUILDING_WOW_DLL
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "world/AdtParser.h"
#include "world/TerrainLayout.h"
#include "world/WdtParser.h"

using namespace wmv::world;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, const char* what, int line)
{
  checks++;
  if (!ok)
  {
    failures++;
    std::printf("FAIL line %d: %s\n", line, what);
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

using Bytes = std::vector<uint8_t>;

// ------------------------------------------------------------------ byte helpers (AdtSynthetic)

void PutU16(Bytes& b, size_t o, uint16_t v)
{
  b[o] = static_cast<uint8_t>(v);
  b[o + 1] = static_cast<uint8_t>(v >> 8);
}

void PutU32(Bytes& b, size_t o, uint32_t v)
{
  for (int k = 0; k < 4; k++)
    b[o + static_cast<size_t>(k)] = static_cast<uint8_t>(v >> (8 * k));
}

void PutU64(Bytes& b, size_t o, uint64_t v)
{
  PutU32(b, o, static_cast<uint32_t>(v));
  PutU32(b, o + 4, static_cast<uint32_t>(v >> 32));
}

void PutF32(Bytes& b, size_t o, float f)
{
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  PutU32(b, o, u);
}

/// One chunk: the tag stored reversed, the u32 size, the payload.
Bytes Chunk(const char* tag, const Bytes& payload)
{
  Bytes b(8 + payload.size());
  b[0] = static_cast<uint8_t>(tag[3]);
  b[1] = static_cast<uint8_t>(tag[2]);
  b[2] = static_cast<uint8_t>(tag[1]);
  b[3] = static_cast<uint8_t>(tag[0]);
  PutU32(b, 4, static_cast<uint32_t>(payload.size()));
  std::memcpy(b.data() + 8, payload.data(), payload.size());
  return b;
}

void Append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }

Bytes Mver()
{
  Bytes p(4);
  PutU32(p, 0, 18);
  return Chunk("MVER", p);
}

/// A root MCNK: 128-byte header + MCVT + MCNR + MCSE (AdtSynthetic.RootMcnk, default spec).
Bytes RootMcnk(int index, int tileX, int tileY, uint32_t flags = 0x10000, uint64_t holes = 0,
               const std::function<float(int)>& height = nullptr)
{
  Bytes h(128);
  PutU32(h, 0, flags);
  PutU32(h, 4, static_cast<uint32_t>(index % 16));
  PutU32(h, 8, static_cast<uint32_t>(index / 16));
  PutU64(h, 20, holes);
  PutU32(h, 52, 12);
  PutU16(h, 60, 0);
  double x, y;
  TerrainCoordinates::ChunkOrigin(tileX, tileY, index % 16, index / 16, x, y);
  PutF32(h, 104, static_cast<float>(x));
  PutF32(h, 108, static_cast<float>(y));
  PutF32(h, 112, 50.0f);
  Bytes mcvt(580);
  for (int k = 0; k < 145; k++)
    PutF32(mcvt, static_cast<size_t>(4 * k), height ? height(k) : static_cast<float>(k) * 0.25f);
  Bytes mcnr(448);
  for (int k = 0; k < 435; k++)
    mcnr[static_cast<size_t>(k)] = static_cast<uint8_t>(k % 127);
  Bytes payload = h;
  Append(payload, Chunk("MCVT", mcvt));
  Append(payload, Chunk("MCNR", mcnr));
  Append(payload, Chunk("MCSE", Bytes()));
  return Chunk("MCNK", payload);
}

/// A root: MVER, `extra`, then `count` MCNKs -- the default chunk unless `override` returns bytes for that index.
Bytes Root(int tileX, int tileY, const std::function<Bytes(int)>& override = nullptr, const Bytes& extra = Bytes(),
           int count = 256)
{
  Bytes b = Mver();
  Append(b, extra);
  for (int i = 0; i < count; i++)
  {
    Bytes o = override ? override(i) : Bytes();
    Append(b, o.empty() ? RootMcnk(i, tileX, tileY) : o);
  }
  return b;
}

/// AdtSynthetic.ShortMcnk: smaller than its 128-byte header.
Bytes ShortMcnk(int size = 100) { return Chunk("MCNK", Bytes(static_cast<size_t>(size))); }

/// AdtSynthetic.RleFill: `count` copies of `value` as fill runs of at most 127.
Bytes RleFill(uint8_t value, int count)
{
  Bytes b;
  while (count > 0)
  {
    int n = count > 127 ? 127 : count;
    b.push_back(static_cast<uint8_t>(0x80 | n));
    b.push_back(value);
    count -= n;
  }
  return b;
}

bool HasIssue(const std::vector<WorldIssue>& issues, WorldIssueKind kind, const std::string& chunk)
{
  for (const WorldIssue& i : issues)
    if (i.Kind == kind && i.Chunk == chunk)
      return true;
  return false;
}

int CountKind(const std::vector<WorldIssue>& issues, WorldIssueKind kind)
{
  int n = 0;
  for (const WorldIssue& i : issues)
    if (i.Kind == kind)
      n++;
  return n;
}

// ------------------------------------------------------------------ cases

void WellFormedRoot()
{
  const int tx = 31, ty = 32;
  Bytes data = Root(tx, ty);
  AdtRoot root = AdtParser::ParseRoot(data, tx, ty);
  CHECK(root.Version == 18);
  CHECK(root.Mcnk.size() == 256);
  CHECK(root.Chunks.size() == 257);
  CHECK(!root.Liquids.has_value());
  CHECK(CountKind(root.Issues, WorldIssueKind::Malformed) == 0);
  bool heightsOk = true, statusOk = true;
  for (const AdtRootChunk& c : root.Mcnk)
  {
    statusOk &= c.Status == WorldDataStatus::Decoded;
    heightsOk &= c.Heights.size() == 145;
    for (size_t k = 0; k < c.Heights.size(); k++)
      heightsOk &= c.Heights[k] == static_cast<float>(k) * 0.25f;
  }
  CHECK(statusOk);
  CHECK(heightsOk);
  const AdtRootChunk& c17 = root.Mcnk[17];
  CHECK(c17.IndexX == 1 && c17.IndexY == 1);
  CHECK(c17.AreaId == 12);
  CHECK(c17.StoredPosition.z == 50.0f);
  double x, y;
  TerrainCoordinates::ChunkOrigin(tx, ty, 1, 1, x, y);
  CHECK(c17.StoredPosition.x == static_cast<float>(x) && c17.StoredPosition.y == static_cast<float>(y));
  CHECK(c17.NormalsRaw.size() == 435 && c17.NormalsTrailingRaw.size() == 13);
  CHECK(c17.McseSize == 0);
  CHECK(!c17.HasVertexColors());
  CHECK(c17.Holes.Kind == HoleMaskKind::HighResolution);
  CHECK(c17.Holes.HasAnyHole() == std::optional<bool>(false));
  CHECK(c17.RawHeader.size() == 128);
  CHECK(TerrainVertexLayout::Inner(7, 7) == 135 && TerrainVertexLayout::Outer(8, 8) == 144);
  CHECK(TerrainVertexLayout::IsOuter(144) && !TerrainVertexLayout::IsOuter(135));
}

void HolesAndHeights()
{
  // chunk 3: a hole at cell (2, 5); chunk 9: heights 10 - k
  Bytes data = Root(0, 0, [](int i) {
    if (i == 3)
      return RootMcnk(3, 0, 0, 0x10000, 1ULL << (2 * 8 + 5));
    if (i == 9)
      return RootMcnk(9, 0, 0, 0x10000, 0, [](int k) { return 10.0f - static_cast<float>(k); });
    return Bytes();
  });
  AdtRoot root = AdtParser::ParseRoot(data, 0, 0);
  CHECK(root.Mcnk[3].Holes.IsHole(2, 5) == std::optional<bool>(true));
  CHECK(root.Mcnk[3].Holes.IsHole(5, 2) == std::optional<bool>(false));
  CHECK(root.Mcnk[9].Heights[144] == 10.0f - 144.0f);
}

void LowResolutionHoles()
{
  // flag 0x10000 clear: Unexpected, holes not read
  Bytes data = Root(0, 0, [](int i) { return i == 0 ? RootMcnk(0, 0, 0, 0, 0xFFFF) : Bytes(); });
  AdtRoot root = AdtParser::ParseRoot(data, 0, 0);
  CHECK(root.Mcnk[0].Status == WorldDataStatus::Decoded);
  CHECK(root.Mcnk[0].Holes.Kind == HoleMaskKind::LowResolutionUnsupported);
  CHECK(root.Mcnk[0].Holes.IsHole(0, 0) == std::optional<bool>(false)); // the 16-bit field is 0
  CHECK(HasIssue(root.Issues, WorldIssueKind::Unexpected, "MCNK 0"));
}

void ShortChunkIsLocal()
{
  // AdtSynthetic.ShortMcnk at index 5: that chunk is Malformed, the rest decode.
  Bytes data = Root(1, 2, [](int i) { return i == 5 ? ShortMcnk() : Bytes(); });
  AdtRoot root = AdtParser::ParseRoot(data, 1, 2);
  CHECK(root.Mcnk.size() == 256);
  CHECK(root.Mcnk[5].Status == WorldDataStatus::Malformed);
  CHECK(root.Mcnk[5].Heights.empty());
  CHECK(root.Mcnk[4].Status == WorldDataStatus::Decoded && root.Mcnk[6].Status == WorldDataStatus::Decoded);
  CHECK(HasIssue(root.Issues, WorldIssueKind::Malformed, "MCNK 5"));
}

void WrongMcnkCountIsFatal()
{
  Bytes data = Root(0, 0, nullptr, Bytes(), 255);
  bool threw = false;
  try
  {
    AdtParser::ParseRoot(data, 0, 0);
  }
  catch (const WorldParseException& e)
  {
    threw = e.Chunk == "MCNK" && e.Reason.find("255 MCNK") != std::string::npos;
  }
  CHECK(threw);

  // a truncated file: the chunk walk fails
  Bytes good = Root(0, 0);
  good.resize(good.size() - 3);
  threw = false;
  try
  {
    AdtParser::ParseRoot(good, 0, 0);
  }
  catch (const WowParseException&)
  {
    threw = true;
  }
  CHECK(threw);
}

void FlatLiquid()
{
  // MH2O: chunk 0 has one instance (type 5, word 42, whole chunk, no bitmap, no vertex data, flat at 100).
  Bytes p(256 * 12 + 24);
  PutU32(p, 0, 256 * 12); // instances offset
  PutU32(p, 4, 1);        // layer count
  size_t o = 256 * 12;
  PutU16(p, o, 5);
  PutU16(p, o + 2, 42);
  PutF32(p, o + 4, 100.0f);
  PutF32(p, o + 8, 100.0f);
  p[o + 14] = 8;
  p[o + 15] = 8;
  Bytes data = Root(0, 0, nullptr, Chunk("MH2O", p));
  AdtRoot root = AdtParser::ParseRoot(data, 0, 0);
  CHECK(root.Liquids.has_value());
  if (!root.Liquids)
    return;
  const AdtLiquids& l = *root.Liquids;
  CHECK(l.Status == WorldDataStatus::PartiallyDecoded);
  CHECK(l.Instances.size() == 256 && l.Instances[0].size() == 1 && l.Instances[1].empty());
  CHECK(l.MalformedInstances == 0);
  if (l.Instances[0].size() == 1)
  {
    const LiquidInstance& li = l.Instances[0][0];
    CHECK(li.Status == WorldDataStatus::Decoded);
    CHECK(li.FormatBySize == -1);
    CHECK(li.QuadExists(7, 7) && !li.QuadExists(8, 0));
    CHECK(li.HeightAt(8, 8) == 100.0f);
  }
}

void AlphaRle()
{
  Bytes rle = RleFill(7, 4096);
  std::string error;
  std::optional<AlphaMap> map = AdtParser::DecodeRle(rle, 0, static_cast<int>(rle.size()), error);
  CHECK(map.has_value());
  if (map)
  {
    CHECK(map->Samples.size() == 4096 && map->Sample(63, 63) == 7);
    CHECK(map->StoredBytes == static_cast<int>(rle.size()));
    CHECK(map->BitDepth() == 8);
  }
  map = AdtParser::DecodeRle(rle, 0, static_cast<int>(rle.size()) - 2, error);
  CHECK(!map.has_value() && !error.empty());
}

void Wdt()
{
  const int tile = 32 * 64 + 31;
  Bytes mphd(32);
  Bytes main(4096 * 8);
  PutU32(main, static_cast<size_t>(8 * tile), 1);
  Bytes maid(4096 * 32);
  for (uint32_t k = 0; k < 4; k++)
    PutU32(maid, static_cast<size_t>(tile * 32) + 4 * k, 1000 + k);
  Bytes data = Mver();
  Append(data, Chunk("MPHD", mphd));
  Append(data, Chunk("MAIN", main));
  Append(data, Chunk("MAID", maid));
  WdtFile wdt = WdtParser::Parse(data);
  CHECK(wdt.Kind == WdtKind::TiledTerrain);
  CHECK(wdt.TileCount() == 1);
  CHECK(wdt.TileExists(31, 32) && !wdt.TileExists(32, 31));
  CHECK(wdt.Issues.empty());
  WdtTileFiles files;
  CHECK(wdt.TryGetTileFiles(31, 32, files));
  CHECK(files[WdtTileFileRole::RootAdt] == 1000 && files[WdtTileFileRole::Tex0Adt] == 1003);
  CHECK(!files.Names(WdtTileFileRole::LodAdt));

  // ParseTile: root parsed, tex0 an empty read, obj0 named but not supplied, lod not named by MAID.
  std::vector<uint32_t> words(files.FileDataIds.begin(), files.FileDataIds.end());
  Bytes root = Root(31, 32);
  Bytes empty;
  Bytes junk = {1, 2, 3};
  AdtTile t = AdtParser::ParseTile(31, 32, &words, &root, &empty, nullptr, &junk, nullptr);
  CHECK(t.Root.State == AdtPartState::Parsed && t.Root.Value && t.Root.Value->Mcnk.size() == 256);
  CHECK(t.Tex0.State == AdtPartState::NotSupplied && !t.Tex0.Error.empty());
  CHECK(t.Obj0.State == AdtPartState::NotSupplied);
  CHECK(t.Obj1.State == AdtPartState::Malformed && !t.Obj1.Error.empty());
  CHECK(t.Lod.State == AdtPartState::NotNamed);
}
} // namespace

int main()
{
  try
  {
    WellFormedRoot();
    HolesAndHeights();
    LowResolutionHoles();
    ShortChunkIsLocal();
    WrongMcnkCountIsFatal();
    FlatLiquid();
    AlphaRle();
    Wdt();
  }
  catch (const std::exception& e)
  {
    std::printf("FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  std::printf("worldparse_test: %d check(s), %d failure(s)\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
