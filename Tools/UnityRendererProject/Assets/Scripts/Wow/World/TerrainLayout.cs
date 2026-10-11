// TerrainLayout.cs
//
// The proven terrain geometry rules, in the GAME frame (x, y horizontal, z up), double precision.
// No Unity conversion and no mesh building here: later stages consume these.
//
// Evidence (docs/world-viewer/00-audit.md):
//   - T = 1600/3 yd (tile), C = T/16 (chunk), U = C/8 (cell). Tile (tx, ty) is MAIN/MAID index
//     ty*64 + tx (4.2). MCNK n of a root has index_x = n % 16, index_y = n / 16 (5.1).
//   - Chunk origin: x = (32 - ty)*T - index_y*C, y = (32 - tx)*T - index_x*C; every stored MCNK
//     position is within 1/768 yd (0.0013021, reached) of it in all four clients (6.1). The stored float32 positions
//     are NOT seam-consistent (0 of 13,440 seams bit-equal when built from them, 6.2), so x/y here
//     come from integer vertex indices, evaluated the same way on both sides of every seam.
//   - MCVT: 145 heights relative to the chunk's stored position.z; outer vertex (r, c), r, c in
//     0..8, is MCVT[17r + c] at (x0 - rU, y0 - cU); inner vertex (r, c), r, c in 0..7, is
//     MCVT[17r + 9 + c] at the cell centre (x0 - (r + 1/2)U, y0 - (c + 1/2)U) (6.2). Heights are
//     equal at every shared seam point (0 of 213,597 differ by more than 1 mm). Each cell is four
//     triangles fanned from its inner vertex (8, 6.3) -- NOT a regular square grid.
//   - High-resolution holes: when MCNK flag 0x10000 is set, a 64-bit mask at header offset 20,
//     bit = row*8 + col, least significant first, cell (row, col) at the inner-vertex position
//     (5.4, 6.5). The 16-bit low-resolution field (header offset 60) was 0 in every audited chunk:
//     its bit layout is NOT established and is not decoded.

using System;

namespace Wmv.Wow.World
{
    public static class TerrainCoordinates
    {
        public const double TileSize = 1600.0 / 3.0;
        public const double ChunkSize = TileSize / 16.0;
        public const double UnitSize = ChunkSize / 8.0;
        public const int TilesPerSide = 64;
        public const int ChunksPerTileSide = 16;
        public const int CellsPerChunkSide = 8;
        /// <summary>Vertex rows of the whole map: 64 tiles * 16 chunks * 8 cells.</summary>
        public const int MapUnits = TilesPerSide * ChunksPerTileSide * CellsPerChunkSide;

        /// <summary>World x of global outer-vertex row R (R = ty*128 + iy*8 + r): (4096 - R) * U.
        /// Unchecked linear formula, valid for 0 &lt;= R &lt;= 8192 (fractional rows allowed, as for
        /// inner vertices); callers normally use ChunkOrigin / OuterVertex / InnerVertex, which check.</summary>
        public static double XOfRow(double globalRow) { return (MapUnits / 2 - globalRow) * UnitSize; }

        /// <summary>World y of global outer-vertex column Cc (Cc = tx*128 + ix*8 + c): (4096 - Cc) * U.
        /// Unchecked, like XOfRow.</summary>
        public static double YOfColumn(double globalColumn) { return (MapUnits / 2 - globalColumn) * UnitSize; }

        /// <summary>The chunk origin (its outer vertex 0,0): x = (32 - ty)T - iy C, y = (32 - tx)T - ix C,
        /// evaluated from the integer vertex index.</summary>
        public static void ChunkOrigin(int tileX, int tileY, int chunkX, int chunkY, out double x, out double y)
        {
            CheckChunk(tileX, tileY, chunkX, chunkY);
            x = XOfRow(tileY * 128 + chunkY * 8);
            y = YOfColumn(tileX * 128 + chunkX * 8);
        }

        /// <summary>Outer vertex (row r, column c), r, c in 0..8.</summary>
        public static void OuterVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, out double x, out double y)
        {
            CheckChunk(tileX, tileY, chunkX, chunkY);
            if (r < 0 || r > 8 || c < 0 || c > 8)
                throw new ArgumentOutOfRangeException("r/c", "outer vertex row/column must be 0..8");
            x = XOfRow(tileY * 128 + chunkY * 8 + r);
            y = YOfColumn(tileX * 128 + chunkX * 8 + c);
        }

        /// <summary>Inner vertex (row r, column c), r, c in 0..7: the centre of cell (r, c).</summary>
        public static void InnerVertex(int tileX, int tileY, int chunkX, int chunkY, int r, int c, out double x, out double y)
        {
            CheckChunk(tileX, tileY, chunkX, chunkY);
            if (r < 0 || r > 7 || c < 0 || c > 7)
                throw new ArgumentOutOfRangeException("r/c", "inner vertex row/column must be 0..7");
            x = XOfRow(tileY * 128 + chunkY * 8 + r + 0.5);
            y = YOfColumn(tileX * 128 + chunkX * 8 + c + 0.5);
        }

        static void CheckChunk(int tileX, int tileY, int chunkX, int chunkY)
        {
            if (tileX < 0 || tileX >= TilesPerSide || tileY < 0 || tileY >= TilesPerSide)
                throw new ArgumentOutOfRangeException("tile", "tile x/y must be 0..63");
            if (chunkX < 0 || chunkX >= ChunksPerTileSide || chunkY < 0 || chunkY >= ChunksPerTileSide)
                throw new ArgumentOutOfRangeException("chunk", "chunk x/y must be 0..15");
        }
    }

    /// <summary>Index arithmetic of the 145-entry MCVT (and the matching MCNR/MCCV entries).</summary>
    public static class TerrainVertexLayout
    {
        public const int VertexCount = 145;
        public const int OuterCount = 81;
        public const int InnerCount = 64;

        /// <summary>Outer vertex (r, c), r, c in 0..8: entry 17r + c.</summary>
        public static int Outer(int r, int c)
        {
            if (r < 0 || r > 8 || c < 0 || c > 8)
                throw new ArgumentOutOfRangeException("r/c", "outer vertex row/column must be 0..8");
            return 17 * r + c;
        }

        /// <summary>Inner vertex (r, c), r, c in 0..7: entry 17r + 9 + c.</summary>
        public static int Inner(int r, int c)
        {
            if (r < 0 || r > 7 || c < 0 || c > 7)
                throw new ArgumentOutOfRangeException("r/c", "inner vertex row/column must be 0..7");
            return 17 * r + 9 + c;
        }

        /// <summary>True for an outer entry (r*17 + c with c &lt;= 8), false for an inner one.</summary>
        public static bool IsOuter(int index)
        {
            if (index < 0 || index >= VertexCount)
                throw new ArgumentOutOfRangeException("index");
            return index % 17 < 9;
        }
    }

    public enum HoleMaskKind
    {
        /// <summary>No header was read (a malformed MCNK): nothing is known about holes.</summary>
        Unknown = 0,
        /// <summary>MCNK flag 0x10000 set: the 64-bit mask at header offset 20 (proven).</summary>
        HighResolution,
        /// <summary>Flag 0x10000 clear: only the 16-bit field at offset 60 exists, whose layout the
        /// audit could not establish (never non-zero in the corpus). Queries return null.</summary>
        LowResolutionUnsupported,
    }

    /// <summary>A chunk's hole mask as stored, with a deterministic query.</summary>
    public struct TerrainHoles
    {
        public HoleMaskKind Kind;
        public ulong HighResolutionBits;   // valid when Kind == HighResolution
        public ushort LowResolutionRaw;    // header offset 60, kept raw

        /// <summary>Is cell (row, col) a hole? bit = row*8 + col, LSB first. Null when nothing is known
        /// (Unknown) or when the mask is the unsupported low-resolution form and non-zero; false for a
        /// zero low-resolution field.</summary>
        public bool? IsHole(int row, int col)
        {
            if (row < 0 || row > 7 || col < 0 || col > 7)
                throw new ArgumentOutOfRangeException("row/col", "hole cell row/column must be 0..7");
            if (Kind == HoleMaskKind.HighResolution)
                return ((HighResolutionBits >> (row * 8 + col)) & 1UL) != 0;
            if (Kind == HoleMaskKind.LowResolutionUnsupported && LowResolutionRaw == 0)
                return false;
            return null;
        }

        /// <summary>Whether the chunk has any hole; null when the mask is Unknown.</summary>
        public bool? HasAnyHole
        {
            get
            {
                if (Kind == HoleMaskKind.Unknown)
                    return null;
                return Kind == HoleMaskKind.HighResolution ? HighResolutionBits != 0 : LowResolutionRaw != 0;
            }
        }

        public static bool HighResolutionBit(ulong mask, int row, int col)
        {
            if (row < 0 || row > 7 || col < 0 || col > 7)
                throw new ArgumentOutOfRangeException("row/col", "hole cell row/column must be 0..7");
            return ((mask >> (row * 8 + col)) & 1UL) != 0;
        }
    }
}
