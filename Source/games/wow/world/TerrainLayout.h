/*
 * TerrainLayout.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/TerrainLayout.cs)
 *
 * The proven terrain geometry rules, in the GAME frame (x, y horizontal, z up), double precision. No engine
 * conversion and no mesh building here: later stages consume these.
 *
 * Evidence (docs/world-viewer/00-audit.md):
 *   - T = 1600/3 yd (tile), C = T/16 (chunk), U = C/8 (cell). Tile (tx, ty) is MAIN/MAID index ty*64 + tx (4.2).
 *     MCNK n of a root has index_x = n % 16, index_y = n / 16 (5.1).
 *   - Chunk origin: x = (32 - ty)*T - index_y*C, y = (32 - tx)*T - index_x*C; every stored MCNK position is within
 *     1/768 yd (0.0013021, reached) of it in all four clients (6.1). The stored float32 positions are NOT
 *     seam-consistent (0 of 13,440 seams bit-equal when built from them, 6.2), so x/y here come from integer vertex
 *     indices, evaluated the same way on both sides of every seam.
 *   - MCVT: 145 heights relative to the chunk's stored position.z; outer vertex (r, c), r, c in 0..8, is
 *     MCVT[17r + c] at (x0 - rU, y0 - cU); inner vertex (r, c), r, c in 0..7, is MCVT[17r + 9 + c] at the cell
 *     centre (x0 - (r + 1/2)U, y0 - (c + 1/2)U) (6.2). Heights are equal at every shared seam point (0 of 213,597
 *     differ by more than 1 mm). Each cell is four triangles fanned from its inner vertex (8, 6.3) -- NOT a
 *     regular square grid.
 *   - High-resolution holes: when MCNK flag 0x10000 is set, a 64-bit mask at header offset 20, bit = row*8 + col,
 *     least significant first, cell (row, col) at the inner-vertex position (5.4, 6.5). The 16-bit
 *     low-resolution field (header offset 60) was 0 in every audited chunk: its bit layout is NOT established
 *     and is not decoded.
 *
 * C# ArgumentOutOfRangeException is std::out_of_range here.
 */

#ifndef _WMV_WORLD_TERRAINLAYOUT_H_
#define _WMV_WORLD_TERRAINLAYOUT_H_

#include <cstdint>
#include <optional>

#include "WorldApi.h"

namespace wmv
{
namespace world
{
class _WMVWORLD_API_ TerrainCoordinates
{
public:
  static constexpr double TileSize = 1600.0 / 3.0;
  static constexpr double ChunkSize = TileSize / 16.0;
  static constexpr double UnitSize = ChunkSize / 8.0;
  static constexpr int TilesPerSide = 64;
  static constexpr int ChunksPerTileSide = 16;
  static constexpr int CellsPerChunkSide = 8;
  /// Vertex rows of the whole map: 64 tiles * 16 chunks * 8 cells.
  static constexpr int MapUnits = TilesPerSide * ChunksPerTileSide * CellsPerChunkSide;

  /// World x of global outer-vertex row R (R = ty*128 + iy*8 + r): (4096 - R) * U. Unchecked linear formula,
  /// valid for 0 <= R <= 8192 (fractional rows allowed, as for inner vertices); callers normally use
  /// ChunkOrigin / OuterVertex / InnerVertex, which check.
  static double XOfRow(double globalRow) { return (MapUnits / 2 - globalRow) * UnitSize; }

  /// World y of global outer-vertex column Cc (Cc = tx*128 + ix*8 + c): (4096 - Cc) * U. Unchecked, like XOfRow.
  static double YOfColumn(double globalColumn) { return (MapUnits / 2 - globalColumn) * UnitSize; }

  /// The chunk origin (its outer vertex 0,0): x = (32 - ty)T - iy C, y = (32 - tx)T - ix C, evaluated from the
  /// integer vertex index.
  static void ChunkOrigin(int tileX, int tileY, int chunkX, int chunkY, double& x, double& y);

  /// Outer vertex (row r, column c), r, c in 0..8.
  static void OuterVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, double& x, double& y);

  /// Inner vertex (row r, column c), r, c in 0..7: the centre of cell (r, c).
  static void InnerVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, double& x, double& y);

private:
  static void CheckChunk(int tileX, int tileY, int chunkX, int chunkY);
};

/// Index arithmetic of the 145-entry MCVT (and the matching MCNR/MCCV entries).
class _WMVWORLD_API_ TerrainVertexLayout
{
public:
  static constexpr int VertexCount = 145;
  static constexpr int OuterCount = 81;
  static constexpr int InnerCount = 64;

  /// Outer vertex (r, c), r, c in 0..8: entry 17r + c.
  static int Outer(int r, int c);
  /// Inner vertex (r, c), r, c in 0..7: entry 17r + 9 + c.
  static int Inner(int r, int c);
  /// True for an outer entry (r*17 + c with c <= 8), false for an inner one.
  static bool IsOuter(int index);
};

enum class HoleMaskKind
{
  /// No header was read (a malformed MCNK): nothing is known about holes.
  Unknown = 0,
  /// MCNK flag 0x10000 set: the 64-bit mask at header offset 20 (proven).
  HighResolution,
  /// Flag 0x10000 clear: only the 16-bit field at offset 60 exists, whose layout the audit could not establish
  /// (never non-zero in the corpus). Queries return nullopt.
  LowResolutionUnsupported,
};

/// A chunk's hole mask as stored, with a deterministic query.
struct _WMVWORLD_API_ TerrainHoles
{
  HoleMaskKind Kind = HoleMaskKind::Unknown;
  uint64_t HighResolutionBits = 0; // valid when Kind == HighResolution
  uint16_t LowResolutionRaw = 0;   // header offset 60, kept raw

  /// Is cell (row, col) a hole? bit = row*8 + col, LSB first. nullopt when nothing is known (Unknown) or when the
  /// mask is the unsupported low-resolution form and non-zero; false for a zero low-resolution field.
  std::optional<bool> IsHole(int row, int col) const;

  /// Whether the chunk has any hole; nullopt when the mask is Unknown.
  std::optional<bool> HasAnyHole() const;

  static bool HighResolutionBit(uint64_t mask, int row, int col);
};
} // namespace world
} // namespace wmv

#endif
