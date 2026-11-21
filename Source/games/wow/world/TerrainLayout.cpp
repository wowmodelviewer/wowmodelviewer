/*
 * TerrainLayout.cpp  (port of Wow/World/TerrainLayout.cs)
 */

#include "TerrainLayout.h"

#include <stdexcept>

namespace wmv
{
namespace world
{
void TerrainCoordinates::ChunkOrigin(int tileX, int tileY, int chunkX, int chunkY, double& x, double& y)
{
  CheckChunk(tileX, tileY, chunkX, chunkY);
  x = XOfRow(tileY * 128 + chunkY * 8);
  y = YOfColumn(tileX * 128 + chunkX * 8);
}

void TerrainCoordinates::OuterVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, double& x, double& y)
{
  CheckChunk(tileX, tileY, chunkX, chunkY);
  if (r < 0 || r > 8 || c < 0 || c > 8)
    throw std::out_of_range("r/c: outer vertex row/column must be 0..8");
  x = XOfRow(tileY * 128 + chunkY * 8 + r);
  y = YOfColumn(tileX * 128 + chunkX * 8 + c);
}

void TerrainCoordinates::InnerVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, double& x, double& y)
{
  CheckChunk(tileX, tileY, chunkX, chunkY);
  if (r < 0 || r > 7 || c < 0 || c > 7)
    throw std::out_of_range("r/c: inner vertex row/column must be 0..7");
  x = XOfRow(tileY * 128 + chunkY * 8 + r + 0.5);
  y = YOfColumn(tileX * 128 + chunkX * 8 + c + 0.5);
}

void TerrainCoordinates::CheckChunk(int tileX, int tileY, int chunkX, int chunkY)
{
  if (tileX < 0 || tileX >= TilesPerSide || tileY < 0 || tileY >= TilesPerSide)
    throw std::out_of_range("tile: tile x/y must be 0..63");
  if (chunkX < 0 || chunkX >= ChunksPerTileSide || chunkY < 0 || chunkY >= ChunksPerTileSide)
    throw std::out_of_range("chunk: chunk x/y must be 0..15");
}

int TerrainVertexLayout::Outer(int r, int c)
{
  if (r < 0 || r > 8 || c < 0 || c > 8)
    throw std::out_of_range("r/c: outer vertex row/column must be 0..8");
  return 17 * r + c;
}

int TerrainVertexLayout::Inner(int r, int c)
{
  if (r < 0 || r > 7 || c < 0 || c > 7)
    throw std::out_of_range("r/c: inner vertex row/column must be 0..7");
  return 17 * r + 9 + c;
}

bool TerrainVertexLayout::IsOuter(int index)
{
  if (index < 0 || index >= VertexCount)
    throw std::out_of_range("index");
  return index % 17 < 9;
}

std::optional<bool> TerrainHoles::IsHole(int row, int col) const
{
  if (row < 0 || row > 7 || col < 0 || col > 7)
    throw std::out_of_range("row/col: hole cell row/column must be 0..7");
  if (Kind == HoleMaskKind::HighResolution)
    return ((HighResolutionBits >> (row * 8 + col)) & 1ULL) != 0;
  if (Kind == HoleMaskKind::LowResolutionUnsupported && LowResolutionRaw == 0)
    return false;
  return std::nullopt;
}

std::optional<bool> TerrainHoles::HasAnyHole() const
{
  if (Kind == HoleMaskKind::Unknown)
    return std::nullopt;
  return Kind == HoleMaskKind::HighResolution ? HighResolutionBits != 0 : LowResolutionRaw != 0;
}

bool TerrainHoles::HighResolutionBit(uint64_t mask, int row, int col)
{
  if (row < 0 || row > 7 || col < 0 || col > 7)
    throw std::out_of_range("row/col: hole cell row/column must be 0..7");
  return ((mask >> (row * 8 + col)) & 1ULL) != 0;
}
} // namespace world
} // namespace wmv
