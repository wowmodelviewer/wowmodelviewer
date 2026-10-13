// TerrainMesh.cs
//
// Mesh data for one root ADT tile: the game frame -> Unity conversion of the world and the terrain
// mesh builder. Pure C#, no UnityEngine: the player turns the arrays into Unity meshes; the console
// runner and the Python reference (Tools/world-audit/terrain_reference.py) check them.
//
// The single conversion (stage 03 design, section 2). Game frame: x, y horizontal, z up, yards
// (TerrainLayout.cs). Unity = M(x, y, z) = (-y, z, x), scale 1, determinant -1 -- the same map as
// WowCoordinateConverter, so placed models (MDDF/MODF, converted with that map) land on this ground.
// Positions are made relative to an origin in double before the float cast:
//   T = 1600/3, U = T/128
//   outer vertex (r, c), r, c in 0..8, of chunk (ix, iy) of tile (tx, ty):
//       R = 128 ty + 8 iy + r,  Cc = 128 tx + 8 ix + c,  X = (4096 - R) U,  Y = (4096 - Cc) U
//   inner vertex (r, c), r, c in 0..7:  X = (4095.5 - R) U,  Y = (4095.5 - Cc) U
//   Z = (double)StoredPosition.C + (double)Heights[k]   (k = 17r + c outer, 17r + 9 + c inner)
//   tile origin (its north-west corner): X0 = (4096 - 128 ty) U = (32 - ty) T, Y0 = (4096 - 128 tx) U, Z0 = 0
//   Unity, origin-local: ux = (float)(-(Y - Y0)), uy = (float)(Z - Z0), uz = (float)(X - X0)
// so Unity +X = east, +Z = north, and the tile spans ux 0..T, uz -T..0. The stored MCNK x/y are never
// used for vertices (they are within 1/768 yd of the formula but not seam-consistent, 00-audit 6.1-6.2).
// Origin-local floats are exact on the tile's first row and column and within 2.1e-5 yd elsewhere;
// absolute coordinates would be off by up to 6.5e-4 yd at the map edges.
//
// Cells and winding. Cell (r, c) is four triangles fanned from its inner vertex (00-audit 6.3), with
// a = Outer(r, c), b = Outer(r, c+1), cc = Outer(r+1, c), d = Outer(r+1, c+1), m = Inner(r, c), in
// Unity index order (m, a, b), (m, b, d), (m, d, cc), (m, cc, a): Cross(v1 - v0, v2 - v0) points up
// (+Y) for every triangle, so the front faces look up. Cross.y is fixed by the grid at U^2/2.
//
// Normals are GEOMETRY-DERIVED DIAGNOSTIC normals, not the file's MCNR (whose decode is unresolved,
// audit U9): face normal = Cross(p1 - p0, p2 - p0) on the double Unity-space positions in the mesh's
// own index order, accumulated unnormalised (area-weighted) per vertex over the KEPT triangles,
// normalised, cast to float. No handedness conversion is applied to them: converting a world-frame
// cross product with WorldToUnity.Direction gives the opposite direction (M has determinant -1);
// Direction is for true vectors only. Outer vertices are accumulated tile-wide (keyed by the tile's
// 129 x 129 outer grid), so the copies of a vertex on a chunk border are bit-equal; inner vertices
// belong to one cell. A vertex with no kept triangle gets (0, 1, 0). Tile-edge normals are one-sided
// (the multi-tile stage re-accumulates them across loaded neighbours).
//
// Rules of the build (design section 3):
//   - 256 chunks in file order, chunk n at (n % 16, n / 16).
//   - Tile identity: every Decoded chunk's stored x/y must be within 0.01 yd of its chunk corner,
//     otherwise the WHOLE build fails (TerrainTileMismatchException). A float32 generator that
//     reproduces the stored positions, f32(f32(32T) - f32((16t + i) f32(C))), is at most 1/512 yd
//     off over all 64 x 16 rows and columns; a root of another tile is off by at least C = 33.3 yd.
//   - No Decoded chunk at all: the build fails ("no decodable chunk").
//   - A chunk that is not Decoded, has no heights, or has |C| or any |C + h| beyond 65,536 yd is
//     Skipped with its reason (never placed at z = 0). A chunk whose hole mask cannot be read
//     (IsHole null: a non-zero low-resolution mask -- never in the audited corpus, but five chunks of Retail
//     map 1051 tile 31_29 have one; its bit layout is not established) is Unsupported.
//   - Hole cells (64-bit mask, bit row*8 + col, LSB first) lose their four triangles.
//   - Bounds come from the vertices the index list references (an unreferenced hole-cell vertex can
//     lie far below the drawn ground); a chunk or tile with no triangle has none.
//   - No smoothing, filling or stitching.
// Thread safety: no mutable static state (the cell table is filled once in the static constructor
// and never written again); Build may run on any thread.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>A point of the game frame (yards, double) that Unity positions are made relative to.</summary>
    public readonly struct WorldOrigin
    {
        public readonly double X, Y, Z;

        public WorldOrigin(double x, double y, double z) { X = x; Y = y; Z = z; }

        public override string ToString() { return string.Format(System.Globalization.CultureInfo.InvariantCulture, "({0:R}, {1:R}, {2:R})", X, Y, Z); }
    }

    /// <summary>The game world frame to Unity: M(x, y, z) = (-y, z, x), the map WowCoordinateConverter
    /// uses for model space, here in double and relative to an origin.</summary>
    public static class WorldToUnity
    {
        /// <summary>The north-west corner of tile (tileX, tileY): X0 = (4096 - 128 tileY) U,
        /// Y0 = (4096 - 128 tileX) U, Z0 = 0 (equal to (32 - tileY) T and (32 - tileX) T exactly).</summary>
        public static WorldOrigin TileOrigin(int tileX, int tileY)
        {
            if (tileX < 0 || tileX >= TerrainCoordinates.TilesPerSide || tileY < 0 || tileY >= TerrainCoordinates.TilesPerSide)
                throw new ArgumentOutOfRangeException("tile", "tile x/y must be 0..63");
            return new WorldOrigin(TerrainCoordinates.XOfRow(128 * tileY), TerrainCoordinates.YOfColumn(128 * tileX), 0.0);
        }

        /// <summary>A world position relative to `o`, as Unity floats: (-(y - o.Y), z - o.Z, x - o.X).</summary>
        public static void Position(double x, double y, double z, WorldOrigin o, out float ux, out float uy, out float uz)
        {
            ux = (float)(-(y - o.Y));
            uy = (float)(z - o.Z);
            uz = (float)(x - o.X);
        }

        /// <summary>Position in double (before the float cast).</summary>
        public static void PositionD(double x, double y, double z, WorldOrigin o, out double ux, out double uy, out double uz)
        {
            ux = -(y - o.Y);
            uy = z - o.Z;
            uz = x - o.X;
        }

        /// <summary>A TRUE vector (a stored normal, a light direction): (-y, z, x), no origin. Not for
        /// cross products of world positions: M has determinant -1, so such a normal would come out
        /// pointing the other way (the builder takes its cross products in Unity space instead).</summary>
        public static void Direction(double x, double y, double z, out float ux, out float uy, out float uz)
        {
            ux = (float)(-y);
            uy = (float)z;
            uz = (float)x;
        }
    }

    public enum TerrainChunkState
    {
        /// <summary>Vertices and normals were made; triangles may still be 0 (every cell a hole).</summary>
        Built,
        /// <summary>Not usable: not Decoded, no heights, or a height out of range. Reason says which.</summary>
        Skipped,
        /// <summary>Decoded, but its hole mask cannot be read (IsHole null). No triangles.</summary>
        Unsupported,
    }

    /// <summary>One MCNK as mesh data, in Unity space relative to the tile's origin.</summary>
    public sealed class TerrainChunkMesh
    {
        public int Index, IndexX, IndexY;
        public TerrainChunkState State;
        public string Reason;               // null for a Built chunk
        public float[] Positions;           // 145 * 3, origin-local Unity, MCVT order k; null unless Built
        public float[] Normals;             // 145 * 3, geometry-derived DIAGNOSTIC normals (not MCNR); null unless Built
        public ushort[] Indices;            // 3 * Triangles, front faces up; never null, length 0 without triangles
        public int Triangles;
        public int HoleCells;               // cells left out as holes (Built chunks only)
        public ulong HoleMask;              // the cells left out, bit row*8 + col (Built chunks only)
        public float[] BoundsMin, BoundsMax;   // Unity, from the REFERENCED vertices only; null when Triangles == 0
    }

    /// <summary>A root ADT tile as mesh data: 256 chunks plus totals.</summary>
    public sealed class TerrainTileMesh
    {
        public int TileX, TileY;
        public WorldOrigin Origin;          // the world point Unity (0, 0, 0) stands for
        public TerrainChunkMesh[] Chunks;   // 256, in file order
        public int ChunksBuilt, ChunksSkipped, ChunksUnsupported;
        /// <summary>Vertices of the chunks that have triangles (the ones a renderer uploads): 145 each.</summary>
        public int Vertices;
        public int Triangles, HoleCells;
        public float[] BoundsMin, BoundsMax;   // over the chunks with triangles; null when the tile has none
        /// <summary>Vertex (position + normal, 24 bytes each) plus 16-bit index bytes of the chunks with triangles.</summary>
        public long MeshBytes;
    }

    /// <summary>The whole terrain build failed (no part of the tile is usable).</summary>
    public class TerrainBuildException : Exception
    {
        public TerrainBuildException(string message) : base(message) { }
    }

    /// <summary>The root's stored chunk positions do not belong to the tile it was asked to build.</summary>
    public sealed class TerrainTileMismatchException : TerrainBuildException
    {
        public readonly int TileX, TileY, ChunkIndex;
        public readonly double StoredX, StoredY, ExpectedX, ExpectedY;

        public TerrainTileMismatchException(int tileX, int tileY, int chunkIndex, double storedX, double storedY, double expectedX, double expectedY)
            : base(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "root ADT does not belong to tile {0}_{1}: MCNK {2} is stored at ({3:0.####}, {4:0.####}), its corner on this tile is " +
                "({5:0.####}, {6:0.####}), more than {7} yd apart", tileX, tileY, chunkIndex, storedX, storedY, expectedX, expectedY,
                TerrainMeshBuilder.IdentityTolerance))
        {
            TileX = tileX;
            TileY = tileY;
            ChunkIndex = chunkIndex;
            StoredX = storedX;
            StoredY = storedY;
            ExpectedX = expectedX;
            ExpectedY = expectedY;
        }
    }

    public static class TerrainMeshBuilder
    {
        public const int VerticesPerChunk = 145, TrianglesPerCell = 4, CellsPerChunk = 64, TrianglesPerChunk = 256;
        public const double IdentityTolerance = 0.01, MaxAbsHeight = 65536.0;
        /// <summary>Outer vertices per tile side (16 chunks x 8 cells + 1).</summary>
        public const int OuterGridSide = 129;

        // The 12 MCVT indices of each cell's four triangles, cell (r, c) at [(r*8 + c) * 12].
        static readonly int[] CellTable;

        static TerrainMeshBuilder()
        {
            var t = new int[CellsPerChunk * 12];
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                {
                    int a = TerrainVertexLayout.Outer(r, c), b = TerrainVertexLayout.Outer(r, c + 1);
                    int cc = TerrainVertexLayout.Outer(r + 1, c), d = TerrainVertexLayout.Outer(r + 1, c + 1);
                    int m = TerrainVertexLayout.Inner(r, c);
                    int o = (r * 8 + c) * 12;
                    t[o + 0] = m; t[o + 1] = a; t[o + 2] = b;
                    t[o + 3] = m; t[o + 4] = b; t[o + 5] = d;
                    t[o + 6] = m; t[o + 7] = d; t[o + 8] = cc;
                    t[o + 9] = m; t[o + 10] = cc; t[o + 11] = a;
                }
            CellTable = t;
        }

        /// <summary>The 12 MCVT indices of cell (r, c)'s four triangles, in Unity index order:
        /// (m, a, b), (m, b, d), (m, d, cc), (m, cc, a).</summary>
        public static void CellIndices(int r, int c, int[] out12)
        {
            if (r < 0 || r > 7 || c < 0 || c > 7)
                throw new ArgumentOutOfRangeException("r/c", "cell row/column must be 0..7");
            if (out12 == null || out12.Length < 12)
                throw new ArgumentException("needs room for 12 indices", "out12");
            Array.Copy(CellTable, (r * 8 + c) * 12, out12, 0, 12);
        }

        /// <summary>World x/y (game frame, double) of MCVT entry k of chunk (chunkX, chunkY) of tile
        /// (tileX, tileY), by the pinned expressions: (4096 - R) U / (4095.5 - R) U.</summary>
        public static void VertexWorldXY(int tileX, int tileY, int chunkX, int chunkY, int k, out double x, out double y)
        {
            if (tileX < 0 || tileX >= TerrainCoordinates.TilesPerSide || tileY < 0 || tileY >= TerrainCoordinates.TilesPerSide)
                throw new ArgumentOutOfRangeException("tile", "tile x/y must be 0..63");
            if (chunkX < 0 || chunkX >= TerrainCoordinates.ChunksPerTileSide || chunkY < 0 || chunkY >= TerrainCoordinates.ChunksPerTileSide)
                throw new ArgumentOutOfRangeException("chunk", "chunk x/y must be 0..15");
            if (k < 0 || k >= VerticesPerChunk)
                throw new ArgumentOutOfRangeException("k", "MCVT entry must be 0..144");
            VertexXY(tileX, tileY, chunkX, chunkY, k, out x, out y);
        }

        static void VertexXY(int tileX, int tileY, int chunkX, int chunkY, int k, out double x, out double y)
        {
            int r = k / 17, j = k % 17;
            const double U = TerrainCoordinates.UnitSize;
            if (j < 9)
            {
                int row = 128 * tileY + 8 * chunkY + r, col = 128 * tileX + 8 * chunkX + j;
                x = (4096 - row) * U;
                y = (4096 - col) * U;
            }
            else
            {
                int row = 128 * tileY + 8 * chunkY + r, col = 128 * tileX + 8 * chunkX + (j - 9);
                x = (4095.5 - row) * U;
                y = (4095.5 - col) * U;
            }
        }

        public static TerrainTileMesh Build(AdtRoot root, int tileX, int tileY, WorldOrigin origin)
        {
            if (tileX < 0 || tileX >= TerrainCoordinates.TilesPerSide || tileY < 0 || tileY >= TerrainCoordinates.TilesPerSide)
                throw new ArgumentOutOfRangeException("tile", "tile x/y must be 0..63");
            if (root == null)
                throw new ArgumentNullException("root");
            if (root.Mcnk == null || root.Mcnk.Length != AdtParser.ChunksPerTile)
                throw new ArgumentException(string.Format("a root ADT has 256 MCNKs, this one has {0}", root.Mcnk == null ? 0 : root.Mcnk.Length), "root");

            // Pass 1: tile identity over every Decoded chunk; the whole build fails on any mismatch.
            int decoded = 0;
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                AdtRootChunk mc = root.Mcnk[n];
                if (mc == null || mc.Status != WorldDataStatus.Decoded)
                    continue;
                decoded++;
                double ex, ey;
                TerrainCoordinates.ChunkOrigin(tileX, tileY, n % 16, n / 16, out ex, out ey);
                double sx = mc.StoredPosition.A, sy = mc.StoredPosition.B;
                if (!(Math.Abs(sx - ex) <= IdentityTolerance) || !(Math.Abs(sy - ey) <= IdentityTolerance))
                    throw new TerrainTileMismatchException(tileX, tileY, n, sx, sy, ex, ey);
            }
            if (decoded == 0)
                throw new TerrainBuildException("no decodable chunk: none of the root's 256 MCNKs decoded (see the root's issues)");

            var tile = new TerrainTileMesh { TileX = tileX, TileY = tileY, Origin = origin, Chunks = new TerrainChunkMesh[AdtParser.ChunksPerTile] };
            var unity = new double[AdtParser.ChunksPerTile][];      // double Unity positions of Built chunks (for normals)

            // Pass 2: state, positions and kept triangles per chunk.
            var kept = new List<ushort>(TrianglesPerChunk * 3);
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                AdtRootChunk mc = root.Mcnk[n];
                var ch = new TerrainChunkMesh { Index = n, IndexX = n % 16, IndexY = n / 16, Indices = new ushort[0] };
                tile.Chunks[n] = ch;
                string why = ChunkProblem(root, mc, n);
                if (why != null)
                {
                    ch.State = TerrainChunkState.Skipped;
                    ch.Reason = why;
                    continue;
                }
                ulong mask = 0;
                string unsupported = null;
                for (int r = 0; r < 8 && unsupported == null; r++)
                    for (int c = 0; c < 8; c++)
                    {
                        bool? hole = mc.Holes.IsHole(r, c);
                        if (hole == null)
                        {
                            unsupported = mc.Holes.Kind == HoleMaskKind.LowResolutionUnsupported
                                ? string.Format("hole mask not decodable: MCNK flag 0x10000 is clear and the low-resolution mask 0x{0:X4} is non-zero (its layout is not established)",
                                                mc.Holes.LowResolutionRaw)
                                : "hole mask unknown (the MCNK header was not read)";
                            break;
                        }
                        if (hole == true)
                            mask |= 1UL << (r * 8 + c);
                    }
                if (unsupported != null)
                {
                    ch.State = TerrainChunkState.Unsupported;
                    ch.Reason = unsupported;
                    continue;
                }

                ch.State = TerrainChunkState.Built;
                var pd = new double[VerticesPerChunk * 3];
                var pf = new float[VerticesPerChunk * 3];
                double baseZ = mc.StoredPosition.C;
                for (int k = 0; k < VerticesPerChunk; k++)
                {
                    double x, y;
                    VertexXY(tileX, tileY, ch.IndexX, ch.IndexY, k, out x, out y);
                    double z = baseZ + (double)mc.Heights[k];
                    double ux, uy, uz;
                    WorldToUnity.PositionD(x, y, z, origin, out ux, out uy, out uz);
                    pd[3 * k] = ux; pd[3 * k + 1] = uy; pd[3 * k + 2] = uz;
                    pf[3 * k] = (float)ux; pf[3 * k + 1] = (float)uy; pf[3 * k + 2] = (float)uz;
                }
                unity[n] = pd;
                ch.Positions = pf;

                kept.Clear();
                int holes = 0;
                for (int cell = 0; cell < CellsPerChunk; cell++)
                {
                    if (((mask >> cell) & 1UL) != 0)
                    {
                        holes++;
                        continue;
                    }
                    for (int i = 0; i < 12; i++)
                    {
                        int idx = CellTable[cell * 12 + i];
                        if (idx < 0 || idx >= VerticesPerChunk)
                            throw new InvalidOperationException("terrain cell table holds index " + idx + " outside 0..144");
                        kept.Add((ushort)idx);
                    }
                }
                ch.Indices = kept.ToArray();
                ch.Triangles = ch.Indices.Length / 3;
                ch.HoleCells = holes;
                ch.HoleMask = mask;
            }

            // Pass 3: tile-wide area-weighted normals from the kept triangles, Unity space, mesh order.
            var outer = new double[OuterGridSide * OuterGridSide * 3];
            var inner = new double[AdtParser.ChunksPerTile][];
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                TerrainChunkMesh ch = tile.Chunks[n];
                if (ch.State != TerrainChunkState.Built)
                    continue;
                double[] p = unity[n];
                double[] inn = inner[n] = new double[CellsPerChunk * 3];
                ushort[] ix = ch.Indices;
                for (int t = 0; t < ix.Length; t += 3)
                {
                    int i0 = ix[t], i1 = ix[t + 1], i2 = ix[t + 2];
                    double e1x = p[3 * i1] - p[3 * i0], e1y = p[3 * i1 + 1] - p[3 * i0 + 1], e1z = p[3 * i1 + 2] - p[3 * i0 + 2];
                    double e2x = p[3 * i2] - p[3 * i0], e2y = p[3 * i2 + 1] - p[3 * i0 + 1], e2z = p[3 * i2 + 2] - p[3 * i0 + 2];
                    double nx = e1y * e2z - e1z * e2y;
                    double ny = e1z * e2x - e1x * e2z;
                    double nz = e1x * e2y - e1y * e2x;
                    Accumulate(outer, inn, ch, i0, nx, ny, nz);
                    Accumulate(outer, inn, ch, i1, nx, ny, nz);
                    Accumulate(outer, inn, ch, i2, nx, ny, nz);
                }
            }
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                TerrainChunkMesh ch = tile.Chunks[n];
                if (ch.State != TerrainChunkState.Built)
                    continue;
                var nf = new float[VerticesPerChunk * 3];
                for (int k = 0; k < VerticesPerChunk; k++)
                {
                    double[] acc;
                    int o;
                    Slot(outer, inner[n], ch, k, out acc, out o);
                    double ax = acc[o], ay = acc[o + 1], az = acc[o + 2];
                    double len = Math.Sqrt(ax * ax + ay * ay + az * az);
                    if (len > 0)
                    {
                        nf[3 * k] = (float)(ax / len);
                        nf[3 * k + 1] = (float)(ay / len);
                        nf[3 * k + 2] = (float)(az / len);
                    }
                    else
                    {
                        nf[3 * k] = 0f;
                        nf[3 * k + 1] = 1f;
                        nf[3 * k + 2] = 0f;
                    }
                }
                ch.Normals = nf;
            }

            // Bounds (referenced vertices only) and totals.
            var used = new bool[VerticesPerChunk];
            foreach (TerrainChunkMesh ch in tile.Chunks)
            {
                switch (ch.State)
                {
                    case TerrainChunkState.Built: tile.ChunksBuilt++; break;
                    case TerrainChunkState.Skipped: tile.ChunksSkipped++; continue;
                    default: tile.ChunksUnsupported++; continue;
                }
                tile.HoleCells += ch.HoleCells;
                if (ch.Triangles == 0)
                    continue;
                Array.Clear(used, 0, used.Length);
                foreach (ushort i in ch.Indices)
                    used[i] = true;
                var mn = new float[] { float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity };
                var mx = new float[] { float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity };
                for (int k = 0; k < VerticesPerChunk; k++)
                {
                    if (!used[k])
                        continue;
                    for (int a = 0; a < 3; a++)
                    {
                        float v = ch.Positions[3 * k + a];
                        if (v < mn[a]) mn[a] = v;
                        if (v > mx[a]) mx[a] = v;
                    }
                }
                ch.BoundsMin = mn;
                ch.BoundsMax = mx;
                if (tile.BoundsMin == null)
                {
                    tile.BoundsMin = (float[])mn.Clone();
                    tile.BoundsMax = (float[])mx.Clone();
                }
                else
                    for (int a = 0; a < 3; a++)
                    {
                        if (mn[a] < tile.BoundsMin[a]) tile.BoundsMin[a] = mn[a];
                        if (mx[a] > tile.BoundsMax[a]) tile.BoundsMax[a] = mx[a];
                    }
                tile.Vertices += VerticesPerChunk;
                tile.Triangles += ch.Triangles;
                tile.MeshBytes += VerticesPerChunk * 24L + ch.Indices.Length * 2L;
            }
            return tile;
        }

        /// <summary>Why a chunk cannot be built, or null when it can.</summary>
        static string ChunkProblem(AdtRoot root, AdtRootChunk mc, int n)
        {
            if (mc == null)
                return "MCNK " + n + " is missing";
            if (mc.Status != WorldDataStatus.Decoded)
            {
                string label = "MCNK " + n;
                if (root.Issues != null)
                    foreach (WorldIssue issue in root.Issues)
                        if (issue != null && issue.Kind == WorldIssueKind.Malformed && issue.Chunk != null &&
                            (issue.Chunk == label || issue.Chunk.StartsWith(label + " ", StringComparison.Ordinal)))
                            return label + " is malformed: " + issue.Reason;
                return label + " is " + mc.Status + " (see the root's issues)";
            }
            if (mc.Heights == null || mc.Heights.Length != VerticesPerChunk)
                return "MCNK " + n + " has no heights (MCVT)";
            double baseZ = mc.StoredPosition.C;
            if (!(Math.Abs(baseZ) <= MaxAbsHeight))
                return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "height out of range: MCNK {0} base height {1:R} is beyond +-{2} yd", n, baseZ, MaxAbsHeight);
            for (int k = 0; k < VerticesPerChunk; k++)
            {
                double z = baseZ + (double)mc.Heights[k];
                if (!(Math.Abs(z) <= MaxAbsHeight))
                    return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                        "height out of range: MCNK {0} vertex {1} lies at z = {2:R}, beyond +-{3} yd", n, k, z, MaxAbsHeight);
            }
            return null;
        }

        /// <summary>The accumulator of MCVT entry k of a chunk: the tile's outer grid (shared by the
        /// chunks that meet there) or the chunk's own inner slot.</summary>
        static void Slot(double[] outer, double[] inner, TerrainChunkMesh ch, int k, out double[] acc, out int offset)
        {
            int r = k / 17, j = k % 17;
            if (j < 9)
            {
                acc = outer;
                offset = ((8 * ch.IndexY + r) * OuterGridSide + 8 * ch.IndexX + j) * 3;
            }
            else
            {
                acc = inner;
                offset = (r * 8 + (j - 9)) * 3;
            }
        }

        static void Accumulate(double[] outer, double[] inner, TerrainChunkMesh ch, int k, double nx, double ny, double nz)
        {
            double[] acc;
            int o;
            Slot(outer, inner, ch, k, out acc, out o);
            acc[o] += nx;
            acc[o + 1] += ny;
            acc[o + 2] += nz;
        }
    }
}
