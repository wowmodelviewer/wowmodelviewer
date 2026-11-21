/*
 * WdtParser.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/WdtParser.cs)
 *
 * Bounds-safe reader for a map's WDT, from the bytes the caller supplies (it never opens CASC).
 *
 * Evidence (docs/world-viewer/00-audit.md 4.1-4.3, T2, T3; tables reproduced by WorldCorpusValidation):
 *   - Forms: MVER(18) MPHD MAIN, then MAID [MODF] [MAI2|MANM] (every Classic-family WDT, 763 of 969 Retail), or
 *     MWMO MODF (193 Retail, old WMO-only form), or nothing (13 Retail). No WDT has both tiles and a MODF.
 *   - MPHD: 8 u32. Word 0 = flags (10 bits seen; bit 0x1 <=> WMO-only exactly; no other bit's meaning
 *     established). Words 1-6 = FileDataIDs of the _lgt.wdt, _occ.wdt, _fogs.wdt, _mpv.wdt, .tex and .wdl side
 *     files. Evidence: the six files of Azeroth, read in all four clients -- Retail's identified by their chunk
 *     ids; the 12-byte MVER-only files of Era (lgt, fogs, mpv), MoP (fogs, mpv) and Beta (mpv) only by their MVER
 *     version (lgt 20, fogs 2, mpv 3) and the same FileDataID; other maps by listfile name only. Word 7 is
 *     non-zero exactly on the modern WMO-only form; its target was never read (listfile name only: .pd4) -- raw.
 *   - MAIN: 4096 entries of 8 bytes, index y*64 + x. The corpus holds only the values 0, 1 and 2; value 1 <=>
 *     MAID names files (every MAID-bearing WDT of every client), 2 => no MAID words; the second word is 0 in every
 *     entry. A tile is "bit 0x1 set"; any other value or a non-zero second word raises an Unexpected issue.
 *   - MAID: 4096 entries of 8 u32 FileDataIDs: root ADT, obj0, obj1, tex0, lod ADT, map texture, map texture _n,
 *     minimap. Evidence (window tiles): words 0-3 by chunk content in all four clients, word 4 by content in
 *     Retail/Beta; words 5-7 by BLP2 size and mip count in Retail/Beta (5: 512^2 with mips, terrain-only image;
 *     6: 256^2; 7: 512^2 without mips, the minimap); Era/MoP word 7 by BLP2 256^2; Era/MoP words 4-6 by listfile
 *     name only (no such file is pinned). Words 4-6 are optional (zero on 21-93% of tiles). A named FileDataID
 *     says nothing about whether the client has the file: availability belongs to the asset provider.
 *   - MODF: the map's single WMO (WMO-only maps), 64-byte record (WorldPlacements.h). Modern form: name is a
 *     FileDataID (flag 0x8); old form: name is an MWMO entry (flag 0), scale 0.
 *   - MAI2 / MANM: present on a few maps, layout/use not established: listed, not decoded.
 *
 * Ownership: the returned WdtFile holds its own arrays; it keeps no reference to `data`.
 */

#ifndef _WMV_WORLD_WDTPARSER_H_
#define _WMV_WORLD_WDTPARSER_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "WorldApi.h"
#include "WorldChunks.h"
#include "WorldPlacements.h"

namespace wmv
{
namespace world
{
/// What kind of map a WDT describes, from its own bytes.
enum class WdtKind
{
  /// At least one MAIN entry has flag bit 0x1 (terrain tiles named by MAID).
  TiledTerrain,
  /// No tile, one or more MODF records: a global-WMO (WMO-only) map.
  GlobalWmo,
  /// No tile and no MODF (the 13 bare Retail WDTs): nothing to show.
  Empty,
  /// Tiles AND a MODF: never seen in the audited clients; both are kept and an Unexpected issue is raised.
  TiledTerrainWithGlobalWmo,
};

_WMVWORLD_API_ const char* ToString(WdtKind k);

/// The role of one MAID word, in the proven order.
enum class WdtTileFileRole
{
  RootAdt = 0,
  Obj0Adt = 1,
  Obj1Adt = 2,
  Tex0Adt = 3,
  LodAdt = 4,
  MapTexture = 5,
  MapTextureN = 6,
  Minimap = 7,
};

/// A reference to a file, as the map's own data states it. A FileDataID or a path; resolving it (by FileDataID
/// or by path, per client) and reading it are the host's job.
struct WorldFileReference
{
  bool IsPath = false;
  uint32_t FileDataId = 0; // when !IsPath
  std::string Path;        // when IsPath (old WMO-only WDT: MWMO string, raw bytes = latin-1)

  static WorldFileReference ById(uint32_t id)
  {
    WorldFileReference r;
    r.FileDataId = id;
    return r;
  }
  static WorldFileReference ByPath(const std::string& path)
  {
    WorldFileReference r;
    r.IsPath = true;
    r.Path = path;
    return r;
  }
  std::string ToString() const { return IsPath ? "path:" + Path : "fdid:" + std::to_string(FileDataId); }
};

struct WdtHeader
{
  std::array<uint32_t, 8> Words{}; // the 8 MPHD words as stored
  uint32_t Flags() const { return Words[0]; }
  uint32_t LgtWdtFileDataId() const { return Words[1]; }
  uint32_t OccWdtFileDataId() const { return Words[2]; }
  uint32_t FogsWdtFileDataId() const { return Words[3]; }
  uint32_t MpvWdtFileDataId() const { return Words[4]; }
  uint32_t TexFileDataId() const { return Words[5]; }
  uint32_t WdlFileDataId() const { return Words[6]; }
  /// Raw: non-zero exactly on modern WMO-only WDTs; target never read (.pd4 by name only).
  uint32_t Word7Raw() const { return Words[7]; }
  /// MPHD bit 0x1: exactly the WMO-only WDTs in all four clients (a 100% correlation).
  bool WmoOnlyFlag() const { return (Words[0] & 1) != 0; }
};

struct WdtMainEntry
{
  uint32_t Flags = 0;    // bit 0x1 <=> MAID names this tile's files
  uint32_t Word1Raw = 0; // 0 in every audited entry
  bool HasTile() const { return (Flags & 1) != 0; }
};

/// A tile's eight MAID words, in the proven order. 0 = the client names no file.
struct WdtTileFiles
{
  int TileX = 0, TileY = 0;
  std::array<uint32_t, 8> FileDataIds{}; // index = (int)WdtTileFileRole

  uint32_t operator[](WdtTileFileRole role) const { return FileDataIds[static_cast<size_t>(role)]; }
  bool Names(WdtTileFileRole role) const { return FileDataIds[static_cast<size_t>(role)] != 0; }
};

struct _WMVWORLD_API_ WdtFile
{
  static constexpr int Tiles = 64;
  static constexpr int MaidWordsPerTile = 8;

  std::string What;
  uint32_t Version = 0;
  std::vector<WorldChunk> Chunks;
  WdtHeader Header;
  std::vector<WdtMainEntry> Main;                 // 4096, index y*64 + x
  std::vector<uint32_t> Maid;                     // 4096 * 8 words, or empty when the WDT has no MAID
  std::optional<std::vector<std::string>> MwmoNames; // old WMO-only form, or nullopt (raw bytes = latin-1)
  std::vector<int> MwmoNameOffsets;               // byte offset of each name inside MWMO
  std::vector<WmoPlacement> GlobalWmos;           // MODF records (empty when none)
  std::vector<WorldUnsupportedChunk> Unsupported;
  WdtKind Kind = WdtKind::Empty;
  std::vector<WorldIssue> Issues;

  bool HasMaid() const { return !Maid.empty(); }

  /// y*64 + x; throws std::out_of_range for x/y outside 0..63.
  static int TileIndex(int x, int y);

  /// MAIN flag bit 0x1 at (x, y). Only what the WDT declares: it says nothing about whether the client has the
  /// tile's files (one audited Beta root, Kalimdor 44_24, is listed but not on the computer). Availability is the
  /// asset provider's, reported per part as AdtPartState::NotSupplied. Throws for x/y outside 0..63.
  bool TileExists(int x, int y) const { return Main[static_cast<size_t>(TileIndex(x, y))].HasTile(); }

  /// The MAID words of (x, y); false when the WDT has no MAID or x/y is outside 0..63.
  bool TryGetTileFiles(int x, int y, WdtTileFiles& files) const;

  int TileCount() const;

  /// The global WMO's file: a FileDataID (modern form) or the MWMO path (old form). False when the record names
  /// nothing resolvable: FileDataID 0, or an MWMO reference that is not unambiguous.
  bool TryGetGlobalWmoReference(int placement, WorldFileReference& reference) const;
};

class _WMVWORLD_API_ WdtParser
{
public:
  static constexpr int MainEntrySize = 8;
  static constexpr int MaidEntrySize = 32;

  /// Throws WorldParseException for a malformed required structure (chunk walk, MVER, MPHD, MAIN, MAID, MODF).
  /// `what` names the file in messages (empty = "wdt").
  static WdtFile Parse(const std::vector<uint8_t>& data, const std::string& what = std::string());
};
} // namespace world
} // namespace wmv

#endif
