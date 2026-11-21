/*
 * WdtParser.cpp  (port of Wow/World/WdtParser.cs)
 */

#include "WdtParser.h"

#include <stdexcept>

#include "WorldText.h"

namespace wmv
{
namespace world
{
using text::Hex;
using text::Num;

const char* ToString(WdtKind k)
{
  switch (k)
  {
    case WdtKind::TiledTerrain: return "TiledTerrain";
    case WdtKind::GlobalWmo: return "GlobalWmo";
    case WdtKind::Empty: return "Empty";
    case WdtKind::TiledTerrainWithGlobalWmo: return "TiledTerrainWithGlobalWmo";
  }
  return "?";
}

int WdtFile::TileIndex(int x, int y)
{
  if (x < 0 || x >= Tiles || y < 0 || y >= Tiles)
    throw std::out_of_range("x/y: tile x/y must be 0..63");
  return y * Tiles + x;
}

bool WdtFile::TryGetTileFiles(int x, int y, WdtTileFiles& files) const
{
  files = WdtTileFiles();
  if (Maid.empty() || x < 0 || x >= Tiles || y < 0 || y >= Tiles)
    return false;
  int i = y * Tiles + x;
  files.TileX = x;
  files.TileY = y;
  for (int k = 0; k < MaidWordsPerTile; k++)
    files.FileDataIds[static_cast<size_t>(k)] = Maid[static_cast<size_t>(i * MaidWordsPerTile + k)];
  return true;
}

int WdtFile::TileCount() const
{
  int n = 0;
  for (const WdtMainEntry& e : Main)
    if (e.HasTile())
      n++;
  return n;
}

bool WdtFile::TryGetGlobalWmoReference(int placement, WorldFileReference& reference) const
{
  reference = WorldFileReference();
  if (placement < 0 || placement >= static_cast<int>(GlobalWmos.size()))
    return false;
  const WmoPlacement& p = GlobalWmos[static_cast<size_t>(placement)];
  if (p.NameKind() == PlacementNameKind::FileDataId)
  {
    if (p.NameId == 0)
      return false; // 0 names no file, as in MAID
    reference = WorldFileReference::ById(p.NameId);
    return true;
  }
  // Old form: NameId is an index or a byte offset into MWMO -- the audited data cannot tell which. Only NameId 0
  // with the first name starting at byte 0 means the same under both.
  if (!MwmoNames || MwmoNames->empty() || MwmoNameOffsets[0] != 0 || p.NameId != 0)
    return false;
  reference = WorldFileReference::ByPath((*MwmoNames)[0]);
  return true;
}

namespace
{
/// NUL-separated names with their byte offsets (latin-1: one byte per char; kept as raw bytes).
void ReadNames(const std::vector<uint8_t>& data, const WorldChunk& c, const std::string& what, std::vector<std::string>& names,
               std::vector<int>& offsets)
{
  WorldSpan s(data, c, what);
  names.clear();
  offsets.clear();
  int start = 0;
  for (int i = 0; i <= s.Size(); i++)
  {
    if (i == s.Size() || data[static_cast<size_t>(c.DataOffset + i)] == 0)
    {
      if (i > start)
      {
        names.emplace_back(reinterpret_cast<const char*>(data.data() + c.DataOffset + start), static_cast<size_t>(i - start));
        offsets.push_back(start);
      }
      start = i + 1;
    }
  }
}
} // namespace

WdtFile WdtParser::Parse(const std::vector<uint8_t>& data, const std::string& whatIn)
{
  const std::string what = whatIn.empty() ? std::string("wdt") : whatIn;
  std::vector<WorldChunk> chunks = WorldChunkWalker::Walk(data, 0, static_cast<int>(data.size()), what, "file");
  WdtFile wdt;
  wdt.What = what;
  wdt.Chunks = chunks;
  std::vector<WorldUnsupportedChunk> unsupported;

  if (chunks.empty() || chunks[0].Tag != "MVER")
    throw WorldParseException(what, "MVER", 0, "the file does not start with an MVER chunk");
  WorldSpan ver(data, chunks[0], what);
  if (ver.Size() < 4)
    throw WorldParseException(what, "MVER", chunks[0].DataOffset, "shorter than 4 bytes");
  wdt.Version = ver.U32(0);
  if (wdt.Version != 18)
    wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MVER", chunks[0].DataOffset,
                            "version " + Num(wdt.Version) + " (every audited WDT has 18)");

  WorldChunk c;
  if (!WorldChunkWalker::FindSingle(chunks, "MPHD", what, c))
    throw WorldParseException(what, "MPHD", 0, "missing");
  WorldSpan mphd(data, c, what);
  if (mphd.Size() < 32)
    throw WorldParseException(what, "MPHD", c.DataOffset, Num(mphd.Size()) + " byte(s), expected 32 (8 words)");
  if (mphd.Size() != 32)
    wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MPHD", c.DataOffset,
                            Num(mphd.Size()) + " byte(s); the 8 audited words are read, the rest is ignored");
  for (int i = 0; i < 8; i++)
    wdt.Header.Words[static_cast<size_t>(i)] = mphd.U32(4 * i);

  if (!WorldChunkWalker::FindSingle(chunks, "MAIN", what, c))
    throw WorldParseException(what, "MAIN", 0, "missing");
  WorldSpan main(data, c, what);
  const int mainBytes = WdtFile::Tiles * WdtFile::Tiles * MainEntrySize;
  if (main.Size() != mainBytes)
    throw WorldParseException(what, "MAIN", c.DataOffset,
                              Num(main.Size()) + " byte(s), expected " + Num(mainBytes) + " (4096 entries of 8 bytes)");
  wdt.Main.resize(WdtFile::Tiles * WdtFile::Tiles);
  int unseen = 0, firstUnseen = -1;
  for (int i = 0; i < static_cast<int>(wdt.Main.size()); i++)
  {
    WdtMainEntry& e = wdt.Main[static_cast<size_t>(i)];
    e.Flags = main.U32(MainEntrySize * i);
    e.Word1Raw = main.U32(MainEntrySize * i + 4);
    if (e.Flags > 2 || e.Word1Raw != 0)
    {
      unseen++;
      if (firstUnseen < 0)
        firstUnseen = i;
    }
  }
  if (unseen > 0)
  {
    const WdtMainEntry& e = wdt.Main[static_cast<size_t>(firstUnseen)];
    wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * firstUnseen),
                            Num(unseen) + " entr(ies) with a value other than 0, 1, 2 or a non-zero second word (first: x " +
                              Num(firstUnseen % 64) + ", y " + Num(firstUnseen / 64) + ", value 0x" + Hex(e.Flags) +
                              ", word 0x" + Hex(e.Word1Raw) +
                              "); the audited WDTs hold only 0, 1 and 2 with a zero second word. A set bit 0x1 is read as a tile");
  }

  if (WorldChunkWalker::FindSingle(chunks, "MAID", what, c))
  {
    WorldSpan maid(data, c, what);
    const int maidBytes = WdtFile::Tiles * WdtFile::Tiles * MaidEntrySize;
    if (maid.Size() != maidBytes)
      throw WorldParseException(what, "MAID", c.DataOffset,
                                Num(maid.Size()) + " byte(s), expected " + Num(maidBytes) + " (4096 entries of 32 bytes)");
    wdt.Maid.resize(static_cast<size_t>(WdtFile::Tiles * WdtFile::Tiles * WdtFile::MaidWordsPerTile));
    for (int i = 0; i < static_cast<int>(wdt.Maid.size()); i++)
      wdt.Maid[static_cast<size_t>(i)] = maid.U32(4 * i);
    // MAIN bit 0x1 <=> MAID names files held in every audited WDT; a break is flagged, not fixed.
    int mismatches = 0, firstBad = -1;
    for (int i = 0; i < static_cast<int>(wdt.Main.size()); i++)
    {
      bool named = false;
      for (int k = 0; k < WdtFile::MaidWordsPerTile; k++)
        named |= wdt.Maid[static_cast<size_t>(i * WdtFile::MaidWordsPerTile + k)] != 0;
      if (named != wdt.Main[static_cast<size_t>(i)].HasTile())
      {
        mismatches++;
        if (firstBad < 0)
          firstBad = i;
      }
    }
    if (mismatches > 0)
      wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * firstBad),
                              Num(mismatches) + " tile(s) where MAIN bit 0x1 and 'MAID names files' disagree (first: x " +
                                Num(firstBad % 64) + ", y " + Num(firstBad / 64) + "; its MAID entry at 0x" +
                                Hex(maid.AbsoluteOffset(MaidEntrySize * firstBad)) + ")");
  }
  else
  {
    for (int i = 0; i < static_cast<int>(wdt.Main.size()); i++)
      if (wdt.Main[static_cast<size_t>(i)].HasTile())
      {
        wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * i),
                                "tile x " + Num(i % 64) + ", y " + Num(i / 64) +
                                  " has MAIN bit 0x1 but the WDT has no MAID: its files are not named");
        break;
      }
  }

  WorldChunk mwmo, modf;
  bool haveMwmo = WorldChunkWalker::FindFirst(chunks, "MWMO", what, wdt.Issues, mwmo);
  if (haveMwmo)
  {
    wdt.MwmoNames.emplace();
    ReadNames(data, mwmo, what, *wdt.MwmoNames, wdt.MwmoNameOffsets);
  }
  bool haveModf = WorldChunkWalker::FindSingle(chunks, "MODF", what, modf);
  if (haveModf)
    wdt.GlobalWmos = PlacementRecords::ReadWmos(WorldSpan(data, modf, what), -1, -1, "WDT MODF");
  for (int i = 0; i < static_cast<int>(wdt.GlobalWmos.size()); i++)
  {
    const WmoPlacement& p = wdt.GlobalWmos[static_cast<size_t>(i)];
    long long at = static_cast<long long>(modf.DataOffset) + static_cast<long long>(PlacementRecords::WmoRecordSize) * i;
    if (p.NameKind() == PlacementNameKind::FileDataId && p.NameId == 0)
      wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MODF", at,
                              "record " + Num(i) + ": FileDataID flag 0x8 with name 0 (names no file)");
    else if (p.NameKind() == PlacementNameKind::NameTableEntry && !haveMwmo)
      wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MODF", at,
                              "record " + Num(i) + ": no FileDataID flag and no MWMO name table: the WMO cannot be resolved");
    else if (p.NameKind() == PlacementNameKind::NameTableEntry &&
             (p.NameId != 0 || wdt.MwmoNames->empty() || wdt.MwmoNameOffsets[0] != 0))
      wdt.Issues.emplace_back(
        WorldIssueKind::Unexpected, what, "MWMO", mwmo.DataOffset,
        "record " + Num(i) + ": name " + Num(p.NameId) + " into an MWMO whose first name starts at " +
          (!wdt.MwmoNames->empty() ? Num(wdt.MwmoNameOffsets[0]) : std::string("none")) +
          ": index and byte-offset readings disagree (every audited old-form WDT has name 0 and one name at offset 0)");
  }

  for (const WorldChunk& k : chunks)
  {
    if (k.Tag == "MVER" || k.Tag == "MPHD" || k.Tag == "MAIN" || k.Tag == "MAID" || k.Tag == "MWMO" || k.Tag == "MODF")
      continue;
    WorldUnsupportedChunk u;
    u.DataOffset = k.DataOffset;
    u.Size = k.Size;
    if (k.Tag == "MAI2" || k.Tag == "MANM")
    {
      u.Tag = k.Tag;
      u.Reason = "layout and use not established (audit U4/U5)";
      unsupported.push_back(u);
    }
    else
    {
      u.Tag = WorldChunkWalker::Printable(k.Tag);
      u.Reason = "not seen in the audited WDTs";
      unsupported.push_back(u);
      wdt.Issues.emplace_back(WorldIssueKind::Unsupported, what, WorldChunkWalker::Printable(k.Tag), k.DataOffset,
                              "chunk not seen in the audited WDTs; kept undecoded");
    }
  }
  wdt.Unsupported = unsupported;

  int tiles = wdt.TileCount();
  if (tiles > 0 && !wdt.GlobalWmos.empty())
  {
    wdt.Kind = WdtKind::TiledTerrainWithGlobalWmo;
    wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MODF", modf.HeaderOffset,
                            "tiles and a MODF in one WDT: never seen in the audited clients");
  }
  else if (tiles > 0)
    wdt.Kind = WdtKind::TiledTerrain;
  else if (!wdt.GlobalWmos.empty())
    wdt.Kind = WdtKind::GlobalWmo;
  else
    wdt.Kind = WdtKind::Empty;
  if (wdt.Header.WmoOnlyFlag() != (wdt.Kind == WdtKind::GlobalWmo))
    wdt.Issues.emplace_back(WorldIssueKind::Unexpected, what, "MPHD", mphd.AbsoluteOffset(0),
                            std::string("MPHD bit 0x1 is ") + (wdt.Header.WmoOnlyFlag() ? "set" : "clear") +
                              " but the WDT is " + ToString(wdt.Kind) +
                              " (bit 0x1 <=> WMO-only held in every audited WDT)");
  return wdt;
}
} // namespace world
} // namespace wmv
